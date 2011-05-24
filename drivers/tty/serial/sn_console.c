/*
 * C-Brick Serial Port (and console) driver for SGI Altix machines.
 *
 * This driver is NOT suitable for talking to the l1-controller for
 * anything other than 'console activities' --- please use the l1
 * driver for that.
 *
 *
 * Copyright (c) 2004-2006 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information:  Silicon Graphics, Inc., 1500 Crittenden Lane,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/sysrq.h>
#include <linux/circ_buf.h>
#include <linux/serial_reg.h>
#include <linux/delay.h> /* for mdelay */
#include <linux/miscdevice.h>
#include <linux/serial_core.h>

#include <asm/io.h>
#include <asm/sn/simulator.h>
#include <asm/sn/sn_sal.h>

/* number of characters we can transmit to the SAL console at a time */
#define SN_SAL_MAX_CHARS 120

/* 64K, when we're asynch, it must be at least printk's LOG_BUF_LEN to
 * avoid losing chars, (always has to be a power of 2) */
#define SN_SAL_BUFFER_SIZE (64 * (1 << 10))

#define SN_SAL_UART_FIFO_DEPTH 16
#define SN_SAL_UART_FIFO_SPEED_CPS (9600/10)

/* sn_transmit_chars() calling args */
#define TRANSMIT_BUFFERED	0
#define TRANSMIT_RAW		1

/* To use dynamic numbers only and not use the assigned major and minor,
 * define the following.. */
				  /* #define USE_DYNAMIC_MINOR 1 *//* use dynamic minor number */
#define USE_DYNAMIC_MINOR 0	/* Don't rely on misc_register dynamic minor */

/* Device name we're using */
#define DEVICE_NAME "ttySG"
#define DEVICE_NAME_DYNAMIC "ttySG0"	/* need full name for misc_register */
/* The major/minor we are using, ignored for USE_DYNAMIC_MINOR */
#define DEVICE_MAJOR 204
#define DEVICE_MINOR 40

#ifdef CONFIG_MAGIC_SYSRQ
static char sysrq_serial_str[] = "\eSYS";
static char *sysrq_serial_ptr = sysrq_serial_str;
static unsigned long sysrq_requested;
#endif /* CONFIG_MAGIC_SYSRQ */

/*
 * Port definition - this kinda drives it all
 */
struct sn_cons_port {
	struct timer_list sc_timer;
	struct uart_port sc_port;
	struct sn_sal_ops {
		int (*sal_puts_raw) (const char *s, int len);
		int (*sal_puts) (const char *s, int len);
		int (*sal_getc) (void);
		int (*sal_input_pending) (void);
		void (*sal_wakeup_transmit) (struct sn_cons_port *, int);
	} *sc_ops;
	unsigned long sc_interrupt_timeout;
	int sc_is_asynch;
};

static struct sn_cons_port sal_console_port;
static int sn_process_input;

/* Only used if USE_DYNAMIC_MINOR is set to 1 */
static struct miscdevice misc;	/* used with misc_register for dynamic */

extern void early_sn_setup(void);

#undef DEBUG
#ifdef DEBUG
static int sn_debug_printf(const char *fmt, ...);
#define DPRINTF(x...) sn_debug_printf(x)
#else
#define DPRINTF(x...) do { } while (0)
#endif

/* Prototypes */
static int snt_hw_puts_raw(const char *, int);
static int snt_hw_puts_buffered(const char *, int);
static int snt_poll_getc(void);
static int snt_poll_input_pending(void);
static int snt_intr_getc(void);
static int snt_intr_input_pending(void);
static void sn_transmit_chars(struct sn_cons_port *, int);

/* A table for polling:
 */
static struct sn_sal_ops poll_ops = {
	.sal_puts_raw = snt_hw_puts_raw,
	.sal_puts = snt_hw_puts_raw,
	.sal_getc = snt_poll_getc,
	.sal_input_pending = snt_poll_input_pending
};

