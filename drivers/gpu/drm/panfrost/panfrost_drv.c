// SPDX-License-Identifier: GPL-2.0
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Linaro, Ltd., Rob Herring <robh@kernel.org> */
/* Copyright 2019 Collabora ltd. */

#ifdef CONFIG_ARM_ARCH_TIMER
#include <asm/arch_timer.h>
#endif

#include <linux/module.h>
#include <linux/of.h>
#include <linux/pagemap.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <drm/panfrost_drm.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_print.h>
#include <drm/drm_syncobj.h>
#include <drm/drm_utils.h>

#include "panfrost_device.h"
#include "panfrost_gem.h"
#include "panfrost_mmu.h"
#include "panfrost_job.h"
#include "panfrost_gpu.h"
#include "panfrost_perfcnt.h"

#define JOB_REQUIREMENTS (PANFROST_JD_REQ_FS | PANFROST_JD_REQ_CYCLE_COUNT)

static bool unstable_ioctls;
module_param_unsafe(unstable_ioctls, bool, 0600);

static int panfrost_ioctl_query_timestamp(struct panfrost_device *pfdev,
					  u64 *arg)
{
	int ret;

	ret = pm_runtime_resume_and_get(pfdev->base.dev);
	if (ret)
		return ret;

	panfrost_cycle_counter_get(pfdev);
	*arg = panfrost_timestamp_read(pfdev);
	panfrost_cycle_counter_put(pfdev);

	pm_runtime_put(pfdev->base.dev);
	return 0;
}

