// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <drm/drm_edid.h>
#include <drm/drm_eld.h>
#include <drm/drm_print.h>

#include "intel_crtc_state_dump.h"
#include "intel_display_core.h"
#include "intel_display_types.h"
#include "intel_hdmi.h"
#include "intel_vblank.h"
#include "intel_vdsc.h"
#include "intel_vrr.h"

static void intel_dump_crtc_timings(struct drm_printer *p,
				    const struct drm_display_mode *mode)
{
	drm_printf(p, "crtc timings: clock=%d, "
		   "hd=%d hb=%d-%d hs=%d-%d ht=%d, "
		   "vd=%d vb=%d-%d vs=%d-%d vt=%d, "
		   "flags=0x%x\n",
		   mode->crtc_clock,
		   mode->crtc_hdisplay, mode->crtc_hblank_start, mode->crtc_hblank_end,
		   mode->crtc_hsync_start, mode->crtc_hsync_end, mode->crtc_htotal,
		   mode->crtc_vdisplay, mode->crtc_vblank_start, mode->crtc_vblank_end,
		   mode->crtc_vsync_start, mode->crtc_vsync_end, mode->crtc_vtotal,
		   mode->flags);
}

static void
intel_dump_m_n_config(struct drm_printer *p,
		      const struct intel_crtc_state *pipe_config,
		      const char *id, unsigned int lane_count,
		      const struct intel_link_m_n *m_n)
{
	drm_printf(p, "%s: lanes: %i; data_m: %u, data_n: %u, link_m: %u, link_n: %u, tu: %u\n",
		   id, lane_count,
		   m_n->data_m, m_n->data_n,
		   m_n->link_m, m_n->link_n, m_n->tu);
}

