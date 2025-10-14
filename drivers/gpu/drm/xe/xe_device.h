/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_DEVICE_H_
#define _XE_DEVICE_H_

#include <drm/drm_util.h>

#include "xe_device_types.h"
#include "xe_gt_types.h"
#include "xe_sriov.h"

static inline struct xe_device *to_xe_device(const struct drm_device *dev)
{
	return container_of(dev, struct xe_device, drm);
}

static inline struct xe_device *kdev_to_xe_device(struct device *kdev)
{
	struct drm_device *drm = dev_get_drvdata(kdev);

	return drm ? to_xe_device(drm) : NULL;
}

static inline struct xe_device *pdev_to_xe_device(struct pci_dev *pdev)
{
	struct drm_device *drm = pci_get_drvdata(pdev);

	return drm ? to_xe_device(drm) : NULL;
}

static inline struct xe_device *xe_device_const_cast(const struct xe_device *xe)
{
	return (struct xe_device *)xe;
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

/*
 * Highest GT/tile count for any platform.  Used only for memory allocation
 * sizing.  Any logic looping over GTs or mapping userspace GT IDs into GT
 * structures should use the per-platform xe->info.max_gt_per_tile instead.
 */
#define XE_MAX_GT_PER_TILE 2

static inline struct xe_gt *xe_device_get_gt(struct xe_device *xe, u8 gt_id)
{
	struct xe_tile *tile;
	struct xe_gt *gt;

	if (gt_id >= xe->info.tile_count * xe->info.max_gt_per_tile)
		return NULL;

	tile = &xe->tiles[gt_id / xe->info.max_gt_per_tile];
	switch (gt_id % xe->info.max_gt_per_tile) {
	default:
		xe_assert(xe, false);
		fallthrough;
	case 0:
		gt = tile->primary_gt;
		break;
	case 1:
		gt = tile->media_gt;
		break;
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

#define for_each_gt(gt__, xe__, id__) \
	for ((id__) = 0; (id__) < (xe__)->info.tile_count * (xe__)->info.max_gt_per_tile; (id__)++) \
		for_each_if((gt__) = xe_device_get_gt((xe__), (id__)))

#define for_each_gt_on_tile(gt__, tile__, id__) \
	for_each_gt((gt__), (tile__)->xe, (id__)) \
		for_each_if((gt__)->tile == (tile__))

static inline struct xe_force_wake *gt_to_fw(struct xe_gt *gt)
{
	return &gt->pm.fw;
}

void xe_device_assert_mem_access(struct xe_device *xe);

static inline bool xe_device_has_flat_ccs(struct xe_device *xe)
{
	return xe->info.has_flat_ccs;
}

static inline bool xe_device_has_sriov(struct xe_device *xe)
{
	return xe->info.has_sriov;
}

static inline bool xe_device_has_msix(struct xe_device *xe)
{
	return xe->irq.msix.nvec > 0;
}

static inline bool xe_device_has_memirq(struct xe_device *xe)
{
	return GRAPHICS_VERx100(xe) >= 1250;
}

static inline bool xe_device_uses_memirq(struct xe_device *xe)
{
	return xe_device_has_memirq(xe) && (IS_SRIOV_VF(xe) || xe_device_has_msix(xe));
}

static inline bool xe_device_has_lmtt(struct xe_device *xe)
{
	return IS_DGFX(xe);
}

u32 xe_device_ccs_bytes(struct xe_device *xe, u64 size);

void xe_device_snapshot_print(struct xe_device *xe, struct drm_printer *p);

u64 xe_device_canonicalize_addr(struct xe_device *xe, u64 address);
u64 xe_device_uncanonicalize_addr(struct xe_device *xe, u64 address);

void xe_device_td_flush(struct xe_device *xe);
void xe_device_l2_flush(struct xe_device *xe);

static inline bool xe_device_wedged(struct xe_device *xe)
{
	return atomic_read(&xe->wedged.flag);
}

void xe_device_set_wedged_method(struct xe_device *xe, unsigned long method);
void xe_device_declare_wedged(struct xe_device *xe);

struct xe_file *xe_file_get(struct xe_file *xef);
void xe_file_put(struct xe_file *xef);

int xe_is_injection_active(void);

/*
 * Occasionally it is seen that the G2H worker starts running after a delay of more than
 * a second even after being queued and activated by the Linux workqueue subsystem. This
 * leads to G2H timeout error. The root cause of issue lies with scheduling latency of
 * Lunarlake Hybrid CPU. Issue disappears if we disable Lunarlake atom cores from BIOS
 * and this is beyond xe kmd.
 *
 * TODO: Drop this change once workqueue scheduling delay issue is fixed on LNL Hybrid CPU.
 */
#define LNL_FLUSH_WORKQUEUE(wq__) \
	flush_workqueue(wq__)
#define LNL_FLUSH_WORK(wrk__) \
	flush_work(wrk__)

#endif
