/*
 * Copyright © 2006-2010 Intel Corporation
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 *      Dave Airlie <airlied@linux.ie>
 *      Jesse Barnes <jesse.barnes@intel.com>
 *      Chris Wilson <chris@chris-wilson.co.uk>
 */

#include <linux/kernel.h>
#include <linux/pwm.h>

#include "intel_backlight.h"
#include "intel_connector.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_panel.h"

bool intel_panel_use_ssc(struct drm_i915_private *i915)
{
	if (i915->params.panel_use_ssc >= 0)
		return i915->params.panel_use_ssc != 0;
	return i915->vbt.lvds_use_ssc
		&& !(i915->quirks & QUIRK_LVDS_SSC_DISABLE);
}

void intel_panel_fixed_mode(const struct drm_display_mode *fixed_mode,
			    struct drm_display_mode *adjusted_mode)
{
	drm_mode_copy(adjusted_mode, fixed_mode);

	drm_mode_set_crtcinfo(adjusted_mode, 0);
}

static bool is_downclock_mode(const struct drm_display_mode *downclock_mode,
			      const struct drm_display_mode *fixed_mode)
{
	return drm_mode_match(downclock_mode, fixed_mode,
			      DRM_MODE_MATCH_TIMINGS |
			      DRM_MODE_MATCH_FLAGS |
			      DRM_MODE_MATCH_3D_FLAGS) &&
		downclock_mode->clock < fixed_mode->clock;
}

struct drm_display_mode *
intel_panel_edid_downclock_mode(struct intel_connector *connector,
				const struct drm_display_mode *fixed_mode)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	const struct drm_display_mode *scan, *best_mode = NULL;
	struct drm_display_mode *downclock_mode;
	int best_clock = fixed_mode->clock;

	list_for_each_entry(scan, &connector->base.probed_modes, head) {
		/*
		 * If one mode has the same resolution with the fixed_panel
		 * mode while they have the different refresh rate, it means
		 * that the reduced downclock is found. In such
		 * case we can set the different FPx0/1 to dynamically select
		 * between low and high frequency.
		 */
		if (is_downclock_mode(scan, fixed_mode) &&
		    scan->clock < best_clock) {
			/*
			 * The downclock is already found. But we
			 * expect to find the lower downclock.
			 */
			best_clock = scan->clock;
			best_mode = scan;
		}
	}

	if (!best_mode)
		return NULL;

	downclock_mode = drm_mode_duplicate(&dev_priv->drm, best_mode);
	if (!downclock_mode)
		return NULL;

	drm_dbg_kms(&dev_priv->drm,
		    "[CONNECTOR:%d:%s] using downclock mode from EDID: ",
		    connector->base.base.id, connector->base.name);
	drm_mode_debug_printmodeline(downclock_mode);

	return downclock_mode;
}

struct drm_display_mode *
intel_panel_edid_fixed_mode(struct intel_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	const struct drm_display_mode *scan;
	struct drm_display_mode *fixed_mode;

	if (list_empty(&connector->base.probed_modes))
		return NULL;

	/* prefer fixed mode from EDID if available */
	list_for_each_entry(scan, &connector->base.probed_modes, head) {
		if ((scan->type & DRM_MODE_TYPE_PREFERRED) == 0)
			continue;

		fixed_mode = drm_mode_duplicate(&dev_priv->drm, scan);
		if (!fixed_mode)
			return NULL;

		drm_dbg_kms(&dev_priv->drm,
			    "[CONNECTOR:%d:%s] using preferred mode from EDID: ",
			    connector->base.base.id, connector->base.name);
		drm_mode_debug_printmodeline(fixed_mode);

		return fixed_mode;
	}

	scan = list_first_entry(&connector->base.probed_modes,
				typeof(*scan), head);

	fixed_mode = drm_mode_duplicate(&dev_priv->drm, scan);
	if (!fixed_mode)
		return NULL;

	fixed_mode->type |= DRM_MODE_TYPE_PREFERRED;

	drm_dbg_kms(&dev_priv->drm,
		    "[CONNECTOR:%d:%s] using first mode from EDID: ",
		    connector->base.base.id, connector->base.name);
	drm_mode_debug_printmodeline(fixed_mode);

	return fixed_mode;
}

struct drm_display_mode *
intel_panel_vbt_fixed_mode(struct intel_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct drm_display_info *info = &connector->base.display_info;
	struct drm_display_mode *fixed_mode;

	if (!dev_priv->vbt.lfp_lvds_vbt_mode)
		return NULL;

	fixed_mode = drm_mode_duplicate(&dev_priv->drm,
					dev_priv->vbt.lfp_lvds_vbt_mode);
	if (!fixed_mode)
		return NULL;

	fixed_mode->type |= DRM_MODE_TYPE_PREFERRED;

	drm_dbg_kms(&dev_priv->drm, "[CONNECTOR:%d:%s] using mode from VBT: ",
		    connector->base.base.id, connector->base.name);
	drm_mode_debug_printmodeline(fixed_mode);

	info->width_mm = fixed_mode->width_mm;
	info->height_mm = fixed_mode->height_mm;

	return fixed_mode;
}