static int panfrost_ioctl_get_param(struct drm_device *ddev, void *data, struct drm_file *file)
{
	struct drm_panfrost_get_param *param = data;
	struct panfrost_device *pfdev = to_panfrost_device(ddev);
	int ret;

	if (param->pad != 0)
		return -EINVAL;

#define PANFROST_FEATURE(name, member)			\
	case DRM_PANFROST_PARAM_ ## name:		\
		param->value = pfdev->features.member;	\
		break
#define PANFROST_FEATURE_ARRAY(name, member, max)			\
	case DRM_PANFROST_PARAM_ ## name ## 0 ...			\
		DRM_PANFROST_PARAM_ ## name ## max:			\
		param->value = pfdev->features.member[param->param -	\
			DRM_PANFROST_PARAM_ ## name ## 0];		\
		break

	switch (param->param) {
		PANFROST_FEATURE(GPU_PROD_ID, id);
		PANFROST_FEATURE(GPU_REVISION, revision);
		PANFROST_FEATURE(SHADER_PRESENT, shader_present);
		PANFROST_FEATURE(TILER_PRESENT, tiler_present);
		PANFROST_FEATURE(L2_PRESENT, l2_present);
		PANFROST_FEATURE(STACK_PRESENT, stack_present);
		PANFROST_FEATURE(AS_PRESENT, as_present);
		PANFROST_FEATURE(JS_PRESENT, js_present);
		PANFROST_FEATURE(L2_FEATURES, l2_features);
		PANFROST_FEATURE(CORE_FEATURES, core_features);
		PANFROST_FEATURE(TILER_FEATURES, tiler_features);
		PANFROST_FEATURE(MEM_FEATURES, mem_features);
		PANFROST_FEATURE(MMU_FEATURES, mmu_features);
		PANFROST_FEATURE(THREAD_FEATURES, thread_features);
		PANFROST_FEATURE(MAX_THREADS, max_threads);
		PANFROST_FEATURE(THREAD_MAX_WORKGROUP_SZ,
				thread_max_workgroup_sz);
		PANFROST_FEATURE(THREAD_MAX_BARRIER_SZ,
				thread_max_barrier_sz);
		PANFROST_FEATURE(COHERENCY_FEATURES, coherency_features);
		PANFROST_FEATURE(AFBC_FEATURES, afbc_features);
		PANFROST_FEATURE_ARRAY(TEXTURE_FEATURES, texture_features, 3);
		PANFROST_FEATURE_ARRAY(JS_FEATURES, js_features, 15);
		PANFROST_FEATURE(NR_CORE_GROUPS, nr_core_groups);
		PANFROST_FEATURE(THREAD_TLS_ALLOC, thread_tls_alloc);
		PANFROST_FEATURE(SELECTED_COHERENCY, selected_coherency);

	case DRM_PANFROST_PARAM_SYSTEM_TIMESTAMP:
		ret = panfrost_ioctl_query_timestamp(pfdev, &param->value);
		if (ret)
			return ret;
		break;

	case DRM_PANFROST_PARAM_SYSTEM_TIMESTAMP_FREQUENCY:
#ifdef CONFIG_ARM_ARCH_TIMER
		param->value = arch_timer_get_cntfrq();
#else
		param->value = 0;
#endif
		break;

	case DRM_PANFROST_PARAM_ALLOWED_JM_CTX_PRIORITIES:
		param->value = BIT(PANFROST_JM_CTX_PRIORITY_LOW) |
			       BIT(PANFROST_JM_CTX_PRIORITY_MEDIUM);

		if (panfrost_high_prio_allowed(file))
			param->value |= BIT(PANFROST_JM_CTX_PRIORITY_HIGH);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

#define PANFROST_BO_FLAGS	(PANFROST_BO_NOEXEC | \
				 PANFROST_BO_HEAP | \
				 PANFROST_BO_WB_MMAP)

static int panfrost_ioctl_create_bo(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct panfrost_file_priv *priv = file->driver_priv;
	struct panfrost_gem_object *bo;
	struct drm_panfrost_create_bo *args = data;
	struct panfrost_gem_mapping *mapping;
	int ret;

	if (!args->size || args->pad || (args->flags & ~PANFROST_BO_FLAGS))
		return -EINVAL;

	/* Heaps should never be executable */
	if ((args->flags & PANFROST_BO_HEAP) &&
	    !(args->flags & PANFROST_BO_NOEXEC))
		return -EINVAL;

	bo = panfrost_gem_create(dev, args->size, args->flags);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	ret = drm_gem_handle_create(file, &bo->base.base, &args->handle);
	if (ret)
		goto out;

	mapping = panfrost_gem_mapping_get(bo, priv);
	if (mapping) {
		args->offset = mapping->mmnode.start << PAGE_SHIFT;
		panfrost_gem_mapping_put(mapping);
	} else {
		/* This can only happen if the handle from
		 * drm_gem_handle_create() has already been guessed and freed
		 * by user space
		 */
		ret = -EINVAL;
	}

out:
	drm_gem_object_put(&bo->base.base);
	return ret;
}

/**
 * panfrost_lookup_bos() - Sets up job->bo[] with the GEM objects
 * referenced by the job.
 * @dev: DRM device
 * @file_priv: DRM file for this fd
 * @args: IOCTL args
 * @job: job being set up
 *
 * Resolve handles from userspace to BOs and attach them to job.
 *
 * Note that this function doesn't need to unreference the BOs on
 * failure, because that will happen at panfrost_job_cleanup() time.
 */
static int
panfrost_lookup_bos(struct drm_device *dev,
		  struct drm_file *file_priv,
		  struct drm_panfrost_submit *args,
		  struct panfrost_job *job)
{
	struct panfrost_file_priv *priv = file_priv->driver_priv;
	struct panfrost_gem_object *bo;
	unsigned int i;
	int ret;

	job->bo_count = args->bo_handle_count;

	if (!job->bo_count)
		return 0;

	ret = drm_gem_objects_lookup(file_priv,
				     (void __user *)(uintptr_t)args->bo_handles,
				     job->bo_count, &job->bos);
	if (ret)
		return ret;

	job->mappings = kvmalloc_array(job->bo_count,
				       sizeof(struct panfrost_gem_mapping *),
				       GFP_KERNEL | __GFP_ZERO);
	if (!job->mappings)
		return -ENOMEM;

	for (i = 0; i < job->bo_count; i++) {
		struct panfrost_gem_mapping *mapping;

		bo = to_panfrost_bo(job->bos[i]);
		mapping = panfrost_gem_mapping_get(bo, priv);
		if (!mapping) {
			ret = -EINVAL;
			break;
		}

		atomic_inc(&bo->gpu_usecount);
		job->mappings[i] = mapping;
	}

	return ret;
}

/**
 * panfrost_copy_in_sync() - Sets up job->deps with the sync objects
 * referenced by the job.
 * @dev: DRM device
 * @file_priv: DRM file for this fd
 * @args: IOCTL args
 * @job: job being set up
 *
 * Resolve syncobjs from userspace to fences and attach them to job.
 *
 * Note that this function doesn't need to unreference the fences on
 * failure, because that will happen at panfrost_job_cleanup() time.
 */
static int
panfrost_copy_in_sync(struct drm_device *dev,
		  struct drm_file *file_priv,
		  struct drm_panfrost_submit *args,
		  struct panfrost_job *job)
{
	u32 *handles;
	int ret = 0;
	int i, in_fence_count;

	in_fence_count = args->in_sync_count;

	if (!in_fence_count)
		return 0;

	handles = kvmalloc_array(in_fence_count, sizeof(u32), GFP_KERNEL);
	if (!handles) {
		ret = -ENOMEM;
		DRM_DEBUG("Failed to allocate incoming syncobj handles\n");
		goto fail;
	}

	if (copy_from_user(handles,
			   (void __user *)(uintptr_t)args->in_syncs,
			   in_fence_count * sizeof(u32))) {
		ret = -EFAULT;
		DRM_DEBUG("Failed to copy in syncobj handles\n");
		goto fail;
	}

	for (i = 0; i < in_fence_count; i++) {
		ret = drm_sched_job_add_syncobj_dependency(&job->base, file_priv,
							   handles[i], 0);
		if (ret)
			goto fail;
	}

fail:
	kvfree(handles);
	return ret;
}

static int panfrost_ioctl_submit(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct panfrost_device *pfdev = to_panfrost_device(dev);
	struct panfrost_file_priv *file_priv = file->driver_priv;
	struct drm_panfrost_submit *args = data;
	struct drm_syncobj *sync_out = NULL;
	struct panfrost_jm_ctx *jm_ctx;
	struct panfrost_job *job;
	int ret = 0, slot;

	if (args->pad)
		return -EINVAL;

	if (!args->jc)
		return -EINVAL;

	if (args->requirements & ~JOB_REQUIREMENTS)
		return -EINVAL;

	if (args->out_sync > 0) {
		sync_out = drm_syncobj_find(file, args->out_sync);
		if (!sync_out)
			return -ENODEV;
	}

	jm_ctx = panfrost_jm_ctx_from_handle(file, args->jm_ctx_handle);
	if (!jm_ctx) {
		ret = -EINVAL;
		goto out_put_syncout;
	}

	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job) {
		ret = -ENOMEM;
		goto out_put_jm_ctx;
	}

	kref_init(&job->refcount);

	job->pfdev = pfdev;
	job->jc = args->jc;
	job->requirements = args->requirements;
	job->flush_id = panfrost_gpu_get_latest_flush_id(pfdev);
	job->mmu = file_priv->mmu;
	job->ctx = panfrost_jm_ctx_get(jm_ctx);
	job->engine_usage = &file_priv->engine_usage;

	slot = panfrost_job_get_slot(job);

	ret = drm_sched_job_init(&job->base,
				 &jm_ctx->slot_entity[slot],
				 1, NULL, file->client_id);
	if (ret)
		goto out_put_job;

	ret = panfrost_copy_in_sync(dev, file, args, job);
	if (ret)
		goto out_cleanup_job;

	ret = panfrost_lookup_bos(dev, file, args, job);
	if (ret)
		goto out_cleanup_job;

	ret = panfrost_job_push(job);
	if (ret)
		goto out_cleanup_job;

	/* Update the return sync object for the job */
	if (sync_out)
		drm_syncobj_replace_fence(sync_out, job->render_done_fence);

out_cleanup_job:
	if (ret)
		drm_sched_job_cleanup(&job->base);
out_put_job:
	panfrost_job_put(job);
out_put_jm_ctx:
	panfrost_jm_ctx_put(jm_ctx);
out_put_syncout:
	if (sync_out)
		drm_syncobj_put(sync_out);

	return ret;
}

static int
panfrost_ioctl_wait_bo(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	long ret;
	struct drm_panfrost_wait_bo *args = data;
	struct drm_gem_object *gem_obj;
	unsigned long timeout = drm_timeout_abs_to_jiffies(args->timeout_ns);

	if (args->pad)
		return -EINVAL;

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj)
		return -ENOENT;

	ret = dma_resv_wait_timeout(gem_obj->resv, DMA_RESV_USAGE_READ,
				    true, timeout);
	if (!ret)
		ret = timeout ? -ETIMEDOUT : -EBUSY;

	drm_gem_object_put(gem_obj);

	return ret;
}

