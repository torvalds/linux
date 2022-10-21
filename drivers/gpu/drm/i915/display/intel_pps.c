// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "g4x_dp.h"
#include "i915_drv.h"
#include "intel_de.h"
#include "intel_display_power_well.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dpll.h"
#include "intel_lvds.h"
#include "intel_pps.h"
#include "intel_quirks.h"

static void vlv_steal_power_sequencer(struct drm_i915_private *dev_priv,
				      enum pipe pipe);

static void pps_init_delays(struct intel_dp *intel_dp);
static void pps_init_registers(struct intel_dp *intel_dp, bool force_disable_vdd);

intel_wakeref_t intel_pps_lock(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	intel_wakeref_t wakeref;

	/*
	 * See intel_pps_reset_all() why we need a power domain reference here.
	 */
	wakeref = intel_display_power_get(dev_priv, POWER_DOMAIN_DISPLAY_CORE);
	mutex_lock(&dev_priv->display.pps.mutex);

	return wakeref;
}

intel_wakeref_t intel_pps_unlock(struct intel_dp *intel_dp,
				 intel_wakeref_t wakeref)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	mutex_unlock(&dev_priv->display.pps.mutex);
	intel_display_power_put(dev_priv, POWER_DOMAIN_DISPLAY_CORE, wakeref);

	return 0;
}

static void
vlv_power_sequencer_kick(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum pipe pipe = intel_dp->pps.pps_pipe;
	bool pll_enabled, release_cl_override = false;
	enum dpio_phy phy = DPIO_PHY(pipe);
	enum dpio_channel ch = vlv_pipe_to_channel(pipe);
	u32 DP;

	if (drm_WARN(&dev_priv->drm,
		     intel_de_read(dev_priv, intel_dp->output_reg) & DP_PORT_EN,
		     "skipping pipe %c power sequencer kick due to [ENCODER:%d:%s] being active\n",
		     pipe_name(pipe), dig_port->base.base.base.id,
		     dig_port->base.base.name))
		return;

	drm_dbg_kms(&dev_priv->drm,
		    "kicking pipe %c power sequencer for [ENCODER:%d:%s]\n",
		    pipe_name(pipe), dig_port->base.base.base.id,
		    dig_port->base.base.name);

	/* Preserve the BIOS-computed detected bit. This is
	 * supposed to be read-only.
	 */
	DP = intel_de_read(dev_priv, intel_dp->output_reg) & DP_DETECTED;
	DP |= DP_VOLTAGE_0_4 | DP_PRE_EMPHASIS_0;
	DP |= DP_PORT_WIDTH(1);
	DP |= DP_LINK_TRAIN_PAT_1;

	if (IS_CHERRYVIEW(dev_priv))
		DP |= DP_PIPE_SEL_CHV(pipe);
	else
		DP |= DP_PIPE_SEL(pipe);

	pll_enabled = intel_de_read(dev_priv, DPLL(pipe)) & DPLL_VCO_ENABLE;

	/*
	 * The DPLL for the pipe must be enabled for this to work.
	 * So enable temporarily it if it's not already enabled.
	 */
	if (!pll_enabled) {
		release_cl_override = IS_CHERRYVIEW(dev_priv) &&
			!chv_phy_powergate_ch(dev_priv, phy, ch, true);

		if (vlv_force_pll_on(dev_priv, pipe, vlv_get_dpll(dev_priv))) {
			drm_err(&dev_priv->drm,
				"Failed to force on pll for pipe %c!\n",
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
	intel_de_write(dev_priv, intel_dp->output_reg, DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);

	intel_de_write(dev_priv, intel_dp->output_reg, DP | DP_PORT_EN);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);

	intel_de_write(dev_priv, intel_dp->output_reg, DP & ~DP_PORT_EN);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);

	if (!pll_enabled) {
		vlv_force_pll_off(dev_priv, pipe);

		if (release_cl_override)
			chv_phy_powergate_ch(dev_priv, phy, ch, false);
	}
}

static enum pipe vlv_find_free_pps(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;
	unsigned int pipes = (1 << PIPE_A) | (1 << PIPE_B);

	/*
	 * We don't have power sequencer currently.
	 * Pick one that's not used by other ports.
	 */
	for_each_intel_dp(&dev_priv->drm, encoder) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		if (encoder->type == INTEL_OUTPUT_EDP) {
			drm_WARN_ON(&dev_priv->drm,
				    intel_dp->pps.active_pipe != INVALID_PIPE &&
				    intel_dp->pps.active_pipe !=
				    intel_dp->pps.pps_pipe);

			if (intel_dp->pps.pps_pipe != INVALID_PIPE)
				pipes &= ~(1 << intel_dp->pps.pps_pipe);
		} else {
			drm_WARN_ON(&dev_priv->drm,
				    intel_dp->pps.pps_pipe != INVALID_PIPE);

			if (intel_dp->pps.active_pipe != INVALID_PIPE)
				pipes &= ~(1 << intel_dp->pps.active_pipe);
		}
	}

	if (pipes == 0)
		return INVALID_PIPE;

	return ffs(pipes) - 1;
}

