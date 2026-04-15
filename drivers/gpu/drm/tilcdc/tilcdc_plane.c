// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Texas Instruments
 * Author: Jyri Sarha <jsarha@ti.com>
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>

#include "tilcdc_drv.h"

static const struct drm_plane_funcs tilcdc_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.reset		= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static int tilcdc_plane_atomic_check(struct drm_plane *plane,
				     struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct drm_crtc_state *crtc_state;
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state,
									   plane);
	unsigned int pitch;

	if (!new_state->crtc)
		return 0;

	if (WARN_ON(!new_state->fb))
		return -EINVAL;

	if (new_state->crtc_x || new_state->crtc_y) {
		drm_err(plane->dev, "%s: crtc position must be zero.",
			__func__);
		return -EINVAL;
	}

	crtc_state = drm_atomic_get_new_crtc_state(state, new_state->crtc);
	/* we should have a crtc state if the plane is attached to a crtc */
	if (WARN_ON(!crtc_state))
		return 0;

	if (crtc_state->mode.hdisplay != new_state->crtc_w ||
	    crtc_state->mode.vdisplay != new_state->crtc_h) {
		drm_err(plane->dev,
			"%s: Size must match mode (%dx%d == %dx%d)", __func__,
			crtc_state->mode.hdisplay, crtc_state->mode.vdisplay,
			new_state->crtc_w, new_state->crtc_h);
		return -EINVAL;
	}

	pitch = crtc_state->mode.hdisplay *
		new_state->fb->format->cpp[0];
	if (new_state->fb->pitches[0] != pitch) {
		drm_err(plane->dev,
			"Invalid pitch: fb and crtc widths must be the same");
		return -EINVAL;
	}

	if (old_state->fb && new_state->fb->format != old_state->fb->format) {
		drm_dbg(plane->dev,
			"%s(): pixel format change requires mode_change\n",
			__func__);
		crtc_state->mode_changed = true;
	}

	return 0;
}

static void tilcdc_plane_atomic_update(struct drm_plane *plane,
				       struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);

	if (!new_state->crtc)
		return;

	if (WARN_ON(!new_state->fb || !new_state->crtc->state))
		return;

	if (tilcdc_crtc_update_fb(new_state->crtc,
				  new_state->fb,
				  new_state->crtc->state->event) == 0) {
		new_state->crtc->state->event = NULL;
	}
}

static const struct drm_plane_helper_funcs plane_helper_funcs = {
	.atomic_check = tilcdc_plane_atomic_check,
	.atomic_update = tilcdc_plane_atomic_update,
};

struct tilcdc_plane *tilcdc_plane_init(struct drm_device *dev)
{
	struct tilcdc_drm_private *priv = ddev_to_tilcdc_priv(dev);
	struct tilcdc_plane *plane;

	plane = drmm_universal_plane_alloc(dev, struct tilcdc_plane, base,
					   1, &tilcdc_plane_funcs,
					   priv->pixelformats,
					   priv->num_pixelformats,
					   NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (IS_ERR(plane))
		return plane;

	drm_plane_helper_add(&plane->base, &plane_helper_funcs);

	return plane;
}
