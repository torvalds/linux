// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include "i915_reg.h"
#include "i915_utils.h"
#include "intel_display_core.h"
#include "intel_display_driver.h"
#include "intel_display_types.h"
#include "intel_lvds_regs.h"
#include "intel_pfit.h"

static int intel_pch_pfit_check_dst_window(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	const struct drm_rect *dst = &crtc_state->pch_pfit.dst;
	int width = drm_rect_width(dst);
	int height = drm_rect_height(dst);
	int x = dst->x1;
	int y = dst->y1;

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE &&
	    (y & 1 || height & 1)) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] pfit window (" DRM_RECT_FMT ") misaligned for interlaced output\n",
			    crtc->base.base.id, crtc->base.name, DRM_RECT_ARG(dst));
		return -EINVAL;
	}

	/*
	 * "Restriction : When pipe scaling is enabled, the scaled
	 *  output must equal the pipe active area, so Pipe active
	 *  size = (2 * PF window position) + PF window size."
	 *
	 * The vertical direction seems more forgiving than the
	 * horizontal direction, but still has some issues so
	 * let's follow the same hard rule for both.
	 */
	if (adjusted_mode->crtc_hdisplay != 2 * x + width ||
	    adjusted_mode->crtc_vdisplay != 2 * y + height) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] pfit window (" DRM_RECT_FMT ") not centered\n",
			    crtc->base.base.id, crtc->base.name, DRM_RECT_ARG(dst));
		return -EINVAL;
	}

	/*
	 * "Restriction : The X position must not be programmed
	 *  to be 1 (28:16=0 0000 0000 0001b)."
	 */
	if (x == 1) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] pfit window (" DRM_RECT_FMT ") badly positioned\n",
			    crtc->base.base.id, crtc->base.name, DRM_RECT_ARG(dst));
		return -EINVAL;
	}

	return 0;
}

static int intel_pch_pfit_check_src_size(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	int pipe_src_w = drm_rect_width(&crtc_state->pipe_src);
	int pipe_src_h = drm_rect_height(&crtc_state->pipe_src);
	int max_src_w, max_src_h;

	if (DISPLAY_VER(display) >= 8) {
		max_src_w = 4096;
		max_src_h = 4096;
	} else if (DISPLAY_VER(display) >= 7) {
		/*
		 * PF0 7x5 capable
		 * PF1 3x3 capable (could be switched to 7x5
		 *                  mode on HSW when PF2 unused)
		 * PF2 3x3 capable
		 *
		 * This assumes we use a 1:1 mapping between pipe and PF.
		 */
		max_src_w = crtc->pipe == PIPE_A ? 4096 : 2048;
		max_src_h = 4096;
	} else {
		max_src_w = 4096;
		max_src_h = 4096;
	}

	if (pipe_src_w > max_src_w || pipe_src_h > max_src_h) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] source size (%dx%d) exceeds pfit max (%dx%d)\n",
			    crtc->base.base.id, crtc->base.name,
			    pipe_src_w, pipe_src_h, max_src_w, max_src_h);
		return -EINVAL;
	}

	return 0;
}

static int intel_pch_pfit_check_scaling(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_rect *dst = &crtc_state->pch_pfit.dst;
	int pipe_src_w = drm_rect_width(&crtc_state->pipe_src);
	int pipe_src_h = drm_rect_height(&crtc_state->pipe_src);
	int hscale, vscale, max_scale = 0x12000; /* 1.125 */
	struct drm_rect src;

	drm_rect_init(&src, 0, 0, pipe_src_w << 16, pipe_src_h << 16);

	hscale = drm_rect_calc_hscale(&src, dst, 0, max_scale);
	if (hscale < 0) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] pfit horizontal downscaling (%d->%d) exceeds max (0x%x)\n",
			    crtc->base.base.id, crtc->base.name,
			    pipe_src_w, drm_rect_width(dst),
			    max_scale);
		return hscale;
	}

	vscale = drm_rect_calc_vscale(&src, dst, 0, max_scale);
	if (vscale < 0) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] pfit vertical downscaling (%d->%d) exceeds max (0x%x)\n",
			    crtc->base.base.id, crtc->base.name,
			    pipe_src_h, drm_rect_height(dst),
			    max_scale);
		return vscale;
	}

	return 0;
}

