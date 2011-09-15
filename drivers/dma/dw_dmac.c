/*
 * Driver for the Synopsys DesignWare DMA Controller (aka DMACA on
 * AVR32 systems.)
 *
 * Copyright (C) 2007-2008 Atmel Corporation
 * Copyright (C) 2010-2011 ST Microelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "dw_dmac_regs.h"

/*
 * This supports the Synopsys "DesignWare AHB Central DMA Controller",
 * (DW_ahb_dmac) which is used with various AMBA 2.0 systems (not all
 * of which use ARM any more).  See the "Databook" from Synopsys for
 * information beyond what licensees probably provide.
 *
 * The driver has currently been tested only with the Atmel AT32AP7000,
 * which does not support descriptor writeback.
 */

#define DWC_DEFAULT_CTLLO(private) ({				\
		struct dw_dma_slave *__slave = (private);	\
		int dms = __slave ? __slave->dst_master : 0;	\
		int sms = __slave ? __slave->src_master : 1;	\
		u8 smsize = __slave ? __slave->src_msize : DW_DMA_MSIZE_16; \
		u8 dmsize = __slave ? __slave->dst_msize : DW_DMA_MSIZE_16; \
								\
		(DWC_CTLL_DST_MSIZE(dmsize)			\
		 | DWC_CTLL_SRC_MSIZE(smsize)			\
		 | DWC_CTLL_LLP_D_EN				\
		 | DWC_CTLL_LLP_S_EN				\
		 | DWC_CTLL_DMS(dms)				\
		 | DWC_CTLL_SMS(sms));				\
	})

/*
 * This is configuration-dependent and usually a funny size like 4095.
 *
 * Note that this is a transfer count, i.e. if we transfer 32-bit
 * words, we can do 16380 bytes per descriptor.
 *
 * This parameter is also system-specific.
 */
#define DWC_MAX_COUNT	4095U

/*
 * Number of descriptors to allocate for each channel. This should be
 * made configurable somehow; preferably, the clients (at least the
 * ones using slave transfers) should be able to give us a hint.
 */
#define NR_DESCS_PER_CHANNEL	64

/*----------------------------------------------------------------------*/

/*
 * Because we're not relying on writeback from the controller (it may not
 * even be configured into the core!) we don't need to use dma_pool.  These
 * descriptors -- and associated data -- are cacheable.  We do need to make
 * sure their dcache entries are written back before handing them off to
 * the controller, though.
 */

static struct device *chan2dev(struct dma_chan *chan)
{
	return &chan->dev->device;
}
static struct device *chan2parent(struct dma_chan *chan)
{
	return chan->dev->device.parent;
}

static struct dw_desc *dwc_first_active(struct dw_dma_chan *dwc)
{
	return list_entry(dwc->active_list.next, struct dw_desc, desc_node);
}

static struct dw_desc *dwc_desc_get(struct dw_dma_chan *dwc)
{
	struct dw_desc *desc, *_desc;
	struct dw_desc *ret = NULL;
	unsigned int i = 0;
	unsigned long flags;

	spin_lock_irqsave(&dwc->lock, flags);
	list_for_each_entry_safe(desc, _desc, &dwc->free_list, desc_node) {
		if (async_tx_test_ack(&desc->txd)) {
			list_del(&desc->desc_node);
			ret = desc;
			break;
		}
		dev_dbg(chan2dev(&dwc->chan), "desc %p not ACKed\n", desc);
		i++;
	}
	spin_unlock_irqrestore(&dwc->lock, flags);

	dev_vdbg(chan2dev(&dwc->chan), "scanned %u descriptors on freelist\n", i);

	return ret;
}

