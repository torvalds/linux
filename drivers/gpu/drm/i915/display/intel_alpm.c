// SPDX-License-Identifier: MIT
/*
 * Copyright 2024, Intel Corporation.
 */

#include <linux/debugfs.h>

#include <drm/drm_print.h>

#include "intel_alpm.h"
#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_aux.h"
#include "intel_psr.h"
#include "intel_psr_regs.h"

#define SILENCE_PERIOD_MIN_TIME	80
#define SILENCE_PERIOD_MAX_TIME	180
#define SILENCE_PERIOD_TIME	(SILENCE_PERIOD_MIN_TIME +	\
				(SILENCE_PERIOD_MAX_TIME -	\
				 SILENCE_PERIOD_MIN_TIME) / 2)

#define LFPS_CYCLE_COUNT 10

bool intel_alpm_aux_wake_supported(struct intel_dp *intel_dp)
{
	return intel_dp->alpm_dpcd & DP_ALPM_CAP;
}

bool intel_alpm_aux_less_wake_supported(struct intel_dp *intel_dp)
{
	return intel_dp->alpm_dpcd & DP_ALPM_AUX_LESS_CAP;
}

bool intel_alpm_is_alpm_aux_less(struct intel_dp *intel_dp,
				 const struct intel_crtc_state *crtc_state)
{
	return intel_psr_needs_alpm_aux_less(intel_dp, crtc_state) ||
		(crtc_state->has_lobf && intel_alpm_aux_less_wake_supported(intel_dp));
}

void intel_alpm_init(struct intel_dp *intel_dp)
{
	u8 dpcd;

	if (drm_dp_dpcd_readb(&intel_dp->aux, DP_RECEIVER_ALPM_CAP, &dpcd) < 0)
		return;

	intel_dp->alpm_dpcd = dpcd;
	mutex_init(&intel_dp->alpm_parameters.lock);
}

static int get_silence_period_symbols(const struct intel_crtc_state *crtc_state)
{
	return SILENCE_PERIOD_TIME * intel_dp_link_symbol_clock(crtc_state->port_clock) /
		1000 / 1000;
}

static int get_lfps_cycle_min_max_time(const struct intel_crtc_state *crtc_state,
				       int *min, int *max)
{
	if (crtc_state->port_clock < 540000) {
		*min = 65 * LFPS_CYCLE_COUNT;
		*max = 75 * LFPS_CYCLE_COUNT;
	} else if (crtc_state->port_clock <= 810000) {
		*min = 140;
		*max = 800;
	} else {
		*min = *max = -1;
		return -1;
	}

	return 0;
}

static int get_lfps_cycle_time(const struct intel_crtc_state *crtc_state)
{
	int tlfps_cycle_min, tlfps_cycle_max, ret;

	ret = get_lfps_cycle_min_max_time(crtc_state, &tlfps_cycle_min,
					  &tlfps_cycle_max);
	if (ret)
		return ret;

	return tlfps_cycle_min +  (tlfps_cycle_max - tlfps_cycle_min) / 2;
}

