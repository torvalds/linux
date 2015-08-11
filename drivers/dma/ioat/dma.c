/*
 * Intel I/OAT DMA Linux driver
 * Copyright(c) 2004 - 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 */

/*
 * This driver supports an Intel I/OAT DMA engine, which does asynchronous
 * copy operations.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/workqueue.h>
#include <linux/prefetch.h>
#include "dma.h"
#include "registers.h"
#include "hw.h"

#include "../dmaengine.h"

/**
 * ioat_dma_do_interrupt - handler used for single vector interrupt mode
 * @irq: interrupt id
 * @data: interrupt data
 */
irqreturn_t ioat_dma_do_interrupt(int irq, void *data)
{
	struct ioatdma_device *instance = data;
	struct ioatdma_chan *ioat_chan;
	unsigned long attnstatus;
	int bit;
	u8 intrctrl;

	intrctrl = readb(instance->reg_base + IOAT_INTRCTRL_OFFSET);

	if (!(intrctrl & IOAT_INTRCTRL_MASTER_INT_EN))
		return IRQ_NONE;

	if (!(intrctrl & IOAT_INTRCTRL_INT_STATUS)) {
		writeb(intrctrl, instance->reg_base + IOAT_INTRCTRL_OFFSET);
		return IRQ_NONE;
	}

	attnstatus = readl(instance->reg_base + IOAT_ATTNSTATUS_OFFSET);
	for_each_set_bit(bit, &attnstatus, BITS_PER_LONG) {
		ioat_chan = ioat_chan_by_index(instance, bit);
		if (test_bit(IOAT_RUN, &ioat_chan->state))
			tasklet_schedule(&ioat_chan->cleanup_task);
	}

	writeb(intrctrl, instance->reg_base + IOAT_INTRCTRL_OFFSET);
	return IRQ_HANDLED;
}

/**
 * ioat_dma_do_interrupt_msix - handler used for vector-per-channel interrupt mode
 * @irq: interrupt id
 * @data: interrupt data
 */
irqreturn_t ioat_dma_do_interrupt_msix(int irq, void *data)
{
	struct ioatdma_chan *ioat_chan = data;

	if (test_bit(IOAT_RUN, &ioat_chan->state))
		tasklet_schedule(&ioat_chan->cleanup_task);

	return IRQ_HANDLED;
}

void ioat_stop(struct ioatdma_chan *ioat_chan)
{
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	struct pci_dev *pdev = ioat_dma->pdev;
	int chan_id = chan_num(ioat_chan);
	struct msix_entry *msix;

	/* 1/ stop irq from firing tasklets
	 * 2/ stop the tasklet from re-arming irqs
	 */
	clear_bit(IOAT_RUN, &ioat_chan->state);

	/* flush inflight interrupts */
	switch (ioat_dma->irq_mode) {
	case IOAT_MSIX:
		msix = &ioat_dma->msix_entries[chan_id];
		synchronize_irq(msix->vector);
		break;
	case IOAT_MSI:
	case IOAT_INTX:
		synchronize_irq(pdev->irq);
		break;
	default:
		break;
	}

	/* flush inflight timers */
	del_timer_sync(&ioat_chan->timer);

	/* flush inflight tasklet runs */
	tasklet_kill(&ioat_chan->cleanup_task);

	/* final cleanup now that everything is quiesced and can't re-arm */
	ioat_dma->cleanup_fn((unsigned long)&ioat_chan->dma_chan);
}

dma_addr_t ioat_get_current_completion(struct ioatdma_chan *ioat_chan)
{
	dma_addr_t phys_complete;
	u64 completion;

	completion = *ioat_chan->completion;
	phys_complete = ioat_chansts_to_addr(completion);

	dev_dbg(to_dev(ioat_chan), "%s: phys_complete: %#llx\n", __func__,
		(unsigned long long) phys_complete);

	if (is_ioat_halted(completion)) {
		u32 chanerr = readl(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);

		dev_err(to_dev(ioat_chan), "Channel halted, chanerr = %x\n",
			chanerr);

		/* TODO do something to salvage the situation */
	}

	return phys_complete;
}

bool ioat_cleanup_preamble(struct ioatdma_chan *ioat_chan,
			   dma_addr_t *phys_complete)
{
	*phys_complete = ioat_get_current_completion(ioat_chan);
	if (*phys_complete == ioat_chan->last_completion)
		return false;
	clear_bit(IOAT_COMPLETION_ACK, &ioat_chan->state);
	mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);

	return true;
}

enum dma_status
ioat_dma_tx_status(struct dma_chan *c, dma_cookie_t cookie,
		   struct dma_tx_state *txstate)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	enum dma_status ret;

	ret = dma_cookie_status(c, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	ioat_dma->cleanup_fn((unsigned long) c);

	return dma_cookie_status(c, cookie, txstate);
}

