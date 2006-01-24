/*
 * dz.c: Serial port driver for DECStations equiped
 *       with the DZ chipset.
 *
 * Copyright (C) 1998 Olivier A. D. Lebaillif
 *
 * Email: olivier.lebaillif@ifrsys.com
 *
 * [31-AUG-98] triemer
 * Changed IRQ to use Harald's dec internals interrupts.h
 * removed base_addr code - moving address assignment to setup.c
 * Changed name of dz_init to rs_init to be consistent with tc code
 * [13-NOV-98] triemer fixed code to receive characters
 *    after patches by harald to irq code.
 * [09-JAN-99] triemer minor fix for schedule - due to removal of timeout
 *            field from "current" - somewhere between 2.1.121 and 2.1.131
 Qua Jun 27 15:02:26 BRT 2001
 * [27-JUN-2001] Arnaldo Carvalho de Melo <acme@conectiva.com.br> - cleanups
 *
 * Parts (C) 1999 David Airlie, airlied@linux.ie
 * [07-SEP-99] Bugfixes
 *
 * [06-Jan-2002] Russell King <rmk@arm.linux.org.uk>
 * Converted to new serial core
 */

#undef DEBUG_DZ

#include <linux/config.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>

#include <asm/bootinfo.h>
#include <asm/dec/interrupts.h>
#include <asm/dec/kn01.h>
#include <asm/dec/kn02.h>
#include <asm/dec/machtype.h>
#include <asm/dec/prom.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#define CONSOLE_LINE (3)	/* for definition of struct console */

#include "dz.h"

#define DZ_INTR_DEBUG 1

static char *dz_name = "DECstation DZ serial driver version ";
static char *dz_version = "1.02";

struct dz_port {
	struct uart_port	port;
	unsigned int		cflag;
};

static struct dz_port dz_ports[DZ_NB_PORT];

#ifdef DEBUG_DZ
/*
 * debugging code to send out chars via prom
 */
static void debug_console(const char *s, int count)
{
	unsigned i;

	for (i = 0; i < count; i++) {
		if (*s == 10)
			prom_printf("%c", 13);
		prom_printf("%c", *s++);
	}
}
#endif

/*
 * ------------------------------------------------------------
 * dz_in () and dz_out ()
 *
 * These routines are used to access the registers of the DZ
 * chip, hiding relocation differences between implementation.
 * ------------------------------------------------------------
 */

static inline unsigned short dz_in(struct dz_port *dport, unsigned offset)
{
	volatile unsigned short *addr =
		(volatile unsigned short *) (dport->port.membase + offset);
	return *addr;
}

static inline void dz_out(struct dz_port *dport, unsigned offset,
                          unsigned short value)
{
	volatile unsigned short *addr =
		(volatile unsigned short *) (dport->port.membase + offset);
	*addr = value;
}

/*
 * ------------------------------------------------------------
 * rs_stop () and rs_start ()
 *
 * These routines are called before setting or resetting
 * tty->stopped. They enable or disable transmitter interrupts,
 * as necessary.
 * ------------------------------------------------------------
 */

static void dz_stop_tx(struct uart_port *uport)
{
	struct dz_port *dport = (struct dz_port *)uport;
	unsigned short tmp, mask = 1 << dport->port.line;
	unsigned long flags;

	spin_lock_irqsave(&dport->port.lock, flags);
	tmp = dz_in(dport, DZ_TCR);	/* read the TX flag */
	tmp &= ~mask;			/* clear the TX flag */
	dz_out(dport, DZ_TCR, tmp);
	spin_unlock_irqrestore(&dport->port.lock, flags);
}

static void dz_start_tx(struct uart_port *uport)
{
	struct dz_port *dport = (struct dz_port *)uport;
	unsigned short tmp, mask = 1 << dport->port.line;
	unsigned long flags;

	spin_lock_irqsave(&dport->port.lock, flags);
	tmp = dz_in(dport, DZ_TCR);	/* read the TX flag */
	tmp |= mask;			/* set the TX flag */
	dz_out(dport, DZ_TCR, tmp);
	spin_unlock_irqrestore(&dport->port.lock, flags);
}

static void dz_stop_rx(struct uart_port *uport)
{
	struct dz_port *dport = (struct dz_port *)uport;
	unsigned long flags;

	spin_lock_irqsave(&dport->port.lock, flags);
	dport->cflag &= ~DZ_CREAD;
	dz_out(dport, DZ_LPR, dport->cflag);
	spin_unlock_irqrestore(&dport->port.lock, flags);
}

