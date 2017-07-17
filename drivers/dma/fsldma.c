/*
 * Freescale MPC85xx, MPC83xx DMA Engine support
 *
 * Copyright (C) 2007-2010 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author:
 *   Zhang Wei <wei.zhang@freescale.com>, Jul 2007
 *   Ebony Zhu <ebony.zhu@freescale.com>, May 2007
 *
 * Description:
 *   DMA engine driver for Freescale MPC8540 DMA controller, which is
 *   also fit for MPC8560, MPC8555, MPC8548, MPC8641, and etc.
 *   The support for MPC8349 DMA controller is also added.
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
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/fsldma.h>
#include "dmaengine.h"
#include "fsldma.h"

#define chan_dbg(chan, fmt, arg...)					\
	dev_dbg(chan->dev, "%s: " fmt, chan->name, ##arg)
#define chan_err(chan, fmt, arg...)					\
	dev_err(chan->dev, "%s: " fmt, chan->name, ##arg)

static const char msg_ld_oom[] = "No free memory for link descriptor";

/*
 * Register Helpers
 */

static void set_sr(struct fsldma_chan *chan, u32 val)
{
	DMA_OUT(chan, &chan->regs->sr, val, 32);
}

static u32 get_sr(struct fsldma_chan *chan)
{
	return DMA_IN(chan, &chan->regs->sr, 32);
}

static void set_mr(struct fsldma_chan *chan, u32 val)
{
	DMA_OUT(chan, &chan->regs->mr, val, 32);
}

static u32 get_mr(struct fsldma_chan *chan)
{
	return DMA_IN(chan, &chan->regs->mr, 32);
}

static void set_cdar(struct fsldma_chan *chan, dma_addr_t addr)
{
	DMA_OUT(chan, &chan->regs->cdar, addr | FSL_DMA_SNEN, 64);
}

static dma_addr_t get_cdar(struct fsldma_chan *chan)
{
	return DMA_IN(chan, &chan->regs->cdar, 64) & ~FSL_DMA_SNEN;
}

static void set_bcr(struct fsldma_chan *chan, u32 val)
{
	DMA_OUT(chan, &chan->regs->bcr, val, 32);
}

static u32 get_bcr(struct fsldma_chan *chan)
{
	return DMA_IN(chan, &chan->regs->bcr, 32);
}

/*
 * Descriptor Helpers
 */

static void set_desc_cnt(struct fsldma_chan *chan,
				struct fsl_dma_ld_hw *hw, u32 count)
{
	hw->count = CPU_TO_DMA(chan, count, 32);
}

static void set_desc_src(struct fsldma_chan *chan,
			 struct fsl_dma_ld_hw *hw, dma_addr_t src)
{
	u64 snoop_bits;

	snoop_bits = ((chan->feature & FSL_DMA_IP_MASK) == FSL_DMA_IP_85XX)
		? ((u64)FSL_DMA_SATR_SREADTYPE_SNOOP_READ << 32) : 0;
	hw->src_addr = CPU_TO_DMA(chan, snoop_bits | src, 64);
}

static void set_desc_dst(struct fsldma_chan *chan,
			 struct fsl_dma_ld_hw *hw, dma_addr_t dst)
{
	u64 snoop_bits;

	snoop_bits = ((chan->feature & FSL_DMA_IP_MASK) == FSL_DMA_IP_85XX)
		? ((u64)FSL_DMA_DATR_DWRITETYPE_SNOOP_WRITE << 32) : 0;
	hw->dst_addr = CPU_TO_DMA(chan, snoop_bits | dst, 64);
}

static void set_desc_next(struct fsldma_chan *chan,
			  struct fsl_dma_ld_hw *hw, dma_addr_t next)
{
	u64 snoop_bits;

	snoop_bits = ((chan->feature & FSL_DMA_IP_MASK) == FSL_DMA_IP_83XX)
		? FSL_DMA_SNEN : 0;
	hw->next_ln_addr = CPU_TO_DMA(chan, snoop_bits | next, 64);
}

static void set_ld_eol(struct fsldma_chan *chan, struct fsl_desc_sw *desc)
{
	u64 snoop_bits;

	snoop_bits = ((chan->feature & FSL_DMA_IP_MASK) == FSL_DMA_IP_83XX)
		? FSL_DMA_SNEN : 0;

	desc->hw.next_ln_addr = CPU_TO_DMA(chan,
		DMA_TO_CPU(chan, desc->hw.next_ln_addr, 64) | FSL_DMA_EOL
			| snoop_bits, 64);
}

/*
 * DMA Engine Hardware Control Helpers
 */

static void dma_init(struct fsldma_chan *chan)
{
	/* Reset the channel */
	set_mr(chan, 0);

	switch (chan->feature & FSL_DMA_IP_MASK) {
	case FSL_DMA_IP_85XX:
		/* Set the channel to below modes:
		 * EIE - Error interrupt enable
		 * EOLNIE - End of links interrupt enable
		 * BWC - Bandwidth sharing among channels
		 */
		set_mr(chan, FSL_DMA_MR_BWC | FSL_DMA_MR_EIE
			| FSL_DMA_MR_EOLNIE);
		break;
	case FSL_DMA_IP_83XX:
		/* Set the channel to below modes:
		 * EOTIE - End-of-transfer interrupt enable
		 * PRC_RM - PCI read multiple
		 */
		set_mr(chan, FSL_DMA_MR_EOTIE | FSL_DMA_MR_PRC_RM);
		break;
	}
}

