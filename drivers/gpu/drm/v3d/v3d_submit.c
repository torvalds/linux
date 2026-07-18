// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2014-2018 Broadcom
 * Copyright (C) 2023 Raspberry Pi
 */

#include <linux/dma-fence-unwrap.h>
#include <linux/overflow.h>

#include <drm/drm_print.h>
#include <drm/drm_syncobj.h>

#include "v3d_drv.h"
#include "v3d_regs.h"
#include "v3d_trace.h"

/* Takes the reservation lock on all the BOs being referenced, so that
 * we can attach fences and update the reservations after pushing the job
 * to the queue.
 *
 * We don't lock the RCL the tile alloc/state BOs, or overflow memory
 * (all of which are on render->unref_list). They're entirely private
 * to v3d, so we don't attach dma-buf fences to them.
 */
static int
v3d_submit_lock_reservations(struct v3d_submit *submit)
{
	int i, j, ret;

	drm_exec_init(&submit->exec,
		      DRM_EXEC_INTERRUPTIBLE_WAIT | DRM_EXEC_IGNORE_DUPLICATES, 0);
	drm_exec_until_all_locked(&submit->exec) {
		for (i = 0; i < submit->job_count; i++) {
			struct v3d_job *job = submit->jobs[i];

			ret = drm_exec_prepare_array(&submit->exec, job->bo,
						     job->bo_count, 1);
			if (ret)
				break;
		}
		drm_exec_retry_on_contention(&submit->exec);
		if (ret)
			goto fail;
	}

	for (i = 0; i < submit->job_count; i++) {
		struct v3d_job *job = submit->jobs[i];

		for (j = 0; j < job->bo_count; j++) {
			ret = drm_sched_job_add_implicit_dependencies(&job->base,
								      job->bo[j],
								      true);
			if (ret)
				goto fail;
		}
	}

	return 0;

fail:
	drm_exec_fini(&submit->exec);
	return ret;
}

static void
v3d_submit_unlock_reservations(struct v3d_submit *submit)
{
	drm_exec_fini(&submit->exec);
}

/**
 * v3d_lookup_bos() - Sets up job->bo[] with the GEM objects
 * referenced by the job.
 * @dev: DRM device
 * @file_priv: DRM file for this fd
 * @job: V3D job being set up
 * @bo_handles: GEM handles
 * @bo_count: Number of GEM handles passed in
 *
 * The command validator needs to reference BOs by their index within
 * the submitted job's BO list. This does the validation of the job's
 * BO list and reference counting for the lifetime of the job.
 *
 * Note that this function doesn't need to unreference the BOs on
 * failure, because that will happen at `v3d_job_free()`.
 */
static int
v3d_lookup_bos(struct v3d_submit *submit, u64 bo_handles, u32 bo_count)
{
	struct v3d_job *last_job = submit->jobs[submit->job_count - 1];

	last_job->bo_count = bo_count;

	if (!last_job->bo_count) {
		/* See comment on bo_index for why we have to check
		 * this.
		 */
		drm_warn(&submit->v3d->drm, "Rendering requires BOs\n");
		return -EINVAL;
	}

	return drm_gem_objects_lookup(submit->file_priv,
				      (void __user *)(uintptr_t)bo_handles,
				      last_job->bo_count, &last_job->bo);
}

static void
v3d_job_free(struct kref *ref)
{
	struct v3d_job *job = container_of(ref, struct v3d_job, refcount);
	int i;

	if (job->bo) {
		for (i = 0; i < job->bo_count; i++)
			drm_gem_object_put(job->bo[i]);
		kvfree(job->bo);
	}

	dma_fence_put(job->irq_fence);
	dma_fence_put(job->done_fence);

	if (job->perfmon)
		v3d_perfmon_put(job->perfmon);

	v3d_stats_put(job->client_stats);
	v3d_stats_put(job->global_stats);

	if (job->has_pm_ref)
		v3d_pm_runtime_put(job->v3d);

	kfree(job);
}

static void
v3d_render_job_free(struct kref *ref)
{
	struct v3d_render_job *job = container_of(ref, struct v3d_render_job,
						  base.refcount);
	struct v3d_bo *bo, *save;

	list_for_each_entry_safe(bo, save, &job->unref_list, unref_head) {
		drm_gem_object_put(&bo->base.base);
	}

	v3d_job_free(ref);
}

static void
v3d_cpu_job_free(struct kref *ref)
{
	struct v3d_cpu_job *job = container_of(ref, struct v3d_cpu_job,
					       base.refcount);

	v3d_timestamp_query_info_free(&job->timestamp_query,
				      job->timestamp_query.count);

	v3d_performance_query_info_free(&job->performance_query,
					job->performance_query.count);

	if (job->indirect_csd.indirect)
		drm_gem_object_put(job->indirect_csd.indirect);

	v3d_job_free(ref);
}

void v3d_job_cleanup(struct v3d_job *job)
{
	if (!job)
		return;

	drm_sched_job_cleanup(&job->base);
	v3d_job_put(job);
}

void v3d_job_put(struct v3d_job *job)
{
	if (!job)
		return;

	kref_put(&job->refcount, job->free);
}

static int
v3d_job_add_syncobjs(struct v3d_job *job, struct drm_file *file_priv,
		     u32 in_sync, struct v3d_submit_ext *se)
{
	bool has_multisync = se && (se->flags & DRM_V3D_EXT_ID_MULTI_SYNC);
	struct v3d_dev *v3d = job->v3d;
	int ret = 0;

	if (!has_multisync) {
		/* Ignore syncobj if its handle is zero */
		if (in_sync)
			ret = drm_sched_job_add_syncobj_dependency(&job->base, file_priv,
								   in_sync, 0);
		return ret;
	}

	if (se->in_sync_count && se->wait_stage == job->queue) {
		struct drm_v3d_sem __user *handle = u64_to_user_ptr(se->in_syncs);

		for (int i = 0; i < se->in_sync_count; i++) {
			struct drm_v3d_sem in;

			if (copy_from_user(&in, handle++, sizeof(in))) {
				drm_dbg(&v3d->drm, "Failed to copy wait dep handle.\n");
				return -EFAULT;
			}

			/* Ignore syncobj if its handle is zero */
			if (in.handle) {
				ret = drm_sched_job_add_syncobj_dependency(&job->base,
									   file_priv, in.handle, 0);
				if (ret)
					return ret;
			}
		}
	}

	return 0;
}

