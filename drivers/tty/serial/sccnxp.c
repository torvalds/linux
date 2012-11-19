/*
 *  NXP (Philips) SCC+++(SCN+++) serial driver
 *
 *  Copyright (C) 2012 Alexander Shiyan <shc_work@mail.ru>
 *
 *  Based on sc26xx.c, by Thomas Bogendörfer (tsbogend@alpha.franken.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#if defined(CONFIG_SERIAL_SCCNXP_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/device.h>
#include <linux/console.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/io.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/platform_device.h>
#include <linux/platform_data/sccnxp.h>

#define SCCNXP_NAME			"uart-sccnxp"
#define SCCNXP_MAJOR			204
#define SCCNXP_MINOR			205

#define SCCNXP_MR_REG			(0x00)
#	define MR0_BAUD_NORMAL		(0 << 0)
#	define MR0_BAUD_EXT1		(1 << 0)
#	define MR0_BAUD_EXT2		(5 << 0)
#	define MR0_FIFO			(1 << 3)
#	define MR0_TXLVL		(1 << 4)
#	define MR1_BITS_5		(0 << 0)
#	define MR1_BITS_6		(1 << 0)
#	define MR1_BITS_7		(2 << 0)
#	define MR1_BITS_8		(3 << 0)
#	define MR1_PAR_EVN		(0 << 2)
#	define MR1_PAR_ODD		(1 << 2)
#	define MR1_PAR_NO		(4 << 2)
#	define MR2_STOP1		(7 << 0)
#	define MR2_STOP2		(0xf << 0)
#define SCCNXP_SR_REG			(0x01)
#define SCCNXP_CSR_REG			SCCNXP_SR_REG
#	define SR_RXRDY			(1 << 0)
#	define SR_FULL			(1 << 1)
#	define SR_TXRDY			(1 << 2)
#	define SR_TXEMT			(1 << 3)
#	define SR_OVR			(1 << 4)
#	define SR_PE			(1 << 5)
#	define SR_FE			(1 << 6)
#	define SR_BRK			(1 << 7)
#define SCCNXP_CR_REG			(0x02)
#	define CR_RX_ENABLE		(1 << 0)
#	define CR_RX_DISABLE		(1 << 1)
#	define CR_TX_ENABLE		(1 << 2)
#	define CR_TX_DISABLE		(1 << 3)
#	define CR_CMD_MRPTR1		(0x01 << 4)
#	define CR_CMD_RX_RESET		(0x02 << 4)
#	define CR_CMD_TX_RESET		(0x03 << 4)
#	define CR_CMD_STATUS_RESET	(0x04 << 4)
#	define CR_CMD_BREAK_RESET	(0x05 << 4)
#	define CR_CMD_START_BREAK	(0x06 << 4)
#	define CR_CMD_STOP_BREAK	(0x07 << 4)
#	define CR_CMD_MRPTR0		(0x0b << 4)
#define SCCNXP_RHR_REG			(0x03)
#define SCCNXP_THR_REG			SCCNXP_RHR_REG
#define SCCNXP_IPCR_REG			(0x04)
#define SCCNXP_ACR_REG			SCCNXP_IPCR_REG
#	define ACR_BAUD0		(0 << 7)
#	define ACR_BAUD1		(1 << 7)
#	define ACR_TIMER_MODE		(6 << 4)
#define SCCNXP_ISR_REG			(0x05)
#define SCCNXP_IMR_REG			SCCNXP_ISR_REG
#	define IMR_TXRDY		(1 << 0)
#	define IMR_RXRDY		(1 << 1)
#	define ISR_TXRDY(x)		(1 << ((x * 4) + 0))
#	define ISR_RXRDY(x)		(1 << ((x * 4) + 1))
#define SCCNXP_IPR_REG			(0x0d)
#define SCCNXP_OPCR_REG			SCCNXP_IPR_REG
#define SCCNXP_SOP_REG			(0x0e)
#define SCCNXP_ROP_REG			(0x0f)

/* Route helpers */
#define MCTRL_MASK(sig)			(0xf << (sig))
#define MCTRL_IBIT(cfg, sig)		((((cfg) >> (sig)) & 0xf) - LINE_IP0)
#define MCTRL_OBIT(cfg, sig)		((((cfg) >> (sig)) & 0xf) - LINE_OP0)

/* Supported chip types */
enum {
	SCCNXP_TYPE_SC2681	= 2681,
	SCCNXP_TYPE_SC2691	= 2691,
	SCCNXP_TYPE_SC2692	= 2692,
	SCCNXP_TYPE_SC2891	= 2891,
	SCCNXP_TYPE_SC2892	= 2892,
	SCCNXP_TYPE_SC28202	= 28202,
	SCCNXP_TYPE_SC68681	= 68681,
	SCCNXP_TYPE_SC68692	= 68692,
};