static int dma_is_idle(struct fsldma_chan *chan)
{
	u32 sr = get_sr(chan);
	return (!(sr & FSL_DMA_SR_CB)) || (sr & FSL_DMA_SR_CH);
}

/*
 * Start the DMA controller
 *
 * Preconditions:
 * - the CDAR register must point to the start descriptor
 * - the MRn[CS] bit must be cleared
 */
static void dma_start(struct fsldma_chan *chan)
{
	u32 mode;

	mode = get_mr(chan);

	if (chan->feature & FSL_DMA_CHAN_PAUSE_EXT) {
		set_bcr(chan, 0);
		mode |= FSL_DMA_MR_EMP_EN;
	} else {
		mode &= ~FSL_DMA_MR_EMP_EN;
	}

	if (chan->feature & FSL_DMA_CHAN_START_EXT) {
		mode |= FSL_DMA_MR_EMS_EN;
	} else {
		mode &= ~FSL_DMA_MR_EMS_EN;
		mode |= FSL_DMA_MR_CS;
	}

	set_mr(chan, mode);
}

static void dma_halt(struct fsldma_chan *chan)
{
	u32 mode;
	int i;

	/* read the mode register */
	mode = get_mr(chan);

	/*
	 * The 85xx controller supports channel abort, which will stop
	 * the current transfer. On 83xx, this bit is the transfer error
	 * mask bit, which should not be changed.
	 */
	if ((chan->feature & FSL_DMA_IP_MASK) == FSL_DMA_IP_85XX) {
		mode |= FSL_DMA_MR_CA;
		set_mr(chan, mode);

		mode &= ~FSL_DMA_MR_CA;
	}

	/* stop the DMA controller */
	mode &= ~(FSL_DMA_MR_CS | FSL_DMA_MR_EMS_EN);
	set_mr(chan, mode);

	/* wait for the DMA controller to become idle */
	for (i = 0; i < 100; i++) {
		if (dma_is_idle(chan))
			return;

		udelay(10);
	}

	if (!dma_is_idle(chan))
		chan_err(chan, "DMA halt timeout!\n");
}

/**
 * fsl_chan_set_src_loop_size - Set source address hold transfer size
 * @chan : Freescale DMA channel
 * @size     : Address loop size, 0 for disable loop
 *
 * The set source address hold transfer size. The source
 * address hold or loop transfer size is when the DMA transfer
 * data from source address (SA), if the loop size is 4, the DMA will
 * read data from SA, SA + 1, SA + 2, SA + 3, then loop back to SA,
 * SA + 1 ... and so on.
 */
static void fsl_chan_set_src_loop_size(struct fsldma_chan *chan, int size)
{
	u32 mode;

	mode = get_mr(chan);

	switch (size) {
	case 0:
		mode &= ~FSL_DMA_MR_SAHE;
		break;
	case 1:
	case 2:
	case 4:
	case 8:
		mode &= ~FSL_DMA_MR_SAHTS_MASK;
		mode |= FSL_DMA_MR_SAHE | (__ilog2(size) << 14);
		break;
	}

	set_mr(chan, mode);
}

/**
 * fsl_chan_set_dst_loop_size - Set destination address hold transfer size
 * @chan : Freescale DMA channel
 * @size     : Address loop size, 0 for disable loop
 *
 * The set destination address hold transfer size. The destination
 * address hold or loop transfer size is when the DMA transfer
 * data to destination address (TA), if the loop size is 4, the DMA will
 * write data to TA, TA + 1, TA + 2, TA + 3, then loop back to TA,
 * TA + 1 ... and so on.
 */
static void fsl_chan_set_dst_loop_size(struct fsldma_chan *chan, int size)
{
	u32 mode;

	mode = get_mr(chan);

	switch (size) {
	case 0:
		mode &= ~FSL_DMA_MR_DAHE;
		break;
	case 1:
	case 2:
	case 4:
	case 8:
		mode &= ~FSL_DMA_MR_DAHTS_MASK;
		mode |= FSL_DMA_MR_DAHE | (__ilog2(size) << 16);
		break;
	}

	set_mr(chan, mode);
}

/**
 * fsl_chan_set_request_count - Set DMA Request Count for external control
 * @chan : Freescale DMA channel
 * @size     : Number of bytes to transfer in a single request
 *
 * The Freescale DMA channel can be controlled by the external signal DREQ#.
 * The DMA request count is how many bytes are allowed to transfer before
 * pausing the channel, after which a new assertion of DREQ# resumes channel
 * operation.
 *
 * A size of 0 disables external pause control. The maximum size is 1024.
 */
static void fsl_chan_set_request_count(struct fsldma_chan *chan, int size)
{
	u32 mode;

	BUG_ON(size > 1024);

	mode = get_mr(chan);
	mode &= ~FSL_DMA_MR_BWC_MASK;
	mode |= (__ilog2(size) << 24) & FSL_DMA_MR_BWC_MASK;

	set_mr(chan, mode);
}

/**
 * fsl_chan_toggle_ext_pause - Toggle channel external pause status
 * @chan : Freescale DMA channel
 * @enable   : 0 is disabled, 1 is enabled.
 *
 * The Freescale DMA channel can be controlled by the external signal DREQ#.
 * The DMA Request Count feature should be used in addition to this feature
 * to set the number of bytes to transfer before pausing the channel.
 */
static void fsl_chan_toggle_ext_pause(struct fsldma_chan *chan, int enable)
{
	if (enable)
		chan->feature |= FSL_DMA_CHAN_PAUSE_EXT;
	else
		chan->feature &= ~FSL_DMA_CHAN_PAUSE_EXT;
}

