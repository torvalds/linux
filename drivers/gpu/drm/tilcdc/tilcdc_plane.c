// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Texas Instruments
 * Author: Jyri Sarha <jsarha@ti.com>
 */

#include <drm/drm_atomic.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>

#include "tilcdc_drv.h"

static const struct drm_plane_funcs tilcdc_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy	= drm_plane_cleanup,
	.reset		= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static int tilcdc_plane_atomic_check(struct drm_plane *plane,
				     struct drm_plane_state *state)
{
	struct drm_crtc_state *crtc_state;
	struct drm_plane_state *old_state = plane->state;
	unsigned int pitch;

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

	pitch = crtc_state->mode.hdisplay *
		state->fb->format->cpp[0];
	if (state->fb->pitches[0] != pitch) {
		dev_err(plane->dev->dev,
			"Invalid pitch: fb and crtc widths must be the same");
		return -EINVAL;
	}

	if (old_state->fb && state->fb->format != old_state->fb->format) {
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
	struct tilcdc_drm_private *priv = dev->dev_private;
	int ret;

	ret = drm_plane_init(dev, plane, 1,
			     &tilcdc_plane_funcs,
			     priv->pixelformats,
			     priv->num_pixelformats,
			     true);
	if (ret) {
		dev_err(dev->dev, "Failed to initialize plane: %d\n", ret);
		return ret;
	}

	drm_plane_helper_add(plane, &plane_helper_funcs);

	return 0;
}
