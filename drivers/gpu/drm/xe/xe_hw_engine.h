/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_HW_ENGINE_H_
#define _XE_HW_ENGINE_H_

#include "xe_hw_engine_types.h"

struct drm_printer;

#ifdef CONFIG_DRM_XE_JOB_TIMEOUT_MIN
#define XE_HW_ENGINE_JOB_TIMEOUT_MIN CONFIG_DRM_XE_JOB_TIMEOUT_MIN
#else
#define XE_HW_ENGINE_JOB_TIMEOUT_MIN 1
#endif
#ifdef CONFIG_DRM_XE_JOB_TIMEOUT_MAX
#define XE_HW_ENGINE_JOB_TIMEOUT_MAX CONFIG_DRM_XE_JOB_TIMEOUT_MAX
#else
#define XE_HW_ENGINE_JOB_TIMEOUT_MAX (10 * 1000)
#endif
#ifdef CONFIG_DRM_XE_TIMESLICE_MIN
#define XE_HW_ENGINE_TIMESLICE_MIN CONFIG_DRM_XE_TIMESLICE_MIN
#else
#define XE_HW_ENGINE_TIMESLICE_MIN 1
#endif
#ifdef CONFIG_DRM_XE_TIMESLICE_MAX
#define XE_HW_ENGINE_TIMESLICE_MAX CONFIG_DRM_XE_TIMESLICE_MAX
#else
#define XE_HW_ENGINE_TIMESLICE_MAX (10 * 1000 * 1000)
#endif
#ifdef CONFIG_DRM_XE_PREEMPT_TIMEOUT
#define XE_HW_ENGINE_PREEMPT_TIMEOUT CONFIG_DRM_XE_PREEMPT_TIMEOUT
#else
#define XE_HW_ENGINE_PREEMPT_TIMEOUT (640 * 1000)
#endif
#ifdef CONFIG_DRM_XE_PREEMPT_TIMEOUT_MIN
#define XE_HW_ENGINE_PREEMPT_TIMEOUT_MIN CONFIG_DRM_XE_PREEMPT_TIMEOUT_MIN
#else
#define XE_HW_ENGINE_PREEMPT_TIMEOUT_MIN 1
#endif
#ifdef CONFIG_DRM_XE_PREEMPT_TIMEOUT_MAX
#define XE_HW_ENGINE_PREEMPT_TIMEOUT_MAX CONFIG_DRM_XE_PREEMPT_TIMEOUT_MAX
#else
#define XE_HW_ENGINE_PREEMPT_TIMEOUT_MAX (10 * 1000 * 1000)
#endif

int xe_hw_engines_init_early(struct xe_gt *gt);
int xe_hw_engines_init(struct xe_gt *gt);
void xe_hw_engine_handle_irq(struct xe_hw_engine *hwe, u16 intr_vec);
void xe_hw_engine_enable_ring(struct xe_hw_engine *hwe);
u32 xe_hw_engine_mask_per_class(struct xe_gt *gt,
				enum xe_engine_class engine_class);

struct xe_hw_engine_snapshot *
xe_hw_engine_snapshot_capture(struct xe_hw_engine *hwe);
void xe_hw_engine_snapshot_free(struct xe_hw_engine_snapshot *snapshot);
void xe_hw_engine_snapshot_print(struct xe_hw_engine_snapshot *snapshot,
				 struct drm_printer *p);
void xe_hw_engine_print(struct xe_hw_engine *hwe, struct drm_printer *p);
void xe_hw_engine_setup_default_lrc_state(struct xe_hw_engine *hwe);

bool xe_hw_engine_is_reserved(struct xe_hw_engine *hwe);
static inline bool xe_hw_engine_is_valid(struct xe_hw_engine *hwe)
{
	return hwe->name;
}

const char *xe_hw_engine_class_to_str(enum xe_engine_class class);
u64 xe_hw_engine_read_timestamp(struct xe_hw_engine *hwe);
enum xe_force_wake_domains xe_hw_engine_to_fw_domain(struct xe_hw_engine *hwe);

#endif
