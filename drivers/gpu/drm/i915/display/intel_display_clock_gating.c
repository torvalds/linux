// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Intel Corporation
 */

#include <drm/intel/intel_gmd_misc_regs.h>

#include "intel_de.h"
#include "i9xx_plane_regs.h"
#include "intel_display.h"
#include "intel_display_clock_gating.h"
#include "intel_display_core.h"
#include "intel_display_regs.h"

static void intel_display_gen9_init_clock_gating(struct intel_display *display)
{
	/* See Bspec note for PSR2_CTL bit 31, Wa#828:skl,bxt,kbl,cfl */
	intel_de_rmw(display, CHICKEN_PAR1_1, 0, SKL_EDP_PSR_FIX_RDWRAP);

	/* WaEnableChickenDCPR:skl,bxt,kbl,glk,cfl */
	intel_de_rmw(display, GEN8_CHICKEN_DCPR_1, 0, MASK_WAKEMEM);

	/*
	 * WaFbcWakeMemOn:skl,bxt,kbl,glk,cfl
	 * Display WA #0859: skl,bxt,kbl,glk,cfl
	 */
	intel_de_rmw(display, DISP_ARB_CTL, 0, DISP_FBC_MEMORY_WAKE);
}

void intel_display_skl_init_clock_gating(struct intel_display *display)
{
	/*
	 * WaCompressedResourceDisplayNewHashMode:skl,kbl
	 * Display WA #0390: skl,kbl
	 *
	 * Must match Sampler, Pixel Back End, and Media. See
	 * WaCompressedResourceSamplerPbeMediaNewHashMode.
	 */
	intel_de_rmw(display, CHICKEN_PAR1_1, 0, SKL_DE_COMPRESSED_HASH_MODE);

	intel_display_gen9_init_clock_gating(display);

	/*
	 * WaFbcTurnOffFbcWatermark:skl
	 * Display WA #0562: skl
	 */
	intel_de_rmw(display, DISP_ARB_CTL, 0, DISP_FBC_WM_DIS);
}

void intel_display_kbl_init_clock_gating(struct intel_display *display)
{
	/*
	 * WaCompressedResourceDisplayNewHashMode:skl,kbl
	 * Display WA #0390: skl,kbl
	 *
	 * Must match Sampler, Pixel Back End, and Media. See
	 * WaCompressedResourceSamplerPbeMediaNewHashMode.
	 */
	intel_de_rmw(display, CHICKEN_PAR1_1, 0, SKL_DE_COMPRESSED_HASH_MODE);

	intel_display_gen9_init_clock_gating(display);

	/*
	 * WaFbcTurnOffFbcWatermark:kbl
	 * Display WA #0562: kbl
	 */
	intel_de_rmw(display, DISP_ARB_CTL, 0, DISP_FBC_WM_DIS);
}

void intel_display_cfl_init_clock_gating(struct intel_display *display)
{
	/*
	 * WaCompressedResourceDisplayNewHashMode:skl,kbl (and cfl, cml)
	 * Display WA #0390: skl,kbl (and cfl, cml)
	 *
	 * Must match Sampler, Pixel Back End, and Media. See
	 * WaCompressedResourceSamplerPbeMediaNewHashMode.
	 *
	 * NOTE: this is the same workaround used for skl and kbl,
	 * because the original implementation was checking HAS_LLC(),
	 * which cfl/cml have, even though the comment for the
	 * workaround doesn't mention it.
	 *
	 */
	intel_de_rmw(display, CHICKEN_PAR1_1, 0, SKL_DE_COMPRESSED_HASH_MODE);

	intel_display_gen9_init_clock_gating(display);

	/*
	 * WaFbcTurnOffFbcWatermark:cfl
	 * Display WA #0562: cfl
	 */
	intel_de_rmw(display, DISP_ARB_CTL, 0, DISP_FBC_WM_DIS);
}

