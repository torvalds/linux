/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GT_H_
#define _XE_GT_H_

#include <drm/drm_util.h>

#include "xe_device.h"
#include "xe_device_types.h"
#include "xe_hw_engine.h"

#define for_each_hw_engine(hwe__, gt__, id__) \
	for ((id__) = 0; (id__) < ARRAY_SIZE((gt__)->hw_engines); (id__)++) \
		for_each_if(((hwe__) = (gt__)->hw_engines + (id__)) && \
			  xe_hw_engine_is_valid((hwe__)))

#define CCS_MASK(gt) (((gt)->info.engine_mask & XE_HW_ENGINE_CCS_MASK) >> XE_HW_ENGINE_CCS0)

#ifdef CONFIG_FAULT_INJECTION
#include <linux/fault-inject.h> /* XXX: fault-inject.h is broken */
extern struct fault_attr gt_reset_failure;
static inline bool xe_fault_inject_gt_reset(void)
{
	return should_fail(&gt_reset_failure, 1);
}
#else
static inline bool xe_fault_inject_gt_reset(void)
{
	return false;
}
#endif

struct xe_gt *xe_gt_alloc(struct xe_tile *tile);
int xe_gt_init_hwconfig(struct xe_gt *gt);
int xe_gt_init_early(struct xe_gt *gt);
int xe_gt_init(struct xe_gt *gt);
int xe_gt_record_default_lrcs(struct xe_gt *gt);

/**
 * xe_gt_record_user_engines - save data related to engines available to
 * usersapce
 * @gt: GT structure
 *
 * Walk the available HW engines from gt->info.engine_mask and calculate data
 * related to those engines that may be used by userspace. To be used whenever
 * available engines change in runtime (e.g. with ccs_mode) or during
 * initialization
 */
void xe_gt_record_user_engines(struct xe_gt *gt);

void xe_gt_suspend_prepare(struct xe_gt *gt);
int xe_gt_suspend(struct xe_gt *gt);
int xe_gt_resume(struct xe_gt *gt);
void xe_gt_reset_async(struct xe_gt *gt);
void xe_gt_sanitize(struct xe_gt *gt);

/**
 * xe_gt_any_hw_engine_by_reset_domain - scan the list of engines and return the
 * first that matches the same reset domain as @class
 * @gt: GT structure
 * @class: hw engine class to lookup
 */
struct xe_hw_engine *
xe_gt_any_hw_engine_by_reset_domain(struct xe_gt *gt, enum xe_engine_class class);

/**
 * xe_gt_any_hw_engine - scan the list of engines and return the
 * first available
 * @gt: GT structure
 */
struct xe_hw_engine *xe_gt_any_hw_engine(struct xe_gt *gt);

struct xe_hw_engine *xe_gt_hw_engine(struct xe_gt *gt,
				     enum xe_engine_class class,
				     u16 instance,
				     bool logical);

static inline bool xe_gt_has_indirect_ring_state(struct xe_gt *gt)
{
	return gt->info.has_indirect_ring_state &&
	       xe_device_uc_enabled(gt_to_xe(gt));
}

static inline bool xe_gt_is_media_type(struct xe_gt *gt)
{
	return gt->info.type == XE_GT_TYPE_MEDIA;
}

static inline bool xe_gt_is_usm_hwe(struct xe_gt *gt, struct xe_hw_engine *hwe)
{
	struct xe_device *xe = gt_to_xe(gt);

	return xe->info.has_usm && hwe->class == XE_ENGINE_CLASS_COPY &&
		hwe->instance == gt->usm.reserved_bcs_instance;
}

#endif