static void dwc_sync_desc_for_cpu(struct dw_dma_chan *dwc, struct dw_desc *desc)
{
	struct dw_desc	*child;

	list_for_each_entry(child, &desc->tx_list, desc_node)
		dma_sync_single_for_cpu(chan2parent(&dwc->chan),
				child->txd.phys, sizeof(child->lli),
				DMA_TO_DEVICE);
	dma_sync_single_for_cpu(chan2parent(&dwc->chan),
			desc->txd.phys, sizeof(desc->lli),
			DMA_TO_DEVICE);
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

		dwc_sync_desc_for_cpu(dwc, desc);

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

/* Called with dwc->lock held and bh disabled */
static dma_cookie_t
dwc_assign_cookie(struct dw_dma_chan *dwc, struct dw_desc *desc)
{
	dma_cookie_t cookie = dwc->chan.cookie;

	if (++cookie < 0)
		cookie = 1;

	dwc->chan.cookie = cookie;
	desc->txd.cookie = cookie;

	return cookie;
}

/*----------------------------------------------------------------------*/

/* Called with dwc->lock held and bh disabled */
static void dwc_dostart(struct dw_dma_chan *dwc, struct dw_desc *first)
{
	struct dw_dma	*dw = to_dw_dma(dwc->chan.device);

	/* ASSERT:  channel is idle */
	if (dma_readl(dw, CH_EN) & dwc->mask) {
		dev_err(chan2dev(&dwc->chan),
			"BUG: Attempted to start non-idle channel\n");
		dev_err(chan2dev(&dwc->chan),
			"  SAR: 0x%x DAR: 0x%x LLP: 0x%x CTL: 0x%x:%08x\n",
			channel_readl(dwc, SAR),
			channel_readl(dwc, DAR),
			channel_readl(dwc, LLP),
			channel_readl(dwc, CTL_HI),
			channel_readl(dwc, CTL_LO));

		/* The tasklet will hopefully advance the queue... */
		return;
	}

	channel_writel(dwc, LLP, first->txd.phys);
	channel_writel(dwc, CTL_LO,
			DWC_CTLL_LLP_D_EN | DWC_CTLL_LLP_S_EN);
	channel_writel(dwc, CTL_HI, 0);
	channel_set_bit(dw, CH_EN, dwc->mask);
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
	dwc->completed = txd->cookie;
	if (callback_required) {
		callback = txd->callback;
		param = txd->callback_param;
	}

	dwc_sync_desc_for_cpu(dwc, desc);

	/* async_tx_ack */
	list_for_each_entry(child, &desc->tx_list, desc_node)
		async_tx_ack(&child->txd);
	async_tx_ack(&desc->txd);

	list_splice_init(&desc->tx_list, &dwc->free_list);
	list_move(&desc->desc_node, &dwc->free_list);

	if (!dwc->chan.private) {
		struct device *parent = chan2parent(&dwc->chan);
		if (!(txd->flags & DMA_COMPL_SKIP_DEST_UNMAP)) {
			if (txd->flags & DMA_COMPL_DEST_UNMAP_SINGLE)
				dma_unmap_single(parent, desc->lli.dar,
						desc->len, DMA_FROM_DEVICE);
			else
				dma_unmap_page(parent, desc->lli.dar,
						desc->len, DMA_FROM_DEVICE);
		}
		if (!(txd->flags & DMA_COMPL_SKIP_SRC_UNMAP)) {
			if (txd->flags & DMA_COMPL_SRC_UNMAP_SINGLE)
				dma_unmap_single(parent, desc->lli.sar,
						desc->len, DMA_TO_DEVICE);
			else
				dma_unmap_page(parent, desc->lli.sar,
						desc->len, DMA_TO_DEVICE);
		}
	}

	spin_unlock_irqrestore(&dwc->lock, flags);

	if (callback_required && callback)
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
		channel_clear_bit(dw, CH_EN, dwc->mask);
		while (dma_readl(dw, CH_EN) & dwc->mask)
			cpu_relax();
	}

	/*
	 * Submit queued descriptors ASAP, i.e. before we go through
	 * the completed ones.
	 */
	list_splice_init(&dwc->active_list, &list);
	if (!list_empty(&dwc->queue)) {
		list_move(dwc->queue.next, &dwc->active_list);
		dwc_dostart(dwc, dwc_first_active(dwc));
	}

	spin_unlock_irqrestore(&dwc->lock, flags);

	list_for_each_entry_safe(desc, _desc, &list, desc_node)
		dwc_descriptor_complete(dwc, desc, true);
}

