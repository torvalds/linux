// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "xe_device.h"
#include "xe_ggtt.h"
#include "xe_gt.h"
#include "xe_migrate.h"
#include "xe_sa.h"
#include "xe_tile.h"
#include "xe_tile_sysfs.h"
#include "xe_ttm_vram_mgr.h"
#include "xe_wa.h"

/**
 * DOC: Multi-tile Design
 *
 * Different vendors use the term "tile" a bit differently, but in the Intel
 * world, a 'tile' is pretty close to what most people would think of as being
 * a complete GPU.  When multiple GPUs are placed behind a single PCI device,
 * that's what is referred to as a "multi-tile device."  In such cases, pretty
 * much all hardware is replicated per-tile, although certain responsibilities
 * like PCI communication, reporting of interrupts to the OS, etc. are handled
 * solely by the "root tile."  A multi-tile platform takes care of tying the
 * tiles together in a way such that interrupt notifications from remote tiles
 * are forwarded to the root tile, the per-tile vram is combined into a single
 * address space, etc.
 *
 * In contrast, a "GT" (which officially stands for "Graphics Technology") is
 * the subset of a GPU/tile that is responsible for implementing graphics
 * and/or media operations.  The GT is where a lot of the driver implementation
 * happens since it's where the hardware engines, the execution units, and the
 * GuC all reside.
 *
 * Historically most Intel devices were single-tile devices that contained a
 * single GT.  PVC is an example of an Intel platform built on a multi-tile
 * design (i.e., multiple GPUs behind a single PCI device); each PVC tile only
 * has a single GT.  In contrast, platforms like MTL that have separate chips
 * for render and media IP are still only a single logical GPU, but the
 * graphics and media IP blocks are each exposed as a separate GT within that
 * single GPU.  This is important from a software perspective because multi-GT
 * platforms like MTL only replicate a subset of the GPU hardware and behave
 * differently than multi-tile platforms like PVC where nearly everything is
 * replicated.
 *
 * Per-tile functionality (shared by all GTs within the tile):
 *  - Complete 4MB MMIO space (containing SGunit/SoC registers, GT
 *    registers, display registers, etc.)
 *  - Global GTT
 *  - VRAM (if discrete)
 *  - Interrupt flows
 *  - Migration context
 *  - kernel batchbuffer pool
 *  - Primary GT
 *  - Media GT (if media version >= 13)
 *
 * Per-GT functionality:
 *  - GuC
 *  - Hardware engines
 *  - Programmable hardware units (subslices, EUs)
 *  - GSI subset of registers (multiple copies of these registers reside
 *    within the complete MMIO space provided by the tile, but at different
 *    offsets --- 0 for render, 0x380000 for media)
 *  - Multicast register steering
 *  - TLBs to cache page table translations
 *  - Reset capability
 *  - Low-level power management (e.g., C6)
 *  - Clock frequency
 *  - MOCS and PAT programming
 */

/**
 * xe_tile_alloc - Perform per-tile memory allocation
 * @tile: Tile to perform allocations for
 *
 * Allocates various per-tile data structures using DRM-managed allocations.
 * Does not touch the hardware.
 *
 * Returns -ENOMEM if allocations fail, otherwise 0.
 */
static int xe_tile_alloc(struct xe_tile *tile)
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

/**
 * xe_tile_init_early - Initialize the tile and primary GT
 * @tile: Tile to initialize
 * @xe: Parent Xe device
 * @id: Tile ID
 *
 * Initializes per-tile resources that don't require any interactions with the
 * hardware or any knowledge about the Graphics/Media IP version.
 *
 * Returns: 0 on success, negative error code on error.
 */
int xe_tile_init_early(struct xe_tile *tile, struct xe_device *xe, u8 id)
{
	int err;

	tile->xe = xe;
	tile->id = id;

	err = xe_tile_alloc(tile);
	if (err)
		return err;

	tile->primary_gt = xe_gt_alloc(tile);
	if (IS_ERR(tile->primary_gt))
		return PTR_ERR(tile->primary_gt);

	return 0;
}

static int tile_ttm_mgr_init(struct xe_tile *tile)
{
	struct xe_device *xe = tile_to_xe(tile);
	int err;

	if (tile->mem.vram.usable_size) {
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

	tile->mem.kernel_bb_pool = xe_sa_bo_manager_init(tile, SZ_1M, 16);
	if (IS_ERR(tile->mem.kernel_bb_pool))
		err = PTR_ERR(tile->mem.kernel_bb_pool);

	xe_wa_apply_tile_workarounds(tile);

	xe_tile_sysfs_init(tile);

err_mem_access:
	xe_device_mem_access_put(tile_to_xe(tile));
	return err;
}

void xe_tile_migrate_wait(struct xe_tile *tile)
{
	xe_migrate_wait(tile->migrate);
}
