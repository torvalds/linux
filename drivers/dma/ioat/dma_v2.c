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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
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
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/workqueue.h>
#include <linux/i7300_idle.h>
#include "dma.h"
#include "dma_v2.h"
#include "registers.h"
#include "hw.h"

int ioat_ring_alloc_order = 8;
module_param(ioat_ring_alloc_order, int, 0644);
MODULE_PARM_DESC(ioat_ring_alloc_order,
		 "ioat2+: allocate 2^n descriptors per channel"
		 " (default: 8 max: 16)");
static int ioat_ring_max_alloc_order = IOAT_MAX_ORDER;
module_param(ioat_ring_max_alloc_order, int, 0644);
MODULE_PARM_DESC(ioat_ring_max_alloc_order,
		 "ioat2+: upper limit for ring size (default: 16)");

void __ioat2_issue_pending(struct ioat2_dma_chan *ioat)
{
	void * __iomem reg_base = ioat->base.reg_base;

	ioat->pending = 0;
	ioat->dmacount += ioat2_ring_pending(ioat);
	ioat->issued = ioat->head;
	/* make descriptor updates globally visible before notifying channel */
	wmb();
	writew(ioat->dmacount, reg_base + IOAT_CHAN_DMACOUNT_OFFSET);
	dev_dbg(to_dev(&ioat->base),
		"%s: head: %#x tail: %#x issued: %#x count: %#x\n",
		__func__, ioat->head, ioat->tail, ioat->issued, ioat->dmacount);
}

void ioat2_issue_pending(struct dma_chan *chan)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(chan);

	spin_lock_bh(&ioat->ring_lock);
	if (ioat->pending == 1)
		__ioat2_issue_pending(ioat);
	spin_unlock_bh(&ioat->ring_lock);
}

/**
 * ioat2_update_pending - log pending descriptors
 * @ioat: ioat2+ channel
 *
 * set pending to '1' unless pending is already set to '2', pending == 2
 * indicates that submission is temporarily blocked due to an in-flight
 * reset.  If we are already above the ioat_pending_level threshold then
 * just issue pending.
 *
 * called with ring_lock held
 */
static void ioat2_update_pending(struct ioat2_dma_chan *ioat)
{
	if (unlikely(ioat->pending == 2))
		return;
	else if (ioat2_ring_pending(ioat) > ioat_pending_level)
		__ioat2_issue_pending(ioat);
	else
		ioat->pending = 1;
}

static void __ioat2_start_null_desc(struct ioat2_dma_chan *ioat)
{
	struct ioat_ring_ent *desc;
	struct ioat_dma_descriptor *hw;
	int idx;

	if (ioat2_ring_space(ioat) < 1) {
		dev_err(to_dev(&ioat->base),
			"Unable to start null desc - ring full\n");
		return;
	}

	dev_dbg(to_dev(&ioat->base), "%s: head: %#x tail: %#x issued: %#x\n",
		__func__, ioat->head, ioat->tail, ioat->issued);
	idx = ioat2_desc_alloc(ioat, 1);
	desc = ioat2_get_ring_ent(ioat, idx);

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
	ioat2_set_chainaddr(ioat, desc->txd.phys);
	dump_desc_dbg(ioat, desc);
	__ioat2_issue_pending(ioat);
}

static void ioat2_start_null_desc(struct ioat2_dma_chan *ioat)
{
	spin_lock_bh(&ioat->ring_lock);
	__ioat2_start_null_desc(ioat);
	spin_unlock_bh(&ioat->ring_lock);
}

