// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_bo_evict.h"

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_ggtt.h"
#include "xe_tile.h"

/**
 * xe_bo_evict_all - evict all BOs from VRAM
 *
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
	struct ttm_device *bdev = &xe->ttm;
	struct xe_bo *bo;
	struct xe_tile *tile;
	struct list_head still_in_list;
	u32 mem_type;
	u8 id;
	int ret;

	if (!IS_DGFX(xe))
		return 0;

	/* User memory */
	for (mem_type = XE_PL_VRAM0; mem_type <= XE_PL_VRAM1; ++mem_type) {
		struct ttm_resource_manager *man =
			ttm_manager_type(bdev, mem_type);

		if (man) {
			ret = ttm_resource_manager_evict_all(bdev, man);
			if (ret)
				return ret;
		}
	}

	/* Pinned user memory in VRAM */
	INIT_LIST_HEAD(&still_in_list);
	spin_lock(&xe->pinned.lock);
	for (;;) {
		bo = list_first_entry_or_null(&xe->pinned.external_vram,
					      typeof(*bo), pinned_link);
		if (!bo)
			break;
		xe_bo_get(bo);
		list_move_tail(&bo->pinned_link, &still_in_list);
		spin_unlock(&xe->pinned.lock);

		xe_bo_lock(bo, false);
		ret = xe_bo_evict_pinned(bo);
		xe_bo_unlock(bo);
		xe_bo_put(bo);
		if (ret) {
			spin_lock(&xe->pinned.lock);
			list_splice_tail(&still_in_list,
					 &xe->pinned.external_vram);
			spin_unlock(&xe->pinned.lock);
			return ret;
		}

		spin_lock(&xe->pinned.lock);
	}
	list_splice_tail(&still_in_list, &xe->pinned.external_vram);
	spin_unlock(&xe->pinned.lock);

	/*
	 * Wait for all user BO to be evicted as those evictions depend on the
	 * memory moved below.
	 */
	for_each_tile(tile, xe, id)
		xe_tile_migrate_wait(tile);

	spin_lock(&xe->pinned.lock);
	for (;;) {
		bo = list_first_entry_or_null(&xe->pinned.kernel_bo_present,
					      typeof(*bo), pinned_link);
		if (!bo)
			break;
		xe_bo_get(bo);
		list_move_tail(&bo->pinned_link, &xe->pinned.evicted);
		spin_unlock(&xe->pinned.lock);

		xe_bo_lock(bo, false);
		ret = xe_bo_evict_pinned(bo);
		xe_bo_unlock(bo);
		xe_bo_put(bo);
		if (ret)
			return ret;

		spin_lock(&xe->pinned.lock);
	}
	spin_unlock(&xe->pinned.lock);

	return 0;
}

/**
 * xe_bo_restore_kernel - restore kernel BOs to VRAM
 *
 * @xe: xe device
 *
 * Move kernel BOs from temporary (typically system) memory to VRAM via CPU. All
 * moves done via TTM calls.
 *
 * This function should be called early, before trying to init the GT, on device
 * resume.
 */
int xe_bo_restore_kernel(struct xe_device *xe)
{
	struct xe_bo *bo;
	int ret;

	if (!IS_DGFX(xe))
		return 0;

	spin_lock(&xe->pinned.lock);
	for (;;) {
		bo = list_first_entry_or_null(&xe->pinned.evicted,
					      typeof(*bo), pinned_link);
		if (!bo)
			break;
		xe_bo_get(bo);
		list_move_tail(&bo->pinned_link, &xe->pinned.kernel_bo_present);
		spin_unlock(&xe->pinned.lock);

		xe_bo_lock(bo, false);
		ret = xe_bo_restore_pinned(bo);
		xe_bo_unlock(bo);
		if (ret) {
			xe_bo_put(bo);
			return ret;
		}

		if (bo->flags & XE_BO_CREATE_GGTT_BIT) {
			struct xe_tile *tile = bo->tile;

			mutex_lock(&tile->mem.ggtt->lock);
			xe_ggtt_map_bo(tile->mem.ggtt, bo);
			mutex_unlock(&tile->mem.ggtt->lock);
		}

		/*
		 * We expect validate to trigger a move VRAM and our move code
		 * should setup the iosys map.
		 */
		xe_assert(xe, !iosys_map_is_null(&bo->vmap));
		xe_assert(xe, xe_bo_is_vram(bo));

		xe_bo_put(bo);

		spin_lock(&xe->pinned.lock);
	}
	spin_unlock(&xe->pinned.lock);

	return 0;
}

/**
 * xe_bo_restore_user - restore pinned user BOs to VRAM
 *
 * @xe: xe device
 *
 * Move pinned user BOs from temporary (typically system) memory to VRAM via
 * CPU. All moves done via TTM calls.
 *
 * This function should be called late, after GT init, on device resume.
 */
int xe_bo_restore_user(struct xe_device *xe)
{
	struct xe_bo *bo;
	struct xe_tile *tile;
	struct list_head still_in_list;
	u8 id;
	int ret;

	if (!IS_DGFX(xe))
		return 0;

	/* Pinned user memory in VRAM should be validated on resume */
	INIT_LIST_HEAD(&still_in_list);
	spin_lock(&xe->pinned.lock);
	for (;;) {
		bo = list_first_entry_or_null(&xe->pinned.external_vram,
					      typeof(*bo), pinned_link);
		if (!bo)
			break;
		list_move_tail(&bo->pinned_link, &still_in_list);
		xe_bo_get(bo);
		spin_unlock(&xe->pinned.lock);

		xe_bo_lock(bo, false);
		ret = xe_bo_restore_pinned(bo);
		xe_bo_unlock(bo);
		xe_bo_put(bo);
		if (ret) {
			spin_lock(&xe->pinned.lock);
			list_splice_tail(&still_in_list,
					 &xe->pinned.external_vram);
			spin_unlock(&xe->pinned.lock);
			return ret;
		}

		spin_lock(&xe->pinned.lock);
	}
	list_splice_tail(&still_in_list, &xe->pinned.external_vram);
	spin_unlock(&xe->pinned.lock);

	/* Wait for validate to complete */
	for_each_tile(tile, xe, id)
		xe_tile_migrate_wait(tile);

	return 0;
}
