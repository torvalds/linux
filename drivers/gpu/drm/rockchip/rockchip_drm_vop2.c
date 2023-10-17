// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2020 Rockchip Electronics Co., Ltd.
 * Author: Andy Yan <andy.yan@rock-chips.com>
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/swab.h>

#include <drm/drm.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_blend.h>
#include <drm/drm_crtc.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_flip_work.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include <uapi/linux/videodev2.h>
#include <dt-bindings/soc/rockchip,vop2.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_gem.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_vop2.h"
#include "rockchip_rgb.h"

/*
 * VOP2 architecture
 *
 +----------+   +-------------+                                                        +-----------+
 |  Cluster |   | Sel 1 from 6|                                                        | 1 from 3  |
 |  window0 |   |    Layer0   |                                                        |    RGB    |
 +----------+   +-------------+              +---------------+    +-------------+      +-----------+
 +----------+   +-------------+              |N from 6 layers|    |             |
 |  Cluster |   | Sel 1 from 6|              |   Overlay0    +--->| Video Port0 |      +-----------+
 |  window1 |   |    Layer1   |              |               |    |             |      | 1 from 3  |
 +----------+   +-------------+              +---------------+    +-------------+      |   LVDS    |
 +----------+   +-------------+                                                        +-----------+
 |  Esmart  |   | Sel 1 from 6|
 |  window0 |   |   Layer2    |              +---------------+    +-------------+      +-----------+
 +----------+   +-------------+              |N from 6 Layers|    |             | +--> | 1 from 3  |
 +----------+   +-------------+   -------->  |   Overlay1    +--->| Video Port1 |      |   MIPI    |
 |  Esmart  |   | Sel 1 from 6|   -------->  |               |    |             |      +-----------+
 |  Window1 |   |   Layer3    |              +---------------+    +-------------+
 +----------+   +-------------+                                                        +-----------+
 +----------+   +-------------+                                                        | 1 from 3  |
 |  Smart   |   | Sel 1 from 6|              +---------------+    +-------------+      |   HDMI    |
 |  Window0 |   |    Layer4   |              |N from 6 Layers|    |             |      +-----------+
 +----------+   +-------------+              |   Overlay2    +--->| Video Port2 |
 +----------+   +-------------+              |               |    |             |      +-----------+
 |  Smart   |   | Sel 1 from 6|              +---------------+    +-------------+      |  1 from 3 |
 |  Window1 |   |    Layer5   |                                                        |    eDP    |
 +----------+   +-------------+                                                        +-----------+
 *
 */

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

union vop2_alpha_ctrl {
	u32 val;
	struct {
		/* [0:1] */
		u32 color_mode:1;
		u32 alpha_mode:1;
		/* [2:3] */
		u32 blend_mode:2;
		u32 alpha_cal_mode:1;
		/* [5:7] */
		u32 factor_mode:3;
		/* [8:9] */
		u32 alpha_en:1;
		u32 src_dst_swap:1;
		u32 reserved:6;
		/* [16:23] */
		u32 glb_alpha:8;
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

struct vop2_win {
	struct vop2 *vop2;
	struct drm_plane base;
	const struct vop2_win_data *data;
	struct regmap_field *reg[VOP2_WIN_MAX_REG];

	/**
	 * @win_id: graphic window id, a cluster may be split into two
	 * graphics windows.
	 */
	u8 win_id;
	u8 delay;
	u32 offset;

	enum drm_plane_type type;
};

struct vop2_video_port {
	struct drm_crtc crtc;
	struct vop2 *vop2;
	struct clk *dclk;
	unsigned int id;
	const struct vop2_video_port_regs *regs;
	const struct vop2_video_port_data *data;

	struct completion dsp_hold_completion;

	/**
	 * @win_mask: Bitmask of windows attached to the video port;
	 */
	u32 win_mask;

	struct vop2_win *primary_plane;
	struct drm_pending_vblank_event *event;

	unsigned int nlayers;
};

struct vop2 {
	struct device *dev;
	struct drm_device *drm;
	struct vop2_video_port vps[ROCKCHIP_MAX_CRTC];

	const struct vop2_data *data;
	/*
	 * Number of windows that are registered as plane, may be less than the
	 * total number of hardware windows.
	 */
	u32 registered_num_wins;

	void __iomem *regs;
	struct regmap *map;

	struct regmap *grf;

	/* physical map length of vop2 register */
	u32 len;

	void __iomem *lut_regs;

	/* protects crtc enable/disable */
	struct mutex vop2_lock;

	int irq;

	/*
	 * Some global resources are shared between all video ports(crtcs), so
	 * we need a ref counter here.
	 */
	unsigned int enable_count;
	struct clk *hclk;
	struct clk *aclk;

	/* optional internal rgb encoder */
	struct rockchip_rgb *rgb;

	/* must be put at the end of the struct */
	struct vop2_win win[];
};

static struct vop2_video_port *to_vop2_video_port(struct drm_crtc *crtc)
{
	return container_of(crtc, struct vop2_video_port, crtc);
}

static struct vop2_win *to_vop2_win(struct drm_plane *p)
{
	return container_of(p, struct vop2_win, base);
}

static void vop2_lock(struct vop2 *vop2)
{
	mutex_lock(&vop2->vop2_lock);
}

static void vop2_unlock(struct vop2 *vop2)
{
	mutex_unlock(&vop2->vop2_lock);
}

static void vop2_writel(struct vop2 *vop2, u32 offset, u32 v)
{
	regmap_write(vop2->map, offset, v);
}

static void vop2_vp_write(struct vop2_video_port *vp, u32 offset, u32 v)
{
	regmap_write(vp->vop2->map, vp->data->offset + offset, v);
}

static u32 vop2_readl(struct vop2 *vop2, u32 offset)
{
	u32 val;

	regmap_read(vop2->map, offset, &val);

	return val;
}

static void vop2_win_write(const struct vop2_win *win, unsigned int reg, u32 v)
{
	regmap_field_write(win->reg[reg], v);
}

static bool vop2_cluster_window(const struct vop2_win *win)
{
	return win->data->feature & WIN_FEATURE_CLUSTER;
}

static void vop2_cfg_done(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;

	regmap_set_bits(vop2->map, RK3568_REG_CFG_DONE,
			BIT(vp->id) | RK3568_REG_CFG_DONE__GLB_CFG_DONE_EN);
}

static void vop2_win_disable(struct vop2_win *win)
{
	vop2_win_write(win, VOP2_WIN_ENABLE, 0);

	if (vop2_cluster_window(win))
		vop2_win_write(win, VOP2_WIN_CLUSTER_ENABLE, 0);
}

static enum vop2_data_format vop2_convert_format(u32 format)
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
	case DRM_FORMAT_NV16:
		return VOP2_FMT_YUV422SP;
	case DRM_FORMAT_NV24:
		return VOP2_FMT_YUV444SP;
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

static enum vop2_afbc_format vop2_convert_afbc_format(u32 format)
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
	case DRM_FORMAT_NV16:
		return VOP2_AFBC_FMT_YUV422;
	default:
		return VOP2_AFBC_FMT_INVALID;
	}

	return VOP2_AFBC_FMT_INVALID;
}

static bool vop2_win_rb_swap(u32 format)
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

static bool vop2_afbc_rb_swap(u32 format)
{
	switch (format) {
	case DRM_FORMAT_NV24:
		return true;
	default:
		return false;
	}
}

static bool vop2_afbc_uv_swap(u32 format)
{
	switch (format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
		return true;
	default:
		return false;
	}
}

static bool vop2_win_uv_swap(u32 format)
{
	switch (format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV24:
		return true;
	default:
		return false;
	}
}

static bool vop2_win_dither_up(u32 format)
{
	switch (format) {
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_RGB565:
		return true;
	default:
		return false;
	}
}

static bool vop2_output_uv_swap(u32 bus_format, u32 output_mode)
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

static bool is_yuv_output(u32 bus_format)
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

static bool rockchip_afbc(struct drm_plane *plane, u64 modifier)
{
	int i;

	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return false;

	for (i = 0 ; i < plane->modifier_count; i++)
		if (plane->modifiers[i] == modifier)
			return true;

	return false;
}

static bool rockchip_vop2_mod_supported(struct drm_plane *plane, u32 format,
					u64 modifier)
{
	struct vop2_win *win = to_vop2_win(plane);
	struct vop2 *vop2 = win->vop2;

	if (modifier == DRM_FORMAT_MOD_INVALID)
		return false;

	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return true;

	if (!rockchip_afbc(plane, modifier)) {
		drm_err(vop2->drm, "Unsupported format modifier 0x%llx\n",
			modifier);

		return false;
	}

	return vop2_convert_afbc_format(format) >= 0;
}

