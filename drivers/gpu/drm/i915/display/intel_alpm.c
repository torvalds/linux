// SPDX-License-Identifier: MIT
/*
 * Copyright 2024, Intel Corporation.
 */

#include "intel_alpm.h"
#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_aux.h"
#include "intel_psr_regs.h"

bool intel_alpm_aux_wake_supported(struct intel_dp *intel_dp)
{
	return intel_dp->alpm_dpcd & DP_ALPM_CAP;
}

bool intel_alpm_aux_less_wake_supported(struct intel_dp *intel_dp)
{
	return intel_dp->alpm_dpcd & DP_ALPM_AUX_LESS_CAP;
}

void intel_alpm_init_dpcd(struct intel_dp *intel_dp)
{
	u8 dpcd;

	if (drm_dp_dpcd_readb(&intel_dp->aux, DP_RECEIVER_ALPM_CAP, &dpcd) < 0)
		return;

	intel_dp->alpm_dpcd = dpcd;
}

/*
 * See Bspec: 71632 for the table
 *
 * Silence_period = tSilence,Min + ((tSilence,Max - tSilence,Min) / 2)
 *
 * Half cycle duration:
 *
 * Link rates 1.62 - 4.32 and tLFPS_Cycle = 70 ns
 * FLOOR( (Link Rate * tLFPS_Cycle) / (2 * 10) )
 *
 * Link rates 5.4 - 8.1
 * PORT_ALPM_LFPS_CTL[ LFPS Cycle Count ] = 10
 * LFPS Period chosen is the mid-point of the min:max values from the table
 * FLOOR( LFPS Period in Symbol clocks /
 * (2 * PORT_ALPM_LFPS_CTL[ LFPS Cycle Count ]) )
 */
static bool _lnl_get_silence_period_and_lfps_half_cycle(int link_rate,
							int *silence_period,
							int *lfps_half_cycle)
{
	switch (link_rate) {
	case 162000:
		*silence_period = 20;
		*lfps_half_cycle = 5;
		break;
	case 216000:
		*silence_period = 27;
		*lfps_half_cycle = 7;
		break;
	case 243000:
		*silence_period = 31;
		*lfps_half_cycle = 8;
		break;
	case 270000:
		*silence_period = 34;
		*lfps_half_cycle = 9;
		break;
	case 324000:
		*silence_period = 41;
		*lfps_half_cycle = 11;
		break;
	case 432000:
		*silence_period = 56;
		*lfps_half_cycle = 15;
		break;
	case 540000:
		*silence_period = 69;
		*lfps_half_cycle = 12;
		break;
	case 648000:
		*silence_period = 84;
		*lfps_half_cycle = 15;
		break;
	case 675000:
		*silence_period = 87;
		*lfps_half_cycle = 15;
		break;
	case 810000:
		*silence_period = 104;
		*lfps_half_cycle = 19;
		break;
	default:
		*silence_period = *lfps_half_cycle = -1;
		return false;
	}
	return true;
}

/*
 * AUX-Less Wake Time = CEILING( ((PHY P2 to P0) + tLFPS_Period, Max+
 * tSilence, Max+ tPHY Establishment + tCDS) / tline)
 * For the "PHY P2 to P0" latency see the PHY Power Control page
 * (PHY P2 to P0) : https://gfxspecs.intel.com/Predator/Home/Index/68965
 * : 12 us
 * The tLFPS_Period, Max term is 800ns
 * The tSilence, Max term is 180ns
 * The tPHY Establishment (a.k.a. t1) term is 50us
 * The tCDS term is 1 or 2 times t2
 * t2 = Number ML_PHY_LOCK * tML_PHY_LOCK
 * Number ML_PHY_LOCK = ( 7 + CEILING( 6.5us / tML_PHY_LOCK ) + 1)
 * Rounding up the 6.5us padding to the next ML_PHY_LOCK boundary and
 * adding the "+ 1" term ensures all ML_PHY_LOCK sequences that start
 * within the CDS period complete within the CDS period regardless of
 * entry into the period
 * tML_PHY_LOCK = TPS4 Length * ( 10 / (Link Rate in MHz) )
 * TPS4 Length = 252 Symbols
 */
