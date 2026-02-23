// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 *
 */

#include <drm/drm_print.h>

#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_regs.h"
#include "intel_display_types.h"
#include "intel_dmc.h"
#include "intel_dmc_regs.h"
#include "intel_dp.h"
#include "intel_psr.h"
#include "intel_vrr.h"
#include "intel_vrr_regs.h"
#include "skl_prefill.h"
#include "skl_watermark.h"

#define FIXED_POINT_PRECISION		100
#define CMRR_PRECISION_TOLERANCE	10

/*
 * Tunable parameters for DC Balance correction.
 * These are captured based on experimentations.
 */
#define DCB_CORRECTION_SENSITIVITY	30
#define DCB_CORRECTION_AGGRESSIVENESS	1000 /* ms × 100; 10 ms */
#define DCB_BLANK_TARGET		50

bool intel_vrr_is_capable(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	const struct drm_display_info *info = &connector->base.display_info;
	struct intel_dp *intel_dp;

	if (!HAS_VRR(display))
		return false;

	/*
	 * DP Sink is capable of VRR video timings if
	 * Ignore MSA bit is set in DPCD.
	 * EDID monitor range also should be atleast 10 for reasonable
	 * Adaptive Sync or Variable Refresh Rate end user experience.
	 */
	switch (connector->base.connector_type) {
	case DRM_MODE_CONNECTOR_eDP:
		if (!connector->panel.vbt.vrr)
			return false;
		fallthrough;
	case DRM_MODE_CONNECTOR_DisplayPort:
		if (connector->mst.dp)
			return false;
		intel_dp = intel_attached_dp(connector);

		if (!drm_dp_sink_can_do_video_without_timing_msa(intel_dp->dpcd))
			return false;

		break;
	default:
		return false;
	}

	return info->monitor_range.max_vfreq - info->monitor_range.min_vfreq > 10;
}

bool intel_vrr_is_in_range(struct intel_connector *connector, int vrefresh)
{
	const struct drm_display_info *info = &connector->base.display_info;

	return intel_vrr_is_capable(connector) &&
		vrefresh >= info->monitor_range.min_vfreq &&
		vrefresh <= info->monitor_range.max_vfreq;
}

bool intel_vrr_possible(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->vrr.flipline;
}

void
intel_vrr_check_modeset(struct intel_atomic_state *state)
{
	int i;
	struct intel_crtc_state *old_crtc_state, *new_crtc_state;
	struct intel_crtc *crtc;

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i) {
		if (new_crtc_state->uapi.vrr_enabled !=
		    old_crtc_state->uapi.vrr_enabled)
			new_crtc_state->uapi.mode_changed = true;
	}
}

static int intel_vrr_extra_vblank_delay(struct intel_display *display)
{
	/*
	 * On ICL/TGL VRR hardware inserts one extra scanline
	 * just after vactive, which pushes the vmin decision
	 * boundary ahead accordingly, and thus reduces the
	 * max guardband length by one scanline.
	 */
	return DISPLAY_VER(display) < 13 ? 1 : 0;
}

static int intel_vrr_vmin_flipline_offset(struct intel_display *display)
{
	/*
	 * ICL/TGL hardware imposes flipline>=vmin+1
	 *
	 * We reduce the vmin value to compensate when programming the
	 * hardware. This approach allows flipline to remain set at the
	 * original value, and thus the frame will have the desired
	 * minimum vtotal.
	 */
	return DISPLAY_VER(display) < 13 ? 1 : 0;
}

static int intel_vrr_guardband_to_pipeline_full(const struct intel_crtc_state *crtc_state,
						int guardband)
{
	/* hardware imposes one extra scanline somewhere */
	return guardband - crtc_state->framestart_delay - 1;
}

static int intel_vrr_pipeline_full_to_guardband(const struct intel_crtc_state *crtc_state,
						int pipeline_full)
{
	/* hardware imposes one extra scanline somewhere */
	return pipeline_full + crtc_state->framestart_delay + 1;
}

/*
 * Without VRR registers get latched at:
 *  vblank_start
 *
 * With VRR the earliest registers can get latched is:
 *  intel_vrr_vmin_vblank_start(), which if we want to maintain
 *  the correct min vtotal is >=vblank_start+1
 *
 * The latest point registers can get latched is the vmax decision boundary:
 *  intel_vrr_vmax_vblank_start()
 *
 * Between those two points the vblank exit starts (and hence registers get
 * latched) ASAP after a push is sent.
 *
 * framestart_delay is programmable 1-4.
 */

int intel_vrr_vmin_vtotal(const struct intel_crtc_state *crtc_state)
{
	/* Min vblank actually determined by flipline */
	return crtc_state->vrr.vmin;
}

int intel_vrr_vmax_vtotal(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->vrr.vmax;
}

int intel_vrr_vmin_vblank_start(const struct intel_crtc_state *crtc_state)
{
	return intel_vrr_vmin_vtotal(crtc_state) - crtc_state->vrr.guardband;
}

int intel_vrr_vmax_vblank_start(const struct intel_crtc_state *crtc_state)
{
	return intel_vrr_vmax_vtotal(crtc_state) - crtc_state->vrr.guardband;
}

static bool
is_cmrr_frac_required(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	int calculated_refresh_k, actual_refresh_k, pixel_clock_per_line;
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;

	/* Avoid CMRR for now till we have VRR with fixed timings working */
	if (!HAS_CMRR(display) || true)
		return false;

	actual_refresh_k =
		drm_mode_vrefresh(adjusted_mode) * FIXED_POINT_PRECISION;
	pixel_clock_per_line =
		adjusted_mode->crtc_clock * 1000 / adjusted_mode->crtc_htotal;
	calculated_refresh_k =
		pixel_clock_per_line * FIXED_POINT_PRECISION / adjusted_mode->crtc_vtotal;

	if ((actual_refresh_k - calculated_refresh_k) < CMRR_PRECISION_TOLERANCE)
		return false;

	return true;
}

