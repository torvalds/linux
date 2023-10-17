// SPDX-License-Identifier: GPL-2.0+
/*
* ***************************************************************************
* Marvell Armada-3700 Serial Driver
* Author: Wilson Ding <dingwei@marvell.com>
* Copyright (C) 2015 Marvell International Ltd.
* ***************************************************************************
*/

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/math64.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

/* Register Map */
#define UART_STD_RBR		0x00
#define UART_EXT_RBR		0x18

#define UART_STD_TSH		0x04
#define UART_EXT_TSH		0x1C

#define UART_STD_CTRL1		0x08
#define UART_EXT_CTRL1		0x04
#define  CTRL_SOFT_RST		BIT(31)
#define  CTRL_TXFIFO_RST	BIT(15)
#define  CTRL_RXFIFO_RST	BIT(14)
#define  CTRL_SND_BRK_SEQ	BIT(11)
#define  CTRL_BRK_DET_INT	BIT(3)
#define  CTRL_FRM_ERR_INT	BIT(2)
#define  CTRL_PAR_ERR_INT	BIT(1)
#define  CTRL_OVR_ERR_INT	BIT(0)
#define  CTRL_BRK_INT		(CTRL_BRK_DET_INT | CTRL_FRM_ERR_INT | \
				CTRL_PAR_ERR_INT | CTRL_OVR_ERR_INT)

#define UART_STD_CTRL2		UART_STD_CTRL1
#define UART_EXT_CTRL2		0x20
#define  CTRL_STD_TX_RDY_INT	BIT(5)
#define  CTRL_EXT_TX_RDY_INT	BIT(6)
#define  CTRL_STD_RX_RDY_INT	BIT(4)
#define  CTRL_EXT_RX_RDY_INT	BIT(5)

#define UART_STAT		0x0C
#define  STAT_TX_FIFO_EMP	BIT(13)
#define  STAT_TX_FIFO_FUL	BIT(11)
#define  STAT_TX_EMP		BIT(6)
#define  STAT_STD_TX_RDY	BIT(5)
#define  STAT_EXT_TX_RDY	BIT(15)
#define  STAT_STD_RX_RDY	BIT(4)
#define  STAT_EXT_RX_RDY	BIT(14)
#define  STAT_BRK_DET		BIT(3)
#define  STAT_FRM_ERR		BIT(2)
#define  STAT_PAR_ERR		BIT(1)
#define  STAT_OVR_ERR		BIT(0)
#define  STAT_BRK_ERR		(STAT_BRK_DET | STAT_FRM_ERR \
				 | STAT_PAR_ERR | STAT_OVR_ERR)

/*
 * Marvell Armada 3700 Functional Specifications describes that bit 21 of UART
 * Clock Control register controls UART1 and bit 20 controls UART2. But in
 * reality bit 21 controls UART2 and bit 20 controls UART1. This seems to be an
 * error in Marvell's documentation. Hence following CLK_DIS macros are swapped.
 */

#define UART_BRDV		0x10
/* These bits are located in UART1 address space and control UART2 */
#define  UART2_CLK_DIS		BIT(21)
/* These bits are located in UART1 address space and control UART1 */
#define  UART1_CLK_DIS		BIT(20)
/* These bits are located in UART1 address space and control both UARTs */
#define  CLK_NO_XTAL		BIT(19)
#define  CLK_TBG_DIV1_SHIFT	15
#define  CLK_TBG_DIV1_MASK	0x7
#define  CLK_TBG_DIV1_MAX	6
#define  CLK_TBG_DIV2_SHIFT	12
#define  CLK_TBG_DIV2_MASK	0x7
#define  CLK_TBG_DIV2_MAX	6
#define  CLK_TBG_SEL_SHIFT	10
#define  CLK_TBG_SEL_MASK	0x3
/* These bits are located in both UARTs address space */
#define  BRDV_BAUD_MASK         0x3FF
#define  BRDV_BAUD_MAX		BRDV_BAUD_MASK

#define UART_OSAMP		0x14
#define  OSAMP_DEFAULT_DIVISOR	16
#define  OSAMP_DIVISORS_MASK	0x3F3F3F3F
#define  OSAMP_MAX_DIVISOR	63

#define MVEBU_NR_UARTS		2

#define MVEBU_UART_TYPE		"mvebu-uart"
#define DRIVER_NAME		"mvebu_serial"

enum {
	/* Either there is only one summed IRQ... */
	UART_IRQ_SUM = 0,
	/* ...or there are two separate IRQ for RX and TX */
	UART_RX_IRQ = 0,
	UART_TX_IRQ,
	UART_IRQ_COUNT
};

/* Diverging register offsets */
struct uart_regs_layout {
	unsigned int rbr;
	unsigned int tsh;
	unsigned int ctrl;
	unsigned int intr;
};

/* Diverging flags */
struct uart_flags {
	unsigned int ctrl_tx_rdy_int;
	unsigned int ctrl_rx_rdy_int;
	unsigned int stat_tx_rdy;
	unsigned int stat_rx_rdy;
};

/* Driver data, a structure for each UART port */
struct mvebu_uart_driver_data {
	bool is_ext;
	struct uart_regs_layout regs;
	struct uart_flags flags;
};

/* Saved registers during suspend */
struct mvebu_uart_pm_regs {
	unsigned int rbr;
	unsigned int tsh;
	unsigned int ctrl;
	unsigned int intr;
	unsigned int stat;
	unsigned int brdv;
	unsigned int osamp;
};

/* MVEBU UART driver structure */
struct mvebu_uart {
	struct uart_port *port;
	struct clk *clk;
	int irq[UART_IRQ_COUNT];
	struct mvebu_uart_driver_data *data;
#if defined(CONFIG_PM)
	struct mvebu_uart_pm_regs pm_regs;
#endif /* CONFIG_PM */
};

static struct mvebu_uart *to_mvuart(struct uart_port *port)
{
	return (struct mvebu_uart *)port->private_data;
}

#define IS_EXTENDED(port) (to_mvuart(port)->data->is_ext)

#define UART_RBR(port) (to_mvuart(port)->data->regs.rbr)
#define UART_TSH(port) (to_mvuart(port)->data->regs.tsh)
#define UART_CTRL(port) (to_mvuart(port)->data->regs.ctrl)
#define UART_INTR(port) (to_mvuart(port)->data->regs.intr)

#define CTRL_TX_RDY_INT(port) (to_mvuart(port)->data->flags.ctrl_tx_rdy_int)
#define CTRL_RX_RDY_INT(port) (to_mvuart(port)->data->flags.ctrl_rx_rdy_int)
#define STAT_TX_RDY(port) (to_mvuart(port)->data->flags.stat_tx_rdy)
#define STAT_RX_RDY(port) (to_mvuart(port)->data->flags.stat_rx_rdy)