struct sccnxp_port {
	struct uart_driver	uart;
	struct uart_port	port[SCCNXP_MAX_UARTS];

	const char		*name;
	int			irq;

	u8			imr;
	u8			addr_mask;
	int			freq_std;

	int			flags;
#define SCCNXP_HAVE_IO		0x00000001
#define SCCNXP_HAVE_MR0		0x00000002

#ifdef CONFIG_SERIAL_SCCNXP_CONSOLE
	struct console		console;
#endif

	struct mutex		sccnxp_mutex;

	struct sccnxp_pdata	pdata;
};

static inline u8 sccnxp_raw_read(void __iomem *base, u8 reg, u8 shift)
{
	return readb(base + (reg << shift));
}

static inline void sccnxp_raw_write(void __iomem *base, u8 reg, u8 shift, u8 v)
{
	writeb(v, base + (reg << shift));
}

static inline u8 sccnxp_read(struct uart_port *port, u8 reg)
{
	struct sccnxp_port *s = dev_get_drvdata(port->dev);

	return sccnxp_raw_read(port->membase, reg & s->addr_mask,
			       port->regshift);
}

static inline void sccnxp_write(struct uart_port *port, u8 reg, u8 v)
{
	struct sccnxp_port *s = dev_get_drvdata(port->dev);

	sccnxp_raw_write(port->membase, reg & s->addr_mask, port->regshift, v);
}

static inline u8 sccnxp_port_read(struct uart_port *port, u8 reg)
{
	return sccnxp_read(port, (port->line << 3) + reg);
}

static inline void sccnxp_port_write(struct uart_port *port, u8 reg, u8 v)
{
	sccnxp_write(port, (port->line << 3) + reg, v);
}

static int sccnxp_update_best_err(int a, int b, int *besterr)
{
	int err = abs(a - b);

	if ((*besterr < 0) || (*besterr > err)) {
		*besterr = err;
		return 0;
	}

	return 1;
}

struct baud_table {
	u8	csr;
	u8	acr;
	u8	mr0;
	int	baud;
};

const struct baud_table baud_std[] = {
	{ 0,	ACR_BAUD0,	MR0_BAUD_NORMAL,	50, },
	{ 0,	ACR_BAUD1,	MR0_BAUD_NORMAL,	75, },
	{ 1,	ACR_BAUD0,	MR0_BAUD_NORMAL,	110, },
	{ 2,	ACR_BAUD0,	MR0_BAUD_NORMAL,	134, },
	{ 3,	ACR_BAUD1,	MR0_BAUD_NORMAL,	150, },
	{ 3,	ACR_BAUD0,	MR0_BAUD_NORMAL,	200, },
	{ 4,	ACR_BAUD0,	MR0_BAUD_NORMAL,	300, },
	{ 0,	ACR_BAUD1,	MR0_BAUD_EXT1,		450, },
	{ 1,	ACR_BAUD0,	MR0_BAUD_EXT2,		880, },
	{ 3,	ACR_BAUD1,	MR0_BAUD_EXT1,		900, },
	{ 5,	ACR_BAUD0,	MR0_BAUD_NORMAL,	600, },
	{ 7,	ACR_BAUD0,	MR0_BAUD_NORMAL,	1050, },
	{ 2,	ACR_BAUD0,	MR0_BAUD_EXT2,		1076, },
	{ 6,	ACR_BAUD0,	MR0_BAUD_NORMAL,	1200, },
	{ 10,	ACR_BAUD1,	MR0_BAUD_NORMAL,	1800, },
	{ 7,	ACR_BAUD1,	MR0_BAUD_NORMAL,	2000, },
	{ 8,	ACR_BAUD0,	MR0_BAUD_NORMAL,	2400, },
	{ 5,	ACR_BAUD1,	MR0_BAUD_EXT1,		3600, },
	{ 9,	ACR_BAUD0,	MR0_BAUD_NORMAL,	4800, },
	{ 10,	ACR_BAUD0,	MR0_BAUD_NORMAL,	7200, },
	{ 11,	ACR_BAUD0,	MR0_BAUD_NORMAL,	9600, },
	{ 8,	ACR_BAUD0,	MR0_BAUD_EXT1,		14400, },
	{ 12,	ACR_BAUD1,	MR0_BAUD_NORMAL,	19200, },
	{ 9,	ACR_BAUD0,	MR0_BAUD_EXT1,		28800, },
	{ 12,	ACR_BAUD0,	MR0_BAUD_NORMAL,	38400, },
	{ 11,	ACR_BAUD0,	MR0_BAUD_EXT1,		57600, },
	{ 12,	ACR_BAUD1,	MR0_BAUD_EXT1,		115200, },
	{ 12,	ACR_BAUD0,	MR0_BAUD_EXT1,		230400, },
	{ 0, 0, 0, 0 }
};