static unsigned int
cmrr_get_vtotal(struct intel_crtc_state *crtc_state, bool video_mode_required)
{
	int multiplier_m = 1, multiplier_n = 1, vtotal, desired_refresh_rate;
	u64 adjusted_pixel_rate;
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;

	desired_refresh_rate = drm_mode_vrefresh(adjusted_mode);

	if (video_mode_required) {
		multiplier_m = 1001;
		multiplier_n = 1000;
	}

	crtc_state->cmrr.cmrr_n = mul_u32_u32(desired_refresh_rate * adjusted_mode->crtc_htotal,
					      multiplier_n);
	vtotal = DIV_ROUND_UP_ULL(mul_u32_u32(adjusted_mode->crtc_clock * 1000, multiplier_n),
				  crtc_state->cmrr.cmrr_n);
	adjusted_pixel_rate = mul_u32_u32(adjusted_mode->crtc_clock * 1000, multiplier_m);
	crtc_state->cmrr.cmrr_m = do_div(adjusted_pixel_rate, crtc_state->cmrr.cmrr_n);

	return vtotal;
}

static
void intel_vrr_compute_cmrr_timings(struct intel_crtc_state *crtc_state)
{
	/*
	 * TODO: Compute precise target refresh rate to determine
	 * if video_mode_required should be true. Currently set to
	 * false due to uncertainty about the precise target
	 * refresh Rate.
	 */
	crtc_state->vrr.vmax = cmrr_get_vtotal(crtc_state, false);
	crtc_state->vrr.vmin = crtc_state->vrr.vmax;
	crtc_state->vrr.flipline = crtc_state->vrr.vmin;

	crtc_state->cmrr.enable = true;
	crtc_state->mode_flags |= I915_MODE_FLAG_VRR;
}

static
void intel_vrr_compute_vrr_timings(struct intel_crtc_state *crtc_state,
				   int vmin, int vmax)
{
	crtc_state->vrr.vmax = vmax;
	crtc_state->vrr.vmin = vmin;
	crtc_state->vrr.flipline = crtc_state->vrr.vmin;

	crtc_state->vrr.enable = true;
	crtc_state->mode_flags |= I915_MODE_FLAG_VRR;
}

static
void intel_vrr_compute_fixed_rr_timings(struct intel_crtc_state *crtc_state)
{
	/* For fixed rr,  vmin = vmax = flipline */
	crtc_state->vrr.vmax = crtc_state->hw.adjusted_mode.crtc_vtotal;
	crtc_state->vrr.vmin = crtc_state->vrr.vmax;
	crtc_state->vrr.flipline = crtc_state->vrr.vmin;
}

static int intel_vrr_hw_value(const struct intel_crtc_state *crtc_state,
			      int value)
{
	struct intel_display *display = to_intel_display(crtc_state);

	/*
	 * On TGL vmin/vmax/flipline also need to be
	 * adjusted by the SCL to maintain correct vtotals.
	 */
	if (DISPLAY_VER(display) >= 13)
		return value;
	else
		return value - crtc_state->set_context_latency;
}

static int intel_vrr_vblank_start(const struct intel_crtc_state *crtc_state,
				  int vmin_vmax)
{
	return intel_vrr_hw_value(crtc_state, vmin_vmax) - crtc_state->vrr.guardband;
}

/*
 * For fixed refresh rate mode Vmin, Vmax and Flipline all are set to
 * Vtotal value.
 */
static
int intel_vrr_fixed_rr_hw_vtotal(const struct intel_crtc_state *crtc_state)
{
	return intel_vrr_hw_value(crtc_state, crtc_state->hw.adjusted_mode.crtc_vtotal);
}

static
int intel_vrr_fixed_rr_hw_vmax(const struct intel_crtc_state *crtc_state)
{
	return intel_vrr_fixed_rr_hw_vtotal(crtc_state);
}

static
int intel_vrr_fixed_rr_hw_vmin(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	return intel_vrr_fixed_rr_hw_vtotal(crtc_state) -
		intel_vrr_vmin_flipline_offset(display);
}

static
int intel_vrr_fixed_rr_hw_flipline(const struct intel_crtc_state *crtc_state)
{
	return intel_vrr_fixed_rr_hw_vtotal(crtc_state);
}

void intel_vrr_set_fixed_rr_timings(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!intel_vrr_possible(crtc_state))
		return;

	intel_de_write(display, TRANS_VRR_VMIN(display, cpu_transcoder),
		       intel_vrr_fixed_rr_hw_vmin(crtc_state) - 1);
	intel_de_write(display, TRANS_VRR_VMAX(display, cpu_transcoder),
		       intel_vrr_fixed_rr_hw_vmax(crtc_state) - 1);
	intel_de_write(display, TRANS_VRR_FLIPLINE(display, cpu_transcoder),
		       intel_vrr_fixed_rr_hw_flipline(crtc_state) - 1);
}

static
int intel_vrr_compute_vmin(struct intel_crtc_state *crtc_state)
{
	/*
	 * To make fixed rr and vrr work seamless the guardband/pipeline full
	 * should be set such that it satisfies both the fixed and variable
	 * timings.
	 * For this set the vmin as crtc_vtotal. With this we never need to
	 * change anything to do with the guardband.
	 */
	return crtc_state->hw.adjusted_mode.crtc_vtotal;
}