static void dz_enable_ms(struct uart_port *port)
{
	/* nothing to do */
}

/*
 * ------------------------------------------------------------
 * Here starts the interrupt handling routines.  All of the
 * following subroutines are declared as inline and are folded
 * into dz_interrupt.  They were separated out for readability's
 * sake.
 *
 * Note: rs_interrupt() is a "fast" interrupt, which means that it
 * runs with interrupts turned off.  People who may want to modify
 * rs_interrupt() should try to keep the interrupt handler as fast as
 * possible.  After you are done making modifications, it is not a bad
 * idea to do:
 *
 *	make drivers/serial/dz.s
 *
 * and look at the resulting assemble code in dz.s.
 *
 * ------------------------------------------------------------
 */

/*
 * ------------------------------------------------------------
 * receive_char ()
 *
 * This routine deals with inputs from any lines.
 * ------------------------------------------------------------
 */
static inline void dz_receive_chars(struct dz_port *dport)
{
	struct tty_struct *tty = NULL;
	struct uart_icount *icount;
	int ignore = 0;
	unsigned short status, tmp;
	unsigned char ch, flag;

	/* this code is going to be a problem...
	   the call to tty_flip_buffer is going to need
	   to be rethought...
	 */
	do {
		status = dz_in(dport, DZ_RBUF);

		/* punt so we don't get duplicate characters */
		if (!(status & DZ_DVAL))
			goto ignore_char;


		ch = UCHAR(status);	/* grab the char */
		flag = TTY_NORMAL;

#if 0
		if (info->is_console) {
			if (ch == 0)
				return;		/* it's a break ... */
		}
#endif

		tty = dport->port.info->tty;/* now tty points to the proper dev */
		icount = &dport->port.icount;

		if (!tty)
			break;

		icount->rx++;

		/* keep track of the statistics */
		if (status & (DZ_OERR | DZ_FERR | DZ_PERR)) {
			if (status & DZ_PERR)	/* parity error */
				icount->parity++;
			else if (status & DZ_FERR)	/* frame error */
				icount->frame++;
			if (status & DZ_OERR)	/* overrun error */
				icount->overrun++;

			/*  check to see if we should ignore the character
			   and mask off conditions that should be ignored
			 */

			if (status & dport->port.ignore_status_mask) {
				if (++ignore > 100)
					break;
				goto ignore_char;
			}
			/* mask off the error conditions we want to ignore */
			tmp = status & dport->port.read_status_mask;

			if (tmp & DZ_PERR) {
				flag = TTY_PARITY;
#ifdef DEBUG_DZ
				debug_console("PERR\n", 5);
#endif
			} else if (tmp & DZ_FERR) {
				flag = TTY_FRAME;
#ifdef DEBUG_DZ
				debug_console("FERR\n", 5);
#endif
			}
			if (tmp & DZ_OERR) {
#ifdef DEBUG_DZ
				debug_console("OERR\n", 5);
#endif
				tty_insert_flip_char(tty, ch, flag);
				ch = 0;
				flag = TTY_OVERRUN;
			}
		}
		tty_insert_flip_char(tty, ch, flag);
	      ignore_char:
	} while (status & DZ_DVAL);

	if (tty)
		tty_flip_buffer_push(tty);
}

/*
 * ------------------------------------------------------------
 * transmit_char ()
 *
 * This routine deals with outputs to any lines.
 * ------------------------------------------------------------
 */
static inline void dz_transmit_chars(struct dz_port *dport)
{
	struct circ_buf *xmit = &dport->port.info->xmit;
	unsigned char tmp;

	if (dport->port.x_char) {	/* XON/XOFF chars */
		dz_out(dport, DZ_TDR, dport->port.x_char);
		dport->port.icount.tx++;
		dport->port.x_char = 0;
		return;
	}
	/* if nothing to do or stopped or hardware stopped */
	if (uart_circ_empty(xmit) || uart_tx_stopped(&dport->port)) {
		dz_stop_tx(&dport->port);
		return;
	}

	/*
	 * if something to do ... (rember the dz has no output fifo so we go
	 * one char at a time :-<
	 */
	tmp = xmit->buf[xmit->tail];
	xmit->tail = (xmit->tail + 1) & (DZ_XMIT_SIZE - 1);
	dz_out(dport, DZ_TDR, tmp);
	dport->port.icount.tx++;

	if (uart_circ_chars_pending(xmit) < DZ_WAKEUP_CHARS)
		uart_write_wakeup(&dport->port);

	/* Are we done */
	if (uart_circ_empty(xmit))
		dz_stop_tx(&dport->port);
}

