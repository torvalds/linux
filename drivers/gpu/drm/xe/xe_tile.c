// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "xe_device.h"
#include "xe_ggtt.h"
#include "xe_sa.h"
#include "xe_tile.h"
#include "xe_ttm_vram_mgr.h"

/**
 * xe_tile_alloc - Perform per-tile memory allocation
 * @tile: Tile to perform allocations for
 *
 * Allocates various per-tile data structures using DRM-managed allocations.
 * Does not touch the hardware.
 *
 * Returns -ENOMEM if allocations fail, otherwise 0.
 */
int xe_tile_alloc(struct xe_tile *tile)
{
	struct drm_device *drm = &tile_to_xe(tile)->drm;

	tile->mem.ggtt = drmm_kzalloc(drm, sizeof(*tile->mem.ggtt),
				      GFP_KERNEL);
	if (!tile->mem.ggtt)
		return -ENOMEM;
	tile->mem.ggtt->tile = tile;

	tile->mem.vram_mgr = drmm_kzalloc(drm, sizeof(*tile->mem.vram_mgr), GFP_KERNEL);
	if (!tile->mem.vram_mgr)
		return -ENOMEM;

	return 0;
}

static int tile_ttm_mgr_init(struct xe_tile *tile)
{
	struct xe_device *xe = tile_to_xe(tile);
	int err;

	if (tile->mem.vram.size) {
		err = xe_ttm_vram_mgr_init(tile, tile->mem.vram_mgr);
		if (err)
			return err;
		xe->info.mem_region_mask |= BIT(tile->id) << 1;
	}

	return 0;
}

/**
 * xe_tile_init_noalloc - Init tile up to the point where allocations can happen.
 * @tile: The tile to initialize.
 *
 * This function prepares the tile to allow memory allocations to VRAM, but is
 * not allowed to allocate memory itself. This state is useful for display
 * readout, because the inherited display framebuffer will otherwise be
 * overwritten as it is usually put at the start of VRAM.
 *
 * Note that since this is tile initialization, it should not perform any
 * GT-specific operations, and thus does not need to hold GT forcewake.
 *
 * Returns: 0 on success, negative error code on error.
 */
int xe_tile_init_noalloc(struct xe_tile *tile)
{
	int err;

	xe_device_mem_access_get(tile_to_xe(tile));

	err = tile_ttm_mgr_init(tile);
	if (err)
		goto err_mem_access;

	err = xe_ggtt_init_noalloc(tile->mem.ggtt);
	if (err)
		goto err_mem_access;

	tile->mem.kernel_bb_pool = xe_sa_bo_manager_init(tile, SZ_1M, 16);
	if (IS_ERR(tile->mem.kernel_bb_pool))
		err = PTR_ERR(tile->mem.kernel_bb_pool);

err_mem_access:
	xe_device_mem_access_put(tile_to_xe(tile));
	return err;
}
