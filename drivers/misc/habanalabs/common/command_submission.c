// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include <uapi/misc/habanalabs.h>
#include "habanalabs.h"

#include <linux/uaccess.h>
#include <linux/slab.h>

#define HL_CS_FLAGS_TYPE_MASK	(HL_CS_FLAGS_SIGNAL | HL_CS_FLAGS_WAIT | \
				HL_CS_FLAGS_COLLECTIVE_WAIT)

/**
 * enum hl_cs_wait_status - cs wait status
 * @CS_WAIT_STATUS_BUSY: cs was not completed yet
 * @CS_WAIT_STATUS_COMPLETED: cs completed
 * @CS_WAIT_STATUS_GONE: cs completed but fence is already gone
 */
enum hl_cs_wait_status {
	CS_WAIT_STATUS_BUSY,
	CS_WAIT_STATUS_COMPLETED,
	CS_WAIT_STATUS_GONE
};

static void job_wq_completion(struct work_struct *work);
static int _hl_cs_wait_ioctl(struct hl_device *hdev, struct hl_ctx *ctx,
				u64 timeout_us, u64 seq,
				enum hl_cs_wait_status *status, s64 *timestamp);
static void cs_do_release(struct kref *ref);

static void hl_sob_reset(struct kref *ref)
{
	struct hl_hw_sob *hw_sob = container_of(ref, struct hl_hw_sob,
							kref);
	struct hl_device *hdev = hw_sob->hdev;

	hdev->asic_funcs->reset_sob(hdev, hw_sob);
}

void hl_sob_reset_error(struct kref *ref)
{
	struct hl_hw_sob *hw_sob = container_of(ref, struct hl_hw_sob,
							kref);
	struct hl_device *hdev = hw_sob->hdev;

	dev_crit(hdev->dev,
		"SOB release shouldn't be called here, q_idx: %d, sob_id: %d\n",
		hw_sob->q_idx, hw_sob->sob_id);
}

/**
 * hl_gen_sob_mask() - Generates a sob mask to be used in a monitor arm packet
 * @sob_base: sob base id
 * @sob_mask: sob user mask, each bit represents a sob offset from sob base
 * @mask: generated mask
 *
 * Return: 0 if given parameters are valid
 */
int hl_gen_sob_mask(u16 sob_base, u8 sob_mask, u8 *mask)
{
	int i;

	if (sob_mask == 0)
		return -EINVAL;

	if (sob_mask == 0x1) {
		*mask = ~(1 << (sob_base & 0x7));
	} else {
		/* find msb in order to verify sob range is valid */
		for (i = BITS_PER_BYTE - 1 ; i >= 0 ; i--)
			if (BIT(i) & sob_mask)
				break;

		if (i > (HL_MAX_SOBS_PER_MONITOR - (sob_base & 0x7) - 1))
			return -EINVAL;

		*mask = ~sob_mask;
	}

	return 0;
}

static void sob_reset_work(struct work_struct *work)
{
	struct hl_cs_compl *hl_cs_cmpl =
		container_of(work, struct hl_cs_compl, sob_reset_work);
	struct hl_device *hdev = hl_cs_cmpl->hdev;

	/*
	 * A signal CS can get completion while the corresponding wait
	 * for signal CS is on its way to the PQ. The wait for signal CS
	 * will get stuck if the signal CS incremented the SOB to its
	 * max value and there are no pending (submitted) waits on this
	 * SOB.
	 * We do the following to void this situation:
	 * 1. The wait for signal CS must get a ref for the signal CS as
	 *    soon as possible in cs_ioctl_signal_wait() and put it
	 *    before being submitted to the PQ but after it incremented
	 *    the SOB refcnt in init_signal_wait_cs().
	 * 2. Signal/Wait for signal CS will decrement the SOB refcnt
	 *    here.
	 * These two measures guarantee that the wait for signal CS will
	 * reset the SOB upon completion rather than the signal CS and
	 * hence the above scenario is avoided.
	 */
	kref_put(&hl_cs_cmpl->hw_sob->kref, hl_sob_reset);

	if (hl_cs_cmpl->type == CS_TYPE_COLLECTIVE_WAIT)
		hdev->asic_funcs->reset_sob_group(hdev,
				hl_cs_cmpl->sob_group);

	kfree(hl_cs_cmpl);
}

static void hl_fence_release(struct kref *kref)
{
	struct hl_fence *fence =
		container_of(kref, struct hl_fence, refcount);
	struct hl_cs_compl *hl_cs_cmpl =
		container_of(fence, struct hl_cs_compl, base_fence);
	struct hl_device *hdev = hl_cs_cmpl->hdev;

	/* EBUSY means the CS was never submitted and hence we don't have
	 * an attached hw_sob object that we should handle here
	 */
	if (fence->error == -EBUSY)
		goto free;

	if ((hl_cs_cmpl->type == CS_TYPE_SIGNAL) ||
		(hl_cs_cmpl->type == CS_TYPE_WAIT) ||
		(hl_cs_cmpl->type == CS_TYPE_COLLECTIVE_WAIT)) {

		dev_dbg(hdev->dev,
			"CS 0x%llx type %d finished, sob_id: %d, sob_val: 0x%x\n",
			hl_cs_cmpl->cs_seq,
			hl_cs_cmpl->type,
			hl_cs_cmpl->hw_sob->sob_id,
			hl_cs_cmpl->sob_val);

		queue_work(hdev->sob_reset_wq, &hl_cs_cmpl->sob_reset_work);

		return;
	}

free:
	kfree(hl_cs_cmpl);
}

void hl_fence_put(struct hl_fence *fence)
{
	if (fence)
		kref_put(&fence->refcount, hl_fence_release);
}

void hl_fence_get(struct hl_fence *fence)
{
	if (fence)
		kref_get(&fence->refcount);
}

static void hl_fence_init(struct hl_fence *fence, u64 sequence)
{
	kref_init(&fence->refcount);
	fence->cs_sequence = sequence;
	fence->error = 0;
	fence->timestamp = ktime_set(0, 0);
	init_completion(&fence->completion);
}

void cs_get(struct hl_cs *cs)
{
	kref_get(&cs->refcount);
}

static int cs_get_unless_zero(struct hl_cs *cs)
{
	return kref_get_unless_zero(&cs->refcount);
}

static void cs_put(struct hl_cs *cs)
{
	kref_put(&cs->refcount, cs_do_release);
}

static void cs_job_do_release(struct kref *ref)
{
	struct hl_cs_job *job = container_of(ref, struct hl_cs_job, refcount);

	kfree(job);
}

static void cs_job_put(struct hl_cs_job *job)
{
	kref_put(&job->refcount, cs_job_do_release);
}

bool cs_needs_completion(struct hl_cs *cs)
{
	/* In case this is a staged CS, only the last CS in sequence should
	 * get a completion, any non staged CS will always get a completion
	 */
	if (cs->staged_cs && !cs->staged_last)
		return false;

	return true;
}

bool cs_needs_timeout(struct hl_cs *cs)
{
	/* In case this is a staged CS, only the first CS in sequence should
	 * get a timeout, any non staged CS will always get a timeout
	 */
	if (cs->staged_cs && !cs->staged_first)
		return false;

	return true;
}

static bool is_cb_patched(struct hl_device *hdev, struct hl_cs_job *job)
{
	/*
	 * Patched CB is created for external queues jobs, and for H/W queues
	 * jobs if the user CB was allocated by driver and MMU is disabled.
	 */
	return (job->queue_type == QUEUE_TYPE_EXT ||
			(job->queue_type == QUEUE_TYPE_HW &&
					job->is_kernel_allocated_cb &&
					!hdev->mmu_enable));
}

/*
 * cs_parser - parse the user command submission
 *
 * @hpriv	: pointer to the private data of the fd
 * @job        : pointer to the job that holds the command submission info
 *
 * The function parses the command submission of the user. It calls the
 * ASIC specific parser, which returns a list of memory blocks to send
 * to the device as different command buffers
 *
 */
static int cs_parser(struct hl_fpriv *hpriv, struct hl_cs_job *job)
{
	struct hl_device *hdev = hpriv->hdev;
	struct hl_cs_parser parser;
	int rc;

	parser.ctx_id = job->cs->ctx->asid;
	parser.cs_sequence = job->cs->sequence;
	parser.job_id = job->id;

	parser.hw_queue_id = job->hw_queue_id;
	parser.job_userptr_list = &job->userptr_list;
	parser.patched_cb = NULL;
	parser.user_cb = job->user_cb;
	parser.user_cb_size = job->user_cb_size;
	parser.queue_type = job->queue_type;
	parser.is_kernel_allocated_cb = job->is_kernel_allocated_cb;
	job->patched_cb = NULL;
	parser.completion = cs_needs_completion(job->cs);

	rc = hdev->asic_funcs->cs_parser(hdev, &parser);

	if (is_cb_patched(hdev, job)) {
		if (!rc) {
			job->patched_cb = parser.patched_cb;
			job->job_cb_size = parser.patched_cb_size;
			job->contains_dma_pkt = parser.contains_dma_pkt;
			atomic_inc(&job->patched_cb->cs_cnt);
		}

		/*
		 * Whether the parsing worked or not, we don't need the
		 * original CB anymore because it was already parsed and
		 * won't be accessed again for this CS
		 */
		atomic_dec(&job->user_cb->cs_cnt);
		hl_cb_put(job->user_cb);
		job->user_cb = NULL;
	} else if (!rc) {
		job->job_cb_size = job->user_cb_size;
	}

	return rc;
}

