/*
 * dz.c: Serial port driver for DECstations equipped
 *       with the DZ chipset.
 *
 * Copyright (C) 1998 Olivier A. D. Lebaillif
 *
 * Email: olivier.lebaillif@ifrsys.com
 *
 * Copyright (C) 2004, 2006, 2007  Maciej W. Rozycki
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

#if defined(CONFIG_SERIAL_DZ_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/module.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/sysrq.h>
#include <linux/tty.h>

#include <asm/bootinfo.h>
#include <asm/system.h>

#include <asm/dec/interrupts.h>
#include <asm/dec/kn01.h>
#include <asm/dec/kn02.h>
#include <asm/dec/machtype.h>
#include <asm/dec/prom.h>

#include "dz.h"

static char *dz_name = "DECstation DZ serial driver version ";
static char *dz_version = "1.03";

struct dz_port {
	struct uart_port	port;
	unsigned int		cflag;
};

static struct dz_port dz_ports[DZ_NB_PORT];

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
	dz_out(dport, DZ_LPR, dport->cflag | dport->port.line);
	spin_unlock_irqrestore(&dport->port.lock, flags);
}

static void dz_enable_ms(struct uart_port *port)
{
	/* nothing to do */
}

/*
 * ------------------------------------------------------------
 *
 * Here start the interrupt handling routines.  All of the following
 * subroutines are declared as inline and are folded into
 * dz_interrupt.  They were separated out for readability's sake.
 *
 * Note: dz_interrupt() is a "fast" interrupt, which means that it
 * runs with interrupts turned off.  People who may want to modify
 * dz_interrupt() should try to keep the interrupt handler as fast as
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
static inline void dz_receive_chars(struct dz_port *dport_in)
{
	struct dz_port *dport;
	struct tty_struct *tty = NULL;
	struct uart_icount *icount;
	int lines_rx[DZ_NB_PORT] = { [0 ... DZ_NB_PORT - 1] = 0 };
	unsigned short status;
	unsigned char ch, flag;
	int i;

	while ((status = dz_in(dport_in, DZ_RBUF)) & DZ_DVAL) {
		dport = &dz_ports[LINE(status)];
		tty = dport->port.info->tty;	/* point to the proper dev */

		ch = UCHAR(status);		/* grab the char */

		icount = &dport->port.icount;
		icount->rx++;

		flag = TTY_NORMAL;
		if (status & DZ_FERR) {		/* frame error */
			/*
			 * There is no separate BREAK status bit, so
			 * treat framing errors as BREAKs for Magic SysRq
			 * and SAK; normally, otherwise.
			 */
			if (uart_handle_break(&dport->port))
				continue;
			if (dport->port.flags & UPF_SAK)
				flag = TTY_BREAK;
			else
				flag = TTY_FRAME;
		} else if (status & DZ_OERR)	/* overrun error */
			flag = TTY_OVERRUN;
		else if (status & DZ_PERR)	/* parity error */
			flag = TTY_PARITY;

		/* keep track of the statistics */
		switch (flag) {
		case TTY_FRAME:
			icount->frame++;
			break;
		case TTY_PARITY:
			icount->parity++;
			break;
		case TTY_OVERRUN:
			icount->overrun++;
			break;
		case TTY_BREAK:
			icount->brk++;
			break;
		default:
			break;
		}

		if (uart_handle_sysrq_char(&dport->port, ch))
			continue;

		if ((status & dport->port.ignore_status_mask) == 0) {
			uart_insert_char(&dport->port,
					 status, DZ_OERR, ch, flag);
			lines_rx[LINE(status)] = 1;
		}
	}
	for (i = 0; i < DZ_NB_PORT; i++)
		if (lines_rx[i])
			tty_flip_buffer_push(dz_ports[i].port.info->tty);
}

/*
 * ------------------------------------------------------------
 * transmit_char ()
 *
 * This routine deals with outputs to any lines.
 * ------------------------------------------------------------
 */