static int get_lfps_half_cycle_clocks(const struct intel_crtc_state *crtc_state)
{
	int lfps_cycle_time = get_lfps_cycle_time(crtc_state);

	if (lfps_cycle_time < 0)
		return -1;

	return lfps_cycle_time * crtc_state->port_clock / 1000 / 1000 / (2 * LFPS_CYCLE_COUNT);
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
static int _lnl_compute_aux_less_wake_time(const struct intel_crtc_state *crtc_state)
{
	int tphy2_p2_to_p0 = 12 * 1000;
	int t1 = 50 * 1000;
	int tps4 = 252;
	/* port_clock is link rate in 10kbit/s units */
	int tml_phy_lock = 1000 * 1000 * tps4 / crtc_state->port_clock;
	int num_ml_phy_lock = 7 + DIV_ROUND_UP(6500, tml_phy_lock) + 1;
	int t2 = num_ml_phy_lock * tml_phy_lock;
	int tcds = 1 * t2;

	return DIV_ROUND_UP(tphy2_p2_to_p0 + get_lfps_cycle_time(crtc_state) +
			    SILENCE_PERIOD_TIME + t1 + tcds, 1000);
}

static int
_lnl_compute_aux_less_alpm_params(struct intel_dp *intel_dp,
				  const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(intel_dp);
	int aux_less_wake_time, aux_less_wake_lines, silence_period,
		lfps_half_cycle;

	aux_less_wake_time =
		_lnl_compute_aux_less_wake_time(crtc_state);
	aux_less_wake_lines = intel_usecs_to_scanlines(&crtc_state->hw.adjusted_mode,
						       aux_less_wake_time);
	silence_period = get_silence_period_symbols(crtc_state);

	lfps_half_cycle = get_lfps_half_cycle_clocks(crtc_state);
	if (lfps_half_cycle < 0)
		return false;

	if (aux_less_wake_lines > ALPM_CTL_AUX_LESS_WAKE_TIME_MASK ||
	    silence_period > PORT_ALPM_CTL_SILENCE_PERIOD_MASK ||
	    lfps_half_cycle > PORT_ALPM_LFPS_CTL_LAST_LFPS_HALF_CYCLE_DURATION_MASK)
		return false;

	if (display->params.psr_safest_params)
		aux_less_wake_lines = ALPM_CTL_AUX_LESS_WAKE_TIME_MASK;

	intel_dp->alpm_parameters.aux_less_wake_lines = aux_less_wake_lines;
	intel_dp->alpm_parameters.silence_period_sym_clocks = silence_period;
	intel_dp->alpm_parameters.lfps_half_cycle_num_of_syms = lfps_half_cycle;

	return true;
}

static bool _lnl_compute_alpm_params(struct intel_dp *intel_dp,
				     const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(intel_dp);
	int check_entry_lines;

	if (DISPLAY_VER(display) < 20)
		return true;

	/* ALPM Entry Check = 2 + CEILING( 5us /tline ) */
	check_entry_lines = 2 +
		intel_usecs_to_scanlines(&crtc_state->hw.adjusted_mode, 5);

	if (check_entry_lines > 15)
		return false;

	if (!_lnl_compute_aux_less_alpm_params(intel_dp, crtc_state))
		return false;

	if (display->params.psr_safest_params)
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
	struct intel_display *display = to_intel_display(crtc_state);

	if (DISPLAY_VER(display) >= 12)
		return tgl_io_buffer_wake_time();
	else
		return skl_io_buffer_wake_time();
}

bool intel_alpm_compute_params(struct intel_dp *intel_dp,
			       const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(intel_dp);
	int io_wake_lines, io_wake_time, fast_wake_lines, fast_wake_time;
	int tfw_exit_latency = 20; /* eDP spec */
	int phy_wake = 4;	   /* eDP spec */
	int preamble = 8;	   /* eDP spec */
	int precharge = intel_dp_aux_fw_sync_len(intel_dp) - preamble;
	u8 max_wake_lines;

	io_wake_time = max(precharge, io_buffer_wake_time(crtc_state)) +
		preamble + phy_wake + tfw_exit_latency;
	fast_wake_time = precharge + preamble + phy_wake +
		tfw_exit_latency;

	if (DISPLAY_VER(display) >= 20)
		max_wake_lines = 68;
	else if (DISPLAY_VER(display) >= 12)
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

	if (display->params.psr_safest_params)
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
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	int waketime_in_lines, first_sdp_position;
	int context_latency, guardband;

	if (intel_dp->alpm_parameters.lobf_disable_debug) {
		drm_dbg_kms(display->drm, "LOBF is disabled by debug flag\n");
		return;
	}

	if (intel_dp->alpm_parameters.sink_alpm_error)
		return;

	if (!intel_dp_is_edp(intel_dp))
		return;

	if (DISPLAY_VER(display) < 20)
		return;

	if (!intel_dp->as_sdp_supported)
		return;

	if (crtc_state->has_psr)
		return;

	if (crtc_state->vrr.vmin != crtc_state->vrr.vmax ||
	    crtc_state->vrr.vmin != crtc_state->vrr.flipline)
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
	struct intel_display *display = to_intel_display(intel_dp);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 alpm_ctl;

	if (DISPLAY_VER(display) < 20 || (!intel_psr_needs_alpm(intel_dp, crtc_state) &&
					  !crtc_state->has_lobf))
		return;

	mutex_lock(&intel_dp->alpm_parameters.lock);
	/*
	 * Panel Replay on eDP is always using ALPM aux less. I.e. no need to
	 * check panel support at this point.
	 */
	if (intel_alpm_is_alpm_aux_less(intel_dp, crtc_state)) {
		alpm_ctl = ALPM_CTL_ALPM_ENABLE |
			ALPM_CTL_ALPM_AUX_LESS_ENABLE |
			ALPM_CTL_AUX_LESS_SLEEP_HOLD_TIME_50_SYMBOLS |
			ALPM_CTL_AUX_LESS_WAKE_TIME(intel_dp->alpm_parameters.aux_less_wake_lines);

		if (intel_dp->as_sdp_supported) {
			u32 pr_alpm_ctl = PR_ALPM_CTL_ADAPTIVE_SYNC_SDP_POSITION_T1;

			if (intel_dp->pr_dpcd[INTEL_PR_DPCD_INDEX(DP_PANEL_REPLAY_CAP_CAPABILITY)] &
			    DP_PANEL_REPLAY_LINK_OFF_SUPPORTED_IN_PR_AFTER_ADAPTIVE_SYNC_SDP)
				pr_alpm_ctl |= PR_ALPM_CTL_ALLOW_LINK_OFF_BETWEEN_AS_SDP_AND_SU;
			if (!(intel_dp->pr_dpcd[INTEL_PR_DPCD_INDEX(DP_PANEL_REPLAY_CAP_CAPABILITY)] &
						DP_PANEL_REPLAY_ASYNC_VIDEO_TIMING_NOT_SUPPORTED_IN_PR))
				pr_alpm_ctl |= PR_ALPM_CTL_AS_SDP_TRANSMISSION_IN_ACTIVE_DISABLE;

			intel_de_write(display, PR_ALPM_CTL(display, cpu_transcoder),
				       pr_alpm_ctl);
		}

	} else {
		alpm_ctl = ALPM_CTL_EXTENDED_FAST_WAKE_ENABLE |
			ALPM_CTL_EXTENDED_FAST_WAKE_TIME(intel_dp->alpm_parameters.fast_wake_lines);
	}

	if (crtc_state->has_lobf) {
		alpm_ctl |= ALPM_CTL_LOBF_ENABLE;
		drm_dbg_kms(display->drm, "Link off between frames (LOBF) enabled\n");
	}

	alpm_ctl |= ALPM_CTL_ALPM_ENTRY_CHECK(intel_dp->alpm_parameters.check_entry_lines);

	intel_de_write(display, ALPM_CTL(display, cpu_transcoder), alpm_ctl);
	mutex_unlock(&intel_dp->alpm_parameters.lock);
}

void intel_alpm_configure(struct intel_dp *intel_dp,
			  const struct intel_crtc_state *crtc_state)
{
	lnl_alpm_configure(intel_dp, crtc_state);
	intel_dp->alpm_parameters.transcoder = crtc_state->cpu_transcoder;
}

void intel_alpm_port_configure(struct intel_dp *intel_dp,
			       const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(intel_dp);
	enum port port = dp_to_dig_port(intel_dp)->base.port;
	u32 alpm_ctl_val = 0, lfps_ctl_val = 0;

	if (DISPLAY_VER(display) < 20)
		return;

	if (intel_alpm_is_alpm_aux_less(intel_dp, crtc_state)) {
		alpm_ctl_val = PORT_ALPM_CTL_ALPM_AUX_LESS_ENABLE |
			PORT_ALPM_CTL_MAX_PHY_SWING_SETUP(15) |
			PORT_ALPM_CTL_MAX_PHY_SWING_HOLD(0) |
			PORT_ALPM_CTL_SILENCE_PERIOD(
				intel_dp->alpm_parameters.silence_period_sym_clocks);
		lfps_ctl_val = PORT_ALPM_LFPS_CTL_LFPS_CYCLE_COUNT(LFPS_CYCLE_COUNT) |
			PORT_ALPM_LFPS_CTL_LFPS_HALF_CYCLE_DURATION(
				intel_dp->alpm_parameters.lfps_half_cycle_num_of_syms) |
			PORT_ALPM_LFPS_CTL_FIRST_LFPS_HALF_CYCLE_DURATION(
				intel_dp->alpm_parameters.lfps_half_cycle_num_of_syms) |
			PORT_ALPM_LFPS_CTL_LAST_LFPS_HALF_CYCLE_DURATION(
				intel_dp->alpm_parameters.lfps_half_cycle_num_of_syms);
	}

	intel_de_write(display, PORT_ALPM_CTL(port), alpm_ctl_val);

	intel_de_write(display, PORT_ALPM_LFPS_CTL(port), lfps_ctl_val);
}

void intel_alpm_pre_plane_update(struct intel_atomic_state *state,
				 struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	struct intel_encoder *encoder;

	if (DISPLAY_VER(display) < 20)
		return;

	if (crtc_state->has_lobf || crtc_state->has_lobf == old_crtc_state->has_lobf)
		return;

	for_each_intel_encoder_mask(display->drm, encoder,
				    crtc_state->uapi.encoder_mask) {
		struct intel_dp *intel_dp;

		if (!intel_encoder_is_dp(encoder))
			continue;

		intel_dp = enc_to_intel_dp(encoder);

		if (!intel_dp_is_edp(intel_dp))
			continue;

		if (old_crtc_state->has_lobf) {
			mutex_lock(&intel_dp->alpm_parameters.lock);
			intel_de_write(display, ALPM_CTL(display, cpu_transcoder), 0);
			drm_dbg_kms(display->drm, "Link off between frames (LOBF) disabled\n");
			mutex_unlock(&intel_dp->alpm_parameters.lock);
		}
	}
}

void intel_alpm_enable_sink(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *crtc_state)
{
	u8 val;

	if (!intel_psr_needs_alpm(intel_dp, crtc_state) && !crtc_state->has_lobf)
		return;

	val = DP_ALPM_ENABLE | DP_ALPM_LOCK_ERROR_IRQ_HPD_ENABLE;

	if (crtc_state->has_panel_replay || (crtc_state->has_lobf &&
					     intel_alpm_aux_less_wake_supported(intel_dp)))
		val |= DP_ALPM_MODE_AUX_LESS;

	drm_dp_dpcd_writeb(&intel_dp->aux, DP_RECEIVER_ALPM_CONFIG, val);
}

void intel_alpm_post_plane_update(struct intel_atomic_state *state,
				  struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct intel_encoder *encoder;

	if (crtc_state->has_psr || !crtc_state->has_lobf ||
	    crtc_state->has_lobf == old_crtc_state->has_lobf)
		return;

	for_each_intel_encoder_mask(display->drm, encoder,
				    crtc_state->uapi.encoder_mask) {
		struct intel_dp *intel_dp;

		if (!intel_encoder_is_dp(encoder))
			continue;

		intel_dp = enc_to_intel_dp(encoder);

		if (intel_dp_is_edp(intel_dp)) {
			intel_alpm_enable_sink(intel_dp, crtc_state);
			intel_alpm_configure(intel_dp, crtc_state);
		}
	}
}

static int i915_edp_lobf_info_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = m->private;
	struct intel_display *display = to_intel_display(connector);
	struct drm_crtc *crtc;
	struct intel_crtc_state *crtc_state;
	enum transcoder cpu_transcoder;
	u32 alpm_ctl;
	int ret;

	ret = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (ret)
		return ret;

	crtc = connector->base.state->crtc;
	if (connector->base.status != connector_status_connected || !crtc) {
		ret = -ENODEV;
		goto out;
	}

	crtc_state = to_intel_crtc_state(crtc->state);
	cpu_transcoder = crtc_state->cpu_transcoder;
	alpm_ctl = intel_de_read(display, ALPM_CTL(display, cpu_transcoder));
	seq_printf(m, "LOBF status: %s\n", str_enabled_disabled(alpm_ctl & ALPM_CTL_LOBF_ENABLE));
	seq_printf(m, "Aux-wake alpm status: %s\n",
		   str_enabled_disabled(!(alpm_ctl & ALPM_CTL_ALPM_AUX_LESS_ENABLE)));
	seq_printf(m, "Aux-less alpm status: %s\n",
		   str_enabled_disabled(alpm_ctl & ALPM_CTL_ALPM_AUX_LESS_ENABLE));
out:
	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	return ret;
}

