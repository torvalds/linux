// SPDX-License-Identifier: GPL-2.0-only
/*
 * DMA driver for Nvidia's Tegra20 APB DMA controller.
 *
 * Copyright (c) 2012-2013, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "dmaengine.h"

#define CREATE_TRACE_POINTS
#include <trace/events/tegra_apb_dma.h>

#define TEGRA_APBDMA_GENERAL			0x0
#define TEGRA_APBDMA_GENERAL_ENABLE		BIT(31)

#define TEGRA_APBDMA_CONTROL			0x010
#define TEGRA_APBDMA_IRQ_MASK			0x01c
#define TEGRA_APBDMA_IRQ_MASK_SET		0x020

/* CSR register */
#define TEGRA_APBDMA_CHAN_CSR			0x00
#define TEGRA_APBDMA_CSR_ENB			BIT(31)
#define TEGRA_APBDMA_CSR_IE_EOC			BIT(30)
#define TEGRA_APBDMA_CSR_HOLD			BIT(29)
#define TEGRA_APBDMA_CSR_DIR			BIT(28)
#define TEGRA_APBDMA_CSR_ONCE			BIT(27)
#define TEGRA_APBDMA_CSR_FLOW			BIT(21)
#define TEGRA_APBDMA_CSR_REQ_SEL_SHIFT		16
#define TEGRA_APBDMA_CSR_REQ_SEL_MASK		0x1F
#define TEGRA_APBDMA_CSR_WCOUNT_MASK		0xFFFC

/* STATUS register */
#define TEGRA_APBDMA_CHAN_STATUS		0x004
#define TEGRA_APBDMA_STATUS_BUSY		BIT(31)
#define TEGRA_APBDMA_STATUS_ISE_EOC		BIT(30)
#define TEGRA_APBDMA_STATUS_HALT		BIT(29)
#define TEGRA_APBDMA_STATUS_PING_PONG		BIT(28)
#define TEGRA_APBDMA_STATUS_COUNT_SHIFT		2
#define TEGRA_APBDMA_STATUS_COUNT_MASK		0xFFFC

#define TEGRA_APBDMA_CHAN_CSRE			0x00C
#define TEGRA_APBDMA_CHAN_CSRE_PAUSE		(1 << 31)

/* AHB memory address */
#define TEGRA_APBDMA_CHAN_AHBPTR		0x010

/* AHB sequence register */
#define TEGRA_APBDMA_CHAN_AHBSEQ		0x14
#define TEGRA_APBDMA_AHBSEQ_INTR_ENB		BIT(31)
#define TEGRA_APBDMA_AHBSEQ_BUS_WIDTH_8		(0 << 28)
#define TEGRA_APBDMA_AHBSEQ_BUS_WIDTH_16	(1 << 28)
#define TEGRA_APBDMA_AHBSEQ_BUS_WIDTH_32	(2 << 28)
#define TEGRA_APBDMA_AHBSEQ_BUS_WIDTH_64	(3 << 28)
#define TEGRA_APBDMA_AHBSEQ_BUS_WIDTH_128	(4 << 28)
#define TEGRA_APBDMA_AHBSEQ_DATA_SWAP		BIT(27)
#define TEGRA_APBDMA_AHBSEQ_BURST_1		(4 << 24)
#define TEGRA_APBDMA_AHBSEQ_BURST_4		(5 << 24)
#define TEGRA_APBDMA_AHBSEQ_BURST_8		(6 << 24)
#define TEGRA_APBDMA_AHBSEQ_DBL_BUF		BIT(19)
#define TEGRA_APBDMA_AHBSEQ_WRAP_SHIFT		16
#define TEGRA_APBDMA_AHBSEQ_WRAP_NONE		0

/* APB address */
#define TEGRA_APBDMA_CHAN_APBPTR		0x018

/* APB sequence register */
#define TEGRA_APBDMA_CHAN_APBSEQ		0x01c
#define TEGRA_APBDMA_APBSEQ_BUS_WIDTH_8		(0 << 28)
#define TEGRA_APBDMA_APBSEQ_BUS_WIDTH_16	(1 << 28)
#define TEGRA_APBDMA_APBSEQ_BUS_WIDTH_32	(2 << 28)
#define TEGRA_APBDMA_APBSEQ_BUS_WIDTH_64	(3 << 28)
#define TEGRA_APBDMA_APBSEQ_BUS_WIDTH_128	(4 << 28)
#define TEGRA_APBDMA_APBSEQ_DATA_SWAP		BIT(27)
#define TEGRA_APBDMA_APBSEQ_WRAP_WORD_1		(1 << 16)

/* Tegra148 specific registers */
#define TEGRA_APBDMA_CHAN_WCOUNT		0x20

#define TEGRA_APBDMA_CHAN_WORD_TRANSFER		0x24

/*
 * If any burst is in flight and DMA paused then this is the time to complete
 * on-flight burst and update DMA status register.
 */
#define TEGRA_APBDMA_BURST_COMPLETE_TIME	20

/* Channel base address offset from APBDMA base address */
#define TEGRA_APBDMA_CHANNEL_BASE_ADD_OFFSET	0x1000

#define TEGRA_APBDMA_SLAVE_ID_INVALID	(TEGRA_APBDMA_CSR_REQ_SEL_MASK + 1)

struct tegra_dma;

/*
 * tegra_dma_chip_data Tegra chip specific DMA data
 * @nr_channels: Number of channels available in the controller.
 * @channel_reg_size: Channel register size/stride.
 * @max_dma_count: Maximum DMA transfer count supported by DMA controller.
 * @support_channel_pause: Support channel wise pause of dma.
 * @support_separate_wcount_reg: Support separate word count register.
 */
struct tegra_dma_chip_data {
	int nr_channels;
	int channel_reg_size;
	int max_dma_count;
	bool support_channel_pause;
	bool support_separate_wcount_reg;
};

/* DMA channel registers */
struct tegra_dma_channel_regs {
	unsigned long	csr;
	unsigned long	ahb_ptr;
	unsigned long	apb_ptr;
	unsigned long	ahb_seq;
	unsigned long	apb_seq;
	unsigned long	wcount;
};

/*
 * tegra_dma_sg_req: DMA request details to configure hardware. This
 * contains the details for one transfer to configure DMA hw.
 * The client's request for data transfer can be broken into multiple
 * sub-transfer as per requester details and hw support.
 * This sub transfer get added in the list of transfer and point to Tegra
 * DMA descriptor which manages the transfer details.
 */
struct tegra_dma_sg_req {
	struct tegra_dma_channel_regs	ch_regs;
	unsigned int			req_len;
	bool				configured;
	bool				last_sg;
	struct list_head		node;
	struct tegra_dma_desc		*dma_desc;
	unsigned int			words_xferred;
};

/*
 * tegra_dma_desc: Tegra DMA descriptors which manages the client requests.
 * This descriptor keep track of transfer status, callbacks and request
 * counts etc.
 */
struct tegra_dma_desc {
	struct dma_async_tx_descriptor	txd;
	unsigned int			bytes_requested;
	unsigned int			bytes_transferred;
	enum dma_status			dma_status;
	struct list_head		node;
	struct list_head		tx_list;
	struct list_head		cb_node;
	int				cb_count;
};

struct tegra_dma_channel;

typedef void (*dma_isr_handler)(struct tegra_dma_channel *tdc,
				bool to_terminate);

/* tegra_dma_channel: Channel specific information */
struct tegra_dma_channel {
	struct dma_chan		dma_chan;
	char			name[12];
	bool			config_init;
	int			id;
	int			irq;
	void __iomem		*chan_addr;
	spinlock_t		lock;
	bool			busy;
	struct tegra_dma	*tdma;
	bool			cyclic;

	/* Different lists for managing the requests */
	struct list_head	free_sg_req;
	struct list_head	pending_sg_req;
	struct list_head	free_dma_desc;
	struct list_head	cb_desc;

