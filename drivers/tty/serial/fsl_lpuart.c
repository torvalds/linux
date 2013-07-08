/*
 *  Freescale lpuart serial port driver
 *
 *  Copyright 2012-2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#if defined(CONFIG_SERIAL_FSL_LPUART_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/console.h>
#include <linux/serial_core.h>
#include <linux/tty_flip.h>

/* All registers are 8-bit width */
#define UARTBDH			0x00
#define UARTBDL			0x01
#define UARTCR1			0x02
#define UARTCR2			0x03
#define UARTSR1			0x04
#define UARTCR3			0x06
#define UARTDR			0x07
#define UARTCR4			0x0a
#define UARTCR5			0x0b
#define UARTMODEM		0x0d
#define UARTPFIFO		0x10
#define UARTCFIFO		0x11
#define UARTSFIFO		0x12
#define UARTTWFIFO		0x13
#define UARTTCFIFO		0x14
#define UARTRWFIFO		0x15

#define UARTBDH_LBKDIE		0x80
#define UARTBDH_RXEDGIE		0x40
#define UARTBDH_SBR_MASK	0x1f

#define UARTCR1_LOOPS		0x80
#define UARTCR1_RSRC		0x20
#define UARTCR1_M		0x10
#define UARTCR1_WAKE		0x08
#define UARTCR1_ILT		0x04
#define UARTCR1_PE		0x02
#define UARTCR1_PT		0x01

#define UARTCR2_TIE		0x80
#define UARTCR2_TCIE		0x40
#define UARTCR2_RIE		0x20
#define UARTCR2_ILIE		0x10
#define UARTCR2_TE		0x08
#define UARTCR2_RE		0x04
#define UARTCR2_RWU		0x02
#define UARTCR2_SBK		0x01

#define UARTSR1_TDRE		0x80
#define UARTSR1_TC		0x40
#define UARTSR1_RDRF		0x20
#define UARTSR1_IDLE		0x10
#define UARTSR1_OR		0x08
#define UARTSR1_NF		0x04
#define UARTSR1_FE		0x02
#define UARTSR1_PE		0x01

#define UARTCR3_R8		0x80
#define UARTCR3_T8		0x40
#define UARTCR3_TXDIR		0x20
#define UARTCR3_TXINV		0x10
#define UARTCR3_ORIE		0x08
#define UARTCR3_NEIE		0x04
#define UARTCR3_FEIE		0x02
#define UARTCR3_PEIE		0x01

#define UARTCR4_MAEN1		0x80
#define UARTCR4_MAEN2		0x40
#define UARTCR4_M10		0x20
#define UARTCR4_BRFA_MASK	0x1f
#define UARTCR4_BRFA_OFF	0

#define UARTCR5_TDMAS		0x80
#define UARTCR5_RDMAS		0x20

#define UARTMODEM_RXRTSE	0x08
#define UARTMODEM_TXRTSPOL	0x04
#define UARTMODEM_TXRTSE	0x02
#define UARTMODEM_TXCTSE	0x01

#define UARTPFIFO_TXFE		0x80
#define UARTPFIFO_FIFOSIZE_MASK	0x7
#define UARTPFIFO_TXSIZE_OFF	4
#define UARTPFIFO_RXFE		0x08
#define UARTPFIFO_RXSIZE_OFF	0

#define UARTCFIFO_TXFLUSH	0x80
#define UARTCFIFO_RXFLUSH	0x40
#define UARTCFIFO_RXOFE		0x04
#define UARTCFIFO_TXOFE		0x02
#define UARTCFIFO_RXUFE		0x01

#define UARTSFIFO_TXEMPT	0x80
#define UARTSFIFO_RXEMPT	0x40
#define UARTSFIFO_RXOF		0x04
#define UARTSFIFO_TXOF		0x02
#define UARTSFIFO_RXUF		0x01