static enum pipe
vlv_power_sequencer_pipe(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum pipe pipe;

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	/* We should never land here with regular DP ports */
	drm_WARN_ON(&dev_priv->drm, !intel_dp_is_edp(intel_dp));

	drm_WARN_ON(&dev_priv->drm, intel_dp->pps.active_pipe != INVALID_PIPE &&
		    intel_dp->pps.active_pipe != intel_dp->pps.pps_pipe);

	if (intel_dp->pps.pps_pipe != INVALID_PIPE)
		return intel_dp->pps.pps_pipe;

	pipe = vlv_find_free_pps(dev_priv);

	/*
	 * Didn't find one. This should not happen since there
	 * are two power sequencers and up to two eDP ports.
	 */
	if (drm_WARN_ON(&dev_priv->drm, pipe == INVALID_PIPE))
		pipe = PIPE_A;

	vlv_steal_power_sequencer(dev_priv, pipe);
	intel_dp->pps.pps_pipe = pipe;

	drm_dbg_kms(&dev_priv->drm,
		    "picked pipe %c power sequencer for [ENCODER:%d:%s]\n",
		    pipe_name(intel_dp->pps.pps_pipe),
		    dig_port->base.base.base.id,
		    dig_port->base.base.name);

	/* init power sequencer on this pipe and port */
	pps_init_delays(intel_dp);
	pps_init_registers(intel_dp, true);

	/*
	 * Even vdd force doesn't work until we've made
	 * the power sequencer lock in on the port.
	 */
	vlv_power_sequencer_kick(intel_dp);

	return intel_dp->pps.pps_pipe;
}

static int
bxt_power_sequencer_idx(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_connector *connector = intel_dp->attached_connector;
	int backlight_controller = connector->panel.vbt.backlight.controller;

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	/* We should never land here with regular DP ports */
	drm_WARN_ON(&dev_priv->drm, !intel_dp_is_edp(intel_dp));

	if (!intel_dp->pps.pps_reset)
		return backlight_controller;

	intel_dp->pps.pps_reset = false;

	/*
	 * Only the HW needs to be reprogrammed, the SW state is fixed and
	 * has been setup during connector init.
	 */
	pps_init_registers(intel_dp, false);

	return backlight_controller;
}

typedef bool (*vlv_pipe_check)(struct drm_i915_private *dev_priv,
			       enum pipe pipe);

static bool vlv_pipe_has_pp_on(struct drm_i915_private *dev_priv,
			       enum pipe pipe)
{
	return intel_de_read(dev_priv, PP_STATUS(pipe)) & PP_ON;
}

static bool vlv_pipe_has_vdd_on(struct drm_i915_private *dev_priv,
				enum pipe pipe)
{
	return intel_de_read(dev_priv, PP_CONTROL(pipe)) & EDP_FORCE_VDD;
}

static bool vlv_pipe_any(struct drm_i915_private *dev_priv,
			 enum pipe pipe)
{
	return true;
}

static enum pipe
vlv_initial_pps_pipe(struct drm_i915_private *dev_priv,
		     enum port port,
		     vlv_pipe_check pipe_check)
{
	enum pipe pipe;

	for (pipe = PIPE_A; pipe <= PIPE_B; pipe++) {
		u32 port_sel = intel_de_read(dev_priv, PP_ON_DELAYS(pipe)) &
			PANEL_PORT_SELECT_MASK;

		if (port_sel != PANEL_PORT_SELECT_VLV(port))
			continue;

		if (!pipe_check(dev_priv, pipe))
			continue;

		return pipe;
	}

	return INVALID_PIPE;
}

static void
vlv_initial_power_sequencer_setup(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum port port = dig_port->base.port;

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	/* try to find a pipe with this port selected */
	/* first pick one where the panel is on */
	intel_dp->pps.pps_pipe = vlv_initial_pps_pipe(dev_priv, port,
						      vlv_pipe_has_pp_on);
	/* didn't find one? pick one where vdd is on */
	if (intel_dp->pps.pps_pipe == INVALID_PIPE)
		intel_dp->pps.pps_pipe = vlv_initial_pps_pipe(dev_priv, port,
							      vlv_pipe_has_vdd_on);
	/* didn't find one? pick one with just the correct port */
	if (intel_dp->pps.pps_pipe == INVALID_PIPE)
		intel_dp->pps.pps_pipe = vlv_initial_pps_pipe(dev_priv, port,
							      vlv_pipe_any);

	/* didn't find one? just let vlv_power_sequencer_pipe() pick one when needed */
	if (intel_dp->pps.pps_pipe == INVALID_PIPE) {
		drm_dbg_kms(&dev_priv->drm,
			    "no initial power sequencer for [ENCODER:%d:%s]\n",
			    dig_port->base.base.base.id,
			    dig_port->base.base.name);
		return;
	}

	drm_dbg_kms(&dev_priv->drm,
		    "initial power sequencer for [ENCODER:%d:%s]: pipe %c\n",
		    dig_port->base.base.base.id,
		    dig_port->base.base.name,
		    pipe_name(intel_dp->pps.pps_pipe));
}

void intel_pps_reset_all(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;

	if (drm_WARN_ON(&dev_priv->drm, !IS_LP(dev_priv)))
		return;

	if (!HAS_DISPLAY(dev_priv))
		return;

	/*
	 * We can't grab pps_mutex here due to deadlock with power_domain
	 * mutex when power_domain functions are called while holding pps_mutex.
	 * That also means that in order to use pps_pipe the code needs to
	 * hold both a power domain reference and pps_mutex, and the power domain
	 * reference get/put must be done while _not_ holding pps_mutex.
	 * pps_{lock,unlock}() do these steps in the correct order, so one
	 * should use them always.
	 */

	for_each_intel_dp(&dev_priv->drm, encoder) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		drm_WARN_ON(&dev_priv->drm,
			    intel_dp->pps.active_pipe != INVALID_PIPE);

		if (encoder->type != INTEL_OUTPUT_EDP)
			continue;

		if (DISPLAY_VER(dev_priv) >= 9)
			intel_dp->pps.pps_reset = true;
		else
			intel_dp->pps.pps_pipe = INVALID_PIPE;
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
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	int pps_idx = 0;

	memset(regs, 0, sizeof(*regs));

	if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		pps_idx = bxt_power_sequencer_idx(intel_dp);
	else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		pps_idx = vlv_power_sequencer_pipe(intel_dp);

	regs->pp_ctrl = PP_CONTROL(pps_idx);
	regs->pp_stat = PP_STATUS(pps_idx);
	regs->pp_on = PP_ON_DELAYS(pps_idx);
	regs->pp_off = PP_OFF_DELAYS(pps_idx);

	/* Cycle delay moved from PP_DIVISOR to PP_CONTROL */
	if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv) ||
	    INTEL_PCH_TYPE(dev_priv) >= PCH_CNP)
		regs->pp_div = INVALID_MMIO_REG;
	else
		regs->pp_div = PP_DIVISOR(pps_idx);
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
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    intel_dp->pps.pps_pipe == INVALID_PIPE)
		return false;

	return (intel_de_read(dev_priv, _pp_stat_reg(intel_dp)) & PP_ON) != 0;
}

