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

static int ioat_ring_alloc_order = 8;
module_param(ioat_ring_alloc_order, int, 0644);
MODULE_PARM_DESC(ioat_ring_alloc_order,
		 "ioat2+: allocate 2^n descriptors per channel (default: n=8)");

static void __ioat2_issue_pending(struct ioat2_dma_chan *ioat)
{
	void * __iomem reg_base = ioat->base.reg_base;

	ioat->pending = 0;
	ioat->dmacount += ioat2_ring_pending(ioat);
	ioat->issued = ioat->head;
	/* make descriptor updates globally visible before notifying channel */
	wmb();
	writew(ioat->dmacount, reg_base + IOAT_CHAN_DMACOUNT_OFFSET);

}

static void ioat2_issue_pending(struct dma_chan *chan)
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
	void __iomem *reg_base = ioat->base.reg_base;
	struct ioat_ring_ent *desc;
	struct ioat_dma_descriptor *hw;
	int idx;

	if (ioat2_ring_space(ioat) < 1) {
		dev_err(to_dev(&ioat->base),
			"Unable to start null desc - ring full\n");
		return;
	}

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
	writel(((u64) desc->txd.phys) & 0x00000000FFFFFFFF,
	       reg_base + IOAT2_CHAINADDR_OFFSET_LOW);
	writel(((u64) desc->txd.phys) >> 32,
	       reg_base + IOAT2_CHAINADDR_OFFSET_HIGH);
	__ioat2_issue_pending(ioat);
}

static void ioat2_start_null_desc(struct ioat2_dma_chan *ioat)
{
	spin_lock_bh(&ioat->ring_lock);
	__ioat2_start_null_desc(ioat);
	spin_unlock_bh(&ioat->ring_lock);
}

static void ioat2_cleanup(struct ioat2_dma_chan *ioat);

/**
 * ioat2_reset_part2 - reinit the channel after a reset
 */
static void ioat2_reset_part2(struct work_struct *work)
{
	struct ioat_chan_common *chan;
	struct ioat2_dma_chan *ioat;

	chan = container_of(work, struct ioat_chan_common, work.work);
	ioat = container_of(chan, struct ioat2_dma_chan, base);

	/* ensure that ->tail points to the stalled descriptor
	 * (ioat->pending is set to 2 at this point so no new
	 * descriptors will be issued while we perform this cleanup)
	 */
	ioat2_cleanup(ioat);

	spin_lock_bh(&chan->cleanup_lock);
	spin_lock_bh(&ioat->ring_lock);

	/* set the tail to be re-issued */
	ioat->issued = ioat->tail;
	ioat->dmacount = 0;

	if (ioat2_ring_pending(ioat)) {
		struct ioat_ring_ent *desc;

		desc = ioat2_get_ring_ent(ioat, ioat->tail);
		writel(((u64) desc->txd.phys) & 0x00000000FFFFFFFF,
		       chan->reg_base + IOAT2_CHAINADDR_OFFSET_LOW);
		writel(((u64) desc->txd.phys) >> 32,
		       chan->reg_base + IOAT2_CHAINADDR_OFFSET_HIGH);
		__ioat2_issue_pending(ioat);
	} else
		__ioat2_start_null_desc(ioat);

	spin_unlock_bh(&ioat->ring_lock);
	spin_unlock_bh(&chan->cleanup_lock);

	dev_info(to_dev(chan),
		 "chan%d reset - %d descs waiting, %d total desc\n",
		 chan_num(chan), ioat->dmacount, 1 << ioat->alloc_order);
}

/**
 * ioat2_reset_channel - restart a channel
 * @ioat: IOAT DMA channel handle
 */
