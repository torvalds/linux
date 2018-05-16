/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
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

#include <drm/drm.h>
#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_flip_work.h>
#include <drm/drm_plane_helper.h>
#include <dt-bindings/clock/rk_system_status.h>

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
#include <linux/pm_runtime.h>
#include <linux/component.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/sort.h>
#include <soc/rockchip/rockchip_dmc.h>
#include <soc/rockchip/rockchip-system-status.h>
#include <uapi/drm/rockchip_drm.h>
#include <uapi/linux/videodev2.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_gem.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_vop.h"
#include "rockchip_drm_backlight.h"

#define MAX_VOPS	2

#define VOP_REG_SUPPORT(vop, reg) \
		(reg.mask && \
		 (!reg.major || \
		  (reg.major == VOP_MAJOR(vop->version) && \
		   reg.begin_minor <= VOP_MINOR(vop->version) && \
		   reg.end_minor >= VOP_MINOR(vop->version))))

#define VOP_WIN_SUPPORT(vop, win, name) \
		VOP_REG_SUPPORT(vop, win->phy->name)

#define VOP_WIN_SCL_EXT_SUPPORT(vop, win, name) \
		(win->phy->scl->ext && \
		VOP_REG_SUPPORT(vop, win->phy->scl->ext->name))

#define VOP_CTRL_SUPPORT(vop, name) \
		VOP_REG_SUPPORT(vop, vop->data->ctrl->name)

#define VOP_INTR_SUPPORT(vop, name) \
		VOP_REG_SUPPORT(vop, vop->data->intr->name)

#define __REG_SET(x, off, mask, shift, v, write_mask, relaxed) \
		vop_mask_write(x, off, mask, shift, v, write_mask, relaxed)

#define _REG_SET(vop, name, off, reg, mask, v, relaxed) \
	do { \
		if (VOP_REG_SUPPORT(vop, reg)) \
			__REG_SET(vop, off + reg.offset, mask, reg.shift, \
				  v, reg.write_mask, relaxed); \
		else \
			dev_dbg(vop->dev, "Warning: not support "#name"\n"); \
	} while(0)

#define REG_SET(x, name, off, reg, v, relaxed) \
		_REG_SET(x, name, off, reg, reg.mask, v, relaxed)
#define REG_SET_MASK(x, name, off, reg, mask, v, relaxed) \
		_REG_SET(x, name, off, reg, reg.mask & mask, v, relaxed)

#define VOP_WIN_SET(x, win, name, v) \
		REG_SET(x, name, win->offset, VOP_WIN_NAME(win, name), v, true)
#define VOP_WIN_SET_EXT(x, win, ext, name, v) \
		REG_SET(x, name, 0, win->ext->name, v, true)
#define VOP_SCL_SET(x, win, name, v) \
		REG_SET(x, name, win->offset, win->phy->scl->name, v, true)
#define VOP_SCL_SET_EXT(x, win, name, v) \
		REG_SET(x, name, win->offset, win->phy->scl->ext->name, v, true)

#define VOP_CTRL_SET(x, name, v) \
		REG_SET(x, name, 0, (x)->data->ctrl->name, v, false)

#define VOP_INTR_GET(vop, name) \
		vop_read_reg(vop, 0, &vop->data->ctrl->name)

#define VOP_INTR_SET(vop, name, v) \
		REG_SET(vop, name, 0, vop->data->intr->name, \
			v, false)
#define VOP_INTR_SET_MASK(vop, name, mask, v) \
		REG_SET_MASK(vop, name, 0, vop->data->intr->name, \
			     mask, v, false)

#define VOP_INTR_SET_TYPE(vop, name, type, v) \
	do { \
		int i, reg = 0, mask = 0; \
		for (i = 0; i < vop->data->intr->nintrs; i++) { \
			if (vop->data->intr->intrs[i] & type) { \
				reg |= (v) << i; \
				mask |= 1 << i; \
			} \
		} \
		VOP_INTR_SET_MASK(vop, name, mask, reg); \
	} while (0)
#define VOP_INTR_GET_TYPE(vop, name, type) \
		vop_get_intr_type(vop, &vop->data->intr->name, type)

#define VOP_CTRL_GET(x, name) \
		vop_read_reg(x, 0, &vop->data->ctrl->name)

#define VOP_WIN_GET(x, win, name) \
		vop_read_reg(x, win->offset, &VOP_WIN_NAME(win, name))

#define VOP_WIN_NAME(win, name) \
		(vop_get_win_phy(win, &win->phy->name)->name)

#define VOP_WIN_GET_YRGBADDR(vop, win) \
		vop_readl(vop, win->offset + VOP_WIN_NAME(win, yrgb_mst).offset)
#define VOP_GRF_SET(vop, reg, v) \
	do { \
		if (vop->data->grf_ctrl) { \
			vop_grf_writel(vop, vop->data->grf_ctrl->reg, v); \
		} \
	} while (0)

#define to_vop(x) container_of(x, struct vop, crtc)
#define to_vop_win(x) container_of(x, struct vop_win, base)
#define to_vop_plane_state(x) container_of(x, struct vop_plane_state, base)

struct vop_zpos {
	int win_id;
	int zpos;
};

enum vop_pending {
	VOP_PENDING_FB_UNREF,
};

struct vop_plane_state {
	struct drm_plane_state base;
	int format;
	int zpos;
	unsigned int logo_ymirror;
	struct drm_rect src;
	struct drm_rect dest;
	dma_addr_t yrgb_mst;
	dma_addr_t uv_mst;
	void *yrgb_kvaddr;
	const uint32_t *y2r_table;
	const uint32_t *r2r_table;
	const uint32_t *r2y_table;
	int eotf;
	bool y2r_en;
	bool r2r_en;
	bool r2y_en;
	int color_space;
	unsigned int csc_mode;
	bool enable;
	int global_alpha;
	int blend_mode;
	unsigned long offset;
};

struct vop_win {
	struct vop_win *parent;
	struct drm_plane base;

	int win_id;
	int area_id;
	uint32_t offset;
	enum drm_plane_type type;
	const struct vop_win_phy *phy;
	const struct vop_csc *csc;
	const uint32_t *data_formats;
	uint32_t nformats;
	u64 feature;
	struct vop *vop;

	struct drm_property *rotation_prop;
	struct vop_plane_state state;
};

struct vop {
	struct drm_crtc crtc;
	struct device *dev;
	struct drm_device *drm_dev;
	struct dentry *debugfs;
	struct drm_info_list *debugfs_files;
	struct drm_property *plane_zpos_prop;
	struct drm_property *plane_feature_prop;
	struct drm_property *feature_prop;
	bool is_iommu_enabled;
	bool is_iommu_needed;
	bool is_enabled;
	bool mode_update;

	u32 version;

	struct drm_tv_connector_state active_tv_state;
	bool pre_overlay;

	/* mutex vsync_ work */
	struct mutex vsync_mutex;
	bool vsync_work_pending;
	bool loader_protect;
	struct completion dsp_hold_completion;

	/* protected by dev->event_lock */
	struct drm_pending_vblank_event *event;

	struct drm_flip_work fb_unref_work;
	unsigned long pending;

	struct completion line_flag_completion;

	const struct vop_data *data;
	int num_wins;

	uint32_t *regsbak;
	void __iomem *regs;
	struct regmap *grf;

	/* physical map length of vop register */
	uint32_t len;

	void __iomem *lut_regs;
	u32 *lut;
	u32 lut_len;
	bool lut_active;
	void __iomem *cabc_lut_regs;
	u32 cabc_lut_len;

	/* one time only one process allowed to config the register */
	spinlock_t reg_lock;
	/* lock vop irq reg */
	spinlock_t irq_lock;
	/* mutex vop enable and disable */
	struct mutex vop_lock;

	unsigned int irq;

	/* vop AHP clk */
	struct clk *hclk;
	/* vop dclk */
	struct clk *dclk;
	/* vop share memory frequency */
	struct clk *aclk;
	/* vop source handling, optional */
	struct clk *dclk_source;

	/* vop dclk reset */
	struct reset_control *dclk_rst;

	struct rockchip_dclk_pll *pll;

	struct vop_win win[];
};

static void vop_lock(struct vop *vop)
{
	mutex_lock(&vop->vop_lock);
	rockchip_dmcfreq_lock();
}

static void vop_unlock(struct vop *vop)
{
	rockchip_dmcfreq_unlock();
	mutex_unlock(&vop->vop_lock);
}

static inline void vop_grf_writel(struct vop *vop, struct vop_reg reg, u32 v)
{
	u32 val = 0;

	if (IS_ERR_OR_NULL(vop->grf))
		return;

	if (VOP_REG_SUPPORT(vop, reg)) {
		val = (v << reg.shift) | (reg.mask << (reg.shift + 16));
		regmap_write(vop->grf, reg.offset, val);
	}
}

static inline void vop_writel(struct vop *vop, uint32_t offset, uint32_t v)
{
	writel(v, vop->regs + offset);
	vop->regsbak[offset >> 2] = v;
}

static inline uint32_t vop_readl(struct vop *vop, uint32_t offset)
{
	return readl(vop->regs + offset);
}

static inline uint32_t vop_read_reg(struct vop *vop, uint32_t base,
				    const struct vop_reg *reg)
{
	return (vop_readl(vop, base + reg->offset) >> reg->shift) & reg->mask;
}

static inline void vop_mask_write(struct vop *vop, uint32_t offset,
				  uint32_t mask, uint32_t shift, uint32_t v,
				  bool write_mask, bool relaxed)
{
	if (!mask)
		return;

	if (write_mask) {
		v = ((v & mask) << shift) | (mask << (shift + 16));
	} else {
		uint32_t cached_val = vop->regsbak[offset >> 2];

		v = (cached_val & ~(mask << shift)) | ((v & mask) << shift);
		vop->regsbak[offset >> 2] = v;
	}

	if (relaxed)
		writel_relaxed(v, vop->regs + offset);
	else
		writel(v, vop->regs + offset);
}

static inline const struct vop_win_phy *
vop_get_win_phy(struct vop_win *win, const struct vop_reg *reg)
{
	if (!reg->mask && win->parent)
		return win->parent->phy;

	return win->phy;
}

static inline uint32_t vop_get_intr_type(struct vop *vop,
					 const struct vop_reg *reg, int type)
{
	uint32_t i, ret = 0;
	uint32_t regs = vop_read_reg(vop, 0, reg);

	for (i = 0; i < vop->data->intr->nintrs; i++) {
		if ((type & vop->data->intr->intrs[i]) && (regs & 1 << i))
			ret |= vop->data->intr->intrs[i];
	}

	return ret;
}

static void vop_load_hdr2sdr_table(struct vop *vop)
{
	int i;
	const struct vop_hdr_table *table = vop->data->hdr_table;
	uint32_t hdr2sdr_eetf_oetf_yn[33];

	for (i = 0; i < 33; i++)
		hdr2sdr_eetf_oetf_yn[i] = table->hdr2sdr_eetf_yn[i] +
				(table->hdr2sdr_bt1886oetf_yn[i] << 16);

	vop_writel(vop, table->hdr2sdr_eetf_oetf_y0_offset,
		   hdr2sdr_eetf_oetf_yn[0]);
	for (i = 1; i < 33; i++)
		vop_writel(vop,
			   table->hdr2sdr_eetf_oetf_y1_offset + (i - 1) * 4,
			   hdr2sdr_eetf_oetf_yn[i]);

	vop_writel(vop, table->hdr2sdr_sat_y0_offset,
		   table->hdr2sdr_sat_yn[0]);
	for (i = 1; i < 9; i++)
		vop_writel(vop, table->hdr2sdr_sat_y1_offset + (i - 1) * 4,
			   table->hdr2sdr_sat_yn[i]);

	VOP_CTRL_SET(vop, hdr2sdr_src_min, table->hdr2sdr_src_range_min);
	VOP_CTRL_SET(vop, hdr2sdr_src_max, table->hdr2sdr_src_range_max);
	VOP_CTRL_SET(vop, hdr2sdr_normfaceetf, table->hdr2sdr_normfaceetf);
	VOP_CTRL_SET(vop, hdr2sdr_dst_min, table->hdr2sdr_dst_range_min);
	VOP_CTRL_SET(vop, hdr2sdr_dst_max, table->hdr2sdr_dst_range_max);
	VOP_CTRL_SET(vop, hdr2sdr_normfacgamma, table->hdr2sdr_normfacgamma);
}

static void vop_load_sdr2hdr_table(struct vop *vop, uint32_t cmd)
{
	int i;
	const struct vop_hdr_table *table = vop->data->hdr_table;
	uint32_t sdr2hdr_eotf_oetf_yn[65];
	uint32_t sdr2hdr_oetf_dx_dxpow[64];

	for (i = 0; i < 65; i++) {
		if (cmd == SDR2HDR_FOR_BT2020)
			sdr2hdr_eotf_oetf_yn[i] =
				table->sdr2hdr_bt1886eotf_yn_for_bt2020[i] +
				(table->sdr2hdr_st2084oetf_yn_for_bt2020[i] << 18);
		else if (cmd == SDR2HDR_FOR_HDR)
			sdr2hdr_eotf_oetf_yn[i] =
				table->sdr2hdr_bt1886eotf_yn_for_hdr[i] +
				(table->sdr2hdr_st2084oetf_yn_for_hdr[i] << 18);
		else if (cmd == SDR2HDR_FOR_HLG_HDR)
			sdr2hdr_eotf_oetf_yn[i] =
				table->sdr2hdr_bt1886eotf_yn_for_hlg_hdr[i] +
				(table->sdr2hdr_st2084oetf_yn_for_hlg_hdr[i] << 18);
	}
	vop_writel(vop, table->sdr2hdr_eotf_oetf_y0_offset,
		   sdr2hdr_eotf_oetf_yn[0]);
	for (i = 1; i < 65; i++)
		vop_writel(vop, table->sdr2hdr_eotf_oetf_y1_offset +
			   (i - 1) * 4, sdr2hdr_eotf_oetf_yn[i]);

	for (i = 0; i < 64; i++) {
		sdr2hdr_oetf_dx_dxpow[i] = table->sdr2hdr_st2084oetf_dxn[i] +
				(table->sdr2hdr_st2084oetf_dxn_pow2[i] << 16);
		vop_writel(vop, table->sdr2hdr_oetf_dx_dxpow1_offset + i * 4,
			   sdr2hdr_oetf_dx_dxpow[i]);
	}

	for (i = 0; i < 63; i++)
		vop_writel(vop, table->sdr2hdr_oetf_xn1_offset + i * 4,
			   table->sdr2hdr_st2084oetf_xn[i]);
}

static void vop_load_csc_table(struct vop *vop, u32 offset, const u32 *table)
{
	int i;

	/*
	 * so far the csc offset is not 0 and in the feature the csc offset
	 * impossible be 0, so when the offset is 0, should return here.
	 */
	if (!table || offset == 0)
		return;

	for (i = 0; i < 8; i++)
		vop_writel(vop, offset + i * 4, table[i]);
}

static inline void vop_cfg_done(struct vop *vop)
{
	VOP_CTRL_SET(vop, cfg_done, 1);
}

static bool vop_is_allwin_disabled(struct vop *vop)
{
	int i;

	for (i = 0; i < vop->num_wins; i++) {
		struct vop_win *win = &vop->win[i];

		if (VOP_WIN_GET(vop, win, enable) != 0)
			return false;
	}

	return true;
}

static void vop_disable_allwin(struct vop *vop)
{
	int i;

	for (i = 0; i < vop->num_wins; i++) {
		struct vop_win *win = &vop->win[i];

		if (win->phy->scl && win->phy->scl->ext) {
			VOP_SCL_SET_EXT(vop, win, yrgb_hor_scl_mode, SCALE_NONE);
			VOP_SCL_SET_EXT(vop, win, yrgb_ver_scl_mode, SCALE_NONE);
			VOP_SCL_SET_EXT(vop, win, cbcr_hor_scl_mode, SCALE_NONE);
			VOP_SCL_SET_EXT(vop, win, cbcr_ver_scl_mode, SCALE_NONE);
		}
		VOP_WIN_SET(vop, win, enable, 0);
		VOP_WIN_SET(vop, win, gate, 0);
	}
}

static bool vop_fs_irq_is_active(struct vop *vop)
{
	if (VOP_MAJOR(vop->version) == 3 && VOP_MINOR(vop->version) >= 7)
		return VOP_INTR_GET_TYPE(vop, status, FS_FIELD_INTR);
	else
		return VOP_INTR_GET_TYPE(vop, status, FS_INTR);
}

static bool vop_line_flag_is_active(struct vop *vop)
{
	return VOP_INTR_GET_TYPE(vop, status, LINE_FLAG_INTR);
}

static inline void vop_write_lut(struct vop *vop, uint32_t offset, uint32_t v)
{
	writel(v, vop->lut_regs + offset);
}

static inline uint32_t vop_read_lut(struct vop *vop, uint32_t offset)
{
	return readl(vop->lut_regs + offset);
}

static inline void vop_write_cabc_lut(struct vop *vop, uint32_t offset, uint32_t v)
{
	writel(v, vop->cabc_lut_regs + offset);
}

static bool has_rb_swapped(uint32_t format)
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

static enum vop_data_format vop_convert_format(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return VOP_FMT_ARGB8888;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		return VOP_FMT_RGB888;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		return VOP_FMT_RGB565;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV12_10:
		return VOP_FMT_YUV420SP;
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV16_10:
		return VOP_FMT_YUV422SP;
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV24_10:
		return VOP_FMT_YUV444SP;
	default:
		DRM_ERROR("unsupport format[%08x]\n", format);
		return -EINVAL;
	}
}

static bool is_uv_swap(uint32_t bus_format, uint32_t output_mode)
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
	if ((bus_format == MEDIA_BUS_FMT_YUV8_1X24 ||
	     bus_format == MEDIA_BUS_FMT_YUV10_1X30) &&
	    (output_mode == ROCKCHIP_OUT_MODE_AAAA ||
	     output_mode == ROCKCHIP_OUT_MODE_P888))
		return true;
	else
		return false;
}

static bool is_yuv_output(uint32_t bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
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
		return true;
	default:
		return false;
	}
}

