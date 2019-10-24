/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#ifndef __I915_SELFTESTS_IGT_RESET_H__
#define __I915_SELFTESTS_IGT_RESET_H__

#include "../i915_drv.h"

void igt_global_reset_lock(struct drm_i915_private *i915);
void igt_global_reset_unlock(struct drm_i915_private *i915);
bool igt_force_reset(struct drm_i915_private *i915);

#endif
