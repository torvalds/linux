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
#include <linux/debugfs.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_flip_work.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include <uapi/linux/videodev2.h>

#include "rockchip_drm_gem.h"
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

#define VOP2_MAX_DCLK_RATE		600000000

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

static const struct regmap_config vop2_regmap_config;

static void vop2_lock(struct vop2 *vop2)
{
	mutex_lock(&vop2->vop2_lock);
}

static void vop2_unlock(struct vop2 *vop2)
{
	mutex_unlock(&vop2->vop2_lock);
}

static void vop2_win_disable(struct vop2_win *win)
{
	vop2_win_write(win, VOP2_WIN_ENABLE, 0);

	if (vop2_cluster_window(win))
		vop2_win_write(win, VOP2_WIN_CLUSTER_ENABLE, 0);
}

static u32 vop2_get_bpp(const struct drm_format_info *format)
{
	switch (format->format) {
	case DRM_FORMAT_YUV420_8BIT:
		return 12;
	case DRM_FORMAT_YUV420_10BIT:
		return 15;
	case DRM_FORMAT_VUY101010:
		return 30;
	default:
		return drm_format_info_bpp(format, 0);
	}
}

static enum vop2_data_format vop2_convert_format(u32 format)
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

static enum vop2_afbc_format vop2_convert_afbc_format(u32 format)
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
	default:
		return VOP2_AFBC_FMT_INVALID;
	}

	return VOP2_AFBC_FMT_INVALID;
}

static bool vop2_win_rb_swap(u32 format)
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