static int _lnl_compute_aux_less_wake_time(int port_clock)
{
	int tphy2_p2_to_p0 = 12 * 1000;
	int tlfps_period_max = 800;
	int tsilence_max = 180;
	int t1 = 50 * 1000;
	int tps4 = 252;
	/* port_clock is link rate in 10kbit/s units */
	int tml_phy_lock = 1000 * 1000 * tps4 / port_clock;
	int num_ml_phy_lock = 7 + DIV_ROUND_UP(6500, tml_phy_lock) + 1;
	int t2 = num_ml_phy_lock * tml_phy_lock;
	int tcds = 1 * t2;

	return DIV_ROUND_UP(tphy2_p2_to_p0 + tlfps_period_max + tsilence_max +
			    t1 + tcds, 1000);
}

static int
_lnl_compute_aux_less_alpm_params(struct intel_dp *intel_dp,
				  const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int aux_less_wake_time, aux_less_wake_lines, silence_period,
		lfps_half_cycle;

	aux_less_wake_time =
		_lnl_compute_aux_less_wake_time(crtc_state->port_clock);
	aux_less_wake_lines = intel_usecs_to_scanlines(&crtc_state->hw.adjusted_mode,
						       aux_less_wake_time);

	if (!_lnl_get_silence_period_and_lfps_half_cycle(crtc_state->port_clock,
							 &silence_period,
							 &lfps_half_cycle))
		return false;

	if (aux_less_wake_lines > ALPM_CTL_AUX_LESS_WAKE_TIME_MASK ||
	    silence_period > PORT_ALPM_CTL_SILENCE_PERIOD_MASK ||
	    lfps_half_cycle > PORT_ALPM_LFPS_CTL_LAST_LFPS_HALF_CYCLE_DURATION_MASK)
		return false;

	if (i915->display.params.psr_safest_params)
		aux_less_wake_lines = ALPM_CTL_AUX_LESS_WAKE_TIME_MASK;

	intel_dp->alpm_parameters.aux_less_wake_lines = aux_less_wake_lines;
	intel_dp->alpm_parameters.silence_period_sym_clocks = silence_period;
	intel_dp->alpm_parameters.lfps_half_cycle_num_of_syms = lfps_half_cycle;

	return true;
}

static bool _lnl_compute_alpm_params(struct intel_dp *intel_dp,
				     const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int check_entry_lines;

	if (DISPLAY_VER(i915) < 20)
		return true;

	/* ALPM Entry Check = 2 + CEILING( 5us /tline ) */
	check_entry_lines = 2 +
		intel_usecs_to_scanlines(&crtc_state->hw.adjusted_mode, 5);

	if (check_entry_lines > 15)
		return false;

	if (!_lnl_compute_aux_less_alpm_params(intel_dp, crtc_state))
		return false;

	if (i915->display.params.psr_safest_params)
		check_entry_lines = 15;

	intel_dp->alpm_parameters.check_entry_lines = check_entry_lines;

	return true;
}

/*
 * IO wake time for DISPLAY_VER < 12 is not directly mentioned in Bspec. There
 * are 50 us io wake time and 32 us fast wake time. Clearly preharge pulses are
 * not (improperly) included in 32 us fast wake time. 50 us - 32 us = 18 us.
 */
static int skl_io_buffer_wake_time(void)
{
	return 18;
}

static int tgl_io_buffer_wake_time(void)
{
	return 10;
}

static int io_buffer_wake_time(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (DISPLAY_VER(i915) >= 12)
		return tgl_io_buffer_wake_time();
	else
		return skl_io_buffer_wake_time();
}