static int intel_pch_pfit_check_timings(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	if (adjusted_mode->crtc_vdisplay < 7) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] vertical active (%d) below minimum (%d) for pfit\n",
			    crtc->base.base.id, crtc->base.name,
			    adjusted_mode->crtc_vdisplay, 7);
		return -EINVAL;
	}

	return 0;
}

static int intel_pch_pfit_check_cloning(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	/*
	 * The panel fitter is in the pipe and thus would affect every
	 * cloned output. The relevant properties (scaling mode, TV
	 * margins) are per-connector so we'd have to make sure each
	 * output sets them up identically. Seems like a very niche use
	 * case so let's just reject cloning entirely when pfit is used.
	 */
	if (crtc_state->uapi.encoder_mask &&
	    !is_power_of_2(crtc_state->uapi.encoder_mask)) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] no pfit when cloning\n",
			    crtc->base.base.id, crtc->base.name);
		return -EINVAL;
	}

	return 0;
}

/* adjusted_mode has been preset to be the panel's fixed mode */
static int pch_panel_fitting(struct intel_crtc_state *crtc_state,
			     const struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	int pipe_src_w = drm_rect_width(&crtc_state->pipe_src);
	int pipe_src_h = drm_rect_height(&crtc_state->pipe_src);
	int ret, x, y, width, height;

	/* Native modes don't need fitting */
	if (adjusted_mode->crtc_hdisplay == pipe_src_w &&
	    adjusted_mode->crtc_vdisplay == pipe_src_h &&
	    crtc_state->output_format != INTEL_OUTPUT_FORMAT_YCBCR420)
		return 0;

	switch (conn_state->scaling_mode) {
	case DRM_MODE_SCALE_CENTER:
		width = pipe_src_w;
		height = pipe_src_h;
		x = (adjusted_mode->crtc_hdisplay - width + 1)/2;
		y = (adjusted_mode->crtc_vdisplay - height + 1)/2;
		break;

	case DRM_MODE_SCALE_ASPECT:
		/* Scale but preserve the aspect ratio */
		{
			u32 scaled_width = adjusted_mode->crtc_hdisplay * pipe_src_h;
			u32 scaled_height = pipe_src_w * adjusted_mode->crtc_vdisplay;

			if (scaled_width > scaled_height) { /* pillar */
				width = scaled_height / pipe_src_h;
				if (width & 1)
					width++;
				x = (adjusted_mode->crtc_hdisplay - width + 1) / 2;
				y = 0;
				height = adjusted_mode->crtc_vdisplay;
			} else if (scaled_width < scaled_height) { /* letter */
				height = scaled_width / pipe_src_w;
				if (height & 1)
					height++;
				y = (adjusted_mode->crtc_vdisplay - height + 1) / 2;
				x = 0;
				width = adjusted_mode->crtc_hdisplay;
			} else {
				x = y = 0;
				width = adjusted_mode->crtc_hdisplay;
				height = adjusted_mode->crtc_vdisplay;
			}
		}
		break;

	case DRM_MODE_SCALE_NONE:
		WARN_ON(adjusted_mode->crtc_hdisplay != pipe_src_w);
		WARN_ON(adjusted_mode->crtc_vdisplay != pipe_src_h);
		fallthrough;
	case DRM_MODE_SCALE_FULLSCREEN:
		x = y = 0;
		width = adjusted_mode->crtc_hdisplay;
		height = adjusted_mode->crtc_vdisplay;
		break;

	default:
		MISSING_CASE(conn_state->scaling_mode);
		return -EINVAL;
	}

	drm_rect_init(&crtc_state->pch_pfit.dst,
		      x, y, width, height);
	crtc_state->pch_pfit.enabled = true;

	/*
	 * SKL+ have unified scalers for pipes/planes so the
	 * checks are done in a single place for all scalers.
	 */
	if (DISPLAY_VER(display) >= 9)
		return 0;

	ret = intel_pch_pfit_check_dst_window(crtc_state);
	if (ret)
		return ret;

	ret = intel_pch_pfit_check_src_size(crtc_state);
	if (ret)
		return ret;

	ret = intel_pch_pfit_check_scaling(crtc_state);
	if (ret)
		return ret;

	ret = intel_pch_pfit_check_timings(crtc_state);
	if (ret)
		return ret;

	ret = intel_pch_pfit_check_cloning(crtc_state);
	if (ret)
		return ret;

	return 0;
}

