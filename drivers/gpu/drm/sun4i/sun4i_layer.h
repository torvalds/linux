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

struct sun4i_layer {
	struct drm_plane	plane;
	struct sun4i_drv	*drv;
	int			id;
};

static inline struct sun4i_layer *
plane_to_sun4i_layer(struct drm_plane *plane)
{
	return container_of(plane, struct sun4i_layer, plane);
}

struct sun4i_layer **sun4i_layers_init(struct drm_device *drm);

#endif /* _SUN4I_LAYER_H_ */
