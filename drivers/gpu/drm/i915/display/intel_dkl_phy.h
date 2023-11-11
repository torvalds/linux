/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_DKL_PHY_H__
#define __INTEL_DKL_PHY_H__

#include <linux/types.h>

#include "i915_reg_defs.h"

struct drm_i915_private;

u32
intel_dkl_phy_read(struct drm_i915_private *i915, i915_reg_t reg, int ln);
void
intel_dkl_phy_write(struct drm_i915_private *i915, i915_reg_t reg, int ln, u32 val);
void
intel_dkl_phy_rmw(struct drm_i915_private *i915, i915_reg_t reg, int ln, u32 clear, u32 set);
void
intel_dkl_phy_posting_read(struct drm_i915_private *i915, i915_reg_t reg, int ln);

#endif /* __INTEL_DKL_PHY_H__ */
