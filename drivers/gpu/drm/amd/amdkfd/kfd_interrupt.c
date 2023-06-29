// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2014-2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * KFD Interrupts.
 *
 * AMD GPUs deliver interrupts by pushing an interrupt description onto the
 * interrupt ring and then sending an interrupt. KGD receives the interrupt
 * in ISR and sends us a pointer to each new entry on the interrupt ring.
 *
 * We generally can't process interrupt-signaled events from ISR, so we call
 * out to each interrupt client module (currently only the scheduler) to ask if
 * each interrupt is interesting. If they return true, then it requires further
 * processing so we copy it to an internal interrupt ring and call each
 * interrupt client again from a work-queue.
 *
 * There's no acknowledgment for the interrupts we use. The hardware simply
 * queues a new interrupt each time without waiting.
 *
 * The fixed-size internal queue means that it's possible for us to lose
 * interrupts because we have no back-pressure to the hardware.
 */

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/kfifo.h>
#include "kfd_priv.h"

#define KFD_IH_NUM_ENTRIES 8192

static void interrupt_wq(struct work_struct *);

int kfd_interrupt_init(struct kfd_node *node)
{
	int r;

	r = kfifo_alloc(&node->ih_fifo,
		KFD_IH_NUM_ENTRIES * node->kfd->device_info.ih_ring_entry_size,
		GFP_KERNEL);
	if (r) {
		dev_err(node->adev->dev, "Failed to allocate IH fifo\n");
		return r;
	}

	node->ih_wq = alloc_workqueue("KFD IH", WQ_HIGHPRI, 1);
	if (unlikely(!node->ih_wq)) {
		kfifo_free(&node->ih_fifo);
		dev_err(node->adev->dev, "Failed to allocate KFD IH workqueue\n");
		return -ENOMEM;
	}
	spin_lock_init(&node->interrupt_lock);

	INIT_WORK(&node->interrupt_work, interrupt_wq);

	node->interrupts_active = true;

	/*
	 * After this function returns, the interrupt will be enabled. This
	 * barrier ensures that the interrupt running on a different processor
	 * sees all the above writes.
	 */
	smp_wmb();

	return 0;
}

void kfd_interrupt_exit(struct kfd_node *node)
{
	/*
	 * Stop the interrupt handler from writing to the ring and scheduling
	 * workqueue items. The spinlock ensures that any interrupt running
	 * after we have unlocked sees interrupts_active = false.
	 */
	unsigned long flags;

	spin_lock_irqsave(&node->interrupt_lock, flags);
	node->interrupts_active = false;
	spin_unlock_irqrestore(&node->interrupt_lock, flags);

	/*
	 * flush_work ensures that there are no outstanding
	 * work-queue items that will access interrupt_ring. New work items
	 * can't be created because we stopped interrupt handling above.
	 */
	flush_workqueue(node->ih_wq);

	kfifo_free(&node->ih_fifo);
}

/*
 * Assumption: single reader/writer. This function is not re-entrant
 */
bool enqueue_ih_ring_entry(struct kfd_node *node, const void *ih_ring_entry)
{
	int count;

	count = kfifo_in(&node->ih_fifo, ih_ring_entry,
				node->kfd->device_info.ih_ring_entry_size);
	if (count != node->kfd->device_info.ih_ring_entry_size) {
		dev_dbg_ratelimited(node->adev->dev,
			"Interrupt ring overflow, dropping interrupt %d\n",
			count);
		return false;
	}

	return true;
}

/*
 * Assumption: single reader/writer. This function is not re-entrant
 */
static bool dequeue_ih_ring_entry(struct kfd_node *node, void *ih_ring_entry)
{
	int count;

	count = kfifo_out(&node->ih_fifo, ih_ring_entry,
				node->kfd->device_info.ih_ring_entry_size);

	WARN_ON(count && count != node->kfd->device_info.ih_ring_entry_size);

	return count == node->kfd->device_info.ih_ring_entry_size;
}

static void interrupt_wq(struct work_struct *work)
{
	struct kfd_node *dev = container_of(work, struct kfd_node,
						interrupt_work);
	uint32_t ih_ring_entry[KFD_MAX_RING_ENTRY_SIZE];
	unsigned long start_jiffies = jiffies;

	if (dev->kfd->device_info.ih_ring_entry_size > sizeof(ih_ring_entry)) {
		dev_err_once(dev->adev->dev, "Ring entry too small\n");
		return;
	}

	while (dequeue_ih_ring_entry(dev, ih_ring_entry)) {
		dev->kfd->device_info.event_interrupt_class->interrupt_wq(dev,
								ih_ring_entry);
		if (time_is_before_jiffies(start_jiffies + HZ)) {
			/* If we spent more than a second processing signals,
			 * reschedule the worker to avoid soft-lockup warnings
			 */
			queue_work(dev->ih_wq, &dev->interrupt_work);
			break;
		}
	}
}

bool interrupt_is_wanted(struct kfd_node *dev,
			const uint32_t *ih_ring_entry,
			uint32_t *patched_ihre, bool *flag)
{
	/* integer and bitwise OR so there is no boolean short-circuiting */
	unsigned int wanted = 0;

	wanted |= dev->kfd->device_info.event_interrupt_class->interrupt_isr(dev,
					 ih_ring_entry, patched_ihre, flag);

	return wanted != 0;
}
