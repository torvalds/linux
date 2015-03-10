/*
 * Core driver for the Synopsys DesignWare DMA Controller
 *
 * Copyright (C) 2007-2008 Atmel Corporation
 * Copyright (C) 2010-2011 ST Microelectronics
 * Copyright (C) 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#include "../dmaengine.h"
#include "internal.h"

/*
 * This supports the Synopsys "DesignWare AHB Central DMA Controller",
 * (DW_ahb_dmac) which is used with various AMBA 2.0 systems (not all
 * of which use ARM any more).  See the "Databook" from Synopsys for
 * information beyond what licensees probably provide.
 *
 * The driver has been tested with the Atmel AT32AP7000, which does not
 * support descriptor writeback.
 */

#define DWC_DEFAULT_CTLLO(_chan) ({				\
		struct dw_dma_chan *_dwc = to_dw_dma_chan(_chan);	\
		struct dma_slave_config	*_sconfig = &_dwc->dma_sconfig;	\
		bool _is_slave = is_slave_direction(_dwc->direction);	\
		u8 _smsize = _is_slave ? _sconfig->src_maxburst :	\
			DW_DMA_MSIZE_16;			\
		u8 _dmsize = _is_slave ? _sconfig->dst_maxburst :	\
			DW_DMA_MSIZE_16;			\
								\
		(DWC_CTLL_DST_MSIZE(_dmsize)			\
		 | DWC_CTLL_SRC_MSIZE(_smsize)			\
		 | DWC_CTLL_LLP_D_EN				\
		 | DWC_CTLL_LLP_S_EN				\
		 | DWC_CTLL_DMS(_dwc->dst_master)		\
		 | DWC_CTLL_SMS(_dwc->src_master));		\
	})

/*
 * Number of descriptors to allocate for each channel. This should be
 * made configurable somehow; preferably, the clients (at least the
 * ones using slave transfers) should be able to give us a hint.
 */
#define NR_DESCS_PER_CHANNEL	64

/* The set of bus widths supported by the DMA controller */
#define DW_DMA_BUSWIDTHS			  \
	BIT(DMA_SLAVE_BUSWIDTH_UNDEFINED)	| \
	BIT(DMA_SLAVE_BUSWIDTH_1_BYTE)		| \
	BIT(DMA_SLAVE_BUSWIDTH_2_BYTES)		| \
	BIT(DMA_SLAVE_BUSWIDTH_4_BYTES)

/*----------------------------------------------------------------------*/

static struct device *chan2dev(struct dma_chan *chan)
{
	return &chan->dev->device;
}

static struct dw_desc *dwc_first_active(struct dw_dma_chan *dwc)
{
	return to_dw_desc(dwc->active_list.next);
}

static struct dw_desc *dwc_desc_get(struct dw_dma_chan *dwc)
{
	struct dw_desc *desc, *_desc;
	struct dw_desc *ret = NULL;
	unsigned int i = 0;
	unsigned long flags;

	spin_lock_irqsave(&dwc->lock, flags);
	list_for_each_entry_safe(desc, _desc, &dwc->free_list, desc_node) {
		i++;
		if (async_tx_test_ack(&desc->txd)) {
			list_del(&desc->desc_node);
			ret = desc;
			break;
		}
		dev_dbg(chan2dev(&dwc->chan), "desc %p not ACKed\n", desc);
	}
	spin_unlock_irqrestore(&dwc->lock, flags);

	dev_vdbg(chan2dev(&dwc->chan), "scanned %u descriptors on freelist\n", i);

	return ret;
}

/*
 * Move a descriptor, including any children, to the free list.
 * `desc' must not be on any lists.
 */
static void dwc_desc_put(struct dw_dma_chan *dwc, struct dw_desc *desc)
{
	unsigned long flags;

	if (desc) {
		struct dw_desc *child;

		spin_lock_irqsave(&dwc->lock, flags);
		list_for_each_entry(child, &desc->tx_list, desc_node)
			dev_vdbg(chan2dev(&dwc->chan),
					"moving child desc %p to freelist\n",
					child);
		list_splice_init(&desc->tx_list, &dwc->free_list);
		dev_vdbg(chan2dev(&dwc->chan), "moving desc %p to freelist\n", desc);
		list_add(&desc->desc_node, &dwc->free_list);
		spin_unlock_irqrestore(&dwc->lock, flags);
	}
}

static void dwc_initialize(struct dw_dma_chan *dwc)
{
	struct dw_dma *dw = to_dw_dma(dwc->chan.device);
	struct dw_dma_slave *dws = dwc->chan.private;
	u32 cfghi = DWC_CFGH_FIFO_MODE;
	u32 cfglo = DWC_CFGL_CH_PRIOR(dwc->priority);

	if (dwc->initialized == true)
		return;

	if (dws) {
		/*
		 * We need controller-specific data to set up slave
		 * transfers.
		 */
		BUG_ON(!dws->dma_dev || dws->dma_dev != dw->dma.dev);

		cfghi |= DWC_CFGH_DST_PER(dws->dst_id);
		cfghi |= DWC_CFGH_SRC_PER(dws->src_id);
	} else {
		cfghi |= DWC_CFGH_DST_PER(dwc->dst_id);
		cfghi |= DWC_CFGH_SRC_PER(dwc->src_id);
	}

	channel_writel(dwc, CFG_LO, cfglo);
	channel_writel(dwc, CFG_HI, cfghi);

	/* Enable interrupts */
	channel_set_bit(dw, MASK.XFER, dwc->mask);
	channel_set_bit(dw, MASK.ERROR, dwc->mask);

	dwc->initialized = true;
}

/*----------------------------------------------------------------------*/

static inline unsigned int dwc_fast_fls(unsigned long long v)
{
	/*
	 * We can be a lot more clever here, but this should take care
	 * of the most common optimization.
	 */
	if (!(v & 7))
		return 3;
	else if (!(v & 3))
		return 2;
	else if (!(v & 1))
		return 1;
	return 0;
}

static inline void dwc_dump_chan_regs(struct dw_dma_chan *dwc)
{
	dev_err(chan2dev(&dwc->chan),
		"  SAR: 0x%x DAR: 0x%x LLP: 0x%x CTL: 0x%x:%08x\n",
		channel_readl(dwc, SAR),
		channel_readl(dwc, DAR),
		channel_readl(dwc, LLP),
		channel_readl(dwc, CTL_HI),
		channel_readl(dwc, CTL_LO));
}

static inline void dwc_chan_disable(struct dw_dma *dw, struct dw_dma_chan *dwc)
{
	channel_clear_bit(dw, CH_EN, dwc->mask);
	while (dma_readl(dw, CH_EN) & dwc->mask)
		cpu_relax();
}

/*----------------------------------------------------------------------*/

/* Perform single block transfer */
static inline void dwc_do_single_block(struct dw_dma_chan *dwc,
				       struct dw_desc *desc)
{
	struct dw_dma	*dw = to_dw_dma(dwc->chan.device);
	u32		ctllo;

	/*
	 * Software emulation of LLP mode relies on interrupts to continue
	 * multi block transfer.
	 */
	ctllo = desc->lli.ctllo | DWC_CTLL_INT_EN;

