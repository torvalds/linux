// SPDX-License-Identifier: GPL-2.0
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Linaro, Ltd., Rob Herring <robh@kernel.org> */
/* Copyright 2019 Collabora ltd. */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/pagemap.h>
#include <linux/pm_runtime.h>
#include <drm/panfrost_drm.h>
#include <drm/drm_drv.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_syncobj.h>
#include <drm/drm_utils.h>

#include "panfrost_device.h"
#include "panfrost_gem.h"
#include "panfrost_mmu.h"
#include "panfrost_job.h"
#include "panfrost_gpu.h"
#include "panfrost_perfcnt.h"

static bool unstable_ioctls;
module_param_unsafe(unstable_ioctls, bool, 0600);

static int panfrost_ioctl_get_param(struct drm_device *ddev, void *data, struct drm_file *file)
{
	struct drm_panfrost_get_param *param = data;
	struct panfrost_device *pfdev = ddev->dev_private;

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
	default:
		return -EINVAL;
	}

	return 0;
}

static int panfrost_ioctl_create_bo(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct panfrost_file_priv *priv = file->driver_priv;
	struct panfrost_gem_object *bo;
	struct drm_panfrost_create_bo *args = data;
	struct panfrost_gem_mapping *mapping;

	if (!args->size || args->pad ||
	    (args->flags & ~(PANFROST_BO_NOEXEC | PANFROST_BO_HEAP)))
		return -EINVAL;

	/* Heaps should never be executable */
	if ((args->flags & PANFROST_BO_HEAP) &&
	    !(args->flags & PANFROST_BO_NOEXEC))
		return -EINVAL;

	bo = panfrost_gem_create_with_handle(file, dev, args->size, args->flags,
					     &args->handle);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	mapping = panfrost_gem_mapping_get(bo, priv);
	if (!mapping) {
		drm_gem_object_put(&bo->base.base);
		return -EINVAL;
	}

	args->offset = mapping->mmnode.start << PAGE_SHIFT;
	panfrost_gem_mapping_put(mapping);

	return 0;
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
		struct dma_fence *fence;

		ret = drm_syncobj_find_fence(file_priv, handles[i], 0, 0,
					     &fence);
		if (ret)
			goto fail;

		ret = drm_gem_fence_array_add(&job->deps, fence);

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
	struct panfrost_device *pfdev = dev->dev_private;
	struct drm_panfrost_submit *args = data;
	struct drm_syncobj *sync_out = NULL;
	struct panfrost_job *job;
	int ret = 0;

	if (!args->jc)
		return -EINVAL;

	if (args->requirements && args->requirements != PANFROST_JD_REQ_FS)
		return -EINVAL;

	if (args->out_sync > 0) {
		sync_out = drm_syncobj_find(file, args->out_sync);
		if (!sync_out)
			return -ENODEV;
	}

	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job) {
		ret = -ENOMEM;
		goto fail_out_sync;
	}

	kref_init(&job->refcount);

	xa_init_flags(&job->deps, XA_FLAGS_ALLOC);

	job->pfdev = pfdev;
	job->jc = args->jc;
	job->requirements = args->requirements;
	job->flush_id = panfrost_gpu_get_latest_flush_id(pfdev);
	job->file_priv = file->driver_priv;

	ret = panfrost_copy_in_sync(dev, file, args, job);
	if (ret)
		goto fail_job;

	ret = panfrost_lookup_bos(dev, file, args, job);
	if (ret)
		goto fail_job;

	ret = panfrost_job_push(job);
	if (ret)
		goto fail_job;

	/* Update the return sync object for the job */
	if (sync_out)
		drm_syncobj_replace_fence(sync_out, job->render_done_fence);

fail_job:
	panfrost_job_put(job);
fail_out_sync:
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

	ret = dma_resv_wait_timeout(gem_obj->resv, true, true, timeout);
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
	struct panfrost_device *pfdev = dev->dev_private;
	struct drm_gem_object *gem_obj;
	struct panfrost_gem_object *bo;
	int ret = 0;

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", args->handle);
		return -ENOENT;
	}

	bo = to_panfrost_bo(gem_obj);

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

	args->retained = drm_gem_shmem_madvise(gem_obj, args->madv);

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

	drm_gem_object_put(gem_obj);
	return ret;
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
	struct panfrost_device *pfdev = dev->dev_private;
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

	ret = panfrost_job_open(panfrost_priv);
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
	panfrost_job_close(panfrost_priv);

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
};

DEFINE_DRM_GEM_FOPS(panfrost_drm_driver_fops);

/*
 * Panfrost driver version:
 * - 1.0 - initial interface
 * - 1.1 - adds HEAP and NOEXEC flags for CREATE_BO
 * - 1.2 - adds AFBC_FEATURES query
 */