static void ioat2_reset_channel(struct ioat2_dma_chan *ioat)
{
	u32 chansts, chanerr;
	struct ioat_chan_common *chan = &ioat->base;
	u16 active;

	spin_lock_bh(&ioat->ring_lock);
	active = ioat2_ring_active(ioat);
	spin_unlock_bh(&ioat->ring_lock);
	if (!active)
		return;

	chanerr = readl(chan->reg_base + IOAT_CHANERR_OFFSET);
	chansts = (chan->completion_virt->low
					& IOAT_CHANSTS_DMA_TRANSFER_STATUS);
	if (chanerr) {
		dev_err(to_dev(chan),
			"chan%d, CHANSTS = 0x%08x CHANERR = 0x%04x, clearing\n",
			chan_num(chan), chansts, chanerr);
		writel(chanerr, chan->reg_base + IOAT_CHANERR_OFFSET);
	}

	spin_lock_bh(&ioat->ring_lock);
	ioat->pending = 2;
	writeb(IOAT_CHANCMD_RESET,
	       chan->reg_base
	       + IOAT_CHANCMD_OFFSET(chan->device->version));
	spin_unlock_bh(&ioat->ring_lock);
	schedule_delayed_work(&chan->work, RESET_DELAY);
}

/**
 * ioat2_chan_watchdog - watch for stuck channels
 */
static void ioat2_chan_watchdog(struct work_struct *work)
{
	struct ioatdma_device *device =
		container_of(work, struct ioatdma_device, work.work);
	struct ioat2_dma_chan *ioat;
	struct ioat_chan_common *chan;
	u16 active;
	int i;

	for (i = 0; i < device->common.chancnt; i++) {
		chan = ioat_chan_by_index(device, i);
		ioat = container_of(chan, struct ioat2_dma_chan, base);

		/*
		 * for version 2.0 if there are descriptors yet to be processed
		 * and the last completed hasn't changed since the last watchdog
		 *      if they haven't hit the pending level
		 *          issue the pending to push them through
		 *      else
		 *          try resetting the channel
		 */
		spin_lock_bh(&ioat->ring_lock);
		active = ioat2_ring_active(ioat);
		spin_unlock_bh(&ioat->ring_lock);

		if (active &&
		    chan->last_completion &&
		    chan->last_completion == chan->watchdog_completion) {

			if (ioat->pending == 1)
				ioat2_issue_pending(&chan->common);
			else {
				ioat2_reset_channel(ioat);
				chan->watchdog_completion = 0;
			}
		} else {
			chan->last_compl_desc_addr_hw = 0;
			chan->watchdog_completion = chan->last_completion;
		}
		chan->watchdog_last_tcp_cookie = chan->watchdog_tcp_cookie;
	}
	schedule_delayed_work(&device->work, WATCHDOG_DELAY);
}

/**
 * ioat2_cleanup - clean finished descriptors (advance tail pointer)
 * @chan: ioat channel to be cleaned up
 */
static void ioat2_cleanup(struct ioat2_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;
	unsigned long phys_complete;
	struct ioat_ring_ent *desc;
	bool seen_current = false;
	u16 active;
	int i;
	struct dma_async_tx_descriptor *tx;

	prefetch(chan->completion_virt);

	spin_lock_bh(&chan->cleanup_lock);
	phys_complete = ioat_get_current_completion(chan);
	if (phys_complete == chan->last_completion) {
		spin_unlock_bh(&chan->cleanup_lock);
		/*
		 * perhaps we're stuck so hard that the watchdog can't go off?
		 * try to catch it after WATCHDOG_DELAY seconds
		 */
		if (chan->device->version < IOAT_VER_3_0) {
			unsigned long tmo;

			tmo = chan->last_completion_time + HZ*WATCHDOG_DELAY;
			if (time_after(jiffies, tmo)) {
				ioat2_chan_watchdog(&(chan->device->work.work));
				chan->last_completion_time = jiffies;
			}
		}
		return;
	}
	chan->last_completion_time = jiffies;

	spin_lock_bh(&ioat->ring_lock);

	active = ioat2_ring_active(ioat);
	for (i = 0; i < active && !seen_current; i++) {
		prefetch(ioat2_get_ring_ent(ioat, ioat->tail + i + 1));
		desc = ioat2_get_ring_ent(ioat, ioat->tail + i);
		tx = &desc->txd;
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
	spin_unlock_bh(&ioat->ring_lock);

	chan->last_completion = phys_complete;

	spin_unlock_bh(&chan->cleanup_lock);
}

static void ioat2_cleanup_tasklet(unsigned long data)
{
	struct ioat2_dma_chan *ioat = (void *) data;

	ioat2_cleanup(ioat);
	writew(IOAT_CHANCTRL_INT_DISABLE,
	       ioat->base.reg_base + IOAT_CHANCTRL_OFFSET);
}

/**
 * ioat2_enumerate_channels - find and initialize the device's channels
 * @device: the device to be enumerated
 */
static int ioat2_enumerate_channels(struct ioatdma_device *device)
{
	struct ioat2_dma_chan *ioat;
	struct device *dev = &device->pdev->dev;
	struct dma_device *dma = &device->common;
	u8 xfercap_log;
	int i;

	INIT_LIST_HEAD(&dma->channels);
	dma->chancnt = readb(device->reg_base + IOAT_CHANCNT_OFFSET);
	xfercap_log = readb(device->reg_base + IOAT_XFERCAP_OFFSET);
	if (xfercap_log == 0)
		return 0;

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
				  ioat2_reset_part2,
				  ioat2_cleanup_tasklet,
				  (unsigned long) ioat);
		ioat->xfercap_log = xfercap_log;
		spin_lock_init(&ioat->ring_lock);
	}
	dma->chancnt = i;
	return i;
}