static inline void dz_transmit_chars(struct dz_port *dport_in)
{
	struct dz_port *dport;
	struct circ_buf *xmit;
	unsigned short status;
	unsigned char tmp;

	status = dz_in(dport_in, DZ_CSR);
	dport = &dz_ports[LINE(status)];
	xmit = &dport->port.info->xmit;

	if (dport->port.x_char) {		/* XON/XOFF chars */
		dz_out(dport, DZ_TDR, dport->port.x_char);
		dport->port.icount.tx++;
		dport->port.x_char = 0;
		return;
	}
	/* If nothing to do or stopped or hardware stopped. */
	if (uart_circ_empty(xmit) || uart_tx_stopped(&dport->port)) {
		dz_stop_tx(&dport->port);
		return;
	}

	/*
	 * If something to do... (remember the dz has no output fifo,
	 * so we go one char at a time) :-<
	 */
	tmp = xmit->buf[xmit->tail];
	xmit->tail = (xmit->tail + 1) & (DZ_XMIT_SIZE - 1);
	dz_out(dport, DZ_TDR, tmp);
	dport->port.icount.tx++;

	if (uart_circ_chars_pending(xmit) < DZ_WAKEUP_CHARS)
		uart_write_wakeup(&dport->port);

	/* Are we are done. */
	if (uart_circ_empty(xmit))
		dz_stop_tx(&dport->port);
}

/*
 * ------------------------------------------------------------
 * check_modem_status()
 *
 * DS 3100 & 5100: Only valid for the MODEM line, duh!
 * DS 5000/200: Valid for the MODEM and PRINTER line.
 * ------------------------------------------------------------
 */
