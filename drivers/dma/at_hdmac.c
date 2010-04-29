/*
 * Driver for the Atmel AHB DMA Controller (aka HDMA or DMAC on AT91 systems)
 *
 * Copyright (C) 2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *
 * This supports the Atmel AHB DMA Controller,
 *
 * The driver has currently been tested with the Atmel AT91SAM9RL
 * and AT91SAM9G45 series.
 */

#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "at_hdmac_regs.h"

/*
 * Glossary
 * --------
 *
 * at_hdmac		: Name of the ATmel AHB DMA Controller
 * at_dma_ / atdma	: ATmel DMA controller entity related
 * atc_	/ atchan	: ATmel DMA Channel entity related
 */

#define	ATC_DEFAULT_CFG		(ATC_FIFOCFG_HALFFIFO)
#define	ATC_DEFAULT_CTRLA	(0)
#define	ATC_DEFAULT_CTRLB	(ATC_SIF(0)	\
				|ATC_DIF(1))

/*
 * Initial number of descriptors to allocate for each channel. This could
 * be increased during dma usage.
 */
static unsigned int init_nr_desc_per_channel = 64;
module_param(init_nr_desc_per_channel, uint, 0644);
MODULE_PARM_DESC(init_nr_desc_per_channel,
		 "initial descriptors per channel (default: 64)");


/* prototypes */
static dma_cookie_t atc_tx_submit(struct dma_async_tx_descriptor *tx);


/*----------------------------------------------------------------------*/

static struct at_desc *atc_first_active(struct at_dma_chan *atchan)
{
	return list_first_entry(&atchan->active_list,
				struct at_desc, desc_node);
}

static struct at_desc *atc_first_queued(struct at_dma_chan *atchan)
{
	return list_first_entry(&atchan->queue,
				struct at_desc, desc_node);
}

/**
 * atc_alloc_descriptor - allocate and return an initilized descriptor
 * @chan: the channel to allocate descriptors for
 * @gfp_flags: GFP allocation flags
 *
 * Note: The ack-bit is positioned in the descriptor flag at creation time
 *       to make initial allocation more convenient. This bit will be cleared
 *       and control will be given to client at usage time (during
 *       preparation functions).
 */
static struct at_desc *atc_alloc_descriptor(struct dma_chan *chan,
					    gfp_t gfp_flags)
{
	struct at_desc	*desc = NULL;
	struct at_dma	*atdma = to_at_dma(chan->device);
	dma_addr_t phys;

	desc = dma_pool_alloc(atdma->dma_desc_pool, gfp_flags, &phys);
	if (desc) {
		memset(desc, 0, sizeof(struct at_desc));
		INIT_LIST_HEAD(&desc->tx_list);
		dma_async_tx_descriptor_init(&desc->txd, chan);
		/* txd.flags will be overwritten in prep functions */
		desc->txd.flags = DMA_CTRL_ACK;
		desc->txd.tx_submit = atc_tx_submit;
		desc->txd.phys = phys;
	}

	return desc;
}

/**
 * atc_desc_get - get an unused descriptor from free_list
 * @atchan: channel we want a new descriptor for
 */
static struct at_desc *atc_desc_get(struct at_dma_chan *atchan)
{
	struct at_desc *desc, *_desc;
	struct at_desc *ret = NULL;
	unsigned int i = 0;
	LIST_HEAD(tmp_list);

	spin_lock_bh(&atchan->lock);
	list_for_each_entry_safe(desc, _desc, &atchan->free_list, desc_node) {
		i++;
		if (async_tx_test_ack(&desc->txd)) {
			list_del(&desc->desc_node);
			ret = desc;
			break;
		}
		dev_dbg(chan2dev(&atchan->chan_common),
				"desc %p not ACKed\n", desc);
	}
	spin_unlock_bh(&atchan->lock);
	dev_vdbg(chan2dev(&atchan->chan_common),
		"scanned %u descriptors on freelist\n", i);

	/* no more descriptor available in initial pool: create one more */
	if (!ret) {
		ret = atc_alloc_descriptor(&atchan->chan_common, GFP_ATOMIC);
		if (ret) {
			spin_lock_bh(&atchan->lock);
			atchan->descs_allocated++;
			spin_unlock_bh(&atchan->lock);
		} else {
			dev_err(chan2dev(&atchan->chan_common),
					"not enough descriptors available\n");
		}
	}

	return ret;
}

/**
 * atc_desc_put - move a descriptor, including any children, to the free list
 * @atchan: channel we work on
 * @desc: descriptor, at the head of a chain, to move to free list
 */
