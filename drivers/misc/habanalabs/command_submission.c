// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include <uapi/misc/habanalabs.h>
#include "habanalabs.h"

#include <linux/uaccess.h>
#include <linux/slab.h>

static void job_wq_completion(struct work_struct *work);
static long _hl_cs_wait_ioctl(struct hl_device *hdev,
		struct hl_ctx *ctx, u64 timeout_us, u64 seq);
static void cs_do_release(struct kref *ref);

static const char *hl_fence_get_driver_name(struct dma_fence *fence)
{
	return "HabanaLabs";
}

static const char *hl_fence_get_timeline_name(struct dma_fence *fence)
{
	struct hl_dma_fence *hl_fence =
		container_of(fence, struct hl_dma_fence, base_fence);

	return dev_name(hl_fence->hdev->dev);
}

static bool hl_fence_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static void hl_fence_release(struct dma_fence *fence)
{
	struct hl_dma_fence *hl_fence =
		container_of(fence, struct hl_dma_fence, base_fence);

	kfree_rcu(hl_fence, base_fence.rcu);
}

static const struct dma_fence_ops hl_fence_ops = {
	.get_driver_name = hl_fence_get_driver_name,
	.get_timeline_name = hl_fence_get_timeline_name,
	.enable_signaling = hl_fence_enable_signaling,
	.wait = dma_fence_default_wait,
	.release = hl_fence_release
};

static void cs_get(struct hl_cs *cs)
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

	rc = hdev->asic_funcs->cs_parser(hdev, &parser);

	if (is_cb_patched(hdev, job)) {
		if (!rc) {
			job->patched_cb = parser.patched_cb;
			job->job_cb_size = parser.patched_cb_size;

			spin_lock(&job->patched_cb->lock);
			job->patched_cb->cs_cnt++;
			spin_unlock(&job->patched_cb->lock);
		}

		/*
		 * Whether the parsing worked or not, we don't need the
		 * original CB anymore because it was already parsed and
		 * won't be accessed again for this CS
		 */
		spin_lock(&job->user_cb->lock);
		job->user_cb->cs_cnt--;
		spin_unlock(&job->user_cb->lock);
		hl_cb_put(job->user_cb);
		job->user_cb = NULL;
	}

	return rc;
}