/* adjusted_mode has been preset to be the panel's fixed mode */
static int pch_panel_fitting(struct intel_crtc_state *crtc_state,
			     const struct drm_connector_state *conn_state)
{
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	int x, y, width, height;

	/* Native modes don't need fitting */
	if (adjusted_mode->crtc_hdisplay == crtc_state->pipe_src_w &&
	    adjusted_mode->crtc_vdisplay == crtc_state->pipe_src_h &&
	    crtc_state->output_format != INTEL_OUTPUT_FORMAT_YCBCR420)
		return 0;

	switch (conn_state->scaling_mode) {
	case DRM_MODE_SCALE_CENTER:
		width = crtc_state->pipe_src_w;
		height = crtc_state->pipe_src_h;
		x = (adjusted_mode->crtc_hdisplay - width + 1)/2;
		y = (adjusted_mode->crtc_vdisplay - height + 1)/2;
		break;

	case DRM_MODE_SCALE_ASPECT:
		/* Scale but preserve the aspect ratio */
		{
			u32 scaled_width = adjusted_mode->crtc_hdisplay
				* crtc_state->pipe_src_h;
			u32 scaled_height = crtc_state->pipe_src_w
				* adjusted_mode->crtc_vdisplay;
			if (scaled_width > scaled_height) { /* pillar */
				width = scaled_height / crtc_state->pipe_src_h;
				if (width & 1)
					width++;
				x = (adjusted_mode->crtc_hdisplay - width + 1) / 2;
				y = 0;
				height = adjusted_mode->crtc_vdisplay;
			} else if (scaled_width < scaled_height) { /* letter */
				height = scaled_width / crtc_state->pipe_src_w;
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
		WARN_ON(adjusted_mode->crtc_hdisplay != crtc_state->pipe_src_w);
		WARN_ON(adjusted_mode->crtc_vdisplay != crtc_state->pipe_src_h);
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
	u32 scaled_width = adjusted_mode->crtc_hdisplay *
		crtc_state->pipe_src_h;
	u32 scaled_height = crtc_state->pipe_src_w *
		adjusted_mode->crtc_vdisplay;

	/* 965+ is easy, it does everything in hw */
	if (scaled_width > scaled_height)
		*pfit_control |= PFIT_ENABLE |
			PFIT_SCALING_PILLAR;
	else if (scaled_width < scaled_height)
		*pfit_control |= PFIT_ENABLE |
			PFIT_SCALING_LETTER;
	else if (adjusted_mode->crtc_hdisplay != crtc_state->pipe_src_w)
		*pfit_control |= PFIT_ENABLE | PFIT_SCALING_AUTO;
}

static void i9xx_scale_aspect(struct intel_crtc_state *crtc_state,
			      u32 *pfit_control, u32 *pfit_pgm_ratios,
			      u32 *border)
{
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	u32 scaled_width = adjusted_mode->crtc_hdisplay *
		crtc_state->pipe_src_h;
	u32 scaled_height = crtc_state->pipe_src_w *
		adjusted_mode->crtc_vdisplay;
	u32 bits;

	/*
	 * For earlier chips we have to calculate the scaling
	 * ratio by hand and program it into the
	 * PFIT_PGM_RATIO register
	 */
	if (scaled_width > scaled_height) { /* pillar */
		centre_horizontally(adjusted_mode,
				    scaled_height /
				    crtc_state->pipe_src_h);

		*border = LVDS_BORDER_ENABLE;
		if (crtc_state->pipe_src_h != adjusted_mode->crtc_vdisplay) {
			bits = panel_fitter_scaling(crtc_state->pipe_src_h,
						    adjusted_mode->crtc_vdisplay);

			*pfit_pgm_ratios |= (bits << PFIT_HORIZ_SCALE_SHIFT |
					     bits << PFIT_VERT_SCALE_SHIFT);
			*pfit_control |= (PFIT_ENABLE |
					  VERT_INTERP_BILINEAR |
					  HORIZ_INTERP_BILINEAR);
		}
	} else if (scaled_width < scaled_height) { /* letter */
		centre_vertically(adjusted_mode,
				  scaled_width /
				  crtc_state->pipe_src_w);

		*border = LVDS_BORDER_ENABLE;
		if (crtc_state->pipe_src_w != adjusted_mode->crtc_hdisplay) {
			bits = panel_fitter_scaling(crtc_state->pipe_src_w,
						    adjusted_mode->crtc_hdisplay);

			*pfit_pgm_ratios |= (bits << PFIT_HORIZ_SCALE_SHIFT |
					     bits << PFIT_VERT_SCALE_SHIFT);
			*pfit_control |= (PFIT_ENABLE |
					  VERT_INTERP_BILINEAR |
					  HORIZ_INTERP_BILINEAR);
		}
	} else {
		/* Aspects match, Let hw scale both directions */
		*pfit_control |= (PFIT_ENABLE |
				  VERT_AUTO_SCALE | HORIZ_AUTO_SCALE |
				  VERT_INTERP_BILINEAR |
				  HORIZ_INTERP_BILINEAR);
	}
}

static int gmch_panel_fitting(struct intel_crtc_state *crtc_state,
			      const struct drm_connector_state *conn_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 pfit_control = 0, pfit_pgm_ratios = 0, border = 0;
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;

	/* Native modes don't need fitting */
	if (adjusted_mode->crtc_hdisplay == crtc_state->pipe_src_w &&
	    adjusted_mode->crtc_vdisplay == crtc_state->pipe_src_h)
		goto out;

	switch (conn_state->scaling_mode) {
	case DRM_MODE_SCALE_CENTER:
		/*
		 * For centered modes, we have to calculate border widths &
		 * heights and modify the values programmed into the CRTC.
		 */
		centre_horizontally(adjusted_mode, crtc_state->pipe_src_w);
		centre_vertically(adjusted_mode, crtc_state->pipe_src_h);
		border = LVDS_BORDER_ENABLE;
		break;
	case DRM_MODE_SCALE_ASPECT:
		/* Scale but preserve the aspect ratio */
		if (DISPLAY_VER(dev_priv) >= 4)
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
		if (crtc_state->pipe_src_h != adjusted_mode->crtc_vdisplay ||
		    crtc_state->pipe_src_w != adjusted_mode->crtc_hdisplay) {
			pfit_control |= PFIT_ENABLE;
			if (DISPLAY_VER(dev_priv) >= 4)
				pfit_control |= PFIT_SCALING_AUTO;
			else
				pfit_control |= (VERT_AUTO_SCALE |
						 VERT_INTERP_BILINEAR |
						 HORIZ_AUTO_SCALE |
						 HORIZ_INTERP_BILINEAR);
		}
		break;
	default:
		MISSING_CASE(conn_state->scaling_mode);
		return -EINVAL;
	}

	/* 965+ wants fuzzy fitting */
	/* FIXME: handle multiple panels by failing gracefully */
	if (DISPLAY_VER(dev_priv) >= 4)
		pfit_control |= PFIT_PIPE(crtc->pipe) | PFIT_FILTER_FUZZY;

out:
	if ((pfit_control & PFIT_ENABLE) == 0) {
		pfit_control = 0;
		pfit_pgm_ratios = 0;
	}

	/* Make sure pre-965 set dither correctly for 18bpp panels. */
	if (DISPLAY_VER(dev_priv) < 4 && crtc_state->pipe_bpp == 18)
		pfit_control |= PANEL_8TO6_DITHER_ENABLE;

	crtc_state->gmch_pfit.control = pfit_control;
	crtc_state->gmch_pfit.pgm_ratios = pfit_pgm_ratios;
	crtc_state->gmch_pfit.lvds_border_bits = border;

	return 0;
}

int intel_panel_fitting(struct intel_crtc_state *crtc_state,
			const struct drm_connector_state *conn_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	if (HAS_GMCH(i915))
		return gmch_panel_fitting(crtc_state, conn_state);
	else
		return pch_panel_fitting(crtc_state, conn_state);
}

enum drm_connector_status
intel_panel_detect(struct drm_connector *connector, bool force)
{
	struct drm_i915_private *i915 = to_i915(connector->dev);

	if (!INTEL_DISPLAY_ENABLED(i915))
		return connector_status_disconnected;

	return connector_status_connected;
}

int intel_panel_init(struct intel_panel *panel,
		     struct drm_display_mode *fixed_mode,
		     struct drm_display_mode *downclock_mode)
{
	intel_backlight_init_funcs(panel);

	panel->fixed_mode = fixed_mode;
	panel->downclock_mode = downclock_mode;

	return 0;
}

void intel_panel_fini(struct intel_panel *panel)
{
	struct intel_connector *intel_connector =
		container_of(panel, struct intel_connector, panel);

	intel_backlight_destroy(panel);

	if (panel->fixed_mode)
		drm_mode_destroy(intel_connector->base.dev, panel->fixed_mode);

	if (panel->downclock_mode)
		drm_mode_destroy(intel_connector->base.dev,
				panel->downclock_mode);
}
