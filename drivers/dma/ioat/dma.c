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
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/workqueue.h>
#include <linux/i7300_idle.h>
#include "dma.h"
#include "registers.h"
#include "hw.h"

static int ioat_pending_level = 4;
module_param(ioat_pending_level, int, 0644);
MODULE_PARM_DESC(ioat_pending_level,
		 "high-water mark for pushing ioat descriptors (default: 4)");

static void ioat_dma_chan_reset_part2(struct work_struct *work);
static void ioat_dma_chan_watchdog(struct work_struct *work);

/* internal functions */
static void ioat_dma_start_null_desc(struct ioat_dma_chan *ioat);
static void ioat_dma_memcpy_cleanup(struct ioat_dma_chan *ioat);

static struct ioat_desc_sw *
ioat1_dma_get_next_descriptor(struct ioat_dma_chan *ioat);
static struct ioat_desc_sw *
ioat2_dma_get_next_descriptor(struct ioat_dma_chan *ioat);

static inline struct ioat_chan_common *
ioat_chan_by_index(struct ioatdma_device *device, int index)
{
	return device->idx[index];
}

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
	for_each_bit(bit, &attnstatus, BITS_PER_LONG) {
		chan = ioat_chan_by_index(instance, bit);
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

	tasklet_schedule(&chan->cleanup_task);

	return IRQ_HANDLED;
}

static void ioat_dma_cleanup_tasklet(unsigned long data);

/**
 * ioat_dma_enumerate_channels - find and initialize the device's channels
 * @device: the device to be enumerated
 */
