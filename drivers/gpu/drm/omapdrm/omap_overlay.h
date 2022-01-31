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
int omap_overlay_assign(struct drm_atomic_state *s, struct drm_plane *plane,
			u32 caps, u32 fourcc, struct omap_hw_overlay **overlay,
			struct omap_hw_overlay **r_overlay);
void omap_overlay_release(struct drm_atomic_state *s, struct omap_hw_overlay *overlay);
void omap_overlay_update_state(struct omap_drm_private *priv, struct omap_hw_overlay *overlay);
#endif /* __OMAPDRM_OVERLAY_H__ */
