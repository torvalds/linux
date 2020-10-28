// SPDX-License-Identifier: GPL-2.0
/*
 * MEN 16z135 High Speed UART
 *
 * Copyright (C) 2014 MEN Mikroelektronik GmbH (www.men.de)
 * Author: Johannes Thumshirn <johannes.thumshirn@men.de>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ":" fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/serial_core.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/tty_flip.h>
#include <linux/bitops.h>
#include <linux/mcb.h>

#define MEN_Z135_MAX_PORTS		12
#define MEN_Z135_BASECLK		29491200
#define MEN_Z135_FIFO_SIZE		1024
#define MEN_Z135_FIFO_WATERMARK		1020

#define MEN_Z135_STAT_REG		0x0
#define MEN_Z135_RX_RAM			0x4
#define MEN_Z135_TX_RAM			0x400
#define MEN_Z135_RX_CTRL		0x800
#define MEN_Z135_TX_CTRL		0x804
#define MEN_Z135_CONF_REG		0x808
#define MEN_Z135_UART_FREQ		0x80c
#define MEN_Z135_BAUD_REG		0x810
#define MEN_Z135_TIMEOUT		0x814

#define IRQ_ID(x) ((x) & 0x1f)

#define MEN_Z135_IER_RXCIEN BIT(0)		/* RX Space IRQ */
#define MEN_Z135_IER_TXCIEN BIT(1)		/* TX Space IRQ */
#define MEN_Z135_IER_RLSIEN BIT(2)		/* Receiver Line Status IRQ */
#define MEN_Z135_IER_MSIEN  BIT(3)		/* Modem Status IRQ */
#define MEN_Z135_ALL_IRQS (MEN_Z135_IER_RXCIEN		\
				| MEN_Z135_IER_RLSIEN	\
				| MEN_Z135_IER_MSIEN	\
				| MEN_Z135_IER_TXCIEN)

#define MEN_Z135_MCR_DTR	BIT(24)
#define MEN_Z135_MCR_RTS	BIT(25)
#define MEN_Z135_MCR_OUT1	BIT(26)
#define MEN_Z135_MCR_OUT2	BIT(27)
#define MEN_Z135_MCR_LOOP	BIT(28)
#define MEN_Z135_MCR_RCFC	BIT(29)

#define MEN_Z135_MSR_DCTS	BIT(0)
#define MEN_Z135_MSR_DDSR	BIT(1)
#define MEN_Z135_MSR_DRI	BIT(2)
#define MEN_Z135_MSR_DDCD	BIT(3)
#define MEN_Z135_MSR_CTS	BIT(4)
#define MEN_Z135_MSR_DSR	BIT(5)
#define MEN_Z135_MSR_RI		BIT(6)
#define MEN_Z135_MSR_DCD	BIT(7)

#define MEN_Z135_LCR_SHIFT 8	/* LCR shift mask */

#define MEN_Z135_WL5 0		/* CS5 */
#define MEN_Z135_WL6 1		/* CS6 */
#define MEN_Z135_WL7 2		/* CS7 */
#define MEN_Z135_WL8 3		/* CS8 */

#define MEN_Z135_STB_SHIFT 2	/* Stopbits */
#define MEN_Z135_NSTB1 0
#define MEN_Z135_NSTB2 1

#define MEN_Z135_PEN_SHIFT 3	/* Parity enable */
#define MEN_Z135_PAR_DIS 0
#define MEN_Z135_PAR_ENA 1

#define MEN_Z135_PTY_SHIFT 4	/* Parity type */
#define MEN_Z135_PTY_ODD 0
#define MEN_Z135_PTY_EVN 1

#define MEN_Z135_LSR_DR BIT(0)
#define MEN_Z135_LSR_OE BIT(1)
#define MEN_Z135_LSR_PE BIT(2)
#define MEN_Z135_LSR_FE BIT(3)
#define MEN_Z135_LSR_BI BIT(4)
#define MEN_Z135_LSR_THEP BIT(5)
#define MEN_Z135_LSR_TEXP BIT(6)
#define MEN_Z135_LSR_RXFIFOERR BIT(7)

#define MEN_Z135_IRQ_ID_RLS BIT(0)
#define MEN_Z135_IRQ_ID_RDA BIT(1)
#define MEN_Z135_IRQ_ID_CTI BIT(2)
#define MEN_Z135_IRQ_ID_TSA BIT(3)
#define MEN_Z135_IRQ_ID_MST BIT(4)