static const struct {
	size_t size;
	void (*free)(struct kref *ref);
} v3d_job_types[] = {
	[V3D_BIN]		= { sizeof(struct v3d_bin_job), v3d_job_free },
	[V3D_RENDER]		= { sizeof(struct v3d_render_job), v3d_render_job_free },
	[V3D_TFU]		= { sizeof(struct v3d_tfu_job), v3d_job_free },
	[V3D_CSD]		= { sizeof(struct v3d_csd_job), v3d_job_free },
	[V3D_CACHE_CLEAN]	= { sizeof(struct v3d_job), v3d_job_free },
	[V3D_CPU]		= { sizeof(struct v3d_cpu_job), v3d_cpu_job_free },
};

static struct v3d_job *
v3d_submit_add_job(struct v3d_submit *submit, enum v3d_queue queue)
{
	struct v3d_file_priv *v3d_priv = submit->file_priv->driver_priv;
	struct v3d_dev *v3d = submit->v3d;
	struct v3d_job *job;
	int ret;

	if (queue >= V3D_MAX_QUEUES)
		return ERR_PTR(-EINVAL);

	job = kzalloc(v3d_job_types[queue].size, GFP_KERNEL);
	if (!job)
		return ERR_PTR(-ENOMEM);

	job->v3d = v3d;
	job->queue = queue;
	job->file_priv = v3d_priv;
	job->free = v3d_job_types[queue].free;

	ret = drm_sched_job_init(&job->base, &v3d_priv->sched_entity[queue],
				 1, v3d_priv, submit->file_priv->client_id);
	if (ret)
		goto fail_free;

	/* CPU jobs don't require hardware resources */
	if (queue != V3D_CPU) {
		ret = v3d_pm_runtime_get(v3d);
		if (ret)
			goto fail_sched_job;
		job->has_pm_ref = true;
	}

	kref_init(&job->refcount);

	job->client_stats = v3d_stats_get(v3d_priv->stats[queue]);
	job->global_stats = v3d_stats_get(v3d->queue[queue].stats);

	submit->jobs[submit->job_count++] = job;

	return job;

fail_sched_job:
	drm_sched_job_cleanup(&job->base);
fail_free:
	kfree(job);
	return ERR_PTR(ret);
}

static void
v3d_submit_put_jobs(struct v3d_submit *submit)
{
	for (int i = 0; i < submit->job_count; i++)
		v3d_job_put(submit->jobs[i]);
}

static void
v3d_submit_cleanup_jobs(struct v3d_submit *submit)
{
	for (int i = 0; i < submit->job_count; i++)
		v3d_job_cleanup(submit->jobs[i]);
}

static int
v3d_attach_perfmon_to_jobs(struct v3d_submit *submit, u32 perfmon_id)
{
	struct v3d_file_priv *v3d_priv = submit->file_priv->driver_priv;
	struct v3d_dev *v3d = submit->v3d;
	struct v3d_perfmon *perfmon;

	if (!perfmon_id)
		return 0;

	scoped_guard(spinlock_irqsave, &v3d->perfmon_state.lock) {
		if (v3d->global_perfmon)
			return -EAGAIN;
	}

	perfmon = v3d_perfmon_find(v3d_priv, perfmon_id);
	if (!perfmon)
		return -ENOENT;

	for (int i = 0; i < submit->job_count; i++) {
		submit->jobs[i]->perfmon = perfmon;
		if (i != 0)
			v3d_perfmon_get(perfmon);
	}

	return 0;
}

/*
 * Prepare fences to enforce job serialization when a perfmon is active. A job
 * that carries a non-global perfmon must wait for every job currently in-flight
 * across all HW queues to finish, otherwise concurrent unrelated work on the
 * same core would pollute the performance counters. Symmetrically, while such a
 * job is still in-flight, all subsequently submitted jobs must wait for it.
 *
 * We don't serialize the jobs when using a global perfmon as it's expected to
 * track concurrent activity from all jobs.
 */
static int
v3d_serialize_for_perfmon(struct v3d_job *job)
{
	struct v3d_dev *v3d = job->v3d;
	struct dma_fence *merged;
	bool is_global_perfmon;
	int ret;

	lockdep_assert_held(&v3d->sched_lock);

	scoped_guard(spinlock_irqsave, &v3d->perfmon_state.lock)
		is_global_perfmon = !!v3d->global_perfmon;

	if (is_global_perfmon)
		goto publish;

	if (job->perfmon) {
		for (enum v3d_queue q = 0; q < V3D_MAX_QUEUES; q++) {
			struct dma_fence *f = v3d->perfmon_state.last_hw_fence[q];

			if (!f || dma_fence_is_signaled(f))
				continue;

			ret = drm_sched_job_add_dependency(&job->base, dma_fence_get(f));
			if (ret)
				return ret;
		}
	} else if (v3d->perfmon_state.fence &&
		   !dma_fence_is_signaled(v3d->perfmon_state.fence)) {
		ret = drm_sched_job_add_dependency(&job->base,
						   dma_fence_get(v3d->perfmon_state.fence));
		if (ret)
			return ret;
	}

publish:
	/*
	 * Accumulate every in-flight job on this queue into one merged fence.
	 * A HW queue is fed by several scheduler entities (one per-fd), so jobs
	 * on it can complete out of order.
	 */
	merged = dma_fence_unwrap_merge(v3d->perfmon_state.last_hw_fence[job->queue],
					job->done_fence);
	if (!merged)
		return -ENOMEM;

	dma_fence_put(v3d->perfmon_state.last_hw_fence[job->queue]);
	v3d->perfmon_state.last_hw_fence[job->queue] = merged;

	if (job->perfmon && !is_global_perfmon) {
		dma_fence_put(v3d->perfmon_state.fence);
		v3d->perfmon_state.fence = dma_fence_get(job->done_fence);
	}

	return 0;
}

static void
v3d_submit_attach_object_fences(struct v3d_submit *submit)
{
	struct v3d_job *last_job = submit->jobs[submit->job_count - 1];

	/* The submission's last fence covers the entire submission. Attach it
	 * to every BO touched by any job in the submission.
	 */
	for (int i = 0; i < submit->job_count; i++) {
		struct v3d_job *job = submit->jobs[i];

		for (int j = 0; j < job->bo_count; j++) {
			/* XXX: Use shared fences for read-only objects. */
			dma_resv_add_fence(job->bo[j]->resv, last_job->done_fence,
					   DMA_RESV_USAGE_WRITE);
		}
	}
}

