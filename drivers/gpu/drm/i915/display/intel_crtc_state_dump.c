// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_crtc_state_dump.h"
#include "intel_display_types.h"
#include "intel_hdmi.h"
#include "intel_vrr.h"

static void intel_dump_crtc_timings(struct drm_i915_private *i915,
				    const struct drm_display_mode *mode)
{
	drm_dbg_kms(&i915->drm, "crtc timings: %d %d %d %d %d %d %d %d %d, "
		    "type: 0x%x flags: 0x%x\n",
		    mode->crtc_clock,
		    mode->crtc_hdisplay, mode->crtc_hsync_start,
		    mode->crtc_hsync_end, mode->crtc_htotal,
		    mode->crtc_vdisplay, mode->crtc_vsync_start,
		    mode->crtc_vsync_end, mode->crtc_vtotal,
		    mode->type, mode->flags);
}

static void
intel_dump_m_n_config(const struct intel_crtc_state *pipe_config,
		      const char *id, unsigned int lane_count,
		      const struct intel_link_m_n *m_n)
{
	struct drm_i915_private *i915 = to_i915(pipe_config->uapi.crtc->dev);

	drm_dbg_kms(&i915->drm,
		    "%s: lanes: %i; data_m: %u, data_n: %u, link_m: %u, link_n: %u, tu: %u\n",
		    id, lane_count,
		    m_n->data_m, m_n->data_n,
		    m_n->link_m, m_n->link_n, m_n->tu);
}

static void
intel_dump_infoframe(struct drm_i915_private *i915,
		     const union hdmi_infoframe *frame)
{
	if (!drm_debug_enabled(DRM_UT_KMS))
		return;

	hdmi_infoframe_log(KERN_DEBUG, i915->drm.dev, frame);
}

static void
intel_dump_dp_vsc_sdp(struct drm_i915_private *i915,
		      const struct drm_dp_vsc_sdp *vsc)
{
	if (!drm_debug_enabled(DRM_UT_KMS))
		return;

	drm_dp_vsc_sdp_log(KERN_DEBUG, i915->drm.dev, vsc);
}

#define OUTPUT_TYPE(x) [INTEL_OUTPUT_ ## x] = #x

static const char * const output_type_str[] = {
	OUTPUT_TYPE(UNUSED),
	OUTPUT_TYPE(ANALOG),
	OUTPUT_TYPE(DVO),
	OUTPUT_TYPE(SDVO),
	OUTPUT_TYPE(LVDS),
	OUTPUT_TYPE(TVOUT),
	OUTPUT_TYPE(HDMI),
	OUTPUT_TYPE(DP),
	OUTPUT_TYPE(EDP),
	OUTPUT_TYPE(DSI),
	OUTPUT_TYPE(DDI),
	OUTPUT_TYPE(DP_MST),
};

#undef OUTPUT_TYPE

static void snprintf_output_types(char *buf, size_t len,
				  unsigned int output_types)
{
	char *str = buf;
	int i;

	str[0] = '\0';

	for (i = 0; i < ARRAY_SIZE(output_type_str); i++) {
		int r;

		if ((output_types & BIT(i)) == 0)
			continue;

		r = snprintf(str, len, "%s%s",
			     str != buf ? "," : "", output_type_str[i]);
		if (r >= len)
			break;
		str += r;
		len -= r;

		output_types &= ~BIT(i);
	}

	WARN_ON_ONCE(output_types != 0);
}

static const char * const output_format_str[] = {
	[INTEL_OUTPUT_FORMAT_RGB] = "RGB",
	[INTEL_OUTPUT_FORMAT_YCBCR420] = "YCBCR4:2:0",
	[INTEL_OUTPUT_FORMAT_YCBCR444] = "YCBCR4:4:4",
};

static const char *output_formats(enum intel_output_format format)
{
	if (format >= ARRAY_SIZE(output_format_str))
		return "invalid";
	return output_format_str[format];
}

static void intel_dump_plane_state(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;

	if (!fb) {
		drm_dbg_kms(&i915->drm,
			    "[PLANE:%d:%s] fb: [NOFB], visible: %s\n",
			    plane->base.base.id, plane->base.name,
			    str_yes_no(plane_state->uapi.visible));
		return;
	}

	drm_dbg_kms(&i915->drm,
		    "[PLANE:%d:%s] fb: [FB:%d] %ux%u format = %p4cc modifier = 0x%llx, visible: %s\n",
		    plane->base.base.id, plane->base.name,
		    fb->base.id, fb->width, fb->height, &fb->format->format,
		    fb->modifier, str_yes_no(plane_state->uapi.visible));
	drm_dbg_kms(&i915->drm, "\trotation: 0x%x, scaler: %d\n",
		    plane_state->hw.rotation, plane_state->scaler_id);
	if (plane_state->uapi.visible)
		drm_dbg_kms(&i915->drm,
			    "\tsrc: " DRM_RECT_FP_FMT " dst: " DRM_RECT_FMT "\n",
			    DRM_RECT_FP_ARG(&plane_state->uapi.src),
			    DRM_RECT_ARG(&plane_state->uapi.dst));
}