static void atc_desc_put(struct at_dma_chan *atchan, struct at_desc *desc)
{
	if (desc) {
		struct at_desc *child;

		spin_lock_bh(&atchan->lock);
		list_for_each_entry(child, &desc->tx_list, desc_node)
			dev_vdbg(chan2dev(&atchan->chan_common),
					"moving child desc %p to freelist\n",
					child);
		list_splice_init(&desc->tx_list, &atchan->free_list);
		dev_vdbg(chan2dev(&atchan->chan_common),
			 "moving desc %p to freelist\n", desc);
		list_add(&desc->desc_node, &atchan->free_list);
		spin_unlock_bh(&atchan->lock);
	}
}

/**
 * atc_assign_cookie - compute and assign new cookie
 * @atchan: channel we work on
 * @desc: descriptor to asign cookie for
 *
 * Called with atchan->lock held and bh disabled
 */
static dma_cookie_t
atc_assign_cookie(struct at_dma_chan *atchan, struct at_desc *desc)
{
	dma_cookie_t cookie = atchan->chan_common.cookie;

	if (++cookie < 0)
		cookie = 1;

	atchan->chan_common.cookie = cookie;
	desc->txd.cookie = cookie;

	return cookie;
}

/**
 * atc_dostart - starts the DMA engine for real
 * @atchan: the channel we want to start
 * @first: first descriptor in the list we want to begin with
 *
 * Called with atchan->lock held and bh disabled
 */
static void atc_dostart(struct at_dma_chan *atchan, struct at_desc *first)
{
	struct at_dma	*atdma = to_at_dma(atchan->chan_common.device);

	/* ASSERT:  channel is idle */
	if (atc_chan_is_enabled(atchan)) {
		dev_err(chan2dev(&atchan->chan_common),
			"BUG: Attempted to start non-idle channel\n");
		dev_err(chan2dev(&atchan->chan_common),
			"  channel: s0x%x d0x%x ctrl0x%x:0x%x l0x%x\n",
			channel_readl(atchan, SADDR),
			channel_readl(atchan, DADDR),
			channel_readl(atchan, CTRLA),
			channel_readl(atchan, CTRLB),
			channel_readl(atchan, DSCR));

		/* The tasklet will hopefully advance the queue... */
		return;
	}

	vdbg_dump_regs(atchan);

	/* clear any pending interrupt */
	while (dma_readl(atdma, EBCISR))
		cpu_relax();

	channel_writel(atchan, SADDR, 0);
	channel_writel(atchan, DADDR, 0);
	channel_writel(atchan, CTRLA, 0);
	channel_writel(atchan, CTRLB, 0);
	channel_writel(atchan, DSCR, first->txd.phys);
	dma_writel(atdma, CHER, atchan->mask);

	vdbg_dump_regs(atchan);
}

/**
 * atc_chain_complete - finish work for one transaction chain
 * @atchan: channel we work on
 * @desc: descriptor at the head of the chain we want do complete
 *
 * Called with atchan->lock held and bh disabled */
static void
atc_chain_complete(struct at_dma_chan *atchan, struct at_desc *desc)
{
	dma_async_tx_callback		callback;
	void				*param;
	struct dma_async_tx_descriptor	*txd = &desc->txd;

	dev_vdbg(chan2dev(&atchan->chan_common),
		"descriptor %u complete\n", txd->cookie);

	atchan->completed_cookie = txd->cookie;
	callback = txd->callback;
	param = txd->callback_param;

	/* move children to free_list */
	list_splice_init(&desc->tx_list, &atchan->free_list);
	/* move myself to free_list */
	list_move(&desc->desc_node, &atchan->free_list);

	/* unmap dma addresses */
	if (!atchan->chan_common.private) {
		struct device *parent = chan2parent(&atchan->chan_common);
		if (!(txd->flags & DMA_COMPL_SKIP_DEST_UNMAP)) {
			if (txd->flags & DMA_COMPL_DEST_UNMAP_SINGLE)
				dma_unmap_single(parent,
						desc->lli.daddr,
						desc->len, DMA_FROM_DEVICE);
			else
				dma_unmap_page(parent,
						desc->lli.daddr,
						desc->len, DMA_FROM_DEVICE);
		}
		if (!(txd->flags & DMA_COMPL_SKIP_SRC_UNMAP)) {
			if (txd->flags & DMA_COMPL_SRC_UNMAP_SINGLE)
				dma_unmap_single(parent,
						desc->lli.saddr,
						desc->len, DMA_TO_DEVICE);
			else
				dma_unmap_page(parent,
						desc->lli.saddr,
						desc->len, DMA_TO_DEVICE);
		}
	}

	/*
	 * The API requires that no submissions are done from a
	 * callback, so we don't need to drop the lock here
	 */
	if (callback)
		callback(param);

	dma_run_dependencies(txd);
}

/**
 * atc_complete_all - finish work for all transactions
 * @atchan: channel to complete transactions for
 *
 * Eventually submit queued descriptors if any
 *
 * Assume channel is idle while calling this function
 * Called with atchan->lock held and bh disabled
 */