static void
v3d_submit_process_post_deps(struct v3d_submit *submit, struct drm_syncobj *sync_out,
			     struct v3d_submit_ext *se)
{
	bool has_multisync = se && (se->flags & DRM_V3D_EXT_ID_MULTI_SYNC);
	struct v3d_job *last_job = submit->jobs[submit->job_count - 1];

	/* Make sure single syncobj and multisync are mutually exclusive */
	WARN_ON_ONCE(sync_out && has_multisync);

	/* Update the return sync object for the job */
	/* If it only supports a single signal semaphore*/
	if (!has_multisync) {
		if (sync_out) {
			drm_syncobj_replace_fence(sync_out, last_job->done_fence);
			drm_syncobj_put(sync_out);
		}
		return;
	}

	/* If multiple semaphores extension is supported */
	if (se->out_sync_count) {
		for (int i = 0; i < se->out_sync_count; i++) {
			drm_syncobj_replace_fence(se->out_syncs[i].syncobj,
						  last_job->done_fence);
			drm_syncobj_put(se->out_syncs[i].syncobj);
		}
		kvfree(se->out_syncs);
	}
}

static int
v3d_submit_jobs(struct v3d_submit *submit, struct drm_syncobj *sync_out,
		struct v3d_submit_ext *se)
{
	struct v3d_dev *v3d = submit->v3d;
	int ret = 0;

	mutex_lock(&v3d->sched_lock);

	for (int i = 0; i < submit->job_count; i++) {
		struct v3d_job *job = submit->jobs[i];

		drm_sched_job_arm(&job->base);
		job->done_fence = dma_fence_get(&job->base.s_fence->finished);

		/* put by scheduler job completion */
		kref_get(&job->refcount);
	}

	for (int i = 1; i < submit->job_count; i++) {
		ret = drm_sched_job_add_dependency(&submit->jobs[i]->base,
						   dma_fence_get(submit->jobs[i - 1]->done_fence));
		if (ret)
			goto err;
	}

	for (int i = 0; i < submit->job_count; i++) {
		ret = v3d_serialize_for_perfmon(submit->jobs[i]);
		if (ret)
			goto err;
	}

	for (int i = 0; i < submit->job_count; i++)
		drm_sched_entity_push_job(&submit->jobs[i]->base);

	mutex_unlock(&v3d->sched_lock);

	v3d_submit_attach_object_fences(submit);
	v3d_submit_unlock_reservations(submit);
	v3d_submit_process_post_deps(submit, sync_out, se);

	v3d_submit_put_jobs(submit);

	return 0;

err:
	/* Mark every armed job as failed so run_job() skips execution */
	for (int i = 0; i < submit->job_count; i++)
		dma_fence_set_error(&submit->jobs[i]->base.s_fence->finished, ret);

	for (int i = 0; i < submit->job_count; i++)
		drm_sched_entity_push_job(&submit->jobs[i]->base);

	mutex_unlock(&v3d->sched_lock);

	v3d_submit_unlock_reservations(submit);
	v3d_submit_put_jobs(submit);

	return ret;
}

static int
v3d_setup_csd_jobs_and_bos(struct v3d_submit *submit,
			   struct drm_v3d_submit_csd *args,
			   struct v3d_submit_ext *se)
{
	struct v3d_csd_job *job;
	struct v3d_job *clean_job;
	int ret;

	job = (struct v3d_csd_job *)v3d_submit_add_job(submit, V3D_CSD);
	if (IS_ERR(job))
		return PTR_ERR(job);

	ret = v3d_job_add_syncobjs(&job->base, submit->file_priv, args->in_sync, se);
	if (ret)
		return ret;

	job->args = *args;

	clean_job = v3d_submit_add_job(submit, V3D_CACHE_CLEAN);
	if (IS_ERR(clean_job))
		return PTR_ERR(clean_job);

	return v3d_lookup_bos(submit, args->bo_handles, args->bo_handle_count);
}

static void
v3d_submit_put_post_deps(struct drm_syncobj *sync_out, struct v3d_submit_ext *se)
{
	unsigned int i;

	if (sync_out)
		drm_syncobj_put(sync_out);

	if (!(se && se->out_sync_count))
		return;

	for (i = 0; i < se->out_sync_count; i++)
		drm_syncobj_put(se->out_syncs[i].syncobj);
	kvfree(se->out_syncs);
}

static int
v3d_get_multisync_post_deps(struct drm_file *file_priv,
			    struct v3d_submit_ext *se,
			    u32 count, u64 handles)
{
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct v3d_dev *v3d = v3d_priv->v3d;
	struct drm_v3d_sem __user *post_deps;
	int i, ret;

	if (!count)
		return 0;

	se->out_syncs = (struct v3d_submit_outsync *)
			kvmalloc_objs(struct v3d_submit_outsync, count);
	if (!se->out_syncs)
		return -ENOMEM;

	post_deps = u64_to_user_ptr(handles);

	for (i = 0; i < count; i++) {
		struct drm_v3d_sem out;

		if (copy_from_user(&out, post_deps++, sizeof(out))) {
			ret = -EFAULT;
			drm_dbg(&v3d->drm, "Failed to copy post dep handles\n");
			goto fail;
		}

		se->out_syncs[i].syncobj = drm_syncobj_find(file_priv,
							    out.handle);
		if (!se->out_syncs[i].syncobj) {
			ret = -EINVAL;
			goto fail;
		}
	}
	se->out_sync_count = count;

	return 0;

fail:
	for (i--; i >= 0; i--)
		drm_syncobj_put(se->out_syncs[i].syncobj);
	kvfree(se->out_syncs);

	return ret;
}

/* Get data for multiple binary semaphores synchronization. Parse syncobj
 * to be signaled when job completes (out_sync).
 */
static int
v3d_get_multisync_submit_deps(struct drm_file *file_priv,
			      struct drm_v3d_extension __user *ext,
			      struct v3d_submit_ext *se)
{
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct v3d_dev *v3d = v3d_priv->v3d;
	struct drm_v3d_multi_sync multisync;
	int ret;

	if (se->in_sync_count || se->out_sync_count) {
		drm_dbg(&v3d->drm, "Two multisync extensions were added to the same job.");
		return -EINVAL;
	}

	if (copy_from_user(&multisync, ext, sizeof(multisync)))
		return -EFAULT;

	if (multisync.pad)
		return -EINVAL;