static void free_job(struct hl_device *hdev, struct hl_cs_job *job)
{
	struct hl_cs *cs = job->cs;

	if (is_cb_patched(hdev, job)) {
		hl_userptr_delete_list(hdev, &job->userptr_list);

		/*
		 * We might arrive here from rollback and patched CB wasn't
		 * created, so we need to check it's not NULL
		 */
		if (job->patched_cb) {
			spin_lock(&job->patched_cb->lock);
			job->patched_cb->cs_cnt--;
			spin_unlock(&job->patched_cb->lock);

			hl_cb_put(job->patched_cb);
		}
	}

	/* For H/W queue jobs, if a user CB was allocated by driver and MMU is
	 * enabled, the user CB isn't released in cs_parser() and thus should be
	 * released here.
	 */
	if (job->queue_type == QUEUE_TYPE_HW &&
			job->is_kernel_allocated_cb && hdev->mmu_enable) {
		spin_lock(&job->user_cb->lock);
		job->user_cb->cs_cnt--;
		spin_unlock(&job->user_cb->lock);

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

	if (job->queue_type == QUEUE_TYPE_EXT ||
			job->queue_type == QUEUE_TYPE_HW)
		cs_put(cs);

	kfree(job);
}

static void cs_do_release(struct kref *ref)
{
	struct hl_cs *cs = container_of(ref, struct hl_cs,
						refcount);
	struct hl_device *hdev = cs->ctx->hdev;
	struct hl_cs_job *job, *tmp;

	cs->completed = true;

	/*
	 * Although if we reached here it means that all external jobs have
	 * finished, because each one of them took refcnt to CS, we still
	 * need to go over the internal jobs and free them. Otherwise, we
	 * will have leaked memory and what's worse, the CS object (and
	 * potentially the CTX object) could be released, while the JOB
	 * still holds a pointer to them (but no reference).
	 */
	list_for_each_entry_safe(job, tmp, &cs->job_list, cs_node)
		free_job(hdev, job);

	/* We also need to update CI for internal queues */
	if (cs->submitted) {
		hdev->asic_funcs->hw_queues_lock(hdev);

		hdev->cs_active_cnt--;
		if (!hdev->cs_active_cnt) {
			struct hl_device_idle_busy_ts *ts;

			ts = &hdev->idle_busy_ts_arr[hdev->idle_busy_ts_idx++];
			ts->busy_to_idle_ts = ktime_get();

			if (hdev->idle_busy_ts_idx == HL_IDLE_BUSY_TS_ARR_SIZE)
				hdev->idle_busy_ts_idx = 0;
		} else if (hdev->cs_active_cnt < 0) {
			dev_crit(hdev->dev, "CS active cnt %d is negative\n",
				hdev->cs_active_cnt);
		}

		hdev->asic_funcs->hw_queues_unlock(hdev);

		hl_int_hw_queue_update_ci(cs);

		spin_lock(&hdev->hw_queues_mirror_lock);
		/* remove CS from hw_queues mirror list */
		list_del_init(&cs->mirror_node);
		spin_unlock(&hdev->hw_queues_mirror_lock);

		/*
		 * Don't cancel TDR in case this CS was timedout because we
		 * might be running from the TDR context
		 */
		if ((!cs->timedout) &&
			(hdev->timeout_jiffies != MAX_SCHEDULE_TIMEOUT)) {
			struct hl_cs *next;

			if (cs->tdr_active)
				cancel_delayed_work_sync(&cs->work_tdr);

			spin_lock(&hdev->hw_queues_mirror_lock);

			/* queue TDR for next CS */
			next = list_first_entry_or_null(
					&hdev->hw_queues_mirror_list,
					struct hl_cs, mirror_node);

			if ((next) && (!next->tdr_active)) {
				next->tdr_active = true;
				schedule_delayed_work(&next->work_tdr,
							hdev->timeout_jiffies);
			}

			spin_unlock(&hdev->hw_queues_mirror_lock);
		}
	}

	/*
	 * Must be called before hl_ctx_put because inside we use ctx to get
	 * the device
	 */
	hl_debugfs_remove_cs(cs);

	hl_ctx_put(cs->ctx);

	if (cs->timedout)
		dma_fence_set_error(cs->fence, -ETIMEDOUT);
	else if (cs->aborted)
		dma_fence_set_error(cs->fence, -EIO);

	dma_fence_signal(cs->fence);
	dma_fence_put(cs->fence);

	kfree(cs);
}

static void cs_timedout(struct work_struct *work)
{
	struct hl_device *hdev;
	int ctx_asid, rc;
	struct hl_cs *cs = container_of(work, struct hl_cs,
						 work_tdr.work);
	rc = cs_get_unless_zero(cs);
	if (!rc)
		return;

	if ((!cs->submitted) || (cs->completed)) {
		cs_put(cs);
		return;
	}

	/* Mark the CS is timed out so we won't try to cancel its TDR */
	cs->timedout = true;

	hdev = cs->ctx->hdev;
	ctx_asid = cs->ctx->asid;

	/* TODO: add information about last signaled seq and last emitted seq */
	dev_err(hdev->dev, "User %d command submission %llu got stuck!\n",
		ctx_asid, cs->sequence);

	cs_put(cs);

	if (hdev->reset_on_lockup)
		hl_device_reset(hdev, false, false);
}

static int allocate_cs(struct hl_device *hdev, struct hl_ctx *ctx,
			struct hl_cs **cs_new)
{
	struct hl_dma_fence *fence;
	struct dma_fence *other = NULL;
	struct hl_cs *cs;
	int rc;

	cs = kzalloc(sizeof(*cs), GFP_ATOMIC);
	if (!cs)
		return -ENOMEM;

	cs->ctx = ctx;
	cs->submitted = false;
	cs->completed = false;
	INIT_LIST_HEAD(&cs->job_list);
	INIT_DELAYED_WORK(&cs->work_tdr, cs_timedout);
	kref_init(&cs->refcount);
	spin_lock_init(&cs->job_lock);

	fence = kmalloc(sizeof(*fence), GFP_ATOMIC);
	if (!fence) {
		rc = -ENOMEM;
		goto free_cs;
	}

	fence->hdev = hdev;
	spin_lock_init(&fence->lock);
	cs->fence = &fence->base_fence;

	spin_lock(&ctx->cs_lock);

	fence->cs_seq = ctx->cs_sequence;
	other = ctx->cs_pending[fence->cs_seq & (HL_MAX_PENDING_CS - 1)];
	if ((other) && (!dma_fence_is_signaled(other))) {
		spin_unlock(&ctx->cs_lock);
		dev_dbg(hdev->dev,
			"Rejecting CS because of too many in-flights CS\n");
		rc = -EAGAIN;
		goto free_fence;
	}

	dma_fence_init(&fence->base_fence, &hl_fence_ops, &fence->lock,
			ctx->asid, ctx->cs_sequence);

	cs->sequence = fence->cs_seq;

	ctx->cs_pending[fence->cs_seq & (HL_MAX_PENDING_CS - 1)] =
							&fence->base_fence;
	ctx->cs_sequence++;

	dma_fence_get(&fence->base_fence);

	dma_fence_put(other);

	spin_unlock(&ctx->cs_lock);

	*cs_new = cs;

	return 0;

free_fence:
	kfree(fence);
free_cs:
	kfree(cs);
	return rc;
}

static void cs_rollback(struct hl_device *hdev, struct hl_cs *cs)
{
	struct hl_cs_job *job, *tmp;

	list_for_each_entry_safe(job, tmp, &cs->job_list, cs_node)
		free_job(hdev, job);
}

void hl_cs_rollback_all(struct hl_device *hdev)
{
	struct hl_cs *cs, *tmp;

	/* flush all completions */
	flush_workqueue(hdev->cq_wq);

	/* Make sure we don't have leftovers in the H/W queues mirror list */
	list_for_each_entry_safe(cs, tmp, &hdev->hw_queues_mirror_list,
				mirror_node) {
		cs_get(cs);
		cs->aborted = true;
		dev_warn_ratelimited(hdev->dev, "Killing CS %d.%llu\n",
					cs->ctx->asid, cs->sequence);
		cs_rollback(hdev, cs);
		cs_put(cs);
	}
}

static void job_wq_completion(struct work_struct *work)
{
	struct hl_cs_job *job = container_of(work, struct hl_cs_job,
						finish_work);
	struct hl_cs *cs = job->cs;
	struct hl_device *hdev = cs->ctx->hdev;

	/* job is no longer needed */
	free_job(hdev, job);
}

static int validate_queue_index(struct hl_device *hdev,
				struct hl_cs_chunk *chunk,
				enum hl_queue_type *queue_type,
				bool *is_kernel_allocated_cb)
{
	struct asic_fixed_properties *asic = &hdev->asic_prop;
	struct hw_queue_properties *hw_queue_prop;

	hw_queue_prop = &asic->hw_queues_props[chunk->queue_index];

	if ((chunk->queue_index >= HL_MAX_QUEUES) ||
			(hw_queue_prop->type == QUEUE_TYPE_NA)) {
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

	*queue_type = hw_queue_prop->type;
	*is_kernel_allocated_cb = !!hw_queue_prop->requires_kernel_cb;

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

	spin_lock(&cb->lock);
	cb->cs_cnt++;
	spin_unlock(&cb->lock);

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
		return NULL;

	job->queue_type = queue_type;
	job->is_kernel_allocated_cb = is_kernel_allocated_cb;

	if (is_cb_patched(hdev, job))
		INIT_LIST_HEAD(&job->userptr_list);

	if (job->queue_type == QUEUE_TYPE_EXT)
		INIT_WORK(&job->finish_work, job_wq_completion);

	return job;
}

static int _hl_cs_ioctl(struct hl_fpriv *hpriv, void __user *chunks,
			u32 num_chunks, u64 *cs_seq)
{
	struct hl_device *hdev = hpriv->hdev;
	struct hl_cs_chunk *cs_chunk_array;
	struct hl_cs_job *job;
	struct hl_cs *cs;
	struct hl_cb *cb;
	bool int_queues_only = true;
	u32 size_to_copy;
	int rc, i, parse_cnt;

	*cs_seq = ULLONG_MAX;

	if (num_chunks > HL_MAX_JOBS_PER_CS) {
		dev_err(hdev->dev,
			"Number of chunks can NOT be larger than %d\n",
			HL_MAX_JOBS_PER_CS);
		rc = -EINVAL;
		goto out;
	}

	cs_chunk_array = kmalloc_array(num_chunks, sizeof(*cs_chunk_array),
					GFP_ATOMIC);
	if (!cs_chunk_array) {
		rc = -ENOMEM;
		goto out;
	}

	size_to_copy = num_chunks * sizeof(struct hl_cs_chunk);
	if (copy_from_user(cs_chunk_array, chunks, size_to_copy)) {
		dev_err(hdev->dev, "Failed to copy cs chunk array from user\n");
		rc = -EFAULT;
		goto free_cs_chunk_array;
	}

	/* increment refcnt for context */
	hl_ctx_get(hdev, hpriv->ctx);

	rc = allocate_cs(hdev, hpriv->ctx, &cs);
	if (rc) {
		hl_ctx_put(hpriv->ctx);
		goto free_cs_chunk_array;
	}

	*cs_seq = cs->sequence;

	hl_debugfs_add_cs(cs);

	/* Validate ALL the CS chunks before submitting the CS */
	for (i = 0, parse_cnt = 0 ; i < num_chunks ; i++, parse_cnt++) {
		struct hl_cs_chunk *chunk = &cs_chunk_array[i];
		enum hl_queue_type queue_type;
		bool is_kernel_allocated_cb;

		rc = validate_queue_index(hdev, chunk, &queue_type,
						&is_kernel_allocated_cb);
		if (rc)
			goto free_cs_object;

		if (is_kernel_allocated_cb) {
			cb = get_cb_from_cs_chunk(hdev, &hpriv->cb_mgr, chunk);
			if (!cb) {
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
			dev_err(hdev->dev, "Failed to allocate a new job\n");
			rc = -ENOMEM;
			if (is_kernel_allocated_cb)
				goto release_cb;
			else
				goto free_cs_object;
		}

		job->id = i + 1;
		job->cs = cs;
		job->user_cb = cb;
		job->user_cb_size = chunk->cb_size;
		if (is_kernel_allocated_cb)
			job->job_cb_size = cb->size;
		else
			job->job_cb_size = chunk->cb_size;
		job->hw_queue_id = chunk->queue_index;

		cs->jobs_in_queue_cnt[job->hw_queue_id]++;

		list_add_tail(&job->cs_node, &cs->job_list);

		/*
		 * Increment CS reference. When CS reference is 0, CS is
		 * done and can be signaled to user and free all its resources
		 * Only increment for JOB on external or H/W queues, because
		 * only for those JOBs we get completion
		 */
		if (job->queue_type == QUEUE_TYPE_EXT ||
				job->queue_type == QUEUE_TYPE_HW)
			cs_get(cs);

		hl_debugfs_add_job(hdev, job);

		rc = cs_parser(hpriv, job);
		if (rc) {
			dev_err(hdev->dev,
				"Failed to parse JOB %d.%llu.%d, err %d, rejecting the CS\n",
				cs->ctx->asid, cs->sequence, job->id, rc);
			goto free_cs_object;
		}
	}

	if (int_queues_only) {
		dev_err(hdev->dev,
			"Reject CS %d.%llu because only internal queues jobs are present\n",
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
	spin_lock(&cb->lock);
	cb->cs_cnt--;
	spin_unlock(&cb->lock);
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

int hl_cs_ioctl(struct hl_fpriv *hpriv, void *data)
{
	struct hl_device *hdev = hpriv->hdev;
	union hl_cs_args *args = data;
	struct hl_ctx *ctx = hpriv->ctx;
	void __user *chunks;
	u32 num_chunks;
	u64 cs_seq = ULONG_MAX;
	int rc, do_ctx_switch;
	bool need_soft_reset = false;

	if (hl_device_disabled_or_in_reset(hdev)) {
		dev_warn_ratelimited(hdev->dev,
			"Device is %s. Can't submit new CS\n",
			atomic_read(&hdev->in_reset) ? "in_reset" : "disabled");
		rc = -EBUSY;
		goto out;
	}

	do_ctx_switch = atomic_cmpxchg(&ctx->thread_ctx_switch_token, 1, 0);

	if (do_ctx_switch || (args->in.cs_flags & HL_CS_FLAGS_FORCE_RESTORE)) {
		long ret;

		chunks = (void __user *)(uintptr_t)args->in.chunks_restore;
		num_chunks = args->in.num_chunks_restore;

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

		if (num_chunks == 0) {
			dev_dbg(hdev->dev,
			"Need to run restore phase but restore CS is empty\n");
			rc = 0;
		} else {
			rc = _hl_cs_ioctl(hpriv, chunks, num_chunks,
						&cs_seq);
		}

		mutex_unlock(&hpriv->restore_phase_mutex);

		if (rc) {
			dev_err(hdev->dev,
				"Failed to submit restore CS for context %d (%d)\n",
				ctx->asid, rc);
			goto out;
		}

		/* Need to wait for restore completion before execution phase */
		if (num_chunks > 0) {
			ret = _hl_cs_wait_ioctl(hdev, ctx,
					jiffies_to_usecs(hdev->timeout_jiffies),
					cs_seq);
			if (ret <= 0) {
				dev_err(hdev->dev,
					"Restore CS for context %d failed to complete %ld\n",
					ctx->asid, ret);
				rc = -ENOEXEC;
				goto out;
			}
		}

		ctx->thread_ctx_switch_wait_token = 1;
	} else if (!ctx->thread_ctx_switch_wait_token) {
		u32 tmp;

		rc = hl_poll_timeout_memory(hdev,
			&ctx->thread_ctx_switch_wait_token, tmp, (tmp == 1),
			100, jiffies_to_usecs(hdev->timeout_jiffies), false);

		if (rc == -ETIMEDOUT) {
			dev_err(hdev->dev,
				"context switch phase timeout (%d)\n", tmp);
			goto out;
		}
	}

	chunks = (void __user *)(uintptr_t)args->in.chunks_execute;
	num_chunks = args->in.num_chunks_execute;

	if (num_chunks == 0) {
		dev_err(hdev->dev,
			"Got execute CS with 0 chunks, context %d\n",
			ctx->asid);
		rc = -EINVAL;
		goto out;
	}

	rc = _hl_cs_ioctl(hpriv, chunks, num_chunks, &cs_seq);

out:
	if (rc != -EAGAIN) {
		memset(args, 0, sizeof(*args));
		args->out.status = rc;
		args->out.seq = cs_seq;
	}

	if (((rc == -ETIMEDOUT) || (rc == -EBUSY)) && (need_soft_reset))
		hl_device_reset(hdev, false, false);

	return rc;
}

static long _hl_cs_wait_ioctl(struct hl_device *hdev,
		struct hl_ctx *ctx, u64 timeout_us, u64 seq)
{
	struct dma_fence *fence;
	unsigned long timeout;
	long rc;

	if (timeout_us == MAX_SCHEDULE_TIMEOUT)
		timeout = timeout_us;
	else
		timeout = usecs_to_jiffies(timeout_us);

	hl_ctx_get(hdev, ctx);

	fence = hl_ctx_get_fence(ctx, seq);
	if (IS_ERR(fence)) {
		rc = PTR_ERR(fence);
	} else if (fence) {
		rc = dma_fence_wait_timeout(fence, true, timeout);
		if (fence->error == -ETIMEDOUT)
			rc = -ETIMEDOUT;
		else if (fence->error == -EIO)
			rc = -EIO;
		dma_fence_put(fence);
	} else
		rc = 1;

	hl_ctx_put(ctx);

	return rc;
}

int hl_cs_wait_ioctl(struct hl_fpriv *hpriv, void *data)
{
	struct hl_device *hdev = hpriv->hdev;
	union hl_wait_cs_args *args = data;
	u64 seq = args->in.seq;
	long rc;

	rc = _hl_cs_wait_ioctl(hdev, hpriv->ctx, args->in.timeout_us, seq);

	memset(args, 0, sizeof(*args));

	if (rc < 0) {
		dev_err_ratelimited(hdev->dev,
				"Error %ld on waiting for CS handle %llu\n",
				rc, seq);
		if (rc == -ERESTARTSYS) {
			args->out.status = HL_WAIT_CS_STATUS_INTERRUPTED;
			rc = -EINTR;
		} else if (rc == -ETIMEDOUT) {
			args->out.status = HL_WAIT_CS_STATUS_TIMEDOUT;
		} else if (rc == -EIO) {
			args->out.status = HL_WAIT_CS_STATUS_ABORTED;
		}
		return rc;
	}

	if (rc == 0)
		args->out.status = HL_WAIT_CS_STATUS_BUSY;
	else
		args->out.status = HL_WAIT_CS_STATUS_COMPLETED;

	return 0;
}
