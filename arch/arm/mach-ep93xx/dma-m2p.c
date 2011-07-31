/*
 * arch/arm/mach-ep93xx/dma-m2p.c
 * M2P DMA handling for Cirrus EP93xx chips.
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 * Copyright (C) 2006 Applied Data Systems
 *
 * Copyright (C) 2009 Ryan Mallon <ryan@bluewatersys.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

/*
 * On the EP93xx chip the following peripherals my be allocated to the 10
 * Memory to Internal Peripheral (M2P) channels (5 transmit + 5 receive).
 *
 *	I2S	contains 3 Tx and 3 Rx DMA Channels
 *	AAC	contains 3 Tx and 3 Rx DMA Channels
 *	UART1	contains 1 Tx and 1 Rx DMA Channels
 *	UART2	contains 1 Tx and 1 Rx DMA Channels
 *	UART3	contains 1 Tx and 1 Rx DMA Channels
 *	IrDA	contains 1 Tx and 1 Rx DMA Channels
 *
 * SSP and IDE use the Memory to Memory (M2M) channels and are not covered
 * with this implementation.
 */

#define pr_fmt(fmt) "ep93xx " KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/io.h>

#include <mach/dma.h>
#include <mach/hardware.h>

#define M2P_CONTROL			0x00
#define  M2P_CONTROL_STALL_IRQ_EN	(1 << 0)
#define  M2P_CONTROL_NFB_IRQ_EN		(1 << 1)
#define  M2P_CONTROL_ERROR_IRQ_EN	(1 << 3)
#define  M2P_CONTROL_ENABLE		(1 << 4)
#define M2P_INTERRUPT			0x04
#define  M2P_INTERRUPT_STALL		(1 << 0)
#define  M2P_INTERRUPT_NFB		(1 << 1)
#define  M2P_INTERRUPT_ERROR		(1 << 3)
#define M2P_PPALLOC			0x08
#define M2P_STATUS			0x0c
#define M2P_REMAIN			0x14
#define M2P_MAXCNT0			0x20
#define M2P_BASE0			0x24
#define M2P_MAXCNT1			0x30
#define M2P_BASE1			0x34

#define STATE_IDLE	0	/* Channel is inactive.  */
#define STATE_STALL	1	/* Channel is active, no buffers pending.  */
#define STATE_ON	2	/* Channel is active, one buffer pending.  */
#define STATE_NEXT	3	/* Channel is active, two buffers pending.  */

struct m2p_channel {
	char				*name;
	void __iomem			*base;
	int				irq;

	struct clk			*clk;
	spinlock_t			lock;

	void				*client;
	unsigned			next_slot:1;
	struct ep93xx_dma_buffer	*buffer_xfer;
	struct ep93xx_dma_buffer	*buffer_next;
	struct list_head		buffers_pending;
};

static struct m2p_channel m2p_rx[] = {
	{"m2p1", EP93XX_DMA_BASE + 0x0040, IRQ_EP93XX_DMAM2P1},
	{"m2p3", EP93XX_DMA_BASE + 0x00c0, IRQ_EP93XX_DMAM2P3},
	{"m2p5", EP93XX_DMA_BASE + 0x0200, IRQ_EP93XX_DMAM2P5},
	{"m2p7", EP93XX_DMA_BASE + 0x0280, IRQ_EP93XX_DMAM2P7},
	{"m2p9", EP93XX_DMA_BASE + 0x0300, IRQ_EP93XX_DMAM2P9},
	{NULL},
};

static struct m2p_channel m2p_tx[] = {
	{"m2p0", EP93XX_DMA_BASE + 0x0000, IRQ_EP93XX_DMAM2P0},
	{"m2p2", EP93XX_DMA_BASE + 0x0080, IRQ_EP93XX_DMAM2P2},
	{"m2p4", EP93XX_DMA_BASE + 0x0240, IRQ_EP93XX_DMAM2P4},
	{"m2p6", EP93XX_DMA_BASE + 0x02c0, IRQ_EP93XX_DMAM2P6},
	{"m2p8", EP93XX_DMA_BASE + 0x0340, IRQ_EP93XX_DMAM2P8},
	{NULL},
};

static void feed_buf(struct m2p_channel *ch, struct ep93xx_dma_buffer *buf)
{
	if (ch->next_slot == 0) {
		writel(buf->size, ch->base + M2P_MAXCNT0);
		writel(buf->bus_addr, ch->base + M2P_BASE0);
	} else {
		writel(buf->size, ch->base + M2P_MAXCNT1);
		writel(buf->bus_addr, ch->base + M2P_BASE1);
	}
	ch->next_slot ^= 1;
}

