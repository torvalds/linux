// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_plane_helper.h>

#include "sun4i_backend.h"
#include "sun4i_frontend.h"
#include "sun4i_layer.h"
#include "sunxi_engine.h"

static void sun4i_backend_layer_reset(struct drm_plane *plane)
{
	struct sun4i_layer_state *state;

	if (plane->state) {
		state = state_to_sun4i_layer_state(plane->state);

		__drm_atomic_helper_plane_destroy_state(&state->state);

		kfree(state);
		plane->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state)
		__drm_atomic_helper_plane_reset(plane, &state->state);
}

static struct drm_plane_state *
sun4i_backend_layer_duplicate_state(struct drm_plane *plane)
{
	struct sun4i_layer_state *orig = state_to_sun4i_layer_state(plane->state);
	struct sun4i_layer_state *copy;

	copy = kzalloc(sizeof(*copy), GFP_KERNEL);
	if (!copy)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &copy->state);
	copy->uses_frontend = orig->uses_frontend;

	return &copy->state;
}

static void sun4i_backend_layer_destroy_state(struct drm_plane *plane,
					      struct drm_plane_state *state)
{
	struct sun4i_layer_state *s_state = state_to_sun4i_layer_state(state);

	__drm_atomic_helper_plane_destroy_state(state);

	kfree(s_state);
}

static void sun4i_backend_layer_atomic_disable(struct drm_plane *plane,
					       struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state,
									   plane);
	struct sun4i_layer_state *layer_state = state_to_sun4i_layer_state(old_state);
	struct sun4i_layer *layer = plane_to_sun4i_layer(plane);
	struct sun4i_backend *backend = layer->backend;

	sun4i_backend_layer_enable(backend, layer->id, false);

	if (layer_state->uses_frontend) {
		unsigned long flags;

		spin_lock_irqsave(&backend->frontend_lock, flags);
		backend->frontend_teardown = true;
		spin_unlock_irqrestore(&backend->frontend_lock, flags);
	}
}

static void sun4i_backend_layer_atomic_update(struct drm_plane *plane,
					      struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct sun4i_layer_state *layer_state = state_to_sun4i_layer_state(new_state);
	struct sun4i_layer *layer = plane_to_sun4i_layer(plane);
	struct sun4i_backend *backend = layer->backend;
	struct sun4i_frontend *frontend = backend->frontend;

	sun4i_backend_cleanup_layer(backend, layer->id);

	if (layer_state->uses_frontend) {
		sun4i_frontend_init(frontend);
		sun4i_frontend_update_coord(frontend, plane);
		sun4i_frontend_update_buffer(frontend, plane);
		sun4i_frontend_update_formats(frontend, plane,
					      DRM_FORMAT_XRGB8888);
		sun4i_backend_update_layer_frontend(backend, layer->id,
						    DRM_FORMAT_XRGB8888);
		sun4i_frontend_enable(frontend);
	} else {
		sun4i_backend_update_layer_formats(backend, layer->id, plane);
		sun4i_backend_update_layer_buffer(backend, layer->id, plane);
	}

	sun4i_backend_update_layer_coord(backend, layer->id, plane);
	sun4i_backend_update_layer_zpos(backend, layer->id, plane);
	sun4i_backend_layer_enable(backend, layer->id, true);
}

static bool sun4i_layer_format_mod_supported(struct drm_plane *plane,
					     uint32_t format, uint64_t modifier)
{
	struct sun4i_layer *layer = plane_to_sun4i_layer(plane);

	if (IS_ERR_OR_NULL(layer->backend->frontend))
		sun4i_backend_format_is_supported(format, modifier);

	return sun4i_backend_format_is_supported(format, modifier) ||
	       sun4i_frontend_format_is_supported(format, modifier);
}

static const struct drm_plane_helper_funcs sun4i_backend_layer_helper_funcs = {
	.atomic_disable	= sun4i_backend_layer_atomic_disable,
	.atomic_update	= sun4i_backend_layer_atomic_update,
};

static const struct drm_plane_funcs sun4i_backend_layer_funcs = {
	.atomic_destroy_state	= sun4i_backend_layer_destroy_state,
	.atomic_duplicate_state	= sun4i_backend_layer_duplicate_state,
	.destroy		= drm_plane_cleanup,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.reset			= sun4i_backend_layer_reset,
	.update_plane		= drm_atomic_helper_update_plane,
	.format_mod_supported	= sun4i_layer_format_mod_supported,
};

static const uint32_t sun4i_layer_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV61,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_YUV411,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YUV444,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVU411,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_YVU422,
	DRM_FORMAT_YVU444,
	DRM_FORMAT_YVYU,
};

static const uint32_t sun4i_backend_layer_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
};

static const uint64_t sun4i_layer_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_ALLWINNER_TILED,
	DRM_FORMAT_MOD_INVALID
};

static struct sun4i_layer *sun4i_layer_init_one(struct drm_device *drm,
						struct sun4i_backend *backend,
						enum drm_plane_type type,
						unsigned int id)
{
	const uint64_t *modifiers = sun4i_layer_modifiers;
	const uint32_t *formats = sun4i_layer_formats;
	unsigned int formats_len = ARRAY_SIZE(sun4i_layer_formats);
	struct sun4i_layer *layer;
	int ret;

	layer = devm_kzalloc(drm->dev, sizeof(*layer), GFP_KERNEL);
	if (!layer)
		return ERR_PTR(-ENOMEM);

	layer->id = id;
	layer->backend = backend;

	if (IS_ERR_OR_NULL(backend->frontend)) {
		formats = sun4i_backend_layer_formats;
		formats_len = ARRAY_SIZE(sun4i_backend_layer_formats);
		modifiers = NULL;
	}

	/* possible crtcs are set later */
	ret = drm_universal_plane_init(drm, &layer->plane, 0,
				       &sun4i_backend_layer_funcs,
				       formats, formats_len,
				       modifiers, type, NULL);
	if (ret) {
		dev_err(drm->dev, "Couldn't initialize layer\n");
		return ERR_PTR(ret);
	}

	drm_plane_helper_add(&layer->plane,
			     &sun4i_backend_layer_helper_funcs);

	drm_plane_create_alpha_property(&layer->plane);
	drm_plane_create_zpos_property(&layer->plane, layer->id,
				       0, SUN4I_BACKEND_NUM_LAYERS - 1);

	return layer;
}

struct drm_plane **sun4i_layers_init(struct drm_device *drm,
				     struct sunxi_engine *engine)
{
	struct drm_plane **planes;
	struct sun4i_backend *backend = engine_to_sun4i_backend(engine);
	int i;

	/* We need to have a sentinel at the need, hence the overallocation */
	planes = devm_kcalloc(drm->dev, SUN4I_BACKEND_NUM_LAYERS + 1,
			      sizeof(*planes), GFP_KERNEL);
	if (!planes)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < SUN4I_BACKEND_NUM_LAYERS; i++) {
		enum drm_plane_type type = i ? DRM_PLANE_TYPE_OVERLAY : DRM_PLANE_TYPE_PRIMARY;
		struct sun4i_layer *layer;

		layer = sun4i_layer_init_one(drm, backend, type, i);
		if (IS_ERR(layer)) {
			dev_err(drm->dev, "Couldn't initialize %s plane\n",
				i ? "overlay" : "primary");
			return ERR_CAST(layer);
		}

		planes[i] = &layer->plane;
	}

	return planes;
}