static bool edp_have_panel_vdd(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    intel_dp->pps.pps_pipe == INVALID_PIPE)
		return false;

	return intel_de_read(dev_priv, _pp_ctrl_reg(intel_dp)) & EDP_FORCE_VDD;
}

void intel_pps_check_power_unlocked(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	if (!intel_dp_is_edp(intel_dp))
		return;

	if (!edp_have_panel_power(intel_dp) && !edp_have_panel_vdd(intel_dp)) {
		drm_WARN(&dev_priv->drm, 1,
			 "eDP powered off while attempting aux channel communication.\n");
		drm_dbg_kms(&dev_priv->drm, "Status 0x%08x Control 0x%08x\n",
			    intel_de_read(dev_priv, _pp_stat_reg(intel_dp)),
			    intel_de_read(dev_priv, _pp_ctrl_reg(intel_dp)));
	}
}

#define IDLE_ON_MASK		(PP_ON | PP_SEQUENCE_MASK | 0                     | PP_SEQUENCE_STATE_MASK)
#define IDLE_ON_VALUE   	(PP_ON | PP_SEQUENCE_NONE | 0                     | PP_SEQUENCE_STATE_ON_IDLE)

#define IDLE_OFF_MASK		(PP_ON | PP_SEQUENCE_MASK | 0                     | 0)
#define IDLE_OFF_VALUE		(0     | PP_SEQUENCE_NONE | 0                     | 0)

#define IDLE_CYCLE_MASK		(PP_ON | PP_SEQUENCE_MASK | PP_CYCLE_DELAY_ACTIVE | PP_SEQUENCE_STATE_MASK)
#define IDLE_CYCLE_VALUE	(0     | PP_SEQUENCE_NONE | 0                     | PP_SEQUENCE_STATE_OFF_IDLE)

static void intel_pps_verify_state(struct intel_dp *intel_dp);

static void wait_panel_status(struct intel_dp *intel_dp,
				       u32 mask,
				       u32 value)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	i915_reg_t pp_stat_reg, pp_ctrl_reg;

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	intel_pps_verify_state(intel_dp);

	pp_stat_reg = _pp_stat_reg(intel_dp);
	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	drm_dbg_kms(&dev_priv->drm,
		    "mask %08x value %08x status %08x control %08x\n",
		    mask, value,
		    intel_de_read(dev_priv, pp_stat_reg),
		    intel_de_read(dev_priv, pp_ctrl_reg));

	if (intel_de_wait_for_register(dev_priv, pp_stat_reg,
				       mask, value, 5000))
		drm_err(&dev_priv->drm,
			"Panel status timeout: status %08x control %08x\n",
			intel_de_read(dev_priv, pp_stat_reg),
			intel_de_read(dev_priv, pp_ctrl_reg));

	drm_dbg_kms(&dev_priv->drm, "Wait complete\n");
}

static void wait_panel_on(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	drm_dbg_kms(&i915->drm, "Wait for panel power on\n");
	wait_panel_status(intel_dp, IDLE_ON_MASK, IDLE_ON_VALUE);
}

static void wait_panel_off(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	drm_dbg_kms(&i915->drm, "Wait for panel power off time\n");
	wait_panel_status(intel_dp, IDLE_OFF_MASK, IDLE_OFF_VALUE);
}

