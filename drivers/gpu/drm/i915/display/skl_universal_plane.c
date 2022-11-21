// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fourcc.h>

#include "i915_drv.h"
#include "intel_atomic_plane.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_fb.h"
#include "intel_fbc.h"
#include "intel_psr.h"
#include "intel_sprite.h"
#include "skl_scaler.h"
#include "skl_universal_plane.h"
#include "skl_watermark.h"
#include "pxp/intel_pxp.h"

static const u32 skl_plane_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_XRGB16161616F,
	DRM_FORMAT_XBGR16161616F,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_XYUV8888,
};

static const u32 skl_planar_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_XRGB16161616F,
	DRM_FORMAT_XBGR16161616F,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_NV12,
	DRM_FORMAT_XYUV8888,
};

static const u32 glk_planar_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_XRGB16161616F,
	DRM_FORMAT_XBGR16161616F,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_NV12,
	DRM_FORMAT_XYUV8888,
	DRM_FORMAT_P010,
	DRM_FORMAT_P012,
	DRM_FORMAT_P016,
};

static const u32 icl_sdr_y_plane_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_Y210,
	DRM_FORMAT_Y212,
	DRM_FORMAT_Y216,
	DRM_FORMAT_XYUV8888,
	DRM_FORMAT_XVYU2101010,
	DRM_FORMAT_XVYU12_16161616,
	DRM_FORMAT_XVYU16161616,
};

static const u32 icl_sdr_uv_plane_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_NV12,
	DRM_FORMAT_P010,
	DRM_FORMAT_P012,
	DRM_FORMAT_P016,
	DRM_FORMAT_Y210,
	DRM_FORMAT_Y212,
	DRM_FORMAT_Y216,
	DRM_FORMAT_XYUV8888,
	DRM_FORMAT_XVYU2101010,
	DRM_FORMAT_XVYU12_16161616,
	DRM_FORMAT_XVYU16161616,
};

static const u32 icl_hdr_plane_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_XRGB16161616F,
	DRM_FORMAT_XBGR16161616F,
	DRM_FORMAT_ARGB16161616F,
	DRM_FORMAT_ABGR16161616F,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_NV12,
	DRM_FORMAT_P010,
	DRM_FORMAT_P012,
	DRM_FORMAT_P016,
	DRM_FORMAT_Y210,
	DRM_FORMAT_Y212,
	DRM_FORMAT_Y216,
	DRM_FORMAT_XYUV8888,
	DRM_FORMAT_XVYU2101010,
	DRM_FORMAT_XVYU12_16161616,
	DRM_FORMAT_XVYU16161616,
};

int skl_format_to_fourcc(int format, bool rgb_order, bool alpha)
{
	switch (format) {
	case PLANE_CTL_FORMAT_RGB_565:
		return DRM_FORMAT_RGB565;
	case PLANE_CTL_FORMAT_NV12:
		return DRM_FORMAT_NV12;
	case PLANE_CTL_FORMAT_XYUV:
		return DRM_FORMAT_XYUV8888;
	case PLANE_CTL_FORMAT_P010:
		return DRM_FORMAT_P010;
	case PLANE_CTL_FORMAT_P012:
		return DRM_FORMAT_P012;
	case PLANE_CTL_FORMAT_P016:
		return DRM_FORMAT_P016;
	case PLANE_CTL_FORMAT_Y210:
		return DRM_FORMAT_Y210;
	case PLANE_CTL_FORMAT_Y212:
		return DRM_FORMAT_Y212;
	case PLANE_CTL_FORMAT_Y216:
		return DRM_FORMAT_Y216;
	case PLANE_CTL_FORMAT_Y410:
		return DRM_FORMAT_XVYU2101010;
	case PLANE_CTL_FORMAT_Y412:
		return DRM_FORMAT_XVYU12_16161616;
	case PLANE_CTL_FORMAT_Y416:
		return DRM_FORMAT_XVYU16161616;
	default:
	case PLANE_CTL_FORMAT_XRGB_8888:
		if (rgb_order) {
			if (alpha)
				return DRM_FORMAT_ABGR8888;
			else
				return DRM_FORMAT_XBGR8888;
		} else {
			if (alpha)
				return DRM_FORMAT_ARGB8888;
			else
				return DRM_FORMAT_XRGB8888;
		}
	case PLANE_CTL_FORMAT_XRGB_2101010:
		if (rgb_order) {
			if (alpha)
				return DRM_FORMAT_ABGR2101010;
			else
				return DRM_FORMAT_XBGR2101010;
		} else {
			if (alpha)
				return DRM_FORMAT_ARGB2101010;
			else
				return DRM_FORMAT_XRGB2101010;
		}
	case PLANE_CTL_FORMAT_XRGB_16161616F:
		if (rgb_order) {
			if (alpha)
				return DRM_FORMAT_ABGR16161616F;
			else
				return DRM_FORMAT_XBGR16161616F;
		} else {
			if (alpha)
				return DRM_FORMAT_ARGB16161616F;
			else
				return DRM_FORMAT_XRGB16161616F;
		}
	}
}

static u8 icl_nv12_y_plane_mask(struct drm_i915_private *i915)
{
	if (DISPLAY_VER(i915) >= 13 || HAS_D12_PLANE_MINIMIZATION(i915))
		return BIT(PLANE_SPRITE2) | BIT(PLANE_SPRITE3);
	else
		return BIT(PLANE_SPRITE4) | BIT(PLANE_SPRITE5);
}

bool icl_is_nv12_y_plane(struct drm_i915_private *dev_priv,
			 enum plane_id plane_id)
{
	return DISPLAY_VER(dev_priv) >= 11 &&
		icl_nv12_y_plane_mask(dev_priv) & BIT(plane_id);
}

bool icl_is_hdr_plane(struct drm_i915_private *dev_priv, enum plane_id plane_id)
{
	return DISPLAY_VER(dev_priv) >= 11 &&
		icl_hdr_plane_mask() & BIT(plane_id);
}

static int icl_plane_min_cdclk(const struct intel_crtc_state *crtc_state,
			       const struct intel_plane_state *plane_state)
{
	unsigned int pixel_rate = intel_plane_pixel_rate(crtc_state, plane_state);

	/* two pixels per clock */
	return DIV_ROUND_UP(pixel_rate, 2);
}

static void
glk_plane_ratio(const struct intel_plane_state *plane_state,
		unsigned int *num, unsigned int *den)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;

	if (fb->format->cpp[0] == 8) {
		*num = 10;
		*den = 8;
	} else {
		*num = 1;
		*den = 1;
	}
}

static int glk_plane_min_cdclk(const struct intel_crtc_state *crtc_state,
			       const struct intel_plane_state *plane_state)
{
	unsigned int pixel_rate = intel_plane_pixel_rate(crtc_state, plane_state);
	unsigned int num, den;

	glk_plane_ratio(plane_state, &num, &den);

	/* two pixels per clock */
	return DIV_ROUND_UP(pixel_rate * num, 2 * den);
}

static void
skl_plane_ratio(const struct intel_plane_state *plane_state,
		unsigned int *num, unsigned int *den)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;

	if (fb->format->cpp[0] == 8) {
		*num = 9;
		*den = 8;
	} else {
		*num = 1;
		*den = 1;
	}
}

static int skl_plane_min_cdclk(const struct intel_crtc_state *crtc_state,
			       const struct intel_plane_state *plane_state)
{
	unsigned int pixel_rate = intel_plane_pixel_rate(crtc_state, plane_state);
	unsigned int num, den;

	skl_plane_ratio(plane_state, &num, &den);

	return DIV_ROUND_UP(pixel_rate * num, den);
}

static int skl_plane_max_width(const struct drm_framebuffer *fb,
			       int color_plane,
			       unsigned int rotation)
{
	int cpp = fb->format->cpp[color_plane];

	switch (fb->modifier) {
	case DRM_FORMAT_MOD_LINEAR:
	case I915_FORMAT_MOD_X_TILED:
		/*
		 * Validated limit is 4k, but has 5k should
		 * work apart from the following features:
		 * - Ytile (already limited to 4k)
		 * - FP16 (already limited to 4k)
		 * - render compression (already limited to 4k)
		 * - KVMR sprite and cursor (don't care)
		 * - horizontal panning (TODO verify this)
		 * - pipe and plane scaling (TODO verify this)
		 */
		if (cpp == 8)
			return 4096;
		else
			return 5120;
	case I915_FORMAT_MOD_Y_TILED_CCS:
	case I915_FORMAT_MOD_Yf_TILED_CCS:
	case I915_FORMAT_MOD_Y_TILED_GEN12_MC_CCS:
		/* FIXME AUX plane? */
	case I915_FORMAT_MOD_Y_TILED:
	case I915_FORMAT_MOD_Yf_TILED:
		if (cpp == 8)
			return 2048;
		else
			return 4096;
	default:
		MISSING_CASE(fb->modifier);
		return 2048;
	}
}

static int glk_plane_max_width(const struct drm_framebuffer *fb,
			       int color_plane,
			       unsigned int rotation)
{
	int cpp = fb->format->cpp[color_plane];

	switch (fb->modifier) {
	case DRM_FORMAT_MOD_LINEAR:
	case I915_FORMAT_MOD_X_TILED:
		if (cpp == 8)
			return 4096;
		else
			return 5120;
	case I915_FORMAT_MOD_Y_TILED_CCS:
	case I915_FORMAT_MOD_Yf_TILED_CCS:
		/* FIXME AUX plane? */
	case I915_FORMAT_MOD_Y_TILED:
	case I915_FORMAT_MOD_Yf_TILED:
		if (cpp == 8)
			return 2048;
		else
			return 5120;
	default:
		MISSING_CASE(fb->modifier);
		return 2048;
	}
}

static int icl_plane_min_width(const struct drm_framebuffer *fb,
			       int color_plane,
			       unsigned int rotation)
{
	/* Wa_14011264657, Wa_14011050563: gen11+ */
	switch (fb->format->format) {
	case DRM_FORMAT_C8:
		return 18;
	case DRM_FORMAT_RGB565:
		return 10;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_XVYU2101010:
	case DRM_FORMAT_Y212:
	case DRM_FORMAT_Y216:
		return 6;
	case DRM_FORMAT_NV12:
		return 20;
	case DRM_FORMAT_P010:
	case DRM_FORMAT_P012:
	case DRM_FORMAT_P016:
		return 12;
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_ABGR16161616F:
	case DRM_FORMAT_XVYU12_16161616:
	case DRM_FORMAT_XVYU16161616:
		return 4;
	default:
		return 1;
	}
}

static int icl_hdr_plane_max_width(const struct drm_framebuffer *fb,
				   int color_plane,
				   unsigned int rotation)
{
	if (intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier))
		return 4096;
	else
		return 5120;
}

