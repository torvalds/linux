/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Kevin Tian <kevin.tian@intel.com>
 *
 * Contributors:
 *    Bing Niu <bing.niu@intel.com>
 *    Xu Han <xu.han@intel.com>
 *    Ping Gao <ping.a.gao@intel.com>
 *    Xiaoguang Chen <xiaoguang.chen@intel.com>
 *    Yang Liu <yang2.liu@intel.com>
 *    Tina Zhang <tina.zhang@intel.com>
 *
 */

#include <uapi/drm/drm_fourcc.h>
#include "i915_drv.h"
#include "gvt.h"
#include "i915_pvinfo.h"

#define PRIMARY_FORMAT_NUM	16
struct pixel_format {
	int	drm_format;	/* Pixel format in DRM definition */
	int	bpp;		/* Bits per pixel, 0 indicates invalid */
	char	*desc;		/* The description */
};

static struct pixel_format bdw_pixel_formats[] = {
	{DRM_FORMAT_C8, 8, "8-bit Indexed"},
	{DRM_FORMAT_RGB565, 16, "16-bit BGRX (5:6:5 MSB-R:G:B)"},
	{DRM_FORMAT_XRGB8888, 32, "32-bit BGRX (8:8:8:8 MSB-X:R:G:B)"},
	{DRM_FORMAT_XBGR2101010, 32, "32-bit RGBX (2:10:10:10 MSB-X:B:G:R)"},

	{DRM_FORMAT_XRGB2101010, 32, "32-bit BGRX (2:10:10:10 MSB-X:R:G:B)"},
	{DRM_FORMAT_XBGR8888, 32, "32-bit RGBX (8:8:8:8 MSB-X:B:G:R)"},

	/* non-supported format has bpp default to 0 */
	{0, 0, NULL},
};

static struct pixel_format skl_pixel_formats[] = {
	{DRM_FORMAT_YUYV, 16, "16-bit packed YUYV (8:8:8:8 MSB-V:Y2:U:Y1)"},
	{DRM_FORMAT_UYVY, 16, "16-bit packed UYVY (8:8:8:8 MSB-Y2:V:Y1:U)"},
	{DRM_FORMAT_YVYU, 16, "16-bit packed YVYU (8:8:8:8 MSB-U:Y2:V:Y1)"},
	{DRM_FORMAT_VYUY, 16, "16-bit packed VYUY (8:8:8:8 MSB-Y2:U:Y1:V)"},

	{DRM_FORMAT_C8, 8, "8-bit Indexed"},
	{DRM_FORMAT_RGB565, 16, "16-bit BGRX (5:6:5 MSB-R:G:B)"},
	{DRM_FORMAT_ABGR8888, 32, "32-bit RGBA (8:8:8:8 MSB-A:B:G:R)"},
	{DRM_FORMAT_XBGR8888, 32, "32-bit RGBX (8:8:8:8 MSB-X:B:G:R)"},

	{DRM_FORMAT_ARGB8888, 32, "32-bit BGRA (8:8:8:8 MSB-A:R:G:B)"},
	{DRM_FORMAT_XRGB8888, 32, "32-bit BGRX (8:8:8:8 MSB-X:R:G:B)"},
	{DRM_FORMAT_XBGR2101010, 32, "32-bit RGBX (2:10:10:10 MSB-X:B:G:R)"},
	{DRM_FORMAT_XRGB2101010, 32, "32-bit BGRX (2:10:10:10 MSB-X:R:G:B)"},

	/* non-supported format has bpp default to 0 */
	{0, 0, NULL},
};

static int bdw_format_to_drm(int format)
{
	int bdw_pixel_formats_index = 6;

	switch (format) {
	case DISPPLANE_8BPP:
		bdw_pixel_formats_index = 0;
		break;
	case DISPPLANE_BGRX565:
		bdw_pixel_formats_index = 1;
		break;
	case DISPPLANE_BGRX888:
		bdw_pixel_formats_index = 2;
		break;
	case DISPPLANE_RGBX101010:
		bdw_pixel_formats_index = 3;
		break;
	case DISPPLANE_BGRX101010:
		bdw_pixel_formats_index = 4;
		break;
	case DISPPLANE_RGBX888:
		bdw_pixel_formats_index = 5;
		break;

	default:
		break;
	}

	return bdw_pixel_formats_index;
}