static u32 vop2_afbc_transform_offset(struct drm_plane_state *pstate,
				      bool afbc_half_block_en)
{
	struct drm_rect *src = &pstate->src;
	struct drm_framebuffer *fb = pstate->fb;
	u32 bpp = fb->format->cpp[0] * 8;
	u32 vir_width = (fb->pitches[0] << 3) / bpp;
	u32 width = drm_rect_width(src) >> 16;
	u32 height = drm_rect_height(src) >> 16;
	u32 act_xoffset = src->x1 >> 16;
	u32 act_yoffset = src->y1 >> 16;
	u32 align16_crop = 0;
	u32 align64_crop = 0;
	u32 height_tmp;
	u8 tx, ty;
	u8 bottom_crop_line_num = 0;

	/* 16 pixel align */
	if (height & 0xf)
		align16_crop = 16 - (height & 0xf);

	height_tmp = height + align16_crop;

	/* 64 pixel align */
	if (height_tmp & 0x3f)
		align64_crop = 64 - (height_tmp & 0x3f);

	bottom_crop_line_num = align16_crop + align64_crop;

	switch (pstate->rotation &
		(DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y |
		 DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270)) {
	case DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y:
		tx = 16 - ((act_xoffset + width) & 0xf);
		ty = bottom_crop_line_num - act_yoffset;
		break;
	case DRM_MODE_REFLECT_X | DRM_MODE_ROTATE_90:
		tx = bottom_crop_line_num - act_yoffset;
		ty = vir_width - width - act_xoffset;
		break;
	case DRM_MODE_REFLECT_X | DRM_MODE_ROTATE_270:
		tx = act_yoffset;
		ty = act_xoffset;
		break;
	case DRM_MODE_REFLECT_X:
		tx = 16 - ((act_xoffset + width) & 0xf);
		ty = act_yoffset;
		break;
	case DRM_MODE_REFLECT_Y:
		tx = act_xoffset;
		ty = bottom_crop_line_num - act_yoffset;
		break;
	case DRM_MODE_ROTATE_90:
		tx = bottom_crop_line_num - act_yoffset;
		ty = act_xoffset;
		break;
	case DRM_MODE_ROTATE_270:
		tx = act_yoffset;
		ty = vir_width - width - act_xoffset;
		break;
	case 0:
		tx = act_xoffset;
		ty = act_yoffset;
		break;
	}

	if (afbc_half_block_en)
		ty &= 0x7f;

#define TRANSFORM_XOFFSET GENMASK(7, 0)
#define TRANSFORM_YOFFSET GENMASK(23, 16)
	return FIELD_PREP(TRANSFORM_XOFFSET, tx) |
		FIELD_PREP(TRANSFORM_YOFFSET, ty);
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
static int vop2_get_cluster_lb_mode(struct vop2_win *win,
				    struct drm_plane_state *pstate)
{
	if ((pstate->rotation & DRM_MODE_ROTATE_270) ||
	    (pstate->rotation & DRM_MODE_ROTATE_90))
		return 2;
	else
		return 0;
}

static u16 vop2_scale_factor(u32 src, u32 dst)
{
	u32 fac;
	int shift;

	if (src == dst)
		return 0;

	if (dst < 2)
		return U16_MAX;

	if (src < 2)
		return 0;

	if (src > dst)
		shift = 12;
	else
		shift = 16;

	src--;
	dst--;

	fac = DIV_ROUND_UP(src << shift, dst) - 1;

	if (fac > U16_MAX)
		return U16_MAX;

	return fac;
}

static void vop2_setup_scale(struct vop2 *vop2, const struct vop2_win *win,
			     u32 src_w, u32 src_h, u32 dst_w,
			     u32 dst_h, u32 pixel_format)
{
	const struct drm_format_info *info;
	u16 hor_scl_mode, ver_scl_mode;
	u16 hscl_filter_mode, vscl_filter_mode;
	u8 gt2 = 0;
	u8 gt4 = 0;
	u32 val;

	info = drm_format_info(pixel_format);

	if (src_h >= (4 * dst_h)) {
		gt4 = 1;
		src_h >>= 2;
	} else if (src_h >= (2 * dst_h)) {
		gt2 = 1;
		src_h >>= 1;
	}

	hor_scl_mode = scl_get_scl_mode(src_w, dst_w);
	ver_scl_mode = scl_get_scl_mode(src_h, dst_h);

	if (hor_scl_mode == SCALE_UP)
		hscl_filter_mode = VOP2_SCALE_UP_BIC;
	else
		hscl_filter_mode = VOP2_SCALE_DOWN_BIL;

	if (ver_scl_mode == SCALE_UP)
		vscl_filter_mode = VOP2_SCALE_UP_BIL;
	else
		vscl_filter_mode = VOP2_SCALE_DOWN_BIL;

	/*
	 * RK3568 VOP Esmart/Smart dsp_w should be even pixel
	 * at scale down mode
	 */
	if (!(win->data->feature & WIN_FEATURE_AFBDC)) {
		if ((hor_scl_mode == SCALE_DOWN) && (dst_w & 0x1)) {
			drm_dbg(vop2->drm, "%s dst_w[%d] should align as 2 pixel\n",
				win->data->name, dst_w);
			dst_w++;
		}
	}

	val = vop2_scale_factor(src_w, dst_w);
	vop2_win_write(win, VOP2_WIN_SCALE_YRGB_X, val);
	val = vop2_scale_factor(src_h, dst_h);
	vop2_win_write(win, VOP2_WIN_SCALE_YRGB_Y, val);

	vop2_win_write(win, VOP2_WIN_VSD_YRGB_GT4, gt4);
	vop2_win_write(win, VOP2_WIN_VSD_YRGB_GT2, gt2);

	vop2_win_write(win, VOP2_WIN_YRGB_HOR_SCL_MODE, hor_scl_mode);
	vop2_win_write(win, VOP2_WIN_YRGB_VER_SCL_MODE, ver_scl_mode);

	if (vop2_cluster_window(win))
		return;

	vop2_win_write(win, VOP2_WIN_YRGB_HSCL_FILTER_MODE, hscl_filter_mode);
	vop2_win_write(win, VOP2_WIN_YRGB_VSCL_FILTER_MODE, vscl_filter_mode);

	if (info->is_yuv) {
		src_w /= info->hsub;
		src_h /= info->vsub;

		gt4 = 0;
		gt2 = 0;

		if (src_h >= (4 * dst_h)) {
			gt4 = 1;
			src_h >>= 2;
		} else if (src_h >= (2 * dst_h)) {
			gt2 = 1;
			src_h >>= 1;
		}

		hor_scl_mode = scl_get_scl_mode(src_w, dst_w);
		ver_scl_mode = scl_get_scl_mode(src_h, dst_h);

		val = vop2_scale_factor(src_w, dst_w);
		vop2_win_write(win, VOP2_WIN_SCALE_CBCR_X, val);

		val = vop2_scale_factor(src_h, dst_h);
		vop2_win_write(win, VOP2_WIN_SCALE_CBCR_Y, val);

		vop2_win_write(win, VOP2_WIN_VSD_CBCR_GT4, gt4);
		vop2_win_write(win, VOP2_WIN_VSD_CBCR_GT2, gt2);
		vop2_win_write(win, VOP2_WIN_CBCR_HOR_SCL_MODE, hor_scl_mode);
		vop2_win_write(win, VOP2_WIN_CBCR_VER_SCL_MODE, ver_scl_mode);
		vop2_win_write(win, VOP2_WIN_CBCR_HSCL_FILTER_MODE, hscl_filter_mode);
		vop2_win_write(win, VOP2_WIN_CBCR_VSCL_FILTER_MODE, vscl_filter_mode);
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
				struct vop2_win *win,
				struct drm_plane_state *pstate)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(vp->crtc.state);
	int is_input_yuv = pstate->fb->format->is_yuv;
	int is_output_yuv = is_yuv_output(vcstate->bus_format);
	int input_csc = V4L2_COLORSPACE_DEFAULT;
	int output_csc = vcstate->color_space;
	bool r2y_en, y2r_en;
	int csc_mode;

	if (is_input_yuv && !is_output_yuv) {
		y2r_en = true;
		r2y_en = false;
		csc_mode = vop2_convert_csc_mode(input_csc);
	} else if (!is_input_yuv && is_output_yuv) {
		y2r_en = false;
		r2y_en = true;
		csc_mode = vop2_convert_csc_mode(output_csc);
	} else {
		y2r_en = false;
		r2y_en = false;
		csc_mode = false;
	}

	vop2_win_write(win, VOP2_WIN_Y2R_EN, y2r_en);
	vop2_win_write(win, VOP2_WIN_R2Y_EN, r2y_en);
	vop2_win_write(win, VOP2_WIN_CSC_MODE, csc_mode);
}

static void vop2_crtc_enable_irq(struct vop2_video_port *vp, u32 irq)
{
	struct vop2 *vop2 = vp->vop2;

	vop2_writel(vop2, RK3568_VP_INT_CLR(vp->id), irq << 16 | irq);
	vop2_writel(vop2, RK3568_VP_INT_EN(vp->id), irq << 16 | irq);
}

static void vop2_crtc_disable_irq(struct vop2_video_port *vp, u32 irq)
{
	struct vop2 *vop2 = vp->vop2;

	vop2_writel(vop2, RK3568_VP_INT_EN(vp->id), irq << 16);
}

static int vop2_core_clks_prepare_enable(struct vop2 *vop2)
{
	int ret;

	ret = clk_prepare_enable(vop2->hclk);
	if (ret < 0) {
		drm_err(vop2->drm, "failed to enable hclk - %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vop2->aclk);
	if (ret < 0) {
		drm_err(vop2->drm, "failed to enable aclk - %d\n", ret);
		goto err;
	}

	return 0;
err:
	clk_disable_unprepare(vop2->hclk);

	return ret;
}

static void vop2_enable(struct vop2 *vop2)
{
	int ret;

	ret = pm_runtime_resume_and_get(vop2->dev);
	if (ret < 0) {
		drm_err(vop2->drm, "failed to get pm runtime: %d\n", ret);
		return;
	}

	ret = vop2_core_clks_prepare_enable(vop2);
	if (ret) {
		pm_runtime_put_sync(vop2->dev);
		return;
	}

	ret = rockchip_drm_dma_attach_device(vop2->drm, vop2->dev);
	if (ret) {
		drm_err(vop2->drm, "failed to attach dma mapping, %d\n", ret);
		return;
	}

	regcache_sync(vop2->map);

	if (vop2->data->soc_id == 3566)
		vop2_writel(vop2, RK3568_OTP_WIN_EN, 1);

	vop2_writel(vop2, RK3568_REG_CFG_DONE, RK3568_REG_CFG_DONE__GLB_CFG_DONE_EN);

	/*
	 * Disable auto gating, this is a workaround to
	 * avoid display image shift when a window enabled.
	 */
	regmap_clear_bits(vop2->map, RK3568_SYS_AUTO_GATING_CTRL,
			  RK3568_SYS_AUTO_GATING_CTRL__AUTO_GATING_EN);

	vop2_writel(vop2, RK3568_SYS0_INT_CLR,
		    VOP2_INT_BUS_ERRPR << 16 | VOP2_INT_BUS_ERRPR);
	vop2_writel(vop2, RK3568_SYS0_INT_EN,
		    VOP2_INT_BUS_ERRPR << 16 | VOP2_INT_BUS_ERRPR);
	vop2_writel(vop2, RK3568_SYS1_INT_CLR,
		    VOP2_INT_BUS_ERRPR << 16 | VOP2_INT_BUS_ERRPR);
	vop2_writel(vop2, RK3568_SYS1_INT_EN,
		    VOP2_INT_BUS_ERRPR << 16 | VOP2_INT_BUS_ERRPR);
}

static void vop2_disable(struct vop2 *vop2)
{
	rockchip_drm_dma_detach_device(vop2->drm, vop2->dev);

	pm_runtime_put_sync(vop2->dev);

	regcache_mark_dirty(vop2->map);

	clk_disable_unprepare(vop2->aclk);
	clk_disable_unprepare(vop2->hclk);
}

static void vop2_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct drm_crtc_state *old_crtc_state;
	int ret;

	vop2_lock(vop2);

	old_crtc_state = drm_atomic_get_old_crtc_state(state, crtc);
	drm_atomic_helper_disable_planes_on_crtc(old_crtc_state, false);

	drm_crtc_vblank_off(crtc);

	/*
	 * Vop standby will take effect at end of current frame,
	 * if dsp hold valid irq happen, it means standby complete.
	 *
	 * we must wait standby complete when we want to disable aclk,
	 * if not, memory bus maybe dead.
	 */
	reinit_completion(&vp->dsp_hold_completion);

	vop2_crtc_enable_irq(vp, VP_INT_DSP_HOLD_VALID);

	vop2_vp_write(vp, RK3568_VP_DSP_CTRL, RK3568_VP_DSP_CTRL__STANDBY);

	ret = wait_for_completion_timeout(&vp->dsp_hold_completion,
					  msecs_to_jiffies(50));
	if (!ret)
		drm_info(vop2->drm, "wait for vp%d dsp_hold timeout\n", vp->id);

	vop2_crtc_disable_irq(vp, VP_INT_DSP_HOLD_VALID);

	clk_disable_unprepare(vp->dclk);

	vop2->enable_count--;

	if (!vop2->enable_count)
		vop2_disable(vop2);

	vop2_unlock(vop2);

	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}
}

static int vop2_plane_atomic_check(struct drm_plane *plane,
				   struct drm_atomic_state *astate)
{
	struct drm_plane_state *pstate = drm_atomic_get_new_plane_state(astate, plane);
	struct drm_framebuffer *fb = pstate->fb;
	struct drm_crtc *crtc = pstate->crtc;
	struct drm_crtc_state *cstate;
	struct vop2_video_port *vp;
	struct vop2 *vop2;
	const struct vop2_data *vop2_data;
	struct drm_rect *dest = &pstate->dst;
	struct drm_rect *src = &pstate->src;
	int min_scale = FRAC_16_16(1, 8);
	int max_scale = FRAC_16_16(8, 1);
	int format;
	int ret;

	if (!crtc)
		return 0;

	vp = to_vop2_video_port(crtc);
	vop2 = vp->vop2;
	vop2_data = vop2->data;

	cstate = drm_atomic_get_existing_crtc_state(pstate->state, crtc);
	if (WARN_ON(!cstate))
		return -EINVAL;

	ret = drm_atomic_helper_check_plane_state(pstate, cstate,
						  min_scale, max_scale,
						  true, true);
	if (ret)
		return ret;

	if (!pstate->visible)
		return 0;

	format = vop2_convert_format(fb->format->format);
	if (format < 0)
		return format;

	if (drm_rect_width(src) >> 16 < 4 || drm_rect_height(src) >> 16 < 4 ||
	    drm_rect_width(dest) < 4 || drm_rect_width(dest) < 4) {
		drm_err(vop2->drm, "Invalid size: %dx%d->%dx%d, min size is 4x4\n",
			drm_rect_width(src) >> 16, drm_rect_height(src) >> 16,
			drm_rect_width(dest), drm_rect_height(dest));
		pstate->visible = false;
		return 0;
	}

	if (drm_rect_width(src) >> 16 > vop2_data->max_input.width ||
	    drm_rect_height(src) >> 16 > vop2_data->max_input.height) {
		drm_err(vop2->drm, "Invalid source: %dx%d. max input: %dx%d\n",
			drm_rect_width(src) >> 16,
			drm_rect_height(src) >> 16,
			vop2_data->max_input.width,
			vop2_data->max_input.height);
		return -EINVAL;
	}

	/*
	 * Src.x1 can be odd when do clip, but yuv plane start point
	 * need align with 2 pixel.
	 */
	if (fb->format->is_yuv && ((pstate->src.x1 >> 16) % 2)) {
		drm_err(vop2->drm, "Invalid Source: Yuv format not support odd xpos\n");
		return -EINVAL;
	}

	return 0;
}

static void vop2_plane_atomic_disable(struct drm_plane *plane,
				      struct drm_atomic_state *state)
{
	struct drm_plane_state *old_pstate = NULL;
	struct vop2_win *win = to_vop2_win(plane);
	struct vop2 *vop2 = win->vop2;

	drm_dbg(vop2->drm, "%s disable\n", win->data->name);

	if (state)
		old_pstate = drm_atomic_get_old_plane_state(state, plane);
	if (old_pstate && !old_pstate->crtc)
		return;

	vop2_win_disable(win);
	vop2_win_write(win, VOP2_WIN_YUV_CLIP, 0);
}

/*
 * The color key is 10 bit, so all format should
 * convert to 10 bit here.
 */
static void vop2_plane_setup_color_key(struct drm_plane *plane, u32 color_key)
{
	struct drm_plane_state *pstate = plane->state;
	struct drm_framebuffer *fb = pstate->fb;
	struct vop2_win *win = to_vop2_win(plane);
	u32 color_key_en = 0;
	u32 r = 0;
	u32 g = 0;
	u32 b = 0;

	if (!(color_key & VOP2_COLOR_KEY_MASK) || fb->format->is_yuv) {
		vop2_win_write(win, VOP2_WIN_COLOR_KEY_EN, 0);
		return;
	}

	switch (fb->format->format) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		r = (color_key & 0xf800) >> 11;
		g = (color_key & 0x7e0) >> 5;
		b = (color_key & 0x1f);
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
		r = (color_key & 0xff0000) >> 16;
		g = (color_key & 0xff00) >> 8;
		b = (color_key & 0xff);
		r <<= 2;
		g <<= 2;
		b <<= 2;
		color_key_en = 1;
		break;
	}

	vop2_win_write(win, VOP2_WIN_COLOR_KEY_EN, color_key_en);
	vop2_win_write(win, VOP2_WIN_COLOR_KEY, (r << 20) | (g << 10) | b);
}