	/* ISR handler and tasklet for bottom half of isr handling */
	dma_isr_handler		isr_handler;
	struct tasklet_struct	tasklet;

	/* Channel-slave specific configuration */
	unsigned int slave_id;
	struct dma_slave_config dma_sconfig;
	struct tegra_dma_channel_regs	channel_reg;
};

/* tegra_dma: Tegra DMA specific information */
struct tegra_dma {
	struct dma_device		dma_dev;
	struct device			*dev;
	struct clk			*dma_clk;
	struct reset_control		*rst;
	spinlock_t			global_lock;
	void __iomem			*base_addr;
	const struct tegra_dma_chip_data *chip_data;

	/*
	 * Counter for managing global pausing of the DMA controller.
	 * Only applicable for devices that don't support individual
	 * channel pausing.
	 */
	u32				global_pause_count;

	/* Some register need to be cache before suspend */
	u32				reg_gen;

	/* Last member of the structure */
	struct tegra_dma_channel channels[0];
};

static inline void tdma_write(struct tegra_dma *tdma, u32 reg, u32 val)
{
	writel(val, tdma->base_addr + reg);
}

static inline u32 tdma_read(struct tegra_dma *tdma, u32 reg)
{
	return readl(tdma->base_addr + reg);
}

static inline void tdc_write(struct tegra_dma_channel *tdc,
		u32 reg, u32 val)
{
	writel(val, tdc->chan_addr + reg);
}

static inline u32 tdc_read(struct tegra_dma_channel *tdc, u32 reg)
{
	return readl(tdc->chan_addr + reg);
}

static inline struct tegra_dma_channel *to_tegra_dma_chan(struct dma_chan *dc)
{
	return container_of(dc, struct tegra_dma_channel, dma_chan);
}

static inline struct tegra_dma_desc *txd_to_tegra_dma_desc(
		struct dma_async_tx_descriptor *td)
{
	return container_of(td, struct tegra_dma_desc, txd);
}

static inline struct device *tdc2dev(struct tegra_dma_channel *tdc)
{
	return &tdc->dma_chan.dev->device;
}

static dma_cookie_t tegra_dma_tx_submit(struct dma_async_tx_descriptor *tx);
static int tegra_dma_runtime_suspend(struct device *dev);
static int tegra_dma_runtime_resume(struct device *dev);

/* Get DMA desc from free list, if not there then allocate it.  */
static struct tegra_dma_desc *tegra_dma_desc_get(
		struct tegra_dma_channel *tdc)
{
	struct tegra_dma_desc *dma_desc;
	unsigned long flags;

	spin_lock_irqsave(&tdc->lock, flags);

	/* Do not allocate if desc are waiting for ack */
	list_for_each_entry(dma_desc, &tdc->free_dma_desc, node) {
		if (async_tx_test_ack(&dma_desc->txd)) {
			list_del(&dma_desc->node);
			spin_unlock_irqrestore(&tdc->lock, flags);
			dma_desc->txd.flags = 0;
			return dma_desc;
		}
	}

	spin_unlock_irqrestore(&tdc->lock, flags);

	/* Allocate DMA desc */
	dma_desc = kzalloc(sizeof(*dma_desc), GFP_NOWAIT);
	if (!dma_desc)
		return NULL;

	dma_async_tx_descriptor_init(&dma_desc->txd, &tdc->dma_chan);
	dma_desc->txd.tx_submit = tegra_dma_tx_submit;
	dma_desc->txd.flags = 0;
	return dma_desc;
}

static void tegra_dma_desc_put(struct tegra_dma_channel *tdc,
		struct tegra_dma_desc *dma_desc)
{
	unsigned long flags;

	spin_lock_irqsave(&tdc->lock, flags);
	if (!list_empty(&dma_desc->tx_list))
		list_splice_init(&dma_desc->tx_list, &tdc->free_sg_req);
	list_add_tail(&dma_desc->node, &tdc->free_dma_desc);
	spin_unlock_irqrestore(&tdc->lock, flags);
}

static struct tegra_dma_sg_req *tegra_dma_sg_req_get(
		struct tegra_dma_channel *tdc)
{
	struct tegra_dma_sg_req *sg_req = NULL;
	unsigned long flags;

	spin_lock_irqsave(&tdc->lock, flags);
	if (!list_empty(&tdc->free_sg_req)) {
		sg_req = list_first_entry(&tdc->free_sg_req,
					typeof(*sg_req), node);
		list_del(&sg_req->node);
		spin_unlock_irqrestore(&tdc->lock, flags);
		return sg_req;
	}
	spin_unlock_irqrestore(&tdc->lock, flags);

	sg_req = kzalloc(sizeof(struct tegra_dma_sg_req), GFP_NOWAIT);

	return sg_req;
}

static int tegra_dma_slave_config(struct dma_chan *dc,
		struct dma_slave_config *sconfig)
{
	struct tegra_dma_channel *tdc = to_tegra_dma_chan(dc);

	if (!list_empty(&tdc->pending_sg_req)) {
		dev_err(tdc2dev(tdc), "Configuration not allowed\n");
		return -EBUSY;
	}

	memcpy(&tdc->dma_sconfig, sconfig, sizeof(*sconfig));
	if (tdc->slave_id == TEGRA_APBDMA_SLAVE_ID_INVALID &&
	    sconfig->device_fc) {
		if (sconfig->slave_id > TEGRA_APBDMA_CSR_REQ_SEL_MASK)
			return -EINVAL;
		tdc->slave_id = sconfig->slave_id;
	}
	tdc->config_init = true;
	return 0;
}

static void tegra_dma_global_pause(struct tegra_dma_channel *tdc,
	bool wait_for_burst_complete)
{
	struct tegra_dma *tdma = tdc->tdma;

	spin_lock(&tdma->global_lock);

	if (tdc->tdma->global_pause_count == 0) {
		tdma_write(tdma, TEGRA_APBDMA_GENERAL, 0);
		if (wait_for_burst_complete)
			udelay(TEGRA_APBDMA_BURST_COMPLETE_TIME);
	}

	tdc->tdma->global_pause_count++;

	spin_unlock(&tdma->global_lock);
}

static void tegra_dma_global_resume(struct tegra_dma_channel *tdc)
{
	struct tegra_dma *tdma = tdc->tdma;

	spin_lock(&tdma->global_lock);

	if (WARN_ON(tdc->tdma->global_pause_count == 0))
		goto out;

	if (--tdc->tdma->global_pause_count == 0)
		tdma_write(tdma, TEGRA_APBDMA_GENERAL,
			   TEGRA_APBDMA_GENERAL_ENABLE);

out:
	spin_unlock(&tdma->global_lock);
}

static void tegra_dma_pause(struct tegra_dma_channel *tdc,
	bool wait_for_burst_complete)
{
	struct tegra_dma *tdma = tdc->tdma;

	if (tdma->chip_data->support_channel_pause) {
		tdc_write(tdc, TEGRA_APBDMA_CHAN_CSRE,
				TEGRA_APBDMA_CHAN_CSRE_PAUSE);
		if (wait_for_burst_complete)
			udelay(TEGRA_APBDMA_BURST_COMPLETE_TIME);
	} else {
		tegra_dma_global_pause(tdc, wait_for_burst_complete);
	}
}

static void tegra_dma_resume(struct tegra_dma_channel *tdc)
{
	struct tegra_dma *tdma = tdc->tdma;

	if (tdma->chip_data->support_channel_pause) {
		tdc_write(tdc, TEGRA_APBDMA_CHAN_CSRE, 0);
	} else {
		tegra_dma_global_resume(tdc);
	}
}

