// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

#include <linux/slab.h>

/**
 * struct hl_eqe_work - This structure is used to schedule work of EQ
 *                      entry and cpucp_reset event
 *
 * @eq_work:          workqueue object to run when EQ entry is received
 * @hdev:             pointer to device structure
 * @eq_entry:         copy of the EQ entry
 */
struct hl_eqe_work {
	struct work_struct	eq_work;
	struct hl_device	*hdev;
	struct hl_eq_entry	eq_entry;
};

/**
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

/**
 * hl_eq_inc_ptr - increment ci of eq
 *
 * @ptr: the current ci value of the event queue
 *
 * Increment ptr by 1. If it reaches the number of event queue
 * entries, set it to 0
 */
static inline u32 hl_eq_inc_ptr(u32 ptr)
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

/**
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

	cq_base = cq->kernel_address;

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
			queue_work(hdev->cq_wq[cq->cq_idx], &job->finish_work);
		}

		atomic_inc(&queue->ci);

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
 * hl_ts_free_objects - handler of the free objects workqueue.
 * This function should put refcount to objects that the registration node
 * took refcount to them.
 * @work: workqueue object pointer
 */
static void hl_ts_free_objects(struct work_struct *work)
{
	struct timestamp_reg_work_obj *job =
			container_of(work, struct timestamp_reg_work_obj, free_obj);
	struct timestamp_reg_free_node *free_obj, *temp_free_obj;
	struct list_head *free_list_head = job->free_obj_head;
	struct hl_device *hdev = job->hdev;

	list_for_each_entry_safe(free_obj, temp_free_obj, free_list_head, free_objects_node) {
		dev_dbg(hdev->dev, "About to put refcount to ts_buff (%p) cq_cb(%p)\n",
					free_obj->ts_buff,
					free_obj->cq_cb);

		hl_ts_put(free_obj->ts_buff);
		hl_cb_put(free_obj->cq_cb);
		kfree(free_obj);
	}

	kfree(free_list_head);
	kfree(job);
}

/*
 * This function called with spin_lock of wait_list_lock taken
 * This function will set timestamp and delete the registration node from the
 * wait_list_lock.
 * and since we're protected with spin_lock here, so we cannot just put the refcount
 * for the objects here, since the release function may be called and it's also a long
 * logic (which might sleep also) that cannot be handled in irq context.
 * so here we'll be filling a list with nodes of "put" jobs and then will send this
 * list to a dedicated workqueue to do the actual put.
 */
static int handle_registration_node(struct hl_device *hdev, struct hl_user_pending_interrupt *pend,
						struct list_head **free_list)
{
	struct timestamp_reg_free_node *free_node;
	u64 timestamp;

	if (!(*free_list)) {
		/* Alloc/Init the timestamp registration free objects list */
		*free_list = kmalloc(sizeof(struct list_head), GFP_ATOMIC);
		if (!(*free_list))
			return -ENOMEM;

		INIT_LIST_HEAD(*free_list);
	}

	free_node = kmalloc(sizeof(*free_node), GFP_ATOMIC);
	if (!free_node)
		return -ENOMEM;

	timestamp = ktime_get_ns();

	*pend->ts_reg_info.timestamp_kernel_addr = timestamp;

	dev_dbg(hdev->dev, "Timestamp is set to ts cb address (%p), ts: 0x%llx\n",
			pend->ts_reg_info.timestamp_kernel_addr,
			*(u64 *)pend->ts_reg_info.timestamp_kernel_addr);

	list_del(&pend->wait_list_node);

	/* Mark kernel CB node as free */
	pend->ts_reg_info.in_use = 0;

	/* Putting the refcount for ts_buff and cq_cb objects will be handled
	 * in workqueue context, just add job to free_list.
	 */
	free_node->ts_buff = pend->ts_reg_info.ts_buff;
	free_node->cq_cb = pend->ts_reg_info.cq_cb;
	list_add(&free_node->free_objects_node, *free_list);

	return 0;
}

static void handle_user_cq(struct hl_device *hdev,
			struct hl_user_interrupt *user_cq)
{
	struct hl_user_pending_interrupt *pend, *temp_pend;
	struct list_head *ts_reg_free_list_head = NULL;
	struct timestamp_reg_work_obj *job;
	bool reg_node_handle_fail = false;
	ktime_t now = ktime_get();
	int rc;

	/* For registration nodes:
	 * As part of handling the registration nodes, we should put refcount to
	 * some objects. the problem is that we cannot do that under spinlock
	 * or in irq handler context at all (since release functions are long and
	 * might sleep), so we will need to handle that part in workqueue context.
	 * To avoid handling kmalloc failure which compels us rolling back actions
	 * and move nodes hanged on the free list back to the interrupt wait list
	 * we always alloc the job of the WQ at the beginning.
	 */
	job = kmalloc(sizeof(*job), GFP_ATOMIC);
	if (!job)
		return;

