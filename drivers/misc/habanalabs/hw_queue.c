// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

#include <linux/slab.h>

/*
 * hl_queue_add_ptr - add to pi or ci and checks if it wraps around
 *
 * @ptr: the current pi/ci value
 * @val: the amount to add
 *
 * Add val to ptr. It can go until twice the queue length.
 */
inline u32 hl_hw_queue_add_ptr(u32 ptr, u16 val)
{
	ptr += val;
	ptr &= ((HL_QUEUE_LENGTH << 1) - 1);
	return ptr;
}

static inline int queue_free_slots(struct hl_hw_queue *q, u32 queue_len)
{
	int delta = (q->pi - q->ci);

	if (delta >= 0)
		return (queue_len - delta);
	else
		return (abs(delta) - queue_len);
}

void hl_int_hw_queue_update_ci(struct hl_cs *cs)
{
	struct hl_device *hdev = cs->ctx->hdev;
	struct hl_hw_queue *q;
	int i;

	hdev->asic_funcs->hw_queues_lock(hdev);

	if (hdev->disabled)
		goto out;

	q = &hdev->kernel_queues[0];
	for (i = 0 ; i < HL_MAX_QUEUES ; i++, q++) {
		if (q->queue_type == QUEUE_TYPE_INT) {
			q->ci += cs->jobs_in_queue_cnt[i];
			q->ci &= ((q->int_queue_len << 1) - 1);
		}
	}

out:
	hdev->asic_funcs->hw_queues_unlock(hdev);
}

/*
 * ext_queue_submit_bd - Submit a buffer descriptor to an external queue
 *
 * @hdev: pointer to habanalabs device structure
 * @q: pointer to habanalabs queue structure
 * @ctl: BD's control word
 * @len: BD's length
 * @ptr: BD's pointer
 *
 * This function assumes there is enough space on the queue to submit a new
 * BD to it. It initializes the next BD and calls the device specific
 * function to set the pi (and doorbell)
 *
 * This function must be called when the scheduler mutex is taken
 *
 */
static void ext_queue_submit_bd(struct hl_device *hdev, struct hl_hw_queue *q,
				u32 ctl, u32 len, u64 ptr)
{
	struct hl_bd *bd;

	bd = (struct hl_bd *) (uintptr_t) q->kernel_address;
	bd += hl_pi_2_offset(q->pi);
	bd->ctl = __cpu_to_le32(ctl);
	bd->len = __cpu_to_le32(len);
	bd->ptr = __cpu_to_le64(ptr + hdev->asic_prop.host_phys_base_address);

	q->pi = hl_queue_inc_ptr(q->pi);
	hdev->asic_funcs->ring_doorbell(hdev, q->hw_queue_id, q->pi);
}

/*
 * ext_queue_sanity_checks - perform some sanity checks on external queue
 *
 * @hdev              : pointer to hl_device structure
 * @q                 :	pointer to hl_hw_queue structure
 * @num_of_entries    : how many entries to check for space
 * @reserve_cq_entry  :	whether to reserve an entry in the cq
 *
 * H/W queues spinlock should be taken before calling this function
 *
 * Perform the following:
 * - Make sure we have enough space in the h/w queue
 * - Make sure we have enough space in the completion queue
 * - Reserve space in the completion queue (needs to be reversed if there
 *   is a failure down the road before the actual submission of work). Only
 *   do this action if reserve_cq_entry is true
 *
 */
static int ext_queue_sanity_checks(struct hl_device *hdev,
				struct hl_hw_queue *q, int num_of_entries,
				bool reserve_cq_entry)
{
	atomic_t *free_slots =
			&hdev->completion_queue[q->hw_queue_id].free_slots_cnt;
	int free_slots_cnt;

	/* Check we have enough space in the queue */
	free_slots_cnt = queue_free_slots(q, HL_QUEUE_LENGTH);

	if (free_slots_cnt < num_of_entries) {
		dev_dbg(hdev->dev, "Queue %d doesn't have room for %d CBs\n",
			q->hw_queue_id, num_of_entries);
		return -EAGAIN;
	}

	if (reserve_cq_entry) {
		/*
		 * Check we have enough space in the completion queue
		 * Add -1 to counter (decrement) unless counter was already 0
		 * In that case, CQ is full so we can't submit a new CB because
		 * we won't get ack on its completion
		 * atomic_add_unless will return 0 if counter was already 0
		 */
		if (atomic_add_negative(num_of_entries * -1, free_slots)) {
			dev_dbg(hdev->dev, "No space for %d on CQ %d\n",
				num_of_entries, q->hw_queue_id);
			atomic_add(num_of_entries, free_slots);
			return -EAGAIN;
		}
	}

	return 0;
}