#define DRIVER_NAME	"fsl-lpuart"
#define DEV_NAME	"ttyLP"
#define UART_NR		6

struct lpuart_port {
	struct uart_port	port;
	struct clk		*clk;
	unsigned int		txfifo_size;
	unsigned int		rxfifo_size;
};

static struct of_device_id lpuart_dt_ids[] = {
	{
		.compatible = "fsl,vf610-lpuart",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lpuart_dt_ids);

static void lpuart_stop_tx(struct uart_port *port)
{
	unsigned char temp;

	temp = readb(port->membase + UARTCR2);
	temp &= ~(UARTCR2_TIE | UARTCR2_TCIE);
	writeb(temp, port->membase + UARTCR2);
}

static void lpuart_stop_rx(struct uart_port *port)
{
	unsigned char temp;

	temp = readb(port->membase + UARTCR2);
	writeb(temp & ~UARTCR2_RE, port->membase + UARTCR2);
}

static void lpuart_enable_ms(struct uart_port *port)
{
}

static inline void lpuart_transmit_buffer(struct lpuart_port *sport)
{
	struct circ_buf *xmit = &sport->port.state->xmit;

	while (!uart_circ_empty(xmit) &&
		(readb(sport->port.membase + UARTTCFIFO) < sport->txfifo_size)) {
		writeb(xmit->buf[xmit->tail], sport->port.membase + UARTDR);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		sport->port.icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&sport->port);

	if (uart_circ_empty(xmit))
		lpuart_stop_tx(&sport->port);
}

static void lpuart_start_tx(struct uart_port *port)
{
	struct lpuart_port *sport = container_of(port, struct lpuart_port, port);
	unsigned char temp;

	temp = readb(port->membase + UARTCR2);
	writeb(temp | UARTCR2_TIE, port->membase + UARTCR2);

	if (readb(port->membase + UARTSR1) & UARTSR1_TDRE)
		lpuart_transmit_buffer(sport);
}

static irqreturn_t lpuart_txint(int irq, void *dev_id)
{
	struct lpuart_port *sport = dev_id;
	struct circ_buf *xmit = &sport->port.state->xmit;
	unsigned long flags;

	spin_lock_irqsave(&sport->port.lock, flags);
	if (sport->port.x_char) {
		writeb(sport->port.x_char, sport->port.membase + UARTDR);
		goto out;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(&sport->port)) {
		lpuart_stop_tx(&sport->port);
		goto out;
	}

	lpuart_transmit_buffer(sport);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&sport->port);

out:
	spin_unlock_irqrestore(&sport->port.lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t lpuart_rxint(int irq, void *dev_id)
{
	struct lpuart_port *sport = dev_id;
	unsigned int flg, ignored = 0;
	struct tty_port *port = &sport->port.state->port;
	unsigned long flags;
	unsigned char rx, sr;

	spin_lock_irqsave(&sport->port.lock, flags);

	while (!(readb(sport->port.membase + UARTSFIFO) & UARTSFIFO_RXEMPT)) {
		flg = TTY_NORMAL;
		sport->port.icount.rx++;
		/*
		 * to clear the FE, OR, NF, FE, PE flags,
		 * read SR1 then read DR
		 */
		sr = readb(sport->port.membase + UARTSR1);
		rx = readb(sport->port.membase + UARTDR);

		if (uart_handle_sysrq_char(&sport->port, (unsigned char)rx))
			continue;

		if (sr & (UARTSR1_PE | UARTSR1_OR | UARTSR1_FE)) {
			if (sr & UARTSR1_PE)
				sport->port.icount.parity++;
			else if (sr & UARTSR1_FE)
				sport->port.icount.frame++;

			if (sr & UARTSR1_OR)
				sport->port.icount.overrun++;

			if (sr & sport->port.ignore_status_mask) {
				if (++ignored > 100)
					goto out;
				continue;
			}

			sr &= sport->port.read_status_mask;

			if (sr & UARTSR1_PE)
				flg = TTY_PARITY;
			else if (sr & UARTSR1_FE)
				flg = TTY_FRAME;

			if (sr & UARTSR1_OR)
				flg = TTY_OVERRUN;

#ifdef SUPPORT_SYSRQ
			sport->port.sysrq = 0;
#endif
		}

		tty_insert_flip_char(port, rx, flg);
	}

out:
	spin_unlock_irqrestore(&sport->port.lock, flags);

	tty_flip_buffer_push(port);
	return IRQ_HANDLED;
}

static irqreturn_t lpuart_int(int irq, void *dev_id)
{
	struct lpuart_port *sport = dev_id;
	unsigned char sts;

	sts = readb(sport->port.membase + UARTSR1);

	if (sts & UARTSR1_RDRF)
		lpuart_rxint(irq, dev_id);

	if (sts & UARTSR1_TDRE &&
		!(readb(sport->port.membase + UARTCR5) & UARTCR5_TDMAS))
		lpuart_txint(irq, dev_id);

	return IRQ_HANDLED;
}

/* return TIOCSER_TEMT when transmitter is not busy */
static unsigned int lpuart_tx_empty(struct uart_port *port)
{
	return (readb(port->membase + UARTSR1) & UARTSR1_TC) ?
		TIOCSER_TEMT : 0;
}

static unsigned int lpuart_get_mctrl(struct uart_port *port)
{
	unsigned int temp = 0;
	unsigned char reg;

	reg = readb(port->membase + UARTMODEM);
	if (reg & UARTMODEM_TXCTSE)
		temp |= TIOCM_CTS;

	if (reg & UARTMODEM_RXRTSE)
		temp |= TIOCM_RTS;

	return temp;
}

static void lpuart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	unsigned char temp;

	temp = readb(port->membase + UARTMODEM) &
			~(UARTMODEM_RXRTSE | UARTMODEM_TXCTSE);

	if (mctrl & TIOCM_RTS)
		temp |= UARTMODEM_RXRTSE;

	if (mctrl & TIOCM_CTS)
		temp |= UARTMODEM_TXCTSE;

	writeb(temp, port->membase + UARTMODEM);
}

static void lpuart_break_ctl(struct uart_port *port, int break_state)
{
	unsigned char temp;

	temp = readb(port->membase + UARTCR2) & ~UARTCR2_SBK;

	if (break_state != 0)
		temp |= UARTCR2_SBK;

	writeb(temp, port->membase + UARTCR2);
}

static void lpuart_setup_watermark(struct lpuart_port *sport)
{
	unsigned char val, cr2;
	unsigned char cr2_saved;

	cr2 = readb(sport->port.membase + UARTCR2);
	cr2_saved = cr2;
	cr2 &= ~(UARTCR2_TIE | UARTCR2_TCIE | UARTCR2_TE |
			UARTCR2_RIE | UARTCR2_RE);
	writeb(cr2, sport->port.membase + UARTCR2);

	/* determine FIFO size and enable FIFO mode */
	val = readb(sport->port.membase + UARTPFIFO);

	sport->txfifo_size = 0x1 << (((val >> UARTPFIFO_TXSIZE_OFF) &
		UARTPFIFO_FIFOSIZE_MASK) + 1);

	sport->rxfifo_size = 0x1 << (((val >> UARTPFIFO_RXSIZE_OFF) &
		UARTPFIFO_FIFOSIZE_MASK) + 1);

	writeb(val | UARTPFIFO_TXFE | UARTPFIFO_RXFE,
			sport->port.membase + UARTPFIFO);

	/* flush Tx and Rx FIFO */
	writeb(UARTCFIFO_TXFLUSH | UARTCFIFO_RXFLUSH,
			sport->port.membase + UARTCFIFO);

	writeb(2, sport->port.membase + UARTTWFIFO);
	writeb(1, sport->port.membase + UARTRWFIFO);

	/* Restore cr2 */
	writeb(cr2_saved, sport->port.membase + UARTCR2);
}

static int lpuart_startup(struct uart_port *port)
{
	struct lpuart_port *sport = container_of(port, struct lpuart_port, port);
	int ret;
	unsigned long flags;
	unsigned char temp;

	ret = devm_request_irq(port->dev, port->irq, lpuart_int, 0,
				DRIVER_NAME, sport);
	if (ret)
		return ret;

	spin_lock_irqsave(&sport->port.lock, flags);

	lpuart_setup_watermark(sport);

	temp = readb(sport->port.membase + UARTCR2);
	temp |= (UARTCR2_RIE | UARTCR2_TIE | UARTCR2_RE | UARTCR2_TE);
	writeb(temp, sport->port.membase + UARTCR2);

	spin_unlock_irqrestore(&sport->port.lock, flags);
	return 0;
}

static void lpuart_shutdown(struct uart_port *port)
{
	struct lpuart_port *sport = container_of(port, struct lpuart_port, port);
	unsigned char temp;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	/* disable Rx/Tx and interrupts */
	temp = readb(port->membase + UARTCR2);
	temp &= ~(UARTCR2_TE | UARTCR2_RE |
			UARTCR2_TIE | UARTCR2_TCIE | UARTCR2_RIE);
	writeb(temp, port->membase + UARTCR2);

	spin_unlock_irqrestore(&port->lock, flags);

	devm_free_irq(port->dev, port->irq, sport);
}

static void
lpuart_set_termios(struct uart_port *port, struct ktermios *termios,
		   struct ktermios *old)
{
	struct lpuart_port *sport = container_of(port, struct lpuart_port, port);
	unsigned long flags;
	unsigned char cr1, old_cr1, old_cr2, cr4, bdh, modem;
	unsigned int  baud;
	unsigned int old_csize = old ? old->c_cflag & CSIZE : CS8;
	unsigned int sbr, brfa;