static dma_cookie_t ioat2_tx_submit_unlock(struct dma_async_tx_descriptor *tx)
{
	struct dma_chan *c = tx->chan;
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
	dma_cookie_t cookie = c->cookie;

	cookie++;
	if (cookie < 0)
		cookie = 1;
	tx->cookie = cookie;
	c->cookie = cookie;
	ioat2_update_pending(ioat);
	spin_unlock_bh(&ioat->ring_lock);

	return cookie;
}

static struct ioat_ring_ent *ioat2_alloc_ring_ent(struct dma_chan *chan)
{
	struct ioat_dma_descriptor *hw;
	struct ioat_ring_ent *desc;
	struct ioatdma_device *dma;
	dma_addr_t phys;

	dma = to_ioatdma_device(chan->device);
	hw = pci_pool_alloc(dma->dma_pool, GFP_KERNEL, &phys);
	if (!hw)
		return NULL;
	memset(hw, 0, sizeof(*hw));

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc) {
		pci_pool_free(dma->dma_pool, hw, phys);
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
	struct ioatdma_device *dma;

	dma = to_ioatdma_device(chan->device);
	pci_pool_free(dma->dma_pool, desc->hw, desc->txd.phys);
	kfree(desc);
}

/* ioat2_alloc_chan_resources - allocate/initialize ioat2 descriptor ring
 * @chan: channel to be initialized
 */