static void dwc_scan_descriptors(struct dw_dma *dw, struct dw_dma_chan *dwc)
{
	dma_addr_t llp;
	struct dw_desc *desc, *_desc;
	struct dw_desc *child;
	u32 status_xfer;
	unsigned long flags;

	spin_lock_irqsave(&dwc->lock, flags);
	/*
	 * Clear block interrupt flag before scanning so that we don't
	 * miss any, and read LLP before RAW_XFER to ensure it is
	 * valid if we decide to scan the list.
	 */
	dma_writel(dw, CLEAR.BLOCK, dwc->mask);
	llp = channel_readl(dwc, LLP);
	status_xfer = dma_readl(dw, RAW.XFER);

	if (status_xfer & dwc->mask) {
		/* Everything we've submitted is done */
		dma_writel(dw, CLEAR.XFER, dwc->mask);
		spin_unlock_irqrestore(&dwc->lock, flags);

		dwc_complete_all(dw, dwc);
		return;
	}

	if (list_empty(&dwc->active_list)) {
		spin_unlock_irqrestore(&dwc->lock, flags);
		return;
	}

	dev_vdbg(chan2dev(&dwc->chan), "scan_descriptors: llp=0x%x\n", llp);

	list_for_each_entry_safe(desc, _desc, &dwc->active_list, desc_node) {
		/* check first descriptors addr */
		if (desc->txd.phys == llp) {
			spin_unlock_irqrestore(&dwc->lock, flags);
			return;
		}

		/* check first descriptors llp */
		if (desc->lli.llp == llp) {
			/* This one is currently in progress */
			spin_unlock_irqrestore(&dwc->lock, flags);
			return;
		}

		list_for_each_entry(child, &desc->tx_list, desc_node)
			if (child->lli.llp == llp) {
				/* Currently in progress */
				spin_unlock_irqrestore(&dwc->lock, flags);
				return;
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
	channel_clear_bit(dw, CH_EN, dwc->mask);
	while (dma_readl(dw, CH_EN) & dwc->mask)
		cpu_relax();

	if (!list_empty(&dwc->queue)) {
		list_move(dwc->queue.next, &dwc->active_list);
		dwc_dostart(dwc, dwc_first_active(dwc));
	}
	spin_unlock_irqrestore(&dwc->lock, flags);
}

static void dwc_dump_lli(struct dw_dma_chan *dwc, struct dw_lli *lli)
{
	dev_printk(KERN_CRIT, chan2dev(&dwc->chan),
			"  desc: s0x%x d0x%x l0x%x c0x%x:%x\n",
			lli->sar, lli->dar, lli->llp,
			lli->ctlhi, lli->ctllo);
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
	 * KERN_CRITICAL may seem harsh, but since this only happens
	 * when someone submits a bad physical address in a
	 * descriptor, we should consider ourselves lucky that the
	 * controller flagged an error instead of scribbling over
	 * random memory locations.
	 */
	dev_printk(KERN_CRIT, chan2dev(&dwc->chan),
			"Bad descriptor submitted for DMA!\n");
	dev_printk(KERN_CRIT, chan2dev(&dwc->chan),
			"  cookie: %d\n", bad_desc->txd.cookie);
	dwc_dump_lli(dwc, &bad_desc->lli);
	list_for_each_entry(child, &bad_desc->tx_list, desc_node)
		dwc_dump_lli(dwc, &child->lli);

	spin_unlock_irqrestore(&dwc->lock, flags);

	/* Pretend the descriptor completed successfully */
	dwc_descriptor_complete(dwc, bad_desc, true);
}

/* --------------------- Cyclic DMA API extensions -------------------- */

inline dma_addr_t dw_dma_get_src_addr(struct dma_chan *chan)
{
	struct dw_dma_chan *dwc = to_dw_dma_chan(chan);
	return channel_readl(dwc, SAR);
}
EXPORT_SYMBOL(dw_dma_get_src_addr);

inline dma_addr_t dw_dma_get_dst_addr(struct dma_chan *chan)
{
	struct dw_dma_chan *dwc = to_dw_dma_chan(chan);
	return channel_readl(dwc, DAR);
}
EXPORT_SYMBOL(dw_dma_get_dst_addr);

/* called with dwc->lock held and all DMAC interrupts disabled */
static void dwc_handle_cyclic(struct dw_dma *dw, struct dw_dma_chan *dwc,
		u32 status_block, u32 status_err, u32 status_xfer)
{
	unsigned long flags;

	if (status_block & dwc->mask) {
		void (*callback)(void *param);
		void *callback_param;

		dev_vdbg(chan2dev(&dwc->chan), "new cyclic period llp 0x%08x\n",
				channel_readl(dwc, LLP));
		dma_writel(dw, CLEAR.BLOCK, dwc->mask);

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

		dev_err(chan2dev(&dwc->chan), "cyclic DMA unexpected %s "
				"interrupt, stopping DMA transfer\n",
				status_xfer ? "xfer" : "error");

		spin_lock_irqsave(&dwc->lock, flags);

		dev_err(chan2dev(&dwc->chan),
			"  SAR: 0x%x DAR: 0x%x LLP: 0x%x CTL: 0x%x:%08x\n",
			channel_readl(dwc, SAR),
			channel_readl(dwc, DAR),
			channel_readl(dwc, LLP),
			channel_readl(dwc, CTL_HI),
			channel_readl(dwc, CTL_LO));

		channel_clear_bit(dw, CH_EN, dwc->mask);
		while (dma_readl(dw, CH_EN) & dwc->mask)
			cpu_relax();

		/* make sure DMA does not restart by loading a new list */
		channel_writel(dwc, LLP, 0);
		channel_writel(dwc, CTL_LO, 0);
		channel_writel(dwc, CTL_HI, 0);

		dma_writel(dw, CLEAR.BLOCK, dwc->mask);
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
	u32 status_block;
	u32 status_xfer;
	u32 status_err;
	int i;

	status_block = dma_readl(dw, RAW.BLOCK);
	status_xfer = dma_readl(dw, RAW.XFER);
	status_err = dma_readl(dw, RAW.ERROR);

	dev_vdbg(dw->dma.dev, "tasklet: status_block=%x status_err=%x\n",
			status_block, status_err);

	for (i = 0; i < dw->dma.chancnt; i++) {
		dwc = &dw->chan[i];
		if (test_bit(DW_DMA_IS_CYCLIC, &dwc->flags))
			dwc_handle_cyclic(dw, dwc, status_block, status_err,
					status_xfer);
		else if (status_err & (1 << i))
			dwc_handle_error(dw, dwc);
		else if ((status_block | status_xfer) & (1 << i))
			dwc_scan_descriptors(dw, dwc);
	}

	/*
	 * Re-enable interrupts. Block Complete interrupts are only
	 * enabled if the INT_EN bit in the descriptor is set. This
	 * will trigger a scan before the whole list is done.
	 */
	channel_set_bit(dw, MASK.XFER, dw->all_chan_mask);
	channel_set_bit(dw, MASK.BLOCK, dw->all_chan_mask);
	channel_set_bit(dw, MASK.ERROR, dw->all_chan_mask);
}

static irqreturn_t dw_dma_interrupt(int irq, void *dev_id)
{
	struct dw_dma *dw = dev_id;
	u32 status;

	dev_vdbg(dw->dma.dev, "interrupt: status=0x%x\n",
			dma_readl(dw, STATUS_INT));

	/*
	 * Just disable the interrupts. We'll turn them back on in the
	 * softirq handler.
	 */
	channel_clear_bit(dw, MASK.XFER, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.BLOCK, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.ERROR, dw->all_chan_mask);

	status = dma_readl(dw, STATUS_INT);
	if (status) {
		dev_err(dw->dma.dev,
			"BUG: Unexpected interrupts pending: 0x%x\n",
			status);

		/* Try to recover */
		channel_clear_bit(dw, MASK.XFER, (1 << 8) - 1);
		channel_clear_bit(dw, MASK.BLOCK, (1 << 8) - 1);
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
	cookie = dwc_assign_cookie(dwc, desc);

	/*
	 * REVISIT: We should attempt to chain as many descriptors as
	 * possible, perhaps even appending to those already submitted
	 * for DMA. But this is hard to do in a race-free manner.
	 */
	if (list_empty(&dwc->active_list)) {
		dev_vdbg(chan2dev(tx->chan), "tx_submit: started %u\n",
				desc->txd.cookie);
		list_add_tail(&desc->desc_node, &dwc->active_list);
		dwc_dostart(dwc, dwc_first_active(dwc));
	} else {
		dev_vdbg(chan2dev(tx->chan), "tx_submit: queued %u\n",
				desc->txd.cookie);

		list_add_tail(&desc->desc_node, &dwc->queue);
	}

	spin_unlock_irqrestore(&dwc->lock, flags);

	return cookie;
}

static struct dma_async_tx_descriptor *
dwc_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_desc		*desc;
	struct dw_desc		*first;
	struct dw_desc		*prev;
	size_t			xfer_count;
	size_t			offset;
	unsigned int		src_width;
	unsigned int		dst_width;
	u32			ctllo;

	dev_vdbg(chan2dev(chan), "prep_dma_memcpy d0x%x s0x%x l0x%zx f0x%lx\n",
			dest, src, len, flags);

	if (unlikely(!len)) {
		dev_dbg(chan2dev(chan), "prep_dma_memcpy: length is zero!\n");
		return NULL;
	}

	/*
	 * We can be a lot more clever here, but this should take care
	 * of the most common optimization.
	 */
	if (!((src | dest  | len) & 7))
		src_width = dst_width = 3;
	else if (!((src | dest  | len) & 3))
		src_width = dst_width = 2;
	else if (!((src | dest | len) & 1))
		src_width = dst_width = 1;
	else
		src_width = dst_width = 0;

	ctllo = DWC_DEFAULT_CTLLO(chan->private)
			| DWC_CTLL_DST_WIDTH(dst_width)
			| DWC_CTLL_SRC_WIDTH(src_width)
			| DWC_CTLL_DST_INC
			| DWC_CTLL_SRC_INC
			| DWC_CTLL_FC_M2M;
	prev = first = NULL;

	for (offset = 0; offset < len; offset += xfer_count << src_width) {
		xfer_count = min_t(size_t, (len - offset) >> src_width,
				DWC_MAX_COUNT);

		desc = dwc_desc_get(dwc);
		if (!desc)
			goto err_desc_get;

		desc->lli.sar = src + offset;
		desc->lli.dar = dest + offset;
		desc->lli.ctllo = ctllo;
		desc->lli.ctlhi = xfer_count;

		if (!first) {
			first = desc;
		} else {
			prev->lli.llp = desc->txd.phys;
			dma_sync_single_for_device(chan2parent(chan),
					prev->txd.phys, sizeof(prev->lli),
					DMA_TO_DEVICE);
			list_add_tail(&desc->desc_node,
					&first->tx_list);
		}
		prev = desc;
	}


	if (flags & DMA_PREP_INTERRUPT)
		/* Trigger interrupt after last block */
		prev->lli.ctllo |= DWC_CTLL_INT_EN;

	prev->lli.llp = 0;
	dma_sync_single_for_device(chan2parent(chan),
			prev->txd.phys, sizeof(prev->lli),
			DMA_TO_DEVICE);

	first->txd.flags = flags;
	first->len = len;

	return &first->txd;

err_desc_get:
	dwc_desc_put(dwc, first);
	return NULL;
}

static struct dma_async_tx_descriptor *
dwc_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_data_direction direction,
		unsigned long flags)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma_slave	*dws = chan->private;
	struct dw_desc		*prev;
	struct dw_desc		*first;
	u32			ctllo;
	dma_addr_t		reg;
	unsigned int		reg_width;
	unsigned int		mem_width;
	unsigned int		i;
	struct scatterlist	*sg;
	size_t			total_len = 0;

	dev_vdbg(chan2dev(chan), "prep_dma_slave\n");

	if (unlikely(!dws || !sg_len))
		return NULL;

	reg_width = dws->reg_width;
	prev = first = NULL;

	switch (direction) {
	case DMA_TO_DEVICE:
		ctllo = (DWC_DEFAULT_CTLLO(chan->private)
				| DWC_CTLL_DST_WIDTH(reg_width)
				| DWC_CTLL_DST_FIX
				| DWC_CTLL_SRC_INC
				| DWC_CTLL_FC(dws->fc));
		reg = dws->tx_reg;
		for_each_sg(sgl, sg, sg_len, i) {
			struct dw_desc	*desc;
			u32		len, dlen, mem;

			mem = sg_phys(sg);
			len = sg_dma_len(sg);
			mem_width = 2;
			if (unlikely(mem & 3 || len & 3))
				mem_width = 0;

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
			if ((len >> mem_width) > DWC_MAX_COUNT) {
				dlen = DWC_MAX_COUNT << mem_width;
				mem += dlen;
				len -= dlen;
			} else {
				dlen = len;
				len = 0;
			}

			desc->lli.ctlhi = dlen >> mem_width;

			if (!first) {
				first = desc;
			} else {
				prev->lli.llp = desc->txd.phys;
				dma_sync_single_for_device(chan2parent(chan),
						prev->txd.phys,
						sizeof(prev->lli),
						DMA_TO_DEVICE);
				list_add_tail(&desc->desc_node,
						&first->tx_list);
			}
			prev = desc;
			total_len += dlen;

			if (len)
				goto slave_sg_todev_fill_desc;
		}
		break;
	case DMA_FROM_DEVICE:
		ctllo = (DWC_DEFAULT_CTLLO(chan->private)
				| DWC_CTLL_SRC_WIDTH(reg_width)
				| DWC_CTLL_DST_INC
				| DWC_CTLL_SRC_FIX
				| DWC_CTLL_FC(dws->fc));

		reg = dws->rx_reg;
		for_each_sg(sgl, sg, sg_len, i) {
			struct dw_desc	*desc;
			u32		len, dlen, mem;

			mem = sg_phys(sg);
			len = sg_dma_len(sg);
			mem_width = 2;
			if (unlikely(mem & 3 || len & 3))
				mem_width = 0;

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
			if ((len >> reg_width) > DWC_MAX_COUNT) {
				dlen = DWC_MAX_COUNT << reg_width;
				mem += dlen;
				len -= dlen;
			} else {
				dlen = len;
				len = 0;
			}
			desc->lli.ctlhi = dlen >> reg_width;

			if (!first) {
				first = desc;
			} else {
				prev->lli.llp = desc->txd.phys;
				dma_sync_single_for_device(chan2parent(chan),
						prev->txd.phys,
						sizeof(prev->lli),
						DMA_TO_DEVICE);
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
	dma_sync_single_for_device(chan2parent(chan),
			prev->txd.phys, sizeof(prev->lli),
			DMA_TO_DEVICE);

	first->len = total_len;

	return &first->txd;

err_desc_get:
	dwc_desc_put(dwc, first);
	return NULL;
}

static int dwc_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
		       unsigned long arg)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dw_desc		*desc, *_desc;
	unsigned long		flags;
	u32			cfglo;
	LIST_HEAD(list);

	if (cmd == DMA_PAUSE) {
		spin_lock_irqsave(&dwc->lock, flags);

		cfglo = channel_readl(dwc, CFG_LO);
		channel_writel(dwc, CFG_LO, cfglo | DWC_CFGL_CH_SUSP);
		while (!(channel_readl(dwc, CFG_LO) & DWC_CFGL_FIFO_EMPTY))
			cpu_relax();

		dwc->paused = true;
		spin_unlock_irqrestore(&dwc->lock, flags);
	} else if (cmd == DMA_RESUME) {
		if (!dwc->paused)
			return 0;

		spin_lock_irqsave(&dwc->lock, flags);

		cfglo = channel_readl(dwc, CFG_LO);
		channel_writel(dwc, CFG_LO, cfglo & ~DWC_CFGL_CH_SUSP);
		dwc->paused = false;

		spin_unlock_irqrestore(&dwc->lock, flags);
	} else if (cmd == DMA_TERMINATE_ALL) {
		spin_lock_irqsave(&dwc->lock, flags);

		channel_clear_bit(dw, CH_EN, dwc->mask);
		while (dma_readl(dw, CH_EN) & dwc->mask)
			cpu_relax();

		dwc->paused = false;

		/* active_list entries will end up before queued entries */
		list_splice_init(&dwc->queue, &list);
		list_splice_init(&dwc->active_list, &list);

		spin_unlock_irqrestore(&dwc->lock, flags);

		/* Flush all pending and queued descriptors */
		list_for_each_entry_safe(desc, _desc, &list, desc_node)
			dwc_descriptor_complete(dwc, desc, false);
	} else
		return -ENXIO;

	return 0;
}

static enum dma_status
dwc_tx_status(struct dma_chan *chan,
	      dma_cookie_t cookie,
	      struct dma_tx_state *txstate)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	dma_cookie_t		last_used;
	dma_cookie_t		last_complete;
	int			ret;

	last_complete = dwc->completed;
	last_used = chan->cookie;

	ret = dma_async_is_complete(cookie, last_complete, last_used);
	if (ret != DMA_SUCCESS) {
		dwc_scan_descriptors(to_dw_dma(chan->device), dwc);

		last_complete = dwc->completed;
		last_used = chan->cookie;

		ret = dma_async_is_complete(cookie, last_complete, last_used);
	}

	if (ret != DMA_SUCCESS)
		dma_set_tx_state(txstate, last_complete, last_used,
				dwc_first_active(dwc)->len);
	else
		dma_set_tx_state(txstate, last_complete, last_used, 0);

	if (dwc->paused)
		return DMA_PAUSED;

	return ret;
}

static void dwc_issue_pending(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);

	if (!list_empty(&dwc->queue))
		dwc_scan_descriptors(to_dw_dma(chan->device), dwc);
}

