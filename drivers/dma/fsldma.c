/*
 * Freescale MPC85xx, MPC83xx DMA Engine support
 *
 * Copyright (C) 2007 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author:
 *   Zhang Wei <wei.zhang@freescale.com>, Jul 2007
 *   Ebony Zhu <ebony.zhu@freescale.com>, May 2007
 *
 * Description:
 *   DMA engine driver for Freescale MPC8540 DMA controller, which is
 *   also fit for MPC8560, MPC8555, MPC8548, MPC8641, and etc.
 *   The support for MPC8349 DMA contorller is also added.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/of_platform.h>

#include "fsldma.h"

static void dma_init(struct fsl_dma_chan *fsl_chan)
{
	/* Reset the channel */
	DMA_OUT(fsl_chan, &fsl_chan->reg_base->mr, 0, 32);

	switch (fsl_chan->feature & FSL_DMA_IP_MASK) {
	case FSL_DMA_IP_85XX:
		/* Set the channel to below modes:
		 * EIE - Error interrupt enable
		 * EOSIE - End of segments interrupt enable (basic mode)
		 * EOLNIE - End of links interrupt enable
		 */
		DMA_OUT(fsl_chan, &fsl_chan->reg_base->mr, FSL_DMA_MR_EIE
				| FSL_DMA_MR_EOLNIE | FSL_DMA_MR_EOSIE, 32);
		break;
	case FSL_DMA_IP_83XX:
		/* Set the channel to below modes:
		 * EOTIE - End-of-transfer interrupt enable
		 */
		DMA_OUT(fsl_chan, &fsl_chan->reg_base->mr, FSL_DMA_MR_EOTIE,
				32);
		break;
	}

}

static void set_sr(struct fsl_dma_chan *fsl_chan, u32 val)
{
	DMA_OUT(fsl_chan, &fsl_chan->reg_base->sr, val, 32);
}

static u32 get_sr(struct fsl_dma_chan *fsl_chan)
{
	return DMA_IN(fsl_chan, &fsl_chan->reg_base->sr, 32);
}

static void set_desc_cnt(struct fsl_dma_chan *fsl_chan,
				struct fsl_dma_ld_hw *hw, u32 count)
{
	hw->count = CPU_TO_DMA(fsl_chan, count, 32);
}

static void set_desc_src(struct fsl_dma_chan *fsl_chan,
				struct fsl_dma_ld_hw *hw, dma_addr_t src)
{
	u64 snoop_bits;

	snoop_bits = ((fsl_chan->feature & FSL_DMA_IP_MASK) == FSL_DMA_IP_85XX)
		? ((u64)FSL_DMA_SATR_SREADTYPE_SNOOP_READ << 32) : 0;
	hw->src_addr = CPU_TO_DMA(fsl_chan, snoop_bits | src, 64);
}

static void set_desc_dest(struct fsl_dma_chan *fsl_chan,
				struct fsl_dma_ld_hw *hw, dma_addr_t dest)
{
	u64 snoop_bits;

	snoop_bits = ((fsl_chan->feature & FSL_DMA_IP_MASK) == FSL_DMA_IP_85XX)
		? ((u64)FSL_DMA_DATR_DWRITETYPE_SNOOP_WRITE << 32) : 0;
	hw->dst_addr = CPU_TO_DMA(fsl_chan, snoop_bits | dest, 64);
}

static void set_desc_next(struct fsl_dma_chan *fsl_chan,
				struct fsl_dma_ld_hw *hw, dma_addr_t next)
{
	u64 snoop_bits;

	snoop_bits = ((fsl_chan->feature & FSL_DMA_IP_MASK) == FSL_DMA_IP_83XX)
		? FSL_DMA_SNEN : 0;
	hw->next_ln_addr = CPU_TO_DMA(fsl_chan, snoop_bits | next, 64);
}

static void set_cdar(struct fsl_dma_chan *fsl_chan, dma_addr_t addr)
{
	DMA_OUT(fsl_chan, &fsl_chan->reg_base->cdar, addr | FSL_DMA_SNEN, 64);
}

static dma_addr_t get_cdar(struct fsl_dma_chan *fsl_chan)
{
	return DMA_IN(fsl_chan, &fsl_chan->reg_base->cdar, 64) & ~FSL_DMA_SNEN;
}

static void set_ndar(struct fsl_dma_chan *fsl_chan, dma_addr_t addr)
{
	DMA_OUT(fsl_chan, &fsl_chan->reg_base->ndar, addr, 64);
}

static dma_addr_t get_ndar(struct fsl_dma_chan *fsl_chan)
{
	return DMA_IN(fsl_chan, &fsl_chan->reg_base->ndar, 64);
}

static int dma_is_idle(struct fsl_dma_chan *fsl_chan)
{
	u32 sr = get_sr(fsl_chan);
	return (!(sr & FSL_DMA_SR_CB)) || (sr & FSL_DMA_SR_CH);
}