static int ioat2_alloc_chan_resources(struct dma_chan *c)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
	struct ioat_chan_common *chan = &ioat->base;
	struct ioat_ring_ent **ring;
	u16 chanctrl;
	u32 chanerr;
	int descs;
	int i;

	/* have we already been set up? */
	if (ioat->ring)
		return 1 << ioat->alloc_order;

	/* Setup register to interrupt and write completion status on error */
	chanctrl = IOAT_CHANCTRL_ERR_INT_EN | IOAT_CHANCTRL_ANY_ERR_ABORT_EN |
		   IOAT_CHANCTRL_ERR_COMPLETION_EN;
	writew(chanctrl, chan->reg_base + IOAT_CHANCTRL_OFFSET);

	chanerr = readl(chan->reg_base + IOAT_CHANERR_OFFSET);
	if (chanerr) {
		dev_err(to_dev(chan), "CHANERR = %x, clearing\n", chanerr);
		writel(chanerr, chan->reg_base + IOAT_CHANERR_OFFSET);
	}

	/* allocate a completion writeback area */
	/* doing 2 32bit writes to mmio since 1 64b write doesn't work */
	chan->completion_virt = pci_pool_alloc(chan->device->completion_pool,
					       GFP_KERNEL,
					       &chan->completion_addr);
	if (!chan->completion_virt)
		return -ENOMEM;

	memset(chan->completion_virt, 0,
	       sizeof(*chan->completion_virt));
	writel(((u64) chan->completion_addr) & 0x00000000FFFFFFFF,
	       chan->reg_base + IOAT_CHANCMP_OFFSET_LOW);
	writel(((u64) chan->completion_addr) >> 32,
	       chan->reg_base + IOAT_CHANCMP_OFFSET_HIGH);

	ioat->alloc_order = ioat_get_alloc_order();
	descs = 1 << ioat->alloc_order;

	/* allocate the array to hold the software ring */
	ring = kcalloc(descs, sizeof(*ring), GFP_KERNEL);
	if (!ring)
		return -ENOMEM;
	for (i = 0; i < descs; i++) {
		ring[i] = ioat2_alloc_ring_ent(c);
		if (!ring[i]) {
			while (i--)
				ioat2_free_ring_ent(ring[i], c);
			kfree(ring);
			return -ENOMEM;
		}
	}

	/* link descs */
	for (i = 0; i < descs-1; i++) {
		struct ioat_ring_ent *next = ring[i+1];
		struct ioat_dma_descriptor *hw = ring[i]->hw;

		hw->next = next->txd.phys;
	}
	ring[i]->hw->next = ring[0]->txd.phys;

	spin_lock_bh(&ioat->ring_lock);
	ioat->ring = ring;
	ioat->head = 0;
	ioat->issued = 0;
	ioat->tail = 0;
	ioat->pending = 0;
	spin_unlock_bh(&ioat->ring_lock);

	tasklet_enable(&chan->cleanup_task);
	ioat2_start_null_desc(ioat);

	return descs;
}

/**
 * ioat2_alloc_and_lock - common descriptor alloc boilerplate for ioat2,3 ops
 * @idx: gets starting descriptor index on successful allocation
 * @ioat: ioat2,3 channel (ring) to operate on
 * @num_descs: allocation length
 */
static int ioat2_alloc_and_lock(u16 *idx, struct ioat2_dma_chan *ioat, int num_descs)
{
	struct ioat_chan_common *chan = &ioat->base;

	spin_lock_bh(&ioat->ring_lock);
	if (unlikely(ioat2_ring_space(ioat) < num_descs)) {
		if (printk_ratelimit())
			dev_dbg(to_dev(chan),
				"%s: ring full! num_descs: %d (%x:%x:%x)\n",
				__func__, num_descs, ioat->head, ioat->tail,
				ioat->issued);
		spin_unlock_bh(&ioat->ring_lock);

		/* do direct reclaim in the allocation failure case */
		ioat2_cleanup(ioat);

		return -ENOMEM;
	}

	dev_dbg(to_dev(chan), "%s: num_descs: %d (%x:%x:%x)\n",
		__func__, num_descs, ioat->head, ioat->tail, ioat->issued);

	*idx = ioat2_desc_alloc(ioat, num_descs);
	return 0;  /* with ioat->ring_lock held */
}

static struct dma_async_tx_descriptor *
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
	for (i = 0; i < num_descs; i++) {
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
	}

	desc->txd.flags = flags;
	desc->len = total_len;
	hw->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
	hw->ctl_f.compl_write = 1;
	/* we leave the channel locked to ensure in order submission */

	return &desc->txd;
}

/**
 * ioat2_free_chan_resources - release all the descriptors
 * @chan: the channel to be cleaned
 */