void __ioat_issue_pending(struct ioatdma_chan *ioat_chan)
{
	ioat_chan->dmacount += ioat_ring_pending(ioat_chan);
	ioat_chan->issued = ioat_chan->head;
	writew(ioat_chan->dmacount,
	       ioat_chan->reg_base + IOAT_CHAN_DMACOUNT_OFFSET);
	dev_dbg(to_dev(ioat_chan),
		"%s: head: %#x tail: %#x issued: %#x count: %#x\n",
		__func__, ioat_chan->head, ioat_chan->tail,
		ioat_chan->issued, ioat_chan->dmacount);
}

void ioat_issue_pending(struct dma_chan *c)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);

	if (ioat_ring_pending(ioat_chan)) {
		spin_lock_bh(&ioat_chan->prep_lock);
		__ioat_issue_pending(ioat_chan);
		spin_unlock_bh(&ioat_chan->prep_lock);
	}
}

/**
 * ioat_update_pending - log pending descriptors
 * @ioat: ioat+ channel
 *
 * Check if the number of unsubmitted descriptors has exceeded the
 * watermark.  Called with prep_lock held
 */
static void ioat_update_pending(struct ioatdma_chan *ioat_chan)
{
	if (ioat_ring_pending(ioat_chan) > ioat_pending_level)
		__ioat_issue_pending(ioat_chan);
}

static void __ioat_start_null_desc(struct ioatdma_chan *ioat_chan)
{
	struct ioat_ring_ent *desc;
	struct ioat_dma_descriptor *hw;

	if (ioat_ring_space(ioat_chan) < 1) {
		dev_err(to_dev(ioat_chan),
			"Unable to start null desc - ring full\n");
		return;
	}

	dev_dbg(to_dev(ioat_chan),
		"%s: head: %#x tail: %#x issued: %#x\n",
		__func__, ioat_chan->head, ioat_chan->tail, ioat_chan->issued);
	desc = ioat_get_ring_ent(ioat_chan, ioat_chan->head);

	hw = desc->hw;
	hw->ctl = 0;
	hw->ctl_f.null = 1;
	hw->ctl_f.int_en = 1;
	hw->ctl_f.compl_write = 1;
	/* set size to non-zero value (channel returns error when size is 0) */
	hw->size = NULL_DESC_BUFFER_SIZE;
	hw->src_addr = 0;
	hw->dst_addr = 0;
	async_tx_ack(&desc->txd);
	ioat_set_chainaddr(ioat_chan, desc->txd.phys);
	dump_desc_dbg(ioat_chan, desc);
	/* make sure descriptors are written before we submit */
	wmb();
	ioat_chan->head += 1;
	__ioat_issue_pending(ioat_chan);
}

void ioat_start_null_desc(struct ioatdma_chan *ioat_chan)
{
	spin_lock_bh(&ioat_chan->prep_lock);
	__ioat_start_null_desc(ioat_chan);
	spin_unlock_bh(&ioat_chan->prep_lock);
}

void __ioat_restart_chan(struct ioatdma_chan *ioat_chan)
{
	/* set the tail to be re-issued */
	ioat_chan->issued = ioat_chan->tail;
	ioat_chan->dmacount = 0;
	set_bit(IOAT_COMPLETION_PENDING, &ioat_chan->state);
	mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);

	dev_dbg(to_dev(ioat_chan),
		"%s: head: %#x tail: %#x issued: %#x count: %#x\n",
		__func__, ioat_chan->head, ioat_chan->tail,
		ioat_chan->issued, ioat_chan->dmacount);

	if (ioat_ring_pending(ioat_chan)) {
		struct ioat_ring_ent *desc;

		desc = ioat_get_ring_ent(ioat_chan, ioat_chan->tail);
		ioat_set_chainaddr(ioat_chan, desc->txd.phys);
		__ioat_issue_pending(ioat_chan);
	} else
		__ioat_start_null_desc(ioat_chan);
}

int ioat_quiesce(struct ioatdma_chan *ioat_chan, unsigned long tmo)
{
	unsigned long end = jiffies + tmo;
	int err = 0;
	u32 status;

	status = ioat_chansts(ioat_chan);
	if (is_ioat_active(status) || is_ioat_idle(status))
		ioat_suspend(ioat_chan);
	while (is_ioat_active(status) || is_ioat_idle(status)) {
		if (tmo && time_after(jiffies, end)) {
			err = -ETIMEDOUT;
			break;
		}
		status = ioat_chansts(ioat_chan);
		cpu_relax();
	}

	return err;
}

