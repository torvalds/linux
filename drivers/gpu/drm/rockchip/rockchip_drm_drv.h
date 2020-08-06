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

#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem.h>
#include <drm/rockchip_drm.h>

#include <linux/module.h>
#include <linux/component.h>

#define ROCKCHIP_MAX_FB_BUFFER	3
#define ROCKCHIP_MAX_CONNECTOR	2
#define ROCKCHIP_MAX_CRTC	2

struct drm_device;
struct drm_connector;
struct iommu_domain;

struct rockchip_drm_sub_dev {
	struct list_head list;
	struct drm_connector *connector;
	struct device_node *of_node;
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

struct rockchip_atomic_commit {
	struct drm_atomic_state *state;
	struct drm_device *dev;
	size_t bandwidth;
	unsigned int plane_num;
};

struct rockchip_dclk_pll {
	struct clk *pll;
	unsigned int use_count;
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
	struct drm_tv_connector_state *tv_state;
	int left_margin;
	int right_margin;
	int top_margin;
	int bottom_margin;
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
	int output_type;
	int output_mode;
	int output_bpc;
	int output_flags;
	u32 bus_format;
	u32 bus_flags;
	int yuv_overlay;
	int post_r2y_en;
	int post_y2r_en;
	int post_csc_mode;
	int bcsh_en;
	int color_space;
	int eotf;
	u8 mode_update;
	struct rockchip_hdr_state hdr;
};
#define to_rockchip_crtc_state(s) \
		container_of(s, struct rockchip_crtc_state, base)

struct rockchip_logo {
	dma_addr_t dma_addr;
	void *kvaddr;
	phys_addr_t start;
	phys_addr_t size;
	int count;
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
	struct drm_property *eotf_prop;
	struct drm_property *color_space_prop;
	struct drm_property *global_alpha_prop;
	struct drm_property *blend_mode_prop;
	struct drm_property *alpha_scale_prop;
	struct drm_property *async_commit_prop;
	struct drm_property *share_id_prop;
	struct drm_fb_helper *fbdev_helper;
	struct drm_gem_object *fbdev_bo;
	const struct rockchip_crtc_funcs *crtc_funcs[ROCKCHIP_MAX_CRTC];
	struct drm_atomic_state *state;

	struct rockchip_atomic_commit *commit;
	/* protect async commit */
	struct mutex commit_lock;
	struct work_struct commit_work;
	struct iommu_domain *domain;
	struct gen_pool *secure_buffer_pool;
	/* protect drm_mm on multi-threads */
	struct mutex mm_lock;
	struct drm_mm mm;
	struct rockchip_dclk_pll default_pll;
	struct rockchip_dclk_pll hdmi_pll;
	struct devfreq *devfreq;
	u8 dmc_support;
	struct list_head psr_list;
	struct mutex psr_list_lock;

	/**
	 * @loader_protect
	 * ignore restore_fbdev_mode_atomic when in logo on state
	 */
	bool loader_protect;
};

#ifndef MODULE
void rockchip_free_loader_memory(struct drm_device *drm);
#endif
void rockchip_drm_atomic_work(struct work_struct *work);
int rockchip_drm_dma_attach_device(struct drm_device *drm_dev,
				   struct device *dev);
void rockchip_drm_dma_detach_device(struct drm_device *drm_dev,
				    struct device *dev);
int rockchip_register_crtc_funcs(struct drm_crtc *crtc,
				 const struct rockchip_crtc_funcs *crtc_funcs);
void rockchip_unregister_crtc_funcs(struct drm_crtc *crtc);
int rockchip_drm_wait_vact_end(struct drm_crtc *crtc, unsigned int mstimeout);

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

#if IS_ENABLED(CONFIG_DRM_ROCKCHIP)
int rockchip_drm_crtc_send_mcu_cmd(struct drm_device *drm_dev,
				   struct device_node *np_crtc,
				   u32 type, u32 value);
#else
static inline int rockchip_drm_crtc_send_mcu_cmd(struct drm_device *drm_dev,
						 struct device_node *np_crtc,
						 u32 type, u32 value)
{
	return 0;
}
#endif

extern struct platform_driver cdn_dp_driver;
extern struct platform_driver dw_hdmi_rockchip_pltfm_driver;
extern struct platform_driver dw_mipi_dsi_driver;
extern struct platform_driver inno_hdmi_driver;
extern struct platform_driver rockchip_dp_driver;
extern struct platform_driver rockchip_lvds_driver;
extern struct platform_driver rockchip_tve_driver;
extern struct platform_driver vop_platform_driver;
extern struct platform_driver rockchip_rgb_driver;
#endif /* _ROCKCHIP_DRM_DRV_H_ */
