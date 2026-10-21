// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Intel Corporation
 */

#include <linux/string_choices.h>

#include <drm/drm_device.h>
#include <drm/drm_print.h>

#include "intel_cmtg.h"
#include "intel_cmtg_regs.h"
#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display.h"
#include "intel_display_device.h"
#include "intel_display_irq.h"
#include "intel_display_power.h"
#include "intel_display_regs.h"
#include "intel_display_types.h"
#include "intel_vrr.h"
#include "intel_vrr_regs.h"

/**
 * DOC: Common Primary Timing Generator (CMTG)
 *
 * The CMTG is a timing generator that runs in parallel to transcoders timing
 * generators (TG) to provide a synchronization mechanism where CMTG acts as
 * primary and transcoders TGs act as secondary to the CMTG. The CMTG outputs
 * its TG start and frame sync signals to the transcoders that are configured
 * as secondary, which use those signals to synchronize their own timing with
 * the CMTG's.
 *
 * The CMTG can be used only with eDP or MIPI command mode and supports the
 * following use cases:
 *
 * - Dual eDP: The CMTG can be used to keep two eDP TGs in sync when on a
 *   dual eDP configuration (with or without PSR/PSR2 enabled).
 *
 * - Single eDP as secondary: It is also possible to use a single eDP
 *   configuration with the transcoder TG as secondary to the CMTG. That would
 *   allow a flow that would not require a modeset on the existing eDP when a
 *   new eDP is added for a dual eDP configuration with CMTG.
 *
 * - DC6v: In DC6v, the transcoder might be off but the CMTG keeps running to
 *   maintain frame timings. When exiting DC6v, the transcoder TG then is
 *   synced back the CMTG.
 *
 * Currently, the driver does not use the CMTG, but we need to make sure that
 * we disable it in case we inherit a display configuration with it enabled.
 */

/*
 * We describe here only the minimum data required to allow us to properly
 * disable the CMTG if necessary.
 */
struct intel_cmtg_config {
	bool cmtg_a_enable;
	/*
	 * Xe2_LPD adds a second CMTG that can be used for dual eDP async mode.
	 */
	bool cmtg_b_enable;
	bool trans_a_secondary;
	bool trans_b_secondary;
};

static bool intel_cmtg_has_cmtg_b(struct intel_display *display)
{
	return DISPLAY_VER(display) >= 20;
}

static bool intel_cmtg_has_clock_sel(struct intel_display *display)
{
	return DISPLAY_VER(display) >= 14;
}

static void intel_cmtg_dump_config(struct intel_display *display,
				   struct intel_cmtg_config *cmtg_config)
{
	drm_dbg_kms(display->drm,
		    "CMTG readout: CMTG A: %s, CMTG B: %s, Transcoder A secondary: %s, Transcoder B secondary: %s\n",
		    str_enabled_disabled(cmtg_config->cmtg_a_enable),
		    intel_cmtg_has_cmtg_b(display) ? str_enabled_disabled(cmtg_config->cmtg_b_enable) : "n/a",
		    str_yes_no(cmtg_config->trans_a_secondary),
		    str_yes_no(cmtg_config->trans_b_secondary));
}

static inline enum transcoder to_cmtg_transcoder(enum transcoder cpu_transcoder)
{
	switch (cpu_transcoder) {
	case TRANSCODER_A:
		return TRANSCODER_CMTG0;
	case TRANSCODER_B:
		return TRANSCODER_CMTG1;
	default:
		return INVALID_TRANSCODER;
	}
}

static bool intel_cmtg_transcoder_is_secondary(struct intel_display *display,
					       enum transcoder trans)
{
	enum intel_display_power_domain power_domain;
	u32 val = 0;

	if (!HAS_TRANSCODER(display, trans))
		return false;

	power_domain = POWER_DOMAIN_TRANSCODER(trans);

	with_intel_display_power_if_enabled(display, power_domain)
		val = intel_de_read(display, TRANS_DDI_FUNC_CTL2(display, trans));

	return val & CMTG_SECONDARY_MODE;
}

static void intel_cmtg_get_config(struct intel_display *display,
				  struct intel_cmtg_config *cmtg_config)
{
	u32 val;

	val = intel_de_read(display, TRANS_CMTG_CTL(TRANSCODER_A));
	cmtg_config->cmtg_a_enable = val & CMTG_ENABLE;

