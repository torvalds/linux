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
static inline int queue_ci_get(atomic_t *ci, u32 queue_len)
{
	return atomic_read(ci) & ((queue_len << 1) - 1);
}

static inline int queue_free_slots(struct hl_hw_queue *q, u32 queue_len)
{
	int delta = (q->pi - queue_ci_get(&q->ci, queue_len));

	if (delta >= 0)
		return (queue_len - delta);
	else
		return (abs(delta) - queue_len);
}

void hl_hw_queue_update_ci(struct hl_cs *cs)
{
	struct hl_device *hdev = cs->ctx->hdev;
	struct hl_hw_queue *q;
	int i;

	if (hdev->disabled)
		return;

	q = &hdev->kernel_queues[0];

	/* There are no internal queues if H/W queues are being used */
	if (!hdev->asic_prop.max_queues || q->queue_type == QUEUE_TYPE_HW)
		return;

	/* We must increment CI for every queue that will never get a
	 * completion, there are 2 scenarios this can happen:
	 * 1. All queues of a non completion CS will never get a completion.
	 * 2. Internal queues never gets completion.
	 */
	for (i = 0 ; i < hdev->asic_prop.max_queues ; i++, q++) {
		if (!cs_needs_completion(cs) || q->queue_type == QUEUE_TYPE_INT)
			atomic_add(cs->jobs_in_queue_cnt[i], &q->ci);
	}
}

/*
 * ext_and_hw_queue_submit_bd() - Submit a buffer descriptor to an external or a
 *                                H/W queue.
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
static void ext_and_hw_queue_submit_bd(struct hl_device *hdev,
			struct hl_hw_queue *q, u32 ctl, u32 len, u64 ptr)
{
	struct hl_bd *bd;

	bd = q->kernel_address;
	bd += hl_pi_2_offset(q->pi);
	bd->ctl = cpu_to_le32(ctl);
	bd->len = cpu_to_le32(len);
	bd->ptr = cpu_to_le64(ptr);

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
			&hdev->completion_queue[q->cq_id].free_slots_cnt;
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

	if (num_of_entries > q->int_queue_len) {
		dev_err(hdev->dev,
			"Cannot populate queue %u with %u jobs\n",
			q->hw_queue_id, num_of_entries);
		return -ENOMEM;
	}

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
 * hw_queue_sanity_checks() - Make sure we have enough space in the h/w queue
 * @hdev: Pointer to hl_device structure.
 * @q: Pointer to hl_hw_queue structure.
 * @num_of_entries: How many entries to check for space.
 *
 * Notice: We do not reserve queue entries so this function mustn't be called
 *         more than once per CS for the same queue
 *
 */
static int hw_queue_sanity_checks(struct hl_device *hdev, struct hl_hw_queue *q,
					int num_of_entries)
{
	int free_slots_cnt;

	/* Check we have enough space in the queue */
	free_slots_cnt = queue_free_slots(q, HL_QUEUE_LENGTH);

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
	int rc = 0;

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

	/*
	 * hl_hw_queue_send_cb_no_cmpl() is called for queues of a H/W queue
	 * type only on init phase, when the queues are empty and being tested,
	 * so there is no need for sanity checks.
	 */
	if (q->queue_type != QUEUE_TYPE_HW) {
		rc = ext_queue_sanity_checks(hdev, q, 1, false);
		if (rc)
			goto out;
	}

	ext_and_hw_queue_submit_bd(hdev, q, 0, cb_size, cb_ptr);

out:
	if (q->queue_type != QUEUE_TYPE_CPU)
		hdev->asic_funcs->hw_queues_unlock(hdev);

	return rc;
}

/*
 * ext_queue_schedule_job - submit a JOB to an external queue
 *
 * @job: pointer to the job that needs to be submitted to the queue
 *
 * This function must be called when the scheduler mutex is taken
 *
 */
