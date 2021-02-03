// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2020 Rockchip Electronics Co., Ltd.
 * Author: Andy Yan <andy.yan@rock-chips.com>
 */
#include <drm/drm.h>
#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_flip_work.h>
#include <drm/drm_plane_helper.h>
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
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/component.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/delay.h>
#include <linux/swab.h>
#include <linux/sort.h>
#include <linux/rockchip/cpu.h>
#include <soc/rockchip/rockchip_dmc.h>
#include <soc/rockchip/rockchip-system-status.h>
#include <uapi/linux/videodev2.h>

#include "../drm_internal.h"

#include "rockchip_drm_drv.h"
#include "rockchip_drm_gem.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_psr.h"
#include "rockchip_drm_vop.h"
#include "rockchip_vop_reg.h"

#define _REG_SET(vop2, name, off, reg, mask, v, relaxed) \
		vop2_mask_write(vop2, off + reg.offset, mask, reg.shift, v, reg.write_mask, relaxed)

#define REG_SET(x, name, off, reg, v, relaxed) \
		_REG_SET(x, name, off, reg, reg.mask, v, relaxed)
#define REG_SET_MASK(x, name, off, reg, mask, v, relaxed) \
		_REG_SET(x, name, off, reg, reg.mask & mask, v, relaxed)

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

#define VOP_WIN_NAME(win, name) \
		(vop2_get_win_regs(win, &win->regs->name)->name)

#define VOP_WIN_TO_INDEX(vop2_win) \
	((vop2_win) - (vop2_win)->vop2->win)

#define VOP_GRF_SET(vop2, reg, v) \
	do { \
		if (vop2->data->grf_ctrl) { \
			vop2_grf_writel(vop2, vop2->data->grf_ctrl->reg, v); \
		} \
	} while (0)

#define to_vop2_video_port(c)         container_of(c, struct vop2_video_port, crtc)
#define to_vop2_win(x) container_of(x, struct vop2_win, base)
#define to_vop2_plane_state(x) container_of(x, struct vop2_plane_state, base)
#define to_wb_state(x) container_of(x, struct vop2_wb_connector_state, base)


#define VOP2_SYS_AXI_BUS_NUM 2

#define VOP2_CLUSTER_YUV444_10 0x12

#define VOP2_COLOR_KEY_NONE		(0 << 31)
#define VOP2_COLOR_KEY_MASK		(1 << 31)


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

enum vop2_hdr_lut_mode {
	VOP2_HDR_LUT_MODE_AXI,
	VOP2_HDR_LUT_MODE_AHB,
};

enum vop2_pending {
	VOP_PENDING_FB_UNREF,
};

struct vop2_zpos {
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
	int eotf;
	int color_space;
	int global_alpha;
	int blend_mode;
	int color_key;
	void *yrgb_kvaddr;
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
	int layer_sel_id;
	/**
	 * @vp_mask: Bitmask of video_port0/1/2 this win attached to,
	 * one win can only attach to one vp at the one time.
	 */
	uint8_t vp_mask;
	/*
	 * @possible_crtcs: which crtc/vp this win can attached to.
	 */
	uint32_t possible_crtcs;
	uint8_t zpos;
	uint32_t offset;
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

struct vop2_wb {
	uint8_t vp_id;
	struct drm_writeback_connector conn;
	const struct vop2_wb_regs *regs;
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
	struct drm_crtc crtc;
	struct vop2 *vop2;
	struct clk *dclk;
	uint8_t id;
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
	 * @wb_en: write back enabled on this port;
	 */
	bool wb_en;

	/**
	 * @fs_vsync_cnt: frame start vysnc counter,
	 * used to get the write back complete event;
	 */
	uint32_t fs_vsync_cnt;

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
	 * @win_mask: Bitmask of wins attached to the video port;
	 */
	uint32_t win_mask;
	/**
	 * @nr_layers: active layers attached to the video port;
	 */
	uint8_t nr_layers;

	/**
	 * @active_tv_state: TV connector related states
	 */
	struct drm_tv_connector_state active_tv_state;
};

struct vop2 {
	u32 version;
	struct device *dev;
	struct drm_device *drm_dev;
	struct vop2_video_port vps[ROCKCHIP_MAX_CRTC];
	struct vop2_wb wb;
	struct dentry *debugfs;
	struct drm_info_list *debugfs_files;
	struct drm_property *soc_id_prop;
	struct drm_property *vp_id_prop;
	struct drm_prop_enum_list *plane_name_list;
	bool is_iommu_enabled;
	bool is_iommu_needed;
	bool is_enabled;
	bool support_multi_area;

	bool loader_protect;

	const struct vop2_data *data;
	/* Number of win that registered as plane,
	 * maybe less than the total number of hardware
	 * win.
	 */
	uint32_t registered_num_wins;
	uint8_t used_mixers;
	/**
	 * @active_vp_mask: Bitmask of active video ports;
	 */
	uint8_t active_vp_mask;

	uint32_t *regsbak;
	void __iomem *regs;
	struct regmap *grf;

	/* physical map length of vop2 register */
	uint32_t len;

	void __iomem *lut_regs;
	u32 *lut;
	u32 lut_len;
	bool lut_active;
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

	struct vop2_layer layers[ROCKCHIP_MAX_LAYER];
	/* must put at the end of the struct */
	struct vop2_win win[];
};

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
	{ MEDIA_BUS_FMT_RGB666_1X7X3_JEIDA, "RGB666_1X7X3_JEIDA" },
	{ MEDIA_BUS_FMT_YUV8_1X24, "YUV8_1X24" },
	{ MEDIA_BUS_FMT_UYYVYY8_0_5X24, "UYYVYY8_0_5X24" },
	{ MEDIA_BUS_FMT_YUV10_1X30, "YUV10_1X30" },
	{ MEDIA_BUS_FMT_UYYVYY10_0_5X30, "UYYVYY10_0_5X30" },
	{ MEDIA_BUS_FMT_SRGB888_3X8, "SRGB888_3X8" },
	{ MEDIA_BUS_FMT_SRGB888_DUMMY_4X8, "SRGB888_DUMMY_4X8" },
	{ MEDIA_BUS_FMT_RGB888_1X24, "RGB888_1X24" },
	{ MEDIA_BUS_FMT_RGB888_1X7X4_SPWG, "RGB888_1X7X4_SPWG" },
	{ MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA, "RGB888_1X7X4_JEIDA" },
};

static DRM_ENUM_NAME_FN(drm_get_bus_format_name, drm_bus_format_enum_list)

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

static inline void vop2_grf_writel(struct vop2 *vop2, struct vop_reg reg, u32 v)
{
	u32 val = 0;

	if (IS_ERR_OR_NULL(vop2->grf))
		return;

	if (reg.mask) {
		val = (v << reg.shift) | (reg.mask << (reg.shift + 16));
		regmap_write(vop2->grf, reg.offset, val);
	}
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

static bool vop2_soc_is_rk3566(void)
{
	return soc_is_rk3566();
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

	return vop2_readl(vp->vop2, offset) >> 16;
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

static void vop2_wait_for_fs_by_vcnt(struct vop2_video_port *vp, uint32_t base_vcnt)
{
	struct vop2 *vop2 = vp->vop2;
	uint32_t vcnt;
	int ret;

	/*
	 * Spin until vcnt wraps around(from a big value to a little value)
	 */
	ret = readx_poll_timeout_atomic(vop2_read_vcnt, vp, vcnt,
					vcnt < base_vcnt, 0, 20 * 1000);
	if (ret)
		DRM_DEV_ERROR(vop2->dev, "wait fs for vp%d timeout: %d--->%d\n",
			      vp->id, base_vcnt, vcnt);
}

static inline void vop2_cfg_done(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2_video_port *done_vp;
	struct drm_display_mode *adjusted_mode;
	struct vop2 *vop2 = vp->vop2;
	uint32_t done_bits;
	uint32_t vp_id;
	uint32_t vcnt;
	uint32_t val;

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
	done_bits = vop2_readl(vop2, RK3568_REG_CFG_DONE) & 0x7;
	/* we have some vp wait for config done take effect */
	if (done_bits) {
		vp_id = ffs(done_bits) - 1;
		/* no need to wait for same vp */
		if (vp_id != vp->id) {
			done_vp = &vop2->vps[vp_id];
			adjusted_mode = &done_vp->crtc.state->adjusted_mode;
			vcnt = vop2_read_vcnt(done_vp);
			/* if close to the last 1/8 frame, wait to next frame */
			if (vcnt > (adjusted_mode->crtc_vtotal * 7 >> 3)) {
				vop2_wait_for_fs_by_vcnt(done_vp, vcnt);
				done_bits = 0;
			}
		}
	}
	val = RK3568_VOP2_GLB_CFG_DONE_EN | BIT(vp->id) | done_bits;
	vop2_writel(vop2, 0, val);
}

static inline void vop2_wb_cfg_done(struct vop2 *vop2)
{
	uint32_t val = RK3568_VOP2_WB_CFG_DONE | (RK3568_VOP2_WB_CFG_DONE << 16);

	vop2_writel(vop2, 0, val);
}

static void vop2_win_disable(struct vop2_win *win)
{
	struct vop2 *vop2 = win->vop2;

	VOP_WIN_SET(vop2, win, enable, 0);
	if (win->feature & WIN_FEATURE_CLUSTER_MAIN)
		VOP_CLUSTER_SET(vop2, win, enable, 0);
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
		return VOP2_FMT_YUV420SP;
	case DRM_FORMAT_NV12_10:
		return VOP2_FMT_YUV420SP_10;
	case DRM_FORMAT_NV16:
		return VOP2_FMT_YUV422SP;
	case DRM_FORMAT_NV16_10:
		return VOP2_FMT_YUV422SP_10;
	case DRM_FORMAT_NV24:
		return VOP2_FMT_YUV444SP;
	case DRM_FORMAT_NV24_10:
		return VOP2_FMT_YUV444SP_10;
	case DRM_FORMAT_YUYV:
		return VOP2_FMT_YUYV422;
	default:
		DRM_ERROR("unsupported format[%08x]\n", format);
		return -EINVAL;
	}
}

static enum vop2_afbc_format vop2_convert_afbc_format(uint32_t format)
{
	switch (format) {
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
	case DRM_FORMAT_NV12:
		return VOP2_AFBC_FMT_YUV420;
	case DRM_FORMAT_NV12_10:
		return VOP2_AFBC_FMT_YUV420_10BIT;
	case DRM_FORMAT_NV16:
		return VOP2_AFBC_FMT_YUV422;
	case DRM_FORMAT_NV16_10:
		return VOP2_AFBC_FMT_YUV422_10BIT;

		/* either of the below should not be reachable */
	default:
		DRM_WARN_ONCE("unsupported AFBC format[%08x]\n", format);
		return VOP2_AFBC_FMT_INVALID;
	}

	return VOP2_AFBC_FMT_INVALID;
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
	case DRM_FORMAT_NV24_10:
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
	case DRM_FORMAT_NV12_10:
	case DRM_FORMAT_NV16_10:
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
	case DRM_FORMAT_NV12_10:
	case DRM_FORMAT_NV16_10:
	case DRM_FORMAT_NV24_10:
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

static bool is_yuv_support(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV12_10:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV16_10:
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV24_10:
	case DRM_FORMAT_YUYV:
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

	if (!rockchip_afbc(plane, modifier)) {
		DRM_ERROR("Unsupported format modifier 0x%llx\n", modifier);

		return false;
	}

	return vop2_convert_afbc_format(format) >= 0;
}

static inline bool vop2_cluster_window(struct vop2_win *win)
{
	return  (win->feature & (WIN_FEATURE_CLUSTER_MAIN | WIN_FEATURE_CLUSTER_SUB));
}

static int vop2_afbc_half_block_enable(struct vop2_plane_state *vpstate)
{
	if (vpstate->rotate_270_en || vpstate->rotate_90_en)
		return 0;
	else
		return 1;
}

static uint32_t vop2_afbc_transform_offset(struct vop2_plane_state *vpstate)
{
	struct drm_rect *src = &vpstate->src;
	struct drm_framebuffer *fb = vpstate->base.fb;
	uint32_t bpp = fb->format->bpp[0];
	uint32_t vir_width = (fb->pitches[0] << 3) / bpp;
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
 * gt2 enable: dst get one line from two line of the src
 * gt4 enable: dst get one line from four line of the src.
 *
 */
#define VOP2_BILI_SCL_DN(src, dst)	(((src - 1) << 12) / (dst - 1))
#define VOP2_COMMON_SCL(src, dst)	(((src - 1) << 16) / (dst - 1))

#define VOP2_BILI_SCL_FAC_CHECK(src, dst, fac)	 \
				(fac * (dst - 1) >> 12 < (src - 1))
#define VOP2_COMMON_SCL_FAC_CHECK(src, dst, fac) \
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
			DRM_WARN("down fac cali: src:%d, dst:%d, fac:0x%x\n", src, dst, fac);
		}
	} else {
		fac = VOP2_COMMON_SCL(src, dst);
		for (i = 0; i < 100; i++) {
			if (VOP2_COMMON_SCL_FAC_CHECK(src, dst, fac))
				break;
			fac -= 1;
			DRM_WARN("up fac cali:  src:%d, dst:%d, fac:0x%x\n", src, dst, fac);
		}
	}

	return fac;
}