static int dwc_alloc_chan_resources(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dw_desc		*desc;
	struct dw_dma_slave	*dws;
	int			i;
	u32			cfghi;
	u32			cfglo;
	unsigned long		flags;

	dev_vdbg(chan2dev(chan), "alloc_chan_resources\n");

	/* ASSERT:  channel is idle */
	if (dma_readl(dw, CH_EN) & dwc->mask) {
		dev_dbg(chan2dev(chan), "DMA channel not idle?\n");
		return -EIO;
	}

	dwc->completed = chan->cookie = 1;

	cfghi = DWC_CFGH_FIFO_MODE;
	cfglo = 0;

	dws = chan->private;
	if (dws) {
		/*
		 * We need controller-specific data to set up slave
		 * transfers.
		 */
		BUG_ON(!dws->dma_dev || dws->dma_dev != dw->dma.dev);

		cfghi = dws->cfg_hi;
		cfglo = dws->cfg_lo & ~DWC_CFGL_CH_PRIOR_MASK;
	}

	cfglo |= DWC_CFGL_CH_PRIOR(dwc->priority);

	channel_writel(dwc, CFG_LO, cfglo);
	channel_writel(dwc, CFG_HI, cfghi);

	/*
	 * NOTE: some controllers may have additional features that we
	 * need to initialize here, like "scatter-gather" (which
	 * doesn't mean what you think it means), and status writeback.
	 */

	spin_lock_irqsave(&dwc->lock, flags);
	i = dwc->descs_allocated;
	while (dwc->descs_allocated < NR_DESCS_PER_CHANNEL) {
		spin_unlock_irqrestore(&dwc->lock, flags);

		desc = kzalloc(sizeof(struct dw_desc), GFP_KERNEL);
		if (!desc) {
			dev_info(chan2dev(chan),
				"only allocated %d descriptors\n", i);
			spin_lock_irqsave(&dwc->lock, flags);
			break;
		}

		INIT_LIST_HEAD(&desc->tx_list);
		dma_async_tx_descriptor_init(&desc->txd, chan);
		desc->txd.tx_submit = dwc_tx_submit;
		desc->txd.flags = DMA_CTRL_ACK;
		desc->txd.phys = dma_map_single(chan2parent(chan), &desc->lli,
				sizeof(desc->lli), DMA_TO_DEVICE);
		dwc_desc_put(dwc, desc);

		spin_lock_irqsave(&dwc->lock, flags);
		i = ++dwc->descs_allocated;
	}

	/* Enable interrupts */
	channel_set_bit(dw, MASK.XFER, dwc->mask);
	channel_set_bit(dw, MASK.BLOCK, dwc->mask);
	channel_set_bit(dw, MASK.ERROR, dwc->mask);

	spin_unlock_irqrestore(&dwc->lock, flags);

	dev_dbg(chan2dev(chan),
		"alloc_chan_resources allocated %d descriptors\n", i);

	return i;
}