static void complete_job(struct hl_device *hdev, struct hl_cs_job *job)
{
	struct hl_cs *cs = job->cs;

	if (is_cb_patched(hdev, job)) {
		hl_userptr_delete_list(hdev, &job->userptr_list);

		/*
		 * We might arrive here from rollback and patched CB wasn't
		 * created, so we need to check it's not NULL
		 */
		if (job->patched_cb) {
			atomic_dec(&job->patched_cb->cs_cnt);
			hl_cb_put(job->patched_cb);
		}
	}

	/* For H/W queue jobs, if a user CB was allocated by driver and MMU is
	 * enabled, the user CB isn't released in cs_parser() and thus should be
	 * released here.
	 * This is also true for INT queues jobs which were allocated by driver
	 */
	if (job->is_kernel_allocated_cb &&
		((job->queue_type == QUEUE_TYPE_HW && hdev->mmu_enable) ||
				job->queue_type == QUEUE_TYPE_INT)) {
		atomic_dec(&job->user_cb->cs_cnt);
		hl_cb_put(job->user_cb);
	}

	/*
	 * This is the only place where there can be multiple threads
	 * modifying the list at the same time
	 */
	spin_lock(&cs->job_lock);
	list_del(&job->cs_node);
	spin_unlock(&cs->job_lock);

	hl_debugfs_remove_job(hdev, job);

	/* We decrement reference only for a CS that gets completion
	 * because the reference was incremented only for this kind of CS
	 * right before it was scheduled.
	 *
	 * In staged submission, only the last CS marked as 'staged_last'
	 * gets completion, hence its release function will be called from here.
	 * As for all the rest CS's in the staged submission which do not get
	 * completion, their CS reference will be decremented by the
	 * 'staged_last' CS during the CS release flow.
	 * All relevant PQ CI counters will be incremented during the CS release
	 * flow by calling 'hl_hw_queue_update_ci'.
	 */
	if (cs_needs_completion(cs) &&
		(job->queue_type == QUEUE_TYPE_EXT ||
			job->queue_type == QUEUE_TYPE_HW))
		cs_put(cs);

	cs_job_put(job);
}

/*
 * hl_staged_cs_find_first - locate the first CS in this staged submission
 *
 * @hdev: pointer to device structure
 * @cs_seq: staged submission sequence number
 *
 * @note: This function must be called under 'hdev->cs_mirror_lock'
 *
 * Find and return a CS pointer with the given sequence
 */
struct hl_cs *hl_staged_cs_find_first(struct hl_device *hdev, u64 cs_seq)
{
	struct hl_cs *cs;

	list_for_each_entry_reverse(cs, &hdev->cs_mirror_list, mirror_node)
		if (cs->staged_cs && cs->staged_first &&
				cs->sequence == cs_seq)
			return cs;

	return NULL;
}

/*
 * is_staged_cs_last_exists - returns true if the last CS in sequence exists
 *
 * @hdev: pointer to device structure
 * @cs: staged submission member
 *
 */
bool is_staged_cs_last_exists(struct hl_device *hdev, struct hl_cs *cs)
{
	struct hl_cs *last_entry;

	last_entry = list_last_entry(&cs->staged_cs_node, struct hl_cs,
								staged_cs_node);

	if (last_entry->staged_last)
		return true;

	return false;
}

/*
 * staged_cs_get - get CS reference if this CS is a part of a staged CS
 *
 * @hdev: pointer to device structure
 * @cs: current CS
 * @cs_seq: staged submission sequence number
 *
 * Increment CS reference for every CS in this staged submission except for
 * the CS which get completion.
 */
static void staged_cs_get(struct hl_device *hdev, struct hl_cs *cs)
{
	/* Only the last CS in this staged submission will get a completion.
	 * We must increment the reference for all other CS's in this
	 * staged submission.
	 * Once we get a completion we will release the whole staged submission.
	 */
	if (!cs->staged_last)
		cs_get(cs);
}

/*
 * staged_cs_put - put a CS in case it is part of staged submission
 *
 * @hdev: pointer to device structure
 * @cs: CS to put
 *
 * This function decrements a CS reference (for a non completion CS)
 */
static void staged_cs_put(struct hl_device *hdev, struct hl_cs *cs)
{
	/* We release all CS's in a staged submission except the last
	 * CS which we have never incremented its reference.
	 */
	if (!cs_needs_completion(cs))
		cs_put(cs);
}

static void cs_handle_tdr(struct hl_device *hdev, struct hl_cs *cs)
{
	bool next_entry_found = false;
	struct hl_cs *next;

	if (!cs_needs_timeout(cs))
		return;

	spin_lock(&hdev->cs_mirror_lock);

	/* We need to handle tdr only once for the complete staged submission.
	 * Hence, we choose the CS that reaches this function first which is
	 * the CS marked as 'staged_last'.
	 */
	if (cs->staged_cs && cs->staged_last)
		cs = hl_staged_cs_find_first(hdev, cs->staged_sequence);

	spin_unlock(&hdev->cs_mirror_lock);

	/* Don't cancel TDR in case this CS was timedout because we might be
	 * running from the TDR context
	 */
	if (cs && (cs->timedout ||
			hdev->timeout_jiffies == MAX_SCHEDULE_TIMEOUT))
		return;

	if (cs && cs->tdr_active)
		cancel_delayed_work_sync(&cs->work_tdr);

	spin_lock(&hdev->cs_mirror_lock);

	/* queue TDR for next CS */
	list_for_each_entry(next, &hdev->cs_mirror_list, mirror_node)
		if (cs_needs_timeout(next)) {
			next_entry_found = true;
			break;
		}

	if (next_entry_found && !next->tdr_active) {
		next->tdr_active = true;
		schedule_delayed_work(&next->work_tdr, next->timeout_jiffies);
	}

	spin_unlock(&hdev->cs_mirror_lock);
}

static void cs_do_release(struct kref *ref)
{
	struct hl_cs *cs = container_of(ref, struct hl_cs, refcount);
	struct hl_device *hdev = cs->ctx->hdev;
	struct hl_cs_job *job, *tmp;

	cs->completed = true;

	/*
	 * Although if we reached here it means that all external jobs have
	 * finished, because each one of them took refcnt to CS, we still
	 * need to go over the internal jobs and complete them. Otherwise, we
	 * will have leaked memory and what's worse, the CS object (and
	 * potentially the CTX object) could be released, while the JOB
	 * still holds a pointer to them (but no reference).
	 */
	list_for_each_entry_safe(job, tmp, &cs->job_list, cs_node)
		complete_job(hdev, job);

	if (!cs->submitted) {
		/* In case the wait for signal CS was submitted, the put occurs
		 * in init_signal_wait_cs() or collective_wait_init_cs()
		 * right before hanging on the PQ.
		 */
		if (cs->type == CS_TYPE_WAIT ||
				cs->type == CS_TYPE_COLLECTIVE_WAIT)
			hl_fence_put(cs->signal_fence);

		goto out;
	}

	/* Need to update CI for all queue jobs that does not get completion */
	hl_hw_queue_update_ci(cs);

	/* remove CS from CS mirror list */
	spin_lock(&hdev->cs_mirror_lock);
	list_del_init(&cs->mirror_node);
	spin_unlock(&hdev->cs_mirror_lock);

	cs_handle_tdr(hdev, cs);

	if (cs->staged_cs) {
		/* the completion CS decrements reference for the entire
		 * staged submission
		 */
		if (cs->staged_last) {
			struct hl_cs *staged_cs, *tmp;

			list_for_each_entry_safe(staged_cs, tmp,
					&cs->staged_cs_node, staged_cs_node)
				staged_cs_put(hdev, staged_cs);
		}

		/* A staged CS will be a member in the list only after it
		 * was submitted. We used 'cs_mirror_lock' when inserting
		 * it to list so we will use it again when removing it
		 */
		if (cs->submitted) {
			spin_lock(&hdev->cs_mirror_lock);
			list_del(&cs->staged_cs_node);
			spin_unlock(&hdev->cs_mirror_lock);
		}
	}

out:
	/* Must be called before hl_ctx_put because inside we use ctx to get
	 * the device
	 */
	hl_debugfs_remove_cs(cs);

	hl_ctx_put(cs->ctx);

	/* We need to mark an error for not submitted because in that case
	 * the hl fence release flow is different. Mainly, we don't need
	 * to handle hw_sob for signal/wait
	 */
	if (cs->timedout)
		cs->fence->error = -ETIMEDOUT;
	else if (cs->aborted)
		cs->fence->error = -EIO;
	else if (!cs->submitted)
		cs->fence->error = -EBUSY;

	if (unlikely(cs->skip_reset_on_timeout)) {
		dev_err(hdev->dev,
			"Command submission %llu completed after %llu (s)\n",
			cs->sequence,
			div_u64(jiffies - cs->submission_time_jiffies, HZ));
	}

	if (cs->timestamp)
		cs->fence->timestamp = ktime_get();
	complete_all(&cs->fence->completion);
	hl_fence_put(cs->fence);

	kfree(cs->jobs_in_queue_cnt);
	kfree(cs);
}