/*
 * ------------------------------------------------------------
 * check_modem_status ()
 *
 * Only valid for the MODEM line duh !
 * ------------------------------------------------------------
 */
static inline void check_modem_status(struct dz_port *dport)
{
	unsigned short status;

	/* if not ne modem line just return */
	if (dport->port.line != DZ_MODEM)
		return;

	status = dz_in(dport, DZ_MSR);

	/* it's easy, since DSR2 is the only bit in the register */
	if (status)
		dport->port.icount.dsr++;
}

/*
 * ------------------------------------------------------------
 * dz_interrupt ()
 *
 * this is the main interrupt routine for the DZ chip.
 * It deals with the multiple ports.
 * ------------------------------------------------------------
 */
static irqreturn_t dz_interrupt(int irq, void *dev, struct pt_regs *regs)
{
	struct dz_port *dport;
	unsigned short status;

	/* get the reason why we just got an irq */
	status = dz_in((struct dz_port *)dev, DZ_CSR);
	dport = &dz_ports[LINE(status)];

	if (status & DZ_RDONE)
		dz_receive_chars(dport);

	if (status & DZ_TRDY)
		dz_transmit_chars(dport);

	/* FIXME: what about check modem status??? --rmk */

	return IRQ_HANDLED;
}

/*
 * -------------------------------------------------------------------
 * Here ends the DZ interrupt routines.
 * -------------------------------------------------------------------
 */

static unsigned int dz_get_mctrl(struct uart_port *uport)
{
	struct dz_port *dport = (struct dz_port *)uport;
	unsigned int mctrl = TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;

	if (dport->port.line == DZ_MODEM) {
		/*
		 * CHECKME: This is a guess from the other code... --rmk
		 */
		if (dz_in(dport, DZ_MSR) & DZ_MODEM_DSR)
			mctrl &= ~TIOCM_DSR;
	}

	return mctrl;
}

static void dz_set_mctrl(struct uart_port *uport, unsigned int mctrl)
{
	struct dz_port *dport = (struct dz_port *)uport;
	unsigned short tmp;

	if (dport->port.line == DZ_MODEM) {
		tmp = dz_in(dport, DZ_TCR);
		if (mctrl & TIOCM_DTR)
			tmp &= ~DZ_MODEM_DTR;
		else
			tmp |= DZ_MODEM_DTR;
		dz_out(dport, DZ_TCR, tmp);
	}
}

/*
 * -------------------------------------------------------------------
 * startup ()
 *
 * various initialization tasks
 * -------------------------------------------------------------------
 */
static int dz_startup(struct uart_port *uport)
{
	struct dz_port *dport = (struct dz_port *)uport;
	unsigned long flags;
	unsigned short tmp;

	/* The dz lines for the mouse/keyboard must be
	 * opened using their respective drivers.
	 */
	if ((dport->port.line == DZ_KEYBOARD) ||
	    (dport->port.line == DZ_MOUSE))
		return -ENODEV;

	spin_lock_irqsave(&dport->port.lock, flags);

	/* enable the interrupt and the scanning */
	tmp = dz_in(dport, DZ_CSR);
	tmp |= DZ_RIE | DZ_TIE | DZ_MSE;
	dz_out(dport, DZ_CSR, tmp);

	spin_unlock_irqrestore(&dport->port.lock, flags);

	return 0;
}

/*
 * -------------------------------------------------------------------
 * shutdown ()
 *
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 * -------------------------------------------------------------------
 */
static void dz_shutdown(struct uart_port *uport)
{
	dz_stop_tx(uport);
}

/*
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 *          is emptied.  On bus types like RS485, the transmitter must
 *          release the bus after transmitting. This must be done when
 *          the transmit shift register is empty, not be done when the
 *          transmit holding register is empty.  This functionality
 *          allows an RS485 driver to be written in user space.
 */
static unsigned int dz_tx_empty(struct uart_port *uport)
{
	struct dz_port *dport = (struct dz_port *)uport;
	unsigned short status = dz_in(dport, DZ_LPR);

	/* FIXME: this appears to be obviously broken --rmk. */
	return status ? TIOCSER_TEMT : 0;
}

static void dz_break_ctl(struct uart_port *uport, int break_state)
{
	struct dz_port *dport = (struct dz_port *)uport;
	unsigned long flags;
	unsigned short tmp, mask = 1 << uport->line;

	spin_lock_irqsave(&uport->lock, flags);
	tmp = dz_in(dport, DZ_TCR);
	if (break_state)
		tmp |= mask;
	else
		tmp &= ~mask;
	dz_out(dport, DZ_TCR, tmp);
	spin_unlock_irqrestore(&uport->lock, flags);
}