static int ioat_dma_enumerate_channels(struct ioatdma_device *device)
{
	u8 xfercap_scale;
	u32 xfercap;
	int i;
	struct ioat_chan_common *chan;
	struct ioat_dma_chan *ioat;
	struct device *dev = &device->pdev->dev;
	struct dma_device *dma = &device->common;

	INIT_LIST_HEAD(&dma->channels);
	dma->chancnt = readb(device->reg_base + IOAT_CHANCNT_OFFSET);
	xfercap_scale = readb(device->reg_base + IOAT_XFERCAP_OFFSET);
	xfercap = (xfercap_scale == 0 ? -1 : (1UL << xfercap_scale));

#ifdef  CONFIG_I7300_IDLE_IOAT_CHANNEL
	if (i7300_idle_platform_probe(NULL, NULL, 1) == 0)
		dma->chancnt--;
#endif
	for (i = 0; i < dma->chancnt; i++) {
		ioat = devm_kzalloc(dev, sizeof(*ioat), GFP_KERNEL);
		if (!ioat) {
			dma->chancnt = i;
			break;
		}

		chan = &ioat->base;
		chan->device = device;
		chan->reg_base = device->reg_base + (0x80 * (i + 1));
		ioat->xfercap = xfercap;
		ioat->desccount = 0;
		INIT_DELAYED_WORK(&chan->work, ioat_dma_chan_reset_part2);
		spin_lock_init(&chan->cleanup_lock);
		spin_lock_init(&ioat->desc_lock);
		INIT_LIST_HEAD(&ioat->free_desc);
		INIT_LIST_HEAD(&ioat->used_desc);
		/* This should be made common somewhere in dmaengine.c */
		chan->common.device = &device->common;
		list_add_tail(&chan->common.device_node, &dma->channels);
		device->idx[i] = chan;
		tasklet_init(&chan->cleanup_task,
			     ioat_dma_cleanup_tasklet,
			     (unsigned long) ioat);
		tasklet_disable(&chan->cleanup_task);
	}
	return dma->chancnt;
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

static inline void
__ioat2_dma_memcpy_issue_pending(struct ioat_dma_chan *ioat)
{
	void __iomem *reg_base = ioat->base.reg_base;

	ioat->pending = 0;
	writew(ioat->dmacount, reg_base + IOAT_CHAN_DMACOUNT_OFFSET);
}

static void ioat2_dma_memcpy_issue_pending(struct dma_chan *chan)
{
	struct ioat_dma_chan *ioat = to_ioat_chan(chan);

	if (ioat->pending > 0) {
		spin_lock_bh(&ioat->desc_lock);
		__ioat2_dma_memcpy_issue_pending(ioat);
		spin_unlock_bh(&ioat->desc_lock);
	}
}


/**
 * ioat_dma_chan_reset_part2 - reinit the channel after a reset
 */
static void ioat_dma_chan_reset_part2(struct work_struct *work)
{
	struct ioat_chan_common *chan;
	struct ioat_dma_chan *ioat;
	struct ioat_desc_sw *desc;

	chan = container_of(work, struct ioat_chan_common, work.work);
	ioat = container_of(chan, struct ioat_dma_chan, base);
	spin_lock_bh(&chan->cleanup_lock);
	spin_lock_bh(&ioat->desc_lock);

	chan->completion_virt->low = 0;
	chan->completion_virt->high = 0;
	ioat->pending = 0;

	/*
	 * count the descriptors waiting, and be sure to do it
	 * right for both the CB1 line and the CB2 ring
	 */
	ioat->dmacount = 0;
	if (ioat->used_desc.prev) {
		desc = to_ioat_desc(ioat->used_desc.prev);
		do {
			ioat->dmacount++;
			desc = to_ioat_desc(desc->node.next);
		} while (&desc->node != ioat->used_desc.next);
	}

	/*
	 * write the new starting descriptor address
	 * this puts channel engine into ARMED state
	 */
	desc = to_ioat_desc(ioat->used_desc.prev);
	switch (chan->device->version) {
	case IOAT_VER_1_2:
		writel(((u64) desc->txd.phys) & 0x00000000FFFFFFFF,
		       chan->reg_base + IOAT1_CHAINADDR_OFFSET_LOW);
		writel(((u64) desc->txd.phys) >> 32,
		       chan->reg_base + IOAT1_CHAINADDR_OFFSET_HIGH);

		writeb(IOAT_CHANCMD_START, chan->reg_base
			+ IOAT_CHANCMD_OFFSET(chan->device->version));
		break;
	case IOAT_VER_2_0:
		writel(((u64) desc->txd.phys) & 0x00000000FFFFFFFF,
		       chan->reg_base + IOAT2_CHAINADDR_OFFSET_LOW);
		writel(((u64) desc->txd.phys) >> 32,
		       chan->reg_base + IOAT2_CHAINADDR_OFFSET_HIGH);

		/* tell the engine to go with what's left to be done */
		writew(ioat->dmacount,
		       chan->reg_base + IOAT_CHAN_DMACOUNT_OFFSET);

		break;
	}
	dev_err(to_dev(chan),
		"chan%d reset - %d descs waiting, %d total desc\n",
		chan_num(chan), ioat->dmacount, ioat->desccount);

	spin_unlock_bh(&ioat->desc_lock);
	spin_unlock_bh(&chan->cleanup_lock);
}

/**
 * ioat_dma_reset_channel - restart a channel
 * @ioat: IOAT DMA channel handle
 */
static void ioat_dma_reset_channel(struct ioat_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;
	void __iomem *reg_base = chan->reg_base;
	u32 chansts, chanerr;

	if (!ioat->used_desc.prev)
		return;

	chanerr = readl(reg_base + IOAT_CHANERR_OFFSET);
	chansts = (chan->completion_virt->low
					& IOAT_CHANSTS_DMA_TRANSFER_STATUS);
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

	spin_lock_bh(&ioat->desc_lock);
	ioat->pending = INT_MIN;
	writeb(IOAT_CHANCMD_RESET,
	       reg_base + IOAT_CHANCMD_OFFSET(chan->device->version));
	spin_unlock_bh(&ioat->desc_lock);

	/* schedule the 2nd half instead of sleeping a long time */
	schedule_delayed_work(&chan->work, RESET_DELAY);
}

/**
 * ioat_dma_chan_watchdog - watch for stuck channels
 */
static void ioat_dma_chan_watchdog(struct work_struct *work)
{
	struct ioatdma_device *device =
		container_of(work, struct ioatdma_device, work.work);
	struct ioat_dma_chan *ioat;
	struct ioat_chan_common *chan;
	int i;

	union {
		u64 full;
		struct {
			u32 low;
			u32 high;
		};
	} completion_hw;
	unsigned long compl_desc_addr_hw;

	for (i = 0; i < device->common.chancnt; i++) {
		chan = ioat_chan_by_index(device, i);
		ioat = container_of(chan, struct ioat_dma_chan, base);

		if (chan->device->version == IOAT_VER_1_2
			/* have we started processing anything yet */
		    && chan->last_completion
			/* have we completed any since last watchdog cycle? */
		    && (chan->last_completion == chan->watchdog_completion)
			/* has TCP stuck on one cookie since last watchdog? */
		    && (chan->watchdog_tcp_cookie == chan->watchdog_last_tcp_cookie)
		    && (chan->watchdog_tcp_cookie != chan->completed_cookie)
			/* is there something in the chain to be processed? */
			/* CB1 chain always has at least the last one processed */
		    && (ioat->used_desc.prev != ioat->used_desc.next)
		    && ioat->pending == 0) {

			/*
			 * check CHANSTS register for completed
			 * descriptor address.
			 * if it is different than completion writeback,
			 * it is not zero
			 * and it has changed since the last watchdog
			 *     we can assume that channel
			 *     is still working correctly
			 *     and the problem is in completion writeback.
			 *     update completion writeback
			 *     with actual CHANSTS value
			 * else
			 *     try resetting the channel
			 */

			completion_hw.low = readl(chan->reg_base +
				IOAT_CHANSTS_OFFSET_LOW(chan->device->version));
			completion_hw.high = readl(chan->reg_base +
				IOAT_CHANSTS_OFFSET_HIGH(chan->device->version));
#if (BITS_PER_LONG == 64)
			compl_desc_addr_hw =
				completion_hw.full
				& IOAT_CHANSTS_COMPLETED_DESCRIPTOR_ADDR;
#else
			compl_desc_addr_hw =
				completion_hw.low & IOAT_LOW_COMPLETION_MASK;
#endif

			if ((compl_desc_addr_hw != 0)
			   && (compl_desc_addr_hw != chan->watchdog_completion)
			   && (compl_desc_addr_hw != chan->last_compl_desc_addr_hw)) {
				chan->last_compl_desc_addr_hw = compl_desc_addr_hw;
				chan->completion_virt->low = completion_hw.low;
				chan->completion_virt->high = completion_hw.high;
			} else {
				ioat_dma_reset_channel(ioat);
				chan->watchdog_completion = 0;
				chan->last_compl_desc_addr_hw = 0;
			}

		/*
		 * for version 2.0 if there are descriptors yet to be processed
		 * and the last completed hasn't changed since the last watchdog
		 *      if they haven't hit the pending level
		 *          issue the pending to push them through
		 *      else
		 *          try resetting the channel
		 */
		} else if (chan->device->version == IOAT_VER_2_0
		    && ioat->used_desc.prev
		    && chan->last_completion
		    && chan->last_completion == chan->watchdog_completion) {

			if (ioat->pending < ioat_pending_level)
				ioat2_dma_memcpy_issue_pending(&chan->common);
			else {
				ioat_dma_reset_channel(ioat);
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

static dma_cookie_t ioat1_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct dma_chan *c = tx->chan;
	struct ioat_dma_chan *ioat = to_ioat_chan(c);
	struct ioat_desc_sw *desc = tx_to_ioat_desc(tx);
	struct ioat_desc_sw *first;
	struct ioat_desc_sw *chain_tail;
	dma_cookie_t cookie;

	spin_lock_bh(&ioat->desc_lock);
	/* cookie incr and addition to used_list must be atomic */
	cookie = c->cookie;
	cookie++;
	if (cookie < 0)
		cookie = 1;
	c->cookie = cookie;
	tx->cookie = cookie;

	/* write address into NextDescriptor field of last desc in chain */
	first = to_ioat_desc(tx->tx_list.next);
	chain_tail = to_ioat_desc(ioat->used_desc.prev);
	/* make descriptor updates globally visible before chaining */
	wmb();
	chain_tail->hw->next = first->txd.phys;
	list_splice_tail_init(&tx->tx_list, &ioat->used_desc);

	ioat->dmacount += desc->tx_cnt;
	ioat->pending += desc->tx_cnt;
	if (ioat->pending >= ioat_pending_level)
		__ioat1_dma_memcpy_issue_pending(ioat);
	spin_unlock_bh(&ioat->desc_lock);

	return cookie;
}

static dma_cookie_t ioat2_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct ioat_dma_chan *ioat = to_ioat_chan(tx->chan);
	struct ioat_desc_sw *first = tx_to_ioat_desc(tx);
	struct ioat_desc_sw *new;
	struct ioat_dma_descriptor *hw;
	dma_cookie_t cookie;
	u32 copy;
	size_t len;
	dma_addr_t src, dst;
	unsigned long orig_flags;
	unsigned int desc_count = 0;

	/* src and dest and len are stored in the initial descriptor */
	len = first->len;
	src = first->src;
	dst = first->dst;
	orig_flags = first->txd.flags;
	new = first;

	/*
	 * ioat->desc_lock is still in force in version 2 path
	 * it gets unlocked at end of this function
	 */
	do {
		copy = min_t(size_t, len, ioat->xfercap);

		async_tx_ack(&new->txd);

		hw = new->hw;
		hw->size = copy;
		hw->ctl = 0;
		hw->src_addr = src;
		hw->dst_addr = dst;

		len -= copy;
		dst += copy;
		src += copy;
		desc_count++;
	} while (len && (new = ioat2_dma_get_next_descriptor(ioat)));

	if (!new) {
		dev_err(to_dev(&ioat->base), "tx submit failed\n");
		spin_unlock_bh(&ioat->desc_lock);
		return -ENOMEM;
	}

	hw->ctl_f.compl_write = 1;
	if (first->txd.callback) {
		hw->ctl_f.int_en = 1;
		if (first != new) {
			/* move callback into to last desc */
			new->txd.callback = first->txd.callback;
			new->txd.callback_param
					= first->txd.callback_param;
			first->txd.callback = NULL;
			first->txd.callback_param = NULL;
		}
	}

	new->tx_cnt = desc_count;
	new->txd.flags = orig_flags; /* client is in control of this ack */

	/* store the original values for use in later cleanup */
	if (new != first) {
		new->src = first->src;
		new->dst = first->dst;
		new->len = first->len;
	}

	/* cookie incr and addition to used_list must be atomic */
	cookie = ioat->base.common.cookie;
	cookie++;
	if (cookie < 0)
		cookie = 1;
	ioat->base.common.cookie = new->txd.cookie = cookie;

	ioat->dmacount += desc_count;
	ioat->pending += desc_count;
	if (ioat->pending >= ioat_pending_level)
		__ioat2_dma_memcpy_issue_pending(ioat);
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
	dma_async_tx_descriptor_init(&desc_sw->txd, &ioat->base.common);
	switch (ioatdma_device->version) {
	case IOAT_VER_1_2:
		desc_sw->txd.tx_submit = ioat1_tx_submit;
		break;
	case IOAT_VER_2_0:
	case IOAT_VER_3_0:
		desc_sw->txd.tx_submit = ioat2_tx_submit;
		break;
	}

	desc_sw->hw = desc;
	desc_sw->txd.phys = phys;

	return desc_sw;
}

static int ioat_initial_desc_count = 256;
module_param(ioat_initial_desc_count, int, 0644);
MODULE_PARM_DESC(ioat_initial_desc_count,
		 "initial descriptors per channel (default: 256)");

/**
 * ioat2_dma_massage_chan_desc - link the descriptors into a circle
 * @ioat: the channel to be massaged
 */
static void ioat2_dma_massage_chan_desc(struct ioat_dma_chan *ioat)
{
	struct ioat_desc_sw *desc, *_desc;

	/* setup used_desc */
	ioat->used_desc.next = ioat->free_desc.next;
	ioat->used_desc.prev = NULL;

	/* pull free_desc out of the circle so that every node is a hw
	 * descriptor, but leave it pointing to the list
	 */
	ioat->free_desc.prev->next = ioat->free_desc.next;
	ioat->free_desc.next->prev = ioat->free_desc.prev;

	/* circle link the hw descriptors */
	desc = to_ioat_desc(ioat->free_desc.next);
	desc->hw->next = to_ioat_desc(desc->node.next)->txd.phys;
	list_for_each_entry_safe(desc, _desc, ioat->free_desc.next, node) {
		desc->hw->next = to_ioat_desc(desc->node.next)->txd.phys;
	}
}

/**
 * ioat_dma_alloc_chan_resources - returns the number of allocated descriptors
 * @chan: the channel to be filled out
 */
static int ioat_dma_alloc_chan_resources(struct dma_chan *c)
{
	struct ioat_dma_chan *ioat = to_ioat_chan(c);
	struct ioat_chan_common *chan = &ioat->base;
	struct ioat_desc_sw *desc;
	u16 chanctrl;
	u32 chanerr;
	int i;
	LIST_HEAD(tmp_list);

	/* have we already been set up? */
	if (!list_empty(&ioat->free_desc))
		return ioat->desccount;

	/* Setup register to interrupt and write completion status on error */
	chanctrl = IOAT_CHANCTRL_ERR_INT_EN |
		IOAT_CHANCTRL_ANY_ERR_ABORT_EN |
		IOAT_CHANCTRL_ERR_COMPLETION_EN;
	writew(chanctrl, chan->reg_base + IOAT_CHANCTRL_OFFSET);

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
		list_add_tail(&desc->node, &tmp_list);
	}
	spin_lock_bh(&ioat->desc_lock);
	ioat->desccount = i;
	list_splice(&tmp_list, &ioat->free_desc);
	if (chan->device->version != IOAT_VER_1_2)
		ioat2_dma_massage_chan_desc(ioat);
	spin_unlock_bh(&ioat->desc_lock);

	/* allocate a completion writeback area */
	/* doing 2 32bit writes to mmio since 1 64b write doesn't work */
	chan->completion_virt = pci_pool_alloc(chan->device->completion_pool,
					       GFP_KERNEL,
					       &chan->completion_addr);
	memset(chan->completion_virt, 0,
	       sizeof(*chan->completion_virt));
	writel(((u64) chan->completion_addr) & 0x00000000FFFFFFFF,
	       chan->reg_base + IOAT_CHANCMP_OFFSET_LOW);
	writel(((u64) chan->completion_addr) >> 32,
	       chan->reg_base + IOAT_CHANCMP_OFFSET_HIGH);

	tasklet_enable(&chan->cleanup_task);
	ioat_dma_start_null_desc(ioat);  /* give chain to dma device */
	return ioat->desccount;
}

/**
 * ioat_dma_free_chan_resources - release all the descriptors
 * @chan: the channel to be cleaned
 */
static void ioat_dma_free_chan_resources(struct dma_chan *c)
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

	tasklet_disable(&chan->cleanup_task);
	ioat_dma_memcpy_cleanup(ioat);

	/* Delay 100ms after reset to allow internal DMA logic to quiesce
	 * before removing DMA descriptor resources.
	 */
	writeb(IOAT_CHANCMD_RESET,
	       chan->reg_base + IOAT_CHANCMD_OFFSET(chan->device->version));
	mdelay(100);

	spin_lock_bh(&ioat->desc_lock);
	switch (chan->device->version) {
	case IOAT_VER_1_2:
		list_for_each_entry_safe(desc, _desc,
					 &ioat->used_desc, node) {
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
		break;
	case IOAT_VER_2_0:
	case IOAT_VER_3_0:
		list_for_each_entry_safe(desc, _desc,
					 ioat->free_desc.next, node) {
			list_del(&desc->node);
			pci_pool_free(ioatdma_device->dma_pool, desc->hw,
				      desc->txd.phys);
			kfree(desc);
		}
		desc = to_ioat_desc(ioat->free_desc.next);
		pci_pool_free(ioatdma_device->dma_pool, desc->hw,
			      desc->txd.phys);
		kfree(desc);
		INIT_LIST_HEAD(&ioat->free_desc);
		INIT_LIST_HEAD(&ioat->used_desc);
		break;
	}
	spin_unlock_bh(&ioat->desc_lock);

	pci_pool_free(ioatdma_device->completion_pool,
		      chan->completion_virt,
		      chan->completion_addr);

	/* one is ok since we left it on there on purpose */
	if (in_use_descs > 1)
		dev_err(to_dev(chan), "Freeing %d in use descriptors!\n",
			in_use_descs - 1);

	chan->last_completion = chan->completion_addr = 0;
	chan->watchdog_completion = 0;
	chan->last_compl_desc_addr_hw = 0;
	chan->watchdog_tcp_cookie = chan->watchdog_last_tcp_cookie = 0;
	ioat->pending = 0;
	ioat->dmacount = 0;
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

	prefetch(new->hw);
	return new;
}

static struct ioat_desc_sw *
ioat2_dma_get_next_descriptor(struct ioat_dma_chan *ioat)
{
	struct ioat_desc_sw *new;

	/*
	 * used.prev points to where to start processing
	 * used.next points to next free descriptor
	 * if used.prev == NULL, there are none waiting to be processed
	 * if used.next == used.prev.prev, there is only one free descriptor,
	 *      and we need to use it to as a noop descriptor before
	 *      linking in a new set of descriptors, since the device
	 *      has probably already read the pointer to it
	 */
	if (ioat->used_desc.prev &&
	    ioat->used_desc.next == ioat->used_desc.prev->prev) {

		struct ioat_desc_sw *desc;
		struct ioat_desc_sw *noop_desc;
		int i;

		/* set up the noop descriptor */
		noop_desc = to_ioat_desc(ioat->used_desc.next);
		/* set size to non-zero value (channel returns error when size is 0) */
		noop_desc->hw->size = NULL_DESC_BUFFER_SIZE;
		noop_desc->hw->ctl = 0;
		noop_desc->hw->ctl_f.null = 1;
		noop_desc->hw->src_addr = 0;
		noop_desc->hw->dst_addr = 0;

		ioat->used_desc.next = ioat->used_desc.next->next;
		ioat->pending++;
		ioat->dmacount++;

		/* try to get a few more descriptors */
		for (i = 16; i; i--) {
			desc = ioat_dma_alloc_descriptor(ioat, GFP_ATOMIC);
			if (!desc) {
				dev_err(to_dev(&ioat->base),
					"alloc failed\n");
				break;
			}
			list_add_tail(&desc->node, ioat->used_desc.next);

			desc->hw->next
				= to_ioat_desc(desc->node.next)->txd.phys;
			to_ioat_desc(desc->node.prev)->hw->next
				= desc->txd.phys;
			ioat->desccount++;
		}

		ioat->used_desc.next = noop_desc->node.next;
	}
	new = to_ioat_desc(ioat->used_desc.next);
	prefetch(new);
	ioat->used_desc.next = new->node.next;

	if (ioat->used_desc.prev == NULL)
		ioat->used_desc.prev = &new->node;

	prefetch(new->hw);
	return new;
}

static struct ioat_desc_sw *
ioat_dma_get_next_descriptor(struct ioat_dma_chan *ioat)
{
	if (!ioat)
		return NULL;

	switch (ioat->base.device->version) {
	case IOAT_VER_1_2:
		return ioat1_dma_get_next_descriptor(ioat);
	case IOAT_VER_2_0:
	case IOAT_VER_3_0:
		return ioat2_dma_get_next_descriptor(ioat);
	}
	return NULL;
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
	desc = ioat_dma_get_next_descriptor(ioat);
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
			next = ioat_dma_get_next_descriptor(ioat);
			hw->next = next ? next->txd.phys : 0;
			desc = next;
		} else
			hw->next = 0;
	} while (len);

	if (!desc) {
		struct ioat_chan_common *chan = &ioat->base;

		dev_err(to_dev(chan),
			"chan%d - get_next_desc failed: %d descs waiting, %d total desc\n",
			chan_num(chan), ioat->dmacount, ioat->desccount);
		list_splice(&chain, &ioat->free_desc);
		spin_unlock_bh(&ioat->desc_lock);
		return NULL;
	}
	spin_unlock_bh(&ioat->desc_lock);

	desc->txd.flags = flags;
	desc->tx_cnt = tx_cnt;
	desc->src = dma_src;
	desc->dst = dma_dest;
	desc->len = total_len;
	list_splice(&chain, &desc->txd.tx_list);
	hw->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
	hw->ctl_f.compl_write = 1;

	return &desc->txd;
}