static void tegra_dma_stop(struct tegra_dma_channel *tdc)
{
	u32 csr;
	u32 status;

	/* Disable interrupts */
	csr = tdc_read(tdc, TEGRA_APBDMA_CHAN_CSR);
	csr &= ~TEGRA_APBDMA_CSR_IE_EOC;
	tdc_write(tdc, TEGRA_APBDMA_CHAN_CSR, csr);

	/* Disable DMA */
	csr &= ~TEGRA_APBDMA_CSR_ENB;
	tdc_write(tdc, TEGRA_APBDMA_CHAN_CSR, csr);

	/* Clear interrupt status if it is there */
	status = tdc_read(tdc, TEGRA_APBDMA_CHAN_STATUS);
	if (status & TEGRA_APBDMA_STATUS_ISE_EOC) {
		dev_dbg(tdc2dev(tdc), "%s():clearing interrupt\n", __func__);
		tdc_write(tdc, TEGRA_APBDMA_CHAN_STATUS, status);
	}
	tdc->busy = false;
}

static void tegra_dma_start(struct tegra_dma_channel *tdc,
		struct tegra_dma_sg_req *sg_req)
{
	struct tegra_dma_channel_regs *ch_regs = &sg_req->ch_regs;

	tdc_write(tdc, TEGRA_APBDMA_CHAN_CSR, ch_regs->csr);
	tdc_write(tdc, TEGRA_APBDMA_CHAN_APBSEQ, ch_regs->apb_seq);
	tdc_write(tdc, TEGRA_APBDMA_CHAN_APBPTR, ch_regs->apb_ptr);
	tdc_write(tdc, TEGRA_APBDMA_CHAN_AHBSEQ, ch_regs->ahb_seq);
	tdc_write(tdc, TEGRA_APBDMA_CHAN_AHBPTR, ch_regs->ahb_ptr);
	if (tdc->tdma->chip_data->support_separate_wcount_reg)
		tdc_write(tdc, TEGRA_APBDMA_CHAN_WCOUNT, ch_regs->wcount);

	/* Start DMA */
	tdc_write(tdc, TEGRA_APBDMA_CHAN_CSR,
				ch_regs->csr | TEGRA_APBDMA_CSR_ENB);
}

static void tegra_dma_configure_for_next(struct tegra_dma_channel *tdc,
		struct tegra_dma_sg_req *nsg_req)
{
	unsigned long status;

	/*
	 * The DMA controller reloads the new configuration for next transfer
	 * after last burst of current transfer completes.
	 * If there is no IEC status then this makes sure that last burst
	 * has not be completed. There may be case that last burst is on
	 * flight and so it can complete but because DMA is paused, it
	 * will not generates interrupt as well as not reload the new
	 * configuration.
	 * If there is already IEC status then interrupt handler need to
	 * load new configuration.
	 */
	tegra_dma_pause(tdc, false);
	status = tdc_read(tdc, TEGRA_APBDMA_CHAN_STATUS);

	/*
	 * If interrupt is pending then do nothing as the ISR will handle
	 * the programing for new request.
	 */
	if (status & TEGRA_APBDMA_STATUS_ISE_EOC) {
		dev_err(tdc2dev(tdc),
			"Skipping new configuration as interrupt is pending\n");
		tegra_dma_resume(tdc);
		return;
	}

	/* Safe to program new configuration */
	tdc_write(tdc, TEGRA_APBDMA_CHAN_APBPTR, nsg_req->ch_regs.apb_ptr);
	tdc_write(tdc, TEGRA_APBDMA_CHAN_AHBPTR, nsg_req->ch_regs.ahb_ptr);
	if (tdc->tdma->chip_data->support_separate_wcount_reg)
		tdc_write(tdc, TEGRA_APBDMA_CHAN_WCOUNT,
						nsg_req->ch_regs.wcount);
	tdc_write(tdc, TEGRA_APBDMA_CHAN_CSR,
				nsg_req->ch_regs.csr | TEGRA_APBDMA_CSR_ENB);
	nsg_req->configured = true;
	nsg_req->words_xferred = 0;

	tegra_dma_resume(tdc);
}

static void tdc_start_head_req(struct tegra_dma_channel *tdc)
{
	struct tegra_dma_sg_req *sg_req;

	if (list_empty(&tdc->pending_sg_req))
		return;

	sg_req = list_first_entry(&tdc->pending_sg_req,
					typeof(*sg_req), node);
	tegra_dma_start(tdc, sg_req);
	sg_req->configured = true;
	sg_req->words_xferred = 0;
	tdc->busy = true;
}

static void tdc_configure_next_head_desc(struct tegra_dma_channel *tdc)
{
	struct tegra_dma_sg_req *hsgreq;
	struct tegra_dma_sg_req *hnsgreq;

	if (list_empty(&tdc->pending_sg_req))
		return;

	hsgreq = list_first_entry(&tdc->pending_sg_req, typeof(*hsgreq), node);
	if (!list_is_last(&hsgreq->node, &tdc->pending_sg_req)) {
		hnsgreq = list_first_entry(&hsgreq->node,
					typeof(*hnsgreq), node);
		tegra_dma_configure_for_next(tdc, hnsgreq);
	}
}

static inline int get_current_xferred_count(struct tegra_dma_channel *tdc,
	struct tegra_dma_sg_req *sg_req, unsigned long status)
{
	return sg_req->req_len - (status & TEGRA_APBDMA_STATUS_COUNT_MASK) - 4;
}

static void tegra_dma_abort_all(struct tegra_dma_channel *tdc)
{
	struct tegra_dma_sg_req *sgreq;
	struct tegra_dma_desc *dma_desc;

	while (!list_empty(&tdc->pending_sg_req)) {
		sgreq = list_first_entry(&tdc->pending_sg_req,
						typeof(*sgreq), node);
		list_move_tail(&sgreq->node, &tdc->free_sg_req);
		if (sgreq->last_sg) {
			dma_desc = sgreq->dma_desc;
			dma_desc->dma_status = DMA_ERROR;
			list_add_tail(&dma_desc->node, &tdc->free_dma_desc);

			/* Add in cb list if it is not there. */
			if (!dma_desc->cb_count)
				list_add_tail(&dma_desc->cb_node,
							&tdc->cb_desc);
			dma_desc->cb_count++;
		}
	}
	tdc->isr_handler = NULL;
}

static bool handle_continuous_head_request(struct tegra_dma_channel *tdc,
		struct tegra_dma_sg_req *last_sg_req, bool to_terminate)
{
	struct tegra_dma_sg_req *hsgreq = NULL;

	if (list_empty(&tdc->pending_sg_req)) {
		dev_err(tdc2dev(tdc), "DMA is running without req\n");
		tegra_dma_stop(tdc);
		return false;
	}

	/*
	 * Check that head req on list should be in flight.
	 * If it is not in flight then abort transfer as
	 * looping of transfer can not continue.
	 */
	hsgreq = list_first_entry(&tdc->pending_sg_req, typeof(*hsgreq), node);
	if (!hsgreq->configured) {
		tegra_dma_stop(tdc);
		dev_err(tdc2dev(tdc), "Error in DMA transfer, aborting DMA\n");
		tegra_dma_abort_all(tdc);
		return false;
	}

	/* Configure next request */
	if (!to_terminate)
		tdc_configure_next_head_desc(tdc);
	return true;
}

static void handle_once_dma_done(struct tegra_dma_channel *tdc,
	bool to_terminate)
{
	struct tegra_dma_sg_req *sgreq;
	struct tegra_dma_desc *dma_desc;

	tdc->busy = false;
	sgreq = list_first_entry(&tdc->pending_sg_req, typeof(*sgreq), node);
	dma_desc = sgreq->dma_desc;
	dma_desc->bytes_transferred += sgreq->req_len;

	list_del(&sgreq->node);
	if (sgreq->last_sg) {
		dma_desc->dma_status = DMA_COMPLETE;
		dma_cookie_complete(&dma_desc->txd);
		if (!dma_desc->cb_count)
			list_add_tail(&dma_desc->cb_node, &tdc->cb_desc);
		dma_desc->cb_count++;
		list_add_tail(&dma_desc->node, &tdc->free_dma_desc);
	}
	list_add_tail(&sgreq->node, &tdc->free_sg_req);

	/* Do not start DMA if it is going to be terminate */
	if (to_terminate || list_empty(&tdc->pending_sg_req))
		return;

	tdc_start_head_req(tdc);
}