static void dwc_free_chan_resources(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dw_desc		*desc, *_desc;
	unsigned long		flags;
	LIST_HEAD(list);

	dev_dbg(chan2dev(chan), "free_chan_resources (descs allocated=%u)\n",
			dwc->descs_allocated);

	/* ASSERT:  channel is idle */
	BUG_ON(!list_empty(&dwc->active_list));
	BUG_ON(!list_empty(&dwc->queue));
	BUG_ON(dma_readl(to_dw_dma(chan->device), CH_EN) & dwc->mask);

	spin_lock_irqsave(&dwc->lock, flags);
	list_splice_init(&dwc->free_list, &list);
	dwc->descs_allocated = 0;

	/* Disable interrupts */
	channel_clear_bit(dw, MASK.XFER, dwc->mask);
	channel_clear_bit(dw, MASK.BLOCK, dwc->mask);
	channel_clear_bit(dw, MASK.ERROR, dwc->mask);

	spin_unlock_irqrestore(&dwc->lock, flags);

	list_for_each_entry_safe(desc, _desc, &list, desc_node) {
		dev_vdbg(chan2dev(chan), "  freeing descriptor %p\n", desc);
		dma_unmap_single(chan2parent(chan), desc->txd.phys,
				sizeof(desc->lli), DMA_TO_DEVICE);
		kfree(desc);
	}

	dev_vdbg(chan2dev(chan), "free_chan_resources done\n");
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

	/* assert channel is idle */
	if (dma_readl(dw, CH_EN) & dwc->mask) {
		dev_err(chan2dev(&dwc->chan),
			"BUG: Attempted to start non-idle channel\n");
		dev_err(chan2dev(&dwc->chan),
			"  SAR: 0x%x DAR: 0x%x LLP: 0x%x CTL: 0x%x:%08x\n",
			channel_readl(dwc, SAR),
			channel_readl(dwc, DAR),
			channel_readl(dwc, LLP),
			channel_readl(dwc, CTL_HI),
			channel_readl(dwc, CTL_LO));
		spin_unlock_irqrestore(&dwc->lock, flags);
		return -EBUSY;
	}

	dma_writel(dw, CLEAR.BLOCK, dwc->mask);
	dma_writel(dw, CLEAR.ERROR, dwc->mask);
	dma_writel(dw, CLEAR.XFER, dwc->mask);

	/* setup DMAC channel registers */
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

	channel_clear_bit(dw, CH_EN, dwc->mask);
	while (dma_readl(dw, CH_EN) & dwc->mask)
		cpu_relax();

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
		enum dma_data_direction direction)
{
	struct dw_dma_chan		*dwc = to_dw_dma_chan(chan);
	struct dw_cyclic_desc		*cdesc;
	struct dw_cyclic_desc		*retval = NULL;
	struct dw_desc			*desc;
	struct dw_desc			*last = NULL;
	struct dw_dma_slave		*dws = chan->private;
	unsigned long			was_cyclic;
	unsigned int			reg_width;
	unsigned int			periods;
	unsigned int			i;
	unsigned long			flags;