static struct dma_async_tx_descriptor *
ioat2_dma_prep_memcpy(struct dma_chan *c, dma_addr_t dma_dest,
		      dma_addr_t dma_src, size_t len, unsigned long flags)
{
	struct ioat_dma_chan *ioat = to_ioat_chan(c);
	struct ioat_desc_sw *new;

	spin_lock_bh(&ioat->desc_lock);
	new = ioat2_dma_get_next_descriptor(ioat);

	/*
	 * leave ioat->desc_lock set in ioat 2 path
	 * it will get unlocked at end of tx_submit
	 */

	if (new) {
		new->len = len;
		new->dst = dma_dest;
		new->src = dma_src;
		new->txd.flags = flags;
		return &new->txd;
	} else {
		struct ioat_chan_common *chan = &ioat->base;

		spin_unlock_bh(&ioat->desc_lock);
		dev_err(to_dev(chan),
			"chan%d - get_next_desc failed: %d descs waiting, %d total desc\n",
			chan_num(chan), ioat->dmacount, ioat->desccount);
		return NULL;
	}
}

static void ioat_dma_cleanup_tasklet(unsigned long data)
{
	struct ioat_dma_chan *chan = (void *)data;
	ioat_dma_memcpy_cleanup(chan);
	writew(IOAT_CHANCTRL_INT_DISABLE,
	       chan->base.reg_base + IOAT_CHANCTRL_OFFSET);
}

