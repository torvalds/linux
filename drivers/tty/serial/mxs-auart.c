/*
 * Freescale STMP37XX/STMP378X Application UART driver
 *
 * Author: dmitry pervushin <dimka@embeddedalley.com>
 *
 * Copyright 2008-2010 Freescale Semiconductor, Inc.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>

#include <asm/cacheflush.h>

#define MXS_AUART_PORTS 5
#define MXS_AUART_FIFO_SIZE		16

#define AUART_CTRL0			0x00000000
#define AUART_CTRL0_SET			0x00000004
#define AUART_CTRL0_CLR			0x00000008
#define AUART_CTRL0_TOG			0x0000000c
#define AUART_CTRL1			0x00000010
#define AUART_CTRL1_SET			0x00000014
#define AUART_CTRL1_CLR			0x00000018
#define AUART_CTRL1_TOG			0x0000001c
#define AUART_CTRL2			0x00000020
#define AUART_CTRL2_SET			0x00000024
#define AUART_CTRL2_CLR			0x00000028
#define AUART_CTRL2_TOG			0x0000002c
#define AUART_LINECTRL			0x00000030
#define AUART_LINECTRL_SET		0x00000034
#define AUART_LINECTRL_CLR		0x00000038
#define AUART_LINECTRL_TOG		0x0000003c
#define AUART_LINECTRL2			0x00000040
#define AUART_LINECTRL2_SET		0x00000044
#define AUART_LINECTRL2_CLR		0x00000048
#define AUART_LINECTRL2_TOG		0x0000004c
#define AUART_INTR			0x00000050
#define AUART_INTR_SET			0x00000054
#define AUART_INTR_CLR			0x00000058
#define AUART_INTR_TOG			0x0000005c
#define AUART_DATA			0x00000060
#define AUART_STAT			0x00000070
#define AUART_DEBUG			0x00000080
#define AUART_VERSION			0x00000090
#define AUART_AUTOBAUD			0x000000a0

#define AUART_CTRL0_SFTRST			(1 << 31)
#define AUART_CTRL0_CLKGATE			(1 << 30)
#define AUART_CTRL0_RXTO_ENABLE			(1 << 27)
#define AUART_CTRL0_RXTIMEOUT(v)		(((v) & 0x7ff) << 16)
#define AUART_CTRL0_XFER_COUNT(v)		((v) & 0xffff)

#define AUART_CTRL1_XFER_COUNT(v)		((v) & 0xffff)

#define AUART_CTRL2_DMAONERR			(1 << 26)
#define AUART_CTRL2_TXDMAE			(1 << 25)
#define AUART_CTRL2_RXDMAE			(1 << 24)

#define AUART_CTRL2_CTSEN			(1 << 15)
#define AUART_CTRL2_RTSEN			(1 << 14)
#define AUART_CTRL2_RTS				(1 << 11)
#define AUART_CTRL2_RXE				(1 << 9)
#define AUART_CTRL2_TXE				(1 << 8)
#define AUART_CTRL2_UARTEN			(1 << 0)

#define AUART_LINECTRL_BAUD_DIVINT_SHIFT	16
#define AUART_LINECTRL_BAUD_DIVINT_MASK		0xffff0000
#define AUART_LINECTRL_BAUD_DIVINT(v)		(((v) & 0xffff) << 16)
#define AUART_LINECTRL_BAUD_DIVFRAC_SHIFT	8
#define AUART_LINECTRL_BAUD_DIVFRAC_MASK	0x00003f00
#define AUART_LINECTRL_BAUD_DIVFRAC(v)		(((v) & 0x3f) << 8)
#define AUART_LINECTRL_WLEN_MASK		0x00000060
#define AUART_LINECTRL_WLEN(v)			(((v) & 0x3) << 5)
#define AUART_LINECTRL_FEN			(1 << 4)
#define AUART_LINECTRL_STP2			(1 << 3)
#define AUART_LINECTRL_EPS			(1 << 2)
#define AUART_LINECTRL_PEN			(1 << 1)
#define AUART_LINECTRL_BRK			(1 << 0)

#define AUART_INTR_RTIEN			(1 << 22)
#define AUART_INTR_TXIEN			(1 << 21)
#define AUART_INTR_RXIEN			(1 << 20)
#define AUART_INTR_CTSMIEN			(1 << 17)
#define AUART_INTR_RTIS				(1 << 6)
#define AUART_INTR_TXIS				(1 << 5)
#define AUART_INTR_RXIS				(1 << 4)
#define AUART_INTR_CTSMIS			(1 << 1)

#define AUART_STAT_BUSY				(1 << 29)
#define AUART_STAT_CTS				(1 << 28)
#define AUART_STAT_TXFE				(1 << 27)
#define AUART_STAT_TXFF				(1 << 25)
#define AUART_STAT_RXFE				(1 << 24)
#define AUART_STAT_OERR				(1 << 19)
#define AUART_STAT_BERR				(1 << 18)
#define AUART_STAT_PERR				(1 << 17)
#define AUART_STAT_FERR				(1 << 16)
#define AUART_STAT_RXCOUNT_MASK			0xffff

static struct uart_driver auart_driver;

enum mxs_auart_type {
	IMX23_AUART,
	IMX28_AUART,
};

struct mxs_auart_port {
	struct uart_port port;

#define MXS_AUART_DMA_ENABLED	0x2
#define MXS_AUART_DMA_TX_SYNC	2  /* bit 2 */
#define MXS_AUART_DMA_RX_READY	3  /* bit 3 */
#define MXS_AUART_RTSCTS	4  /* bit 4 */
	unsigned long flags;
	unsigned int ctrl;
	enum mxs_auart_type devtype;

	unsigned int irq;

	struct clk *clk;
	struct device *dev;

	/* for DMA */
	struct scatterlist tx_sgl;
	struct dma_chan	*tx_dma_chan;
	void *tx_dma_buf;

	struct scatterlist rx_sgl;
	struct dma_chan	*rx_dma_chan;
	void *rx_dma_buf;
};

