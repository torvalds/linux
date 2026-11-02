// SPDX-License-Identifier: GPL-2.0
/* Copyright 2019 Collabora Ltd */

#include "asm-generic/errno-base.h"
#include <linux/completion.h>
#include <linux/iopoll.h>
#include <linux/iosys-map.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <drm/drm_file.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/panfrost_drm.h>
#include <drm/drm_print.h>

#include "panfrost_device.h"
#include "panfrost_features.h"
#include "panfrost_gem.h"
#include "panfrost_issues.h"
#include "panfrost_job.h"
#include "panfrost_mmu.h"
#include "panfrost_perfcnt.h"
#include "panfrost_regs.h"

#define COUNTERS_PER_BLOCK		64
#define BYTES_PER_COUNTER		4
#define BLOCKS_PER_COREGROUP		8
#define V4_SHADERS_PER_COREGROUP	4

struct panfrost_perfcnt {
	struct panfrost_gem_mapping *mapping;
	unsigned int counterset;
	size_t bosize;
	void *buf;
	struct panfrost_file_priv *user;
	struct mutex lock;
	struct completion dump_comp;
	u32 state;
	bool owns_as_ref;
};

static void panfrost_perfcnt_hw_disable(struct panfrost_device *pfdev)
{
	struct panfrost_perfcnt *perfcnt = pfdev->perfcnt;

	gpu_write(pfdev, GPU_PERFCNT_CFG,
		  GPU_PERFCNT_CFG_MODE(GPU_PERFCNT_CFG_MODE_OFF));
	gpu_write(pfdev, GPU_PRFCNT_JM_EN, 0x0);
	gpu_write(pfdev, GPU_PRFCNT_SHADER_EN, 0x0);
	gpu_write(pfdev, GPU_PRFCNT_MMU_L2_EN, 0x0);
	gpu_write(pfdev, GPU_PRFCNT_TILER_EN, 0);

	if (perfcnt->owns_as_ref) {
		panfrost_mmu_as_put(pfdev, perfcnt->mapping->mmu);
		perfcnt->owns_as_ref = false;
	}
}

void panfrost_perfcnt_clean_cache_done(struct panfrost_device *pfdev)
{
	complete(&pfdev->perfcnt->dump_comp);
}

void panfrost_perfcnt_sample_done(struct panfrost_device *pfdev)
{
	if (pfdev->features.selected_coherency != COHERENCY_ACE)
		gpu_write(pfdev, GPU_CMD, GPU_CMD_CLEAN_CACHES);
	else
		complete(&pfdev->perfcnt->dump_comp);
}

static int panfrost_perfcnt_hw_enable(struct panfrost_device *pfdev)
{
	struct panfrost_perfcnt *perfcnt = pfdev->perfcnt;
	u32 cfg, as;
	int ret;

	lockdep_assert_held(&pfdev->reset.lock);
	drm_WARN_ON(&pfdev->base, perfcnt->owns_as_ref);

	ret = panfrost_mmu_as_get(pfdev, perfcnt->mapping->mmu);
	if (ret < 0)
		return ret;

	perfcnt->owns_as_ref = true;

	as = ret;
	cfg = GPU_PERFCNT_CFG_AS(as) |
	      GPU_PERFCNT_CFG_MODE(GPU_PERFCNT_CFG_MODE_MANUAL);

	/*
	 * Bifrost GPUs have 2 set of counters, but we're only interested by
	 * the first one for now.
	 */
	if (panfrost_model_is_bifrost(pfdev))
		cfg |= GPU_PERFCNT_CFG_SETSEL(perfcnt->counterset);

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
}

