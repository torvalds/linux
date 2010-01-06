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
 * This driver instructs the DMA controller to issue the PCI Read Multiple
 * command for PCI read operations, instead of using the default PCI Read Line
 * command. Please be aware that this setting may result in read pre-fetching
 * on some platforms.
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

#include <asm/fsldma.h>
#include "fsldma.h"

static void dma_init(struct fsldma_chan *fsl_chan)
{
	/* Reset the channel */
	DMA_OUT(fsl_chan, &fsl_chan->regs->mr, 0, 32);

	switch (fsl_chan->feature & FSL_DMA_IP_MASK) {
	case FSL_DMA_IP_85XX:
		/* Set the channel to below modes:
		 * EIE - Error interrupt enable
		 * EOSIE - End of segments interrupt enable (basic mode)
		 * EOLNIE - End of links interrupt enable
		 */
		DMA_OUT(fsl_chan, &fsl_chan->regs->mr, FSL_DMA_MR_EIE
				| FSL_DMA_MR_EOLNIE | FSL_DMA_MR_EOSIE, 32);
		break;
	case FSL_DMA_IP_83XX:
		/* Set the channel to below modes:
		 * EOTIE - End-of-transfer interrupt enable
		 * PRC_RM - PCI read multiple
		 */
		DMA_OUT(fsl_chan, &fsl_chan->regs->mr, FSL_DMA_MR_EOTIE
				| FSL_DMA_MR_PRC_RM, 32);
		break;
	}

}

static void set_sr(struct fsldma_chan *fsl_chan, u32 val)
{
	DMA_OUT(fsl_chan, &fsl_chan->regs->sr, val, 32);
}

static u32 get_sr(struct fsldma_chan *fsl_chan)
{
	return DMA_IN(fsl_chan, &fsl_chan->regs->sr, 32);
}

static void set_desc_cnt(struct fsldma_chan *fsl_chan,
				struct fsl_dma_ld_hw *hw, u32 count)
{
	hw->count = CPU_TO_DMA(fsl_chan, count, 32);
}

static void set_desc_src(struct fsldma_chan *fsl_chan,
				struct fsl_dma_ld_hw *hw, dma_addr_t src)
{
	u64 snoop_bits;

	snoop_bits = ((fsl_chan->feature & FSL_DMA_IP_MASK) == FSL_DMA_IP_85XX)
		? ((u64)FSL_DMA_SATR_SREADTYPE_SNOOP_READ << 32) : 0;
	hw->src_addr = CPU_TO_DMA(fsl_chan, snoop_bits | src, 64);
}

static void set_desc_dst(struct fsldma_chan *fsl_chan,
				struct fsl_dma_ld_hw *hw, dma_addr_t dst)
{
	u64 snoop_bits;

	snoop_bits = ((fsl_chan->feature & FSL_DMA_IP_MASK) == FSL_DMA_IP_85XX)
		? ((u64)FSL_DMA_DATR_DWRITETYPE_SNOOP_WRITE << 32) : 0;
	hw->dst_addr = CPU_TO_DMA(fsl_chan, snoop_bits | dst, 64);
}

static void set_desc_next(struct fsldma_chan *fsl_chan,
				struct fsl_dma_ld_hw *hw, dma_addr_t next)
{
	u64 snoop_bits;

	snoop_bits = ((fsl_chan->feature & FSL_DMA_IP_MASK) == FSL_DMA_IP_83XX)
		? FSL_DMA_SNEN : 0;
	hw->next_ln_addr = CPU_TO_DMA(fsl_chan, snoop_bits | next, 64);
}

static void set_cdar(struct fsldma_chan *fsl_chan, dma_addr_t addr)
{
	DMA_OUT(fsl_chan, &fsl_chan->regs->cdar, addr | FSL_DMA_SNEN, 64);
}

static dma_addr_t get_cdar(struct fsldma_chan *fsl_chan)
{
	return DMA_IN(fsl_chan, &fsl_chan->regs->cdar, 64) & ~FSL_DMA_SNEN;
}

static void set_ndar(struct fsldma_chan *fsl_chan, dma_addr_t addr)
{
	DMA_OUT(fsl_chan, &fsl_chan->regs->ndar, addr, 64);
}

static dma_addr_t get_ndar(struct fsldma_chan *fsl_chan)
{
	return DMA_IN(fsl_chan, &fsl_chan->regs->ndar, 64);
}

static u32 get_bcr(struct fsldma_chan *fsl_chan)
{
	return DMA_IN(fsl_chan, &fsl_chan->regs->bcr, 32);
}

static int dma_is_idle(struct fsldma_chan *fsl_chan)
{
	u32 sr = get_sr(fsl_chan);
	return (!(sr & FSL_DMA_SR_CB)) || (sr & FSL_DMA_SR_CH);
}