	channel_writel(dwc, SAR, desc->lli.sar);
	channel_writel(dwc, DAR, desc->lli.dar);
	channel_writel(dwc, CTL_LO, ctllo);
	channel_writel(dwc, CTL_HI, desc->lli.ctlhi);
	channel_set_bit(dw, CH_EN, dwc->mask);

	/* Move pointer to next descriptor */
	dwc->tx_node_active = dwc->tx_node_active->next;
}

/* Called with dwc->lock held and bh disabled */
static void dwc_dostart(struct dw_dma_chan *dwc, struct dw_desc *first)
{
	struct dw_dma	*dw = to_dw_dma(dwc->chan.device);
	unsigned long	was_soft_llp;

	/* ASSERT:  channel is idle */
	if (dma_readl(dw, CH_EN) & dwc->mask) {
		dev_err(chan2dev(&dwc->chan),
			"%s: BUG: Attempted to start non-idle channel\n",
			__func__);
		dwc_dump_chan_regs(dwc);

		/* The tasklet will hopefully advance the queue... */
		return;
	}

	if (dwc->nollp) {
		was_soft_llp = test_and_set_bit(DW_DMA_IS_SOFT_LLP,
						&dwc->flags);
		if (was_soft_llp) {
			dev_err(chan2dev(&dwc->chan),
				"BUG: Attempted to start new LLP transfer inside ongoing one\n");
			return;
		}

		dwc_initialize(dwc);

		dwc->residue = first->total_len;
		dwc->tx_node_active = &first->tx_list;

		/* Submit first block */
		dwc_do_single_block(dwc, first);

		return;
	}

	dwc_initialize(dwc);

	channel_writel(dwc, LLP, first->txd.phys);
	channel_writel(dwc, CTL_LO,
			DWC_CTLL_LLP_D_EN | DWC_CTLL_LLP_S_EN);
	channel_writel(dwc, CTL_HI, 0);
	channel_set_bit(dw, CH_EN, dwc->mask);
}

static void dwc_dostart_first_queued(struct dw_dma_chan *dwc)
{
	struct dw_desc *desc;

	if (list_empty(&dwc->queue))
		return;

	list_move(dwc->queue.next, &dwc->active_list);
	desc = dwc_first_active(dwc);
	dev_vdbg(chan2dev(&dwc->chan), "%s: started %u\n", __func__, desc->txd.cookie);
	dwc_dostart(dwc, desc);
}

/*----------------------------------------------------------------------*/

static void
dwc_descriptor_complete(struct dw_dma_chan *dwc, struct dw_desc *desc,
		bool callback_required)
{
	dma_async_tx_callback		callback = NULL;
	void				*param = NULL;
	struct dma_async_tx_descriptor	*txd = &desc->txd;
	struct dw_desc			*child;
	unsigned long			flags;

	dev_vdbg(chan2dev(&dwc->chan), "descriptor %u complete\n", txd->cookie);

	spin_lock_irqsave(&dwc->lock, flags);
	dma_cookie_complete(txd);
	if (callback_required) {
		callback = txd->callback;
		param = txd->callback_param;
	}

	/* async_tx_ack */
	list_for_each_entry(child, &desc->tx_list, desc_node)
		async_tx_ack(&child->txd);
	async_tx_ack(&desc->txd);

	list_splice_init(&desc->tx_list, &dwc->free_list);
	list_move(&desc->desc_node, &dwc->free_list);

	dma_descriptor_unmap(txd);
	spin_unlock_irqrestore(&dwc->lock, flags);

	if (callback)
		callback(param);
}

static void dwc_complete_all(struct dw_dma *dw, struct dw_dma_chan *dwc)
{
	struct dw_desc *desc, *_desc;
	LIST_HEAD(list);
	unsigned long flags;

	spin_lock_irqsave(&dwc->lock, flags);
	if (dma_readl(dw, CH_EN) & dwc->mask) {
		dev_err(chan2dev(&dwc->chan),
			"BUG: XFER bit set, but channel not idle!\n");

		/* Try to continue after resetting the channel... */
		dwc_chan_disable(dw, dwc);
	}

	/*
	 * Submit queued descriptors ASAP, i.e. before we go through
	 * the completed ones.
	 */
	list_splice_init(&dwc->active_list, &list);
	dwc_dostart_first_queued(dwc);

	spin_unlock_irqrestore(&dwc->lock, flags);

	list_for_each_entry_safe(desc, _desc, &list, desc_node)
		dwc_descriptor_complete(dwc, desc, true);
}

/* Returns how many bytes were already received from source */
static inline u32 dwc_get_sent(struct dw_dma_chan *dwc)
{
	u32 ctlhi = channel_readl(dwc, CTL_HI);
	u32 ctllo = channel_readl(dwc, CTL_LO);

	return (ctlhi & DWC_CTLH_BLOCK_TS_MASK) * (1 << (ctllo >> 4 & 7));
}

static void dwc_scan_descriptors(struct dw_dma *dw, struct dw_dma_chan *dwc)
{
	dma_addr_t llp;
	struct dw_desc *desc, *_desc;
	struct dw_desc *child;
	u32 status_xfer;
	unsigned long flags;

	spin_lock_irqsave(&dwc->lock, flags);
	llp = channel_readl(dwc, LLP);
	status_xfer = dma_readl(dw, RAW.XFER);

	if (status_xfer & dwc->mask) {
		/* Everything we've submitted is done */
		dma_writel(dw, CLEAR.XFER, dwc->mask);

		if (test_bit(DW_DMA_IS_SOFT_LLP, &dwc->flags)) {
			struct list_head *head, *active = dwc->tx_node_active;

			/*
			 * We are inside first active descriptor.
			 * Otherwise something is really wrong.
			 */
			desc = dwc_first_active(dwc);

			head = &desc->tx_list;
			if (active != head) {
				/* Update desc to reflect last sent one */
				if (active != head->next)
					desc = to_dw_desc(active->prev);

				dwc->residue -= desc->len;

				child = to_dw_desc(active);

				/* Submit next block */
				dwc_do_single_block(dwc, child);

				spin_unlock_irqrestore(&dwc->lock, flags);
				return;
			}

			/* We are done here */
			clear_bit(DW_DMA_IS_SOFT_LLP, &dwc->flags);
		}

		dwc->residue = 0;

		spin_unlock_irqrestore(&dwc->lock, flags);

		dwc_complete_all(dw, dwc);
		return;
	}

	if (list_empty(&dwc->active_list)) {
		dwc->residue = 0;
		spin_unlock_irqrestore(&dwc->lock, flags);
		return;
	}

	if (test_bit(DW_DMA_IS_SOFT_LLP, &dwc->flags)) {
		dev_vdbg(chan2dev(&dwc->chan), "%s: soft LLP mode\n", __func__);
		spin_unlock_irqrestore(&dwc->lock, flags);
		return;
	}

	dev_vdbg(chan2dev(&dwc->chan), "%s: llp=%pad\n", __func__, &llp);

	list_for_each_entry_safe(desc, _desc, &dwc->active_list, desc_node) {
		/* Initial residue value */
		dwc->residue = desc->total_len;

		/* Check first descriptors addr */
		if (desc->txd.phys == llp) {
			spin_unlock_irqrestore(&dwc->lock, flags);
			return;
		}

		/* Check first descriptors llp */
		if (desc->lli.llp == llp) {
			/* This one is currently in progress */
			dwc->residue -= dwc_get_sent(dwc);
			spin_unlock_irqrestore(&dwc->lock, flags);
			return;
		}

		dwc->residue -= desc->len;
		list_for_each_entry(child, &desc->tx_list, desc_node) {
			if (child->lli.llp == llp) {
				/* Currently in progress */
				dwc->residue -= dwc_get_sent(dwc);
				spin_unlock_irqrestore(&dwc->lock, flags);
				return;
			}
			dwc->residue -= child->len;
		}

		/*
		 * No descriptors so far seem to be in progress, i.e.
		 * this one must be done.
		 */
		spin_unlock_irqrestore(&dwc->lock, flags);
		dwc_descriptor_complete(dwc, desc, true);
		spin_lock_irqsave(&dwc->lock, flags);
	}

	dev_err(chan2dev(&dwc->chan),
		"BUG: All descriptors done, but channel not idle!\n");

	/* Try to continue after resetting the channel... */
	dwc_chan_disable(dw, dwc);

	dwc_dostart_first_queued(dwc);
	spin_unlock_irqrestore(&dwc->lock, flags);
}