static int skl_format_to_drm(int format, bool rgb_order, bool alpha,
	int yuv_order)
{
	int skl_pixel_formats_index = 12;

	switch (format) {
	case PLANE_CTL_FORMAT_INDEXED:
		skl_pixel_formats_index = 4;
		break;
	case PLANE_CTL_FORMAT_RGB_565:
		skl_pixel_formats_index = 5;
		break;
	case PLANE_CTL_FORMAT_XRGB_8888:
		if (rgb_order)
			skl_pixel_formats_index = alpha ? 6 : 7;
		else
			skl_pixel_formats_index = alpha ? 8 : 9;
		break;
	case PLANE_CTL_FORMAT_XRGB_2101010:
		skl_pixel_formats_index = rgb_order ? 10 : 11;
		break;
	case PLANE_CTL_FORMAT_YUV422:
		skl_pixel_formats_index = yuv_order >> 16;
		if (skl_pixel_formats_index > 3)
			return -EINVAL;
		break;

	default:
		break;
	}

	return skl_pixel_formats_index;
}

static u32 intel_vgpu_get_stride(struct intel_vgpu *vgpu, int pipe,
	u32 tiled, int stride_mask, int bpp)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;

	u32 stride_reg = vgpu_vreg_t(vgpu, DSPSTRIDE(pipe)) & stride_mask;
	u32 stride = stride_reg;

	if (IS_SKYLAKE(dev_priv)
		|| IS_KABYLAKE(dev_priv)
		|| IS_BROXTON(dev_priv)) {
		switch (tiled) {
		case PLANE_CTL_TILED_LINEAR:
			stride = stride_reg * 64;
			break;
		case PLANE_CTL_TILED_X:
			stride = stride_reg * 512;
			break;
		case PLANE_CTL_TILED_Y:
			stride = stride_reg * 128;
			break;
		case PLANE_CTL_TILED_YF:
			if (bpp == 8)
				stride = stride_reg * 64;
			else if (bpp == 16 || bpp == 32 || bpp == 64)
				stride = stride_reg * 128;
			else
				gvt_dbg_core("skl: unsupported bpp:%d\n", bpp);
			break;
		default:
			gvt_dbg_core("skl: unsupported tile format:%x\n",
				tiled);
		}
	}

	return stride;
}

static int get_active_pipe(struct intel_vgpu *vgpu)
{
	int i;

	for (i = 0; i < I915_MAX_PIPES; i++)
		if (pipe_is_enabled(vgpu, i))
			break;

	return i;
}

/**
 * intel_vgpu_decode_primary_plane - Decode primary plane
 * @vgpu: input vgpu
 * @plane: primary plane to save decoded info
 * This function is called for decoding plane
 *
 * Returns:
 * 0 on success, non-zero if failed.
 */
int intel_vgpu_decode_primary_plane(struct intel_vgpu *vgpu,
	struct intel_vgpu_primary_plane_format *plane)
{
	u32 val, fmt;
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	int pipe;

	pipe = get_active_pipe(vgpu);
	if (pipe >= I915_MAX_PIPES)
		return -ENODEV;

	val = vgpu_vreg_t(vgpu, DSPCNTR(pipe));
	plane->enabled = !!(val & DISPLAY_PLANE_ENABLE);
	if (!plane->enabled)
		return -ENODEV;

	if (IS_SKYLAKE(dev_priv)
		|| IS_KABYLAKE(dev_priv)
		|| IS_BROXTON(dev_priv)) {
		plane->tiled = (val & PLANE_CTL_TILED_MASK) >>
		_PLANE_CTL_TILED_SHIFT;
		fmt = skl_format_to_drm(
			val & PLANE_CTL_FORMAT_MASK,
			val & PLANE_CTL_ORDER_RGBX,
			val & PLANE_CTL_ALPHA_MASK,
			val & PLANE_CTL_YUV422_ORDER_MASK);

		if (fmt >= ARRAY_SIZE(skl_pixel_formats)) {
			gvt_vgpu_err("Out-of-bounds pixel format index\n");
			return -EINVAL;
		}

		plane->bpp = skl_pixel_formats[fmt].bpp;
		plane->drm_format = skl_pixel_formats[fmt].drm_format;
	} else {
		plane->tiled = !!(val & DISPPLANE_TILED);
		fmt = bdw_format_to_drm(val & DISPPLANE_PIXFORMAT_MASK);
		plane->bpp = bdw_pixel_formats[fmt].bpp;
		plane->drm_format = bdw_pixel_formats[fmt].drm_format;
	}