static void __cleanup(struct ioat2_dma_chan *ioat, unsigned long phys_complete)
{
	struct ioat_chan_common *chan = &ioat->base;
	struct dma_async_tx_descriptor *tx;
	struct ioat_ring_ent *desc;
	bool seen_current = false;
	u16 active;
	int i;

	dev_dbg(to_dev(chan), "%s: head: %#x tail: %#x issued: %#x\n",
		__func__, ioat->head, ioat->tail, ioat->issued);

	active = ioat2_ring_active(ioat);
	for (i = 0; i < active && !seen_current; i++) {
		prefetch(ioat2_get_ring_ent(ioat, ioat->tail + i + 1));
		desc = ioat2_get_ring_ent(ioat, ioat->tail + i);
		tx = &desc->txd;
		dump_desc_dbg(ioat, desc);
		if (tx->cookie) {
			ioat_dma_unmap(chan, tx->flags, desc->len, desc->hw);
			chan->completed_cookie = tx->cookie;
			tx->cookie = 0;
			if (tx->callback) {
				tx->callback(tx->callback_param);
				tx->callback = NULL;
			}
		}

		if (tx->phys == phys_complete)
			seen_current = true;
	}
	ioat->tail += i;
	BUG_ON(!seen_current); /* no active descs have written a completion? */

	chan->last_completion = phys_complete;
	if (ioat->head == ioat->tail) {
		dev_dbg(to_dev(chan), "%s: cancel completion timeout\n",
			__func__);
		clear_bit(IOAT_COMPLETION_PENDING, &chan->state);
		mod_timer(&chan->timer, jiffies + IDLE_TIMEOUT);
	}
}

/**
 * ioat2_cleanup - clean finished descriptors (advance tail pointer)
 * @chan: ioat channel to be cleaned up
 */
static void ioat2_cleanup(struct ioat2_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;
	unsigned long phys_complete;

	prefetch(chan->completion);

	if (!spin_trylock_bh(&chan->cleanup_lock))
		return;

	if (!ioat_cleanup_preamble(chan, &phys_complete)) {
		spin_unlock_bh(&chan->cleanup_lock);
		return;
	}

	if (!spin_trylock_bh(&ioat->ring_lock)) {
		spin_unlock_bh(&chan->cleanup_lock);
		return;
	}

	__cleanup(ioat, phys_complete);

	spin_unlock_bh(&ioat->ring_lock);
	spin_unlock_bh(&chan->cleanup_lock);
}

void ioat2_cleanup_tasklet(unsigned long data)
{
	struct ioat2_dma_chan *ioat = (void *) data;

	ioat2_cleanup(ioat);
	writew(IOAT_CHANCTRL_RUN, ioat->base.reg_base + IOAT_CHANCTRL_OFFSET);
}

void __ioat2_restart_chan(struct ioat2_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;

	/* set the tail to be re-issued */
	ioat->issued = ioat->tail;
	ioat->dmacount = 0;
	set_bit(IOAT_COMPLETION_PENDING, &chan->state);
	mod_timer(&chan->timer, jiffies + COMPLETION_TIMEOUT);

	dev_dbg(to_dev(chan),
		"%s: head: %#x tail: %#x issued: %#x count: %#x\n",
		__func__, ioat->head, ioat->tail, ioat->issued, ioat->dmacount);

	if (ioat2_ring_pending(ioat)) {
		struct ioat_ring_ent *desc;

		desc = ioat2_get_ring_ent(ioat, ioat->tail);
		ioat2_set_chainaddr(ioat, desc->txd.phys);
		__ioat2_issue_pending(ioat);
	} else
		__ioat2_start_null_desc(ioat);
}

int ioat2_quiesce(struct ioat_chan_common *chan, unsigned long tmo)
{
	unsigned long end = jiffies + tmo;
	int err = 0;
	u32 status;

	status = ioat_chansts(chan);
	if (is_ioat_active(status) || is_ioat_idle(status))
		ioat_suspend(chan);
	while (is_ioat_active(status) || is_ioat_idle(status)) {
		if (tmo && time_after(jiffies, end)) {
			err = -ETIMEDOUT;
			break;
		}
		status = ioat_chansts(chan);
		cpu_relax();
	}

	return err;
}