	cr1 = old_cr1 = readb(sport->port.membase + UARTCR1);
	old_cr2 = readb(sport->port.membase + UARTCR2);
	cr4 = readb(sport->port.membase + UARTCR4);
	bdh = readb(sport->port.membase + UARTBDH);
	modem = readb(sport->port.membase + UARTMODEM);
	/*
	 * only support CS8 and CS7, and for CS7 must enable PE.
	 * supported mode:
	 *  - (7,e/o,1)
	 *  - (8,n,1)
	 *  - (8,m/s,1)
	 *  - (8,e/o,1)
	 */
	while ((termios->c_cflag & CSIZE) != CS8 &&
		(termios->c_cflag & CSIZE) != CS7) {
		termios->c_cflag &= ~CSIZE;
		termios->c_cflag |= old_csize;
		old_csize = CS8;
	}

	if ((termios->c_cflag & CSIZE) == CS8 ||
		(termios->c_cflag & CSIZE) == CS7)
		cr1 = old_cr1 & ~UARTCR1_M;

	if (termios->c_cflag & CMSPAR) {
		if ((termios->c_cflag & CSIZE) != CS8) {
			termios->c_cflag &= ~CSIZE;
			termios->c_cflag |= CS8;
		}
		cr1 |= UARTCR1_M;
	}

