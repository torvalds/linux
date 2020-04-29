/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * omap_plane.h -- OMAP DRM Plane
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 */

#ifndef __OMAPDRM_PLANE_H__
#define __OMAPDRM_PLANE_H__

#include <linux/types.h>

enum drm_plane_type;

struct drm_device;
struct drm_mode_object;
struct drm_plane;

struct drm_plane *omap_plane_init(struct drm_device *dev,
		int idx, enum drm_plane_type type,
		u32 possible_crtcs);
void omap_plane_install_properties(struct drm_plane *plane,
		struct drm_mode_object *obj);

#endif /* __OMAPDRM_PLANE_H__ */
