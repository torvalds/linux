// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_bo_evict.h"

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_ggtt.h"
#include "xe_tile.h"

typedef int (*xe_pinned_fn)(struct xe_bo *bo);

static int xe_bo_apply_to_pinned(struct xe_device *xe,
				 struct list_head *pinned_list,
				 struct list_head *new_list,
				 const xe_pinned_fn pinned_fn)
{
	LIST_HEAD(still_in_list);
	struct xe_bo *bo;
	int ret = 0;

	spin_lock(&xe->pinned.lock);
	while (!ret) {
		bo = list_first_entry_or_null(pinned_list, typeof(*bo),
					      pinned_link);
		if (!bo)
			break;
		xe_bo_get(bo);
		list_move_tail(&bo->pinned_link, &still_in_list);
		spin_unlock(&xe->pinned.lock);

		ret = pinned_fn(bo);
		if (ret && pinned_list != new_list) {
			spin_lock(&xe->pinned.lock);
			/*
			 * We might no longer be pinned, since PM notifier can
			 * call this. If the pinned link is now empty, keep it
			 * that way.
			 */
			if (!list_empty(&bo->pinned_link))
				list_move(&bo->pinned_link, pinned_list);
			spin_unlock(&xe->pinned.lock);
		}
		xe_bo_put(bo);
		spin_lock(&xe->pinned.lock);
	}
	list_splice_tail(&still_in_list, new_list);
	spin_unlock(&xe->pinned.lock);

	return ret;
}

/**
 * xe_bo_notifier_prepare_all_pinned() - Pre-allocate the backing pages for all
 * pinned VRAM objects which need to be saved.
 * @xe: xe device
 *
 * Should be called from PM notifier when preparing for s3/s4.
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_bo_notifier_prepare_all_pinned(struct xe_device *xe)
{
	int ret;

	ret = xe_bo_apply_to_pinned(xe, &xe->pinned.early.kernel_bo_present,
				    &xe->pinned.early.kernel_bo_present,
				    xe_bo_notifier_prepare_pinned);
	if (!ret)
		ret = xe_bo_apply_to_pinned(xe, &xe->pinned.late.kernel_bo_present,
					    &xe->pinned.late.kernel_bo_present,
					    xe_bo_notifier_prepare_pinned);

	return ret;
}

/**
 * xe_bo_notifier_unprepare_all_pinned() - Remove the backing pages for all
 * pinned VRAM objects which have been restored.
 * @xe: xe device
 *
 * Should be called from PM notifier after exiting s3/s4 (either on success or
 * failure).
 */
void xe_bo_notifier_unprepare_all_pinned(struct xe_device *xe)
{
	(void)xe_bo_apply_to_pinned(xe, &xe->pinned.early.kernel_bo_present,
				    &xe->pinned.early.kernel_bo_present,
				    xe_bo_notifier_unprepare_pinned);

	(void)xe_bo_apply_to_pinned(xe, &xe->pinned.late.kernel_bo_present,
				    &xe->pinned.late.kernel_bo_present,
				    xe_bo_notifier_unprepare_pinned);
}

/**
 * xe_bo_evict_all_user - evict all non-pinned user BOs from VRAM
 * @xe: xe device
 *
 * Evict non-pinned user BOs (via GPU).
 *
 * Evict == move VRAM BOs to temporary (typically system) memory.
 */
int xe_bo_evict_all_user(struct xe_device *xe)
{
	struct ttm_device *bdev = &xe->ttm;
	u32 mem_type;
	int ret;

	/* User memory */
	for (mem_type = XE_PL_TT; mem_type <= XE_PL_VRAM1; ++mem_type) {
		struct ttm_resource_manager *man =
			ttm_manager_type(bdev, mem_type);

		/*
		 * On igpu platforms with flat CCS we need to ensure we save and restore any CCS
		 * state since this state lives inside graphics stolen memory which doesn't survive
		 * hibernation.
		 *
		 * This can be further improved by only evicting objects that we know have actually
		 * used a compression enabled PAT index.
		 */
		if (mem_type == XE_PL_TT && (IS_DGFX(xe) || !xe_device_has_flat_ccs(xe)))
			continue;

		if (man) {
			ret = ttm_resource_manager_evict_all(bdev, man);
			if (ret)
				return ret;
		}
	}

	return 0;
}

/**
 * xe_bo_evict_all - evict all BOs from VRAM
 * @xe: xe device
 *
 * Evict non-pinned user BOs first (via GPU), evict pinned external BOs next
 * (via GPU), wait for evictions, and finally evict pinned kernel BOs via CPU.
 * All eviction magic done via TTM calls.
 *
 * Evict == move VRAM BOs to temporary (typically system) memory.
 *
 * This function should be called before the device goes into a suspend state
 * where the VRAM loses power.
 */
int xe_bo_evict_all(struct xe_device *xe)
{
	struct xe_tile *tile;
	u8 id;
	int ret;

	ret = xe_bo_evict_all_user(xe);
	if (ret)
		return ret;

	ret = xe_bo_apply_to_pinned(xe, &xe->pinned.late.external,
				    &xe->pinned.late.external, xe_bo_evict_pinned);

	if (!ret)
		ret = xe_bo_apply_to_pinned(xe, &xe->pinned.late.kernel_bo_present,
					    &xe->pinned.late.evicted, xe_bo_evict_pinned);

	/*
	 * Wait for all user BO to be evicted as those evictions depend on the
	 * memory moved below.
	 */
	for_each_tile(tile, xe, id)
		xe_tile_migrate_wait(tile);

	if (ret)
		return ret;

	return xe_bo_apply_to_pinned(xe, &xe->pinned.early.kernel_bo_present,
				     &xe->pinned.early.evicted,
				     xe_bo_evict_pinned);
}

