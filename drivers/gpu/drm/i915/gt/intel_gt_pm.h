/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_GT_PM_H
#define INTEL_GT_PM_H

#include <linux/types.h>

#include "intel_gt_types.h"
#include "intel_wakeref.h"

static inline bool intel_gt_pm_is_awake(const struct intel_gt *gt)
{
	return intel_wakeref_is_active(&gt->wakeref);
}

static inline void intel_gt_pm_get(struct intel_gt *gt)
{
	intel_wakeref_get(&gt->wakeref);
}

static inline void __intel_gt_pm_get(struct intel_gt *gt)
{
	__intel_wakeref_get(&gt->wakeref);
}

static inline bool intel_gt_pm_get_if_awake(struct intel_gt *gt)
{
	return intel_wakeref_get_if_active(&gt->wakeref);
}

static inline void intel_gt_pm_put(struct intel_gt *gt)
{
	intel_wakeref_put(&gt->wakeref);
}

static inline void intel_gt_pm_put_async(struct intel_gt *gt)
{
	intel_wakeref_put_async(&gt->wakeref);
}

static inline int intel_gt_pm_wait_for_idle(struct intel_gt *gt)
{
	return intel_wakeref_wait_for_idle(&gt->wakeref);
}

void intel_gt_pm_init_early(struct intel_gt *gt);
void intel_gt_pm_init(struct intel_gt *gt);
void intel_gt_pm_fini(struct intel_gt *gt);

void intel_gt_suspend_prepare(struct intel_gt *gt);
void intel_gt_suspend_late(struct intel_gt *gt);
int intel_gt_resume(struct intel_gt *gt);

void intel_gt_runtime_suspend(struct intel_gt *gt);
int intel_gt_runtime_resume(struct intel_gt *gt);

static inline bool is_mock_gt(const struct intel_gt *gt)
{
	return I915_SELFTEST_ONLY(gt->awake == -ENODEV);
}

#endif /* INTEL_GT_PM_H */