	spin_lock(&user_cq->wait_list_lock);
	list_for_each_entry_safe(pend, temp_pend, &user_cq->wait_list_head, wait_list_node) {
		if ((pend->cq_kernel_addr && *(pend->cq_kernel_addr) >= pend->cq_target_value) ||
				!pend->cq_kernel_addr) {
			if (pend->ts_reg_info.ts_buff) {
				if (!reg_node_handle_fail) {
					rc = handle_registration_node(hdev, pend,
									&ts_reg_free_list_head);
					if (rc)
						reg_node_handle_fail = true;
				}
			} else {
				/* Handle wait target value node */
				pend->fence.timestamp = now;
				complete_all(&pend->fence.completion);
			}
		}
	}
	spin_unlock(&user_cq->wait_list_lock);

	if (ts_reg_free_list_head) {
		INIT_WORK(&job->free_obj, hl_ts_free_objects);
		job->free_obj_head = ts_reg_free_list_head;
		job->hdev = hdev;
		queue_work(hdev->ts_free_obj_wq, &job->free_obj);
	} else {
		kfree(job);
	}
}

/**
 * hl_irq_handler_user_cq - irq handler for user completion queues
 *
 * @irq: irq number
 * @arg: pointer to user interrupt structure
 *
 */
irqreturn_t hl_irq_handler_user_cq(int irq, void *arg)
{
	struct hl_user_interrupt *user_cq = arg;
	struct hl_device *hdev = user_cq->hdev;

	dev_dbg(hdev->dev,
		"got user completion interrupt id %u",
		user_cq->interrupt_id);

	/* Handle user cq interrupts registered on all interrupts */
	handle_user_cq(hdev, &hdev->common_user_interrupt);

	/* Handle user cq interrupts registered on this specific interrupt */
	handle_user_cq(hdev, user_cq);

	return IRQ_HANDLED;
}

/**
 * hl_irq_handler_default - default irq handler
 *
 * @irq: irq number
 * @arg: pointer to user interrupt structure
 *
 */
irqreturn_t hl_irq_handler_default(int irq, void *arg)
{
	struct hl_user_interrupt *user_interrupt = arg;
	struct hl_device *hdev = user_interrupt->hdev;
	u32 interrupt_id = user_interrupt->interrupt_id;

	dev_err(hdev->dev,
		"got invalid user interrupt %u",
		interrupt_id);

	return IRQ_HANDLED;
}

/**
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
	bool entry_ready;
	u32 cur_eqe;
	u16 cur_eqe_index;

	eq_base = eq->kernel_address;

	while (1) {
		cur_eqe = le32_to_cpu(eq_base[eq->ci].hdr.ctl);
		entry_ready = !!FIELD_GET(EQ_CTL_READY_MASK, cur_eqe);

		if (!entry_ready)
			break;

		cur_eqe_index = FIELD_GET(EQ_CTL_INDEX_MASK, cur_eqe);
		if ((hdev->event_queue.check_eqe_index) &&
				(((eq->prev_eqe_index + 1) & EQ_CTL_INDEX_MASK)
							!= cur_eqe_index)) {
			dev_dbg(hdev->dev,
				"EQE 0x%x in queue is ready but index does not match %d!=%d",
				eq_base[eq->ci].hdr.ctl,
				((eq->prev_eqe_index + 1) & EQ_CTL_INDEX_MASK),
				cur_eqe_index);
			break;
		}

		eq->prev_eqe_index++;

		eq_entry = &eq_base[eq->ci];

		/*
		 * Make sure we read EQ entry contents after we've
		 * checked the ownership bit.
		 */
		dma_rmb();

		if (hdev->disabled && !hdev->reset_info.is_in_soft_reset) {
			dev_warn(hdev->dev, "Device disabled but received an EQ event\n");
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

/**
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

	p = hdev->asic_funcs->asic_dma_alloc_coherent(hdev, HL_CQ_SIZE_IN_BYTES,
				&q->bus_address, GFP_KERNEL | __GFP_ZERO);
	if (!p)
		return -ENOMEM;

	q->hdev = hdev;
	q->kernel_address = p;
	q->hw_queue_id = hw_queue_id;
	q->ci = 0;
	q->pi = 0;

	atomic_set(&q->free_slots_cnt, HL_CQ_LENGTH);

	return 0;
}

/**
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
						 q->kernel_address,
						 q->bus_address);
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

	memset(q->kernel_address, 0, HL_CQ_SIZE_IN_BYTES);
}

/**
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

	p = hdev->asic_funcs->cpu_accessible_dma_pool_alloc(hdev,
							HL_EQ_SIZE_IN_BYTES,
							&q->bus_address);
	if (!p)
		return -ENOMEM;

	q->hdev = hdev;
	q->kernel_address = p;
	q->ci = 0;
	q->prev_eqe_index = 0;

	return 0;
}

/**
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
					q->kernel_address);
}

void hl_eq_reset(struct hl_device *hdev, struct hl_eq *q)
{
	q->ci = 0;
	q->prev_eqe_index = 0;

	/*
	 * It's not enough to just reset the PI/CI because the H/W may have
	 * written valid completion entries before it was halted and therefore
	 * we need to clean the actual queues so we won't process old entries
	 * when the device is operational again
	 */

	memset(q->kernel_address, 0, HL_EQ_SIZE_IN_BYTES);
}
