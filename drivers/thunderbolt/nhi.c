// SPDX-License-Identifier: GPL-2.0-only
/*
 * Thunderbolt driver - NHI driver
 *
 * The NHI (native host interface) is the pci device that allows us to send and
 * receive frames from the thunderbolt bus.
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2018, Intel Corporation
 */

#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/property.h>
#include <linux/string_helpers.h>

#include "nhi.h"
#include "nhi_regs.h"
#include "tb.h"

#define RING_TYPE(ring) ((ring)->is_tx ? "TX ring" : "RX ring")

#define RING_FIRST_USABLE_HOPID	1
/*
 * Used with QUIRK_E2E to specify an unused HopID the Rx credits are
 * transferred.
 */
#define RING_E2E_RESERVED_HOPID	RING_FIRST_USABLE_HOPID
/*
 * Minimal number of vectors when we use MSI-X. Two for control channel
 * Rx/Tx and the rest four are for cross domain DMA paths.
 */
#define MSIX_MIN_VECS		6
#define MSIX_MAX_VECS		16

#define NHI_MAILBOX_TIMEOUT	500 /* ms */

/* Host interface quirks */
#define QUIRK_AUTO_CLEAR_INT	BIT(0)
#define QUIRK_E2E		BIT(1)

static bool host_reset = true;
module_param(host_reset, bool, 0444);
MODULE_PARM_DESC(host_reset, "reset USB4 host router (default: true)");

static int ring_interrupt_index(const struct tb_ring *ring)
{
	int bit = ring->hop;
	if (!ring->is_tx)
		bit += ring->nhi->hop_count;
	return bit;
}

static void nhi_mask_interrupt(struct tb_nhi *nhi, int mask, int ring)
{
	if (nhi->quirks & QUIRK_AUTO_CLEAR_INT) {
		u32 val;

		val = ioread32(nhi->iobase + REG_RING_INTERRUPT_BASE + ring);
		iowrite32(val & ~mask, nhi->iobase + REG_RING_INTERRUPT_BASE + ring);
	} else {
		iowrite32(mask, nhi->iobase + REG_RING_INTERRUPT_MASK_CLEAR_BASE + ring);
	}
}

static void nhi_clear_interrupt(struct tb_nhi *nhi, int ring)
{
	if (nhi->quirks & QUIRK_AUTO_CLEAR_INT)
		ioread32(nhi->iobase + REG_RING_NOTIFY_BASE + ring);
	else
		iowrite32(~0, nhi->iobase + REG_RING_INT_CLEAR + ring);
}

/*
 * ring_interrupt_active() - activate/deactivate interrupts for a single ring
 *
 * ring->nhi->lock must be held.
 */
static void ring_interrupt_active(struct tb_ring *ring, bool active)
{
	int index = ring_interrupt_index(ring) / 32 * 4;
	int reg = REG_RING_INTERRUPT_BASE + index;
	int interrupt_bit = ring_interrupt_index(ring) & 31;
	int mask = 1 << interrupt_bit;
	u32 old, new;

	if (ring->irq > 0) {
		u32 step, shift, ivr, misc;
		void __iomem *ivr_base;
		int auto_clear_bit;
		int index;

		if (ring->is_tx)
			index = ring->hop;
		else
			index = ring->hop + ring->nhi->hop_count;

		/*
		 * Intel routers support a bit that isn't part of
		 * the USB4 spec to ask the hardware to clear
		 * interrupt status bits automatically since
		 * we already know which interrupt was triggered.
		 *
		 * Other routers explicitly disable auto-clear
		 * to prevent conditions that may occur where two
		 * MSIX interrupts are simultaneously active and
		 * reading the register clears both of them.
		 */
		misc = ioread32(ring->nhi->iobase + REG_DMA_MISC);
		if (ring->nhi->quirks & QUIRK_AUTO_CLEAR_INT)
			auto_clear_bit = REG_DMA_MISC_INT_AUTO_CLEAR;
		else
			auto_clear_bit = REG_DMA_MISC_DISABLE_AUTO_CLEAR;
		if (!(misc & auto_clear_bit))
			iowrite32(misc | auto_clear_bit,
				  ring->nhi->iobase + REG_DMA_MISC);

		ivr_base = ring->nhi->iobase + REG_INT_VEC_ALLOC_BASE;
		step = index / REG_INT_VEC_ALLOC_REGS * REG_INT_VEC_ALLOC_BITS;
		shift = index % REG_INT_VEC_ALLOC_REGS * REG_INT_VEC_ALLOC_BITS;
		ivr = ioread32(ivr_base + step);
		ivr &= ~(REG_INT_VEC_ALLOC_MASK << shift);
		if (active)
			ivr |= ring->vector << shift;
		iowrite32(ivr, ivr_base + step);
	}

	old = ioread32(ring->nhi->iobase + reg);
	if (active)
		new = old | mask;
	else
		new = old & ~mask;

	dev_dbg(&ring->nhi->pdev->dev,
		"%s interrupt at register %#x bit %d (%#x -> %#x)\n",
		active ? "enabling" : "disabling", reg, interrupt_bit, old, new);

	if (new == old)
		dev_WARN(&ring->nhi->pdev->dev,
					 "interrupt for %s %d is already %s\n",
					 RING_TYPE(ring), ring->hop,
					 active ? "enabled" : "disabled");

	if (active)
		iowrite32(new, ring->nhi->iobase + reg);
	else
		nhi_mask_interrupt(ring->nhi, mask, index);
}

/*
 * nhi_disable_interrupts() - disable interrupts for all rings
 *
 * Use only during init and shutdown.
 */
static void nhi_disable_interrupts(struct tb_nhi *nhi)
{
	int i = 0;
	/* disable interrupts */
	for (i = 0; i < RING_INTERRUPT_REG_COUNT(nhi); i++)
		nhi_mask_interrupt(nhi, ~0, 4 * i);

	/* clear interrupt status bits */
	for (i = 0; i < RING_NOTIFY_REG_COUNT(nhi); i++)
		nhi_clear_interrupt(nhi, 4 * i);
}

/* ring helper methods */

static void __iomem *ring_desc_base(struct tb_ring *ring)
{
	void __iomem *io = ring->nhi->iobase;
	io += ring->is_tx ? REG_TX_RING_BASE : REG_RX_RING_BASE;
	io += ring->hop * 16;
	return io;
}