/* A table for interrupts enabled */
static struct sn_sal_ops intr_ops = {
	.sal_puts_raw = snt_hw_puts_raw,
	.sal_puts = snt_hw_puts_buffered,
	.sal_getc = snt_intr_getc,
	.sal_input_pending = snt_intr_input_pending,
	.sal_wakeup_transmit = sn_transmit_chars
};

/* the console does output in two distinctly different ways:
 * synchronous (raw) and asynchronous (buffered).  initially, early_printk
 * does synchronous output.  any data written goes directly to the SAL
 * to be output (incidentally, it is internally buffered by the SAL)
 * after interrupts and timers are initialized and available for use,
 * the console init code switches to asynchronous output.  this is
 * also the earliest opportunity to begin polling for console input.
 * after console initialization, console output and tty (serial port)
 * output is buffered and sent to the SAL asynchronously (either by
 * timer callback or by UART interrupt) */

/* routines for running the console in polling mode */

/**
 * snt_poll_getc - Get a character from the console in polling mode
 *
 */
static int snt_poll_getc(void)
{
	int ch;

	ia64_sn_console_getc(&ch);
	return ch;
}

/**
 * snt_poll_input_pending - Check if any input is waiting - polling mode.
 *
 */
static int snt_poll_input_pending(void)
{
	int status, input;

	status = ia64_sn_console_check(&input);
	return !status && input;
}

/* routines for an interrupt driven console (normal) */

/**
 * snt_intr_getc - Get a character from the console, interrupt mode
 *
 */
static int snt_intr_getc(void)
{
	return ia64_sn_console_readc();
}

/**
 * snt_intr_input_pending - Check if input is pending, interrupt mode
 *
 */
static int snt_intr_input_pending(void)
{
	return ia64_sn_console_intr_status() & SAL_CONSOLE_INTR_RECV;
}

/* these functions are polled and interrupt */

/**
 * snt_hw_puts_raw - Send raw string to the console, polled or interrupt mode
 * @s: String
 * @len: Length
 *
 */
static int snt_hw_puts_raw(const char *s, int len)
{
	/* this will call the PROM and not return until this is done */
	return ia64_sn_console_putb(s, len);
}

/**
 * snt_hw_puts_buffered - Send string to console, polled or interrupt mode
 * @s: String
 * @len: Length
 *
 */
static int snt_hw_puts_buffered(const char *s, int len)
{
	/* queue data to the PROM */
	return ia64_sn_console_xmit_chars((char *)s, len);
}

/* uart interface structs
 * These functions are associated with the uart_port that the serial core
 * infrastructure calls.
 *
 * Note: Due to how the console works, many routines are no-ops.
 */

/**
 * snp_type - What type of console are we?
 * @port: Port to operate with (we ignore since we only have one port)
 *
 */
static const char *snp_type(struct uart_port *port)
{
	return ("SGI SN L1");
}

/**
 * snp_tx_empty - Is the transmitter empty?  We pretend we're always empty
 * @port: Port to operate on (we ignore since we only have one port)
 *
 */
static unsigned int snp_tx_empty(struct uart_port *port)
{
	return 1;
}

/**
 * snp_stop_tx - stop the transmitter - no-op for us
 * @port: Port to operat eon - we ignore - no-op function
 *
 */
static void snp_stop_tx(struct uart_port *port)
{
}

/**
 * snp_release_port - Free i/o and resources for port - no-op for us
 * @port: Port to operate on - we ignore - no-op function
 *
 */
static void snp_release_port(struct uart_port *port)
{
}

/**
 * snp_enable_ms - Force modem status interrupts on - no-op for us
 * @port: Port to operate on - we ignore - no-op function
 *
 */
static void snp_enable_ms(struct uart_port *port)
{
}

/**
 * snp_shutdown - shut down the port - free irq and disable - no-op for us
 * @port: Port to shut down - we ignore
 *
 */
static void snp_shutdown(struct uart_port *port)
{
}

/**
 * snp_set_mctrl - set control lines (dtr, rts, etc) - no-op for our console
 * @port: Port to operate on - we ignore
 * @mctrl: Lines to set/unset - we ignore
 *
 */
static void snp_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