static bool is_yuv_10bit(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_NV12_10:
	case DRM_FORMAT_NV16_10:
	case DRM_FORMAT_NV24_10:
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

static uint16_t scl_vop_cal_scale(enum scale_mode mode, uint32_t src,
				  uint32_t dst, bool is_horizontal,
				  int vsu_mode, int *vskiplines)
{
	uint16_t val = 1 << SCL_FT_DEFAULT_FIXPOINT_SHIFT;

	if (is_horizontal) {
		if (mode == SCALE_UP)
			val = GET_SCL_FT_BIC(src, dst);
		else if (mode == SCALE_DOWN)
			val = GET_SCL_FT_BILI_DN(src, dst);
	} else {
		if (mode == SCALE_UP) {
			if (vsu_mode == SCALE_UP_BIL)
				val = GET_SCL_FT_BILI_UP(src, dst);
			else
				val = GET_SCL_FT_BIC(src, dst);
		} else if (mode == SCALE_DOWN) {
			if (vskiplines) {
				*vskiplines = scl_get_vskiplines(src, dst);
				val = scl_get_bili_dn_vskip(src, dst,
							    *vskiplines);
			} else {
				val = GET_SCL_FT_BILI_DN(src, dst);
			}
		}
	}

	return val;
}

static void scl_vop_cal_scl_fac(struct vop *vop, struct vop_win *win,
				uint32_t src_w, uint32_t src_h, uint32_t dst_w,
				uint32_t dst_h, uint32_t pixel_format)
{
	uint16_t yrgb_hor_scl_mode, yrgb_ver_scl_mode;
	uint16_t cbcr_hor_scl_mode = SCALE_NONE;
	uint16_t cbcr_ver_scl_mode = SCALE_NONE;
	int hsub = drm_format_horz_chroma_subsampling(pixel_format);
	int vsub = drm_format_vert_chroma_subsampling(pixel_format);
	bool is_yuv = is_yuv_support(pixel_format);
	uint16_t cbcr_src_w = src_w / hsub;
	uint16_t cbcr_src_h = src_h / vsub;
	uint16_t vsu_mode;
	uint16_t lb_mode;
	uint32_t val;
	int vskiplines = 0;
	const struct vop_data *vop_data = vop->data;

	if (!win->phy->scl)
		return;

	if (!(vop_data->feature & VOP_FEATURE_ALPHA_SCALE)) {
		if (is_alpha_support(pixel_format) &&
		    ((src_w != dst_w) || (src_h != dst_h)))
			DRM_ERROR("ERROR : unsupport ppixel alpha add scale\n");
	}
	if (!win->phy->scl->ext) {
		VOP_SCL_SET(vop, win, scale_yrgb_x,
			    scl_cal_scale2(src_w, dst_w));
		VOP_SCL_SET(vop, win, scale_yrgb_y,
			    scl_cal_scale2(src_h, dst_h));
		if (is_yuv) {
			VOP_SCL_SET(vop, win, scale_cbcr_x,
				    scl_cal_scale2(cbcr_src_w, dst_w));
			VOP_SCL_SET(vop, win, scale_cbcr_y,
				    scl_cal_scale2(cbcr_src_h, dst_h));
		}
		return;
	}

	yrgb_hor_scl_mode = scl_get_scl_mode(src_w, dst_w);
	yrgb_ver_scl_mode = scl_get_scl_mode(src_h, dst_h);

	if (is_yuv) {
		cbcr_hor_scl_mode = scl_get_scl_mode(cbcr_src_w, dst_w);
		cbcr_ver_scl_mode = scl_get_scl_mode(cbcr_src_h, dst_h);
		if (cbcr_hor_scl_mode == SCALE_DOWN)
			lb_mode = scl_vop_cal_lb_mode(dst_w, true);
		else
			lb_mode = scl_vop_cal_lb_mode(cbcr_src_w, true);
	} else {
		if (yrgb_hor_scl_mode == SCALE_DOWN)
			lb_mode = scl_vop_cal_lb_mode(dst_w, false);
		else
			lb_mode = scl_vop_cal_lb_mode(src_w, false);
	}

	VOP_SCL_SET_EXT(vop, win, lb_mode, lb_mode);
	if (lb_mode == LB_RGB_3840X2) {
		if (yrgb_ver_scl_mode != SCALE_NONE) {
			DRM_ERROR("ERROR : not allow yrgb ver scale\n");
			return;
		}
		if (cbcr_ver_scl_mode != SCALE_NONE) {
			DRM_ERROR("ERROR : not allow cbcr ver scale\n");
			return;
		}
		vsu_mode = SCALE_UP_BIL;
	} else if (lb_mode == LB_RGB_2560X4) {
		vsu_mode = SCALE_UP_BIL;
	} else {
		vsu_mode = SCALE_UP_BIC;
	}

	val = scl_vop_cal_scale(yrgb_hor_scl_mode, src_w, dst_w,
				true, 0, NULL);
	VOP_SCL_SET(vop, win, scale_yrgb_x, val);
	val = scl_vop_cal_scale(yrgb_ver_scl_mode, src_h, dst_h,
				false, vsu_mode, &vskiplines);
	VOP_SCL_SET(vop, win, scale_yrgb_y, val);

	VOP_SCL_SET_EXT(vop, win, vsd_yrgb_gt4, vskiplines == 4);
	VOP_SCL_SET_EXT(vop, win, vsd_yrgb_gt2, vskiplines == 2);

	VOP_SCL_SET_EXT(vop, win, yrgb_hor_scl_mode, yrgb_hor_scl_mode);
	VOP_SCL_SET_EXT(vop, win, yrgb_ver_scl_mode, yrgb_ver_scl_mode);
	VOP_SCL_SET_EXT(vop, win, yrgb_hsd_mode, SCALE_DOWN_BIL);
	VOP_SCL_SET_EXT(vop, win, yrgb_vsd_mode, SCALE_DOWN_BIL);
	VOP_SCL_SET_EXT(vop, win, yrgb_vsu_mode, vsu_mode);
	if (is_yuv) {
		vskiplines = 0;

		val = scl_vop_cal_scale(cbcr_hor_scl_mode, cbcr_src_w,
					dst_w, true, 0, NULL);
		VOP_SCL_SET(vop, win, scale_cbcr_x, val);
		val = scl_vop_cal_scale(cbcr_ver_scl_mode, cbcr_src_h,
					dst_h, false, vsu_mode, &vskiplines);
		VOP_SCL_SET(vop, win, scale_cbcr_y, val);

		VOP_SCL_SET_EXT(vop, win, vsd_cbcr_gt4, vskiplines == 4);
		VOP_SCL_SET_EXT(vop, win, vsd_cbcr_gt2, vskiplines == 2);
		VOP_SCL_SET_EXT(vop, win, cbcr_hor_scl_mode, cbcr_hor_scl_mode);
		VOP_SCL_SET_EXT(vop, win, cbcr_ver_scl_mode, cbcr_ver_scl_mode);
		VOP_SCL_SET_EXT(vop, win, cbcr_hsd_mode, SCALE_DOWN_BIL);
		VOP_SCL_SET_EXT(vop, win, cbcr_vsd_mode, SCALE_DOWN_BIL);
		VOP_SCL_SET_EXT(vop, win, cbcr_vsu_mode, vsu_mode);
	}
}

/*
 * rk3328 HDR/CSC path
 *
 * HDR/SDR --> win0  --> HDR2SDR ----\
 *		  \		      MUX --\
 *                 \ --> SDR2HDR/CSC--/      \
 *                                            \
 * SDR --> win1 -->pre_overlay ->SDR2HDR/CSC --> post_ovrlay-->post CSC-->output
 * SDR --> win2 -/
 *
 */

static int vop_hdr_atomic_check(struct drm_crtc *crtc,
				struct drm_crtc_state *crtc_state)
{
	struct drm_atomic_state *state = crtc_state->state;
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct drm_plane_state *pstate;
	struct drm_plane *plane;
	struct vop *vop = to_vop(crtc);
	int pre_sdr2hdr_state = 0, post_sdr2hdr_state = 0;
	int pre_sdr2hdr_mode = 0, post_sdr2hdr_mode = 0, sdr2hdr_func = 0;
	bool pre_overlay = false;
	int hdr2sdr_en = 0, plane_id = 0;

	if (!vop->data->hdr_table)
		return 0;
	/* hdr cover */
	drm_atomic_crtc_state_for_each_plane(plane, crtc_state) {
		struct vop_plane_state *vop_plane_state;
		struct vop_win *win = to_vop_win(plane);

		pstate = drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(pstate))
			return PTR_ERR(pstate);
		vop_plane_state = to_vop_plane_state(pstate);
		if (!pstate->fb)
			continue;

		if (vop_plane_state->eotf > s->eotf)
			if (win->feature & WIN_FEATURE_HDR2SDR)
				hdr2sdr_en = 1;
		if (vop_plane_state->eotf < s->eotf) {
			if (win->feature & WIN_FEATURE_PRE_OVERLAY)
				pre_sdr2hdr_state |= BIT(plane_id);
			else
				post_sdr2hdr_state |= BIT(plane_id);
		}
		plane_id++;
	}

	if (pre_sdr2hdr_state || post_sdr2hdr_state || hdr2sdr_en) {
		pre_overlay = true;
		pre_sdr2hdr_mode = BT709_TO_BT2020;
		post_sdr2hdr_mode = BT709_TO_BT2020;
		sdr2hdr_func = SDR2HDR_FOR_HDR;
		goto exit_hdr_conver;
	}

	/* overlay mode */
	plane_id = 0;
	pre_overlay = false;
	pre_sdr2hdr_mode = 0;
	post_sdr2hdr_mode = 0;
	pre_sdr2hdr_state = 0;
	post_sdr2hdr_state = 0;
	drm_atomic_crtc_state_for_each_plane(plane, crtc_state) {
		struct vop_plane_state *vop_plane_state;
		struct vop_win *win = to_vop_win(plane);

		pstate = drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(pstate))
			return PTR_ERR(pstate);
		vop_plane_state = to_vop_plane_state(pstate);
		if (!pstate->fb)
			continue;

		if (vop_plane_state->color_space == V4L2_COLORSPACE_BT2020 &&
		    vop_plane_state->color_space > s->color_space) {
			if (win->feature & WIN_FEATURE_PRE_OVERLAY) {
				pre_sdr2hdr_mode = BT2020_TO_BT709;
				pre_sdr2hdr_state |= BIT(plane_id);
			} else {
				post_sdr2hdr_mode = BT2020_TO_BT709;
				post_sdr2hdr_state |= BIT(plane_id);
			}
		}
		if (s->color_space == V4L2_COLORSPACE_BT2020 &&
		    vop_plane_state->color_space < s->color_space) {
			if (win->feature & WIN_FEATURE_PRE_OVERLAY) {
				pre_sdr2hdr_mode = BT709_TO_BT2020;
				pre_sdr2hdr_state |= BIT(plane_id);
			} else {
				post_sdr2hdr_mode = BT709_TO_BT2020;
				post_sdr2hdr_state |= BIT(plane_id);
			}
		}
		plane_id++;
	}

	if (pre_sdr2hdr_state || post_sdr2hdr_state) {
		pre_overlay = true;
		sdr2hdr_func = SDR2HDR_FOR_BT2020;
	}

exit_hdr_conver:
	s->hdr.pre_overlay = pre_overlay;
	s->hdr.hdr2sdr_en = hdr2sdr_en;
	if (s->hdr.pre_overlay)
		s->yuv_overlay = 0;

	s->hdr.sdr2hdr_state.bt1886eotf_pre_conv_en = !!pre_sdr2hdr_state;
	s->hdr.sdr2hdr_state.rgb2rgb_pre_conv_en = !!pre_sdr2hdr_state;
	s->hdr.sdr2hdr_state.rgb2rgb_pre_conv_mode = pre_sdr2hdr_mode;
	s->hdr.sdr2hdr_state.st2084oetf_pre_conv_en = !!pre_sdr2hdr_state;

	s->hdr.sdr2hdr_state.bt1886eotf_post_conv_en = !!post_sdr2hdr_state;
	s->hdr.sdr2hdr_state.rgb2rgb_post_conv_en = !!post_sdr2hdr_state;
	s->hdr.sdr2hdr_state.rgb2rgb_post_conv_mode = post_sdr2hdr_mode;
	s->hdr.sdr2hdr_state.st2084oetf_post_conv_en = !!post_sdr2hdr_state;
	s->hdr.sdr2hdr_state.sdr2hdr_func = sdr2hdr_func;

	return 0;
}

static int to_vop_csc_mode(int csc_mode)
{
	switch (csc_mode) {
	case V4L2_COLORSPACE_SMPTE170M:
		return CSC_BT601L;
	case V4L2_COLORSPACE_REC709:
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

static void vop_disable_all_planes(struct vop *vop)
{
	bool active;
	int ret;

	vop_disable_allwin(vop);
	vop_cfg_done(vop);
	ret = readx_poll_timeout_atomic(vop_is_allwin_disabled,
					vop, active, active,
					0, 500 * 1000);
	if (ret)
		dev_err(vop->dev, "wait win close timeout\n");
}

/*
 * rk3399 colorspace path:
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
static int vop_setup_csc_table(const struct vop_csc_table *csc_table,
			       bool is_input_yuv, bool is_output_yuv,
			       int input_csc, int output_csc,
			       const uint32_t **y2r_table,
			       const uint32_t **r2r_table,
			       const uint32_t **r2y_table)
{
	*y2r_table = NULL;
	*r2r_table = NULL;
	*r2y_table = NULL;

	if (!csc_table)
		return 0;

	if (is_output_yuv) {
		if (output_csc == V4L2_COLORSPACE_BT2020) {
			if (is_input_yuv) {
				if (input_csc == V4L2_COLORSPACE_BT2020)
					return 0;
				*y2r_table = csc_table->y2r_bt709;
			}
			if (input_csc != V4L2_COLORSPACE_BT2020)
				*r2r_table = csc_table->r2r_bt709_to_bt2020;
			*r2y_table = csc_table->r2y_bt2020;
		} else {
			if (is_input_yuv && input_csc == V4L2_COLORSPACE_BT2020)
				*y2r_table = csc_table->y2r_bt2020;
			if (input_csc == V4L2_COLORSPACE_BT2020)
				*r2r_table = csc_table->r2r_bt2020_to_bt709;
			if (!is_input_yuv || *y2r_table) {
				if (output_csc == V4L2_COLORSPACE_REC709 ||
				    output_csc == V4L2_COLORSPACE_DEFAULT)
					*r2y_table = csc_table->r2y_bt709;
				else
					*r2y_table = csc_table->r2y_bt601;
			}
		}
	} else {
		if (!is_input_yuv)
			return 0;

		/*
		 * is possible use bt2020 on rgb mode?
		 */
		if (WARN_ON(output_csc == V4L2_COLORSPACE_BT2020))
			return -EINVAL;

		if (input_csc == V4L2_COLORSPACE_BT2020)
			*y2r_table = csc_table->y2r_bt2020;
		else if ((input_csc == V4L2_COLORSPACE_REC709) ||
			 (input_csc == V4L2_COLORSPACE_DEFAULT))
			*y2r_table = csc_table->y2r_bt709;
		else
			*y2r_table = csc_table->y2r_bt601;

		if (input_csc == V4L2_COLORSPACE_BT2020)
			/*
			 * We don't have bt601 to bt709 table, force use bt709.
			 */
			*r2r_table = csc_table->r2r_bt2020_to_bt709;
	}

	return 0;
}

static void vop_setup_csc_mode(bool is_input_yuv, bool is_output_yuv,
			       int input_csc, int output_csc,
			       bool *y2r_en, bool *r2y_en, int *csc_mode)
{
	if (is_input_yuv && !is_output_yuv) {
		*y2r_en = true;
		*csc_mode =  to_vop_csc_mode(input_csc);
	} else if (!is_input_yuv && is_output_yuv) {
		*r2y_en = true;
		*csc_mode = to_vop_csc_mode(output_csc);
	}
}

static int vop_csc_atomic_check(struct drm_crtc *crtc,
				struct drm_crtc_state *crtc_state)
{
	struct vop *vop = to_vop(crtc);
	struct drm_atomic_state *state = crtc_state->state;
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	const struct vop_csc_table *csc_table = vop->data->csc_table;
	struct drm_plane_state *pstate;
	struct drm_plane *plane;
	bool is_input_yuv, is_output_yuv;
	int ret;

	is_output_yuv = is_yuv_output(s->bus_format);

	drm_atomic_crtc_state_for_each_plane(plane, crtc_state) {
		struct vop_plane_state *vop_plane_state;
		struct vop_win *win = to_vop_win(plane);

		pstate = drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(pstate))
			return PTR_ERR(pstate);
		vop_plane_state = to_vop_plane_state(pstate);

		if (!pstate->fb)
			continue;
		is_input_yuv = is_yuv_support(pstate->fb->pixel_format);
		vop_plane_state->y2r_en = false;
		vop_plane_state->r2r_en = false;
		vop_plane_state->r2y_en = false;

		ret = vop_setup_csc_table(csc_table, is_input_yuv,
					  is_output_yuv,
					  vop_plane_state->color_space,
					  s->color_space,
					  &vop_plane_state->y2r_table,
					  &vop_plane_state->r2r_table,
					  &vop_plane_state->r2y_table);
		if (ret)
			return ret;

		if (csc_table) {
			vop_plane_state->y2r_en = !!vop_plane_state->y2r_table;
			vop_plane_state->r2r_en = !!vop_plane_state->r2r_table;
			vop_plane_state->r2y_en = !!vop_plane_state->r2y_table;
			continue;
		}

		vop_setup_csc_mode(is_input_yuv, s->yuv_overlay,
				   vop_plane_state->color_space, s->color_space,
				   &vop_plane_state->y2r_en,
				   &vop_plane_state->r2y_en,
				   &vop_plane_state->csc_mode);

		/*
		 * This is update for IC design not reasonable, when enable
		 * hdr2sdr on rk3328, vop can't support per-pixel alpha * global
		 * alpha,so we must back to gpu, but gpu can't support hdr2sdr,
		 * gpu output hdr UI, vop will do:
		 * UI(rgbx) -> yuv -> rgb ->hdr2sdr -> overlay -> output.
		 */
		if (s->hdr.hdr2sdr_en &&
		    vop_plane_state->eotf == SMPTE_ST2084 &&
		    !is_yuv_support(pstate->fb->pixel_format))
			vop_plane_state->r2y_en = true;
		if (win->feature & WIN_FEATURE_PRE_OVERLAY)
			vop_plane_state->r2r_en =
				s->hdr.sdr2hdr_state.rgb2rgb_pre_conv_en;
		else if (win->feature & WIN_FEATURE_HDR2SDR)
			vop_plane_state->r2r_en =
				s->hdr.sdr2hdr_state.rgb2rgb_post_conv_en;
	}

	return 0;
}

static void vop_enable_debug_irq(struct drm_crtc *crtc)
{
	struct vop *vop = to_vop(crtc);
	uint32_t irqs;

	irqs = BUS_ERROR_INTR | WIN0_EMPTY_INTR | WIN1_EMPTY_INTR |
		WIN2_EMPTY_INTR | WIN3_EMPTY_INTR | HWC_EMPTY_INTR |
		POST_BUF_EMPTY_INTR;
	VOP_INTR_SET_TYPE(vop, clear, irqs, 1);
	VOP_INTR_SET_TYPE(vop, enable, irqs, 1);
}

static void vop_dsp_hold_valid_irq_enable(struct vop *vop)
{
	unsigned long flags;

	spin_lock_irqsave(&vop->irq_lock, flags);

	VOP_INTR_SET_TYPE(vop, clear, DSP_HOLD_VALID_INTR, 1);
	VOP_INTR_SET_TYPE(vop, enable, DSP_HOLD_VALID_INTR, 1);

	spin_unlock_irqrestore(&vop->irq_lock, flags);
}