	if (termios->c_cflag & CRTSCTS) {
		modem |= (UARTMODEM_RXRTSE | UARTMODEM_TXCTSE);
	} else {
		termios->c_cflag &= ~CRTSCTS;
		modem &= ~(UARTMODEM_RXRTSE | UARTMODEM_TXCTSE);
	}

	if (termios->c_cflag & CSTOPB)
		termios->c_cflag &= ~CSTOPB;

	/* parity must be enabled when CS7 to match 8-bits format */
	if ((termios->c_cflag & CSIZE) == CS7)
		termios->c_cflag |= PARENB;

	if ((termios->c_cflag & PARENB)) {
		if (termios->c_cflag & CMSPAR) {
			cr1 &= ~UARTCR1_PE;
			cr1 |= UARTCR1_M;
		} else {
			cr1 |= UARTCR1_PE;
			if ((termios->c_cflag & CSIZE) == CS8)
				cr1 |= UARTCR1_M;
			if (termios->c_cflag & PARODD)
				cr1 |= UARTCR1_PT;
			else
				cr1 &= ~UARTCR1_PT;
		}
	}

	/* ask the core to calculate the divisor */
	baud = uart_get_baud_rate(port, termios, old, 50, port->uartclk / 16);

	spin_lock_irqsave(&sport->port.lock, flags);

