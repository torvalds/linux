/*
 * Copyright © 2011 Intel Corporation
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
 *   Jesse Barnes <jbarnes@virtuousgeek.org>
 *
 * New plane/sprite handling.
 *
 * The older chips had a separate interface for programming plane related
 * registers; newer ones are much simpler and we can use the new DRM plane
 * support.
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_color_mgmt.h>
#include <drm/drm_crtc.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_rect.h>

#include "i915_drv.h"
#include "i915_trace.h"
#include "i915_vgpu.h"
#include "intel_atomic_plane.h"
#include "intel_display_types.h"
#include "intel_frontbuffer.h"
#include "intel_sprite.h"
#include "i9xx_plane.h"
#include "intel_vrr.h"

int intel_plane_check_stride(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rotation = plane_state->hw.rotation;
	u32 stride, max_stride;

	/*
	 * We ignore stride for all invisible planes that
	 * can be remapped. Otherwise we could end up
	 * with a false positive when the remapping didn't
	 * kick in due the plane being invisible.
	 */
	if (intel_plane_can_remap(plane_state) &&
	    !plane_state->uapi.visible)
		return 0;

	/* FIXME other color planes? */
	stride = plane_state->color_plane[0].stride;
	max_stride = plane->max_stride(plane, fb->format->format,
				       fb->modifier, rotation);

	if (stride > max_stride) {
		DRM_DEBUG_KMS("[FB:%d] stride (%d) exceeds [PLANE:%d:%s] max stride (%d)\n",
			      fb->base.id, stride,
			      plane->base.base.id, plane->base.name, max_stride);
		return -EINVAL;
	}

	return 0;
}

int intel_plane_check_src_coordinates(struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	struct drm_rect *src = &plane_state->uapi.src;
	u32 src_x, src_y, src_w, src_h, hsub, vsub;
	bool rotated = drm_rotation_90_or_270(plane_state->hw.rotation);

	/*
	 * FIXME hsub/vsub vs. block size is a mess. Pre-tgl CCS
	 * abuses hsub/vsub so we can't use them here. But as they
	 * are limited to 32bpp RGB formats we don't actually need
	 * to check anything.
	 */
	if (fb->modifier == I915_FORMAT_MOD_Y_TILED_CCS ||
	    fb->modifier == I915_FORMAT_MOD_Yf_TILED_CCS)
		return 0;

	/*
	 * Hardware doesn't handle subpixel coordinates.
	 * Adjust to (macro)pixel boundary, but be careful not to
	 * increase the source viewport size, because that could
	 * push the downscaling factor out of bounds.
	 */
	src_x = src->x1 >> 16;
	src_w = drm_rect_width(src) >> 16;
	src_y = src->y1 >> 16;
	src_h = drm_rect_height(src) >> 16;

	drm_rect_init(src, src_x << 16, src_y << 16,
		      src_w << 16, src_h << 16);

	if (fb->format->format == DRM_FORMAT_RGB565 && rotated) {
		hsub = 2;
		vsub = 2;
	} else {
		hsub = fb->format->hsub;
		vsub = fb->format->vsub;
	}

	if (rotated)
		hsub = vsub = max(hsub, vsub);

	if (src_x % hsub || src_w % hsub) {
		DRM_DEBUG_KMS("src x/w (%u, %u) must be a multiple of %u (rotated: %s)\n",
			      src_x, src_w, hsub, yesno(rotated));
		return -EINVAL;
	}

	if (src_y % vsub || src_h % vsub) {
		DRM_DEBUG_KMS("src y/h (%u, %u) must be a multiple of %u (rotated: %s)\n",
			      src_y, src_h, vsub, yesno(rotated));
		return -EINVAL;
	}

	return 0;
}

static void i9xx_plane_linear_gamma(u16 gamma[8])
{
	/* The points are not evenly spaced. */
	static const u8 in[8] = { 0, 1, 2, 4, 8, 16, 24, 32 };
	int i;

	for (i = 0; i < 8; i++)
		gamma[i] = (in[i] << 8) / 32;
}

static void
chv_update_csc(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	enum plane_id plane_id = plane->id;
	/*
	 * |r|   | c0 c1 c2 |   |cr|
	 * |g| = | c3 c4 c5 | x |y |
	 * |b|   | c6 c7 c8 |   |cb|
	 *
	 * Coefficients are s3.12.
	 *
	 * Cb and Cr apparently come in as signed already, and
	 * we always get full range data in on account of CLRC0/1.
	 */
	static const s16 csc_matrix[][9] = {
		/* BT.601 full range YCbCr -> full range RGB */
		[DRM_COLOR_YCBCR_BT601] = {
			 5743, 4096,     0,
			-2925, 4096, -1410,
			    0, 4096,  7258,
		},
		/* BT.709 full range YCbCr -> full range RGB */
		[DRM_COLOR_YCBCR_BT709] = {
			 6450, 4096,     0,
			-1917, 4096,  -767,
			    0, 4096,  7601,
		},
	};
	const s16 *csc = csc_matrix[plane_state->hw.color_encoding];

	/* Seems RGB data bypasses the CSC always */
	if (!fb->format->is_yuv)
		return;

	intel_de_write_fw(dev_priv, SPCSCYGOFF(plane_id),
			  SPCSC_OOFF(0) | SPCSC_IOFF(0));
	intel_de_write_fw(dev_priv, SPCSCCBOFF(plane_id),
			  SPCSC_OOFF(0) | SPCSC_IOFF(0));
	intel_de_write_fw(dev_priv, SPCSCCROFF(plane_id),
			  SPCSC_OOFF(0) | SPCSC_IOFF(0));

	intel_de_write_fw(dev_priv, SPCSCC01(plane_id),
			  SPCSC_C1(csc[1]) | SPCSC_C0(csc[0]));
	intel_de_write_fw(dev_priv, SPCSCC23(plane_id),
			  SPCSC_C1(csc[3]) | SPCSC_C0(csc[2]));
	intel_de_write_fw(dev_priv, SPCSCC45(plane_id),
			  SPCSC_C1(csc[5]) | SPCSC_C0(csc[4]));
	intel_de_write_fw(dev_priv, SPCSCC67(plane_id),
			  SPCSC_C1(csc[7]) | SPCSC_C0(csc[6]));
	intel_de_write_fw(dev_priv, SPCSCC8(plane_id), SPCSC_C0(csc[8]));

	intel_de_write_fw(dev_priv, SPCSCYGICLAMP(plane_id),
			  SPCSC_IMAX(1023) | SPCSC_IMIN(0));
	intel_de_write_fw(dev_priv, SPCSCCBICLAMP(plane_id),
			  SPCSC_IMAX(512) | SPCSC_IMIN(-512));
	intel_de_write_fw(dev_priv, SPCSCCRICLAMP(plane_id),
			  SPCSC_IMAX(512) | SPCSC_IMIN(-512));

	intel_de_write_fw(dev_priv, SPCSCYGOCLAMP(plane_id),
			  SPCSC_OMAX(1023) | SPCSC_OMIN(0));
	intel_de_write_fw(dev_priv, SPCSCCBOCLAMP(plane_id),
			  SPCSC_OMAX(1023) | SPCSC_OMIN(0));
	intel_de_write_fw(dev_priv, SPCSCCROCLAMP(plane_id),
			  SPCSC_OMAX(1023) | SPCSC_OMIN(0));
}

