// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

#include <linux/slab.h>

/**
 * This structure is used to schedule work of EQ entry and armcp_reset event
 *
 * @eq_work          - workqueue object to run when EQ entry is received
 * @hdev             - pointer to device structure
 * @eq_entry         - copy of the EQ entry
 */
struct hl_eqe_work {
	struct work_struct	eq_work;
	struct hl_device	*hdev;
	struct hl_eq_entry	eq_entry;
};

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
 * hl_eq_inc_ptr - increment ci of eq
 *
 * @ptr: the current ci value of the event queue
 *
 * Increment ptr by 1. If it reaches the number of event queue
 * entries, set it to 0
 */
inline u32 hl_eq_inc_ptr(u32 ptr)
{
	ptr++;
	if (unlikely(ptr == HL_EQ_LENGTH))
		ptr = 0;
	return ptr;
}

static void irq_handle_eqe(struct work_struct *work)
{
	struct hl_eqe_work *eqe_work = container_of(work, struct hl_eqe_work,
							eq_work);
	struct hl_device *hdev = eqe_work->hdev;

	hdev->asic_funcs->handle_eqe(hdev, &eqe_work->eq_entry);

	kfree(eqe_work);
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
	struct hl_cq_entry *cq_entry, *cq_base;

	if (hdev->disabled) {
		dev_dbg(hdev->dev,
			"Device disabled but received IRQ %d for CQ %d\n",
			irq, cq->hw_queue_id);
		return IRQ_HANDLED;
	}

	cq_base = (struct hl_cq_entry *) (uintptr_t) cq->kernel_address;

	while (1) {
		bool entry_ready = ((le32_to_cpu(cq_base[cq->ci].data) &
					CQ_ENTRY_READY_MASK)
						>> CQ_ENTRY_READY_SHIFT);

		if (!entry_ready)
			break;

		cq_entry = (struct hl_cq_entry *) &cq_base[cq->ci];

		/* Make sure we read CQ entry contents after we've
		 * checked the ownership bit.
		 */
		dma_rmb();

		shadow_index_valid = ((le32_to_cpu(cq_entry->data) &
					CQ_ENTRY_SHADOW_INDEX_VALID_MASK)
					>> CQ_ENTRY_SHADOW_INDEX_VALID_SHIFT);

		shadow_index = (u16) ((le32_to_cpu(cq_entry->data) &
					CQ_ENTRY_SHADOW_INDEX_MASK)
					>> CQ_ENTRY_SHADOW_INDEX_SHIFT);

		queue = &hdev->kernel_queues[cq->hw_queue_id];

		if ((shadow_index_valid) && (!hdev->disabled)) {
			job = queue->shadow_queue[hl_pi_2_offset(shadow_index)];
			queue_work(hdev->cq_wq, &job->finish_work);
		}

		/* Update ci of the context's queue. There is no
		 * need to protect it with spinlock because this update is
		 * done only inside IRQ and there is a different IRQ per
		 * queue
		 */
		queue->ci = hl_queue_inc_ptr(queue->ci);

		/* Clear CQ entry ready bit */
		cq_entry->data = cpu_to_le32(le32_to_cpu(cq_entry->data) &
						~CQ_ENTRY_READY_MASK);

		cq->ci = hl_cq_inc_ptr(cq->ci);

		/* Increment free slots */
		atomic_inc(&cq->free_slots_cnt);
	}

	return IRQ_HANDLED;
}

/*
 * hl_irq_handler_eq - irq handler for event queue
 *
 * @irq: irq number
 * @arg: pointer to event queue structure
 *
 */