static struct platform_device_id mxs_auart_devtype[] = {
	{ .name = "mxs-auart-imx23", .driver_data = IMX23_AUART },
	{ .name = "mxs-auart-imx28", .driver_data = IMX28_AUART },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, mxs_auart_devtype);

static struct of_device_id mxs_auart_dt_ids[] = {
	{
		.compatible = "fsl,imx28-auart",
		.data = &mxs_auart_devtype[IMX28_AUART]
	}, {
		.compatible = "fsl,imx23-auart",
		.data = &mxs_auart_devtype[IMX23_AUART]
	}, { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_auart_dt_ids);

static inline int is_imx28_auart(struct mxs_auart_port *s)
{
	return s->devtype == IMX28_AUART;
}

static inline bool auart_dma_enabled(struct mxs_auart_port *s)
{
	return s->flags & MXS_AUART_DMA_ENABLED;
}

static void mxs_auart_stop_tx(struct uart_port *u);

#define to_auart_port(u) container_of(u, struct mxs_auart_port, port)

static void mxs_auart_tx_chars(struct mxs_auart_port *s);

static void dma_tx_callback(void *param)
{
	struct mxs_auart_port *s = param;
	struct circ_buf *xmit = &s->port.state->xmit;

	dma_unmap_sg(s->dev, &s->tx_sgl, 1, DMA_TO_DEVICE);

	/* clear the bit used to serialize the DMA tx. */
	clear_bit(MXS_AUART_DMA_TX_SYNC, &s->flags);
	smp_mb__after_atomic();

	/* wake up the possible processes. */
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&s->port);

	mxs_auart_tx_chars(s);
}

static int mxs_auart_dma_tx(struct mxs_auart_port *s, int size)
{
	struct dma_async_tx_descriptor *desc;
	struct scatterlist *sgl = &s->tx_sgl;
	struct dma_chan *channel = s->tx_dma_chan;
	u32 pio;

	/* [1] : send PIO. Note, the first pio word is CTRL1. */
	pio = AUART_CTRL1_XFER_COUNT(size);
	desc = dmaengine_prep_slave_sg(channel, (struct scatterlist *)&pio,
					1, DMA_TRANS_NONE, 0);
	if (!desc) {
		dev_err(s->dev, "step 1 error\n");
		return -EINVAL;
	}

	/* [2] : set DMA buffer. */
	sg_init_one(sgl, s->tx_dma_buf, size);
	dma_map_sg(s->dev, sgl, 1, DMA_TO_DEVICE);
	desc = dmaengine_prep_slave_sg(channel, sgl,
			1, DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		dev_err(s->dev, "step 2 error\n");
		return -EINVAL;
	}

	/* [3] : submit the DMA */
	desc->callback = dma_tx_callback;
	desc->callback_param = s;
	dmaengine_submit(desc);
	dma_async_issue_pending(channel);
	return 0;
}