static int panfrost_ioctl_mmap_bo(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_panfrost_mmap_bo *args = data;
	struct drm_gem_object *gem_obj;
	int ret;

	if (args->flags != 0) {
		DRM_INFO("unknown mmap_bo flags: %d\n", args->flags);
		return -EINVAL;
	}

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", args->handle);
		return -ENOENT;
	}

	/* Don't allow mmapping of heap objects as pages are not pinned. */
	if (to_panfrost_bo(gem_obj)->is_heap) {
		ret = -EINVAL;
		goto out;
	}

	ret = drm_gem_create_mmap_offset(gem_obj);
	if (ret == 0)
		args->offset = drm_vma_node_offset_addr(&gem_obj->vma_node);

out:
	drm_gem_object_put(gem_obj);
	return ret;
}

static int panfrost_ioctl_get_bo_offset(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	struct panfrost_file_priv *priv = file_priv->driver_priv;
	struct drm_panfrost_get_bo_offset *args = data;
	struct panfrost_gem_mapping *mapping;
	struct drm_gem_object *gem_obj;
	struct panfrost_gem_object *bo;

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", args->handle);
		return -ENOENT;
	}
	bo = to_panfrost_bo(gem_obj);

	mapping = panfrost_gem_mapping_get(bo, priv);
	drm_gem_object_put(gem_obj);

	if (!mapping)
		return -EINVAL;

	args->offset = mapping->mmnode.start << PAGE_SHIFT;
	panfrost_gem_mapping_put(mapping);
	return 0;
}