	spin_lock_irqsave(&dwc->lock, flags);
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
	reg_width = dws->reg_width;
	periods = buf_len / period_len;

	/* Check for too big/unaligned periods and unaligned DMA buffer. */
	if (period_len > (DWC_MAX_COUNT << reg_width))
		goto out_err;
	if (unlikely(period_len & ((1 << reg_width) - 1)))
		goto out_err;
	if (unlikely(buf_addr & ((1 << reg_width) - 1)))
		goto out_err;
	if (unlikely(!(direction & (DMA_TO_DEVICE | DMA_FROM_DEVICE))))
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
		case DMA_TO_DEVICE:
			desc->lli.dar = dws->tx_reg;
			desc->lli.sar = buf_addr + (period_len * i);
			desc->lli.ctllo = (DWC_DEFAULT_CTLLO(chan->private)
					| DWC_CTLL_DST_WIDTH(reg_width)
					| DWC_CTLL_SRC_WIDTH(reg_width)
					| DWC_CTLL_DST_FIX
					| DWC_CTLL_SRC_INC
					| DWC_CTLL_FC(dws->fc)
					| DWC_CTLL_INT_EN);
			break;
		case DMA_FROM_DEVICE:
			desc->lli.dar = buf_addr + (period_len * i);
			desc->lli.sar = dws->rx_reg;
			desc->lli.ctllo = (DWC_DEFAULT_CTLLO(chan->private)
					| DWC_CTLL_SRC_WIDTH(reg_width)
					| DWC_CTLL_DST_WIDTH(reg_width)
					| DWC_CTLL_DST_INC
					| DWC_CTLL_SRC_FIX
					| DWC_CTLL_FC(dws->fc)
					| DWC_CTLL_INT_EN);
			break;
		default:
			break;
		}

		desc->lli.ctlhi = (period_len >> reg_width);
		cdesc->desc[i] = desc;

		if (last) {
			last->lli.llp = desc->txd.phys;
			dma_sync_single_for_device(chan2parent(chan),
					last->txd.phys, sizeof(last->lli),
					DMA_TO_DEVICE);
		}

		last = desc;
	}

	/* lets make a cyclic list */
	last->lli.llp = cdesc->desc[0]->txd.phys;
	dma_sync_single_for_device(chan2parent(chan), last->txd.phys,
			sizeof(last->lli), DMA_TO_DEVICE);

	dev_dbg(chan2dev(&dwc->chan), "cyclic prepared buf 0x%08x len %zu "
			"period %zu periods %d\n", buf_addr, buf_len,
			period_len, periods);

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

	dev_dbg(chan2dev(&dwc->chan), "cyclic free\n");

	if (!cdesc)
		return;

	spin_lock_irqsave(&dwc->lock, flags);

	channel_clear_bit(dw, CH_EN, dwc->mask);
	while (dma_readl(dw, CH_EN) & dwc->mask)
		cpu_relax();

	dma_writel(dw, CLEAR.BLOCK, dwc->mask);
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