	if (!multisync.in_sync_count && !multisync.out_sync_count) {
		drm_dbg(&v3d->drm, "Empty multisync extension\n");
		return -EINVAL;
	}

	ret = v3d_get_multisync_post_deps(file_priv, se, multisync.out_sync_count,
					  multisync.out_syncs);
	if (ret)
		return ret;

	se->in_sync_count = multisync.in_sync_count;
	se->in_syncs = multisync.in_syncs;
	se->flags |= DRM_V3D_EXT_ID_MULTI_SYNC;
	se->wait_stage = multisync.wait_stage;

	return 0;
}

/* Returns false if the CPU job has an invalid configuration. */
static bool
v3d_validate_cpu_job(struct drm_file *file_priv, struct v3d_cpu_job *job)
{
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct v3d_dev *v3d = v3d_priv->v3d;

	if (!job) {
		drm_dbg(&v3d->drm, "CPU job extension was attached to a GPU job.\n");
		return false;
	}

	if (job->job_type) {
		drm_dbg(&v3d->drm, "Two CPU job extensions were added to the same CPU job.\n");
		return false;
	}

	return true;
}

/* Get data for the indirect CSD job submission. */
static int
v3d_get_cpu_indirect_csd_params(struct drm_file *file_priv,
				struct drm_v3d_extension __user *ext,
				struct v3d_cpu_job *job)
{
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct v3d_dev *v3d = v3d_priv->v3d;
	struct drm_v3d_indirect_csd indirect_csd;
	struct v3d_indirect_csd_info *info = &job->indirect_csd;

	if (!v3d_validate_cpu_job(file_priv, job))
		return -EINVAL;

	if (copy_from_user(&indirect_csd, ext, sizeof(indirect_csd)))
		return -EFAULT;

	if (!v3d_has_csd(v3d)) {
		drm_warn(&v3d->drm, "Attempting CSD submit on non-CSD hardware.\n");
		return -EINVAL;
	}

	job->job_type = V3D_CPU_JOB_TYPE_INDIRECT_CSD;
	info->args = indirect_csd.submit;
	info->offset = indirect_csd.offset;
	info->wg_size = indirect_csd.wg_size;
	memcpy(&info->wg_uniform_offsets, &indirect_csd.wg_uniform_offsets,
	       sizeof(indirect_csd.wg_uniform_offsets));

	info->indirect = drm_gem_object_lookup(file_priv, indirect_csd.indirect);

	return 0;
}

/* Get data for the query timestamp job submission. */
static int
v3d_get_cpu_timestamp_query_params(struct drm_file *file_priv,
				   struct drm_v3d_extension __user *ext,
				   struct v3d_cpu_job *job)
{
	u32 __user *offsets, *syncs;
	struct drm_v3d_timestamp_query timestamp;
	struct v3d_timestamp_query_info *query_info = &job->timestamp_query;
	unsigned int i;
	int err;

	if (!v3d_validate_cpu_job(file_priv, job))
		return -EINVAL;

	if (copy_from_user(&timestamp, ext, sizeof(timestamp)))
		return -EFAULT;

	if (timestamp.pad)
		return -EINVAL;

	job->job_type = V3D_CPU_JOB_TYPE_TIMESTAMP_QUERY;

	query_info->queries = kvmalloc_objs(struct v3d_timestamp_query,
					    timestamp.count);
	if (!query_info->queries)
		return -ENOMEM;

	offsets = u64_to_user_ptr(timestamp.offsets);
	syncs = u64_to_user_ptr(timestamp.syncs);

	for (i = 0; i < timestamp.count; i++) {
		u32 offset, sync;

		if (get_user(offset, offsets++)) {
			err = -EFAULT;
			goto error;
		}

		query_info->queries[i].offset = offset;

		if (get_user(sync, syncs++)) {
			err = -EFAULT;
			goto error;
		}

		query_info->queries[i].syncobj = drm_syncobj_find(file_priv,
								  sync);
		if (!query_info->queries[i].syncobj) {
			err = -ENOENT;
			goto error;
		}
	}
	query_info->count = timestamp.count;

	return 0;

error:
	v3d_timestamp_query_info_free(&job->timestamp_query, i);
	return err;
}

static int
v3d_get_cpu_reset_timestamp_params(struct drm_file *file_priv,
				   struct drm_v3d_extension __user *ext,
				   struct v3d_cpu_job *job)
{
	u32 __user *syncs;
	struct drm_v3d_reset_timestamp_query reset;
	struct v3d_timestamp_query_info *query_info = &job->timestamp_query;
	unsigned int i;
	int err;

	if (!v3d_validate_cpu_job(file_priv, job))
		return -EINVAL;

	if (copy_from_user(&reset, ext, sizeof(reset)))
		return -EFAULT;

	job->job_type = V3D_CPU_JOB_TYPE_RESET_TIMESTAMP_QUERY;

	query_info->queries = kvmalloc_objs(struct v3d_timestamp_query,
					    reset.count);
	if (!query_info->queries)
		return -ENOMEM;

	syncs = u64_to_user_ptr(reset.syncs);

	for (i = 0; i < reset.count; i++) {
		u32 sync;

		query_info->queries[i].offset = reset.offset + 8 * i;

		if (get_user(sync, syncs++)) {
			err = -EFAULT;
			goto error;
		}

		query_info->queries[i].syncobj = drm_syncobj_find(file_priv,
								  sync);
		if (!query_info->queries[i].syncobj) {
			err = -ENOENT;
			goto error;
		}
	}
	query_info->count = reset.count;

	return 0;

error:
	v3d_timestamp_query_info_free(&job->timestamp_query, i);
	return err;
}

/* Get data for the copy timestamp query results job submission. */
static int
v3d_get_cpu_copy_query_results_params(struct drm_file *file_priv,
				      struct drm_v3d_extension __user *ext,
				      struct v3d_cpu_job *job)
{
	u32 __user *offsets, *syncs;
	struct drm_v3d_copy_timestamp_query copy;
	struct v3d_timestamp_query_info *query_info = &job->timestamp_query;
	unsigned int i;
	int err;

	if (!v3d_validate_cpu_job(file_priv, job))
		return -EINVAL;

	if (copy_from_user(&copy, ext, sizeof(copy)))
		return -EFAULT;

	if (copy.pad)
		return -EINVAL;

	job->job_type = V3D_CPU_JOB_TYPE_COPY_TIMESTAMP_QUERY;

