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
#include <linux/i7300_idle.h>
#include "dma.h"
#include "registers.h"
#include "hw.h"

#include "../dmaengine.h"

int ioat_pending_level = 4;
module_param(ioat_pending_level, int, 0644);
MODULE_PARM_DESC(ioat_pending_level,
		 "high-water mark for pushing ioat descriptors (default: 4)");

/* internal functions */
static void ioat1_cleanup(struct ioat_dma_chan *ioat);
static void ioat1_dma_start_null_desc(struct ioat_dma_chan *ioat);

/**
 * ioat_dma_do_interrupt - handler used for single vector interrupt mode
 * @irq: interrupt id
 * @data: interrupt data
 */
static irqreturn_t ioat_dma_do_interrupt(int irq, void *data)
{
	struct ioatdma_device *instance = data;
	struct ioat_chan_common *chan;
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
		chan = ioat_chan_by_index(instance, bit);
		if (test_bit(IOAT_RUN, &chan->state))
			tasklet_schedule(&chan->cleanup_task);
	}

	writeb(intrctrl, instance->reg_base + IOAT_INTRCTRL_OFFSET);
	return IRQ_HANDLED;
}

/**
 * ioat_dma_do_interrupt_msix - handler used for vector-per-channel interrupt mode
 * @irq: interrupt id
 * @data: interrupt data
 */
static irqreturn_t ioat_dma_do_interrupt_msix(int irq, void *data)
{
	struct ioat_chan_common *chan = data;

	if (test_bit(IOAT_RUN, &chan->state))
		tasklet_schedule(&chan->cleanup_task);

	return IRQ_HANDLED;
}

/* common channel initialization */
void ioat_init_channel(struct ioatdma_device *device, struct ioat_chan_common *chan, int idx)
{
	struct dma_device *dma = &device->common;
	struct dma_chan *c = &chan->common;
	unsigned long data = (unsigned long) c;

	chan->device = device;
	chan->reg_base = device->reg_base + (0x80 * (idx + 1));
	spin_lock_init(&chan->cleanup_lock);
	chan->common.device = dma;
	dma_cookie_init(&chan->common);
	list_add_tail(&chan->common.device_node, &dma->channels);
	device->idx[idx] = chan;
	init_timer(&chan->timer);
	chan->timer.function = device->timer_fn;
	chan->timer.data = data;
	tasklet_init(&chan->cleanup_task, device->cleanup_fn, data);
}

/**
 * ioat1_dma_enumerate_channels - find and initialize the device's channels
 * @device: the device to be enumerated
 */
static int ioat1_enumerate_channels(struct ioatdma_device *device)
{
	u8 xfercap_scale;
	u32 xfercap;
	int i;
	struct ioat_dma_chan *ioat;
	struct device *dev = &device->pdev->dev;
	struct dma_device *dma = &device->common;

	INIT_LIST_HEAD(&dma->channels);
	dma->chancnt = readb(device->reg_base + IOAT_CHANCNT_OFFSET);
	dma->chancnt &= 0x1f; /* bits [4:0] valid */
	if (dma->chancnt > ARRAY_SIZE(device->idx)) {
		dev_warn(dev, "(%d) exceeds max supported channels (%zu)\n",
			 dma->chancnt, ARRAY_SIZE(device->idx));
		dma->chancnt = ARRAY_SIZE(device->idx);
	}
	xfercap_scale = readb(device->reg_base + IOAT_XFERCAP_OFFSET);
	xfercap_scale &= 0x1f; /* bits [4:0] valid */
	xfercap = (xfercap_scale == 0 ? -1 : (1UL << xfercap_scale));
	dev_dbg(dev, "%s: xfercap = %d\n", __func__, xfercap);

#ifdef  CONFIG_I7300_IDLE_IOAT_CHANNEL
	if (i7300_idle_platform_probe(NULL, NULL, 1) == 0)
		dma->chancnt--;
#endif
	for (i = 0; i < dma->chancnt; i++) {
		ioat = devm_kzalloc(dev, sizeof(*ioat), GFP_KERNEL);
		if (!ioat)
			break;

		ioat_init_channel(device, &ioat->base, i);
		ioat->xfercap = xfercap;
		spin_lock_init(&ioat->desc_lock);
		INIT_LIST_HEAD(&ioat->free_desc);
		INIT_LIST_HEAD(&ioat->used_desc);
	}
	dma->chancnt = i;
	return i;
}

/**
 * ioat_dma_memcpy_issue_pending - push potentially unrecognized appended
 *                                 descriptors to hw
 * @chan: DMA channel handle
 */
static inline void
__ioat1_dma_memcpy_issue_pending(struct ioat_dma_chan *ioat)
{
	void __iomem *reg_base = ioat->base.reg_base;

	dev_dbg(to_dev(&ioat->base), "%s: pending: %d\n",
		__func__, ioat->pending);
	ioat->pending = 0;
	writeb(IOAT_CHANCMD_APPEND, reg_base + IOAT1_CHANCMD_OFFSET);
}

