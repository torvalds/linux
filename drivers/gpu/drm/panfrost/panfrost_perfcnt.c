// SPDX-License-Identifier: GPL-2.0
/* Copyright 2019 Collabora Ltd */

#include <drm/drm_file.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/panfrost_drm.h>
#include <linux/completion.h>
#include <linux/iopoll.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "panfrost_device.h"
#include "panfrost_features.h"
#include "panfrost_gem.h"
#include "panfrost_issues.h"
#include "panfrost_job.h"
#include "panfrost_mmu.h"
#include "panfrost_regs.h"

#define COUNTERS_PER_BLOCK		64
#define BYTES_PER_COUNTER		4
#define BLOCKS_PER_COREGROUP		8
#define V4_SHADERS_PER_COREGROUP	4

struct panfrost_perfcnt {
	struct panfrost_gem_object *bo;
	size_t bosize;
	void *buf;
	struct panfrost_file_priv *user;
	struct mutex lock;
	struct completion dump_comp;
};

void panfrost_perfcnt_clean_cache_done(struct panfrost_device *pfdev)
{
	complete(&pfdev->perfcnt->dump_comp);
}

void panfrost_perfcnt_sample_done(struct panfrost_device *pfdev)
{
	gpu_write(pfdev, GPU_CMD, GPU_CMD_CLEAN_CACHES);
}

static int panfrost_perfcnt_dump_locked(struct panfrost_device *pfdev)
{
	u64 gpuva;
	int ret;

	reinit_completion(&pfdev->perfcnt->dump_comp);
	gpuva = pfdev->perfcnt->bo->node.start << PAGE_SHIFT;
	gpu_write(pfdev, GPU_PERFCNT_BASE_LO, gpuva);
	gpu_write(pfdev, GPU_PERFCNT_BASE_HI, gpuva >> 32);
	gpu_write(pfdev, GPU_INT_CLEAR,
		  GPU_IRQ_CLEAN_CACHES_COMPLETED |
		  GPU_IRQ_PERFCNT_SAMPLE_COMPLETED);
	gpu_write(pfdev, GPU_CMD, GPU_CMD_PERFCNT_SAMPLE);
	ret = wait_for_completion_interruptible_timeout(&pfdev->perfcnt->dump_comp,
							msecs_to_jiffies(1000));
	if (!ret)
		ret = -ETIMEDOUT;
	else if (ret > 0)
		ret = 0;

	return ret;
}

