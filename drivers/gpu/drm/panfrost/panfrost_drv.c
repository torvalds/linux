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
#include "panfrost_devfreq.h"
#include "panfrost_gem.h"
#include "panfrost_mmu.h"
#include "panfrost_job.h"
#include "panfrost_gpu.h"

static int panfrost_ioctl_get_param(struct drm_device *ddev, void *data, struct drm_file *file)
{
	struct drm_panfrost_get_param *param = data;
	struct panfrost_device *pfdev = ddev->dev_private;

	if (param->pad != 0)
		return -EINVAL;

	switch (param->param) {
	case DRM_PANFROST_PARAM_GPU_PROD_ID:
		param->value = pfdev->features.id;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int panfrost_ioctl_create_bo(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	int ret;
	struct drm_gem_shmem_object *shmem;
	struct drm_panfrost_create_bo *args = data;

	if (!args->size || args->flags || args->pad)
		return -EINVAL;

	shmem = drm_gem_shmem_create_with_handle(file, dev, args->size,
						 &args->handle);
	if (IS_ERR(shmem))
		return PTR_ERR(shmem);

	ret = panfrost_mmu_map(to_panfrost_bo(&shmem->base));
	if (ret)
		goto err_free;

	args->offset = to_panfrost_bo(&shmem->base)->node.start << PAGE_SHIFT;

	return 0;

err_free:
	drm_gem_object_put_unlocked(&shmem->base);
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
	job->bo_count = args->bo_handle_count;

	if (!job->bo_count)
		return 0;

	job->implicit_fences = kvmalloc_array(job->bo_count,
				  sizeof(struct dma_fence *),
				  GFP_KERNEL | __GFP_ZERO);
	if (!job->implicit_fences)
		return -ENOMEM;

	return drm_gem_objects_lookup(file_priv,
				      (void __user *)(uintptr_t)args->bo_handles,
				      job->bo_count, &job->bos);
}

/**
 * panfrost_copy_in_sync() - Sets up job->in_fences[] with the sync objects
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
	int i;

	job->in_fence_count = args->in_sync_count;

	if (!job->in_fence_count)
		return 0;

	job->in_fences = kvmalloc_array(job->in_fence_count,
					sizeof(struct dma_fence *),
					GFP_KERNEL | __GFP_ZERO);
	if (!job->in_fences) {
		DRM_DEBUG("Failed to allocate job in fences\n");
		return -ENOMEM;
	}

	handles = kvmalloc_array(job->in_fence_count, sizeof(u32), GFP_KERNEL);
	if (!handles) {
		ret = -ENOMEM;
		DRM_DEBUG("Failed to allocate incoming syncobj handles\n");
		goto fail;
	}

	if (copy_from_user(handles,
			   (void __user *)(uintptr_t)args->in_syncs,
			   job->in_fence_count * sizeof(u32))) {
		ret = -EFAULT;
		DRM_DEBUG("Failed to copy in syncobj handles\n");
		goto fail;
	}

	for (i = 0; i < job->in_fence_count; i++) {
		ret = drm_syncobj_find_fence(file_priv, handles[i], 0, 0,
					     &job->in_fences[i]);
		if (ret == -EINVAL)
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

	ret = reservation_object_wait_timeout_rcu(gem_obj->resv, true,
						  true, timeout);
	if (!ret)
		ret = timeout ? -ETIMEDOUT : -EBUSY;

	drm_gem_object_put_unlocked(gem_obj);

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

	ret = drm_gem_create_mmap_offset(gem_obj);
	if (ret == 0)
		args->offset = drm_vma_node_offset_addr(&gem_obj->vma_node);
	drm_gem_object_put_unlocked(gem_obj);

	return ret;
}

static int panfrost_ioctl_get_bo_offset(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	struct drm_panfrost_get_bo_offset *args = data;
	struct drm_gem_object *gem_obj;
	struct panfrost_gem_object *bo;

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", args->handle);
		return -ENOENT;
	}
	bo = to_panfrost_bo(gem_obj);

	args->offset = bo->node.start << PAGE_SHIFT;

	drm_gem_object_put_unlocked(gem_obj);
	return 0;
}

static int
panfrost_open(struct drm_device *dev, struct drm_file *file)
{
	struct panfrost_device *pfdev = dev->dev_private;
	struct panfrost_file_priv *panfrost_priv;

	panfrost_priv = kzalloc(sizeof(*panfrost_priv), GFP_KERNEL);
	if (!panfrost_priv)
		return -ENOMEM;

	panfrost_priv->pfdev = pfdev;
	file->driver_priv = panfrost_priv;

	return panfrost_job_open(panfrost_priv);
}

static void
panfrost_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct panfrost_file_priv *panfrost_priv = file->driver_priv;

	panfrost_job_close(panfrost_priv);

	kfree(panfrost_priv);
}

/* DRM_AUTH is required on SUBMIT for now, while all clients share a single
 * address space.  Note that render nodes would be able to submit jobs that
 * could access BOs from clients authenticated with the master node.
 */
static const struct drm_ioctl_desc panfrost_drm_driver_ioctls[] = {
#define PANFROST_IOCTL(n, func, flags) \
	DRM_IOCTL_DEF_DRV(PANFROST_##n, panfrost_ioctl_##func, flags)

	PANFROST_IOCTL(SUBMIT,		submit,		DRM_RENDER_ALLOW | DRM_AUTH),
	PANFROST_IOCTL(WAIT_BO,		wait_bo,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(CREATE_BO,	create_bo,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(MMAP_BO,		mmap_bo,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(GET_PARAM,	get_param,	DRM_RENDER_ALLOW),
	PANFROST_IOCTL(GET_BO_OFFSET,	get_bo_offset,	DRM_RENDER_ALLOW),
};

DEFINE_DRM_GEM_SHMEM_FOPS(panfrost_drm_driver_fops);

static struct drm_driver panfrost_drm_driver = {
	.driver_features	= DRIVER_RENDER | DRIVER_GEM | DRIVER_PRIME |
				  DRIVER_SYNCOBJ,
	.open			= panfrost_open,
	.postclose		= panfrost_postclose,
	.ioctls			= panfrost_drm_driver_ioctls,
	.num_ioctls		= ARRAY_SIZE(panfrost_drm_driver_ioctls),
	.fops			= &panfrost_drm_driver_fops,
	.name			= "panfrost",
	.desc			= "panfrost DRM",
	.date			= "20180908",
	.major			= 1,
	.minor			= 0,

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

	/* Allocate and initialze the DRM device. */
	ddev = drm_dev_alloc(&panfrost_drm_driver, &pdev->dev);
	if (IS_ERR(ddev))
		return PTR_ERR(ddev);

	ddev->dev_private = pfdev;
	pfdev->ddev = ddev;

	spin_lock_init(&pfdev->mm_lock);

	/* 4G enough for now. can be 48-bit */
	drm_mm_init(&pfdev->mm, SZ_32M >> PAGE_SHIFT, (SZ_4G - SZ_32M) >> PAGE_SHIFT);

	pm_runtime_use_autosuspend(pfdev->dev);
	pm_runtime_set_autosuspend_delay(pfdev->dev, 50); /* ~3 frames */
	pm_runtime_enable(pfdev->dev);

	err = panfrost_device_init(pfdev);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Fatal error during GPU init\n");
		goto err_out0;
	}

	err = panfrost_devfreq_init(pfdev);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Fatal error during devfreq init\n");
		goto err_out1;
	}

	/*
	 * Register the DRM device with the core and the connectors with
	 * sysfs
	 */
	err = drm_dev_register(ddev, 0);
	if (err < 0)
		goto err_out1;

	return 0;

err_out1:
	panfrost_device_fini(pfdev);
err_out0:
	pm_runtime_disable(pfdev->dev);
	drm_dev_put(ddev);
	return err;
}

static int panfrost_remove(struct platform_device *pdev)
{
	struct panfrost_device *pfdev = platform_get_drvdata(pdev);
	struct drm_device *ddev = pfdev->ddev;

	drm_dev_unregister(ddev);
	pm_runtime_get_sync(pfdev->dev);
	pm_runtime_put_sync_autosuspend(pfdev->dev);
	pm_runtime_disable(pfdev->dev);
	panfrost_device_fini(pfdev);
	drm_dev_put(ddev);
	return 0;
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "arm,mali-t604" },
	{ .compatible = "arm,mali-t624" },
	{ .compatible = "arm,mali-t628" },
	{ .compatible = "arm,mali-t720" },
	{ .compatible = "arm,mali-t760" },
	{ .compatible = "arm,mali-t820" },
	{ .compatible = "arm,mali-t830" },
	{ .compatible = "arm,mali-t860" },
	{ .compatible = "arm,mali-t880" },
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