static void mxs_auart_tx_chars(struct mxs_auart_port *s)
{
	struct circ_buf *xmit = &s->port.state->xmit;

	if (auart_dma_enabled(s)) {
		u32 i = 0;
		int size;
		void *buffer = s->tx_dma_buf;

		if (test_and_set_bit(MXS_AUART_DMA_TX_SYNC, &s->flags))
			return;

		while (!uart_circ_empty(xmit) && !uart_tx_stopped(&s->port)) {
			size = min_t(u32, UART_XMIT_SIZE - i,
				     CIRC_CNT_TO_END(xmit->head,
						     xmit->tail,
						     UART_XMIT_SIZE));
			memcpy(buffer + i, xmit->buf + xmit->tail, size);
			xmit->tail = (xmit->tail + size) & (UART_XMIT_SIZE - 1);

			i += size;
			if (i >= UART_XMIT_SIZE)
				break;
		}

		if (uart_tx_stopped(&s->port))
			mxs_auart_stop_tx(&s->port);

		if (i) {
			mxs_auart_dma_tx(s, i);
		} else {
			clear_bit(MXS_AUART_DMA_TX_SYNC, &s->flags);
			smp_mb__after_atomic();
		}
		return;
	}


	while (!(readl(s->port.membase + AUART_STAT) &
		 AUART_STAT_TXFF)) {
		if (s->port.x_char) {
			s->port.icount.tx++;
			writel(s->port.x_char,
				     s->port.membase + AUART_DATA);
			s->port.x_char = 0;
			continue;
		}
		if (!uart_circ_empty(xmit) && !uart_tx_stopped(&s->port)) {
			s->port.icount.tx++;
			writel(xmit->buf[xmit->tail],
				     s->port.membase + AUART_DATA);
			xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		} else
			break;
	}
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&s->port);

	if (uart_circ_empty(&(s->port.state->xmit)))
		writel(AUART_INTR_TXIEN,
			     s->port.membase + AUART_INTR_CLR);
	else
		writel(AUART_INTR_TXIEN,
			     s->port.membase + AUART_INTR_SET);

	if (uart_tx_stopped(&s->port))
		mxs_auart_stop_tx(&s->port);
}

static void mxs_auart_rx_char(struct mxs_auart_port *s)
{
	int flag;
	u32 stat;
	u8 c;

	c = readl(s->port.membase + AUART_DATA);
	stat = readl(s->port.membase + AUART_STAT);

	flag = TTY_NORMAL;
	s->port.icount.rx++;

	if (stat & AUART_STAT_BERR) {
		s->port.icount.brk++;
		if (uart_handle_break(&s->port))
			goto out;
	} else if (stat & AUART_STAT_PERR) {
		s->port.icount.parity++;
	} else if (stat & AUART_STAT_FERR) {
		s->port.icount.frame++;
	}

	/*
	 * Mask off conditions which should be ingored.
	 */
	stat &= s->port.read_status_mask;

	if (stat & AUART_STAT_BERR) {
		flag = TTY_BREAK;
	} else if (stat & AUART_STAT_PERR)
		flag = TTY_PARITY;
	else if (stat & AUART_STAT_FERR)
		flag = TTY_FRAME;

	if (stat & AUART_STAT_OERR)
		s->port.icount.overrun++;

	if (uart_handle_sysrq_char(&s->port, c))
		goto out;

	uart_insert_char(&s->port, stat, AUART_STAT_OERR, c, flag);
out:
	writel(stat, s->port.membase + AUART_STAT);
}

static void mxs_auart_rx_chars(struct mxs_auart_port *s)
{
	u32 stat = 0;

	for (;;) {
		stat = readl(s->port.membase + AUART_STAT);
		if (stat & AUART_STAT_RXFE)
			break;
		mxs_auart_rx_char(s);
	}

	writel(stat, s->port.membase + AUART_STAT);
	tty_flip_buffer_push(&s->port.state->port);
}

static int mxs_auart_request_port(struct uart_port *u)
{
	return 0;
}

static int mxs_auart_verify_port(struct uart_port *u,
				    struct serial_struct *ser)
{
	if (u->type != PORT_UNKNOWN && u->type != PORT_IMX)
		return -EINVAL;
	return 0;
}

static void mxs_auart_config_port(struct uart_port *u, int flags)
{
}

static const char *mxs_auart_type(struct uart_port *u)
{
	struct mxs_auart_port *s = to_auart_port(u);

	return dev_name(s->dev);
}

static void mxs_auart_release_port(struct uart_port *u)
{
}

static void mxs_auart_set_mctrl(struct uart_port *u, unsigned mctrl)
{
	struct mxs_auart_port *s = to_auart_port(u);

	u32 ctrl = readl(u->membase + AUART_CTRL2);

	ctrl &= ~(AUART_CTRL2_RTSEN | AUART_CTRL2_RTS);
	if (mctrl & TIOCM_RTS) {
		if (tty_port_cts_enabled(&u->state->port))
			ctrl |= AUART_CTRL2_RTSEN;
		else
			ctrl |= AUART_CTRL2_RTS;
	}

	s->ctrl = mctrl;
	writel(ctrl, u->membase + AUART_CTRL2);
}

