// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_de.h"
#include "intel_display.h"
#include "intel_hti.h"
#include "intel_hti_regs.h"

void intel_hti_init(struct drm_i915_private *i915)
{
	/*
	 * If the platform has HTI, we need to find out whether it has reserved
	 * any display resources before we create our display outputs.
	 */
	if (DISPLAY_INFO(i915)->has_hti)
		i915->display.hti.state = intel_de_read(i915, HDPORT_STATE);
}

bool intel_hti_uses_phy(struct drm_i915_private *i915, enum phy phy)
{
	if (drm_WARN_ON(&i915->drm, phy == PHY_NONE))
		return false;

	return i915->display.hti.state & HDPORT_ENABLED &&
		i915->display.hti.state & HDPORT_DDI_USED(phy);
}

u32 intel_hti_dpll_mask(struct drm_i915_private *i915)
{
	if (!(i915->display.hti.state & HDPORT_ENABLED))
		return 0;

	/*
	 * Note: This is subtle. The values must coincide with what's defined
	 * for the platform.
	 */
	return REG_FIELD_GET(HDPORT_DPLL_USED_MASK, i915->display.hti.state);
}
