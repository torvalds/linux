/*
 * Copyright (C) 2015 Texas Instruments
 * Author: Jyri Sarha <jsarha@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <drm/drmP.h>

#include <drm/drm_atomic.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_atomic_helper.h>
#include <uapi/drm/drm_fourcc.h>

#include "tilcdc_drv.h"

static const u32 tilcdc_formats[] = { DRM_FORMAT_RGB565,
				      DRM_FORMAT_RGB888,
				      DRM_FORMAT_XRGB8888 };

static struct drm_plane_funcs tilcdc_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy	= drm_plane_cleanup,
	.set_property	= drm_atomic_helper_plane_set_property,
	.reset		= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static int tilcdc_plane_atomic_check(struct drm_plane *plane,
				     struct drm_plane_state *state)
{
	struct drm_crtc_state *crtc_state;
	struct drm_plane_state *old_state = plane->state;
	unsigned int depth, bpp;

	if (!state->crtc)
		return 0;

	if (WARN_ON(!state->fb))
		return -EINVAL;

	if (state->crtc_x || state->crtc_y) {
		dev_err(plane->dev->dev, "%s: crtc position must be zero.",
			__func__);
		return -EINVAL;
	}

	crtc_state = drm_atomic_get_existing_crtc_state(state->state,
							state->crtc);
	/* we should have a crtc state if the plane is attached to a crtc */
	if (WARN_ON(!crtc_state))
		return 0;

	if (crtc_state->mode.hdisplay != state->crtc_w ||
	    crtc_state->mode.vdisplay != state->crtc_h) {
		dev_err(plane->dev->dev,
			"%s: Size must match mode (%dx%d == %dx%d)", __func__,
			crtc_state->mode.hdisplay, crtc_state->mode.vdisplay,
			state->crtc_w, state->crtc_h);
		return -EINVAL;
	}

	drm_fb_get_bpp_depth(state->fb->pixel_format, &depth, &bpp);
	if (state->fb->pitches[0] != crtc_state->mode.hdisplay * bpp / 8) {
		dev_err(plane->dev->dev,
			"Invalid pitch: fb and crtc widths must be the same");
		return -EINVAL;
	}

	if (state->fb && old_state->fb &&
	    state->fb->pixel_format != old_state->fb->pixel_format) {
		dev_dbg(plane->dev->dev,
			"%s(): pixel format change requires mode_change\n",
			__func__);
		crtc_state->mode_changed = true;
	}

	return 0;
}

static void tilcdc_plane_atomic_update(struct drm_plane *plane,
				       struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = plane->state;

	if (!state->crtc)
		return;

	if (WARN_ON(!state->fb || !state->crtc->state))
		return;

	tilcdc_crtc_update_fb(state->crtc,
			      state->fb,
			      state->crtc->state->event);
}

static const struct drm_plane_helper_funcs plane_helper_funcs = {
	.atomic_check = tilcdc_plane_atomic_check,
	.atomic_update = tilcdc_plane_atomic_update,
};

int tilcdc_plane_init(struct drm_device *dev,
		      struct drm_plane *plane)
{
	int ret;

	ret = drm_plane_init(dev, plane, 1,
			     &tilcdc_plane_funcs,
			     tilcdc_formats,
			     ARRAY_SIZE(tilcdc_formats),
			     true);
	if (ret) {
		dev_err(dev->dev, "Failed to initialize plane: %d\n", ret);
		return ret;
	}

	drm_plane_helper_add(plane, &plane_helper_funcs);

	return 0;
}
