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
struct iommu_domain;

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
 * @mm_lock: protect drm_mm on multi-threads.
 */
struct rockchip_drm_private {
	struct drm_fb_helper fbdev_helper;
	struct drm_gem_object *fbdev_bo;
	struct drm_atomic_state *state;
	struct iommu_domain *domain;
	struct mutex mm_lock;
	struct drm_mm mm;
	struct list_head psr_list;
	spinlock_t psr_list_lock;
};

int rockchip_drm_dma_attach_device(struct drm_device *drm_dev,
				   struct device *dev);
void rockchip_drm_dma_detach_device(struct drm_device *drm_dev,
				    struct device *dev);
int rockchip_drm_wait_line_flag(struct drm_crtc *crtc, unsigned int line_num,
				unsigned int mstimeout);

extern struct platform_driver cdn_dp_driver;
extern struct platform_driver dw_hdmi_rockchip_pltfm_driver;
extern struct platform_driver dw_mipi_dsi_driver;
extern struct platform_driver inno_hdmi_driver;
extern struct platform_driver rockchip_dp_driver;
extern struct platform_driver vop_platform_driver;
#endif /* _ROCKCHIP_DRM_DRV_H_ */
