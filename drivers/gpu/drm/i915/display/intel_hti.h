/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_HTI_H__
#define __INTEL_HTI_H__

#include <linux/types.h>

struct drm_i915_private;
enum phy;

void intel_hti_init(struct drm_i915_private *i915);
bool intel_hti_uses_phy(struct drm_i915_private *i915, enum phy phy);
u32 intel_hti_dpll_mask(struct drm_i915_private *i915);

#endif /* __INTEL_HTI_H__ */