	sport->port.read_status_mask = 0;
	if (termios->c_iflag & INPCK)
		sport->port.read_status_mask |=	(UARTSR1_FE | UARTSR1_PE);
	if (termios->c_iflag & (BRKINT | PARMRK))
		sport->port.read_status_mask |= UARTSR1_FE;

	/* characters to ignore */
	sport->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		sport->port.ignore_status_mask |= UARTSR1_PE;
	if (termios->c_iflag & IGNBRK) {
		sport->port.ignore_status_mask |= UARTSR1_FE;
		/*
		 * if we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			sport->port.ignore_status_mask |= UARTSR1_OR;
	}

	/* update the per-port timeout */
	uart_update_timeout(port, termios->c_cflag, baud);

	/* wait transmit engin complete */
	while (!(readb(sport->port.membase + UARTSR1) & UARTSR1_TC))
		barrier();

	/* disable transmit and receive */
	writeb(old_cr2 & ~(UARTCR2_TE | UARTCR2_RE),
			sport->port.membase + UARTCR2);

	sbr = sport->port.uartclk / (16 * baud);
	brfa = ((sport->port.uartclk - (16 * sbr * baud)) * 2) / baud;
	bdh &= ~UARTBDH_SBR_MASK;
	bdh |= (sbr >> 8) & 0x1F;
	cr4 &= ~UARTCR4_BRFA_MASK;
	brfa &= UARTCR4_BRFA_MASK;
	writeb(cr4 | brfa, sport->port.membase + UARTCR4);
	writeb(bdh, sport->port.membase + UARTBDH);
	writeb(sbr & 0xFF, sport->port.membase + UARTBDL);
	writeb(cr1, sport->port.membase + UARTCR1);
	writeb(modem, sport->port.membase + UARTMODEM);

	/* restore control register */
	writeb(old_cr2, sport->port.membase + UARTCR2);

	spin_unlock_irqrestore(&sport->port.lock, flags);
}

static const char *lpuart_type(struct uart_port *port)
{
	return "FSL_LPUART";
}

static void lpuart_release_port(struct uart_port *port)
{
	/* nothing to do */
}

static int lpuart_request_port(struct uart_port *port)
{
	return  0;
}

/* configure/autoconfigure the port */
static void lpuart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_LPUART;
}

static int lpuart_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;

	if (ser->type != PORT_UNKNOWN && ser->type != PORT_LPUART)
		ret = -EINVAL;
	if (port->irq != ser->irq)
		ret = -EINVAL;
	if (ser->io_type != UPIO_MEM)
		ret = -EINVAL;
	if (port->uartclk / 16 != ser->baud_base)
		ret = -EINVAL;
	if (port->iobase != ser->port)
		ret = -EINVAL;
	if (ser->hub6 != 0)
		ret = -EINVAL;
	return ret;
}