static void wait_panel_power_cycle(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	ktime_t panel_power_on_time;
	s64 panel_power_off_duration;

	drm_dbg_kms(&i915->drm, "Wait for panel power cycle\n");

	/* take the difference of current time and panel power off time
	 * and then make panel wait for t11_t12 if needed. */
	panel_power_on_time = ktime_get_boottime();
	panel_power_off_duration = ktime_ms_delta(panel_power_on_time, intel_dp->pps.panel_power_off_time);

	/* When we disable the VDD override bit last we have to do the manual
	 * wait. */
	if (panel_power_off_duration < (s64)intel_dp->pps.panel_power_cycle_delay)
		wait_remaining_ms_from_jiffies(jiffies,
				       intel_dp->pps.panel_power_cycle_delay - panel_power_off_duration);

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
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 control;

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	control = intel_de_read(dev_priv, _pp_ctrl_reg(intel_dp));
	if (drm_WARN_ON(&dev_priv->drm, !HAS_DDI(dev_priv) &&
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
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	u32 pp;
	i915_reg_t pp_stat_reg, pp_ctrl_reg;
	bool need_to_disable = !intel_dp->pps.want_panel_vdd;

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	if (!intel_dp_is_edp(intel_dp))
		return false;

	cancel_delayed_work(&intel_dp->pps.panel_vdd_work);
	intel_dp->pps.want_panel_vdd = true;

	if (edp_have_panel_vdd(intel_dp))
		return need_to_disable;

	drm_WARN_ON(&dev_priv->drm, intel_dp->pps.vdd_wakeref);
	intel_dp->pps.vdd_wakeref = intel_display_power_get(dev_priv,
							    intel_aux_power_domain(dig_port));

	drm_dbg_kms(&dev_priv->drm, "Turning [ENCODER:%d:%s] VDD on\n",
		    dig_port->base.base.base.id,
		    dig_port->base.base.name);

	if (!edp_have_panel_power(intel_dp))
		wait_panel_power_cycle(intel_dp);

	pp = ilk_get_pp_control(intel_dp);
	pp |= EDP_FORCE_VDD;

	pp_stat_reg = _pp_stat_reg(intel_dp);
	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	intel_de_write(dev_priv, pp_ctrl_reg, pp);
	intel_de_posting_read(dev_priv, pp_ctrl_reg);
	drm_dbg_kms(&dev_priv->drm, "PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
		    intel_de_read(dev_priv, pp_stat_reg),
		    intel_de_read(dev_priv, pp_ctrl_reg));
	/*
	 * If the panel wasn't on, delay before accessing aux channel
	 */
	if (!edp_have_panel_power(intel_dp)) {
		drm_dbg_kms(&dev_priv->drm,
			    "[ENCODER:%d:%s] panel power wasn't enabled\n",
			    dig_port->base.base.base.id,
			    dig_port->base.base.name);
		msleep(intel_dp->pps.panel_power_up_delay);
	}

	return need_to_disable;
}

/*
 * Must be paired with intel_pps_off().
 * Nested calls to these functions are not allowed since
 * we drop the lock. Caller must use some higher level
 * locking to prevent nested calls from other threads.
 */
void intel_pps_vdd_on(struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;
	bool vdd;

	if (!intel_dp_is_edp(intel_dp))
		return;

	vdd = false;
	with_intel_pps_lock(intel_dp, wakeref)
		vdd = intel_pps_vdd_on_unlocked(intel_dp);
	I915_STATE_WARN(!vdd, "[ENCODER:%d:%s] VDD already requested on\n",
			dp_to_dig_port(intel_dp)->base.base.base.id,
			dp_to_dig_port(intel_dp)->base.base.name);
}

static void intel_pps_vdd_off_sync_unlocked(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port =
		dp_to_dig_port(intel_dp);
	u32 pp;
	i915_reg_t pp_stat_reg, pp_ctrl_reg;

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	drm_WARN_ON(&dev_priv->drm, intel_dp->pps.want_panel_vdd);

	if (!edp_have_panel_vdd(intel_dp))
		return;

	drm_dbg_kms(&dev_priv->drm, "Turning [ENCODER:%d:%s] VDD off\n",
		    dig_port->base.base.base.id,
		    dig_port->base.base.name);

	pp = ilk_get_pp_control(intel_dp);
	pp &= ~EDP_FORCE_VDD;

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
	pp_stat_reg = _pp_stat_reg(intel_dp);

	intel_de_write(dev_priv, pp_ctrl_reg, pp);
	intel_de_posting_read(dev_priv, pp_ctrl_reg);

	/* Make sure sequencer is idle before allowing subsequent activity */
	drm_dbg_kms(&dev_priv->drm, "PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
		    intel_de_read(dev_priv, pp_stat_reg),
		    intel_de_read(dev_priv, pp_ctrl_reg));

	if ((pp & PANEL_POWER_ON) == 0)
		intel_dp->pps.panel_power_off_time = ktime_get_boottime();

	intel_display_power_put(dev_priv,
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
	schedule_delayed_work(&intel_dp->pps.panel_vdd_work, delay);
}

/*
 * Must be paired with edp_panel_vdd_on().
 * Must hold pps_mutex around the whole on/off sequence.
 * Can be nested with intel_pps_vdd_{on,off}() calls.
 */
void intel_pps_vdd_off_unlocked(struct intel_dp *intel_dp, bool sync)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	if (!intel_dp_is_edp(intel_dp))
		return;

	I915_STATE_WARN(!intel_dp->pps.want_panel_vdd, "[ENCODER:%d:%s] VDD not forced on",
			dp_to_dig_port(intel_dp)->base.base.base.id,
			dp_to_dig_port(intel_dp)->base.base.name);

	intel_dp->pps.want_panel_vdd = false;

	if (sync)
		intel_pps_vdd_off_sync_unlocked(intel_dp);
	else
		edp_panel_vdd_schedule_off(intel_dp);
}

void intel_pps_on_unlocked(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 pp;
	i915_reg_t pp_ctrl_reg;

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	if (!intel_dp_is_edp(intel_dp))
		return;

	drm_dbg_kms(&dev_priv->drm, "Turn [ENCODER:%d:%s] panel power on\n",
		    dp_to_dig_port(intel_dp)->base.base.base.id,
		    dp_to_dig_port(intel_dp)->base.base.name);

	if (drm_WARN(&dev_priv->drm, edp_have_panel_power(intel_dp),
		     "[ENCODER:%d:%s] panel power already on\n",
		     dp_to_dig_port(intel_dp)->base.base.base.id,
		     dp_to_dig_port(intel_dp)->base.base.name))
		return;

	wait_panel_power_cycle(intel_dp);

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
	pp = ilk_get_pp_control(intel_dp);
	if (IS_IRONLAKE(dev_priv)) {
		/* ILK workaround: disable reset around power sequence */
		pp &= ~PANEL_POWER_RESET;
		intel_de_write(dev_priv, pp_ctrl_reg, pp);
		intel_de_posting_read(dev_priv, pp_ctrl_reg);
	}

	pp |= PANEL_POWER_ON;
	if (!IS_IRONLAKE(dev_priv))
		pp |= PANEL_POWER_RESET;

	intel_de_write(dev_priv, pp_ctrl_reg, pp);
	intel_de_posting_read(dev_priv, pp_ctrl_reg);

	wait_panel_on(intel_dp);
	intel_dp->pps.last_power_on = jiffies;

	if (IS_IRONLAKE(dev_priv)) {
		pp |= PANEL_POWER_RESET; /* restore panel reset bit */
		intel_de_write(dev_priv, pp_ctrl_reg, pp);
		intel_de_posting_read(dev_priv, pp_ctrl_reg);
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
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	u32 pp;
	i915_reg_t pp_ctrl_reg;

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	if (!intel_dp_is_edp(intel_dp))
		return;

	drm_dbg_kms(&dev_priv->drm, "Turn [ENCODER:%d:%s] panel power off\n",
		    dig_port->base.base.base.id, dig_port->base.base.name);

	drm_WARN(&dev_priv->drm, !intel_dp->pps.want_panel_vdd,
		 "Need [ENCODER:%d:%s] VDD to turn off panel\n",
		 dig_port->base.base.base.id, dig_port->base.base.name);

	pp = ilk_get_pp_control(intel_dp);
	/* We need to switch off panel power _and_ force vdd, for otherwise some
	 * panels get very unhappy and cease to work. */
	pp &= ~(PANEL_POWER_ON | PANEL_POWER_RESET | EDP_FORCE_VDD |
		EDP_BLC_ENABLE);

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	intel_dp->pps.want_panel_vdd = false;

	intel_de_write(dev_priv, pp_ctrl_reg, pp);
	intel_de_posting_read(dev_priv, pp_ctrl_reg);

	wait_panel_off(intel_dp);
	intel_dp->pps.panel_power_off_time = ktime_get_boottime();

	/* We got a reference when we enabled the VDD. */
	intel_display_power_put(dev_priv,
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
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
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

		intel_de_write(dev_priv, pp_ctrl_reg, pp);
		intel_de_posting_read(dev_priv, pp_ctrl_reg);
	}
}

/* Disable backlight in the panel power control. */
void intel_pps_backlight_off(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	intel_wakeref_t wakeref;

	if (!intel_dp_is_edp(intel_dp))
		return;

	with_intel_pps_lock(intel_dp, wakeref) {
		i915_reg_t pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
		u32 pp;

		pp = ilk_get_pp_control(intel_dp);
		pp &= ~EDP_BLC_ENABLE;

		intel_de_write(dev_priv, pp_ctrl_reg, pp);
		intel_de_posting_read(dev_priv, pp_ctrl_reg);
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
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	intel_wakeref_t wakeref;
	bool is_enabled;

	is_enabled = false;
	with_intel_pps_lock(intel_dp, wakeref)
		is_enabled = ilk_get_pp_control(intel_dp) & EDP_BLC_ENABLE;
	if (is_enabled == enable)
		return;

	drm_dbg_kms(&i915->drm, "panel power control backlight %s\n",
		    enable ? "enable" : "disable");

	if (enable)
		intel_pps_backlight_on(intel_dp);
	else
		intel_pps_backlight_off(intel_dp);
}

static void vlv_detach_power_sequencer(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum pipe pipe = intel_dp->pps.pps_pipe;
	i915_reg_t pp_on_reg = PP_ON_DELAYS(pipe);

	drm_WARN_ON(&dev_priv->drm, intel_dp->pps.active_pipe != INVALID_PIPE);

	if (drm_WARN_ON(&dev_priv->drm, pipe != PIPE_A && pipe != PIPE_B))
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
	drm_dbg_kms(&dev_priv->drm,
		    "detaching pipe %c power sequencer from [ENCODER:%d:%s]\n",
		    pipe_name(pipe), dig_port->base.base.base.id,
		    dig_port->base.base.name);
	intel_de_write(dev_priv, pp_on_reg, 0);
	intel_de_posting_read(dev_priv, pp_on_reg);

	intel_dp->pps.pps_pipe = INVALID_PIPE;
}

static void vlv_steal_power_sequencer(struct drm_i915_private *dev_priv,
				      enum pipe pipe)
{
	struct intel_encoder *encoder;

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	for_each_intel_dp(&dev_priv->drm, encoder) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		drm_WARN(&dev_priv->drm, intel_dp->pps.active_pipe == pipe,
			 "stealing pipe %c power sequencer from active [ENCODER:%d:%s]\n",
			 pipe_name(pipe), encoder->base.base.id,
			 encoder->base.name);

		if (intel_dp->pps.pps_pipe != pipe)
			continue;

		drm_dbg_kms(&dev_priv->drm,
			    "stealing pipe %c power sequencer from [ENCODER:%d:%s]\n",
			    pipe_name(pipe), encoder->base.base.id,
			    encoder->base.name);

		/* make sure vdd is off before we steal it */
		vlv_detach_power_sequencer(intel_dp);
	}
}

void vlv_pps_init(struct intel_encoder *encoder,
		  const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	drm_WARN_ON(&dev_priv->drm, intel_dp->pps.active_pipe != INVALID_PIPE);

	if (intel_dp->pps.pps_pipe != INVALID_PIPE &&
	    intel_dp->pps.pps_pipe != crtc->pipe) {
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
	vlv_steal_power_sequencer(dev_priv, crtc->pipe);

	intel_dp->pps.active_pipe = crtc->pipe;

	if (!intel_dp_is_edp(intel_dp))
		return;

	/* now it's all ours */
	intel_dp->pps.pps_pipe = crtc->pipe;

	drm_dbg_kms(&dev_priv->drm,
		    "initializing pipe %c power sequencer for [ENCODER:%d:%s]\n",
		    pipe_name(intel_dp->pps.pps_pipe), encoder->base.base.id,
		    encoder->base.name);

	/* init power sequencer on this pipe and port */
	pps_init_delays(intel_dp);
	pps_init_registers(intel_dp, true);
}

static void pps_vdd_init(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	if (!edp_have_panel_vdd(intel_dp))
		return;

	/*
	 * The VDD bit needs a power domain reference, so if the bit is
	 * already enabled when we boot or resume, grab this reference and
	 * schedule a vdd off, so we don't hold on to the reference
	 * indefinitely.
	 */
	drm_dbg_kms(&dev_priv->drm,
		    "VDD left on by BIOS, adjusting state tracking\n");
	drm_WARN_ON(&dev_priv->drm, intel_dp->pps.vdd_wakeref);
	intel_dp->pps.vdd_wakeref = intel_display_power_get(dev_priv,
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
	intel_dp->pps.panel_power_off_time = ktime_get_boottime();
	intel_dp->pps.last_power_on = jiffies;
	intel_dp->pps.last_backlight_off = jiffies;
}

static void
intel_pps_readout_hw_state(struct intel_dp *intel_dp, struct edp_power_seq *seq)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 pp_on, pp_off, pp_ctl;
	struct pps_registers regs;

	intel_pps_get_registers(intel_dp, &regs);

	pp_ctl = ilk_get_pp_control(intel_dp);

	/* Ensure PPS is unlocked */
	if (!HAS_DDI(dev_priv))
		intel_de_write(dev_priv, regs.pp_ctrl, pp_ctl);

	pp_on = intel_de_read(dev_priv, regs.pp_on);
	pp_off = intel_de_read(dev_priv, regs.pp_off);

	/* Pull timing values out of registers */
	seq->t1_t3 = REG_FIELD_GET(PANEL_POWER_UP_DELAY_MASK, pp_on);
	seq->t8 = REG_FIELD_GET(PANEL_LIGHT_ON_DELAY_MASK, pp_on);
	seq->t9 = REG_FIELD_GET(PANEL_LIGHT_OFF_DELAY_MASK, pp_off);
	seq->t10 = REG_FIELD_GET(PANEL_POWER_DOWN_DELAY_MASK, pp_off);

	if (i915_mmio_reg_valid(regs.pp_div)) {
		u32 pp_div;

		pp_div = intel_de_read(dev_priv, regs.pp_div);

		seq->t11_t12 = REG_FIELD_GET(PANEL_POWER_CYCLE_DELAY_MASK, pp_div) * 1000;
	} else {
		seq->t11_t12 = REG_FIELD_GET(BXT_POWER_CYCLE_DELAY_MASK, pp_ctl) * 1000;
	}
}

static void
intel_pps_dump_state(struct intel_dp *intel_dp, const char *state_name,
		     const struct edp_power_seq *seq)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	drm_dbg_kms(&i915->drm, "%s t1_t3 %d t8 %d t9 %d t10 %d t11_t12 %d\n",
		    state_name,
		    seq->t1_t3, seq->t8, seq->t9, seq->t10, seq->t11_t12);
}

static void
intel_pps_verify_state(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct edp_power_seq hw;
	struct edp_power_seq *sw = &intel_dp->pps.pps_delays;

	intel_pps_readout_hw_state(intel_dp, &hw);

	if (hw.t1_t3 != sw->t1_t3 || hw.t8 != sw->t8 || hw.t9 != sw->t9 ||
	    hw.t10 != sw->t10 || hw.t11_t12 != sw->t11_t12) {
		drm_err(&i915->drm, "PPS state mismatch\n");
		intel_pps_dump_state(intel_dp, "sw", sw);
		intel_pps_dump_state(intel_dp, "hw", &hw);
	}
}

static bool pps_delays_valid(struct edp_power_seq *delays)
{
	return delays->t1_t3 || delays->t8 || delays->t9 ||
		delays->t10 || delays->t11_t12;
}

static void pps_init_delays_bios(struct intel_dp *intel_dp,
				 struct edp_power_seq *bios)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	if (!pps_delays_valid(&intel_dp->pps.bios_pps_delays))
		intel_pps_readout_hw_state(intel_dp, &intel_dp->pps.bios_pps_delays);

	*bios = intel_dp->pps.bios_pps_delays;

	intel_pps_dump_state(intel_dp, "bios", bios);
}

static void pps_init_delays_vbt(struct intel_dp *intel_dp,
				struct edp_power_seq *vbt)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_connector *connector = intel_dp->attached_connector;

	*vbt = connector->panel.vbt.edp.pps;

	if (!pps_delays_valid(vbt))
		return;

	/* On Toshiba Satellite P50-C-18C system the VBT T12 delay
	 * of 500ms appears to be too short. Ocassionally the panel
	 * just fails to power back on. Increasing the delay to 800ms
	 * seems sufficient to avoid this problem.
	 */
	if (intel_has_quirk(dev_priv, QUIRK_INCREASE_T12_DELAY)) {
		vbt->t11_t12 = max_t(u16, vbt->t11_t12, 1300 * 10);
		drm_dbg_kms(&dev_priv->drm,
			    "Increasing T12 panel delay as per the quirk to %d\n",
			    vbt->t11_t12);
	}

	/* T11_T12 delay is special and actually in units of 100ms, but zero
	 * based in the hw (so we need to add 100 ms). But the sw vbt
	 * table multiplies it with 1000 to make it in units of 100usec,
	 * too. */
	vbt->t11_t12 += 100 * 10;

	intel_pps_dump_state(intel_dp, "vbt", vbt);
}

static void pps_init_delays_spec(struct intel_dp *intel_dp,
				 struct edp_power_seq *spec)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	lockdep_assert_held(&dev_priv->display.pps.mutex);

	/* Upper limits from eDP 1.3 spec. Note that we use the clunky units of
	 * our hw here, which are all in 100usec. */
	spec->t1_t3 = 210 * 10;
	spec->t8 = 50 * 10; /* no limit for t8, use t7 instead */
	spec->t9 = 50 * 10; /* no limit for t9, make it symmetric with t8 */
	spec->t10 = 500 * 10;
	/* This one is special and actually in units of 100ms, but zero
	 * based in the hw (so we need to add 100 ms). But the sw vbt
	 * table multiplies it with 1000 to make it in units of 100usec,
	 * too. */
	spec->t11_t12 = (510 + 100) * 10;

	intel_pps_dump_state(intel_dp, "spec", spec);
}

