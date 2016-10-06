/*
 * Copyright (c) 2015 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef MTK_DRM_DRV_H
#define MTK_DRM_DRV_H

#include <linux/io.h>
#include "mtk_drm_ddp_comp.h"

#define MAX_CRTC	2
#define MAX_CONNECTOR	2

struct device;
struct device_node;
struct drm_crtc;
struct drm_device;
struct drm_fb_helper;
struct drm_property;
struct regmap;

struct mtk_drm_private {
	struct drm_device *drm;
	struct device *dma_dev;

	struct drm_crtc *crtc[MAX_CRTC];
	unsigned int num_pipes;

	struct device_node *mutex_node;
	struct device *mutex_dev;
	void __iomem *config_regs;
	struct device_node *comp_node[DDP_COMPONENT_ID_MAX];
	struct mtk_ddp_comp *ddp_comp[DDP_COMPONENT_ID_MAX];

	struct {
		struct drm_atomic_state *state;
		struct work_struct work;
		struct mutex lock;
	} commit;

	struct drm_atomic_state *suspend_state;
};

extern struct platform_driver mtk_ddp_driver;
extern struct platform_driver mtk_disp_ovl_driver;
extern struct platform_driver mtk_disp_rdma_driver;
extern struct platform_driver mtk_dpi_driver;
extern struct platform_driver mtk_dsi_driver;
extern struct platform_driver mtk_mipi_tx_driver;

#endif /* MTK_DRM_DRV_H */
