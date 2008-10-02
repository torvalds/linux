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
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/module.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/sysrq.h>
#include <linux/tty.h>

#include <asm/atomic.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/system.h>

#include <asm/dec/interrupts.h>
#include <asm/dec/kn01.h>
#include <asm/dec/kn02.h>
#include <asm/dec/machtype.h>
#include <asm/dec/prom.h>
#include <asm/dec/system.h>

#include "dz.h"


MODULE_DESCRIPTION("DECstation DZ serial driver");
MODULE_LICENSE("GPL");


static char dz_name[] __initdata = "DECstation DZ serial driver version ";
static char dz_version[] __initdata = "1.04";

struct dz_port {
	struct dz_mux		*mux;
	struct uart_port	port;
	unsigned int		cflag;
};

struct dz_mux {
	struct dz_port		dport[DZ_NB_PORT];
	atomic_t		map_guard;
	atomic_t		irq_guard;
	int			initialised;
};

static struct dz_mux dz_mux;

static inline struct dz_port *to_dport(struct uart_port *uport)
{
	return container_of(uport, struct dz_port, port);
}

/*
 * ------------------------------------------------------------
 * dz_in () and dz_out ()
 *
 * These routines are used to access the registers of the DZ
 * chip, hiding relocation differences between implementation.
 * ------------------------------------------------------------
 */

static u16 dz_in(struct dz_port *dport, unsigned offset)
{
	void __iomem *addr = dport->port.membase + offset;

	return readw(addr);
}

static void dz_out(struct dz_port *dport, unsigned offset, u16 value)
{
	void __iomem *addr = dport->port.membase + offset;

	writew(value, addr);
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
	struct dz_port *dport = to_dport(uport);
	u16 tmp, mask = 1 << dport->port.line;

	tmp = dz_in(dport, DZ_TCR);	/* read the TX flag */
	tmp &= ~mask;			/* clear the TX flag */
	dz_out(dport, DZ_TCR, tmp);
}

static void dz_start_tx(struct uart_port *uport)
{
	struct dz_port *dport = to_dport(uport);
	u16 tmp, mask = 1 << dport->port.line;

	tmp = dz_in(dport, DZ_TCR);	/* read the TX flag */
	tmp |= mask;			/* set the TX flag */
	dz_out(dport, DZ_TCR, tmp);
}

static void dz_stop_rx(struct uart_port *uport)
{
	struct dz_port *dport = to_dport(uport);

	dport->cflag &= ~DZ_RXENAB;
	dz_out(dport, DZ_LPR, dport->cflag);
}

static void dz_enable_ms(struct uart_port *uport)
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
static inline void dz_receive_chars(struct dz_mux *mux)
{
	struct uart_port *uport;
	struct dz_port *dport = &mux->dport[0];
	struct tty_struct *tty = NULL;
	struct uart_icount *icount;
	int lines_rx[DZ_NB_PORT] = { [0 ... DZ_NB_PORT - 1] = 0 };
	unsigned char ch, flag;
	u16 status;
	int i;

	while ((status = dz_in(dport, DZ_RBUF)) & DZ_DVAL) {
		dport = &mux->dport[LINE(status)];
		uport = &dport->port;
		tty = uport->info->port.tty;	/* point to the proper dev */

		ch = UCHAR(status);		/* grab the char */
		flag = TTY_NORMAL;

		icount = &uport->icount;
		icount->rx++;

		if (unlikely(status & (DZ_OERR | DZ_FERR | DZ_PERR))) {

			/*
			 * There is no separate BREAK status bit, so treat
			 * null characters with framing errors as BREAKs;
			 * normally, otherwise.  For this move the Framing
			 * Error bit to a simulated BREAK bit.
			 */
			if (!ch) {
				status |= (status & DZ_FERR) >>
					  (ffs(DZ_FERR) - ffs(DZ_BREAK));
				status &= ~DZ_FERR;
			}

			/* Handle SysRq/SAK & keep track of the statistics. */
			if (status & DZ_BREAK) {
				icount->brk++;
				if (uart_handle_break(uport))
					continue;
			} else if (status & DZ_FERR)
				icount->frame++;
			else if (status & DZ_PERR)
				icount->parity++;
			if (status & DZ_OERR)
				icount->overrun++;

			status &= uport->read_status_mask;
			if (status & DZ_BREAK)
				flag = TTY_BREAK;
			else if (status & DZ_FERR)
				flag = TTY_FRAME;
			else if (status & DZ_PERR)
				flag = TTY_PARITY;

		}

		if (uart_handle_sysrq_char(uport, ch))
			continue;

		uart_insert_char(uport, status, DZ_OERR, ch, flag);
		lines_rx[LINE(status)] = 1;
	}
	for (i = 0; i < DZ_NB_PORT; i++)
		if (lines_rx[i])
			tty_flip_buffer_push(mux->dport[i].port.info->port.tty);
}