static void handle_cont_sngl_cycle_dma_done(struct tegra_dma_channel *tdc,
		bool to_terminate)
{
	struct tegra_dma_sg_req *sgreq;
	struct tegra_dma_desc *dma_desc;
	bool st;

	sgreq = list_first_entry(&tdc->pending_sg_req, typeof(*sgreq), node);
	dma_desc = sgreq->dma_desc;
	/* if we dma for long enough the transfer count will wrap */
	dma_desc->bytes_transferred =
		(dma_desc->bytes_transferred + sgreq->req_len) %
		dma_desc->bytes_requested;

	/* Callback need to be call */
	if (!dma_desc->cb_count)
		list_add_tail(&dma_desc->cb_node, &tdc->cb_desc);
	dma_desc->cb_count++;

	sgreq->words_xferred = 0;

	/* If not last req then put at end of pending list */
	if (!list_is_last(&sgreq->node, &tdc->pending_sg_req)) {
		list_move_tail(&sgreq->node, &tdc->pending_sg_req);
		sgreq->configured = false;
		st = handle_continuous_head_request(tdc, sgreq, to_terminate);
		if (!st)
			dma_desc->dma_status = DMA_ERROR;
	}
}

static void tegra_dma_tasklet(unsigned long data)
{
	struct tegra_dma_channel *tdc = (struct tegra_dma_channel *)data;
	struct dmaengine_desc_callback cb;
	struct tegra_dma_desc *dma_desc;
	unsigned long flags;
	int cb_count;

	spin_lock_irqsave(&tdc->lock, flags);
	while (!list_empty(&tdc->cb_desc)) {
		dma_desc  = list_first_entry(&tdc->cb_desc,
					typeof(*dma_desc), cb_node);
		list_del(&dma_desc->cb_node);
		dmaengine_desc_get_callback(&dma_desc->txd, &cb);
		cb_count = dma_desc->cb_count;
		dma_desc->cb_count = 0;
		trace_tegra_dma_complete_cb(&tdc->dma_chan, cb_count,
					    cb.callback);
		spin_unlock_irqrestore(&tdc->lock, flags);
		while (cb_count--)
			dmaengine_desc_callback_invoke(&cb, NULL);
		spin_lock_irqsave(&tdc->lock, flags);
	}
	spin_unlock_irqrestore(&tdc->lock, flags);
}

static irqreturn_t tegra_dma_isr(int irq, void *dev_id)
{
	struct tegra_dma_channel *tdc = dev_id;
	unsigned long status;
	unsigned long flags;

	spin_lock_irqsave(&tdc->lock, flags);

	trace_tegra_dma_isr(&tdc->dma_chan, irq);
	status = tdc_read(tdc, TEGRA_APBDMA_CHAN_STATUS);
	if (status & TEGRA_APBDMA_STATUS_ISE_EOC) {
		tdc_write(tdc, TEGRA_APBDMA_CHAN_STATUS, status);
		tdc->isr_handler(tdc, false);
		tasklet_schedule(&tdc->tasklet);
		spin_unlock_irqrestore(&tdc->lock, flags);
		return IRQ_HANDLED;
	}

	spin_unlock_irqrestore(&tdc->lock, flags);
	dev_info(tdc2dev(tdc),
		"Interrupt already served status 0x%08lx\n", status);
	return IRQ_NONE;
}

static dma_cookie_t tegra_dma_tx_submit(struct dma_async_tx_descriptor *txd)
{
	struct tegra_dma_desc *dma_desc = txd_to_tegra_dma_desc(txd);
	struct tegra_dma_channel *tdc = to_tegra_dma_chan(txd->chan);
	unsigned long flags;
	dma_cookie_t cookie;

	spin_lock_irqsave(&tdc->lock, flags);
	dma_desc->dma_status = DMA_IN_PROGRESS;
	cookie = dma_cookie_assign(&dma_desc->txd);
	list_splice_tail_init(&dma_desc->tx_list, &tdc->pending_sg_req);
	spin_unlock_irqrestore(&tdc->lock, flags);
	return cookie;
}

static void tegra_dma_issue_pending(struct dma_chan *dc)
{
	struct tegra_dma_channel *tdc = to_tegra_dma_chan(dc);
	unsigned long flags;

	spin_lock_irqsave(&tdc->lock, flags);
	if (list_empty(&tdc->pending_sg_req)) {
		dev_err(tdc2dev(tdc), "No DMA request\n");
		goto end;
	}
	if (!tdc->busy) {
		tdc_start_head_req(tdc);

		/* Continuous single mode: Configure next req */
		if (tdc->cyclic) {
			/*
			 * Wait for 1 burst time for configure DMA for
			 * next transfer.
			 */
			udelay(TEGRA_APBDMA_BURST_COMPLETE_TIME);
			tdc_configure_next_head_desc(tdc);
		}
	}
end:
	spin_unlock_irqrestore(&tdc->lock, flags);
}

static int tegra_dma_terminate_all(struct dma_chan *dc)
{
	struct tegra_dma_channel *tdc = to_tegra_dma_chan(dc);
	struct tegra_dma_sg_req *sgreq;
	struct tegra_dma_desc *dma_desc;
	unsigned long flags;
	unsigned long status;
	unsigned long wcount;
	bool was_busy;

	spin_lock_irqsave(&tdc->lock, flags);

	if (!tdc->busy)
		goto skip_dma_stop;

	/* Pause DMA before checking the queue status */
	tegra_dma_pause(tdc, true);

	status = tdc_read(tdc, TEGRA_APBDMA_CHAN_STATUS);
	if (status & TEGRA_APBDMA_STATUS_ISE_EOC) {
		dev_dbg(tdc2dev(tdc), "%s():handling isr\n", __func__);
		tdc->isr_handler(tdc, true);
		status = tdc_read(tdc, TEGRA_APBDMA_CHAN_STATUS);
	}
	if (tdc->tdma->chip_data->support_separate_wcount_reg)
		wcount = tdc_read(tdc, TEGRA_APBDMA_CHAN_WORD_TRANSFER);
	else
		wcount = status;

	was_busy = tdc->busy;
	tegra_dma_stop(tdc);

	if (!list_empty(&tdc->pending_sg_req) && was_busy) {
		sgreq = list_first_entry(&tdc->pending_sg_req,
					typeof(*sgreq), node);
		sgreq->dma_desc->bytes_transferred +=
				get_current_xferred_count(tdc, sgreq, wcount);
	}
	tegra_dma_resume(tdc);

skip_dma_stop:
	tegra_dma_abort_all(tdc);

	while (!list_empty(&tdc->cb_desc)) {
		dma_desc  = list_first_entry(&tdc->cb_desc,
					typeof(*dma_desc), cb_node);
		list_del(&dma_desc->cb_node);
		dma_desc->cb_count = 0;
	}
	spin_unlock_irqrestore(&tdc->lock, flags);
	return 0;
}

static unsigned int tegra_dma_sg_bytes_xferred(struct tegra_dma_channel *tdc,
					       struct tegra_dma_sg_req *sg_req)
{
	unsigned long status, wcount = 0;

	if (!list_is_first(&sg_req->node, &tdc->pending_sg_req))
		return 0;

	if (tdc->tdma->chip_data->support_separate_wcount_reg)
		wcount = tdc_read(tdc, TEGRA_APBDMA_CHAN_WORD_TRANSFER);

	status = tdc_read(tdc, TEGRA_APBDMA_CHAN_STATUS);

	if (!tdc->tdma->chip_data->support_separate_wcount_reg)
		wcount = status;

	if (status & TEGRA_APBDMA_STATUS_ISE_EOC)
		return sg_req->req_len;

	wcount = get_current_xferred_count(tdc, sg_req, wcount);