static void vop_dsp_hold_valid_irq_disable(struct vop *vop)
{
	unsigned long flags;

	spin_lock_irqsave(&vop->irq_lock, flags);

	VOP_INTR_SET_TYPE(vop, enable, DSP_HOLD_VALID_INTR, 0);

	spin_unlock_irqrestore(&vop->irq_lock, flags);
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
static bool vop_line_flag_irq_is_enabled(struct vop *vop)
{
	uint32_t line_flag_irq;
	unsigned long flags;

	spin_lock_irqsave(&vop->irq_lock, flags);

	line_flag_irq = VOP_INTR_GET_TYPE(vop, enable, LINE_FLAG_INTR);

	spin_unlock_irqrestore(&vop->irq_lock, flags);

	return !!line_flag_irq;
}

static void vop_line_flag_irq_enable(struct vop *vop, int line_num)
{
	unsigned long flags;

	if (WARN_ON(!vop->is_enabled))
		return;

	spin_lock_irqsave(&vop->irq_lock, flags);

	VOP_INTR_SET(vop, line_flag_num[0], line_num);
	VOP_INTR_SET_TYPE(vop, clear, LINE_FLAG_INTR, 1);
	VOP_INTR_SET_TYPE(vop, enable, LINE_FLAG_INTR, 1);

	spin_unlock_irqrestore(&vop->irq_lock, flags);
}

static void vop_line_flag_irq_disable(struct vop *vop)
{
	unsigned long flags;

	if (WARN_ON(!vop->is_enabled))
		return;

	spin_lock_irqsave(&vop->irq_lock, flags);

	VOP_INTR_SET_TYPE(vop, enable, LINE_FLAG_INTR, 0);

	spin_unlock_irqrestore(&vop->irq_lock, flags);
}

static void vop_crtc_load_lut(struct drm_crtc *crtc)
{
	struct vop *vop = to_vop(crtc);
	int i, dle, lut_idx = 0;

	if (!vop->is_enabled || !vop->lut || !vop->lut_regs)
		return;

	if (WARN_ON(!drm_modeset_is_locked(&crtc->mutex)))
		return;

	if (!VOP_CTRL_SUPPORT(vop, update_gamma_lut)) {
		spin_lock(&vop->reg_lock);
		VOP_CTRL_SET(vop, dsp_lut_en, 0);
		vop_cfg_done(vop);
		spin_unlock(&vop->reg_lock);

#define CTRL_GET(name) VOP_CTRL_GET(vop, name)
		readx_poll_timeout(CTRL_GET, dsp_lut_en,
				dle, !dle, 5, 33333);
	} else {
		lut_idx = CTRL_GET(lut_buffer_index);
	}

	for (i = 0; i < vop->lut_len; i++)
		vop_write_lut(vop, i << 2, vop->lut[i]);

	spin_lock(&vop->reg_lock);

	VOP_CTRL_SET(vop, dsp_lut_en, 1);
	VOP_CTRL_SET(vop, update_gamma_lut, 1);
	vop_cfg_done(vop);
	vop->lut_active = true;

	spin_unlock(&vop->reg_lock);

	if (VOP_CTRL_SUPPORT(vop, update_gamma_lut)) {
		readx_poll_timeout(CTRL_GET, lut_buffer_index,
				   dle, dle != lut_idx, 5, 33333);
		/* FIXME:
		 * update_gamma value auto clean to 0 by HW, should not
		 * bakeup it.
		 */
		VOP_CTRL_SET(vop, update_gamma_lut, 0);
	}
#undef CTRL_GET
}

void rockchip_vop_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
				    u16 blue, int regno)
{
	struct vop *vop = to_vop(crtc);
	u32 lut_len = vop->lut_len;
	u32 r, g, b;

	if (regno >= lut_len || !vop->lut)
		return;

	r = red * (lut_len - 1) / 0xffff;
	g = green * (lut_len - 1) / 0xffff;
	b = blue * (lut_len - 1) / 0xffff;
	vop->lut[regno] = r * lut_len * lut_len + g * lut_len + b;
}

void rockchip_vop_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
				    u16 *blue, int regno)
{
	struct vop *vop = to_vop(crtc);
	u32 lut_len = vop->lut_len;
	u32 r, g, b;

	if (regno >= lut_len || !vop->lut)
		return;

	r = (vop->lut[regno] / lut_len / lut_len) & (lut_len - 1);
	g = (vop->lut[regno] / lut_len) & (lut_len - 1);
	b = vop->lut[regno] & (lut_len - 1);
	*red = r * 0xffff / (lut_len - 1);
	*green = g * 0xffff / (lut_len - 1);
	*blue = b * 0xffff / (lut_len - 1);
}

static void vop_power_enable(struct drm_crtc *crtc)
{
	struct vop *vop = to_vop(crtc);
	int ret;

	ret = clk_prepare_enable(vop->hclk);
	if (ret < 0) {
		dev_err(vop->dev, "failed to enable hclk - %d\n", ret);
		return;
	}

	ret = clk_prepare_enable(vop->dclk);
	if (ret < 0) {
		dev_err(vop->dev, "failed to enable dclk - %d\n", ret);
		goto err_disable_hclk;
	}

	ret = clk_prepare_enable(vop->aclk);
	if (ret < 0) {
		dev_err(vop->dev, "failed to enable aclk - %d\n", ret);
		goto err_disable_dclk;
	}

	ret = pm_runtime_get_sync(vop->dev);
	if (ret < 0) {
		dev_err(vop->dev, "failed to get pm runtime: %d\n", ret);
		return;
	}

	memcpy(vop->regsbak, vop->regs, vop->len);

	if (VOP_CTRL_SUPPORT(vop, version)) {
		uint32_t version = VOP_CTRL_GET(vop, version);

		/*
		 * Fixup rk3288w version.
		 */
		if (version && version == 0x0a05)
			vop->version = VOP_VERSION(3, 1);
	}

	vop->is_enabled = true;

	return;

err_disable_dclk:
	clk_disable_unprepare(vop->dclk);
err_disable_hclk:
	clk_disable_unprepare(vop->hclk);
}

static void vop_initial(struct drm_crtc *crtc)
{
	struct vop *vop = to_vop(crtc);
	int i;

	vop_power_enable(crtc);

	VOP_CTRL_SET(vop, global_regdone_en, 1);
	VOP_CTRL_SET(vop, dsp_blank, 0);
	VOP_CTRL_SET(vop, axi_outstanding_max_num, 30);
	VOP_CTRL_SET(vop, axi_max_outstanding_en, 1);

	/*
	 * We need to make sure that all windows are disabled before resume
	 * the crtc. Otherwise we might try to scan from a destroyed
	 * buffer later.
	 */
	for (i = 0; i < vop->num_wins; i++) {
		struct vop_win *win = &vop->win[i];
		int channel = i * 2 + 1;

		VOP_WIN_SET(vop, win, channel, (channel + 1) << 4 | channel);
	}
	VOP_CTRL_SET(vop, afbdc_en, 0);
	vop_enable_debug_irq(crtc);
}

static void vop_crtc_disable(struct drm_crtc *crtc)
{
	struct vop *vop = to_vop(crtc);
	int sys_status = drm_crtc_index(crtc) ?
				SYS_STATUS_LCDC1 : SYS_STATUS_LCDC0;

	vop_lock(vop);
	VOP_CTRL_SET(vop, reg_done_frm, 1);
	VOP_CTRL_SET(vop, dsp_interlace, 0);
	drm_crtc_vblank_off(crtc);
	vop_disable_all_planes(vop);

	/*
	 * Vop standby will take effect at end of current frame,
	 * if dsp hold valid irq happen, it means standby complete.
	 *
	 * we must wait standby complete when we want to disable aclk,
	 * if not, memory bus maybe dead.
	 */
	reinit_completion(&vop->dsp_hold_completion);
	vop_dsp_hold_valid_irq_enable(vop);

	spin_lock(&vop->reg_lock);

	VOP_CTRL_SET(vop, standby, 1);

	spin_unlock(&vop->reg_lock);

	WARN_ON(!wait_for_completion_timeout(&vop->dsp_hold_completion,
					     msecs_to_jiffies(50)));

	vop_dsp_hold_valid_irq_disable(vop);

	disable_irq(vop->irq);

	vop->is_enabled = false;
	smp_wmb();
	if (vop->is_iommu_enabled) {
		/*
		 * vop standby complete, so iommu detach is safe.
		 */
		VOP_CTRL_SET(vop, dma_stop, 1);
		rockchip_drm_dma_detach_device(vop->drm_dev, vop->dev);
		vop->is_iommu_enabled = false;
	}

	pm_runtime_put(vop->dev);
	clk_disable_unprepare(vop->dclk);
	clk_disable_unprepare(vop->aclk);
	clk_disable_unprepare(vop->hclk);
	vop_unlock(vop);

	rockchip_clear_system_status(sys_status);

	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}
}

static void vop_plane_destroy(struct drm_plane *plane)
{
	drm_plane_cleanup(plane);
}

static int vop_plane_prepare_fb(struct drm_plane *plane,
				const struct drm_plane_state *new_state)
{
	if (plane->state->fb)
		drm_framebuffer_reference(plane->state->fb);

	return 0;
}

static void vop_plane_cleanup_fb(struct drm_plane *plane,
				 const struct drm_plane_state *old_state)
{
	if (old_state->fb)
		drm_framebuffer_unreference(old_state->fb);
}

static int vop_plane_atomic_check(struct drm_plane *plane,
			   struct drm_plane_state *state)
{
	struct drm_crtc *crtc = state->crtc;
	struct drm_framebuffer *fb = state->fb;
	struct vop_win *win = to_vop_win(plane);
	struct vop_plane_state *vop_plane_state = to_vop_plane_state(state);
	struct drm_crtc_state *crtc_state;
	const struct vop_data *vop_data;
	struct vop *vop;
	bool visible;
	int ret;
	struct drm_rect *dest = &vop_plane_state->dest;
	struct drm_rect *src = &vop_plane_state->src;
	struct drm_rect clip;
	int min_scale = win->phy->scl ? FRAC_16_16(1, 8) :
					DRM_PLANE_HELPER_NO_SCALING;
	int max_scale = win->phy->scl ? FRAC_16_16(8, 1) :
					DRM_PLANE_HELPER_NO_SCALING;
	unsigned long offset;
	dma_addr_t dma_addr;
	void *kvaddr;
	u16 vdisplay;

	crtc = crtc ? crtc : plane->state->crtc;
	/*
	 * Both crtc or plane->state->crtc can be null.
	 */
	if (!crtc || !fb)
		goto out_disable;

	crtc_state = drm_atomic_get_crtc_state(state->state, crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	src->x1 = state->src_x;
	src->y1 = state->src_y;
	src->x2 = state->src_x + state->src_w;
	src->y2 = state->src_y + state->src_h;
	dest->x1 = state->crtc_x;
	dest->y1 = state->crtc_y;
	dest->x2 = state->crtc_x + state->crtc_w;
	dest->y2 = state->crtc_y + state->crtc_h;

	vdisplay = crtc_state->adjusted_mode.crtc_vdisplay;
	if (crtc_state->adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE)
		vdisplay *= 2;

	clip.x1 = 0;
	clip.y1 = 0;
	clip.x2 = crtc_state->adjusted_mode.crtc_hdisplay;
	clip.y2 = vdisplay;

	ret = drm_plane_helper_check_update(plane, crtc, state->fb,
					    src, dest, &clip,
					    min_scale,
					    max_scale,
					    true, true, &visible);
	if (ret)
		return ret;

	if (!visible)
		goto out_disable;

	vop_plane_state->format = vop_convert_format(fb->pixel_format);
	if (vop_plane_state->format < 0)
		return vop_plane_state->format;

	vop = to_vop(crtc);
	vop_data = vop->data;

	if (drm_rect_width(src) >> 16 > vop_data->max_input.width ||
	    drm_rect_height(src) >> 16 > vop_data->max_input.height) {
		DRM_ERROR("Invalid source: %dx%d. max input: %dx%d\n",
			  drm_rect_width(src) >> 16,
			  drm_rect_height(src) >> 16,
			  vop_data->max_input.width,
			  vop_data->max_input.height);
		return -EINVAL;
	}

	/*
	 * Src.x1 can be odd when do clip, but yuv plane start point
	 * need align with 2 pixel.
	 */
	if (is_yuv_support(fb->pixel_format) && ((src->x1 >> 16) % 2)) {
		DRM_ERROR("Invalid Source: Yuv format Can't support odd xpos\n");
		return -EINVAL;
	}

	offset = (src->x1 >> 16) * drm_format_plane_bpp(fb->pixel_format, 0) / 8;
	vop_plane_state->offset = offset + fb->offsets[0];
	if (state->rotation & BIT(DRM_REFLECT_Y) ||
	    (rockchip_fb_is_logo(fb) && vop_plane_state->logo_ymirror))
		offset += ((src->y2 >> 16) - 1) * fb->pitches[0];
	else
		offset += (src->y1 >> 16) * fb->pitches[0];

	dma_addr = rockchip_fb_get_dma_addr(fb, 0);
	kvaddr = rockchip_fb_get_kvaddr(fb, 0);
	vop_plane_state->yrgb_mst = dma_addr + offset + fb->offsets[0];
	vop_plane_state->yrgb_kvaddr = kvaddr + offset + fb->offsets[0];
	if (is_yuv_support(fb->pixel_format)) {
		int hsub = drm_format_horz_chroma_subsampling(fb->pixel_format);
		int vsub = drm_format_vert_chroma_subsampling(fb->pixel_format);
		int bpp = drm_format_plane_bpp(fb->pixel_format, 1);

		offset = (src->x1 >> 16) * bpp / hsub / 8;
		offset += (src->y1 >> 16) * fb->pitches[1] / vsub;

		dma_addr = rockchip_fb_get_dma_addr(fb, 1);
		dma_addr += offset + fb->offsets[1];
		vop_plane_state->uv_mst = dma_addr;
	}

	vop_plane_state->enable = true;

	return 0;

out_disable:
	vop_plane_state->enable = false;
	return 0;
}

static void vop_plane_atomic_disable(struct drm_plane *plane,
				     struct drm_plane_state *old_state)
{
	struct vop_plane_state *vop_plane_state = to_vop_plane_state(old_state);
	struct vop_win *win = to_vop_win(plane);
	struct vop *vop = to_vop(old_state->crtc);

	if (!old_state->crtc)
		return;

	spin_lock(&vop->reg_lock);

	/*
	 * FIXUP: some of the vop scale would be abnormal after windows power
	 * on/off so deinit scale to scale_none mode.
	 */
	if (win->phy->scl && win->phy->scl->ext) {
		VOP_SCL_SET_EXT(vop, win, yrgb_hor_scl_mode, SCALE_NONE);
		VOP_SCL_SET_EXT(vop, win, yrgb_ver_scl_mode, SCALE_NONE);
		VOP_SCL_SET_EXT(vop, win, cbcr_hor_scl_mode, SCALE_NONE);
		VOP_SCL_SET_EXT(vop, win, cbcr_ver_scl_mode, SCALE_NONE);
	}
	VOP_WIN_SET(vop, win, enable, 0);
	if (win->area_id == 0)
		VOP_WIN_SET(vop, win, gate, 0);

	/*
	 * IC design bug: in the bandwidth tension environment when close win2,
	 * vop will access the freed memory lead to iommu pagefault.
	 * so we add this reset to workaround.
	 */
	if (VOP_MAJOR(vop->version) == 2 && VOP_MINOR(vop->version) == 5 &&
	     win->win_id == 2)
		VOP_WIN_SET(vop, win, yrgb_mst, 0);

	spin_unlock(&vop->reg_lock);