static void atc_complete_all(struct at_dma_chan *atchan)
{
	struct at_desc *desc, *_desc;
	LIST_HEAD(list);

	dev_vdbg(chan2dev(&atchan->chan_common), "complete all\n");

	BUG_ON(atc_chan_is_enabled(atchan));

	/*
	 * Submit queued descriptors ASAP, i.e. before we go through
	 * the completed ones.
	 */
	if (!list_empty(&atchan->queue))
		atc_dostart(atchan, atc_first_queued(atchan));
	/* empty active_list now it is completed */
	list_splice_init(&atchan->active_list, &list);
	/* empty queue list by moving descriptors (if any) to active_list */
	list_splice_init(&atchan->queue, &atchan->active_list);

	list_for_each_entry_safe(desc, _desc, &list, desc_node)
		atc_chain_complete(atchan, desc);
}

/**
 * atc_cleanup_descriptors - cleanup up finished descriptors in active_list
 * @atchan: channel to be cleaned up
 *
 * Called with atchan->lock held and bh disabled
 */
static void atc_cleanup_descriptors(struct at_dma_chan *atchan)
{
	struct at_desc	*desc, *_desc;
	struct at_desc	*child;

	dev_vdbg(chan2dev(&atchan->chan_common), "cleanup descriptors\n");

	list_for_each_entry_safe(desc, _desc, &atchan->active_list, desc_node) {
		if (!(desc->lli.ctrla & ATC_DONE))
			/* This one is currently in progress */
			return;

		list_for_each_entry(child, &desc->tx_list, desc_node)
			if (!(child->lli.ctrla & ATC_DONE))
				/* Currently in progress */
				return;

		/*
		 * No descriptors so far seem to be in progress, i.e.
		 * this chain must be done.
		 */
		atc_chain_complete(atchan, desc);
	}
}

/**
 * atc_advance_work - at the end of a transaction, move forward
 * @atchan: channel where the transaction ended
 *
 * Called with atchan->lock held and bh disabled
 */
static void atc_advance_work(struct at_dma_chan *atchan)
{
	dev_vdbg(chan2dev(&atchan->chan_common), "advance_work\n");

	if (list_empty(&atchan->active_list) ||
	    list_is_singular(&atchan->active_list)) {
		atc_complete_all(atchan);
	} else {
		atc_chain_complete(atchan, atc_first_active(atchan));
		/* advance work */
		atc_dostart(atchan, atc_first_active(atchan));
	}
}


/**
 * atc_handle_error - handle errors reported by DMA controller
 * @atchan: channel where error occurs
 *
 * Called with atchan->lock held and bh disabled
 */
static void atc_handle_error(struct at_dma_chan *atchan)
{
	struct at_desc *bad_desc;
	struct at_desc *child;

	/*
	 * The descriptor currently at the head of the active list is
	 * broked. Since we don't have any way to report errors, we'll
	 * just have to scream loudly and try to carry on.
	 */
	bad_desc = atc_first_active(atchan);
	list_del_init(&bad_desc->desc_node);

	/* As we are stopped, take advantage to push queued descriptors
	 * in active_list */
	list_splice_init(&atchan->queue, atchan->active_list.prev);

	/* Try to restart the controller */
	if (!list_empty(&atchan->active_list))
		atc_dostart(atchan, atc_first_active(atchan));

	/*
	 * KERN_CRITICAL may seem harsh, but since this only happens
	 * when someone submits a bad physical address in a
	 * descriptor, we should consider ourselves lucky that the
	 * controller flagged an error instead of scribbling over
	 * random memory locations.
	 */
	dev_crit(chan2dev(&atchan->chan_common),
			"Bad descriptor submitted for DMA!\n");
	dev_crit(chan2dev(&atchan->chan_common),
			"  cookie: %d\n", bad_desc->txd.cookie);
	atc_dump_lli(atchan, &bad_desc->lli);
	list_for_each_entry(child, &bad_desc->tx_list, desc_node)
		atc_dump_lli(atchan, &child->lli);

	/* Pretend the descriptor completed successfully */
	atc_chain_complete(atchan, bad_desc);
}


/*--  IRQ & Tasklet  ---------------------------------------------------*/

static void atc_tasklet(unsigned long data)
{
	struct at_dma_chan *atchan = (struct at_dma_chan *)data;

	/* Channel cannot be enabled here */
	if (atc_chan_is_enabled(atchan)) {
		dev_err(chan2dev(&atchan->chan_common),
			"BUG: channel enabled in tasklet\n");
		return;
	}

	spin_lock(&atchan->lock);
	if (test_and_clear_bit(0, &atchan->error_status))
		atc_handle_error(atchan);
	else
		atc_advance_work(atchan);

	spin_unlock(&atchan->lock);
}

