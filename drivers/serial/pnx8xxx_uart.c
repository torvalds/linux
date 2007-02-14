/*
 * UART driver for PNX8XXX SoCs
 *
 * Author: Per Hallsmark per.hallsmark@mvista.com
 * Ported to 2.6 kernel by EmbeddedAlley
 * Reworked by Vitaly Wool <vitalywool@gmail.com>
 *
 * Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 * Copyright (C) 2000 Deep Blue Solutions Ltd.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of
 * any kind, whether express or implied.
 *
 */

#if defined(CONFIG_SERIAL_PNX8XXX_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/serial_pnx8xxx.h>

#include <asm/io.h>
#include <asm/irq.h>

/* We'll be using StrongARM sa1100 serial port major/minor */
#define SERIAL_PNX8XXX_MAJOR	204
#define MINOR_START		5

#define NR_PORTS		2

#define PNX8XXX_ISR_PASS_LIMIT	256

/*
 * Convert from ignore_status_mask or read_status_mask to FIFO
 * and interrupt status bits
 */
#define SM_TO_FIFO(x)	((x) >> 10)
#define SM_TO_ISTAT(x)	((x) & 0x000001ff)
#define FIFO_TO_SM(x)	((x) << 10)
#define ISTAT_TO_SM(x)	((x) & 0x000001ff)

/*
 * This is the size of our serial port register set.
 */
#define UART_PORT_SIZE	0x1000

/*
 * This determines how often we check the modem status signals
 * for any change.  They generally aren't connected to an IRQ
 * so we have to poll them.  We also check immediately before
 * filling the TX fifo incase CTS has been dropped.
 */
#define MCTRL_TIMEOUT	(250*HZ/1000)

extern struct pnx8xxx_port pnx8xxx_ports[];

static inline int serial_in(struct pnx8xxx_port *sport, int offset)
{
	return (__raw_readl(sport->port.membase + offset));
}

static inline void serial_out(struct pnx8xxx_port *sport, int offset, int value)
{
	__raw_writel(value, sport->port.membase + offset);
}

/*
 * Handle any change of modem status signal since we were last called.
 */
static void pnx8xxx_mctrl_check(struct pnx8xxx_port *sport)
{
	unsigned int status, changed;

	status = sport->port.ops->get_mctrl(&sport->port);
	changed = status ^ sport->old_status;

	if (changed == 0)
		return;

	sport->old_status = status;

	if (changed & TIOCM_RI)
		sport->port.icount.rng++;
	if (changed & TIOCM_DSR)
		sport->port.icount.dsr++;
	if (changed & TIOCM_CAR)
		uart_handle_dcd_change(&sport->port, status & TIOCM_CAR);
	if (changed & TIOCM_CTS)
		uart_handle_cts_change(&sport->port, status & TIOCM_CTS);

	wake_up_interruptible(&sport->port.info->delta_msr_wait);
}

/*
 * This is our per-port timeout handler, for checking the
 * modem status signals.
 */
static void pnx8xxx_timeout(unsigned long data)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)data;
	unsigned long flags;

	if (sport->port.info) {
		spin_lock_irqsave(&sport->port.lock, flags);
		pnx8xxx_mctrl_check(sport);
		spin_unlock_irqrestore(&sport->port.lock, flags);

		mod_timer(&sport->timer, jiffies + MCTRL_TIMEOUT);
	}
}

/*
 * interrupts disabled on entry
 */
static void pnx8xxx_stop_tx(struct uart_port *port)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;
	u32 ien;

	/* Disable TX intr */
	ien = serial_in(sport, PNX8XXX_IEN);
	serial_out(sport, PNX8XXX_IEN, ien & ~PNX8XXX_UART_INT_ALLTX);

	/* Clear all pending TX intr */
	serial_out(sport, PNX8XXX_ICLR, PNX8XXX_UART_INT_ALLTX);
}

/*
 * interrupts may not be disabled on entry
 */
static void pnx8xxx_start_tx(struct uart_port *port)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;
	u32 ien;

	/* Clear all pending TX intr */
	serial_out(sport, PNX8XXX_ICLR, PNX8XXX_UART_INT_ALLTX);

	/* Enable TX intr */
	ien = serial_in(sport, PNX8XXX_IEN);
	serial_out(sport, PNX8XXX_IEN, ien | PNX8XXX_UART_INT_ALLTX);
}

/*
 * Interrupts enabled
 */
