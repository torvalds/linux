/*
 * Intel I/OAT DMA Linux driver
 * Copyright(c) 2004 - 2009 Intel Corporation.
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
 * This driver supports an Intel I/OAT DMA engine (versions >= 2), which
 * does asynchronous data movement and checksumming operations.
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
#include "dma_v2.h"
#include "registers.h"
#include "hw.h"

#include "../dmaengine.h"

int ioat_ring_alloc_order = 8;
module_param(ioat_ring_alloc_order, int, 0644);
MODULE_PARM_DESC(ioat_ring_alloc_order,
		 "ioat2+: allocate 2^n descriptors per channel"
		 " (default: 8 max: 16)");
static int ioat_ring_max_alloc_order = IOAT_MAX_ORDER;
module_param(ioat_ring_max_alloc_order, int, 0644);
MODULE_PARM_DESC(ioat_ring_max_alloc_order,
		 "ioat2+: upper limit for ring size (default: 16)");

void __ioat2_issue_pending(struct ioatdma_chan *ioat_chan)
{
	ioat_chan->dmacount += ioat2_ring_pending(ioat_chan);
	ioat_chan->issued = ioat_chan->head;
	writew(ioat_chan->dmacount,
	       ioat_chan->reg_base + IOAT_CHAN_DMACOUNT_OFFSET);
	dev_dbg(to_dev(ioat_chan),
		"%s: head: %#x tail: %#x issued: %#x count: %#x\n",
		__func__, ioat_chan->head, ioat_chan->tail,
		ioat_chan->issued, ioat_chan->dmacount);
}

void ioat2_issue_pending(struct dma_chan *c)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);

	if (ioat2_ring_pending(ioat_chan)) {
		spin_lock_bh(&ioat_chan->prep_lock);
		__ioat2_issue_pending(ioat_chan);
		spin_unlock_bh(&ioat_chan->prep_lock);
	}
}

/**
 * ioat2_update_pending - log pending descriptors
 * @ioat: ioat2+ channel
 *
 * Check if the number of unsubmitted descriptors has exceeded the
 * watermark.  Called with prep_lock held
 */
static void ioat2_update_pending(struct ioatdma_chan *ioat_chan)
{
	if (ioat2_ring_pending(ioat_chan) > ioat_pending_level)
		__ioat2_issue_pending(ioat_chan);
}

static void __ioat2_start_null_desc(struct ioatdma_chan *ioat_chan)
{
	struct ioat_ring_ent *desc;
	struct ioat_dma_descriptor *hw;

	if (ioat2_ring_space(ioat_chan) < 1) {
		dev_err(to_dev(ioat_chan),
			"Unable to start null desc - ring full\n");
		return;
	}

	dev_dbg(to_dev(ioat_chan),
		"%s: head: %#x tail: %#x issued: %#x\n",
		__func__, ioat_chan->head, ioat_chan->tail, ioat_chan->issued);
	desc = ioat2_get_ring_ent(ioat_chan, ioat_chan->head);

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
	ioat2_set_chainaddr(ioat_chan, desc->txd.phys);
	dump_desc_dbg(ioat_chan, desc);
	wmb();
	ioat_chan->head += 1;
	__ioat2_issue_pending(ioat_chan);
}

static void ioat2_start_null_desc(struct ioatdma_chan *ioat_chan)
{
	spin_lock_bh(&ioat_chan->prep_lock);
	__ioat2_start_null_desc(ioat_chan);
	spin_unlock_bh(&ioat_chan->prep_lock);
}

void __ioat2_restart_chan(struct ioatdma_chan *ioat_chan)
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

	if (ioat2_ring_pending(ioat_chan)) {
		struct ioat_ring_ent *desc;

		desc = ioat2_get_ring_ent(ioat_chan, ioat_chan->tail);
		ioat2_set_chainaddr(ioat_chan, desc->txd.phys);
		__ioat2_issue_pending(ioat_chan);
	} else
		__ioat2_start_null_desc(ioat_chan);
}

int ioat2_quiesce(struct ioatdma_chan *ioat_chan, unsigned long tmo)
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

int ioat2_reset_sync(struct ioatdma_chan *ioat_chan, unsigned long tmo)
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