void intel_display_bxt_init_clock_gating(struct intel_display *display)
{
	intel_display_gen9_init_clock_gating(display);

	/*
	 * Wa: Backlight PWM may stop in the asserted state, causing backlight
	 * to stay fully on.
	 */
	intel_de_write(display, GEN9_CLKGATE_DIS_0,
		       intel_de_read(display, GEN9_CLKGATE_DIS_0) |
		       PWM1_GATING_DIS | PWM2_GATING_DIS);

	/*
	 * Lower the display internal timeout.
	 * This is needed to avoid any hard hangs when DSI port PLL
	 * is off and a MMIO access is attempted by any privilege
	 * application, using batch buffers or any other means.
	 */
	intel_de_write(display, RM_TIMEOUT, MMIO_TIMEOUT_US(950));

	/*
	 * WaFbcTurnOffFbcWatermark:bxt
	 * Display WA #0562: bxt
	 */
	intel_de_rmw(display, DISP_ARB_CTL, 0, DISP_FBC_WM_DIS);
}

void intel_display_glk_init_clock_gating(struct intel_display *display)
{
	intel_display_gen9_init_clock_gating(display);

	/*
	 * WaDisablePWMClockGating:glk
	 * Backlight PWM may stop in the asserted state, causing backlight
	 * to stay fully on.
	 */
	intel_de_write(display, GEN9_CLKGATE_DIS_0,
		       intel_de_read(display, GEN9_CLKGATE_DIS_0) |
		       PWM1_GATING_DIS | PWM2_GATING_DIS);
}

void intel_display_bdw_clock_gating_disable_fbcq(struct intel_display *display)
{
	/* WaFbcAsynchFlipDisableFbcQueue:hsw,bdw */
	intel_de_rmw(display, CHICKEN_PIPESL_1(PIPE_A), 0, HSW_FBCQ_DIS);
}

void intel_display_bdw_clock_gating_vblank_in_srd(struct intel_display *display)
{
	enum pipe pipe;

	/* WaPsrDPAMaskVBlankInSRD:hsw */
	intel_de_rmw(display, CHICKEN_PAR1_1, 0, HSW_MASK_VBL_TO_PIPE_IN_SRD);

	for_each_pipe(display, pipe) {
		/* WaPsrDPRSUnmaskVBlankInSRD:hsw,bdw */
		intel_de_rmw(display, CHICKEN_PIPESL_1(pipe), 0,
			     BDW_UNMASK_VBL_TO_REGS_IN_SRD);
	}
}

void intel_display_bdw_clock_gating_kvm_notif(struct intel_display *display)
{
	/* WaKVMNotificationOnConfigChange:bdw */
	intel_de_rmw(display, CHICKEN_PAR2_1, 0,
		     KVM_CONFIG_CHANGE_NOTIFICATION_SELECT);
}

void intel_display_hsw_init_clock_gating(struct intel_display *display)
{
	enum pipe pipe;

	/* WaFbcAsynchFlipDisableFbcQueue:hsw,bdw */
	intel_de_rmw(display, CHICKEN_PIPESL_1(PIPE_A), 0, HSW_FBCQ_DIS);

	/* WaPsrDPAMaskVBlankInSRD:hsw */
	intel_de_rmw(display, CHICKEN_PAR1_1, 0, HSW_MASK_VBL_TO_PIPE_IN_SRD);

	for_each_pipe(display, pipe) {
		/* WaPsrDPRSUnmaskVBlankInSRD:hsw,bdw */
		intel_de_rmw(display, CHICKEN_PIPESL_1(pipe), 0,
			     HSW_UNMASK_VBL_TO_REGS_IN_SRD);
	}
}

void intel_display_disable_trickle_feed(struct intel_display *display)
{
	enum pipe pipe;

	for_each_pipe(display, pipe) {
		intel_de_rmw(display, DSPCNTR(display, pipe), 0,
			     DISP_TRICKLE_FEED_DISABLE);

		intel_de_rmw(display, DSPSURF(display, pipe), 0, 0);
		intel_de_posting_read(display, DSPSURF(display, pipe));
	}
}