static void pnx8xxx_stop_rx(struct uart_port *port)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;
	u32 ien;

	/* Disable RX intr */
	ien = serial_in(sport, PNX8XXX_IEN);
	serial_out(sport, PNX8XXX_IEN, ien & ~PNX8XXX_UART_INT_ALLRX);

	/* Clear all pending RX intr */
	serial_out(sport, PNX8XXX_ICLR, PNX8XXX_UART_INT_ALLRX);
}

/*
 * Set the modem control timer to fire immediately.
 */
static void pnx8xxx_enable_ms(struct uart_port *port)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;

	mod_timer(&sport->timer, jiffies);
}

static void pnx8xxx_rx_chars(struct pnx8xxx_port *sport)
{
	struct tty_struct *tty = sport->port.info->tty;
	unsigned int status, ch, flg;

	status = FIFO_TO_SM(serial_in(sport, PNX8XXX_FIFO)) |
		 ISTAT_TO_SM(serial_in(sport, PNX8XXX_ISTAT));
	while (status & FIFO_TO_SM(PNX8XXX_UART_FIFO_RXFIFO)) {
		ch = serial_in(sport, PNX8XXX_FIFO);

		sport->port.icount.rx++;

		flg = TTY_NORMAL;

		/*
		 * note that the error handling code is
		 * out of the main execution path
		 */
		if (status & (FIFO_TO_SM(PNX8XXX_UART_FIFO_RXFE |
					PNX8XXX_UART_FIFO_RXPAR) |
			      ISTAT_TO_SM(PNX8XXX_UART_INT_RXOVRN))) {
			if (status & FIFO_TO_SM(PNX8XXX_UART_FIFO_RXPAR))
				sport->port.icount.parity++;
			else if (status & FIFO_TO_SM(PNX8XXX_UART_FIFO_RXFE))
				sport->port.icount.frame++;
			if (status & ISTAT_TO_SM(PNX8XXX_UART_INT_RXOVRN))
				sport->port.icount.overrun++;

			status &= sport->port.read_status_mask;

			if (status & FIFO_TO_SM(PNX8XXX_UART_FIFO_RXPAR))
				flg = TTY_PARITY;
			else if (status & FIFO_TO_SM(PNX8XXX_UART_FIFO_RXFE))
				flg = TTY_FRAME;

#ifdef SUPPORT_SYSRQ
			sport->port.sysrq = 0;
#endif
		}

		if (uart_handle_sysrq_char(&sport->port, ch))
			goto ignore_char;

		uart_insert_char(&sport->port, status,
				ISTAT_TO_SM(PNX8XXX_UART_INT_RXOVRN), ch, flg);

	ignore_char:
		serial_out(sport, PNX8XXX_LCR, serial_in(sport, PNX8XXX_LCR) |
				PNX8XXX_UART_LCR_RX_NEXT);
		status = FIFO_TO_SM(serial_in(sport, PNX8XXX_FIFO)) |
			 ISTAT_TO_SM(serial_in(sport, PNX8XXX_ISTAT));
	}
	tty_flip_buffer_push(tty);
}

static void pnx8xxx_tx_chars(struct pnx8xxx_port *sport)
{
	struct circ_buf *xmit = &sport->port.info->xmit;

	if (sport->port.x_char) {
		serial_out(sport, PNX8XXX_FIFO, sport->port.x_char);
		sport->port.icount.tx++;
		sport->port.x_char = 0;
		return;
	}

	/*
	 * Check the modem control lines before
	 * transmitting anything.
	 */
	pnx8xxx_mctrl_check(sport);

	if (uart_circ_empty(xmit) || uart_tx_stopped(&sport->port)) {
		pnx8xxx_stop_tx(&sport->port);
		return;
	}

	/*
	 * TX while bytes available
	 */
	while (((serial_in(sport, PNX8XXX_FIFO) &
					PNX8XXX_UART_FIFO_TXFIFO) >> 16) < 16) {
		serial_out(sport, PNX8XXX_FIFO, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		sport->port.icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&sport->port);

	if (uart_circ_empty(xmit))
		pnx8xxx_stop_tx(&sport->port);
}

static irqreturn_t pnx8xxx_int(int irq, void *dev_id)
{
	struct pnx8xxx_port *sport = dev_id;
	unsigned int status;

	spin_lock(&sport->port.lock);
	/* Get the interrupts */
	status  = serial_in(sport, PNX8XXX_ISTAT) & serial_in(sport, PNX8XXX_IEN);

	/* Break signal received */
	if (status & PNX8XXX_UART_INT_BREAK) {
		sport->port.icount.brk++;
		uart_handle_break(&sport->port);
	}

	/* Byte received */
	if (status & PNX8XXX_UART_INT_RX)
		pnx8xxx_rx_chars(sport);

	/* TX holding register empty - transmit a byte */
	if (status & PNX8XXX_UART_INT_TX)
		pnx8xxx_tx_chars(sport);

	/* Clear the ISTAT register */
	serial_out(sport, PNX8XXX_ICLR, status);

	spin_unlock(&sport->port.lock);
	return IRQ_HANDLED;
}

/*
 * Return TIOCSER_TEMT when transmitter is not busy.
 */
static unsigned int pnx8xxx_tx_empty(struct uart_port *port)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;

	return serial_in(sport, PNX8XXX_FIFO) & PNX8XXX_UART_FIFO_TXFIFO_STA ? 0 : TIOCSER_TEMT;
}