static
int intel_vrr_compute_vmax(struct intel_connector *connector,
			   const struct drm_display_mode *adjusted_mode)
{
	const struct drm_display_info *info = &connector->base.display_info;
	int vmax;

	vmax = adjusted_mode->crtc_clock * 1000 /
		(adjusted_mode->crtc_htotal * info->monitor_range.min_vfreq);
	vmax = max_t(int, vmax, adjusted_mode->crtc_vtotal);

	return vmax;
}

static bool intel_vrr_dc_balance_possible(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum pipe pipe = crtc->pipe;

	/*
	 * FIXME: Currently Firmware supports DC Balancing on PIPE A
	 * and PIPE B. Account those limitation while computing DC
	 * Balance parameters.
	 */
	return (HAS_VRR_DC_BALANCE(display) &&
		((pipe == PIPE_A) || (pipe == PIPE_B)));
}

static void
intel_vrr_dc_balance_compute_config(struct intel_crtc_state *crtc_state)
{
	int guardband_usec, adjustment_usec;
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;

	if (!intel_vrr_dc_balance_possible(crtc_state) || !crtc_state->vrr.enable)
		return;

	crtc_state->vrr.dc_balance.vmax = crtc_state->vrr.vmax;
	crtc_state->vrr.dc_balance.vmin = crtc_state->vrr.vmin;
	crtc_state->vrr.dc_balance.max_increase =
		crtc_state->vrr.vmax - crtc_state->vrr.vmin;
	crtc_state->vrr.dc_balance.max_decrease =
		crtc_state->vrr.vmax - crtc_state->vrr.vmin;
	crtc_state->vrr.dc_balance.guardband =
		DIV_ROUND_UP(crtc_state->vrr.dc_balance.vmax *
			     DCB_CORRECTION_SENSITIVITY, 100);
	guardband_usec =
		intel_scanlines_to_usecs(adjusted_mode,
					 crtc_state->vrr.dc_balance.guardband);
	/*
	 *  The correction_aggressiveness/100 is the number of milliseconds to
	 *  adjust by when the balance is at twice the guardband.
	 *  guardband_slope = correction_aggressiveness / (guardband * 100)
	 */
	adjustment_usec = DCB_CORRECTION_AGGRESSIVENESS * 10;
	crtc_state->vrr.dc_balance.slope =
		DIV_ROUND_UP(adjustment_usec, guardband_usec);
	crtc_state->vrr.dc_balance.vblank_target =
		DIV_ROUND_UP((crtc_state->vrr.vmax - crtc_state->vrr.vmin) *
			     DCB_BLANK_TARGET, 100);
	crtc_state->vrr.dc_balance.enable = true;
}

void
intel_vrr_compute_config(struct intel_crtc_state *crtc_state,
			 struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	bool is_edp = intel_dp_is_edp(intel_dp);
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	int vmin, vmax;

	if (!HAS_VRR(display))
		return;

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
		return;

	crtc_state->vrr.in_range =
		intel_vrr_is_in_range(connector, drm_mode_vrefresh(adjusted_mode));

	/*
	 * Allow fixed refresh rate with VRR Timing Generator.
	 * For now set the vrr.in_range to 0, to allow fixed_rr but skip actual
	 * VRR and LRR.
	 * #TODO For actual VRR with joiner, we need to figure out how to
	 * correctly sequence transcoder level stuff vs. pipe level stuff
	 * in the commit.
	 */
	if (crtc_state->joiner_pipes)
		crtc_state->vrr.in_range = false;

	vmin = intel_vrr_compute_vmin(crtc_state);

	if (crtc_state->vrr.in_range) {
		if (HAS_LRR(display))
			crtc_state->update_lrr = true;
		vmax = intel_vrr_compute_vmax(connector, adjusted_mode);
	} else {
		vmax = vmin;
	}

	if (crtc_state->uapi.vrr_enabled && vmin < vmax)
		intel_vrr_compute_vrr_timings(crtc_state, vmin, vmax);
	else if (is_cmrr_frac_required(crtc_state) && is_edp)
		intel_vrr_compute_cmrr_timings(crtc_state);
	else
		intel_vrr_compute_fixed_rr_timings(crtc_state);

	if (HAS_AS_SDP(display)) {
		crtc_state->vrr.vsync_start =
			(crtc_state->hw.adjusted_mode.crtc_vtotal -
			 crtc_state->hw.adjusted_mode.crtc_vsync_start);
		crtc_state->vrr.vsync_end =
			(crtc_state->hw.adjusted_mode.crtc_vtotal -
			 crtc_state->hw.adjusted_mode.crtc_vsync_end);
	}

	intel_vrr_dc_balance_compute_config(crtc_state);
}

static int
intel_vrr_max_hw_guardband(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	int max_pipeline_full = REG_FIELD_MAX(VRR_CTL_PIPELINE_FULL_MASK);

	if (DISPLAY_VER(display) >= 13)
		return REG_FIELD_MAX(XELPD_VRR_CTL_VRR_GUARDBAND_MASK);
	else
		return intel_vrr_pipeline_full_to_guardband(crtc_state,
							    max_pipeline_full);
}

static int
intel_vrr_max_vblank_guardband(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	const struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;

	return crtc_state->vrr.vmin -
	       adjusted_mode->crtc_vdisplay -
	       crtc_state->set_context_latency -
	       intel_vrr_extra_vblank_delay(display);
}

static int
intel_vrr_max_guardband(struct intel_crtc_state *crtc_state)
{
	return min(intel_vrr_max_hw_guardband(crtc_state),
		   intel_vrr_max_vblank_guardband(crtc_state));
}