	query_info->queries = kvmalloc_objs(struct v3d_timestamp_query,
					    copy.count);
	if (!query_info->queries)
		return -ENOMEM;

	offsets = u64_to_user_ptr(copy.offsets);
	syncs = u64_to_user_ptr(copy.syncs);

	for (i = 0; i < copy.count; i++) {
		u32 offset, sync;

		if (get_user(offset, offsets++)) {
			err = -EFAULT;
			goto error;
		}

		query_info->queries[i].offset = offset;

		if (get_user(sync, syncs++)) {
			err = -EFAULT;
			goto error;
		}

		query_info->queries[i].syncobj = drm_syncobj_find(file_priv,
								  sync);
		if (!query_info->queries[i].syncobj) {
			err = -ENOENT;
			goto error;
		}
	}
	query_info->count = copy.count;

	job->copy.do_64bit = copy.do_64bit;
	job->copy.do_partial = copy.do_partial;
	job->copy.availability_bit = copy.availability_bit;
	job->copy.offset = copy.offset;
	job->copy.stride = copy.stride;

	return 0;

error:
	v3d_timestamp_query_info_free(&job->timestamp_query, i);
	return err;
}

static int
v3d_copy_query_info(struct v3d_performance_query_info *query_info,
		    unsigned int count,
		    unsigned int nperfmons,
		    u32 __user *syncs,
		    u64 __user *kperfmon_ids,
		    struct drm_file *file_priv)
{
	unsigned int i, j;
	int err;

	for (i = 0; i < count; i++) {
		struct v3d_performance_query *query = &query_info->queries[i];
		u32 __user *ids_pointer;
		u32 sync, id;
		u64 ids;

		if (get_user(sync, syncs++)) {
			err = -EFAULT;
			goto error;
		}

		if (get_user(ids, kperfmon_ids++)) {
			err = -EFAULT;
			goto error;
		}

		query->kperfmon_ids =
			kvmalloc_array(nperfmons,
				       sizeof(struct v3d_performance_query *),
				       GFP_KERNEL);
		if (!query->kperfmon_ids) {
			err = -ENOMEM;
			goto error;
		}

		ids_pointer = u64_to_user_ptr(ids);

		for (j = 0; j < nperfmons; j++) {
			if (get_user(id, ids_pointer++)) {
				kvfree(query->kperfmon_ids);
				err = -EFAULT;
				goto error;
			}

			query->kperfmon_ids[j] = id;
		}

		query->syncobj = drm_syncobj_find(file_priv, sync);
		if (!query->syncobj) {
			kvfree(query->kperfmon_ids);
			err = -ENOENT;
			goto error;
		}
	}

	return 0;

error:
	v3d_performance_query_info_free(query_info, i);
	return err;
}

static int
v3d_get_cpu_reset_performance_params(struct drm_file *file_priv,
				     struct drm_v3d_extension __user *ext,
				     struct v3d_cpu_job *job)
{
	struct v3d_performance_query_info *query_info = &job->performance_query;
	struct drm_v3d_reset_performance_query reset;
	int err;

	if (!v3d_validate_cpu_job(file_priv, job))
		return -EINVAL;

	if (copy_from_user(&reset, ext, sizeof(reset)))
		return -EFAULT;

	job->job_type = V3D_CPU_JOB_TYPE_RESET_PERFORMANCE_QUERY;

	query_info->queries =
		kvmalloc_objs(struct v3d_performance_query, reset.count);
	if (!query_info->queries)
		return -ENOMEM;

	err = v3d_copy_query_info(query_info,
				  reset.count,
				  reset.nperfmons,
				  u64_to_user_ptr(reset.syncs),
				  u64_to_user_ptr(reset.kperfmon_ids),
				  file_priv);
	if (err)
		return err;

	query_info->count = reset.count;
	query_info->nperfmons = reset.nperfmons;

	return 0;
}

static int
v3d_get_cpu_copy_performance_query_params(struct drm_file *file_priv,
					  struct drm_v3d_extension __user *ext,
					  struct v3d_cpu_job *job)
{
	struct v3d_performance_query_info *query_info = &job->performance_query;
	struct drm_v3d_copy_performance_query copy;
	int err;

	if (!v3d_validate_cpu_job(file_priv, job))
		return -EINVAL;

	if (copy_from_user(&copy, ext, sizeof(copy)))
		return -EFAULT;

	if (copy.pad)
		return -EINVAL;

	job->job_type = V3D_CPU_JOB_TYPE_COPY_PERFORMANCE_QUERY;

	query_info->queries =
		kvmalloc_objs(struct v3d_performance_query, copy.count);
	if (!query_info->queries)
		return -ENOMEM;

	err = v3d_copy_query_info(query_info,
				  copy.count,
				  copy.nperfmons,
				  u64_to_user_ptr(copy.syncs),
				  u64_to_user_ptr(copy.kperfmon_ids),
				  file_priv);
	if (err)
		return err;

	query_info->count = copy.count;
	query_info->nperfmons = copy.nperfmons;
	query_info->ncounters = copy.ncounters;

	job->copy.do_64bit = copy.do_64bit;
	job->copy.do_partial = copy.do_partial;
	job->copy.availability_bit = copy.availability_bit;
	job->copy.offset = copy.offset;
	job->copy.stride = copy.stride;

	return 0;
}

/* Whenever userspace sets ioctl extensions, v3d_get_extensions parses data
 * according to the extension id (name).
 */
static int
v3d_get_extensions(struct drm_file *file_priv,
		   u64 ext_handles,
		   struct v3d_submit_ext *se,
		   struct v3d_cpu_job *job)
{
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct v3d_dev *v3d = v3d_priv->v3d;
	struct drm_v3d_extension __user *user_ext;
	int ret;