static void dma_start(struct fsl_dma_chan *fsl_chan)
{
	u32 mr_set = 0;;

	if (fsl_chan->feature & FSL_DMA_CHAN_PAUSE_EXT) {
		DMA_OUT(fsl_chan, &fsl_chan->reg_base->bcr, 0, 32);
		mr_set |= FSL_DMA_MR_EMP_EN;
	} else
		DMA_OUT(fsl_chan, &fsl_chan->reg_base->mr,
			DMA_IN(fsl_chan, &fsl_chan->reg_base->mr, 32)
				& ~FSL_DMA_MR_EMP_EN, 32);

	if (fsl_chan->feature & FSL_DMA_CHAN_START_EXT)
		mr_set |= FSL_DMA_MR_EMS_EN;
	else
		mr_set |= FSL_DMA_MR_CS;

	DMA_OUT(fsl_chan, &fsl_chan->reg_base->mr,
			DMA_IN(fsl_chan, &fsl_chan->reg_base->mr, 32)
			| mr_set, 32);
}

static void dma_halt(struct fsl_dma_chan *fsl_chan)
{
	int i = 0;
	DMA_OUT(fsl_chan, &fsl_chan->reg_base->mr,
		DMA_IN(fsl_chan, &fsl_chan->reg_base->mr, 32) | FSL_DMA_MR_CA,
		32);
	DMA_OUT(fsl_chan, &fsl_chan->reg_base->mr,
		DMA_IN(fsl_chan, &fsl_chan->reg_base->mr, 32) & ~(FSL_DMA_MR_CS
		| FSL_DMA_MR_EMS_EN | FSL_DMA_MR_CA), 32);

	while (!dma_is_idle(fsl_chan) && (i++ < 100))
		udelay(10);
	if (i >= 100 && !dma_is_idle(fsl_chan))
		dev_err(fsl_chan->dev, "DMA halt timeout!\n");
}

static void set_ld_eol(struct fsl_dma_chan *fsl_chan,
			struct fsl_desc_sw *desc)
{
	desc->hw.next_ln_addr = CPU_TO_DMA(fsl_chan,
		DMA_TO_CPU(fsl_chan, desc->hw.next_ln_addr, 64)	| FSL_DMA_EOL,
		64);
}

static void append_ld_queue(struct fsl_dma_chan *fsl_chan,
		struct fsl_desc_sw *new_desc)
{
	struct fsl_desc_sw *queue_tail = to_fsl_desc(fsl_chan->ld_queue.prev);

	if (list_empty(&fsl_chan->ld_queue))
		return;

	/* Link to the new descriptor physical address and
	 * Enable End-of-segment interrupt for
	 * the last link descriptor.
	 * (the previous node's next link descriptor)
	 *
	 * For FSL_DMA_IP_83xx, the snoop enable bit need be set.
	 */
	queue_tail->hw.next_ln_addr = CPU_TO_DMA(fsl_chan,
			new_desc->async_tx.phys | FSL_DMA_EOSIE |
			(((fsl_chan->feature & FSL_DMA_IP_MASK)
				== FSL_DMA_IP_83XX) ? FSL_DMA_SNEN : 0), 64);
}

/**
 * fsl_chan_set_src_loop_size - Set source address hold transfer size
 * @fsl_chan : Freescale DMA channel
 * @size     : Address loop size, 0 for disable loop
 *
 * The set source address hold transfer size. The source
 * address hold or loop transfer size is when the DMA transfer
 * data from source address (SA), if the loop size is 4, the DMA will
 * read data from SA, SA + 1, SA + 2, SA + 3, then loop back to SA,
 * SA + 1 ... and so on.
 */
static void fsl_chan_set_src_loop_size(struct fsl_dma_chan *fsl_chan, int size)
{
	switch (size) {
	case 0:
		DMA_OUT(fsl_chan, &fsl_chan->reg_base->mr,
			DMA_IN(fsl_chan, &fsl_chan->reg_base->mr, 32) &
			(~FSL_DMA_MR_SAHE), 32);
		break;
	case 1:
	case 2:
	case 4:
	case 8:
		DMA_OUT(fsl_chan, &fsl_chan->reg_base->mr,
			DMA_IN(fsl_chan, &fsl_chan->reg_base->mr, 32) |
			FSL_DMA_MR_SAHE | (__ilog2(size) << 14),
			32);
		break;
	}
}

/**
 * fsl_chan_set_dest_loop_size - Set destination address hold transfer size
 * @fsl_chan : Freescale DMA channel
 * @size     : Address loop size, 0 for disable loop
 *
 * The set destination address hold transfer size. The destination
 * address hold or loop transfer size is when the DMA transfer
 * data to destination address (TA), if the loop size is 4, the DMA will
 * write data to TA, TA + 1, TA + 2, TA + 3, then loop back to TA,
 * TA + 1 ... and so on.
 */
static void fsl_chan_set_dest_loop_size(struct fsl_dma_chan *fsl_chan, int size)
{
	switch (size) {
	case 0:
		DMA_OUT(fsl_chan, &fsl_chan->reg_base->mr,
			DMA_IN(fsl_chan, &fsl_chan->reg_base->mr, 32) &
			(~FSL_DMA_MR_DAHE), 32);
		break;
	case 1:
	case 2:
	case 4:
	case 8:
		DMA_OUT(fsl_chan, &fsl_chan->reg_base->mr,
			DMA_IN(fsl_chan, &fsl_chan->reg_base->mr, 32) |
			FSL_DMA_MR_DAHE | (__ilog2(size) << 16),
			32);
		break;
	}
}