static void vop2_setup_scale(struct vop2 *vop2, const struct vop2_win *win,
			     uint32_t src_w, uint32_t src_h, uint32_t dst_w,
			     uint32_t dst_h, uint32_t pixel_format)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_win_data *win_data = &vop2_data->win[win->win_id];
	const struct drm_format_info *info;
	int hsub = drm_format_horz_chroma_subsampling(pixel_format);
	int vsub = drm_format_vert_chroma_subsampling(pixel_format);
	uint16_t cbcr_src_w = src_w / hsub;
	uint16_t cbcr_src_h = src_h / vsub;
	uint16_t yrgb_hor_scl_mode, yrgb_ver_scl_mode;
	uint16_t cbcr_hor_scl_mode, cbcr_ver_scl_mode;
	uint16_t hscl_filter_mode, vscl_filter_mode;
	uint8_t gt2 = 0;
	uint8_t gt4 = 0;
	uint32_t val;

	info = drm_format_info(pixel_format);

	if (src_h >= (4 * dst_h))
		gt4 = 1;
	else if (src_h >= (2 * dst_h))
		gt2 = 1;

	if (gt4)
		src_h >>= 2;
	else if (gt2)
		src_h >>= 1;

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
	if (!(win->feature & WIN_FEATURE_AFBDC)) {
		if ((yrgb_hor_scl_mode == SCALE_DOWN) && (dst_w & 0x1)) {
			DRM_WARN("%s dst_w[%d] should align as 2 pixel\n", win->name, dst_w);
			dst_w += 1;
		}
	}

	val = vop2_scale_factor(yrgb_hor_scl_mode, hscl_filter_mode,
				src_w, dst_w);
	VOP_SCL_SET(vop2, win, scale_yrgb_x, val);
	val = vop2_scale_factor(yrgb_ver_scl_mode, vscl_filter_mode,
				src_h, dst_h);
	VOP_SCL_SET(vop2, win, scale_yrgb_y, val);

	VOP_SCL_SET(vop2, win, vsd_yrgb_gt4, gt4);
	VOP_SCL_SET(vop2, win, vsd_yrgb_gt2, gt2);

	VOP_SCL_SET(vop2, win, yrgb_hor_scl_mode, yrgb_hor_scl_mode);
	VOP_SCL_SET(vop2, win, yrgb_ver_scl_mode, yrgb_ver_scl_mode);

	VOP_SCL_SET(vop2, win, yrgb_hscl_filter_mode, hscl_filter_mode);
	VOP_SCL_SET(vop2, win, yrgb_vscl_filter_mode, vscl_filter_mode);

	if (info->is_yuv) {
		gt4 = gt2 = 0;

		if (cbcr_src_h >= (4 * dst_h))
			gt4 = 1;
		else if (cbcr_src_h >= (2 * dst_h))
			gt2 = 1;

		if (gt4)
			cbcr_src_h >>= 2;
		else if (gt2)
			cbcr_src_h >>= 1;

		cbcr_hor_scl_mode = scl_get_scl_mode(cbcr_src_w, dst_w);
		cbcr_ver_scl_mode = scl_get_scl_mode(cbcr_src_h, dst_h);

		val = vop2_scale_factor(cbcr_hor_scl_mode, hscl_filter_mode,
					cbcr_src_w, dst_w);
		VOP_SCL_SET(vop2, win, scale_cbcr_x, val);
		val = vop2_scale_factor(cbcr_ver_scl_mode, vscl_filter_mode,
					cbcr_src_h, dst_h);
		VOP_SCL_SET(vop2, win, scale_cbcr_y, val);

		VOP_SCL_SET(vop2, win, vsd_cbcr_gt4, gt4);
		VOP_SCL_SET(vop2, win, vsd_cbcr_gt2, gt2);
		VOP_SCL_SET(vop2, win, cbcr_hor_scl_mode, cbcr_hor_scl_mode);
		VOP_SCL_SET(vop2, win, cbcr_ver_scl_mode, cbcr_ver_scl_mode);
		VOP_SCL_SET(vop2, win, cbcr_hscl_filter_mode, hscl_filter_mode);
		VOP_SCL_SET(vop2, win, cbcr_vscl_filter_mode, vscl_filter_mode);
	}
}

static int vop2_convert_csc_mode(int csc_mode)
{
	switch (csc_mode) {
	case V4L2_COLORSPACE_SMPTE170M:
	case V4L2_COLORSPACE_470_SYSTEM_M:
	case V4L2_COLORSPACE_470_SYSTEM_BG:
		return CSC_BT601L;
	case V4L2_COLORSPACE_REC709:
	case V4L2_COLORSPACE_SMPTE240M:
	case V4L2_COLORSPACE_DEFAULT:
		return CSC_BT709L;
	case V4L2_COLORSPACE_JPEG:
		return CSC_BT601F;
	case V4L2_COLORSPACE_BT2020:
		return CSC_BT2020;
	default:
		return CSC_BT709L;
	}
}

static bool vop2_is_allwin_disabled(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct drm_plane *plane;
	struct vop2_win *win;

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		win = to_vop2_win(plane);
		if (VOP_WIN_GET(vop2, win, enable) != 0)
			return false;
	}

	return true;
}

static void vop2_disable_all_planes_for_crtc(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct drm_plane *plane;
	struct vop2_win *win;
	bool active;
	int ret;

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		win = to_vop2_win(plane);
		vop2_win_disable(win);
	}
	vop2_cfg_done(crtc);
	ret = readx_poll_timeout_atomic(vop2_is_allwin_disabled, crtc,
					active, active, 0, 500 * 1000);
	if (ret)
		DRM_DEV_ERROR(vop2->dev, "wait win close timeout\n");
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
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(vp->crtc.state);
	int is_input_yuv = is_yuv_support(pstate->fb->format->format);
	int is_output_yuv = vcstate->yuv_overlay;
	int input_csc = vpstate->color_space;
	int output_csc = vcstate->color_space;

	vpstate->y2r_en = 0;
	vpstate->r2y_en = 0;
	vpstate->csc_mode = 0;

	/* hdr2sdr and sdr2hdr will do csc itself */
	if (vpstate->hdr2sdr_en) {
	/* This is hdr2sdr enabled plane */
		return;
	} else if (!vpstate->hdr_in && vp->sdr2hdr_en) {
	/* This is sdr2hdr enabled plane */
		return;
	}

	if (is_input_yuv && !is_output_yuv) {
		vpstate->y2r_en = 1;
		vpstate->csc_mode = vop2_convert_csc_mode(input_csc);
	} else if (!is_input_yuv && is_output_yuv) {
		vpstate->r2y_en = 1;
		vpstate->csc_mode = vop2_convert_csc_mode(output_csc);
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

	return 0;

err_disable_hclk:
	clk_disable(vop2->hclk);
	return ret;
}