static struct uart_port mvebu_uart_ports[MVEBU_NR_UARTS];

static DEFINE_SPINLOCK(mvebu_uart_lock);

/* Core UART Driver Operations */
static unsigned int mvebu_uart_tx_empty(struct uart_port *port)
{
	unsigned long flags;
	unsigned int st;

	spin_lock_irqsave(&port->lock, flags);
	st = readl(port->membase + UART_STAT);
	spin_unlock_irqrestore(&port->lock, flags);

	return (st & STAT_TX_EMP) ? TIOCSER_TEMT : 0;
}

static unsigned int mvebu_uart_get_mctrl(struct uart_port *port)
{
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static void mvebu_uart_set_mctrl(struct uart_port *port,
				 unsigned int mctrl)
{
/*
 * Even if we do not support configuring the modem control lines, this
 * function must be proided to the serial core
 */
}

static void mvebu_uart_stop_tx(struct uart_port *port)
{
	unsigned int ctl = readl(port->membase + UART_INTR(port));

	ctl &= ~CTRL_TX_RDY_INT(port);
	writel(ctl, port->membase + UART_INTR(port));
}

static void mvebu_uart_start_tx(struct uart_port *port)
{
	unsigned int ctl;
	struct circ_buf *xmit = &port->state->xmit;

	if (IS_EXTENDED(port) && !uart_circ_empty(xmit)) {
		writel(xmit->buf[xmit->tail], port->membase + UART_TSH(port));
		uart_xmit_advance(port, 1);
	}

	ctl = readl(port->membase + UART_INTR(port));
	ctl |= CTRL_TX_RDY_INT(port);
	writel(ctl, port->membase + UART_INTR(port));
}

static void mvebu_uart_stop_rx(struct uart_port *port)
{
	unsigned int ctl;

	ctl = readl(port->membase + UART_CTRL(port));
	ctl &= ~CTRL_BRK_INT;
	writel(ctl, port->membase + UART_CTRL(port));

	ctl = readl(port->membase + UART_INTR(port));
	ctl &= ~CTRL_RX_RDY_INT(port);
	writel(ctl, port->membase + UART_INTR(port));
}

static void mvebu_uart_break_ctl(struct uart_port *port, int brk)
{
	unsigned int ctl;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	ctl = readl(port->membase + UART_CTRL(port));
	if (brk == -1)
		ctl |= CTRL_SND_BRK_SEQ;
	else
		ctl &= ~CTRL_SND_BRK_SEQ;
	writel(ctl, port->membase + UART_CTRL(port));
	spin_unlock_irqrestore(&port->lock, flags);
}

static void mvebu_uart_rx_chars(struct uart_port *port, unsigned int status)
{
	struct tty_port *tport = &port->state->port;
	unsigned char ch = 0;
	char flag = 0;
	int ret;

	do {
		if (status & STAT_RX_RDY(port)) {
			ch = readl(port->membase + UART_RBR(port));
			ch &= 0xff;
			flag = TTY_NORMAL;
			port->icount.rx++;

			if (status & STAT_PAR_ERR)
				port->icount.parity++;
		}

		/*
		 * For UART2, error bits are not cleared on buffer read.
		 * This causes interrupt loop and system hang.
		 */
		if (IS_EXTENDED(port) && (status & STAT_BRK_ERR)) {
			ret = readl(port->membase + UART_STAT);
			ret |= STAT_BRK_ERR;
			writel(ret, port->membase + UART_STAT);
		}

		if (status & STAT_BRK_DET) {
			port->icount.brk++;
			status &= ~(STAT_FRM_ERR | STAT_PAR_ERR);
			if (uart_handle_break(port))
				goto ignore_char;
		}

		if (status & STAT_OVR_ERR)
			port->icount.overrun++;

		if (status & STAT_FRM_ERR)
			port->icount.frame++;

		if (uart_handle_sysrq_char(port, ch))
			goto ignore_char;

		if (status & port->ignore_status_mask & STAT_PAR_ERR)
			status &= ~STAT_RX_RDY(port);

		status &= port->read_status_mask;

		if (status & STAT_PAR_ERR)
			flag = TTY_PARITY;

		status &= ~port->ignore_status_mask;

		if (status & STAT_RX_RDY(port))
			tty_insert_flip_char(tport, ch, flag);

		if (status & STAT_BRK_DET)
			tty_insert_flip_char(tport, 0, TTY_BREAK);

		if (status & STAT_FRM_ERR)
			tty_insert_flip_char(tport, 0, TTY_FRAME);

		if (status & STAT_OVR_ERR)
			tty_insert_flip_char(tport, 0, TTY_OVERRUN);

ignore_char:
		status = readl(port->membase + UART_STAT);
	} while (status & (STAT_RX_RDY(port) | STAT_BRK_DET));

	tty_flip_buffer_push(tport);
}

static void mvebu_uart_tx_chars(struct uart_port *port, unsigned int status)
{
	u8 ch;

	uart_port_tx_limited(port, ch, port->fifosize,
		!(readl(port->membase + UART_STAT) & STAT_TX_FIFO_FUL),
		writel(ch, port->membase + UART_TSH(port)),
		({}));
}

static irqreturn_t mvebu_uart_isr(int irq, void *dev_id)
{
	struct uart_port *port = (struct uart_port *)dev_id;
	unsigned int st = readl(port->membase + UART_STAT);

	if (st & (STAT_RX_RDY(port) | STAT_OVR_ERR | STAT_FRM_ERR |
		  STAT_BRK_DET))
		mvebu_uart_rx_chars(port, st);

	if (st & STAT_TX_RDY(port))
		mvebu_uart_tx_chars(port, st);

	return IRQ_HANDLED;
}

static irqreturn_t mvebu_uart_rx_isr(int irq, void *dev_id)
{
	struct uart_port *port = (struct uart_port *)dev_id;
	unsigned int st = readl(port->membase + UART_STAT);

	if (st & (STAT_RX_RDY(port) | STAT_OVR_ERR | STAT_FRM_ERR |
			STAT_BRK_DET))
		mvebu_uart_rx_chars(port, st);

	return IRQ_HANDLED;
}

static irqreturn_t mvebu_uart_tx_isr(int irq, void *dev_id)
{
	struct uart_port *port = (struct uart_port *)dev_id;
	unsigned int st = readl(port->membase + UART_STAT);

	if (st & STAT_TX_RDY(port))
		mvebu_uart_tx_chars(port, st);

	return IRQ_HANDLED;
}

static int mvebu_uart_startup(struct uart_port *port)
{
	struct mvebu_uart *mvuart = to_mvuart(port);
	unsigned int ctl;
	int ret;

	writel(CTRL_TXFIFO_RST | CTRL_RXFIFO_RST,
	       port->membase + UART_CTRL(port));
	udelay(1);

	/* Clear the error bits of state register before IRQ request */
	ret = readl(port->membase + UART_STAT);
	ret |= STAT_BRK_ERR;
	writel(ret, port->membase + UART_STAT);

	writel(CTRL_BRK_INT, port->membase + UART_CTRL(port));

	ctl = readl(port->membase + UART_INTR(port));
	ctl |= CTRL_RX_RDY_INT(port);
	writel(ctl, port->membase + UART_INTR(port));

	if (!mvuart->irq[UART_TX_IRQ]) {
		/* Old bindings with just one interrupt (UART0 only) */
		ret = devm_request_irq(port->dev, mvuart->irq[UART_IRQ_SUM],
				       mvebu_uart_isr, port->irqflags,
				       dev_name(port->dev), port);
		if (ret) {
			dev_err(port->dev, "unable to request IRQ %d\n",
				mvuart->irq[UART_IRQ_SUM]);
			return ret;
		}
	} else {
		/* New bindings with an IRQ for RX and TX (both UART) */
		ret = devm_request_irq(port->dev, mvuart->irq[UART_RX_IRQ],
				       mvebu_uart_rx_isr, port->irqflags,
				       dev_name(port->dev), port);
		if (ret) {
			dev_err(port->dev, "unable to request IRQ %d\n",
				mvuart->irq[UART_RX_IRQ]);
			return ret;
		}

		ret = devm_request_irq(port->dev, mvuart->irq[UART_TX_IRQ],
				       mvebu_uart_tx_isr, port->irqflags,
				       dev_name(port->dev),
				       port);
		if (ret) {
			dev_err(port->dev, "unable to request IRQ %d\n",
				mvuart->irq[UART_TX_IRQ]);
			devm_free_irq(port->dev, mvuart->irq[UART_RX_IRQ],
				      port);
			return ret;
		}
	}

	return 0;
}

static void mvebu_uart_shutdown(struct uart_port *port)
{
	struct mvebu_uart *mvuart = to_mvuart(port);

	writel(0, port->membase + UART_INTR(port));

	if (!mvuart->irq[UART_TX_IRQ]) {
		devm_free_irq(port->dev, mvuart->irq[UART_IRQ_SUM], port);
	} else {
		devm_free_irq(port->dev, mvuart->irq[UART_RX_IRQ], port);
		devm_free_irq(port->dev, mvuart->irq[UART_TX_IRQ], port);
	}
}

static unsigned int mvebu_uart_baud_rate_set(struct uart_port *port, unsigned int baud)
{
	unsigned int d_divisor, m_divisor;
	unsigned long flags;
	u32 brdv, osamp;

	if (!port->uartclk)
		return 0;

	/*
	 * The baudrate is derived from the UART clock thanks to divisors:
	 *   > d1 * d2 ("TBG divisors"): can divide only TBG clock from 1 to 6
	 *   > D ("baud generator"): can divide the clock from 1 to 1023
	 *   > M ("fractional divisor"): allows a better accuracy (from 1 to 63)
	 *
	 * Exact formulas for calculating baudrate:
	 *
	 * with default x16 scheme:
	 *   baudrate = xtal / (d * 16)
	 *   baudrate = tbg / (d1 * d2 * d * 16)
	 *
	 * with fractional divisor:
	 *   baudrate = 10 * xtal / (d * (3 * (m1 + m2) + 2 * (m3 + m4)))
	 *   baudrate = 10 * tbg / (d1*d2 * d * (3 * (m1 + m2) + 2 * (m3 + m4)))
	 *
	 * Oversampling value:
	 *   osamp = (m1 << 0) | (m2 << 8) | (m3 << 16) | (m4 << 24);
	 *
	 * Where m1 controls number of clock cycles per bit for bits 1,2,3;
	 * m2 for bits 4,5,6; m3 for bits 7,8 and m4 for bits 9,10.
	 *
	 * To simplify baudrate setup set all the M prescalers to the same
	 * value. For baudrates 9600 Bd and higher, it is enough to use the
	 * default (x16) divisor or fractional divisor with M = 63, so there
	 * is no need to use real fractional support (where the M prescalers
	 * are not equal).
	 *
	 * When all the M prescalers are zeroed then default (x16) divisor is
	 * used. Default x16 scheme is more stable than M (fractional divisor),
	 * so use M only when D divisor is not enough to derive baudrate.
	 *
	 * Member port->uartclk is either xtal clock rate or TBG clock rate
	 * divided by (d1 * d2). So d1 and d2 are already set by the UART clock
	 * driver (and UART driver itself cannot change them). Moreover they are
	 * shared between both UARTs.
	 */

	m_divisor = OSAMP_DEFAULT_DIVISOR;
	d_divisor = DIV_ROUND_CLOSEST(port->uartclk, baud * m_divisor);

	if (d_divisor > BRDV_BAUD_MAX) {
		/*
		 * Experiments show that small M divisors are unstable.
		 * Use maximal possible M = 63 and calculate D divisor.
		 */
		m_divisor = OSAMP_MAX_DIVISOR;
		d_divisor = DIV_ROUND_CLOSEST(port->uartclk, baud * m_divisor);
	}

	if (d_divisor < 1)
		d_divisor = 1;
	else if (d_divisor > BRDV_BAUD_MAX)
		d_divisor = BRDV_BAUD_MAX;

	spin_lock_irqsave(&mvebu_uart_lock, flags);
	brdv = readl(port->membase + UART_BRDV);
	brdv &= ~BRDV_BAUD_MASK;
	brdv |= d_divisor;
	writel(brdv, port->membase + UART_BRDV);
	spin_unlock_irqrestore(&mvebu_uart_lock, flags);

	osamp = readl(port->membase + UART_OSAMP);
	osamp &= ~OSAMP_DIVISORS_MASK;
	if (m_divisor != OSAMP_DEFAULT_DIVISOR)
		osamp |= (m_divisor << 0) | (m_divisor << 8) |
			(m_divisor << 16) | (m_divisor << 24);
	writel(osamp, port->membase + UART_OSAMP);

	return DIV_ROUND_CLOSEST(port->uartclk, d_divisor * m_divisor);
}

static void mvebu_uart_set_termios(struct uart_port *port,
				   struct ktermios *termios,
				   const struct ktermios *old)
{
	unsigned long flags;
	unsigned int baud, min_baud, max_baud;

	spin_lock_irqsave(&port->lock, flags);

	port->read_status_mask = STAT_RX_RDY(port) | STAT_OVR_ERR |
		STAT_TX_RDY(port) | STAT_TX_FIFO_FUL;

	if (termios->c_iflag & INPCK)
		port->read_status_mask |= STAT_FRM_ERR | STAT_PAR_ERR;

	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |=
			STAT_FRM_ERR | STAT_PAR_ERR | STAT_OVR_ERR;

	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= STAT_RX_RDY(port) | STAT_BRK_ERR;

	/*
	 * Maximal divisor is 1023 and maximal fractional divisor is 63. And
	 * experiments show that baudrates above 1/80 of parent clock rate are
	 * not stable. So disallow baudrates above 1/80 of the parent clock
	 * rate. If port->uartclk is not available, then
	 * mvebu_uart_baud_rate_set() fails, so values min_baud and max_baud
	 * in this case do not matter.
	 */
	min_baud = DIV_ROUND_UP(port->uartclk, BRDV_BAUD_MAX *
				OSAMP_MAX_DIVISOR);
	max_baud = port->uartclk / 80;

	baud = uart_get_baud_rate(port, termios, old, min_baud, max_baud);
	baud = mvebu_uart_baud_rate_set(port, baud);

	/* In case baudrate cannot be changed, report previous old value */
	if (baud == 0 && old)
		baud = tty_termios_baud_rate(old);

	/* Only the following flag changes are supported */
	if (old) {
		termios->c_iflag &= INPCK | IGNPAR;
		termios->c_iflag |= old->c_iflag & ~(INPCK | IGNPAR);
		termios->c_cflag &= CREAD | CBAUD;
		termios->c_cflag |= old->c_cflag & ~(CREAD | CBAUD);
		termios->c_cflag |= CS8;
	}

	if (baud != 0) {
		tty_termios_encode_baud_rate(termios, baud, baud);
		uart_update_timeout(port, termios->c_cflag, baud);
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *mvebu_uart_type(struct uart_port *port)
{
	return MVEBU_UART_TYPE;
}

static void mvebu_uart_release_port(struct uart_port *port)
{
	/* Nothing to do here */
}

static int mvebu_uart_request_port(struct uart_port *port)
{
	return 0;
}

#ifdef CONFIG_CONSOLE_POLL
static int mvebu_uart_get_poll_char(struct uart_port *port)
{
	unsigned int st = readl(port->membase + UART_STAT);

	if (!(st & STAT_RX_RDY(port)))
		return NO_POLL_CHAR;

	return readl(port->membase + UART_RBR(port));
}

static void mvebu_uart_put_poll_char(struct uart_port *port, unsigned char c)
{
	unsigned int st;

	for (;;) {
		st = readl(port->membase + UART_STAT);

		if (!(st & STAT_TX_FIFO_FUL))
			break;

		udelay(1);
	}

	writel(c, port->membase + UART_TSH(port));
}
#endif

static const struct uart_ops mvebu_uart_ops = {
	.tx_empty	= mvebu_uart_tx_empty,
	.set_mctrl	= mvebu_uart_set_mctrl,
	.get_mctrl	= mvebu_uart_get_mctrl,
	.stop_tx	= mvebu_uart_stop_tx,
	.start_tx	= mvebu_uart_start_tx,
	.stop_rx	= mvebu_uart_stop_rx,
	.break_ctl	= mvebu_uart_break_ctl,
	.startup	= mvebu_uart_startup,
	.shutdown	= mvebu_uart_shutdown,
	.set_termios	= mvebu_uart_set_termios,
	.type		= mvebu_uart_type,
	.release_port	= mvebu_uart_release_port,
	.request_port	= mvebu_uart_request_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char	= mvebu_uart_get_poll_char,
	.poll_put_char	= mvebu_uart_put_poll_char,
#endif
};

/* Console Driver Operations  */

#ifdef CONFIG_SERIAL_MVEBU_CONSOLE
/* Early Console */
static void mvebu_uart_putc(struct uart_port *port, unsigned char c)
{
	unsigned int st;

	for (;;) {
		st = readl(port->membase + UART_STAT);
		if (!(st & STAT_TX_FIFO_FUL))
			break;
	}

	/* At early stage, DT is not parsed yet, only use UART0 */
	writel(c, port->membase + UART_STD_TSH);

	for (;;) {
		st = readl(port->membase + UART_STAT);
		if (st & STAT_TX_FIFO_EMP)
			break;
	}
}

static void mvebu_uart_putc_early_write(struct console *con,
					const char *s,
					unsigned int n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, mvebu_uart_putc);
}

static int __init
mvebu_uart_early_console_setup(struct earlycon_device *device,
			       const char *opt)
{
	if (!device->port.membase)
		return -ENODEV;

	device->con->write = mvebu_uart_putc_early_write;

	return 0;
}

EARLYCON_DECLARE(ar3700_uart, mvebu_uart_early_console_setup);
OF_EARLYCON_DECLARE(ar3700_uart, "marvell,armada-3700-uart",
		    mvebu_uart_early_console_setup);

static void wait_for_xmitr(struct uart_port *port)
{
	u32 val;

	readl_poll_timeout_atomic(port->membase + UART_STAT, val,
				  (val & STAT_TX_RDY(port)), 1, 10000);
}

static void wait_for_xmite(struct uart_port *port)
{
	u32 val;

	readl_poll_timeout_atomic(port->membase + UART_STAT, val,
				  (val & STAT_TX_EMP), 1, 10000);
}

static void mvebu_uart_console_putchar(struct uart_port *port, unsigned char ch)
{
	wait_for_xmitr(port);
	writel(ch, port->membase + UART_TSH(port));
}

static void mvebu_uart_console_write(struct console *co, const char *s,
				     unsigned int count)
{
	struct uart_port *port = &mvebu_uart_ports[co->index];
	unsigned long flags;
	unsigned int ier, intr, ctl;
	int locked = 1;

	if (oops_in_progress)
		locked = spin_trylock_irqsave(&port->lock, flags);
	else
		spin_lock_irqsave(&port->lock, flags);

	ier = readl(port->membase + UART_CTRL(port)) & CTRL_BRK_INT;
	intr = readl(port->membase + UART_INTR(port)) &
		(CTRL_RX_RDY_INT(port) | CTRL_TX_RDY_INT(port));
	writel(0, port->membase + UART_CTRL(port));
	writel(0, port->membase + UART_INTR(port));

	uart_console_write(port, s, count, mvebu_uart_console_putchar);

	wait_for_xmite(port);

	if (ier)
		writel(ier, port->membase + UART_CTRL(port));

	if (intr) {
		ctl = intr | readl(port->membase + UART_INTR(port));
		writel(ctl, port->membase + UART_INTR(port));
	}

	if (locked)
		spin_unlock_irqrestore(&port->lock, flags);
}

static int mvebu_uart_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= MVEBU_NR_UARTS)
		return -EINVAL;

	port = &mvebu_uart_ports[co->index];

	if (!port->mapbase || !port->membase) {
		pr_debug("console on ttyMV%i not present\n", co->index);
		return -ENODEV;
	}

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver mvebu_uart_driver;

static struct console mvebu_uart_console = {
	.name	= "ttyMV",
	.write	= mvebu_uart_console_write,
	.device	= uart_console_device,
	.setup	= mvebu_uart_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
	.data	= &mvebu_uart_driver,
};

static int __init mvebu_uart_console_init(void)
{
	register_console(&mvebu_uart_console);
	return 0;
}

console_initcall(mvebu_uart_console_init);


#endif /* CONFIG_SERIAL_MVEBU_CONSOLE */

static struct uart_driver mvebu_uart_driver = {
	.owner			= THIS_MODULE,
	.driver_name		= DRIVER_NAME,
	.dev_name		= "ttyMV",
	.nr			= MVEBU_NR_UARTS,
#ifdef CONFIG_SERIAL_MVEBU_CONSOLE
	.cons			= &mvebu_uart_console,
#endif
};

#if defined(CONFIG_PM)
static int mvebu_uart_suspend(struct device *dev)
{
	struct mvebu_uart *mvuart = dev_get_drvdata(dev);
	struct uart_port *port = mvuart->port;
	unsigned long flags;

	uart_suspend_port(&mvebu_uart_driver, port);

	mvuart->pm_regs.rbr = readl(port->membase + UART_RBR(port));
	mvuart->pm_regs.tsh = readl(port->membase + UART_TSH(port));
	mvuart->pm_regs.ctrl = readl(port->membase + UART_CTRL(port));
	mvuart->pm_regs.intr = readl(port->membase + UART_INTR(port));
	mvuart->pm_regs.stat = readl(port->membase + UART_STAT);
	spin_lock_irqsave(&mvebu_uart_lock, flags);
	mvuart->pm_regs.brdv = readl(port->membase + UART_BRDV);
	spin_unlock_irqrestore(&mvebu_uart_lock, flags);
	mvuart->pm_regs.osamp = readl(port->membase + UART_OSAMP);

	device_set_wakeup_enable(dev, true);

	return 0;
}

static int mvebu_uart_resume(struct device *dev)
{
	struct mvebu_uart *mvuart = dev_get_drvdata(dev);
	struct uart_port *port = mvuart->port;
	unsigned long flags;

	writel(mvuart->pm_regs.rbr, port->membase + UART_RBR(port));
	writel(mvuart->pm_regs.tsh, port->membase + UART_TSH(port));
	writel(mvuart->pm_regs.ctrl, port->membase + UART_CTRL(port));
	writel(mvuart->pm_regs.intr, port->membase + UART_INTR(port));
	writel(mvuart->pm_regs.stat, port->membase + UART_STAT);
	spin_lock_irqsave(&mvebu_uart_lock, flags);
	writel(mvuart->pm_regs.brdv, port->membase + UART_BRDV);
	spin_unlock_irqrestore(&mvebu_uart_lock, flags);
	writel(mvuart->pm_regs.osamp, port->membase + UART_OSAMP);

	uart_resume_port(&mvebu_uart_driver, port);

	return 0;
}

static const struct dev_pm_ops mvebu_uart_pm_ops = {
	.suspend        = mvebu_uart_suspend,
	.resume         = mvebu_uart_resume,
};
#endif /* CONFIG_PM */

static const struct of_device_id mvebu_uart_of_match[];

/* Counter to keep track of each UART port id when not using CONFIG_OF */
static int uart_num_counter;

static int mvebu_uart_probe(struct platform_device *pdev)
{
	const struct of_device_id *match = of_match_device(mvebu_uart_of_match,
							   &pdev->dev);
	struct uart_port *port;
	struct mvebu_uart *mvuart;
	struct resource *reg;
	int id, irq;

	/* Assume that all UART ports have a DT alias or none has */
	id = of_alias_get_id(pdev->dev.of_node, "serial");
	if (!pdev->dev.of_node || id < 0)
		pdev->id = uart_num_counter++;
	else
		pdev->id = id;

	if (pdev->id >= MVEBU_NR_UARTS) {
		dev_err(&pdev->dev, "cannot have more than %d UART ports\n",
			MVEBU_NR_UARTS);
		return -EINVAL;
	}

	port = &mvebu_uart_ports[pdev->id];

	spin_lock_init(&port->lock);

	port->dev        = &pdev->dev;
	port->type       = PORT_MVEBU;
	port->ops        = &mvebu_uart_ops;
	port->regshift   = 0;

	port->fifosize   = 32;
	port->iotype     = UPIO_MEM32;
	port->flags      = UPF_FIXED_PORT;
	port->line       = pdev->id;

	/*
	 * IRQ number is not stored in this structure because we may have two of
	 * them per port (RX and TX). Instead, use the driver UART structure
	 * array so called ->irq[].
	 */
	port->irq        = 0;
	port->irqflags   = 0;

	port->membase = devm_platform_get_and_ioremap_resource(pdev, 0, &reg);
	if (IS_ERR(port->membase))
		return PTR_ERR(port->membase);
	port->mapbase    = reg->start;

	mvuart = devm_kzalloc(&pdev->dev, sizeof(struct mvebu_uart),
			      GFP_KERNEL);
	if (!mvuart)
		return -ENOMEM;

	/* Get controller data depending on the compatible string */
	mvuart->data = (struct mvebu_uart_driver_data *)match->data;
	mvuart->port = port;

	port->private_data = mvuart;
	platform_set_drvdata(pdev, mvuart);

	/* Get fixed clock frequency */
	mvuart->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(mvuart->clk)) {
		if (PTR_ERR(mvuart->clk) == -EPROBE_DEFER)
			return PTR_ERR(mvuart->clk);

		if (IS_EXTENDED(port)) {
			dev_err(&pdev->dev, "unable to get UART clock\n");
			return PTR_ERR(mvuart->clk);
		}
	} else {
		if (!clk_prepare_enable(mvuart->clk))
			port->uartclk = clk_get_rate(mvuart->clk);
	}

	/* Manage interrupts */
	if (platform_irq_count(pdev) == 1) {
		/* Old bindings: no name on the single unamed UART0 IRQ */
		irq = platform_get_irq(pdev, 0);
		if (irq < 0)
			return irq;

		mvuart->irq[UART_IRQ_SUM] = irq;
	} else {
		/*
		 * New bindings: named interrupts (RX, TX) for both UARTS,
		 * only make use of uart-rx and uart-tx interrupts, do not use
		 * uart-sum of UART0 port.
		 */
		irq = platform_get_irq_byname(pdev, "uart-rx");
		if (irq < 0)
			return irq;

		mvuart->irq[UART_RX_IRQ] = irq;

		irq = platform_get_irq_byname(pdev, "uart-tx");
		if (irq < 0)
			return irq;

		mvuart->irq[UART_TX_IRQ] = irq;
	}

	/* UART Soft Reset*/
	writel(CTRL_SOFT_RST, port->membase + UART_CTRL(port));
	udelay(1);
	writel(0, port->membase + UART_CTRL(port));

	return uart_add_one_port(&mvebu_uart_driver, port);
}