/**
 * fsl_chan_toggle_ext_pause - Toggle channel external pause status
 * @fsl_chan : Freescale DMA channel
 * @size     : Pause control size, 0 for disable external pause control.
 *             The maximum is 1024.
 *
 * The Freescale DMA channel can be controlled by the external
 * signal DREQ#. The pause control size is how many bytes are allowed
 * to transfer before pausing the channel, after which a new assertion
 * of DREQ# resumes channel operation.
 */
static void fsl_chan_toggle_ext_pause(struct fsl_dma_chan *fsl_chan, int size)
{
	if (size > 1024)
		return;

	if (size) {
		DMA_OUT(fsl_chan, &fsl_chan->reg_base->mr,
			DMA_IN(fsl_chan, &fsl_chan->reg_base->mr, 32)
				| ((__ilog2(size) << 24) & 0x0f000000),
			32);
		fsl_chan->feature |= FSL_DMA_CHAN_PAUSE_EXT;
	} else
		fsl_chan->feature &= ~FSL_DMA_CHAN_PAUSE_EXT;
}

/**
 * fsl_chan_toggle_ext_start - Toggle channel external start status
 * @fsl_chan : Freescale DMA channel
 * @enable   : 0 is disabled, 1 is enabled.
 *
 * If enable the external start, the channel can be started by an
 * external DMA start pin. So the dma_start() does not start the
 * transfer immediately. The DMA channel will wait for the
 * control pin asserted.
 */
static void fsl_chan_toggle_ext_start(struct fsl_dma_chan *fsl_chan, int enable)
{
	if (enable)
		fsl_chan->feature |= FSL_DMA_CHAN_START_EXT;
	else
		fsl_chan->feature &= ~FSL_DMA_CHAN_START_EXT;
}

static dma_cookie_t fsl_dma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct fsl_desc_sw *desc = tx_to_fsl_desc(tx);
	struct fsl_dma_chan *fsl_chan = to_fsl_chan(tx->chan);
	unsigned long flags;
	dma_cookie_t cookie;

	/* cookie increment and adding to ld_queue must be atomic */
	spin_lock_irqsave(&fsl_chan->desc_lock, flags);

	cookie = fsl_chan->common.cookie;
	cookie++;
	if (cookie < 0)
		cookie = 1;
	desc->async_tx.cookie = cookie;
	fsl_chan->common.cookie = desc->async_tx.cookie;

	append_ld_queue(fsl_chan, desc);
	list_splice_init(&desc->async_tx.tx_list, fsl_chan->ld_queue.prev);

	spin_unlock_irqrestore(&fsl_chan->desc_lock, flags);

	return cookie;
}

/**
 * fsl_dma_alloc_descriptor - Allocate descriptor from channel's DMA pool.
 * @fsl_chan : Freescale DMA channel
 *
 * Return - The descriptor allocated. NULL for failed.
 */
static struct fsl_desc_sw *fsl_dma_alloc_descriptor(
					struct fsl_dma_chan *fsl_chan)
{
	dma_addr_t pdesc;
	struct fsl_desc_sw *desc_sw;

	desc_sw = dma_pool_alloc(fsl_chan->desc_pool, GFP_ATOMIC, &pdesc);
	if (desc_sw) {
		memset(desc_sw, 0, sizeof(struct fsl_desc_sw));
		dma_async_tx_descriptor_init(&desc_sw->async_tx,
						&fsl_chan->common);
		desc_sw->async_tx.tx_submit = fsl_dma_tx_submit;
		INIT_LIST_HEAD(&desc_sw->async_tx.tx_list);
		desc_sw->async_tx.phys = pdesc;
	}

	return desc_sw;
}


/**
 * fsl_dma_alloc_chan_resources - Allocate resources for DMA channel.
 * @fsl_chan : Freescale DMA channel
 *
 * This function will create a dma pool for descriptor allocation.
 *
 * Return - The number of descriptors allocated.
 */
static int fsl_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct fsl_dma_chan *fsl_chan = to_fsl_chan(chan);
	LIST_HEAD(tmp_list);

	/* We need the descriptor to be aligned to 32bytes
	 * for meeting FSL DMA specification requirement.
	 */
	fsl_chan->desc_pool = dma_pool_create("fsl_dma_engine_desc_pool",
			fsl_chan->dev, sizeof(struct fsl_desc_sw),
			32, 0);
	if (!fsl_chan->desc_pool) {
		dev_err(fsl_chan->dev, "No memory for channel %d "
			"descriptor dma pool.\n", fsl_chan->id);
		return 0;
	}

	return 1;
}

/**
 * fsl_dma_free_chan_resources - Free all resources of the channel.
 * @fsl_chan : Freescale DMA channel
 */
