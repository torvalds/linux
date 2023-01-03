// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include <uapi/misc/habanalabs.h>
#include "habanalabs.h"

#include <linux/uaccess.h>
#include <linux/slab.h>

#define HL_CS_FLAGS_TYPE_MASK	(HL_CS_FLAGS_SIGNAL | HL_CS_FLAGS_WAIT | \
			HL_CS_FLAGS_COLLECTIVE_WAIT | HL_CS_FLAGS_RESERVE_SIGNALS_ONLY | \
			HL_CS_FLAGS_UNRESERVE_SIGNALS_ONLY | HL_CS_FLAGS_ENGINE_CORE_COMMAND)


#define MAX_TS_ITER_NUM 10

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
static int _hl_cs_wait_ioctl(struct hl_device *hdev, struct hl_ctx *ctx, u64 timeout_us, u64 seq,
				enum hl_cs_wait_status *status, s64 *timestamp);
static void cs_do_release(struct kref *ref);

static void hl_push_cs_outcome(struct hl_device *hdev,
			       struct hl_cs_outcome_store *outcome_store,
			       u64 seq, ktime_t ts, int error)
{
	struct hl_cs_outcome *node;
	unsigned long flags;

	/*
	 * CS outcome store supports the following operations:
	 * push outcome - store a recent CS outcome in the store
	 * pop outcome - retrieve a SPECIFIC (by seq) CS outcome from the store
	 * It uses 2 lists: used list and free list.
	 * It has a pre-allocated amount of nodes, each node stores
	 * a single CS outcome.
	 * Initially, all the nodes are in the free list.
	 * On push outcome, a node (any) is taken from the free list, its
	 * information is filled in, and the node is moved to the used list.
	 * It is possible, that there are no nodes left in the free list.
	 * In this case, we will lose some information about old outcomes. We
	 * will pop the OLDEST node from the used list, and make it free.
	 * On pop, the node is searched for in the used list (using a search
	 * index).
	 * If found, the node is then removed from the used list, and moved
	 * back to the free list. The outcome data that the node contained is
	 * returned back to the user.
	 */

	spin_lock_irqsave(&outcome_store->db_lock, flags);

	if (list_empty(&outcome_store->free_list)) {
		node = list_last_entry(&outcome_store->used_list,
				       struct hl_cs_outcome, list_link);
		hash_del(&node->map_link);
		dev_dbg(hdev->dev, "CS %llu outcome was lost\n", node->seq);
	} else {
		node = list_last_entry(&outcome_store->free_list,
				       struct hl_cs_outcome, list_link);
	}

	list_del_init(&node->list_link);

	node->seq = seq;
	node->ts = ts;
	node->error = error;

	list_add(&node->list_link, &outcome_store->used_list);
	hash_add(outcome_store->outcome_map, &node->map_link, node->seq);

	spin_unlock_irqrestore(&outcome_store->db_lock, flags);
}

static bool hl_pop_cs_outcome(struct hl_cs_outcome_store *outcome_store,
			       u64 seq, ktime_t *ts, int *error)
{
	struct hl_cs_outcome *node;
	unsigned long flags;

	spin_lock_irqsave(&outcome_store->db_lock, flags);

	hash_for_each_possible(outcome_store->outcome_map, node, map_link, seq)
		if (node->seq == seq) {
			*ts = node->ts;
			*error = node->error;

			hash_del(&node->map_link);
			list_del_init(&node->list_link);
			list_add(&node->list_link, &outcome_store->free_list);

			spin_unlock_irqrestore(&outcome_store->db_lock, flags);

			return true;
		}

	spin_unlock_irqrestore(&outcome_store->db_lock, flags);

	return false;
}

