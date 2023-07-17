// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012-2015 Spreadtrum Communications Inc.
 */

#include <linux/clk.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma/sprd-dma.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

/* device name */
#define UART_NR_MAX		8
#define SPRD_TTY_NAME		"ttyS"
#define SPRD_FIFO_SIZE		128
#define SPRD_DEF_RATE		26000000
#define SPRD_BAUD_IO_LIMIT	3000000
#define SPRD_TIMEOUT		256000

/* the offset of serial registers and BITs for them */
/* data registers */
#define SPRD_TXD		0x0000
#define SPRD_RXD		0x0004

/* line status register and its BITs  */
#define SPRD_LSR		0x0008
#define SPRD_LSR_OE		BIT(4)
#define SPRD_LSR_FE		BIT(3)
#define SPRD_LSR_PE		BIT(2)
#define SPRD_LSR_BI		BIT(7)
#define SPRD_LSR_TX_OVER	BIT(15)

/* data number in TX and RX fifo */
#define SPRD_STS1		0x000C
#define SPRD_RX_FIFO_CNT_MASK	GENMASK(7, 0)
#define SPRD_TX_FIFO_CNT_MASK	GENMASK(15, 8)

/* interrupt enable register and its BITs */
#define SPRD_IEN		0x0010
#define SPRD_IEN_RX_FULL	BIT(0)
#define SPRD_IEN_TX_EMPTY	BIT(1)
#define SPRD_IEN_BREAK_DETECT	BIT(7)
#define SPRD_IEN_TIMEOUT	BIT(13)

/* interrupt clear register */
#define SPRD_ICLR		0x0014
#define SPRD_ICLR_TIMEOUT	BIT(13)

/* line control register */
#define SPRD_LCR		0x0018
#define SPRD_LCR_STOP_1BIT	0x10
#define SPRD_LCR_STOP_2BIT	0x30
#define SPRD_LCR_DATA_LEN	(BIT(2) | BIT(3))
#define SPRD_LCR_DATA_LEN5	0x0
#define SPRD_LCR_DATA_LEN6	0x4
#define SPRD_LCR_DATA_LEN7	0x8
#define SPRD_LCR_DATA_LEN8	0xc
#define SPRD_LCR_PARITY		(BIT(0) | BIT(1))
#define SPRD_LCR_PARITY_EN	0x2
#define SPRD_LCR_EVEN_PAR	0x0
#define SPRD_LCR_ODD_PAR	0x1

/* control register 1 */
#define SPRD_CTL1		0x001C
#define SPRD_DMA_EN		BIT(15)
#define SPRD_LOOPBACK_EN	BIT(14)
#define RX_HW_FLOW_CTL_THLD	BIT(6)
#define RX_HW_FLOW_CTL_EN	BIT(7)
#define TX_HW_FLOW_CTL_EN	BIT(8)
#define RX_TOUT_THLD_DEF	0x3E00
#define RX_HFC_THLD_DEF		0x40

/* fifo threshold register */
#define SPRD_CTL2		0x0020
#define THLD_TX_EMPTY		0x40
#define THLD_TX_EMPTY_SHIFT	8
#define THLD_RX_FULL		0x40
#define THLD_RX_FULL_MASK	GENMASK(6, 0)

/* config baud rate register */
#define SPRD_CLKD0		0x0024
#define SPRD_CLKD0_MASK		GENMASK(15, 0)
#define SPRD_CLKD1		0x0028
#define SPRD_CLKD1_MASK		GENMASK(20, 16)
#define SPRD_CLKD1_SHIFT	16

/* interrupt mask status register */
#define SPRD_IMSR		0x002C
#define SPRD_IMSR_RX_FIFO_FULL	BIT(0)
#define SPRD_IMSR_TX_FIFO_EMPTY	BIT(1)
#define SPRD_IMSR_BREAK_DETECT	BIT(7)
#define SPRD_IMSR_TIMEOUT	BIT(13)
#define SPRD_DEFAULT_SOURCE_CLK	26000000

#define SPRD_RX_DMA_STEP	1
#define SPRD_RX_FIFO_FULL	1
#define SPRD_TX_FIFO_FULL	0x20
#define SPRD_UART_RX_SIZE	(UART_XMIT_SIZE / 4)

struct sprd_uart_dma {
	struct dma_chan *chn;
	unsigned char *virt;
	dma_addr_t phys_addr;
	dma_cookie_t cookie;
	u32 trans_len;
	bool enable;
};

struct sprd_uart_port {
	struct uart_port port;
	char name[16];
	struct clk *clk;
	struct sprd_uart_dma tx_dma;
	struct sprd_uart_dma rx_dma;
	dma_addr_t pos;
	unsigned char *rx_buf_tail;
};

static struct sprd_uart_port *sprd_port[UART_NR_MAX];
static int sprd_ports_num;

static int sprd_start_dma_rx(struct uart_port *port);
static int sprd_tx_dma_config(struct uart_port *port);

