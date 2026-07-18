/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eugeni Dodonov <eugeni.dodonov@intel.com>
 *
 */

#include <drm/drm_print.h>
#include <drm/intel/intel_gmd_interrupt_regs.h>
#include <drm/intel/intel_gmd_misc_regs.h>
#include <drm/intel/mchbar_regs.h>

#include "display/intel_display_clock_gating.h"
#include "display/intel_pch.h"
#include "gt/intel_engine_regs.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_mcr.h"
#include "gt/intel_gt_regs.h"

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_clock_gating.h"
#include "vlv_iosf_sb.h"

struct drm_i915_clock_gating_funcs {
	void (*init_clock_gating)(struct drm_i915_private *i915);
};

static void bxt_init_clock_gating(struct drm_i915_private *i915)
{
	/* WaDisableSDEUnitClockGating:bxt */
	intel_uncore_rmw(&i915->uncore, GEN8_UCGCTL6, 0, GEN8_SDEUNIT_CLOCK_GATE_DISABLE);

	/*
	 * FIXME:
	 * GEN8_HDCUNIT_CLOCK_GATE_DISABLE_HDCREQ applies on 3x6 GT SKUs only.
	 */
	intel_uncore_rmw(&i915->uncore, GEN8_UCGCTL6, 0, GEN8_HDCUNIT_CLOCK_GATE_DISABLE_HDCREQ);

	intel_display_bxt_init_clock_gating(i915->display);
}

static void glk_init_clock_gating(struct drm_i915_private *i915)
{
	intel_display_glk_init_clock_gating(i915->display);
}

static void ilk_init_clock_gating(struct drm_i915_private *i915)
{
	intel_uncore_write(&i915->uncore, PCH_3DCGDIS0,
			   MARIUNIT_CLOCK_GATE_DISABLE |
			   SVSMUNIT_CLOCK_GATE_DISABLE);
	intel_uncore_write(&i915->uncore, PCH_3DCGDIS1,
			   VFMUNIT_CLOCK_GATE_DISABLE);

	intel_display_ilk_init_clock_gating(i915->display);
	intel_pch_init_clock_gating(i915->display);
}

static void gen6_check_mch_setup(struct drm_i915_private *i915)
{
	u32 tmp;

	tmp = intel_uncore_read(&i915->uncore, MCH_SSKPD);
	if (REG_FIELD_GET(SSKPD_WM0_MASK_SNB, tmp) != 12)
		drm_dbg_kms(&i915->drm,
			    "Wrong MCH_SSKPD value: 0x%08x This can cause underruns.\n",
			    tmp);
}

static void gen6_init_clock_gating(struct drm_i915_private *i915)
{
	intel_display_gen6_init_clock_gating(i915->display);

	intel_uncore_write(&i915->uncore, GEN6_UCGCTL1,
			   intel_uncore_read(&i915->uncore, GEN6_UCGCTL1) |
			   GEN6_BLBUNIT_CLOCK_GATE_DISABLE |
			   GEN6_CSUNIT_CLOCK_GATE_DISABLE);

	/* According to the BSpec vol1g, bit 12 (RCPBUNIT) clock
	 * gating disable must be set.  Failure to set it results in
	 * flickering pixels due to Z write ordering failures after
	 * some amount of runtime in the Mesa "fire" demo, and Unigine
	 * Sanctuary and Tropics, and apparently anything else with
	 * alpha test or pixel discard.
	 *
	 * According to the spec, bit 11 (RCCUNIT) must also be set,
	 * but we didn't debug actual testcases to find it out.
	 *
	 * WaDisableRCCUnitClockGating:snb
	 * WaDisableRCPBUnitClockGating:snb
	 */
	intel_uncore_write(&i915->uncore, GEN6_UCGCTL2,
			   GEN6_RCPBUNIT_CLOCK_GATE_DISABLE |
			   GEN6_RCCUNIT_CLOCK_GATE_DISABLE);

	/*
	 * According to the spec the following bits should be
	 * set in order to enable memory self-refresh and fbc:
	 * The bit21 and bit22 of 0x42000
	 * The bit21 and bit22 of 0x42004
	 * The bit5 and bit7 of 0x42020
	 * The bit14 of 0x70180
	 * The bit14 of 0x71180
	 *
	 * WaFbcAsynchFlipDisableFbcQueue:snb
	 */
	intel_pch_init_clock_gating(i915->display);

	gen6_check_mch_setup(i915);
}