	vop_plane_state->enable = false;
}

static void vop_plane_atomic_update(struct drm_plane *plane,
		struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = plane->state;
	struct drm_crtc *crtc = state->crtc;
	struct drm_display_mode *mode = NULL;
	struct vop_win *win = to_vop_win(plane);
	struct vop_plane_state *vop_plane_state = to_vop_plane_state(state);
	struct rockchip_crtc_state *s;
	struct vop *vop;
	struct drm_framebuffer *fb = state->fb;
	unsigned int actual_w, actual_h;
	unsigned int dsp_stx, dsp_sty;
	uint32_t act_info, dsp_info, dsp_st;
	struct drm_rect *src = &vop_plane_state->src;
	struct drm_rect *dest = &vop_plane_state->dest;
	const uint32_t *y2r_table = vop_plane_state->y2r_table;
	const uint32_t *r2r_table = vop_plane_state->r2r_table;
	const uint32_t *r2y_table = vop_plane_state->r2y_table;
	int ymirror, xmirror;
	uint32_t val;
	bool rb_swap, global_alpha_en;

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
	if (fb->modifier[0] == DRM_FORMAT_MOD_ARM_AFBC)
		AFBC_flag = true;
	else
		AFBC_flag = false;
#endif
	/*
	 * can't update plane when vop is disabled.
	 */
	if (!crtc)
		return;

	if (!vop_plane_state->enable) {
		vop_plane_atomic_disable(plane, old_state);
		return;
	}

	mode = &crtc->state->adjusted_mode;
	actual_w = drm_rect_width(src) >> 16;
	actual_h = drm_rect_height(src) >> 16;
	act_info = (actual_h - 1) << 16 | ((actual_w - 1) & 0xffff);

	dsp_info = (drm_rect_height(dest) - 1) << 16;
	dsp_info |= (drm_rect_width(dest) - 1) & 0xffff;

	dsp_stx = dest->x1 + mode->crtc_htotal - mode->crtc_hsync_start;
	dsp_sty = dest->y1 + mode->crtc_vtotal - mode->crtc_vsync_start;
	dsp_st = dsp_sty << 16 | (dsp_stx & 0xffff);

	ymirror = state->rotation & BIT(DRM_REFLECT_Y) ||
		  (rockchip_fb_is_logo(fb) && vop_plane_state->logo_ymirror);
	xmirror = !!(state->rotation & BIT(DRM_REFLECT_X));

	vop = to_vop(state->crtc);
	s = to_rockchip_crtc_state(crtc->state);

	spin_lock(&vop->reg_lock);

	VOP_WIN_SET(vop, win, xmirror, xmirror);
	VOP_WIN_SET(vop, win, ymirror, ymirror);
	VOP_WIN_SET(vop, win, format, vop_plane_state->format);
	VOP_WIN_SET(vop, win, yrgb_vir, fb->pitches[0] >> 2);
	VOP_WIN_SET(vop, win, yrgb_mst, vop_plane_state->yrgb_mst);
	if (is_yuv_support(fb->pixel_format)) {
		VOP_WIN_SET(vop, win, uv_vir, fb->pitches[1] >> 2);
		VOP_WIN_SET(vop, win, uv_mst, vop_plane_state->uv_mst);
	}
	VOP_WIN_SET(vop, win, fmt_10, is_yuv_10bit(fb->pixel_format));

	scl_vop_cal_scl_fac(vop, win, actual_w, actual_h,
			    drm_rect_width(dest), drm_rect_height(dest),
			    fb->pixel_format);

	VOP_WIN_SET(vop, win, act_info, act_info);
	VOP_WIN_SET(vop, win, dsp_info, dsp_info);
	VOP_WIN_SET(vop, win, dsp_st, dsp_st);

	rb_swap = has_rb_swapped(fb->pixel_format);
	VOP_WIN_SET(vop, win, rb_swap, rb_swap);

	global_alpha_en = (vop_plane_state->global_alpha == 0xff) ? 0 : 1;
	if ((is_alpha_support(fb->pixel_format) || global_alpha_en) &&
	    (s->dsp_layer_sel & 0x3) != win->win_id) {
		int src_bland_m0;

		if (is_alpha_support(fb->pixel_format) && global_alpha_en)
			src_bland_m0 = ALPHA_PER_PIX_GLOBAL;
		else if (is_alpha_support(fb->pixel_format))
			src_bland_m0 = ALPHA_PER_PIX;
		else
			src_bland_m0 = ALPHA_GLOBAL;

		VOP_WIN_SET(vop, win, dst_alpha_ctl,
			    DST_FACTOR_M0(ALPHA_SRC_INVERSE));
		val = SRC_ALPHA_EN(1) | SRC_COLOR_M0(ALPHA_SRC_PRE_MUL) |
			SRC_ALPHA_M0(ALPHA_STRAIGHT) |
			SRC_BLEND_M0(src_bland_m0) |
			SRC_ALPHA_CAL_M0(ALPHA_NO_SATURATION) |
			SRC_FACTOR_M0(global_alpha_en ?
				      ALPHA_SRC_GLOBAL : ALPHA_ONE);
		VOP_WIN_SET(vop, win, src_alpha_ctl, val);
		VOP_WIN_SET(vop, win, alpha_pre_mul,
			    vop_plane_state->blend_mode);
		VOP_WIN_SET(vop, win, alpha_mode, 1);
		VOP_WIN_SET(vop, win, alpha_en, 1);
	} else {
		VOP_WIN_SET(vop, win, src_alpha_ctl, SRC_ALPHA_EN(0));
		VOP_WIN_SET(vop, win, alpha_en, 0);
	}
	VOP_WIN_SET(vop, win, global_alpha_val, vop_plane_state->global_alpha);

	VOP_WIN_SET(vop, win, csc_mode, vop_plane_state->csc_mode);
	if (win->csc) {
		vop_load_csc_table(vop, win->csc->y2r_offset, y2r_table);
		vop_load_csc_table(vop, win->csc->r2r_offset, r2r_table);
		vop_load_csc_table(vop, win->csc->r2y_offset, r2y_table);
		VOP_WIN_SET_EXT(vop, win, csc, y2r_en, vop_plane_state->y2r_en);
		VOP_WIN_SET_EXT(vop, win, csc, r2r_en, vop_plane_state->r2r_en);
		VOP_WIN_SET_EXT(vop, win, csc, r2y_en, vop_plane_state->r2y_en);
	}
	VOP_WIN_SET(vop, win, enable, 1);
	VOP_WIN_SET(vop, win, gate, 1);
	spin_unlock(&vop->reg_lock);
	/*
	 * spi interface(vop_plane_state->yrgb_kvaddr, fb->pixel_format,
	 * actual_w, actual_h)
	 */
	vop->is_iommu_needed = true;

#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	planlist = kmalloc(sizeof(*planlist), GFP_KERNEL);
	if (planlist) {
		planlist->dump_info.AFBC_flag = AFBC_flag;
		planlist->dump_info.area_id = win->area_id;
		planlist->dump_info.win_id = win->win_id;
		planlist->dump_info.yuv_format =
			is_yuv_support(fb->pixel_format);
		planlist->dump_info.num_pages = num_pages;
		planlist->dump_info.pages = pages;
		planlist->dump_info.offset = vop_plane_state->offset;
		planlist->dump_info.pitches = fb->pitches[0];
		planlist->dump_info.height = actual_h;
		planlist->dump_info.pixel_format = fb->pixel_format;
		list_add_tail(&planlist->entry, &crtc->vop_dump_list_head);
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

static const struct drm_plane_helper_funcs plane_helper_funcs = {
	.prepare_fb = vop_plane_prepare_fb,
	.cleanup_fb = vop_plane_cleanup_fb,
	.atomic_check = vop_plane_atomic_check,
	.atomic_update = vop_plane_atomic_update,
	.atomic_disable = vop_plane_atomic_disable,
};

static void vop_atomic_plane_reset(struct drm_plane *plane)
{
	struct vop_win *win = to_vop_win(plane);
	struct vop_plane_state *vop_plane_state =
					to_vop_plane_state(plane->state);

	if (plane->state && plane->state->fb)
		drm_framebuffer_unreference(plane->state->fb);

	kfree(vop_plane_state);
	vop_plane_state = kzalloc(sizeof(*vop_plane_state), GFP_KERNEL);
	if (!vop_plane_state)
		return;

	vop_plane_state->zpos = win->win_id;
	vop_plane_state->global_alpha = 0xff;
	plane->state = &vop_plane_state->base;
	plane->state->plane = plane;
}

static struct drm_plane_state *
vop_atomic_plane_duplicate_state(struct drm_plane *plane)
{
	struct vop_plane_state *old_vop_plane_state;
	struct vop_plane_state *vop_plane_state;

	if (WARN_ON(!plane->state))
		return NULL;

	old_vop_plane_state = to_vop_plane_state(plane->state);
	vop_plane_state = kmemdup(old_vop_plane_state,
				  sizeof(*vop_plane_state), GFP_KERNEL);
	if (!vop_plane_state)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane,
						  &vop_plane_state->base);

	return &vop_plane_state->base;
}

static void vop_atomic_plane_destroy_state(struct drm_plane *plane,
					   struct drm_plane_state *state)
{
	struct vop_plane_state *vop_state = to_vop_plane_state(state);

	__drm_atomic_helper_plane_destroy_state(plane, state);

	kfree(vop_state);
}

static int vop_atomic_plane_set_property(struct drm_plane *plane,
					 struct drm_plane_state *state,
					 struct drm_property *property,
					 uint64_t val)
{
	struct rockchip_drm_private *private = plane->dev->dev_private;
	struct vop_win *win = to_vop_win(plane);
	struct vop_plane_state *plane_state = to_vop_plane_state(state);

	if (property == win->vop->plane_zpos_prop) {
		plane_state->zpos = val;
		return 0;
	}

	if (property == win->rotation_prop) {
		state->rotation = val;
		return 0;
	}

	if (property == private->logo_ymirror_prop) {
		WARN_ON(!rockchip_fb_is_logo(state->fb));
		plane_state->logo_ymirror = val;
		return 0;
	}

	if (property == private->eotf_prop) {
		plane_state->eotf = val;
		return 0;
	}

	if (property == private->color_space_prop) {
		plane_state->color_space = val;
		return 0;
	}

	if (property == private->global_alpha_prop) {
		plane_state->global_alpha = val;
		return 0;
	}

	if (property == private->blend_mode_prop) {
		plane_state->blend_mode = val;
		return 0;
	}

	DRM_ERROR("failed to set vop plane property\n");
	return -EINVAL;
}

static int vop_atomic_plane_get_property(struct drm_plane *plane,
					 const struct drm_plane_state *state,
					 struct drm_property *property,
					 uint64_t *val)
{
	struct vop_win *win = to_vop_win(plane);
	struct vop_plane_state *plane_state = to_vop_plane_state(state);
	struct rockchip_drm_private *private = plane->dev->dev_private;

	if (property == win->vop->plane_zpos_prop) {
		*val = plane_state->zpos;
		return 0;
	}

	if (property == win->rotation_prop) {
		*val = state->rotation;
		return 0;
	}

	if (property == private->eotf_prop) {
		*val = plane_state->eotf;
		return 0;
	}

	if (property == private->color_space_prop) {
		*val = plane_state->color_space;
		return 0;
	}

	if (property == private->global_alpha_prop) {
		*val = plane_state->global_alpha;
		return 0;
	}

	if (property == private->blend_mode_prop) {
		*val = plane_state->blend_mode;
		return 0;
	}

	DRM_ERROR("failed to get vop plane property\n");
	return -EINVAL;
}

static const struct drm_plane_funcs vop_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy = vop_plane_destroy,
	.reset = vop_atomic_plane_reset,
	.set_property = drm_atomic_helper_plane_set_property,
	.atomic_duplicate_state = vop_atomic_plane_duplicate_state,
	.atomic_destroy_state = vop_atomic_plane_destroy_state,
	.atomic_set_property = vop_atomic_plane_set_property,
	.atomic_get_property = vop_atomic_plane_get_property,
};

static int vop_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct vop *vop = to_vop(crtc);
	unsigned long flags;

	if (!vop->is_enabled)
		return -EPERM;

	spin_lock_irqsave(&vop->irq_lock, flags);

	if (VOP_MAJOR(vop->version) == 3 && VOP_MINOR(vop->version) >= 7) {
		VOP_INTR_SET_TYPE(vop, clear, FS_FIELD_INTR, 1);
		VOP_INTR_SET_TYPE(vop, enable, FS_FIELD_INTR, 1);
	} else {
		VOP_INTR_SET_TYPE(vop, clear, FS_INTR, 1);
		VOP_INTR_SET_TYPE(vop, enable, FS_INTR, 1);
	}

	spin_unlock_irqrestore(&vop->irq_lock, flags);

	return 0;
}

static void vop_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct vop *vop = to_vop(crtc);
	unsigned long flags;

	if (!vop->is_enabled)
		return;

	spin_lock_irqsave(&vop->irq_lock, flags);

	if (VOP_MAJOR(vop->version) == 3 && VOP_MINOR(vop->version) >= 7)
		VOP_INTR_SET_TYPE(vop, enable, FS_FIELD_INTR, 0);
	else
		VOP_INTR_SET_TYPE(vop, enable, FS_INTR, 0);

	spin_unlock_irqrestore(&vop->irq_lock, flags);
}

static void vop_crtc_cancel_pending_vblank(struct drm_crtc *crtc,
					   struct drm_file *file_priv)
{
	struct drm_device *drm = crtc->dev;
	struct vop *vop = to_vop(crtc);
	struct drm_pending_vblank_event *e;
	unsigned long flags;

	spin_lock_irqsave(&drm->event_lock, flags);
	e = vop->event;
	if (e && e->base.file_priv == file_priv) {
		vop->event = NULL;

		e->base.destroy(&e->base);
		file_priv->event_space += sizeof(e->event);
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);
}

static int vop_crtc_loader_protect(struct drm_crtc *crtc, bool on)
{
	struct rockchip_drm_private *private = crtc->dev->dev_private;
	struct vop *vop = to_vop(crtc);
	int sys_status = drm_crtc_index(crtc) ?
				SYS_STATUS_LCDC1 : SYS_STATUS_LCDC0;

	if (on == vop->loader_protect)
		return 0;

	if (on) {
		if (vop->dclk_source) {
			struct clk *parent;

			parent = clk_get_parent(vop->dclk_source);
			if (parent) {
				if (clk_is_match(private->default_pll.pll, parent))
					vop->pll = &private->default_pll;
				else if (clk_is_match(private->hdmi_pll.pll, parent))
					vop->pll = &private->hdmi_pll;
				if (vop->pll)
					vop->pll->use_count++;
			}
		}

		rockchip_set_system_status(sys_status);
		vop_initial(crtc);
		enable_irq(vop->irq);
		drm_crtc_vblank_on(crtc);
		vop->loader_protect = true;
	} else {
		vop_crtc_disable(crtc);

		if (vop->dclk_source && vop->pll) {
			vop->pll->use_count--;
			vop->pll = NULL;
		}
		vop->loader_protect = false;
	}

	return 0;
}

#define DEBUG_PRINT(args...) \
		do { \
			if (s) \
				seq_printf(s, args); \
			else \
				printk(args); \
		} while (0)

static int vop_plane_info_dump(struct seq_file *s, struct drm_plane *plane)
{
	struct vop_win *win = to_vop_win(plane);
	struct drm_plane_state *state = plane->state;
	struct vop_plane_state *pstate = to_vop_plane_state(state);
	struct drm_rect *src, *dest;
	struct drm_framebuffer *fb = state->fb;
	int i;

	DEBUG_PRINT("    win%d-%d: %s\n", win->win_id, win->area_id,
		    pstate->enable ? "ACTIVE" : "DISABLED");
	if (!fb)
		return 0;

	src = &pstate->src;
	dest = &pstate->dest;

	DEBUG_PRINT("\tformat: %s%s%s[%d] color_space[%d]\n",
		    drm_get_format_name(fb->pixel_format),
		    fb->modifier[0] == DRM_FORMAT_MOD_ARM_AFBC ? "[AFBC]" : "",
		    pstate->eotf ? " HDR" : " SDR", pstate->eotf,
		    pstate->color_space);
	DEBUG_PRINT("\tcsc: y2r[%d] r2r[%d] r2y[%d] csc mode[%d]\n",
		    pstate->y2r_en, pstate->r2r_en, pstate->r2y_en,
		    pstate->csc_mode);
	DEBUG_PRINT("\tzpos: %d\n", pstate->zpos);
	DEBUG_PRINT("\tsrc: pos[%dx%d] rect[%dx%d]\n", src->x1 >> 16,
		    src->y1 >> 16, drm_rect_width(src) >> 16,
		    drm_rect_height(src) >> 16);
	DEBUG_PRINT("\tdst: pos[%dx%d] rect[%dx%d]\n", dest->x1, dest->y1,
		    drm_rect_width(dest), drm_rect_height(dest));

	for (i = 0; i < drm_format_num_planes(fb->pixel_format); i++) {
		dma_addr_t fb_addr = rockchip_fb_get_dma_addr(fb, i);
		DEBUG_PRINT("\tbuf[%d]: addr: %pad pitch: %d offset: %d\n",
			    i, &fb_addr, fb->pitches[i], fb->offsets[i]);
	}

	return 0;
}

static int vop_crtc_debugfs_dump(struct drm_crtc *crtc, struct seq_file *s)
{
	struct vop *vop = to_vop(crtc);
	struct drm_crtc_state *crtc_state = crtc->state;
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct rockchip_crtc_state *state = to_rockchip_crtc_state(crtc->state);
	bool interlaced = !!(mode->flags & DRM_MODE_FLAG_INTERLACE);
	struct drm_plane *plane;
	int i;

	DEBUG_PRINT("VOP [%s]: %s\n", dev_name(vop->dev),
		    crtc_state->active ? "ACTIVE" : "DISABLED");

	if (!crtc_state->active)
		return 0;

	DEBUG_PRINT("    Connector: %s\n",
		    drm_get_connector_name(state->output_type));
	DEBUG_PRINT("\toverlay_mode[%d] bus_format[%x] output_mode[%x]",
		    state->yuv_overlay, state->bus_format, state->output_mode);
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

	for (i = 0; i < vop->num_wins; i++) {
		plane = &vop->win[i].base;
		vop_plane_info_dump(s, plane);
	}
	DEBUG_PRINT("    post: sdr2hdr[%d] hdr2sdr[%d]\n",
		    state->hdr.sdr2hdr_state.bt1886eotf_post_conv_en,
		    state->hdr.hdr2sdr_en);
	DEBUG_PRINT("    pre : sdr2hdr[%d]\n",
		    state->hdr.sdr2hdr_state.bt1886eotf_pre_conv_en);
	DEBUG_PRINT("    post CSC: r2y[%d] y2r[%d] CSC mode[%d]\n",
		    state->post_r2y_en, state->post_y2r_en,
		    state->post_csc_mode);

	return 0;
}

static void vop_crtc_regs_dump(struct drm_crtc *crtc, struct seq_file *s)
{
	struct vop *vop = to_vop(crtc);
	struct drm_crtc_state *crtc_state = crtc->state;
	int dump_len = vop->len > 0x400 ? 0x400 : vop->len;
	int i;

	if (!crtc_state->active)
		return;

	for (i = 0; i < dump_len; i += 4) {
		if (i % 16 == 0)
			DEBUG_PRINT("\n0x%08x: ", i);
		DEBUG_PRINT("%08x ", vop_readl(vop, i));
	}
}

static int vop_gamma_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct vop *vop = node->info_ent->data;
	int i;

	if (!vop->lut || !vop->lut_active || !vop->lut_regs)
		return 0;

	for (i = 0; i < vop->lut_len; i++) {
		if (i % 8 == 0)
			DEBUG_PRINT("\n");
		DEBUG_PRINT("0x%08x ", vop->lut[i]);
	}
	DEBUG_PRINT("\n");

	return 0;
}

#undef DEBUG_PRINT

static struct drm_info_list vop_debugfs_files[] = {
	{ "gamma_lut", vop_gamma_show, 0, NULL },
};

static int vop_crtc_debugfs_init(struct drm_minor *minor, struct drm_crtc *crtc)
{
	struct vop *vop = to_vop(crtc);
	int ret, i;

	vop->debugfs = debugfs_create_dir(dev_name(vop->dev),
					  minor->debugfs_root);

	if (!vop->debugfs)
		return -ENOMEM;

	vop->debugfs_files = kmemdup(vop_debugfs_files,
				     sizeof(vop_debugfs_files),
				     GFP_KERNEL);
	if (!vop->debugfs_files) {
		ret = -ENOMEM;
		goto remove;
	}
#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	drm_debugfs_vop_add(crtc, vop->debugfs);
#endif
	for (i = 0; i < ARRAY_SIZE(vop_debugfs_files); i++)
		vop->debugfs_files[i].data = vop;

	ret = drm_debugfs_create_files(vop->debugfs_files,
				       ARRAY_SIZE(vop_debugfs_files),
				       vop->debugfs,
				       minor);
	if (ret) {
		dev_err(vop->dev, "could not install rockchip_debugfs_list\n");
		goto free;
	}

	return 0;
free:
	kfree(vop->debugfs_files);
	vop->debugfs_files = NULL;
remove:
	debugfs_remove(vop->debugfs);
	vop->debugfs = NULL;
	return ret;
}

static enum drm_mode_status
vop_crtc_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *mode,
		    int output_type)
{
	struct vop *vop = to_vop(crtc);
	const struct vop_data *vop_data = vop->data;
	int request_clock = mode->clock;
	int clock;

	if (mode->hdisplay > vop_data->max_output.width)
		return MODE_BAD_HVALUE;

	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) &&
	    VOP_MAJOR(vop->version) == 3 &&
	    VOP_MINOR(vop->version) <= 2)
		return MODE_BAD;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		request_clock *= 2;
	clock = clk_round_rate(vop->dclk, request_clock * 1000) / 1000;

	/*
	 * Hdmi or DisplayPort request a Accurate clock.
	 */
	if (output_type == DRM_MODE_CONNECTOR_HDMIA ||
	    output_type == DRM_MODE_CONNECTOR_DisplayPort)
		if (clock != request_clock)
			return MODE_CLOCK_RANGE;

	return MODE_OK;
}