	if (!wcount) {
		/*
		 * If wcount wasn't ever polled for this SG before, then
		 * simply assume that transfer hasn't started yet.
		 *
		 * Otherwise it's the end of the transfer.
		 *
		 * The alternative would be to poll the status register
		 * until EOC bit is set or wcount goes UP. That's so
		 * because EOC bit is getting set only after the last
		 * burst's completion and counter is less than the actual
		 * transfer size by 4 bytes. The counter value wraps around
		 * in a cyclic mode before EOC is set(!), so we can't easily
		 * distinguish start of transfer from its end.
		 */
		if (sg_req->words_xferred)
			wcount = sg_req->req_len - 4;

	} else if (wcount < sg_req->words_xferred) {
		/*
		 * This case will never happen for a non-cyclic transfer.
		 *
		 * For a cyclic transfer, although it is possible for the
		 * next transfer to have already started (resetting the word
		 * count), this case should still not happen because we should
		 * have detected that the EOC bit is set and hence the transfer
		 * was completed.
		 */
		WARN_ON_ONCE(1);

		wcount = sg_req->req_len - 4;
	} else {
		sg_req->words_xferred = wcount;
	}

	return wcount;
}

static enum dma_status tegra_dma_tx_status(struct dma_chan *dc,
	dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct tegra_dma_channel *tdc = to_tegra_dma_chan(dc);
	struct tegra_dma_desc *dma_desc;
	struct tegra_dma_sg_req *sg_req;
	enum dma_status ret;
	unsigned long flags;
	unsigned int residual;
	unsigned int bytes = 0;

	ret = dma_cookie_status(dc, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	spin_lock_irqsave(&tdc->lock, flags);

	/* Check on wait_ack desc status */
	list_for_each_entry(dma_desc, &tdc->free_dma_desc, node) {
		if (dma_desc->txd.cookie == cookie) {
			ret = dma_desc->dma_status;
			goto found;
		}
	}

	/* Check in pending list */
	list_for_each_entry(sg_req, &tdc->pending_sg_req, node) {
		dma_desc = sg_req->dma_desc;
		if (dma_desc->txd.cookie == cookie) {
			bytes = tegra_dma_sg_bytes_xferred(tdc, sg_req);
			ret = dma_desc->dma_status;
			goto found;
		}
	}

	dev_dbg(tdc2dev(tdc), "cookie %d not found\n", cookie);
	dma_desc = NULL;

found:
	if (dma_desc && txstate) {
		residual = dma_desc->bytes_requested -
			   ((dma_desc->bytes_transferred + bytes) %
			    dma_desc->bytes_requested);
		dma_set_residue(txstate, residual);
	}

	trace_tegra_dma_tx_status(&tdc->dma_chan, cookie, txstate);
	spin_unlock_irqrestore(&tdc->lock, flags);
	return ret;
}

static inline int get_bus_width(struct tegra_dma_channel *tdc,
		enum dma_slave_buswidth slave_bw)
{
	switch (slave_bw) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		return TEGRA_APBDMA_APBSEQ_BUS_WIDTH_8;
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		return TEGRA_APBDMA_APBSEQ_BUS_WIDTH_16;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		return TEGRA_APBDMA_APBSEQ_BUS_WIDTH_32;
	case DMA_SLAVE_BUSWIDTH_8_BYTES:
		return TEGRA_APBDMA_APBSEQ_BUS_WIDTH_64;
	default:
		dev_warn(tdc2dev(tdc),
			"slave bw is not supported, using 32bits\n");
		return TEGRA_APBDMA_APBSEQ_BUS_WIDTH_32;
	}
}

static inline int get_burst_size(struct tegra_dma_channel *tdc,
	u32 burst_size, enum dma_slave_buswidth slave_bw, int len)
{
	int burst_byte;
	int burst_ahb_width;

	/*
	 * burst_size from client is in terms of the bus_width.
	 * convert them into AHB memory width which is 4 byte.
	 */
	burst_byte = burst_size * slave_bw;
	burst_ahb_width = burst_byte / 4;

	/* If burst size is 0 then calculate the burst size based on length */
	if (!burst_ahb_width) {
		if (len & 0xF)
			return TEGRA_APBDMA_AHBSEQ_BURST_1;
		else if ((len >> 4) & 0x1)
			return TEGRA_APBDMA_AHBSEQ_BURST_4;
		else
			return TEGRA_APBDMA_AHBSEQ_BURST_8;
	}
	if (burst_ahb_width < 4)
		return TEGRA_APBDMA_AHBSEQ_BURST_1;
	else if (burst_ahb_width < 8)
		return TEGRA_APBDMA_AHBSEQ_BURST_4;
	else
		return TEGRA_APBDMA_AHBSEQ_BURST_8;
}

static int get_transfer_param(struct tegra_dma_channel *tdc,
	enum dma_transfer_direction direction, unsigned long *apb_addr,
	unsigned long *apb_seq,	unsigned long *csr, unsigned int *burst_size,
	enum dma_slave_buswidth *slave_bw)
{
	switch (direction) {
	case DMA_MEM_TO_DEV:
		*apb_addr = tdc->dma_sconfig.dst_addr;
		*apb_seq = get_bus_width(tdc, tdc->dma_sconfig.dst_addr_width);
		*burst_size = tdc->dma_sconfig.dst_maxburst;
		*slave_bw = tdc->dma_sconfig.dst_addr_width;
		*csr = TEGRA_APBDMA_CSR_DIR;
		return 0;

	case DMA_DEV_TO_MEM:
		*apb_addr = tdc->dma_sconfig.src_addr;
		*apb_seq = get_bus_width(tdc, tdc->dma_sconfig.src_addr_width);
		*burst_size = tdc->dma_sconfig.src_maxburst;
		*slave_bw = tdc->dma_sconfig.src_addr_width;
		*csr = 0;
		return 0;

	default:
		dev_err(tdc2dev(tdc), "DMA direction is not supported\n");
		return -EINVAL;
	}
	return -EINVAL;
}

static void tegra_dma_prep_wcount(struct tegra_dma_channel *tdc,
	struct tegra_dma_channel_regs *ch_regs, u32 len)
{
	u32 len_field = (len - 4) & 0xFFFC;

	if (tdc->tdma->chip_data->support_separate_wcount_reg)
		ch_regs->wcount = len_field;
	else
		ch_regs->csr |= len_field;
}