	if (!plane->bpp) {
		gvt_vgpu_err("Non-supported pixel format (0x%x)\n", fmt);
		return -EINVAL;
	}

	plane->hw_format = fmt;

	plane->base = vgpu_vreg_t(vgpu, DSPSURF(pipe)) & I915_GTT_PAGE_MASK;
	if (!intel_gvt_ggtt_validate_range(vgpu, plane->base, 0))
		return  -EINVAL;

	plane->base_gpa = intel_vgpu_gma_to_gpa(vgpu->gtt.ggtt_mm, plane->base);
	if (plane->base_gpa == INTEL_GVT_INVALID_ADDR) {
		gvt_vgpu_err("Translate primary plane gma 0x%x to gpa fail\n",
				plane->base);
		return  -EINVAL;
	}

	plane->stride = intel_vgpu_get_stride(vgpu, pipe, (plane->tiled << 10),
		(IS_SKYLAKE(dev_priv)
		|| IS_KABYLAKE(dev_priv)
		|| IS_BROXTON(dev_priv)) ?
			(_PRI_PLANE_STRIDE_MASK >> 6) :
				_PRI_PLANE_STRIDE_MASK, plane->bpp);

	plane->width = (vgpu_vreg_t(vgpu, PIPESRC(pipe)) & _PIPE_H_SRCSZ_MASK) >>
		_PIPE_H_SRCSZ_SHIFT;
	plane->width += 1;
	plane->height = (vgpu_vreg_t(vgpu, PIPESRC(pipe)) &
			_PIPE_V_SRCSZ_MASK) >> _PIPE_V_SRCSZ_SHIFT;
	plane->height += 1;	/* raw height is one minus the real value */

	val = vgpu_vreg_t(vgpu, DSPTILEOFF(pipe));
	plane->x_offset = (val & _PRI_PLANE_X_OFF_MASK) >>
		_PRI_PLANE_X_OFF_SHIFT;
	plane->y_offset = (val & _PRI_PLANE_Y_OFF_MASK) >>
		_PRI_PLANE_Y_OFF_SHIFT;

	return 0;
}

#define CURSOR_FORMAT_NUM	(1 << 6)
struct cursor_mode_format {
	int	drm_format;	/* Pixel format in DRM definition */
	u8	bpp;		/* Bits per pixel; 0 indicates invalid */
	u32	width;		/* In pixel */
	u32	height;		/* In lines */
	char	*desc;		/* The description */
};

static struct cursor_mode_format cursor_pixel_formats[] = {
	{DRM_FORMAT_ARGB8888, 32, 128, 128, "128x128 32bpp ARGB"},
	{DRM_FORMAT_ARGB8888, 32, 256, 256, "256x256 32bpp ARGB"},
	{DRM_FORMAT_ARGB8888, 32, 64, 64, "64x64 32bpp ARGB"},
	{DRM_FORMAT_ARGB8888, 32, 64, 64, "64x64 32bpp ARGB"},

	/* non-supported format has bpp default to 0 */
	{0, 0, 0, 0, NULL},
};

static int cursor_mode_to_drm(int mode)
{
	int cursor_pixel_formats_index = 4;

	switch (mode) {
	case MCURSOR_MODE_128_ARGB_AX:
		cursor_pixel_formats_index = 0;
		break;
	case MCURSOR_MODE_256_ARGB_AX:
		cursor_pixel_formats_index = 1;
		break;
	case MCURSOR_MODE_64_ARGB_AX:
		cursor_pixel_formats_index = 2;
		break;
	case MCURSOR_MODE_64_32B_AX:
		cursor_pixel_formats_index = 3;
		break;

	default:
		break;
	}

	return cursor_pixel_formats_index;
}