DEFINE_SHOW_ATTRIBUTE(i915_edp_lobf_info);

static int
i915_edp_lobf_debug_get(void *data, u64 *val)
{
	struct intel_connector *connector = data;
	struct intel_dp *intel_dp = enc_to_intel_dp(connector->encoder);

	*val = intel_dp->alpm_parameters.lobf_disable_debug;

	return 0;
}

static int
i915_edp_lobf_debug_set(void *data, u64 val)
{
	struct intel_connector *connector = data;
	struct intel_dp *intel_dp = enc_to_intel_dp(connector->encoder);

	intel_dp->alpm_parameters.lobf_disable_debug = val;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_edp_lobf_debug_fops,
			i915_edp_lobf_debug_get, i915_edp_lobf_debug_set,
			"%llu\n");

void intel_alpm_lobf_debugfs_add(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct dentry *root = connector->base.debugfs_entry;

	if (DISPLAY_VER(display) < 20 ||
	    connector->base.connector_type != DRM_MODE_CONNECTOR_eDP)
		return;

	debugfs_create_file("i915_edp_lobf_debug", 0644, root,
			    connector, &i915_edp_lobf_debug_fops);

	debugfs_create_file("i915_edp_lobf_info", 0444, root,
			    connector, &i915_edp_lobf_info_fops);
}

