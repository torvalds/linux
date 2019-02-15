// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

#include <linux/irqreturn.h>

/*
 * hl_cq_inc_ptr - increment ci or pi of cq
 *
 * @ptr: the current ci or pi value of the completion queue
 *
 * Increment ptr by 1. If it reaches the number of completion queue
 * entries, set it to 0
 */
inline u32 hl_cq_inc_ptr(u32 ptr)
{
	ptr++;
	if (unlikely(ptr == HL_CQ_LENGTH))
		ptr = 0;
	return ptr;
}

/*
 * hl_irq_handler_cq - irq handler for completion queue
 *
 * @irq: irq number
 * @arg: pointer to completion queue structure
 *
 */
irqreturn_t hl_irq_handler_cq(int irq, void *arg)
{
	struct hl_cq *cq = arg;
	struct hl_device *hdev = cq->hdev;
	struct hl_hw_queue *queue;
	struct hl_cs_job *job;
	bool shadow_index_valid;
	u16 shadow_index;
	u32 *cq_entry;
	u32 *cq_base;

	if (hdev->disabled) {
		dev_dbg(hdev->dev,
			"Device disabled but received IRQ %d for CQ %d\n",
			irq, cq->hw_queue_id);
		return IRQ_HANDLED;
	}

	cq_base = (u32 *) (uintptr_t) cq->kernel_address;

	while (1) {
		bool entry_ready = ((cq_base[cq->ci] & CQ_ENTRY_READY_MASK)
						>> CQ_ENTRY_READY_SHIFT);

		if (!entry_ready)
			break;

		cq_entry = (u32 *) &cq_base[cq->ci];

		/*
		 * Make sure we read CQ entry contents after we've
		 * checked the ownership bit.
		 */
		dma_rmb();

		shadow_index_valid =
			((*cq_entry & CQ_ENTRY_SHADOW_INDEX_VALID_MASK)
					>> CQ_ENTRY_SHADOW_INDEX_VALID_SHIFT);

		shadow_index = (u16)
			((*cq_entry & CQ_ENTRY_SHADOW_INDEX_MASK)
					>> CQ_ENTRY_SHADOW_INDEX_SHIFT);

		queue = &hdev->kernel_queues[cq->hw_queue_id];

		if ((shadow_index_valid) && (!hdev->disabled)) {
			job = queue->shadow_queue[hl_pi_2_offset(shadow_index)];
			queue_work(hdev->cq_wq, &job->finish_work);
		}

		/*
		 * Update ci of the context's queue. There is no
		 * need to protect it with spinlock because this update is
		 * done only inside IRQ and there is a different IRQ per
		 * queue
		 */
		queue->ci = hl_queue_inc_ptr(queue->ci);

		/* Clear CQ entry ready bit */
		cq_base[cq->ci] &= ~CQ_ENTRY_READY_MASK;

		cq->ci = hl_cq_inc_ptr(cq->ci);

		/* Increment free slots */
		atomic_inc(&cq->free_slots_cnt);
	}

	return IRQ_HANDLED;
}

/*
 * hl_cq_init - main initialization function for an cq object
 *
 * @hdev: pointer to device structure
 * @q: pointer to cq structure
 * @hw_queue_id: The H/W queue ID this completion queue belongs to
 *
 * Allocate dma-able memory for the completion queue and initialize fields
 * Returns 0 on success
 */
int hl_cq_init(struct hl_device *hdev, struct hl_cq *q, u32 hw_queue_id)
{
	void *p;

	BUILD_BUG_ON(HL_CQ_SIZE_IN_BYTES > HL_PAGE_SIZE);

	p = hdev->asic_funcs->dma_alloc_coherent(hdev, HL_CQ_SIZE_IN_BYTES,
				&q->bus_address, GFP_KERNEL | __GFP_ZERO);
	if (!p)
		return -ENOMEM;

	q->hdev = hdev;
	q->kernel_address = (u64) (uintptr_t) p;
	q->hw_queue_id = hw_queue_id;
	q->ci = 0;
	q->pi = 0;

	atomic_set(&q->free_slots_cnt, HL_CQ_LENGTH);

	return 0;
}

/*
 * hl_cq_fini - destroy completion queue
 *
 * @hdev: pointer to device structure
 * @q: pointer to cq structure
 *
 * Free the completion queue memory
 */
void hl_cq_fini(struct hl_device *hdev, struct hl_cq *q)
{
	hdev->asic_funcs->dma_free_coherent(hdev, HL_CQ_SIZE_IN_BYTES,
			(void *) (uintptr_t) q->kernel_address, q->bus_address);
}