static void fsl_dma_free_chan_resources(struct dma_chan *chan)
{
	struct fsl_dma_chan *fsl_chan = to_fsl_chan(chan);
	struct fsl_desc_sw *desc, *_desc;
	unsigned long flags;

	dev_dbg(fsl_chan->dev, "Free all channel resources.\n");
	spin_lock_irqsave(&fsl_chan->desc_lock, flags);
	list_for_each_entry_safe(desc, _desc, &fsl_chan->ld_queue, node) {
#ifdef FSL_DMA_LD_DEBUG
		dev_dbg(fsl_chan->dev,
				"LD %p will be released.\n", desc);
#endif
		list_del(&desc->node);
		/* free link descriptor */
		dma_pool_free(fsl_chan->desc_pool, desc, desc->async_tx.phys);
	}
	spin_unlock_irqrestore(&fsl_chan->desc_lock, flags);
	dma_pool_destroy(fsl_chan->desc_pool);
}

static struct dma_async_tx_descriptor *
fsl_dma_prep_interrupt(struct dma_chan *chan)
{
	struct fsl_dma_chan *fsl_chan;
	struct fsl_desc_sw *new;

	if (!chan)
		return NULL;

	fsl_chan = to_fsl_chan(chan);

	new = fsl_dma_alloc_descriptor(fsl_chan);
	if (!new) {
		dev_err(fsl_chan->dev, "No free memory for link descriptor\n");
		return NULL;
	}

	new->async_tx.cookie = -EBUSY;
	new->async_tx.ack = 0;

	/* Set End-of-link to the last link descriptor of new list*/
	set_ld_eol(fsl_chan, new);

	return &new->async_tx;
}

static struct dma_async_tx_descriptor *fsl_dma_prep_memcpy(
	struct dma_chan *chan, dma_addr_t dma_dest, dma_addr_t dma_src,
	size_t len, unsigned long flags)
{
	struct fsl_dma_chan *fsl_chan;
	struct fsl_desc_sw *first = NULL, *prev = NULL, *new;
	size_t copy;
	LIST_HEAD(link_chain);

	if (!chan)
		return NULL;

	if (!len)
		return NULL;

	fsl_chan = to_fsl_chan(chan);

	do {

		/* Allocate the link descriptor from DMA pool */
		new = fsl_dma_alloc_descriptor(fsl_chan);
		if (!new) {
			dev_err(fsl_chan->dev,
					"No free memory for link descriptor\n");
			return NULL;
		}
#ifdef FSL_DMA_LD_DEBUG
		dev_dbg(fsl_chan->dev, "new link desc alloc %p\n", new);
#endif

		copy = min(len, (size_t)FSL_DMA_BCR_MAX_CNT);

		set_desc_cnt(fsl_chan, &new->hw, copy);
		set_desc_src(fsl_chan, &new->hw, dma_src);
		set_desc_dest(fsl_chan, &new->hw, dma_dest);

		if (!first)
			first = new;
		else
			set_desc_next(fsl_chan, &prev->hw, new->async_tx.phys);

		new->async_tx.cookie = 0;
		new->async_tx.ack = 1;

		prev = new;
		len -= copy;
		dma_src += copy;
		dma_dest += copy;

		/* Insert the link descriptor to the LD ring */
		list_add_tail(&new->node, &first->async_tx.tx_list);
	} while (len);

	new->async_tx.ack = 0; /* client is in control of this ack */
	new->async_tx.cookie = -EBUSY;

	/* Set End-of-link to the last link descriptor of new list*/
	set_ld_eol(fsl_chan, new);

	return first ? &first->async_tx : NULL;
}

/**
 * fsl_dma_update_completed_cookie - Update the completed cookie.
 * @fsl_chan : Freescale DMA channel
 */
static void fsl_dma_update_completed_cookie(struct fsl_dma_chan *fsl_chan)
{
	struct fsl_desc_sw *cur_desc, *desc;
	dma_addr_t ld_phy;

	ld_phy = get_cdar(fsl_chan) & FSL_DMA_NLDA_MASK;

	if (ld_phy) {
		cur_desc = NULL;
		list_for_each_entry(desc, &fsl_chan->ld_queue, node)
			if (desc->async_tx.phys == ld_phy) {
				cur_desc = desc;
				break;
			}

		if (cur_desc && cur_desc->async_tx.cookie) {
			if (dma_is_idle(fsl_chan))
				fsl_chan->completed_cookie =
					cur_desc->async_tx.cookie;
			else
				fsl_chan->completed_cookie =
					cur_desc->async_tx.cookie - 1;
		}
	}
}

/**
 * fsl_chan_ld_cleanup - Clean up link descriptors
 * @fsl_chan : Freescale DMA channel
 *
 * This function clean up the ld_queue of DMA channel.
 * If 'in_intr' is set, the function will move the link descriptor to
 * the recycle list. Otherwise, free it directly.
 */