	user_ext = u64_to_user_ptr(ext_handles);
	while (user_ext) {
		struct drm_v3d_extension ext;

		if (copy_from_user(&ext, user_ext, sizeof(ext))) {
			drm_dbg(&v3d->drm, "Failed to copy submit extension\n");
			return -EFAULT;
		}

		switch (ext.id) {
		case DRM_V3D_EXT_ID_MULTI_SYNC:
			ret = v3d_get_multisync_submit_deps(file_priv, user_ext, se);
			break;
		case DRM_V3D_EXT_ID_CPU_INDIRECT_CSD:
			ret = v3d_get_cpu_indirect_csd_params(file_priv, user_ext, job);
			break;
		case DRM_V3D_EXT_ID_CPU_TIMESTAMP_QUERY:
			ret = v3d_get_cpu_timestamp_query_params(file_priv, user_ext, job);
			break;
		case DRM_V3D_EXT_ID_CPU_RESET_TIMESTAMP_QUERY:
			ret = v3d_get_cpu_reset_timestamp_params(file_priv, user_ext, job);
			break;
		case DRM_V3D_EXT_ID_CPU_COPY_TIMESTAMP_QUERY:
			ret = v3d_get_cpu_copy_query_results_params(file_priv, user_ext, job);
			break;
		case DRM_V3D_EXT_ID_CPU_RESET_PERFORMANCE_QUERY:
			ret = v3d_get_cpu_reset_performance_params(file_priv, user_ext, job);
			break;
		case DRM_V3D_EXT_ID_CPU_COPY_PERFORMANCE_QUERY:
			ret = v3d_get_cpu_copy_performance_query_params(file_priv, user_ext, job);
			break;
		default:
			drm_dbg(&v3d->drm, "Unknown V3D extension ID: %d\n", ext.id);
			return -EINVAL;
		}

		if (ret)
			return ret;

		user_ext = u64_to_user_ptr(ext.next);
	}

	return 0;
}

/**
 * v3d_submit_cl_ioctl() - Submits a job (frame) to the V3D.
 * @dev: DRM device
 * @data: ioctl argument
 * @file_priv: DRM file for this fd
 *
 * This is the main entrypoint for userspace to submit a 3D frame to
 * the GPU.  Userspace provides the binner command list (if
 * applicable), and the kernel sets up the render command list to draw
 * to the framebuffer described in the ioctl, using the command lists
 * that the 3D engine's binner will produce.
 */
int
v3d_submit_cl_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct v3d_submit submit = { .v3d = to_v3d_dev(dev), .file_priv = file_priv };
	struct drm_v3d_submit_cl *args = data;
	struct drm_syncobj *sync_out = NULL;
	struct v3d_submit_ext se = {0};
	struct v3d_bin_job *bin = NULL;
	struct v3d_render_job *render;
	struct v3d_job *clean_job;
	int ret;

	trace_v3d_submit_cl_ioctl(dev, args->rcl_start, args->rcl_end);

	if (args->pad)
		return -EINVAL;

	if (args->flags &&
	    args->flags & ~(DRM_V3D_SUBMIT_CL_FLUSH_CACHE |
			    DRM_V3D_SUBMIT_EXTENSION)) {
		drm_dbg(dev, "invalid flags: %d\n", args->flags);
		return -EINVAL;
	}

	if (args->flags & DRM_V3D_SUBMIT_EXTENSION) {
		ret = v3d_get_extensions(file_priv, args->extensions, &se, NULL);
		if (ret) {
			drm_dbg(dev, "Failed to get extensions.\n");
			return ret;
		}
	}

	/* If multisync is configured, give priority to it and ignore out_sync. */
	if (args->out_sync && !(se.flags & DRM_V3D_EXT_ID_MULTI_SYNC)) {
		sync_out = drm_syncobj_find(file_priv, args->out_sync);
		if (!sync_out)
			return -ENOENT;
	}

	if (args->bcl_start != args->bcl_end) {
		bin = (struct v3d_bin_job *)v3d_submit_add_job(&submit, V3D_BIN);
		if (IS_ERR(bin)) {
			ret = PTR_ERR(bin);
			goto fail;
		}

		bin->start = args->bcl_start;
		bin->end = args->bcl_end;
		bin->qma = args->qma;
		bin->qms = args->qms;
		bin->qts = args->qts;

		ret = v3d_job_add_syncobjs(&bin->base, file_priv, args->in_sync_bcl,
					   &se);
		if (ret)
			goto fail;
	}

	render = (struct v3d_render_job *)v3d_submit_add_job(&submit, V3D_RENDER);
	if (IS_ERR(render)) {
		ret = PTR_ERR(render);
		goto fail;
	}

	INIT_LIST_HEAD(&render->unref_list);
	render->start = args->rcl_start;
	render->end = args->rcl_end;

	if (bin)
		bin->render = render;

	ret = v3d_job_add_syncobjs(&render->base, file_priv, args->in_sync_rcl, &se);
	if (ret)
		goto fail;

	if (args->flags & DRM_V3D_SUBMIT_CL_FLUSH_CACHE) {
		clean_job = v3d_submit_add_job(&submit, V3D_CACHE_CLEAN);
		if (IS_ERR(clean_job)) {
			ret = PTR_ERR(clean_job);
			goto fail;
		}
	}

	ret = v3d_attach_perfmon_to_jobs(&submit, args->perfmon_id);
	if (ret)
		goto fail;

	ret = v3d_lookup_bos(&submit, args->bo_handles, args->bo_handle_count);
	if (ret)
		goto fail;

	ret = v3d_submit_lock_reservations(&submit);
	if (ret)
		goto fail;

	ret = v3d_submit_jobs(&submit, sync_out, &se);
	if (ret)
		goto fail_submit;

	return 0;

fail:
	v3d_submit_cleanup_jobs(&submit);
fail_submit:
	v3d_submit_put_post_deps(sync_out, &se);

	return ret;
}

/**
 * v3d_submit_tfu_ioctl() - Submits a TFU (texture formatting) job to the V3D.
 * @dev: DRM device
 * @data: ioctl argument
 * @file_priv: DRM file for this fd
 *
 * Userspace provides the register setup for the TFU, which we don't
 * need to validate since the TFU is behind the MMU.
 */
