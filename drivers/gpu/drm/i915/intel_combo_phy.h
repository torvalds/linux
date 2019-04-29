/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_COMBO_PHY_H__
#define __INTEL_COMBO_PHY_H__

struct drm_i915_private;

void icl_combo_phys_init(struct drm_i915_private *dev_priv);
void icl_combo_phys_uninit(struct drm_i915_private *dev_priv);
void cnl_combo_phys_init(struct drm_i915_private *dev_priv);
void cnl_combo_phys_uninit(struct drm_i915_private *dev_priv);

#endif /* __INTEL_COMBO_PHY_H__ */
