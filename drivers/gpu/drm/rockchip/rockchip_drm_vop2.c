// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2020 Rockchip Electronics Co., Ltd.
 * Author: Andy Yan <andy.yan@rock-chips.com>
 */
#include <drm/drm.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_flip_work.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_self_refresh_helper.h>

#include <drm/drm_writeback.h>
#ifdef CONFIG_DRM_ANALOGIX_DP
#include <drm/bridge/analogix_dp.h>
#endif
#include <dt-bindings/soc/rockchip-system-status.h>

#include <linux/debugfs.h>
#include <linux/fixp-arith.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/clk-conf.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/component.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/mfd/syscon.h>
#include <linux/delay.h>
#include <linux/swab.h>
#include <linux/sort.h>
#include <linux/rockchip/cpu.h>
#include <linux/workqueue.h>
#include <linux/types.h>
#include <soc/rockchip/rockchip_dmc.h>
#include <soc/rockchip/rockchip-system-status.h>
#include <uapi/linux/videodev2.h>

#include "../drm_crtc_internal.h"
#include "../drm_internal.h"

#include "rockchip_drm_drv.h"
#include "rockchip_drm_gem.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_vop.h"
#include "rockchip_vop_reg.h"
#include "rockchip_post_csc.h"

#define _REG_SET(vop2, name, off, reg, mask, v, relaxed) \
		vop2_mask_write(vop2, off + reg.offset, mask, reg.shift, v, reg.write_mask, relaxed)

#define REG_SET(x, name, off, reg, v, relaxed) \
		_REG_SET(x, name, off, reg, reg.mask, v, relaxed)
#define REG_SET_MASK(x, name, off, reg, mask, v, relaxed) \
		_REG_SET(x, name, off, reg, reg.mask & mask, v, relaxed)

#define REG_GET(vop2, reg) ((vop2_readl(vop2, reg.offset) >> reg.shift) & reg.mask)

#define VOP_CLUSTER_SET(x, win, name, v) \
	do { \
		if (win->regs->cluster) \
			REG_SET(x, name, 0, win->regs->cluster->name, v, true); \
	} while (0)

#define VOP_AFBC_SET(x, win, name, v) \
	do { \
		if (win->regs->afbc) \
			REG_SET(x, name, win->offset, win->regs->afbc->name, v, true); \
	} while (0)

#define VOP_WIN_SET(x, win, name, v) \
		REG_SET(x, name, win->offset, VOP_WIN_NAME(win, name), v, true)

#define VOP_SCL_SET(x, win, name, v) \
		REG_SET(x, name, win->offset, win->regs->scl->name, v, true)

#define VOP_CTRL_SET(x, name, v) \
		REG_SET(x, name, 0, (x)->data->ctrl->name, v, false)

#define VOP_CTRL_GET(x, name) vop2_read_reg(x, 0, &(x)->data->ctrl->name)

#define VOP_INTR_GET(vop2, name) \
		vop2_read_reg(vop2, 0, &vop2->data->ctrl->name)

#define VOP_INTR_SET(vop2, intr, name, v) \
		REG_SET(vop2, name, 0, intr->name, v, false)

#define VOP_MODULE_SET(vop2, module, name, v) \
		REG_SET(vop2, name, 0, module->regs->name, v, false)

#define VOP_INTR_SET_MASK(vop2, intr, name, mask, v) \
		REG_SET_MASK(vop2, name, 0, intr->name, mask, v, false)

#define VOP_INTR_SET_TYPE(vop2, intr, name, type, v) \
	do { \
		int i, reg = 0, mask = 0; \
		for (i = 0; i < intr->nintrs; i++) { \
			if (intr->intrs[i] & type) { \
				reg |= (v) << i; \
				mask |= 1 << i; \
			} \
		} \
		VOP_INTR_SET_MASK(vop2, intr, name, mask, reg); \
	} while (0)

#define VOP_INTR_GET_TYPE(vop2, intr, name, type) \
		vop2_get_intr_type(vop2, intr, &intr->name, type)

#define VOP_MODULE_GET(x, module, name) \
		vop2_read_reg(x, 0, &module->regs->name)

#define VOP_WIN_GET(vop2, win, name) \
		vop2_read_reg(vop2, win->offset, &VOP_WIN_NAME(win, name))

#define VOP_WIN_GET_REG_BAK(vop2, win, name) \
			vop2_read_reg_bak(vop2, win->offset, &VOP_WIN_NAME(win, name))

#define VOP_WIN_NAME(win, name) \
		(vop2_get_win_regs(win, &win->regs->name)->name)

#define VOP_WIN_TO_INDEX(vop2_win) \
	((vop2_win) - (vop2_win)->vop2->win)

#define VOP_GRF_SET(vop2, grf, reg, v) \
	do { \
		if (vop2->data->grf) { \
			vop2_grf_writel(vop2->grf, vop2->data->grf->reg, v); \
		} \
	} while (0)

#define to_vop2_win(x) container_of(x, struct vop2_win, base)
#define to_vop2_plane_state(x) container_of(x, struct vop2_plane_state, base)
#define to_wb_state(x) container_of(x, struct vop2_wb_connector_state, base)
#define output_if_is_hdmi(x)		(x & (VOP_OUTPUT_IF_HDMI0 | VOP_OUTPUT_IF_HDMI1))
#define output_if_is_dp(x)		(x & (VOP_OUTPUT_IF_DP0 | VOP_OUTPUT_IF_DP1))
#define output_if_is_edp(x)		(x & (VOP_OUTPUT_IF_eDP0 | VOP_OUTPUT_IF_eDP1))
#define output_if_is_mipi(x)		(x & (VOP_OUTPUT_IF_MIPI0 | VOP_OUTPUT_IF_MIPI1))
#define output_if_is_lvds(x)		(x & (VOP_OUTPUT_IF_LVDS0 | VOP_OUTPUT_IF_LVDS1))
#define output_if_is_dpi(x)		(x & (VOP_OUTPUT_IF_BT656 | VOP_OUTPUT_IF_BT1120 | \
					      VOP_OUTPUT_IF_RGB))

/*
 * max two jobs a time, one is running(writing back),
 * another one will run in next frame.
 */
#define VOP2_WB_JOB_MAX      2
#define VOP2_SYS_AXI_BUS_NUM 2

#define VOP2_MAX_VP_OUTPUT_WIDTH	4096
/* KHZ */
#define VOP2_MAX_DCLK_RATE		600000
/* KHZ */
#define VOP2_COMMON_ACLK_RATE		500000

enum vop2_data_format {
	VOP2_FMT_ARGB8888 = 0,
	VOP2_FMT_RGB888,
	VOP2_FMT_RGB565,
	VOP2_FMT_XRGB101010,
	VOP2_FMT_YUV420SP,
	VOP2_FMT_YUV422SP,
	VOP2_FMT_YUV444SP,
	VOP2_FMT_YUYV422 = 8,
	VOP2_FMT_YUYV420,
	VOP2_FMT_VYUY422,
	VOP2_FMT_VYUY420,
	VOP2_FMT_YUV420SP_TILE_8x4 = 0x10,
	VOP2_FMT_YUV420SP_TILE_16x2,
	VOP2_FMT_YUV422SP_TILE_8x4,
	VOP2_FMT_YUV422SP_TILE_16x2,
	VOP2_FMT_YUV420SP_10,
	VOP2_FMT_YUV422SP_10,
	VOP2_FMT_YUV444SP_10,
};

enum vop2_afbc_format {
	VOP2_AFBC_FMT_RGB565,
	VOP2_AFBC_FMT_ARGB2101010 = 2,
	VOP2_AFBC_FMT_YUV420_10BIT,
	VOP2_AFBC_FMT_RGB888,
	VOP2_AFBC_FMT_ARGB8888,
	VOP2_AFBC_FMT_YUV420 = 9,
	VOP2_AFBC_FMT_YUV422 = 0xb,
	VOP2_AFBC_FMT_YUV422_10BIT = 0xe,
	VOP2_AFBC_FMT_INVALID = -1,
};

enum vop2_tiled_format {
	VOP2_TILED_8X8_FMT_YUV420SP = 0xc,
	VOP2_TILED_8X8_FMT_YUV422SP,
	VOP2_TILED_8X8_FMT_YUV444SP,
	VOP2_TILED_8X8_FMT_YUV400SP,
	VOP2_TILED_8X8_FMT_YUV420SP_10 = 0x1c,
	VOP2_TILED_8X8_FMT_YUV422SP_10,
	VOP2_TILED_8X8_FMT_YUV444SP_10,
	VOP2_TILED_8X8_FMT_YUV400SP_10,
	VOP2_TILED_FMT_INVALID = -1,
};

enum vop3_tiled_format {
	VOP3_TILED_4X4_FMT_YUV420SP = 0xc,
	VOP3_TILED_4X4_FMT_YUV422SP,
	VOP3_TILED_4X4_FMT_YUV444SP,
	VOP3_TILED_4X4_FMT_YUV400SP,
	VOP3_TILED_4X4_FMT_YUV420SP_10 = 0x1c,
	VOP3_TILED_4X4_FMT_YUV422SP_10,
	VOP3_TILED_4X4_FMT_YUV444SP_10,
	VOP3_TILED_4X4_FMT_YUV400SP_10,

	VOP3_TILED_8X8_FMT_YUV420SP = 0x2c,
	VOP3_TILED_8X8_FMT_YUV422SP,
	VOP3_TILED_8X8_FMT_YUV444SP,
	VOP3_TILED_8X8_FMT_YUV400SP,
	VOP3_TILED_8X8_FMT_YUV420SP_10 = 0x3c,
	VOP3_TILED_8X8_FMT_YUV422SP_10,
	VOP3_TILED_8X8_FMT_YUV444SP_10,
	VOP3_TILED_8X8_FMT_YUV400SP_10,

	VOP3_TILED_FMT_INVALID = -1,
};

enum vop2_hdr_lut_mode {
	VOP2_HDR_LUT_MODE_AXI,
	VOP2_HDR_LUT_MODE_AHB,
};

enum vop2_pending {
	VOP_PENDING_FB_UNREF,
};

enum vop2_layer_phy_id {
	ROCKCHIP_VOP2_CLUSTER0 = 0,
	ROCKCHIP_VOP2_CLUSTER1,
	ROCKCHIP_VOP2_ESMART0,
	ROCKCHIP_VOP2_ESMART1,
	ROCKCHIP_VOP2_SMART0,
	ROCKCHIP_VOP2_SMART1,
	ROCKCHIP_VOP2_CLUSTER2,
	ROCKCHIP_VOP2_CLUSTER3,
	ROCKCHIP_VOP2_ESMART2,
	ROCKCHIP_VOP2_ESMART3,
	ROCKCHIP_VOP2_PHY_ID_INVALID = -1,
};

struct vop2_power_domain {
	struct vop2_power_domain *parent;
	struct vop2 *vop2;
	/*
	 * @lock: protect power up/down procedure.
	 * power on take effect immediately,
	 * power down take effect by vsync.
	 * we must check power_domain_status register
	 * to make sure the power domain is down before
	 * send a power on request.
	 *
	 */
	spinlock_t lock;
	unsigned int ref_count;
	bool on;
	/* @vp_mask: Bit mask of video port of the power domain's
	 * module attached to.
	 * For example: PD_CLUSTER0 belongs to module Cluster0, it's
	 * bitmask is the VP which Cluster0 attached to. PD_ESMART is
	 * shared between Esmart1/2/3, it's bitmask will be all the VP
	 * which Esmart1/2/3 attached to.
	 * This is used to check if we can power off a PD by vsync.
	 */
	uint8_t vp_mask;

	const struct vop2_power_domain_data *data;
	struct list_head list;
	struct delayed_work power_off_work;
};

struct vop2_zpos {
	struct drm_plane *plane;
	int win_phys_id;
	int zpos;
};

union vop2_alpha_ctrl {
	uint32_t val;
	struct {
		/* [0:1] */
		uint32_t color_mode:1;
		uint32_t alpha_mode:1;
		/* [2:3] */
		uint32_t blend_mode:2;
		uint32_t alpha_cal_mode:1;
		/* [5:7] */
		uint32_t factor_mode:3;
		/* [8:9] */
		uint32_t alpha_en:1;
		uint32_t src_dst_swap:1;
		uint32_t reserved:6;
		/* [16:23] */
		uint32_t glb_alpha:8;
	} bits;
};

union vop2_bg_alpha_ctrl {
	uint32_t val;
	struct {
		/* [0:1] */
		uint32_t alpha_en:1;
		uint32_t alpha_mode:1;
		/* [2:3] */
		uint32_t alpha_pre_mul:1;
		uint32_t alpha_sat_mode:1;
		/* [4:7] */
		uint32_t reserved:4;
		/* [8:15] */
		uint32_t glb_alpha:8;
	} bits;
};

struct vop2_alpha {
	union vop2_alpha_ctrl src_color_ctrl;
	union vop2_alpha_ctrl dst_color_ctrl;
	union vop2_alpha_ctrl src_alpha_ctrl;
	union vop2_alpha_ctrl dst_alpha_ctrl;
};

struct vop2_alpha_config {
	bool src_premulti_en;
	bool dst_premulti_en;
	bool src_pixel_alpha_en;
	bool dst_pixel_alpha_en;
	u16 src_glb_alpha_value;
	u16 dst_glb_alpha_value;
};

struct vop2_plane_state {
	struct drm_plane_state base;
	int format;
	int zpos;
	struct drm_rect src;
	struct drm_rect dest;
	dma_addr_t yrgb_mst;
	dma_addr_t uv_mst;
	bool afbc_en;
	bool hdr_in;
	bool hdr2sdr_en;
	bool r2y_en;
	bool y2r_en;
	uint32_t csc_mode;
	uint8_t xmirror_en;
	uint8_t ymirror_en;
	uint8_t rotate_90_en;
	uint8_t rotate_270_en;
	uint8_t afbc_half_block_en;
	uint8_t tiled_en;
	int eotf;
	int color_space;
	int global_alpha;
	int blend_mode;
	uint64_t color_key;
	unsigned long offset;
	int pdaf_data_type;
	bool async_commit;
	struct vop_dump_list *planlist;
};

struct vop2_win {
	const char *name;
	struct vop2 *vop2;
	struct vop2_win *parent;
	struct drm_plane base;

	/*
	 * This is for cluster window
	 *
	 * A cluster window can split as two windows:
	 * a main window and a sub window.
	 */
	bool two_win_mode;

	/**
	 * ---------------------------
	 * |          |              |
	 * | Left     |  Right       |
	 * |          |              |
	 * | Cluster0 |  Cluster1    |
	 * ---------------------------
	 */

	/*
	 * @splice_mode_right: As right part of the screen in splice mode.
	 */
	bool splice_mode_right;

	/**
	 * @splice_win: splice win which used to splice for a plane
	 * hdisplay > 4096
	 */
	struct vop2_win *splice_win;
	struct vop2_win *left_win;

	uint8_t splice_win_id;

	struct vop2_power_domain *pd;

	/**
	 * @phys_id: physical id for cluster0/1, esmart0/1, smart0/1
	 * Will be used as a identification for some register
	 * configuration such as OVL_LAYER_SEL/OVL_PORT_SEL.
	 */
	uint8_t phys_id;

	/**
	 * @win_id: graphic window id, a cluster maybe split into two
	 * graphics windows.
	 */
	uint8_t win_id;
	/**
	 * @area_id: multi display region id in a graphic window, they
	 * share the same win_id.
	 */
	uint8_t area_id;
	/**
	 * @plane_id: unique plane id.
	 */
	uint8_t plane_id;
	/**
	 * @layer_id: id of the layer which the window attached to
	 */
	uint8_t layer_id;
	const uint8_t *layer_sel_id;
	/**
	 * @vp_mask: Bitmask of video_port0/1/2 this win attached to,
	 * one win can only attach to one vp at the one time.
	 */
	uint8_t vp_mask;
	/**
	 * @old_vp_mask: Bitmask of video_port0/1/2 this win attached of last commit,
	 * this is used for trackng the change of VOP2_PORT_SEL register.
	 */
	uint8_t old_vp_mask;
	uint8_t zpos;
	uint32_t offset;
	uint8_t axi_id;
	uint8_t axi_yrgb_id;
	uint8_t axi_uv_id;
	uint8_t scale_engine_num;
	uint8_t possible_crtcs;
	enum drm_plane_type type;
	unsigned int max_upscale_factor;
	unsigned int max_downscale_factor;
	unsigned int supported_rotations;
	const uint8_t *dly;
	/*
	 * vertical/horizontal scale up/down filter mode
	 */
	uint8_t hsu_filter_mode;
	uint8_t hsd_filter_mode;
	uint8_t vsu_filter_mode;
	uint8_t vsd_filter_mode;
	uint8_t hsd_pre_filter_mode;
	uint8_t vsd_pre_filter_mode;

	const struct vop2_win_regs *regs;
	const uint64_t *format_modifiers;
	const uint32_t *formats;
	uint32_t nformats;
	uint64_t feature;
	struct drm_property *feature_prop;
	struct drm_property *input_width_prop;
	struct drm_property *input_height_prop;
	struct drm_property *output_width_prop;
	struct drm_property *output_height_prop;
	struct drm_property *color_key_prop;
	struct drm_property *scale_prop;
	struct drm_property *name_prop;
};

struct vop2_cluster {
	bool splice_mode;
	struct vop2_win *main;
	struct vop2_win *sub;
};

struct vop2_layer {
	uint8_t id;
	/*
	 * @win_phys_id: window id of the layer selected.
	 * Every layer must make sure to select different
	 * windows of others.
	 */
	uint8_t win_phys_id;
	const struct vop2_layer_regs *regs;
};

struct vop2_wb_job {

	bool pending;
	/**
	 * @fs_vsync_cnt: frame start vysnc counter,
	 * used to get the write back complete event;
	 */
	uint32_t fs_vsync_cnt;
};

struct vop2_wb {
	uint8_t vp_id;
	struct drm_writeback_connector conn;
	const struct vop2_wb_regs *regs;
	struct vop2_wb_job jobs[VOP2_WB_JOB_MAX];
	uint8_t job_index;

	/**
	 * @job_lock:
	 *
	 * spinlock to protect the job between vop2_wb_commit and vop2_wb_handler in isr.
	 */
	spinlock_t job_lock;

};

struct vop2_dsc {
	uint8_t id;
	uint8_t max_slice_num;
	uint8_t max_linebuf_depth;	/* used to generate the bitstream */
	uint8_t min_bits_per_pixel;	/* bit num after encoder compress */
	bool enabled;
	char attach_vp_id;
	const struct vop2_dsc_regs *regs;
	struct vop2_power_domain *pd;
};

enum vop2_wb_format {
	VOP2_WB_ARGB8888,
	VOP2_WB_BGR888,
	VOP2_WB_RGB565,
	VOP2_WB_YUV420SP = 4,
	VOP2_WB_INVALID = -1,
};

struct vop2_wb_connector_state {
	struct drm_connector_state base;
	dma_addr_t yrgb_addr;
	dma_addr_t uv_addr;
	enum vop2_wb_format format;
	uint16_t scale_x_factor;
	uint8_t scale_x_en;
	uint8_t scale_y_en;
	uint8_t vp_id;
};

struct vop2_video_port {
	struct rockchip_crtc rockchip_crtc;
	struct rockchip_mcu_timing mcu_timing;
	struct vop2 *vop2;
	struct reset_control *dclk_rst;
	struct clk *dclk;
	struct clk *dclk_parent;
	uint8_t id;
	bool layer_sel_update;
	bool xmirror_en;
	bool need_reset_p2i_flag;
	atomic_t post_buf_empty_flag;
	const struct vop2_video_port_regs *regs;

	struct completion dsp_hold_completion;
	struct completion line_flag_completion;

	/* protected by dev->event_lock */
	struct drm_pending_vblank_event *event;

	struct drm_flip_work fb_unref_work;
	unsigned long pending;

	/**
	 * @hdr_in: Indicate we have a hdr plane input.
	 *
	 */
	bool hdr_in;
	/**
	 * @hdr_out: Indicate the screen want a hdr output
	 * from video port.
	 *
	 */
	bool hdr_out;
	/*
	 * @sdr2hdr_en: All the ui plane need to do sdr2hdr for a hdr_out enabled vp.
	 *
	 */
	bool sdr2hdr_en;
	/**
	 * @skip_vsync: skip on vsync when port_mux changed on this vp.
	 * a win move from one VP to another need wait one vsync until
	 * port_mut take effect before this win can be enabled.
	 *
	 */
	bool skip_vsync;

	/**
	 * @bg_ovl_dly: The timing delay from background layer
	 * to overlay module.
	 */
	u8 bg_ovl_dly;

	/**
	 * @hdr_en: Set when has a hdr video input.
	 */
	int hdr_en;

	/**
	 * -----------------
	 * |      |       |
	 * | Left | Right |
	 * |      |       |
	 * | VP0  |  VP1  |
	 * -----------------
	 * @splice_mode_right: As right part of the screen in splice mode.
	 */
	bool splice_mode_right;

	/**
	 * @hdr10_at_splice_mode: enable hdr10 at splice mode on rk3588.
	 */
	bool hdr10_at_splice_mode;
	/**
	 * @left_vp: VP as left part of the screen in splice mode.
	 */
	struct vop2_video_port *left_vp;

	/**
	 * @win_mask: Bitmask of wins attached to the video port;
	 */
	uint32_t win_mask;
	/**
	 * @nr_layers: active layers attached to the video port;
	 */
	uint8_t nr_layers;

	int cursor_win_id;
	/**
	 * @output_if: output connector attached to the video port,
	 * this flag is maintained in vop driver, updated in crtc_atomic_enable,
	 * cleared in crtc_atomic_disable;
	 */
	u32 output_if;

	/**
	 * @active_tv_state: TV connector related states
	 */
	struct drm_tv_connector_state active_tv_state;

	/**
	 * @lut: store legacy gamma look up table
	 */
	u32 *lut;

	/**
	 * @gamma_lut_len: gamma look up table size
	 */
	u32 gamma_lut_len;

	/**
	 * @gamma_lut_active: gamma states
	 */
	bool gamma_lut_active;

	/**
	 * @lut_dma_rid: lut dma id
	 */
	u16 lut_dma_rid;

	/**
	 * @gamma_lut: atomic gamma look up table
	 */
	struct drm_color_lut *gamma_lut;

	/**
	 * @cubic_lut_len: cubic look up table size
	 */
	u32 cubic_lut_len;

	/**
	 * @cubic_lut_gem_obj: gem obj to store cubic lut
	 */
	struct rockchip_gem_object *cubic_lut_gem_obj;

	/**
	 * @hdr_lut_gem_obj: gem obj to store hdr lut
	 */
	struct rockchip_gem_object *hdr_lut_gem_obj;

	/**
	 * @cubic_lut: cubic look up table
	 */
	struct drm_color_lut *cubic_lut;

	/**
	 * @loader_protect: loader logo protect state
	 */
	bool loader_protect;

	/**
	 * @plane_mask: show the plane attach to this vp,
	 * it maybe init at dts file or uboot driver
	 */
	uint32_t plane_mask;

	/**
	 * @plane_mask_prop: plane mask interaction with userspace
	 */
	struct drm_property *plane_mask_prop;
	/**
	 * @feature_prop: crtc feature interaction with userspace
	 */
	struct drm_property *feature_prop;

	/**
	 * @variable_refresh_rate_prop: crtc variable refresh rate interaction with userspace
	 */
	struct drm_property *variable_refresh_rate_prop;

	/**
	 * @max_refresh_rate_prop: crtc max refresh rate interaction with userspace
	 */
	struct drm_property *max_refresh_rate_prop;

	/**
	 * @min_refresh_rate_prop: crtc min refresh rate interaction with userspace
	 */
	struct drm_property *min_refresh_rate_prop;

	/**
	 * @hdr_ext_data_prop: hdr extend data interaction with userspace
	 */
	struct drm_property *hdr_ext_data_prop;

	int hdrvivid_mode;

	/**
	 * @acm_lut_data_prop: acm lut data interaction with userspace
	 */
	struct drm_property *acm_lut_data_prop;
	/**
	 * @post_csc_data_prop: post csc data interaction with userspace
	 */
	struct drm_property *post_csc_data_prop;
	/**
	 * @output_width_prop: vp max output width prop
	 */
	struct drm_property *output_width_prop;
	/**
	 * @output_dclk_prop: vp max output dclk prop
	 */
	struct drm_property *output_dclk_prop;

	/**
	 * @primary_plane_phy_id: vp primary plane phy id, the primary plane
	 * will be used to show uboot logo and kernel logo
	 */
	enum vop2_layer_phy_id primary_plane_phy_id;

	/**
	 * @refresh_rate_change: indicate whether refresh rate change
	 */
	bool refresh_rate_change;
};

struct vop2_extend_pll {
	struct list_head list;
	struct clk *clk;
	char clk_name[32];
	u32 vp_mask;
};

struct vop2 {
	u32 version;
	struct device *dev;
	struct drm_device *drm_dev;
	struct vop2_dsc dscs[ROCKCHIP_MAX_CRTC];
	struct vop2_video_port vps[ROCKCHIP_MAX_CRTC];
	struct vop2_wb wb;
	struct dentry *debugfs;
	struct drm_info_list *debugfs_files;
	struct drm_prop_enum_list *plane_name_list;
	bool is_iommu_enabled;
	bool is_iommu_needed;
	bool is_enabled;
	bool support_multi_area;
	bool disable_afbc_win;

	/* no move win from one vp to another */
	bool disable_win_move;
	/*
	 * Usually we increase old fb refcount at
	 * atomic_flush and decrease it when next
	 * vsync come, this can make user the fb
	 * not been releasced before vop finish use
	 * it.
	 *
	 * But vop decrease fb refcount by a thread
	 * vop2_unref_fb_work, which may run a little
	 * slow sometimes, so when userspace do a rmfb,
	 *
	 * see drm_mode_rmfb,
	 * it will find the fb refcount is still > 1,
	 * than goto a fallback to init drm_mode_rmfb_work_fn,
	 * this will cost a long time(>10 ms maybe) and block
	 * rmfb work. Some userspace don't have with this(such as vo).
	 *
	 * Don't reference framebuffer refcount by
	 * drm_framebuffer_get as some userspace want
	 * rmfb as soon as possible(nvr vo). And the userspace
	 * should make sure release fb after it receive the vsync.
	 */
	bool skip_ref_fb;

	bool loader_protect;

	bool aclk_rate_reset;
	unsigned long aclk_rate;

	const struct vop2_data *data;
	/* Number of win that registered as plane,
	 * maybe less than the total number of hardware
	 * win.
	 */
	uint32_t registered_num_wins;
	uint8_t used_mixers;
	uint8_t esmart_lb_mode;
	/**
	 * @active_vp_mask: Bitmask of active video ports;
	 */
	uint8_t active_vp_mask;
	uint16_t port_mux_cfg;

	uint32_t *regsbak;
	struct resource *res;
	void __iomem *regs;
	struct regmap *grf;
	struct regmap *sys_grf;
	struct regmap *vo0_grf;
	struct regmap *vo1_grf;
	struct regmap *sys_pmu;

	/* physical map length of vop2 register */
	uint32_t len;

	void __iomem *lut_regs;
	void __iomem *acm_regs;
	/* one time only one process allowed to config the register */
	spinlock_t reg_lock;
	/* lock vop2 irq reg */
	spinlock_t irq_lock;
	/* protects crtc enable/disable */
	struct mutex vop2_lock;

	int irq;

	/*
	 * Some globle resource are shared between all
	 * the vidoe ports(crtcs), so we need a ref counter here.
	 */
	unsigned int enable_count;
	struct clk *hclk;
	struct clk *aclk;
	struct clk *pclk;
	struct reset_control *ahb_rst;
	struct reset_control *axi_rst;

	/* list_head of extend clk */
	struct list_head extend_clk_list_head;
	/* list_head of internal clk */
	struct list_head clk_list_head;
	struct list_head pd_list_head;
	struct work_struct post_buf_empty_work;
	struct workqueue_struct *workqueue;

	struct vop2_layer layers[ROCKCHIP_MAX_LAYER];
	/* must put at the end of the struct */
	struct vop2_win win[];
};

struct vop2_clk {
	struct vop2 *vop2;
	struct list_head list;
	unsigned long rate;
	struct clk_hw hw;
	struct clk_divider div;
	int div_val;
	u8 parent_index;
};

#define to_vop2_clk(_hw) container_of(_hw, struct vop2_clk, hw)

/*
 * bus-format types.
 */
struct drm_bus_format_enum_list {
	int type;
	const char *name;
};

static const struct drm_bus_format_enum_list drm_bus_format_enum_list[] = {
	{ DRM_MODE_CONNECTOR_Unknown, "Unknown" },
	{ MEDIA_BUS_FMT_RGB565_1X16, "RGB565_1X16" },
	{ MEDIA_BUS_FMT_RGB666_1X18, "RGB666_1X18" },
	{ MEDIA_BUS_FMT_RGB666_1X24_CPADHI, "RGB666_1X24_CPADHI" },
	{ MEDIA_BUS_FMT_RGB666_1X7X3_SPWG, "RGB666_1X7X3_SPWG" },
	{ MEDIA_BUS_FMT_YUV8_1X24, "YUV8_1X24" },
	{ MEDIA_BUS_FMT_UYYVYY8_0_5X24, "UYYVYY8_0_5X24" },
	{ MEDIA_BUS_FMT_YUV10_1X30, "YUV10_1X30" },
	{ MEDIA_BUS_FMT_UYYVYY10_0_5X30, "UYYVYY10_0_5X30" },
	{ MEDIA_BUS_FMT_RGB888_3X8, "RGB888_3X8" },
	{ MEDIA_BUS_FMT_RGB888_DUMMY_4X8, "RGB888_DUMMY_4X8" },
	{ MEDIA_BUS_FMT_RGB888_1X24, "RGB888_1X24" },
	{ MEDIA_BUS_FMT_RGB888_1X7X4_SPWG, "RGB888_1X7X4_SPWG" },
	{ MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA, "RGB888_1X7X4_JEIDA" },
	{ MEDIA_BUS_FMT_UYVY8_2X8, "UYVY8_2X8" },
	{ MEDIA_BUS_FMT_YUYV8_1X16, "YUYV8_1X16" },
	{ MEDIA_BUS_FMT_UYVY8_1X16, "UYVY8_1X16" },
	{ MEDIA_BUS_FMT_RGB101010_1X30, "RGB101010_1X30" },
	{ MEDIA_BUS_FMT_YUYV10_1X20, "YUYV10_1X20" },
};

static DRM_ENUM_NAME_FN(drm_get_bus_format_name, drm_bus_format_enum_list)

static inline struct vop2_video_port *to_vop2_video_port(struct drm_crtc *crtc)
{
	struct rockchip_crtc *rockchip_crtc;

	rockchip_crtc = container_of(crtc, struct rockchip_crtc, crtc);

	return container_of(rockchip_crtc, struct vop2_video_port, rockchip_crtc);
}

static void vop2_lock(struct vop2 *vop2)
{
	mutex_lock(&vop2->vop2_lock);
	rockchip_dmcfreq_lock();
}

static void vop2_unlock(struct vop2 *vop2)
{
	rockchip_dmcfreq_unlock();
	mutex_unlock(&vop2->vop2_lock);
}

static inline void vop2_grf_writel(struct regmap *regmap, struct vop_reg reg, u32 v)
{
	u32 val = 0;

	if (IS_ERR_OR_NULL(regmap))
		return;

	if (reg.mask) {
		val = (v << reg.shift) | (reg.mask << (reg.shift + 16));
		regmap_write(regmap, reg.offset, val);
	}
}

static inline uint32_t vop2_grf_readl(struct regmap *regmap, const struct vop_reg *reg)
{
	uint32_t v;

	regmap_read(regmap, reg->offset, &v);

	return v;
}

static inline void vop2_writel(struct vop2 *vop2, uint32_t offset, uint32_t v)
{
	writel(v, vop2->regs + offset);
	vop2->regsbak[offset >> 2] = v;
}

static inline uint32_t vop2_readl(struct vop2 *vop2, uint32_t offset)
{
	return readl(vop2->regs + offset);
}

static inline uint32_t vop2_read_reg(struct vop2 *vop2, uint32_t base,
				     const struct vop_reg *reg)
{
	return (vop2_readl(vop2, base + reg->offset) >> reg->shift) & reg->mask;
}

static inline uint32_t vop2_read_reg_bak(struct vop2 *vop2, uint32_t base,
					 const struct vop_reg *reg)
{
	return (vop2->regsbak[(base + reg->offset) >> 2] >> reg->shift) & reg->mask;
}

static inline uint32_t vop2_read_grf_reg(struct regmap *regmap, const struct vop_reg *reg)
{
	return (vop2_grf_readl(regmap, reg) >> reg->shift) & reg->mask;
}

static inline void vop2_write_reg_uncached(struct vop2 *vop2, const struct vop_reg *reg, uint32_t v)
{
	uint32_t offset = reg->offset;
	uint32_t cached_val = vop2->regsbak[offset >> 2];

	v = (cached_val & ~(reg->mask << reg->shift)) | ((v & reg->mask) << reg->shift);
	writel(v, vop2->regs + offset);
}

static inline void vop2_mask_write(struct vop2 *vop2, uint32_t offset,
				   uint32_t mask, uint32_t shift, uint32_t v,
				   bool write_mask, bool relaxed)
{
	uint32_t cached_val;

	if (!mask)
		return;

	if (write_mask) {
		v = ((v & mask) << shift) | (mask << (shift + 16));
	} else {
		cached_val = vop2->regsbak[offset >> 2];

		v = (cached_val & ~(mask << shift)) | ((v & mask) << shift);
		vop2->regsbak[offset >> 2] = v;
	}

	if (relaxed)
		writel_relaxed(v, vop2->regs + offset);
	else
		writel(v, vop2->regs + offset);
}

static inline u32 vop2_line_to_time(struct drm_display_mode *mode, int line)
{
	u64 val = 1000000000ULL * mode->crtc_htotal * line;

	do_div(val, mode->crtc_clock);
	do_div(val, 1000000);

	return val; /* us */
}

static inline bool vop2_plane_active(struct drm_plane_state *pstate)
{
	if (!pstate || !pstate->fb)
		return false;
	else
		return true;
}

static inline bool is_vop3(struct vop2 *vop2)
{
	if (vop2->version == VOP_VERSION_RK3568 || vop2->version == VOP_VERSION_RK3588)
		return false;
	else
		return true;
}

static bool vop2_soc_is_rk3566(void)
{
	return soc_is_rk3566();
}

static bool vop2_is_mirror_win(struct vop2_win *win)
{
	return soc_is_rk3566() && (win->feature & WIN_FEATURE_MIRROR);
}

static uint64_t vop2_soc_id_fixup(uint64_t soc_id)
{
	switch (soc_id) {
	case 0x3566:
		if (rockchip_get_cpu_version())
			return 0x3566A;
		else
			return 0x3566;
	case 0x3568:
		if (rockchip_get_cpu_version())
			return 0x3568A;
		else
			return 0x3568;
	default:
		return soc_id;
	}
}

static void vop2_crtc_standby(struct drm_crtc *crtc, bool standby)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	if (standby) {
		VOP_MODULE_SET(vop2, vp, standby, 1);
		mdelay(20);
	} else {
		VOP_MODULE_SET(vop2, vp, standby, 0);
	}
}

static inline const struct vop2_win_regs *vop2_get_win_regs(struct vop2_win *win,
							    const struct vop_reg *reg)
{
	if (!reg->mask && win->parent)
		return win->parent->regs;

	return win->regs;
}

static inline uint32_t vop2_get_intr_type(struct vop2 *vop2, const struct vop_intr *intr,
					  const struct vop_reg *reg, int type)
{
	uint32_t val, i;
	uint32_t ret = 0;

	val = vop2_read_reg(vop2, 0, reg);

	for (i = 0; i < intr->nintrs; i++) {
		if ((type & intr->intrs[i]) && (val & 1 << i))
			ret |= intr->intrs[i];
	}

	return ret;
}

/*
 * phys_id is used to identify a main window(Cluster Win/Smart Win, not
 * include the sub win of a cluster or the multi area) that can do
 * overlay in main overlay stage.
 */
static struct vop2_win *vop2_find_win_by_phys_id(struct vop2 *vop2, uint8_t phys_id)
{
	struct vop2_win *win;
	int i;

	for (i = 0; i < vop2->registered_num_wins; i++) {
		win = &vop2->win[i];
		if (win->phys_id == phys_id)
			return win;
	}

	return NULL;
}

static struct vop2_power_domain *vop2_find_pd_by_id(struct vop2 *vop2, uint8_t id)
{
	struct vop2_power_domain *pd, *n;

	list_for_each_entry_safe(pd, n, &vop2->pd_list_head, list) {
		if (pd->data->id == id)
			return pd;
	}

	return NULL;
}

static const struct vop2_connector_if_data *vop2_find_connector_if_data(struct vop2 *vop2, int id)
{
	const struct vop2_connector_if_data *if_data;
	int i;

	for (i = 0; i < vop2->data->nr_conns; i++) {
		if_data = &vop2->data->conn[i];
		if (if_data->id == id)
			return if_data;
	}

	return NULL;
}

static struct drm_crtc *vop2_find_crtc_by_plane_mask(struct vop2 *vop2, uint8_t phys_id)
{
	struct vop2_video_port *vp;
	int i;

	for (i = 0; i < vop2->data->nr_vps; i++) {
		vp = &vop2->vps[i];
		if (vp->plane_mask & BIT(phys_id))
			return &vp->rockchip_crtc.crtc;
	}

	return NULL;
}

static int vop2_clk_reset(struct reset_control *rstc)
{
	int ret;

	if (!rstc)
		return 0;

	ret = reset_control_assert(rstc);
	if (ret < 0)
		DRM_WARN("failed to assert reset\n");
	udelay(10);
	ret = reset_control_deassert(rstc);
	if (ret < 0)
		DRM_WARN("failed to deassert reset\n");

	return ret;
}

static void vop2_load_hdr2sdr_table(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_hdr_table *table = vp_data->hdr_table;
	const struct vop2_video_port_regs *regs = vp->regs;
	uint32_t hdr2sdr_eetf_oetf_yn[33];
	int i;

	for (i = 0; i < 33; i++)
		hdr2sdr_eetf_oetf_yn[i] = table->hdr2sdr_eetf_yn[i] +
				(table->hdr2sdr_bt1886oetf_yn[i] << 16);

	for (i = 0; i < 33; i++)
		vop2_writel(vop2, regs->hdr2sdr_eetf_oetf_y0_offset + i * 4,
			    hdr2sdr_eetf_oetf_yn[i]);

	for (i = 0; i < 9; i++)
		vop2_writel(vop2, regs->hdr2sdr_sat_y0_offset + i * 4,
			    table->hdr2sdr_sat_yn[i]);
}

static void vop2_load_sdr2hdr_table(struct vop2_video_port *vp, int sdr2hdr_tf)
{
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_hdr_table *table = vp_data->hdr_table;
	const struct vop2_video_port_regs *regs = vp->regs;
	uint32_t sdr2hdr_eotf_oetf_yn[65];
	uint32_t sdr2hdr_oetf_dx_dxpow[64];
	int i;

	for (i = 0; i < 65; i++) {
		if (sdr2hdr_tf == SDR2HDR_FOR_BT2020)
			sdr2hdr_eotf_oetf_yn[i] =
				table->sdr2hdr_bt1886eotf_yn_for_bt2020[i] +
				(table->sdr2hdr_st2084oetf_yn_for_bt2020[i] << 18);
		else if (sdr2hdr_tf == SDR2HDR_FOR_HDR)
			sdr2hdr_eotf_oetf_yn[i] =
				table->sdr2hdr_bt1886eotf_yn_for_hdr[i] +
				(table->sdr2hdr_st2084oetf_yn_for_hdr[i] << 18);
		else if (sdr2hdr_tf == SDR2HDR_FOR_HLG_HDR)
			sdr2hdr_eotf_oetf_yn[i] =
				table->sdr2hdr_bt1886eotf_yn_for_hlg_hdr[i] +
				(table->sdr2hdr_st2084oetf_yn_for_hlg_hdr[i] << 18);
	}

	for (i = 0; i < 65; i++)
		vop2_writel(vop2, regs->sdr2hdr_eotf_oetf_y0_offset + i * 4,
			    sdr2hdr_eotf_oetf_yn[i]);

	for (i = 0; i < 64; i++) {
		sdr2hdr_oetf_dx_dxpow[i] = table->sdr2hdr_st2084oetf_dxn[i] +
				(table->sdr2hdr_st2084oetf_dxn_pow2[i] << 16);
		vop2_writel(vop2, regs->sdr2hdr_oetf_dx_pow1_offset + i * 4,
			    sdr2hdr_oetf_dx_dxpow[i]);
	}

	for (i = 0; i < 63; i++)
		vop2_writel(vop2, regs->sdr2hdr_oetf_xn1_offset + i * 4,
			    table->sdr2hdr_st2084oetf_xn[i]);
}

static bool vop2_fs_irq_is_pending(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;

	return VOP_INTR_GET_TYPE(vop2, intr, status, FS_FIELD_INTR);
}

static uint32_t vop2_read_vcnt(struct vop2_video_port *vp)
{
	uint32_t offset =  RK3568_SYS_STATUS0 + (vp->id << 2);
	uint32_t vcnt0, vcnt1;
	int i = 0;

	for (i = 0; i < 10; i++) {
		vcnt0 = vop2_readl(vp->vop2, offset) >> 16;
		vcnt1 = vop2_readl(vp->vop2, offset) >> 16;

		if ((vcnt1 - vcnt0) <= 1)
			break;
	}

	if (i == 10) {
		DRM_DEV_ERROR(vp->vop2->dev, "read VP%d vcnt error: %d %d\n", vp->id, vcnt0, vcnt1);
		vcnt1 = vop2_readl(vp->vop2, offset) >> 16;
	}

	return vcnt1;
}

static void vop2_wait_for_irq_handler(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	bool pending;
	int ret;

	/*
	 * Spin until frame start interrupt status bit goes low, which means
	 * that interrupt handler was invoked and cleared it. The timeout of
	 * 10 msecs is really too long, but it is just a safety measure if
	 * something goes really wrong. The wait will only happen in the very
	 * unlikely case of a vblank happening exactly at the same time and
	 * shouldn't exceed microseconds range.
	 */
	ret = readx_poll_timeout_atomic(vop2_fs_irq_is_pending, vp, pending,
					!pending, 0, 10 * 1000);
	if (ret)
		DRM_DEV_ERROR(vop2->dev, "VOP vblank IRQ stuck for 10 ms\n");

	synchronize_irq(vop2->irq);
}

static bool vop2_vp_done_bit_status(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	u32 done_bits = vop2_readl(vop2, RK3568_REG_CFG_DONE) & BIT(vp->id);

	/*
	 * When done bit is 0, indicate current frame is take effect.
	 */
	return done_bits == 0 ? true : false;
}

static void vop2_wait_for_fs_by_done_bit_status(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	bool done_bit;
	int ret;

	ret = readx_poll_timeout_atomic(vop2_vp_done_bit_status, vp, done_bit,
					done_bit, 0, 50 * 1000);
	if (ret)
		DRM_DEV_ERROR(vop2->dev, "wait vp%d done bit status timeout, vcnt: %d\n",
			      vp->id, vop2_read_vcnt(vp));
}

static uint16_t vop2_read_port_mux(struct vop2 *vop2)
{
	return vop2_readl(vop2, RK3568_OVL_PORT_SEL) & 0xffff;
}

static void vop2_wait_for_port_mux_done(struct vop2 *vop2)
{
	uint16_t port_mux_cfg;
	int ret;

	/*
	 * Spin until the previous port_mux figuration
	 * is done.
	 */
	ret = readx_poll_timeout_atomic(vop2_read_port_mux, vop2, port_mux_cfg,
					port_mux_cfg == vop2->port_mux_cfg, 0, 50 * 1000);
	if (ret)
		DRM_DEV_ERROR(vop2->dev, "wait port_mux done timeout: 0x%x--0x%x\n",
			      port_mux_cfg, vop2->port_mux_cfg);
}

static u32 vop2_read_layer_cfg(struct vop2 *vop2)
{
	return vop2_readl(vop2, RK3568_OVL_LAYER_SEL);
}

static void vop2_wait_for_layer_cfg_done(struct vop2 *vop2, u32 cfg)
{
	u32 atv_layer_cfg;
	int ret;

	/*
	 * Spin until the previous layer configuration is done.
	 */
	ret = readx_poll_timeout_atomic(vop2_read_layer_cfg, vop2, atv_layer_cfg,
					atv_layer_cfg == cfg, 0, 50 * 1000);
	if (ret)
		DRM_DEV_ERROR(vop2->dev, "wait layer cfg done timeout: 0x%x--0x%x\n",
			      atv_layer_cfg, cfg);
}

static int32_t vop2_pending_done_bits(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	struct drm_display_mode *adjusted_mode;
	struct vop2_video_port *done_vp;
	uint32_t done_bits, done_bits_bak;
	uint32_t vp_id;
	uint32_t vcnt;

	done_bits = vop2_readl(vop2, RK3568_REG_CFG_DONE) & 0x7;
	done_bits_bak = done_bits;

	/* no done bit, so no need to wait config done take effect */
	if (done_bits == 0)
		return 0;

	vp_id = ffs(done_bits) - 1;
	/* done bit is same with current vp config done, so no need to wait */
	if (hweight32(done_bits) == 1 && vp_id == vp->id)
		return 0;

	/* have the other one different vp, wait for config done take effect */
	if (hweight32(done_bits) == 1 ||
	    (hweight32(done_bits) == 2 && (done_bits & BIT(vp->id)))) {
		/* two done bit, clear current vp done bit and find the other done bit vp */
		if (done_bits & BIT(vp->id))
			done_bits &= ~BIT(vp->id);
		vp_id = ffs(done_bits) - 1;
		done_vp = &vop2->vps[vp_id];
		adjusted_mode = &done_vp->rockchip_crtc.crtc.state->adjusted_mode;
		vcnt = vop2_read_vcnt(done_vp);
		if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
			vcnt >>= 1;
		/* if close to the last 1/8 frame, wait to next frame */
		if (vcnt > (adjusted_mode->crtc_vtotal * 7 >> 3)) {
			vop2_wait_for_fs_by_done_bit_status(done_vp);
			done_bits = 0;
		}
	} else { /* exist the other two vp done bit */
		struct drm_display_mode *first_mode, *second_mode;
		struct vop2_video_port *first_done_vp, *second_done_vp, *wait_vp;
		uint32_t first_vp_id, second_vp_id;
		uint32_t first_vp_vcnt, second_vp_vcnt;
		uint32_t first_vp_left_vcnt, second_vp_left_vcnt;
		uint32_t first_vp_left_time, second_vp_left_time;
		uint32_t first_vp_safe_time, second_vp_safe_time;
		unsigned int vrefresh;

		first_vp_id = ffs(done_bits) - 1;
		first_done_vp = &vop2->vps[first_vp_id];
		first_mode = &first_done_vp->rockchip_crtc.crtc.state->adjusted_mode;
		/* set last 1/8 frame time as safe section */
		vrefresh = drm_mode_vrefresh(first_mode);
		if (!vrefresh) {
			WARN(1, "%s first vp:%d vrefresh is zero\n", __func__, first_vp_id);
			vrefresh = 60;
		}
		first_vp_safe_time = (1000000 / vrefresh) >> 3;

		done_bits &= ~BIT(first_vp_id);
		second_vp_id = ffs(done_bits) - 1;
		second_done_vp = &vop2->vps[second_vp_id];
		second_mode = &second_done_vp->rockchip_crtc.crtc.state->adjusted_mode;
		/* set last 1/8 frame time as safe section */
		vrefresh = drm_mode_vrefresh(second_mode);
		if (!vrefresh) {
			WARN(1, "%s second vp:%d vrefresh is zero\n", __func__, second_vp_id);
			vrefresh = 60;
		}
		second_vp_safe_time = (1000000 / vrefresh) >> 3;

		first_vp_vcnt = vop2_read_vcnt(first_done_vp);
		if (first_mode->flags & DRM_MODE_FLAG_INTERLACE)
			first_vp_vcnt >>= 1;
		second_vp_vcnt = vop2_read_vcnt(second_done_vp);
		if (second_mode->flags & DRM_MODE_FLAG_INTERLACE)
			second_vp_vcnt >>= 1;

		first_vp_left_vcnt = first_mode->crtc_vtotal - first_vp_vcnt;
		second_vp_left_vcnt = second_mode->crtc_vtotal - second_vp_vcnt;
		first_vp_left_time = vop2_line_to_time(first_mode, first_vp_left_vcnt);
		second_vp_left_time = vop2_line_to_time(second_mode, second_vp_left_vcnt);

		/* if the two vp both at safe section, no need to wait */
		if (first_vp_left_time > first_vp_safe_time &&
		    second_vp_left_time > second_vp_safe_time)
			return done_bits_bak;

		if (first_vp_left_time > second_vp_left_time) {
			if ((first_vp_left_time - second_vp_left_time) > first_vp_safe_time)
				wait_vp = second_done_vp;
			else
				wait_vp = first_done_vp;
		} else {
			if ((second_vp_left_time - first_vp_left_time) > second_vp_safe_time)
				wait_vp = first_done_vp;
			else
				wait_vp = second_done_vp;
		}

		vop2_wait_for_fs_by_done_bit_status(wait_vp);

		done_bits = vop2_readl(vop2, RK3568_REG_CFG_DONE) & 0x7;
	}
	return done_bits;
}

static inline void rk3588_vop2_dsc_cfg_done(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2_dsc *dsc = &vop2->dscs[vcstate->dsc_id];

	if (vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE) {
		dsc = &vop2->dscs[0];
		if (vcstate->dsc_enable)
			VOP_MODULE_SET(vop2, dsc, dsc_cfg_done, 1);
		dsc = &vop2->dscs[1];
		if (vcstate->dsc_enable)
			VOP_MODULE_SET(vop2, dsc, dsc_cfg_done, 1);
	} else {
		if (vcstate->dsc_enable)
			VOP_MODULE_SET(vop2, dsc, dsc_cfg_done, 1);
	}
}

static inline void rk3568_vop2_cfg_done(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	uint32_t done_bits;
	uint32_t val;
	u32 old_layer_sel_val, cfg_layer_sel_val;
	struct vop2_layer *layer = &vop2->layers[0];
	u32 layer_sel_offset = layer->regs->layer_sel.offset;

	/*
	 * This is a workaround, the config done bits of VP0,
	 * VP1, VP2 on RK3568 stands on the first three bits
	 * on REG_CFG_DONE register without mask bit.
	 * If two or three config done events happens one after
	 * another in a very shot time, the flowing config done
	 * write may override the previous config done bit before
	 * it take effect:
	 * 1: config done 0x8001 for VP0
	 * 2: config done 0x8002 for VP1
	 *
	 * 0x8002 may override 0x8001 before it take effect.
	 *
	 * So we do a read | write here.
	 *
	 */
	done_bits = vop2_pending_done_bits(vp);
	val = RK3568_VOP2_GLB_CFG_DONE_EN | BIT(vp->id) | done_bits;
	old_layer_sel_val = vop2_readl(vop2, layer_sel_offset);
	cfg_layer_sel_val = vop2->regsbak[layer_sel_offset >> 2];
	/**
	 * This is rather low probability for miss some done bit.
	 */
	val |= vop2_readl(vop2, RK3568_REG_CFG_DONE) & 0x7;

	rockchip_drm_dbg(vop2->dev, VOP_DEBUG_CFG_DONE, "cfg_done: 0x%x\n", val);

	vop2_writel(vop2, 0, val);

	/**
	 * Make sure the layer sel is take effect when it's updated.
	 */
	if (old_layer_sel_val != cfg_layer_sel_val) {
		vp->layer_sel_update = true;
		vop2_wait_for_fs_by_done_bit_status(vp);
		DRM_DEV_DEBUG(vop2->dev, "vp%d need to wait fs as old layer_sel val[0x%x] != new val[0x%x]\n",
			      vp->id, old_layer_sel_val, cfg_layer_sel_val);
	}
}

static inline void rk3588_vop2_cfg_done(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	const struct vop2_video_port_data *vp_data = &vp->vop2->data->vp[vp->id];
	struct vop2 *vop2 = vp->vop2;
	uint32_t val;

	val = RK3568_VOP2_GLB_CFG_DONE_EN | BIT(vp->id) | (BIT(vp->id) << 16);
	if (vcstate->splice_mode)
		val |= BIT(vp_data->splice_vp_id) | (BIT(vp_data->splice_vp_id) << 16);

	rockchip_drm_dbg(vop2->dev, VOP_DEBUG_CFG_DONE, "cfg_done: 0x%x\n", val);

	vop2_writel(vop2, 0, val);
}

static inline void vop2_wb_cfg_done(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	uint32_t val = RK3568_VOP2_WB_CFG_DONE | (RK3568_VOP2_WB_CFG_DONE << 16) |
		       RK3568_VOP2_GLB_CFG_DONE_EN;
	uint32_t done_bits;
	unsigned long flags;

	if (vop2->version == VOP_VERSION_RK3568) {
		spin_lock_irqsave(&vop2->irq_lock, flags);
		done_bits = vop2_pending_done_bits(vp);
		val |= done_bits;
		vop2_writel(vop2, 0, val);
		spin_unlock_irqrestore(&vop2->irq_lock, flags);
	} else {
		vop2_writel(vop2, 0, val);
	}

}

static inline void vop2_cfg_done(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	if (vop2->version == VOP_VERSION_RK3568)
		return rk3568_vop2_cfg_done(crtc);
	else
		return rk3588_vop2_cfg_done(crtc);
}

/*
 * A PD can power off by vsync when it's module attached to
 * a activated VP.
 */
static uint32_t vop2_power_domain_can_off_by_vsync(struct vop2_power_domain *pd)
{
	struct vop2 *vop2 = pd->vop2;

	if (vop2->active_vp_mask & pd->vp_mask)
		return true;
	else
		return false;
}

/*
 * Read VOP internal power domain on/off status.
 * We should query BISR_STS register in PMU for
 * power up/down status when memory repair is enabled.
 * Return value: 1 for power on, 0 for power off;
 */
static uint32_t vop2_power_domain_status(struct vop2_power_domain *pd)
{
	struct vop2 *vop2 = pd->vop2;

	if (vop2_read_grf_reg(vop2->sys_pmu, &pd->data->regs->bisr_en_status))
		return vop2_read_grf_reg(vop2->sys_pmu, &pd->data->regs->pmu_status);
	else
		return vop2_read_reg(vop2, 0, &pd->data->regs->status) ? 0 : 1;
}

static void vop2_wait_power_domain_off(struct vop2_power_domain *pd)
{
	struct vop2 *vop2 = pd->vop2;
	int val;
	int ret;

	ret = readx_poll_timeout_atomic(vop2_power_domain_status, pd, val, !val, 0, 50 * 1000);

	if (ret)
		DRM_DEV_ERROR(vop2->dev, "wait pd%d off timeout power_ctrl: 0x%x\n",
			      ffs(pd->data->id) - 1, vop2_readl(vop2, 0x34));
}

static void vop2_wait_power_domain_on(struct vop2_power_domain *pd)
{
	struct vop2 *vop2 = pd->vop2;
	int val;
	int ret;

	ret = readx_poll_timeout_atomic(vop2_power_domain_status, pd, val, val, 0, 50 * 1000);
	if (ret)
		DRM_DEV_ERROR(vop2->dev, "wait pd%d on timeout power_ctrl: 0x%x\n",
			      ffs(pd->data->id) - 1, vop2_readl(vop2, 0x34));
}

/*
 * Power domain on take effect immediately
 */
static void vop2_power_domain_on(struct vop2_power_domain *pd)
{
	struct vop2 *vop2 = pd->vop2;

	if (!pd->on) {
		dev_dbg(vop2->dev, "pd%d on\n", ffs(pd->data->id) - 1);
		vop2_wait_power_domain_off(pd);
		VOP_MODULE_SET(vop2, pd->data, pd, 0);
		vop2_wait_power_domain_on(pd);
		pd->on = true;
	}
}

/*
 * Power domain off take effect by vsync.
 */
static void vop2_power_domain_off(struct vop2_power_domain *pd)
{
	struct vop2 *vop2 = pd->vop2;

	dev_dbg(vop2->dev, "pd%d off\n", ffs(pd->data->id) - 1);
	pd->on = false;
	VOP_MODULE_SET(vop2, pd->data, pd, 1);
}

static void vop2_power_domain_get(struct vop2_power_domain *pd)
{
	if (pd->parent)
		vop2_power_domain_get(pd->parent);

	spin_lock(&pd->lock);
	if (pd->ref_count == 0) {
		if (pd->vop2->data->delayed_pd)
			cancel_delayed_work(&pd->power_off_work);
		vop2_power_domain_on(pd);
	}
	pd->ref_count++;
	spin_unlock(&pd->lock);
}

static void vop2_power_domain_put(struct vop2_power_domain *pd)
{
	spin_lock(&pd->lock);

	/*
	 * For a nested power domain(PD_Cluster0 is the parent of PD_CLuster1/2/3)
	 * the parent power domain must be enabled before child power domain
	 * is on.
	 *
	 * So we may met this condition: Cluster0 is not on a activated VP,
	 * but PD_Cluster0 must enabled as one of the child PD_CLUSTER1/2/3 is enabled.
	 * when all child PD is disabled, we want disable the parent
	 * PD(PD_CLUSTER0), but as module CLUSTER0 is not attcthed on a activated VP,
	 * the turn off operation(which is take effect by vsync) will never take effect.
	 * so we will see a "wait pd0 off timeout" log when we turn on PD_CLUSTER0 next time.
	 *
	 * So we have a check here
	 */
	if (--pd->ref_count == 0 && vop2_power_domain_can_off_by_vsync(pd)) {
		if (pd->vop2->data->delayed_pd)
			schedule_delayed_work(&pd->power_off_work, msecs_to_jiffies(2500));
		else
			vop2_power_domain_off(pd);
	}

	spin_unlock(&pd->lock);
	if (pd->parent)
		vop2_power_domain_put(pd->parent);
}

/*
 * Called if the pd ref_count reach 0 after 2.5
 * seconds.
 */
static void vop2_power_domain_off_work(struct work_struct *work)
{
	struct vop2_power_domain *pd;

	pd = container_of(to_delayed_work(work), struct vop2_power_domain, power_off_work);

	spin_lock(&pd->lock);
	if (pd->ref_count == 0)
		vop2_power_domain_off(pd);
	spin_unlock(&pd->lock);
}

static void vop2_win_enable(struct vop2_win *win)
{
	/*
	 * a win such as cursor update by async:
	 * first frame enable win pd, enable win, return without wait vsync
	 * second frame come, but the first frame may still not enabled
	 * in this case, the win pd is turn on by fist frame, so we don't
	 * need get pd again.
	 *
	 * another case:
	 * first frame: disable win, disable pd, return without wait vsync
	 * second frame come very soon, the previous win disable may still not
	 * take effect, but the pd is disable in progress, we should do pd_get
	 * at this situation.
	 *
	 * check the backup register for previous enable operation.
	 */
	if (!VOP_WIN_GET_REG_BAK(win->vop2, win, enable)) {
		if (win->pd) {
			if (win->pd->data->id == VOP2_PD_ESMART)
				return;

			vop2_power_domain_get(win->pd);
			win->pd->vp_mask |= win->vp_mask;
		}
	}
}

static void vop2_win_multi_area_disable(struct vop2_win *parent)
{
	struct vop2 *vop2 = parent->vop2;
	struct vop2_win *area;
	int i;

	for (i = 0; i < vop2->registered_num_wins; i++) {
		area = &vop2->win[i];
		if (area->parent == parent)
			VOP_WIN_SET(vop2, area, enable, 0);
	}
}

static void vop2_win_disable(struct vop2_win *win, bool skip_splice_win)
{
	struct vop2 *vop2 = win->vop2;

	/* Disable the right splice win */
	if (win->splice_win && !skip_splice_win) {
		vop2_win_disable(win->splice_win, false);
		win->splice_win = NULL;
	}

	if (VOP_WIN_GET(vop2, win, enable) || VOP_WIN_GET_REG_BAK(vop2, win, enable)) {
		VOP_WIN_SET(vop2, win, enable, 0);
		if (win->feature & WIN_FEATURE_CLUSTER_MAIN) {
			struct vop2_win *sub_win;
			int i = 0;

			for (i = 0; i < vop2->registered_num_wins; i++) {
				sub_win = &vop2->win[i];

				if ((sub_win->phys_id == win->phys_id) &&
				    (sub_win->feature & WIN_FEATURE_CLUSTER_SUB))
					VOP_WIN_SET(vop2, sub_win, enable, 0);
			}

			VOP_CLUSTER_SET(vop2, win, enable, 0);
		}

		/*
		 * disable all other multi area win if we want disable area0 here
		 */
		if (!win->parent && (win->feature & WIN_FEATURE_MULTI_AREA))
			vop2_win_multi_area_disable(win);

		if (win->pd) {

			/*
			 * Don't dynamic turn on/off PD_ESMART.
			 * (1) There is a design issue for PD_EMSART when attached
			 *     on VP1/2/3, we found it will trigger POST_BUF_EMPTY irq at vp0
			 *     in splice mode.
			 * (2) PD_ESMART will be closed at esmart layers attathed on VPs
			 *     config done + FS, but different VP FS time is different, this
			 *     maybe lead to PD_ESMART closed at wrong time and display error.
			 * (3) PD_ESMART power up maybe have 4 us delay, this will lead to POST_BUF_EMPTY.
			 */
			if (win->pd->data->id == VOP2_PD_ESMART)
				return;

			vop2_power_domain_put(win->pd);
			win->pd->vp_mask &= ~win->vp_mask;
		}
	}

	if (win->left_win && win->splice_mode_right) {
		win->left_win = NULL;
		win->splice_mode_right = false;
	}
}

static inline void vop2_write_lut(struct vop2 *vop2, uint32_t offset, uint32_t v)
{
	writel(v, vop2->lut_regs + offset);
}

static inline uint32_t vop2_read_lut(struct vop2 *vop2, uint32_t offset)
{
	return readl(vop2->lut_regs + offset);
}

static enum vop2_data_format vop2_convert_format(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
		return VOP2_FMT_XRGB101010;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return VOP2_FMT_ARGB8888;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		return VOP2_FMT_RGB888;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		return VOP2_FMT_RGB565;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_YUV420_8BIT:
		return VOP2_FMT_YUV420SP;
	case DRM_FORMAT_NV15:
	case DRM_FORMAT_YUV420_10BIT:
		return VOP2_FMT_YUV420SP_10;
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
		return VOP2_FMT_YUV422SP;
	case DRM_FORMAT_NV20:
	case DRM_FORMAT_Y210:
		return VOP2_FMT_YUV422SP_10;
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV42:
		return VOP2_FMT_YUV444SP;
	case DRM_FORMAT_NV30:
		return VOP2_FMT_YUV444SP_10;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
		return VOP2_FMT_VYUY422;
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_UYVY:
		return VOP2_FMT_YUYV422;
	default:
		DRM_ERROR("unsupported format[%08x]\n", format);
		return -EINVAL;
	}
}

static enum vop2_afbc_format vop2_convert_afbc_format(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
		return VOP2_AFBC_FMT_ARGB2101010;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return VOP2_AFBC_FMT_ARGB8888;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		return VOP2_AFBC_FMT_RGB888;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		return VOP2_AFBC_FMT_RGB565;
	case DRM_FORMAT_YUV420_8BIT:
		return VOP2_AFBC_FMT_YUV420;
	case DRM_FORMAT_YUV420_10BIT:
		return VOP2_AFBC_FMT_YUV420_10BIT;
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_UYVY:
		return VOP2_AFBC_FMT_YUV422;
	case DRM_FORMAT_Y210:
		return VOP2_AFBC_FMT_YUV422_10BIT;

		/* either of the below should not be reachable */
	default:
		DRM_WARN_ONCE("unsupported AFBC format[%08x]\n", format);
		return VOP2_AFBC_FMT_INVALID;
	}

	return VOP2_AFBC_FMT_INVALID;
}

static enum vop2_tiled_format vop2_convert_tiled_format(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		return VOP2_TILED_8X8_FMT_YUV420SP;
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
		return VOP2_TILED_8X8_FMT_YUV422SP;
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV42:
		return VOP2_TILED_8X8_FMT_YUV444SP;
	case DRM_FORMAT_NV15:
		return VOP2_TILED_8X8_FMT_YUV420SP_10;
	case DRM_FORMAT_NV20:
		return VOP2_TILED_8X8_FMT_YUV422SP_10;
	case DRM_FORMAT_NV30:
		return VOP2_TILED_8X8_FMT_YUV444SP_10;
	default:
		DRM_WARN_ONCE("unsupported tiled format[%08x]\n", format);
		return VOP2_TILED_FMT_INVALID;
	}

	return VOP2_TILED_FMT_INVALID;
}

static enum vop3_tiled_format vop3_convert_tiled_format(uint32_t format, uint32_t tile_mode)
{
	switch (format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		return tile_mode == ROCKCHIP_TILED_BLOCK_SIZE_8x8 ?
				VOP3_TILED_8X8_FMT_YUV420SP : VOP3_TILED_4X4_FMT_YUV420SP;
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
		return tile_mode == ROCKCHIP_TILED_BLOCK_SIZE_8x8 ?
				VOP3_TILED_8X8_FMT_YUV422SP : VOP3_TILED_4X4_FMT_YUV422SP;
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV42:
		return tile_mode == ROCKCHIP_TILED_BLOCK_SIZE_8x8 ?
				VOP3_TILED_8X8_FMT_YUV444SP : VOP3_TILED_4X4_FMT_YUV444SP;
	case DRM_FORMAT_NV15:
		return tile_mode == ROCKCHIP_TILED_BLOCK_SIZE_8x8 ?
				VOP3_TILED_8X8_FMT_YUV420SP_10 : VOP3_TILED_4X4_FMT_YUV420SP_10;
	case DRM_FORMAT_NV20:
		return tile_mode == ROCKCHIP_TILED_BLOCK_SIZE_8x8 ?
				VOP3_TILED_8X8_FMT_YUV422SP_10 : VOP3_TILED_4X4_FMT_YUV422SP_10;
	case DRM_FORMAT_NV30:
		return tile_mode == ROCKCHIP_TILED_BLOCK_SIZE_8x8 ?
				VOP3_TILED_8X8_FMT_YUV444SP_10 : VOP3_TILED_4X4_FMT_YUV444SP_10;
	default:
		DRM_WARN_ONCE("unsupported tiled format[%08x]\n", format);
		return VOP3_TILED_FMT_INVALID;
	}

	return VOP3_TILED_FMT_INVALID;
}

static enum vop2_wb_format vop2_convert_wb_format(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_ARGB8888:
		return VOP2_WB_ARGB8888;
	case DRM_FORMAT_BGR888:
		return VOP2_WB_BGR888;
	case DRM_FORMAT_RGB565:
		return VOP2_WB_RGB565;
	case DRM_FORMAT_NV12:
		return VOP2_WB_YUV420SP;
	default:
		DRM_ERROR("unsupported wb format[%08x]\n", format);
		return VOP2_WB_INVALID;
	}
}

static void vop2_set_system_status(struct vop2 *vop2)
{
	if (hweight8(vop2->active_vp_mask) > 1)
		rockchip_set_system_status(SYS_STATUS_DUALVIEW);
	else
		rockchip_clear_system_status(SYS_STATUS_DUALVIEW);
}

static bool vop2_win_rb_swap(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_BGR565:
		return true;
	default:
		return false;
	}
}

static bool vop2_afbc_rb_swap(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV30:
		return true;
	default:
		return false;
	}
}

static bool vop2_afbc_uv_swap(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_Y210:
	case DRM_FORMAT_YUV420_8BIT:
	case DRM_FORMAT_YUV420_10BIT:
		return true;
	default:
		return false;
	}
}

static bool vop2_win_uv_swap(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV15:
	case DRM_FORMAT_NV20:
	case DRM_FORMAT_NV30:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
		return true;
	default:
		return false;
	}
}

static bool vop2_win_dither_up(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_RGB565:
		return true;
	default:
		return false;
	}
}

static bool vop2_output_uv_swap(uint32_t bus_format, uint32_t output_mode)
{
	/*
	 * FIXME:
	 *
	 * There is no media type for YUV444 output,
	 * so when out_mode is AAAA or P888, assume output is YUV444 on
	 * yuv format.
	 *
	 * From H/W testing, YUV444 mode need a rb swap.
	 */
	if (bus_format == MEDIA_BUS_FMT_YVYU8_1X16 ||
	    bus_format == MEDIA_BUS_FMT_VYUY8_1X16 ||
	    bus_format == MEDIA_BUS_FMT_YVYU8_2X8 ||
	    bus_format == MEDIA_BUS_FMT_VYUY8_2X8 ||
	    ((bus_format == MEDIA_BUS_FMT_YUV8_1X24 ||
	      bus_format == MEDIA_BUS_FMT_YUV10_1X30) &&
	     (output_mode == ROCKCHIP_OUT_MODE_AAAA ||
	      output_mode == ROCKCHIP_OUT_MODE_P888)))
		return true;
	else
		return false;
}

static bool vop3_output_rb_swap(uint32_t bus_format, uint32_t output_mode)
{
	/*
	 * The default component order of serial rgb3x8 formats
	 * is BGR. So it is needed to enable RB swap.
	 */
	if (bus_format == MEDIA_BUS_FMT_RGB888_3X8 ||
	    bus_format == MEDIA_BUS_FMT_RGB888_DUMMY_4X8)
		return true;
	else
		return false;
}

static bool vop2_output_yc_swap(uint32_t bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YVYU8_1X16:
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
		return true;
	default:
		return false;
	}
}

static bool is_yuv_output(uint32_t bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_YUYV10_1X20:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_VYUY8_2X8:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YVYU8_1X16:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_VYUY8_1X16:
		return true;
	default:
		return false;
	}
}

static bool is_alpha_support(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
		return true;
	default:
		return false;
	}
}

static inline bool rockchip_afbc(struct drm_plane *plane, u64 modifier)
{
	int i;

	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return false;

	if (!drm_is_afbc(modifier))
		return false;

	for (i = 0 ; i < plane->modifier_count; i++)
		if (plane->modifiers[i] == modifier)
			break;

	return (i < plane->modifier_count) ? true : false;
}

static inline bool rockchip_tiled(struct drm_plane *plane, u64 modifier)
{
	int i;

	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return false;

	if (!IS_ROCKCHIP_TILED_MOD(modifier))
		return false;

	for (i = 0 ; i < plane->modifier_count; i++)
		if (plane->modifiers[i] == modifier)
			break;

	return (i < plane->modifier_count) ? true : false;
}

static bool rockchip_vop2_mod_supported(struct drm_plane *plane, u32 format, u64 modifier)
{
	if (modifier == DRM_FORMAT_MOD_INVALID)
		return false;

	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return true;

	if (!rockchip_afbc(plane, modifier) && !rockchip_tiled(plane, modifier)) {
		DRM_ERROR("Unsupported format modifier 0x%llx\n", modifier);

		return false;
	}

	return vop2_convert_afbc_format(format) >= 0 ||
	       vop2_convert_tiled_format(format) >= 0 ||
	       vop3_convert_tiled_format(format, 0) >= 0;
}

static inline bool vop2_multi_area_sub_window(struct vop2_win *win)
{
	return (win->parent && (win->feature & WIN_FEATURE_MULTI_AREA));
}

static inline bool vop2_cluster_window(struct vop2_win *win)
{
	return  (win->feature & (WIN_FEATURE_CLUSTER_MAIN | WIN_FEATURE_CLUSTER_SUB));
}

static inline bool vop2_cluster_sub_window(struct vop2_win *win)
{
	return (win->feature & WIN_FEATURE_CLUSTER_SUB);
}

static inline bool vop2_has_feature(struct vop2 *vop2, uint64_t feature)
{
	return (vop2->data->feature & feature);
}

/*
 * 0: Full mode, 16 lines for one tail
 * 1: half block mode
 */
static int vop2_afbc_half_block_enable(struct vop2_plane_state *vpstate)
{
	if (vpstate->rotate_270_en || vpstate->rotate_90_en)
		return 0;
	else
		return 1;
}

/*
 * @xoffset: the src x offset of the right win in splice mode, other wise it
 * must be zero.
 */
static uint32_t vop2_afbc_transform_offset(struct vop2_plane_state *vpstate, int xoffset)
{
	struct drm_rect *src = &vpstate->src;
	struct drm_framebuffer *fb = vpstate->base.fb;
	uint32_t bpp = rockchip_drm_get_bpp(fb->format);
	uint32_t vir_width = (fb->pitches[0] << 3) / (bpp ? bpp : 1);
	uint32_t width = drm_rect_width(src) >> 16;
	uint32_t height = drm_rect_height(src) >> 16;
	uint32_t act_xoffset = src->x1 >> 16;
	uint32_t act_yoffset = src->y1 >> 16;
	uint32_t align16_crop = 0;
	uint32_t align64_crop = 0;
	uint32_t height_tmp = 0;
	uint32_t transform_tmp = 0;
	uint8_t transform_xoffset = 0;
	uint8_t transform_yoffset = 0;
	uint8_t top_crop = 0;
	uint8_t top_crop_line_num = 0;
	uint8_t bottom_crop_line_num = 0;

	act_xoffset += xoffset;
	/* 16 pixel align */
	if (height & 0xf)
		align16_crop = 16 - (height & 0xf);

	height_tmp = height + align16_crop;

	/* 64 pixel align */
	if (height_tmp & 0x3f)
		align64_crop = 64 - (height_tmp & 0x3f);

	top_crop_line_num = top_crop << 2;
	if (top_crop == 0)
		bottom_crop_line_num = align16_crop + align64_crop;
	else if (top_crop == 1)
		bottom_crop_line_num = align16_crop + align64_crop + 12;
	else if (top_crop == 2)
		bottom_crop_line_num = align16_crop + align64_crop + 8;

	if (vpstate->xmirror_en) {
		if (vpstate->ymirror_en) {
			if (vpstate->afbc_half_block_en) {
				transform_tmp = act_xoffset + width;
				transform_xoffset = 16 - (transform_tmp & 0xf);
				transform_tmp = bottom_crop_line_num - act_yoffset;
				transform_yoffset = transform_tmp & 0x7;
			} else { //FULL MODEL
				transform_tmp = act_xoffset + width;
				transform_xoffset = 16 - (transform_tmp & 0xf);
				transform_tmp = bottom_crop_line_num - act_yoffset;
				transform_yoffset = (transform_tmp & 0xf);
			}
		} else if (vpstate->rotate_90_en) {
			transform_tmp = bottom_crop_line_num - act_yoffset;
			transform_xoffset = transform_tmp & 0xf;
			transform_tmp = vir_width - width - act_xoffset;
			transform_yoffset = transform_tmp & 0xf;
		} else if (vpstate->rotate_270_en) {
			transform_tmp = top_crop_line_num + act_yoffset;
			transform_xoffset = transform_tmp & 0xf;
			transform_tmp = act_xoffset;
			transform_yoffset = transform_tmp & 0xf;

		} else { //xmir
			if (vpstate->afbc_half_block_en) {
				transform_tmp = act_xoffset + width;
				transform_xoffset = 16 - (transform_tmp & 0xf);
				transform_tmp = top_crop_line_num + act_yoffset;
				transform_yoffset = transform_tmp & 0x7;
			} else {
				transform_tmp = act_xoffset + width;
				transform_xoffset = 16 - (transform_tmp & 0xf);
				transform_tmp = top_crop_line_num + act_yoffset;
				transform_yoffset = transform_tmp & 0xf;
			}
		}
	} else if (vpstate->ymirror_en) {
		if (vpstate->afbc_half_block_en) {
			transform_tmp = act_xoffset;
			transform_xoffset = transform_tmp & 0xf;
			transform_tmp = bottom_crop_line_num - act_yoffset;
			transform_yoffset = transform_tmp & 0x7;
		} else { //full_mode
			transform_tmp = act_xoffset;
			transform_xoffset = transform_tmp & 0xf;
			transform_tmp = bottom_crop_line_num - act_yoffset;
			transform_yoffset = transform_tmp & 0xf;
		}
	} else if (vpstate->rotate_90_en) {
		transform_tmp = bottom_crop_line_num - act_yoffset;
		transform_xoffset = transform_tmp & 0xf;
		transform_tmp = act_xoffset;
		transform_yoffset = transform_tmp & 0xf;
	} else if (vpstate->rotate_270_en) {
		transform_tmp = top_crop_line_num + act_yoffset;
		transform_xoffset = transform_tmp & 0xf;
		transform_tmp = vir_width - width - act_xoffset;
		transform_yoffset = transform_tmp & 0xf;
	} else { //normal
		if (vpstate->afbc_half_block_en) {
			transform_tmp = act_xoffset;
			transform_xoffset = transform_tmp & 0xf;
			transform_tmp = top_crop_line_num + act_yoffset;
			transform_yoffset = transform_tmp & 0x7;
		} else { //full_mode
			transform_tmp = act_xoffset;
			transform_xoffset = transform_tmp & 0xf;
			transform_tmp = top_crop_line_num + act_yoffset;
			transform_yoffset = transform_tmp & 0xf;
		}
	}

	return (transform_xoffset & 0xf) | ((transform_yoffset & 0xf) << 16);
}

static uint32_t vop2_tile_transform_offset(struct vop2_plane_state *vpstate, uint8_t tiled_en)
{
	struct drm_rect *src = &vpstate->src;
	uint32_t act_xoffset = src->x1 >> 16;
	uint32_t act_yoffset = src->y1 >> 16;
	uint8_t transform_xoffset = 0;
	uint8_t transform_yoffset = 0;
	uint32_t tile_size = 1;

	if (tiled_en == 0)
		return 0;

	tile_size = tiled_en == ROCKCHIP_TILED_BLOCK_SIZE_8x8 ? 8 : 4;
	transform_xoffset = act_xoffset & (tile_size - 1);
	transform_yoffset = act_yoffset & (tile_size - 1);

	return (transform_xoffset & 0xf) | ((transform_yoffset & 0xf) << 16);
}

/*
 * A Cluster window has 2048 x 16 line buffer, which can
 * works at 2048 x 16(Full) or 4096 x 8 (Half) mode.
 * for Cluster_lb_mode register:
 * 0: half mode, for plane input width range 2048 ~ 4096
 * 1: half mode, for cluster work at 2 * 2048 plane mode
 * 2: half mode, for rotate_90/270 mode
 *
 */
static int vop2_get_cluster_lb_mode(struct vop2_win *win, struct vop2_plane_state *vpstate)
{
	if (vpstate->rotate_270_en || vpstate->rotate_90_en)
		return 2;
	else if (win->feature & WIN_FEATURE_CLUSTER_SUB)
		return 1;
	else
		return 0;
}

/*
 * bli_sd_factor = (src - 1) / (dst - 1) << 12;
 * avg_sd_factor:
 * bli_su_factor:
 * bic_su_factor:
 * = (src - 1) / (dst - 1) << 16;
 *
 * ygt2 enable: dst get one line from two line of the src
 * ygt4 enable: dst get one line from four line of the src.
 *
 */
#define VOP2_BILI_SCL_DN(src, dst)	(((src - 1) << 12) / (dst - 1))
#define VOP2_COMMON_SCL(src, dst)	(((src - 1) << 16) / (dst - 1))

#define VOP2_BILI_SCL_FAC_CHECK(src, dst, fac)	 \
				(fac * (dst - 1) >> 12 < (src - 1))
#define VOP2_COMMON_SCL_FAC_CHECK(src, dst, fac) \
				(fac * (dst - 1) >> 16 < (src - 1))
#define VOP3_COMMON_HOR_SCL_FAC_CHECK(src, dst, fac) \
					(fac * (dst - 1) >> 16 < (src - 1))

static uint16_t vop2_scale_factor(enum scale_mode mode,
				  int32_t filter_mode,
				  uint32_t src, uint32_t dst)
{
	uint32_t fac = 0;
	int i = 0;

	if (mode == SCALE_NONE)
		return 0;

	/*
	 * A workaround to avoid zero div.
	 */
	if ((dst == 1) || (src == 1)) {
		dst = dst + 1;
		src = src + 1;
	}

	if ((mode == SCALE_DOWN) && (filter_mode == VOP2_SCALE_DOWN_BIL)) {
		fac = VOP2_BILI_SCL_DN(src, dst);
		for (i = 0; i < 100; i++) {
			if (VOP2_BILI_SCL_FAC_CHECK(src, dst, fac))
				break;
			fac -= 1;
			DRM_DEBUG("down fac cali: src:%d, dst:%d, fac:0x%x\n", src, dst, fac);
		}
	} else {
		fac = VOP2_COMMON_SCL(src, dst);
		for (i = 0; i < 100; i++) {
			if (VOP2_COMMON_SCL_FAC_CHECK(src, dst, fac))
				break;
			fac -= 1;
			DRM_DEBUG("up fac cali:  src:%d, dst:%d, fac:0x%x\n", src, dst, fac);
		}
	}

	return fac;
}

static bool vop3_scale_up_fac_check(uint32_t src, uint32_t dst, uint32_t fac, bool is_hor)
{
	if (is_hor)
		return VOP3_COMMON_HOR_SCL_FAC_CHECK(src, dst, fac);
	return VOP2_COMMON_SCL_FAC_CHECK(src, dst, fac);
}

static uint16_t vop3_scale_factor(enum scale_mode mode,
				  uint32_t src, uint32_t dst, bool is_hor)
{
	uint32_t fac = 0;
	int i = 0;

	if (mode == SCALE_NONE)
		return 0;

	/*
	 * A workaround to avoid zero div.
	 */
	if ((dst == 1) || (src == 1)) {
		dst = dst + 1;
		src = src + 1;
	}

	if (mode == SCALE_DOWN) {
		fac = VOP2_BILI_SCL_DN(src, dst);
		for (i = 0; i < 100; i++) {
			if (VOP2_BILI_SCL_FAC_CHECK(src, dst, fac))
				break;
			fac -= 1;
			DRM_DEBUG("down fac cali: src:%d, dst:%d, fac:0x%x\n", src, dst, fac);
		}
	} else {
		fac = VOP2_COMMON_SCL(src, dst);
		for (i = 0; i < 100; i++) {
			if (vop3_scale_up_fac_check(src, dst, fac, is_hor))
				break;
			fac -= 1;
			DRM_DEBUG("up fac cali:  src:%d, dst:%d, fac:0x%x\n", src, dst, fac);
		}
	}

	return fac;
}

static void vop2_setup_scale(struct vop2 *vop2, struct vop2_win *win,
			     uint32_t src_w, uint32_t src_h, uint32_t dst_w,
			     uint32_t dst_h, struct drm_plane_state *pstate)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_win_data *win_data = &vop2_data->win[win->win_id];
	struct vop2_plane_state *vpstate = to_vop2_plane_state(pstate);
	struct drm_framebuffer *fb = pstate->fb;
	uint32_t pixel_format = fb->format->format;
	const struct drm_format_info *info = drm_format_info(pixel_format);
	uint8_t hsub = info->hsub;
	uint8_t vsub = info->vsub;
	uint16_t cbcr_src_w = src_w / hsub;
	uint16_t cbcr_src_h = src_h / vsub;
	uint16_t yrgb_hor_scl_mode, yrgb_ver_scl_mode;
	uint16_t cbcr_hor_scl_mode, cbcr_ver_scl_mode;
	uint16_t hscl_filter_mode, vscl_filter_mode;
	uint8_t xgt2 = 0, xgt4 = 0;
	uint8_t ygt2 = 0, ygt4 = 0;
	uint32_t val;

	if (is_vop3(vop2)) {
		if (src_w >= (4 * dst_w)) {
			xgt4 = 1;
			src_w >>= 2;
		} else if (src_w >= (2 * dst_w)) {
			xgt2 = 1;
			src_w >>= 1;
		}
	}

	/**
	 * The rk3528 is processed as 2 pixel/cycle,
	 * so ygt2/ygt4 needs to be triggered in advance to improve performance
	 * when src_w is bigger than 1920.
	 * dst_h / src_h is at [1, 0.65)     ygt2=0; ygt4=0;
	 * dst_h / src_h is at [0.65, 0.35)  ygt2=1; ygt4=0;
	 * dst_h / src_h is at [0.35, 0)     ygt2=0; ygt4=1;
	 */
	if (vop2->version == VOP_VERSION_RK3528 && src_w > 1920) {
		if (src_h >= (100 * dst_h / 35)) {
			ygt4 = 1;
			src_h >>= 2;
		} else if ((src_h >= 100 * dst_h / 65) && (src_h < 100 * dst_h / 35)) {
			ygt2 = 1;
			src_h >>= 1;
		}
	} else {
		if (src_h >= (4 * dst_h)) {
			ygt4 = 1;
			src_h >>= 2;
		} else if (src_h >= (2 * dst_h)) {
			ygt2 = 1;
			src_h >>= 1;
		}
	}

	yrgb_hor_scl_mode = scl_get_scl_mode(src_w, dst_w);
	yrgb_ver_scl_mode = scl_get_scl_mode(src_h, dst_h);

	if (yrgb_hor_scl_mode == SCALE_UP)
		hscl_filter_mode = win_data->hsu_filter_mode;
	else
		hscl_filter_mode = win_data->hsd_filter_mode;

	if (yrgb_ver_scl_mode == SCALE_UP)
		vscl_filter_mode = win_data->vsu_filter_mode;
	else
		vscl_filter_mode = win_data->vsd_filter_mode;

	/*
	 * RK3568 VOP Esmart/Smart dsp_w should be even pixel
	 * at scale down mode
	 */
	if (!(win->feature & WIN_FEATURE_AFBDC) && !is_vop3(vop2)) {
		if ((yrgb_hor_scl_mode == SCALE_DOWN) && (dst_w & 0x1)) {
			dev_dbg(vop2->dev, "%s dst_w[%d] should align as 2 pixel\n", win->name, dst_w);
			dst_w += 1;
		}
	}

	if (is_vop3(vop2)) {
		bool xgt_en = false, xavg_en = false;

		val = vop3_scale_factor(yrgb_hor_scl_mode, src_w, dst_w, true);
		VOP_SCL_SET(vop2, win, scale_yrgb_x, val);
		val = vop3_scale_factor(yrgb_ver_scl_mode, src_h, dst_h, false);
		VOP_SCL_SET(vop2, win, scale_yrgb_y, val);

		if (win_data->hsd_pre_filter_mode == VOP3_PRE_SCALE_DOWN_AVG)
			xavg_en = xgt2 || xgt4;
		else
			xgt_en = xgt2 || xgt4;

		VOP_SCL_SET(vop2, win, xgt_en, xgt_en);
		VOP_SCL_SET(vop2, win, xavg_en, xavg_en);
		VOP_SCL_SET(vop2, win, xgt_mode, xgt2 ? 0 : 1);
	} else {
		val = vop2_scale_factor(yrgb_hor_scl_mode, hscl_filter_mode, src_w, dst_w);
		VOP_SCL_SET(vop2, win, scale_yrgb_x, val);
		val = vop2_scale_factor(yrgb_ver_scl_mode, vscl_filter_mode, src_h, dst_h);
		VOP_SCL_SET(vop2, win, scale_yrgb_y, val);
	}

	/* vop2 and linear mode only can support gt */
	if (!is_vop3(vop2) ||
	    (!vpstate->afbc_en && !vpstate->tiled_en) ||
	    win_data->vsd_pre_filter_mode == VOP3_PRE_SCALE_DOWN_GT) {
		VOP_SCL_SET(vop2, win, vsd_yrgb_gt4, ygt4);
		VOP_SCL_SET(vop2, win, vsd_yrgb_gt2, ygt2);
		VOP_SCL_SET(vop2, win, vsd_avg4, 0);
		VOP_SCL_SET(vop2, win, vsd_avg2, 0);
	} else {
		VOP_SCL_SET(vop2, win, vsd_yrgb_gt4, 0);
		VOP_SCL_SET(vop2, win, vsd_yrgb_gt2, 0);
		VOP_SCL_SET(vop2, win, vsd_avg4, ygt4);
		VOP_SCL_SET(vop2, win, vsd_avg2, ygt2);
	}

	VOP_SCL_SET(vop2, win, yrgb_hor_scl_mode, yrgb_hor_scl_mode);
	VOP_SCL_SET(vop2, win, yrgb_ver_scl_mode, yrgb_ver_scl_mode);

	VOP_SCL_SET(vop2, win, yrgb_hscl_filter_mode, hscl_filter_mode);
	VOP_SCL_SET(vop2, win, yrgb_vscl_filter_mode, vscl_filter_mode);

	if (info->is_yuv) {
		ygt4 = ygt2 = 0;

		if (!is_vop3(vop2) ||
		    (!vpstate->afbc_en && !vpstate->tiled_en) ||
		    win_data->vsd_pre_filter_mode == VOP3_PRE_SCALE_DOWN_GT) {
			if (vop2->version == VOP_VERSION_RK3528 && src_w > 1920) {
				if (cbcr_src_h >= (100 * dst_h / 35))
					ygt4 = 1;
				else if ((cbcr_src_h >= 100 * dst_h / 65) && (cbcr_src_h < 100 * dst_h / 35))
					ygt2 = 1;
			} else {
				if (cbcr_src_h >= (4 * dst_h))
					ygt4 = 1;
				else if (cbcr_src_h >= (2 * dst_h))
					ygt2 = 1;
			}

			if (ygt4)
				cbcr_src_h >>= 2;
			else if (ygt2)
				cbcr_src_h >>= 1;
		}
		VOP_SCL_SET(vop2, win, vsd_cbcr_gt4, ygt4);
		VOP_SCL_SET(vop2, win, vsd_cbcr_gt2, ygt2);

		if (!is_vop3(vop2)) {
			cbcr_hor_scl_mode = scl_get_scl_mode(cbcr_src_w, dst_w);
			cbcr_ver_scl_mode = scl_get_scl_mode(cbcr_src_h, dst_h);

			val = vop2_scale_factor(cbcr_hor_scl_mode, hscl_filter_mode,
						cbcr_src_w, dst_w);
			VOP_SCL_SET(vop2, win, scale_cbcr_x, val);
			val = vop2_scale_factor(cbcr_ver_scl_mode, vscl_filter_mode,
						cbcr_src_h, dst_h);
			VOP_SCL_SET(vop2, win, scale_cbcr_y, val);

			VOP_SCL_SET(vop2, win, cbcr_hor_scl_mode, cbcr_hor_scl_mode);
			VOP_SCL_SET(vop2, win, cbcr_ver_scl_mode, cbcr_ver_scl_mode);
			VOP_SCL_SET(vop2, win, cbcr_hscl_filter_mode, hscl_filter_mode);
			VOP_SCL_SET(vop2, win, cbcr_vscl_filter_mode, vscl_filter_mode);
		}
	}
}

static int vop2_convert_csc_mode(int csc_mode, int bit_depth)
{
	switch (csc_mode) {
	case V4L2_COLORSPACE_SMPTE170M:
	case V4L2_COLORSPACE_470_SYSTEM_M:
	case V4L2_COLORSPACE_470_SYSTEM_BG:
		return CSC_BT601L;
	case V4L2_COLORSPACE_REC709:
	case V4L2_COLORSPACE_SMPTE240M:
	case V4L2_COLORSPACE_DEFAULT:
		if (bit_depth == CSC_13BIT_DEPTH)
			return CSC_BT709L_13BIT;
		else
			return CSC_BT709L;
	case V4L2_COLORSPACE_JPEG:
		return CSC_BT601F;
	case V4L2_COLORSPACE_BT2020:
		if (bit_depth == CSC_13BIT_DEPTH)
			return CSC_BT2020L_13BIT;
		else
			return CSC_BT2020;
	case V4L2_COLORSPACE_BT709F:
		if (bit_depth == CSC_10BIT_DEPTH) {
			DRM_WARN("Unsupported bt709f at 10bit csc depth, use bt601f instead\n");
			return CSC_BT601F;
		} else {
			return CSC_BT709F_13BIT;
		}
	case V4L2_COLORSPACE_BT2020F:
		if (bit_depth == CSC_10BIT_DEPTH) {
			DRM_WARN("Unsupported bt2020f at 10bit csc depth, use bt601f instead\n");
			return CSC_BT601F;
		} else {
			return CSC_BT2020F_13BIT;
		}
	default:
		return CSC_BT709L;
	}
}

static bool vop2_is_allwin_disabled(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	unsigned long win_mask = vp->win_mask;
	struct vop2_win *win;
	int phys_id;

	for_each_set_bit(phys_id, &win_mask, ROCKCHIP_MAX_LAYER) {
		win = vop2_find_win_by_phys_id(vop2, phys_id);
		if (VOP_WIN_GET(vop2, win, enable) != 0)
			return false;
	}

	return true;
}

static void vop2_disable_all_planes_for_crtc(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct vop2_win *win;
	unsigned long win_mask = vp->win_mask;
	int phys_id, ret;
	bool active, need_wait_win_disabled = false;

	for_each_set_bit(phys_id, &win_mask, ROCKCHIP_MAX_LAYER) {
		win = vop2_find_win_by_phys_id(vop2, phys_id);
		need_wait_win_disabled |= VOP_WIN_GET(vop2, win, enable);
		vop2_win_disable(win, false);
	}

	if (need_wait_win_disabled) {
		vop2_cfg_done(crtc);
		ret = readx_poll_timeout_atomic(vop2_is_allwin_disabled, crtc,
						active, active, 0, 500 * 1000);
		if (ret)
			DRM_DEV_ERROR(vop2->dev, "wait win close timeout\n");
	}
}

/*
 * colorspace path:
 *      Input        Win csc                     Output
 * 1. YUV(2020)  --> Y2R->2020To709->R2Y   --> YUV_OUTPUT(601/709)
 *    RGB        --> R2Y                  __/
 *
 * 2. YUV(2020)  --> bypasss               --> YUV_OUTPUT(2020)
 *    RGB        --> 709To2020->R2Y       __/
 *
 * 3. YUV(2020)  --> Y2R->2020To709        --> RGB_OUTPUT(709)
 *    RGB        --> R2Y                  __/
 *
 * 4. YUV(601/709)-> Y2R->709To2020->R2Y   --> YUV_OUTPUT(2020)
 *    RGB        --> 709To2020->R2Y       __/
 *
 * 5. YUV(601/709)-> bypass                --> YUV_OUTPUT(709)
 *    RGB        --> R2Y                  __/
 *
 * 6. YUV(601/709)-> bypass                --> YUV_OUTPUT(601)
 *    RGB        --> R2Y(601)             __/
 *
 * 7. YUV        --> Y2R(709)              --> RGB_OUTPUT(709)
 *    RGB        --> bypass               __/
 *
 * 8. RGB        --> 709To2020->R2Y        --> YUV_OUTPUT(2020)
 *
 * 9. RGB        --> R2Y(709)              --> YUV_OUTPUT(709)
 *
 * 10. RGB       --> R2Y(601)              --> YUV_OUTPUT(601)
 *
 * 11. RGB       --> bypass                --> RGB_OUTPUT(709)
 */

static void vop2_setup_csc_mode(struct vop2_video_port *vp,
				struct vop2_plane_state *vpstate)
{
	struct drm_plane_state *pstate = &vpstate->base;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(vp->rockchip_crtc.crtc.state);
	int is_input_yuv = pstate->fb->format->is_yuv;
	int is_output_yuv = vcstate->yuv_overlay;
	int input_csc = vpstate->color_space;
	int output_csc = vcstate->color_space;
	struct vop2_win *win = to_vop2_win(pstate->plane);
	int csc_y2r_bit_depth = CSC_10BIT_DEPTH;

	if (win->feature & WIN_FEATURE_Y2R_13BIT_DEPTH)
		csc_y2r_bit_depth = CSC_13BIT_DEPTH;

	vpstate->y2r_en = 0;
	vpstate->r2y_en = 0;
	vpstate->csc_mode = 0;

	if (is_vop3(vp->vop2)) {
		if (vpstate->hdr_in) {
			if (is_input_yuv) {
				vpstate->y2r_en = 1;
				vpstate->csc_mode = vop2_convert_csc_mode(input_csc,
									  CSC_13BIT_DEPTH);
			}
			return;
		} else if (vp->sdr2hdr_en) {
			if (is_input_yuv) {
				vpstate->y2r_en = 1;
				vpstate->csc_mode = vop2_convert_csc_mode(input_csc,
									  csc_y2r_bit_depth);
			}
			return;
		}
	} else {
		/* hdr2sdr and sdr2hdr will do csc itself */
		if (vpstate->hdr2sdr_en) {
			/*
			 * This is hdr2sdr enabled plane
			 * If it's RGB layer do hdr2sdr, we need to do r2y before send to hdr2sdr,
			 * because hdr2sdr only support yuv input.
			 */
			if (!is_input_yuv) {
				vpstate->r2y_en = 1;
				vpstate->csc_mode = vop2_convert_csc_mode(output_csc,
									  CSC_10BIT_DEPTH);
			}
			return;
		} else if (!vpstate->hdr_in && vp->sdr2hdr_en) {
			/*
			 * This is sdr2hdr enabled plane
			 * If it's YUV layer do sdr2hdr, we need to do y2r before send to sdr2hdr,
			 * because sdr2hdr only support rgb input.
			 */
			if (is_input_yuv) {
				vpstate->y2r_en = 1;
				vpstate->csc_mode = vop2_convert_csc_mode(input_csc,
									  csc_y2r_bit_depth);
			}
			return;
		}
	}

	if (is_input_yuv && !is_output_yuv) {
		vpstate->y2r_en = 1;
		vpstate->csc_mode = vop2_convert_csc_mode(input_csc, csc_y2r_bit_depth);
	} else if (!is_input_yuv && is_output_yuv) {
		vpstate->r2y_en = 1;
		vpstate->csc_mode = vop2_convert_csc_mode(output_csc, CSC_10BIT_DEPTH);
	}
}

static void vop2_axi_irqs_enable(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop_intr *intr;
	uint32_t irqs = BUS_ERROR_INTR;
	uint32_t i;

	for (i = 0; i < vop2_data->nr_axi_intr; i++) {
		intr = &vop2_data->axi_intr[i];
		VOP_INTR_SET_TYPE(vop2, intr, clear, irqs, 1);
		VOP_INTR_SET_TYPE(vop2, intr, enable, irqs, 1);
	}
}

static uint32_t vop2_read_and_clear_axi_irqs(struct vop2 *vop2, int index)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop_intr *intr = &vop2_data->axi_intr[index];
	uint32_t irqs = BUS_ERROR_INTR;
	uint32_t val;

	val = VOP_INTR_GET_TYPE(vop2, intr, status, irqs);
	if (val)
		VOP_INTR_SET_TYPE(vop2, intr, clear, val, 1);

	return val;
}

static void vop2_dsp_hold_valid_irq_enable(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;

	unsigned long flags;

	if (WARN_ON(!vop2->is_enabled))
		return;

	spin_lock_irqsave(&vop2->irq_lock, flags);

	VOP_INTR_SET_TYPE(vop2, intr, clear, DSP_HOLD_VALID_INTR, 1);
	VOP_INTR_SET_TYPE(vop2, intr, enable, DSP_HOLD_VALID_INTR, 1);

	spin_unlock_irqrestore(&vop2->irq_lock, flags);
}

static void vop2_dsp_hold_valid_irq_disable(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	unsigned long flags;

	if (WARN_ON(!vop2->is_enabled))
		return;

	spin_lock_irqsave(&vop2->irq_lock, flags);

	VOP_INTR_SET_TYPE(vop2, intr, enable, DSP_HOLD_VALID_INTR, 0);

	spin_unlock_irqrestore(&vop2->irq_lock, flags);
}

static void vop2_debug_irq_enable(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	uint32_t irqs = POST_BUF_EMPTY_INTR;

	VOP_INTR_SET_TYPE(vop2, intr, clear, irqs, 1);
	VOP_INTR_SET_TYPE(vop2, intr, enable, irqs, 1);
}

/*
 * (1) each frame starts at the start of the Vsync pulse which is signaled by
 *     the "FRAME_SYNC" interrupt.
 * (2) the active data region of each frame ends at dsp_vact_end
 * (3) we should program this same number (dsp_vact_end) into dsp_line_frag_num,
 *      to get "LINE_FLAG" interrupt at the end of the active on screen data.
 *
 * VOP_INTR_CTRL0.dsp_line_frag_num = VOP_DSP_VACT_ST_END.dsp_vact_end
 * Interrupts
 * LINE_FLAG -------------------------------+
 * FRAME_SYNC ----+                         |
 *                |                         |
 *                v                         v
 *                | Vsync | Vbp |  Vactive  | Vfp |
 *                        ^     ^           ^     ^
 *                        |     |           |     |
 *                        |     |           |     |
 * dsp_vs_end ------------+     |           |     |   VOP_DSP_VTOTAL_VS_END
 * dsp_vact_start --------------+           |     |   VOP_DSP_VACT_ST_END
 * dsp_vact_end ----------------------------+     |   VOP_DSP_VACT_ST_END
 * dsp_total -------------------------------------+   VOP_DSP_VTOTAL_VS_END
 */

static int vop2_core_clks_enable(struct vop2 *vop2)
{
	int ret;

	ret = clk_enable(vop2->hclk);
	if (ret < 0)
		return ret;

	ret = clk_enable(vop2->aclk);
	if (ret < 0)
		goto err_disable_hclk;

	ret = clk_enable(vop2->pclk);
	if (ret < 0)
		goto err_disable_aclk;

	return 0;

err_disable_aclk:
	clk_disable(vop2->aclk);
err_disable_hclk:
	clk_disable(vop2->hclk);
	return ret;
}

static void vop2_core_clks_disable(struct vop2 *vop2)
{
	clk_disable(vop2->pclk);
	clk_disable(vop2->aclk);
	clk_disable(vop2->hclk);
}

static void vop2_wb_connector_reset(struct drm_connector *connector)
{
	struct vop2_wb_connector_state *wb_state;

	if (connector->state) {
		__drm_atomic_helper_connector_destroy_state(connector->state);
		kfree(connector->state);
		connector->state = NULL;
	}

	wb_state = kzalloc(sizeof(*wb_state), GFP_KERNEL);
	if (wb_state)
		__drm_atomic_helper_connector_reset(connector, &wb_state->base);
}

static enum drm_connector_status
vop2_wb_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void vop2_wb_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static struct drm_connector_state *
vop2_wb_connector_duplicate_state(struct drm_connector *connector)
{
	struct vop2_wb_connector_state *wb_state;

	if (WARN_ON(!connector->state))
		return NULL;

	wb_state = kzalloc(sizeof(*wb_state), GFP_KERNEL);
	if (!wb_state)
		return NULL;

	__drm_atomic_helper_connector_duplicate_state(connector, &wb_state->base);

	return &wb_state->base;
}

static const struct drm_connector_funcs vop2_wb_connector_funcs = {
	.reset = vop2_wb_connector_reset,
	.detect = vop2_wb_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = vop2_wb_connector_destroy,
	.atomic_duplicate_state = vop2_wb_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int vop2_wb_connector_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	int i;

	for (i = 0; i < 2; i++) {
		mode = drm_mode_create(connector->dev);
		if (!mode)
			break;

		mode->type = DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER;
		mode->clock = 148500 >> i;
		mode->hdisplay = 1920 >> i;
		mode->hsync_start = 1930 >> i;
		mode->hsync_end = 1940 >> i;
		mode->htotal = 1990 >> i;
		mode->vdisplay = 1080 >> i;
		mode->vsync_start = 1090 >> i;
		mode->vsync_end = 1100 >> i;
		mode->vtotal = 1110 >> i;
		mode->flags = 0;

		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
	}
	return i;
}

static enum drm_mode_status
vop2_wb_connector_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{

	struct drm_writeback_connector *wb_conn;
	struct vop2_wb *wb;
	struct vop2 *vop2;
	int w, h;

	wb_conn = container_of(connector, struct drm_writeback_connector, base);
	wb = container_of(wb_conn, struct vop2_wb, conn);
	vop2 = container_of(wb, struct vop2, wb);
	w = mode->hdisplay;
	h = mode->vdisplay;


	if (w > vop2->data->wb->max_output.width)
		return MODE_BAD_HVALUE;

	if (h > vop2->data->wb->max_output.height)
		return MODE_BAD_VVALUE;

	return MODE_OK;
}

static inline bool
vop2_wb_connector_changed_only(struct drm_crtc_state *cstate, struct drm_connector *conn)
{
	struct drm_crtc_state *old_state;
	u32 changed_connectors;

	old_state = drm_atomic_get_old_crtc_state(cstate->state, cstate->crtc);
	changed_connectors = cstate->connector_mask ^ old_state->connector_mask;

	return BIT(drm_connector_index(conn)) == changed_connectors;
}

static int vop2_wb_encoder_atomic_check(struct drm_encoder *encoder,
			       struct drm_crtc_state *cstate,
			       struct drm_connector_state *conn_state)
{
	struct vop2_wb_connector_state *wb_state = to_wb_state(conn_state);
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(cstate);
	struct vop2_video_port *vp = to_vop2_video_port(cstate->crtc);
	struct drm_framebuffer *fb;
	struct drm_gem_object *obj, *uv_obj;
	struct rockchip_gem_object *rk_obj, *rk_uv_obj;

	/*
	 * No need for a full modested when the only connector changed is the
	 * writeback connector.
	 */
	if (cstate->connectors_changed &&
	    vop2_wb_connector_changed_only(cstate, conn_state->connector)) {
		cstate->connectors_changed = false;
		DRM_DEBUG("VP%d force change connectors_changed to false when only wb changed\n", vp->id);
	}
	if (!conn_state->writeback_job || !conn_state->writeback_job->fb)
		return 0;

	fb = conn_state->writeback_job->fb;
	DRM_DEV_DEBUG(vp->vop2->dev, "%d x % d\n", fb->width, fb->height);

	if (!fb->format->is_yuv && is_yuv_output(vcstate->bus_format)) {
		DRM_ERROR("YUV2RGB is not supported by writeback\n");
		return -EINVAL;
	}

	if ((fb->width > cstate->mode.hdisplay) ||
	    ((fb->height < cstate->mode.vdisplay) &&
	    (fb->height != (cstate->mode.vdisplay >> 1)))) {
		DRM_DEBUG_KMS("Invalid framebuffer size %ux%u, Only support x scale down and 1/2 y scale down\n",
				fb->width, fb->height);
		return -EINVAL;
	}

	wb_state->scale_x_factor = vop2_scale_factor(SCALE_DOWN, VOP2_SCALE_DOWN_BIL,
						      cstate->mode.hdisplay, fb->width);
	wb_state->scale_x_en = (fb->width < cstate->mode.hdisplay) ? 1 : 0;
	wb_state->scale_y_en = (fb->height < cstate->mode.vdisplay) ? 1 : 0;

	wb_state->format = vop2_convert_wb_format(fb->format->format);
	if (wb_state->format < 0) {
		struct drm_format_name_buf format_name;

		DRM_DEBUG_KMS("Invalid pixel format %s\n",
			      drm_get_format_name(fb->format->format,
						  &format_name));
		return -EINVAL;
	}

	wb_state->vp_id = vp->id;
	obj = fb->obj[0];
	rk_obj = to_rockchip_obj(obj);
	wb_state->yrgb_addr = rk_obj->dma_addr + fb->offsets[0];

	if (fb->format->is_yuv) {
		uv_obj = fb->obj[1];
		rk_uv_obj = to_rockchip_obj(uv_obj);

		wb_state->uv_addr = rk_uv_obj->dma_addr + fb->offsets[1];
	}

	return 0;
}

static const struct drm_encoder_helper_funcs vop2_wb_encoder_helper_funcs = {
	.atomic_check = vop2_wb_encoder_atomic_check,
};

static const struct drm_connector_helper_funcs vop2_wb_connector_helper_funcs = {
	.get_modes = vop2_wb_connector_get_modes,
	.mode_valid = vop2_wb_connector_mode_valid,
};


static int vop2_wb_connector_init(struct vop2 *vop2, int nr_crtcs)
{
	const struct vop2_data *vop2_data = vop2->data;
	int ret;

	vop2->wb.regs = vop2_data->wb->regs;
	vop2->wb.conn.encoder.possible_crtcs = (1 << nr_crtcs) - 1;
	spin_lock_init(&vop2->wb.job_lock);
	drm_connector_helper_add(&vop2->wb.conn.base, &vop2_wb_connector_helper_funcs);

	ret = drm_writeback_connector_init(vop2->drm_dev, &vop2->wb.conn,
					   &vop2_wb_connector_funcs,
					   &vop2_wb_encoder_helper_funcs,
					   vop2_data->wb->formats,
					   vop2_data->wb->nformats);
	if (ret)
		DRM_DEV_ERROR(vop2->dev, "writeback connector init failed\n");
	return ret;
}

static void vop2_wb_connector_destory(struct vop2 *vop2)
{
	drm_encoder_cleanup(&vop2->wb.conn.encoder);
	drm_connector_cleanup(&vop2->wb.conn.base);
}

static void vop2_wb_irqs_enable(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop_intr *intr = &vop2_data->axi_intr[0];
	uint32_t irqs = WB_UV_FIFO_FULL_INTR | WB_YRGB_FIFO_FULL_INTR;

	VOP_INTR_SET_TYPE(vop2, intr, clear, irqs, 1);
	VOP_INTR_SET_TYPE(vop2, intr, enable, irqs, 1);
}

static uint32_t vop2_read_and_clear_wb_irqs(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop_intr *intr = &vop2_data->axi_intr[0];
	uint32_t irqs = WB_UV_FIFO_FULL_INTR | WB_YRGB_FIFO_FULL_INTR;
	uint32_t val;

	val = VOP_INTR_GET_TYPE(vop2, intr, status, irqs);
	if (val)
		VOP_INTR_SET_TYPE(vop2, intr, clear, val, 1);


	return val;
}

static void vop2_wb_commit(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct vop2_wb *wb = &vop2->wb;
	struct drm_writeback_connector *wb_conn = &wb->conn;
	struct drm_connector_state *conn_state = wb_conn->base.state;
	struct vop2_wb_connector_state *wb_state;
	unsigned long flags;
	uint32_t fifo_throd;
	uint8_t r2y;

	if (!conn_state)
		return;
	wb_state = to_wb_state(conn_state);

	if (wb_state->vp_id != vp->id)
		return;

	if (conn_state->writeback_job && conn_state->writeback_job->fb) {
		struct drm_framebuffer *fb = conn_state->writeback_job->fb;

		rockchip_drm_dbg(vop2->dev, VOP_DEBUG_WB,
				 "Enable wb %ux%u fmt: %u pitches: %d addr: %pad\n",
				 fb->width, fb->height, wb_state->format,
				 fb->pitches[0], &wb_state->yrgb_addr);

		drm_writeback_queue_job(wb_conn, conn_state);
		conn_state->writeback_job = NULL;

		spin_lock_irqsave(&wb->job_lock, flags);
		wb->jobs[wb->job_index].pending = true;
		wb->job_index++;
		if (wb->job_index >= VOP2_WB_JOB_MAX)
			wb->job_index = 0;
		spin_unlock_irqrestore(&wb->job_lock, flags);

		fifo_throd = fb->pitches[0] >> 4;
		if (fifo_throd >= vop2->data->wb->fifo_depth)
			fifo_throd = vop2->data->wb->fifo_depth;
		r2y = !vcstate->yuv_overlay && fb->format->is_yuv;

		/*
		 * the vp_id register config done immediately
		 */
		VOP_MODULE_SET(vop2, wb, vp_id, wb_state->vp_id);
		VOP_MODULE_SET(vop2, wb, format, wb_state->format);
		VOP_MODULE_SET(vop2, wb, yrgb_mst, wb_state->yrgb_addr);
		VOP_MODULE_SET(vop2, wb, uv_mst, wb_state->uv_addr);
		VOP_MODULE_SET(vop2, wb, fifo_throd, fifo_throd);
		VOP_MODULE_SET(vop2, wb, scale_x_factor, wb_state->scale_x_factor);
		VOP_MODULE_SET(vop2, wb, scale_x_en, wb_state->scale_x_en);
		VOP_MODULE_SET(vop2, wb, scale_y_en, wb_state->scale_y_en);
		VOP_MODULE_SET(vop2, wb, r2y_en, r2y);
		VOP_MODULE_SET(vop2, wb, enable, 1);
		vop2_wb_irqs_enable(vop2);
		VOP_CTRL_SET(vop2, wb_dma_finish_and_en, 1);
	}
}

static void rk3568_crtc_load_lut(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	int dle = 0, i = 0;
	u8 vp_enable_gamma_nr = 0;

	for (i = 0; i < vop2->data->nr_vps; i++) {
		struct vop2_video_port *_vp = &vop2->vps[i];

		if (VOP_MODULE_GET(vop2, _vp, dsp_lut_en))
			vp_enable_gamma_nr++;
	}

	if (vop2->data->nr_gammas &&
	    vp_enable_gamma_nr >= vop2->data->nr_gammas &&
	    VOP_MODULE_GET(vop2, vp, dsp_lut_en) == 0) {
		DRM_INFO("only support %d gamma\n", vop2->data->nr_gammas);

		return;
	}

	spin_lock(&vop2->reg_lock);
	VOP_MODULE_SET(vop2, vp, dsp_lut_en, 0);
	vop2_cfg_done(crtc);
	spin_unlock(&vop2->reg_lock);

#define CTRL_GET(name) VOP_MODULE_GET(vop2, vp, name)
	readx_poll_timeout(CTRL_GET, dsp_lut_en, dle, !dle, 5, 33333);

	VOP_CTRL_SET(vop2, gamma_port_sel, vp->id);
	for (i = 0; i < vp->gamma_lut_len; i++)
		vop2_write_lut(vop2, i << 2, vp->lut[i]);

	spin_lock(&vop2->reg_lock);

	VOP_MODULE_SET(vop2, vp, dsp_lut_en, 1);
	vop2_write_reg_uncached(vop2, &vp->regs->gamma_update_en, 1);
	vop2_cfg_done(crtc);
	vp->gamma_lut_active = true;

	spin_unlock(&vop2->reg_lock);
#undef CTRL_GET
}

static void rk3588_crtc_load_lut(struct drm_crtc *crtc, u32 *lut)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	int i = 0;

	spin_lock(&vop2->reg_lock);

	VOP_CTRL_SET(vop2, gamma_port_sel, vp->id);
	for (i = 0; i < vp->gamma_lut_len; i++)
		vop2_write_lut(vop2, i << 2, lut[i]);

	VOP_MODULE_SET(vop2, vp, dsp_lut_en, 1);
	vop2_write_reg_uncached(vop2, &vp->regs->gamma_update_en, 1);
	vp->gamma_lut_active = true;

	spin_unlock(&vop2->reg_lock);
}

static void vop2_crtc_load_lut(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	if (!vop2->is_enabled || !vp->lut || !vop2->lut_regs)
		return;

	if (WARN_ON(!drm_modeset_is_locked(&crtc->mutex)))
		return;

	if (vop2->version == VOP_VERSION_RK3568) {
		rk3568_crtc_load_lut(crtc);
	} else {
		struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
		const struct vop2_video_port_data *vp_data = &vop2->data->vp[vp->id];
		struct vop2_video_port *splice_vp = &vop2->vps[vp_data->splice_vp_id];

		rk3588_crtc_load_lut(&vp->rockchip_crtc.crtc, vp->lut);
		if (vcstate->splice_mode)
			rk3588_crtc_load_lut(&splice_vp->rockchip_crtc.crtc, vp->lut);
		vop2_cfg_done(crtc);
	}
	/*
	 * maybe appear the following case:
	 * -> set gamma
	 * -> config done
	 * -> atomic commit
	 *  --> update win format
	 *  --> update win address
	 *  ---> here maybe meet vop hardware frame start, and triggle some config take affect.
	 *  ---> as only some config take affect, this maybe lead to iommu pagefault.
	 *  --> update win size
	 *  --> update win other parameters
	 * -> config done
	 *
	 * so we add vop2_wait_for_fs_by_done_bit_status() to make sure the first config done take
	 * effect and then to do next frame config.
	 */
	if (VOP_MODULE_GET(vop2, vp, standby) == 0)
		vop2_wait_for_fs_by_done_bit_status(vp);
}

static void rockchip_vop2_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red,
					    u16 green, u16 blue, int regno)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	u32 lut_len = vp->gamma_lut_len;
	u32 r, g, b;

	if (regno >= lut_len || !vp->lut)
		return;

	r = red * (lut_len - 1) / 0xffff;
	g = green * (lut_len - 1) / 0xffff;
	b = blue * (lut_len - 1) / 0xffff;
	vp->lut[regno] = b * lut_len * lut_len + g * lut_len + r;
}

static void rockchip_vop2_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red,
				       u16 *green, u16 *blue, int regno)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	u32 lut_len = vp->gamma_lut_len;
	u32 r, g, b;

	if (regno >= lut_len || !vp->lut)
		return;

	b = (vp->lut[regno] / lut_len / lut_len) & (lut_len - 1);
	g = (vp->lut[regno] / lut_len) & (lut_len - 1);
	r = vp->lut[regno] & (lut_len - 1);
	*red = r * 0xffff / (lut_len - 1);
	*green = g * 0xffff / (lut_len - 1);
	*blue = b * 0xffff / (lut_len - 1);
}

static int vop2_crtc_legacy_gamma_set(struct drm_crtc *crtc, u16 *red,
				      u16 *green, u16 *blue, uint32_t size,
				      struct drm_modeset_acquire_ctx *ctx)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	int i;

	if (!vp->lut)
		return -EINVAL;

	if (size > vp->gamma_lut_len) {
		DRM_ERROR("gamma size[%d] out of video port%d gamma lut len[%d]\n",
			  size, vp->id, vp->gamma_lut_len);
		return -ENOMEM;
	}
	for (i = 0; i < size; i++)
		rockchip_vop2_crtc_fb_gamma_set(crtc, red[i], green[i],
						blue[i], i);
	vop2_crtc_load_lut(crtc);

	return 0;
}

static int vop2_crtc_atomic_gamma_set(struct drm_crtc *crtc,
				      struct drm_crtc_state *old_state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_color_lut *lut = vp->gamma_lut;
	unsigned int i;

	for (i = 0; i < vp->gamma_lut_len; i++)
		rockchip_vop2_crtc_fb_gamma_set(crtc, lut[i].red, lut[i].green,
						lut[i].blue, i);
	vop2_crtc_load_lut(crtc);

	return 0;
}

static int vop2_crtc_atomic_cubic_lut_set(struct drm_crtc *crtc,
					  struct drm_crtc_state *old_state)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct rockchip_drm_private *private = crtc->dev->dev_private;
	struct drm_color_lut *lut = vp->cubic_lut;
	struct vop2 *vop2 = vp->vop2;
	u32 *cubic_lut_kvaddr;
	dma_addr_t cubic_lut_mst;
	unsigned int i;

	if (!vp->cubic_lut_len) {
		DRM_ERROR("Video Port%d unsupported 3D lut\n", vp->id);
		return -ENODEV;
	}

	if (!private->cubic_lut[vp->id].enable) {
		if (!vp->cubic_lut_gem_obj) {
			size_t size = (vp->cubic_lut_len + 1) / 2 * 16;

			vp->cubic_lut_gem_obj = rockchip_gem_create_object(crtc->dev, size, true, 0);
			if (IS_ERR(vp->cubic_lut_gem_obj))
				return -ENOMEM;
		}

		cubic_lut_kvaddr = (u32 *)vp->cubic_lut_gem_obj->kvaddr;
		cubic_lut_mst = vp->cubic_lut_gem_obj->dma_addr;
	} else {
		cubic_lut_kvaddr = private->cubic_lut[vp->id].offset + private->cubic_lut_kvaddr;
		cubic_lut_mst = private->cubic_lut[vp->id].offset + private->cubic_lut_dma_addr;
	}

	for (i = 0; i < vp->cubic_lut_len / 2; i++) {
		*cubic_lut_kvaddr++ = (lut[2 * i].red & 0xfff) +
					((lut[2 * i].green & 0xfff) << 12) +
					((lut[2 * i].blue & 0xff) << 24);
		*cubic_lut_kvaddr++ = ((lut[2 * i].blue & 0xf00) >> 8) +
					((lut[2 * i + 1].red & 0xfff) << 4) +
					((lut[2 * i + 1].green & 0xfff) << 16) +
					((lut[2 * i + 1].blue & 0xf) << 28);
		*cubic_lut_kvaddr++ = (lut[2 * i + 1].blue & 0xff0) >> 4;
		*cubic_lut_kvaddr++ = 0;
	}

	if (vp->cubic_lut_len % 2) {
		*cubic_lut_kvaddr++ = (lut[2 * i].red & 0xfff) +
					((lut[2 * i].green & 0xfff) << 12) +
					((lut[2 * i].blue & 0xff) << 24);
		*cubic_lut_kvaddr++ = (lut[2 * i].blue & 0xf00) >> 8;
		*cubic_lut_kvaddr++ = 0;
		*cubic_lut_kvaddr = 0;
	}

	VOP_MODULE_SET(vop2, vp, lut_dma_rid, vp->lut_dma_rid);
	VOP_MODULE_SET(vop2, vp, cubic_lut_mst, cubic_lut_mst);
	VOP_MODULE_SET(vop2, vp, cubic_lut_update_en, 1);
	VOP_MODULE_SET(vop2, vp, cubic_lut_en, 1);
	VOP_CTRL_SET(vop2, lut_dma_en, 1);

	if (vcstate->splice_mode) {
		const struct vop2_video_port_data *vp_data = &vop2->data->vp[vp->id];
		struct vop2_video_port *splice_vp = &vop2->vps[vp_data->splice_vp_id];

		VOP_MODULE_SET(vop2, splice_vp, cubic_lut_mst, cubic_lut_mst);
		VOP_MODULE_SET(vop2, splice_vp, cubic_lut_update_en, 1);
		VOP_MODULE_SET(vop2, splice_vp, cubic_lut_en, 1);
	}

	return 0;
}

static void vop2_attach_cubic_lut_prop(struct drm_crtc *crtc, unsigned int cubic_lut_size)
{
	struct rockchip_drm_private *private = crtc->dev->dev_private;

	drm_object_attach_property(&crtc->base, private->cubic_lut_prop, 0);
	drm_object_attach_property(&crtc->base, private->cubic_lut_size_prop, cubic_lut_size);
}

static void vop2_cubic_lut_init(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data;
	struct vop2_video_port *vp;
	struct drm_crtc *crtc;
	int i;

	for (i = 0; i < vop2_data->nr_vps; i++) {
		vp = &vop2->vps[i];
		crtc = &vp->rockchip_crtc.crtc;
		if (!crtc->dev)
			continue;
		vp_data = &vop2_data->vp[vp->id];
		vp->cubic_lut_len = vp_data->cubic_lut_len;

		if (vp->cubic_lut_len)
			vop2_attach_cubic_lut_prop(crtc, vp->cubic_lut_len);
	}
}

static int vop2_core_clks_prepare_enable(struct vop2 *vop2)
{
	int ret;

	ret = clk_prepare_enable(vop2->hclk);
	if (ret < 0) {
		dev_err(vop2->dev, "failed to enable hclk - %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vop2->aclk);
	if (ret < 0) {
		dev_err(vop2->dev, "failed to enable aclk - %d\n", ret);
		goto err;
	}

	ret = clk_prepare_enable(vop2->pclk);
	if (ret < 0) {
		dev_err(vop2->dev, "failed to enable pclk - %d\n", ret);
		goto err1;
	}

	return 0;
err1:
	clk_disable_unprepare(vop2->aclk);
err:
	clk_disable_unprepare(vop2->hclk);

	return ret;
}

/*
 * VOP2 architecture
 *
 +----------+   +-------------+
 |  Cluster |   | Sel 1 from 6
 |  window0 |   |    Layer0   |              +---------------+    +-------------+    +-----------+
 +----------+   +-------------+              |N from 6 layers|    |             |    | 1 from 3  |
 +----------+   +-------------+              |   Overlay0    |    | Video Port0 |    |    RGB    |
 |  Cluster |   | Sel 1 from 6|              |               |    |             |    +-----------+
 |  window1 |   |    Layer1   |              +---------------+    +-------------+
 +----------+   +-------------+                                                      +-----------+
 +----------+   +-------------+                               +-->                   | 1 from 3  |
 |  Esmart  |   | Sel 1 from 6|              +---------------+    +-------------+    |   LVDS    |
 |  window0 |   |   Layer2    |              |N from 6 Layers     |             |    +-----------+
 +----------+   +-------------+              |   Overlay1    +    | Video Port1 | +--->
 +----------+   +-------------+   -------->  |               |    |             |    +-----------+
 |  Esmart  |   | Sel 1 from 6|   -------->  +---------------+    +-------------+    | 1 from 3  |
 |  Window1 |   |   Layer3    |                               +-->                   |   MIPI    |
 +----------+   +-------------+                                                      +-----------+
 +----------+   +-------------+              +---------------+    +-------------+
 |  Smart   |   | Sel 1 from 6|              |N from 6 Layers|    |             |    +-----------+
 |  Window0 |   |    Layer4   |              |   Overlay2    |    | Video Port2 |    | 1 from 3  |
 +----------+   +-------------+              |               |    |             |    |   HDMI    |
 +----------+   +-------------+              +---------------+    +-------------+    +-----------+
 |  Smart   |   | Sel 1 from 6|                                                      +-----------+
 |  Window1 |   |    Layer5   |                                                      |  1 from 3 |
 +----------+   +-------------+                                                      |    eDP    |
 *                                                                                   +-----------+
 */
static void vop3_layer_map_initial(struct vop2 *vop2, uint32_t current_vp_id)
{
	uint16_t vp_id;
	struct drm_plane *plane = NULL;

	drm_for_each_plane(plane, vop2->drm_dev) {
		struct vop2_win *win = to_vop2_win(plane);

		vp_id = VOP_CTRL_GET(vop2, win_vp_id[win->phys_id]);
		win->vp_mask = BIT(vp_id);
		win->old_vp_mask = win->vp_mask;
		vop2->vps[vp_id].win_mask |= BIT(win->phys_id);
	}
}

static void vop2_layer_map_initial(struct vop2 *vop2, uint32_t current_vp_id)
{
	struct vop2_layer *layer;
	struct vop2_video_port *vp;
	struct vop2_win *win;
	unsigned long win_mask;
	uint32_t used_layers = 0;
	uint16_t port_mux_cfg = 0;
	uint16_t port_mux;
	uint16_t vp_id;
	uint8_t nr_layers;
	int phys_id;
	int i, j;

	if (is_vop3(vop2)) {
		vop3_layer_map_initial(vop2, current_vp_id);
		return;
	}

	for (i = 0; i < vop2->data->nr_vps; i++) {
		vp_id = i;
		j = 0;
		vp = &vop2->vps[vp_id];
		vp->win_mask = vp->plane_mask;
		nr_layers = hweight32(vp->win_mask);
		win_mask = vp->win_mask;
		for_each_set_bit(phys_id, &win_mask, ROCKCHIP_MAX_LAYER) {
			layer = &vop2->layers[used_layers + j];
			win = vop2_find_win_by_phys_id(vop2, phys_id);
			VOP_CTRL_SET(vop2, win_vp_id[phys_id], vp_id);
			VOP_MODULE_SET(vop2, layer, layer_sel, win->layer_sel_id[vp_id]);
			win->vp_mask = BIT(i);
			win->old_vp_mask = win->vp_mask;
			layer->win_phys_id = win->phys_id;
			win->layer_id = layer->id;
			j++;
			DRM_DEV_DEBUG(vop2->dev, "layer%d select %s for vp%d phys_id: %d\n",
				      layer->id, win->name, vp_id, phys_id);
		}
		used_layers += nr_layers;
	}

	/*
	 * The last Video Port(VP2 for RK3568, VP3 for RK3588) is fixed
	 * at the last level of the all the mixers by hardware design,
	 * so we just need to handle (nr_vps - 1) vps here.
	 */
	used_layers = 0;
	for (i = 0; i < vop2->data->nr_vps - 1; i++) {
		vp = &vop2->vps[i];
		used_layers += hweight32(vp->win_mask);
		if (used_layers == 0)
			port_mux = 8;
		else
			port_mux = used_layers - 1;
		port_mux_cfg |= port_mux << (vp->id * 4);
	}

	/* the last VP is fixed */
	if (vop2->data->nr_vps >= 1)
		port_mux_cfg |= 7 << (4 * (vop2->data->nr_vps - 1));
	vop2->port_mux_cfg = port_mux_cfg;
	VOP_CTRL_SET(vop2, ovl_port_mux_cfg, port_mux_cfg);

}

static void rk3588_vop2_regsbak(struct vop2 *vop2)
{
	uint32_t *base = vop2->regs;
	int i;

	/*
	 * No need to backup DSC/GAMMA_LUT/BPP_LUT/MMU
	 */
	for (i = 0; i < (0x2000 >> 2); i++)
		vop2->regsbak[i] = base[i];
}

static void vop2_initial(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	uint32_t current_vp_id = vp->id;
	struct vop2_wb *wb = &vop2->wb;
	int ret;

	if (vop2->enable_count == 0) {
		ret = pm_runtime_get_sync(vop2->dev);
		if (ret < 0) {
			DRM_DEV_ERROR(vop2->dev, "failed to get pm runtime: %d\n", ret);
			return;
		}

		ret = vop2_core_clks_prepare_enable(vop2);
		if (ret) {
			pm_runtime_put_sync(vop2->dev);
			return;
		}

		if (vop2_soc_is_rk3566())
			VOP_CTRL_SET(vop2, otp_en, 1);

		/*
		 * rk3588 don't support access mmio by memcpy
		 */
		if (vop2->version == VOP_VERSION_RK3588)
			rk3588_vop2_regsbak(vop2);
		else
			memcpy(vop2->regsbak, vop2->regs, vop2->len);

		VOP_MODULE_SET(vop2, wb, axi_yrgb_id, 0xd);
		VOP_MODULE_SET(vop2, wb, axi_uv_id, 0xe);
		vop2_wb_cfg_done(vp);

		if (is_vop3(vop2)) {
			VOP_CTRL_SET(vop2, dsp_vs_t_sel, 0);
			VOP_CTRL_SET(vop2, esmart_lb_mode, vop2->esmart_lb_mode);
		}

		/*
		 * This is unused and error init value for rk3528/rk3562 vp1, if less of this config,
		 * vp1 can't display normally.
		 */
		if (vop2->version == VOP_VERSION_RK3528 || vop2->version == VOP_VERSION_RK3562)
			vop2_mask_write(vop2, 0x700, 0x3, 4, 0, 0, true);

		VOP_CTRL_SET(vop2, cfg_done_en, 1);
		/*
		 * Disable auto gating, this is a workaround to
		 * avoid display image shift when a window enabled.
		 */
		VOP_CTRL_SET(vop2, auto_gating_en, 0);

		VOP_CTRL_SET(vop2, aclk_pre_auto_gating_en, 0);

		/*
		 * Register OVERLAY_LAYER_SEL and OVERLAY_PORT_SEL should take effect immediately,
		 * than windows configuration(CLUSTER/ESMART/SMART) can take effect according the
		 * video port mux configuration as we wished.
		 */
		VOP_CTRL_SET(vop2, ovl_port_mux_cfg_done_imd, 1);
		/*
		 * Let SYS_DSP_INFACE_EN/SYS_DSP_INFACE_CTRL/SYS_DSP_INFACE_POL take effect
		 * immediately.
		 */
		VOP_CTRL_SET(vop2, if_ctrl_cfg_done_imd, 1);

		/* Close dynamic turn on/off rk3588 PD_ESMART and keep esmart pd on when enable */
		if (vop2->version == VOP_VERSION_RK3588) {
			struct vop2_power_domain *esmart_pd = vop2_find_pd_by_id(vop2, VOP2_PD_ESMART);

			if (vop2_power_domain_status(esmart_pd))
				esmart_pd->on = true;
			else
				vop2_power_domain_on(esmart_pd);
		}
		vop2_layer_map_initial(vop2, current_vp_id);
		vop2_axi_irqs_enable(vop2);
		vop2->is_enabled = true;
	}

	vop2_debug_irq_enable(crtc);

	vop2->enable_count++;

	ret = clk_prepare_enable(vp->dclk);
	if (ret < 0)
		DRM_DEV_ERROR(vop2->dev, "failed to enable dclk for video port%d - %d\n",
			      vp->id, ret);
}

/*
 * The internal PD of VOP2 on rk3588 take effect immediately
 * for power up and take effect by vsync for power down.
 *
 * And the PD_CLUSTER0 is a parent PD of PD_CLUSTER1/2/3,
 * we may have this use case:
 * Cluster0 is attached to VP0 for HDMI output,
 * Cluster1 is attached to VP1 for MIPI DSI,

 * When we enable Cluster1 on VP1, we should enable PD_CLUSTER0 as
 * it is the parent PD, event though HDMI is plugout, VP1 is disabled,
 * the PD of Cluster0 should keep power on.

 * When system go to suspend:
 * (1) Power down PD of Cluster1 before VP1 standby(the power down is take
 *     effect by vsync)
 * (2) Power down PD of Cluster0
 *
 * But we have problem at step (2), Cluster0 is attached to VP0. but VP0
 * is in standby mode, as it is never used or hdmi plugout. So there is
 * no vsync, the power down will never take effect.

 * According to IC designer: We must power down all internal PD of VOP
 * before we power down the global PD_VOP.

 * So we get this workaround:
 * If we found a VP is in standby mode when we want power down a PD is
 * attached to it, we release the VP from standby mode, than it will
 * run a default timing and generate vsync. Than we can power down the
 * PD by this vsync. After all this is done, we standby the VP at last.
 */
static void vop2_power_domain_off_by_disabled_vp(struct vop2_power_domain *pd)
{
	struct vop2_video_port *vp = NULL;
	struct vop2 *vop2 = pd->vop2;
	struct vop2_win *win;
	struct drm_crtc *crtc;
	uint32_t vp_id;
	uint8_t phys_id;
	int ret;

	if (pd->data->id == VOP2_PD_CLUSTER0 || pd->data->id == VOP2_PD_CLUSTER1 ||
	    pd->data->id == VOP2_PD_CLUSTER2 || pd->data->id == VOP2_PD_CLUSTER3 ||
	    pd->data->id == VOP2_PD_ESMART) {
		phys_id = ffs(pd->data->module_id_mask) - 1;
		win = vop2_find_win_by_phys_id(vop2, phys_id);
		vp_id = ffs(win->vp_mask) - 1;
		vp = &vop2->vps[vp_id];
	} else {
		DRM_DEV_ERROR(vop2->dev, "unexpected power on pd%d\n", ffs(pd->data->id) - 1);
	}

	if (vp) {
		ret = clk_prepare_enable(vp->dclk);
		if (ret < 0)
			DRM_DEV_ERROR(vop2->dev, "failed to enable dclk for video port%d - %d\n",
				      vp->id, ret);
		crtc = &vp->rockchip_crtc.crtc;
		VOP_MODULE_SET(vop2, vp, standby, 0);
		vop2_power_domain_off(pd);
		vop2_cfg_done(crtc);
		vop2_wait_power_domain_off(pd);

		reinit_completion(&vp->dsp_hold_completion);
		vop2_dsp_hold_valid_irq_enable(crtc);
		VOP_MODULE_SET(vop2, vp, standby, 1);
		ret = wait_for_completion_timeout(&vp->dsp_hold_completion, msecs_to_jiffies(50));
		if (!ret)
			DRM_DEV_INFO(vop2->dev, "wait for vp%d dsp_hold timeout\n", vp->id);

		vop2_dsp_hold_valid_irq_disable(crtc);
		clk_disable_unprepare(vp->dclk);
	}
}

static void vop2_power_off_all_pd(struct vop2 *vop2)
{
	struct vop2_power_domain *pd, *n;

	list_for_each_entry_safe_reverse(pd, n, &vop2->pd_list_head, list) {
		if (vop2_power_domain_status(pd))
			vop2_power_domain_off_by_disabled_vp(pd);
		pd->on = false;
		pd->vp_mask = 0;
	}
}

static void vop2_disable(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	clk_disable_unprepare(vp->dclk);

	if (--vop2->enable_count > 0)
		return;

	if (vop2->is_iommu_enabled) {
		/*
		 * vop2 standby complete, so iommu detach is safe.
		 */
		VOP_CTRL_SET(vop2, dma_stop, 1);
		rockchip_drm_dma_detach_device(vop2->drm_dev, vop2->dev);
		vop2->is_iommu_enabled = false;
	}
	if (vop2->version == VOP_VERSION_RK3588)
		vop2_power_off_all_pd(vop2);

	vop2->is_enabled = false;
	pm_runtime_put_sync(vop2->dev);

	clk_disable_unprepare(vop2->pclk);
	clk_disable_unprepare(vop2->aclk);
	clk_disable_unprepare(vop2->hclk);
}

static void vop2_crtc_disable_dsc(struct vop2 *vop2, u8 dsc_id)
{
	struct vop2_dsc *dsc = &vop2->dscs[dsc_id];

	VOP_MODULE_SET(vop2, dsc, dsc_mer, 1);
	VOP_MODULE_SET(vop2, dsc, dsc_interface_mode, 0);
	VOP_MODULE_SET(vop2, dsc, dsc_en, 0);
	VOP_MODULE_SET(vop2, dsc, rst_deassert, 0);
}

static struct vop2_clk *vop2_clk_get(struct vop2 *vop2, const char *name)
{
	struct vop2_clk *clk, *n;

	if (!name)
		return NULL;

	list_for_each_entry_safe(clk, n, &vop2->clk_list_head, list) {
		if (!strcmp(clk_hw_get_name(&clk->hw), name))
			return clk;
	}

	return NULL;
}

static void vop2_clk_set_parent(struct clk *clk, struct clk *parent)
{
	int ret = 0;

	if (parent)
		ret = clk_set_parent(clk, parent);
	if (ret < 0)
		DRM_WARN("failed to set %s as parent for %s\n",
			 __clk_get_name(parent), __clk_get_name(clk));
}

static int vop2_extend_clk_init(struct vop2 *vop2)
{
	const char * const extend_clk_name[] = {
		"hdmi0_phy_pll", "hdmi1_phy_pll"};
	struct drm_device *drm_dev = vop2->drm_dev;
	struct clk *clk;
	struct vop2_extend_pll *extend_pll;
	int i;

	INIT_LIST_HEAD(&vop2->extend_clk_list_head);

	if (vop2->version != VOP_VERSION_RK3588)
		return 0;

	for (i = 0; i < ARRAY_SIZE(extend_clk_name); i++) {
		clk = devm_clk_get_optional(drm_dev->dev, extend_clk_name[i]);
		if (IS_ERR(clk)) {
			dev_warn(drm_dev->dev, "failed to get %s: %ld\n",
				 extend_clk_name[i], PTR_ERR(clk));
			continue;
		}

		if (!clk)
			continue;

		extend_pll = devm_kzalloc(drm_dev->dev, sizeof(*extend_pll), GFP_KERNEL);
		if (!extend_pll)
			return -ENOMEM;

		extend_pll->clk = clk;
		extend_pll->vp_mask = 0;
		strncpy(extend_pll->clk_name, extend_clk_name[i], sizeof(extend_pll->clk_name));
		list_add_tail(&extend_pll->list, &vop2->extend_clk_list_head);
	}

	return 0;
}

static struct vop2_extend_pll *vop2_extend_clk_find_by_name(struct vop2 *vop2, char *clk_name)
{
	struct vop2_extend_pll *extend_pll;

	list_for_each_entry(extend_pll, &vop2->extend_clk_list_head, list) {
		if (!strcmp(extend_pll->clk_name, clk_name))
			return extend_pll;
	}

	return NULL;
}

static int vop2_extend_clk_switch_pll(struct vop2 *vop2, struct vop2_extend_pll *src,
				      struct vop2_extend_pll *dst)
{
	struct vop2_clk *dclk;
	u32 vp_mask;
	int i = 0;
	char clk_name[32];

	if (!src->vp_mask)
		return -EINVAL;

	if (dst->vp_mask)
		return -EBUSY;

	vp_mask = src->vp_mask;

	while (vp_mask) {
		if ((BIT(i) & src->vp_mask)) {
			snprintf(clk_name, sizeof(clk_name), "dclk%d", i);
			dclk = vop2_clk_get(vop2, clk_name);
			clk_set_rate(dst->clk, dclk->rate);
			vop2_clk_set_parent(vop2->vps[i].dclk, dst->clk);
			src->vp_mask &= ~BIT(i);
			dst->vp_mask |= BIT(i);
		}
		i++;
		vp_mask  = vp_mask >> 1;
	}

	return 0;
}

static inline int vop2_extend_clk_get_vp_id(struct vop2_extend_pll *ext_pll)
{
	return ffs(ext_pll->vp_mask) - 1;
}

/*
 * Here are 2 hdmi phy pll can use for video port dclk. The strategies of how to use hdmi phy pll
 * as follow:
 *
 * 1. hdmi phy pll can be used for video port0/1/2 when output format under 4K@60Hz;
 *
 * 2. When a video port connect both hdmi0 and hdmi1(may also connect other output interface),
 *    it must hold the hdmi0 and hdmi1 phy pll, and other video port can't use it. if request dclk
 *    is under 4K@60Hz, set the video port dlk parent as hdmi0 phy pll.if hdmi0 or hdmi1 phy pll
 *    is used by other video port, report a error.
 *
 * 3. When a video port(A) connect hdmi0(may also connect other output interface but not hdmi1),
 *    it must hold the hdmi0 phy pll, and other video port can't use it. If both hdmi0 and hdmi1
 *    phy pll is used by other video port, report a error. If hdmi0 phy pll is used by another
 *    video port(B) and hdmi1 phy pll is free, set hdmi1 phy pll as video port(B) dclk parent and
 *    video port(A) hold hdmi0 phy pll. If hdmi0 phy pll is free, video port(A) hold hdmi0 pll.If
 *    video port(A) hold hdmi0 phy pll and request dclk is under 4k@60Hz, set hdmi0 phy pll as
 *    video port(A) dclk parent.
 *
 * 4. When a video port(A) connect hdmi1(may also connect other output interface but not hdmi0),
 *    it must hold the hdmi1 phy pll, and other video port can't use it. If both hdmi0 and hdmi1
 *    phy pll is used by other video port, report a error. If hdmi1 phy pll is used by another
 *    video port(B) and hdmi0 phy pll is free, set hdmi0 phy pll as video port(B) dclk parent and
 *    video port(A) hold hdmi1 phy pll. If hdmi1 phy pll is free, video port(A) hold hdmi1 pll. If
 *    video port(A) hold hdmi1 phy pll and request dclk is under 4k@60Hz, set hdmi1 phy pll as
 *    video port(A) dclk parent.
 *
 * 5. When a video port connect dp(0, 1, or both, may also connect other output type but not hdmi0
 *    and hdmi1). If the request dclk is higher than 4K@60Hz or video port id is 2, do nothing.
 *    Otherwise get a free hdmi phy pll as video port dclk parent. If no free hdmi phy pll can be
 *    get, report a error.
 */

static int vop2_clk_set_parent_extend(struct vop2_video_port *vp,
				      struct rockchip_crtc_state *vcstate, bool enable)
{
	struct vop2 *vop2 = vp->vop2;
	struct vop2_extend_pll *hdmi0_phy_pll, *hdmi1_phy_pll;
	struct drm_crtc *crtc = &vp->rockchip_crtc.crtc;
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;

	hdmi0_phy_pll = vop2_extend_clk_find_by_name(vop2, "hdmi0_phy_pll");
	hdmi1_phy_pll = vop2_extend_clk_find_by_name(vop2, "hdmi1_phy_pll");

	if (hdmi0_phy_pll)
		clk_get_rate(hdmi0_phy_pll->clk);
	if (hdmi1_phy_pll)
		clk_get_rate(hdmi1_phy_pll->clk);

	if ((!hdmi0_phy_pll && !hdmi1_phy_pll) ||
	    ((vcstate->output_if & VOP_OUTPUT_IF_HDMI0) && !hdmi0_phy_pll) ||
	    ((vcstate->output_if & VOP_OUTPUT_IF_HDMI1) && !hdmi1_phy_pll))
		return 0;

	if (enable) {
		if ((vcstate->output_if & VOP_OUTPUT_IF_HDMI0) &&
		    (vcstate->output_if & VOP_OUTPUT_IF_HDMI1)) {
			if (hdmi0_phy_pll->vp_mask) {
				DRM_ERROR("hdmi0 phy pll is used by vp%d\n",
					  vop2_extend_clk_get_vp_id(hdmi0_phy_pll));
				return -EBUSY;
			}

			if (hdmi1_phy_pll->vp_mask) {
				DRM_ERROR("hdmi1 phy pll is used by vp%d\n",
					  vop2_extend_clk_get_vp_id(hdmi1_phy_pll));
				return -EBUSY;
			}

			if (adjusted_mode->crtc_clock > VOP2_MAX_DCLK_RATE)
				vop2_clk_set_parent(vp->dclk, vp->dclk_parent);
			else
				vop2_clk_set_parent(vp->dclk, hdmi0_phy_pll->clk);

			hdmi0_phy_pll->vp_mask |= BIT(vp->id);
			hdmi1_phy_pll->vp_mask |= BIT(vp->id);
		} else if ((vcstate->output_if & VOP_OUTPUT_IF_HDMI0) &&
			   !(vcstate->output_if & VOP_OUTPUT_IF_HDMI1)) {
			if (hdmi0_phy_pll->vp_mask) {
				if (hdmi1_phy_pll) {
					if (hdmi1_phy_pll->vp_mask) {
						DRM_ERROR("hdmi0: phy pll is used by vp%d:vp%d\n",
							  vop2_extend_clk_get_vp_id(hdmi0_phy_pll),
							  vop2_extend_clk_get_vp_id(hdmi1_phy_pll));
						return -EBUSY;
					}

					vop2_extend_clk_switch_pll(vop2, hdmi0_phy_pll,
								   hdmi1_phy_pll);
				} else {
					DRM_ERROR("hdmi0: phy pll is used by vp%d\n",
						  vop2_extend_clk_get_vp_id(hdmi0_phy_pll));
					return -EBUSY;
				}
			}

			if (adjusted_mode->crtc_clock > VOP2_MAX_DCLK_RATE)
				vop2_clk_set_parent(vp->dclk, vp->dclk_parent);
			else
				vop2_clk_set_parent(vp->dclk, hdmi0_phy_pll->clk);

			hdmi0_phy_pll->vp_mask |= BIT(vp->id);
		} else if (!(vcstate->output_if & VOP_OUTPUT_IF_HDMI0) &&
			   (vcstate->output_if & VOP_OUTPUT_IF_HDMI1)) {
			if (hdmi1_phy_pll->vp_mask) {
				if (hdmi0_phy_pll) {
					if (hdmi0_phy_pll->vp_mask) {
						DRM_ERROR("hdmi1: phy pll is used by vp%d:vp%d\n",
							  vop2_extend_clk_get_vp_id(hdmi0_phy_pll),
							  vop2_extend_clk_get_vp_id(hdmi1_phy_pll));
						return -EBUSY;
					}

					vop2_extend_clk_switch_pll(vop2, hdmi1_phy_pll,
								   hdmi0_phy_pll);
				} else {
					DRM_ERROR("hdmi1: phy pll is used by vp%d\n",
						  vop2_extend_clk_get_vp_id(hdmi1_phy_pll));
					return -EBUSY;
				}
			}

			if (adjusted_mode->crtc_clock > VOP2_MAX_DCLK_RATE)
				vop2_clk_set_parent(vp->dclk, vp->dclk_parent);
			else
				vop2_clk_set_parent(vp->dclk, hdmi1_phy_pll->clk);

			hdmi1_phy_pll->vp_mask |= BIT(vp->id);
		} else if (output_if_is_dp(vcstate->output_if)) {
			if (vp->id == 2) {
				vop2_clk_set_parent(vp->dclk, vp->dclk_parent);
				return 0;
			}

			if (hdmi0_phy_pll && !hdmi0_phy_pll->vp_mask) {
				vop2_clk_set_parent(vp->dclk, hdmi0_phy_pll->clk);
				hdmi0_phy_pll->vp_mask |= BIT(vp->id);
			} else if (hdmi1_phy_pll && !hdmi1_phy_pll->vp_mask) {
				vop2_clk_set_parent(vp->dclk, hdmi1_phy_pll->clk);
				hdmi1_phy_pll->vp_mask |= BIT(vp->id);
			} else {
				vop2_clk_set_parent(vp->dclk, vp->dclk_parent);
				DRM_INFO("No free hdmi phy pll for DP, use default parent\n");
			}
		}
	} else {
		if (hdmi0_phy_pll && (BIT(vp->id) & hdmi0_phy_pll->vp_mask))
			hdmi0_phy_pll->vp_mask &= ~BIT(vp->id);

		if (hdmi1_phy_pll && (BIT(vp->id) & hdmi1_phy_pll->vp_mask))
			hdmi1_phy_pll->vp_mask &= ~BIT(vp->id);
	}

	return 0;
}

static void vop2_crtc_atomic_disable_for_psr(struct drm_crtc *crtc,
					     struct drm_crtc_state *old_state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	vop2_disable_all_planes_for_crtc(crtc);
	drm_crtc_vblank_off(crtc);
	if (hweight8(vop2->active_vp_mask) == 1) {
		u32 adjust_aclk_rate = 0;
		u32 htotal = (VOP_MODULE_GET(vop2, vp, htotal_pw) >> 16) & 0xffff;
		u32 pre_scan_dly = VOP_MODULE_GET(vop2, vp, pre_scan_htiming);
		u32 pre_scan_hblank = pre_scan_dly & 0x1fff;
		u32 pre_scan_hactive = (pre_scan_dly >> 16) & 0x1fff;
		u32 dclk_rate = crtc->state->adjusted_mode.crtc_clock / 1000;
		/**
		 * (pre_scan_hblank + pre_scan_hactive) x aclk_margin / adjust_aclk_rate = hotal / dclk_rate
		 * aclk_margin = 1.2, so
		 * adjust_aclk_rate = (pre_scan_hblank + pre_scan_hactive) x 1.2 * aclk_margin / htotal
		 */

		adjust_aclk_rate = (pre_scan_hblank + pre_scan_hactive) * dclk_rate * 12 / 10 / htotal;

		vop2->aclk_rate = clk_get_rate(vop2->aclk);
		clk_set_rate(vop2->aclk, adjust_aclk_rate * 1000000L);
		vop2->aclk_rate_reset = true;
	}
}

static void vop2_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_video_port_data *vp_data = &vop2->data->vp[vp->id];
	struct vop2_video_port *splice_vp = &vop2->vps[vp_data->splice_vp_id];
	bool dual_channel = !!(vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE);
	int ret;

	WARN_ON(vp->event);

	if (crtc->state->self_refresh_active) {
		vop2_crtc_atomic_disable_for_psr(crtc, old_state);
		goto out;
	}

	vop2_lock(vop2);
	DRM_DEV_INFO(vop2->dev, "Crtc atomic disable vp%d\n", vp->id);
	VOP_MODULE_SET(vop2, vp, almost_full_or_en, 0);
	VOP_MODULE_SET(vop2, vp, line_flag_or_en, 0);
	drm_crtc_vblank_off(crtc);
	if (vop2->dscs[vcstate->dsc_id].enabled &&
	    vop2->dscs[vcstate->dsc_id].attach_vp_id == vp->id &&
	    vop2->data->nr_dscs) {
		if (dual_channel) {
			vop2_crtc_disable_dsc(vop2, 0);
			vop2_crtc_disable_dsc(vop2, 1);
		} else {
			vop2_crtc_disable_dsc(vop2, vcstate->dsc_id);
		}
	}

	if (vp->cubic_lut) {
		VOP_MODULE_SET(vop2, vp, cubic_lut_update_en, 0);
		VOP_MODULE_SET(vop2, vp, cubic_lut_en, 0);
	}

	if (vp_data->feature & VOP_FEATURE_VIVID_HDR)
		VOP_MODULE_SET(vop2, vp, hdr_lut_update_en, 0);
	vop2_disable_all_planes_for_crtc(crtc);

	if (vop2->dscs[vcstate->dsc_id].enabled &&
	    vop2->dscs[vcstate->dsc_id].attach_vp_id == vp->id &&
	    vop2->data->nr_dscs && vop2->dscs[vcstate->dsc_id].pd) {
		if (dual_channel) {
			vop2_power_domain_put(vop2->dscs[0].pd);
			vop2_power_domain_put(vop2->dscs[1].pd);
			vop2->dscs[0].pd->vp_mask = 0;
			vop2->dscs[1].pd->vp_mask = 0;
			vop2->dscs[0].attach_vp_id = -1;
			vop2->dscs[1].attach_vp_id = -1;
		} else {
			vop2_power_domain_put(vop2->dscs[vcstate->dsc_id].pd);
			vop2->dscs[vcstate->dsc_id].pd->vp_mask = 0;
			vop2->dscs[vcstate->dsc_id].attach_vp_id = -1;
		}
		vop2->dscs[vcstate->dsc_id].enabled = false;
		vcstate->dsc_enable = false;
	}

	if (vp->output_if & VOP_OUTPUT_IF_eDP0)
		VOP_GRF_SET(vop2, grf, grf_edp0_en, 0);

	if (vp->output_if & VOP_OUTPUT_IF_eDP1)
		VOP_GRF_SET(vop2, grf, grf_edp1_en, 0);

	if (vp->output_if & VOP_OUTPUT_IF_HDMI0) {
		VOP_GRF_SET(vop2, grf, grf_hdmi0_dsc_en, 0);
		VOP_GRF_SET(vop2, grf, grf_hdmi0_en, 0);
	}

	if (vp->output_if & VOP_OUTPUT_IF_HDMI1) {
		VOP_GRF_SET(vop2, grf, grf_hdmi1_dsc_en, 0);
		VOP_GRF_SET(vop2, grf, grf_hdmi1_en, 0);
	}

	VOP_MODULE_SET(vop2, vp, dual_channel_en, 0);
	VOP_MODULE_SET(vop2, vp, dual_channel_swap, 0);

	vp->output_if = 0;

	vop2_clk_set_parent_extend(vp, vcstate, false);
	/*
	 * Vop standby will take effect at end of current frame,
	 * if dsp hold valid irq happen, it means standby complete.
	 *
	 * we must wait standby complete when we want to disable aclk,
	 * if not, memory bus maybe dead.
	 */
	reinit_completion(&vp->dsp_hold_completion);
	vop2_dsp_hold_valid_irq_enable(crtc);

	spin_lock(&vop2->reg_lock);

	VOP_MODULE_SET(vop2, vp, splice_en, 0);

	VOP_MODULE_SET(vop2, vp, standby, 1);

	spin_unlock(&vop2->reg_lock);

	ret = wait_for_completion_timeout(&vp->dsp_hold_completion, msecs_to_jiffies(50));
	if (!ret)
		DRM_DEV_INFO(vop2->dev, "wait for vp%d dsp_hold timeout\n", vp->id);

	vop2_dsp_hold_valid_irq_disable(crtc);

	vop2_disable(crtc);

	vop2->active_vp_mask &= ~BIT(vp->id);
	if (vcstate->splice_mode)
		vop2->active_vp_mask &= ~BIT(splice_vp->id);
	vcstate->splice_mode = false;
	vcstate->output_flags = 0;
	vp->splice_mode_right = false;
	vp->loader_protect = false;
	splice_vp->splice_mode_right = false;
	memset(&vp->active_tv_state, 0, sizeof(vp->active_tv_state));
	vop2_unlock(vop2);

	vop2_set_system_status(vop2);

out:
	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}
}

static int vop2_cluster_two_win_mode_check(struct drm_plane_state *pstate)
{
	struct drm_atomic_state *state = pstate->state;
	struct drm_plane *plane = pstate->plane;
	struct vop2_win *win = to_vop2_win(plane);
	struct vop2 *vop2 = win->vop2;
	struct vop2_win *main_win = vop2_find_win_by_phys_id(vop2, win->phys_id);
	struct drm_plane_state *main_pstate;
	int actual_w = drm_rect_width(&pstate->src) >> 16;
	int xoffset;

	if (pstate->fb->modifier == DRM_FORMAT_MOD_LINEAR)
		xoffset = 0;
	else
		xoffset = pstate->src.x1 >> 16;

	if ((actual_w + xoffset % 16) > 2048) {
		DRM_ERROR("%s act_w(%d) + xoffset(%d) / 16  << 2048 in two win mode\n",
				  win->name, actual_w, xoffset);
		return -EINVAL;
	}

	main_pstate = drm_atomic_get_new_plane_state(state, &main_win->base);

	if (pstate->fb->modifier != main_pstate->fb->modifier) {
		DRM_ERROR("%s(fb->modifier: 0x%llx) must use same data layout as %s(fb->modifier: 0x%llx)\n",
				win->name, pstate->fb->modifier, main_win->name, main_pstate->fb->modifier);
		return -EINVAL;
	}

	if (main_pstate->fb->modifier == DRM_FORMAT_MOD_LINEAR)
		xoffset = 0;
	else
		xoffset = main_pstate->src.x1 >> 16;
	actual_w = drm_rect_width(&main_pstate->src) >> 16;

	if ((actual_w + xoffset % 16) > 2048) {
		DRM_ERROR("%s act_w(%d) + xoffset(%d) / 16  << 2048 in two win mode\n",
				  main_win->name, actual_w, xoffset);
		return -EINVAL;
	}

	return 0;
}

static int vop2_cluter_splice_scale_check(struct vop2_win *win, struct drm_plane_state *pstate,
					  u16 hdisplay)
{
	struct drm_rect src = drm_plane_state_src(pstate);
	struct drm_rect dst = drm_plane_state_dest(pstate);
	u16 half_hdisplay = hdisplay >> 1;

	/* scale up is ok */
	if ((drm_rect_width(&src) >> 16) <= drm_rect_width(&dst))
		return 0;

	if ((drm_rect_width(&src) >> 16) <= VOP2_MAX_VP_OUTPUT_WIDTH)
		return 0;
	/*
	 * Cluster scale down limitation in splice mode:
	 * If scale down, must display at horizontal center
	 */
	if ((dst.x1 < half_hdisplay) && (dst.x2 > half_hdisplay)) {
		if ((dst.x2 + dst.x1) != hdisplay) {
			DRM_ERROR("%s src_w: %d dst_w %d dst(%d %d) must scale down at center in splice mode\n",
				  win->name, drm_rect_width(&src) >> 16,
				  drm_rect_width(&dst), dst.x1, dst.x2);
			return -EINVAL;
		}

		if (drm_rect_calc_hscale(&src, &dst, 1, FRAC_16_16(6, 5)) < 0) {
			DRM_ERROR("%s %d --> %d scale down factor should < 1.2 in splice mode\n",
				  win->name, drm_rect_width(&src) >> 16, drm_rect_width(&dst));
			return -EINVAL;
		}
	}

	return 0;
}

static int vop2_plane_splice_check(struct drm_plane *plane, struct drm_plane_state *pstate,
				   struct drm_display_mode *mode)
{
	struct vop2_win *win = to_vop2_win(plane);
	int ret = 0;

	if (!(win->feature & WIN_FEATURE_SPLICE_LEFT)) {
		DRM_ERROR("%s can't be left win in splice mode\n", win->name);
		return -EINVAL;
	}

	if (win->feature & WIN_FEATURE_CLUSTER_SUB) {
		DRM_ERROR("%s can't use two win mode in splice mode\n", win->name);
		return -EINVAL;
	}

	if ((pstate->rotation & DRM_MODE_ROTATE_270) ||
	    (pstate->rotation & DRM_MODE_ROTATE_90) ||
	    (pstate->rotation & DRM_MODE_REFLECT_X)) {
		DRM_ERROR("%s can't rotate 270/90 and xmirror in splice mode\n", win->name);
		return -EINVAL;
	}

	/* check for cluster splice scale down */
	if (win->feature & WIN_FEATURE_CLUSTER_MAIN)
		ret = vop2_cluter_splice_scale_check(win, pstate, mode->hdisplay);

	return ret;
}

static int vop2_plane_atomic_check(struct drm_plane *plane, struct drm_plane_state *state)
{
	struct vop2_plane_state *vpstate = to_vop2_plane_state(state);
	struct vop2_win *win = to_vop2_win(plane);
	struct vop2_win *splice_win;
	struct vop2 *vop2 = win->vop2;
	struct drm_framebuffer *fb = state->fb;
	struct drm_display_mode *mode;
	struct drm_crtc *crtc = state->crtc;
	struct drm_crtc_state *cstate;
	struct rockchip_crtc_state *vcstate;
	struct vop2_video_port *vp;
	const struct vop2_data *vop2_data;
	struct drm_rect *dest = &vpstate->dest;
	struct drm_rect *src = &vpstate->src;
	struct drm_gem_object *obj, *uv_obj;
	struct rockchip_gem_object *rk_obj, *rk_uv_obj;
	int min_scale = win->regs->scl ? FRAC_16_16(1, 8) : DRM_PLANE_HELPER_NO_SCALING;
	int max_scale = win->regs->scl ? FRAC_16_16(8, 1) : DRM_PLANE_HELPER_NO_SCALING;
	uint32_t tile_size = 1;
	int max_input_w;
	int max_input_h;
	unsigned long offset;
	dma_addr_t dma_addr;
	int ret;

	crtc = crtc ? crtc : plane->state->crtc;
	if (!crtc || !fb) {
		plane->state->visible = false;
		return 0;
	}

	vp = to_vop2_video_port(crtc);
	vop2_data = vp->vop2->data;

	cstate = drm_atomic_get_existing_crtc_state(state->state, crtc);
	if (WARN_ON(!cstate))
		return -EINVAL;

	mode = &cstate->mode;
	vcstate = to_rockchip_crtc_state(cstate);

	max_input_w = vop2_data->max_input.width;
	max_input_h = vop2_data->max_input.height;

	if (vop2_has_feature(win->vop2, VOP_FEATURE_SPLICE)) {
		if (mode->hdisplay > VOP2_MAX_VP_OUTPUT_WIDTH) {
			vcstate->splice_mode = true;
			ret = vop2_plane_splice_check(plane, state, mode);
			if (ret < 0)
				return ret;
			splice_win = vop2_find_win_by_phys_id(vop2, win->splice_win_id);
			splice_win->splice_mode_right = true;
			splice_win->left_win = win;
			win->splice_win = splice_win;
			max_input_w <<= 1;
		}
	}

	vpstate->xmirror_en = (state->rotation & DRM_MODE_REFLECT_X) ? 1 : 0;
	vpstate->ymirror_en = (state->rotation & DRM_MODE_REFLECT_Y) ? 1 : 0;
	vpstate->rotate_270_en = (state->rotation & DRM_MODE_ROTATE_270) ? 1 : 0;
	vpstate->rotate_90_en = (state->rotation & DRM_MODE_ROTATE_90) ? 1 : 0;

	if (vpstate->rotate_270_en && vpstate->rotate_90_en) {
		DRM_ERROR("Can't rotate 90 and 270 at the same time\n");
		return -EINVAL;
	}

	ret = drm_atomic_helper_check_plane_state(state, cstate,
						  min_scale, max_scale,
						  true, true);
	if (ret)
		return ret;

	if (!state->visible) {
		DRM_ERROR("%s is invisible(src: pos[%d, %d] rect[%d x %d] dst: pos[%d, %d] rect[%d x %d]\n",
			  plane->name, state->src_x >> 16, state->src_y >> 16, state->src_w >> 16,
			  state->src_h >> 16, state->crtc_x, state->crtc_y, state->crtc_w,
			  state->crtc_h);
		return 0;
	}

	src->x1 = state->src.x1;
	src->y1 = state->src.y1;
	src->x2 = state->src.x2;
	src->y2 = state->src.y2;
	dest->x1 = state->dst.x1;
	dest->y1 = state->dst.y1;
	dest->x2 = state->dst.x2;
	dest->y2 = state->dst.y2;

	vpstate->zpos = state->zpos;
	vpstate->global_alpha = state->alpha >> 8;
	vpstate->blend_mode = state->pixel_blend_mode;
	vpstate->format = vop2_convert_format(fb->format->format);
	if (vpstate->format < 0)
		return vpstate->format;

	if (drm_rect_width(src) >> 16 < 4 || drm_rect_height(src) >> 16 < 4 ||
	    drm_rect_width(dest) < 4 || drm_rect_width(dest) < 4) {
		DRM_ERROR("Invalid size: %dx%d->%dx%d, min size is 4x4\n",
			  drm_rect_width(src) >> 16, drm_rect_height(src) >> 16,
			  drm_rect_width(dest), drm_rect_height(dest));
		state->visible = false;
		return 0;
	}

	if (drm_rect_width(src) >> 16 > max_input_w ||
	    drm_rect_height(src) >> 16 > max_input_h) {
		DRM_ERROR("Invalid source: %dx%d. max input: %dx%d\n",
			  drm_rect_width(src) >> 16,
			  drm_rect_height(src) >> 16,
			  max_input_w,
			  max_input_h);
		return -EINVAL;
	}

	if (rockchip_afbc(plane, fb->modifier))
		vpstate->afbc_en = true;
	else
		vpstate->afbc_en = false;

	vpstate->tiled_en = rockchip_tiled(plane, fb->modifier) ?
				fb->modifier & ROCKCHIP_TILED_BLOCK_SIZE_MASK : 0;
	if (vpstate->tiled_en && vpstate->afbc_en) {
		DRM_ERROR("%s afbc and tiled format can't be enabled at same time(modifier: 0x%llx)\n",
			  win->name, fb->modifier);
		return -EINVAL;
	}
	if (vpstate->tiled_en)
		tile_size = vpstate->tiled_en == ROCKCHIP_TILED_BLOCK_SIZE_8x8 ? 8 : 4;

	/*
	 * This is special feature at rk356x, the cluster layer only can support
	 * afbc format and can't support linear format;
	 */
	if (vp->vop2->version == VOP_VERSION_RK3568) {
		if (vop2_cluster_window(win) && !vpstate->afbc_en) {
			DRM_ERROR("Unsupported linear format at %s\n", win->name);
			return -EINVAL;
		}
	}

	if (vp->vop2->version > VOP_VERSION_RK3568) {
		if (vop2_cluster_window(win) && !vpstate->afbc_en && fb->format->is_yuv && !is_vop3(vop2)) {
			DRM_ERROR("Unsupported linear yuv format at %s\n", win->name);
			return -EINVAL;
		}

		if (vop2_cluster_window(win) && !vpstate->afbc_en &&
		    (win->supported_rotations & state->rotation)) {
			DRM_ERROR("Unsupported linear rotation(%d) format at %s\n",
				  state->rotation, win->name);
			return -EINVAL;
		}
	}

	if (win->feature & WIN_FEATURE_CLUSTER_SUB) {
		ret = vop2_cluster_two_win_mode_check(state);
		if (ret < 0)
			return ret;
	}

	/*
	 * Src.x1 can be odd when do clip, but yuv plane start point
	 * need align with 2 pixel.
	 */
	if (fb->format->is_yuv && ((state->src.x1 >> 16) % 2)) {
		DRM_ERROR("Invalid Source: Yuv format not support odd xpos\n");
		return -EINVAL;
	}

	if (fb->format->char_per_block[0] == 0)
		offset = ALIGN_DOWN(src->x1 >> 16, tile_size) * fb->format->cpp[0] * tile_size;
	else
		offset = drm_format_info_min_pitch(fb->format, 0, ALIGN_DOWN(src->x1 >> 16, tile_size)) * tile_size;
	vpstate->offset = offset + fb->offsets[0];

	/*
	 * AFBC HDR_PTR must set to the zero offset of the framebuffer.
	 */
	if (vpstate->afbc_en)
		offset = 0;
	else if (vpstate->ymirror_en)
		offset += ((src->y2 >> 16) - 1) * fb->pitches[0];
	else
		offset += ALIGN_DOWN(src->y1 >> 16, tile_size) * fb->pitches[0];

	obj = fb->obj[0];
	rk_obj = to_rockchip_obj(obj);

	vpstate->yrgb_mst = rk_obj->dma_addr + offset + fb->offsets[0];
	if (fb->format->is_yuv && fb->format->num_planes > 1) {
		int hsub = fb->format->hsub;
		int vsub = fb->format->vsub;

		if (fb->format->char_per_block[0] == 0)
			offset = ALIGN_DOWN(src->x1 >> 16, tile_size) * fb->format->cpp[1] / hsub * tile_size;
		else
			offset = drm_format_info_min_pitch(fb->format, 1, ALIGN_DOWN(src->x1 >> 16, tile_size)) * tile_size / hsub;

		if (vpstate->tiled_en)
			offset /= vsub;
		offset += ALIGN_DOWN(src->y1 >> 16, tile_size) * fb->pitches[1] / vsub;

		uv_obj = fb->obj[1];
		rk_uv_obj = to_rockchip_obj(uv_obj);

		if (vpstate->ymirror_en && !vpstate->afbc_en)
			offset += fb->pitches[1] * ((state->src_h >> 16) - 2)  / vsub;
		dma_addr = rk_uv_obj->dma_addr + offset + fb->offsets[1];
		vpstate->uv_mst = dma_addr;
		/* tile 4x4 m0 format, y and uv is packed together */
		if (vpstate->tiled_en == ROCKCHIP_TILED_BLOCK_SIZE_4x4_MODE0)
			vpstate->yrgb_mst += offset;
	}

	return 0;
}

static void vop2_plane_atomic_disable(struct drm_plane *plane, struct drm_plane_state *old_state)
{
	struct vop2_win *win = to_vop2_win(plane);
	struct vop2 *vop2 = win->vop2;
#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	struct vop2_plane_state *vpstate = to_vop2_plane_state(plane->state);
#endif

	rockchip_drm_dbg(vop2->dev, VOP_DEBUG_PLANE, "%s disable %s\n",
			 win->name, current->comm);

	if (!old_state->crtc)
		return;

	spin_lock(&vop2->reg_lock);

	vop2_win_disable(win, false);
	if (win->splice_win)
		vop2_win_disable(win->splice_win, false);

#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	kfree(vpstate->planlist);
	vpstate->planlist = NULL;
#endif

	spin_unlock(&vop2->reg_lock);
}

/*
 * The color key is 10 bit, so all format should
 * convert to 10 bit here.
 */
static void vop2_plane_setup_color_key(struct drm_plane *plane)
{
	struct drm_plane_state *pstate = plane->state;
	struct vop2_plane_state *vpstate = to_vop2_plane_state(pstate);
	struct drm_framebuffer *fb = pstate->fb;
	struct vop2_win *win = to_vop2_win(plane);
	struct vop2 *vop2 = win->vop2;
	uint32_t color_key_en = 0;
	uint32_t color_key;
	uint32_t r = 0;
	uint32_t g = 0;
	uint32_t b = 0;

	if (!(vpstate->color_key & VOP_COLOR_KEY_MASK) || fb->format->is_yuv) {
		VOP_WIN_SET(vop2, win, color_key_en, 0);
		return;
	}

	switch (fb->format->format) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		r = (vpstate->color_key & 0xf800) >> 11;
		g = (vpstate->color_key & 0x7e0) >> 5;
		b = (vpstate->color_key & 0x1f);
		r <<= 5;
		g <<= 4;
		b <<= 5;
		color_key_en = 1;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		r = (vpstate->color_key & 0xff0000) >> 16;
		g = (vpstate->color_key & 0xff00) >> 8;
		b = (vpstate->color_key & 0xff);
		r <<= 2;
		g <<= 2;
		b <<= 2;
		color_key_en = 1;
		break;
	}

	color_key = (r << 20) | (g << 10) | b;
	VOP_WIN_SET(vop2, win, color_key_en, color_key_en);
	VOP_WIN_SET(vop2, win, color_key, color_key);
}

static void vop2_calc_drm_rect_for_splice(struct vop2_plane_state *vpstate,
					  struct drm_rect *left_src, struct drm_rect *left_dst,
					  struct drm_rect *right_src, struct drm_rect *right_dst)
{
	struct drm_crtc *crtc = vpstate->base.crtc;
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct drm_rect *dst = &vpstate->dest;
	struct drm_rect *src = &vpstate->src;
	u16 half_hdisplay = mode->crtc_hdisplay >> 1;
	int hscale = drm_rect_calc_hscale(src, dst, 0, INT_MAX);
	int dst_w = drm_rect_width(dst);
	int src_w = drm_rect_width(src) >> 16;
	int left_src_w, left_dst_w, right_dst_w;
	struct drm_plane_state *pstate = &vpstate->base;
	struct drm_framebuffer *fb = pstate->fb;

	left_dst_w = min_t(u16, half_hdisplay, dst->x2) - dst->x1;
	if (left_dst_w < 0)
		left_dst_w = 0;
	right_dst_w = dst_w - left_dst_w;

	if (!right_dst_w)
		left_src_w = src_w;
	else
		left_src_w = (left_dst_w * hscale) >> 16;

	/*
	 * Make sure the yrgb/uv mst of right win are byte aligned
	 * with full pixel.
	 */
	if (right_dst_w) {
		if (fb->format->format == DRM_FORMAT_NV15)
			left_src_w &= ~0x7;
		else if (fb->format->format == DRM_FORMAT_NV12)
			left_src_w &= ~0x1;
	}
	left_src->x1 = src->x1;
	left_src->x2 = src->x1 + (left_src_w << 16);
	left_dst->x1 = dst->x1;
	left_dst->x2 = dst->x1 + left_dst_w;
	right_src->x1 = left_src->x2;
	right_src->x2 = src->x2;
	right_dst->x1 = dst->x1 + left_dst_w - half_hdisplay;
	if (right_dst->x1 < 0)
		right_dst->x1 = 0;

	right_dst->x2 = right_dst->x1 + right_dst_w;

	left_src->y1 = src->y1;
	left_src->y2 = src->y2;
	left_dst->y1 = dst->y1;
	left_dst->y2 = dst->y2;
	right_src->y1 = src->y1;
	right_src->y2 = src->y2;
	right_dst->y1 = dst->y1;
	right_dst->y2 = dst->y2;
}

static void rk3588_vop2_win_cfg_axi(struct vop2_win *win)
{
	struct vop2 *vop2 = win->vop2;

	/*
	 * No need to set multi area sub windows as it
	 * share the same axi bus and read_id with main window.
	 */
	if (vop2_multi_area_sub_window(win))
		return;
	/*
	 * No need to set Cluster sub windows axi_id as it
	 * share the same axi bus with main window.
	 */
	if (!vop2_cluster_sub_window(win))
		VOP_WIN_SET(vop2, win, axi_id, win->axi_id);
	VOP_WIN_SET(vop2, win, axi_yrgb_id, win->axi_yrgb_id);
	VOP_WIN_SET(vop2, win, axi_uv_id, win->axi_uv_id);
}

static const char *modifier_to_string(uint64_t modifier)
{
	switch (modifier) {
	case DRM_FORMAT_MOD_ROCKCHIP_TILED(ROCKCHIP_TILED_BLOCK_SIZE_8x8):
		return "[TILE_8x8]";
	case DRM_FORMAT_MOD_ROCKCHIP_TILED(ROCKCHIP_TILED_BLOCK_SIZE_4x4_MODE0):
		return "[TILE_4x4_M0]";
	case DRM_FORMAT_MOD_ROCKCHIP_TILED(ROCKCHIP_TILED_BLOCK_SIZE_4x4_MODE1):
		return "[TILE_4x4_M1]";
	default:
		return drm_is_afbc(modifier) ? "[AFBC]" : "";
	}
}

static void vop2_win_atomic_update(struct vop2_win *win, struct drm_rect *src, struct drm_rect *dst,
				   struct drm_plane_state *pstate)
{
	struct drm_crtc *crtc = pstate->crtc;
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2_plane_state *vpstate = to_vop2_plane_state(pstate);
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	struct vop2 *vop2 = win->vop2;
	struct drm_framebuffer *fb = pstate->fb;
	struct drm_rect *left_src = &vpstate->src;
	uint32_t bpp = rockchip_drm_get_bpp(fb->format);
	uint32_t actual_w, actual_h, dsp_w, dsp_h;
	uint32_t dsp_stx, dsp_sty;
	uint32_t act_info, dsp_info, dsp_st;
	uint32_t format;
	uint32_t afbc_format;
	uint32_t rb_swap;
	uint32_t uv_swap;
	uint32_t afbc_half_block_en;
	uint32_t afbc_tile_num;
	uint32_t lb_mode;
	uint32_t stride, uv_stride = 0;
	uint32_t transform_offset;
	/* offset of the right window in splice mode */
	uint32_t splice_pixel_offset = 0;
	uint32_t splice_yrgb_offset = 0;
	uint32_t splice_uv_offset = 0;
	uint32_t afbc_xoffset;
	uint32_t hsub;
	dma_addr_t yrgb_mst;
	dma_addr_t uv_mst;

	struct drm_format_name_buf format_name;
	bool dither_up;
	bool tile_4x4_m0 = vpstate->tiled_en == ROCKCHIP_TILED_BLOCK_SIZE_4x4_MODE0 ? true : false;

	actual_w = drm_rect_width(src) >> 16;
	actual_h = drm_rect_height(src) >> 16;

	if (!actual_w || !actual_h) {
		vop2_win_disable(win, true);
		return;
	}

	dsp_w = drm_rect_width(dst);
	/*
	 * This win is for the right part of the plane,
	 * we need calculate the fb offset for it.
	 */
	if (win->splice_mode_right) {
		splice_pixel_offset = (src->x1 - left_src->x1) >> 16;
		splice_yrgb_offset = drm_format_info_min_pitch(fb->format, 0, splice_pixel_offset);
		if (fb->format->is_yuv && fb->format->num_planes > 1) {
			hsub = fb->format->hsub;
			splice_uv_offset = drm_format_info_min_pitch(fb->format, 1, splice_pixel_offset / hsub);
		}
	}

	if (dst->x1 + dsp_w > adjusted_mode->crtc_hdisplay) {
		DRM_ERROR("vp%d %s dest->x1[%d] + dsp_w[%d] exceed mode hdisplay[%d]\n",
			  vp->id, win->name, dst->x1, dsp_w, adjusted_mode->crtc_hdisplay);
		dsp_w = adjusted_mode->crtc_hdisplay - dst->x1;
		if (dsp_w < 4)
			dsp_w = 4;
		actual_w = dsp_w * actual_w / drm_rect_width(dst);
	}
	dsp_h = drm_rect_height(dst);
	if (dst->y1 + dsp_h > adjusted_mode->crtc_vdisplay) {
		DRM_ERROR("vp%d %s dest->y1[%d] + dsp_h[%d] exceed mode vdisplay[%d]\n",
			  vp->id, win->name, dst->y1, dsp_h, adjusted_mode->crtc_vdisplay);
		dsp_h = adjusted_mode->crtc_vdisplay - dst->y1;
		if (dsp_h < 4)
			dsp_h = 4;
		actual_h = dsp_h * actual_h / drm_rect_height(dst);
	}

	/*
	 * Workaround only for rk3568 vop
	 */
	if (vop2->version == VOP_VERSION_RK3568) {
		/*
		 * This is workaround solution for IC design:
		 * esmart can't support scale down when actual_w % 16 == 1.
		 */
		if (!(win->feature & WIN_FEATURE_AFBDC)) {
			if (actual_w > dsp_w && (actual_w & 0xf) == 1) {
				DRM_WARN("vp%d %s act_w[%d] MODE 16 == 1\n", vp->id, win->name, actual_w);
				actual_w -= 1;
			}
		}

		if (vpstate->afbc_en && actual_w % 4) {
			DRM_ERROR("vp%d %s actual_w[%d] should align as 4 pixel when enable afbc\n",
				  vp->id, win->name, actual_w);
			actual_w = ALIGN_DOWN(actual_w, 4);
		}
	}

	act_info = (actual_h - 1) << 16 | ((actual_w - 1) & 0xffff);
	dsp_info = (dsp_h - 1) << 16 | ((dsp_w - 1) & 0xffff);
	stride = DIV_ROUND_UP(fb->pitches[0], 4);
	dsp_stx = dst->x1;
	dsp_sty = dst->y1;
	dsp_st = dsp_sty << 16 | (dsp_stx & 0xffff);

	if (vpstate->tiled_en) {
		if (is_vop3(vop2))
			format = vop3_convert_tiled_format(fb->format->format, vpstate->tiled_en);
		else
			format = vop2_convert_tiled_format(fb->format->format);
	} else {
		format = vop2_convert_format(fb->format->format);
	}

	vop2_setup_csc_mode(vp, vpstate);

	afbc_half_block_en = vop2_afbc_half_block_enable(vpstate);

	vop2_win_enable(win);
	spin_lock(&vop2->reg_lock);
	rockchip_drm_dbg(vop2->dev, VOP_DEBUG_PLANE,
			 "vp%d update %s[%dx%d->%dx%d@(%d, %d)] fmt[%.4s%s] addr[%pad] by %s\n",
			 vp->id, win->name, actual_w, actual_h, dsp_w, dsp_h,
			 dsp_stx, dsp_sty,
			 drm_get_format_name(fb->format->format, &format_name),
			 modifier_to_string(fb->modifier), &vpstate->yrgb_mst, current->comm);

	if (vop2->version != VOP_VERSION_RK3568)
		rk3588_vop2_win_cfg_axi(win);

	if (!win->parent && !vop2_cluster_window(win) && is_vop3(vop2))
		VOP_WIN_SET(vop2, win, scale_engine_num, win->scale_engine_num);

	if (vpstate->afbc_en) {
		/* the afbc superblock is 16 x 16 */
		afbc_format = vop2_convert_afbc_format(fb->format->format);
		/* Enable color transform for YTR */
		if (fb->modifier & AFBC_FORMAT_MOD_YTR)
			afbc_format |= (1 << 4);
		afbc_tile_num = ALIGN(actual_w, 16) >> 4;

		/* The right win should have a src  offset in splice mode */
		afbc_xoffset = (src->x1 >> 16);
		/* AFBC pic_vir_width is count by pixel, this is different
		 * with WIN_VIR_STRIDE.
		 */
		if (!bpp) {
			WARN(1, "bpp is zero\n");
			bpp = 1;
		}
		stride = (fb->pitches[0] << 3) / bpp;
		if ((stride & 0x3f) &&
		    (vpstate->xmirror_en || vpstate->rotate_90_en || vpstate->rotate_270_en))
			DRM_ERROR("vp%d %s stride[%d] must align as 64 pixel when enable xmirror/rotate_90/rotate_270[0x%x]\n",
				  vp->id, win->name, stride, pstate->rotation);

		rb_swap = vop2_afbc_rb_swap(fb->format->format);
		uv_swap = vop2_afbc_uv_swap(fb->format->format);
		vpstate->afbc_half_block_en = afbc_half_block_en;

		transform_offset = vop2_afbc_transform_offset(vpstate, splice_pixel_offset);
		VOP_CLUSTER_SET(vop2, win, afbc_enable, 1);
		VOP_AFBC_SET(vop2, win, format, afbc_format);
		VOP_AFBC_SET(vop2, win, rb_swap, rb_swap);
		VOP_AFBC_SET(vop2, win, uv_swap, uv_swap);

		if (vop2->version == VOP_VERSION_RK3568)
			VOP_AFBC_SET(vop2, win, auto_gating_en, 0);
		else
			VOP_AFBC_SET(vop2, win, auto_gating_en, 1);
		VOP_AFBC_SET(vop2, win, block_split_en, 0);
		VOP_AFBC_SET(vop2, win, hdr_ptr, vpstate->yrgb_mst);
		VOP_AFBC_SET(vop2, win, pic_size, act_info);
		VOP_AFBC_SET(vop2, win, transform_offset, transform_offset);
		VOP_AFBC_SET(vop2, win, pic_offset, (afbc_xoffset | src->y1));
		VOP_AFBC_SET(vop2, win, dsp_offset, (dst->x1 | (dst->y1 << 16)));
		VOP_AFBC_SET(vop2, win, pic_vir_width, stride);
		VOP_AFBC_SET(vop2, win, tile_num, afbc_tile_num);
		VOP_AFBC_SET(vop2, win, xmirror, vpstate->xmirror_en);
		VOP_AFBC_SET(vop2, win, ymirror, vpstate->ymirror_en);
		VOP_AFBC_SET(vop2, win, rotate_270, vpstate->rotate_270_en);
		VOP_AFBC_SET(vop2, win, rotate_90, vpstate->rotate_90_en);
	} else {
		VOP_CLUSTER_SET(vop2, win, afbc_enable, 0);
		transform_offset = vop2_tile_transform_offset(vpstate, vpstate->tiled_en);
		VOP_AFBC_SET(vop2, win, transform_offset, transform_offset);
		VOP_WIN_SET(vop2, win, ymirror, vpstate->ymirror_en);
		VOP_WIN_SET(vop2, win, xmirror, vpstate->xmirror_en);
	}

	if (vpstate->rotate_90_en || vpstate->rotate_270_en) {
		act_info = swahw32(act_info);
		actual_w = drm_rect_height(src) >> 16;
		actual_h = drm_rect_width(src) >> 16;
	}

	yrgb_mst = vpstate->yrgb_mst + splice_yrgb_offset;
	uv_mst = vpstate->uv_mst + splice_uv_offset;
	/* rk3588 should set half_blocK_en to 1 in line and tile mode */
	VOP_AFBC_SET(vop2, win, half_block_en, afbc_half_block_en);

	VOP_WIN_SET(vop2, win, format, format);
	VOP_WIN_SET(vop2, win, yrgb_mst, yrgb_mst);

	rb_swap = vop2_win_rb_swap(fb->format->format);
	uv_swap = vop2_win_uv_swap(fb->format->format);
	if (vpstate->tiled_en) {
		uv_swap = 1;
		if (vpstate->tiled_en == ROCKCHIP_TILED_BLOCK_SIZE_8x8)
			stride <<= 3;
		else
			stride <<= 2;
	}
	VOP_WIN_SET(vop2, win, rb_swap, rb_swap);
	VOP_WIN_SET(vop2, win, uv_swap, uv_swap);

	if (fb->format->is_yuv) {
		uv_stride = DIV_ROUND_UP(fb->pitches[1], 4);
		if (vpstate->tiled_en) {
			int vsub = fb->format->vsub;

			if (vpstate->tiled_en == ROCKCHIP_TILED_BLOCK_SIZE_8x8)
				uv_stride = uv_stride * 8 / vsub;
			else
				uv_stride = uv_stride * 4 / vsub;
			VOP_WIN_SET(vop2, win, tile_mode, tile_4x4_m0);
		}

		VOP_WIN_SET(vop2, win, uv_vir, uv_stride);
		VOP_WIN_SET(vop2, win, uv_mst, uv_mst);
	}

	/* tile 4x4 m0 format, y and uv is packed together */
	if (tile_4x4_m0)
		VOP_WIN_SET(vop2, win, yrgb_vir, stride + uv_stride);
	else
		VOP_WIN_SET(vop2, win, yrgb_vir, stride);

	vop2_setup_scale(vop2, win, actual_w, actual_h, dsp_w, dsp_h, pstate);
	vop2_plane_setup_color_key(&win->base);
	VOP_WIN_SET(vop2, win, act_info, act_info);
	VOP_WIN_SET(vop2, win, dsp_info, dsp_info);
	VOP_WIN_SET(vop2, win, dsp_st, dsp_st);

	VOP_WIN_SET(vop2, win, y2r_en, vpstate->y2r_en);
	VOP_WIN_SET(vop2, win, r2y_en, vpstate->r2y_en);
	VOP_WIN_SET(vop2, win, csc_mode, vpstate->csc_mode);

	if (win->feature & WIN_FEATURE_Y2R_13BIT_DEPTH && !vop2_cluster_window(win))
		VOP_WIN_SET(vop2, win, csc_13bit_en, !!(vpstate->csc_mode & CSC_BT709L_13BIT));

	dither_up = vop2_win_dither_up(fb->format->format);
	VOP_WIN_SET(vop2, win, dither_up, dither_up);

	VOP_WIN_SET(vop2, win, enable, 1);
	if (vop2_cluster_window(win)) {
		lb_mode = vop2_get_cluster_lb_mode(win, vpstate);
		VOP_CLUSTER_SET(vop2, win, lb_mode, lb_mode);
		VOP_CLUSTER_SET(vop2, win, scl_lb_mode, lb_mode == 1 ? 3 : 0);
		VOP_CLUSTER_SET(vop2, win, enable, 1);
		VOP_CLUSTER_SET(vop2, win, frm_reset_en, 1);
	}
	spin_unlock(&vop2->reg_lock);
}

static void vop2_plane_atomic_update(struct drm_plane *plane, struct drm_plane_state *old_state)
{
	struct drm_plane_state *pstate = plane->state;
	struct drm_crtc *crtc = pstate->crtc;
	struct vop2_win *win = to_vop2_win(plane);
	struct vop2_win *splice_win;
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2_plane_state *vpstate = to_vop2_plane_state(pstate);
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct drm_framebuffer *fb = pstate->fb;
	struct drm_format_name_buf format_name;
	struct vop2 *vop2 = win->vop2;
	struct drm_rect wsrc;
	struct drm_rect wdst;
	/* right part in splice mode */
	struct drm_rect right_wsrc;
	struct drm_rect right_wdst;

#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	struct drm_rect *psrc = &vpstate->src;
	bool AFBC_flag = false;
	struct vop_dump_list *planlist;
	unsigned long num_pages;
	struct page **pages;
	struct drm_gem_object *obj;
	struct rockchip_gem_object *rk_obj;

	num_pages = 0;
	pages = NULL;
	obj = fb->obj[0];
	rk_obj = to_rockchip_obj(obj);
	if (rk_obj) {
		num_pages = rk_obj->num_pages;
		pages = rk_obj->pages;
	}
	if (rockchip_afbc(plane, fb->modifier))
		AFBC_flag = true;
	else
		AFBC_flag = false;
#endif

	/*
	 * can't update plane when vop2 is disabled.
	 */
	if (WARN_ON(!crtc))
		return;

	if (WARN_ON(!vop2->is_enabled))
		return;

	if (!pstate->visible) {
		vop2_plane_atomic_disable(plane, old_state);
		return;
	}

	/*
	 * This means this window is moved from another vp
	 * so the VOP2_PORT_SEL register is changed and
	 * take effect by vop2_wait_for_port_mux_done
	 * in this commit. so we can continue configure
	 * the window and report vsync
	 */
	if (win->old_vp_mask != win->vp_mask) {
		win->old_vp_mask = win->vp_mask;
		if (!is_vop3(vop2))
			vp->skip_vsync = false;
	}

	if (vcstate->splice_mode) {
		DRM_DEV_DEBUG(vop2->dev, "vp%d update %s[%dx%d->%dx%d@(%d,%d)] fmt[%.4s%s] addr[%pad]\n",
			      vp->id, win->name, drm_rect_width(&vpstate->src) >> 16,
			      drm_rect_height(&vpstate->src) >> 16,
			      drm_rect_width(&vpstate->dest), drm_rect_height(&vpstate->dest),
			      vpstate->dest.x1, vpstate->dest.y1,
			      drm_get_format_name(fb->format->format, &format_name),
			      modifier_to_string(fb->modifier), &vpstate->yrgb_mst);

		vop2_calc_drm_rect_for_splice(vpstate, &wsrc, &wdst, &right_wsrc, &right_wdst);
		splice_win = win->splice_win;
		vop2_win_atomic_update(splice_win, &right_wsrc, &right_wdst, pstate);
	} else {
		memcpy(&wsrc, &vpstate->src, sizeof(struct drm_rect));
		memcpy(&wdst, &vpstate->dest, sizeof(struct drm_rect));
	}

	vop2_win_atomic_update(win, &wsrc, &wdst, pstate);

	vop2->is_iommu_needed = true;
#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	kfree(vpstate->planlist);
	vpstate->planlist = NULL;

	planlist = kmalloc(sizeof(*planlist), GFP_KERNEL);
	if (planlist) {
		planlist->dump_info.AFBC_flag = AFBC_flag;
		planlist->dump_info.area_id = win->area_id;
		planlist->dump_info.win_id = win->win_id;
		planlist->dump_info.yuv_format = fb->format->is_yuv;
		planlist->dump_info.num_pages = num_pages;
		planlist->dump_info.pages = pages;
		planlist->dump_info.offset = vpstate->offset;
		planlist->dump_info.pitches = fb->pitches[0];
		planlist->dump_info.height = drm_rect_height(psrc) >> 16;
		planlist->dump_info.format = fb->format;
		list_add_tail(&planlist->entry, &vp->rockchip_crtc.vop_dump_list_head);
		vpstate->planlist = planlist;
	} else {
		DRM_ERROR("can't alloc a node of planlist %p\n", planlist);
		return;
	}
	if (vp->rockchip_crtc.vop_dump_status == DUMP_KEEP ||
	    vp->rockchip_crtc.vop_dump_times > 0) {
		rockchip_drm_dump_plane_buffer(&planlist->dump_info, vp->rockchip_crtc.frame_count);
		vp->rockchip_crtc.vop_dump_times--;
	}
#endif
}

static const struct drm_plane_helper_funcs vop2_plane_helper_funcs = {
	.atomic_check = vop2_plane_atomic_check,
	.atomic_update = vop2_plane_atomic_update,
	.atomic_disable = vop2_plane_atomic_disable,
};

/**
 * rockchip_atomic_helper_update_plane copy from drm_atomic_helper_update_plane
 * be designed to support async commit at ioctl DRM_IOCTL_MODE_SETPLANE.
 * @plane: plane object to update
 * @crtc: owning CRTC of owning plane
 * @fb: framebuffer to flip onto plane
 * @crtc_x: x offset of primary plane on crtc
 * @crtc_y: y offset of primary plane on crtc
 * @crtc_w: width of primary plane rectangle on crtc
 * @crtc_h: height of primary plane rectangle on crtc
 * @src_x: x offset of @fb for panning
 * @src_y: y offset of @fb for panning
 * @src_w: width of source rectangle in @fb
 * @src_h: height of source rectangle in @fb
 * @ctx: lock acquire context
 *
 * Provides a default plane update handler using the atomic driver interface.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
static int __maybe_unused
rockchip_atomic_helper_update_plane(struct drm_plane *plane,
				    struct drm_crtc *crtc,
				    struct drm_framebuffer *fb,
				    int crtc_x, int crtc_y,
				    unsigned int crtc_w, unsigned int crtc_h,
				    uint32_t src_x, uint32_t src_y,
				    uint32_t src_w, uint32_t src_h,
				    struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_plane_state *pstate;
	struct vop2_plane_state *vpstate;
	int ret = 0;

	state = drm_atomic_state_alloc(plane->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;
	pstate = drm_atomic_get_plane_state(state, plane);
	if (IS_ERR(pstate)) {
		ret = PTR_ERR(pstate);
		goto fail;
	}

	vpstate = to_vop2_plane_state(pstate);

	ret = drm_atomic_set_crtc_for_plane(pstate, crtc);
	if (ret != 0)
		goto fail;
	drm_atomic_set_fb_for_plane(pstate, fb);
	pstate->crtc_x = crtc_x;
	pstate->crtc_y = crtc_y;
	pstate->crtc_w = crtc_w;
	pstate->crtc_h = crtc_h;
	pstate->src_x = src_x;
	pstate->src_y = src_y;
	pstate->src_w = src_w;
	pstate->src_h = src_h;

	if (plane == crtc->cursor || vpstate->async_commit)
		state->legacy_cursor_update = true;

	ret = drm_atomic_commit(state);
fail:
	drm_atomic_state_put(state);
	return ret;
}

/**
 * drm_atomic_helper_disable_plane copy from drm_atomic_helper_disable_plane
 * be designed to support async commit at ioctl DRM_IOCTL_MODE_SETPLANE.
 *
 * @plane: plane to disable
 * @ctx: lock acquire context
 *
 * Provides a default plane disable handler using the atomic driver interface.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
static int __maybe_unused
rockchip_atomic_helper_disable_plane(struct drm_plane *plane,
				     struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_plane_state *pstate;
	struct vop2_plane_state *vpstate;
	int ret = 0;

	state = drm_atomic_state_alloc(plane->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;
	pstate = drm_atomic_get_plane_state(state, plane);
	if (IS_ERR(pstate)) {
		ret = PTR_ERR(pstate);
		goto fail;
	}
	vpstate = to_vop2_plane_state(pstate);

	if ((pstate->crtc && pstate->crtc->cursor == plane) ||
	    vpstate->async_commit)
		pstate->state->legacy_cursor_update = true;

	ret = __drm_atomic_helper_disable_plane(plane, pstate);
	if (ret != 0)
		goto fail;

	ret = drm_atomic_commit(state);
fail:
	drm_atomic_state_put(state);
	return ret;
}

static void vop2_plane_destroy(struct drm_plane *plane)
{
	drm_plane_cleanup(plane);
}

static void vop2_atomic_plane_reset(struct drm_plane *plane)
{
	struct vop2_plane_state *vpstate = to_vop2_plane_state(plane->state);
	struct vop2_win *win = to_vop2_win(plane);

	if (plane->state && plane->state->fb)
		__drm_atomic_helper_plane_destroy_state(plane->state);
	kfree(vpstate);
	vpstate = kzalloc(sizeof(*vpstate), GFP_KERNEL);
	if (!vpstate)
		return;

	__drm_atomic_helper_plane_reset(plane, &vpstate->base);
	vpstate->base.zpos = win->zpos;
}

static struct drm_plane_state *vop2_atomic_plane_duplicate_state(struct drm_plane *plane)
{
	struct vop2_plane_state *old_vpstate;
	struct vop2_plane_state *vpstate;

	if (WARN_ON(!plane->state))
		return NULL;

	old_vpstate = to_vop2_plane_state(plane->state);
	vpstate = kmemdup(old_vpstate, sizeof(*vpstate), GFP_KERNEL);
	if (!vpstate)
		return NULL;

	vpstate->hdr_in = 0;
	vpstate->hdr2sdr_en = 0;

	__drm_atomic_helper_plane_duplicate_state(plane, &vpstate->base);

	return &vpstate->base;
}

static void vop2_atomic_plane_destroy_state(struct drm_plane *plane,
					    struct drm_plane_state *state)
{
	struct vop2_plane_state *vpstate = to_vop2_plane_state(state);

	__drm_atomic_helper_plane_destroy_state(state);

	kfree(vpstate);
}

static int vop2_atomic_plane_set_property(struct drm_plane *plane,
					  struct drm_plane_state *state,
					  struct drm_property *property,
					  uint64_t val)
{
	struct rockchip_drm_private *private = plane->dev->dev_private;
	struct vop2_plane_state *vpstate = to_vop2_plane_state(state);
	struct vop2_win *win = to_vop2_win(plane);

	if (property == private->eotf_prop) {
		vpstate->eotf = val;
		return 0;
	}

	if (property == private->color_space_prop) {
		vpstate->color_space = val;
		return 0;
	}

	if (property == private->async_commit_prop) {
		vpstate->async_commit = val;
		return 0;
	}

	if (property == win->color_key_prop) {
		vpstate->color_key = val;
		return 0;
	}

	DRM_ERROR("failed to set vop2 plane property id:%d, name:%s\n",
		  property->base.id, property->name);

	return -EINVAL;
}

static int vop2_atomic_plane_get_property(struct drm_plane *plane,
					  const struct drm_plane_state *state,
					  struct drm_property *property,
					  uint64_t *val)
{
	struct rockchip_drm_private *private = plane->dev->dev_private;
	struct vop2_plane_state *vpstate = to_vop2_plane_state(state);
	struct vop2_win *win = to_vop2_win(plane);

	if (property == private->eotf_prop) {
		*val = vpstate->eotf;
		return 0;
	}

	if (property == private->color_space_prop) {
		*val = vpstate->color_space;
		return 0;
	}

	if (property == private->async_commit_prop) {
		*val = vpstate->async_commit;
		return 0;
	}

	if (property == private->share_id_prop) {
		int i;
		struct drm_mode_object *obj = &plane->base;

		for (i = 0; i < obj->properties->count; i++) {
			if (obj->properties->properties[i] == property) {
				*val = obj->properties->values[i];
				return 0;
			}
		}
	}

	if (property == win->color_key_prop) {
		*val = vpstate->color_key;
		return 0;
	}

	DRM_ERROR("failed to get vop2 plane property id:%d, name:%s\n",
		  property->base.id, property->name);

	return -EINVAL;
}

static const struct drm_plane_funcs vop2_plane_funcs = {
	.update_plane	= rockchip_atomic_helper_update_plane,
	.disable_plane	= rockchip_atomic_helper_disable_plane,
	.destroy = vop2_plane_destroy,
	.reset = vop2_atomic_plane_reset,
	.atomic_duplicate_state = vop2_atomic_plane_duplicate_state,
	.atomic_destroy_state = vop2_atomic_plane_destroy_state,
	.atomic_set_property = vop2_atomic_plane_set_property,
	.atomic_get_property = vop2_atomic_plane_get_property,
	.format_mod_supported = rockchip_vop2_mod_supported,
};

static int vop2_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	unsigned long flags;

	if (WARN_ON(!vop2->is_enabled))
		return -EPERM;

	spin_lock_irqsave(&vop2->irq_lock, flags);

	VOP_INTR_SET_TYPE(vop2, intr, clear, FS_FIELD_INTR, 1);
	VOP_INTR_SET_TYPE(vop2, intr, enable, FS_FIELD_INTR, 1);

	spin_unlock_irqrestore(&vop2->irq_lock, flags);

	return 0;
}

static void vop2_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	unsigned long flags;

	if (WARN_ON(!vop2->is_enabled))
		return;

	spin_lock_irqsave(&vop2->irq_lock, flags);

	VOP_INTR_SET_TYPE(vop2, intr, enable, FS_FIELD_INTR, 0);

	spin_unlock_irqrestore(&vop2->irq_lock, flags);
}

static void vop2_crtc_cancel_pending_vblank(struct drm_crtc *crtc,
					    struct drm_file *file_priv)
{
	struct drm_device *drm = crtc->dev;
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_pending_vblank_event *e;
	unsigned long flags;

	spin_lock_irqsave(&drm->event_lock, flags);
	e = vp->event;
	if (e && e->base.file_priv == file_priv) {
		vp->event = NULL;

		//e->base.destroy(&e->base);//todo
		file_priv->event_space += sizeof(e->event);
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);
}

static bool vop2_crtc_line_flag_irq_is_enabled(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	uint32_t line_flag_irq;
	unsigned long flags;

	spin_lock_irqsave(&vop2->irq_lock, flags);
	line_flag_irq = VOP_INTR_GET_TYPE(vop2, intr, enable, LINE_FLAG_INTR);
	spin_unlock_irqrestore(&vop2->irq_lock, flags);

	return !!line_flag_irq;
}

static void vop2_crtc_line_flag_irq_enable(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	unsigned long flags;

	if (!vop2->is_enabled)
		return;

	spin_lock_irqsave(&vop2->irq_lock, flags);
	VOP_INTR_SET_TYPE(vop2, intr, clear, LINE_FLAG_INTR, 1);
	VOP_INTR_SET_TYPE(vop2, intr, enable, LINE_FLAG_INTR, 1);
	spin_unlock_irqrestore(&vop2->irq_lock, flags);
}

static void vop2_crtc_line_flag_irq_disable(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	unsigned long flags;

	if (!vop2->is_enabled)
		return;

	spin_lock_irqsave(&vop2->irq_lock, flags);
	VOP_INTR_SET_TYPE(vop2, intr, enable, LINE_FLAG_INTR, 0);
	spin_unlock_irqrestore(&vop2->irq_lock, flags);
}

static void vop3_mcu_mode_setup(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	VOP_MODULE_SET(vop2, vp, mcu_type, 1);
	VOP_MODULE_SET(vop2, vp, mcu_hold_mode, 1);
	VOP_MODULE_SET(vop2, vp, mcu_pix_total, vp->mcu_timing.mcu_pix_total);
	VOP_MODULE_SET(vop2, vp, mcu_cs_pst, vp->mcu_timing.mcu_cs_pst);
	VOP_MODULE_SET(vop2, vp, mcu_cs_pend, vp->mcu_timing.mcu_cs_pend);
	VOP_MODULE_SET(vop2, vp, mcu_rw_pst, vp->mcu_timing.mcu_rw_pst);
	VOP_MODULE_SET(vop2, vp, mcu_rw_pend, vp->mcu_timing.mcu_rw_pend);
}

static void vop3_mcu_bypass_mode_setup(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	VOP_MODULE_SET(vop2, vp, mcu_type, 1);
	VOP_MODULE_SET(vop2, vp, mcu_hold_mode, 1);
	VOP_MODULE_SET(vop2, vp, mcu_pix_total, 53);
	VOP_MODULE_SET(vop2, vp, mcu_cs_pst, 6);
	VOP_MODULE_SET(vop2, vp, mcu_cs_pend, 48);
	VOP_MODULE_SET(vop2, vp, mcu_rw_pst, 12);
	VOP_MODULE_SET(vop2, vp, mcu_rw_pend, 30);
}

static u32 vop3_mode_done(struct vop2_video_port *vp)
{
	return VOP_MODULE_GET(vp->vop2, vp, out_mode);
}

static void vop3_set_out_mode(struct drm_crtc *crtc, u32 out_mode)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	int ret;
	u32 val;

	VOP_MODULE_SET(vop2, vp, out_mode, out_mode);
	vop2_cfg_done(crtc);
	ret = readx_poll_timeout(vop3_mode_done, vp, val, val == out_mode,
				 1000, 500 * 1000);
	if (ret)
		dev_err(vop2->dev, "wait mode 0x%x timeout\n", out_mode);
}

static void vop3_crtc_send_mcu_cmd(struct drm_crtc *crtc, u32 type, u32 value)
{
	struct drm_crtc_state *crtc_state;
	struct drm_display_mode *adjusted_mode;
	struct vop2_video_port *vp;
	struct vop2 *vop2;

	if (!crtc)
		return;

	crtc_state = crtc->state;
	adjusted_mode = &crtc_state->adjusted_mode;
	vp = to_vop2_video_port(crtc);
	vop2 = vp->vop2;

	/*
	 * 1.set mcu bypass mode timing.
	 * 2.set dclk rate to 150M.
	 */
	if ((type == MCU_SETBYPASS) && value) {
		vop3_mcu_bypass_mode_setup(crtc);
		clk_set_rate(vp->dclk, 150000000);
	}

	mutex_lock(&vop2->vop2_lock);
	if (vop2 && vop2->is_enabled) {
		switch (type) {
		case MCU_WRCMD:
			VOP_MODULE_SET(vop2, vp, mcu_rs, 0);
			VOP_MODULE_SET(vop2, vp, mcu_rw_bypass_port, value);
			VOP_MODULE_SET(vop2, vp, mcu_rs, 1);
			break;
		case MCU_WRDATA:
			VOP_MODULE_SET(vop2, vp, mcu_rs, 1);
			VOP_MODULE_SET(vop2, vp, mcu_rw_bypass_port, value);
			break;
		case MCU_SETBYPASS:
			VOP_MODULE_SET(vop2, vp, mcu_bypass, value ? 1 : 0);
			break;
		default:
			break;
		}
	}
	mutex_unlock(&vop2->vop2_lock);

	/*
	 * 1.restore mcu data mode timing.
	 * 2.restore dclk rate to crtc_clock.
	 */
	if ((type == MCU_SETBYPASS) && !value) {
		vop3_mcu_mode_setup(crtc);
		clk_set_rate(vp->dclk, adjusted_mode->crtc_clock * 1000);
	}
}

static int vop2_crtc_wait_vact_end(struct drm_crtc *crtc, unsigned int mstimeout)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	unsigned long jiffies_left;
	int ret = 0;

	if (!vop2->is_enabled)
		return -ENODEV;

	mutex_lock(&vop2->vop2_lock);

	if (vop2_crtc_line_flag_irq_is_enabled(vp)) {
		ret = -EBUSY;
		goto out;
	}

	reinit_completion(&vp->line_flag_completion);
	vop2_crtc_line_flag_irq_enable(vp);
	jiffies_left = wait_for_completion_timeout(&vp->line_flag_completion,
						   msecs_to_jiffies(mstimeout));
	vop2_crtc_line_flag_irq_disable(vp);

	if (jiffies_left == 0) {
		DRM_DEV_ERROR(vop2->dev, "timeout waiting for lineflag IRQ\n");
		ret = -ETIMEDOUT;
		goto out;
	}

out:
	mutex_unlock(&vop2->vop2_lock);
	return ret;
}

static int vop2_crtc_enable_line_flag_event(struct drm_crtc *crtc, uint32_t line)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	unsigned long flags;

	if (WARN_ON(!vop2->is_enabled))
		return -EPERM;

	spin_lock_irqsave(&vop2->irq_lock, flags);

	VOP_INTR_SET(vop2, intr, line_flag_num[1], line);

	VOP_INTR_SET_TYPE(vop2, intr, clear, LINE_FLAG1_INTR, 1);
	VOP_INTR_SET_TYPE(vop2, intr, enable, LINE_FLAG1_INTR, 1);

	spin_unlock_irqrestore(&vop2->irq_lock, flags);

	return 0;
}

static void vop2_crtc_disable_line_flag_event(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	unsigned long flags;

	if (WARN_ON(!vop2->is_enabled))
		return;

	spin_lock_irqsave(&vop2->irq_lock, flags);

	VOP_INTR_SET_TYPE(vop2, intr, enable, LINE_FLAG1_INTR, 0);

	spin_unlock_irqrestore(&vop2->irq_lock, flags);
}

static int vop2_crtc_loader_protect(struct drm_crtc *crtc, bool on)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2 *vop2 = vp->vop2;
	struct rockchip_drm_private *private = crtc->dev->dev_private;
	const struct vop2_video_port_data *vp_data = &vop2->data->vp[vp->id];
	struct vop2_video_port *splice_vp = &vop2->vps[vp_data->splice_vp_id];
	struct drm_crtc_state *crtc_state;
	struct drm_display_mode *mode;
	struct vop2_win *win, *splice_win;
	struct vop2_extend_pll *ext_pll;
	struct clk *parent_clk;
	const char *clk_name;

	if (on == vp->loader_protect)
		return 0;

	if (on) {
		vp->loader_protect = true;
		vop2->active_vp_mask |= BIT(vp->id);
		vop2_set_system_status(vop2);
		vop2_initial(crtc);
		if (crtc->primary) {
			win = to_vop2_win(crtc->primary);
			if (VOP_WIN_GET(vop2, win, enable)) {
				if (win->pd) {
					win->pd->ref_count++;
					win->pd->vp_mask |= BIT(vp->id);
				}

				crtc_state = drm_atomic_get_crtc_state(crtc->state->state, crtc);
				mode = &crtc_state->adjusted_mode;
				if (mode->hdisplay > VOP2_MAX_VP_OUTPUT_WIDTH)	{
					vcstate->splice_mode = true;
					splice_win = vop2_find_win_by_phys_id(vop2,
									      win->splice_win_id);
					splice_win->splice_mode_right = true;
					splice_win->left_win = win;
					win->splice_win = splice_win;
					splice_vp->win_mask |=  BIT(splice_win->phys_id);
					splice_win->vp_mask = BIT(splice_vp->id);
					vop2->active_vp_mask |= BIT(splice_vp->id);

					if (splice_win->pd &&
					    VOP_WIN_GET(vop2, splice_win, enable)) {
						splice_win->pd->ref_count++;
						splice_win->pd->vp_mask |= BIT(splice_vp->id);
					}
				}
			}
		}
		parent_clk = clk_get_parent(vp->dclk);
		clk_name = __clk_get_name(parent_clk);
		if (!strcmp(clk_name, "clk_hdmiphy_pixel0")) {
			ext_pll = vop2_extend_clk_find_by_name(vop2, "hdmi0_phy_pll");
			if (ext_pll)
				ext_pll->vp_mask |= BIT(vp->id);
		} else if (!strcmp(clk_name, "clk_hdmiphy_pixel1")) {
			ext_pll = vop2_extend_clk_find_by_name(vop2, "hdmi1_phy_pll");
			if (ext_pll)
				ext_pll->vp_mask |= BIT(vp->id);
		}
		drm_crtc_vblank_on(crtc);
		if (private->cubic_lut[vp->id].enable) {
			dma_addr_t cubic_lut_mst;
			struct loader_cubic_lut *cubic_lut = &private->cubic_lut[vp->id];

			cubic_lut_mst = cubic_lut->offset + private->cubic_lut_dma_addr;
			VOP_MODULE_SET(vop2, vp, cubic_lut_mst, cubic_lut_mst);
		}
	} else {
		vop2_crtc_atomic_disable(crtc, NULL);
	}

	return 0;
}

#define DEBUG_PRINT(args...) \
		do { \
			if (s) \
				seq_printf(s, args); \
			else \
				pr_err(args); \
		} while (0)

static int vop2_plane_info_dump(struct seq_file *s, struct drm_plane *plane)
{
	struct vop2_win *win = to_vop2_win(plane);
	struct drm_plane_state *pstate = plane->state;
	struct vop2_plane_state *vpstate = to_vop2_plane_state(pstate);
	struct drm_rect *src, *dest;
	struct drm_framebuffer *fb = pstate->fb;
	struct drm_format_name_buf format_name;
	struct drm_gem_object *obj;
	struct rockchip_gem_object *rk_obj;
	dma_addr_t fb_addr;

	int i;

	DEBUG_PRINT("    %s: %s\n", win->name, pstate->crtc ? "ACTIVE" : "DISABLED");
	if (!fb)
		return 0;

	src = &vpstate->src;
	dest = &vpstate->dest;

	DEBUG_PRINT("\twin_id: %d\n", win->win_id);

	drm_get_format_name(fb->format->format, &format_name);
	DEBUG_PRINT("\tformat: %s%s%s[%d] color_space[%d] glb_alpha[0x%x]\n",
		    format_name.str,
		    modifier_to_string(fb->modifier),
		    vpstate->eotf ? " HDR" : " SDR", vpstate->eotf,
		    vpstate->color_space, vpstate->global_alpha);
	DEBUG_PRINT("\trotate: xmirror: %d ymirror: %d rotate_90: %d rotate_270: %d\n",
		    vpstate->xmirror_en, vpstate->ymirror_en, vpstate->rotate_90_en,
		    vpstate->rotate_270_en);
	DEBUG_PRINT("\tcsc: y2r[%d] r2y[%d] csc mode[%d]\n",
		    vpstate->y2r_en, vpstate->r2y_en,
		    vpstate->csc_mode);
	DEBUG_PRINT("\tzpos: %d\n", vpstate->zpos);
	DEBUG_PRINT("\tsrc: pos[%d, %d] rect[%d x %d]\n", src->x1 >> 16,
		    src->y1 >> 16, drm_rect_width(src) >> 16,
		    drm_rect_height(src) >> 16);
	DEBUG_PRINT("\tdst: pos[%d, %d] rect[%d x %d]\n", dest->x1, dest->y1,
		    drm_rect_width(dest), drm_rect_height(dest));

	for (i = 0; i < fb->format->num_planes; i++) {
		obj = fb->obj[0];
		rk_obj = to_rockchip_obj(obj);
		fb_addr = rk_obj->dma_addr + fb->offsets[0];

		DEBUG_PRINT("\tbuf[%d]: addr: %pad pitch: %d offset: %d\n",
			    i, &fb_addr, fb->pitches[i], fb->offsets[i]);
	}

	return 0;
}

static void vop2_dump_connector_on_crtc(struct drm_crtc *crtc, struct seq_file *s)
{
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;

	drm_connector_list_iter_begin(crtc->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (crtc->state->connector_mask & drm_connector_mask(connector))
			DEBUG_PRINT("    Connector: %s\n", connector->name);

	}
	drm_connector_list_iter_end(&conn_iter);
}

static int vop2_crtc_debugfs_dump(struct drm_crtc *crtc, struct seq_file *s)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_crtc_state *crtc_state = crtc->state;
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct rockchip_crtc_state *state = to_rockchip_crtc_state(crtc->state);
	bool interlaced = !!(mode->flags & DRM_MODE_FLAG_INTERLACE);
	struct drm_plane *plane;

	DEBUG_PRINT("Video Port%d: %s\n", vp->id, crtc_state->active ? "ACTIVE" : "DISABLED");

	if (!crtc_state->active)
		return 0;

	vop2_dump_connector_on_crtc(crtc, s);
	DEBUG_PRINT("\tbus_format[%x]: %s\n", state->bus_format,
		    drm_get_bus_format_name(state->bus_format));
	DEBUG_PRINT("\toverlay_mode[%d] output_mode[%x]",
		    state->yuv_overlay, state->output_mode);
	DEBUG_PRINT(" color_space[%d], eotf:%d\n",
		    state->color_space, state->eotf);
	DEBUG_PRINT("    Display mode: %dx%d%s%d\n",
		    mode->hdisplay, mode->vdisplay, interlaced ? "i" : "p",
		    drm_mode_vrefresh(mode));
	DEBUG_PRINT("\tclk[%d] real_clk[%d] type[%x] flag[%x]\n",
		    mode->clock, mode->crtc_clock, mode->type, mode->flags);
	DEBUG_PRINT("\tH: %d %d %d %d\n", mode->hdisplay, mode->hsync_start,
		    mode->hsync_end, mode->htotal);
	DEBUG_PRINT("\tV: %d %d %d %d\n", mode->vdisplay, mode->vsync_start,
		    mode->vsync_end, mode->vtotal);

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		vop2_plane_info_dump(s, plane);
	}

	return 0;
}

static void vop2_crtc_regs_dump(struct drm_crtc *crtc, struct seq_file *s)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	struct drm_crtc_state *cstate = crtc->state;
	const struct vop_dump_regs *regs = vop2->data->dump_regs;
	uint32_t buf[68];
	uint32_t len = ARRAY_SIZE(buf);
	unsigned int n, i, j;
	resource_size_t offset_addr;
	uint32_t base;
	struct drm_crtc *first_active_crtc = NULL;

	if (!cstate->active)
		return;

	/* only need to dump once at first active crtc for vop2 */
	for (i = 0; i < vop2_data->nr_vps; i++) {
		if (vop2->vps[i].rockchip_crtc.crtc.state->active) {
			first_active_crtc = &vop2->vps[i].rockchip_crtc.crtc;
			break;
		}
	}
	if (first_active_crtc != crtc)
		return;

	n = vop2->data->dump_regs_size;
	for (i = 0; i < n; i++) {
		base = regs[i].offset;
		offset_addr = vop2->res->start + base;
		DEBUG_PRINT("\n%s:\n", regs[i].name);
		for (j = 0; j < len;) {
			DEBUG_PRINT("%08x:  %08x %08x %08x %08x\n", (u32)offset_addr + j * 4,
				    vop2_readl(vop2, base + (4 * j)),
				    vop2_readl(vop2, base + (4 * (j + 1))),
				    vop2_readl(vop2, base + (4 * (j + 2))),
				    vop2_readl(vop2, base + (4 * (j + 3))));
			j += 4;
		}
	}
}

static void vop2_crtc_active_regs_dump(struct drm_crtc *crtc, struct seq_file *s)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	struct drm_crtc_state *cstate = crtc->state;
	const struct vop_dump_regs *regs = vop2->data->dump_regs;
	uint32_t buf[68];
	uint32_t len = ARRAY_SIZE(buf);
	unsigned int n, i, j;
	resource_size_t offset_addr;
	uint32_t base;
	struct drm_crtc *first_active_crtc = NULL;

	if (!cstate->active)
		return;

	/* only need to dump once at first active crtc for vop2 */
	for (i = 0; i < vop2_data->nr_vps; i++) {
		if (vop2->vps[i].rockchip_crtc.crtc.state->active) {
			first_active_crtc = &vop2->vps[i].rockchip_crtc.crtc;
			break;
		}
	}
	if (first_active_crtc != crtc)
		return;

	n = vop2->data->dump_regs_size;
	for (i = 0; i < n; i++) {
		if (regs[i].state.mask &&
		    REG_GET(vop2, regs[i].state) != regs[i].enable_state)
			continue;
		base = regs[i].offset;
		offset_addr = vop2->res->start + base;
		DEBUG_PRINT("\n%s:\n", regs[i].name);
		for (j = 0; j < len;) {
			DEBUG_PRINT("%08x:  %08x %08x %08x %08x\n", (u32)offset_addr + j * 4,
				    vop2_readl(vop2, base + (4 * j)),
				    vop2_readl(vop2, base + (4 * (j + 1))),
				    vop2_readl(vop2, base + (4 * (j + 2))),
				    vop2_readl(vop2, base + (4 * (j + 3))));
			j += 4;
		}
	}
}

static int vop2_gamma_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct vop2 *vop2 = node->info_ent->data;
	int i, j;

	for (i = 0; i < vop2->data->nr_vps; i++) {
		struct vop2_video_port *vp = &vop2->vps[i];

		if (!vp->lut || !vp->gamma_lut_active ||
		    !vop2->lut_regs || !vp->rockchip_crtc.crtc.state->enable) {
			DEBUG_PRINT("Video port%d gamma disabled\n", vp->id);
			continue;
		}
		DEBUG_PRINT("Video port%d gamma:\n", vp->id);
		for (j = 0; j < vp->gamma_lut_len; j++) {
			if (j % 8 == 0)
				DEBUG_PRINT("\n");
			DEBUG_PRINT("0x%08x ", vp->lut[j]);
		}
		DEBUG_PRINT("\n");
	}

	return 0;
}

static int vop2_cubic_lut_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct vop2 *vop2 = node->info_ent->data;
	struct rockchip_drm_private *private = vop2->drm_dev->dev_private;
	int i, j;

	for (i = 0; i < vop2->data->nr_vps; i++) {
		struct vop2_video_port *vp = &vop2->vps[i];

		if ((!vp->cubic_lut_gem_obj && !private->cubic_lut[vp->id].enable) ||
		    !vp->cubic_lut || !vp->rockchip_crtc.crtc.state->enable) {
			DEBUG_PRINT("Video port%d cubic lut disabled\n", vp->id);
			continue;
		}
		DEBUG_PRINT("Video port%d cubic lut:\n", vp->id);
		for (j = 0; j < vp->cubic_lut_len; j++) {
			DEBUG_PRINT("%04d: 0x%04x 0x%04x 0x%04x\n", j,
				    vp->cubic_lut[j].red,
				    vp->cubic_lut[j].green,
				    vp->cubic_lut[j].blue);
		}
		DEBUG_PRINT("\n");
	}

	return 0;
}

#undef DEBUG_PRINT

static struct drm_info_list vop2_debugfs_files[] = {
	{ "gamma_lut", vop2_gamma_show, 0, NULL },
	{ "cubic_lut", vop2_cubic_lut_show, 0, NULL },
};

static int vop2_crtc_debugfs_init(struct drm_minor *minor, struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	int ret, i;
	char name[12];

	snprintf(name, sizeof(name), "video_port%d", vp->id);
	vop2->debugfs = debugfs_create_dir(name, minor->debugfs_root);
	if (!vop2->debugfs)
		return -ENOMEM;

	vop2->debugfs_files = kmemdup(vop2_debugfs_files, sizeof(vop2_debugfs_files),
				      GFP_KERNEL);
	if (!vop2->debugfs_files) {
		ret = -ENOMEM;
		goto remove;
	}
#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	rockchip_drm_add_dump_buffer(crtc, vop2->debugfs);
	rockchip_drm_debugfs_add_color_bar(crtc, vop2->debugfs);
#endif
	for (i = 0; i < ARRAY_SIZE(vop2_debugfs_files); i++)
		vop2->debugfs_files[i].data = vop2;

	drm_debugfs_create_files(vop2->debugfs_files,
				 ARRAY_SIZE(vop2_debugfs_files),
				 vop2->debugfs,
				 minor);
	return 0;
remove:
	debugfs_remove(vop2->debugfs);
	vop2->debugfs = NULL;
	return ret;
}

static enum drm_mode_status
vop2_crtc_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *mode)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	int request_clock = mode->clock;
	int clock;
	unsigned long aclk_rate;
	uint8_t active_vp_mask = vop2->active_vp_mask;

	/*
	 * For RK3588, VP0 and VP1 will be both used in splice mode. All display
	 * modes of the right VP should be set as invalid when vop2 is working in
	 * splice mode.
	 */
	if (vp->splice_mode_right)
		return MODE_BAD;

	if ((active_vp_mask & BIT(ROCKCHIP_VOP_VP1)) && !vcstate->splice_mode &&
	    mode->hdisplay > VOP2_MAX_VP_OUTPUT_WIDTH) {
		DRM_DEV_DEBUG(vop2->dev, "can not support resolution %dx%d, vp1 is busy\n",
			      mode->hdisplay, mode->vdisplay);
		return MODE_BAD;
	}

	if (mode->hdisplay > vp_data->max_output.width)
		return MODE_BAD_HVALUE;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK || vcstate->output_if & VOP_OUTPUT_IF_BT656)
		request_clock *= 2;

	aclk_rate = clk_get_rate(vop2->aclk) / 1000;

	if (request_clock > VOP2_MAX_DCLK_RATE && aclk_rate <= VOP2_COMMON_ACLK_RATE)
		return MODE_BAD;

	if ((request_clock <= VOP2_MAX_DCLK_RATE) &&
	    (vop2_extend_clk_find_by_name(vop2, "hdmi0_phy_pll") ||
	     vop2_extend_clk_find_by_name(vop2, "hdmi1_phy_pll"))) {
		clock = request_clock;
	} else {
		if (request_clock > VOP2_MAX_DCLK_RATE)
			request_clock = request_clock >> 2;
		clock = clk_round_rate(vp->dclk, request_clock * 1000) / 1000;
	}

	/*
	 * Hdmi or DisplayPort request a Accurate clock.
	 */
	if (vcstate->output_type == DRM_MODE_CONNECTOR_HDMIA ||
	    vcstate->output_type == DRM_MODE_CONNECTOR_DisplayPort)
		if (clock != request_clock)
			return MODE_CLOCK_RANGE;

	return MODE_OK;
}

struct vop2_bandwidth {
	size_t bandwidth;
	int y1;
	int y2;
};

static int vop2_bandwidth_cmp(const void *a, const void *b)
{
	struct vop2_bandwidth *pa = (struct vop2_bandwidth *)a;
	struct vop2_bandwidth *pb = (struct vop2_bandwidth *)b;

	return pa->y1 - pb->y2;
}

static size_t vop2_plane_line_bandwidth(struct drm_plane_state *pstate)
{
	struct vop2_plane_state *vpstate = to_vop2_plane_state(pstate);
	struct drm_framebuffer *fb = pstate->fb;
	struct drm_rect *dst = &vpstate->dest;
	struct drm_rect *src = &vpstate->src;
	int bpp = rockchip_drm_get_bpp(fb->format);
	int src_width = drm_rect_width(src) >> 16;
	int src_height = drm_rect_height(src) >> 16;
	int dst_width = drm_rect_width(dst);
	int dst_height = drm_rect_height(dst);
	int vskiplines = scl_get_vskiplines(src_height, dst_height);
	size_t bandwidth;

	if (src_width <= 0 || src_height <= 0 || dst_width <= 0 ||
	    dst_height <= 0)
		return 0;

	bandwidth = src_width * bpp / 8;

	bandwidth = bandwidth * src_width / dst_width;
	bandwidth = bandwidth * src_height / dst_height;
	if (vskiplines == 2)
		bandwidth /= 2;
	else if (vskiplines == 4)
		bandwidth /= 4;

	return bandwidth;
}

static u64 vop2_calc_max_bandwidth(struct vop2_bandwidth *bw, int start,
				   int count, int y2)
{
	u64 max_bandwidth = 0;
	int i;

	for (i = start; i < count; i++) {
		u64 bandwidth = 0;

		if (bw[i].y1 > y2)
			continue;
		bandwidth = bw[i].bandwidth;
		bandwidth += vop2_calc_max_bandwidth(bw, i + 1, count,
						    min(bw[i].y2, y2));

		if (bandwidth > max_bandwidth)
			max_bandwidth = bandwidth;
	}

	return max_bandwidth;
}

static size_t vop2_crtc_bandwidth(struct drm_crtc *crtc,
				  struct drm_crtc_state *crtc_state,
				  struct dmcfreq_vop_info *vop_bw_info)
{
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	uint16_t htotal = adjusted_mode->crtc_htotal;
	uint16_t vdisplay = adjusted_mode->crtc_vdisplay;
	int clock = adjusted_mode->crtc_clock;
	struct drm_atomic_state *state = crtc_state->state;
	struct vop2_plane_state *vpstate;
	struct drm_plane_state *pstate;
	struct vop2_bandwidth *pbandwidth;
	struct drm_plane *plane;
	u64 line_bw_mbyte = 0;
	int8_t cnt = 0, plane_num = 0;
	int i = 0;
#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	struct vop_dump_list *pos, *n;
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
#endif

	if (!htotal || !vdisplay)
		return 0;

#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	if (!vp->rockchip_crtc.vop_dump_list_init_flag) {
		INIT_LIST_HEAD(&vp->rockchip_crtc.vop_dump_list_head);
		vp->rockchip_crtc.vop_dump_list_init_flag = true;
	}
	list_for_each_entry_safe(pos, n, &vp->rockchip_crtc.vop_dump_list_head, entry) {
		list_del(&pos->entry);
	}
	if (vp->rockchip_crtc.vop_dump_status == DUMP_KEEP ||
	    vp->rockchip_crtc.vop_dump_times > 0) {
		vp->rockchip_crtc.frame_count++;
	}
#endif

	for_each_new_plane_in_state(state, plane, pstate, i) {
		if (pstate->crtc == crtc)
			plane_num++;
	}

	vop_bw_info->plane_num += plane_num;
	pbandwidth = kmalloc_array(plane_num, sizeof(*pbandwidth),
				   GFP_KERNEL);
	if (!pbandwidth)
		return -ENOMEM;

	for_each_new_plane_in_state(state, plane, pstate, i) {
		int act_w, act_h, bpp, afbc_fac;
		int fps = drm_mode_vrefresh(adjusted_mode);

		if (!pstate || pstate->crtc != crtc || !pstate->fb)
			continue;

		/* This is an empirical value, if it's afbc format, the frame buffer size div 2 */
		afbc_fac = rockchip_afbc(plane, pstate->fb->modifier) ? 2 : 1;

		vpstate = to_vop2_plane_state(pstate);
		pbandwidth[cnt].y1 = vpstate->dest.y1;
		pbandwidth[cnt].y2 = vpstate->dest.y2;
		pbandwidth[cnt++].bandwidth = vop2_plane_line_bandwidth(pstate) / afbc_fac;

		act_w = drm_rect_width(&pstate->src) >> 16;
		act_h = drm_rect_height(&pstate->src) >> 16;
		bpp = rockchip_drm_get_bpp(pstate->fb->format);

		vop_bw_info->frame_bw_mbyte += act_w * act_h / 1000 * bpp / 8 * fps / 1000;
	}

	sort(pbandwidth, cnt, sizeof(pbandwidth[0]), vop2_bandwidth_cmp, NULL);

	line_bw_mbyte = vop2_calc_max_bandwidth(pbandwidth, 0, cnt, vdisplay);
	kfree(pbandwidth);
	/*
	 * line_bandwidth(MB/s)
	 *    = line_bandwidth / line_time
	 *    = line_bandwidth(Byte) * clock(KHZ) / 1000 / htotal
	 */
	line_bw_mbyte *= clock;
	do_div(line_bw_mbyte, htotal * 1000);
	vop_bw_info->line_bw_mbyte = line_bw_mbyte;

	return 0;
}

static void vop2_crtc_close(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	if (!crtc)
		return;

	mutex_lock(&vop2->vop2_lock);
	if (!vop2->is_enabled) {
		mutex_unlock(&vop2->vop2_lock);
		return;
	}

	vop2_disable_all_planes_for_crtc(crtc);
	mutex_unlock(&vop2->vop2_lock);
}

static void vop2_crtc_te_handler(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	if (!crtc || !crtc->state->active)
		return;

	VOP_MODULE_SET(vop2, vp, edpi_wms_fs, 1);
}

static int vop2_crtc_set_color_bar(struct drm_crtc *crtc, enum rockchip_color_bar_mode mode)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	int ret = 0;

	if (!crtc->state->active) {
		DRM_INFO("Video port%d disabled\n", vp->id);
		return -EINVAL;
	}

	switch (mode) {
	case ROCKCHIP_COLOR_BAR_OFF:
		DRM_INFO("disable color bar in VP%d\n", vp->id);
		VOP_MODULE_SET(vop2, vp, color_bar_en, 0);
		vop2_cfg_done(crtc);
		break;
	case ROCKCHIP_COLOR_BAR_HORIZONTAL:
		DRM_INFO("enable horizontal color bar in VP%d\n", vp->id);
		VOP_MODULE_SET(vop2, vp, color_bar_mode, 0);
		VOP_MODULE_SET(vop2, vp, color_bar_en, 1);
		vop2_cfg_done(crtc);
		break;
	case ROCKCHIP_COLOR_BAR_VERTICAL:
		DRM_INFO("enable vertical color bar in VP%d\n", vp->id);
		VOP_MODULE_SET(vop2, vp, color_bar_mode, 1);
		VOP_MODULE_SET(vop2, vp, color_bar_en, 1);
		vop2_cfg_done(crtc);
		break;
	default:
		DRM_INFO("Unsupported color bar mode\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct rockchip_crtc_funcs private_crtc_funcs = {
	.loader_protect = vop2_crtc_loader_protect,
	.cancel_pending_vblank = vop2_crtc_cancel_pending_vblank,
	.debugfs_init = vop2_crtc_debugfs_init,
	.debugfs_dump = vop2_crtc_debugfs_dump,
	.regs_dump = vop2_crtc_regs_dump,
	.active_regs_dump = vop2_crtc_active_regs_dump,
	.bandwidth = vop2_crtc_bandwidth,
	.crtc_close = vop2_crtc_close,
	.te_handler = vop2_crtc_te_handler,
	.crtc_send_mcu_cmd = vop3_crtc_send_mcu_cmd,
	.wait_vact_end = vop2_crtc_wait_vact_end,
	.crtc_standby = vop2_crtc_standby,
	.crtc_set_color_bar = vop2_crtc_set_color_bar,
};

static bool vop2_crtc_mode_fixup(struct drm_crtc *crtc,
				 const struct drm_display_mode *mode,
				 struct drm_display_mode *adj_mode)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct drm_crtc_state *new_crtc_state = container_of(mode, struct drm_crtc_state, mode);
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(new_crtc_state);

	/*
	 * For RK3568 and RK3588, the hactive of video timing must
	 * be 4-pixel aligned.
	 */
	if (vop2->version == VOP_VERSION_RK3568 || vop2->version == VOP_VERSION_RK3588) {
		if (adj_mode->hdisplay % 4) {
			u16 old_hdisplay = adj_mode->hdisplay;
			u16 align;

			align = 4 - (adj_mode->hdisplay % 4);
			adj_mode->hdisplay += align;
			adj_mode->hsync_start += align;
			adj_mode->hsync_end += align;
			adj_mode->htotal += align;

			DRM_WARN("VP%d: hactive need to be aligned with 4-pixel, %d -> %d\n",
				 vp->id, old_hdisplay, adj_mode->hdisplay);
		}
	}

	drm_mode_set_crtcinfo(adj_mode, CRTC_INTERLACE_HALVE_V | CRTC_STEREO_DOUBLE);

	if (mode->flags & DRM_MODE_FLAG_DBLCLK || vcstate->output_if & VOP_OUTPUT_IF_BT656)
		adj_mode->crtc_clock *= 2;

	if (vp->mcu_timing.mcu_pix_total) {
		if (vcstate->output_mode == ROCKCHIP_OUT_MODE_S888)
			adj_mode->crtc_clock *= 3;
		else if (vcstate->output_mode == ROCKCHIP_OUT_MODE_S888_DUMMY)
			adj_mode->crtc_clock *= 4;
	}

	drm_connector_list_iter_begin(crtc->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if ((new_crtc_state->connector_mask & drm_connector_mask(connector)) &&
		    ((connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort) ||
		     (connector->connector_type == DRM_MODE_CONNECTOR_HDMIA))) {
			drm_connector_list_iter_end(&conn_iter);
			return true;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	if (adj_mode->crtc_clock <= VOP2_MAX_DCLK_RATE)
		adj_mode->crtc_clock = DIV_ROUND_UP(clk_round_rate(vp->dclk,
						    adj_mode->crtc_clock * 1000), 1000);
	return true;
}

static void vop2_dither_setup(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	switch (vcstate->bus_format) {
	case MEDIA_BUS_FMT_RGB565_1X16:
		VOP_MODULE_SET(vop2, vp, dither_down_en, 1);
		VOP_MODULE_SET(vop2, vp, dither_down_mode, RGB888_TO_RGB565);
		VOP_MODULE_SET(vop2, vp, pre_dither_down_en, 1);
		break;
	case MEDIA_BUS_FMT_RGB666_1X18:
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
		VOP_MODULE_SET(vop2, vp, dither_down_en, 1);
		VOP_MODULE_SET(vop2, vp, dither_down_mode, RGB888_TO_RGB666);
		VOP_MODULE_SET(vop2, vp, pre_dither_down_en, 1);
		break;
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
		VOP_MODULE_SET(vop2, vp, dither_down_en, 0);
		VOP_MODULE_SET(vop2, vp, pre_dither_down_en, 1);
		break;
	case MEDIA_BUS_FMT_YUYV10_1X20:
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
	case MEDIA_BUS_FMT_RGB101010_1X30:
		VOP_MODULE_SET(vop2, vp, dither_down_en, 0);
		VOP_MODULE_SET(vop2, vp, pre_dither_down_en, 0);
		break;
	case MEDIA_BUS_FMT_RGB888_3X8:
	case MEDIA_BUS_FMT_RGB888_DUMMY_4X8:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
	default:
		VOP_MODULE_SET(vop2, vp, dither_down_en, 0);
		VOP_MODULE_SET(vop2, vp, pre_dither_down_en, 1);
		break;
	}

	VOP_MODULE_SET(vop2, vp, dither_down_sel, DITHER_DOWN_ALLEGRO);
}

static void vop2_post_config(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *vcstate =
			to_rockchip_crtc_state(crtc->state);
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	u16 vtotal = mode->crtc_vtotal;
	u16 hdisplay = mode->crtc_hdisplay;
	u16 hact_st = mode->crtc_htotal - mode->crtc_hsync_start;
	u16 vdisplay = mode->crtc_vdisplay;
	u16 vact_st = mode->crtc_vtotal - mode->crtc_vsync_start;
	u16 hsize = hdisplay * (vcstate->left_margin + vcstate->right_margin) / 200;
	u16 vsize = vdisplay * (vcstate->top_margin + vcstate->bottom_margin) / 200;
	u16 hact_end, vact_end;
	u32 val;

	vsize = rounddown(vsize, 2);
	hsize = rounddown(hsize, 2);
	hact_st += hdisplay * (100 - vcstate->left_margin) / 200;
	hact_end = hact_st + hsize;
	val = hact_st << 16;
	val |= hact_end;
	VOP_MODULE_SET(vop2, vp, hpost_st_end, val);
	vact_st += vdisplay * (100 - vcstate->top_margin) / 200;
	vact_end = vact_st + vsize;
	val = vact_st << 16;
	val |= vact_end;
	VOP_MODULE_SET(vop2, vp, vpost_st_end, val);
	val = scl_cal_scale2(vdisplay, vsize) << 16;
	val |= scl_cal_scale2(hdisplay, hsize);
	VOP_MODULE_SET(vop2, vp, post_scl_factor, val);

#define POST_HORIZONTAL_SCALEDOWN_EN(x)		((x) << 0)
#define POST_VERTICAL_SCALEDOWN_EN(x)		((x) << 1)
	VOP_MODULE_SET(vop2, vp, post_scl_ctrl,
		       POST_HORIZONTAL_SCALEDOWN_EN(hdisplay != hsize) |
		       POST_VERTICAL_SCALEDOWN_EN(vdisplay != vsize));
	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		u16 vact_st_f1 = vtotal + vact_st + 1;
		u16 vact_end_f1 = vact_st_f1 + vsize;

		val = vact_st_f1 << 16 | vact_end_f1;
		VOP_MODULE_SET(vop2, vp, vpost_st_end_f1, val);
	}

	/*
	 * BCSH[R2Y] -> POST Linebuffer[post scale] -> the background R2Y will be deal by post_dsp_out_r2y
	 *
	 * POST Linebuffer[post scale] -> ACM[R2Y] -> the background R2Y will be deal by ACM[R2Y]
	 */
	if (vp_data->feature & VOP_FEATURE_POST_ACM)
		VOP_MODULE_SET(vop2, vp, post_dsp_out_r2y, vcstate->yuv_overlay);
	else
		VOP_MODULE_SET(vop2, vp, post_dsp_out_r2y, is_yuv_output(vcstate->bus_format));
}

/*
 * if adjusted mode update, return true, else return false
 */
static bool vop2_crtc_mode_update(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	u16 hsync_len = adjusted_mode->crtc_hsync_end -
				adjusted_mode->crtc_hsync_start;
	u16 hdisplay = adjusted_mode->crtc_hdisplay;
	u16 htotal = adjusted_mode->crtc_htotal;
	u16 hact_st = adjusted_mode->crtc_htotal -
				adjusted_mode->crtc_hsync_start;
	u16 hact_end = hact_st + hdisplay;
	u16 vdisplay = adjusted_mode->crtc_vdisplay;
	u16 vtotal = adjusted_mode->crtc_vtotal;
	u16 vsync_len = adjusted_mode->crtc_vsync_end -
				adjusted_mode->crtc_vsync_start;
	u16 vact_st = adjusted_mode->crtc_vtotal -
				adjusted_mode->crtc_vsync_start;
	u16 vact_end = vact_st + vdisplay;
	u32 htotal_sync = htotal << 16 | hsync_len;
	u32 hactive_st_end = hact_st << 16 | hact_end;
	u32 vactive_st_end = vact_st << 16 | vact_end;
	u32 crtc_clock = adjusted_mode->crtc_clock * 100;

	if (htotal_sync != VOP_MODULE_GET(vop2, vp, htotal_pw) ||
	    hactive_st_end != VOP_MODULE_GET(vop2, vp, hact_st_end) ||
	    vtotal != VOP_MODULE_GET(vop2, vp, dsp_vtotal) ||
	    vsync_len != VOP_MODULE_GET(vop2, vp, dsp_vs_end) ||
	    vactive_st_end != VOP_MODULE_GET(vop2, vp, vact_st_end) ||
	    crtc_clock != clk_get_rate(vp->dclk))
		return true;

	return false;
}

static int vop2_cru_set_rate(struct vop2_clk *if_pixclk, struct vop2_clk *if_dclk)
{
	int ret = 0;

	if (if_pixclk) {
		ret =  clk_set_rate(if_pixclk->hw.clk, if_pixclk->rate);
		if (ret < 0) {
			DRM_DEV_ERROR(if_pixclk->vop2->dev, "set %s to %ld failed: %d\n",
				      clk_hw_get_name(&if_pixclk->hw), if_pixclk->rate, ret);
			return ret;
		}
	}

	if (if_dclk) {
		ret = clk_set_rate(if_dclk->hw.clk, if_dclk->rate);
		if (ret < 0)
			DRM_DEV_ERROR(if_dclk->vop2->dev, "set %s to %ld failed %d\n",
				      clk_hw_get_name(&if_dclk->hw), if_dclk->rate, ret);
	}

	return ret;
}

static int vop2_set_dsc_clk(struct drm_crtc *crtc, u8 dsc_id)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_dsc_data *dsc_data = &vop2_data->dsc[dsc_id];
	struct vop2_clk *dsc_txp_clk, *dsc_pxl_clk, *dsc_cds_clk, *dsc_txp_clk_parent;
	char clk_name[32];
	int ret = 0;

	/* set clk parent */
	snprintf(clk_name, sizeof(clk_name), "dclk%d", vp->id);
	dsc_txp_clk = vop2_clk_get(vop2, dsc_data->dsc_txp_clk_src_name);
	dsc_txp_clk_parent = vop2_clk_get(vop2, clk_name);
	if (!dsc_txp_clk || !dsc_txp_clk_parent) {
		DRM_DEV_ERROR(vop2->dev, "failed to get dsc clk\n");
		return -ENODEV;
	}
	ret = clk_set_parent(dsc_txp_clk->hw.clk, dsc_txp_clk_parent->hw.clk);
	if (ret < 0) {
		DRM_DEV_ERROR(vop2->dev, "failed to set parent(%s) for %s: %d\n",
			      __clk_get_name(dsc_txp_clk_parent->hw.clk),
			      __clk_get_name(dsc_txp_clk->hw.clk), ret);
		return ret;
	}

	/* set dsc txp clk rate */
	clk_set_rate(dsc_txp_clk->hw.clk, vcstate->dsc_txp_clk_rate);

	/* set dsc pxl clk rate */
	dsc_pxl_clk = vop2_clk_get(vop2, dsc_data->dsc_pxl_clk_name);
	if (!dsc_pxl_clk) {
		DRM_DEV_ERROR(vop2->dev, "failed to get dsc_pxl_clk\n");
		return -ENODEV;
	}
	clk_set_rate(dsc_pxl_clk->hw.clk, vcstate->dsc_pxl_clk_rate);

	/* set dsc cds clk rate */
	dsc_cds_clk = vop2_clk_get(vop2, dsc_data->dsc_cds_clk_name);
	if (!dsc_cds_clk) {
		DRM_DEV_ERROR(vop2->dev, "failed to get dsc_cds_clk\n");
		return -ENODEV;
	}
	clk_set_rate(dsc_cds_clk->hw.clk, vcstate->dsc_cds_clk_rate);

	return 0;
}

static int vop2_calc_if_clk(struct drm_crtc *crtc, const struct vop2_connector_if_data *if_data,
			    struct vop2_clk *if_pixclk, struct vop2_clk *if_dclk, int conn_id)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	u64 v_pixclk = adjusted_mode->crtc_clock * 1000LL; /* video timing pixclk */
	unsigned long dclk_core_rate, dclk_out_rate = 0;
	/*conn_dclk = conn_pixclk or conn_dclk = conn_pixclk / 2 */
	u64 hdmi_edp_pixclk, hdmi_edp_dclk, mipi_pixclk;
	char dclk_core_div_shift = 2;
	char K = 1;
	char clk_name[32];
	struct vop2_clk *dclk_core, *dclk_out, *dclk;
	int ret;
	bool dsc_txp_clk_is_biggest = false;
	u8 dsc_id = conn_id & (VOP_OUTPUT_IF_MIPI0 | VOP_OUTPUT_IF_HDMI0) ? 0 : 1;

	dclk_core_div_shift = if_data->post_proc_div_shift;
	dclk_core_rate = v_pixclk >> dclk_core_div_shift;

	if (!if_dclk && (output_if_is_hdmi(conn_id) || output_if_is_edp(conn_id)))
		return -EINVAL;
	if ((vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE) &&
	    (vcstate->output_mode == ROCKCHIP_OUT_MODE_YUV420)) {
		DRM_DEV_ERROR(vop2->dev, "Dual channel and YUV420 can't work together\n");
		return -EINVAL;
	}

	if ((vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE) ||
	    (vcstate->output_mode == ROCKCHIP_OUT_MODE_YUV420))
		K = 2;

	if (output_if_is_hdmi(conn_id)) {
		if (vcstate->dsc_enable) {
			hdmi_edp_pixclk = vcstate->dsc_cds_clk_rate << 1;
			hdmi_edp_dclk = vcstate->dsc_cds_clk_rate;
		} else {
			hdmi_edp_pixclk = (dclk_core_rate << 1) / K;
			hdmi_edp_dclk = dclk_core_rate / K;
		}

		if_pixclk->rate = hdmi_edp_pixclk;
		if_dclk->rate = hdmi_edp_dclk;
	} else if (output_if_is_edp(conn_id)) {
		hdmi_edp_pixclk = v_pixclk;
		do_div(hdmi_edp_pixclk, K);
		hdmi_edp_dclk = hdmi_edp_pixclk;

		if_pixclk->rate = hdmi_edp_pixclk;
		if_dclk->rate = hdmi_edp_dclk;
	} else if (output_if_is_dp(conn_id)) {
		dclk_out_rate = v_pixclk >> 2;
		dclk_out_rate = dclk_out_rate / K;
		if_pixclk->rate = dclk_out_rate;
	} else if (output_if_is_mipi(conn_id)) {
		if (vcstate->dsc_enable)
			/* dsc output is 96bit, dsi input is 192 bit */
			mipi_pixclk = vcstate->dsc_cds_clk_rate >> 1;
		else
			mipi_pixclk = dclk_core_rate / K;

		dclk_out_rate = dclk_core_rate / K;
		if_pixclk->rate = mipi_pixclk;
	} else if (output_if_is_dpi(conn_id)) {
		if_pixclk->rate = v_pixclk;
	}

	/*
	 * RGB/eDP/HDMI: if_pixclk >= dclk_core
	 * DP: dp_pixclk = dclk_out <= dclk_core
	 * DSI: mipi_pixclk <= dclk_out <= dclk_core
	 *
	 */
	snprintf(clk_name, sizeof(clk_name), "dclk_core%d", vp->id);
	dclk_core = vop2_clk_get(vop2, clk_name);

	snprintf(clk_name, sizeof(clk_name), "dclk_out%d", vp->id);
	dclk_out = vop2_clk_get(vop2, clk_name);

	/*
	 * HDMI use 1:1 dclk for rgb/yuv444, 1:2 for yuv420 when
	 * pixclk <= 600
	 * We want use HDMI PHY clk as dclk source for DP/HDMI.
	 * The max freq of HDMI PHY CLK is 600 MHZ.
	 * When used for HDMI, the input freq and v_pixclk must
	 * keep 1:1 for rgb/yuv444, 1:2 for yuv420
	 */
	if (output_if_is_hdmi(conn_id) || output_if_is_dp(conn_id) || output_if_is_mipi(conn_id)) {
		snprintf(clk_name, sizeof(clk_name), "dclk%d", vp->id);
		dclk = vop2_clk_get(vop2, clk_name);
		if (v_pixclk <= (VOP2_MAX_DCLK_RATE * 1000)) {
			if (vcstate->output_mode == ROCKCHIP_OUT_MODE_YUV420 ||
			    (vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE))
				v_pixclk = v_pixclk >> 1;
		} else {
			v_pixclk = v_pixclk >> 2;
		}
		clk_set_rate(dclk->hw.clk, v_pixclk);
	}

	if (vcstate->dsc_enable) {
		if ((vcstate->dsc_txp_clk_rate >= dclk_core_rate) &&
		    (vcstate->dsc_txp_clk_rate >= if_pixclk->rate)) {
			dsc_txp_clk_is_biggest = true;
			if (vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE) {
				vop2_set_dsc_clk(crtc, 0);
				vop2_set_dsc_clk(crtc, 1);
			} else {
				vop2_set_dsc_clk(crtc, dsc_id);
			}
		}
	}

	if (dclk_core_rate > if_pixclk->rate) {
		clk_set_rate(dclk_core->hw.clk, dclk_core_rate);
		if (output_if_is_mipi(conn_id))
			clk_set_rate(dclk_out->hw.clk, dclk_out_rate);
		ret = vop2_cru_set_rate(if_pixclk, if_dclk);
	} else {
		if (output_if_is_mipi(conn_id))
			clk_set_rate(dclk_out->hw.clk, dclk_out_rate);
		ret = vop2_cru_set_rate(if_pixclk, if_dclk);
		clk_set_rate(dclk_core->hw.clk, dclk_core_rate);
	}

	if (!dsc_txp_clk_is_biggest && vcstate->dsc_enable) {
		if (vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE) {
			vop2_set_dsc_clk(crtc, 0);
			vop2_set_dsc_clk(crtc, 1);
		} else {
			vop2_set_dsc_clk(crtc, dsc_id);
		}
	}

	return ret;
}

static int vop2_calc_dsc_clk(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2 *vop2 = vp->vop2;
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	u64 v_pixclk = adjusted_mode->crtc_clock * 1000LL; /* video timing pixclk */
	u8 k = 1;

	if (!vop2->data->nr_dscs) {
		DRM_WARN("Unsupported DSC\n");

		return 0;
	}

	if (vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE)
		k = 2;

	vcstate->dsc_txp_clk_rate = v_pixclk;
	do_div(vcstate->dsc_txp_clk_rate, (vcstate->dsc_pixel_num * k));

	vcstate->dsc_pxl_clk_rate = v_pixclk;
	do_div(vcstate->dsc_pxl_clk_rate, (vcstate->dsc_slice_num * k));

	/* dsc_cds = crtc_clock / (cds_dat_width / bits_per_pixel)
	 * cds_dat_width = 96;
	 * bits_per_pixel = [8-12];
	 * As cds clk is div from txp clk and only support 1/2/4 div,
	 * so when txp_clk is equal to v_pixclk, we set dsc_cds = crtc_clock / 4,
	 * otherwise dsc_cds = crtc_clock / 8;
	 */
	vcstate->dsc_cds_clk_rate = v_pixclk / (vcstate->dsc_txp_clk_rate == v_pixclk ? 4 : 8);

	return 0;
}

static int vop2_calc_cru_cfg(struct drm_crtc *crtc, int conn_id,
			     struct vop2_clk **if_pixclk, struct vop2_clk **if_dclk)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_connector_if_data *if_data;
	struct vop2_clk *if_clk_src, *if_clk_parent;
	char clk_name[32];
	int ret;

	if (vop2->version != VOP_VERSION_RK3588)
		return 0;

	if_data = vop2_find_connector_if_data(vop2, conn_id);
	if_clk_src = vop2_clk_get(vop2, if_data->clk_src_name);
	snprintf(clk_name, sizeof(clk_name), "%s%d", if_data->clk_parent_name, vp->id);
	if_clk_parent = vop2_clk_get(vop2, clk_name);
	*if_pixclk = vop2_clk_get(vop2, if_data->pixclk_name);
	*if_dclk = vop2_clk_get(vop2, if_data->dclk_name);
	if (!(*if_pixclk) || !if_clk_parent) {
		DRM_DEV_ERROR(vop2->dev, "failed to get connector interface clk\n");
		return -ENODEV;
	}

	ret = clk_set_parent(if_clk_src->hw.clk, if_clk_parent->hw.clk);
	if (ret < 0) {
		DRM_DEV_ERROR(vop2->dev, "failed to set parent(%s) for %s: %d\n",
			      __clk_get_name(if_clk_parent->hw.clk),
			      __clk_get_name(if_clk_src->hw.clk), ret);
		return ret;
	}

	/* HDMI and eDP use independent if_pixclk and if_dclk, and others if_pixclk = if_dclk */
	if (output_if_is_hdmi(conn_id) || output_if_is_edp(conn_id))
		ret = vop2_calc_if_clk(crtc, if_data, *if_pixclk, *if_dclk, conn_id);
	else
		ret = vop2_calc_if_clk(crtc, if_data, *if_pixclk, NULL, conn_id);

	return ret;
}

static void vop2_crtc_load_pps(struct drm_crtc *crtc, u8 dsc_id)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);

	struct drm_dsc_picture_parameter_set *pps = &vcstate->pps;
	struct drm_dsc_picture_parameter_set config_pps;
	int i = 0;
	u32 *pps_val = (u32 *)&config_pps;
	u32 offset;
	struct vop2_dsc *dsc;

	dsc = &vop2->dscs[dsc_id];
	offset = dsc->regs->dsc_pps0_3.offset;

	memcpy(&config_pps, pps, sizeof(config_pps));

	if ((config_pps.pps_3 & 0xf) > dsc->max_linebuf_depth) {
		config_pps.pps_3 &= 0xf0;
		config_pps.pps_3 |= dsc->max_linebuf_depth;
		DRM_WARN("DSC%d max_linebuf_depth is: %d, current set value is: %d\n",
			 dsc_id, dsc->max_linebuf_depth, config_pps.pps_3 & 0xf);
	}

	for (i = 0; i < DSC_NUM_BUF_RANGES; i++) {
		config_pps.rc_range_parameters[i] =
			(pps->rc_range_parameters[i] >> 3 & 0x1f) |
			((pps->rc_range_parameters[i] >> 14 & 0x3) << 5) |
			((pps->rc_range_parameters[i] >> 0 & 0x7) << 7) |
			((pps->rc_range_parameters[i] >> 8 & 0x3f) << 10);
	}

	for (i = 0; i < ROCKCHIP_DSC_PPS_SIZE_BYTE / 4; i++)
		vop2_writel(vop2, offset + i * 4, *pps_val++);
}

static void vop2_crtc_enable_dsc(struct drm_crtc *crtc, struct drm_crtc_state *old_state, u8 dsc_id)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	struct rockchip_dsc_sink_cap *dsc_sink_cap = &vcstate->dsc_sink_cap;
	u16 hsync_len = adjusted_mode->crtc_hsync_end - adjusted_mode->crtc_hsync_start;
	u16 hdisplay = adjusted_mode->crtc_hdisplay;
	u16 htotal = adjusted_mode->crtc_htotal;
	u16 hact_st = adjusted_mode->crtc_htotal - adjusted_mode->crtc_hsync_start;
	u16 vdisplay = adjusted_mode->crtc_vdisplay;
	u16 vtotal = adjusted_mode->crtc_vtotal;
	u16 vsync_len = adjusted_mode->crtc_vsync_end - adjusted_mode->crtc_vsync_start;
	u16 vact_st = adjusted_mode->crtc_vtotal - adjusted_mode->crtc_vsync_start;
	u16 vact_end = vact_st + vdisplay;
	u8 dsc_interface_mode = 0;
	struct vop2_dsc *dsc;
	struct vop2_clk *dsc_cds_clk, *dsc_pxl_clk, *dsc_txp_clk;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_dsc_data *dsc_data = &vop2_data->dsc[dsc_id];
	bool mipi_ds_mode = false;
	uint32_t *reg_base = vop2->regs;
	u32 offset = 0;

	if (!vop2->data->nr_dscs) {
		DRM_WARN("Unsupported DSC\n");

		return;
	}

	if (vcstate->dsc_slice_num > dsc_data->max_slice_num)
		DRM_ERROR("DSC%d supported max slice is: %d, current is: %d\n",
			  dsc_data->id, dsc_data->max_slice_num, vcstate->dsc_slice_num);

	dsc = &vop2->dscs[dsc_id];
	if (dsc->pd) {
		dsc->pd->vp_mask = BIT(vp->id);
		vop2_power_domain_get(dsc->pd);
	}

	VOP_MODULE_SET(vop2, dsc, scan_timing_para_imd_en, 1);
	VOP_MODULE_SET(vop2, dsc, dsc_port_sel, vp->id);
	if (vcstate->output_if & (VOP_OUTPUT_IF_HDMI0 | VOP_OUTPUT_IF_HDMI1)) {
		dsc_interface_mode = VOP_DSC_IF_HDMI;
	} else {
		mipi_ds_mode = !!(vcstate->output_flags & ROCKCHIP_OUTPUT_MIPI_DS_MODE);
		if (mipi_ds_mode)
			dsc_interface_mode = VOP_DSC_IF_MIPI_DS_MODE;
		else
			dsc_interface_mode = VOP_DSC_IF_MIPI_VIDEO_MODE;
	}

	if (vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE)
		VOP_MODULE_SET(vop2, dsc, dsc_man_mode, 0);
	else
		VOP_MODULE_SET(vop2, dsc, dsc_man_mode, 1);
	dsc_cds_clk = vop2_clk_get(vop2, dsc_data->dsc_cds_clk_name);
	dsc_pxl_clk = vop2_clk_get(vop2, dsc_data->dsc_pxl_clk_name);
	dsc_txp_clk = vop2_clk_get(vop2, dsc_data->dsc_txp_clk_name);

	VOP_MODULE_SET(vop2, dsc, dsc_interface_mode, dsc_interface_mode);
	VOP_MODULE_SET(vop2, dsc, dsc_pixel_num, vcstate->dsc_pixel_num >> 1);
	VOP_MODULE_SET(vop2, dsc, dsc_txp_clk_div, dsc_txp_clk->div_val);
	VOP_MODULE_SET(vop2, dsc, dsc_pxl_clk_div, dsc_pxl_clk->div_val);
	VOP_MODULE_SET(vop2, dsc, dsc_cds_clk_div, dsc_cds_clk->div_val);
	VOP_MODULE_SET(vop2, dsc, dsc_scan_en, !mipi_ds_mode);
	VOP_MODULE_SET(vop2, dsc, dsc_halt_en, mipi_ds_mode);

	if (!mipi_ds_mode) {
		u16 dsc_hsync, dsc_htotal, dsc_hact_st, dsc_hact_end;
		u32 target_bpp = dsc_sink_cap->target_bits_per_pixel_x16;
		u64 dsc_cds_rate = vcstate->dsc_cds_clk_rate;
		u32 v_pixclk_mhz = adjusted_mode->crtc_clock / 1000; /* video timing pixclk */
		u32 dly_num, dsc_cds_rate_mhz, val = 0;
		struct vop2_clk *dclk_core;
		char clk_name[32];
		int k = 1;

		if (vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE)
			k = 2;

		snprintf(clk_name, sizeof(clk_name), "dclk_core%d", vp->id);
		dclk_core = vop2_clk_get(vop2, clk_name);

		if (target_bpp >> 4 < dsc->min_bits_per_pixel)
			DRM_ERROR("Unsupported bpp less than: %d\n", dsc->min_bits_per_pixel);

		/*
		 * dly_num = delay_line_num * T(one-line) / T (dsc_cds)
		 * T (one-line) = 1/v_pixclk_mhz * htotal = htotal/v_pixclk_mhz
		 * T (dsc_cds) = 1 / dsc_cds_rate_mhz
		 *
		 * HDMI:
		 * delay_line_num: according the pps initial_xmit_delay to adjust vop dsc delay
		 *                 delay_line_num = 4 - BPP / 8
		 *                                = (64 - target_bpp / 8) / 16
		 * dly_num = htotal * dsc_cds_rate_mhz / v_pixclk_mhz * (64 - target_bpp / 8) / 16;
		 *
		 * MIPI DSI[4320 and 9216 is buffer size for DSC]:
		 * DSC0:delay_line_num = 4320 * 8 / slince_num / chunk_size;
		 *	delay_line_num = delay_line_num > 5 ? 5 : delay_line_num;
		 * DSC1:delay_line_num = 9216 * 2 / slince_num / chunk_size;
		 *	delay_line_num = delay_line_num > 5 ? 5 : delay_line_num;
		 * dly_num = htotal * dsc_cds_rate_mhz / v_pixclk_mhz * delay_line_num
		 */
		do_div(dsc_cds_rate, 1000000); /* hz to Mhz */
		dsc_cds_rate_mhz = dsc_cds_rate;
		dsc_hsync = hsync_len / 2;
		if (dsc_interface_mode == VOP_DSC_IF_HDMI) {
			dly_num = htotal * dsc_cds_rate_mhz / v_pixclk_mhz * (64 - target_bpp / 8) / 16;
		} else {
			int dsc_buf_size  = dsc->id == 0 ? 4320 * 8 : 9216 * 2;
			int delay_line_num = dsc_buf_size / vcstate->dsc_slice_num / be16_to_cpu(vcstate->pps.chunk_size);

			delay_line_num = delay_line_num > 5 ? 5 : delay_line_num;
			dly_num = htotal * dsc_cds_rate_mhz / v_pixclk_mhz * delay_line_num;

			/* The dsc mipi video mode dsc_hsync minimum size is 8 pixels */
			if (dsc_hsync < 8)
				dsc_hsync = 8;
		}
		VOP_MODULE_SET(vop2, dsc, dsc_init_dly_mode, 0);
		VOP_MODULE_SET(vop2, dsc, dsc_init_dly_num, dly_num);
		/*
		 * htotal / dclk_core = dsc_htotal /cds_clk
		 *
		 * dclk_core = DCLK / (1 << dclk_core->div_val)
		 * cds_clk = txp_clk / (1 << dsc_cds_clk->div_val)
		 * txp_clk = DCLK / (1 << dsc_txp_clk->div_val)
		 *
		 * dsc_htotal = htotal * (1 << dclk_core->div_val) /
				((1 << dsc_txp_clk->div_val) * (1 << dsc_cds_clk->div_val))
		*/
		dsc_htotal = htotal * (1 << dclk_core->div_val) /
				((1 << dsc_txp_clk->div_val) * (1 << dsc_cds_clk->div_val));
		val = dsc_htotal << 16 | dsc_hsync;
		VOP_MODULE_SET(vop2, dsc, dsc_htotal_pw, val);

		dsc_hact_st = hact_st / 2;
		dsc_hact_end = (hdisplay / k * target_bpp >> 4) / 24 + dsc_hact_st;
		val = dsc_hact_end << 16 | dsc_hact_st;
		VOP_MODULE_SET(vop2, dsc, dsc_hact_st_end, val);

		VOP_MODULE_SET(vop2, dsc, dsc_vtotal, vtotal);
		VOP_MODULE_SET(vop2, dsc, dsc_vs_end, vsync_len);
		VOP_MODULE_SET(vop2, dsc, dsc_vact_st_end, vact_end << 16 | vact_st);
	}

	VOP_MODULE_SET(vop2, dsc, rst_deassert, 1);
	udelay(10);
	/* read current dsc core register and backup to regsbak */
	offset = dsc->regs->dsc_en.offset;
	vop2->regsbak[offset >> 2] = reg_base[offset >> 2];

	VOP_MODULE_SET(vop2, dsc, dsc_en, 1);
	vop2_crtc_load_pps(crtc, dsc_id);

	VOP_MODULE_SET(vop2, dsc, dsc_rbit, 1);
	VOP_MODULE_SET(vop2, dsc, dsc_rbyt, 0);
	VOP_MODULE_SET(vop2, dsc, dsc_flal, 1);
	VOP_MODULE_SET(vop2, dsc, dsc_mer, 1);
	VOP_MODULE_SET(vop2, dsc, dsc_epb, 0);
	VOP_MODULE_SET(vop2, dsc, dsc_epl, 1);
	VOP_MODULE_SET(vop2, dsc, dsc_nslc, ilog2(vcstate->dsc_slice_num));
	VOP_MODULE_SET(vop2, dsc, dsc_sbo, 1);
	VOP_MODULE_SET(vop2, dsc, dsc_ifep, dsc_sink_cap->version_minor == 2 ? 1 : 0);
	VOP_MODULE_SET(vop2, dsc, dsc_pps_upd, 1);

	DRM_DEV_INFO(vop2->dev, "DSC%d: txp:%lld div:%d, pxl:%lld div:%d, dsc:%lld div:%d\n",
		     dsc->id,
		     vcstate->dsc_txp_clk_rate, dsc_txp_clk->div_val,
		     vcstate->dsc_pxl_clk_rate, dsc_pxl_clk->div_val,
		     vcstate->dsc_cds_clk_rate, dsc_cds_clk->div_val);

	dsc->attach_vp_id = vp->id;
	dsc->enabled = true;
}

static void vop2_setup_dual_channel_if(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2 *vop2 = vp->vop2;

	VOP_MODULE_SET(vop2, vp, dual_channel_en, 1);
	if (vcstate->output_flags & ROCKCHIP_OUTPUT_DATA_SWAP)
		VOP_MODULE_SET(vop2, vp, dual_channel_swap, 1);

	if (vcstate->output_if & VOP_OUTPUT_IF_DP1)
		VOP_CTRL_SET(vop2, dp_dual_en, 1);
	else if (vcstate->output_if & VOP_OUTPUT_IF_eDP1)
		VOP_CTRL_SET(vop2, edp_dual_en, 1);
	else if (vcstate->output_if & VOP_OUTPUT_IF_HDMI1)
		VOP_CTRL_SET(vop2, hdmi_dual_en, 1);
	else if (vcstate->output_if & VOP_OUTPUT_IF_MIPI1)
		VOP_CTRL_SET(vop2, mipi_dual_en, 1);
}

/*
 * MIPI port mux on rk3588:
 * 0: Video Port2
 * 1: Video Port3
 * 3: Video Port 1(MIPI1 only)
 */
static int vop2_get_mipi_port_mux(struct vop2 *vop2, int vp_id)
{
	if (vop2->version == VOP_VERSION_RK3588) {
		if (vp_id == 1)
			return 3;
		else if (vp_id == 3)
			return 1;
		else
			return 0;
	} else {
		return vp_id;
	}
}

static u32 vop2_get_hdmi_pol(struct vop2 *vop2, u32 flags)
{
	u32 val;

	if (vop2->version == VOP_VERSION_RK3588) {
		val = (flags & DRM_MODE_FLAG_NHSYNC) ? BIT(HSYNC_POSITIVE) : 0;
		val |= (flags & DRM_MODE_FLAG_NVSYNC) ? BIT(VSYNC_POSITIVE) : 0;
	} else {
		val = (flags & DRM_MODE_FLAG_NHSYNC) ? 0 : BIT(HSYNC_POSITIVE);
		val |= (flags & DRM_MODE_FLAG_NVSYNC) ? 0 : BIT(VSYNC_POSITIVE);
	}

	return val;
}

static void vop2_post_color_swap(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	u32 output_if = vcstate->output_if;
	u32 data_swap = 0;

	if (vop2_output_uv_swap(vcstate->bus_format, vcstate->output_mode) ||
	    vop3_output_rb_swap(vcstate->bus_format, vcstate->output_mode))
		data_swap = DSP_RB_SWAP;

	if (vop2->version == VOP_VERSION_RK3588 &&
	    (output_if_is_hdmi(output_if) || output_if_is_dp(output_if)) &&
	    (vcstate->bus_format == MEDIA_BUS_FMT_YUV8_1X24 ||
	     vcstate->bus_format == MEDIA_BUS_FMT_YUV10_1X30))
		data_swap |= DSP_RG_SWAP;

	VOP_MODULE_SET(vop2, vp, dsp_data_swap, data_swap);
}

/*
 * For vop3 video port0, if hdr_vivid is not enable, the pipe delay time as follow:
 * win_dly + config_win_dly + layer_mix_dly + sdr2hdr_dly + * hdr_mix_dly = config_bg_dly
 *
 * if hdr_vivid is enable, the hdr layer's pipe delay time as follow:
 * win_dly + config_win_dly +hdrvivid_dly + hdr_mix_dly = config_bg_dly
 *
 * If hdrvivid and sdr2hdr bot enable, the time arrivr hdr_mix should be the same:
 * win_dly + config_win_dly0 + hdrvivid_dly = win_dly + config_win_dly1 + laer_mix_dly +
 * sdr2hdr_dly
 *
 * For vop3 video port1, the pipe delay time as follow:
 * win_dly + config_win_dly + layer_mix_dly = config_bg_dly
 *
 * Here, win_dly, layer_mix_dly, sdr2hdr_dly, hdr_mix_dly, hdrvivid_dly is the hardware
 * delay cycles. Config_win_dly and config_bg_dly is the register value that we can config.
 * Different hdr vivid mode have different hdrvivid_dly. For sdr2hdr_dly, only sde2hdr
 * enable, it will delay, otherwise, the sdr2hdr_dly is 0.
 *
 * For default, the config_win_dly will be 0, it just user to make the pipe to arrive
 * hdr_mix at the same time.
 */
static void vop3_setup_pipe_dly(struct vop2_video_port *vp, const struct vop2_zpos *vop2_zpos)
{
	struct vop2 *vop2 = vp->vop2;
	struct drm_crtc *crtc = &vp->rockchip_crtc.crtc;
	const struct vop2_zpos *zpos;
	struct drm_plane *plane;
	struct vop2_plane_state *vpstate;
	struct vop2_win *win;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	u16 hsync_len = adjusted_mode->crtc_hsync_end - adjusted_mode->crtc_hsync_start;
	u16 hdisplay = adjusted_mode->crtc_hdisplay;
	int bg_dly = 0x0;
	int dly = 0x0;
	int hdr_win_dly;
	int sdr_win_dly;
	int sdr2hdr_dly;
	int pre_scan_dly;
	int i;

	/**
	 * config bg dly, select the max delay num of hdrvivid and sdr2hdr module
	 * as the increase value of bg delay num. If hdrvivid and sdr2hdr is not
	 * work, the default bg_dly is 0x10. and the default win delay num is 0.
	 */
	if ((vp->hdr_en || vp->sdr2hdr_en) &&
	    (vp->hdrvivid_mode >= 0 && vp->hdrvivid_mode <= SDR2HLG)) {
		/* set sdr2hdr_dly to 0 if sdr2hdr is disable */
		sdr2hdr_dly = vp->sdr2hdr_en ? vp_data->sdr2hdr_dly : 0;

		/* set the max delay pipe's config_win_dly as 0 */
		if (vp_data->hdrvivid_dly[vp->hdrvivid_mode] >=
		    sdr2hdr_dly + vp_data->layer_mix_dly) {
			bg_dly = vp_data->win_dly + vp_data->hdrvivid_dly[vp->hdrvivid_mode] +
				 vp_data->hdr_mix_dly;
			hdr_win_dly = 0;
			sdr_win_dly = vp_data->hdrvivid_dly[vp->hdrvivid_mode] -
				      vp_data->layer_mix_dly - sdr2hdr_dly;
		} else {
			bg_dly = vp_data->win_dly + vp_data->layer_mix_dly + sdr2hdr_dly +
				 vp_data->hdr_mix_dly;
			hdr_win_dly = sdr2hdr_dly + vp_data->layer_mix_dly -
				      vp_data->hdrvivid_dly[vp->hdrvivid_mode];
			sdr_win_dly = 0;
		}
	} else {
		bg_dly = vp_data->win_dly + vp_data->layer_mix_dly + vp_data->hdr_mix_dly;
		sdr_win_dly = 0;
	}

	pre_scan_dly = bg_dly + (hdisplay >> 1) - 1;
	pre_scan_dly = (pre_scan_dly << 16) | hsync_len;
	VOP_MODULE_SET(vop2, vp, bg_dly, bg_dly);
	VOP_MODULE_SET(vop2, vp, pre_scan_htiming, pre_scan_dly);

	/**
	 * config win dly
	 */
	if (!vop2_zpos)
		return;

	for (i = 0; i < vp->nr_layers; i++) {
		zpos = &vop2_zpos[i];
		win = vop2_find_win_by_phys_id(vop2, zpos->win_phys_id);
		plane = &win->base;
		vpstate = to_vop2_plane_state(plane->state);

		if ((vp->hdr_en || vp->sdr2hdr_en) &&
		    (vp->hdrvivid_mode >= 0 && vp->hdrvivid_mode <= SDR2HLG)) {
			dly = vpstate->hdr_in ? hdr_win_dly : sdr_win_dly;
		}
		if (vop2_cluster_window(win))
			dly |= dly << 8;

		VOP_CTRL_SET(vop2, win_dly[win->phys_id], dly);
	}
}

static int vop2_get_vrefresh(struct vop2_video_port *vp, const struct drm_display_mode *mode)
{
	if (vp->mcu_timing.mcu_pix_total)
		return drm_mode_vrefresh(mode) / vp->mcu_timing.mcu_pix_total;
	else
		return drm_mode_vrefresh(mode);
}

static void vop2_crtc_atomic_enable(struct drm_crtc *crtc, struct drm_crtc_state *old_state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2_video_port *splice_vp;
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	struct rockchip_dsc_sink_cap *dsc_sink_cap = &vcstate->dsc_sink_cap;
	u16 hsync_len = adjusted_mode->crtc_hsync_end - adjusted_mode->crtc_hsync_start;
	u16 hdisplay = adjusted_mode->crtc_hdisplay;
	u16 htotal = adjusted_mode->crtc_htotal;
	u16 hact_st = adjusted_mode->crtc_htotal - adjusted_mode->crtc_hsync_start;
	u16 hact_end = hact_st + hdisplay;
	u16 vdisplay = adjusted_mode->crtc_vdisplay;
	u16 vtotal = adjusted_mode->crtc_vtotal;
	u16 vsync_len = adjusted_mode->crtc_vsync_end - adjusted_mode->crtc_vsync_start;
	u16 vact_st = adjusted_mode->crtc_vtotal - adjusted_mode->crtc_vsync_start;
	u16 vact_end = vact_st + vdisplay;
	bool interlaced = !!(adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE);
	bool dclk_inv, yc_swap = false;
	int act_end;
	uint32_t val;
	char clk_name[32];
	struct vop2_clk *if_pixclk = NULL;
	struct vop2_clk *if_dclk = NULL;
	struct vop2_clk *dclk, *dclk_out, *dclk_core;
	int splice_en = 0;
	int port_mux;
	int ret;

	if (old_state && old_state->self_refresh_active) {
		drm_crtc_vblank_on(crtc);
		if (vop2->aclk_rate_reset)
			clk_set_rate(vop2->aclk, vop2->aclk_rate);
		vop2->aclk_rate_reset = false;

		return;
	}

	vop2->active_vp_mask |= BIT(vp->id);
	vop2_set_system_status(vop2);

	vop2_lock(vop2);
	DRM_DEV_INFO(vop2->dev, "Update mode to %dx%d%s%d, type: %d(if:%x) for vp%d dclk: %d\n",
		     hdisplay, adjusted_mode->vdisplay, interlaced ? "i" : "p",
		     vop2_get_vrefresh(vp, adjusted_mode), vcstate->output_type, vcstate->output_if,
		     vp->id, adjusted_mode->crtc_clock * 1000);

	if (adjusted_mode->hdisplay > VOP2_MAX_VP_OUTPUT_WIDTH) {
		vcstate->splice_mode = true;
		splice_vp = &vop2->vps[vp_data->splice_vp_id];
		splice_vp->splice_mode_right = true;
		splice_vp->left_vp = vp;
		splice_en = 1;
		vop2->active_vp_mask |= BIT(splice_vp->id);
	}

	if (vcstate->dsc_enable) {
		int k = 1;

		if (vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE)
			k = 2;

		vcstate->dsc_id = vcstate->output_if & (VOP_OUTPUT_IF_MIPI0 | VOP_OUTPUT_IF_HDMI0) ? 0 : 1;
		vcstate->dsc_slice_num = hdisplay / dsc_sink_cap->slice_width / k;
		vcstate->dsc_pixel_num = vcstate->dsc_slice_num > 4 ? 4 : vcstate->dsc_slice_num;

		vop2_calc_dsc_clk(crtc);
		DRM_DEV_INFO(vop2->dev, "Enable DSC%d slice:%dx%d, slice num:%d\n",
			     vcstate->dsc_id, dsc_sink_cap->slice_width,
			     dsc_sink_cap->slice_height, vcstate->dsc_slice_num);
	}

	vop2_initial(crtc);
	vcstate->vdisplay = vdisplay;
	vcstate->mode_update = vop2_crtc_mode_update(crtc);
	if (vcstate->mode_update)
		vop2_disable_all_planes_for_crtc(crtc);

	dclk_inv = (vcstate->bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE) ? 1 : 0;
	val = (adjusted_mode->flags & DRM_MODE_FLAG_NHSYNC) ? 0 : BIT(HSYNC_POSITIVE);
	val |= (adjusted_mode->flags & DRM_MODE_FLAG_NVSYNC) ? 0 : BIT(VSYNC_POSITIVE);

	vp->output_if = vcstate->output_if;

	if (vcstate->output_if & VOP_OUTPUT_IF_RGB) {
		ret = vop2_calc_cru_cfg(crtc, VOP_OUTPUT_IF_RGB, &if_pixclk, &if_dclk);
		if (ret < 0)
			goto out;

		VOP_CTRL_SET(vop2, rgb_en, 1);
		VOP_CTRL_SET(vop2, rgb_mux, vp_data->id);
		VOP_CTRL_SET(vop2, rgb_pin_pol, val);
		VOP_GRF_SET(vop2, sys_grf, grf_dclk_inv, dclk_inv);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_BT1120) {
		ret = vop2_calc_cru_cfg(crtc, VOP_OUTPUT_IF_RGB, &if_pixclk, &if_dclk);
		if (ret < 0)
			goto out;

		if (vop2->version == VOP_VERSION_RK3588) {
			VOP_CTRL_SET(vop2, bt1120_en, 3);
		} else {
			VOP_CTRL_SET(vop2, rgb_en, 1);
			VOP_CTRL_SET(vop2, bt1120_en, 1);
		}
		VOP_CTRL_SET(vop2, rgb_mux, vp_data->id);
		VOP_GRF_SET(vop2, sys_grf, grf_bt1120_clk_inv, !dclk_inv);
		VOP_CTRL_SET(vop2, bt1120_dclk_pol, !dclk_inv);
		yc_swap = vop2_output_yc_swap(vcstate->bus_format);
		VOP_CTRL_SET(vop2, bt1120_yc_swap, yc_swap);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_BT656) {
		ret = vop2_calc_cru_cfg(crtc, VOP_OUTPUT_IF_RGB, &if_pixclk, &if_dclk);
		if (ret < 0)
			goto out;

		if (vop2->version == VOP_VERSION_RK3588) {
			VOP_CTRL_SET(vop2, bt656_en, 1);
		} else {
			VOP_CTRL_SET(vop2, rgb_en, 1);
			VOP_CTRL_SET(vop2, bt656_en, 1);
		}
		VOP_CTRL_SET(vop2, rgb_mux, vp_data->id);
		VOP_GRF_SET(vop2, sys_grf, grf_bt656_clk_inv, !dclk_inv);
		VOP_CTRL_SET(vop2, bt656_dclk_pol, !dclk_inv);
		yc_swap = vop2_output_yc_swap(vcstate->bus_format);
		VOP_CTRL_SET(vop2, bt656_yc_swap, yc_swap);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_LVDS0) {
		VOP_CTRL_SET(vop2, lvds0_en, 1);
		VOP_CTRL_SET(vop2, lvds0_mux, vp_data->id);
		VOP_CTRL_SET(vop2, lvds_pin_pol, val);
		VOP_CTRL_SET(vop2, lvds_dclk_pol, dclk_inv);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_LVDS1) {
		VOP_CTRL_SET(vop2, lvds1_en, 1);
		VOP_CTRL_SET(vop2, lvds1_mux, vp_data->id);
		VOP_CTRL_SET(vop2, lvds_pin_pol, val);
		VOP_CTRL_SET(vop2, lvds_dclk_pol, dclk_inv);
	}

	if (vcstate->output_flags & (ROCKCHIP_OUTPUT_DUAL_CHANNEL_ODD_EVEN_MODE |
	    ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE)) {
		VOP_CTRL_SET(vop2, lvds_dual_en, 1);
		if (vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE)
			VOP_CTRL_SET(vop2, lvds_dual_mode, 1);
		if (vcstate->output_flags & ROCKCHIP_OUTPUT_DATA_SWAP)
			VOP_CTRL_SET(vop2, lvds_dual_channel_swap, 1);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_MIPI0) {
		ret = vop2_calc_cru_cfg(crtc, VOP_OUTPUT_IF_MIPI0, &if_pixclk, &if_dclk);
		if (ret < 0)
			goto out;
		if (if_pixclk)
			VOP_CTRL_SET(vop2, mipi0_pixclk_div, if_pixclk->div_val);

		if (vcstate->output_flags & ROCKCHIP_OUTPUT_MIPI_DS_MODE)
			VOP_CTRL_SET(vop2, mipi0_ds_mode, 1);

		port_mux = vop2_get_mipi_port_mux(vop2, vp_data->id);
		VOP_CTRL_SET(vop2, mipi0_en, 1);
		VOP_CTRL_SET(vop2, mipi0_mux, port_mux);
		VOP_CTRL_SET(vop2, mipi_pin_pol, val);
		VOP_CTRL_SET(vop2, mipi_dclk_pol, dclk_inv);
		if (vcstate->hold_mode) {
			VOP_MODULE_SET(vop2, vp, edpi_te_en, !vcstate->soft_te);
			VOP_MODULE_SET(vop2, vp, edpi_wms_hold_en, 1);
		}
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_MIPI1) {
		ret = vop2_calc_cru_cfg(crtc, VOP_OUTPUT_IF_MIPI1, &if_pixclk, &if_dclk);
		if (ret < 0)
			goto out;
		if (if_pixclk)
			VOP_CTRL_SET(vop2, mipi1_pixclk_div, if_pixclk->div_val);

		if (vcstate->output_flags & ROCKCHIP_OUTPUT_MIPI_DS_MODE)
			VOP_CTRL_SET(vop2, mipi1_ds_mode, 1);

		port_mux = vop2_get_mipi_port_mux(vop2, vp_data->id);

		VOP_CTRL_SET(vop2, mipi1_en, 1);
		VOP_CTRL_SET(vop2, mipi1_mux, port_mux);
		VOP_CTRL_SET(vop2, mipi_pin_pol, val);
		VOP_CTRL_SET(vop2, mipi_dclk_pol, dclk_inv);
		if (vcstate->hold_mode) {
			VOP_MODULE_SET(vop2, vp, edpi_te_en, !vcstate->soft_te);
			VOP_MODULE_SET(vop2, vp, edpi_wms_hold_en, 1);
		}
	}

	if (vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE)
		vop2_setup_dual_channel_if(crtc);

	if (vcstate->output_if & VOP_OUTPUT_IF_eDP0) {
		ret = vop2_calc_cru_cfg(crtc, VOP_OUTPUT_IF_eDP0, &if_pixclk, &if_dclk);
		if (ret < 0)
			goto out;
		if (if_pixclk && if_dclk) {
			VOP_CTRL_SET(vop2, edp0_pixclk_div, if_pixclk->div_val);
			VOP_CTRL_SET(vop2, edp0_dclk_div, if_dclk->div_val);
		}

		VOP_CTRL_SET(vop2, edp0_en, 1);
		VOP_CTRL_SET(vop2, edp0_mux, vp_data->id);
		VOP_CTRL_SET(vop2, edp_pin_pol, val);
		VOP_CTRL_SET(vop2, edp_dclk_pol, dclk_inv);
		VOP_GRF_SET(vop2, grf, grf_edp0_en, 1);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_eDP1) {
		ret = vop2_calc_cru_cfg(crtc, VOP_OUTPUT_IF_eDP1, &if_pixclk, &if_dclk);
		if (ret < 0)
			goto out;
		if (if_pixclk && if_dclk) {
			VOP_CTRL_SET(vop2, edp1_pixclk_div, if_pixclk->div_val);
			VOP_CTRL_SET(vop2, edp1_dclk_div, if_dclk->div_val);
		}

		VOP_CTRL_SET(vop2, edp1_en, 1);
		VOP_CTRL_SET(vop2, edp1_mux, vp_data->id);
		VOP_CTRL_SET(vop2, edp_pin_pol, val);
		VOP_CTRL_SET(vop2, edp_dclk_pol, dclk_inv);
		VOP_GRF_SET(vop2, grf, grf_edp1_en, 1);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_DP0) {
		ret = vop2_calc_cru_cfg(crtc, VOP_OUTPUT_IF_DP0, &if_pixclk, &if_dclk);
		if (ret < 0)
			goto out;
		VOP_CTRL_SET(vop2, dp0_en, 1);
		VOP_CTRL_SET(vop2, dp0_mux, vp_data->id);
		VOP_CTRL_SET(vop2, dp0_dclk_pol, 0);
		VOP_CTRL_SET(vop2, dp0_pin_pol, val);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_DP1) {
		ret = vop2_calc_cru_cfg(crtc, VOP_OUTPUT_IF_DP0, &if_pixclk, &if_dclk);
		if (ret < 0)
			goto out;

		VOP_CTRL_SET(vop2, dp1_en, 1);
		VOP_CTRL_SET(vop2, dp1_mux, vp_data->id);
		VOP_CTRL_SET(vop2, dp1_dclk_pol, 0);
		VOP_CTRL_SET(vop2, dp1_pin_pol, val);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_HDMI0) {
		ret = vop2_calc_cru_cfg(crtc, VOP_OUTPUT_IF_HDMI0, &if_pixclk, &if_dclk);
		if (ret < 0)
			goto out;
		if (if_pixclk && if_dclk) {
			VOP_CTRL_SET(vop2, hdmi0_pixclk_div, if_pixclk->div_val);
			VOP_CTRL_SET(vop2, hdmi0_dclk_div, if_dclk->div_val);
		}

		if (vcstate->dsc_enable)
			VOP_GRF_SET(vop2, grf, grf_hdmi0_dsc_en, 1);

		val = vop2_get_hdmi_pol(vop2, adjusted_mode->flags);
		VOP_GRF_SET(vop2, grf, grf_hdmi0_en, 1);
		VOP_GRF_SET(vop2, vo1_grf, grf_hdmi0_pin_pol, val);

		VOP_CTRL_SET(vop2, hdmi0_en, 1);
		VOP_CTRL_SET(vop2, hdmi0_mux, vp_data->id);
		VOP_CTRL_SET(vop2, hdmi_pin_pol, val);
		VOP_CTRL_SET(vop2, hdmi_dclk_pol, 1);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_HDMI1) {
		ret = vop2_calc_cru_cfg(crtc, VOP_OUTPUT_IF_HDMI1, &if_pixclk, &if_dclk);
		if (ret < 0)
			goto out;

		if (if_pixclk && if_dclk) {
			VOP_CTRL_SET(vop2, hdmi1_pixclk_div, if_pixclk->div_val);
			VOP_CTRL_SET(vop2, hdmi1_dclk_div, if_dclk->div_val);
		}

		if (vcstate->dsc_enable)
			VOP_GRF_SET(vop2, grf, grf_hdmi1_dsc_en, 1);

		val = vop2_get_hdmi_pol(vop2, adjusted_mode->flags);
		VOP_GRF_SET(vop2, grf, grf_hdmi1_en, 1);
		VOP_GRF_SET(vop2, vo1_grf, grf_hdmi1_pin_pol, val);

		VOP_CTRL_SET(vop2, hdmi1_en, 1);
		VOP_CTRL_SET(vop2, hdmi1_mux, vp_data->id);
		VOP_CTRL_SET(vop2, hdmi_pin_pol, val);
		VOP_CTRL_SET(vop2, hdmi_dclk_pol, 1);
	}

	VOP_MODULE_SET(vop2, vp, splice_en, splice_en);

	VOP_MODULE_SET(vop2, vp, htotal_pw, (htotal << 16) | hsync_len);
	val = hact_st << 16;
	val |= hact_end;
	VOP_MODULE_SET(vop2, vp, hact_st_end, val);

	val = vact_st << 16;
	val |= vact_end;
	VOP_MODULE_SET(vop2, vp, vact_st_end, val);

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		u16 vact_st_f1 = vtotal + vact_st + 1;
		u16 vact_end_f1 = vact_st_f1 + vdisplay;

		val = vact_st_f1 << 16 | vact_end_f1;
		VOP_MODULE_SET(vop2, vp, vact_st_end_f1, val);

		val = vtotal << 16 | (vtotal + vsync_len);
		VOP_MODULE_SET(vop2, vp, vs_st_end_f1, val);
		VOP_MODULE_SET(vop2, vp, dsp_interlace, 1);
		VOP_MODULE_SET(vop2, vp, dsp_filed_pol, 1);
		VOP_MODULE_SET(vop2, vp, p2i_en, 1);
		vtotal += vtotal + 1;
		act_end = vact_end_f1;
	} else {
		VOP_MODULE_SET(vop2, vp, dsp_interlace, 0);
		VOP_MODULE_SET(vop2, vp, dsp_filed_pol, 0);
		VOP_MODULE_SET(vop2, vp, p2i_en, 0);
		act_end = vact_end;
	}

	if (vp->xmirror_en)
		VOP_MODULE_SET(vop2, vp, dsp_x_mir_en, 1);

	VOP_INTR_SET(vop2, intr, line_flag_num[0], act_end);
	VOP_INTR_SET(vop2, intr, line_flag_num[1], act_end);

	VOP_MODULE_SET(vop2, vp, dsp_vtotal, vtotal);
	VOP_MODULE_SET(vop2, vp, dsp_vs_end, vsync_len);
	/**
	 * when display interface support vrr, config vtotal valid immediately
	 */
	if (vcstate->max_refresh_rate && vcstate->min_refresh_rate)
		VOP_MODULE_SET(vop2, vp, sw_dsp_vtotal_imd, 1);

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLCLK ||
	    vcstate->output_if & VOP_OUTPUT_IF_BT656)
		VOP_MODULE_SET(vop2, vp, core_dclk_div, 1);
	else
		VOP_MODULE_SET(vop2, vp, core_dclk_div, 0);

	if (vcstate->output_mode == ROCKCHIP_OUT_MODE_YUV420) {
		VOP_MODULE_SET(vop2, vp, dclk_div2, 1);
		VOP_MODULE_SET(vop2, vp, dclk_div2_phase_lock, 1);
	} else {
		VOP_MODULE_SET(vop2, vp, dclk_div2, 0);
		VOP_MODULE_SET(vop2, vp, dclk_div2_phase_lock, 0);
	}

	snprintf(clk_name, sizeof(clk_name), "dclk_out%d", vp->id);
	dclk_out = vop2_clk_get(vop2, clk_name);
	snprintf(clk_name, sizeof(clk_name), "dclk_core%d", vp->id);
	dclk_core = vop2_clk_get(vop2, clk_name);
	if (dclk_out && dclk_core) {
		DRM_DEV_INFO(vop2->dev, "%s div: %d %s div: %d\n",
			     __clk_get_name(dclk_out->hw.clk), dclk_out->div_val,
			     __clk_get_name(dclk_core->hw.clk), dclk_core->div_val);
		VOP_MODULE_SET(vop2, vp, dclk_src_sel, 0);
		VOP_MODULE_SET(vop2, vp, dclk_out_div, dclk_out->div_val);
		VOP_MODULE_SET(vop2, vp, dclk_core_div, dclk_core->div_val);
	}

	snprintf(clk_name, sizeof(clk_name), "dclk%d", vp->id);
	dclk = vop2_clk_get(vop2, clk_name);
	if (dclk) {
		/*
		 * use HDMI_PHY_PLL as dclk source under 4K@60 if it is available,
		 * otherwise use system cru as dclk source.
		 */
		ret = vop2_clk_set_parent_extend(vp, vcstate, true);
		if (ret < 0)
			goto out;

		clk_set_rate(vp->dclk, dclk->rate);
		DRM_DEV_INFO(vop2->dev, "set %s to %ld, get %ld\n",
			      __clk_get_name(vp->dclk), dclk->rate, clk_get_rate(vp->dclk));
	} else {
		/*
		 * For RK3528, the path of CVBS output is like:
		 * VOP BT656 ENCODER -> CVBS BT656 DECODER -> CVBS ENCODER -> CVBS VDAC
		 * The vop2 dclk should be four times crtc_clock for CVBS sampling clock needs.
		 */
		if (vop2->version == VOP_VERSION_RK3528 && vcstate->output_if & VOP_OUTPUT_IF_BT656)
			clk_set_rate(vp->dclk, 4 * adjusted_mode->crtc_clock * 1000);
		else
			clk_set_rate(vp->dclk, adjusted_mode->crtc_clock * 1000);
	}

	if (vp_data->feature & VOP_FEATURE_OVERSCAN)
		vop2_post_config(crtc);

	VOP_MODULE_SET(vop2, vp, almost_full_or_en, 1);
	VOP_MODULE_SET(vop2, vp, line_flag_or_en, 1);
	if (vcstate->dsc_enable) {
		if (vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE) {
			vop2_crtc_enable_dsc(crtc, old_state, 0);
			vop2_crtc_enable_dsc(crtc, old_state, 1);
		} else {
			vop2_crtc_enable_dsc(crtc, old_state, vcstate->dsc_id);
		}
	}
	/* For RK3588, the reset value of background is 0xa0080200,
	 * which will enable background and output a grey image. But
	 * the reset value is just valid in first frame and disable
	 * in follow frames. If the panel backlight is valid before
	 * follow frames. The screen may flick a grey image. To avoid
	 * this phenomenon appear, setting black background after
	 * reset vop
	 */
	if (vop2->version == VOP_VERSION_RK3588)
		VOP_MODULE_SET(vop2, vp, dsp_background, 0x80000000);
	if (is_vop3(vop2))
		vop3_setup_pipe_dly(vp, NULL);

	vop2_cfg_done(crtc);

	/*
	 * when clear standby bits, it will take effect immediately,
	 * This means the vp will start scan out immediately with
	 * the timing it been configured before.
	 * So we must make sure release standby after the display
	 * timing is correctly configured.
	 * This is important when switch resolution, such as
	 * 4K-->720P:
	 * if we release standby before 720P timing is configured,
	 * the VP will start scan out immediately with 4K timing,
	 * when we switch dclk to 74.25MHZ, VP timing is still 4K,
	 * so VP scan out with 4K timing at 74.25MHZ dclk, this is
	 * very slow, than this will trigger vblank timeout.
	 *
	 */
	VOP_MODULE_SET(vop2, vp, standby, 0);

	if (vp->mcu_timing.mcu_pix_total) {
		vop3_set_out_mode(crtc, vcstate->output_mode);
		vop3_mcu_mode_setup(crtc);
	}

	if (!vp->loader_protect)
		vop2_clk_reset(vp->dclk_rst);
	if (vcstate->dsc_enable)
		rk3588_vop2_dsc_cfg_done(crtc);
	drm_crtc_vblank_on(crtc);
	/*
	 * restore the lut table.
	 */
	if (vp->gamma_lut_active)
		vop2_crtc_load_lut(crtc);
out:
	vop2_unlock(vop2);
}

static int vop2_zpos_cmp(const void *a, const void *b)
{
	struct vop2_zpos *pa = (struct vop2_zpos *)a;
	struct vop2_zpos *pb = (struct vop2_zpos *)b;

	if (pa->zpos != pb->zpos)
		return pa->zpos - pb->zpos;
	else
		return pa->plane->base.id - pb->plane->base.id;
}

static int vop2_crtc_atomic_check(struct drm_crtc *crtc,
				  struct drm_crtc_state *crtc_state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2_video_port *splice_vp;
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct rockchip_crtc_state *new_vcstate = to_rockchip_crtc_state(crtc_state);
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;

	if (vop2_has_feature(vop2, VOP_FEATURE_SPLICE)) {
		if (adjusted_mode->hdisplay > VOP2_MAX_VP_OUTPUT_WIDTH) {
			vcstate->splice_mode = true;
			splice_vp = &vop2->vps[vp_data->splice_vp_id];
			splice_vp->splice_mode_right = true;
			splice_vp->left_vp = vp;
		}
	}

	if ((vcstate->request_refresh_rate != new_vcstate->request_refresh_rate) ||
	    crtc_state->active_changed || crtc_state->mode_changed)
		vp->refresh_rate_change = true;
	else
		vp->refresh_rate_change = false;

	return 0;
}

static void vop3_disable_dynamic_hdr(struct vop2_video_port *vp, uint8_t win_phys_id)
{
	struct vop2 *vop2 = vp->vop2;
	struct vop2_win *win = vop2_find_win_by_phys_id(vop2, win_phys_id);
	struct drm_plane *plane = &win->base;
	struct drm_plane_state *pstate = plane->state;
	struct vop2_plane_state *vpstate = to_vop2_plane_state(pstate);

	VOP_MODULE_SET(vop2, vp, hdr10_en, 0);
	VOP_MODULE_SET(vop2, vp, hdr_vivid_en, 0);
	VOP_MODULE_SET(vop2, vp, hdr_vivid_bypass_en, 0);
	VOP_MODULE_SET(vop2, vp, hdr_lut_update_en, 0);
	VOP_MODULE_SET(vop2, vp, sdr2hdr_en, 0);
	VOP_MODULE_SET(vop2, vp, sdr2hdr_path_en, 0);
	VOP_MODULE_SET(vop2, vp, sdr2hdr_auto_gating_en, 1);

	vp->hdr_en = false;
	vp->hdr_in = false;
	vp->hdr_out = false;
	vp->sdr2hdr_en = false;
	vpstate->hdr_in = false;
	vpstate->hdr2sdr_en = false;
}

static void vop3_setup_hdrvivid(struct vop2_video_port *vp, uint8_t win_phys_id)
{
	struct vop2 *vop2 = vp->vop2;
	struct vop2_win *win = vop2_find_win_by_phys_id(vop2, win_phys_id);
	struct drm_plane *plane = &win->base;
	struct drm_plane_state *pstate = plane->state;
	struct vop2_plane_state *vpstate = to_vop2_plane_state(pstate);
	struct drm_crtc_state *cstate = vp->rockchip_crtc.crtc.state;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(cstate);
	unsigned long win_mask = vp->win_mask;
	int phys_id;
	struct hdrvivid_regs *hdrvivid_data;
	struct hdr_extend *hdr_data;
	struct rockchip_gem_object *lut_gem_obj;
	bool have_sdr_layer = false;
	uint32_t hdr_mode;
	int i;
	u32 *tone_lut_kvaddr;
	dma_addr_t tone_lut_mst;

	vp->hdr_en = false;
	vp->hdr_in = false;
	vp->hdr_out = false;
	vp->sdr2hdr_en = false;
	vpstate->hdr_in = false;
	vpstate->hdr2sdr_en = false;

	hdr_data = (struct hdr_extend *)vcstate->hdr_ext_data->data;
	hdrvivid_data = &hdr_data->hdrvivid_data;

	hdr_mode = hdrvivid_data->hdr_mode;

	if (hdr_mode > SDR2HLG && hdr_mode != SDR2HDR10_USERSPACE &&
	    hdr_mode != SDR2HLG_USERSPACE) {
		DRM_ERROR("Invalid HDR mode:%d, beyond the mode range\n", hdr_mode);
		return;
	}

	/* adjust userspace hdr mode value to kernel value */
	if (hdr_mode == SDR2HDR10_USERSPACE)
		hdr_mode = SDR2HDR10;
	if (hdr_mode == SDR2HLG_USERSPACE)
		hdr_mode = SDR2HLG;

	if (hdr_mode <= HDR102SDR && vpstate->eotf != HDMI_EOTF_SMPTE_ST2084 && vpstate->eotf != HDMI_EOTF_BT_2100_HLG) {
		DRM_ERROR("Invalid HDR mode:%d, mismatch plane eotf:%d\n", hdr_mode,
			  vpstate->eotf);
		return;
	}

	vp->hdrvivid_mode = hdr_mode;
	vcstate->yuv_overlay = false;

	if (hdr_mode <= HDR102SDR) {
		vp->hdr_en = true;
		vp->hdr_in = true;
		vpstate->hdr_in = true;
	} else {
		vp->sdr2hdr_en = true;
	}

	/*
	 * To confirm whether need to enable sdr2hdr.
	 */
	for_each_set_bit(phys_id, &win_mask, ROCKCHIP_MAX_LAYER) {
		win = vop2_find_win_by_phys_id(vop2, phys_id);
		plane = &win->base;
		pstate = plane->state;
		vpstate = to_vop2_plane_state(pstate);

		/* skip inactive plane */
		if (!vop2_plane_active(pstate))
			continue;

		if (vpstate->eotf != HDMI_EOTF_SMPTE_ST2084 &&
		    vpstate->eotf != HDMI_EOTF_BT_2100_HLG) {
			have_sdr_layer = true;
			break;
		}
	}

	if (hdr_mode == PQHDR2SDR_WITH_DYNAMIC || hdr_mode == HLG2SDR_WITH_DYNAMIC ||
	    hdr_mode == HLG2SDR_WITHOUT_DYNAMIC || hdr_mode == HDR102SDR) {
		vpstate->hdr2sdr_en = true;
	} else {
		vp->hdr_out = true;
		if (have_sdr_layer)
			vp->sdr2hdr_en = true;
	}

	/**
	 * Config hdr ctrl registers
	 */
	vop2_writel(vop2, RK3528_SDR2HDR_CTRL, hdrvivid_data->sdr2hdr_ctrl);
	vop2_writel(vop2, RK3528_HDRVIVID_CTRL, hdrvivid_data->hdrvivid_ctrl);

	VOP_MODULE_SET(vop2, vp, hdr10_en, vp->hdr_en);
	if (vp->hdr_en) {
		VOP_MODULE_SET(vop2, vp, hdr_vivid_en, (hdr_mode == HDR_BYPASS) ? 0 : 1);
		VOP_MODULE_SET(vop2, vp, hdr_vivid_path_mode,
			       (hdr_mode == HDR102SDR) ? PQHDR2SDR_WITH_DYNAMIC : hdr_mode);
		VOP_MODULE_SET(vop2, vp, hdr_vivid_bypass_en, (hdr_mode == HDR_BYPASS) ? 1 : 0);
	} else {
		VOP_MODULE_SET(vop2, vp, hdr_vivid_en, 0);
	}
	VOP_MODULE_SET(vop2, vp, sdr2hdr_en, vp->sdr2hdr_en);
	VOP_MODULE_SET(vop2, vp, sdr2hdr_path_en, vp->sdr2hdr_en);
	VOP_MODULE_SET(vop2, vp, sdr2hdr_auto_gating_en, vp->sdr2hdr_en ? 0 : 1);

	vop2_writel(vop2, RK3528_SDR_CFG_COE0, hdrvivid_data->sdr2hdr_coe0);
	vop2_writel(vop2, RK3528_SDR_CFG_COE1, hdrvivid_data->sdr2hdr_coe1);
	vop2_writel(vop2, RK3528_SDR_CSC_COE00_01, hdrvivid_data->sdr2hdr_csc_coe00_01);
	vop2_writel(vop2, RK3528_SDR_CSC_COE02_10, hdrvivid_data->sdr2hdr_csc_coe02_10);
	vop2_writel(vop2, RK3528_SDR_CSC_COE11_12, hdrvivid_data->sdr2hdr_csc_coe11_12);
	vop2_writel(vop2, RK3528_SDR_CSC_COE20_21, hdrvivid_data->sdr2hdr_csc_coe20_21);
	vop2_writel(vop2, RK3528_SDR_CSC_COE22, hdrvivid_data->sdr2hdr_csc_coe22);

	vop2_writel(vop2, RK3528_HDR_PQ_GAMMA, hdrvivid_data->hdr_pq_gamma);
	vop2_writel(vop2, RK3528_HLG_RFIX_SCALEFAC, hdrvivid_data->hlg_rfix_scalefac);
	vop2_writel(vop2, RK3528_HLG_MAXLUMA, hdrvivid_data->hlg_maxluma);
	vop2_writel(vop2, RK3528_HLG_R_TM_LIN2NON, hdrvivid_data->hlg_r_tm_lin2non);

	vop2_writel(vop2, RK3528_HDR_CSC_COE00_01, hdrvivid_data->hdr_csc_coe00_01);
	vop2_writel(vop2, RK3528_HDR_CSC_COE02_10, hdrvivid_data->hdr_csc_coe02_10);
	vop2_writel(vop2, RK3528_HDR_CSC_COE11_12, hdrvivid_data->hdr_csc_coe11_12);
	vop2_writel(vop2, RK3528_HDR_CSC_COE20_21, hdrvivid_data->hdr_csc_coe20_21);
	vop2_writel(vop2, RK3528_HDR_CSC_COE22, hdrvivid_data->hdr_csc_coe22);

	if (!vp->hdr_lut_gem_obj) {
		lut_gem_obj = rockchip_gem_create_object(vop2->drm_dev,
			RK_HDRVIVID_TONE_SCA_AXI_TAB_LENGTH * 4, true, 0);
		if (IS_ERR(lut_gem_obj)) {
			DRM_ERROR("create hdr lut obj failed\n");
			return;
		}
		vp->hdr_lut_gem_obj = lut_gem_obj;
	}

	tone_lut_kvaddr = (u32 *)vp->hdr_lut_gem_obj->kvaddr;
	tone_lut_mst = vp->hdr_lut_gem_obj->dma_addr;

	for (i = 0; i < RK_HDRVIVID_TONE_SCA_AXI_TAB_LENGTH; i++)
		*tone_lut_kvaddr++ =  hdrvivid_data->tone_sca_axi_tab[i];

	VOP_MODULE_SET(vop2, vp, lut_dma_rid, vp->lut_dma_rid - vp->id);
	VOP_MODULE_SET(vop2, vp, hdr_lut_mode, 1);
	VOP_MODULE_SET(vop2, vp, hdr_lut_mst, tone_lut_mst);
	VOP_MODULE_SET(vop2, vp, hdr_lut_update_en, 1);
	VOP_CTRL_SET(vop2, lut_dma_en, 1);

	for (i = 0; i < RK_HDRVIVID_GAMMA_CURVE_LENGTH; i++)
		vop2_writel(vop2, RK3528_HDRGAMMA_CURVE + i * 4, hdrvivid_data->hdrgamma_curve[i]);

	for (i = 0; i < RK_HDRVIVID_GAMMA_MDFVALUE_LENGTH; i++)
		vop2_writel(vop2, RK3528_HDRGAMMA_MDFVALUE + i * 4,
			    hdrvivid_data->hdrgamma_mdfvalue[i]);

	for (i = 0; i < RK_SDR2HDR_INVGAMMA_CURVE_LENGTH; i++)
		vop2_writel(vop2, RK3528_SDRINVGAMMA_CURVE + i * 4,
			    hdrvivid_data->sdrinvgamma_curve[i]);

	for (i = 0; i < RK_SDR2HDR_INVGAMMA_S_IDX_LENGTH; i++)
		vop2_writel(vop2, RK3528_SDRINVGAMMA_STARTIDX + i * 4,
			    hdrvivid_data->sdrinvgamma_startidx[i]);

	for (i = 0; i < RK_SDR2HDR_INVGAMMA_C_IDX_LENGTH; i++)
		vop2_writel(vop2, RK3528_SDRINVGAMMA_CHANGEIDX + i * 4,
			    hdrvivid_data->sdrinvgamma_changeidx[i]);

	for (i = 0; i < RK_SDR2HDR_SMGAIN_LENGTH; i++)
		vop2_writel(vop2, RK3528_SDR_SMGAIN + i * 4, hdrvivid_data->sdr_smgain[i]);
}

static void vop3_setup_dynamic_hdr(struct vop2_video_port *vp, uint8_t win_phys_id)
{
	struct drm_crtc_state *cstate = vp->rockchip_crtc.crtc.state;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(cstate);
	struct hdr_extend *hdr_data;
	uint32_t hdr_format;

	/* If hdr extend data is null, exit hdr mode */
	if (!vcstate->hdr_ext_data) {
		vop3_disable_dynamic_hdr(vp, win_phys_id);
		return;
	}

	hdr_data = (struct hdr_extend *)vcstate->hdr_ext_data->data;
	hdr_format = hdr_data->hdr_type;

	switch (hdr_format) {
	case HDR_NONE:
	case HDR_HDR10:
	case HDR_HLGSTATIC:
	case HDR_HDRVIVID:
		/*
		 * hdr module support hdr10, hlg, vividhdr
		 * sdr2hdr module support hdrnone for sdr2hdr
		 */
		vop3_setup_hdrvivid(vp, win_phys_id);
		break;
	default:
		DRM_DEBUG("unsupprot hdr format:%u\n", hdr_format);
		break;
	}
}

static void vop2_setup_hdr10(struct vop2_video_port *vp, uint8_t win_phys_id)
{
	struct vop2 *vop2 = vp->vop2;
	struct vop2_win *win = vop2_find_win_by_phys_id(vop2, win_phys_id);
	struct drm_plane *plane = &win->base;
	struct drm_plane_state *pstate;
	struct drm_crtc_state *cstate = vp->rockchip_crtc.crtc.state;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_hdr_table *hdr_table = vp_data->hdr_table;
	struct rockchip_crtc_state *vcstate;
	struct vop2_plane_state *vpstate;
	uint32_t lut_mode = VOP2_HDR_LUT_MODE_AHB;
	uint32_t sdr2hdr_r2r_mode = 0;
	bool hdr_en = 0;
	bool hdr2sdr_en = 0;
	bool sdr2hdr_en = 0;
	bool sdr2hdr_tf = 0;
	bool hdr2sdr_tf_update = 1;
	bool sdr2hdr_tf_update = 0; /* default sdr2hdr curve is 1000 nit */
	unsigned long win_mask = vp->win_mask;
	int phys_id;
	bool have_sdr_layer = false;

	/*
	 * Check whether this video port support hdr or not
	 */
	if (!hdr_table)
		return;

	/*
	 * right vp share the same crtc/plane state in splice mode
	 */
	if (vp->splice_mode_right) {
		vcstate = to_rockchip_crtc_state(vp->left_vp->rockchip_crtc.crtc.state);
		pstate = win->left_win->base.state;
	} else {
		vcstate = to_rockchip_crtc_state(cstate);
		pstate = plane->state;
	}

	vpstate = to_vop2_plane_state(pstate);

	/*
	 * HDR video plane input
	 */
	if (vpstate->eotf == HDMI_EOTF_SMPTE_ST2084)
		hdr_en = 1;

	vp->hdr_en = hdr_en;
	vp->hdr_in = hdr_en;
	vp->hdr_out = (vcstate->eotf == HDMI_EOTF_SMPTE_ST2084) ? true : false;

	/*
	 * only laryer0 support hdr2sdr
	 * if we have more than one active win attached to the video port,
	 * the other attached win must for ui, and should do sdr2hdr.
	 *
	 */
	if (vp->hdr_in && !vp->hdr_out)
		hdr2sdr_en = 1;
	vpstate->hdr_in = hdr_en;
	vpstate->hdr2sdr_en = hdr2sdr_en;

	/*
	 * To confirm whether need to enable sdr2hdr.
	 */
	for_each_set_bit(phys_id, &win_mask, ROCKCHIP_MAX_LAYER) {
		win = vop2_find_win_by_phys_id(vop2, phys_id);
		if (vp->splice_mode_right) {
			if (win->left_win)
				pstate = win->left_win->base.state;
			else
				pstate = NULL; /* this win is not activated */
		} else {
			pstate = win->base.state;
		}

		vpstate = pstate ? to_vop2_plane_state(pstate) : NULL;

		if (!vop2_plane_active(pstate))
			continue;

		if (vpstate->eotf != HDMI_EOTF_SMPTE_ST2084) {
			have_sdr_layer = true;
			break;
		}
	}

	if (have_sdr_layer && vp->hdr_out) {
		sdr2hdr_en = 1;
		sdr2hdr_r2r_mode = BT709_TO_BT2020;
		sdr2hdr_tf = SDR2HDR_FOR_HDR;
	}
	vp->sdr2hdr_en = sdr2hdr_en;

	VOP_MODULE_SET(vop2, vp, hdr10_en, hdr_en);

	if (hdr2sdr_en || sdr2hdr_en) {
		/*
		 * HDR2SDR and SDR2HDR must overlay in yuv color space
		 */
		vcstate->yuv_overlay = false;
		VOP_MODULE_SET(vop2, vp, hdr_lut_mode, lut_mode);
	}

	if (hdr2sdr_en) {
		if (hdr2sdr_tf_update)
			vop2_load_hdr2sdr_table(vp);
		VOP_MODULE_SET(vop2, vp, hdr2sdr_src_min, hdr_table->hdr2sdr_src_range_min);
		VOP_MODULE_SET(vop2, vp, hdr2sdr_src_max, hdr_table->hdr2sdr_src_range_max);
		VOP_MODULE_SET(vop2, vp, hdr2sdr_normfaceetf, hdr_table->hdr2sdr_normfaceetf);
		VOP_MODULE_SET(vop2, vp, hdr2sdr_dst_min, hdr_table->hdr2sdr_dst_range_min);
		VOP_MODULE_SET(vop2, vp, hdr2sdr_dst_max, hdr_table->hdr2sdr_dst_range_max);
		VOP_MODULE_SET(vop2, vp, hdr2sdr_normfacgamma, hdr_table->hdr2sdr_normfacgamma);
	}
	VOP_MODULE_SET(vop2, vp, hdr2sdr_en, hdr2sdr_en);
	VOP_MODULE_SET(vop2, vp, hdr2sdr_bypass_en, !hdr2sdr_en);

	if (sdr2hdr_en) {
		if (sdr2hdr_tf_update)
			vop2_load_sdr2hdr_table(vp, sdr2hdr_tf);
		VOP_MODULE_SET(vop2, vp, sdr2hdr_r2r_mode, sdr2hdr_r2r_mode);
	}
	VOP_MODULE_SET(vop2, vp, sdr2hdr_path_en, sdr2hdr_en);
	VOP_MODULE_SET(vop2, vp, sdr2hdr_oetf_en, sdr2hdr_en);
	VOP_MODULE_SET(vop2, vp, sdr2hdr_eotf_en, sdr2hdr_en);
	VOP_MODULE_SET(vop2, vp, sdr2hdr_r2r_en, sdr2hdr_en);
	VOP_MODULE_SET(vop2, vp, sdr2hdr_bypass_en, !sdr2hdr_en);
}

static void vop2_parse_alpha(struct vop2_alpha_config *alpha_config,
			     struct vop2_alpha *alpha)
{
	int src_glb_alpha_en = (alpha_config->src_glb_alpha_value == 0xff) ? 0 : 1;
	int dst_glb_alpha_en = (alpha_config->dst_glb_alpha_value == 0xff) ? 0 : 1;
	int src_color_mode = alpha_config->src_premulti_en ? ALPHA_SRC_PRE_MUL : ALPHA_SRC_NO_PRE_MUL;
	int dst_color_mode = alpha_config->dst_premulti_en ? ALPHA_SRC_PRE_MUL : ALPHA_SRC_NO_PRE_MUL;

	alpha->src_color_ctrl.val = 0;
	alpha->dst_color_ctrl.val = 0;
	alpha->src_alpha_ctrl.val = 0;
	alpha->dst_alpha_ctrl.val = 0;

	if (!alpha_config->src_pixel_alpha_en)
		alpha->src_color_ctrl.bits.blend_mode = ALPHA_GLOBAL;
	else if (alpha_config->src_pixel_alpha_en && !src_glb_alpha_en)
		alpha->src_color_ctrl.bits.blend_mode = ALPHA_PER_PIX;
	else
		alpha->src_color_ctrl.bits.blend_mode = ALPHA_PER_PIX_GLOBAL;

	alpha->src_color_ctrl.bits.alpha_en = 1;

	if (alpha->src_color_ctrl.bits.blend_mode == ALPHA_GLOBAL) {
		alpha->src_color_ctrl.bits.color_mode = src_color_mode;
		alpha->src_color_ctrl.bits.factor_mode = SRC_FAC_ALPHA_SRC_GLOBAL;
	} else if (alpha->src_color_ctrl.bits.blend_mode == ALPHA_PER_PIX) {
		alpha->src_color_ctrl.bits.color_mode = src_color_mode;
		alpha->src_color_ctrl.bits.factor_mode = SRC_FAC_ALPHA_ONE;
	} else {
		alpha->src_color_ctrl.bits.color_mode = ALPHA_SRC_PRE_MUL;
		alpha->src_color_ctrl.bits.factor_mode = SRC_FAC_ALPHA_SRC_GLOBAL;
	}
	alpha->src_color_ctrl.bits.glb_alpha = alpha_config->src_glb_alpha_value;
	alpha->src_color_ctrl.bits.alpha_mode = ALPHA_STRAIGHT;
	alpha->src_color_ctrl.bits.alpha_cal_mode = ALPHA_SATURATION;

	alpha->dst_color_ctrl.bits.alpha_mode = ALPHA_STRAIGHT;
	alpha->dst_color_ctrl.bits.alpha_cal_mode = ALPHA_SATURATION;
	alpha->dst_color_ctrl.bits.blend_mode = ALPHA_GLOBAL;
	alpha->dst_color_ctrl.bits.glb_alpha = alpha_config->dst_glb_alpha_value;
	alpha->dst_color_ctrl.bits.color_mode = dst_color_mode;
	alpha->dst_color_ctrl.bits.factor_mode = ALPHA_SRC_INVERSE;

	alpha->src_alpha_ctrl.bits.alpha_mode = ALPHA_STRAIGHT;
	alpha->src_alpha_ctrl.bits.blend_mode = alpha->src_color_ctrl.bits.blend_mode;
	alpha->src_alpha_ctrl.bits.alpha_cal_mode = ALPHA_SATURATION;
	alpha->src_alpha_ctrl.bits.factor_mode = ALPHA_ONE;

	alpha->dst_alpha_ctrl.bits.alpha_mode = ALPHA_STRAIGHT;
	if (alpha_config->dst_pixel_alpha_en && !dst_glb_alpha_en)
		alpha->dst_alpha_ctrl.bits.blend_mode = ALPHA_PER_PIX;
	else
		alpha->dst_alpha_ctrl.bits.blend_mode = ALPHA_PER_PIX_GLOBAL;
	alpha->dst_alpha_ctrl.bits.alpha_cal_mode = ALPHA_NO_SATURATION;
	alpha->dst_alpha_ctrl.bits.factor_mode = ALPHA_SRC_INVERSE;
}

static int vop2_find_start_mixer_id_for_vp(struct vop2 *vop2, uint8_t port_id)
{
	struct vop2_video_port *vp;
	int used_layer = 0;
	int i;

	for (i = 0; i < port_id; i++) {
		vp = &vop2->vps[i];
		used_layer += hweight32(vp->win_mask);
	}

	return used_layer;
}

/*
 * src: top layer
 * dst: bottom layer.
 * Cluster mixer default use win1 as top layer
 */
static void vop2_setup_cluster_alpha(struct vop2 *vop2, struct vop2_cluster *cluster)
{
	uint32_t src_color_ctrl_offset = cluster->main->regs->cluster->src_color_ctrl.offset;
	uint32_t dst_color_ctrl_offset = cluster->main->regs->cluster->dst_color_ctrl.offset;
	uint32_t src_alpha_ctrl_offset = cluster->main->regs->cluster->src_alpha_ctrl.offset;
	uint32_t dst_alpha_ctrl_offset = cluster->main->regs->cluster->dst_alpha_ctrl.offset;
	struct drm_framebuffer *fb;
	struct vop2_alpha_config alpha_config;
	struct vop2_alpha alpha;
	struct vop2_win *main_win = cluster->main;
	struct vop2_win *sub_win = cluster->sub;
	struct drm_plane *plane;
	struct vop2_plane_state *main_vpstate;
	struct vop2_plane_state *sub_vpstate;
	struct vop2_plane_state *top_win_vpstate;
	struct vop2_plane_state *bottom_win_vpstate;
	bool src_pixel_alpha_en = false;
	u16 src_glb_alpha_val = 0xff, dst_glb_alpha_val = 0xff;
	bool premulti_en = false;
	bool swap = false;

	if (!sub_win) {
		/* At one win mode, win0 is dst/bottom win, and win1 is a all zero src/top win */

		/*
		 * right cluster share the same plane state in splice mode
		 */
		if (cluster->splice_mode)
			plane = &main_win->left_win->base;
		else
			plane = &main_win->base;

		top_win_vpstate = NULL;
		bottom_win_vpstate = to_vop2_plane_state(plane->state);
		src_glb_alpha_val = 0;
		dst_glb_alpha_val = bottom_win_vpstate->global_alpha;
	} else {
		plane = &sub_win->base;
		sub_vpstate = to_vop2_plane_state(plane->state);
		plane = &main_win->base;
		main_vpstate = to_vop2_plane_state(plane->state);
		if (main_vpstate->zpos > sub_vpstate->zpos) {
			swap = 1;
			top_win_vpstate = main_vpstate;
			bottom_win_vpstate = sub_vpstate;
		} else {
			swap = 0;
			top_win_vpstate = sub_vpstate;
			bottom_win_vpstate = main_vpstate;
		}
		src_glb_alpha_val = top_win_vpstate->global_alpha;
		dst_glb_alpha_val = bottom_win_vpstate->global_alpha;
	}

	if (top_win_vpstate) {
		fb = top_win_vpstate->base.fb;
		if (!fb)
			return;
		if (top_win_vpstate->base.pixel_blend_mode == DRM_MODE_BLEND_PREMULTI)
			premulti_en = true;
		else
			premulti_en = false;
		src_pixel_alpha_en = is_alpha_support(fb->format->format);
	}
	fb = bottom_win_vpstate->base.fb;
	if (!fb)
		return;
	alpha_config.src_premulti_en = premulti_en;
	alpha_config.dst_premulti_en = false;
	alpha_config.src_pixel_alpha_en = src_pixel_alpha_en;
	alpha_config.dst_pixel_alpha_en = true; /* alpha value need transfer to next mix */
	alpha_config.src_glb_alpha_value = src_glb_alpha_val;
	alpha_config.dst_glb_alpha_value = dst_glb_alpha_val;
	vop2_parse_alpha(&alpha_config, &alpha);

	alpha.src_color_ctrl.bits.src_dst_swap = swap;
	vop2_writel(vop2, src_color_ctrl_offset, alpha.src_color_ctrl.val);
	vop2_writel(vop2, dst_color_ctrl_offset, alpha.dst_color_ctrl.val);
	vop2_writel(vop2, src_alpha_ctrl_offset, alpha.src_alpha_ctrl.val);
	vop2_writel(vop2, dst_alpha_ctrl_offset, alpha.dst_alpha_ctrl.val);
}

static void vop2_setup_alpha(struct vop2_video_port *vp,
			     const struct vop2_zpos *vop2_zpos)
{
	struct vop2 *vop2 = vp->vop2;
	uint32_t src_color_ctrl_offset = vop2->data->ctrl->src_color_ctrl.offset;
	uint32_t dst_color_ctrl_offset = vop2->data->ctrl->dst_color_ctrl.offset;
	uint32_t src_alpha_ctrl_offset = vop2->data->ctrl->src_alpha_ctrl.offset;
	uint32_t dst_alpha_ctrl_offset = vop2->data->ctrl->dst_alpha_ctrl.offset;
	unsigned long win_mask = vp->win_mask;
	const struct vop2_zpos *zpos;
	struct vop2_plane_state *vpstate;
	struct vop2_alpha_config alpha_config;
	struct vop2_alpha alpha;
	struct vop2_win *win;
	struct drm_plane_state *pstate;
	struct drm_framebuffer *fb;
	int pixel_alpha_en;
	int premulti_en = 1;
	int mixer_id;
	int phys_id;
	uint32_t offset;
	int i;
	bool bottom_layer_alpha_en = false;
	u32 dst_global_alpha = 0xff;

	for_each_set_bit(phys_id, &win_mask, ROCKCHIP_MAX_LAYER) {
		win = vop2_find_win_by_phys_id(vop2, phys_id);
		if (win->splice_mode_right)
			pstate = win->left_win->base.state;
		else
			pstate = win->base.state;

		vpstate = to_vop2_plane_state(pstate);

		if (!vop2_plane_active(pstate))
			continue;

		if (vpstate->zpos == 0 && vpstate->global_alpha != 0xff &&
		    !vop2_cluster_window(win)) {
			/*
			 * If bottom layer have global alpha effect [except cluster layer,
			 * because cluster have deal with bottom layer global alpha value
			 * at cluster mix], bottom layer mix need deal with global alpha.
			 */
			bottom_layer_alpha_en = true;
			dst_global_alpha = vpstate->global_alpha;
			if (pstate->pixel_blend_mode == DRM_MODE_BLEND_PREMULTI)
				premulti_en = 1;
			else
				premulti_en = 0;

			break;
		}
	}

	mixer_id = vop2_find_start_mixer_id_for_vp(vop2, vp->id);

	if (vop2->version == VOP_VERSION_RK3588 &&
	    vp->hdr10_at_splice_mode && vp->id == 0)
		mixer_id++;/* fixed path for rk3588: layer1 -> hdr10_1 */

	alpha_config.dst_pixel_alpha_en = true; /* alpha value need transfer to next mix */
	for (i = 1; i < vp->nr_layers; i++) {
		zpos = &vop2_zpos[i];
		win = vop2_find_win_by_phys_id(vop2, zpos->win_phys_id);
		if (win->splice_mode_right)
			pstate = win->left_win->base.state;
		else
			pstate = win->base.state;

		vpstate = to_vop2_plane_state(pstate);
		fb = pstate->fb;
		if (pstate->pixel_blend_mode == DRM_MODE_BLEND_PREMULTI)
			premulti_en = 1;
		else
			premulti_en = 0;
		pixel_alpha_en = is_alpha_support(fb->format->format);

		alpha_config.src_premulti_en = premulti_en;
		if (bottom_layer_alpha_en && i == 1) {/* Cd = Cs + (1 - As) * Cd * Agd */
			alpha_config.dst_premulti_en = false;
			alpha_config.src_pixel_alpha_en = pixel_alpha_en;
			alpha_config.src_glb_alpha_value =  vpstate->global_alpha;
			alpha_config.dst_glb_alpha_value = dst_global_alpha;
		} else if (vop2_cluster_window(win)) {/* Mix output data only have pixel alpha */
			alpha_config.dst_premulti_en = true;
			alpha_config.src_pixel_alpha_en = true;
			alpha_config.src_glb_alpha_value = 0xff;
			alpha_config.dst_glb_alpha_value = 0xff;
		} else {/* Cd = Cs + (1 - As) * Cd */
			alpha_config.dst_premulti_en = true;
			alpha_config.src_pixel_alpha_en = pixel_alpha_en;
			alpha_config.src_glb_alpha_value =  vpstate->global_alpha;
			alpha_config.dst_glb_alpha_value = 0xff;
		}
		vop2_parse_alpha(&alpha_config, &alpha);

		offset = (mixer_id + i - 1) * 0x10;
		vop2_writel(vop2, src_color_ctrl_offset + offset, alpha.src_color_ctrl.val);
		vop2_writel(vop2, dst_color_ctrl_offset + offset, alpha.dst_color_ctrl.val);
		vop2_writel(vop2, src_alpha_ctrl_offset + offset, alpha.src_alpha_ctrl.val);
		vop2_writel(vop2, dst_alpha_ctrl_offset + offset, alpha.dst_alpha_ctrl.val);
	}

	if (bottom_layer_alpha_en || vp->hdr_en) {
		/* Transfer pixel alpha to hdr mix */
		alpha_config.src_premulti_en = premulti_en;
		alpha_config.dst_premulti_en = true;
		alpha_config.src_pixel_alpha_en = true;
		alpha_config.src_glb_alpha_value = 0xff;
		alpha_config.dst_glb_alpha_value = 0xff;
		vop2_parse_alpha(&alpha_config, &alpha);

		VOP_MODULE_SET(vop2, vp, hdr_src_color_ctrl,
			       alpha.src_color_ctrl.val);
		VOP_MODULE_SET(vop2, vp, hdr_dst_color_ctrl,
			       alpha.dst_color_ctrl.val);
		VOP_MODULE_SET(vop2, vp, hdr_src_alpha_ctrl,
			       alpha.src_alpha_ctrl.val);
		VOP_MODULE_SET(vop2, vp, hdr_dst_alpha_ctrl,
			       alpha.dst_alpha_ctrl.val);
	} else {
		VOP_MODULE_SET(vop2, vp, hdr_src_color_ctrl, 0);
	}

	/* Transfer pixel alpha value to next mix */
	alpha_config.src_premulti_en = true;
	alpha_config.dst_premulti_en = true;
	alpha_config.src_pixel_alpha_en = false;
	alpha_config.src_glb_alpha_value = 0xff;
	alpha_config.dst_glb_alpha_value = 0xff;
	vop2_parse_alpha(&alpha_config, &alpha);

	for (; i < hweight32(vp->win_mask); i++) {
		offset = (mixer_id + i - 1) * 0x10;

		vop2_writel(vop2, src_color_ctrl_offset + offset, alpha.src_alpha_ctrl.val);
		vop2_writel(vop2, dst_color_ctrl_offset + offset, alpha.dst_color_ctrl.val);
		vop2_writel(vop2, src_alpha_ctrl_offset + offset, alpha.src_alpha_ctrl.val);
		vop2_writel(vop2, dst_alpha_ctrl_offset + offset, alpha.dst_alpha_ctrl.val);
	}
}

static void vop3_setup_alpha(struct vop2_video_port *vp,
			     const struct vop2_zpos *vop2_zpos)
{
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_video_port_data *vp_data = &vop2->data->vp[vp->id];
	const struct vop3_ovl_regs *ovl_regs = vop2->data->vp[vp->id].ovl_regs;
	uint32_t src_color_ctrl_offset = ovl_regs->layer_mix_regs->src_color_ctrl.offset;
	uint32_t dst_color_ctrl_offset = ovl_regs->layer_mix_regs->dst_color_ctrl.offset;
	uint32_t src_alpha_ctrl_offset = ovl_regs->layer_mix_regs->src_alpha_ctrl.offset;
	uint32_t dst_alpha_ctrl_offset = ovl_regs->layer_mix_regs->dst_alpha_ctrl.offset;
	unsigned long win_mask = vp->win_mask;
	const struct vop2_zpos *zpos;
	struct vop2_plane_state *vpstate;
	struct vop2_alpha_config alpha_config;
	union vop2_bg_alpha_ctrl bg_alpha_ctrl;
	struct vop2_alpha alpha;
	struct vop2_win *win;
	struct drm_plane_state *pstate;
	struct drm_framebuffer *fb;
	int pixel_alpha_en;
	int premulti_en = 1;
	int phys_id;
	uint32_t offset;
	int i;
	bool bottom_layer_alpha_en = false;
	u32 dst_global_alpha = 0xff;

	for_each_set_bit(phys_id, &win_mask, ROCKCHIP_MAX_LAYER) {
		win = vop2_find_win_by_phys_id(vop2, phys_id);
		pstate = win->base.state;
		vpstate = to_vop2_plane_state(pstate);

		if (!vop2_plane_active(pstate))
			continue;

		if (vpstate->zpos == 0 && vpstate->global_alpha != 0xff &&
		    !vop2_cluster_window(win)) {
			/*
			 * If bottom layer have global alpha effect [except cluster layer,
			 * because cluster have deal with bottom layer global alpha value
			 * at cluster mix], bottom layer mix need deal with global alpha.
			 */
			bottom_layer_alpha_en = true;
			dst_global_alpha = vpstate->global_alpha;
			if (pstate->pixel_blend_mode == DRM_MODE_BLEND_PREMULTI)
				premulti_en = 1;
			else
				premulti_en = 0;

			break;
		}
	}

	alpha_config.dst_pixel_alpha_en = true; /* alpha value need transfer to next mix */
	for (i = 1; i < vp->nr_layers; i++) {
		zpos = &vop2_zpos[i];
		win = vop2_find_win_by_phys_id(vop2, zpos->win_phys_id);
		pstate = win->base.state;
		vpstate = to_vop2_plane_state(pstate);
		fb = pstate->fb;
		if (pstate->pixel_blend_mode == DRM_MODE_BLEND_PREMULTI)
			premulti_en = 1;
		else
			premulti_en = 0;
		pixel_alpha_en = is_alpha_support(fb->format->format);

		alpha_config.src_premulti_en = premulti_en;
		if (bottom_layer_alpha_en && i == 1) {/* Cd = Cs + (1 - As) * Cd * Agd */
			alpha_config.dst_premulti_en = false;
			alpha_config.src_pixel_alpha_en = pixel_alpha_en;
			alpha_config.src_glb_alpha_value =  vpstate->global_alpha;
			alpha_config.dst_glb_alpha_value = dst_global_alpha;
		} else if (vop2_cluster_window(win)) {/* Mix output data only have pixel alpha */
			alpha_config.dst_premulti_en = true;
			alpha_config.src_pixel_alpha_en = true;
			alpha_config.src_glb_alpha_value = 0xff;
			alpha_config.dst_glb_alpha_value = 0xff;
		} else {/* Cd = Cs + (1 - As) * Cd */
			alpha_config.dst_premulti_en = true;
			alpha_config.src_pixel_alpha_en = pixel_alpha_en;
			alpha_config.src_glb_alpha_value =  vpstate->global_alpha;
			alpha_config.dst_glb_alpha_value = 0xff;
		}
		vop2_parse_alpha(&alpha_config, &alpha);

		offset = (i - 1) * 0x10;
		vop2_writel(vop2, src_color_ctrl_offset + offset, alpha.src_color_ctrl.val);
		vop2_writel(vop2, dst_color_ctrl_offset + offset, alpha.dst_color_ctrl.val);
		vop2_writel(vop2, src_alpha_ctrl_offset + offset, alpha.src_alpha_ctrl.val);
		vop2_writel(vop2, dst_alpha_ctrl_offset + offset, alpha.dst_alpha_ctrl.val);
	}

	/* Transfer pixel alpha value to next mix */
	alpha_config.src_premulti_en = true;
	alpha_config.dst_premulti_en = true;
	alpha_config.src_pixel_alpha_en = false;
	alpha_config.src_glb_alpha_value = 0xff;
	alpha_config.dst_glb_alpha_value = 0xff;
	vop2_parse_alpha(&alpha_config, &alpha);

	for (; i < vop2->data->nr_layers; i++) {
		offset = (i - 1) * 0x10;

		vop2_writel(vop2, src_color_ctrl_offset + offset, alpha.src_color_ctrl.val);
		vop2_writel(vop2, dst_color_ctrl_offset + offset, alpha.dst_color_ctrl.val);
		vop2_writel(vop2, src_alpha_ctrl_offset + offset, alpha.src_alpha_ctrl.val);
		vop2_writel(vop2, dst_alpha_ctrl_offset + offset, alpha.dst_alpha_ctrl.val);
	}

	if (vp_data->feature & (VOP_FEATURE_HDR10 | VOP_FEATURE_VIVID_HDR)) {
		src_color_ctrl_offset = ovl_regs->hdr_mix_regs->src_color_ctrl.offset;
		dst_color_ctrl_offset = ovl_regs->hdr_mix_regs->dst_color_ctrl.offset;
		src_alpha_ctrl_offset = ovl_regs->hdr_mix_regs->src_alpha_ctrl.offset;
		dst_alpha_ctrl_offset = ovl_regs->hdr_mix_regs->dst_alpha_ctrl.offset;

		if (bottom_layer_alpha_en || vp->hdr_en) {
			/* Transfer pixel alpha to hdr mix */
			alpha_config.src_premulti_en = premulti_en;
			alpha_config.dst_premulti_en = true;
			alpha_config.src_pixel_alpha_en = true;
			alpha_config.src_glb_alpha_value = 0xff;
			alpha_config.dst_glb_alpha_value = 0xff;
			vop2_parse_alpha(&alpha_config, &alpha);

			vop2_writel(vop2, src_color_ctrl_offset, alpha.src_color_ctrl.val);
			vop2_writel(vop2, dst_color_ctrl_offset, alpha.dst_color_ctrl.val);
			vop2_writel(vop2, src_alpha_ctrl_offset, alpha.src_alpha_ctrl.val);
			vop2_writel(vop2, dst_alpha_ctrl_offset, alpha.dst_alpha_ctrl.val);
		} else {
			vop2_writel(vop2, src_color_ctrl_offset, 0);
			vop2_writel(vop2, dst_color_ctrl_offset, 0);
			vop2_writel(vop2, src_alpha_ctrl_offset, 0);
			vop2_writel(vop2, dst_alpha_ctrl_offset, 0);
		}
	}

	bg_alpha_ctrl.bits.alpha_en = 0;
	VOP_MODULE_SET(vop2, vp, bg_mix_ctrl, bg_alpha_ctrl.val);
}

static u32 vop2_layer_cfg_update(struct vop2_layer *layer, u32 old_layer_cfg, u8 win_layer_id)
{
	const struct vop_reg *reg = &layer->regs->layer_sel;
	u32 mask = reg->mask;
	u32 shift = reg->shift;

	return (old_layer_cfg & ~(mask << shift)) | ((win_layer_id & mask) << shift);
}

static u16 vop2_calc_bg_ovl_and_port_mux(struct vop2_video_port *vp)
{
	struct vop2_video_port *prev_vp;
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	u16 port_mux_cfg = 0;
	u8 port_mux;
	u8 used_layers = 0;
	int i;

	for (i = 0; i < vop2_data->nr_vps - 1; i++) {
		prev_vp = &vop2->vps[i];
		used_layers += hweight32(prev_vp->win_mask);
		if (vop2->version == VOP_VERSION_RK3588) {
			if (vop2->vps[0].hdr10_at_splice_mode && i == 0)
				used_layers += 1;
			if (vop2->vps[0].hdr10_at_splice_mode && i == 1)
				used_layers -= 1;
		}
		/*
		 * when a window move from vp0 to vp1, or vp0 to vp2,
		 * it should flow these steps:
		 * (1) first commit, disable this windows on VP0,
		 *     keep the win_mask of VP0.
		 * (2) second commit, set this window to VP1, clear
		 *     the corresponding win_mask on VP0, and set the
		 *     corresponding win_mask on VP1.
		 *  This means we only know the decrease of the windows
		 *  number of VP0 until VP1 take it, so the port_mux of
		 *  VP0 should change at VP1's commit.
		 */
		if (used_layers == 0)
			port_mux = 8;
		else
			port_mux = used_layers - 1;

		port_mux_cfg |= port_mux << (prev_vp->id * 4);

		if (port_mux > vop2_data->nr_mixers)
			prev_vp->bg_ovl_dly = 0;
		else
			prev_vp->bg_ovl_dly = (vop2_data->nr_mixers - port_mux) << 1;
	}

	port_mux_cfg |= 7 << (4 * (vop2->data->nr_vps - 1));

	return port_mux_cfg;
}

static void vop2_setup_port_mux(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	u16 port_mux_cfg;

	port_mux_cfg = vop2_calc_bg_ovl_and_port_mux(vp);
	spin_lock(&vop2->reg_lock);
	if (vop2->port_mux_cfg != port_mux_cfg) {
		VOP_CTRL_SET(vop2, ovl_port_mux_cfg, port_mux_cfg);
		vp->skip_vsync = true;
		vop2_cfg_done(&vp->rockchip_crtc.crtc);
		vop2->port_mux_cfg = port_mux_cfg;
		vop2_wait_for_port_mux_done(vop2);
	}
	spin_unlock(&vop2->reg_lock);
}

static void vop2_setup_layer_mixer_for_vp(struct vop2_video_port *vp,
					  const struct vop2_zpos *vop2_zpos)
{
	struct vop2_video_port *prev_vp;
	struct vop2 *vop2 = vp->vop2;
	struct vop2_layer *layer = &vop2->layers[0];
	u8 port_id = vp->id;
	const struct vop2_zpos *zpos;
	struct vop2_win *win;
	u8 used_layers = 0;
	u8 layer_id, win_phys_id;
	u32 layer_cfg_reg_offset = layer->regs->layer_sel.offset;
	u8 nr_layers = vp->nr_layers;
	u32 old_layer_cfg = 0;
	u32 new_layer_cfg = 0;
	u32 atv_layer_cfg;
	int i;

	/*
	 * Win and layer must map one by one, if a win is selected
	 * by two layers, unexpected error may happen.
	 * So when we attach a new win to a layer, we also move the
	 * old win of the layer to the layer where the new win comes from.
	 *
	 */
	for (i = 0; i < port_id; i++) {
		prev_vp = &vop2->vps[i];
		used_layers += hweight32(prev_vp->win_mask);
	}

	old_layer_cfg = vop2->regsbak[layer_cfg_reg_offset >> 2];
	new_layer_cfg = old_layer_cfg;

	if (vp->hdr10_at_splice_mode)
		nr_layers *= 2;

	for (i = 0; i < nr_layers; i++) {
		layer = &vop2->layers[used_layers + i];
		zpos = &vop2_zpos[i];
		win = vop2_find_win_by_phys_id(vop2, zpos->win_phys_id);
		layer_id = win->layer_id;
		win_phys_id = layer->win_phys_id;
		VOP_CTRL_SET(vop2, win_vp_id[win->phys_id], port_id);
		new_layer_cfg = vop2_layer_cfg_update(layer, new_layer_cfg, win->layer_sel_id[vp->id]);
		win->layer_id = layer->id;
		layer->win_phys_id = win->phys_id;
		layer = &vop2->layers[layer_id];
		win = vop2_find_win_by_phys_id(vop2, win_phys_id);
		new_layer_cfg = vop2_layer_cfg_update(layer, new_layer_cfg, win->layer_sel_id[vp->id]);
		win->layer_id = layer_id;
		layer->win_phys_id = win_phys_id;
	}

	atv_layer_cfg = vop2_read_layer_cfg(vop2);
	if (new_layer_cfg != old_layer_cfg &&
	    atv_layer_cfg != old_layer_cfg &&
	    !vp->splice_mode_right) {
		dev_dbg(vop2->dev, "wait old_layer_sel: 0x%x\n", old_layer_cfg);
		vop2_wait_for_layer_cfg_done(vop2, old_layer_cfg);
	}
	vop2_writel(vop2, RK3568_OVL_LAYER_SEL, new_layer_cfg);
	if (new_layer_cfg != old_layer_cfg)
		VOP_CTRL_SET(vop2, ovl_cfg_done_port, vp->id);
	VOP_CTRL_SET(vop2, ovl_port_mux_cfg_done_imd, 0);
}

static void vop3_setup_layer_sel_for_vp(struct vop2_video_port *vp,
					const struct vop2_zpos *vop2_zpos)
{
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_zpos *zpos;
	struct vop2_win *win;
	u32 layer_sel = 0;
	u8 port_id = vp->id;
	u8 layer_sel_id;
	u8 layer_sel_none = 0xff;
	int i;

	for (i = 0; i < vop2->data->nr_layers; i++) {
		layer_sel_id = layer_sel_none;
		if (i < vp->nr_layers) {
			zpos = &vop2_zpos[i];
			win = vop2_find_win_by_phys_id(vop2, zpos->win_phys_id);
			if (win->old_vp_mask != win->vp_mask && VOP_WIN_GET(vop2, win, enable))
				DRM_ERROR("must wait %s disabled and change vp_mask[0x%x->0x%x]\n",
					  win->name, win->old_vp_mask, win->vp_mask);
			VOP_CTRL_SET(vop2, win_vp_id[win->phys_id], port_id);
			layer_sel_id = win->layer_sel_id[vp->id];
		}
		layer_sel |= layer_sel_id << i * 4;
	}
	VOP_MODULE_SET(vop2, vp, layer_sel, layer_sel);
}

/*
 * HDR window is fixed(not move in the overlay path with port_mux change)
 * and is the most slow window. And the bg is the fast. So other windows
 * and bg need to add delay number to keep align with the most slow window.
 * The delay number list in the trm is a relative value for port_mux set at
 * last level.
 */
static void vop2_setup_dly_for_vp(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	struct vop2_video_port *left_vp = vp->left_vp;
	struct drm_crtc *crtc = &vp->rockchip_crtc.crtc;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	u16 hsync_len = adjusted_mode->crtc_hsync_end - adjusted_mode->crtc_hsync_start;
	u16 hdisplay = adjusted_mode->crtc_hdisplay;
	u32 bg_dly = vp_data->pre_scan_max_dly[0];
	u32 pre_scan_dly;

	if (vp_data->hdr_table)  {
		if (vp->hdr_in) {
			if (vp->hdr_out)
				bg_dly = vp_data->pre_scan_max_dly[2];
		} else {
			if (vp->hdr_out)
				bg_dly = vp_data->pre_scan_max_dly[1];
			else
				bg_dly = vp_data->pre_scan_max_dly[3];
		}
	}

	if (!vp->hdr_in ||
	    (vop2->version == VOP_VERSION_RK3588 && vp->hdr_out))
		bg_dly -= vp->bg_ovl_dly;

	/*
	 * right vp share the same crtc state in splice mode
	 */
	if (vp->splice_mode_right) {
		vcstate = to_rockchip_crtc_state(left_vp->rockchip_crtc.crtc.state);
		adjusted_mode = &left_vp->rockchip_crtc.crtc.state->adjusted_mode;
		hsync_len = adjusted_mode->crtc_hsync_end - adjusted_mode->crtc_hsync_start;
		hdisplay = adjusted_mode->crtc_hdisplay;
	}

	if (vcstate->splice_mode)
		pre_scan_dly = bg_dly + (hdisplay >> 2) - 1;
	else
		pre_scan_dly = bg_dly + (hdisplay >> 1) - 1;

	if (vop2->version == VOP_VERSION_RK3588 && hsync_len < 8)
		hsync_len = 8;

	pre_scan_dly = (pre_scan_dly << 16) | hsync_len;

	VOP_MODULE_SET(vop2, vp, bg_dly, bg_dly);
	VOP_MODULE_SET(vop2, vp, pre_scan_htiming, pre_scan_dly);
}

static void vop2_setup_dly_for_window(struct vop2_video_port *vp, const struct vop2_zpos *vop2_zpos)
{
	struct vop2 *vop2 = vp->vop2;
	struct vop2_plane_state *vpstate;
	const struct vop2_zpos *zpos;
	struct drm_plane *plane;
	struct vop2_win *win;
	uint32_t dly;
	int i = 0;

	for (i = 0; i < vp->nr_layers; i++) {
		zpos = &vop2_zpos[i];
		win = vop2_find_win_by_phys_id(vop2, zpos->win_phys_id);
		/*
		 * right vp share the same plane state in splice mode
		 */
		if (vp->splice_mode_right) {
			plane = &win->left_win->base;
			vpstate = to_vop2_plane_state(plane->state);
		} else {
			plane = &win->base;
			vpstate = to_vop2_plane_state(plane->state);
		}

		if (vp->hdr_in && !vp->hdr_out && !vpstate->hdr_in) {
			dly = win->dly[VOP2_DLY_MODE_HISO_S];
			dly += vp->bg_ovl_dly;
		} else if (vp->hdr_in && vp->hdr_out && vpstate->hdr_in) {
			dly = win->dly[VOP2_DLY_MODE_HIHO_H];
			dly -= vp->bg_ovl_dly;
		} else {
			dly = win->dly[VOP2_DLY_MODE_DEFAULT];
		}
		if (vop2_cluster_window(win))
			dly |= dly << 8;

		VOP_CTRL_SET(vop2, win_dly[win->phys_id], dly);
	}
}

static void rk3588_vop2_setup_hdr10_splice_layer_mixer(struct drm_crtc *crtc,
						       struct vop2_zpos *vop2_zpos,
						       struct vop2_zpos *vop2_zpos_splice)
{
	int zpos_id, i;
	struct vop2_zpos *vop2_zpos_splice_hdr;
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	vop2_zpos_splice_hdr = kmalloc_array(vop2->data->win_size, sizeof(*vop2_zpos),
					 GFP_KERNEL);
	if (!vop2_zpos_splice_hdr)
		goto out;

	zpos_id = 0;
	vop2_zpos_splice_hdr[zpos_id].zpos = zpos_id;
	vop2_zpos_splice_hdr[zpos_id].win_phys_id = vop2_zpos[0].win_phys_id;
	vop2_zpos_splice_hdr[zpos_id].plane = vop2_zpos[0].plane;

	zpos_id++;
	vop2_zpos_splice_hdr[zpos_id].zpos = zpos_id;
	vop2_zpos_splice_hdr[zpos_id].win_phys_id = vop2_zpos_splice[0].win_phys_id;
	vop2_zpos_splice_hdr[zpos_id].plane = vop2_zpos_splice[0].plane;

	for (i = 1; i < vp->nr_layers; i++) {
		zpos_id++;
		vop2_zpos_splice_hdr[zpos_id].zpos = zpos_id;
		vop2_zpos_splice_hdr[zpos_id].win_phys_id = vop2_zpos[i].win_phys_id;
		vop2_zpos_splice_hdr[zpos_id].plane = vop2_zpos[i].plane;
	}

	for (i = 1; i < vp->nr_layers; i++) {
		zpos_id++;
		vop2_zpos_splice_hdr[zpos_id].zpos = zpos_id;
		vop2_zpos_splice_hdr[zpos_id].win_phys_id = vop2_zpos_splice[i].win_phys_id;
		vop2_zpos_splice_hdr[zpos_id].plane = vop2_zpos_splice[i].plane;
	}
	vop2_setup_layer_mixer_for_vp(vp, vop2_zpos_splice_hdr);

out:
	kfree(vop2_zpos_splice_hdr);
}

static void vop2_crtc_update_vrr(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct drm_display_mode *adjust_mode = &crtc->state->adjusted_mode;

	unsigned int vrefresh;
	unsigned int new_vtotal, vfp, new_vfp;

	if (!vp->refresh_rate_change)
		return;

	if (!vcstate->min_refresh_rate || !vcstate->max_refresh_rate)
		return;

	if (vcstate->request_refresh_rate < vcstate->min_refresh_rate ||
	    vcstate->request_refresh_rate > vcstate->max_refresh_rate) {
		DRM_ERROR("invalid rate:%d\n", vcstate->request_refresh_rate);
		return;
	}

	vrefresh = drm_mode_vrefresh(adjust_mode);

	/* calculate new vfp for new refresh rate */
	new_vtotal = adjust_mode->vtotal * vrefresh / vcstate->request_refresh_rate;
	vfp = adjust_mode->vsync_start -  adjust_mode->vdisplay;
	new_vfp = vfp + new_vtotal - adjust_mode->vtotal;

	/* config vop2 vtotal register */
	VOP_MODULE_SET(vop2, vp, dsp_vtotal, new_vtotal);

	/* config dsc vtotal register */
	if (vcstate->dsc_enable) {
		struct vop2_dsc *dsc;

		dsc = &vop2->dscs[vcstate->dsc_id];
		VOP_MODULE_SET(vop2, dsc, dsc_vtotal, new_vtotal);

		if (vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE) {
			dsc = &vop2->dscs[vcstate->dsc_id ? 0 : 1];
			VOP_MODULE_SET(vop2, dsc, dsc_vtotal, new_vtotal);
		}
	}

	/* config all connectors attach to this crtc */
	rockchip_connector_update_vfp_for_vrr(crtc, adjust_mode, new_vfp);
}

static void vop2_crtc_atomic_begin(struct drm_crtc *crtc, struct drm_crtc_state *old_crtc_state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_video_port_data *vp_data = &vop2->data->vp[vp->id];
	struct vop2_video_port *splice_vp = &vop2->vps[vp_data->splice_vp_id];
	struct drm_plane *plane;
	struct vop2_plane_state *vpstate;
	struct vop2_zpos *vop2_zpos;
	struct vop2_zpos *vop2_zpos_splice;
	struct vop2_cluster cluster;
	uint8_t nr_layers = 0;
	uint8_t splice_nr_layers = 0;
	bool hdr10_in = false;
	bool hdr10_at_splice_mode = false;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);

	vcstate->yuv_overlay = is_yuv_output(vcstate->bus_format);
	vop2_zpos = kmalloc_array(vop2->data->win_size, sizeof(*vop2_zpos), GFP_KERNEL);
	if (!vop2_zpos)
		return;
	if (vcstate->splice_mode) {
		vop2_zpos_splice = kmalloc_array(vop2->data->win_size, sizeof(*vop2_zpos),
						 GFP_KERNEL);
		if (!vop2_zpos_splice)
			goto out;
	}

	if (vop2->version == VOP_VERSION_RK3588)
		vop2_crtc_update_vrr(crtc);

	/* Process cluster sub windows overlay. */
	drm_atomic_crtc_for_each_plane(plane, crtc) {
		struct vop2_win *win = to_vop2_win(plane);
		struct vop2_win *main_win;

		win->two_win_mode = false;
		if (!(win->feature & WIN_FEATURE_CLUSTER_SUB))
			continue;
		if (vcstate->splice_mode)
			DRM_ERROR("vp%d %s not supported two win mode at splice mode\n",
				  vp->id, win->name);
		main_win = vop2_find_win_by_phys_id(vop2, win->phys_id);
		cluster.main = main_win;
		cluster.sub = win;
		cluster.splice_mode = false;
		win->two_win_mode = true;
		main_win->two_win_mode = true;
		vop2_setup_cluster_alpha(vop2, &cluster);
		if (abs(main_win->base.state->zpos - win->base.state->zpos) != 1)
			DRM_ERROR("vp%d Cluster%d win0[zpos:%d] must next to win1[zpos:%d]\n",
				  vp->id, cluster.main->phys_id,
				  main_win->base.state->zpos, win->base.state->zpos);
	}

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		struct vop2_win *win = to_vop2_win(plane);
		struct vop2_win *splice_win;
		struct vop2_video_port *old_vp;
		uint8_t old_vp_id;

		/*
		 * Sub win of a cluster will be handled by pre overlay module automatically
		 * win in multi area share the same overlay zorder with it's parent.
		 */
		if ((win->feature & WIN_FEATURE_CLUSTER_SUB) || win->parent)
			continue;
		old_vp_id = ffs(win->vp_mask);
		old_vp_id = (old_vp_id == 0) ? 0 : old_vp_id - 1;
		old_vp = &vop2->vps[old_vp_id];
		old_vp->win_mask &= ~BIT(win->phys_id);
		vp->win_mask |=  BIT(win->phys_id);
		win->vp_mask = BIT(vp->id);
		vpstate = to_vop2_plane_state(plane->state);
		vop2_zpos[nr_layers].win_phys_id = win->phys_id;
		vop2_zpos[nr_layers].zpos = vpstate->zpos;
		vop2_zpos[nr_layers].plane = plane;

		rockchip_drm_dbg(vop2->dev, VOP_DEBUG_OVERLAY, "%s active zpos:%d for vp%d from vp%d\n",
				 win->name, vpstate->zpos, vp->id, old_vp->id);
		/* left and right win may have different number */
		if (vcstate->splice_mode) {
			splice_win = vop2_find_win_by_phys_id(vop2, win->splice_win_id);
			splice_win->splice_mode_right = true;
			splice_win->left_win = win;
			win->splice_win = splice_win;

			old_vp_id = ffs(splice_win->vp_mask);
			old_vp_id = (old_vp_id == 0) ? 0 : old_vp_id - 1;
			old_vp = &vop2->vps[old_vp_id];
			old_vp->win_mask &= ~BIT(splice_win->phys_id);
			splice_vp->win_mask |=  BIT(splice_win->phys_id);
			splice_win->vp_mask = BIT(splice_vp->id);
			hdr10_in |= vpstate->eotf == HDMI_EOTF_SMPTE_ST2084 ? true : false;
			vop2_zpos_splice[splice_nr_layers].win_phys_id = splice_win->phys_id;
			vop2_zpos_splice[splice_nr_layers].zpos = vpstate->zpos;
			vop2_zpos_splice[splice_nr_layers].plane = &splice_win->base;
			splice_nr_layers++;
			DRM_DEV_DEBUG(vop2->dev, "%s active zpos:%d for vp%d from vp%d\n",
			     splice_win->name, vpstate->zpos, splice_vp->id, old_vp->id);
		}
		nr_layers++;
	}

	if (vcstate->splice_mode) {
		if (hdr10_in)
			hdr10_at_splice_mode = true;

		splice_vp->hdr10_at_splice_mode = hdr10_at_splice_mode;
	}
	vp->hdr10_at_splice_mode = hdr10_at_splice_mode;

	rockchip_drm_dbg(vop2->dev, VOP_DEBUG_OVERLAY, "vp%d: %d windows, active layers %d\n",
			 vp->id, hweight32(vp->win_mask), nr_layers);
	if (nr_layers) {
		vp->nr_layers = nr_layers;

		sort(vop2_zpos, nr_layers, sizeof(vop2_zpos[0]), vop2_zpos_cmp, NULL);

		if (!vp->hdr10_at_splice_mode) {
			if (is_vop3(vop2)) {
				vop3_setup_layer_sel_for_vp(vp, vop2_zpos);
			} else {
				vop2_setup_port_mux(vp);
				vop2_setup_layer_mixer_for_vp(vp, vop2_zpos);
			}
		}

		if (is_vop3(vop2)) {
			if (vp_data->feature & VOP_FEATURE_VIVID_HDR)
				vop3_setup_dynamic_hdr(vp, vop2_zpos[0].win_phys_id);
			vop3_setup_alpha(vp, vop2_zpos);
			vop3_setup_pipe_dly(vp, vop2_zpos);
		} else {
			vop2_setup_hdr10(vp, vop2_zpos[0].win_phys_id);
			vop2_setup_alpha(vp, vop2_zpos);
			vop2_setup_dly_for_vp(vp);
			vop2_setup_dly_for_window(vp, vop2_zpos);
		}

		if (vcstate->splice_mode) {/* Fixme for VOP3 8K */
			splice_vp->nr_layers = splice_nr_layers;

			sort(vop2_zpos_splice, splice_nr_layers, sizeof(vop2_zpos_splice[0]),
			     vop2_zpos_cmp, NULL);

			vop2_setup_port_mux(splice_vp);
			if (!vp->hdr10_at_splice_mode)
				vop2_setup_layer_mixer_for_vp(splice_vp, vop2_zpos_splice);
			vop2_setup_hdr10(splice_vp, vop2_zpos_splice[0].win_phys_id);
			vop2_setup_alpha(splice_vp, vop2_zpos_splice);
			vop2_setup_dly_for_vp(splice_vp);
			vop2_setup_dly_for_window(splice_vp, vop2_zpos_splice);

			if (vop2->version == VOP_VERSION_RK3588 &&
			    vp->hdr10_at_splice_mode)
				rk3588_vop2_setup_hdr10_splice_layer_mixer(crtc, vop2_zpos, vop2_zpos_splice);
		}
	} else {
		if (!is_vop3(vop2)) {
			vop2_calc_bg_ovl_and_port_mux(vp);
			vop2_setup_dly_for_vp(vp);
			if (vcstate->splice_mode)
				vop2_setup_dly_for_vp(splice_vp);
		} else {
			vop3_setup_pipe_dly(vp, NULL);
		}
	}

	/* The pre alpha overlay of Cluster still need process in one win mode. */
	drm_atomic_crtc_for_each_plane(plane, crtc) {
		struct vop2_win *win = to_vop2_win(plane);
		struct vop2_win *splice_win;

		if (!(win->feature & WIN_FEATURE_CLUSTER_MAIN))
			continue;
		if (win->two_win_mode)
			continue;
		cluster.main = win;
		cluster.sub = NULL;
		cluster.splice_mode = false;
		vop2_setup_cluster_alpha(vop2, &cluster);
		if (vcstate->splice_mode) {
			splice_win = win->splice_win;
			cluster.main = splice_win;
			cluster.splice_mode = true;
			vop2_setup_cluster_alpha(vop2, &cluster);
		}
	}

	if (vcstate->splice_mode)
		kfree(vop2_zpos_splice);
out:
	kfree(vop2_zpos);
}

static void vop2_bcsh_reg_update(struct rockchip_crtc_state *vcstate,
				 struct vop2_video_port *vp,
				 struct rockchip_bcsh_state *bcsh_state)
{
	struct vop2 *vop2 = vp->vop2;

	VOP_MODULE_SET(vop2, vp, bcsh_r2y_en, vcstate->post_r2y_en);
	VOP_MODULE_SET(vop2, vp, bcsh_y2r_en, vcstate->post_y2r_en);
	VOP_MODULE_SET(vop2, vp, bcsh_r2y_csc_mode, vcstate->post_csc_mode);
	VOP_MODULE_SET(vop2, vp, bcsh_y2r_csc_mode, vcstate->post_csc_mode);
	if (!vcstate->bcsh_en) {
		VOP_MODULE_SET(vop2, vp, bcsh_en, vcstate->bcsh_en);
		return;
	}

	VOP_MODULE_SET(vop2, vp, bcsh_brightness, bcsh_state->brightness);
	VOP_MODULE_SET(vop2, vp, bcsh_contrast, bcsh_state->contrast);
	VOP_MODULE_SET(vop2, vp, bcsh_sat_con,
		       bcsh_state->saturation * bcsh_state->contrast / 0x100);
	VOP_MODULE_SET(vop2, vp, bcsh_sin_hue, bcsh_state->sin_hue);
	VOP_MODULE_SET(vop2, vp, bcsh_cos_hue, bcsh_state->cos_hue);
	VOP_MODULE_SET(vop2, vp, bcsh_out_mode, BCSH_OUT_MODE_NORMAL_VIDEO);
	VOP_MODULE_SET(vop2, vp, bcsh_en, vcstate->bcsh_en);
}

static void vop2_tv_config_update(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_crtc_state)
{
	struct rockchip_crtc_state *vcstate =
			to_rockchip_crtc_state(crtc->state);
	struct rockchip_crtc_state *old_vcstate =
			to_rockchip_crtc_state(old_crtc_state);
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	int brightness, contrast, saturation, hue, sin_hue, cos_hue;
	struct rockchip_bcsh_state bcsh_state;

	if (!vcstate->tv_state)
		return;

	/* post BCSH CSC */
	vcstate->post_r2y_en = 0;
	vcstate->post_y2r_en = 0;
	vcstate->bcsh_en = 0;
	if (vcstate->tv_state->brightness != 50 ||
	    vcstate->tv_state->contrast != 50 ||
	    vcstate->tv_state->saturation != 50 || vcstate->tv_state->hue != 50)
		vcstate->bcsh_en = 1;
	/*
	 * The BCSH only need to config once except one of the following
	 * condition changed:
	 *   1. tv_state: include brightness,contrast,saturation and hue;
	 *   2. yuv_overlay: it is related to BCSH r2y module;
	 *   4. bcsh_en: control the BCSH module enable or disable state;
	 *   5. bus_format: it is related to BCSH y2r module;
	 */
	if (!memcmp(vcstate->tv_state, &vp->active_tv_state, sizeof(*vcstate->tv_state)) &&
	    vcstate->yuv_overlay == old_vcstate->yuv_overlay &&
	    vcstate->bcsh_en == old_vcstate->bcsh_en &&
	    vcstate->bus_format == old_vcstate->bus_format)
		return;

	memcpy(&vp->active_tv_state, vcstate->tv_state, sizeof(*vcstate->tv_state));
	if (vcstate->bcsh_en) {
		if (!vcstate->yuv_overlay)
			vcstate->post_r2y_en = 1;
		if (!is_yuv_output(vcstate->bus_format))
			vcstate->post_y2r_en = 1;
	} else {
		if (!vcstate->yuv_overlay && is_yuv_output(vcstate->bus_format))
			vcstate->post_r2y_en = 1;
		if (vcstate->yuv_overlay && !is_yuv_output(vcstate->bus_format))
			vcstate->post_y2r_en = 1;
	}

	vcstate->post_csc_mode = vop2_convert_csc_mode(vcstate->color_space, CSC_10BIT_DEPTH);

	if (vp_data->feature & VOP_FEATURE_OUTPUT_10BIT)
		brightness = interpolate(0, -128, 100, 127,
					 vcstate->tv_state->brightness);
	else
		brightness = interpolate(0, -32, 100, 31,
					 vcstate->tv_state->brightness);
	contrast = interpolate(0, 0, 100, 511, vcstate->tv_state->contrast);
	saturation = interpolate(0, 0, 100, 511, vcstate->tv_state->saturation);
	hue = interpolate(0, -30, 100, 30, vcstate->tv_state->hue);

	/*
	 *  a:[-30~0]:
	 *    sin_hue = 0x100 - sin(a)*256;
	 *    cos_hue = cos(a)*256;
	 *  a:[0~30]
	 *    sin_hue = sin(a)*256;
	 *    cos_hue = cos(a)*256;
	 */
	sin_hue = fixp_sin32(hue) >> 23;
	cos_hue = fixp_cos32(hue) >> 23;

	bcsh_state.brightness = brightness;
	bcsh_state.contrast = contrast;
	bcsh_state.saturation = saturation;
	bcsh_state.sin_hue = sin_hue;
	bcsh_state.cos_hue = cos_hue;

	vop2_bcsh_reg_update(vcstate, vp, &bcsh_state);
	if (vcstate->splice_mode) {
		const struct vop2_video_port_data *vp_data = &vop2->data->vp[vp->id];
		struct vop2_video_port *splice_vp = &vop2->vps[vp_data->splice_vp_id];

		vop2_bcsh_reg_update(vcstate, splice_vp, &bcsh_state);
	}
}

static void vop3_post_csc_config(struct drm_crtc *crtc, struct post_acm *acm, struct post_csc *csc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2 *vop2 = vp->vop2;
	struct post_csc_coef csc_coef;
	bool acm_enable;
	bool is_input_yuv = false;
	bool is_output_yuv = false;
	bool post_r2y_en = false;
	bool post_csc_en = false;
	int range_type;

	if (!acm)
		acm_enable = false;
	else
		acm_enable = acm->acm_enable;

	if (acm_enable) {
		if (!vcstate->yuv_overlay)
			post_r2y_en = true;

		/* do y2r in csc module */
		if (!is_yuv_output(vcstate->bus_format))
			post_csc_en = true;
	} else {
		if (!vcstate->yuv_overlay && is_yuv_output(vcstate->bus_format))
			post_r2y_en = true;

		/* do y2r in csc module */
		if (vcstate->yuv_overlay && !is_yuv_output(vcstate->bus_format))
			post_csc_en = true;
	}

	if (csc && csc->csc_enable)
		post_csc_en = true;

	if (vcstate->yuv_overlay || post_r2y_en)
		is_input_yuv = true;

	if (is_yuv_output(vcstate->bus_format))
		is_output_yuv = true;

	vcstate->post_csc_mode = vop2_convert_csc_mode(vcstate->color_space, CSC_13BIT_DEPTH);

	if (post_csc_en) {
		rockchip_calc_post_csc(csc, &csc_coef, vcstate->post_csc_mode, is_input_yuv,
				       is_output_yuv);

		VOP_MODULE_SET(vop2, vp, csc_coe00, csc_coef.csc_coef00);
		VOP_MODULE_SET(vop2, vp, csc_coe01, csc_coef.csc_coef01);
		VOP_MODULE_SET(vop2, vp, csc_coe02, csc_coef.csc_coef02);
		VOP_MODULE_SET(vop2, vp, csc_coe10, csc_coef.csc_coef10);
		VOP_MODULE_SET(vop2, vp, csc_coe11, csc_coef.csc_coef11);
		VOP_MODULE_SET(vop2, vp, csc_coe12, csc_coef.csc_coef12);
		VOP_MODULE_SET(vop2, vp, csc_coe20, csc_coef.csc_coef20);
		VOP_MODULE_SET(vop2, vp, csc_coe21, csc_coef.csc_coef21);
		VOP_MODULE_SET(vop2, vp, csc_coe22, csc_coef.csc_coef22);
		VOP_MODULE_SET(vop2, vp, csc_offset0, csc_coef.csc_dc0);
		VOP_MODULE_SET(vop2, vp, csc_offset1, csc_coef.csc_dc1);
		VOP_MODULE_SET(vop2, vp, csc_offset2, csc_coef.csc_dc2);

		range_type = csc_coef.range_type ? 0 : 1;
		range_type <<= is_input_yuv ? 0 : 1;
		VOP_MODULE_SET(vop2, vp, csc_mode, range_type);
	}

	VOP_MODULE_SET(vop2, vp, acm_r2y_en, post_r2y_en ? 1 : 0);
	VOP_MODULE_SET(vop2, vp, csc_en, post_csc_en ? 1 : 0);
	VOP_MODULE_SET(vop2, vp, acm_r2y_mode, vcstate->post_csc_mode);
}

static void vop3_post_acm_config(struct drm_crtc *crtc, struct post_acm *acm)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	s16 *lut_y;
	s16 *lut_h;
	s16 *lut_s;
	u32 value;
	int i;

	if (!acm) {
		writel(0x2, vop2->acm_regs + RK3528_ACM_CTRL);
		VOP_MODULE_SET(vop2, vp, acm_bypass_en, 1);
		return;
	}

	writel(1, vop2->acm_regs + RK3528_ACM_FETCH_START);

	value = (acm->acm_enable & 0x1) + ((adjusted_mode->hdisplay & 0xfff) << 8) +
		((adjusted_mode->vdisplay & 0xfff) << 20);
	writel(value, vop2->acm_regs + RK3528_ACM_CTRL);
	VOP_MODULE_SET(vop2, vp, acm_bypass_en, acm->acm_enable ? 0 : 1);

	value = (acm->y_gain & 0x3ff) + ((acm->h_gain << 10) & 0xffc00) +
		((acm->s_gain << 20) & 0x3ff00000);
	writel(value, vop2->acm_regs + RK3528_ACM_DELTA_RANGE);

	lut_y = &acm->gain_lut_hy[0];
	lut_h = &acm->gain_lut_hy[ACM_GAIN_LUT_HY_LENGTH];
	lut_s = &acm->gain_lut_hy[ACM_GAIN_LUT_HY_LENGTH * 2];
	for (i = 0; i < ACM_GAIN_LUT_HY_LENGTH; i++) {
		value = (lut_y[i] & 0xff) + ((lut_h[i] << 8) & 0xff00) +
			((lut_s[i] << 16) & 0xff0000);
		writel(value, vop2->acm_regs + RK3528_ACM_YHS_DEL_HY_SEG0 + (i << 2));
	}

	lut_y = &acm->gain_lut_hs[0];
	lut_h = &acm->gain_lut_hs[ACM_GAIN_LUT_HS_LENGTH];
	lut_s = &acm->gain_lut_hs[ACM_GAIN_LUT_HS_LENGTH * 2];
	for (i = 0; i < ACM_GAIN_LUT_HS_LENGTH; i++) {
		value = (lut_y[i] & 0xff) + ((lut_h[i] << 8) & 0xff00) +
			((lut_s[i] << 16) & 0xff0000);
		writel(value, vop2->acm_regs + RK3528_ACM_YHS_DEL_HS_SEG0 + (i << 2));
	}

	lut_y = &acm->delta_lut_h[0];
	lut_h = &acm->delta_lut_h[ACM_DELTA_LUT_H_LENGTH];
	lut_s = &acm->delta_lut_h[ACM_DELTA_LUT_H_LENGTH * 2];
	for (i = 0; i < ACM_DELTA_LUT_H_LENGTH; i++) {
		value = (lut_y[i] & 0x3ff) + ((lut_h[i] << 12) & 0xff000) +
			((lut_s[i] << 20) & 0x3ff00000);
		writel(value, vop2->acm_regs + RK3528_ACM_YHS_DEL_HGAIN_SEG0 + (i << 2));
	}

	writel(1, vop2->acm_regs + RK3528_ACM_FETCH_DONE);
}

static void vop3_post_config(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct post_acm *acm;
	struct post_csc *csc;

	acm = vcstate->acm_lut_data ? (struct post_acm *)vcstate->acm_lut_data->data : NULL;
	vop3_post_acm_config(crtc, acm);

	csc = vcstate->post_csc_data ? (struct post_csc *)vcstate->post_csc_data->data : NULL;
	vop3_post_csc_config(crtc, acm, csc);
}

static void vop2_cfg_update(struct drm_crtc *crtc,
			    struct drm_crtc_state *old_crtc_state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	struct vop2_video_port *splice_vp = &vop2->vps[vp_data->splice_vp_id];
	uint32_t val;
	uint32_t r, g, b;
	uint8_t out_mode;

	spin_lock(&vop2->reg_lock);

	if ((vcstate->output_mode == ROCKCHIP_OUT_MODE_AAAA &&
	     !(vp_data->feature & VOP_FEATURE_OUTPUT_10BIT)) ||
	    vcstate->output_if & VOP_OUTPUT_IF_BT656)
		out_mode = ROCKCHIP_OUT_MODE_P888;
	else
		out_mode = vcstate->output_mode;
	VOP_MODULE_SET(vop2, vp, out_mode, out_mode);

	vop2_post_color_swap(crtc);

	vop2_dither_setup(crtc);

	VOP_MODULE_SET(vop2, vp, overlay_mode, vcstate->yuv_overlay);

	/*
	 * userspace specified background.
	 */
	if (vcstate->background) {
		r = (vcstate->background & 0xff0000) >> 16;
		g = (vcstate->background & 0xff00) >> 8;
		b = (vcstate->background & 0xff);
		r <<= 2;
		g <<= 2;
		b <<= 2;
		val = (r << 20) | (g << 10) | b;
	} else {
		if (vcstate->yuv_overlay)
			val = 0x20010200;
		else
			val = 0;
	}

	VOP_MODULE_SET(vop2, vp, dsp_background, val);
	if (vcstate->splice_mode) {
		VOP_MODULE_SET(vop2, splice_vp, overlay_mode, vcstate->yuv_overlay);
		VOP_MODULE_SET(vop2, splice_vp, dsp_background, val);
	}

	vop2_tv_config_update(crtc, old_crtc_state);

	if (vp_data->feature & VOP_FEATURE_OVERSCAN)
		vop2_post_config(crtc);

	if (vp_data->feature & (VOP_FEATURE_POST_ACM | VOP_FEATURE_POST_CSC))
		vop3_post_config(crtc);

	spin_unlock(&vop2->reg_lock);
}

static void vop2_sleep_scan_line_time(struct vop2_video_port *vp, int scan_line)
{
	struct vop2 *vop2 = vp->vop2;
	struct drm_display_mode *mode = &vp->rockchip_crtc.crtc.state->adjusted_mode;

	if (scan_line <= 0)
		return;

	if (IS_ENABLED(CONFIG_HIGH_RES_TIMERS) &&
	    (!IS_ENABLED(CONFIG_NO_GKI) || (hrtimer_resolution != LOW_RES_NSEC))) {
		u16 htotal = VOP_MODULE_GET(vop2, vp, htotal_pw) >> 16;
		u32 linedur_ns = div_u64((u64) htotal * 1000000, mode->crtc_clock);
		u64 sleep_time = linedur_ns * scan_line;

		sleep_time = div_u64((sleep_time + 1000), 1000);
		if (sleep_time > 200)
			usleep_range(sleep_time, sleep_time);
	}
}

/*
 * return scan timing from FS to the assigned wait line
 */
static void vop2_wait_for_scan_timing_max_to_assigned_line(struct vop2_video_port *vp,
							   u32 current_line,
							   u32 wait_line)

{
	struct vop2 *vop2 = vp->vop2;
	u32 vcnt;
	int ret;
	u16 vtotal = VOP_MODULE_GET(vop2, vp, dsp_vtotal);
	int delta_line = vtotal - current_line;

	vop2_sleep_scan_line_time(vp, delta_line);
	if (vop2_read_vcnt(vp) < wait_line)
		return;

	ret = readx_poll_timeout_atomic(vop2_read_vcnt, vp, vcnt, vcnt < wait_line, 0, 50 * 1000);
	if (ret)
		DRM_DEV_ERROR(vop2->dev, "wait scan timing from FS to the assigned wait line: %d, vcnt:%d, ret:%d\n",
			      wait_line, vcnt, ret);
}

/*
 * return scan timing from the assigned wait line
 */
static void vop2_wait_for_scan_timing_from_the_assigned_line(struct vop2_video_port *vp,
							     u32 current_line,
							     u32 wait_line)
{
	struct vop2 *vop2 = vp->vop2;
	u32 vcnt;
	int ret;
	int delta_line = wait_line - current_line;

	vop2_sleep_scan_line_time(vp, delta_line);
	if (vop2_read_vcnt(vp) > wait_line)
		return;

	ret = readx_poll_timeout_atomic(vop2_read_vcnt, vp, vcnt, vcnt > wait_line, 0, 50 * 1000);
	if (ret)
		DRM_DEV_ERROR(vop2->dev, "wait scan timing from the assigned wait line: %d, vcnt:%d, ret:%d\n",
			      wait_line, vcnt, ret);
}

static void vop2_crtc_atomic_flush(struct drm_crtc *crtc, struct drm_crtc_state *old_cstate)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct drm_atomic_state *old_state = old_cstate->state;
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct drm_plane_state *old_pstate;
	struct drm_plane *plane;
	unsigned long flags;
	int i, ret;
	struct vop2_wb *wb = &vop2->wb;
	struct drm_writeback_connector *wb_conn = &wb->conn;
	struct drm_connector_state *conn_state = wb_conn->base.state;

	if (conn_state && conn_state->writeback_job && conn_state->writeback_job->fb) {
		u16 vtotal = VOP_MODULE_GET(vop2, vp, dsp_vtotal);
		u32 current_line = vop2_read_vcnt(vp);

		if (current_line > vtotal * 7 >> 3)
			vop2_wait_for_scan_timing_max_to_assigned_line(vp, current_line, vtotal * 7 >> 3);

		current_line = vop2_read_vcnt(vp);
		if (current_line < vtotal >> 3)
			vop2_wait_for_scan_timing_from_the_assigned_line(vp, current_line, vtotal >> 3);
	}

	vop2_cfg_update(crtc, old_cstate);

	if (!vop2->is_iommu_enabled && vop2->is_iommu_needed) {
		if (vcstate->mode_update)
			VOP_CTRL_SET(vop2, dma_stop, 1);

		ret = rockchip_drm_dma_attach_device(vop2->drm_dev, vop2->dev);
		if (ret) {
			vop2->is_iommu_enabled = false;
			vop2_disable_all_planes_for_crtc(crtc);
			DRM_DEV_ERROR(vop2->dev, "vp%d failed to attach dma mapping, %d\n", vp->id, ret);
		} else {
			vop2->is_iommu_enabled = true;
			VOP_CTRL_SET(vop2, dma_stop, 0);
		}
	}


	if (crtc->state->color_mgmt_changed || crtc->state->active_changed) {
		if (crtc->state->gamma_lut || vp->gamma_lut) {
			if (crtc->state->gamma_lut)
				vp->gamma_lut = crtc->state->gamma_lut->data;
			vop2_crtc_atomic_gamma_set(crtc, crtc->state);
		}
		if (vcstate->cubic_lut_data || vp->cubic_lut) {
			if (vcstate->cubic_lut_data)
				vp->cubic_lut = vcstate->cubic_lut_data->data;
			vop2_crtc_atomic_cubic_lut_set(crtc, crtc->state);
		}
	} else {
		VOP_MODULE_SET(vop2, vp, cubic_lut_update_en, 0);
	}

	if (vcstate->line_flag)
		vop2_crtc_enable_line_flag_event(crtc, vcstate->line_flag);
	else
		vop2_crtc_disable_line_flag_event(crtc);

	spin_lock_irqsave(&vop2->irq_lock, flags);
	vop2_wb_commit(crtc);
	vop2_cfg_done(crtc);

	if (vp->mcu_timing.mcu_pix_total)
		VOP_MODULE_SET(vop2, vp, mcu_hold_mode, 0);

	spin_unlock_irqrestore(&vop2->irq_lock, flags);

	/*
	 * There is a (rather unlikely) possibility that a vblank interrupt
	 * fired before we set the cfg_done bit. To avoid spuriously
	 * signalling flip completion we need to wait for it to finish.
	 */
	vop2_wait_for_irq_handler(crtc);

	/**
	 * move here is to make sure current fs call function is complete,
	 * so when layer_sel_update is true, we can skip current vblank correctly.
	 */
	vp->layer_sel_update = false;

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);
		WARN_ON(vp->event);

		vp->event = crtc->state->event;
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	for_each_old_plane_in_state(old_state, plane, old_pstate, i) {
		if (!old_pstate->fb)
			continue;

		if (old_pstate->fb == plane->state->fb)
			continue;
		if (!vop2->skip_ref_fb)
			drm_framebuffer_get(old_pstate->fb);
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);
		drm_flip_work_queue(&vp->fb_unref_work, old_pstate->fb);
		set_bit(VOP_PENDING_FB_UNREF, &vp->pending);
	}
}

static const struct drm_crtc_helper_funcs vop2_crtc_helper_funcs = {
	.mode_valid = vop2_crtc_mode_valid,
	.mode_fixup = vop2_crtc_mode_fixup,
	.atomic_check = vop2_crtc_atomic_check,
	.atomic_begin = vop2_crtc_atomic_begin,
	.atomic_flush = vop2_crtc_atomic_flush,
	.atomic_enable = vop2_crtc_atomic_enable,
	.atomic_disable = vop2_crtc_atomic_disable,
};

static void vop2_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
}

static void vop2_crtc_reset(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);

	if (crtc->state) {
		__drm_atomic_helper_crtc_destroy_state(crtc->state);
		kfree(vcstate);
	}

	vcstate = kzalloc(sizeof(*vcstate), GFP_KERNEL);
	if (!vcstate)
		return;
	crtc->state = &vcstate->base;
	crtc->state->crtc = crtc;

	vcstate->left_margin = 100;
	vcstate->right_margin = 100;
	vcstate->top_margin = 100;
	vcstate->bottom_margin = 100;
	vcstate->background = 0;
}

static struct drm_crtc_state *vop2_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *vcstate, *old_vcstate;
	struct vop2_video_port *vp = to_vop2_video_port(crtc);

	if (WARN_ON(!crtc->state))
		return NULL;

	old_vcstate = to_rockchip_crtc_state(crtc->state);
	vcstate = kmemdup(old_vcstate, sizeof(*old_vcstate), GFP_KERNEL);
	if (!vcstate)
		return NULL;

	vcstate->vp_id = vp->id;
	if (vcstate->hdr_ext_data)
		drm_property_blob_get(vcstate->hdr_ext_data);
	if (vcstate->acm_lut_data)
		drm_property_blob_get(vcstate->acm_lut_data);
	if (vcstate->post_csc_data)
		drm_property_blob_get(vcstate->post_csc_data);
	if (vcstate->cubic_lut_data)
		drm_property_blob_get(vcstate->cubic_lut_data);

	__drm_atomic_helper_crtc_duplicate_state(crtc, &vcstate->base);
	return &vcstate->base;
}

static void vop2_crtc_destroy_state(struct drm_crtc *crtc,
				    struct drm_crtc_state *state)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(state);

	__drm_atomic_helper_crtc_destroy_state(&vcstate->base);
	drm_property_blob_put(vcstate->hdr_ext_data);
	drm_property_blob_put(vcstate->acm_lut_data);
	drm_property_blob_put(vcstate->post_csc_data);
	drm_property_blob_put(vcstate->cubic_lut_data);
	kfree(vcstate);
}

#ifdef CONFIG_DRM_ANALOGIX_DP
static struct drm_connector *vop2_get_edp_connector(struct vop2 *vop2)
{
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(vop2->drm_dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->connector_type == DRM_MODE_CONNECTOR_eDP) {
			drm_connector_list_iter_end(&conn_iter);
			return connector;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	return NULL;
}

static int vop2_crtc_set_crc_source(struct drm_crtc *crtc,
				    const char *source_name)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct drm_connector *connector;
	int ret;

	connector = vop2_get_edp_connector(vop2);
	if (!connector)
		return -EINVAL;

	if (source_name && strcmp(source_name, "auto") == 0)
		ret = analogix_dp_start_crc(connector);
	else if (!source_name)
		ret = analogix_dp_stop_crc(connector);
	else
		ret = -EINVAL;

	return ret;
}

static int vop2_crtc_verify_crc_source(struct drm_crtc *crtc, const char *source_name,
				       size_t *values_cnt)
{
	if (source_name && strcmp(source_name, "auto") != 0)
		return -EINVAL;

	*values_cnt = 3;
	return 0;
}

#else
static int vop2_crtc_set_crc_source(struct drm_crtc *crtc,
				    const char *source_name)
{
	return -ENODEV;
}

static int
vop2_crtc_verify_crc_source(struct drm_crtc *crtc, const char *source_name,
			    size_t *values_cnt)
{
	return -ENODEV;
}
#endif

static int vop2_crtc_atomic_get_property(struct drm_crtc *crtc,
					 const struct drm_crtc_state *state,
					 struct drm_property *property,
					 uint64_t *val)
{
	struct drm_device *drm_dev = crtc->dev;
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(state);
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	if (property == mode_config->tv_left_margin_property) {
		*val = vcstate->left_margin;
		return 0;
	}

	if (property == mode_config->tv_right_margin_property) {
		*val = vcstate->right_margin;
		return 0;
	}

	if (property == mode_config->tv_top_margin_property) {
		*val = vcstate->top_margin;
		return 0;
	}

	if (property == mode_config->tv_bottom_margin_property) {
		*val = vcstate->bottom_margin;
		return 0;
	}

	if (property == private->aclk_prop) {
		/* KHZ, keep align with mode->clock */
		*val = clk_get_rate(vop2->aclk) / 1000;
		return 0;
	}

	if (property == private->bg_prop) {
		*val = vcstate->background;
		return 0;
	}

	if (property == private->line_flag_prop) {
		*val = vcstate->line_flag;
		return 0;
	}

	if (property == vp->variable_refresh_rate_prop) {
		*val = vcstate->request_refresh_rate;
		return 0;
	}

	if (property == vp->max_refresh_rate_prop) {
		*val = vcstate->max_refresh_rate;
		return 0;
	}

	if (property == vp->min_refresh_rate_prop) {
		*val = vcstate->min_refresh_rate;
		return 0;
	}

	if (property == vp->hdr_ext_data_prop) {
		*val = vcstate->hdr_ext_data ? vcstate->hdr_ext_data->base.id : 0;
		return 0;
	}

	if (property == vp->acm_lut_data_prop) {
		*val = vcstate->acm_lut_data ? vcstate->acm_lut_data->base.id : 0;
		return 0;
	}

	if (property == vp->post_csc_data_prop) {
		*val = vcstate->post_csc_data ? vcstate->post_csc_data->base.id : 0;
		return 0;
	}

	if (property == private->cubic_lut_prop) {
		*val = (vcstate->cubic_lut_data) ? vcstate->cubic_lut_data->base.id : 0;
		return 0;
	}

	DRM_ERROR("failed to get vop2 crtc property: %s\n", property->name);

	return -EINVAL;
}

/* copied from drm_atomic.c */
static int
vop2_atomic_replace_property_blob_from_id(struct drm_device *dev,
					 struct drm_property_blob **blob,
					 uint64_t blob_id,
					 ssize_t expected_size,
					 ssize_t expected_elem_size,
					 bool *replaced)
{
	struct drm_property_blob *new_blob = NULL;

	if (blob_id != 0) {
		new_blob = drm_property_lookup_blob(dev, blob_id);
		if (new_blob == NULL)
			return -EINVAL;

		if (expected_size > 0 &&
		    new_blob->length != expected_size) {
			drm_property_blob_put(new_blob);
			return -EINVAL;
		}
		if (expected_elem_size > 0 &&
		    new_blob->length % expected_elem_size != 0) {
			drm_property_blob_put(new_blob);
			return -EINVAL;
		}
	}

	*replaced |= drm_property_replace_blob(blob, new_blob);
	drm_property_blob_put(new_blob);

	return 0;
}

static int vop2_crtc_atomic_set_property(struct drm_crtc *crtc,
					 struct drm_crtc_state *state,
					 struct drm_property *property,
					 uint64_t val)
{
	struct drm_device *drm_dev = crtc->dev;
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(state);
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	bool replaced = false;
	int ret;

	if (property == mode_config->tv_left_margin_property) {
		vcstate->left_margin = val;
		return 0;
	}

	if (property == mode_config->tv_right_margin_property) {
		vcstate->right_margin = val;
		return 0;
	}

	if (property == mode_config->tv_top_margin_property) {
		vcstate->top_margin = val;
		return 0;
	}

	if (property == mode_config->tv_bottom_margin_property) {
		vcstate->bottom_margin = val;
		return 0;
	}


	if (property == private->bg_prop) {
		vcstate->background = val;
		return 0;
	}

	if (property == private->line_flag_prop) {
		vcstate->line_flag = val;
		return 0;
	}

	if (property == vp->variable_refresh_rate_prop) {
		vcstate->request_refresh_rate = val;
		return 0;
	}

	if (property == vp->max_refresh_rate_prop) {
		vcstate->max_refresh_rate = val;
		return 0;
	}

	if (property == vp->min_refresh_rate_prop) {
		vcstate->min_refresh_rate = val;
		return 0;
	}

	if (property == vp->hdr_ext_data_prop) {
		ret = vop2_atomic_replace_property_blob_from_id(drm_dev,
								&vcstate->hdr_ext_data,
								val,
								-1, -1,
								&replaced);
		return ret;
	}

	if (property == vp->acm_lut_data_prop) {
		ret = vop2_atomic_replace_property_blob_from_id(drm_dev,
								&vcstate->acm_lut_data,
								val,
								sizeof(struct post_acm), -1,
								&replaced);
		return ret;
	}

	if (property == vp->post_csc_data_prop) {
		ret = vop2_atomic_replace_property_blob_from_id(drm_dev,
								&vcstate->post_csc_data,
								val,
								sizeof(struct post_csc), -1,
								&replaced);
		return ret;
	}

	if (property == private->cubic_lut_prop) {
		ret = vop2_atomic_replace_property_blob_from_id(drm_dev,
								&vcstate->cubic_lut_data,
								val,
								-1, sizeof(struct drm_color_lut),
								&replaced);
		state->color_mgmt_changed |= replaced;
		return ret;
	}

	DRM_ERROR("failed to set vop2 crtc property %s\n", property->name);

	return -EINVAL;
}

static const struct drm_crtc_funcs vop2_crtc_funcs = {
	.gamma_set = vop2_crtc_legacy_gamma_set,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.destroy = vop2_crtc_destroy,
	.reset = vop2_crtc_reset,
	.atomic_get_property = vop2_crtc_atomic_get_property,
	.atomic_set_property = vop2_crtc_atomic_set_property,
	.atomic_duplicate_state = vop2_crtc_duplicate_state,
	.atomic_destroy_state = vop2_crtc_destroy_state,
	.enable_vblank = vop2_crtc_enable_vblank,
	.disable_vblank = vop2_crtc_disable_vblank,
	.set_crc_source = vop2_crtc_set_crc_source,
	.verify_crc_source = vop2_crtc_verify_crc_source,
};

static void vop2_fb_unref_worker(struct drm_flip_work *work, void *val)
{
	struct vop2_video_port *vp = container_of(work, struct vop2_video_port, fb_unref_work);
	struct drm_framebuffer *fb = val;

	drm_crtc_vblank_put(&vp->rockchip_crtc.crtc);
	if (!vp->vop2->skip_ref_fb)
		drm_framebuffer_put(fb);
}

static void vop2_handle_vblank(struct vop2 *vop2, struct drm_crtc *crtc)
{
	struct drm_device *drm = vop2->drm_dev;
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	unsigned long flags;

	spin_lock_irqsave(&drm->event_lock, flags);
	if (vp->event) {
		drm_crtc_send_vblank_event(crtc, vp->event);
		drm_crtc_vblank_put(crtc);
		vp->event = NULL;
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);

	if (test_and_clear_bit(VOP_PENDING_FB_UNREF, &vp->pending))
		drm_flip_work_commit(&vp->fb_unref_work, system_unbound_wq);
}

static void vop2_handle_vcnt(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct rockchip_drm_private *priv = dev->dev_private;
	struct rockchip_drm_vcnt *vcnt;
	struct drm_pending_vblank_event *e;
	struct timespec64 now;
	unsigned long irqflags;
	int pipe;

	now = ktime_to_timespec64(ktime_get());

	spin_lock_irqsave(&dev->event_lock, irqflags);
	pipe = drm_crtc_index(crtc);
	vcnt = &priv->vcnt[pipe];
	vcnt->sequence++;
	if (vcnt->event) {
		e = vcnt->event;
		e->event.vbl.tv_sec = now.tv_sec;
		e->event.vbl.tv_usec = now.tv_nsec / NSEC_PER_USEC;
		e->event.vbl.sequence = vcnt->sequence;
		drm_send_event_locked(dev, &e->base);
		vcnt->event = NULL;
	}
	spin_unlock_irqrestore(&dev->event_lock, irqflags);
}

static u32 vop2_read_and_clear_active_vp_irqs(struct vop2 *vop2, int vp_id)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data;
	const struct vop_intr *intr;
	int val;

	vp_data = &vop2_data->vp[vp_id];
	intr = vp_data->intr;
	val = VOP_INTR_GET_TYPE(vop2, intr, status, INTR_MASK);
	if (val)
		VOP_INTR_SET_TYPE(vop2, intr, clear, val, 1);
	return val;
}

static void vop2_wb_disable(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	struct vop2_wb *wb = &vop2->wb;

	VOP_MODULE_SET(vop2, wb, enable, 0);
	VOP_CTRL_SET(vop2, wb_dma_finish_and_en, 0);
	vop2_wb_cfg_done(vp);
}

static void vop2_wb_handler(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	struct vop2_wb *wb = &vop2->wb;
	struct vop2_wb_job *job;
	unsigned long flags;
	uint8_t wb_en;
	uint8_t wb_vp_id;
	uint8_t i;

	wb_en = vop2_readl(vop2, RK3568_WB_CTRL) & 0x01;
	wb_vp_id = (vop2_readl(vop2, RK3568_LUT_PORT_SEL) >> 8) & 0x3;
	if (wb_vp_id != vp->id)
		return;
	/*
	 * The write back should work in one shot mode,
	 * stop when write back complete in next vsync.
	 */
	if (wb_en)
		vop2_wb_disable(vp);

	spin_lock_irqsave(&wb->job_lock, flags);
	for (i = 0; i < VOP2_WB_JOB_MAX; i++) {
		job = &wb->jobs[i];
		if (job->pending) {
			job->fs_vsync_cnt++;

			if (job->fs_vsync_cnt == 2) {
				job->pending = false;
				job->fs_vsync_cnt = 0;
				drm_writeback_signal_completion(&vop2->wb.conn, 0);
			}
		}
	}
	spin_unlock_irqrestore(&wb->job_lock, flags);
}

static void vop2_dsc_isr(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	struct vop2_dsc *dsc;
	const struct dsc_error_info *dsc_error_ecw = vop2_data->dsc_error_ecw;
	const struct dsc_error_info *dsc_error_buffer_flow = vop2_data->dsc_error_buffer_flow;
	u32 dsc_error_status = 0, dsc_ecw = 0;
	int i = 0, j = 0;

	for (i = 0; i < vop2_data->nr_dscs; i++) {
		dsc = &vop2->dscs[i];

		if (!dsc->enabled)
			continue;

		dsc_error_status = VOP_MODULE_GET(vop2, dsc, dsc_error_status);
		if (!dsc_error_status)
			continue;
		dsc_ecw = VOP_MODULE_GET(vop2, dsc, dsc_ecw);

		for (j = 0; j < vop2_data->nr_dsc_ecw; j++) {
			if (dsc_ecw == dsc_error_ecw[j].dsc_error_val) {
				DRM_ERROR("dsc%d %s\n", dsc->id, dsc_error_ecw[j].dsc_error_info);
				break;
			}
		}

		if (dsc_ecw == 0x0120ffff) {
			u32 offset = dsc->regs->dsc_status.offset;

			for (j = 0; j < vop2_data->nr_dsc_buffer_flow; j++)
				DRM_ERROR("dsc%d %s:0x%x\n", dsc->id, dsc_error_buffer_flow[j].dsc_error_info,
					  vop2_readl(vop2, offset + (j << 2)));
		}
	}
}

static irqreturn_t vop2_isr(int irq, void *data)
{
	struct vop2 *vop2 = data;
	struct drm_crtc *crtc;
	struct vop2_video_port *vp;
	const struct vop2_data *vop2_data = vop2->data;
	size_t vp_max = min_t(size_t, vop2_data->nr_vps, ROCKCHIP_MAX_CRTC);
	size_t axi_max = min_t(size_t, vop2_data->nr_axi_intr, VOP2_SYS_AXI_BUS_NUM);
	uint32_t vp_irqs[ROCKCHIP_MAX_CRTC];
	uint32_t axi_irqs[VOP2_SYS_AXI_BUS_NUM];
	uint32_t active_irqs;
	uint32_t wb_irqs;
	unsigned long flags;
	int ret = IRQ_NONE;
	int i;

#define ERROR_HANDLER(x) \
	do { \
		if (active_irqs & x##_INTR) {\
			if (x##_INTR == POST_BUF_EMPTY_INTR) \
				DRM_DEV_ERROR_RATELIMITED(vop2->dev, #x " irq err at vp%d\n", vp->id); \
			else \
				DRM_DEV_ERROR_RATELIMITED(vop2->dev, #x " irq err\n"); \
			active_irqs &= ~x##_INTR; \
			ret = IRQ_HANDLED; \
		} \
	} while (0)

	/*
	 * The irq is shared with the iommu. If the runtime-pm state of the
	 * vop2-device is disabled the irq has to be targeted at the iommu.
	 */
	if (!pm_runtime_get_if_in_use(vop2->dev))
		return IRQ_NONE;

	if (vop2_core_clks_enable(vop2)) {
		DRM_DEV_ERROR(vop2->dev, "couldn't enable clocks\n");
		goto out;
	}

	/*
	 * interrupt register has interrupt status, enable and clear bits, we
	 * must hold irq_lock to avoid a race with enable/disable_vblank().
	 */
	spin_lock_irqsave(&vop2->irq_lock, flags);
	for (i = 0; i < vp_max; i++)
		vp_irqs[i] = vop2_read_and_clear_active_vp_irqs(vop2, i);
	for (i = 0; i < axi_max; i++)
		axi_irqs[i] = vop2_read_and_clear_axi_irqs(vop2, i);
	wb_irqs = vop2_read_and_clear_wb_irqs(vop2);
	spin_unlock_irqrestore(&vop2->irq_lock, flags);

	for (i = 0; i < vp_max; i++) {
		vp = &vop2->vps[i];
		crtc = &vp->rockchip_crtc.crtc;
		active_irqs = vp_irqs[i];
		if (active_irqs & DSP_HOLD_VALID_INTR) {
			complete(&vp->dsp_hold_completion);
			active_irqs &= ~DSP_HOLD_VALID_INTR;
			ret = IRQ_HANDLED;
		}

		if (active_irqs & LINE_FLAG_INTR) {
			complete(&vp->line_flag_completion);
			active_irqs &= ~LINE_FLAG_INTR;
			ret = IRQ_HANDLED;
		}

		if (active_irqs & LINE_FLAG1_INTR) {
			vop2_handle_vcnt(crtc);
			active_irqs &= ~LINE_FLAG1_INTR;
			ret = IRQ_HANDLED;
		}

		if (vop2->version == VOP_VERSION_RK3528 && vp->id == 1) {
			if (active_irqs & POST_BUF_EMPTY_INTR)
				atomic_inc(&vp->post_buf_empty_flag);

			if (active_irqs & FS_FIELD_INTR &&
			    (atomic_read(&vp->post_buf_empty_flag) > 0 ||
			     vp->need_reset_p2i_flag == true))
				queue_work(vop2->workqueue, &vop2->post_buf_empty_work);
		}

		if (active_irqs & FS_FIELD_INTR) {
			rockchip_drm_dbg(vop2->dev, VOP_DEBUG_VSYNC, "vsync_vp%d\n", vp->id);
			vop2_wb_handler(vp);
			if (likely(!vp->skip_vsync) || (vp->layer_sel_update == false)) {
				drm_crtc_handle_vblank(crtc);
				vop2_handle_vblank(vop2, crtc);
			}
			active_irqs &= ~FS_FIELD_INTR;
			ret = IRQ_HANDLED;
		}

		ERROR_HANDLER(POST_BUF_EMPTY);

		/* Unhandled irqs are spurious. */
		if (active_irqs)
			DRM_ERROR("Unknown video_port%d IRQs: %02x\n", i, active_irqs);
	}

	if (wb_irqs) {
		active_irqs = wb_irqs;
		ERROR_HANDLER(WB_UV_FIFO_FULL);
		ERROR_HANDLER(WB_YRGB_FIFO_FULL);
	}

	for (i = 0; i < axi_max; i++) {
		active_irqs = axi_irqs[i];

		ERROR_HANDLER(BUS_ERROR);

		/* Unhandled irqs are spurious. */
		if (active_irqs)
			DRM_ERROR("Unknown axi_bus%d IRQs: %02x\n", i, active_irqs);
	}

	if (vop2->data->nr_dscs)
		vop2_dsc_isr(vop2);

	vop2_core_clks_disable(vop2);
out:
	pm_runtime_put(vop2->dev);
	return ret;
}

static int vop2_plane_create_name_property(struct vop2 *vop2, struct vop2_win *win)
{
	struct drm_prop_enum_list *props = vop2->plane_name_list;
	struct drm_property *prop;
	uint64_t bits = BIT_ULL(win->plane_id);

	prop = drm_property_create_bitmask(vop2->drm_dev,
					   DRM_MODE_PROP_IMMUTABLE, "NAME",
					   props, vop2->registered_num_wins,
					   bits);
	if (!prop) {
		DRM_DEV_ERROR(vop2->dev, "create Name prop for %s failed\n", win->name);
		return -ENOMEM;
	}
	win->name_prop = prop;
	drm_object_attach_property(&win->base.base, win->name_prop, bits);

	return 0;
}

static int vop2_plane_create_feature_property(struct vop2 *vop2, struct vop2_win *win)
{
	uint64_t feature = 0;
	struct drm_property *prop;

	static const struct drm_prop_enum_list props[] = {
		{ ROCKCHIP_DRM_PLANE_FEATURE_SCALE, "scale" },
		{ ROCKCHIP_DRM_PLANE_FEATURE_AFBDC, "afbdc" },
	};

	if ((win->max_upscale_factor != 1) || (win->max_downscale_factor != 1))
		feature |= BIT(ROCKCHIP_DRM_PLANE_FEATURE_SCALE);
	if (win->feature & WIN_FEATURE_AFBDC)
		feature |= BIT(ROCKCHIP_DRM_PLANE_FEATURE_AFBDC);

	prop = drm_property_create_bitmask(vop2->drm_dev,
					   DRM_MODE_PROP_IMMUTABLE, "FEATURE",
					   props, ARRAY_SIZE(props),
					   feature);
	if (!prop) {
		DRM_DEV_ERROR(vop2->dev, "create feature prop for %s failed\n", win->name);
		return -ENOMEM;
	}

	win->feature_prop = prop;

	drm_object_attach_property(&win->base.base, win->feature_prop, feature);

	return 0;
}

static bool vop3_ignore_plane(struct vop2 *vop2, struct vop2_win *win)
{
	if (!is_vop3(vop2))
		return false;

	if (vop2->esmart_lb_mode == VOP3_ESMART_8K_MODE &&
	    win->phys_id != ROCKCHIP_VOP2_ESMART0)
		return true;
	else if (vop2->esmart_lb_mode == VOP3_ESMART_4K_4K_MODE &&
		 (win->phys_id == ROCKCHIP_VOP2_ESMART1 || win->phys_id == ROCKCHIP_VOP2_ESMART3))
		return true;
	else if (vop2->esmart_lb_mode == VOP3_ESMART_4K_2K_2K_MODE &&
		 win->phys_id == ROCKCHIP_VOP2_ESMART1)
		return true;
	else
		return false;
}

static u32 vop3_esmart_linebuffer_size(struct vop2 *vop2, struct vop2_win *win)
{
	if (!is_vop3(vop2) || vop2_cluster_window(win))
		return vop2->data->max_output.width;

	if (vop2->esmart_lb_mode == VOP3_ESMART_2K_2K_2K_2K_MODE ||
	    (vop2->esmart_lb_mode == VOP3_ESMART_4K_2K_2K_MODE && win->phys_id != ROCKCHIP_VOP2_ESMART0))
		return vop2->data->max_output.width / 2;
	else
		return vop2->data->max_output.width;
}

static void vop3_init_esmart_scale_engine(struct vop2 *vop2)
{
	u8 scale_engine_num = 0;
	struct drm_plane *plane = NULL;

	drm_for_each_plane(plane, vop2->drm_dev) {
		struct vop2_win *win = to_vop2_win(plane);

		if (win->parent || vop2_cluster_window(win))
			continue;

		win->scale_engine_num = scale_engine_num++;
	}
}

static int vop2_plane_init(struct vop2 *vop2, struct vop2_win *win, unsigned long possible_crtcs)
{
	struct rockchip_drm_private *private = vop2->drm_dev->dev_private;
	unsigned int blend_caps = BIT(DRM_MODE_BLEND_PIXEL_NONE) | BIT(DRM_MODE_BLEND_PREMULTI) |
				  BIT(DRM_MODE_BLEND_COVERAGE);
	unsigned int max_width, max_height;
	int ret;

	/*
	 * Some userspace software don't want use afbc plane
	 */
	if (win->feature & WIN_FEATURE_AFBDC) {
		if (vop2->disable_afbc_win)
			return -EACCES;
	}

	/*
	 * Some userspace software don't want cluster sub plane
	 */
	if (!vop2->support_multi_area) {
		if (win->feature & WIN_FEATURE_CLUSTER_SUB)
			return -EACCES;
	}

	/* ignore some plane register according vop3 esmart lb mode */
	if (vop3_ignore_plane(vop2, win))
		return -EACCES;

	ret = drm_universal_plane_init(vop2->drm_dev, &win->base, possible_crtcs,
				       &vop2_plane_funcs, win->formats, win->nformats,
				       win->format_modifiers, win->type, win->name);
	if (ret) {
		DRM_DEV_ERROR(vop2->dev, "failed to initialize plane %d\n", ret);
		return ret;
	}

	drm_plane_helper_add(&win->base, &vop2_plane_helper_funcs);

	drm_object_attach_property(&win->base.base, private->eotf_prop, 0);
	drm_object_attach_property(&win->base.base, private->color_space_prop, 0);
	drm_object_attach_property(&win->base.base, private->async_commit_prop, 0);

	if (win->feature & (WIN_FEATURE_CLUSTER_SUB | WIN_FEATURE_CLUSTER_MAIN))
		drm_object_attach_property(&win->base.base, private->share_id_prop, win->plane_id);

	if (win->parent)
		drm_object_attach_property(&win->base.base, private->share_id_prop,
					   win->parent->base.base.id);
	else
		drm_object_attach_property(&win->base.base, private->share_id_prop,
					   win->base.base.id);
	if (win->supported_rotations)
		drm_plane_create_rotation_property(&win->base, DRM_MODE_ROTATE_0,
						   DRM_MODE_ROTATE_0 | win->supported_rotations);
	drm_plane_create_alpha_property(&win->base);
	drm_plane_create_blend_mode_property(&win->base, blend_caps);
	drm_plane_create_zpos_property(&win->base, win->win_id, 0, vop2->registered_num_wins - 1);
	vop2_plane_create_name_property(vop2, win);
	vop2_plane_create_feature_property(vop2, win);
	max_width = vop2->data->max_input.width;
	max_height = vop2->data->max_input.height;
	if (win->feature & WIN_FEATURE_CLUSTER_SUB)
		max_width >>= 1;
	win->input_width_prop = drm_property_create_range(vop2->drm_dev, DRM_MODE_PROP_IMMUTABLE,
							  "INPUT_WIDTH", 0, max_width);
	win->input_height_prop = drm_property_create_range(vop2->drm_dev, DRM_MODE_PROP_IMMUTABLE,
							   "INPUT_HEIGHT", 0, max_height);
	max_width = vop3_esmart_linebuffer_size(vop2, win);
	max_height = vop2->data->max_output.height;
	if (win->feature & WIN_FEATURE_CLUSTER_SUB)
		max_width >>= 1;
	win->output_width_prop = drm_property_create_range(vop2->drm_dev, DRM_MODE_PROP_IMMUTABLE,
							   "OUTPUT_WIDTH", 0, max_width);
	win->output_height_prop = drm_property_create_range(vop2->drm_dev, DRM_MODE_PROP_IMMUTABLE,
							    "OUTPUT_HEIGHT", 0, max_height);
	win->scale_prop = drm_property_create_range(vop2->drm_dev, DRM_MODE_PROP_IMMUTABLE,
						    "SCALE_RATE", win->max_downscale_factor,
						    win->max_upscale_factor);
	/*
	 * Support 24 bit(RGB888) or 16 bit(rgb565) color key.
	 * Bit 31 is used as a flag to disable (0) or enable
	 * color keying (1).
	 */
	win->color_key_prop = drm_property_create_range(vop2->drm_dev, 0, "colorkey", 0,
							0x80ffffff);

	if (!win->input_width_prop || !win->input_height_prop ||
	    !win->output_width_prop || !win->output_height_prop ||
	    !win->scale_prop || !win->color_key_prop) {
		DRM_ERROR("failed to create property\n");
		return -ENOMEM;
	}

	drm_object_attach_property(&win->base.base, win->input_width_prop, 0);
	drm_object_attach_property(&win->base.base, win->input_height_prop, 0);
	drm_object_attach_property(&win->base.base, win->output_width_prop, 0);
	drm_object_attach_property(&win->base.base, win->output_height_prop, 0);
	drm_object_attach_property(&win->base.base, win->scale_prop, 0);
	drm_object_attach_property(&win->base.base, win->color_key_prop, 0);

	return 0;
}

static struct drm_plane *vop2_cursor_plane_init(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	struct drm_plane *cursor = NULL;
	struct vop2_win *win;
	unsigned long possible_crtcs = 0;

	win = vop2_find_win_by_phys_id(vop2, vp->cursor_win_id);
	if (win) {
		if (vop2->disable_win_move) {
			const struct vop2_data *vop2_data = vop2->data;
			struct drm_crtc *crtc = vop2_find_crtc_by_plane_mask(vop2, win->phys_id);

			if (crtc)
				possible_crtcs = drm_crtc_mask(crtc);
			else
				possible_crtcs = (1 << vop2_data->nr_vps) - 1;
		}

		if (win->possible_crtcs)
			possible_crtcs = win->possible_crtcs;
		win->type = DRM_PLANE_TYPE_CURSOR;
		win->zpos = vop2->registered_num_wins - 1;
		if (!vop2_plane_init(vop2, win, possible_crtcs))
			cursor = &win->base;
	}

	return cursor;
}

static int vop2_gamma_init(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data;
	struct vop2_video_port *vp;
	struct device *dev = vop2->dev;
	u16 *r_base, *g_base, *b_base;
	struct drm_crtc *crtc;
	int i = 0, j = 0;
	u32 lut_len = 0;

	if (!vop2->lut_regs)
		return 0;

	for (i = 0; i < vop2_data->nr_vps; i++) {
		vp = &vop2->vps[i];
		crtc = &vp->rockchip_crtc.crtc;
		if (!crtc->dev)
			continue;
		vp_data = &vop2_data->vp[vp->id];
		lut_len = vp_data->gamma_lut_len;
		if (!lut_len)
			continue;
		vp->gamma_lut_len = vp_data->gamma_lut_len;
		vp->lut_dma_rid = vp_data->lut_dma_rid;
		vp->lut = devm_kmalloc_array(dev, lut_len, sizeof(*vp->lut),
					     GFP_KERNEL);
		if (!vp->lut)
			return -ENOMEM;

		for (j = 0; j < lut_len; j++) {
			u32 b = j * lut_len * lut_len;
			u32 g = j * lut_len;
			u32 r = j;

			vp->lut[j] = r | g | b;
		}

		drm_mode_crtc_set_gamma_size(crtc, lut_len);
		drm_crtc_enable_color_mgmt(crtc, 0, false, lut_len);
		r_base = crtc->gamma_store;
		g_base = r_base + crtc->gamma_size;
		b_base = g_base + crtc->gamma_size;
		for (j = 0; j < lut_len; j++) {
			rockchip_vop2_crtc_fb_gamma_get(crtc, &r_base[j],
							&g_base[j],
							&b_base[j], j);
		}
	}

	return 0;
}

static int vop2_crtc_create_plane_mask_property(struct vop2 *vop2,
						struct drm_crtc *crtc,
						uint32_t plane_mask)
{
	struct drm_property *prop;
	struct vop2_video_port *vp = to_vop2_video_port(crtc);

	static const struct drm_prop_enum_list props[] = {
		{ ROCKCHIP_VOP2_CLUSTER0, "Cluster0" },
		{ ROCKCHIP_VOP2_CLUSTER1, "Cluster1" },
		{ ROCKCHIP_VOP2_ESMART0, "Esmart0" },
		{ ROCKCHIP_VOP2_ESMART1, "Esmart1" },
		{ ROCKCHIP_VOP2_SMART0, "Smart0" },
		{ ROCKCHIP_VOP2_SMART1, "Smart1" },
		{ ROCKCHIP_VOP2_CLUSTER2, "Cluster2" },
		{ ROCKCHIP_VOP2_CLUSTER3, "Cluster3" },
		{ ROCKCHIP_VOP2_ESMART2, "Esmart2" },
		{ ROCKCHIP_VOP2_ESMART3, "Esmart3" },
	};

	prop = drm_property_create_bitmask(vop2->drm_dev,
					   DRM_MODE_PROP_IMMUTABLE, "PLANE_MASK",
					   props, ARRAY_SIZE(props),
					   0xffffffff);
	if (!prop) {
		DRM_DEV_ERROR(vop2->dev, "create plane_mask prop for vp%d failed\n", vp->id);
		return -ENOMEM;
	}

	vp->plane_mask_prop = prop;
	drm_object_attach_property(&crtc->base, vp->plane_mask_prop, plane_mask);

	return 0;
}

static int vop2_crtc_create_feature_property(struct vop2 *vop2, struct drm_crtc *crtc)
{
	const struct vop2_data *vop2_data = vop2->data;
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	struct drm_property *prop;
	u64 feature = 0;

	static const struct drm_prop_enum_list props[] = {
		{ ROCKCHIP_DRM_CRTC_FEATURE_ALPHA_SCALE, "ALPHA_SCALE" },
		{ ROCKCHIP_DRM_CRTC_FEATURE_HDR10, "HDR10" },
		{ ROCKCHIP_DRM_CRTC_FEATURE_NEXT_HDR, "NEXT_HDR" },
	};

	if (vp_data->feature & VOP_FEATURE_ALPHA_SCALE)
		feature |= BIT(ROCKCHIP_DRM_CRTC_FEATURE_ALPHA_SCALE);
	if (vp_data->feature & VOP_FEATURE_HDR10)
		feature |= BIT(ROCKCHIP_DRM_CRTC_FEATURE_HDR10);
	if (vp_data->feature & VOP_FEATURE_NEXT_HDR)
		feature |= BIT(ROCKCHIP_DRM_CRTC_FEATURE_NEXT_HDR);

	prop = drm_property_create_bitmask(vop2->drm_dev,
					   DRM_MODE_PROP_IMMUTABLE, "FEATURE",
					   props, ARRAY_SIZE(props),
					   0xffffffff);
	if (!prop) {
		DRM_DEV_ERROR(vop2->dev, "create FEATURE prop for vp%d failed\n", vp->id);
		return -ENOMEM;
	}

	vp->feature_prop = prop;
	drm_object_attach_property(&crtc->base, vp->feature_prop, feature);

	prop = drm_property_create_range(vop2->drm_dev, DRM_MODE_PROP_IMMUTABLE, "OUTPUT_WIDTH",
					 0, vop2->data->vp[vp->id].max_output.width);
	if (!prop) {
		DRM_DEV_ERROR(vop2->dev, "create OUTPUT_WIDTH prop for vp%d failed\n", vp->id);
		return -ENOMEM;
	}
	vp->output_width_prop = prop;
	drm_object_attach_property(&crtc->base, vp->output_width_prop, 0);

	prop = drm_property_create_range(vop2->drm_dev, DRM_MODE_PROP_IMMUTABLE, "OUTPUT_DCLK",
					 0, rockchip_drm_get_dclk_by_width(vop2->data->vp[vp->id].max_output.width) * 1000);
	if (!prop) {
		DRM_DEV_ERROR(vop2->dev, "create OUTPUT_DCLK prop for vp%d failed\n", vp->id);
		return -ENOMEM;
	}
	vp->output_dclk_prop = prop;
	drm_object_attach_property(&crtc->base, vp->output_dclk_prop, 0);

	return 0;
}

static int vop2_crtc_create_vrr_property(struct vop2 *vop2, struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_property *prop;

	prop = drm_property_create_range(vop2->drm_dev, 0, "variable refresh rate", 0, 144);
	if (!prop) {
		DRM_DEV_ERROR(vop2->dev, "create vrr prop for vp%d failed\n", vp->id);
		return -ENOMEM;
	}
	vp->variable_refresh_rate_prop = prop;
	drm_object_attach_property(&crtc->base, vp->variable_refresh_rate_prop, 0);

	prop = drm_property_create_range(vop2->drm_dev, 0, "max refresh rate", 0, 144);
	if (!prop) {
		DRM_DEV_ERROR(vop2->dev, "create vrr prop for vp%d failed\n", vp->id);
		return -ENOMEM;
	}
	vp->max_refresh_rate_prop = prop;
	drm_object_attach_property(&crtc->base, vp->max_refresh_rate_prop, 0);

	prop = drm_property_create_range(vop2->drm_dev, 0, "min refresh rate", 0, 144);
	if (!prop) {
		DRM_DEV_ERROR(vop2->dev, "create vrr prop for vp%d failed\n", vp->id);
		return -ENOMEM;
	}
	vp->min_refresh_rate_prop = prop;
	drm_object_attach_property(&crtc->base, vp->min_refresh_rate_prop, 0);

	return 0;
}

static int vop2_crtc_create_hdr_property(struct vop2 *vop2, struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_property *prop;

	prop = drm_property_create(vop2->drm_dev, DRM_MODE_PROP_BLOB, "HDR_EXT_DATA", 0);
	if (!prop) {
		DRM_DEV_ERROR(vop2->dev, "create hdr ext data prop for vp%d failed\n", vp->id);
		return -ENOMEM;
	}
	vp->hdr_ext_data_prop = prop;
	drm_object_attach_property(&crtc->base, vp->hdr_ext_data_prop, 0);

	return 0;
}

static int vop2_crtc_create_post_acm_property(struct vop2 *vop2, struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_property *prop;

	prop = drm_property_create(vop2->drm_dev, DRM_MODE_PROP_BLOB, "ACM_LUT_DATA", 0);
	if (!prop) {
		DRM_DEV_ERROR(vop2->dev, "create acm lut data prop for vp%d failed\n", vp->id);
		return -ENOMEM;
	}
	vp->acm_lut_data_prop = prop;
	drm_object_attach_property(&crtc->base, vp->acm_lut_data_prop, 0);

	return 0;
}

static int vop2_crtc_create_post_csc_property(struct vop2 *vop2, struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_property *prop;

	prop = drm_property_create(vop2->drm_dev, DRM_MODE_PROP_BLOB, "POST_CSC_DATA", 0);
	if (!prop) {
		DRM_DEV_ERROR(vop2->dev, "create post csc data prop for vp%d failed\n", vp->id);
		return -ENOMEM;
	}
	vp->post_csc_data_prop = prop;
	drm_object_attach_property(&crtc->base, vp->post_csc_data_prop, 0);

	return 0;
}
#define RK3566_MIRROR_PLANE_MASK (BIT(ROCKCHIP_VOP2_CLUSTER1) | BIT(ROCKCHIP_VOP2_ESMART1) | \
				  BIT(ROCKCHIP_VOP2_SMART1))

/*
 * Returns:
 * Registered crtc number on success, negative error code on failure.
 */
static int vop2_create_crtc(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	struct drm_device *drm_dev = vop2->drm_dev;
	struct device *dev = vop2->dev;
	struct drm_plane *primary;
	struct drm_plane *cursor = NULL;
	struct drm_crtc *crtc;
	struct device_node *port;
	struct vop2_win *win = NULL;
	struct vop2_video_port *vp;
	const struct vop2_video_port_data *vp_data;
	uint32_t possible_crtcs;
	uint64_t soc_id;
	uint32_t registered_num_crtcs = 0;
	uint32_t plane_mask = 0;
	char clk_name[16];
	int i = 0, j = 0, k = 0;
	int ret = 0;
	bool be_used_for_primary_plane = false;
	bool find_primary_plane = false;
	bool bootloader_initialized = false;
	struct rockchip_drm_private *private = drm_dev->dev_private;

	/* all planes can attach to any crtc */
	possible_crtcs = (1 << vop2_data->nr_vps) - 1;

	/*
	 * We set plane_mask from dts or bootloader
	 * if all the plane_mask are zero, that means
	 * the bootloader don't initialized the vop, or
	 * something is wrong, the kernel will try to
	 * initial all the vp.
	 */
	for (i = 0; i < vop2_data->nr_vps; i++) {
		vp = &vop2->vps[i];
		if (vp->plane_mask) {
			bootloader_initialized = true;
			break;
		}
	}

	/*
	 * Create primary plane for eache crtc first, since we need
	 * to pass them to drm_crtc_init_with_planes, which sets the
	 * "possible_crtcs" to the newly initialized crtc.
	 */
	for (i = 0; i < vop2_data->nr_vps; i++) {
		vp_data = &vop2_data->vp[i];
		vp = &vop2->vps[i];
		vp->vop2 = vop2;
		vp->id = vp_data->id;
		vp->regs = vp_data->regs;
		vp->cursor_win_id = -1;
		primary = NULL;
		cursor = NULL;

		if (vop2->disable_win_move)
			possible_crtcs = BIT(registered_num_crtcs);

		/*
		 * we assume a vp with a zere plane_mask(set from dts or bootloader)
		 * as unused.
		 */
		if (!vp->plane_mask && bootloader_initialized)
			continue;

		if (vop2_soc_is_rk3566())
			soc_id = vp_data->soc_id[1];
		else
			soc_id = vp_data->soc_id[0];

		snprintf(clk_name, sizeof(clk_name), "dclk_vp%d", vp->id);
		vp->dclk_rst = devm_reset_control_get_optional(vop2->dev, clk_name);
		if (IS_ERR(vp->dclk_rst)) {
			DRM_DEV_ERROR(vop2->dev, "failed to get dclk reset\n");
			return PTR_ERR(vp->dclk_rst);
		}

		vp->dclk = devm_clk_get(vop2->dev, clk_name);
		if (IS_ERR(vp->dclk)) {
			DRM_DEV_ERROR(vop2->dev, "failed to get %s\n", clk_name);
			return PTR_ERR(vp->dclk);
		}

		snprintf(clk_name, sizeof(clk_name), "dclk_src_vp%d", vp->id);
		vp->dclk_parent = devm_clk_get_optional(vop2->dev, clk_name);
		if (IS_ERR(vp->dclk)) {
			DRM_DEV_ERROR(vop2->dev, "failed to get %s\n", clk_name);
			return PTR_ERR(vp->dclk);
		}

		crtc = &vp->rockchip_crtc.crtc;

		port = of_graph_get_port_by_id(dev->of_node, i);
		if (!port) {
			DRM_DEV_ERROR(vop2->dev, "no port node found for video_port%d\n", i);
			return -ENOENT;
		}
		crtc->port = port;
		of_property_read_u32(port, "cursor-win-id", &vp->cursor_win_id);

		plane_mask = vp->plane_mask;
		if (vop2_soc_is_rk3566()) {
			if ((vp->plane_mask & RK3566_MIRROR_PLANE_MASK) &&
			    (vp->plane_mask & ~RK3566_MIRROR_PLANE_MASK)) {
				plane_mask &= ~RK3566_MIRROR_PLANE_MASK;
			}
		}

		if (vp->primary_plane_phy_id >= 0) {
			win = vop2_find_win_by_phys_id(vop2, vp->primary_plane_phy_id);
			if (win) {
				find_primary_plane = true;
				win->type = DRM_PLANE_TYPE_PRIMARY;
			}
		} else {
			j = 0;
			while (j < vop2->registered_num_wins) {
				be_used_for_primary_plane = false;
				win = &vop2->win[j];
				j++;

				if (win->parent || (win->feature & WIN_FEATURE_CLUSTER_SUB))
					continue;

				if (win->type != DRM_PLANE_TYPE_PRIMARY)
					continue;

				for (k = 0; k < vop2_data->nr_vps; k++) {
					if (win->phys_id == vop2->vps[k].primary_plane_phy_id) {
						be_used_for_primary_plane = true;
						break;
					}
				}

				if (be_used_for_primary_plane)
					continue;

				find_primary_plane = true;
				break;
			}

			if (find_primary_plane)
				vp->primary_plane_phy_id = win->phys_id;
		}

		if (!find_primary_plane) {
			DRM_DEV_ERROR(vop2->dev, "No primary plane find for video_port%d\n", i);
			break;
		} else {
			/* give lowest zpos for primary plane */
			win->zpos = registered_num_crtcs;
			if (win->possible_crtcs)
				possible_crtcs = win->possible_crtcs;
			if (vop2_plane_init(vop2, win, possible_crtcs)) {
				DRM_DEV_ERROR(vop2->dev, "failed to init primary plane\n");
				break;
			}
			primary = &win->base;
		}

		/* some times we want a cursor window for some vp */
		if (vp->cursor_win_id < 0) {
			bool be_used_for_cursor_plane = false;

			j = 0;
			while (j < vop2->registered_num_wins) {
				win = &vop2->win[j++];

				if (win->parent || (win->feature & WIN_FEATURE_CLUSTER_SUB))
					continue;

				if (win->type != DRM_PLANE_TYPE_CURSOR)
					continue;

				for (k = 0; k < vop2_data->nr_vps; k++) {
					if (vop2->vps[k].cursor_win_id == win->phys_id)
						be_used_for_cursor_plane = true;
				}
				if (be_used_for_cursor_plane)
					continue;
				vp->cursor_win_id = win->phys_id;
			}
		}

		if (vp->cursor_win_id >= 0) {
			cursor = vop2_cursor_plane_init(vp);
			if (!cursor)
				DRM_WARN("failed to init cursor plane for vp%d\n", vp->id);
			else
				DRM_DEV_INFO(vop2->dev, "%s as cursor plane for vp%d\n",
					     cursor->name, vp->id);
		}

		ret = drm_crtc_init_with_planes(drm_dev, crtc, primary, cursor, &vop2_crtc_funcs,
						"video_port%d", vp->id);
		if (ret) {
			DRM_DEV_ERROR(vop2->dev, "crtc init for video_port%d failed\n", i);
			return ret;
		}

		drm_crtc_helper_add(crtc, &vop2_crtc_helper_funcs);

		drm_flip_work_init(&vp->fb_unref_work, "fb_unref", vop2_fb_unref_worker);

		init_completion(&vp->dsp_hold_completion);
		init_completion(&vp->line_flag_completion);
		rockchip_register_crtc_funcs(crtc, &private_crtc_funcs);
		soc_id = vop2_soc_id_fixup(soc_id);
		drm_object_attach_property(&crtc->base, private->soc_id_prop, soc_id);
		drm_object_attach_property(&crtc->base, private->port_id_prop, vp->id);
		drm_object_attach_property(&crtc->base, private->aclk_prop, 0);
		drm_object_attach_property(&crtc->base, private->bg_prop, 0);
		drm_object_attach_property(&crtc->base, private->line_flag_prop, 0);
		if (vp_data->feature & VOP_FEATURE_OVERSCAN) {
			drm_object_attach_property(&crtc->base,
						   drm_dev->mode_config.tv_left_margin_property, 100);
			drm_object_attach_property(&crtc->base,
						   drm_dev->mode_config.tv_right_margin_property, 100);
			drm_object_attach_property(&crtc->base,
						   drm_dev->mode_config.tv_top_margin_property, 100);
			drm_object_attach_property(&crtc->base,
						   drm_dev->mode_config.tv_bottom_margin_property, 100);
		}
		if (plane_mask)
			vop2_crtc_create_plane_mask_property(vop2, crtc, plane_mask);
		vop2_crtc_create_feature_property(vop2, crtc);
		vop2_crtc_create_vrr_property(vop2, crtc);

		ret = drm_self_refresh_helper_init(crtc);
		if (ret)
			DRM_DEV_DEBUG_KMS(vop2->dev,
					  "Failed to init %s with SR helpers %d, ignoring\n",
					  crtc->name, ret);

		if (vp_data->feature & VOP_FEATURE_VIVID_HDR)
			vop2_crtc_create_hdr_property(vop2, crtc);
		if (vp_data->feature & VOP_FEATURE_POST_ACM)
			vop2_crtc_create_post_acm_property(vop2, crtc);
		if (vp_data->feature & VOP_FEATURE_POST_CSC)
			vop2_crtc_create_post_csc_property(vop2, crtc);

		registered_num_crtcs++;
	}

	/*
	 * change the unused primary window to overlay window
	 */
	for (j = 0; j < vop2->registered_num_wins; j++) {
		win = &vop2->win[j];
		be_used_for_primary_plane = false;

		for (k = 0; k < vop2_data->nr_vps; k++) {
			if (vop2->vps[k].primary_plane_phy_id == win->phys_id) {
				be_used_for_primary_plane = true;
				break;
			}
		}

		if (win->type == DRM_PLANE_TYPE_PRIMARY &&
		    !be_used_for_primary_plane)
			win->type = DRM_PLANE_TYPE_OVERLAY;
	}

	/*
	 * create overlay planes of the leftover overlay win
	 * Create drm_planes for overlay windows with possible_crtcs restricted
	 */
	for (j = 0; j < vop2->registered_num_wins; j++) {
		win = &vop2->win[j];

		if (win->type != DRM_PLANE_TYPE_OVERLAY)
			continue;
		/*
		 * Only dual display on rk3568(which need two crtcs) need mirror win
		 */
		if (registered_num_crtcs < 2 && vop2_is_mirror_win(win))
			continue;
		/*
		 * zpos of overlay plane is higher than primary
		 * and lower than cursor
		 */
		win->zpos = registered_num_crtcs + j;

		if (vop2->disable_win_move) {
			crtc = vop2_find_crtc_by_plane_mask(vop2, win->phys_id);
			if (crtc)
				possible_crtcs = drm_crtc_mask(crtc);
			else
				possible_crtcs = (1 << vop2_data->nr_vps) - 1;
		}
		if (win->possible_crtcs)
			possible_crtcs = win->possible_crtcs;

		ret = vop2_plane_init(vop2, win, possible_crtcs);
		if (ret)
			DRM_WARN("failed to init overlay plane %s\n", win->name);
	}

	if (is_vop3(vop2))
		vop3_init_esmart_scale_engine(vop2);

	return registered_num_crtcs;
}

static void vop2_destroy_crtc(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);

	drm_self_refresh_helper_cleanup(crtc);
	if (vp->hdr_lut_gem_obj)
		rockchip_gem_free_object(&vp->hdr_lut_gem_obj->base);

	of_node_put(crtc->port);

	/*
	 * Destroy CRTC after vop2_plane_destroy() since vop2_disable_plane()
	 * references the CRTC.
	 */
	drm_crtc_cleanup(crtc);
	drm_flip_work_cleanup(&vp->fb_unref_work);
}

static int vop2_pd_data_init(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_power_domain_data *pd_data;
	struct vop2_power_domain *pd;
	int i;

	INIT_LIST_HEAD(&vop2->pd_list_head);

	for (i = 0; i < vop2_data->nr_pds; i++) {
		pd_data = &vop2_data->pd[i];
		pd = devm_kzalloc(vop2->dev, sizeof(*pd), GFP_KERNEL);
		if (!pd)
			return -ENOMEM;
		pd->vop2 = vop2;
		pd->data = pd_data;
		pd->vp_mask = 0;
		spin_lock_init(&pd->lock);
		list_add_tail(&pd->list, &vop2->pd_list_head);
		INIT_DELAYED_WORK(&pd->power_off_work, vop2_power_domain_off_work);
		if (pd_data->parent_id) {
			pd->parent = vop2_find_pd_by_id(vop2, pd_data->parent_id);
			if (!pd->parent) {
				DRM_DEV_ERROR(vop2->dev, "no parent pd find for pd%d\n", pd->data->id);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static void vop2_dsc_data_init(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_dsc_data *dsc_data;
	struct vop2_dsc *dsc;
	int i;

	for (i = 0; i < vop2_data->nr_dscs; i++) {
		dsc = &vop2->dscs[i];
		dsc_data = &vop2_data->dsc[i];
		dsc->id = dsc_data->id;
		dsc->max_slice_num = dsc_data->max_slice_num;
		dsc->max_linebuf_depth = dsc_data->max_linebuf_depth;
		dsc->min_bits_per_pixel = dsc_data->min_bits_per_pixel;
		dsc->regs = dsc_data->regs;
		dsc->attach_vp_id = -1;
		if (dsc_data->pd_id)
			dsc->pd = vop2_find_pd_by_id(vop2, dsc_data->pd_id);
	}
}

static int vop2_win_init(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_layer_data *layer_data;
	struct drm_prop_enum_list *plane_name_list;
	struct vop2_win *win;
	struct vop2_layer *layer;
	char name[DRM_PROP_NAME_LEN];
	unsigned int num_wins = 0;
	uint8_t plane_id = 0;
	unsigned int i, j;

	for (i = 0; i < vop2_data->win_size; i++) {
		const struct vop2_win_data *win_data = &vop2_data->win[i];

		win = &vop2->win[num_wins];
		win->name = win_data->name;
		win->regs = win_data->regs;
		win->offset = win_data->base;
		win->type = win_data->type;
		win->formats = win_data->formats;
		win->nformats = win_data->nformats;
		win->format_modifiers = win_data->format_modifiers;
		win->supported_rotations = win_data->supported_rotations;
		win->max_upscale_factor = win_data->max_upscale_factor;
		win->max_downscale_factor = win_data->max_downscale_factor;
		win->hsu_filter_mode = win_data->hsu_filter_mode;
		win->hsd_filter_mode = win_data->hsd_filter_mode;
		win->vsu_filter_mode = win_data->vsu_filter_mode;
		win->vsd_filter_mode = win_data->vsd_filter_mode;
		win->hsd_pre_filter_mode = win_data->hsd_pre_filter_mode;
		win->vsd_pre_filter_mode = win_data->vsd_pre_filter_mode;
		win->dly = win_data->dly;
		win->feature = win_data->feature;
		win->phys_id = win_data->phys_id;
		win->splice_win_id = win_data->splice_win_id;
		win->layer_sel_id = win_data->layer_sel_id;
		win->win_id = i;
		win->plane_id = plane_id++;
		win->area_id = 0;
		win->zpos = i;
		win->vop2 = vop2;
		win->axi_id = win_data->axi_id;
		win->axi_yrgb_id = win_data->axi_yrgb_id;
		win->axi_uv_id = win_data->axi_uv_id;
		win->possible_crtcs = win_data->possible_crtcs;

		if (win_data->pd_id)
			win->pd = vop2_find_pd_by_id(vop2, win_data->pd_id);

		num_wins++;

		if (!vop2->support_multi_area)
			continue;

		for (j = 0; j < win_data->area_size; j++) {
			struct vop2_win *area = &vop2->win[num_wins];
			const struct vop2_win_regs *regs = win_data->area[j];

			area->parent = win;
			area->offset = win->offset;
			area->regs = regs;
			area->type = DRM_PLANE_TYPE_OVERLAY;
			area->formats = win->formats;
			area->feature = win->feature;
			area->nformats = win->nformats;
			area->format_modifiers = win->format_modifiers;
			area->max_upscale_factor = win_data->max_upscale_factor;
			area->max_downscale_factor = win_data->max_downscale_factor;
			area->supported_rotations = win_data->supported_rotations;
			area->hsu_filter_mode = win_data->hsu_filter_mode;
			area->hsd_filter_mode = win_data->hsd_filter_mode;
			area->vsu_filter_mode = win_data->vsu_filter_mode;
			area->vsd_filter_mode = win_data->vsd_filter_mode;
			area->hsd_pre_filter_mode = win_data->hsd_pre_filter_mode;
			area->vsd_pre_filter_mode = win_data->vsd_pre_filter_mode;
			area->possible_crtcs = win->possible_crtcs;

			area->vop2 = vop2;
			area->win_id = i;
			area->phys_id = win->phys_id;
			area->area_id = j + 1;
			area->plane_id = plane_id++;
			snprintf(name, min(sizeof(name), strlen(win->name)), "%s", win->name);
			snprintf(name, sizeof(name), "%s%d", name, area->area_id);
			area->name = devm_kstrdup(vop2->dev, name, GFP_KERNEL);
			num_wins++;
		}
	}

	vop2->registered_num_wins = num_wins;

	if (!is_vop3(vop2)) {
		for (i = 0; i < vop2_data->nr_layers; i++) {
			layer = &vop2->layers[i];
			layer_data = &vop2_data->layer[i];
			layer->id = layer_data->id;
			layer->regs = layer_data->regs;
		}
	}

	plane_name_list = devm_kzalloc(vop2->dev,
				       vop2->registered_num_wins * sizeof(*plane_name_list),
				       GFP_KERNEL);
	if (!plane_name_list) {
		DRM_DEV_ERROR(vop2->dev, "failed to alloc memory for plane_name_list\n");
		return -ENOMEM;
	}

	for (i = 0; i < vop2->registered_num_wins; i++) {
		win = &vop2->win[i];
		plane_name_list[i].type = win->plane_id;
		plane_name_list[i].name = win->name;
	}

	vop2->plane_name_list = plane_name_list;

	return 0;
}

#include "rockchip_vop2_clk.c"
static void post_buf_empty_work_event(struct work_struct *work)
{
	struct vop2 *vop2 = container_of(work, struct vop2, post_buf_empty_work);
	struct rockchip_drm_private *private = vop2->drm_dev->dev_private;
	struct vop2_video_port *vp = &vop2->vps[1];

	/*
	 * For RK3528, VP1 only supports NTSC and PAL mode(both interlace). If
	 * POST_BUF_EMPTY_INTR comes, it is needed to reset the p2i_en bit, in
	 * order to update the line parity flag, which ensures the correct order
	 * of odd and even lines.
	 */
	if (vop2->version == VOP_VERSION_RK3528) {
		if (atomic_read(&vp->post_buf_empty_flag) > 0) {
			atomic_set(&vp->post_buf_empty_flag, 0);

			mutex_lock(&private->ovl_lock);
			vop2_wait_for_fs_by_done_bit_status(vp);
			VOP_MODULE_SET(vop2, vp, p2i_en, 0);
			vop2_cfg_done(&vp->rockchip_crtc.crtc);
			vop2_wait_for_fs_by_done_bit_status(vp);
			mutex_unlock(&private->ovl_lock);

			vp->need_reset_p2i_flag = true;
		} else if (vp->need_reset_p2i_flag == true) {
			mutex_lock(&private->ovl_lock);
			vop2_wait_for_fs_by_done_bit_status(vp);
			VOP_MODULE_SET(vop2, vp, p2i_en, 1);
			vop2_cfg_done(&vp->rockchip_crtc.crtc);
			vop2_wait_for_fs_by_done_bit_status(vp);
			mutex_unlock(&private->ovl_lock);

			vp->need_reset_p2i_flag = false;
		}
	}
}

static bool vop2_plane_mask_check(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	u32 plane_mask = 0;
	int i;

	/*
	 * For RK3568 and RK3588, all windows need to be assigned to
	 * one of all vps, and two of vps can not share the same window.
	 */
	if (vop2->version != VOP_VERSION_RK3568 && vop2->version != VOP_VERSION_RK3588)
		return true;

	for (i = 0; i < vop2_data->nr_vps; i++) {
		if (plane_mask & vop2->vps[i].plane_mask) {
			DRM_WARN("the same window can't be assigned to two vp\n");
			return false;
		}
		plane_mask |= vop2->vps[i].plane_mask;
	}

	if (hweight32(plane_mask) != vop2_data->nr_layers ||
	    plane_mask != vop2_data->plane_mask_base) {
		DRM_WARN("all windows should be assigned, full plane mask: 0x%x, current plane mask: 0x%x\n",
			 vop2_data->plane_mask_base, plane_mask);
		return false;
	}

	return true;
}

static uint32_t vop2_vp_plane_mask_to_bitmap(const struct vop2_vp_plane_mask *vp_plane_mask)
{
	int layer_phy_id = 0;
	int plane_mask = 0;
	int i;

	for (i = 0; i < vp_plane_mask->attached_layers_nr; i++) {
		layer_phy_id = vp_plane_mask->attached_layers[i];
		plane_mask |= BIT(layer_phy_id);
	}

	return plane_mask;
}

static bool vop2_get_vp_of_status(struct device_node *vp_node)
{
	struct device_node *vp_sub_node;
	struct device_node *remote_node;
	bool vp_enable = false;

	for_each_child_of_node(vp_node, vp_sub_node) {
		remote_node = of_graph_get_remote_endpoint(vp_sub_node);
		vp_enable |= of_device_is_available(remote_node);
	}

	return vp_enable;
}

static void vop2_plane_mask_assign(struct vop2 *vop2, struct device_node *vop_out_node)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_vp_plane_mask *plane_mask;
	struct device_node *child;
	int active_vp_num = 0;
	int vp_id;
	int i = 0;

	for_each_child_of_node(vop_out_node, child) {
		if (vop2_get_vp_of_status(child))
			active_vp_num++;
	}

	if (vop2_soc_is_rk3566() && active_vp_num > 2)
		DRM_WARN("RK3566 only support 2 vps\n");
	plane_mask = vop2_data->plane_mask;
	plane_mask += (active_vp_num - 1) * ROCKCHIP_MAX_CRTC;

	for_each_child_of_node(vop_out_node, child) {
		of_property_read_u32(child, "reg", &vp_id);
		if (vop2_get_vp_of_status(child)) {
			vop2->vps[vp_id].plane_mask = vop2_vp_plane_mask_to_bitmap(&plane_mask[i]);
			vop2->vps[vp_id].primary_plane_phy_id = plane_mask[i].primary_plane_id;
			i++;
		} else {
			vop2->vps[vp_id].plane_mask = 0;
			vop2->vps[vp_id].primary_plane_phy_id = ROCKCHIP_VOP2_PHY_ID_INVALID;
		}
	}
}

static int vop2_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct vop2_data *vop2_data;
	struct drm_device *drm_dev = data;
	struct vop2 *vop2;
	struct resource *res;
	size_t alloc_size;
	int ret, i;
	int num_wins = 0;
	int registered_num_crtcs;
	struct device_node *vop_out_node;
	struct device_node *mcu_timing_node;

	vop2_data = of_device_get_match_data(dev);
	if (!vop2_data)
		return -ENODEV;

	for (i = 0; i < vop2_data->win_size; i++) {
		const struct vop2_win_data *win_data = &vop2_data->win[i];

		num_wins += win_data->area_size + 1;
	}

	/* Allocate vop2 struct and its vop2_win array */
	alloc_size = sizeof(*vop2) + sizeof(*vop2->win) * num_wins;
	vop2 = devm_kzalloc(dev, alloc_size, GFP_KERNEL);
	if (!vop2)
		return -ENOMEM;

	vop2->dev = dev;
	vop2->data = vop2_data;
	vop2->drm_dev = drm_dev;
	vop2->version = vop2_data->version;

	dev_set_drvdata(dev, vop2);

	vop2->support_multi_area = of_property_read_bool(dev->of_node, "support-multi-area");
	vop2->disable_afbc_win = of_property_read_bool(dev->of_node, "disable-afbc-win");
	vop2->disable_win_move = of_property_read_bool(dev->of_node, "disable-win-move");
	vop2->skip_ref_fb = of_property_read_bool(dev->of_node, "skip-ref-fb");

	ret = vop2_pd_data_init(vop2);
	if (ret)
		return ret;
	/*
	 * esmart lb mode default config at vop2_reg.c vop2_data.esmart_lb_mode,
	 * you can rewrite at dts vop node:
	 *
	 * VOP3_ESMART_8K_MODE = 0,
	 * VOP3_ESMART_4K_4K_MODE = 1,
	 * VOP3_ESMART_4K_2K_2K_MODE = 2,
	 * VOP3_ESMART_2K_2K_2K_2K_MODE = 3,
	 *
	 * &vop {
	 *	 esmart_lb_mode = /bits/ 8 <2>;
	 * };
	 */
	ret = of_property_read_u8(dev->of_node, "esmart_lb_mode", &vop2->esmart_lb_mode);
	if (ret < 0)
		vop2->esmart_lb_mode = vop2->data->esmart_lb_mode;

	ret = vop2_win_init(vop2);
	if (ret)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (!res) {
		DRM_DEV_ERROR(vop2->dev, "failed to get vop2 register byname\n");
		return -EINVAL;
	}
	vop2->res = res;
	vop2->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(vop2->regs))
		return PTR_ERR(vop2->regs);
	vop2->len = resource_size(res);

	vop2->regsbak = devm_kzalloc(dev, vop2->len, GFP_KERNEL);
	if (!vop2->regsbak)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gamma_lut");
	if (res) {
		vop2->lut_regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(vop2->lut_regs))
			return PTR_ERR(vop2->lut_regs);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "acm_regs");
	if (res) {
		vop2->acm_regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(vop2->acm_regs))
			return PTR_ERR(vop2->acm_regs);
	}

	vop2->sys_grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");
	vop2->grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,vop-grf");
	vop2->vo1_grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,vo1-grf");
	vop2->sys_pmu = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,pmu");

	vop2->hclk = devm_clk_get(vop2->dev, "hclk_vop");
	if (IS_ERR(vop2->hclk)) {
		DRM_DEV_ERROR(vop2->dev, "failed to get hclk source\n");
		return PTR_ERR(vop2->hclk);
	}
	vop2->aclk = devm_clk_get(vop2->dev, "aclk_vop");
	if (IS_ERR(vop2->aclk)) {
		DRM_DEV_ERROR(vop2->dev, "failed to get aclk source\n");
		return PTR_ERR(vop2->aclk);
	}

	vop2->pclk = devm_clk_get_optional(vop2->dev, "pclk_vop");
	if (IS_ERR(vop2->pclk)) {
		DRM_DEV_ERROR(vop2->dev, "failed to get pclk source\n");
		return PTR_ERR(vop2->pclk);
	}

	vop2->ahb_rst = devm_reset_control_get_optional(vop2->dev, "ahb");
	if (IS_ERR(vop2->ahb_rst)) {
		DRM_DEV_ERROR(vop2->dev, "failed to get ahb reset\n");
		return PTR_ERR(vop2->ahb_rst);
	}

	vop2->axi_rst = devm_reset_control_get_optional(vop2->dev, "axi");
	if (IS_ERR(vop2->axi_rst)) {
		DRM_DEV_ERROR(vop2->dev, "failed to get axi reset\n");
		return PTR_ERR(vop2->axi_rst);
	}

	vop2->irq = platform_get_irq(pdev, 0);
	if (vop2->irq < 0) {
		DRM_DEV_ERROR(dev, "cannot find irq for vop2\n");
		return vop2->irq;
	}

	vop_out_node = of_get_child_by_name(dev->of_node, "ports");
	if (vop_out_node) {
		struct device_node *child;

		for_each_child_of_node(vop_out_node, child) {
			u32 plane_mask = 0;
			u32 primary_plane_phy_id = 0;
			u32 vp_id = 0;
			u32 val = 0;

			of_property_read_u32(child, "rockchip,plane-mask", &plane_mask);
			of_property_read_u32(child, "rockchip,primary-plane", &primary_plane_phy_id);
			of_property_read_u32(child, "reg", &vp_id);

			vop2->vps[vp_id].plane_mask = plane_mask;
			if (plane_mask)
				vop2->vps[vp_id].primary_plane_phy_id = primary_plane_phy_id;
			else
				vop2->vps[vp_id].primary_plane_phy_id = ROCKCHIP_VOP2_PHY_ID_INVALID;

			vop2->vps[vp_id].xmirror_en = of_property_read_bool(child, "xmirror-enable");

			ret = of_clk_set_defaults(child, false);
			if (ret) {
				DRM_DEV_ERROR(dev, "Failed to set clock defaults %d\n", ret);
				return ret;
			}

			mcu_timing_node = of_get_child_by_name(child, "mcu-timing");
			if (mcu_timing_node) {
				if (!of_property_read_u32(mcu_timing_node, "mcu-pix-total", &val))
					vop2->vps[vp_id].mcu_timing.mcu_pix_total = val;
				if (!of_property_read_u32(mcu_timing_node, "mcu-cs-pst", &val))
					vop2->vps[vp_id].mcu_timing.mcu_cs_pst = val;
				if (!of_property_read_u32(mcu_timing_node, "mcu-cs-pend", &val))
					vop2->vps[vp_id].mcu_timing.mcu_cs_pend = val;
				if (!of_property_read_u32(mcu_timing_node, "mcu-rw-pst", &val))
					vop2->vps[vp_id].mcu_timing.mcu_rw_pst = val;
				if (!of_property_read_u32(mcu_timing_node, "mcu-rw-pend", &val))
					vop2->vps[vp_id].mcu_timing.mcu_rw_pend = val;
				if (!of_property_read_u32(mcu_timing_node, "mcu-hold-mode", &val))
					vop2->vps[vp_id].mcu_timing.mcu_hold_mode = val;
			}
		}

		if (!vop2_plane_mask_check(vop2)) {
			DRM_WARN("use default plane mask\n");
			vop2_plane_mask_assign(vop2, vop_out_node);
		}

		for (i = 0; i < vop2->data->nr_vps; i++) {
			DRM_DEV_INFO(dev, "vp%d assign plane mask: 0x%x, primary plane phy id: %d\n",
				     i, vop2->vps[i].plane_mask,
				     vop2->vps[i].primary_plane_phy_id);
		}
	}

	vop2_extend_clk_init(vop2);
	spin_lock_init(&vop2->reg_lock);
	spin_lock_init(&vop2->irq_lock);
	mutex_init(&vop2->vop2_lock);

	if (vop2->version == VOP_VERSION_RK3528) {
		atomic_set(&vop2->vps[1].post_buf_empty_flag, 0);
		vop2->workqueue = create_workqueue("post_buf_empty_wq");
		INIT_WORK(&vop2->post_buf_empty_work, post_buf_empty_work_event);
	}

	ret = devm_request_irq(dev, vop2->irq, vop2_isr, IRQF_SHARED, dev_name(dev), vop2);
	if (ret)
		return ret;

	vop2_dsc_data_init(vop2);

	registered_num_crtcs = vop2_create_crtc(vop2);
	if (registered_num_crtcs <= 0)
		return -ENODEV;

	ret = vop2_gamma_init(vop2);
	if (ret)
		return ret;
	vop2_clk_init(vop2);
	vop2_cubic_lut_init(vop2);
	vop2_wb_connector_init(vop2, registered_num_crtcs);
	pm_runtime_enable(&pdev->dev);

	return 0;
}

static void vop2_unbind(struct device *dev, struct device *master, void *data)
{
	struct vop2 *vop2 = dev_get_drvdata(dev);
	struct drm_device *drm_dev = vop2->drm_dev;
	struct list_head *plane_list = &drm_dev->mode_config.plane_list;
	struct list_head *crtc_list = &drm_dev->mode_config.crtc_list;
	struct drm_crtc *crtc, *tmpc;
	struct drm_plane *plane, *tmpp;

	pm_runtime_disable(dev);

	list_for_each_entry_safe(plane, tmpp, plane_list, head)
		drm_plane_cleanup(plane);

	list_for_each_entry_safe(crtc, tmpc, crtc_list, head)
		vop2_destroy_crtc(crtc);

	vop2_wb_connector_destory(vop2);
}

const struct component_ops vop2_component_ops = {
	.bind = vop2_bind,
	.unbind = vop2_unbind,
};
EXPORT_SYMBOL_GPL(vop2_component_ops);