/**
 * fsl_chan_toggle_ext_start - Toggle channel external start status
 * @chan : Freescale DMA channel
 * @enable   : 0 is disabled, 1 is enabled.
 *
 * If enable the external start, the channel can be started by an
 * external DMA start pin. So the dma_start() does not start the
 * transfer immediately. The DMA channel will wait for the
 * control pin asserted.
 */
static void fsl_chan_toggle_ext_start(struct fsldma_chan *chan, int enable)
{
	if (enable)
		chan->feature |= FSL_DMA_CHAN_START_EXT;
	else
		chan->feature &= ~FSL_DMA_CHAN_START_EXT;
}

int fsl_dma_external_start(struct dma_chan *dchan, int enable)
{
	struct fsldma_chan *chan;

	if (!dchan)
		return -EINVAL;

	chan = to_fsl_chan(dchan);

	fsl_chan_toggle_ext_start(chan, enable);
	return 0;
}
EXPORT_SYMBOL_GPL(fsl_dma_external_start);

static void append_ld_queue(struct fsldma_chan *chan, struct fsl_desc_sw *desc)
{
	struct fsl_desc_sw *tail = to_fsl_desc(chan->ld_pending.prev);

	if (list_empty(&chan->ld_pending))
		goto out_splice;

	/*
	 * Add the hardware descriptor to the chain of hardware descriptors
	 * that already exists in memory.
	 *
	 * This will un-set the EOL bit of the existing transaction, and the
	 * last link in this transaction will become the EOL descriptor.
	 */
	set_desc_next(chan, &tail->hw, desc->async_tx.phys);

	/*
	 * Add the software descriptor and all children to the list
	 * of pending transactions
	 */
out_splice:
	list_splice_tail_init(&desc->tx_list, &chan->ld_pending);
}

static dma_cookie_t fsl_dma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct fsldma_chan *chan = to_fsl_chan(tx->chan);
	struct fsl_desc_sw *desc = tx_to_fsl_desc(tx);
	struct fsl_desc_sw *child;
	dma_cookie_t cookie = -EINVAL;

	spin_lock_bh(&chan->desc_lock);

#ifdef CONFIG_PM
	if (unlikely(chan->pm_state != RUNNING)) {
		chan_dbg(chan, "cannot submit due to suspend\n");
		spin_unlock_bh(&chan->desc_lock);
		return -1;
	}
#endif

	/*
	 * assign cookies to all of the software descriptors
	 * that make up this transaction
	 */
	list_for_each_entry(child, &desc->tx_list, node) {
		cookie = dma_cookie_assign(&child->async_tx);
	}

	/* put this transaction onto the tail of the pending queue */
	append_ld_queue(chan, desc);

	spin_unlock_bh(&chan->desc_lock);

	return cookie;
}

/**
 * fsl_dma_free_descriptor - Free descriptor from channel's DMA pool.
 * @chan : Freescale DMA channel
 * @desc: descriptor to be freed
 */
static void fsl_dma_free_descriptor(struct fsldma_chan *chan,
		struct fsl_desc_sw *desc)
{
	list_del(&desc->node);
	chan_dbg(chan, "LD %p free\n", desc);
	dma_pool_free(chan->desc_pool, desc, desc->async_tx.phys);
}

/**
 * fsl_dma_alloc_descriptor - Allocate descriptor from channel's DMA pool.
 * @chan : Freescale DMA channel
 *
 * Return - The descriptor allocated. NULL for failed.
 */
static struct fsl_desc_sw *fsl_dma_alloc_descriptor(struct fsldma_chan *chan)
{
	struct fsl_desc_sw *desc;
	dma_addr_t pdesc;

	desc = dma_pool_zalloc(chan->desc_pool, GFP_ATOMIC, &pdesc);
	if (!desc) {
		chan_dbg(chan, "out of memory for link descriptor\n");
		return NULL;
	}

	INIT_LIST_HEAD(&desc->tx_list);
	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = fsl_dma_tx_submit;
	desc->async_tx.phys = pdesc;

	chan_dbg(chan, "LD %p allocated\n", desc);

	return desc;
}

/**
 * fsldma_clean_completed_descriptor - free all descriptors which
 * has been completed and acked
 * @chan: Freescale DMA channel
 *
 * This function is used on all completed and acked descriptors.
 * All descriptors should only be freed in this function.
 */
static void fsldma_clean_completed_descriptor(struct fsldma_chan *chan)
{
	struct fsl_desc_sw *desc, *_desc;

	/* Run the callback for each descriptor, in order */
	list_for_each_entry_safe(desc, _desc, &chan->ld_completed, node)
		if (async_tx_test_ack(&desc->async_tx))
			fsl_dma_free_descriptor(chan, desc);
}

/**
 * fsldma_run_tx_complete_actions - cleanup a single link descriptor
 * @chan: Freescale DMA channel
 * @desc: descriptor to cleanup and free
 * @cookie: Freescale DMA transaction identifier
 *
 * This function is used on a descriptor which has been executed by the DMA
 * controller. It will run any callbacks, submit any dependencies.
 */
static dma_cookie_t fsldma_run_tx_complete_actions(struct fsldma_chan *chan,
		struct fsl_desc_sw *desc, dma_cookie_t cookie)
{
	struct dma_async_tx_descriptor *txd = &desc->async_tx;
	dma_cookie_t ret = cookie;

	BUG_ON(txd->cookie < 0);

	if (txd->cookie > 0) {
		ret = txd->cookie;

		dma_descriptor_unmap(txd);
		/* Run the link descriptor callback function */
		dmaengine_desc_get_callback_invoke(txd, NULL);
	}