static u32 mxs_auart_get_mctrl(struct uart_port *u)
{
	struct mxs_auart_port *s = to_auart_port(u);
	u32 stat = readl(u->membase + AUART_STAT);
	int ctrl2 = readl(u->membase + AUART_CTRL2);
	u32 mctrl = s->ctrl;

	mctrl &= ~TIOCM_CTS;
	if (stat & AUART_STAT_CTS)
		mctrl |= TIOCM_CTS;

	if (ctrl2 & AUART_CTRL2_RTS)
		mctrl |= TIOCM_RTS;

	return mctrl;
}

static int mxs_auart_dma_prep_rx(struct mxs_auart_port *s);
static void dma_rx_callback(void *arg)
{
	struct mxs_auart_port *s = (struct mxs_auart_port *) arg;
	struct tty_port *port = &s->port.state->port;
	int count;
	u32 stat;

	dma_unmap_sg(s->dev, &s->rx_sgl, 1, DMA_FROM_DEVICE);

	stat = readl(s->port.membase + AUART_STAT);
	stat &= ~(AUART_STAT_OERR | AUART_STAT_BERR |
			AUART_STAT_PERR | AUART_STAT_FERR);

	count = stat & AUART_STAT_RXCOUNT_MASK;
	tty_insert_flip_string(port, s->rx_dma_buf, count);

	writel(stat, s->port.membase + AUART_STAT);
	tty_flip_buffer_push(port);

	/* start the next DMA for RX. */
	mxs_auart_dma_prep_rx(s);
}

static int mxs_auart_dma_prep_rx(struct mxs_auart_port *s)
{
	struct dma_async_tx_descriptor *desc;
	struct scatterlist *sgl = &s->rx_sgl;
	struct dma_chan *channel = s->rx_dma_chan;
	u32 pio[1];

	/* [1] : send PIO */
	pio[0] = AUART_CTRL0_RXTO_ENABLE
		| AUART_CTRL0_RXTIMEOUT(0x80)
		| AUART_CTRL0_XFER_COUNT(UART_XMIT_SIZE);
	desc = dmaengine_prep_slave_sg(channel, (struct scatterlist *)pio,
					1, DMA_TRANS_NONE, 0);
	if (!desc) {
		dev_err(s->dev, "step 1 error\n");
		return -EINVAL;
	}

	/* [2] : send DMA request */
	sg_init_one(sgl, s->rx_dma_buf, UART_XMIT_SIZE);
	dma_map_sg(s->dev, sgl, 1, DMA_FROM_DEVICE);
	desc = dmaengine_prep_slave_sg(channel, sgl, 1, DMA_DEV_TO_MEM,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		dev_err(s->dev, "step 2 error\n");
		return -1;
	}

	/* [3] : submit the DMA, but do not issue it. */
	desc->callback = dma_rx_callback;
	desc->callback_param = s;
	dmaengine_submit(desc);
	dma_async_issue_pending(channel);
	return 0;
}

static void mxs_auart_dma_exit_channel(struct mxs_auart_port *s)
{
	if (s->tx_dma_chan) {
		dma_release_channel(s->tx_dma_chan);
		s->tx_dma_chan = NULL;
	}
	if (s->rx_dma_chan) {
		dma_release_channel(s->rx_dma_chan);
		s->rx_dma_chan = NULL;
	}

	kfree(s->tx_dma_buf);
	kfree(s->rx_dma_buf);
	s->tx_dma_buf = NULL;
	s->rx_dma_buf = NULL;
}

static void mxs_auart_dma_exit(struct mxs_auart_port *s)
{

	writel(AUART_CTRL2_TXDMAE | AUART_CTRL2_RXDMAE | AUART_CTRL2_DMAONERR,
		s->port.membase + AUART_CTRL2_CLR);

	mxs_auart_dma_exit_channel(s);
	s->flags &= ~MXS_AUART_DMA_ENABLED;
	clear_bit(MXS_AUART_DMA_TX_SYNC, &s->flags);
	clear_bit(MXS_AUART_DMA_RX_READY, &s->flags);
}