static void dz_set_termios(struct uart_port *uport, struct termios *termios,
			   struct termios *old_termios)
{
	struct dz_port *dport = (struct dz_port *)uport;
	unsigned long flags;
	unsigned int cflag, baud;

	cflag = dport->port.line;

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		cflag |= DZ_CS5;
		break;
	case CS6:
		cflag |= DZ_CS6;
		break;
	case CS7:
		cflag |= DZ_CS7;
		break;
	case CS8:
	default:
		cflag |= DZ_CS8;
	}

	if (termios->c_cflag & CSTOPB)
		cflag |= DZ_CSTOPB;
	if (termios->c_cflag & PARENB)
		cflag |= DZ_PARENB;
	if (termios->c_cflag & PARODD)
		cflag |= DZ_PARODD;

	baud = uart_get_baud_rate(uport, termios, old_termios, 50, 9600);
	switch (baud) {
	case 50:
		cflag |= DZ_B50;
		break;
	case 75:
		cflag |= DZ_B75;
		break;
	case 110:
		cflag |= DZ_B110;
		break;
	case 134:
		cflag |= DZ_B134;
		break;
	case 150:
		cflag |= DZ_B150;
		break;
	case 300:
		cflag |= DZ_B300;
		break;
	case 600:
		cflag |= DZ_B600;
		break;
	case 1200:
		cflag |= DZ_B1200;
		break;
	case 1800:
		cflag |= DZ_B1800;
		break;
	case 2000:
		cflag |= DZ_B2000;
		break;
	case 2400:
		cflag |= DZ_B2400;
		break;
	case 3600:
		cflag |= DZ_B3600;
		break;
	case 4800:
		cflag |= DZ_B4800;
		break;
	case 7200:
		cflag |= DZ_B7200;
		break;
	case 9600:
	default:
		cflag |= DZ_B9600;
	}

	if (termios->c_cflag & CREAD)
		cflag |= DZ_RXENAB;

	spin_lock_irqsave(&dport->port.lock, flags);

	dz_out(dport, DZ_LPR, cflag);
	dport->cflag = cflag;

	/* setup accept flag */
	dport->port.read_status_mask = DZ_OERR;
	if (termios->c_iflag & INPCK)
		dport->port.read_status_mask |= DZ_FERR | DZ_PERR;

	/* characters to ignore */
	uport->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		dport->port.ignore_status_mask |= DZ_FERR | DZ_PERR;

	spin_unlock_irqrestore(&dport->port.lock, flags);
}

static const char *dz_type(struct uart_port *port)
{
	return "DZ";
}

static void dz_release_port(struct uart_port *port)
{
	/* nothing to do */
}

static int dz_request_port(struct uart_port *port)
{
	return 0;
}

static void dz_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_DZ;
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int dz_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_DZ)
		ret = -EINVAL;
	if (ser->irq != port->irq)
		ret = -EINVAL;
	return ret;
}

static struct uart_ops dz_ops = {
	.tx_empty	= dz_tx_empty,
	.get_mctrl	= dz_get_mctrl,
	.set_mctrl	= dz_set_mctrl,
	.stop_tx	= dz_stop_tx,
	.start_tx	= dz_start_tx,
	.stop_rx	= dz_stop_rx,
	.enable_ms	= dz_enable_ms,
	.break_ctl	= dz_break_ctl,
	.startup	= dz_startup,
	.shutdown	= dz_shutdown,
	.set_termios	= dz_set_termios,
	.type		= dz_type,
	.release_port	= dz_release_port,
	.request_port	= dz_request_port,
	.config_port	= dz_config_port,
	.verify_port	= dz_verify_port,
};

static void __init dz_init_ports(void)
{
	static int first = 1;
	struct dz_port *dport;
	unsigned long base;
	int i;

	if (!first)
		return;
	first = 0;

	if (mips_machtype == MACH_DS23100 ||
	    mips_machtype == MACH_DS5100)
		base = CKSEG1ADDR(KN01_SLOT_BASE + KN01_DZ11);
	else
		base = CKSEG1ADDR(KN02_SLOT_BASE + KN02_DZ11);

	for (i = 0, dport = dz_ports; i < DZ_NB_PORT; i++, dport++) {
		spin_lock_init(&dport->port.lock);
		dport->port.membase	= (char *) base;
		dport->port.iotype	= SERIAL_IO_PORT;
		dport->port.irq		= dec_interrupt[DEC_IRQ_DZ11];
		dport->port.line	= i;
		dport->port.fifosize	= 1;
		dport->port.ops		= &dz_ops;
		dport->port.flags	= UPF_BOOT_AUTOCONF;
	}
}

