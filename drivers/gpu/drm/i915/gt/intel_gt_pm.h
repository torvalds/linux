/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_GT_PM_H
#define INTEL_GT_PM_H

#include <linux/types.h>

struct drm_i915_private;

enum {
	INTEL_GT_UNPARK,
	INTEL_GT_PARK,
};

void intel_gt_pm_get(struct drm_i915_private *i915);
void intel_gt_pm_put(struct drm_i915_private *i915);

void intel_gt_pm_init(struct drm_i915_private *i915);

void intel_gt_sanitize(struct drm_i915_private *i915, bool force);
int intel_gt_resume(struct drm_i915_private *i915);

#endif /* INTEL_GT_PM_H */