static int mxs_auart_dma_init(struct mxs_auart_port *s)
{
	if (auart_dma_enabled(s))
		return 0;

	/* init for RX */
	s->rx_dma_chan = dma_request_slave_channel(s->dev, "rx");
	if (!s->rx_dma_chan)
		goto err_out;
	s->rx_dma_buf = kzalloc(UART_XMIT_SIZE, GFP_KERNEL | GFP_DMA);
	if (!s->rx_dma_buf)
		goto err_out;

	/* init for TX */
	s->tx_dma_chan = dma_request_slave_channel(s->dev, "tx");
	if (!s->tx_dma_chan)
		goto err_out;
	s->tx_dma_buf = kzalloc(UART_XMIT_SIZE, GFP_KERNEL | GFP_DMA);
	if (!s->tx_dma_buf)
		goto err_out;

	/* set the flags */
	s->flags |= MXS_AUART_DMA_ENABLED;
	dev_dbg(s->dev, "enabled the DMA support.");

	/* The DMA buffer is now the FIFO the TTY subsystem can use */
	s->port.fifosize = UART_XMIT_SIZE;

	return 0;

err_out:
	mxs_auart_dma_exit_channel(s);
	return -EINVAL;

}

static void mxs_auart_settermios(struct uart_port *u,
				 struct ktermios *termios,
				 struct ktermios *old)
{
	struct mxs_auart_port *s = to_auart_port(u);
	u32 bm, ctrl, ctrl2, div;
	unsigned int cflag, baud;

	cflag = termios->c_cflag;

	ctrl = AUART_LINECTRL_FEN;
	ctrl2 = readl(u->membase + AUART_CTRL2);

	/* byte size */
	switch (cflag & CSIZE) {
	case CS5:
		bm = 0;
		break;
	case CS6:
		bm = 1;
		break;
	case CS7:
		bm = 2;
		break;
	case CS8:
		bm = 3;
		break;
	default:
		return;
	}

	ctrl |= AUART_LINECTRL_WLEN(bm);

	/* parity */
	if (cflag & PARENB) {
		ctrl |= AUART_LINECTRL_PEN;
		if ((cflag & PARODD) == 0)
			ctrl |= AUART_LINECTRL_EPS;
	}

	u->read_status_mask = 0;

	if (termios->c_iflag & INPCK)
		u->read_status_mask |= AUART_STAT_PERR;
	if (termios->c_iflag & (IGNBRK | BRKINT | PARMRK))
		u->read_status_mask |= AUART_STAT_BERR;

	/*
	 * Characters to ignore
	 */
	u->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		u->ignore_status_mask |= AUART_STAT_PERR;
	if (termios->c_iflag & IGNBRK) {
		u->ignore_status_mask |= AUART_STAT_BERR;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			u->ignore_status_mask |= AUART_STAT_OERR;
	}

	/*
	 * ignore all characters if CREAD is not set
	 */
	if (cflag & CREAD)
		ctrl2 |= AUART_CTRL2_RXE;
	else
		ctrl2 &= ~AUART_CTRL2_RXE;

	/* figure out the stop bits requested */
	if (cflag & CSTOPB)
		ctrl |= AUART_LINECTRL_STP2;

	/* figure out the hardware flow control settings */
	if (cflag & CRTSCTS) {
		/*
		 * The DMA has a bug(see errata:2836) in mx23.
		 * So we can not implement the DMA for auart in mx23,
		 * we can only implement the DMA support for auart
		 * in mx28.
		 */
		if (is_imx28_auart(s)
				&& test_bit(MXS_AUART_RTSCTS, &s->flags)) {
			if (!mxs_auart_dma_init(s))
				/* enable DMA tranfer */
				ctrl2 |= AUART_CTRL2_TXDMAE | AUART_CTRL2_RXDMAE
				       | AUART_CTRL2_DMAONERR;
		}
		ctrl2 |= AUART_CTRL2_CTSEN | AUART_CTRL2_RTSEN;
	} else {
		ctrl2 &= ~(AUART_CTRL2_CTSEN | AUART_CTRL2_RTSEN);
	}

	/* set baud rate */
	baud = uart_get_baud_rate(u, termios, old, 0, u->uartclk);
	div = u->uartclk * 32 / baud;
	ctrl |= AUART_LINECTRL_BAUD_DIVFRAC(div & 0x3F);
	ctrl |= AUART_LINECTRL_BAUD_DIVINT(div >> 6);

	writel(ctrl, u->membase + AUART_LINECTRL);
	writel(ctrl2, u->membase + AUART_CTRL2);

	uart_update_timeout(u, termios->c_cflag, baud);

	/* prepare for the DMA RX. */
	if (auart_dma_enabled(s) &&
		!test_and_set_bit(MXS_AUART_DMA_RX_READY, &s->flags)) {
		if (!mxs_auart_dma_prep_rx(s)) {
			/* Disable the normal RX interrupt. */
			writel(AUART_INTR_RXIEN | AUART_INTR_RTIEN,
					u->membase + AUART_INTR_CLR);
		} else {
			mxs_auart_dma_exit(s);
			dev_err(s->dev, "We can not start up the DMA.\n");
		}
	}
}