#define SIN_0 0
#define COS_0 1

static void
vlv_update_clrc(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	enum pipe pipe = plane->pipe;
	enum plane_id plane_id = plane->id;
	int contrast, brightness, sh_scale, sh_sin, sh_cos;

	if (fb->format->is_yuv &&
	    plane_state->hw.color_range == DRM_COLOR_YCBCR_LIMITED_RANGE) {
		/*
		 * Expand limited range to full range:
		 * Contrast is applied first and is used to expand Y range.
		 * Brightness is applied second and is used to remove the
		 * offset from Y. Saturation/hue is used to expand CbCr range.
		 */
		contrast = DIV_ROUND_CLOSEST(255 << 6, 235 - 16);
		brightness = -DIV_ROUND_CLOSEST(16 * 255, 235 - 16);
		sh_scale = DIV_ROUND_CLOSEST(128 << 7, 240 - 128);
		sh_sin = SIN_0 * sh_scale;
		sh_cos = COS_0 * sh_scale;
	} else {
		/* Pass-through everything. */
		contrast = 1 << 6;
		brightness = 0;
		sh_scale = 1 << 7;
		sh_sin = SIN_0 * sh_scale;
		sh_cos = COS_0 * sh_scale;
	}

	/* FIXME these register are single buffered :( */
	intel_de_write_fw(dev_priv, SPCLRC0(pipe, plane_id),
			  SP_CONTRAST(contrast) | SP_BRIGHTNESS(brightness));
	intel_de_write_fw(dev_priv, SPCLRC1(pipe, plane_id),
			  SP_SH_SIN(sh_sin) | SP_SH_COS(sh_cos));
}

static void
vlv_plane_ratio(const struct intel_crtc_state *crtc_state,
		const struct intel_plane_state *plane_state,
		unsigned int *num, unsigned int *den)
{
	u8 active_planes = crtc_state->active_planes & ~BIT(PLANE_CURSOR);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int cpp = fb->format->cpp[0];

	/*
	 * VLV bspec only considers cases where all three planes are
	 * enabled, and cases where the primary and one sprite is enabled.
	 * Let's assume the case with just two sprites enabled also
	 * maps to the latter case.
	 */
	if (hweight8(active_planes) == 3) {
		switch (cpp) {
		case 8:
			*num = 11;
			*den = 8;
			break;
		case 4:
			*num = 18;
			*den = 16;
			break;
		default:
			*num = 1;
			*den = 1;
			break;
		}
	} else if (hweight8(active_planes) == 2) {
		switch (cpp) {
		case 8:
			*num = 10;
			*den = 8;
			break;
		case 4:
			*num = 17;
			*den = 16;
			break;
		default:
			*num = 1;
			*den = 1;
			break;
		}
	} else {
		switch (cpp) {
		case 8:
			*num = 10;
			*den = 8;
			break;
		default:
			*num = 1;
			*den = 1;
			break;
		}
	}
}

int vlv_plane_min_cdclk(const struct intel_crtc_state *crtc_state,
			const struct intel_plane_state *plane_state)
{
	unsigned int pixel_rate;
	unsigned int num, den;

	/*
	 * Note that crtc_state->pixel_rate accounts for both
	 * horizontal and vertical panel fitter downscaling factors.
	 * Pre-HSW bspec tells us to only consider the horizontal
	 * downscaling factor here. We ignore that and just consider
	 * both for simplicity.
	 */
	pixel_rate = crtc_state->pixel_rate;

	vlv_plane_ratio(crtc_state, plane_state, &num, &den);

	return DIV_ROUND_UP(pixel_rate * num, den);
}

static u32 vlv_sprite_ctl_crtc(const struct intel_crtc_state *crtc_state)
{
	u32 sprctl = 0;

	if (crtc_state->gamma_enable)
		sprctl |= SP_GAMMA_ENABLE;

	return sprctl;
}

static u32 vlv_sprite_ctl(const struct intel_crtc_state *crtc_state,
			  const struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rotation = plane_state->hw.rotation;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	u32 sprctl;

	sprctl = SP_ENABLE;

	switch (fb->format->format) {
	case DRM_FORMAT_YUYV:
		sprctl |= SP_FORMAT_YUV422 | SP_YUV_ORDER_YUYV;
		break;
	case DRM_FORMAT_YVYU:
		sprctl |= SP_FORMAT_YUV422 | SP_YUV_ORDER_YVYU;
		break;
	case DRM_FORMAT_UYVY:
		sprctl |= SP_FORMAT_YUV422 | SP_YUV_ORDER_UYVY;
		break;
	case DRM_FORMAT_VYUY:
		sprctl |= SP_FORMAT_YUV422 | SP_YUV_ORDER_VYUY;
		break;
	case DRM_FORMAT_C8:
		sprctl |= SP_FORMAT_8BPP;
		break;
	case DRM_FORMAT_RGB565:
		sprctl |= SP_FORMAT_BGR565;
		break;
	case DRM_FORMAT_XRGB8888:
		sprctl |= SP_FORMAT_BGRX8888;
		break;
	case DRM_FORMAT_ARGB8888:
		sprctl |= SP_FORMAT_BGRA8888;
		break;
	case DRM_FORMAT_XBGR2101010:
		sprctl |= SP_FORMAT_RGBX1010102;
		break;
	case DRM_FORMAT_ABGR2101010:
		sprctl |= SP_FORMAT_RGBA1010102;
		break;
	case DRM_FORMAT_XRGB2101010:
		sprctl |= SP_FORMAT_BGRX1010102;
		break;
	case DRM_FORMAT_ARGB2101010:
		sprctl |= SP_FORMAT_BGRA1010102;
		break;
	case DRM_FORMAT_XBGR8888:
		sprctl |= SP_FORMAT_RGBX8888;
		break;
	case DRM_FORMAT_ABGR8888:
		sprctl |= SP_FORMAT_RGBA8888;
		break;
	default:
		MISSING_CASE(fb->format->format);
		return 0;
	}

	if (plane_state->hw.color_encoding == DRM_COLOR_YCBCR_BT709)
		sprctl |= SP_YUV_FORMAT_BT709;

	if (fb->modifier == I915_FORMAT_MOD_X_TILED)
		sprctl |= SP_TILED;

	if (rotation & DRM_MODE_ROTATE_180)
		sprctl |= SP_ROTATE_180;

	if (rotation & DRM_MODE_REFLECT_X)
		sprctl |= SP_MIRROR;

	if (key->flags & I915_SET_COLORKEY_SOURCE)
		sprctl |= SP_SOURCE_KEY;

	return sprctl;
}

static void vlv_update_gamma(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	enum pipe pipe = plane->pipe;
	enum plane_id plane_id = plane->id;
	u16 gamma[8];
	int i;

	/* Seems RGB data bypasses the gamma always */
	if (!fb->format->is_yuv)
		return;

	i9xx_plane_linear_gamma(gamma);

	/* FIXME these register are single buffered :( */
	/* The two end points are implicit (0.0 and 1.0) */
	for (i = 1; i < 8 - 1; i++)
		intel_de_write_fw(dev_priv, SPGAMC(pipe, plane_id, i - 1),
				  gamma[i] << 16 | gamma[i] << 8 | gamma[i]);
}