static irqreturn_t at_dma_interrupt(int irq, void *dev_id)
{
	struct at_dma		*atdma = (struct at_dma *)dev_id;
	struct at_dma_chan	*atchan;
	int			i;
	u32			status, pending, imr;
	int			ret = IRQ_NONE;

	do {
		imr = dma_readl(atdma, EBCIMR);
		status = dma_readl(atdma, EBCISR);
		pending = status & imr;

		if (!pending)
			break;

		dev_vdbg(atdma->dma_common.dev,
			"interrupt: status = 0x%08x, 0x%08x, 0x%08x\n",
			 status, imr, pending);

		for (i = 0; i < atdma->dma_common.chancnt; i++) {
			atchan = &atdma->chan[i];
			if (pending & (AT_DMA_CBTC(i) | AT_DMA_ERR(i))) {
				if (pending & AT_DMA_ERR(i)) {
					/* Disable channel on AHB error */
					dma_writel(atdma, CHDR, atchan->mask);
					/* Give information to tasklet */
					set_bit(0, &atchan->error_status);
				}
				tasklet_schedule(&atchan->tasklet);
				ret = IRQ_HANDLED;
			}
		}

	} while (pending);

	return ret;
}


/*--  DMA Engine API  --------------------------------------------------*/

/**
 * atc_tx_submit - set the prepared descriptor(s) to be executed by the engine
 * @desc: descriptor at the head of the transaction chain
 *
 * Queue chain if DMA engine is working already
 *
 * Cookie increment and adding to active_list or queue must be atomic
 */
static dma_cookie_t atc_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct at_desc		*desc = txd_to_at_desc(tx);
	struct at_dma_chan	*atchan = to_at_dma_chan(tx->chan);
	dma_cookie_t		cookie;

	spin_lock_bh(&atchan->lock);
	cookie = atc_assign_cookie(atchan, desc);

	if (list_empty(&atchan->active_list)) {
		dev_vdbg(chan2dev(tx->chan), "tx_submit: started %u\n",
				desc->txd.cookie);
		atc_dostart(atchan, desc);
		list_add_tail(&desc->desc_node, &atchan->active_list);
	} else {
		dev_vdbg(chan2dev(tx->chan), "tx_submit: queued %u\n",
				desc->txd.cookie);
		list_add_tail(&desc->desc_node, &atchan->queue);
	}

	spin_unlock_bh(&atchan->lock);

	return cookie;
}

/**
 * atc_prep_dma_memcpy - prepare a memcpy operation
 * @chan: the channel to prepare operation on
 * @dest: operation virtual destination address
 * @src: operation virtual source address
 * @len: operation length
 * @flags: tx descriptor status flags
 */
static struct dma_async_tx_descriptor *
atc_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct at_desc		*desc = NULL;
	struct at_desc		*first = NULL;
	struct at_desc		*prev = NULL;
	size_t			xfer_count;
	size_t			offset;
	unsigned int		src_width;
	unsigned int		dst_width;
	u32			ctrla;
	u32			ctrlb;

	dev_vdbg(chan2dev(chan), "prep_dma_memcpy: d0x%x s0x%x l0x%zx f0x%lx\n",
			dest, src, len, flags);

	if (unlikely(!len)) {
		dev_dbg(chan2dev(chan), "prep_dma_memcpy: length is zero!\n");
		return NULL;
	}

	ctrla =   ATC_DEFAULT_CTRLA;
	ctrlb =   ATC_DEFAULT_CTRLB
		| ATC_SRC_ADDR_MODE_INCR
		| ATC_DST_ADDR_MODE_INCR
		| ATC_FC_MEM2MEM;

	/*
	 * We can be a lot more clever here, but this should take care
	 * of the most common optimization.
	 */
	if (!((src | dest  | len) & 3)) {
		ctrla |= ATC_SRC_WIDTH_WORD | ATC_DST_WIDTH_WORD;
		src_width = dst_width = 2;
	} else if (!((src | dest | len) & 1)) {
		ctrla |= ATC_SRC_WIDTH_HALFWORD | ATC_DST_WIDTH_HALFWORD;
		src_width = dst_width = 1;
	} else {
		ctrla |= ATC_SRC_WIDTH_BYTE | ATC_DST_WIDTH_BYTE;
		src_width = dst_width = 0;
	}

	for (offset = 0; offset < len; offset += xfer_count << src_width) {
		xfer_count = min_t(size_t, (len - offset) >> src_width,
				ATC_BTSIZE_MAX);

		desc = atc_desc_get(atchan);
		if (!desc)
			goto err_desc_get;

		desc->lli.saddr = src + offset;
		desc->lli.daddr = dest + offset;
		desc->lli.ctrla = ctrla | xfer_count;
		desc->lli.ctrlb = ctrlb;

		desc->txd.cookie = 0;
		async_tx_ack(&desc->txd);

		if (!first) {
			first = desc;
		} else {
			/* inform the HW lli about chaining */
			prev->lli.dscr = desc->txd.phys;
			/* insert the link descriptor to the LD ring */
			list_add_tail(&desc->desc_node,
					&first->tx_list);
		}
		prev = desc;
	}

	/* First descriptor of the chain embedds additional information */
	first->txd.cookie = -EBUSY;
	first->len = len;

	/* set end-of-link to the last link descriptor of list*/
	set_desc_eol(desc);

	desc->txd.flags = flags; /* client is in control of this ack */

	return &first->txd;

