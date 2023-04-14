/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022-2023 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_DRIVER_H__
#define __INTEL_DISPLAY_DRIVER_H__

struct drm_i915_private;

void intel_display_driver_register(struct drm_i915_private *i915);
void intel_display_driver_unregister(struct drm_i915_private *i915);

#endif /* __INTEL_DISPLAY_DRIVER_H__ */