static void ext_queue_schedule_job(struct hl_cs_job *job)
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

	/* Skip completion flow in case this is a non completion CS */
	if (!cs_needs_completion(job->cs))
		goto submit_bd;

	cq_pkt.data = cpu_to_le32(
			((q->pi << CQ_ENTRY_SHADOW_INDEX_SHIFT)
				& CQ_ENTRY_SHADOW_INDEX_MASK) |
			FIELD_PREP(CQ_ENTRY_SHADOW_INDEX_VALID_MASK, 1) |
			FIELD_PREP(CQ_ENTRY_READY_MASK, 1));

	/*
	 * No need to protect pi_offset because scheduling to the
	 * H/W queues is done under the scheduler mutex
	 *
	 * No need to check if CQ is full because it was already
	 * checked in ext_queue_sanity_checks
	 */
	cq = &hdev->completion_queue[q->cq_id];
	cq_addr = cq->bus_address + cq->pi * sizeof(struct hl_cq_entry);

	hdev->asic_funcs->add_end_of_cb_packets(hdev, cb->kernel_address, len,
						cq_addr,
						le32_to_cpu(cq_pkt.data),
						q->msi_vec,
						job->contains_dma_pkt);

	q->shadow_queue[hl_pi_2_offset(q->pi)] = job;

	cq->pi = hl_cq_inc_ptr(cq->pi);

submit_bd:
	ext_and_hw_queue_submit_bd(hdev, q, ctl, len, ptr);
}

/*
 * int_queue_schedule_job - submit a JOB to an internal queue
 *
 * @job: pointer to the job that needs to be submitted to the queue
 *
 * This function must be called when the scheduler mutex is taken
 *
 */
static void int_queue_schedule_job(struct hl_cs_job *job)
{
	struct hl_device *hdev = job->cs->ctx->hdev;
	struct hl_hw_queue *q = &hdev->kernel_queues[job->hw_queue_id];
	struct hl_bd bd;
	__le64 *pi;

	bd.ctl = 0;
	bd.len = cpu_to_le32(job->job_cb_size);

	if (job->is_kernel_allocated_cb)
		/* bus_address is actually a mmu mapped address
		 * allocated from an internal pool
		 */
		bd.ptr = cpu_to_le64(job->user_cb->bus_address);
	else
		bd.ptr = cpu_to_le64((u64) (uintptr_t) job->user_cb);

	pi = q->kernel_address + (q->pi & (q->int_queue_len - 1)) * sizeof(bd);

	q->pi++;
	q->pi &= ((q->int_queue_len << 1) - 1);

	hdev->asic_funcs->pqe_write(hdev, pi, &bd);

	hdev->asic_funcs->ring_doorbell(hdev, q->hw_queue_id, q->pi);
}

/*
 * hw_queue_schedule_job - submit a JOB to a H/W queue
 *
 * @job: pointer to the job that needs to be submitted to the queue
 *
 * This function must be called when the scheduler mutex is taken
 *
 */
static void hw_queue_schedule_job(struct hl_cs_job *job)
{
	struct hl_device *hdev = job->cs->ctx->hdev;
	struct hl_hw_queue *q = &hdev->kernel_queues[job->hw_queue_id];
	u64 ptr;
	u32 offset, ctl, len;

	/*
	 * Upon PQE completion, COMP_DATA is used as the write data to the
	 * completion queue (QMAN HBW message), and COMP_OFFSET is used as the
	 * write address offset in the SM block (QMAN LBW message).
	 * The write address offset is calculated as "COMP_OFFSET << 2".
	 */
	offset = job->cs->sequence & (hdev->asic_prop.max_pending_cs - 1);
	ctl = ((offset << BD_CTL_COMP_OFFSET_SHIFT) & BD_CTL_COMP_OFFSET_MASK) |
		((q->pi << BD_CTL_COMP_DATA_SHIFT) & BD_CTL_COMP_DATA_MASK);

	len = job->job_cb_size;

	/*
	 * A patched CB is created only if a user CB was allocated by driver and
	 * MMU is disabled. If MMU is enabled, the user CB should be used
	 * instead. If the user CB wasn't allocated by driver, assume that it
	 * holds an address.
	 */
	if (job->patched_cb)
		ptr = job->patched_cb->bus_address;
	else if (job->is_kernel_allocated_cb)
		ptr = job->user_cb->bus_address;
	else
		ptr = (u64) (uintptr_t) job->user_cb;

	ext_and_hw_queue_submit_bd(hdev, q, ctl, len, ptr);
}

