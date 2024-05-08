// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 *
 */

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_vrr.h"
#include "intel_dp.h"

bool intel_vrr_is_capable(struct intel_connector *connector)
{
	const struct drm_display_info *info = &connector->base.display_info;
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
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
		intel_dp = intel_attached_dp(connector);

		if (!drm_dp_sink_can_do_video_without_timing_msa(intel_dp->dpcd))
			return false;

		break;
	default:
		return false;
	}

	return HAS_VRR(i915) &&
		info->monitor_range.max_vfreq - info->monitor_range.min_vfreq > 10;
}

bool intel_vrr_is_in_range(struct intel_connector *connector, int vrefresh)
{
	const struct drm_display_info *info = &connector->base.display_info;

	return intel_vrr_is_capable(connector) &&
		vrefresh >= info->monitor_range.min_vfreq &&
		vrefresh <= info->monitor_range.max_vfreq;
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
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	if (DISPLAY_VER(i915) >= 13)
		return crtc_state->vrr.guardband;
	else
		/* The hw imposes the extra scanline before frame start */
		return crtc_state->vrr.pipeline_full + crtc_state->framestart_delay + 1;
}

int intel_vrr_vmin_vblank_start(const struct intel_crtc_state *crtc_state)
{
	/* Min vblank actually determined by flipline that is always >=vmin+1 */
	return crtc_state->vrr.vmin + 1 - intel_vrr_vblank_exit_length(crtc_state);
}

int intel_vrr_vmax_vblank_start(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->vrr.vmax - intel_vrr_vblank_exit_length(crtc_state);
}

void
intel_vrr_compute_config(struct intel_crtc_state *crtc_state,
			 struct drm_connector_state *conn_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	const struct drm_display_info *info = &connector->base.display_info;
	int vmin, vmax;

	/*
	 * FIXME all joined pipes share the same transcoder.
	 * Need to account for that during VRR toggle/push/etc.
	 */
	if (crtc_state->bigjoiner_pipes)
		return;

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
		return;

	crtc_state->vrr.in_range =
		intel_vrr_is_in_range(connector, drm_mode_vrefresh(adjusted_mode));
	if (!crtc_state->vrr.in_range)
		return;

	if (HAS_LRR(i915))
		crtc_state->update_lrr = true;

	vmin = DIV_ROUND_UP(adjusted_mode->crtc_clock * 1000,
			    adjusted_mode->crtc_htotal * info->monitor_range.max_vfreq);
	vmax = adjusted_mode->crtc_clock * 1000 /
		(adjusted_mode->crtc_htotal * info->monitor_range.min_vfreq);

	vmin = max_t(int, vmin, adjusted_mode->crtc_vtotal);
	vmax = max_t(int, vmax, adjusted_mode->crtc_vtotal);

	if (vmin >= vmax)
		return;

	/*
	 * flipline determines the min vblank length the hardware will
	 * generate, and flipline>=vmin+1, hence we reduce vmin by one
	 * to make sure we can get the actual min vblank length.
	 */
	crtc_state->vrr.vmin = vmin - 1;
	crtc_state->vrr.vmax = vmax;

	crtc_state->vrr.flipline = crtc_state->vrr.vmin + 1;

	/*
	 * For XE_LPD+, we use guardband and pipeline override
	 * is deprecated.
	 */
	if (DISPLAY_VER(i915) >= 13) {
		crtc_state->vrr.guardband =
			crtc_state->vrr.vmin + 1 - adjusted_mode->crtc_vblank_start;
	} else {
		crtc_state->vrr.pipeline_full =
			min(255, crtc_state->vrr.vmin - adjusted_mode->crtc_vblank_start -
			    crtc_state->framestart_delay - 1);
	}

	if (crtc_state->uapi.vrr_enabled) {
		crtc_state->vrr.enable = true;
		crtc_state->mode_flags |= I915_MODE_FLAG_VRR;
		if (intel_dp_as_sdp_supported(intel_dp)) {
			crtc_state->vrr.vsync_start =
				(crtc_state->hw.adjusted_mode.crtc_vtotal -
					crtc_state->hw.adjusted_mode.vsync_start);
			crtc_state->vrr.vsync_end =
				(crtc_state->hw.adjusted_mode.crtc_vtotal -
					crtc_state->hw.adjusted_mode.vsync_end);
		}
	}
}

static u32 trans_vrr_ctl(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (DISPLAY_VER(i915) >= 13)
		return VRR_CTL_IGN_MAX_SHIFT | VRR_CTL_FLIP_LINE_EN |
			XELPD_VRR_CTL_VRR_GUARDBAND(crtc_state->vrr.guardband);
	else
		return VRR_CTL_IGN_MAX_SHIFT | VRR_CTL_FLIP_LINE_EN |
			VRR_CTL_PIPELINE_FULL(crtc_state->vrr.pipeline_full) |
			VRR_CTL_PIPELINE_FULL_OVERRIDE;
}