static void pps_init_delays(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct edp_power_seq cur, vbt, spec,
		*final = &intel_dp->pps.pps_delays;

	lockdep_assert_held(&dev_priv->display.pps.mutex);

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
	assign_final(t1_t3);
	assign_final(t8);
	assign_final(t9);
	assign_final(t10);
	assign_final(t11_t12);
#undef assign_final

#define get_delay(field)	(DIV_ROUND_UP(final->field, 10))
	intel_dp->pps.panel_power_up_delay = get_delay(t1_t3);
	intel_dp->pps.backlight_on_delay = get_delay(t8);
	intel_dp->pps.backlight_off_delay = get_delay(t9);
	intel_dp->pps.panel_power_down_delay = get_delay(t10);
	intel_dp->pps.panel_power_cycle_delay = get_delay(t11_t12);
#undef get_delay

	drm_dbg_kms(&dev_priv->drm,
		    "panel power up delay %d, power down delay %d, power cycle delay %d\n",
		    intel_dp->pps.panel_power_up_delay,
		    intel_dp->pps.panel_power_down_delay,
		    intel_dp->pps.panel_power_cycle_delay);

	drm_dbg_kms(&dev_priv->drm, "backlight on delay %d, off delay %d\n",
		    intel_dp->pps.backlight_on_delay,
		    intel_dp->pps.backlight_off_delay);

	/*
	 * We override the HW backlight delays to 1 because we do manual waits
	 * on them. For T8, even BSpec recommends doing it. For T9, if we
	 * don't do this, we'll end up waiting for the backlight off delay
	 * twice: once when we do the manual sleep, and once when we disable
	 * the panel and wait for the PP_STATUS bit to become zero.
	 */
	final->t8 = 1;
	final->t9 = 1;

	/*
	 * HW has only a 100msec granularity for t11_t12 so round it up
	 * accordingly.
	 */
	final->t11_t12 = roundup(final->t11_t12, 100 * 10);
}

