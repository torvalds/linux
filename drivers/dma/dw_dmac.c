/*
 * Driver for the Synopsys DesignWare DMA Controller (aka DMACA on
 * AVR32 systems.)
 *
 * Copyright (C) 2007-2008 Atmel Corporation
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

/* NOTE:  DMS+SMS is system-specific. We should get this information
 * from the platform code somehow.
 */
#define DWC_DEFAULT_CTLLO	(DWC_CTLL_DST_MSIZE(0)		\
				| DWC_CTLL_SRC_MSIZE(0)		\
				| DWC_CTLL_DMS(0)		\
				| DWC_CTLL_SMS(1)		\
				| DWC_CTLL_LLP_D_EN		\
				| DWC_CTLL_LLP_S_EN)

/*
 * This is configuration-dependent and usually a funny size like 4095.
 * Let's round it down to the nearest power of two.
 *
 * Note that this is a transfer count, i.e. if we transfer 32-bit
 * words, we can do 8192 bytes per descriptor.
 *
 * This parameter is also system-specific.
 */
#define DWC_MAX_COUNT	2048U

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

static struct dw_desc *dwc_first_active(struct dw_dma_chan *dwc)
{
	return list_entry(dwc->active_list.next, struct dw_desc, desc_node);
}

static struct dw_desc *dwc_first_queued(struct dw_dma_chan *dwc)
{
	return list_entry(dwc->queue.next, struct dw_desc, desc_node);
}

static struct dw_desc *dwc_desc_get(struct dw_dma_chan *dwc)
{
	struct dw_desc *desc, *_desc;
	struct dw_desc *ret = NULL;
	unsigned int i = 0;

	spin_lock_bh(&dwc->lock);
	list_for_each_entry_safe(desc, _desc, &dwc->free_list, desc_node) {
		if (async_tx_test_ack(&desc->txd)) {
			list_del(&desc->desc_node);
			ret = desc;
			break;
		}
		dev_dbg(&dwc->chan.dev, "desc %p not ACKed\n", desc);
		i++;
	}
	spin_unlock_bh(&dwc->lock);

	dev_vdbg(&dwc->chan.dev, "scanned %u descriptors on freelist\n", i);

	return ret;
}

static void dwc_sync_desc_for_cpu(struct dw_dma_chan *dwc, struct dw_desc *desc)
{
	struct dw_desc	*child;

	list_for_each_entry(child, &desc->txd.tx_list, desc_node)
		dma_sync_single_for_cpu(dwc->chan.dev.parent,
				child->txd.phys, sizeof(child->lli),
				DMA_TO_DEVICE);
	dma_sync_single_for_cpu(dwc->chan.dev.parent,
			desc->txd.phys, sizeof(desc->lli),
			DMA_TO_DEVICE);
}

/*
 * Move a descriptor, including any children, to the free list.
 * `desc' must not be on any lists.
 */