/**
 * snp_get_mctrl - get contorl line info, we just return a static value
 * @port: port to operate on - we only have one port so we ignore this
 *
 */
static unsigned int snp_get_mctrl(struct uart_port *port)
{
	return TIOCM_CAR | TIOCM_RNG | TIOCM_DSR | TIOCM_CTS;
}

/**
 * snp_stop_rx - Stop the receiver - we ignor ethis
 * @port: Port to operate on - we ignore
 *
 */
static void snp_stop_rx(struct uart_port *port)
{
}

/**
 * snp_start_tx - Start transmitter
 * @port: Port to operate on
 *
 */
static void snp_start_tx(struct uart_port *port)
{
	if (sal_console_port.sc_ops->sal_wakeup_transmit)
		sal_console_port.sc_ops->sal_wakeup_transmit(&sal_console_port,
							     TRANSMIT_BUFFERED);

}

/**
 * snp_break_ctl - handle breaks - ignored by us
 * @port: Port to operate on
 * @break_state: Break state
 *
 */
static void snp_break_ctl(struct uart_port *port, int break_state)
{
}

/**
 * snp_startup - Start up the serial port - always return 0 (We're always on)
 * @port: Port to operate on
 *
 */
static int snp_startup(struct uart_port *port)
{
	return 0;
}

/**
 * snp_set_termios - set termios stuff - we ignore these
 * @port: port to operate on
 * @termios: New settings
 * @termios: Old
 *
 */
static void
snp_set_termios(struct uart_port *port, struct ktermios *termios,
		struct ktermios *old)
{
}

/**
 * snp_request_port - allocate resources for port - ignored by us
 * @port: port to operate on
 *
 */
static int snp_request_port(struct uart_port *port)
{
	return 0;
}

/**
 * snp_config_port - allocate resources, set up - we ignore,  we're always on
 * @port: Port to operate on
 * @flags: flags used for port setup
 *
 */
static void snp_config_port(struct uart_port *port, int flags)
{
}

/* Associate the uart functions above - given to serial core */

static struct uart_ops sn_console_ops = {
	.tx_empty = snp_tx_empty,
	.set_mctrl = snp_set_mctrl,
	.get_mctrl = snp_get_mctrl,
	.stop_tx = snp_stop_tx,
	.start_tx = snp_start_tx,
	.stop_rx = snp_stop_rx,
	.enable_ms = snp_enable_ms,
	.break_ctl = snp_break_ctl,
	.startup = snp_startup,
	.shutdown = snp_shutdown,
	.set_termios = snp_set_termios,
	.pm = NULL,
	.type = snp_type,
	.release_port = snp_release_port,
	.request_port = snp_request_port,
	.config_port = snp_config_port,
	.verify_port = NULL,
};

/* End of uart struct functions and defines */

#ifdef DEBUG

/**
 * sn_debug_printf - close to hardware debugging printf
 * @fmt: printf format
 *
 * This is as "close to the metal" as we can get, used when the driver
 * itself may be broken.
 *
 */
static int sn_debug_printf(const char *fmt, ...)
{
	static char printk_buf[1024];
	int printed_len;
	va_list args;

	va_start(args, fmt);
	printed_len = vsnprintf(printk_buf, sizeof(printk_buf), fmt, args);

	if (!sal_console_port.sc_ops) {
		sal_console_port.sc_ops = &poll_ops;
		early_sn_setup();
	}
	sal_console_port.sc_ops->sal_puts_raw(printk_buf, printed_len);

	va_end(args);
	return printed_len;
}
#endif				/* DEBUG */

/*
 * Interrupt handling routines.
 */

/**
 * sn_receive_chars - Grab characters, pass them to tty layer
 * @port: Port to operate on
 * @flags: irq flags
 *
 * Note: If we're not registered with the serial core infrastructure yet,
 * we don't try to send characters to it...
 *
 */
