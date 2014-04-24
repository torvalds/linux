/*
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 *
 * based on exynos_drm_connector.h
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
#ifndef _ROCKCHIP_DRM_CONNECTOR_H_
#define _ROCKCHIP_DRM_CONNECTOR_H_

struct drm_connector *rockchip_drm_connector_create(struct drm_device *dev,
						   struct drm_encoder *encoder);

struct drm_encoder *rockchip_drm_best_encoder(struct drm_connector *connector);

void rockchip_drm_display_power(struct drm_connector *connector, int mode);

#endif