#define LCR(x) (((x) >> MEN_Z135_LCR_SHIFT) & 0xff)

#define BYTES_TO_ALIGN(x) ((x) & 0x3)

static int line;

static int txlvl = 5;
module_param(txlvl, int, S_IRUGO);
MODULE_PARM_DESC(txlvl, "TX IRQ trigger level 0-7, default 5 (128 byte)");

static int rxlvl = 6;
module_param(rxlvl, int, S_IRUGO);
MODULE_PARM_DESC(rxlvl, "RX IRQ trigger level 0-7, default 6 (256 byte)");

static int align;
module_param(align, int, S_IRUGO);
MODULE_PARM_DESC(align, "Keep hardware FIFO write pointer aligned, default 0");

static uint rx_timeout;
module_param(rx_timeout, uint, S_IRUGO);
MODULE_PARM_DESC(rx_timeout, "RX timeout. "
		"Timeout in seconds = (timeout_reg * baud_reg * 4) / freq_reg");

struct men_z135_port {
	struct uart_port port;
	struct mcb_device *mdev;
	struct resource *mem;
	unsigned char *rxbuf;
	u32 stat_reg;
	spinlock_t lock;
	bool automode;
};
#define to_men_z135(port) container_of((port), struct men_z135_port, port)

/**
 * men_z135_reg_set() - Set value in register
 * @uart: The UART port
 * @addr: Register address
 * @val: value to set
 */
static inline void men_z135_reg_set(struct men_z135_port *uart,
				u32 addr, u32 val)
{
	struct uart_port *port = &uart->port;
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&uart->lock, flags);

	reg = ioread32(port->membase + addr);
	reg |= val;
	iowrite32(reg, port->membase + addr);

	spin_unlock_irqrestore(&uart->lock, flags);
}

/**
 * men_z135_reg_clr() - Unset value in register
 * @uart: The UART port
 * @addr: Register address
 * @val: value to clear
 */
static void men_z135_reg_clr(struct men_z135_port *uart,
				u32 addr, u32 val)
{
	struct uart_port *port = &uart->port;
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&uart->lock, flags);

	reg = ioread32(port->membase + addr);
	reg &= ~val;
	iowrite32(reg, port->membase + addr);

	spin_unlock_irqrestore(&uart->lock, flags);
}

/**
 * men_z135_handle_modem_status() - Handle change of modem status
 * @uart: The UART port
 *
 * Handle change of modem status register. This is done by reading the "delta"
 * versions of DCD (Data Carrier Detect) and CTS (Clear To Send).
 */
static void men_z135_handle_modem_status(struct men_z135_port *uart)
{
	u8 msr;

	msr = (uart->stat_reg >> 8) & 0xff;

	if (msr & MEN_Z135_MSR_DDCD)
		uart_handle_dcd_change(&uart->port,
				msr & MEN_Z135_MSR_DCD);
	if (msr & MEN_Z135_MSR_DCTS)
		uart_handle_cts_change(&uart->port,
				msr & MEN_Z135_MSR_CTS);
}

static void men_z135_handle_lsr(struct men_z135_port *uart)
{
	struct uart_port *port = &uart->port;
	u8 lsr;

	lsr = (uart->stat_reg >> 16) & 0xff;

	if (lsr & MEN_Z135_LSR_OE)
		port->icount.overrun++;
	if (lsr & MEN_Z135_LSR_PE)
		port->icount.parity++;
	if (lsr & MEN_Z135_LSR_FE)
		port->icount.frame++;
	if (lsr & MEN_Z135_LSR_BI) {
		port->icount.brk++;
		uart_handle_break(port);
	}
}

/**
 * get_rx_fifo_content() - Get the number of bytes in RX FIFO
 * @uart: The UART port
 *
 * Read RXC register from hardware and return current FIFO fill size.
 */
static u16 get_rx_fifo_content(struct men_z135_port *uart)
{
	struct uart_port *port = &uart->port;
	u32 stat_reg;
	u16 rxc;
	u8 rxc_lo;
	u8 rxc_hi;

	stat_reg = ioread32(port->membase + MEN_Z135_STAT_REG);
	rxc_lo = stat_reg >> 24;
	rxc_hi = (stat_reg & 0xC0) >> 6;

	rxc = rxc_lo | (rxc_hi << 8);

	return rxc;
}