static int sccnxp_set_baud(struct uart_port *port, int baud)
{
	struct sccnxp_port *s = dev_get_drvdata(port->dev);
	int div_std, tmp_baud, bestbaud = baud, besterr = -1;
	u8 i, acr = 0, csr = 0, mr0 = 0;

	/* Find best baud from table */
	for (i = 0; baud_std[i].baud && besterr; i++) {
		if (baud_std[i].mr0 && !(s->flags & SCCNXP_HAVE_MR0))
			continue;
		div_std = DIV_ROUND_CLOSEST(s->freq_std, baud_std[i].baud);
		tmp_baud = DIV_ROUND_CLOSEST(port->uartclk, div_std);
		if (!sccnxp_update_best_err(baud, tmp_baud, &besterr)) {
			acr = baud_std[i].acr;
			csr = baud_std[i].csr;
			mr0 = baud_std[i].mr0;
			bestbaud = tmp_baud;
		}
	}

	if (s->flags & SCCNXP_HAVE_MR0) {
		/* Enable FIFO, set half level for TX */
		mr0 |= MR0_FIFO | MR0_TXLVL;
		/* Update MR0 */
		sccnxp_port_write(port, SCCNXP_CR_REG, CR_CMD_MRPTR0);
		sccnxp_port_write(port, SCCNXP_MR_REG, mr0);
	}

	sccnxp_port_write(port, SCCNXP_ACR_REG, acr | ACR_TIMER_MODE);
	sccnxp_port_write(port, SCCNXP_CSR_REG, (csr << 4) | csr);

	if (baud != bestbaud)
		dev_dbg(port->dev, "Baudrate desired: %i, calculated: %i\n",
			baud, bestbaud);

	return bestbaud;
}

static void sccnxp_enable_irq(struct uart_port *port, int mask)
{
	struct sccnxp_port *s = dev_get_drvdata(port->dev);

	s->imr |= mask << (port->line * 4);
	sccnxp_write(port, SCCNXP_IMR_REG, s->imr);
}

static void sccnxp_disable_irq(struct uart_port *port, int mask)
{
	struct sccnxp_port *s = dev_get_drvdata(port->dev);

	s->imr &= ~(mask << (port->line * 4));
	sccnxp_write(port, SCCNXP_IMR_REG, s->imr);
}

static void sccnxp_set_bit(struct uart_port *port, int sig, int state)
{
	u8 bitmask;
	struct sccnxp_port *s = dev_get_drvdata(port->dev);

	if (s->pdata.mctrl_cfg[port->line] & MCTRL_MASK(sig)) {
		bitmask = 1 << MCTRL_OBIT(s->pdata.mctrl_cfg[port->line], sig);
		if (state)
			sccnxp_write(port, SCCNXP_SOP_REG, bitmask);
		else
			sccnxp_write(port, SCCNXP_ROP_REG, bitmask);
	}
}

static void sccnxp_handle_rx(struct uart_port *port)
{
	u8 sr;
	unsigned int ch, flag;
	struct tty_struct *tty = tty_port_tty_get(&port->state->port);

	if (!tty)
		return;

	for (;;) {
		sr = sccnxp_port_read(port, SCCNXP_SR_REG);
		if (!(sr & SR_RXRDY))
			break;
		sr &= SR_PE | SR_FE | SR_OVR | SR_BRK;

		ch = sccnxp_port_read(port, SCCNXP_RHR_REG);

		port->icount.rx++;
		flag = TTY_NORMAL;

		if (unlikely(sr)) {
			if (sr & SR_BRK) {
				port->icount.brk++;
				if (uart_handle_break(port))
					continue;
			} else if (sr & SR_PE)
				port->icount.parity++;
			else if (sr & SR_FE)
				port->icount.frame++;
			else if (sr & SR_OVR)
				port->icount.overrun++;

			sr &= port->read_status_mask;
			if (sr & SR_BRK)
				flag = TTY_BREAK;
			else if (sr & SR_PE)
				flag = TTY_PARITY;
			else if (sr & SR_FE)
				flag = TTY_FRAME;
			else if (sr & SR_OVR)
				flag = TTY_OVERRUN;
		}

		if (uart_handle_sysrq_char(port, ch))
			continue;

		if (sr & port->ignore_status_mask)
			continue;

		uart_insert_char(port, sr, SR_OVR, ch, flag);
	}

	tty_flip_buffer_push(tty);

	tty_kref_put(tty);
}