static void dw_dma_off(struct dw_dma *dw)
{
	dma_writel(dw, CFG, 0);

	channel_clear_bit(dw, MASK.XFER, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.BLOCK, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.SRC_TRAN, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.DST_TRAN, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.ERROR, dw->all_chan_mask);

	while (dma_readl(dw, CFG) & DW_CFG_DMA_EN)
		cpu_relax();
}

static int __init dw_probe(struct platform_device *pdev)
{
	struct dw_dma_platform_data *pdata;
	struct resource		*io;
	struct dw_dma		*dw;
	size_t			size;
	int			irq;
	int			err;
	int			i;

	pdata = pdev->dev.platform_data;
	if (!pdata || pdata->nr_channels > DW_DMA_MAX_NR_CHANNELS)
		return -EINVAL;

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!io)
		return -EINVAL;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	size = sizeof(struct dw_dma);
	size += pdata->nr_channels * sizeof(struct dw_dma_chan);
	dw = kzalloc(size, GFP_KERNEL);
	if (!dw)
		return -ENOMEM;

	if (!request_mem_region(io->start, DW_REGLEN, pdev->dev.driver->name)) {
		err = -EBUSY;
		goto err_kfree;
	}

	dw->regs = ioremap(io->start, DW_REGLEN);
	if (!dw->regs) {
		err = -ENOMEM;
		goto err_release_r;
	}

	dw->clk = clk_get(&pdev->dev, "hclk");
	if (IS_ERR(dw->clk)) {
		err = PTR_ERR(dw->clk);
		goto err_clk;
	}
	clk_enable(dw->clk);

	/* force dma off, just in case */
	dw_dma_off(dw);

	err = request_irq(irq, dw_dma_interrupt, 0, "dw_dmac", dw);
	if (err)
		goto err_irq;

	platform_set_drvdata(pdev, dw);

	tasklet_init(&dw->tasklet, dw_dma_tasklet, (unsigned long)dw);

	dw->all_chan_mask = (1 << pdata->nr_channels) - 1;

	INIT_LIST_HEAD(&dw->dma.channels);
	for (i = 0; i < pdata->nr_channels; i++) {
		struct dw_dma_chan	*dwc = &dw->chan[i];

		dwc->chan.device = &dw->dma;
		dwc->chan.cookie = dwc->completed = 1;
		if (pdata->chan_allocation_order == CHAN_ALLOCATION_ASCENDING)
			list_add_tail(&dwc->chan.device_node,
					&dw->dma.channels);
		else
			list_add(&dwc->chan.device_node, &dw->dma.channels);

		/* 7 is highest priority & 0 is lowest. */
		if (pdata->chan_priority == CHAN_PRIORITY_ASCENDING)
			dwc->priority = 7 - i;
		else
			dwc->priority = i;

		dwc->ch_regs = &__dw_regs(dw)->CHAN[i];
		spin_lock_init(&dwc->lock);
		dwc->mask = 1 << i;

		INIT_LIST_HEAD(&dwc->active_list);
		INIT_LIST_HEAD(&dwc->queue);
		INIT_LIST_HEAD(&dwc->free_list);

		channel_clear_bit(dw, CH_EN, dwc->mask);
	}

	/* Clear/disable all interrupts on all channels. */
	dma_writel(dw, CLEAR.XFER, dw->all_chan_mask);
	dma_writel(dw, CLEAR.BLOCK, dw->all_chan_mask);
	dma_writel(dw, CLEAR.SRC_TRAN, dw->all_chan_mask);
	dma_writel(dw, CLEAR.DST_TRAN, dw->all_chan_mask);
	dma_writel(dw, CLEAR.ERROR, dw->all_chan_mask);

	channel_clear_bit(dw, MASK.XFER, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.BLOCK, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.SRC_TRAN, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.DST_TRAN, dw->all_chan_mask);
	channel_clear_bit(dw, MASK.ERROR, dw->all_chan_mask);

	dma_cap_set(DMA_MEMCPY, dw->dma.cap_mask);
	dma_cap_set(DMA_SLAVE, dw->dma.cap_mask);
	if (pdata->is_private)
		dma_cap_set(DMA_PRIVATE, dw->dma.cap_mask);
	dw->dma.dev = &pdev->dev;
	dw->dma.device_alloc_chan_resources = dwc_alloc_chan_resources;
	dw->dma.device_free_chan_resources = dwc_free_chan_resources;

	dw->dma.device_prep_dma_memcpy = dwc_prep_dma_memcpy;

	dw->dma.device_prep_slave_sg = dwc_prep_slave_sg;
	dw->dma.device_control = dwc_control;

	dw->dma.device_tx_status = dwc_tx_status;
	dw->dma.device_issue_pending = dwc_issue_pending;

	dma_writel(dw, CFG, DW_CFG_DMA_EN);

	printk(KERN_INFO "%s: DesignWare DMA Controller, %d channels\n",
			dev_name(&pdev->dev), pdata->nr_channels);

	dma_async_device_register(&dw->dma);

	return 0;