static void
sn_receive_chars(struct sn_cons_port *port, unsigned long flags)
{
	int ch;
	struct tty_struct *tty;

	if (!port) {
		printk(KERN_ERR "sn_receive_chars - port NULL so can't receieve\n");
		return;
	}

	if (!port->sc_ops) {
		printk(KERN_ERR "sn_receive_chars - port->sc_ops  NULL so can't receieve\n");
		return;
	}

	if (port->sc_port.state) {
		/* The serial_core stuffs are initialized, use them */
		tty = port->sc_port.state->port.tty;
	}
	else {
		/* Not registered yet - can't pass to tty layer.  */
		tty = NULL;
	}

	while (port->sc_ops->sal_input_pending()) {
		ch = port->sc_ops->sal_getc();
		if (ch < 0) {
			printk(KERN_ERR "sn_console: An error occurred while "
			       "obtaining data from the console (0x%0x)\n", ch);
			break;
		}
#ifdef CONFIG_MAGIC_SYSRQ
                if (sysrq_requested) {
                        unsigned long sysrq_timeout = sysrq_requested + HZ*5;

                        sysrq_requested = 0;
                        if (ch && time_before(jiffies, sysrq_timeout)) {
                                spin_unlock_irqrestore(&port->sc_port.lock, flags);
                                handle_sysrq(ch);
                                spin_lock_irqsave(&port->sc_port.lock, flags);
                                /* ignore actual sysrq command char */
                                continue;
                        }
                }
                if (ch == *sysrq_serial_ptr) {
                        if (!(*++sysrq_serial_ptr)) {
                                sysrq_requested = jiffies;
                                sysrq_serial_ptr = sysrq_serial_str;
                        }
			/*
			 * ignore the whole sysrq string except for the
			 * leading escape
			 */
			if (ch != '\e')
				continue;
                }
                else
			sysrq_serial_ptr = sysrq_serial_str;
#endif /* CONFIG_MAGIC_SYSRQ */

		/* record the character to pass up to the tty layer */
		if (tty) {
			if(tty_insert_flip_char(tty, ch, TTY_NORMAL) == 0)
				break;
		}
		port->sc_port.icount.rx++;
	}

	if (tty)
		tty_flip_buffer_push(tty);
}

/**
 * sn_transmit_chars - grab characters from serial core, send off
 * @port: Port to operate on
 * @raw: Transmit raw or buffered
 *
 * Note: If we're early, before we're registered with serial core, the
 * writes are going through sn_sal_console_write because that's how
 * register_console has been set up.  We currently could have asynch
 * polls calling this function due to sn_sal_switch_to_asynch but we can
 * ignore them until we register with the serial core stuffs.
 *
 */
static void sn_transmit_chars(struct sn_cons_port *port, int raw)
{
	int xmit_count, tail, head, loops, ii;
	int result;
	char *start;
	struct circ_buf *xmit;

	if (!port)
		return;

	BUG_ON(!port->sc_is_asynch);

	if (port->sc_port.state) {
		/* We're initialized, using serial core infrastructure */
		xmit = &port->sc_port.state->xmit;
	} else {
		/* Probably sn_sal_switch_to_asynch has been run but serial core isn't
		 * initialized yet.  Just return.  Writes are going through
		 * sn_sal_console_write (due to register_console) at this time.
		 */
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(&port->sc_port)) {
		/* Nothing to do. */
		ia64_sn_console_intr_disable(SAL_CONSOLE_INTR_XMIT);
		return;
	}

	head = xmit->head;
	tail = xmit->tail;
	start = &xmit->buf[tail];

	/* twice around gets the tail to the end of the buffer and
	 * then to the head, if needed */
	loops = (head < tail) ? 2 : 1;

	for (ii = 0; ii < loops; ii++) {
		xmit_count = (head < tail) ?
		    (UART_XMIT_SIZE - tail) : (head - tail);

		if (xmit_count > 0) {
			if (raw == TRANSMIT_RAW)
				result =
				    port->sc_ops->sal_puts_raw(start,
							       xmit_count);
			else
				result =
				    port->sc_ops->sal_puts(start, xmit_count);
#ifdef DEBUG
			if (!result)
				DPRINTF("`");
#endif
			if (result > 0) {
				xmit_count -= result;
				port->sc_port.icount.tx += result;
				tail += result;
				tail &= UART_XMIT_SIZE - 1;
				xmit->tail = tail;
				start = &xmit->buf[tail];
			}
		}
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&port->sc_port);

	if (uart_circ_empty(xmit))
		snp_stop_tx(&port->sc_port);	/* no-op for us */
}