static void
vlv_update_plane(struct intel_plane *plane,
		 const struct intel_crtc_state *crtc_state,
		 const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;
	enum plane_id plane_id = plane->id;
	u32 sprsurf_offset = plane_state->color_plane[0].offset;
	u32 linear_offset;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	int crtc_x = plane_state->uapi.dst.x1;
	int crtc_y = plane_state->uapi.dst.y1;
	u32 crtc_w = drm_rect_width(&plane_state->uapi.dst);
	u32 crtc_h = drm_rect_height(&plane_state->uapi.dst);
	u32 x = plane_state->color_plane[0].x;
	u32 y = plane_state->color_plane[0].y;
	unsigned long irqflags;
	u32 sprctl;

	sprctl = plane_state->ctl | vlv_sprite_ctl_crtc(crtc_state);

	/* Sizes are 0 based */
	crtc_w--;
	crtc_h--;

	linear_offset = intel_fb_xy_to_linear(x, y, plane_state, 0);

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	intel_de_write_fw(dev_priv, SPSTRIDE(pipe, plane_id),
			  plane_state->color_plane[0].stride);
	intel_de_write_fw(dev_priv, SPPOS(pipe, plane_id),
			  (crtc_y << 16) | crtc_x);
	intel_de_write_fw(dev_priv, SPSIZE(pipe, plane_id),
			  (crtc_h << 16) | crtc_w);
	intel_de_write_fw(dev_priv, SPCONSTALPHA(pipe, plane_id), 0);

	if (IS_CHERRYVIEW(dev_priv) && pipe == PIPE_B)
		chv_update_csc(plane_state);

	if (key->flags) {
		intel_de_write_fw(dev_priv, SPKEYMINVAL(pipe, plane_id),
				  key->min_value);
		intel_de_write_fw(dev_priv, SPKEYMSK(pipe, plane_id),
				  key->channel_mask);
		intel_de_write_fw(dev_priv, SPKEYMAXVAL(pipe, plane_id),
				  key->max_value);
	}

	intel_de_write_fw(dev_priv, SPLINOFF(pipe, plane_id), linear_offset);
	intel_de_write_fw(dev_priv, SPTILEOFF(pipe, plane_id), (y << 16) | x);

	/*
	 * The control register self-arms if the plane was previously
	 * disabled. Try to make the plane enable atomic by writing
	 * the control register just before the surface register.
	 */
	intel_de_write_fw(dev_priv, SPCNTR(pipe, plane_id), sprctl);
	intel_de_write_fw(dev_priv, SPSURF(pipe, plane_id),
			  intel_plane_ggtt_offset(plane_state) + sprsurf_offset);

	vlv_update_clrc(plane_state);
	vlv_update_gamma(plane_state);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static void
vlv_disable_plane(struct intel_plane *plane,
		  const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;
	enum plane_id plane_id = plane->id;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	intel_de_write_fw(dev_priv, SPCNTR(pipe, plane_id), 0);
	intel_de_write_fw(dev_priv, SPSURF(pipe, plane_id), 0);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static bool
vlv_plane_get_hw_state(struct intel_plane *plane,
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

	ret = intel_de_read(dev_priv, SPCNTR(plane->pipe, plane_id)) & SP_ENABLE;

	*pipe = plane->pipe;

	intel_display_power_put(dev_priv, power_domain, wakeref);

	return ret;
}

static void ivb_plane_ratio(const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state,
			    unsigned int *num, unsigned int *den)
{
	u8 active_planes = crtc_state->active_planes & ~BIT(PLANE_CURSOR);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int cpp = fb->format->cpp[0];

	if (hweight8(active_planes) == 2) {
		switch (cpp) {
		case 8:
			*num = 10;
			*den = 8;
			break;
		case 4:
			*num = 17;
			*den = 16;
			break;
		default:
			*num = 1;
			*den = 1;
			break;
		}
	} else {
		switch (cpp) {
		case 8:
			*num = 9;
			*den = 8;
			break;
		default:
			*num = 1;
			*den = 1;
			break;
		}
	}
}

static void ivb_plane_ratio_scaling(const struct intel_crtc_state *crtc_state,
				    const struct intel_plane_state *plane_state,
				    unsigned int *num, unsigned int *den)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int cpp = fb->format->cpp[0];

	switch (cpp) {
	case 8:
		*num = 12;
		*den = 8;
		break;
	case 4:
		*num = 19;
		*den = 16;
		break;
	case 2:
		*num = 33;
		*den = 32;
		break;
	default:
		*num = 1;
		*den = 1;
		break;
	}
}

int ivb_plane_min_cdclk(const struct intel_crtc_state *crtc_state,
			const struct intel_plane_state *plane_state)
{
	unsigned int pixel_rate;
	unsigned int num, den;

	/*
	 * Note that crtc_state->pixel_rate accounts for both
	 * horizontal and vertical panel fitter downscaling factors.
	 * Pre-HSW bspec tells us to only consider the horizontal
	 * downscaling factor here. We ignore that and just consider
	 * both for simplicity.
	 */
	pixel_rate = crtc_state->pixel_rate;

	ivb_plane_ratio(crtc_state, plane_state, &num, &den);

	return DIV_ROUND_UP(pixel_rate * num, den);
}

static int ivb_sprite_min_cdclk(const struct intel_crtc_state *crtc_state,
				const struct intel_plane_state *plane_state)
{
	unsigned int src_w, dst_w, pixel_rate;
	unsigned int num, den;

	/*
	 * Note that crtc_state->pixel_rate accounts for both
	 * horizontal and vertical panel fitter downscaling factors.
	 * Pre-HSW bspec tells us to only consider the horizontal
	 * downscaling factor here. We ignore that and just consider
	 * both for simplicity.
	 */
	pixel_rate = crtc_state->pixel_rate;

	src_w = drm_rect_width(&plane_state->uapi.src) >> 16;
	dst_w = drm_rect_width(&plane_state->uapi.dst);

	if (src_w != dst_w)
		ivb_plane_ratio_scaling(crtc_state, plane_state, &num, &den);
	else
		ivb_plane_ratio(crtc_state, plane_state, &num, &den);

	/* Horizontal downscaling limits the maximum pixel rate */
	dst_w = min(src_w, dst_w);

	return DIV_ROUND_UP_ULL(mul_u32_u32(pixel_rate, num * src_w),
				den * dst_w);
}

static void hsw_plane_ratio(const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state,
			    unsigned int *num, unsigned int *den)
{
	u8 active_planes = crtc_state->active_planes & ~BIT(PLANE_CURSOR);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int cpp = fb->format->cpp[0];

	if (hweight8(active_planes) == 2) {
		switch (cpp) {
		case 8:
			*num = 10;
			*den = 8;
			break;
		default:
			*num = 1;
			*den = 1;
			break;
		}
	} else {
		switch (cpp) {
		case 8:
			*num = 9;
			*den = 8;
			break;
		default:
			*num = 1;
			*den = 1;
			break;
		}
	}
}

int hsw_plane_min_cdclk(const struct intel_crtc_state *crtc_state,
			const struct intel_plane_state *plane_state)
{
	unsigned int pixel_rate = crtc_state->pixel_rate;
	unsigned int num, den;

	hsw_plane_ratio(crtc_state, plane_state, &num, &den);

	return DIV_ROUND_UP(pixel_rate * num, den);
}

static u32 ivb_sprite_ctl_crtc(const struct intel_crtc_state *crtc_state)
{
	u32 sprctl = 0;

	if (crtc_state->gamma_enable)
		sprctl |= SPRITE_GAMMA_ENABLE;

	if (crtc_state->csc_enable)
		sprctl |= SPRITE_PIPE_CSC_ENABLE;

	return sprctl;
}

static bool ivb_need_sprite_gamma(const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;

	return fb->format->cpp[0] == 8 &&
		(IS_IVYBRIDGE(dev_priv) || IS_HASWELL(dev_priv));
}

static u32 ivb_sprite_ctl(const struct intel_crtc_state *crtc_state,
			  const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rotation = plane_state->hw.rotation;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	u32 sprctl;

	sprctl = SPRITE_ENABLE;

	if (IS_IVYBRIDGE(dev_priv))
		sprctl |= SPRITE_TRICKLE_FEED_DISABLE;

	switch (fb->format->format) {
	case DRM_FORMAT_XBGR8888:
		sprctl |= SPRITE_FORMAT_RGBX888 | SPRITE_RGB_ORDER_RGBX;
		break;
	case DRM_FORMAT_XRGB8888:
		sprctl |= SPRITE_FORMAT_RGBX888;
		break;
	case DRM_FORMAT_XBGR2101010:
		sprctl |= SPRITE_FORMAT_RGBX101010 | SPRITE_RGB_ORDER_RGBX;
		break;
	case DRM_FORMAT_XRGB2101010:
		sprctl |= SPRITE_FORMAT_RGBX101010;
		break;
	case DRM_FORMAT_XBGR16161616F:
		sprctl |= SPRITE_FORMAT_RGBX161616 | SPRITE_RGB_ORDER_RGBX;
		break;
	case DRM_FORMAT_XRGB16161616F:
		sprctl |= SPRITE_FORMAT_RGBX161616;
		break;
	case DRM_FORMAT_YUYV:
		sprctl |= SPRITE_FORMAT_YUV422 | SPRITE_YUV_ORDER_YUYV;
		break;
	case DRM_FORMAT_YVYU:
		sprctl |= SPRITE_FORMAT_YUV422 | SPRITE_YUV_ORDER_YVYU;
		break;
	case DRM_FORMAT_UYVY:
		sprctl |= SPRITE_FORMAT_YUV422 | SPRITE_YUV_ORDER_UYVY;
		break;
	case DRM_FORMAT_VYUY:
		sprctl |= SPRITE_FORMAT_YUV422 | SPRITE_YUV_ORDER_VYUY;
		break;
	default:
		MISSING_CASE(fb->format->format);
		return 0;
	}

	if (!ivb_need_sprite_gamma(plane_state))
		sprctl |= SPRITE_INT_GAMMA_DISABLE;

	if (plane_state->hw.color_encoding == DRM_COLOR_YCBCR_BT709)
		sprctl |= SPRITE_YUV_TO_RGB_CSC_FORMAT_BT709;

	if (plane_state->hw.color_range == DRM_COLOR_YCBCR_FULL_RANGE)
		sprctl |= SPRITE_YUV_RANGE_CORRECTION_DISABLE;

	if (fb->modifier == I915_FORMAT_MOD_X_TILED)
		sprctl |= SPRITE_TILED;

	if (rotation & DRM_MODE_ROTATE_180)
		sprctl |= SPRITE_ROTATE_180;

	if (key->flags & I915_SET_COLORKEY_DESTINATION)
		sprctl |= SPRITE_DEST_KEY;
	else if (key->flags & I915_SET_COLORKEY_SOURCE)
		sprctl |= SPRITE_SOURCE_KEY;

	return sprctl;
}

static void ivb_sprite_linear_gamma(const struct intel_plane_state *plane_state,
				    u16 gamma[18])
{
	int scale, i;

	/*
	 * WaFP16GammaEnabling:ivb,hsw
	 * "Workaround : When using the 64-bit format, the sprite output
	 *  on each color channel has one quarter amplitude. It can be
	 *  brought up to full amplitude by using sprite internal gamma
	 *  correction, pipe gamma correction, or pipe color space
	 *  conversion to multiply the sprite output by four."
	 */
	scale = 4;

	for (i = 0; i < 16; i++)
		gamma[i] = min((scale * i << 10) / 16, (1 << 10) - 1);

	gamma[i] = min((scale * i << 10) / 16, 1 << 10);
	i++;

	gamma[i] = 3 << 10;
	i++;
}

static void ivb_update_gamma(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;
	u16 gamma[18];
	int i;

	if (!ivb_need_sprite_gamma(plane_state))
		return;

	ivb_sprite_linear_gamma(plane_state, gamma);

	/* FIXME these register are single buffered :( */
	for (i = 0; i < 16; i++)
		intel_de_write_fw(dev_priv, SPRGAMC(pipe, i),
				  gamma[i] << 20 | gamma[i] << 10 | gamma[i]);

	intel_de_write_fw(dev_priv, SPRGAMC16(pipe, 0), gamma[i]);
	intel_de_write_fw(dev_priv, SPRGAMC16(pipe, 1), gamma[i]);
	intel_de_write_fw(dev_priv, SPRGAMC16(pipe, 2), gamma[i]);
	i++;

	intel_de_write_fw(dev_priv, SPRGAMC17(pipe, 0), gamma[i]);
	intel_de_write_fw(dev_priv, SPRGAMC17(pipe, 1), gamma[i]);
	intel_de_write_fw(dev_priv, SPRGAMC17(pipe, 2), gamma[i]);
	i++;
}

static void
ivb_update_plane(struct intel_plane *plane,
		 const struct intel_crtc_state *crtc_state,
		 const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;
	u32 sprsurf_offset = plane_state->color_plane[0].offset;
	u32 linear_offset;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	int crtc_x = plane_state->uapi.dst.x1;
	int crtc_y = plane_state->uapi.dst.y1;
	u32 crtc_w = drm_rect_width(&plane_state->uapi.dst);
	u32 crtc_h = drm_rect_height(&plane_state->uapi.dst);
	u32 x = plane_state->color_plane[0].x;
	u32 y = plane_state->color_plane[0].y;
	u32 src_w = drm_rect_width(&plane_state->uapi.src) >> 16;
	u32 src_h = drm_rect_height(&plane_state->uapi.src) >> 16;
	u32 sprctl, sprscale = 0;
	unsigned long irqflags;

	sprctl = plane_state->ctl | ivb_sprite_ctl_crtc(crtc_state);

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	crtc_w--;
	crtc_h--;

	if (crtc_w != src_w || crtc_h != src_h)
		sprscale = SPRITE_SCALE_ENABLE | (src_w << 16) | src_h;

	linear_offset = intel_fb_xy_to_linear(x, y, plane_state, 0);

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	intel_de_write_fw(dev_priv, SPRSTRIDE(pipe),
			  plane_state->color_plane[0].stride);
	intel_de_write_fw(dev_priv, SPRPOS(pipe), (crtc_y << 16) | crtc_x);
	intel_de_write_fw(dev_priv, SPRSIZE(pipe), (crtc_h << 16) | crtc_w);
	if (IS_IVYBRIDGE(dev_priv))
		intel_de_write_fw(dev_priv, SPRSCALE(pipe), sprscale);

	if (key->flags) {
		intel_de_write_fw(dev_priv, SPRKEYVAL(pipe), key->min_value);
		intel_de_write_fw(dev_priv, SPRKEYMSK(pipe),
				  key->channel_mask);
		intel_de_write_fw(dev_priv, SPRKEYMAX(pipe), key->max_value);
	}

	/* HSW consolidates SPRTILEOFF and SPRLINOFF into a single SPROFFSET
	 * register */
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		intel_de_write_fw(dev_priv, SPROFFSET(pipe), (y << 16) | x);
	} else {
		intel_de_write_fw(dev_priv, SPRLINOFF(pipe), linear_offset);
		intel_de_write_fw(dev_priv, SPRTILEOFF(pipe), (y << 16) | x);
	}

	/*
	 * The control register self-arms if the plane was previously
	 * disabled. Try to make the plane enable atomic by writing
	 * the control register just before the surface register.
	 */
	intel_de_write_fw(dev_priv, SPRCTL(pipe), sprctl);
	intel_de_write_fw(dev_priv, SPRSURF(pipe),
			  intel_plane_ggtt_offset(plane_state) + sprsurf_offset);

	ivb_update_gamma(plane_state);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static void
ivb_disable_plane(struct intel_plane *plane,
		  const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	intel_de_write_fw(dev_priv, SPRCTL(pipe), 0);
	/* Disable the scaler */
	if (IS_IVYBRIDGE(dev_priv))
		intel_de_write_fw(dev_priv, SPRSCALE(pipe), 0);
	intel_de_write_fw(dev_priv, SPRSURF(pipe), 0);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static bool
ivb_plane_get_hw_state(struct intel_plane *plane,
		       enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum intel_display_power_domain power_domain;
	intel_wakeref_t wakeref;
	bool ret;

	power_domain = POWER_DOMAIN_PIPE(plane->pipe);
	wakeref = intel_display_power_get_if_enabled(dev_priv, power_domain);
	if (!wakeref)
		return false;

	ret =  intel_de_read(dev_priv, SPRCTL(plane->pipe)) & SPRITE_ENABLE;

	*pipe = plane->pipe;

	intel_display_power_put(dev_priv, power_domain, wakeref);

	return ret;
}

static int g4x_sprite_min_cdclk(const struct intel_crtc_state *crtc_state,
				const struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int hscale, pixel_rate;
	unsigned int limit, decimate;

	/*
	 * Note that crtc_state->pixel_rate accounts for both
	 * horizontal and vertical panel fitter downscaling factors.
	 * Pre-HSW bspec tells us to only consider the horizontal
	 * downscaling factor here. We ignore that and just consider
	 * both for simplicity.
	 */
	pixel_rate = crtc_state->pixel_rate;

	/* Horizontal downscaling limits the maximum pixel rate */
	hscale = drm_rect_calc_hscale(&plane_state->uapi.src,
				      &plane_state->uapi.dst,
				      0, INT_MAX);
	hscale = max(hscale, 0x10000u);

	/* Decimation steps at 2x,4x,8x,16x */
	decimate = ilog2(hscale >> 16);
	hscale >>= decimate;

	/* Starting limit is 90% of cdclk */
	limit = 9;

	/* -10% per decimation step */
	limit -= decimate;

	/* -10% for RGB */
	if (!fb->format->is_yuv)
		limit--;

	/*
	 * We should also do -10% if sprite scaling is enabled
	 * on the other pipe, but we can't really check for that,
	 * so we ignore it.
	 */

	return DIV_ROUND_UP_ULL(mul_u32_u32(pixel_rate, 10 * hscale),
				limit << 16);
}

static unsigned int
g4x_sprite_max_stride(struct intel_plane *plane,
		      u32 pixel_format, u64 modifier,
		      unsigned int rotation)
{
	const struct drm_format_info *info = drm_format_info(pixel_format);
	int cpp = info->cpp[0];

	/* Limit to 4k pixels to guarantee TILEOFF.x doesn't get too big. */
	if (modifier == I915_FORMAT_MOD_X_TILED)
		return min(4096 * cpp, 16 * 1024);
	else
		return 16 * 1024;
}

static unsigned int
hsw_sprite_max_stride(struct intel_plane *plane,
		      u32 pixel_format, u64 modifier,
		      unsigned int rotation)
{
	const struct drm_format_info *info = drm_format_info(pixel_format);
	int cpp = info->cpp[0];

	/* Limit to 8k pixels to guarantee OFFSET.x doesn't get too big. */
	return min(8192 * cpp, 16 * 1024);
}

static u32 g4x_sprite_ctl_crtc(const struct intel_crtc_state *crtc_state)
{
	u32 dvscntr = 0;

	if (crtc_state->gamma_enable)
		dvscntr |= DVS_GAMMA_ENABLE;

	if (crtc_state->csc_enable)
		dvscntr |= DVS_PIPE_CSC_ENABLE;

	return dvscntr;
}

static u32 g4x_sprite_ctl(const struct intel_crtc_state *crtc_state,
			  const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rotation = plane_state->hw.rotation;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	u32 dvscntr;

	dvscntr = DVS_ENABLE;

	if (IS_GEN(dev_priv, 6))
		dvscntr |= DVS_TRICKLE_FEED_DISABLE;

	switch (fb->format->format) {
	case DRM_FORMAT_XBGR8888:
		dvscntr |= DVS_FORMAT_RGBX888 | DVS_RGB_ORDER_XBGR;
		break;
	case DRM_FORMAT_XRGB8888:
		dvscntr |= DVS_FORMAT_RGBX888;
		break;
	case DRM_FORMAT_XBGR2101010:
		dvscntr |= DVS_FORMAT_RGBX101010 | DVS_RGB_ORDER_XBGR;
		break;
	case DRM_FORMAT_XRGB2101010:
		dvscntr |= DVS_FORMAT_RGBX101010;
		break;
	case DRM_FORMAT_XBGR16161616F:
		dvscntr |= DVS_FORMAT_RGBX161616 | DVS_RGB_ORDER_XBGR;
		break;
	case DRM_FORMAT_XRGB16161616F:
		dvscntr |= DVS_FORMAT_RGBX161616;
		break;
	case DRM_FORMAT_YUYV:
		dvscntr |= DVS_FORMAT_YUV422 | DVS_YUV_ORDER_YUYV;
		break;
	case DRM_FORMAT_YVYU:
		dvscntr |= DVS_FORMAT_YUV422 | DVS_YUV_ORDER_YVYU;
		break;
	case DRM_FORMAT_UYVY:
		dvscntr |= DVS_FORMAT_YUV422 | DVS_YUV_ORDER_UYVY;
		break;
	case DRM_FORMAT_VYUY:
		dvscntr |= DVS_FORMAT_YUV422 | DVS_YUV_ORDER_VYUY;
		break;
	default:
		MISSING_CASE(fb->format->format);
		return 0;
	}

	if (plane_state->hw.color_encoding == DRM_COLOR_YCBCR_BT709)
		dvscntr |= DVS_YUV_FORMAT_BT709;

	if (plane_state->hw.color_range == DRM_COLOR_YCBCR_FULL_RANGE)
		dvscntr |= DVS_YUV_RANGE_CORRECTION_DISABLE;

	if (fb->modifier == I915_FORMAT_MOD_X_TILED)
		dvscntr |= DVS_TILED;

	if (rotation & DRM_MODE_ROTATE_180)
		dvscntr |= DVS_ROTATE_180;

	if (key->flags & I915_SET_COLORKEY_DESTINATION)
		dvscntr |= DVS_DEST_KEY;
	else if (key->flags & I915_SET_COLORKEY_SOURCE)
		dvscntr |= DVS_SOURCE_KEY;

	return dvscntr;
}

static void g4x_update_gamma(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	enum pipe pipe = plane->pipe;
	u16 gamma[8];
	int i;

	/* Seems RGB data bypasses the gamma always */
	if (!fb->format->is_yuv)
		return;

	i9xx_plane_linear_gamma(gamma);

	/* FIXME these register are single buffered :( */
	/* The two end points are implicit (0.0 and 1.0) */
	for (i = 1; i < 8 - 1; i++)
		intel_de_write_fw(dev_priv, DVSGAMC_G4X(pipe, i - 1),
				  gamma[i] << 16 | gamma[i] << 8 | gamma[i]);
}

static void ilk_sprite_linear_gamma(u16 gamma[17])
{
	int i;

	for (i = 0; i < 17; i++)
		gamma[i] = (i << 10) / 16;
}

static void ilk_update_gamma(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	enum pipe pipe = plane->pipe;
	u16 gamma[17];
	int i;

	/* Seems RGB data bypasses the gamma always */
	if (!fb->format->is_yuv)
		return;

	ilk_sprite_linear_gamma(gamma);

	/* FIXME these register are single buffered :( */
	for (i = 0; i < 16; i++)
		intel_de_write_fw(dev_priv, DVSGAMC_ILK(pipe, i),
				  gamma[i] << 20 | gamma[i] << 10 | gamma[i]);

	intel_de_write_fw(dev_priv, DVSGAMCMAX_ILK(pipe, 0), gamma[i]);
	intel_de_write_fw(dev_priv, DVSGAMCMAX_ILK(pipe, 1), gamma[i]);
	intel_de_write_fw(dev_priv, DVSGAMCMAX_ILK(pipe, 2), gamma[i]);
	i++;
}

static void
g4x_update_plane(struct intel_plane *plane,
		 const struct intel_crtc_state *crtc_state,
		 const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;
	u32 dvssurf_offset = plane_state->color_plane[0].offset;
	u32 linear_offset;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	int crtc_x = plane_state->uapi.dst.x1;
	int crtc_y = plane_state->uapi.dst.y1;
	u32 crtc_w = drm_rect_width(&plane_state->uapi.dst);
	u32 crtc_h = drm_rect_height(&plane_state->uapi.dst);
	u32 x = plane_state->color_plane[0].x;
	u32 y = plane_state->color_plane[0].y;
	u32 src_w = drm_rect_width(&plane_state->uapi.src) >> 16;
	u32 src_h = drm_rect_height(&plane_state->uapi.src) >> 16;
	u32 dvscntr, dvsscale = 0;
	unsigned long irqflags;

	dvscntr = plane_state->ctl | g4x_sprite_ctl_crtc(crtc_state);

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	crtc_w--;
	crtc_h--;

	if (crtc_w != src_w || crtc_h != src_h)
		dvsscale = DVS_SCALE_ENABLE | (src_w << 16) | src_h;

	linear_offset = intel_fb_xy_to_linear(x, y, plane_state, 0);

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	intel_de_write_fw(dev_priv, DVSSTRIDE(pipe),
			  plane_state->color_plane[0].stride);
	intel_de_write_fw(dev_priv, DVSPOS(pipe), (crtc_y << 16) | crtc_x);
	intel_de_write_fw(dev_priv, DVSSIZE(pipe), (crtc_h << 16) | crtc_w);
	intel_de_write_fw(dev_priv, DVSSCALE(pipe), dvsscale);

	if (key->flags) {
		intel_de_write_fw(dev_priv, DVSKEYVAL(pipe), key->min_value);
		intel_de_write_fw(dev_priv, DVSKEYMSK(pipe),
				  key->channel_mask);
		intel_de_write_fw(dev_priv, DVSKEYMAX(pipe), key->max_value);
	}

	intel_de_write_fw(dev_priv, DVSLINOFF(pipe), linear_offset);
	intel_de_write_fw(dev_priv, DVSTILEOFF(pipe), (y << 16) | x);

	/*
	 * The control register self-arms if the plane was previously
	 * disabled. Try to make the plane enable atomic by writing
	 * the control register just before the surface register.
	 */
	intel_de_write_fw(dev_priv, DVSCNTR(pipe), dvscntr);
	intel_de_write_fw(dev_priv, DVSSURF(pipe),
			  intel_plane_ggtt_offset(plane_state) + dvssurf_offset);

	if (IS_G4X(dev_priv))
		g4x_update_gamma(plane_state);
	else
		ilk_update_gamma(plane_state);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static void
g4x_disable_plane(struct intel_plane *plane,
		  const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	intel_de_write_fw(dev_priv, DVSCNTR(pipe), 0);
	/* Disable the scaler */
	intel_de_write_fw(dev_priv, DVSSCALE(pipe), 0);
	intel_de_write_fw(dev_priv, DVSSURF(pipe), 0);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static bool
g4x_plane_get_hw_state(struct intel_plane *plane,
		       enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum intel_display_power_domain power_domain;
	intel_wakeref_t wakeref;
	bool ret;

	power_domain = POWER_DOMAIN_PIPE(plane->pipe);
	wakeref = intel_display_power_get_if_enabled(dev_priv, power_domain);
	if (!wakeref)
		return false;

	ret = intel_de_read(dev_priv, DVSCNTR(plane->pipe)) & DVS_ENABLE;

	*pipe = plane->pipe;

	intel_display_power_put(dev_priv, power_domain, wakeref);

	return ret;
}

static bool g4x_fb_scalable(const struct drm_framebuffer *fb)
{
	if (!fb)
		return false;

	switch (fb->format->format) {
	case DRM_FORMAT_C8:
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ABGR16161616F:
		return false;
	default:
		return true;
	}
}

static int
g4x_sprite_check_scaling(struct intel_crtc_state *crtc_state,
			 struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	const struct drm_rect *src = &plane_state->uapi.src;
	const struct drm_rect *dst = &plane_state->uapi.dst;
	int src_x, src_w, src_h, crtc_w, crtc_h;
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	unsigned int stride = plane_state->color_plane[0].stride;
	unsigned int cpp = fb->format->cpp[0];
	unsigned int width_bytes;
	int min_width, min_height;

	crtc_w = drm_rect_width(dst);
	crtc_h = drm_rect_height(dst);

	src_x = src->x1 >> 16;
	src_w = drm_rect_width(src) >> 16;
	src_h = drm_rect_height(src) >> 16;

	if (src_w == crtc_w && src_h == crtc_h)
		return 0;

	min_width = 3;

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		if (src_h & 1) {
			DRM_DEBUG_KMS("Source height must be even with interlaced modes\n");
			return -EINVAL;
		}
		min_height = 6;
	} else {
		min_height = 3;
	}

	width_bytes = ((src_x * cpp) & 63) + src_w * cpp;

	if (src_w < min_width || src_h < min_height ||
	    src_w > 2048 || src_h > 2048) {
		DRM_DEBUG_KMS("Source dimensions (%dx%d) exceed hardware limits (%dx%d - %dx%d)\n",
			      src_w, src_h, min_width, min_height, 2048, 2048);
		return -EINVAL;
	}

	if (width_bytes > 4096) {
		DRM_DEBUG_KMS("Fetch width (%d) exceeds hardware max with scaling (%u)\n",
			      width_bytes, 4096);
		return -EINVAL;
	}

	if (stride > 4096) {
		DRM_DEBUG_KMS("Stride (%u) exceeds hardware max with scaling (%u)\n",
			      stride, 4096);
		return -EINVAL;
	}

	return 0;
}

static int
g4x_sprite_check(struct intel_crtc_state *crtc_state,
		 struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	int min_scale = DRM_PLANE_HELPER_NO_SCALING;
	int max_scale = DRM_PLANE_HELPER_NO_SCALING;
	int ret;

	if (g4x_fb_scalable(plane_state->hw.fb)) {
		if (INTEL_GEN(dev_priv) < 7) {
			min_scale = 1;
			max_scale = 16 << 16;
		} else if (IS_IVYBRIDGE(dev_priv)) {
			min_scale = 1;
			max_scale = 2 << 16;
		}
	}

	ret = intel_atomic_plane_check_clipping(plane_state, crtc_state,
						min_scale, max_scale, true);
	if (ret)
		return ret;

	ret = i9xx_check_plane_surface(plane_state);
	if (ret)
		return ret;

	if (!plane_state->uapi.visible)
		return 0;

	ret = intel_plane_check_src_coordinates(plane_state);
	if (ret)
		return ret;

	ret = g4x_sprite_check_scaling(crtc_state, plane_state);
	if (ret)
		return ret;

	if (INTEL_GEN(dev_priv) >= 7)
		plane_state->ctl = ivb_sprite_ctl(crtc_state, plane_state);
	else
		plane_state->ctl = g4x_sprite_ctl(crtc_state, plane_state);

	return 0;
}

int chv_plane_check_rotation(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	unsigned int rotation = plane_state->hw.rotation;

	/* CHV ignores the mirror bit when the rotate bit is set :( */
	if (IS_CHERRYVIEW(dev_priv) &&
	    rotation & DRM_MODE_ROTATE_180 &&
	    rotation & DRM_MODE_REFLECT_X) {
		drm_dbg_kms(&dev_priv->drm,
			    "Cannot rotate and reflect at the same time\n");
		return -EINVAL;
	}

	return 0;
}

static int
vlv_sprite_check(struct intel_crtc_state *crtc_state,
		 struct intel_plane_state *plane_state)
{
	int ret;

	ret = chv_plane_check_rotation(plane_state);
	if (ret)
		return ret;

	ret = intel_atomic_plane_check_clipping(plane_state, crtc_state,
						DRM_PLANE_HELPER_NO_SCALING,
						DRM_PLANE_HELPER_NO_SCALING,
						true);
	if (ret)
		return ret;

	ret = i9xx_check_plane_surface(plane_state);
	if (ret)
		return ret;

	if (!plane_state->uapi.visible)
		return 0;

	ret = intel_plane_check_src_coordinates(plane_state);
	if (ret)
		return ret;

	plane_state->ctl = vlv_sprite_ctl(crtc_state, plane_state);

	return 0;
}

static bool has_dst_key_in_primary_plane(struct drm_i915_private *dev_priv)
{
	return INTEL_GEN(dev_priv) >= 9;
}

static void intel_plane_set_ckey(struct intel_plane_state *plane_state,
				 const struct drm_intel_sprite_colorkey *set)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	struct drm_intel_sprite_colorkey *key = &plane_state->ckey;

	*key = *set;

	/*
	 * We want src key enabled on the
	 * sprite and not on the primary.
	 */
	if (plane->id == PLANE_PRIMARY &&
	    set->flags & I915_SET_COLORKEY_SOURCE)
		key->flags = 0;

	/*
	 * On SKL+ we want dst key enabled on
	 * the primary and not on the sprite.
	 */
	if (INTEL_GEN(dev_priv) >= 9 && plane->id != PLANE_PRIMARY &&
	    set->flags & I915_SET_COLORKEY_DESTINATION)
		key->flags = 0;
}

int intel_sprite_set_colorkey_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_intel_sprite_colorkey *set = data;
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	struct drm_atomic_state *state;
	struct drm_modeset_acquire_ctx ctx;
	int ret = 0;

	/* ignore the pointless "none" flag */
	set->flags &= ~I915_SET_COLORKEY_NONE;

	if (set->flags & ~(I915_SET_COLORKEY_DESTINATION | I915_SET_COLORKEY_SOURCE))
		return -EINVAL;

	/* Make sure we don't try to enable both src & dest simultaneously */
	if ((set->flags & (I915_SET_COLORKEY_DESTINATION | I915_SET_COLORKEY_SOURCE)) == (I915_SET_COLORKEY_DESTINATION | I915_SET_COLORKEY_SOURCE))
		return -EINVAL;

	if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    set->flags & I915_SET_COLORKEY_DESTINATION)
		return -EINVAL;

	plane = drm_plane_find(dev, file_priv, set->plane_id);
	if (!plane || plane->type != DRM_PLANE_TYPE_OVERLAY)
		return -ENOENT;

	/*
	 * SKL+ only plane 2 can do destination keying against plane 1.
	 * Also multiple planes can't do destination keying on the same
	 * pipe simultaneously.
	 */
	if (INTEL_GEN(dev_priv) >= 9 &&
	    to_intel_plane(plane)->id >= PLANE_SPRITE1 &&
	    set->flags & I915_SET_COLORKEY_DESTINATION)
		return -EINVAL;

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_atomic_state_alloc(plane->dev);
	if (!state) {
		ret = -ENOMEM;
		goto out;
	}
	state->acquire_ctx = &ctx;

	while (1) {
		plane_state = drm_atomic_get_plane_state(state, plane);
		ret = PTR_ERR_OR_ZERO(plane_state);
		if (!ret)
			intel_plane_set_ckey(to_intel_plane_state(plane_state), set);

		/*
		 * On some platforms we have to configure
		 * the dst colorkey on the primary plane.
		 */
		if (!ret && has_dst_key_in_primary_plane(dev_priv)) {
			struct intel_crtc *crtc =
				intel_get_crtc_for_pipe(dev_priv,
							to_intel_plane(plane)->pipe);

			plane_state = drm_atomic_get_plane_state(state,
								 crtc->base.primary);
			ret = PTR_ERR_OR_ZERO(plane_state);
			if (!ret)
				intel_plane_set_ckey(to_intel_plane_state(plane_state), set);
		}

		if (!ret)
			ret = drm_atomic_commit(state);

		if (ret != -EDEADLK)
			break;

		drm_atomic_state_clear(state);
		drm_modeset_backoff(&ctx);
	}

	drm_atomic_state_put(state);
out:
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	return ret;
}

