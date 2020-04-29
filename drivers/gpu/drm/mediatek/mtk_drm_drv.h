/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#ifndef MTK_DRM_DRV_H
#define MTK_DRM_DRV_H

#include <linux/io.h>
#include "mtk_drm_ddp_comp.h"

#define MAX_CRTC	3
#define MAX_CONNECTOR	2

struct device;
struct device_node;
struct drm_crtc;
struct drm_device;
struct drm_fb_helper;
struct drm_property;
struct regmap;

struct mtk_mmsys_driver_data {
	const enum mtk_ddp_comp_id *main_path;
	unsigned int main_len;
	const enum mtk_ddp_comp_id *ext_path;
	unsigned int ext_len;
	const enum mtk_ddp_comp_id *third_path;
	unsigned int third_len;

	bool shadow_register;
};

struct mtk_drm_private {
	struct drm_device *drm;
	struct device *dma_dev;

	unsigned int num_pipes;

	struct device_node *mutex_node;
	struct device *mutex_dev;
	void __iomem *config_regs;
	struct device_node *comp_node[DDP_COMPONENT_ID_MAX];
	struct mtk_ddp_comp *ddp_comp[DDP_COMPONENT_ID_MAX];
	const struct mtk_mmsys_driver_data *data;
	struct drm_atomic_state *suspend_state;

	bool dma_parms_allocated;
};

extern struct platform_driver mtk_ddp_driver;
extern struct platform_driver mtk_disp_color_driver;
extern struct platform_driver mtk_disp_ovl_driver;
extern struct platform_driver mtk_disp_rdma_driver;
extern struct platform_driver mtk_dpi_driver;
extern struct platform_driver mtk_dsi_driver;
extern struct platform_driver mtk_mipi_tx_driver;

#endif /* MTK_DRM_DRV_H */