static
int intel_vrr_compute_optimized_guardband(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct skl_prefill_ctx prefill_ctx;
	int prefill_latency_us;
	int guardband = 0;

	skl_prefill_init_worst(&prefill_ctx, crtc_state);

	/*
	 * The SoC power controller runs SAGV mutually exclusive with package C states,
	 * so the max of package C and SAGV latencies is used to compute the min prefill guardband.
	 * PM delay = max(sagv_latency, pkgc_max_latency (highest enabled wm level 1 and up))
	 */
	prefill_latency_us = max(display->sagv.block_time_us,
				 skl_watermark_max_latency(display, 1));

	guardband = skl_prefill_min_guardband(&prefill_ctx,
					      crtc_state,
					      prefill_latency_us);

	if (intel_crtc_has_dp_encoder(crtc_state)) {
		guardband = max(guardband, intel_psr_min_guardband(crtc_state));
		guardband = max(guardband, intel_dp_sdp_min_guardband(crtc_state, true));
	}

	return guardband;
}

static bool intel_vrr_use_optimized_guardband(const struct intel_crtc_state *crtc_state)
{
	/*
	 * #TODO: Enable optimized guardband for HDMI
	 * For HDMI lot of infoframes are transmitted a line or two after vsync.
	 * Since with optimized guardband the double bufferring point is at delayed vblank,
	 * we need to ensure that vsync happens after delayed vblank for the HDMI case.
	 */
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		return false;

	return true;
}

void intel_vrr_compute_guardband(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	struct drm_display_mode *pipe_mode = &crtc_state->hw.pipe_mode;
	int guardband;

	if (!intel_vrr_possible(crtc_state))
		return;

	if (intel_vrr_use_optimized_guardband(crtc_state))
		guardband = intel_vrr_compute_optimized_guardband(crtc_state);
	else
		guardband = crtc_state->vrr.vmin - adjusted_mode->crtc_vdisplay;

	crtc_state->vrr.guardband = min(guardband, intel_vrr_max_guardband(crtc_state));

	if (intel_vrr_always_use_vrr_tg(display)) {
		adjusted_mode->crtc_vblank_start  =
			adjusted_mode->crtc_vtotal - crtc_state->vrr.guardband;
		/*
		 * pipe_mode has already been derived from the
		 * original adjusted_mode, keep the two in sync.
		 */
		pipe_mode->crtc_vblank_start =
			adjusted_mode->crtc_vblank_start;
	}

	if (DISPLAY_VER(display) < 13)
		crtc_state->vrr.pipeline_full =
			intel_vrr_guardband_to_pipeline_full(crtc_state,
							     crtc_state->vrr.guardband);
}

static u32 trans_vrr_ctl(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	if (DISPLAY_VER(display) >= 14)
		return VRR_CTL_FLIP_LINE_EN |
			XELPD_VRR_CTL_VRR_GUARDBAND(crtc_state->vrr.guardband);
	else if (DISPLAY_VER(display) >= 13)
		return VRR_CTL_IGN_MAX_SHIFT | VRR_CTL_FLIP_LINE_EN |
			XELPD_VRR_CTL_VRR_GUARDBAND(crtc_state->vrr.guardband);
	else
		return VRR_CTL_IGN_MAX_SHIFT | VRR_CTL_FLIP_LINE_EN |
			VRR_CTL_PIPELINE_FULL(crtc_state->vrr.pipeline_full) |
			VRR_CTL_PIPELINE_FULL_OVERRIDE;
}

void intel_vrr_set_transcoder_timings(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!HAS_VRR(display))
		return;

	/*
	 * This bit seems to have two meanings depending on the platform:
	 * TGL: generate VRR "safe window" for DSB vblank waits
	 * ADL/DG2: make TRANS_SET_CONTEXT_LATENCY effective with VRR
	 */
	if (IS_DISPLAY_VER(display, 12, 13))
		intel_de_rmw(display, CHICKEN_TRANS(display, cpu_transcoder),
			     0, PIPE_VBLANK_WITH_DELAY);

	if (!intel_vrr_possible(crtc_state)) {
		intel_de_write(display,
			       TRANS_VRR_CTL(display, cpu_transcoder), 0);
		return;
	}

	if (crtc_state->cmrr.enable) {
		intel_de_write(display, TRANS_CMRR_M_HI(display, cpu_transcoder),
			       upper_32_bits(crtc_state->cmrr.cmrr_m));
		intel_de_write(display, TRANS_CMRR_M_LO(display, cpu_transcoder),
			       lower_32_bits(crtc_state->cmrr.cmrr_m));
		intel_de_write(display, TRANS_CMRR_N_HI(display, cpu_transcoder),
			       upper_32_bits(crtc_state->cmrr.cmrr_n));
		intel_de_write(display, TRANS_CMRR_N_LO(display, cpu_transcoder),
			       lower_32_bits(crtc_state->cmrr.cmrr_n));
	}

	intel_vrr_set_fixed_rr_timings(crtc_state);

	if (!intel_vrr_always_use_vrr_tg(display))
		intel_de_write(display, TRANS_VRR_CTL(display, cpu_transcoder),
			       trans_vrr_ctl(crtc_state));

	if (HAS_AS_SDP(display))
		intel_de_write(display,
			       TRANS_VRR_VSYNC(display, cpu_transcoder),
			       VRR_VSYNC_END(crtc_state->vrr.vsync_end) |
			       VRR_VSYNC_START(crtc_state->vrr.vsync_start));

	/*
	 * For BMG and LNL+ onwards the EMP_AS_SDP_TL is used for programming
	 * double buffering point and transmission line for VRR packets for
	 * HDMI2.1/DP/eDP/DP->HDMI2.1 PCON.
	 * Since currently we support VRR only for DP/eDP, so this is programmed
	 * to for Adaptive Sync SDP to Vsync start.
	 */
	if (DISPLAY_VERx100(display) == 1401 || DISPLAY_VER(display) >= 20)
		intel_de_write(display,
			       EMP_AS_SDP_TL(display, cpu_transcoder),
			       EMP_AS_SDP_DB_TL(crtc_state->vrr.vsync_start));
}

