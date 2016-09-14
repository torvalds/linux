/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * based on exynos_drm_drv.h
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

#ifndef _ROCKCHIP_DRM_DRV_H
#define _ROCKCHIP_DRM_DRV_H

#include <drm/drm_fb_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem.h>

#include <linux/module.h>
#include <linux/component.h>

#define ROCKCHIP_MAX_FB_BUFFER	3
#define ROCKCHIP_MAX_CONNECTOR	2
#define ROCKCHIP_MAX_CRTC	2

struct drm_device;
struct drm_connector;

/*
 * Rockchip drm private crtc funcs.
 * @enable_vblank: enable crtc vblank irq.
 * @disable_vblank: disable crtc vblank irq.
 */
struct rockchip_crtc_funcs {
	int (*enable_vblank)(struct drm_crtc *crtc);
	void (*disable_vblank)(struct drm_crtc *crtc);
	void (*wait_for_update)(struct drm_crtc *crtc);
};

struct rockchip_crtc_state {
	struct drm_crtc_state base;
	int output_type;
	int output_mode;
};
#define to_rockchip_crtc_state(s) \
		container_of(s, struct rockchip_crtc_state, base)

/*
 * Rockchip drm private structure.
 *
 * @crtc: array of enabled CRTCs, used to map from "pipe" to drm_crtc.
 * @num_pipe: number of pipes for this device.
 */
struct rockchip_drm_private {
	struct drm_fb_helper fbdev_helper;
	struct drm_gem_object *fbdev_bo;
	const struct rockchip_crtc_funcs *crtc_funcs[ROCKCHIP_MAX_CRTC];
	struct drm_atomic_state *state;
};

int rockchip_register_crtc_funcs(struct drm_crtc *crtc,
				 const struct rockchip_crtc_funcs *crtc_funcs);
void rockchip_unregister_crtc_funcs(struct drm_crtc *crtc);
int rockchip_drm_dma_attach_device(struct drm_device *drm_dev,
				   struct device *dev);
void rockchip_drm_dma_detach_device(struct drm_device *drm_dev,
				    struct device *dev);
#endif /* _ROCKCHIP_DRM_DRV_H_ */
