// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_tt.h>

#include "i915_drv.h"
#include "intel_memory_region.h"
#include "intel_region_ttm.h"

#include "gem/i915_gem_region.h"
#include "gem/i915_gem_ttm.h"
#include "gem/i915_gem_ttm_move.h"
#include "gem/i915_gem_ttm_pm.h"

/**
 * i915_ttm_backup_free - Free any backup attached to this object
 * @obj: The object whose backup is to be freed.
 */
void i915_ttm_backup_free(struct drm_i915_gem_object *obj)
{
	if (obj->ttm.backup) {
		i915_gem_object_put(obj->ttm.backup);
		obj->ttm.backup = NULL;
	}
}

/**
 * struct i915_gem_ttm_pm_apply - Apply-to-region subclass for restore
 * @base: The i915_gem_apply_to_region we derive from.
 * @allow_gpu: Whether using the gpu blitter is allowed.
 * @backup_pinned: On backup, backup also pinned objects.
 */
struct i915_gem_ttm_pm_apply {
	struct i915_gem_apply_to_region base;
	bool allow_gpu : 1;
	bool backup_pinned : 1;
};

static int i915_ttm_backup(struct i915_gem_apply_to_region *apply,
			   struct drm_i915_gem_object *obj)
{
	struct i915_gem_ttm_pm_apply *pm_apply =
		container_of(apply, typeof(*pm_apply), base);
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);
	struct ttm_buffer_object *backup_bo;
	struct drm_i915_private *i915 =
		container_of(bo->bdev, typeof(*i915), bdev);
	struct drm_i915_gem_object *backup;
	struct ttm_operation_ctx ctx = {};
	unsigned int flags;
	int err = 0;

	if (bo->resource->mem_type == I915_PL_SYSTEM || obj->ttm.backup)
		return 0;

	if (pm_apply->allow_gpu && i915_gem_object_evictable(obj))
		return ttm_bo_validate(bo, i915_ttm_sys_placement(), &ctx);

	if (!pm_apply->backup_pinned ||
	    (pm_apply->allow_gpu && (obj->flags & I915_BO_ALLOC_PM_EARLY)))
		return 0;

	if (obj->flags & I915_BO_ALLOC_PM_VOLATILE)
		return 0;

	/*
	 * It seems that we might have some framebuffers still pinned at this
	 * stage, but for such objects we might also need to deal with the CCS
	 * aux state. Make sure we force the save/restore of the CCS state,
	 * otherwise we might observe display corruption, when returning from
	 * suspend.
	 */
	flags = 0;
	if (i915_gem_object_needs_ccs_pages(obj)) {
		WARN_ON_ONCE(!i915_gem_object_is_framebuffer(obj));
		WARN_ON_ONCE(!pm_apply->allow_gpu);

		flags = I915_BO_ALLOC_CCS_AUX;
	}
	backup = i915_gem_object_create_region(i915->mm.regions[INTEL_REGION_SMEM],
					       obj->base.size, 0, flags);
	if (IS_ERR(backup))
		return PTR_ERR(backup);

	err = i915_gem_object_lock(backup, apply->ww);
	if (err)
		goto out_no_lock;

	backup_bo = i915_gem_to_ttm(backup);
	err = ttm_tt_populate(backup_bo->bdev, backup_bo->ttm, &ctx);
	if (err)
		goto out_no_populate;

	err = i915_gem_obj_copy_ttm(backup, obj, pm_apply->allow_gpu, false);
	if (err) {
		drm_err(&i915->drm,
			"Unable to copy from device to system memory, err:%pe\n",
			ERR_PTR(err));
		goto out_no_populate;
	}
	ttm_bo_wait_ctx(backup_bo, &ctx);

	obj->ttm.backup = backup;
	return 0;

out_no_populate:
	i915_gem_ww_unlock_single(backup);
out_no_lock:
	i915_gem_object_put(backup);

	return err;
}