static void vop2_plane_atomic_update(struct drm_plane *plane,
				     struct drm_atomic_state *state)
{
	struct drm_plane_state *pstate = plane->state;
	struct drm_crtc *crtc = pstate->crtc;
	struct vop2_win *win = to_vop2_win(plane);
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	struct vop2 *vop2 = win->vop2;
	struct drm_framebuffer *fb = pstate->fb;
	u32 bpp = fb->format->cpp[0] * 8;
	u32 actual_w, actual_h, dsp_w, dsp_h;
	u32 act_info, dsp_info;
	u32 format;
	u32 afbc_format;
	u32 rb_swap;
	u32 uv_swap;
	struct drm_rect *src = &pstate->src;
	struct drm_rect *dest = &pstate->dst;
	u32 afbc_tile_num;
	u32 transform_offset;
	bool dither_up;
	bool xmirror = pstate->rotation & DRM_MODE_REFLECT_X ? true : false;
	bool ymirror = pstate->rotation & DRM_MODE_REFLECT_Y ? true : false;
	bool rotate_270 = pstate->rotation & DRM_MODE_ROTATE_270;
	bool rotate_90 = pstate->rotation & DRM_MODE_ROTATE_90;
	struct rockchip_gem_object *rk_obj;
	unsigned long offset;
	bool afbc_en;
	dma_addr_t yrgb_mst;
	dma_addr_t uv_mst;

	/*
	 * can't update plane when vop2 is disabled.
	 */
	if (WARN_ON(!crtc))
		return;

	if (!pstate->visible) {
		vop2_plane_atomic_disable(plane, state);
		return;
	}

	afbc_en = rockchip_afbc(plane, fb->modifier);

	offset = (src->x1 >> 16) * fb->format->cpp[0];

	/*
	 * AFBC HDR_PTR must set to the zero offset of the framebuffer.
	 */
	if (afbc_en)
		offset = 0;
	else if (pstate->rotation & DRM_MODE_REFLECT_Y)
		offset += ((src->y2 >> 16) - 1) * fb->pitches[0];
	else
		offset += (src->y1 >> 16) * fb->pitches[0];

	rk_obj = to_rockchip_obj(fb->obj[0]);

	yrgb_mst = rk_obj->dma_addr + offset + fb->offsets[0];
	if (fb->format->is_yuv) {
		int hsub = fb->format->hsub;
		int vsub = fb->format->vsub;

		offset = (src->x1 >> 16) * fb->format->cpp[1] / hsub;
		offset += (src->y1 >> 16) * fb->pitches[1] / vsub;

		if ((pstate->rotation & DRM_MODE_REFLECT_Y) && !afbc_en)
			offset += fb->pitches[1] * ((pstate->src_h >> 16) - 2) / vsub;

		rk_obj = to_rockchip_obj(fb->obj[0]);
		uv_mst = rk_obj->dma_addr + offset + fb->offsets[1];
	}

	actual_w = drm_rect_width(src) >> 16;
	actual_h = drm_rect_height(src) >> 16;
	dsp_w = drm_rect_width(dest);

	if (dest->x1 + dsp_w > adjusted_mode->hdisplay) {
		drm_err(vop2->drm, "vp%d %s dest->x1[%d] + dsp_w[%d] exceed mode hdisplay[%d]\n",
			vp->id, win->data->name, dest->x1, dsp_w, adjusted_mode->hdisplay);
		dsp_w = adjusted_mode->hdisplay - dest->x1;
		if (dsp_w < 4)
			dsp_w = 4;
		actual_w = dsp_w * actual_w / drm_rect_width(dest);
	}

	dsp_h = drm_rect_height(dest);

	if (dest->y1 + dsp_h > adjusted_mode->vdisplay) {
		drm_err(vop2->drm, "vp%d %s dest->y1[%d] + dsp_h[%d] exceed mode vdisplay[%d]\n",
			vp->id, win->data->name, dest->y1, dsp_h, adjusted_mode->vdisplay);
		dsp_h = adjusted_mode->vdisplay - dest->y1;
		if (dsp_h < 4)
			dsp_h = 4;
		actual_h = dsp_h * actual_h / drm_rect_height(dest);
	}

	/*
	 * This is workaround solution for IC design:
	 * esmart can't support scale down when actual_w % 16 == 1.
	 */
	if (!(win->data->feature & WIN_FEATURE_AFBDC)) {
		if (actual_w > dsp_w && (actual_w & 0xf) == 1) {
			drm_err(vop2->drm, "vp%d %s act_w[%d] MODE 16 == 1\n",
				vp->id, win->data->name, actual_w);
			actual_w -= 1;
		}
	}

	if (afbc_en && actual_w % 4) {
		drm_err(vop2->drm, "vp%d %s actual_w[%d] not 4 pixel aligned\n",
			vp->id, win->data->name, actual_w);
		actual_w = ALIGN_DOWN(actual_w, 4);
	}

	act_info = (actual_h - 1) << 16 | ((actual_w - 1) & 0xffff);
	dsp_info = (dsp_h - 1) << 16 | ((dsp_w - 1) & 0xffff);

	format = vop2_convert_format(fb->format->format);

	drm_dbg(vop2->drm, "vp%d update %s[%dx%d->%dx%d@%dx%d] fmt[%p4cc_%s] addr[%pad]\n",
		vp->id, win->data->name, actual_w, actual_h, dsp_w, dsp_h,
		dest->x1, dest->y1,
		&fb->format->format,
		afbc_en ? "AFBC" : "", &yrgb_mst);

	if (afbc_en) {
		u32 stride;

		/* the afbc superblock is 16 x 16 */
		afbc_format = vop2_convert_afbc_format(fb->format->format);

		/* Enable color transform for YTR */
		if (fb->modifier & AFBC_FORMAT_MOD_YTR)
			afbc_format |= (1 << 4);

		afbc_tile_num = ALIGN(actual_w, 16) >> 4;

		/*
		 * AFBC pic_vir_width is count by pixel, this is different
		 * with WIN_VIR_STRIDE.
		 */
		stride = (fb->pitches[0] << 3) / bpp;
		if ((stride & 0x3f) && (xmirror || rotate_90 || rotate_270))
			drm_err(vop2->drm, "vp%d %s stride[%d] not 64 pixel aligned\n",
				vp->id, win->data->name, stride);

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
		if (fb->format->is_yuv && bpp == 10)
			format = VOP2_CLUSTER_YUV444_10;

		if (vop2_cluster_window(win))
			vop2_win_write(win, VOP2_WIN_AFBC_ENABLE, 1);
		vop2_win_write(win, VOP2_WIN_AFBC_FORMAT, afbc_format);
		vop2_win_write(win, VOP2_WIN_AFBC_RB_SWAP, rb_swap);
		vop2_win_write(win, VOP2_WIN_AFBC_UV_SWAP, uv_swap);
		vop2_win_write(win, VOP2_WIN_AFBC_AUTO_GATING_EN, 0);
		vop2_win_write(win, VOP2_WIN_AFBC_BLOCK_SPLIT_EN, 0);
		if (pstate->rotation & (DRM_MODE_ROTATE_270 | DRM_MODE_ROTATE_90)) {
			vop2_win_write(win, VOP2_WIN_AFBC_HALF_BLOCK_EN, 0);
			transform_offset = vop2_afbc_transform_offset(pstate, false);
		} else {
			vop2_win_write(win, VOP2_WIN_AFBC_HALF_BLOCK_EN, 1);
			transform_offset = vop2_afbc_transform_offset(pstate, true);
		}
		vop2_win_write(win, VOP2_WIN_AFBC_HDR_PTR, yrgb_mst);
		vop2_win_write(win, VOP2_WIN_AFBC_PIC_SIZE, act_info);
		vop2_win_write(win, VOP2_WIN_AFBC_TRANSFORM_OFFSET, transform_offset);
		vop2_win_write(win, VOP2_WIN_AFBC_PIC_OFFSET, ((src->x1 >> 16) | src->y1));
		vop2_win_write(win, VOP2_WIN_AFBC_DSP_OFFSET, (dest->x1 | (dest->y1 << 16)));
		vop2_win_write(win, VOP2_WIN_AFBC_PIC_VIR_WIDTH, stride);
		vop2_win_write(win, VOP2_WIN_AFBC_TILE_NUM, afbc_tile_num);
		vop2_win_write(win, VOP2_WIN_XMIRROR, xmirror);
		vop2_win_write(win, VOP2_WIN_AFBC_ROTATE_270, rotate_270);
		vop2_win_write(win, VOP2_WIN_AFBC_ROTATE_90, rotate_90);
	} else {
		vop2_win_write(win, VOP2_WIN_YRGB_VIR, DIV_ROUND_UP(fb->pitches[0], 4));
	}

	vop2_win_write(win, VOP2_WIN_YMIRROR, ymirror);

	if (rotate_90 || rotate_270) {
		act_info = swahw32(act_info);
		actual_w = drm_rect_height(src) >> 16;
		actual_h = drm_rect_width(src) >> 16;
	}

	vop2_win_write(win, VOP2_WIN_FORMAT, format);
	vop2_win_write(win, VOP2_WIN_YRGB_MST, yrgb_mst);

	rb_swap = vop2_win_rb_swap(fb->format->format);
	vop2_win_write(win, VOP2_WIN_RB_SWAP, rb_swap);
	if (!vop2_cluster_window(win)) {
		uv_swap = vop2_win_uv_swap(fb->format->format);
		vop2_win_write(win, VOP2_WIN_UV_SWAP, uv_swap);
	}

	if (fb->format->is_yuv) {
		vop2_win_write(win, VOP2_WIN_UV_VIR, DIV_ROUND_UP(fb->pitches[1], 4));
		vop2_win_write(win, VOP2_WIN_UV_MST, uv_mst);
	}

	vop2_setup_scale(vop2, win, actual_w, actual_h, dsp_w, dsp_h, fb->format->format);
	if (!vop2_cluster_window(win))
		vop2_plane_setup_color_key(plane, 0);
	vop2_win_write(win, VOP2_WIN_ACT_INFO, act_info);
	vop2_win_write(win, VOP2_WIN_DSP_INFO, dsp_info);
	vop2_win_write(win, VOP2_WIN_DSP_ST, dest->y1 << 16 | (dest->x1 & 0xffff));

	vop2_setup_csc_mode(vp, win, pstate);

	dither_up = vop2_win_dither_up(fb->format->format);
	vop2_win_write(win, VOP2_WIN_DITHER_UP, dither_up);

	vop2_win_write(win, VOP2_WIN_ENABLE, 1);