/**
 * men_z135_handle_rx() - RX tasklet routine
 * @uart: Pointer to struct men_z135_port
 *
 * Copy from RX FIFO and acknowledge number of bytes copied.
 */
static void men_z135_handle_rx(struct men_z135_port *uart)
{
	struct uart_port *port = &uart->port;
	struct tty_port *tport = &port->state->port;
	int copied;
	u16 size;
	int room;

	size = get_rx_fifo_content(uart);

	if (size == 0)
		return;

	/* Avoid accidently accessing TX FIFO instead of RX FIFO. Last
	 * longword in RX FIFO cannot be read.(0x004-0x3FF)
	 */
	if (size > MEN_Z135_FIFO_WATERMARK)
		size = MEN_Z135_FIFO_WATERMARK;

	room = tty_buffer_request_room(tport, size);
	if (room != size)
		dev_warn(&uart->mdev->dev,
			"Not enough room in flip buffer, truncating to %d\n",
			room);

	if (room == 0)
		return;

	memcpy_fromio(uart->rxbuf, port->membase + MEN_Z135_RX_RAM, room);
	/* Be sure to first copy all data and then acknowledge it */
	mb();
	iowrite32(room, port->membase +  MEN_Z135_RX_CTRL);

	copied = tty_insert_flip_string(tport, uart->rxbuf, room);
	if (copied != room)
		dev_warn(&uart->mdev->dev,
			"Only copied %d instead of %d bytes\n",
			copied, room);

	port->icount.rx += copied;

	tty_flip_buffer_push(tport);

}

/**
 * men_z135_handle_tx() - TX tasklet routine
 * @uart: Pointer to struct men_z135_port
 *
 */
static void men_z135_handle_tx(struct men_z135_port *uart)
{
	struct uart_port *port = &uart->port;
	struct circ_buf *xmit = &port->state->xmit;
	u32 txc;
	u32 wptr;
	int qlen;
	int n;
	int txfree;
	int head;
	int tail;
	int s;

	if (uart_circ_empty(xmit))
		goto out;

	if (uart_tx_stopped(port))
		goto out;

	if (port->x_char)
		goto out;

	/* calculate bytes to copy */
	qlen = uart_circ_chars_pending(xmit);
	if (qlen <= 0)
		goto out;

	wptr = ioread32(port->membase + MEN_Z135_TX_CTRL);
	txc = (wptr >> 16) & 0x3ff;
	wptr &= 0x3ff;

	if (txc > MEN_Z135_FIFO_WATERMARK)
		txc = MEN_Z135_FIFO_WATERMARK;

	txfree = MEN_Z135_FIFO_WATERMARK - txc;
	if (txfree <= 0) {
		dev_err(&uart->mdev->dev,
			"Not enough room in TX FIFO have %d, need %d\n",
			txfree, qlen);
		goto irq_en;
	}

	/* if we're not aligned, it's better to copy only 1 or 2 bytes and
	 * then the rest.
	 */
	if (align && qlen >= 3 && BYTES_TO_ALIGN(wptr))
		n = 4 - BYTES_TO_ALIGN(wptr);
	else if (qlen > txfree)
		n = txfree;
	else
		n = qlen;

	if (n <= 0)
		goto irq_en;

	head = xmit->head & (UART_XMIT_SIZE - 1);
	tail = xmit->tail & (UART_XMIT_SIZE - 1);

	s = ((head >= tail) ? head : UART_XMIT_SIZE) - tail;
	n = min(n, s);

	memcpy_toio(port->membase + MEN_Z135_TX_RAM, &xmit->buf[xmit->tail], n);
	xmit->tail = (xmit->tail + n) & (UART_XMIT_SIZE - 1);

	iowrite32(n & 0x3ff, port->membase + MEN_Z135_TX_CTRL);

	port->icount.tx += n;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

irq_en:
	if (!uart_circ_empty(xmit))
		men_z135_reg_set(uart, MEN_Z135_CONF_REG, MEN_Z135_IER_TXCIEN);
	else
		men_z135_reg_clr(uart, MEN_Z135_CONF_REG, MEN_Z135_IER_TXCIEN);

out:
	return;

}