static void
centre_horizontally(struct drm_display_mode *adjusted_mode,
		    int width)
{
	u32 border, sync_pos, blank_width, sync_width;

	/* keep the hsync and hblank widths constant */
	sync_width = adjusted_mode->crtc_hsync_end - adjusted_mode->crtc_hsync_start;
	blank_width = adjusted_mode->crtc_hblank_end - adjusted_mode->crtc_hblank_start;
	sync_pos = (blank_width - sync_width + 1) / 2;

	border = (adjusted_mode->crtc_hdisplay - width + 1) / 2;
	border += border & 1; /* make the border even */

	adjusted_mode->crtc_hdisplay = width;
	adjusted_mode->crtc_hblank_start = width + border;
	adjusted_mode->crtc_hblank_end = adjusted_mode->crtc_hblank_start + blank_width;

	adjusted_mode->crtc_hsync_start = adjusted_mode->crtc_hblank_start + sync_pos;
	adjusted_mode->crtc_hsync_end = adjusted_mode->crtc_hsync_start + sync_width;
}

static void
centre_vertically(struct drm_display_mode *adjusted_mode,
		  int height)
{
	u32 border, sync_pos, blank_width, sync_width;

	/* keep the vsync and vblank widths constant */
	sync_width = adjusted_mode->crtc_vsync_end - adjusted_mode->crtc_vsync_start;
	blank_width = adjusted_mode->crtc_vblank_end - adjusted_mode->crtc_vblank_start;
	sync_pos = (blank_width - sync_width + 1) / 2;

	border = (adjusted_mode->crtc_vdisplay - height + 1) / 2;

	adjusted_mode->crtc_vdisplay = height;
	adjusted_mode->crtc_vblank_start = height + border;
	adjusted_mode->crtc_vblank_end = adjusted_mode->crtc_vblank_start + blank_width;

	adjusted_mode->crtc_vsync_start = adjusted_mode->crtc_vblank_start + sync_pos;
	adjusted_mode->crtc_vsync_end = adjusted_mode->crtc_vsync_start + sync_width;
}

static u32 panel_fitter_scaling(u32 source, u32 target)
{
	/*
	 * Floating point operation is not supported. So the FACTOR
	 * is defined, which can avoid the floating point computation
	 * when calculating the panel ratio.
	 */
#define ACCURACY 12
#define FACTOR (1 << ACCURACY)
	u32 ratio = source * FACTOR / target;
	return (FACTOR * ratio + FACTOR/2) / FACTOR;
}

static void i965_scale_aspect(struct intel_crtc_state *crtc_state,
			      u32 *pfit_control)
{
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	int pipe_src_w = drm_rect_width(&crtc_state->pipe_src);
	int pipe_src_h = drm_rect_height(&crtc_state->pipe_src);
	u32 scaled_width = adjusted_mode->crtc_hdisplay * pipe_src_h;
	u32 scaled_height = pipe_src_w * adjusted_mode->crtc_vdisplay;

	/* 965+ is easy, it does everything in hw */
	if (scaled_width > scaled_height)
		*pfit_control |= PFIT_ENABLE |
			PFIT_SCALING_PILLAR;
	else if (scaled_width < scaled_height)
		*pfit_control |= PFIT_ENABLE |
			PFIT_SCALING_LETTER;
	else if (adjusted_mode->crtc_hdisplay != pipe_src_w)
		*pfit_control |= PFIT_ENABLE | PFIT_SCALING_AUTO;
}