	/* Run any dependencies */
	dma_run_dependencies(txd);

	return ret;
}

/**
 * fsldma_clean_running_descriptor - move the completed descriptor from
 * ld_running to ld_completed
 * @chan: Freescale DMA channel
 * @desc: the descriptor which is completed
 *
 * Free the descriptor directly if acked by async_tx api, or move it to
 * queue ld_completed.
 */
static void fsldma_clean_running_descriptor(struct fsldma_chan *chan,
		struct fsl_desc_sw *desc)
{
	/* Remove from the list of transactions */
	list_del(&desc->node);

	/*
	 * the client is allowed to attach dependent operations
	 * until 'ack' is set
	 */
	if (!async_tx_test_ack(&desc->async_tx)) {
		/*
		 * Move this descriptor to the list of descriptors which is
		 * completed, but still awaiting the 'ack' bit to be set.
		 */
		list_add_tail(&desc->node, &chan->ld_completed);
		return;
	}

	dma_pool_free(chan->desc_pool, desc, desc->async_tx.phys);
}

/**
 * fsl_chan_xfer_ld_queue - transfer any pending transactions
 * @chan : Freescale DMA channel
 *
 * HARDWARE STATE: idle
 * LOCKING: must hold chan->desc_lock
 */
static void fsl_chan_xfer_ld_queue(struct fsldma_chan *chan)
{
	struct fsl_desc_sw *desc;

	/*
	 * If the list of pending descriptors is empty, then we
	 * don't need to do any work at all
	 */
	if (list_empty(&chan->ld_pending)) {
		chan_dbg(chan, "no pending LDs\n");
		return;
	}

	/*
	 * The DMA controller is not idle, which means that the interrupt
	 * handler will start any queued transactions when it runs after
	 * this transaction finishes
	 */
	if (!chan->idle) {
		chan_dbg(chan, "DMA controller still busy\n");
		return;
	}

	/*
	 * If there are some link descriptors which have not been
	 * transferred, we need to start the controller
	 */

	/*
	 * Move all elements from the queue of pending transactions
	 * onto the list of running transactions
	 */
	chan_dbg(chan, "idle, starting controller\n");
	desc = list_first_entry(&chan->ld_pending, struct fsl_desc_sw, node);
	list_splice_tail_init(&chan->ld_pending, &chan->ld_running);

	/*
	 * The 85xx DMA controller doesn't clear the channel start bit
	 * automatically at the end of a transfer. Therefore we must clear
	 * it in software before starting the transfer.
	 */
	if ((chan->feature & FSL_DMA_IP_MASK) == FSL_DMA_IP_85XX) {
		u32 mode;

		mode = get_mr(chan);
		mode &= ~FSL_DMA_MR_CS;
		set_mr(chan, mode);
	}

	/*
	 * Program the descriptor's address into the DMA controller,
	 * then start the DMA transaction
	 */
	set_cdar(chan, desc->async_tx.phys);
	get_cdar(chan);

	dma_start(chan);
	chan->idle = false;
}

/**
 * fsldma_cleanup_descriptors - cleanup link descriptors which are completed
 * and move them to ld_completed to free until flag 'ack' is set
 * @chan: Freescale DMA channel
 *
 * This function is used on descriptors which have been executed by the DMA
 * controller. It will run any callbacks, submit any dependencies, then
 * free these descriptors if flag 'ack' is set.
 */
static void fsldma_cleanup_descriptors(struct fsldma_chan *chan)
{
	struct fsl_desc_sw *desc, *_desc;
	dma_cookie_t cookie = 0;
	dma_addr_t curr_phys = get_cdar(chan);
	int seen_current = 0;

	fsldma_clean_completed_descriptor(chan);

	/* Run the callback for each descriptor, in order */
	list_for_each_entry_safe(desc, _desc, &chan->ld_running, node) {
		/*
		 * do not advance past the current descriptor loaded into the
		 * hardware channel, subsequent descriptors are either in
		 * process or have not been submitted
		 */
		if (seen_current)
			break;

		/*
		 * stop the search if we reach the current descriptor and the
		 * channel is busy
		 */
		if (desc->async_tx.phys == curr_phys) {
			seen_current = 1;
			if (!dma_is_idle(chan))
				break;
		}

		cookie = fsldma_run_tx_complete_actions(chan, desc, cookie);

		fsldma_clean_running_descriptor(chan, desc);
	}

	/*
	 * Start any pending transactions automatically
	 *
	 * In the ideal case, we keep the DMA controller busy while we go
	 * ahead and free the descriptors below.
	 */
	fsl_chan_xfer_ld_queue(chan);

	if (cookie > 0)
		chan->common.completed_cookie = cookie;
}

/**
 * fsl_dma_alloc_chan_resources - Allocate resources for DMA channel.
 * @chan : Freescale DMA channel
 *
 * This function will create a dma pool for descriptor allocation.
 *
 * Return - The number of descriptors allocated.
 */
static int fsl_dma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct fsldma_chan *chan = to_fsl_chan(dchan);

	/* Has this channel already been allocated? */
	if (chan->desc_pool)
		return 1;

	/*
	 * We need the descriptor to be aligned to 32bytes
	 * for meeting FSL DMA specification requirement.
	 */
	chan->desc_pool = dma_pool_create(chan->name, chan->dev,
					  sizeof(struct fsl_desc_sw),
					  __alignof__(struct fsl_desc_sw), 0);
	if (!chan->desc_pool) {
		chan_err(chan, "unable to allocate descriptor pool\n");
		return -ENOMEM;
	}

	/* there is at least one descriptor free to be allocated */
	return 1;
}

