/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * based on exynos_drm_drv.h
 */

#ifndef _ROCKCHIP_DRM_DRV_H
#define _ROCKCHIP_DRM_DRV_H

#include <drm/drm_atomic_helper.h>
#include <drm/drm_dsc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem.h>
#include <drm/rockchip_drm.h>
#include <linux/module.h>
#include <linux/component.h>

#include <soc/rockchip/rockchip_dmc.h>

#include "../panel/panel-simple.h"

#include "rockchip_drm_debugfs.h"

#define ROCKCHIP_MAX_FB_BUFFER	3
#define ROCKCHIP_MAX_CONNECTOR	2
#define ROCKCHIP_MAX_CRTC	4
#define ROCKCHIP_MAX_LAYER	16


struct drm_device;
struct drm_connector;
struct iommu_domain;

#define VOP_COLOR_KEY_NONE	(0 << 31)
#define VOP_COLOR_KEY_MASK	(1 << 31)

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

#ifndef DRM_FORMAT_NV20
#define DRM_FORMAT_NV20		fourcc_code('N', 'V', '2', '0') /* 2x1 subsampled Cr:Cb plane */
#endif

#ifndef DRM_FORMAT_NV30
#define DRM_FORMAT_NV30		fourcc_code('N', 'V', '3', '0') /* non-subsampled Cr:Cb plane */
#endif

#define RK_IF_PROP_COLOR_DEPTH		"color_depth"
#define RK_IF_PROP_COLOR_FORMAT		"color_format"
#define RK_IF_PROP_COLOR_DEPTH_CAPS	"color_depth_caps"
#define RK_IF_PROP_COLOR_FORMAT_CAPS	"color_format_caps"

enum rockchip_drm_debug_category {
	VOP_DEBUG_PLANE		= BIT(0),
	VOP_DEBUG_OVERLAY	= BIT(1),
	VOP_DEBUG_WB		= BIT(2),
	VOP_DEBUG_CFG_DONE	= BIT(3),
	VOP_DEBUG_VSYNC		= BIT(7),
};

enum rk_if_color_depth {
	RK_IF_DEPTH_8,
	RK_IF_DEPTH_10,
	RK_IF_DEPTH_12,
	RK_IF_DEPTH_16,
	RK_IF_DEPTH_420_10,
	RK_IF_DEPTH_420_12,
	RK_IF_DEPTH_420_16,
	RK_IF_DEPTH_6,
	RK_IF_DEPTH_MAX,
};

enum rk_if_color_format {
	RK_IF_FORMAT_RGB, /* default RGB */
	RK_IF_FORMAT_YCBCR444, /* YCBCR 444 */
	RK_IF_FORMAT_YCBCR422, /* YCBCR 422 */
	RK_IF_FORMAT_YCBCR420, /* YCBCR 420 */
	RK_IF_FORMAT_YCBCR_HQ, /* Highest subsampled YUV */
	RK_IF_FORMAT_YCBCR_LQ, /* Lowest subsampled YUV */
	RK_IF_FORMAT_MAX,
};

struct rockchip_drm_sub_dev {
	struct list_head list;
	struct drm_connector *connector;
	struct device_node *of_node;
	int (*loader_protect)(struct drm_encoder *encoder, bool on);
	void (*oob_hotplug_event)(struct drm_connector *connector);
	void (*update_vfp_for_vrr)(struct drm_connector *connector, struct drm_display_mode *mode,
				   int vfp);
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

struct rockchip_bcsh_state {
	int brightness;
	int contrast;
	int saturation;
	int sin_hue;
	int cos_hue;
};

struct rockchip_crtc {
	struct drm_crtc crtc;
#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	/**
	 * @vop_dump_status the status of vop dump control
	 * @vop_dump_list_head the list head of vop dump list
	 * @vop_dump_list_init_flag init once
	 * @vop_dump_times control the dump times
	 * @frme_count the frame of dump buf
	 */
	enum vop_dump_status vop_dump_status;
	struct list_head vop_dump_list_head;
	bool vop_dump_list_init_flag;
	int vop_dump_times;
	int frame_count;
#endif
};

struct rockchip_dsc_sink_cap {
	/**
	 * @slice_width: the number of pixel columns that comprise the slice width
	 * @slice_height: the number of pixel rows that comprise the slice height
	 * @block_pred: Does block prediction
	 * @native_420: Does sink support DSC with 4:2:0 compression
	 * @bpc_supported: compressed bpc supported by sink : 10, 12 or 16 bpc
	 * @version_major: DSC major version
	 * @version_minor: DSC minor version
	 * @target_bits_per_pixel_x16: bits num after compress and multiply 16
	 */
	u16 slice_width;
	u16 slice_height;
	bool block_pred;
	bool native_420;
	u8 bpc_supported;
	u8 version_major;
	u8 version_minor;
	u16 target_bits_per_pixel_x16;
};

struct rockchip_crtc_state {
	struct drm_crtc_state base;
	int vp_id;
	int output_type;
	int output_mode;
	int output_bpc;
	int output_flags;
	bool enable_afbc;
	/**
	 * @splice_mode: enabled when display a hdisplay > 4096 on rk3588
	 */
	bool splice_mode;

