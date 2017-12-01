/*
 * Copyright (C) Icenowy Zheng <icenowy@aosc.io>
 *
 * Based on sun4i_layer.h, which is:
 *   Copyright (C) 2015 Free Electrons
 *   Copyright (C) 2015 NextThing Co
 *
 *   Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_plane_helper.h>
#include <drm/drmP.h>

#include "sun8i_layer.h"
#include "sun8i_mixer.h"

static int sun8i_mixer_layer_atomic_check(struct drm_plane *plane,
					  struct drm_plane_state *state)
{
	struct drm_crtc *crtc = state->crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_rect clip;

	if (!crtc)
		return 0;

	crtc_state = drm_atomic_get_existing_crtc_state(state->state, crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	clip.x1 = 0;
	clip.y1 = 0;
	clip.x2 = crtc_state->adjusted_mode.hdisplay;
	clip.y2 = crtc_state->adjusted_mode.vdisplay;

	return drm_atomic_helper_check_plane_state(state, crtc_state, &clip,
						   DRM_PLANE_HELPER_NO_SCALING,
						   DRM_PLANE_HELPER_NO_SCALING,
						   true, true);
}

static void sun8i_mixer_layer_atomic_disable(struct drm_plane *plane,
					       struct drm_plane_state *old_state)
{
	struct sun8i_layer *layer = plane_to_sun8i_layer(plane);
	struct sun8i_mixer *mixer = layer->mixer;

	sun8i_mixer_layer_enable(mixer, layer->channel,
				 layer->overlay, false);
}

static void sun8i_mixer_layer_atomic_update(struct drm_plane *plane,
					      struct drm_plane_state *old_state)
{
	struct sun8i_layer *layer = plane_to_sun8i_layer(plane);
	struct sun8i_mixer *mixer = layer->mixer;

	if (!plane->state->visible) {
		sun8i_mixer_layer_enable(mixer, layer->channel,
					 layer->overlay, false);
		return;
	}

	sun8i_mixer_update_layer_coord(mixer, layer->channel,
				       layer->overlay, plane);
	sun8i_mixer_update_layer_formats(mixer, layer->channel,
					 layer->overlay, plane);
	sun8i_mixer_update_layer_buffer(mixer, layer->channel,
					layer->overlay, plane);
	sun8i_mixer_layer_enable(mixer, layer->channel,
				 layer->overlay, true);
}

static struct drm_plane_helper_funcs sun8i_mixer_layer_helper_funcs = {
	.atomic_check	= sun8i_mixer_layer_atomic_check,
	.atomic_disable	= sun8i_mixer_layer_atomic_disable,
	.atomic_update	= sun8i_mixer_layer_atomic_update,
};

static const struct drm_plane_funcs sun8i_mixer_layer_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.destroy		= drm_plane_cleanup,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.reset			= drm_atomic_helper_plane_reset,
	.update_plane		= drm_atomic_helper_update_plane,
};

static const uint32_t sun8i_mixer_layer_formats[] = {
	DRM_FORMAT_RGB888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
};

static struct sun8i_layer *sun8i_layer_init_one(struct drm_device *drm,
						struct sun8i_mixer *mixer,
						int index)
{
	struct sun8i_layer *layer;
	enum drm_plane_type type;
	int ret;

	layer = devm_kzalloc(drm->dev, sizeof(*layer), GFP_KERNEL);
	if (!layer)
		return ERR_PTR(-ENOMEM);

	type = index == 0 ? DRM_PLANE_TYPE_PRIMARY : DRM_PLANE_TYPE_OVERLAY;

	/* possible crtcs are set later */
	ret = drm_universal_plane_init(drm, &layer->plane, 0,
				       &sun8i_mixer_layer_funcs,
				       sun8i_mixer_layer_formats,
				       ARRAY_SIZE(sun8i_mixer_layer_formats),
				       NULL, type, NULL);
	if (ret) {
		dev_err(drm->dev, "Couldn't initialize layer\n");
		return ERR_PTR(ret);
	}

	/* fixed zpos for now */
	ret = drm_plane_create_zpos_immutable_property(&layer->plane, index);
	if (ret) {
		dev_err(drm->dev, "Couldn't add zpos property\n");
		return ERR_PTR(ret);
	}

	drm_plane_helper_add(&layer->plane,
			     &sun8i_mixer_layer_helper_funcs);
	layer->mixer = mixer;

	return layer;
}

struct drm_plane **sun8i_layers_init(struct drm_device *drm,
				     struct sunxi_engine *engine)
{
	struct drm_plane **planes;
	struct sun8i_mixer *mixer = engine_to_sun8i_mixer(engine);
	int i;

	planes = devm_kcalloc(drm->dev, mixer->cfg->ui_num + 1,
			      sizeof(*planes), GFP_KERNEL);
	if (!planes)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < mixer->cfg->ui_num; i++) {
		struct sun8i_layer *layer;

		layer = sun8i_layer_init_one(drm, mixer, i);
		if (IS_ERR(layer)) {
			dev_err(drm->dev, "Couldn't initialize %s plane\n",
				i ? "overlay" : "primary");
			return ERR_CAST(layer);
		};

		layer->channel = mixer->cfg->vi_num + i;
		layer->overlay = 0;
		planes[i] = &layer->plane;
	};

	return planes;
}