static inline void dwc_dump_lli(struct dw_dma_chan *dwc, struct dw_lli *lli)
{
	dev_crit(chan2dev(&dwc->chan), "  desc: s0x%x d0x%x l0x%x c0x%x:%x\n",
		 lli->sar, lli->dar, lli->llp, lli->ctlhi, lli->ctllo);
}

static void dwc_handle_error(struct dw_dma *dw, struct dw_dma_chan *dwc)
{
	struct dw_desc *bad_desc;
	struct dw_desc *child;
	unsigned long flags;

	dwc_scan_descriptors(dw, dwc);

	spin_lock_irqsave(&dwc->lock, flags);

	/*
	 * The descriptor currently at the head of the active list is
	 * borked. Since we don't have any way to report errors, we'll
	 * just have to scream loudly and try to carry on.
	 */
	bad_desc = dwc_first_active(dwc);
	list_del_init(&bad_desc->desc_node);
	list_move(dwc->queue.next, dwc->active_list.prev);

	/* Clear the error flag and try to restart the controller */
	dma_writel(dw, CLEAR.ERROR, dwc->mask);
	if (!list_empty(&dwc->active_list))
		dwc_dostart(dwc, dwc_first_active(dwc));

	/*
	 * WARN may seem harsh, but since this only happens
	 * when someone submits a bad physical address in a
	 * descriptor, we should consider ourselves lucky that the
	 * controller flagged an error instead of scribbling over
	 * random memory locations.
	 */
	dev_WARN(chan2dev(&dwc->chan), "Bad descriptor submitted for DMA!\n"
				       "  cookie: %d\n", bad_desc->txd.cookie);
	dwc_dump_lli(dwc, &bad_desc->lli);
	list_for_each_entry(child, &bad_desc->tx_list, desc_node)
		dwc_dump_lli(dwc, &child->lli);

	spin_unlock_irqrestore(&dwc->lock, flags);

	/* Pretend the descriptor completed successfully */
	dwc_descriptor_complete(dwc, bad_desc, true);
}

/* --------------------- Cyclic DMA API extensions -------------------- */

dma_addr_t dw_dma_get_src_addr(struct dma_chan *chan)
{
	struct dw_dma_chan *dwc = to_dw_dma_chan(chan);
	return channel_readl(dwc, SAR);
}
EXPORT_SYMBOL(dw_dma_get_src_addr);

dma_addr_t dw_dma_get_dst_addr(struct dma_chan *chan)
{
	struct dw_dma_chan *dwc = to_dw_dma_chan(chan);
	return channel_readl(dwc, DAR);
}
EXPORT_SYMBOL(dw_dma_get_dst_addr);

/* Called with dwc->lock held and all DMAC interrupts disabled */
static void dwc_handle_cyclic(struct dw_dma *dw, struct dw_dma_chan *dwc,
		u32 status_err, u32 status_xfer)
{
	unsigned long flags;

	if (dwc->mask) {
		void (*callback)(void *param);
		void *callback_param;

		dev_vdbg(chan2dev(&dwc->chan), "new cyclic period llp 0x%08x\n",
				channel_readl(dwc, LLP));

		callback = dwc->cdesc->period_callback;
		callback_param = dwc->cdesc->period_callback_param;

		if (callback)
			callback(callback_param);
	}

	/*
	 * Error and transfer complete are highly unlikely, and will most
	 * likely be due to a configuration error by the user.
	 */
	if (unlikely(status_err & dwc->mask) ||
			unlikely(status_xfer & dwc->mask)) {
		int i;

		dev_err(chan2dev(&dwc->chan),
			"cyclic DMA unexpected %s interrupt, stopping DMA transfer\n",
			status_xfer ? "xfer" : "error");

		spin_lock_irqsave(&dwc->lock, flags);

		dwc_dump_chan_regs(dwc);

		dwc_chan_disable(dw, dwc);

		/* Make sure DMA does not restart by loading a new list */
		channel_writel(dwc, LLP, 0);
		channel_writel(dwc, CTL_LO, 0);
		channel_writel(dwc, CTL_HI, 0);

		dma_writel(dw, CLEAR.ERROR, dwc->mask);
		dma_writel(dw, CLEAR.XFER, dwc->mask);

		for (i = 0; i < dwc->cdesc->periods; i++)
			dwc_dump_lli(dwc, &dwc->cdesc->desc[i]->lli);

		spin_unlock_irqrestore(&dwc->lock, flags);
	}
}

/* ------------------------------------------------------------------------- */

static void dw_dma_tasklet(unsigned long data)
{
	struct dw_dma *dw = (struct dw_dma *)data;
	struct dw_dma_chan *dwc;
	u32 status_xfer;
	u32 status_err;
	int i;

	status_xfer = dma_readl(dw, RAW.XFER);
	status_err = dma_readl(dw, RAW.ERROR);

	dev_vdbg(dw->dma.dev, "%s: status_err=%x\n", __func__, status_err);

	for (i = 0; i < dw->dma.chancnt; i++) {
		dwc = &dw->chan[i];
		if (test_bit(DW_DMA_IS_CYCLIC, &dwc->flags))
			dwc_handle_cyclic(dw, dwc, status_err, status_xfer);
		else if (status_err & (1 << i))
			dwc_handle_error(dw, dwc);
		else if (status_xfer & (1 << i))
			dwc_scan_descriptors(dw, dwc);
	}

	/*
	 * Re-enable interrupts.
	 */
	channel_set_bit(dw, MASK.XFER, dw->all_chan_mask);
	channel_set_bit(dw, MASK.ERROR, dw->all_chan_mask);
}