void
intel_vrr_dcb_increment_flip_count(struct intel_crtc_state *crtc_state,
				   struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum pipe pipe = crtc->pipe;

	if (!crtc_state->vrr.dc_balance.enable)
		return;

	intel_de_write(display, PIPEDMC_DCB_FLIP_COUNT(pipe),
		       ++crtc->dc_balance.flip_count);
}

void
intel_vrr_dcb_reset(const struct intel_crtc_state *old_crtc_state,
		    struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(old_crtc_state);
	enum pipe pipe = crtc->pipe;

	if (!old_crtc_state->vrr.dc_balance.enable)
		return;

	intel_de_write(display, PIPEDMC_DCB_FLIP_COUNT(pipe), 0);
	intel_de_write(display, PIPEDMC_DCB_BALANCE_RESET(pipe), 0);
}

void intel_vrr_send_push(struct intel_dsb *dsb,
			 const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!crtc_state->vrr.enable)
		return;

	if (dsb)
		intel_dsb_nonpost_start(dsb);

	intel_de_write_dsb(display, dsb,
			   TRANS_PUSH(display, cpu_transcoder),
			   TRANS_PUSH_EN | TRANS_PUSH_SEND);

	if (dsb)
		intel_dsb_nonpost_end(dsb);
}

void intel_vrr_check_push_sent(struct intel_dsb *dsb,
			       const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!crtc_state->vrr.enable)
		return;

	/*
	 * Make sure the push send bit has cleared. This should
	 * already be the case as long as the caller makes sure
	 * this is called after the delayed vblank has occurred.
	 */
	if (dsb) {
		int wait_us, count;

		wait_us = 2;
		count = 1;

		/*
		 * If the bit hasn't cleared the DSB will
		 * raise the poll error interrupt.
		 */
		intel_dsb_poll(dsb, TRANS_PUSH(display, cpu_transcoder),
			       TRANS_PUSH_SEND, 0, wait_us, count);
	} else {
		if (intel_vrr_is_push_sent(crtc_state))
			drm_err(display->drm, "[CRTC:%d:%s] VRR push send still pending\n",
				crtc->base.base.id, crtc->base.name);
	}
}

bool intel_vrr_is_push_sent(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!crtc_state->vrr.enable)
		return false;

	return intel_de_read(display, TRANS_PUSH(display, cpu_transcoder)) & TRANS_PUSH_SEND;
}

bool intel_vrr_always_use_vrr_tg(struct intel_display *display)
{
	if (!HAS_VRR(display))
		return false;

	if (DISPLAY_VER(display) >= 30)
		return true;

	return false;
}

static int intel_vrr_hw_vmin(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	return intel_vrr_hw_value(crtc_state, crtc_state->vrr.vmin) -
		intel_vrr_vmin_flipline_offset(display);
}

static int intel_vrr_hw_vmax(const struct intel_crtc_state *crtc_state)
{
	return intel_vrr_hw_value(crtc_state, crtc_state->vrr.vmax);
}

static int intel_vrr_hw_flipline(const struct intel_crtc_state *crtc_state)
{
	return intel_vrr_hw_value(crtc_state, crtc_state->vrr.flipline);
}

static void intel_vrr_set_vrr_timings(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	intel_de_write(display, TRANS_VRR_VMIN(display, cpu_transcoder),
		       intel_vrr_hw_vmin(crtc_state) - 1);
	intel_de_write(display, TRANS_VRR_VMAX(display, cpu_transcoder),
		       intel_vrr_hw_vmax(crtc_state) - 1);
	intel_de_write(display, TRANS_VRR_FLIPLINE(display, cpu_transcoder),
		       intel_vrr_hw_flipline(crtc_state) - 1);
}