static int panfrost_perfcnt_dump_locked(struct panfrost_device *pfdev, u32 *state)
{
	struct panfrost_perfcnt *perfcnt = pfdev->perfcnt;
	u64 gpuva = perfcnt->mapping->mmnode.start << PAGE_SHIFT;
	int ret;

	scoped_guard(rwsem_read, &pfdev->reset.lock) {
		*state = perfcnt->state;
		if (perfcnt->state & PANFROST_PERFCNT_SESSION_DEAD)
			return -EIO;

		perfcnt->state = 0;

		reinit_completion(&pfdev->perfcnt->dump_comp);

		gpu_write(pfdev, GPU_PERFCNT_BASE_LO, lower_32_bits(gpuva));
		gpu_write(pfdev, GPU_PERFCNT_BASE_HI, upper_32_bits(gpuva));
		gpu_write(pfdev, GPU_INT_CLEAR, GPU_IRQ_CLEAN_CACHES_COMPLETED |
						GPU_IRQ_PERFCNT_SAMPLE_COMPLETED);
		gpu_write(pfdev, GPU_CMD, GPU_CMD_PERFCNT_SAMPLE);
	}

	/*
	 * Here we release the reset semaphore because perfcnt should not get in the way
	 * of a HW reset. Besides, a legitimate reset might be issued during the wait.
	 */
	ret = wait_for_completion_interruptible_timeout(&pfdev->perfcnt->dump_comp,
							msecs_to_jiffies(1000));

	if (!ret)
		ret = -ETIMEDOUT;

	/* We need to restore perfcnt state so the next successful dump can relay it to UM */
	if (ret < 0) {
		perfcnt->state |= *state;
		*state = 0;
		return ret;
	}

	scoped_guard(rwsem_read, &pfdev->reset.lock) {
		*state |= perfcnt->state;

		/* UM must re-enable their session before requesting new dumps. */
		if (perfcnt->state & PANFROST_PERFCNT_SESSION_DEAD)
			return -EIO;

		/* If we faced a reset during our SAMPLE, the user needs to try again. */
		if (perfcnt->state & PANFROST_PERFCNT_SESSION_INTERRUPTED_BY_RESET)
			return -EAGAIN;
	}

	return 0;
}

static int panfrost_perfcnt_disable_locked(struct panfrost_device *pfdev,
					   struct drm_file *file_priv)
{
	struct panfrost_file_priv *user = file_priv->driver_priv;
	struct panfrost_perfcnt *perfcnt = pfdev->perfcnt;
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(perfcnt->buf);

	if (user != perfcnt->user)
		return -EINVAL;

	scoped_guard(rwsem_read, &pfdev->reset.lock) {
		panfrost_perfcnt_hw_disable(pfdev);
		perfcnt->user = NULL;
	}

	drm_gem_vunmap(&perfcnt->mapping->obj->base.base, &map);
	perfcnt->buf = NULL;
	panfrost_gem_close(&perfcnt->mapping->obj->base.base, file_priv);
	panfrost_gem_mapping_put(perfcnt->mapping);
	perfcnt->mapping = NULL;
	pm_runtime_put_autosuspend(pfdev->base.dev);

	return 0;
}

static int panfrost_perfcnt_enable_locked(struct panfrost_device *pfdev,
					  struct drm_file *file_priv,
					  unsigned int counterset)
{
	struct panfrost_file_priv *user = file_priv->driver_priv;
	struct panfrost_perfcnt *perfcnt = pfdev->perfcnt;
	struct drm_gem_shmem_object *bo;
	struct iosys_map map;
	int ret;

	if (perfcnt->user == user) {
		scoped_guard(rwsem_read, &pfdev->reset.lock) {
			if (perfcnt->state & PANFROST_PERFCNT_SESSION_DEAD) {
				/* A successful reset might've come through in the time gap
				 * between the last failed one and this re-enable attempt.
				 */
				ret = (perfcnt->owns_as_ref) ? 0 :
					panfrost_perfcnt_hw_enable(pfdev);
				if (!ret)
					perfcnt->state = 0;

				return ret;
			}
		}

		return 0;
	}

	if (perfcnt->user)
		return -EBUSY;

	ret = pm_runtime_get_sync(pfdev->base.dev);
	if (ret < 0)
		goto err_put_pm;

	bo = drm_gem_shmem_create(&pfdev->base, perfcnt->bosize);
	if (IS_ERR(bo)) {
		ret = PTR_ERR(bo);
		goto err_put_pm;
	}

	/* Map the perfcnt buf in the address space attached to file_priv. */
	ret = panfrost_gem_open(&bo->base, file_priv);
	if (ret)
		goto err_put_bo;

	perfcnt->mapping = panfrost_gem_mapping_get(to_panfrost_bo(&bo->base),
						    user);
	if (!perfcnt->mapping) {
		ret = -EINVAL;
		goto err_close_bo;
	}

	ret = drm_gem_vmap(&bo->base, &map);
	if (ret)
		goto err_put_mapping;

	perfcnt->buf = map.vaddr;
	perfcnt->counterset = counterset;

	panfrost_gem_internal_set_label(&bo->base, "Perfcnt sample buffer");

	scoped_guard(rwsem_read, &pfdev->reset.lock) {
		/*
		 * Clear the counters to start from a fresh state.
		 */
		gpu_write(pfdev, GPU_INT_CLEAR, GPU_IRQ_PERFCNT_SAMPLE_COMPLETED);
		gpu_write(pfdev, GPU_CMD, GPU_CMD_PERFCNT_CLEAR);

		ret = panfrost_perfcnt_hw_enable(pfdev);
		if (ret)
			goto err_vunmap;

		perfcnt->user = user;
		perfcnt->state = 0;
	}

	/* The BO ref is retained by the mapping. */
	drm_gem_object_put(&bo->base);

	return 0;

err_vunmap:
	drm_gem_vunmap(&bo->base, &map);
err_put_mapping:
	panfrost_gem_mapping_put(perfcnt->mapping);
err_close_bo:
	panfrost_gem_close(&bo->base, file_priv);
err_put_bo:
	drm_gem_object_put(&bo->base);
err_put_pm:
	pm_runtime_put(pfdev->base.dev);
	return ret;
}