static void i9xx_scale_aspect(struct intel_crtc_state *crtc_state,
			      u32 *pfit_control, u32 *pfit_pgm_ratios,
			      u32 *border)
{
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	int pipe_src_w = drm_rect_width(&crtc_state->pipe_src);
	int pipe_src_h = drm_rect_height(&crtc_state->pipe_src);
	u32 scaled_width = adjusted_mode->crtc_hdisplay * pipe_src_h;
	u32 scaled_height = pipe_src_w * adjusted_mode->crtc_vdisplay;
	u32 bits;

	/*
	 * For earlier chips we have to calculate the scaling
	 * ratio by hand and program it into the
	 * PFIT_PGM_RATIO register
	 */
	if (scaled_width > scaled_height) { /* pillar */
		centre_horizontally(adjusted_mode,
				    scaled_height / pipe_src_h);

		*border = LVDS_BORDER_ENABLE;
		if (pipe_src_h != adjusted_mode->crtc_vdisplay) {
			bits = panel_fitter_scaling(pipe_src_h,
						    adjusted_mode->crtc_vdisplay);

			*pfit_pgm_ratios |= (PFIT_HORIZ_SCALE(bits) |
					     PFIT_VERT_SCALE(bits));
			*pfit_control |= (PFIT_ENABLE |
					  PFIT_VERT_INTERP_BILINEAR |
					  PFIT_HORIZ_INTERP_BILINEAR);
		}
	} else if (scaled_width < scaled_height) { /* letter */
		centre_vertically(adjusted_mode,
				  scaled_width / pipe_src_w);

		*border = LVDS_BORDER_ENABLE;
		if (pipe_src_w != adjusted_mode->crtc_hdisplay) {
			bits = panel_fitter_scaling(pipe_src_w,
						    adjusted_mode->crtc_hdisplay);

			*pfit_pgm_ratios |= (PFIT_HORIZ_SCALE(bits) |
					     PFIT_VERT_SCALE(bits));
			*pfit_control |= (PFIT_ENABLE |
					  PFIT_VERT_INTERP_BILINEAR |
					  PFIT_HORIZ_INTERP_BILINEAR);
		}
	} else {
		/* Aspects match, Let hw scale both directions */
		*pfit_control |= (PFIT_ENABLE |
				  PFIT_VERT_AUTO_SCALE |
				  PFIT_HORIZ_AUTO_SCALE |
				  PFIT_VERT_INTERP_BILINEAR |
				  PFIT_HORIZ_INTERP_BILINEAR);
	}
}

static int intel_gmch_pfit_check_timings(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	int min;

	if (DISPLAY_VER(display) >= 4)
		min = 3;
	else
		min = 2;

	if (adjusted_mode->crtc_hdisplay < min) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] horizontal active (%d) below minimum (%d) for pfit\n",
			    crtc->base.base.id, crtc->base.name,
			    adjusted_mode->crtc_hdisplay, min);
		return -EINVAL;
	}

	if (adjusted_mode->crtc_vdisplay < min) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] vertical active (%d) below minimum (%d) for pfit\n",
			    crtc->base.base.id, crtc->base.name,
			    adjusted_mode->crtc_vdisplay, min);
		return -EINVAL;
	}

	return 0;
}