static void vop2_core_clks_disable(struct vop2 *vop2)
{
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

static int vop2_wb_encoder_atomic_check(struct drm_encoder *encoder,
			       struct drm_crtc_state *cstate,
			       struct drm_connector_state *conn_state)
{
	struct vop2_wb_connector_state *wb_state = to_wb_state(conn_state);
	struct drm_crtc *crtc = cstate->crtc;
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_framebuffer *fb;

	if (!conn_state->writeback_job || !conn_state->writeback_job->fb)
		return 0;

	fb = conn_state->writeback_job->fb;
	DRM_DEV_DEBUG(vp->vop2->dev, "%d x % d\n", fb->width, fb->height);
	if ((fb->width > cstate->mode.hdisplay) ||
	    ((fb->height != cstate->mode.vdisplay) &&
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
	wb_state->yrgb_addr = rockchip_fb_get_dma_addr(fb, 0);
	if (fb->format->is_yuv) {
		wb_state->uv_addr = rockchip_fb_get_dma_addr(fb, 1);
		wb_state->uv_addr += fb->offsets[1];
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


static int vop2_wb_connector_init(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	int ret;

	vop2->wb.regs = vop2_data->wb->regs;
	vop2->wb.conn.encoder.possible_crtcs = (1 << vop2_data->nr_vps) - 1;
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
	uint32_t fifo_throd;
	uint8_t r2y;

	if (!conn_state)
		return;
	wb_state = to_wb_state(conn_state);

	if (wb_state->vp_id != vp->id)
		return;

	if (conn_state->writeback_job && conn_state->writeback_job->fb) {
		struct drm_framebuffer *fb = conn_state->writeback_job->fb;

		DRM_DEV_DEBUG(vop2->dev, "Enable wb %ux%u  fmt: %u pitches: %d\n",
			      fb->width, fb->height, wb_state->format, fb->pitches[0]);

		drm_writeback_queue_job(wb_conn, conn_state->writeback_job);
		conn_state->writeback_job = NULL;

		fifo_throd = fb->pitches[0] >> 4;
		r2y = is_yuv_support(fb->format->format) && (!is_yuv_output(vcstate->bus_format));

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
		vp->wb_en = true;
		vp->fs_vsync_cnt = 0;
	}
}


static void vop2_crtc_load_lut(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	int dle = 0, i = 0;

	if (!vop2->is_enabled || !vop2->lut || !vop2->lut_regs)
		return;

	if (WARN_ON(!drm_modeset_is_locked(&crtc->mutex)))
		return;

	spin_lock(&vop2->reg_lock);
	VOP_MODULE_SET(vop2, vp, dsp_lut_en, 0);
	vop2_cfg_done(crtc);
	spin_unlock(&vop2->reg_lock);

#define CTRL_GET(name) VOP_MODULE_GET(vop2, vp, name)
	readx_poll_timeout(CTRL_GET, dsp_lut_en, dle, !dle, 5, 33333);

	for (i = 0; i < vop2->lut_len; i++)
		vop2_write_lut(vop2, i << 2, vop2->lut[i]);

	spin_lock(&vop2->reg_lock);

	VOP_MODULE_SET(vop2, vp, dsp_lut_en, 1);
	VOP_CTRL_SET(vop2, gamma_port_sel, vp->id);
	vop2_cfg_done(crtc);
	vop2->lut_active = true;

	spin_unlock(&vop2->reg_lock);
#undef CTRL_GET
}

static void rockchip_vop2_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red,
					    u16 green, u16 blue, int regno)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	u32 lut_len = vop2->lut_len;
	u32 r, g, b;

	if (regno >= lut_len || !vop2->lut)
		return;

	r = red * (lut_len - 1) / 0xffff;
	g = green * (lut_len - 1) / 0xffff;
	b = blue * (lut_len - 1) / 0xffff;
	vop2->lut[regno] = r * lut_len * lut_len + g * lut_len + b;
}

static void rockchip_vop2_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red,
				       u16 *green, u16 *blue, int regno)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	u32 lut_len = vop2->lut_len;
	u32 r, g, b;

	if (regno >= lut_len || !vop2->lut)
		return;

	r = (vop2->lut[regno] / lut_len / lut_len) & (lut_len - 1);
	g = (vop2->lut[regno] / lut_len) & (lut_len - 1);
	b = vop2->lut[regno] & (lut_len - 1);
	*red = r * 0xffff / (lut_len - 1);
	*green = g * 0xffff / (lut_len - 1);
	*blue = b * 0xffff / (lut_len - 1);
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

	return 0;
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
static void vop2_layer_map_initial(struct vop2 *vop2, uint32_t current_vp_id)
{
	const struct vop2_data *vop2_data = vop2->data;
	struct vop2_layer *layer = &vop2->layers[0];
	struct vop2_video_port *vp = &vop2->vps[0];
	struct vop2_video_port *current_vp = &vop2->vps[current_vp_id];
	struct vop2_win *win;
	const struct vop2_win_data *win_data = NULL;
	uint32_t used_layers = 0;
	uint32_t layer_map, sel;
	uint32_t win_map, vp_id;
	uint32_t port_mux;
	uint32_t standby;
	uint32_t shift;
	int i, j;

	layer_map = vop2_readl(vop2, layer->regs->layer_sel.offset);
	win_map = vop2_readl(vop2, vp->regs->port_mux.offset);
	current_vp->win_mask = 0;
	for (i = 0; i < vop2->data->nr_vps; i++) {
		vp = &vop2->vps[i];
		vp->win_mask = 0;
		standby = vop2_readl(vop2, vp->regs->standby.offset);
		shift = vp->regs->standby.shift;
		standby = (standby >> shift) & 0x1;
		for (j = 0; j < vop2_data->nr_layers; j++) {
			win = vop2_find_win_by_phys_id(vop2, j);
			shift = vop2->data->ctrl->win_vp_id[j].shift;
			vp_id = (win_map >> shift) & 0x3;
			if (vp_id == i) {
				/*
				 * If the Video Port is not activated,
				 * move the attached win to the Video Port
				 * we want enabled.
				 */
				if (standby) {
					current_vp->win_mask |=  BIT(j);
					win->vp_mask = BIT(current_vp_id);
					VOP_CTRL_SET(vop2, win_vp_id[win->phys_id], current_vp_id);
				} else {
					vp->win_mask |=  BIT(j);
					win->vp_mask = BIT(vp_id);
				}
			}
		}
	}

	/*
	 * The last Video Port(VP2 for RK3568, VP3 for RK3588) is fixed
	 * at the last level of the all the mixers by hardware design,
	 * so we just need to handle (nr_vps - 1) vps here.
	 */
	for (i = 0; i < vop2->data->nr_vps - 1; i++) {
		vp = &vop2->vps[i];
		used_layers += hweight32(vp->win_mask);
		if (used_layers == 0)
			port_mux = 8;
		else
			port_mux = used_layers - 1;
		VOP_MODULE_SET(vop2, vp, port_mux, port_mux);
	}

	for (i = 0; i < vop2->data->nr_layers; i++) {
		sel = (layer_map >> (4 * i)) & 0xf;
		layer = &vop2->layers[i];
		for (j = 0; j < vop2_data->win_size; j++) {
			win_data = &vop2_data->win[j];
			if (sel == win_data->layer_sel_id)
				break;
			win_data = NULL;
		}

		if (!win_data) {
			DRM_DEV_ERROR(vop2->dev, "invalid layer map :0x%x\n", layer_map);
			return;
		}
		win = vop2_find_win_by_phys_id(vop2, win_data->phys_id);
		layer->win_phys_id = win->phys_id;
		win->layer_id = layer->id;
		DRM_DEV_DEBUG(vop2->dev, "layer%d select %s\n", layer->id, win->name);
	}
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

		memcpy(vop2->regsbak, vop2->regs, vop2->len);

		VOP_MODULE_SET(vop2, wb, axi_yrgb_id, 0xd);
		VOP_MODULE_SET(vop2, wb, axi_uv_id, 0xe);
		vop2_wb_cfg_done(vop2);

		VOP_CTRL_SET(vop2, cfg_done_en, 1);
		/*
		 * Disable auto gating, this is a workaround to
		 * avoid display image shift when a window enabled.
		 */
		VOP_CTRL_SET(vop2, auto_gating_en, 0);
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

static void vop2_disable(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	clk_disable_unprepare(vp->dclk);

	if (--vop2->enable_count > 0)
		return;

	vop2->is_enabled = false;
	if (vop2->is_iommu_enabled) {
		/*
		 * vop2 standby complete, so iommu detach is safe.
		 */
		VOP_CTRL_SET(vop2, dma_stop, 1);
		rockchip_drm_dma_detach_device(vop2->drm_dev, vop2->dev);
		vop2->is_iommu_enabled = false;
	}

	pm_runtime_put_sync(vop2->dev);

	clk_disable_unprepare(vop2->aclk);
	clk_disable_unprepare(vop2->hclk);
}

static void vop2_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	WARN_ON(vp->event);
	vop2_lock(vop2);
	drm_crtc_vblank_off(crtc);
	vop2_disable_all_planes_for_crtc(crtc);

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

	VOP_MODULE_SET(vop2, vp, standby, 1);

	spin_unlock(&vop2->reg_lock);

	WARN_ON(!wait_for_completion_timeout(&vp->dsp_hold_completion, msecs_to_jiffies(50)));

	vop2_dsp_hold_valid_irq_disable(crtc);

	vop2_disable(crtc);
	vop2_unlock(vop2);

	vop2->active_vp_mask &= ~BIT(vp->id);
	vop2_set_system_status(vop2);

	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}
}

static int vop2_plane_prepare_fb(struct drm_plane *plane,
				 struct drm_plane_state *new_state)
{
	if (plane->state->fb)
		drm_framebuffer_get(plane->state->fb);

	return 0;
}

static void vop2_plane_cleanup_fb(struct drm_plane *plane,
				  struct drm_plane_state *old_state)
{
	if (old_state->fb)
		drm_framebuffer_put(old_state->fb);
}

static int vop2_plane_atomic_check(struct drm_plane *plane, struct drm_plane_state *state)
{
	struct vop2_plane_state *vpstate = to_vop2_plane_state(state);
	struct vop2_win *win = to_vop2_win(plane);
	struct drm_framebuffer *fb = state->fb;
	struct drm_crtc *crtc = state->crtc;
	struct drm_crtc_state *cstate;
	struct vop2_video_port *vp;
	const struct vop2_data *vop2_data;
	struct drm_rect *dest = &vpstate->dest;
	struct drm_rect *src = &vpstate->src;
	int min_scale = win->regs->scl ? FRAC_16_16(1, 8) : DRM_PLANE_HELPER_NO_SCALING;
	int max_scale = win->regs->scl ? FRAC_16_16(8, 1) : DRM_PLANE_HELPER_NO_SCALING;
	unsigned long offset;
	dma_addr_t dma_addr;
	void *kvaddr;
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

	vpstate->xmirror_en = (state->rotation & DRM_MODE_REFLECT_X) ? 1 : 0;
	vpstate->ymirror_en = (state->rotation & DRM_MODE_REFLECT_Y) ? 1 : 0;
	vpstate->rotate_270_en = (state->rotation & DRM_MODE_ROTATE_270) ? 1 : 0;
	vpstate->rotate_90_en = (state->rotation & DRM_MODE_ROTATE_90) ? 1 : 0;

	if (vpstate->rotate_270_en && vpstate->rotate_90_en) {
		DRM_ERROR("Can't rotate 90 and 270 at the same time\n");
		return -EINVAL;
	}

	src->x1 = state->src_x;
	src->y1 = state->src_y;
	src->x2 = state->src_x + state->src_w;
	src->y2 = state->src_y + state->src_h;
	dest->x1 = state->crtc_x;
	dest->y1 = state->crtc_y;

	dest->x2 = state->crtc_x + state->crtc_w;
	dest->y2 = state->crtc_y + state->crtc_h;

	ret = drm_atomic_helper_check_plane_state(state, cstate,
						  min_scale, max_scale,
						  true, true);
	if (ret)
		return ret;

	if (!state->visible)
		return 0;

	vpstate->zpos = state->zpos;
	vpstate->global_alpha = state->alpha >> 8;
	vpstate->blend_mode = state->pixel_blend_mode;
	vpstate->format = vop2_convert_format(fb->format->format);
	if (vpstate->format < 0)
		return vpstate->format;
	if (drm_rect_width(src) >> 16 > vop2_data->max_input.width ||
	    drm_rect_height(src) >> 16 > vop2_data->max_input.height) {
		DRM_ERROR("Invalid source: %dx%d. max input: %dx%d\n",
			  drm_rect_width(src) >> 16,
			  drm_rect_height(src) >> 16,
			  vop2_data->max_input.width,
			  vop2_data->max_input.height);
		return -EINVAL;
	}

	if (rockchip_afbc(plane, fb->modifier))
		vpstate->afbc_en = true;
	else
		vpstate->afbc_en = false;

	/*
	 * Src.x1 can be odd when do clip, but yuv plane start point
	 * need align with 2 pixel.
	 */
	if (fb->format->is_yuv && ((state->src.x1 >> 16) % 2)) {
		DRM_ERROR("Invalid Source: Yuv format not support odd xpos\n");
		return -EINVAL;
	}

	offset = (src->x1 >> 16) * fb->format->bpp[0] / 8;
	vpstate->offset = offset + fb->offsets[0];

	/*
	 * AFBC HDR_PTR must set to the zero offset of the framebuffer.
	 */
	if (vpstate->afbc_en)
		offset = 0;
	else if (vpstate->ymirror_en)
		offset += ((src->y2 >> 16) - 1) * fb->pitches[0];
	else
		offset += (src->y1 >> 16) * fb->pitches[0];

	dma_addr = rockchip_fb_get_dma_addr(fb, 0);
	kvaddr = rockchip_fb_get_kvaddr(fb, 0);

	vpstate->yrgb_mst = dma_addr + offset + fb->offsets[0];
	vpstate->yrgb_kvaddr = kvaddr + offset + fb->offsets[0];
	if (fb->format->is_yuv) {
		int hsub = drm_format_horz_chroma_subsampling(fb->format->format);
		int vsub = drm_format_vert_chroma_subsampling(fb->format->format);

		offset = (src->x1 >> 16) * fb->format->bpp[1] / hsub / 8;
		offset += (src->y1 >> 16) * fb->pitches[1] / vsub;

		dma_addr = rockchip_fb_get_dma_addr(fb, 1);
		dma_addr += offset + fb->offsets[1];
		vpstate->uv_mst = dma_addr;
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

	DRM_DEV_DEBUG(vop2->dev, "%s disable\n", win->name);

	if (!old_state->crtc)
		return;

	spin_lock(&vop2->reg_lock);

	vop2_win_disable(win);

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

	if (!(vpstate->color_key & VOP2_COLOR_KEY_MASK) || fb->format->is_yuv) {
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

	color_key = (r << 20) || (g << 10) || b;
	VOP_WIN_SET(vop2, win, color_key_en, color_key_en);
	VOP_WIN_SET(vop2, win, color_key, color_key);
}

static void vop2_plane_atomic_update(struct drm_plane *plane, struct drm_plane_state *old_state)
{
	struct drm_plane_state *pstate = plane->state;
	struct drm_crtc *crtc = pstate->crtc;
	struct vop2_win *win = to_vop2_win(plane);
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2_plane_state *vpstate = to_vop2_plane_state(pstate);
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	struct vop2 *vop2 = win->vop2;
	struct drm_framebuffer *fb = pstate->fb;
	uint32_t bpp = fb->format->bpp[0];
	uint32_t actual_w, actual_h, dsp_w, dsp_h;
	uint32_t dsp_stx, dsp_sty;
	uint32_t act_info, dsp_info, dsp_st;
	uint32_t format;
	uint32_t afbc_format;
	uint32_t rb_swap;
	uint32_t uv_swap;
	struct drm_rect *src = &vpstate->src;
	struct drm_rect *dest = &vpstate->dest;
	uint32_t afbc_tile_num;
	uint32_t afbc_half_block_en;
	uint32_t lb_mode;
	uint32_t stride;
	uint32_t transform_offset;

#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	bool AFBC_flag = false;
	struct vop_dump_list *planlist;
	unsigned long num_pages;
	struct page **pages;
	struct rockchip_drm_fb *rk_fb;
	struct drm_gem_object *obj;
	struct rockchip_gem_object *rk_obj;

	num_pages = 0;
	pages = NULL;
	rk_fb = to_rockchip_fb(fb);
	obj = rk_fb->obj[0];
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

	actual_w = drm_rect_width(src) >> 16;
	actual_h = drm_rect_height(src) >> 16;
	dsp_w = drm_rect_width(dest);
	if (dest->x1 + dsp_w > adjusted_mode->hdisplay) {
		DRM_ERROR("%s dest->x1[%d] + dsp_w[%d] exceed mode hdisplay[%d]\n",
			  win->name, dest->x1, dsp_w, adjusted_mode->hdisplay);
		dsp_w = adjusted_mode->hdisplay - dest->x1;
	}
	dsp_h = drm_rect_height(dest);
	if (dest->y1 + dsp_h > adjusted_mode->vdisplay) {
		DRM_ERROR("%s dest->y1[%d] + dsp_h[%d] exceed mode vdisplay[%d]\n",
			  win->name, dest->y1, dsp_h, adjusted_mode->vdisplay);
		dsp_h = adjusted_mode->vdisplay - dest->y1;
	}

	/*
	 * This is workaround solution for IC design:
	 * esmart can't support scale down when actual_w % 16 == 1.
	 */
	if (!(win->feature & WIN_FEATURE_AFBDC)) {
		if (actual_w > dsp_w && (actual_w & 0xf) == 1) {
			DRM_WARN("%s act_w[%d] MODE 16 == 1\n", win->name, actual_w);
			actual_w -= 1;
		}
	}

	if (vpstate->afbc_en && actual_w % 4) {
		DRM_ERROR("%s actual_w[%d] should align as 4 pixel when enable afbc\n",
			  win->name, actual_w);
		actual_w = ALIGN_DOWN(actual_w, 4);
	}

	act_info = (actual_h - 1) << 16 | ((actual_w - 1) & 0xffff);
	dsp_info = (dsp_h - 1) << 16 | ((dsp_w - 1) & 0xffff);
	stride = DIV_ROUND_UP(fb->pitches[0], 4);
	dsp_stx = dest->x1;
	dsp_sty = dest->y1;
	dsp_st = dsp_sty << 16 | (dsp_stx & 0xffff);

	format = vop2_convert_format(fb->format->format);

	vop2_setup_csc_mode(vp, vpstate);

	spin_lock(&vop2->reg_lock);

	if (vpstate->afbc_en) {
		/* the afbc superblock is 16 x 16 */
		afbc_format = vop2_convert_afbc_format(fb->format->format);
		/* Enable color transform for YTR */
		if (fb->modifier & AFBC_FORMAT_MOD_YTR)
			afbc_format |= (1 << 4);
		afbc_tile_num = ALIGN(actual_w, 16) >> 4;
		/* AFBC pic_vir_width is count by pixel, this is different
		 * with WIN_VIR_STRIDE.
		 */
		stride = (fb->pitches[0] << 3) / bpp;
		if ((stride & 0x3f) &&
		    (vpstate->xmirror_en || vpstate->rotate_90_en || vpstate->rotate_270_en))
			DRM_ERROR("%s stride[%d] must align as 64 pixel when enable xmirror/rotate_90/rotate_270[0x%x]\n",
				  win->name, stride, pstate->rotation);

		rb_swap = vop2_afbc_rb_swap(fb->format->format);
		uv_swap = vop2_afbc_uv_swap(fb->format->format);
		/*
		 * This is a workaround for crazy IC design, Cluster
		 * and Esmart/Smart use different format configuration map:
		 * YUV420_10BIT: 0x10 for Cluster, 0x14 for Esmart/Smart.
		 *
		 * This is one thing we can make the convert simple:
		 * AFBCD decode all the YUV data to YUV444. So we just
		 * set all the yuv 10 bit to YUV444_10.
		 */
		if (fb->format->is_yuv && (bpp == 10))
			format = VOP2_CLUSTER_YUV444_10;

		afbc_half_block_en = vop2_afbc_half_block_enable(vpstate);
		vpstate->afbc_half_block_en = afbc_half_block_en;
		transform_offset = vop2_afbc_transform_offset(vpstate);
		VOP_CLUSTER_SET(vop2, win, afbc_enable, 1);
		VOP_AFBC_SET(vop2, win, format, afbc_format);
		VOP_AFBC_SET(vop2, win, rb_swap, rb_swap);
		VOP_AFBC_SET(vop2, win, uv_swap, uv_swap);
		VOP_AFBC_SET(vop2, win, auto_gating_en, 0);
		VOP_AFBC_SET(vop2, win, block_split_en, 0);
		VOP_AFBC_SET(vop2, win, half_block_en, afbc_half_block_en);
		VOP_AFBC_SET(vop2, win, hdr_ptr, vpstate->yrgb_mst);
		VOP_AFBC_SET(vop2, win, pic_size, act_info);
		VOP_AFBC_SET(vop2, win, transform_offset, transform_offset);
		VOP_AFBC_SET(vop2, win, pic_offset, ((src->x1 >> 16) | src->y1));
		VOP_AFBC_SET(vop2, win, dsp_offset, (dest->x1 | (dest->y1 << 16)));
		VOP_AFBC_SET(vop2, win, pic_vir_width, stride);
		VOP_AFBC_SET(vop2, win, tile_num, afbc_tile_num);
		VOP_AFBC_SET(vop2, win, xmirror, vpstate->xmirror_en);
		VOP_AFBC_SET(vop2, win, ymirror, vpstate->ymirror_en);
		VOP_AFBC_SET(vop2, win, rotate_270, vpstate->rotate_270_en);
		VOP_AFBC_SET(vop2, win, rotate_90, vpstate->rotate_90_en);
	} else {
		VOP_AFBC_SET(vop2, win, enable, 0);
		VOP_WIN_SET(vop2, win, ymirror, vpstate->ymirror_en);
		VOP_WIN_SET(vop2, win, xmirror, vpstate->xmirror_en);
	}

	if (vpstate->rotate_90_en || vpstate->rotate_270_en) {
		act_info = swahw32(act_info);
		actual_w = drm_rect_height(src) >> 16;
		actual_h = drm_rect_width(src) >> 16;
	}

	VOP_WIN_SET(vop2, win, format, format);
	/* win->yrgb_vir only take effect at non-afbc mode */
	VOP_WIN_SET(vop2, win, yrgb_vir, stride);
	VOP_WIN_SET(vop2, win, yrgb_mst, vpstate->yrgb_mst);

	rb_swap = vop2_win_rb_swap(fb->format->format);
	uv_swap = vop2_win_uv_swap(fb->format->format);
	VOP_WIN_SET(vop2, win, rb_swap, rb_swap);
	VOP_WIN_SET(vop2, win, uv_swap, uv_swap);

	if (fb->format->is_yuv) {
		VOP_WIN_SET(vop2, win, uv_vir, DIV_ROUND_UP(fb->pitches[1], 4));
		VOP_WIN_SET(vop2, win, uv_mst, vpstate->uv_mst);
	}

	vop2_setup_scale(vop2, win, actual_w, actual_h, dsp_w, dsp_h, fb->format->format);
	vop2_plane_setup_color_key(plane);
	VOP_WIN_SET(vop2, win, act_info, act_info);
	VOP_WIN_SET(vop2, win, dsp_info, dsp_info);
	VOP_WIN_SET(vop2, win, dsp_st, dsp_st);

	VOP_WIN_SET(vop2, win, y2r_en, vpstate->y2r_en);
	VOP_WIN_SET(vop2, win, r2y_en, vpstate->r2y_en);
	VOP_WIN_SET(vop2, win, csc_mode, vpstate->csc_mode);

	VOP_WIN_SET(vop2, win, enable, 1);
	if (vop2_cluster_window(win)) {
		lb_mode = vop2_get_cluster_lb_mode(win, vpstate);
		VOP_CLUSTER_SET(vop2, win, lb_mode, lb_mode);
		VOP_CLUSTER_SET(vop2, win, enable, 1);
	}
	spin_unlock(&vop2->reg_lock);

	vop2->is_iommu_needed = true;
#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	kfree(vpstate->planlist);
	vpstate->planlist = NULL;

	planlist = kmalloc(sizeof(*planlist), GFP_KERNEL);
	if (planlist) {
		planlist->dump_info.AFBC_flag = AFBC_flag;
		planlist->dump_info.area_id = win->area_id;
		planlist->dump_info.win_id = win->win_id;
		planlist->dump_info.yuv_format = is_yuv_support(fb->format->format);
		planlist->dump_info.num_pages = num_pages;
		planlist->dump_info.pages = pages;
		planlist->dump_info.offset = vpstate->offset;
		planlist->dump_info.pitches = fb->pitches[0];
		planlist->dump_info.height = actual_h;
		planlist->dump_info.pixel_format = fb->format->format;
		list_add_tail(&planlist->entry, &crtc->vop_dump_list_head);
		vpstate->planlist = planlist;
	} else {
		DRM_ERROR("can't alloc a node of planlist %p\n", planlist);
		return;
	}
	if (crtc->vop_dump_status == DUMP_KEEP ||
	    crtc->vop_dump_times > 0) {
		vop_plane_dump(&planlist->dump_info, crtc->frame_count);
		crtc->vop_dump_times--;
	}
#endif
}

static const struct drm_plane_helper_funcs vop2_plane_helper_funcs = {
	.prepare_fb = vop2_plane_prepare_fb,
	.cleanup_fb = vop2_plane_cleanup_fb,
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

	plane->state = &vpstate->base;
	plane->state->plane = plane;
	plane->state->zpos = win->zpos;
	plane->state->alpha = DRM_BLEND_ALPHA_OPAQUE;
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

static int vop2_crtc_loader_protect(struct drm_crtc *crtc, bool on)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	if (on == vop2->loader_protect)
		return 0;

	if (on) {
		vop2->active_vp_mask |= BIT(vp->id);
		vop2_set_system_status(vop2);
		vop2_initial(crtc);
		drm_crtc_vblank_on(crtc);
		vop2->loader_protect = true;
	} else {
		vop2_crtc_atomic_disable(crtc, NULL);
		vop2->loader_protect = false;
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
		    rockchip_afbc(plane, fb->modifier) ? "[AFBC]" : "",
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

	for (i = 0; i < drm_format_num_planes(fb->format->format); i++) {
		dma_addr_t fb_addr = rockchip_fb_get_dma_addr(fb, i);

		DEBUG_PRINT("\tbuf[%d]: addr: %pad pitch: %d offset: %d\n",
			    i, &fb_addr, fb->pitches[i], fb->offsets[i]);
	}

	return 0;
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

	DEBUG_PRINT("    Connector: %s\n",
		    drm_get_connector_name(state->output_type));
	DEBUG_PRINT("\tbus_format[%x]: %s\n", state->bus_format,
		    drm_get_bus_format_name(state->bus_format));
	DEBUG_PRINT("\toverlay_mode[%d] output_mode[%x]",
		    state->yuv_overlay, state->output_mode);
	DEBUG_PRINT(" color_space[%d]\n",
		    state->color_space);
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
	struct drm_crtc_state *cstate = crtc->state;
	uint32_t addr[] = {
		RK3568_REG_CFG_DONE,
		RK3568_OVL_CTRL,
		RK3568_VP0_DSP_CTRL,
		RK3568_VP1_DSP_CTRL,
		RK3568_VP2_DSP_CTRL,
		RK3568_CLUSTER0_WIN0_CTRL0,
		RK3568_CLUSTER1_WIN0_CTRL0,
		RK3568_ESMART0_CTRL0,
		RK3568_ESMART1_CTRL0,
		RK3568_SMART0_CTRL0,
		RK3568_SMART1_CTRL0,
		RK3568_HDR_LUT_CTRL,
	};
	uint32_t buf[68];
	unsigned int len = ARRAY_SIZE(buf);
	unsigned int n, i, j;
	uint32_t base;

	if (!cstate->active)
		return;

	n = sizeof(addr) >> 2;

	for (i = 0; i < n; i++) {
		base = addr[i];
		pr_info("0x%08x:\n", base);
		for (j = 0; j < len; j++)
			buf[j] = vop2_readl(vop2, base + (4 * j));
		print_hex_dump(KERN_ERR, "", DUMP_PREFIX_OFFSET, 16, 4, buf,
			       len << 2, 0);
	}
}

static int vop2_gamma_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct vop2 *vop2 = node->info_ent->data;
	int i;

	if (!vop2->lut || !vop2->lut_active || !vop2->lut_regs)
		return 0;

	for (i = 0; i < vop2->lut_len; i++) {
		if (i % 8 == 0)
			DEBUG_PRINT("\n");
		DEBUG_PRINT("0x%08x ", vop2->lut[i]);
	}
	DEBUG_PRINT("\n");

	return 0;
}

#undef DEBUG_PRINT

static struct drm_info_list vop2_debugfs_files[] = {
	{ "gamma_lut", vop2_gamma_show, 0, NULL },
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
	drm_debugfs_vop_add(crtc, vop2->debugfs);
#endif
	for (i = 0; i < ARRAY_SIZE(vop2_debugfs_files); i++)
		vop2->debugfs_files[i].data = vop2;

	ret = drm_debugfs_create_files(vop2->debugfs_files,
				       ARRAY_SIZE(vop2_debugfs_files),
				       vop2->debugfs,
				       minor);
	if (ret) {
		dev_err(vop2->dev, "could not install rockchip_debugfs_list\n");
		goto free;
	}

	return 0;
free:
	kfree(vop2->debugfs_files);
	vop2->debugfs_files = NULL;
remove:
	debugfs_remove(vop2->debugfs);
	vop2->debugfs = NULL;
	return ret;
}

static enum drm_mode_status
vop2_crtc_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *mode,
		     int output_type)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	int request_clock = mode->clock;
	int clock;

	if (mode->hdisplay > vp_data->max_output.width)
		return MODE_BAD_HVALUE;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		request_clock *= 2;

	clock = clk_round_rate(vp->dclk, request_clock * 1000) / 1000;

	/*
	 * Hdmi or DisplayPort request a Accurate clock.
	 */
	if (output_type == DRM_MODE_CONNECTOR_HDMIA ||
	    output_type == DRM_MODE_CONNECTOR_DisplayPort)
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
	int bpp = fb->format->bpp[0];
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
				  unsigned int *plane_num_total)
{
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	uint16_t htotal = adjusted_mode->crtc_htotal;
	uint16_t vdisplay = adjusted_mode->crtc_vdisplay;
	int clock = adjusted_mode->crtc_clock;
	struct drm_atomic_state *state = crtc_state->state;
	struct vop2_plane_state *vpstate;
	struct drm_plane_state *pstate;
	struct vop2_bandwidth *pbandwidth;
	struct drm_plane *plane;
	uint64_t bandwidth;
	int8_t cnt = 0, plane_num = 0;
#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	struct vop_dump_list *pos, *n;
#endif

	if (!htotal || !vdisplay)
		return 0;

#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	if (!crtc->vop_dump_list_init_flag) {
		INIT_LIST_HEAD(&crtc->vop_dump_list_head);
		crtc->vop_dump_list_init_flag = true;
	}
	list_for_each_entry_safe(pos, n, &crtc->vop_dump_list_head, entry) {
		list_del(&pos->entry);
	}
	if (crtc->vop_dump_status == DUMP_KEEP ||
	    crtc->vop_dump_times > 0) {
		crtc->frame_count++;
	}
#endif

	drm_atomic_crtc_state_for_each_plane(plane, crtc_state)
		plane_num++;

	if (plane_num_total)
		*plane_num_total += plane_num;
	pbandwidth = kmalloc_array(plane_num, sizeof(*pbandwidth),
				   GFP_KERNEL);
	if (!pbandwidth)
		return -ENOMEM;
	drm_atomic_crtc_state_for_each_plane(plane, crtc_state) {
		pstate = drm_atomic_get_new_plane_state(state, plane);
		if (!pstate || pstate->crtc != crtc || !pstate->fb)
			continue;

		vpstate = to_vop2_plane_state(pstate);
		pbandwidth[cnt].y1 = vpstate->dest.y1;
		pbandwidth[cnt].y2 = vpstate->dest.y2;
		pbandwidth[cnt++].bandwidth = vop2_plane_line_bandwidth(pstate);
	}

	sort(pbandwidth, cnt, sizeof(pbandwidth[0]), vop2_bandwidth_cmp, NULL);

	bandwidth = vop2_calc_max_bandwidth(pbandwidth, 0, cnt, vdisplay);
	kfree(pbandwidth);
	/*
	 * bandwidth(MB/s)
	 *    = line_bandwidth / line_time
	 *    = line_bandwidth(Byte) * clock(KHZ) / 1000 / htotal
	 */
	bandwidth *= clock;
	do_div(bandwidth, htotal * 1000);

	return bandwidth;
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

static const struct rockchip_crtc_funcs private_crtc_funcs = {
	.loader_protect = vop2_crtc_loader_protect,
	.cancel_pending_vblank = vop2_crtc_cancel_pending_vblank,
	.debugfs_init = vop2_crtc_debugfs_init,
	.debugfs_dump = vop2_crtc_debugfs_dump,
	.regs_dump = vop2_crtc_regs_dump,
	.mode_valid = vop2_crtc_mode_valid,
	.bandwidth = vop2_crtc_bandwidth,
	.crtc_close = vop2_crtc_close,
};

static bool vop2_crtc_mode_fixup(struct drm_crtc *crtc,
				 const struct drm_display_mode *mode,
				 struct drm_display_mode *adj_mode)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);

	drm_mode_set_crtcinfo(adj_mode, CRTC_INTERLACE_HALVE_V | CRTC_STEREO_DOUBLE);

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		adj_mode->crtc_clock *= 2;

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
		break;
	case MEDIA_BUS_FMT_RGB666_1X18:
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
	case MEDIA_BUS_FMT_RGB666_1X7X3_JEIDA:
		VOP_MODULE_SET(vop2, vp, dither_down_en, 1);
		VOP_MODULE_SET(vop2, vp, dither_down_mode, RGB888_TO_RGB666);
		break;
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
		VOP_MODULE_SET(vop2, vp, dither_down_en, 0);
		VOP_MODULE_SET(vop2, vp, pre_dither_down_en, 1);
		break;
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
		VOP_MODULE_SET(vop2, vp, dither_down_en, 0);
		VOP_MODULE_SET(vop2, vp, pre_dither_down_en, 0);
		break;
	case MEDIA_BUS_FMT_SRGB888_3X8:
	case MEDIA_BUS_FMT_SRGB888_DUMMY_4X8:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
	default:
		VOP_MODULE_SET(vop2, vp, dither_down_en, 0);
		VOP_MODULE_SET(vop2, vp, pre_dither_down_en, 0);
		break;
	}

	VOP_MODULE_SET(vop2, vp, pre_dither_down_en,
		       vcstate->output_mode == ROCKCHIP_OUT_MODE_AAAA ? 0 : 1);
	VOP_MODULE_SET(vop2, vp, dither_down_sel, DITHER_DOWN_ALLEGRO);
}