bool intel_alpm_compute_params(struct intel_dp *intel_dp,
			       const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int io_wake_lines, io_wake_time, fast_wake_lines, fast_wake_time;
	int tfw_exit_latency = 20; /* eDP spec */
	int phy_wake = 4;	   /* eDP spec */
	int preamble = 8;	   /* eDP spec */
	int precharge = intel_dp_aux_fw_sync_len() - preamble;
	u8 max_wake_lines;

	io_wake_time = max(precharge, io_buffer_wake_time(crtc_state)) +
		preamble + phy_wake + tfw_exit_latency;
	fast_wake_time = precharge + preamble + phy_wake +
		tfw_exit_latency;

	if (DISPLAY_VER(i915) >= 20)
		max_wake_lines = 68;
	else if (DISPLAY_VER(i915) >= 12)
		max_wake_lines = 12;
	else
		max_wake_lines = 8;

	io_wake_lines = intel_usecs_to_scanlines(
		&crtc_state->hw.adjusted_mode, io_wake_time);
	fast_wake_lines = intel_usecs_to_scanlines(
		&crtc_state->hw.adjusted_mode, fast_wake_time);

	if (io_wake_lines > max_wake_lines ||
	    fast_wake_lines > max_wake_lines)
		return false;

	if (!_lnl_compute_alpm_params(intel_dp, crtc_state))
		return false;

	if (i915->display.params.psr_safest_params)
		io_wake_lines = fast_wake_lines = max_wake_lines;

	/* According to Bspec lower limit should be set as 7 lines. */
	intel_dp->alpm_parameters.io_wake_lines = max(io_wake_lines, 7);
	intel_dp->alpm_parameters.fast_wake_lines = max(fast_wake_lines, 7);

	return true;
}

void intel_alpm_lobf_compute_config(struct intel_dp *intel_dp,
				    struct intel_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	int waketime_in_lines, first_sdp_position;
	int context_latency, guardband;

	if (!intel_dp_is_edp(intel_dp))
		return;

	if (DISPLAY_VER(i915) < 20)
		return;

	if (!intel_dp_as_sdp_supported(intel_dp))
		return;

	if (crtc_state->has_psr)
		return;

	if (!(intel_alpm_aux_wake_supported(intel_dp) ||
	      intel_alpm_aux_less_wake_supported(intel_dp)))
		return;

	if (!intel_alpm_compute_params(intel_dp, crtc_state))
		return;

	context_latency = adjusted_mode->crtc_vblank_start - adjusted_mode->crtc_vdisplay;
	guardband = adjusted_mode->crtc_vtotal -
		    adjusted_mode->crtc_vdisplay - context_latency;
	first_sdp_position = adjusted_mode->crtc_vtotal - adjusted_mode->crtc_vsync_start;
	if (intel_alpm_aux_less_wake_supported(intel_dp))
		waketime_in_lines = intel_dp->alpm_parameters.io_wake_lines;
	else
		waketime_in_lines = intel_dp->alpm_parameters.aux_less_wake_lines;

	crtc_state->has_lobf = (context_latency + guardband) >
		(first_sdp_position + waketime_in_lines);
}