int ioat2_reset_sync(struct ioat_chan_common *chan, unsigned long tmo)
{
	unsigned long end = jiffies + tmo;
	int err = 0;

	ioat_reset(chan);
	while (ioat_reset_pending(chan)) {
		if (end && time_after(jiffies, end)) {
			err = -ETIMEDOUT;
			break;
		}
		cpu_relax();
	}

	return err;
}

static void ioat2_restart_channel(struct ioat2_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;
	unsigned long phys_complete;

	ioat2_quiesce(chan, 0);
	if (ioat_cleanup_preamble(chan, &phys_complete))
		__cleanup(ioat, phys_complete);

	__ioat2_restart_chan(ioat);
}

void ioat2_timer_event(unsigned long data)
{
	struct ioat2_dma_chan *ioat = (void *) data;
	struct ioat_chan_common *chan = &ioat->base;

	spin_lock_bh(&chan->cleanup_lock);
	if (test_bit(IOAT_COMPLETION_PENDING, &chan->state)) {
		unsigned long phys_complete;
		u64 status;

		spin_lock_bh(&ioat->ring_lock);
		status = ioat_chansts(chan);

		/* when halted due to errors check for channel
		 * programming errors before advancing the completion state
		 */
		if (is_ioat_halted(status)) {
			u32 chanerr;

			chanerr = readl(chan->reg_base + IOAT_CHANERR_OFFSET);
			dev_err(to_dev(chan), "%s: Channel halted (%x)\n",
				__func__, chanerr);
			BUG_ON(is_ioat_bug(chanerr));
		}

		/* if we haven't made progress and we have already
		 * acknowledged a pending completion once, then be more
		 * forceful with a restart
		 */
		if (ioat_cleanup_preamble(chan, &phys_complete))
			__cleanup(ioat, phys_complete);
		else if (test_bit(IOAT_COMPLETION_ACK, &chan->state))
			ioat2_restart_channel(ioat);
		else {
			set_bit(IOAT_COMPLETION_ACK, &chan->state);
			mod_timer(&chan->timer, jiffies + COMPLETION_TIMEOUT);
		}
		spin_unlock_bh(&ioat->ring_lock);
	} else {
		u16 active;

		/* if the ring is idle, empty, and oversized try to step
		 * down the size
		 */
		spin_lock_bh(&ioat->ring_lock);
		active = ioat2_ring_active(ioat);
		if (active == 0 && ioat->alloc_order > ioat_get_alloc_order())
			reshape_ring(ioat, ioat->alloc_order-1);
		spin_unlock_bh(&ioat->ring_lock);

		/* keep shrinking until we get back to our minimum
		 * default size
		 */
		if (ioat->alloc_order > ioat_get_alloc_order())
			mod_timer(&chan->timer, jiffies + IDLE_TIMEOUT);
	}
	spin_unlock_bh(&chan->cleanup_lock);
}

static int ioat2_reset_hw(struct ioat_chan_common *chan)
{
	/* throw away whatever the channel was doing and get it initialized */
	u32 chanerr;

	ioat2_quiesce(chan, msecs_to_jiffies(100));

	chanerr = readl(chan->reg_base + IOAT_CHANERR_OFFSET);
	writel(chanerr, chan->reg_base + IOAT_CHANERR_OFFSET);

	return ioat2_reset_sync(chan, msecs_to_jiffies(200));
}

/**
 * ioat2_enumerate_channels - find and initialize the device's channels
 * @device: the device to be enumerated
 */