static void ioat1_dma_memcpy_issue_pending(struct dma_chan *chan)
{
	struct ioat_dma_chan *ioat = to_ioat_chan(chan);

	if (ioat->pending > 0) {
		spin_lock_bh(&ioat->desc_lock);
		__ioat1_dma_memcpy_issue_pending(ioat);
		spin_unlock_bh(&ioat->desc_lock);
	}
}

/**
 * ioat1_reset_channel - restart a channel
 * @ioat: IOAT DMA channel handle
 */
static void ioat1_reset_channel(struct ioat_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;
	void __iomem *reg_base = chan->reg_base;
	u32 chansts, chanerr;

	dev_warn(to_dev(chan), "reset\n");
	chanerr = readl(reg_base + IOAT_CHANERR_OFFSET);
	chansts = *chan->completion & IOAT_CHANSTS_STATUS;
	if (chanerr) {
		dev_err(to_dev(chan),
			"chan%d, CHANSTS = 0x%08x CHANERR = 0x%04x, clearing\n",
			chan_num(chan), chansts, chanerr);
		writel(chanerr, reg_base + IOAT_CHANERR_OFFSET);
	}

	/*
	 * whack it upside the head with a reset
	 * and wait for things to settle out.
	 * force the pending count to a really big negative
	 * to make sure no one forces an issue_pending
	 * while we're waiting.
	 */

	ioat->pending = INT_MIN;
	writeb(IOAT_CHANCMD_RESET,
	       reg_base + IOAT_CHANCMD_OFFSET(chan->device->version));
	set_bit(IOAT_RESET_PENDING, &chan->state);
	mod_timer(&chan->timer, jiffies + RESET_DELAY);
}

static dma_cookie_t ioat1_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct dma_chan *c = tx->chan;
	struct ioat_dma_chan *ioat = to_ioat_chan(c);
	struct ioat_desc_sw *desc = tx_to_ioat_desc(tx);
	struct ioat_chan_common *chan = &ioat->base;
	struct ioat_desc_sw *first;
	struct ioat_desc_sw *chain_tail;
	dma_cookie_t cookie;

	spin_lock_bh(&ioat->desc_lock);
	/* cookie incr and addition to used_list must be atomic */
	cookie = dma_cookie_assign(tx);
	dev_dbg(to_dev(&ioat->base), "%s: cookie: %d\n", __func__, cookie);

	/* write address into NextDescriptor field of last desc in chain */
	first = to_ioat_desc(desc->tx_list.next);
	chain_tail = to_ioat_desc(ioat->used_desc.prev);
	/* make descriptor updates globally visible before chaining */
	wmb();
	chain_tail->hw->next = first->txd.phys;
	list_splice_tail_init(&desc->tx_list, &ioat->used_desc);
	dump_desc_dbg(ioat, chain_tail);
	dump_desc_dbg(ioat, first);

	if (!test_and_set_bit(IOAT_COMPLETION_PENDING, &chan->state))
		mod_timer(&chan->timer, jiffies + COMPLETION_TIMEOUT);

	ioat->active += desc->hw->tx_cnt;
	ioat->pending += desc->hw->tx_cnt;
	if (ioat->pending >= ioat_pending_level)
		__ioat1_dma_memcpy_issue_pending(ioat);
	spin_unlock_bh(&ioat->desc_lock);

	return cookie;
}

/**
 * ioat_dma_alloc_descriptor - allocate and return a sw and hw descriptor pair
 * @ioat: the channel supplying the memory pool for the descriptors
 * @flags: allocation flags
 */
static struct ioat_desc_sw *
ioat_dma_alloc_descriptor(struct ioat_dma_chan *ioat, gfp_t flags)
{
	struct ioat_dma_descriptor *desc;
	struct ioat_desc_sw *desc_sw;
	struct ioatdma_device *ioatdma_device;
	dma_addr_t phys;

	ioatdma_device = ioat->base.device;
	desc = pci_pool_alloc(ioatdma_device->dma_pool, flags, &phys);
	if (unlikely(!desc))
		return NULL;

	desc_sw = kzalloc(sizeof(*desc_sw), flags);
	if (unlikely(!desc_sw)) {
		pci_pool_free(ioatdma_device->dma_pool, desc, phys);
		return NULL;
	}

	memset(desc, 0, sizeof(*desc));

	INIT_LIST_HEAD(&desc_sw->tx_list);
	dma_async_tx_descriptor_init(&desc_sw->txd, &ioat->base.common);
	desc_sw->txd.tx_submit = ioat1_tx_submit;
	desc_sw->hw = desc;
	desc_sw->txd.phys = phys;
	set_desc_id(desc_sw, -1);

	return desc_sw;
}

static int ioat_initial_desc_count = 256;
module_param(ioat_initial_desc_count, int, 0644);
MODULE_PARM_DESC(ioat_initial_desc_count,
		 "ioat1: initial descriptors per channel (default: 256)");
/**
 * ioat1_dma_alloc_chan_resources - returns the number of allocated descriptors
 * @chan: the channel to be filled out
 */