/**
 * sn_sal_interrupt - Handle console interrupts
 * @irq: irq #, useful for debug statements
 * @dev_id: our pointer to our port (sn_cons_port which contains the uart port)
 *
 */
static irqreturn_t sn_sal_interrupt(int irq, void *dev_id)
{
	struct sn_cons_port *port = (struct sn_cons_port *)dev_id;
	unsigned long flags;
	int status = ia64_sn_console_intr_status();

	if (!port)
		return IRQ_NONE;

	spin_lock_irqsave(&port->sc_port.lock, flags);
	if (status & SAL_CONSOLE_INTR_RECV) {
		sn_receive_chars(port, flags);
	}
	if (status & SAL_CONSOLE_INTR_XMIT) {
		sn_transmit_chars(port, TRANSMIT_BUFFERED);
	}
	spin_unlock_irqrestore(&port->sc_port.lock, flags);
	return IRQ_HANDLED;
}

/**
 * sn_sal_timer_poll - this function handles polled console mode
 * @data: A pointer to our sn_cons_port (which contains the uart port)
 *
 * data is the pointer that init_timer will store for us.  This function is
 * associated with init_timer to see if there is any console traffic.
 * Obviously not used in interrupt mode
 *
 */
static void sn_sal_timer_poll(unsigned long data)
{
	struct sn_cons_port *port = (struct sn_cons_port *)data;
	unsigned long flags;

	if (!port)
		return;

	if (!port->sc_port.irq) {
		spin_lock_irqsave(&port->sc_port.lock, flags);
		if (sn_process_input)
			sn_receive_chars(port, flags);
		sn_transmit_chars(port, TRANSMIT_RAW);
		spin_unlock_irqrestore(&port->sc_port.lock, flags);
		mod_timer(&port->sc_timer,
			  jiffies + port->sc_interrupt_timeout);
	}
}

/*
 * Boot-time initialization code
 */

/**
 * sn_sal_switch_to_asynch - Switch to async mode (as opposed to synch)
 * @port: Our sn_cons_port (which contains the uart port)
 *
 * So this is used by sn_sal_serial_console_init (early on, before we're
 * registered with serial core).  It's also used by sn_sal_module_init
 * right after we've registered with serial core.  The later only happens
 * if we didn't already come through here via sn_sal_serial_console_init.
 *
 */
static void __init sn_sal_switch_to_asynch(struct sn_cons_port *port)
{
	unsigned long flags;

	if (!port)
		return;

	DPRINTF("sn_console: about to switch to asynchronous console\n");

	/* without early_printk, we may be invoked late enough to race
	 * with other cpus doing console IO at this point, however
	 * console interrupts will never be enabled */
	spin_lock_irqsave(&port->sc_port.lock, flags);

	/* early_printk invocation may have done this for us */
	if (!port->sc_ops)
		port->sc_ops = &poll_ops;

	/* we can't turn on the console interrupt (as request_irq
	 * calls kmalloc, which isn't set up yet), so we rely on a
	 * timer to poll for input and push data from the console
	 * buffer.
	 */
	init_timer(&port->sc_timer);
	port->sc_timer.function = sn_sal_timer_poll;
	port->sc_timer.data = (unsigned long)port;

	if (IS_RUNNING_ON_SIMULATOR())
		port->sc_interrupt_timeout = 6;
	else {
		/* 960cps / 16 char FIFO = 60HZ
		 * HZ / (SN_SAL_FIFO_SPEED_CPS / SN_SAL_FIFO_DEPTH) */
		port->sc_interrupt_timeout =
		    HZ * SN_SAL_UART_FIFO_DEPTH / SN_SAL_UART_FIFO_SPEED_CPS;
	}
	mod_timer(&port->sc_timer, jiffies + port->sc_interrupt_timeout);

	port->sc_is_asynch = 1;
	spin_unlock_irqrestore(&port->sc_port.lock, flags);
}