static void lnl_alpm_configure(struct intel_dp *intel_dp,
			       const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	enum port port = dp_to_dig_port(intel_dp)->base.port;
	u32 alpm_ctl;

	if (DISPLAY_VER(dev_priv) < 20 || (!intel_dp->psr.sel_update_enabled &&
					   !intel_dp_is_edp(intel_dp)))
		return;

	/*
	 * Panel Replay on eDP is always using ALPM aux less. I.e. no need to
	 * check panel support at this point.
	 */
	if ((intel_dp->psr.panel_replay_enabled && intel_dp_is_edp(intel_dp)) ||
	    (crtc_state->has_lobf && intel_alpm_aux_less_wake_supported(intel_dp))) {
		alpm_ctl = ALPM_CTL_ALPM_ENABLE |
			ALPM_CTL_ALPM_AUX_LESS_ENABLE |
			ALPM_CTL_AUX_LESS_SLEEP_HOLD_TIME_50_SYMBOLS |
			ALPM_CTL_AUX_LESS_WAKE_TIME(intel_dp->alpm_parameters.aux_less_wake_lines);

		intel_de_write(dev_priv,
			       PORT_ALPM_CTL(dev_priv, port),
			       PORT_ALPM_CTL_ALPM_AUX_LESS_ENABLE |
			       PORT_ALPM_CTL_MAX_PHY_SWING_SETUP(15) |
			       PORT_ALPM_CTL_MAX_PHY_SWING_HOLD(0) |
			       PORT_ALPM_CTL_SILENCE_PERIOD(
				       intel_dp->alpm_parameters.silence_period_sym_clocks));

		intel_de_write(dev_priv,
			       PORT_ALPM_LFPS_CTL(dev_priv, port),
			       PORT_ALPM_LFPS_CTL_LFPS_CYCLE_COUNT(10) |
			       PORT_ALPM_LFPS_CTL_LFPS_HALF_CYCLE_DURATION(
				       intel_dp->alpm_parameters.lfps_half_cycle_num_of_syms) |
			       PORT_ALPM_LFPS_CTL_FIRST_LFPS_HALF_CYCLE_DURATION(
				       intel_dp->alpm_parameters.lfps_half_cycle_num_of_syms) |
			       PORT_ALPM_LFPS_CTL_LAST_LFPS_HALF_CYCLE_DURATION(
				       intel_dp->alpm_parameters.lfps_half_cycle_num_of_syms));
	} else {
		alpm_ctl = ALPM_CTL_EXTENDED_FAST_WAKE_ENABLE |
			ALPM_CTL_EXTENDED_FAST_WAKE_TIME(intel_dp->alpm_parameters.fast_wake_lines);
	}

	if (crtc_state->has_lobf)
		alpm_ctl |= ALPM_CTL_LOBF_ENABLE;

	alpm_ctl |= ALPM_CTL_ALPM_ENTRY_CHECK(intel_dp->alpm_parameters.check_entry_lines);

	intel_de_write(dev_priv, ALPM_CTL(dev_priv, cpu_transcoder), alpm_ctl);
}

void intel_alpm_configure(struct intel_dp *intel_dp,
			  const struct intel_crtc_state *crtc_state)
{
	lnl_alpm_configure(intel_dp, crtc_state);
}

static int i915_edp_lobf_info_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = m->private;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct drm_crtc *crtc;
	struct intel_crtc_state *crtc_state;
	enum transcoder cpu_transcoder;
	u32 alpm_ctl;
	int ret;

	ret = drm_modeset_lock_single_interruptible(&dev_priv->drm.mode_config.connection_mutex);
	if (ret)
		return ret;

	crtc = connector->base.state->crtc;
	if (connector->base.status != connector_status_connected || !crtc) {
		ret = -ENODEV;
		goto out;
	}

	crtc_state = to_intel_crtc_state(crtc->state);
	cpu_transcoder = crtc_state->cpu_transcoder;
	alpm_ctl = intel_de_read(dev_priv, ALPM_CTL(dev_priv, cpu_transcoder));
	seq_printf(m, "LOBF status: %s\n", str_enabled_disabled(alpm_ctl & ALPM_CTL_LOBF_ENABLE));
	seq_printf(m, "Aux-wake alpm status: %s\n",
		   str_enabled_disabled(!(alpm_ctl & ALPM_CTL_ALPM_AUX_LESS_ENABLE)));
	seq_printf(m, "Aux-less alpm status: %s\n",
		   str_enabled_disabled(alpm_ctl & ALPM_CTL_ALPM_AUX_LESS_ENABLE));
out:
	drm_modeset_unlock(&dev_priv->drm.mode_config.connection_mutex);

	return ret;
}

DEFINE_SHOW_ATTRIBUTE(i915_edp_lobf_info);

void intel_alpm_lobf_debugfs_add(struct intel_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct dentry *root = connector->base.debugfs_entry;

	if (DISPLAY_VER(i915) < 20 ||
	    connector->base.connector_type != DRM_MODE_CONNECTOR_eDP)
		return;

	debugfs_create_file("i915_edp_lobf_info", 0444, root,
			    connector, &i915_edp_lobf_info_fops);
}