static void cs_timedout(struct work_struct *work)
{
	struct hl_device *hdev;
	int rc;
	struct hl_cs *cs = container_of(work, struct hl_cs,
						 work_tdr.work);
	bool skip_reset_on_timeout = cs->skip_reset_on_timeout;

	rc = cs_get_unless_zero(cs);
	if (!rc)
		return;

	if ((!cs->submitted) || (cs->completed)) {
		cs_put(cs);
		return;
	}

	/* Mark the CS is timed out so we won't try to cancel its TDR */
	if (likely(!skip_reset_on_timeout))
		cs->timedout = true;

	hdev = cs->ctx->hdev;

	switch (cs->type) {
	case CS_TYPE_SIGNAL:
		dev_err(hdev->dev,
			"Signal command submission %llu has not finished in time!\n",
			cs->sequence);
		break;

	case CS_TYPE_WAIT:
		dev_err(hdev->dev,
			"Wait command submission %llu has not finished in time!\n",
			cs->sequence);
		break;

	case CS_TYPE_COLLECTIVE_WAIT:
		dev_err(hdev->dev,
			"Collective Wait command submission %llu has not finished in time!\n",
			cs->sequence);
		break;

	default:
		dev_err(hdev->dev,
			"Command submission %llu has not finished in time!\n",
			cs->sequence);
		break;
	}

	cs_put(cs);

	if (likely(!skip_reset_on_timeout)) {
		if (hdev->reset_on_lockup)
			hl_device_reset(hdev, HL_RESET_TDR);
		else
			hdev->needs_reset = true;
	}
}

static int allocate_cs(struct hl_device *hdev, struct hl_ctx *ctx,
			enum hl_cs_type cs_type, u64 user_sequence,
			struct hl_cs **cs_new, u32 flags, u32 timeout)
{
	struct hl_cs_counters_atomic *cntr;
	struct hl_fence *other = NULL;
	struct hl_cs_compl *cs_cmpl;
	struct hl_cs *cs;
	int rc;

	cntr = &hdev->aggregated_cs_counters;

	cs = kzalloc(sizeof(*cs), GFP_ATOMIC);
	if (!cs)
		cs = kzalloc(sizeof(*cs), GFP_KERNEL);

	if (!cs) {
		atomic64_inc(&ctx->cs_counters.out_of_mem_drop_cnt);
		atomic64_inc(&cntr->out_of_mem_drop_cnt);
		return -ENOMEM;
	}

	/* increment refcnt for context */
	hl_ctx_get(hdev, ctx);

	cs->ctx = ctx;
	cs->submitted = false;
	cs->completed = false;
	cs->type = cs_type;
	cs->timestamp = !!(flags & HL_CS_FLAGS_TIMESTAMP);
	cs->timeout_jiffies = timeout;
	cs->skip_reset_on_timeout =
		hdev->skip_reset_on_timeout ||
		!!(flags & HL_CS_FLAGS_SKIP_RESET_ON_TIMEOUT);
	cs->submission_time_jiffies = jiffies;
	INIT_LIST_HEAD(&cs->job_list);
	INIT_DELAYED_WORK(&cs->work_tdr, cs_timedout);
	kref_init(&cs->refcount);
	spin_lock_init(&cs->job_lock);

	cs_cmpl = kmalloc(sizeof(*cs_cmpl), GFP_ATOMIC);
	if (!cs_cmpl)
		cs_cmpl = kmalloc(sizeof(*cs_cmpl), GFP_KERNEL);

	if (!cs_cmpl) {
		atomic64_inc(&ctx->cs_counters.out_of_mem_drop_cnt);
		atomic64_inc(&cntr->out_of_mem_drop_cnt);
		rc = -ENOMEM;
		goto free_cs;
	}

	cs->jobs_in_queue_cnt = kcalloc(hdev->asic_prop.max_queues,
			sizeof(*cs->jobs_in_queue_cnt), GFP_ATOMIC);
	if (!cs->jobs_in_queue_cnt)
		cs->jobs_in_queue_cnt = kcalloc(hdev->asic_prop.max_queues,
				sizeof(*cs->jobs_in_queue_cnt), GFP_KERNEL);

	if (!cs->jobs_in_queue_cnt) {
		atomic64_inc(&ctx->cs_counters.out_of_mem_drop_cnt);
		atomic64_inc(&cntr->out_of_mem_drop_cnt);
		rc = -ENOMEM;
		goto free_cs_cmpl;
	}

	cs_cmpl->hdev = hdev;
	cs_cmpl->type = cs->type;
	spin_lock_init(&cs_cmpl->lock);
	INIT_WORK(&cs_cmpl->sob_reset_work, sob_reset_work);
	cs->fence = &cs_cmpl->base_fence;

	spin_lock(&ctx->cs_lock);

	cs_cmpl->cs_seq = ctx->cs_sequence;
	other = ctx->cs_pending[cs_cmpl->cs_seq &
				(hdev->asic_prop.max_pending_cs - 1)];

	if (other && !completion_done(&other->completion)) {
		/* If the following statement is true, it means we have reached
		 * a point in which only part of the staged submission was
		 * submitted and we don't have enough room in the 'cs_pending'
		 * array for the rest of the submission.
		 * This causes a deadlock because this CS will never be
		 * completed as it depends on future CS's for completion.
		 */
		if (other->cs_sequence == user_sequence)
			dev_crit_ratelimited(hdev->dev,
				"Staged CS %llu deadlock due to lack of resources",
				user_sequence);

		dev_dbg_ratelimited(hdev->dev,
			"Rejecting CS because of too many in-flights CS\n");
		atomic64_inc(&ctx->cs_counters.max_cs_in_flight_drop_cnt);
		atomic64_inc(&cntr->max_cs_in_flight_drop_cnt);
		rc = -EAGAIN;
		goto free_fence;
	}

	/* init hl_fence */
	hl_fence_init(&cs_cmpl->base_fence, cs_cmpl->cs_seq);

	cs->sequence = cs_cmpl->cs_seq;

	ctx->cs_pending[cs_cmpl->cs_seq &
			(hdev->asic_prop.max_pending_cs - 1)] =
							&cs_cmpl->base_fence;
	ctx->cs_sequence++;

	hl_fence_get(&cs_cmpl->base_fence);

	hl_fence_put(other);

	spin_unlock(&ctx->cs_lock);

	*cs_new = cs;

	return 0;

free_fence:
	spin_unlock(&ctx->cs_lock);
	kfree(cs->jobs_in_queue_cnt);
free_cs_cmpl:
	kfree(cs_cmpl);
free_cs:
	kfree(cs);
	hl_ctx_put(ctx);
	return rc;
}

static void cs_rollback(struct hl_device *hdev, struct hl_cs *cs)
{
	struct hl_cs_job *job, *tmp;

	staged_cs_put(hdev, cs);

	list_for_each_entry_safe(job, tmp, &cs->job_list, cs_node)
		complete_job(hdev, job);
}

void hl_cs_rollback_all(struct hl_device *hdev)
{
	int i;
	struct hl_cs *cs, *tmp;

	flush_workqueue(hdev->sob_reset_wq);

	/* flush all completions before iterating over the CS mirror list in
	 * order to avoid a race with the release functions
	 */
	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
		flush_workqueue(hdev->cq_wq[i]);

	/* Make sure we don't have leftovers in the CS mirror list */
	list_for_each_entry_safe(cs, tmp, &hdev->cs_mirror_list, mirror_node) {
		cs_get(cs);
		cs->aborted = true;
		dev_warn_ratelimited(hdev->dev, "Killing CS %d.%llu\n",
				cs->ctx->asid, cs->sequence);
		cs_rollback(hdev, cs);
		cs_put(cs);
	}
}

void hl_pending_cb_list_flush(struct hl_ctx *ctx)
{
	struct hl_pending_cb *pending_cb, *tmp;

	list_for_each_entry_safe(pending_cb, tmp,
			&ctx->pending_cb_list, cb_node) {
		list_del(&pending_cb->cb_node);
		hl_cb_put(pending_cb->cb);
		kfree(pending_cb);
	}
}

static void
wake_pending_user_interrupt_threads(struct hl_user_interrupt *interrupt)
{
	struct hl_user_pending_interrupt *pend;

	spin_lock(&interrupt->wait_list_lock);
	list_for_each_entry(pend, &interrupt->wait_list_head, wait_list_node) {
		pend->fence.error = -EIO;
		complete_all(&pend->fence.completion);
	}
	spin_unlock(&interrupt->wait_list_lock);
}

void hl_release_pending_user_interrupts(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_user_interrupt *interrupt;
	int i;

	if (!prop->user_interrupt_count)
		return;

	/* We iterate through the user interrupt requests and waking up all
	 * user threads waiting for interrupt completion. We iterate the
	 * list under a lock, this is why all user threads, once awake,
	 * will wait on the same lock and will release the waiting object upon
	 * unlock.
	 */

	for (i = 0 ; i < prop->user_interrupt_count ; i++) {
		interrupt = &hdev->user_interrupt[i];
		wake_pending_user_interrupt_threads(interrupt);
	}

	interrupt = &hdev->common_user_interrupt;
	wake_pending_user_interrupt_threads(interrupt);
}

static void job_wq_completion(struct work_struct *work)
{
	struct hl_cs_job *job = container_of(work, struct hl_cs_job,
						finish_work);
	struct hl_cs *cs = job->cs;
	struct hl_device *hdev = cs->ctx->hdev;

	/* job is no longer needed */
	complete_job(hdev, job);
}