static void vop2_post_config(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *vcstate =
			to_rockchip_crtc_state(crtc->state);
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
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
	VOP_MODULE_SET(vop2, vp, post_dsp_out_r2y,
		       is_yuv_output(vcstate->bus_format));
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
	u32 vtotal_sync = vtotal << 16 | vsync_len;
	u32 vactive_st_end = vact_st << 16 | vact_end;
	u32 crtc_clock = adjusted_mode->crtc_clock * 100;

	if (htotal_sync != VOP_MODULE_GET(vop2, vp, htotal_pw) ||
	    hactive_st_end != VOP_MODULE_GET(vop2, vp, hact_st_end) ||
	    vtotal_sync != VOP_MODULE_GET(vop2, vp, vtotal_pw) ||
	    vactive_st_end != VOP_MODULE_GET(vop2, vp, vact_st_end) ||
	    crtc_clock != clk_get_rate(vp->dclk))
		return true;

	return false;
}

static void vop2_crtc_atomic_enable(struct drm_crtc *crtc, struct drm_crtc_state *old_state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_intr *intr = vp_data->intr;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
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
	uint8_t out_mode;
	int for_ddr_freq = 0;
	bool dclk_inv, yc_swap = false;
	int act_end;
	uint32_t val;

	vop2->active_vp_mask |= BIT(vp->id);
	vop2_set_system_status(vop2);

	vop2_lock(vop2);
	DRM_DEV_INFO(vop2->dev, "Update mode to %dx%d%s%d, type: %d for vp%d\n",
		     hdisplay, vdisplay, interlaced ? "i" : "p",
		     adjusted_mode->vrefresh, vcstate->output_type, vp->id);
	vop2_initial(crtc);
	vcstate->vdisplay = vdisplay;
	vcstate->mode_update = vop2_crtc_mode_update(crtc);
	if (vcstate->mode_update)
		vop2_disable_all_planes_for_crtc(crtc);
	/*
	 * restore the lut table.
	 */
	if (vop2->lut_active)
		vop2_crtc_load_lut(crtc);

	dclk_inv = (vcstate->bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE) ? 1 : 0;
	val = (adjusted_mode->flags & DRM_MODE_FLAG_NHSYNC) ? 0 : BIT(HSYNC_POSITIVE);
	val |= (adjusted_mode->flags & DRM_MODE_FLAG_NVSYNC) ? 0 : BIT(VSYNC_POSITIVE);

	if (vcstate->output_if & VOP_OUTPUT_IF_RGB) {
		VOP_CTRL_SET(vop2, rgb_en, 1);
		VOP_CTRL_SET(vop2, rgb_mux, vp_data->id);
		VOP_GRF_SET(vop2, grf_dclk_inv, dclk_inv);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_BT1120) {
		VOP_CTRL_SET(vop2, rgb_en, 1);
		VOP_CTRL_SET(vop2, bt1120_en, 1);
		VOP_CTRL_SET(vop2, rgb_mux, vp_data->id);
		VOP_GRF_SET(vop2, grf_bt1120_clk_inv, !dclk_inv);
		yc_swap = vop2_output_yc_swap(vcstate->bus_format);
		VOP_CTRL_SET(vop2, bt1120_yc_swap, yc_swap);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_BT656) {
		VOP_CTRL_SET(vop2, bt656_en, 1);
		VOP_CTRL_SET(vop2, rgb_mux, vp_data->id);
		VOP_GRF_SET(vop2, grf_bt656_clk_inv, !dclk_inv);
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
		VOP_CTRL_SET(vop2, mipi0_en, 1);
		VOP_CTRL_SET(vop2, mipi0_mux, vp_data->id);
		VOP_CTRL_SET(vop2, mipi_pin_pol, val);
		VOP_CTRL_SET(vop2, mipi_dclk_pol, dclk_inv);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_MIPI1) {
		VOP_CTRL_SET(vop2, mipi1_en, 1);
		VOP_CTRL_SET(vop2, mipi1_mux, vp_data->id);
		VOP_CTRL_SET(vop2, mipi_pin_pol, val);
		VOP_CTRL_SET(vop2, mipi_dclk_pol, dclk_inv);
	}

	if (vcstate->output_flags & ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE) {
		VOP_MODULE_SET(vop2, vp, mipi_dual_en, 1);
		if (vcstate->output_flags & ROCKCHIP_OUTPUT_DATA_SWAP)
			VOP_MODULE_SET(vop2, vp, mipi_dual_channel_swap, 1);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_eDP0) {
		VOP_CTRL_SET(vop2, edp0_en, 1);
		VOP_CTRL_SET(vop2, edp0_mux, vp_data->id);
		VOP_CTRL_SET(vop2, edp_pin_pol, val);
		VOP_CTRL_SET(vop2, edp_dclk_pol, dclk_inv);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_eDP1) {
		VOP_CTRL_SET(vop2, edp1_en, 1);
		VOP_CTRL_SET(vop2, edp1_mux, vp_data->id);
		VOP_CTRL_SET(vop2, edp_pin_pol, val);
		VOP_CTRL_SET(vop2, edp_dclk_pol, dclk_inv);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_DP0) {
		VOP_CTRL_SET(vop2, dp0_en, 1);
		VOP_CTRL_SET(vop2, dp0_mux, vp_data->id);
		VOP_CTRL_SET(vop2, dp_dclk_pol, 0);
		VOP_CTRL_SET(vop2, dp_pin_pol, val);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_DP1) {
		VOP_CTRL_SET(vop2, dp1_en, 1);
		VOP_CTRL_SET(vop2, dp1_mux, vp_data->id);
		VOP_CTRL_SET(vop2, dp_dclk_pol, 0);
		VOP_CTRL_SET(vop2, dp_pin_pol, val);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_HDMI0) {
		VOP_CTRL_SET(vop2, hdmi0_en, 1);
		VOP_CTRL_SET(vop2, hdmi0_mux, vp_data->id);
		VOP_CTRL_SET(vop2, hdmi_pin_pol, val);
		VOP_CTRL_SET(vop2, hdmi_dclk_pol, 1);
	}

	if (vcstate->output_if & VOP_OUTPUT_IF_HDMI1) {
		VOP_CTRL_SET(vop2, hdmi1_en, 1);
		VOP_CTRL_SET(vop2, hdmi1_mux, vp_data->id);
		VOP_CTRL_SET(vop2, hdmi_pin_pol, val);
		VOP_CTRL_SET(vop2, hdmi_dclk_pol, 1);
	}

	if ((vcstate->output_mode == ROCKCHIP_OUT_MODE_AAAA &&
	     !(vp_data->feature & VOP_FEATURE_OUTPUT_10BIT)) ||
	    vcstate->output_if & VOP_OUTPUT_IF_BT656)
		out_mode = ROCKCHIP_OUT_MODE_P888;
	else
		out_mode = vcstate->output_mode;
	VOP_MODULE_SET(vop2, vp, out_mode, out_mode);

	if (vop2_output_uv_swap(vcstate->bus_format, vcstate->output_mode))
		VOP_MODULE_SET(vop2, vp, dsp_data_swap, DSP_RB_SWAP);
	else
		VOP_MODULE_SET(vop2, vp, dsp_data_swap, 0);

	vop2_dither_setup(crtc);

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

	VOP_INTR_SET(vop2, intr, line_flag_num[0], act_end);
	VOP_INTR_SET(vop2, intr, line_flag_num[1],
		     act_end - us_to_vertical_line(adjusted_mode, for_ddr_freq));

	VOP_MODULE_SET(vop2, vp, vtotal_pw, vtotal << 16 | vsync_len);

	VOP_MODULE_SET(vop2, vp, core_dclk_div, !!(adjusted_mode->flags & DRM_MODE_FLAG_DBLCLK));
	if (vcstate->output_mode == ROCKCHIP_OUT_MODE_YUV420) {
		VOP_MODULE_SET(vop2, vp, dclk_div2, 1);
		VOP_MODULE_SET(vop2, vp, dclk_div2_phase_lock, 1);
	} else {
		VOP_MODULE_SET(vop2, vp, dclk_div2, 0);
		VOP_MODULE_SET(vop2, vp, dclk_div2_phase_lock, 0);
	}

	clk_set_rate(vp->dclk, adjusted_mode->crtc_clock * 1000);

	vop2_post_config(crtc);

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

	drm_crtc_vblank_on(crtc);

	vop2_unlock(vop2);
}