static bool vop2_afbc_uv_swap(u32 format)
{
	switch (format) {
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_Y210:
	case DRM_FORMAT_YUV420_8BIT:
	case DRM_FORMAT_YUV420_10BIT:
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

static bool vop2_output_rg_swap(struct vop2 *vop2, u32 bus_format)
{
	if (vop2->version == VOP_VERSION_RK3588) {
		if (bus_format == MEDIA_BUS_FMT_YUV8_1X24 ||
		    bus_format == MEDIA_BUS_FMT_YUV10_1X30)
			return true;
	}

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

	if (vop2->version == VOP_VERSION_RK3568) {
		if (vop2_cluster_window(win)) {
			if (modifier == DRM_FORMAT_MOD_LINEAR) {
				drm_dbg_kms(vop2->drm,
					    "Cluster window only supports format with afbc\n");
				return false;
			}
		}
	}

	if (format == DRM_FORMAT_XRGB2101010 || format == DRM_FORMAT_XBGR2101010) {
		if (vop2->version == VOP_VERSION_RK3588) {
			if (!rockchip_afbc(plane, modifier)) {
				drm_dbg_kms(vop2->drm, "Only support 32 bpp format with afbc\n");
				return false;
			}
		}
	}

	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return true;

	if (!rockchip_afbc(plane, modifier)) {
		drm_dbg_kms(vop2->drm, "Unsupported format modifier 0x%llx\n",
			    modifier);

		return false;
	}

	return vop2_convert_afbc_format(format) >= 0;
}

/*
 * 0: Full mode, 16 lines for one tail
 * 1: half block mode, 8 lines one tail
 */
static bool vop2_half_block_enable(struct drm_plane_state *pstate)
{
	if (pstate->rotation & (DRM_MODE_ROTATE_270 | DRM_MODE_ROTATE_90))
		return false;
	else
		return true;
}

static u32 vop2_afbc_transform_offset(struct drm_plane_state *pstate,
				      bool afbc_half_block_en)
{
	struct drm_rect *src = &pstate->src;
	struct drm_framebuffer *fb = pstate->fb;
	u32 bpp = vop2_get_bpp(fb->format);
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
	uint16_t cbcr_src_w = src_w;
	uint16_t cbcr_src_h = src_h;
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
		cbcr_src_w /= info->hsub;
		cbcr_src_h /= info->vsub;

		gt4 = 0;
		gt2 = 0;

		if (cbcr_src_h >= (4 * dst_h)) {
			gt4 = 1;
			cbcr_src_h >>= 2;
		} else if (cbcr_src_h >= (2 * dst_h)) {
			gt2 = 1;
			cbcr_src_h >>= 1;
		}

		hor_scl_mode = scl_get_scl_mode(cbcr_src_w, dst_w);
		ver_scl_mode = scl_get_scl_mode(cbcr_src_h, dst_h);

		val = vop2_scale_factor(cbcr_src_w, dst_w);
		vop2_win_write(win, VOP2_WIN_SCALE_CBCR_X, val);

		val = vop2_scale_factor(cbcr_src_h, dst_h);
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

	ret = clk_prepare_enable(vop2->pclk);
	if (ret < 0) {
		drm_err(vop2->drm, "failed to enable pclk - %d\n", ret);
		goto err1;
	}

	return 0;
err1:
	clk_disable_unprepare(vop2->aclk);
err:
	clk_disable_unprepare(vop2->hclk);

	return ret;
}

static void rk3588_vop2_power_domain_enable_all(struct vop2 *vop2)
{
	u32 pd;

	pd = vop2_readl(vop2, RK3588_SYS_PD_CTRL);
	pd &= ~(VOP2_PD_CLUSTER0 | VOP2_PD_CLUSTER1 | VOP2_PD_CLUSTER2 |
		VOP2_PD_CLUSTER3 | VOP2_PD_ESMART);

	vop2_writel(vop2, RK3588_SYS_PD_CTRL, pd);
}

static void vop2_enable(struct vop2 *vop2)
{
	int ret;
	u32 version;

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

	version = vop2_readl(vop2, RK3568_VERSION_INFO);
	if (version != vop2->version) {
		drm_err(vop2->drm, "Hardware version(0x%08x) mismatch\n", version);
		return;
	}

	/*
	 * rk3566 share the same vop version with rk3568, so
	 * we need to use soc_id for identification here.
	 */
	if (vop2->data->soc_id == 3566)
		vop2_writel(vop2, RK3568_OTP_WIN_EN, 1);

	if (vop2->version == VOP_VERSION_RK3588)
		rk3588_vop2_power_domain_enable_all(vop2);

	if (vop2->version <= VOP_VERSION_RK3588) {
		vop2->old_layer_sel = vop2_readl(vop2, RK3568_OVL_LAYER_SEL);
		vop2->old_port_sel = vop2_readl(vop2, RK3568_OVL_PORT_SEL);
	}

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

	regcache_drop_region(vop2->map, 0, vop2_regmap_config.max_register);

	clk_disable_unprepare(vop2->pclk);
	clk_disable_unprepare(vop2->aclk);
	clk_disable_unprepare(vop2->hclk);
}

static bool vop2_vp_dsp_lut_is_enabled(struct vop2_video_port *vp)
{
	u32 dsp_ctrl = vop2_vp_read(vp, RK3568_VP_DSP_CTRL);

	return dsp_ctrl & RK3568_VP_DSP_CTRL__DSP_LUT_EN;
}

static void vop2_vp_dsp_lut_disable(struct vop2_video_port *vp)
{
	u32 dsp_ctrl = vop2_vp_read(vp, RK3568_VP_DSP_CTRL);

	dsp_ctrl &= ~RK3568_VP_DSP_CTRL__DSP_LUT_EN;
	vop2_vp_write(vp, RK3568_VP_DSP_CTRL, dsp_ctrl);
}

static bool vop2_vp_dsp_lut_poll_disabled(struct vop2_video_port *vp)
{
	u32 dsp_ctrl;
	int ret = readx_poll_timeout(vop2_vp_dsp_lut_is_enabled, vp, dsp_ctrl,
				!dsp_ctrl, 5, 30 * 1000);
	if (ret) {
		drm_err(vp->vop2->drm, "display LUT RAM enable timeout!\n");
		return false;
	}

	return true;
}

static void vop2_vp_dsp_lut_enable(struct vop2_video_port *vp)
{
	u32 dsp_ctrl = vop2_vp_read(vp, RK3568_VP_DSP_CTRL);

	dsp_ctrl |= RK3568_VP_DSP_CTRL__DSP_LUT_EN;
	vop2_vp_write(vp, RK3568_VP_DSP_CTRL, dsp_ctrl);
}

static void vop2_vp_dsp_lut_update_enable(struct vop2_video_port *vp)
{
	u32 dsp_ctrl = vop2_vp_read(vp, RK3568_VP_DSP_CTRL);

	dsp_ctrl |= RK3588_VP_DSP_CTRL__GAMMA_UPDATE_EN;
	vop2_vp_write(vp, RK3568_VP_DSP_CTRL, dsp_ctrl);
}

static inline bool vop2_supports_seamless_gamma_lut_update(struct vop2 *vop2)
{
	return vop2->version != VOP_VERSION_RK3568;
}

static bool vop2_gamma_lut_in_use(struct vop2 *vop2, struct vop2_video_port *vp)
{
	const int nr_vps = vop2->data->nr_vps;
	int gamma_en_vp_id;

	for (gamma_en_vp_id = 0; gamma_en_vp_id < nr_vps; gamma_en_vp_id++)
		if (vop2_vp_dsp_lut_is_enabled(&vop2->vps[gamma_en_vp_id]))
			break;

	return gamma_en_vp_id != nr_vps && gamma_en_vp_id != vp->id;
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

	if (vp->dclk_src)
		clk_set_parent(vp->dclk, vp->dclk_src);

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
	    drm_rect_width(dest) < 4 || drm_rect_height(dest) < 4) {
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
	u32 bpp = vop2_get_bpp(fb->format);
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
	bool half_block_en;
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
		drm_dbg_kms(vop2->drm,
			    "vp%d %s dest->x1[%d] + dsp_w[%d] exceed mode hdisplay[%d]\n",
			    vp->id, win->data->name, dest->x1, dsp_w, adjusted_mode->hdisplay);
		dsp_w = adjusted_mode->hdisplay - dest->x1;
		if (dsp_w < 4)
			dsp_w = 4;
		actual_w = dsp_w * actual_w / drm_rect_width(dest);
	}

	dsp_h = drm_rect_height(dest);

	if (dest->y1 + dsp_h > adjusted_mode->vdisplay) {
		drm_dbg_kms(vop2->drm,
			    "vp%d %s dest->y1[%d] + dsp_h[%d] exceed mode vdisplay[%d]\n",
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
			drm_dbg_kms(vop2->drm, "vp%d %s act_w[%d] MODE 16 == 1\n",
				    vp->id, win->data->name, actual_w);
			actual_w -= 1;
		}
	}

	if (afbc_en && actual_w % 4) {
		drm_dbg_kms(vop2->drm, "vp%d %s actual_w[%d] not 4 pixel aligned\n",
			    vp->id, win->data->name, actual_w);
		actual_w = ALIGN_DOWN(actual_w, 4);
	}

	act_info = (actual_h - 1) << 16 | ((actual_w - 1) & 0xffff);
	dsp_info = (dsp_h - 1) << 16 | ((dsp_w - 1) & 0xffff);

	format = vop2_convert_format(fb->format->format);
	half_block_en = vop2_half_block_enable(pstate);

	drm_dbg(vop2->drm, "vp%d update %s[%dx%d->%dx%d@%dx%d] fmt[%p4cc_%s] addr[%pad]\n",
		vp->id, win->data->name, actual_w, actual_h, dsp_w, dsp_h,
		dest->x1, dest->y1,
		&fb->format->format,
		afbc_en ? "AFBC" : "", &yrgb_mst);

	if (vop2->version > VOP_VERSION_RK3568) {
		vop2_win_write(win, VOP2_WIN_AXI_BUS_ID, win->data->axi_bus_id);
		vop2_win_write(win, VOP2_WIN_AXI_YRGB_R_ID, win->data->axi_yrgb_r_id);
		vop2_win_write(win, VOP2_WIN_AXI_UV_R_ID, win->data->axi_uv_r_id);
	}

	if (vop2->version >= VOP_VERSION_RK3576)
		vop2_win_write(win, VOP2_WIN_VP_SEL, vp->id);

	if (vop2_cluster_window(win))
		vop2_win_write(win, VOP2_WIN_AFBC_HALF_BLOCK_EN, half_block_en);

	if (afbc_en) {
		u32 stride, block_w;

		/* the afbc superblock is 16 x 16 or 32 x 8 */
		block_w = fb->modifier & AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 ? 32 : 16;

		afbc_format = vop2_convert_afbc_format(fb->format->format);

		/* Enable color transform for YTR */
		if (fb->modifier & AFBC_FORMAT_MOD_YTR)
			afbc_format |= (1 << 4);

		afbc_tile_num = ALIGN(actual_w, block_w) / block_w;

		/*
		 * AFBC pic_vir_width is count by pixel, this is different
		 * with WIN_VIR_STRIDE.
		 */
		stride = (fb->pitches[0] << 3) / bpp;
		if ((stride & 0x3f) && (xmirror || rotate_90 || rotate_270))
			drm_dbg_kms(vop2->drm, "vp%d %s stride[%d] not 64 pixel aligned\n",
				    vp->id, win->data->name, stride);

		 /* It's for head stride, each head size is 16 byte */
		stride = ALIGN(stride, block_w) / block_w * 16;

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
		vop2_win_write(win, VOP2_WIN_AFBC_UV_SWAP, uv_swap);
		/*
		 * On rk3566/8, this bit is auto gating enable,
		 * but this function is not work well so we need
		 * to disable it for these two platform.
		 * On rk3588, and the following new soc(rk3528/rk3576),
		 * this bit is gating disable, we should write 1 to
		 * disable gating when enable afbc.
		 */
		if (vop2->version == VOP_VERSION_RK3568)
			vop2_win_write(win, VOP2_WIN_AFBC_AUTO_GATING_EN, 0);
		else
			vop2_win_write(win, VOP2_WIN_AFBC_AUTO_GATING_EN, 1);

		if (fb->modifier & AFBC_FORMAT_MOD_SPLIT)
			vop2_win_write(win, VOP2_WIN_AFBC_BLOCK_SPLIT_EN, 1);
		else
			vop2_win_write(win, VOP2_WIN_AFBC_BLOCK_SPLIT_EN, 0);

		if (vop2->version >= VOP_VERSION_RK3576) {
			vop2_win_write(win, VOP2_WIN_AFBC_PLD_OFFSET_EN, 1);
			vop2_win_write(win, VOP2_WIN_AFBC_PLD_OFFSET, yrgb_mst);
		}

		transform_offset = vop2_afbc_transform_offset(pstate, half_block_en);
		vop2_win_write(win, VOP2_WIN_AFBC_HDR_PTR, yrgb_mst);
		vop2_win_write(win, VOP2_WIN_AFBC_PIC_SIZE, act_info);
		vop2_win_write(win, VOP2_WIN_TRANSFORM_OFFSET, transform_offset);
		vop2_win_write(win, VOP2_WIN_AFBC_PIC_OFFSET, ((src->x1 >> 16) | src->y1));
		vop2_win_write(win, VOP2_WIN_AFBC_DSP_OFFSET, (dest->x1 | (dest->y1 << 16)));
		vop2_win_write(win, VOP2_WIN_AFBC_PIC_VIR_WIDTH, stride);
		vop2_win_write(win, VOP2_WIN_AFBC_TILE_NUM, afbc_tile_num);
		vop2_win_write(win, VOP2_WIN_XMIRROR, xmirror);
		vop2_win_write(win, VOP2_WIN_AFBC_ROTATE_270, rotate_270);
		vop2_win_write(win, VOP2_WIN_AFBC_ROTATE_90, rotate_90);
	} else {
		if (vop2_cluster_window(win)) {
			vop2_win_write(win, VOP2_WIN_AFBC_ENABLE, 0);
			vop2_win_write(win, VOP2_WIN_TRANSFORM_OFFSET, 0);
		}

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
	uv_swap = vop2_win_uv_swap(fb->format->format);
	vop2_win_write(win, VOP2_WIN_UV_SWAP, uv_swap);

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

static void vop2_crtc_write_gamma_lut(struct vop2 *vop2, struct drm_crtc *crtc)
{
	const struct vop2_video_port *vp = to_vop2_video_port(crtc);
	const struct vop2_video_port_data *vp_data = &vop2->data->vp[vp->id];
	struct drm_color_lut *lut = crtc->state->gamma_lut->data;
	unsigned int i, bpc = ilog2(vp_data->gamma_lut_len);
	u32 word;

	for (i = 0; i < crtc->gamma_size; i++) {
		word = (drm_color_lut_extract(lut[i].blue, bpc) << (2 * bpc)) |
		    (drm_color_lut_extract(lut[i].green, bpc) << bpc) |
		    drm_color_lut_extract(lut[i].red, bpc);

		writel(word, vop2->lut_regs + i * 4);
	}
}

static void vop2_crtc_atomic_set_gamma_seamless(struct vop2 *vop2,
						struct vop2_video_port *vp,
						struct drm_crtc *crtc)
{
	vop2_writel(vop2, RK3568_LUT_PORT_SEL,
		    FIELD_PREP(RK3588_LUT_PORT_SEL__GAMMA_AHB_WRITE_SEL, vp->id));
	vop2_vp_dsp_lut_enable(vp);
	vop2_crtc_write_gamma_lut(vop2, crtc);
	vop2_vp_dsp_lut_update_enable(vp);
}

static void vop2_crtc_atomic_set_gamma_rk356x(struct vop2 *vop2,
					      struct vop2_video_port *vp,
					      struct drm_crtc *crtc)
{
	vop2_vp_dsp_lut_disable(vp);
	vop2_cfg_done(vp);
	if (!vop2_vp_dsp_lut_poll_disabled(vp))
		return;

	vop2_writel(vop2, RK3568_LUT_PORT_SEL, vp->id);
	vop2_crtc_write_gamma_lut(vop2, crtc);
	vop2_vp_dsp_lut_enable(vp);
}

static void vop2_crtc_atomic_try_set_gamma(struct vop2 *vop2,
					   struct vop2_video_port *vp,
					   struct drm_crtc *crtc,
					   struct drm_crtc_state *crtc_state)
{
	if (!vop2->lut_regs)
		return;

	if (!crtc_state->gamma_lut) {
		vop2_vp_dsp_lut_disable(vp);
		return;
	}

	if (vop2_supports_seamless_gamma_lut_update(vop2))
		vop2_crtc_atomic_set_gamma_seamless(vop2, vp, crtc);
	else
		vop2_crtc_atomic_set_gamma_rk356x(vop2, vp, crtc);
}

static inline void vop2_crtc_atomic_try_set_gamma_locked(struct vop2 *vop2,
							 struct vop2_video_port *vp,
							 struct drm_crtc *crtc,
							 struct drm_crtc_state *crtc_state)
{
	vop2_lock(vop2);
	vop2_crtc_atomic_try_set_gamma(vop2, vp, crtc, crtc_state);
	vop2_unlock(vop2);
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
	struct vop2 *vop2 = vp->vop2;
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

	vop2->ops->setup_bg_dly(vp);

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

	vcstate->yuv_overlay = is_yuv_output(vcstate->bus_format);

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

		/*
		 * for drive a high resolution(4KP120, 8K), vop on rk3588/rk3576 need
		 * process multi(1/2/4/8) pixels per cycle, so the dclk feed by the
		 * system cru may be the 1/2 or 1/4 of mode->clock.
		 */
		clock = vop2->ops->setup_intf_mux(vp, rkencoder->crtc_endpoint_id, polflags);
	}

	if (!clock) {
		vop2_unlock(vop2);
		return;
	}

	if (vcstate->output_mode == ROCKCHIP_OUT_MODE_AAAA &&
	    !(vp_data->feature & VOP2_VP_FEATURE_OUTPUT_10BIT))
		out_mode = ROCKCHIP_OUT_MODE_P888;
	else
		out_mode = vcstate->output_mode;

	dsp_ctrl |= FIELD_PREP(RK3568_VP_DSP_CTRL__OUT_MODE, out_mode);

	if (vop2_output_uv_swap(vcstate->bus_format, vcstate->output_mode))
		dsp_ctrl |= RK3568_VP_DSP_CTRL__DSP_RB_SWAP;
	if (vop2_output_rg_swap(vop2, vcstate->bus_format))
		dsp_ctrl |= RK3568_VP_DSP_CTRL__DSP_RG_SWAP;

	if (vcstate->yuv_overlay)
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

	/*
	 * Switch to HDMI PHY PLL as DCLK source for display modes up
	 * to 4K@60Hz, if available, otherwise keep using the system CRU.
	 */
	if ((vop2->pll_hdmiphy0 || vop2->pll_hdmiphy1) && clock <= VOP2_MAX_DCLK_RATE) {
		drm_for_each_encoder_mask(encoder, crtc->dev, crtc_state->encoder_mask) {
			struct rockchip_encoder *rkencoder = to_rockchip_encoder(encoder);

			if (rkencoder->crtc_endpoint_id == ROCKCHIP_VOP2_EP_HDMI0) {
				if (!vop2->pll_hdmiphy0)
					break;

				if (!vp->dclk_src)
					vp->dclk_src = clk_get_parent(vp->dclk);

				ret = clk_set_parent(vp->dclk, vop2->pll_hdmiphy0);
				if (ret < 0)
					drm_warn(vop2->drm,
						 "Could not switch to HDMI0 PHY PLL: %d\n", ret);
				break;
			}

			if (rkencoder->crtc_endpoint_id == ROCKCHIP_VOP2_EP_HDMI1) {
				if (!vop2->pll_hdmiphy1)
					break;

				if (!vp->dclk_src)
					vp->dclk_src = clk_get_parent(vp->dclk);

				ret = clk_set_parent(vp->dclk, vop2->pll_hdmiphy1);
				if (ret < 0)
					drm_warn(vop2->drm,
						 "Could not switch to HDMI1 PHY PLL: %d\n", ret);
				break;
			}
		}
	}

	clk_set_rate(vp->dclk, clock);

	vop2_post_config(crtc);

	vop2_cfg_done(vp);

	vop2_vp_write(vp, RK3568_VP_DSP_CTRL, dsp_ctrl);

	vop2_crtc_atomic_try_set_gamma(vop2, vp, crtc, crtc_state);

	drm_crtc_vblank_on(crtc);

	vop2_unlock(vop2);
}

static int vop2_crtc_atomic_check_gamma(struct vop2_video_port *vp,
					struct drm_crtc *crtc,
					struct drm_atomic_state *state,
					struct drm_crtc_state *crtc_state)
{
	struct vop2 *vop2 = vp->vop2;
	unsigned int len;

	if (!vp->vop2->lut_regs || !crtc_state->color_mgmt_changed ||
	    !crtc_state->gamma_lut)
		return 0;

	len = drm_color_lut_size(crtc_state->gamma_lut);
	if (len != crtc->gamma_size) {
		drm_dbg(vop2->drm, "Invalid LUT size; got %d, expected %d\n",
			len, crtc->gamma_size);
		return -EINVAL;
	}

	if (!vop2_supports_seamless_gamma_lut_update(vop2) && vop2_gamma_lut_in_use(vop2, vp)) {
		drm_info(vop2->drm, "Gamma LUT can be enabled for only one CRTC at a time\n");
		return -EINVAL;
	}

	return 0;
}

static int vop2_crtc_atomic_check(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_plane *plane;
	int nplanes = 0;
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	int ret;

	ret = vop2_crtc_atomic_check_gamma(vp, crtc, state, crtc_state);
	if (ret)
		return ret;

	drm_atomic_crtc_state_for_each_plane(plane, crtc_state)
		nplanes++;

	if (nplanes > vp->nlayers)
		return -EINVAL;

	return 0;
}

static void vop2_crtc_atomic_begin(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	vop2->ops->setup_overlay(vp);
}

static void vop2_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	/* In case of modeset, gamma lut update already happened in atomic enable */
	if (!drm_atomic_crtc_needs_modeset(crtc_state) && crtc_state->color_mgmt_changed)
		vop2_crtc_atomic_try_set_gamma_locked(vop2, vp, crtc, crtc_state);

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

static void vop2_dump_connector_on_crtc(struct drm_crtc *crtc, struct seq_file *s)
{
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;

	drm_connector_list_iter_begin(crtc->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (crtc->state->connector_mask & drm_connector_mask(connector))
			seq_printf(s, "    Connector: %s\n", connector->name);
	}
	drm_connector_list_iter_end(&conn_iter);
}

static int vop2_plane_state_dump(struct seq_file *s, struct drm_plane *plane)
{
	struct vop2_win *win = to_vop2_win(plane);
	struct drm_plane_state *pstate = plane->state;
	struct drm_rect *src, *dst;
	struct drm_framebuffer *fb;
	struct drm_gem_object *obj;
	struct rockchip_gem_object *rk_obj;
	bool xmirror;
	bool ymirror;
	bool rotate_270;
	bool rotate_90;
	dma_addr_t fb_addr;
	int i;

	seq_printf(s, "    %s: %s\n", win->data->name, !pstate ?
		   "DISABLED" : pstate->crtc ? "ACTIVE" : "DISABLED");

	if (!pstate || !pstate->fb)
		return 0;

	fb = pstate->fb;
	src = &pstate->src;
	dst = &pstate->dst;
	xmirror = pstate->rotation & DRM_MODE_REFLECT_X ? true : false;
	ymirror = pstate->rotation & DRM_MODE_REFLECT_Y ? true : false;
	rotate_270 = pstate->rotation & DRM_MODE_ROTATE_270;
	rotate_90 = pstate->rotation & DRM_MODE_ROTATE_90;

	seq_printf(s, "\twin_id: %d\n", win->win_id);

	seq_printf(s, "\tformat: %p4cc%s glb_alpha[0x%x]\n",
		   &fb->format->format,
		   drm_is_afbc(fb->modifier) ? "[AFBC]" : "",
		   pstate->alpha >> 8);
	seq_printf(s, "\trotate: xmirror: %d ymirror: %d rotate_90: %d rotate_270: %d\n",
		   xmirror, ymirror, rotate_90, rotate_270);
	seq_printf(s, "\tzpos: %d\n", pstate->normalized_zpos);
	seq_printf(s, "\tsrc: pos[%d, %d] rect[%d x %d]\n", src->x1 >> 16,
		   src->y1 >> 16, drm_rect_width(src) >> 16,
		   drm_rect_height(src) >> 16);
	seq_printf(s, "\tdst: pos[%d, %d] rect[%d x %d]\n", dst->x1, dst->y1,
		   drm_rect_width(dst), drm_rect_height(dst));

	for (i = 0; i < fb->format->num_planes; i++) {
		obj = fb->obj[i];
		rk_obj = to_rockchip_obj(obj);
		fb_addr = rk_obj->dma_addr + fb->offsets[i];

		seq_printf(s, "\tbuf[%d]: addr: %pad pitch: %d offset: %d\n",
			   i, &fb_addr, fb->pitches[i], fb->offsets[i]);
	}

	return 0;
}

static int vop2_crtc_state_dump(struct drm_crtc *crtc, struct seq_file *s)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct drm_crtc_state *cstate = crtc->state;
	struct rockchip_crtc_state *vcstate;
	struct drm_display_mode *mode;
	struct drm_plane *plane;
	bool interlaced;

	seq_printf(s, "Video Port%d: %s\n", vp->id, !cstate ?
		   "DISABLED" : cstate->active ? "ACTIVE" : "DISABLED");

	if (!cstate || !cstate->active)
		return 0;

	mode = &crtc->state->adjusted_mode;
	vcstate = to_rockchip_crtc_state(cstate);
	interlaced = !!(mode->flags & DRM_MODE_FLAG_INTERLACE);

	vop2_dump_connector_on_crtc(crtc, s);
	seq_printf(s, "\tbus_format[%x]: %s\n", vcstate->bus_format,
		   drm_get_bus_format_name(vcstate->bus_format));
	seq_printf(s, "\toutput_mode[%x]", vcstate->output_mode);
	seq_printf(s, " color_space[%d]\n", vcstate->color_space);
	seq_printf(s, "    Display mode: %dx%d%s%d\n",
		   mode->hdisplay, mode->vdisplay, interlaced ? "i" : "p",
		   drm_mode_vrefresh(mode));
	seq_printf(s, "\tclk[%d] real_clk[%d] type[%x] flag[%x]\n",
		   mode->clock, mode->crtc_clock, mode->type, mode->flags);
	seq_printf(s, "\tH: %d %d %d %d\n", mode->hdisplay, mode->hsync_start,
		   mode->hsync_end, mode->htotal);
	seq_printf(s, "\tV: %d %d %d %d\n", mode->vdisplay, mode->vsync_start,
		   mode->vsync_end, mode->vtotal);

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		vop2_plane_state_dump(s, plane);
	}

	return 0;
}