static irqreturn_t dw_dma_interrupt(int irq, void *dev_id)
{
	struct dw_dma *dw = dev_id;
	u32 status = dma_readl(dw, STATUS_INT);

	dev_vdbg(dw->dma.dev, "%s: status=0x%x\n", __func__, status);

	/* Check if we have any interrupt from the DMAC */
	if (!status)
		return IRQ_NONE;

	/*
	 * Just disable the interrupts. We'll turn them back on in the
	 * softirq handler.
	 */
	channel_clear_bit(dw, MASK.XFER, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.ERROR, dw->all_chan_mask);

	status = dma_readl(dw, STATUS_INT);
	if (status) {
		dev_err(dw->dma.dev,
			"BUG: Unexpected interrupts pending: 0x%x\n",
			status);

		/* Try to recover */
		channel_clear_bit(dw, MASK.XFER, (1 << 8) - 1);
		channel_clear_bit(dw, MASK.SRC_TRAN, (1 << 8) - 1);
		channel_clear_bit(dw, MASK.DST_TRAN, (1 << 8) - 1);
		channel_clear_bit(dw, MASK.ERROR, (1 << 8) - 1);
	}

	tasklet_schedule(&dw->tasklet);

	return IRQ_HANDLED;
}

/*----------------------------------------------------------------------*/

static dma_cookie_t dwc_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct dw_desc		*desc = txd_to_dw_desc(tx);
	struct dw_dma_chan	*dwc = to_dw_dma_chan(tx->chan);
	dma_cookie_t		cookie;
	unsigned long		flags;

	spin_lock_irqsave(&dwc->lock, flags);
	cookie = dma_cookie_assign(tx);

	/*
	 * REVISIT: We should attempt to chain as many descriptors as
	 * possible, perhaps even appending to those already submitted
	 * for DMA. But this is hard to do in a race-free manner.
	 */

	dev_vdbg(chan2dev(tx->chan), "%s: queued %u\n", __func__, desc->txd.cookie);
	list_add_tail(&desc->desc_node, &dwc->queue);

	spin_unlock_irqrestore(&dwc->lock, flags);

	return cookie;
}

static struct dma_async_tx_descriptor *
dwc_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dw_desc		*desc;
	struct dw_desc		*first;
	struct dw_desc		*prev;
	size_t			xfer_count;
	size_t			offset;
	unsigned int		src_width;
	unsigned int		dst_width;
	unsigned int		data_width;
	u32			ctllo;

	dev_vdbg(chan2dev(chan),
			"%s: d%pad s%pad l0x%zx f0x%lx\n", __func__,
			&dest, &src, len, flags);

	if (unlikely(!len)) {
		dev_dbg(chan2dev(chan), "%s: length is zero!\n", __func__);
		return NULL;
	}

	dwc->direction = DMA_MEM_TO_MEM;

	data_width = min_t(unsigned int, dw->data_width[dwc->src_master],
			   dw->data_width[dwc->dst_master]);

	src_width = dst_width = min_t(unsigned int, data_width,
				      dwc_fast_fls(src | dest | len));

	ctllo = DWC_DEFAULT_CTLLO(chan)
			| DWC_CTLL_DST_WIDTH(dst_width)
			| DWC_CTLL_SRC_WIDTH(src_width)
			| DWC_CTLL_DST_INC
			| DWC_CTLL_SRC_INC
			| DWC_CTLL_FC_M2M;
	prev = first = NULL;

	for (offset = 0; offset < len; offset += xfer_count << src_width) {
		xfer_count = min_t(size_t, (len - offset) >> src_width,
					   dwc->block_size);

		desc = dwc_desc_get(dwc);
		if (!desc)
			goto err_desc_get;

		desc->lli.sar = src + offset;
		desc->lli.dar = dest + offset;
		desc->lli.ctllo = ctllo;
		desc->lli.ctlhi = xfer_count;
		desc->len = xfer_count << src_width;

		if (!first) {
			first = desc;
		} else {
			prev->lli.llp = desc->txd.phys;
			list_add_tail(&desc->desc_node,
					&first->tx_list);
		}
		prev = desc;
	}

	if (flags & DMA_PREP_INTERRUPT)
		/* Trigger interrupt after last block */
		prev->lli.ctllo |= DWC_CTLL_INT_EN;

	prev->lli.llp = 0;
	first->txd.flags = flags;
	first->total_len = len;

	return &first->txd;

err_desc_get:
	dwc_desc_put(dwc, first);
	return NULL;
}

static struct dma_async_tx_descriptor *
dwc_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dma_slave_config	*sconfig = &dwc->dma_sconfig;
	struct dw_desc		*prev;
	struct dw_desc		*first;
	u32			ctllo;
	dma_addr_t		reg;
	unsigned int		reg_width;
	unsigned int		mem_width;
	unsigned int		data_width;
	unsigned int		i;
	struct scatterlist	*sg;
	size_t			total_len = 0;

	dev_vdbg(chan2dev(chan), "%s\n", __func__);

	if (unlikely(!is_slave_direction(direction) || !sg_len))
		return NULL;

	dwc->direction = direction;

	prev = first = NULL;

	switch (direction) {
	case DMA_MEM_TO_DEV:
		reg_width = __fls(sconfig->dst_addr_width);
		reg = sconfig->dst_addr;
		ctllo = (DWC_DEFAULT_CTLLO(chan)
				| DWC_CTLL_DST_WIDTH(reg_width)
				| DWC_CTLL_DST_FIX
				| DWC_CTLL_SRC_INC);

		ctllo |= sconfig->device_fc ? DWC_CTLL_FC(DW_DMA_FC_P_M2P) :
			DWC_CTLL_FC(DW_DMA_FC_D_M2P);

		data_width = dw->data_width[dwc->src_master];

		for_each_sg(sgl, sg, sg_len, i) {
			struct dw_desc	*desc;
			u32		len, dlen, mem;

			mem = sg_dma_address(sg);
			len = sg_dma_len(sg);

			mem_width = min_t(unsigned int,
					  data_width, dwc_fast_fls(mem | len));

slave_sg_todev_fill_desc:
			desc = dwc_desc_get(dwc);
			if (!desc) {
				dev_err(chan2dev(chan),
					"not enough descriptors available\n");
				goto err_desc_get;
			}

			desc->lli.sar = mem;
			desc->lli.dar = reg;
			desc->lli.ctllo = ctllo | DWC_CTLL_SRC_WIDTH(mem_width);
			if ((len >> mem_width) > dwc->block_size) {
				dlen = dwc->block_size << mem_width;
				mem += dlen;
				len -= dlen;
			} else {
				dlen = len;
				len = 0;
			}

			desc->lli.ctlhi = dlen >> mem_width;
			desc->len = dlen;

			if (!first) {
				first = desc;
			} else {
				prev->lli.llp = desc->txd.phys;
				list_add_tail(&desc->desc_node,
						&first->tx_list);
			}
			prev = desc;
			total_len += dlen;

			if (len)
				goto slave_sg_todev_fill_desc;
		}
		break;
	case DMA_DEV_TO_MEM:
		reg_width = __fls(sconfig->src_addr_width);
		reg = sconfig->src_addr;
		ctllo = (DWC_DEFAULT_CTLLO(chan)
				| DWC_CTLL_SRC_WIDTH(reg_width)
				| DWC_CTLL_DST_INC
				| DWC_CTLL_SRC_FIX);

		ctllo |= sconfig->device_fc ? DWC_CTLL_FC(DW_DMA_FC_P_P2M) :
			DWC_CTLL_FC(DW_DMA_FC_D_P2M);

		data_width = dw->data_width[dwc->dst_master];

		for_each_sg(sgl, sg, sg_len, i) {
			struct dw_desc	*desc;
			u32		len, dlen, mem;

			mem = sg_dma_address(sg);
			len = sg_dma_len(sg);

			mem_width = min_t(unsigned int,
					  data_width, dwc_fast_fls(mem | len));

slave_sg_fromdev_fill_desc:
			desc = dwc_desc_get(dwc);
			if (!desc) {
				dev_err(chan2dev(chan),
						"not enough descriptors available\n");
				goto err_desc_get;
			}

			desc->lli.sar = reg;
			desc->lli.dar = mem;
			desc->lli.ctllo = ctllo | DWC_CTLL_DST_WIDTH(mem_width);
			if ((len >> reg_width) > dwc->block_size) {
				dlen = dwc->block_size << reg_width;
				mem += dlen;
				len -= dlen;
			} else {
				dlen = len;
				len = 0;
			}
			desc->lli.ctlhi = dlen >> reg_width;
			desc->len = dlen;

			if (!first) {
				first = desc;
			} else {
				prev->lli.llp = desc->txd.phys;
				list_add_tail(&desc->desc_node,
						&first->tx_list);
			}
			prev = desc;
			total_len += dlen;

			if (len)
				goto slave_sg_fromdev_fill_desc;
		}
		break;
	default:
		return NULL;
	}

	if (flags & DMA_PREP_INTERRUPT)
		/* Trigger interrupt after last block */
		prev->lli.ctllo |= DWC_CTLL_INT_EN;

	prev->lli.llp = 0;
	first->total_len = total_len;

	return &first->txd;