static inline unsigned int serial_in(struct uart_port *port,
				     unsigned int offset)
{
	return readl_relaxed(port->membase + offset);
}

static inline void serial_out(struct uart_port *port, unsigned int offset,
			      int value)
{
	writel_relaxed(value, port->membase + offset);
}

static unsigned int sprd_tx_empty(struct uart_port *port)
{
	if (serial_in(port, SPRD_STS1) & SPRD_TX_FIFO_CNT_MASK)
		return 0;
	else
		return TIOCSER_TEMT;
}

static unsigned int sprd_get_mctrl(struct uart_port *port)
{
	return TIOCM_DSR | TIOCM_CTS;
}

static void sprd_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	u32 val = serial_in(port, SPRD_CTL1);

	if (mctrl & TIOCM_LOOP)
		val |= SPRD_LOOPBACK_EN;
	else
		val &= ~SPRD_LOOPBACK_EN;

	serial_out(port, SPRD_CTL1, val);
}

static void sprd_stop_rx(struct uart_port *port)
{
	struct sprd_uart_port *sp =
		container_of(port, struct sprd_uart_port, port);
	unsigned int ien, iclr;

	if (sp->rx_dma.enable)
		dmaengine_terminate_all(sp->rx_dma.chn);

	iclr = serial_in(port, SPRD_ICLR);
	ien = serial_in(port, SPRD_IEN);

	ien &= ~(SPRD_IEN_RX_FULL | SPRD_IEN_BREAK_DETECT);
	iclr |= SPRD_IEN_RX_FULL | SPRD_IEN_BREAK_DETECT;

	serial_out(port, SPRD_IEN, ien);
	serial_out(port, SPRD_ICLR, iclr);
}

static void sprd_uart_dma_enable(struct uart_port *port, bool enable)
{
	u32 val = serial_in(port, SPRD_CTL1);

	if (enable)
		val |= SPRD_DMA_EN;
	else
		val &= ~SPRD_DMA_EN;

	serial_out(port, SPRD_CTL1, val);
}

static void sprd_stop_tx_dma(struct uart_port *port)
{
	struct sprd_uart_port *sp =
		container_of(port, struct sprd_uart_port, port);
	struct dma_tx_state state;
	u32 trans_len;

	dmaengine_pause(sp->tx_dma.chn);

	dmaengine_tx_status(sp->tx_dma.chn, sp->tx_dma.cookie, &state);
	if (state.residue) {
		trans_len = state.residue - sp->tx_dma.phys_addr;
		uart_xmit_advance(port, trans_len);
		dma_unmap_single(port->dev, sp->tx_dma.phys_addr,
				 sp->tx_dma.trans_len, DMA_TO_DEVICE);
	}

	dmaengine_terminate_all(sp->tx_dma.chn);
	sp->tx_dma.trans_len = 0;
}

static int sprd_tx_buf_remap(struct uart_port *port)
{
	struct sprd_uart_port *sp =
		container_of(port, struct sprd_uart_port, port);
	struct circ_buf *xmit = &port->state->xmit;

	sp->tx_dma.trans_len =
		CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE);

	sp->tx_dma.phys_addr = dma_map_single(port->dev,
					      (void *)&(xmit->buf[xmit->tail]),
					      sp->tx_dma.trans_len,
					      DMA_TO_DEVICE);
	return dma_mapping_error(port->dev, sp->tx_dma.phys_addr);
}

static void sprd_complete_tx_dma(void *data)
{
	struct uart_port *port = (struct uart_port *)data;
	struct sprd_uart_port *sp =
		container_of(port, struct sprd_uart_port, port);
	struct circ_buf *xmit = &port->state->xmit;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	dma_unmap_single(port->dev, sp->tx_dma.phys_addr,
			 sp->tx_dma.trans_len, DMA_TO_DEVICE);

	uart_xmit_advance(port, sp->tx_dma.trans_len);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit) || sprd_tx_buf_remap(port) ||
	    sprd_tx_dma_config(port))
		sp->tx_dma.trans_len = 0;

	spin_unlock_irqrestore(&port->lock, flags);
}

static int sprd_uart_dma_submit(struct uart_port *port,
				struct sprd_uart_dma *ud, u32 trans_len,
				enum dma_transfer_direction direction,
				dma_async_tx_callback callback)
{
	struct dma_async_tx_descriptor *dma_des;
	unsigned long flags;

	flags = SPRD_DMA_FLAGS(SPRD_DMA_CHN_MODE_NONE,
			       SPRD_DMA_NO_TRG,
			       SPRD_DMA_FRAG_REQ,
			       SPRD_DMA_TRANS_INT);

	dma_des = dmaengine_prep_slave_single(ud->chn, ud->phys_addr, trans_len,
					      direction, flags);
	if (!dma_des)
		return -ENODEV;

	dma_des->callback = callback;
	dma_des->callback_param = port;

	ud->cookie = dmaengine_submit(dma_des);
	if (dma_submit_error(ud->cookie))
		return dma_submit_error(ud->cookie);

	dma_async_issue_pending(ud->chn);

	return 0;
}