int
v3d_submit_tfu_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct v3d_submit submit = { .v3d = to_v3d_dev(dev), .file_priv = file_priv };
	struct drm_v3d_submit_tfu *args = data;
	struct drm_syncobj *sync_out = NULL;
	struct v3d_submit_ext se = {0};
	struct v3d_tfu_job *job;
	int ret = 0;

	trace_v3d_submit_tfu_ioctl(dev, args->iia);

	if (args->flags && !(args->flags & DRM_V3D_SUBMIT_EXTENSION)) {
		drm_dbg(dev, "invalid flags: %d\n", args->flags);
		return -EINVAL;
	}

	if (args->flags & DRM_V3D_SUBMIT_EXTENSION) {
		ret = v3d_get_extensions(file_priv, args->extensions, &se, NULL);
		if (ret) {
			drm_dbg(dev, "Failed to get extensions.\n");
			return ret;
		}
	}

	/* If multisync is configured, give priority to it and ignore out_sync. */
	if (args->out_sync && !(se.flags & DRM_V3D_EXT_ID_MULTI_SYNC)) {
		sync_out = drm_syncobj_find(file_priv, args->out_sync);
		if (!sync_out)
			return -ENOENT;
	}

	job = (struct v3d_tfu_job *)v3d_submit_add_job(&submit, V3D_TFU);
	if (IS_ERR(job)) {
		ret = PTR_ERR(job);
		goto fail;
	}

	ret = v3d_job_add_syncobjs(&job->base, file_priv, args->in_sync, &se);
	if (ret)
		goto fail;

	job->base.bo = kzalloc_objs(*job->base.bo, ARRAY_SIZE(args->bo_handles));
	if (!job->base.bo) {
		ret = -ENOMEM;
		goto fail;
	}

	job->args = *args;

	for (job->base.bo_count = 0;
	     job->base.bo_count < ARRAY_SIZE(args->bo_handles);
	     job->base.bo_count++) {
		struct drm_gem_object *bo;

		if (!args->bo_handles[job->base.bo_count])
			break;

		bo = drm_gem_object_lookup(file_priv, args->bo_handles[job->base.bo_count]);
		if (!bo) {
			drm_dbg(dev, "Failed to look up GEM BO %d: %d\n",
				job->base.bo_count,
				args->bo_handles[job->base.bo_count]);
			ret = -ENOENT;
			goto fail;
		}
		job->base.bo[job->base.bo_count] = bo;
	}

	ret = v3d_submit_lock_reservations(&submit);
	if (ret)
		goto fail;

	ret = v3d_submit_jobs(&submit, sync_out, &se);
	if (ret)
		goto fail_submit;

	return 0;

fail:
	v3d_submit_cleanup_jobs(&submit);
fail_submit:
	v3d_submit_put_post_deps(sync_out, &se);

	return ret;
}

/**
 * v3d_submit_csd_ioctl() - Submits a CSD (compute shader) job to the V3D.
 * @dev: DRM device
 * @data: ioctl argument
 * @file_priv: DRM file for this fd
 *
 * Userspace provides the register setup for the CSD, which we don't
 * need to validate since the CSD is behind the MMU.
 */
int
v3d_submit_csd_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct v3d_submit submit = { .v3d = to_v3d_dev(dev), .file_priv = file_priv };
	struct drm_v3d_submit_csd *args = data;
	struct drm_syncobj *sync_out = NULL;
	struct v3d_submit_ext se = {0};
	int ret;

	trace_v3d_submit_csd_ioctl(dev, args->cfg[5], args->cfg[6]);

	if (args->pad)
		return -EINVAL;

	if (!v3d_has_csd(submit.v3d)) {
		drm_warn(dev, "Attempting CSD submit on non-CSD hardware\n");
		return -EINVAL;
	}

	if (args->flags && !(args->flags & DRM_V3D_SUBMIT_EXTENSION)) {
		drm_dbg(dev, "invalid flags: %d\n", args->flags);
		return -EINVAL;
	}

	if (args->flags & DRM_V3D_SUBMIT_EXTENSION) {
		ret = v3d_get_extensions(file_priv, args->extensions, &se, NULL);
		if (ret) {
			drm_dbg(dev, "Failed to get extensions.\n");
			return ret;
		}
	}

	/* If multisync is configured, give priority to it and ignore out_sync. */
	if (args->out_sync && !(se.flags & DRM_V3D_EXT_ID_MULTI_SYNC)) {
		sync_out = drm_syncobj_find(file_priv, args->out_sync);
		if (!sync_out)
			return -ENOENT;
	}

	ret = v3d_setup_csd_jobs_and_bos(&submit, args, &se);
	if (ret)
		goto fail;

	ret = v3d_attach_perfmon_to_jobs(&submit, args->perfmon_id);
	if (ret)
		goto fail;

	ret = v3d_submit_lock_reservations(&submit);
	if (ret)
		goto fail;

	ret = v3d_submit_jobs(&submit, sync_out, &se);
	if (ret)
		goto fail_submit;

	return 0;

fail:
	v3d_submit_cleanup_jobs(&submit);
fail_submit:
	v3d_submit_put_post_deps(sync_out, &se);

	return ret;
}

static const unsigned int cpu_job_bo_handle_count[] = {
	[V3D_CPU_JOB_TYPE_INDIRECT_CSD] = 1,
	[V3D_CPU_JOB_TYPE_TIMESTAMP_QUERY] = 1,
	[V3D_CPU_JOB_TYPE_RESET_TIMESTAMP_QUERY] = 1,
	[V3D_CPU_JOB_TYPE_COPY_TIMESTAMP_QUERY] = 2,
	[V3D_CPU_JOB_TYPE_RESET_PERFORMANCE_QUERY] = 0,
	[V3D_CPU_JOB_TYPE_COPY_PERFORMANCE_QUERY] = 1,
};

/* Reject offset + (count - 1) * stride + write_size if it leaves the BO. */
static int
v3d_check_copy_extent(struct drm_device *dev, size_t bo_size,
		      u32 offset, u32 stride, u32 count, u64 write_size)
{
	u64 last;

	if (!count)
		return 0;

	/*
	 * The executors walk a u8 * cursor, so the furthest written byte is
	 * offset + (count - 1) * stride + write_size, matching the pointer
	 * arithmetic in v3d_copy_query_results()/v3d_copy_performance_query().
	 * (count - 1) * stride is a u32 * u32 product that is exact in u64,
	 * and offset + write_size stays far below the u64 range, so a single
	 * overflow check guards the total.
	 */
	last = write_size + offset;
	if (check_add_overflow((u64)(count - 1) * stride, last, &last) ||
	    last > bo_size) {
		drm_dbg(dev, "CPU job copy buffer exceeds the destination BO.\n");
		return -EINVAL;
	}

	return 0;
}