static void
intel_vrr_enable_dc_balancing(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum pipe pipe = crtc->pipe;
	u32 vrr_ctl = intel_de_read(display, TRANS_VRR_CTL(display, cpu_transcoder));

	if (!crtc_state->vrr.dc_balance.enable)
		return;

	intel_de_write(display, TRANS_VRR_DCB_ADJ_VMAX_CFG(cpu_transcoder),
		       VRR_DCB_ADJ_VMAX(crtc_state->vrr.vmax - 1));
	intel_de_write(display, TRANS_VRR_DCB_ADJ_VMAX_CFG_LIVE(cpu_transcoder),
		       VRR_DCB_ADJ_VMAX(crtc_state->vrr.vmax - 1));
	intel_de_write(display, TRANS_VRR_DCB_VMAX(cpu_transcoder),
		       VRR_DCB_VMAX(crtc_state->vrr.vmax - 1));
	intel_de_write(display, TRANS_VRR_DCB_VMAX_LIVE(cpu_transcoder),
		       VRR_DCB_VMAX(crtc_state->vrr.vmax - 1));
	intel_de_write(display, TRANS_VRR_DCB_FLIPLINE(cpu_transcoder),
		       VRR_DCB_FLIPLINE(crtc_state->vrr.flipline - 1));
	intel_de_write(display, TRANS_VRR_DCB_FLIPLINE_LIVE(cpu_transcoder),
		       VRR_DCB_FLIPLINE(crtc_state->vrr.flipline - 1));
	intel_de_write(display, TRANS_VRR_DCB_ADJ_FLIPLINE_CFG_LIVE(cpu_transcoder),
		       VRR_DCB_ADJ_FLIPLINE(crtc_state->vrr.flipline - 1));
	intel_de_write(display, TRANS_VRR_DCB_ADJ_FLIPLINE_CFG(cpu_transcoder),
		       VRR_DCB_ADJ_FLIPLINE(crtc_state->vrr.flipline - 1));
	intel_de_write(display, PIPEDMC_DCB_VMIN(pipe),
		       crtc_state->vrr.dc_balance.vmin - 1);
	intel_de_write(display, PIPEDMC_DCB_VMAX(pipe),
		       crtc_state->vrr.dc_balance.vmax - 1);
	intel_de_write(display, PIPEDMC_DCB_MAX_INCREASE(pipe),
		       crtc_state->vrr.dc_balance.max_increase);
	intel_de_write(display, PIPEDMC_DCB_MAX_DECREASE(pipe),
		       crtc_state->vrr.dc_balance.max_decrease);
	intel_de_write(display, PIPEDMC_DCB_GUARDBAND(pipe),
		       crtc_state->vrr.dc_balance.guardband);
	intel_de_write(display, PIPEDMC_DCB_SLOPE(pipe),
		       crtc_state->vrr.dc_balance.slope);
	intel_de_write(display, PIPEDMC_DCB_VBLANK(pipe),
		       crtc_state->vrr.dc_balance.vblank_target);
	intel_dmc_configure_dc_balance_event(display, pipe, true);
	intel_de_write(display, TRANS_ADAPTIVE_SYNC_DCB_CTL(cpu_transcoder),
		       ADAPTIVE_SYNC_COUNTER_EN);
	intel_pipedmc_dcb_enable(NULL, crtc);

	vrr_ctl |= VRR_CTL_DCB_ADJ_ENABLE;
	intel_de_write(display, TRANS_VRR_CTL(display, cpu_transcoder), vrr_ctl);
}

static void
intel_vrr_disable_dc_balancing(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_display *display = to_intel_display(old_crtc_state);
	enum transcoder cpu_transcoder = old_crtc_state->cpu_transcoder;
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	enum pipe pipe = crtc->pipe;
	u32 vrr_ctl = intel_de_read(display, TRANS_VRR_CTL(display, cpu_transcoder));

	if (!old_crtc_state->vrr.dc_balance.enable)
		return;

	intel_pipedmc_dcb_disable(NULL, crtc);
	intel_dmc_configure_dc_balance_event(display, pipe, false);
	intel_de_write(display, TRANS_ADAPTIVE_SYNC_DCB_CTL(cpu_transcoder), 0);
	intel_de_write(display, PIPEDMC_DCB_VMIN(pipe), 0);
	intel_de_write(display, PIPEDMC_DCB_VMAX(pipe), 0);
	intel_de_write(display, PIPEDMC_DCB_MAX_INCREASE(pipe), 0);
	intel_de_write(display, PIPEDMC_DCB_MAX_DECREASE(pipe), 0);
	intel_de_write(display, PIPEDMC_DCB_GUARDBAND(pipe), 0);
	intel_de_write(display, PIPEDMC_DCB_SLOPE(pipe), 0);
	intel_de_write(display, PIPEDMC_DCB_VBLANK(pipe), 0);
	intel_de_write(display, TRANS_VRR_DCB_ADJ_VMAX_CFG_LIVE(cpu_transcoder), 0);
	intel_de_write(display, TRANS_VRR_DCB_ADJ_FLIPLINE_CFG_LIVE(cpu_transcoder), 0);
	intel_de_write(display, TRANS_VRR_DCB_VMAX_LIVE(cpu_transcoder), 0);
	intel_de_write(display, TRANS_VRR_DCB_FLIPLINE_LIVE(cpu_transcoder), 0);
	intel_de_write(display, TRANS_VRR_DCB_ADJ_VMAX_CFG(cpu_transcoder), 0);
	intel_de_write(display, TRANS_VRR_DCB_ADJ_FLIPLINE_CFG(cpu_transcoder), 0);
	intel_de_write(display, TRANS_VRR_DCB_VMAX(cpu_transcoder), 0);
	intel_de_write(display, TRANS_VRR_DCB_FLIPLINE(cpu_transcoder), 0);

	vrr_ctl &= ~VRR_CTL_DCB_ADJ_ENABLE;
	intel_de_write(display, TRANS_VRR_CTL(display, cpu_transcoder), vrr_ctl);
}

static void intel_vrr_tg_enable(const struct intel_crtc_state *crtc_state,
				bool cmrr_enable)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 vrr_ctl;

	intel_de_write(display, TRANS_PUSH(display, cpu_transcoder), TRANS_PUSH_EN);

	vrr_ctl = VRR_CTL_VRR_ENABLE | trans_vrr_ctl(crtc_state);

	/*
	 * FIXME this might be broken as bspec seems to imply that
	 * even VRR_CTL_CMRR_ENABLE is armed by TRANS_CMRR_N_HI
	 * when enabling CMRR (but not when disabling CMRR?).
	 */
	if (cmrr_enable)
		vrr_ctl |= VRR_CTL_CMRR_ENABLE;

	intel_de_write(display, TRANS_VRR_CTL(display, cpu_transcoder), vrr_ctl);
}