static void __iomem *ring_options_base(struct tb_ring *ring)
{
	void __iomem *io = ring->nhi->iobase;
	io += ring->is_tx ? REG_TX_OPTIONS_BASE : REG_RX_OPTIONS_BASE;
	io += ring->hop * 32;
	return io;
}

static void ring_iowrite_cons(struct tb_ring *ring, u16 cons)
{
	/*
	 * The other 16-bits in the register is read-only and writes to it
	 * are ignored by the hardware so we can save one ioread32() by
	 * filling the read-only bits with zeroes.
	 */
	iowrite32(cons, ring_desc_base(ring) + 8);
}

static void ring_iowrite_prod(struct tb_ring *ring, u16 prod)
{
	/* See ring_iowrite_cons() above for explanation */
	iowrite32(prod << 16, ring_desc_base(ring) + 8);
}

static void ring_iowrite32desc(struct tb_ring *ring, u32 value, u32 offset)
{
	iowrite32(value, ring_desc_base(ring) + offset);
}

static void ring_iowrite64desc(struct tb_ring *ring, u64 value, u32 offset)
{
	iowrite32(value, ring_desc_base(ring) + offset);
	iowrite32(value >> 32, ring_desc_base(ring) + offset + 4);
}

static void ring_iowrite32options(struct tb_ring *ring, u32 value, u32 offset)
{
	iowrite32(value, ring_options_base(ring) + offset);
}

static bool ring_full(struct tb_ring *ring)
{
	return ((ring->head + 1) % ring->size) == ring->tail;
}

static bool ring_empty(struct tb_ring *ring)
{
	return ring->head == ring->tail;
}

/*
 * ring_write_descriptors() - post frames from ring->queue to the controller
 *
 * ring->lock is held.
 */
static void ring_write_descriptors(struct tb_ring *ring)
{
	struct ring_frame *frame, *n;
	struct ring_desc *descriptor;
	list_for_each_entry_safe(frame, n, &ring->queue, list) {
		if (ring_full(ring))
			break;
		list_move_tail(&frame->list, &ring->in_flight);
		descriptor = &ring->descriptors[ring->head];
		descriptor->phys = frame->buffer_phy;
		descriptor->time = 0;
		descriptor->flags = RING_DESC_POSTED | RING_DESC_INTERRUPT;
		if (ring->is_tx) {
			descriptor->length = frame->size;
			descriptor->eof = frame->eof;
			descriptor->sof = frame->sof;
		}
		ring->head = (ring->head + 1) % ring->size;
		if (ring->is_tx)
			ring_iowrite_prod(ring, ring->head);
		else
			ring_iowrite_cons(ring, ring->head);
	}
}

/*
 * ring_work() - progress completed frames
 *
 * If the ring is shutting down then all frames are marked as canceled and
 * their callbacks are invoked.
 *
 * Otherwise we collect all completed frame from the ring buffer, write new
 * frame to the ring buffer and invoke the callbacks for the completed frames.
 */
static void ring_work(struct work_struct *work)
{
	struct tb_ring *ring = container_of(work, typeof(*ring), work);
	struct ring_frame *frame;
	bool canceled = false;
	unsigned long flags;
	LIST_HEAD(done);

	spin_lock_irqsave(&ring->lock, flags);

	if (!ring->running) {
		/*  Move all frames to done and mark them as canceled. */
		list_splice_tail_init(&ring->in_flight, &done);
		list_splice_tail_init(&ring->queue, &done);
		canceled = true;
		goto invoke_callback;
	}

	while (!ring_empty(ring)) {
		if (!(ring->descriptors[ring->tail].flags
				& RING_DESC_COMPLETED))
			break;
		frame = list_first_entry(&ring->in_flight, typeof(*frame),
					 list);
		list_move_tail(&frame->list, &done);
		if (!ring->is_tx) {
			frame->size = ring->descriptors[ring->tail].length;
			frame->eof = ring->descriptors[ring->tail].eof;
			frame->sof = ring->descriptors[ring->tail].sof;
			frame->flags = ring->descriptors[ring->tail].flags;
		}
		ring->tail = (ring->tail + 1) % ring->size;
	}
	ring_write_descriptors(ring);

invoke_callback:
	/* allow callbacks to schedule new work */
	spin_unlock_irqrestore(&ring->lock, flags);
	while (!list_empty(&done)) {
		frame = list_first_entry(&done, typeof(*frame), list);
		/*
		 * The callback may reenqueue or delete frame.
		 * Do not hold on to it.
		 */
		list_del_init(&frame->list);
		if (frame->callback)
			frame->callback(ring, frame, canceled);
	}
}

int __tb_ring_enqueue(struct tb_ring *ring, struct ring_frame *frame)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&ring->lock, flags);
	if (ring->running) {
		list_add_tail(&frame->list, &ring->queue);
		ring_write_descriptors(ring);
	} else {
		ret = -ESHUTDOWN;
	}
	spin_unlock_irqrestore(&ring->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(__tb_ring_enqueue);

/**
 * tb_ring_poll() - Poll one completed frame from the ring
 * @ring: Ring to poll
 *
 * This function can be called when @start_poll callback of the @ring
 * has been called. It will read one completed frame from the ring and
 * return it to the caller. Returns %NULL if there is no more completed
 * frames.
 */
struct ring_frame *tb_ring_poll(struct tb_ring *ring)
{
	struct ring_frame *frame = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ring->lock, flags);
	if (!ring->running)
		goto unlock;
	if (ring_empty(ring))
		goto unlock;

	if (ring->descriptors[ring->tail].flags & RING_DESC_COMPLETED) {
		frame = list_first_entry(&ring->in_flight, typeof(*frame),
					 list);
		list_del_init(&frame->list);

		if (!ring->is_tx) {
			frame->size = ring->descriptors[ring->tail].length;
			frame->eof = ring->descriptors[ring->tail].eof;
			frame->sof = ring->descriptors[ring->tail].sof;
			frame->flags = ring->descriptors[ring->tail].flags;
		}

		ring->tail = (ring->tail + 1) % ring->size;
	}

unlock:
	spin_unlock_irqrestore(&ring->lock, flags);
	return frame;
}
EXPORT_SYMBOL_GPL(tb_ring_poll);