/*
 * ------------------------------------------------------------
 * transmit_char ()
 *
 * This routine deals with outputs to any lines.
 * ------------------------------------------------------------
 */
static inline void dz_transmit_chars(struct dz_mux *mux)
{
	struct dz_port *dport = &mux->dport[0];
	struct circ_buf *xmit;
	unsigned char tmp;
	u16 status;

	status = dz_in(dport, DZ_CSR);
	dport = &mux->dport[LINE(status)];
	xmit = &dport->port.info->xmit;

	if (dport->port.x_char) {		/* XON/XOFF chars */
		dz_out(dport, DZ_TDR, dport->port.x_char);
		dport->port.icount.tx++;
		dport->port.x_char = 0;
		return;
	}
	/* If nothing to do or stopped or hardware stopped. */
	if (uart_circ_empty(xmit) || uart_tx_stopped(&dport->port)) {
		spin_lock(&dport->port.lock);
		dz_stop_tx(&dport->port);
		spin_unlock(&dport->port.lock);
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
	if (uart_circ_empty(xmit)) {
		spin_lock(&dport->port.lock);
		dz_stop_tx(&dport->port);
		spin_unlock(&dport->port.lock);
	}
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
	u16 status;

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
static irqreturn_t dz_interrupt(int irq, void *dev_id)
{
	struct dz_mux *mux = dev_id;
	struct dz_port *dport = &mux->dport[0];
	u16 status;

	/* get the reason why we just got an irq */
	status = dz_in(dport, DZ_CSR);

	if ((status & (DZ_RDONE | DZ_RIE)) == (DZ_RDONE | DZ_RIE))
		dz_receive_chars(mux);

	if ((status & (DZ_TRDY | DZ_TIE)) == (DZ_TRDY | DZ_TIE))
		dz_transmit_chars(mux);

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
	struct dz_port *dport = to_dport(uport);
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
	struct dz_port *dport = to_dport(uport);
	u16 tmp;

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
	struct dz_port *dport = to_dport(uport);
	struct dz_mux *mux = dport->mux;
	unsigned long flags;
	int irq_guard;
	int ret;
	u16 tmp;

	irq_guard = atomic_add_return(1, &mux->irq_guard);
	if (irq_guard != 1)
		return 0;

	ret = request_irq(dport->port.irq, dz_interrupt,
			  IRQF_SHARED, "dz", mux);
	if (ret) {
		atomic_add(-1, &mux->irq_guard);
		printk(KERN_ERR "dz: Cannot get IRQ %d!\n", dport->port.irq);
		return ret;
	}

	spin_lock_irqsave(&dport->port.lock, flags);

	/* Enable interrupts.  */
	tmp = dz_in(dport, DZ_CSR);
	tmp |= DZ_RIE | DZ_TIE;
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
	struct dz_port *dport = to_dport(uport);
	struct dz_mux *mux = dport->mux;
	unsigned long flags;
	int irq_guard;
	u16 tmp;

	spin_lock_irqsave(&dport->port.lock, flags);
	dz_stop_tx(&dport->port);
	spin_unlock_irqrestore(&dport->port.lock, flags);

	irq_guard = atomic_add_return(-1, &mux->irq_guard);
	if (!irq_guard) {
		/* Disable interrupts.  */
		tmp = dz_in(dport, DZ_CSR);
		tmp &= ~(DZ_RIE | DZ_TIE);
		dz_out(dport, DZ_CSR, tmp);

		free_irq(dport->port.irq, mux);
	}
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
	struct dz_port *dport = to_dport(uport);
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
	struct dz_port *dport = to_dport(uport);
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

static int dz_encode_baud_rate(unsigned int baud)
{
	switch (baud) {
	case 50:
		return DZ_B50;
	case 75:
		return DZ_B75;
	case 110:
		return DZ_B110;
	case 134:
		return DZ_B134;
	case 150:
		return DZ_B150;
	case 300:
		return DZ_B300;
	case 600:
		return DZ_B600;
	case 1200:
		return DZ_B1200;
	case 1800:
		return DZ_B1800;
	case 2000:
		return DZ_B2000;
	case 2400:
		return DZ_B2400;
	case 3600:
		return DZ_B3600;
	case 4800:
		return DZ_B4800;
	case 7200:
		return DZ_B7200;
	case 9600:
		return DZ_B9600;
	default:
		return -1;
	}
}


static void dz_reset(struct dz_port *dport)
{
	struct dz_mux *mux = dport->mux;

	if (mux->initialised)
		return;

	dz_out(dport, DZ_CSR, DZ_CLR);
	while (dz_in(dport, DZ_CSR) & DZ_CLR);
	iob();

	/* Enable scanning.  */
	dz_out(dport, DZ_CSR, DZ_MSE);

	mux->initialised = 1;
}

static void dz_set_termios(struct uart_port *uport, struct ktermios *termios,
			   struct ktermios *old_termios)
{
	struct dz_port *dport = to_dport(uport);
	unsigned long flags;
	unsigned int cflag, baud;
	int bflag;

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
	bflag = dz_encode_baud_rate(baud);
	if (bflag < 0)	{			/* Try to keep unchanged.  */
		baud = uart_get_baud_rate(uport, old_termios, NULL, 50, 9600);
		bflag = dz_encode_baud_rate(baud);
		if (bflag < 0)	{		/* Resort to 9600.  */
			baud = 9600;
			bflag = DZ_B9600;
		}
		tty_termios_encode_baud_rate(termios, baud, baud);
	}
	cflag |= bflag;

	if (termios->c_cflag & CREAD)
		cflag |= DZ_RXENAB;

	spin_lock_irqsave(&dport->port.lock, flags);

	uart_update_timeout(uport, termios->c_cflag, baud);

	dz_out(dport, DZ_LPR, cflag);
	dport->cflag = cflag;

	/* setup accept flag */
	dport->port.read_status_mask = DZ_OERR;
	if (termios->c_iflag & INPCK)
		dport->port.read_status_mask |= DZ_FERR | DZ_PERR;
	if (termios->c_iflag & (BRKINT | PARMRK))
		dport->port.read_status_mask |= DZ_BREAK;

	/* characters to ignore */
	uport->ignore_status_mask = 0;
	if ((termios->c_iflag & (IGNPAR | IGNBRK)) == (IGNPAR | IGNBRK))
		dport->port.ignore_status_mask |= DZ_OERR;
	if (termios->c_iflag & IGNPAR)
		dport->port.ignore_status_mask |= DZ_FERR | DZ_PERR;
	if (termios->c_iflag & IGNBRK)
		dport->port.ignore_status_mask |= DZ_BREAK;

	spin_unlock_irqrestore(&dport->port.lock, flags);
}

/*
 * Hack alert!
 * Required solely so that the initial PROM-based console
 * works undisturbed in parallel with this one.
 */
static void dz_pm(struct uart_port *uport, unsigned int state,
		  unsigned int oldstate)
{
	struct dz_port *dport = to_dport(uport);
	unsigned long flags;

	spin_lock_irqsave(&dport->port.lock, flags);
	if (state < 3)
		dz_start_tx(&dport->port);
	else
		dz_stop_tx(&dport->port);
	spin_unlock_irqrestore(&dport->port.lock, flags);
}


static const char *dz_type(struct uart_port *uport)
{
	return "DZ";
}

static void dz_release_port(struct uart_port *uport)
{
	struct dz_mux *mux = to_dport(uport)->mux;
	int map_guard;

	iounmap(uport->membase);
	uport->membase = NULL;

	map_guard = atomic_add_return(-1, &mux->map_guard);
	if (!map_guard)
		release_mem_region(uport->mapbase, dec_kn_slot_size);
}

static int dz_map_port(struct uart_port *uport)
{
	if (!uport->membase)
		uport->membase = ioremap_nocache(uport->mapbase,
						 dec_kn_slot_size);
	if (!uport->membase) {
		printk(KERN_ERR "dz: Cannot map MMIO\n");
		return -ENOMEM;
	}
	return 0;
}

static int dz_request_port(struct uart_port *uport)
{
	struct dz_mux *mux = to_dport(uport)->mux;
	int map_guard;
	int ret;

	map_guard = atomic_add_return(1, &mux->map_guard);
	if (map_guard == 1) {
		if (!request_mem_region(uport->mapbase, dec_kn_slot_size,
					"dz")) {
			atomic_add(-1, &mux->map_guard);
			printk(KERN_ERR
			       "dz: Unable to reserve MMIO resource\n");
			return -EBUSY;
		}
	}
	ret = dz_map_port(uport);
	if (ret) {
		map_guard = atomic_add_return(-1, &mux->map_guard);
		if (!map_guard)
			release_mem_region(uport->mapbase, dec_kn_slot_size);
		return ret;
	}
	return 0;
}

static void dz_config_port(struct uart_port *uport, int flags)
{
	struct dz_port *dport = to_dport(uport);

	if (flags & UART_CONFIG_TYPE) {
		if (dz_request_port(uport))
			return;

		uport->type = PORT_DZ;

		dz_reset(dport);
	}
}

/*
 * Verify the new serial_struct (for TIOCSSERIAL).
 */
static int dz_verify_port(struct uart_port *uport, struct serial_struct *ser)
{
	int ret = 0;

	if (ser->type != PORT_UNKNOWN && ser->type != PORT_DZ)
		ret = -EINVAL;
	if (ser->irq != uport->irq)
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
	.pm		= dz_pm,
	.type		= dz_type,
	.release_port	= dz_release_port,
	.request_port	= dz_request_port,
	.config_port	= dz_config_port,
	.verify_port	= dz_verify_port,
};

static void __init dz_init_ports(void)
{
	static int first = 1;
	unsigned long base;
	int line;

	if (!first)
		return;
	first = 0;

	if (mips_machtype == MACH_DS23100 || mips_machtype == MACH_DS5100)
		base = dec_kn_slot_base + KN01_DZ11;
	else
		base = dec_kn_slot_base + KN02_DZ11;

	for (line = 0; line < DZ_NB_PORT; line++) {
		struct dz_port *dport = &dz_mux.dport[line];
		struct uart_port *uport = &dport->port;

		dport->mux	= &dz_mux;

		uport->irq	= dec_interrupt[DEC_IRQ_DZ11];
		uport->fifosize	= 1;
		uport->iotype	= UPIO_MEM;
		uport->flags	= UPF_BOOT_AUTOCONF;
		uport->ops	= &dz_ops;
		uport->line	= line;
		uport->mapbase	= base;
	}
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
	struct dz_port *dport = to_dport(uport);
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
	} while (--loops);

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
	struct dz_port *dport = &dz_mux.dport[co->index];
#ifdef DEBUG_DZ
	prom_printf((char *) str);
#endif
	uart_console_write(&dport->port, str, count, dz_console_putchar);
}

static int __init dz_console_setup(struct console *co, char *options)
{
	struct dz_port *dport = &dz_mux.dport[co->index];
	struct uart_port *uport = &dport->port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;

	ret = dz_map_port(uport);
	if (ret)
		return ret;

	spin_lock_init(&dport->port.lock);	/* For dz_pm().  */

	dz_reset(dport);
	dz_pm(uport, 0, -1);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

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

	ret = uart_register_driver(&dz_reg);
	if (ret)
		return ret;

	for (i = 0; i < DZ_NB_PORT; i++)
		uart_add_one_port(&dz_reg, &dz_mux.dport[i].port);

	return 0;
}

module_init(dz_init);