static void gen8_set_l3sqc_credits(struct drm_i915_private *i915,
				   int general_prio_credits,
				   int high_prio_credits)
{
	u32 misccpctl;
	u32 val;

	/* WaTempDisableDOPClkGating:bdw */
	misccpctl = intel_uncore_rmw(&i915->uncore, GEN7_MISCCPCTL,
				     GEN7_DOP_CLOCK_GATE_ENABLE, 0);

	val = intel_gt_mcr_read_any(to_gt(i915), GEN8_L3SQCREG1);
	val &= ~L3_PRIO_CREDITS_MASK;
	val |= L3_GENERAL_PRIO_CREDITS(general_prio_credits);
	val |= L3_HIGH_PRIO_CREDITS(high_prio_credits);
	intel_gt_mcr_multicast_write(to_gt(i915), GEN8_L3SQCREG1, val);

	/*
	 * Wait at least 100 clocks before re-enabling clock gating.
	 * See the definition of L3SQCREG1 in BSpec.
	 */
	intel_gt_mcr_read_any(to_gt(i915), GEN8_L3SQCREG1);
	udelay(1);
	intel_uncore_write(&i915->uncore, GEN7_MISCCPCTL, misccpctl);
}

static void dg2_init_clock_gating(struct drm_i915_private *i915)
{
	/* Wa_22010954014:dg2 */
	intel_uncore_rmw(&i915->uncore, XEHP_CLOCK_GATE_DIS, 0,
			 SGSI_SIDECLK_DIS);
}

static void cfl_init_clock_gating(struct drm_i915_private *i915)
{
	intel_pch_init_clock_gating(i915->display);

	/* WAC6entrylatency:cfl */
	intel_uncore_rmw(&i915->uncore, FBC_LLC_READ_CTRL, 0, FBC_LLC_FULLY_OPEN);

	intel_display_cfl_init_clock_gating(i915->display);
}

static void kbl_init_clock_gating(struct drm_i915_private *i915)
{
	/* WAC6entrylatency:kbl */
	intel_uncore_rmw(&i915->uncore, FBC_LLC_READ_CTRL, 0, FBC_LLC_FULLY_OPEN);

	/* WaDisableSDEUnitClockGating:kbl */
	if (IS_KABYLAKE(i915) && IS_GRAPHICS_STEP(i915, 0, STEP_C0))
		intel_uncore_rmw(&i915->uncore, GEN8_UCGCTL6,
				 0, GEN8_SDEUNIT_CLOCK_GATE_DISABLE);

	/* WaDisableGamClockGating:kbl */
	if (IS_KABYLAKE(i915) && IS_GRAPHICS_STEP(i915, 0, STEP_C0))
		intel_uncore_rmw(&i915->uncore, GEN6_UCGCTL1,
				 0, GEN6_GAMUNIT_CLOCK_GATE_DISABLE);

	intel_display_kbl_init_clock_gating(i915->display);
}

static void skl_init_clock_gating(struct drm_i915_private *i915)
{
	/* WaDisableDopClockGating:skl */
	intel_uncore_rmw(&i915->uncore, GEN7_MISCCPCTL,
			 GEN7_DOP_CLOCK_GATE_ENABLE, 0);

	/* WAC6entrylatency:skl */
	intel_uncore_rmw(&i915->uncore, FBC_LLC_READ_CTRL, 0, FBC_LLC_FULLY_OPEN);

	intel_display_skl_init_clock_gating(i915->display);
}