static struct mvebu_uart_driver_data uart_std_driver_data = {
	.is_ext = false,
	.regs.rbr = UART_STD_RBR,
	.regs.tsh = UART_STD_TSH,
	.regs.ctrl = UART_STD_CTRL1,
	.regs.intr = UART_STD_CTRL2,
	.flags.ctrl_tx_rdy_int = CTRL_STD_TX_RDY_INT,
	.flags.ctrl_rx_rdy_int = CTRL_STD_RX_RDY_INT,
	.flags.stat_tx_rdy = STAT_STD_TX_RDY,
	.flags.stat_rx_rdy = STAT_STD_RX_RDY,
};

static struct mvebu_uart_driver_data uart_ext_driver_data = {
	.is_ext = true,
	.regs.rbr = UART_EXT_RBR,
	.regs.tsh = UART_EXT_TSH,
	.regs.ctrl = UART_EXT_CTRL1,
	.regs.intr = UART_EXT_CTRL2,
	.flags.ctrl_tx_rdy_int = CTRL_EXT_TX_RDY_INT,
	.flags.ctrl_rx_rdy_int = CTRL_EXT_RX_RDY_INT,
	.flags.stat_tx_rdy = STAT_EXT_TX_RDY,
	.flags.stat_rx_rdy = STAT_EXT_RX_RDY,
};