err_desc_get:
	dwc_desc_put(dwc, first);
	return NULL;
}

bool dw_dma_filter(struct dma_chan *chan, void *param)
{
	struct dw_dma_chan *dwc = to_dw_dma_chan(chan);
	struct dw_dma_slave *dws = param;

	if (!dws || dws->dma_dev != chan->device->dev)
		return false;

	/* We have to copy data since dws can be temporary storage */

	dwc->src_id = dws->src_id;
	dwc->dst_id = dws->dst_id;

	dwc->src_master = dws->src_master;
	dwc->dst_master = dws->dst_master;

	return true;
}
EXPORT_SYMBOL_GPL(dw_dma_filter);

/*
 * Fix sconfig's burst size according to dw_dmac. We need to convert them as:
 * 1 -> 0, 4 -> 1, 8 -> 2, 16 -> 3.
 *
 * NOTE: burst size 2 is not supported by controller.
 *
 * This can be done by finding least significant bit set: n & (n - 1)
 */
static inline void convert_burst(u32 *maxburst)
{
	if (*maxburst > 1)
		*maxburst = fls(*maxburst) - 2;
	else
		*maxburst = 0;
}

static int dwc_config(struct dma_chan *chan, struct dma_slave_config *sconfig)
{
	struct dw_dma_chan *dwc = to_dw_dma_chan(chan);

	/* Check if chan will be configured for slave transfers */
	if (!is_slave_direction(sconfig->direction))
		return -EINVAL;

	memcpy(&dwc->dma_sconfig, sconfig, sizeof(*sconfig));
	dwc->direction = sconfig->direction;

	convert_burst(&dwc->dma_sconfig.src_maxburst);
	convert_burst(&dwc->dma_sconfig.dst_maxburst);

	return 0;
}

static int dwc_pause(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	unsigned long		flags;
	unsigned int		count = 20;	/* timeout iterations */
	u32			cfglo;

	spin_lock_irqsave(&dwc->lock, flags);

	cfglo = channel_readl(dwc, CFG_LO);
	channel_writel(dwc, CFG_LO, cfglo | DWC_CFGL_CH_SUSP);
	while (!(channel_readl(dwc, CFG_LO) & DWC_CFGL_FIFO_EMPTY) && count--)
		udelay(2);

	dwc->paused = true;

	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static inline void dwc_chan_resume(struct dw_dma_chan *dwc)
{
	u32 cfglo = channel_readl(dwc, CFG_LO);

	channel_writel(dwc, CFG_LO, cfglo & ~DWC_CFGL_CH_SUSP);

	dwc->paused = false;
}

static int dwc_resume(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	unsigned long		flags;

	if (!dwc->paused)
		return 0;

	spin_lock_irqsave(&dwc->lock, flags);

	dwc_chan_resume(dwc);

	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static int dwc_terminate_all(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dw_desc		*desc, *_desc;
	unsigned long		flags;
	LIST_HEAD(list);

	spin_lock_irqsave(&dwc->lock, flags);

	clear_bit(DW_DMA_IS_SOFT_LLP, &dwc->flags);

	dwc_chan_disable(dw, dwc);

	dwc_chan_resume(dwc);

	/* active_list entries will end up before queued entries */
	list_splice_init(&dwc->queue, &list);
	list_splice_init(&dwc->active_list, &list);

	spin_unlock_irqrestore(&dwc->lock, flags);

	/* Flush all pending and queued descriptors */
	list_for_each_entry_safe(desc, _desc, &list, desc_node)
		dwc_descriptor_complete(dwc, desc, false);

	return 0;
}

static inline u32 dwc_get_residue(struct dw_dma_chan *dwc)
{
	unsigned long flags;
	u32 residue;

	spin_lock_irqsave(&dwc->lock, flags);

	residue = dwc->residue;
	if (test_bit(DW_DMA_IS_SOFT_LLP, &dwc->flags) && residue)
		residue -= dwc_get_sent(dwc);

	spin_unlock_irqrestore(&dwc->lock, flags);
	return residue;
}

static enum dma_status
dwc_tx_status(struct dma_chan *chan,
	      dma_cookie_t cookie,
	      struct dma_tx_state *txstate)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	enum dma_status		ret;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	dwc_scan_descriptors(to_dw_dma(chan->device), dwc);

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret != DMA_COMPLETE)
		dma_set_residue(txstate, dwc_get_residue(dwc));

	if (dwc->paused && ret == DMA_IN_PROGRESS)
		return DMA_PAUSED;

	return ret;
}

static void dwc_issue_pending(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	unsigned long		flags;

	spin_lock_irqsave(&dwc->lock, flags);
	if (list_empty(&dwc->active_list))
		dwc_dostart_first_queued(dwc);
	spin_unlock_irqrestore(&dwc->lock, flags);
}

/*----------------------------------------------------------------------*/

static void dw_dma_off(struct dw_dma *dw)
{
	int i;

	dma_writel(dw, CFG, 0);

	channel_clear_bit(dw, MASK.XFER, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.SRC_TRAN, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.DST_TRAN, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.ERROR, dw->all_chan_mask);

	while (dma_readl(dw, CFG) & DW_CFG_DMA_EN)
		cpu_relax();

	for (i = 0; i < dw->dma.chancnt; i++)
		dw->chan[i].initialized = false;
}

static void dw_dma_on(struct dw_dma *dw)
{
	dma_writel(dw, CFG, DW_CFG_DMA_EN);
}