static inline void check_modem_status(struct dz_port *dport)
{
	/*
	 * FIXME:
	 * 1. No status change interrupt; use a timer.
	 * 2. Handle the 3100/5000 as appropriate. --macro
	 */
	unsigned short status;

	/* If not the modem line just return.  */
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
static irqreturn_t dz_interrupt(int irq, void *dev)
{
	struct dz_port *dport = dev;
	unsigned short status;

	/* get the reason why we just got an irq */
	status = dz_in(dport, DZ_CSR);

	if ((status & (DZ_RDONE | DZ_RIE)) == (DZ_RDONE | DZ_RIE))
		dz_receive_chars(dport);

	if ((status & (DZ_TRDY | DZ_TIE)) == (DZ_TRDY | DZ_TIE))
		dz_transmit_chars(dport);

	return IRQ_HANDLED;
}

/*
 * -------------------------------------------------------------------
 * Here ends the DZ interrupt routines.
 * -------------------------------------------------------------------
 */

static unsigned int dz_get_mctrl(struct uart_port *uport)
{
	/*
	 * FIXME: Handle the 3100/5000 as appropriate. --macro
	 */
	struct dz_port *dport = (struct dz_port *)uport;
	unsigned int mctrl = TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;

	if (dport->port.line == DZ_MODEM) {
		if (dz_in(dport, DZ_MSR) & DZ_MODEM_DSR)
			mctrl &= ~TIOCM_DSR;
	}

	return mctrl;
}

static void dz_set_mctrl(struct uart_port *uport, unsigned int mctrl)
{
	/*
	 * FIXME: Handle the 3100/5000 as appropriate. --macro
	 */
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
 * -------------------------------------------------------------------
 * dz_tx_empty() -- get the transmitter empty status
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 *          is emptied.  On bus types like RS485, the transmitter must
 *          release the bus after transmitting. This must be done when
 *          the transmit shift register is empty, not be done when the
 *          transmit holding register is empty.  This functionality
 *          allows an RS485 driver to be written in user space.
 * -------------------------------------------------------------------
 */
static unsigned int dz_tx_empty(struct uart_port *uport)
{
	struct dz_port *dport = (struct dz_port *)uport;
	unsigned short tmp, mask = 1 << dport->port.line;

	tmp = dz_in(dport, DZ_TCR);
	tmp &= mask;

	return tmp ? 0 : TIOCSER_TEMT;
}

static void dz_break_ctl(struct uart_port *uport, int break_state)
{
	/*
	 * FIXME: Can't access BREAK bits in TDR easily;
	 * reuse the code for polled TX. --macro
	 */
	struct dz_port *dport = (struct dz_port *)uport;
	unsigned long flags;
	unsigned short tmp, mask = 1 << dport->port.line;

	spin_lock_irqsave(&uport->lock, flags);
	tmp = dz_in(dport, DZ_TCR);
	if (break_state)
		tmp |= mask;
	else
		tmp &= ~mask;
	dz_out(dport, DZ_TCR, tmp);
	spin_unlock_irqrestore(&uport->lock, flags);
}

static void dz_set_termios(struct uart_port *uport, struct ktermios *termios,
			   struct ktermios *old_termios)
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

	dz_out(dport, DZ_LPR, cflag | dport->port.line);
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
		dport->port.iotype	= UPIO_MEM;
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
	iob();

	/* enable scanning */
	dz_out(dport, DZ_CSR, DZ_MSE);
}

#ifdef CONFIG_SERIAL_DZ_CONSOLE
/*
 * -------------------------------------------------------------------
 * dz_console_putchar() -- transmit a character
 *
 * Polled transmission.  This is tricky.  We need to mask transmit
 * interrupts so that they do not interfere, enable the transmitter
 * for the line requested and then wait till the transmit scanner
 * requests data for this line.  But it may request data for another
 * line first, in which case we have to disable its transmitter and
 * repeat waiting till our line pops up.  Only then the character may
 * be transmitted.  Finally, the state of the transmitter mask is
 * restored.  Welcome to the world of PDP-11!
 * -------------------------------------------------------------------
 */
static void dz_console_putchar(struct uart_port *uport, int ch)
{
	struct dz_port *dport = (struct dz_port *)uport;
	unsigned long flags;
	unsigned short csr, tcr, trdy, mask;
	int loops = 10000;

	spin_lock_irqsave(&dport->port.lock, flags);
	csr = dz_in(dport, DZ_CSR);
	dz_out(dport, DZ_CSR, csr & ~DZ_TIE);
	tcr = dz_in(dport, DZ_TCR);
	tcr |= 1 << dport->port.line;
	mask = tcr;
	dz_out(dport, DZ_TCR, mask);
	iob();
	spin_unlock_irqrestore(&dport->port.lock, flags);

	do {
		trdy = dz_in(dport, DZ_CSR);
		if (!(trdy & DZ_TRDY))
			continue;
		trdy = (trdy & DZ_TLINE) >> 8;
		if (trdy == dport->port.line)
			break;
		mask &= ~(1 << trdy);
		dz_out(dport, DZ_TCR, mask);
		iob();
		udelay(2);
	} while (loops--);

	if (loops)				/* Cannot send otherwise. */
		dz_out(dport, DZ_TDR, ch);

	dz_out(dport, DZ_TCR, tcr);
	dz_out(dport, DZ_CSR, csr);
}

/*
 * -------------------------------------------------------------------
 * dz_console_print ()
 *
 * dz_console_print is registered for printk.
 * The console must be locked when we get here.
 * -------------------------------------------------------------------
 */
static void dz_console_print(struct console *co,
			     const char *str,
			     unsigned int count)
{
	struct dz_port *dport = &dz_ports[co->index];
#ifdef DEBUG_DZ
	prom_printf((char *) str);
#endif
	uart_console_write(&dport->port, str, count, dz_console_putchar);
}

static int __init dz_console_setup(struct console *co, char *options)
{
	struct dz_port *dport = &dz_ports[co->index];
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	dz_reset(dport);

	return uart_set_options(&dport->port, co, baud, parity, bits, flow);
}

static struct uart_driver dz_reg;
static struct console dz_console = {
	.name	= "ttyS",
	.write	= dz_console_print,
	.device	= uart_console_device,
	.setup	= dz_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
	.data	= &dz_reg,
};

static int __init dz_serial_console_init(void)
{
	if (!IOASIC) {
		dz_init_ports();
		register_console(&dz_console);
		return 0;
	} else
		return -ENXIO;
}

console_initcall(dz_serial_console_init);

#define SERIAL_DZ_CONSOLE	&dz_console
#else
#define SERIAL_DZ_CONSOLE	NULL
#endif /* CONFIG_SERIAL_DZ_CONSOLE */

static struct uart_driver dz_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "serial",
	.dev_name		= "ttyS",
	.major			= TTY_MAJOR,
	.minor			= 64,
	.nr			= DZ_NB_PORT,
	.cons			= SERIAL_DZ_CONSOLE,
};

static int __init dz_init(void)
{
	int ret, i;

	if (IOASIC)
		return -ENXIO;

	printk("%s%s\n", dz_name, dz_version);

	dz_init_ports();

#ifndef CONFIG_SERIAL_DZ_CONSOLE
	/* reset the chip */
	dz_reset(&dz_ports[0]);
#endif

	ret = uart_register_driver(&dz_reg);
	if (ret != 0)
		goto out;

	ret = request_irq(dz_ports[0].port.irq, dz_interrupt, IRQF_DISABLED,
			  "DZ", &dz_ports[0]);
	if (ret != 0) {
		printk(KERN_ERR "dz: Cannot get IRQ %d!\n",
		       dz_ports[0].port.irq);
		goto out_unregister;
	}

	for (i = 0; i < DZ_NB_PORT; i++)
		uart_add_one_port(&dz_reg, &dz_ports[i].port);

	return ret;

out_unregister:
	uart_unregister_driver(&dz_reg);

out:
	return ret;
}

module_init(dz_init);

MODULE_DESCRIPTION("DECstation DZ serial driver");
MODULE_LICENSE("GPL");
