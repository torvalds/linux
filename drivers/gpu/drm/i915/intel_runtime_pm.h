/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_RUNTIME_PM_H__
#define __INTEL_RUNTIME_PM_H__

#include <linux/types.h>

#include "intel_display.h"
#include "intel_wakeref.h"

struct drm_i915_private;
struct drm_printer;

enum i915_drm_suspend_mode {
	I915_DRM_SUSPEND_IDLE,
	I915_DRM_SUSPEND_MEM,
	I915_DRM_SUSPEND_HIBERNATE,
};

void intel_runtime_pm_init_early(struct drm_i915_private *dev_priv);
void intel_runtime_pm_enable(struct drm_i915_private *dev_priv);
void intel_runtime_pm_disable(struct drm_i915_private *dev_priv);
void intel_runtime_pm_cleanup(struct drm_i915_private *dev_priv);

intel_wakeref_t intel_runtime_pm_get(struct drm_i915_private *i915);
intel_wakeref_t intel_runtime_pm_get_if_in_use(struct drm_i915_private *i915);
intel_wakeref_t intel_runtime_pm_get_noresume(struct drm_i915_private *i915);
intel_wakeref_t intel_runtime_pm_get_raw(struct drm_i915_private *i915);

#define with_intel_runtime_pm(i915, wf) \
	for ((wf) = intel_runtime_pm_get(i915); (wf); \
	     intel_runtime_pm_put((i915), (wf)), (wf) = 0)

#define with_intel_runtime_pm_if_in_use(i915, wf) \
	for ((wf) = intel_runtime_pm_get_if_in_use(i915); (wf); \
	     intel_runtime_pm_put((i915), (wf)), (wf) = 0)

void intel_runtime_pm_put_unchecked(struct drm_i915_private *i915);
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
void intel_runtime_pm_put(struct drm_i915_private *i915, intel_wakeref_t wref);
#else
static inline void
intel_runtime_pm_put(struct drm_i915_private *i915, intel_wakeref_t wref)
{
	intel_runtime_pm_put_unchecked(i915);
}
#endif
void intel_runtime_pm_put_raw(struct drm_i915_private *i915, intel_wakeref_t wref);

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
void print_intel_runtime_pm_wakeref(struct drm_i915_private *i915,
				    struct drm_printer *p);
#else
static inline void print_intel_runtime_pm_wakeref(struct drm_i915_private *i915,
						  struct drm_printer *p)
{
}
#endif

#endif /* __INTEL_RUNTIME_PM_H__ */