static int dwc_alloc_chan_resources(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dw_desc		*desc;
	int			i;
	unsigned long		flags;

	dev_vdbg(chan2dev(chan), "%s\n", __func__);

	/* ASSERT:  channel is idle */
	if (dma_readl(dw, CH_EN) & dwc->mask) {
		dev_dbg(chan2dev(chan), "DMA channel not idle?\n");
		return -EIO;
	}

	dma_cookie_init(chan);

	/*
	 * NOTE: some controllers may have additional features that we
	 * need to initialize here, like "scatter-gather" (which
	 * doesn't mean what you think it means), and status writeback.
	 */

	/* Enable controller here if needed */
	if (!dw->in_use)
		dw_dma_on(dw);
	dw->in_use |= dwc->mask;

	spin_lock_irqsave(&dwc->lock, flags);
	i = dwc->descs_allocated;
	while (dwc->descs_allocated < NR_DESCS_PER_CHANNEL) {
		dma_addr_t phys;

		spin_unlock_irqrestore(&dwc->lock, flags);

		desc = dma_pool_alloc(dw->desc_pool, GFP_ATOMIC, &phys);
		if (!desc)
			goto err_desc_alloc;

		memset(desc, 0, sizeof(struct dw_desc));

		INIT_LIST_HEAD(&desc->tx_list);
		dma_async_tx_descriptor_init(&desc->txd, chan);
		desc->txd.tx_submit = dwc_tx_submit;
		desc->txd.flags = DMA_CTRL_ACK;
		desc->txd.phys = phys;

		dwc_desc_put(dwc, desc);

		spin_lock_irqsave(&dwc->lock, flags);
		i = ++dwc->descs_allocated;
	}

	spin_unlock_irqrestore(&dwc->lock, flags);

	dev_dbg(chan2dev(chan), "%s: allocated %d descriptors\n", __func__, i);

	return i;

err_desc_alloc:
	dev_info(chan2dev(chan), "only allocated %d descriptors\n", i);

	return i;
}

static void dwc_free_chan_resources(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dw_desc		*desc, *_desc;
	unsigned long		flags;
	LIST_HEAD(list);

	dev_dbg(chan2dev(chan), "%s: descs allocated=%u\n", __func__,
			dwc->descs_allocated);

	/* ASSERT:  channel is idle */
	BUG_ON(!list_empty(&dwc->active_list));
	BUG_ON(!list_empty(&dwc->queue));
	BUG_ON(dma_readl(to_dw_dma(chan->device), CH_EN) & dwc->mask);

	spin_lock_irqsave(&dwc->lock, flags);
	list_splice_init(&dwc->free_list, &list);
	dwc->descs_allocated = 0;
	dwc->initialized = false;

	/* Disable interrupts */
	channel_clear_bit(dw, MASK.XFER, dwc->mask);
	channel_clear_bit(dw, MASK.ERROR, dwc->mask);

	spin_unlock_irqrestore(&dwc->lock, flags);

	/* Disable controller in case it was a last user */
	dw->in_use &= ~dwc->mask;
	if (!dw->in_use)
		dw_dma_off(dw);

	list_for_each_entry_safe(desc, _desc, &list, desc_node) {
		dev_vdbg(chan2dev(chan), "  freeing descriptor %p\n", desc);
		dma_pool_free(dw->desc_pool, desc, desc->txd.phys);
	}

	dev_vdbg(chan2dev(chan), "%s: done\n", __func__);
}

/* --------------------- Cyclic DMA API extensions -------------------- */

/**
 * dw_dma_cyclic_start - start the cyclic DMA transfer
 * @chan: the DMA channel to start
 *
 * Must be called with soft interrupts disabled. Returns zero on success or
 * -errno on failure.
 */
int dw_dma_cyclic_start(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(dwc->chan.device);
	unsigned long		flags;

	if (!test_bit(DW_DMA_IS_CYCLIC, &dwc->flags)) {
		dev_err(chan2dev(&dwc->chan), "missing prep for cyclic DMA\n");
		return -ENODEV;
	}

	spin_lock_irqsave(&dwc->lock, flags);

	/* Assert channel is idle */
	if (dma_readl(dw, CH_EN) & dwc->mask) {
		dev_err(chan2dev(&dwc->chan),
			"%s: BUG: Attempted to start non-idle channel\n",
			__func__);
		dwc_dump_chan_regs(dwc);
		spin_unlock_irqrestore(&dwc->lock, flags);
		return -EBUSY;
	}

	dma_writel(dw, CLEAR.ERROR, dwc->mask);
	dma_writel(dw, CLEAR.XFER, dwc->mask);

	/* Setup DMAC channel registers */
	channel_writel(dwc, LLP, dwc->cdesc->desc[0]->txd.phys);
	channel_writel(dwc, CTL_LO, DWC_CTLL_LLP_D_EN | DWC_CTLL_LLP_S_EN);
	channel_writel(dwc, CTL_HI, 0);

	channel_set_bit(dw, CH_EN, dwc->mask);

	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}
EXPORT_SYMBOL(dw_dma_cyclic_start);

/**
 * dw_dma_cyclic_stop - stop the cyclic DMA transfer
 * @chan: the DMA channel to stop
 *
 * Must be called with soft interrupts disabled.
 */
void dw_dma_cyclic_stop(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(dwc->chan.device);
	unsigned long		flags;

	spin_lock_irqsave(&dwc->lock, flags);

	dwc_chan_disable(dw, dwc);

	spin_unlock_irqrestore(&dwc->lock, flags);
}
EXPORT_SYMBOL(dw_dma_cyclic_stop);

/**
 * dw_dma_cyclic_prep - prepare the cyclic DMA transfer
 * @chan: the DMA channel to prepare
 * @buf_addr: physical DMA address where the buffer starts
 * @buf_len: total number of bytes for the entire buffer
 * @period_len: number of bytes for each period
 * @direction: transfer direction, to or from device
 *
 * Must be called before trying to start the transfer. Returns a valid struct
 * dw_cyclic_desc if successful or an ERR_PTR(-errno) if not successful.
 */
struct dw_cyclic_desc *dw_dma_cyclic_prep(struct dma_chan *chan,
		dma_addr_t buf_addr, size_t buf_len, size_t period_len,
		enum dma_transfer_direction direction)
{
	struct dw_dma_chan		*dwc = to_dw_dma_chan(chan);
	struct dma_slave_config		*sconfig = &dwc->dma_sconfig;
	struct dw_cyclic_desc		*cdesc;
	struct dw_cyclic_desc		*retval = NULL;
	struct dw_desc			*desc;
	struct dw_desc			*last = NULL;
	unsigned long			was_cyclic;
	unsigned int			reg_width;
	unsigned int			periods;
	unsigned int			i;
	unsigned long			flags;

	spin_lock_irqsave(&dwc->lock, flags);
	if (dwc->nollp) {
		spin_unlock_irqrestore(&dwc->lock, flags);
		dev_dbg(chan2dev(&dwc->chan),
				"channel doesn't support LLP transfers\n");
		return ERR_PTR(-EINVAL);
	}

	if (!list_empty(&dwc->queue) || !list_empty(&dwc->active_list)) {
		spin_unlock_irqrestore(&dwc->lock, flags);
		dev_dbg(chan2dev(&dwc->chan),
				"queue and/or active list are not empty\n");
		return ERR_PTR(-EBUSY);
	}

