/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * based on exynos_drm_drv.h
 */

#ifndef _ROCKCHIP_DRM_DRV_H
#define _ROCKCHIP_DRM_DRV_H

#include <drm/drm_fb_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem.h>
#include <drm/rockchip_drm.h>
#include <linux/module.h>
#include <linux/component.h>

#include "../panel/panel-simple.h"

#define ROCKCHIP_MAX_FB_BUFFER	3
#define ROCKCHIP_MAX_CONNECTOR	2
#define ROCKCHIP_MAX_CRTC	4
#define ROCKCHIP_MAX_LAYER	16


struct drm_device;
struct drm_connector;
struct iommu_domain;

#define VOP_OUTPUT_IF_RGB	BIT(0)
#define VOP_OUTPUT_IF_BT1120	BIT(1)
#define VOP_OUTPUT_IF_BT656	BIT(2)
#define VOP_OUTPUT_IF_LVDS0	BIT(3)
#define VOP_OUTPUT_IF_LVDS1	BIT(4)
#define VOP_OUTPUT_IF_MIPI0	BIT(5)
#define VOP_OUTPUT_IF_MIPI1	BIT(6)
#define VOP_OUTPUT_IF_eDP0	BIT(7)
#define VOP_OUTPUT_IF_eDP1	BIT(8)
#define VOP_OUTPUT_IF_DP0	BIT(9)
#define VOP_OUTPUT_IF_DP1	BIT(10)
#define VOP_OUTPUT_IF_HDMI0	BIT(11)
#define VOP_OUTPUT_IF_HDMI1	BIT(12)

struct rockchip_drm_sub_dev {
	struct list_head list;
	struct drm_connector *connector;
	struct device_node *of_node;
	void (*loader_protect)(struct drm_encoder *encoder, bool on);
};

struct rockchip_sdr2hdr_state {
	int sdr2hdr_func;

	bool bt1886eotf_pre_conv_en;
	bool rgb2rgb_pre_conv_en;
	bool rgb2rgb_pre_conv_mode;
	bool st2084oetf_pre_conv_en;

	bool bt1886eotf_post_conv_en;
	bool rgb2rgb_post_conv_en;
	bool rgb2rgb_post_conv_mode;
	bool st2084oetf_post_conv_en;
};

struct rockchip_hdr_state {
	bool pre_overlay;
	bool hdr2sdr_en;
	struct rockchip_sdr2hdr_state sdr2hdr_state;
};

struct rockchip_crtc_state {
	struct drm_crtc_state base;
	int output_type;
	int output_mode;
	int output_bpc;
	int output_flags;
	bool enable_afbc;

	struct drm_tv_connector_state *tv_state;
	int left_margin;
	int right_margin;
	int top_margin;
	int bottom_margin;
	int vdisplay;
	int afbdc_win_format;
	int afbdc_win_width;
	int afbdc_win_height;
	int afbdc_win_ptr;
	int afbdc_win_id;
	int afbdc_en;
	int afbdc_win_vir_width;
	int afbdc_win_xoffset;
	int afbdc_win_yoffset;
	int dsp_layer_sel;
	u32 output_if;
	u32 bus_format;
	u32 bus_flags;
	int yuv_overlay;
	int post_r2y_en;
	int post_y2r_en;
	int post_csc_mode;
	int bcsh_en;
	int color_space;
	int eotf;
	u32 background;
	u32 line_flag;
	u8 mode_update;
	struct rockchip_hdr_state hdr;
};

#define to_rockchip_crtc_state(s) \
		container_of(s, struct rockchip_crtc_state, base)

struct rockchip_drm_vcnt {
	struct drm_pending_vblank_event *event;
	__u32 sequence;
	int pipe;
};

struct rockchip_logo {
	dma_addr_t dma_addr;
	void *kvaddr;
	phys_addr_t start;
	phys_addr_t size;
	int count;
};

struct loader_cubic_lut {
	bool enable;
	u32 offset;
};

/*
 * Rockchip drm private crtc funcs.
 * @loader_protect: protect loader logo crtc's power
 * @enable_vblank: enable crtc vblank irq.
 * @disable_vblank: disable crtc vblank irq.
 * @bandwidth: report present crtc bandwidth consume.
 */