static void __ring_interrupt_mask(struct tb_ring *ring, bool mask)
{
	int idx = ring_interrupt_index(ring);
	int reg = REG_RING_INTERRUPT_BASE + idx / 32 * 4;
	int bit = idx % 32;
	u32 val;

	val = ioread32(ring->nhi->iobase + reg);
	if (mask)
		val &= ~BIT(bit);
	else
		val |= BIT(bit);
	iowrite32(val, ring->nhi->iobase + reg);
}

/* Both @nhi->lock and @ring->lock should be held */
static void __ring_interrupt(struct tb_ring *ring)
{
	if (!ring->running)
		return;

	if (ring->start_poll) {
		__ring_interrupt_mask(ring, true);
		ring->start_poll(ring->poll_data);
	} else {
		schedule_work(&ring->work);
	}
}

/**
 * tb_ring_poll_complete() - Re-start interrupt for the ring
 * @ring: Ring to re-start the interrupt
 *
 * This will re-start (unmask) the ring interrupt once the user is done
 * with polling.
 */
void tb_ring_poll_complete(struct tb_ring *ring)
{
	unsigned long flags;

	spin_lock_irqsave(&ring->nhi->lock, flags);
	spin_lock(&ring->lock);
	if (ring->start_poll)
		__ring_interrupt_mask(ring, false);
	spin_unlock(&ring->lock);
	spin_unlock_irqrestore(&ring->nhi->lock, flags);
}
EXPORT_SYMBOL_GPL(tb_ring_poll_complete);

static void ring_clear_msix(const struct tb_ring *ring)
{
	int bit;

	if (ring->nhi->quirks & QUIRK_AUTO_CLEAR_INT)
		return;

	bit = ring_interrupt_index(ring) & 31;
	if (ring->is_tx)
		iowrite32(BIT(bit), ring->nhi->iobase + REG_RING_INT_CLEAR);
	else
		iowrite32(BIT(bit), ring->nhi->iobase + REG_RING_INT_CLEAR +
			  4 * (ring->nhi->hop_count / 32));
}

static irqreturn_t ring_msix(int irq, void *data)
{
	struct tb_ring *ring = data;

	spin_lock(&ring->nhi->lock);
	ring_clear_msix(ring);
	spin_lock(&ring->lock);
	__ring_interrupt(ring);
	spin_unlock(&ring->lock);
	spin_unlock(&ring->nhi->lock);

	return IRQ_HANDLED;
}

static int ring_request_msix(struct tb_ring *ring, bool no_suspend)
{
	struct tb_nhi *nhi = ring->nhi;
	unsigned long irqflags;
	int ret;

	if (!nhi->pdev->msix_enabled)
		return 0;

	ret = ida_alloc_max(&nhi->msix_ida, MSIX_MAX_VECS - 1, GFP_KERNEL);
	if (ret < 0)
		return ret;

	ring->vector = ret;

	ret = pci_irq_vector(ring->nhi->pdev, ring->vector);
	if (ret < 0)
		goto err_ida_remove;

	ring->irq = ret;

	irqflags = no_suspend ? IRQF_NO_SUSPEND : 0;
	ret = request_irq(ring->irq, ring_msix, irqflags, "thunderbolt", ring);
	if (ret)
		goto err_ida_remove;

	return 0;

err_ida_remove:
	ida_free(&nhi->msix_ida, ring->vector);

	return ret;
}

static void ring_release_msix(struct tb_ring *ring)
{
	if (ring->irq <= 0)
		return;

	free_irq(ring->irq, ring);
	ida_free(&ring->nhi->msix_ida, ring->vector);
	ring->vector = 0;
	ring->irq = 0;
}

static int nhi_alloc_hop(struct tb_nhi *nhi, struct tb_ring *ring)
{
	unsigned int start_hop = RING_FIRST_USABLE_HOPID;
	int ret = 0;

	if (nhi->quirks & QUIRK_E2E) {
		start_hop = RING_FIRST_USABLE_HOPID + 1;
		if (ring->flags & RING_FLAG_E2E && !ring->is_tx) {
			dev_dbg(&nhi->pdev->dev, "quirking E2E TX HopID %u -> %u\n",
				ring->e2e_tx_hop, RING_E2E_RESERVED_HOPID);
			ring->e2e_tx_hop = RING_E2E_RESERVED_HOPID;
		}
	}

	spin_lock_irq(&nhi->lock);

	if (ring->hop < 0) {
		unsigned int i;

		/*
		 * Automatically allocate HopID from the non-reserved
		 * range 1 .. hop_count - 1.
		 */
		for (i = start_hop; i < nhi->hop_count; i++) {
			if (ring->is_tx) {
				if (!nhi->tx_rings[i]) {
					ring->hop = i;
					break;
				}
			} else {
				if (!nhi->rx_rings[i]) {
					ring->hop = i;
					break;
				}
			}
		}
	}

	if (ring->hop > 0 && ring->hop < start_hop) {
		dev_warn(&nhi->pdev->dev, "invalid hop: %d\n", ring->hop);
		ret = -EINVAL;
		goto err_unlock;
	}
	if (ring->hop < 0 || ring->hop >= nhi->hop_count) {
		dev_warn(&nhi->pdev->dev, "invalid hop: %d\n", ring->hop);
		ret = -EINVAL;
		goto err_unlock;
	}
	if (ring->is_tx && nhi->tx_rings[ring->hop]) {
		dev_warn(&nhi->pdev->dev, "TX hop %d already allocated\n",
			 ring->hop);
		ret = -EBUSY;
		goto err_unlock;
	}
	if (!ring->is_tx && nhi->rx_rings[ring->hop]) {
		dev_warn(&nhi->pdev->dev, "RX hop %d already allocated\n",
			 ring->hop);
		ret = -EBUSY;
		goto err_unlock;
	}

	if (ring->is_tx)
		nhi->tx_rings[ring->hop] = ring;
	else
		nhi->rx_rings[ring->hop] = ring;

err_unlock:
	spin_unlock_irq(&nhi->lock);

	return ret;
}