static void choose_buffer_xfer(struct m2p_channel *ch)
{
	struct ep93xx_dma_buffer *buf;

	ch->buffer_xfer = NULL;
	if (!list_empty(&ch->buffers_pending)) {
		buf = list_entry(ch->buffers_pending.next,
				 struct ep93xx_dma_buffer, list);
		list_del(&buf->list);
		feed_buf(ch, buf);
		ch->buffer_xfer = buf;
	}
}

static void choose_buffer_next(struct m2p_channel *ch)
{
	struct ep93xx_dma_buffer *buf;

	ch->buffer_next = NULL;
	if (!list_empty(&ch->buffers_pending)) {
		buf = list_entry(ch->buffers_pending.next,
				 struct ep93xx_dma_buffer, list);
		list_del(&buf->list);
		feed_buf(ch, buf);
		ch->buffer_next = buf;
	}
}

static inline void m2p_set_control(struct m2p_channel *ch, u32 v)
{
	/*
	 * The control register must be read immediately after being written so
	 * that the internal state machine is correctly updated. See the ep93xx
	 * users' guide for details.
	 */
	writel(v, ch->base + M2P_CONTROL);
	readl(ch->base + M2P_CONTROL);
}

static inline int m2p_channel_state(struct m2p_channel *ch)
{
	return (readl(ch->base + M2P_STATUS) >> 4) & 0x3;
}

static irqreturn_t m2p_irq(int irq, void *dev_id)
{
	struct m2p_channel *ch = dev_id;
	struct ep93xx_dma_m2p_client *cl;
	u32 irq_status, v;
	int error = 0;

	cl = ch->client;

	spin_lock(&ch->lock);
	irq_status = readl(ch->base + M2P_INTERRUPT);

	if (irq_status & M2P_INTERRUPT_ERROR) {
		writel(M2P_INTERRUPT_ERROR, ch->base + M2P_INTERRUPT);
		error = 1;
	}

	if ((irq_status & (M2P_INTERRUPT_STALL | M2P_INTERRUPT_NFB)) == 0) {
		spin_unlock(&ch->lock);
		return IRQ_NONE;
	}

	switch (m2p_channel_state(ch)) {
	case STATE_IDLE:
		pr_crit("dma interrupt without a dma buffer\n");
		BUG();
		break;

	case STATE_STALL:
		cl->buffer_finished(cl->cookie, ch->buffer_xfer, 0, error);
		if (ch->buffer_next != NULL) {
			cl->buffer_finished(cl->cookie, ch->buffer_next,
					    0, error);
		}
		choose_buffer_xfer(ch);
		choose_buffer_next(ch);
		if (ch->buffer_xfer != NULL)
			cl->buffer_started(cl->cookie, ch->buffer_xfer);
		break;

	case STATE_ON:
		cl->buffer_finished(cl->cookie, ch->buffer_xfer, 0, error);
		ch->buffer_xfer = ch->buffer_next;
		choose_buffer_next(ch);
		cl->buffer_started(cl->cookie, ch->buffer_xfer);
		break;

	case STATE_NEXT:
		pr_crit("dma interrupt while next\n");
		BUG();
		break;
	}

	v = readl(ch->base + M2P_CONTROL) & ~(M2P_CONTROL_STALL_IRQ_EN |
					      M2P_CONTROL_NFB_IRQ_EN);
	if (ch->buffer_xfer != NULL)
		v |= M2P_CONTROL_STALL_IRQ_EN;
	if (ch->buffer_next != NULL)
		v |= M2P_CONTROL_NFB_IRQ_EN;
	m2p_set_control(ch, v);

	spin_unlock(&ch->lock);
	return IRQ_HANDLED;
}

static struct m2p_channel *find_free_channel(struct ep93xx_dma_m2p_client *cl)
{
	struct m2p_channel *ch;
	int i;

	if (cl->flags & EP93XX_DMA_M2P_RX)
		ch = m2p_rx;
	else
		ch = m2p_tx;

	for (i = 0; ch[i].base; i++) {
		struct ep93xx_dma_m2p_client *client;

		client = ch[i].client;
		if (client != NULL) {
			int port;

			port = cl->flags & EP93XX_DMA_M2P_PORT_MASK;
			if (port == (client->flags &
				     EP93XX_DMA_M2P_PORT_MASK)) {
				pr_warning("DMA channel already used by %s\n",
					   cl->name ? : "unknown client");
				return ERR_PTR(-EBUSY);
			}
		}
	}

	for (i = 0; ch[i].base; i++) {
		if (ch[i].client == NULL)
			return ch + i;
	}

	pr_warning("No free DMA channel for %s\n",
		   cl->name ? : "unknown client");
	return ERR_PTR(-ENODEV);
}