static struct uart_ops lpuart_pops = {
	.tx_empty	= lpuart_tx_empty,
	.set_mctrl	= lpuart_set_mctrl,
	.get_mctrl	= lpuart_get_mctrl,
	.stop_tx	= lpuart_stop_tx,
	.start_tx	= lpuart_start_tx,
	.stop_rx	= lpuart_stop_rx,
	.enable_ms	= lpuart_enable_ms,
	.break_ctl	= lpuart_break_ctl,
	.startup	= lpuart_startup,
	.shutdown	= lpuart_shutdown,
	.set_termios	= lpuart_set_termios,
	.type		= lpuart_type,
	.request_port	= lpuart_request_port,
	.release_port	= lpuart_release_port,
	.config_port	= lpuart_config_port,
	.verify_port	= lpuart_verify_port,
};

static struct lpuart_port *lpuart_ports[UART_NR];

#ifdef CONFIG_SERIAL_FSL_LPUART_CONSOLE
static void lpuart_console_putchar(struct uart_port *port, int ch)
{
	while (!(readb(port->membase + UARTSR1) & UARTSR1_TDRE))
		barrier();

	writeb(ch, port->membase + UARTDR);
}

static void
lpuart_console_write(struct console *co, const char *s, unsigned int count)
{
	struct lpuart_port *sport = lpuart_ports[co->index];
	unsigned char  old_cr2, cr2;

	/* first save CR2 and then disable interrupts */
	cr2 = old_cr2 = readb(sport->port.membase + UARTCR2);
	cr2 |= (UARTCR2_TE |  UARTCR2_RE);
	cr2 &= ~(UARTCR2_TIE | UARTCR2_TCIE | UARTCR2_RIE);
	writeb(cr2, sport->port.membase + UARTCR2);

	uart_console_write(&sport->port, s, count, lpuart_console_putchar);

	/* wait for transmitter finish complete and restore CR2 */
	while (!(readb(sport->port.membase + UARTSR1) & UARTSR1_TC))
		barrier();

	writeb(old_cr2, sport->port.membase + UARTCR2);
}

/*
 * if the port was already initialised (eg, by a boot loader),
 * try to determine the current setup.
 */
static void __init
lpuart_console_get_options(struct lpuart_port *sport, int *baud,
			   int *parity, int *bits)
{
	unsigned char cr, bdh, bdl, brfa;
	unsigned int sbr, uartclk, baud_raw;

	cr = readb(sport->port.membase + UARTCR2);
	cr &= UARTCR2_TE | UARTCR2_RE;
	if (!cr)
		return;

	/* ok, the port was enabled */

	cr = readb(sport->port.membase + UARTCR1);

	*parity = 'n';
	if (cr & UARTCR1_PE) {
		if (cr & UARTCR1_PT)
			*parity = 'o';
		else
			*parity = 'e';
	}

	if (cr & UARTCR1_M)
		*bits = 9;
	else
		*bits = 8;

	bdh = readb(sport->port.membase + UARTBDH);
	bdh &= UARTBDH_SBR_MASK;
	bdl = readb(sport->port.membase + UARTBDL);
	sbr = bdh;
	sbr <<= 8;
	sbr |= bdl;
	brfa = readb(sport->port.membase + UARTCR4);
	brfa &= UARTCR4_BRFA_MASK;

	uartclk = clk_get_rate(sport->clk);
	/*
	 * baud = mod_clk/(16*(sbr[13]+(brfa)/32)
	 */
	baud_raw = uartclk / (16 * (sbr + brfa / 32));

	if (*baud != baud_raw)
		printk(KERN_INFO "Serial: Console lpuart rounded baud rate"
				"from %d to %d\n", baud_raw, *baud);
}

static int __init lpuart_console_setup(struct console *co, char *options)
{
	struct lpuart_port *sport;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index == -1 || co->index >= ARRAY_SIZE(lpuart_ports))
		co->index = 0;

	sport = lpuart_ports[co->index];
	if (sport == NULL)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		lpuart_console_get_options(sport, &baud, &parity, &bits);

	lpuart_setup_watermark(sport);

	return uart_set_options(&sport->port, co, baud, parity, bits, flow);
}

