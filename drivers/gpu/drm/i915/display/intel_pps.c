// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/debugfs.h>

#include "g4x_dp.h"
#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_de.h"
#include "intel_display_power_well.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dpio_phy.h"
#include "intel_dpll.h"
#include "intel_lvds.h"
#include "intel_lvds_regs.h"
#include "intel_pps.h"
#include "intel_pps_regs.h"
#include "intel_quirks.h"

static void vlv_steal_power_sequencer(struct intel_display *display,
				      enum pipe pipe);

static void pps_init_delays(struct intel_dp *intel_dp);
static void pps_init_registers(struct intel_dp *intel_dp, bool force_disable_vdd);

static const char *pps_name(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_pps *pps = &intel_dp->pps;

	if (display->platform.valleyview || display->platform.cherryview) {
		switch (pps->vlv_pps_pipe) {
		case INVALID_PIPE:
			/*
			 * FIXME would be nice if we can guarantee
			 * to always have a valid PPS when calling this.
			 */
			return "PPS <none>";
		case PIPE_A:
			return "PPS A";
		case PIPE_B:
			return "PPS B";
		default:
			MISSING_CASE(pps->vlv_pps_pipe);
			break;
		}
	} else {
		switch (pps->pps_idx) {
		case 0:
			return "PPS 0";
		case 1:
			return "PPS 1";
		default:
			MISSING_CASE(pps->pps_idx);
			break;
		}
	}

	return "PPS <invalid>";
}

intel_wakeref_t intel_pps_lock(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	intel_wakeref_t wakeref;

	/*
	 * See vlv_pps_reset_all() why we need a power domain reference here.
	 */
	wakeref = intel_display_power_get(display, POWER_DOMAIN_DISPLAY_CORE);
	mutex_lock(&display->pps.mutex);

	return wakeref;
}

intel_wakeref_t intel_pps_unlock(struct intel_dp *intel_dp,
				 intel_wakeref_t wakeref)
{
	struct intel_display *display = to_intel_display(intel_dp);

	mutex_unlock(&display->pps.mutex);
	intel_display_power_put(display, POWER_DOMAIN_DISPLAY_CORE, wakeref);

	return NULL;
}

static void
vlv_power_sequencer_kick(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum pipe pipe = intel_dp->pps.vlv_pps_pipe;
	bool pll_enabled, release_cl_override = false;
	enum dpio_phy phy = vlv_pipe_to_phy(pipe);
	enum dpio_channel ch = vlv_pipe_to_channel(pipe);
	u32 DP;

	if (drm_WARN(display->drm,
		     intel_de_read(display, intel_dp->output_reg) & DP_PORT_EN,
		     "skipping %s kick due to [ENCODER:%d:%s] being active\n",
		     pps_name(intel_dp),
		     dig_port->base.base.base.id, dig_port->base.base.name))
		return;

	drm_dbg_kms(display->drm,
		    "kicking %s for [ENCODER:%d:%s]\n",
		    pps_name(intel_dp),
		    dig_port->base.base.base.id, dig_port->base.base.name);

	/* Preserve the BIOS-computed detected bit. This is
	 * supposed to be read-only.
	 */
	DP = intel_de_read(display, intel_dp->output_reg) & DP_DETECTED;
	DP |= DP_VOLTAGE_0_4 | DP_PRE_EMPHASIS_0;
	DP |= DP_PORT_WIDTH(1);
	DP |= DP_LINK_TRAIN_PAT_1;

	if (display->platform.cherryview)
		DP |= DP_PIPE_SEL_CHV(pipe);
	else
		DP |= DP_PIPE_SEL(pipe);

	pll_enabled = intel_de_read(display, DPLL(display, pipe)) & DPLL_VCO_ENABLE;

	/*
	 * The DPLL for the pipe must be enabled for this to work.
	 * So enable temporarily it if it's not already enabled.
	 */
	if (!pll_enabled) {
		release_cl_override = display->platform.cherryview &&
			!chv_phy_powergate_ch(display, phy, ch, true);

		if (vlv_force_pll_on(dev_priv, pipe, vlv_get_dpll(display))) {
			drm_err(display->drm,
				"Failed to force on PLL for pipe %c!\n",
				pipe_name(pipe));
			return;
		}
	}

	/*
	 * Similar magic as in intel_dp_enable_port().
	 * We _must_ do this port enable + disable trick
	 * to make this power sequencer lock onto the port.
	 * Otherwise even VDD force bit won't work.
	 */
	intel_de_write(display, intel_dp->output_reg, DP);
	intel_de_posting_read(display, intel_dp->output_reg);

	intel_de_write(display, intel_dp->output_reg, DP | DP_PORT_EN);
	intel_de_posting_read(display, intel_dp->output_reg);

	intel_de_write(display, intel_dp->output_reg, DP & ~DP_PORT_EN);
	intel_de_posting_read(display, intel_dp->output_reg);

	if (!pll_enabled) {
		vlv_force_pll_off(dev_priv, pipe);

		if (release_cl_override)
			chv_phy_powergate_ch(display, phy, ch, false);
	}
}

static enum pipe vlv_find_free_pps(struct intel_display *display)
{
	struct intel_encoder *encoder;
	unsigned int pipes = (1 << PIPE_A) | (1 << PIPE_B);

	/*
	 * We don't have power sequencer currently.
	 * Pick one that's not used by other ports.
	 */
	for_each_intel_dp(display->drm, encoder) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		if (encoder->type == INTEL_OUTPUT_EDP) {
			drm_WARN_ON(display->drm,
				    intel_dp->pps.vlv_active_pipe != INVALID_PIPE &&
				    intel_dp->pps.vlv_active_pipe !=
				    intel_dp->pps.vlv_pps_pipe);

			if (intel_dp->pps.vlv_pps_pipe != INVALID_PIPE)
				pipes &= ~(1 << intel_dp->pps.vlv_pps_pipe);
		} else {
			drm_WARN_ON(display->drm,
				    intel_dp->pps.vlv_pps_pipe != INVALID_PIPE);

			if (intel_dp->pps.vlv_active_pipe != INVALID_PIPE)
				pipes &= ~(1 << intel_dp->pps.vlv_active_pipe);
		}
	}

	if (pipes == 0)
		return INVALID_PIPE;

	return ffs(pipes) - 1;
}

static enum pipe
vlv_power_sequencer_pipe(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum pipe pipe;

	lockdep_assert_held(&display->pps.mutex);

	/* We should never land here with regular DP ports */
	drm_WARN_ON(display->drm, !intel_dp_is_edp(intel_dp));

	drm_WARN_ON(display->drm, intel_dp->pps.vlv_active_pipe != INVALID_PIPE &&
		    intel_dp->pps.vlv_active_pipe != intel_dp->pps.vlv_pps_pipe);

	if (intel_dp->pps.vlv_pps_pipe != INVALID_PIPE)
		return intel_dp->pps.vlv_pps_pipe;

	pipe = vlv_find_free_pps(display);

	/*
	 * Didn't find one. This should not happen since there
	 * are two power sequencers and up to two eDP ports.
	 */
	if (drm_WARN_ON(display->drm, pipe == INVALID_PIPE))
		pipe = PIPE_A;

	vlv_steal_power_sequencer(display, pipe);
	intel_dp->pps.vlv_pps_pipe = pipe;

	drm_dbg_kms(display->drm,
		    "picked %s for [ENCODER:%d:%s]\n",
		    pps_name(intel_dp),
		    dig_port->base.base.base.id, dig_port->base.base.name);

	/* init power sequencer on this pipe and port */
	pps_init_delays(intel_dp);
	pps_init_registers(intel_dp, true);

	/*
	 * Even vdd force doesn't work until we've made
	 * the power sequencer lock in on the port.
	 */
	vlv_power_sequencer_kick(intel_dp);

	return intel_dp->pps.vlv_pps_pipe;
}

