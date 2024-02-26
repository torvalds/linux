/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_DEVICE_H_
#define _XE_DEVICE_H_

struct xe_exec_queue;
struct xe_file;

#include <drm/drm_util.h>

#include "regs/xe_gpu_commands.h"
#include "xe_device_types.h"
#include "xe_force_wake.h"
#include "xe_macros.h"

#ifdef CONFIG_LOCKDEP
extern struct lockdep_map xe_device_mem_access_lockdep_map;
#endif

static inline struct xe_device *to_xe_device(const struct drm_device *dev)
{
	return container_of(dev, struct xe_device, drm);
}

static inline struct xe_device *pdev_to_xe_device(struct pci_dev *pdev)
{
	return pci_get_drvdata(pdev);
}

static inline struct xe_device *ttm_to_xe_device(struct ttm_device *ttm)
{
	return container_of(ttm, struct xe_device, ttm);
}

struct xe_device *xe_device_create(struct pci_dev *pdev,
				   const struct pci_device_id *ent);
int xe_device_probe_early(struct xe_device *xe);
int xe_device_probe(struct xe_device *xe);
void xe_device_remove(struct xe_device *xe);
void xe_device_shutdown(struct xe_device *xe);

void xe_device_wmb(struct xe_device *xe);

static inline struct xe_file *to_xe_file(const struct drm_file *file)
{
	return file->driver_priv;
}

static inline struct xe_tile *xe_device_get_root_tile(struct xe_device *xe)
{
	return &xe->tiles[0];
}

#define XE_MAX_GT_PER_TILE 2

static inline struct xe_gt *xe_tile_get_gt(struct xe_tile *tile, u8 gt_id)
{
	if (drm_WARN_ON(&tile_to_xe(tile)->drm, gt_id > XE_MAX_GT_PER_TILE))
		gt_id = 0;

	return gt_id ? tile->media_gt : tile->primary_gt;
}

static inline struct xe_gt *xe_device_get_gt(struct xe_device *xe, u8 gt_id)
{
	struct xe_tile *root_tile = xe_device_get_root_tile(xe);
	struct xe_gt *gt;

	/*
	 * FIXME: This only works for now because multi-tile and standalone
	 * media are mutually exclusive on the platforms we have today.
	 *
	 * id => GT mapping may change once we settle on how we want to handle
	 * our UAPI.
	 */
	if (MEDIA_VER(xe) >= 13) {
		gt = xe_tile_get_gt(root_tile, gt_id);
	} else {
		if (drm_WARN_ON(&xe->drm, gt_id > XE_MAX_TILES_PER_DEVICE))
			gt_id = 0;

		gt = xe->tiles[gt_id].primary_gt;
	}

	if (!gt)
		return NULL;

	drm_WARN_ON(&xe->drm, gt->info.id != gt_id);
	drm_WARN_ON(&xe->drm, gt->info.type == XE_GT_TYPE_UNINITIALIZED);

	return gt;
}

/*
 * Provide a GT structure suitable for performing non-GT MMIO operations against
 * the primary tile.  Primarily intended for early tile initialization, display
 * handling, top-most interrupt enable/disable, etc.  Since anything using the
 * MMIO handle returned by this function doesn't need GSI offset translation,
 * we'll return the primary GT from the root tile.
 *
 * FIXME: Fix the driver design so that 'gt' isn't the target of all MMIO
 * operations.
 *
 * Returns the primary gt of the root tile.
 */
static inline struct xe_gt *xe_root_mmio_gt(struct xe_device *xe)
{
	return xe_device_get_root_tile(xe)->primary_gt;
}

static inline bool xe_device_uc_enabled(struct xe_device *xe)
{
	return !xe->info.force_execlist;
}

#define for_each_tile(tile__, xe__, id__) \
	for ((id__) = 0; (id__) < (xe__)->info.tile_count; (id__)++) \
		for_each_if((tile__) = &(xe__)->tiles[(id__)])

#define for_each_remote_tile(tile__, xe__, id__) \
	for ((id__) = 1; (id__) < (xe__)->info.tile_count; (id__)++) \
		for_each_if((tile__) = &(xe__)->tiles[(id__)])

/*
 * FIXME: This only works for now since multi-tile and standalone media
 * happen to be mutually exclusive.  Future platforms may change this...
 */
#define for_each_gt(gt__, xe__, id__) \
	for ((id__) = 0; (id__) < (xe__)->info.gt_count; (id__)++) \
		for_each_if((gt__) = xe_device_get_gt((xe__), (id__)))

static inline struct xe_force_wake *gt_to_fw(struct xe_gt *gt)
{
	return &gt->mmio.fw;
}

void xe_device_mem_access_get(struct xe_device *xe);
bool xe_device_mem_access_get_if_ongoing(struct xe_device *xe);
void xe_device_mem_access_put(struct xe_device *xe);

void xe_device_assert_mem_access(struct xe_device *xe);
bool xe_device_mem_access_ongoing(struct xe_device *xe);

static inline bool xe_device_in_fault_mode(struct xe_device *xe)
{
	return xe->usm.num_vm_in_fault_mode != 0;
}

static inline bool xe_device_in_non_fault_mode(struct xe_device *xe)
{
	return xe->usm.num_vm_in_non_fault_mode != 0;
}

static inline bool xe_device_has_flat_ccs(struct xe_device *xe)
{
	return xe->info.has_flat_ccs;
}

static inline bool xe_device_has_sriov(struct xe_device *xe)
{
	return xe->info.has_sriov;
}

static inline bool xe_device_has_memirq(struct xe_device *xe)
{
	return GRAPHICS_VERx100(xe) >= 1250;
}

u32 xe_device_ccs_bytes(struct xe_device *xe, u64 size);

void xe_device_snapshot_print(struct xe_device *xe, struct drm_printer *p);

u64 xe_device_canonicalize_addr(struct xe_device *xe, u64 address);
u64 xe_device_uncanonicalize_addr(struct xe_device *xe, u64 address);

#endif