static int vop2_zpos_cmp(const void *a, const void *b)
{
	struct vop2_zpos *pa = (struct vop2_zpos *)a;
	struct vop2_zpos *pb = (struct vop2_zpos *)b;

	return pa->zpos - pb->zpos;
}

static int vop2_crtc_atomic_check(struct drm_crtc *crtc,
				  struct drm_crtc_state *crtc_state)
{
	return 0;
}

static void vop2_setup_hdr10(struct vop2_video_port *vp, uint8_t win_phys_id)
{
	struct vop2 *vop2 = vp->vop2;
	struct vop2_win *win = vop2_find_win_by_phys_id(vop2, win_phys_id);
	struct drm_plane *plane = &win->base;
	struct drm_plane_state *pstate = plane->state;
	struct vop2_plane_state *vpstate = to_vop2_plane_state(pstate);
	struct drm_crtc_state *cstate = vp->crtc.state;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(cstate);
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	const struct vop_hdr_table *hdr_table = vp_data->hdr_table;
	uint32_t lut_mode = VOP2_HDR_LUT_MODE_AHB;
	uint32_t sdr2hdr_r2r_mode = 0;
	bool hdr_en = 0;
	bool hdr2sdr_en = 0;
	bool sdr2hdr_en = 0;
	bool sdr2hdr_tf = 0;
	bool hdr2sdr_tf_update = 0;
	bool sdr2hdr_tf_update = 0;

	/*
	 * Check whether this video port support hdr or not
	 */
	if (!hdr_table)
		return;

	/*
	 * HDR video plane input
	 */
	if (vpstate->eotf == SMPTE_ST2084)
		hdr_en = 1;

	vp->hdr_en = hdr_en;
	vp->hdr_in = hdr_en;
	vp->hdr_out = (vcstate->eotf == SMPTE_ST2084) ? true : false;

	/*
	 * only laryer0 support hdr2sdr
	 * if we have more than one active win attached to the video port,
	 * the other attached win must for ui, and should do sdr2hdr.
	 *
	 */
	if (vp->hdr_in && !vp->hdr_out)
		hdr2sdr_en = 1;

	if (vp->hdr_out && (vp->nr_layers > 1))
		sdr2hdr_en = 1;

	vp->sdr2hdr_en = sdr2hdr_en;
	vpstate->hdr_in = hdr_en;
	vpstate->hdr2sdr_en = hdr2sdr_en;

	if (sdr2hdr_en) {
		sdr2hdr_r2r_mode = BT709_TO_BT2020;
		if (vp->hdr_in)
			sdr2hdr_tf = SDR2HDR_FOR_HDR;
		else
			sdr2hdr_tf = SDR2HDR_FOR_BT2020;
	}

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
	if (!alpha_config->dst_pixel_alpha_en)
		alpha->dst_alpha_ctrl.bits.blend_mode = ALPHA_GLOBAL;
	else if (alpha_config->dst_pixel_alpha_en && !dst_glb_alpha_en)
		alpha->dst_alpha_ctrl.bits.blend_mode = ALPHA_PER_PIX;
	else
		alpha->dst_alpha_ctrl.bits.blend_mode = ALPHA_PER_PIX_GLOBAL;
	alpha->dst_alpha_ctrl.bits.alpha_cal_mode = ALPHA_NO_SATURATION;
	alpha->dst_alpha_ctrl.bits.factor_mode = ALPHA_SRC_INVERSE;
}