static struct dma_async_tx_descriptor *tegra_dma_prep_slave_sg(
	struct dma_chan *dc, struct scatterlist *sgl, unsigned int sg_len,
	enum dma_transfer_direction direction, unsigned long flags,
	void *context)
{
	struct tegra_dma_channel *tdc = to_tegra_dma_chan(dc);
	struct tegra_dma_desc *dma_desc;
	unsigned int i;
	struct scatterlist *sg;
	unsigned long csr, ahb_seq, apb_ptr, apb_seq;
	struct list_head req_list;
	struct tegra_dma_sg_req  *sg_req = NULL;
	u32 burst_size;
	enum dma_slave_buswidth slave_bw;

	if (!tdc->config_init) {
		dev_err(tdc2dev(tdc), "DMA channel is not configured\n");
		return NULL;
	}
	if (sg_len < 1) {
		dev_err(tdc2dev(tdc), "Invalid segment length %d\n", sg_len);
		return NULL;
	}

	if (get_transfer_param(tdc, direction, &apb_ptr, &apb_seq, &csr,
				&burst_size, &slave_bw) < 0)
		return NULL;

	INIT_LIST_HEAD(&req_list);

	ahb_seq = TEGRA_APBDMA_AHBSEQ_INTR_ENB;
	ahb_seq |= TEGRA_APBDMA_AHBSEQ_WRAP_NONE <<
					TEGRA_APBDMA_AHBSEQ_WRAP_SHIFT;
	ahb_seq |= TEGRA_APBDMA_AHBSEQ_BUS_WIDTH_32;

	csr |= TEGRA_APBDMA_CSR_ONCE;

	if (tdc->slave_id != TEGRA_APBDMA_SLAVE_ID_INVALID) {
		csr |= TEGRA_APBDMA_CSR_FLOW;
		csr |= tdc->slave_id << TEGRA_APBDMA_CSR_REQ_SEL_SHIFT;
	}

	if (flags & DMA_PREP_INTERRUPT) {
		csr |= TEGRA_APBDMA_CSR_IE_EOC;
	} else {
		WARN_ON_ONCE(1);
		return NULL;
	}

	apb_seq |= TEGRA_APBDMA_APBSEQ_WRAP_WORD_1;

	dma_desc = tegra_dma_desc_get(tdc);
	if (!dma_desc) {
		dev_err(tdc2dev(tdc), "DMA descriptors not available\n");
		return NULL;
	}
	INIT_LIST_HEAD(&dma_desc->tx_list);
	INIT_LIST_HEAD(&dma_desc->cb_node);
	dma_desc->cb_count = 0;
	dma_desc->bytes_requested = 0;
	dma_desc->bytes_transferred = 0;
	dma_desc->dma_status = DMA_IN_PROGRESS;

	/* Make transfer requests */
	for_each_sg(sgl, sg, sg_len, i) {
		u32 len, mem;

		mem = sg_dma_address(sg);
		len = sg_dma_len(sg);

		if ((len & 3) || (mem & 3) ||
				(len > tdc->tdma->chip_data->max_dma_count)) {
			dev_err(tdc2dev(tdc),
				"DMA length/memory address is not supported\n");
			tegra_dma_desc_put(tdc, dma_desc);
			return NULL;
		}

		sg_req = tegra_dma_sg_req_get(tdc);
		if (!sg_req) {
			dev_err(tdc2dev(tdc), "DMA sg-req not available\n");
			tegra_dma_desc_put(tdc, dma_desc);
			return NULL;
		}

		ahb_seq |= get_burst_size(tdc, burst_size, slave_bw, len);
		dma_desc->bytes_requested += len;

		sg_req->ch_regs.apb_ptr = apb_ptr;
		sg_req->ch_regs.ahb_ptr = mem;
		sg_req->ch_regs.csr = csr;
		tegra_dma_prep_wcount(tdc, &sg_req->ch_regs, len);
		sg_req->ch_regs.apb_seq = apb_seq;
		sg_req->ch_regs.ahb_seq = ahb_seq;
		sg_req->configured = false;
		sg_req->last_sg = false;
		sg_req->dma_desc = dma_desc;
		sg_req->req_len = len;

		list_add_tail(&sg_req->node, &dma_desc->tx_list);
	}
	sg_req->last_sg = true;
	if (flags & DMA_CTRL_ACK)
		dma_desc->txd.flags = DMA_CTRL_ACK;

	/*
	 * Make sure that mode should not be conflicting with currently
	 * configured mode.
	 */
	if (!tdc->isr_handler) {
		tdc->isr_handler = handle_once_dma_done;
		tdc->cyclic = false;
	} else {
		if (tdc->cyclic) {
			dev_err(tdc2dev(tdc), "DMA configured in cyclic mode\n");
			tegra_dma_desc_put(tdc, dma_desc);
			return NULL;
		}
	}

	return &dma_desc->txd;
}

static struct dma_async_tx_descriptor *tegra_dma_prep_dma_cyclic(
	struct dma_chan *dc, dma_addr_t buf_addr, size_t buf_len,
	size_t period_len, enum dma_transfer_direction direction,
	unsigned long flags)
{
	struct tegra_dma_channel *tdc = to_tegra_dma_chan(dc);
	struct tegra_dma_desc *dma_desc = NULL;
	struct tegra_dma_sg_req *sg_req = NULL;
	unsigned long csr, ahb_seq, apb_ptr, apb_seq;
	int len;
	size_t remain_len;
	dma_addr_t mem = buf_addr;
	u32 burst_size;
	enum dma_slave_buswidth slave_bw;

	if (!buf_len || !period_len) {
		dev_err(tdc2dev(tdc), "Invalid buffer/period len\n");
		return NULL;
	}

	if (!tdc->config_init) {
		dev_err(tdc2dev(tdc), "DMA slave is not configured\n");
		return NULL;
	}

	/*
	 * We allow to take more number of requests till DMA is
	 * not started. The driver will loop over all requests.
	 * Once DMA is started then new requests can be queued only after
	 * terminating the DMA.
	 */
	if (tdc->busy) {
		dev_err(tdc2dev(tdc), "Request not allowed when DMA running\n");
		return NULL;
	}

	/*
	 * We only support cycle transfer when buf_len is multiple of
	 * period_len.
	 */
	if (buf_len % period_len) {
		dev_err(tdc2dev(tdc), "buf_len is not multiple of period_len\n");
		return NULL;
	}

	len = period_len;
	if ((len & 3) || (buf_addr & 3) ||
			(len > tdc->tdma->chip_data->max_dma_count)) {
		dev_err(tdc2dev(tdc), "Req len/mem address is not correct\n");
		return NULL;
	}

	if (get_transfer_param(tdc, direction, &apb_ptr, &apb_seq, &csr,
				&burst_size, &slave_bw) < 0)
		return NULL;

	ahb_seq = TEGRA_APBDMA_AHBSEQ_INTR_ENB;
	ahb_seq |= TEGRA_APBDMA_AHBSEQ_WRAP_NONE <<
					TEGRA_APBDMA_AHBSEQ_WRAP_SHIFT;
	ahb_seq |= TEGRA_APBDMA_AHBSEQ_BUS_WIDTH_32;

	if (tdc->slave_id != TEGRA_APBDMA_SLAVE_ID_INVALID) {
		csr |= TEGRA_APBDMA_CSR_FLOW;
		csr |= tdc->slave_id << TEGRA_APBDMA_CSR_REQ_SEL_SHIFT;
	}

	if (flags & DMA_PREP_INTERRUPT) {
		csr |= TEGRA_APBDMA_CSR_IE_EOC;
	} else {
		WARN_ON_ONCE(1);
		return NULL;
	}

	apb_seq |= TEGRA_APBDMA_APBSEQ_WRAP_WORD_1;

	dma_desc = tegra_dma_desc_get(tdc);
	if (!dma_desc) {
		dev_err(tdc2dev(tdc), "not enough descriptors available\n");
		return NULL;
	}

	INIT_LIST_HEAD(&dma_desc->tx_list);
	INIT_LIST_HEAD(&dma_desc->cb_node);
	dma_desc->cb_count = 0;

	dma_desc->bytes_transferred = 0;
	dma_desc->bytes_requested = buf_len;
	remain_len = buf_len;

	/* Split transfer equal to period size */
	while (remain_len) {
		sg_req = tegra_dma_sg_req_get(tdc);
		if (!sg_req) {
			dev_err(tdc2dev(tdc), "DMA sg-req not available\n");
			tegra_dma_desc_put(tdc, dma_desc);
			return NULL;
		}

		ahb_seq |= get_burst_size(tdc, burst_size, slave_bw, len);
		sg_req->ch_regs.apb_ptr = apb_ptr;
		sg_req->ch_regs.ahb_ptr = mem;
		sg_req->ch_regs.csr = csr;
		tegra_dma_prep_wcount(tdc, &sg_req->ch_regs, len);
		sg_req->ch_regs.apb_seq = apb_seq;
		sg_req->ch_regs.ahb_seq = ahb_seq;
		sg_req->configured = false;
		sg_req->last_sg = false;
		sg_req->dma_desc = dma_desc;
		sg_req->req_len = len;

		list_add_tail(&sg_req->node, &dma_desc->tx_list);
		remain_len -= len;
		mem += len;
	}
	sg_req->last_sg = true;
	if (flags & DMA_CTRL_ACK)
		dma_desc->txd.flags = DMA_CTRL_ACK;

	/*
	 * Make sure that mode should not be conflicting with currently
	 * configured mode.
	 */
	if (!tdc->isr_handler) {
		tdc->isr_handler = handle_cont_sngl_cycle_dma_done;
		tdc->cyclic = true;
	} else {
		if (!tdc->cyclic) {
			dev_err(tdc2dev(tdc), "DMA configuration conflict\n");
			tegra_dma_desc_put(tdc, dma_desc);
			return NULL;
		}
	}

	return &dma_desc->txd;
}