static int icl_sdr_plane_max_width(const struct drm_framebuffer *fb,
				   int color_plane,
				   unsigned int rotation)
{
	return 5120;
}

static int skl_plane_max_height(const struct drm_framebuffer *fb,
				int color_plane,
				unsigned int rotation)
{
	return 4096;
}

static int icl_plane_max_height(const struct drm_framebuffer *fb,
				int color_plane,
				unsigned int rotation)
{
	return 4320;
}

static unsigned int
skl_plane_max_stride(struct intel_plane *plane,
		     u32 pixel_format, u64 modifier,
		     unsigned int rotation)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
	const struct drm_format_info *info = drm_format_info(pixel_format);
	int cpp = info->cpp[0];
	int max_horizontal_pixels = 8192;
	int max_stride_bytes;

	if (DISPLAY_VER(i915) >= 13) {
		/*
		 * The stride in bytes must not exceed of the size
		 * of 128K bytes. For pixel formats of 64bpp will allow
		 * for a 16K pixel surface.
		 */
		max_stride_bytes = 131072;
		if (cpp == 8)
			max_horizontal_pixels = 16384;
		else
			max_horizontal_pixels = 65536;
	} else {
		/*
		 * "The stride in bytes must not exceed the
		 * of the size of 8K pixels and 32K bytes."
		 */
		max_stride_bytes = 32768;
	}

	if (drm_rotation_90_or_270(rotation))
		return min(max_horizontal_pixels, max_stride_bytes / cpp);
	else
		return min(max_horizontal_pixels * cpp, max_stride_bytes);
}


/* Preoffset values for YUV to RGB Conversion */
#define PREOFF_YUV_TO_RGB_HI		0x1800
#define PREOFF_YUV_TO_RGB_ME		0x0000
#define PREOFF_YUV_TO_RGB_LO		0x1800

#define  ROFF(x)          (((x) & 0xffff) << 16)
#define  GOFF(x)          (((x) & 0xffff) << 0)
#define  BOFF(x)          (((x) & 0xffff) << 16)

/*
 * Programs the input color space conversion stage for ICL HDR planes.
 * Note that it is assumed that this stage always happens after YUV
 * range correction. Thus, the input to this stage is assumed to be
 * in full-range YCbCr.
 */
static void
icl_program_input_csc(struct intel_plane *plane,
		      const struct intel_crtc_state *crtc_state,
		      const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;
	enum plane_id plane_id = plane->id;

	static const u16 input_csc_matrix[][9] = {
		/*
		 * BT.601 full range YCbCr -> full range RGB
		 * The matrix required is :
		 * [1.000, 0.000, 1.371,
		 *  1.000, -0.336, -0.698,
		 *  1.000, 1.732, 0.0000]
		 */
		[DRM_COLOR_YCBCR_BT601] = {
			0x7AF8, 0x7800, 0x0,
			0x8B28, 0x7800, 0x9AC0,
			0x0, 0x7800, 0x7DD8,
		},
		/*
		 * BT.709 full range YCbCr -> full range RGB
		 * The matrix required is :
		 * [1.000, 0.000, 1.574,
		 *  1.000, -0.187, -0.468,
		 *  1.000, 1.855, 0.0000]
		 */
		[DRM_COLOR_YCBCR_BT709] = {
			0x7C98, 0x7800, 0x0,
			0x9EF8, 0x7800, 0xAC00,
			0x0, 0x7800,  0x7ED8,
		},
		/*
		 * BT.2020 full range YCbCr -> full range RGB
		 * The matrix required is :
		 * [1.000, 0.000, 1.474,
		 *  1.000, -0.1645, -0.5713,
		 *  1.000, 1.8814, 0.0000]
		 */
		[DRM_COLOR_YCBCR_BT2020] = {
			0x7BC8, 0x7800, 0x0,
			0x8928, 0x7800, 0xAA88,
			0x0, 0x7800, 0x7F10,
		},
	};
	const u16 *csc = input_csc_matrix[plane_state->hw.color_encoding];

	intel_de_write_fw(dev_priv, PLANE_INPUT_CSC_COEFF(pipe, plane_id, 0),
			  ROFF(csc[0]) | GOFF(csc[1]));
	intel_de_write_fw(dev_priv, PLANE_INPUT_CSC_COEFF(pipe, plane_id, 1),
			  BOFF(csc[2]));
	intel_de_write_fw(dev_priv, PLANE_INPUT_CSC_COEFF(pipe, plane_id, 2),
			  ROFF(csc[3]) | GOFF(csc[4]));
	intel_de_write_fw(dev_priv, PLANE_INPUT_CSC_COEFF(pipe, plane_id, 3),
			  BOFF(csc[5]));
	intel_de_write_fw(dev_priv, PLANE_INPUT_CSC_COEFF(pipe, plane_id, 4),
			  ROFF(csc[6]) | GOFF(csc[7]));
	intel_de_write_fw(dev_priv, PLANE_INPUT_CSC_COEFF(pipe, plane_id, 5),
			  BOFF(csc[8]));

	intel_de_write_fw(dev_priv, PLANE_INPUT_CSC_PREOFF(pipe, plane_id, 0),
			  PREOFF_YUV_TO_RGB_HI);
	intel_de_write_fw(dev_priv, PLANE_INPUT_CSC_PREOFF(pipe, plane_id, 1),
			  PREOFF_YUV_TO_RGB_ME);
	intel_de_write_fw(dev_priv, PLANE_INPUT_CSC_PREOFF(pipe, plane_id, 2),
			  PREOFF_YUV_TO_RGB_LO);
	intel_de_write_fw(dev_priv,
			  PLANE_INPUT_CSC_POSTOFF(pipe, plane_id, 0), 0x0);
	intel_de_write_fw(dev_priv,
			  PLANE_INPUT_CSC_POSTOFF(pipe, plane_id, 1), 0x0);
	intel_de_write_fw(dev_priv,
			  PLANE_INPUT_CSC_POSTOFF(pipe, plane_id, 2), 0x0);
}

static unsigned int skl_plane_stride_mult(const struct drm_framebuffer *fb,
					  int color_plane, unsigned int rotation)
{
	/*
	 * The stride is either expressed as a multiple of 64 bytes chunks for
	 * linear buffers or in number of tiles for tiled buffers.
	 */
	if (is_surface_linear(fb, color_plane))
		return 64;
	else if (drm_rotation_90_or_270(rotation))
		return intel_tile_height(fb, color_plane);
	else
		return intel_tile_width_bytes(fb, color_plane);
}

static u32 skl_plane_stride(const struct intel_plane_state *plane_state,
			    int color_plane)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rotation = plane_state->hw.rotation;
	u32 stride = plane_state->view.color_plane[color_plane].scanout_stride;

	if (color_plane >= fb->format->num_planes)
		return 0;

	return stride / skl_plane_stride_mult(fb, color_plane, rotation);
}

static void
skl_plane_disable_arm(struct intel_plane *plane,
		      const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum plane_id plane_id = plane->id;
	enum pipe pipe = plane->pipe;

	skl_write_plane_wm(plane, crtc_state);

	intel_de_write_fw(dev_priv, PLANE_CTL(pipe, plane_id), 0);
	intel_de_write_fw(dev_priv, PLANE_SURF(pipe, plane_id), 0);
}

static void
icl_plane_disable_arm(struct intel_plane *plane,
		      const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum plane_id plane_id = plane->id;
	enum pipe pipe = plane->pipe;

	if (icl_is_hdr_plane(dev_priv, plane_id))
		intel_de_write_fw(dev_priv, PLANE_CUS_CTL(pipe, plane_id), 0);

	skl_write_plane_wm(plane, crtc_state);

	intel_psr2_disable_plane_sel_fetch(plane, crtc_state);
	intel_de_write_fw(dev_priv, PLANE_CTL(pipe, plane_id), 0);
	intel_de_write_fw(dev_priv, PLANE_SURF(pipe, plane_id), 0);
}

static bool
skl_plane_get_hw_state(struct intel_plane *plane,
		       enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum intel_display_power_domain power_domain;
	enum plane_id plane_id = plane->id;
	intel_wakeref_t wakeref;
	bool ret;

	power_domain = POWER_DOMAIN_PIPE(plane->pipe);
	wakeref = intel_display_power_get_if_enabled(dev_priv, power_domain);
	if (!wakeref)
		return false;

	ret = intel_de_read(dev_priv, PLANE_CTL(plane->pipe, plane_id)) & PLANE_CTL_ENABLE;

	*pipe = plane->pipe;

	intel_display_power_put(dev_priv, power_domain, wakeref);

	return ret;
}

static u32 skl_plane_ctl_format(u32 pixel_format)
{
	switch (pixel_format) {
	case DRM_FORMAT_C8:
		return PLANE_CTL_FORMAT_INDEXED;
	case DRM_FORMAT_RGB565:
		return PLANE_CTL_FORMAT_RGB_565;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return PLANE_CTL_FORMAT_XRGB_8888 | PLANE_CTL_ORDER_RGBX;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		return PLANE_CTL_FORMAT_XRGB_8888;
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
		return PLANE_CTL_FORMAT_XRGB_2101010 | PLANE_CTL_ORDER_RGBX;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
		return PLANE_CTL_FORMAT_XRGB_2101010;
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ABGR16161616F:
		return PLANE_CTL_FORMAT_XRGB_16161616F | PLANE_CTL_ORDER_RGBX;
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_ARGB16161616F:
		return PLANE_CTL_FORMAT_XRGB_16161616F;
	case DRM_FORMAT_XYUV8888:
		return PLANE_CTL_FORMAT_XYUV;
	case DRM_FORMAT_YUYV:
		return PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_ORDER_YUYV;
	case DRM_FORMAT_YVYU:
		return PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_ORDER_YVYU;
	case DRM_FORMAT_UYVY:
		return PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_ORDER_UYVY;
	case DRM_FORMAT_VYUY:
		return PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_ORDER_VYUY;
	case DRM_FORMAT_NV12:
		return PLANE_CTL_FORMAT_NV12;
	case DRM_FORMAT_P010:
		return PLANE_CTL_FORMAT_P010;
	case DRM_FORMAT_P012:
		return PLANE_CTL_FORMAT_P012;
	case DRM_FORMAT_P016:
		return PLANE_CTL_FORMAT_P016;
	case DRM_FORMAT_Y210:
		return PLANE_CTL_FORMAT_Y210;
	case DRM_FORMAT_Y212:
		return PLANE_CTL_FORMAT_Y212;
	case DRM_FORMAT_Y216:
		return PLANE_CTL_FORMAT_Y216;
	case DRM_FORMAT_XVYU2101010:
		return PLANE_CTL_FORMAT_Y410;
	case DRM_FORMAT_XVYU12_16161616:
		return PLANE_CTL_FORMAT_Y412;
	case DRM_FORMAT_XVYU16161616:
		return PLANE_CTL_FORMAT_Y416;
	default:
		MISSING_CASE(pixel_format);
	}

	return 0;
}

