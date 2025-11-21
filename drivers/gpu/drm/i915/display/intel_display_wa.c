// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_print.h>

#include "i915_reg.h"
#include "intel_de.h"
#include "intel_display_core.h"
#include "intel_display_regs.h"
#include "intel_display_wa.h"

static void gen11_display_wa_apply(struct intel_display *display)
{
	/* Wa_14010594013 */
	intel_de_rmw(display, GEN8_CHICKEN_DCPR_1, 0, ICL_DELAY_PMRSP);
}

static void xe_d_display_wa_apply(struct intel_display *display)
{
	/* Wa_14013723622 */
	intel_de_rmw(display, CLKREQ_POLICY, CLKREQ_POLICY_MEM_UP_OVRD, 0);
}

static void adlp_display_wa_apply(struct intel_display *display)
{
	/* Wa_22011091694:adlp */
	intel_de_rmw(display, GEN9_CLKGATE_DIS_5, 0, DPCE_GATING_DIS);

	/* Bspec/49189 Initialize Sequence */
	intel_de_rmw(display, GEN8_CHICKEN_DCPR_1, DDI_CLOCK_REG_ACCESS, 0);
}

void intel_display_wa_apply(struct intel_display *display)
{
	if (display->platform.alderlake_p)
		adlp_display_wa_apply(display);
	else if (DISPLAY_VER(display) == 12)
		xe_d_display_wa_apply(display);
	else if (DISPLAY_VER(display) == 11)
		gen11_display_wa_apply(display);
}

/*
 * Wa_16025573575:
 * Fixes: Issue with bitbashing on Xe3 based platforms.
 * Workaround: Set masks bits in GPIO CTL and preserve it during bitbashing sequence.
 */
static bool intel_display_needs_wa_16025573575(struct intel_display *display)
{
	return DISPLAY_VERx100(display) == 3000 || DISPLAY_VERx100(display) == 3002;
}

/*
 * Wa_14011503117:
 * Fixes: Before enabling the scaler DE fatal error is masked
 * Workaround: Unmask the DE fatal error register after enabling the scaler
 * and after waiting of at least 1 frame.
 */
bool __intel_display_wa(struct intel_display *display, enum intel_display_wa wa, const char *name)
{
	switch (wa) {
	case INTEL_DISPLAY_WA_16023588340:
		return intel_display_needs_wa_16023588340(display);
	case INTEL_DISPLAY_WA_16025573575:
		return intel_display_needs_wa_16025573575(display);
	case INTEL_DISPLAY_WA_14011503117:
		return DISPLAY_VER(display) == 13;
	default:
		drm_WARN(display->drm, 1, "Missing Wa number: %s\n", name);
		break;
	}

	return false;
}