static int i915_ttm_recover(struct i915_gem_apply_to_region *apply,
			    struct drm_i915_gem_object *obj)
{
	i915_ttm_backup_free(obj);
	return 0;
}

/**
 * i915_ttm_recover_region - Free the backup of all objects of a region
 * @mr: The memory region
 *
 * Checks all objects of a region if there is backup attached and if so
 * frees that backup. Typically this is called to recover after a partially
 * performed backup.
 */
void i915_ttm_recover_region(struct intel_memory_region *mr)
{
	static const struct i915_gem_apply_to_region_ops recover_ops = {
		.process_obj = i915_ttm_recover,
	};
	struct i915_gem_apply_to_region apply = {.ops = &recover_ops};
	int ret;

	ret = i915_gem_process_region(mr, &apply);
	GEM_WARN_ON(ret);
}

/**
 * i915_ttm_backup_region - Back up all objects of a region to smem.
 * @mr: The memory region
 * @allow_gpu: Whether to allow the gpu blitter for this backup.
 * @backup_pinned: Backup also pinned objects.
 *
 * Loops over all objects of a region and either evicts them if they are
 * evictable or backs them up using a backup object if they are pinned.
 *
 * Return: Zero on success. Negative error code on error.
 */
int i915_ttm_backup_region(struct intel_memory_region *mr, u32 flags)
{
	static const struct i915_gem_apply_to_region_ops backup_ops = {
		.process_obj = i915_ttm_backup,
	};
	struct i915_gem_ttm_pm_apply pm_apply = {
		.base = {.ops = &backup_ops},
		.allow_gpu = flags & I915_TTM_BACKUP_ALLOW_GPU,
		.backup_pinned = flags & I915_TTM_BACKUP_PINNED,
	};

	return i915_gem_process_region(mr, &pm_apply.base);
}

static int i915_ttm_restore(struct i915_gem_apply_to_region *apply,
			    struct drm_i915_gem_object *obj)
{
	struct i915_gem_ttm_pm_apply *pm_apply =
		container_of(apply, typeof(*pm_apply), base);
	struct drm_i915_gem_object *backup = obj->ttm.backup;
	struct ttm_buffer_object *backup_bo = i915_gem_to_ttm(backup);
	struct ttm_operation_ctx ctx = {};
	int err;

	if (!backup)
		return 0;

	if (!pm_apply->allow_gpu && !(obj->flags & I915_BO_ALLOC_PM_EARLY))
		return 0;

	err = i915_gem_object_lock(backup, apply->ww);
	if (err)
		return err;

	/* Content may have been swapped. */
	err = ttm_tt_populate(backup_bo->bdev, backup_bo->ttm, &ctx);
	if (!err) {
		err = i915_gem_obj_copy_ttm(obj, backup, pm_apply->allow_gpu,
					    false);
		GEM_WARN_ON(err);
		ttm_bo_wait_ctx(backup_bo, &ctx);

		obj->ttm.backup = NULL;
		err = 0;
	}

	i915_gem_ww_unlock_single(backup);

	if (!err)
		i915_gem_object_put(backup);

	return err;
}

/**
 * i915_ttm_restore_region - Restore backed-up objects of a region from smem.
 * @mr: The memory region
 * @allow_gpu: Whether to allow the gpu blitter to recover.
 *
 * Loops over all objects of a region and if they are backed-up, restores
 * them from smem.
 *
 * Return: Zero on success. Negative error code on error.
 */
int i915_ttm_restore_region(struct intel_memory_region *mr, u32 flags)
{
	static const struct i915_gem_apply_to_region_ops restore_ops = {
		.process_obj = i915_ttm_restore,
	};
	struct i915_gem_ttm_pm_apply pm_apply = {
		.base = {.ops = &restore_ops},
		.allow_gpu = flags & I915_TTM_BACKUP_ALLOW_GPU,
	};

	return i915_gem_process_region(mr, &pm_apply.base);
}