static int vop2_summary_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *drm_dev = minor->dev;
	struct drm_crtc *crtc;

	drm_modeset_lock_all(drm_dev);
	drm_for_each_crtc(crtc, drm_dev) {
		vop2_crtc_state_dump(crtc, s);
	}
	drm_modeset_unlock_all(drm_dev);

	return 0;
}

static void vop2_regs_print(struct vop2 *vop2, struct seq_file *s,
			    const struct vop2_regs_dump *dump, bool active_only)
{
	resource_size_t start;
	u32 val;
	int i;

	if (dump->en_mask && active_only) {
		val = vop2_readl(vop2, dump->base + dump->en_reg);
		if ((val & dump->en_mask) != dump->en_val)
			return;
	}

	seq_printf(s, "\n%s:\n", dump->name);

	start = vop2->res->start + dump->base;
	for (i = 0; i < dump->size >> 2; i += 4) {
		seq_printf(s, "%08x:  %08x %08x %08x %08x\n", (u32)start + i * 4,
			   vop2_readl(vop2, dump->base + (4 * i)),
			   vop2_readl(vop2, dump->base + (4 * (i + 1))),
			   vop2_readl(vop2, dump->base + (4 * (i + 2))),
			   vop2_readl(vop2, dump->base + (4 * (i + 3))));
	}
}