static int gmch_panel_fitting(struct intel_crtc_state *crtc_state,
			      const struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	u32 pfit_control = 0, pfit_pgm_ratios = 0, border = 0;
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	int pipe_src_w = drm_rect_width(&crtc_state->pipe_src);
	int pipe_src_h = drm_rect_height(&crtc_state->pipe_src);

	/* Native modes don't need fitting */
	if (adjusted_mode->crtc_hdisplay == pipe_src_w &&
	    adjusted_mode->crtc_vdisplay == pipe_src_h)
		goto out;

	/*
	 * TODO: implement downscaling for i965+. Need to account
	 * for downscaling in intel_crtc_compute_pixel_rate().
	 */
	if (adjusted_mode->crtc_hdisplay < pipe_src_w) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] pfit horizontal downscaling (%d->%d) not supported\n",
			    crtc->base.base.id, crtc->base.name,
			    pipe_src_w, adjusted_mode->crtc_hdisplay);
		return -EINVAL;
	}
	if (adjusted_mode->crtc_vdisplay < pipe_src_h) {
		drm_dbg_kms(display->drm,
			    "[CRTC:%d:%s] pfit vertical downscaling (%d->%d) not supported\n",
			    crtc->base.base.id, crtc->base.name,
			    pipe_src_h, adjusted_mode->crtc_vdisplay);
		return -EINVAL;
	}

	switch (conn_state->scaling_mode) {
	case DRM_MODE_SCALE_CENTER:
		/*
		 * For centered modes, we have to calculate border widths &
		 * heights and modify the values programmed into the CRTC.
		 */
		centre_horizontally(adjusted_mode, pipe_src_w);
		centre_vertically(adjusted_mode, pipe_src_h);
		border = LVDS_BORDER_ENABLE;
		break;
	case DRM_MODE_SCALE_ASPECT:
		/* Scale but preserve the aspect ratio */
		if (DISPLAY_VER(display) >= 4)
			i965_scale_aspect(crtc_state, &pfit_control);
		else
			i9xx_scale_aspect(crtc_state, &pfit_control,
					  &pfit_pgm_ratios, &border);
		break;
	case DRM_MODE_SCALE_FULLSCREEN:
		/*
		 * Full scaling, even if it changes the aspect ratio.
		 * Fortunately this is all done for us in hw.
		 */
		if (pipe_src_h != adjusted_mode->crtc_vdisplay ||
		    pipe_src_w != adjusted_mode->crtc_hdisplay) {
			pfit_control |= PFIT_ENABLE;
			if (DISPLAY_VER(display) >= 4)
				pfit_control |= PFIT_SCALING_AUTO;
			else
				pfit_control |= (PFIT_VERT_AUTO_SCALE |
						 PFIT_VERT_INTERP_BILINEAR |
						 PFIT_HORIZ_AUTO_SCALE |
						 PFIT_HORIZ_INTERP_BILINEAR);
		}
		break;
	default:
		MISSING_CASE(conn_state->scaling_mode);
		return -EINVAL;
	}

	/* 965+ wants fuzzy fitting */
	/* FIXME: handle multiple panels by failing gracefully */
	if (DISPLAY_VER(display) >= 4)
		pfit_control |= PFIT_PIPE(crtc->pipe) | PFIT_FILTER_FUZZY;

out:
	if ((pfit_control & PFIT_ENABLE) == 0) {
		pfit_control = 0;
		pfit_pgm_ratios = 0;
	}

	/* Make sure pre-965 set dither correctly for 18bpp panels. */
	if (DISPLAY_VER(display) < 4 && crtc_state->pipe_bpp == 18)
		pfit_control |= PFIT_PANEL_8TO6_DITHER_ENABLE;

	crtc_state->gmch_pfit.control = pfit_control;
	crtc_state->gmch_pfit.pgm_ratios = pfit_pgm_ratios;
	crtc_state->gmch_pfit.lvds_border_bits = border;

	if ((pfit_control & PFIT_ENABLE) == 0)
		return 0;

	return intel_gmch_pfit_check_timings(crtc_state);
}

int intel_panel_fitting(struct intel_crtc_state *crtc_state,
			const struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	if (HAS_GMCH(display))
		return gmch_panel_fitting(crtc_state, conn_state);
	else
		return pch_panel_fitting(crtc_state, conn_state);
}