static int sprd_tx_dma_config(struct uart_port *port)
{
	struct sprd_uart_port *sp =
		container_of(port, struct sprd_uart_port, port);
	u32 burst = sp->tx_dma.trans_len > SPRD_TX_FIFO_FULL ?
		SPRD_TX_FIFO_FULL : sp->tx_dma.trans_len;
	int ret;
	struct dma_slave_config cfg = {
		.dst_addr = port->mapbase + SPRD_TXD,
		.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
		.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
		.src_maxburst = burst,
	};

	ret = dmaengine_slave_config(sp->tx_dma.chn, &cfg);
	if (ret < 0)
		return ret;

	return sprd_uart_dma_submit(port, &sp->tx_dma, sp->tx_dma.trans_len,
				    DMA_MEM_TO_DEV, sprd_complete_tx_dma);
}

static void sprd_start_tx_dma(struct uart_port *port)
{
	struct sprd_uart_port *sp =
		container_of(port, struct sprd_uart_port, port);
	struct circ_buf *xmit = &port->state->xmit;

	if (port->x_char) {
		serial_out(port, SPRD_TXD, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		sprd_stop_tx_dma(port);
		return;
	}

	if (sp->tx_dma.trans_len)
		return;

	if (sprd_tx_buf_remap(port) || sprd_tx_dma_config(port))
		sp->tx_dma.trans_len = 0;
}

static void sprd_rx_full_thld(struct uart_port *port, u32 thld)
{
	u32 val = serial_in(port, SPRD_CTL2);

	val &= ~THLD_RX_FULL_MASK;
	val |= thld & THLD_RX_FULL_MASK;
	serial_out(port, SPRD_CTL2, val);
}

static int sprd_rx_alloc_buf(struct sprd_uart_port *sp)
{
	sp->rx_dma.virt = dma_alloc_coherent(sp->port.dev, SPRD_UART_RX_SIZE,
					     &sp->rx_dma.phys_addr, GFP_KERNEL);
	if (!sp->rx_dma.virt)
		return -ENOMEM;

	return 0;
}

static void sprd_rx_free_buf(struct sprd_uart_port *sp)
{
	if (sp->rx_dma.virt)
		dma_free_coherent(sp->port.dev, SPRD_UART_RX_SIZE,
				  sp->rx_dma.virt, sp->rx_dma.phys_addr);

}

static int sprd_rx_dma_config(struct uart_port *port, u32 burst)
{
	struct sprd_uart_port *sp =
		container_of(port, struct sprd_uart_port, port);
	struct dma_slave_config cfg = {
		.src_addr = port->mapbase + SPRD_RXD,
		.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
		.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
		.src_maxburst = burst,
	};

	return dmaengine_slave_config(sp->rx_dma.chn, &cfg);
}

static void sprd_uart_dma_rx(struct uart_port *port)
{
	struct sprd_uart_port *sp =
		container_of(port, struct sprd_uart_port, port);
	struct tty_port *tty = &port->state->port;

	port->icount.rx += sp->rx_dma.trans_len;
	tty_insert_flip_string(tty, sp->rx_buf_tail, sp->rx_dma.trans_len);
	tty_flip_buffer_push(tty);
}

static void sprd_uart_dma_irq(struct uart_port *port)
{
	struct sprd_uart_port *sp =
		container_of(port, struct sprd_uart_port, port);
	struct dma_tx_state state;
	enum dma_status status;

	status = dmaengine_tx_status(sp->rx_dma.chn,
				     sp->rx_dma.cookie, &state);
	if (status == DMA_ERROR)
		sprd_stop_rx(port);

	if (!state.residue && sp->pos == sp->rx_dma.phys_addr)
		return;

	if (!state.residue) {
		sp->rx_dma.trans_len = SPRD_UART_RX_SIZE +
			sp->rx_dma.phys_addr - sp->pos;
		sp->pos = sp->rx_dma.phys_addr;
	} else {
		sp->rx_dma.trans_len = state.residue - sp->pos;
		sp->pos = state.residue;
	}

	sprd_uart_dma_rx(port);
	sp->rx_buf_tail += sp->rx_dma.trans_len;
}

static void sprd_complete_rx_dma(void *data)
{
	struct uart_port *port = (struct uart_port *)data;
	struct sprd_uart_port *sp =
		container_of(port, struct sprd_uart_port, port);
	struct dma_tx_state state;
	enum dma_status status;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	status = dmaengine_tx_status(sp->rx_dma.chn,
				     sp->rx_dma.cookie, &state);
	if (status != DMA_COMPLETE) {
		sprd_stop_rx(port);
		spin_unlock_irqrestore(&port->lock, flags);
		return;
	}

	if (sp->pos != sp->rx_dma.phys_addr) {
		sp->rx_dma.trans_len =  SPRD_UART_RX_SIZE +
			sp->rx_dma.phys_addr - sp->pos;
		sprd_uart_dma_rx(port);
		sp->rx_buf_tail += sp->rx_dma.trans_len;
	}

	if (sprd_start_dma_rx(port))
		sprd_stop_rx(port);

	spin_unlock_irqrestore(&port->lock, flags);
}

static int sprd_start_dma_rx(struct uart_port *port)
{
	struct sprd_uart_port *sp =
		container_of(port, struct sprd_uart_port, port);
	int ret;

	if (!sp->rx_dma.enable)
		return 0;

	sp->pos = sp->rx_dma.phys_addr;
	sp->rx_buf_tail = sp->rx_dma.virt;
	sprd_rx_full_thld(port, SPRD_RX_FIFO_FULL);
	ret = sprd_rx_dma_config(port, SPRD_RX_DMA_STEP);
	if (ret)
		return ret;

	return sprd_uart_dma_submit(port, &sp->rx_dma, SPRD_UART_RX_SIZE,
				    DMA_DEV_TO_MEM, sprd_complete_rx_dma);
}

static void sprd_release_dma(struct uart_port *port)
{
	struct sprd_uart_port *sp =
		container_of(port, struct sprd_uart_port, port);

	sprd_uart_dma_enable(port, false);

	if (sp->rx_dma.enable)
		dma_release_channel(sp->rx_dma.chn);

	if (sp->tx_dma.enable)
		dma_release_channel(sp->tx_dma.chn);

	sp->tx_dma.enable = false;
	sp->rx_dma.enable = false;
}

static void sprd_request_dma(struct uart_port *port)
{
	struct sprd_uart_port *sp =
		container_of(port, struct sprd_uart_port, port);

	sp->tx_dma.enable = true;
	sp->rx_dma.enable = true;

	sp->tx_dma.chn = dma_request_chan(port->dev, "tx");
	if (IS_ERR(sp->tx_dma.chn)) {
		dev_err(port->dev, "request TX DMA channel failed, ret = %ld\n",
			PTR_ERR(sp->tx_dma.chn));
		sp->tx_dma.enable = false;
	}

	sp->rx_dma.chn = dma_request_chan(port->dev, "rx");
	if (IS_ERR(sp->rx_dma.chn)) {
		dev_err(port->dev, "request RX DMA channel failed, ret = %ld\n",
			PTR_ERR(sp->rx_dma.chn));
		sp->rx_dma.enable = false;
	}
}

static void sprd_stop_tx(struct uart_port *port)
{
	struct sprd_uart_port *sp = container_of(port, struct sprd_uart_port,
						 port);
	unsigned int ien, iclr;

	if (sp->tx_dma.enable) {
		sprd_stop_tx_dma(port);
		return;
	}

	iclr = serial_in(port, SPRD_ICLR);
	ien = serial_in(port, SPRD_IEN);

	iclr |= SPRD_IEN_TX_EMPTY;
	ien &= ~SPRD_IEN_TX_EMPTY;

	serial_out(port, SPRD_IEN, ien);
	serial_out(port, SPRD_ICLR, iclr);
}

static void sprd_start_tx(struct uart_port *port)
{
	struct sprd_uart_port *sp = container_of(port, struct sprd_uart_port,
						 port);
	unsigned int ien;

	if (sp->tx_dma.enable) {
		sprd_start_tx_dma(port);
		return;
	}

	ien = serial_in(port, SPRD_IEN);
	if (!(ien & SPRD_IEN_TX_EMPTY)) {
		ien |= SPRD_IEN_TX_EMPTY;
		serial_out(port, SPRD_IEN, ien);
	}
}

/* The Sprd serial does not support this function. */
static void sprd_break_ctl(struct uart_port *port, int break_state)
{
	/* nothing to do */
}

static int handle_lsr_errors(struct uart_port *port,
			     unsigned int *flag,
			     unsigned int *lsr)
{
	int ret = 0;

	/* statistics */
	if (*lsr & SPRD_LSR_BI) {
		*lsr &= ~(SPRD_LSR_FE | SPRD_LSR_PE);
		port->icount.brk++;
		ret = uart_handle_break(port);
		if (ret)
			return ret;
	} else if (*lsr & SPRD_LSR_PE)
		port->icount.parity++;
	else if (*lsr & SPRD_LSR_FE)
		port->icount.frame++;
	if (*lsr & SPRD_LSR_OE)
		port->icount.overrun++;

	/* mask off conditions which should be ignored */
	*lsr &= port->read_status_mask;
	if (*lsr & SPRD_LSR_BI)
		*flag = TTY_BREAK;
	else if (*lsr & SPRD_LSR_PE)
		*flag = TTY_PARITY;
	else if (*lsr & SPRD_LSR_FE)
		*flag = TTY_FRAME;

	return ret;
}

static inline void sprd_rx(struct uart_port *port)
{
	struct sprd_uart_port *sp = container_of(port, struct sprd_uart_port,
						 port);
	struct tty_port *tty = &port->state->port;
	unsigned int ch, flag, lsr, max_count = SPRD_TIMEOUT;

	if (sp->rx_dma.enable) {
		sprd_uart_dma_irq(port);
		return;
	}

	while ((serial_in(port, SPRD_STS1) & SPRD_RX_FIFO_CNT_MASK) &&
	       max_count--) {
		lsr = serial_in(port, SPRD_LSR);
		ch = serial_in(port, SPRD_RXD);
		flag = TTY_NORMAL;
		port->icount.rx++;

		if (lsr & (SPRD_LSR_BI | SPRD_LSR_PE |
			   SPRD_LSR_FE | SPRD_LSR_OE))
			if (handle_lsr_errors(port, &flag, &lsr))
				continue;
		if (uart_handle_sysrq_char(port, ch))
			continue;

		uart_insert_char(port, lsr, SPRD_LSR_OE, ch, flag);
	}

	tty_flip_buffer_push(tty);
}

static inline void sprd_tx(struct uart_port *port)
{
	u8 ch;

	uart_port_tx_limited(port, ch, THLD_TX_EMPTY,
		true,
		serial_out(port, SPRD_TXD, ch),
		({}));
}

/* this handles the interrupt from one port */
static irqreturn_t sprd_handle_irq(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	unsigned int ims;

	spin_lock(&port->lock);

	ims = serial_in(port, SPRD_IMSR);

	if (!ims) {
		spin_unlock(&port->lock);
		return IRQ_NONE;
	}

	if (ims & SPRD_IMSR_TIMEOUT)
		serial_out(port, SPRD_ICLR, SPRD_ICLR_TIMEOUT);

	if (ims & SPRD_IMSR_BREAK_DETECT)
		serial_out(port, SPRD_ICLR, SPRD_IMSR_BREAK_DETECT);

	if (ims & (SPRD_IMSR_RX_FIFO_FULL | SPRD_IMSR_BREAK_DETECT |
		   SPRD_IMSR_TIMEOUT))
		sprd_rx(port);

	if (ims & SPRD_IMSR_TX_FIFO_EMPTY)
		sprd_tx(port);

	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

static void sprd_uart_dma_startup(struct uart_port *port,
				  struct sprd_uart_port *sp)
{
	int ret;

	sprd_request_dma(port);
	if (!(sp->rx_dma.enable || sp->tx_dma.enable))
		return;

	ret = sprd_start_dma_rx(port);
	if (ret) {
		sp->rx_dma.enable = false;
		dma_release_channel(sp->rx_dma.chn);
		dev_warn(port->dev, "fail to start RX dma mode\n");
	}

	sprd_uart_dma_enable(port, true);
}

static int sprd_startup(struct uart_port *port)
{
	int ret = 0;
	unsigned int ien, fc;
	unsigned int timeout;
	struct sprd_uart_port *sp;
	unsigned long flags;

	serial_out(port, SPRD_CTL2,
		   THLD_TX_EMPTY << THLD_TX_EMPTY_SHIFT | THLD_RX_FULL);

	/* clear rx fifo */
	timeout = SPRD_TIMEOUT;
	while (timeout-- && serial_in(port, SPRD_STS1) & SPRD_RX_FIFO_CNT_MASK)
		serial_in(port, SPRD_RXD);

	/* clear tx fifo */
	timeout = SPRD_TIMEOUT;
	while (timeout-- && serial_in(port, SPRD_STS1) & SPRD_TX_FIFO_CNT_MASK)
		cpu_relax();

	/* clear interrupt */
	serial_out(port, SPRD_IEN, 0);
	serial_out(port, SPRD_ICLR, ~0);

	/* allocate irq */
	sp = container_of(port, struct sprd_uart_port, port);
	snprintf(sp->name, sizeof(sp->name), "sprd_serial%d", port->line);

	sprd_uart_dma_startup(port, sp);

	ret = devm_request_irq(port->dev, port->irq, sprd_handle_irq,
			       IRQF_SHARED, sp->name, port);
	if (ret) {
		dev_err(port->dev, "fail to request serial irq %d, ret=%d\n",
			port->irq, ret);
		return ret;
	}
	fc = serial_in(port, SPRD_CTL1);
	fc |= RX_TOUT_THLD_DEF | RX_HFC_THLD_DEF;
	serial_out(port, SPRD_CTL1, fc);

	/* enable interrupt */
	spin_lock_irqsave(&port->lock, flags);
	ien = serial_in(port, SPRD_IEN);
	ien |= SPRD_IEN_BREAK_DETECT | SPRD_IEN_TIMEOUT;
	if (!sp->rx_dma.enable)
		ien |= SPRD_IEN_RX_FULL;
	serial_out(port, SPRD_IEN, ien);
	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static void sprd_shutdown(struct uart_port *port)
{
	sprd_release_dma(port);
	serial_out(port, SPRD_IEN, 0);
	serial_out(port, SPRD_ICLR, ~0);
	devm_free_irq(port->dev, port->irq, port);
}

static void sprd_set_termios(struct uart_port *port, struct ktermios *termios,
		             const struct ktermios *old)
{
	unsigned int baud, quot;
	unsigned int lcr = 0, fc;
	unsigned long flags;

	/* ask the core to calculate the divisor for us */
	baud = uart_get_baud_rate(port, termios, old, 0, SPRD_BAUD_IO_LIMIT);

	quot = port->uartclk / baud;

	/* set data length */
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		lcr |= SPRD_LCR_DATA_LEN5;
		break;
	case CS6:
		lcr |= SPRD_LCR_DATA_LEN6;
		break;
	case CS7:
		lcr |= SPRD_LCR_DATA_LEN7;
		break;
	case CS8:
	default:
		lcr |= SPRD_LCR_DATA_LEN8;
		break;
	}

	/* calculate stop bits */
	lcr &= ~(SPRD_LCR_STOP_1BIT | SPRD_LCR_STOP_2BIT);
	if (termios->c_cflag & CSTOPB)
		lcr |= SPRD_LCR_STOP_2BIT;
	else
		lcr |= SPRD_LCR_STOP_1BIT;

	/* calculate parity */
	lcr &= ~SPRD_LCR_PARITY;
	termios->c_cflag &= ~CMSPAR;	/* no support mark/space */
	if (termios->c_cflag & PARENB) {
		lcr |= SPRD_LCR_PARITY_EN;
		if (termios->c_cflag & PARODD)
			lcr |= SPRD_LCR_ODD_PAR;
		else
			lcr |= SPRD_LCR_EVEN_PAR;
	}

	spin_lock_irqsave(&port->lock, flags);

	/* update the per-port timeout */
	uart_update_timeout(port, termios->c_cflag, baud);

	port->read_status_mask = SPRD_LSR_OE;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= SPRD_LSR_FE | SPRD_LSR_PE;
	if (termios->c_iflag & (IGNBRK | BRKINT | PARMRK))
		port->read_status_mask |= SPRD_LSR_BI;

	/* characters to ignore */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= SPRD_LSR_PE | SPRD_LSR_FE;
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= SPRD_LSR_BI;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= SPRD_LSR_OE;
	}

	/* flow control */
	fc = serial_in(port, SPRD_CTL1);
	fc &= ~(RX_HW_FLOW_CTL_THLD | RX_HW_FLOW_CTL_EN | TX_HW_FLOW_CTL_EN);
	if (termios->c_cflag & CRTSCTS) {
		fc |= RX_HW_FLOW_CTL_THLD;
		fc |= RX_HW_FLOW_CTL_EN;
		fc |= TX_HW_FLOW_CTL_EN;
	}

	/* clock divider bit0~bit15 */
	serial_out(port, SPRD_CLKD0, quot & SPRD_CLKD0_MASK);

	/* clock divider bit16~bit20 */
	serial_out(port, SPRD_CLKD1,
		   (quot & SPRD_CLKD1_MASK) >> SPRD_CLKD1_SHIFT);
	serial_out(port, SPRD_LCR, lcr);
	fc |= RX_TOUT_THLD_DEF | RX_HFC_THLD_DEF;
	serial_out(port, SPRD_CTL1, fc);

	spin_unlock_irqrestore(&port->lock, flags);

	/* Don't rewrite B0 */
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);
}

