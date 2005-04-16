/*
 * drivers/serial/v850e_uart.c -- Serial I/O using V850E on-chip UART or UARTB
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

/* This driver supports both the original V850E UART interface (called
   merely `UART' in the docs) and the newer `UARTB' interface, which is
   roughly a superset of the first one.  The selection is made at
   configure time -- if CONFIG_V850E_UARTB is defined, then UARTB is
   presumed, otherwise the old UART -- as these are on-CPU UARTS, a system
   can never have both.

   The UARTB interface also has a 16-entry FIFO mode, which is not
   yet supported by this driver.  */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_core.h>

#include <asm/v850e_uart.h>

/* Initial UART state.  This may be overridden by machine-dependent headers. */
#ifndef V850E_UART_INIT_BAUD
#define V850E_UART_INIT_BAUD	115200
#endif
#ifndef V850E_UART_INIT_CFLAGS
#define V850E_UART_INIT_CFLAGS	(B115200 | CS8 | CREAD)
#endif

/* A string used for prefixing printed descriptions; since the same UART
   macro is actually used on other chips than the V850E.  This must be a
   constant string.  */
#ifndef V850E_UART_CHIP_NAME
#define V850E_UART_CHIP_NAME	"V850E"
#endif

#define V850E_UART_MINOR_BASE	64	   /* First tty minor number */


/* Low-level UART functions.  */

/* Configure and turn on uart channel CHAN, using the termios `control
   modes' bits in CFLAGS, and a baud-rate of BAUD.  */
void v850e_uart_configure (unsigned chan, unsigned cflags, unsigned baud)
{
	int flags;
	v850e_uart_speed_t old_speed;
	v850e_uart_config_t old_config;
	v850e_uart_speed_t new_speed = v850e_uart_calc_speed (baud);
	v850e_uart_config_t new_config = v850e_uart_calc_config (cflags);

	/* Disable interrupts while we're twiddling the hardware.  */
	local_irq_save (flags);

#ifdef V850E_UART_PRE_CONFIGURE
	V850E_UART_PRE_CONFIGURE (chan, cflags, baud);
#endif

	old_config = V850E_UART_CONFIG (chan);
	old_speed = v850e_uart_speed (chan);

	if (! v850e_uart_speed_eq (old_speed, new_speed)) {
		/* The baud rate has changed.  First, disable the UART.  */
		V850E_UART_CONFIG (chan) = V850E_UART_CONFIG_FINI;
		old_config = 0;	/* Force the uart to be re-initialized. */

		/* Reprogram the baud-rate generator.  */
		v850e_uart_set_speed (chan, new_speed);
	}

	if (! (old_config & V850E_UART_CONFIG_ENABLED)) {
		/* If we are using the uart for the first time, start by
		   enabling it, which must be done before turning on any
		   other bits.  */
		V850E_UART_CONFIG (chan) = V850E_UART_CONFIG_INIT;
		/* See the initial state.  */
		old_config = V850E_UART_CONFIG (chan);
	}

	if (new_config != old_config) {
		/* Which of the TXE/RXE bits we'll temporarily turn off
		   before changing other control bits.  */
		unsigned temp_disable = 0;
		/* Which of the TXE/RXE bits will be enabled.  */
		unsigned enable = 0;
		unsigned changed_bits = new_config ^ old_config;

		/* Which of RX/TX will be enabled in the new configuration.  */
		if (new_config & V850E_UART_CONFIG_RX_BITS)
			enable |= (new_config & V850E_UART_CONFIG_RX_ENABLE);
		if (new_config & V850E_UART_CONFIG_TX_BITS)
			enable |= (new_config & V850E_UART_CONFIG_TX_ENABLE);

		/* Figure out which of RX/TX needs to be disabled; note
		   that this will only happen if they're not already
		   disabled.  */
		if (changed_bits & V850E_UART_CONFIG_RX_BITS)
			temp_disable
				|= (old_config & V850E_UART_CONFIG_RX_ENABLE);
		if (changed_bits & V850E_UART_CONFIG_TX_BITS)
			temp_disable
				|= (old_config & V850E_UART_CONFIG_TX_ENABLE);

		/* We have to turn off RX and/or TX mode before changing
		   any associated control bits.  */
		if (temp_disable)
			V850E_UART_CONFIG (chan) = old_config & ~temp_disable;

		/* Write the new control bits, while RX/TX are disabled. */ 
		if (changed_bits & ~enable)
			V850E_UART_CONFIG (chan) = new_config & ~enable;

		v850e_uart_config_delay (new_config, new_speed);

		/* Write the final version, with enable bits turned on.  */
		V850E_UART_CONFIG (chan) = new_config;
	}

	local_irq_restore (flags);
}