static void sccnxp_handle_tx(struct uart_port *port)
{
	u8 sr;
	struct circ_buf *xmit = &port->state->xmit;
	struct sccnxp_port *s = dev_get_drvdata(port->dev);

	if (unlikely(port->x_char)) {
		sccnxp_port_write(port, SCCNXP_THR_REG, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		/* Disable TX if FIFO is empty */
		if (sccnxp_port_read(port, SCCNXP_SR_REG) & SR_TXEMT) {
			sccnxp_disable_irq(port, IMR_TXRDY);

			/* Set direction to input */
			if (s->flags & SCCNXP_HAVE_IO)
				sccnxp_set_bit(port, DIR_OP, 0);
		}
		return;
	}

	while (!uart_circ_empty(xmit)) {
		sr = sccnxp_port_read(port, SCCNXP_SR_REG);
		if (!(sr & SR_TXRDY))
			break;

		sccnxp_port_write(port, SCCNXP_THR_REG, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
}

static irqreturn_t sccnxp_ist(int irq, void *dev_id)
{
	int i;
	u8 isr;
	struct sccnxp_port *s = (struct sccnxp_port *)dev_id;

	mutex_lock(&s->sccnxp_mutex);

	for (;;) {
		isr = sccnxp_read(&s->port[0], SCCNXP_ISR_REG);
		isr &= s->imr;
		if (!isr)
			break;

		dev_dbg(s->port[0].dev, "IRQ status: 0x%02x\n", isr);

		for (i = 0; i < s->uart.nr; i++) {
			if (isr & ISR_RXRDY(i))
				sccnxp_handle_rx(&s->port[i]);
			if (isr & ISR_TXRDY(i))
				sccnxp_handle_tx(&s->port[i]);
		}
	}

	mutex_unlock(&s->sccnxp_mutex);

	return IRQ_HANDLED;
}

static void sccnxp_start_tx(struct uart_port *port)
{
	struct sccnxp_port *s = dev_get_drvdata(port->dev);

	mutex_lock(&s->sccnxp_mutex);

	/* Set direction to output */
	if (s->flags & SCCNXP_HAVE_IO)
		sccnxp_set_bit(port, DIR_OP, 1);

	sccnxp_enable_irq(port, IMR_TXRDY);

	mutex_unlock(&s->sccnxp_mutex);
}

static void sccnxp_stop_tx(struct uart_port *port)
{
	/* Do nothing */
}

static void sccnxp_stop_rx(struct uart_port *port)
{
	struct sccnxp_port *s = dev_get_drvdata(port->dev);

	mutex_lock(&s->sccnxp_mutex);
	sccnxp_port_write(port, SCCNXP_CR_REG, CR_RX_DISABLE);
	mutex_unlock(&s->sccnxp_mutex);
}

static unsigned int sccnxp_tx_empty(struct uart_port *port)
{
	u8 val;
	struct sccnxp_port *s = dev_get_drvdata(port->dev);

	mutex_lock(&s->sccnxp_mutex);
	val = sccnxp_port_read(port, SCCNXP_SR_REG);
	mutex_unlock(&s->sccnxp_mutex);

	return (val & SR_TXEMT) ? TIOCSER_TEMT : 0;
}

static void sccnxp_enable_ms(struct uart_port *port)
{
	/* Do nothing */
}

static void sccnxp_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct sccnxp_port *s = dev_get_drvdata(port->dev);

	if (!(s->flags & SCCNXP_HAVE_IO))
		return;

	mutex_lock(&s->sccnxp_mutex);

	sccnxp_set_bit(port, DTR_OP, mctrl & TIOCM_DTR);
	sccnxp_set_bit(port, RTS_OP, mctrl & TIOCM_RTS);

	mutex_unlock(&s->sccnxp_mutex);
}

static unsigned int sccnxp_get_mctrl(struct uart_port *port)
{
	u8 bitmask, ipr;
	struct sccnxp_port *s = dev_get_drvdata(port->dev);
	unsigned int mctrl = TIOCM_DSR | TIOCM_CTS | TIOCM_CAR;

	if (!(s->flags & SCCNXP_HAVE_IO))
		return mctrl;

	mutex_lock(&s->sccnxp_mutex);

	ipr = ~sccnxp_read(port, SCCNXP_IPCR_REG);

	if (s->pdata.mctrl_cfg[port->line] & MCTRL_MASK(DSR_IP)) {
		bitmask = 1 << MCTRL_IBIT(s->pdata.mctrl_cfg[port->line],
					  DSR_IP);
		mctrl &= ~TIOCM_DSR;
		mctrl |= (ipr & bitmask) ? TIOCM_DSR : 0;
	}
	if (s->pdata.mctrl_cfg[port->line] & MCTRL_MASK(CTS_IP)) {
		bitmask = 1 << MCTRL_IBIT(s->pdata.mctrl_cfg[port->line],
					  CTS_IP);
		mctrl &= ~TIOCM_CTS;
		mctrl |= (ipr & bitmask) ? TIOCM_CTS : 0;
	}
	if (s->pdata.mctrl_cfg[port->line] & MCTRL_MASK(DCD_IP)) {
		bitmask = 1 << MCTRL_IBIT(s->pdata.mctrl_cfg[port->line],
					  DCD_IP);
		mctrl &= ~TIOCM_CAR;
		mctrl |= (ipr & bitmask) ? TIOCM_CAR : 0;
	}
	if (s->pdata.mctrl_cfg[port->line] & MCTRL_MASK(RNG_IP)) {
		bitmask = 1 << MCTRL_IBIT(s->pdata.mctrl_cfg[port->line],
					  RNG_IP);
		mctrl &= ~TIOCM_RNG;
		mctrl |= (ipr & bitmask) ? TIOCM_RNG : 0;
	}

	mutex_unlock(&s->sccnxp_mutex);

	return mctrl;
}

static void sccnxp_break_ctl(struct uart_port *port, int break_state)
{
	struct sccnxp_port *s = dev_get_drvdata(port->dev);

	mutex_lock(&s->sccnxp_mutex);
	sccnxp_port_write(port, SCCNXP_CR_REG, break_state ?
			  CR_CMD_START_BREAK : CR_CMD_STOP_BREAK);
	mutex_unlock(&s->sccnxp_mutex);
}

static void sccnxp_set_termios(struct uart_port *port,
			       struct ktermios *termios, struct ktermios *old)
{
	struct sccnxp_port *s = dev_get_drvdata(port->dev);
	u8 mr1, mr2;
	int baud;

	mutex_lock(&s->sccnxp_mutex);

	/* Mask termios capabilities we don't support */
	termios->c_cflag &= ~CMSPAR;

	/* Disable RX & TX, reset break condition, status and FIFOs */
	sccnxp_port_write(port, SCCNXP_CR_REG, CR_CMD_RX_RESET |
					       CR_RX_DISABLE | CR_TX_DISABLE);
	sccnxp_port_write(port, SCCNXP_CR_REG, CR_CMD_TX_RESET);
	sccnxp_port_write(port, SCCNXP_CR_REG, CR_CMD_STATUS_RESET);
	sccnxp_port_write(port, SCCNXP_CR_REG, CR_CMD_BREAK_RESET);

	/* Word size */
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		mr1 = MR1_BITS_5;
		break;
	case CS6:
		mr1 = MR1_BITS_6;
		break;
	case CS7:
		mr1 = MR1_BITS_7;
		break;
	case CS8:
	default:
		mr1 = MR1_BITS_8;
		break;
	}

	/* Parity */
	if (termios->c_cflag & PARENB) {
		if (termios->c_cflag & PARODD)
			mr1 |= MR1_PAR_ODD;
	} else
		mr1 |= MR1_PAR_NO;

	/* Stop bits */
	mr2 = (termios->c_cflag & CSTOPB) ? MR2_STOP2 : MR2_STOP1;

	/* Update desired format */
	sccnxp_port_write(port, SCCNXP_CR_REG, CR_CMD_MRPTR1);
	sccnxp_port_write(port, SCCNXP_MR_REG, mr1);
	sccnxp_port_write(port, SCCNXP_MR_REG, mr2);

	/* Set read status mask */
	port->read_status_mask = SR_OVR;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= SR_PE | SR_FE;
	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= SR_BRK;

	/* Set status ignore mask */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNBRK)
		port->ignore_status_mask |= SR_BRK;
	if (!(termios->c_cflag & CREAD))
		port->ignore_status_mask |= SR_PE | SR_OVR | SR_FE | SR_BRK;

	/* Setup baudrate */
	baud = uart_get_baud_rate(port, termios, old, 50,
				  (s->flags & SCCNXP_HAVE_MR0) ?
				  230400 : 38400);
	baud = sccnxp_set_baud(port, baud);

	/* Update timeout according to new baud rate */
	uart_update_timeout(port, termios->c_cflag, baud);

	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);

	/* Enable RX & TX */
	sccnxp_port_write(port, SCCNXP_CR_REG, CR_RX_ENABLE | CR_TX_ENABLE);

	mutex_unlock(&s->sccnxp_mutex);
}