	if (vop2_cluster_window(win)) {
		int lb_mode = vop2_get_cluster_lb_mode(win, pstate);

		vop2_win_write(win, VOP2_WIN_CLUSTER_LB_MODE, lb_mode);
		vop2_win_write(win, VOP2_WIN_CLUSTER_ENABLE, 1);
	}
}

static const struct drm_plane_helper_funcs vop2_plane_helper_funcs = {
	.atomic_check = vop2_plane_atomic_check,
	.atomic_update = vop2_plane_atomic_update,
	.atomic_disable = vop2_plane_atomic_disable,
};

static const struct drm_plane_funcs vop2_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
	.format_mod_supported = rockchip_vop2_mod_supported,
};

static int vop2_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);

	vop2_crtc_enable_irq(vp, VP_INT_FS_FIELD);

	return 0;
}

static void vop2_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);

	vop2_crtc_disable_irq(vp, VP_INT_FS_FIELD);
}

static bool vop2_crtc_mode_fixup(struct drm_crtc *crtc,
				 const struct drm_display_mode *mode,
				 struct drm_display_mode *adj_mode)
{
	drm_mode_set_crtcinfo(adj_mode, CRTC_INTERLACE_HALVE_V |
					CRTC_STEREO_DOUBLE);

	return true;
}

static void vop2_dither_setup(struct drm_crtc *crtc, u32 *dsp_ctrl)
{
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);

	switch (vcstate->bus_format) {
	case MEDIA_BUS_FMT_RGB565_1X16:
		*dsp_ctrl |= RK3568_VP_DSP_CTRL__DITHER_DOWN_EN;
		break;
	case MEDIA_BUS_FMT_RGB666_1X18:
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
		*dsp_ctrl |= RK3568_VP_DSP_CTRL__DITHER_DOWN_EN;
		*dsp_ctrl |= RGB888_TO_RGB666;
		break;
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
		*dsp_ctrl |= RK3568_VP_DSP_CTRL__PRE_DITHER_DOWN_EN;
		break;
	default:
		break;
	}

	if (vcstate->output_mode != ROCKCHIP_OUT_MODE_AAAA)
		*dsp_ctrl |= RK3568_VP_DSP_CTRL__PRE_DITHER_DOWN_EN;

	*dsp_ctrl |= FIELD_PREP(RK3568_VP_DSP_CTRL__DITHER_DOWN_SEL,
				DITHER_DOWN_ALLEGRO);
}

static void vop2_post_config(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	u16 vtotal = mode->crtc_vtotal;
	u16 hdisplay = mode->crtc_hdisplay;
	u16 hact_st = mode->crtc_htotal - mode->crtc_hsync_start;
	u16 vdisplay = mode->crtc_vdisplay;
	u16 vact_st = mode->crtc_vtotal - mode->crtc_vsync_start;
	u32 left_margin = 100, right_margin = 100;
	u32 top_margin = 100, bottom_margin = 100;
	u16 hsize = hdisplay * (left_margin + right_margin) / 200;
	u16 vsize = vdisplay * (top_margin + bottom_margin) / 200;
	u16 hact_end, vact_end;
	u32 val;

	vsize = rounddown(vsize, 2);
	hsize = rounddown(hsize, 2);
	hact_st += hdisplay * (100 - left_margin) / 200;
	hact_end = hact_st + hsize;
	val = hact_st << 16;
	val |= hact_end;
	vop2_vp_write(vp, RK3568_VP_POST_DSP_HACT_INFO, val);
	vact_st += vdisplay * (100 - top_margin) / 200;
	vact_end = vact_st + vsize;
	val = vact_st << 16;
	val |= vact_end;
	vop2_vp_write(vp, RK3568_VP_POST_DSP_VACT_INFO, val);
	val = scl_cal_scale2(vdisplay, vsize) << 16;
	val |= scl_cal_scale2(hdisplay, hsize);
	vop2_vp_write(vp, RK3568_VP_POST_SCL_FACTOR_YRGB, val);

	val = 0;
	if (hdisplay != hsize)
		val |= RK3568_VP_POST_SCL_CTRL__HSCALEDOWN;
	if (vdisplay != vsize)
		val |= RK3568_VP_POST_SCL_CTRL__VSCALEDOWN;
	vop2_vp_write(vp, RK3568_VP_POST_SCL_CTRL, val);

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		u16 vact_st_f1 = vtotal + vact_st + 1;
		u16 vact_end_f1 = vact_st_f1 + vsize;

		val = vact_st_f1 << 16 | vact_end_f1;
		vop2_vp_write(vp, RK3568_VP_POST_DSP_VACT_INFO_F1, val);
	}

	vop2_vp_write(vp, RK3568_VP_DSP_BG, 0);
}

static void rk3568_set_intf_mux(struct vop2_video_port *vp, int id,
				u32 polflags)
{
	struct vop2 *vop2 = vp->vop2;
	u32 die, dip;

	die = vop2_readl(vop2, RK3568_DSP_IF_EN);
	dip = vop2_readl(vop2, RK3568_DSP_IF_POL);

	switch (id) {
	case ROCKCHIP_VOP2_EP_RGB0:
		die &= ~RK3568_SYS_DSP_INFACE_EN_RGB_MUX;
		die |= RK3568_SYS_DSP_INFACE_EN_RGB |
			   FIELD_PREP(RK3568_SYS_DSP_INFACE_EN_RGB_MUX, vp->id);
		dip &= ~RK3568_DSP_IF_POL__RGB_LVDS_PIN_POL;
		dip |= FIELD_PREP(RK3568_DSP_IF_POL__RGB_LVDS_PIN_POL, polflags);
		if (polflags & POLFLAG_DCLK_INV)
			regmap_write(vop2->grf, RK3568_GRF_VO_CON1, BIT(3 + 16) | BIT(3));
		else
			regmap_write(vop2->grf, RK3568_GRF_VO_CON1, BIT(3 + 16));
		break;
	case ROCKCHIP_VOP2_EP_HDMI0:
		die &= ~RK3568_SYS_DSP_INFACE_EN_HDMI_MUX;
		die |= RK3568_SYS_DSP_INFACE_EN_HDMI |
			   FIELD_PREP(RK3568_SYS_DSP_INFACE_EN_HDMI_MUX, vp->id);
		dip &= ~RK3568_DSP_IF_POL__HDMI_PIN_POL;
		dip |= FIELD_PREP(RK3568_DSP_IF_POL__HDMI_PIN_POL, polflags);
		break;
	case ROCKCHIP_VOP2_EP_EDP0:
		die &= ~RK3568_SYS_DSP_INFACE_EN_EDP_MUX;
		die |= RK3568_SYS_DSP_INFACE_EN_EDP |
			   FIELD_PREP(RK3568_SYS_DSP_INFACE_EN_EDP_MUX, vp->id);
		dip &= ~RK3568_DSP_IF_POL__EDP_PIN_POL;
		dip |= FIELD_PREP(RK3568_DSP_IF_POL__EDP_PIN_POL, polflags);
		break;
	case ROCKCHIP_VOP2_EP_MIPI0:
		die &= ~RK3568_SYS_DSP_INFACE_EN_MIPI0_MUX;
		die |= RK3568_SYS_DSP_INFACE_EN_MIPI0 |
			   FIELD_PREP(RK3568_SYS_DSP_INFACE_EN_MIPI0_MUX, vp->id);
		dip &= ~RK3568_DSP_IF_POL__MIPI_PIN_POL;
		dip |= FIELD_PREP(RK3568_DSP_IF_POL__MIPI_PIN_POL, polflags);
		break;
	case ROCKCHIP_VOP2_EP_MIPI1:
		die &= ~RK3568_SYS_DSP_INFACE_EN_MIPI1_MUX;
		die |= RK3568_SYS_DSP_INFACE_EN_MIPI1 |
			   FIELD_PREP(RK3568_SYS_DSP_INFACE_EN_MIPI1_MUX, vp->id);
		dip &= ~RK3568_DSP_IF_POL__MIPI_PIN_POL;
		dip |= FIELD_PREP(RK3568_DSP_IF_POL__MIPI_PIN_POL, polflags);
		break;
	case ROCKCHIP_VOP2_EP_LVDS0:
		die &= ~RK3568_SYS_DSP_INFACE_EN_LVDS0_MUX;
		die |= RK3568_SYS_DSP_INFACE_EN_LVDS0 |
			   FIELD_PREP(RK3568_SYS_DSP_INFACE_EN_LVDS0_MUX, vp->id);
		dip &= ~RK3568_DSP_IF_POL__RGB_LVDS_PIN_POL;
		dip |= FIELD_PREP(RK3568_DSP_IF_POL__RGB_LVDS_PIN_POL, polflags);
		break;
	case ROCKCHIP_VOP2_EP_LVDS1:
		die &= ~RK3568_SYS_DSP_INFACE_EN_LVDS1_MUX;
		die |= RK3568_SYS_DSP_INFACE_EN_LVDS1 |
			   FIELD_PREP(RK3568_SYS_DSP_INFACE_EN_LVDS1_MUX, vp->id);
		dip &= ~RK3568_DSP_IF_POL__RGB_LVDS_PIN_POL;
		dip |= FIELD_PREP(RK3568_DSP_IF_POL__RGB_LVDS_PIN_POL, polflags);
		break;
	default:
		drm_err(vop2->drm, "Invalid interface id %d on vp%d\n", id, vp->id);
		return;
	}

	dip |= RK3568_DSP_IF_POL__CFG_DONE_IMD;

	vop2_writel(vop2, RK3568_DSP_IF_EN, die);
	vop2_writel(vop2, RK3568_DSP_IF_POL, dip);
}

static int us_to_vertical_line(struct drm_display_mode *mode, int us)
{
	return us * mode->clock / mode->htotal / 1000;
}

static void vop2_crtc_atomic_enable(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	const struct vop2_data *vop2_data = vop2->data;
	const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	unsigned long clock = mode->crtc_clock * 1000;
	u16 hsync_len = mode->crtc_hsync_end - mode->crtc_hsync_start;
	u16 hdisplay = mode->crtc_hdisplay;
	u16 htotal = mode->crtc_htotal;
	u16 hact_st = mode->crtc_htotal - mode->crtc_hsync_start;
	u16 hact_end = hact_st + hdisplay;
	u16 vdisplay = mode->crtc_vdisplay;
	u16 vtotal = mode->crtc_vtotal;
	u16 vsync_len = mode->crtc_vsync_end - mode->crtc_vsync_start;
	u16 vact_st = mode->crtc_vtotal - mode->crtc_vsync_start;
	u16 vact_end = vact_st + vdisplay;
	u8 out_mode;
	u32 dsp_ctrl = 0;
	int act_end;
	u32 val, polflags;
	int ret;
	struct drm_encoder *encoder;

	drm_dbg(vop2->drm, "Update mode to %dx%d%s%d, type: %d for vp%d\n",
		hdisplay, vdisplay, mode->flags & DRM_MODE_FLAG_INTERLACE ? "i" : "p",
		drm_mode_vrefresh(mode), vcstate->output_type, vp->id);

	vop2_lock(vop2);

	ret = clk_prepare_enable(vp->dclk);
	if (ret < 0) {
		drm_err(vop2->drm, "failed to enable dclk for video port%d - %d\n",
			vp->id, ret);
		vop2_unlock(vop2);
		return;
	}

	if (!vop2->enable_count)
		vop2_enable(vop2);

	vop2->enable_count++;

	vop2_crtc_enable_irq(vp, VP_INT_POST_BUF_EMPTY);

	polflags = 0;
	if (vcstate->bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE)
		polflags |= POLFLAG_DCLK_INV;
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		polflags |= BIT(HSYNC_POSITIVE);
	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		polflags |= BIT(VSYNC_POSITIVE);

	drm_for_each_encoder_mask(encoder, crtc->dev, crtc_state->encoder_mask) {
		struct rockchip_encoder *rkencoder = to_rockchip_encoder(encoder);

		rk3568_set_intf_mux(vp, rkencoder->crtc_endpoint_id, polflags);
	}

	if (vcstate->output_mode == ROCKCHIP_OUT_MODE_AAAA &&
	    !(vp_data->feature & VOP_FEATURE_OUTPUT_10BIT))
		out_mode = ROCKCHIP_OUT_MODE_P888;
	else
		out_mode = vcstate->output_mode;

	dsp_ctrl |= FIELD_PREP(RK3568_VP_DSP_CTRL__OUT_MODE, out_mode);

	if (vop2_output_uv_swap(vcstate->bus_format, vcstate->output_mode))
		dsp_ctrl |= RK3568_VP_DSP_CTRL__DSP_RB_SWAP;

	if (is_yuv_output(vcstate->bus_format))
		dsp_ctrl |= RK3568_VP_DSP_CTRL__POST_DSP_OUT_R2Y;

	vop2_dither_setup(crtc, &dsp_ctrl);

	vop2_vp_write(vp, RK3568_VP_DSP_HTOTAL_HS_END, (htotal << 16) | hsync_len);
	val = hact_st << 16;
	val |= hact_end;
	vop2_vp_write(vp, RK3568_VP_DSP_HACT_ST_END, val);

	val = vact_st << 16;
	val |= vact_end;
	vop2_vp_write(vp, RK3568_VP_DSP_VACT_ST_END, val);

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		u16 vact_st_f1 = vtotal + vact_st + 1;
		u16 vact_end_f1 = vact_st_f1 + vdisplay;

		val = vact_st_f1 << 16 | vact_end_f1;
		vop2_vp_write(vp, RK3568_VP_DSP_VACT_ST_END_F1, val);

		val = vtotal << 16 | (vtotal + vsync_len);
		vop2_vp_write(vp, RK3568_VP_DSP_VS_ST_END_F1, val);
		dsp_ctrl |= RK3568_VP_DSP_CTRL__DSP_INTERLACE;
		dsp_ctrl |= RK3568_VP_DSP_CTRL__DSP_FILED_POL;
		dsp_ctrl |= RK3568_VP_DSP_CTRL__P2I_EN;
		vtotal += vtotal + 1;
		act_end = vact_end_f1;
	} else {
		act_end = vact_end;
	}

	vop2_writel(vop2, RK3568_VP_LINE_FLAG(vp->id),
		    (act_end - us_to_vertical_line(mode, 0)) << 16 | act_end);

	vop2_vp_write(vp, RK3568_VP_DSP_VTOTAL_VS_END, vtotal << 16 | vsync_len);

	if (mode->flags & DRM_MODE_FLAG_DBLCLK) {
		dsp_ctrl |= RK3568_VP_DSP_CTRL__CORE_DCLK_DIV;
		clock *= 2;
	}

	vop2_vp_write(vp, RK3568_VP_MIPI_CTRL, 0);

	clk_set_rate(vp->dclk, clock);

	vop2_post_config(crtc);

	vop2_cfg_done(vp);

	vop2_vp_write(vp, RK3568_VP_DSP_CTRL, dsp_ctrl);

	drm_crtc_vblank_on(crtc);

	vop2_unlock(vop2);
}