/*  Low-level console. */

#ifdef CONFIG_V850E_UART_CONSOLE

static void v850e_uart_cons_write (struct console *co,
				   const char *s, unsigned count)
{
	if (count > 0) {
		unsigned chan = co->index;
		unsigned irq = V850E_UART_TX_IRQ (chan);
		int irq_was_enabled, irq_was_pending, flags;

		/* We don't want to get `transmission completed'
		   interrupts, since we're busy-waiting, so we disable them
		   while sending (we don't disable interrupts entirely
		   because sending over a serial line is really slow).  We
		   save the status of the tx interrupt and restore it when
		   we're done so that using printk doesn't interfere with
		   normal serial transmission (other than interleaving the
		   output, of course!).  This should work correctly even if
		   this function is interrupted and the interrupt printks
		   something.  */

		/* Disable interrupts while fiddling with tx interrupt.  */
		local_irq_save (flags);
		/* Get current tx interrupt status.  */
		irq_was_enabled = v850e_intc_irq_enabled (irq);
		irq_was_pending = v850e_intc_irq_pending (irq);
		/* Disable tx interrupt if necessary.  */
		if (irq_was_enabled)
			v850e_intc_disable_irq (irq);
		/* Turn interrupts back on.  */
		local_irq_restore (flags);

		/* Send characters.  */
		while (count > 0) {
			int ch = *s++;

			if (ch == '\n') {
				/* We don't have the benefit of a tty
				   driver, so translate NL into CR LF.  */
				v850e_uart_wait_for_xmit_ok (chan);
				v850e_uart_putc (chan, '\r');
			}

			v850e_uart_wait_for_xmit_ok (chan);
			v850e_uart_putc (chan, ch);

			count--;
		}

		/* Restore saved tx interrupt status.  */
		if (irq_was_enabled) {
			/* Wait for the last character we sent to be
			   completely transmitted (as we'll get an
			   interrupt interrupt at that point).  */
			v850e_uart_wait_for_xmit_done (chan);
			/* Clear pending interrupts received due
			   to our transmission, unless there was already
			   one pending, in which case we want the
			   handler to be called.  */
			if (! irq_was_pending)
				v850e_intc_clear_pending_irq (irq);
			/* ... and then turn back on handling.  */
			v850e_intc_enable_irq (irq);
		}
	}
}

extern struct uart_driver v850e_uart_driver;
static struct console v850e_uart_cons =
{
    .name	= "ttyS",
    .write	= v850e_uart_cons_write,
    .device	= uart_console_device,
    .flags	= CON_PRINTBUFFER,
    .cflag	= V850E_UART_INIT_CFLAGS,
    .index	= -1,
    .data	= &v850e_uart_driver,
};

void v850e_uart_cons_init (unsigned chan)
{
	v850e_uart_configure (chan, V850E_UART_INIT_CFLAGS,
			      V850E_UART_INIT_BAUD);
	v850e_uart_cons.index = chan;
	register_console (&v850e_uart_cons);
	printk ("Console: %s on-chip UART channel %d\n",
		V850E_UART_CHIP_NAME, chan);
}

/* This is what the init code actually calls.  */
static int v850e_uart_console_init (void)
{
	v850e_uart_cons_init (V850E_UART_CONSOLE_CHANNEL);
	return 0;
}
console_initcall(v850e_uart_console_init);

#define V850E_UART_CONSOLE &v850e_uart_cons

#else /* !CONFIG_V850E_UART_CONSOLE */
#define V850E_UART_CONSOLE 0
#endif /* CONFIG_V850E_UART_CONSOLE */

/* TX/RX interrupt handlers.  */

static void v850e_uart_stop_tx (struct uart_port *port, unsigned tty_stop);

void v850e_uart_tx (struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;
	int stopped = uart_tx_stopped (port);

	if (v850e_uart_xmit_ok (port->line)) {
		int tx_ch;

		if (port->x_char) {
			tx_ch = port->x_char;
			port->x_char = 0;
		} else if (!uart_circ_empty (xmit) && !stopped) {
			tx_ch = xmit->buf[xmit->tail];
			xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		} else
			goto no_xmit;

		v850e_uart_putc (port->line, tx_ch);
		port->icount.tx++;

		if (uart_circ_chars_pending (xmit) < WAKEUP_CHARS)
			uart_write_wakeup (port);
	}

 no_xmit:
	if (uart_circ_empty (xmit) || stopped)
		v850e_uart_stop_tx (port, stopped);
}