static void bdw_init_clock_gating(struct drm_i915_private *i915)
{
	intel_display_bdw_clock_gating_disable_fbcq(i915->display);

	/* WaSwitchSolVfFArbitrationPriority:bdw */
	intel_uncore_rmw(&i915->uncore, GAM_ECOCHK, 0, HSW_ECOCHK_ARB_PRIO_SOL);

	intel_display_bdw_clock_gating_vblank_in_srd(i915->display);

	/* WaVSRefCountFullforceMissDisable:bdw */
	/* WaDSRefCountFullforceMissDisable:bdw */
	intel_uncore_rmw(&i915->uncore, GEN7_FF_THREAD_MODE,
			 GEN8_FF_DS_REF_CNT_FFME | GEN7_FF_VS_REF_CNT_FFME, 0);

	intel_uncore_write(&i915->uncore, RING_PSMI_CTL(RENDER_RING_BASE),
			   REG_MASKED_FIELD_ENABLE(GEN8_RC_SEMA_IDLE_MSG_DISABLE));

	/* WaDisableSDEUnitClockGating:bdw */
	intel_uncore_rmw(&i915->uncore, GEN8_UCGCTL6, 0, GEN8_SDEUNIT_CLOCK_GATE_DISABLE);

	/* WaProgramL3SqcReg1Default:bdw */
	gen8_set_l3sqc_credits(i915, 30, 2);

	intel_display_bdw_clock_gating_kvm_notif(i915->display);

	intel_pch_init_clock_gating(i915->display);

	/* WaDisableDopClockGating:bdw
	 *
	 * Also see the CHICKEN2 write in bdw_init_workarounds() to disable DOP
	 * clock gating.
	 */
	intel_uncore_rmw(&i915->uncore, GEN6_UCGCTL1, 0, GEN6_EU_TCUNIT_CLOCK_GATE_DISABLE);
}

static void hsw_init_clock_gating(struct drm_i915_private *i915)
{
	intel_display_hsw_init_clock_gating(i915->display);

	/* This is required by WaCatErrorRejectionIssue:hsw */
	intel_uncore_rmw(&i915->uncore, GEN7_SQ_CHICKEN_MBCUNIT_CONFIG,
			 0, GEN7_SQ_CHICKEN_MBCUNIT_SQINTMOB);

	/* WaSwitchSolVfFArbitrationPriority:hsw */
	intel_uncore_rmw(&i915->uncore, GAM_ECOCHK, 0, HSW_ECOCHK_ARB_PRIO_SOL);

	intel_pch_init_clock_gating(i915->display);
}

static void ivb_init_clock_gating(struct drm_i915_private *i915)
{
	struct intel_display *display = i915->display;

	intel_display_ivb_init_clock_gating(display);

	/* WaDisableBackToBackFlipFix:ivb */
	intel_uncore_write(&i915->uncore, IVB_CHICKEN3,
			   CHICKEN3_DGMG_REQ_OUT_FIX_DISABLE |
			   CHICKEN3_DGMG_DONE_FIX_DISABLE);

	if (INTEL_INFO(i915)->gt == 1)
		intel_uncore_write(&i915->uncore, GEN7_ROW_CHICKEN2,
				   REG_MASKED_FIELD_ENABLE(DOP_CLOCK_GATING_DISABLE));
	else {
		/* must write both registers */
		intel_uncore_write(&i915->uncore, GEN7_ROW_CHICKEN2,
				   REG_MASKED_FIELD_ENABLE(DOP_CLOCK_GATING_DISABLE));
		intel_uncore_write(&i915->uncore, GEN7_ROW_CHICKEN2_GT2,
				   REG_MASKED_FIELD_ENABLE(DOP_CLOCK_GATING_DISABLE));
	}

	/*
	 * According to the spec, bit 13 (RCZUNIT) must be set on IVB.
	 * This implements the WaDisableRCZUnitClockGating:ivb workaround.
	 */
	intel_uncore_write(&i915->uncore, GEN6_UCGCTL2,
			   GEN6_RCZUNIT_CLOCK_GATE_DISABLE);

	/* This is required by WaCatErrorRejectionIssue:ivb */
	intel_uncore_rmw(&i915->uncore, GEN7_SQ_CHICKEN_MBCUNIT_CONFIG,
			 0, GEN7_SQ_CHICKEN_MBCUNIT_SQINTMOB);

	intel_display_disable_trickle_feed(display);

	intel_uncore_rmw(&i915->uncore, GEN6_MBCUNIT_SNPCR, GEN6_MBC_SNPCR_MASK,
			 GEN6_MBC_SNPCR_MED);

	intel_pch_init_clock_gating(display);

	gen6_check_mch_setup(i915);
}