/* Match table for of_platform binding */
static const struct of_device_id mvebu_uart_of_match[] = {
	{
		.compatible = "marvell,armada-3700-uart",
		.data = (void *)&uart_std_driver_data,
	},
	{
		.compatible = "marvell,armada-3700-uart-ext",
		.data = (void *)&uart_ext_driver_data,
	},
	{}
};

static struct platform_driver mvebu_uart_platform_driver = {
	.probe	= mvebu_uart_probe,
	.driver	= {
		.name  = "mvebu-uart",
		.of_match_table = of_match_ptr(mvebu_uart_of_match),
		.suppress_bind_attrs = true,
#if defined(CONFIG_PM)
		.pm	= &mvebu_uart_pm_ops,
#endif /* CONFIG_PM */
	},
};

/* This code is based on clk-fixed-factor.c driver and modified. */

struct mvebu_uart_clock {
	struct clk_hw clk_hw;
	int clock_idx;
	u32 pm_context_reg1;
	u32 pm_context_reg2;
};

struct mvebu_uart_clock_base {
	struct mvebu_uart_clock clocks[2];
	unsigned int parent_rates[5];
	int parent_idx;
	unsigned int div;
	void __iomem *reg1;
	void __iomem *reg2;
	bool configured;
};

#define PARENT_CLOCK_XTAL 4

