/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 */

#ifndef _ROCKCHIP_DRM_FBDEV_H
#define _ROCKCHIP_DRM_FBDEV_H

#ifdef CONFIG_DRM_FBDEV_EMULATION
int rockchip_drm_fbdev_init(struct drm_device *dev);
void rockchip_drm_fbdev_fini(struct drm_device *dev);
#else
static inline int rockchip_drm_fbdev_init(struct drm_device *dev)
{
	return 0;
}

static inline void rockchip_drm_fbdev_fini(struct drm_device *dev)
{
}
#endif

#endif /* _ROCKCHIP_DRM_FBDEV_H */