static const struct drm_driver panfrost_drm_driver = {
	.driver_features	= DRIVER_RENDER | DRIVER_GEM | DRIVER_SYNCOBJ,
	.open			= panfrost_open,
	.postclose		= panfrost_postclose,
	.ioctls			= panfrost_drm_driver_ioctls,
	.num_ioctls		= ARRAY_SIZE(panfrost_drm_driver_ioctls),
	.fops			= &panfrost_drm_driver_fops,
	.name			= "panfrost",
	.desc			= "panfrost DRM",
	.date			= "20180908",
	.major			= 1,
	.minor			= 2,

	.gem_create_object	= panfrost_gem_create_object,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import_sg_table = panfrost_gem_prime_import_sg_table,
	.gem_prime_mmap		= drm_gem_prime_mmap,
};

static int panfrost_probe(struct platform_device *pdev)
{
	struct panfrost_device *pfdev;
	struct drm_device *ddev;
	int err;

	pfdev = devm_kzalloc(&pdev->dev, sizeof(*pfdev), GFP_KERNEL);
	if (!pfdev)
		return -ENOMEM;

	pfdev->pdev = pdev;
	pfdev->dev = &pdev->dev;

	platform_set_drvdata(pdev, pfdev);

	pfdev->comp = of_device_get_match_data(&pdev->dev);
	if (!pfdev->comp)
		return -ENODEV;

	pfdev->coherent = device_get_dma_attr(&pdev->dev) == DEV_DMA_COHERENT;

	/* Allocate and initialze the DRM device. */
	ddev = drm_dev_alloc(&panfrost_drm_driver, &pdev->dev);
	if (IS_ERR(ddev))
		return PTR_ERR(ddev);

	ddev->dev_private = pfdev;
	pfdev->ddev = ddev;

	mutex_init(&pfdev->shrinker_lock);
	INIT_LIST_HEAD(&pfdev->shrinker_list);

	err = panfrost_device_init(pfdev);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Fatal error during GPU init\n");
		goto err_out0;
	}

	pm_runtime_set_active(pfdev->dev);
	pm_runtime_mark_last_busy(pfdev->dev);
	pm_runtime_enable(pfdev->dev);
	pm_runtime_set_autosuspend_delay(pfdev->dev, 50); /* ~3 frames */
	pm_runtime_use_autosuspend(pfdev->dev);

	/*
	 * Register the DRM device with the core and the connectors with
	 * sysfs
	 */
	err = drm_dev_register(ddev, 0);
	if (err < 0)
		goto err_out1;

	panfrost_gem_shrinker_init(ddev);

	return 0;

err_out1:
	pm_runtime_disable(pfdev->dev);
	panfrost_device_fini(pfdev);
	pm_runtime_set_suspended(pfdev->dev);
err_out0:
	drm_dev_put(ddev);
	return err;
}

static int panfrost_remove(struct platform_device *pdev)
{
	struct panfrost_device *pfdev = platform_get_drvdata(pdev);
	struct drm_device *ddev = pfdev->ddev;

	drm_dev_unregister(ddev);
	panfrost_gem_shrinker_cleanup(ddev);

	pm_runtime_get_sync(pfdev->dev);
	pm_runtime_disable(pfdev->dev);
	panfrost_device_fini(pfdev);
	pm_runtime_set_suspended(pfdev->dev);

	drm_dev_put(ddev);
	return 0;
}

static const char * const default_supplies[] = { "mali" };
static const struct panfrost_compatible default_data = {
	.num_supplies = ARRAY_SIZE(default_supplies),
	.supply_names = default_supplies,
	.num_pm_domains = 1, /* optional */
	.pm_domain_names = NULL,
};

static const struct panfrost_compatible amlogic_data = {
	.num_supplies = ARRAY_SIZE(default_supplies),
	.supply_names = default_supplies,
	.vendor_quirk = panfrost_gpu_amlogic_quirk,
};

const char * const mediatek_mt8183_supplies[] = { "mali", "sram" };
const char * const mediatek_mt8183_pm_domains[] = { "core0", "core1", "core2" };
static const struct panfrost_compatible mediatek_mt8183_data = {
	.num_supplies = ARRAY_SIZE(mediatek_mt8183_supplies),
	.supply_names = mediatek_mt8183_supplies,
	.num_pm_domains = ARRAY_SIZE(mediatek_mt8183_pm_domains),
	.pm_domain_names = mediatek_mt8183_pm_domains,
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
	{ .compatible = "mediatek,mt8183-mali", .data = &mediatek_mt8183_data },
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static const struct dev_pm_ops panfrost_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(panfrost_device_suspend, panfrost_device_resume, NULL)
};

static struct platform_driver panfrost_driver = {
	.probe		= panfrost_probe,
	.remove		= panfrost_remove,
	.driver		= {
		.name	= "panfrost",
		.pm	= &panfrost_pm_ops,
		.of_match_table = dt_match,
	},
};
module_platform_driver(panfrost_driver);

MODULE_AUTHOR("Panfrost Project Developers");
MODULE_DESCRIPTION("Panfrost DRM Driver");
MODULE_LICENSE("GPL v2");