/*
 * int_queue_sanity_checks - perform some sanity checks on internal queue
 *
 * @hdev              : pointer to hl_device structure
 * @q                 :	pointer to hl_hw_queue structure
 * @num_of_entries    : how many entries to check for space
 *
 * H/W queues spinlock should be taken before calling this function
 *
 * Perform the following:
 * - Make sure we have enough space in the h/w queue
 *
 */
static int int_queue_sanity_checks(struct hl_device *hdev,
					struct hl_hw_queue *q,
					int num_of_entries)
{
	int free_slots_cnt;

	/* Check we have enough space in the queue */
	free_slots_cnt = queue_free_slots(q, q->int_queue_len);

	if (free_slots_cnt < num_of_entries) {
		dev_dbg(hdev->dev, "Queue %d doesn't have room for %d CBs\n",
			q->hw_queue_id, num_of_entries);
		return -EAGAIN;
	}

	return 0;
}

/*
 * hl_hw_queue_send_cb_no_cmpl - send a single CB (not a JOB) without completion
 *
 * @hdev: pointer to hl_device structure
 * @hw_queue_id: Queue's type
 * @cb_size: size of CB
 * @cb_ptr: pointer to CB location
 *
 * This function sends a single CB, that must NOT generate a completion entry
 *
 */
int hl_hw_queue_send_cb_no_cmpl(struct hl_device *hdev, u32 hw_queue_id,
				u32 cb_size, u64 cb_ptr)
{
	struct hl_hw_queue *q = &hdev->kernel_queues[hw_queue_id];
	int rc;

	/*
	 * The CPU queue is a synchronous queue with an effective depth of
	 * a single entry (although it is allocated with room for multiple
	 * entries). Therefore, there is a different lock, called
	 * send_cpu_message_lock, that serializes accesses to the CPU queue.
	 * As a result, we don't need to lock the access to the entire H/W
	 * queues module when submitting a JOB to the CPU queue
	 */
	if (q->queue_type != QUEUE_TYPE_CPU)
		hdev->asic_funcs->hw_queues_lock(hdev);

	if (hdev->disabled) {
		rc = -EPERM;
		goto out;
	}

	rc = ext_queue_sanity_checks(hdev, q, 1, false);
	if (rc)
		goto out;

	ext_queue_submit_bd(hdev, q, 0, cb_size, cb_ptr);

out:
	if (q->queue_type != QUEUE_TYPE_CPU)
		hdev->asic_funcs->hw_queues_unlock(hdev);

	return rc;
}

/*
 * ext_hw_queue_schedule_job - submit an JOB to an external queue
 *
 * @job: pointer to the job that needs to be submitted to the queue
 *
 * This function must be called when the scheduler mutex is taken
 *
 */
static void ext_hw_queue_schedule_job(struct hl_cs_job *job)
{
	struct hl_device *hdev = job->cs->ctx->hdev;
	struct hl_hw_queue *q = &hdev->kernel_queues[job->hw_queue_id];
	struct hl_cq_entry cq_pkt;
	struct hl_cq *cq;
	u64 cq_addr;
	struct hl_cb *cb;
	u32 ctl;
	u32 len;
	u64 ptr;

	/*
	 * Update the JOB ID inside the BD CTL so the device would know what
	 * to write in the completion queue
	 */
	ctl = ((q->pi << BD_CTL_SHADOW_INDEX_SHIFT) & BD_CTL_SHADOW_INDEX_MASK);

	cb = job->patched_cb;
	len = job->job_cb_size;
	ptr = cb->bus_address;

	cq_pkt.data = __cpu_to_le32(
				((q->pi << CQ_ENTRY_SHADOW_INDEX_SHIFT)
					& CQ_ENTRY_SHADOW_INDEX_MASK) |
				(1 << CQ_ENTRY_SHADOW_INDEX_VALID_SHIFT) |
				(1 << CQ_ENTRY_READY_SHIFT));

	/*
	 * No need to protect pi_offset because scheduling to the
	 * H/W queues is done under the scheduler mutex
	 *
	 * No need to check if CQ is full because it was already
	 * checked in hl_queue_sanity_checks
	 */
	cq = &hdev->completion_queue[q->hw_queue_id];
	cq_addr = cq->bus_address +
			hdev->asic_prop.host_phys_base_address;
	cq_addr += cq->pi * sizeof(struct hl_cq_entry);

	hdev->asic_funcs->add_end_of_cb_packets(cb->kernel_address, len,
						cq_addr,
						__le32_to_cpu(cq_pkt.data),
						q->hw_queue_id);

	q->shadow_queue[hl_pi_2_offset(q->pi)] = job;

	cq->pi = hl_cq_inc_ptr(cq->pi);

	ext_queue_submit_bd(hdev, q, ctl, len, ptr);
}