static struct tb_ring *tb_ring_alloc(struct tb_nhi *nhi, u32 hop, int size,
				     bool transmit, unsigned int flags,
				     int e2e_tx_hop, u16 sof_mask, u16 eof_mask,
				     void (*start_poll)(void *),
				     void *poll_data)
{
	struct tb_ring *ring = NULL;

	dev_dbg(&nhi->pdev->dev, "allocating %s ring %d of size %d\n",
		transmit ? "TX" : "RX", hop, size);

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring)
		return NULL;

	spin_lock_init(&ring->lock);
	INIT_LIST_HEAD(&ring->queue);
	INIT_LIST_HEAD(&ring->in_flight);
	INIT_WORK(&ring->work, ring_work);

	ring->nhi = nhi;
	ring->hop = hop;
	ring->is_tx = transmit;
	ring->size = size;
	ring->flags = flags;
	ring->e2e_tx_hop = e2e_tx_hop;
	ring->sof_mask = sof_mask;
	ring->eof_mask = eof_mask;
	ring->head = 0;
	ring->tail = 0;
	ring->running = false;
	ring->start_poll = start_poll;
	ring->poll_data = poll_data;

	ring->descriptors = dma_alloc_coherent(&ring->nhi->pdev->dev,
			size * sizeof(*ring->descriptors),
			&ring->descriptors_dma, GFP_KERNEL | __GFP_ZERO);
	if (!ring->descriptors)
		goto err_free_ring;

	if (ring_request_msix(ring, flags & RING_FLAG_NO_SUSPEND))
		goto err_free_descs;

	if (nhi_alloc_hop(nhi, ring))
		goto err_release_msix;

	return ring;

err_release_msix:
	ring_release_msix(ring);
err_free_descs:
	dma_free_coherent(&ring->nhi->pdev->dev,
			  ring->size * sizeof(*ring->descriptors),
			  ring->descriptors, ring->descriptors_dma);
err_free_ring:
	kfree(ring);

	return NULL;
}

/**
 * tb_ring_alloc_tx() - Allocate DMA ring for transmit
 * @nhi: Pointer to the NHI the ring is to be allocated
 * @hop: HopID (ring) to allocate
 * @size: Number of entries in the ring
 * @flags: Flags for the ring
 */
struct tb_ring *tb_ring_alloc_tx(struct tb_nhi *nhi, int hop, int size,
				 unsigned int flags)
{
	return tb_ring_alloc(nhi, hop, size, true, flags, 0, 0, 0, NULL, NULL);
}
EXPORT_SYMBOL_GPL(tb_ring_alloc_tx);

/**
 * tb_ring_alloc_rx() - Allocate DMA ring for receive
 * @nhi: Pointer to the NHI the ring is to be allocated
 * @hop: HopID (ring) to allocate. Pass %-1 for automatic allocation.
 * @size: Number of entries in the ring
 * @flags: Flags for the ring
 * @e2e_tx_hop: Transmit HopID when E2E is enabled in @flags
 * @sof_mask: Mask of PDF values that start a frame
 * @eof_mask: Mask of PDF values that end a frame
 * @start_poll: If not %NULL the ring will call this function when an
 *		interrupt is triggered and masked, instead of callback
 *		in each Rx frame.
 * @poll_data: Optional data passed to @start_poll
 */
struct tb_ring *tb_ring_alloc_rx(struct tb_nhi *nhi, int hop, int size,
				 unsigned int flags, int e2e_tx_hop,
				 u16 sof_mask, u16 eof_mask,
				 void (*start_poll)(void *), void *poll_data)
{
	return tb_ring_alloc(nhi, hop, size, false, flags, e2e_tx_hop, sof_mask, eof_mask,
			     start_poll, poll_data);
}
EXPORT_SYMBOL_GPL(tb_ring_alloc_rx);

/**
 * tb_ring_start() - enable a ring
 * @ring: Ring to start
 *
 * Must not be invoked in parallel with tb_ring_stop().
 */
void tb_ring_start(struct tb_ring *ring)
{
	u16 frame_size;
	u32 flags;

	spin_lock_irq(&ring->nhi->lock);
	spin_lock(&ring->lock);
	if (ring->nhi->going_away)
		goto err;
	if (ring->running) {
		dev_WARN(&ring->nhi->pdev->dev, "ring already started\n");
		goto err;
	}
	dev_dbg(&ring->nhi->pdev->dev, "starting %s %d\n",
		RING_TYPE(ring), ring->hop);

	if (ring->flags & RING_FLAG_FRAME) {
		/* Means 4096 */
		frame_size = 0;
		flags = RING_FLAG_ENABLE;
	} else {
		frame_size = TB_FRAME_SIZE;
		flags = RING_FLAG_ENABLE | RING_FLAG_RAW;
	}

	ring_iowrite64desc(ring, ring->descriptors_dma, 0);
	if (ring->is_tx) {
		ring_iowrite32desc(ring, ring->size, 12);
		ring_iowrite32options(ring, 0, 4); /* time releated ? */
		ring_iowrite32options(ring, flags, 0);
	} else {
		u32 sof_eof_mask = ring->sof_mask << 16 | ring->eof_mask;

		ring_iowrite32desc(ring, (frame_size << 16) | ring->size, 12);
		ring_iowrite32options(ring, sof_eof_mask, 4);
		ring_iowrite32options(ring, flags, 0);
	}

	/*
	 * Now that the ring valid bit is set we can configure E2E if
	 * enabled for the ring.
	 */
	if (ring->flags & RING_FLAG_E2E) {
		if (!ring->is_tx) {
			u32 hop;

			hop = ring->e2e_tx_hop << REG_RX_OPTIONS_E2E_HOP_SHIFT;
			hop &= REG_RX_OPTIONS_E2E_HOP_MASK;
			flags |= hop;

			dev_dbg(&ring->nhi->pdev->dev,
				"enabling E2E for %s %d with TX HopID %d\n",
				RING_TYPE(ring), ring->hop, ring->e2e_tx_hop);
		} else {
			dev_dbg(&ring->nhi->pdev->dev, "enabling E2E for %s %d\n",
				RING_TYPE(ring), ring->hop);
		}

		flags |= RING_FLAG_E2E_FLOW_CONTROL;
		ring_iowrite32options(ring, flags, 0);
	}

	ring_interrupt_active(ring, true);
	ring->running = true;
err:
	spin_unlock(&ring->lock);
	spin_unlock_irq(&ring->nhi->lock);
}
EXPORT_SYMBOL_GPL(tb_ring_start);