static void init_signal_cs(struct hl_device *hdev,
		struct hl_cs_job *job, struct hl_cs_compl *cs_cmpl)
{
	struct hl_sync_stream_properties *prop;
	struct hl_hw_sob *hw_sob;
	u32 q_idx;

	q_idx = job->hw_queue_id;
	prop = &hdev->kernel_queues[q_idx].sync_stream_prop;
	hw_sob = &prop->hw_sob[prop->curr_sob_offset];

	cs_cmpl->hw_sob = hw_sob;
	cs_cmpl->sob_val = prop->next_sob_val++;

	dev_dbg(hdev->dev,
		"generate signal CB, sob_id: %d, sob val: 0x%x, q_idx: %d\n",
		cs_cmpl->hw_sob->sob_id, cs_cmpl->sob_val, q_idx);

	/* we set an EB since we must make sure all oeprations are done
	 * when sending the signal
	 */
	hdev->asic_funcs->gen_signal_cb(hdev, job->patched_cb,
				cs_cmpl->hw_sob->sob_id, 0, true);

	kref_get(&hw_sob->kref);

	/* check for wraparound */
	if (prop->next_sob_val == HL_MAX_SOB_VAL) {
		/*
		 * Decrement as we reached the max value.
		 * The release function won't be called here as we've
		 * just incremented the refcount.
		 */
		kref_put(&hw_sob->kref, hl_sob_reset_error);
		prop->next_sob_val = 1;
		/* only two SOBs are currently in use */
		prop->curr_sob_offset =
			(prop->curr_sob_offset + 1) % HL_RSVD_SOBS;

		dev_dbg(hdev->dev, "switched to SOB %d, q_idx: %d\n",
				prop->curr_sob_offset, q_idx);
	}
}

static void init_wait_cs(struct hl_device *hdev, struct hl_cs *cs,
		struct hl_cs_job *job, struct hl_cs_compl *cs_cmpl)
{
	struct hl_cs_compl *signal_cs_cmpl;
	struct hl_sync_stream_properties *prop;
	struct hl_gen_wait_properties wait_prop;
	u32 q_idx;

	q_idx = job->hw_queue_id;
	prop = &hdev->kernel_queues[q_idx].sync_stream_prop;

	signal_cs_cmpl = container_of(cs->signal_fence,
					struct hl_cs_compl,
					base_fence);

	/* copy the SOB id and value of the signal CS */
	cs_cmpl->hw_sob = signal_cs_cmpl->hw_sob;
	cs_cmpl->sob_val = signal_cs_cmpl->sob_val;

	dev_dbg(hdev->dev,
		"generate wait CB, sob_id: %d, sob_val: 0x%x, mon_id: %d, q_idx: %d\n",
		cs_cmpl->hw_sob->sob_id, cs_cmpl->sob_val,
		prop->base_mon_id, q_idx);

	wait_prop.data = (void *) job->patched_cb;
	wait_prop.sob_base = cs_cmpl->hw_sob->sob_id;
	wait_prop.sob_mask = 0x1;
	wait_prop.sob_val = cs_cmpl->sob_val;
	wait_prop.mon_id = prop->base_mon_id;
	wait_prop.q_idx = q_idx;
	wait_prop.size = 0;
	hdev->asic_funcs->gen_wait_cb(hdev, &wait_prop);