static u32 skl_plane_ctl_alpha(const struct intel_plane_state *plane_state)
{
	if (!plane_state->hw.fb->format->has_alpha)
		return PLANE_CTL_ALPHA_DISABLE;

	switch (plane_state->hw.pixel_blend_mode) {
	case DRM_MODE_BLEND_PIXEL_NONE:
		return PLANE_CTL_ALPHA_DISABLE;
	case DRM_MODE_BLEND_PREMULTI:
		return PLANE_CTL_ALPHA_SW_PREMULTIPLY;
	case DRM_MODE_BLEND_COVERAGE:
		return PLANE_CTL_ALPHA_HW_PREMULTIPLY;
	default:
		MISSING_CASE(plane_state->hw.pixel_blend_mode);
		return PLANE_CTL_ALPHA_DISABLE;
	}
}

static u32 glk_plane_color_ctl_alpha(const struct intel_plane_state *plane_state)
{
	if (!plane_state->hw.fb->format->has_alpha)
		return PLANE_COLOR_ALPHA_DISABLE;

	switch (plane_state->hw.pixel_blend_mode) {
	case DRM_MODE_BLEND_PIXEL_NONE:
		return PLANE_COLOR_ALPHA_DISABLE;
	case DRM_MODE_BLEND_PREMULTI:
		return PLANE_COLOR_ALPHA_SW_PREMULTIPLY;
	case DRM_MODE_BLEND_COVERAGE:
		return PLANE_COLOR_ALPHA_HW_PREMULTIPLY;
	default:
		MISSING_CASE(plane_state->hw.pixel_blend_mode);
		return PLANE_COLOR_ALPHA_DISABLE;
	}
}

static u32 skl_plane_ctl_tiling(u64 fb_modifier)
{
	switch (fb_modifier) {
	case DRM_FORMAT_MOD_LINEAR:
		break;
	case I915_FORMAT_MOD_X_TILED:
		return PLANE_CTL_TILED_X;
	case I915_FORMAT_MOD_Y_TILED:
		return PLANE_CTL_TILED_Y;
	case I915_FORMAT_MOD_4_TILED:
		return PLANE_CTL_TILED_4;
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS:
		return PLANE_CTL_TILED_4 |
			PLANE_CTL_RENDER_DECOMPRESSION_ENABLE |
			PLANE_CTL_CLEAR_COLOR_DISABLE;
	case I915_FORMAT_MOD_4_TILED_DG2_MC_CCS:
		return PLANE_CTL_TILED_4 |
			PLANE_CTL_MEDIA_DECOMPRESSION_ENABLE |
			PLANE_CTL_CLEAR_COLOR_DISABLE;
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC:
		return PLANE_CTL_TILED_4 | PLANE_CTL_RENDER_DECOMPRESSION_ENABLE;
	case I915_FORMAT_MOD_Y_TILED_CCS:
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC:
		return PLANE_CTL_TILED_Y | PLANE_CTL_RENDER_DECOMPRESSION_ENABLE;
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS:
		return PLANE_CTL_TILED_Y |
		       PLANE_CTL_RENDER_DECOMPRESSION_ENABLE |
		       PLANE_CTL_CLEAR_COLOR_DISABLE;
	case I915_FORMAT_MOD_Y_TILED_GEN12_MC_CCS:
		return PLANE_CTL_TILED_Y | PLANE_CTL_MEDIA_DECOMPRESSION_ENABLE;
	case I915_FORMAT_MOD_Yf_TILED:
		return PLANE_CTL_TILED_YF;
	case I915_FORMAT_MOD_Yf_TILED_CCS:
		return PLANE_CTL_TILED_YF | PLANE_CTL_RENDER_DECOMPRESSION_ENABLE;
	default:
		MISSING_CASE(fb_modifier);
	}

	return 0;
}

static u32 skl_plane_ctl_rotate(unsigned int rotate)
{
	switch (rotate) {
	case DRM_MODE_ROTATE_0:
		break;
	/*
	 * DRM_MODE_ROTATE_ is counter clockwise to stay compatible with Xrandr
	 * while i915 HW rotation is clockwise, thats why this swapping.
	 */
	case DRM_MODE_ROTATE_90:
		return PLANE_CTL_ROTATE_270;
	case DRM_MODE_ROTATE_180:
		return PLANE_CTL_ROTATE_180;
	case DRM_MODE_ROTATE_270:
		return PLANE_CTL_ROTATE_90;
	default:
		MISSING_CASE(rotate);
	}

	return 0;
}

static u32 icl_plane_ctl_flip(unsigned int reflect)
{
	switch (reflect) {
	case 0:
		break;
	case DRM_MODE_REFLECT_X:
		return PLANE_CTL_FLIP_HORIZONTAL;
	case DRM_MODE_REFLECT_Y:
	default:
		MISSING_CASE(reflect);
	}

	return 0;
}

static u32 adlp_plane_ctl_arb_slots(const struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;

	if (intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier)) {
		switch (fb->format->cpp[0]) {
		case 2:
			return PLANE_CTL_ARB_SLOTS(1);
		default:
			return PLANE_CTL_ARB_SLOTS(0);
		}
	} else {
		switch (fb->format->cpp[0]) {
		case 8:
			return PLANE_CTL_ARB_SLOTS(3);
		case 4:
			return PLANE_CTL_ARB_SLOTS(1);
		default:
			return PLANE_CTL_ARB_SLOTS(0);
		}
	}
}

static u32 skl_plane_ctl_crtc(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	u32 plane_ctl = 0;

	if (DISPLAY_VER(dev_priv) >= 10)
		return plane_ctl;

	if (crtc_state->gamma_enable)
		plane_ctl |= PLANE_CTL_PIPE_GAMMA_ENABLE;

	if (crtc_state->csc_enable)
		plane_ctl |= PLANE_CTL_PIPE_CSC_ENABLE;

	return plane_ctl;
}

static u32 skl_plane_ctl(const struct intel_crtc_state *crtc_state,
			 const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rotation = plane_state->hw.rotation;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	u32 plane_ctl;

	plane_ctl = PLANE_CTL_ENABLE;

	if (DISPLAY_VER(dev_priv) < 10) {
		plane_ctl |= skl_plane_ctl_alpha(plane_state);
		plane_ctl |= PLANE_CTL_PLANE_GAMMA_DISABLE;

		if (plane_state->hw.color_encoding == DRM_COLOR_YCBCR_BT709)
			plane_ctl |= PLANE_CTL_YUV_TO_RGB_CSC_FORMAT_BT709;

		if (plane_state->hw.color_range == DRM_COLOR_YCBCR_FULL_RANGE)
			plane_ctl |= PLANE_CTL_YUV_RANGE_CORRECTION_DISABLE;
	}

	plane_ctl |= skl_plane_ctl_format(fb->format->format);
	plane_ctl |= skl_plane_ctl_tiling(fb->modifier);
	plane_ctl |= skl_plane_ctl_rotate(rotation & DRM_MODE_ROTATE_MASK);

	if (DISPLAY_VER(dev_priv) >= 11)
		plane_ctl |= icl_plane_ctl_flip(rotation &
						DRM_MODE_REFLECT_MASK);

	if (key->flags & I915_SET_COLORKEY_DESTINATION)
		plane_ctl |= PLANE_CTL_KEY_ENABLE_DESTINATION;
	else if (key->flags & I915_SET_COLORKEY_SOURCE)
		plane_ctl |= PLANE_CTL_KEY_ENABLE_SOURCE;

	/* Wa_22012358565:adl-p */
	if (DISPLAY_VER(dev_priv) == 13)
		plane_ctl |= adlp_plane_ctl_arb_slots(plane_state);

	return plane_ctl;
}

static u32 glk_plane_color_ctl_crtc(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	u32 plane_color_ctl = 0;

	if (DISPLAY_VER(dev_priv) >= 11)
		return plane_color_ctl;

	if (crtc_state->gamma_enable)
		plane_color_ctl |= PLANE_COLOR_PIPE_GAMMA_ENABLE;

	if (crtc_state->csc_enable)
		plane_color_ctl |= PLANE_COLOR_PIPE_CSC_ENABLE;

	return plane_color_ctl;
}

static u32 glk_plane_color_ctl(const struct intel_crtc_state *crtc_state,
			       const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	u32 plane_color_ctl = 0;

	plane_color_ctl |= PLANE_COLOR_PLANE_GAMMA_DISABLE;
	plane_color_ctl |= glk_plane_color_ctl_alpha(plane_state);

	if (fb->format->is_yuv && !icl_is_hdr_plane(dev_priv, plane->id)) {
		switch (plane_state->hw.color_encoding) {
		case DRM_COLOR_YCBCR_BT709:
			plane_color_ctl |= PLANE_COLOR_CSC_MODE_YUV709_TO_RGB709;
			break;
		case DRM_COLOR_YCBCR_BT2020:
			plane_color_ctl |=
				PLANE_COLOR_CSC_MODE_YUV2020_TO_RGB2020;
			break;
		default:
			plane_color_ctl |=
				PLANE_COLOR_CSC_MODE_YUV601_TO_RGB601;
		}
		if (plane_state->hw.color_range == DRM_COLOR_YCBCR_FULL_RANGE)
			plane_color_ctl |= PLANE_COLOR_YUV_RANGE_CORRECTION_DISABLE;
	} else if (fb->format->is_yuv) {
		plane_color_ctl |= PLANE_COLOR_INPUT_CSC_ENABLE;
		if (plane_state->hw.color_range == DRM_COLOR_YCBCR_FULL_RANGE)
			plane_color_ctl |= PLANE_COLOR_YUV_RANGE_CORRECTION_DISABLE;
	}

	if (plane_state->force_black)
		plane_color_ctl |= PLANE_COLOR_PLANE_CSC_ENABLE;

	return plane_color_ctl;
}

static u32 skl_surf_address(const struct intel_plane_state *plane_state,
			    int color_plane)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	u32 offset = plane_state->view.color_plane[color_plane].offset;

	if (intel_fb_uses_dpt(fb)) {
		/*
		 * The DPT object contains only one vma, so the VMA's offset
		 * within the DPT is always 0.
		 */
		drm_WARN_ON(&i915->drm, plane_state->dpt_vma->node.start);
		drm_WARN_ON(&i915->drm, offset & 0x1fffff);
		return offset >> 9;
	} else {
		drm_WARN_ON(&i915->drm, offset & 0xfff);
		return offset;
	}
}