static void
ioat_dma_unmap(struct ioat_chan_common *chan, struct ioat_desc_sw *desc)
{
	if (!(desc->txd.flags & DMA_COMPL_SKIP_DEST_UNMAP)) {
		if (desc->txd.flags & DMA_COMPL_DEST_UNMAP_SINGLE)
			pci_unmap_single(chan->device->pdev,
					 pci_unmap_addr(desc, dst),
					 pci_unmap_len(desc, len),
					 PCI_DMA_FROMDEVICE);
		else
			pci_unmap_page(chan->device->pdev,
				       pci_unmap_addr(desc, dst),
				       pci_unmap_len(desc, len),
				       PCI_DMA_FROMDEVICE);
	}

	if (!(desc->txd.flags & DMA_COMPL_SKIP_SRC_UNMAP)) {
		if (desc->txd.flags & DMA_COMPL_SRC_UNMAP_SINGLE)
			pci_unmap_single(chan->device->pdev,
					 pci_unmap_addr(desc, src),
					 pci_unmap_len(desc, len),
					 PCI_DMA_TODEVICE);
		else
			pci_unmap_page(chan->device->pdev,
				       pci_unmap_addr(desc, src),
				       pci_unmap_len(desc, len),
				       PCI_DMA_TODEVICE);
	}
}