struct vop_bandwidth {
	size_t bandwidth;
	int y1;
	int y2;
};

static int vop_bandwidth_cmp(const void *a, const void *b)
{
	struct vop_bandwidth *pa = (struct vop_bandwidth *)a;
	struct vop_bandwidth *pb = (struct vop_bandwidth *)b;

	return pa->y1 - pb->y2;
}

static size_t vop_plane_line_bandwidth(struct drm_plane_state *pstate)
{
	struct vop_plane_state *vop_plane_state = to_vop_plane_state(pstate);
	struct vop_win *win = to_vop_win(pstate->plane);
	struct drm_crtc *crtc = pstate->crtc;
	struct vop *vop = to_vop(crtc);
	struct drm_framebuffer *fb = pstate->fb;
	struct drm_rect *dest = &vop_plane_state->dest;
	struct drm_rect *src = &vop_plane_state->src;
	int bpp = drm_format_plane_bpp(fb->pixel_format, 0);
	int src_width = drm_rect_width(src) >> 16;
	int src_height = drm_rect_height(src) >> 16;
	int dest_width = drm_rect_width(dest);
	int dest_height = drm_rect_height(dest);
	int vskiplines = scl_get_vskiplines(src_height, dest_height);
	size_t bandwidth;

	if (!src_width || !src_height || !dest_width || !dest_height)
		return 0;

	bandwidth = src_width * bpp / 8;

	bandwidth = bandwidth * src_width / dest_width;
	bandwidth = bandwidth * src_height / dest_height;
	if (vskiplines == 2 && VOP_WIN_SCL_EXT_SUPPORT(vop, win, vsd_yrgb_gt2))
		bandwidth /= 2;
	else if (vskiplines == 4 &&
		 VOP_WIN_SCL_EXT_SUPPORT(vop, win, vsd_yrgb_gt4))
		bandwidth /= 4;

	return bandwidth;
}

static u64 vop_calc_max_bandwidth(struct vop_bandwidth *bw, int start,
				  int count, int y2)
{
	u64 max_bandwidth = 0;
	int i;

	for (i = start; i < count; i++) {
		u64 bandwidth = 0;

		if (bw[i].y1 > y2)
			continue;
		bandwidth = bw[i].bandwidth;
		bandwidth += vop_calc_max_bandwidth(bw, i + 1, count,
						    min(bw[i].y2, y2));

		if (bandwidth > max_bandwidth)
			max_bandwidth = bandwidth;
	}

	return max_bandwidth;
}

static size_t vop_crtc_bandwidth(struct drm_crtc *crtc,
				 struct drm_crtc_state *crtc_state)
{
	struct drm_atomic_state *state = crtc_state->state;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	u16 htotal = adjusted_mode->crtc_htotal;
	u16 vdisplay = adjusted_mode->crtc_vdisplay;
	int clock = adjusted_mode->crtc_clock;
	struct vop_plane_state *vop_plane_state;
	struct drm_plane_state *pstate;
	struct vop_bandwidth *pbandwidth;
	struct drm_plane *plane;
	u64 bandwidth;
	int i, cnt = 0, plane_num = 0;

	if (!htotal || !vdisplay)
		return 0;

	for_each_plane_in_state(state, plane, pstate, i) {
		if (pstate->crtc != crtc || !pstate->fb)
			continue;
		plane_num++;
	}
	pbandwidth = kmalloc_array(plane_num, sizeof(*pbandwidth),
				   GFP_KERNEL);
	if (!pbandwidth)
		return -ENOMEM;

	for_each_plane_in_state(state, plane, pstate, i) {
		if (pstate->crtc != crtc || !pstate->fb)
			continue;

		vop_plane_state = to_vop_plane_state(pstate);
		pbandwidth[cnt].y1 = vop_plane_state->dest.y1;
		pbandwidth[cnt].y2 = vop_plane_state->dest.y2;
		pbandwidth[cnt++].bandwidth = vop_plane_line_bandwidth(pstate);
	}

	sort(pbandwidth, cnt, sizeof(pbandwidth[0]), vop_bandwidth_cmp, NULL);

	bandwidth = vop_calc_max_bandwidth(pbandwidth, 0, cnt, vdisplay);
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

static void vop_crtc_close(struct drm_crtc *crtc)
{
	struct vop *vop = NULL;

	if (!crtc)
		return;
	vop = to_vop(crtc);
	mutex_lock(&vop->vop_lock);
	if (!vop->is_enabled) {
		mutex_unlock(&vop->vop_lock);
		return;
	}

	vop_disable_all_planes(vop);
	mutex_unlock(&vop->vop_lock);
}

static const struct rockchip_crtc_funcs private_crtc_funcs = {
	.loader_protect = vop_crtc_loader_protect,
	.enable_vblank = vop_crtc_enable_vblank,
	.disable_vblank = vop_crtc_disable_vblank,
	.cancel_pending_vblank = vop_crtc_cancel_pending_vblank,
	.debugfs_init = vop_crtc_debugfs_init,
	.debugfs_dump = vop_crtc_debugfs_dump,
	.regs_dump = vop_crtc_regs_dump,
	.mode_valid = vop_crtc_mode_valid,
	.bandwidth = vop_crtc_bandwidth,
	.crtc_close = vop_crtc_close,
};

static bool vop_crtc_mode_fixup(struct drm_crtc *crtc,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adj_mode)
{
	struct vop *vop = to_vop(crtc);
	const struct vop_data *vop_data = vop->data;

	if (mode->hdisplay > vop_data->max_output.width)
		return false;

	drm_mode_set_crtcinfo(adj_mode,
			      CRTC_INTERLACE_HALVE_V | CRTC_STEREO_DOUBLE);

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		adj_mode->crtc_clock *= 2;

	adj_mode->crtc_clock =
		clk_round_rate(vop->dclk, adj_mode->crtc_clock * 1000) / 1000;

	return true;
}

static void vop_update_csc(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *s =
			to_rockchip_crtc_state(crtc->state);
	struct vop *vop = to_vop(crtc);
	u32 val;

	if (s->output_mode == ROCKCHIP_OUT_MODE_AAAA &&
	    !(vop->data->feature & VOP_FEATURE_OUTPUT_10BIT))
		s->output_mode = ROCKCHIP_OUT_MODE_P888;

	if (is_uv_swap(s->bus_format, s->output_mode))
		VOP_CTRL_SET(vop, dsp_data_swap, DSP_RB_SWAP);
	else
		VOP_CTRL_SET(vop, dsp_data_swap, 0);

	VOP_CTRL_SET(vop, out_mode, s->output_mode);

	switch (s->bus_format) {
	case MEDIA_BUS_FMT_RGB565_1X16:
		VOP_CTRL_SET(vop, dither_down_en, 1);
		VOP_CTRL_SET(vop, dither_down_mode, RGB888_TO_RGB565);
		break;
	case MEDIA_BUS_FMT_RGB666_1X18:
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
		VOP_CTRL_SET(vop, dither_down_en, 1);
		VOP_CTRL_SET(vop, dither_down_mode, RGB888_TO_RGB666);
		break;
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
		VOP_CTRL_SET(vop, dither_down_en, 0);
		VOP_CTRL_SET(vop, pre_dither_down_en, 1);
		break;
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
		VOP_CTRL_SET(vop, dither_down_en, 0);
		VOP_CTRL_SET(vop, pre_dither_down_en, 0);
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
	default:
		VOP_CTRL_SET(vop, dither_down_en, 0);
		VOP_CTRL_SET(vop, pre_dither_down_en, 0);
		break;
	}

	VOP_CTRL_SET(vop, pre_dither_down_en,
		     s->output_mode == ROCKCHIP_OUT_MODE_AAAA ? 0 : 1);
	VOP_CTRL_SET(vop, dither_down_sel, DITHER_DOWN_ALLEGRO);

	VOP_CTRL_SET(vop, dclk_ddr,
		     s->output_mode == ROCKCHIP_OUT_MODE_YUV420 ? 1 : 0);
	VOP_CTRL_SET(vop, hdmi_dclk_out_en,
		     s->output_mode == ROCKCHIP_OUT_MODE_YUV420 ? 1 : 0);

	VOP_CTRL_SET(vop, overlay_mode, s->yuv_overlay);
	VOP_CTRL_SET(vop, dsp_out_yuv, is_yuv_output(s->bus_format));

	/*
	 * Background color is 10bit depth if vop version >= 3.5
	 */
	if (!is_yuv_output(s->bus_format))
		val = 0;
	else if (VOP_MAJOR(vop->version) == 3 && VOP_MINOR(vop->version) == 8 &&
		 s->hdr.pre_overlay)
		val = 0;
	else if (VOP_MAJOR(vop->version) == 3 && VOP_MINOR(vop->version) >= 5)
		val = 0x20010200;
	else
		val = 0x801080;
	VOP_CTRL_SET(vop, dsp_background, val);
}

/*
 * if adjusted mode update, return true, else return false
 */
static bool vop_crtc_mode_update(struct drm_crtc *crtc)
{
	struct vop *vop = to_vop(crtc);
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

	if ((htotal_sync != VOP_CTRL_GET(vop, htotal_pw)) ||
	    (hactive_st_end != VOP_CTRL_GET(vop, hact_st_end)) ||
	    (vtotal_sync != VOP_CTRL_GET(vop, vtotal_pw)) ||
	    (vactive_st_end != VOP_CTRL_GET(vop, vact_st_end)) ||
	    (crtc_clock != clk_get_rate(vop->dclk)))
		return true;

	return false;
}

static void vop_crtc_enable(struct drm_crtc *crtc)
{
	struct vop *vop = to_vop(crtc);
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc->state);
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
	int sys_status = drm_crtc_index(crtc) ?
				SYS_STATUS_LCDC1 : SYS_STATUS_LCDC0;
	uint32_t val;
	int act_end;
	bool interlaced = !!(adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE);
	int for_ddr_freq = 0;
	bool dclk_inv;

	rockchip_set_system_status(sys_status);
	vop_lock(vop);
	DRM_DEV_INFO(vop->dev, "Update mode to %dx%d%s%d, type: %d\n",
		     hdisplay, vdisplay, interlaced ? "i" : "p",
		     adjusted_mode->vrefresh, s->output_type);
	vop_initial(crtc);
	vop_disable_allwin(vop);
	VOP_CTRL_SET(vop, standby, 0);
	vop->mode_update = vop_crtc_mode_update(crtc);
	if (vop->mode_update)
		vop_disable_all_planes(vop);
	/*
	 * restore the lut table.
	 */
	if (vop->lut_active)
		vop_crtc_load_lut(crtc);
	dclk_inv = (adjusted_mode->flags & DRM_MODE_FLAG_PPIXDATA) ? 0 : 1;

	VOP_CTRL_SET(vop, dclk_pol, dclk_inv);
	val = (adjusted_mode->flags & DRM_MODE_FLAG_NHSYNC) ?
		   0 : BIT(HSYNC_POSITIVE);
	val |= (adjusted_mode->flags & DRM_MODE_FLAG_NVSYNC) ?
		   0 : BIT(VSYNC_POSITIVE);
	VOP_CTRL_SET(vop, pin_pol, val);

	if (vop->dclk_source && vop->pll && vop->pll->pll) {
		if (clk_set_parent(vop->dclk_source, vop->pll->pll))
			DRM_DEV_ERROR(vop->dev,
				      "failed to set dclk's parents\n");
	}

	switch (s->output_type) {
	case DRM_MODE_CONNECTOR_LVDS:
		VOP_CTRL_SET(vop, rgb_en, 1);
		VOP_CTRL_SET(vop, rgb_pin_pol, val);
		VOP_CTRL_SET(vop, rgb_dclk_pol, dclk_inv);
		VOP_CTRL_SET(vop, lvds_en, 1);
		VOP_CTRL_SET(vop, lvds_pin_pol, val);
		VOP_CTRL_SET(vop, lvds_dclk_pol, dclk_inv);

		VOP_GRF_SET(vop, grf_dclk_inv, !dclk_inv);
		break;
	case DRM_MODE_CONNECTOR_eDP:
		VOP_CTRL_SET(vop, edp_en, 1);
		VOP_CTRL_SET(vop, edp_pin_pol, val);
		VOP_CTRL_SET(vop, edp_dclk_pol, dclk_inv);
		break;
	case DRM_MODE_CONNECTOR_HDMIA:
		VOP_CTRL_SET(vop, hdmi_en, 1);
		VOP_CTRL_SET(vop, hdmi_pin_pol, val);
		VOP_CTRL_SET(vop, hdmi_dclk_pol, 1);
		break;
	case DRM_MODE_CONNECTOR_DSI:
		VOP_CTRL_SET(vop, mipi_en, 1);
		VOP_CTRL_SET(vop, mipi_pin_pol, val);
		VOP_CTRL_SET(vop, mipi_dclk_pol, dclk_inv);
		VOP_CTRL_SET(vop, mipi_dual_channel_en,
			!!(s->output_flags & ROCKCHIP_OUTPUT_DSI_DUAL_CHANNEL));
		VOP_CTRL_SET(vop, data01_swap,
			!!(s->output_flags & ROCKCHIP_OUTPUT_DSI_DUAL_LINK));
		break;
	case DRM_MODE_CONNECTOR_DisplayPort:
		VOP_CTRL_SET(vop, dp_dclk_pol, 0);
		VOP_CTRL_SET(vop, dp_pin_pol, val);
		VOP_CTRL_SET(vop, dp_en, 1);
		break;
	case DRM_MODE_CONNECTOR_TV:
		if (vdisplay == CVBS_PAL_VDISPLAY)
			VOP_CTRL_SET(vop, tve_sw_mode, 1);
		else
			VOP_CTRL_SET(vop, tve_sw_mode, 0);

		VOP_CTRL_SET(vop, tve_dclk_pol, 1);
		VOP_CTRL_SET(vop, tve_dclk_en, 1);
		/* use the same pol reg with hdmi */
		VOP_CTRL_SET(vop, hdmi_pin_pol, val);
		VOP_CTRL_SET(vop, sw_genlock, 1);
		VOP_CTRL_SET(vop, sw_uv_offset_en, 1);
		VOP_CTRL_SET(vop, dither_up_en, 1);
		break;
	default:
		DRM_ERROR("unsupport connector_type[%d]\n", s->output_type);
	}

	vop_update_csc(crtc);

	VOP_CTRL_SET(vop, htotal_pw, (htotal << 16) | hsync_len);
	val = hact_st << 16;
	val |= hact_end;
	VOP_CTRL_SET(vop, hact_st_end, val);
	VOP_CTRL_SET(vop, hpost_st_end, val);

	val = vact_st << 16;
	val |= vact_end;
	VOP_CTRL_SET(vop, vact_st_end, val);
	VOP_CTRL_SET(vop, vpost_st_end, val);

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		u16 vact_st_f1 = vtotal + vact_st + 1;
		u16 vact_end_f1 = vact_st_f1 + vdisplay;

		val = vact_st_f1 << 16 | vact_end_f1;
		VOP_CTRL_SET(vop, vact_st_end_f1, val);
		VOP_CTRL_SET(vop, vpost_st_end_f1, val);

		val = vtotal << 16 | (vtotal + vsync_len);
		VOP_CTRL_SET(vop, vs_st_end_f1, val);
		VOP_CTRL_SET(vop, dsp_interlace, 1);
		VOP_CTRL_SET(vop, p2i_en, 1);
		vtotal += vtotal + 1;
		act_end = vact_end_f1;
	} else {
		VOP_CTRL_SET(vop, dsp_interlace, 0);
		VOP_CTRL_SET(vop, p2i_en, 0);
		act_end = vact_end;
	}

	if (VOP_MAJOR(vop->version) == 3 &&
	    (VOP_MINOR(vop->version) == 2 || VOP_MINOR(vop->version) == 8))
		for_ddr_freq = 1000;
	VOP_INTR_SET(vop, line_flag_num[0], act_end);
	VOP_INTR_SET(vop, line_flag_num[1],
		     act_end - us_to_vertical_line(adjusted_mode, for_ddr_freq));

	VOP_CTRL_SET(vop, vtotal_pw, vtotal << 16 | vsync_len);

	VOP_CTRL_SET(vop, core_dclk_div,
		     !!(adjusted_mode->flags & DRM_MODE_FLAG_DBLCLK));

	VOP_CTRL_SET(vop, cabc_total_num, hdisplay * vdisplay);
	VOP_CTRL_SET(vop, cabc_config_mode, STAGE_BY_STAGE);
	VOP_CTRL_SET(vop, cabc_stage_up_mode, MUL_MODE);
	VOP_CTRL_SET(vop, cabc_scale_cfg_value, 1);
	VOP_CTRL_SET(vop, cabc_scale_cfg_enable, 0);
	VOP_CTRL_SET(vop, cabc_global_dn_limit_en, 1);
	VOP_CTRL_SET(vop, win_csc_mode_sel, 1);

	clk_set_rate(vop->dclk, adjusted_mode->crtc_clock * 1000);

	vop_cfg_done(vop);

	enable_irq(vop->irq);
	drm_crtc_vblank_on(crtc);
	vop_unlock(vop);
}

static int vop_zpos_cmp(const void *a, const void *b)
{
	struct vop_zpos *pa = (struct vop_zpos *)a;
	struct vop_zpos *pb = (struct vop_zpos *)b;

	return pa->zpos - pb->zpos;
}

static int vop_afbdc_atomic_check(struct drm_crtc *crtc,
				  struct drm_crtc_state *crtc_state)
{
	struct vop *vop = to_vop(crtc);
	const struct vop_data *vop_data = vop->data;
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct drm_atomic_state *state = crtc_state->state;
	struct drm_plane *plane;
	struct drm_plane_state *pstate;
	struct vop_plane_state *plane_state;
	struct drm_framebuffer *fb;
	struct drm_rect *src;
	struct vop_win *win;
	int afbdc_format;

	s->afbdc_en = 0;