static const char *sprd_type(struct uart_port *port)
{
	return "SPX";
}

static void sprd_release_port(struct uart_port *port)
{
	/* nothing to do */
}

static int sprd_request_port(struct uart_port *port)
{
	return 0;
}

static void sprd_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_SPRD;
}

static int sprd_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	if (ser->type != PORT_SPRD)
		return -EINVAL;
	if (port->irq != ser->irq)
		return -EINVAL;
	if (port->iotype != ser->io_type)
		return -EINVAL;
	return 0;
}

static void sprd_pm(struct uart_port *port, unsigned int state,
		unsigned int oldstate)
{
	struct sprd_uart_port *sup =
		container_of(port, struct sprd_uart_port, port);

	switch (state) {
	case UART_PM_STATE_ON:
		clk_prepare_enable(sup->clk);
		break;
	case UART_PM_STATE_OFF:
		clk_disable_unprepare(sup->clk);
		break;
	}
}

#ifdef CONFIG_CONSOLE_POLL
static int sprd_poll_init(struct uart_port *port)
{
	if (port->state->pm_state != UART_PM_STATE_ON) {
		sprd_pm(port, UART_PM_STATE_ON, 0);
		port->state->pm_state = UART_PM_STATE_ON;
	}

	return 0;
}

static int sprd_poll_get_char(struct uart_port *port)
{
	while (!(serial_in(port, SPRD_STS1) & SPRD_RX_FIFO_CNT_MASK))
		cpu_relax();

	return serial_in(port, SPRD_RXD);
}