/**
 * men_z135_intr() - Handle legacy IRQs
 * @irq: The IRQ number
 * @data: Pointer to UART port
 *
 * Check IIR register to find the cause of the interrupt and handle it.
 * It is possible that multiple interrupts reason bits are set and reading
 * the IIR is a destructive read, so we always need to check for all possible
 * interrupts and handle them.
 */
static irqreturn_t men_z135_intr(int irq, void *data)
{
	struct men_z135_port *uart = (struct men_z135_port *)data;
	struct uart_port *port = &uart->port;
	bool handled = false;
	int irq_id;

	uart->stat_reg = ioread32(port->membase + MEN_Z135_STAT_REG);
	irq_id = IRQ_ID(uart->stat_reg);

	if (!irq_id)
		goto out;

	spin_lock(&port->lock);
	/* It's save to write to IIR[7:6] RXC[9:8] */
	iowrite8(irq_id, port->membase + MEN_Z135_STAT_REG);

	if (irq_id & MEN_Z135_IRQ_ID_RLS) {
		men_z135_handle_lsr(uart);
		handled = true;
	}

	if (irq_id & (MEN_Z135_IRQ_ID_RDA | MEN_Z135_IRQ_ID_CTI)) {
		if (irq_id & MEN_Z135_IRQ_ID_CTI)
			dev_dbg(&uart->mdev->dev, "Character Timeout Indication\n");
		men_z135_handle_rx(uart);
		handled = true;
	}

	if (irq_id & MEN_Z135_IRQ_ID_TSA) {
		men_z135_handle_tx(uart);
		handled = true;
	}

	if (irq_id & MEN_Z135_IRQ_ID_MST) {
		men_z135_handle_modem_status(uart);
		handled = true;
	}

	spin_unlock(&port->lock);
out:
	return IRQ_RETVAL(handled);
}

/**
 * men_z135_request_irq() - Request IRQ for 16z135 core
 * @uart: z135 private uart port structure
 *
 * Request an IRQ for 16z135 to use. First try using MSI, if it fails
 * fall back to using legacy interrupts.
 */
static int men_z135_request_irq(struct men_z135_port *uart)
{
	struct device *dev = &uart->mdev->dev;
	struct uart_port *port = &uart->port;
	int err = 0;

	err = request_irq(port->irq, men_z135_intr, IRQF_SHARED,
			"men_z135_intr", uart);
	if (err)
		dev_err(dev, "Error %d getting interrupt\n", err);

	return err;
}

/**
 * men_z135_tx_empty() - Handle tx_empty call
 * @port: The UART port
 *
 * This function tests whether the TX FIFO and shifter for the port
 * described by @port is empty.
 */
static unsigned int men_z135_tx_empty(struct uart_port *port)
{
	u32 wptr;
	u16 txc;

	wptr = ioread32(port->membase + MEN_Z135_TX_CTRL);
	txc = (wptr >> 16) & 0x3ff;

	if (txc == 0)
		return TIOCSER_TEMT;
	else
		return 0;
}

/**
 * men_z135_set_mctrl() - Set modem control lines
 * @port: The UART port
 * @mctrl: The modem control lines
 *
 * This function sets the modem control lines for a port described by @port
 * to the state described by @mctrl
 */
static void men_z135_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	u32 old;
	u32 conf_reg;

	conf_reg = old = ioread32(port->membase + MEN_Z135_CONF_REG);
	if (mctrl & TIOCM_RTS)
		conf_reg |= MEN_Z135_MCR_RTS;
	else
		conf_reg &= ~MEN_Z135_MCR_RTS;

	if (mctrl & TIOCM_DTR)
		conf_reg |= MEN_Z135_MCR_DTR;
	else
		conf_reg &= ~MEN_Z135_MCR_DTR;

	if (mctrl & TIOCM_OUT1)
		conf_reg |= MEN_Z135_MCR_OUT1;
	else
		conf_reg &= ~MEN_Z135_MCR_OUT1;

	if (mctrl & TIOCM_OUT2)
		conf_reg |= MEN_Z135_MCR_OUT2;
	else
		conf_reg &= ~MEN_Z135_MCR_OUT2;

	if (mctrl & TIOCM_LOOP)
		conf_reg |= MEN_Z135_MCR_LOOP;
	else
		conf_reg &= ~MEN_Z135_MCR_LOOP;

	if (conf_reg != old)
		iowrite32(conf_reg, port->membase + MEN_Z135_CONF_REG);
}