/**
 * ioat_dma_memcpy_cleanup - cleanup up finished descriptors
 * @chan: ioat channel to be cleaned up
 */
static void ioat_dma_memcpy_cleanup(struct ioat_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;
	unsigned long phys_complete;
	struct ioat_desc_sw *desc, *_desc;
	dma_cookie_t cookie = 0;
	unsigned long desc_phys;
	struct ioat_desc_sw *latest_desc;
	struct dma_async_tx_descriptor *tx;

	prefetch(chan->completion_virt);

	if (!spin_trylock_bh(&chan->cleanup_lock))
		return;

	/* The completion writeback can happen at any time,
	   so reads by the driver need to be atomic operations
	   The descriptor physical addresses are limited to 32-bits
	   when the CPU can only do a 32-bit mov */

#if (BITS_PER_LONG == 64)
	phys_complete =
		chan->completion_virt->full
		& IOAT_CHANSTS_COMPLETED_DESCRIPTOR_ADDR;
#else
	phys_complete = chan->completion_virt->low & IOAT_LOW_COMPLETION_MASK;
#endif

	if ((chan->completion_virt->full
		& IOAT_CHANSTS_DMA_TRANSFER_STATUS) ==
				IOAT_CHANSTS_DMA_TRANSFER_STATUS_HALTED) {
		dev_err(to_dev(chan), "Channel halted, chanerr = %x\n",
			readl(chan->reg_base + IOAT_CHANERR_OFFSET));

		/* TODO do something to salvage the situation */
	}

	if (phys_complete == chan->last_completion) {
		spin_unlock_bh(&chan->cleanup_lock);
		/*
		 * perhaps we're stuck so hard that the watchdog can't go off?
		 * try to catch it after 2 seconds
		 */
		if (chan->device->version != IOAT_VER_3_0) {
			if (time_after(jiffies,
				       chan->last_completion_time + HZ*WATCHDOG_DELAY)) {
				ioat_dma_chan_watchdog(&(chan->device->work.work));
				chan->last_completion_time = jiffies;
			}
		}
		return;
	}
	chan->last_completion_time = jiffies;

	cookie = 0;
	if (!spin_trylock_bh(&ioat->desc_lock)) {
		spin_unlock_bh(&chan->cleanup_lock);
		return;
	}

	switch (chan->device->version) {
	case IOAT_VER_1_2:
		list_for_each_entry_safe(desc, _desc, &ioat->used_desc, node) {
			tx = &desc->txd;
			/*
			 * Incoming DMA requests may use multiple descriptors,
			 * due to exceeding xfercap, perhaps. If so, only the
			 * last one will have a cookie, and require unmapping.
			 */
			if (tx->cookie) {
				cookie = tx->cookie;
				ioat_dma_unmap(chan, desc);
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
				if (async_tx_test_ack(tx)) {
					list_move_tail(&desc->node,
						       &ioat->free_desc);
				} else
					tx->cookie = 0;
			} else {
				/*
				 * last used desc. Do not remove, so we can
				 * append from it, but don't look at it next
				 * time, either
				 */
				tx->cookie = 0;

				/* TODO check status bits? */
				break;
			}
		}
		break;
	case IOAT_VER_2_0:
	case IOAT_VER_3_0:
		/* has some other thread has already cleaned up? */
		if (ioat->used_desc.prev == NULL)
			break;

		/* work backwards to find latest finished desc */
		desc = to_ioat_desc(ioat->used_desc.next);
		tx = &desc->txd;
		latest_desc = NULL;
		do {
			desc = to_ioat_desc(desc->node.prev);
			desc_phys = (unsigned long)tx->phys
				       & IOAT_CHANSTS_COMPLETED_DESCRIPTOR_ADDR;
			if (desc_phys == phys_complete) {
				latest_desc = desc;
				break;
			}
		} while (&desc->node != ioat->used_desc.prev);

		if (latest_desc != NULL) {
			/* work forwards to clear finished descriptors */
			for (desc = to_ioat_desc(ioat->used_desc.prev);
			     &desc->node != latest_desc->node.next &&
			     &desc->node != ioat->used_desc.next;
			     desc = to_ioat_desc(desc->node.next)) {
				if (tx->cookie) {
					cookie = tx->cookie;
					tx->cookie = 0;
					ioat_dma_unmap(chan, desc);
					if (tx->callback) {
						tx->callback(tx->callback_param);
						tx->callback = NULL;
					}
				}
			}

			/* move used.prev up beyond those that are finished */
			if (&desc->node == ioat->used_desc.next)
				ioat->used_desc.prev = NULL;
			else
				ioat->used_desc.prev = &desc->node;
		}
		break;
	}

	spin_unlock_bh(&ioat->desc_lock);

	chan->last_completion = phys_complete;
	if (cookie != 0)
		chan->completed_cookie = cookie;

	spin_unlock_bh(&chan->cleanup_lock);
}