static void sprd_poll_put_char(struct uart_port *port, unsigned char ch)
{
	while (serial_in(port, SPRD_STS1) & SPRD_TX_FIFO_CNT_MASK)
		cpu_relax();

	serial_out(port, SPRD_TXD, ch);
}
#endif

static const struct uart_ops serial_sprd_ops = {
	.tx_empty = sprd_tx_empty,
	.get_mctrl = sprd_get_mctrl,
	.set_mctrl = sprd_set_mctrl,
	.stop_tx = sprd_stop_tx,
	.start_tx = sprd_start_tx,
	.stop_rx = sprd_stop_rx,
	.break_ctl = sprd_break_ctl,
	.startup = sprd_startup,
	.shutdown = sprd_shutdown,
	.set_termios = sprd_set_termios,
	.type = sprd_type,
	.release_port = sprd_release_port,
	.request_port = sprd_request_port,
	.config_port = sprd_config_port,
	.verify_port = sprd_verify_port,
	.pm = sprd_pm,
#ifdef CONFIG_CONSOLE_POLL
	.poll_init	= sprd_poll_init,
	.poll_get_char	= sprd_poll_get_char,
	.poll_put_char	= sprd_poll_put_char,
#endif
};

#ifdef CONFIG_SERIAL_SPRD_CONSOLE
static void wait_for_xmitr(struct uart_port *port)
{
	unsigned int status, tmout = 10000;

	/* wait up to 10ms for the character(s) to be sent */
	do {
		status = serial_in(port, SPRD_STS1);
		if (--tmout == 0)
			break;
		udelay(1);
	} while (status & SPRD_TX_FIFO_CNT_MASK);
}