static unsigned int pnx8xxx_get_mctrl(struct uart_port *port)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;
	unsigned int mctrl = TIOCM_DSR;
	unsigned int msr;

	/* REVISIT */

	msr = serial_in(sport, PNX8XXX_MCR);

	mctrl |= msr & PNX8XXX_UART_MCR_CTS ? TIOCM_CTS : 0;
	mctrl |= msr & PNX8XXX_UART_MCR_DCD ? TIOCM_CAR : 0;

	return mctrl;
}

static void pnx8xxx_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
#if	0	/* FIXME */
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;
	unsigned int msr;
#endif
}

/*
 * Interrupts always disabled.
 */
static void pnx8xxx_break_ctl(struct uart_port *port, int break_state)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;
	unsigned long flags;
	unsigned int lcr;

	spin_lock_irqsave(&sport->port.lock, flags);
	lcr = serial_in(sport, PNX8XXX_LCR);
	if (break_state == -1)
		lcr |= PNX8XXX_UART_LCR_TXBREAK;
	else
		lcr &= ~PNX8XXX_UART_LCR_TXBREAK;
	serial_out(sport, PNX8XXX_LCR, lcr);
	spin_unlock_irqrestore(&sport->port.lock, flags);
}

static int pnx8xxx_startup(struct uart_port *port)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;
	int retval;

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(sport->port.irq, pnx8xxx_int, 0,
			     "pnx8xxx-uart", sport);
	if (retval)
		return retval;

	/*
	 * Finally, clear and enable interrupts
	 */

	serial_out(sport, PNX8XXX_ICLR, PNX8XXX_UART_INT_ALLRX |
			     PNX8XXX_UART_INT_ALLTX);

	serial_out(sport, PNX8XXX_IEN, serial_in(sport, PNX8XXX_IEN) |
			    PNX8XXX_UART_INT_ALLRX |
			    PNX8XXX_UART_INT_ALLTX);

	/*
	 * Enable modem status interrupts
	 */
	spin_lock_irq(&sport->port.lock);
	pnx8xxx_enable_ms(&sport->port);
	spin_unlock_irq(&sport->port.lock);

	return 0;
}

static void pnx8xxx_shutdown(struct uart_port *port)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;
	int lcr;

	/*
	 * Stop our timer.
	 */
	del_timer_sync(&sport->timer);

	/*
	 * Disable all interrupts
	 */
	serial_out(sport, PNX8XXX_IEN, 0);

	/*
	 * Reset the Tx and Rx FIFOS, disable the break condition
	 */
	lcr = serial_in(sport, PNX8XXX_LCR);
	lcr &= ~PNX8XXX_UART_LCR_TXBREAK;
	lcr |= PNX8XXX_UART_LCR_TX_RST | PNX8XXX_UART_LCR_RX_RST;
	serial_out(sport, PNX8XXX_LCR, lcr);

	/*
	 * Clear all interrupts
	 */
	serial_out(sport, PNX8XXX_ICLR, PNX8XXX_UART_INT_ALLRX |
			     PNX8XXX_UART_INT_ALLTX);

	/*
	 * Free the interrupt
	 */
	free_irq(sport->port.irq, sport);
}