static int vop2_crtc_atomic_check(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_plane *plane;
	int nplanes = 0;
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	drm_atomic_crtc_state_for_each_plane(plane, crtc_state)
		nplanes++;

	if (nplanes > vp->nlayers)
		return -EINVAL;

	return 0;
}

static bool is_opaque(u16 alpha)
{
	return (alpha >> 8) == 0xff;
}

static void vop2_parse_alpha(struct vop2_alpha_config *alpha_config,
			     struct vop2_alpha *alpha)
{
	int src_glb_alpha_en = is_opaque(alpha_config->src_glb_alpha_value) ? 0 : 1;
	int dst_glb_alpha_en = is_opaque(alpha_config->dst_glb_alpha_value) ? 0 : 1;
	int src_color_mode = alpha_config->src_premulti_en ?
				ALPHA_SRC_PRE_MUL : ALPHA_SRC_NO_PRE_MUL;
	int dst_color_mode = alpha_config->dst_premulti_en ?
				ALPHA_SRC_PRE_MUL : ALPHA_SRC_NO_PRE_MUL;

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
	alpha->src_color_ctrl.bits.glb_alpha = alpha_config->src_glb_alpha_value >> 8;
	alpha->src_color_ctrl.bits.alpha_mode = ALPHA_STRAIGHT;
	alpha->src_color_ctrl.bits.alpha_cal_mode = ALPHA_SATURATION;

	alpha->dst_color_ctrl.bits.alpha_mode = ALPHA_STRAIGHT;
	alpha->dst_color_ctrl.bits.alpha_cal_mode = ALPHA_SATURATION;
	alpha->dst_color_ctrl.bits.blend_mode = ALPHA_GLOBAL;
	alpha->dst_color_ctrl.bits.glb_alpha = alpha_config->dst_glb_alpha_value >> 8;
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

static int vop2_find_start_mixer_id_for_vp(struct vop2 *vop2, u8 port_id)
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

static void vop2_setup_cluster_alpha(struct vop2 *vop2, struct vop2_win *main_win)
{
	u32 offset = (main_win->data->phys_id * 0x10);
	struct vop2_alpha_config alpha_config;
	struct vop2_alpha alpha;
	struct drm_plane_state *bottom_win_pstate;
	bool src_pixel_alpha_en = false;
	u16 src_glb_alpha_val, dst_glb_alpha_val;
	bool premulti_en = false;
	bool swap = false;

	/* At one win mode, win0 is dst/bottom win, and win1 is a all zero src/top win */
	bottom_win_pstate = main_win->base.state;
	src_glb_alpha_val = 0;
	dst_glb_alpha_val = main_win->base.state->alpha;

	if (!bottom_win_pstate->fb)
		return;

	alpha_config.src_premulti_en = premulti_en;
	alpha_config.dst_premulti_en = false;
	alpha_config.src_pixel_alpha_en = src_pixel_alpha_en;
	alpha_config.dst_pixel_alpha_en = true; /* alpha value need transfer to next mix */
	alpha_config.src_glb_alpha_value = src_glb_alpha_val;
	alpha_config.dst_glb_alpha_value = dst_glb_alpha_val;
	vop2_parse_alpha(&alpha_config, &alpha);

	alpha.src_color_ctrl.bits.src_dst_swap = swap;
	vop2_writel(vop2, RK3568_CLUSTER0_MIX_SRC_COLOR_CTRL + offset,
		    alpha.src_color_ctrl.val);
	vop2_writel(vop2, RK3568_CLUSTER0_MIX_DST_COLOR_CTRL + offset,
		    alpha.dst_color_ctrl.val);
	vop2_writel(vop2, RK3568_CLUSTER0_MIX_SRC_ALPHA_CTRL + offset,
		    alpha.src_alpha_ctrl.val);
	vop2_writel(vop2, RK3568_CLUSTER0_MIX_DST_ALPHA_CTRL + offset,
		    alpha.dst_alpha_ctrl.val);
}

static void vop2_setup_alpha(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	struct drm_framebuffer *fb;
	struct vop2_alpha_config alpha_config;
	struct vop2_alpha alpha;
	struct drm_plane *plane;
	int pixel_alpha_en;
	int premulti_en, gpremulti_en = 0;
	int mixer_id;
	u32 offset;
	bool bottom_layer_alpha_en = false;
	u32 dst_global_alpha = DRM_BLEND_ALPHA_OPAQUE;

	mixer_id = vop2_find_start_mixer_id_for_vp(vop2, vp->id);
	alpha_config.dst_pixel_alpha_en = true; /* alpha value need transfer to next mix */

	drm_atomic_crtc_for_each_plane(plane, &vp->crtc) {
		struct vop2_win *win = to_vop2_win(plane);

		if (plane->state->normalized_zpos == 0 &&
		    !is_opaque(plane->state->alpha) &&
		    !vop2_cluster_window(win)) {
			/*
			 * If bottom layer have global alpha effect [except cluster layer,
			 * because cluster have deal with bottom layer global alpha value
			 * at cluster mix], bottom layer mix need deal with global alpha.
			 */
			bottom_layer_alpha_en = true;
			dst_global_alpha = plane->state->alpha;
		}
	}

	drm_atomic_crtc_for_each_plane(plane, &vp->crtc) {
		struct vop2_win *win = to_vop2_win(plane);
		int zpos = plane->state->normalized_zpos;

		if (plane->state->pixel_blend_mode == DRM_MODE_BLEND_PREMULTI)
			premulti_en = 1;
		else
			premulti_en = 0;

		plane = &win->base;
		fb = plane->state->fb;

		pixel_alpha_en = fb->format->has_alpha;

		alpha_config.src_premulti_en = premulti_en;

		if (bottom_layer_alpha_en && zpos == 1) {
			gpremulti_en = premulti_en;
			/* Cd = Cs + (1 - As) * Cd * Agd */
			alpha_config.dst_premulti_en = false;
			alpha_config.src_pixel_alpha_en = pixel_alpha_en;
			alpha_config.src_glb_alpha_value = plane->state->alpha;
			alpha_config.dst_glb_alpha_value = dst_global_alpha;
		} else if (vop2_cluster_window(win)) {
			/* Mix output data only have pixel alpha */
			alpha_config.dst_premulti_en = true;
			alpha_config.src_pixel_alpha_en = true;
			alpha_config.src_glb_alpha_value = DRM_BLEND_ALPHA_OPAQUE;
			alpha_config.dst_glb_alpha_value = DRM_BLEND_ALPHA_OPAQUE;
		} else {
			/* Cd = Cs + (1 - As) * Cd */
			alpha_config.dst_premulti_en = true;
			alpha_config.src_pixel_alpha_en = pixel_alpha_en;
			alpha_config.src_glb_alpha_value = plane->state->alpha;
			alpha_config.dst_glb_alpha_value = DRM_BLEND_ALPHA_OPAQUE;
		}

		vop2_parse_alpha(&alpha_config, &alpha);

		offset = (mixer_id + zpos - 1) * 0x10;
		vop2_writel(vop2, RK3568_MIX0_SRC_COLOR_CTRL + offset,
			    alpha.src_color_ctrl.val);
		vop2_writel(vop2, RK3568_MIX0_DST_COLOR_CTRL + offset,
			    alpha.dst_color_ctrl.val);
		vop2_writel(vop2, RK3568_MIX0_SRC_ALPHA_CTRL + offset,
			    alpha.src_alpha_ctrl.val);
		vop2_writel(vop2, RK3568_MIX0_DST_ALPHA_CTRL + offset,
			    alpha.dst_alpha_ctrl.val);
	}

	if (vp->id == 0) {
		if (bottom_layer_alpha_en) {
			/* Transfer pixel alpha to hdr mix */
			alpha_config.src_premulti_en = gpremulti_en;
			alpha_config.dst_premulti_en = true;
			alpha_config.src_pixel_alpha_en = true;
			alpha_config.src_glb_alpha_value = DRM_BLEND_ALPHA_OPAQUE;
			alpha_config.dst_glb_alpha_value = DRM_BLEND_ALPHA_OPAQUE;
			vop2_parse_alpha(&alpha_config, &alpha);

			vop2_writel(vop2, RK3568_HDR0_SRC_COLOR_CTRL,
				    alpha.src_color_ctrl.val);
			vop2_writel(vop2, RK3568_HDR0_DST_COLOR_CTRL,
				    alpha.dst_color_ctrl.val);
			vop2_writel(vop2, RK3568_HDR0_SRC_ALPHA_CTRL,
				    alpha.src_alpha_ctrl.val);
			vop2_writel(vop2, RK3568_HDR0_DST_ALPHA_CTRL,
				    alpha.dst_alpha_ctrl.val);
		} else {
			vop2_writel(vop2, RK3568_HDR0_SRC_COLOR_CTRL, 0);
		}
	}
}

static void vop2_setup_layer_mixer(struct vop2_video_port *vp)
{
	struct vop2 *vop2 = vp->vop2;
	struct drm_plane *plane;
	u32 layer_sel = 0;
	u32 port_sel;
	unsigned int nlayer, ofs;
	struct drm_display_mode *adjusted_mode;
	u16 hsync_len;
	u16 hdisplay;
	u32 bg_dly;
	u32 pre_scan_dly;
	int i;
	struct vop2_video_port *vp0 = &vop2->vps[0];
	struct vop2_video_port *vp1 = &vop2->vps[1];
	struct vop2_video_port *vp2 = &vop2->vps[2];

	adjusted_mode = &vp->crtc.state->adjusted_mode;
	hsync_len = adjusted_mode->crtc_hsync_end - adjusted_mode->crtc_hsync_start;
	hdisplay = adjusted_mode->crtc_hdisplay;

	bg_dly = vp->data->pre_scan_max_dly[3];
	vop2_writel(vop2, RK3568_VP_BG_MIX_CTRL(vp->id),
		    FIELD_PREP(RK3568_VP_BG_MIX_CTRL__BG_DLY, bg_dly));

	pre_scan_dly = ((bg_dly + (hdisplay >> 1) - 1) << 16) | hsync_len;
	vop2_vp_write(vp, RK3568_VP_PRE_SCAN_HTIMING, pre_scan_dly);

	vop2_writel(vop2, RK3568_OVL_CTRL, 0);
	port_sel = vop2_readl(vop2, RK3568_OVL_PORT_SEL);
	port_sel &= RK3568_OVL_PORT_SEL__SEL_PORT;

	if (vp0->nlayers)
		port_sel |= FIELD_PREP(RK3568_OVL_PORT_SET__PORT0_MUX,
				     vp0->nlayers - 1);
	else
		port_sel |= FIELD_PREP(RK3568_OVL_PORT_SET__PORT0_MUX, 8);

	if (vp1->nlayers)
		port_sel |= FIELD_PREP(RK3568_OVL_PORT_SET__PORT1_MUX,
				     (vp0->nlayers + vp1->nlayers - 1));
	else
		port_sel |= FIELD_PREP(RK3568_OVL_PORT_SET__PORT1_MUX, 8);

	if (vp2->nlayers)
		port_sel |= FIELD_PREP(RK3568_OVL_PORT_SET__PORT2_MUX,
			(vp2->nlayers + vp1->nlayers + vp0->nlayers - 1));
	else
		port_sel |= FIELD_PREP(RK3568_OVL_PORT_SET__PORT1_MUX, 8);

	layer_sel = vop2_readl(vop2, RK3568_OVL_LAYER_SEL);

	ofs = 0;
	for (i = 0; i < vp->id; i++)
		ofs += vop2->vps[i].nlayers;

	nlayer = 0;
	drm_atomic_crtc_for_each_plane(plane, &vp->crtc) {
		struct vop2_win *win = to_vop2_win(plane);

		switch (win->data->phys_id) {
		case ROCKCHIP_VOP2_CLUSTER0:
			port_sel &= ~RK3568_OVL_PORT_SEL__CLUSTER0;
			port_sel |= FIELD_PREP(RK3568_OVL_PORT_SEL__CLUSTER0, vp->id);
			break;
		case ROCKCHIP_VOP2_CLUSTER1:
			port_sel &= ~RK3568_OVL_PORT_SEL__CLUSTER1;
			port_sel |= FIELD_PREP(RK3568_OVL_PORT_SEL__CLUSTER1, vp->id);
			break;
		case ROCKCHIP_VOP2_ESMART0:
			port_sel &= ~RK3568_OVL_PORT_SEL__ESMART0;
			port_sel |= FIELD_PREP(RK3568_OVL_PORT_SEL__ESMART0, vp->id);
			break;
		case ROCKCHIP_VOP2_ESMART1:
			port_sel &= ~RK3568_OVL_PORT_SEL__ESMART1;
			port_sel |= FIELD_PREP(RK3568_OVL_PORT_SEL__ESMART1, vp->id);
			break;
		case ROCKCHIP_VOP2_SMART0:
			port_sel &= ~RK3568_OVL_PORT_SEL__SMART0;
			port_sel |= FIELD_PREP(RK3568_OVL_PORT_SEL__SMART0, vp->id);
			break;
		case ROCKCHIP_VOP2_SMART1:
			port_sel &= ~RK3568_OVL_PORT_SEL__SMART1;
			port_sel |= FIELD_PREP(RK3568_OVL_PORT_SEL__SMART1, vp->id);
			break;
		}

		layer_sel &= ~RK3568_OVL_LAYER_SEL__LAYER(plane->state->normalized_zpos + ofs,
							  0x7);
		layer_sel |= RK3568_OVL_LAYER_SEL__LAYER(plane->state->normalized_zpos + ofs,
							 win->data->layer_sel_id);
		nlayer++;
	}

	/* configure unused layers to 0x5 (reserved) */
	for (; nlayer < vp->nlayers; nlayer++) {
		layer_sel &= ~RK3568_OVL_LAYER_SEL__LAYER(nlayer + ofs, 0x7);
		layer_sel |= RK3568_OVL_LAYER_SEL__LAYER(nlayer + ofs, 5);
	}

	vop2_writel(vop2, RK3568_OVL_LAYER_SEL, layer_sel);
	vop2_writel(vop2, RK3568_OVL_PORT_SEL, port_sel);
	vop2_writel(vop2, RK3568_OVL_CTRL, RK3568_OVL_CTRL__LAYERSEL_REGDONE_IMD);
}

static void vop2_setup_dly_for_windows(struct vop2 *vop2)
{
	struct vop2_win *win;
	int i = 0;
	u32 cdly = 0, sdly = 0;

	for (i = 0; i < vop2->data->win_size; i++) {
		u32 dly;

		win = &vop2->win[i];
		dly = win->delay;

		switch (win->data->phys_id) {
		case ROCKCHIP_VOP2_CLUSTER0:
			cdly |= FIELD_PREP(RK3568_CLUSTER_DLY_NUM__CLUSTER0_0, dly);
			cdly |= FIELD_PREP(RK3568_CLUSTER_DLY_NUM__CLUSTER0_1, dly);
			break;
		case ROCKCHIP_VOP2_CLUSTER1:
			cdly |= FIELD_PREP(RK3568_CLUSTER_DLY_NUM__CLUSTER1_0, dly);
			cdly |= FIELD_PREP(RK3568_CLUSTER_DLY_NUM__CLUSTER1_1, dly);
			break;
		case ROCKCHIP_VOP2_ESMART0:
			sdly |= FIELD_PREP(RK3568_SMART_DLY_NUM__ESMART0, dly);
			break;
		case ROCKCHIP_VOP2_ESMART1:
			sdly |= FIELD_PREP(RK3568_SMART_DLY_NUM__ESMART1, dly);
			break;
		case ROCKCHIP_VOP2_SMART0:
			sdly |= FIELD_PREP(RK3568_SMART_DLY_NUM__SMART0, dly);
			break;
		case ROCKCHIP_VOP2_SMART1:
			sdly |= FIELD_PREP(RK3568_SMART_DLY_NUM__SMART1, dly);
			break;
		}
	}

	vop2_writel(vop2, RK3568_CLUSTER_DLY_NUM, cdly);
	vop2_writel(vop2, RK3568_SMART_DLY_NUM, sdly);
}

static void vop2_crtc_atomic_begin(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;
	struct drm_plane *plane;

	vp->win_mask = 0;

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		struct vop2_win *win = to_vop2_win(plane);

		win->delay = win->data->dly[VOP2_DLY_MODE_DEFAULT];

		vp->win_mask |= BIT(win->data->phys_id);

		if (vop2_cluster_window(win))
			vop2_setup_cluster_alpha(vop2, win);
	}