int ioat_reset_sync(struct ioatdma_chan *ioat_chan, unsigned long tmo)
{
	unsigned long end = jiffies + tmo;
	int err = 0;

	ioat_reset(ioat_chan);
	while (ioat_reset_pending(ioat_chan)) {
		if (end && time_after(jiffies, end)) {
			err = -ETIMEDOUT;
			break;
		}
		cpu_relax();
	}

	return err;
}

static dma_cookie_t ioat_tx_submit_unlock(struct dma_async_tx_descriptor *tx)
{
	struct dma_chan *c = tx->chan;
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	dma_cookie_t cookie;

	cookie = dma_cookie_assign(tx);
	dev_dbg(to_dev(ioat_chan), "%s: cookie: %d\n", __func__, cookie);

	if (!test_and_set_bit(IOAT_CHAN_ACTIVE, &ioat_chan->state))
		mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);

	/* make descriptor updates visible before advancing ioat->head,
	 * this is purposefully not smp_wmb() since we are also
	 * publishing the descriptor updates to a dma device
	 */
	wmb();

	ioat_chan->head += ioat_chan->produce;

	ioat_update_pending(ioat_chan);
	spin_unlock_bh(&ioat_chan->prep_lock);

	return cookie;
}

static struct ioat_ring_ent *
ioat_alloc_ring_ent(struct dma_chan *chan, gfp_t flags)
{
	struct ioat_dma_descriptor *hw;
	struct ioat_ring_ent *desc;
	struct ioatdma_device *ioat_dma;
	dma_addr_t phys;

	ioat_dma = to_ioatdma_device(chan->device);
	hw = pci_pool_alloc(ioat_dma->dma_pool, flags, &phys);
	if (!hw)
		return NULL;
	memset(hw, 0, sizeof(*hw));

	desc = kmem_cache_zalloc(ioat_cache, flags);
	if (!desc) {
		pci_pool_free(ioat_dma->dma_pool, hw, phys);
		return NULL;
	}

	dma_async_tx_descriptor_init(&desc->txd, chan);
	desc->txd.tx_submit = ioat_tx_submit_unlock;
	desc->hw = hw;
	desc->txd.phys = phys;
	return desc;
}

void ioat_free_ring_ent(struct ioat_ring_ent *desc, struct dma_chan *chan)
{
	struct ioatdma_device *ioat_dma;

	ioat_dma = to_ioatdma_device(chan->device);
	pci_pool_free(ioat_dma->dma_pool, desc->hw, desc->txd.phys);
	kmem_cache_free(ioat_cache, desc);
}

struct ioat_ring_ent **
ioat_alloc_ring(struct dma_chan *c, int order, gfp_t flags)
{
	struct ioat_ring_ent **ring;
	int descs = 1 << order;
	int i;

	if (order > ioat_get_max_alloc_order())
		return NULL;

	/* allocate the array to hold the software ring */
	ring = kcalloc(descs, sizeof(*ring), flags);
	if (!ring)
		return NULL;
	for (i = 0; i < descs; i++) {
		ring[i] = ioat_alloc_ring_ent(c, flags);
		if (!ring[i]) {
			while (i--)
				ioat_free_ring_ent(ring[i], c);
			kfree(ring);
			return NULL;
		}
		set_desc_id(ring[i], i);
	}

	/* link descs */
	for (i = 0; i < descs-1; i++) {
		struct ioat_ring_ent *next = ring[i+1];
		struct ioat_dma_descriptor *hw = ring[i]->hw;

		hw->next = next->txd.phys;
	}
	ring[i]->hw->next = ring[0]->txd.phys;

	return ring;
}

