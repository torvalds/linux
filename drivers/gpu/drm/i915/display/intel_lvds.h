/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_LVDS_H__
#define __INTEL_LVDS_H__

#include <linux/types.h>

#include "i915_reg_defs.h"

enum pipe;
struct drm_i915_private;

bool intel_lvds_port_enabled(struct drm_i915_private *dev_priv,
			     i915_reg_t lvds_reg, enum pipe *pipe);
void intel_lvds_init(struct drm_i915_private *dev_priv);
struct intel_encoder *intel_get_lvds_encoder(struct drm_i915_private *dev_priv);
bool intel_is_dual_link_lvds(struct drm_i915_private *dev_priv);

#endif /* __INTEL_LVDS_H__ */