#define to_uart_clock(hw) container_of(hw, struct mvebu_uart_clock, clk_hw)
#define to_uart_clock_base(uart_clock) container_of(uart_clock, \
	struct mvebu_uart_clock_base, clocks[uart_clock->clock_idx])

static int mvebu_uart_clock_prepare(struct clk_hw *hw)
{
	struct mvebu_uart_clock *uart_clock = to_uart_clock(hw);
	struct mvebu_uart_clock_base *uart_clock_base =
						to_uart_clock_base(uart_clock);
	unsigned int prev_clock_idx, prev_clock_rate, prev_d1d2;
	unsigned int parent_clock_idx, parent_clock_rate;
	unsigned long flags;
	unsigned int d1, d2;
	u64 divisor;
	u32 val;

	/*
	 * This function just reconfigures UART Clock Control register (located
	 * in UART1 address space which controls both UART1 and UART2) to
	 * selected UART base clock and recalculates current UART1/UART2
	 * divisors in their address spaces, so that final baudrate will not be
	 * changed by switching UART parent clock. This is required for
	 * otherwise kernel's boot log stops working - we need to ensure that
	 * UART baudrate does not change during this setup. It is a one time
	 * operation, it will execute only once and set `configured` to true,
	 * and be skipped on subsequent calls. Because this UART Clock Control
	 * register (UART_BRDV) is shared between UART1 baudrate function,
	 * UART1 clock selector and UART2 clock selector, every access to
	 * UART_BRDV (reg1) needs to be protected by a lock.
	 */

	spin_lock_irqsave(&mvebu_uart_lock, flags);

	if (uart_clock_base->configured) {
		spin_unlock_irqrestore(&mvebu_uart_lock, flags);
		return 0;
	}

	parent_clock_idx = uart_clock_base->parent_idx;
	parent_clock_rate = uart_clock_base->parent_rates[parent_clock_idx];

	val = readl(uart_clock_base->reg1);

	if (uart_clock_base->div > CLK_TBG_DIV1_MAX) {
		d1 = CLK_TBG_DIV1_MAX;
		d2 = uart_clock_base->div / CLK_TBG_DIV1_MAX;
	} else {
		d1 = uart_clock_base->div;
		d2 = 1;
	}

	if (val & CLK_NO_XTAL) {
		prev_clock_idx = (val >> CLK_TBG_SEL_SHIFT) & CLK_TBG_SEL_MASK;
		prev_d1d2 = ((val >> CLK_TBG_DIV1_SHIFT) & CLK_TBG_DIV1_MASK) *
			    ((val >> CLK_TBG_DIV2_SHIFT) & CLK_TBG_DIV2_MASK);
	} else {
		prev_clock_idx = PARENT_CLOCK_XTAL;
		prev_d1d2 = 1;
	}

	/* Note that uart_clock_base->parent_rates[i] may not be available */
	prev_clock_rate = uart_clock_base->parent_rates[prev_clock_idx];

	/* Recalculate UART1 divisor so UART1 baudrate does not change */
	if (prev_clock_rate) {
		divisor = DIV_U64_ROUND_CLOSEST((u64)(val & BRDV_BAUD_MASK) *
						parent_clock_rate * prev_d1d2,
						prev_clock_rate * d1 * d2);
		if (divisor < 1)
			divisor = 1;
		else if (divisor > BRDV_BAUD_MAX)
			divisor = BRDV_BAUD_MAX;
		val = (val & ~BRDV_BAUD_MASK) | divisor;
	}

	if (parent_clock_idx != PARENT_CLOCK_XTAL) {
		/* Do not use XTAL, select TBG clock and TBG d1 * d2 divisors */
		val |= CLK_NO_XTAL;
		val &= ~(CLK_TBG_DIV1_MASK << CLK_TBG_DIV1_SHIFT);
		val |= d1 << CLK_TBG_DIV1_SHIFT;
		val &= ~(CLK_TBG_DIV2_MASK << CLK_TBG_DIV2_SHIFT);
		val |= d2 << CLK_TBG_DIV2_SHIFT;
		val &= ~(CLK_TBG_SEL_MASK << CLK_TBG_SEL_SHIFT);
		val |= parent_clock_idx << CLK_TBG_SEL_SHIFT;
	} else {
		/* Use XTAL, TBG bits are then ignored */
		val &= ~CLK_NO_XTAL;
	}

	writel(val, uart_clock_base->reg1);

	/* Recalculate UART2 divisor so UART2 baudrate does not change */
	if (prev_clock_rate) {
		val = readl(uart_clock_base->reg2);
		divisor = DIV_U64_ROUND_CLOSEST((u64)(val & BRDV_BAUD_MASK) *
						parent_clock_rate * prev_d1d2,
						prev_clock_rate * d1 * d2);
		if (divisor < 1)
			divisor = 1;
		else if (divisor > BRDV_BAUD_MAX)
			divisor = BRDV_BAUD_MAX;
		val = (val & ~BRDV_BAUD_MASK) | divisor;
		writel(val, uart_clock_base->reg2);
	}

	uart_clock_base->configured = true;

	spin_unlock_irqrestore(&mvebu_uart_lock, flags);

	return 0;
}