/**
 * men_z135_get_mctrl() - Get modem control lines
 * @port: The UART port
 *
 * Retruns the current state of modem control inputs.
 */
static unsigned int men_z135_get_mctrl(struct uart_port *port)
{
	unsigned int mctrl = 0;
	u8 msr;

	msr = ioread8(port->membase + MEN_Z135_STAT_REG + 1);

	if (msr & MEN_Z135_MSR_CTS)
		mctrl |= TIOCM_CTS;
	if (msr & MEN_Z135_MSR_DSR)
		mctrl |= TIOCM_DSR;
	if (msr & MEN_Z135_MSR_RI)
		mctrl |= TIOCM_RI;
	if (msr & MEN_Z135_MSR_DCD)
		mctrl |= TIOCM_CAR;

	return mctrl;
}

/**
 * men_z135_stop_tx() - Stop transmitting characters
 * @port: The UART port
 *
 * Stop transmitting characters. This might be due to CTS line becomming
 * inactive or the tty layer indicating we want to stop transmission due to
 * an XOFF character.
 */
static void men_z135_stop_tx(struct uart_port *port)
{
	struct men_z135_port *uart = to_men_z135(port);

	men_z135_reg_clr(uart, MEN_Z135_CONF_REG, MEN_Z135_IER_TXCIEN);
}

/*
 * men_z135_disable_ms() - Disable Modem Status
 * port: The UART port
 *
 * Enable Modem Status IRQ.
 */
static void men_z135_disable_ms(struct uart_port *port)
{
	struct men_z135_port *uart = to_men_z135(port);

	men_z135_reg_clr(uart, MEN_Z135_CONF_REG, MEN_Z135_IER_MSIEN);
}

/**
 * men_z135_start_tx() - Start transmitting characters
 * @port: The UART port
 *
 * Start transmitting character. This actually doesn't transmit anything, but
 * fires off the TX tasklet.
 */
static void men_z135_start_tx(struct uart_port *port)
{
	struct men_z135_port *uart = to_men_z135(port);

	if (uart->automode)
		men_z135_disable_ms(port);

	men_z135_handle_tx(uart);
}

/**
 * men_z135_stop_rx() - Stop receiving characters
 * @port: The UART port
 *
 * Stop receiving characters; the port is in the process of being closed.
 */
static void men_z135_stop_rx(struct uart_port *port)
{
	struct men_z135_port *uart = to_men_z135(port);

	men_z135_reg_clr(uart, MEN_Z135_CONF_REG, MEN_Z135_IER_RXCIEN);
}

/**
 * men_z135_enable_ms() - Enable Modem Status
 * @port: the port
 *
 * Enable Modem Status IRQ.
 */
static void men_z135_enable_ms(struct uart_port *port)
{
	struct men_z135_port *uart = to_men_z135(port);

	men_z135_reg_set(uart, MEN_Z135_CONF_REG, MEN_Z135_IER_MSIEN);
}

static int men_z135_startup(struct uart_port *port)
{
	struct men_z135_port *uart = to_men_z135(port);
	int err;
	u32 conf_reg = 0;

	err = men_z135_request_irq(uart);
	if (err)
		return -ENODEV;

	conf_reg = ioread32(port->membase + MEN_Z135_CONF_REG);

	/* Activate all but TX space available IRQ */
	conf_reg |= MEN_Z135_ALL_IRQS & ~MEN_Z135_IER_TXCIEN;
	conf_reg &= ~(0xff << 16);
	conf_reg |= (txlvl << 16);
	conf_reg |= (rxlvl << 20);

	iowrite32(conf_reg, port->membase + MEN_Z135_CONF_REG);

	if (rx_timeout)
		iowrite32(rx_timeout, port->membase + MEN_Z135_TIMEOUT);

	return 0;
}

static void men_z135_shutdown(struct uart_port *port)
{
	struct men_z135_port *uart = to_men_z135(port);
	u32 conf_reg = 0;

	conf_reg |= MEN_Z135_ALL_IRQS;

	men_z135_reg_clr(uart, MEN_Z135_CONF_REG, conf_reg);

	free_irq(uart->port.irq, uart);
}