static int
bxt_power_sequencer_idx(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	int pps_idx = intel_dp->pps.pps_idx;

	lockdep_assert_held(&display->pps.mutex);

	/* We should never land here with regular DP ports */
	drm_WARN_ON(display->drm, !intel_dp_is_edp(intel_dp));

	if (!intel_dp->pps.bxt_pps_reset)
		return pps_idx;

	intel_dp->pps.bxt_pps_reset = false;

	/*
	 * Only the HW needs to be reprogrammed, the SW state is fixed and
	 * has been setup during connector init.
	 */
	pps_init_registers(intel_dp, false);

	return pps_idx;
}

typedef bool (*pps_check)(struct intel_display *display, int pps_idx);

static bool pps_has_pp_on(struct intel_display *display, int pps_idx)
{
	return intel_de_read(display, PP_STATUS(display, pps_idx)) & PP_ON;
}

static bool pps_has_vdd_on(struct intel_display *display, int pps_idx)
{
	return intel_de_read(display, PP_CONTROL(display, pps_idx)) & EDP_FORCE_VDD;
}

static bool pps_any(struct intel_display *display, int pps_idx)
{
	return true;
}

static enum pipe
vlv_initial_pps_pipe(struct intel_display *display,
		     enum port port, pps_check check)
{
	enum pipe pipe;

	for (pipe = PIPE_A; pipe <= PIPE_B; pipe++) {
		u32 port_sel = intel_de_read(display,
					     PP_ON_DELAYS(display, pipe)) &
			PANEL_PORT_SELECT_MASK;

		if (port_sel != PANEL_PORT_SELECT_VLV(port))
			continue;

		if (!check(display, pipe))
			continue;

		return pipe;
	}

	return INVALID_PIPE;
}

static void
vlv_initial_power_sequencer_setup(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum port port = dig_port->base.port;

	lockdep_assert_held(&display->pps.mutex);

	/* try to find a pipe with this port selected */
	/* first pick one where the panel is on */
	intel_dp->pps.vlv_pps_pipe = vlv_initial_pps_pipe(display, port,
							  pps_has_pp_on);
	/* didn't find one? pick one where vdd is on */
	if (intel_dp->pps.vlv_pps_pipe == INVALID_PIPE)
		intel_dp->pps.vlv_pps_pipe = vlv_initial_pps_pipe(display, port,
								  pps_has_vdd_on);
	/* didn't find one? pick one with just the correct port */
	if (intel_dp->pps.vlv_pps_pipe == INVALID_PIPE)
		intel_dp->pps.vlv_pps_pipe = vlv_initial_pps_pipe(display, port,
								  pps_any);

	/* didn't find one? just let vlv_power_sequencer_pipe() pick one when needed */
	if (intel_dp->pps.vlv_pps_pipe == INVALID_PIPE) {
		drm_dbg_kms(display->drm,
			    "[ENCODER:%d:%s] no initial power sequencer\n",
			    dig_port->base.base.base.id, dig_port->base.base.name);
		return;
	}

	drm_dbg_kms(display->drm,
		    "[ENCODER:%d:%s] initial power sequencer: %s\n",
		    dig_port->base.base.base.id, dig_port->base.base.name,
		    pps_name(intel_dp));
}

static int intel_num_pps(struct intel_display *display)
{
	struct drm_i915_private *i915 = to_i915(display->drm);

	if (display->platform.valleyview || display->platform.cherryview)
		return 2;

	if (display->platform.geminilake || display->platform.broxton)
		return 2;

	if (INTEL_PCH_TYPE(i915) >= PCH_MTL)
		return 2;

	if (INTEL_PCH_TYPE(i915) >= PCH_DG1)
		return 1;

	if (INTEL_PCH_TYPE(i915) >= PCH_ICP)
		return 2;

	return 1;
}

static bool intel_pps_is_valid(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_i915_private *i915 = to_i915(display->drm);

	if (intel_dp->pps.pps_idx == 1 &&
	    INTEL_PCH_TYPE(i915) >= PCH_ICP &&
	    INTEL_PCH_TYPE(i915) <= PCH_ADP)
		return intel_de_read(display, SOUTH_CHICKEN1) & ICP_SECOND_PPS_IO_SELECT;

	return true;
}

static int
bxt_initial_pps_idx(struct intel_display *display, pps_check check)
{
	int pps_idx, pps_num = intel_num_pps(display);

	for (pps_idx = 0; pps_idx < pps_num; pps_idx++) {
		if (check(display, pps_idx))
			return pps_idx;
	}

	return -1;
}

static bool
pps_initial_setup(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct intel_connector *connector = intel_dp->attached_connector;

	lockdep_assert_held(&display->pps.mutex);

	if (display->platform.valleyview || display->platform.cherryview) {
		vlv_initial_power_sequencer_setup(intel_dp);
		return true;
	}

	/* first ask the VBT */
	if (intel_num_pps(display) > 1)
		intel_dp->pps.pps_idx = connector->panel.vbt.backlight.controller;
	else
		intel_dp->pps.pps_idx = 0;

	if (drm_WARN_ON(display->drm, intel_dp->pps.pps_idx >= intel_num_pps(display)))
		intel_dp->pps.pps_idx = -1;

	/* VBT wasn't parsed yet? pick one where the panel is on */
	if (intel_dp->pps.pps_idx < 0)
		intel_dp->pps.pps_idx = bxt_initial_pps_idx(display, pps_has_pp_on);
	/* didn't find one? pick one where vdd is on */
	if (intel_dp->pps.pps_idx < 0)
		intel_dp->pps.pps_idx = bxt_initial_pps_idx(display, pps_has_vdd_on);
	/* didn't find one? pick any */
	if (intel_dp->pps.pps_idx < 0) {
		intel_dp->pps.pps_idx = bxt_initial_pps_idx(display, pps_any);

		drm_dbg_kms(display->drm,
			    "[ENCODER:%d:%s] no initial power sequencer, assuming %s\n",
			    encoder->base.base.id, encoder->base.name,
			    pps_name(intel_dp));
	} else {
		drm_dbg_kms(display->drm,
			    "[ENCODER:%d:%s] initial power sequencer: %s\n",
			    encoder->base.base.id, encoder->base.name,
			    pps_name(intel_dp));
	}

	return intel_pps_is_valid(intel_dp);
}

void vlv_pps_reset_all(struct intel_display *display)
{
	struct intel_encoder *encoder;

	if (!HAS_DISPLAY(display))
		return;

	/*
	 * We can't grab pps_mutex here due to deadlock with power_domain
	 * mutex when power_domain functions are called while holding pps_mutex.
	 * That also means that in order to use vlv_pps_pipe the code needs to
	 * hold both a power domain reference and pps_mutex, and the power domain
	 * reference get/put must be done while _not_ holding pps_mutex.
	 * pps_{lock,unlock}() do these steps in the correct order, so one
	 * should use them always.
	 */

	for_each_intel_dp(display->drm, encoder) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		drm_WARN_ON(display->drm, intel_dp->pps.vlv_active_pipe != INVALID_PIPE);

		if (encoder->type == INTEL_OUTPUT_EDP)
			intel_dp->pps.vlv_pps_pipe = INVALID_PIPE;
	}
}

void bxt_pps_reset_all(struct intel_display *display)
{
	struct intel_encoder *encoder;

	if (!HAS_DISPLAY(display))
		return;

	/* See vlv_pps_reset_all() for why we can't grab pps_mutex here. */

	for_each_intel_dp(display->drm, encoder) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		if (encoder->type == INTEL_OUTPUT_EDP)
			intel_dp->pps.bxt_pps_reset = true;
	}
}

struct pps_registers {
	i915_reg_t pp_ctrl;
	i915_reg_t pp_stat;
	i915_reg_t pp_on;
	i915_reg_t pp_off;
	i915_reg_t pp_div;
};