static int panfrost_ioctl_madvise(struct drm_device *dev, void *data,
				  struct drm_file *file_priv)
{
	struct panfrost_file_priv *priv = file_priv->driver_priv;
	struct drm_panfrost_madvise *args = data;
	struct panfrost_device *pfdev = to_panfrost_device(dev);
	struct drm_gem_object *gem_obj;
	struct panfrost_gem_object *bo;
	int ret = 0;

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", args->handle);
		return -ENOENT;
	}

	bo = to_panfrost_bo(gem_obj);

	ret = dma_resv_lock_interruptible(bo->base.base.resv, NULL);
	if (ret)
		goto out_put_object;

	mutex_lock(&pfdev->shrinker_lock);
	mutex_lock(&bo->mappings.lock);
	if (args->madv == PANFROST_MADV_DONTNEED) {
		struct panfrost_gem_mapping *first;

		first = list_first_entry(&bo->mappings.list,
					 struct panfrost_gem_mapping,
					 node);

		/*
		 * If we want to mark the BO purgeable, there must be only one
		 * user: the caller FD.
		 * We could do something smarter and mark the BO purgeable only
		 * when all its users have marked it purgeable, but globally
		 * visible/shared BOs are likely to never be marked purgeable
		 * anyway, so let's not bother.
		 */
		if (!list_is_singular(&bo->mappings.list) ||
		    WARN_ON_ONCE(first->mmu != priv->mmu)) {
			ret = -EINVAL;
			goto out_unlock_mappings;
		}
	}

	args->retained = drm_gem_shmem_madvise_locked(&bo->base, args->madv);

	if (args->retained) {
		if (args->madv == PANFROST_MADV_DONTNEED)
			list_move_tail(&bo->base.madv_list,
				       &pfdev->shrinker_list);
		else if (args->madv == PANFROST_MADV_WILLNEED)
			list_del_init(&bo->base.madv_list);
	}

out_unlock_mappings:
	mutex_unlock(&bo->mappings.lock);
	mutex_unlock(&pfdev->shrinker_lock);
	dma_resv_unlock(bo->base.base.resv);
out_put_object:
	drm_gem_object_put(gem_obj);
	return ret;
}

static int panfrost_ioctl_set_label_bo(struct drm_device *ddev, void *data,
				       struct drm_file *file)
{
	struct drm_panfrost_set_label_bo *args = data;
	struct drm_gem_object *obj;
	const char *label = NULL;
	int ret = 0;

	if (args->pad)
		return -EINVAL;

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	if (args->label) {
		label = strndup_user(u64_to_user_ptr(args->label),
				     PANFROST_BO_LABEL_MAXLEN);
		if (IS_ERR(label)) {
			ret = PTR_ERR(label);
			if (ret == -EINVAL)
				ret = -E2BIG;
			goto err_put_obj;
		}
	}

	/*
	 * We treat passing a label of length 0 and passing a NULL label
	 * differently, because even though they might seem conceptually
	 * similar, future uses of the BO label might expect a different
	 * behaviour in each case.
	 */
	panfrost_gem_set_label(obj, label);

err_put_obj:
	drm_gem_object_put(obj);

	return ret;
}

static int panfrost_ioctl_jm_ctx_create(struct drm_device *dev, void *data,
					struct drm_file *file)
{
	return panfrost_jm_ctx_create(file, data);
}

static int panfrost_ioctl_jm_ctx_destroy(struct drm_device *dev, void *data,
					 struct drm_file *file)
{
	const struct drm_panfrost_jm_ctx_destroy *args = data;

	if (args->pad)
		return -EINVAL;

	/* We can't destroy the default context created when the file is opened. */
	if (!args->handle)
		return -EINVAL;

	return panfrost_jm_ctx_destroy(file, args->handle);
}

static int panfrost_ioctl_sync_bo(struct drm_device *ddev, void *data,
				  struct drm_file *file)
{
	struct drm_panfrost_sync_bo *args = data;
	struct drm_panfrost_bo_sync_op *ops;
	struct drm_gem_object *obj;
	int ret;
	u32 i;

	if (args->pad)
		return -EINVAL;

	if (!args->op_count)
		return 0;

	ops = kvmalloc_array(args->op_count, sizeof(*ops), GFP_KERNEL);
	if (!ops) {
		DRM_DEBUG("Failed to allocate incoming BO sync ops array\n");
		return -ENOMEM;
	}

	if (copy_from_user(ops, (void __user *)(uintptr_t)args->ops,
			   args->op_count * sizeof(*ops))) {
		DRM_DEBUG("Failed to copy in BO sync ops\n");
		ret = -EFAULT;
		goto err_ops;
	}

	for (i = 0; i < args->op_count; i++) {
		obj = drm_gem_object_lookup(file, ops[i].handle);
		if (!obj) {
			ret = -ENOENT;
			goto err_ops;
		}

		ret = panfrost_gem_sync(obj, ops[i].type,
					ops[i].offset, ops[i].size);

		drm_gem_object_put(obj);

		if (ret)
			goto err_ops;
	}

err_ops:
	kvfree(ops);

	return ret;
}

static int panfrost_ioctl_query_bo_info(struct drm_device *dev, void *data,
					struct drm_file *file_priv)
{
	struct drm_panfrost_query_bo_info *args = data;
	struct drm_gem_object *gem_obj;
	struct panfrost_gem_object *bo;

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", args->handle);
		return -ENOENT;
	}

	bo = to_panfrost_bo(gem_obj);
	args->pad = 0;
	args->create_flags = 0;
	args->extra_flags = 0;

	if (drm_gem_is_imported(gem_obj)) {
		args->extra_flags |= DRM_PANFROST_BO_IS_IMPORTED;
	} else {
		if (bo->noexec)
			args->create_flags |= PANFROST_BO_NOEXEC;

		if (bo->is_heap)
			args->create_flags |= PANFROST_BO_HEAP;

		if (!bo->base.map_wc)
			args->create_flags |= PANFROST_BO_WB_MMAP;
	}

	drm_gem_object_put(gem_obj);
	return 0;
}

