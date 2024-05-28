// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_de.h"
#include "intel_display_wa.h"

static void gen11_display_wa_apply(struct drm_i915_private *i915)
{
	/* Wa_1409120013 */
	intel_de_write(i915, ILK_DPFC_CHICKEN(INTEL_FBC_A),
		       DPFC_CHICKEN_COMP_DUMMY_PIXEL);

	/* Wa_14010594013 */
	intel_de_rmw(i915, GEN8_CHICKEN_DCPR_1, 0, ICL_DELAY_PMRSP);
}

static void xe_d_display_wa_apply(struct drm_i915_private *i915)
{
	/* Wa_1409120013 */
	intel_de_write(i915, ILK_DPFC_CHICKEN(INTEL_FBC_A),
		       DPFC_CHICKEN_COMP_DUMMY_PIXEL);

	/* Wa_14013723622 */
	intel_de_rmw(i915, CLKREQ_POLICY, CLKREQ_POLICY_MEM_UP_OVRD, 0);
}

static void adlp_display_wa_apply(struct drm_i915_private *i915)
{
	/* Wa_22011091694:adlp */
	intel_de_rmw(i915, GEN9_CLKGATE_DIS_5, 0, DPCE_GATING_DIS);

	/* Bspec/49189 Initialize Sequence */
	intel_de_rmw(i915, GEN8_CHICKEN_DCPR_1, DDI_CLOCK_REG_ACCESS, 0);
}

void intel_display_wa_apply(struct drm_i915_private *i915)
{
	if (IS_ALDERLAKE_P(i915))
		adlp_display_wa_apply(i915);
	else if (DISPLAY_VER(i915) == 12)
		xe_d_display_wa_apply(i915);
	else if (DISPLAY_VER(i915) == 11)
		gen11_display_wa_apply(i915);
}