static void dwc_desc_put(struct dw_dma_chan *dwc, struct dw_desc *desc)
{
	if (desc) {
		struct dw_desc *child;

		dwc_sync_desc_for_cpu(dwc, desc);

		spin_lock_bh(&dwc->lock);
		list_for_each_entry(child, &desc->txd.tx_list, desc_node)
			dev_vdbg(&dwc->chan.dev,
					"moving child desc %p to freelist\n",
					child);
		list_splice_init(&desc->txd.tx_list, &dwc->free_list);
		dev_vdbg(&dwc->chan.dev, "moving desc %p to freelist\n", desc);
		list_add(&desc->desc_node, &dwc->free_list);
		spin_unlock_bh(&dwc->lock);
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
		dev_err(&dwc->chan.dev,
			"BUG: Attempted to start non-idle channel\n");
		dev_err(&dwc->chan.dev,
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
dwc_descriptor_complete(struct dw_dma_chan *dwc, struct dw_desc *desc)
{
	dma_async_tx_callback		callback;
	void				*param;
	struct dma_async_tx_descriptor	*txd = &desc->txd;

	dev_vdbg(&dwc->chan.dev, "descriptor %u complete\n", txd->cookie);

	dwc->completed = txd->cookie;
	callback = txd->callback;
	param = txd->callback_param;

	dwc_sync_desc_for_cpu(dwc, desc);
	list_splice_init(&txd->tx_list, &dwc->free_list);
	list_move(&desc->desc_node, &dwc->free_list);

	/*
	 * We use dma_unmap_page() regardless of how the buffers were
	 * mapped before they were submitted...
	 */
	if (!(txd->flags & DMA_COMPL_SKIP_DEST_UNMAP))
		dma_unmap_page(dwc->chan.dev.parent, desc->lli.dar, desc->len,
				DMA_FROM_DEVICE);
	if (!(txd->flags & DMA_COMPL_SKIP_SRC_UNMAP))
		dma_unmap_page(dwc->chan.dev.parent, desc->lli.sar, desc->len,
				DMA_TO_DEVICE);

	/*
	 * The API requires that no submissions are done from a
	 * callback, so we don't need to drop the lock here
	 */
	if (callback)
		callback(param);
}

static void dwc_complete_all(struct dw_dma *dw, struct dw_dma_chan *dwc)
{
	struct dw_desc *desc, *_desc;
	LIST_HEAD(list);

	if (dma_readl(dw, CH_EN) & dwc->mask) {
		dev_err(&dwc->chan.dev,
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
	if (!list_empty(&dwc->queue))
		dwc_dostart(dwc, dwc_first_queued(dwc));
	list_splice_init(&dwc->active_list, &list);
	list_splice_init(&dwc->queue, &dwc->active_list);

	list_for_each_entry_safe(desc, _desc, &list, desc_node)
		dwc_descriptor_complete(dwc, desc);
}

static void dwc_scan_descriptors(struct dw_dma *dw, struct dw_dma_chan *dwc)
{
	dma_addr_t llp;
	struct dw_desc *desc, *_desc;
	struct dw_desc *child;
	u32 status_xfer;

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
		dwc_complete_all(dw, dwc);
		return;
	}

	dev_vdbg(&dwc->chan.dev, "scan_descriptors: llp=0x%x\n", llp);

	list_for_each_entry_safe(desc, _desc, &dwc->active_list, desc_node) {
		if (desc->lli.llp == llp)
			/* This one is currently in progress */
			return;

		list_for_each_entry(child, &desc->txd.tx_list, desc_node)
			if (child->lli.llp == llp)
				/* Currently in progress */
				return;

		/*
		 * No descriptors so far seem to be in progress, i.e.
		 * this one must be done.
		 */
		dwc_descriptor_complete(dwc, desc);
	}

	dev_err(&dwc->chan.dev,
		"BUG: All descriptors done, but channel not idle!\n");

	/* Try to continue after resetting the channel... */
	channel_clear_bit(dw, CH_EN, dwc->mask);
	while (dma_readl(dw, CH_EN) & dwc->mask)
		cpu_relax();

	if (!list_empty(&dwc->queue)) {
		dwc_dostart(dwc, dwc_first_queued(dwc));
		list_splice_init(&dwc->queue, &dwc->active_list);
	}
}

static void dwc_dump_lli(struct dw_dma_chan *dwc, struct dw_lli *lli)
{
	dev_printk(KERN_CRIT, &dwc->chan.dev,
			"  desc: s0x%x d0x%x l0x%x c0x%x:%x\n",
			lli->sar, lli->dar, lli->llp,
			lli->ctlhi, lli->ctllo);
}

static void dwc_handle_error(struct dw_dma *dw, struct dw_dma_chan *dwc)
{
	struct dw_desc *bad_desc;
	struct dw_desc *child;

	dwc_scan_descriptors(dw, dwc);

	/*
	 * The descriptor currently at the head of the active list is
	 * borked. Since we don't have any way to report errors, we'll
	 * just have to scream loudly and try to carry on.
	 */
	bad_desc = dwc_first_active(dwc);
	list_del_init(&bad_desc->desc_node);
	list_splice_init(&dwc->queue, dwc->active_list.prev);

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
	dev_printk(KERN_CRIT, &dwc->chan.dev,
			"Bad descriptor submitted for DMA!\n");
	dev_printk(KERN_CRIT, &dwc->chan.dev,
			"  cookie: %d\n", bad_desc->txd.cookie);
	dwc_dump_lli(dwc, &bad_desc->lli);
	list_for_each_entry(child, &bad_desc->txd.tx_list, desc_node)
		dwc_dump_lli(dwc, &child->lli);

	/* Pretend the descriptor completed successfully */
	dwc_descriptor_complete(dwc, bad_desc);
}

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
		spin_lock(&dwc->lock);
		if (status_err & (1 << i))
			dwc_handle_error(dw, dwc);
		else if ((status_block | status_xfer) & (1 << i))
			dwc_scan_descriptors(dw, dwc);
		spin_unlock(&dwc->lock);
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

	spin_lock_bh(&dwc->lock);
	cookie = dwc_assign_cookie(dwc, desc);

	/*
	 * REVISIT: We should attempt to chain as many descriptors as
	 * possible, perhaps even appending to those already submitted
	 * for DMA. But this is hard to do in a race-free manner.
	 */
	if (list_empty(&dwc->active_list)) {
		dev_vdbg(&tx->chan->dev, "tx_submit: started %u\n",
				desc->txd.cookie);
		dwc_dostart(dwc, desc);
		list_add_tail(&desc->desc_node, &dwc->active_list);
	} else {
		dev_vdbg(&tx->chan->dev, "tx_submit: queued %u\n",
				desc->txd.cookie);

		list_add_tail(&desc->desc_node, &dwc->queue);
	}

	spin_unlock_bh(&dwc->lock);

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

	dev_vdbg(&chan->dev, "prep_dma_memcpy d0x%x s0x%x l0x%zx f0x%lx\n",
			dest, src, len, flags);

	if (unlikely(!len)) {
		dev_dbg(&chan->dev, "prep_dma_memcpy: length is zero!\n");
		return NULL;
	}

	/*
	 * We can be a lot more clever here, but this should take care
	 * of the most common optimization.
	 */
	if (!((src | dest  | len) & 3))
		src_width = dst_width = 2;
	else if (!((src | dest | len) & 1))
		src_width = dst_width = 1;
	else
		src_width = dst_width = 0;

	ctllo = DWC_DEFAULT_CTLLO
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
			dma_sync_single_for_device(chan->dev.parent,
					prev->txd.phys, sizeof(prev->lli),
					DMA_TO_DEVICE);
			list_add_tail(&desc->desc_node,
					&first->txd.tx_list);
		}
		prev = desc;
	}


	if (flags & DMA_PREP_INTERRUPT)
		/* Trigger interrupt after last block */
		prev->lli.ctllo |= DWC_CTLL_INT_EN;

	prev->lli.llp = 0;
	dma_sync_single_for_device(chan->dev.parent,
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
	struct dw_dma_slave	*dws = dwc->dws;
	struct dw_desc		*prev;
	struct dw_desc		*first;
	u32			ctllo;
	dma_addr_t		reg;
	unsigned int		reg_width;
	unsigned int		mem_width;
	unsigned int		i;
	struct scatterlist	*sg;
	size_t			total_len = 0;

	dev_vdbg(&chan->dev, "prep_dma_slave\n");

	if (unlikely(!dws || !sg_len))
		return NULL;

	reg_width = dws->slave.reg_width;
	prev = first = NULL;

	sg_len = dma_map_sg(chan->dev.parent, sgl, sg_len, direction);

	switch (direction) {
	case DMA_TO_DEVICE:
		ctllo = (DWC_DEFAULT_CTLLO
				| DWC_CTLL_DST_WIDTH(reg_width)
				| DWC_CTLL_DST_FIX
				| DWC_CTLL_SRC_INC
				| DWC_CTLL_FC_M2P);
		reg = dws->slave.tx_reg;
		for_each_sg(sgl, sg, sg_len, i) {
			struct dw_desc	*desc;
			u32		len;
			u32		mem;

			desc = dwc_desc_get(dwc);
			if (!desc) {
				dev_err(&chan->dev,
					"not enough descriptors available\n");
				goto err_desc_get;
			}

			mem = sg_phys(sg);
			len = sg_dma_len(sg);
			mem_width = 2;
			if (unlikely(mem & 3 || len & 3))
				mem_width = 0;

			desc->lli.sar = mem;
			desc->lli.dar = reg;
			desc->lli.ctllo = ctllo | DWC_CTLL_SRC_WIDTH(mem_width);
			desc->lli.ctlhi = len >> mem_width;

			if (!first) {
				first = desc;
			} else {
				prev->lli.llp = desc->txd.phys;
				dma_sync_single_for_device(chan->dev.parent,
						prev->txd.phys,
						sizeof(prev->lli),
						DMA_TO_DEVICE);
				list_add_tail(&desc->desc_node,
						&first->txd.tx_list);
			}
			prev = desc;
			total_len += len;
		}
		break;
	case DMA_FROM_DEVICE:
		ctllo = (DWC_DEFAULT_CTLLO
				| DWC_CTLL_SRC_WIDTH(reg_width)
				| DWC_CTLL_DST_INC
				| DWC_CTLL_SRC_FIX
				| DWC_CTLL_FC_P2M);

		reg = dws->slave.rx_reg;
		for_each_sg(sgl, sg, sg_len, i) {
			struct dw_desc	*desc;
			u32		len;
			u32		mem;

			desc = dwc_desc_get(dwc);
			if (!desc) {
				dev_err(&chan->dev,
					"not enough descriptors available\n");
				goto err_desc_get;
			}

			mem = sg_phys(sg);
			len = sg_dma_len(sg);
			mem_width = 2;
			if (unlikely(mem & 3 || len & 3))
				mem_width = 0;

			desc->lli.sar = reg;
			desc->lli.dar = mem;
			desc->lli.ctllo = ctllo | DWC_CTLL_DST_WIDTH(mem_width);
			desc->lli.ctlhi = len >> reg_width;

			if (!first) {
				first = desc;
			} else {
				prev->lli.llp = desc->txd.phys;
				dma_sync_single_for_device(chan->dev.parent,
						prev->txd.phys,
						sizeof(prev->lli),
						DMA_TO_DEVICE);
				list_add_tail(&desc->desc_node,
						&first->txd.tx_list);
			}
			prev = desc;
			total_len += len;
		}
		break;
	default:
		return NULL;
	}

	if (flags & DMA_PREP_INTERRUPT)
		/* Trigger interrupt after last block */
		prev->lli.ctllo |= DWC_CTLL_INT_EN;

	prev->lli.llp = 0;
	dma_sync_single_for_device(chan->dev.parent,
			prev->txd.phys, sizeof(prev->lli),
			DMA_TO_DEVICE);

	first->len = total_len;

	return &first->txd;

err_desc_get:
	dwc_desc_put(dwc, first);
	return NULL;
}

static void dwc_terminate_all(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dw_desc		*desc, *_desc;
	LIST_HEAD(list);

	/*
	 * This is only called when something went wrong elsewhere, so
	 * we don't really care about the data. Just disable the
	 * channel. We still have to poll the channel enable bit due
	 * to AHB/HSB limitations.
	 */
	spin_lock_bh(&dwc->lock);

	channel_clear_bit(dw, CH_EN, dwc->mask);

	while (dma_readl(dw, CH_EN) & dwc->mask)
		cpu_relax();

	/* active_list entries will end up before queued entries */
	list_splice_init(&dwc->queue, &list);
	list_splice_init(&dwc->active_list, &list);

	spin_unlock_bh(&dwc->lock);

	/* Flush all pending and queued descriptors */
	list_for_each_entry_safe(desc, _desc, &list, desc_node)
		dwc_descriptor_complete(dwc, desc);
}

static enum dma_status
dwc_is_tx_complete(struct dma_chan *chan,
		dma_cookie_t cookie,
		dma_cookie_t *done, dma_cookie_t *used)
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

	if (done)
		*done = last_complete;
	if (used)
		*used = last_used;

	return ret;
}

static void dwc_issue_pending(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);

	spin_lock_bh(&dwc->lock);
	if (!list_empty(&dwc->queue))
		dwc_scan_descriptors(to_dw_dma(chan->device), dwc);
	spin_unlock_bh(&dwc->lock);
}