static void men_z135_set_termios(struct uart_port *port,
				struct ktermios *termios,
				struct ktermios *old)
{
	struct men_z135_port *uart = to_men_z135(port);
	unsigned int baud;
	u32 conf_reg;
	u32 bd_reg;
	u32 uart_freq;
	u8 lcr;

	conf_reg = ioread32(port->membase + MEN_Z135_CONF_REG);
	lcr = LCR(conf_reg);

	/* byte size */
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		lcr |= MEN_Z135_WL5;
		break;
	case CS6:
		lcr |= MEN_Z135_WL6;
		break;
	case CS7:
		lcr |= MEN_Z135_WL7;
		break;
	case CS8:
		lcr |= MEN_Z135_WL8;
		break;
	}

	/* stop bits */
	if (termios->c_cflag & CSTOPB)
		lcr |= MEN_Z135_NSTB2 << MEN_Z135_STB_SHIFT;

	/* parity */
	if (termios->c_cflag & PARENB) {
		lcr |= MEN_Z135_PAR_ENA << MEN_Z135_PEN_SHIFT;

		if (termios->c_cflag & PARODD)
			lcr |= MEN_Z135_PTY_ODD << MEN_Z135_PTY_SHIFT;
		else
			lcr |= MEN_Z135_PTY_EVN << MEN_Z135_PTY_SHIFT;
	} else
		lcr |= MEN_Z135_PAR_DIS << MEN_Z135_PEN_SHIFT;

	conf_reg |= MEN_Z135_IER_MSIEN;
	if (termios->c_cflag & CRTSCTS) {
		conf_reg |= MEN_Z135_MCR_RCFC;
		uart->automode = true;
		termios->c_cflag &= ~CLOCAL;
	} else {
		conf_reg &= ~MEN_Z135_MCR_RCFC;
		uart->automode = false;
	}

	termios->c_cflag &= ~CMSPAR; /* Mark/Space parity is not supported */

	conf_reg |= lcr << MEN_Z135_LCR_SHIFT;
	iowrite32(conf_reg, port->membase + MEN_Z135_CONF_REG);

	uart_freq = ioread32(port->membase + MEN_Z135_UART_FREQ);
	if (uart_freq == 0)
		uart_freq = MEN_Z135_BASECLK;

	baud = uart_get_baud_rate(port, termios, old, 0, uart_freq / 16);

	spin_lock_irq(&port->lock);
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);

	bd_reg = uart_freq / (4 * baud);
	iowrite32(bd_reg, port->membase + MEN_Z135_BAUD_REG);

	uart_update_timeout(port, termios->c_cflag, baud);
	spin_unlock_irq(&port->lock);
}

static const char *men_z135_type(struct uart_port *port)
{
	return KBUILD_MODNAME;
}

static void men_z135_release_port(struct uart_port *port)
{
	struct men_z135_port *uart = to_men_z135(port);

	iounmap(port->membase);
	port->membase = NULL;

	mcb_release_mem(uart->mem);
}

static int men_z135_request_port(struct uart_port *port)
{
	struct men_z135_port *uart = to_men_z135(port);
	struct mcb_device *mdev = uart->mdev;
	struct resource *mem;

	mem = mcb_request_mem(uart->mdev, dev_name(&mdev->dev));
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	port->mapbase = mem->start;
	uart->mem = mem;

	port->membase = ioremap(mem->start, resource_size(mem));
	if (port->membase == NULL) {
		mcb_release_mem(mem);
		return -ENOMEM;
	}

	return 0;
}

static void men_z135_config_port(struct uart_port *port, int type)
{
	port->type = PORT_MEN_Z135;
	men_z135_request_port(port);
}

static int men_z135_verify_port(struct uart_port *port,
				struct serial_struct *serinfo)
{
	return -EINVAL;
}

static const struct uart_ops men_z135_ops = {
	.tx_empty = men_z135_tx_empty,
	.set_mctrl = men_z135_set_mctrl,
	.get_mctrl = men_z135_get_mctrl,
	.stop_tx = men_z135_stop_tx,
	.start_tx = men_z135_start_tx,
	.stop_rx = men_z135_stop_rx,
	.enable_ms = men_z135_enable_ms,
	.startup = men_z135_startup,
	.shutdown = men_z135_shutdown,
	.set_termios = men_z135_set_termios,
	.type = men_z135_type,
	.release_port = men_z135_release_port,
	.request_port = men_z135_request_port,
	.config_port = men_z135_config_port,
	.verify_port = men_z135_verify_port,
};