static int validate_queue_index(struct hl_device *hdev,
				struct hl_cs_chunk *chunk,
				enum hl_queue_type *queue_type,
				bool *is_kernel_allocated_cb)
{
	struct asic_fixed_properties *asic = &hdev->asic_prop;
	struct hw_queue_properties *hw_queue_prop;

	/* This must be checked here to prevent out-of-bounds access to
	 * hw_queues_props array
	 */
	if (chunk->queue_index >= asic->max_queues) {
		dev_err(hdev->dev, "Queue index %d is invalid\n",
			chunk->queue_index);
		return -EINVAL;
	}

	hw_queue_prop = &asic->hw_queues_props[chunk->queue_index];

	if (hw_queue_prop->type == QUEUE_TYPE_NA) {
		dev_err(hdev->dev, "Queue index %d is invalid\n",
			chunk->queue_index);
		return -EINVAL;
	}

	if (hw_queue_prop->driver_only) {
		dev_err(hdev->dev,
			"Queue index %d is restricted for the kernel driver\n",
			chunk->queue_index);
		return -EINVAL;
	}

	/* When hw queue type isn't QUEUE_TYPE_HW,
	 * USER_ALLOC_CB flag shall be referred as "don't care".
	 */
	if (hw_queue_prop->type == QUEUE_TYPE_HW) {
		if (chunk->cs_chunk_flags & HL_CS_CHUNK_FLAGS_USER_ALLOC_CB) {
			if (!(hw_queue_prop->cb_alloc_flags & CB_ALLOC_USER)) {
				dev_err(hdev->dev,
					"Queue index %d doesn't support user CB\n",
					chunk->queue_index);
				return -EINVAL;
			}

			*is_kernel_allocated_cb = false;
		} else {
			if (!(hw_queue_prop->cb_alloc_flags &
					CB_ALLOC_KERNEL)) {
				dev_err(hdev->dev,
					"Queue index %d doesn't support kernel CB\n",
					chunk->queue_index);
				return -EINVAL;
			}

			*is_kernel_allocated_cb = true;
		}
	} else {
		*is_kernel_allocated_cb = !!(hw_queue_prop->cb_alloc_flags
						& CB_ALLOC_KERNEL);
	}

	*queue_type = hw_queue_prop->type;
	return 0;
}

static struct hl_cb *get_cb_from_cs_chunk(struct hl_device *hdev,
					struct hl_cb_mgr *cb_mgr,
					struct hl_cs_chunk *chunk)
{
	struct hl_cb *cb;
	u32 cb_handle;

	cb_handle = (u32) (chunk->cb_handle >> PAGE_SHIFT);

	cb = hl_cb_get(hdev, cb_mgr, cb_handle);
	if (!cb) {
		dev_err(hdev->dev, "CB handle 0x%x invalid\n", cb_handle);
		return NULL;
	}

	if ((chunk->cb_size < 8) || (chunk->cb_size > cb->size)) {
		dev_err(hdev->dev, "CB size %u invalid\n", chunk->cb_size);
		goto release_cb;
	}

	atomic_inc(&cb->cs_cnt);

	return cb;

release_cb:
	hl_cb_put(cb);
	return NULL;
}

struct hl_cs_job *hl_cs_allocate_job(struct hl_device *hdev,
		enum hl_queue_type queue_type, bool is_kernel_allocated_cb)
{
	struct hl_cs_job *job;

	job = kzalloc(sizeof(*job), GFP_ATOMIC);
	if (!job)
		job = kzalloc(sizeof(*job), GFP_KERNEL);

	if (!job)
		return NULL;

	kref_init(&job->refcount);
	job->queue_type = queue_type;
	job->is_kernel_allocated_cb = is_kernel_allocated_cb;

	if (is_cb_patched(hdev, job))
		INIT_LIST_HEAD(&job->userptr_list);

	if (job->queue_type == QUEUE_TYPE_EXT)
		INIT_WORK(&job->finish_work, job_wq_completion);

	return job;
}

static enum hl_cs_type hl_cs_get_cs_type(u32 cs_type_flags)
{
	if (cs_type_flags & HL_CS_FLAGS_SIGNAL)
		return CS_TYPE_SIGNAL;
	else if (cs_type_flags & HL_CS_FLAGS_WAIT)
		return CS_TYPE_WAIT;
	else if (cs_type_flags & HL_CS_FLAGS_COLLECTIVE_WAIT)
		return CS_TYPE_COLLECTIVE_WAIT;
	else
		return CS_TYPE_DEFAULT;
}

static int hl_cs_sanity_checks(struct hl_fpriv *hpriv, union hl_cs_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	struct hl_ctx *ctx = hpriv->ctx;
	u32 cs_type_flags, num_chunks;
	enum hl_device_status status;
	enum hl_cs_type cs_type;

	if (!hl_device_operational(hdev, &status)) {
		dev_warn_ratelimited(hdev->dev,
			"Device is %s. Can't submit new CS\n",
			hdev->status[status]);
		return -EBUSY;
	}

	if ((args->in.cs_flags & HL_CS_FLAGS_STAGED_SUBMISSION) &&
			!hdev->supports_staged_submission) {
		dev_err(hdev->dev, "staged submission not supported");
		return -EPERM;
	}

	cs_type_flags = args->in.cs_flags & HL_CS_FLAGS_TYPE_MASK;

	if (unlikely(cs_type_flags && !is_power_of_2(cs_type_flags))) {
		dev_err(hdev->dev,
			"CS type flags are mutually exclusive, context %d\n",
			ctx->asid);
		return -EINVAL;
	}

	cs_type = hl_cs_get_cs_type(cs_type_flags);
	num_chunks = args->in.num_chunks_execute;

	if (unlikely((cs_type != CS_TYPE_DEFAULT) &&
					!hdev->supports_sync_stream)) {
		dev_err(hdev->dev, "Sync stream CS is not supported\n");
		return -EINVAL;
	}

	if (cs_type == CS_TYPE_DEFAULT) {
		if (!num_chunks) {
			dev_err(hdev->dev,
				"Got execute CS with 0 chunks, context %d\n",
				ctx->asid);
			return -EINVAL;
		}
	} else if (num_chunks != 1) {
		dev_err(hdev->dev,
			"Sync stream CS mandates one chunk only, context %d\n",
			ctx->asid);
		return -EINVAL;
	}

	return 0;
}

static int hl_cs_copy_chunk_array(struct hl_device *hdev,
					struct hl_cs_chunk **cs_chunk_array,
					void __user *chunks, u32 num_chunks,
					struct hl_ctx *ctx)
{
	u32 size_to_copy;

	if (num_chunks > HL_MAX_JOBS_PER_CS) {
		atomic64_inc(&ctx->cs_counters.validation_drop_cnt);
		atomic64_inc(&hdev->aggregated_cs_counters.validation_drop_cnt);
		dev_err(hdev->dev,
			"Number of chunks can NOT be larger than %d\n",
			HL_MAX_JOBS_PER_CS);
		return -EINVAL;
	}

	*cs_chunk_array = kmalloc_array(num_chunks, sizeof(**cs_chunk_array),
					GFP_ATOMIC);
	if (!*cs_chunk_array)
		*cs_chunk_array = kmalloc_array(num_chunks,
					sizeof(**cs_chunk_array), GFP_KERNEL);
	if (!*cs_chunk_array) {
		atomic64_inc(&ctx->cs_counters.out_of_mem_drop_cnt);
		atomic64_inc(&hdev->aggregated_cs_counters.out_of_mem_drop_cnt);
		return -ENOMEM;
	}

	size_to_copy = num_chunks * sizeof(struct hl_cs_chunk);
	if (copy_from_user(*cs_chunk_array, chunks, size_to_copy)) {
		atomic64_inc(&ctx->cs_counters.validation_drop_cnt);
		atomic64_inc(&hdev->aggregated_cs_counters.validation_drop_cnt);
		dev_err(hdev->dev, "Failed to copy cs chunk array from user\n");
		kfree(*cs_chunk_array);
		return -EFAULT;
	}

	return 0;
}

static int cs_staged_submission(struct hl_device *hdev, struct hl_cs *cs,
				u64 sequence, u32 flags)
{
	if (!(flags & HL_CS_FLAGS_STAGED_SUBMISSION))
		return 0;

	cs->staged_last = !!(flags & HL_CS_FLAGS_STAGED_SUBMISSION_LAST);
	cs->staged_first = !!(flags & HL_CS_FLAGS_STAGED_SUBMISSION_FIRST);

	if (cs->staged_first) {
		/* Staged CS sequence is the first CS sequence */
		INIT_LIST_HEAD(&cs->staged_cs_node);
		cs->staged_sequence = cs->sequence;
	} else {
		/* User sequence will be validated in 'hl_hw_queue_schedule_cs'
		 * under the cs_mirror_lock
		 */
		cs->staged_sequence = sequence;
	}

	/* Increment CS reference if needed */
	staged_cs_get(hdev, cs);

	cs->staged_cs = true;

	return 0;
}

