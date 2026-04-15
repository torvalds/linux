// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_print.h>

#include "intel_de.h"
#include "intel_display_core.h"
#include "intel_display_regs.h"
#include "intel_display_wa.h"
#include "intel_step.h"

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

static void xe3plpd_display_wa_apply(struct intel_display *display)
{
	/* Wa_22021451799 */
	intel_de_rmw(display, GEN9_CLKGATE_DIS_0, 0, DMG_GATING_DIS);
}

void intel_display_wa_apply(struct intel_display *display)
{
	if (DISPLAY_VER(display) == 35)
		xe3plpd_display_wa_apply(display);
	else if (display->platform.alderlake_p)
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
	return DISPLAY_VERx100(display) == 3000 || DISPLAY_VERx100(display) == 3002 ||
		DISPLAY_VERx100(display) == 3500;
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
	case INTEL_DISPLAY_WA_1409120013:
		return IS_DISPLAY_VER(display, 11, 12);
	case INTEL_DISPLAY_WA_1409767108:
		return (display->platform.alderlake_s ||
			(display->platform.rocketlake &&
			 IS_DISPLAY_STEP(display, STEP_A0, STEP_B0)));
	case INTEL_DISPLAY_WA_13012396614:
		return DISPLAY_VERx100(display) == 3000 ||
			DISPLAY_VERx100(display) == 3500;
	case INTEL_DISPLAY_WA_14010477008:
		return display->platform.dg1 || display->platform.rocketlake ||
			(display->platform.tigerlake &&
			 IS_DISPLAY_STEP(display, STEP_A0, STEP_D0));
	case INTEL_DISPLAY_WA_14010480278:
		return (IS_DISPLAY_VER(display, 10, 12));
	case INTEL_DISPLAY_WA_14010547955:
		return display->platform.dg2;
	case INTEL_DISPLAY_WA_14010685332:
		return INTEL_PCH_TYPE(display) >= PCH_CNP &&
			INTEL_PCH_TYPE(display) < PCH_DG1;
	case INTEL_DISPLAY_WA_14011294188:
		return INTEL_PCH_TYPE(display) >= PCH_TGP &&
			INTEL_PCH_TYPE(display) < PCH_DG1;
	case INTEL_DISPLAY_WA_14011503030:
	case INTEL_DISPLAY_WA_14011503117:
	case INTEL_DISPLAY_WA_22012358565:
		return DISPLAY_VER(display) == 13;
	case INTEL_DISPLAY_WA_14011508470:
		return (IS_DISPLAY_VERx100(display, 1200, 1300));
	case INTEL_DISPLAY_WA_14011765242:
		return display->platform.alderlake_s &&
			IS_DISPLAY_STEP(display, STEP_A0, STEP_A2);
	case INTEL_DISPLAY_WA_14014143976:
		return IS_DISPLAY_STEP(display, STEP_E0, STEP_FOREVER);
	case INTEL_DISPLAY_WA_14016740474:
		return IS_DISPLAY_VERx100_STEP(display, 1400, STEP_A0, STEP_C0);
	case INTEL_DISPLAY_WA_14020863754:
		return DISPLAY_VERx100(display) == 3000 ||
			DISPLAY_VERx100(display) == 2000 ||
			DISPLAY_VERx100(display) == 1401;
	case INTEL_DISPLAY_WA_14025769978:
		return DISPLAY_VER(display) == 35;
	case INTEL_DISPLAY_WA_15013987218:
		return DISPLAY_VER(display) == 20;
	case INTEL_DISPLAY_WA_15018326506:
		return display->platform.battlemage;
	case INTEL_DISPLAY_WA_16011303918:
	case INTEL_DISPLAY_WA_22011320316:
		return display->platform.alderlake_p &&
			IS_DISPLAY_STEP(display, STEP_A0, STEP_B0);
	case INTEL_DISPLAY_WA_16011181250:
		return display->platform.rocketlake || display->platform.alderlake_s ||
			display->platform.dg2;
	case INTEL_DISPLAY_WA_16011342517:
		return display->platform.alderlake_p &&
			IS_DISPLAY_STEP(display, STEP_A0, STEP_D0);
	case INTEL_DISPLAY_WA_16011863758:
		return DISPLAY_VER(display) >= 11;
	case INTEL_DISPLAY_WA_16023588340:
		return intel_display_needs_wa_16023588340(display);
	case INTEL_DISPLAY_WA_16025573575:
		return intel_display_needs_wa_16025573575(display);
	case INTEL_DISPLAY_WA_16025596647:
		return DISPLAY_VER(display) == 20 &&
			IS_DISPLAY_VERx100_STEP(display, 3000,
						STEP_A0, STEP_B0);
	case INTEL_DISPLAY_WA_18034343758:
		return DISPLAY_VER(display) == 20 ||
			(display->platform.pantherlake &&
			 IS_DISPLAY_STEP(display, STEP_A0, STEP_B0));
	case INTEL_DISPLAY_WA_22010178259:
		return DISPLAY_VER(display) == 12;
	case INTEL_DISPLAY_WA_22010947358:
		return display->platform.alderlake_p;
	case INTEL_DISPLAY_WA_22012278275:
		return display->platform.alderlake_p &&
			IS_DISPLAY_STEP(display, STEP_A0, STEP_E0);
	case INTEL_DISPLAY_WA_22014263786:
		return IS_DISPLAY_VERx100(display, 1100, 1400);
	case INTEL_DISPLAY_WA_22021048059:
		return IS_DISPLAY_VER(display, 14, 35);
	default:
		drm_WARN(display->drm, 1, "Missing Wa: %s\n", name);
		break;
	}

	return false;
}