static int vop2_get_mixer_number(int nr_layers)
{
	if (nr_layers <= 1)
		return nr_layers;
	else
		return nr_layers - 1;
}

static int vop2_find_start_mixer_id_for_vp(struct vop2 *vop2, uint8_t port_id)
{
	struct vop2_video_port *vp;
	int mixer_id = 0;
	int i;

	for (i = 0; i < port_id; i++) {
		vp = &vop2->vps[i];
		mixer_id += vop2_get_mixer_number(hweight32(vp->win_mask));
	}

	return mixer_id ? (mixer_id + 1) : 0;
}

/*
 * src: top layer
 * dst: bottom layer.
 * Cluster mixer default use win1 as top layer
 */
static void vop2_setup_cluster_alpha(struct vop2 *vop2, struct vop2_cluster *cluster)
{
	uint32_t src_color_ctrl_offset = vop2->data->ctrl->cluster0_src_color_ctrl.offset;
	uint32_t dst_color_ctrl_offset = vop2->data->ctrl->cluster0_dst_color_ctrl.offset;
	uint32_t src_alpha_ctrl_offset = vop2->data->ctrl->cluster0_src_alpha_ctrl.offset;
	uint32_t dst_alpha_ctrl_offset = vop2->data->ctrl->cluster0_dst_alpha_ctrl.offset;
	uint32_t offset = (cluster->main->phys_id * 0x10);
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
	bool src_pixel_alpha_en = false, dst_pixel_alpha_en = false;
	u16 src_glb_alpha_val = 0xff, dst_glb_alpha_val = 0xff;
	bool premulti_en = false;
	bool swap = false;

	if (!sub_win) {
		/* At one win mode, win0 is dst/bottom win, and win1 is a all zero src/top win */
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
		if (top_win_vpstate->base.pixel_blend_mode == DRM_MODE_BLEND_PREMULTI)
			premulti_en = true;
		else
			premulti_en = false;
		src_pixel_alpha_en = is_alpha_support(fb->format->format);
	}
	fb = bottom_win_vpstate->base.fb;
	dst_pixel_alpha_en = is_alpha_support(fb->format->format);
	alpha_config.src_premulti_en = premulti_en;
	alpha_config.dst_premulti_en = false;
	alpha_config.src_pixel_alpha_en = src_pixel_alpha_en;
	alpha_config.dst_pixel_alpha_en = dst_pixel_alpha_en;
	alpha_config.src_glb_alpha_value = src_glb_alpha_val;
	alpha_config.dst_glb_alpha_value = dst_glb_alpha_val;
	vop2_parse_alpha(&alpha_config, &alpha);

	alpha.src_color_ctrl.bits.src_dst_swap = swap;
	vop2_writel(vop2, src_color_ctrl_offset + offset, alpha.src_color_ctrl.val);
	vop2_writel(vop2, dst_color_ctrl_offset + offset, alpha.dst_color_ctrl.val);
	vop2_writel(vop2, src_alpha_ctrl_offset + offset, alpha.src_alpha_ctrl.val);
	vop2_writel(vop2, dst_alpha_ctrl_offset + offset, alpha.dst_alpha_ctrl.val);
}