int panfrost_ioctl_perfcnt_enable(struct drm_device *dev, void *data,
				  struct drm_file *file_priv)
{
	struct panfrost_device *pfdev = to_panfrost_device(dev);
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
		ret = panfrost_perfcnt_enable_locked(pfdev, file_priv,
						     req->counterset);
	else
		ret = panfrost_perfcnt_disable_locked(pfdev, file_priv);
	mutex_unlock(&perfcnt->lock);

	return ret;
}

int panfrost_ioctl_perfcnt_dump(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct panfrost_device *pfdev = to_panfrost_device(dev);
	struct panfrost_perfcnt *perfcnt = pfdev->perfcnt;
	struct drm_panfrost_perfcnt_dump *req = data;
	void __user *user_ptr = (void __user *)(uintptr_t)req->buf_ptr;
	int ret;

	ret = panfrost_unstable_ioctl_check();
	if (ret)
		return ret;

	if (req->pad)
		return -EINVAL;

	mutex_lock(&perfcnt->lock);
	if (perfcnt->user != file_priv->driver_priv) {
		ret = -EINVAL;
		goto out;
	}

	ret = panfrost_perfcnt_dump_locked(pfdev, &req->state);
	if (ret)
		goto out;

	if (copy_to_user(user_ptr, perfcnt->buf, perfcnt->bosize))
		ret = -EFAULT;

out:
	mutex_unlock(&perfcnt->lock);

	return ret;
}

void panfrost_perfcnt_close(struct drm_file *file_priv)
{
	struct panfrost_file_priv *pfile = file_priv->driver_priv;
	struct panfrost_device *pfdev = pfile->pfdev;
	struct panfrost_perfcnt *perfcnt = pfdev->perfcnt;

	pm_runtime_get_sync(pfdev->base.dev);
	mutex_lock(&perfcnt->lock);
	if (perfcnt->user == pfile)
		panfrost_perfcnt_disable_locked(pfdev, file_priv);
	mutex_unlock(&perfcnt->lock);
	pm_runtime_put_autosuspend(pfdev->base.dev);
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

	perfcnt = devm_kzalloc(pfdev->base.dev, sizeof(*perfcnt), GFP_KERNEL);
	if (!perfcnt)
		return -ENOMEM;

	perfcnt->bosize = size;
	pfdev->perfcnt = perfcnt;
	mutex_init(&perfcnt->lock);
	init_completion(&perfcnt->dump_comp);

	/* Start with everything disabled. */
	panfrost_perfcnt_hw_disable(pfdev);

	return 0;
}

void panfrost_perfcnt_fini(struct panfrost_device *pfdev)
{
	/* Disable everything before leaving. */
	panfrost_perfcnt_hw_disable(pfdev);
}

void panfrost_perfcnt_reset(struct panfrost_device *pfdev)
{
	struct panfrost_perfcnt *perfcnt = pfdev->perfcnt;

	if (drm_WARN_ON(&pfdev->base, !perfcnt))
		return;

	lockdep_assert_held(&pfdev->reset.lock);

	if (!perfcnt->user)
		return;

	/* All active AS are released during the MMU post_reset. */
	perfcnt->owns_as_ref = false;
	perfcnt->state |= PANFROST_PERFCNT_SESSION_INTERRUPTED_BY_RESET;
	if (panfrost_perfcnt_hw_enable(pfdev))
		perfcnt->state |= PANFROST_PERFCNT_SESSION_DEAD;

	/* Unblock pending sample requests. */
	complete(&perfcnt->dump_comp);
}