static void dma_start(struct fsldma_chan *fsl_chan)
{
	u32 mode;

	mode = DMA_IN(fsl_chan, &fsl_chan->regs->mr, 32);

	if ((fsl_chan->feature & FSL_DMA_IP_MASK) == FSL_DMA_IP_85XX) {
		if (fsl_chan->feature & FSL_DMA_CHAN_PAUSE_EXT) {
			DMA_OUT(fsl_chan, &fsl_chan->regs->bcr, 0, 32);
			mode |= FSL_DMA_MR_EMP_EN;
		} else {
			mode &= ~FSL_DMA_MR_EMP_EN;
		}
	}

	if (fsl_chan->feature & FSL_DMA_CHAN_START_EXT)
		mode |= FSL_DMA_MR_EMS_EN;
	else
		mode |= FSL_DMA_MR_CS;

	DMA_OUT(fsl_chan, &fsl_chan->regs->mr, mode, 32);
}

static void dma_halt(struct fsldma_chan *fsl_chan)
{
	u32 mode;
	int i;

	mode = DMA_IN(fsl_chan, &fsl_chan->regs->mr, 32);
	mode |= FSL_DMA_MR_CA;
	DMA_OUT(fsl_chan, &fsl_chan->regs->mr, mode, 32);

	mode &= ~(FSL_DMA_MR_CS | FSL_DMA_MR_EMS_EN | FSL_DMA_MR_CA);
	DMA_OUT(fsl_chan, &fsl_chan->regs->mr, mode, 32);

	for (i = 0; i < 100; i++) {
		if (dma_is_idle(fsl_chan))
			break;
		udelay(10);
	}

	if (i >= 100 && !dma_is_idle(fsl_chan))
		dev_err(fsl_chan->dev, "DMA halt timeout!\n");
}

static void set_ld_eol(struct fsldma_chan *fsl_chan,
			struct fsl_desc_sw *desc)
{
	u64 snoop_bits;

	snoop_bits = ((fsl_chan->feature & FSL_DMA_IP_MASK) == FSL_DMA_IP_83XX)
		? FSL_DMA_SNEN : 0;

	desc->hw.next_ln_addr = CPU_TO_DMA(fsl_chan,
		DMA_TO_CPU(fsl_chan, desc->hw.next_ln_addr, 64) | FSL_DMA_EOL
			| snoop_bits, 64);
}