static u32 skl_plane_surf(const struct intel_plane_state *plane_state,
			  int color_plane)
{
	u32 plane_surf;

	plane_surf = intel_plane_ggtt_offset(plane_state) +
		skl_surf_address(plane_state, color_plane);

	if (plane_state->decrypt)
		plane_surf |= PLANE_SURF_DECRYPT;

	return plane_surf;
}

static u32 skl_plane_aux_dist(const struct intel_plane_state *plane_state,
			      int color_plane)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	int aux_plane = skl_main_to_aux_plane(fb, color_plane);
	u32 aux_dist;

	if (!aux_plane)
		return 0;

	aux_dist = skl_surf_address(plane_state, aux_plane) -
		skl_surf_address(plane_state, color_plane);

	if (DISPLAY_VER(i915) < 12)
		aux_dist |= PLANE_AUX_STRIDE(skl_plane_stride(plane_state, aux_plane));

	return aux_dist;
}

static u32 skl_plane_keyval(const struct intel_plane_state *plane_state)
{
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;

	return key->min_value;
}

static u32 skl_plane_keymax(const struct intel_plane_state *plane_state)
{
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	u8 alpha = plane_state->hw.alpha >> 8;

	return (key->max_value & 0xffffff) | PLANE_KEYMAX_ALPHA(alpha);
}

static u32 skl_plane_keymsk(const struct intel_plane_state *plane_state)
{
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	u8 alpha = plane_state->hw.alpha >> 8;
	u32 keymsk;

	keymsk = key->channel_mask & 0x7ffffff;
	if (alpha < 0xff)
		keymsk |= PLANE_KEYMSK_ALPHA_ENABLE;

	return keymsk;
}

static void icl_plane_csc_load_black(struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
	enum plane_id plane_id = plane->id;
	enum pipe pipe = plane->pipe;

	intel_de_write_fw(i915, PLANE_CSC_COEFF(pipe, plane_id, 0), 0);
	intel_de_write_fw(i915, PLANE_CSC_COEFF(pipe, plane_id, 1), 0);

	intel_de_write_fw(i915, PLANE_CSC_COEFF(pipe, plane_id, 2), 0);
	intel_de_write_fw(i915, PLANE_CSC_COEFF(pipe, plane_id, 3), 0);

	intel_de_write_fw(i915, PLANE_CSC_COEFF(pipe, plane_id, 4), 0);
	intel_de_write_fw(i915, PLANE_CSC_COEFF(pipe, plane_id, 5), 0);

	intel_de_write_fw(i915, PLANE_CSC_PREOFF(pipe, plane_id, 0), 0);
	intel_de_write_fw(i915, PLANE_CSC_PREOFF(pipe, plane_id, 1), 0);
	intel_de_write_fw(i915, PLANE_CSC_PREOFF(pipe, plane_id, 2), 0);

	intel_de_write_fw(i915, PLANE_CSC_POSTOFF(pipe, plane_id, 0), 0);
	intel_de_write_fw(i915, PLANE_CSC_POSTOFF(pipe, plane_id, 1), 0);
	intel_de_write_fw(i915, PLANE_CSC_POSTOFF(pipe, plane_id, 2), 0);
}

static int icl_plane_color_plane(const struct intel_plane_state *plane_state)
{
	/* Program the UV plane on planar master */
	if (plane_state->planar_linked_plane && !plane_state->planar_slave)
		return 1;
	else
		return 0;
}

static void
skl_plane_update_noarm(struct intel_plane *plane,
		       const struct intel_crtc_state *crtc_state,
		       const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum plane_id plane_id = plane->id;
	enum pipe pipe = plane->pipe;
	u32 stride = skl_plane_stride(plane_state, 0);
	int crtc_x = plane_state->uapi.dst.x1;
	int crtc_y = plane_state->uapi.dst.y1;
	u32 src_w = drm_rect_width(&plane_state->uapi.src) >> 16;
	u32 src_h = drm_rect_height(&plane_state->uapi.src) >> 16;

	/* The scaler will handle the output position */
	if (plane_state->scaler_id >= 0) {
		crtc_x = 0;
		crtc_y = 0;
	}

	intel_de_write_fw(dev_priv, PLANE_STRIDE(pipe, plane_id),
			  PLANE_STRIDE_(stride));
	intel_de_write_fw(dev_priv, PLANE_POS(pipe, plane_id),
			  PLANE_POS_Y(crtc_y) | PLANE_POS_X(crtc_x));
	intel_de_write_fw(dev_priv, PLANE_SIZE(pipe, plane_id),
			  PLANE_HEIGHT(src_h - 1) | PLANE_WIDTH(src_w - 1));

	skl_write_plane_wm(plane, crtc_state);
}

static void
skl_plane_update_arm(struct intel_plane *plane,
		     const struct intel_crtc_state *crtc_state,
		     const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum plane_id plane_id = plane->id;
	enum pipe pipe = plane->pipe;
	u32 x = plane_state->view.color_plane[0].x;
	u32 y = plane_state->view.color_plane[0].y;
	u32 plane_ctl, plane_color_ctl = 0;

	plane_ctl = plane_state->ctl |
		skl_plane_ctl_crtc(crtc_state);

	if (DISPLAY_VER(dev_priv) >= 10)
		plane_color_ctl = plane_state->color_ctl |
			glk_plane_color_ctl_crtc(crtc_state);

	intel_de_write_fw(dev_priv, PLANE_KEYVAL(pipe, plane_id), skl_plane_keyval(plane_state));
	intel_de_write_fw(dev_priv, PLANE_KEYMSK(pipe, plane_id), skl_plane_keymsk(plane_state));
	intel_de_write_fw(dev_priv, PLANE_KEYMAX(pipe, plane_id), skl_plane_keymax(plane_state));

	intel_de_write_fw(dev_priv, PLANE_OFFSET(pipe, plane_id),
			  PLANE_OFFSET_Y(y) | PLANE_OFFSET_X(x));

	intel_de_write_fw(dev_priv, PLANE_AUX_DIST(pipe, plane_id),
			  skl_plane_aux_dist(plane_state, 0));

	intel_de_write_fw(dev_priv, PLANE_AUX_OFFSET(pipe, plane_id),
			  PLANE_OFFSET_Y(plane_state->view.color_plane[1].y) |
			  PLANE_OFFSET_X(plane_state->view.color_plane[1].x));

	if (DISPLAY_VER(dev_priv) >= 10)
		intel_de_write_fw(dev_priv, PLANE_COLOR_CTL(pipe, plane_id), plane_color_ctl);

	/*
	 * Enable the scaler before the plane so that we don't
	 * get a catastrophic underrun even if the two operations
	 * end up happening in two different frames.
	 *
	 * TODO: split into noarm+arm pair
	 */
	if (plane_state->scaler_id >= 0)
		skl_program_plane_scaler(plane, crtc_state, plane_state);

	/*
	 * The control register self-arms if the plane was previously
	 * disabled. Try to make the plane enable atomic by writing
	 * the control register just before the surface register.
	 */
	intel_de_write_fw(dev_priv, PLANE_CTL(pipe, plane_id), plane_ctl);
	intel_de_write_fw(dev_priv, PLANE_SURF(pipe, plane_id),
			  skl_plane_surf(plane_state, 0));
}

static void
icl_plane_update_noarm(struct intel_plane *plane,
		       const struct intel_crtc_state *crtc_state,
		       const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum plane_id plane_id = plane->id;
	enum pipe pipe = plane->pipe;
	int color_plane = icl_plane_color_plane(plane_state);
	u32 stride = skl_plane_stride(plane_state, color_plane);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	int crtc_x = plane_state->uapi.dst.x1;
	int crtc_y = plane_state->uapi.dst.y1;
	int x = plane_state->view.color_plane[color_plane].x;
	int y = plane_state->view.color_plane[color_plane].y;
	int src_w = drm_rect_width(&plane_state->uapi.src) >> 16;
	int src_h = drm_rect_height(&plane_state->uapi.src) >> 16;
	u32 plane_color_ctl;

	plane_color_ctl = plane_state->color_ctl |
		glk_plane_color_ctl_crtc(crtc_state);

	/* The scaler will handle the output position */
	if (plane_state->scaler_id >= 0) {
		crtc_x = 0;
		crtc_y = 0;
	}

	intel_de_write_fw(dev_priv, PLANE_STRIDE(pipe, plane_id),
			  PLANE_STRIDE_(stride));
	intel_de_write_fw(dev_priv, PLANE_POS(pipe, plane_id),
			  PLANE_POS_Y(crtc_y) | PLANE_POS_X(crtc_x));
	intel_de_write_fw(dev_priv, PLANE_SIZE(pipe, plane_id),
			  PLANE_HEIGHT(src_h - 1) | PLANE_WIDTH(src_w - 1));

	intel_de_write_fw(dev_priv, PLANE_KEYVAL(pipe, plane_id), skl_plane_keyval(plane_state));
	intel_de_write_fw(dev_priv, PLANE_KEYMSK(pipe, plane_id), skl_plane_keymsk(plane_state));
	intel_de_write_fw(dev_priv, PLANE_KEYMAX(pipe, plane_id), skl_plane_keymax(plane_state));

	intel_de_write_fw(dev_priv, PLANE_OFFSET(pipe, plane_id),
			  PLANE_OFFSET_Y(y) | PLANE_OFFSET_X(x));

	if (intel_fb_is_rc_ccs_cc_modifier(fb->modifier)) {
		intel_de_write_fw(dev_priv, PLANE_CC_VAL(pipe, plane_id, 0),
				  lower_32_bits(plane_state->ccval));
		intel_de_write_fw(dev_priv, PLANE_CC_VAL(pipe, plane_id, 1),
				  upper_32_bits(plane_state->ccval));
	}

	/* FLAT CCS doesn't need to program AUX_DIST */
	if (!HAS_FLAT_CCS(dev_priv))
		intel_de_write_fw(dev_priv, PLANE_AUX_DIST(pipe, plane_id),
				  skl_plane_aux_dist(plane_state, color_plane));

	if (icl_is_hdr_plane(dev_priv, plane_id))
		intel_de_write_fw(dev_priv, PLANE_CUS_CTL(pipe, plane_id),
				  plane_state->cus_ctl);

	intel_de_write_fw(dev_priv, PLANE_COLOR_CTL(pipe, plane_id), plane_color_ctl);

	if (fb->format->is_yuv && icl_is_hdr_plane(dev_priv, plane_id))
		icl_program_input_csc(plane, crtc_state, plane_state);

	skl_write_plane_wm(plane, crtc_state);

	/*
	 * FIXME: pxp session invalidation can hit any time even at time of commit
	 * or after the commit, display content will be garbage.
	 */
	if (plane_state->force_black)
		icl_plane_csc_load_black(plane);

	intel_psr2_program_plane_sel_fetch(plane, crtc_state, plane_state, color_plane);
}