static void intel_pps_get_registers(struct intel_dp *intel_dp,
				    struct pps_registers *regs)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	int pps_idx;

	memset(regs, 0, sizeof(*regs));

	if (display->platform.valleyview || display->platform.cherryview)
		pps_idx = vlv_power_sequencer_pipe(intel_dp);
	else if (display->platform.geminilake || display->platform.broxton)
		pps_idx = bxt_power_sequencer_idx(intel_dp);
	else
		pps_idx = intel_dp->pps.pps_idx;

	regs->pp_ctrl = PP_CONTROL(display, pps_idx);
	regs->pp_stat = PP_STATUS(display, pps_idx);
	regs->pp_on = PP_ON_DELAYS(display, pps_idx);
	regs->pp_off = PP_OFF_DELAYS(display, pps_idx);

	/* Cycle delay moved from PP_DIVISOR to PP_CONTROL */
	if (display->platform.geminilake || display->platform.broxton ||
	    INTEL_PCH_TYPE(dev_priv) >= PCH_CNP)
		regs->pp_div = INVALID_MMIO_REG;
	else
		regs->pp_div = PP_DIVISOR(display, pps_idx);
}

static i915_reg_t
_pp_ctrl_reg(struct intel_dp *intel_dp)
{
	struct pps_registers regs;

	intel_pps_get_registers(intel_dp, &regs);

	return regs.pp_ctrl;
}

static i915_reg_t
_pp_stat_reg(struct intel_dp *intel_dp)
{
	struct pps_registers regs;

	intel_pps_get_registers(intel_dp, &regs);

	return regs.pp_stat;
}

static bool edp_have_panel_power(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);

	lockdep_assert_held(&display->pps.mutex);

	if ((display->platform.valleyview || display->platform.cherryview) &&
	    intel_dp->pps.vlv_pps_pipe == INVALID_PIPE)
		return false;

	return (intel_de_read(display, _pp_stat_reg(intel_dp)) & PP_ON) != 0;
}

static bool edp_have_panel_vdd(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);

	lockdep_assert_held(&display->pps.mutex);

	if ((display->platform.valleyview || display->platform.cherryview) &&
	    intel_dp->pps.vlv_pps_pipe == INVALID_PIPE)
		return false;

	return intel_de_read(display, _pp_ctrl_reg(intel_dp)) & EDP_FORCE_VDD;
}

void intel_pps_check_power_unlocked(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);

	if (!intel_dp_is_edp(intel_dp))
		return;

	if (!edp_have_panel_power(intel_dp) && !edp_have_panel_vdd(intel_dp)) {
		drm_WARN(display->drm, 1,
			 "[ENCODER:%d:%s] %s powered off while attempting AUX CH communication.\n",
			 dig_port->base.base.base.id, dig_port->base.base.name,
			 pps_name(intel_dp));
		drm_dbg_kms(display->drm,
			    "[ENCODER:%d:%s] %s PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
			    dig_port->base.base.base.id, dig_port->base.base.name,
			    pps_name(intel_dp),
			    intel_de_read(display, _pp_stat_reg(intel_dp)),
			    intel_de_read(display, _pp_ctrl_reg(intel_dp)));
	}
}

#define IDLE_ON_MASK		(PP_ON | PP_SEQUENCE_MASK | 0                     | PP_SEQUENCE_STATE_MASK)
#define IDLE_ON_VALUE		(PP_ON | PP_SEQUENCE_NONE | 0                     | PP_SEQUENCE_STATE_ON_IDLE)

#define IDLE_OFF_MASK		(PP_ON | PP_SEQUENCE_MASK | 0                     | 0)
#define IDLE_OFF_VALUE		(0     | PP_SEQUENCE_NONE | 0                     | 0)

#define IDLE_CYCLE_MASK		(PP_ON | PP_SEQUENCE_MASK | PP_CYCLE_DELAY_ACTIVE | PP_SEQUENCE_STATE_MASK)
#define IDLE_CYCLE_VALUE	(0     | PP_SEQUENCE_NONE | 0                     | PP_SEQUENCE_STATE_OFF_IDLE)

static void intel_pps_verify_state(struct intel_dp *intel_dp);

static void wait_panel_status(struct intel_dp *intel_dp,
			      u32 mask, u32 value)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	i915_reg_t pp_stat_reg, pp_ctrl_reg;

	lockdep_assert_held(&display->pps.mutex);

	intel_pps_verify_state(intel_dp);

	pp_stat_reg = _pp_stat_reg(intel_dp);
	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	drm_dbg_kms(display->drm,
		    "[ENCODER:%d:%s] %s mask: 0x%08x value: 0x%08x PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
		    dig_port->base.base.base.id, dig_port->base.base.name,
		    pps_name(intel_dp),
		    mask, value,
		    intel_de_read(display, pp_stat_reg),
		    intel_de_read(display, pp_ctrl_reg));

	if (intel_de_wait(display, pp_stat_reg, mask, value, 5000))
		drm_err(display->drm,
			"[ENCODER:%d:%s] %s panel status timeout: PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
			dig_port->base.base.base.id, dig_port->base.base.name,
			pps_name(intel_dp),
			intel_de_read(display, pp_stat_reg),
			intel_de_read(display, pp_ctrl_reg));

	drm_dbg_kms(display->drm, "Wait complete\n");
}

static void wait_panel_on(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);

	drm_dbg_kms(display->drm,
		    "[ENCODER:%d:%s] %s wait for panel power on\n",
		    dig_port->base.base.base.id, dig_port->base.base.name,
		    pps_name(intel_dp));
	wait_panel_status(intel_dp, IDLE_ON_MASK, IDLE_ON_VALUE);
}

static void wait_panel_off(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);

	drm_dbg_kms(display->drm,
		    "[ENCODER:%d:%s] %s wait for panel power off time\n",
		    dig_port->base.base.base.id, dig_port->base.base.name,
		    pps_name(intel_dp));
	wait_panel_status(intel_dp, IDLE_OFF_MASK, IDLE_OFF_VALUE);
}

static void wait_panel_power_cycle(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	ktime_t panel_power_on_time;
	s64 panel_power_off_duration, remaining;

	/* take the difference of current time and panel power off time
	 * and then make panel wait for power_cycle if needed. */
	panel_power_on_time = ktime_get_boottime();
	panel_power_off_duration = ktime_ms_delta(panel_power_on_time, intel_dp->pps.panel_power_off_time);

	remaining = max(0, intel_dp->pps.panel_power_cycle_delay - panel_power_off_duration);

	drm_dbg_kms(display->drm,
		    "[ENCODER:%d:%s] %s wait for panel power cycle (%lld ms remaining)\n",
		    dig_port->base.base.base.id, dig_port->base.base.name,
		    pps_name(intel_dp), remaining);

	/* When we disable the VDD override bit last we have to do the manual
	 * wait. */
	if (remaining)
		wait_remaining_ms_from_jiffies(jiffies, remaining);

	wait_panel_status(intel_dp, IDLE_CYCLE_MASK, IDLE_CYCLE_VALUE);
}

void intel_pps_wait_power_cycle(struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;

	if (!intel_dp_is_edp(intel_dp))
		return;

	with_intel_pps_lock(intel_dp, wakeref)
		wait_panel_power_cycle(intel_dp);
}

static void wait_backlight_on(struct intel_dp *intel_dp)
{
	wait_remaining_ms_from_jiffies(intel_dp->pps.last_power_on,
				       intel_dp->pps.backlight_on_delay);
}

static void edp_wait_backlight_off(struct intel_dp *intel_dp)
{
	wait_remaining_ms_from_jiffies(intel_dp->pps.last_backlight_off,
				       intel_dp->pps.backlight_off_delay);
}

/* Read the current pp_control value, unlocking the register if it
 * is locked
 */

static  u32 ilk_get_pp_control(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	u32 control;

	lockdep_assert_held(&display->pps.mutex);

	control = intel_de_read(display, _pp_ctrl_reg(intel_dp));
	if (drm_WARN_ON(display->drm, !HAS_DDI(display) &&
			(control & PANEL_UNLOCK_MASK) != PANEL_UNLOCK_REGS)) {
		control &= ~PANEL_UNLOCK_MASK;
		control |= PANEL_UNLOCK_REGS;
	}
	return control;
}

/*
 * Must be paired with intel_pps_vdd_off_unlocked().
 * Must hold pps_mutex around the whole on/off sequence.
 * Can be nested with intel_pps_vdd_{on,off}() calls.
 */