static void append_ld_queue(struct fsldma_chan *fsl_chan,
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
static void fsl_chan_set_src_loop_size(struct fsldma_chan *fsl_chan, int size)
{
	u32 mode;

	mode = DMA_IN(fsl_chan, &fsl_chan->regs->mr, 32);

	switch (size) {
	case 0:
		mode &= ~FSL_DMA_MR_SAHE;
		break;
	case 1:
	case 2:
	case 4:
	case 8:
		mode |= FSL_DMA_MR_SAHE | (__ilog2(size) << 14);
		break;
	}

	DMA_OUT(fsl_chan, &fsl_chan->regs->mr, mode, 32);
}

/**
 * fsl_chan_set_dst_loop_size - Set destination address hold transfer size
 * @fsl_chan : Freescale DMA channel
 * @size     : Address loop size, 0 for disable loop
 *
 * The set destination address hold transfer size. The destination
 * address hold or loop transfer size is when the DMA transfer
 * data to destination address (TA), if the loop size is 4, the DMA will
 * write data to TA, TA + 1, TA + 2, TA + 3, then loop back to TA,
 * TA + 1 ... and so on.
 */
static void fsl_chan_set_dst_loop_size(struct fsldma_chan *fsl_chan, int size)
{
	u32 mode;

	mode = DMA_IN(fsl_chan, &fsl_chan->regs->mr, 32);

	switch (size) {
	case 0:
		mode &= ~FSL_DMA_MR_DAHE;
		break;
	case 1:
	case 2:
	case 4:
	case 8:
		mode |= FSL_DMA_MR_DAHE | (__ilog2(size) << 16);
		break;
	}

	DMA_OUT(fsl_chan, &fsl_chan->regs->mr, mode, 32);
}

/**
 * fsl_chan_set_request_count - Set DMA Request Count for external control
 * @fsl_chan : Freescale DMA channel
 * @size     : Number of bytes to transfer in a single request
 *
 * The Freescale DMA channel can be controlled by the external signal DREQ#.
 * The DMA request count is how many bytes are allowed to transfer before
 * pausing the channel, after which a new assertion of DREQ# resumes channel
 * operation.
 *
 * A size of 0 disables external pause control. The maximum size is 1024.
 */
static void fsl_chan_set_request_count(struct fsldma_chan *fsl_chan, int size)
{
	u32 mode;

	BUG_ON(size > 1024);

	mode = DMA_IN(fsl_chan, &fsl_chan->regs->mr, 32);
	mode |= (__ilog2(size) << 24) & 0x0f000000;

	DMA_OUT(fsl_chan, &fsl_chan->regs->mr, mode, 32);
}

/**
 * fsl_chan_toggle_ext_pause - Toggle channel external pause status
 * @fsl_chan : Freescale DMA channel
 * @enable   : 0 is disabled, 1 is enabled.
 *
 * The Freescale DMA channel can be controlled by the external signal DREQ#.
 * The DMA Request Count feature should be used in addition to this feature
 * to set the number of bytes to transfer before pausing the channel.
 */
static void fsl_chan_toggle_ext_pause(struct fsldma_chan *fsl_chan, int enable)
{
	if (enable)
		fsl_chan->feature |= FSL_DMA_CHAN_PAUSE_EXT;
	else
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
static void fsl_chan_toggle_ext_start(struct fsldma_chan *fsl_chan, int enable)
{
	if (enable)
		fsl_chan->feature |= FSL_DMA_CHAN_START_EXT;
	else
		fsl_chan->feature &= ~FSL_DMA_CHAN_START_EXT;
}

static dma_cookie_t fsl_dma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct fsldma_chan *fsl_chan = to_fsl_chan(tx->chan);
	struct fsl_desc_sw *desc = tx_to_fsl_desc(tx);
	struct fsl_desc_sw *child;
	unsigned long flags;
	dma_cookie_t cookie;

	/* cookie increment and adding to ld_queue must be atomic */
	spin_lock_irqsave(&fsl_chan->desc_lock, flags);

	cookie = fsl_chan->common.cookie;
	list_for_each_entry(child, &desc->tx_list, node) {
		cookie++;
		if (cookie < 0)
			cookie = 1;

		desc->async_tx.cookie = cookie;
	}

	fsl_chan->common.cookie = cookie;
	append_ld_queue(fsl_chan, desc);
	list_splice_init(&desc->tx_list, fsl_chan->ld_queue.prev);

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
					struct fsldma_chan *fsl_chan)
{
	dma_addr_t pdesc;
	struct fsl_desc_sw *desc_sw;

	desc_sw = dma_pool_alloc(fsl_chan->desc_pool, GFP_ATOMIC, &pdesc);
	if (desc_sw) {
		memset(desc_sw, 0, sizeof(struct fsl_desc_sw));
		INIT_LIST_HEAD(&desc_sw->tx_list);
		dma_async_tx_descriptor_init(&desc_sw->async_tx,
						&fsl_chan->common);
		desc_sw->async_tx.tx_submit = fsl_dma_tx_submit;
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
	struct fsldma_chan *fsl_chan = to_fsl_chan(chan);

	/* Has this channel already been allocated? */
	if (fsl_chan->desc_pool)
		return 1;

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
	struct fsldma_chan *fsl_chan = to_fsl_chan(chan);
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

	fsl_chan->desc_pool = NULL;
}

static struct dma_async_tx_descriptor *
fsl_dma_prep_interrupt(struct dma_chan *chan, unsigned long flags)
{
	struct fsldma_chan *fsl_chan;
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
	new->async_tx.flags = flags;

	/* Insert the link descriptor to the LD ring */
	list_add_tail(&new->node, &new->tx_list);

	/* Set End-of-link to the last link descriptor of new list*/
	set_ld_eol(fsl_chan, new);

	return &new->async_tx;
}

static struct dma_async_tx_descriptor *fsl_dma_prep_memcpy(
	struct dma_chan *chan, dma_addr_t dma_dst, dma_addr_t dma_src,
	size_t len, unsigned long flags)
{
	struct fsldma_chan *fsl_chan;
	struct fsl_desc_sw *first = NULL, *prev = NULL, *new;
	struct list_head *list;
	size_t copy;

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
			goto fail;
		}
#ifdef FSL_DMA_LD_DEBUG
		dev_dbg(fsl_chan->dev, "new link desc alloc %p\n", new);
#endif

		copy = min(len, (size_t)FSL_DMA_BCR_MAX_CNT);

		set_desc_cnt(fsl_chan, &new->hw, copy);
		set_desc_src(fsl_chan, &new->hw, dma_src);
		set_desc_dst(fsl_chan, &new->hw, dma_dst);

		if (!first)
			first = new;
		else
			set_desc_next(fsl_chan, &prev->hw, new->async_tx.phys);

		new->async_tx.cookie = 0;
		async_tx_ack(&new->async_tx);

		prev = new;
		len -= copy;
		dma_src += copy;
		dma_dst += copy;

		/* Insert the link descriptor to the LD ring */
		list_add_tail(&new->node, &first->tx_list);
	} while (len);

	new->async_tx.flags = flags; /* client is in control of this ack */
	new->async_tx.cookie = -EBUSY;

	/* Set End-of-link to the last link descriptor of new list*/
	set_ld_eol(fsl_chan, new);

	return &first->async_tx;

fail:
	if (!first)
		return NULL;

	list = &first->tx_list;
	list_for_each_entry_safe_reverse(new, prev, list, node) {
		list_del(&new->node);
		dma_pool_free(fsl_chan->desc_pool, new, new->async_tx.phys);
	}

	return NULL;
}

