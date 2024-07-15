/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_WA_H__
#define __INTEL_DISPLAY_WA_H__

struct drm_i915_private;

void intel_display_wa_apply(struct drm_i915_private *i915);

#endif