/**
 * fsldma_free_desc_list - Free all descriptors in a queue
 * @chan: Freescae DMA channel
 * @list: the list to free
 *
 * LOCKING: must hold chan->desc_lock
 */
static void fsldma_free_desc_list(struct fsldma_chan *chan,
				  struct list_head *list)
{
	struct fsl_desc_sw *desc, *_desc;

	list_for_each_entry_safe(desc, _desc, list, node)
		fsl_dma_free_descriptor(chan, desc);
}

static void fsldma_free_desc_list_reverse(struct fsldma_chan *chan,
					  struct list_head *list)
{
	struct fsl_desc_sw *desc, *_desc;

	list_for_each_entry_safe_reverse(desc, _desc, list, node)
		fsl_dma_free_descriptor(chan, desc);
}

/**
 * fsl_dma_free_chan_resources - Free all resources of the channel.
 * @chan : Freescale DMA channel
 */
static void fsl_dma_free_chan_resources(struct dma_chan *dchan)
{
	struct fsldma_chan *chan = to_fsl_chan(dchan);

	chan_dbg(chan, "free all channel resources\n");
	spin_lock_bh(&chan->desc_lock);
	fsldma_cleanup_descriptors(chan);
	fsldma_free_desc_list(chan, &chan->ld_pending);
	fsldma_free_desc_list(chan, &chan->ld_running);
	fsldma_free_desc_list(chan, &chan->ld_completed);
	spin_unlock_bh(&chan->desc_lock);

	dma_pool_destroy(chan->desc_pool);
	chan->desc_pool = NULL;
}