static void fsl_chan_ld_cleanup(struct fsl_dma_chan *fsl_chan)
{
	struct fsl_desc_sw *desc, *_desc;
	unsigned long flags;

	spin_lock_irqsave(&fsl_chan->desc_lock, flags);

	dev_dbg(fsl_chan->dev, "chan completed_cookie = %d\n",
			fsl_chan->completed_cookie);
	list_for_each_entry_safe(desc, _desc, &fsl_chan->ld_queue, node) {
		dma_async_tx_callback callback;
		void *callback_param;

		if (dma_async_is_complete(desc->async_tx.cookie,
			    fsl_chan->completed_cookie, fsl_chan->common.cookie)
				== DMA_IN_PROGRESS)
			break;

		callback = desc->async_tx.callback;
		callback_param = desc->async_tx.callback_param;

		/* Remove from ld_queue list */
		list_del(&desc->node);

		dev_dbg(fsl_chan->dev, "link descriptor %p will be recycle.\n",
				desc);
		dma_pool_free(fsl_chan->desc_pool, desc, desc->async_tx.phys);

		/* Run the link descriptor callback function */
		if (callback) {
			spin_unlock_irqrestore(&fsl_chan->desc_lock, flags);
			dev_dbg(fsl_chan->dev, "link descriptor %p callback\n",
					desc);
			callback(callback_param);
			spin_lock_irqsave(&fsl_chan->desc_lock, flags);
		}
	}
	spin_unlock_irqrestore(&fsl_chan->desc_lock, flags);
}

/**
 * fsl_chan_xfer_ld_queue - Transfer link descriptors in channel ld_queue.
 * @fsl_chan : Freescale DMA channel
 */
static void fsl_chan_xfer_ld_queue(struct fsl_dma_chan *fsl_chan)
{
	struct list_head *ld_node;
	dma_addr_t next_dest_addr;
	unsigned long flags;

	if (!dma_is_idle(fsl_chan))
		return;

	dma_halt(fsl_chan);

	/* If there are some link descriptors
	 * not transfered in queue. We need to start it.
	 */
	spin_lock_irqsave(&fsl_chan->desc_lock, flags);

	/* Find the first un-transfer desciptor */
	for (ld_node = fsl_chan->ld_queue.next;
		(ld_node != &fsl_chan->ld_queue)
			&& (dma_async_is_complete(
				to_fsl_desc(ld_node)->async_tx.cookie,
				fsl_chan->completed_cookie,
				fsl_chan->common.cookie) == DMA_SUCCESS);
		ld_node = ld_node->next);

	spin_unlock_irqrestore(&fsl_chan->desc_lock, flags);

	if (ld_node != &fsl_chan->ld_queue) {
		/* Get the ld start address from ld_queue */
		next_dest_addr = to_fsl_desc(ld_node)->async_tx.phys;
		dev_dbg(fsl_chan->dev, "xfer LDs staring from %p\n",
				(void *)next_dest_addr);
		set_cdar(fsl_chan, next_dest_addr);
		dma_start(fsl_chan);
	} else {
		set_cdar(fsl_chan, 0);
		set_ndar(fsl_chan, 0);
	}
}

/**
 * fsl_dma_memcpy_issue_pending - Issue the DMA start command
 * @fsl_chan : Freescale DMA channel
 */
static void fsl_dma_memcpy_issue_pending(struct dma_chan *chan)
{
	struct fsl_dma_chan *fsl_chan = to_fsl_chan(chan);

#ifdef FSL_DMA_LD_DEBUG
	struct fsl_desc_sw *ld;
	unsigned long flags;

	spin_lock_irqsave(&fsl_chan->desc_lock, flags);
	if (list_empty(&fsl_chan->ld_queue)) {
		spin_unlock_irqrestore(&fsl_chan->desc_lock, flags);
		return;
	}

	dev_dbg(fsl_chan->dev, "--memcpy issue--\n");
	list_for_each_entry(ld, &fsl_chan->ld_queue, node) {
		int i;
		dev_dbg(fsl_chan->dev, "Ch %d, LD %08x\n",
				fsl_chan->id, ld->async_tx.phys);
		for (i = 0; i < 8; i++)
			dev_dbg(fsl_chan->dev, "LD offset %d: %08x\n",
					i, *(((u32 *)&ld->hw) + i));
	}
	dev_dbg(fsl_chan->dev, "----------------\n");
	spin_unlock_irqrestore(&fsl_chan->desc_lock, flags);
#endif

	fsl_chan_xfer_ld_queue(fsl_chan);
}

static void fsl_dma_dependency_added(struct dma_chan *chan)
{
	struct fsl_dma_chan *fsl_chan = to_fsl_chan(chan);

	fsl_chan_ld_cleanup(fsl_chan);
}

/**
 * fsl_dma_is_complete - Determine the DMA status
 * @fsl_chan : Freescale DMA channel
 */
static enum dma_status fsl_dma_is_complete(struct dma_chan *chan,
					dma_cookie_t cookie,
					dma_cookie_t *done,
					dma_cookie_t *used)
{
	struct fsl_dma_chan *fsl_chan = to_fsl_chan(chan);
	dma_cookie_t last_used;
	dma_cookie_t last_complete;

	fsl_chan_ld_cleanup(fsl_chan);

	last_used = chan->cookie;
	last_complete = fsl_chan->completed_cookie;

	if (done)
		*done = last_complete;

	if (used)
		*used = last_used;

	return dma_async_is_complete(cookie, last_complete, last_used);
}