static int cs_ioctl_default(struct hl_fpriv *hpriv, void __user *chunks,
				u32 num_chunks, u64 *cs_seq, u32 flags,
				u32 timeout)
{
	bool staged_mid, int_queues_only = true;
	struct hl_device *hdev = hpriv->hdev;
	struct hl_cs_chunk *cs_chunk_array;
	struct hl_cs_counters_atomic *cntr;
	struct hl_ctx *ctx = hpriv->ctx;
	struct hl_cs_job *job;
	struct hl_cs *cs;
	struct hl_cb *cb;
	u64 user_sequence;
	int rc, i;

	cntr = &hdev->aggregated_cs_counters;
	user_sequence = *cs_seq;
	*cs_seq = ULLONG_MAX;

	rc = hl_cs_copy_chunk_array(hdev, &cs_chunk_array, chunks, num_chunks,
			hpriv->ctx);
	if (rc)
		goto out;

	if ((flags & HL_CS_FLAGS_STAGED_SUBMISSION) &&
			!(flags & HL_CS_FLAGS_STAGED_SUBMISSION_FIRST))
		staged_mid = true;
	else
		staged_mid = false;

	rc = allocate_cs(hdev, hpriv->ctx, CS_TYPE_DEFAULT,
			staged_mid ? user_sequence : ULLONG_MAX, &cs, flags,
			timeout);
	if (rc)
		goto free_cs_chunk_array;

	*cs_seq = cs->sequence;

	hl_debugfs_add_cs(cs);

	rc = cs_staged_submission(hdev, cs, user_sequence, flags);
	if (rc)
		goto free_cs_object;

	/* Validate ALL the CS chunks before submitting the CS */
	for (i = 0 ; i < num_chunks ; i++) {
		struct hl_cs_chunk *chunk = &cs_chunk_array[i];
		enum hl_queue_type queue_type;
		bool is_kernel_allocated_cb;

		rc = validate_queue_index(hdev, chunk, &queue_type,
						&is_kernel_allocated_cb);
		if (rc) {
			atomic64_inc(&ctx->cs_counters.validation_drop_cnt);
			atomic64_inc(&cntr->validation_drop_cnt);
			goto free_cs_object;
		}

		if (is_kernel_allocated_cb) {
			cb = get_cb_from_cs_chunk(hdev, &hpriv->cb_mgr, chunk);
			if (!cb) {
				atomic64_inc(
					&ctx->cs_counters.validation_drop_cnt);
				atomic64_inc(&cntr->validation_drop_cnt);
				rc = -EINVAL;
				goto free_cs_object;
			}
		} else {
			cb = (struct hl_cb *) (uintptr_t) chunk->cb_handle;
		}

		if (queue_type == QUEUE_TYPE_EXT || queue_type == QUEUE_TYPE_HW)
			int_queues_only = false;

		job = hl_cs_allocate_job(hdev, queue_type,
						is_kernel_allocated_cb);
		if (!job) {
			atomic64_inc(&ctx->cs_counters.out_of_mem_drop_cnt);
			atomic64_inc(&cntr->out_of_mem_drop_cnt);
			dev_err(hdev->dev, "Failed to allocate a new job\n");
			rc = -ENOMEM;
			if (is_kernel_allocated_cb)
				goto release_cb;

			goto free_cs_object;
		}

		job->id = i + 1;
		job->cs = cs;
		job->user_cb = cb;
		job->user_cb_size = chunk->cb_size;
		job->hw_queue_id = chunk->queue_index;

		cs->jobs_in_queue_cnt[job->hw_queue_id]++;

		list_add_tail(&job->cs_node, &cs->job_list);

		/*
		 * Increment CS reference. When CS reference is 0, CS is
		 * done and can be signaled to user and free all its resources
		 * Only increment for JOB on external or H/W queues, because
		 * only for those JOBs we get completion
		 */
		if (cs_needs_completion(cs) &&
			(job->queue_type == QUEUE_TYPE_EXT ||
				job->queue_type == QUEUE_TYPE_HW))
			cs_get(cs);

		hl_debugfs_add_job(hdev, job);

		rc = cs_parser(hpriv, job);
		if (rc) {
			atomic64_inc(&ctx->cs_counters.parsing_drop_cnt);
			atomic64_inc(&cntr->parsing_drop_cnt);
			dev_err(hdev->dev,
				"Failed to parse JOB %d.%llu.%d, err %d, rejecting the CS\n",
				cs->ctx->asid, cs->sequence, job->id, rc);
			goto free_cs_object;
		}
	}

	/* We allow a CS with any queue type combination as long as it does
	 * not get a completion
	 */
	if (int_queues_only && cs_needs_completion(cs)) {
		atomic64_inc(&ctx->cs_counters.validation_drop_cnt);
		atomic64_inc(&cntr->validation_drop_cnt);
		dev_err(hdev->dev,
			"Reject CS %d.%llu since it contains only internal queues jobs and needs completion\n",
			cs->ctx->asid, cs->sequence);
		rc = -EINVAL;
		goto free_cs_object;
	}

	rc = hl_hw_queue_schedule_cs(cs);
	if (rc) {
		if (rc != -EAGAIN)
			dev_err(hdev->dev,
				"Failed to submit CS %d.%llu to H/W queues, error %d\n",
				cs->ctx->asid, cs->sequence, rc);
		goto free_cs_object;
	}

	rc = HL_CS_STATUS_SUCCESS;
	goto put_cs;

release_cb:
	atomic_dec(&cb->cs_cnt);
	hl_cb_put(cb);
free_cs_object:
	cs_rollback(hdev, cs);
	*cs_seq = ULLONG_MAX;
	/* The path below is both for good and erroneous exits */
put_cs:
	/* We finished with the CS in this function, so put the ref */
	cs_put(cs);
free_cs_chunk_array:
	kfree(cs_chunk_array);
out:
	return rc;
}

static int pending_cb_create_job(struct hl_device *hdev, struct hl_ctx *ctx,
		struct hl_cs *cs, struct hl_cb *cb, u32 size, u32 hw_queue_id)
{
	struct hw_queue_properties *hw_queue_prop;
	struct hl_cs_counters_atomic *cntr;
	struct hl_cs_job *job;

	hw_queue_prop = &hdev->asic_prop.hw_queues_props[hw_queue_id];
	cntr = &hdev->aggregated_cs_counters;

	job = hl_cs_allocate_job(hdev, hw_queue_prop->type, true);
	if (!job) {
		atomic64_inc(&ctx->cs_counters.out_of_mem_drop_cnt);
		atomic64_inc(&cntr->out_of_mem_drop_cnt);
		dev_err(hdev->dev, "Failed to allocate a new job\n");
		return -ENOMEM;
	}

	job->id = 0;
	job->cs = cs;
	job->user_cb = cb;
	atomic_inc(&job->user_cb->cs_cnt);
	job->user_cb_size = size;
	job->hw_queue_id = hw_queue_id;
	job->patched_cb = job->user_cb;
	job->job_cb_size = job->user_cb_size;

	/* increment refcount as for external queues we get completion */
	cs_get(cs);

	cs->jobs_in_queue_cnt[job->hw_queue_id]++;

	list_add_tail(&job->cs_node, &cs->job_list);

	hl_debugfs_add_job(hdev, job);

	return 0;
}

static int hl_submit_pending_cb(struct hl_fpriv *hpriv)
{
	struct hl_device *hdev = hpriv->hdev;
	struct hl_ctx *ctx = hpriv->ctx;
	struct hl_pending_cb *pending_cb, *tmp;
	struct list_head local_cb_list;
	struct hl_cs *cs;
	struct hl_cb *cb;
	u32 hw_queue_id;
	u32 cb_size;
	int process_list, rc = 0;

	if (list_empty(&ctx->pending_cb_list))
		return 0;

	process_list = atomic_cmpxchg(&ctx->thread_pending_cb_token, 1, 0);

	/* Only a single thread is allowed to process the list */
	if (!process_list)
		return 0;

	if (list_empty(&ctx->pending_cb_list))
		goto free_pending_cb_token;

	/* move all list elements to a local list */
	INIT_LIST_HEAD(&local_cb_list);
	spin_lock(&ctx->pending_cb_lock);
	list_for_each_entry_safe(pending_cb, tmp, &ctx->pending_cb_list,
								cb_node)
		list_move_tail(&pending_cb->cb_node, &local_cb_list);
	spin_unlock(&ctx->pending_cb_lock);

	rc = allocate_cs(hdev, ctx, CS_TYPE_DEFAULT, ULLONG_MAX, &cs, 0,
				hdev->timeout_jiffies);
	if (rc)
		goto add_list_elements;

	hl_debugfs_add_cs(cs);

	/* Iterate through pending cb list, create jobs and add to CS */
	list_for_each_entry(pending_cb, &local_cb_list, cb_node) {
		cb = pending_cb->cb;
		cb_size = pending_cb->cb_size;
		hw_queue_id = pending_cb->hw_queue_id;

		rc = pending_cb_create_job(hdev, ctx, cs, cb, cb_size,
								hw_queue_id);
		if (rc)
			goto free_cs_object;
	}

	rc = hl_hw_queue_schedule_cs(cs);
	if (rc) {
		if (rc != -EAGAIN)
			dev_err(hdev->dev,
				"Failed to submit CS %d.%llu (%d)\n",
				ctx->asid, cs->sequence, rc);
		goto free_cs_object;
	}

	/* pending cb was scheduled successfully */
	list_for_each_entry_safe(pending_cb, tmp, &local_cb_list, cb_node) {
		list_del(&pending_cb->cb_node);
		kfree(pending_cb);
	}

	cs_put(cs);

	goto free_pending_cb_token;

free_cs_object:
	cs_rollback(hdev, cs);
	cs_put(cs);
add_list_elements:
	spin_lock(&ctx->pending_cb_lock);
	list_for_each_entry_safe_reverse(pending_cb, tmp, &local_cb_list,
								cb_node)
		list_move(&pending_cb->cb_node, &ctx->pending_cb_list);
	spin_unlock(&ctx->pending_cb_lock);
free_pending_cb_token:
	atomic_set(&ctx->thread_pending_cb_token, 1);

	return rc;
}

