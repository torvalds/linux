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

int kfd_interrupt_init(struct kfd_dev *kfd)
{
	int r;

	r = kfifo_alloc(&kfd->ih_fifo,
		KFD_IH_NUM_ENTRIES * kfd->device_info.ih_ring_entry_size,
		GFP_KERNEL);
	if (r) {
		dev_err(kfd->adev->dev, "Failed to allocate IH fifo\n");
		return r;
	}

	kfd->ih_wq = alloc_workqueue("KFD IH", WQ_HIGHPRI, 1);
	if (unlikely(!kfd->ih_wq)) {
		kfifo_free(&kfd->ih_fifo);
		dev_err(kfd->adev->dev, "Failed to allocate KFD IH workqueue\n");
		return -ENOMEM;
	}
	spin_lock_init(&kfd->interrupt_lock);

	INIT_WORK(&kfd->interrupt_work, interrupt_wq);

	kfd->interrupts_active = true;

	/*
	 * After this function returns, the interrupt will be enabled. This
	 * barrier ensures that the interrupt running on a different processor
	 * sees all the above writes.
	 */
	smp_wmb();

	return 0;
}

void kfd_interrupt_exit(struct kfd_dev *kfd)
{
	/*
	 * Stop the interrupt handler from writing to the ring and scheduling
	 * workqueue items. The spinlock ensures that any interrupt running
	 * after we have unlocked sees interrupts_active = false.
	 */
	unsigned long flags;

	spin_lock_irqsave(&kfd->interrupt_lock, flags);
	kfd->interrupts_active = false;
	spin_unlock_irqrestore(&kfd->interrupt_lock, flags);

	/*
	 * flush_work ensures that there are no outstanding
	 * work-queue items that will access interrupt_ring. New work items
	 * can't be created because we stopped interrupt handling above.
	 */
	flush_workqueue(kfd->ih_wq);

	kfifo_free(&kfd->ih_fifo);
}

/*
 * Assumption: single reader/writer. This function is not re-entrant
 */
bool enqueue_ih_ring_entry(struct kfd_dev *kfd,	const void *ih_ring_entry)
{
	int count;

	count = kfifo_in(&kfd->ih_fifo, ih_ring_entry,
				kfd->device_info.ih_ring_entry_size);
	if (count != kfd->device_info.ih_ring_entry_size) {
		dev_dbg_ratelimited(kfd->adev->dev,
			"Interrupt ring overflow, dropping interrupt %d\n",
			count);
		return false;
	}

	return true;
}

/*
 * Assumption: single reader/writer. This function is not re-entrant
 */
static bool dequeue_ih_ring_entry(struct kfd_dev *kfd, void *ih_ring_entry)
{
	int count;

	count = kfifo_out(&kfd->ih_fifo, ih_ring_entry,
				kfd->device_info.ih_ring_entry_size);

	WARN_ON(count && count != kfd->device_info.ih_ring_entry_size);

	return count == kfd->device_info.ih_ring_entry_size;
}

static void interrupt_wq(struct work_struct *work)
{
	struct kfd_dev *dev = container_of(work, struct kfd_dev,
						interrupt_work);
	uint32_t ih_ring_entry[KFD_MAX_RING_ENTRY_SIZE];
	long start_jiffies = jiffies;

	if (dev->device_info.ih_ring_entry_size > sizeof(ih_ring_entry)) {
		dev_err_once(dev->adev->dev, "Ring entry too small\n");
		return;
	}

	while (dequeue_ih_ring_entry(dev, ih_ring_entry)) {
		dev->device_info.event_interrupt_class->interrupt_wq(dev,
								ih_ring_entry);
		if (jiffies - start_jiffies > HZ) {
			/* If we spent more than a second processing signals,
			 * reschedule the worker to avoid soft-lockup warnings
			 */
			queue_work(dev->ih_wq, &dev->interrupt_work);
			break;
		}
	}
}

bool interrupt_is_wanted(struct kfd_dev *dev,
			const uint32_t *ih_ring_entry,
			uint32_t *patched_ihre, bool *flag)
{
	/* integer and bitwise OR so there is no boolean short-circuiting */
	unsigned int wanted = 0;

	wanted |= dev->device_info.event_interrupt_class->interrupt_isr(dev,
					 ih_ring_entry, patched_ihre, flag);

	return wanted != 0;
}