static irqreturn_t fsl_dma_chan_do_interrupt(int irq, void *data)
{
	struct fsl_dma_chan *fsl_chan = (struct fsl_dma_chan *)data;
	u32 stat;

	stat = get_sr(fsl_chan);
	dev_dbg(fsl_chan->dev, "event: channel %d, stat = 0x%x\n",
						fsl_chan->id, stat);
	set_sr(fsl_chan, stat);		/* Clear the event register */

	stat &= ~(FSL_DMA_SR_CB | FSL_DMA_SR_CH);
	if (!stat)
		return IRQ_NONE;

	if (stat & FSL_DMA_SR_TE)
		dev_err(fsl_chan->dev, "Transfer Error!\n");

	/* If the link descriptor segment transfer finishes,
	 * we will recycle the used descriptor.
	 */
	if (stat & FSL_DMA_SR_EOSI) {
		dev_dbg(fsl_chan->dev, "event: End-of-segments INT\n");
		dev_dbg(fsl_chan->dev, "event: clndar %p, nlndar %p\n",
			(void *)get_cdar(fsl_chan), (void *)get_ndar(fsl_chan));
		stat &= ~FSL_DMA_SR_EOSI;
		fsl_dma_update_completed_cookie(fsl_chan);
	}

	/* If it current transfer is the end-of-transfer,
	 * we should clear the Channel Start bit for
	 * prepare next transfer.
	 */
	if (stat & (FSL_DMA_SR_EOLNI | FSL_DMA_SR_EOCDI)) {
		dev_dbg(fsl_chan->dev, "event: End-of-link INT\n");
		stat &= ~FSL_DMA_SR_EOLNI;
		fsl_chan_xfer_ld_queue(fsl_chan);
	}

	if (stat)
		dev_dbg(fsl_chan->dev, "event: unhandled sr 0x%02x\n",
					stat);

	dev_dbg(fsl_chan->dev, "event: Exit\n");
	tasklet_schedule(&fsl_chan->tasklet);
	return IRQ_HANDLED;
}

static irqreturn_t fsl_dma_do_interrupt(int irq, void *data)
{
	struct fsl_dma_device *fdev = (struct fsl_dma_device *)data;
	u32 gsr;
	int ch_nr;

	gsr = (fdev->feature & FSL_DMA_BIG_ENDIAN) ? in_be32(fdev->reg_base)
			: in_le32(fdev->reg_base);
	ch_nr = (32 - ffs(gsr)) / 8;

	return fdev->chan[ch_nr] ? fsl_dma_chan_do_interrupt(irq,
			fdev->chan[ch_nr]) : IRQ_NONE;
}

static void dma_do_tasklet(unsigned long data)
{
	struct fsl_dma_chan *fsl_chan = (struct fsl_dma_chan *)data;
	fsl_chan_ld_cleanup(fsl_chan);
}

#ifdef FSL_DMA_CALLBACKTEST
static void fsl_dma_callback_test(struct fsl_dma_chan *fsl_chan)
{
	if (fsl_chan)
		dev_info(fsl_chan->dev, "selftest: callback is ok!\n");
}
#endif

#ifdef CONFIG_FSL_DMA_SELFTEST
static int fsl_dma_self_test(struct fsl_dma_chan *fsl_chan)
{
	struct dma_chan *chan;
	int err = 0;
	dma_addr_t dma_dest, dma_src;
	dma_cookie_t cookie;
	u8 *src, *dest;
	int i;
	size_t test_size;
	struct dma_async_tx_descriptor *tx1, *tx2, *tx3;

	test_size = 4096;

	src = kmalloc(test_size * 2, GFP_KERNEL);
	if (!src) {
		dev_err(fsl_chan->dev,
				"selftest: Cannot alloc memory for test!\n");
		err = -ENOMEM;
		goto out;
	}

	dest = src + test_size;

	for (i = 0; i < test_size; i++)
		src[i] = (u8) i;

	chan = &fsl_chan->common;

	if (fsl_dma_alloc_chan_resources(chan) < 1) {
		dev_err(fsl_chan->dev,
				"selftest: Cannot alloc resources for DMA\n");
		err = -ENODEV;
		goto out;
	}

	/* TX 1 */
	dma_src = dma_map_single(fsl_chan->dev, src, test_size / 2,
				 DMA_TO_DEVICE);
	dma_dest = dma_map_single(fsl_chan->dev, dest, test_size / 2,
				  DMA_FROM_DEVICE);
	tx1 = fsl_dma_prep_memcpy(chan, dma_dest, dma_src, test_size / 2, 0);
	async_tx_ack(tx1);

	cookie = fsl_dma_tx_submit(tx1);
	fsl_dma_memcpy_issue_pending(chan);
	msleep(2);

	if (fsl_dma_is_complete(chan, cookie, NULL, NULL) != DMA_SUCCESS) {
		dev_err(fsl_chan->dev, "selftest: Time out!\n");
		err = -ENODEV;
		goto out;
	}

	/* Test free and re-alloc channel resources */
	fsl_dma_free_chan_resources(chan);

	if (fsl_dma_alloc_chan_resources(chan) < 1) {
		dev_err(fsl_chan->dev,
				"selftest: Cannot alloc resources for DMA\n");
		err = -ENODEV;
		goto free_resources;
	}

	/* Continue to test
	 * TX 2
	 */
	dma_src = dma_map_single(fsl_chan->dev, src + test_size / 2,
					test_size / 4, DMA_TO_DEVICE);
	dma_dest = dma_map_single(fsl_chan->dev, dest + test_size / 2,
					test_size / 4, DMA_FROM_DEVICE);
	tx2 = fsl_dma_prep_memcpy(chan, dma_dest, dma_src, test_size / 4, 0);
	async_tx_ack(tx2);

	/* TX 3 */
	dma_src = dma_map_single(fsl_chan->dev, src + test_size * 3 / 4,
					test_size / 4, DMA_TO_DEVICE);
	dma_dest = dma_map_single(fsl_chan->dev, dest + test_size * 3 / 4,
					test_size / 4, DMA_FROM_DEVICE);
	tx3 = fsl_dma_prep_memcpy(chan, dma_dest, dma_src, test_size / 4, 0);
	async_tx_ack(tx3);

	/* Test exchanging the prepared tx sort */
	cookie = fsl_dma_tx_submit(tx3);
	cookie = fsl_dma_tx_submit(tx2);

#ifdef FSL_DMA_CALLBACKTEST
	if (dma_has_cap(DMA_INTERRUPT, ((struct fsl_dma_device *)
	    dev_get_drvdata(fsl_chan->dev->parent))->common.cap_mask)) {
		tx3->callback = fsl_dma_callback_test;
		tx3->callback_param = fsl_chan;
	}
#endif
	fsl_dma_memcpy_issue_pending(chan);
	msleep(2);

	if (fsl_dma_is_complete(chan, cookie, NULL, NULL) != DMA_SUCCESS) {
		dev_err(fsl_chan->dev, "selftest: Time out!\n");
		err = -ENODEV;
		goto free_resources;
	}

	err = memcmp(src, dest, test_size);
	if (err) {
		for (i = 0; (*(src + i) == *(dest + i)) && (i < test_size);
				i++);
		dev_err(fsl_chan->dev, "selftest: Test failed, data %d/%ld is "
				"error! src 0x%x, dest 0x%x\n",
				i, (long)test_size, *(src + i), *(dest + i));
	}

free_resources:
	fsl_dma_free_chan_resources(chan);
out:
	kfree(src);
	return err;
}
#endif