	kref_get(&cs_cmpl->hw_sob->kref);
	/*
	 * Must put the signal fence after the SOB refcnt increment so
	 * the SOB refcnt won't turn 0 and reset the SOB before the
	 * wait CS was submitted.
	 */
	mb();
	hl_fence_put(cs->signal_fence);
	cs->signal_fence = NULL;
}

/*
 * init_signal_wait_cs - initialize a signal/wait CS
 * @cs: pointer to the signal/wait CS
 *
 * H/W queues spinlock should be taken before calling this function
 */
static void init_signal_wait_cs(struct hl_cs *cs)
{
	struct hl_ctx *ctx = cs->ctx;
	struct hl_device *hdev = ctx->hdev;
	struct hl_cs_job *job;
	struct hl_cs_compl *cs_cmpl =
			container_of(cs->fence, struct hl_cs_compl, base_fence);

	/* There is only one job in a signal/wait CS */
	job = list_first_entry(&cs->job_list, struct hl_cs_job,
				cs_node);

	if (cs->type & CS_TYPE_SIGNAL)
		init_signal_cs(hdev, job, cs_cmpl);
	else if (cs->type & CS_TYPE_WAIT)
		init_wait_cs(hdev, cs, job, cs_cmpl);
}

/*
 * hl_hw_queue_schedule_cs - schedule a command submission
 * @cs: pointer to the CS
 */