void intel_display_ilk_init_clock_gating(struct intel_display *display)
{
	u32 dspclk_gate = ILK_VRHUNIT_CLOCK_GATE_DISABLE;

	/*
	 * Required for FBC
	 * WaFbcDisableDpfcClockGating:ilk
	 */
	dspclk_gate |= ILK_DPFCRUNIT_CLOCK_GATE_DISABLE |
		       ILK_DPFCUNIT_CLOCK_GATE_DISABLE |
		       ILK_DPFDUNIT_CLOCK_GATE_ENABLE;

	intel_de_write(display, ILK_DISPLAY_CHICKEN2,
		       intel_de_read(display, ILK_DISPLAY_CHICKEN2) |
		       ILK_DPARB_GATE | ILK_VSDPFD_FULL);
	dspclk_gate |= ILK_DPARBUNIT_CLOCK_GATE_ENABLE;
	intel_de_write(display, DISP_ARB_CTL,
		       intel_de_read(display, DISP_ARB_CTL) |
		       DISP_FBC_WM_DIS);

	if (display->platform.ironlake && display->platform.mobile) {
		/* WaFbcAsynchFlipDisableFbcQueue:ilk */
		intel_de_rmw(display, ILK_DISPLAY_CHICKEN1, 0, ILK_FBCQ_DIS);
		intel_de_rmw(display, ILK_DISPLAY_CHICKEN2, 0, ILK_DPARB_GATE);
	}

	intel_de_write(display, ILK_DSPCLK_GATE_D, dspclk_gate);
	intel_de_rmw(display, ILK_DISPLAY_CHICKEN2, 0, ILK_ELPIN_409_SELECT);

	intel_display_disable_trickle_feed(display);
}

void intel_display_gen6_init_clock_gating(struct intel_display *display)
{
	u32 dspclk_gate = ILK_VRHUNIT_CLOCK_GATE_DISABLE;

	intel_de_write(display, ILK_DSPCLK_GATE_D, dspclk_gate);
	intel_de_rmw(display, ILK_DISPLAY_CHICKEN2, 0, ILK_ELPIN_409_SELECT);

	intel_de_write(display, ILK_DISPLAY_CHICKEN1,
		       intel_de_read(display, ILK_DISPLAY_CHICKEN1) |
		       ILK_FBCQ_DIS | ILK_PABSTRETCH_DIS);
	intel_de_write(display, ILK_DISPLAY_CHICKEN2,
		       intel_de_read(display, ILK_DISPLAY_CHICKEN2) |
		       ILK_DPARB_GATE | ILK_VSDPFD_FULL);
	intel_de_write(display, ILK_DSPCLK_GATE_D,
		       intel_de_read(display, ILK_DSPCLK_GATE_D) |
		       ILK_DPARBUNIT_CLOCK_GATE_ENABLE |
		       ILK_DPFDUNIT_CLOCK_GATE_ENABLE);

	intel_display_disable_trickle_feed(display);
}

void intel_display_ivb_init_clock_gating(struct intel_display *display)
{
	intel_de_write(display, ILK_DSPCLK_GATE_D, ILK_VRHUNIT_CLOCK_GATE_DISABLE);
	intel_de_rmw(display, ILK_DISPLAY_CHICKEN1, 0, ILK_FBCQ_DIS);
}

void intel_display_g4x_init_clock_gating(struct intel_display *display)
{
	u32 dspclk_gate = VRHUNIT_CLOCK_GATE_DISABLE |
			  OVRUNIT_CLOCK_GATE_DISABLE |
			  OVCUNIT_CLOCK_GATE_DISABLE;

	if (display->platform.gm45)
		dspclk_gate |= DSSUNIT_CLOCK_GATE_DISABLE;

	intel_de_write(display, DSPCLK_GATE_D, dspclk_gate);

	intel_display_disable_trickle_feed(display);
}

void intel_display_i965gm_init_clock_gating(struct intel_display *display)
{
	intel_de_write(display, DSPCLK_GATE_D, 0);
}