void intel_crtc_state_dump(const struct intel_crtc_state *pipe_config,
			   struct intel_atomic_state *state,
			   const char *context)
{
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	const struct intel_plane_state *plane_state;
	struct intel_plane *plane;
	char buf[64];
	int i;

	drm_dbg_kms(&i915->drm, "[CRTC:%d:%s] enable: %s [%s]\n",
		    crtc->base.base.id, crtc->base.name,
		    str_yes_no(pipe_config->hw.enable), context);

	if (!pipe_config->hw.enable)
		goto dump_planes;

	snprintf_output_types(buf, sizeof(buf), pipe_config->output_types);
	drm_dbg_kms(&i915->drm,
		    "active: %s, output_types: %s (0x%x), output format: %s\n",
		    str_yes_no(pipe_config->hw.active),
		    buf, pipe_config->output_types,
		    output_formats(pipe_config->output_format));

	drm_dbg_kms(&i915->drm,
		    "cpu_transcoder: %s, pipe bpp: %i, dithering: %i\n",
		    transcoder_name(pipe_config->cpu_transcoder),
		    pipe_config->pipe_bpp, pipe_config->dither);

	drm_dbg_kms(&i915->drm, "MST master transcoder: %s\n",
		    transcoder_name(pipe_config->mst_master_transcoder));

	drm_dbg_kms(&i915->drm,
		    "port sync: master transcoder: %s, slave transcoder bitmask = 0x%x\n",
		    transcoder_name(pipe_config->master_transcoder),
		    pipe_config->sync_mode_slaves_mask);

	drm_dbg_kms(&i915->drm, "bigjoiner: %s, pipes: 0x%x\n",
		    intel_crtc_is_bigjoiner_slave(pipe_config) ? "slave" :
		    intel_crtc_is_bigjoiner_master(pipe_config) ? "master" : "no",
		    pipe_config->bigjoiner_pipes);

	drm_dbg_kms(&i915->drm, "splitter: %s, link count %d, overlap %d\n",
		    str_enabled_disabled(pipe_config->splitter.enable),
		    pipe_config->splitter.link_count,
		    pipe_config->splitter.pixel_overlap);

	if (pipe_config->has_pch_encoder)
		intel_dump_m_n_config(pipe_config, "fdi",
				      pipe_config->fdi_lanes,
				      &pipe_config->fdi_m_n);

	if (intel_crtc_has_dp_encoder(pipe_config)) {
		intel_dump_m_n_config(pipe_config, "dp m_n",
				      pipe_config->lane_count,
				      &pipe_config->dp_m_n);
		intel_dump_m_n_config(pipe_config, "dp m2_n2",
				      pipe_config->lane_count,
				      &pipe_config->dp_m2_n2);
	}

	drm_dbg_kms(&i915->drm, "framestart delay: %d, MSA timing delay: %d\n",
		    pipe_config->framestart_delay, pipe_config->msa_timing_delay);

	drm_dbg_kms(&i915->drm,
		    "audio: %i, infoframes: %i, infoframes enabled: 0x%x\n",
		    pipe_config->has_audio, pipe_config->has_infoframe,
		    pipe_config->infoframes.enable);

	if (pipe_config->infoframes.enable &
	    intel_hdmi_infoframe_enable(HDMI_PACKET_TYPE_GENERAL_CONTROL))
		drm_dbg_kms(&i915->drm, "GCP: 0x%x\n",
			    pipe_config->infoframes.gcp);
	if (pipe_config->infoframes.enable &
	    intel_hdmi_infoframe_enable(HDMI_INFOFRAME_TYPE_AVI))
		intel_dump_infoframe(i915, &pipe_config->infoframes.avi);
	if (pipe_config->infoframes.enable &
	    intel_hdmi_infoframe_enable(HDMI_INFOFRAME_TYPE_SPD))
		intel_dump_infoframe(i915, &pipe_config->infoframes.spd);
	if (pipe_config->infoframes.enable &
	    intel_hdmi_infoframe_enable(HDMI_INFOFRAME_TYPE_VENDOR))
		intel_dump_infoframe(i915, &pipe_config->infoframes.hdmi);
	if (pipe_config->infoframes.enable &
	    intel_hdmi_infoframe_enable(HDMI_INFOFRAME_TYPE_DRM))
		intel_dump_infoframe(i915, &pipe_config->infoframes.drm);
	if (pipe_config->infoframes.enable &
	    intel_hdmi_infoframe_enable(HDMI_PACKET_TYPE_GAMUT_METADATA))
		intel_dump_infoframe(i915, &pipe_config->infoframes.drm);
	if (pipe_config->infoframes.enable &
	    intel_hdmi_infoframe_enable(DP_SDP_VSC))
		intel_dump_dp_vsc_sdp(i915, &pipe_config->infoframes.vsc);

	drm_dbg_kms(&i915->drm, "vrr: %s, vmin: %d, vmax: %d, pipeline full: %d, guardband: %d flipline: %d, vmin vblank: %d, vmax vblank: %d\n",
		    str_yes_no(pipe_config->vrr.enable),
		    pipe_config->vrr.vmin, pipe_config->vrr.vmax,
		    pipe_config->vrr.pipeline_full, pipe_config->vrr.guardband,
		    pipe_config->vrr.flipline,
		    intel_vrr_vmin_vblank_start(pipe_config),
		    intel_vrr_vmax_vblank_start(pipe_config));

	drm_dbg_kms(&i915->drm, "requested mode: " DRM_MODE_FMT "\n",
		    DRM_MODE_ARG(&pipe_config->hw.mode));
	drm_dbg_kms(&i915->drm, "adjusted mode: " DRM_MODE_FMT "\n",
		    DRM_MODE_ARG(&pipe_config->hw.adjusted_mode));
	intel_dump_crtc_timings(i915, &pipe_config->hw.adjusted_mode);
	drm_dbg_kms(&i915->drm, "pipe mode: " DRM_MODE_FMT "\n",
		    DRM_MODE_ARG(&pipe_config->hw.pipe_mode));
	intel_dump_crtc_timings(i915, &pipe_config->hw.pipe_mode);
	drm_dbg_kms(&i915->drm,
		    "port clock: %d, pipe src: " DRM_RECT_FMT ", pixel rate %d\n",
		    pipe_config->port_clock, DRM_RECT_ARG(&pipe_config->pipe_src),
		    pipe_config->pixel_rate);

	drm_dbg_kms(&i915->drm, "linetime: %d, ips linetime: %d\n",
		    pipe_config->linetime, pipe_config->ips_linetime);

	if (DISPLAY_VER(i915) >= 9)
		drm_dbg_kms(&i915->drm,
			    "num_scalers: %d, scaler_users: 0x%x, scaler_id: %d\n",
			    crtc->num_scalers,
			    pipe_config->scaler_state.scaler_users,
			    pipe_config->scaler_state.scaler_id);

	if (HAS_GMCH(i915))
		drm_dbg_kms(&i915->drm,
			    "gmch pfit: control: 0x%08x, ratios: 0x%08x, lvds border: 0x%08x\n",
			    pipe_config->gmch_pfit.control,
			    pipe_config->gmch_pfit.pgm_ratios,
			    pipe_config->gmch_pfit.lvds_border_bits);
	else
		drm_dbg_kms(&i915->drm,
			    "pch pfit: " DRM_RECT_FMT ", %s, force thru: %s\n",
			    DRM_RECT_ARG(&pipe_config->pch_pfit.dst),
			    str_enabled_disabled(pipe_config->pch_pfit.enabled),
			    str_yes_no(pipe_config->pch_pfit.force_thru));

	drm_dbg_kms(&i915->drm, "ips: %i, double wide: %i, drrs: %i\n",
		    pipe_config->ips_enabled, pipe_config->double_wide,
		    pipe_config->has_drrs);

	intel_dpll_dump_hw_state(i915, &pipe_config->dpll_hw_state);

	if (IS_CHERRYVIEW(i915))
		drm_dbg_kms(&i915->drm,
			    "cgm_mode: 0x%x gamma_mode: 0x%x gamma_enable: %d csc_enable: %d\n",
			    pipe_config->cgm_mode, pipe_config->gamma_mode,
			    pipe_config->gamma_enable, pipe_config->csc_enable);
	else
		drm_dbg_kms(&i915->drm,
			    "csc_mode: 0x%x gamma_mode: 0x%x gamma_enable: %d csc_enable: %d\n",
			    pipe_config->csc_mode, pipe_config->gamma_mode,
			    pipe_config->gamma_enable, pipe_config->csc_enable);

	drm_dbg_kms(&i915->drm, "degamma lut: %d entries, gamma lut: %d entries\n",
		    pipe_config->hw.degamma_lut ?
		    drm_color_lut_size(pipe_config->hw.degamma_lut) : 0,
		    pipe_config->hw.gamma_lut ?
		    drm_color_lut_size(pipe_config->hw.gamma_lut) : 0);

dump_planes:
	if (!state)
		return;

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		if (plane->pipe == crtc->pipe)
			intel_dump_plane_state(plane_state);
	}
}