static void __vop2_regs_dump(struct seq_file *s, bool active_only)
{
	struct drm_info_node *node = s->private;
	struct vop2 *vop2 = node->info_ent->data;
	struct drm_minor *minor = node->minor;
	struct drm_device *drm_dev = minor->dev;
	const struct vop2_regs_dump *dump;
	unsigned int i;

	drm_modeset_lock_all(drm_dev);

	regcache_drop_region(vop2->map, 0, vop2_regmap_config.max_register);

	if (vop2->enable_count) {
		for (i = 0; i < vop2->data->regs_dump_size; i++) {
			dump = &vop2->data->regs_dump[i];
			vop2_regs_print(vop2, s, dump, active_only);
		}
	} else {
		seq_puts(s, "VOP disabled\n");
	}
	drm_modeset_unlock_all(drm_dev);
}

static int vop2_regs_show(struct seq_file *s, void *arg)
{
	__vop2_regs_dump(s, false);

	return 0;
}

static int vop2_active_regs_show(struct seq_file *s, void *data)
{
	__vop2_regs_dump(s, true);

	return 0;
}

static struct drm_info_list vop2_debugfs_list[] = {
	{ "summary", vop2_summary_show, 0, NULL },
	{ "active_regs", vop2_active_regs_show,   0, NULL },
	{ "regs", vop2_regs_show,   0, NULL },
};