	/**
	 * @hold_mode: enabled when it's:
	 * (1) mcu hold mode
	 * (2) mipi dsi cmd mode
	 * (3) edp psr mode
	 */
	bool hold_mode;
	/**
	 * when enable soft_te, use gpio irq to triggle new fs,
	 * otherwise use hardware te
	 */
	bool soft_te;

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
	u8 dsc_id;
	u8 dsc_enable;

	u8 dsc_slice_num;
	u8 dsc_pixel_num;

	u64 dsc_txp_clk_rate;
	u64 dsc_pxl_clk_rate;
	u64 dsc_cds_clk_rate;

	struct drm_dsc_picture_parameter_set pps;
	struct rockchip_dsc_sink_cap dsc_sink_cap;
	struct rockchip_hdr_state hdr;

	int request_refresh_rate;
	int max_refresh_rate;
	int min_refresh_rate;
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
	struct drm_mm_node logo_reserved_node;
	void *kvaddr;
	phys_addr_t start;
	phys_addr_t size;
	int count;
};

struct loader_cubic_lut {
	bool enable;
	u32 offset;
};

struct rockchip_drm_dsc_cap {
	bool v_1p2;
	bool native_420;
	bool all_bpp;
	u8 bpc_supported;
	u8 max_slices;
	u8 max_lanes;
	u8 max_frl_rate_per_lane;
	u8 total_chunk_kbytes;
	int clk_per_slice;
};

struct ver_26_v0 {
	u8 yuv422_12bit;
	u8 support_2160p_60;
	u8 global_dimming;
	u8 dm_major_ver;
	u8 dm_minor_ver;
	u16 t_min_pq;
	u16 t_max_pq;
	u16 rx;
	u16 ry;
	u16 gx;
	u16 gy;
	u16 bx;
	u16 by;
	u16 wx;
	u16 wy;
} __packed;

struct ver_15_v1 {
	u8 yuv422_12bit;
	u8 support_2160p_60;
	u8 global_dimming;
	u8 dm_version;
	u8 colorimetry;
	u8 t_max_lum;
	u8 t_min_lum;
	u8 rx;
	u8 ry;
	u8 gx;
	u8 gy;
	u8 bx;
	u8 by;
} __packed;

struct ver_12_v1 {
	u8 yuv422_12bit;
	u8 support_2160p_60;
	u8 global_dimming;
	u8 dm_version;
	u8 colorimetry;
	u8 low_latency;
	u8 t_max_lum;
	u8 t_min_lum;
	u8 unique_rx;
	u8 unique_ry;
	u8 unique_gx;
	u8 unique_gy;
	u8 unique_bx;
	u8 unique_by;
} __packed;

struct ver_12_v2 {
	u8 yuv422_12bit;
	u8 backlt_ctrl;
	u8 global_dimming;
	u8 dm_version;
	u8 backlt_min_luma;
	u8 interface;
	u8 yuv444_10b_12b;
	u8 t_min_pq_v2;
	u8 t_max_pq_v2;
	u8 unique_rx;
	u8 unique_ry;
	u8 unique_gx;
	u8 unique_gy;
	u8 unique_bx;
	u8 unique_by;
} __packed;

struct next_hdr_sink_data {
	u8 version;
	struct ver_26_v0 ver_26_v0;
	struct ver_15_v1 ver_15_v1;
	struct ver_12_v1 ver_12_v1;
	struct ver_12_v2 ver_12_v2;
} __packed;

/*
 * Rockchip drm private crtc funcs.
 * @loader_protect: protect loader logo crtc's power
 * @enable_vblank: enable crtc vblank irq.
 * @disable_vblank: disable crtc vblank irq.
 * @bandwidth: report present crtc bandwidth consume.
 * @cancel_pending_vblank: cancel pending vblank.
 * @debugfs_init: init crtc debugfs.
 * @debugfs_dump: debugfs to dump crtc and plane state.
 * @regs_dump: dump vop current register config.
 * @mode_valid: verify that the current mode is supported.
 * @crtc_close: close vop.
 * @crtc_send_mcu_cmd: send mcu panel init cmd.
 * @te_handler: soft te hand for cmd mode panel.
 * @wait_vact_end: wait the last active line.
 */
struct rockchip_crtc_funcs {
	int (*loader_protect)(struct drm_crtc *crtc, bool on);
	int (*enable_vblank)(struct drm_crtc *crtc);
	void (*disable_vblank)(struct drm_crtc *crtc);
	size_t (*bandwidth)(struct drm_crtc *crtc,
			    struct drm_crtc_state *crtc_state,
			    struct dmcfreq_vop_info *vop_bw_info);
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
	void (*te_handler)(struct drm_crtc *crtc);
	int (*wait_vact_end)(struct drm_crtc *crtc, unsigned int mstimeout);
	void (*crtc_standby)(struct drm_crtc *crtc, bool standby);
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

