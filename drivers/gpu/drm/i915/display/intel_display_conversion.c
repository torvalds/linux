// SPDX-License-Identifier: MIT
/* Copyright © 2024 Intel Corporation */

#include "i915_drv.h"

struct intel_display *__i915_to_display(struct drm_i915_private *i915)
{
	return &i915->display;
}

struct intel_display *__drm_to_display(struct drm_device *drm)
{
	return __i915_to_display(to_i915(drm));
}
