/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Yakir Yang <ykk@rock-chips.com>
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

#ifndef __ROCKCHIP_DRM_PSR___
#define __ROCKCHIP_DRM_PSR___

void rockchip_drm_psr_flush_all(struct drm_device *dev);
int rockchip_drm_psr_flush(struct drm_crtc *crtc);

int rockchip_drm_psr_activate(struct drm_crtc *crtc);
int rockchip_drm_psr_deactivate(struct drm_crtc *crtc);

int rockchip_drm_psr_register(struct drm_encoder *encoder,
			void (*psr_set)(struct drm_encoder *, bool enable));
void rockchip_drm_psr_unregister(struct drm_encoder *encoder);

#endif /* __ROCKCHIP_DRM_PSR__ */