static void vop2_setup_alpha(struct vop2_video_port *vp,
			     const struct vop2_zpos *vop2_zpos)
{
	struct vop2 *vop2 = vp->vop2;
	uint32_t src_color_ctrl_offset = vop2->data->ctrl->src_color_ctrl.offset;
	uint32_t dst_color_ctrl_offset = vop2->data->ctrl->dst_color_ctrl.offset;
	uint32_t src_alpha_ctrl_offset = vop2->data->ctrl->src_alpha_ctrl.offset;
	uint32_t dst_alpha_ctrl_offset = vop2->data->ctrl->dst_alpha_ctrl.offset;
	const struct vop2_zpos *zpos;
	struct drm_framebuffer *fb;
	struct vop2_alpha_config alpha_config;
	struct vop2_alpha alpha;
	struct vop2_win *win;
	struct drm_plane *plane;
	struct vop2_plane_state *vpstate;
	int pixel_alpha_en;
	int premulti_en;
	int mixer_id;
	uint32_t offset;
	int i;
	bool bottom_layer_alpha_en = false;
	u32 dst_global_alpha = 0xff;

	drm_atomic_crtc_for_each_plane(plane, &vp->crtc) {
		struct vop2_win *win = to_vop2_win(plane);

		vpstate = to_vop2_plane_state(plane->state);
		if (vpstate->zpos == 0 && vpstate->global_alpha != 0xff &&
		    !vop2_cluster_window(win)) {
			/*
			 * If bottom layer have global alpha effect [except cluster layer,
			 * because cluster have deal with bottom layer global alpha value
			 * at cluster mix], bottom layer mix need deal with global alpha.
			 */
			bottom_layer_alpha_en = true;
			dst_global_alpha = vpstate->global_alpha;
			break;
		}
	}

	mixer_id = vop2_find_start_mixer_id_for_vp(vop2, vp->id);
	for (i = 1; i < vp->nr_layers; i++) {
		zpos = &vop2_zpos[i];
		win = vop2_find_win_by_phys_id(vop2, zpos->win_phys_id);
		plane = &win->base;
		vpstate = to_vop2_plane_state(plane->state);
		fb = plane->state->fb;
		if (plane->state->pixel_blend_mode == DRM_MODE_BLEND_PREMULTI)
			premulti_en = 1;
		else
			premulti_en = 0;
		pixel_alpha_en = is_alpha_support(fb->format->format);

		alpha_config.src_premulti_en = premulti_en;
		alpha_config.dst_pixel_alpha_en = false;
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

		if (i == 1) {
			if (bottom_layer_alpha_en || vp->hdr_en) {
				/* Transfer pixel alpha to hdr mix */
				alpha_config.src_premulti_en = premulti_en;
				alpha_config.dst_premulti_en = true;
				alpha_config.src_pixel_alpha_en = true;
				alpha_config.dst_pixel_alpha_en = true;
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
		}
	}

	/* Transfer pixel alpha value to next mix */
	alpha_config.src_premulti_en = true;
	alpha_config.dst_premulti_en = true;
	alpha_config.src_pixel_alpha_en = true;
	alpha_config.dst_pixel_alpha_en = true;
	alpha_config.src_glb_alpha_value = 0xff;
	alpha_config.dst_glb_alpha_value = 0xff;
	vop2_parse_alpha(&alpha_config, &alpha);
	for (; i < hweight32(vp->win_mask); i++) {
		offset = (mixer_id + i - 1) * 0x10;
		vop2_writel(vop2, src_alpha_ctrl_offset + offset, alpha.src_alpha_ctrl.val);
		vop2_writel(vop2, dst_alpha_ctrl_offset + offset, alpha.dst_alpha_ctrl.val);
	}
}

static void vop2_setup_layer_mixer_for_vp(struct vop2_video_port *vp,
					  const struct vop2_zpos *vop2_zpos)
{
	struct vop2_video_port *prev_vp;
	struct vop2 *vop2 = vp->vop2;
	u8 port_id = vp->id;
	const struct vop2_data *vop2_data = vop2->data;
	uint8_t nr_layers = vp->nr_layers;
	const struct vop2_zpos *zpos;
	struct vop2_win *win;
	struct vop2_layer *layer;
	u8 used_layers = 0;
	u8 port_mux;
	u8 layer_id, win_phys_id;
	int i;

	VOP_CTRL_SET(vop2, ovl_cfg_done_port, port_id);
	VOP_CTRL_SET(vop2, ovl_port_mux_cfg_done_imd, 0);
	for (i = 0; i < vop2_data->nr_vps - 1; i++) {
		prev_vp = &vop2->vps[i];
		used_layers += hweight32(prev_vp->win_mask);
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

		if (port_mux > vop2_data->nr_mixers)
			vp->bg_ovl_dly = 0;
		else
			vp->bg_ovl_dly = (vop2_data->nr_mixers - port_mux) << 1;
		VOP_MODULE_SET(vop2, prev_vp, port_mux, port_mux);
	}

	/*
	 * Win and layer must map one by one, if a win is selected
	 * by two layers, unexpected error may happen.
	 * So when we attach a new win to a layer, we also move the
	 * old win of the layer to the layer where the new win comes from.
	 *
	 */
	used_layers = 0;
	for (i = 0; i < port_id; i++) {
		prev_vp = &vop2->vps[i];
		used_layers += hweight32(prev_vp->win_mask);
	}

	for (i = 0; i < nr_layers; i++) {
		layer = &vop2->layers[used_layers + i];
		zpos = &vop2_zpos[i];
		win = vop2_find_win_by_phys_id(vop2, zpos->win_phys_id);
		layer_id = win->layer_id;
		win_phys_id = layer->win_phys_id;
		VOP_CTRL_SET(vop2, win_vp_id[win->phys_id], port_id);
		VOP_MODULE_SET(vop2, layer, layer_sel, win->layer_sel_id);
		win->layer_id = layer->id;
		layer->win_phys_id = win->phys_id;
		layer = &vop2->layers[layer_id];
		win = vop2_find_win_by_phys_id(vop2, win_phys_id);
		VOP_MODULE_SET(vop2, layer, layer_sel, win->layer_sel_id);
		win->layer_id = layer_id;
		layer->win_phys_id = win_phys_id;
	}
}

static void vop2_setup_dly_for_vp(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	struct drm_crtc *crtc = &vp->crtc;
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

	bg_dly -= vp->bg_ovl_dly;

	pre_scan_dly = bg_dly + (hdisplay >> 1) - 1;
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
		plane = &win->base;
		vpstate = to_vop2_plane_state(plane->state);
		if (vp->hdr_in && !vp->hdr_out && !vpstate->hdr_in)
			dly = win->dly[VOP2_DLY_MODE_HISO_S];
		else if (vp->hdr_in && vp->hdr_out && vpstate->hdr_in)
			dly = win->dly[VOP2_DLY_MODE_HIHO_H];
		else
			dly = win->dly[VOP2_DLY_MODE_DEFAULT];
		if (vop2_cluster_window(win))
			dly |= dly << 8;

		VOP_CTRL_SET(vop2, win_dly[win->phys_id], dly);
	}

}
static void vop2_crtc_atomic_begin(struct drm_crtc *crtc, struct drm_crtc_state *old_crtc_state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct drm_plane *plane;
	struct vop2_plane_state *vpstate;
	struct vop2_zpos *vop2_zpos;
	struct vop2_cluster cluster;
	uint8_t nr_layers = 0;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);

	vcstate->yuv_overlay = is_yuv_output(vcstate->bus_format);
	vop2_zpos = kmalloc_array(vop2->data->win_size, sizeof(*vop2_zpos), GFP_KERNEL);
	if (!vop2_zpos)
		return;

	/* Process cluster sub windows overlay. */
	drm_atomic_crtc_for_each_plane(plane, crtc) {
		struct vop2_win *win = to_vop2_win(plane);
		struct vop2_win *main_win;

		win->two_win_mode = false;
		if (!(win->feature & WIN_FEATURE_CLUSTER_SUB))
			continue;
		main_win = vop2_find_win_by_phys_id(vop2, win->phys_id);
		cluster.main = main_win;
		cluster.sub = win;
		win->two_win_mode = true;
		main_win->two_win_mode = true;
		vop2_setup_cluster_alpha(vop2, &cluster);
		if (abs(main_win->base.state->zpos - win->base.state->zpos) != 1)
			DRM_ERROR("Cluster%d win0[zpos:%d] must next to win1[zpos:%d]\n",
				  cluster.main->phys_id,
				  main_win->base.state->zpos, win->base.state->zpos);
	}

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		struct vop2_win *win = to_vop2_win(plane);
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
		nr_layers++;
		DRM_DEV_DEBUG(vop2->dev, "%s active zpos:%d for vp%d from vp%d\n",
			     win->name, vpstate->zpos, vp->id, old_vp->id);
	}

	DRM_DEV_DEBUG(vop2->dev, "vp%d: %d windows, active layers %d\n",
		      vp->id, hweight32(vp->win_mask), nr_layers);
	if (nr_layers) {
		vp->nr_layers = nr_layers;

		sort(vop2_zpos, nr_layers, sizeof(vop2_zpos[0]), vop2_zpos_cmp, NULL);

		vop2_setup_hdr10(vp, vop2_zpos[0].win_phys_id);
		vop2_setup_layer_mixer_for_vp(vp, vop2_zpos);
		vop2_setup_alpha(vp, vop2_zpos);
		vop2_setup_dly_for_vp(vp);
		vop2_setup_dly_for_window(vp, vop2_zpos);
	}

	/* The pre alpha overlay of Cluster still need process in one win mode. */
	drm_atomic_crtc_for_each_plane(plane, crtc) {
		struct vop2_win *win = to_vop2_win(plane);

		if (!(win->feature & WIN_FEATURE_CLUSTER_MAIN))
			continue;
		if (win->two_win_mode)
			continue;
		cluster.main = win;
		cluster.sub = NULL;
		vop2_setup_cluster_alpha(vop2, &cluster);
	}

	kfree(vop2_zpos);
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

	vcstate->post_csc_mode = vop2_convert_csc_mode(vcstate->color_space);
	VOP_MODULE_SET(vop2, vp, bcsh_r2y_en, vcstate->post_r2y_en);
	VOP_MODULE_SET(vop2, vp, bcsh_y2r_en, vcstate->post_y2r_en);
	VOP_MODULE_SET(vop2, vp, bcsh_r2y_csc_mode, vcstate->post_csc_mode);
	VOP_MODULE_SET(vop2, vp, bcsh_y2r_csc_mode, vcstate->post_csc_mode);
	if (!vcstate->bcsh_en) {
		VOP_MODULE_SET(vop2, vp, bcsh_en, vcstate->bcsh_en);
		return;
	}

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
	VOP_MODULE_SET(vop2, vp, bcsh_brightness, brightness);
	VOP_MODULE_SET(vop2, vp, bcsh_contrast, contrast);
	VOP_MODULE_SET(vop2, vp, bcsh_sat_con, saturation * contrast / 0x100);
	VOP_MODULE_SET(vop2, vp, bcsh_sin_hue, sin_hue);
	VOP_MODULE_SET(vop2, vp, bcsh_cos_hue, cos_hue);
	VOP_MODULE_SET(vop2, vp, bcsh_out_mode, BCSH_OUT_MODE_NORMAL_VIDEO);
	VOP_MODULE_SET(vop2, vp, bcsh_en, vcstate->bcsh_en);
}

static void vop2_cfg_update(struct drm_crtc *crtc,
			    struct drm_crtc_state *old_crtc_state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vop2 *vop2 = vp->vop2;
	uint32_t val;

	spin_lock(&vop2->reg_lock);

	VOP_MODULE_SET(vop2, vp, overlay_mode, vcstate->yuv_overlay);

	if (vcstate->yuv_overlay)
		val = 0x20010200;
	else
		val = 0;
	VOP_MODULE_SET(vop2, vp, dsp_background, val);

	vop2_tv_config_update(crtc, old_crtc_state);

	vop2_post_config(crtc);

	spin_unlock(&vop2->reg_lock);
}

static void vop2_crtc_atomic_flush(struct drm_crtc *crtc, struct drm_crtc_state *old_cstate)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct drm_atomic_state *old_state = old_cstate->state;
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_plane_state *old_pstate;
	struct vop2 *vop2 = vp->vop2;
	struct drm_plane *plane;
	unsigned long flags;
	int i, ret;

	vop2_cfg_update(crtc, old_cstate);

	if (!vop2->is_iommu_enabled && vop2->is_iommu_needed) {
		if (vcstate->mode_update)
			VOP_CTRL_SET(vop2, dma_stop, 1);

		ret = rockchip_drm_dma_attach_device(vop2->drm_dev, vop2->dev);
		if (ret) {
			vop2->is_iommu_enabled = false;
			vop2_disable_all_planes_for_crtc(crtc);
			DRM_DEV_ERROR(vop2->dev, "failed to attach dma mapping, %d\n", ret);
		} else {
			vop2->is_iommu_enabled = true;
			VOP_CTRL_SET(vop2, dma_stop, 0);
		}
	}

	spin_lock_irqsave(&vop2->irq_lock, flags);

	vop2_wb_commit(crtc);
	vop2_cfg_done(crtc);

	spin_unlock_irqrestore(&vop2->irq_lock, flags);

	/*
	 * There is a (rather unlikely) possibility that a vblank interrupt
	 * fired before we set the cfg_done bit. To avoid spuriously
	 * signalling flip completion we need to wait for it to finish.
	 */
	vop2_wait_for_irq_handler(crtc);

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

		drm_framebuffer_get(old_pstate->fb);
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);
		drm_flip_work_queue(&vp->fb_unref_work, old_pstate->fb);
		set_bit(VOP_PENDING_FB_UNREF, &vp->pending);
	}
}

static const struct drm_crtc_helper_funcs vop2_crtc_helper_funcs = {
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
}

static struct drm_crtc_state *vop2_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *vcstate, *old_vcstate;

	old_vcstate = to_rockchip_crtc_state(crtc->state);
	vcstate = kmemdup(old_vcstate, sizeof(*old_vcstate), GFP_KERNEL);
	if (!vcstate)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &vcstate->base);
	return &vcstate->base;
}

static void vop2_crtc_destroy_state(struct drm_crtc *crtc,
				    struct drm_crtc_state *state)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(state);

	__drm_atomic_helper_crtc_destroy_state(&vcstate->base);
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

	if (property == private->alpha_scale_prop) {
		*val = (vop2->data->feature & VOP_FEATURE_ALPHA_SCALE) ? 1 : 0;
		return 0;
	}

	DRM_ERROR("failed to get vop2 crtc property\n");
	return -EINVAL;
}

