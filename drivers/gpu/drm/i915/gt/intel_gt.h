/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_GT__
#define __INTEL_GT__

#include "intel_gt_types.h"

struct drm_i915_private;

void intel_gt_init_early(struct intel_gt *gt, struct drm_i915_private *i915);

#endif /* __INTEL_GT_H__ */