/* Reject a query CPU job whose writes would land outside their BO. */
static int
v3d_cpu_job_bounds_check(struct v3d_cpu_job *job)
{
	struct drm_device *dev = &job->base.v3d->drm;
	struct v3d_timestamp_query_info *tquery = &job->timestamp_query;
	struct v3d_copy_query_results_info *copy = &job->copy;
	u32 elem = copy->do_64bit ? sizeof(u64) : sizeof(u32);
	struct v3d_bo *dst, *src;
	u64 slots, write_size;
	u32 i;

	switch (job->job_type) {
	case V3D_CPU_JOB_TYPE_TIMESTAMP_QUERY:
	case V3D_CPU_JOB_TYPE_RESET_TIMESTAMP_QUERY:
		/* Each query writes one u64 timestamp slot into bo[0]. */
		dst = to_v3d_bo(job->base.bo[0]);

		for (i = 0; i < tquery->count; i++) {
			if ((u64)tquery->queries[i].offset + sizeof(u64) >
			    dst->base.base.size)
				goto err_range;
		}
		return 0;
	case V3D_CPU_JOB_TYPE_COPY_TIMESTAMP_QUERY:
		/* Copies one u64 per query from bo[1] into bo[0]. */
		dst = to_v3d_bo(job->base.bo[0]);
		src = to_v3d_bo(job->base.bo[1]);

		for (i = 0; i < tquery->count; i++) {
			if ((u64)tquery->queries[i].offset + sizeof(u64) >
			    src->base.base.size)
				goto err_range;
		}

		write_size = (copy->availability_bit ? 2 : 1) * elem;
		return v3d_check_copy_extent(dev, dst->base.base.size,
					     copy->offset, copy->stride,
					     tquery->count, write_size);
	case V3D_CPU_JOB_TYPE_COPY_PERFORMANCE_QUERY:
		/*
		 * Each query writes nperfmons * DRM_V3D_MAX_PERF_COUNTERS
		 * counter slots into bo[0], plus an availability slot at
		 * index ncounters. nperfmons and ncounters are user values,
		 * so the slot count is computed overflow-safe.
		 */
		dst = to_v3d_bo(job->base.bo[0]);

		slots = (u64)job->performance_query.nperfmons *
			DRM_V3D_MAX_PERF_COUNTERS;
		if (copy->availability_bit)
			slots = max(slots,
				    (u64)job->performance_query.ncounters + 1);

		write_size = slots * elem;
		return v3d_check_copy_extent(dev, dst->base.base.size,
					     copy->offset, copy->stride,
					     job->performance_query.count,
					     write_size);
	case V3D_CPU_JOB_TYPE_INDIRECT_CSD: {
		struct v3d_indirect_csd_info *indirect_csd = &job->indirect_csd;

		/* 3 is the three dimensions (x, y, z) of the workgroup counts. */
		src = to_v3d_bo(job->base.bo[0]);
		if ((u64)indirect_csd->offset + 3 * sizeof(u32) >
		    src->base.base.size)
			goto err_range;

		dst = to_v3d_bo(indirect_csd->indirect);
		for (i = 0; i < 3; i++) {
			u32 uidx = indirect_csd->wg_uniform_offsets[i];

			/*
			 * 0xffffffff means "skip this rewrite", so the exec
			 * path never writes that index and it needs no check.
			 */
			if (uidx != 0xffffffff &&
			    (u64)uidx * sizeof(u32) + sizeof(u32) >
			    dst->base.base.size)
				goto err_range;
		}
		return 0;
	}
	default:
		return 0;
	}

err_range:
	drm_dbg(dev, "CPU job query offset exceeds the BO.\n");
	return -EINVAL;
}

/**
 * v3d_submit_cpu_ioctl() - Submits a CPU job to the V3D.
 * @dev: DRM device
 * @data: ioctl argument
 * @file_priv: DRM file for this fd
 *
 * Userspace specifies the CPU job type and data required to perform its
 * operations through the drm_v3d_extension struct.
 */
int
v3d_submit_cpu_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct v3d_submit submit = { .v3d = to_v3d_dev(dev), .file_priv = file_priv };
	struct drm_v3d_submit_cpu *args = data;
	struct v3d_submit_ext se = {0};
	struct v3d_cpu_job *cpu_job = NULL;
	int ret;

	if (args->flags && !(args->flags & DRM_V3D_SUBMIT_EXTENSION)) {
		drm_dbg(dev, "Invalid flags: %d\n", args->flags);
		return -EINVAL;
	}

	cpu_job = (struct v3d_cpu_job *)v3d_submit_add_job(&submit, V3D_CPU);
	if (IS_ERR(cpu_job))
		return PTR_ERR(cpu_job);

	if (args->flags & DRM_V3D_SUBMIT_EXTENSION) {
		ret = v3d_get_extensions(file_priv, args->extensions, &se, cpu_job);
		if (ret) {
			drm_dbg(dev, "Failed to get extensions.\n");
			goto fail;
		}
	}

	/* Every CPU job must have a CPU job user extension */
	if (!cpu_job->job_type) {
		drm_dbg(dev, "CPU job must have a CPU job user extension.\n");
		ret = -EINVAL;
		goto fail;
	}

	if (args->bo_handle_count != cpu_job_bo_handle_count[cpu_job->job_type]) {
		drm_dbg(dev, "This CPU job was not submitted with the proper number of BOs.\n");
		ret = -EINVAL;
		goto fail;
	}

	trace_v3d_submit_cpu_ioctl(dev, cpu_job->job_type);

	ret = v3d_job_add_syncobjs(&cpu_job->base, file_priv, 0, &se);
	if (ret)
		goto fail;

	/* Look up the CPU jobs' BOs before v3d_setup_csd_jobs_and_bos() appends
	 * the CSD and clean jobs in the case of indirect CSD job.
	 */
	if (args->bo_handle_count) {
		ret = v3d_lookup_bos(&submit, args->bo_handles, args->bo_handle_count);
		if (ret)
			goto fail;

		ret = v3d_cpu_job_bounds_check(cpu_job);
		if (ret)
			goto fail;
	}

	if (cpu_job->job_type == V3D_CPU_JOB_TYPE_INDIRECT_CSD) {
		ret = v3d_setup_csd_jobs_and_bos(&submit, &cpu_job->indirect_csd.args,
						 NULL);
		if (ret)
			goto fail;

		/* The CSD job was appended at jobs[1] */
		if (WARN_ON(submit.jobs[1]->queue != V3D_CSD)) {
			ret = -EINVAL;
			goto fail;
		}

		cpu_job->indirect_csd.job = container_of(submit.jobs[1], struct v3d_csd_job,
							 base);
	}

	ret = v3d_submit_lock_reservations(&submit);
	if (ret)
		goto fail;

	ret = v3d_submit_jobs(&submit, NULL, &se);
	if (ret)
		goto fail_submit;

	return 0;

fail:
	v3d_submit_cleanup_jobs(&submit);
fail_submit:
	v3d_submit_put_post_deps(NULL, &se);

	return ret;
}