static int vop2_crtc_atomic_set_property(struct drm_crtc *crtc,
					 struct drm_crtc_state *state,
					 struct drm_property *property,
					 uint64_t val)
{
	struct drm_device *drm_dev = crtc->dev;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(state);

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

	DRM_ERROR("failed to set vop2 crtc property\n");
	return -EINVAL;
}

static int vop2_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
			       u16 *blue, uint32_t size,
			       struct drm_modeset_acquire_ctx *ctx)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	int len, i;

	if (!vop2->lut)
		return -EINVAL;

	len = min(size, vop2->lut_len);
	for (i = 0; i < len; i++)
		rockchip_vop2_crtc_fb_gamma_set(crtc, red[i], green[i],
						blue[i], i);
	vop2_crtc_load_lut(crtc);

	return 0;
}

static const struct drm_crtc_funcs vop2_crtc_funcs = {
	.gamma_set = vop2_crtc_gamma_set,
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

	drm_crtc_vblank_put(&vp->crtc);
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

static irqreturn_t vop2_isr(int irq, void *data)
{
	struct vop2 *vop2 = data;
	struct drm_crtc *crtc;
	struct vop2_video_port *vp;
	const struct vop2_data *vop2_data = vop2->data;
	size_t vp_max = min_t(size_t, vop2_data->nr_vps, ROCKCHIP_MAX_CRTC);
	size_t axi_max = min_t(size_t, vop2_data->nr_axi_intr, VOP2_SYS_AXI_BUS_NUM);
	struct vop2_wb *wb = &vop2->wb;
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
		crtc = &vp->crtc;
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

		if (active_irqs & FS_FIELD_INTR) {
			if (vp->wb_en) {
				vp->fs_vsync_cnt++;
				/*
				 * First frame start, the start of the write back
				 * we should stop when write back complete.
				 */
				if (vp->fs_vsync_cnt == 1) {
					VOP_MODULE_SET(vop2, wb, enable, 0);
					vop2_wb_cfg_done(vop2);
				}

				/*
				 * Second frame start, write back complete now.
				 */
				if (vp->fs_vsync_cnt == 2) {
					drm_writeback_signal_completion(&vop2->wb.conn, 0);
					vp->wb_en = 0;
				}
			}
			drm_crtc_handle_vblank(crtc);
			vop2_handle_vblank(vop2, crtc);
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

static int vop2_plane_init(struct vop2 *vop2, struct vop2_win *win)
{
	struct rockchip_drm_private *private = vop2->drm_dev->dev_private;
	unsigned int blend_caps = BIT(DRM_MODE_BLEND_PIXEL_NONE) | BIT(DRM_MODE_BLEND_PREMULTI) |
				  BIT(DRM_MODE_BLEND_COVERAGE);
	unsigned int max_width, max_height;
	int ret;

	ret = drm_universal_plane_init(vop2->drm_dev, &win->base, win->possible_crtcs,
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
	max_width = vop2->data->max_output.width;
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

static int vop2_gamma_init(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	struct vop2_video_port *vp;
	struct device *dev = vop2->dev;
	u16 *r_base, *g_base, *b_base;
	u32 lut_len = vop2->lut_len;
	struct drm_crtc *crtc;
	int i = 0, j = 0;

	if (!vop2->lut_regs)
		return 0;

	vop2->lut = devm_kmalloc_array(dev, lut_len, sizeof(*vop2->lut),
				       GFP_KERNEL);
	if (!vop2->lut)
		return -ENOMEM;

	for (i = 0; i < lut_len; i++) {
		u32 r = i * lut_len * lut_len;
		u32 g = i * lut_len;
		u32 b = i;

		vop2->lut[i] = r | g | b;
	}

	for (i = 0; i < vop2_data->nr_vps; i++) {
		vp = &vop2->vps[i];
		crtc = &vp->crtc;

		drm_mode_crtc_set_gamma_size(crtc, lut_len);
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

static int vop2_create_crtc(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	struct drm_device *drm_dev = vop2->drm_dev;
	struct device *dev = vop2->dev;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	struct device_node *port;
	struct vop2_win *win = NULL;
	struct vop2_video_port *vp;
	const struct vop2_video_port_data *vp_data;
	uint64_t soc_id;
	char dclk_name[9];
	int i = 0, j = 0;
	int ret = 0;

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
		if (vop2_soc_is_rk3566())
			soc_id = vp_data->soc_id[1];
		else
			soc_id = vp_data->soc_id[0];

		snprintf(dclk_name, sizeof(dclk_name), "dclk_vp%d", vp->id);
		vp->dclk = devm_clk_get(vop2->dev, dclk_name);
		if (IS_ERR(vp->dclk)) {
			DRM_DEV_ERROR(vop2->dev, "failed to get %s\n", dclk_name);
			return PTR_ERR(vp->dclk);
		}

		crtc = &vp->crtc;

		while (j < vop2->registered_num_wins) {
			win = &vop2->win[j];
			j++;

			if (win->type == DRM_PLANE_TYPE_PRIMARY)
				break;
			win = NULL;
		}

		if (!win) {
			DRM_DEV_ERROR(vop2->dev, "No primary plane find for video_port%d\n", i);
			break;
		}

		if (vop2_plane_init(vop2, win)) {
			DRM_DEV_ERROR(vop2->dev, "failed to init plane\n");
			break;
		}
		plane = &win->base;
		ret = drm_crtc_init_with_planes(drm_dev, crtc, plane, NULL, &vop2_crtc_funcs,
						"video_port%d", vp->id);
		if (ret) {
			DRM_DEV_ERROR(vop2->dev, "crtc init for video_port%d failed\n", i);
			return ret;
		}

		drm_crtc_helper_add(crtc, &vop2_crtc_helper_funcs);

		port = of_graph_get_port_by_id(dev->of_node, i);
		if (!port) {
			DRM_DEV_ERROR(vop2->dev, "no port node found for video_port%d\n", i);
			ret = -ENOENT;
		}

		crtc->port = port;

		drm_flip_work_init(&vp->fb_unref_work, "fb_unref", vop2_fb_unref_worker);

		init_completion(&vp->dsp_hold_completion);
		init_completion(&vp->line_flag_completion);
		rockchip_register_crtc_funcs(crtc, &private_crtc_funcs);
		soc_id = vop2_soc_id_fixup(soc_id);
		drm_object_attach_property(&crtc->base, vop2->soc_id_prop, soc_id);
		drm_object_attach_property(&crtc->base, vop2->vp_id_prop, vp->id);
		drm_object_attach_property(&crtc->base,
					   drm_dev->mode_config.tv_left_margin_property, 100);
		drm_object_attach_property(&crtc->base,
					   drm_dev->mode_config.tv_right_margin_property, 100);
		drm_object_attach_property(&crtc->base,
					   drm_dev->mode_config.tv_top_margin_property, 100);
		drm_object_attach_property(&crtc->base,
					   drm_dev->mode_config.tv_bottom_margin_property, 100);
	}

	/*
	 * change the unused primary window to overlay window
	 */
	for (; j < vop2->registered_num_wins; j++) {
		win = &vop2->win[j];
		if (win->type == DRM_PLANE_TYPE_PRIMARY)
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

		ret = vop2_plane_init(vop2, win);
		if (ret) {
			DRM_DEV_ERROR(vop2->dev, "failed to init overlay\n");
			break;
		}
	}

	return ret;
}

static void vop2_destroy_crtc(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);

	of_node_put(crtc->port);

	/*
	 * Destroy CRTC after vop2_plane_destroy() since vop2_disable_plane()
	 * references the CRTC.
	 */
	drm_crtc_cleanup(crtc);
	drm_flip_work_cleanup(&vp->fb_unref_work);
}

static int vop2_win_init(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_layer_data *layer_data;
	struct drm_prop_enum_list *plane_name_list;
	struct vop2_win *win;
	struct vop2_layer *layer;
	struct drm_property *prop;
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
		win->dly = win_data->dly;
		win->feature = win_data->feature;
		win->phys_id = win_data->phys_id;
		win->layer_sel_id = win_data->layer_sel_id;
		win->win_id = i;
		win->plane_id = plane_id++;
		win->area_id = 0;
		win->zpos = i;
		win->vop2 = vop2;
		if (vop2_soc_is_rk3566())
			win->possible_crtcs = win_data->possible_crtcs[1];
		else
			win->possible_crtcs = win_data->possible_crtcs[0];

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
			area->nformats = win->nformats;
			area->format_modifiers = win->format_modifiers;
			area->possible_crtcs = win->possible_crtcs;
			area->max_upscale_factor = win_data->max_upscale_factor;
			area->max_downscale_factor = win_data->max_downscale_factor;
			area->supported_rotations = win_data->supported_rotations;
			area->hsu_filter_mode = win_data->hsu_filter_mode;
			area->hsd_filter_mode = win_data->hsd_filter_mode;
			area->vsu_filter_mode = win_data->vsu_filter_mode;
			area->vsd_filter_mode = win_data->vsd_filter_mode;

			area->vop2 = vop2;
			area->win_id = i;
			area->phys_id = win->phys_id;
			area->area_id = j + 1;
			area->plane_id = plane_id++;
			area->layer_sel_id = -1;
			snprintf(name, min(sizeof(name), strlen(win->name)), "%s", win->name);
			snprintf(name, sizeof(name), "%s%d", name, area->area_id);
			area->name = devm_kstrdup(vop2->dev, name, GFP_KERNEL);
			num_wins++;
		}
	}

	vop2->registered_num_wins = num_wins;

	for (i = 0; i < vop2_data->nr_layers; i++) {
		layer = &vop2->layers[i];
		layer_data = &vop2_data->layer[i];
		layer->id = layer_data->id;
		layer->regs = layer_data->regs;
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

	prop = drm_property_create_object(vop2->drm_dev,
					  DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_IMMUTABLE,
					  "SOC_ID", DRM_MODE_OBJECT_CRTC);
	vop2->soc_id_prop = prop;

	prop = drm_property_create_object(vop2->drm_dev,
					  DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_IMMUTABLE,
					  "PORT_ID", DRM_MODE_OBJECT_CRTC);
	vop2->vp_id_prop = prop;

	if (!vop2->soc_id_prop || !vop2->vp_id_prop) {
		DRM_DEV_ERROR(vop2->dev, "failed to create soc_id/vp_id property\n");
		return -ENOMEM;
	}

	return 0;
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

	ret = vop2_win_init(vop2);
	if (ret)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (!res) {
		DRM_DEV_ERROR(vop2->dev, "failed to get vop2 register byname\n");
		return -EINVAL;
	}
	vop2->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(vop2->regs))
		return PTR_ERR(vop2->regs);
	vop2->len = resource_size(res);

	vop2->regsbak = devm_kzalloc(dev, vop2->len, GFP_KERNEL);
	if (!vop2->regsbak)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gamma_lut");
	if (res) {
		vop2->lut_len = resource_size(res) / sizeof(*vop2->lut);
		if (vop2->lut_len != 256 && vop2->lut_len != 1024) {
			DRM_DEV_ERROR(vop2->dev, "unsupported lut sizes %d\n", vop2->lut_len);
			return -EINVAL;
		}

		vop2->lut_regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(vop2->lut_regs))
			return PTR_ERR(vop2->lut_regs);
	}

	vop2->grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");

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

	vop2->irq = platform_get_irq(pdev, 0);
	if (vop2->irq < 0) {
		DRM_DEV_ERROR(dev, "cannot find irq for vop2\n");
		return vop2->irq;
	}

	spin_lock_init(&vop2->reg_lock);
	spin_lock_init(&vop2->irq_lock);
	mutex_init(&vop2->vop2_lock);

	ret = devm_request_irq(dev, vop2->irq, vop2_isr, IRQF_SHARED, dev_name(dev), vop2);
	if (ret)
		return ret;

	ret = vop2_create_crtc(vop2);
	if (ret)
		return ret;
	ret = vop2_gamma_init(vop2);
	if (ret)
		return ret;
	vop2_wb_connector_init(vop2);
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
}

const struct component_ops vop2_component_ops = {
	.bind = vop2_bind,
	.unbind = vop2_unbind,
};
EXPORT_SYMBOL_GPL(vop2_component_ops);