/**
 * intel_vgpu_decode_cursor_plane - Decode sprite plane
 * @vgpu: input vgpu
 * @plane: cursor plane to save decoded info
 * This function is called for decoding plane
 *
 * Returns:
 * 0 on success, non-zero if failed.
 */
int intel_vgpu_decode_cursor_plane(struct intel_vgpu *vgpu,
	struct intel_vgpu_cursor_plane_format *plane)
{
	u32 val, mode, index;
	u32 alpha_plane, alpha_force;
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	int pipe;

	pipe = get_active_pipe(vgpu);
	if (pipe >= I915_MAX_PIPES)
		return -ENODEV;

	val = vgpu_vreg_t(vgpu, CURCNTR(pipe));
	mode = val & MCURSOR_MODE;
	plane->enabled = (mode != MCURSOR_MODE_DISABLE);
	if (!plane->enabled)
		return -ENODEV;

	index = cursor_mode_to_drm(mode);

	if (!cursor_pixel_formats[index].bpp) {
		gvt_vgpu_err("Non-supported cursor mode (0x%x)\n", mode);
		return -EINVAL;
	}
	plane->mode = mode;
	plane->bpp = cursor_pixel_formats[index].bpp;
	plane->drm_format = cursor_pixel_formats[index].drm_format;
	plane->width = cursor_pixel_formats[index].width;
	plane->height = cursor_pixel_formats[index].height;

	alpha_plane = (val & _CURSOR_ALPHA_PLANE_MASK) >>
				_CURSOR_ALPHA_PLANE_SHIFT;
	alpha_force = (val & _CURSOR_ALPHA_FORCE_MASK) >>
				_CURSOR_ALPHA_FORCE_SHIFT;
	if (alpha_plane || alpha_force)
		gvt_dbg_core("alpha_plane=0x%x, alpha_force=0x%x\n",
			alpha_plane, alpha_force);

	plane->base = vgpu_vreg_t(vgpu, CURBASE(pipe)) & I915_GTT_PAGE_MASK;
	if (!intel_gvt_ggtt_validate_range(vgpu, plane->base, 0))
		return  -EINVAL;

	plane->base_gpa = intel_vgpu_gma_to_gpa(vgpu->gtt.ggtt_mm, plane->base);
	if (plane->base_gpa == INTEL_GVT_INVALID_ADDR) {
		gvt_vgpu_err("Translate cursor plane gma 0x%x to gpa fail\n",
				plane->base);
		return  -EINVAL;
	}

	val = vgpu_vreg_t(vgpu, CURPOS(pipe));
	plane->x_pos = (val & _CURSOR_POS_X_MASK) >> _CURSOR_POS_X_SHIFT;
	plane->x_sign = (val & _CURSOR_SIGN_X_MASK) >> _CURSOR_SIGN_X_SHIFT;
	plane->y_pos = (val & _CURSOR_POS_Y_MASK) >> _CURSOR_POS_Y_SHIFT;
	plane->y_sign = (val & _CURSOR_SIGN_Y_MASK) >> _CURSOR_SIGN_Y_SHIFT;

	plane->x_hot = vgpu_vreg_t(vgpu, vgtif_reg(cursor_x_hot));
	plane->y_hot = vgpu_vreg_t(vgpu, vgtif_reg(cursor_y_hot));
	return 0;
}

#define SPRITE_FORMAT_NUM	(1 << 3)

static struct pixel_format sprite_pixel_formats[SPRITE_FORMAT_NUM] = {
	[0x0] = {DRM_FORMAT_YUV422, 16, "YUV 16-bit 4:2:2 packed"},
	[0x1] = {DRM_FORMAT_XRGB2101010, 32, "RGB 32-bit 2:10:10:10"},
	[0x2] = {DRM_FORMAT_XRGB8888, 32, "RGB 32-bit 8:8:8:8"},
	[0x4] = {DRM_FORMAT_AYUV, 32,
		"YUV 32-bit 4:4:4 packed (8:8:8:8 MSB-X:Y:U:V)"},
};

/**
 * intel_vgpu_decode_sprite_plane - Decode sprite plane
 * @vgpu: input vgpu
 * @plane: sprite plane to save decoded info
 * This function is called for decoding plane
 *
 * Returns:
 * 0 on success, non-zero if failed.
 */
