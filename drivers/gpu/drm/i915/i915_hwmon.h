/* SPDX-License-Identifier: MIT */

/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __I915_HWMON_H__
#define __I915_HWMON_H__

#include <linux/types.h>

struct drm_i915_private;
struct intel_gt;

#if IS_REACHABLE(CONFIG_HWMON)
void i915_hwmon_register(struct drm_i915_private *i915);
void i915_hwmon_unregister(struct drm_i915_private *i915);
void i915_hwmon_power_max_disable(struct drm_i915_private *i915, bool *old);
void i915_hwmon_power_max_restore(struct drm_i915_private *i915, bool old);
#else
static inline void i915_hwmon_register(struct drm_i915_private *i915) { };
static inline void i915_hwmon_unregister(struct drm_i915_private *i915) { };
static inline void i915_hwmon_power_max_disable(struct drm_i915_private *i915, bool *old) { };
static inline void i915_hwmon_power_max_restore(struct drm_i915_private *i915, bool old) { };
#endif

#endif /* __I915_HWMON_H__ */