void intel_alpm_disable(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	enum transcoder cpu_transcoder = intel_dp->alpm_parameters.transcoder;

	if (DISPLAY_VER(display) < 20 || !intel_dp->alpm_dpcd)
		return;

	mutex_lock(&intel_dp->alpm_parameters.lock);

	intel_de_rmw(display, ALPM_CTL(display, cpu_transcoder),
		     ALPM_CTL_ALPM_ENABLE | ALPM_CTL_LOBF_ENABLE |
		     ALPM_CTL_ALPM_AUX_LESS_ENABLE, 0);

	intel_de_rmw(display,
		     PORT_ALPM_CTL(cpu_transcoder),
		     PORT_ALPM_CTL_ALPM_AUX_LESS_ENABLE, 0);

	drm_dbg_kms(display->drm, "Disabling ALPM\n");
	mutex_unlock(&intel_dp->alpm_parameters.lock);
}

bool intel_alpm_get_error(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_dp_aux *aux = &intel_dp->aux;
	u8 val;
	int r;

	r = drm_dp_dpcd_readb(aux, DP_RECEIVER_ALPM_STATUS, &val);
	if (r != 1) {
		drm_err(display->drm, "Error reading ALPM status\n");
		return true;
	}

	if (val & DP_ALPM_LOCK_TIMEOUT_ERROR) {
		drm_dbg_kms(display->drm, "ALPM lock timeout error\n");

		/* Clearing error */
		drm_dp_dpcd_writeb(aux, DP_RECEIVER_ALPM_STATUS, val);
		return true;
	}

	return false;
}