static int __devinit of_fsl_dma_chan_probe(struct of_device *dev,
			const struct of_device_id *match)
{
	struct fsl_dma_device *fdev;
	struct fsl_dma_chan *new_fsl_chan;
	int err;

	fdev = dev_get_drvdata(dev->dev.parent);
	BUG_ON(!fdev);

	/* alloc channel */
	new_fsl_chan = kzalloc(sizeof(struct fsl_dma_chan), GFP_KERNEL);
	if (!new_fsl_chan) {
		dev_err(&dev->dev, "No free memory for allocating "
				"dma channels!\n");
		err = -ENOMEM;
		goto err;
	}

	/* get dma channel register base */
	err = of_address_to_resource(dev->node, 0, &new_fsl_chan->reg);
	if (err) {
		dev_err(&dev->dev, "Can't get %s property 'reg'\n",
				dev->node->full_name);
		goto err;
	}

	new_fsl_chan->feature = *(u32 *)match->data;

	if (!fdev->feature)
		fdev->feature = new_fsl_chan->feature;

	/* If the DMA device's feature is different than its channels',
	 * report the bug.
	 */
	WARN_ON(fdev->feature != new_fsl_chan->feature);

	new_fsl_chan->dev = &dev->dev;
	new_fsl_chan->reg_base = ioremap(new_fsl_chan->reg.start,
			new_fsl_chan->reg.end - new_fsl_chan->reg.start + 1);

	new_fsl_chan->id = ((new_fsl_chan->reg.start - 0x100) & 0xfff) >> 7;
	if (new_fsl_chan->id > FSL_DMA_MAX_CHANS_PER_DEVICE) {
		dev_err(&dev->dev, "There is no %d channel!\n",
				new_fsl_chan->id);
		err = -EINVAL;
		goto err;
	}
	fdev->chan[new_fsl_chan->id] = new_fsl_chan;
	tasklet_init(&new_fsl_chan->tasklet, dma_do_tasklet,
			(unsigned long)new_fsl_chan);

	/* Init the channel */
	dma_init(new_fsl_chan);

	/* Clear cdar registers */
	set_cdar(new_fsl_chan, 0);

	switch (new_fsl_chan->feature & FSL_DMA_IP_MASK) {
	case FSL_DMA_IP_85XX:
		new_fsl_chan->toggle_ext_start = fsl_chan_toggle_ext_start;
		new_fsl_chan->toggle_ext_pause = fsl_chan_toggle_ext_pause;
	case FSL_DMA_IP_83XX:
		new_fsl_chan->set_src_loop_size = fsl_chan_set_src_loop_size;
		new_fsl_chan->set_dest_loop_size = fsl_chan_set_dest_loop_size;
	}

	spin_lock_init(&new_fsl_chan->desc_lock);
	INIT_LIST_HEAD(&new_fsl_chan->ld_queue);

	new_fsl_chan->common.device = &fdev->common;

	/* Add the channel to DMA device channel list */
	list_add_tail(&new_fsl_chan->common.device_node,
			&fdev->common.channels);
	fdev->common.chancnt++;

	new_fsl_chan->irq = irq_of_parse_and_map(dev->node, 0);
	if (new_fsl_chan->irq != NO_IRQ) {
		err = request_irq(new_fsl_chan->irq,
					&fsl_dma_chan_do_interrupt, IRQF_SHARED,
					"fsldma-channel", new_fsl_chan);
		if (err) {
			dev_err(&dev->dev, "DMA channel %s request_irq error "
				"with return %d\n", dev->node->full_name, err);
			goto err;
		}
	}

#ifdef CONFIG_FSL_DMA_SELFTEST
	err = fsl_dma_self_test(new_fsl_chan);
	if (err)
		goto err;
#endif

	dev_info(&dev->dev, "#%d (%s), irq %d\n", new_fsl_chan->id,
				match->compatible, new_fsl_chan->irq);

	return 0;
err:
	dma_halt(new_fsl_chan);
	iounmap(new_fsl_chan->reg_base);
	free_irq(new_fsl_chan->irq, new_fsl_chan);
	list_del(&new_fsl_chan->common.device_node);
	kfree(new_fsl_chan);
	return err;
}