static int sccnxp_startup(struct uart_port *port)
{
	struct sccnxp_port *s = dev_get_drvdata(port->dev);

	mutex_lock(&s->sccnxp_mutex);

	if (s->flags & SCCNXP_HAVE_IO) {
		/* Outputs are controlled manually */
		sccnxp_write(port, SCCNXP_OPCR_REG, 0);
	}

	/* Reset break condition, status and FIFOs */
	sccnxp_port_write(port, SCCNXP_CR_REG, CR_CMD_RX_RESET);
	sccnxp_port_write(port, SCCNXP_CR_REG, CR_CMD_TX_RESET);
	sccnxp_port_write(port, SCCNXP_CR_REG, CR_CMD_STATUS_RESET);
	sccnxp_port_write(port, SCCNXP_CR_REG, CR_CMD_BREAK_RESET);

	/* Enable RX & TX */
	sccnxp_port_write(port, SCCNXP_CR_REG, CR_RX_ENABLE | CR_TX_ENABLE);

	/* Enable RX interrupt */
	sccnxp_enable_irq(port, IMR_RXRDY);

	mutex_unlock(&s->sccnxp_mutex);

	return 0;
}

static void sccnxp_shutdown(struct uart_port *port)
{
	struct sccnxp_port *s = dev_get_drvdata(port->dev);

	mutex_lock(&s->sccnxp_mutex);

	/* Disable interrupts */
	sccnxp_disable_irq(port, IMR_TXRDY | IMR_RXRDY);

	/* Disable TX & RX */
	sccnxp_port_write(port, SCCNXP_CR_REG, CR_RX_DISABLE | CR_TX_DISABLE);

	/* Leave direction to input */
	if (s->flags & SCCNXP_HAVE_IO)
		sccnxp_set_bit(port, DIR_OP, 0);

	mutex_unlock(&s->sccnxp_mutex);
}