err_irq:
	clk_disable(dw->clk);
	clk_put(dw->clk);
err_clk:
	iounmap(dw->regs);
	dw->regs = NULL;
err_release_r:
	release_resource(io);
err_kfree:
	kfree(dw);
	return err;
}

static int __exit dw_remove(struct platform_device *pdev)
{
	struct dw_dma		*dw = platform_get_drvdata(pdev);
	struct dw_dma_chan	*dwc, *_dwc;
	struct resource		*io;

	dw_dma_off(dw);
	dma_async_device_unregister(&dw->dma);

	free_irq(platform_get_irq(pdev, 0), dw);
	tasklet_kill(&dw->tasklet);

	list_for_each_entry_safe(dwc, _dwc, &dw->dma.channels,
			chan.device_node) {
		list_del(&dwc->chan.device_node);
		channel_clear_bit(dw, CH_EN, dwc->mask);
	}

	clk_disable(dw->clk);
	clk_put(dw->clk);

	iounmap(dw->regs);
	dw->regs = NULL;

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(io->start, DW_REGLEN);

	kfree(dw);

	return 0;
}

static void dw_shutdown(struct platform_device *pdev)
{
	struct dw_dma	*dw = platform_get_drvdata(pdev);

	dw_dma_off(platform_get_drvdata(pdev));
	clk_disable(dw->clk);
}

static int dw_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_dma	*dw = platform_get_drvdata(pdev);

	dw_dma_off(platform_get_drvdata(pdev));
	clk_disable(dw->clk);
	return 0;
}

static int dw_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_dma	*dw = platform_get_drvdata(pdev);

	clk_enable(dw->clk);
	dma_writel(dw, CFG, DW_CFG_DMA_EN);
	return 0;
}

static const struct dev_pm_ops dw_dev_pm_ops = {
	.suspend_noirq = dw_suspend_noirq,
	.resume_noirq = dw_resume_noirq,
};

static struct platform_driver dw_driver = {
	.remove		= __exit_p(dw_remove),
	.shutdown	= dw_shutdown,
	.driver = {
		.name	= "dw_dmac",
		.pm	= &dw_dev_pm_ops,
	},
};

static int __init dw_init(void)
{
	return platform_driver_probe(&dw_driver, dw_probe);
}
subsys_initcall(dw_init);

static void __exit dw_exit(void)
{
	platform_driver_unregister(&dw_driver);
}
module_exit(dw_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Synopsys DesignWare DMA Controller driver");
MODULE_AUTHOR("Haavard Skinnemoen (Atmel)");
MODULE_AUTHOR("Viresh Kumar <viresh.kumar@st.com>");