static void sprd_console_putchar(struct uart_port *port, unsigned char ch)
{
	wait_for_xmitr(port);
	serial_out(port, SPRD_TXD, ch);
}

static void sprd_console_write(struct console *co, const char *s,
			       unsigned int count)
{
	struct uart_port *port = &sprd_port[co->index]->port;
	int locked = 1;
	unsigned long flags;

	if (port->sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock_irqsave(&port->lock, flags);
	else
		spin_lock_irqsave(&port->lock, flags);

	uart_console_write(port, s, count, sprd_console_putchar);

	/* wait for transmitter to become empty */
	wait_for_xmitr(port);

	if (locked)
		spin_unlock_irqrestore(&port->lock, flags);
}

static int sprd_console_setup(struct console *co, char *options)
{
	struct sprd_uart_port *sprd_uart_port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index >= UART_NR_MAX || co->index < 0)
		co->index = 0;

	sprd_uart_port = sprd_port[co->index];
	if (!sprd_uart_port || !sprd_uart_port->port.membase) {
		pr_info("serial port %d not yet initialized\n", co->index);
		return -ENODEV;
	}

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&sprd_uart_port->port, co, baud,
				parity, bits, flow);
}

static struct uart_driver sprd_uart_driver;
static struct console sprd_console = {
	.name = SPRD_TTY_NAME,
	.write = sprd_console_write,
	.device = uart_console_device,
	.setup = sprd_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.data = &sprd_uart_driver,
};