/*
 * int_hw_queue_schedule_job - submit an JOB to an internal queue
 *
 * @job: pointer to the job that needs to be submitted to the queue
 *
 * This function must be called when the scheduler mutex is taken
 *
 */
static void int_hw_queue_schedule_job(struct hl_cs_job *job)
{
	struct hl_device *hdev = job->cs->ctx->hdev;
	struct hl_hw_queue *q = &hdev->kernel_queues[job->hw_queue_id];
	struct hl_bd bd;
	u64 *pi, *pbd = (u64 *) &bd;

	bd.ctl = 0;
	bd.len = __cpu_to_le32(job->job_cb_size);
	bd.ptr = __cpu_to_le64((u64) (uintptr_t) job->user_cb);

	pi = (u64 *) (uintptr_t) (q->kernel_address +
		((q->pi & (q->int_queue_len - 1)) * sizeof(bd)));

	pi[0] = pbd[0];
	pi[1] = pbd[1];

	q->pi++;
	q->pi &= ((q->int_queue_len << 1) - 1);

	/* Flush PQ entry write. Relevant only for specific ASICs */
	hdev->asic_funcs->flush_pq_write(hdev, pi, pbd[0]);

	hdev->asic_funcs->ring_doorbell(hdev, q->hw_queue_id, q->pi);
}

/*
 * hl_hw_queue_schedule_cs - schedule a command submission
 *
 * @job        : pointer to the CS
 *
 */
int hl_hw_queue_schedule_cs(struct hl_cs *cs)
{
	struct hl_device *hdev = cs->ctx->hdev;
	struct hl_cs_job *job, *tmp;
	struct hl_hw_queue *q;
	int rc = 0, i, cq_cnt;

	hdev->asic_funcs->hw_queues_lock(hdev);

	if (hl_device_disabled_or_in_reset(hdev)) {
		dev_err(hdev->dev,
			"device is disabled or in reset, CS rejected!\n");
		rc = -EPERM;
		goto out;
	}

	q = &hdev->kernel_queues[0];
	/* This loop assumes all external queues are consecutive */
	for (i = 0, cq_cnt = 0 ; i < HL_MAX_QUEUES ; i++, q++) {
		if (q->queue_type == QUEUE_TYPE_EXT) {
			if (cs->jobs_in_queue_cnt[i]) {
				rc = ext_queue_sanity_checks(hdev, q,
					cs->jobs_in_queue_cnt[i], true);
				if (rc)
					goto unroll_cq_resv;
				cq_cnt++;
			}
		} else if (q->queue_type == QUEUE_TYPE_INT) {
			if (cs->jobs_in_queue_cnt[i]) {
				rc = int_queue_sanity_checks(hdev, q,
					cs->jobs_in_queue_cnt[i]);
				if (rc)
					goto unroll_cq_resv;
			}
		}
	}

	spin_lock(&hdev->hw_queues_mirror_lock);
	list_add_tail(&cs->mirror_node, &hdev->hw_queues_mirror_list);

	/* Queue TDR if the CS is the first entry and if timeout is wanted */
	if ((hdev->timeout_jiffies != MAX_SCHEDULE_TIMEOUT) &&
			(list_first_entry(&hdev->hw_queues_mirror_list,
					struct hl_cs, mirror_node) == cs)) {
		cs->tdr_active = true;
		schedule_delayed_work(&cs->work_tdr, hdev->timeout_jiffies);
		spin_unlock(&hdev->hw_queues_mirror_lock);
	} else {
		spin_unlock(&hdev->hw_queues_mirror_lock);
	}

	atomic_inc(&hdev->cs_active_cnt);

	list_for_each_entry_safe(job, tmp, &cs->job_list, cs_node)
		if (job->ext_queue)
			ext_hw_queue_schedule_job(job);
		else
			int_hw_queue_schedule_job(job);

	cs->submitted = true;

	goto out;

unroll_cq_resv:
	/* This loop assumes all external queues are consecutive */
	q = &hdev->kernel_queues[0];
	for (i = 0 ; (i < HL_MAX_QUEUES) && (cq_cnt > 0) ; i++, q++) {
		if ((q->queue_type == QUEUE_TYPE_EXT) &&
				(cs->jobs_in_queue_cnt[i])) {
			atomic_t *free_slots =
				&hdev->completion_queue[i].free_slots_cnt;
			atomic_add(cs->jobs_in_queue_cnt[i], free_slots);
			cq_cnt--;
		}
	}

out:
	hdev->asic_funcs->hw_queues_unlock(hdev);

	return rc;
}