/**
 * tb_ring_stop() - shutdown a ring
 * @ring: Ring to stop
 *
 * Must not be invoked from a callback.
 *
 * This method will disable the ring. Further calls to
 * tb_ring_tx/tb_ring_rx will return -ESHUTDOWN until ring_stop has been
 * called.
 *
 * All enqueued frames will be canceled and their callbacks will be executed
 * with frame->canceled set to true (on the callback thread). This method
 * returns only after all callback invocations have finished.
 */
void tb_ring_stop(struct tb_ring *ring)
{
	spin_lock_irq(&ring->nhi->lock);
	spin_lock(&ring->lock);
	dev_dbg(&ring->nhi->pdev->dev, "stopping %s %d\n",
		RING_TYPE(ring), ring->hop);
	if (ring->nhi->going_away)
		goto err;
	if (!ring->running) {
		dev_WARN(&ring->nhi->pdev->dev, "%s %d already stopped\n",
			 RING_TYPE(ring), ring->hop);
		goto err;
	}
	ring_interrupt_active(ring, false);

	ring_iowrite32options(ring, 0, 0);
	ring_iowrite64desc(ring, 0, 0);
	ring_iowrite32desc(ring, 0, 8);
	ring_iowrite32desc(ring, 0, 12);
	ring->head = 0;
	ring->tail = 0;
	ring->running = false;

err:
	spin_unlock(&ring->lock);
	spin_unlock_irq(&ring->nhi->lock);

	/*
	 * schedule ring->work to invoke callbacks on all remaining frames.
	 */
	schedule_work(&ring->work);
	flush_work(&ring->work);
}
EXPORT_SYMBOL_GPL(tb_ring_stop);

/*
 * tb_ring_free() - free ring
 *
 * When this method returns all invocations of ring->callback will have
 * finished.
 *
 * Ring must be stopped.
 *
 * Must NOT be called from ring_frame->callback!
 */
void tb_ring_free(struct tb_ring *ring)
{
	spin_lock_irq(&ring->nhi->lock);
	/*
	 * Dissociate the ring from the NHI. This also ensures that
	 * nhi_interrupt_work cannot reschedule ring->work.
	 */
	if (ring->is_tx)
		ring->nhi->tx_rings[ring->hop] = NULL;
	else
		ring->nhi->rx_rings[ring->hop] = NULL;

	if (ring->running) {
		dev_WARN(&ring->nhi->pdev->dev, "%s %d still running\n",
			 RING_TYPE(ring), ring->hop);
	}
	spin_unlock_irq(&ring->nhi->lock);

	ring_release_msix(ring);

	dma_free_coherent(&ring->nhi->pdev->dev,
			  ring->size * sizeof(*ring->descriptors),
			  ring->descriptors, ring->descriptors_dma);

	ring->descriptors = NULL;
	ring->descriptors_dma = 0;


	dev_dbg(&ring->nhi->pdev->dev, "freeing %s %d\n", RING_TYPE(ring),
		ring->hop);

	/*
	 * ring->work can no longer be scheduled (it is scheduled only
	 * by nhi_interrupt_work, ring_stop and ring_msix). Wait for it
	 * to finish before freeing the ring.
	 */
	flush_work(&ring->work);
	kfree(ring);
}
EXPORT_SYMBOL_GPL(tb_ring_free);

/**
 * nhi_mailbox_cmd() - Send a command through NHI mailbox
 * @nhi: Pointer to the NHI structure
 * @cmd: Command to send
 * @data: Data to be send with the command
 *
 * Sends mailbox command to the firmware running on NHI. Returns %0 in
 * case of success and negative errno in case of failure.
 */
int nhi_mailbox_cmd(struct tb_nhi *nhi, enum nhi_mailbox_cmd cmd, u32 data)
{
	ktime_t timeout;
	u32 val;

	iowrite32(data, nhi->iobase + REG_INMAIL_DATA);

	val = ioread32(nhi->iobase + REG_INMAIL_CMD);
	val &= ~(REG_INMAIL_CMD_MASK | REG_INMAIL_ERROR);
	val |= REG_INMAIL_OP_REQUEST | cmd;
	iowrite32(val, nhi->iobase + REG_INMAIL_CMD);

	timeout = ktime_add_ms(ktime_get(), NHI_MAILBOX_TIMEOUT);
	do {
		val = ioread32(nhi->iobase + REG_INMAIL_CMD);
		if (!(val & REG_INMAIL_OP_REQUEST))
			break;
		usleep_range(10, 20);
	} while (ktime_before(ktime_get(), timeout));

	if (val & REG_INMAIL_OP_REQUEST)
		return -ETIMEDOUT;
	if (val & REG_INMAIL_ERROR)
		return -EIO;

	return 0;
}

/**
 * nhi_mailbox_mode() - Return current firmware operation mode
 * @nhi: Pointer to the NHI structure
 *
 * The function reads current firmware operation mode using NHI mailbox
 * registers and returns it to the caller.
 */
enum nhi_fw_mode nhi_mailbox_mode(struct tb_nhi *nhi)
{
	u32 val;

	val = ioread32(nhi->iobase + REG_OUTMAIL_CMD);
	val &= REG_OUTMAIL_CMD_OPMODE_MASK;
	val >>= REG_OUTMAIL_CMD_OPMODE_SHIFT;

	return (enum nhi_fw_mode)val;
}

static void nhi_interrupt_work(struct work_struct *work)
{
	struct tb_nhi *nhi = container_of(work, typeof(*nhi), interrupt_work);
	int value = 0; /* Suppress uninitialized usage warning. */
	int bit;
	int hop = -1;
	int type = 0; /* current interrupt type 0: TX, 1: RX, 2: RX overflow */
	struct tb_ring *ring;

	spin_lock_irq(&nhi->lock);

	/*
	 * Starting at REG_RING_NOTIFY_BASE there are three status bitfields
	 * (TX, RX, RX overflow). We iterate over the bits and read a new
	 * dwords as required. The registers are cleared on read.
	 */
	for (bit = 0; bit < 3 * nhi->hop_count; bit++) {
		if (bit % 32 == 0)
			value = ioread32(nhi->iobase
					 + REG_RING_NOTIFY_BASE
					 + 4 * (bit / 32));
		if (++hop == nhi->hop_count) {
			hop = 0;
			type++;
		}
		if ((value & (1 << (bit % 32))) == 0)
			continue;
		if (type == 2) {
			dev_warn(&nhi->pdev->dev,
				 "RX overflow for ring %d\n",
				 hop);
			continue;
		}
		if (type == 0)
			ring = nhi->tx_rings[hop];
		else
			ring = nhi->rx_rings[hop];
		if (ring == NULL) {
			dev_warn(&nhi->pdev->dev,
				 "got interrupt for inactive %s ring %d\n",
				 type ? "RX" : "TX",
				 hop);
			continue;
		}

		spin_lock(&ring->lock);
		__ring_interrupt(ring);
		spin_unlock(&ring->lock);
	}
	spin_unlock_irq(&nhi->lock);
}

