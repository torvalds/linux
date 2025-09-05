/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * based on exynos_drm_drv.h
 */

#ifndef _ROCKCHIP_DRM_DRV_H
#define _ROCKCHIP_DRM_DRV_H

#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem.h>

#include <linux/bits.h>
#include <linux/component.h>
#include <linux/i2c.h>
#include <linux/module.h>

#define ROCKCHIP_MAX_FB_BUFFER	3
#define ROCKCHIP_MAX_CONNECTOR	2
#define ROCKCHIP_MAX_CRTC	4

/*
 * display output interface supported by rockchip lcdc
 */
#define ROCKCHIP_OUT_MODE_P888		0
#define ROCKCHIP_OUT_MODE_BT1120	0
#define ROCKCHIP_OUT_MODE_P666		1
#define ROCKCHIP_OUT_MODE_P565		2
#define ROCKCHIP_OUT_MODE_BT656		5
#define ROCKCHIP_OUT_MODE_S888		8
#define ROCKCHIP_OUT_MODE_S888_DUMMY	12
#define ROCKCHIP_OUT_MODE_YUV420	14
/* for use special outface */
#define ROCKCHIP_OUT_MODE_AAAA		15

/* output flags */
#define ROCKCHIP_OUTPUT_DSI_DUAL	BIT(0)

struct drm_device;
struct drm_connector;
struct iommu_domain;

struct rockchip_crtc_state {
	struct drm_crtc_state base;
	int output_type;
	int output_mode;
	int output_bpc;
	int output_flags;
	bool enable_afbc;
	bool yuv_overlay;
	u32 bus_format;
	u32 bus_flags;
	int color_space;
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
	struct iommu_domain *domain;
	struct device *iommu_dev;
	struct mutex mm_lock;
	struct drm_mm mm;
};

struct rockchip_encoder {
	int crtc_endpoint_id;
	struct drm_encoder encoder;
};

int rockchip_drm_dma_attach_device(struct drm_device *drm_dev,
				   struct device *dev);
void rockchip_drm_dma_detach_device(struct drm_device *drm_dev,
				    struct device *dev);
void rockchip_drm_dma_init_device(struct drm_device *drm_dev,
				  struct device *dev);
int rockchip_drm_wait_vact_end(struct drm_crtc *crtc, unsigned int mstimeout);
int rockchip_drm_encoder_set_crtc_endpoint_id(struct rockchip_encoder *rencoder,
					      struct device_node *np, int port, int reg);
int rockchip_drm_endpoint_is_subdriver(struct device_node *ep);
extern struct platform_driver cdn_dp_driver;
extern struct platform_driver dw_dp_driver;
extern struct platform_driver dw_hdmi_rockchip_pltfm_driver;
extern struct platform_driver dw_hdmi_qp_rockchip_pltfm_driver;
extern struct platform_driver dw_mipi_dsi_rockchip_driver;
extern struct platform_driver dw_mipi_dsi2_rockchip_driver;
extern struct platform_driver inno_hdmi_driver;
extern struct platform_driver rockchip_dp_driver;
extern struct platform_driver rockchip_lvds_driver;
extern struct platform_driver vop_platform_driver;
extern struct platform_driver rk3066_hdmi_driver;
extern struct platform_driver vop2_platform_driver;

static inline struct rockchip_encoder *to_rockchip_encoder(struct drm_encoder *encoder)
{
	return container_of(encoder, struct rockchip_encoder, encoder);
}

#endif /* _ROCKCHIP_DRM_DRV_H_ */
