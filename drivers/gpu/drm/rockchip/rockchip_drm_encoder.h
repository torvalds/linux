/*
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 *
 * based on exynos_drm_encoder.h
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

#ifndef _ROCKCHIP_DRM_ENCODER_H_
#define _ROCKCHIP_DRM_ENCODER_H_

struct rockchip_drm_manager;

void rockchip_drm_encoder_setup(struct drm_device *dev);
struct drm_encoder *rockchip_drm_encoder_create(struct drm_device *dev,
					       struct rockchip_drm_manager *mgr,
					       unsigned int possible_crtcs);
struct rockchip_drm_manager *
rockchip_drm_get_manager(struct drm_encoder *encoder);
void rockchip_drm_fn_encoder(struct drm_crtc *crtc, void *data,
			    void (*fn)(struct drm_encoder *, void *));
void rockchip_drm_enable_vblank(struct drm_encoder *encoder, void *data);
void rockchip_drm_disable_vblank(struct drm_encoder *encoder, void *data);
void rockchip_drm_encoder_crtc_dpms(struct drm_encoder *encoder, void *data);
void rockchip_drm_encoder_crtc_pipe(struct drm_encoder *encoder, void *data);
void rockchip_drm_encoder_plane_mode_set(struct drm_encoder *encoder, void *data);
void rockchip_drm_encoder_plane_commit(struct drm_encoder *encoder, void *data);
void rockchip_drm_encoder_plane_enable(struct drm_encoder *encoder, void *data);
void rockchip_drm_encoder_plane_disable(struct drm_encoder *encoder, void *data);
void rockchip_drm_encoder_complete_scanout(struct drm_framebuffer *fb);

#endif
