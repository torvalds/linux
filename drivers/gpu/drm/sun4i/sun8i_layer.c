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

#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drmP.h>

#include "sun8i_layer.h"
#include "sun8i_mixer.h"

struct sun8i_plane_desc {
	       enum drm_plane_type     type;
	       const uint32_t          *formats;
	       uint32_t                nformats;
};

static void sun8i_mixer_layer_atomic_disable(struct drm_plane *plane,
					       struct drm_plane_state *old_state)
{
	struct sun8i_layer *layer = plane_to_sun8i_layer(plane);
	struct sun8i_mixer *mixer = layer->mixer;

	sun8i_mixer_layer_enable(mixer, layer->id, false);
}

static void sun8i_mixer_layer_atomic_update(struct drm_plane *plane,
					      struct drm_plane_state *old_state)
{
	struct sun8i_layer *layer = plane_to_sun8i_layer(plane);
	struct sun8i_mixer *mixer = layer->mixer;

	sun8i_mixer_update_layer_coord(mixer, layer->id, plane);
	sun8i_mixer_update_layer_formats(mixer, layer->id, plane);
	sun8i_mixer_update_layer_buffer(mixer, layer->id, plane);
	sun8i_mixer_layer_enable(mixer, layer->id, true);
}

static struct drm_plane_helper_funcs sun8i_mixer_layer_helper_funcs = {
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

static const struct sun8i_plane_desc sun8i_mixer_planes[] = {
	{
		.type = DRM_PLANE_TYPE_PRIMARY,
		.formats = sun8i_mixer_layer_formats,
		.nformats = ARRAY_SIZE(sun8i_mixer_layer_formats),
	},
};

static struct sun8i_layer *sun8i_layer_init_one(struct drm_device *drm,
						struct sun8i_mixer *mixer,
						const struct sun8i_plane_desc *plane)
{
	struct sun8i_layer *layer;
	int ret;

	layer = devm_kzalloc(drm->dev, sizeof(*layer), GFP_KERNEL);
	if (!layer)
		return ERR_PTR(-ENOMEM);

	/* possible crtcs are set later */
	ret = drm_universal_plane_init(drm, &layer->plane, 0,
				       &sun8i_mixer_layer_funcs,
				       plane->formats, plane->nformats,
				       NULL, plane->type, NULL);
	if (ret) {
		dev_err(drm->dev, "Couldn't initialize layer\n");
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

	planes = devm_kcalloc(drm->dev, ARRAY_SIZE(sun8i_mixer_planes) + 1,
			      sizeof(*planes), GFP_KERNEL);
	if (!planes)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < ARRAY_SIZE(sun8i_mixer_planes); i++) {
		const struct sun8i_plane_desc *plane = &sun8i_mixer_planes[i];
		struct sun8i_layer *layer;

		layer = sun8i_layer_init_one(drm, mixer, plane);
		if (IS_ERR(layer)) {
			dev_err(drm->dev, "Couldn't initialize %s plane\n",
				i ? "overlay" : "primary");
			return ERR_CAST(layer);
		};

		layer->id = i;
		planes[i] = &layer->plane;
	};

	return planes;
}