static struct uart_driver men_z135_driver = {
	.owner = THIS_MODULE,
	.driver_name = KBUILD_MODNAME,
	.dev_name = "ttyHSU",
	.major = 0,
	.minor = 0,
	.nr = MEN_Z135_MAX_PORTS,
};

/**
 * men_z135_probe() - Probe a z135 instance
 * @mdev: The MCB device
 * @id: The MCB device ID
 *
 * men_z135_probe does the basic setup of hardware resources and registers the
 * new uart port to the tty layer.
 */
static int men_z135_probe(struct mcb_device *mdev,
			const struct mcb_device_id *id)
{
	struct men_z135_port *uart;
	struct resource *mem;
	struct device *dev;
	int err;

	dev = &mdev->dev;

	uart = devm_kzalloc(dev, sizeof(struct men_z135_port), GFP_KERNEL);
	if (!uart)
		return -ENOMEM;

	uart->rxbuf = (unsigned char *)__get_free_page(GFP_KERNEL);
	if (!uart->rxbuf)
		return -ENOMEM;

	mem = &mdev->mem;

	mcb_set_drvdata(mdev, uart);

	uart->port.uartclk = MEN_Z135_BASECLK * 16;
	uart->port.fifosize = MEN_Z135_FIFO_SIZE;
	uart->port.iotype = UPIO_MEM;
	uart->port.ops = &men_z135_ops;
	uart->port.irq = mcb_get_irq(mdev);
	uart->port.iotype = UPIO_MEM;
	uart->port.flags = UPF_BOOT_AUTOCONF | UPF_IOREMAP;
	uart->port.line = line++;
	uart->port.dev = dev;
	uart->port.type = PORT_MEN_Z135;
	uart->port.mapbase = mem->start;
	uart->port.membase = NULL;
	uart->mdev = mdev;

	spin_lock_init(&uart->lock);

	err = uart_add_one_port(&men_z135_driver, &uart->port);
	if (err)
		goto err;

	return 0;

err:
	free_page((unsigned long) uart->rxbuf);
	dev_err(dev, "Failed to add UART: %d\n", err);

	return err;
}

/**
 * men_z135_remove() - Remove a z135 instance from the system
 *
 * @mdev: The MCB device
 */
static void men_z135_remove(struct mcb_device *mdev)
{
	struct men_z135_port *uart = mcb_get_drvdata(mdev);

	line--;
	uart_remove_one_port(&men_z135_driver, &uart->port);
	free_page((unsigned long) uart->rxbuf);
}

static const struct mcb_device_id men_z135_ids[] = {
	{ .device = 0x87 },
	{ }
};
MODULE_DEVICE_TABLE(mcb, men_z135_ids);

static struct mcb_driver mcb_driver = {
	.driver = {
		.name = "z135-uart",
		.owner = THIS_MODULE,
	},
	.probe = men_z135_probe,
	.remove = men_z135_remove,
	.id_table = men_z135_ids,
};

/**
 * men_z135_init() - Driver Registration Routine
 *
 * men_z135_init is the first routine called when the driver is loaded. All it
 * does is register with the legacy MEN Chameleon subsystem.
 */
static int __init men_z135_init(void)
{
	int err;

	err = uart_register_driver(&men_z135_driver);
	if (err) {
		pr_err("Failed to register UART: %d\n", err);
		return err;
	}

	err = mcb_register_driver(&mcb_driver);
	if  (err) {
		pr_err("Failed to register MCB driver: %d\n", err);
		uart_unregister_driver(&men_z135_driver);
		return err;
	}

	return 0;
}
module_init(men_z135_init);

/**
 * men_z135_exit() - Driver Exit Routine
 *
 * men_z135_exit is called just before the driver is removed from memory.
 */
static void __exit men_z135_exit(void)
{
	mcb_unregister_driver(&mcb_driver);
	uart_unregister_driver(&men_z135_driver);
}
module_exit(men_z135_exit);

MODULE_AUTHOR("Johannes Thumshirn <johannes.thumshirn@men.de>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MEN 16z135 High Speed UART");
MODULE_ALIAS("mcb:16z135");
MODULE_IMPORT_NS(MCB);