static const u32 g4x_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
};

static const u64 i9xx_plane_format_modifiers[] = {
	I915_FORMAT_MOD_X_TILED,
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static const u32 snb_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_XRGB16161616F,
	DRM_FORMAT_XBGR16161616F,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
};

static const u32 vlv_plane_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
};

static const u32 chv_pipe_b_sprite_formats[] = {
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
};

static bool g4x_sprite_format_mod_supported(struct drm_plane *_plane,
					    u32 format, u64 modifier)
{
	switch (modifier) {
	case DRM_FORMAT_MOD_LINEAR:
	case I915_FORMAT_MOD_X_TILED:
		break;
	default:
		return false;
	}

	switch (format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		if (modifier == DRM_FORMAT_MOD_LINEAR ||
		    modifier == I915_FORMAT_MOD_X_TILED)
			return true;
		fallthrough;
	default:
		return false;
	}
}

static bool snb_sprite_format_mod_supported(struct drm_plane *_plane,
					    u32 format, u64 modifier)
{
	switch (modifier) {
	case DRM_FORMAT_MOD_LINEAR:
	case I915_FORMAT_MOD_X_TILED:
		break;
	default:
		return false;
	}

	switch (format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		if (modifier == DRM_FORMAT_MOD_LINEAR ||
		    modifier == I915_FORMAT_MOD_X_TILED)
			return true;
		fallthrough;
	default:
		return false;
	}
}