	was_cyclic = test_and_set_bit(DW_DMA_IS_CYCLIC, &dwc->flags);
	spin_unlock_irqrestore(&dwc->lock, flags);
	if (was_cyclic) {
		dev_dbg(chan2dev(&dwc->chan),
				"channel already prepared for cyclic DMA\n");
		return ERR_PTR(-EBUSY);
	}

	retval = ERR_PTR(-EINVAL);

	if (unlikely(!is_slave_direction(direction)))
		goto out_err;

	dwc->direction = direction;

	if (direction == DMA_MEM_TO_DEV)
		reg_width = __ffs(sconfig->dst_addr_width);
	else
		reg_width = __ffs(sconfig->src_addr_width);

	periods = buf_len / period_len;

	/* Check for too big/unaligned periods and unaligned DMA buffer. */
	if (period_len > (dwc->block_size << reg_width))
		goto out_err;
	if (unlikely(period_len & ((1 << reg_width) - 1)))
		goto out_err;
	if (unlikely(buf_addr & ((1 << reg_width) - 1)))
		goto out_err;

	retval = ERR_PTR(-ENOMEM);

	if (periods > NR_DESCS_PER_CHANNEL)
		goto out_err;

	cdesc = kzalloc(sizeof(struct dw_cyclic_desc), GFP_KERNEL);
	if (!cdesc)
		goto out_err;

	cdesc->desc = kzalloc(sizeof(struct dw_desc *) * periods, GFP_KERNEL);
	if (!cdesc->desc)
		goto out_err_alloc;

	for (i = 0; i < periods; i++) {
		desc = dwc_desc_get(dwc);
		if (!desc)
			goto out_err_desc_get;

		switch (direction) {
		case DMA_MEM_TO_DEV:
			desc->lli.dar = sconfig->dst_addr;
			desc->lli.sar = buf_addr + (period_len * i);
			desc->lli.ctllo = (DWC_DEFAULT_CTLLO(chan)
					| DWC_CTLL_DST_WIDTH(reg_width)
					| DWC_CTLL_SRC_WIDTH(reg_width)
					| DWC_CTLL_DST_FIX
					| DWC_CTLL_SRC_INC
					| DWC_CTLL_INT_EN);

			desc->lli.ctllo |= sconfig->device_fc ?
				DWC_CTLL_FC(DW_DMA_FC_P_M2P) :
				DWC_CTLL_FC(DW_DMA_FC_D_M2P);

			break;
		case DMA_DEV_TO_MEM:
			desc->lli.dar = buf_addr + (period_len * i);
			desc->lli.sar = sconfig->src_addr;
			desc->lli.ctllo = (DWC_DEFAULT_CTLLO(chan)
					| DWC_CTLL_SRC_WIDTH(reg_width)
					| DWC_CTLL_DST_WIDTH(reg_width)
					| DWC_CTLL_DST_INC
					| DWC_CTLL_SRC_FIX
					| DWC_CTLL_INT_EN);

			desc->lli.ctllo |= sconfig->device_fc ?
				DWC_CTLL_FC(DW_DMA_FC_P_P2M) :
				DWC_CTLL_FC(DW_DMA_FC_D_P2M);

			break;
		default:
			break;
		}

		desc->lli.ctlhi = (period_len >> reg_width);
		cdesc->desc[i] = desc;

		if (last)
			last->lli.llp = desc->txd.phys;

		last = desc;
	}

	/* Let's make a cyclic list */
	last->lli.llp = cdesc->desc[0]->txd.phys;

	dev_dbg(chan2dev(&dwc->chan),
			"cyclic prepared buf %pad len %zu period %zu periods %d\n",
			&buf_addr, buf_len, period_len, periods);

	cdesc->periods = periods;
	dwc->cdesc = cdesc;

	return cdesc;

out_err_desc_get:
	while (i--)
		dwc_desc_put(dwc, cdesc->desc[i]);
out_err_alloc:
	kfree(cdesc);
out_err:
	clear_bit(DW_DMA_IS_CYCLIC, &dwc->flags);
	return (struct dw_cyclic_desc *)retval;
}
EXPORT_SYMBOL(dw_dma_cyclic_prep);

/**
 * dw_dma_cyclic_free - free a prepared cyclic DMA transfer
 * @chan: the DMA channel to free
 */
void dw_dma_cyclic_free(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(dwc->chan.device);
	struct dw_cyclic_desc	*cdesc = dwc->cdesc;
	int			i;
	unsigned long		flags;

	dev_dbg(chan2dev(&dwc->chan), "%s\n", __func__);

	if (!cdesc)
		return;

	spin_lock_irqsave(&dwc->lock, flags);

	dwc_chan_disable(dw, dwc);

	dma_writel(dw, CLEAR.ERROR, dwc->mask);
	dma_writel(dw, CLEAR.XFER, dwc->mask);

	spin_unlock_irqrestore(&dwc->lock, flags);

	for (i = 0; i < cdesc->periods; i++)
		dwc_desc_put(dwc, cdesc->desc[i]);

	kfree(cdesc->desc);
	kfree(cdesc);

	clear_bit(DW_DMA_IS_CYCLIC, &dwc->flags);
}
EXPORT_SYMBOL(dw_dma_cyclic_free);

/*----------------------------------------------------------------------*/

