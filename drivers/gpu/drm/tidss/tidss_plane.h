/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#ifndef __TIDSS_PLANE_H__
#define __TIDSS_PLANE_H__

#define to_tidss_plane(p) container_of((p), struct tidss_plane, plane)

struct tidss_device;

struct tidss_plane {
	struct drm_plane plane;

	u32 hw_plane_id;
};

struct tidss_plane *tidss_plane_create(struct tidss_device *tidss,
				       u32 hw_plane_id, u32 plane_type,
				       u32 crtc_mask, const u32 *formats,
				       u32 num_formats);

#endif
