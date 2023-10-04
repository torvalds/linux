/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#ifndef MTK_DRM_DRV_H
#define MTK_DRM_DRV_H

#include <linux/io.h>
#include "mtk_drm_ddp_comp.h"

#define MAX_CONNECTOR	2
#define DDP_COMPONENT_DRM_OVL_ADAPTOR (DDP_COMPONENT_ID_MAX + 1)
#define DDP_COMPONENT_DRM_ID_MAX (DDP_COMPONENT_DRM_OVL_ADAPTOR + 1)

enum mtk_drm_crtc_path {
	CRTC_MAIN,
	CRTC_EXT,
	CRTC_THIRD,
	MAX_CRTC,
};

struct device;
struct device_node;
struct drm_crtc;
struct drm_device;
struct drm_fb_helper;
struct drm_property;
struct regmap;

struct mtk_mmsys_driver_data {
	const unsigned int *main_path;
	unsigned int main_len;
	const unsigned int *ext_path;
	unsigned int ext_len;
	const unsigned int *third_path;
	unsigned int third_len;

	bool shadow_register;
	unsigned int mmsys_id;
	unsigned int mmsys_dev_num;
};

struct mtk_drm_private {
	struct drm_device *drm;
	struct device *dma_dev;
	bool mtk_drm_bound;
	bool drm_master;
	struct device *dev;
	struct device_node *mutex_node;
	struct device *mutex_dev;
	struct device *mmsys_dev;
	struct device_node *comp_node[DDP_COMPONENT_DRM_ID_MAX];
	struct mtk_ddp_comp ddp_comp[DDP_COMPONENT_DRM_ID_MAX];
	const struct mtk_mmsys_driver_data *data;
	struct drm_atomic_state *suspend_state;
	unsigned int mbox_index;
	struct mtk_drm_private **all_drm_private;
};

extern struct platform_driver mtk_disp_aal_driver;
extern struct platform_driver mtk_disp_ccorr_driver;
extern struct platform_driver mtk_disp_color_driver;
extern struct platform_driver mtk_disp_gamma_driver;
extern struct platform_driver mtk_disp_merge_driver;
extern struct platform_driver mtk_disp_ovl_adaptor_driver;
extern struct platform_driver mtk_disp_ovl_driver;
extern struct platform_driver mtk_disp_rdma_driver;
extern struct platform_driver mtk_dpi_driver;
extern struct platform_driver mtk_dsi_driver;
extern struct platform_driver mtk_ethdr_driver;
extern struct platform_driver mtk_mdp_rdma_driver;

#endif /* MTK_DRM_DRV_H */