	if (intel_cmtg_has_cmtg_b(display)) {
		val = intel_de_read(display, TRANS_CMTG_CTL(TRANSCODER_B));
		cmtg_config->cmtg_b_enable = val & CMTG_ENABLE;
	}

	cmtg_config->trans_a_secondary = intel_cmtg_transcoder_is_secondary(display, TRANSCODER_A);
	cmtg_config->trans_b_secondary = intel_cmtg_transcoder_is_secondary(display, TRANSCODER_B);
}

static bool intel_cmtg_disable_requires_modeset(struct intel_display *display,
						struct intel_cmtg_config *cmtg_config)
{
	if (DISPLAY_VER(display) >= 20)
		return false;

	return cmtg_config->trans_a_secondary || cmtg_config->trans_b_secondary;
}

static void intel_cmtg_disable_all(struct intel_display *display,
				   struct intel_cmtg_config *cmtg_config)
{
	u32 clk_sel_clr = 0;
	u32 clk_sel_set = 0;

	if (cmtg_config->trans_a_secondary)
		intel_de_rmw(display, TRANS_DDI_FUNC_CTL2(display, TRANSCODER_A),
			     CMTG_SECONDARY_MODE, 0);

	if (cmtg_config->trans_b_secondary)
		intel_de_rmw(display, TRANS_DDI_FUNC_CTL2(display, TRANSCODER_B),
			     CMTG_SECONDARY_MODE, 0);

	if (cmtg_config->cmtg_a_enable) {
		drm_dbg_kms(display->drm, "Disabling CMTG A\n");
		intel_de_rmw(display, TRANS_CMTG_CTL(TRANSCODER_A), CMTG_ENABLE, 0);
		clk_sel_clr |= CMTG_CLK_SEL_A_MASK;
		clk_sel_set |= CMTG_CLK_SEL_A_DISABLED;
	}

	if (cmtg_config->cmtg_b_enable) {
		drm_dbg_kms(display->drm, "Disabling CMTG B\n");
		intel_de_rmw(display, TRANS_CMTG_CTL(TRANSCODER_B), CMTG_ENABLE, 0);
		clk_sel_clr |= CMTG_CLK_SEL_B_MASK;
		clk_sel_set |= CMTG_CLK_SEL_B_DISABLED;
	}

	if (intel_cmtg_has_clock_sel(display) && clk_sel_clr)
		intel_de_rmw(display, CMTG_CLK_SEL, clk_sel_clr, clk_sel_set);
}

void intel_cmtg_disable(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	enum transcoder cmtg_transcoder = to_cmtg_transcoder(crtc_state->cpu_transcoder);
	u32 clk_sel_clr = 0, interrupt_mask = 0;

	if (!crtc->cmtg.enabled)
		return;

	if (drm_WARN_ON(display->drm, cmtg_transcoder == INVALID_TRANSCODER))
		return;

	crtc->cmtg.enabled = false;
	intel_de_rmw(display, TRANS_VRR_CTL(display, cmtg_transcoder),
		     VRR_CTL_VRR_ENABLE | VRR_CTL_FLIP_LINE_EN, 0);

	/*
	 * Use cpu_transcoder for:
	 * 1. Exclusive CMTG registers that do not use the standard transcoder offset
	 *    (e.g., TRANS_CMTG_CTL, CMTG_CLK_SEL).
	 * 2. Registers shared between the eDP and CMTG transcoders.
	 *    (e.g., TRANS_DDI_FUNC_CTL2).
	 */

	intel_de_rmw(display, TRANS_DDI_FUNC_CTL2(display, cpu_transcoder),
		     CMTG_SECONDARY_MODE, 0);
	intel_de_rmw(display, CMTG_SCANLINE_GB1(cpu_transcoder), CMTG_HW_GB_ENABLE, 0);

	intel_de_rmw(display, TRANS_CMTG_CTL(cpu_transcoder), CMTG_ENABLE, 0);

	if (intel_de_wait_for_clear_ms(display, TRANS_CMTG_CTL(cpu_transcoder), CMTG_STATE, 50)) {
		drm_WARN(display->drm, 1, "CMTG: %s disable timeout\n",
			 transcoder_name(cpu_transcoder));
		return;
	}

	clk_sel_clr = cpu_transcoder == TRANSCODER_A ? CMTG_CLK_SEL_A_MASK : CMTG_CLK_SEL_B_MASK;
	intel_de_rmw(display, CMTG_CLK_SEL, clk_sel_clr, 0);

	drm_dbg_kms(display->drm, "CMTG: %s disabled\n", transcoder_name(cpu_transcoder));

	if (cpu_transcoder == TRANSCODER_A)
		interrupt_mask = CMTG_VBLANK_A;
	else if (cpu_transcoder == TRANSCODER_B)
		interrupt_mask = CMTG_VBLANK_B;

	intel_display_irq_port_interrupt_mask(display, interrupt_mask, true);
}