err_desc_get:
	atc_desc_put(atchan, first);
	return NULL;
}


/**
 * atc_prep_slave_sg - prepare descriptors for a DMA_SLAVE transaction
 * @chan: DMA channel
 * @sgl: scatterlist to transfer to/from
 * @sg_len: number of entries in @scatterlist
 * @direction: DMA direction
 * @flags: tx descriptor status flags
 */
static struct dma_async_tx_descriptor *
atc_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_data_direction direction,
		unsigned long flags)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct at_dma_slave	*atslave = chan->private;
	struct at_desc		*first = NULL;
	struct at_desc		*prev = NULL;
	u32			ctrla;
	u32			ctrlb;
	dma_addr_t		reg;
	unsigned int		reg_width;
	unsigned int		mem_width;
	unsigned int		i;
	struct scatterlist	*sg;
	size_t			total_len = 0;

	dev_vdbg(chan2dev(chan), "prep_slave_sg: %s f0x%lx\n",
			direction == DMA_TO_DEVICE ? "TO DEVICE" : "FROM DEVICE",
			flags);

	if (unlikely(!atslave || !sg_len)) {
		dev_dbg(chan2dev(chan), "prep_dma_memcpy: length is zero!\n");
		return NULL;
	}

	reg_width = atslave->reg_width;

	ctrla = ATC_DEFAULT_CTRLA | atslave->ctrla;
	ctrlb = ATC_DEFAULT_CTRLB | ATC_IEN;

	switch (direction) {
	case DMA_TO_DEVICE:
		ctrla |=  ATC_DST_WIDTH(reg_width);
		ctrlb |=  ATC_DST_ADDR_MODE_FIXED
			| ATC_SRC_ADDR_MODE_INCR
			| ATC_FC_MEM2PER;
		reg = atslave->tx_reg;
		for_each_sg(sgl, sg, sg_len, i) {
			struct at_desc	*desc;
			u32		len;
			u32		mem;

			desc = atc_desc_get(atchan);
			if (!desc)
				goto err_desc_get;

			mem = sg_phys(sg);
			len = sg_dma_len(sg);
			mem_width = 2;
			if (unlikely(mem & 3 || len & 3))
				mem_width = 0;

			desc->lli.saddr = mem;
			desc->lli.daddr = reg;
			desc->lli.ctrla = ctrla
					| ATC_SRC_WIDTH(mem_width)
					| len >> mem_width;
			desc->lli.ctrlb = ctrlb;

			if (!first) {
				first = desc;
			} else {
				/* inform the HW lli about chaining */
				prev->lli.dscr = desc->txd.phys;
				/* insert the link descriptor to the LD ring */
				list_add_tail(&desc->desc_node,
						&first->tx_list);
			}
			prev = desc;
			total_len += len;
		}
		break;
	case DMA_FROM_DEVICE:
		ctrla |=  ATC_SRC_WIDTH(reg_width);
		ctrlb |=  ATC_DST_ADDR_MODE_INCR
			| ATC_SRC_ADDR_MODE_FIXED
			| ATC_FC_PER2MEM;

		reg = atslave->rx_reg;
		for_each_sg(sgl, sg, sg_len, i) {
			struct at_desc	*desc;
			u32		len;
			u32		mem;

			desc = atc_desc_get(atchan);
			if (!desc)
				goto err_desc_get;

			mem = sg_phys(sg);
			len = sg_dma_len(sg);
			mem_width = 2;
			if (unlikely(mem & 3 || len & 3))
				mem_width = 0;

			desc->lli.saddr = reg;
			desc->lli.daddr = mem;
			desc->lli.ctrla = ctrla
					| ATC_DST_WIDTH(mem_width)
					| len >> mem_width;
			desc->lli.ctrlb = ctrlb;

			if (!first) {
				first = desc;
			} else {
				/* inform the HW lli about chaining */
				prev->lli.dscr = desc->txd.phys;
				/* insert the link descriptor to the LD ring */
				list_add_tail(&desc->desc_node,
						&first->tx_list);
			}
			prev = desc;
			total_len += len;
		}
		break;
	default:
		return NULL;
	}

	/* set end-of-link to the last link descriptor of list*/
	set_desc_eol(prev);

	/* First descriptor of the chain embedds additional information */
	first->txd.cookie = -EBUSY;
	first->len = total_len;

	/* last link descriptor of list is responsible of flags */
	prev->txd.flags = flags; /* client is in control of this ack */

	return &first->txd;

err_desc_get:
	dev_err(chan2dev(chan), "not enough descriptors available\n");
	atc_desc_put(atchan, first);
	return NULL;
}