int intel_vgpu_decode_sprite_plane(struct intel_vgpu *vgpu,
	struct intel_vgpu_sprite_plane_format *plane)
{
	u32 val, fmt;
	u32 color_order, yuv_order;
	int drm_format;
	int pipe;

	pipe = get_active_pipe(vgpu);
	if (pipe >= I915_MAX_PIPES)
		return -ENODEV;

	val = vgpu_vreg_t(vgpu, SPRCTL(pipe));
	plane->enabled = !!(val & SPRITE_ENABLE);
	if (!plane->enabled)
		return -ENODEV;

	plane->tiled = !!(val & SPRITE_TILED);
	color_order = !!(val & SPRITE_RGB_ORDER_RGBX);
	yuv_order = (val & SPRITE_YUV_BYTE_ORDER_MASK) >>
				_SPRITE_YUV_ORDER_SHIFT;

	fmt = (val & SPRITE_PIXFORMAT_MASK) >> _SPRITE_FMT_SHIFT;
	if (!sprite_pixel_formats[fmt].bpp) {
		gvt_vgpu_err("Non-supported pixel format (0x%x)\n", fmt);
		return -EINVAL;
	}
	plane->hw_format = fmt;
	plane->bpp = sprite_pixel_formats[fmt].bpp;
	drm_format = sprite_pixel_formats[fmt].drm_format;

	/* Order of RGB values in an RGBxxx buffer may be ordered RGB or
	 * BGR depending on the state of the color_order field
	 */
	if (!color_order) {
		if (drm_format == DRM_FORMAT_XRGB2101010)
			drm_format = DRM_FORMAT_XBGR2101010;
		else if (drm_format == DRM_FORMAT_XRGB8888)
			drm_format = DRM_FORMAT_XBGR8888;
	}

	if (drm_format == DRM_FORMAT_YUV422) {
		switch (yuv_order) {
		case 0:
			drm_format = DRM_FORMAT_YUYV;
			break;
		case 1:
			drm_format = DRM_FORMAT_UYVY;
			break;
		case 2:
			drm_format = DRM_FORMAT_YVYU;
			break;
		case 3:
			drm_format = DRM_FORMAT_VYUY;
			break;
		default:
			/* yuv_order has only 2 bits */
			break;
		}
	}

	plane->drm_format = drm_format;

	plane->base = vgpu_vreg_t(vgpu, SPRSURF(pipe)) & I915_GTT_PAGE_MASK;
	if (!intel_gvt_ggtt_validate_range(vgpu, plane->base, 0))
		return  -EINVAL;

	plane->base_gpa = intel_vgpu_gma_to_gpa(vgpu->gtt.ggtt_mm, plane->base);
	if (plane->base_gpa == INTEL_GVT_INVALID_ADDR) {
		gvt_vgpu_err("Translate sprite plane gma 0x%x to gpa fail\n",
				plane->base);
		return  -EINVAL;
	}

	plane->stride = vgpu_vreg_t(vgpu, SPRSTRIDE(pipe)) &
				_SPRITE_STRIDE_MASK;

	val = vgpu_vreg_t(vgpu, SPRSIZE(pipe));
	plane->height = (val & _SPRITE_SIZE_HEIGHT_MASK) >>
		_SPRITE_SIZE_HEIGHT_SHIFT;
	plane->width = (val & _SPRITE_SIZE_WIDTH_MASK) >>
		_SPRITE_SIZE_WIDTH_SHIFT;
	plane->height += 1;	/* raw height is one minus the real value */
	plane->width += 1;	/* raw width is one minus the real value */

	val = vgpu_vreg_t(vgpu, SPRPOS(pipe));
	plane->x_pos = (val & _SPRITE_POS_X_MASK) >> _SPRITE_POS_X_SHIFT;
	plane->y_pos = (val & _SPRITE_POS_Y_MASK) >> _SPRITE_POS_Y_SHIFT;

	val = vgpu_vreg_t(vgpu, SPROFFSET(pipe));
	plane->x_offset = (val & _SPRITE_OFFSET_START_X_MASK) >>
			   _SPRITE_OFFSET_START_X_SHIFT;
	plane->y_offset = (val & _SPRITE_OFFSET_START_Y_MASK) >>
			   _SPRITE_OFFSET_START_Y_SHIFT;

	return 0;
}