	drm_atomic_crtc_state_for_each_plane(plane, crtc_state) {
		pstate = drm_atomic_get_existing_plane_state(state, plane);
		/*
		 * plane might not have changed, in which case take
		 * current state:
		 */
		if (!pstate)
			pstate = plane->state;

		fb = pstate->fb;
		if (pstate->crtc != crtc || !fb)
			continue;
		if (fb->modifier[0] != DRM_FORMAT_MOD_ARM_AFBC)
			continue;

		if (!(vop_data->feature & VOP_FEATURE_AFBDC)) {
			DRM_ERROR("not support afbdc\n");
			return -EINVAL;
		}

		plane_state = to_vop_plane_state(pstate);

		switch (plane_state->format) {
		case VOP_FMT_ARGB8888:
			afbdc_format = AFBDC_FMT_U8U8U8U8;
			break;
		case VOP_FMT_RGB888:
			afbdc_format = AFBDC_FMT_U8U8U8;
			break;
		case VOP_FMT_RGB565:
			afbdc_format = AFBDC_FMT_RGB565;
			break;
		default:
			return -EINVAL;
		}

		if (s->afbdc_en) {
			DRM_ERROR("vop only support one afbc layer\n");
			return -EINVAL;
		}

		win = to_vop_win(plane);
		src = &plane_state->src;
		if (!(win->feature & WIN_FEATURE_AFBDC)) {
			DRM_ERROR("win[%d] feature:0x%llx, not support afbdc\n",
				  win->win_id, win->feature);
			return -EINVAL;
		}
		if (!IS_ALIGNED(fb->width, 16)) {
			DRM_ERROR("win[%d] afbdc must 16 align, width: %d\n",
				  win->win_id, fb->width);
			return -EINVAL;
		}

		if (VOP_CTRL_SUPPORT(vop, afbdc_pic_vir_width)) {
			u32 align_x1, align_x2, align_y1, align_y2, align_val;

			s->afbdc_win_format = afbdc_format;
			s->afbdc_win_id = win->win_id;
			s->afbdc_win_ptr = rockchip_fb_get_dma_addr(fb, 0);
			s->afbdc_win_vir_width = fb->width;
			s->afbdc_win_xoffset = (src->x1 >> 16);
			s->afbdc_win_yoffset = (src->y1 >> 16);

			align_x1 = (src->x1 >> 16) - ((src->x1 >> 16) % 16);
			align_y1 = (src->y1 >> 16) - ((src->y1 >> 16) % 16);

			align_val = (src->x2 >> 16) % 16;
			if (align_val)
				align_x2 = (src->x2 >> 16) + (16 - align_val);
			else
				align_x2 = src->x2 >> 16;

			align_val = (src->y2 >> 16) % 16;
			if (align_val)
				align_y2 = (src->y2 >> 16) + (16 - align_val);
			else
				align_y2 = src->y2 >> 16;

			s->afbdc_win_width = align_x2 - align_x1 - 1;
			s->afbdc_win_height = align_y2 - align_y1 - 1;

			s->afbdc_en = 1;

			break;
		}
		if (src->x1 || src->y1 || fb->offsets[0]) {
			DRM_ERROR("win[%d] afbdc not support offset display\n",
				  win->win_id);
			DRM_ERROR("xpos=%d, ypos=%d, offset=%d\n",
				  src->x1, src->y1, fb->offsets[0]);
			return -EINVAL;
		}
		s->afbdc_win_format = afbdc_format;
		s->afbdc_win_width = fb->width - 1;
		s->afbdc_win_height = (drm_rect_height(src) >> 16) - 1;
		s->afbdc_win_id = win->win_id;
		s->afbdc_win_ptr = plane_state->yrgb_mst;
		s->afbdc_en = 1;
	}

	return 0;
}

static void vop_dclk_source_generate(struct drm_crtc *crtc,
				     struct drm_crtc_state *crtc_state)
{
	struct rockchip_drm_private *private = crtc->dev->dev_private;
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct rockchip_crtc_state *old_s = to_rockchip_crtc_state(crtc->state);
	struct vop *vop = to_vop(crtc);
	struct rockchip_dclk_pll *old_pll = vop->pll;

	if (!vop->dclk_source)
		return;

	if (crtc_state->active) {
		WARN_ON(vop->pll && !vop->pll->use_count);
		if (!vop->pll || vop->pll->use_count > 1 ||
		    s->output_type != old_s->output_type) {
			if (vop->pll)
				vop->pll->use_count--;

			if (s->output_type != DRM_MODE_CONNECTOR_HDMIA &&
			    !private->default_pll.use_count)
				vop->pll = &private->default_pll;
			else
				vop->pll = &private->hdmi_pll;

			vop->pll->use_count++;
		}
	} else if (vop->pll) {
		vop->pll->use_count--;
		vop->pll = NULL;
	}
	if (vop->pll != old_pll)
		crtc_state->mode_changed = true;
}

static int vop_crtc_atomic_check(struct drm_crtc *crtc,
				 struct drm_crtc_state *crtc_state)
{
	struct drm_atomic_state *state = crtc_state->state;
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct vop *vop = to_vop(crtc);
	const struct vop_data *vop_data = vop->data;
	struct drm_plane *plane;
	struct drm_plane_state *pstate;
	struct vop_plane_state *plane_state;
	struct vop_zpos *pzpos;
	int dsp_layer_sel = 0;
	int i, j, cnt = 0, ret = 0;

#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
	struct vop_dump_list *pos, *n;

	if (!crtc->vop_dump_list_init_flag) {
		INIT_LIST_HEAD(&crtc->vop_dump_list_head);
		crtc->vop_dump_list_init_flag = true;
	}
	list_for_each_entry_safe(pos, n, &crtc->vop_dump_list_head, entry) {
		list_del(&pos->entry);
		kfree(pos);
	}
	if (crtc->vop_dump_status == DUMP_KEEP ||
	    crtc->vop_dump_times > 0) {
		crtc->frame_count++;
	}
#endif

	ret = vop_afbdc_atomic_check(crtc, crtc_state);
	if (ret)
		return ret;

	s->yuv_overlay = 0;
	if (VOP_CTRL_SUPPORT(vop, overlay_mode))
		s->yuv_overlay = is_yuv_output(s->bus_format);

	ret = vop_hdr_atomic_check(crtc, crtc_state);
	if (ret)
		return ret;
	ret = vop_csc_atomic_check(crtc, crtc_state);
	if (ret)
		return ret;

	pzpos = kmalloc_array(vop_data->win_size, sizeof(*pzpos), GFP_KERNEL);
	if (!pzpos)
		return -ENOMEM;

	for (i = 0; i < vop_data->win_size; i++) {
		const struct vop_win_data *win_data = &vop_data->win[i];
		struct vop_win *win;

		if (!win_data->phy)
			continue;

		for (j = 0; j < vop->num_wins; j++) {
			win = &vop->win[j];

			if (win->win_id == i && !win->area_id)
				break;
		}
		if (WARN_ON(j >= vop->num_wins)) {
			ret = -EINVAL;
			goto err_free_pzpos;
		}

		plane = &win->base;
		pstate = state->plane_states[drm_plane_index(plane)];
		/*
		 * plane might not have changed, in which case take
		 * current state:
		 */
		if (!pstate)
			pstate = plane->state;
		plane_state = to_vop_plane_state(pstate);

		if (!plane_state->enable)
			pzpos[cnt].zpos = INT_MAX;
		else
			pzpos[cnt].zpos = plane_state->zpos;
		pzpos[cnt++].win_id = win->win_id;
	}

	sort(pzpos, cnt, sizeof(pzpos[0]), vop_zpos_cmp, NULL);

	for (i = 0, cnt = 0; i < vop_data->win_size; i++) {
		const struct vop_win_data *win_data = &vop_data->win[i];
		int shift = i * 2;

		if (win_data->phy) {
			struct vop_zpos *zpos = &pzpos[cnt++];

			dsp_layer_sel |= zpos->win_id << shift;
		} else {
			dsp_layer_sel |= i << shift;
		}
	}

	s->dsp_layer_sel = dsp_layer_sel;

	vop_dclk_source_generate(crtc, crtc_state);

err_free_pzpos:
	kfree(pzpos);
	return ret;
}

static void vop_post_config(struct drm_crtc *crtc)
{
	struct vop *vop = to_vop(crtc);
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc->state);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	u16 vtotal = mode->crtc_vtotal;
	u16 hdisplay = mode->crtc_hdisplay;
	u16 hact_st = mode->crtc_htotal - mode->crtc_hsync_start;
	u16 vdisplay = mode->crtc_vdisplay;
	u16 vact_st = mode->crtc_vtotal - mode->crtc_vsync_start;
	u16 hsize = hdisplay * (s->left_margin + s->right_margin) / 200;
	u16 vsize = vdisplay * (s->top_margin + s->bottom_margin) / 200;
	u16 hact_end, vact_end;
	u32 val;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vsize = rounddown(vsize, 2);

	hact_st += hdisplay * (100 - s->left_margin) / 200;
	hact_end = hact_st + hsize;
	val = hact_st << 16;
	val |= hact_end;
	VOP_CTRL_SET(vop, hpost_st_end, val);
	vact_st += vdisplay * (100 - s->top_margin) / 200;
	vact_end = vact_st + vsize;
	val = vact_st << 16;
	val |= vact_end;
	VOP_CTRL_SET(vop, vpost_st_end, val);
	val = scl_cal_scale2(vdisplay, vsize) << 16;
	val |= scl_cal_scale2(hdisplay, hsize);
	VOP_CTRL_SET(vop, post_scl_factor, val);

#define POST_HORIZONTAL_SCALEDOWN_EN(x)		((x) << 0)
#define POST_VERTICAL_SCALEDOWN_EN(x)		((x) << 1)
	VOP_CTRL_SET(vop, post_scl_ctrl,
		     POST_HORIZONTAL_SCALEDOWN_EN(hdisplay != hsize) |
		     POST_VERTICAL_SCALEDOWN_EN(vdisplay != vsize));
	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		u16 vact_st_f1 = vtotal + vact_st + 1;
		u16 vact_end_f1 = vact_st_f1 + vsize;

		val = vact_st_f1 << 16 | vact_end_f1;
		VOP_CTRL_SET(vop, vpost_st_end_f1, val);
	}
}

static void vop_update_cabc_lut(struct drm_crtc *crtc,
			    struct drm_crtc_state *old_crtc_state)
{
	struct rockchip_crtc_state *s =
			to_rockchip_crtc_state(crtc->state);
	struct rockchip_crtc_state *old_s =
			to_rockchip_crtc_state(old_crtc_state);
	struct drm_property_blob *cabc_lut = s->cabc_lut;
	struct drm_property_blob *old_cabc_lut = old_s->cabc_lut;
	struct vop *vop = to_vop(crtc);
	int lut_size;
	u32 *lut;
	u32 lut_len = vop->cabc_lut_len;
	int i, dle;

	if (!cabc_lut && old_cabc_lut) {
		VOP_CTRL_SET(vop, cabc_lut_en, 0);
		return;
	}
	if (!cabc_lut)
		return;

	if (old_cabc_lut && old_cabc_lut->base.id == cabc_lut->base.id)
		return;

	lut = (u32 *)cabc_lut->data;
	lut_size = cabc_lut->length / sizeof(u32);
	if (WARN(lut_size != lut_len, "Unexpect cabc lut size not match\n"))
		return;

#define CTRL_GET(name) VOP_CTRL_GET(vop, name)
	if (CTRL_GET(cabc_lut_en)) {
		VOP_CTRL_SET(vop, cabc_lut_en, 0);
		vop_cfg_done(vop);
		readx_poll_timeout(CTRL_GET, cabc_lut_en, dle, !dle, 5, 33333);
	}

	for (i = 0; i < lut_len; i++)
		vop_write_cabc_lut(vop, (i << 2), lut[i]);
#undef CTRL_GET
	VOP_CTRL_SET(vop, cabc_lut_en, 1);
}

static void vop_update_hdr(struct drm_crtc *crtc,
			   struct drm_crtc_state *old_crtc_state)
{
	struct rockchip_crtc_state *s =
			to_rockchip_crtc_state(crtc->state);
	struct vop *vop = to_vop(crtc);
	struct rockchip_sdr2hdr_state *sdr2hdr_state = &s->hdr.sdr2hdr_state;

	if (!vop->data->hdr_table)
		return;

	if (s->hdr.hdr2sdr_en) {
		vop_load_hdr2sdr_table(vop);
		/* This is ic design bug, when in hdr2sdr mode, the overlay mode
		 * is rgb domain, so the win0 is do yuv2rgb, but in this case,
		 * we must close win0 y2r.
		 */
		VOP_CTRL_SET(vop, hdr2sdr_en_win0_csc, 0);
	}
	VOP_CTRL_SET(vop, hdr2sdr_en, s->hdr.hdr2sdr_en);

	VOP_CTRL_SET(vop, bt1886eotf_pre_conv_en,
		     sdr2hdr_state->bt1886eotf_pre_conv_en);
	VOP_CTRL_SET(vop, bt1886eotf_post_conv_en,
		     sdr2hdr_state->bt1886eotf_post_conv_en);

	VOP_CTRL_SET(vop, rgb2rgb_pre_conv_en,
		     sdr2hdr_state->rgb2rgb_pre_conv_en);
	VOP_CTRL_SET(vop, rgb2rgb_pre_conv_mode,
		     sdr2hdr_state->rgb2rgb_pre_conv_mode);
	VOP_CTRL_SET(vop, st2084oetf_pre_conv_en,
		     sdr2hdr_state->st2084oetf_pre_conv_en);

	VOP_CTRL_SET(vop, rgb2rgb_post_conv_en,
		     sdr2hdr_state->rgb2rgb_post_conv_en);
	VOP_CTRL_SET(vop, rgb2rgb_post_conv_mode,
		     sdr2hdr_state->rgb2rgb_post_conv_mode);
	VOP_CTRL_SET(vop, st2084oetf_post_conv_en,
		     sdr2hdr_state->st2084oetf_post_conv_en);

	if (sdr2hdr_state->bt1886eotf_pre_conv_en ||
	    sdr2hdr_state->bt1886eotf_post_conv_en)
		vop_load_sdr2hdr_table(vop, sdr2hdr_state->sdr2hdr_func);
	VOP_CTRL_SET(vop, win_csc_mode_sel, 1);
}

static void vop_update_cabc(struct drm_crtc *crtc,
			    struct drm_crtc_state *old_crtc_state)
{
	struct rockchip_crtc_state *s =
			to_rockchip_crtc_state(crtc->state);
	struct vop *vop = to_vop(crtc);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	int pixel_total = mode->hdisplay * mode->vdisplay;

	if (!vop->cabc_lut_regs)
		return;

	vop_update_cabc_lut(crtc, old_crtc_state);

	if (s->cabc_mode != ROCKCHIP_DRM_CABC_MODE_DISABLE) {
		VOP_CTRL_SET(vop, cabc_en, 1);
		VOP_CTRL_SET(vop, cabc_handle_en, 1);
		VOP_CTRL_SET(vop, cabc_stage_up, s->cabc_stage_up);
		VOP_CTRL_SET(vop, cabc_stage_down, s->cabc_stage_down);
		VOP_CTRL_SET(vop, cabc_global_dn, s->cabc_global_dn);
		VOP_CTRL_SET(vop, cabc_calc_pixel_num,
			     pixel_total / 1000 * s->cabc_calc_pixel_num);
	} else {
		/*
		 * There are some hardware issues on cabc disabling:
		 *   1: if cabc auto gating enable, cabc disabling will cause
		 *      vop die
		 *   2: cabc disabling always would make timing several
		 *      pixel cycle abnormal, cause some panel abnormal.
		 *
		 * So just keep cabc enable, and make it no work with max
		 * cabc_calc_pixel_num, it only has little power consume.
		 */
		VOP_CTRL_SET(vop, cabc_calc_pixel_num, pixel_total);
	}
}

static void vop_tv_config_update(struct drm_crtc *crtc,
				 struct drm_crtc_state *old_crtc_state)
{
	struct rockchip_crtc_state *s =
			to_rockchip_crtc_state(crtc->state);
	struct rockchip_crtc_state *old_s =
			to_rockchip_crtc_state(old_crtc_state);
	int brightness, contrast, saturation, hue, sin_hue, cos_hue;
	struct vop *vop = to_vop(crtc);
	const struct vop_data *vop_data = vop->data;

	if (!s->tv_state)
		return;

	if (!memcmp(s->tv_state,
		    &vop->active_tv_state, sizeof(*s->tv_state)) &&
	    s->yuv_overlay == old_s->yuv_overlay &&
	    s->bcsh_en == old_s->bcsh_en && s->bus_format == old_s->bus_format)
		return;

	memcpy(&vop->active_tv_state, s->tv_state, sizeof(*s->tv_state));
	/* post BCSH CSC */
	s->post_r2y_en = 0;
	s->post_y2r_en = 0;
	s->bcsh_en = 0;
	if (s->tv_state) {
		if (s->tv_state->brightness != 50 ||
		    s->tv_state->contrast != 50 ||
		    s->tv_state->saturation != 50 || s->tv_state->hue != 50)
			s->bcsh_en = 1;
	}

	if (s->bcsh_en) {
		if (!s->yuv_overlay)
			s->post_r2y_en = 1;
		if (!is_yuv_output(s->bus_format))
			s->post_y2r_en = 1;
	} else {
		if (!s->yuv_overlay && is_yuv_output(s->bus_format))
			s->post_r2y_en = 1;
		if (s->yuv_overlay && !is_yuv_output(s->bus_format))
			s->post_y2r_en = 1;
	}

	s->post_csc_mode = to_vop_csc_mode(s->color_space);
	VOP_CTRL_SET(vop, bcsh_r2y_en, s->post_r2y_en);
	VOP_CTRL_SET(vop, bcsh_y2r_en, s->post_y2r_en);
	VOP_CTRL_SET(vop, bcsh_r2y_csc_mode, s->post_csc_mode);
	VOP_CTRL_SET(vop, bcsh_y2r_csc_mode, s->post_csc_mode);
	if (!s->bcsh_en) {
		VOP_CTRL_SET(vop, bcsh_en, s->bcsh_en);
		return;
	}

	if (vop_data->feature & VOP_FEATURE_OUTPUT_10BIT)
		brightness = interpolate(0, -128, 100, 127,
					 s->tv_state->brightness);
	else
		brightness = interpolate(0, -32, 100, 31,
					 s->tv_state->brightness);
	contrast = interpolate(0, 0, 100, 511, s->tv_state->contrast);
	saturation = interpolate(0, 0, 100, 511, s->tv_state->saturation);
	hue = interpolate(0, -30, 100, 30, s->tv_state->hue);

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
	VOP_CTRL_SET(vop, bcsh_brightness, brightness);
	VOP_CTRL_SET(vop, bcsh_contrast, contrast);
	VOP_CTRL_SET(vop, bcsh_sat_con, saturation * contrast / 0x100);
	VOP_CTRL_SET(vop, bcsh_sin_hue, sin_hue);
	VOP_CTRL_SET(vop, bcsh_cos_hue, cos_hue);
	VOP_CTRL_SET(vop, bcsh_out_mode, BCSH_OUT_MODE_NORMAL_VIDEO);
	VOP_CTRL_SET(vop, bcsh_en, s->bcsh_en);
}

static void vop_cfg_update(struct drm_crtc *crtc,
			   struct drm_crtc_state *old_crtc_state)
{
	struct rockchip_crtc_state *s =
			to_rockchip_crtc_state(crtc->state);
	struct vop *vop = to_vop(crtc);

	spin_lock(&vop->reg_lock);

	vop_update_csc(crtc);

	vop_tv_config_update(crtc, old_crtc_state);