static bool vlv_sprite_format_mod_supported(struct drm_plane *_plane,
					    u32 format, u64 modifier)
{
	switch (modifier) {
	case DRM_FORMAT_MOD_LINEAR:
	case I915_FORMAT_MOD_X_TILED:
		break;
	default:
		return false;
	}

	switch (format) {
	case DRM_FORMAT_C8:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		if (modifier == DRM_FORMAT_MOD_LINEAR ||
		    modifier == I915_FORMAT_MOD_X_TILED)
			return true;
		fallthrough;
	default:
		return false;
	}
}

static const struct drm_plane_funcs g4x_sprite_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = intel_plane_destroy,
	.atomic_duplicate_state = intel_plane_duplicate_state,
	.atomic_destroy_state = intel_plane_destroy_state,
	.format_mod_supported = g4x_sprite_format_mod_supported,
};

static const struct drm_plane_funcs snb_sprite_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = intel_plane_destroy,
	.atomic_duplicate_state = intel_plane_duplicate_state,
	.atomic_destroy_state = intel_plane_destroy_state,
	.format_mod_supported = snb_sprite_format_mod_supported,
};

static const struct drm_plane_funcs vlv_sprite_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = intel_plane_destroy,
	.atomic_duplicate_state = intel_plane_duplicate_state,
	.atomic_destroy_state = intel_plane_destroy_state,
	.format_mod_supported = vlv_sprite_format_mod_supported,
};