static void vlv_init_clock_gating(struct drm_i915_private *i915)
{
	/* WaDisableBackToBackFlipFix:vlv */
	intel_uncore_write(&i915->uncore, IVB_CHICKEN3,
			   CHICKEN3_DGMG_REQ_OUT_FIX_DISABLE |
			   CHICKEN3_DGMG_DONE_FIX_DISABLE);

	/* WaDisableDopClockGating:vlv */
	intel_uncore_write(&i915->uncore, GEN7_ROW_CHICKEN2,
			   REG_MASKED_FIELD_ENABLE(DOP_CLOCK_GATING_DISABLE));

	/* This is required by WaCatErrorRejectionIssue:vlv */
	intel_uncore_rmw(&i915->uncore, GEN7_SQ_CHICKEN_MBCUNIT_CONFIG,
			 0, GEN7_SQ_CHICKEN_MBCUNIT_SQINTMOB);

	/*
	 * According to the spec, bit 13 (RCZUNIT) must be set on IVB.
	 * This implements the WaDisableRCZUnitClockGating:vlv workaround.
	 */
	intel_uncore_write(&i915->uncore, GEN6_UCGCTL2,
			   GEN6_RCZUNIT_CLOCK_GATE_DISABLE);

	/* WaDisableL3Bank2xClockGate:vlv
	 * Disabling L3 clock gating- MMIO 940c[25] = 1
	 * Set bit 25, to disable L3_BANK_2x_CLK_GATING */
	intel_uncore_rmw(&i915->uncore, GEN7_UCGCTL4, 0, GEN7_L3BANK2X_CLOCK_GATE_DISABLE);

	/*
	 * WaDisableVLVClockGating_VBIIssue:vlv
	 * Disable clock gating on th GCFG unit to prevent a delay
	 * in the reporting of vblank events.
	 */
	intel_uncore_write(&i915->uncore, VLV_GUNIT_CLOCK_GATE, GCFG_DIS);
}

static void chv_init_clock_gating(struct drm_i915_private *i915)
{
	/* WaVSRefCountFullforceMissDisable:chv */
	/* WaDSRefCountFullforceMissDisable:chv */
	intel_uncore_rmw(&i915->uncore, GEN7_FF_THREAD_MODE,
			 GEN8_FF_DS_REF_CNT_FFME | GEN7_FF_VS_REF_CNT_FFME, 0);

	/* WaDisableSemaphoreAndSyncFlipWait:chv */
	intel_uncore_write(&i915->uncore, RING_PSMI_CTL(RENDER_RING_BASE),
			   REG_MASKED_FIELD_ENABLE(GEN8_RC_SEMA_IDLE_MSG_DISABLE));

	/* WaDisableCSUnitClockGating:chv */
	intel_uncore_rmw(&i915->uncore, GEN6_UCGCTL1, 0, GEN6_CSUNIT_CLOCK_GATE_DISABLE);

	/* WaDisableSDEUnitClockGating:chv */
	intel_uncore_rmw(&i915->uncore, GEN8_UCGCTL6, 0, GEN8_SDEUNIT_CLOCK_GATE_DISABLE);

	/*
	 * WaProgramL3SqcReg1Default:chv
	 * See gfxspecs/Related Documents/Performance Guide/
	 * LSQC Setting Recommendations.
	 */
	gen8_set_l3sqc_credits(i915, 38, 2);
}

static void g4x_init_clock_gating(struct drm_i915_private *i915)
{
	intel_uncore_write(&i915->uncore, RENCLK_GATE_D1, 0);
	intel_uncore_write(&i915->uncore, RENCLK_GATE_D2, VF_UNIT_CLOCK_GATE_DISABLE |
			   GS_UNIT_CLOCK_GATE_DISABLE |
			   CL_UNIT_CLOCK_GATE_DISABLE);
	intel_uncore_write(&i915->uncore, RAMCLK_GATE_D, 0);
	intel_display_g4x_init_clock_gating(i915->display);
}

static void i965gm_init_clock_gating(struct drm_i915_private *i915)
{
	struct intel_uncore *uncore = &i915->uncore;

	intel_uncore_write(uncore, RENCLK_GATE_D1, I965_RCC_CLOCK_GATE_DISABLE);
	intel_uncore_write(uncore, RENCLK_GATE_D2, 0);
	intel_display_i965gm_init_clock_gating(i915->display);
	intel_uncore_write(uncore, RAMCLK_GATE_D, 0);
	intel_uncore_write16(uncore, DEUC, 0);
	intel_uncore_write(uncore,
			   MI_ARB_STATE,
			   REG_MASKED_FIELD_ENABLE(MI_ARB_DISPLAY_TRICKLE_FEED_DISABLE));
}

