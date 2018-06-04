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

#ifndef _SUN4I_LAYER_H_
#define _SUN4I_LAYER_H_

struct sunxi_engine;

struct sun4i_layer {
	struct drm_plane	plane;
	struct sun4i_drv	*drv;
	struct sun4i_backend	*backend;
	int			id;
};

struct sun4i_layer_state {
	struct drm_plane_state	state;
	unsigned int		pipe;
	bool			uses_frontend;
};

static inline struct sun4i_layer *
plane_to_sun4i_layer(struct drm_plane *plane)
{
	return container_of(plane, struct sun4i_layer, plane);
}

static inline struct sun4i_layer_state *
state_to_sun4i_layer_state(struct drm_plane_state *state)
{
	return container_of(state, struct sun4i_layer_state, state);
}

struct drm_plane **sun4i_layers_init(struct drm_device *drm,
				     struct sunxi_engine *engine);

#endif /* _SUN4I_LAYER_H_ */