/**
 * sn_sal_switch_to_interrupts - Switch to interrupt driven mode
 * @port: Our sn_cons_port (which contains the uart port)
 *
 * In sn_sal_module_init, after we're registered with serial core and
 * the port is added, this function is called to switch us to interrupt
 * mode.  We were previously in asynch/polling mode (using init_timer).
 *
 * We attempt to switch to interrupt mode here by calling
 * request_irq.  If that works out, we enable receive interrupts.
 */
static void __init sn_sal_switch_to_interrupts(struct sn_cons_port *port)
{
	unsigned long flags;

	if (port) {
		DPRINTF("sn_console: switching to interrupt driven console\n");

		if (request_irq(SGI_UART_VECTOR, sn_sal_interrupt,
				IRQF_DISABLED | IRQF_SHARED,
				"SAL console driver", port) >= 0) {
			spin_lock_irqsave(&port->sc_port.lock, flags);
			port->sc_port.irq = SGI_UART_VECTOR;
			port->sc_ops = &intr_ops;

			/* turn on receive interrupts */
			ia64_sn_console_intr_enable(SAL_CONSOLE_INTR_RECV);
			spin_unlock_irqrestore(&port->sc_port.lock, flags);
		}
		else {
			printk(KERN_INFO
			    "sn_console: console proceeding in polled mode\n");
		}
	}
}

/*
 * Kernel console definitions
 */

static void sn_sal_console_write(struct console *, const char *, unsigned);
static int sn_sal_console_setup(struct console *, char *);
static struct uart_driver sal_console_uart;
extern struct tty_driver *uart_console_device(struct console *, int *);

static struct console sal_console = {
	.name = DEVICE_NAME,
	.write = sn_sal_console_write,
	.device = uart_console_device,
	.setup = sn_sal_console_setup,
	.index = -1,		/* unspecified */
	.data = &sal_console_uart,
};

#define SAL_CONSOLE	&sal_console

static struct uart_driver sal_console_uart = {
	.owner = THIS_MODULE,
	.driver_name = "sn_console",
	.dev_name = DEVICE_NAME,
	.major = 0,		/* major/minor set at registration time per USE_DYNAMIC_MINOR */
	.minor = 0,
	.nr = 1,		/* one port */
	.cons = SAL_CONSOLE,
};

/**
 * sn_sal_module_init - When the kernel loads us, get us rolling w/ serial core
 *
 * Before this is called, we've been printing kernel messages in a special
 * early mode not making use of the serial core infrastructure.  When our
 * driver is loaded for real, we register the driver and port with serial
 * core and try to enable interrupt driven mode.
 *
 */
static int __init sn_sal_module_init(void)
{
	int retval;

	if (!ia64_platform_is("sn2"))
		return 0;

	printk(KERN_INFO "sn_console: Console driver init\n");

	if (USE_DYNAMIC_MINOR == 1) {
		misc.minor = MISC_DYNAMIC_MINOR;
		misc.name = DEVICE_NAME_DYNAMIC;
		retval = misc_register(&misc);
		if (retval != 0) {
			printk(KERN_WARNING "Failed to register console "
			       "device using misc_register.\n");
			return -ENODEV;
		}
		sal_console_uart.major = MISC_MAJOR;
		sal_console_uart.minor = misc.minor;
	} else {
		sal_console_uart.major = DEVICE_MAJOR;
		sal_console_uart.minor = DEVICE_MINOR;
	}

	/* We register the driver and the port before switching to interrupts
	 * or async above so the proper uart structures are populated */

	if (uart_register_driver(&sal_console_uart) < 0) {
		printk
		    ("ERROR sn_sal_module_init failed uart_register_driver, line %d\n",
		     __LINE__);
		return -ENODEV;
	}

	spin_lock_init(&sal_console_port.sc_port.lock);

	/* Setup the port struct with the minimum needed */
	sal_console_port.sc_port.membase = (char *)1;	/* just needs to be non-zero */
	sal_console_port.sc_port.type = PORT_16550A;
	sal_console_port.sc_port.fifosize = SN_SAL_MAX_CHARS;
	sal_console_port.sc_port.ops = &sn_console_ops;
	sal_console_port.sc_port.line = 0;

	if (uart_add_one_port(&sal_console_uart, &sal_console_port.sc_port) < 0) {
		/* error - not sure what I'd do - so I'll do nothing */
		printk(KERN_ERR "%s: unable to add port\n", __func__);
	}

	/* when this driver is compiled in, the console initialization
	 * will have already switched us into asynchronous operation
	 * before we get here through the module initcalls */
	if (!sal_console_port.sc_is_asynch) {
		sn_sal_switch_to_asynch(&sal_console_port);
	}

	/* at this point (module_init) we can try to turn on interrupts */
	if (!IS_RUNNING_ON_SIMULATOR()) {
		sn_sal_switch_to_interrupts(&sal_console_port);
	}
	sn_process_input = 1;
	return 0;
}

