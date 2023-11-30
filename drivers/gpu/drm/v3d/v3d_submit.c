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
			      void *data)
{
	struct drm_v3d_multi_sync multisync;
	struct v3d_submit_ext *se = data;
	int ret;

	if (se->in_sync_count || se->out_sync_count) {
		DRM_DEBUG("Two multisync extensions were added to the same job.");
		return -EINVAL;
	}

	if (copy_from_user(&multisync, ext, sizeof(multisync)))
		return -EFAULT;

	if (multisync.pad)
		return -EINVAL;

	ret = v3d_get_multisync_post_deps(file_priv, data, multisync.out_sync_count,
					  multisync.out_syncs);
	if (ret)
		return ret;

	se->in_sync_count = multisync.in_sync_count;
	se->in_syncs = multisync.in_syncs;
	se->flags |= DRM_V3D_EXT_ID_MULTI_SYNC;
	se->wait_stage = multisync.wait_stage;

	return 0;
}

/* Whenever userspace sets ioctl extensions, v3d_get_extensions parses data
 * according to the extension id (name).
 */
static int
v3d_get_extensions(struct drm_file *file_priv,
		   u64 ext_handles,
		   void *data)
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
			ret = v3d_get_multisync_submit_deps(file_priv, user_ext, data);
			if (ret)
				return ret;
			break;
		default:
			DRM_DEBUG_DRIVER("Unknown extension id: %d\n", ext.id);
			return -EINVAL;
		}

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
		ret = v3d_get_extensions(file_priv, args->extensions, &se);
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
	if (ret)
		goto fail;

	render->start = args->rcl_start;
	render->end = args->rcl_end;
	INIT_LIST_HEAD(&render->unref_list);

	if (args->bcl_start != args->bcl_end) {
		ret = v3d_job_allocate((void *)&bin, sizeof(*bin));
		if (ret)
			goto fail;

		ret = v3d_job_init(v3d, file_priv, &bin->base,
				   v3d_job_free, args->in_sync_bcl, &se, V3D_BIN);
		if (ret)
			goto fail;

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
		if (ret)
			goto fail;

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
		ret = v3d_get_extensions(file_priv, args->extensions, &se);
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
	if (ret)
		goto fail;

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
		ret = v3d_get_extensions(file_priv, args->extensions, &se);
		if (ret) {
			DRM_DEBUG("Failed to get extensions.\n");
			return ret;
		}
	}

	ret = v3d_job_allocate((void *)&job, sizeof(*job));
	if (ret)
		return ret;

	ret = v3d_job_init(v3d, file_priv, &job->base,
			   v3d_job_free, args->in_sync, &se, V3D_CSD);
	if (ret)
		goto fail;

	ret = v3d_job_allocate((void *)&clean_job, sizeof(*clean_job));
	if (ret)
		goto fail;

	ret = v3d_job_init(v3d, file_priv, clean_job,
			   v3d_job_free, 0, NULL, V3D_CACHE_CLEAN);
	if (ret)
		goto fail;

	job->args = *args;

	ret = v3d_lookup_bos(dev, file_priv, clean_job,
			     args->bo_handles, args->bo_handle_count);
	if (ret)
		goto fail;

	ret = v3d_lock_bo_reservations(clean_job, &acquire_ctx);
	if (ret)
		goto fail;

	if (args->perfmon_id) {
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