static void
icl_plane_update_arm(struct intel_plane *plane,
		     const struct intel_crtc_state *crtc_state,
		     const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum plane_id plane_id = plane->id;
	enum pipe pipe = plane->pipe;
	int color_plane = icl_plane_color_plane(plane_state);
	u32 plane_ctl;

	plane_ctl = plane_state->ctl |
		skl_plane_ctl_crtc(crtc_state);

	/*
	 * Enable the scaler before the plane so that we don't
	 * get a catastrophic underrun even if the two operations
	 * end up happening in two different frames.
	 *
	 * TODO: split into noarm+arm pair
	 */
	if (plane_state->scaler_id >= 0)
		skl_program_plane_scaler(plane, crtc_state, plane_state);

	/*
	 * The control register self-arms if the plane was previously
	 * disabled. Try to make the plane enable atomic by writing
	 * the control register just before the surface register.
	 */
	intel_de_write_fw(dev_priv, PLANE_CTL(pipe, plane_id), plane_ctl);
	intel_de_write_fw(dev_priv, PLANE_SURF(pipe, plane_id),
			  skl_plane_surf(plane_state, color_plane));
}

static void
skl_plane_async_flip(struct intel_plane *plane,
		     const struct intel_crtc_state *crtc_state,
		     const struct intel_plane_state *plane_state,
		     bool async_flip)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum plane_id plane_id = plane->id;
	enum pipe pipe = plane->pipe;
	u32 plane_ctl = plane_state->ctl;

	plane_ctl |= skl_plane_ctl_crtc(crtc_state);

	if (async_flip)
		plane_ctl |= PLANE_CTL_ASYNC_FLIP;

	intel_de_write_fw(dev_priv, PLANE_CTL(pipe, plane_id), plane_ctl);
	intel_de_write_fw(dev_priv, PLANE_SURF(pipe, plane_id),
			  skl_plane_surf(plane_state, 0));
}

static bool intel_format_is_p01x(u32 format)
{
	switch (format) {
	case DRM_FORMAT_P010:
	case DRM_FORMAT_P012:
	case DRM_FORMAT_P016:
		return true;
	default:
		return false;
	}
}

static int skl_plane_check_fb(const struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rotation = plane_state->hw.rotation;

	if (!fb)
		return 0;

	if (rotation & ~(DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_180) &&
	    intel_fb_is_ccs_modifier(fb->modifier)) {
		drm_dbg_kms(&dev_priv->drm,
			    "RC support only with 0/180 degree rotation (%x)\n",
			    rotation);
		return -EINVAL;
	}

	if (rotation & DRM_MODE_REFLECT_X &&
	    fb->modifier == DRM_FORMAT_MOD_LINEAR) {
		drm_dbg_kms(&dev_priv->drm,
			    "horizontal flip is not supported with linear surface formats\n");
		return -EINVAL;
	}

	if (drm_rotation_90_or_270(rotation)) {
		if (!intel_fb_supports_90_270_rotation(to_intel_framebuffer(fb))) {
			drm_dbg_kms(&dev_priv->drm,
				    "Y/Yf tiling required for 90/270!\n");
			return -EINVAL;
		}

		/*
		 * 90/270 is not allowed with RGB64 16:16:16:16 and
		 * Indexed 8-bit. RGB 16-bit 5:6:5 is allowed gen11 onwards.
		 */
		switch (fb->format->format) {
		case DRM_FORMAT_RGB565:
			if (DISPLAY_VER(dev_priv) >= 11)
				break;
			fallthrough;
		case DRM_FORMAT_C8:
		case DRM_FORMAT_XRGB16161616F:
		case DRM_FORMAT_XBGR16161616F:
		case DRM_FORMAT_ARGB16161616F:
		case DRM_FORMAT_ABGR16161616F:
		case DRM_FORMAT_Y210:
		case DRM_FORMAT_Y212:
		case DRM_FORMAT_Y216:
		case DRM_FORMAT_XVYU12_16161616:
		case DRM_FORMAT_XVYU16161616:
			drm_dbg_kms(&dev_priv->drm,
				    "Unsupported pixel format %p4cc for 90/270!\n",
				    &fb->format->format);
			return -EINVAL;
		default:
			break;
		}
	}

	/* Y-tiling is not supported in IF-ID Interlace mode */
	if (crtc_state->hw.enable &&
	    crtc_state->hw.adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE &&
	    fb->modifier != DRM_FORMAT_MOD_LINEAR &&
	    fb->modifier != I915_FORMAT_MOD_X_TILED) {
		drm_dbg_kms(&dev_priv->drm,
			    "Y/Yf tiling not supported in IF-ID mode\n");
		return -EINVAL;
	}

	/* Wa_1606054188:tgl,adl-s */
	if ((IS_ALDERLAKE_S(dev_priv) || IS_TIGERLAKE(dev_priv)) &&
	    plane_state->ckey.flags & I915_SET_COLORKEY_SOURCE &&
	    intel_format_is_p01x(fb->format->format)) {
		drm_dbg_kms(&dev_priv->drm,
			    "Source color keying not supported with P01x formats\n");
		return -EINVAL;
	}

	return 0;
}

static int skl_plane_check_dst_coordinates(const struct intel_crtc_state *crtc_state,
					   const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->uapi.plane->dev);
	int crtc_x = plane_state->uapi.dst.x1;
	int crtc_w = drm_rect_width(&plane_state->uapi.dst);
	int pipe_src_w = drm_rect_width(&crtc_state->pipe_src);

	/*
	 * Display WA #1175: glk
	 * Planes other than the cursor may cause FIFO underflow and display
	 * corruption if starting less than 4 pixels from the right edge of
	 * the screen.
	 * Besides the above WA fix the similar problem, where planes other
	 * than the cursor ending less than 4 pixels from the left edge of the
	 * screen may cause FIFO underflow and display corruption.
	 */
	if (DISPLAY_VER(dev_priv) == 10 &&
	    (crtc_x + crtc_w < 4 || crtc_x > pipe_src_w - 4)) {
		drm_dbg_kms(&dev_priv->drm,
			    "requested plane X %s position %d invalid (valid range %d-%d)\n",
			    crtc_x + crtc_w < 4 ? "end" : "start",
			    crtc_x + crtc_w < 4 ? crtc_x + crtc_w : crtc_x,
			    4, pipe_src_w - 4);
		return -ERANGE;
	}

	return 0;
}

static int skl_plane_check_nv12_rotation(const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rotation = plane_state->hw.rotation;
	int src_w = drm_rect_width(&plane_state->uapi.src) >> 16;

	/* Display WA #1106 */
	if (intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier) &&
	    src_w & 3 &&
	    (rotation == DRM_MODE_ROTATE_270 ||
	     rotation == (DRM_MODE_REFLECT_X | DRM_MODE_ROTATE_90))) {
		drm_dbg_kms(&i915->drm, "src width must be multiple of 4 for rotated planar YUV\n");
		return -EINVAL;
	}

	return 0;
}

static int skl_plane_max_scale(struct drm_i915_private *dev_priv,
			       const struct drm_framebuffer *fb)
{
	/*
	 * We don't yet know the final source width nor
	 * whether we can use the HQ scaler mode. Assume
	 * the best case.
	 * FIXME need to properly check this later.
	 */
	if (DISPLAY_VER(dev_priv) >= 10 ||
	    !intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier))
		return 0x30000 - 1;
	else
		return 0x20000 - 1;
}

static int intel_plane_min_width(struct intel_plane *plane,
				 const struct drm_framebuffer *fb,
				 int color_plane,
				 unsigned int rotation)
{
	if (plane->min_width)
		return plane->min_width(fb, color_plane, rotation);
	else
		return 1;
}

static int intel_plane_max_width(struct intel_plane *plane,
				 const struct drm_framebuffer *fb,
				 int color_plane,
				 unsigned int rotation)
{
	if (plane->max_width)
		return plane->max_width(fb, color_plane, rotation);
	else
		return INT_MAX;
}

static int intel_plane_max_height(struct intel_plane *plane,
				  const struct drm_framebuffer *fb,
				  int color_plane,
				  unsigned int rotation)
{
	if (plane->max_height)
		return plane->max_height(fb, color_plane, rotation);
	else
		return INT_MAX;
}

static bool
skl_check_main_ccs_coordinates(struct intel_plane_state *plane_state,
			       int main_x, int main_y, u32 main_offset,
			       int ccs_plane)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	int aux_x = plane_state->view.color_plane[ccs_plane].x;
	int aux_y = plane_state->view.color_plane[ccs_plane].y;
	u32 aux_offset = plane_state->view.color_plane[ccs_plane].offset;
	u32 alignment = intel_surf_alignment(fb, ccs_plane);
	int hsub;
	int vsub;

	intel_fb_plane_get_subsampling(&hsub, &vsub, fb, ccs_plane);
	while (aux_offset >= main_offset && aux_y <= main_y) {
		int x, y;

		if (aux_x == main_x && aux_y == main_y)
			break;

		if (aux_offset == 0)
			break;

		x = aux_x / hsub;
		y = aux_y / vsub;
		aux_offset = intel_plane_adjust_aligned_offset(&x, &y,
							       plane_state,
							       ccs_plane,
							       aux_offset,
							       aux_offset -
								alignment);
		aux_x = x * hsub + aux_x % hsub;
		aux_y = y * vsub + aux_y % vsub;
	}

	if (aux_x != main_x || aux_y != main_y)
		return false;

	plane_state->view.color_plane[ccs_plane].offset = aux_offset;
	plane_state->view.color_plane[ccs_plane].x = aux_x;
	plane_state->view.color_plane[ccs_plane].y = aux_y;

	return true;
}


int skl_calc_main_surface_offset(const struct intel_plane_state *plane_state,
				 int *x, int *y, u32 *offset)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	const int aux_plane = skl_main_to_aux_plane(fb, 0);
	const u32 aux_offset = plane_state->view.color_plane[aux_plane].offset;
	const u32 alignment = intel_surf_alignment(fb, 0);
	const int w = drm_rect_width(&plane_state->uapi.src) >> 16;

	intel_add_fb_offsets(x, y, plane_state, 0);
	*offset = intel_plane_compute_aligned_offset(x, y, plane_state, 0);
	if (drm_WARN_ON(&dev_priv->drm, alignment && !is_power_of_2(alignment)))
		return -EINVAL;

	/*
	 * AUX surface offset is specified as the distance from the
	 * main surface offset, and it must be non-negative. Make
	 * sure that is what we will get.
	 */
	if (aux_plane && *offset > aux_offset)
		*offset = intel_plane_adjust_aligned_offset(x, y, plane_state, 0,
							    *offset,
							    aux_offset & ~(alignment - 1));

	/*
	 * When using an X-tiled surface, the plane blows up
	 * if the x offset + width exceed the stride.
	 *
	 * TODO: linear and Y-tiled seem fine, Yf untested,
	 */
	if (fb->modifier == I915_FORMAT_MOD_X_TILED) {
		int cpp = fb->format->cpp[0];

		while ((*x + w) * cpp > plane_state->view.color_plane[0].mapping_stride) {
			if (*offset == 0) {
				drm_dbg_kms(&dev_priv->drm,
					    "Unable to find suitable display surface offset due to X-tiling\n");
				return -EINVAL;
			}

			*offset = intel_plane_adjust_aligned_offset(x, y, plane_state, 0,
								    *offset,
								    *offset - alignment);
		}
	}

	return 0;
}