static int xe_bo_restore_and_map_ggtt(struct xe_bo *bo)
{
	int ret;

	ret = xe_bo_restore_pinned(bo);
	if (ret)
		return ret;

	if (bo->flags & XE_BO_FLAG_GGTT) {
		struct xe_tile *tile;
		u8 id;

		for_each_tile(tile, xe_bo_device(bo), id) {
			if (tile != bo->tile && !(bo->flags & XE_BO_FLAG_GGTTx(tile)))
				continue;

			xe_ggtt_map_bo_unlocked(tile->mem.ggtt, bo);
		}
	}

	return 0;
}

/**
 * xe_bo_restore_early - restore early phase kernel BOs to VRAM
 *
 * @xe: xe device
 *
 * Move kernel BOs from temporary (typically system) memory to VRAM via CPU. All
 * moves done via TTM calls.
 *
 * This function should be called early, before trying to init the GT, on device
 * resume.
 */
int xe_bo_restore_early(struct xe_device *xe)
{
	return xe_bo_apply_to_pinned(xe, &xe->pinned.early.evicted,
				     &xe->pinned.early.kernel_bo_present,
				     xe_bo_restore_and_map_ggtt);
}

/**
 * xe_bo_restore_late - restore pinned late phase BOs
 *
 * @xe: xe device
 *
 * Move pinned user and kernel BOs which can use blitter from temporary
 * (typically system) memory to VRAM. All moves done via TTM calls.
 *
 * This function should be called late, after GT init, on device resume.
 */
int xe_bo_restore_late(struct xe_device *xe)
{
	struct xe_tile *tile;
	int ret, id;

	ret = xe_bo_apply_to_pinned(xe, &xe->pinned.late.evicted,
				    &xe->pinned.late.kernel_bo_present,
				    xe_bo_restore_and_map_ggtt);

	for_each_tile(tile, xe, id)
		xe_tile_migrate_wait(tile);

	if (ret)
		return ret;

	if (!IS_DGFX(xe))
		return 0;

	/* Pinned user memory in VRAM should be validated on resume */
	ret = xe_bo_apply_to_pinned(xe, &xe->pinned.late.external,
				    &xe->pinned.late.external,
				    xe_bo_restore_pinned);

	/* Wait for restore to complete */
	for_each_tile(tile, xe, id)
		xe_tile_migrate_wait(tile);

	return ret;
}

static void xe_bo_pci_dev_remove_pinned(struct xe_device *xe)
{
	struct xe_tile *tile;
	unsigned int id;

	(void)xe_bo_apply_to_pinned(xe, &xe->pinned.late.external,
				    &xe->pinned.late.external,
				    xe_bo_dma_unmap_pinned);
	for_each_tile(tile, xe, id)
		xe_tile_migrate_wait(tile);
}

/**
 * xe_bo_pci_dev_remove_all() - Handle bos when the pci_device is about to be removed
 * @xe: The xe device.
 *
 * On pci_device removal we need to drop all dma mappings and move
 * the data of exported bos out to system. This includes SVM bos and
 * exported dma-buf bos. This is done by evicting all bos, but
 * the evict placement in xe_evict_flags() is chosen such that all
 * bos except those mentioned are purged, and thus their memory
 * is released.
 *
 * For pinned bos, we're unmapping dma.
 */
void xe_bo_pci_dev_remove_all(struct xe_device *xe)
{
	unsigned int mem_type;

	/*
	 * Move pagemap bos and exported dma-buf to system, and
	 * purge everything else.
	 */
	for (mem_type = XE_PL_VRAM1; mem_type >= XE_PL_TT; --mem_type) {
		struct ttm_resource_manager *man =
			ttm_manager_type(&xe->ttm, mem_type);

		if (man) {
			int ret = ttm_resource_manager_evict_all(&xe->ttm, man);

			drm_WARN_ON(&xe->drm, ret);
		}
	}

	xe_bo_pci_dev_remove_pinned(xe);
}

static void xe_bo_pinned_fini(void *arg)
{
	struct xe_device *xe = arg;

	(void)xe_bo_apply_to_pinned(xe, &xe->pinned.late.kernel_bo_present,
				    &xe->pinned.late.kernel_bo_present,
				    xe_bo_dma_unmap_pinned);
	(void)xe_bo_apply_to_pinned(xe, &xe->pinned.early.kernel_bo_present,
				    &xe->pinned.early.kernel_bo_present,
				    xe_bo_dma_unmap_pinned);
}

/**
 * xe_bo_pinned_init() - Initialize pinned bo tracking
 * @xe: The xe device.
 *
 * Initializes the lists and locks required for pinned bo
 * tracking and registers a callback to dma-unmap
 * any remaining pinned bos on pci device removal.
 *
 * Return: %0 on success, negative error code on error.
 */
int xe_bo_pinned_init(struct xe_device *xe)
{
	spin_lock_init(&xe->pinned.lock);
	INIT_LIST_HEAD(&xe->pinned.early.kernel_bo_present);
	INIT_LIST_HEAD(&xe->pinned.early.evicted);
	INIT_LIST_HEAD(&xe->pinned.late.kernel_bo_present);
	INIT_LIST_HEAD(&xe->pinned.late.evicted);
	INIT_LIST_HEAD(&xe->pinned.late.external);

	return devm_add_action_or_reset(xe->drm.dev, xe_bo_pinned_fini, xe);
}