int dw_dma_probe(struct dw_dma_chip *chip, struct dw_dma_platform_data *pdata)
{
	struct dw_dma		*dw;
	bool			autocfg;
	unsigned int		dw_params;
	unsigned int		nr_channels;
	unsigned int		max_blk_size = 0;
	int			err;
	int			i;

	dw = devm_kzalloc(chip->dev, sizeof(*dw), GFP_KERNEL);
	if (!dw)
		return -ENOMEM;

	dw->regs = chip->regs;
	chip->dw = dw;

	pm_runtime_get_sync(chip->dev);

	dw_params = dma_read_byaddr(chip->regs, DW_PARAMS);
	autocfg = dw_params >> DW_PARAMS_EN & 0x1;

	dev_dbg(chip->dev, "DW_PARAMS: 0x%08x\n", dw_params);

	if (!pdata && autocfg) {
		pdata = devm_kzalloc(chip->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			err = -ENOMEM;
			goto err_pdata;
		}

		/* Fill platform data with the default values */
		pdata->is_private = true;
		pdata->chan_allocation_order = CHAN_ALLOCATION_ASCENDING;
		pdata->chan_priority = CHAN_PRIORITY_ASCENDING;
	} else if (!pdata || pdata->nr_channels > DW_DMA_MAX_NR_CHANNELS) {
		err = -EINVAL;
		goto err_pdata;
	}

	if (autocfg)
		nr_channels = (dw_params >> DW_PARAMS_NR_CHAN & 0x7) + 1;
	else
		nr_channels = pdata->nr_channels;

	dw->chan = devm_kcalloc(chip->dev, nr_channels, sizeof(*dw->chan),
				GFP_KERNEL);
	if (!dw->chan) {
		err = -ENOMEM;
		goto err_pdata;
	}

	/* Get hardware configuration parameters */
	if (autocfg) {
		max_blk_size = dma_readl(dw, MAX_BLK_SIZE);

		dw->nr_masters = (dw_params >> DW_PARAMS_NR_MASTER & 3) + 1;
		for (i = 0; i < dw->nr_masters; i++) {
			dw->data_width[i] =
				(dw_params >> DW_PARAMS_DATA_WIDTH(i) & 3) + 2;
		}
	} else {
		dw->nr_masters = pdata->nr_masters;
		for (i = 0; i < dw->nr_masters; i++)
			dw->data_width[i] = pdata->data_width[i];
	}

	/* Calculate all channel mask before DMA setup */
	dw->all_chan_mask = (1 << nr_channels) - 1;

	/* Force dma off, just in case */
	dw_dma_off(dw);

	/* Disable BLOCK interrupts as well */
	channel_clear_bit(dw, MASK.BLOCK, dw->all_chan_mask);

	/* Create a pool of consistent memory blocks for hardware descriptors */
	dw->desc_pool = dmam_pool_create("dw_dmac_desc_pool", chip->dev,
					 sizeof(struct dw_desc), 4, 0);
	if (!dw->desc_pool) {
		dev_err(chip->dev, "No memory for descriptors dma pool\n");
		err = -ENOMEM;
		goto err_pdata;
	}

	tasklet_init(&dw->tasklet, dw_dma_tasklet, (unsigned long)dw);

	err = request_irq(chip->irq, dw_dma_interrupt, IRQF_SHARED,
			  "dw_dmac", dw);
	if (err)
		goto err_pdata;

	INIT_LIST_HEAD(&dw->dma.channels);
	for (i = 0; i < nr_channels; i++) {
		struct dw_dma_chan	*dwc = &dw->chan[i];
		int			r = nr_channels - i - 1;

		dwc->chan.device = &dw->dma;
		dma_cookie_init(&dwc->chan);
		if (pdata->chan_allocation_order == CHAN_ALLOCATION_ASCENDING)
			list_add_tail(&dwc->chan.device_node,
					&dw->dma.channels);
		else
			list_add(&dwc->chan.device_node, &dw->dma.channels);

		/* 7 is highest priority & 0 is lowest. */
		if (pdata->chan_priority == CHAN_PRIORITY_ASCENDING)
			dwc->priority = r;
		else
			dwc->priority = i;

		dwc->ch_regs = &__dw_regs(dw)->CHAN[i];
		spin_lock_init(&dwc->lock);
		dwc->mask = 1 << i;

		INIT_LIST_HEAD(&dwc->active_list);
		INIT_LIST_HEAD(&dwc->queue);
		INIT_LIST_HEAD(&dwc->free_list);

		channel_clear_bit(dw, CH_EN, dwc->mask);

		dwc->direction = DMA_TRANS_NONE;

		/* Hardware configuration */
		if (autocfg) {
			unsigned int dwc_params;
			void __iomem *addr = chip->regs + r * sizeof(u32);

			dwc_params = dma_read_byaddr(addr, DWC_PARAMS);

			dev_dbg(chip->dev, "DWC_PARAMS[%d]: 0x%08x\n", i,
					   dwc_params);

			/*
			 * Decode maximum block size for given channel. The
			 * stored 4 bit value represents blocks from 0x00 for 3
			 * up to 0x0a for 4095.
			 */
			dwc->block_size =
				(4 << ((max_blk_size >> 4 * i) & 0xf)) - 1;
			dwc->nollp =
				(dwc_params >> DWC_PARAMS_MBLK_EN & 0x1) == 0;
		} else {
			dwc->block_size = pdata->block_size;

			/* Check if channel supports multi block transfer */
			channel_writel(dwc, LLP, 0xfffffffc);
			dwc->nollp =
				(channel_readl(dwc, LLP) & 0xfffffffc) == 0;
			channel_writel(dwc, LLP, 0);
		}
	}

	/* Clear all interrupts on all channels. */
	dma_writel(dw, CLEAR.XFER, dw->all_chan_mask);
	dma_writel(dw, CLEAR.BLOCK, dw->all_chan_mask);
	dma_writel(dw, CLEAR.SRC_TRAN, dw->all_chan_mask);
	dma_writel(dw, CLEAR.DST_TRAN, dw->all_chan_mask);
	dma_writel(dw, CLEAR.ERROR, dw->all_chan_mask);

	dma_cap_set(DMA_MEMCPY, dw->dma.cap_mask);
	dma_cap_set(DMA_SLAVE, dw->dma.cap_mask);
	if (pdata->is_private)
		dma_cap_set(DMA_PRIVATE, dw->dma.cap_mask);
	dw->dma.dev = chip->dev;
	dw->dma.device_alloc_chan_resources = dwc_alloc_chan_resources;
	dw->dma.device_free_chan_resources = dwc_free_chan_resources;

	dw->dma.device_prep_dma_memcpy = dwc_prep_dma_memcpy;
	dw->dma.device_prep_slave_sg = dwc_prep_slave_sg;

	dw->dma.device_config = dwc_config;
	dw->dma.device_pause = dwc_pause;
	dw->dma.device_resume = dwc_resume;
	dw->dma.device_terminate_all = dwc_terminate_all;

	dw->dma.device_tx_status = dwc_tx_status;
	dw->dma.device_issue_pending = dwc_issue_pending;

	/* DMA capabilities */
	dw->dma.src_addr_widths = DW_DMA_BUSWIDTHS;
	dw->dma.dst_addr_widths = DW_DMA_BUSWIDTHS;
	dw->dma.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV) |
			     BIT(DMA_MEM_TO_MEM);
	dw->dma.residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;

	err = dma_async_device_register(&dw->dma);
	if (err)
		goto err_dma_register;

	dev_info(chip->dev, "DesignWare DMA Controller, %d channels\n",
		 nr_channels);

	pm_runtime_put_sync_suspend(chip->dev);

	return 0;

err_dma_register:
	free_irq(chip->irq, dw);
err_pdata:
	pm_runtime_put_sync_suspend(chip->dev);
	return err;
}
EXPORT_SYMBOL_GPL(dw_dma_probe);

int dw_dma_remove(struct dw_dma_chip *chip)
{
	struct dw_dma		*dw = chip->dw;
	struct dw_dma_chan	*dwc, *_dwc;

	pm_runtime_get_sync(chip->dev);

	dw_dma_off(dw);
	dma_async_device_unregister(&dw->dma);

	free_irq(chip->irq, dw);
	tasklet_kill(&dw->tasklet);

	list_for_each_entry_safe(dwc, _dwc, &dw->dma.channels,
			chan.device_node) {
		list_del(&dwc->chan.device_node);
		channel_clear_bit(dw, CH_EN, dwc->mask);
	}

	pm_runtime_put_sync_suspend(chip->dev);
	return 0;
}
EXPORT_SYMBOL_GPL(dw_dma_remove);

int dw_dma_disable(struct dw_dma_chip *chip)
{
	struct dw_dma *dw = chip->dw;

	dw_dma_off(dw);
	return 0;
}
EXPORT_SYMBOL_GPL(dw_dma_disable);

int dw_dma_enable(struct dw_dma_chip *chip)
{
	struct dw_dma *dw = chip->dw;

	dw_dma_on(dw);
	return 0;
}
EXPORT_SYMBOL_GPL(dw_dma_enable);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Synopsys DesignWare DMA Controller core driver");
MODULE_AUTHOR("Haavard Skinnemoen (Atmel)");
MODULE_AUTHOR("Viresh Kumar <viresh.linux@gmail.com>");