static void vop2_debugfs_init(struct vop2 *vop2, struct drm_minor *minor)
{
	struct dentry *root;
	unsigned int i;

	root = debugfs_create_dir("vop2", minor->debugfs_root);
	if (!IS_ERR(root)) {
		for (i = 0; i < ARRAY_SIZE(vop2_debugfs_list); i++)
			vop2_debugfs_list[i].data = vop2;

		drm_debugfs_create_files(vop2_debugfs_list,
					 ARRAY_SIZE(vop2_debugfs_list),
					 root, minor);
	}
}

static int vop2_crtc_late_register(struct drm_crtc *crtc)
{
	struct vop2_video_port *vp = to_vop2_video_port(crtc);
	struct vop2 *vop2 = vp->vop2;

	if (drm_crtc_index(crtc) == 0)
		vop2_debugfs_init(vop2, crtc->dev->primary);

	return 0;
}

static struct drm_crtc_state *vop2_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *vcstate;

	if (WARN_ON(!crtc->state))
		return NULL;

	vcstate = kmemdup(to_rockchip_crtc_state(crtc->state),
			  sizeof(*vcstate), GFP_KERNEL);
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

static void vop2_crtc_reset(struct drm_crtc *crtc)
{
	struct rockchip_crtc_state *vcstate =
		kzalloc(sizeof(*vcstate), GFP_KERNEL);

	if (crtc->state)
		vop2_crtc_destroy_state(crtc, crtc->state);

	if (vcstate)
		__drm_atomic_helper_crtc_reset(crtc, &vcstate->base);
	else
		__drm_atomic_helper_crtc_reset(crtc, NULL);
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
	.late_register = vop2_crtc_late_register,
};