static int panfrost_perfcnt_enable_locked(struct panfrost_device *pfdev,
					  struct panfrost_file_priv *user,
					  unsigned int counterset)
{
	struct panfrost_perfcnt *perfcnt = pfdev->perfcnt;
	struct drm_gem_shmem_object *bo;
	u32 cfg;
	int ret;

	if (user == perfcnt->user)
		return 0;
	else if (perfcnt->user)
		return -EBUSY;

	ret = pm_runtime_get_sync(pfdev->dev);
	if (ret < 0)
		return ret;

	bo = drm_gem_shmem_create(pfdev->ddev, perfcnt->bosize);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	perfcnt->bo = to_panfrost_bo(&bo->base);

	/* Map the perfcnt buf in the address space attached to file_priv. */
	ret = panfrost_mmu_map(perfcnt->bo);
	if (ret)
		goto err_put_bo;

	perfcnt->buf = drm_gem_shmem_vmap(&bo->base);
	if (IS_ERR(perfcnt->buf)) {
		ret = PTR_ERR(perfcnt->buf);
		goto err_put_bo;
	}

	/*
	 * Invalidate the cache and clear the counters to start from a fresh
	 * state.
	 */
	reinit_completion(&pfdev->perfcnt->dump_comp);
	gpu_write(pfdev, GPU_INT_CLEAR,
		  GPU_IRQ_CLEAN_CACHES_COMPLETED |
		  GPU_IRQ_PERFCNT_SAMPLE_COMPLETED);
	gpu_write(pfdev, GPU_CMD, GPU_CMD_PERFCNT_CLEAR);
	gpu_write(pfdev, GPU_CMD, GPU_CMD_CLEAN_INV_CACHES);
	ret = wait_for_completion_timeout(&pfdev->perfcnt->dump_comp,
					  msecs_to_jiffies(1000));
	if (!ret) {
		ret = -ETIMEDOUT;
		goto err_vunmap;
	}

	perfcnt->user = user;

	/*
	 * Always use address space 0 for now.
	 * FIXME: this needs to be updated when we start using different
	 * address space.
	 */
	cfg = GPU_PERFCNT_CFG_AS(0) |
	      GPU_PERFCNT_CFG_MODE(GPU_PERFCNT_CFG_MODE_MANUAL);

	/*
	 * Bifrost GPUs have 2 set of counters, but we're only interested by
	 * the first one for now.
	 */
	if (panfrost_model_is_bifrost(pfdev))
		cfg |= GPU_PERFCNT_CFG_SETSEL(counterset);

	gpu_write(pfdev, GPU_PRFCNT_JM_EN, 0xffffffff);
	gpu_write(pfdev, GPU_PRFCNT_SHADER_EN, 0xffffffff);
	gpu_write(pfdev, GPU_PRFCNT_MMU_L2_EN, 0xffffffff);

	/*
	 * Due to PRLAM-8186 we need to disable the Tiler before we enable HW
	 * counters.
	 */
	if (panfrost_has_hw_issue(pfdev, HW_ISSUE_8186))
		gpu_write(pfdev, GPU_PRFCNT_TILER_EN, 0);
	else
		gpu_write(pfdev, GPU_PRFCNT_TILER_EN, 0xffffffff);

	gpu_write(pfdev, GPU_PERFCNT_CFG, cfg);

	if (panfrost_has_hw_issue(pfdev, HW_ISSUE_8186))
		gpu_write(pfdev, GPU_PRFCNT_TILER_EN, 0xffffffff);

	return 0;

err_vunmap:
	drm_gem_shmem_vunmap(&perfcnt->bo->base.base, perfcnt->buf);
err_put_bo:
	drm_gem_object_put_unlocked(&bo->base);
	return ret;
}

static int panfrost_perfcnt_disable_locked(struct panfrost_device *pfdev,
					   struct panfrost_file_priv *user)
{
	struct panfrost_perfcnt *perfcnt = pfdev->perfcnt;

	if (user != perfcnt->user)
		return -EINVAL;

	gpu_write(pfdev, GPU_PRFCNT_JM_EN, 0x0);
	gpu_write(pfdev, GPU_PRFCNT_SHADER_EN, 0x0);
	gpu_write(pfdev, GPU_PRFCNT_MMU_L2_EN, 0x0);
	gpu_write(pfdev, GPU_PRFCNT_TILER_EN, 0);
	gpu_write(pfdev, GPU_PERFCNT_CFG,
		  GPU_PERFCNT_CFG_MODE(GPU_PERFCNT_CFG_MODE_OFF));

	perfcnt->user = NULL;
	drm_gem_shmem_vunmap(&perfcnt->bo->base.base, perfcnt->buf);
	perfcnt->buf = NULL;
	drm_gem_object_put_unlocked(&perfcnt->bo->base.base);
	perfcnt->bo = NULL;
	pm_runtime_mark_last_busy(pfdev->dev);
	pm_runtime_put_autosuspend(pfdev->dev);

	return 0;
}

int panfrost_ioctl_perfcnt_enable(struct drm_device *dev, void *data,
				  struct drm_file *file_priv)
{
	struct panfrost_file_priv *pfile = file_priv->driver_priv;
	struct panfrost_device *pfdev = dev->dev_private;
	struct panfrost_perfcnt *perfcnt = pfdev->perfcnt;
	struct drm_panfrost_perfcnt_enable *req = data;
	int ret;

	ret = panfrost_unstable_ioctl_check();
	if (ret)
		return ret;

	/* Only Bifrost GPUs have 2 set of counters. */
	if (req->counterset > (panfrost_model_is_bifrost(pfdev) ? 1 : 0))
		return -EINVAL;

	mutex_lock(&perfcnt->lock);
	if (req->enable)
		ret = panfrost_perfcnt_enable_locked(pfdev, pfile,
						     req->counterset);
	else
		ret = panfrost_perfcnt_disable_locked(pfdev, pfile);
	mutex_unlock(&perfcnt->lock);

	return ret;
}