static void intel_vrr_tg_disable(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_display *display = to_intel_display(old_crtc_state);
	enum transcoder cpu_transcoder = old_crtc_state->cpu_transcoder;

	intel_de_write(display, TRANS_VRR_CTL(display, cpu_transcoder),
		       trans_vrr_ctl(old_crtc_state));

	if (intel_de_wait_for_clear_ms(display,
				       TRANS_VRR_STATUS(display, cpu_transcoder),
				       VRR_STATUS_VRR_EN_LIVE, 1000))
		drm_err(display->drm, "Timed out waiting for VRR live status to clear\n");

	intel_de_write(display, TRANS_PUSH(display, cpu_transcoder), 0);
}

void intel_vrr_enable(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	if (!crtc_state->vrr.enable)
		return;

	intel_vrr_set_vrr_timings(crtc_state);
	intel_vrr_enable_dc_balancing(crtc_state);

	if (!intel_vrr_always_use_vrr_tg(display))
		intel_vrr_tg_enable(crtc_state, crtc_state->cmrr.enable);
}

void intel_vrr_disable(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_display *display = to_intel_display(old_crtc_state);

	if (!old_crtc_state->vrr.enable)
		return;

	if (!intel_vrr_always_use_vrr_tg(display))
		intel_vrr_tg_disable(old_crtc_state);

	intel_vrr_disable_dc_balancing(old_crtc_state);
	intel_vrr_set_fixed_rr_timings(old_crtc_state);
}

void intel_vrr_transcoder_enable(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	if (!intel_vrr_possible(crtc_state))
		return;

	if (intel_vrr_always_use_vrr_tg(display))
		intel_vrr_tg_enable(crtc_state, false);
}

void intel_vrr_transcoder_disable(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_display *display = to_intel_display(old_crtc_state);

	if (!intel_vrr_possible(old_crtc_state))
		return;

	if (intel_vrr_always_use_vrr_tg(display))
		intel_vrr_tg_disable(old_crtc_state);
}

bool intel_vrr_is_fixed_rr(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->vrr.flipline &&
	       crtc_state->vrr.flipline == crtc_state->vrr.vmax &&
	       crtc_state->vrr.flipline == crtc_state->vrr.vmin;
}

static
void intel_vrr_get_dc_balance_config(struct intel_crtc_state *crtc_state)
{
	u32 reg_val;
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum pipe pipe = crtc->pipe;

	if (!intel_vrr_dc_balance_possible(crtc_state))
		return;

	reg_val = intel_de_read(display, PIPEDMC_DCB_VMIN(pipe));
	crtc_state->vrr.dc_balance.vmin = reg_val ? reg_val + 1 : 0;

	reg_val = intel_de_read(display, PIPEDMC_DCB_VMAX(pipe));
	crtc_state->vrr.dc_balance.vmax = reg_val ? reg_val + 1 : 0;

	crtc_state->vrr.dc_balance.guardband =
		intel_de_read(display, PIPEDMC_DCB_GUARDBAND(pipe));
	crtc_state->vrr.dc_balance.max_increase =
		intel_de_read(display, PIPEDMC_DCB_MAX_INCREASE(pipe));
	crtc_state->vrr.dc_balance.max_decrease =
		intel_de_read(display, PIPEDMC_DCB_MAX_DECREASE(pipe));
	crtc_state->vrr.dc_balance.slope =
		intel_de_read(display, PIPEDMC_DCB_SLOPE(pipe));
	crtc_state->vrr.dc_balance.vblank_target =
		intel_de_read(display, PIPEDMC_DCB_VBLANK(pipe));
}