static int dwc_alloc_chan_resources(struct dma_chan *chan,
		struct dma_client *client)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dw_desc		*desc;
	struct dma_slave	*slave;
	struct dw_dma_slave	*dws;
	int			i;
	u32			cfghi;
	u32			cfglo;

	dev_vdbg(&chan->dev, "alloc_chan_resources\n");

	/* Channels doing slave DMA can only handle one client. */
	if (dwc->dws || client->slave) {
		if (chan->client_count)
			return -EBUSY;
	}

	/* ASSERT:  channel is idle */
	if (dma_readl(dw, CH_EN) & dwc->mask) {
		dev_dbg(&chan->dev, "DMA channel not idle?\n");
		return -EIO;
	}

	dwc->completed = chan->cookie = 1;

	cfghi = DWC_CFGH_FIFO_MODE;
	cfglo = 0;

	slave = client->slave;
	if (slave) {
		/*
		 * We need controller-specific data to set up slave
		 * transfers.
		 */
		BUG_ON(!slave->dma_dev || slave->dma_dev != dw->dma.dev);

		dws = container_of(slave, struct dw_dma_slave, slave);

		dwc->dws = dws;
		cfghi = dws->cfg_hi;
		cfglo = dws->cfg_lo;
	} else {
		dwc->dws = NULL;
	}

	channel_writel(dwc, CFG_LO, cfglo);
	channel_writel(dwc, CFG_HI, cfghi);

	/*
	 * NOTE: some controllers may have additional features that we
	 * need to initialize here, like "scatter-gather" (which
	 * doesn't mean what you think it means), and status writeback.
	 */

	spin_lock_bh(&dwc->lock);
	i = dwc->descs_allocated;
	while (dwc->descs_allocated < NR_DESCS_PER_CHANNEL) {
		spin_unlock_bh(&dwc->lock);

		desc = kzalloc(sizeof(struct dw_desc), GFP_KERNEL);
		if (!desc) {
			dev_info(&chan->dev,
				"only allocated %d descriptors\n", i);
			spin_lock_bh(&dwc->lock);
			break;
		}

		dma_async_tx_descriptor_init(&desc->txd, chan);
		desc->txd.tx_submit = dwc_tx_submit;
		desc->txd.flags = DMA_CTRL_ACK;
		INIT_LIST_HEAD(&desc->txd.tx_list);
		desc->txd.phys = dma_map_single(chan->dev.parent, &desc->lli,
				sizeof(desc->lli), DMA_TO_DEVICE);
		dwc_desc_put(dwc, desc);

		spin_lock_bh(&dwc->lock);
		i = ++dwc->descs_allocated;
	}

	/* Enable interrupts */
	channel_set_bit(dw, MASK.XFER, dwc->mask);
	channel_set_bit(dw, MASK.BLOCK, dwc->mask);
	channel_set_bit(dw, MASK.ERROR, dwc->mask);

	spin_unlock_bh(&dwc->lock);

	dev_dbg(&chan->dev,
		"alloc_chan_resources allocated %d descriptors\n", i);

	return i;
}

