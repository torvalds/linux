// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include "i915_drv.h"
#include "intel_dpio_phy.h"
#include "vlv_sideband.h"

static enum vlv_iosf_sb_unit vlv_dpio_phy_to_unit(struct drm_i915_private *i915,
						  enum dpio_phy phy)
{
	/*
	 * IOSF_PORT_DPIO: VLV x2 PHY (DP/HDMI B and C), CHV x1 PHY (DP/HDMI D)
	 * IOSF_PORT_DPIO_2: CHV x2 PHY (DP/HDMI B and C)
	 */
	if (IS_CHERRYVIEW(i915))
		return phy == DPIO_PHY0 ? VLV_IOSF_SB_DPIO_2 : VLV_IOSF_SB_DPIO;
	else
		return VLV_IOSF_SB_DPIO;
}

u32 vlv_dpio_read(struct drm_i915_private *i915, enum dpio_phy phy, int reg)
{
	enum vlv_iosf_sb_unit unit = vlv_dpio_phy_to_unit(i915, phy);
	u32 val;

	val = vlv_iosf_sb_read(i915, unit, reg);

	/*
	 * FIXME: There might be some registers where all 1's is a valid value,
	 * so ideally we should check the register offset instead...
	 */
	drm_WARN(&i915->drm, val == 0xffffffff,
		 "DPIO PHY%d read reg 0x%x == 0x%x\n",
		 phy, reg, val);

	return val;
}

void vlv_dpio_write(struct drm_i915_private *i915,
		    enum dpio_phy phy, int reg, u32 val)
{
	enum vlv_iosf_sb_unit unit = vlv_dpio_phy_to_unit(i915, phy);

	vlv_iosf_sb_write(i915, unit, reg, val);
}