static int tegra_dma_alloc_chan_resources(struct dma_chan *dc)
{
	struct tegra_dma_channel *tdc = to_tegra_dma_chan(dc);
	struct tegra_dma *tdma = tdc->tdma;
	int ret;

	dma_cookie_init(&tdc->dma_chan);
	tdc->config_init = false;

	ret = pm_runtime_get_sync(tdma->dev);
	if (ret < 0)
		return ret;

	return 0;
}

static void tegra_dma_free_chan_resources(struct dma_chan *dc)
{
	struct tegra_dma_channel *tdc = to_tegra_dma_chan(dc);
	struct tegra_dma *tdma = tdc->tdma;
	struct tegra_dma_desc *dma_desc;
	struct tegra_dma_sg_req *sg_req;
	struct list_head dma_desc_list;
	struct list_head sg_req_list;
	unsigned long flags;

	INIT_LIST_HEAD(&dma_desc_list);
	INIT_LIST_HEAD(&sg_req_list);

	dev_dbg(tdc2dev(tdc), "Freeing channel %d\n", tdc->id);

	if (tdc->busy)
		tegra_dma_terminate_all(dc);

	spin_lock_irqsave(&tdc->lock, flags);
	list_splice_init(&tdc->pending_sg_req, &sg_req_list);
	list_splice_init(&tdc->free_sg_req, &sg_req_list);
	list_splice_init(&tdc->free_dma_desc, &dma_desc_list);
	INIT_LIST_HEAD(&tdc->cb_desc);
	tdc->config_init = false;
	tdc->isr_handler = NULL;
	spin_unlock_irqrestore(&tdc->lock, flags);

	while (!list_empty(&dma_desc_list)) {
		dma_desc = list_first_entry(&dma_desc_list,
					typeof(*dma_desc), node);
		list_del(&dma_desc->node);
		kfree(dma_desc);
	}

	while (!list_empty(&sg_req_list)) {
		sg_req = list_first_entry(&sg_req_list, typeof(*sg_req), node);
		list_del(&sg_req->node);
		kfree(sg_req);
	}
	pm_runtime_put(tdma->dev);

	tdc->slave_id = TEGRA_APBDMA_SLAVE_ID_INVALID;
}

static struct dma_chan *tegra_dma_of_xlate(struct of_phandle_args *dma_spec,
					   struct of_dma *ofdma)
{
	struct tegra_dma *tdma = ofdma->of_dma_data;
	struct dma_chan *chan;
	struct tegra_dma_channel *tdc;

	if (dma_spec->args[0] > TEGRA_APBDMA_CSR_REQ_SEL_MASK) {
		dev_err(tdma->dev, "Invalid slave id: %d\n", dma_spec->args[0]);
		return NULL;
	}

	chan = dma_get_any_slave_channel(&tdma->dma_dev);
	if (!chan)
		return NULL;

	tdc = to_tegra_dma_chan(chan);
	tdc->slave_id = dma_spec->args[0];

	return chan;
}

/* Tegra20 specific DMA controller information */
static const struct tegra_dma_chip_data tegra20_dma_chip_data = {
	.nr_channels		= 16,
	.channel_reg_size	= 0x20,
	.max_dma_count		= 1024UL * 64,
	.support_channel_pause	= false,
	.support_separate_wcount_reg = false,
};

/* Tegra30 specific DMA controller information */
static const struct tegra_dma_chip_data tegra30_dma_chip_data = {
	.nr_channels		= 32,
	.channel_reg_size	= 0x20,
	.max_dma_count		= 1024UL * 64,
	.support_channel_pause	= false,
	.support_separate_wcount_reg = false,
};

/* Tegra114 specific DMA controller information */
static const struct tegra_dma_chip_data tegra114_dma_chip_data = {
	.nr_channels		= 32,
	.channel_reg_size	= 0x20,
	.max_dma_count		= 1024UL * 64,
	.support_channel_pause	= true,
	.support_separate_wcount_reg = false,
};

/* Tegra148 specific DMA controller information */
static const struct tegra_dma_chip_data tegra148_dma_chip_data = {
	.nr_channels		= 32,
	.channel_reg_size	= 0x40,
	.max_dma_count		= 1024UL * 64,
	.support_channel_pause	= true,
	.support_separate_wcount_reg = true,
};

static int tegra_dma_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct tegra_dma *tdma;
	int ret;
	int i;
	const struct tegra_dma_chip_data *cdata;

	cdata = of_device_get_match_data(&pdev->dev);
	if (!cdata) {
		dev_err(&pdev->dev, "Error: No device match data found\n");
		return -ENODEV;
	}

	tdma = devm_kzalloc(&pdev->dev,
			    struct_size(tdma, channels, cdata->nr_channels),
			    GFP_KERNEL);
	if (!tdma)
		return -ENOMEM;

	tdma->dev = &pdev->dev;
	tdma->chip_data = cdata;
	platform_set_drvdata(pdev, tdma);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tdma->base_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tdma->base_addr))
		return PTR_ERR(tdma->base_addr);

	tdma->dma_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(tdma->dma_clk)) {
		dev_err(&pdev->dev, "Error: Missing controller clock\n");
		return PTR_ERR(tdma->dma_clk);
	}

	tdma->rst = devm_reset_control_get(&pdev->dev, "dma");
	if (IS_ERR(tdma->rst)) {
		dev_err(&pdev->dev, "Error: Missing reset\n");
		return PTR_ERR(tdma->rst);
	}

	spin_lock_init(&tdma->global_lock);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev))
		ret = tegra_dma_runtime_resume(&pdev->dev);
	else
		ret = pm_runtime_get_sync(&pdev->dev);

	if (ret < 0) {
		pm_runtime_disable(&pdev->dev);
		return ret;
	}

	/* Reset DMA controller */
	reset_control_assert(tdma->rst);
	udelay(2);
	reset_control_deassert(tdma->rst);

	/* Enable global DMA registers */
	tdma_write(tdma, TEGRA_APBDMA_GENERAL, TEGRA_APBDMA_GENERAL_ENABLE);
	tdma_write(tdma, TEGRA_APBDMA_CONTROL, 0);
	tdma_write(tdma, TEGRA_APBDMA_IRQ_MASK_SET, 0xFFFFFFFFul);

	pm_runtime_put(&pdev->dev);

	INIT_LIST_HEAD(&tdma->dma_dev.channels);
	for (i = 0; i < cdata->nr_channels; i++) {
		struct tegra_dma_channel *tdc = &tdma->channels[i];

		tdc->chan_addr = tdma->base_addr +
				 TEGRA_APBDMA_CHANNEL_BASE_ADD_OFFSET +
				 (i * cdata->channel_reg_size);

		res = platform_get_resource(pdev, IORESOURCE_IRQ, i);
		if (!res) {
			ret = -EINVAL;
			dev_err(&pdev->dev, "No irq resource for chan %d\n", i);
			goto err_irq;
		}
		tdc->irq = res->start;
		snprintf(tdc->name, sizeof(tdc->name), "apbdma.%d", i);
		ret = request_irq(tdc->irq, tegra_dma_isr, 0, tdc->name, tdc);
		if (ret) {
			dev_err(&pdev->dev,
				"request_irq failed with err %d channel %d\n",
				ret, i);
			goto err_irq;
		}

		tdc->dma_chan.device = &tdma->dma_dev;
		dma_cookie_init(&tdc->dma_chan);
		list_add_tail(&tdc->dma_chan.device_node,
				&tdma->dma_dev.channels);
		tdc->tdma = tdma;
		tdc->id = i;
		tdc->slave_id = TEGRA_APBDMA_SLAVE_ID_INVALID;

		tasklet_init(&tdc->tasklet, tegra_dma_tasklet,
				(unsigned long)tdc);
		spin_lock_init(&tdc->lock);

		INIT_LIST_HEAD(&tdc->pending_sg_req);
		INIT_LIST_HEAD(&tdc->free_sg_req);
		INIT_LIST_HEAD(&tdc->free_dma_desc);
		INIT_LIST_HEAD(&tdc->cb_desc);
	}

	dma_cap_set(DMA_SLAVE, tdma->dma_dev.cap_mask);
	dma_cap_set(DMA_PRIVATE, tdma->dma_dev.cap_mask);
	dma_cap_set(DMA_CYCLIC, tdma->dma_dev.cap_mask);

	tdma->global_pause_count = 0;
	tdma->dma_dev.dev = &pdev->dev;
	tdma->dma_dev.device_alloc_chan_resources =
					tegra_dma_alloc_chan_resources;
	tdma->dma_dev.device_free_chan_resources =
					tegra_dma_free_chan_resources;
	tdma->dma_dev.device_prep_slave_sg = tegra_dma_prep_slave_sg;
	tdma->dma_dev.device_prep_dma_cyclic = tegra_dma_prep_dma_cyclic;
	tdma->dma_dev.src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
		BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
		BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) |
		BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);
	tdma->dma_dev.dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
		BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
		BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) |
		BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);
	tdma->dma_dev.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	tdma->dma_dev.residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	tdma->dma_dev.device_config = tegra_dma_slave_config;
	tdma->dma_dev.device_terminate_all = tegra_dma_terminate_all;
	tdma->dma_dev.device_tx_status = tegra_dma_tx_status;
	tdma->dma_dev.device_issue_pending = tegra_dma_issue_pending;

	ret = dma_async_device_register(&tdma->dma_dev);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Tegra20 APB DMA driver registration failed %d\n", ret);
		goto err_irq;
	}

	ret = of_dma_controller_register(pdev->dev.of_node,
					 tegra_dma_of_xlate, tdma);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Tegra20 APB DMA OF registration failed %d\n", ret);
		goto err_unregister_dma_dev;
	}

	dev_info(&pdev->dev, "Tegra20 APB DMA driver register %d channels\n",
			cdata->nr_channels);
	return 0;