/**
 * ioat_dma_is_complete - poll the status of a IOAT DMA transaction
 * @chan: IOAT DMA channel handle
 * @cookie: DMA transaction identifier
 * @done: if not %NULL, updated with last completed transaction
 * @used: if not %NULL, updated with last used transaction
 */
static enum dma_status
ioat_dma_is_complete(struct dma_chan *c, dma_cookie_t cookie,
		     dma_cookie_t *done, dma_cookie_t *used)
{
	struct ioat_dma_chan *ioat = to_ioat_chan(c);
	struct ioat_chan_common *chan = &ioat->base;
	dma_cookie_t last_used;
	dma_cookie_t last_complete;
	enum dma_status ret;

	last_used = c->cookie;
	last_complete = chan->completed_cookie;
	chan->watchdog_tcp_cookie = cookie;

	if (done)
		*done = last_complete;
	if (used)
		*used = last_used;

	ret = dma_async_is_complete(cookie, last_complete, last_used);
	if (ret == DMA_SUCCESS)
		return ret;

	ioat_dma_memcpy_cleanup(ioat);

	last_used = c->cookie;
	last_complete = chan->completed_cookie;

	if (done)
		*done = last_complete;
	if (used)
		*used = last_used;

	return dma_async_is_complete(cookie, last_complete, last_used);
}