struct rockchip_crtc_funcs {
	int (*loader_protect)(struct drm_crtc *crtc, bool on);
	int (*enable_vblank)(struct drm_crtc *crtc);
	void (*disable_vblank)(struct drm_crtc *crtc);
	size_t (*bandwidth)(struct drm_crtc *crtc,
			    struct drm_crtc_state *crtc_state,
			    unsigned int *plane_num_total);
	void (*cancel_pending_vblank)(struct drm_crtc *crtc,
				      struct drm_file *file_priv);
	int (*debugfs_init)(struct drm_minor *minor, struct drm_crtc *crtc);
	int (*debugfs_dump)(struct drm_crtc *crtc, struct seq_file *s);
	void (*regs_dump)(struct drm_crtc *crtc, struct seq_file *s);
	enum drm_mode_status (*mode_valid)(struct drm_crtc *crtc,
					   const struct drm_display_mode *mode,
					   int output_type);
	void (*crtc_close)(struct drm_crtc *crtc);
	void (*crtc_send_mcu_cmd)(struct drm_crtc *crtc, u32 type, u32 value);
};

struct rockchip_dclk_pll {
	struct clk *pll;
	unsigned int use_count;
};

/*
 * Rockchip drm private structure.
 *
 * @crtc: array of enabled CRTCs, used to map from "pipe" to drm_crtc.
 * @num_pipe: number of pipes for this device.
 * @mm_lock: protect drm_mm on multi-threads.
 */
struct rockchip_drm_private {
	struct rockchip_logo *logo;
	struct drm_fb_helper *fbdev_helper;
	struct drm_gem_object *fbdev_bo;
	struct iommu_domain *domain;
	struct gen_pool *secure_buffer_pool;
	struct mutex mm_lock;
	struct drm_mm mm;
	struct list_head psr_list;
	struct mutex psr_list_lock;
	struct mutex commit_lock;

	struct drm_property *eotf_prop;
	struct drm_property *color_space_prop;
	struct drm_property *global_alpha_prop;
	struct drm_property *blend_mode_prop;
	struct drm_property *alpha_scale_prop;
	struct drm_property *async_commit_prop;
	struct drm_property *share_id_prop;
	struct drm_property *connector_id_prop;

	const struct rockchip_crtc_funcs *crtc_funcs[ROCKCHIP_MAX_CRTC];

	struct rockchip_dclk_pll default_pll;
	struct rockchip_dclk_pll hdmi_pll;
	struct rockchip_drm_vcnt vcnt[ROCKCHIP_MAX_CRTC];
	/**
	 * @loader_protect
	 * ignore restore_fbdev_mode_atomic when in logo on state
	 */
	bool loader_protect;

	dma_addr_t cubic_lut_dma_addr;
	void *cubic_lut_kvaddr;
	struct loader_cubic_lut cubic_lut[ROCKCHIP_MAX_CRTC];
};

int rockchip_drm_dma_attach_device(struct drm_device *drm_dev,
				   struct device *dev);
void rockchip_drm_dma_detach_device(struct drm_device *drm_dev,
				    struct device *dev);
int rockchip_drm_wait_vact_end(struct drm_crtc *crtc, unsigned int mstimeout);
int rockchip_register_crtc_funcs(struct drm_crtc *crtc,
				 const struct rockchip_crtc_funcs *crtc_funcs);
void rockchip_unregister_crtc_funcs(struct drm_crtc *crtc);

void rockchip_drm_register_sub_dev(struct rockchip_drm_sub_dev *sub_dev);
void rockchip_drm_unregister_sub_dev(struct rockchip_drm_sub_dev *sub_dev);
struct rockchip_drm_sub_dev *rockchip_drm_get_sub_dev(struct device_node *node);
int rockchip_drm_add_modes_noedid(struct drm_connector *connector);
#if IS_ENABLED(CONFIG_DRM_ROCKCHIP)
int rockchip_drm_get_sub_dev_type(void);
#else
static inline int rockchip_drm_get_sub_dev_type(void)
{
	return DRM_MODE_CONNECTOR_Unknown;
}
#endif

int rockchip_drm_endpoint_is_subdriver(struct device_node *ep);
uint32_t rockchip_drm_of_find_possible_crtcs(struct drm_device *dev,
					     struct device_node *port);

extern struct platform_driver cdn_dp_driver;
extern struct platform_driver dw_hdmi_rockchip_pltfm_driver;
extern struct platform_driver dw_mipi_dsi_rockchip_driver;
extern struct platform_driver inno_hdmi_driver;
extern struct platform_driver rockchip_dp_driver;
extern struct platform_driver rockchip_lvds_driver;
extern struct platform_driver vop_platform_driver;
extern struct platform_driver vop2_platform_driver;
extern struct platform_driver rk3066_hdmi_driver;
#endif /* _ROCKCHIP_DRM_DRV_H_ */