static void dz_reset(struct dz_port *dport)
{
	dz_out(dport, DZ_CSR, DZ_CLR);

	while (dz_in(dport, DZ_CSR) & DZ_CLR);
		/* FIXME: cpu_relax? */

	iob();

	/* enable scanning */
	dz_out(dport, DZ_CSR, DZ_MSE);
}

#ifdef CONFIG_SERIAL_DZ_CONSOLE
static void dz_console_put_char(struct dz_port *dport, unsigned char ch)
{
	unsigned long flags;
	int loops = 2500;
	unsigned short tmp = ch;
	/* this code sends stuff out to serial device - spinning its
	   wheels and waiting. */

	spin_lock_irqsave(&dport->port.lock, flags);

	/* spin our wheels */
	while (((dz_in(dport, DZ_CSR) & DZ_TRDY) != DZ_TRDY) && loops--)
		/* FIXME: cpu_relax, udelay? --rmk */
		;

	/* Actually transmit the character. */
	dz_out(dport, DZ_TDR, tmp);

	spin_unlock_irqrestore(&dport->port.lock, flags);
}
/*
 * -------------------------------------------------------------------
 * dz_console_print ()
 *
 * dz_console_print is registered for printk.
 * The console must be locked when we get here.
 * -------------------------------------------------------------------
 */
static void dz_console_print(struct console *cons,
			     const char *str,
			     unsigned int count)
{
	struct dz_port *dport = &dz_ports[CONSOLE_LINE];
#ifdef DEBUG_DZ
	prom_printf((char *) str);
#endif
	while (count--) {
		if (*str == '\n')
			dz_console_put_char(dport, '\r');
		dz_console_put_char(dport, *str++);
	}
}

static int __init dz_console_setup(struct console *co, char *options)
{
	struct dz_port *dport = &dz_ports[CONSOLE_LINE];
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;
	unsigned short mask, tmp;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	dz_reset(dport);

	ret = uart_set_options(&dport->port, co, baud, parity, bits, flow);
	if (ret == 0) {
		mask = 1 << dport->port.line;
		tmp = dz_in(dport, DZ_TCR);	/* read the TX flag */
		if (!(tmp & mask)) {
			tmp |= mask;		/* set the TX flag */
			dz_out(dport, DZ_TCR, tmp);
		}
	}

	return ret;
}

static struct console dz_sercons =
{
	.name	= "ttyS",
	.write	= dz_console_print,
	.device	= uart_console_device,
	.setup	= dz_console_setup,
	.flags	= CON_CONSDEV | CON_PRINTBUFFER,
	.index	= CONSOLE_LINE,
};

void __init dz_serial_console_init(void)
{
	dz_init_ports();

	register_console(&dz_sercons);
}

#define SERIAL_DZ_CONSOLE	&dz_sercons
#else
#define SERIAL_DZ_CONSOLE	NULL
#endif /* CONFIG_SERIAL_DZ_CONSOLE */

static struct uart_driver dz_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "serial",
#ifdef CONFIG_DEVFS
	.dev_name		= "tts/%d",
#else
	.dev_name		= "ttyS%d",
#endif
	.major			= TTY_MAJOR,
	.minor			= 64,
	.nr			= DZ_NB_PORT,
	.cons			= SERIAL_DZ_CONSOLE,
};

int __init dz_init(void)
{
	unsigned long flags;
	int ret, i;

	printk("%s%s\n", dz_name, dz_version);

	dz_init_ports();

	save_flags(flags);
	cli();

#ifndef CONFIG_SERIAL_DZ_CONSOLE
	/* reset the chip */
	dz_reset(&dz_ports[0]);
#endif

	/* order matters here... the trick is that flags
	   is updated... in request_irq - to immediatedly obliterate
	   it is unwise. */
	restore_flags(flags);

	if (request_irq(dz_ports[0].port.irq, dz_interrupt,
			SA_INTERRUPT, "DZ", &dz_ports[0]))
		panic("Unable to register DZ interrupt");

	ret = uart_register_driver(&dz_reg);
	if (ret != 0)
		return ret;

	for (i = 0; i < DZ_NB_PORT; i++)
		uart_add_one_port(&dz_reg, &dz_ports[i].port);

	return ret;
}

MODULE_DESCRIPTION("DECstation DZ serial driver");
MODULE_LICENSE("GPL");