static int hl_cs_ctx_switch(struct hl_fpriv *hpriv, union hl_cs_args *args,
				u64 *cs_seq)
{
	struct hl_device *hdev = hpriv->hdev;
	struct hl_ctx *ctx = hpriv->ctx;
	bool need_soft_reset = false;
	int rc = 0, do_ctx_switch;
	void __user *chunks;
	u32 num_chunks, tmp;
	int ret;

	do_ctx_switch = atomic_cmpxchg(&ctx->thread_ctx_switch_token, 1, 0);

	if (do_ctx_switch || (args->in.cs_flags & HL_CS_FLAGS_FORCE_RESTORE)) {
		mutex_lock(&hpriv->restore_phase_mutex);

		if (do_ctx_switch) {
			rc = hdev->asic_funcs->context_switch(hdev, ctx->asid);
			if (rc) {
				dev_err_ratelimited(hdev->dev,
					"Failed to switch to context %d, rejecting CS! %d\n",
					ctx->asid, rc);
				/*
				 * If we timedout, or if the device is not IDLE
				 * while we want to do context-switch (-EBUSY),
				 * we need to soft-reset because QMAN is
				 * probably stuck. However, we can't call to
				 * reset here directly because of deadlock, so
				 * need to do it at the very end of this
				 * function
				 */
				if ((rc == -ETIMEDOUT) || (rc == -EBUSY))
					need_soft_reset = true;
				mutex_unlock(&hpriv->restore_phase_mutex);
				goto out;
			}
		}

		hdev->asic_funcs->restore_phase_topology(hdev);

		chunks = (void __user *) (uintptr_t) args->in.chunks_restore;
		num_chunks = args->in.num_chunks_restore;

		if (!num_chunks) {
			dev_dbg(hdev->dev,
				"Need to run restore phase but restore CS is empty\n");
			rc = 0;
		} else {
			rc = cs_ioctl_default(hpriv, chunks, num_chunks,
					cs_seq, 0, hdev->timeout_jiffies);
		}

		mutex_unlock(&hpriv->restore_phase_mutex);

		if (rc) {
			dev_err(hdev->dev,
				"Failed to submit restore CS for context %d (%d)\n",
				ctx->asid, rc);
			goto out;
		}

		/* Need to wait for restore completion before execution phase */
		if (num_chunks) {
			enum hl_cs_wait_status status;
wait_again:
			ret = _hl_cs_wait_ioctl(hdev, ctx,
					jiffies_to_usecs(hdev->timeout_jiffies),
					*cs_seq, &status, NULL);
			if (ret) {
				if (ret == -ERESTARTSYS) {
					usleep_range(100, 200);
					goto wait_again;
				}

				dev_err(hdev->dev,
					"Restore CS for context %d failed to complete %d\n",
					ctx->asid, ret);
				rc = -ENOEXEC;
				goto out;
			}
		}

		ctx->thread_ctx_switch_wait_token = 1;

	} else if (!ctx->thread_ctx_switch_wait_token) {
		rc = hl_poll_timeout_memory(hdev,
			&ctx->thread_ctx_switch_wait_token, tmp, (tmp == 1),
			100, jiffies_to_usecs(hdev->timeout_jiffies), false);

		if (rc == -ETIMEDOUT) {
			dev_err(hdev->dev,
				"context switch phase timeout (%d)\n", tmp);
			goto out;
		}
	}

out:
	if ((rc == -ETIMEDOUT || rc == -EBUSY) && (need_soft_reset))
		hl_device_reset(hdev, 0);

	return rc;
}

/*
 * hl_cs_signal_sob_wraparound_handler: handle SOB value wrapaound case.
 * if the SOB value reaches the max value move to the other SOB reserved
 * to the queue.
 * Note that this function must be called while hw_queues_lock is taken.
 */
int hl_cs_signal_sob_wraparound_handler(struct hl_device *hdev, u32 q_idx,
			struct hl_hw_sob **hw_sob, u32 count)
{
	struct hl_sync_stream_properties *prop;
	struct hl_hw_sob *sob = *hw_sob, *other_sob;
	u8 other_sob_offset;

	prop = &hdev->kernel_queues[q_idx].sync_stream_prop;

	kref_get(&sob->kref);

	/* check for wraparound */
	if (prop->next_sob_val + count >= HL_MAX_SOB_VAL) {
		/*
		 * Decrement as we reached the max value.
		 * The release function won't be called here as we've
		 * just incremented the refcount right before calling this
		 * function.
		 */
		kref_put(&sob->kref, hl_sob_reset_error);

		/*
		 * check the other sob value, if it still in use then fail
		 * otherwise make the switch
		 */
		other_sob_offset = (prop->curr_sob_offset + 1) % HL_RSVD_SOBS;
		other_sob = &prop->hw_sob[other_sob_offset];

		if (kref_read(&other_sob->kref) != 1) {
			dev_err(hdev->dev, "error: Cannot switch SOBs q_idx: %d\n",
								q_idx);
			return -EINVAL;
		}

		prop->next_sob_val = 1;

		/* only two SOBs are currently in use */
		prop->curr_sob_offset = other_sob_offset;
		*hw_sob = other_sob;

		dev_dbg(hdev->dev, "switched to SOB %d, q_idx: %d\n",
				prop->curr_sob_offset, q_idx);
	} else {
		prop->next_sob_val += count;
	}

	return 0;
}

static int cs_ioctl_extract_signal_seq(struct hl_device *hdev,
		struct hl_cs_chunk *chunk, u64 *signal_seq, struct hl_ctx *ctx)
{
	u64 *signal_seq_arr = NULL;
	u32 size_to_copy, signal_seq_arr_len;
	int rc = 0;

	signal_seq_arr_len = chunk->num_signal_seq_arr;

	/* currently only one signal seq is supported */
	if (signal_seq_arr_len != 1) {
		atomic64_inc(&ctx->cs_counters.validation_drop_cnt);
		atomic64_inc(&hdev->aggregated_cs_counters.validation_drop_cnt);
		dev_err(hdev->dev,
			"Wait for signal CS supports only one signal CS seq\n");
		return -EINVAL;
	}

	signal_seq_arr = kmalloc_array(signal_seq_arr_len,
					sizeof(*signal_seq_arr),
					GFP_ATOMIC);
	if (!signal_seq_arr)
		signal_seq_arr = kmalloc_array(signal_seq_arr_len,
					sizeof(*signal_seq_arr),
					GFP_KERNEL);
	if (!signal_seq_arr) {
		atomic64_inc(&ctx->cs_counters.out_of_mem_drop_cnt);
		atomic64_inc(&hdev->aggregated_cs_counters.out_of_mem_drop_cnt);
		return -ENOMEM;
	}

	size_to_copy = chunk->num_signal_seq_arr * sizeof(*signal_seq_arr);
	if (copy_from_user(signal_seq_arr,
				u64_to_user_ptr(chunk->signal_seq_arr),
				size_to_copy)) {
		atomic64_inc(&ctx->cs_counters.validation_drop_cnt);
		atomic64_inc(&hdev->aggregated_cs_counters.validation_drop_cnt);
		dev_err(hdev->dev,
			"Failed to copy signal seq array from user\n");
		rc = -EFAULT;
		goto out;
	}

	/* currently it is guaranteed to have only one signal seq */
	*signal_seq = signal_seq_arr[0];

out:
	kfree(signal_seq_arr);

	return rc;
}

static int cs_ioctl_signal_wait_create_jobs(struct hl_device *hdev,
		struct hl_ctx *ctx, struct hl_cs *cs, enum hl_queue_type q_type,
		u32 q_idx)
{
	struct hl_cs_counters_atomic *cntr;
	struct hl_cs_job *job;
	struct hl_cb *cb;
	u32 cb_size;

	cntr = &hdev->aggregated_cs_counters;

	job = hl_cs_allocate_job(hdev, q_type, true);
	if (!job) {
		atomic64_inc(&ctx->cs_counters.out_of_mem_drop_cnt);
		atomic64_inc(&cntr->out_of_mem_drop_cnt);
		dev_err(hdev->dev, "Failed to allocate a new job\n");
		return -ENOMEM;
	}

	if (cs->type == CS_TYPE_WAIT)
		cb_size = hdev->asic_funcs->get_wait_cb_size(hdev);
	else
		cb_size = hdev->asic_funcs->get_signal_cb_size(hdev);

	cb = hl_cb_kernel_create(hdev, cb_size,
				q_type == QUEUE_TYPE_HW && hdev->mmu_enable);
	if (!cb) {
		atomic64_inc(&ctx->cs_counters.out_of_mem_drop_cnt);
		atomic64_inc(&cntr->out_of_mem_drop_cnt);
		kfree(job);
		return -EFAULT;
	}

	job->id = 0;
	job->cs = cs;
	job->user_cb = cb;
	atomic_inc(&job->user_cb->cs_cnt);
	job->user_cb_size = cb_size;
	job->hw_queue_id = q_idx;

	/*
	 * No need in parsing, user CB is the patched CB.
	 * We call hl_cb_destroy() out of two reasons - we don't need the CB in
	 * the CB idr anymore and to decrement its refcount as it was
	 * incremented inside hl_cb_kernel_create().
	 */
	job->patched_cb = job->user_cb;
	job->job_cb_size = job->user_cb_size;
	hl_cb_destroy(hdev, &hdev->kernel_cb_mgr, cb->id << PAGE_SHIFT);

	/* increment refcount as for external queues we get completion */
	cs_get(cs);

	cs->jobs_in_queue_cnt[job->hw_queue_id]++;

	list_add_tail(&job->cs_node, &cs->job_list);

	hl_debugfs_add_job(hdev, job);

	return 0;
}