/**
 * sn_sal_module_exit - When we're unloaded, remove the driver/port
 *
 */
static void __exit sn_sal_module_exit(void)
{
	del_timer_sync(&sal_console_port.sc_timer);
	uart_remove_one_port(&sal_console_uart, &sal_console_port.sc_port);
	uart_unregister_driver(&sal_console_uart);
	misc_deregister(&misc);
}

module_init(sn_sal_module_init);
module_exit(sn_sal_module_exit);

/**
 * puts_raw_fixed - sn_sal_console_write helper for adding \r's as required
 * @puts_raw : puts function to do the writing
 * @s: input string
 * @count: length
 *
 * We need a \r ahead of every \n for direct writes through
 * ia64_sn_console_putb (what sal_puts_raw below actually does).
 *
 */

static void puts_raw_fixed(int (*puts_raw) (const char *s, int len),
			   const char *s, int count)
{
	const char *s1;

	/* Output '\r' before each '\n' */
	while ((s1 = memchr(s, '\n', count)) != NULL) {
		puts_raw(s, s1 - s);
		puts_raw("\r\n", 2);
		count -= s1 + 1 - s;
		s = s1 + 1;
	}
	puts_raw(s, count);
}

/**
 * sn_sal_console_write - Print statements before serial core available
 * @console: Console to operate on - we ignore since we have just one
 * @s: String to send
 * @count: length
 *
 * This is referenced in the console struct.  It is used for early
 * console printing before we register with serial core and for things
 * such as kdb.  The console_lock must be held when we get here.
 *
 * This function has some code for trying to print output even if the lock
 * is held.  We try to cover the case where a lock holder could have died.
 * We don't use this special case code if we're not registered with serial
 * core yet.  After we're registered with serial core, the only time this
 * function would be used is for high level kernel output like magic sys req,
 * kdb, and printk's.
 */