static irqreturn_t rk3576_vp_isr(int irq, void *data)
{
	struct vop2_video_port *vp = data;
	struct vop2 *vop2 = vp->vop2;
	struct drm_crtc *crtc = &vp->crtc;
	uint32_t irqs;
	int ret = IRQ_NONE;

	if (!pm_runtime_get_if_in_use(vop2->dev))
		return IRQ_NONE;

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
		drm_err_ratelimited(vop2->drm, "POST_BUF_EMPTY irq err at vp%d\n", vp->id);
		ret = IRQ_HANDLED;
	}

	pm_runtime_put(vop2->dev);

	return ret;
}

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

	if (vop2->version < VOP_VERSION_RK3576) {
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

/*
 * On RK3566 these windows don't have an independent
 * framebuffer. They can only share/mirror the framebuffer
 * with smart0, esmart0 and cluster0 respectively.
 * And RK3566 share the same vop version with Rk3568, so we
 * need to use soc_id for identification here.
 */
static bool vop2_is_mirror_win(struct vop2_win *win)
{
	struct vop2 *vop2 = win->vop2;

	if (vop2->data->soc_id == 3566) {
		switch (win->data->phys_id) {
		case ROCKCHIP_VOP2_SMART1:
		case ROCKCHIP_VOP2_ESMART1:
		case ROCKCHIP_VOP2_CLUSTER1:
			return true;
		default:
			return false;
		}
	} else {
		return false;
	}
}

static int vop2_create_crtcs(struct vop2 *vop2)
{
	const struct vop2_data *vop2_data = vop2->data;
	struct drm_device *drm = vop2->drm;
	struct device *dev = vop2->dev;
	struct drm_plane *plane;
	struct device_node *port;
	struct vop2_video_port *vp;
	struct vop2_win *win;
	u32 possible_crtcs;
	int i, j, nvp, nvps = 0;
	int ret;

	for (i = 0; i < vop2_data->nr_vps; i++) {
		const struct vop2_video_port_data *vp_data;
		struct device_node *np;
		char dclk_name[9];

		vp_data = &vop2_data->vp[i];
		vp = &vop2->vps[i];
		vp->vop2 = vop2;
		vp->id = vp_data->id;
		vp->data = vp_data;

		snprintf(dclk_name, sizeof(dclk_name), "dclk_vp%d", vp->id);
		vp->dclk = devm_clk_get(vop2->dev, dclk_name);
		if (IS_ERR(vp->dclk))
			return dev_err_probe(drm->dev, PTR_ERR(vp->dclk),
					     "failed to get %s\n", dclk_name);

		np = of_graph_get_remote_node(dev->of_node, i, -1);
		if (!np) {
			drm_dbg(vop2->drm, "%s: No remote for vp%d\n", __func__, i);
			continue;
		}
		of_node_put(np);

		port = of_graph_get_port_by_id(dev->of_node, i);
		if (!port)
			return dev_err_probe(drm->dev, -ENOENT,
					     "no port node found for video_port%d\n", i);
		vp->crtc.port = port;
		nvps++;
	}

	nvp = 0;
	/* Register a primary plane for every crtc */
	for (i = 0; i < vop2_data->nr_vps; i++) {
		vp = &vop2->vps[i];

		if (!vp->crtc.port)
			continue;

		for (j = 0; j < vop2->registered_num_wins; j++) {
			win = &vop2->win[j];

			/* Aready registered as primary plane */
			if (win->base.type == DRM_PLANE_TYPE_PRIMARY)
				continue;

			/* If this win can not attached to this VP */
			if (!(win->data->possible_vp_mask & BIT(vp->id)))
				continue;

			if (vop2_is_mirror_win(win))
				continue;

			if (win->type == DRM_PLANE_TYPE_PRIMARY) {
				possible_crtcs = BIT(nvp);
				vp->primary_plane = win;
				ret = vop2_plane_init(vop2, win, possible_crtcs);
				if (ret)
					return dev_err_probe(drm->dev, ret,
							     "failed to init primary plane %s\n",
							     win->data->name);
				nvp++;
				break;
			}
		}

		if (!vp->primary_plane)
			return dev_err_probe(drm->dev, -ENOENT,
					     "no primary plane for vp %d\n", i);
	}

	/* Register all unused window as overlay plane */
	for (i = 0; i < vop2->registered_num_wins; i++) {
		win = &vop2->win[i];

		/* Aready registered as primary plane */
		if (win->base.type == DRM_PLANE_TYPE_PRIMARY)
			continue;

		if (vop2_is_mirror_win(win))
			continue;

		win->type = DRM_PLANE_TYPE_OVERLAY;

		possible_crtcs = 0;
		nvp = 0;
		for (j = 0; j < vop2_data->nr_vps; j++) {
			vp = &vop2->vps[j];

			if (!vp->crtc.port)
				continue;

			if (win->data->possible_vp_mask & BIT(vp->id))
				possible_crtcs |= BIT(nvp);
			nvp++;
		}

		ret = vop2_plane_init(vop2, win, possible_crtcs);
		if (ret)
			return dev_err_probe(drm->dev, ret, "failed to init overlay plane %s\n",
					     win->data->name);
	}

	for (i = 0; i < vop2_data->nr_vps; i++) {
		vp = &vop2->vps[i];

		if (!vp->crtc.port)
			continue;

		plane = &vp->primary_plane->base;

		ret = drm_crtc_init_with_planes(drm, &vp->crtc, plane, NULL,
						&vop2_crtc_funcs,
						"video_port%d", vp->id);
		if (ret)
			return dev_err_probe(drm->dev, ret,
					     "crtc init for video_port%d failed\n", i);

		drm_crtc_helper_add(&vp->crtc, &vop2_crtc_helper_funcs);
		if (vop2->lut_regs) {
			const struct vop2_video_port_data *vp_data = &vop2_data->vp[vp->id];

			drm_mode_crtc_set_gamma_size(&vp->crtc, vp_data->gamma_lut_len);
			drm_crtc_enable_color_mgmt(&vp->crtc, 0, false, vp_data->gamma_lut_len);
		}
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
			vp->nlayers = vop2_data->win_size / nvps;
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

static int vop2_regmap_init(struct vop2_win *win, const struct reg_field *regs,
			    int nr_regs)
{
	struct vop2 *vop2 = win->vop2;
	int i;

	for (i = 0; i < nr_regs; i++) {
		const struct reg_field field = {
			.reg = (regs[i].reg != 0xffffffff) ?
				regs[i].reg + win->offset : regs[i].reg,
			.lsb = regs[i].lsb,
			.msb = regs[i].msb
		};

		win->reg[i] = devm_regmap_field_alloc(vop2->dev, vop2->map, field);
		if (IS_ERR(win->reg[i]))
			return PTR_ERR(win->reg[i]);
	}

	return 0;
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
			ret = vop2_regmap_init(win, vop2->data->cluster_reg,
					       vop2->data->nr_cluster_regs);
		else
			ret = vop2_regmap_init(win, vop2->data->smart_reg,
					       vop2->data->nr_smart_regs);
		if (ret)
			return ret;
	}

	vop2->registered_num_wins = vop2_data->win_size;

	return 0;
}

/*
 * The window and video port registers are only updated when config
 * done is written. Until that they read back the old value. As we
 * read-modify-write these registers mark them as non-volatile. This
 * makes sure we read the new values from the regmap register cache.
 */
static const struct regmap_range vop2_nonvolatile_range[] = {
	regmap_reg_range(RK3568_VP0_CTRL_BASE, RK3588_VP3_CTRL_BASE + 255),
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
	.cache_type	= REGCACHE_MAPLE,
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
	vop2->ops = vop2_data->ops;
	vop2->version = vop2_data->version;
	vop2->drm = drm;

	dev_set_drvdata(dev, vop2);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vop");
	if (!res)
		return dev_err_probe(drm->dev, -EINVAL,
				     "failed to get vop2 register byname\n");

	vop2->res = res;
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
	if (vop2_data->feature & VOP2_FEATURE_HAS_SYS_GRF) {
		vop2->sys_grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");
		if (IS_ERR(vop2->sys_grf))
			return dev_err_probe(drm->dev, PTR_ERR(vop2->sys_grf),
					     "cannot get sys_grf\n");
	}

	if (vop2_data->feature & VOP2_FEATURE_HAS_VOP_GRF) {
		vop2->vop_grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,vop-grf");
		if (IS_ERR(vop2->vop_grf))
			return dev_err_probe(drm->dev, PTR_ERR(vop2->vop_grf),
					     "cannot get vop_grf\n");
	}

	if (vop2_data->feature & VOP2_FEATURE_HAS_VO1_GRF) {
		vop2->vo1_grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,vo1-grf");
		if (IS_ERR(vop2->vo1_grf))
			return dev_err_probe(drm->dev, PTR_ERR(vop2->vo1_grf),
					     "cannot get vo1_grf\n");
	}

	if (vop2_data->feature & VOP2_FEATURE_HAS_SYS_PMU) {
		vop2->sys_pmu = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,pmu");
		if (IS_ERR(vop2->sys_pmu))
			return dev_err_probe(drm->dev, PTR_ERR(vop2->sys_pmu),
					     "cannot get sys_pmu\n");
	}

	vop2->hclk = devm_clk_get(vop2->dev, "hclk");
	if (IS_ERR(vop2->hclk))
		return dev_err_probe(drm->dev, PTR_ERR(vop2->hclk),
				     "failed to get hclk source\n");

	vop2->aclk = devm_clk_get(vop2->dev, "aclk");
	if (IS_ERR(vop2->aclk))
		return dev_err_probe(drm->dev, PTR_ERR(vop2->aclk),
				     "failed to get aclk source\n");

	vop2->pclk = devm_clk_get_optional(vop2->dev, "pclk_vop");
	if (IS_ERR(vop2->pclk))
		return dev_err_probe(drm->dev, PTR_ERR(vop2->pclk),
				     "failed to get pclk source\n");

	vop2->pll_hdmiphy0 = devm_clk_get_optional(vop2->dev, "pll_hdmiphy0");
	if (IS_ERR(vop2->pll_hdmiphy0))
		return dev_err_probe(drm->dev, PTR_ERR(vop2->pll_hdmiphy0),
				     "failed to get pll_hdmiphy0\n");

	vop2->pll_hdmiphy1 = devm_clk_get_optional(vop2->dev, "pll_hdmiphy1");
	if (IS_ERR(vop2->pll_hdmiphy1))
		return dev_err_probe(drm->dev, PTR_ERR(vop2->pll_hdmiphy1),
				     "failed to get pll_hdmiphy1\n");

	vop2->irq = platform_get_irq(pdev, 0);
	if (vop2->irq < 0)
		return dev_err_probe(drm->dev, vop2->irq, "cannot find irq for vop2\n");

	mutex_init(&vop2->vop2_lock);
	mutex_init(&vop2->ovl_lock);

	ret = devm_request_irq(dev, vop2->irq, vop2_isr, IRQF_SHARED, dev_name(dev), vop2);
	if (ret)
		return ret;

	ret = vop2_create_crtcs(vop2);
	if (ret)
		return ret;

	if (vop2->version >= VOP_VERSION_RK3576) {
		struct drm_crtc *crtc;

		drm_for_each_crtc(crtc, drm) {
			struct vop2_video_port *vp = to_vop2_video_port(crtc);
			int vp_irq;
			const char *irq_name = devm_kasprintf(dev, GFP_KERNEL, "vp%d", vp->id);

			if (!irq_name)
				return -ENOMEM;

			vp_irq = platform_get_irq_byname(pdev, irq_name);
			if (vp_irq < 0)
				return dev_err_probe(drm->dev, vp_irq,
						     "cannot find irq for vop2 vp%d\n", vp->id);

			ret = devm_request_irq(dev, vp_irq, rk3576_vp_isr, IRQF_SHARED, irq_name,
					       vp);
			if (ret)
				dev_err_probe(drm->dev, ret,
					      "request irq for vop2 vp%d failed\n", vp->id);
		}
	}

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