static int ioat1_dma_alloc_chan_resources(struct dma_chan *c)
{
	struct ioat_dma_chan *ioat = to_ioat_chan(c);
	struct ioat_chan_common *chan = &ioat->base;
	struct ioat_desc_sw *desc;
	u32 chanerr;
	int i;
	LIST_HEAD(tmp_list);

	/* have we already been set up? */
	if (!list_empty(&ioat->free_desc))
		return ioat->desccount;

	/* Setup register to interrupt and write completion status on error */
	writew(IOAT_CHANCTRL_RUN, chan->reg_base + IOAT_CHANCTRL_OFFSET);

	chanerr = readl(chan->reg_base + IOAT_CHANERR_OFFSET);
	if (chanerr) {
		dev_err(to_dev(chan), "CHANERR = %x, clearing\n", chanerr);
		writel(chanerr, chan->reg_base + IOAT_CHANERR_OFFSET);
	}

	/* Allocate descriptors */
	for (i = 0; i < ioat_initial_desc_count; i++) {
		desc = ioat_dma_alloc_descriptor(ioat, GFP_KERNEL);
		if (!desc) {
			dev_err(to_dev(chan), "Only %d initial descriptors\n", i);
			break;
		}
		set_desc_id(desc, i);
		list_add_tail(&desc->node, &tmp_list);
	}
	spin_lock_bh(&ioat->desc_lock);
	ioat->desccount = i;
	list_splice(&tmp_list, &ioat->free_desc);
	spin_unlock_bh(&ioat->desc_lock);

	/* allocate a completion writeback area */
	/* doing 2 32bit writes to mmio since 1 64b write doesn't work */
	chan->completion = pci_pool_alloc(chan->device->completion_pool,
					  GFP_KERNEL, &chan->completion_dma);
	memset(chan->completion, 0, sizeof(*chan->completion));
	writel(((u64) chan->completion_dma) & 0x00000000FFFFFFFF,
	       chan->reg_base + IOAT_CHANCMP_OFFSET_LOW);
	writel(((u64) chan->completion_dma) >> 32,
	       chan->reg_base + IOAT_CHANCMP_OFFSET_HIGH);

	set_bit(IOAT_RUN, &chan->state);
	ioat1_dma_start_null_desc(ioat);  /* give chain to dma device */
	dev_dbg(to_dev(chan), "%s: allocated %d descriptors\n",
		__func__, ioat->desccount);
	return ioat->desccount;
}