const u32 mpc8540_dma_ip_feature = FSL_DMA_IP_85XX | FSL_DMA_BIG_ENDIAN;
const u32 mpc8349_dma_ip_feature = FSL_DMA_IP_83XX | FSL_DMA_LITTLE_ENDIAN;

static struct of_device_id of_fsl_dma_chan_ids[] = {
	{
		.compatible = "fsl,mpc8540-dma-channel",
		.data = (void *)&mpc8540_dma_ip_feature,
	},
	{
		.compatible = "fsl,mpc8349-dma-channel",
		.data = (void *)&mpc8349_dma_ip_feature,
	},
	{}
};

static struct of_platform_driver of_fsl_dma_chan_driver = {
	.name = "of-fsl-dma-channel",
	.match_table = of_fsl_dma_chan_ids,
	.probe = of_fsl_dma_chan_probe,
};

static __init int of_fsl_dma_chan_init(void)
{
	return of_register_platform_driver(&of_fsl_dma_chan_driver);
}

static int __devinit of_fsl_dma_probe(struct of_device *dev,
			const struct of_device_id *match)
{
	int err;
	unsigned int irq;
	struct fsl_dma_device *fdev;

	fdev = kzalloc(sizeof(struct fsl_dma_device), GFP_KERNEL);
	if (!fdev) {
		dev_err(&dev->dev, "No enough memory for 'priv'\n");
		err = -ENOMEM;
		goto err;
	}
	fdev->dev = &dev->dev;
	INIT_LIST_HEAD(&fdev->common.channels);

	/* get DMA controller register base */
	err = of_address_to_resource(dev->node, 0, &fdev->reg);
	if (err) {
		dev_err(&dev->dev, "Can't get %s property 'reg'\n",
				dev->node->full_name);
		goto err;
	}

	dev_info(&dev->dev, "Probe the Freescale DMA driver for %s "
			"controller at %p...\n",
			match->compatible, (void *)fdev->reg.start);
	fdev->reg_base = ioremap(fdev->reg.start, fdev->reg.end
						- fdev->reg.start + 1);

	dma_cap_set(DMA_MEMCPY, fdev->common.cap_mask);
	dma_cap_set(DMA_INTERRUPT, fdev->common.cap_mask);
	fdev->common.device_alloc_chan_resources = fsl_dma_alloc_chan_resources;
	fdev->common.device_free_chan_resources = fsl_dma_free_chan_resources;
	fdev->common.device_prep_dma_interrupt = fsl_dma_prep_interrupt;
	fdev->common.device_prep_dma_memcpy = fsl_dma_prep_memcpy;
	fdev->common.device_is_tx_complete = fsl_dma_is_complete;
	fdev->common.device_issue_pending = fsl_dma_memcpy_issue_pending;
	fdev->common.device_dependency_added = fsl_dma_dependency_added;
	fdev->common.dev = &dev->dev;

	irq = irq_of_parse_and_map(dev->node, 0);
	if (irq != NO_IRQ) {
		err = request_irq(irq, &fsl_dma_do_interrupt, IRQF_SHARED,
					"fsldma-device", fdev);
		if (err) {
			dev_err(&dev->dev, "DMA device request_irq error "
				"with return %d\n", err);
			goto err;
		}
	}

	dev_set_drvdata(&(dev->dev), fdev);
	of_platform_bus_probe(dev->node, of_fsl_dma_chan_ids, &dev->dev);

	dma_async_device_register(&fdev->common);
	return 0;

err:
	iounmap(fdev->reg_base);
	kfree(fdev);
	return err;
}

static struct of_device_id of_fsl_dma_ids[] = {
	{ .compatible = "fsl,mpc8540-dma", },
	{ .compatible = "fsl,mpc8349-dma", },
	{}
};

static struct of_platform_driver of_fsl_dma_driver = {
	.name = "of-fsl-dma",
	.match_table = of_fsl_dma_ids,
	.probe = of_fsl_dma_probe,
};

static __init int of_fsl_dma_init(void)
{
	return of_register_platform_driver(&of_fsl_dma_driver);
}

subsys_initcall(of_fsl_dma_chan_init);
subsys_initcall(of_fsl_dma_init);