static void atc_terminate_all(struct dma_chan *chan)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct at_dma		*atdma = to_at_dma(chan->device);
	struct at_desc		*desc, *_desc;
	LIST_HEAD(list);

	/*
	 * This is only called when something went wrong elsewhere, so
	 * we don't really care about the data. Just disable the
	 * channel. We still have to poll the channel enable bit due
	 * to AHB/HSB limitations.
	 */
	spin_lock_bh(&atchan->lock);

	dma_writel(atdma, CHDR, atchan->mask);

	/* confirm that this channel is disabled */
	while (dma_readl(atdma, CHSR) & atchan->mask)
		cpu_relax();

	/* active_list entries will end up before queued entries */
	list_splice_init(&atchan->queue, &list);
	list_splice_init(&atchan->active_list, &list);

	spin_unlock_bh(&atchan->lock);

	/* Flush all pending and queued descriptors */
	list_for_each_entry_safe(desc, _desc, &list, desc_node)
		atc_chain_complete(atchan, desc);
}

/**
 * atc_is_tx_complete - poll for transaction completion
 * @chan: DMA channel
 * @cookie: transaction identifier to check status of
 * @done: if not %NULL, updated with last completed transaction
 * @used: if not %NULL, updated with last used transaction
 *
 * If @done and @used are passed in, upon return they reflect the driver
 * internal state and can be used with dma_async_is_complete() to check
 * the status of multiple cookies without re-checking hardware state.
 */
static enum dma_status
atc_is_tx_complete(struct dma_chan *chan,
		dma_cookie_t cookie,
		dma_cookie_t *done, dma_cookie_t *used)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	dma_cookie_t		last_used;
	dma_cookie_t		last_complete;
	enum dma_status		ret;

	dev_vdbg(chan2dev(chan), "is_tx_complete: %d (d%d, u%d)\n",
			cookie, done ? *done : 0, used ? *used : 0);

	spin_lock_bh(&atchan->lock);

	last_complete = atchan->completed_cookie;
	last_used = chan->cookie;

	ret = dma_async_is_complete(cookie, last_complete, last_used);
	if (ret != DMA_SUCCESS) {
		atc_cleanup_descriptors(atchan);

		last_complete = atchan->completed_cookie;
		last_used = chan->cookie;

		ret = dma_async_is_complete(cookie, last_complete, last_used);
	}

	spin_unlock_bh(&atchan->lock);

	if (done)
		*done = last_complete;
	if (used)
		*used = last_used;

	return ret;
}

/**
 * atc_issue_pending - try to finish work
 * @chan: target DMA channel
 */
static void atc_issue_pending(struct dma_chan *chan)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);

	dev_vdbg(chan2dev(chan), "issue_pending\n");

	if (!atc_chan_is_enabled(atchan)) {
		spin_lock_bh(&atchan->lock);
		atc_advance_work(atchan);
		spin_unlock_bh(&atchan->lock);
	}
}

/**
 * atc_alloc_chan_resources - allocate resources for DMA channel
 * @chan: allocate descriptor resources for this channel
 * @client: current client requesting the channel be ready for requests
 *
 * return - the number of allocated descriptors
 */
static int atc_alloc_chan_resources(struct dma_chan *chan)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct at_dma		*atdma = to_at_dma(chan->device);
	struct at_desc		*desc;
	struct at_dma_slave	*atslave;
	int			i;
	u32			cfg;
	LIST_HEAD(tmp_list);

	dev_vdbg(chan2dev(chan), "alloc_chan_resources\n");

	/* ASSERT:  channel is idle */
	if (atc_chan_is_enabled(atchan)) {
		dev_dbg(chan2dev(chan), "DMA channel not idle ?\n");
		return -EIO;
	}

	cfg = ATC_DEFAULT_CFG;

	atslave = chan->private;
	if (atslave) {
		/*
		 * We need controller-specific data to set up slave
		 * transfers.
		 */
		BUG_ON(!atslave->dma_dev || atslave->dma_dev != atdma->dma_common.dev);

		/* if cfg configuration specified take it instad of default */
		if (atslave->cfg)
			cfg = atslave->cfg;
	}

	/* have we already been set up?
	 * reconfigure channel but no need to reallocate descriptors */
	if (!list_empty(&atchan->free_list))
		return atchan->descs_allocated;

	/* Allocate initial pool of descriptors */
	for (i = 0; i < init_nr_desc_per_channel; i++) {
		desc = atc_alloc_descriptor(chan, GFP_KERNEL);
		if (!desc) {
			dev_err(atdma->dma_common.dev,
				"Only %d initial descriptors\n", i);
			break;
		}
		list_add_tail(&desc->desc_node, &tmp_list);
	}

	spin_lock_bh(&atchan->lock);
	atchan->descs_allocated = i;
	list_splice(&tmp_list, &atchan->free_list);
	atchan->completed_cookie = chan->cookie = 1;
	spin_unlock_bh(&atchan->lock);

	/* channel parameters */
	channel_writel(atchan, CFG, cfg);

	dev_dbg(chan2dev(chan),
		"alloc_chan_resources: allocated %d descriptors\n",
		atchan->descs_allocated);

	return atchan->descs_allocated;
}