static irqreturn_t v850e_uart_tx_irq(int irq, void *data, struct pt_regs *regs)
{
	struct uart_port *port = data;
	v850e_uart_tx (port);
	return IRQ_HANDLED;
}

static irqreturn_t v850e_uart_rx_irq(int irq, void *data, struct pt_regs *regs)
{
	struct uart_port *port = data;
	unsigned ch_stat = TTY_NORMAL;
	unsigned ch = v850e_uart_getc (port->line);
	unsigned err = v850e_uart_err (port->line);

	if (err) {
		if (err & V850E_UART_ERR_OVERRUN) {
			ch_stat = TTY_OVERRUN;
			port->icount.overrun++;
		} else if (err & V850E_UART_ERR_FRAME) {
			ch_stat = TTY_FRAME;
			port->icount.frame++;
		} else if (err & V850E_UART_ERR_PARITY) {
			ch_stat = TTY_PARITY;
			port->icount.parity++;
		}
	}

	port->icount.rx++;

	tty_insert_flip_char (port->info->tty, ch, ch_stat);
	tty_schedule_flip (port->info->tty);

	return IRQ_HANDLED;
}


/* Control functions for the serial framework.  */

static void v850e_uart_nop (struct uart_port *port) { }
static int v850e_uart_success (struct uart_port *port) { return 0; }

static unsigned v850e_uart_tx_empty (struct uart_port *port)
{
	return TIOCSER_TEMT;	/* Can't detect.  */
}

static void v850e_uart_set_mctrl (struct uart_port *port, unsigned mctrl)
{
#ifdef V850E_UART_SET_RTS
	V850E_UART_SET_RTS (port->line, (mctrl & TIOCM_RTS));
#endif
}

static unsigned v850e_uart_get_mctrl (struct uart_port *port)
{
	/* We don't support DCD or DSR, so consider them permanently active. */
	int mctrl = TIOCM_CAR | TIOCM_DSR;

	/* We may support CTS.  */
#ifdef V850E_UART_CTS
	mctrl |= V850E_UART_CTS(port->line) ? TIOCM_CTS : 0;
#else
	mctrl |= TIOCM_CTS;
#endif

	return mctrl;
}

static void v850e_uart_start_tx (struct uart_port *port, unsigned tty_start)
{
	v850e_intc_disable_irq (V850E_UART_TX_IRQ (port->line));
	v850e_uart_tx (port);
	v850e_intc_enable_irq (V850E_UART_TX_IRQ (port->line));
}

static void v850e_uart_stop_tx (struct uart_port *port, unsigned tty_stop)
{
	v850e_intc_disable_irq (V850E_UART_TX_IRQ (port->line));
}

static void v850e_uart_start_rx (struct uart_port *port)
{
	v850e_intc_enable_irq (V850E_UART_RX_IRQ (port->line));
}

static void v850e_uart_stop_rx (struct uart_port *port)
{
	v850e_intc_disable_irq (V850E_UART_RX_IRQ (port->line));
}

static void v850e_uart_break_ctl (struct uart_port *port, int break_ctl)
{
	/* Umm, do this later.  */
}

static int v850e_uart_startup (struct uart_port *port)
{
	int err;

	/* Alloc RX irq.  */
	err = request_irq (V850E_UART_RX_IRQ (port->line), v850e_uart_rx_irq,
			   SA_INTERRUPT, "v850e_uart", port);
	if (err)
		return err;

	/* Alloc TX irq.  */
	err = request_irq (V850E_UART_TX_IRQ (port->line), v850e_uart_tx_irq,
			   SA_INTERRUPT, "v850e_uart", port);
	if (err) {
		free_irq (V850E_UART_RX_IRQ (port->line), port);
		return err;
	}

	v850e_uart_start_rx (port);

	return 0;
}

static void v850e_uart_shutdown (struct uart_port *port)
{
	/* Disable port interrupts.  */
	free_irq (V850E_UART_TX_IRQ (port->line), port);
	free_irq (V850E_UART_RX_IRQ (port->line), port);

	/* Turn off xmit/recv enable bits.  */
	V850E_UART_CONFIG (port->line)
		&= ~(V850E_UART_CONFIG_TX_ENABLE
		     | V850E_UART_CONFIG_RX_ENABLE);
	/* Then reset the channel.  */
	V850E_UART_CONFIG (port->line) = 0;
}

static void
v850e_uart_set_termios (struct uart_port *port, struct termios *termios,
		        struct termios *old)
{
	unsigned cflags = termios->c_cflag;

	/* Restrict flags to legal values.  */
	if ((cflags & CSIZE) != CS7 && (cflags & CSIZE) != CS8)
		/* The new value of CSIZE is invalid, use the old value.  */
		cflags = (cflags & ~CSIZE)
			| (old ? (old->c_cflag & CSIZE) : CS8);

