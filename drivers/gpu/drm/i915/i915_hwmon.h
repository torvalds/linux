/* SPDX-License-Identifier: MIT */

/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __I915_HWMON_H__
#define __I915_HWMON_H__

struct drm_i915_private;

#if IS_REACHABLE(CONFIG_HWMON)
void i915_hwmon_register(struct drm_i915_private *i915);
void i915_hwmon_unregister(struct drm_i915_private *i915);
#else
static inline void i915_hwmon_register(struct drm_i915_private *i915) { };
static inline void i915_hwmon_unregister(struct drm_i915_private *i915) { };
#endif

#endif /* __I915_HWMON_H__ */