static void pps_init_registers(struct intel_dp *intel_dp, bool force_disable_vdd)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 pp_on, pp_off, port_sel = 0;
	int div = RUNTIME_INFO(dev_priv)->rawclk_freq / 1000;
	struct pps_registers regs;
	enum port port = dp_to_dig_port(intel_dp)->base.port;
	const struct edp_power_seq *seq = &intel_dp->pps.pps_delays;

	lockdep_assert_held(&dev_priv->display.pps.mutex);

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

		drm_WARN(&dev_priv->drm, pp & PANEL_POWER_ON,
			 "Panel power already on\n");

		if (pp & EDP_FORCE_VDD)
			drm_dbg_kms(&dev_priv->drm,
				    "VDD already on, disabling first\n");

		pp &= ~EDP_FORCE_VDD;

		intel_de_write(dev_priv, regs.pp_ctrl, pp);
	}

	pp_on = REG_FIELD_PREP(PANEL_POWER_UP_DELAY_MASK, seq->t1_t3) |
		REG_FIELD_PREP(PANEL_LIGHT_ON_DELAY_MASK, seq->t8);
	pp_off = REG_FIELD_PREP(PANEL_LIGHT_OFF_DELAY_MASK, seq->t9) |
		REG_FIELD_PREP(PANEL_POWER_DOWN_DELAY_MASK, seq->t10);

	/* Haswell doesn't have any port selection bits for the panel
	 * power sequencer any more. */
	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
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

	intel_de_write(dev_priv, regs.pp_on, pp_on);
	intel_de_write(dev_priv, regs.pp_off, pp_off);

	/*
	 * Compute the divisor for the pp clock, simply match the Bspec formula.
	 */
	if (i915_mmio_reg_valid(regs.pp_div)) {
		intel_de_write(dev_priv, regs.pp_div,
			       REG_FIELD_PREP(PP_REFERENCE_DIVIDER_MASK, (100 * div) / 2 - 1) | REG_FIELD_PREP(PANEL_POWER_CYCLE_DELAY_MASK, DIV_ROUND_UP(seq->t11_t12, 1000)));
	} else {
		u32 pp_ctl;

		pp_ctl = intel_de_read(dev_priv, regs.pp_ctrl);
		pp_ctl &= ~BXT_POWER_CYCLE_DELAY_MASK;
		pp_ctl |= REG_FIELD_PREP(BXT_POWER_CYCLE_DELAY_MASK, DIV_ROUND_UP(seq->t11_t12, 1000));
		intel_de_write(dev_priv, regs.pp_ctrl, pp_ctl);
	}

	drm_dbg_kms(&dev_priv->drm,
		    "panel power sequencer register settings: PP_ON %#x, PP_OFF %#x, PP_DIV %#x\n",
		    intel_de_read(dev_priv, regs.pp_on),
		    intel_de_read(dev_priv, regs.pp_off),
		    i915_mmio_reg_valid(regs.pp_div) ?
		    intel_de_read(dev_priv, regs.pp_div) :
		    (intel_de_read(dev_priv, regs.pp_ctrl) & BXT_POWER_CYCLE_DELAY_MASK));
}