int ioat2_enumerate_channels(struct ioatdma_device *device)
{
	struct ioat2_dma_chan *ioat;
	struct device *dev = &device->pdev->dev;
	struct dma_device *dma = &device->common;
	u8 xfercap_log;
	int i;

	INIT_LIST_HEAD(&dma->channels);
	dma->chancnt = readb(device->reg_base + IOAT_CHANCNT_OFFSET);
	dma->chancnt &= 0x1f; /* bits [4:0] valid */
	if (dma->chancnt > ARRAY_SIZE(device->idx)) {
		dev_warn(dev, "(%d) exceeds max supported channels (%zu)\n",
			 dma->chancnt, ARRAY_SIZE(device->idx));
		dma->chancnt = ARRAY_SIZE(device->idx);
	}
	xfercap_log = readb(device->reg_base + IOAT_XFERCAP_OFFSET);
	xfercap_log &= 0x1f; /* bits [4:0] valid */
	if (xfercap_log == 0)
		return 0;
	dev_dbg(dev, "%s: xfercap = %d\n", __func__, 1 << xfercap_log);

	/* FIXME which i/oat version is i7300? */
#ifdef CONFIG_I7300_IDLE_IOAT_CHANNEL
	if (i7300_idle_platform_probe(NULL, NULL, 1) == 0)
		dma->chancnt--;
#endif
	for (i = 0; i < dma->chancnt; i++) {
		ioat = devm_kzalloc(dev, sizeof(*ioat), GFP_KERNEL);
		if (!ioat)
			break;

		ioat_init_channel(device, &ioat->base, i,
				  device->timer_fn,
				  device->cleanup_tasklet,
				  (unsigned long) ioat);
		ioat->xfercap_log = xfercap_log;
		spin_lock_init(&ioat->ring_lock);
		if (device->reset_hw(&ioat->base)) {
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
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
	struct ioat_chan_common *chan = &ioat->base;
	dma_cookie_t cookie = c->cookie;

	cookie++;
	if (cookie < 0)
		cookie = 1;
	tx->cookie = cookie;
	c->cookie = cookie;
	dev_dbg(to_dev(&ioat->base), "%s: cookie: %d\n", __func__, cookie);

	if (!test_and_set_bit(IOAT_COMPLETION_PENDING, &chan->state))
		mod_timer(&chan->timer, jiffies + COMPLETION_TIMEOUT);
	ioat2_update_pending(ioat);
	spin_unlock_bh(&ioat->ring_lock);

	return cookie;
}

static struct ioat_ring_ent *ioat2_alloc_ring_ent(struct dma_chan *chan, gfp_t flags)
{
	struct ioat_dma_descriptor *hw;
	struct ioat_ring_ent *desc;
	struct ioatdma_device *dma;
	dma_addr_t phys;

	dma = to_ioatdma_device(chan->device);
	hw = pci_pool_alloc(dma->dma_pool, flags, &phys);
	if (!hw)
		return NULL;
	memset(hw, 0, sizeof(*hw));

	desc = kmem_cache_alloc(ioat2_cache, flags);
	if (!desc) {
		pci_pool_free(dma->dma_pool, hw, phys);
		return NULL;
	}
	memset(desc, 0, sizeof(*desc));

	dma_async_tx_descriptor_init(&desc->txd, chan);
	desc->txd.tx_submit = ioat2_tx_submit_unlock;
	desc->hw = hw;
	desc->txd.phys = phys;
	return desc;
}

static void ioat2_free_ring_ent(struct ioat_ring_ent *desc, struct dma_chan *chan)
{
	struct ioatdma_device *dma;

	dma = to_ioatdma_device(chan->device);
	pci_pool_free(dma->dma_pool, desc->hw, desc->txd.phys);
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

/* ioat2_alloc_chan_resources - allocate/initialize ioat2 descriptor ring
 * @chan: channel to be initialized
 */
int ioat2_alloc_chan_resources(struct dma_chan *c)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
	struct ioat_chan_common *chan = &ioat->base;
	struct ioat_ring_ent **ring;
	int order;

	/* have we already been set up? */
	if (ioat->ring)
		return 1 << ioat->alloc_order;

	/* Setup register to interrupt and write completion status on error */
	writew(IOAT_CHANCTRL_RUN, chan->reg_base + IOAT_CHANCTRL_OFFSET);

	/* allocate a completion writeback area */
	/* doing 2 32bit writes to mmio since 1 64b write doesn't work */
	chan->completion = pci_pool_alloc(chan->device->completion_pool,
					  GFP_KERNEL, &chan->completion_dma);
	if (!chan->completion)
		return -ENOMEM;

	memset(chan->completion, 0, sizeof(*chan->completion));
	writel(((u64) chan->completion_dma) & 0x00000000FFFFFFFF,
	       chan->reg_base + IOAT_CHANCMP_OFFSET_LOW);
	writel(((u64) chan->completion_dma) >> 32,
	       chan->reg_base + IOAT_CHANCMP_OFFSET_HIGH);

	order = ioat_get_alloc_order();
	ring = ioat2_alloc_ring(c, order, GFP_KERNEL);
	if (!ring)
		return -ENOMEM;

	spin_lock_bh(&ioat->ring_lock);
	ioat->ring = ring;
	ioat->head = 0;
	ioat->issued = 0;
	ioat->tail = 0;
	ioat->pending = 0;
	ioat->alloc_order = order;
	spin_unlock_bh(&ioat->ring_lock);

	tasklet_enable(&chan->cleanup_task);
	ioat2_start_null_desc(ioat);

	return 1 << ioat->alloc_order;
}

bool reshape_ring(struct ioat2_dma_chan *ioat, int order)
{
	/* reshape differs from normal ring allocation in that we want
	 * to allocate a new software ring while only
	 * extending/truncating the hardware ring
	 */
	struct ioat_chan_common *chan = &ioat->base;
	struct dma_chan *c = &chan->common;
	const u16 curr_size = ioat2_ring_mask(ioat) + 1;
	const u16 active = ioat2_ring_active(ioat);
	const u16 new_size = 1 << order;
	struct ioat_ring_ent **ring;
	u16 i;

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
			u16 curr_idx = (ioat->tail+i) & (curr_size-1);
			u16 new_idx = (ioat->tail+i) & (new_size-1);

			ring[new_idx] = ioat->ring[curr_idx];
			set_desc_id(ring[new_idx], new_idx);
		}

		/* add new descriptors to the ring */
		for (i = curr_size; i < new_size; i++) {
			u16 new_idx = (ioat->tail+i) & (new_size-1);

			ring[new_idx] = ioat2_alloc_ring_ent(c, GFP_NOWAIT);
			if (!ring[new_idx]) {
				while (i--) {
					u16 new_idx = (ioat->tail+i) & (new_size-1);

					ioat2_free_ring_ent(ring[new_idx], c);
				}
				kfree(ring);
				return false;
			}
			set_desc_id(ring[new_idx], new_idx);
		}

		/* hw link new descriptors */
		for (i = curr_size-1; i < new_size; i++) {
			u16 new_idx = (ioat->tail+i) & (new_size-1);
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
			u16 curr_idx = (ioat->tail+i) & (curr_size-1);
			u16 new_idx = (ioat->tail+i) & (new_size-1);

			ring[new_idx] = ioat->ring[curr_idx];
			set_desc_id(ring[new_idx], new_idx);
		}

		/* free deleted descriptors */
		for (i = new_size; i < curr_size; i++) {
			struct ioat_ring_ent *ent;

			ent = ioat2_get_ring_ent(ioat, ioat->tail+i);
			ioat2_free_ring_ent(ent, c);
		}

		/* fix up hardware ring */
		hw = ring[(ioat->tail+new_size-1) & (new_size-1)]->hw;
		next = ring[(ioat->tail+new_size) & (new_size-1)];
		hw->next = next->txd.phys;
	}

	dev_dbg(to_dev(chan), "%s: allocated %d descriptors\n",
		__func__, new_size);

	kfree(ioat->ring);
	ioat->ring = ring;
	ioat->alloc_order = order;

	return true;
}