static int cs_ioctl_signal_wait(struct hl_fpriv *hpriv, enum hl_cs_type cs_type,
				void __user *chunks, u32 num_chunks,
				u64 *cs_seq, u32 flags, u32 timeout)
{
	struct hl_cs_chunk *cs_chunk_array, *chunk;
	struct hw_queue_properties *hw_queue_prop;
	struct hl_device *hdev = hpriv->hdev;
	struct hl_cs_compl *sig_waitcs_cmpl;
	u32 q_idx, collective_engine_id = 0;
	struct hl_cs_counters_atomic *cntr;
	struct hl_fence *sig_fence = NULL;
	struct hl_ctx *ctx = hpriv->ctx;
	enum hl_queue_type q_type;
	struct hl_cs *cs;
	u64 signal_seq;
	int rc;

	cntr = &hdev->aggregated_cs_counters;
	*cs_seq = ULLONG_MAX;

	rc = hl_cs_copy_chunk_array(hdev, &cs_chunk_array, chunks, num_chunks,
			ctx);
	if (rc)
		goto out;

	/* currently it is guaranteed to have only one chunk */
	chunk = &cs_chunk_array[0];

	if (chunk->queue_index >= hdev->asic_prop.max_queues) {
		atomic64_inc(&ctx->cs_counters.validation_drop_cnt);
		atomic64_inc(&cntr->validation_drop_cnt);
		dev_err(hdev->dev, "Queue index %d is invalid\n",
			chunk->queue_index);
		rc = -EINVAL;
		goto free_cs_chunk_array;
	}

	q_idx = chunk->queue_index;
	hw_queue_prop = &hdev->asic_prop.hw_queues_props[q_idx];
	q_type = hw_queue_prop->type;

	if (!hw_queue_prop->supports_sync_stream) {
		atomic64_inc(&ctx->cs_counters.validation_drop_cnt);
		atomic64_inc(&cntr->validation_drop_cnt);
		dev_err(hdev->dev,
			"Queue index %d does not support sync stream operations\n",
			q_idx);
		rc = -EINVAL;
		goto free_cs_chunk_array;
	}

	if (cs_type == CS_TYPE_COLLECTIVE_WAIT) {
		if (!(hw_queue_prop->collective_mode == HL_COLLECTIVE_MASTER)) {
			atomic64_inc(&ctx->cs_counters.validation_drop_cnt);
			atomic64_inc(&cntr->validation_drop_cnt);
			dev_err(hdev->dev,
				"Queue index %d is invalid\n", q_idx);
			rc = -EINVAL;
			goto free_cs_chunk_array;
		}

		collective_engine_id = chunk->collective_engine_id;
	}

	if (cs_type == CS_TYPE_WAIT || cs_type == CS_TYPE_COLLECTIVE_WAIT) {
		rc = cs_ioctl_extract_signal_seq(hdev, chunk, &signal_seq, ctx);
		if (rc)
			goto free_cs_chunk_array;

		sig_fence = hl_ctx_get_fence(ctx, signal_seq);
		if (IS_ERR(sig_fence)) {
			atomic64_inc(&ctx->cs_counters.validation_drop_cnt);
			atomic64_inc(&cntr->validation_drop_cnt);
			dev_err(hdev->dev,
				"Failed to get signal CS with seq 0x%llx\n",
				signal_seq);
			rc = PTR_ERR(sig_fence);
			goto free_cs_chunk_array;
		}

		if (!sig_fence) {
			/* signal CS already finished */
			rc = 0;
			goto free_cs_chunk_array;
		}

		sig_waitcs_cmpl =
			container_of(sig_fence, struct hl_cs_compl, base_fence);

		if (sig_waitcs_cmpl->type != CS_TYPE_SIGNAL) {
			atomic64_inc(&ctx->cs_counters.validation_drop_cnt);
			atomic64_inc(&cntr->validation_drop_cnt);
			dev_err(hdev->dev,
				"CS seq 0x%llx is not of a signal CS\n",
				signal_seq);
			hl_fence_put(sig_fence);
			rc = -EINVAL;
			goto free_cs_chunk_array;
		}

		if (completion_done(&sig_fence->completion)) {
			/* signal CS already finished */
			hl_fence_put(sig_fence);
			rc = 0;
			goto free_cs_chunk_array;
		}
	}

	rc = allocate_cs(hdev, ctx, cs_type, ULLONG_MAX, &cs, flags, timeout);
	if (rc) {
		if (cs_type == CS_TYPE_WAIT ||
			cs_type == CS_TYPE_COLLECTIVE_WAIT)
			hl_fence_put(sig_fence);
		goto free_cs_chunk_array;
	}

	/*
	 * Save the signal CS fence for later initialization right before
	 * hanging the wait CS on the queue.
	 */
	if (cs_type == CS_TYPE_WAIT || cs_type == CS_TYPE_COLLECTIVE_WAIT)
		cs->signal_fence = sig_fence;

	hl_debugfs_add_cs(cs);

	*cs_seq = cs->sequence;

	if (cs_type == CS_TYPE_WAIT || cs_type == CS_TYPE_SIGNAL)
		rc = cs_ioctl_signal_wait_create_jobs(hdev, ctx, cs, q_type,
				q_idx);
	else if (cs_type == CS_TYPE_COLLECTIVE_WAIT)
		rc = hdev->asic_funcs->collective_wait_create_jobs(hdev, ctx,
				cs, q_idx, collective_engine_id);
	else {
		atomic64_inc(&ctx->cs_counters.validation_drop_cnt);
		atomic64_inc(&cntr->validation_drop_cnt);
		rc = -EINVAL;
	}

	if (rc)
		goto free_cs_object;

	rc = hl_hw_queue_schedule_cs(cs);
	if (rc) {
		if (rc != -EAGAIN)
			dev_err(hdev->dev,
				"Failed to submit CS %d.%llu to H/W queues, error %d\n",
				ctx->asid, cs->sequence, rc);
		goto free_cs_object;
	}

	rc = HL_CS_STATUS_SUCCESS;
	goto put_cs;

free_cs_object:
	cs_rollback(hdev, cs);
	*cs_seq = ULLONG_MAX;
	/* The path below is both for good and erroneous exits */
put_cs:
	/* We finished with the CS in this function, so put the ref */
	cs_put(cs);
free_cs_chunk_array:
	kfree(cs_chunk_array);
out:
	return rc;
}

int hl_cs_ioctl(struct hl_fpriv *hpriv, void *data)
{
	union hl_cs_args *args = data;
	enum hl_cs_type cs_type;
	u64 cs_seq = ULONG_MAX;
	void __user *chunks;
	u32 num_chunks, flags, timeout;
	int rc;

	rc = hl_cs_sanity_checks(hpriv, args);
	if (rc)
		goto out;

	rc = hl_cs_ctx_switch(hpriv, args, &cs_seq);
	if (rc)
		goto out;

	rc = hl_submit_pending_cb(hpriv);
	if (rc)
		goto out;

	cs_type = hl_cs_get_cs_type(args->in.cs_flags &
					~HL_CS_FLAGS_FORCE_RESTORE);
	chunks = (void __user *) (uintptr_t) args->in.chunks_execute;
	num_chunks = args->in.num_chunks_execute;
	flags = args->in.cs_flags;

	/* In case this is a staged CS, user should supply the CS sequence */
	if ((flags & HL_CS_FLAGS_STAGED_SUBMISSION) &&
			!(flags & HL_CS_FLAGS_STAGED_SUBMISSION_FIRST))
		cs_seq = args->in.seq;

	timeout = flags & HL_CS_FLAGS_CUSTOM_TIMEOUT
			? msecs_to_jiffies(args->in.timeout * 1000)
			: hpriv->hdev->timeout_jiffies;

	switch (cs_type) {
	case CS_TYPE_SIGNAL:
	case CS_TYPE_WAIT:
	case CS_TYPE_COLLECTIVE_WAIT:
		rc = cs_ioctl_signal_wait(hpriv, cs_type, chunks, num_chunks,
					&cs_seq, args->in.cs_flags, timeout);
		break;
	default:
		rc = cs_ioctl_default(hpriv, chunks, num_chunks, &cs_seq,
						args->in.cs_flags, timeout);
		break;
	}

out:
	if (rc != -EAGAIN) {
		memset(args, 0, sizeof(*args));
		args->out.status = rc;
		args->out.seq = cs_seq;
	}

	return rc;
}

static int _hl_cs_wait_ioctl(struct hl_device *hdev, struct hl_ctx *ctx,
				u64 timeout_us, u64 seq,
				enum hl_cs_wait_status *status, s64 *timestamp)
{
	struct hl_fence *fence;
	unsigned long timeout;
	int rc = 0;
	long completion_rc;

	if (timestamp)
		*timestamp = 0;

