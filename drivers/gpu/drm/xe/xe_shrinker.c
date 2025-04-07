// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <linux/shrinker.h>

#include <drm/ttm/ttm_backup.h>
#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_tt.h>

#include "xe_bo.h"
#include "xe_pm.h"
#include "xe_shrinker.h"

/**
 * struct xe_shrinker - per-device shrinker
 * @xe: Back pointer to the device.
 * @lock: Lock protecting accounting.
 * @shrinkable_pages: Number of pages that are currently shrinkable.
 * @purgeable_pages: Number of pages that are currently purgeable.
 * @shrink: Pointer to the mm shrinker.
 * @pm_worker: Worker to wake up the device if required.
 */
struct xe_shrinker {
	struct xe_device *xe;
	rwlock_t lock;
	long shrinkable_pages;
	long purgeable_pages;
	struct shrinker *shrink;
	struct work_struct pm_worker;
};

static struct xe_shrinker *to_xe_shrinker(struct shrinker *shrink)
{
	return shrink->private_data;
}

/**
 * xe_shrinker_mod_pages() - Modify shrinker page accounting
 * @shrinker: Pointer to the struct xe_shrinker.
 * @shrinkable: Shrinkable pages delta. May be negative.
 * @purgeable: Purgeable page delta. May be negative.
 *
 * Modifies the shrinkable and purgeable pages accounting.
 */
void
xe_shrinker_mod_pages(struct xe_shrinker *shrinker, long shrinkable, long purgeable)
{
	write_lock(&shrinker->lock);
	shrinker->shrinkable_pages += shrinkable;
	shrinker->purgeable_pages += purgeable;
	write_unlock(&shrinker->lock);
}

static s64 xe_shrinker_walk(struct xe_device *xe,
			    struct ttm_operation_ctx *ctx,
			    const struct xe_bo_shrink_flags flags,
			    unsigned long to_scan, unsigned long *scanned)
{
	unsigned int mem_type;
	s64 freed = 0, lret;

	for (mem_type = XE_PL_SYSTEM; mem_type <= XE_PL_TT; ++mem_type) {
		struct ttm_resource_manager *man = ttm_manager_type(&xe->ttm, mem_type);
		struct ttm_bo_lru_cursor curs;
		struct ttm_buffer_object *ttm_bo;

		if (!man || !man->use_tt)
			continue;

		ttm_bo_lru_for_each_reserved_guarded(&curs, man, ctx, ttm_bo) {
			if (!ttm_bo_shrink_suitable(ttm_bo, ctx))
				continue;

			lret = xe_bo_shrink(ctx, ttm_bo, flags, scanned);
			if (lret < 0)
				return lret;

			freed += lret;
			if (*scanned >= to_scan)
				break;
		}
	}

	return freed;
}

static unsigned long
xe_shrinker_count(struct shrinker *shrink, struct shrink_control *sc)
{
	struct xe_shrinker *shrinker = to_xe_shrinker(shrink);
	unsigned long num_pages;
	bool can_backup = !!(sc->gfp_mask & __GFP_FS);

	num_pages = ttm_backup_bytes_avail() >> PAGE_SHIFT;
	read_lock(&shrinker->lock);

	if (can_backup)
		num_pages = min_t(unsigned long, num_pages, shrinker->shrinkable_pages);
	else
		num_pages = 0;

	num_pages += shrinker->purgeable_pages;
	read_unlock(&shrinker->lock);

	return num_pages ? num_pages : SHRINK_EMPTY;
}

/*
 * Check if we need runtime pm, and if so try to grab a reference if
 * already active. If grabbing a reference fails, queue a worker that
 * does it for us outside of reclaim, but don't wait for it to complete.
 * If bo shrinking needs an rpm reference and we don't have it (yet),
 * that bo will be skipped anyway.
 */
static bool xe_shrinker_runtime_pm_get(struct xe_shrinker *shrinker, bool force,
				       unsigned long nr_to_scan, bool can_backup)
{
	struct xe_device *xe = shrinker->xe;

	if (IS_DGFX(xe) || !xe_device_has_flat_ccs(xe) ||
	    !ttm_backup_bytes_avail())
		return false;

	if (!force) {
		read_lock(&shrinker->lock);
		force = (nr_to_scan > shrinker->purgeable_pages && can_backup);
		read_unlock(&shrinker->lock);
		if (!force)
			return false;
	}

	if (!xe_pm_runtime_get_if_active(xe)) {
		if (xe_rpm_reclaim_safe(xe) && !ttm_bo_shrink_avoid_wait()) {
			xe_pm_runtime_get(xe);
			return true;
		}
		queue_work(xe->unordered_wq, &shrinker->pm_worker);
		return false;
	}

	return true;
}

