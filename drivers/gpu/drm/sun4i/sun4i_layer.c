/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_plane_helper.h>
#include <drm/drmP.h>

#include "sun4i_backend.h"
#include "sun4i_drv.h"
#include "sun4i_layer.h"

#define SUN4I_NUM_LAYERS	2

static int sun4i_backend_layer_atomic_check(struct drm_plane *plane,
					    struct drm_plane_state *state)
{
	return 0;
}

static void sun4i_backend_layer_atomic_disable(struct drm_plane *plane,
					       struct drm_plane_state *old_state)
{
	struct sun4i_layer *layer = plane_to_sun4i_layer(plane);
	struct sun4i_drv *drv = layer->drv;
	struct sun4i_backend *backend = drv->backend;

	sun4i_backend_layer_enable(backend, layer->id, false);
}

static void sun4i_backend_layer_atomic_update(struct drm_plane *plane,
					      struct drm_plane_state *old_state)
{
	struct sun4i_layer *layer = plane_to_sun4i_layer(plane);
	struct sun4i_drv *drv = layer->drv;
	struct sun4i_backend *backend = drv->backend;

	sun4i_backend_update_layer_coord(backend, layer->id, plane);
	sun4i_backend_update_layer_formats(backend, layer->id, plane);
	sun4i_backend_update_layer_buffer(backend, layer->id, plane);
	sun4i_backend_layer_enable(backend, layer->id, true);
}

static struct drm_plane_helper_funcs sun4i_backend_layer_helper_funcs = {
	.atomic_check	= sun4i_backend_layer_atomic_check,
	.atomic_disable	= sun4i_backend_layer_atomic_disable,
	.atomic_update	= sun4i_backend_layer_atomic_update,
};

static const struct drm_plane_funcs sun4i_backend_layer_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.destroy		= drm_plane_cleanup,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.reset			= drm_atomic_helper_plane_reset,
	.update_plane		= drm_atomic_helper_update_plane,
};

static const uint32_t sun4i_backend_layer_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB888,
};

static struct sun4i_layer *sun4i_layer_init_one(struct drm_device *drm,
						enum drm_plane_type type)
{
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_layer *layer;
	int ret;

	layer = devm_kzalloc(drm->dev, sizeof(*layer), GFP_KERNEL);
	if (!layer)
		return ERR_PTR(-ENOMEM);

	ret = drm_universal_plane_init(drm, &layer->plane, BIT(0),
				       &sun4i_backend_layer_funcs,
				       sun4i_backend_layer_formats,
				       ARRAY_SIZE(sun4i_backend_layer_formats),
				       type,
				       NULL);
	if (ret) {
		dev_err(drm->dev, "Couldn't initialize layer\n");
		return ERR_PTR(ret);
	}

	drm_plane_helper_add(&layer->plane,
			     &sun4i_backend_layer_helper_funcs);
	layer->drv = drv;

	if (type == DRM_PLANE_TYPE_PRIMARY)
		drv->primary = &layer->plane;

	return layer;
}

struct sun4i_layer **sun4i_layers_init(struct drm_device *drm)
{
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_layer **layers;
	int i;

	layers = devm_kcalloc(drm->dev, SUN4I_NUM_LAYERS, sizeof(**layers),
			      GFP_KERNEL);
	if (!layers)
		return ERR_PTR(-ENOMEM);

	/*
	 * The hardware is a bit unusual here.
	 *
	 * Even though it supports 4 layers, it does the composition
	 * in two separate steps.
	 *
	 * The first one is assigning a layer to one of its two
	 * pipes. If more that 1 layer is assigned to the same pipe,
	 * and if pixels overlaps, the pipe will take the pixel from
	 * the layer with the highest priority.
	 *
	 * The second step is the actual alpha blending, that takes
	 * the two pipes as input, and uses the eventual alpha
	 * component to do the transparency between the two.
	 *
	 * This two steps scenario makes us unable to guarantee a
	 * robust alpha blending between the 4 layers in all
	 * situations. So we just expose two layers, one per pipe. On
	 * SoCs that support it, sprites could fill the need for more
	 * layers.
	 */
	for (i = 0; i < SUN4I_NUM_LAYERS; i++) {
		enum drm_plane_type type = (i == 0)
					 ? DRM_PLANE_TYPE_PRIMARY
					 : DRM_PLANE_TYPE_OVERLAY;
		struct sun4i_layer *layer = layers[i];

		layer = sun4i_layer_init_one(drm, type);
		if (IS_ERR(layer)) {
			dev_err(drm->dev, "Couldn't initialize %s plane\n",
				i ? "overlay" : "primary");
			return ERR_CAST(layer);
		};

		DRM_DEBUG_DRIVER("Assigning %s plane to pipe %d\n",
				 i ? "overlay" : "primary", i);
		regmap_update_bits(drv->backend->regs, SUN4I_BACKEND_ATTCTL_REG0(i),
				   SUN4I_BACKEND_ATTCTL_REG0_LAY_PIPESEL_MASK,
				   SUN4I_BACKEND_ATTCTL_REG0_LAY_PIPESEL(i));

		layer->id = i;
	};

	return layers;
}