static int mvebu_uart_clock_enable(struct clk_hw *hw)
{
	struct mvebu_uart_clock *uart_clock = to_uart_clock(hw);
	struct mvebu_uart_clock_base *uart_clock_base =
						to_uart_clock_base(uart_clock);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&mvebu_uart_lock, flags);

	val = readl(uart_clock_base->reg1);

	if (uart_clock->clock_idx == 0)
		val &= ~UART1_CLK_DIS;
	else
		val &= ~UART2_CLK_DIS;

	writel(val, uart_clock_base->reg1);

	spin_unlock_irqrestore(&mvebu_uart_lock, flags);

	return 0;
}

static void mvebu_uart_clock_disable(struct clk_hw *hw)
{
	struct mvebu_uart_clock *uart_clock = to_uart_clock(hw);
	struct mvebu_uart_clock_base *uart_clock_base =
						to_uart_clock_base(uart_clock);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&mvebu_uart_lock, flags);

	val = readl(uart_clock_base->reg1);

	if (uart_clock->clock_idx == 0)
		val |= UART1_CLK_DIS;
	else
		val |= UART2_CLK_DIS;

	writel(val, uart_clock_base->reg1);

	spin_unlock_irqrestore(&mvebu_uart_lock, flags);
}

static int mvebu_uart_clock_is_enabled(struct clk_hw *hw)
{
	struct mvebu_uart_clock *uart_clock = to_uart_clock(hw);
	struct mvebu_uart_clock_base *uart_clock_base =
						to_uart_clock_base(uart_clock);
	u32 val;

	val = readl(uart_clock_base->reg1);

	if (uart_clock->clock_idx == 0)
		return !(val & UART1_CLK_DIS);
	else
		return !(val & UART2_CLK_DIS);
}