/**
 * ioat2_enumerate_channels - find and initialize the device's channels
 * @ioat_dma: the ioat dma device to be enumerated
 */
int ioat2_enumerate_channels(struct ioatdma_device *ioat_dma)
{
	struct ioatdma_chan *ioat_chan;
	struct device *dev = &ioat_dma->pdev->dev;
	struct dma_device *dma = &ioat_dma->dma_dev;
	u8 xfercap_log;
	int i;

	INIT_LIST_HEAD(&dma->channels);
	dma->chancnt = readb(ioat_dma->reg_base + IOAT_CHANCNT_OFFSET);
	dma->chancnt &= 0x1f; /* bits [4:0] valid */
	if (dma->chancnt > ARRAY_SIZE(ioat_dma->idx)) {
		dev_warn(dev, "(%d) exceeds max supported channels (%zu)\n",
			 dma->chancnt, ARRAY_SIZE(ioat_dma->idx));
		dma->chancnt = ARRAY_SIZE(ioat_dma->idx);
	}
	xfercap_log = readb(ioat_dma->reg_base + IOAT_XFERCAP_OFFSET);
	xfercap_log &= 0x1f; /* bits [4:0] valid */
	if (xfercap_log == 0)
		return 0;
	dev_dbg(dev, "%s: xfercap = %d\n", __func__, 1 << xfercap_log);

	for (i = 0; i < dma->chancnt; i++) {
		ioat_chan = devm_kzalloc(dev, sizeof(*ioat_chan), GFP_KERNEL);
		if (!ioat_chan)
			break;

		ioat_init_channel(ioat_dma, ioat_chan, i);
		ioat_chan->xfercap_log = xfercap_log;
		spin_lock_init(&ioat_chan->prep_lock);
		if (ioat_dma->reset_hw(ioat_chan)) {
			i = 0;
			break;
		}
	}
	dma->chancnt = i;
	return i;
}

static dma_cookie_t ioat2_tx_submit_unlock(struct dma_async_tx_descriptor *tx)
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

	ioat2_update_pending(ioat_chan);
	spin_unlock_bh(&ioat_chan->prep_lock);

	return cookie;
}

static struct ioat_ring_ent *ioat2_alloc_ring_ent(struct dma_chan *chan, gfp_t flags)
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

	desc = kmem_cache_zalloc(ioat2_cache, flags);
	if (!desc) {
		pci_pool_free(ioat_dma->dma_pool, hw, phys);
		return NULL;
	}

	dma_async_tx_descriptor_init(&desc->txd, chan);
	desc->txd.tx_submit = ioat2_tx_submit_unlock;
	desc->hw = hw;
	desc->txd.phys = phys;
	return desc;
}

static void ioat2_free_ring_ent(struct ioat_ring_ent *desc, struct dma_chan *chan)
{
	struct ioatdma_device *ioat_dma;

	ioat_dma = to_ioatdma_device(chan->device);
	pci_pool_free(ioat_dma->dma_pool, desc->hw, desc->txd.phys);
	kmem_cache_free(ioat2_cache, desc);
}

