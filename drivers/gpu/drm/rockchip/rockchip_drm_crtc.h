/*
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _ROCKCHIP_DRM_CRTC_H_
#define _ROCKCHIP_DRM_CRTC_H_

int rockchip_drm_crtc_create(struct drm_device *dev, unsigned int nr);
int rockchip_drm_crtc_enable_vblank(struct drm_device *dev, int crtc);
void rockchip_drm_crtc_disable_vblank(struct drm_device *dev, int crtc);
void rockchip_drm_crtc_finish_pageflip(struct drm_device *dev, int crtc);

#endif