	if (!vp->win_mask)
		return;

	vop2_setup_layer_mixer(vp);
	vop2_setup_alpha(vp);
	vop2_setup_dly_for_windows(vop2);
}

static void vop2_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);

	vop2_post_config(crtc);

	vop2_cfg_done(vp);

	spin_lock_irq(&crtc->dev->event_lock);

	if (crtc->state->event) {
		WARN_ON(drm_crtc_vblank_get(crtc));
		vp->event = crtc->state->event;
		crtc->state->event = NULL;
	}

	spin_unlock_irq(&crtc->dev->event_lock);
}

static const struct drm_crtc_helper_funcs vop2_crtc_helper_funcs = {
	.mode_fixup = vop2_crtc_mode_fixup,
	.atomic_check = vop2_crtc_atomic_check,
	.atomic_begin = vop2_crtc_atomic_begin,
	.atomic_flush = vop2_crtc_atomic_flush,
	.atomic_enable = vop2_crtc_atomic_enable,
	.atomic_disable = vop2_crtc_atomic_disable,
};

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

static const struct drm_crtc_funcs vop2_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.destroy = drm_crtc_cleanup,
	.reset = vop2_crtc_reset,
	.atomic_duplicate_state = vop2_crtc_duplicate_state,
	.atomic_destroy_state = vop2_crtc_destroy_state,
	.enable_vblank = vop2_crtc_enable_vblank,
	.disable_vblank = vop2_crtc_disable_vblank,
};

static irqreturn_t vop2_isr(int irq, void *data)
{
	struct vop2 *vop2 = data;
	const struct vop2_data *vop2_data = vop2->data;
	u32 axi_irqs[VOP2_SYS_AXI_BUS_NUM];
	int ret = IRQ_NONE;
	int i;

	/*
	 * The irq is shared with the iommu. If the runtime-pm state of the
	 * vop2-device is disabled the irq has to be targeted at the iommu.
	 */
	if (!pm_runtime_get_if_in_use(vop2->dev))
		return IRQ_NONE;

	for (i = 0; i < vop2_data->nr_vps; i++) {
		struct vop2_video_port *vp = &vop2->vps[i];
		struct drm_crtc *crtc = &vp->crtc;
		u32 irqs;

		irqs = vop2_readl(vop2, RK3568_VP_INT_STATUS(vp->id));
		vop2_writel(vop2, RK3568_VP_INT_CLR(vp->id), irqs << 16 | irqs);

		if (irqs & VP_INT_DSP_HOLD_VALID) {
			complete(&vp->dsp_hold_completion);
			ret = IRQ_HANDLED;
		}

		if (irqs & VP_INT_FS_FIELD) {
			drm_crtc_handle_vblank(crtc);
			spin_lock(&crtc->dev->event_lock);
			if (vp->event) {
				u32 val = vop2_readl(vop2, RK3568_REG_CFG_DONE);

				if (!(val & BIT(vp->id))) {
					drm_crtc_send_vblank_event(crtc, vp->event);
					vp->event = NULL;
					drm_crtc_vblank_put(crtc);
				}
			}
			spin_unlock(&crtc->dev->event_lock);

			ret = IRQ_HANDLED;
		}

		if (irqs & VP_INT_POST_BUF_EMPTY) {
			drm_err_ratelimited(vop2->drm,
					    "POST_BUF_EMPTY irq err at vp%d\n",
					    vp->id);
			ret = IRQ_HANDLED;
		}
	}

	axi_irqs[0] = vop2_readl(vop2, RK3568_SYS0_INT_STATUS);
	vop2_writel(vop2, RK3568_SYS0_INT_CLR, axi_irqs[0] << 16 | axi_irqs[0]);
	axi_irqs[1] = vop2_readl(vop2, RK3568_SYS1_INT_STATUS);
	vop2_writel(vop2, RK3568_SYS1_INT_CLR, axi_irqs[1] << 16 | axi_irqs[1]);

	for (i = 0; i < ARRAY_SIZE(axi_irqs); i++) {
		if (axi_irqs[i] & VOP2_INT_BUS_ERRPR) {
			drm_err_ratelimited(vop2->drm, "BUS_ERROR irq err\n");
			ret = IRQ_HANDLED;
		}
	}

	pm_runtime_put(vop2->dev);

	return ret;
}

static int vop2_plane_init(struct vop2 *vop2, struct vop2_win *win,
			   unsigned long possible_crtcs)
{
	const struct vop2_win_data *win_data = win->data;
	unsigned int blend_caps = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
				  BIT(DRM_MODE_BLEND_PREMULTI) |
				  BIT(DRM_MODE_BLEND_COVERAGE);
	int ret;

	ret = drm_universal_plane_init(vop2->drm, &win->base, possible_crtcs,
				       &vop2_plane_funcs, win_data->formats,
				       win_data->nformats,
				       win_data->format_modifiers,
				       win->type, win_data->name);
	if (ret) {
		drm_err(vop2->drm, "failed to initialize plane %d\n", ret);
		return ret;
	}

	drm_plane_helper_add(&win->base, &vop2_plane_helper_funcs);

	if (win->data->supported_rotations)
		drm_plane_create_rotation_property(&win->base, DRM_MODE_ROTATE_0,
						   DRM_MODE_ROTATE_0 |
						   win->data->supported_rotations);
	drm_plane_create_alpha_property(&win->base);
	drm_plane_create_blend_mode_property(&win->base, blend_caps);
	drm_plane_create_zpos_property(&win->base, win->win_id, 0,
				       vop2->registered_num_wins - 1);

	return 0;
}

static struct vop2_video_port *find_vp_without_primary(struct vop2 *vop2)
{
	int i;