static int skl_check_main_surface(struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	const unsigned int rotation = plane_state->hw.rotation;
	int x = plane_state->uapi.src.x1 >> 16;
	int y = plane_state->uapi.src.y1 >> 16;
	const int w = drm_rect_width(&plane_state->uapi.src) >> 16;
	const int h = drm_rect_height(&plane_state->uapi.src) >> 16;
	const int min_width = intel_plane_min_width(plane, fb, 0, rotation);
	const int max_width = intel_plane_max_width(plane, fb, 0, rotation);
	const int max_height = intel_plane_max_height(plane, fb, 0, rotation);
	const int aux_plane = skl_main_to_aux_plane(fb, 0);
	const u32 alignment = intel_surf_alignment(fb, 0);
	u32 offset;
	int ret;

	if (w > max_width || w < min_width || h > max_height) {
		drm_dbg_kms(&dev_priv->drm,
			    "requested Y/RGB source size %dx%d outside limits (min: %dx1 max: %dx%d)\n",
			    w, h, min_width, max_width, max_height);
		return -EINVAL;
	}

	ret = skl_calc_main_surface_offset(plane_state, &x, &y, &offset);
	if (ret)
		return ret;

	/*
	 * CCS AUX surface doesn't have its own x/y offsets, we must make sure
	 * they match with the main surface x/y offsets. On DG2
	 * there's no aux plane on fb so skip this checking.
	 */
	if (intel_fb_is_ccs_modifier(fb->modifier) && aux_plane) {
		while (!skl_check_main_ccs_coordinates(plane_state, x, y,
						       offset, aux_plane)) {
			if (offset == 0)
				break;

			offset = intel_plane_adjust_aligned_offset(&x, &y, plane_state, 0,
								   offset, offset - alignment);
		}

		if (x != plane_state->view.color_plane[aux_plane].x ||
		    y != plane_state->view.color_plane[aux_plane].y) {
			drm_dbg_kms(&dev_priv->drm,
				    "Unable to find suitable display surface offset due to CCS\n");
			return -EINVAL;
		}
	}

	if (DISPLAY_VER(dev_priv) >= 13)
		drm_WARN_ON(&dev_priv->drm, x > 65535 || y > 65535);
	else
		drm_WARN_ON(&dev_priv->drm, x > 8191 || y > 8191);

	plane_state->view.color_plane[0].offset = offset;
	plane_state->view.color_plane[0].x = x;
	plane_state->view.color_plane[0].y = y;

	/*
	 * Put the final coordinates back so that the src
	 * coordinate checks will see the right values.
	 */
	drm_rect_translate_to(&plane_state->uapi.src,
			      x << 16, y << 16);

	return 0;
}

static int skl_check_nv12_aux_surface(struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rotation = plane_state->hw.rotation;
	int uv_plane = 1;
	int ccs_plane = intel_fb_is_ccs_modifier(fb->modifier) ?
			skl_main_to_aux_plane(fb, uv_plane) : 0;
	int max_width = intel_plane_max_width(plane, fb, uv_plane, rotation);
	int max_height = intel_plane_max_height(plane, fb, uv_plane, rotation);
	int x = plane_state->uapi.src.x1 >> 17;
	int y = plane_state->uapi.src.y1 >> 17;
	int w = drm_rect_width(&plane_state->uapi.src) >> 17;
	int h = drm_rect_height(&plane_state->uapi.src) >> 17;
	u32 offset;

	/* FIXME not quite sure how/if these apply to the chroma plane */
	if (w > max_width || h > max_height) {
		drm_dbg_kms(&i915->drm,
			    "CbCr source size %dx%d too big (limit %dx%d)\n",
			    w, h, max_width, max_height);
		return -EINVAL;
	}

	intel_add_fb_offsets(&x, &y, plane_state, uv_plane);
	offset = intel_plane_compute_aligned_offset(&x, &y,
						    plane_state, uv_plane);

	if (ccs_plane) {
		u32 aux_offset = plane_state->view.color_plane[ccs_plane].offset;
		u32 alignment = intel_surf_alignment(fb, uv_plane);

		if (offset > aux_offset)
			offset = intel_plane_adjust_aligned_offset(&x, &y,
								   plane_state,
								   uv_plane,
								   offset,
								   aux_offset & ~(alignment - 1));

		while (!skl_check_main_ccs_coordinates(plane_state, x, y,
						       offset, ccs_plane)) {
			if (offset == 0)
				break;

			offset = intel_plane_adjust_aligned_offset(&x, &y,
								   plane_state,
								   uv_plane,
								   offset, offset - alignment);
		}

		if (x != plane_state->view.color_plane[ccs_plane].x ||
		    y != plane_state->view.color_plane[ccs_plane].y) {
			drm_dbg_kms(&i915->drm,
				    "Unable to find suitable display surface offset due to CCS\n");
			return -EINVAL;
		}
	}

	if (DISPLAY_VER(i915) >= 13)
		drm_WARN_ON(&i915->drm, x > 65535 || y > 65535);
	else
		drm_WARN_ON(&i915->drm, x > 8191 || y > 8191);

	plane_state->view.color_plane[uv_plane].offset = offset;
	plane_state->view.color_plane[uv_plane].x = x;
	plane_state->view.color_plane[uv_plane].y = y;

	return 0;
}

static int skl_check_ccs_aux_surface(struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	int src_x = plane_state->uapi.src.x1 >> 16;
	int src_y = plane_state->uapi.src.y1 >> 16;
	u32 offset;
	int ccs_plane;

	for (ccs_plane = 0; ccs_plane < fb->format->num_planes; ccs_plane++) {
		int main_hsub, main_vsub;
		int hsub, vsub;
		int x, y;

		if (!intel_fb_is_ccs_aux_plane(fb, ccs_plane))
			continue;

		intel_fb_plane_get_subsampling(&main_hsub, &main_vsub, fb,
					       skl_ccs_to_main_plane(fb, ccs_plane));
		intel_fb_plane_get_subsampling(&hsub, &vsub, fb, ccs_plane);

		hsub *= main_hsub;
		vsub *= main_vsub;
		x = src_x / hsub;
		y = src_y / vsub;

		intel_add_fb_offsets(&x, &y, plane_state, ccs_plane);

		offset = intel_plane_compute_aligned_offset(&x, &y,
							    plane_state,
							    ccs_plane);

		plane_state->view.color_plane[ccs_plane].offset = offset;
		plane_state->view.color_plane[ccs_plane].x = (x * hsub + src_x % hsub) / main_hsub;
		plane_state->view.color_plane[ccs_plane].y = (y * vsub + src_y % vsub) / main_vsub;
	}

	return 0;
}

static int skl_check_plane_surface(struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	int ret;

	ret = intel_plane_compute_gtt(plane_state);
	if (ret)
		return ret;

	if (!plane_state->uapi.visible)
		return 0;

	/*
	 * Handle the AUX surface first since the main surface setup depends on
	 * it.
	 */
	if (intel_fb_is_ccs_modifier(fb->modifier)) {
		ret = skl_check_ccs_aux_surface(plane_state);
		if (ret)
			return ret;
	}

	if (intel_format_info_is_yuv_semiplanar(fb->format,
						fb->modifier)) {
		ret = skl_check_nv12_aux_surface(plane_state);
		if (ret)
			return ret;
	}

	ret = skl_check_main_surface(plane_state);
	if (ret)
		return ret;

	return 0;
}

static bool skl_fb_scalable(const struct drm_framebuffer *fb)
{
	if (!fb)
		return false;

	switch (fb->format->format) {
	case DRM_FORMAT_C8:
		return false;
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ABGR16161616F:
		return DISPLAY_VER(to_i915(fb->dev)) >= 11;
	default:
		return true;
	}
}

static bool bo_has_valid_encryption(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);

	return intel_pxp_key_check(&to_gt(i915)->pxp, obj, false) == 0;
}

static bool pxp_is_borked(struct drm_i915_gem_object *obj)
{
	return i915_gem_object_is_protected(obj) && !bo_has_valid_encryption(obj);
}

static int skl_plane_check(struct intel_crtc_state *crtc_state,
			   struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	int min_scale = DRM_PLANE_NO_SCALING;
	int max_scale = DRM_PLANE_NO_SCALING;
	int ret;

	ret = skl_plane_check_fb(crtc_state, plane_state);
	if (ret)
		return ret;

	/* use scaler when colorkey is not required */
	if (!plane_state->ckey.flags && skl_fb_scalable(fb)) {
		min_scale = 1;
		max_scale = skl_plane_max_scale(dev_priv, fb);
	}

	ret = intel_atomic_plane_check_clipping(plane_state, crtc_state,
						min_scale, max_scale, true);
	if (ret)
		return ret;

	ret = skl_check_plane_surface(plane_state);
	if (ret)
		return ret;

	if (!plane_state->uapi.visible)
		return 0;

	ret = skl_plane_check_dst_coordinates(crtc_state, plane_state);
	if (ret)
		return ret;

	ret = intel_plane_check_src_coordinates(plane_state);
	if (ret)
		return ret;

	ret = skl_plane_check_nv12_rotation(plane_state);
	if (ret)
		return ret;

	if (DISPLAY_VER(dev_priv) >= 11) {
		plane_state->decrypt = bo_has_valid_encryption(intel_fb_obj(fb));
		plane_state->force_black = pxp_is_borked(intel_fb_obj(fb));
	}

	/* HW only has 8 bits pixel precision, disable plane if invisible */
	if (!(plane_state->hw.alpha >> 8))
		plane_state->uapi.visible = false;

	plane_state->ctl = skl_plane_ctl(crtc_state, plane_state);

	if (DISPLAY_VER(dev_priv) >= 10)
		plane_state->color_ctl = glk_plane_color_ctl(crtc_state,
							     plane_state);

	if (intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier) &&
	    icl_is_hdr_plane(dev_priv, plane->id))
		/* Enable and use MPEG-2 chroma siting */
		plane_state->cus_ctl = PLANE_CUS_ENABLE |
			PLANE_CUS_HPHASE_0 |
			PLANE_CUS_VPHASE_SIGN_NEGATIVE | PLANE_CUS_VPHASE_0_25;
	else
		plane_state->cus_ctl = 0;

	return 0;
}

