/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * omap_fbdev.h -- OMAP DRM FBDEV Compatibility
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 */

#ifndef __OMAPDRM_FBDEV_H__
#define __OMAPDRM_FBDEV_H__

struct drm_device;
struct drm_fb_helper;
struct drm_fb_helper_surface_size;

#ifdef CONFIG_DRM_FBDEV_EMULATION
int omap_fbdev_driver_fbdev_probe(struct drm_fb_helper *helper,
				  struct drm_fb_helper_surface_size *sizes);
#define OMAP_FBDEV_DRIVER_OPS \
	.fbdev_probe = omap_fbdev_driver_fbdev_probe
void omap_fbdev_setup(struct drm_device *dev);
#else
#define OMAP_FBDEV_DRIVER_OPS \
	.fbdev_probe = NULL
static inline void omap_fbdev_setup(struct drm_device *dev)
{
}
#endif

#endif /* __OMAPDRM_FBDEV_H__ */