int hl_hw_queue_schedule_cs(struct hl_cs *cs)
{
	enum hl_device_status status;
	struct hl_cs_counters_atomic *cntr;
	struct hl_ctx *ctx = cs->ctx;
	struct hl_device *hdev = ctx->hdev;
	struct hl_cs_job *job, *tmp;
	struct hl_hw_queue *q;
	int rc = 0, i, cq_cnt;
	bool first_entry;
	u32 max_queues;

	cntr = &hdev->aggregated_cs_counters;

	hdev->asic_funcs->hw_queues_lock(hdev);

	if (!hl_device_operational(hdev, &status)) {
		atomic64_inc(&cntr->device_in_reset_drop_cnt);
		atomic64_inc(&ctx->cs_counters.device_in_reset_drop_cnt);
		dev_err(hdev->dev,
			"device is %s, CS rejected!\n", hdev->status[status]);
		rc = -EPERM;
		goto out;
	}

	max_queues = hdev->asic_prop.max_queues;

	q = &hdev->kernel_queues[0];
	for (i = 0, cq_cnt = 0 ; i < max_queues ; i++, q++) {
		if (cs->jobs_in_queue_cnt[i]) {
			switch (q->queue_type) {
			case QUEUE_TYPE_EXT:
				rc = ext_queue_sanity_checks(hdev, q,
						cs->jobs_in_queue_cnt[i],
						cs_needs_completion(cs) ?
								true : false);
				break;
			case QUEUE_TYPE_INT:
				rc = int_queue_sanity_checks(hdev, q,
						cs->jobs_in_queue_cnt[i]);
				break;
			case QUEUE_TYPE_HW:
				rc = hw_queue_sanity_checks(hdev, q,
						cs->jobs_in_queue_cnt[i]);
				break;
			default:
				dev_err(hdev->dev, "Queue type %d is invalid\n",
					q->queue_type);
				rc = -EINVAL;
				break;
			}

			if (rc) {
				atomic64_inc(
					&ctx->cs_counters.queue_full_drop_cnt);
				atomic64_inc(&cntr->queue_full_drop_cnt);
				goto unroll_cq_resv;
			}

			if (q->queue_type == QUEUE_TYPE_EXT)
				cq_cnt++;
		}
	}

	if ((cs->type == CS_TYPE_SIGNAL) || (cs->type == CS_TYPE_WAIT))
		init_signal_wait_cs(cs);
	else if (cs->type == CS_TYPE_COLLECTIVE_WAIT)
		hdev->asic_funcs->collective_wait_init_cs(cs);

	spin_lock(&hdev->cs_mirror_lock);

	/* Verify staged CS exists and add to the staged list */
	if (cs->staged_cs && !cs->staged_first) {
		struct hl_cs *staged_cs;

		staged_cs = hl_staged_cs_find_first(hdev, cs->staged_sequence);
		if (!staged_cs) {
			dev_err(hdev->dev,
				"Cannot find staged submission sequence %llu",
				cs->staged_sequence);
			rc = -EINVAL;
			goto unlock_cs_mirror;
		}

		if (is_staged_cs_last_exists(hdev, staged_cs)) {
			dev_err(hdev->dev,
				"Staged submission sequence %llu already submitted",
				cs->staged_sequence);
			rc = -EINVAL;
			goto unlock_cs_mirror;
		}

		list_add_tail(&cs->staged_cs_node, &staged_cs->staged_cs_node);
	}

	list_add_tail(&cs->mirror_node, &hdev->cs_mirror_list);

	/* Queue TDR if the CS is the first entry and if timeout is wanted */
	first_entry = list_first_entry(&hdev->cs_mirror_list,
					struct hl_cs, mirror_node) == cs;
	if ((hdev->timeout_jiffies != MAX_SCHEDULE_TIMEOUT) &&
				first_entry && cs_needs_timeout(cs)) {
		cs->tdr_active = true;
		schedule_delayed_work(&cs->work_tdr, hdev->timeout_jiffies);

	}

	spin_unlock(&hdev->cs_mirror_lock);

	if (!hdev->cs_active_cnt++) {
		struct hl_device_idle_busy_ts *ts;

		ts = &hdev->idle_busy_ts_arr[hdev->idle_busy_ts_idx];
		ts->busy_to_idle_ts = ktime_set(0, 0);
		ts->idle_to_busy_ts = ktime_get();
	}

	list_for_each_entry_safe(job, tmp, &cs->job_list, cs_node)
		switch (job->queue_type) {
		case QUEUE_TYPE_EXT:
			ext_queue_schedule_job(job);
			break;
		case QUEUE_TYPE_INT:
			int_queue_schedule_job(job);
			break;
		case QUEUE_TYPE_HW:
			hw_queue_schedule_job(job);
			break;
		default:
			break;
		}

	cs->submitted = true;

	goto out;

unlock_cs_mirror:
	spin_unlock(&hdev->cs_mirror_lock);
unroll_cq_resv:
	q = &hdev->kernel_queues[0];
	for (i = 0 ; (i < max_queues) && (cq_cnt > 0) ; i++, q++) {
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

	atomic_inc(&q->ci);
}

static int ext_and_cpu_queue_init(struct hl_device *hdev, struct hl_hw_queue *q,
					bool is_cpu_queue)
{
	void *p;
	int rc;

	if (is_cpu_queue)
		p = hdev->asic_funcs->cpu_accessible_dma_pool_alloc(hdev,
							HL_QUEUE_SIZE_IN_BYTES,
							&q->bus_address);
	else
		p = hdev->asic_funcs->asic_dma_alloc_coherent(hdev,
						HL_QUEUE_SIZE_IN_BYTES,
						&q->bus_address,
						GFP_KERNEL | __GFP_ZERO);
	if (!p)
		return -ENOMEM;

	q->kernel_address = p;

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
	atomic_set(&q->ci, 0);
	q->pi = 0;

	return 0;

free_queue:
	if (is_cpu_queue)
		hdev->asic_funcs->cpu_accessible_dma_pool_free(hdev,
					HL_QUEUE_SIZE_IN_BYTES,
					q->kernel_address);
	else
		hdev->asic_funcs->asic_dma_free_coherent(hdev,
					HL_QUEUE_SIZE_IN_BYTES,
					q->kernel_address,
					q->bus_address);

	return rc;
}

static int int_queue_init(struct hl_device *hdev, struct hl_hw_queue *q)
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

	q->kernel_address = p;
	q->pi = 0;
	atomic_set(&q->ci, 0);

	return 0;
}