static irqreturn_t mxs_auart_irq_handle(int irq, void *context)
{
	u32 istat;
	struct mxs_auart_port *s = context;
	u32 stat = readl(s->port.membase + AUART_STAT);

	istat = readl(s->port.membase + AUART_INTR);

	/* ack irq */
	writel(istat & (AUART_INTR_RTIS
		| AUART_INTR_TXIS
		| AUART_INTR_RXIS
		| AUART_INTR_CTSMIS),
			s->port.membase + AUART_INTR_CLR);

	if (istat & AUART_INTR_CTSMIS) {
		uart_handle_cts_change(&s->port, stat & AUART_STAT_CTS);
		writel(AUART_INTR_CTSMIS,
				s->port.membase + AUART_INTR_CLR);
		istat &= ~AUART_INTR_CTSMIS;
	}

	if (istat & (AUART_INTR_RTIS | AUART_INTR_RXIS)) {
		if (!auart_dma_enabled(s))
			mxs_auart_rx_chars(s);
		istat &= ~(AUART_INTR_RTIS | AUART_INTR_RXIS);
	}

	if (istat & AUART_INTR_TXIS) {
		mxs_auart_tx_chars(s);
		istat &= ~AUART_INTR_TXIS;
	}

	return IRQ_HANDLED;
}

static void mxs_auart_reset(struct uart_port *u)
{
	int i;
	unsigned int reg;

	writel(AUART_CTRL0_SFTRST, u->membase + AUART_CTRL0_CLR);

	for (i = 0; i < 10000; i++) {
		reg = readl(u->membase + AUART_CTRL0);
		if (!(reg & AUART_CTRL0_SFTRST))
			break;
		udelay(3);
	}
	writel(AUART_CTRL0_CLKGATE, u->membase + AUART_CTRL0_CLR);
}

static int mxs_auart_startup(struct uart_port *u)
{
	int ret;
	struct mxs_auart_port *s = to_auart_port(u);

	ret = clk_prepare_enable(s->clk);
	if (ret)
		return ret;

	writel(AUART_CTRL0_CLKGATE, u->membase + AUART_CTRL0_CLR);

	writel(AUART_CTRL2_UARTEN, u->membase + AUART_CTRL2_SET);

	writel(AUART_INTR_RXIEN | AUART_INTR_RTIEN | AUART_INTR_CTSMIEN,
			u->membase + AUART_INTR);

	/* Reset FIFO size (it could have changed if DMA was enabled) */
	u->fifosize = MXS_AUART_FIFO_SIZE;

	/*
	 * Enable fifo so all four bytes of a DMA word are written to
	 * output (otherwise, only the LSB is written, ie. 1 in 4 bytes)
	 */
	writel(AUART_LINECTRL_FEN, u->membase + AUART_LINECTRL_SET);

	return 0;
}

static void mxs_auart_shutdown(struct uart_port *u)
{
	struct mxs_auart_port *s = to_auart_port(u);

	if (auart_dma_enabled(s))
		mxs_auart_dma_exit(s);

	writel(AUART_CTRL2_UARTEN, u->membase + AUART_CTRL2_CLR);

	writel(AUART_INTR_RXIEN | AUART_INTR_RTIEN | AUART_INTR_CTSMIEN,
			u->membase + AUART_INTR_CLR);

	writel(AUART_CTRL0_CLKGATE, u->membase + AUART_CTRL0_SET);

	clk_disable_unprepare(s->clk);
}

static unsigned int mxs_auart_tx_empty(struct uart_port *u)
{
	if (readl(u->membase + AUART_STAT) & AUART_STAT_TXFE)
		return TIOCSER_TEMT;
	else
		return 0;
}

static void mxs_auart_start_tx(struct uart_port *u)
{
	struct mxs_auart_port *s = to_auart_port(u);

	/* enable transmitter */
	writel(AUART_CTRL2_TXE, u->membase + AUART_CTRL2_SET);

	mxs_auart_tx_chars(s);
}

static void mxs_auart_stop_tx(struct uart_port *u)
{
	writel(AUART_CTRL2_TXE, u->membase + AUART_CTRL2_CLR);
}

static void mxs_auart_stop_rx(struct uart_port *u)
{
	writel(AUART_CTRL2_RXE, u->membase + AUART_CTRL2_CLR);
}

static void mxs_auart_break_ctl(struct uart_port *u, int ctl)
{
	if (ctl)
		writel(AUART_LINECTRL_BRK,
			     u->membase + AUART_LINECTRL_SET);
	else
		writel(AUART_LINECTRL_BRK,
			     u->membase + AUART_LINECTRL_CLR);
}

