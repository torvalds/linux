/*
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 *
 * based on exynos_drm_fbdev.h
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
#ifndef _ROCKCHIP_DRM_FBDEV_H_
#define _ROCKCHIP_DRM_FBDEV_H_

int rockchip_drm_fbdev_init(struct drm_device *dev);
int rockchip_drm_fbdev_reinit(struct drm_device *dev);
void rockchip_drm_fbdev_fini(struct drm_device *dev);
void rockchip_drm_fbdev_restore_mode(struct drm_device *dev);

#endif