static struct uart_driver lpuart_reg;
static struct console lpuart_console = {
	.name		= DEV_NAME,
	.write		= lpuart_console_write,
	.device		= uart_console_device,
	.setup		= lpuart_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &lpuart_reg,
};

#define LPUART_CONSOLE	(&lpuart_console)
#else
#define LPUART_CONSOLE	NULL
#endif

static struct uart_driver lpuart_reg = {
	.owner		= THIS_MODULE,
	.driver_name	= DRIVER_NAME,
	.dev_name	= DEV_NAME,
	.nr		= ARRAY_SIZE(lpuart_ports),
	.cons		= LPUART_CONSOLE,
};

static int lpuart_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct lpuart_port *sport;
	struct resource *res;
	int ret;

	sport = devm_kzalloc(&pdev->dev, sizeof(*sport), GFP_KERNEL);
	if (!sport)
		return -ENOMEM;

	pdev->dev.coherent_dma_mask = 0;

	ret = of_alias_get_id(np, "serial");
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get alias id, errno %d\n", ret);
		return ret;
	}
	sport->port.line = ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	sport->port.mapbase = res->start;
	sport->port.membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(sport->port.membase))
		return PTR_ERR(sport->port.membase);

	sport->port.dev = &pdev->dev;
	sport->port.type = PORT_LPUART;
	sport->port.iotype = UPIO_MEM;
	sport->port.irq = platform_get_irq(pdev, 0);
	sport->port.ops = &lpuart_pops;
	sport->port.flags = UPF_BOOT_AUTOCONF;

	sport->clk = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(sport->clk)) {
		ret = PTR_ERR(sport->clk);
		dev_err(&pdev->dev, "failed to get uart clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(sport->clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable uart clk: %d\n", ret);
		return ret;
	}

	sport->port.uartclk = clk_get_rate(sport->clk);

	lpuart_ports[sport->port.line] = sport;

	platform_set_drvdata(pdev, &sport->port);

	ret = uart_add_one_port(&lpuart_reg, &sport->port);
	if (ret) {
		clk_disable_unprepare(sport->clk);
		return ret;
	}

	return 0;
}

static int lpuart_remove(struct platform_device *pdev)
{
	struct lpuart_port *sport = platform_get_drvdata(pdev);

	uart_remove_one_port(&lpuart_reg, &sport->port);

	clk_disable_unprepare(sport->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int lpuart_suspend(struct device *dev)
{
	struct lpuart_port *sport = dev_get_drvdata(dev);

	uart_suspend_port(&lpuart_reg, &sport->port);

	return 0;
}

static int lpuart_resume(struct device *dev)
{
	struct lpuart_port *sport = dev_get_drvdata(dev);

	uart_resume_port(&lpuart_reg, &sport->port);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(lpuart_pm_ops, lpuart_suspend, lpuart_resume);

static struct platform_driver lpuart_driver = {
	.probe		= lpuart_probe,
	.remove		= lpuart_remove,
	.driver		= {
		.name	= "fsl-lpuart",
		.owner	= THIS_MODULE,
		.of_match_table = lpuart_dt_ids,
		.pm	= &lpuart_pm_ops,
	},
};

static int __init lpuart_serial_init(void)
{
	int ret;

	pr_info("serial: Freescale lpuart driver\n");

	ret = uart_register_driver(&lpuart_reg);
	if (ret)
		return ret;

	ret = platform_driver_register(&lpuart_driver);
	if (ret)
		uart_unregister_driver(&lpuart_reg);

	return 0;
}

static void __exit lpuart_serial_exit(void)
{
	platform_driver_unregister(&lpuart_driver);
	uart_unregister_driver(&lpuart_reg);
}

module_init(lpuart_serial_init);
module_exit(lpuart_serial_exit);

MODULE_DESCRIPTION("Freescale lpuart serial port driver");
MODULE_LICENSE("GPL v2");