	for (i = 0; i < vop2->data->nr_vps; i++) {
		struct vop2_video_port *vp = &vop2->vps[i];

		if (!vp->crtc.port)
			continue;
		if (vp->primary_plane)
			continue;

		return vp;
	}

	return NULL;
}

#define NR_LAYERS 6

static int vop2_create_crtcs(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	struct drm_device *drm = vop2->drm;
	struct device *dev = vop2->dev;
	struct drm_plane *plane;
	struct device_node *port;
	struct vop2_video_port *vp;
	int i, nvp, nvps = 0;
	int ret;

	for (i = 0; i < vop2_data->nr_vps; i++) {
		const struct vop2_video_port_data *vp_data;
		struct device_node *np;
		char dclk_name[9];

		vp_data = &vop2_data->vp[i];
		vp = &vop2->vps[i];
		vp->vop2 = vop2;
		vp->id = vp_data->id;
		vp->regs = vp_data->regs;
		vp->data = vp_data;

		snprintf(dclk_name, sizeof(dclk_name), "dclk_vp%d", vp->id);
		vp->dclk = devm_clk_get(vop2->dev, dclk_name);
		if (IS_ERR(vp->dclk)) {
			drm_err(vop2->drm, "failed to get %s\n", dclk_name);
			return PTR_ERR(vp->dclk);
		}

		np = of_graph_get_remote_node(dev->of_node, i, -1);
		if (!np) {
			drm_dbg(vop2->drm, "%s: No remote for vp%d\n", __func__, i);
			continue;
		}
		of_node_put(np);

		port = of_graph_get_port_by_id(dev->of_node, i);
		if (!port) {
			drm_err(vop2->drm, "no port node found for video_port%d\n", i);
			return -ENOENT;
		}

		vp->crtc.port = port;
		nvps++;
	}

	nvp = 0;
	for (i = 0; i < vop2->registered_num_wins; i++) {
		struct vop2_win *win = &vop2->win[i];
		u32 possible_crtcs = 0;

		if (vop2->data->soc_id == 3566) {
			/*
			 * On RK3566 these windows don't have an independent
			 * framebuffer. They share the framebuffer with smart0,
			 * esmart0 and cluster0 respectively.
			 */
			switch (win->data->phys_id) {
			case ROCKCHIP_VOP2_SMART1:
			case ROCKCHIP_VOP2_ESMART1:
			case ROCKCHIP_VOP2_CLUSTER1:
				continue;
			}
		}

		if (win->type == DRM_PLANE_TYPE_PRIMARY) {
			vp = find_vp_without_primary(vop2);
			if (vp) {
				possible_crtcs = BIT(nvp);
				vp->primary_plane = win;
				nvp++;
			} else {
				/* change the unused primary window to overlay window */
				win->type = DRM_PLANE_TYPE_OVERLAY;
			}
		}

		if (win->type == DRM_PLANE_TYPE_OVERLAY)
			possible_crtcs = (1 << nvps) - 1;

		ret = vop2_plane_init(vop2, win, possible_crtcs);
		if (ret) {
			drm_err(vop2->drm, "failed to init plane %s: %d\n",
				win->data->name, ret);
			return ret;
		}
	}

	for (i = 0; i < vop2_data->nr_vps; i++) {
		vp = &vop2->vps[i];

		if (!vp->crtc.port)
			continue;

		plane = &vp->primary_plane->base;

		ret = drm_crtc_init_with_planes(drm, &vp->crtc, plane, NULL,
						&vop2_crtc_funcs,
						"video_port%d", vp->id);
		if (ret) {
			drm_err(vop2->drm, "crtc init for video_port%d failed\n", i);
			return ret;
		}

		drm_crtc_helper_add(&vp->crtc, &vop2_crtc_helper_funcs);

		init_completion(&vp->dsp_hold_completion);
	}

	/*
	 * On the VOP2 it's very hard to change the number of layers on a VP
	 * during runtime, so we distribute the layers equally over the used
	 * VPs
	 */
	for (i = 0; i < vop2->data->nr_vps; i++) {
		struct vop2_video_port *vp = &vop2->vps[i];

		if (vp->crtc.port)
			vp->nlayers = NR_LAYERS / nvps;
	}

	return 0;
}

static void vop2_destroy_crtcs(struct vop2 *vop2)
{
	struct drm_device *drm = vop2->drm;
	struct list_head *crtc_list = &drm->mode_config.crtc_list;
	struct list_head *plane_list = &drm->mode_config.plane_list;
	struct drm_crtc *crtc, *tmpc;
	struct drm_plane *plane, *tmpp;

	list_for_each_entry_safe(plane, tmpp, plane_list, head)
		drm_plane_cleanup(plane);

	/*
	 * Destroy CRTC after vop2_plane_destroy() since vop2_disable_plane()
	 * references the CRTC.
	 */
	list_for_each_entry_safe(crtc, tmpc, crtc_list, head) {
		of_node_put(crtc->port);
		drm_crtc_cleanup(crtc);
	}
}

static int vop2_find_rgb_encoder(struct vop2 *vop2)
{
	struct device_node *node = vop2->dev->of_node;
	struct device_node *endpoint;
	int i;

	for (i = 0; i < vop2->data->nr_vps; i++) {
		endpoint = of_graph_get_endpoint_by_regs(node, i,
							 ROCKCHIP_VOP2_EP_RGB0);
		if (!endpoint)
			continue;

		of_node_put(endpoint);
		return i;
	}

	return -ENOENT;
}

static struct reg_field vop2_cluster_regs[VOP2_WIN_MAX_REG] = {
	[VOP2_WIN_ENABLE] = REG_FIELD(RK3568_CLUSTER_WIN_CTRL0, 0, 0),
	[VOP2_WIN_FORMAT] = REG_FIELD(RK3568_CLUSTER_WIN_CTRL0, 1, 5),
	[VOP2_WIN_RB_SWAP] = REG_FIELD(RK3568_CLUSTER_WIN_CTRL0, 14, 14),
	[VOP2_WIN_DITHER_UP] = REG_FIELD(RK3568_CLUSTER_WIN_CTRL0, 18, 18),
	[VOP2_WIN_ACT_INFO] = REG_FIELD(RK3568_CLUSTER_WIN_ACT_INFO, 0, 31),
	[VOP2_WIN_DSP_INFO] = REG_FIELD(RK3568_CLUSTER_WIN_DSP_INFO, 0, 31),
	[VOP2_WIN_DSP_ST] = REG_FIELD(RK3568_CLUSTER_WIN_DSP_ST, 0, 31),
	[VOP2_WIN_YRGB_MST] = REG_FIELD(RK3568_CLUSTER_WIN_YRGB_MST, 0, 31),
	[VOP2_WIN_UV_MST] = REG_FIELD(RK3568_CLUSTER_WIN_CBR_MST, 0, 31),
	[VOP2_WIN_YUV_CLIP] = REG_FIELD(RK3568_CLUSTER_WIN_CTRL0, 19, 19),
	[VOP2_WIN_YRGB_VIR] = REG_FIELD(RK3568_CLUSTER_WIN_VIR, 0, 15),
	[VOP2_WIN_UV_VIR] = REG_FIELD(RK3568_CLUSTER_WIN_VIR, 16, 31),
	[VOP2_WIN_Y2R_EN] = REG_FIELD(RK3568_CLUSTER_WIN_CTRL0, 8, 8),
	[VOP2_WIN_R2Y_EN] = REG_FIELD(RK3568_CLUSTER_WIN_CTRL0, 9, 9),
	[VOP2_WIN_CSC_MODE] = REG_FIELD(RK3568_CLUSTER_WIN_CTRL0, 10, 11),

	/* Scale */
	[VOP2_WIN_SCALE_YRGB_X] = REG_FIELD(RK3568_CLUSTER_WIN_SCL_FACTOR_YRGB, 0, 15),
	[VOP2_WIN_SCALE_YRGB_Y] = REG_FIELD(RK3568_CLUSTER_WIN_SCL_FACTOR_YRGB, 16, 31),
	[VOP2_WIN_YRGB_VER_SCL_MODE] = REG_FIELD(RK3568_CLUSTER_WIN_CTRL1, 14, 15),
	[VOP2_WIN_YRGB_HOR_SCL_MODE] = REG_FIELD(RK3568_CLUSTER_WIN_CTRL1, 12, 13),
	[VOP2_WIN_BIC_COE_SEL] = REG_FIELD(RK3568_CLUSTER_WIN_CTRL1, 2, 3),
	[VOP2_WIN_VSD_YRGB_GT2] = REG_FIELD(RK3568_CLUSTER_WIN_CTRL1, 28, 28),
	[VOP2_WIN_VSD_YRGB_GT4] = REG_FIELD(RK3568_CLUSTER_WIN_CTRL1, 29, 29),

	/* cluster regs */
	[VOP2_WIN_AFBC_ENABLE] = REG_FIELD(RK3568_CLUSTER_CTRL, 1, 1),
	[VOP2_WIN_CLUSTER_ENABLE] = REG_FIELD(RK3568_CLUSTER_CTRL, 0, 0),
	[VOP2_WIN_CLUSTER_LB_MODE] = REG_FIELD(RK3568_CLUSTER_CTRL, 4, 7),

	/* afbc regs */
	[VOP2_WIN_AFBC_FORMAT] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_CTRL, 2, 6),
	[VOP2_WIN_AFBC_RB_SWAP] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_CTRL, 9, 9),
	[VOP2_WIN_AFBC_UV_SWAP] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_CTRL, 10, 10),
	[VOP2_WIN_AFBC_AUTO_GATING_EN] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_OUTPUT_CTRL, 4, 4),
	[VOP2_WIN_AFBC_HALF_BLOCK_EN] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_CTRL, 7, 7),
	[VOP2_WIN_AFBC_BLOCK_SPLIT_EN] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_CTRL, 8, 8),
	[VOP2_WIN_AFBC_HDR_PTR] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_HDR_PTR, 0, 31),
	[VOP2_WIN_AFBC_PIC_SIZE] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_PIC_SIZE, 0, 31),
	[VOP2_WIN_AFBC_PIC_VIR_WIDTH] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_VIR_WIDTH, 0, 15),
	[VOP2_WIN_AFBC_TILE_NUM] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_VIR_WIDTH, 16, 31),
	[VOP2_WIN_AFBC_PIC_OFFSET] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_PIC_OFFSET, 0, 31),
	[VOP2_WIN_AFBC_DSP_OFFSET] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_DSP_OFFSET, 0, 31),
	[VOP2_WIN_AFBC_TRANSFORM_OFFSET] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_TRANSFORM_OFFSET, 0, 31),
	[VOP2_WIN_AFBC_ROTATE_90] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_ROTATE_MODE, 0, 0),
	[VOP2_WIN_AFBC_ROTATE_270] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_ROTATE_MODE, 1, 1),
	[VOP2_WIN_XMIRROR] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_ROTATE_MODE, 2, 2),
	[VOP2_WIN_YMIRROR] = REG_FIELD(RK3568_CLUSTER_WIN_AFBCD_ROTATE_MODE, 3, 3),
	[VOP2_WIN_UV_SWAP] = { .reg = 0xffffffff },
	[VOP2_WIN_COLOR_KEY] = { .reg = 0xffffffff },
	[VOP2_WIN_COLOR_KEY_EN] = { .reg = 0xffffffff },
	[VOP2_WIN_SCALE_CBCR_X] = { .reg = 0xffffffff },
	[VOP2_WIN_SCALE_CBCR_Y] = { .reg = 0xffffffff },
	[VOP2_WIN_YRGB_HSCL_FILTER_MODE] = { .reg = 0xffffffff },
	[VOP2_WIN_YRGB_VSCL_FILTER_MODE] = { .reg = 0xffffffff },
	[VOP2_WIN_CBCR_VER_SCL_MODE] = { .reg = 0xffffffff },
	[VOP2_WIN_CBCR_HSCL_FILTER_MODE] = { .reg = 0xffffffff },
	[VOP2_WIN_CBCR_HOR_SCL_MODE] = { .reg = 0xffffffff },
	[VOP2_WIN_CBCR_VSCL_FILTER_MODE] = { .reg = 0xffffffff },
	[VOP2_WIN_VSD_CBCR_GT2] = { .reg = 0xffffffff },
	[VOP2_WIN_VSD_CBCR_GT4] = { .reg = 0xffffffff },
};