	termios->c_cflag = cflags;

	v850e_uart_configure (port->line, cflags,
			      uart_get_baud_rate (port, termios, old,
						  v850e_uart_min_baud(),
						  v850e_uart_max_baud()));
}

static const char *v850e_uart_type (struct uart_port *port)
{
	return port->type == PORT_V850E_UART ? "v850e_uart" : 0;
}

static void v850e_uart_config_port (struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_V850E_UART;
}

static int
v850e_uart_verify_port (struct uart_port *port, struct serial_struct *ser)
{
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_V850E_UART)
		return -EINVAL;
	if (ser->irq != V850E_UART_TX_IRQ (port->line))
		return -EINVAL;
	return 0;
}

static struct uart_ops v850e_uart_ops = {
	.tx_empty	= v850e_uart_tx_empty,
	.get_mctrl	= v850e_uart_get_mctrl,
	.set_mctrl	= v850e_uart_set_mctrl,
	.start_tx	= v850e_uart_start_tx,
	.stop_tx	= v850e_uart_stop_tx,
	.stop_rx	= v850e_uart_stop_rx,
	.enable_ms	= v850e_uart_nop,
	.break_ctl	= v850e_uart_break_ctl,
	.startup	= v850e_uart_startup,
	.shutdown	= v850e_uart_shutdown,
	.set_termios	= v850e_uart_set_termios,
	.type		= v850e_uart_type,
	.release_port	= v850e_uart_nop,
	.request_port	= v850e_uart_success,
	.config_port	= v850e_uart_config_port,
	.verify_port	= v850e_uart_verify_port,
};

/* Initialization and cleanup.  */

static struct uart_driver v850e_uart_driver = {
	.owner			= THIS_MODULE,
	.driver_name		= "v850e_uart",
	.devfs_name		= "tts/",
	.dev_name		= "ttyS",
	.major			= TTY_MAJOR,
	.minor			= V850E_UART_MINOR_BASE,
	.nr			= V850E_UART_NUM_CHANNELS,
	.cons			= V850E_UART_CONSOLE,
};


static struct uart_port v850e_uart_ports[V850E_UART_NUM_CHANNELS];

static int __init v850e_uart_init (void)
{
	int rval;

	printk (KERN_INFO "%s on-chip UART\n", V850E_UART_CHIP_NAME);

	rval = uart_register_driver (&v850e_uart_driver);
	if (rval == 0) {
		unsigned chan;

		for (chan = 0; chan < V850E_UART_NUM_CHANNELS; chan++) {
			struct uart_port *port = &v850e_uart_ports[chan];
			
			memset (port, 0, sizeof *port);

			port->ops = &v850e_uart_ops;
			port->line = chan;
			port->iotype = SERIAL_IO_MEM;
			port->flags = UPF_BOOT_AUTOCONF;

			/* We actually use multiple IRQs, but the serial
			   framework seems to mainly use this for
			   informational purposes anyway.  Here we use the TX
			   irq.  */
			port->irq = V850E_UART_TX_IRQ (chan);

			/* The serial framework doesn't really use these
			   membase/mapbase fields for anything useful, but
			   it requires that they be something non-zero to
			   consider the port `valid', and also uses them
			   for informational purposes.  */
			port->membase = (void *)V850E_UART_BASE_ADDR (chan);
			port->mapbase = V850E_UART_BASE_ADDR (chan);

			/* The framework insists on knowing the uart's master
			   clock freq, though it doesn't seem to do anything
			   useful for us with it.  We must make it at least
			   higher than (the maximum baud rate * 16), otherwise
			   the framework will puke during its internal
			   calculations, and force the baud rate to be 9600.
			   To be accurate though, just repeat the calculation
			   we use when actually setting the speed.  */
			port->uartclk = v850e_uart_max_clock() * 16;

			uart_add_one_port (&v850e_uart_driver, port);
		}
	}

	return rval;
}

static void __exit v850e_uart_exit (void)
{
	unsigned chan;

	for (chan = 0; chan < V850E_UART_NUM_CHANNELS; chan++)
		uart_remove_one_port (&v850e_uart_driver,
				      &v850e_uart_ports[chan]);

	uart_unregister_driver (&v850e_uart_driver);
}

module_init (v850e_uart_init);
module_exit (v850e_uart_exit);

MODULE_AUTHOR ("Miles Bader");
MODULE_DESCRIPTION ("NEC " V850E_UART_CHIP_NAME " on-chip UART");
MODULE_LICENSE ("GPL");