void intel_pps_encoder_reset(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	intel_wakeref_t wakeref;

	if (!intel_dp_is_edp(intel_dp))
		return;

	with_intel_pps_lock(intel_dp, wakeref) {
		/*
		 * Reinit the power sequencer also on the resume path, in case
		 * BIOS did something nasty with it.
		 */
		if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
			vlv_initial_power_sequencer_setup(intel_dp);

		pps_init_delays(intel_dp);
		pps_init_registers(intel_dp, false);
		pps_vdd_init(intel_dp);

		if (edp_have_panel_vdd(intel_dp))
			edp_panel_vdd_schedule_off(intel_dp);
	}
}

void intel_pps_init(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	intel_wakeref_t wakeref;

	intel_dp->pps.initializing = true;
	INIT_DELAYED_WORK(&intel_dp->pps.panel_vdd_work, edp_panel_vdd_work);

	pps_init_timestamps(intel_dp);

	with_intel_pps_lock(intel_dp, wakeref) {
		if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
			vlv_initial_power_sequencer_setup(intel_dp);

		pps_init_delays(intel_dp);
		pps_init_registers(intel_dp, false);
		pps_vdd_init(intel_dp);
	}
}

void intel_pps_init_late(struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;

	with_intel_pps_lock(intel_dp, wakeref) {
		/* Reinit delays after per-panel info has been parsed from VBT */
		memset(&intel_dp->pps.pps_delays, 0, sizeof(intel_dp->pps.pps_delays));
		pps_init_delays(intel_dp);
		pps_init_registers(intel_dp, false);

		intel_dp->pps.initializing = false;

		if (edp_have_panel_vdd(intel_dp))
			edp_panel_vdd_schedule_off(intel_dp);
	}
}