bool intel_pps_vdd_on_unlocked(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	u32 pp;
	i915_reg_t pp_stat_reg, pp_ctrl_reg;
	bool need_to_disable = !intel_dp->pps.want_panel_vdd;

	lockdep_assert_held(&display->pps.mutex);

	if (!intel_dp_is_edp(intel_dp))
		return false;

	cancel_delayed_work(&intel_dp->pps.panel_vdd_work);
	intel_dp->pps.want_panel_vdd = true;

	if (edp_have_panel_vdd(intel_dp))
		return need_to_disable;

	drm_WARN_ON(display->drm, intel_dp->pps.vdd_wakeref);
	intel_dp->pps.vdd_wakeref = intel_display_power_get(display,
							    intel_aux_power_domain(dig_port));

	pp_stat_reg = _pp_stat_reg(intel_dp);
	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	drm_dbg_kms(display->drm, "[ENCODER:%d:%s] %s turning VDD on\n",
		    dig_port->base.base.base.id, dig_port->base.base.name,
		    pps_name(intel_dp));

	if (!edp_have_panel_power(intel_dp))
		wait_panel_power_cycle(intel_dp);

	pp = ilk_get_pp_control(intel_dp);
	pp |= EDP_FORCE_VDD;

	intel_de_write(display, pp_ctrl_reg, pp);
	intel_de_posting_read(display, pp_ctrl_reg);
	drm_dbg_kms(display->drm,
		    "[ENCODER:%d:%s] %s PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
		    dig_port->base.base.base.id, dig_port->base.base.name,
		    pps_name(intel_dp),
		    intel_de_read(display, pp_stat_reg),
		    intel_de_read(display, pp_ctrl_reg));
	/*
	 * If the panel wasn't on, delay before accessing aux channel
	 */
	if (!edp_have_panel_power(intel_dp)) {
		drm_dbg_kms(display->drm,
			    "[ENCODER:%d:%s] %s panel power wasn't enabled\n",
			    dig_port->base.base.base.id, dig_port->base.base.name,
			    pps_name(intel_dp));
		msleep(intel_dp->pps.panel_power_up_delay);
	}

	return need_to_disable;
}

/*
 * Must be paired with intel_pps_vdd_off() or - to disable
 * both VDD and panel power - intel_pps_off().
 * Nested calls to these functions are not allowed since
 * we drop the lock. Caller must use some higher level
 * locking to prevent nested calls from other threads.
 */
void intel_pps_vdd_on(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	intel_wakeref_t wakeref;
	bool vdd;

	if (!intel_dp_is_edp(intel_dp))
		return;

	vdd = false;
	with_intel_pps_lock(intel_dp, wakeref)
		vdd = intel_pps_vdd_on_unlocked(intel_dp);
	INTEL_DISPLAY_STATE_WARN(display, !vdd, "[ENCODER:%d:%s] %s VDD already requested on\n",
				 dp_to_dig_port(intel_dp)->base.base.base.id,
				 dp_to_dig_port(intel_dp)->base.base.name,
				 pps_name(intel_dp));
}

static void intel_pps_vdd_off_sync_unlocked(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	u32 pp;
	i915_reg_t pp_stat_reg, pp_ctrl_reg;

	lockdep_assert_held(&display->pps.mutex);

	drm_WARN_ON(display->drm, intel_dp->pps.want_panel_vdd);

	if (!edp_have_panel_vdd(intel_dp))
		return;

	drm_dbg_kms(display->drm, "[ENCODER:%d:%s] %s turning VDD off\n",
		    dig_port->base.base.base.id, dig_port->base.base.name,
		    pps_name(intel_dp));

	pp = ilk_get_pp_control(intel_dp);
	pp &= ~EDP_FORCE_VDD;

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
	pp_stat_reg = _pp_stat_reg(intel_dp);

	intel_de_write(display, pp_ctrl_reg, pp);
	intel_de_posting_read(display, pp_ctrl_reg);

	/* Make sure sequencer is idle before allowing subsequent activity */
	drm_dbg_kms(display->drm,
		    "[ENCODER:%d:%s] %s PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
		    dig_port->base.base.base.id, dig_port->base.base.name,
		    pps_name(intel_dp),
		    intel_de_read(display, pp_stat_reg),
		    intel_de_read(display, pp_ctrl_reg));

	if ((pp & PANEL_POWER_ON) == 0) {
		intel_dp->pps.panel_power_off_time = ktime_get_boottime();
		intel_dp_invalidate_source_oui(intel_dp);
	}

	intel_display_power_put(display,
				intel_aux_power_domain(dig_port),
				fetch_and_zero(&intel_dp->pps.vdd_wakeref));
}

void intel_pps_vdd_off_sync(struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;

	if (!intel_dp_is_edp(intel_dp))
		return;

	cancel_delayed_work_sync(&intel_dp->pps.panel_vdd_work);
	/*
	 * vdd might still be enabled due to the delayed vdd off.
	 * Make sure vdd is actually turned off here.
	 */
	with_intel_pps_lock(intel_dp, wakeref)
		intel_pps_vdd_off_sync_unlocked(intel_dp);
}

static void edp_panel_vdd_work(struct work_struct *__work)
{
	struct intel_pps *pps = container_of(to_delayed_work(__work),
					     struct intel_pps, panel_vdd_work);
	struct intel_dp *intel_dp = container_of(pps, struct intel_dp, pps);
	intel_wakeref_t wakeref;

	with_intel_pps_lock(intel_dp, wakeref) {
		if (!intel_dp->pps.want_panel_vdd)
			intel_pps_vdd_off_sync_unlocked(intel_dp);
	}
}

static void edp_panel_vdd_schedule_off(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_i915_private *i915 = to_i915(display->drm);
	unsigned long delay;

	/*
	 * We may not yet know the real power sequencing delays,
	 * so keep VDD enabled until we're done with init.
	 */
	if (intel_dp->pps.initializing)
		return;

	/*
	 * Queue the timer to fire a long time from now (relative to the power
	 * down delay) to keep the panel power up across a sequence of
	 * operations.
	 */
	delay = msecs_to_jiffies(intel_dp->pps.panel_power_cycle_delay * 5);
	queue_delayed_work(i915->unordered_wq,
			   &intel_dp->pps.panel_vdd_work, delay);
}

/*
 * Must be paired with edp_panel_vdd_on().
 * Must hold pps_mutex around the whole on/off sequence.
 * Can be nested with intel_pps_vdd_{on,off}() calls.
 */
void intel_pps_vdd_off_unlocked(struct intel_dp *intel_dp, bool sync)
{
	struct intel_display *display = to_intel_display(intel_dp);

	lockdep_assert_held(&display->pps.mutex);

	if (!intel_dp_is_edp(intel_dp))
		return;

	INTEL_DISPLAY_STATE_WARN(display, !intel_dp->pps.want_panel_vdd,
				 "[ENCODER:%d:%s] %s VDD not forced on",
				 dp_to_dig_port(intel_dp)->base.base.base.id,
				 dp_to_dig_port(intel_dp)->base.base.name,
				 pps_name(intel_dp));

	intel_dp->pps.want_panel_vdd = false;

	if (sync)
		intel_pps_vdd_off_sync_unlocked(intel_dp);
	else
		edp_panel_vdd_schedule_off(intel_dp);
}

void intel_pps_vdd_off(struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;

	if (!intel_dp_is_edp(intel_dp))
		return;

	with_intel_pps_lock(intel_dp, wakeref)
		intel_pps_vdd_off_unlocked(intel_dp, false);
}

