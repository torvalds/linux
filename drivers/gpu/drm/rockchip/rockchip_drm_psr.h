/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Yakir Yang <ykk@rock-chips.com>
 */

#ifndef __ROCKCHIP_DRM_PSR___
#define __ROCKCHIP_DRM_PSR___

void rockchip_drm_psr_flush_all(struct drm_device *dev);

int rockchip_drm_psr_inhibit_put(struct drm_encoder *encoder);
int rockchip_drm_psr_inhibit_get(struct drm_encoder *encoder);

void rockchip_drm_psr_inhibit_get_state(struct drm_atomic_state *state);
void rockchip_drm_psr_inhibit_put_state(struct drm_atomic_state *state);

int rockchip_drm_psr_register(struct drm_encoder *encoder,
			int (*psr_set)(struct drm_encoder *, bool enable));
void rockchip_drm_psr_unregister(struct drm_encoder *encoder);

#endif /* __ROCKCHIP_DRM_PSR__ */