static void dwc_free_chan_resources(struct dma_chan *chan)
{
	struct dw_dma_chan	*dwc = to_dw_dma_chan(chan);
	struct dw_dma		*dw = to_dw_dma(chan->device);
	struct dw_desc		*desc, *_desc;
	LIST_HEAD(list);

	dev_dbg(&chan->dev, "free_chan_resources (descs allocated=%u)\n",
			dwc->descs_allocated);

	/* ASSERT:  channel is idle */
	BUG_ON(!list_empty(&dwc->active_list));
	BUG_ON(!list_empty(&dwc->queue));
	BUG_ON(dma_readl(to_dw_dma(chan->device), CH_EN) & dwc->mask);

	spin_lock_bh(&dwc->lock);
	list_splice_init(&dwc->free_list, &list);
	dwc->descs_allocated = 0;
	dwc->dws = NULL;

	/* Disable interrupts */
	channel_clear_bit(dw, MASK.XFER, dwc->mask);
	channel_clear_bit(dw, MASK.BLOCK, dwc->mask);
	channel_clear_bit(dw, MASK.ERROR, dwc->mask);

	spin_unlock_bh(&dwc->lock);

	list_for_each_entry_safe(desc, _desc, &list, desc_node) {
		dev_vdbg(&chan->dev, "  freeing descriptor %p\n", desc);
		dma_unmap_single(chan->dev.parent, desc->txd.phys,
				sizeof(desc->lli), DMA_TO_DEVICE);
		kfree(desc);
	}

	dev_vdbg(&chan->dev, "free_chan_resources done\n");
}

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

	memset(dw, 0, sizeof *dw);

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
	for (i = 0; i < pdata->nr_channels; i++, dw->dma.chancnt++) {
		struct dw_dma_chan	*dwc = &dw->chan[i];

		dwc->chan.device = &dw->dma;
		dwc->chan.cookie = dwc->completed = 1;
		dwc->chan.chan_id = i;
		list_add_tail(&dwc->chan.device_node, &dw->dma.channels);

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
	dw->dma.dev = &pdev->dev;
	dw->dma.device_alloc_chan_resources = dwc_alloc_chan_resources;
	dw->dma.device_free_chan_resources = dwc_free_chan_resources;

	dw->dma.device_prep_dma_memcpy = dwc_prep_dma_memcpy;

	dw->dma.device_prep_slave_sg = dwc_prep_slave_sg;
	dw->dma.device_terminate_all = dwc_terminate_all;

	dw->dma.device_is_tx_complete = dwc_is_tx_complete;
	dw->dma.device_issue_pending = dwc_issue_pending;

	dma_writel(dw, CFG, DW_CFG_DMA_EN);

	printk(KERN_INFO "%s: DesignWare DMA Controller, %d channels\n",
			pdev->dev.bus_id, dw->dma.chancnt);

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

static int dw_suspend_late(struct platform_device *pdev, pm_message_t mesg)
{
	struct dw_dma	*dw = platform_get_drvdata(pdev);

	dw_dma_off(platform_get_drvdata(pdev));
	clk_disable(dw->clk);
	return 0;
}

static int dw_resume_early(struct platform_device *pdev)
{
	struct dw_dma	*dw = platform_get_drvdata(pdev);

	clk_enable(dw->clk);
	dma_writel(dw, CFG, DW_CFG_DMA_EN);
	return 0;

}

static struct platform_driver dw_driver = {
	.remove		= __exit_p(dw_remove),
	.shutdown	= dw_shutdown,
	.suspend_late	= dw_suspend_late,
	.resume_early	= dw_resume_early,
	.driver = {
		.name	= "dw_dmac",
	},
};

static int __init dw_init(void)
{
	return platform_driver_probe(&dw_driver, dw_probe);
}
module_init(dw_init);

static void __exit dw_exit(void)
{
	platform_driver_unregister(&dw_driver);
}
module_exit(dw_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Synopsys DesignWare DMA Controller driver");
MODULE_AUTHOR("Haavard Skinnemoen <haavard.skinnemoen@atmel.com>");
