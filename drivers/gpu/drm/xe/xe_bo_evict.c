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

		xe_bo_lock(bo, false);
		ret = pinned_fn(bo);
		if (ret && pinned_list != new_list) {
			spin_lock(&xe->pinned.lock);
			list_move(&bo->pinned_link, pinned_list);
			spin_unlock(&xe->pinned.lock);
		}
		xe_bo_unlock(bo);
		xe_bo_put(bo);
		spin_lock(&xe->pinned.lock);
	}
	list_splice_tail(&still_in_list, new_list);
	spin_unlock(&xe->pinned.lock);

	return ret;
}

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
	struct xe_tile *tile;
	u32 mem_type;
	u8 id;
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

	ret = xe_bo_apply_to_pinned(xe, &xe->pinned.external_vram,
				    &xe->pinned.external_vram,
				    xe_bo_evict_pinned);

	/*
	 * Wait for all user BO to be evicted as those evictions depend on the
	 * memory moved below.
	 */
	for_each_tile(tile, xe, id)
		xe_tile_migrate_wait(tile);

	if (ret)
		return ret;

	return xe_bo_apply_to_pinned(xe, &xe->pinned.kernel_bo_present,
				     &xe->pinned.evicted,
				     xe_bo_evict_pinned);
}

static int xe_bo_restore_and_map_ggtt(struct xe_bo *bo)
{
	struct xe_device *xe = xe_bo_device(bo);
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

			mutex_lock(&tile->mem.ggtt->lock);
			xe_ggtt_map_bo(tile->mem.ggtt, bo);
			mutex_unlock(&tile->mem.ggtt->lock);
		}
	}

	/*
	 * We expect validate to trigger a move VRAM and our move code
	 * should setup the iosys map.
	 */
	xe_assert(xe, !iosys_map_is_null(&bo->vmap));

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
	return xe_bo_apply_to_pinned(xe, &xe->pinned.evicted,
				     &xe->pinned.kernel_bo_present,
				     xe_bo_restore_and_map_ggtt);
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
	struct xe_tile *tile;
	int ret, id;

	if (!IS_DGFX(xe))
		return 0;

	/* Pinned user memory in VRAM should be validated on resume */
	ret = xe_bo_apply_to_pinned(xe, &xe->pinned.external_vram,
				    &xe->pinned.external_vram,
				    xe_bo_restore_pinned);

	/* Wait for restore to complete */
	for_each_tile(tile, xe, id)
		xe_tile_migrate_wait(tile);

	return ret;
}
