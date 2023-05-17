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

#ifdef CONFIG_DRM_FBDEV_EMULATION
void omap_fbdev_setup(struct drm_device *dev);
#else
static inline void omap_fbdev_setup(struct drm_device *dev)
{
}
#endif

#endif /* __OMAPDRM_FBDEV_H__ */