static void
pnx8xxx_set_termios(struct uart_port *port, struct ktermios *termios,
		   struct ktermios *old)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;
	unsigned long flags;
	unsigned int lcr_fcr, old_ien, baud, quot;
	unsigned int old_csize = old ? old->c_cflag & CSIZE : CS8;

	/*
	 * We only support CS7 and CS8.
	 */
	while ((termios->c_cflag & CSIZE) != CS7 &&
	       (termios->c_cflag & CSIZE) != CS8) {
		termios->c_cflag &= ~CSIZE;
		termios->c_cflag |= old_csize;
		old_csize = CS8;
	}

	if ((termios->c_cflag & CSIZE) == CS8)
		lcr_fcr = PNX8XXX_UART_LCR_8BIT;
	else
		lcr_fcr = 0;

	if (termios->c_cflag & CSTOPB)
		lcr_fcr |= PNX8XXX_UART_LCR_2STOPB;
	if (termios->c_cflag & PARENB) {
		lcr_fcr |= PNX8XXX_UART_LCR_PAREN;
		if (!(termios->c_cflag & PARODD))
			lcr_fcr |= PNX8XXX_UART_LCR_PAREVN;
	}

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16);
	quot = uart_get_divisor(port, baud);

	spin_lock_irqsave(&sport->port.lock, flags);

	sport->port.read_status_mask = ISTAT_TO_SM(PNX8XXX_UART_INT_RXOVRN) |
				ISTAT_TO_SM(PNX8XXX_UART_INT_EMPTY) |
				ISTAT_TO_SM(PNX8XXX_UART_INT_RX);
	if (termios->c_iflag & INPCK)
		sport->port.read_status_mask |=
			FIFO_TO_SM(PNX8XXX_UART_FIFO_RXFE) |
			FIFO_TO_SM(PNX8XXX_UART_FIFO_RXPAR);
	if (termios->c_iflag & (BRKINT | PARMRK))
		sport->port.read_status_mask |=
			ISTAT_TO_SM(PNX8XXX_UART_INT_BREAK);

	/*
	 * Characters to ignore
	 */
	sport->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		sport->port.ignore_status_mask |=
			FIFO_TO_SM(PNX8XXX_UART_FIFO_RXFE) |
			FIFO_TO_SM(PNX8XXX_UART_FIFO_RXPAR);
	if (termios->c_iflag & IGNBRK) {
		sport->port.ignore_status_mask |=
			ISTAT_TO_SM(PNX8XXX_UART_INT_BREAK);
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			sport->port.ignore_status_mask |=
				ISTAT_TO_SM(PNX8XXX_UART_INT_RXOVRN);
	}

	/*
	 * ignore all characters if CREAD is not set
	 */
	if ((termios->c_cflag & CREAD) == 0)
		sport->port.ignore_status_mask |=
			ISTAT_TO_SM(PNX8XXX_UART_INT_RX);

	del_timer_sync(&sport->timer);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	/*
	 * disable interrupts and drain transmitter
	 */
	old_ien = serial_in(sport, PNX8XXX_IEN);
	serial_out(sport, PNX8XXX_IEN, old_ien & ~(PNX8XXX_UART_INT_ALLTX |
					PNX8XXX_UART_INT_ALLRX));

	while (serial_in(sport, PNX8XXX_FIFO) & PNX8XXX_UART_FIFO_TXFIFO_STA)
		barrier();

	/* then, disable everything */
	serial_out(sport, PNX8XXX_IEN, 0);

	/* Reset the Rx and Tx FIFOs too */
	lcr_fcr |= PNX8XXX_UART_LCR_TX_RST;
	lcr_fcr |= PNX8XXX_UART_LCR_RX_RST;

	/* set the parity, stop bits and data size */
	serial_out(sport, PNX8XXX_LCR, lcr_fcr);

	/* set the baud rate */
	quot -= 1;
	serial_out(sport, PNX8XXX_BAUD, quot);

	serial_out(sport, PNX8XXX_ICLR, -1);

	serial_out(sport, PNX8XXX_IEN, old_ien);

	if (UART_ENABLE_MS(&sport->port, termios->c_cflag))
		pnx8xxx_enable_ms(&sport->port);

	spin_unlock_irqrestore(&sport->port.lock, flags);
}

static const char *pnx8xxx_type(struct uart_port *port)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;

	return sport->port.type == PORT_PNX8XXX ? "PNX8XXX" : NULL;
}

/*
 * Release the memory region(s) being used by 'port'.
 */
static void pnx8xxx_release_port(struct uart_port *port)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;

	release_mem_region(sport->port.mapbase, UART_PORT_SIZE);
}