void intel_pps_on_unlocked(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	u32 pp;
	i915_reg_t pp_ctrl_reg;

	lockdep_assert_held(&display->pps.mutex);

	if (!intel_dp_is_edp(intel_dp))
		return;

	drm_dbg_kms(display->drm, "[ENCODER:%d:%s] %s turn panel power on\n",
		    dp_to_dig_port(intel_dp)->base.base.base.id,
		    dp_to_dig_port(intel_dp)->base.base.name,
		    pps_name(intel_dp));

	if (drm_WARN(display->drm, edp_have_panel_power(intel_dp),
		     "[ENCODER:%d:%s] %s panel power already on\n",
		     dp_to_dig_port(intel_dp)->base.base.base.id,
		     dp_to_dig_port(intel_dp)->base.base.name,
		     pps_name(intel_dp)))
		return;

	wait_panel_power_cycle(intel_dp);

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
	pp = ilk_get_pp_control(intel_dp);
	if (display->platform.ironlake) {
		/* ILK workaround: disable reset around power sequence */
		pp &= ~PANEL_POWER_RESET;
		intel_de_write(display, pp_ctrl_reg, pp);
		intel_de_posting_read(display, pp_ctrl_reg);
	}

	/*
	 * WA: 22019252566
	 * Disable DPLS gating around power sequence.
	 */
	if (IS_DISPLAY_VER(display, 13, 14))
		intel_de_rmw(display, SOUTH_DSPCLK_GATE_D,
			     0, PCH_DPLSUNIT_CLOCK_GATE_DISABLE);

	pp |= PANEL_POWER_ON;
	if (!display->platform.ironlake)
		pp |= PANEL_POWER_RESET;

	intel_de_write(display, pp_ctrl_reg, pp);
	intel_de_posting_read(display, pp_ctrl_reg);

	wait_panel_on(intel_dp);
	intel_dp->pps.last_power_on = jiffies;

	if (IS_DISPLAY_VER(display, 13, 14))
		intel_de_rmw(display, SOUTH_DSPCLK_GATE_D,
			     PCH_DPLSUNIT_CLOCK_GATE_DISABLE, 0);

	if (display->platform.ironlake) {
		pp |= PANEL_POWER_RESET; /* restore panel reset bit */
		intel_de_write(display, pp_ctrl_reg, pp);
		intel_de_posting_read(display, pp_ctrl_reg);
	}
}

void intel_pps_on(struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;

	if (!intel_dp_is_edp(intel_dp))
		return;

	with_intel_pps_lock(intel_dp, wakeref)
		intel_pps_on_unlocked(intel_dp);
}

void intel_pps_off_unlocked(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	u32 pp;
	i915_reg_t pp_ctrl_reg;

	lockdep_assert_held(&display->pps.mutex);

	if (!intel_dp_is_edp(intel_dp))
		return;

	drm_dbg_kms(display->drm, "[ENCODER:%d:%s] %s turn panel power off\n",
		    dig_port->base.base.base.id, dig_port->base.base.name,
		    pps_name(intel_dp));

	drm_WARN(display->drm, !intel_dp->pps.want_panel_vdd,
		 "[ENCODER:%d:%s] %s need VDD to turn off panel\n",
		 dig_port->base.base.base.id, dig_port->base.base.name,
		 pps_name(intel_dp));

	pp = ilk_get_pp_control(intel_dp);
	/* We need to switch off panel power _and_ force vdd, for otherwise some
	 * panels get very unhappy and cease to work. */
	pp &= ~(PANEL_POWER_ON | PANEL_POWER_RESET | EDP_FORCE_VDD |
		EDP_BLC_ENABLE);

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	intel_dp->pps.want_panel_vdd = false;

	intel_de_write(display, pp_ctrl_reg, pp);
	intel_de_posting_read(display, pp_ctrl_reg);

	wait_panel_off(intel_dp);
	intel_dp->pps.panel_power_off_time = ktime_get_boottime();

	intel_dp_invalidate_source_oui(intel_dp);

	/* We got a reference when we enabled the VDD. */
	intel_display_power_put(display,
				intel_aux_power_domain(dig_port),
				fetch_and_zero(&intel_dp->pps.vdd_wakeref));
}

void intel_pps_off(struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;

	if (!intel_dp_is_edp(intel_dp))
		return;

	with_intel_pps_lock(intel_dp, wakeref)
		intel_pps_off_unlocked(intel_dp);
}

/* Enable backlight in the panel power control. */
void intel_pps_backlight_on(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	intel_wakeref_t wakeref;

	/*
	 * If we enable the backlight right away following a panel power
	 * on, we may see slight flicker as the panel syncs with the eDP
	 * link.  So delay a bit to make sure the image is solid before
	 * allowing it to appear.
	 */
	wait_backlight_on(intel_dp);

	with_intel_pps_lock(intel_dp, wakeref) {
		i915_reg_t pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
		u32 pp;

		pp = ilk_get_pp_control(intel_dp);
		pp |= EDP_BLC_ENABLE;

		intel_de_write(display, pp_ctrl_reg, pp);
		intel_de_posting_read(display, pp_ctrl_reg);
	}
}

/* Disable backlight in the panel power control. */
void intel_pps_backlight_off(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	intel_wakeref_t wakeref;

	if (!intel_dp_is_edp(intel_dp))
		return;

	with_intel_pps_lock(intel_dp, wakeref) {
		i915_reg_t pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
		u32 pp;

		pp = ilk_get_pp_control(intel_dp);
		pp &= ~EDP_BLC_ENABLE;

		intel_de_write(display, pp_ctrl_reg, pp);
		intel_de_posting_read(display, pp_ctrl_reg);
	}

	intel_dp->pps.last_backlight_off = jiffies;
	edp_wait_backlight_off(intel_dp);
}

/*
 * Hook for controlling the panel power control backlight through the bl_power
 * sysfs attribute. Take care to handle multiple calls.
 */
void intel_pps_backlight_power(struct intel_connector *connector, bool enable)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	intel_wakeref_t wakeref;
	bool is_enabled;

	is_enabled = false;
	with_intel_pps_lock(intel_dp, wakeref)
		is_enabled = ilk_get_pp_control(intel_dp) & EDP_BLC_ENABLE;
	if (is_enabled == enable)
		return;

	drm_dbg_kms(display->drm, "panel power control backlight %s\n",
		    str_enable_disable(enable));

	if (enable)
		intel_pps_backlight_on(intel_dp);
	else
		intel_pps_backlight_off(intel_dp);
}

static void vlv_detach_power_sequencer(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum pipe pipe = intel_dp->pps.vlv_pps_pipe;
	i915_reg_t pp_on_reg = PP_ON_DELAYS(display, pipe);

	drm_WARN_ON(display->drm, intel_dp->pps.vlv_active_pipe != INVALID_PIPE);

	if (drm_WARN_ON(display->drm, pipe != PIPE_A && pipe != PIPE_B))
		return;

	intel_pps_vdd_off_sync_unlocked(intel_dp);

	/*
	 * VLV seems to get confused when multiple power sequencers
	 * have the same port selected (even if only one has power/vdd
	 * enabled). The failure manifests as vlv_wait_port_ready() failing
	 * CHV on the other hand doesn't seem to mind having the same port
	 * selected in multiple power sequencers, but let's clear the
	 * port select always when logically disconnecting a power sequencer
	 * from a port.
	 */
	drm_dbg_kms(display->drm,
		    "detaching %s from [ENCODER:%d:%s]\n",
		    pps_name(intel_dp),
		    dig_port->base.base.base.id, dig_port->base.base.name);
	intel_de_write(display, pp_on_reg, 0);
	intel_de_posting_read(display, pp_on_reg);

	intel_dp->pps.vlv_pps_pipe = INVALID_PIPE;
}

static void vlv_steal_power_sequencer(struct intel_display *display,
				      enum pipe pipe)
{
	struct intel_encoder *encoder;

	lockdep_assert_held(&display->pps.mutex);

	for_each_intel_dp(display->drm, encoder) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		drm_WARN(display->drm, intel_dp->pps.vlv_active_pipe == pipe,
			 "stealing PPS %c from active [ENCODER:%d:%s]\n",
			 pipe_name(pipe), encoder->base.base.id,
			 encoder->base.name);

		if (intel_dp->pps.vlv_pps_pipe != pipe)
			continue;

		drm_dbg_kms(display->drm,
			    "stealing PPS %c from [ENCODER:%d:%s]\n",
			    pipe_name(pipe), encoder->base.base.id,
			    encoder->base.name);

