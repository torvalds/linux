// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2014-2018 Broadcom
 * Copyright (C) 2023 Raspberry Pi
 */

#include <drm/drm_syncobj.h>

#include "v3d_drv.h"
#include "v3d_regs.h"
#include "v3d_trace.h"

/* Takes the reservation lock on all the BOs being referenced, so that
 * at queue submit time we can update the reservations.
 *
 * We don't lock the RCL the tile alloc/state BOs, or overflow memory
 * (all of which are on exec->unref_list).  They're entirely private
 * to v3d, so we don't attach dma-buf fences to them.
 */
static int
v3d_lock_bo_reservations(struct v3d_job *job,
			 struct ww_acquire_ctx *acquire_ctx)
{
	int i, ret;

	ret = drm_gem_lock_reservations(job->bo, job->bo_count, acquire_ctx);
	if (ret)
		return ret;

	for (i = 0; i < job->bo_count; i++) {
		ret = dma_resv_reserve_fences(job->bo[i]->resv, 1);
		if (ret)
			goto fail;

		ret = drm_sched_job_add_implicit_dependencies(&job->base,
							      job->bo[i], true);
		if (ret)
			goto fail;
	}

	return 0;

fail:
	drm_gem_unlock_reservations(job->bo, job->bo_count, acquire_ctx);
	return ret;
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
 * the submitted job's BO list.  This does the validation of the job's
 * BO list and reference counting for the lifetime of the job.
 *
 * Note that this function doesn't need to unreference the BOs on
 * failure, because that will happen at v3d_exec_cleanup() time.
 */
static int
v3d_lookup_bos(struct drm_device *dev,
	       struct drm_file *file_priv,
	       struct v3d_job *job,
	       u64 bo_handles,
	       u32 bo_count)
{
	job->bo_count = bo_count;

	if (!job->bo_count) {
		/* See comment on bo_index for why we have to check
		 * this.
		 */
		DRM_DEBUG("Rendering requires BOs\n");
		return -EINVAL;
	}

	return drm_gem_objects_lookup(file_priv,
				      (void __user *)(uintptr_t)bo_handles,
				      job->bo_count, &job->bo);
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
v3d_job_allocate(void **container, size_t size)
{
	*container = kcalloc(1, size, GFP_KERNEL);
	if (!*container) {
		DRM_ERROR("Cannot allocate memory for V3D job.\n");
		return -ENOMEM;
	}

	return 0;
}

static void
v3d_job_deallocate(void **container)
{
	kfree(*container);
	*container = NULL;
}

static int
v3d_job_init(struct v3d_dev *v3d, struct drm_file *file_priv,
	     struct v3d_job *job, void (*free)(struct kref *ref),
	     u32 in_sync, struct v3d_submit_ext *se, enum v3d_queue queue)
{
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	bool has_multisync = se && (se->flags & DRM_V3D_EXT_ID_MULTI_SYNC);
	int ret, i;

	job->v3d = v3d;
	job->free = free;
	job->file = file_priv;

	ret = drm_sched_job_init(&job->base, &v3d_priv->sched_entity[queue],
				 1, v3d_priv);
	if (ret)
		return ret;

	if (has_multisync) {
		if (se->in_sync_count && se->wait_stage == queue) {
			struct drm_v3d_sem __user *handle = u64_to_user_ptr(se->in_syncs);

			for (i = 0; i < se->in_sync_count; i++) {
				struct drm_v3d_sem in;

				if (copy_from_user(&in, handle++, sizeof(in))) {
					ret = -EFAULT;
					DRM_DEBUG("Failed to copy wait dep handle.\n");
					goto fail_deps;
				}
				ret = drm_sched_job_add_syncobj_dependency(&job->base, file_priv, in.handle, 0);

				// TODO: Investigate why this was filtered out for the IOCTL.
				if (ret && ret != -ENOENT)
					goto fail_deps;
			}
		}
	} else {
		ret = drm_sched_job_add_syncobj_dependency(&job->base, file_priv, in_sync, 0);

		// TODO: Investigate why this was filtered out for the IOCTL.
		if (ret && ret != -ENOENT)
			goto fail_deps;
	}

	kref_init(&job->refcount);

	return 0;

fail_deps:
	drm_sched_job_cleanup(&job->base);
	return ret;
}

static void
v3d_push_job(struct v3d_job *job)
{
	drm_sched_job_arm(&job->base);

	job->done_fence = dma_fence_get(&job->base.s_fence->finished);

	/* put by scheduler job completion */
	kref_get(&job->refcount);

	drm_sched_entity_push_job(&job->base);
}

static void
v3d_attach_fences_and_unlock_reservation(struct drm_file *file_priv,
					 struct v3d_job *job,
					 struct ww_acquire_ctx *acquire_ctx,
					 u32 out_sync,
					 struct v3d_submit_ext *se,
					 struct dma_fence *done_fence)
{
	struct drm_syncobj *sync_out;
	bool has_multisync = se && (se->flags & DRM_V3D_EXT_ID_MULTI_SYNC);
	int i;

	for (i = 0; i < job->bo_count; i++) {
		/* XXX: Use shared fences for read-only objects. */
		dma_resv_add_fence(job->bo[i]->resv, job->done_fence,
				   DMA_RESV_USAGE_WRITE);
	}

	drm_gem_unlock_reservations(job->bo, job->bo_count, acquire_ctx);

	/* Update the return sync object for the job */
	/* If it only supports a single signal semaphore*/
	if (!has_multisync) {
		sync_out = drm_syncobj_find(file_priv, out_sync);
		if (sync_out) {
			drm_syncobj_replace_fence(sync_out, done_fence);
			drm_syncobj_put(sync_out);
		}
		return;
	}

	/* If multiple semaphores extension is supported */
	if (se->out_sync_count) {
		for (i = 0; i < se->out_sync_count; i++) {
			drm_syncobj_replace_fence(se->out_syncs[i].syncobj,
						  done_fence);
			drm_syncobj_put(se->out_syncs[i].syncobj);
		}
		kvfree(se->out_syncs);
	}
}

static int
v3d_setup_csd_jobs_and_bos(struct drm_file *file_priv,
			   struct v3d_dev *v3d,
			   struct drm_v3d_submit_csd *args,
			   struct v3d_csd_job **job,
			   struct v3d_job **clean_job,
			   struct v3d_submit_ext *se,
			   struct ww_acquire_ctx *acquire_ctx)
{
	int ret;

	ret = v3d_job_allocate((void *)job, sizeof(**job));
	if (ret)
		return ret;

	ret = v3d_job_init(v3d, file_priv, &(*job)->base,
			   v3d_job_free, args->in_sync, se, V3D_CSD);
	if (ret) {
		v3d_job_deallocate((void *)job);
		return ret;
	}

	ret = v3d_job_allocate((void *)clean_job, sizeof(**clean_job));
	if (ret)
		return ret;

	ret = v3d_job_init(v3d, file_priv, *clean_job,
			   v3d_job_free, 0, NULL, V3D_CACHE_CLEAN);
	if (ret) {
		v3d_job_deallocate((void *)clean_job);
		return ret;
	}

	(*job)->args = *args;

	ret = v3d_lookup_bos(&v3d->drm, file_priv, *clean_job,
			     args->bo_handles, args->bo_handle_count);
	if (ret)
		return ret;

	return v3d_lock_bo_reservations(*clean_job, acquire_ctx);
}

static void
v3d_put_multisync_post_deps(struct v3d_submit_ext *se)
{
	unsigned int i;

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
	struct drm_v3d_sem __user *post_deps;
	int i, ret;

	if (!count)
		return 0;

	se->out_syncs = (struct v3d_submit_outsync *)
			kvmalloc_array(count,
				       sizeof(struct v3d_submit_outsync),
				       GFP_KERNEL);
	if (!se->out_syncs)
		return -ENOMEM;

	post_deps = u64_to_user_ptr(handles);

	for (i = 0; i < count; i++) {
		struct drm_v3d_sem out;

		if (copy_from_user(&out, post_deps++, sizeof(out))) {
			ret = -EFAULT;
			DRM_DEBUG("Failed to copy post dep handles\n");
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
	struct drm_v3d_multi_sync multisync;
	int ret;

	if (se->in_sync_count || se->out_sync_count) {
		DRM_DEBUG("Two multisync extensions were added to the same job.");
		return -EINVAL;
	}

	if (copy_from_user(&multisync, ext, sizeof(multisync)))
		return -EFAULT;

	if (multisync.pad)
		return -EINVAL;

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

	if (!job) {
		DRM_DEBUG("CPU job extension was attached to a GPU job.\n");
		return -EINVAL;
	}

	if (job->job_type) {
		DRM_DEBUG("Two CPU job extensions were added to the same CPU job.\n");
		return -EINVAL;
	}

	if (copy_from_user(&indirect_csd, ext, sizeof(indirect_csd)))
		return -EFAULT;

	if (!v3d_has_csd(v3d)) {
		DRM_DEBUG("Attempting CSD submit on non-CSD hardware.\n");
		return -EINVAL;
	}

	job->job_type = V3D_CPU_JOB_TYPE_INDIRECT_CSD;
	info->offset = indirect_csd.offset;
	info->wg_size = indirect_csd.wg_size;
	memcpy(&info->wg_uniform_offsets, &indirect_csd.wg_uniform_offsets,
	       sizeof(indirect_csd.wg_uniform_offsets));

	info->indirect = drm_gem_object_lookup(file_priv, indirect_csd.indirect);

	return v3d_setup_csd_jobs_and_bos(file_priv, v3d, &indirect_csd.submit,
					  &info->job, &info->clean_job,
					  NULL, &info->acquire_ctx);
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

	if (!job) {
		DRM_DEBUG("CPU job extension was attached to a GPU job.\n");
		return -EINVAL;
	}

	if (job->job_type) {
		DRM_DEBUG("Two CPU job extensions were added to the same CPU job.\n");
		return -EINVAL;
	}

	if (copy_from_user(&timestamp, ext, sizeof(timestamp)))
		return -EFAULT;

	if (timestamp.pad)
		return -EINVAL;

	job->job_type = V3D_CPU_JOB_TYPE_TIMESTAMP_QUERY;

	query_info->queries = kvmalloc_array(timestamp.count,
					     sizeof(struct v3d_timestamp_query),
					     GFP_KERNEL);
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

	if (!job) {
		DRM_DEBUG("CPU job extension was attached to a GPU job.\n");
		return -EINVAL;
	}

	if (job->job_type) {
		DRM_DEBUG("Two CPU job extensions were added to the same CPU job.\n");
		return -EINVAL;
	}

	if (copy_from_user(&reset, ext, sizeof(reset)))
		return -EFAULT;

	job->job_type = V3D_CPU_JOB_TYPE_RESET_TIMESTAMP_QUERY;

	query_info->queries = kvmalloc_array(reset.count,
					     sizeof(struct v3d_timestamp_query),
					     GFP_KERNEL);
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

	if (!job) {
		DRM_DEBUG("CPU job extension was attached to a GPU job.\n");
		return -EINVAL;
	}

	if (job->job_type) {
		DRM_DEBUG("Two CPU job extensions were added to the same CPU job.\n");
		return -EINVAL;
	}

	if (copy_from_user(&copy, ext, sizeof(copy)))
		return -EFAULT;

	if (copy.pad)
		return -EINVAL;

	job->job_type = V3D_CPU_JOB_TYPE_COPY_TIMESTAMP_QUERY;

	query_info->queries = kvmalloc_array(copy.count,
					     sizeof(struct v3d_timestamp_query),
					     GFP_KERNEL);
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

	if (!job) {
		DRM_DEBUG("CPU job extension was attached to a GPU job.\n");
		return -EINVAL;
	}

	if (job->job_type) {
		DRM_DEBUG("Two CPU job extensions were added to the same CPU job.\n");
		return -EINVAL;
	}

	if (copy_from_user(&reset, ext, sizeof(reset)))
		return -EFAULT;

	job->job_type = V3D_CPU_JOB_TYPE_RESET_PERFORMANCE_QUERY;

	query_info->queries =
		kvmalloc_array(reset.count,
			       sizeof(struct v3d_performance_query),
			       GFP_KERNEL);
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

	if (!job) {
		DRM_DEBUG("CPU job extension was attached to a GPU job.\n");
		return -EINVAL;
	}

	if (job->job_type) {
		DRM_DEBUG("Two CPU job extensions were added to the same CPU job.\n");
		return -EINVAL;
	}

	if (copy_from_user(&copy, ext, sizeof(copy)))
		return -EFAULT;

	if (copy.pad)
		return -EINVAL;

	job->job_type = V3D_CPU_JOB_TYPE_COPY_PERFORMANCE_QUERY;

	query_info->queries =
		kvmalloc_array(copy.count,
			       sizeof(struct v3d_performance_query),
			       GFP_KERNEL);
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
	struct drm_v3d_extension __user *user_ext;
	int ret;

	user_ext = u64_to_user_ptr(ext_handles);
	while (user_ext) {
		struct drm_v3d_extension ext;

		if (copy_from_user(&ext, user_ext, sizeof(ext))) {
			DRM_DEBUG("Failed to copy submit extension\n");
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
			DRM_DEBUG_DRIVER("Unknown extension id: %d\n", ext.id);
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
	struct v3d_dev *v3d = to_v3d_dev(dev);
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct drm_v3d_submit_cl *args = data;
	struct v3d_submit_ext se = {0};
	struct v3d_bin_job *bin = NULL;
	struct v3d_render_job *render = NULL;
	struct v3d_job *clean_job = NULL;
	struct v3d_job *last_job;
	struct ww_acquire_ctx acquire_ctx;
	int ret = 0;

	trace_v3d_submit_cl_ioctl(&v3d->drm, args->rcl_start, args->rcl_end);

	if (args->pad)
		return -EINVAL;

	if (args->flags &&
	    args->flags & ~(DRM_V3D_SUBMIT_CL_FLUSH_CACHE |
			    DRM_V3D_SUBMIT_EXTENSION)) {
		DRM_INFO("invalid flags: %d\n", args->flags);
		return -EINVAL;
	}

	if (args->flags & DRM_V3D_SUBMIT_EXTENSION) {
		ret = v3d_get_extensions(file_priv, args->extensions, &se, NULL);
		if (ret) {
			DRM_DEBUG("Failed to get extensions.\n");
			return ret;
		}
	}

	ret = v3d_job_allocate((void *)&render, sizeof(*render));
	if (ret)
		return ret;

	ret = v3d_job_init(v3d, file_priv, &render->base,
			   v3d_render_job_free, args->in_sync_rcl, &se, V3D_RENDER);
	if (ret) {
		v3d_job_deallocate((void *)&render);
		goto fail;
	}

	render->start = args->rcl_start;
	render->end = args->rcl_end;
	INIT_LIST_HEAD(&render->unref_list);

	if (args->bcl_start != args->bcl_end) {
		ret = v3d_job_allocate((void *)&bin, sizeof(*bin));
		if (ret)
			goto fail;

		ret = v3d_job_init(v3d, file_priv, &bin->base,
				   v3d_job_free, args->in_sync_bcl, &se, V3D_BIN);
		if (ret) {
			v3d_job_deallocate((void *)&bin);
			goto fail;
		}

		bin->start = args->bcl_start;
		bin->end = args->bcl_end;
		bin->qma = args->qma;
		bin->qms = args->qms;
		bin->qts = args->qts;
		bin->render = render;
	}

	if (args->flags & DRM_V3D_SUBMIT_CL_FLUSH_CACHE) {
		ret = v3d_job_allocate((void *)&clean_job, sizeof(*clean_job));
		if (ret)
			goto fail;

		ret = v3d_job_init(v3d, file_priv, clean_job,
				   v3d_job_free, 0, NULL, V3D_CACHE_CLEAN);
		if (ret) {
			v3d_job_deallocate((void *)&clean_job);
			goto fail;
		}

		last_job = clean_job;
	} else {
		last_job = &render->base;
	}

	ret = v3d_lookup_bos(dev, file_priv, last_job,
			     args->bo_handles, args->bo_handle_count);
	if (ret)
		goto fail;

	ret = v3d_lock_bo_reservations(last_job, &acquire_ctx);
	if (ret)
		goto fail;

	if (args->perfmon_id) {
		if (v3d->global_perfmon) {
			ret = -EAGAIN;
			goto fail_perfmon;
		}

		render->base.perfmon = v3d_perfmon_find(v3d_priv,
							args->perfmon_id);

		if (!render->base.perfmon) {
			ret = -ENOENT;
			goto fail_perfmon;
		}
	}

	mutex_lock(&v3d->sched_lock);
	if (bin) {
		bin->base.perfmon = render->base.perfmon;
		v3d_perfmon_get(bin->base.perfmon);
		v3d_push_job(&bin->base);

		ret = drm_sched_job_add_dependency(&render->base.base,
						   dma_fence_get(bin->base.done_fence));
		if (ret)
			goto fail_unreserve;
	}

	v3d_push_job(&render->base);

	if (clean_job) {
		struct dma_fence *render_fence =
			dma_fence_get(render->base.done_fence);
		ret = drm_sched_job_add_dependency(&clean_job->base,
						   render_fence);
		if (ret)
			goto fail_unreserve;
		clean_job->perfmon = render->base.perfmon;
		v3d_perfmon_get(clean_job->perfmon);
		v3d_push_job(clean_job);
	}

	mutex_unlock(&v3d->sched_lock);

	v3d_attach_fences_and_unlock_reservation(file_priv,
						 last_job,
						 &acquire_ctx,
						 args->out_sync,
						 &se,
						 last_job->done_fence);

	v3d_job_put(&bin->base);
	v3d_job_put(&render->base);
	v3d_job_put(clean_job);

	return 0;

fail_unreserve:
	mutex_unlock(&v3d->sched_lock);
fail_perfmon:
	drm_gem_unlock_reservations(last_job->bo,
				    last_job->bo_count, &acquire_ctx);
fail:
	v3d_job_cleanup((void *)bin);
	v3d_job_cleanup((void *)render);
	v3d_job_cleanup(clean_job);
	v3d_put_multisync_post_deps(&se);

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
	struct v3d_dev *v3d = to_v3d_dev(dev);
	struct drm_v3d_submit_tfu *args = data;
	struct v3d_submit_ext se = {0};
	struct v3d_tfu_job *job = NULL;
	struct ww_acquire_ctx acquire_ctx;
	int ret = 0;

	trace_v3d_submit_tfu_ioctl(&v3d->drm, args->iia);

	if (args->flags && !(args->flags & DRM_V3D_SUBMIT_EXTENSION)) {
		DRM_DEBUG("invalid flags: %d\n", args->flags);
		return -EINVAL;
	}

	if (args->flags & DRM_V3D_SUBMIT_EXTENSION) {
		ret = v3d_get_extensions(file_priv, args->extensions, &se, NULL);
		if (ret) {
			DRM_DEBUG("Failed to get extensions.\n");
			return ret;
		}
	}

	ret = v3d_job_allocate((void *)&job, sizeof(*job));
	if (ret)
		return ret;

	ret = v3d_job_init(v3d, file_priv, &job->base,
			   v3d_job_free, args->in_sync, &se, V3D_TFU);
	if (ret) {
		v3d_job_deallocate((void *)&job);
		goto fail;
	}

	job->base.bo = kcalloc(ARRAY_SIZE(args->bo_handles),
			       sizeof(*job->base.bo), GFP_KERNEL);
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
			DRM_DEBUG("Failed to look up GEM BO %d: %d\n",
				  job->base.bo_count,
				  args->bo_handles[job->base.bo_count]);
			ret = -ENOENT;
			goto fail;
		}
		job->base.bo[job->base.bo_count] = bo;
	}

	ret = v3d_lock_bo_reservations(&job->base, &acquire_ctx);
	if (ret)
		goto fail;

	mutex_lock(&v3d->sched_lock);
	v3d_push_job(&job->base);
	mutex_unlock(&v3d->sched_lock);

	v3d_attach_fences_and_unlock_reservation(file_priv,
						 &job->base, &acquire_ctx,
						 args->out_sync,
						 &se,
						 job->base.done_fence);

	v3d_job_put(&job->base);

	return 0;

fail:
	v3d_job_cleanup((void *)job);
	v3d_put_multisync_post_deps(&se);

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
	struct v3d_dev *v3d = to_v3d_dev(dev);
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct drm_v3d_submit_csd *args = data;
	struct v3d_submit_ext se = {0};
	struct v3d_csd_job *job = NULL;
	struct v3d_job *clean_job = NULL;
	struct ww_acquire_ctx acquire_ctx;
	int ret;

	trace_v3d_submit_csd_ioctl(&v3d->drm, args->cfg[5], args->cfg[6]);

	if (args->pad)
		return -EINVAL;

	if (!v3d_has_csd(v3d)) {
		DRM_DEBUG("Attempting CSD submit on non-CSD hardware\n");
		return -EINVAL;
	}

	if (args->flags && !(args->flags & DRM_V3D_SUBMIT_EXTENSION)) {
		DRM_INFO("invalid flags: %d\n", args->flags);
		return -EINVAL;
	}

	if (args->flags & DRM_V3D_SUBMIT_EXTENSION) {
		ret = v3d_get_extensions(file_priv, args->extensions, &se, NULL);
		if (ret) {
			DRM_DEBUG("Failed to get extensions.\n");
			return ret;
		}
	}

	ret = v3d_setup_csd_jobs_and_bos(file_priv, v3d, args,
					 &job, &clean_job, &se,
					 &acquire_ctx);
	if (ret)
		goto fail;

	if (args->perfmon_id) {
		if (v3d->global_perfmon) {
			ret = -EAGAIN;
			goto fail_perfmon;
		}

		job->base.perfmon = v3d_perfmon_find(v3d_priv,
						     args->perfmon_id);
		if (!job->base.perfmon) {
			ret = -ENOENT;
			goto fail_perfmon;
		}
	}

	mutex_lock(&v3d->sched_lock);
	v3d_push_job(&job->base);

	ret = drm_sched_job_add_dependency(&clean_job->base,
					   dma_fence_get(job->base.done_fence));
	if (ret)
		goto fail_unreserve;

	v3d_push_job(clean_job);
	mutex_unlock(&v3d->sched_lock);

	v3d_attach_fences_and_unlock_reservation(file_priv,
						 clean_job,
						 &acquire_ctx,
						 args->out_sync,
						 &se,
						 clean_job->done_fence);

	v3d_job_put(&job->base);
	v3d_job_put(clean_job);

	return 0;

fail_unreserve:
	mutex_unlock(&v3d->sched_lock);
fail_perfmon:
	drm_gem_unlock_reservations(clean_job->bo, clean_job->bo_count,
				    &acquire_ctx);
fail:
	v3d_job_cleanup((void *)job);
	v3d_job_cleanup(clean_job);
	v3d_put_multisync_post_deps(&se);

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
	struct v3d_dev *v3d = to_v3d_dev(dev);
	struct drm_v3d_submit_cpu *args = data;
	struct v3d_submit_ext se = {0};
	struct v3d_submit_ext *out_se = NULL;
	struct v3d_cpu_job *cpu_job = NULL;
	struct v3d_csd_job *csd_job = NULL;
	struct v3d_job *clean_job = NULL;
	struct ww_acquire_ctx acquire_ctx;
	int ret;

	if (args->flags && !(args->flags & DRM_V3D_SUBMIT_EXTENSION)) {
		DRM_INFO("Invalid flags: %d\n", args->flags);
		return -EINVAL;
	}

	ret = v3d_job_allocate((void *)&cpu_job, sizeof(*cpu_job));
	if (ret)
		return ret;

	if (args->flags & DRM_V3D_SUBMIT_EXTENSION) {
		ret = v3d_get_extensions(file_priv, args->extensions, &se, cpu_job);
		if (ret) {
			DRM_DEBUG("Failed to get extensions.\n");
			goto fail;
		}
	}

	/* Every CPU job must have a CPU job user extension */
	if (!cpu_job->job_type) {
		DRM_DEBUG("CPU job must have a CPU job user extension.\n");
		ret = -EINVAL;
		goto fail;
	}

	if (args->bo_handle_count != cpu_job_bo_handle_count[cpu_job->job_type]) {
		DRM_DEBUG("This CPU job was not submitted with the proper number of BOs.\n");
		ret = -EINVAL;
		goto fail;
	}

	trace_v3d_submit_cpu_ioctl(&v3d->drm, cpu_job->job_type);

	ret = v3d_job_init(v3d, file_priv, &cpu_job->base,
			   v3d_job_free, 0, &se, V3D_CPU);
	if (ret) {
		v3d_job_deallocate((void *)&cpu_job);
		goto fail;
	}

	clean_job = cpu_job->indirect_csd.clean_job;
	csd_job = cpu_job->indirect_csd.job;

	if (args->bo_handle_count) {
		ret = v3d_lookup_bos(dev, file_priv, &cpu_job->base,
				     args->bo_handles, args->bo_handle_count);
		if (ret)
			goto fail;

		ret = v3d_lock_bo_reservations(&cpu_job->base, &acquire_ctx);
		if (ret)
			goto fail;
	}

	mutex_lock(&v3d->sched_lock);
	v3d_push_job(&cpu_job->base);

	switch (cpu_job->job_type) {
	case V3D_CPU_JOB_TYPE_INDIRECT_CSD:
		ret = drm_sched_job_add_dependency(&csd_job->base.base,
						   dma_fence_get(cpu_job->base.done_fence));
		if (ret)
			goto fail_unreserve;

		v3d_push_job(&csd_job->base);

		ret = drm_sched_job_add_dependency(&clean_job->base,
						   dma_fence_get(csd_job->base.done_fence));
		if (ret)
			goto fail_unreserve;

		v3d_push_job(clean_job);

		break;
	default:
		break;
	}
	mutex_unlock(&v3d->sched_lock);

	out_se = (cpu_job->job_type == V3D_CPU_JOB_TYPE_INDIRECT_CSD) ? NULL : &se;

	v3d_attach_fences_and_unlock_reservation(file_priv,
						 &cpu_job->base,
						 &acquire_ctx, 0,
						 out_se, cpu_job->base.done_fence);

	switch (cpu_job->job_type) {
	case V3D_CPU_JOB_TYPE_INDIRECT_CSD:
		v3d_attach_fences_and_unlock_reservation(file_priv,
							 clean_job,
							 &cpu_job->indirect_csd.acquire_ctx,
							 0, &se, clean_job->done_fence);
		break;
	default:
		break;
	}

	v3d_job_put(&cpu_job->base);
	v3d_job_put(&csd_job->base);
	v3d_job_put(clean_job);

	return 0;

fail_unreserve:
	mutex_unlock(&v3d->sched_lock);

	drm_gem_unlock_reservations(cpu_job->base.bo, cpu_job->base.bo_count,
				    &acquire_ctx);

	drm_gem_unlock_reservations(clean_job->bo, clean_job->bo_count,
				    &cpu_job->indirect_csd.acquire_ctx);

fail:
	v3d_job_cleanup((void *)cpu_job);
	v3d_job_cleanup((void *)csd_job);
	v3d_job_cleanup(clean_job);
	v3d_put_multisync_post_deps(&se);
	kvfree(cpu_job->timestamp_query.queries);
	kvfree(cpu_job->performance_query.queries);

	return ret;
}
