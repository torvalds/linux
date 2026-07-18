/* SPDX-License-Identifier: MIT */
/* Copyright © 2026 Intel Corporation */

#ifndef __I915_BO_H__
#define __I915_BO_H__

#include <linux/types.h>

struct drm_i915_private;

bool i915_bo_fbdev_prefer_stolen(struct drm_i915_private *i915, unsigned int size);

extern const struct intel_display_bo_interface i915_display_bo_interface;

#endif /* __I915_BO_H__ */