static int vop2_cluster_init(struct vop2_win *win)
{
	struct vop2 *vop2 = win->vop2;
	struct reg_field *cluster_regs;
	int ret, i;

	cluster_regs = kmemdup(vop2_cluster_regs, sizeof(vop2_cluster_regs),
			       GFP_KERNEL);
	if (!cluster_regs)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(vop2_cluster_regs); i++)
		if (cluster_regs[i].reg != 0xffffffff)
			cluster_regs[i].reg += win->offset;

	ret = devm_regmap_field_bulk_alloc(vop2->dev, vop2->map, win->reg,
					   cluster_regs,
					   ARRAY_SIZE(vop2_cluster_regs));

	kfree(cluster_regs);

	return ret;
};

static struct reg_field vop2_esmart_regs[VOP2_WIN_MAX_REG] = {
	[VOP2_WIN_ENABLE] = REG_FIELD(RK3568_SMART_REGION0_CTRL, 0, 0),
	[VOP2_WIN_FORMAT] = REG_FIELD(RK3568_SMART_REGION0_CTRL, 1, 5),
	[VOP2_WIN_DITHER_UP] = REG_FIELD(RK3568_SMART_REGION0_CTRL, 12, 12),
	[VOP2_WIN_RB_SWAP] = REG_FIELD(RK3568_SMART_REGION0_CTRL, 14, 14),
	[VOP2_WIN_UV_SWAP] = REG_FIELD(RK3568_SMART_REGION0_CTRL, 16, 16),
	[VOP2_WIN_ACT_INFO] = REG_FIELD(RK3568_SMART_REGION0_ACT_INFO, 0, 31),
	[VOP2_WIN_DSP_INFO] = REG_FIELD(RK3568_SMART_REGION0_DSP_INFO, 0, 31),
	[VOP2_WIN_DSP_ST] = REG_FIELD(RK3568_SMART_REGION0_DSP_ST, 0, 28),
	[VOP2_WIN_YRGB_MST] = REG_FIELD(RK3568_SMART_REGION0_YRGB_MST, 0, 31),
	[VOP2_WIN_UV_MST] = REG_FIELD(RK3568_SMART_REGION0_CBR_MST, 0, 31),
	[VOP2_WIN_YUV_CLIP] = REG_FIELD(RK3568_SMART_REGION0_CTRL, 17, 17),
	[VOP2_WIN_YRGB_VIR] = REG_FIELD(RK3568_SMART_REGION0_VIR, 0, 15),
	[VOP2_WIN_UV_VIR] = REG_FIELD(RK3568_SMART_REGION0_VIR, 16, 31),
	[VOP2_WIN_Y2R_EN] = REG_FIELD(RK3568_SMART_CTRL0, 0, 0),
	[VOP2_WIN_R2Y_EN] = REG_FIELD(RK3568_SMART_CTRL0, 1, 1),
	[VOP2_WIN_CSC_MODE] = REG_FIELD(RK3568_SMART_CTRL0, 2, 3),
	[VOP2_WIN_YMIRROR] = REG_FIELD(RK3568_SMART_CTRL1, 31, 31),
	[VOP2_WIN_COLOR_KEY] = REG_FIELD(RK3568_SMART_COLOR_KEY_CTRL, 0, 29),
	[VOP2_WIN_COLOR_KEY_EN] = REG_FIELD(RK3568_SMART_COLOR_KEY_CTRL, 31, 31),

	/* Scale */
	[VOP2_WIN_SCALE_YRGB_X] = REG_FIELD(RK3568_SMART_REGION0_SCL_FACTOR_YRGB, 0, 15),
	[VOP2_WIN_SCALE_YRGB_Y] = REG_FIELD(RK3568_SMART_REGION0_SCL_FACTOR_YRGB, 16, 31),
	[VOP2_WIN_SCALE_CBCR_X] = REG_FIELD(RK3568_SMART_REGION0_SCL_FACTOR_CBR, 0, 15),
	[VOP2_WIN_SCALE_CBCR_Y] = REG_FIELD(RK3568_SMART_REGION0_SCL_FACTOR_CBR, 16, 31),
	[VOP2_WIN_YRGB_HOR_SCL_MODE] = REG_FIELD(RK3568_SMART_REGION0_SCL_CTRL, 0, 1),
	[VOP2_WIN_YRGB_HSCL_FILTER_MODE] = REG_FIELD(RK3568_SMART_REGION0_SCL_CTRL, 2, 3),
	[VOP2_WIN_YRGB_VER_SCL_MODE] = REG_FIELD(RK3568_SMART_REGION0_SCL_CTRL, 4, 5),
	[VOP2_WIN_YRGB_VSCL_FILTER_MODE] = REG_FIELD(RK3568_SMART_REGION0_SCL_CTRL, 6, 7),
	[VOP2_WIN_CBCR_HOR_SCL_MODE] = REG_FIELD(RK3568_SMART_REGION0_SCL_CTRL, 8, 9),
	[VOP2_WIN_CBCR_HSCL_FILTER_MODE] = REG_FIELD(RK3568_SMART_REGION0_SCL_CTRL, 10, 11),
	[VOP2_WIN_CBCR_VER_SCL_MODE] = REG_FIELD(RK3568_SMART_REGION0_SCL_CTRL, 12, 13),
	[VOP2_WIN_CBCR_VSCL_FILTER_MODE] = REG_FIELD(RK3568_SMART_REGION0_SCL_CTRL, 14, 15),
	[VOP2_WIN_BIC_COE_SEL] = REG_FIELD(RK3568_SMART_REGION0_SCL_CTRL, 16, 17),
	[VOP2_WIN_VSD_YRGB_GT2] = REG_FIELD(RK3568_SMART_REGION0_CTRL, 8, 8),
	[VOP2_WIN_VSD_YRGB_GT4] = REG_FIELD(RK3568_SMART_REGION0_CTRL, 9, 9),
	[VOP2_WIN_VSD_CBCR_GT2] = REG_FIELD(RK3568_SMART_REGION0_CTRL, 10, 10),
	[VOP2_WIN_VSD_CBCR_GT4] = REG_FIELD(RK3568_SMART_REGION0_CTRL, 11, 11),
	[VOP2_WIN_XMIRROR] = { .reg = 0xffffffff },
	[VOP2_WIN_CLUSTER_ENABLE] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_ENABLE] = { .reg = 0xffffffff },
	[VOP2_WIN_CLUSTER_LB_MODE] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_FORMAT] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_RB_SWAP] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_UV_SWAP] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_AUTO_GATING_EN] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_BLOCK_SPLIT_EN] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_PIC_VIR_WIDTH] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_TILE_NUM] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_PIC_OFFSET] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_PIC_SIZE] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_DSP_OFFSET] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_TRANSFORM_OFFSET] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_HDR_PTR] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_HALF_BLOCK_EN] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_ROTATE_270] = { .reg = 0xffffffff },
	[VOP2_WIN_AFBC_ROTATE_90] = { .reg = 0xffffffff },
};

static int vop2_esmart_init(struct vop2_win *win)
{
	struct vop2 *vop2 = win->vop2;
	struct reg_field *esmart_regs;
	int ret, i;

	esmart_regs = kmemdup(vop2_esmart_regs, sizeof(vop2_esmart_regs),
			      GFP_KERNEL);
	if (!esmart_regs)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(vop2_esmart_regs); i++)
		if (esmart_regs[i].reg != 0xffffffff)
			esmart_regs[i].reg += win->offset;

	ret = devm_regmap_field_bulk_alloc(vop2->dev, vop2->map, win->reg,
					   esmart_regs,
					   ARRAY_SIZE(vop2_esmart_regs));

	kfree(esmart_regs);

	return ret;
};

static int vop2_win_init(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	struct vop2_win *win;
	int i, ret;

	for (i = 0; i < vop2_data->win_size; i++) {
		const struct vop2_win_data *win_data = &vop2_data->win[i];

		win = &vop2->win[i];
		win->data = win_data;
		win->type = win_data->type;
		win->offset = win_data->base;
		win->win_id = i;
		win->vop2 = vop2;
		if (vop2_cluster_window(win))
			ret = vop2_cluster_init(win);
		else
			ret = vop2_esmart_init(win);
		if (ret)
			return ret;
	}

	vop2->registered_num_wins = vop2_data->win_size;

	return 0;
}

/*
 * The window registers are only updated when config done is written.
 * Until that they read back the old value. As we read-modify-write
 * these registers mark them as non-volatile. This makes sure we read
 * the new values from the regmap register cache.
 */
static const struct regmap_range vop2_nonvolatile_range[] = {
	regmap_reg_range(0x1000, 0x23ff),
};

static const struct regmap_access_table vop2_volatile_table = {
	.no_ranges = vop2_nonvolatile_range,
	.n_no_ranges = ARRAY_SIZE(vop2_nonvolatile_range),
};

static const struct regmap_config vop2_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x3000,
	.name		= "vop2",
	.volatile_table	= &vop2_volatile_table,
	.cache_type	= REGCACHE_RBTREE,
};

static int vop2_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct vop2_data *vop2_data;
	struct drm_device *drm = data;
	struct vop2 *vop2;
	struct resource *res;
	size_t alloc_size;
	int ret;

	vop2_data = of_device_get_match_data(dev);
	if (!vop2_data)
		return -ENODEV;

	/* Allocate vop2 struct and its vop2_win array */
	alloc_size = struct_size(vop2, win, vop2_data->win_size);
	vop2 = devm_kzalloc(dev, alloc_size, GFP_KERNEL);
	if (!vop2)
		return -ENOMEM;

	vop2->dev = dev;
	vop2->data = vop2_data;
	vop2->drm = drm;

	dev_set_drvdata(dev, vop2);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vop");
	if (!res) {
		drm_err(vop2->drm, "failed to get vop2 register byname\n");
		return -EINVAL;
	}

	vop2->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(vop2->regs))
		return PTR_ERR(vop2->regs);
	vop2->len = resource_size(res);

	vop2->map = devm_regmap_init_mmio(dev, vop2->regs, &vop2_regmap_config);
	if (IS_ERR(vop2->map))
		return PTR_ERR(vop2->map);

	ret = vop2_win_init(vop2);
	if (ret)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gamma-lut");
	if (res) {
		vop2->lut_regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(vop2->lut_regs))
			return PTR_ERR(vop2->lut_regs);
	}

	vop2->grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");

	vop2->hclk = devm_clk_get(vop2->dev, "hclk");
	if (IS_ERR(vop2->hclk)) {
		drm_err(vop2->drm, "failed to get hclk source\n");
		return PTR_ERR(vop2->hclk);
	}

	vop2->aclk = devm_clk_get(vop2->dev, "aclk");
	if (IS_ERR(vop2->aclk)) {
		drm_err(vop2->drm, "failed to get aclk source\n");
		return PTR_ERR(vop2->aclk);
	}

	vop2->irq = platform_get_irq(pdev, 0);
	if (vop2->irq < 0) {
		drm_err(vop2->drm, "cannot find irq for vop2\n");
		return vop2->irq;
	}

	mutex_init(&vop2->vop2_lock);

	ret = devm_request_irq(dev, vop2->irq, vop2_isr, IRQF_SHARED, dev_name(dev), vop2);
	if (ret)
		return ret;

	ret = vop2_create_crtcs(vop2);
	if (ret)
		return ret;

	ret = vop2_find_rgb_encoder(vop2);
	if (ret >= 0) {
		vop2->rgb = rockchip_rgb_init(dev, &vop2->vps[ret].crtc,
					      vop2->drm, ret);
		if (IS_ERR(vop2->rgb)) {
			if (PTR_ERR(vop2->rgb) == -EPROBE_DEFER) {
				ret = PTR_ERR(vop2->rgb);
				goto err_crtcs;
			}
			vop2->rgb = NULL;
		}
	}

	rockchip_drm_dma_init_device(vop2->drm, vop2->dev);

	pm_runtime_enable(&pdev->dev);

	return 0;

err_crtcs:
	vop2_destroy_crtcs(vop2);

	return ret;
}

static void vop2_unbind(struct device *dev, struct device *master, void *data)
{
	struct vop2 *vop2 = dev_get_drvdata(dev);

	pm_runtime_disable(dev);

	if (vop2->rgb)
		rockchip_rgb_fini(vop2->rgb);

	vop2_destroy_crtcs(vop2);
}

const struct component_ops vop2_component_ops = {
	.bind = vop2_bind,
	.unbind = vop2_unbind,
};
EXPORT_SYMBOL_GPL(vop2_component_ops);