		/* make sure vdd is off before we steal it */
		vlv_detach_power_sequencer(intel_dp);
	}
}

static enum pipe vlv_active_pipe(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	enum pipe pipe;

	if (g4x_dp_port_enabled(display, intel_dp->output_reg,
				encoder->port, &pipe))
		return pipe;

	return INVALID_PIPE;
}

/* Call on all DP, not just eDP */
void vlv_pps_pipe_init(struct intel_dp *intel_dp)
{
	intel_dp->pps.vlv_pps_pipe = INVALID_PIPE;
	intel_dp->pps.vlv_active_pipe = vlv_active_pipe(intel_dp);
}

/* Call on all DP, not just eDP */
void vlv_pps_pipe_reset(struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;

	with_intel_pps_lock(intel_dp, wakeref)
		intel_dp->pps.vlv_active_pipe = vlv_active_pipe(intel_dp);
}

enum pipe vlv_pps_backlight_initial_pipe(struct intel_dp *intel_dp)
{
	enum pipe pipe;

	/*
	 * Figure out the current pipe for the initial backlight setup. If the
	 * current pipe isn't valid, try the PPS pipe, and if that fails just
	 * assume pipe A.
	 */
	pipe = vlv_active_pipe(intel_dp);

	if (pipe != PIPE_A && pipe != PIPE_B)
		pipe = intel_dp->pps.vlv_pps_pipe;

	if (pipe != PIPE_A && pipe != PIPE_B)
		pipe = PIPE_A;

	return pipe;
}

/* Call on all DP, not just eDP */
void vlv_pps_port_enable_unlocked(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	lockdep_assert_held(&display->pps.mutex);

	drm_WARN_ON(display->drm, intel_dp->pps.vlv_active_pipe != INVALID_PIPE);

	if (intel_dp->pps.vlv_pps_pipe != INVALID_PIPE &&
	    intel_dp->pps.vlv_pps_pipe != crtc->pipe) {
		/*
		 * If another power sequencer was being used on this
		 * port previously make sure to turn off vdd there while
		 * we still have control of it.
		 */
		vlv_detach_power_sequencer(intel_dp);
	}

	/*
	 * We may be stealing the power
	 * sequencer from another port.
	 */
	vlv_steal_power_sequencer(display, crtc->pipe);

	intel_dp->pps.vlv_active_pipe = crtc->pipe;

	if (!intel_dp_is_edp(intel_dp))
		return;

	/* now it's all ours */
	intel_dp->pps.vlv_pps_pipe = crtc->pipe;

	drm_dbg_kms(display->drm,
		    "initializing %s for [ENCODER:%d:%s]\n",
		    pps_name(intel_dp),
		    encoder->base.base.id, encoder->base.name);

	/* init power sequencer on this pipe and port */
	pps_init_delays(intel_dp);
	pps_init_registers(intel_dp, true);
}

/* Call on all DP, not just eDP */
void vlv_pps_port_disable(struct intel_encoder *encoder,
			  const struct intel_crtc_state *crtc_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	intel_wakeref_t wakeref;

	with_intel_pps_lock(intel_dp, wakeref)
		intel_dp->pps.vlv_active_pipe = INVALID_PIPE;
}

static void pps_vdd_init(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);

	lockdep_assert_held(&display->pps.mutex);

	if (!edp_have_panel_vdd(intel_dp))
		return;

	/*
	 * The VDD bit needs a power domain reference, so if the bit is
	 * already enabled when we boot or resume, grab this reference and
	 * schedule a vdd off, so we don't hold on to the reference
	 * indefinitely.
	 */
	drm_dbg_kms(display->drm,
		    "[ENCODER:%d:%s] %s VDD left on by BIOS, adjusting state tracking\n",
		    dig_port->base.base.base.id, dig_port->base.base.name,
		    pps_name(intel_dp));
	drm_WARN_ON(display->drm, intel_dp->pps.vdd_wakeref);
	intel_dp->pps.vdd_wakeref = intel_display_power_get(display,
							    intel_aux_power_domain(dig_port));
}

bool intel_pps_have_panel_power_or_vdd(struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;
	bool have_power = false;

	with_intel_pps_lock(intel_dp, wakeref) {
		have_power = edp_have_panel_power(intel_dp) ||
			     edp_have_panel_vdd(intel_dp);
	}

	return have_power;
}

static void pps_init_timestamps(struct intel_dp *intel_dp)
{
	/*
	 * Initialize panel power off time to 0, assuming panel power could have
	 * been toggled between kernel boot and now only by a previously loaded
	 * and removed i915, which has already ensured sufficient power off
	 * delay at module remove.
	 */
	intel_dp->pps.panel_power_off_time = 0;
	intel_dp->pps.last_power_on = jiffies;
	intel_dp->pps.last_backlight_off = jiffies;
}

static void
intel_pps_readout_hw_state(struct intel_dp *intel_dp, struct intel_pps_delays *seq)
{
	struct intel_display *display = to_intel_display(intel_dp);
	u32 pp_on, pp_off, pp_ctl, power_cycle_delay;
	struct pps_registers regs;

	intel_pps_get_registers(intel_dp, &regs);

	pp_ctl = ilk_get_pp_control(intel_dp);

	/* Ensure PPS is unlocked */
	if (!HAS_DDI(display))
		intel_de_write(display, regs.pp_ctrl, pp_ctl);

	pp_on = intel_de_read(display, regs.pp_on);
	pp_off = intel_de_read(display, regs.pp_off);

	/* Pull timing values out of registers */
	seq->power_up = REG_FIELD_GET(PANEL_POWER_UP_DELAY_MASK, pp_on);
	seq->backlight_on = REG_FIELD_GET(PANEL_LIGHT_ON_DELAY_MASK, pp_on);
	seq->backlight_off = REG_FIELD_GET(PANEL_LIGHT_OFF_DELAY_MASK, pp_off);
	seq->power_down = REG_FIELD_GET(PANEL_POWER_DOWN_DELAY_MASK, pp_off);

	if (i915_mmio_reg_valid(regs.pp_div)) {
		u32 pp_div;

		pp_div = intel_de_read(display, regs.pp_div);

		power_cycle_delay = REG_FIELD_GET(PANEL_POWER_CYCLE_DELAY_MASK, pp_div);
	} else {
		power_cycle_delay = REG_FIELD_GET(BXT_POWER_CYCLE_DELAY_MASK, pp_ctl);
	}

	/* hardware wants <delay>+1 in 100ms units */
	seq->power_cycle = power_cycle_delay ? (power_cycle_delay - 1) * 1000 : 0;
}

static void
intel_pps_dump_state(struct intel_dp *intel_dp, const char *state_name,
		     const struct intel_pps_delays *seq)
{
	struct intel_display *display = to_intel_display(intel_dp);

	drm_dbg_kms(display->drm,
		    "%s power_up %d backlight_on %d backlight_off %d power_down %d power_cycle %d\n",
		    state_name, seq->power_up, seq->backlight_on,
		    seq->backlight_off, seq->power_down, seq->power_cycle);
}

static void
intel_pps_verify_state(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_pps_delays hw;
	struct intel_pps_delays *sw = &intel_dp->pps.pps_delays;

	intel_pps_readout_hw_state(intel_dp, &hw);

	if (hw.power_up != sw->power_up ||
	    hw.backlight_on != sw->backlight_on ||
	    hw.backlight_off != sw->backlight_off ||
	    hw.power_down != sw->power_down ||
	    hw.power_cycle != sw->power_cycle) {
		drm_err(display->drm, "PPS state mismatch\n");
		intel_pps_dump_state(intel_dp, "sw", sw);
		intel_pps_dump_state(intel_dp, "hw", &hw);
	}
}

static bool pps_delays_valid(struct intel_pps_delays *delays)
{
	return delays->power_up || delays->backlight_on || delays->backlight_off ||
		delays->power_down || delays->power_cycle;
}

static int msecs_to_pps_units(int msecs)
{
	/* PPS uses 100us units */
	return msecs * 10;
}

static int pps_units_to_msecs(int val)
{
	/* PPS uses 100us units */
	return DIV_ROUND_UP(val, 10);
}