/*
 * Read out CMTG configuration and, on platforms that allow disabling it without
 * a modeset, do it.
 *
 * This function must be called before any port PLL is disabled in the general
 * sanitization process, because we need whatever port PLL that is providing the
 * clock for CMTG to be on before accessing CMTG registers.
 */
void intel_cmtg_sanitize(struct intel_display *display)
{
	struct intel_cmtg_config cmtg_config = {};

	if (!HAS_CMTG(display))
		return;

	intel_cmtg_get_config(display, &cmtg_config);
	intel_cmtg_dump_config(display, &cmtg_config);

	/*
	 * FIXME: The driver is not prepared to handle cases where a modeset is
	 * required for disabling the CMTG: we need a proper way of tracking
	 * CMTG state and do the right syncronization with respect to triggering
	 * the modeset as part of the disable sequence.
	 */
	if (intel_cmtg_disable_requires_modeset(display, &cmtg_config))
		return;

	intel_cmtg_disable_all(display, &cmtg_config);
}

bool intel_cmtg_is_allowed(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if ((cpu_transcoder == TRANSCODER_A || cpu_transcoder == TRANSCODER_B) &&
	    DISPLAY_VER(display) == 35 && intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP))
		return true;

	return false;
}

void intel_cmtg_set_clk_select(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 clk_sel_clr = 0;
	u32 clk_sel_set = 0;

	if (!intel_cmtg_is_allowed(crtc_state))
		return;

	if (cpu_transcoder == TRANSCODER_A) {
		clk_sel_clr = CMTG_CLK_SEL_A_MASK;
		clk_sel_set = CMTG_CLK_SELECT_PHYA_ENABLE;
	} else if (cpu_transcoder == TRANSCODER_B) {
		clk_sel_clr = CMTG_CLK_SEL_B_MASK;
		clk_sel_set = CMTG_CLK_SELECT_PHYB_ENABLE;
	}

	if (clk_sel_set)
		intel_de_rmw(display, CMTG_CLK_SEL, clk_sel_clr, clk_sel_set);
}

void intel_cmtg_set_timings(const struct intel_crtc_state *crtc_state, enum set_timing_type type)
{
	enum transcoder cmtg_transcoder = to_cmtg_transcoder(crtc_state->cpu_transcoder);

	if (cmtg_transcoder == INVALID_TRANSCODER)
		return;

	if (!intel_cmtg_is_allowed(crtc_state))
		return;

	if (type == LRR)
		intel_set_transcoder_timings_lrr(crtc_state, cmtg_transcoder);
	else
		intel_set_transcoder_timings(crtc_state, cmtg_transcoder);
}

void intel_cmtg_set_vrr_timings(const struct intel_crtc_state *crtc_state)
{
	enum transcoder cmtg_transcoder = to_cmtg_transcoder(crtc_state->cpu_transcoder);

	if (!intel_cmtg_is_allowed(crtc_state))
		return;

	intel_vrr_set_fixed_rr_timings(crtc_state, cmtg_transcoder);
}

void intel_cmtg_set_vrr_ctl(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cmtg_transcoder = to_cmtg_transcoder(crtc_state->cpu_transcoder);
	u32 vrr_ctl;

	if (!intel_cmtg_is_allowed(crtc_state))
		return;

	vrr_ctl = VRR_CTL_VRR_ENABLE | VRR_CTL_FLIP_LINE_EN |
		  XELPD_VRR_CTL_VRR_GUARDBAND(crtc_state->vrr.guardband);

	/* TODO: The code below may need to be revisited once CMRR is enabled */
	if (crtc_state->cmrr.enable)
		vrr_ctl |= VRR_CTL_CMRR_ENABLE;

	intel_de_write(display, TRANS_VRR_CTL(display, cmtg_transcoder), vrr_ctl);
}