irqreturn_t hl_irq_handler_eq(int irq, void *arg)
{
	struct hl_eq *eq = arg;
	struct hl_device *hdev = eq->hdev;
	struct hl_eq_entry *eq_entry;
	struct hl_eq_entry *eq_base;
	struct hl_eqe_work *handle_eqe_work;

	eq_base = (struct hl_eq_entry *) (uintptr_t) eq->kernel_address;

	while (1) {
		bool entry_ready =
			((le32_to_cpu(eq_base[eq->ci].hdr.ctl) &
				EQ_CTL_READY_MASK) >> EQ_CTL_READY_SHIFT);

		if (!entry_ready)
			break;

		eq_entry = &eq_base[eq->ci];

		/*
		 * Make sure we read EQ entry contents after we've
		 * checked the ownership bit.
		 */
		dma_rmb();

		if (hdev->disabled) {
			dev_warn(hdev->dev,
				"Device disabled but received IRQ %d for EQ\n",
					irq);
			goto skip_irq;
		}

		handle_eqe_work = kmalloc(sizeof(*handle_eqe_work), GFP_ATOMIC);
		if (handle_eqe_work) {
			INIT_WORK(&handle_eqe_work->eq_work, irq_handle_eqe);
			handle_eqe_work->hdev = hdev;

			memcpy(&handle_eqe_work->eq_entry, eq_entry,
					sizeof(*eq_entry));

			queue_work(hdev->eq_wq, &handle_eqe_work->eq_work);
		}
skip_irq:
		/* Clear EQ entry ready bit */
		eq_entry->hdr.ctl =
			cpu_to_le32(le32_to_cpu(eq_entry->hdr.ctl) &
							~EQ_CTL_READY_MASK);

		eq->ci = hl_eq_inc_ptr(eq->ci);

		hdev->asic_funcs->update_eq_ci(hdev, eq->ci);
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

	p = hdev->asic_funcs->asic_dma_alloc_coherent(hdev, HL_CQ_SIZE_IN_BYTES,
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
	hdev->asic_funcs->asic_dma_free_coherent(hdev, HL_CQ_SIZE_IN_BYTES,
			(void *) (uintptr_t) q->kernel_address, q->bus_address);
}

void hl_cq_reset(struct hl_device *hdev, struct hl_cq *q)
{
	q->ci = 0;
	q->pi = 0;

	atomic_set(&q->free_slots_cnt, HL_CQ_LENGTH);

	/*
	 * It's not enough to just reset the PI/CI because the H/W may have
	 * written valid completion entries before it was halted and therefore
	 * we need to clean the actual queues so we won't process old entries
	 * when the device is operational again
	 */

	memset((void *) (uintptr_t) q->kernel_address, 0, HL_CQ_SIZE_IN_BYTES);
}

/*
 * hl_eq_init - main initialization function for an event queue object
 *
 * @hdev: pointer to device structure
 * @q: pointer to eq structure
 *
 * Allocate dma-able memory for the event queue and initialize fields
 * Returns 0 on success
 */
int hl_eq_init(struct hl_device *hdev, struct hl_eq *q)
{
	void *p;

	BUILD_BUG_ON(HL_EQ_SIZE_IN_BYTES > HL_PAGE_SIZE);

	p = hdev->asic_funcs->cpu_accessible_dma_pool_alloc(hdev,
							HL_EQ_SIZE_IN_BYTES,
							&q->bus_address);
	if (!p)
		return -ENOMEM;

	q->hdev = hdev;
	q->kernel_address = (u64) (uintptr_t) p;
	q->ci = 0;

	return 0;
}

/*
 * hl_eq_fini - destroy event queue
 *
 * @hdev: pointer to device structure
 * @q: pointer to eq structure
 *
 * Free the event queue memory
 */
void hl_eq_fini(struct hl_device *hdev, struct hl_eq *q)
{
	flush_workqueue(hdev->eq_wq);

	hdev->asic_funcs->cpu_accessible_dma_pool_free(hdev,
					HL_EQ_SIZE_IN_BYTES,
					(void *) (uintptr_t) q->kernel_address);
}

void hl_eq_reset(struct hl_device *hdev, struct hl_eq *q)
{
	q->ci = 0;

	/*
	 * It's not enough to just reset the PI/CI because the H/W may have
	 * written valid completion entries before it was halted and therefore
	 * we need to clean the actual queues so we won't process old entries
	 * when the device is operational again
	 */

	memset((void *) (uintptr_t) q->kernel_address, 0, HL_EQ_SIZE_IN_BYTES);
}