static void ioat2_free_chan_resources(struct dma_chan *c)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
	struct ioat_chan_common *chan = &ioat->base;
	struct ioatdma_device *ioatdma_device = chan->device;
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
	ioat2_cleanup(ioat);

	/* Delay 100ms after reset to allow internal DMA logic to quiesce
	 * before removing DMA descriptor resources.
	 */
	writeb(IOAT_CHANCMD_RESET,
	       chan->reg_base + IOAT_CHANCMD_OFFSET(chan->device->version));
	mdelay(100);

	spin_lock_bh(&ioat->ring_lock);
	descs = ioat2_ring_space(ioat);
	for (i = 0; i < descs; i++) {
		desc = ioat2_get_ring_ent(ioat, ioat->head + i);
		ioat2_free_ring_ent(desc, c);
	}

	if (descs < total_descs)
		dev_err(to_dev(chan), "Freeing %d in use descriptors!\n",
			total_descs - descs);

	for (i = 0; i < total_descs - descs; i++) {
		desc = ioat2_get_ring_ent(ioat, ioat->tail + i);
		ioat2_free_ring_ent(desc, c);
	}

	kfree(ioat->ring);
	ioat->ring = NULL;
	ioat->alloc_order = 0;
	pci_pool_free(ioatdma_device->completion_pool,
		      chan->completion_virt,
		      chan->completion_addr);
	spin_unlock_bh(&ioat->ring_lock);

	chan->last_completion = 0;
	chan->completion_addr = 0;
	ioat->pending = 0;
	ioat->dmacount = 0;
	chan->watchdog_completion = 0;
	chan->last_compl_desc_addr_hw = 0;
	chan->watchdog_tcp_cookie = 0;
	chan->watchdog_last_tcp_cookie = 0;
}

static enum dma_status
ioat2_is_complete(struct dma_chan *c, dma_cookie_t cookie,
		     dma_cookie_t *done, dma_cookie_t *used)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);

	if (ioat_is_complete(c, cookie, done, used) == DMA_SUCCESS)
		return DMA_SUCCESS;

	ioat2_cleanup(ioat);

	return ioat_is_complete(c, cookie, done, used);
}

int ioat2_dma_probe(struct ioatdma_device *device, int dca)
{
	struct pci_dev *pdev = device->pdev;
	struct dma_device *dma;
	struct dma_chan *c;
	struct ioat_chan_common *chan;
	int err;

	device->enumerate_channels = ioat2_enumerate_channels;
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
	if (dca)
		device->dca = ioat2_dca_init(pdev, device->reg_base);

	INIT_DELAYED_WORK(&device->work, ioat2_chan_watchdog);
	schedule_delayed_work(&device->work, WATCHDOG_DELAY);

	return err;
}

int ioat3_dma_probe(struct ioatdma_device *device, int dca)
{
	struct pci_dev *pdev = device->pdev;
	struct dma_device *dma;
	struct dma_chan *c;
	struct ioat_chan_common *chan;
	int err;
	u16 dev_id;

	device->enumerate_channels = ioat2_enumerate_channels;
	dma = &device->common;
	dma->device_prep_dma_memcpy = ioat2_dma_prep_memcpy_lock;
	dma->device_issue_pending = ioat2_issue_pending;
	dma->device_alloc_chan_resources = ioat2_alloc_chan_resources;
	dma->device_free_chan_resources = ioat2_free_chan_resources;
	dma->device_is_tx_complete = ioat2_is_complete;

	/* -= IOAT ver.3 workarounds =- */
	/* Write CHANERRMSK_INT with 3E07h to mask out the errors
	 * that can cause stability issues for IOAT ver.3
	 */
	pci_write_config_dword(pdev, IOAT_PCI_CHANERRMASK_INT_OFFSET, 0x3e07);

	/* Clear DMAUNCERRSTS Cfg-Reg Parity Error status bit
	 * (workaround for spurious config parity error after restart)
	 */
	pci_read_config_word(pdev, IOAT_PCI_DEVICE_ID_OFFSET, &dev_id);
	if (dev_id == PCI_DEVICE_ID_INTEL_IOAT_TBG0)
		pci_write_config_dword(pdev, IOAT_PCI_DMAUNCERRSTS_OFFSET, 0x10);

	err = ioat_probe(device);
	if (err)
		return err;
	ioat_set_tcp_copy_break(262144);

	list_for_each_entry(c, &dma->channels, device_node) {
		chan = to_chan_common(c);
		writel(IOAT_DMA_DCA_ANY_CPU,
		       chan->reg_base + IOAT_DCACTRL_OFFSET);
	}

	err = ioat_register(device);
	if (err)
		return err;
	if (dca)
		device->dca = ioat3_dca_init(pdev, device->reg_base);

	return err;
}