static void pps_init_delays_bios(struct intel_dp *intel_dp,
				 struct intel_pps_delays *bios)
{
	struct intel_display *display = to_intel_display(intel_dp);

	lockdep_assert_held(&display->pps.mutex);

	if (!pps_delays_valid(&intel_dp->pps.bios_pps_delays))
		intel_pps_readout_hw_state(intel_dp, &intel_dp->pps.bios_pps_delays);

	*bios = intel_dp->pps.bios_pps_delays;

	intel_pps_dump_state(intel_dp, "bios", bios);
}

static void pps_init_delays_vbt(struct intel_dp *intel_dp,
				struct intel_pps_delays *vbt)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_connector *connector = intel_dp->attached_connector;

	*vbt = connector->panel.vbt.edp.pps;

	if (!pps_delays_valid(vbt))
		return;

	/*
	 * On Toshiba Satellite P50-C-18C system the VBT T12 delay
	 * of 500ms appears to be too short. Occasionally the panel
	 * just fails to power back on. Increasing the delay to 800ms
	 * seems sufficient to avoid this problem.
	 */
	if (intel_has_quirk(display, QUIRK_INCREASE_T12_DELAY)) {
		vbt->power_cycle = max_t(u16, vbt->power_cycle, msecs_to_pps_units(1300));
		drm_dbg_kms(display->drm,
			    "Increasing T12 panel delay as per the quirk to %d\n",
			    vbt->power_cycle);
	}

	intel_pps_dump_state(intel_dp, "vbt", vbt);
}

static void pps_init_delays_spec(struct intel_dp *intel_dp,
				 struct intel_pps_delays *spec)
{
	struct intel_display *display = to_intel_display(intel_dp);

	lockdep_assert_held(&display->pps.mutex);

	/* Upper limits from eDP 1.3 spec */
	spec->power_up = msecs_to_pps_units(10 + 200); /* T1+T3 */
	spec->backlight_on = msecs_to_pps_units(50); /* no limit for T8, use T7 instead */
	spec->backlight_off = msecs_to_pps_units(50); /* no limit for T9, make it symmetric with T8 */
	spec->power_down = msecs_to_pps_units(500); /* T10 */
	spec->power_cycle = msecs_to_pps_units(10 + 500); /* T11+T12 */

	intel_pps_dump_state(intel_dp, "spec", spec);
}

static void pps_init_delays(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_pps_delays cur, vbt, spec,
		*final = &intel_dp->pps.pps_delays;

	lockdep_assert_held(&display->pps.mutex);

	/* already initialized? */
	if (pps_delays_valid(final))
		return;

	pps_init_delays_bios(intel_dp, &cur);
	pps_init_delays_vbt(intel_dp, &vbt);
	pps_init_delays_spec(intel_dp, &spec);

	/* Use the max of the register settings and vbt. If both are
	 * unset, fall back to the spec limits. */
#define assign_final(field)	final->field = (max(cur.field, vbt.field) == 0 ? \
				       spec.field : \
				       max(cur.field, vbt.field))
	assign_final(power_up);
	assign_final(backlight_on);
	assign_final(backlight_off);
	assign_final(power_down);
	assign_final(power_cycle);
#undef assign_final

	intel_dp->pps.panel_power_up_delay = pps_units_to_msecs(final->power_up);
	intel_dp->pps.backlight_on_delay = pps_units_to_msecs(final->backlight_on);
	intel_dp->pps.backlight_off_delay = pps_units_to_msecs(final->backlight_off);
	intel_dp->pps.panel_power_down_delay = pps_units_to_msecs(final->power_down);
	intel_dp->pps.panel_power_cycle_delay = pps_units_to_msecs(final->power_cycle);

	drm_dbg_kms(display->drm,
		    "panel power up delay %d, power down delay %d, power cycle delay %d\n",
		    intel_dp->pps.panel_power_up_delay,
		    intel_dp->pps.panel_power_down_delay,
		    intel_dp->pps.panel_power_cycle_delay);

	drm_dbg_kms(display->drm, "backlight on delay %d, off delay %d\n",
		    intel_dp->pps.backlight_on_delay,
		    intel_dp->pps.backlight_off_delay);

	/*
	 * We override the HW backlight delays to 1 because we do manual waits
	 * on them. For backlight_on, even BSpec recommends doing it. For
	 * backlight_off, if we don't do this, we'll end up waiting for the
	 * backlight off delay twice: once when we do the manual sleep, and
	 * once when we disable the panel and wait for the PP_STATUS bit to
	 * become zero.
	 */
	final->backlight_on = 1;
	final->backlight_off = 1;

	/*
	 * HW has only a 100msec granularity for power_cycle so round it up
	 * accordingly.
	 */
	final->power_cycle = roundup(final->power_cycle, msecs_to_pps_units(100));
}

static void pps_init_registers(struct intel_dp *intel_dp, bool force_disable_vdd)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	u32 pp_on, pp_off, port_sel = 0;
	int div = DISPLAY_RUNTIME_INFO(display)->rawclk_freq / 1000;
	struct pps_registers regs;
	enum port port = dp_to_dig_port(intel_dp)->base.port;
	const struct intel_pps_delays *seq = &intel_dp->pps.pps_delays;

	lockdep_assert_held(&display->pps.mutex);

	intel_pps_get_registers(intel_dp, &regs);

	/*
	 * On some VLV machines the BIOS can leave the VDD
	 * enabled even on power sequencers which aren't
	 * hooked up to any port. This would mess up the
	 * power domain tracking the first time we pick
	 * one of these power sequencers for use since
	 * intel_pps_vdd_on_unlocked() would notice that the VDD was
	 * already on and therefore wouldn't grab the power
	 * domain reference. Disable VDD first to avoid this.
	 * This also avoids spuriously turning the VDD on as
	 * soon as the new power sequencer gets initialized.
	 */
	if (force_disable_vdd) {
		u32 pp = ilk_get_pp_control(intel_dp);

		drm_WARN(display->drm, pp & PANEL_POWER_ON,
			 "Panel power already on\n");

		if (pp & EDP_FORCE_VDD)
			drm_dbg_kms(display->drm,
				    "VDD already on, disabling first\n");

		pp &= ~EDP_FORCE_VDD;

		intel_de_write(display, regs.pp_ctrl, pp);
	}

	pp_on = REG_FIELD_PREP(PANEL_POWER_UP_DELAY_MASK, seq->power_up) |
		REG_FIELD_PREP(PANEL_LIGHT_ON_DELAY_MASK, seq->backlight_on);
	pp_off = REG_FIELD_PREP(PANEL_LIGHT_OFF_DELAY_MASK, seq->backlight_off) |
		REG_FIELD_PREP(PANEL_POWER_DOWN_DELAY_MASK, seq->power_down);

	/* Haswell doesn't have any port selection bits for the panel
	 * power sequencer any more. */
	if (display->platform.valleyview || display->platform.cherryview) {
		port_sel = PANEL_PORT_SELECT_VLV(port);
	} else if (HAS_PCH_IBX(dev_priv) || HAS_PCH_CPT(dev_priv)) {
		switch (port) {
		case PORT_A:
			port_sel = PANEL_PORT_SELECT_DPA;
			break;
		case PORT_C:
			port_sel = PANEL_PORT_SELECT_DPC;
			break;
		case PORT_D:
			port_sel = PANEL_PORT_SELECT_DPD;
			break;
		default:
			MISSING_CASE(port);
			break;
		}
	}

	pp_on |= port_sel;

	intel_de_write(display, regs.pp_on, pp_on);
	intel_de_write(display, regs.pp_off, pp_off);

	/*
	 * Compute the divisor for the pp clock, simply match the Bspec formula.
	 */
	if (i915_mmio_reg_valid(regs.pp_div))
		intel_de_write(display, regs.pp_div,
			       REG_FIELD_PREP(PP_REFERENCE_DIVIDER_MASK,
					      (100 * div) / 2 - 1) |
			       REG_FIELD_PREP(PANEL_POWER_CYCLE_DELAY_MASK,
					      DIV_ROUND_UP(seq->power_cycle, 1000) + 1));
	else
		intel_de_rmw(display, regs.pp_ctrl, BXT_POWER_CYCLE_DELAY_MASK,
			     REG_FIELD_PREP(BXT_POWER_CYCLE_DELAY_MASK,
					    DIV_ROUND_UP(seq->power_cycle, 1000) + 1));

	drm_dbg_kms(display->drm,
		    "panel power sequencer register settings: PP_ON %#x, PP_OFF %#x, PP_DIV %#x\n",
		    intel_de_read(display, regs.pp_on),
		    intel_de_read(display, regs.pp_off),
		    i915_mmio_reg_valid(regs.pp_div) ?
		    intel_de_read(display, regs.pp_div) :
		    (intel_de_read(display, regs.pp_ctrl) & BXT_POWER_CYCLE_DELAY_MASK));
}