static void ioat_dma_start_null_desc(struct ioat_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;
	struct ioat_desc_sw *desc;
	struct ioat_dma_descriptor *hw;

	spin_lock_bh(&ioat->desc_lock);

	desc = ioat_dma_get_next_descriptor(ioat);

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
	switch (chan->device->version) {
	case IOAT_VER_1_2:
		hw->next = 0;
		list_add_tail(&desc->node, &ioat->used_desc);

		writel(((u64) desc->txd.phys) & 0x00000000FFFFFFFF,
		       chan->reg_base + IOAT1_CHAINADDR_OFFSET_LOW);
		writel(((u64) desc->txd.phys) >> 32,
		       chan->reg_base + IOAT1_CHAINADDR_OFFSET_HIGH);

		writeb(IOAT_CHANCMD_START, chan->reg_base
			+ IOAT_CHANCMD_OFFSET(chan->device->version));
		break;
	case IOAT_VER_2_0:
	case IOAT_VER_3_0:
		writel(((u64) desc->txd.phys) & 0x00000000FFFFFFFF,
		       chan->reg_base + IOAT2_CHAINADDR_OFFSET_LOW);
		writel(((u64) desc->txd.phys) >> 32,
		       chan->reg_base + IOAT2_CHAINADDR_OFFSET_HIGH);

		ioat->dmacount++;
		__ioat2_dma_memcpy_issue_pending(ioat);
		break;
	}
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
static int ioat_dma_self_test(struct ioatdma_device *device)
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
	dma_dest = dma_map_single(dev, dest, IOAT_TEST_SIZE, DMA_FROM_DEVICE);
	flags = DMA_COMPL_SRC_UNMAP_SINGLE | DMA_COMPL_DEST_UNMAP_SINGLE |
		DMA_PREP_INTERRUPT;
	tx = device->common.device_prep_dma_memcpy(dma_chan, dma_dest, dma_src,
						   IOAT_TEST_SIZE, flags);
	if (!tx) {
		dev_err(dev, "Self-test prep failed, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}

	async_tx_ack(tx);
	init_completion(&cmp);
	tx->callback = ioat_dma_test_callback;
	tx->callback_param = &cmp;
	cookie = tx->tx_submit(tx);
	if (cookie < 0) {
		dev_err(dev, "Self-test setup failed, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}
	dma->device_issue_pending(dma_chan);

	tmo = wait_for_completion_timeout(&cmp, msecs_to_jiffies(3000));

	if (tmo == 0 ||
	    dma->device_is_tx_complete(dma_chan, cookie, NULL, NULL)
					!= DMA_SUCCESS) {
		dev_err(dev, "Self-test copy timed out, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}
	if (memcmp(src, dest, IOAT_TEST_SIZE)) {
		dev_err(dev, "Self-test copy failed compare, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}

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
		 "set ioat interrupt style: msix (default), "
		 "msix-single-vector, msi, intx)");

/**
 * ioat_dma_setup_interrupts - setup interrupt handler
 * @device: ioat device
 */
static int ioat_dma_setup_interrupts(struct ioatdma_device *device)
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
	if (!strcmp(ioat_interrupt_style, "msix-single-vector"))
		goto msix_single_vector;
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

	err = pci_enable_msix(pdev, device->msix_entries, msixcnt);
	if (err < 0)
		goto msi;
	if (err > 0)
		goto msix_single_vector;

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
			goto msix_single_vector;
		}
	}
	intrctrl |= IOAT_INTRCTRL_MSIX_VECTOR_CONTROL;
	goto done;

msix_single_vector:
	msix = &device->msix_entries[0];
	msix->entry = 0;
	err = pci_enable_msix(pdev, device->msix_entries, 1);
	if (err)
		goto msi;

	err = devm_request_irq(dev, msix->vector, ioat_dma_do_interrupt, 0,
			       "ioat-msix", device);
	if (err) {
		pci_disable_msix(pdev);
		goto msi;
	}
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
	goto done;

intx:
	err = devm_request_irq(dev, pdev->irq, ioat_dma_do_interrupt,
			       IRQF_SHARED, "ioat-intx", device);
	if (err)
		goto err_no_irq;

done:
	if (device->intr_quirk)
		device->intr_quirk(device);
	intrctrl |= IOAT_INTRCTRL_MASTER_INT_EN;
	writeb(intrctrl, device->reg_base + IOAT_INTRCTRL_OFFSET);
	return 0;

err_no_irq:
	/* Disable all interrupt generation */
	writeb(0, device->reg_base + IOAT_INTRCTRL_OFFSET);
	dev_err(dev, "no usable interrupts\n");
	return err;
}

static void ioat_disable_interrupts(struct ioatdma_device *device)
{
	/* Disable all interrupt generation */
	writeb(0, device->reg_base + IOAT_INTRCTRL_OFFSET);
}

static int ioat_probe(struct ioatdma_device *device)
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

	ioat_dma_enumerate_channels(device);

	dma_cap_set(DMA_MEMCPY, dma->cap_mask);
	dma->device_alloc_chan_resources = ioat_dma_alloc_chan_resources;
	dma->device_free_chan_resources = ioat_dma_free_chan_resources;
	dma->device_is_tx_complete = ioat_dma_is_complete;
	dma->dev = &pdev->dev;

	dev_err(dev, "Intel(R) I/OAT DMA Engine found,"
		" %d channels, device version 0x%02x, driver version %s\n",
		dma->chancnt, device->version, IOAT_DMA_VERSION);

	if (!dma->chancnt) {
		dev_err(dev, "Intel(R) I/OAT DMA Engine problem found: "
			"zero channels detected\n");
		goto err_setup_interrupts;
	}

	err = ioat_dma_setup_interrupts(device);
	if (err)
		goto err_setup_interrupts;

	err = ioat_dma_self_test(device);
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

static int ioat_register(struct ioatdma_device *device)
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

int ioat1_dma_probe(struct ioatdma_device *device, int dca)
{
	struct pci_dev *pdev = device->pdev;
	struct dma_device *dma;
	int err;

	device->intr_quirk = ioat1_intr_quirk;
	dma = &device->common;
	dma->device_prep_dma_memcpy = ioat1_dma_prep_memcpy;
	dma->device_issue_pending = ioat1_dma_memcpy_issue_pending;

	err = ioat_probe(device);
	if (err)
		return err;
	ioat_set_tcp_copy_break(4096);
	err = ioat_register(device);
	if (err)
		return err;
	if (dca)
		device->dca = ioat_dca_init(pdev, device->reg_base);

	INIT_DELAYED_WORK(&device->work, ioat_dma_chan_watchdog);
	schedule_delayed_work(&device->work, WATCHDOG_DELAY);

	return err;
}

int ioat2_dma_probe(struct ioatdma_device *device, int dca)
{
	struct pci_dev *pdev = device->pdev;
	struct dma_device *dma;
	struct dma_chan *c;
	struct ioat_chan_common *chan;
	int err;

	dma = &device->common;
	dma->device_prep_dma_memcpy = ioat2_dma_prep_memcpy;
	dma->device_issue_pending = ioat2_dma_memcpy_issue_pending;

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

	INIT_DELAYED_WORK(&device->work, ioat_dma_chan_watchdog);
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

	dma = &device->common;
	dma->device_prep_dma_memcpy = ioat2_dma_prep_memcpy;
	dma->device_issue_pending = ioat2_dma_memcpy_issue_pending;

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

void ioat_dma_remove(struct ioatdma_device *device)
{
	struct dma_device *dma = &device->common;

	if (device->version != IOAT_VER_3_0)
		cancel_delayed_work(&device->work);

	ioat_disable_interrupts(device);

	dma_async_device_unregister(dma);

	pci_pool_destroy(device->dma_pool);
	pci_pool_destroy(device->completion_pool);

	INIT_LIST_HEAD(&dma->channels);
}