/*
 * Request the memory region(s) being used by 'port'.
 */
static int pnx8xxx_request_port(struct uart_port *port)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;
	return request_mem_region(sport->port.mapbase, UART_PORT_SIZE,
			"pnx8xxx-uart") != NULL ? 0 : -EBUSY;
}

/*
 * Configure/autoconfigure the port.
 */
static void pnx8xxx_config_port(struct uart_port *port, int flags)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;

	if (flags & UART_CONFIG_TYPE &&
	    pnx8xxx_request_port(&sport->port) == 0)
		sport->port.type = PORT_PNX8XXX;
}

/*
 * Verify the new serial_struct (for TIOCSSERIAL).
 * The only change we allow are to the flags and type, and
 * even then only between PORT_PNX8XXX and PORT_UNKNOWN
 */
static int
pnx8xxx_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;
	int ret = 0;

	if (ser->type != PORT_UNKNOWN && ser->type != PORT_PNX8XXX)
		ret = -EINVAL;
	if (sport->port.irq != ser->irq)
		ret = -EINVAL;
	if (ser->io_type != SERIAL_IO_MEM)
		ret = -EINVAL;
	if (sport->port.uartclk / 16 != ser->baud_base)
		ret = -EINVAL;
	if ((void *)sport->port.mapbase != ser->iomem_base)
		ret = -EINVAL;
	if (sport->port.iobase != ser->port)
		ret = -EINVAL;
	if (ser->hub6 != 0)
		ret = -EINVAL;
	return ret;
}

static struct uart_ops pnx8xxx_pops = {
	.tx_empty	= pnx8xxx_tx_empty,
	.set_mctrl	= pnx8xxx_set_mctrl,
	.get_mctrl	= pnx8xxx_get_mctrl,
	.stop_tx	= pnx8xxx_stop_tx,
	.start_tx	= pnx8xxx_start_tx,
	.stop_rx	= pnx8xxx_stop_rx,
	.enable_ms	= pnx8xxx_enable_ms,
	.break_ctl	= pnx8xxx_break_ctl,
	.startup	= pnx8xxx_startup,
	.shutdown	= pnx8xxx_shutdown,
	.set_termios	= pnx8xxx_set_termios,
	.type		= pnx8xxx_type,
	.release_port	= pnx8xxx_release_port,
	.request_port	= pnx8xxx_request_port,
	.config_port	= pnx8xxx_config_port,
	.verify_port	= pnx8xxx_verify_port,
};


/*
 * Setup the PNX8XXX serial ports.
 *
 * Note also that we support "console=ttySx" where "x" is either 0 or 1.
 */
static void __init pnx8xxx_init_ports(void)
{
	static int first = 1;
	int i;

	if (!first)
		return;
	first = 0;

	for (i = 0; i < NR_PORTS; i++) {
		init_timer(&pnx8xxx_ports[i].timer);
		pnx8xxx_ports[i].timer.function = pnx8xxx_timeout;
		pnx8xxx_ports[i].timer.data     = (unsigned long)&pnx8xxx_ports[i];
		pnx8xxx_ports[i].port.ops = &pnx8xxx_pops;
	}
}

#ifdef CONFIG_SERIAL_PNX8XXX_CONSOLE

static void pnx8xxx_console_putchar(struct uart_port *port, int ch)
{
	struct pnx8xxx_port *sport = (struct pnx8xxx_port *)port;
	int status;

	do {
		/* Wait for UART_TX register to empty */
		status = serial_in(sport, PNX8XXX_FIFO);
	} while (status & PNX8XXX_UART_FIFO_TXFIFO);
	serial_out(sport, PNX8XXX_FIFO, ch);
}

/*
 * Interrupts are disabled on entering
 */static void
pnx8xxx_console_write(struct console *co, const char *s, unsigned int count)
{
	struct pnx8xxx_port *sport = &pnx8xxx_ports[co->index];
	unsigned int old_ien, status;

	/*
	 *	First, save IEN and then disable interrupts
	 */
	old_ien = serial_in(sport, PNX8XXX_IEN);
	serial_out(sport, PNX8XXX_IEN, old_ien & ~(PNX8XXX_UART_INT_ALLTX |
					PNX8XXX_UART_INT_ALLRX));

	uart_console_write(&sport->port, s, count, pnx8xxx_console_putchar);

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore IEN
	 */
	do {
		/* Wait for UART_TX register to empty */
		status = serial_in(sport, PNX8XXX_FIFO);
	} while (status & PNX8XXX_UART_FIFO_TXFIFO);

	/* Clear TX and EMPTY interrupt */
	serial_out(sport, PNX8XXX_ICLR, PNX8XXX_UART_INT_TX |
			     PNX8XXX_UART_INT_EMPTY);

	serial_out(sport, PNX8XXX_IEN, old_ien);
}