static const char *sccnxp_type(struct uart_port *port)
{
	struct sccnxp_port *s = dev_get_drvdata(port->dev);

	return (port->type == PORT_SC26XX) ? s->name : NULL;
}

static void sccnxp_release_port(struct uart_port *port)
{
	/* Do nothing */
}

static int sccnxp_request_port(struct uart_port *port)
{
	/* Do nothing */
	return 0;
}

static void sccnxp_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_SC26XX;
}

static int sccnxp_verify_port(struct uart_port *port, struct serial_struct *s)
{
	if ((s->type == PORT_UNKNOWN) || (s->type == PORT_SC26XX))
		return 0;
	if (s->irq == port->irq)
		return 0;

	return -EINVAL;
}

static const struct uart_ops sccnxp_ops = {
	.tx_empty	= sccnxp_tx_empty,
	.set_mctrl	= sccnxp_set_mctrl,
	.get_mctrl	= sccnxp_get_mctrl,
	.stop_tx	= sccnxp_stop_tx,
	.start_tx	= sccnxp_start_tx,
	.stop_rx	= sccnxp_stop_rx,
	.enable_ms	= sccnxp_enable_ms,
	.break_ctl	= sccnxp_break_ctl,
	.startup	= sccnxp_startup,
	.shutdown	= sccnxp_shutdown,
	.set_termios	= sccnxp_set_termios,
	.type		= sccnxp_type,
	.release_port	= sccnxp_release_port,
	.request_port	= sccnxp_request_port,
	.config_port	= sccnxp_config_port,
	.verify_port	= sccnxp_verify_port,
};

#ifdef CONFIG_SERIAL_SCCNXP_CONSOLE
static void sccnxp_console_putchar(struct uart_port *port, int c)
{
	int tryes = 100000;

	while (tryes--) {
		if (sccnxp_port_read(port, SCCNXP_SR_REG) & SR_TXRDY) {
			sccnxp_port_write(port, SCCNXP_THR_REG, c);
			break;
		}
		barrier();
	}
}

static void sccnxp_console_write(struct console *co, const char *c, unsigned n)
{
	struct sccnxp_port *s = (struct sccnxp_port *)co->data;
	struct uart_port *port = &s->port[co->index];

	mutex_lock(&s->sccnxp_mutex);
	uart_console_write(port, c, n, sccnxp_console_putchar);
	mutex_unlock(&s->sccnxp_mutex);
}