struct intel_plane *
intel_sprite_plane_create(struct drm_i915_private *dev_priv,
			  enum pipe pipe, int sprite)
{
	struct intel_plane *plane;
	const struct drm_plane_funcs *plane_funcs;
	unsigned int supported_rotations;
	const u64 *modifiers;
	const u32 *formats;
	int num_formats;
	int ret, zpos;

	plane = intel_plane_alloc();
	if (IS_ERR(plane))
		return plane;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		plane->update_plane = vlv_update_plane;
		plane->disable_plane = vlv_disable_plane;
		plane->get_hw_state = vlv_plane_get_hw_state;
		plane->check_plane = vlv_sprite_check;
		plane->max_stride = i965_plane_max_stride;
		plane->min_cdclk = vlv_plane_min_cdclk;

		if (IS_CHERRYVIEW(dev_priv) && pipe == PIPE_B) {
			formats = chv_pipe_b_sprite_formats;
			num_formats = ARRAY_SIZE(chv_pipe_b_sprite_formats);
		} else {
			formats = vlv_plane_formats;
			num_formats = ARRAY_SIZE(vlv_plane_formats);
		}
		modifiers = i9xx_plane_format_modifiers;

		plane_funcs = &vlv_sprite_funcs;
	} else if (INTEL_GEN(dev_priv) >= 7) {
		plane->update_plane = ivb_update_plane;
		plane->disable_plane = ivb_disable_plane;
		plane->get_hw_state = ivb_plane_get_hw_state;
		plane->check_plane = g4x_sprite_check;

		if (IS_BROADWELL(dev_priv) || IS_HASWELL(dev_priv)) {
			plane->max_stride = hsw_sprite_max_stride;
			plane->min_cdclk = hsw_plane_min_cdclk;
		} else {
			plane->max_stride = g4x_sprite_max_stride;
			plane->min_cdclk = ivb_sprite_min_cdclk;
		}

		formats = snb_plane_formats;
		num_formats = ARRAY_SIZE(snb_plane_formats);
		modifiers = i9xx_plane_format_modifiers;

		plane_funcs = &snb_sprite_funcs;
	} else {
		plane->update_plane = g4x_update_plane;
		plane->disable_plane = g4x_disable_plane;
		plane->get_hw_state = g4x_plane_get_hw_state;
		plane->check_plane = g4x_sprite_check;
		plane->max_stride = g4x_sprite_max_stride;
		plane->min_cdclk = g4x_sprite_min_cdclk;

		modifiers = i9xx_plane_format_modifiers;
		if (IS_GEN(dev_priv, 6)) {
			formats = snb_plane_formats;
			num_formats = ARRAY_SIZE(snb_plane_formats);

			plane_funcs = &snb_sprite_funcs;
		} else {
			formats = g4x_plane_formats;
			num_formats = ARRAY_SIZE(g4x_plane_formats);

			plane_funcs = &g4x_sprite_funcs;
		}
	}

	if (IS_CHERRYVIEW(dev_priv) && pipe == PIPE_B) {
		supported_rotations =
			DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_180 |
			DRM_MODE_REFLECT_X;
	} else {
		supported_rotations =
			DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_180;
	}

	plane->pipe = pipe;
	plane->id = PLANE_SPRITE0 + sprite;
	plane->frontbuffer_bit = INTEL_FRONTBUFFER(pipe, plane->id);

	ret = drm_universal_plane_init(&dev_priv->drm, &plane->base,
				       0, plane_funcs,
				       formats, num_formats, modifiers,
				       DRM_PLANE_TYPE_OVERLAY,
				       "sprite %c", sprite_name(pipe, sprite));
	if (ret)
		goto fail;

	drm_plane_create_rotation_property(&plane->base,
					   DRM_MODE_ROTATE_0,
					   supported_rotations);

	drm_plane_create_color_properties(&plane->base,
					  BIT(DRM_COLOR_YCBCR_BT601) |
					  BIT(DRM_COLOR_YCBCR_BT709),
					  BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) |
					  BIT(DRM_COLOR_YCBCR_FULL_RANGE),
					  DRM_COLOR_YCBCR_BT709,
					  DRM_COLOR_YCBCR_LIMITED_RANGE);

	zpos = sprite + 1;
	drm_plane_create_zpos_immutable_property(&plane->base, zpos);

	drm_plane_helper_add(&plane->base, &intel_plane_helper_funcs);

	return plane;

fail:
	intel_plane_free(plane);

	return ERR_PTR(ret);
}
