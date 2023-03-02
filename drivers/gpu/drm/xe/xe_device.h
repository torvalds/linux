/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_DEVICE_H_
#define _XE_DEVICE_H_

struct xe_engine;
struct xe_file;

#include <drm/drm_util.h>

#include "regs/xe_gpu_commands.h"
#include "xe_device_types.h"
#include "xe_force_wake.h"
#include "xe_macros.h"

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
int xe_device_probe(struct xe_device *xe);
void xe_device_remove(struct xe_device *xe);
void xe_device_shutdown(struct xe_device *xe);

void xe_device_add_persistent_engines(struct xe_device *xe, struct xe_engine *e);
void xe_device_remove_persistent_engines(struct xe_device *xe,
					 struct xe_engine *e);

void xe_device_wmb(struct xe_device *xe);

static inline struct xe_file *to_xe_file(const struct drm_file *file)
{
	return file->driver_priv;
}

static inline struct xe_gt *xe_device_get_gt(struct xe_device *xe, u8 gt_id)
{
	struct xe_gt *gt;

	XE_BUG_ON(gt_id > XE_MAX_GT);
	gt = xe->gt + gt_id;
	XE_BUG_ON(gt->info.id != gt_id);
	XE_BUG_ON(gt->info.type == XE_GT_TYPE_UNINITIALIZED);

	return gt;
}

/*
 * FIXME: Placeholder until multi-gt lands. Once that lands, kill this function.
 */
static inline struct xe_gt *to_gt(struct xe_device *xe)
{
	return xe->gt;
}

static inline bool xe_device_guc_submission_enabled(struct xe_device *xe)
{
	return xe->info.enable_guc;
}

static inline void xe_device_guc_submission_disable(struct xe_device *xe)
{
	xe->info.enable_guc = false;
}

#define for_each_gt(gt__, xe__, id__) \
	for ((id__) = 0; (id__) < (xe__)->info.tile_count; (id__++)) \
		for_each_if ((gt__) = xe_device_get_gt((xe__), (id__)))

static inline struct xe_force_wake * gt_to_fw(struct xe_gt *gt)
{
	return &gt->mmio.fw;
}

void xe_device_mem_access_get(struct xe_device *xe);
void xe_device_mem_access_put(struct xe_device *xe);

static inline void xe_device_assert_mem_access(struct xe_device *xe)
{
	XE_WARN_ON(!xe->mem_access.ref);
}

static inline bool xe_device_mem_access_ongoing(struct xe_device *xe)
{
	bool ret;

	mutex_lock(&xe->mem_access.lock);
	ret = xe->mem_access.ref;
	mutex_unlock(&xe->mem_access.lock);

	return ret;
}

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

u32 xe_device_ccs_bytes(struct xe_device *xe, u64 size);
#endif