void intel_vrr_set_transcoder_timings(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	/*
	 * This bit seems to have two meanings depending on the platform:
	 * TGL: generate VRR "safe window" for DSB vblank waits
	 * ADL/DG2: make TRANS_SET_CONTEXT_LATENCY effective with VRR
	 */
	if (IS_DISPLAY_VER(dev_priv, 12, 13))
		intel_de_rmw(dev_priv, CHICKEN_TRANS(cpu_transcoder),
			     0, PIPE_VBLANK_WITH_DELAY);

	if (!crtc_state->vrr.flipline) {
		intel_de_write(dev_priv,
			       TRANS_VRR_CTL(dev_priv, cpu_transcoder), 0);
		return;
	}

	intel_de_write(dev_priv, TRANS_VRR_VMIN(dev_priv, cpu_transcoder),
		       crtc_state->vrr.vmin - 1);
	intel_de_write(dev_priv, TRANS_VRR_VMAX(dev_priv, cpu_transcoder),
		       crtc_state->vrr.vmax - 1);
	intel_de_write(dev_priv, TRANS_VRR_CTL(dev_priv, cpu_transcoder),
		       trans_vrr_ctl(crtc_state));
	intel_de_write(dev_priv, TRANS_VRR_FLIPLINE(dev_priv, cpu_transcoder),
		       crtc_state->vrr.flipline - 1);
}

void intel_vrr_send_push(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!crtc_state->vrr.enable)
		return;

	intel_de_write(dev_priv, TRANS_PUSH(dev_priv, cpu_transcoder),
		       TRANS_PUSH_EN | TRANS_PUSH_SEND);
}

bool intel_vrr_is_push_sent(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!crtc_state->vrr.enable)
		return false;

	return intel_de_read(dev_priv, TRANS_PUSH(dev_priv, cpu_transcoder)) & TRANS_PUSH_SEND;
}

void intel_vrr_enable(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!crtc_state->vrr.enable)
		return;

	intel_de_write(dev_priv, TRANS_PUSH(dev_priv, cpu_transcoder),
		       TRANS_PUSH_EN);

	if (HAS_AS_SDP(dev_priv))
		intel_de_write(dev_priv,
			       TRANS_VRR_VSYNC(dev_priv, cpu_transcoder),
			       VRR_VSYNC_END(crtc_state->vrr.vsync_end) |
			       VRR_VSYNC_START(crtc_state->vrr.vsync_start));

	intel_de_write(dev_priv, TRANS_VRR_CTL(dev_priv, cpu_transcoder),
		       VRR_CTL_VRR_ENABLE | trans_vrr_ctl(crtc_state));
}

void intel_vrr_disable(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = old_crtc_state->cpu_transcoder;

	if (!old_crtc_state->vrr.enable)
		return;

	intel_de_write(dev_priv, TRANS_VRR_CTL(dev_priv, cpu_transcoder),
		       trans_vrr_ctl(old_crtc_state));
	intel_de_wait_for_clear(dev_priv,
				TRANS_VRR_STATUS(dev_priv, cpu_transcoder),
				VRR_STATUS_VRR_EN_LIVE, 1000);
	intel_de_write(dev_priv, TRANS_PUSH(dev_priv, cpu_transcoder), 0);

	if (HAS_AS_SDP(dev_priv))
		intel_de_write(dev_priv,
			       TRANS_VRR_VSYNC(dev_priv, cpu_transcoder), 0);
}

void intel_vrr_get_config(struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 trans_vrr_ctl, trans_vrr_vsync;

	trans_vrr_ctl = intel_de_read(dev_priv,
				      TRANS_VRR_CTL(dev_priv, cpu_transcoder));

	crtc_state->vrr.enable = trans_vrr_ctl & VRR_CTL_VRR_ENABLE;

	if (DISPLAY_VER(dev_priv) >= 13)
		crtc_state->vrr.guardband =
			REG_FIELD_GET(XELPD_VRR_CTL_VRR_GUARDBAND_MASK, trans_vrr_ctl);
	else
		if (trans_vrr_ctl & VRR_CTL_PIPELINE_FULL_OVERRIDE)
			crtc_state->vrr.pipeline_full =
				REG_FIELD_GET(VRR_CTL_PIPELINE_FULL_MASK, trans_vrr_ctl);

	if (trans_vrr_ctl & VRR_CTL_FLIP_LINE_EN) {
		crtc_state->vrr.flipline = intel_de_read(dev_priv,
							 TRANS_VRR_FLIPLINE(dev_priv, cpu_transcoder)) + 1;
		crtc_state->vrr.vmax = intel_de_read(dev_priv,
						     TRANS_VRR_VMAX(dev_priv, cpu_transcoder)) + 1;
		crtc_state->vrr.vmin = intel_de_read(dev_priv,
						     TRANS_VRR_VMIN(dev_priv, cpu_transcoder)) + 1;
	}

	if (crtc_state->vrr.enable) {
		crtc_state->mode_flags |= I915_MODE_FLAG_VRR;

		if (HAS_AS_SDP(dev_priv)) {
			trans_vrr_vsync =
				intel_de_read(dev_priv,
					      TRANS_VRR_VSYNC(dev_priv, cpu_transcoder));
			crtc_state->vrr.vsync_start =
				REG_FIELD_GET(VRR_VSYNC_START_MASK, trans_vrr_vsync);
			crtc_state->vrr.vsync_end =
				REG_FIELD_GET(VRR_VSYNC_END_MASK, trans_vrr_vsync);
		}
	}
}