static void i965g_init_clock_gating(struct drm_i915_private *i915)
{
	intel_uncore_write(&i915->uncore, RENCLK_GATE_D1, I965_RCZ_CLOCK_GATE_DISABLE |
			   I965_RCC_CLOCK_GATE_DISABLE |
			   I965_RCPB_CLOCK_GATE_DISABLE |
			   I965_ISC_CLOCK_GATE_DISABLE |
			   I965_FBC_CLOCK_GATE_DISABLE);
	intel_uncore_write(&i915->uncore, RENCLK_GATE_D2, 0);
	intel_uncore_write(&i915->uncore, MI_ARB_STATE,
			   REG_MASKED_FIELD_ENABLE(MI_ARB_DISPLAY_TRICKLE_FEED_DISABLE));
}

static void gen3_init_clock_gating(struct drm_i915_private *i915)
{
	u32 dstate = intel_uncore_read(&i915->uncore, D_STATE);

	dstate |= DSTATE_PLL_D3_OFF | DSTATE_GFX_CLOCK_GATING |
		DSTATE_DOT_CLOCK_GATING;
	intel_uncore_write(&i915->uncore, D_STATE, dstate);

	if (IS_PINEVIEW(i915))
		intel_uncore_write(&i915->uncore, ECOSKPD(RENDER_RING_BASE),
				   REG_MASKED_FIELD_ENABLE(ECO_GATING_CX_ONLY));

	/* IIR "flip pending" means done if this bit is set */
	intel_uncore_write(&i915->uncore, ECOSKPD(RENDER_RING_BASE),
			   REG_MASKED_FIELD_DISABLE(ECO_FLIP_DONE));

	/* interrupts should cause a wake up from C3 */
	intel_uncore_write(&i915->uncore, INSTPM, REG_MASKED_FIELD_ENABLE(INSTPM_AGPBUSY_INT_EN));

	/* On GEN3 we really need to make sure the ARB C3 LP bit is set */
	intel_uncore_write(&i915->uncore, MI_ARB_STATE,
			   REG_MASKED_FIELD_ENABLE(MI_ARB_C3_LP_WRITE_ENABLE));

	intel_uncore_write(&i915->uncore, MI_ARB_STATE,
			   REG_MASKED_FIELD_ENABLE(MI_ARB_DISPLAY_TRICKLE_FEED_DISABLE));
}

static void i85x_init_clock_gating(struct drm_i915_private *i915)
{
	intel_uncore_write(&i915->uncore, RENCLK_GATE_D1, SV_CLOCK_GATE_DISABLE);

	/* interrupts should cause a wake up from C3 */
	intel_uncore_write(&i915->uncore, MI_STATE, REG_MASKED_FIELD_ENABLE(MI_AGPBUSY_INT_EN) |
			   REG_MASKED_FIELD_DISABLE(MI_AGPBUSY_830_MODE));

	intel_uncore_write(&i915->uncore, MEM_MODE,
			   REG_MASKED_FIELD_ENABLE(MEM_DISPLAY_TRICKLE_FEED_DISABLE));

	/*
	 * Have FBC ignore 3D activity since we use software
	 * render tracking, and otherwise a pure 3D workload
	 * (even if it just renders a single frame and then does
	 * absolutely nothing) would not allow FBC to recompress
	 * until a 2D blit occurs.
	 */
	intel_uncore_write(&i915->uncore, SCPD0,
			   REG_MASKED_FIELD_ENABLE(SCPD_FBC_IGNORE_3D));
}

static void i830_init_clock_gating(struct drm_i915_private *i915)
{
	intel_uncore_write(&i915->uncore, MEM_MODE,
			   REG_MASKED_FIELD_ENABLE(MEM_DISPLAY_A_TRICKLE_FEED_DISABLE) |
			   REG_MASKED_FIELD_ENABLE(MEM_DISPLAY_B_TRICKLE_FEED_DISABLE));
}

