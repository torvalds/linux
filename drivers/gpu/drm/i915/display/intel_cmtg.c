// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Intel Corporation
 */

#include <linux/string_choices.h>
#include <linux/types.h>

#include <drm/drm_device.h>
#include <drm/drm_print.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_crtc.h"
#include "intel_cmtg.h"
#include "intel_cmtg_regs.h"
#include "intel_de.h"
#include "intel_display_device.h"
#include "intel_display_power.h"

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

static bool intel_cmtg_transcoder_is_secondary(struct intel_display *display,
					       enum transcoder trans)
{
	enum intel_display_power_domain power_domain;
	intel_wakeref_t wakeref;
	u32 val = 0;

	if (!HAS_TRANSCODER(display, trans))
		return false;

	power_domain = POWER_DOMAIN_TRANSCODER(trans);

	with_intel_display_power_if_enabled(display, power_domain, wakeref)
		val = intel_de_read(display, TRANS_DDI_FUNC_CTL2(display, trans));

	return val & CMTG_SECONDARY_MODE;
}

static void intel_cmtg_get_config(struct intel_display *display,
				  struct intel_cmtg_config *cmtg_config)
{
	u32 val;

	val = intel_de_read(display, TRANS_CMTG_CTL_A);
	cmtg_config->cmtg_a_enable = val & CMTG_ENABLE;

	if (intel_cmtg_has_cmtg_b(display)) {
		val = intel_de_read(display, TRANS_CMTG_CTL_B);
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

static void intel_cmtg_disable(struct intel_display *display,
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
		intel_de_rmw(display, TRANS_CMTG_CTL_A, CMTG_ENABLE, 0);
		clk_sel_clr |= CMTG_CLK_SEL_A_MASK;
		clk_sel_set |= CMTG_CLK_SEL_A_DISABLED;
	}

	if (cmtg_config->cmtg_b_enable) {
		drm_dbg_kms(display->drm, "Disabling CMTG B\n");
		intel_de_rmw(display, TRANS_CMTG_CTL_B, CMTG_ENABLE, 0);
		clk_sel_clr |= CMTG_CLK_SEL_B_MASK;
		clk_sel_set |= CMTG_CLK_SEL_B_DISABLED;
	}

	if (intel_cmtg_has_clock_sel(display) && clk_sel_clr)
		intel_de_rmw(display, CMTG_CLK_SEL, clk_sel_clr, clk_sel_set);
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

	intel_cmtg_disable(display, &cmtg_config);
}