err_unregister_dma_dev:
	dma_async_device_unregister(&tdma->dma_dev);
err_irq:
	while (--i >= 0) {
		struct tegra_dma_channel *tdc = &tdma->channels[i];

		free_irq(tdc->irq, tdc);
		tasklet_kill(&tdc->tasklet);
	}

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra_dma_runtime_suspend(&pdev->dev);
	return ret;
}

static int tegra_dma_remove(struct platform_device *pdev)
{
	struct tegra_dma *tdma = platform_get_drvdata(pdev);
	int i;
	struct tegra_dma_channel *tdc;

	dma_async_device_unregister(&tdma->dma_dev);

	for (i = 0; i < tdma->chip_data->nr_channels; ++i) {
		tdc = &tdma->channels[i];
		free_irq(tdc->irq, tdc);
		tasklet_kill(&tdc->tasklet);
	}

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra_dma_runtime_suspend(&pdev->dev);

	return 0;
}

static int tegra_dma_runtime_suspend(struct device *dev)
{
	struct tegra_dma *tdma = dev_get_drvdata(dev);
	int i;

	tdma->reg_gen = tdma_read(tdma, TEGRA_APBDMA_GENERAL);
	for (i = 0; i < tdma->chip_data->nr_channels; i++) {
		struct tegra_dma_channel *tdc = &tdma->channels[i];
		struct tegra_dma_channel_regs *ch_reg = &tdc->channel_reg;

		/* Only save the state of DMA channels that are in use */
		if (!tdc->config_init)
			continue;

		ch_reg->csr = tdc_read(tdc, TEGRA_APBDMA_CHAN_CSR);
		ch_reg->ahb_ptr = tdc_read(tdc, TEGRA_APBDMA_CHAN_AHBPTR);
		ch_reg->apb_ptr = tdc_read(tdc, TEGRA_APBDMA_CHAN_APBPTR);
		ch_reg->ahb_seq = tdc_read(tdc, TEGRA_APBDMA_CHAN_AHBSEQ);
		ch_reg->apb_seq = tdc_read(tdc, TEGRA_APBDMA_CHAN_APBSEQ);
		if (tdma->chip_data->support_separate_wcount_reg)
			ch_reg->wcount = tdc_read(tdc,
						  TEGRA_APBDMA_CHAN_WCOUNT);
	}

	clk_disable_unprepare(tdma->dma_clk);

	return 0;
}

static int tegra_dma_runtime_resume(struct device *dev)
{
	struct tegra_dma *tdma = dev_get_drvdata(dev);
	int i, ret;

	ret = clk_prepare_enable(tdma->dma_clk);
	if (ret < 0) {
		dev_err(dev, "clk_enable failed: %d\n", ret);
		return ret;
	}

	tdma_write(tdma, TEGRA_APBDMA_GENERAL, tdma->reg_gen);
	tdma_write(tdma, TEGRA_APBDMA_CONTROL, 0);
	tdma_write(tdma, TEGRA_APBDMA_IRQ_MASK_SET, 0xFFFFFFFFul);

	for (i = 0; i < tdma->chip_data->nr_channels; i++) {
		struct tegra_dma_channel *tdc = &tdma->channels[i];
		struct tegra_dma_channel_regs *ch_reg = &tdc->channel_reg;

		/* Only restore the state of DMA channels that are in use */
		if (!tdc->config_init)
			continue;

		if (tdma->chip_data->support_separate_wcount_reg)
			tdc_write(tdc, TEGRA_APBDMA_CHAN_WCOUNT,
				  ch_reg->wcount);
		tdc_write(tdc, TEGRA_APBDMA_CHAN_APBSEQ, ch_reg->apb_seq);
		tdc_write(tdc, TEGRA_APBDMA_CHAN_APBPTR, ch_reg->apb_ptr);
		tdc_write(tdc, TEGRA_APBDMA_CHAN_AHBSEQ, ch_reg->ahb_seq);
		tdc_write(tdc, TEGRA_APBDMA_CHAN_AHBPTR, ch_reg->ahb_ptr);
		tdc_write(tdc, TEGRA_APBDMA_CHAN_CSR,
			(ch_reg->csr & ~TEGRA_APBDMA_CSR_ENB));
	}

	return 0;
}

static const struct dev_pm_ops tegra_dma_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_dma_runtime_suspend, tegra_dma_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static const struct of_device_id tegra_dma_of_match[] = {
	{
		.compatible = "nvidia,tegra148-apbdma",
		.data = &tegra148_dma_chip_data,
	}, {
		.compatible = "nvidia,tegra114-apbdma",
		.data = &tegra114_dma_chip_data,
	}, {
		.compatible = "nvidia,tegra30-apbdma",
		.data = &tegra30_dma_chip_data,
	}, {
		.compatible = "nvidia,tegra20-apbdma",
		.data = &tegra20_dma_chip_data,
	}, {
	},
};
MODULE_DEVICE_TABLE(of, tegra_dma_of_match);

static struct platform_driver tegra_dmac_driver = {
	.driver = {
		.name	= "tegra-apbdma",
		.pm	= &tegra_dma_dev_pm_ops,
		.of_match_table = tegra_dma_of_match,
	},
	.probe		= tegra_dma_probe,
	.remove		= tegra_dma_remove,
};

module_platform_driver(tegra_dmac_driver);

MODULE_ALIAS("platform:tegra20-apbdma");
MODULE_DESCRIPTION("NVIDIA Tegra APB DMA Controller driver");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_LICENSE("GPL v2");