/**
 * fsl_dma_prep_slave_sg - prepare descriptors for a DMA_SLAVE transaction
 * @chan: DMA channel
 * @sgl: scatterlist to transfer to/from
 * @sg_len: number of entries in @scatterlist
 * @direction: DMA direction
 * @flags: DMAEngine flags
 *
 * Prepare a set of descriptors for a DMA_SLAVE transaction. Following the
 * DMA_SLAVE API, this gets the device-specific information from the
 * chan->private variable.
 */
static struct dma_async_tx_descriptor *fsl_dma_prep_slave_sg(
	struct dma_chan *chan, struct scatterlist *sgl, unsigned int sg_len,
	enum dma_data_direction direction, unsigned long flags)
{
	struct fsldma_chan *fsl_chan;
	struct fsl_desc_sw *first = NULL, *prev = NULL, *new = NULL;
	struct fsl_dma_slave *slave;
	struct list_head *tx_list;
	size_t copy;

	int i;
	struct scatterlist *sg;
	size_t sg_used;
	size_t hw_used;
	struct fsl_dma_hw_addr *hw;
	dma_addr_t dma_dst, dma_src;

	if (!chan)
		return NULL;

	if (!chan->private)
		return NULL;

	fsl_chan = to_fsl_chan(chan);
	slave = chan->private;

	if (list_empty(&slave->addresses))
		return NULL;

	hw = list_first_entry(&slave->addresses, struct fsl_dma_hw_addr, entry);
	hw_used = 0;

	/*
	 * Build the hardware transaction to copy from the scatterlist to
	 * the hardware, or from the hardware to the scatterlist
	 *
	 * If you are copying from the hardware to the scatterlist and it
	 * takes two hardware entries to fill an entire page, then both
	 * hardware entries will be coalesced into the same page
	 *
	 * If you are copying from the scatterlist to the hardware and a
	 * single page can fill two hardware entries, then the data will
	 * be read out of the page into the first hardware entry, and so on
	 */
	for_each_sg(sgl, sg, sg_len, i) {
		sg_used = 0;

		/* Loop until the entire scatterlist entry is used */
		while (sg_used < sg_dma_len(sg)) {

			/*
			 * If we've used up the current hardware address/length
			 * pair, we need to load a new one
			 *
			 * This is done in a while loop so that descriptors with
			 * length == 0 will be skipped
			 */
			while (hw_used >= hw->length) {

				/*
				 * If the current hardware entry is the last
				 * entry in the list, we're finished
				 */
				if (list_is_last(&hw->entry, &slave->addresses))
					goto finished;

				/* Get the next hardware address/length pair */
				hw = list_entry(hw->entry.next,
						struct fsl_dma_hw_addr, entry);
				hw_used = 0;
			}

			/* Allocate the link descriptor from DMA pool */
			new = fsl_dma_alloc_descriptor(fsl_chan);
			if (!new) {
				dev_err(fsl_chan->dev, "No free memory for "
						       "link descriptor\n");
				goto fail;
			}
#ifdef FSL_DMA_LD_DEBUG
			dev_dbg(fsl_chan->dev, "new link desc alloc %p\n", new);
#endif

			/*
			 * Calculate the maximum number of bytes to transfer,
			 * making sure it is less than the DMA controller limit
			 */
			copy = min_t(size_t, sg_dma_len(sg) - sg_used,
					     hw->length - hw_used);
			copy = min_t(size_t, copy, FSL_DMA_BCR_MAX_CNT);

			/*
			 * DMA_FROM_DEVICE
			 * from the hardware to the scatterlist
			 *
			 * DMA_TO_DEVICE
			 * from the scatterlist to the hardware
			 */
			if (direction == DMA_FROM_DEVICE) {
				dma_src = hw->address + hw_used;
				dma_dst = sg_dma_address(sg) + sg_used;
			} else {
				dma_src = sg_dma_address(sg) + sg_used;
				dma_dst = hw->address + hw_used;
			}

			/* Fill in the descriptor */
			set_desc_cnt(fsl_chan, &new->hw, copy);
			set_desc_src(fsl_chan, &new->hw, dma_src);
			set_desc_dst(fsl_chan, &new->hw, dma_dst);

			/*
			 * If this is not the first descriptor, chain the
			 * current descriptor after the previous descriptor
			 */
			if (!first) {
				first = new;
			} else {
				set_desc_next(fsl_chan, &prev->hw,
					      new->async_tx.phys);
			}

			new->async_tx.cookie = 0;
			async_tx_ack(&new->async_tx);

			prev = new;
			sg_used += copy;
			hw_used += copy;

			/* Insert the link descriptor into the LD ring */
			list_add_tail(&new->node, &first->tx_list);
		}
	}

finished:

	/* All of the hardware address/length pairs had length == 0 */
	if (!first || !new)
		return NULL;

	new->async_tx.flags = flags;
	new->async_tx.cookie = -EBUSY;

	/* Set End-of-link to the last link descriptor of new list */
	set_ld_eol(fsl_chan, new);

	/* Enable extra controller features */
	if (fsl_chan->set_src_loop_size)
		fsl_chan->set_src_loop_size(fsl_chan, slave->src_loop_size);