void intel_cmtg_set_m_n(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cmtg_transcoder = to_cmtg_transcoder(crtc_state->cpu_transcoder);
	const struct intel_link_m_n *m_n = &crtc_state->dp_m_n;

	if (!intel_cmtg_is_allowed(crtc_state))
		return;

	intel_de_write(display, PIPE_LINK_M1(display, cmtg_transcoder), m_n->link_m);
	intel_de_write(display, PIPE_LINK_N1(display, cmtg_transcoder), m_n->link_n);
}

static void intel_cmtg_enable_sync(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 cmtg_ctl;

	cmtg_ctl = CMTG_SYNC_TO_PORT | CMTG_ENABLE;

	intel_de_rmw(display, TRANS_CMTG_CTL(cpu_transcoder), 0, cmtg_ctl);
	if (intel_de_wait_for_clear_ms(display, TRANS_CMTG_CTL(cpu_transcoder),
				       CMTG_SYNC_TO_PORT, 50)) {
		drm_WARN(display->drm, 1, "CMTG: %s enable timeout\n",
			 transcoder_name(cpu_transcoder));
	}
}

static void intel_cmtg_enable_ddi(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 interrupt_mask = 0;

	intel_de_rmw(display, TRANS_DDI_FUNC_CTL2(display, cpu_transcoder), 0, CMTG_SECONDARY_MODE);
	intel_de_rmw(display, CMTG_SCANLINE_GB1(cpu_transcoder), 0, CMTG_HW_GB_ENABLE);
	crtc->cmtg.enabled = true;
	drm_dbg_kms(display->drm, "CMTG: %s enabled\n", transcoder_name(cpu_transcoder));

	/*
	 * TODO: Currently cmtg is enabled along with eDP transcoder so cmtg
	 * interrupt is not enabled through IER, need to do some fine
	 * tuning in future.
	 */

	if (cpu_transcoder == TRANSCODER_A)
		interrupt_mask = CMTG_VBLANK_A;
	else if (cpu_transcoder == TRANSCODER_B)
		interrupt_mask = CMTG_VBLANK_B;

	intel_display_irq_port_interrupt_mask(display, interrupt_mask, false);
}

/* Bspec: 75253 */
#define DC3CO_ENTRY_LATENCY_US	55
#define DC3CO_EXIT_LATENCY_US	40

static void intel_cmtg_set_hwgb(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 breakeven_gb;
	u32 dc5_exit_latency;
	u32 line_time_us = 75;	/* Max default initialization value */
	u32 val;

	if (crtc_state->linetime)
		line_time_us = DIV_ROUND_UP(crtc_state->linetime, 8);

	/* Break Even Guardband - DC3co Entry Latency / linetime */
	breakeven_gb = DIV_ROUND_UP(DC3CO_ENTRY_LATENCY_US, line_time_us);

	/* DC5 Exit Latency - DC3co Exit Latency / linetime */
	dc5_exit_latency = DIV_ROUND_UP(DC3CO_EXIT_LATENCY_US, line_time_us);

	val = REG_FIELD_PREP(CMTG_HW_GB_BREAKEVEN_MASK, breakeven_gb) |
	      REG_FIELD_PREP(CMTG_HW_GB_DC5_EXIT_LATENCY_MASK, dc5_exit_latency) |
	      REG_FIELD_PREP(CMTG_HW_GB_UP_LW_BG_DIFF_MASK, 1);

	intel_de_write(display, CMTG_HW_GB(cpu_transcoder), val);
}

static void intel_cmtg_restore(const struct intel_crtc_state *crtc_state)
{
	intel_cmtg_set_clk_select(crtc_state);
	intel_cmtg_set_timings(crtc_state, MODESET);
	intel_cmtg_set_vrr_timings(crtc_state);
	intel_cmtg_set_vrr_ctl(crtc_state);
	intel_cmtg_set_m_n(crtc_state);
}

void intel_cmtg_program(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc *crtc;
	struct intel_crtc_state *new_crtc_state;
	bool dc3co_to_dc6 = intel_display_power_get_and_reset_dc3co_to_dc6(display);

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state) {
		bool modeset = intel_crtc_needs_modeset(new_crtc_state);

		if (!intel_cmtg_is_allowed(new_crtc_state))
			continue;

		if ((modeset || dc3co_to_dc6) &&
		    new_crtc_state->hw.active && !crtc->cmtg.enabled) {
			if (dc3co_to_dc6)
				intel_cmtg_restore(new_crtc_state);

			intel_cmtg_enable_sync(new_crtc_state);
			intel_cmtg_set_hwgb(new_crtc_state);
			intel_cmtg_enable_ddi(new_crtc_state);
		}
	}
}