static int __init sprd_serial_console_init(void)
{
	register_console(&sprd_console);
	return 0;
}
console_initcall(sprd_serial_console_init);

#define SPRD_CONSOLE	(&sprd_console)

/* Support for earlycon */
static void sprd_putc(struct uart_port *port, unsigned char c)
{
	unsigned int timeout = SPRD_TIMEOUT;

	while (timeout-- &&
	       !(readl(port->membase + SPRD_LSR) & SPRD_LSR_TX_OVER))
		cpu_relax();

	writeb(c, port->membase + SPRD_TXD);
}

static void sprd_early_write(struct console *con, const char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, sprd_putc);
}

static int __init sprd_early_console_setup(struct earlycon_device *device,
					   const char *opt)
{
	if (!device->port.membase)
		return -ENODEV;

	device->con->write = sprd_early_write;
	return 0;
}
OF_EARLYCON_DECLARE(sprd_serial, "sprd,sc9836-uart",
		    sprd_early_console_setup);

#else /* !CONFIG_SERIAL_SPRD_CONSOLE */
#define SPRD_CONSOLE		NULL
#endif

static struct uart_driver sprd_uart_driver = {
	.owner = THIS_MODULE,
	.driver_name = "sprd_serial",
	.dev_name = SPRD_TTY_NAME,
	.major = 0,
	.minor = 0,
	.nr = UART_NR_MAX,
	.cons = SPRD_CONSOLE,
};