int panfrost_ioctl_perfcnt_dump(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct panfrost_device *pfdev = dev->dev_private;
	struct panfrost_perfcnt *perfcnt = pfdev->perfcnt;
	struct drm_panfrost_perfcnt_dump *req = data;
	void __user *user_ptr = (void __user *)(uintptr_t)req->buf_ptr;
	int ret;

	ret = panfrost_unstable_ioctl_check();
	if (ret)
		return ret;

	mutex_lock(&perfcnt->lock);
	if (perfcnt->user != file_priv->driver_priv) {
		ret = -EINVAL;
		goto out;
	}

	ret = panfrost_perfcnt_dump_locked(pfdev);
	if (ret)
		goto out;

	if (copy_to_user(user_ptr, perfcnt->buf, perfcnt->bosize))
		ret = -EFAULT;

out:
	mutex_unlock(&perfcnt->lock);

	return ret;
}

void panfrost_perfcnt_close(struct panfrost_file_priv *pfile)
{
	struct panfrost_device *pfdev = pfile->pfdev;
	struct panfrost_perfcnt *perfcnt = pfdev->perfcnt;

	pm_runtime_get_sync(pfdev->dev);
	mutex_lock(&perfcnt->lock);
	if (perfcnt->user == pfile)
		panfrost_perfcnt_disable_locked(pfdev, pfile);
	mutex_unlock(&perfcnt->lock);
	pm_runtime_mark_last_busy(pfdev->dev);
	pm_runtime_put_autosuspend(pfdev->dev);
}

int panfrost_perfcnt_init(struct panfrost_device *pfdev)
{
	struct panfrost_perfcnt *perfcnt;
	size_t size;

	if (panfrost_has_hw_feature(pfdev, HW_FEATURE_V4)) {
		unsigned int ncoregroups;

		ncoregroups = hweight64(pfdev->features.l2_present);
		size = ncoregroups * BLOCKS_PER_COREGROUP *
		       COUNTERS_PER_BLOCK * BYTES_PER_COUNTER;
	} else {
		unsigned int nl2c, ncores;

		/*
		 * TODO: define a macro to extract the number of l2 caches from
		 * mem_features.
		 */
		nl2c = ((pfdev->features.mem_features >> 8) & GENMASK(3, 0)) + 1;

		/*
		 * shader_present might be sparse, but the counters layout
		 * forces to dump unused regions too, hence the fls64() call
		 * instead of hweight64().
		 */
		ncores = fls64(pfdev->features.shader_present);

		/*
		 * There's always one JM and one Tiler block, hence the '+ 2'
		 * here.
		 */
		size = (nl2c + ncores + 2) *
		       COUNTERS_PER_BLOCK * BYTES_PER_COUNTER;
	}

	perfcnt = devm_kzalloc(pfdev->dev, sizeof(*perfcnt), GFP_KERNEL);
	if (!perfcnt)
		return -ENOMEM;

	perfcnt->bosize = size;

	/* Start with everything disabled. */
	gpu_write(pfdev, GPU_PERFCNT_CFG,
		  GPU_PERFCNT_CFG_MODE(GPU_PERFCNT_CFG_MODE_OFF));
	gpu_write(pfdev, GPU_PRFCNT_JM_EN, 0);
	gpu_write(pfdev, GPU_PRFCNT_SHADER_EN, 0);
	gpu_write(pfdev, GPU_PRFCNT_MMU_L2_EN, 0);
	gpu_write(pfdev, GPU_PRFCNT_TILER_EN, 0);

	init_completion(&perfcnt->dump_comp);
	mutex_init(&perfcnt->lock);
	pfdev->perfcnt = perfcnt;

	return 0;
}

void panfrost_perfcnt_fini(struct panfrost_device *pfdev)
{
	/* Disable everything before leaving. */
	gpu_write(pfdev, GPU_PERFCNT_CFG,
		  GPU_PERFCNT_CFG_MODE(GPU_PERFCNT_CFG_MODE_OFF));
	gpu_write(pfdev, GPU_PRFCNT_JM_EN, 0);
	gpu_write(pfdev, GPU_PRFCNT_SHADER_EN, 0);
	gpu_write(pfdev, GPU_PRFCNT_MMU_L2_EN, 0);
	gpu_write(pfdev, GPU_PRFCNT_TILER_EN, 0);
}
