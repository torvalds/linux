// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 *
 */

#include <drm/drm_print.h>

#include "intel_de.h"
#include "intel_display_regs.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_vrr.h"
#include "intel_vrr_regs.h"

#define FIXED_POINT_PRECISION		100
#define CMRR_PRECISION_TOLERANCE	10

bool intel_vrr_is_capable(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	const struct drm_display_info *info = &connector->base.display_info;
	struct intel_dp *intel_dp;

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

	return HAS_VRR(display) &&
		info->monitor_range.max_vfreq - info->monitor_range.min_vfreq > 10;
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

static int intel_vrr_real_vblank_delay(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->hw.adjusted_mode.crtc_vblank_start -
		crtc_state->hw.adjusted_mode.crtc_vdisplay;
}

static int intel_vrr_extra_vblank_delay(struct intel_display *display)
{
	/*
	 * On ICL/TGL VRR hardware inserts one extra scanline
	 * just after vactive, which pushes the vmin decision
	 * boundary ahead accordingly. We'll include the extra
	 * scanline in our vblank delay estimates to make sure
	 * that we never underestimate how long we have until
	 * the delayed vblank has passed.
	 */
	return DISPLAY_VER(display) < 13 ? 1 : 0;
}

int intel_vrr_vblank_delay(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	return intel_vrr_real_vblank_delay(crtc_state) +
		intel_vrr_extra_vblank_delay(display);
}

static int intel_vrr_flipline_offset(struct intel_display *display)
{
	/* ICL/TGL hardware imposes flipline>=vmin+1 */
	return DISPLAY_VER(display) < 13 ? 1 : 0;
}

static int intel_vrr_vmin_flipline(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	return crtc_state->vrr.vmin + intel_vrr_flipline_offset(display);
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
static int intel_vrr_vblank_exit_length(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	if (DISPLAY_VER(display) >= 13)
		return crtc_state->vrr.guardband;
	else
		/* hardware imposes one extra scanline somewhere */
		return crtc_state->vrr.pipeline_full + crtc_state->framestart_delay + 1;
}

int intel_vrr_vmin_vtotal(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	/* Min vblank actually determined by flipline */
	if (DISPLAY_VER(display) >= 13)
		return intel_vrr_vmin_flipline(crtc_state);
	else
		return intel_vrr_vmin_flipline(crtc_state) +
			intel_vrr_real_vblank_delay(crtc_state);
}

int intel_vrr_vmax_vtotal(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	if (DISPLAY_VER(display) >= 13)
		return crtc_state->vrr.vmax;
	else
		return crtc_state->vrr.vmax +
			intel_vrr_real_vblank_delay(crtc_state);
}

int intel_vrr_vmin_vblank_start(const struct intel_crtc_state *crtc_state)
{
	return intel_vrr_vmin_vtotal(crtc_state) - intel_vrr_vblank_exit_length(crtc_state);
}

int intel_vrr_vmax_vblank_start(const struct intel_crtc_state *crtc_state)
{
	return intel_vrr_vmax_vtotal(crtc_state) - intel_vrr_vblank_exit_length(crtc_state);
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
	crtc_state->cmrr.enable = true;
	/*
	 * TODO: Compute precise target refresh rate to determine
	 * if video_mode_required should be true. Currently set to
	 * false due to uncertainty about the precise target
	 * refresh Rate.
	 */
	crtc_state->vrr.vmax = cmrr_get_vtotal(crtc_state, false);
	crtc_state->vrr.vmin = crtc_state->vrr.vmax;
	crtc_state->vrr.flipline = crtc_state->vrr.vmin;
	crtc_state->mode_flags |= I915_MODE_FLAG_VRR;
}

static
void intel_vrr_compute_vrr_timings(struct intel_crtc_state *crtc_state)
{
	crtc_state->vrr.enable = true;
	crtc_state->mode_flags |= I915_MODE_FLAG_VRR;
}

/*
 * For fixed refresh rate mode Vmin, Vmax and Flipline all are set to
 * Vtotal value.
 */
static
int intel_vrr_fixed_rr_vtotal(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	int crtc_vtotal = crtc_state->hw.adjusted_mode.crtc_vtotal;

	if (DISPLAY_VER(display) >= 13)
		return crtc_vtotal;
	else
		return crtc_vtotal -
			intel_vrr_real_vblank_delay(crtc_state);
}

static
int intel_vrr_fixed_rr_vmax(const struct intel_crtc_state *crtc_state)
{
	return intel_vrr_fixed_rr_vtotal(crtc_state);
}

static
int intel_vrr_fixed_rr_vmin(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	return intel_vrr_fixed_rr_vtotal(crtc_state) -
		intel_vrr_flipline_offset(display);
}

static
int intel_vrr_fixed_rr_flipline(const struct intel_crtc_state *crtc_state)
{
	return intel_vrr_fixed_rr_vtotal(crtc_state);
}

void intel_vrr_set_fixed_rr_timings(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!intel_vrr_possible(crtc_state))
		return;

	intel_de_write(display, TRANS_VRR_VMIN(display, cpu_transcoder),
		       intel_vrr_fixed_rr_vmin(crtc_state) - 1);
	intel_de_write(display, TRANS_VRR_VMAX(display, cpu_transcoder),
		       intel_vrr_fixed_rr_vmax(crtc_state) - 1);
	intel_de_write(display, TRANS_VRR_FLIPLINE(display, cpu_transcoder),
		       intel_vrr_fixed_rr_flipline(crtc_state) - 1);
}

static
void intel_vrr_compute_fixed_rr_timings(struct intel_crtc_state *crtc_state)
{
	/*
	 * For fixed rr,  vmin = vmax = flipline.
	 * vmin is already set to crtc_vtotal set vmax and flipline the same.
	 */
	crtc_state->vrr.vmax = crtc_state->hw.adjusted_mode.crtc_vtotal;
	crtc_state->vrr.flipline = crtc_state->hw.adjusted_mode.crtc_vtotal;
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

	crtc_state->vrr.vmin = vmin;
	crtc_state->vrr.vmax = vmax;

	crtc_state->vrr.flipline = crtc_state->vrr.vmin;

	if (crtc_state->uapi.vrr_enabled && vmin < vmax)
		intel_vrr_compute_vrr_timings(crtc_state);
	else if (is_cmrr_frac_required(crtc_state) && is_edp)
		intel_vrr_compute_cmrr_timings(crtc_state);
	else
		intel_vrr_compute_fixed_rr_timings(crtc_state);

	/*
	 * flipline determines the min vblank length the hardware will
	 * generate, and on ICL/TGL flipline>=vmin+1, hence we reduce
	 * vmin by one to make sure we can get the actual min vblank length.
	 */
	crtc_state->vrr.vmin -= intel_vrr_flipline_offset(display);

	if (HAS_AS_SDP(display)) {
		crtc_state->vrr.vsync_start =
			(crtc_state->hw.adjusted_mode.crtc_vtotal -
			 crtc_state->hw.adjusted_mode.vsync_start);
		crtc_state->vrr.vsync_end =
			(crtc_state->hw.adjusted_mode.crtc_vtotal -
			 crtc_state->hw.adjusted_mode.vsync_end);
	}
}

void intel_vrr_compute_config_late(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	const struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;

	if (!intel_vrr_possible(crtc_state))
		return;

	if (DISPLAY_VER(display) >= 13) {
		crtc_state->vrr.guardband =
			crtc_state->vrr.vmin - adjusted_mode->crtc_vblank_start;
	} else {
		/* hardware imposes one extra scanline somewhere */
		crtc_state->vrr.pipeline_full =
			min(255, crtc_state->vrr.vmin - adjusted_mode->crtc_vblank_start -
			    crtc_state->framestart_delay - 1);

		/*
		 * vmin/vmax/flipline also need to be adjusted by
		 * the vblank delay to maintain correct vtotals.
		 */
		crtc_state->vrr.vmin -= intel_vrr_real_vblank_delay(crtc_state);
		crtc_state->vrr.vmax -= intel_vrr_real_vblank_delay(crtc_state);
		crtc_state->vrr.flipline -= intel_vrr_real_vblank_delay(crtc_state);
	}
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

	if (!intel_vrr_always_use_vrr_tg(display) && !crtc_state->vrr.enable)
		intel_de_write(display, TRANS_VRR_CTL(display, cpu_transcoder),
			       trans_vrr_ctl(crtc_state));

	if (HAS_AS_SDP(display))
		intel_de_write(display,
			       TRANS_VRR_VSYNC(display, cpu_transcoder),
			       VRR_VSYNC_END(crtc_state->vrr.vsync_end) |
			       VRR_VSYNC_START(crtc_state->vrr.vsync_start));
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

static
void intel_vrr_set_db_point_and_transmission_line(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

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

void intel_vrr_enable(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!crtc_state->vrr.enable)
		return;

	intel_de_write(display, TRANS_VRR_VMIN(display, cpu_transcoder),
		       crtc_state->vrr.vmin - 1);
	intel_de_write(display, TRANS_VRR_VMAX(display, cpu_transcoder),
		       crtc_state->vrr.vmax - 1);
	intel_de_write(display, TRANS_VRR_FLIPLINE(display, cpu_transcoder),
		       crtc_state->vrr.flipline - 1);

	intel_de_write(display, TRANS_PUSH(display, cpu_transcoder),
		       TRANS_PUSH_EN);

	if (!intel_vrr_always_use_vrr_tg(display)) {
		intel_vrr_set_db_point_and_transmission_line(crtc_state);

		if (crtc_state->cmrr.enable) {
			intel_de_write(display, TRANS_VRR_CTL(display, cpu_transcoder),
				       VRR_CTL_VRR_ENABLE | VRR_CTL_CMRR_ENABLE |
				       trans_vrr_ctl(crtc_state));
		} else {
			intel_de_write(display, TRANS_VRR_CTL(display, cpu_transcoder),
				       VRR_CTL_VRR_ENABLE | trans_vrr_ctl(crtc_state));
		}
	}
}

void intel_vrr_disable(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_display *display = to_intel_display(old_crtc_state);
	enum transcoder cpu_transcoder = old_crtc_state->cpu_transcoder;

	if (!old_crtc_state->vrr.enable)
		return;

	if (!intel_vrr_always_use_vrr_tg(display)) {
		intel_de_write(display, TRANS_VRR_CTL(display, cpu_transcoder),
			       trans_vrr_ctl(old_crtc_state));
		intel_de_wait_for_clear(display,
					TRANS_VRR_STATUS(display, cpu_transcoder),
					VRR_STATUS_VRR_EN_LIVE, 1000);
		intel_de_write(display, TRANS_PUSH(display, cpu_transcoder), 0);
	}

	intel_vrr_set_fixed_rr_timings(old_crtc_state);
}

void intel_vrr_transcoder_enable(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!HAS_VRR(display))
		return;

	if (!intel_vrr_possible(crtc_state))
		return;

	if (!intel_vrr_always_use_vrr_tg(display)) {
		intel_de_write(display, TRANS_VRR_CTL(display, cpu_transcoder),
			       trans_vrr_ctl(crtc_state));
		return;
	}

	intel_de_write(display, TRANS_PUSH(display, cpu_transcoder),
		       TRANS_PUSH_EN);

	intel_vrr_set_db_point_and_transmission_line(crtc_state);

	intel_de_write(display, TRANS_VRR_CTL(display, cpu_transcoder),
		       VRR_CTL_VRR_ENABLE | trans_vrr_ctl(crtc_state));
}

void intel_vrr_transcoder_disable(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!HAS_VRR(display))
		return;

	if (!intel_vrr_possible(crtc_state))
		return;

	intel_de_write(display, TRANS_VRR_CTL(display, cpu_transcoder), 0);

	intel_de_wait_for_clear(display, TRANS_VRR_STATUS(display, cpu_transcoder),
				VRR_STATUS_VRR_EN_LIVE, 1000);
	intel_de_write(display, TRANS_PUSH(display, cpu_transcoder), 0);
}