void intel_pps_unlock_regs_wa(struct drm_i915_private *dev_priv)
{
	int pps_num;
	int pps_idx;

	if (!HAS_DISPLAY(dev_priv) || HAS_DDI(dev_priv))
		return;
	/*
	 * This w/a is needed at least on CPT/PPT, but to be sure apply it
	 * everywhere where registers can be write protected.
	 */
	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		pps_num = 2;
	else
		pps_num = 1;

	for (pps_idx = 0; pps_idx < pps_num; pps_idx++) {
		u32 val = intel_de_read(dev_priv, PP_CONTROL(pps_idx));

		val = (val & ~PANEL_UNLOCK_MASK) | PANEL_UNLOCK_REGS;
		intel_de_write(dev_priv, PP_CONTROL(pps_idx), val);
	}
}

void intel_pps_setup(struct drm_i915_private *i915)
{
	if (HAS_PCH_SPLIT(i915) || IS_GEMINILAKE(i915) || IS_BROXTON(i915))
		i915->display.pps.mmio_base = PCH_PPS_BASE;
	else if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
		i915->display.pps.mmio_base = VLV_PPS_BASE;
	else
		i915->display.pps.mmio_base = PPS_BASE;
}

void assert_pps_unlocked(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	i915_reg_t pp_reg;
	u32 val;
	enum pipe panel_pipe = INVALID_PIPE;
	bool locked = true;

	if (drm_WARN_ON(&dev_priv->drm, HAS_DDI(dev_priv)))
		return;

	if (HAS_PCH_SPLIT(dev_priv)) {
		u32 port_sel;

		pp_reg = PP_CONTROL(0);
		port_sel = intel_de_read(dev_priv, PP_ON_DELAYS(0)) & PANEL_PORT_SELECT_MASK;

		switch (port_sel) {
		case PANEL_PORT_SELECT_LVDS:
			intel_lvds_port_enabled(dev_priv, PCH_LVDS, &panel_pipe);
			break;
		case PANEL_PORT_SELECT_DPA:
			g4x_dp_port_enabled(dev_priv, DP_A, PORT_A, &panel_pipe);
			break;
		case PANEL_PORT_SELECT_DPC:
			g4x_dp_port_enabled(dev_priv, PCH_DP_C, PORT_C, &panel_pipe);
			break;
		case PANEL_PORT_SELECT_DPD:
			g4x_dp_port_enabled(dev_priv, PCH_DP_D, PORT_D, &panel_pipe);
			break;
		default:
			MISSING_CASE(port_sel);
			break;
		}
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		/* presumably write lock depends on pipe, not port select */
		pp_reg = PP_CONTROL(pipe);
		panel_pipe = pipe;
	} else {
		u32 port_sel;

		pp_reg = PP_CONTROL(0);
		port_sel = intel_de_read(dev_priv, PP_ON_DELAYS(0)) & PANEL_PORT_SELECT_MASK;

		drm_WARN_ON(&dev_priv->drm,
			    port_sel != PANEL_PORT_SELECT_LVDS);
		intel_lvds_port_enabled(dev_priv, LVDS, &panel_pipe);
	}

	val = intel_de_read(dev_priv, pp_reg);
	if (!(val & PANEL_POWER_ON) ||
	    ((val & PANEL_UNLOCK_MASK) == PANEL_UNLOCK_REGS))
		locked = false;

	I915_STATE_WARN(panel_pipe == pipe && locked,
			"panel assertion failure, pipe %c regs locked\n",
			pipe_name(pipe));
}