	if (s->afbdc_en) {
		u32 pic_size, pic_offset;

		VOP_CTRL_SET(vop, afbdc_format, s->afbdc_win_format | 1 << 4);
		VOP_CTRL_SET(vop, afbdc_hreg_block_split, 0);
		VOP_CTRL_SET(vop, afbdc_sel, s->afbdc_win_id);
		VOP_CTRL_SET(vop, afbdc_hdr_ptr, s->afbdc_win_ptr);
		pic_size = (s->afbdc_win_width & 0xffff);
		pic_size |= s->afbdc_win_height << 16;
		VOP_CTRL_SET(vop, afbdc_pic_size, pic_size);

		VOP_CTRL_SET(vop, afbdc_pic_vir_width, s->afbdc_win_vir_width);
		pic_offset = (s->afbdc_win_xoffset & 0xffff);
		pic_offset |= s->afbdc_win_yoffset << 16;
		VOP_CTRL_SET(vop, afbdc_pic_offset, pic_offset);
	}

	VOP_CTRL_SET(vop, afbdc_en, s->afbdc_en);
	VOP_CTRL_SET(vop, dsp_layer_sel, s->dsp_layer_sel);
	vop_post_config(crtc);

	spin_unlock(&vop->reg_lock);
}

static bool vop_fs_irq_is_pending(struct vop *vop)
{
	if (VOP_MAJOR(vop->version) == 3 && VOP_MINOR(vop->version) >= 7)
		return VOP_INTR_GET_TYPE(vop, status, FS_FIELD_INTR);
	else
		return VOP_INTR_GET_TYPE(vop, status, FS_INTR);
}

static void vop_wait_for_irq_handler(struct vop *vop)
{
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
	ret = readx_poll_timeout_atomic(vop_fs_irq_is_pending, vop, pending,
					!pending, 0, 10 * 1000);
	if (ret)
		DRM_DEV_ERROR(vop->dev, "VOP vblank IRQ stuck for 10 ms\n");

	synchronize_irq(vop->irq);
}

static void vop_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_crtc_state)
{
	struct drm_atomic_state *old_state = old_crtc_state->state;
	struct drm_plane_state *old_plane_state;
	struct vop *vop = to_vop(crtc);
	struct drm_plane *plane;
	int i;
	unsigned long flags;
	struct rockchip_crtc_state *s =
		to_rockchip_crtc_state(crtc->state);

	vop_cfg_update(crtc, old_crtc_state);

	if (!vop->is_iommu_enabled && vop->is_iommu_needed) {
		bool need_wait_vblank;
		int ret;

		if (vop->mode_update)
			vop_disable_all_planes(vop);

		need_wait_vblank = !vop_is_allwin_disabled(vop);
		if (vop->mode_update && need_wait_vblank)
			dev_warn(vop->dev, "mode_update:%d, need wait blk:%d\n",
				 vop->mode_update, need_wait_vblank);

		if (need_wait_vblank) {
			bool active;

			disable_irq(vop->irq);
			drm_crtc_vblank_get(crtc);
			VOP_INTR_SET_TYPE(vop, enable, LINE_FLAG_INTR, 1);

			ret = readx_poll_timeout_atomic(vop_fs_irq_is_active,
							vop, active, active,
							0, 50 * 1000);
			if (ret)
				dev_err(vop->dev, "wait fs irq timeout\n");

			VOP_INTR_SET_TYPE(vop, clear, LINE_FLAG_INTR, 1);
			vop_cfg_done(vop);

			ret = readx_poll_timeout_atomic(vop_line_flag_is_active,
							vop, active, active,
							0, 50 * 1000);
			if (ret)
				dev_err(vop->dev, "wait line flag timeout\n");

			enable_irq(vop->irq);
		}
		ret = rockchip_drm_dma_attach_device(vop->drm_dev, vop->dev);
		if (ret) {
			vop->is_iommu_enabled = false;
			vop_disable_all_planes(vop);
			dev_err(vop->dev, "failed to attach dma mapping, %d\n",
				ret);
		} else {
			vop->is_iommu_enabled = true;
			VOP_CTRL_SET(vop, dma_stop, 0);
		}

		if (need_wait_vblank) {
			VOP_INTR_SET_TYPE(vop, enable, LINE_FLAG_INTR, 0);
			drm_crtc_vblank_put(crtc);
		}
	}

	vop_update_cabc(crtc, old_crtc_state);
	vop_update_hdr(crtc, old_crtc_state);

	spin_lock_irqsave(&vop->irq_lock, flags);
	vop->pre_overlay = s->hdr.pre_overlay;
	vop_cfg_done(vop);
	/*
	 * rk322x and rk332x odd-even field will mistake when in interlace mode.
	 * we must switch to frame effect before switch screen and switch to
	 * field effect after switch screen complete.
	 */
	if (VOP_MAJOR(vop->version) == 3 &&
	    (VOP_MINOR(vop->version) == 7 || VOP_MINOR(vop->version) == 8)) {
		if (!vop->mode_update && VOP_CTRL_GET(vop, reg_done_frm))
			VOP_CTRL_SET(vop, reg_done_frm, 0);
	} else {
		VOP_CTRL_SET(vop, reg_done_frm, 0);
	}

	vop->mode_update = false;
	spin_unlock_irqrestore(&vop->irq_lock, flags);

	/*
	 * There is a (rather unlikely) possiblity that a vblank interrupt
	 * fired before we set the cfg_done bit. To avoid spuriously
	 * signalling flip completion we need to wait for it to finish.
	 */
	vop_wait_for_irq_handler(vop);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);
		WARN_ON(vop->event);

		vop->event = crtc->state->event;
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	for_each_plane_in_state(old_state, plane, old_plane_state, i) {
		if (!old_plane_state->fb)
			continue;

		if (old_plane_state->fb == plane->state->fb)
			continue;

		drm_framebuffer_reference(old_plane_state->fb);
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);
		drm_flip_work_queue(&vop->fb_unref_work, old_plane_state->fb);
		set_bit(VOP_PENDING_FB_UNREF, &vop->pending);
	}
}

static void vop_crtc_atomic_begin(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_crtc_state)
{
}

static const struct drm_crtc_helper_funcs vop_crtc_helper_funcs = {
	.load_lut = vop_crtc_load_lut,
	.enable = vop_crtc_enable,
	.disable = vop_crtc_disable,
	.mode_fixup = vop_crtc_mode_fixup,
	.atomic_check = vop_crtc_atomic_check,
	.atomic_flush = vop_crtc_atomic_flush,
	.atomic_begin = vop_crtc_atomic_begin,
};

static void vop_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
}

static void vop_crtc_reset(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc->state);

	if (crtc->state) {
		__drm_atomic_helper_crtc_destroy_state(crtc, crtc->state);
		kfree(s);
	}

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return;
	crtc->state = &s->base;
	crtc->state->crtc = crtc;

	s->left_margin = 100;
	s->right_margin = 100;
	s->top_margin = 100;
	s->bottom_margin = 100;
}

static struct drm_crtc_state *vop_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *rockchip_state, *old_state;

	old_state = to_rockchip_crtc_state(crtc->state);
	rockchip_state = kmemdup(old_state, sizeof(*old_state), GFP_KERNEL);
	if (!rockchip_state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &rockchip_state->base);
	return &rockchip_state->base;
}

static void vop_crtc_destroy_state(struct drm_crtc *crtc,
				   struct drm_crtc_state *state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(state);

	__drm_atomic_helper_crtc_destroy_state(crtc, &s->base);
	kfree(s);
}

static int vop_crtc_atomic_get_property(struct drm_crtc *crtc,
					const struct drm_crtc_state *state,
					struct drm_property *property,
					uint64_t *val)
{
	struct drm_device *drm_dev = crtc->dev;
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(state);
	struct vop *vop = to_vop(crtc);

	if (property == mode_config->tv_left_margin_property) {
		*val = s->left_margin;
		return 0;
	}

	if (property == mode_config->tv_right_margin_property) {
		*val = s->right_margin;
		return 0;
	}

	if (property == mode_config->tv_top_margin_property) {
		*val = s->top_margin;
		return 0;
	}

	if (property == mode_config->tv_bottom_margin_property) {
		*val = s->bottom_margin;
		return 0;
	}

	if (property == private->cabc_mode_property) {
		*val = s->cabc_mode;
		return 0;
	}

	if (property == private->cabc_stage_up_property) {
		*val = s->cabc_stage_up;
		return 0;
	}

	if (property == private->cabc_stage_down_property) {
		*val = s->cabc_stage_down;
		return 0;
	}

	if (property == private->cabc_global_dn_property) {
		*val = s->cabc_global_dn;
		return 0;
	}

	if (property == private->cabc_calc_pixel_num_property) {
		*val = s->cabc_calc_pixel_num;
		return 0;
	}

	if (property == private->cabc_lut_property) {
		*val = s->cabc_lut ? s->cabc_lut->base.id : 0;
		return 0;
	}

	if (property == private->alpha_scale_prop) {
		*val = (vop->data->feature & VOP_FEATURE_ALPHA_SCALE) ? 1 : 0;
		return 0;
	}

	DRM_ERROR("failed to get vop crtc property\n");
	return -EINVAL;
}

static int vop_crtc_atomic_set_property(struct drm_crtc *crtc,
					struct drm_crtc_state *state,
					struct drm_property *property,
					uint64_t val)
{
	struct drm_device *drm_dev = crtc->dev;
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(state);
	struct vop *vop = to_vop(crtc);

	if (property == mode_config->tv_left_margin_property) {
		s->left_margin = val;
		return 0;
	}

	if (property == mode_config->tv_right_margin_property) {
		s->right_margin = val;
		return 0;
	}

	if (property == mode_config->tv_top_margin_property) {
		s->top_margin = val;
		return 0;
	}

	if (property == mode_config->tv_bottom_margin_property) {
		s->bottom_margin = val;
		return 0;
	}

	if (property == private->cabc_mode_property) {
		s->cabc_mode = val;
		/*
		 * Pre-define lowpower and normal mode to make cabc
		 * easier to use.
		 */
		if (s->cabc_mode == ROCKCHIP_DRM_CABC_MODE_NORMAL) {
			s->cabc_stage_up = 257;
			s->cabc_stage_down = 255;
			s->cabc_global_dn = 192;
			s->cabc_calc_pixel_num = 995;
		} else if (s->cabc_mode == ROCKCHIP_DRM_CABC_MODE_LOWPOWER) {
			s->cabc_stage_up = 260;
			s->cabc_stage_down = 252;
			s->cabc_global_dn = 180;
			s->cabc_calc_pixel_num = 992;
		}
		return 0;
	}

	if (property == private->cabc_stage_up_property) {
		s->cabc_stage_up = val;
		return 0;
	}

	if (property == private->cabc_stage_down_property) {
		s->cabc_stage_down = val;
		return 0;
	}

	if (property == private->cabc_calc_pixel_num_property) {
		s->cabc_calc_pixel_num = val;
		return 0;
	}

	if (property == private->cabc_global_dn_property) {
		s->cabc_global_dn = val;
		return 0;
	}

	if (property == private->cabc_lut_property) {
		bool replaced;
		ssize_t size = vop->cabc_lut_len * 4;

		return drm_atomic_replace_property_blob_from_id(crtc->dev,
								&s->cabc_lut,
								val,
								size,
								&replaced);
	}

	DRM_ERROR("failed to set vop crtc property\n");
	return -EINVAL;
}

static void vop_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
			       u16 *blue, uint32_t start, uint32_t size)
{
	struct vop *vop = to_vop(crtc);
	int end = min_t(u32, start + size, vop->lut_len);
	int i;

	if (!vop->lut)
		return;

	for (i = start; i < end; i++)
		rockchip_vop_crtc_fb_gamma_set(crtc, red[i], green[i],
					       blue[i], i);

	vop_crtc_load_lut(crtc);
}

static const struct drm_crtc_funcs vop_crtc_funcs = {
	.gamma_set = vop_crtc_gamma_set,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.destroy = vop_crtc_destroy,
	.reset = vop_crtc_reset,
	.set_property = drm_atomic_helper_crtc_set_property,
	.atomic_get_property = vop_crtc_atomic_get_property,
	.atomic_set_property = vop_crtc_atomic_set_property,
	.atomic_duplicate_state = vop_crtc_duplicate_state,
	.atomic_destroy_state = vop_crtc_destroy_state,
};

static void vop_fb_unref_worker(struct drm_flip_work *work, void *val)
{
	struct vop *vop = container_of(work, struct vop, fb_unref_work);
	struct drm_framebuffer *fb = val;

	drm_crtc_vblank_put(&vop->crtc);
	drm_framebuffer_unreference(fb);
}

static void vop_handle_vblank(struct vop *vop)
{
	struct drm_device *drm = vop->drm_dev;
	struct drm_crtc *crtc = &vop->crtc;
	unsigned long flags;

	spin_lock_irqsave(&drm->event_lock, flags);
	if (vop->event) {
		drm_crtc_send_vblank_event(crtc, vop->event);
		drm_crtc_vblank_put(crtc);
		vop->event = NULL;
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);

	if (test_and_clear_bit(VOP_PENDING_FB_UNREF, &vop->pending))
		drm_flip_work_commit(&vop->fb_unref_work, system_unbound_wq);
}