/*
 * hl_hw_queue_inc_ci_kernel - increment ci for kernel's queue
 *
 * @hdev: pointer to hl_device structure
 * @hw_queue_id: which queue to increment its ci
 */
void hl_hw_queue_inc_ci_kernel(struct hl_device *hdev, u32 hw_queue_id)
{
	struct hl_hw_queue *q = &hdev->kernel_queues[hw_queue_id];

	q->ci = hl_queue_inc_ptr(q->ci);
}

static int ext_and_cpu_hw_queue_init(struct hl_device *hdev,
					struct hl_hw_queue *q)
{
	void *p;
	int rc;

	p = hdev->asic_funcs->dma_alloc_coherent(hdev,
				HL_QUEUE_SIZE_IN_BYTES,
				&q->bus_address, GFP_KERNEL | __GFP_ZERO);
	if (!p)
		return -ENOMEM;

	q->kernel_address = (u64) (uintptr_t) p;

	q->shadow_queue = kmalloc_array(HL_QUEUE_LENGTH,
					sizeof(*q->shadow_queue),
					GFP_KERNEL);
	if (!q->shadow_queue) {
		dev_err(hdev->dev,
			"Failed to allocate shadow queue for H/W queue %d\n",
			q->hw_queue_id);
		rc = -ENOMEM;
		goto free_queue;
	}

	/* Make sure read/write pointers are initialized to start of queue */
	q->ci = 0;
	q->pi = 0;

	return 0;

free_queue:
	hdev->asic_funcs->dma_free_coherent(hdev, HL_QUEUE_SIZE_IN_BYTES,
			(void *) (uintptr_t) q->kernel_address, q->bus_address);

	return rc;
}

static int int_hw_queue_init(struct hl_device *hdev, struct hl_hw_queue *q)
{
	void *p;

	p = hdev->asic_funcs->get_int_queue_base(hdev, q->hw_queue_id,
					&q->bus_address, &q->int_queue_len);
	if (!p) {
		dev_err(hdev->dev,
			"Failed to get base address for internal queue %d\n",
			q->hw_queue_id);
		return -EFAULT;
	}

	q->kernel_address = (u64) (uintptr_t) p;
	q->pi = 0;
	q->ci = 0;

	return 0;
}

static int cpu_hw_queue_init(struct hl_device *hdev, struct hl_hw_queue *q)
{
	return ext_and_cpu_hw_queue_init(hdev, q);
}

static int ext_hw_queue_init(struct hl_device *hdev, struct hl_hw_queue *q)
{
	return ext_and_cpu_hw_queue_init(hdev, q);
}

/*
 * hw_queue_init - main initialization function for H/W queue object
 *
 * @hdev: pointer to hl_device device structure
 * @q: pointer to hl_hw_queue queue structure
 * @hw_queue_id: The id of the H/W queue
 *
 * Allocate dma-able memory for the queue and initialize fields
 * Returns 0 on success
 */