/**
 * atc_free_chan_resources - free all channel resources
 * @chan: DMA channel
 */
static void atc_free_chan_resources(struct dma_chan *chan)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct at_dma		*atdma = to_at_dma(chan->device);
	struct at_desc		*desc, *_desc;
	LIST_HEAD(list);

	dev_dbg(chan2dev(chan), "free_chan_resources: (descs allocated=%u)\n",
		atchan->descs_allocated);

	/* ASSERT:  channel is idle */
	BUG_ON(!list_empty(&atchan->active_list));
	BUG_ON(!list_empty(&atchan->queue));
	BUG_ON(atc_chan_is_enabled(atchan));

	list_for_each_entry_safe(desc, _desc, &atchan->free_list, desc_node) {
		dev_vdbg(chan2dev(chan), "  freeing descriptor %p\n", desc);
		list_del(&desc->desc_node);
		/* free link descriptor */
		dma_pool_free(atdma->dma_desc_pool, desc, desc->txd.phys);
	}
	list_splice_init(&atchan->free_list, &list);
	atchan->descs_allocated = 0;

	dev_vdbg(chan2dev(chan), "free_chan_resources: done\n");
}


/*--  Module Management  -----------------------------------------------*/

/**
 * at_dma_off - disable DMA controller
 * @atdma: the Atmel HDAMC device
 */
static void at_dma_off(struct at_dma *atdma)
{
	dma_writel(atdma, EN, 0);

	/* disable all interrupts */
	dma_writel(atdma, EBCIDR, -1L);

	/* confirm that all channels are disabled */
	while (dma_readl(atdma, CHSR) & atdma->all_chan_mask)
		cpu_relax();
}

static int __init at_dma_probe(struct platform_device *pdev)
{
	struct at_dma_platform_data *pdata;
	struct resource		*io;
	struct at_dma		*atdma;
	size_t			size;
	int			irq;
	int			err;
	int			i;

	/* get DMA Controller parameters from platform */
	pdata = pdev->dev.platform_data;
	if (!pdata || pdata->nr_channels > AT_DMA_MAX_NR_CHANNELS)
		return -EINVAL;

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!io)
		return -EINVAL;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	size = sizeof(struct at_dma);
	size += pdata->nr_channels * sizeof(struct at_dma_chan);
	atdma = kzalloc(size, GFP_KERNEL);
	if (!atdma)
		return -ENOMEM;

	/* discover transaction capabilites from the platform data */
	atdma->dma_common.cap_mask = pdata->cap_mask;
	atdma->all_chan_mask = (1 << pdata->nr_channels) - 1;

	size = io->end - io->start + 1;
	if (!request_mem_region(io->start, size, pdev->dev.driver->name)) {
		err = -EBUSY;
		goto err_kfree;
	}

	atdma->regs = ioremap(io->start, size);
	if (!atdma->regs) {
		err = -ENOMEM;
		goto err_release_r;
	}

	atdma->clk = clk_get(&pdev->dev, "dma_clk");
	if (IS_ERR(atdma->clk)) {
		err = PTR_ERR(atdma->clk);
		goto err_clk;
	}
	clk_enable(atdma->clk);

	/* force dma off, just in case */
	at_dma_off(atdma);

	err = request_irq(irq, at_dma_interrupt, 0, "at_hdmac", atdma);
	if (err)
		goto err_irq;

	platform_set_drvdata(pdev, atdma);

	/* create a pool of consistent memory blocks for hardware descriptors */
	atdma->dma_desc_pool = dma_pool_create("at_hdmac_desc_pool",
			&pdev->dev, sizeof(struct at_desc),
			4 /* word alignment */, 0);
	if (!atdma->dma_desc_pool) {
		dev_err(&pdev->dev, "No memory for descriptors dma pool\n");
		err = -ENOMEM;
		goto err_pool_create;
	}

	/* clear any pending interrupt */
	while (dma_readl(atdma, EBCISR))
		cpu_relax();

	/* initialize channels related values */
	INIT_LIST_HEAD(&atdma->dma_common.channels);
	for (i = 0; i < pdata->nr_channels; i++, atdma->dma_common.chancnt++) {
		struct at_dma_chan	*atchan = &atdma->chan[i];

		atchan->chan_common.device = &atdma->dma_common;
		atchan->chan_common.cookie = atchan->completed_cookie = 1;
		atchan->chan_common.chan_id = i;
		list_add_tail(&atchan->chan_common.device_node,
				&atdma->dma_common.channels);

		atchan->ch_regs = atdma->regs + ch_regs(i);
		spin_lock_init(&atchan->lock);
		atchan->mask = 1 << i;

		INIT_LIST_HEAD(&atchan->active_list);
		INIT_LIST_HEAD(&atchan->queue);
		INIT_LIST_HEAD(&atchan->free_list);

		tasklet_init(&atchan->tasklet, atc_tasklet,
				(unsigned long)atchan);
		atc_enable_irq(atchan);
	}

	/* set base routines */
	atdma->dma_common.device_alloc_chan_resources = atc_alloc_chan_resources;
	atdma->dma_common.device_free_chan_resources = atc_free_chan_resources;
	atdma->dma_common.device_is_tx_complete = atc_is_tx_complete;
	atdma->dma_common.device_issue_pending = atc_issue_pending;
	atdma->dma_common.dev = &pdev->dev;

	/* set prep routines based on capability */
	if (dma_has_cap(DMA_MEMCPY, atdma->dma_common.cap_mask))
		atdma->dma_common.device_prep_dma_memcpy = atc_prep_dma_memcpy;

	if (dma_has_cap(DMA_SLAVE, atdma->dma_common.cap_mask)) {
		atdma->dma_common.device_prep_slave_sg = atc_prep_slave_sg;
		atdma->dma_common.device_terminate_all = atc_terminate_all;
	}

	dma_writel(atdma, EN, AT_DMA_ENABLE);

	dev_info(&pdev->dev, "Atmel AHB DMA Controller ( %s%s), %d channels\n",
	  dma_has_cap(DMA_MEMCPY, atdma->dma_common.cap_mask) ? "cpy " : "",
	  dma_has_cap(DMA_SLAVE, atdma->dma_common.cap_mask)  ? "slave " : "",
	  atdma->dma_common.chancnt);

	dma_async_device_register(&atdma->dma_common);

	return 0;