static struct uart_ops mxs_auart_ops = {
	.tx_empty       = mxs_auart_tx_empty,
	.start_tx       = mxs_auart_start_tx,
	.stop_tx	= mxs_auart_stop_tx,
	.stop_rx	= mxs_auart_stop_rx,
	.break_ctl      = mxs_auart_break_ctl,
	.set_mctrl	= mxs_auart_set_mctrl,
	.get_mctrl      = mxs_auart_get_mctrl,
	.startup	= mxs_auart_startup,
	.shutdown       = mxs_auart_shutdown,
	.set_termios    = mxs_auart_settermios,
	.type	   	= mxs_auart_type,
	.release_port   = mxs_auart_release_port,
	.request_port   = mxs_auart_request_port,
	.config_port    = mxs_auart_config_port,
	.verify_port    = mxs_auart_verify_port,
};

static struct mxs_auart_port *auart_port[MXS_AUART_PORTS];

#ifdef CONFIG_SERIAL_MXS_AUART_CONSOLE
static void mxs_auart_console_putchar(struct uart_port *port, int ch)
{
	unsigned int to = 1000;

	while (readl(port->membase + AUART_STAT) & AUART_STAT_TXFF) {
		if (!to--)
			break;
		udelay(1);
	}

	writel(ch, port->membase + AUART_DATA);
}

static void
auart_console_write(struct console *co, const char *str, unsigned int count)
{
	struct mxs_auart_port *s;
	struct uart_port *port;
	unsigned int old_ctrl0, old_ctrl2;
	unsigned int to = 20000;

	if (co->index >= MXS_AUART_PORTS || co->index < 0)
		return;

	s = auart_port[co->index];
	port = &s->port;

	clk_enable(s->clk);

	/* First save the CR then disable the interrupts */
	old_ctrl2 = readl(port->membase + AUART_CTRL2);
	old_ctrl0 = readl(port->membase + AUART_CTRL0);

	writel(AUART_CTRL0_CLKGATE,
		     port->membase + AUART_CTRL0_CLR);
	writel(AUART_CTRL2_UARTEN | AUART_CTRL2_TXE,
		     port->membase + AUART_CTRL2_SET);

	uart_console_write(port, str, count, mxs_auart_console_putchar);

	/* Finally, wait for transmitter to become empty ... */
	while (readl(port->membase + AUART_STAT) & AUART_STAT_BUSY) {
		udelay(1);
		if (!to--)
			break;
	}

	/*
	 * ... and restore the TCR if we waited long enough for the transmitter
	 * to be idle. This might keep the transmitter enabled although it is
	 * unused, but that is better than to disable it while it is still
	 * transmitting.
	 */
	if (!(readl(port->membase + AUART_STAT) & AUART_STAT_BUSY)) {
		writel(old_ctrl0, port->membase + AUART_CTRL0);
		writel(old_ctrl2, port->membase + AUART_CTRL2);
	}

	clk_disable(s->clk);
}

static void __init
auart_console_get_options(struct uart_port *port, int *baud,
			  int *parity, int *bits)
{
	unsigned int lcr_h, quot;

	if (!(readl(port->membase + AUART_CTRL2) & AUART_CTRL2_UARTEN))
		return;

	lcr_h = readl(port->membase + AUART_LINECTRL);

	*parity = 'n';
	if (lcr_h & AUART_LINECTRL_PEN) {
		if (lcr_h & AUART_LINECTRL_EPS)
			*parity = 'e';
		else
			*parity = 'o';
	}

	if ((lcr_h & AUART_LINECTRL_WLEN_MASK) == AUART_LINECTRL_WLEN(2))
		*bits = 7;
	else
		*bits = 8;

	quot = ((readl(port->membase + AUART_LINECTRL)
			& AUART_LINECTRL_BAUD_DIVINT_MASK))
			    >> (AUART_LINECTRL_BAUD_DIVINT_SHIFT - 6);
	quot |= ((readl(port->membase + AUART_LINECTRL)
			& AUART_LINECTRL_BAUD_DIVFRAC_MASK))
				>> AUART_LINECTRL_BAUD_DIVFRAC_SHIFT;
	if (quot == 0)
		quot = 1;

	*baud = (port->uartclk << 2) / quot;
}

static int __init
auart_console_setup(struct console *co, char *options)
{
	struct mxs_auart_port *s;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index == -1 || co->index >= ARRAY_SIZE(auart_port))
		co->index = 0;
	s = auart_port[co->index];
	if (!s)
		return -ENODEV;

	ret = clk_prepare_enable(s->clk);
	if (ret)
		return ret;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		auart_console_get_options(&s->port, &baud, &parity, &bits);

	ret = uart_set_options(&s->port, co, baud, parity, bits, flow);

	clk_disable_unprepare(s->clk);

	return ret;
}