static void channel_enable(struct m2p_channel *ch)
{
	struct ep93xx_dma_m2p_client *cl = ch->client;
	u32 v;

	clk_enable(ch->clk);

	v = cl->flags & EP93XX_DMA_M2P_PORT_MASK;
	writel(v, ch->base + M2P_PPALLOC);

	v = cl->flags & EP93XX_DMA_M2P_ERROR_MASK;
	v |= M2P_CONTROL_ENABLE | M2P_CONTROL_ERROR_IRQ_EN;
	m2p_set_control(ch, v);
}

static void channel_disable(struct m2p_channel *ch)
{
	u32 v;

	v = readl(ch->base + M2P_CONTROL);
	v &= ~(M2P_CONTROL_STALL_IRQ_EN | M2P_CONTROL_NFB_IRQ_EN);
	m2p_set_control(ch, v);

	while (m2p_channel_state(ch) >= STATE_ON)
		cpu_relax();

	m2p_set_control(ch, 0x0);

	while (m2p_channel_state(ch) == STATE_STALL)
		cpu_relax();

	clk_disable(ch->clk);
}

int ep93xx_dma_m2p_client_register(struct ep93xx_dma_m2p_client *cl)
{
	struct m2p_channel *ch;
	int err;

	ch = find_free_channel(cl);
	if (IS_ERR(ch))
		return PTR_ERR(ch);

	err = request_irq(ch->irq, m2p_irq, 0, cl->name ? : "dma-m2p", ch);
	if (err)
		return err;

	ch->client = cl;
	ch->next_slot = 0;
	ch->buffer_xfer = NULL;
	ch->buffer_next = NULL;
	INIT_LIST_HEAD(&ch->buffers_pending);

	cl->channel = ch;

	channel_enable(ch);

	return 0;
}
EXPORT_SYMBOL_GPL(ep93xx_dma_m2p_client_register);

void ep93xx_dma_m2p_client_unregister(struct ep93xx_dma_m2p_client *cl)
{
	struct m2p_channel *ch = cl->channel;

	channel_disable(ch);
	free_irq(ch->irq, ch);
	ch->client = NULL;
}
EXPORT_SYMBOL_GPL(ep93xx_dma_m2p_client_unregister);

void ep93xx_dma_m2p_submit(struct ep93xx_dma_m2p_client *cl,
			   struct ep93xx_dma_buffer *buf)
{
	struct m2p_channel *ch = cl->channel;
	unsigned long flags;
	u32 v;

	spin_lock_irqsave(&ch->lock, flags);
	v = readl(ch->base + M2P_CONTROL);
	if (ch->buffer_xfer == NULL) {
		ch->buffer_xfer = buf;
		feed_buf(ch, buf);
		cl->buffer_started(cl->cookie, buf);

		v |= M2P_CONTROL_STALL_IRQ_EN;
		m2p_set_control(ch, v);

	} else if (ch->buffer_next == NULL) {
		ch->buffer_next = buf;
		feed_buf(ch, buf);

		v |= M2P_CONTROL_NFB_IRQ_EN;
		m2p_set_control(ch, v);
	} else {
		list_add_tail(&buf->list, &ch->buffers_pending);
	}
	spin_unlock_irqrestore(&ch->lock, flags);
}
EXPORT_SYMBOL_GPL(ep93xx_dma_m2p_submit);

void ep93xx_dma_m2p_submit_recursive(struct ep93xx_dma_m2p_client *cl,
				     struct ep93xx_dma_buffer *buf)
{
	struct m2p_channel *ch = cl->channel;

	list_add_tail(&buf->list, &ch->buffers_pending);
}
EXPORT_SYMBOL_GPL(ep93xx_dma_m2p_submit_recursive);

void ep93xx_dma_m2p_flush(struct ep93xx_dma_m2p_client *cl)
{
	struct m2p_channel *ch = cl->channel;

	channel_disable(ch);
	ch->next_slot = 0;
	ch->buffer_xfer = NULL;
	ch->buffer_next = NULL;
	INIT_LIST_HEAD(&ch->buffers_pending);
	channel_enable(ch);
}
EXPORT_SYMBOL_GPL(ep93xx_dma_m2p_flush);

static int init_channel(struct m2p_channel *ch)
{
	ch->clk = clk_get(NULL, ch->name);
	if (IS_ERR(ch->clk))
		return PTR_ERR(ch->clk);

	spin_lock_init(&ch->lock);
	ch->client = NULL;

	return 0;
}

static int __init ep93xx_dma_m2p_init(void)
{
	int i;
	int ret;

	for (i = 0; m2p_rx[i].base; i++) {
		ret = init_channel(m2p_rx + i);
		if (ret)
			return ret;
	}

	for (i = 0; m2p_tx[i].base; i++) {
		ret = init_channel(m2p_tx + i);
		if (ret)
			return ret;
	}

	pr_info("M2P DMA subsystem initialized\n");
	return 0;
}
arch_initcall(ep93xx_dma_m2p_init);