static irqreturn_t nhi_msi(int irq, void *data)
{
	struct tb_nhi *nhi = data;
	schedule_work(&nhi->interrupt_work);
	return IRQ_HANDLED;
}

static int __nhi_suspend_noirq(struct device *dev, bool wakeup)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct tb *tb = pci_get_drvdata(pdev);
	struct tb_nhi *nhi = tb->nhi;
	int ret;

	ret = tb_domain_suspend_noirq(tb);
	if (ret)
		return ret;

	if (nhi->ops && nhi->ops->suspend_noirq) {
		ret = nhi->ops->suspend_noirq(tb->nhi, wakeup);
		if (ret)
			return ret;
	}

	return 0;
}

static int nhi_suspend_noirq(struct device *dev)
{
	return __nhi_suspend_noirq(dev, device_may_wakeup(dev));
}

static int nhi_freeze_noirq(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct tb *tb = pci_get_drvdata(pdev);

	return tb_domain_freeze_noirq(tb);
}

static int nhi_thaw_noirq(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct tb *tb = pci_get_drvdata(pdev);

	return tb_domain_thaw_noirq(tb);
}

static bool nhi_wake_supported(struct pci_dev *pdev)
{
	u8 val;

	/*
	 * If power rails are sustainable for wakeup from S4 this
	 * property is set by the BIOS.
	 */
	if (device_property_read_u8(&pdev->dev, "WAKE_SUPPORTED", &val))
		return !!val;

	return true;
}

static int nhi_poweroff_noirq(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	bool wakeup;

	wakeup = device_may_wakeup(dev) && nhi_wake_supported(pdev);
	return __nhi_suspend_noirq(dev, wakeup);
}

static void nhi_enable_int_throttling(struct tb_nhi *nhi)
{
	/* Throttling is specified in 256ns increments */
	u32 throttle = DIV_ROUND_UP(128 * NSEC_PER_USEC, 256);
	unsigned int i;

	/*
	 * Configure interrupt throttling for all vectors even if we
	 * only use few.
	 */
	for (i = 0; i < MSIX_MAX_VECS; i++) {
		u32 reg = REG_INT_THROTTLING_RATE + i * 4;
		iowrite32(throttle, nhi->iobase + reg);
	}
}

static int nhi_resume_noirq(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct tb *tb = pci_get_drvdata(pdev);
	struct tb_nhi *nhi = tb->nhi;
	int ret;

	/*
	 * Check that the device is still there. It may be that the user
	 * unplugged last device which causes the host controller to go
	 * away on PCs.
	 */
	if (!pci_device_is_present(pdev)) {
		nhi->going_away = true;
	} else {
		if (nhi->ops && nhi->ops->resume_noirq) {
			ret = nhi->ops->resume_noirq(nhi);
			if (ret)
				return ret;
		}
		nhi_enable_int_throttling(tb->nhi);
	}

	return tb_domain_resume_noirq(tb);
}

static int nhi_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct tb *tb = pci_get_drvdata(pdev);

	return tb_domain_suspend(tb);
}

static void nhi_complete(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct tb *tb = pci_get_drvdata(pdev);

	/*
	 * If we were runtime suspended when system suspend started,
	 * schedule runtime resume now. It should bring the domain back
	 * to functional state.
	 */
	if (pm_runtime_suspended(&pdev->dev))
		pm_runtime_resume(&pdev->dev);
	else
		tb_domain_complete(tb);
}

static int nhi_runtime_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct tb *tb = pci_get_drvdata(pdev);
	struct tb_nhi *nhi = tb->nhi;
	int ret;

	ret = tb_domain_runtime_suspend(tb);
	if (ret)
		return ret;

	if (nhi->ops && nhi->ops->runtime_suspend) {
		ret = nhi->ops->runtime_suspend(tb->nhi);
		if (ret)
			return ret;
	}
	return 0;
}

static int nhi_runtime_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct tb *tb = pci_get_drvdata(pdev);
	struct tb_nhi *nhi = tb->nhi;
	int ret;

	if (nhi->ops && nhi->ops->runtime_resume) {
		ret = nhi->ops->runtime_resume(nhi);
		if (ret)
			return ret;
	}

	nhi_enable_int_throttling(nhi);
	return tb_domain_runtime_resume(tb);
}

static void nhi_shutdown(struct tb_nhi *nhi)
{
	int i;

	dev_dbg(&nhi->pdev->dev, "shutdown\n");

	for (i = 0; i < nhi->hop_count; i++) {
		if (nhi->tx_rings[i])
			dev_WARN(&nhi->pdev->dev,
				 "TX ring %d is still active\n", i);
		if (nhi->rx_rings[i])
			dev_WARN(&nhi->pdev->dev,
				 "RX ring %d is still active\n", i);
	}
	nhi_disable_interrupts(nhi);
	/*
	 * We have to release the irq before calling flush_work. Otherwise an
	 * already executing IRQ handler could call schedule_work again.
	 */
	if (!nhi->pdev->msix_enabled) {
		devm_free_irq(&nhi->pdev->dev, nhi->pdev->irq, nhi);
		flush_work(&nhi->interrupt_work);
	}
	ida_destroy(&nhi->msix_ida);

	if (nhi->ops && nhi->ops->shutdown)
		nhi->ops->shutdown(nhi);
}