int panfrost_unstable_ioctl_check(void)
{
	if (!unstable_ioctls)
		return -ENOSYS;

	return 0;
}

static int
panfrost_open(struct drm_device *dev, struct drm_file *file)
{
	int ret;
	struct panfrost_device *pfdev = to_panfrost_device(dev);
	struct panfrost_file_priv *panfrost_priv;

	panfrost_priv = kzalloc(sizeof(*panfrost_priv), GFP_KERNEL);
	if (!panfrost_priv)
		return -ENOMEM;

	panfrost_priv->pfdev = pfdev;
	file->driver_priv = panfrost_priv;

	panfrost_priv->mmu = panfrost_mmu_ctx_create(pfdev);
	if (IS_ERR(panfrost_priv->mmu)) {
		ret = PTR_ERR(panfrost_priv->mmu);
		goto err_free;
	}

	ret = panfrost_jm_open(file);
	if (ret)
		goto err_job;

	return 0;

err_job:
	panfrost_mmu_ctx_put(panfrost_priv->mmu);
err_free:
	kfree(panfrost_priv);
	return ret;
}

static void
panfrost_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct panfrost_file_priv *panfrost_priv = file->driver_priv;

	panfrost_perfcnt_close(file);
	panfrost_jm_close(file);

	panfrost_mmu_ctx_put(panfrost_priv->mmu);
	kfree(panfrost_priv);
}