static int mvebu_uart_clock_save_context(struct clk_hw *hw)
{
	struct mvebu_uart_clock *uart_clock = to_uart_clock(hw);
	struct mvebu_uart_clock_base *uart_clock_base =
						to_uart_clock_base(uart_clock);
	unsigned long flags;

	spin_lock_irqsave(&mvebu_uart_lock, flags);
	uart_clock->pm_context_reg1 = readl(uart_clock_base->reg1);
	uart_clock->pm_context_reg2 = readl(uart_clock_base->reg2);
	spin_unlock_irqrestore(&mvebu_uart_lock, flags);

	return 0;
}

static void mvebu_uart_clock_restore_context(struct clk_hw *hw)
{
	struct mvebu_uart_clock *uart_clock = to_uart_clock(hw);
	struct mvebu_uart_clock_base *uart_clock_base =
						to_uart_clock_base(uart_clock);
	unsigned long flags;

	spin_lock_irqsave(&mvebu_uart_lock, flags);
	writel(uart_clock->pm_context_reg1, uart_clock_base->reg1);
	writel(uart_clock->pm_context_reg2, uart_clock_base->reg2);
	spin_unlock_irqrestore(&mvebu_uart_lock, flags);
}

static unsigned long mvebu_uart_clock_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct mvebu_uart_clock *uart_clock = to_uart_clock(hw);
	struct mvebu_uart_clock_base *uart_clock_base =
						to_uart_clock_base(uart_clock);

	return parent_rate / uart_clock_base->div;
}

static long mvebu_uart_clock_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	struct mvebu_uart_clock *uart_clock = to_uart_clock(hw);
	struct mvebu_uart_clock_base *uart_clock_base =
						to_uart_clock_base(uart_clock);

	return *parent_rate / uart_clock_base->div;
}

static int mvebu_uart_clock_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	/*
	 * We must report success but we can do so unconditionally because
	 * mvebu_uart_clock_round_rate returns values that ensure this call is a
	 * nop.
	 */

	return 0;
}

static const struct clk_ops mvebu_uart_clock_ops = {
	.prepare = mvebu_uart_clock_prepare,
	.enable = mvebu_uart_clock_enable,
	.disable = mvebu_uart_clock_disable,
	.is_enabled = mvebu_uart_clock_is_enabled,
	.save_context = mvebu_uart_clock_save_context,
	.restore_context = mvebu_uart_clock_restore_context,
	.round_rate = mvebu_uart_clock_round_rate,
	.set_rate = mvebu_uart_clock_set_rate,
	.recalc_rate = mvebu_uart_clock_recalc_rate,
};

static int mvebu_uart_clock_register(struct device *dev,
				     struct mvebu_uart_clock *uart_clock,
				     const char *name,
				     const char *parent_name)
{
	struct clk_init_data init = { };

	uart_clock->clk_hw.init = &init;

	init.name = name;
	init.ops = &mvebu_uart_clock_ops;
	init.flags = 0;
	init.num_parents = 1;
	init.parent_names = &parent_name;

	return devm_clk_hw_register(dev, &uart_clock->clk_hw);
}

