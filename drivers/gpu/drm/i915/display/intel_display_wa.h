/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_WA_H__
#define __INTEL_DISPLAY_WA_H__

#include <linux/types.h>

struct drm_i915_private;

void intel_display_wa_apply(struct drm_i915_private *i915);

#ifdef I915
static inline bool intel_display_needs_wa_16023588340(struct drm_i915_private *i915) { return false; }
#else
bool intel_display_needs_wa_16023588340(struct drm_i915_private *i915);
#endif

#endif
