/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Texas Instruments Incorporated -  http://www.ti.com/
 * Author: Benoit Parrot <bparrot@ti.com>
 */

#ifndef __OMAPDRM_OVERLAY_H__
#define __OMAPDRM_OVERLAY_H__

#include <linux/types.h>

enum drm_plane_type;

struct drm_device;
struct drm_mode_object;
struct drm_plane;

/* Used to associate a HW overlay/plane to a plane */
struct omap_hw_overlay {
	unsigned int idx;

	const char *name;
	enum omap_plane_id id;

	enum omap_overlay_caps caps;
};

int omap_hwoverlays_init(struct omap_drm_private *priv);
void omap_hwoverlays_destroy(struct omap_drm_private *priv);
#endif /* __OMAPDRM_OVERLAY_H__ */