	if (fsl_chan->set_dst_loop_size)
		fsl_chan->set_dst_loop_size(fsl_chan, slave->dst_loop_size);

	if (fsl_chan->toggle_ext_start)
		fsl_chan->toggle_ext_start(fsl_chan, slave->external_start);

	if (fsl_chan->toggle_ext_pause)
		fsl_chan->toggle_ext_pause(fsl_chan, slave->external_pause);

	if (fsl_chan->set_request_count)
		fsl_chan->set_request_count(fsl_chan, slave->request_count);

	return &first->async_tx;

fail:
	/* If first was not set, then we failed to allocate the very first
	 * descriptor, and we're done */
	if (!first)
		return NULL;

	/*
	 * First is set, so all of the descriptors we allocated have been added
	 * to first->tx_list, INCLUDING "first" itself. Therefore we
	 * must traverse the list backwards freeing each descriptor in turn
	 *
	 * We're re-using variables for the loop, oh well
	 */
	tx_list = &first->tx_list;
	list_for_each_entry_safe_reverse(new, prev, tx_list, node) {
		list_del_init(&new->node);
		dma_pool_free(fsl_chan->desc_pool, new, new->async_tx.phys);
	}

	return NULL;
}

static void fsl_dma_device_terminate_all(struct dma_chan *chan)
{
	struct fsldma_chan *fsl_chan;
	struct fsl_desc_sw *desc, *tmp;
	unsigned long flags;

	if (!chan)
		return;

	fsl_chan = to_fsl_chan(chan);

	/* Halt the DMA engine */
	dma_halt(fsl_chan);

	spin_lock_irqsave(&fsl_chan->desc_lock, flags);

	/* Remove and free all of the descriptors in the LD queue */
	list_for_each_entry_safe(desc, tmp, &fsl_chan->ld_queue, node) {
		list_del(&desc->node);
		dma_pool_free(fsl_chan->desc_pool, desc, desc->async_tx.phys);
	}

	spin_unlock_irqrestore(&fsl_chan->desc_lock, flags);
}

/**
 * fsl_dma_update_completed_cookie - Update the completed cookie.
 * @fsl_chan : Freescale DMA channel
 */
static void fsl_dma_update_completed_cookie(struct fsldma_chan *fsl_chan)
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
static void fsl_chan_ld_cleanup(struct fsldma_chan *fsl_chan)
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
static void fsl_chan_xfer_ld_queue(struct fsldma_chan *fsl_chan)
{
	struct list_head *ld_node;
	dma_addr_t next_dst_addr;
	unsigned long flags;

	spin_lock_irqsave(&fsl_chan->desc_lock, flags);

	if (!dma_is_idle(fsl_chan))
		goto out_unlock;

	dma_halt(fsl_chan);

	/* If there are some link descriptors
	 * not transfered in queue. We need to start it.
	 */

	/* Find the first un-transfer desciptor */
	for (ld_node = fsl_chan->ld_queue.next;
		(ld_node != &fsl_chan->ld_queue)
			&& (dma_async_is_complete(
				to_fsl_desc(ld_node)->async_tx.cookie,
				fsl_chan->completed_cookie,
				fsl_chan->common.cookie) == DMA_SUCCESS);
		ld_node = ld_node->next);

	if (ld_node != &fsl_chan->ld_queue) {
		/* Get the ld start address from ld_queue */
		next_dst_addr = to_fsl_desc(ld_node)->async_tx.phys;
		dev_dbg(fsl_chan->dev, "xfer LDs staring from 0x%llx\n",
				(unsigned long long)next_dst_addr);
		set_cdar(fsl_chan, next_dst_addr);
		dma_start(fsl_chan);
	} else {
		set_cdar(fsl_chan, 0);
		set_ndar(fsl_chan, 0);
	}

out_unlock:
	spin_unlock_irqrestore(&fsl_chan->desc_lock, flags);
}

/**
 * fsl_dma_memcpy_issue_pending - Issue the DMA start command
 * @fsl_chan : Freescale DMA channel
 */