static struct console auart_console = {
	.name		= "ttyAPP",
	.write		= auart_console_write,
	.device		= uart_console_device,
	.setup		= auart_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &auart_driver,
};
#endif

static struct uart_driver auart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "ttyAPP",
	.dev_name	= "ttyAPP",
	.major		= 0,
	.minor		= 0,
	.nr		= MXS_AUART_PORTS,
#ifdef CONFIG_SERIAL_MXS_AUART_CONSOLE
	.cons =		&auart_console,
#endif
};

/*
 * This function returns 1 if pdev isn't a device instatiated by dt, 0 if it
 * could successfully get all information from dt or a negative errno.
 */
static int serial_mxs_probe_dt(struct mxs_auart_port *s,
		struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (!np)
		/* no device tree device */
		return 1;

	ret = of_alias_get_id(np, "serial");
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get alias id: %d\n", ret);
		return ret;
	}
	s->port.line = ret;

	if (of_get_property(np, "fsl,uart-has-rtscts", NULL))
		set_bit(MXS_AUART_RTSCTS, &s->flags);

	return 0;
}

static int mxs_auart_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(mxs_auart_dt_ids, &pdev->dev);
	struct mxs_auart_port *s;
	u32 version;
	int ret = 0;
	struct resource *r;

	s = kzalloc(sizeof(struct mxs_auart_port), GFP_KERNEL);
	if (!s) {
		ret = -ENOMEM;
		goto out;
	}

	ret = serial_mxs_probe_dt(s, pdev);
	if (ret > 0)
		s->port.line = pdev->id < 0 ? 0 : pdev->id;
	else if (ret < 0)
		goto out_free;

	if (of_id) {
		pdev->id_entry = of_id->data;
		s->devtype = pdev->id_entry->driver_data;
	}

	s->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(s->clk)) {
		ret = PTR_ERR(s->clk);
		goto out_free;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		ret = -ENXIO;
		goto out_free_clk;
	}

	s->port.mapbase = r->start;
	s->port.membase = ioremap(r->start, resource_size(r));
	s->port.ops = &mxs_auart_ops;
	s->port.iotype = UPIO_MEM;
	s->port.fifosize = MXS_AUART_FIFO_SIZE;
	s->port.uartclk = clk_get_rate(s->clk);
	s->port.type = PORT_IMX;
	s->port.dev = s->dev = &pdev->dev;

	s->ctrl = 0;

	s->irq = platform_get_irq(pdev, 0);
	s->port.irq = s->irq;
	ret = request_irq(s->irq, mxs_auart_irq_handle, 0, dev_name(&pdev->dev), s);
	if (ret)
		goto out_free_clk;

	platform_set_drvdata(pdev, s);

	auart_port[s->port.line] = s;

	mxs_auart_reset(&s->port);

	ret = uart_add_one_port(&auart_driver, &s->port);
	if (ret)
		goto out_free_irq;

	version = readl(s->port.membase + AUART_VERSION);
	dev_info(&pdev->dev, "Found APPUART %d.%d.%d\n",
	       (version >> 24) & 0xff,
	       (version >> 16) & 0xff, version & 0xffff);

	return 0;

out_free_irq:
	auart_port[pdev->id] = NULL;
	free_irq(s->irq, s);
out_free_clk:
	clk_put(s->clk);
out_free:
	kfree(s);
out:
	return ret;
}

static int mxs_auart_remove(struct platform_device *pdev)
{
	struct mxs_auart_port *s = platform_get_drvdata(pdev);

	uart_remove_one_port(&auart_driver, &s->port);

	auart_port[pdev->id] = NULL;

	clk_put(s->clk);
	free_irq(s->irq, s);
	kfree(s);

	return 0;
}

static struct platform_driver mxs_auart_driver = {
	.probe = mxs_auart_probe,
	.remove = mxs_auart_remove,
	.driver = {
		.name = "mxs-auart",
		.owner = THIS_MODULE,
		.of_match_table = mxs_auart_dt_ids,
	},
};

static int __init mxs_auart_init(void)
{
	int r;

	r = uart_register_driver(&auart_driver);
	if (r)
		goto out;

	r = platform_driver_register(&mxs_auart_driver);
	if (r)
		goto out_err;

	return 0;
out_err:
	uart_unregister_driver(&auart_driver);
out:
	return r;
}

static void __exit mxs_auart_exit(void)
{
	platform_driver_unregister(&mxs_auart_driver);
	uart_unregister_driver(&auart_driver);
}

module_init(mxs_auart_init);
module_exit(mxs_auart_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Freescale MXS application uart driver");
MODULE_ALIAS("platform:mxs-auart");