static void nhi_check_quirks(struct tb_nhi *nhi)
{
	if (nhi->pdev->vendor == PCI_VENDOR_ID_INTEL) {
		/*
		 * Intel hardware supports auto clear of the interrupt
		 * status register right after interrupt is being
		 * issued.
		 */
		nhi->quirks |= QUIRK_AUTO_CLEAR_INT;

		switch (nhi->pdev->device) {
		case PCI_DEVICE_ID_INTEL_FALCON_RIDGE_2C_NHI:
		case PCI_DEVICE_ID_INTEL_FALCON_RIDGE_4C_NHI:
			/*
			 * Falcon Ridge controller needs the end-to-end
			 * flow control workaround to avoid losing Rx
			 * packets when RING_FLAG_E2E is set.
			 */
			nhi->quirks |= QUIRK_E2E;
			break;
		}
	}
}

static int nhi_check_iommu_pdev(struct pci_dev *pdev, void *data)
{
	if (!pdev->external_facing ||
	    !device_iommu_capable(&pdev->dev, IOMMU_CAP_PRE_BOOT_PROTECTION))
		return 0;
	*(bool *)data = true;
	return 1; /* Stop walking */
}

static void nhi_check_iommu(struct tb_nhi *nhi)
{
	struct pci_bus *bus = nhi->pdev->bus;
	bool port_ok = false;

	/*
	 * Ideally what we'd do here is grab every PCI device that
	 * represents a tunnelling adapter for this NHI and check their
	 * status directly, but unfortunately USB4 seems to make it
	 * obnoxiously difficult to reliably make any correlation.
	 *
	 * So for now we'll have to bodge it... Hoping that the system
	 * is at least sane enough that an adapter is in the same PCI
	 * segment as its NHI, if we can find *something* on that segment
	 * which meets the requirements for Kernel DMA Protection, we'll
	 * take that to imply that firmware is aware and has (hopefully)
	 * done the right thing in general. We need to know that the PCI
	 * layer has seen the ExternalFacingPort property which will then
	 * inform the IOMMU layer to enforce the complete "untrusted DMA"
	 * flow, but also that the IOMMU driver itself can be trusted not
	 * to have been subverted by a pre-boot DMA attack.
	 */
	while (bus->parent)
		bus = bus->parent;

	pci_walk_bus(bus, nhi_check_iommu_pdev, &port_ok);

	nhi->iommu_dma_protection = port_ok;
	dev_dbg(&nhi->pdev->dev, "IOMMU DMA protection is %s\n",
		str_enabled_disabled(port_ok));
}

static void nhi_reset(struct tb_nhi *nhi)
{
	ktime_t timeout;
	u32 val;

	val = ioread32(nhi->iobase + REG_CAPS);
	/* Reset only v2 and later routers */
	if (FIELD_GET(REG_CAPS_VERSION_MASK, val) < REG_CAPS_VERSION_2)
		return;

	if (!host_reset) {
		dev_dbg(&nhi->pdev->dev, "skipping host router reset\n");
		return;
	}

	iowrite32(REG_RESET_HRR, nhi->iobase + REG_RESET);
	msleep(100);

	timeout = ktime_add_ms(ktime_get(), 500);
	do {
		val = ioread32(nhi->iobase + REG_RESET);
		if (!(val & REG_RESET_HRR)) {
			dev_warn(&nhi->pdev->dev, "host router reset successful\n");
			return;
		}
		usleep_range(10, 20);
	} while (ktime_before(ktime_get(), timeout));

	dev_warn(&nhi->pdev->dev, "timeout resetting host router\n");
}

static int nhi_init_msi(struct tb_nhi *nhi)
{
	struct pci_dev *pdev = nhi->pdev;
	struct device *dev = &pdev->dev;
	int res, irq, nvec;

	/* In case someone left them on. */
	nhi_disable_interrupts(nhi);

	nhi_enable_int_throttling(nhi);

	ida_init(&nhi->msix_ida);

	/*
	 * The NHI has 16 MSI-X vectors or a single MSI. We first try to
	 * get all MSI-X vectors and if we succeed, each ring will have
	 * one MSI-X. If for some reason that does not work out, we
	 * fallback to a single MSI.
	 */
	nvec = pci_alloc_irq_vectors(pdev, MSIX_MIN_VECS, MSIX_MAX_VECS,
				     PCI_IRQ_MSIX);
	if (nvec < 0) {
		nvec = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
		if (nvec < 0)
			return nvec;

		INIT_WORK(&nhi->interrupt_work, nhi_interrupt_work);

		irq = pci_irq_vector(nhi->pdev, 0);
		if (irq < 0)
			return irq;

		res = devm_request_irq(&pdev->dev, irq, nhi_msi,
				       IRQF_NO_SUSPEND, "thunderbolt", nhi);
		if (res)
			return dev_err_probe(dev, res, "request_irq failed, aborting\n");
	}

	return 0;
}

static bool nhi_imr_valid(struct pci_dev *pdev)
{
	u8 val;

	if (!device_property_read_u8(&pdev->dev, "IMR_VALID", &val))
		return !!val;

	return true;
}

static struct tb *nhi_select_cm(struct tb_nhi *nhi)
{
	struct tb *tb;

	/*
	 * USB4 case is simple. If we got control of any of the
	 * capabilities, we use software CM.
	 */
	if (tb_acpi_is_native())
		return tb_probe(nhi);

	/*
	 * Either firmware based CM is running (we did not get control
	 * from the firmware) or this is pre-USB4 PC so try first
	 * firmware CM and then fallback to software CM.
	 */
	tb = icm_probe(nhi);
	if (!tb)
		tb = tb_probe(nhi);

	return tb;
}