static void fsl_dma_memcpy_issue_pending(struct dma_chan *chan)
{
	struct fsldma_chan *fsl_chan = to_fsl_chan(chan);

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

/**
 * fsl_dma_is_complete - Determine the DMA status
 * @fsl_chan : Freescale DMA channel
 */
static enum dma_status fsl_dma_is_complete(struct dma_chan *chan,
					dma_cookie_t cookie,
					dma_cookie_t *done,
					dma_cookie_t *used)
{
	struct fsldma_chan *fsl_chan = to_fsl_chan(chan);
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

/*----------------------------------------------------------------------------*/
/* Interrupt Handling                                                         */
/*----------------------------------------------------------------------------*/

static irqreturn_t fsldma_chan_irq(int irq, void *data)
{
	struct fsldma_chan *fsl_chan = data;
	u32 stat;
	int update_cookie = 0;
	int xfer_ld_q = 0;

	stat = get_sr(fsl_chan);
	dev_dbg(fsl_chan->dev, "event: channel %d, stat = 0x%x\n",
						fsl_chan->id, stat);
	set_sr(fsl_chan, stat);		/* Clear the event register */

	stat &= ~(FSL_DMA_SR_CB | FSL_DMA_SR_CH);
	if (!stat)
		return IRQ_NONE;

	if (stat & FSL_DMA_SR_TE)
		dev_err(fsl_chan->dev, "Transfer Error!\n");

	/* Programming Error
	 * The DMA_INTERRUPT async_tx is a NULL transfer, which will
	 * triger a PE interrupt.
	 */
	if (stat & FSL_DMA_SR_PE) {
		dev_dbg(fsl_chan->dev, "event: Programming Error INT\n");
		if (get_bcr(fsl_chan) == 0) {
			/* BCR register is 0, this is a DMA_INTERRUPT async_tx.
			 * Now, update the completed cookie, and continue the
			 * next uncompleted transfer.
			 */
			update_cookie = 1;
			xfer_ld_q = 1;
		}
		stat &= ~FSL_DMA_SR_PE;
	}

	/* If the link descriptor segment transfer finishes,
	 * we will recycle the used descriptor.
	 */
	if (stat & FSL_DMA_SR_EOSI) {
		dev_dbg(fsl_chan->dev, "event: End-of-segments INT\n");
		dev_dbg(fsl_chan->dev, "event: clndar 0x%llx, nlndar 0x%llx\n",
			(unsigned long long)get_cdar(fsl_chan),
			(unsigned long long)get_ndar(fsl_chan));
		stat &= ~FSL_DMA_SR_EOSI;
		update_cookie = 1;
	}

	/* For MPC8349, EOCDI event need to update cookie
	 * and start the next transfer if it exist.
	 */
	if (stat & FSL_DMA_SR_EOCDI) {
		dev_dbg(fsl_chan->dev, "event: End-of-Chain link INT\n");
		stat &= ~FSL_DMA_SR_EOCDI;
		update_cookie = 1;
		xfer_ld_q = 1;
	}

	/* If it current transfer is the end-of-transfer,
	 * we should clear the Channel Start bit for
	 * prepare next transfer.
	 */
	if (stat & FSL_DMA_SR_EOLNI) {
		dev_dbg(fsl_chan->dev, "event: End-of-link INT\n");
		stat &= ~FSL_DMA_SR_EOLNI;
		xfer_ld_q = 1;
	}

	if (update_cookie)
		fsl_dma_update_completed_cookie(fsl_chan);
	if (xfer_ld_q)
		fsl_chan_xfer_ld_queue(fsl_chan);
	if (stat)
		dev_dbg(fsl_chan->dev, "event: unhandled sr 0x%02x\n",
					stat);

	dev_dbg(fsl_chan->dev, "event: Exit\n");
	tasklet_schedule(&fsl_chan->tasklet);
	return IRQ_HANDLED;
}

static void dma_do_tasklet(unsigned long data)
{
	struct fsldma_chan *fsl_chan = (struct fsldma_chan *)data;
	fsl_chan_ld_cleanup(fsl_chan);
}

static irqreturn_t fsldma_ctrl_irq(int irq, void *data)
{
	struct fsldma_device *fdev = data;
	struct fsldma_chan *chan;
	unsigned int handled = 0;
	u32 gsr, mask;
	int i;

	gsr = (fdev->feature & FSL_DMA_BIG_ENDIAN) ? in_be32(fdev->regs)
						   : in_le32(fdev->regs);
	mask = 0xff000000;
	dev_dbg(fdev->dev, "IRQ: gsr 0x%.8x\n", gsr);

	for (i = 0; i < FSL_DMA_MAX_CHANS_PER_DEVICE; i++) {
		chan = fdev->chan[i];
		if (!chan)
			continue;

		if (gsr & mask) {
			dev_dbg(fdev->dev, "IRQ: chan %d\n", chan->id);
			fsldma_chan_irq(irq, chan);
			handled++;
		}

		gsr &= ~mask;
		mask >>= 8;
	}

	return IRQ_RETVAL(handled);
}

static void fsldma_free_irqs(struct fsldma_device *fdev)
{
	struct fsldma_chan *chan;
	int i;

	if (fdev->irq != NO_IRQ) {
		dev_dbg(fdev->dev, "free per-controller IRQ\n");
		free_irq(fdev->irq, fdev);
		return;
	}

	for (i = 0; i < FSL_DMA_MAX_CHANS_PER_DEVICE; i++) {
		chan = fdev->chan[i];
		if (chan && chan->irq != NO_IRQ) {
			dev_dbg(fdev->dev, "free channel %d IRQ\n", chan->id);
			free_irq(chan->irq, chan);
		}
	}
}

static int fsldma_request_irqs(struct fsldma_device *fdev)
{
	struct fsldma_chan *chan;
	int ret;
	int i;

	/* if we have a per-controller IRQ, use that */
	if (fdev->irq != NO_IRQ) {
		dev_dbg(fdev->dev, "request per-controller IRQ\n");
		ret = request_irq(fdev->irq, fsldma_ctrl_irq, IRQF_SHARED,
				  "fsldma-controller", fdev);
		return ret;
	}

	/* no per-controller IRQ, use the per-channel IRQs */
	for (i = 0; i < FSL_DMA_MAX_CHANS_PER_DEVICE; i++) {
		chan = fdev->chan[i];
		if (!chan)
			continue;

		if (chan->irq == NO_IRQ) {
			dev_err(fdev->dev, "no interrupts property defined for "
					   "DMA channel %d. Please fix your "
					   "device tree\n", chan->id);
			ret = -ENODEV;
			goto out_unwind;
		}

		dev_dbg(fdev->dev, "request channel %d IRQ\n", chan->id);
		ret = request_irq(chan->irq, fsldma_chan_irq, IRQF_SHARED,
				  "fsldma-chan", chan);
		if (ret) {
			dev_err(fdev->dev, "unable to request IRQ for DMA "
					   "channel %d\n", chan->id);
			goto out_unwind;
		}
	}

	return 0;

out_unwind:
	for (/* none */; i >= 0; i--) {
		chan = fdev->chan[i];
		if (!chan)
			continue;

		if (chan->irq == NO_IRQ)
			continue;

		free_irq(chan->irq, chan);
	}

	return ret;
}

/*----------------------------------------------------------------------------*/
/* OpenFirmware Subsystem                                                     */
/*----------------------------------------------------------------------------*/

static int __devinit fsl_dma_chan_probe(struct fsldma_device *fdev,
	struct device_node *node, u32 feature, const char *compatible)
{
	struct fsldma_chan *fchan;
	struct resource res;
	int err;

	/* alloc channel */
	fchan = kzalloc(sizeof(*fchan), GFP_KERNEL);
	if (!fchan) {
		dev_err(fdev->dev, "no free memory for DMA channels!\n");
		err = -ENOMEM;
		goto out_return;
	}

	/* ioremap registers for use */
	fchan->regs = of_iomap(node, 0);
	if (!fchan->regs) {
		dev_err(fdev->dev, "unable to ioremap registers\n");
		err = -ENOMEM;
		goto out_free_fchan;
	}

	err = of_address_to_resource(node, 0, &res);
	if (err) {
		dev_err(fdev->dev, "unable to find 'reg' property\n");
		goto out_iounmap_regs;
	}

	fchan->feature = feature;
	if (!fdev->feature)
		fdev->feature = fchan->feature;

	/*
	 * If the DMA device's feature is different than the feature
	 * of its channels, report the bug
	 */
	WARN_ON(fdev->feature != fchan->feature);

	fchan->dev = fdev->dev;
	fchan->id = ((res.start - 0x100) & 0xfff) >> 7;
	if (fchan->id >= FSL_DMA_MAX_CHANS_PER_DEVICE) {
		dev_err(fdev->dev, "too many channels for device\n");
		err = -EINVAL;
		goto out_iounmap_regs;
	}

	fdev->chan[fchan->id] = fchan;
	tasklet_init(&fchan->tasklet, dma_do_tasklet, (unsigned long)fchan);

	/* Initialize the channel */
	dma_init(fchan);

	/* Clear cdar registers */
	set_cdar(fchan, 0);

	switch (fchan->feature & FSL_DMA_IP_MASK) {
	case FSL_DMA_IP_85XX:
		fchan->toggle_ext_pause = fsl_chan_toggle_ext_pause;
	case FSL_DMA_IP_83XX:
		fchan->toggle_ext_start = fsl_chan_toggle_ext_start;
		fchan->set_src_loop_size = fsl_chan_set_src_loop_size;
		fchan->set_dst_loop_size = fsl_chan_set_dst_loop_size;
		fchan->set_request_count = fsl_chan_set_request_count;
	}

	spin_lock_init(&fchan->desc_lock);
	INIT_LIST_HEAD(&fchan->ld_queue);

	fchan->common.device = &fdev->common;

	/* find the IRQ line, if it exists in the device tree */
	fchan->irq = irq_of_parse_and_map(node, 0);

	/* Add the channel to DMA device channel list */
	list_add_tail(&fchan->common.device_node, &fdev->common.channels);
	fdev->common.chancnt++;

	dev_info(fdev->dev, "#%d (%s), irq %d\n", fchan->id, compatible,
		 fchan->irq != NO_IRQ ? fchan->irq : fdev->irq);

	return 0;

out_iounmap_regs:
	iounmap(fchan->regs);
out_free_fchan:
	kfree(fchan);
out_return:
	return err;
}

static void fsl_dma_chan_remove(struct fsldma_chan *fchan)
{
	irq_dispose_mapping(fchan->irq);
	list_del(&fchan->common.device_node);
	iounmap(fchan->regs);
	kfree(fchan);
}

static int __devinit fsldma_of_probe(struct of_device *op,
			const struct of_device_id *match)
{
	struct fsldma_device *fdev;
	struct device_node *child;
	int err;

	fdev = kzalloc(sizeof(*fdev), GFP_KERNEL);
	if (!fdev) {
		dev_err(&op->dev, "No enough memory for 'priv'\n");
		err = -ENOMEM;
		goto out_return;
	}

	fdev->dev = &op->dev;
	INIT_LIST_HEAD(&fdev->common.channels);

	/* ioremap the registers for use */
	fdev->regs = of_iomap(op->node, 0);
	if (!fdev->regs) {
		dev_err(&op->dev, "unable to ioremap registers\n");
		err = -ENOMEM;
		goto out_free_fdev;
	}

	/* map the channel IRQ if it exists, but don't hookup the handler yet */
	fdev->irq = irq_of_parse_and_map(op->node, 0);

	dma_cap_set(DMA_MEMCPY, fdev->common.cap_mask);
	dma_cap_set(DMA_INTERRUPT, fdev->common.cap_mask);
	dma_cap_set(DMA_SLAVE, fdev->common.cap_mask);
	fdev->common.device_alloc_chan_resources = fsl_dma_alloc_chan_resources;
	fdev->common.device_free_chan_resources = fsl_dma_free_chan_resources;
	fdev->common.device_prep_dma_interrupt = fsl_dma_prep_interrupt;
	fdev->common.device_prep_dma_memcpy = fsl_dma_prep_memcpy;
	fdev->common.device_is_tx_complete = fsl_dma_is_complete;
	fdev->common.device_issue_pending = fsl_dma_memcpy_issue_pending;
	fdev->common.device_prep_slave_sg = fsl_dma_prep_slave_sg;
	fdev->common.device_terminate_all = fsl_dma_device_terminate_all;
	fdev->common.dev = &op->dev;

	dev_set_drvdata(&op->dev, fdev);

	/*
	 * We cannot use of_platform_bus_probe() because there is no
	 * of_platform_bus_remove(). Instead, we manually instantiate every DMA
	 * channel object.
	 */
	for_each_child_of_node(op->node, child) {
		if (of_device_is_compatible(child, "fsl,eloplus-dma-channel")) {
			fsl_dma_chan_probe(fdev, child,
				FSL_DMA_IP_85XX | FSL_DMA_BIG_ENDIAN,
				"fsl,eloplus-dma-channel");
		}

		if (of_device_is_compatible(child, "fsl,elo-dma-channel")) {
			fsl_dma_chan_probe(fdev, child,
				FSL_DMA_IP_83XX | FSL_DMA_LITTLE_ENDIAN,
				"fsl,elo-dma-channel");
		}
	}

	/*
	 * Hookup the IRQ handler(s)
	 *
	 * If we have a per-controller interrupt, we prefer that to the
	 * per-channel interrupts to reduce the number of shared interrupt
	 * handlers on the same IRQ line
	 */
	err = fsldma_request_irqs(fdev);
	if (err) {
		dev_err(fdev->dev, "unable to request IRQs\n");
		goto out_free_fdev;
	}

	dma_async_device_register(&fdev->common);
	return 0;

out_free_fdev:
	irq_dispose_mapping(fdev->irq);
	kfree(fdev);
out_return:
	return err;
}

static int fsldma_of_remove(struct of_device *op)
{
	struct fsldma_device *fdev;
	unsigned int i;

	fdev = dev_get_drvdata(&op->dev);
	dma_async_device_unregister(&fdev->common);

	fsldma_free_irqs(fdev);

	for (i = 0; i < FSL_DMA_MAX_CHANS_PER_DEVICE; i++) {
		if (fdev->chan[i])
			fsl_dma_chan_remove(fdev->chan[i]);
	}

	iounmap(fdev->regs);
	dev_set_drvdata(&op->dev, NULL);
	kfree(fdev);

	return 0;
}

static struct of_device_id fsldma_of_ids[] = {
	{ .compatible = "fsl,eloplus-dma", },
	{ .compatible = "fsl,elo-dma", },
	{}
};

static struct of_platform_driver fsldma_of_driver = {
	.name		= "fsl-elo-dma",
	.match_table	= fsldma_of_ids,
	.probe		= fsldma_of_probe,
	.remove		= fsldma_of_remove,
};

/*----------------------------------------------------------------------------*/
/* Module Init / Exit                                                         */
/*----------------------------------------------------------------------------*/

static __init int fsldma_init(void)
{
	int ret;

	pr_info("Freescale Elo / Elo Plus DMA driver\n");

	ret = of_register_platform_driver(&fsldma_of_driver);
	if (ret)
		pr_err("fsldma: failed to register platform driver\n");

	return ret;
}

static void __exit fsldma_exit(void)
{
	of_unregister_platform_driver(&fsldma_of_driver);
}

subsys_initcall(fsldma_init);
module_exit(fsldma_exit);

MODULE_DESCRIPTION("Freescale Elo / Elo Plus DMA driver");
MODULE_LICENSE("GPL");
