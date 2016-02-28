/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