	if (timeout_us == MAX_SCHEDULE_TIMEOUT)
		timeout = timeout_us;
	else
		timeout = usecs_to_jiffies(timeout_us);

	hl_ctx_get(hdev, ctx);

	fence = hl_ctx_get_fence(ctx, seq);
	if (IS_ERR(fence)) {
		rc = PTR_ERR(fence);
		if (rc == -EINVAL)
			dev_notice_ratelimited(hdev->dev,
				"Can't wait on CS %llu because current CS is at seq %llu\n",
				seq, ctx->cs_sequence);
	} else if (fence) {
		if (!timeout_us)
			completion_rc = completion_done(&fence->completion);
		else
			completion_rc =
				wait_for_completion_interruptible_timeout(
					&fence->completion, timeout);

		if (completion_rc > 0) {
			*status = CS_WAIT_STATUS_COMPLETED;
			if (timestamp)
				*timestamp = ktime_to_ns(fence->timestamp);
		} else {
			*status = CS_WAIT_STATUS_BUSY;
		}

		if (fence->error == -ETIMEDOUT)
			rc = -ETIMEDOUT;
		else if (fence->error == -EIO)
			rc = -EIO;

		hl_fence_put(fence);
	} else {
		dev_dbg(hdev->dev,
			"Can't wait on seq %llu because current CS is at seq %llu (Fence is gone)\n",
			seq, ctx->cs_sequence);
		*status = CS_WAIT_STATUS_GONE;
	}

	hl_ctx_put(ctx);

	return rc;
}

static int hl_cs_wait_ioctl(struct hl_fpriv *hpriv, void *data)
{
	struct hl_device *hdev = hpriv->hdev;
	union hl_wait_cs_args *args = data;
	enum hl_cs_wait_status status;
	u64 seq = args->in.seq;
	s64 timestamp;
	int rc;

	rc = _hl_cs_wait_ioctl(hdev, hpriv->ctx, args->in.timeout_us, seq,
				&status, &timestamp);

	memset(args, 0, sizeof(*args));

	if (rc) {
		if (rc == -ERESTARTSYS) {
			dev_err_ratelimited(hdev->dev,
				"user process got signal while waiting for CS handle %llu\n",
				seq);
			args->out.status = HL_WAIT_CS_STATUS_INTERRUPTED;
			rc = -EINTR;
		} else if (rc == -ETIMEDOUT) {
			dev_err_ratelimited(hdev->dev,
				"CS %llu has timed-out while user process is waiting for it\n",
				seq);
			args->out.status = HL_WAIT_CS_STATUS_TIMEDOUT;
		} else if (rc == -EIO) {
			dev_err_ratelimited(hdev->dev,
				"CS %llu has been aborted while user process is waiting for it\n",
				seq);
			args->out.status = HL_WAIT_CS_STATUS_ABORTED;
		}
		return rc;
	}

	if (timestamp) {
		args->out.flags |= HL_WAIT_CS_STATUS_FLAG_TIMESTAMP_VLD;
		args->out.timestamp_nsec = timestamp;
	}

	switch (status) {
	case CS_WAIT_STATUS_GONE:
		args->out.flags |= HL_WAIT_CS_STATUS_FLAG_GONE;
		fallthrough;
	case CS_WAIT_STATUS_COMPLETED:
		args->out.status = HL_WAIT_CS_STATUS_COMPLETED;
		break;
	case CS_WAIT_STATUS_BUSY:
	default:
		args->out.status = HL_WAIT_CS_STATUS_BUSY;
		break;
	}

	return 0;
}

static int _hl_interrupt_wait_ioctl(struct hl_device *hdev, struct hl_ctx *ctx,
				u32 timeout_us, u64 user_address,
				u32 target_value, u16 interrupt_offset,
				enum hl_cs_wait_status *status)
{
	struct hl_user_pending_interrupt *pend;
	struct hl_user_interrupt *interrupt;
	unsigned long timeout;
	long completion_rc;
	u32 completion_value;
	int rc = 0;

	if (timeout_us == U32_MAX)
		timeout = timeout_us;
	else
		timeout = usecs_to_jiffies(timeout_us);

	hl_ctx_get(hdev, ctx);

	pend = kmalloc(sizeof(*pend), GFP_KERNEL);
	if (!pend) {
		hl_ctx_put(ctx);
		return -ENOMEM;
	}

	hl_fence_init(&pend->fence, ULONG_MAX);

	if (interrupt_offset == HL_COMMON_USER_INTERRUPT_ID)
		interrupt = &hdev->common_user_interrupt;
	else
		interrupt = &hdev->user_interrupt[interrupt_offset];

	spin_lock(&interrupt->wait_list_lock);
	if (!hl_device_operational(hdev, NULL)) {
		rc = -EPERM;
		goto unlock_and_free_fence;
	}

	if (copy_from_user(&completion_value, u64_to_user_ptr(user_address),
									4)) {
		dev_err(hdev->dev,
			"Failed to copy completion value from user\n");
		rc = -EFAULT;
		goto unlock_and_free_fence;
	}

	if (completion_value >= target_value)
		*status = CS_WAIT_STATUS_COMPLETED;
	else
		*status = CS_WAIT_STATUS_BUSY;

	if (!timeout_us || (*status == CS_WAIT_STATUS_COMPLETED))
		goto unlock_and_free_fence;

	/* Add pending user interrupt to relevant list for the interrupt
	 * handler to monitor
	 */
	list_add_tail(&pend->wait_list_node, &interrupt->wait_list_head);
	spin_unlock(&interrupt->wait_list_lock);

wait_again:
	/* Wait for interrupt handler to signal completion */
	completion_rc =
		wait_for_completion_interruptible_timeout(
				&pend->fence.completion, timeout);

	/* If timeout did not expire we need to perform the comparison.
	 * If comparison fails, keep waiting until timeout expires
	 */
	if (completion_rc > 0) {
		spin_lock(&interrupt->wait_list_lock);

		if (copy_from_user(&completion_value,
				u64_to_user_ptr(user_address), 4)) {

			spin_unlock(&interrupt->wait_list_lock);

			dev_err(hdev->dev,
				"Failed to copy completion value from user\n");
			rc = -EFAULT;

			goto remove_pending_user_interrupt;
		}

		if (completion_value >= target_value) {
			spin_unlock(&interrupt->wait_list_lock);
			*status = CS_WAIT_STATUS_COMPLETED;
		} else {
			reinit_completion(&pend->fence.completion);
			timeout = completion_rc;

			spin_unlock(&interrupt->wait_list_lock);
			goto wait_again;
		}
	} else {
		*status = CS_WAIT_STATUS_BUSY;
	}

remove_pending_user_interrupt:
	spin_lock(&interrupt->wait_list_lock);
	list_del(&pend->wait_list_node);

unlock_and_free_fence:
	spin_unlock(&interrupt->wait_list_lock);
	kfree(pend);
	hl_ctx_put(ctx);

	return rc;
}

static int hl_interrupt_wait_ioctl(struct hl_fpriv *hpriv, void *data)
{
	u16 interrupt_id, interrupt_offset, first_interrupt, last_interrupt;
	struct hl_device *hdev = hpriv->hdev;
	struct asic_fixed_properties *prop;
	union hl_wait_cs_args *args = data;
	enum hl_cs_wait_status status;
	int rc;

	prop = &hdev->asic_prop;

	if (!prop->user_interrupt_count) {
		dev_err(hdev->dev, "no user interrupts allowed");
		return -EPERM;
	}

	interrupt_id =
		FIELD_GET(HL_WAIT_CS_FLAGS_INTERRUPT_MASK, args->in.flags);

	first_interrupt = prop->first_available_user_msix_interrupt;
	last_interrupt = prop->first_available_user_msix_interrupt +
						prop->user_interrupt_count - 1;

	if ((interrupt_id < first_interrupt || interrupt_id > last_interrupt) &&
			interrupt_id != HL_COMMON_USER_INTERRUPT_ID) {
		dev_err(hdev->dev, "invalid user interrupt %u", interrupt_id);
		return -EINVAL;
	}

	if (interrupt_id == HL_COMMON_USER_INTERRUPT_ID)
		interrupt_offset = HL_COMMON_USER_INTERRUPT_ID;
	else
		interrupt_offset = interrupt_id - first_interrupt;

	rc = _hl_interrupt_wait_ioctl(hdev, hpriv->ctx,
				args->in.interrupt_timeout_us, args->in.addr,
				args->in.target, interrupt_offset, &status);

	memset(args, 0, sizeof(*args));

	if (rc) {
		dev_err_ratelimited(hdev->dev,
			"interrupt_wait_ioctl failed (%d)\n", rc);

		return rc;
	}

	switch (status) {
	case CS_WAIT_STATUS_COMPLETED:
		args->out.status = HL_WAIT_CS_STATUS_COMPLETED;
		break;
	case CS_WAIT_STATUS_BUSY:
	default:
		args->out.status = HL_WAIT_CS_STATUS_BUSY;
		break;
	}

	return 0;
}

int hl_wait_ioctl(struct hl_fpriv *hpriv, void *data)
{
	union hl_wait_cs_args *args = data;
	u32 flags = args->in.flags;
	int rc;

	if (flags & HL_WAIT_CS_FLAGS_INTERRUPT)
		rc = hl_interrupt_wait_ioctl(hpriv, data);
	else
		rc = hl_cs_wait_ioctl(hpriv, data);

	return rc;
}