static void hl_sob_reset(struct kref *ref)
{
	struct hl_hw_sob *hw_sob = container_of(ref, struct hl_hw_sob,
							kref);
	struct hl_device *hdev = hw_sob->hdev;

	dev_dbg(hdev->dev, "reset sob id %u\n", hw_sob->sob_id);

	hdev->asic_funcs->reset_sob(hdev, hw_sob);

	hw_sob->need_reset = false;
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

void hw_sob_put(struct hl_hw_sob *hw_sob)
{
	if (hw_sob)
		kref_put(&hw_sob->kref, hl_sob_reset);
}

static void hw_sob_put_err(struct hl_hw_sob *hw_sob)
{
	if (hw_sob)
		kref_put(&hw_sob->kref, hl_sob_reset_error);
}

void hw_sob_get(struct hl_hw_sob *hw_sob)
{
	if (hw_sob)
		kref_get(&hw_sob->kref);
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

static void hl_fence_release(struct kref *kref)
{
	struct hl_fence *fence =
		container_of(kref, struct hl_fence, refcount);
	struct hl_cs_compl *hl_cs_cmpl =
		container_of(fence, struct hl_cs_compl, base_fence);

	kfree(hl_cs_cmpl);
}

void hl_fence_put(struct hl_fence *fence)
{
	if (IS_ERR_OR_NULL(fence))
		return;
	kref_put(&fence->refcount, hl_fence_release);
}

void hl_fences_put(struct hl_fence **fence, int len)
{
	int i;

	for (i = 0; i < len; i++, fence++)
		hl_fence_put(*fence);
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
	fence->mcs_handling_done = false;
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

static void hl_cs_job_put(struct hl_cs_job *job)
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

static void hl_complete_job(struct hl_device *hdev, struct hl_cs_job *job)
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
	 * released here. This is also true for INT queues jobs which were
	 * allocated by driver.
	 */
	if ((job->is_kernel_allocated_cb &&
		((job->queue_type == QUEUE_TYPE_HW && hdev->mmu_enable) ||
				job->queue_type == QUEUE_TYPE_INT))) {
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
		(job->queue_type == QUEUE_TYPE_EXT || job->queue_type == QUEUE_TYPE_HW))
		cs_put(cs);

	hl_cs_job_put(job);
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
	struct hl_cs *next = NULL, *iter, *first_cs;

	if (!cs_needs_timeout(cs))
		return;

	spin_lock(&hdev->cs_mirror_lock);

	/* We need to handle tdr only once for the complete staged submission.
	 * Hence, we choose the CS that reaches this function first which is
	 * the CS marked as 'staged_last'.
	 * In case single staged cs was submitted which has both first and last
	 * indications, then "cs_find_first" below will return NULL, since we
	 * removed the cs node from the list before getting here,
	 * in such cases just continue with the cs to cancel it's TDR work.
	 */
	if (cs->staged_cs && cs->staged_last) {
		first_cs = hl_staged_cs_find_first(hdev, cs->staged_sequence);
		if (first_cs)
			cs = first_cs;
	}

	spin_unlock(&hdev->cs_mirror_lock);

	/* Don't cancel TDR in case this CS was timedout because we might be
	 * running from the TDR context
	 */
	if (cs->timedout || hdev->timeout_jiffies == MAX_SCHEDULE_TIMEOUT)
		return;

	if (cs->tdr_active)
		cancel_delayed_work_sync(&cs->work_tdr);

	spin_lock(&hdev->cs_mirror_lock);

	/* queue TDR for next CS */
	list_for_each_entry(iter, &hdev->cs_mirror_list, mirror_node)
		if (cs_needs_timeout(iter)) {
			next = iter;
			break;
		}

	if (next && !next->tdr_active) {
		next->tdr_active = true;
		schedule_delayed_work(&next->work_tdr, next->timeout_jiffies);
	}

	spin_unlock(&hdev->cs_mirror_lock);
}

/*
 * force_complete_multi_cs - complete all contexts that wait on multi-CS
 *
 * @hdev: pointer to habanalabs device structure
 */
static void force_complete_multi_cs(struct hl_device *hdev)
{
	int i;

	for (i = 0; i < MULTI_CS_MAX_USER_CTX; i++) {
		struct multi_cs_completion *mcs_compl;

		mcs_compl = &hdev->multi_cs_completion[i];

		spin_lock(&mcs_compl->lock);

		if (!mcs_compl->used) {
			spin_unlock(&mcs_compl->lock);
			continue;
		}

		/* when calling force complete no context should be waiting on
		 * multi-cS.
		 * We are calling the function as a protection for such case
		 * to free any pending context and print error message
		 */
		dev_err(hdev->dev,
				"multi-CS completion context %d still waiting when calling force completion\n",
				i);
		complete_all(&mcs_compl->completion);
		spin_unlock(&mcs_compl->lock);
	}
}

/*
 * complete_multi_cs - complete all waiting entities on multi-CS
 *
 * @hdev: pointer to habanalabs device structure
 * @cs: CS structure
 * The function signals a waiting entity that has an overlapping stream masters
 * with the completed CS.
 * For example:
 * - a completed CS worked on stream master QID 4, multi CS completion
 *   is actively waiting on stream master QIDs 3, 5. don't send signal as no
 *   common stream master QID
 * - a completed CS worked on stream master QID 4, multi CS completion
 *   is actively waiting on stream master QIDs 3, 4. send signal as stream
 *   master QID 4 is common
 */
static void complete_multi_cs(struct hl_device *hdev, struct hl_cs *cs)
{
	struct hl_fence *fence = cs->fence;
	int i;

	/* in case of multi CS check for completion only for the first CS */
	if (cs->staged_cs && !cs->staged_first)
		return;

	for (i = 0; i < MULTI_CS_MAX_USER_CTX; i++) {
		struct multi_cs_completion *mcs_compl;

		mcs_compl = &hdev->multi_cs_completion[i];
		if (!mcs_compl->used)
			continue;

		spin_lock(&mcs_compl->lock);

		/*
		 * complete if:
		 * 1. still waiting for completion
		 * 2. the completed CS has at least one overlapping stream
		 *    master with the stream masters in the completion
		 */
		if (mcs_compl->used &&
				(fence->stream_master_qid_map &
					mcs_compl->stream_master_qid_map)) {
			/* extract the timestamp only of first completed CS */
			if (!mcs_compl->timestamp)
				mcs_compl->timestamp = ktime_to_ns(fence->timestamp);

			complete_all(&mcs_compl->completion);

			/*
			 * Setting mcs_handling_done inside the lock ensures
			 * at least one fence have mcs_handling_done set to
			 * true before wait for mcs finish. This ensures at
			 * least one CS will be set as completed when polling
			 * mcs fences.
			 */
			fence->mcs_handling_done = true;
		}

		spin_unlock(&mcs_compl->lock);
	}
	/* In case CS completed without mcs completion initialized */
	fence->mcs_handling_done = true;
}

static inline void cs_release_sob_reset_handler(struct hl_device *hdev,
					struct hl_cs *cs,
					struct hl_cs_compl *hl_cs_cmpl)
{
	/* Skip this handler if the cs wasn't submitted, to avoid putting
	 * the hw_sob twice, since this case already handled at this point,
	 * also skip if the hw_sob pointer wasn't set.
	 */
	if (!hl_cs_cmpl->hw_sob || !cs->submitted)
		return;

	spin_lock(&hl_cs_cmpl->lock);

	/*
	 * we get refcount upon reservation of signals or signal/wait cs for the
	 * hw_sob object, and need to put it when the first staged cs
	 * (which cotains the encaps signals) or cs signal/wait is completed.
	 */
	if ((hl_cs_cmpl->type == CS_TYPE_SIGNAL) ||
			(hl_cs_cmpl->type == CS_TYPE_WAIT) ||
			(hl_cs_cmpl->type == CS_TYPE_COLLECTIVE_WAIT) ||
			(!!hl_cs_cmpl->encaps_signals)) {
		dev_dbg(hdev->dev,
				"CS 0x%llx type %d finished, sob_id: %d, sob_val: %u\n",
				hl_cs_cmpl->cs_seq,
				hl_cs_cmpl->type,
				hl_cs_cmpl->hw_sob->sob_id,
				hl_cs_cmpl->sob_val);

		hw_sob_put(hl_cs_cmpl->hw_sob);

		if (hl_cs_cmpl->type == CS_TYPE_COLLECTIVE_WAIT)
			hdev->asic_funcs->reset_sob_group(hdev,
					hl_cs_cmpl->sob_group);
	}

	spin_unlock(&hl_cs_cmpl->lock);
}

static void cs_do_release(struct kref *ref)
{
	struct hl_cs *cs = container_of(ref, struct hl_cs, refcount);
	struct hl_device *hdev = cs->ctx->hdev;
	struct hl_cs_job *job, *tmp;
	struct hl_cs_compl *hl_cs_cmpl =
			container_of(cs->fence, struct hl_cs_compl, base_fence);

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
		hl_complete_job(hdev, job);

	if (!cs->submitted) {
		/*
		 * In case the wait for signal CS was submitted, the fence put
		 * occurs in init_signal_wait_cs() or collective_wait_init_cs()
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
			struct hl_cs *staged_cs, *tmp_cs;

			list_for_each_entry_safe(staged_cs, tmp_cs,
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

		/* decrement refcount to handle when first staged cs
		 * with encaps signals is completed.
		 */
		if (hl_cs_cmpl->encaps_signals)
			kref_put(&hl_cs_cmpl->encaps_sig_hdl->refcount,
					hl_encaps_release_handle_and_put_ctx);
	}

	if ((cs->type == CS_TYPE_WAIT || cs->type == CS_TYPE_COLLECTIVE_WAIT) && cs->encaps_signals)
		kref_put(&cs->encaps_sig_hdl->refcount, hl_encaps_release_handle_and_put_ctx);

out:
	/* Must be called before hl_ctx_put because inside we use ctx to get
	 * the device
	 */
	hl_debugfs_remove_cs(cs);

	hdev->shadow_cs_queue[cs->sequence & (hdev->asic_prop.max_pending_cs - 1)] = NULL;

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

	if (cs->timestamp) {
		cs->fence->timestamp = ktime_get();
		hl_push_cs_outcome(hdev, &cs->ctx->outcome_store, cs->sequence,
				   cs->fence->timestamp, cs->fence->error);
	}

	hl_ctx_put(cs->ctx);

	complete_all(&cs->fence->completion);
	complete_multi_cs(hdev, cs);

	cs_release_sob_reset_handler(hdev, cs, hl_cs_cmpl);

	hl_fence_put(cs->fence);

	kfree(cs->jobs_in_queue_cnt);
	kfree(cs);
}

static void cs_timedout(struct work_struct *work)
{
	struct hl_device *hdev;
	u64 event_mask = 0x0;
	int rc;
	struct hl_cs *cs = container_of(work, struct hl_cs,
						 work_tdr.work);
	bool skip_reset_on_timeout = cs->skip_reset_on_timeout, device_reset = false;

	rc = cs_get_unless_zero(cs);
	if (!rc)
		return;

	if ((!cs->submitted) || (cs->completed)) {
		cs_put(cs);
		return;
	}

	hdev = cs->ctx->hdev;

	if (likely(!skip_reset_on_timeout)) {
		if (hdev->reset_on_lockup)
			device_reset = true;
		else
			hdev->reset_info.needs_reset = true;

		/* Mark the CS is timed out so we won't try to cancel its TDR */
		cs->timedout = true;
	}

	/* Save only the first CS timeout parameters */
	rc = atomic_cmpxchg(&hdev->captured_err_info.cs_timeout.write_enable, 1, 0);
	if (rc) {
		hdev->captured_err_info.cs_timeout.timestamp = ktime_get();
		hdev->captured_err_info.cs_timeout.seq = cs->sequence;
		event_mask |= HL_NOTIFIER_EVENT_CS_TIMEOUT;
	}

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

	rc = hl_state_dump(hdev);
	if (rc)
		dev_err(hdev->dev, "Error during system state dump %d\n", rc);

	cs_put(cs);

	if (device_reset) {
		event_mask |= HL_NOTIFIER_EVENT_DEVICE_RESET;
		hl_device_cond_reset(hdev, HL_DRV_RESET_TDR, event_mask);
	} else if (event_mask) {
		hl_notifier_event_send_all(hdev, event_mask);
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
	hl_ctx_get(ctx);

	cs->ctx = ctx;
	cs->submitted = false;
	cs->completed = false;
	cs->type = cs_type;
	cs->timestamp = !!(flags & HL_CS_FLAGS_TIMESTAMP);
	cs->encaps_signals = !!(flags & HL_CS_FLAGS_ENCAP_SIGNALS);
	cs->timeout_jiffies = timeout;
	cs->skip_reset_on_timeout =
		hdev->reset_info.skip_reset_on_timeout ||
		!!(flags & HL_CS_FLAGS_SKIP_RESET_ON_TIMEOUT);
	cs->submission_time_jiffies = jiffies;
	INIT_LIST_HEAD(&cs->job_list);
	INIT_DELAYED_WORK(&cs->work_tdr, cs_timedout);
	kref_init(&cs->refcount);
	spin_lock_init(&cs->job_lock);

	cs_cmpl = kzalloc(sizeof(*cs_cmpl), GFP_ATOMIC);
	if (!cs_cmpl)
		cs_cmpl = kzalloc(sizeof(*cs_cmpl), GFP_KERNEL);

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
		hl_complete_job(hdev, job);
}

/*
 * release_reserved_encaps_signals() - release reserved encapsulated signals.
 * @hdev: pointer to habanalabs device structure
 *
 * Release reserved encapsulated signals which weren't un-reserved, or for which a CS with
 * encapsulated signals wasn't submitted and thus weren't released as part of CS roll-back.
 * For these signals need also to put the refcount of the H/W SOB which was taken at the
 * reservation.
 */
static void release_reserved_encaps_signals(struct hl_device *hdev)
{
	struct hl_ctx *ctx = hl_get_compute_ctx(hdev);
	struct hl_cs_encaps_sig_handle *handle;
	struct hl_encaps_signals_mgr *mgr;
	u32 id;

	if (!ctx)
		return;

	mgr = &ctx->sig_mgr;

	idr_for_each_entry(&mgr->handles, handle, id)
		if (handle->cs_seq == ULLONG_MAX)
			kref_put(&handle->refcount, hl_encaps_release_handle_and_put_sob_ctx);

	hl_ctx_put(ctx);
}

void hl_cs_rollback_all(struct hl_device *hdev, bool skip_wq_flush)
{
	int i;
	struct hl_cs *cs, *tmp;

	if (!skip_wq_flush) {
		flush_workqueue(hdev->ts_free_obj_wq);

		/* flush all completions before iterating over the CS mirror list in
		 * order to avoid a race with the release functions
		 */
		for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
			flush_workqueue(hdev->cq_wq[i]);

		flush_workqueue(hdev->cs_cmplt_wq);
	}

	/* Make sure we don't have leftovers in the CS mirror list */
	list_for_each_entry_safe(cs, tmp, &hdev->cs_mirror_list, mirror_node) {
		cs_get(cs);
		cs->aborted = true;
		dev_warn_ratelimited(hdev->dev, "Killing CS %d.%llu\n",
					cs->ctx->asid, cs->sequence);
		cs_rollback(hdev, cs);
		cs_put(cs);
	}

	force_complete_multi_cs(hdev);

	release_reserved_encaps_signals(hdev);
}

static void
wake_pending_user_interrupt_threads(struct hl_user_interrupt *interrupt)
{
	struct hl_user_pending_interrupt *pend, *temp;
	unsigned long flags;

	spin_lock_irqsave(&interrupt->wait_list_lock, flags);
	list_for_each_entry_safe(pend, temp, &interrupt->wait_list_head, wait_list_node) {
		if (pend->ts_reg_info.buf) {
			list_del(&pend->wait_list_node);
			hl_mmap_mem_buf_put(pend->ts_reg_info.buf);
			hl_cb_put(pend->ts_reg_info.cq_cb);
		} else {
			pend->fence.error = -EIO;
			complete_all(&pend->fence.completion);
		}
	}
	spin_unlock_irqrestore(&interrupt->wait_list_lock, flags);
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

	interrupt = &hdev->common_user_cq_interrupt;
	wake_pending_user_interrupt_threads(interrupt);

	interrupt = &hdev->common_decoder_interrupt;
	wake_pending_user_interrupt_threads(interrupt);
}

static void job_wq_completion(struct work_struct *work)
{
	struct hl_cs_job *job = container_of(work, struct hl_cs_job,
						finish_work);
	struct hl_cs *cs = job->cs;
	struct hl_device *hdev = cs->ctx->hdev;

	/* job is no longer needed */
	hl_complete_job(hdev, job);
}

static void cs_completion(struct work_struct *work)
{
	struct hl_cs *cs = container_of(work, struct hl_cs, finish_work);
	struct hl_device *hdev = cs->ctx->hdev;
	struct hl_cs_job *job, *tmp;

	list_for_each_entry_safe(job, tmp, &cs->job_list, cs_node)
		hl_complete_job(hdev, job);
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
		dev_err(hdev->dev, "Queue index %d is not applicable\n",
			chunk->queue_index);
		return -EINVAL;
	}

	if (hw_queue_prop->binned) {
		dev_err(hdev->dev, "Queue index %d is binned out\n",
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
					struct hl_mem_mgr *mmg,
					struct hl_cs_chunk *chunk)
{
	struct hl_cb *cb;

	cb = hl_cb_get(mmg, chunk->cb_handle);
	if (!cb) {
		dev_err(hdev->dev, "CB handle 0x%llx invalid\n", chunk->cb_handle);
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
	else if (cs_type_flags & HL_CS_FLAGS_RESERVE_SIGNALS_ONLY)
		return CS_RESERVE_SIGNALS;
	else if (cs_type_flags & HL_CS_FLAGS_UNRESERVE_SIGNALS_ONLY)
		return CS_UNRESERVE_SIGNALS;
	else if (cs_type_flags & HL_CS_FLAGS_ENGINE_CORE_COMMAND)
		return CS_TYPE_ENGINE_CORE;
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
	bool is_sync_stream;

	if (!hl_device_operational(hdev, &status)) {
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

	is_sync_stream = (cs_type == CS_TYPE_SIGNAL || cs_type == CS_TYPE_WAIT ||
			cs_type == CS_TYPE_COLLECTIVE_WAIT);

	if (unlikely(is_sync_stream && !hdev->supports_sync_stream)) {
		dev_err(hdev->dev, "Sync stream CS is not supported\n");
		return -EINVAL;
	}

	if (cs_type == CS_TYPE_DEFAULT) {
		if (!num_chunks) {
			dev_err(hdev->dev, "Got execute CS with 0 chunks, context %d\n", ctx->asid);
			return -EINVAL;
		}
	} else if (is_sync_stream && num_chunks != 1) {
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
				u64 sequence, u32 flags,
				u32 encaps_signal_handle)
{
	if (!(flags & HL_CS_FLAGS_STAGED_SUBMISSION))
		return 0;

	cs->staged_last = !!(flags & HL_CS_FLAGS_STAGED_SUBMISSION_LAST);
	cs->staged_first = !!(flags & HL_CS_FLAGS_STAGED_SUBMISSION_FIRST);

	if (cs->staged_first) {
		/* Staged CS sequence is the first CS sequence */
		INIT_LIST_HEAD(&cs->staged_cs_node);
		cs->staged_sequence = cs->sequence;

		if (cs->encaps_signals)
			cs->encaps_sig_hdl_id = encaps_signal_handle;
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

static u32 get_stream_master_qid_mask(struct hl_device *hdev, u32 qid)
{
	int i;

	for (i = 0; i < hdev->stream_master_qid_arr_size; i++)
		if (qid == hdev->stream_master_qid_arr[i])
			return BIT(i);

	return 0;
}

static int cs_ioctl_default(struct hl_fpriv *hpriv, void __user *chunks,
				u32 num_chunks, u64 *cs_seq, u32 flags,
				u32 encaps_signals_handle, u32 timeout,
				u16 *signal_initial_sob_count)
{
	bool staged_mid, int_queues_only = true, using_hw_queues = false;
	struct hl_device *hdev = hpriv->hdev;
	struct hl_cs_chunk *cs_chunk_array;
	struct hl_cs_counters_atomic *cntr;
	struct hl_ctx *ctx = hpriv->ctx;
	struct hl_cs_job *job;
	struct hl_cs *cs;
	struct hl_cb *cb;
	u64 user_sequence;
	u8 stream_master_qid_map = 0;
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

	rc = cs_staged_submission(hdev, cs, user_sequence, flags,
						encaps_signals_handle);
	if (rc)
		goto free_cs_object;

	/* If this is a staged submission we must return the staged sequence
	 * rather than the internal CS sequence
	 */
	if (cs->staged_cs)
		*cs_seq = cs->staged_sequence;

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
			cb = get_cb_from_cs_chunk(hdev, &hpriv->mem_mgr, chunk);
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

		if (queue_type == QUEUE_TYPE_EXT ||
						queue_type == QUEUE_TYPE_HW) {
			int_queues_only = false;

			/*
			 * store which stream are being used for external/HW
			 * queues of this CS
			 */
			if (hdev->supports_wait_for_multi_cs)
				stream_master_qid_map |=
					get_stream_master_qid_mask(hdev,
							chunk->queue_index);
		}

		if (queue_type == QUEUE_TYPE_HW)
			using_hw_queues = true;

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
		cs->jobs_cnt++;

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

	if (using_hw_queues)
		INIT_WORK(&cs->finish_work, cs_completion);

	/*
	 * store the (external/HW queues) streams used by the CS in the
	 * fence object for multi-CS completion
	 */
	if (hdev->supports_wait_for_multi_cs)
		cs->fence->stream_master_qid_map = stream_master_qid_map;

	rc = hl_hw_queue_schedule_cs(cs);
	if (rc) {
		if (rc != -EAGAIN)
			dev_err(hdev->dev,
				"Failed to submit CS %d.%llu to H/W queues, error %d\n",
				cs->ctx->asid, cs->sequence, rc);
		goto free_cs_object;
	}

	*signal_initial_sob_count = cs->initial_sob_count;

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

static int hl_cs_ctx_switch(struct hl_fpriv *hpriv, union hl_cs_args *args,
				u64 *cs_seq)
{
	struct hl_device *hdev = hpriv->hdev;
	struct hl_ctx *ctx = hpriv->ctx;
	bool need_soft_reset = false;
	int rc = 0, do_ctx_switch = 0;
	void __user *chunks;
	u32 num_chunks, tmp;
	u16 sob_count;
	int ret;

	if (hdev->supports_ctx_switch)
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
					cs_seq, 0, 0, hdev->timeout_jiffies, &sob_count);
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

		if (hdev->supports_ctx_switch)
			ctx->thread_ctx_switch_wait_token = 1;

	} else if (hdev->supports_ctx_switch && !ctx->thread_ctx_switch_wait_token) {
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
 * @hdev: pointer to device structure
 * @q_idx: stream queue index
 * @hw_sob: the H/W SOB used in this signal CS.
 * @count: signals count
 * @encaps_sig: tells whether it's reservation for encaps signals or not.
 *
 * Note that this function must be called while hw_queues_lock is taken.
 */
int hl_cs_signal_sob_wraparound_handler(struct hl_device *hdev, u32 q_idx,
			struct hl_hw_sob **hw_sob, u32 count, bool encaps_sig)

{
	struct hl_sync_stream_properties *prop;
	struct hl_hw_sob *sob = *hw_sob, *other_sob;
	u8 other_sob_offset;

	prop = &hdev->kernel_queues[q_idx].sync_stream_prop;

	hw_sob_get(sob);

	/* check for wraparound */
	if (prop->next_sob_val + count >= HL_MAX_SOB_VAL) {
		/*
		 * Decrement as we reached the max value.
		 * The release function won't be called here as we've
		 * just incremented the refcount right before calling this
		 * function.
		 */
		hw_sob_put_err(sob);

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

		/*
		 * next_sob_val always points to the next available signal
		 * in the sob, so in encaps signals it will be the next one
		 * after reserving the required amount.
		 */
		if (encaps_sig)
			prop->next_sob_val = count + 1;
		else
			prop->next_sob_val = count;

		/* only two SOBs are currently in use */
		prop->curr_sob_offset = other_sob_offset;
		*hw_sob = other_sob;

		/*
		 * check if other_sob needs reset, then do it before using it
		 * for the reservation or the next signal cs.
		 * we do it here, and for both encaps and regular signal cs
		 * cases in order to avoid possible races of two kref_put
		 * of the sob which can occur at the same time if we move the
		 * sob reset(kref_put) to cs_do_release function.
		 * in addition, if we have combination of cs signal and
		 * encaps, and at the point we need to reset the sob there was
		 * no more reservations and only signal cs keep coming,
		 * in such case we need signal_cs to put the refcount and
		 * reset the sob.
		 */
		if (other_sob->need_reset)
			hw_sob_put(other_sob);

		if (encaps_sig) {
			/* set reset indication for the sob */
			sob->need_reset = true;
			hw_sob_get(other_sob);
		}

		dev_dbg(hdev->dev, "switched to SOB %d, q_idx: %d\n",
				prop->curr_sob_offset, q_idx);
	} else {
		prop->next_sob_val += count;
	}

	return 0;
}

static int cs_ioctl_extract_signal_seq(struct hl_device *hdev,
		struct hl_cs_chunk *chunk, u64 *signal_seq, struct hl_ctx *ctx,
		bool encaps_signals)
{
	u64 *signal_seq_arr = NULL;
	u32 size_to_copy, signal_seq_arr_len;
	int rc = 0;

	if (encaps_signals) {
		*signal_seq = chunk->encaps_signal_seq;
		return 0;
	}

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

	size_to_copy = signal_seq_arr_len * sizeof(*signal_seq_arr);
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
		struct hl_ctx *ctx, struct hl_cs *cs,
		enum hl_queue_type q_type, u32 q_idx, u32 encaps_signal_offset)
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

	if ((cs->type == CS_TYPE_WAIT || cs->type == CS_TYPE_COLLECTIVE_WAIT)
			&& cs->encaps_signals)
		job->encaps_sig_wait_offset = encaps_signal_offset;
	/*
	 * No need in parsing, user CB is the patched CB.
	 * We call hl_cb_destroy() out of two reasons - we don't need the CB in
	 * the CB idr anymore and to decrement its refcount as it was
	 * incremented inside hl_cb_kernel_create().
	 */
	job->patched_cb = job->user_cb;
	job->job_cb_size = job->user_cb_size;
	hl_cb_destroy(&hdev->kernel_mem_mgr, cb->buf->handle);

	/* increment refcount as for external queues we get completion */
	cs_get(cs);

	cs->jobs_in_queue_cnt[job->hw_queue_id]++;
	cs->jobs_cnt++;

	list_add_tail(&job->cs_node, &cs->job_list);

	hl_debugfs_add_job(hdev, job);

	return 0;
}

static int cs_ioctl_reserve_signals(struct hl_fpriv *hpriv,
				u32 q_idx, u32 count,
				u32 *handle_id, u32 *sob_addr,
				u32 *signals_count)
{
	struct hw_queue_properties *hw_queue_prop;
	struct hl_sync_stream_properties *prop;
	struct hl_device *hdev = hpriv->hdev;
	struct hl_cs_encaps_sig_handle *handle;
	struct hl_encaps_signals_mgr *mgr;
	struct hl_hw_sob *hw_sob;
	int hdl_id;
	int rc = 0;

	if (count >= HL_MAX_SOB_VAL) {
		dev_err(hdev->dev, "signals count(%u) exceeds the max SOB value\n",
						count);
		rc = -EINVAL;
		goto out;
	}

	if (q_idx >= hdev->asic_prop.max_queues) {
		dev_err(hdev->dev, "Queue index %d is invalid\n",
			q_idx);
		rc = -EINVAL;
		goto out;
	}

	hw_queue_prop = &hdev->asic_prop.hw_queues_props[q_idx];

	if (!hw_queue_prop->supports_sync_stream) {
		dev_err(hdev->dev,
			"Queue index %d does not support sync stream operations\n",
									q_idx);
		rc = -EINVAL;
		goto out;
	}

	prop = &hdev->kernel_queues[q_idx].sync_stream_prop;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle) {
		rc = -ENOMEM;
		goto out;
	}

	handle->count = count;

	hl_ctx_get(hpriv->ctx);
	handle->ctx = hpriv->ctx;
	mgr = &hpriv->ctx->sig_mgr;

	spin_lock(&mgr->lock);
	hdl_id = idr_alloc(&mgr->handles, handle, 1, 0, GFP_ATOMIC);
	spin_unlock(&mgr->lock);

	if (hdl_id < 0) {
		dev_err(hdev->dev, "Failed to allocate IDR for a new signal reservation\n");
		rc = -EINVAL;
		goto put_ctx;
	}

	handle->id = hdl_id;
	handle->q_idx = q_idx;
	handle->hdev = hdev;
	kref_init(&handle->refcount);

	hdev->asic_funcs->hw_queues_lock(hdev);

	hw_sob = &prop->hw_sob[prop->curr_sob_offset];

	/*
	 * Increment the SOB value by count by user request
	 * to reserve those signals
	 * check if the signals amount to reserve is not exceeding the max sob
	 * value, if yes then switch sob.
	 */
	rc = hl_cs_signal_sob_wraparound_handler(hdev, q_idx, &hw_sob, count,
								true);
	if (rc) {
		dev_err(hdev->dev, "Failed to switch SOB\n");
		hdev->asic_funcs->hw_queues_unlock(hdev);
		rc = -EINVAL;
		goto remove_idr;
	}
	/* set the hw_sob to the handle after calling the sob wraparound handler
	 * since sob could have changed.
	 */
	handle->hw_sob = hw_sob;

	/* store the current sob value for unreserve validity check, and
	 * signal offset support
	 */
	handle->pre_sob_val = prop->next_sob_val - handle->count;

	handle->cs_seq = ULLONG_MAX;

	*signals_count = prop->next_sob_val;
	hdev->asic_funcs->hw_queues_unlock(hdev);

	*sob_addr = handle->hw_sob->sob_addr;
	*handle_id = hdl_id;

	dev_dbg(hdev->dev,
		"Signals reserved, sob_id: %d, sob addr: 0x%x, last sob_val: %u, q_idx: %d, hdl_id: %d\n",
			hw_sob->sob_id, handle->hw_sob->sob_addr,
			prop->next_sob_val - 1, q_idx, hdl_id);
	goto out;

remove_idr:
	spin_lock(&mgr->lock);
	idr_remove(&mgr->handles, hdl_id);
	spin_unlock(&mgr->lock);

put_ctx:
	hl_ctx_put(handle->ctx);
	kfree(handle);

out:
	return rc;
}

static int cs_ioctl_unreserve_signals(struct hl_fpriv *hpriv, u32 handle_id)
{
	struct hl_cs_encaps_sig_handle *encaps_sig_hdl;
	struct hl_sync_stream_properties *prop;
	struct hl_device *hdev = hpriv->hdev;
	struct hl_encaps_signals_mgr *mgr;
	struct hl_hw_sob *hw_sob;
	u32 q_idx, sob_addr;
	int rc = 0;

	mgr = &hpriv->ctx->sig_mgr;

	spin_lock(&mgr->lock);
	encaps_sig_hdl = idr_find(&mgr->handles, handle_id);
	if (encaps_sig_hdl) {
		dev_dbg(hdev->dev, "unreserve signals, handle: %u, SOB:0x%x, count: %u\n",
				handle_id, encaps_sig_hdl->hw_sob->sob_addr,
					encaps_sig_hdl->count);

		hdev->asic_funcs->hw_queues_lock(hdev);

		q_idx = encaps_sig_hdl->q_idx;
		prop = &hdev->kernel_queues[q_idx].sync_stream_prop;
		hw_sob = &prop->hw_sob[prop->curr_sob_offset];
		sob_addr = hdev->asic_funcs->get_sob_addr(hdev, hw_sob->sob_id);

		/* Check if sob_val got out of sync due to other
		 * signal submission requests which were handled
		 * between the reserve-unreserve calls or SOB switch
		 * upon reaching SOB max value.
		 */
		if (encaps_sig_hdl->pre_sob_val + encaps_sig_hdl->count
				!= prop->next_sob_val ||
				sob_addr != encaps_sig_hdl->hw_sob->sob_addr) {
			dev_err(hdev->dev, "Cannot unreserve signals, SOB val ran out of sync, expected: %u, actual val: %u\n",
				encaps_sig_hdl->pre_sob_val,
				(prop->next_sob_val - encaps_sig_hdl->count));

			hdev->asic_funcs->hw_queues_unlock(hdev);
			rc = -EINVAL;
			goto out;
		}

		/*
		 * Decrement the SOB value by count by user request
		 * to unreserve those signals
		 */
		prop->next_sob_val -= encaps_sig_hdl->count;

		hdev->asic_funcs->hw_queues_unlock(hdev);

		hw_sob_put(hw_sob);

		/* Release the id and free allocated memory of the handle */
		idr_remove(&mgr->handles, handle_id);
		hl_ctx_put(encaps_sig_hdl->ctx);
		kfree(encaps_sig_hdl);
	} else {
		rc = -EINVAL;
		dev_err(hdev->dev, "failed to unreserve signals, cannot find handler\n");
	}
out:
	spin_unlock(&mgr->lock);

	return rc;
}

static int cs_ioctl_signal_wait(struct hl_fpriv *hpriv, enum hl_cs_type cs_type,
				void __user *chunks, u32 num_chunks,
				u64 *cs_seq, u32 flags, u32 timeout,
				u32 *signal_sob_addr_offset, u16 *signal_initial_sob_count)
{
	struct hl_cs_encaps_sig_handle *encaps_sig_hdl = NULL;
	bool handle_found = false, is_wait_cs = false,
			wait_cs_submitted = false,
			cs_encaps_signals = false;
	struct hl_cs_chunk *cs_chunk_array, *chunk;
	bool staged_cs_with_encaps_signals = false;
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

		if (!hdev->nic_ports_mask) {
			atomic64_inc(&ctx->cs_counters.validation_drop_cnt);
			atomic64_inc(&cntr->validation_drop_cnt);
			dev_err(hdev->dev,
				"Collective operations not supported when NIC ports are disabled");
			rc = -EINVAL;
			goto free_cs_chunk_array;
		}

		collective_engine_id = chunk->collective_engine_id;
	}

	is_wait_cs = !!(cs_type == CS_TYPE_WAIT ||
			cs_type == CS_TYPE_COLLECTIVE_WAIT);

	cs_encaps_signals = !!(flags & HL_CS_FLAGS_ENCAP_SIGNALS);

	if (is_wait_cs) {
		rc = cs_ioctl_extract_signal_seq(hdev, chunk, &signal_seq,
				ctx, cs_encaps_signals);
		if (rc)
			goto free_cs_chunk_array;

		if (cs_encaps_signals) {
			/* check if cs sequence has encapsulated
			 * signals handle
			 */
			struct idr *idp;
			u32 id;

			spin_lock(&ctx->sig_mgr.lock);
			idp = &ctx->sig_mgr.handles;
			idr_for_each_entry(idp, encaps_sig_hdl, id) {
				if (encaps_sig_hdl->cs_seq == signal_seq) {
					/* get refcount to protect removing this handle from idr,
					 * needed when multiple wait cs are used with offset
					 * to wait on reserved encaps signals.
					 * Since kref_put of this handle is executed outside the
					 * current lock, it is possible that the handle refcount
					 * is 0 but it yet to be removed from the list. In this
					 * case need to consider the handle as not valid.
					 */
					if (kref_get_unless_zero(&encaps_sig_hdl->refcount))
						handle_found = true;
					break;
				}
			}
			spin_unlock(&ctx->sig_mgr.lock);

			if (!handle_found) {
				/* treat as signal CS already finished */
				dev_dbg(hdev->dev, "Cannot find encapsulated signals handle for seq 0x%llx\n",
						signal_seq);
				rc = 0;
				goto free_cs_chunk_array;
			}

			/* validate also the signal offset value */
			if (chunk->encaps_signal_offset >
					encaps_sig_hdl->count) {
				dev_err(hdev->dev, "offset(%u) value exceed max reserved signals count(%u)!\n",
						chunk->encaps_signal_offset,
						encaps_sig_hdl->count);
				rc = -EINVAL;
				goto free_cs_chunk_array;
			}
		}

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

		staged_cs_with_encaps_signals = !!
				(sig_waitcs_cmpl->type == CS_TYPE_DEFAULT &&
				(flags & HL_CS_FLAGS_ENCAP_SIGNALS));

		if (sig_waitcs_cmpl->type != CS_TYPE_SIGNAL &&
				!staged_cs_with_encaps_signals) {
			atomic64_inc(&ctx->cs_counters.validation_drop_cnt);
			atomic64_inc(&cntr->validation_drop_cnt);
			dev_err(hdev->dev,
				"CS seq 0x%llx is not of a signal/encaps-signal CS\n",
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
		if (is_wait_cs)
			hl_fence_put(sig_fence);

		goto free_cs_chunk_array;
	}

	/*
	 * Save the signal CS fence for later initialization right before
	 * hanging the wait CS on the queue.
	 * for encaps signals case, we save the cs sequence and handle pointer
	 * for later initialization.
	 */
	if (is_wait_cs) {
		cs->signal_fence = sig_fence;
		/* store the handle pointer, so we don't have to
		 * look for it again, later on the flow
		 * when we need to set SOB info in hw_queue.
		 */
		if (cs->encaps_signals)
			cs->encaps_sig_hdl = encaps_sig_hdl;
	}

	hl_debugfs_add_cs(cs);

	*cs_seq = cs->sequence;

	if (cs_type == CS_TYPE_WAIT || cs_type == CS_TYPE_SIGNAL)
		rc = cs_ioctl_signal_wait_create_jobs(hdev, ctx, cs, q_type,
				q_idx, chunk->encaps_signal_offset);
	else if (cs_type == CS_TYPE_COLLECTIVE_WAIT)
		rc = hdev->asic_funcs->collective_wait_create_jobs(hdev, ctx,
				cs, q_idx, collective_engine_id,
				chunk->encaps_signal_offset);
	else {
		atomic64_inc(&ctx->cs_counters.validation_drop_cnt);
		atomic64_inc(&cntr->validation_drop_cnt);
		rc = -EINVAL;
	}

	if (rc)
		goto free_cs_object;

	if (q_type == QUEUE_TYPE_HW)
		INIT_WORK(&cs->finish_work, cs_completion);

	rc = hl_hw_queue_schedule_cs(cs);
	if (rc) {
		/* In case wait cs failed here, it means the signal cs
		 * already completed. we want to free all it's related objects
		 * but we don't want to fail the ioctl.
		 */
		if (is_wait_cs)
			rc = 0;
		else if (rc != -EAGAIN)
			dev_err(hdev->dev,
				"Failed to submit CS %d.%llu to H/W queues, error %d\n",
				ctx->asid, cs->sequence, rc);
		goto free_cs_object;
	}

	*signal_sob_addr_offset = cs->sob_addr_offset;
	*signal_initial_sob_count = cs->initial_sob_count;

	rc = HL_CS_STATUS_SUCCESS;
	if (is_wait_cs)
		wait_cs_submitted = true;
	goto put_cs;

free_cs_object:
	cs_rollback(hdev, cs);
	*cs_seq = ULLONG_MAX;
	/* The path below is both for good and erroneous exits */
put_cs:
	/* We finished with the CS in this function, so put the ref */
	cs_put(cs);
free_cs_chunk_array:
	if (!wait_cs_submitted && cs_encaps_signals && handle_found && is_wait_cs)
		kref_put(&encaps_sig_hdl->refcount, hl_encaps_release_handle_and_put_ctx);
	kfree(cs_chunk_array);
out:
	return rc;
}

static int cs_ioctl_engine_cores(struct hl_fpriv *hpriv, u64 engine_cores,
						u32 num_engine_cores, u32 core_command)
{
	int rc;
	struct hl_device *hdev = hpriv->hdev;
	void __user *engine_cores_arr;
	u32 *cores;

	if (!num_engine_cores || num_engine_cores > hdev->asic_prop.num_engine_cores) {
		dev_err(hdev->dev, "Number of engine cores %d is invalid\n", num_engine_cores);
		return -EINVAL;
	}

	if (core_command != HL_ENGINE_CORE_RUN && core_command != HL_ENGINE_CORE_HALT) {
		dev_err(hdev->dev, "Engine core command is invalid\n");
		return -EINVAL;
	}

	engine_cores_arr = (void __user *) (uintptr_t) engine_cores;
	cores = kmalloc_array(num_engine_cores, sizeof(u32), GFP_KERNEL);
	if (!cores)
		return -ENOMEM;

	if (copy_from_user(cores, engine_cores_arr, num_engine_cores * sizeof(u32))) {
		dev_err(hdev->dev, "Failed to copy core-ids array from user\n");
		kfree(cores);
		return -EFAULT;
	}

	rc = hdev->asic_funcs->set_engine_cores(hdev, cores, num_engine_cores, core_command);
	kfree(cores);

	return rc;
}

int hl_cs_ioctl(struct hl_fpriv *hpriv, void *data)
{
	union hl_cs_args *args = data;
	enum hl_cs_type cs_type = 0;
	u64 cs_seq = ULONG_MAX;
	void __user *chunks;
	u32 num_chunks, flags, timeout,
		signals_count = 0, sob_addr = 0, handle_id = 0;
	u16 sob_initial_count = 0;
	int rc;

	rc = hl_cs_sanity_checks(hpriv, args);
	if (rc)
		goto out;

	rc = hl_cs_ctx_switch(hpriv, args, &cs_seq);
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
					&cs_seq, args->in.cs_flags, timeout,
					&sob_addr, &sob_initial_count);
		break;
	case CS_RESERVE_SIGNALS:
		rc = cs_ioctl_reserve_signals(hpriv,
					args->in.encaps_signals_q_idx,
					args->in.encaps_signals_count,
					&handle_id, &sob_addr, &signals_count);
		break;
	case CS_UNRESERVE_SIGNALS:
		rc = cs_ioctl_unreserve_signals(hpriv,
					args->in.encaps_sig_handle_id);
		break;
	case CS_TYPE_ENGINE_CORE:
		rc = cs_ioctl_engine_cores(hpriv, args->in.engine_cores,
				args->in.num_engine_cores, args->in.core_command);
		break;
	default:
		rc = cs_ioctl_default(hpriv, chunks, num_chunks, &cs_seq,
						args->in.cs_flags,
						args->in.encaps_sig_handle_id,
						timeout, &sob_initial_count);
		break;
	}
out:
	if (rc != -EAGAIN) {
		memset(args, 0, sizeof(*args));

		switch (cs_type) {
		case CS_RESERVE_SIGNALS:
			args->out.handle_id = handle_id;
			args->out.sob_base_addr_offset = sob_addr;
			args->out.count = signals_count;
			break;
		case CS_TYPE_SIGNAL:
			args->out.sob_base_addr_offset = sob_addr;
			args->out.sob_count_before_submission = sob_initial_count;
			args->out.seq = cs_seq;
			break;
		case CS_TYPE_DEFAULT:
			args->out.sob_count_before_submission = sob_initial_count;
			args->out.seq = cs_seq;
			break;
		default:
			args->out.seq = cs_seq;
			break;
		}

		args->out.status = rc;
	}

	return rc;
}

static int hl_wait_for_fence(struct hl_ctx *ctx, u64 seq, struct hl_fence *fence,
				enum hl_cs_wait_status *status, u64 timeout_us, s64 *timestamp)
{
	struct hl_device *hdev = ctx->hdev;
	ktime_t timestamp_kt;
	long completion_rc;
	int rc = 0, error;

	if (IS_ERR(fence)) {
		rc = PTR_ERR(fence);
		if (rc == -EINVAL)
			dev_notice_ratelimited(hdev->dev,
				"Can't wait on CS %llu because current CS is at seq %llu\n",
				seq, ctx->cs_sequence);
		return rc;
	}

	if (!fence) {
		if (!hl_pop_cs_outcome(&ctx->outcome_store, seq, &timestamp_kt, &error)) {
			dev_dbg(hdev->dev,
				"Can't wait on seq %llu because current CS is at seq %llu (Fence is gone)\n",
				seq, ctx->cs_sequence);
			*status = CS_WAIT_STATUS_GONE;
			return 0;
		}

		completion_rc = 1;
		goto report_results;
	}

	if (!timeout_us) {
		completion_rc = completion_done(&fence->completion);
	} else {
		unsigned long timeout;

		timeout = (timeout_us == MAX_SCHEDULE_TIMEOUT) ?
				timeout_us : usecs_to_jiffies(timeout_us);
		completion_rc =
			wait_for_completion_interruptible_timeout(
				&fence->completion, timeout);
	}

	error = fence->error;
	timestamp_kt = fence->timestamp;

report_results:
	if (completion_rc > 0) {
		*status = CS_WAIT_STATUS_COMPLETED;
		if (timestamp)
			*timestamp = ktime_to_ns(timestamp_kt);
	} else {
		*status = CS_WAIT_STATUS_BUSY;
	}

	if (error == -ETIMEDOUT || error == -EIO)
		rc = error;

	return rc;
}

/*
 * hl_cs_poll_fences - iterate CS fences to check for CS completion
 *
 * @mcs_data: multi-CS internal data
 * @mcs_compl: multi-CS completion structure
 *
 * @return 0 on success, otherwise non 0 error code
 *
 * The function iterates on all CS sequence in the list and set bit in
 * completion_bitmap for each completed CS.
 * While iterating, the function sets the stream map of each fence in the fence
 * array in the completion QID stream map to be used by CSs to perform
 * completion to the multi-CS context.
 * This function shall be called after taking context ref
 */
static int hl_cs_poll_fences(struct multi_cs_data *mcs_data, struct multi_cs_completion *mcs_compl)
{
	struct hl_fence **fence_ptr = mcs_data->fence_arr;
	struct hl_device *hdev = mcs_data->ctx->hdev;
	int i, rc, arr_len = mcs_data->arr_len;
	u64 *seq_arr = mcs_data->seq_arr;
	ktime_t max_ktime, first_cs_time;
	enum hl_cs_wait_status status;

	memset(fence_ptr, 0, arr_len * sizeof(struct hl_fence *));

	/* get all fences under the same lock */
	rc = hl_ctx_get_fences(mcs_data->ctx, seq_arr, fence_ptr, arr_len);
	if (rc)
		return rc;

	/*
	 * re-initialize the completion here to handle 2 possible cases:
	 * 1. CS will complete the multi-CS prior clearing the completion. in which
	 *    case the fence iteration is guaranteed to catch the CS completion.
	 * 2. the completion will occur after re-init of the completion.
	 *    in which case we will wake up immediately in wait_for_completion.
	 */
	reinit_completion(&mcs_compl->completion);

	/*
	 * set to maximum time to verify timestamp is valid: if at the end
	 * this value is maintained- no timestamp was updated
	 */
	max_ktime = ktime_set(KTIME_SEC_MAX, 0);
	first_cs_time = max_ktime;

	for (i = 0; i < arr_len; i++, fence_ptr++) {
		struct hl_fence *fence = *fence_ptr;

		/*
		 * In order to prevent case where we wait until timeout even though a CS associated
		 * with the multi-CS actually completed we do things in the below order:
		 * 1. for each fence set it's QID map in the multi-CS completion QID map. This way
		 *    any CS can, potentially, complete the multi CS for the specific QID (note
		 *    that once completion is initialized, calling complete* and then wait on the
		 *    completion will cause it to return at once)
		 * 2. only after allowing multi-CS completion for the specific QID we check whether
		 *    the specific CS already completed (and thus the wait for completion part will
		 *    be skipped). if the CS not completed it is guaranteed that completing CS will
		 *    wake up the completion.
		 */
		if (fence)
			mcs_compl->stream_master_qid_map |= fence->stream_master_qid_map;

		/*
		 * function won't sleep as it is called with timeout 0 (i.e.
		 * poll the fence)
		 */
		rc = hl_wait_for_fence(mcs_data->ctx, seq_arr[i], fence, &status, 0, NULL);
		if (rc) {
			dev_err(hdev->dev,
				"wait_for_fence error :%d for CS seq %llu\n",
								rc, seq_arr[i]);
			break;
		}

		switch (status) {
		case CS_WAIT_STATUS_BUSY:
			/* CS did not finished, QID to wait on already stored */
			break;
		case CS_WAIT_STATUS_COMPLETED:
			/*
			 * Using mcs_handling_done to avoid possibility of mcs_data
			 * returns to user indicating CS completed before it finished
			 * all of its mcs handling, to avoid race the next time the
			 * user waits for mcs.
			 * note: when reaching this case fence is definitely not NULL
			 *       but NULL check was added to overcome static analysis
			 */
			if (fence && !fence->mcs_handling_done) {
				/*
				 * in case multi CS is completed but MCS handling not done
				 * we "complete" the multi CS to prevent it from waiting
				 * until time-out and the "multi-CS handling done" will have
				 * another chance at the next iteration
				 */
				complete_all(&mcs_compl->completion);
				break;
			}

			mcs_data->completion_bitmap |= BIT(i);
			/*
			 * For all completed CSs we take the earliest timestamp.
			 * For this we have to validate that the timestamp is
			 * earliest of all timestamps so far.
			 */
			if (fence && mcs_data->update_ts &&
					(ktime_compare(fence->timestamp, first_cs_time) < 0))
				first_cs_time = fence->timestamp;
			break;
		case CS_WAIT_STATUS_GONE:
			mcs_data->update_ts = false;
			mcs_data->gone_cs = true;
			/*
			 * It is possible to get an old sequence numbers from user
			 * which related to already completed CSs and their fences
			 * already gone. In this case, CS set as completed but
			 * no need to consider its QID for mcs completion.
			 */
			mcs_data->completion_bitmap |= BIT(i);
			break;
		default:
			dev_err(hdev->dev, "Invalid fence status\n");
			return -EINVAL;
		}

	}

	hl_fences_put(mcs_data->fence_arr, arr_len);

	if (mcs_data->update_ts &&
			(ktime_compare(first_cs_time, max_ktime) != 0))
		mcs_data->timestamp = ktime_to_ns(first_cs_time);

	return rc;
}

static int _hl_cs_wait_ioctl(struct hl_device *hdev, struct hl_ctx *ctx, u64 timeout_us, u64 seq,
				enum hl_cs_wait_status *status, s64 *timestamp)
{
	struct hl_fence *fence;
	int rc = 0;

	if (timestamp)
		*timestamp = 0;

	hl_ctx_get(ctx);

	fence = hl_ctx_get_fence(ctx, seq);

	rc = hl_wait_for_fence(ctx, seq, fence, status, timeout_us, timestamp);
	hl_fence_put(fence);
	hl_ctx_put(ctx);

	return rc;
}

static inline unsigned long hl_usecs64_to_jiffies(const u64 usecs)
{
	if (usecs <= U32_MAX)
		return usecs_to_jiffies(usecs);

	/*
	 * If the value in nanoseconds is larger than 64 bit, use the largest
	 * 64 bit value.
	 */
	if (usecs >= ((u64)(U64_MAX / NSEC_PER_USEC)))
		return nsecs_to_jiffies(U64_MAX);

	return nsecs_to_jiffies(usecs * NSEC_PER_USEC);
}

/*
 * hl_wait_multi_cs_completion_init - init completion structure
 *
 * @hdev: pointer to habanalabs device structure
 * @stream_master_bitmap: stream master QIDs map, set bit indicates stream
 *                        master QID to wait on
 *
 * @return valid completion struct pointer on success, otherwise error pointer
 *
 * up to MULTI_CS_MAX_USER_CTX calls can be done concurrently to the driver.
 * the function gets the first available completion (by marking it "used")
 * and initialize its values.
 */
static struct multi_cs_completion *hl_wait_multi_cs_completion_init(struct hl_device *hdev)
{
	struct multi_cs_completion *mcs_compl;
	int i;

	/* find free multi_cs completion structure */
	for (i = 0; i < MULTI_CS_MAX_USER_CTX; i++) {
		mcs_compl = &hdev->multi_cs_completion[i];
		spin_lock(&mcs_compl->lock);
		if (!mcs_compl->used) {
			mcs_compl->used = 1;
			mcs_compl->timestamp = 0;
			/*
			 * init QID map to 0 to avoid completion by CSs. the actual QID map
			 * to multi-CS CSs will be set incrementally at a later stage
			 */
			mcs_compl->stream_master_qid_map = 0;
			spin_unlock(&mcs_compl->lock);
			break;
		}
		spin_unlock(&mcs_compl->lock);
	}

	if (i == MULTI_CS_MAX_USER_CTX) {
		dev_err(hdev->dev, "no available multi-CS completion structure\n");
		return ERR_PTR(-ENOMEM);
	}
	return mcs_compl;
}

/*
 * hl_wait_multi_cs_completion_fini - return completion structure and set as
 *                                    unused
 *
 * @mcs_compl: pointer to the completion structure
 */
static void hl_wait_multi_cs_completion_fini(
					struct multi_cs_completion *mcs_compl)
{
	/*
	 * free completion structure, do it under lock to be in-sync with the
	 * thread that signals completion
	 */
	spin_lock(&mcs_compl->lock);
	mcs_compl->used = 0;
	spin_unlock(&mcs_compl->lock);
}

/*
 * hl_wait_multi_cs_completion - wait for first CS to complete
 *
 * @mcs_data: multi-CS internal data
 *
 * @return 0 on success, otherwise non 0 error code
 */
static int hl_wait_multi_cs_completion(struct multi_cs_data *mcs_data,
						struct multi_cs_completion *mcs_compl)
{
	long completion_rc;

	completion_rc = wait_for_completion_interruptible_timeout(&mcs_compl->completion,
									mcs_data->timeout_jiffies);

	/* update timestamp */
	if (completion_rc > 0)
		mcs_data->timestamp = mcs_compl->timestamp;

	mcs_data->wait_status = completion_rc;

	return 0;
}

/*
 * hl_multi_cs_completion_init - init array of multi-CS completion structures
 *
 * @hdev: pointer to habanalabs device structure
 */
void hl_multi_cs_completion_init(struct hl_device *hdev)
{
	struct multi_cs_completion *mcs_cmpl;
	int i;

	for (i = 0; i < MULTI_CS_MAX_USER_CTX; i++) {
		mcs_cmpl = &hdev->multi_cs_completion[i];
		mcs_cmpl->used = 0;
		spin_lock_init(&mcs_cmpl->lock);
		init_completion(&mcs_cmpl->completion);
	}
}

/*
 * hl_multi_cs_wait_ioctl - implementation of the multi-CS wait ioctl
 *
 * @hpriv: pointer to the private data of the fd
 * @data: pointer to multi-CS wait ioctl in/out args
 *
 */
static int hl_multi_cs_wait_ioctl(struct hl_fpriv *hpriv, void *data)
{
	struct multi_cs_completion *mcs_compl;
	struct hl_device *hdev = hpriv->hdev;
	struct multi_cs_data mcs_data = {};
	union hl_wait_cs_args *args = data;
	struct hl_ctx *ctx = hpriv->ctx;
	struct hl_fence **fence_arr;
	void __user *seq_arr;
	u32 size_to_copy;
	u64 *cs_seq_arr;
	u8 seq_arr_len;
	int rc;

	if (!hdev->supports_wait_for_multi_cs) {
		dev_err(hdev->dev, "Wait for multi CS is not supported\n");
		return -EPERM;
	}

	seq_arr_len = args->in.seq_arr_len;

	if (seq_arr_len > HL_WAIT_MULTI_CS_LIST_MAX_LEN) {
		dev_err(hdev->dev, "Can wait only up to %d CSs, input sequence is of length %u\n",
				HL_WAIT_MULTI_CS_LIST_MAX_LEN, seq_arr_len);
		return -EINVAL;
	}

	/* allocate memory for sequence array */
	cs_seq_arr =
		kmalloc_array(seq_arr_len, sizeof(*cs_seq_arr), GFP_KERNEL);
	if (!cs_seq_arr)
		return -ENOMEM;

	/* copy CS sequence array from user */
	seq_arr = (void __user *) (uintptr_t) args->in.seq;
	size_to_copy = seq_arr_len * sizeof(*cs_seq_arr);
	if (copy_from_user(cs_seq_arr, seq_arr, size_to_copy)) {
		dev_err(hdev->dev, "Failed to copy multi-cs sequence array from user\n");
		rc = -EFAULT;
		goto free_seq_arr;
	}

	/* allocate array for the fences */
	fence_arr = kmalloc_array(seq_arr_len, sizeof(struct hl_fence *), GFP_KERNEL);
	if (!fence_arr) {
		rc = -ENOMEM;
		goto free_seq_arr;
	}

	/* initialize the multi-CS internal data */
	mcs_data.ctx = ctx;
	mcs_data.seq_arr = cs_seq_arr;
	mcs_data.fence_arr = fence_arr;
	mcs_data.arr_len = seq_arr_len;

	hl_ctx_get(ctx);

	/* wait (with timeout) for the first CS to be completed */
	mcs_data.timeout_jiffies = hl_usecs64_to_jiffies(args->in.timeout_us);
	mcs_compl = hl_wait_multi_cs_completion_init(hdev);
	if (IS_ERR(mcs_compl)) {
		rc = PTR_ERR(mcs_compl);
		goto put_ctx;
	}

	/* poll all CS fences, extract timestamp */
	mcs_data.update_ts = true;
	rc = hl_cs_poll_fences(&mcs_data, mcs_compl);
	/*
	 * skip wait for CS completion when one of the below is true:
	 * - an error on the poll function
	 * - one or more CS in the list completed
	 * - the user called ioctl with timeout 0
	 */
	if (rc || mcs_data.completion_bitmap || !args->in.timeout_us)
		goto completion_fini;

	while (true) {
		rc = hl_wait_multi_cs_completion(&mcs_data, mcs_compl);
		if (rc || (mcs_data.wait_status == 0))
			break;

		/*
		 * poll fences once again to update the CS map.
		 * no timestamp should be updated this time.
		 */
		mcs_data.update_ts = false;
		rc = hl_cs_poll_fences(&mcs_data, mcs_compl);

		if (rc || mcs_data.completion_bitmap)
			break;

		/*
		 * if hl_wait_multi_cs_completion returned before timeout (i.e.
		 * it got a completion) it either got completed by CS in the multi CS list
		 * (in which case the indication will be non empty completion_bitmap) or it
		 * got completed by CS submitted to one of the shared stream master but
		 * not in the multi CS list (in which case we should wait again but modify
		 * the timeout and set timestamp as zero to let a CS related to the current
		 * multi-CS set a new, relevant, timestamp)
		 */
		mcs_data.timeout_jiffies = mcs_data.wait_status;
		mcs_compl->timestamp = 0;
	}

completion_fini:
	hl_wait_multi_cs_completion_fini(mcs_compl);

put_ctx:
	hl_ctx_put(ctx);
	kfree(fence_arr);

free_seq_arr:
	kfree(cs_seq_arr);

	if (rc)
		return rc;

	if (mcs_data.wait_status == -ERESTARTSYS) {
		dev_err_ratelimited(hdev->dev,
				"user process got signal while waiting for Multi-CS\n");
		return -EINTR;
	}

	/* update output args */
	memset(args, 0, sizeof(*args));

	if (mcs_data.completion_bitmap) {
		args->out.status = HL_WAIT_CS_STATUS_COMPLETED;
		args->out.cs_completion_map = mcs_data.completion_bitmap;

		/* if timestamp not 0- it's valid */
		if (mcs_data.timestamp) {
			args->out.timestamp_nsec = mcs_data.timestamp;
			args->out.flags |= HL_WAIT_CS_STATUS_FLAG_TIMESTAMP_VLD;
		}

		/* update if some CS was gone */
		if (!mcs_data.timestamp)
			args->out.flags |= HL_WAIT_CS_STATUS_FLAG_GONE;
	} else {
		args->out.status = HL_WAIT_CS_STATUS_BUSY;
	}

	return 0;
}

static int hl_cs_wait_ioctl(struct hl_fpriv *hpriv, void *data)
{
	struct hl_device *hdev = hpriv->hdev;
	union hl_wait_cs_args *args = data;
	enum hl_cs_wait_status status;
	u64 seq = args->in.seq;
	s64 timestamp;
	int rc;

	rc = _hl_cs_wait_ioctl(hdev, hpriv->ctx, args->in.timeout_us, seq, &status, &timestamp);

	if (rc == -ERESTARTSYS) {
		dev_err_ratelimited(hdev->dev,
			"user process got signal while waiting for CS handle %llu\n",
			seq);
		return -EINTR;
	}

	memset(args, 0, sizeof(*args));

	if (rc) {
		if (rc == -ETIMEDOUT) {
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

static int ts_buff_get_kernel_ts_record(struct hl_mmap_mem_buf *buf,
					struct hl_cb *cq_cb,
					u64 ts_offset, u64 cq_offset, u64 target_value,
					spinlock_t *wait_list_lock,
					struct hl_user_pending_interrupt **pend)
{
	struct hl_ts_buff *ts_buff = buf->private;
	struct hl_user_pending_interrupt *requested_offset_record =
				(struct hl_user_pending_interrupt *)ts_buff->kernel_buff_address +
				ts_offset;
	struct hl_user_pending_interrupt *cb_last =
			(struct hl_user_pending_interrupt *)ts_buff->kernel_buff_address +
			(ts_buff->kernel_buff_size / sizeof(struct hl_user_pending_interrupt));
	unsigned long flags, iter_counter = 0;
	u64 current_cq_counter;

	/* Validate ts_offset not exceeding last max */
	if (requested_offset_record >= cb_last) {
		dev_err(buf->mmg->dev, "Ts offset exceeds max CB offset(0x%llx)\n",
								(u64)(uintptr_t)cb_last);
		return -EINVAL;
	}

start_over:
	spin_lock_irqsave(wait_list_lock, flags);

	/* Unregister only if we didn't reach the target value
	 * since in this case there will be no handling in irq context
	 * and then it's safe to delete the node out of the interrupt list
	 * then re-use it on other interrupt
	 */
	if (requested_offset_record->ts_reg_info.in_use) {
		current_cq_counter = *requested_offset_record->cq_kernel_addr;
		if (current_cq_counter < requested_offset_record->cq_target_value) {
			list_del(&requested_offset_record->wait_list_node);
			spin_unlock_irqrestore(wait_list_lock, flags);

			hl_mmap_mem_buf_put(requested_offset_record->ts_reg_info.buf);
			hl_cb_put(requested_offset_record->ts_reg_info.cq_cb);

			dev_dbg(buf->mmg->dev,
				"ts node removed from interrupt list now can re-use\n");
		} else {
			dev_dbg(buf->mmg->dev,
				"ts node in middle of irq handling\n");

			/* irq handling in the middle give it time to finish */
			spin_unlock_irqrestore(wait_list_lock, flags);
			usleep_range(1, 10);
			if (++iter_counter == MAX_TS_ITER_NUM) {
				dev_err(buf->mmg->dev,
					"handling registration interrupt took too long!!\n");
				return -EINVAL;
			}

			goto start_over;
		}
	} else {
		spin_unlock_irqrestore(wait_list_lock, flags);
	}

	/* Fill up the new registration node info */
	requested_offset_record->ts_reg_info.in_use = 1;
	requested_offset_record->ts_reg_info.buf = buf;
	requested_offset_record->ts_reg_info.cq_cb = cq_cb;
	requested_offset_record->ts_reg_info.timestamp_kernel_addr =
			(u64 *) ts_buff->user_buff_address + ts_offset;
	requested_offset_record->cq_kernel_addr =
			(u64 *) cq_cb->kernel_address + cq_offset;
	requested_offset_record->cq_target_value = target_value;

	*pend = requested_offset_record;

	dev_dbg(buf->mmg->dev, "Found available node in TS kernel CB %p\n",
		requested_offset_record);
	return 0;
}

static int _hl_interrupt_wait_ioctl(struct hl_device *hdev, struct hl_ctx *ctx,
				struct hl_mem_mgr *cb_mmg, struct hl_mem_mgr *mmg,
				u64 timeout_us, u64 cq_counters_handle,	u64 cq_counters_offset,
				u64 target_value, struct hl_user_interrupt *interrupt,
				bool register_ts_record, u64 ts_handle, u64 ts_offset,
				u32 *status, u64 *timestamp)
{
	struct hl_user_pending_interrupt *pend;
	struct hl_mmap_mem_buf *buf;
	struct hl_cb *cq_cb;
	unsigned long timeout, flags;
	long completion_rc;
	int rc = 0;

	timeout = hl_usecs64_to_jiffies(timeout_us);

	hl_ctx_get(ctx);

	cq_cb = hl_cb_get(cb_mmg, cq_counters_handle);
	if (!cq_cb) {
		rc = -EINVAL;
		goto put_ctx;
	}

	/* Validate the cq offset */
	if (((u64 *) cq_cb->kernel_address + cq_counters_offset) >=
			((u64 *) cq_cb->kernel_address + (cq_cb->size / sizeof(u64)))) {
		rc = -EINVAL;
		goto put_cq_cb;
	}

	if (register_ts_record) {
		dev_dbg(hdev->dev, "Timestamp registration: interrupt id: %u, ts offset: %llu, cq_offset: %llu\n",
					interrupt->interrupt_id, ts_offset, cq_counters_offset);
		buf = hl_mmap_mem_buf_get(mmg, ts_handle);
		if (!buf) {
			rc = -EINVAL;
			goto put_cq_cb;
		}

		/* Find first available record */
		rc = ts_buff_get_kernel_ts_record(buf, cq_cb, ts_offset,
						cq_counters_offset, target_value,
						&interrupt->wait_list_lock, &pend);
		if (rc)
			goto put_ts_buff;
	} else {
		pend = kzalloc(sizeof(*pend), GFP_KERNEL);
		if (!pend) {
			rc = -ENOMEM;
			goto put_cq_cb;
		}
		hl_fence_init(&pend->fence, ULONG_MAX);
		pend->cq_kernel_addr = (u64 *) cq_cb->kernel_address + cq_counters_offset;
		pend->cq_target_value = target_value;
	}

	spin_lock_irqsave(&interrupt->wait_list_lock, flags);

	/* We check for completion value as interrupt could have been received
	 * before we added the node to the wait list
	 */
	if (*pend->cq_kernel_addr >= target_value) {
		if (register_ts_record)
			pend->ts_reg_info.in_use = 0;
		spin_unlock_irqrestore(&interrupt->wait_list_lock, flags);

		*status = HL_WAIT_CS_STATUS_COMPLETED;

		if (register_ts_record) {
			*pend->ts_reg_info.timestamp_kernel_addr = ktime_get_ns();
			goto put_ts_buff;
		} else {
			pend->fence.timestamp = ktime_get();
			goto set_timestamp;
		}
	} else if (!timeout_us) {
		spin_unlock_irqrestore(&interrupt->wait_list_lock, flags);
		*status = HL_WAIT_CS_STATUS_BUSY;
		pend->fence.timestamp = ktime_get();
		goto set_timestamp;
	}

	/* Add pending user interrupt to relevant list for the interrupt
	 * handler to monitor.
	 * Note that we cannot have sorted list by target value,
	 * in order to shorten the list pass loop, since
	 * same list could have nodes for different cq counter handle.
	 */
	list_add_tail(&pend->wait_list_node, &interrupt->wait_list_head);
	spin_unlock_irqrestore(&interrupt->wait_list_lock, flags);

	if (register_ts_record) {
		rc = *status = HL_WAIT_CS_STATUS_COMPLETED;
		goto ts_registration_exit;
	}

	/* Wait for interrupt handler to signal completion */
	completion_rc = wait_for_completion_interruptible_timeout(&pend->fence.completion,
								timeout);
	if (completion_rc > 0) {
		*status = HL_WAIT_CS_STATUS_COMPLETED;
	} else {
		if (completion_rc == -ERESTARTSYS) {
			dev_err_ratelimited(hdev->dev,
					"user process got signal while waiting for interrupt ID %d\n",
					interrupt->interrupt_id);
			rc = -EINTR;
			*status = HL_WAIT_CS_STATUS_ABORTED;
		} else {
			if (pend->fence.error == -EIO) {
				dev_err_ratelimited(hdev->dev,
						"interrupt based wait ioctl aborted(error:%d) due to a reset cycle initiated\n",
						pend->fence.error);
				rc = -EIO;
				*status = HL_WAIT_CS_STATUS_ABORTED;
			} else {
				/* The wait has timed-out. We don't know anything beyond that
				 * because the workload wasn't submitted through the driver.
				 * Therefore, from driver's perspective, the workload is still
				 * executing.
				 */
				rc = 0;
				*status = HL_WAIT_CS_STATUS_BUSY;
			}
		}
	}

	/*
	 * We keep removing the node from list here, and not at the irq handler
	 * for completion timeout case. and if it's a registration
	 * for ts record, the node will be deleted in the irq handler after
	 * we reach the target value.
	 */
	spin_lock_irqsave(&interrupt->wait_list_lock, flags);
	list_del(&pend->wait_list_node);
	spin_unlock_irqrestore(&interrupt->wait_list_lock, flags);

set_timestamp:
	*timestamp = ktime_to_ns(pend->fence.timestamp);
	kfree(pend);
	hl_cb_put(cq_cb);
ts_registration_exit:
	hl_ctx_put(ctx);

	return rc;

put_ts_buff:
	hl_mmap_mem_buf_put(buf);
put_cq_cb:
	hl_cb_put(cq_cb);
put_ctx:
	hl_ctx_put(ctx);

	return rc;
}

static int _hl_interrupt_wait_ioctl_user_addr(struct hl_device *hdev, struct hl_ctx *ctx,
				u64 timeout_us, u64 user_address,
				u64 target_value, struct hl_user_interrupt *interrupt,
				u32 *status,
				u64 *timestamp)
{
	struct hl_user_pending_interrupt *pend;
	unsigned long timeout, flags;
	u64 completion_value;
	long completion_rc;
	int rc = 0;

	timeout = hl_usecs64_to_jiffies(timeout_us);

	hl_ctx_get(ctx);

	pend = kzalloc(sizeof(*pend), GFP_KERNEL);
	if (!pend) {
		hl_ctx_put(ctx);
		return -ENOMEM;
	}

	hl_fence_init(&pend->fence, ULONG_MAX);

	/* Add pending user interrupt to relevant list for the interrupt
	 * handler to monitor
	 */
	spin_lock_irqsave(&interrupt->wait_list_lock, flags);
	list_add_tail(&pend->wait_list_node, &interrupt->wait_list_head);
	spin_unlock_irqrestore(&interrupt->wait_list_lock, flags);

	/* We check for completion value as interrupt could have been received
	 * before we added the node to the wait list
	 */
	if (copy_from_user(&completion_value, u64_to_user_ptr(user_address), 8)) {
		dev_err(hdev->dev, "Failed to copy completion value from user\n");
		rc = -EFAULT;
		goto remove_pending_user_interrupt;
	}

	if (completion_value >= target_value) {
		*status = HL_WAIT_CS_STATUS_COMPLETED;
		/* There was no interrupt, we assume the completion is now. */
		pend->fence.timestamp = ktime_get();
	} else {
		*status = HL_WAIT_CS_STATUS_BUSY;
	}

	if (!timeout_us || (*status == HL_WAIT_CS_STATUS_COMPLETED))
		goto remove_pending_user_interrupt;

wait_again:
	/* Wait for interrupt handler to signal completion */
	completion_rc = wait_for_completion_interruptible_timeout(&pend->fence.completion,
										timeout);

	/* If timeout did not expire we need to perform the comparison.
	 * If comparison fails, keep waiting until timeout expires
	 */
	if (completion_rc > 0) {
		spin_lock_irqsave(&interrupt->wait_list_lock, flags);
		/* reinit_completion must be called before we check for user
		 * completion value, otherwise, if interrupt is received after
		 * the comparison and before the next wait_for_completion,
		 * we will reach timeout and fail
		 */
		reinit_completion(&pend->fence.completion);
		spin_unlock_irqrestore(&interrupt->wait_list_lock, flags);

		if (copy_from_user(&completion_value, u64_to_user_ptr(user_address), 8)) {
			dev_err(hdev->dev, "Failed to copy completion value from user\n");
			rc = -EFAULT;

			goto remove_pending_user_interrupt;
		}

		if (completion_value >= target_value) {
			*status = HL_WAIT_CS_STATUS_COMPLETED;
		} else if (pend->fence.error) {
			dev_err_ratelimited(hdev->dev,
				"interrupt based wait ioctl aborted(error:%d) due to a reset cycle initiated\n",
				pend->fence.error);
			/* set the command completion status as ABORTED */
			*status = HL_WAIT_CS_STATUS_ABORTED;
		} else {
			timeout = completion_rc;
			goto wait_again;
		}
	} else if (completion_rc == -ERESTARTSYS) {
		dev_err_ratelimited(hdev->dev,
			"user process got signal while waiting for interrupt ID %d\n",
			interrupt->interrupt_id);
		rc = -EINTR;
	} else {
		/* The wait has timed-out. We don't know anything beyond that
		 * because the workload wasn't submitted through the driver.
		 * Therefore, from driver's perspective, the workload is still
		 * executing.
		 */
		rc = 0;
		*status = HL_WAIT_CS_STATUS_BUSY;
	}

remove_pending_user_interrupt:
	spin_lock_irqsave(&interrupt->wait_list_lock, flags);
	list_del(&pend->wait_list_node);
	spin_unlock_irqrestore(&interrupt->wait_list_lock, flags);

	*timestamp = ktime_to_ns(pend->fence.timestamp);

	kfree(pend);
	hl_ctx_put(ctx);

	return rc;
}

static int hl_interrupt_wait_ioctl(struct hl_fpriv *hpriv, void *data)
{
	u16 interrupt_id, first_interrupt, last_interrupt;
	struct hl_device *hdev = hpriv->hdev;
	struct asic_fixed_properties *prop;
	struct hl_user_interrupt *interrupt;
	union hl_wait_cs_args *args = data;
	u32 status = HL_WAIT_CS_STATUS_BUSY;
	u64 timestamp = 0;
	int rc, int_idx;

	prop = &hdev->asic_prop;

	if (!(prop->user_interrupt_count + prop->user_dec_intr_count)) {
		dev_err(hdev->dev, "no user interrupts allowed");
		return -EPERM;
	}

	interrupt_id = FIELD_GET(HL_WAIT_CS_FLAGS_INTERRUPT_MASK, args->in.flags);

	first_interrupt = prop->first_available_user_interrupt;
	last_interrupt = prop->first_available_user_interrupt + prop->user_interrupt_count - 1;

	if (interrupt_id < prop->user_dec_intr_count) {

		/* Check if the requested core is enabled */
		if (!(prop->decoder_enabled_mask & BIT(interrupt_id))) {
			dev_err(hdev->dev, "interrupt on a disabled core(%u) not allowed",
				interrupt_id);
			return -EINVAL;
		}

		interrupt = &hdev->user_interrupt[interrupt_id];

	} else if (interrupt_id >= first_interrupt && interrupt_id <= last_interrupt) {

		int_idx = interrupt_id - first_interrupt + prop->user_dec_intr_count;
		interrupt = &hdev->user_interrupt[int_idx];

	} else if (interrupt_id == HL_COMMON_USER_CQ_INTERRUPT_ID) {
		interrupt = &hdev->common_user_cq_interrupt;
	} else if (interrupt_id == HL_COMMON_DEC_INTERRUPT_ID) {
		interrupt = &hdev->common_decoder_interrupt;
	} else {
		dev_err(hdev->dev, "invalid user interrupt %u", interrupt_id);
		return -EINVAL;
	}

	if (args->in.flags & HL_WAIT_CS_FLAGS_INTERRUPT_KERNEL_CQ)
		rc = _hl_interrupt_wait_ioctl(hdev, hpriv->ctx, &hpriv->mem_mgr, &hpriv->mem_mgr,
				args->in.interrupt_timeout_us, args->in.cq_counters_handle,
				args->in.cq_counters_offset,
				args->in.target, interrupt,
				!!(args->in.flags & HL_WAIT_CS_FLAGS_REGISTER_INTERRUPT),
				args->in.timestamp_handle, args->in.timestamp_offset,
				&status, &timestamp);
	else
		rc = _hl_interrupt_wait_ioctl_user_addr(hdev, hpriv->ctx,
				args->in.interrupt_timeout_us, args->in.addr,
				args->in.target, interrupt, &status,
				&timestamp);
	if (rc)
		return rc;

	memset(args, 0, sizeof(*args));
	args->out.status = status;

	if (timestamp) {
		args->out.timestamp_nsec = timestamp;
		args->out.flags |= HL_WAIT_CS_STATUS_FLAG_TIMESTAMP_VLD;
	}

	return 0;
}

int hl_wait_ioctl(struct hl_fpriv *hpriv, void *data)
{
	union hl_wait_cs_args *args = data;
	u32 flags = args->in.flags;
	int rc;

	/* If the device is not operational, no point in waiting for any command submission or
	 * user interrupt
	 */
	if (!hl_device_operational(hpriv->hdev, NULL))
		return -EBUSY;

	if (flags & HL_WAIT_CS_FLAGS_INTERRUPT)
		rc = hl_interrupt_wait_ioctl(hpriv, data);
	else if (flags & HL_WAIT_CS_FLAGS_MULTI_CS)
		rc = hl_multi_cs_wait_ioctl(hpriv, data);
	else
		rc = hl_cs_wait_ioctl(hpriv, data);

	return rc;
}