static int cpu_queue_init(struct hl_device *hdev, struct hl_hw_queue *q)
{
	return ext_and_cpu_queue_init(hdev, q, true);
}

static int ext_queue_init(struct hl_device *hdev, struct hl_hw_queue *q)
{
	return ext_and_cpu_queue_init(hdev, q, false);
}

static int hw_queue_init(struct hl_device *hdev, struct hl_hw_queue *q)
{
	void *p;

	p = hdev->asic_funcs->asic_dma_alloc_coherent(hdev,
						HL_QUEUE_SIZE_IN_BYTES,
						&q->bus_address,
						GFP_KERNEL | __GFP_ZERO);
	if (!p)
		return -ENOMEM;

	q->kernel_address = p;

	/* Make sure read/write pointers are initialized to start of queue */
	atomic_set(&q->ci, 0);
	q->pi = 0;

	return 0;
}

static void sync_stream_queue_init(struct hl_device *hdev, u32 q_idx)
{
	struct hl_sync_stream_properties *sync_stream_prop;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_hw_sob *hw_sob;
	int sob, reserved_mon_idx, queue_idx;

	sync_stream_prop = &hdev->kernel_queues[q_idx].sync_stream_prop;

	/* We use 'collective_mon_idx' as a running index in order to reserve
	 * monitors for collective master/slave queues.
	 * collective master queue gets 2 reserved monitors
	 * collective slave queue gets 1 reserved monitor
	 */
	if (hdev->kernel_queues[q_idx].collective_mode ==
			HL_COLLECTIVE_MASTER) {
		reserved_mon_idx = hdev->collective_mon_idx;

		/* reserve the first monitor for collective master queue */
		sync_stream_prop->collective_mstr_mon_id[0] =
			prop->collective_first_mon + reserved_mon_idx;

		/* reserve the second monitor for collective master queue */
		sync_stream_prop->collective_mstr_mon_id[1] =
			prop->collective_first_mon + reserved_mon_idx + 1;

		hdev->collective_mon_idx += HL_COLLECTIVE_RSVD_MSTR_MONS;
	} else if (hdev->kernel_queues[q_idx].collective_mode ==
			HL_COLLECTIVE_SLAVE) {
		reserved_mon_idx = hdev->collective_mon_idx++;

		/* reserve a monitor for collective slave queue */
		sync_stream_prop->collective_slave_mon_id =
			prop->collective_first_mon + reserved_mon_idx;
	}

	if (!hdev->kernel_queues[q_idx].supports_sync_stream)
		return;

	queue_idx = hdev->sync_stream_queue_idx++;

	sync_stream_prop->base_sob_id = prop->sync_stream_first_sob +
			(queue_idx * HL_RSVD_SOBS);
	sync_stream_prop->base_mon_id = prop->sync_stream_first_mon +
			(queue_idx * HL_RSVD_MONS);
	sync_stream_prop->next_sob_val = 1;
	sync_stream_prop->curr_sob_offset = 0;

	for (sob = 0 ; sob < HL_RSVD_SOBS ; sob++) {
		hw_sob = &sync_stream_prop->hw_sob[sob];
		hw_sob->hdev = hdev;
		hw_sob->sob_id = sync_stream_prop->base_sob_id + sob;
		hw_sob->q_idx = q_idx;
		kref_init(&hw_sob->kref);
	}
}

static void sync_stream_queue_reset(struct hl_device *hdev, u32 q_idx)
{
	struct hl_sync_stream_properties *prop =
			&hdev->kernel_queues[q_idx].sync_stream_prop;

	/*
	 * In case we got here due to a stuck CS, the refcnt might be bigger
	 * than 1 and therefore we reset it.
	 */
	kref_init(&prop->hw_sob[prop->curr_sob_offset].kref);
	prop->curr_sob_offset = 0;
	prop->next_sob_val = 1;
}