static int sprd_remove(struct platform_device *dev)
{
	struct sprd_uart_port *sup = platform_get_drvdata(dev);

	if (sup) {
		uart_remove_one_port(&sprd_uart_driver, &sup->port);
		sprd_port[sup->port.line] = NULL;
		sprd_rx_free_buf(sup);
		sprd_ports_num--;
	}

	if (!sprd_ports_num)
		uart_unregister_driver(&sprd_uart_driver);

	return 0;
}

static bool sprd_uart_is_console(struct uart_port *uport)
{
	struct console *cons = sprd_uart_driver.cons;

	if ((cons && cons->index >= 0 && cons->index == uport->line) ||
	    of_console_check(uport->dev->of_node, SPRD_TTY_NAME, uport->line))
		return true;

	return false;
}

static int sprd_clk_init(struct uart_port *uport)
{
	struct clk *clk_uart, *clk_parent;
	struct sprd_uart_port *u = sprd_port[uport->line];

	clk_uart = devm_clk_get(uport->dev, "uart");
	if (IS_ERR(clk_uart)) {
		dev_warn(uport->dev, "uart%d can't get uart clock\n",
			 uport->line);
		clk_uart = NULL;
	}

	clk_parent = devm_clk_get(uport->dev, "source");
	if (IS_ERR(clk_parent)) {
		dev_warn(uport->dev, "uart%d can't get source clock\n",
			 uport->line);
		clk_parent = NULL;
	}

	if (!clk_uart || clk_set_parent(clk_uart, clk_parent))
		uport->uartclk = SPRD_DEFAULT_SOURCE_CLK;
	else
		uport->uartclk = clk_get_rate(clk_uart);

	u->clk = devm_clk_get(uport->dev, "enable");
	if (IS_ERR(u->clk)) {
		if (PTR_ERR(u->clk) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		dev_warn(uport->dev, "uart%d can't get enable clock\n",
			uport->line);

		/* To keep console alive even if the error occurred */
		if (!sprd_uart_is_console(uport))
			return PTR_ERR(u->clk);

		u->clk = NULL;
	}

	return 0;
}

static int sprd_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct uart_port *up;
	int irq;
	int index;
	int ret;

	index = of_alias_get_id(pdev->dev.of_node, "serial");
	if (index < 0 || index >= ARRAY_SIZE(sprd_port)) {
		dev_err(&pdev->dev, "got a wrong serial alias id %d\n", index);
		return -EINVAL;
	}

	sprd_port[index] = devm_kzalloc(&pdev->dev, sizeof(*sprd_port[index]),
					GFP_KERNEL);
	if (!sprd_port[index])
		return -ENOMEM;

	up = &sprd_port[index]->port;
	up->dev = &pdev->dev;
	up->line = index;
	up->type = PORT_SPRD;
	up->iotype = UPIO_MEM;
	up->uartclk = SPRD_DEF_RATE;
	up->fifosize = SPRD_FIFO_SIZE;
	up->ops = &serial_sprd_ops;
	up->flags = UPF_BOOT_AUTOCONF;
	up->has_sysrq = IS_ENABLED(CONFIG_SERIAL_SPRD_CONSOLE);

	ret = sprd_clk_init(up);
	if (ret)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	up->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(up->membase))
		return PTR_ERR(up->membase);

	up->mapbase = res->start;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	up->irq = irq;

	/*
	 * Allocate one dma buffer to prepare for receive transfer, in case
	 * memory allocation failure at runtime.
	 */
	ret = sprd_rx_alloc_buf(sprd_port[index]);
	if (ret)
		return ret;

	if (!sprd_ports_num) {
		ret = uart_register_driver(&sprd_uart_driver);
		if (ret < 0) {
			pr_err("Failed to register SPRD-UART driver\n");
			return ret;
		}
	}
	sprd_ports_num++;

	ret = uart_add_one_port(&sprd_uart_driver, up);
	if (ret)
		sprd_remove(pdev);

	platform_set_drvdata(pdev, up);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int sprd_suspend(struct device *dev)
{
	struct sprd_uart_port *sup = dev_get_drvdata(dev);

	uart_suspend_port(&sprd_uart_driver, &sup->port);

	return 0;
}

static int sprd_resume(struct device *dev)
{
	struct sprd_uart_port *sup = dev_get_drvdata(dev);

	uart_resume_port(&sprd_uart_driver, &sup->port);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(sprd_pm_ops, sprd_suspend, sprd_resume);

static const struct of_device_id serial_ids[] = {
	{.compatible = "sprd,sc9836-uart",},
	{}
};
MODULE_DEVICE_TABLE(of, serial_ids);

static struct platform_driver sprd_platform_driver = {
	.probe		= sprd_probe,
	.remove		= sprd_remove,
	.driver		= {
		.name	= "sprd_serial",
		.of_match_table = serial_ids,
		.pm	= &sprd_pm_ops,
	},
};

module_platform_driver(sprd_platform_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Spreadtrum SoC serial driver series");