static const struct drm_ioctl_desc panfrost_drm_driver_ioctls[] = {
#define PANFROST_IOCTL(n, func, flags) \
	DRM_IOCTL_DEF_DRV(PANFROST_##n, panfrost_ioctl_##func, flags)

	PANFROST_IOCTL(SUBMIT,		submit,		DRM_RENDER_ALLOW),
	PANFROST_IOCTL(WAIT_BO,		wait_bo,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(CREATE_BO,	create_bo,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(MMAP_BO,		mmap_bo,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(GET_PARAM,	get_param,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(GET_BO_OFFSET,	get_bo_offset,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(PERFCNT_ENABLE,	perfcnt_enable,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(PERFCNT_DUMP,	perfcnt_dump,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(MADVISE,		madvise,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(SET_LABEL_BO,	set_label_bo,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(JM_CTX_CREATE,	jm_ctx_create,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(JM_CTX_DESTROY,	jm_ctx_destroy,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(SYNC_BO,		sync_bo,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(QUERY_BO_INFO,	query_bo_info,	DRM_RENDER_ALLOW),
};

static void panfrost_gpu_show_fdinfo(struct panfrost_device *pfdev,
				     struct panfrost_file_priv *panfrost_priv,
				     struct drm_printer *p)
{
	int i;

	/*
	 * IMPORTANT NOTE: drm-cycles and drm-engine measurements are not
	 * accurate, as they only provide a rough estimation of the number of
	 * GPU cycles and CPU time spent in a given context. This is due to two
	 * different factors:
	 * - Firstly, we must consider the time the CPU and then the kernel
	 *   takes to process the GPU interrupt, which means additional time and
	 *   GPU cycles will be added in excess to the real figure.
	 * - Secondly, the pipelining done by the Job Manager (2 job slots per
	 *   engine) implies there is no way to know exactly how much time each
	 *   job spent on the GPU.
	 */

	for (i = 0; i < NUM_JOB_SLOTS - 1; i++) {
		if (pfdev->profile_mode) {
			drm_printf(p, "drm-engine-%s:\t%llu ns\n",
				   panfrost_engine_names[i],
				   panfrost_priv->engine_usage.elapsed_ns[i]);
			drm_printf(p, "drm-cycles-%s:\t%llu\n",
				   panfrost_engine_names[i],
				   panfrost_priv->engine_usage.cycles[i]);
		}
		drm_printf(p, "drm-maxfreq-%s:\t%lu Hz\n",
			   panfrost_engine_names[i], pfdev->pfdevfreq.fast_rate);
		drm_printf(p, "drm-curfreq-%s:\t%lu Hz\n",
			   panfrost_engine_names[i], pfdev->pfdevfreq.current_frequency);
	}
}

static void panfrost_show_fdinfo(struct drm_printer *p, struct drm_file *file)
{
	struct panfrost_device *pfdev = to_panfrost_device(file->minor->dev);

	panfrost_gpu_show_fdinfo(pfdev, file->driver_priv, p);

	drm_show_memory_stats(p, file);
}

static const struct file_operations panfrost_drm_driver_fops = {
	.owner = THIS_MODULE,
	DRM_GEM_FOPS,
	.show_fdinfo = drm_show_fdinfo,
};

#ifdef CONFIG_DEBUG_FS
static int panthor_gems_show(struct seq_file *m, void *data)
{
	struct drm_info_node *node = m->private;
	struct panfrost_device *pfdev = to_panfrost_device(node->minor->dev);

	panfrost_gem_debugfs_print_bos(pfdev, m);

	return 0;
}

static void show_panfrost_jm_ctx(struct panfrost_jm_ctx *jm_ctx, u32 handle,
				 struct seq_file *m)
{
	struct drm_device *ddev = ((struct drm_info_node *)m->private)->minor->dev;
	const char *prio = "UNKNOWN";

	static const char * const prios[] = {
		[DRM_SCHED_PRIORITY_HIGH] = "HIGH",
		[DRM_SCHED_PRIORITY_NORMAL] = "NORMAL",
		[DRM_SCHED_PRIORITY_LOW] = "LOW",
	};

	if (jm_ctx->slot_entity[0].priority !=
	    jm_ctx->slot_entity[1].priority)
		drm_warn(ddev, "Slot priorities should be the same in a single context");

	if (jm_ctx->slot_entity[0].priority < ARRAY_SIZE(prios))
		prio = prios[jm_ctx->slot_entity[0].priority];

	seq_printf(m, " JM context %u: priority %s\n", handle, prio);
}

static int show_file_jm_ctxs(struct panfrost_file_priv *pfile,
			     struct seq_file *m)
{
	struct panfrost_jm_ctx *jm_ctx;
	unsigned long i;

	xa_lock(&pfile->jm_ctxs);
	xa_for_each(&pfile->jm_ctxs, i, jm_ctx) {
		jm_ctx = panfrost_jm_ctx_get(jm_ctx);
		xa_unlock(&pfile->jm_ctxs);
		show_panfrost_jm_ctx(jm_ctx, i, m);
		panfrost_jm_ctx_put(jm_ctx);
		xa_lock(&pfile->jm_ctxs);
	}
	xa_unlock(&pfile->jm_ctxs);

	return 0;
}

static struct drm_info_list panthor_debugfs_list[] = {
	{"gems",
	 panthor_gems_show, 0, NULL},
};

static int panthor_gems_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(panthor_debugfs_list,
				 ARRAY_SIZE(panthor_debugfs_list),
				 minor->debugfs_root, minor);

	return 0;
}

static int show_each_file(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *ddev = node->minor->dev;
	int (*show)(struct panfrost_file_priv *, struct seq_file *) =
		node->info_ent->data;
	struct drm_file *file;
	int ret;

	ret = mutex_lock_interruptible(&ddev->filelist_mutex);
	if (ret)
		return ret;

	list_for_each_entry(file, &ddev->filelist, lhead) {
		struct task_struct *task;
		struct panfrost_file_priv *pfile = file->driver_priv;
		struct pid *pid;

		/*
		 * Although we have a valid reference on file->pid, that does
		 * not guarantee that the task_struct who called get_pid() is
		 * still alive (e.g. get_pid(current) => fork() => exit()).
		 * Therefore, we need to protect this ->comm access using RCU.
		 */
		rcu_read_lock();
		pid = rcu_dereference(file->pid);
		task = pid_task(pid, PIDTYPE_TGID);
		seq_printf(m, "client_id %8llu pid %8d command %s:\n",
			   file->client_id, pid_nr(pid),
			   task ? task->comm : "<unknown>");
		rcu_read_unlock();

		ret = show(pfile, m);
		if (ret < 0)
			break;

		seq_puts(m, "\n");
	}

	mutex_unlock(&ddev->filelist_mutex);
	return ret;
}

static struct drm_info_list panfrost_sched_debugfs_list[] = {
	{ "sched_ctxs", show_each_file, 0, show_file_jm_ctxs },
};

static void panfrost_sched_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(panfrost_sched_debugfs_list,
				 ARRAY_SIZE(panfrost_sched_debugfs_list),
				 minor->debugfs_root, minor);
}

static void panfrost_debugfs_init(struct drm_minor *minor)
{
	panthor_gems_debugfs_init(minor);
	panfrost_sched_debugfs_init(minor);
}
#endif

/*
 * Panfrost driver version:
 * - 1.0 - initial interface
 * - 1.1 - adds HEAP and NOEXEC flags for CREATE_BO
 * - 1.2 - adds AFBC_FEATURES query
 * - 1.3 - adds JD_REQ_CYCLE_COUNT job requirement for SUBMIT
 *       - adds SYSTEM_TIMESTAMP and SYSTEM_TIMESTAMP_FREQUENCY queries
 * - 1.4 - adds SET_LABEL_BO
 * - 1.5 - adds JM_CTX_{CREATE,DESTROY} ioctls and extend SUBMIT to allow
 *	   context creation with configurable priorities/affinity
 * - 1.6 - adds PANFROST_BO_MAP_WB, PANFROST_IOCTL_SYNC_BO,
 *	   PANFROST_IOCTL_QUERY_BO_INFO and
 *	   DRM_PANFROST_PARAM_SELECTED_COHERENCY
 */
static const struct drm_driver panfrost_drm_driver = {
	.driver_features	= DRIVER_RENDER | DRIVER_GEM | DRIVER_SYNCOBJ,
	.open			= panfrost_open,
	.postclose		= panfrost_postclose,
	.show_fdinfo		= panfrost_show_fdinfo,
	.ioctls			= panfrost_drm_driver_ioctls,
	.num_ioctls		= ARRAY_SIZE(panfrost_drm_driver_ioctls),
	.fops			= &panfrost_drm_driver_fops,
	.name			= "panfrost",
	.desc			= "panfrost DRM",
	.major			= 1,
	.minor			= 6,

	.gem_create_object	= panfrost_gem_create_object,
	.gem_prime_import	= panfrost_gem_prime_import,
	.gem_prime_import_sg_table = panfrost_gem_prime_import_sg_table,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init = panfrost_debugfs_init,
#endif
};

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
bool panfrost_transparent_hugepage = true;
module_param_named(transparent_hugepage, panfrost_transparent_hugepage, bool, 0400);
MODULE_PARM_DESC(transparent_hugepage, "Use a dedicated tmpfs mount point with Transparent Hugepage enabled (true = default)");
#endif

static int panfrost_probe(struct platform_device *pdev)
{
	struct panfrost_device *pfdev;
	int err;

	pfdev = devm_drm_dev_alloc(&pdev->dev, &panfrost_drm_driver,
				   struct panfrost_device, base);
	if (IS_ERR(pfdev))
		return PTR_ERR(pfdev);

	platform_set_drvdata(pdev, pfdev);

	pfdev->comp = of_device_get_match_data(&pdev->dev);
	if (!pfdev->comp)
		return -ENODEV;

	pfdev->coherent = device_get_dma_attr(&pdev->dev) == DEV_DMA_COHERENT;

	mutex_init(&pfdev->shrinker_lock);
	INIT_LIST_HEAD(&pfdev->shrinker_list);

	err = panfrost_device_init(pfdev);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Fatal error during GPU init\n");
		goto err_out0;
	}

	pm_runtime_set_active(pfdev->base.dev);
	pm_runtime_mark_last_busy(pfdev->base.dev);
	pm_runtime_enable(pfdev->base.dev);
	pm_runtime_set_autosuspend_delay(pfdev->base.dev, 50); /* ~3 frames */
	pm_runtime_use_autosuspend(pfdev->base.dev);

	/*
	 * Register the DRM device with the core and the connectors with
	 * sysfs
	 */
	err = drm_dev_register(&pfdev->base, 0);
	if (err < 0)
		goto err_out1;

	err = panfrost_gem_shrinker_init(&pfdev->base);
	if (err)
		goto err_out2;

	return 0;

err_out2:
	drm_dev_unregister(&pfdev->base);
err_out1:
	pm_runtime_disable(pfdev->base.dev);
	panfrost_device_fini(pfdev);
	pm_runtime_set_suspended(pfdev->base.dev);
err_out0:
	return err;
}

static void panfrost_remove(struct platform_device *pdev)
{
	struct panfrost_device *pfdev = platform_get_drvdata(pdev);

	drm_dev_unregister(&pfdev->base);
	panfrost_gem_shrinker_cleanup(&pfdev->base);

	pm_runtime_get_sync(pfdev->base.dev);
	pm_runtime_disable(pfdev->base.dev);
	panfrost_device_fini(pfdev);
	pm_runtime_set_suspended(pfdev->base.dev);
}

static ssize_t profiling_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct panfrost_device *pfdev = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", pfdev->profile_mode);
}

static ssize_t profiling_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t len)
{
	struct panfrost_device *pfdev = dev_get_drvdata(dev);
	bool value;
	int err;

	err = kstrtobool(buf, &value);
	if (err)
		return err;

	pfdev->profile_mode = value;

	return len;
}

static DEVICE_ATTR_RW(profiling);

static struct attribute *panfrost_attrs[] = {
	&dev_attr_profiling.attr,
	NULL,
};

ATTRIBUTE_GROUPS(panfrost);

/*
 * The OPP core wants the supply names to be NULL terminated, but we need the
 * correct num_supplies value for regulator core. Hence, we NULL terminate here
 * and then initialize num_supplies with ARRAY_SIZE - 1.
 */
static const char * const default_supplies[] = { "mali", NULL };
static const struct panfrost_compatible default_data = {
	.num_supplies = ARRAY_SIZE(default_supplies) - 1,
	.supply_names = default_supplies,
	.num_pm_domains = 1, /* optional */
	.pm_domain_names = NULL,
};

static const struct panfrost_compatible allwinner_h616_data = {
	.num_supplies = ARRAY_SIZE(default_supplies) - 1,
	.supply_names = default_supplies,
	.num_pm_domains = 1,
	.pm_features = BIT(GPU_PM_RT),
};

static const struct panfrost_compatible amlogic_data = {
	.num_supplies = ARRAY_SIZE(default_supplies) - 1,
	.supply_names = default_supplies,
	.vendor_quirk = panfrost_gpu_amlogic_quirk,
};

static const char * const mediatek_pm_domains[] = { "core0", "core1", "core2",
						    "core3", "core4" };
/*
 * The old data with two power supplies for MT8183 is here only to
 * keep retro-compatibility with older devicetrees, as DVFS will
 * not work with this one.
 *
 * On new devicetrees please use the _b variant with a single and
 * coupled regulators instead.
 */
static const char * const legacy_supplies[] = { "mali", "sram", NULL };
static const struct panfrost_compatible mediatek_mt8183_data = {
	.num_supplies = ARRAY_SIZE(legacy_supplies) - 1,
	.supply_names = legacy_supplies,
	.num_pm_domains = 3,
	.pm_domain_names = mediatek_pm_domains,
};

static const struct panfrost_compatible mediatek_mt8183_b_data = {
	.num_supplies = ARRAY_SIZE(default_supplies) - 1,
	.supply_names = default_supplies,
	.num_pm_domains = 3,
	.pm_domain_names = mediatek_pm_domains,
	.pm_features = BIT(GPU_PM_CLK_DIS) | BIT(GPU_PM_VREG_OFF),
};

static const struct panfrost_compatible mediatek_mt8186_data = {
	.num_supplies = ARRAY_SIZE(default_supplies) - 1,
	.supply_names = default_supplies,
	.num_pm_domains = 2,
	.pm_domain_names = mediatek_pm_domains,
	.pm_features = BIT(GPU_PM_CLK_DIS) | BIT(GPU_PM_VREG_OFF),
};

static const struct panfrost_compatible mediatek_mt8188_data = {
	.num_supplies = ARRAY_SIZE(default_supplies) - 1,
	.supply_names = default_supplies,
	.num_pm_domains = 3,
	.pm_domain_names = mediatek_pm_domains,
	.pm_features = BIT(GPU_PM_CLK_DIS) | BIT(GPU_PM_VREG_OFF),
	.gpu_quirks = BIT(GPU_QUIRK_FORCE_AARCH64_PGTABLE),
};

static const struct panfrost_compatible mediatek_mt8192_data = {
	.num_supplies = ARRAY_SIZE(default_supplies) - 1,
	.supply_names = default_supplies,
	.num_pm_domains = 5,
	.pm_domain_names = mediatek_pm_domains,
	.pm_features = BIT(GPU_PM_CLK_DIS) | BIT(GPU_PM_VREG_OFF),
	.gpu_quirks = BIT(GPU_QUIRK_FORCE_AARCH64_PGTABLE),
};

static const struct panfrost_compatible mediatek_mt8370_data = {
	.num_supplies = ARRAY_SIZE(default_supplies) - 1,
	.supply_names = default_supplies,
	.num_pm_domains = 2,
	.pm_domain_names = mediatek_pm_domains,
	.pm_features = BIT(GPU_PM_CLK_DIS) | BIT(GPU_PM_VREG_OFF),
	.gpu_quirks = BIT(GPU_QUIRK_FORCE_AARCH64_PGTABLE),
};

static const struct of_device_id dt_match[] = {
	/* Set first to probe before the generic compatibles */
	{ .compatible = "amlogic,meson-gxm-mali",
	  .data = &amlogic_data, },
	{ .compatible = "amlogic,meson-g12a-mali",
	  .data = &amlogic_data, },
	{ .compatible = "arm,mali-t604", .data = &default_data, },
	{ .compatible = "arm,mali-t624", .data = &default_data, },
	{ .compatible = "arm,mali-t628", .data = &default_data, },
	{ .compatible = "arm,mali-t720", .data = &default_data, },
	{ .compatible = "arm,mali-t760", .data = &default_data, },
	{ .compatible = "arm,mali-t820", .data = &default_data, },
	{ .compatible = "arm,mali-t830", .data = &default_data, },
	{ .compatible = "arm,mali-t860", .data = &default_data, },
	{ .compatible = "arm,mali-t880", .data = &default_data, },
	{ .compatible = "arm,mali-bifrost", .data = &default_data, },
	{ .compatible = "arm,mali-valhall-jm", .data = &default_data, },
	{ .compatible = "mediatek,mt8183-mali", .data = &mediatek_mt8183_data },
	{ .compatible = "mediatek,mt8183b-mali", .data = &mediatek_mt8183_b_data },
	{ .compatible = "mediatek,mt8186-mali", .data = &mediatek_mt8186_data },
	{ .compatible = "mediatek,mt8188-mali", .data = &mediatek_mt8188_data },
	{ .compatible = "mediatek,mt8192-mali", .data = &mediatek_mt8192_data },
	{ .compatible = "mediatek,mt8370-mali", .data = &mediatek_mt8370_data },
	{ .compatible = "allwinner,sun50i-h616-mali", .data = &allwinner_h616_data },
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static struct platform_driver panfrost_driver = {
	.probe		= panfrost_probe,
	.remove		= panfrost_remove,
	.driver		= {
		.name	= "panfrost",
		.pm	= pm_ptr(&panfrost_pm_ops),
		.of_match_table = dt_match,
		.dev_groups = panfrost_groups,
	},
};
module_platform_driver(panfrost_driver);

MODULE_AUTHOR("Panfrost Project Developers");
MODULE_DESCRIPTION("Panfrost DRM Driver");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: governor_simpleondemand");