static enum intel_fbc_id skl_fbc_id_for_pipe(enum pipe pipe)
{
	return pipe - PIPE_A + INTEL_FBC_A;
}

static bool skl_plane_has_fbc(struct drm_i915_private *dev_priv,
			      enum intel_fbc_id fbc_id, enum plane_id plane_id)
{
	if ((RUNTIME_INFO(dev_priv)->fbc_mask & BIT(fbc_id)) == 0)
		return false;

	return plane_id == PLANE_PRIMARY;
}

static struct intel_fbc *skl_plane_fbc(struct drm_i915_private *dev_priv,
				       enum pipe pipe, enum plane_id plane_id)
{
	enum intel_fbc_id fbc_id = skl_fbc_id_for_pipe(pipe);

	if (skl_plane_has_fbc(dev_priv, fbc_id, plane_id))
		return dev_priv->display.fbc[fbc_id];
	else
		return NULL;
}

static bool skl_plane_has_planar(struct drm_i915_private *dev_priv,
				 enum pipe pipe, enum plane_id plane_id)
{
	/* Display WA #0870: skl, bxt */
	if (IS_SKYLAKE(dev_priv) || IS_BROXTON(dev_priv))
		return false;

	if (DISPLAY_VER(dev_priv) == 9 && pipe == PIPE_C)
		return false;

	if (plane_id != PLANE_PRIMARY && plane_id != PLANE_SPRITE0)
		return false;

	return true;
}

static const u32 *skl_get_plane_formats(struct drm_i915_private *dev_priv,
					enum pipe pipe, enum plane_id plane_id,
					int *num_formats)
{
	if (skl_plane_has_planar(dev_priv, pipe, plane_id)) {
		*num_formats = ARRAY_SIZE(skl_planar_formats);
		return skl_planar_formats;
	} else {
		*num_formats = ARRAY_SIZE(skl_plane_formats);
		return skl_plane_formats;
	}
}

static const u32 *glk_get_plane_formats(struct drm_i915_private *dev_priv,
					enum pipe pipe, enum plane_id plane_id,
					int *num_formats)
{
	if (skl_plane_has_planar(dev_priv, pipe, plane_id)) {
		*num_formats = ARRAY_SIZE(glk_planar_formats);
		return glk_planar_formats;
	} else {
		*num_formats = ARRAY_SIZE(skl_plane_formats);
		return skl_plane_formats;
	}
}

static const u32 *icl_get_plane_formats(struct drm_i915_private *dev_priv,
					enum pipe pipe, enum plane_id plane_id,
					int *num_formats)
{
	if (icl_is_hdr_plane(dev_priv, plane_id)) {
		*num_formats = ARRAY_SIZE(icl_hdr_plane_formats);
		return icl_hdr_plane_formats;
	} else if (icl_is_nv12_y_plane(dev_priv, plane_id)) {
		*num_formats = ARRAY_SIZE(icl_sdr_y_plane_formats);
		return icl_sdr_y_plane_formats;
	} else {
		*num_formats = ARRAY_SIZE(icl_sdr_uv_plane_formats);
		return icl_sdr_uv_plane_formats;
	}
}

static bool skl_plane_format_mod_supported(struct drm_plane *_plane,
					   u32 format, u64 modifier)
{
	struct intel_plane *plane = to_intel_plane(_plane);

	if (!intel_fb_plane_supports_modifier(plane, modifier))
		return false;

	switch (format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
		if (intel_fb_is_ccs_modifier(modifier))
			return true;
		fallthrough;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_XYUV8888:
	case DRM_FORMAT_P010:
	case DRM_FORMAT_P012:
	case DRM_FORMAT_P016:
	case DRM_FORMAT_XVYU2101010:
		if (modifier == I915_FORMAT_MOD_Yf_TILED)
			return true;
		fallthrough;
	case DRM_FORMAT_C8:
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ABGR16161616F:
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_Y210:
	case DRM_FORMAT_Y212:
	case DRM_FORMAT_Y216:
	case DRM_FORMAT_XVYU12_16161616:
	case DRM_FORMAT_XVYU16161616:
		if (modifier == DRM_FORMAT_MOD_LINEAR ||
		    modifier == I915_FORMAT_MOD_X_TILED ||
		    modifier == I915_FORMAT_MOD_Y_TILED)
			return true;
		fallthrough;
	default:
		return false;
	}
}

static bool gen12_plane_format_mod_supported(struct drm_plane *_plane,
					     u32 format, u64 modifier)
{
	struct intel_plane *plane = to_intel_plane(_plane);

	if (!intel_fb_plane_supports_modifier(plane, modifier))
		return false;

	switch (format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
		if (intel_fb_is_ccs_modifier(modifier))
			return true;
		fallthrough;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_XYUV8888:
	case DRM_FORMAT_P010:
	case DRM_FORMAT_P012:
	case DRM_FORMAT_P016:
		if (intel_fb_is_mc_ccs_modifier(modifier))
			return true;
		fallthrough;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_XVYU2101010:
	case DRM_FORMAT_C8:
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ABGR16161616F:
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_Y210:
	case DRM_FORMAT_Y212:
	case DRM_FORMAT_Y216:
	case DRM_FORMAT_XVYU12_16161616:
	case DRM_FORMAT_XVYU16161616:
		if (!intel_fb_is_ccs_modifier(modifier))
			return true;
		fallthrough;
	default:
		return false;
	}
}

static const struct drm_plane_funcs skl_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = intel_plane_destroy,
	.atomic_duplicate_state = intel_plane_duplicate_state,
	.atomic_destroy_state = intel_plane_destroy_state,
	.format_mod_supported = skl_plane_format_mod_supported,
};

static const struct drm_plane_funcs gen12_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = intel_plane_destroy,
	.atomic_duplicate_state = intel_plane_duplicate_state,
	.atomic_destroy_state = intel_plane_destroy_state,
	.format_mod_supported = gen12_plane_format_mod_supported,
};

static void
skl_plane_enable_flip_done(struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;

	spin_lock_irq(&i915->irq_lock);
	bdw_enable_pipe_irq(i915, pipe, GEN9_PIPE_PLANE_FLIP_DONE(plane->id));
	spin_unlock_irq(&i915->irq_lock);
}

static void
skl_plane_disable_flip_done(struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;

	spin_lock_irq(&i915->irq_lock);
	bdw_disable_pipe_irq(i915, pipe, GEN9_PIPE_PLANE_FLIP_DONE(plane->id));
	spin_unlock_irq(&i915->irq_lock);
}

static bool skl_plane_has_rc_ccs(struct drm_i915_private *i915,
				 enum pipe pipe, enum plane_id plane_id)
{
	/* Wa_22011186057 */
	if (IS_ADLP_DISPLAY_STEP(i915, STEP_A0, STEP_B0))
		return false;

	if (DISPLAY_VER(i915) >= 11)
		return true;

	if (IS_GEMINILAKE(i915))
		return pipe != PIPE_C;

	return pipe != PIPE_C &&
		(plane_id == PLANE_PRIMARY ||
		 plane_id == PLANE_SPRITE0);
}

static bool gen12_plane_has_mc_ccs(struct drm_i915_private *i915,
				   enum plane_id plane_id)
{
	if (DISPLAY_VER(i915) < 12)
		return false;

	/* Wa_14010477008:tgl[a0..c0],rkl[all],dg1[all] */
	if (IS_DG1(i915) || IS_ROCKETLAKE(i915) ||
	    IS_TGL_DISPLAY_STEP(i915, STEP_A0, STEP_D0))
		return false;

	/* Wa_22011186057 */
	if (IS_ADLP_DISPLAY_STEP(i915, STEP_A0, STEP_B0))
		return false;

	/* Wa_14013215631 */
	if (IS_DG2_DISPLAY_STEP(i915, STEP_A0, STEP_C0))
		return false;

	return plane_id < PLANE_SPRITE4;
}

static u8 skl_get_plane_caps(struct drm_i915_private *i915,
			     enum pipe pipe, enum plane_id plane_id)
{
	u8 caps = INTEL_PLANE_CAP_TILING_X;

	if (DISPLAY_VER(i915) < 13 || IS_ALDERLAKE_P(i915))
		caps |= INTEL_PLANE_CAP_TILING_Y;
	if (DISPLAY_VER(i915) < 12)
		caps |= INTEL_PLANE_CAP_TILING_Yf;
	if (HAS_4TILE(i915))
		caps |= INTEL_PLANE_CAP_TILING_4;

	if (skl_plane_has_rc_ccs(i915, pipe, plane_id)) {
		caps |= INTEL_PLANE_CAP_CCS_RC;
		if (DISPLAY_VER(i915) >= 12)
			caps |= INTEL_PLANE_CAP_CCS_RC_CC;
	}

	if (gen12_plane_has_mc_ccs(i915, plane_id))
		caps |= INTEL_PLANE_CAP_CCS_MC;

	return caps;
}

struct intel_plane *
skl_universal_plane_create(struct drm_i915_private *dev_priv,
			   enum pipe pipe, enum plane_id plane_id)
{
	const struct drm_plane_funcs *plane_funcs;
	struct intel_plane *plane;
	enum drm_plane_type plane_type;
	unsigned int supported_rotations;
	unsigned int supported_csc;
	const u64 *modifiers;
	const u32 *formats;
	int num_formats;
	int ret;

	plane = intel_plane_alloc();
	if (IS_ERR(plane))
		return plane;

	plane->pipe = pipe;
	plane->id = plane_id;
	plane->frontbuffer_bit = INTEL_FRONTBUFFER(pipe, plane_id);

	intel_fbc_add_plane(skl_plane_fbc(dev_priv, pipe, plane_id), plane);

	if (DISPLAY_VER(dev_priv) >= 11) {
		plane->min_width = icl_plane_min_width;
		if (icl_is_hdr_plane(dev_priv, plane_id))
			plane->max_width = icl_hdr_plane_max_width;
		else
			plane->max_width = icl_sdr_plane_max_width;
		plane->max_height = icl_plane_max_height;
		plane->min_cdclk = icl_plane_min_cdclk;
	} else if (DISPLAY_VER(dev_priv) >= 10) {
		plane->max_width = glk_plane_max_width;
		plane->max_height = skl_plane_max_height;
		plane->min_cdclk = glk_plane_min_cdclk;
	} else {
		plane->max_width = skl_plane_max_width;
		plane->max_height = skl_plane_max_height;
		plane->min_cdclk = skl_plane_min_cdclk;
	}

	plane->max_stride = skl_plane_max_stride;
	if (DISPLAY_VER(dev_priv) >= 11) {
		plane->update_noarm = icl_plane_update_noarm;
		plane->update_arm = icl_plane_update_arm;
		plane->disable_arm = icl_plane_disable_arm;
	} else {
		plane->update_noarm = skl_plane_update_noarm;
		plane->update_arm = skl_plane_update_arm;
		plane->disable_arm = skl_plane_disable_arm;
	}
	plane->get_hw_state = skl_plane_get_hw_state;
	plane->check_plane = skl_plane_check;