static struct dma_async_tx_descriptor *
fsl_dma_prep_memcpy(struct dma_chan *dchan,
	dma_addr_t dma_dst, dma_addr_t dma_src,
	size_t len, unsigned long flags)
{
	struct fsldma_chan *chan;
	struct fsl_desc_sw *first = NULL, *prev = NULL, *new;
	size_t copy;

	if (!dchan)
		return NULL;

	if (!len)
		return NULL;

	chan = to_fsl_chan(dchan);

	do {

		/* Allocate the link descriptor from DMA pool */
		new = fsl_dma_alloc_descriptor(chan);
		if (!new) {
			chan_err(chan, "%s\n", msg_ld_oom);
			goto fail;
		}

		copy = min(len, (size_t)FSL_DMA_BCR_MAX_CNT);

		set_desc_cnt(chan, &new->hw, copy);
		set_desc_src(chan, &new->hw, dma_src);
		set_desc_dst(chan, &new->hw, dma_dst);

		if (!first)
			first = new;
		else
			set_desc_next(chan, &prev->hw, new->async_tx.phys);

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

	/* Set End-of-link to the last link descriptor of new list */
	set_ld_eol(chan, new);

	return &first->async_tx;

fail:
	if (!first)
		return NULL;

	fsldma_free_desc_list_reverse(chan, &first->tx_list);
	return NULL;
}

static struct dma_async_tx_descriptor *fsl_dma_prep_sg(struct dma_chan *dchan,
	struct scatterlist *dst_sg, unsigned int dst_nents,
	struct scatterlist *src_sg, unsigned int src_nents,
	unsigned long flags)
{
	struct fsl_desc_sw *first = NULL, *prev = NULL, *new = NULL;
	struct fsldma_chan *chan = to_fsl_chan(dchan);
	size_t dst_avail, src_avail;
	dma_addr_t dst, src;
	size_t len;

	/* basic sanity checks */
	if (dst_nents == 0 || src_nents == 0)
		return NULL;

	if (dst_sg == NULL || src_sg == NULL)
		return NULL;

	/*
	 * TODO: should we check that both scatterlists have the same
	 * TODO: number of bytes in total? Is that really an error?
	 */

	/* get prepared for the loop */
	dst_avail = sg_dma_len(dst_sg);
	src_avail = sg_dma_len(src_sg);

	/* run until we are out of scatterlist entries */
	while (true) {

		/* create the largest transaction possible */
		len = min_t(size_t, src_avail, dst_avail);
		len = min_t(size_t, len, FSL_DMA_BCR_MAX_CNT);
		if (len == 0)
			goto fetch;

		dst = sg_dma_address(dst_sg) + sg_dma_len(dst_sg) - dst_avail;
		src = sg_dma_address(src_sg) + sg_dma_len(src_sg) - src_avail;

		/* allocate and populate the descriptor */
		new = fsl_dma_alloc_descriptor(chan);
		if (!new) {
			chan_err(chan, "%s\n", msg_ld_oom);
			goto fail;
		}

		set_desc_cnt(chan, &new->hw, len);
		set_desc_src(chan, &new->hw, src);
		set_desc_dst(chan, &new->hw, dst);

		if (!first)
			first = new;
		else
			set_desc_next(chan, &prev->hw, new->async_tx.phys);

		new->async_tx.cookie = 0;
		async_tx_ack(&new->async_tx);
		prev = new;

		/* Insert the link descriptor to the LD ring */
		list_add_tail(&new->node, &first->tx_list);

		/* update metadata */
		dst_avail -= len;
		src_avail -= len;

fetch:
		/* fetch the next dst scatterlist entry */
		if (dst_avail == 0) {

			/* no more entries: we're done */
			if (dst_nents == 0)
				break;

			/* fetch the next entry: if there are no more: done */
			dst_sg = sg_next(dst_sg);
			if (dst_sg == NULL)
				break;

			dst_nents--;
			dst_avail = sg_dma_len(dst_sg);
		}

		/* fetch the next src scatterlist entry */
		if (src_avail == 0) {

			/* no more entries: we're done */
			if (src_nents == 0)
				break;

			/* fetch the next entry: if there are no more: done */
			src_sg = sg_next(src_sg);
			if (src_sg == NULL)
				break;

			src_nents--;
			src_avail = sg_dma_len(src_sg);
		}
	}

	new->async_tx.flags = flags; /* client is in control of this ack */
	new->async_tx.cookie = -EBUSY;

	/* Set End-of-link to the last link descriptor of new list */
	set_ld_eol(chan, new);

	return &first->async_tx;

fail:
	if (!first)
		return NULL;

	fsldma_free_desc_list_reverse(chan, &first->tx_list);
	return NULL;
}

static int fsl_dma_device_terminate_all(struct dma_chan *dchan)
{
	struct fsldma_chan *chan;

	if (!dchan)
		return -EINVAL;

	chan = to_fsl_chan(dchan);

	spin_lock_bh(&chan->desc_lock);

	/* Halt the DMA engine */
	dma_halt(chan);

	/* Remove and free all of the descriptors in the LD queue */
	fsldma_free_desc_list(chan, &chan->ld_pending);
	fsldma_free_desc_list(chan, &chan->ld_running);
	fsldma_free_desc_list(chan, &chan->ld_completed);
	chan->idle = true;

	spin_unlock_bh(&chan->desc_lock);
	return 0;
}

static int fsl_dma_device_config(struct dma_chan *dchan,
				 struct dma_slave_config *config)
{
	struct fsldma_chan *chan;
	int size;

	if (!dchan)
		return -EINVAL;

	chan = to_fsl_chan(dchan);

	/* make sure the channel supports setting burst size */
	if (!chan->set_request_count)
		return -ENXIO;

	/* we set the controller burst size depending on direction */
	if (config->direction == DMA_MEM_TO_DEV)
		size = config->dst_addr_width * config->dst_maxburst;
	else
		size = config->src_addr_width * config->src_maxburst;

	chan->set_request_count(chan, size);
	return 0;
}


/**
 * fsl_dma_memcpy_issue_pending - Issue the DMA start command
 * @chan : Freescale DMA channel
 */
static void fsl_dma_memcpy_issue_pending(struct dma_chan *dchan)
{
	struct fsldma_chan *chan = to_fsl_chan(dchan);

	spin_lock_bh(&chan->desc_lock);
	fsl_chan_xfer_ld_queue(chan);
	spin_unlock_bh(&chan->desc_lock);
}

/**
 * fsl_tx_status - Determine the DMA status
 * @chan : Freescale DMA channel
 */
static enum dma_status fsl_tx_status(struct dma_chan *dchan,
					dma_cookie_t cookie,
					struct dma_tx_state *txstate)
{
	struct fsldma_chan *chan = to_fsl_chan(dchan);
	enum dma_status ret;

	ret = dma_cookie_status(dchan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	spin_lock_bh(&chan->desc_lock);
	fsldma_cleanup_descriptors(chan);
	spin_unlock_bh(&chan->desc_lock);

	return dma_cookie_status(dchan, cookie, txstate);
}

/*----------------------------------------------------------------------------*/
/* Interrupt Handling                                                         */
/*----------------------------------------------------------------------------*/

static irqreturn_t fsldma_chan_irq(int irq, void *data)
{
	struct fsldma_chan *chan = data;
	u32 stat;

	/* save and clear the status register */
	stat = get_sr(chan);
	set_sr(chan, stat);
	chan_dbg(chan, "irq: stat = 0x%x\n", stat);

	/* check that this was really our device */
	stat &= ~(FSL_DMA_SR_CB | FSL_DMA_SR_CH);
	if (!stat)
		return IRQ_NONE;

	if (stat & FSL_DMA_SR_TE)
		chan_err(chan, "Transfer Error!\n");

	/*
	 * Programming Error
	 * The DMA_INTERRUPT async_tx is a NULL transfer, which will
	 * trigger a PE interrupt.
	 */
	if (stat & FSL_DMA_SR_PE) {
		chan_dbg(chan, "irq: Programming Error INT\n");
		stat &= ~FSL_DMA_SR_PE;
		if (get_bcr(chan) != 0)
			chan_err(chan, "Programming Error!\n");
	}

	/*
	 * For MPC8349, EOCDI event need to update cookie
	 * and start the next transfer if it exist.
	 */
	if (stat & FSL_DMA_SR_EOCDI) {
		chan_dbg(chan, "irq: End-of-Chain link INT\n");
		stat &= ~FSL_DMA_SR_EOCDI;
	}

	/*
	 * If it current transfer is the end-of-transfer,
	 * we should clear the Channel Start bit for
	 * prepare next transfer.
	 */
	if (stat & FSL_DMA_SR_EOLNI) {
		chan_dbg(chan, "irq: End-of-link INT\n");
		stat &= ~FSL_DMA_SR_EOLNI;
	}

	/* check that the DMA controller is really idle */
	if (!dma_is_idle(chan))
		chan_err(chan, "irq: controller not idle!\n");

	/* check that we handled all of the bits */
	if (stat)
		chan_err(chan, "irq: unhandled sr 0x%08x\n", stat);

	/*
	 * Schedule the tasklet to handle all cleanup of the current
	 * transaction. It will start a new transaction if there is
	 * one pending.
	 */
	tasklet_schedule(&chan->tasklet);
	chan_dbg(chan, "irq: Exit\n");
	return IRQ_HANDLED;
}

static void dma_do_tasklet(unsigned long data)
{
	struct fsldma_chan *chan = (struct fsldma_chan *)data;

	chan_dbg(chan, "tasklet entry\n");

	spin_lock_bh(&chan->desc_lock);

	/* the hardware is now idle and ready for more */
	chan->idle = true;

	/* Run all cleanup for descriptors which have been completed */
	fsldma_cleanup_descriptors(chan);

	spin_unlock_bh(&chan->desc_lock);

	chan_dbg(chan, "tasklet exit\n");
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

	if (fdev->irq) {
		dev_dbg(fdev->dev, "free per-controller IRQ\n");
		free_irq(fdev->irq, fdev);
		return;
	}

	for (i = 0; i < FSL_DMA_MAX_CHANS_PER_DEVICE; i++) {
		chan = fdev->chan[i];
		if (chan && chan->irq) {
			chan_dbg(chan, "free per-channel IRQ\n");
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
	if (fdev->irq) {
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

		if (!chan->irq) {
			chan_err(chan, "interrupts property missing in device tree\n");
			ret = -ENODEV;
			goto out_unwind;
		}

		chan_dbg(chan, "request per-channel IRQ\n");
		ret = request_irq(chan->irq, fsldma_chan_irq, IRQF_SHARED,
				  "fsldma-chan", chan);
		if (ret) {
			chan_err(chan, "unable to request per-channel IRQ\n");
			goto out_unwind;
		}
	}

	return 0;

out_unwind:
	for (/* none */; i >= 0; i--) {
		chan = fdev->chan[i];
		if (!chan)
			continue;

		if (!chan->irq)
			continue;

		free_irq(chan->irq, chan);
	}

	return ret;
}

/*----------------------------------------------------------------------------*/
/* OpenFirmware Subsystem                                                     */
/*----------------------------------------------------------------------------*/

static int fsl_dma_chan_probe(struct fsldma_device *fdev,
	struct device_node *node, u32 feature, const char *compatible)
{
	struct fsldma_chan *chan;
	struct resource res;
	int err;

	/* alloc channel */
	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan) {
		err = -ENOMEM;
		goto out_return;
	}

	/* ioremap registers for use */
	chan->regs = of_iomap(node, 0);
	if (!chan->regs) {
		dev_err(fdev->dev, "unable to ioremap registers\n");
		err = -ENOMEM;
		goto out_free_chan;
	}

	err = of_address_to_resource(node, 0, &res);
	if (err) {
		dev_err(fdev->dev, "unable to find 'reg' property\n");
		goto out_iounmap_regs;
	}

	chan->feature = feature;
	if (!fdev->feature)
		fdev->feature = chan->feature;

	/*
	 * If the DMA device's feature is different than the feature
	 * of its channels, report the bug
	 */
	WARN_ON(fdev->feature != chan->feature);

	chan->dev = fdev->dev;
	chan->id = (res.start & 0xfff) < 0x300 ?
		   ((res.start - 0x100) & 0xfff) >> 7 :
		   ((res.start - 0x200) & 0xfff) >> 7;
	if (chan->id >= FSL_DMA_MAX_CHANS_PER_DEVICE) {
		dev_err(fdev->dev, "too many channels for device\n");
		err = -EINVAL;
		goto out_iounmap_regs;
	}

	fdev->chan[chan->id] = chan;
	tasklet_init(&chan->tasklet, dma_do_tasklet, (unsigned long)chan);
	snprintf(chan->name, sizeof(chan->name), "chan%d", chan->id);

	/* Initialize the channel */
	dma_init(chan);

	/* Clear cdar registers */
	set_cdar(chan, 0);

	switch (chan->feature & FSL_DMA_IP_MASK) {
	case FSL_DMA_IP_85XX:
		chan->toggle_ext_pause = fsl_chan_toggle_ext_pause;
	case FSL_DMA_IP_83XX:
		chan->toggle_ext_start = fsl_chan_toggle_ext_start;
		chan->set_src_loop_size = fsl_chan_set_src_loop_size;
		chan->set_dst_loop_size = fsl_chan_set_dst_loop_size;
		chan->set_request_count = fsl_chan_set_request_count;
	}

	spin_lock_init(&chan->desc_lock);
	INIT_LIST_HEAD(&chan->ld_pending);
	INIT_LIST_HEAD(&chan->ld_running);
	INIT_LIST_HEAD(&chan->ld_completed);
	chan->idle = true;
#ifdef CONFIG_PM
	chan->pm_state = RUNNING;
#endif

	chan->common.device = &fdev->common;
	dma_cookie_init(&chan->common);

	/* find the IRQ line, if it exists in the device tree */
	chan->irq = irq_of_parse_and_map(node, 0);

	/* Add the channel to DMA device channel list */
	list_add_tail(&chan->common.device_node, &fdev->common.channels);

	dev_info(fdev->dev, "#%d (%s), irq %d\n", chan->id, compatible,
		 chan->irq ? chan->irq : fdev->irq);

	return 0;

out_iounmap_regs:
	iounmap(chan->regs);
out_free_chan:
	kfree(chan);
out_return:
	return err;
}

static void fsl_dma_chan_remove(struct fsldma_chan *chan)
{
	irq_dispose_mapping(chan->irq);
	list_del(&chan->common.device_node);
	iounmap(chan->regs);
	kfree(chan);
}

static int fsldma_of_probe(struct platform_device *op)
{
	struct fsldma_device *fdev;
	struct device_node *child;
	int err;

	fdev = kzalloc(sizeof(*fdev), GFP_KERNEL);
	if (!fdev) {
		err = -ENOMEM;
		goto out_return;
	}

	fdev->dev = &op->dev;
	INIT_LIST_HEAD(&fdev->common.channels);

	/* ioremap the registers for use */
	fdev->regs = of_iomap(op->dev.of_node, 0);
	if (!fdev->regs) {
		dev_err(&op->dev, "unable to ioremap registers\n");
		err = -ENOMEM;
		goto out_free;
	}

	/* map the channel IRQ if it exists, but don't hookup the handler yet */
	fdev->irq = irq_of_parse_and_map(op->dev.of_node, 0);

	dma_cap_set(DMA_MEMCPY, fdev->common.cap_mask);
	dma_cap_set(DMA_SG, fdev->common.cap_mask);
	dma_cap_set(DMA_SLAVE, fdev->common.cap_mask);
	fdev->common.device_alloc_chan_resources = fsl_dma_alloc_chan_resources;
	fdev->common.device_free_chan_resources = fsl_dma_free_chan_resources;
	fdev->common.device_prep_dma_memcpy = fsl_dma_prep_memcpy;
	fdev->common.device_prep_dma_sg = fsl_dma_prep_sg;
	fdev->common.device_tx_status = fsl_tx_status;
	fdev->common.device_issue_pending = fsl_dma_memcpy_issue_pending;
	fdev->common.device_config = fsl_dma_device_config;
	fdev->common.device_terminate_all = fsl_dma_device_terminate_all;
	fdev->common.dev = &op->dev;

	fdev->common.src_addr_widths = FSL_DMA_BUSWIDTHS;
	fdev->common.dst_addr_widths = FSL_DMA_BUSWIDTHS;
	fdev->common.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	fdev->common.residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;

	dma_set_mask(&(op->dev), DMA_BIT_MASK(36));

	platform_set_drvdata(op, fdev);

	/*
	 * We cannot use of_platform_bus_probe() because there is no
	 * of_platform_bus_remove(). Instead, we manually instantiate every DMA
	 * channel object.
	 */
	for_each_child_of_node(op->dev.of_node, child) {
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
	iounmap(fdev->regs);
out_free:
	kfree(fdev);
out_return:
	return err;
}

static int fsldma_of_remove(struct platform_device *op)
{
	struct fsldma_device *fdev;
	unsigned int i;

	fdev = platform_get_drvdata(op);
	dma_async_device_unregister(&fdev->common);

	fsldma_free_irqs(fdev);

	for (i = 0; i < FSL_DMA_MAX_CHANS_PER_DEVICE; i++) {
		if (fdev->chan[i])
			fsl_dma_chan_remove(fdev->chan[i]);
	}

	iounmap(fdev->regs);
	kfree(fdev);

	return 0;
}

#ifdef CONFIG_PM
static int fsldma_suspend_late(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsldma_device *fdev = platform_get_drvdata(pdev);
	struct fsldma_chan *chan;
	int i;

	for (i = 0; i < FSL_DMA_MAX_CHANS_PER_DEVICE; i++) {
		chan = fdev->chan[i];
		if (!chan)
			continue;

		spin_lock_bh(&chan->desc_lock);
		if (unlikely(!chan->idle))
			goto out;
		chan->regs_save.mr = get_mr(chan);
		chan->pm_state = SUSPENDED;
		spin_unlock_bh(&chan->desc_lock);
	}
	return 0;

out:
	for (; i >= 0; i--) {
		chan = fdev->chan[i];
		if (!chan)
			continue;
		chan->pm_state = RUNNING;
		spin_unlock_bh(&chan->desc_lock);
	}
	return -EBUSY;
}

static int fsldma_resume_early(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsldma_device *fdev = platform_get_drvdata(pdev);
	struct fsldma_chan *chan;
	u32 mode;
	int i;

	for (i = 0; i < FSL_DMA_MAX_CHANS_PER_DEVICE; i++) {
		chan = fdev->chan[i];
		if (!chan)
			continue;

		spin_lock_bh(&chan->desc_lock);
		mode = chan->regs_save.mr
			& ~FSL_DMA_MR_CS & ~FSL_DMA_MR_CC & ~FSL_DMA_MR_CA;
		set_mr(chan, mode);
		chan->pm_state = RUNNING;
		spin_unlock_bh(&chan->desc_lock);
	}

	return 0;
}

static const struct dev_pm_ops fsldma_pm_ops = {
	.suspend_late	= fsldma_suspend_late,
	.resume_early	= fsldma_resume_early,
};
#endif

static const struct of_device_id fsldma_of_ids[] = {
	{ .compatible = "fsl,elo3-dma", },
	{ .compatible = "fsl,eloplus-dma", },
	{ .compatible = "fsl,elo-dma", },
	{}
};
MODULE_DEVICE_TABLE(of, fsldma_of_ids);

static struct platform_driver fsldma_of_driver = {
	.driver = {
		.name = "fsl-elo-dma",
		.of_match_table = fsldma_of_ids,
#ifdef CONFIG_PM
		.pm = &fsldma_pm_ops,
#endif
	},
	.probe = fsldma_of_probe,
	.remove = fsldma_of_remove,
};

/*----------------------------------------------------------------------------*/
/* Module Init / Exit                                                         */
/*----------------------------------------------------------------------------*/

static __init int fsldma_init(void)
{
	pr_info("Freescale Elo series DMA driver\n");
	return platform_driver_register(&fsldma_of_driver);
}

static void __exit fsldma_exit(void)
{
	platform_driver_unregister(&fsldma_of_driver);
}

subsys_initcall(fsldma_init);
module_exit(fsldma_exit);

MODULE_DESCRIPTION("Freescale Elo series DMA driver");
MODULE_LICENSE("GPL");