static struct ioat_ring_ent **ioat2_alloc_ring(struct dma_chan *c, int order, gfp_t flags)
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
		ring[i] = ioat2_alloc_ring_ent(c, flags);
		if (!ring[i]) {
			while (i--)
				ioat2_free_ring_ent(ring[i], c);
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

void ioat2_free_chan_resources(struct dma_chan *c);

/* ioat2_alloc_chan_resources - allocate/initialize ioat2 descriptor ring
 * @chan: channel to be initialized
 */
int ioat2_alloc_chan_resources(struct dma_chan *c)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioat_ring_ent **ring;
	u64 status;
	int order;
	int i = 0;

	/* have we already been set up? */
	if (ioat_chan->ring)
		return 1 << ioat_chan->alloc_order;

	/* Setup register to interrupt and write completion status on error */
	writew(IOAT_CHANCTRL_RUN, ioat_chan->reg_base + IOAT_CHANCTRL_OFFSET);

	/* allocate a completion writeback area */
	/* doing 2 32bit writes to mmio since 1 64b write doesn't work */
	ioat_chan->completion =
		pci_pool_alloc(ioat_chan->ioat_dma->completion_pool,
			       GFP_KERNEL, &ioat_chan->completion_dma);
	if (!ioat_chan->completion)
		return -ENOMEM;

	memset(ioat_chan->completion, 0, sizeof(*ioat_chan->completion));
	writel(((u64)ioat_chan->completion_dma) & 0x00000000FFFFFFFF,
	       ioat_chan->reg_base + IOAT_CHANCMP_OFFSET_LOW);
	writel(((u64)ioat_chan->completion_dma) >> 32,
	       ioat_chan->reg_base + IOAT_CHANCMP_OFFSET_HIGH);

	order = ioat_get_alloc_order();
	ring = ioat2_alloc_ring(c, order, GFP_KERNEL);
	if (!ring)
		return -ENOMEM;

	spin_lock_bh(&ioat_chan->cleanup_lock);
	spin_lock_bh(&ioat_chan->prep_lock);
	ioat_chan->ring = ring;
	ioat_chan->head = 0;
	ioat_chan->issued = 0;
	ioat_chan->tail = 0;
	ioat_chan->alloc_order = order;
	set_bit(IOAT_RUN, &ioat_chan->state);
	spin_unlock_bh(&ioat_chan->prep_lock);
	spin_unlock_bh(&ioat_chan->cleanup_lock);

	ioat2_start_null_desc(ioat_chan);

	/* check that we got off the ground */
	do {
		udelay(1);
		status = ioat_chansts(ioat_chan);
	} while (i++ < 20 && !is_ioat_active(status) && !is_ioat_idle(status));

	if (is_ioat_active(status) || is_ioat_idle(status)) {
		return 1 << ioat_chan->alloc_order;
	} else {
		u32 chanerr = readl(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);

		dev_WARN(to_dev(ioat_chan),
			"failed to start channel chanerr: %#x\n", chanerr);
		ioat2_free_chan_resources(c);
		return -EFAULT;
	}
}