static void
intel_dump_infoframe(struct intel_display *display,
		     const union hdmi_infoframe *frame)
{
	if (!drm_debug_enabled(DRM_UT_KMS))
		return;

	hdmi_infoframe_log(KERN_DEBUG, display->drm->dev, frame);
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

const char *intel_output_format_name(enum intel_output_format format)
{
	if (format >= ARRAY_SIZE(output_format_str))
		return "invalid";
	return output_format_str[format];
}

static void intel_dump_plane_state(struct drm_printer *p,
				   const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	const struct drm_framebuffer *fb = plane_state->hw.fb;

	if (!fb) {
		drm_printf(p, "[PLANE:%d:%s] fb: [NOFB], visible: %s\n",
			   plane->base.base.id, plane->base.name,
			   str_yes_no(plane_state->uapi.visible));
		return;
	}

	drm_printf(p, "[PLANE:%d:%s] fb: [FB:%d] %ux%u format = %p4cc modifier = 0x%llx, visible: %s\n",
		   plane->base.base.id, plane->base.name,
		   fb->base.id, fb->width, fb->height, &fb->format->format,
		   fb->modifier, str_yes_no(plane_state->uapi.visible));
	drm_printf(p, "\trotation: 0x%x, scaler: %d, scaling_filter: %d\n",
		   plane_state->hw.rotation, plane_state->scaler_id, plane_state->hw.scaling_filter);
	if (plane_state->uapi.visible)
		drm_printf(p, "\tsrc: " DRM_RECT_FP_FMT " dst: " DRM_RECT_FMT "\n",
			   DRM_RECT_FP_ARG(&plane_state->uapi.src),
			   DRM_RECT_ARG(&plane_state->uapi.dst));
}

static void
ilk_dump_csc(struct intel_display *display,
	     struct drm_printer *p,
	     const char *name,
	     const struct intel_csc_matrix *csc)
{
	int i;

	drm_printf(p, "%s: pre offsets: 0x%04x 0x%04x 0x%04x\n", name,
		   csc->preoff[0], csc->preoff[1], csc->preoff[2]);

	for (i = 0; i < 3; i++)
		drm_printf(p, "%s: coefficients: 0x%04x 0x%04x 0x%04x\n", name,
			   csc->coeff[3 * i + 0],
			   csc->coeff[3 * i + 1],
			   csc->coeff[3 * i + 2]);

	if (DISPLAY_VER(display) < 7)
		return;

	drm_printf(p, "%s: post offsets: 0x%04x 0x%04x 0x%04x\n", name,
		   csc->postoff[0], csc->postoff[1], csc->postoff[2]);
}

static void
vlv_dump_csc(struct drm_printer *p, const char *name,
	     const struct intel_csc_matrix *csc)
{
	int i;

	for (i = 0; i < 3; i++)
		drm_printf(p, "%s: coefficients: 0x%04x 0x%04x 0x%04x\n", name,
			   csc->coeff[3 * i + 0],
			   csc->coeff[3 * i + 1],
			   csc->coeff[3 * i + 2]);
}

void intel_crtc_state_dump(const struct intel_crtc_state *pipe_config,
			   struct intel_atomic_state *state,
			   const char *context)
{
	struct intel_display *display = to_intel_display(pipe_config);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	const struct intel_plane_state *plane_state;
	struct intel_plane *plane;
	struct drm_printer p;
	char buf[64];
	int i;

	if (!drm_debug_enabled(DRM_UT_KMS))
		return;

	p = drm_dbg_printer(display->drm, DRM_UT_KMS, NULL);

	drm_printf(&p, "[CRTC:%d:%s] enable: %s [%s]\n",
		   crtc->base.base.id, crtc->base.name,
		   str_yes_no(pipe_config->hw.enable), context);

	if (!pipe_config->hw.enable)
		goto dump_planes;

	snprintf_output_types(buf, sizeof(buf), pipe_config->output_types);
	drm_printf(&p, "active: %s, output_types: %s (0x%x), output format: %s, sink format: %s\n",
		   str_yes_no(pipe_config->hw.active),
		   buf, pipe_config->output_types,
		   intel_output_format_name(pipe_config->output_format),
		   intel_output_format_name(pipe_config->sink_format));

	drm_printf(&p, "cpu_transcoder: %s, pipe bpp: %i, dithering: %i\n",
		   transcoder_name(pipe_config->cpu_transcoder),
		   pipe_config->pipe_bpp, pipe_config->dither);

	drm_printf(&p, "MST master transcoder: %s\n",
		   transcoder_name(pipe_config->mst_master_transcoder));

	drm_printf(&p, "port sync: master transcoder: %s, slave transcoder bitmask = 0x%x\n",
		   transcoder_name(pipe_config->master_transcoder),
		   pipe_config->sync_mode_slaves_mask);

	drm_printf(&p, "joiner: %s, pipes: 0x%x\n",
		   intel_crtc_is_joiner_secondary(pipe_config) ? "secondary" :
		   intel_crtc_is_joiner_primary(pipe_config) ? "primary" : "no",
		   pipe_config->joiner_pipes);

	drm_printf(&p, "splitter: %s, link count %d, overlap %d\n",
		   str_enabled_disabled(pipe_config->splitter.enable),
		   pipe_config->splitter.link_count,
		   pipe_config->splitter.pixel_overlap);

	if (pipe_config->has_pch_encoder)
		intel_dump_m_n_config(&p, pipe_config, "fdi",
				      pipe_config->fdi_lanes,
				      &pipe_config->fdi_m_n);

	if (intel_crtc_has_dp_encoder(pipe_config)) {
		intel_dump_m_n_config(&p, pipe_config, "dp m_n",
				      pipe_config->lane_count,
				      &pipe_config->dp_m_n);
		intel_dump_m_n_config(&p, pipe_config, "dp m2_n2",
				      pipe_config->lane_count,
				      &pipe_config->dp_m2_n2);
		drm_printf(&p, "fec: %s, enhanced framing: %s\n",
			   str_enabled_disabled(pipe_config->fec_enable),
			   str_enabled_disabled(pipe_config->enhanced_framing));

		drm_printf(&p, "sdp split: %s\n",
			   str_enabled_disabled(pipe_config->sdp_split_enable));

		drm_printf(&p, "psr: %s, selective update: %s, panel replay: %s, selective fetch: %s\n",
			   str_enabled_disabled(pipe_config->has_psr &&
						!pipe_config->has_panel_replay),
			   str_enabled_disabled(pipe_config->has_sel_update),
			   str_enabled_disabled(pipe_config->has_panel_replay),
			   str_enabled_disabled(pipe_config->enable_psr2_sel_fetch));
		drm_printf(&p, "minimum HBlank: %d\n", pipe_config->min_hblank);
	}

	drm_printf(&p, "audio: %i, infoframes: %i, infoframes enabled: 0x%x\n",
		   pipe_config->has_audio, pipe_config->has_infoframe,
		   pipe_config->infoframes.enable);

	if (pipe_config->infoframes.enable &
	    intel_hdmi_infoframe_enable(HDMI_PACKET_TYPE_GENERAL_CONTROL))
		drm_printf(&p, "GCP: 0x%x\n", pipe_config->infoframes.gcp);
	if (pipe_config->infoframes.enable &
	    intel_hdmi_infoframe_enable(HDMI_INFOFRAME_TYPE_AVI))
		intel_dump_infoframe(display, &pipe_config->infoframes.avi);
	if (pipe_config->infoframes.enable &
	    intel_hdmi_infoframe_enable(HDMI_INFOFRAME_TYPE_SPD))
		intel_dump_infoframe(display, &pipe_config->infoframes.spd);
	if (pipe_config->infoframes.enable &
	    intel_hdmi_infoframe_enable(HDMI_INFOFRAME_TYPE_VENDOR))
		intel_dump_infoframe(display, &pipe_config->infoframes.hdmi);
	if (pipe_config->infoframes.enable &
	    intel_hdmi_infoframe_enable(HDMI_INFOFRAME_TYPE_DRM))
		intel_dump_infoframe(display, &pipe_config->infoframes.drm);
	if (pipe_config->infoframes.enable &
	    intel_hdmi_infoframe_enable(HDMI_PACKET_TYPE_GAMUT_METADATA))
		intel_dump_infoframe(display, &pipe_config->infoframes.drm);
	if (pipe_config->infoframes.enable &
	    intel_hdmi_infoframe_enable(DP_SDP_VSC))
		drm_dp_vsc_sdp_log(&p, &pipe_config->infoframes.vsc);
	if (pipe_config->infoframes.enable &
	    intel_hdmi_infoframe_enable(DP_SDP_ADAPTIVE_SYNC))
		drm_dp_as_sdp_log(&p, &pipe_config->infoframes.as_sdp);

	if (pipe_config->has_audio)
		drm_print_hex_dump(&p, "ELD: ", pipe_config->eld,
				   drm_eld_size(pipe_config->eld));

	drm_printf(&p, "scanline offset: %d\n",
		   intel_crtc_scanline_offset(pipe_config));

	drm_printf(&p, "vblank delay: %d, framestart delay: %d, MSA timing delay: %d\n",
		   pipe_config->hw.adjusted_mode.crtc_vblank_start -
		   pipe_config->hw.adjusted_mode.crtc_vdisplay,
		   pipe_config->framestart_delay, pipe_config->msa_timing_delay);

	drm_printf(&p, "vrr: %s, fixed rr: %s, vmin: %d, vmax: %d, flipline: %d, pipeline full: %d, guardband: %d vsync start: %d, vsync end: %d\n",
		   str_yes_no(pipe_config->vrr.enable),
		   str_yes_no(intel_vrr_is_fixed_rr(pipe_config)),
		   pipe_config->vrr.vmin, pipe_config->vrr.vmax, pipe_config->vrr.flipline,
		   pipe_config->vrr.pipeline_full, pipe_config->vrr.guardband,
		   pipe_config->vrr.vsync_start, pipe_config->vrr.vsync_end);

	drm_printf(&p, "vrr: vmin vblank: %d, vmax vblank: %d, vmin vtotal: %d, vmax vtotal: %d\n",
		   intel_vrr_vmin_vblank_start(pipe_config), intel_vrr_vmax_vblank_start(pipe_config),
		   intel_vrr_vmin_vtotal(pipe_config), intel_vrr_vmax_vtotal(pipe_config));

	drm_printf(&p, "requested mode: " DRM_MODE_FMT "\n",
		   DRM_MODE_ARG(&pipe_config->hw.mode));
	drm_printf(&p, "adjusted mode: " DRM_MODE_FMT "\n",
		   DRM_MODE_ARG(&pipe_config->hw.adjusted_mode));
	intel_dump_crtc_timings(&p, &pipe_config->hw.adjusted_mode);
	drm_printf(&p, "pipe mode: " DRM_MODE_FMT "\n",
		   DRM_MODE_ARG(&pipe_config->hw.pipe_mode));
	intel_dump_crtc_timings(&p, &pipe_config->hw.pipe_mode);
	drm_printf(&p, "port clock: %d, pipe src: " DRM_RECT_FMT ", pixel rate %d\n",
		   pipe_config->port_clock, DRM_RECT_ARG(&pipe_config->pipe_src),
		   pipe_config->pixel_rate);

	drm_printf(&p, "linetime: %d, ips linetime: %d\n",
		   pipe_config->linetime, pipe_config->ips_linetime);

	if (DISPLAY_VER(display) >= 9)
		drm_printf(&p, "num_scalers: %d, scaler_users: 0x%x, scaler_id: %d, scaling_filter: %d\n",
			   crtc->num_scalers,
			   pipe_config->scaler_state.scaler_users,
			   pipe_config->scaler_state.scaler_id,
			   pipe_config->hw.scaling_filter);

	if (HAS_GMCH(display))
		drm_printf(&p, "gmch pfit: control: 0x%08x, ratios: 0x%08x, lvds border: 0x%08x\n",
			   pipe_config->gmch_pfit.control,
			   pipe_config->gmch_pfit.pgm_ratios,
			   pipe_config->gmch_pfit.lvds_border_bits);
	else
		drm_printf(&p, "pch pfit: " DRM_RECT_FMT ", %s, force thru: %s\n",
			   DRM_RECT_ARG(&pipe_config->pch_pfit.dst),
			   str_enabled_disabled(pipe_config->pch_pfit.enabled),
			   str_yes_no(pipe_config->pch_pfit.force_thru));

	drm_printf(&p, "ips: %i, double wide: %i, drrs: %i\n",
		   pipe_config->ips_enabled, pipe_config->double_wide,
		   pipe_config->has_drrs);

	intel_dpll_dump_hw_state(display, &p, &pipe_config->dpll_hw_state);

	if (display->platform.cherryview)
		drm_printf(&p, "cgm_mode: 0x%x gamma_mode: 0x%x gamma_enable: %d csc_enable: %d\n",
			   pipe_config->cgm_mode, pipe_config->gamma_mode,
			   pipe_config->gamma_enable, pipe_config->csc_enable);
	else
		drm_printf(&p, "csc_mode: 0x%x gamma_mode: 0x%x gamma_enable: %d csc_enable: %d\n",
			   pipe_config->csc_mode, pipe_config->gamma_mode,
			   pipe_config->gamma_enable, pipe_config->csc_enable);

	drm_printf(&p, "pre csc lut: %s%d entries, post csc lut: %d entries\n",
		   pipe_config->pre_csc_lut && pipe_config->pre_csc_lut ==
		   display->color.glk_linear_degamma_lut ? "(linear) " : "",
		   pipe_config->pre_csc_lut ?
		   drm_color_lut_size(pipe_config->pre_csc_lut) : 0,
		   pipe_config->post_csc_lut ?
		   drm_color_lut_size(pipe_config->post_csc_lut) : 0);

	if (DISPLAY_VER(display) >= 11)
		ilk_dump_csc(display, &p, "output csc", &pipe_config->output_csc);

	if (!HAS_GMCH(display))
		ilk_dump_csc(display, &p, "pipe csc", &pipe_config->csc);
	else if (display->platform.cherryview)
		vlv_dump_csc(&p, "cgm csc", &pipe_config->csc);
	else if (display->platform.valleyview)
		vlv_dump_csc(&p, "wgc csc", &pipe_config->csc);

	intel_vdsc_state_dump(&p, 0, pipe_config);

dump_planes:
	if (!state)
		return;

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		if (plane->pipe == crtc->pipe)
			intel_dump_plane_state(&p, plane_state);
	}
}