static void xe_shrinker_runtime_pm_put(struct xe_shrinker *shrinker, bool runtime_pm)
{
	if (runtime_pm)
		xe_pm_runtime_put(shrinker->xe);
}

static unsigned long xe_shrinker_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	struct xe_shrinker *shrinker = to_xe_shrinker(shrink);
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.no_wait_gpu = ttm_bo_shrink_avoid_wait(),
	};
	unsigned long nr_to_scan, nr_scanned = 0, freed = 0;
	struct xe_bo_shrink_flags shrink_flags = {
		.purge = true,
		/* Don't request writeback without __GFP_IO. */
		.writeback = !ctx.no_wait_gpu && (sc->gfp_mask & __GFP_IO),
	};
	bool runtime_pm;
	bool purgeable;
	bool can_backup = !!(sc->gfp_mask & __GFP_FS);
	s64 lret;

	nr_to_scan = sc->nr_to_scan;

	read_lock(&shrinker->lock);
	purgeable = !!shrinker->purgeable_pages;
	read_unlock(&shrinker->lock);

	/* Might need runtime PM. Try to wake early if it looks like it. */
	runtime_pm = xe_shrinker_runtime_pm_get(shrinker, false, nr_to_scan, can_backup);

	if (purgeable && nr_scanned < nr_to_scan) {
		lret = xe_shrinker_walk(shrinker->xe, &ctx, shrink_flags,
					nr_to_scan, &nr_scanned);
		if (lret >= 0)
			freed += lret;
	}

	sc->nr_scanned = nr_scanned;
	if (nr_scanned >= nr_to_scan || !can_backup)
		goto out;

	/* If we didn't wake before, try to do it now if needed. */
	if (!runtime_pm)
		runtime_pm = xe_shrinker_runtime_pm_get(shrinker, true, 0, can_backup);

	shrink_flags.purge = false;
	lret = xe_shrinker_walk(shrinker->xe, &ctx, shrink_flags,
				nr_to_scan, &nr_scanned);
	if (lret >= 0)
		freed += lret;

	sc->nr_scanned = nr_scanned;
out:
	xe_shrinker_runtime_pm_put(shrinker, runtime_pm);
	return nr_scanned ? freed : SHRINK_STOP;
}

/* Wake up the device for shrinking. */
static void xe_shrinker_pm(struct work_struct *work)
{
	struct xe_shrinker *shrinker =
		container_of(work, typeof(*shrinker), pm_worker);

	xe_pm_runtime_get(shrinker->xe);
	xe_pm_runtime_put(shrinker->xe);
}

/**
 * xe_shrinker_create() - Create an xe per-device shrinker
 * @xe: Pointer to the xe device.
 *
 * Returns: A pointer to the created shrinker on success,
 * Negative error code on failure.
 */
struct xe_shrinker *xe_shrinker_create(struct xe_device *xe)
{
	struct xe_shrinker *shrinker = kzalloc(sizeof(*shrinker), GFP_KERNEL);

	if (!shrinker)
		return ERR_PTR(-ENOMEM);

	shrinker->shrink = shrinker_alloc(0, "xe system shrinker");
	if (!shrinker->shrink) {
		kfree(shrinker);
		return ERR_PTR(-ENOMEM);
	}

	INIT_WORK(&shrinker->pm_worker, xe_shrinker_pm);
	shrinker->xe = xe;
	rwlock_init(&shrinker->lock);
	shrinker->shrink->count_objects = xe_shrinker_count;
	shrinker->shrink->scan_objects = xe_shrinker_scan;
	shrinker->shrink->private_data = shrinker;
	shrinker_register(shrinker->shrink);

	return shrinker;
}

/**
 * xe_shrinker_destroy() - Destroy an xe per-device shrinker
 * @shrinker: Pointer to the shrinker to destroy.
 */
void xe_shrinker_destroy(struct xe_shrinker *shrinker)
{
	xe_assert(shrinker->xe, !shrinker->shrinkable_pages);
	xe_assert(shrinker->xe, !shrinker->purgeable_pages);
	shrinker_free(shrinker->shrink);
	flush_work(&shrinker->pm_worker);
	kfree(shrinker);
}