bool intel_vrr_is_fixed_rr(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->vrr.flipline &&
	       crtc_state->vrr.flipline == crtc_state->vrr.vmax &&
	       crtc_state->vrr.flipline == intel_vrr_vmin_flipline(crtc_state);
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

	if (DISPLAY_VER(display) >= 13)
		crtc_state->vrr.guardband =
			REG_FIELD_GET(XELPD_VRR_CTL_VRR_GUARDBAND_MASK, trans_vrr_ctl);
	else
		if (trans_vrr_ctl & VRR_CTL_PIPELINE_FULL_OVERRIDE)
			crtc_state->vrr.pipeline_full =
				REG_FIELD_GET(VRR_CTL_PIPELINE_FULL_MASK, trans_vrr_ctl);

	if (trans_vrr_ctl & VRR_CTL_FLIP_LINE_EN) {
		crtc_state->vrr.flipline = intel_de_read(display,
							 TRANS_VRR_FLIPLINE(display, cpu_transcoder)) + 1;
		crtc_state->vrr.vmax = intel_de_read(display,
						     TRANS_VRR_VMAX(display, cpu_transcoder)) + 1;
		crtc_state->vrr.vmin = intel_de_read(display,
						     TRANS_VRR_VMIN(display, cpu_transcoder)) + 1;

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

	/*
	 * #TODO: For Both VRR and CMRR the flag I915_MODE_FLAG_VRR is set for mode_flags.
	 * Since CMRR is currently disabled, set this flag for VRR for now.
	 * Need to keep this in mind while re-enabling CMRR.
	 */
	if (crtc_state->vrr.enable)
		crtc_state->mode_flags |= I915_MODE_FLAG_VRR;
}