	if (plane_id == PLANE_PRIMARY) {
		plane->need_async_flip_disable_wa = IS_DISPLAY_VER(dev_priv,
								   9, 10);
		plane->async_flip = skl_plane_async_flip;
		plane->enable_flip_done = skl_plane_enable_flip_done;
		plane->disable_flip_done = skl_plane_disable_flip_done;
	}

	if (DISPLAY_VER(dev_priv) >= 11)
		formats = icl_get_plane_formats(dev_priv, pipe,
						plane_id, &num_formats);
	else if (DISPLAY_VER(dev_priv) >= 10)
		formats = glk_get_plane_formats(dev_priv, pipe,
						plane_id, &num_formats);
	else
		formats = skl_get_plane_formats(dev_priv, pipe,
						plane_id, &num_formats);

	if (DISPLAY_VER(dev_priv) >= 12)
		plane_funcs = &gen12_plane_funcs;
	else
		plane_funcs = &skl_plane_funcs;

	if (plane_id == PLANE_PRIMARY)
		plane_type = DRM_PLANE_TYPE_PRIMARY;
	else
		plane_type = DRM_PLANE_TYPE_OVERLAY;

	modifiers = intel_fb_plane_get_modifiers(dev_priv,
						 skl_get_plane_caps(dev_priv, pipe, plane_id));

	ret = drm_universal_plane_init(&dev_priv->drm, &plane->base,
				       0, plane_funcs,
				       formats, num_formats, modifiers,
				       plane_type,
				       "plane %d%c", plane_id + 1,
				       pipe_name(pipe));

	kfree(modifiers);

	if (ret)
		goto fail;

	if (DISPLAY_VER(dev_priv) >= 13)
		supported_rotations = DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_180;
	else
		supported_rotations =
			DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_90 |
			DRM_MODE_ROTATE_180 | DRM_MODE_ROTATE_270;

	if (DISPLAY_VER(dev_priv) >= 11)
		supported_rotations |= DRM_MODE_REFLECT_X;

	drm_plane_create_rotation_property(&plane->base,
					   DRM_MODE_ROTATE_0,
					   supported_rotations);

	supported_csc = BIT(DRM_COLOR_YCBCR_BT601) | BIT(DRM_COLOR_YCBCR_BT709);

	if (DISPLAY_VER(dev_priv) >= 10)
		supported_csc |= BIT(DRM_COLOR_YCBCR_BT2020);

	drm_plane_create_color_properties(&plane->base,
					  supported_csc,
					  BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) |
					  BIT(DRM_COLOR_YCBCR_FULL_RANGE),
					  DRM_COLOR_YCBCR_BT709,
					  DRM_COLOR_YCBCR_LIMITED_RANGE);

	drm_plane_create_alpha_property(&plane->base);
	drm_plane_create_blend_mode_property(&plane->base,
					     BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					     BIT(DRM_MODE_BLEND_PREMULTI) |
					     BIT(DRM_MODE_BLEND_COVERAGE));

	drm_plane_create_zpos_immutable_property(&plane->base, plane_id);

	if (DISPLAY_VER(dev_priv) >= 12)
		drm_plane_enable_fb_damage_clips(&plane->base);

	if (DISPLAY_VER(dev_priv) >= 11)
		drm_plane_create_scaling_filter_property(&plane->base,
						BIT(DRM_SCALING_FILTER_DEFAULT) |
						BIT(DRM_SCALING_FILTER_NEAREST_NEIGHBOR));

	intel_plane_helper_add(plane);

	return plane;

fail:
	intel_plane_free(plane);

	return ERR_PTR(ret);
}

void
skl_get_initial_plane_config(struct intel_crtc *crtc,
			     struct intel_initial_plane_config *plane_config)
{
	struct intel_crtc_state *crtc_state = to_intel_crtc_state(crtc->base.state);
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_plane *plane = to_intel_plane(crtc->base.primary);
	enum plane_id plane_id = plane->id;
	enum pipe pipe;
	u32 val, base, offset, stride_mult, tiling, alpha;
	int fourcc, pixel_format;
	unsigned int aligned_height;
	struct drm_framebuffer *fb;
	struct intel_framebuffer *intel_fb;
	static_assert(PLANE_CTL_TILED_YF == PLANE_CTL_TILED_4);

	if (!plane->get_hw_state(plane, &pipe))
		return;

	drm_WARN_ON(dev, pipe != crtc->pipe);

	if (crtc_state->bigjoiner_pipes) {
		drm_dbg_kms(&dev_priv->drm,
			    "Unsupported bigjoiner configuration for initial FB\n");
		return;
	}

	intel_fb = kzalloc(sizeof(*intel_fb), GFP_KERNEL);
	if (!intel_fb) {
		drm_dbg_kms(&dev_priv->drm, "failed to alloc fb\n");
		return;
	}

	fb = &intel_fb->base;

	fb->dev = dev;

	val = intel_de_read(dev_priv, PLANE_CTL(pipe, plane_id));

	if (DISPLAY_VER(dev_priv) >= 11)
		pixel_format = val & PLANE_CTL_FORMAT_MASK_ICL;
	else
		pixel_format = val & PLANE_CTL_FORMAT_MASK_SKL;

	if (DISPLAY_VER(dev_priv) >= 10) {
		u32 color_ctl;

		color_ctl = intel_de_read(dev_priv, PLANE_COLOR_CTL(pipe, plane_id));
		alpha = REG_FIELD_GET(PLANE_COLOR_ALPHA_MASK, color_ctl);
	} else {
		alpha = REG_FIELD_GET(PLANE_CTL_ALPHA_MASK, val);
	}

	fourcc = skl_format_to_fourcc(pixel_format,
				      val & PLANE_CTL_ORDER_RGBX, alpha);
	fb->format = drm_format_info(fourcc);

	tiling = val & PLANE_CTL_TILED_MASK;
	switch (tiling) {
	case PLANE_CTL_TILED_LINEAR:
		fb->modifier = DRM_FORMAT_MOD_LINEAR;
		break;
	case PLANE_CTL_TILED_X:
		plane_config->tiling = I915_TILING_X;
		fb->modifier = I915_FORMAT_MOD_X_TILED;
		break;
	case PLANE_CTL_TILED_Y:
		plane_config->tiling = I915_TILING_Y;
		if (val & PLANE_CTL_RENDER_DECOMPRESSION_ENABLE)
			if (DISPLAY_VER(dev_priv) >= 12)
				fb->modifier = I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS;
			else
				fb->modifier = I915_FORMAT_MOD_Y_TILED_CCS;
		else if (val & PLANE_CTL_MEDIA_DECOMPRESSION_ENABLE)
			fb->modifier = I915_FORMAT_MOD_Y_TILED_GEN12_MC_CCS;
		else
			fb->modifier = I915_FORMAT_MOD_Y_TILED;
		break;
	case PLANE_CTL_TILED_YF: /* aka PLANE_CTL_TILED_4 on XE_LPD+ */
		if (HAS_4TILE(dev_priv)) {
			u32 rc_mask = PLANE_CTL_RENDER_DECOMPRESSION_ENABLE |
				      PLANE_CTL_CLEAR_COLOR_DISABLE;

			if ((val & rc_mask) == rc_mask)
				fb->modifier = I915_FORMAT_MOD_4_TILED_DG2_RC_CCS;
			else if (val & PLANE_CTL_MEDIA_DECOMPRESSION_ENABLE)
				fb->modifier = I915_FORMAT_MOD_4_TILED_DG2_MC_CCS;
			else if (val & PLANE_CTL_RENDER_DECOMPRESSION_ENABLE)
				fb->modifier = I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC;
			else
				fb->modifier = I915_FORMAT_MOD_4_TILED;
		} else {
			if (val & PLANE_CTL_RENDER_DECOMPRESSION_ENABLE)
				fb->modifier = I915_FORMAT_MOD_Yf_TILED_CCS;
			else
				fb->modifier = I915_FORMAT_MOD_Yf_TILED;
		}
		break;
	default:
		MISSING_CASE(tiling);
		goto error;
	}

	/*
	 * DRM_MODE_ROTATE_ is counter clockwise to stay compatible with Xrandr
	 * while i915 HW rotation is clockwise, thats why this swapping.
	 */
	switch (val & PLANE_CTL_ROTATE_MASK) {
	case PLANE_CTL_ROTATE_0:
		plane_config->rotation = DRM_MODE_ROTATE_0;
		break;
	case PLANE_CTL_ROTATE_90:
		plane_config->rotation = DRM_MODE_ROTATE_270;
		break;
	case PLANE_CTL_ROTATE_180:
		plane_config->rotation = DRM_MODE_ROTATE_180;
		break;
	case PLANE_CTL_ROTATE_270:
		plane_config->rotation = DRM_MODE_ROTATE_90;
		break;
	}

	if (DISPLAY_VER(dev_priv) >= 11 && val & PLANE_CTL_FLIP_HORIZONTAL)
		plane_config->rotation |= DRM_MODE_REFLECT_X;

	/* 90/270 degree rotation would require extra work */
	if (drm_rotation_90_or_270(plane_config->rotation))
		goto error;

	base = intel_de_read(dev_priv, PLANE_SURF(pipe, plane_id)) & PLANE_SURF_ADDR_MASK;
	plane_config->base = base;

	offset = intel_de_read(dev_priv, PLANE_OFFSET(pipe, plane_id));

	val = intel_de_read(dev_priv, PLANE_SIZE(pipe, plane_id));
	fb->height = REG_FIELD_GET(PLANE_HEIGHT_MASK, val) + 1;
	fb->width = REG_FIELD_GET(PLANE_WIDTH_MASK, val) + 1;

	val = intel_de_read(dev_priv, PLANE_STRIDE(pipe, plane_id));
	stride_mult = skl_plane_stride_mult(fb, 0, DRM_MODE_ROTATE_0);

	fb->pitches[0] = REG_FIELD_GET(PLANE_STRIDE__MASK, val) * stride_mult;

	aligned_height = intel_fb_align_height(fb, 0, fb->height);

	plane_config->size = fb->pitches[0] * aligned_height;

	drm_dbg_kms(&dev_priv->drm,
		    "%s/%s with fb: size=%dx%d@%d, offset=%x, pitch %d, size 0x%x\n",
		    crtc->base.name, plane->base.name, fb->width, fb->height,
		    fb->format->cpp[0] * 8, base, fb->pitches[0],
		    plane_config->size);

	plane_config->fb = intel_fb;
	return;

error:
	kfree(intel_fb);
}