static irqreturn_t vop_isr(int irq, void *data)
{
	struct vop *vop = data;
	struct drm_crtc *crtc = &vop->crtc;
	uint32_t active_irqs;
	unsigned long flags;
	int ret = IRQ_NONE;

	/*
	 * interrupt register has interrupt status, enable and clear bits, we
	 * must hold irq_lock to avoid a race with enable/disable_vblank().
	*/
	spin_lock_irqsave(&vop->irq_lock, flags);

	active_irqs = VOP_INTR_GET_TYPE(vop, status, INTR_MASK);
	/* Clear all active interrupt sources */
	if (active_irqs)
		VOP_INTR_SET_TYPE(vop, clear, active_irqs, 1);

	spin_unlock_irqrestore(&vop->irq_lock, flags);

	/* This is expected for vop iommu irqs, since the irq is shared */
	if (!active_irqs)
		return IRQ_NONE;

	if (active_irqs & DSP_HOLD_VALID_INTR) {
		complete(&vop->dsp_hold_completion);
		active_irqs &= ~DSP_HOLD_VALID_INTR;
		ret = IRQ_HANDLED;
	}

	if (active_irqs & LINE_FLAG_INTR) {
		complete(&vop->line_flag_completion);
		active_irqs &= ~LINE_FLAG_INTR;
		ret = IRQ_HANDLED;
	}

	if ((active_irqs & FS_INTR) || (active_irqs & FS_FIELD_INTR)) {
		/* This is IC design not reasonable, this two register bit need
		 * frame effective, but actually it's effective immediately, so
		 * we config this register at frame start.
		 */
		spin_lock_irqsave(&vop->irq_lock, flags);
		VOP_CTRL_SET(vop, level2_overlay_en, vop->pre_overlay);
		VOP_CTRL_SET(vop, alpha_hard_calc, vop->pre_overlay);
		spin_unlock_irqrestore(&vop->irq_lock, flags);
		drm_crtc_handle_vblank(crtc);
		vop_handle_vblank(vop);
		active_irqs &= ~(FS_INTR | FS_FIELD_INTR);
		ret = IRQ_HANDLED;
	}

#define ERROR_HANDLER(x) \
	do { \
		if (active_irqs & x##_INTR) {\
			DRM_DEV_ERROR_RATELIMITED(vop->dev, #x " irq err\n"); \
			active_irqs &= ~x##_INTR; \
			ret = IRQ_HANDLED; \
		} \
	} while (0)

	ERROR_HANDLER(BUS_ERROR);
	ERROR_HANDLER(WIN0_EMPTY);
	ERROR_HANDLER(WIN1_EMPTY);
	ERROR_HANDLER(WIN2_EMPTY);
	ERROR_HANDLER(WIN3_EMPTY);
	ERROR_HANDLER(HWC_EMPTY);
	ERROR_HANDLER(POST_BUF_EMPTY);

	/* Unhandled irqs are spurious. */
	if (active_irqs)
		DRM_ERROR("Unknown VOP IRQs: %#02x\n", active_irqs);

	return ret;
}

static int vop_plane_init(struct vop *vop, struct vop_win *win,
			  unsigned long possible_crtcs)
{
	struct rockchip_drm_private *private = vop->drm_dev->dev_private;
	struct drm_plane *share = NULL;
	unsigned int rotations = 0;
	struct drm_property *prop;
	uint64_t feature = 0;
	int ret;

	if (win->parent)
		share = &win->parent->base;

	ret = drm_share_plane_init(vop->drm_dev, &win->base, share,
				   possible_crtcs, &vop_plane_funcs,
				   win->data_formats, win->nformats, win->type);
	if (ret) {
		DRM_ERROR("failed to initialize plane\n");
		return ret;
	}
	drm_plane_helper_add(&win->base, &plane_helper_funcs);
	drm_object_attach_property(&win->base.base,
				   vop->plane_zpos_prop, win->win_id);

	if (VOP_WIN_SUPPORT(vop, win, xmirror))
		rotations |= BIT(DRM_REFLECT_X);

	if (VOP_WIN_SUPPORT(vop, win, ymirror)) {
		rotations |= BIT(DRM_REFLECT_Y);

		prop = drm_property_create_bool(vop->drm_dev,
						DRM_MODE_PROP_ATOMIC,
						"LOGO_YMIRROR");
		if (!prop)
			return -ENOMEM;
		private->logo_ymirror_prop = prop;
	}

	if (rotations) {
		rotations |= BIT(DRM_ROTATE_0);
		prop = drm_mode_create_rotation_property(vop->drm_dev,
							 rotations);
		if (!prop) {
			DRM_ERROR("failed to create zpos property\n");
			return -EINVAL;
		}
		drm_object_attach_property(&win->base.base, prop,
					   BIT(DRM_ROTATE_0));
		win->rotation_prop = prop;
	}
	if (win->phy->scl)
		feature |= BIT(ROCKCHIP_DRM_PLANE_FEATURE_SCALE);
	if (VOP_WIN_SUPPORT(vop, win, src_alpha_ctl) ||
	    VOP_WIN_SUPPORT(vop, win, alpha_en))
		feature |= BIT(ROCKCHIP_DRM_PLANE_FEATURE_ALPHA);
	if (win->feature & WIN_FEATURE_HDR2SDR)
		feature |= BIT(ROCKCHIP_DRM_PLANE_FEATURE_HDR2SDR);
	if (win->feature & WIN_FEATURE_SDR2HDR)
		feature |= BIT(ROCKCHIP_DRM_PLANE_FEATURE_SDR2HDR);
	if (win->feature & WIN_FEATURE_AFBDC)
		feature |= BIT(ROCKCHIP_DRM_PLANE_FEATURE_AFBDC);

	drm_object_attach_property(&win->base.base, vop->plane_feature_prop,
				   feature);
	drm_object_attach_property(&win->base.base, private->eotf_prop, 0);
	drm_object_attach_property(&win->base.base,
				   private->color_space_prop, 0);
	if (VOP_WIN_SUPPORT(vop, win, global_alpha_val))
		drm_object_attach_property(&win->base.base,
					   private->global_alpha_prop, 0xff);
	drm_object_attach_property(&win->base.base,
				   private->blend_mode_prop, 0);

	return 0;
}

static int vop_of_init_display_lut(struct vop *vop)
{
	struct device_node *node = vop->dev->of_node;
	struct device_node *dsp_lut;
	u32 lut_len = vop->lut_len;
	struct property *prop;
	int length, i, j;
	int ret;

	if (!vop->lut)
		return -ENOMEM;

	dsp_lut = of_parse_phandle(node, "dsp-lut", 0);
	if (!dsp_lut)
		return -ENXIO;

	prop = of_find_property(dsp_lut, "gamma-lut", &length);
	if (!prop) {
		dev_err(vop->dev, "failed to find gamma_lut\n");
		return -ENXIO;
	}

	length >>= 2;

	if (length != lut_len) {
		u32 r, g, b;
		u32 *lut = kmalloc_array(length, sizeof(*lut), GFP_KERNEL);

		if (!lut)
			return -ENOMEM;
		ret = of_property_read_u32_array(dsp_lut, "gamma-lut", lut,
						 length);
		if (ret) {
			dev_err(vop->dev, "load gamma-lut failed\n");
			kfree(lut);
			return -EINVAL;
		}

		for (i = 0; i < lut_len; i++) {
			j = i * length / lut_len;
			r = lut[j] / length / length * lut_len / length;
			g = lut[j] / length % length * lut_len / length;
			b = lut[j] % length * lut_len / length;

			vop->lut[i] = r * lut_len * lut_len + g * lut_len + b;
		}

		kfree(lut);
	} else {
		of_property_read_u32_array(dsp_lut, "gamma-lut",
					   vop->lut, vop->lut_len);
	}
	vop->lut_active = true;

	return 0;
}

static int vop_create_crtc(struct vop *vop)
{
	struct device *dev = vop->dev;
	const struct vop_data *vop_data = vop->data;
	struct drm_device *drm_dev = vop->drm_dev;
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct drm_plane *primary = NULL, *cursor = NULL, *plane, *tmp;
	struct drm_crtc *crtc = &vop->crtc;
	struct device_node *port;
	uint64_t feature = 0;
	int ret;
	int i;

	/*
	 * Create drm_plane for primary and cursor planes first, since we need
	 * to pass them to drm_crtc_init_with_planes, which sets the
	 * "possible_crtcs" to the newly initialized crtc.
	 */
	for (i = 0; i < vop->num_wins; i++) {
		struct vop_win *win = &vop->win[i];

		if (win->type != DRM_PLANE_TYPE_PRIMARY &&
		    win->type != DRM_PLANE_TYPE_CURSOR)
			continue;

		ret = vop_plane_init(vop, win, 0);
		if (ret)
			goto err_cleanup_planes;

		plane = &win->base;
		if (plane->type == DRM_PLANE_TYPE_PRIMARY)
			primary = plane;
		else if (plane->type == DRM_PLANE_TYPE_CURSOR)
			cursor = plane;

	}

	ret = drm_crtc_init_with_planes(drm_dev, crtc, primary, cursor,
					&vop_crtc_funcs, NULL);
	if (ret)
		goto err_cleanup_planes;

	drm_crtc_helper_add(crtc, &vop_crtc_helper_funcs);

	/*
	 * Create drm_planes for overlay windows with possible_crtcs restricted
	 * to the newly created crtc.
	 */
	for (i = 0; i < vop->num_wins; i++) {
		struct vop_win *win = &vop->win[i];
		unsigned long possible_crtcs = 1 << drm_crtc_index(crtc);

		if (win->type != DRM_PLANE_TYPE_OVERLAY)
			continue;

		ret = vop_plane_init(vop, win, possible_crtcs);
		if (ret)
			goto err_cleanup_crtc;
	}

	port = of_get_child_by_name(dev->of_node, "port");
	if (!port) {
		DRM_ERROR("no port node found in %s\n",
			  dev->of_node->full_name);
		ret = -ENOENT;
		goto err_cleanup_crtc;
	}

	drm_flip_work_init(&vop->fb_unref_work, "fb_unref",
			   vop_fb_unref_worker);

	init_completion(&vop->dsp_hold_completion);
	init_completion(&vop->line_flag_completion);
	crtc->port = port;
	rockchip_register_crtc_funcs(crtc, &private_crtc_funcs);

#define VOP_ATTACH_MODE_CONFIG_PROP(prop, v) \
	drm_object_attach_property(&crtc->base, drm_dev->mode_config.prop, v)

	VOP_ATTACH_MODE_CONFIG_PROP(tv_left_margin_property, 100);
	VOP_ATTACH_MODE_CONFIG_PROP(tv_right_margin_property, 100);
	VOP_ATTACH_MODE_CONFIG_PROP(tv_top_margin_property, 100);
	VOP_ATTACH_MODE_CONFIG_PROP(tv_bottom_margin_property, 100);

#undef VOP_ATTACH_MODE_CONFIG_PROP

	drm_object_attach_property(&crtc->base, private->cabc_lut_property, 0);
	drm_object_attach_property(&crtc->base, private->cabc_mode_property, 0);
	drm_object_attach_property(&crtc->base, private->cabc_stage_up_property, 0);
	drm_object_attach_property(&crtc->base, private->cabc_stage_down_property, 0);
	drm_object_attach_property(&crtc->base, private->cabc_global_dn_property, 0);
	drm_object_attach_property(&crtc->base, private->cabc_calc_pixel_num_property, 0);
	drm_object_attach_property(&crtc->base, private->cabc_mode_property, 0);
	drm_object_attach_property(&crtc->base, private->alpha_scale_prop, 0);

	if (vop_data->feature & VOP_FEATURE_AFBDC)
		feature |= BIT(ROCKCHIP_DRM_CRTC_FEATURE_AFBDC);
	drm_object_attach_property(&crtc->base, vop->feature_prop,
				   feature);
	if (vop->lut_regs) {
		u16 *r_base, *g_base, *b_base;
		u32 lut_len = vop->lut_len;

		vop->lut = devm_kmalloc_array(dev, lut_len, sizeof(*vop->lut),
					      GFP_KERNEL);
		if (!vop->lut)
			goto err_unregister_crtc_funcs;

		if (vop_of_init_display_lut(vop)) {
			for (i = 0; i < lut_len; i++) {
				u32 r = i * lut_len * lut_len;
				u32 g = i * lut_len;
				u32 b = i;

				vop->lut[i] = r | g | b;
			}
		}

		drm_mode_crtc_set_gamma_size(crtc, lut_len);
		r_base = crtc->gamma_store;
		g_base = r_base + crtc->gamma_size;
		b_base = g_base + crtc->gamma_size;

		for (i = 0; i < lut_len; i++) {
			rockchip_vop_crtc_fb_gamma_get(crtc, &r_base[i],
						       &g_base[i], &b_base[i],
						       i);
		}
	}

	return 0;

err_unregister_crtc_funcs:
	rockchip_unregister_crtc_funcs(crtc);
err_cleanup_crtc:
	drm_crtc_cleanup(crtc);
err_cleanup_planes:
	list_for_each_entry_safe(plane, tmp, &drm_dev->mode_config.plane_list,
				 head)
		drm_plane_cleanup(plane);
	return ret;
}

static void vop_destroy_crtc(struct vop *vop)
{
	struct drm_crtc *crtc = &vop->crtc;
	struct drm_device *drm_dev = vop->drm_dev;
	struct drm_plane *plane, *tmp;

	rockchip_unregister_crtc_funcs(crtc);
	of_node_put(crtc->port);

	/*
	 * We need to cleanup the planes now.  Why?
	 *
	 * The planes are "&vop->win[i].base".  That means the memory is
	 * all part of the big "struct vop" chunk of memory.  That memory
	 * was devm allocated and associated with this component.  We need to
	 * free it ourselves before vop_unbind() finishes.
	 */
	list_for_each_entry_safe(plane, tmp, &drm_dev->mode_config.plane_list,
				 head)
		vop_plane_destroy(plane);

	/*
	 * Destroy CRTC after vop_plane_destroy() since vop_disable_plane()
	 * references the CRTC.
	 */
	drm_crtc_cleanup(crtc);
	drm_flip_work_cleanup(&vop->fb_unref_work);
}

/*
 * Initialize the vop->win array elements.
 */
static int vop_win_init(struct vop *vop)
{
	const struct vop_data *vop_data = vop->data;
	unsigned int i, j;
	unsigned int num_wins = 0;
	struct drm_property *prop;
	static const struct drm_prop_enum_list props[] = {
		{ ROCKCHIP_DRM_PLANE_FEATURE_SCALE, "scale" },
		{ ROCKCHIP_DRM_PLANE_FEATURE_ALPHA, "alpha" },
		{ ROCKCHIP_DRM_PLANE_FEATURE_HDR2SDR, "hdr2sdr" },
		{ ROCKCHIP_DRM_PLANE_FEATURE_SDR2HDR, "sdr2hdr" },
		{ ROCKCHIP_DRM_PLANE_FEATURE_AFBDC, "afbdc" },
	};
	static const struct drm_prop_enum_list crtc_props[] = {
		{ ROCKCHIP_DRM_CRTC_FEATURE_AFBDC, "afbdc" },
	};

	for (i = 0; i < vop_data->win_size; i++) {
		struct vop_win *vop_win = &vop->win[num_wins];
		const struct vop_win_data *win_data = &vop_data->win[i];

		if (!win_data->phy)
			continue;

		vop_win->phy = win_data->phy;
		vop_win->csc = win_data->csc;
		vop_win->offset = win_data->base;
		vop_win->type = win_data->type;
		vop_win->data_formats = win_data->phy->data_formats;
		vop_win->nformats = win_data->phy->nformats;
		vop_win->feature = win_data->feature;
		vop_win->vop = vop;
		vop_win->win_id = i;
		vop_win->area_id = 0;
		num_wins++;

		for (j = 0; j < win_data->area_size; j++) {
			struct vop_win *vop_area = &vop->win[num_wins];
			const struct vop_win_phy *area = win_data->area[j];

			vop_area->parent = vop_win;
			vop_area->offset = vop_win->offset;
			vop_area->phy = area;
			vop_area->type = DRM_PLANE_TYPE_OVERLAY;
			vop_area->data_formats = vop_win->data_formats;
			vop_area->nformats = vop_win->nformats;
			vop_area->vop = vop;
			vop_area->win_id = i;
			vop_area->area_id = j + 1;
			num_wins++;
		}
	}

	vop->num_wins = num_wins;

	prop = drm_property_create_range(vop->drm_dev, DRM_MODE_PROP_ATOMIC,
					 "ZPOS", 0, vop->data->win_size);
	if (!prop) {
		DRM_ERROR("failed to create zpos property\n");
		return -EINVAL;
	}
	vop->plane_zpos_prop = prop;

	vop->plane_feature_prop = drm_property_create_bitmask(vop->drm_dev,
				DRM_MODE_PROP_IMMUTABLE, "FEATURE",
				props, ARRAY_SIZE(props),
				BIT(ROCKCHIP_DRM_PLANE_FEATURE_SCALE) |
				BIT(ROCKCHIP_DRM_PLANE_FEATURE_ALPHA) |
				BIT(ROCKCHIP_DRM_PLANE_FEATURE_HDR2SDR) |
				BIT(ROCKCHIP_DRM_PLANE_FEATURE_SDR2HDR) |
				BIT(ROCKCHIP_DRM_PLANE_FEATURE_AFBDC));
	if (!vop->plane_feature_prop) {
		DRM_ERROR("failed to create feature property\n");
		return -EINVAL;
	}

	vop->feature_prop = drm_property_create_bitmask(vop->drm_dev,
				DRM_MODE_PROP_IMMUTABLE, "FEATURE",
				crtc_props, ARRAY_SIZE(crtc_props),
				BIT(ROCKCHIP_DRM_CRTC_FEATURE_AFBDC));
	if (!vop->feature_prop) {
		DRM_ERROR("failed to create vop feature property\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * rockchip_drm_wait_line_flag - acqiure the give line flag event
 * @crtc: CRTC to enable line flag
 * @line_num: interested line number
 * @mstimeout: millisecond for timeout
 *
 * Driver would hold here until the interested line flag interrupt have
 * happened or timeout to wait.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int rockchip_drm_wait_line_flag(struct drm_crtc *crtc, unsigned int line_num,
				unsigned int mstimeout)
{
	struct vop *vop = to_vop(crtc);
	unsigned long jiffies_left;
	int ret = 0;

	if (!crtc || !vop->is_enabled)
		return -ENODEV;

	mutex_lock(&vop->vop_lock);

	if (line_num > crtc->mode.vtotal || mstimeout <= 0) {
		ret = -EINVAL;
		goto out;
	}

	if (vop_line_flag_irq_is_enabled(vop)) {
		ret = -EBUSY;
		goto out;
	}

	reinit_completion(&vop->line_flag_completion);
	vop_line_flag_irq_enable(vop, line_num);

	jiffies_left = wait_for_completion_timeout(&vop->line_flag_completion,
						   msecs_to_jiffies(mstimeout));
	vop_line_flag_irq_disable(vop);

	if (jiffies_left == 0) {
		dev_err(vop->dev, "Timeout waiting for IRQ\n");
		ret = -ETIMEDOUT;
		goto out;
	}

out:
	mutex_unlock(&vop->vop_lock);

	return ret;
}
EXPORT_SYMBOL(rockchip_drm_wait_line_flag);

static void vop_backlight_config_done(struct device *dev, bool async)
{
	struct vop *vop = dev_get_drvdata(dev);

	if (vop && vop->is_enabled) {
		int dle;

		vop_cfg_done(vop);
		if (!async) {
			#define CTRL_GET(name) VOP_CTRL_GET(vop, name)
			readx_poll_timeout(CTRL_GET, cfg_done,
					   dle, !dle, 5, 33333);
			#undef CTRL_GET
		}
	}
}

static const struct rockchip_sub_backlight_ops rockchip_sub_backlight_ops = {
	.config_done = vop_backlight_config_done,
};

static int vop_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct vop_data *vop_data;
	struct drm_device *drm_dev = data;
	struct vop *vop;
	struct resource *res;
	size_t alloc_size;
	int ret, irq, i;
	int num_wins = 0;

	vop_data = of_device_get_match_data(dev);
	if (!vop_data)
		return -ENODEV;

	for (i = 0; i < vop_data->win_size; i++) {
		const struct vop_win_data *win_data = &vop_data->win[i];

		num_wins += win_data->area_size + 1;
	}

	/* Allocate vop struct and its vop_win array */
	alloc_size = sizeof(*vop) + sizeof(*vop->win) * num_wins;
	vop = devm_kzalloc(dev, alloc_size, GFP_KERNEL);
	if (!vop)
		return -ENOMEM;

	vop->dev = dev;
	vop->data = vop_data;
	vop->drm_dev = drm_dev;
	vop->num_wins = num_wins;
	vop->version = vop_data->version;
	dev_set_drvdata(dev, vop);

	ret = vop_win_init(vop);
	if (ret)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (!res) {
		dev_warn(vop->dev, "failed to get vop register byname\n");
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	}
	vop->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(vop->regs))
		return PTR_ERR(vop->regs);
	vop->len = resource_size(res);

	vop->regsbak = devm_kzalloc(dev, vop->len, GFP_KERNEL);
	if (!vop->regsbak)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gamma_lut");
	vop->lut_regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(vop->lut_regs)) {
		dev_warn(vop->dev, "failed to get vop lut registers\n");
		vop->lut_regs = NULL;
	}
	if (vop->lut_regs) {
		vop->lut_len = resource_size(res) / sizeof(*vop->lut);
		if (vop->lut_len != 256 && vop->lut_len != 1024) {
			dev_err(vop->dev, "unsupport lut sizes %d\n",
				vop->lut_len);
			return -EINVAL;
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cabc_lut");
	vop->cabc_lut_regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(vop->cabc_lut_regs)) {
		dev_warn(vop->dev, "failed to get vop cabc lut registers\n");
		vop->cabc_lut_regs = NULL;
	}

	if (vop->cabc_lut_regs) {
		vop->cabc_lut_len = resource_size(res) >> 2;
		if (vop->cabc_lut_len != 128) {
			dev_err(vop->dev, "unsupport cabc lut sizes %d\n",
				vop->cabc_lut_len);
			return -EINVAL;
		}
	}

	vop->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
						   "rockchip,grf");
	if (IS_ERR(vop->grf))
		dev_err(dev, "missing rockchip,grf property\n");

	vop->hclk = devm_clk_get(vop->dev, "hclk_vop");
	if (IS_ERR(vop->hclk)) {
		dev_err(vop->dev, "failed to get hclk source\n");
		return PTR_ERR(vop->hclk);
	}
	vop->aclk = devm_clk_get(vop->dev, "aclk_vop");
	if (IS_ERR(vop->aclk)) {
		dev_err(vop->dev, "failed to get aclk source\n");
		return PTR_ERR(vop->aclk);
	}
	vop->dclk = devm_clk_get(vop->dev, "dclk_vop");
	if (IS_ERR(vop->dclk)) {
		dev_err(vop->dev, "failed to get dclk source\n");
		return PTR_ERR(vop->dclk);
	}

	vop->dclk_source = devm_clk_get(vop->dev, "dclk_source");
	if (PTR_ERR(vop->dclk_source) == -ENOENT) {
		vop->dclk_source = NULL;
	} else if (PTR_ERR(vop->dclk_source) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(vop->dclk_source)) {
		dev_err(vop->dev, "failed to get dclk source parent\n");
		return PTR_ERR(vop->dclk_source);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "cannot find irq for vop\n");
		return irq;
	}
	vop->irq = (unsigned int)irq;

	spin_lock_init(&vop->reg_lock);
	spin_lock_init(&vop->irq_lock);
	mutex_init(&vop->vop_lock);

	mutex_init(&vop->vsync_mutex);

	ret = devm_request_irq(dev, vop->irq, vop_isr,
			       IRQF_SHARED, dev_name(dev), vop);
	if (ret)
		return ret;

	/* IRQ is initially disabled; it gets enabled in power_on */
	disable_irq(vop->irq);

	ret = vop_create_crtc(vop);
	if (ret)
		return ret;

	pm_runtime_enable(&pdev->dev);

	of_rockchip_drm_sub_backlight_register(dev, &vop->crtc,
					       &rockchip_sub_backlight_ops);

	return 0;
}

static void vop_unbind(struct device *dev, struct device *master, void *data)
{
	struct vop *vop = dev_get_drvdata(dev);

	pm_runtime_disable(dev);
	vop_destroy_crtc(vop);
}

const struct component_ops vop_component_ops = {
	.bind = vop_bind,
	.unbind = vop_unbind,
};
EXPORT_SYMBOL_GPL(vop_component_ops);