static int nhi_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct tb_nhi *nhi;
	struct tb *tb;
	int res;

	if (!nhi_imr_valid(pdev))
		return dev_err_probe(dev, -ENODEV, "firmware image not valid, aborting\n");

	res = pcim_enable_device(pdev);
	if (res)
		return dev_err_probe(dev, res, "cannot enable PCI device, aborting\n");

	res = pcim_iomap_regions(pdev, 1 << 0, "thunderbolt");
	if (res)
		return dev_err_probe(dev, res, "cannot obtain PCI resources, aborting\n");

	nhi = devm_kzalloc(&pdev->dev, sizeof(*nhi), GFP_KERNEL);
	if (!nhi)
		return -ENOMEM;

	nhi->pdev = pdev;
	nhi->ops = (const struct tb_nhi_ops *)id->driver_data;
	/* cannot fail - table is allocated in pcim_iomap_regions */
	nhi->iobase = pcim_iomap_table(pdev)[0];
	nhi->hop_count = ioread32(nhi->iobase + REG_CAPS) & 0x3ff;
	dev_dbg(dev, "total paths: %d\n", nhi->hop_count);

	nhi->tx_rings = devm_kcalloc(&pdev->dev, nhi->hop_count,
				     sizeof(*nhi->tx_rings), GFP_KERNEL);
	nhi->rx_rings = devm_kcalloc(&pdev->dev, nhi->hop_count,
				     sizeof(*nhi->rx_rings), GFP_KERNEL);
	if (!nhi->tx_rings || !nhi->rx_rings)
		return -ENOMEM;

	nhi_check_quirks(nhi);
	nhi_check_iommu(nhi);
	nhi_reset(nhi);

	res = nhi_init_msi(nhi);
	if (res)
		return dev_err_probe(dev, res, "cannot enable MSI, aborting\n");

	spin_lock_init(&nhi->lock);

	res = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (res)
		return dev_err_probe(dev, res, "failed to set DMA mask\n");

	pci_set_master(pdev);

	if (nhi->ops && nhi->ops->init) {
		res = nhi->ops->init(nhi);
		if (res)
			return res;
	}

	tb = nhi_select_cm(nhi);
	if (!tb)
		return dev_err_probe(dev, -ENODEV,
			"failed to determine connection manager, aborting\n");

	dev_dbg(dev, "NHI initialized, starting thunderbolt\n");

	res = tb_domain_add(tb, host_reset);
	if (res) {
		/*
		 * At this point the RX/TX rings might already have been
		 * activated. Do a proper shutdown.
		 */
		tb_domain_put(tb);
		nhi_shutdown(nhi);
		return res;
	}
	pci_set_drvdata(pdev, tb);

	device_wakeup_enable(&pdev->dev);

	pm_runtime_allow(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, TB_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	return 0;
}

static void nhi_remove(struct pci_dev *pdev)
{
	struct tb *tb = pci_get_drvdata(pdev);
	struct tb_nhi *nhi = tb->nhi;

	pm_runtime_get_sync(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_forbid(&pdev->dev);

	tb_domain_remove(tb);
	nhi_shutdown(nhi);
}

/*
 * The tunneled pci bridges are siblings of us. Use resume_noirq to reenable
 * the tunnels asap. A corresponding pci quirk blocks the downstream bridges
 * resume_noirq until we are done.
 */
static const struct dev_pm_ops nhi_pm_ops = {
	.suspend_noirq = nhi_suspend_noirq,
	.resume_noirq = nhi_resume_noirq,
	.freeze_noirq = nhi_freeze_noirq,  /*
					    * we just disable hotplug, the
					    * pci-tunnels stay alive.
					    */
	.thaw_noirq = nhi_thaw_noirq,
	.restore_noirq = nhi_resume_noirq,
	.suspend = nhi_suspend,
	.poweroff_noirq = nhi_poweroff_noirq,
	.poweroff = nhi_suspend,
	.complete = nhi_complete,
	.runtime_suspend = nhi_runtime_suspend,
	.runtime_resume = nhi_runtime_resume,
};

static struct pci_device_id nhi_ids[] = {
	/*
	 * We have to specify class, the TB bridges use the same device and
	 * vendor (sub)id on gen 1 and gen 2 controllers.
	 */
	{
		.class = PCI_CLASS_SYSTEM_OTHER << 8, .class_mask = ~0,
		.vendor = PCI_VENDOR_ID_INTEL,
		.device = PCI_DEVICE_ID_INTEL_LIGHT_RIDGE,
		.subvendor = 0x2222, .subdevice = 0x1111,
	},
	{
		.class = PCI_CLASS_SYSTEM_OTHER << 8, .class_mask = ~0,
		.vendor = PCI_VENDOR_ID_INTEL,
		.device = PCI_DEVICE_ID_INTEL_CACTUS_RIDGE_4C,
		.subvendor = 0x2222, .subdevice = 0x1111,
	},
	{
		.class = PCI_CLASS_SYSTEM_OTHER << 8, .class_mask = ~0,
		.vendor = PCI_VENDOR_ID_INTEL,
		.device = PCI_DEVICE_ID_INTEL_FALCON_RIDGE_2C_NHI,
		.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID,
	},
	{
		.class = PCI_CLASS_SYSTEM_OTHER << 8, .class_mask = ~0,
		.vendor = PCI_VENDOR_ID_INTEL,
		.device = PCI_DEVICE_ID_INTEL_FALCON_RIDGE_4C_NHI,
		.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID,
	},

	/* Thunderbolt 3 */
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_2C_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_4C_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_USBONLY_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_USBONLY_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_USBONLY_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_TITAN_RIDGE_2C_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_TITAN_RIDGE_4C_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ICL_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ICL_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	/* Thunderbolt 4 */
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_TGL_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_TGL_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_TGL_H_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_TGL_H_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ADL_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ADL_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_RPL_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_RPL_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_MTL_M_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_MTL_P_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_MTL_P_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_LNL_NHI0),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_LNL_NHI1),
	  .driver_data = (kernel_ulong_t)&icl_nhi_ops },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_BARLOW_RIDGE_HOST_80G_NHI) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_BARLOW_RIDGE_HOST_40G_NHI) },

	/* Any USB4 compliant host */
	{ PCI_DEVICE_CLASS(PCI_CLASS_SERIAL_USB_USB4, ~0) },

	{ 0,}
};

MODULE_DEVICE_TABLE(pci, nhi_ids);
MODULE_DESCRIPTION("Thunderbolt/USB4 core driver");
MODULE_LICENSE("GPL");

static struct pci_driver nhi_driver = {
	.name = "thunderbolt",
	.id_table = nhi_ids,
	.probe = nhi_probe,
	.remove = nhi_remove,
	.shutdown = nhi_remove,
	.driver.pm = &nhi_pm_ops,
};

static int __init nhi_init(void)
{
	int ret;

	ret = tb_domain_init();
	if (ret)
		return ret;
	ret = pci_register_driver(&nhi_driver);
	if (ret)
		tb_domain_exit();
	return ret;
}

static void __exit nhi_unload(void)
{
	pci_unregister_driver(&nhi_driver);
	tb_domain_exit();
}

rootfs_initcall(nhi_init);
module_exit(nhi_unload);