bool reshape_ring(struct ioatdma_chan *ioat_chan, int order)
{
	/* reshape differs from normal ring allocation in that we want
	 * to allocate a new software ring while only
	 * extending/truncating the hardware ring
	 */
	struct dma_chan *c = &ioat_chan->dma_chan;
	const u32 curr_size = ioat_ring_size(ioat_chan);
	const u16 active = ioat_ring_active(ioat_chan);
	const u32 new_size = 1 << order;
	struct ioat_ring_ent **ring;
	u32 i;

	if (order > ioat_get_max_alloc_order())
		return false;

	/* double check that we have at least 1 free descriptor */
	if (active == curr_size)
		return false;

	/* when shrinking, verify that we can hold the current active
	 * set in the new ring
	 */
	if (active >= new_size)
		return false;

	/* allocate the array to hold the software ring */
	ring = kcalloc(new_size, sizeof(*ring), GFP_NOWAIT);
	if (!ring)
		return false;

	/* allocate/trim descriptors as needed */
	if (new_size > curr_size) {
		/* copy current descriptors to the new ring */
		for (i = 0; i < curr_size; i++) {
			u16 curr_idx = (ioat_chan->tail+i) & (curr_size-1);
			u16 new_idx = (ioat_chan->tail+i) & (new_size-1);

			ring[new_idx] = ioat_chan->ring[curr_idx];
			set_desc_id(ring[new_idx], new_idx);
		}

		/* add new descriptors to the ring */
		for (i = curr_size; i < new_size; i++) {
			u16 new_idx = (ioat_chan->tail+i) & (new_size-1);

			ring[new_idx] = ioat_alloc_ring_ent(c, GFP_NOWAIT);
			if (!ring[new_idx]) {
				while (i--) {
					u16 new_idx = (ioat_chan->tail+i) &
						       (new_size-1);

					ioat_free_ring_ent(ring[new_idx], c);
				}
				kfree(ring);
				return false;
			}
			set_desc_id(ring[new_idx], new_idx);
		}

		/* hw link new descriptors */
		for (i = curr_size-1; i < new_size; i++) {
			u16 new_idx = (ioat_chan->tail+i) & (new_size-1);
			struct ioat_ring_ent *next =
				ring[(new_idx+1) & (new_size-1)];
			struct ioat_dma_descriptor *hw = ring[new_idx]->hw;

			hw->next = next->txd.phys;
		}
	} else {
		struct ioat_dma_descriptor *hw;
		struct ioat_ring_ent *next;

		/* copy current descriptors to the new ring, dropping the
		 * removed descriptors
		 */
		for (i = 0; i < new_size; i++) {
			u16 curr_idx = (ioat_chan->tail+i) & (curr_size-1);
			u16 new_idx = (ioat_chan->tail+i) & (new_size-1);

			ring[new_idx] = ioat_chan->ring[curr_idx];
			set_desc_id(ring[new_idx], new_idx);
		}

		/* free deleted descriptors */
		for (i = new_size; i < curr_size; i++) {
			struct ioat_ring_ent *ent;

			ent = ioat_get_ring_ent(ioat_chan, ioat_chan->tail+i);
			ioat_free_ring_ent(ent, c);
		}

		/* fix up hardware ring */
		hw = ring[(ioat_chan->tail+new_size-1) & (new_size-1)]->hw;
		next = ring[(ioat_chan->tail+new_size) & (new_size-1)];
		hw->next = next->txd.phys;
	}

	dev_dbg(to_dev(ioat_chan), "%s: allocated %d descriptors\n",
		__func__, new_size);

	kfree(ioat_chan->ring);
	ioat_chan->ring = ring;
	ioat_chan->alloc_order = order;

	return true;
}

/**
 * ioat_check_space_lock - verify space and grab ring producer lock
 * @ioat: ioat,3 channel (ring) to operate on
 * @num_descs: allocation length
 */
int ioat_check_space_lock(struct ioatdma_chan *ioat_chan, int num_descs)
{
	bool retry;

 retry:
	spin_lock_bh(&ioat_chan->prep_lock);
	/* never allow the last descriptor to be consumed, we need at
	 * least one free at all times to allow for on-the-fly ring
	 * resizing.
	 */
	if (likely(ioat_ring_space(ioat_chan) > num_descs)) {
		dev_dbg(to_dev(ioat_chan), "%s: num_descs: %d (%x:%x:%x)\n",
			__func__, num_descs, ioat_chan->head,
			ioat_chan->tail, ioat_chan->issued);
		ioat_chan->produce = num_descs;
		return 0;  /* with ioat->prep_lock held */
	}
	retry = test_and_set_bit(IOAT_RESHAPE_PENDING, &ioat_chan->state);
	spin_unlock_bh(&ioat_chan->prep_lock);

	/* is another cpu already trying to expand the ring? */
	if (retry)
		goto retry;

	spin_lock_bh(&ioat_chan->cleanup_lock);
	spin_lock_bh(&ioat_chan->prep_lock);
	retry = reshape_ring(ioat_chan, ioat_chan->alloc_order + 1);
	clear_bit(IOAT_RESHAPE_PENDING, &ioat_chan->state);
	spin_unlock_bh(&ioat_chan->prep_lock);
	spin_unlock_bh(&ioat_chan->cleanup_lock);

	/* if we were able to expand the ring retry the allocation */
	if (retry)
		goto retry;

	dev_dbg_ratelimited(to_dev(ioat_chan),
			    "%s: ring full! num_descs: %d (%x:%x:%x)\n",
			    __func__, num_descs, ioat_chan->head,
			    ioat_chan->tail, ioat_chan->issued);

	/* progress reclaim in the allocation failure case we may be
	 * called under bh_disabled so we need to trigger the timer
	 * event directly
	 */
	if (time_is_before_jiffies(ioat_chan->timer.expires)
	    && timer_pending(&ioat_chan->timer)) {
		struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;

		mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);
		ioat_dma->timer_fn((unsigned long)ioat_chan);
	}

	return -ENOMEM;
}