void intel_pps_encoder_reset(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	intel_wakeref_t wakeref;

	if (!intel_dp_is_edp(intel_dp))
		return;

	with_intel_pps_lock(intel_dp, wakeref) {
		/*
		 * Reinit the power sequencer also on the resume path, in case
		 * BIOS did something nasty with it.
		 */
		if (display->platform.valleyview || display->platform.cherryview)
			vlv_initial_power_sequencer_setup(intel_dp);

		pps_init_delays(intel_dp);
		pps_init_registers(intel_dp, false);
		pps_vdd_init(intel_dp);

		if (edp_have_panel_vdd(intel_dp))
			edp_panel_vdd_schedule_off(intel_dp);
	}
}

bool intel_pps_init(struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;
	bool ret;

	intel_dp->pps.initializing = true;
	INIT_DELAYED_WORK(&intel_dp->pps.panel_vdd_work, edp_panel_vdd_work);

	pps_init_timestamps(intel_dp);

	with_intel_pps_lock(intel_dp, wakeref) {
		ret = pps_initial_setup(intel_dp);

		pps_init_delays(intel_dp);
		pps_init_registers(intel_dp, false);
		pps_vdd_init(intel_dp);
	}

	return ret;
}

static void pps_init_late(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct intel_connector *connector = intel_dp->attached_connector;

	if (display->platform.valleyview || display->platform.cherryview)
		return;

	if (intel_num_pps(display) < 2)
		return;

	drm_WARN(display->drm,
		 connector->panel.vbt.backlight.controller >= 0 &&
		 intel_dp->pps.pps_idx != connector->panel.vbt.backlight.controller,
		 "[ENCODER:%d:%s] power sequencer mismatch: %d (initial) vs. %d (VBT)\n",
		 encoder->base.base.id, encoder->base.name,
		 intel_dp->pps.pps_idx, connector->panel.vbt.backlight.controller);

	if (connector->panel.vbt.backlight.controller >= 0)
		intel_dp->pps.pps_idx = connector->panel.vbt.backlight.controller;
}

void intel_pps_init_late(struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;

	with_intel_pps_lock(intel_dp, wakeref) {
		/* Reinit delays after per-panel info has been parsed from VBT */
		pps_init_late(intel_dp);

		memset(&intel_dp->pps.pps_delays, 0, sizeof(intel_dp->pps.pps_delays));
		pps_init_delays(intel_dp);
		pps_init_registers(intel_dp, false);

		intel_dp->pps.initializing = false;

		if (edp_have_panel_vdd(intel_dp))
			edp_panel_vdd_schedule_off(intel_dp);
	}
}

void intel_pps_unlock_regs_wa(struct intel_display *display)
{
	int pps_num;
	int pps_idx;

	if (!HAS_DISPLAY(display) || HAS_DDI(display))
		return;
	/*
	 * This w/a is needed at least on CPT/PPT, but to be sure apply it
	 * everywhere where registers can be write protected.
	 */
	pps_num = intel_num_pps(display);

	for (pps_idx = 0; pps_idx < pps_num; pps_idx++)
		intel_de_rmw(display, PP_CONTROL(display, pps_idx),
			     PANEL_UNLOCK_MASK, PANEL_UNLOCK_REGS);
}

void intel_pps_setup(struct intel_display *display)
{
	struct drm_i915_private *i915 = to_i915(display->drm);

	if (HAS_PCH_SPLIT(i915) || display->platform.geminilake || display->platform.broxton)
		display->pps.mmio_base = PCH_PPS_BASE;
	else if (display->platform.valleyview || display->platform.cherryview)
		display->pps.mmio_base = VLV_PPS_BASE;
	else
		display->pps.mmio_base = PPS_BASE;
}

static int intel_pps_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = m->private;
	struct intel_dp *intel_dp = intel_attached_dp(connector);

	if (connector->base.status != connector_status_connected)
		return -ENODEV;

	seq_printf(m, "Panel power up delay: %d\n",
		   intel_dp->pps.panel_power_up_delay);
	seq_printf(m, "Panel power down delay: %d\n",
		   intel_dp->pps.panel_power_down_delay);
	seq_printf(m, "Panel power cycle delay: %d\n",
		   intel_dp->pps.panel_power_cycle_delay);
	seq_printf(m, "Backlight on delay: %d\n",
		   intel_dp->pps.backlight_on_delay);
	seq_printf(m, "Backlight off delay: %d\n",
		   intel_dp->pps.backlight_off_delay);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(intel_pps);

void intel_pps_connector_debugfs_add(struct intel_connector *connector)
{
	struct dentry *root = connector->base.debugfs_entry;
	int connector_type = connector->base.connector_type;

	if (connector_type == DRM_MODE_CONNECTOR_eDP)
		debugfs_create_file("i915_panel_timings", 0444, root,
				    connector, &intel_pps_fops);
}

void assert_pps_unlocked(struct intel_display *display, enum pipe pipe)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	i915_reg_t pp_reg;
	u32 val;
	enum pipe panel_pipe = INVALID_PIPE;
	bool locked = true;

	if (drm_WARN_ON(display->drm, HAS_DDI(display)))
		return;

	if (HAS_PCH_SPLIT(dev_priv)) {
		u32 port_sel;

		pp_reg = PP_CONTROL(display, 0);
		port_sel = intel_de_read(display, PP_ON_DELAYS(display, 0)) &
			PANEL_PORT_SELECT_MASK;

		switch (port_sel) {
		case PANEL_PORT_SELECT_LVDS:
			intel_lvds_port_enabled(dev_priv, PCH_LVDS, &panel_pipe);
			break;
		case PANEL_PORT_SELECT_DPA:
			g4x_dp_port_enabled(display, DP_A, PORT_A, &panel_pipe);
			break;
		case PANEL_PORT_SELECT_DPC:
			g4x_dp_port_enabled(display, PCH_DP_C, PORT_C, &panel_pipe);
			break;
		case PANEL_PORT_SELECT_DPD:
			g4x_dp_port_enabled(display, PCH_DP_D, PORT_D, &panel_pipe);
			break;
		default:
			MISSING_CASE(port_sel);
			break;
		}
	} else if (display->platform.valleyview || display->platform.cherryview) {
		/* presumably write lock depends on pipe, not port select */
		pp_reg = PP_CONTROL(display, pipe);
		panel_pipe = pipe;
	} else {
		u32 port_sel;

		pp_reg = PP_CONTROL(display, 0);
		port_sel = intel_de_read(display, PP_ON_DELAYS(display, 0)) &
			PANEL_PORT_SELECT_MASK;

		drm_WARN_ON(display->drm,
			    port_sel != PANEL_PORT_SELECT_LVDS);
		intel_lvds_port_enabled(dev_priv, LVDS, &panel_pipe);
	}

	val = intel_de_read(display, pp_reg);
	if (!(val & PANEL_POWER_ON) ||
	    ((val & PANEL_UNLOCK_MASK) == PANEL_UNLOCK_REGS))
		locked = false;

	INTEL_DISPLAY_STATE_WARN(display, panel_pipe == pipe && locked,
				 "panel assertion failure, pipe %c regs locked\n",
				 pipe_name(pipe));
}