	/* private crtc prop */
	struct drm_property *soc_id_prop;
	struct drm_property *port_id_prop;
	struct drm_property *aclk_prop;
	struct drm_property *bg_prop;
	struct drm_property *line_flag_prop;

	/* private plane prop */
	struct drm_property *eotf_prop;
	struct drm_property *color_space_prop;
	struct drm_property *async_commit_prop;
	struct drm_property *share_id_prop;

	/* private connector prop */
	struct drm_property *connector_id_prop;

	const struct rockchip_crtc_funcs *crtc_funcs[ROCKCHIP_MAX_CRTC];

	struct rockchip_dclk_pll default_pll;
	struct rockchip_dclk_pll hdmi_pll;

	/*
	 * protect some shared overlay resource
	 * OVL_LAYER_SEL/OVL_PORT_SEL
	 */
	struct mutex ovl_lock;

	struct rockchip_drm_vcnt vcnt[ROCKCHIP_MAX_CRTC];
	/**
	 * @loader_protect
	 * ignore restore_fbdev_mode_atomic when in logo on state
	 */
	bool loader_protect;

	dma_addr_t cubic_lut_dma_addr;
	void *cubic_lut_kvaddr;
	struct drm_mm_node *clut_reserved_node;
	struct loader_cubic_lut cubic_lut[ROCKCHIP_MAX_CRTC];
};

void rockchip_connector_update_vfp_for_vrr(struct drm_crtc *crtc, struct drm_display_mode *mode,
					   int vfp);
int rockchip_drm_dma_attach_device(struct drm_device *drm_dev,
				   struct device *dev);
void rockchip_drm_dma_detach_device(struct drm_device *drm_dev,
				    struct device *dev);
int rockchip_drm_wait_vact_end(struct drm_crtc *crtc, unsigned int mstimeout);
int rockchip_register_crtc_funcs(struct drm_crtc *crtc,
				 const struct rockchip_crtc_funcs *crtc_funcs);
void rockchip_unregister_crtc_funcs(struct drm_crtc *crtc);
void rockchip_drm_crtc_standby(struct drm_crtc *crtc, bool standby);

void rockchip_drm_register_sub_dev(struct rockchip_drm_sub_dev *sub_dev);
void rockchip_drm_unregister_sub_dev(struct rockchip_drm_sub_dev *sub_dev);
struct rockchip_drm_sub_dev *rockchip_drm_get_sub_dev(struct device_node *node);
int rockchip_drm_add_modes_noedid(struct drm_connector *connector);
void rockchip_drm_te_handle(struct drm_crtc *crtc);
void drm_mode_convert_to_split_mode(struct drm_display_mode *mode);
void drm_mode_convert_to_origin_mode(struct drm_display_mode *mode);
#if IS_REACHABLE(CONFIG_DRM_ROCKCHIP)
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
uint32_t rockchip_drm_get_bpp(const struct drm_format_info *info);
int rockchip_drm_get_yuv422_format(struct drm_connector *connector,
				   struct edid *edid);
int rockchip_drm_parse_cea_ext(struct rockchip_drm_dsc_cap *dsc_cap,
			       u8 *max_frl_rate_per_lane, u8 *max_lanes, u8 *add_func,
			       const struct edid *edid);
int rockchip_drm_parse_next_hdr(struct next_hdr_sink_data *sink_data,
				const struct edid *edid);
__printf(3, 4)
void rockchip_drm_dbg(const struct device *dev, enum rockchip_drm_debug_category category,
		      const char *format, ...);

extern struct platform_driver cdn_dp_driver;
extern struct platform_driver dw_hdmi_rockchip_pltfm_driver;
extern struct platform_driver dw_mipi_dsi_rockchip_driver;
extern struct platform_driver dw_mipi_dsi2_rockchip_driver;
extern struct platform_driver inno_hdmi_driver;
extern struct platform_driver rockchip_dp_driver;
extern struct platform_driver rockchip_lvds_driver;
extern struct platform_driver vop_platform_driver;
extern struct platform_driver vop2_platform_driver;
extern struct platform_driver rk3066_hdmi_driver;
extern struct platform_driver rockchip_rgb_driver;
extern struct platform_driver rockchip_tve_driver;
extern struct platform_driver dw_dp_driver;
extern struct platform_driver vconn_platform_driver;
extern struct platform_driver vvop_platform_driver;
#endif /* _ROCKCHIP_DRM_DRV_H_ */