static int mvebu_uart_clock_probe(struct platform_device *pdev)
{
	static const char *const uart_clk_names[] = { "uart_1", "uart_2" };
	static const char *const parent_clk_names[] = { "TBG-A-P", "TBG-B-P",
							"TBG-A-S", "TBG-B-S",
							"xtal" };
	struct clk *parent_clks[ARRAY_SIZE(parent_clk_names)];
	struct mvebu_uart_clock_base *uart_clock_base;
	struct clk_hw_onecell_data *hw_clk_data;
	struct device *dev = &pdev->dev;
	int i, parent_clk_idx, ret;
	unsigned long div, rate;
	struct resource *res;
	unsigned int d1, d2;

	BUILD_BUG_ON(ARRAY_SIZE(uart_clk_names) !=
		     ARRAY_SIZE(uart_clock_base->clocks));
	BUILD_BUG_ON(ARRAY_SIZE(parent_clk_names) !=
		     ARRAY_SIZE(uart_clock_base->parent_rates));

	uart_clock_base = devm_kzalloc(dev,
				       sizeof(*uart_clock_base),
				       GFP_KERNEL);
	if (!uart_clock_base)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Couldn't get first register\n");
		return -ENOENT;
	}

	/*
	 * UART Clock Control register (reg1 / UART_BRDV) is in the address
	 * space of UART1 (standard UART variant), controls parent clock and
	 * dividers for both UART1 and UART2 and is supplied via DT as the first
	 * resource. Therefore use ioremap() rather than ioremap_resource() to
	 * avoid conflicts with UART1 driver. Access to UART_BRDV is protected
	 * by a lock shared between clock and UART driver.
	 */
	uart_clock_base->reg1 = devm_ioremap(dev, res->start,
					     resource_size(res));
	if (!uart_clock_base->reg1)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(dev, "Couldn't get second register\n");
		return -ENOENT;
	}

	/*
	 * UART 2 Baud Rate Divisor register (reg2 / UART_BRDV) is in address
	 * space of UART2 (extended UART variant), controls only one UART2
	 * specific divider and is supplied via DT as second resource.
	 * Therefore use ioremap() rather than ioremap_resource() to avoid
	 * conflicts with UART2 driver. Access to UART_BRDV is protected by a
	 * by lock shared between clock and UART driver.
	 */
	uart_clock_base->reg2 = devm_ioremap(dev, res->start,
					     resource_size(res));
	if (!uart_clock_base->reg2)
		return -ENOMEM;

	hw_clk_data = devm_kzalloc(dev,
				   struct_size(hw_clk_data, hws,
					       ARRAY_SIZE(uart_clk_names)),
				   GFP_KERNEL);
	if (!hw_clk_data)
		return -ENOMEM;

	hw_clk_data->num = ARRAY_SIZE(uart_clk_names);
	for (i = 0; i < ARRAY_SIZE(uart_clk_names); i++) {
		hw_clk_data->hws[i] = &uart_clock_base->clocks[i].clk_hw;
		uart_clock_base->clocks[i].clock_idx = i;
	}

	parent_clk_idx = -1;

	for (i = 0; i < ARRAY_SIZE(parent_clk_names); i++) {
		parent_clks[i] = devm_clk_get(dev, parent_clk_names[i]);
		if (IS_ERR(parent_clks[i])) {
			if (PTR_ERR(parent_clks[i]) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			dev_warn(dev, "Couldn't get the parent clock %s: %ld\n",
				 parent_clk_names[i], PTR_ERR(parent_clks[i]));
			continue;
		}

		ret = clk_prepare_enable(parent_clks[i]);
		if (ret) {
			dev_warn(dev, "Couldn't enable parent clock %s: %d\n",
				 parent_clk_names[i], ret);
			continue;
		}
		rate = clk_get_rate(parent_clks[i]);
		uart_clock_base->parent_rates[i] = rate;

		if (i != PARENT_CLOCK_XTAL) {
			/*
			 * Calculate the smallest TBG d1 and d2 divisors that
			 * still can provide 9600 baudrate.
			 */
			d1 = DIV_ROUND_UP(rate, 9600 * OSAMP_MAX_DIVISOR *
					  BRDV_BAUD_MAX);
			if (d1 < 1)
				d1 = 1;
			else if (d1 > CLK_TBG_DIV1_MAX)
				d1 = CLK_TBG_DIV1_MAX;

			d2 = DIV_ROUND_UP(rate, 9600 * OSAMP_MAX_DIVISOR *
					  BRDV_BAUD_MAX * d1);
			if (d2 < 1)
				d2 = 1;
			else if (d2 > CLK_TBG_DIV2_MAX)
				d2 = CLK_TBG_DIV2_MAX;
		} else {
			/*
			 * When UART clock uses XTAL clock as a source then it
			 * is not possible to use d1 and d2 divisors.
			 */
			d1 = d2 = 1;
		}

		/* Skip clock source which cannot provide 9600 baudrate */
		if (rate > 9600 * OSAMP_MAX_DIVISOR * BRDV_BAUD_MAX * d1 * d2)
			continue;

		/*
		 * Choose TBG clock source with the smallest divisors. Use XTAL
		 * clock source only in case TBG is not available as XTAL cannot
		 * be used for baudrates higher than 230400.
		 */
		if (parent_clk_idx == -1 ||
		    (i != PARENT_CLOCK_XTAL && div > d1 * d2)) {
			parent_clk_idx = i;
			div = d1 * d2;
		}
	}

	for (i = 0; i < ARRAY_SIZE(parent_clk_names); i++) {
		if (i == parent_clk_idx || IS_ERR(parent_clks[i]))
			continue;
		clk_disable_unprepare(parent_clks[i]);
		devm_clk_put(dev, parent_clks[i]);
	}

	if (parent_clk_idx == -1) {
		dev_err(dev, "No usable parent clock\n");
		return -ENOENT;
	}

	uart_clock_base->parent_idx = parent_clk_idx;
	uart_clock_base->div = div;

	dev_notice(dev, "Using parent clock %s as base UART clock\n",
		   __clk_get_name(parent_clks[parent_clk_idx]));

	for (i = 0; i < ARRAY_SIZE(uart_clk_names); i++) {
		ret = mvebu_uart_clock_register(dev,
				&uart_clock_base->clocks[i],
				uart_clk_names[i],
				__clk_get_name(parent_clks[parent_clk_idx]));
		if (ret) {
			dev_err(dev, "Can't register UART clock %d: %d\n",
				i, ret);
			return ret;
		}
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					   hw_clk_data);
}

static const struct of_device_id mvebu_uart_clock_of_match[] = {
	{ .compatible = "marvell,armada-3700-uart-clock", },
	{ }
};

static struct platform_driver mvebu_uart_clock_platform_driver = {
	.probe = mvebu_uart_clock_probe,
	.driver		= {
		.name	= "mvebu-uart-clock",
		.of_match_table = mvebu_uart_clock_of_match,
	},
};

static int __init mvebu_uart_init(void)
{
	int ret;

	ret = uart_register_driver(&mvebu_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&mvebu_uart_clock_platform_driver);
	if (ret) {
		uart_unregister_driver(&mvebu_uart_driver);
		return ret;
	}

	ret = platform_driver_register(&mvebu_uart_platform_driver);
	if (ret) {
		platform_driver_unregister(&mvebu_uart_clock_platform_driver);
		uart_unregister_driver(&mvebu_uart_driver);
		return ret;
	}

	return 0;
}
arch_initcall(mvebu_uart_init);