static void
sn_sal_console_write(struct console *co, const char *s, unsigned count)
{
	unsigned long flags = 0;
	struct sn_cons_port *port = &sal_console_port;
	static int stole_lock = 0;

	BUG_ON(!port->sc_is_asynch);

	/* We can't look at the xmit buffer if we're not registered with serial core
	 *  yet.  So only do the fancy recovery after registering
	 */
	if (!port->sc_port.state) {
		/* Not yet registered with serial core - simple case */
		puts_raw_fixed(port->sc_ops->sal_puts_raw, s, count);
		return;
	}

	/* somebody really wants this output, might be an
	 * oops, kdb, panic, etc.  make sure they get it. */
	if (spin_is_locked(&port->sc_port.lock)) {
		int lhead = port->sc_port.state->xmit.head;
		int ltail = port->sc_port.state->xmit.tail;
		int counter, got_lock = 0;

		/*
		 * We attempt to determine if someone has died with the
		 * lock. We wait ~20 secs after the head and tail ptrs
		 * stop moving and assume the lock holder is not functional
		 * and plow ahead. If the lock is freed within the time out
		 * period we re-get the lock and go ahead normally. We also
		 * remember if we have plowed ahead so that we don't have
		 * to wait out the time out period again - the asumption
		 * is that we will time out again.
		 */

		for (counter = 0; counter < 150; mdelay(125), counter++) {
			if (!spin_is_locked(&port->sc_port.lock)
			    || stole_lock) {
				if (!stole_lock) {
					spin_lock_irqsave(&port->sc_port.lock,
							  flags);
					got_lock = 1;
				}
				break;
			} else {
				/* still locked */
				if ((lhead != port->sc_port.state->xmit.head)
				    || (ltail !=
					port->sc_port.state->xmit.tail)) {
					lhead =
						port->sc_port.state->xmit.head;
					ltail =
						port->sc_port.state->xmit.tail;
					counter = 0;
				}
			}
		}
		/* flush anything in the serial core xmit buffer, raw */
		sn_transmit_chars(port, 1);
		if (got_lock) {
			spin_unlock_irqrestore(&port->sc_port.lock, flags);
			stole_lock = 0;
		} else {
			/* fell thru */
			stole_lock = 1;
		}
		puts_raw_fixed(port->sc_ops->sal_puts_raw, s, count);
	} else {
		stole_lock = 0;
		spin_lock_irqsave(&port->sc_port.lock, flags);
		sn_transmit_chars(port, 1);
		spin_unlock_irqrestore(&port->sc_port.lock, flags);

		puts_raw_fixed(port->sc_ops->sal_puts_raw, s, count);
	}
}


/**
 * sn_sal_console_setup - Set up console for early printing
 * @co: Console to work with
 * @options: Options to set
 *
 * Altix console doesn't do anything with baud rates, etc, anyway.
 *
 * This isn't required since not providing the setup function in the
 * console struct is ok.  However, other patches like KDB plop something
 * here so providing it is easier.
 *
 */
static int sn_sal_console_setup(struct console *co, char *options)
{
	return 0;
}

/**
 * sn_sal_console_write_early - simple early output routine
 * @co - console struct
 * @s - string to print
 * @count - count
 *
 * Simple function to provide early output, before even
 * sn_sal_serial_console_init is called.  Referenced in the
 * console struct registerd in sn_serial_console_early_setup.
 *
 */
static void __init
sn_sal_console_write_early(struct console *co, const char *s, unsigned count)
{
	puts_raw_fixed(sal_console_port.sc_ops->sal_puts_raw, s, count);
}

/* Used for very early console printing - again, before
 * sn_sal_serial_console_init is run */
static struct console sal_console_early __initdata = {
	.name = "sn_sal",
	.write = sn_sal_console_write_early,
	.flags = CON_PRINTBUFFER,
	.index = -1,
};

/**
 * sn_serial_console_early_setup - Sets up early console output support
 *
 * Register a console early on...  This is for output before even
 * sn_sal_serial_cosnole_init is called.  This function is called from
 * setup.c.  This allows us to do really early polled writes. When
 * sn_sal_serial_console_init is called, this console is unregistered
 * and a new one registered.
 */
int __init sn_serial_console_early_setup(void)
{
	if (!ia64_platform_is("sn2"))
		return -1;

	sal_console_port.sc_ops = &poll_ops;
	spin_lock_init(&sal_console_port.sc_port.lock);
	early_sn_setup();	/* Find SAL entry points */
	register_console(&sal_console_early);

	return 0;
}

/**
 * sn_sal_serial_console_init - Early console output - set up for register
 *
 * This function is called when regular console init happens.  Because we
 * support even earlier console output with sn_serial_console_early_setup
 * (called from setup.c directly), this function unregisters the really
 * early console.
 *
 * Note: Even if setup.c doesn't register sal_console_early, unregistering
 * it here doesn't hurt anything.
 *
 */
static int __init sn_sal_serial_console_init(void)
{
	if (ia64_platform_is("sn2")) {
		sn_sal_switch_to_asynch(&sal_console_port);
		DPRINTF("sn_sal_serial_console_init : register console\n");
		register_console(&sal_console);
		unregister_console(&sal_console_early);
	}
	return 0;
}

console_initcall(sn_sal_serial_console_init);