static int hw_queue_init(struct hl_device *hdev, struct hl_hw_queue *q,
			u32 hw_queue_id)
{
	int rc;

	BUILD_BUG_ON(HL_QUEUE_SIZE_IN_BYTES > HL_PAGE_SIZE);

	q->hw_queue_id = hw_queue_id;

	switch (q->queue_type) {
	case QUEUE_TYPE_EXT:
		rc = ext_hw_queue_init(hdev, q);
		break;

	case QUEUE_TYPE_INT:
		rc = int_hw_queue_init(hdev, q);
		break;

	case QUEUE_TYPE_CPU:
		rc = cpu_hw_queue_init(hdev, q);
		break;

	case QUEUE_TYPE_NA:
		q->valid = 0;
		return 0;

	default:
		dev_crit(hdev->dev, "wrong queue type %d during init\n",
			q->queue_type);
		rc = -EINVAL;
		break;
	}

	if (rc)
		return rc;

	q->valid = 1;

	return 0;
}

/*
 * hw_queue_fini - destroy queue
 *
 * @hdev: pointer to hl_device device structure
 * @q: pointer to hl_hw_queue queue structure
 *
 * Free the queue memory
 */
static void hw_queue_fini(struct hl_device *hdev, struct hl_hw_queue *q)
{
	if (!q->valid)
		return;

	/*
	 * If we arrived here, there are no jobs waiting on this queue
	 * so we can safely remove it.
	 * This is because this function can only called when:
	 * 1. Either a context is deleted, which only can occur if all its
	 *    jobs were finished
	 * 2. A context wasn't able to be created due to failure or timeout,
	 *    which means there are no jobs on the queue yet
	 *
	 * The only exception are the queues of the kernel context, but
	 * if they are being destroyed, it means that the entire module is
	 * being removed. If the module is removed, it means there is no open
	 * user context. It also means that if a job was submitted by
	 * the kernel driver (e.g. context creation), the job itself was
	 * released by the kernel driver when a timeout occurred on its
	 * Completion. Thus, we don't need to release it again.
	 */

	if (q->queue_type == QUEUE_TYPE_INT)
		return;

	kfree(q->shadow_queue);

	hdev->asic_funcs->dma_free_coherent(hdev, HL_QUEUE_SIZE_IN_BYTES,
			(void *) (uintptr_t) q->kernel_address, q->bus_address);
}

int hl_hw_queues_create(struct hl_device *hdev)
{
	struct asic_fixed_properties *asic = &hdev->asic_prop;
	struct hl_hw_queue *q;
	int i, rc, q_ready_cnt;

	hdev->kernel_queues = kcalloc(HL_MAX_QUEUES,
				sizeof(*hdev->kernel_queues), GFP_KERNEL);

	if (!hdev->kernel_queues) {
		dev_err(hdev->dev, "Not enough memory for H/W queues\n");
		return -ENOMEM;
	}

	/* Initialize the H/W queues */
	for (i = 0, q_ready_cnt = 0, q = hdev->kernel_queues;
			i < HL_MAX_QUEUES ; i++, q_ready_cnt++, q++) {

		q->queue_type = asic->hw_queues_props[i].type;
		rc = hw_queue_init(hdev, q, i);
		if (rc) {
			dev_err(hdev->dev,
				"failed to initialize queue %d\n", i);
			goto release_queues;
		}
	}

	return 0;

release_queues:
	for (i = 0, q = hdev->kernel_queues ; i < q_ready_cnt ; i++, q++)
		hw_queue_fini(hdev, q);

	kfree(hdev->kernel_queues);

	return rc;
}

void hl_hw_queues_destroy(struct hl_device *hdev)
{
	struct hl_hw_queue *q;
	int i;

	for (i = 0, q = hdev->kernel_queues ; i < HL_MAX_QUEUES ; i++, q++)
		hw_queue_fini(hdev, q);

	kfree(hdev->kernel_queues);
}

void hl_hw_queue_reset(struct hl_device *hdev, bool hard_reset)
{
	struct hl_hw_queue *q;
	int i;

	for (i = 0, q = hdev->kernel_queues ; i < HL_MAX_QUEUES ; i++, q++) {
		if ((!q->valid) ||
			((!hard_reset) && (q->queue_type == QUEUE_TYPE_CPU)))
			continue;
		q->pi = q->ci = 0;
	}
}