static int __init
pnx8xxx_console_setup(struct console *co, char *options)
{
	struct pnx8xxx_port *sport;
	int baud = 38400;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index == -1 || co->index >= NR_PORTS)
		co->index = 0;
	sport = &pnx8xxx_ports[co->index];

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&sport->port, co, baud, parity, bits, flow);
}

static struct uart_driver pnx8xxx_reg;
static struct console pnx8xxx_console = {
	.name		= "ttyS",
	.write		= pnx8xxx_console_write,
	.device		= uart_console_device,
	.setup		= pnx8xxx_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &pnx8xxx_reg,
};

static int __init pnx8xxx_rs_console_init(void)
{
	pnx8xxx_init_ports();
	register_console(&pnx8xxx_console);
	return 0;
}
console_initcall(pnx8xxx_rs_console_init);

#define PNX8XXX_CONSOLE	&pnx8xxx_console
#else
#define PNX8XXX_CONSOLE	NULL
#endif

static struct uart_driver pnx8xxx_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "ttyS",
	.dev_name		= "ttyS",
	.major			= SERIAL_PNX8XXX_MAJOR,
	.minor			= MINOR_START,
	.nr			= NR_PORTS,
	.cons			= PNX8XXX_CONSOLE,
};

static int pnx8xxx_serial_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct pnx8xxx_port *sport = platform_get_drvdata(pdev);

	return uart_suspend_port(&pnx8xxx_reg, &sport->port);
}

static int pnx8xxx_serial_resume(struct platform_device *pdev)
{
	struct pnx8xxx_port *sport = platform_get_drvdata(pdev);

	return uart_resume_port(&pnx8xxx_reg, &sport->port);
}

static int pnx8xxx_serial_probe(struct platform_device *pdev)
{
	struct resource *res = pdev->resource;
	int i;

	for (i = 0; i < pdev->num_resources; i++, res++) {
		if (!(res->flags & IORESOURCE_MEM))
			continue;

		for (i = 0; i < NR_PORTS; i++) {
			if (pnx8xxx_ports[i].port.mapbase != res->start)
				continue;

			pnx8xxx_ports[i].port.dev = &pdev->dev;
			uart_add_one_port(&pnx8xxx_reg, &pnx8xxx_ports[i].port);
			platform_set_drvdata(pdev, &pnx8xxx_ports[i]);
			break;
		}
	}

	return 0;
}

static int pnx8xxx_serial_remove(struct platform_device *pdev)
{
	struct pnx8xxx_port *sport = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (sport)
		uart_remove_one_port(&pnx8xxx_reg, &sport->port);

	return 0;
}

static struct platform_driver pnx8xxx_serial_driver = {
	.driver		= {
		.name	= "pnx8xxx-uart",
		.owner	= THIS_MODULE,
	},
	.probe		= pnx8xxx_serial_probe,
	.remove		= pnx8xxx_serial_remove,
	.suspend	= pnx8xxx_serial_suspend,
	.resume		= pnx8xxx_serial_resume,
};

static int __init pnx8xxx_serial_init(void)
{
	int ret;

	printk(KERN_INFO "Serial: PNX8XXX driver $Revision: 1.2 $\n");

	pnx8xxx_init_ports();

	ret = uart_register_driver(&pnx8xxx_reg);
	if (ret == 0) {
		ret = platform_driver_register(&pnx8xxx_serial_driver);
		if (ret)
			uart_unregister_driver(&pnx8xxx_reg);
	}
	return ret;
}

static void __exit pnx8xxx_serial_exit(void)
{
	platform_driver_unregister(&pnx8xxx_serial_driver);
	uart_unregister_driver(&pnx8xxx_reg);
}

module_init(pnx8xxx_serial_init);
module_exit(pnx8xxx_serial_exit);

MODULE_AUTHOR("Embedded Alley Solutions, Inc.");
MODULE_DESCRIPTION("PNX8XXX SoCs serial port driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(SERIAL_PNX8XXX_MAJOR);
