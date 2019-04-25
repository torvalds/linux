/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_COMBO_PHY_H__
#define __INTEL_COMBO_PHY_H__

#include <linux/types.h>
#include <drm/i915_drm.h>

struct drm_i915_private;

void icl_combo_phys_init(struct drm_i915_private *dev_priv);
void icl_combo_phys_uninit(struct drm_i915_private *dev_priv);
void cnl_combo_phys_init(struct drm_i915_private *dev_priv);
void cnl_combo_phys_uninit(struct drm_i915_private *dev_priv);
void intel_combo_phy_power_up_lanes(struct drm_i915_private *dev_priv,
				    enum port port, bool is_dsi,
				    int lane_count, bool lane_reversal);

#endif /* __INTEL_COMBO_PHY_H__ */
