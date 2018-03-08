/*
 * omap_plane.h -- OMAP DRM Plane
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
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