/**
 * ioat2_alloc_and_lock - common descriptor alloc boilerplate for ioat2,3 ops
 * @idx: gets starting descriptor index on successful allocation
 * @ioat: ioat2,3 channel (ring) to operate on
 * @num_descs: allocation length
 */
int ioat2_alloc_and_lock(u16 *idx, struct ioat2_dma_chan *ioat, int num_descs)
{
	struct ioat_chan_common *chan = &ioat->base;

	spin_lock_bh(&ioat->ring_lock);
	/* never allow the last descriptor to be consumed, we need at
	 * least one free at all times to allow for on-the-fly ring
	 * resizing.
	 */
	while (unlikely(ioat2_ring_space(ioat) <= num_descs)) {
		if (reshape_ring(ioat, ioat->alloc_order + 1) &&
		    ioat2_ring_space(ioat) > num_descs)
				break;

		if (printk_ratelimit())
			dev_dbg(to_dev(chan),
				"%s: ring full! num_descs: %d (%x:%x:%x)\n",
				__func__, num_descs, ioat->head, ioat->tail,
				ioat->issued);
		spin_unlock_bh(&ioat->ring_lock);

		/* progress reclaim in the allocation failure case we
		 * may be called under bh_disabled so we need to trigger
		 * the timer event directly
		 */
		spin_lock_bh(&chan->cleanup_lock);
		if (jiffies > chan->timer.expires &&
		    timer_pending(&chan->timer)) {
			struct ioatdma_device *device = chan->device;

			mod_timer(&chan->timer, jiffies + COMPLETION_TIMEOUT);
			spin_unlock_bh(&chan->cleanup_lock);
			device->timer_fn((unsigned long) ioat);
		} else
			spin_unlock_bh(&chan->cleanup_lock);
		return -ENOMEM;
	}

	dev_dbg(to_dev(chan), "%s: num_descs: %d (%x:%x:%x)\n",
		__func__, num_descs, ioat->head, ioat->tail, ioat->issued);

	*idx = ioat2_desc_alloc(ioat, num_descs);
	return 0;  /* with ioat->ring_lock held */
}