/*
 * queue_init - main initialization function for H/W queue object
 *
 * @hdev: pointer to hl_device device structure
 * @q: pointer to hl_hw_queue queue structure
 * @hw_queue_id: The id of the H/W queue
 *
 * Allocate dma-able memory for the queue and initialize fields
 * Returns 0 on success
 */
static int queue_init(struct hl_device *hdev, struct hl_hw_queue *q,
			u32 hw_queue_id)
{
	int rc;

	q->hw_queue_id = hw_queue_id;

	switch (q->queue_type) {
	case QUEUE_TYPE_EXT:
		rc = ext_queue_init(hdev, q);
		break;
	case QUEUE_TYPE_INT:
		rc = int_queue_init(hdev, q);
		break;
	case QUEUE_TYPE_CPU:
		rc = cpu_queue_init(hdev, q);
		break;
	case QUEUE_TYPE_HW:
		rc = hw_queue_init(hdev, q);
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

	sync_stream_queue_init(hdev, q->hw_queue_id);

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
static void queue_fini(struct hl_device *hdev, struct hl_hw_queue *q)
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

	if (q->queue_type == QUEUE_TYPE_CPU)
		hdev->asic_funcs->cpu_accessible_dma_pool_free(hdev,
					HL_QUEUE_SIZE_IN_BYTES,
					q->kernel_address);
	else
		hdev->asic_funcs->asic_dma_free_coherent(hdev,
					HL_QUEUE_SIZE_IN_BYTES,
					q->kernel_address,
					q->bus_address);
}

int hl_hw_queues_create(struct hl_device *hdev)
{
	struct asic_fixed_properties *asic = &hdev->asic_prop;
	struct hl_hw_queue *q;
	int i, rc, q_ready_cnt;

	hdev->kernel_queues = kcalloc(asic->max_queues,
				sizeof(*hdev->kernel_queues), GFP_KERNEL);

	if (!hdev->kernel_queues) {
		dev_err(hdev->dev, "Not enough memory for H/W queues\n");
		return -ENOMEM;
	}

	/* Initialize the H/W queues */
	for (i = 0, q_ready_cnt = 0, q = hdev->kernel_queues;
			i < asic->max_queues ; i++, q_ready_cnt++, q++) {

		q->queue_type = asic->hw_queues_props[i].type;
		q->supports_sync_stream =
				asic->hw_queues_props[i].supports_sync_stream;
		q->collective_mode = asic->hw_queues_props[i].collective_mode;
		rc = queue_init(hdev, q, i);
		if (rc) {
			dev_err(hdev->dev,
				"failed to initialize queue %d\n", i);
			goto release_queues;
		}
	}

	return 0;

release_queues:
	for (i = 0, q = hdev->kernel_queues ; i < q_ready_cnt ; i++, q++)
		queue_fini(hdev, q);

	kfree(hdev->kernel_queues);

	return rc;
}

void hl_hw_queues_destroy(struct hl_device *hdev)
{
	struct hl_hw_queue *q;
	u32 max_queues = hdev->asic_prop.max_queues;
	int i;

	for (i = 0, q = hdev->kernel_queues ; i < max_queues ; i++, q++)
		queue_fini(hdev, q);

	kfree(hdev->kernel_queues);
}

void hl_hw_queue_reset(struct hl_device *hdev, bool hard_reset)
{
	struct hl_hw_queue *q;
	u32 max_queues = hdev->asic_prop.max_queues;
	int i;

	for (i = 0, q = hdev->kernel_queues ; i < max_queues ; i++, q++) {
		if ((!q->valid) ||
			((!hard_reset) && (q->queue_type == QUEUE_TYPE_CPU)))
			continue;
		q->pi = 0;
		atomic_set(&q->ci, 0);

		if (q->supports_sync_stream)
			sync_stream_queue_reset(hdev, q->hw_queue_id);
	}
}
