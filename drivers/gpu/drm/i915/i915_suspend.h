/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_SUSPEND_H__
#define __I915_SUSPEND_H__

struct drm_i915_private;

void i915_save_display(struct drm_i915_private *i915);
void i915_restore_display(struct drm_i915_private *i915);

#endif /* __I915_SUSPEND_H__ */