void ioat_stop(struct ioat_chan_common *chan)
{
	struct ioatdma_device *device = chan->device;
	struct pci_dev *pdev = device->pdev;
	int chan_id = chan_num(chan);
	struct msix_entry *msix;

	/* 1/ stop irq from firing tasklets
	 * 2/ stop the tasklet from re-arming irqs
	 */
	clear_bit(IOAT_RUN, &chan->state);

	/* flush inflight interrupts */
	switch (device->irq_mode) {
	case IOAT_MSIX:
		msix = &device->msix_entries[chan_id];
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
	del_timer_sync(&chan->timer);

	/* flush inflight tasklet runs */
	tasklet_kill(&chan->cleanup_task);

	/* final cleanup now that everything is quiesced and can't re-arm */
	device->cleanup_fn((unsigned long) &chan->common);
}

/**
 * ioat1_dma_free_chan_resources - release all the descriptors
 * @chan: the channel to be cleaned
 */
static void ioat1_dma_free_chan_resources(struct dma_chan *c)
{
	struct ioat_dma_chan *ioat = to_ioat_chan(c);
	struct ioat_chan_common *chan = &ioat->base;
	struct ioatdma_device *ioatdma_device = chan->device;
	struct ioat_desc_sw *desc, *_desc;
	int in_use_descs = 0;

	/* Before freeing channel resources first check
	 * if they have been previously allocated for this channel.
	 */
	if (ioat->desccount == 0)
		return;

	ioat_stop(chan);

	/* Delay 100ms after reset to allow internal DMA logic to quiesce
	 * before removing DMA descriptor resources.
	 */
	writeb(IOAT_CHANCMD_RESET,
	       chan->reg_base + IOAT_CHANCMD_OFFSET(chan->device->version));
	mdelay(100);

	spin_lock_bh(&ioat->desc_lock);
	list_for_each_entry_safe(desc, _desc, &ioat->used_desc, node) {
		dev_dbg(to_dev(chan), "%s: freeing %d from used list\n",
			__func__, desc_id(desc));
		dump_desc_dbg(ioat, desc);
		in_use_descs++;
		list_del(&desc->node);
		pci_pool_free(ioatdma_device->dma_pool, desc->hw,
			      desc->txd.phys);
		kfree(desc);
	}
	list_for_each_entry_safe(desc, _desc,
				 &ioat->free_desc, node) {
		list_del(&desc->node);
		pci_pool_free(ioatdma_device->dma_pool, desc->hw,
			      desc->txd.phys);
		kfree(desc);
	}
	spin_unlock_bh(&ioat->desc_lock);

	pci_pool_free(ioatdma_device->completion_pool,
		      chan->completion,
		      chan->completion_dma);

	/* one is ok since we left it on there on purpose */
	if (in_use_descs > 1)
		dev_err(to_dev(chan), "Freeing %d in use descriptors!\n",
			in_use_descs - 1);

	chan->last_completion = 0;
	chan->completion_dma = 0;
	ioat->pending = 0;
	ioat->desccount = 0;
}

/**
 * ioat1_dma_get_next_descriptor - return the next available descriptor
 * @ioat: IOAT DMA channel handle
 *
 * Gets the next descriptor from the chain, and must be called with the
 * channel's desc_lock held.  Allocates more descriptors if the channel
 * has run out.
 */
static struct ioat_desc_sw *
ioat1_dma_get_next_descriptor(struct ioat_dma_chan *ioat)
{
	struct ioat_desc_sw *new;

	if (!list_empty(&ioat->free_desc)) {
		new = to_ioat_desc(ioat->free_desc.next);
		list_del(&new->node);
	} else {
		/* try to get another desc */
		new = ioat_dma_alloc_descriptor(ioat, GFP_ATOMIC);
		if (!new) {
			dev_err(to_dev(&ioat->base), "alloc failed\n");
			return NULL;
		}
	}
	dev_dbg(to_dev(&ioat->base), "%s: allocated: %d\n",
		__func__, desc_id(new));
	prefetch(new->hw);
	return new;
}

static struct dma_async_tx_descriptor *
ioat1_dma_prep_memcpy(struct dma_chan *c, dma_addr_t dma_dest,
		      dma_addr_t dma_src, size_t len, unsigned long flags)
{
	struct ioat_dma_chan *ioat = to_ioat_chan(c);
	struct ioat_desc_sw *desc;
	size_t copy;
	LIST_HEAD(chain);
	dma_addr_t src = dma_src;
	dma_addr_t dest = dma_dest;
	size_t total_len = len;
	struct ioat_dma_descriptor *hw = NULL;
	int tx_cnt = 0;

	spin_lock_bh(&ioat->desc_lock);
	desc = ioat1_dma_get_next_descriptor(ioat);
	do {
		if (!desc)
			break;

		tx_cnt++;
		copy = min_t(size_t, len, ioat->xfercap);

		hw = desc->hw;
		hw->size = copy;
		hw->ctl = 0;
		hw->src_addr = src;
		hw->dst_addr = dest;

		list_add_tail(&desc->node, &chain);

		len -= copy;
		dest += copy;
		src += copy;
		if (len) {
			struct ioat_desc_sw *next;

			async_tx_ack(&desc->txd);
			next = ioat1_dma_get_next_descriptor(ioat);
			hw->next = next ? next->txd.phys : 0;
			dump_desc_dbg(ioat, desc);
			desc = next;
		} else
			hw->next = 0;
	} while (len);

	if (!desc) {
		struct ioat_chan_common *chan = &ioat->base;

		dev_err(to_dev(chan),
			"chan%d - get_next_desc failed\n", chan_num(chan));
		list_splice(&chain, &ioat->free_desc);
		spin_unlock_bh(&ioat->desc_lock);
		return NULL;
	}
	spin_unlock_bh(&ioat->desc_lock);

	desc->txd.flags = flags;
	desc->len = total_len;
	list_splice(&chain, &desc->tx_list);
	hw->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
	hw->ctl_f.compl_write = 1;
	hw->tx_cnt = tx_cnt;
	dump_desc_dbg(ioat, desc);

	return &desc->txd;
}

static void ioat1_cleanup_event(unsigned long data)
{
	struct ioat_dma_chan *ioat = to_ioat_chan((void *) data);
	struct ioat_chan_common *chan = &ioat->base;

	ioat1_cleanup(ioat);
	if (!test_bit(IOAT_RUN, &chan->state))
		return;
	writew(IOAT_CHANCTRL_RUN, ioat->base.reg_base + IOAT_CHANCTRL_OFFSET);
}

dma_addr_t ioat_get_current_completion(struct ioat_chan_common *chan)
{
	dma_addr_t phys_complete;
	u64 completion;

	completion = *chan->completion;
	phys_complete = ioat_chansts_to_addr(completion);

	dev_dbg(to_dev(chan), "%s: phys_complete: %#llx\n", __func__,
		(unsigned long long) phys_complete);

	if (is_ioat_halted(completion)) {
		u32 chanerr = readl(chan->reg_base + IOAT_CHANERR_OFFSET);
		dev_err(to_dev(chan), "Channel halted, chanerr = %x\n",
			chanerr);

		/* TODO do something to salvage the situation */
	}

	return phys_complete;
}

bool ioat_cleanup_preamble(struct ioat_chan_common *chan,
			   dma_addr_t *phys_complete)
{
	*phys_complete = ioat_get_current_completion(chan);
	if (*phys_complete == chan->last_completion)
		return false;
	clear_bit(IOAT_COMPLETION_ACK, &chan->state);
	mod_timer(&chan->timer, jiffies + COMPLETION_TIMEOUT);

	return true;
}

static void __cleanup(struct ioat_dma_chan *ioat, dma_addr_t phys_complete)
{
	struct ioat_chan_common *chan = &ioat->base;
	struct list_head *_desc, *n;
	struct dma_async_tx_descriptor *tx;

	dev_dbg(to_dev(chan), "%s: phys_complete: %llx\n",
		 __func__, (unsigned long long) phys_complete);
	list_for_each_safe(_desc, n, &ioat->used_desc) {
		struct ioat_desc_sw *desc;

		prefetch(n);
		desc = list_entry(_desc, typeof(*desc), node);
		tx = &desc->txd;
		/*
		 * Incoming DMA requests may use multiple descriptors,
		 * due to exceeding xfercap, perhaps. If so, only the
		 * last one will have a cookie, and require unmapping.
		 */
		dump_desc_dbg(ioat, desc);
		if (tx->cookie) {
			dma_cookie_complete(tx);
			dma_descriptor_unmap(tx);
			ioat->active -= desc->hw->tx_cnt;
			if (tx->callback) {
				tx->callback(tx->callback_param);
				tx->callback = NULL;
			}
		}

		if (tx->phys != phys_complete) {
			/*
			 * a completed entry, but not the last, so clean
			 * up if the client is done with the descriptor
			 */
			if (async_tx_test_ack(tx))
				list_move_tail(&desc->node, &ioat->free_desc);
		} else {
			/*
			 * last used desc. Do not remove, so we can
			 * append from it.
			 */

			/* if nothing else is pending, cancel the
			 * completion timeout
			 */
			if (n == &ioat->used_desc) {
				dev_dbg(to_dev(chan),
					"%s cancel completion timeout\n",
					__func__);
				clear_bit(IOAT_COMPLETION_PENDING, &chan->state);
			}

			/* TODO check status bits? */
			break;
		}
	}

	chan->last_completion = phys_complete;
}

/**
 * ioat1_cleanup - cleanup up finished descriptors
 * @chan: ioat channel to be cleaned up
 *
 * To prevent lock contention we defer cleanup when the locks are
 * contended with a terminal timeout that forces cleanup and catches
 * completion notification errors.
 */
static void ioat1_cleanup(struct ioat_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;
	dma_addr_t phys_complete;

	prefetch(chan->completion);

	if (!spin_trylock_bh(&chan->cleanup_lock))
		return;

	if (!ioat_cleanup_preamble(chan, &phys_complete)) {
		spin_unlock_bh(&chan->cleanup_lock);
		return;
	}

	if (!spin_trylock_bh(&ioat->desc_lock)) {
		spin_unlock_bh(&chan->cleanup_lock);
		return;
	}

	__cleanup(ioat, phys_complete);

	spin_unlock_bh(&ioat->desc_lock);
	spin_unlock_bh(&chan->cleanup_lock);
}

static void ioat1_timer_event(unsigned long data)
{
	struct ioat_dma_chan *ioat = to_ioat_chan((void *) data);
	struct ioat_chan_common *chan = &ioat->base;

	dev_dbg(to_dev(chan), "%s: state: %lx\n", __func__, chan->state);

	spin_lock_bh(&chan->cleanup_lock);
	if (test_and_clear_bit(IOAT_RESET_PENDING, &chan->state)) {
		struct ioat_desc_sw *desc;

		spin_lock_bh(&ioat->desc_lock);

		/* restart active descriptors */
		desc = to_ioat_desc(ioat->used_desc.prev);
		ioat_set_chainaddr(ioat, desc->txd.phys);
		ioat_start(chan);

		ioat->pending = 0;
		set_bit(IOAT_COMPLETION_PENDING, &chan->state);
		mod_timer(&chan->timer, jiffies + COMPLETION_TIMEOUT);
		spin_unlock_bh(&ioat->desc_lock);
	} else if (test_bit(IOAT_COMPLETION_PENDING, &chan->state)) {
		dma_addr_t phys_complete;

		spin_lock_bh(&ioat->desc_lock);
		/* if we haven't made progress and we have already
		 * acknowledged a pending completion once, then be more
		 * forceful with a restart
		 */
		if (ioat_cleanup_preamble(chan, &phys_complete))
			__cleanup(ioat, phys_complete);
		else if (test_bit(IOAT_COMPLETION_ACK, &chan->state))
			ioat1_reset_channel(ioat);
		else {
			u64 status = ioat_chansts(chan);

			/* manually update the last completion address */
			if (ioat_chansts_to_addr(status) != 0)
				*chan->completion = status;

			set_bit(IOAT_COMPLETION_ACK, &chan->state);
			mod_timer(&chan->timer, jiffies + COMPLETION_TIMEOUT);
		}
		spin_unlock_bh(&ioat->desc_lock);
	}
	spin_unlock_bh(&chan->cleanup_lock);
}

enum dma_status
ioat_dma_tx_status(struct dma_chan *c, dma_cookie_t cookie,
		   struct dma_tx_state *txstate)
{
	struct ioat_chan_common *chan = to_chan_common(c);
	struct ioatdma_device *device = chan->device;
	enum dma_status ret;

	ret = dma_cookie_status(c, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	device->cleanup_fn((unsigned long) c);

	return dma_cookie_status(c, cookie, txstate);
}

static void ioat1_dma_start_null_desc(struct ioat_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;
	struct ioat_desc_sw *desc;
	struct ioat_dma_descriptor *hw;

	spin_lock_bh(&ioat->desc_lock);

	desc = ioat1_dma_get_next_descriptor(ioat);

	if (!desc) {
		dev_err(to_dev(chan),
			"Unable to start null desc - get next desc failed\n");
		spin_unlock_bh(&ioat->desc_lock);
		return;
	}

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
	hw->next = 0;
	list_add_tail(&desc->node, &ioat->used_desc);
	dump_desc_dbg(ioat, desc);

	ioat_set_chainaddr(ioat, desc->txd.phys);
	ioat_start(chan);
	spin_unlock_bh(&ioat->desc_lock);
}

/*
 * Perform a IOAT transaction to verify the HW works.
 */
#define IOAT_TEST_SIZE 2000

static void ioat_dma_test_callback(void *dma_async_param)
{
	struct completion *cmp = dma_async_param;

	complete(cmp);
}

/**
 * ioat_dma_self_test - Perform a IOAT transaction to verify the HW works.
 * @device: device to be tested
 */
int ioat_dma_self_test(struct ioatdma_device *device)
{
	int i;
	u8 *src;
	u8 *dest;
	struct dma_device *dma = &device->common;
	struct device *dev = &device->pdev->dev;
	struct dma_chan *dma_chan;
	struct dma_async_tx_descriptor *tx;
	dma_addr_t dma_dest, dma_src;
	dma_cookie_t cookie;
	int err = 0;
	struct completion cmp;
	unsigned long tmo;
	unsigned long flags;

	src = kzalloc(sizeof(u8) * IOAT_TEST_SIZE, GFP_KERNEL);
	if (!src)
		return -ENOMEM;
	dest = kzalloc(sizeof(u8) * IOAT_TEST_SIZE, GFP_KERNEL);
	if (!dest) {
		kfree(src);
		return -ENOMEM;
	}

	/* Fill in src buffer */
	for (i = 0; i < IOAT_TEST_SIZE; i++)
		src[i] = (u8)i;

	/* Start copy, using first DMA channel */
	dma_chan = container_of(dma->channels.next, struct dma_chan,
				device_node);
	if (dma->device_alloc_chan_resources(dma_chan) < 1) {
		dev_err(dev, "selftest cannot allocate chan resource\n");
		err = -ENODEV;
		goto out;
	}

	dma_src = dma_map_single(dev, src, IOAT_TEST_SIZE, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_src)) {
		dev_err(dev, "mapping src buffer failed\n");
		goto free_resources;
	}
	dma_dest = dma_map_single(dev, dest, IOAT_TEST_SIZE, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, dma_dest)) {
		dev_err(dev, "mapping dest buffer failed\n");
		goto unmap_src;
	}
	flags = DMA_PREP_INTERRUPT;
	tx = device->common.device_prep_dma_memcpy(dma_chan, dma_dest, dma_src,
						   IOAT_TEST_SIZE, flags);
	if (!tx) {
		dev_err(dev, "Self-test prep failed, disabling\n");
		err = -ENODEV;
		goto unmap_dma;
	}

	async_tx_ack(tx);
	init_completion(&cmp);
	tx->callback = ioat_dma_test_callback;
	tx->callback_param = &cmp;
	cookie = tx->tx_submit(tx);
	if (cookie < 0) {
		dev_err(dev, "Self-test setup failed, disabling\n");
		err = -ENODEV;
		goto unmap_dma;
	}
	dma->device_issue_pending(dma_chan);

	tmo = wait_for_completion_timeout(&cmp, msecs_to_jiffies(3000));

	if (tmo == 0 ||
	    dma->device_tx_status(dma_chan, cookie, NULL)
					!= DMA_COMPLETE) {
		dev_err(dev, "Self-test copy timed out, disabling\n");
		err = -ENODEV;
		goto unmap_dma;
	}
	if (memcmp(src, dest, IOAT_TEST_SIZE)) {
		dev_err(dev, "Self-test copy failed compare, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}

unmap_dma:
	dma_unmap_single(dev, dma_dest, IOAT_TEST_SIZE, DMA_FROM_DEVICE);
unmap_src:
	dma_unmap_single(dev, dma_src, IOAT_TEST_SIZE, DMA_TO_DEVICE);
free_resources:
	dma->device_free_chan_resources(dma_chan);
out:
	kfree(src);
	kfree(dest);
	return err;
}

static char ioat_interrupt_style[32] = "msix";
module_param_string(ioat_interrupt_style, ioat_interrupt_style,
		    sizeof(ioat_interrupt_style), 0644);
MODULE_PARM_DESC(ioat_interrupt_style,
		 "set ioat interrupt style: msix (default), msi, intx");

/**
 * ioat_dma_setup_interrupts - setup interrupt handler
 * @device: ioat device
 */
int ioat_dma_setup_interrupts(struct ioatdma_device *device)
{
	struct ioat_chan_common *chan;
	struct pci_dev *pdev = device->pdev;
	struct device *dev = &pdev->dev;
	struct msix_entry *msix;
	int i, j, msixcnt;
	int err = -EINVAL;
	u8 intrctrl = 0;

	if (!strcmp(ioat_interrupt_style, "msix"))
		goto msix;
	if (!strcmp(ioat_interrupt_style, "msi"))
		goto msi;
	if (!strcmp(ioat_interrupt_style, "intx"))
		goto intx;
	dev_err(dev, "invalid ioat_interrupt_style %s\n", ioat_interrupt_style);
	goto err_no_irq;

msix:
	/* The number of MSI-X vectors should equal the number of channels */
	msixcnt = device->common.chancnt;
	for (i = 0; i < msixcnt; i++)
		device->msix_entries[i].entry = i;

	err = pci_enable_msix_exact(pdev, device->msix_entries, msixcnt);
	if (err)
		goto msi;

	for (i = 0; i < msixcnt; i++) {
		msix = &device->msix_entries[i];
		chan = ioat_chan_by_index(device, i);
		err = devm_request_irq(dev, msix->vector,
				       ioat_dma_do_interrupt_msix, 0,
				       "ioat-msix", chan);
		if (err) {
			for (j = 0; j < i; j++) {
				msix = &device->msix_entries[j];
				chan = ioat_chan_by_index(device, j);
				devm_free_irq(dev, msix->vector, chan);
			}
			goto msi;
		}
	}
	intrctrl |= IOAT_INTRCTRL_MSIX_VECTOR_CONTROL;
	device->irq_mode = IOAT_MSIX;
	goto done;

msi:
	err = pci_enable_msi(pdev);
	if (err)
		goto intx;

	err = devm_request_irq(dev, pdev->irq, ioat_dma_do_interrupt, 0,
			       "ioat-msi", device);
	if (err) {
		pci_disable_msi(pdev);
		goto intx;
	}
	device->irq_mode = IOAT_MSI;
	goto done;

intx:
	err = devm_request_irq(dev, pdev->irq, ioat_dma_do_interrupt,
			       IRQF_SHARED, "ioat-intx", device);
	if (err)
		goto err_no_irq;

	device->irq_mode = IOAT_INTX;
done:
	if (device->intr_quirk)
		device->intr_quirk(device);
	intrctrl |= IOAT_INTRCTRL_MASTER_INT_EN;
	writeb(intrctrl, device->reg_base + IOAT_INTRCTRL_OFFSET);
	return 0;

err_no_irq:
	/* Disable all interrupt generation */
	writeb(0, device->reg_base + IOAT_INTRCTRL_OFFSET);
	device->irq_mode = IOAT_NOIRQ;
	dev_err(dev, "no usable interrupts\n");
	return err;
}
EXPORT_SYMBOL(ioat_dma_setup_interrupts);

static void ioat_disable_interrupts(struct ioatdma_device *device)
{
	/* Disable all interrupt generation */
	writeb(0, device->reg_base + IOAT_INTRCTRL_OFFSET);
}

int ioat_probe(struct ioatdma_device *device)
{
	int err = -ENODEV;
	struct dma_device *dma = &device->common;
	struct pci_dev *pdev = device->pdev;
	struct device *dev = &pdev->dev;

	/* DMA coherent memory pool for DMA descriptor allocations */
	device->dma_pool = pci_pool_create("dma_desc_pool", pdev,
					   sizeof(struct ioat_dma_descriptor),
					   64, 0);
	if (!device->dma_pool) {
		err = -ENOMEM;
		goto err_dma_pool;
	}

	device->completion_pool = pci_pool_create("completion_pool", pdev,
						  sizeof(u64), SMP_CACHE_BYTES,
						  SMP_CACHE_BYTES);

	if (!device->completion_pool) {
		err = -ENOMEM;
		goto err_completion_pool;
	}

	device->enumerate_channels(device);

	dma_cap_set(DMA_MEMCPY, dma->cap_mask);
	dma->dev = &pdev->dev;

	if (!dma->chancnt) {
		dev_err(dev, "channel enumeration error\n");
		goto err_setup_interrupts;
	}

	err = ioat_dma_setup_interrupts(device);
	if (err)
		goto err_setup_interrupts;

	err = device->self_test(device);
	if (err)
		goto err_self_test;

	return 0;

err_self_test:
	ioat_disable_interrupts(device);
err_setup_interrupts:
	pci_pool_destroy(device->completion_pool);
err_completion_pool:
	pci_pool_destroy(device->dma_pool);
err_dma_pool:
	return err;
}

int ioat_register(struct ioatdma_device *device)
{
	int err = dma_async_device_register(&device->common);

	if (err) {
		ioat_disable_interrupts(device);
		pci_pool_destroy(device->completion_pool);
		pci_pool_destroy(device->dma_pool);
	}

	return err;
}

/* ioat1_intr_quirk - fix up dma ctrl register to enable / disable msi */
static void ioat1_intr_quirk(struct ioatdma_device *device)
{
	struct pci_dev *pdev = device->pdev;
	u32 dmactrl;

	pci_read_config_dword(pdev, IOAT_PCI_DMACTRL_OFFSET, &dmactrl);
	if (pdev->msi_enabled)
		dmactrl |= IOAT_PCI_DMACTRL_MSI_EN;
	else
		dmactrl &= ~IOAT_PCI_DMACTRL_MSI_EN;
	pci_write_config_dword(pdev, IOAT_PCI_DMACTRL_OFFSET, dmactrl);
}

static ssize_t ring_size_show(struct dma_chan *c, char *page)
{
	struct ioat_dma_chan *ioat = to_ioat_chan(c);

	return sprintf(page, "%d\n", ioat->desccount);
}
static struct ioat_sysfs_entry ring_size_attr = __ATTR_RO(ring_size);

static ssize_t ring_active_show(struct dma_chan *c, char *page)
{
	struct ioat_dma_chan *ioat = to_ioat_chan(c);

	return sprintf(page, "%d\n", ioat->active);
}
static struct ioat_sysfs_entry ring_active_attr = __ATTR_RO(ring_active);

static ssize_t cap_show(struct dma_chan *c, char *page)
{
	struct dma_device *dma = c->device;

	return sprintf(page, "copy%s%s%s%s%s\n",
		       dma_has_cap(DMA_PQ, dma->cap_mask) ? " pq" : "",
		       dma_has_cap(DMA_PQ_VAL, dma->cap_mask) ? " pq_val" : "",
		       dma_has_cap(DMA_XOR, dma->cap_mask) ? " xor" : "",
		       dma_has_cap(DMA_XOR_VAL, dma->cap_mask) ? " xor_val" : "",
		       dma_has_cap(DMA_INTERRUPT, dma->cap_mask) ? " intr" : "");

}
struct ioat_sysfs_entry ioat_cap_attr = __ATTR_RO(cap);

static ssize_t version_show(struct dma_chan *c, char *page)
{
	struct dma_device *dma = c->device;
	struct ioatdma_device *device = to_ioatdma_device(dma);

	return sprintf(page, "%d.%d\n",
		       device->version >> 4, device->version & 0xf);
}
struct ioat_sysfs_entry ioat_version_attr = __ATTR_RO(version);

static struct attribute *ioat1_attrs[] = {
	&ring_size_attr.attr,
	&ring_active_attr.attr,
	&ioat_cap_attr.attr,
	&ioat_version_attr.attr,
	NULL,
};

static ssize_t
ioat_attr_show(struct kobject *kobj, struct attribute *attr, char *page)
{
	struct ioat_sysfs_entry *entry;
	struct ioat_chan_common *chan;

	entry = container_of(attr, struct ioat_sysfs_entry, attr);
	chan = container_of(kobj, struct ioat_chan_common, kobj);

	if (!entry->show)
		return -EIO;
	return entry->show(&chan->common, page);
}

const struct sysfs_ops ioat_sysfs_ops = {
	.show	= ioat_attr_show,
};

static struct kobj_type ioat1_ktype = {
	.sysfs_ops = &ioat_sysfs_ops,
	.default_attrs = ioat1_attrs,
};

void ioat_kobject_add(struct ioatdma_device *device, struct kobj_type *type)
{
	struct dma_device *dma = &device->common;
	struct dma_chan *c;

	list_for_each_entry(c, &dma->channels, device_node) {
		struct ioat_chan_common *chan = to_chan_common(c);
		struct kobject *parent = &c->dev->device.kobj;
		int err;

		err = kobject_init_and_add(&chan->kobj, type, parent, "quickdata");
		if (err) {
			dev_warn(to_dev(chan),
				 "sysfs init error (%d), continuing...\n", err);
			kobject_put(&chan->kobj);
			set_bit(IOAT_KOBJ_INIT_FAIL, &chan->state);
		}
	}
}

void ioat_kobject_del(struct ioatdma_device *device)
{
	struct dma_device *dma = &device->common;
	struct dma_chan *c;

	list_for_each_entry(c, &dma->channels, device_node) {
		struct ioat_chan_common *chan = to_chan_common(c);

		if (!test_bit(IOAT_KOBJ_INIT_FAIL, &chan->state)) {
			kobject_del(&chan->kobj);
			kobject_put(&chan->kobj);
		}
	}
}

int ioat1_dma_probe(struct ioatdma_device *device, int dca)
{
	struct pci_dev *pdev = device->pdev;
	struct dma_device *dma;
	int err;

	device->intr_quirk = ioat1_intr_quirk;
	device->enumerate_channels = ioat1_enumerate_channels;
	device->self_test = ioat_dma_self_test;
	device->timer_fn = ioat1_timer_event;
	device->cleanup_fn = ioat1_cleanup_event;
	dma = &device->common;
	dma->device_prep_dma_memcpy = ioat1_dma_prep_memcpy;
	dma->device_issue_pending = ioat1_dma_memcpy_issue_pending;
	dma->device_alloc_chan_resources = ioat1_dma_alloc_chan_resources;
	dma->device_free_chan_resources = ioat1_dma_free_chan_resources;
	dma->device_tx_status = ioat_dma_tx_status;

	err = ioat_probe(device);
	if (err)
		return err;
	err = ioat_register(device);
	if (err)
		return err;
	ioat_kobject_add(device, &ioat1_ktype);

	if (dca)
		device->dca = ioat_dca_init(pdev, device->reg_base);

	return err;
}

void ioat_dma_remove(struct ioatdma_device *device)
{
	struct dma_device *dma = &device->common;

	ioat_disable_interrupts(device);

	ioat_kobject_del(device);

	dma_async_device_unregister(dma);

	pci_pool_destroy(device->dma_pool);
	pci_pool_destroy(device->completion_pool);

	INIT_LIST_HEAD(&dma->channels);
}