bool reshape_ring(struct ioatdma_chan *ioat_chan, int order)
{
	/* reshape differs from normal ring allocation in that we want
	 * to allocate a new software ring while only
	 * extending/truncating the hardware ring
	 */
	struct dma_chan *c = &ioat_chan->dma_chan;
	const u32 curr_size = ioat2_ring_size(ioat_chan);
	const u16 active = ioat2_ring_active(ioat_chan);
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

			ring[new_idx] = ioat2_alloc_ring_ent(c, GFP_NOWAIT);
			if (!ring[new_idx]) {
				while (i--) {
					u16 new_idx = (ioat_chan->tail+i) &
						       (new_size-1);

					ioat2_free_ring_ent(ring[new_idx], c);
				}
				kfree(ring);
				return false;
			}
			set_desc_id(ring[new_idx], new_idx);
		}

		/* hw link new descriptors */
		for (i = curr_size-1; i < new_size; i++) {
			u16 new_idx = (ioat_chan->tail+i) & (new_size-1);
			struct ioat_ring_ent *next = ring[(new_idx+1) & (new_size-1)];
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

			ent = ioat2_get_ring_ent(ioat_chan, ioat_chan->tail+i);
			ioat2_free_ring_ent(ent, c);
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
 * ioat2_check_space_lock - verify space and grab ring producer lock
 * @ioat: ioat2,3 channel (ring) to operate on
 * @num_descs: allocation length
 */
int ioat2_check_space_lock(struct ioatdma_chan *ioat_chan, int num_descs)
{
	bool retry;

 retry:
	spin_lock_bh(&ioat_chan->prep_lock);
	/* never allow the last descriptor to be consumed, we need at
	 * least one free at all times to allow for on-the-fly ring
	 * resizing.
	 */
	if (likely(ioat2_ring_space(ioat_chan) > num_descs)) {
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

	if (printk_ratelimit())
		dev_dbg(to_dev(ioat_chan),
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

struct dma_async_tx_descriptor *
ioat2_dma_prep_memcpy_lock(struct dma_chan *c, dma_addr_t dma_dest,
			   dma_addr_t dma_src, size_t len, unsigned long flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioat_dma_descriptor *hw;
	struct ioat_ring_ent *desc;
	dma_addr_t dst = dma_dest;
	dma_addr_t src = dma_src;
	size_t total_len = len;
	int num_descs, idx, i;

	num_descs = ioat2_xferlen_to_descs(ioat_chan, len);
	if (likely(num_descs) &&
	    ioat2_check_space_lock(ioat_chan, num_descs) == 0)
		idx = ioat_chan->head;
	else
		return NULL;
	i = 0;
	do {
		size_t copy = min_t(size_t, len, 1 << ioat_chan->xfercap_log);

		desc = ioat2_get_ring_ent(ioat_chan, idx + i);
		hw = desc->hw;

		hw->size = copy;
		hw->ctl = 0;
		hw->src_addr = src;
		hw->dst_addr = dst;

		len -= copy;
		dst += copy;
		src += copy;
		dump_desc_dbg(ioat_chan, desc);
	} while (++i < num_descs);

	desc->txd.flags = flags;
	desc->len = total_len;
	hw->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
	hw->ctl_f.fence = !!(flags & DMA_PREP_FENCE);
	hw->ctl_f.compl_write = 1;
	dump_desc_dbg(ioat_chan, desc);
	/* we leave the channel locked to ensure in order submission */

	return &desc->txd;
}

/**
 * ioat2_free_chan_resources - release all the descriptors
 * @chan: the channel to be cleaned
 */
void ioat2_free_chan_resources(struct dma_chan *c)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	struct ioat_ring_ent *desc;
	const int total_descs = 1 << ioat_chan->alloc_order;
	int descs;
	int i;

	/* Before freeing channel resources first check
	 * if they have been previously allocated for this channel.
	 */
	if (!ioat_chan->ring)
		return;

	ioat_stop(ioat_chan);
	ioat_dma->reset_hw(ioat_chan);

	spin_lock_bh(&ioat_chan->cleanup_lock);
	spin_lock_bh(&ioat_chan->prep_lock);
	descs = ioat2_ring_space(ioat_chan);
	dev_dbg(to_dev(ioat_chan), "freeing %d idle descriptors\n", descs);
	for (i = 0; i < descs; i++) {
		desc = ioat2_get_ring_ent(ioat_chan, ioat_chan->head + i);
		ioat2_free_ring_ent(desc, c);
	}

	if (descs < total_descs)
		dev_err(to_dev(ioat_chan), "Freeing %d in use descriptors!\n",
			total_descs - descs);

	for (i = 0; i < total_descs - descs; i++) {
		desc = ioat2_get_ring_ent(ioat_chan, ioat_chan->tail + i);
		dump_desc_dbg(ioat_chan, desc);
		ioat2_free_ring_ent(desc, c);
	}

	kfree(ioat_chan->ring);
	ioat_chan->ring = NULL;
	ioat_chan->alloc_order = 0;
	pci_pool_free(ioat_dma->completion_pool, ioat_chan->completion,
		      ioat_chan->completion_dma);
	spin_unlock_bh(&ioat_chan->prep_lock);
	spin_unlock_bh(&ioat_chan->cleanup_lock);

	ioat_chan->last_completion = 0;
	ioat_chan->completion_dma = 0;
	ioat_chan->dmacount = 0;
}

static ssize_t ring_size_show(struct dma_chan *c, char *page)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);

	return sprintf(page, "%d\n", (1 << ioat_chan->alloc_order) & ~1);
}
static struct ioat_sysfs_entry ring_size_attr = __ATTR_RO(ring_size);

static ssize_t ring_active_show(struct dma_chan *c, char *page)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);

	/* ...taken outside the lock, no need to be precise */
	return sprintf(page, "%d\n", ioat2_ring_active(ioat_chan));
}
static struct ioat_sysfs_entry ring_active_attr = __ATTR_RO(ring_active);

static struct attribute *ioat2_attrs[] = {
	&ring_size_attr.attr,
	&ring_active_attr.attr,
	&ioat_cap_attr.attr,
	&ioat_version_attr.attr,
	NULL,
};

struct kobj_type ioat2_ktype = {
	.sysfs_ops = &ioat_sysfs_ops,
	.default_attrs = ioat2_attrs,
};