struct dma_async_tx_descriptor *
ioat2_dma_prep_memcpy_lock(struct dma_chan *c, dma_addr_t dma_dest,
			   dma_addr_t dma_src, size_t len, unsigned long flags)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
	struct ioat_dma_descriptor *hw;
	struct ioat_ring_ent *desc;
	dma_addr_t dst = dma_dest;
	dma_addr_t src = dma_src;
	size_t total_len = len;
	int num_descs;
	u16 idx;
	int i;

	num_descs = ioat2_xferlen_to_descs(ioat, len);
	if (likely(num_descs) &&
	    ioat2_alloc_and_lock(&idx, ioat, num_descs) == 0)
		/* pass */;
	else
		return NULL;
	i = 0;
	do {
		size_t copy = min_t(size_t, len, 1 << ioat->xfercap_log);

		desc = ioat2_get_ring_ent(ioat, idx + i);
		hw = desc->hw;

		hw->size = copy;
		hw->ctl = 0;
		hw->src_addr = src;
		hw->dst_addr = dst;

		len -= copy;
		dst += copy;
		src += copy;
		dump_desc_dbg(ioat, desc);
	} while (++i < num_descs);

	desc->txd.flags = flags;
	desc->len = total_len;
	hw->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
	hw->ctl_f.fence = !!(flags & DMA_PREP_FENCE);
	hw->ctl_f.compl_write = 1;
	dump_desc_dbg(ioat, desc);
	/* we leave the channel locked to ensure in order submission */

	return &desc->txd;
}

/**
 * ioat2_free_chan_resources - release all the descriptors
 * @chan: the channel to be cleaned
 */