static int sccnxp_console_setup(struct console *co, char *options)
{
	struct sccnxp_port *s = (struct sccnxp_port *)co->data;
	struct uart_port *port = &s->port[(co->index > 0) ? co->index : 0];
	int baud = 9600, bits = 8, parity = 'n', flow = 'n';

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}
#endif

static int __devinit sccnxp_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	int chiptype = pdev->id_entry->driver_data;
	struct sccnxp_pdata *pdata = dev_get_platdata(&pdev->dev);
	int i, ret, fifosize, freq_min, freq_max;
	struct sccnxp_port *s;
	void __iomem *membase;

	if (!res) {
		dev_err(&pdev->dev, "Missing memory resource data\n");
		return -EADDRNOTAVAIL;
	}

	dev_set_name(&pdev->dev, SCCNXP_NAME);

	s = devm_kzalloc(&pdev->dev, sizeof(struct sccnxp_port), GFP_KERNEL);
	if (!s) {
		dev_err(&pdev->dev, "Error allocating port structure\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, s);

	mutex_init(&s->sccnxp_mutex);

	/* Individual chip settings */
	switch (chiptype) {
	case SCCNXP_TYPE_SC2681:
		s->name		= "SC2681";
		s->uart.nr	= 2;
		s->freq_std	= 3686400;
		s->addr_mask	= 0x0f;
		s->flags	= SCCNXP_HAVE_IO;
		fifosize	= 3;
		freq_min	= 1000000;
		freq_max	= 4000000;
		break;
	case SCCNXP_TYPE_SC2691:
		s->name		= "SC2691";
		s->uart.nr	= 1;
		s->freq_std	= 3686400;
		s->addr_mask	= 0x07;
		s->flags	= 0;
		fifosize	= 3;
		freq_min	= 1000000;
		freq_max	= 4000000;
		break;
	case SCCNXP_TYPE_SC2692:
		s->name		= "SC2692";
		s->uart.nr	= 2;
		s->freq_std	= 3686400;
		s->addr_mask	= 0x0f;
		s->flags	= SCCNXP_HAVE_IO;
		fifosize	= 3;
		freq_min	= 1000000;
		freq_max	= 4000000;
		break;
	case SCCNXP_TYPE_SC2891:
		s->name		= "SC2891";
		s->uart.nr	= 1;
		s->freq_std	= 3686400;
		s->addr_mask	= 0x0f;
		s->flags	= SCCNXP_HAVE_IO | SCCNXP_HAVE_MR0;
		fifosize	= 16;
		freq_min	= 100000;
		freq_max	= 8000000;
		break;
	case SCCNXP_TYPE_SC2892:
		s->name		= "SC2892";
		s->uart.nr	= 2;
		s->freq_std	= 3686400;
		s->addr_mask	= 0x0f;
		s->flags	= SCCNXP_HAVE_IO | SCCNXP_HAVE_MR0;
		fifosize	= 16;
		freq_min	= 100000;
		freq_max	= 8000000;
		break;
	case SCCNXP_TYPE_SC28202:
		s->name		= "SC28202";
		s->uart.nr	= 2;
		s->freq_std	= 14745600;
		s->addr_mask	= 0x7f;
		s->flags	= SCCNXP_HAVE_IO | SCCNXP_HAVE_MR0;
		fifosize	= 256;
		freq_min	= 1000000;
		freq_max	= 50000000;
		break;
	case SCCNXP_TYPE_SC68681:
		s->name		= "SC68681";
		s->uart.nr	= 2;
		s->freq_std	= 3686400;
		s->addr_mask	= 0x0f;
		s->flags	= SCCNXP_HAVE_IO;
		fifosize	= 3;
		freq_min	= 1000000;
		freq_max	= 4000000;
		break;
	case SCCNXP_TYPE_SC68692:
		s->name		= "SC68692";
		s->uart.nr	= 2;
		s->freq_std	= 3686400;
		s->addr_mask	= 0x0f;
		s->flags	= SCCNXP_HAVE_IO;
		fifosize	= 3;
		freq_min	= 1000000;
		freq_max	= 4000000;
		break;
	default:
		dev_err(&pdev->dev, "Unsupported chip type %i\n", chiptype);
		ret = -ENOTSUPP;
		goto err_out;
	}

	if (!pdata) {
		dev_warn(&pdev->dev,
			 "No platform data supplied, using defaults\n");
		s->pdata.frequency = s->freq_std;
	} else
		memcpy(&s->pdata, pdata, sizeof(struct sccnxp_pdata));

	s->irq = platform_get_irq(pdev, 0);
	if (s->irq <= 0) {
		dev_err(&pdev->dev, "Missing irq resource data\n");
		ret = -ENXIO;
		goto err_out;
	}

	/* Check input frequency */
	if ((s->pdata.frequency < freq_min) ||
	    (s->pdata.frequency > freq_max)) {
		dev_err(&pdev->dev, "Frequency out of bounds\n");
		ret = -EINVAL;
		goto err_out;
	}

	membase = devm_request_and_ioremap(&pdev->dev, res);
	if (!membase) {
		dev_err(&pdev->dev, "Failed to ioremap\n");
		ret = -EIO;
		goto err_out;
	}

	s->uart.owner		= THIS_MODULE;
	s->uart.dev_name	= "ttySC";
	s->uart.major		= SCCNXP_MAJOR;
	s->uart.minor		= SCCNXP_MINOR;
#ifdef CONFIG_SERIAL_SCCNXP_CONSOLE
	s->uart.cons		= &s->console;
	s->uart.cons->device	= uart_console_device;
	s->uart.cons->write	= sccnxp_console_write;
	s->uart.cons->setup	= sccnxp_console_setup;
	s->uart.cons->flags	= CON_PRINTBUFFER;
	s->uart.cons->index	= -1;
	s->uart.cons->data	= s;
	strcpy(s->uart.cons->name, "ttySC");
#endif
	ret = uart_register_driver(&s->uart);
	if (ret) {
		dev_err(&pdev->dev, "Registering UART driver failed\n");
		goto err_out;
	}

	for (i = 0; i < s->uart.nr; i++) {
		s->port[i].line		= i;
		s->port[i].dev		= &pdev->dev;
		s->port[i].irq		= s->irq;
		s->port[i].type		= PORT_SC26XX;
		s->port[i].fifosize	= fifosize;
		s->port[i].flags	= UPF_SKIP_TEST | UPF_FIXED_TYPE;
		s->port[i].iotype	= UPIO_MEM;
		s->port[i].mapbase	= res->start;
		s->port[i].membase	= membase;
		s->port[i].regshift	= s->pdata.reg_shift;
		s->port[i].uartclk	= s->pdata.frequency;
		s->port[i].ops		= &sccnxp_ops;
		uart_add_one_port(&s->uart, &s->port[i]);
		/* Set direction to input */
		if (s->flags & SCCNXP_HAVE_IO)
			sccnxp_set_bit(&s->port[i], DIR_OP, 0);
	}

	/* Disable interrupts */
	s->imr = 0;
	sccnxp_write(&s->port[0], SCCNXP_IMR_REG, 0);

	/* Board specific configure */
	if (s->pdata.init)
		s->pdata.init();

	ret = devm_request_threaded_irq(&pdev->dev, s->irq, NULL, sccnxp_ist,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					dev_name(&pdev->dev), s);
	if (!ret)
		return 0;

	dev_err(&pdev->dev, "Unable to reguest IRQ %i\n", s->irq);

err_out:
	platform_set_drvdata(pdev, NULL);

	return ret;
}

static int __devexit sccnxp_remove(struct platform_device *pdev)
{
	int i;
	struct sccnxp_port *s = platform_get_drvdata(pdev);

	devm_free_irq(&pdev->dev, s->irq, s);

	for (i = 0; i < s->uart.nr; i++)
		uart_remove_one_port(&s->uart, &s->port[i]);

	uart_unregister_driver(&s->uart);
	platform_set_drvdata(pdev, NULL);

	if (s->pdata.exit)
		s->pdata.exit();

	return 0;
}

static const struct platform_device_id sccnxp_id_table[] = {
	{ "sc2681",	SCCNXP_TYPE_SC2681 },
	{ "sc2691",	SCCNXP_TYPE_SC2691 },
	{ "sc2692",	SCCNXP_TYPE_SC2692 },
	{ "sc2891",	SCCNXP_TYPE_SC2891 },
	{ "sc2892",	SCCNXP_TYPE_SC2892 },
	{ "sc28202",	SCCNXP_TYPE_SC28202 },
	{ "sc68681",	SCCNXP_TYPE_SC68681 },
	{ "sc68692",	SCCNXP_TYPE_SC68692 },
	{ },
};
MODULE_DEVICE_TABLE(platform, sccnxp_id_table);

static struct platform_driver sccnxp_uart_driver = {
	.driver = {
		.name	= SCCNXP_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= sccnxp_probe,
	.remove		= sccnxp_remove,
	.id_table	= sccnxp_id_table,
};
module_platform_driver(sccnxp_uart_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("SCCNXP serial driver");