void intel_vrr_get_config(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 trans_vrr_ctl, trans_vrr_vsync;
	bool vrr_enable;

	trans_vrr_ctl = intel_de_read(display,
				      TRANS_VRR_CTL(display, cpu_transcoder));

	if (HAS_CMRR(display))
		crtc_state->cmrr.enable = (trans_vrr_ctl & VRR_CTL_CMRR_ENABLE);

	if (crtc_state->cmrr.enable) {
		crtc_state->cmrr.cmrr_n =
			intel_de_read64_2x32(display, TRANS_CMRR_N_LO(display, cpu_transcoder),
					     TRANS_CMRR_N_HI(display, cpu_transcoder));
		crtc_state->cmrr.cmrr_m =
			intel_de_read64_2x32(display, TRANS_CMRR_M_LO(display, cpu_transcoder),
					     TRANS_CMRR_M_HI(display, cpu_transcoder));
	}

	if (DISPLAY_VER(display) >= 13) {
		crtc_state->vrr.guardband =
			REG_FIELD_GET(XELPD_VRR_CTL_VRR_GUARDBAND_MASK, trans_vrr_ctl);
	} else {
		if (trans_vrr_ctl & VRR_CTL_PIPELINE_FULL_OVERRIDE) {
			crtc_state->vrr.pipeline_full =
				REG_FIELD_GET(VRR_CTL_PIPELINE_FULL_MASK, trans_vrr_ctl);

			crtc_state->vrr.guardband =
				intel_vrr_pipeline_full_to_guardband(crtc_state,
								     crtc_state->vrr.pipeline_full);
		}
	}

	if (trans_vrr_ctl & VRR_CTL_FLIP_LINE_EN) {
		crtc_state->vrr.flipline = intel_de_read(display,
							 TRANS_VRR_FLIPLINE(display, cpu_transcoder)) + 1;
		crtc_state->vrr.vmax = intel_de_read(display,
						     TRANS_VRR_VMAX(display, cpu_transcoder)) + 1;
		crtc_state->vrr.vmin = intel_de_read(display,
						     TRANS_VRR_VMIN(display, cpu_transcoder)) + 1;

		if (DISPLAY_VER(display) < 13) {
			/* undo what intel_vrr_hw_value() does when writing the values */
			crtc_state->vrr.flipline += crtc_state->set_context_latency;
			crtc_state->vrr.vmax += crtc_state->set_context_latency;
			crtc_state->vrr.vmin += crtc_state->set_context_latency;

			crtc_state->vrr.vmin += intel_vrr_vmin_flipline_offset(display);
		}

		/*
		 * For platforms that always use VRR Timing Generator, the VTOTAL.Vtotal
		 * bits are not filled. Since for these platforms TRAN_VMIN is always
		 * filled with crtc_vtotal, use TRAN_VRR_VMIN to get the vtotal for
		 * adjusted_mode.
		 */
		if (intel_vrr_always_use_vrr_tg(display))
			crtc_state->hw.adjusted_mode.crtc_vtotal =
				intel_vrr_vmin_vtotal(crtc_state);

		if (HAS_AS_SDP(display)) {
			trans_vrr_vsync =
				intel_de_read(display,
					      TRANS_VRR_VSYNC(display, cpu_transcoder));
			crtc_state->vrr.vsync_start =
				REG_FIELD_GET(VRR_VSYNC_START_MASK, trans_vrr_vsync);
			crtc_state->vrr.vsync_end =
				REG_FIELD_GET(VRR_VSYNC_END_MASK, trans_vrr_vsync);
		}
	}

	vrr_enable = trans_vrr_ctl & VRR_CTL_VRR_ENABLE;

	if (intel_vrr_always_use_vrr_tg(display))
		crtc_state->vrr.enable = vrr_enable && !intel_vrr_is_fixed_rr(crtc_state);
	else
		crtc_state->vrr.enable = vrr_enable;

	intel_vrr_get_dc_balance_config(crtc_state);

	/*
	 * #TODO: For Both VRR and CMRR the flag I915_MODE_FLAG_VRR is set for mode_flags.
	 * Since CMRR is currently disabled, set this flag for VRR for now.
	 * Need to keep this in mind while re-enabling CMRR.
	 */
	if (crtc_state->vrr.enable)
		crtc_state->mode_flags |= I915_MODE_FLAG_VRR;

	/*
	 * For platforms that always use the VRR timing generator, we overwrite
	 * crtc_vblank_start with vtotal - guardband to reflect the delayed
	 * vblank start. This works for both default and optimized guardband values.
	 * On other platforms, we keep the original value from
	 * intel_get_transcoder_timings() and apply adjustments only in VRR-specific
	 * paths as needed.
	 */
	if (intel_vrr_always_use_vrr_tg(display))
		crtc_state->hw.adjusted_mode.crtc_vblank_start =
			crtc_state->hw.adjusted_mode.crtc_vtotal -
			crtc_state->vrr.guardband;
}

int intel_vrr_safe_window_start(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	if (DISPLAY_VER(display) >= 30)
		return crtc_state->hw.adjusted_mode.crtc_vdisplay -
		       crtc_state->set_context_latency;
	else
		return crtc_state->hw.adjusted_mode.crtc_vdisplay;
}

static int
intel_vrr_dcb_vmin_vblank_start(const struct intel_crtc_state *crtc_state)
{
	return (intel_vrr_dcb_vmin_vblank_start_next(crtc_state) < 0) ?
		intel_vrr_dcb_vmin_vblank_start_final(crtc_state) :
		intel_vrr_dcb_vmin_vblank_start_next(crtc_state);
}

int intel_vrr_vmin_safe_window_end(const struct intel_crtc_state *crtc_state)
{
	int vmin_vblank_start = crtc_state->vrr.dc_balance.enable ?
			intel_vrr_dcb_vmin_vblank_start(crtc_state) :
			intel_vrr_vmin_vblank_start(crtc_state);

	return vmin_vblank_start - crtc_state->set_context_latency;
}

int intel_vrr_dcb_vmin_vblank_start_next(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 tmp = 0;

	tmp = intel_de_read(display, TRANS_VRR_DCB_ADJ_FLIPLINE_CFG_LIVE(cpu_transcoder));

	if (REG_FIELD_GET(VRR_DCB_ADJ_FLIPLINE_CNT_MASK, tmp) == 0)
		return -EINVAL;

	return intel_vrr_vblank_start(crtc_state, VRR_DCB_ADJ_FLIPLINE(tmp) + 1);
}

int intel_vrr_dcb_vmax_vblank_start_next(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 tmp = 0;

	tmp = intel_de_read(display, TRANS_VRR_DCB_ADJ_VMAX_CFG_LIVE(cpu_transcoder));

	if (REG_FIELD_GET(VRR_DCB_ADJ_VMAX_CNT_MASK, tmp) == 0)
		return -EINVAL;

	return intel_vrr_vblank_start(crtc_state, VRR_DCB_ADJ_VMAX(tmp) + 1);
}

int intel_vrr_dcb_vmin_vblank_start_final(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 tmp = 0;

	tmp = intel_de_read(display, TRANS_VRR_DCB_FLIPLINE_LIVE(cpu_transcoder));

	return intel_vrr_vblank_start(crtc_state, VRR_DCB_FLIPLINE(tmp) + 1);
}

int intel_vrr_dcb_vmax_vblank_start_final(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 tmp = 0;

	tmp = intel_de_read(display, TRANS_VRR_DCB_VMAX_LIVE(cpu_transcoder));

	return intel_vrr_vblank_start(crtc_state, VRR_DCB_VMAX(tmp) + 1);
}