void ioat2_free_chan_resources(struct dma_chan *c)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
	struct ioat_chan_common *chan = &ioat->base;
	struct ioatdma_device *device = chan->device;
	struct ioat_ring_ent *desc;
	const u16 total_descs = 1 << ioat->alloc_order;
	int descs;
	int i;

	/* Before freeing channel resources first check
	 * if they have been previously allocated for this channel.
	 */
	if (!ioat->ring)
		return;

	tasklet_disable(&chan->cleanup_task);
	del_timer_sync(&chan->timer);
	device->cleanup_tasklet((unsigned long) ioat);
	device->reset_hw(chan);

	spin_lock_bh(&ioat->ring_lock);
	descs = ioat2_ring_space(ioat);
	dev_dbg(to_dev(chan), "freeing %d idle descriptors\n", descs);
	for (i = 0; i < descs; i++) {
		desc = ioat2_get_ring_ent(ioat, ioat->head + i);
		ioat2_free_ring_ent(desc, c);
	}

	if (descs < total_descs)
		dev_err(to_dev(chan), "Freeing %d in use descriptors!\n",
			total_descs - descs);

	for (i = 0; i < total_descs - descs; i++) {
		desc = ioat2_get_ring_ent(ioat, ioat->tail + i);
		dump_desc_dbg(ioat, desc);
		ioat2_free_ring_ent(desc, c);
	}

	kfree(ioat->ring);
	ioat->ring = NULL;
	ioat->alloc_order = 0;
	pci_pool_free(device->completion_pool, chan->completion,
		      chan->completion_dma);
	spin_unlock_bh(&ioat->ring_lock);

	chan->last_completion = 0;
	chan->completion_dma = 0;
	ioat->pending = 0;
	ioat->dmacount = 0;
}

enum dma_status
ioat2_is_complete(struct dma_chan *c, dma_cookie_t cookie,
		     dma_cookie_t *done, dma_cookie_t *used)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
	struct ioatdma_device *device = ioat->base.device;

	if (ioat_is_complete(c, cookie, done, used) == DMA_SUCCESS)
		return DMA_SUCCESS;

	device->cleanup_tasklet((unsigned long) ioat);

	return ioat_is_complete(c, cookie, done, used);
}

static ssize_t ring_size_show(struct dma_chan *c, char *page)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);

	return sprintf(page, "%d\n", (1 << ioat->alloc_order) & ~1);
}
static struct ioat_sysfs_entry ring_size_attr = __ATTR_RO(ring_size);

static ssize_t ring_active_show(struct dma_chan *c, char *page)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);

	/* ...taken outside the lock, no need to be precise */
	return sprintf(page, "%d\n", ioat2_ring_active(ioat));
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

int __devinit ioat2_dma_probe(struct ioatdma_device *device, int dca)
{
	struct pci_dev *pdev = device->pdev;
	struct dma_device *dma;
	struct dma_chan *c;
	struct ioat_chan_common *chan;
	int err;

	device->enumerate_channels = ioat2_enumerate_channels;
	device->reset_hw = ioat2_reset_hw;
	device->cleanup_tasklet = ioat2_cleanup_tasklet;
	device->timer_fn = ioat2_timer_event;
	device->self_test = ioat_dma_self_test;
	dma = &device->common;
	dma->device_prep_dma_memcpy = ioat2_dma_prep_memcpy_lock;
	dma->device_issue_pending = ioat2_issue_pending;
	dma->device_alloc_chan_resources = ioat2_alloc_chan_resources;
	dma->device_free_chan_resources = ioat2_free_chan_resources;
	dma->device_is_tx_complete = ioat2_is_complete;

	err = ioat_probe(device);
	if (err)
		return err;
	ioat_set_tcp_copy_break(2048);

	list_for_each_entry(c, &dma->channels, device_node) {
		chan = to_chan_common(c);
		writel(IOAT_DCACTRL_CMPL_WRITE_ENABLE | IOAT_DMA_DCA_ANY_CPU,
		       chan->reg_base + IOAT_DCACTRL_OFFSET);
	}

	err = ioat_register(device);
	if (err)
		return err;

	ioat_kobject_add(device, &ioat2_ktype);

	if (dca)
		device->dca = ioat2_dca_init(pdev, device->reg_base);

	return err;
}