void intel_clock_gating_init(struct drm_device *drm)
{
	struct drm_i915_private *i915 = to_i915(drm);

	i915->clock_gating_funcs->init_clock_gating(i915);
}

static void nop_init_clock_gating(struct drm_i915_private *i915)
{
	drm_dbg_kms(&i915->drm,
		    "No clock gating settings or workarounds applied.\n");
}

#define CG_FUNCS(platform)						\
static const struct drm_i915_clock_gating_funcs platform##_clock_gating_funcs = { \
	.init_clock_gating = platform##_init_clock_gating,		\
}

CG_FUNCS(dg2);
CG_FUNCS(cfl);
CG_FUNCS(skl);
CG_FUNCS(kbl);
CG_FUNCS(bxt);
CG_FUNCS(glk);
CG_FUNCS(bdw);
CG_FUNCS(chv);
CG_FUNCS(hsw);
CG_FUNCS(ivb);
CG_FUNCS(vlv);
CG_FUNCS(gen6);
CG_FUNCS(ilk);
CG_FUNCS(g4x);
CG_FUNCS(i965gm);
CG_FUNCS(i965g);
CG_FUNCS(gen3);
CG_FUNCS(i85x);
CG_FUNCS(i830);
CG_FUNCS(nop);
#undef CG_FUNCS

/**
 * intel_clock_gating_hooks_init - setup the clock gating hooks
 * @drm: drm device
 *
 * Setup the hooks that configure which clocks of a given platform can be
 * gated and also apply various GT and display specific workarounds for these
 * platforms. Note that some GT specific workarounds are applied separately
 * when GPU contexts or batchbuffers start their execution.
 */
void intel_clock_gating_hooks_init(struct drm_device *drm)
{
	struct drm_i915_private *i915 = to_i915(drm);

	if (IS_DG2(i915))
		i915->clock_gating_funcs = &dg2_clock_gating_funcs;
	else if (IS_COFFEELAKE(i915) || IS_COMETLAKE(i915))
		i915->clock_gating_funcs = &cfl_clock_gating_funcs;
	else if (IS_SKYLAKE(i915))
		i915->clock_gating_funcs = &skl_clock_gating_funcs;
	else if (IS_KABYLAKE(i915))
		i915->clock_gating_funcs = &kbl_clock_gating_funcs;
	else if (IS_BROXTON(i915))
		i915->clock_gating_funcs = &bxt_clock_gating_funcs;
	else if (IS_GEMINILAKE(i915))
		i915->clock_gating_funcs = &glk_clock_gating_funcs;
	else if (IS_BROADWELL(i915))
		i915->clock_gating_funcs = &bdw_clock_gating_funcs;
	else if (IS_CHERRYVIEW(i915))
		i915->clock_gating_funcs = &chv_clock_gating_funcs;
	else if (IS_HASWELL(i915))
		i915->clock_gating_funcs = &hsw_clock_gating_funcs;
	else if (IS_IVYBRIDGE(i915))
		i915->clock_gating_funcs = &ivb_clock_gating_funcs;
	else if (IS_VALLEYVIEW(i915))
		i915->clock_gating_funcs = &vlv_clock_gating_funcs;
	else if (GRAPHICS_VER(i915) == 6)
		i915->clock_gating_funcs = &gen6_clock_gating_funcs;
	else if (GRAPHICS_VER(i915) == 5)
		i915->clock_gating_funcs = &ilk_clock_gating_funcs;
	else if (IS_G4X(i915))
		i915->clock_gating_funcs = &g4x_clock_gating_funcs;
	else if (IS_I965GM(i915))
		i915->clock_gating_funcs = &i965gm_clock_gating_funcs;
	else if (IS_I965G(i915))
		i915->clock_gating_funcs = &i965g_clock_gating_funcs;
	else if (GRAPHICS_VER(i915) == 3)
		i915->clock_gating_funcs = &gen3_clock_gating_funcs;
	else if (IS_I85X(i915) || IS_I865G(i915))
		i915->clock_gating_funcs = &i85x_clock_gating_funcs;
	else if (GRAPHICS_VER(i915) == 2)
		i915->clock_gating_funcs = &i830_clock_gating_funcs;
	else
		i915->clock_gating_funcs = &nop_clock_gating_funcs;
}