err_pool_create:
	platform_set_drvdata(pdev, NULL);
	free_irq(platform_get_irq(pdev, 0), atdma);
err_irq:
	clk_disable(atdma->clk);
	clk_put(atdma->clk);
err_clk:
	iounmap(atdma->regs);
	atdma->regs = NULL;
err_release_r:
	release_mem_region(io->start, size);
err_kfree:
	kfree(atdma);
	return err;
}

static int __exit at_dma_remove(struct platform_device *pdev)
{
	struct at_dma		*atdma = platform_get_drvdata(pdev);
	struct dma_chan		*chan, *_chan;
	struct resource		*io;

	at_dma_off(atdma);
	dma_async_device_unregister(&atdma->dma_common);

	dma_pool_destroy(atdma->dma_desc_pool);
	platform_set_drvdata(pdev, NULL);
	free_irq(platform_get_irq(pdev, 0), atdma);

	list_for_each_entry_safe(chan, _chan, &atdma->dma_common.channels,
			device_node) {
		struct at_dma_chan	*atchan = to_at_dma_chan(chan);

		/* Disable interrupts */
		atc_disable_irq(atchan);
		tasklet_disable(&atchan->tasklet);

		tasklet_kill(&atchan->tasklet);
		list_del(&chan->device_node);
	}

	clk_disable(atdma->clk);
	clk_put(atdma->clk);

	iounmap(atdma->regs);
	atdma->regs = NULL;

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(io->start, io->end - io->start + 1);

	kfree(atdma);

	return 0;
}

static void at_dma_shutdown(struct platform_device *pdev)
{
	struct at_dma	*atdma = platform_get_drvdata(pdev);

	at_dma_off(platform_get_drvdata(pdev));
	clk_disable(atdma->clk);
}

static int at_dma_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct at_dma *atdma = platform_get_drvdata(pdev);

	at_dma_off(platform_get_drvdata(pdev));
	clk_disable(atdma->clk);
	return 0;
}

static int at_dma_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct at_dma *atdma = platform_get_drvdata(pdev);

	clk_enable(atdma->clk);
	dma_writel(atdma, EN, AT_DMA_ENABLE);
	return 0;
}

static const struct dev_pm_ops at_dma_dev_pm_ops = {
	.suspend_noirq = at_dma_suspend_noirq,
	.resume_noirq = at_dma_resume_noirq,
};

static struct platform_driver at_dma_driver = {
	.remove		= __exit_p(at_dma_remove),
	.shutdown	= at_dma_shutdown,
	.driver = {
		.name	= "at_hdmac",
		.pm	= &at_dma_dev_pm_ops,
	},
};

static int __init at_dma_init(void)
{
	return platform_driver_probe(&at_dma_driver, at_dma_probe);
}
module_init(at_dma_init);

static void __exit at_dma_exit(void)
{
	platform_driver_unregister(&at_dma_driver);
}
module_exit(at_dma_exit);

MODULE_DESCRIPTION("Atmel AHB DMA Controller driver");
MODULE_AUTHOR("Nicolas Ferre <nicolas.ferre@atmel.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:at_hdmac");
