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
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fourcc.h>
#include "intel_drv.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"

static void
vlv_update_plane(struct drm_plane *dplane, struct drm_framebuffer *fb,
		 struct drm_i915_gem_object *obj, int crtc_x, int crtc_y,
		 unsigned int crtc_w, unsigned int crtc_h,
		 uint32_t x, uint32_t y,
		 uint32_t src_w, uint32_t src_h)
{
	struct drm_device *dev = dplane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(dplane);
	int pipe = intel_plane->pipe;
	int plane = intel_plane->plane;
	u32 sprctl;
	unsigned long sprsurf_offset, linear_offset;
	int pixel_size = drm_format_plane_cpp(fb->pixel_format, 0);

	sprctl = I915_READ(SPCNTR(pipe, plane));

	/* Mask out pixel format bits in case we change it */
	sprctl &= ~SP_PIXFORMAT_MASK;
	sprctl &= ~SP_YUV_BYTE_ORDER_MASK;
	sprctl &= ~SP_TILED;

	switch (fb->pixel_format) {
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
	case DRM_FORMAT_XBGR8888:
		sprctl |= SP_FORMAT_RGBX8888;
		break;
	case DRM_FORMAT_ABGR8888:
		sprctl |= SP_FORMAT_RGBA8888;
		break;
	default:
		/*
		 * If we get here one of the upper layers failed to filter
		 * out the unsupported plane formats
		 */
		BUG();
		break;
	}

	if (obj->tiling_mode != I915_TILING_NONE)
		sprctl |= SP_TILED;

	sprctl |= SP_ENABLE;

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	crtc_w--;
	crtc_h--;

	intel_update_sprite_watermarks(dev, pipe, crtc_w, pixel_size);

	I915_WRITE(SPSTRIDE(pipe, plane), fb->pitches[0]);
	I915_WRITE(SPPOS(pipe, plane), (crtc_y << 16) | crtc_x);

	linear_offset = y * fb->pitches[0] + x * pixel_size;
	sprsurf_offset = intel_gen4_compute_page_offset(&x, &y,
							obj->tiling_mode,
							pixel_size,
							fb->pitches[0]);
	linear_offset -= sprsurf_offset;

	if (obj->tiling_mode != I915_TILING_NONE)
		I915_WRITE(SPTILEOFF(pipe, plane), (y << 16) | x);
	else
		I915_WRITE(SPLINOFF(pipe, plane), linear_offset);

	I915_WRITE(SPSIZE(pipe, plane), (crtc_h << 16) | crtc_w);
	I915_WRITE(SPCNTR(pipe, plane), sprctl);
	I915_MODIFY_DISPBASE(SPSURF(pipe, plane), obj->gtt_offset +
			     sprsurf_offset);
	POSTING_READ(SPSURF(pipe, plane));
}

static void
vlv_disable_plane(struct drm_plane *dplane)
{
	struct drm_device *dev = dplane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(dplane);
	int pipe = intel_plane->pipe;
	int plane = intel_plane->plane;

	I915_WRITE(SPCNTR(pipe, plane), I915_READ(SPCNTR(pipe, plane)) &
		   ~SP_ENABLE);
	/* Activate double buffered register update */
	I915_MODIFY_DISPBASE(SPSURF(pipe, plane), 0);
	POSTING_READ(SPSURF(pipe, plane));
}

static int
vlv_update_colorkey(struct drm_plane *dplane,
		    struct drm_intel_sprite_colorkey *key)
{
	struct drm_device *dev = dplane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(dplane);
	int pipe = intel_plane->pipe;
	int plane = intel_plane->plane;
	u32 sprctl;

	if (key->flags & I915_SET_COLORKEY_DESTINATION)
		return -EINVAL;

	I915_WRITE(SPKEYMINVAL(pipe, plane), key->min_value);
	I915_WRITE(SPKEYMAXVAL(pipe, plane), key->max_value);
	I915_WRITE(SPKEYMSK(pipe, plane), key->channel_mask);

	sprctl = I915_READ(SPCNTR(pipe, plane));
	sprctl &= ~SP_SOURCE_KEY;
	if (key->flags & I915_SET_COLORKEY_SOURCE)
		sprctl |= SP_SOURCE_KEY;
	I915_WRITE(SPCNTR(pipe, plane), sprctl);

	POSTING_READ(SPKEYMSK(pipe, plane));

	return 0;
}

static void
vlv_get_colorkey(struct drm_plane *dplane,
		 struct drm_intel_sprite_colorkey *key)
{
	struct drm_device *dev = dplane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(dplane);
	int pipe = intel_plane->pipe;
	int plane = intel_plane->plane;
	u32 sprctl;

	key->min_value = I915_READ(SPKEYMINVAL(pipe, plane));
	key->max_value = I915_READ(SPKEYMAXVAL(pipe, plane));
	key->channel_mask = I915_READ(SPKEYMSK(pipe, plane));

	sprctl = I915_READ(SPCNTR(pipe, plane));
	if (sprctl & SP_SOURCE_KEY)
		key->flags = I915_SET_COLORKEY_SOURCE;
	else
		key->flags = I915_SET_COLORKEY_NONE;
}

static void
ivb_update_plane(struct drm_plane *plane, struct drm_framebuffer *fb,
		 struct drm_i915_gem_object *obj, int crtc_x, int crtc_y,
		 unsigned int crtc_w, unsigned int crtc_h,
		 uint32_t x, uint32_t y,
		 uint32_t src_w, uint32_t src_h)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	int pipe = intel_plane->pipe;
	u32 sprctl, sprscale = 0;
	unsigned long sprsurf_offset, linear_offset;
	int pixel_size = drm_format_plane_cpp(fb->pixel_format, 0);
	bool scaling_was_enabled = dev_priv->sprite_scaling_enabled;

	sprctl = I915_READ(SPRCTL(pipe));

	/* Mask out pixel format bits in case we change it */
	sprctl &= ~SPRITE_PIXFORMAT_MASK;
	sprctl &= ~SPRITE_RGB_ORDER_RGBX;
	sprctl &= ~SPRITE_YUV_BYTE_ORDER_MASK;
	sprctl &= ~SPRITE_TILED;

	switch (fb->pixel_format) {
	case DRM_FORMAT_XBGR8888:
		sprctl |= SPRITE_FORMAT_RGBX888 | SPRITE_RGB_ORDER_RGBX;
		break;
	case DRM_FORMAT_XRGB8888:
		sprctl |= SPRITE_FORMAT_RGBX888;
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
		BUG();
	}

	if (obj->tiling_mode != I915_TILING_NONE)
		sprctl |= SPRITE_TILED;

	/* must disable */
	sprctl |= SPRITE_TRICKLE_FEED_DISABLE;
	sprctl |= SPRITE_ENABLE;

	if (IS_HASWELL(dev))
		sprctl |= SPRITE_PIPE_CSC_ENABLE;

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	crtc_w--;
	crtc_h--;

	intel_update_sprite_watermarks(dev, pipe, crtc_w, pixel_size);

	/*
	 * IVB workaround: must disable low power watermarks for at least
	 * one frame before enabling scaling.  LP watermarks can be re-enabled
	 * when scaling is disabled.
	 */
	if (crtc_w != src_w || crtc_h != src_h) {
		dev_priv->sprite_scaling_enabled |= 1 << pipe;

		if (!scaling_was_enabled) {
			intel_update_watermarks(dev);
			intel_wait_for_vblank(dev, pipe);
		}
		sprscale = SPRITE_SCALE_ENABLE | (src_w << 16) | src_h;
	} else
		dev_priv->sprite_scaling_enabled &= ~(1 << pipe);

	I915_WRITE(SPRSTRIDE(pipe), fb->pitches[0]);
	I915_WRITE(SPRPOS(pipe), (crtc_y << 16) | crtc_x);

	linear_offset = y * fb->pitches[0] + x * pixel_size;
	sprsurf_offset =
		intel_gen4_compute_page_offset(&x, &y, obj->tiling_mode,
					       pixel_size, fb->pitches[0]);
	linear_offset -= sprsurf_offset;

	/* HSW consolidates SPRTILEOFF and SPRLINOFF into a single SPROFFSET
	 * register */
	if (IS_HASWELL(dev))
		I915_WRITE(SPROFFSET(pipe), (y << 16) | x);
	else if (obj->tiling_mode != I915_TILING_NONE)
		I915_WRITE(SPRTILEOFF(pipe), (y << 16) | x);
	else
		I915_WRITE(SPRLINOFF(pipe), linear_offset);

	I915_WRITE(SPRSIZE(pipe), (crtc_h << 16) | crtc_w);
	if (intel_plane->can_scale)
		I915_WRITE(SPRSCALE(pipe), sprscale);
	I915_WRITE(SPRCTL(pipe), sprctl);
	I915_MODIFY_DISPBASE(SPRSURF(pipe), obj->gtt_offset + sprsurf_offset);
	POSTING_READ(SPRSURF(pipe));

	/* potentially re-enable LP watermarks */
	if (scaling_was_enabled && !dev_priv->sprite_scaling_enabled)
		intel_update_watermarks(dev);
}

static void
ivb_disable_plane(struct drm_plane *plane)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	int pipe = intel_plane->pipe;
	bool scaling_was_enabled = dev_priv->sprite_scaling_enabled;

	I915_WRITE(SPRCTL(pipe), I915_READ(SPRCTL(pipe)) & ~SPRITE_ENABLE);
	/* Can't leave the scaler enabled... */
	if (intel_plane->can_scale)
		I915_WRITE(SPRSCALE(pipe), 0);
	/* Activate double buffered register update */
	I915_MODIFY_DISPBASE(SPRSURF(pipe), 0);
	POSTING_READ(SPRSURF(pipe));

	dev_priv->sprite_scaling_enabled &= ~(1 << pipe);

	/* potentially re-enable LP watermarks */
	if (scaling_was_enabled && !dev_priv->sprite_scaling_enabled)
		intel_update_watermarks(dev);
}

static int
ivb_update_colorkey(struct drm_plane *plane,
		    struct drm_intel_sprite_colorkey *key)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane;
	u32 sprctl;
	int ret = 0;

	intel_plane = to_intel_plane(plane);

	I915_WRITE(SPRKEYVAL(intel_plane->pipe), key->min_value);
	I915_WRITE(SPRKEYMAX(intel_plane->pipe), key->max_value);
	I915_WRITE(SPRKEYMSK(intel_plane->pipe), key->channel_mask);

	sprctl = I915_READ(SPRCTL(intel_plane->pipe));
	sprctl &= ~(SPRITE_SOURCE_KEY | SPRITE_DEST_KEY);
	if (key->flags & I915_SET_COLORKEY_DESTINATION)
		sprctl |= SPRITE_DEST_KEY;
	else if (key->flags & I915_SET_COLORKEY_SOURCE)
		sprctl |= SPRITE_SOURCE_KEY;
	I915_WRITE(SPRCTL(intel_plane->pipe), sprctl);

	POSTING_READ(SPRKEYMSK(intel_plane->pipe));

	return ret;
}

static void
ivb_get_colorkey(struct drm_plane *plane, struct drm_intel_sprite_colorkey *key)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane;
	u32 sprctl;

	intel_plane = to_intel_plane(plane);

	key->min_value = I915_READ(SPRKEYVAL(intel_plane->pipe));
	key->max_value = I915_READ(SPRKEYMAX(intel_plane->pipe));
	key->channel_mask = I915_READ(SPRKEYMSK(intel_plane->pipe));
	key->flags = 0;

	sprctl = I915_READ(SPRCTL(intel_plane->pipe));

	if (sprctl & SPRITE_DEST_KEY)
		key->flags = I915_SET_COLORKEY_DESTINATION;
	else if (sprctl & SPRITE_SOURCE_KEY)
		key->flags = I915_SET_COLORKEY_SOURCE;
	else
		key->flags = I915_SET_COLORKEY_NONE;
}

static void
ilk_update_plane(struct drm_plane *plane, struct drm_framebuffer *fb,
		 struct drm_i915_gem_object *obj, int crtc_x, int crtc_y,
		 unsigned int crtc_w, unsigned int crtc_h,
		 uint32_t x, uint32_t y,
		 uint32_t src_w, uint32_t src_h)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	int pipe = intel_plane->pipe;
	unsigned long dvssurf_offset, linear_offset;
	u32 dvscntr, dvsscale;
	int pixel_size = drm_format_plane_cpp(fb->pixel_format, 0);

	dvscntr = I915_READ(DVSCNTR(pipe));

	/* Mask out pixel format bits in case we change it */
	dvscntr &= ~DVS_PIXFORMAT_MASK;
	dvscntr &= ~DVS_RGB_ORDER_XBGR;
	dvscntr &= ~DVS_YUV_BYTE_ORDER_MASK;
	dvscntr &= ~DVS_TILED;

	switch (fb->pixel_format) {
	case DRM_FORMAT_XBGR8888:
		dvscntr |= DVS_FORMAT_RGBX888 | DVS_RGB_ORDER_XBGR;
		break;
	case DRM_FORMAT_XRGB8888:
		dvscntr |= DVS_FORMAT_RGBX888;
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
		BUG();
	}

	if (obj->tiling_mode != I915_TILING_NONE)
		dvscntr |= DVS_TILED;

	if (IS_GEN6(dev))
		dvscntr |= DVS_TRICKLE_FEED_DISABLE; /* must disable */
	dvscntr |= DVS_ENABLE;

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	crtc_w--;
	crtc_h--;

	intel_update_sprite_watermarks(dev, pipe, crtc_w, pixel_size);

	dvsscale = 0;
	if (IS_GEN5(dev) || crtc_w != src_w || crtc_h != src_h)
		dvsscale = DVS_SCALE_ENABLE | (src_w << 16) | src_h;

	I915_WRITE(DVSSTRIDE(pipe), fb->pitches[0]);
	I915_WRITE(DVSPOS(pipe), (crtc_y << 16) | crtc_x);

	linear_offset = y * fb->pitches[0] + x * pixel_size;
	dvssurf_offset =
		intel_gen4_compute_page_offset(&x, &y, obj->tiling_mode,
					       pixel_size, fb->pitches[0]);
	linear_offset -= dvssurf_offset;

	if (obj->tiling_mode != I915_TILING_NONE)
		I915_WRITE(DVSTILEOFF(pipe), (y << 16) | x);
	else
		I915_WRITE(DVSLINOFF(pipe), linear_offset);

	I915_WRITE(DVSSIZE(pipe), (crtc_h << 16) | crtc_w);
	I915_WRITE(DVSSCALE(pipe), dvsscale);
	I915_WRITE(DVSCNTR(pipe), dvscntr);
	I915_MODIFY_DISPBASE(DVSSURF(pipe), obj->gtt_offset + dvssurf_offset);
	POSTING_READ(DVSSURF(pipe));
}

static void
ilk_disable_plane(struct drm_plane *plane)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	int pipe = intel_plane->pipe;

	I915_WRITE(DVSCNTR(pipe), I915_READ(DVSCNTR(pipe)) & ~DVS_ENABLE);
	/* Disable the scaler */
	I915_WRITE(DVSSCALE(pipe), 0);
	/* Flush double buffered register updates */
	I915_MODIFY_DISPBASE(DVSSURF(pipe), 0);
	POSTING_READ(DVSSURF(pipe));
}

static void
intel_enable_primary(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int reg = DSPCNTR(intel_crtc->plane);

	if (!intel_crtc->primary_disabled)
		return;

	intel_crtc->primary_disabled = false;
	intel_update_fbc(dev);

	I915_WRITE(reg, I915_READ(reg) | DISPLAY_PLANE_ENABLE);
}

static void
intel_disable_primary(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int reg = DSPCNTR(intel_crtc->plane);

	if (intel_crtc->primary_disabled)
		return;

	I915_WRITE(reg, I915_READ(reg) & ~DISPLAY_PLANE_ENABLE);

	intel_crtc->primary_disabled = true;
	intel_update_fbc(dev);
}

static int
ilk_update_colorkey(struct drm_plane *plane,
		    struct drm_intel_sprite_colorkey *key)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane;
	u32 dvscntr;
	int ret = 0;

	intel_plane = to_intel_plane(plane);

	I915_WRITE(DVSKEYVAL(intel_plane->pipe), key->min_value);
	I915_WRITE(DVSKEYMAX(intel_plane->pipe), key->max_value);
	I915_WRITE(DVSKEYMSK(intel_plane->pipe), key->channel_mask);

	dvscntr = I915_READ(DVSCNTR(intel_plane->pipe));
	dvscntr &= ~(DVS_SOURCE_KEY | DVS_DEST_KEY);
	if (key->flags & I915_SET_COLORKEY_DESTINATION)
		dvscntr |= DVS_DEST_KEY;
	else if (key->flags & I915_SET_COLORKEY_SOURCE)
		dvscntr |= DVS_SOURCE_KEY;
	I915_WRITE(DVSCNTR(intel_plane->pipe), dvscntr);

	POSTING_READ(DVSKEYMSK(intel_plane->pipe));

	return ret;
}

static void
ilk_get_colorkey(struct drm_plane *plane, struct drm_intel_sprite_colorkey *key)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane;
	u32 dvscntr;

	intel_plane = to_intel_plane(plane);

	key->min_value = I915_READ(DVSKEYVAL(intel_plane->pipe));
	key->max_value = I915_READ(DVSKEYMAX(intel_plane->pipe));
	key->channel_mask = I915_READ(DVSKEYMSK(intel_plane->pipe));
	key->flags = 0;

	dvscntr = I915_READ(DVSCNTR(intel_plane->pipe));

	if (dvscntr & DVS_DEST_KEY)
		key->flags = I915_SET_COLORKEY_DESTINATION;
	else if (dvscntr & DVS_SOURCE_KEY)
		key->flags = I915_SET_COLORKEY_SOURCE;
	else
		key->flags = I915_SET_COLORKEY_NONE;
}

static int
intel_update_plane(struct drm_plane *plane, struct drm_crtc *crtc,
		   struct drm_framebuffer *fb, int crtc_x, int crtc_y,
		   unsigned int crtc_w, unsigned int crtc_h,
		   uint32_t src_x, uint32_t src_y,
		   uint32_t src_w, uint32_t src_h)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct intel_framebuffer *intel_fb;
	struct drm_i915_gem_object *obj, *old_obj;
	int pipe = intel_plane->pipe;
	enum transcoder cpu_transcoder = intel_pipe_to_cpu_transcoder(dev_priv,
								      pipe);
	int ret = 0;
	int x = src_x >> 16, y = src_y >> 16;
	int primary_w = crtc->mode.hdisplay, primary_h = crtc->mode.vdisplay;
	bool disable_primary = false;

	intel_fb = to_intel_framebuffer(fb);
	obj = intel_fb->obj;

	old_obj = intel_plane->obj;

	intel_plane->crtc_x = crtc_x;
	intel_plane->crtc_y = crtc_y;
	intel_plane->crtc_w = crtc_w;
	intel_plane->crtc_h = crtc_h;
	intel_plane->src_x = src_x;
	intel_plane->src_y = src_y;
	intel_plane->src_w = src_w;
	intel_plane->src_h = src_h;

	src_w = src_w >> 16;
	src_h = src_h >> 16;

	/* Pipe must be running... */
	if (!(I915_READ(PIPECONF(cpu_transcoder)) & PIPECONF_ENABLE))
		return -EINVAL;

	if (crtc_x >= primary_w || crtc_y >= primary_h)
		return -EINVAL;

	/* Don't modify another pipe's plane */
	if (intel_plane->pipe != intel_crtc->pipe)
		return -EINVAL;

	/* Sprite planes can be linear or x-tiled surfaces */
	switch (obj->tiling_mode) {
		case I915_TILING_NONE:
		case I915_TILING_X:
			break;
		default:
			return -EINVAL;
	}

	/*
	 * Clamp the width & height into the visible area.  Note we don't
	 * try to scale the source if part of the visible region is offscreen.
	 * The caller must handle that by adjusting source offset and size.
	 */
	if ((crtc_x < 0) && ((crtc_x + crtc_w) > 0)) {
		crtc_w += crtc_x;
		crtc_x = 0;
	}
	if ((crtc_x + crtc_w) <= 0) /* Nothing to display */
		goto out;
	if ((crtc_x + crtc_w) > primary_w)
		crtc_w = primary_w - crtc_x;

	if ((crtc_y < 0) && ((crtc_y + crtc_h) > 0)) {
		crtc_h += crtc_y;
		crtc_y = 0;
	}
	if ((crtc_y + crtc_h) <= 0) /* Nothing to display */
		goto out;
	if (crtc_y + crtc_h > primary_h)
		crtc_h = primary_h - crtc_y;

	if (!crtc_w || !crtc_h) /* Again, nothing to display */
		goto out;

	/*
	 * We may not have a scaler, eg. HSW does not have it any more
	 */
	if (!intel_plane->can_scale && (crtc_w != src_w || crtc_h != src_h))
		return -EINVAL;

	/*
	 * We can take a larger source and scale it down, but
	 * only so much...  16x is the max on SNB.
	 */
	if (((src_w * src_h) / (crtc_w * crtc_h)) > intel_plane->max_downscale)
		return -EINVAL;

	/*
	 * If the sprite is completely covering the primary plane,
	 * we can disable the primary and save power.
	 */
	if ((crtc_x == 0) && (crtc_y == 0) &&
	    (crtc_w == primary_w) && (crtc_h == primary_h))
		disable_primary = true;

	mutex_lock(&dev->struct_mutex);

	/* Note that this will apply the VT-d workaround for scanouts,
	 * which is more restrictive than required for sprites. (The
	 * primary plane requires 256KiB alignment with 64 PTE padding,
	 * the sprite planes only require 128KiB alignment and 32 PTE padding.
	 */
	ret = intel_pin_and_fence_fb_obj(dev, obj, NULL);
	if (ret)
		goto out_unlock;

	intel_plane->obj = obj;

	/*
	 * Be sure to re-enable the primary before the sprite is no longer
	 * covering it fully.
	 */
	if (!disable_primary)
		intel_enable_primary(crtc);

	intel_plane->update_plane(plane, fb, obj, crtc_x, crtc_y,
				  crtc_w, crtc_h, x, y, src_w, src_h);

	if (disable_primary)
		intel_disable_primary(crtc);

	/* Unpin old obj after new one is active to avoid ugliness */
	if (old_obj) {
		/*
		 * It's fairly common to simply update the position of
		 * an existing object.  In that case, we don't need to
		 * wait for vblank to avoid ugliness, we only need to
		 * do the pin & ref bookkeeping.
		 */
		if (old_obj != obj) {
			mutex_unlock(&dev->struct_mutex);
			intel_wait_for_vblank(dev, to_intel_crtc(crtc)->pipe);
			mutex_lock(&dev->struct_mutex);
		}
		intel_unpin_fb_obj(old_obj);
	}

out_unlock:
	mutex_unlock(&dev->struct_mutex);
out:
	return ret;
}

static int
intel_disable_plane(struct drm_plane *plane)
{
	struct drm_device *dev = plane->dev;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	int ret = 0;

	if (plane->crtc)
		intel_enable_primary(plane->crtc);
	intel_plane->disable_plane(plane);

	if (!intel_plane->obj)
		goto out;

	intel_wait_for_vblank(dev, intel_plane->pipe);

	mutex_lock(&dev->struct_mutex);
	intel_unpin_fb_obj(intel_plane->obj);
	intel_plane->obj = NULL;
	mutex_unlock(&dev->struct_mutex);
out:

	return ret;
}

static void intel_destroy_plane(struct drm_plane *plane)
{
	struct intel_plane *intel_plane = to_intel_plane(plane);
	intel_disable_plane(plane);
	drm_plane_cleanup(plane);
	kfree(intel_plane);
}

int intel_sprite_set_colorkey(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_intel_sprite_colorkey *set = data;
	struct drm_mode_object *obj;
	struct drm_plane *plane;
	struct intel_plane *intel_plane;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	/* Make sure we don't try to enable both src & dest simultaneously */
	if ((set->flags & (I915_SET_COLORKEY_DESTINATION | I915_SET_COLORKEY_SOURCE)) == (I915_SET_COLORKEY_DESTINATION | I915_SET_COLORKEY_SOURCE))
		return -EINVAL;

	drm_modeset_lock_all(dev);

	obj = drm_mode_object_find(dev, set->plane_id, DRM_MODE_OBJECT_PLANE);
	if (!obj) {
		ret = -EINVAL;
		goto out_unlock;
	}

	plane = obj_to_plane(obj);
	intel_plane = to_intel_plane(plane);
	ret = intel_plane->update_colorkey(plane, set);

out_unlock:
	drm_modeset_unlock_all(dev);
	return ret;
}

int intel_sprite_get_colorkey(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_intel_sprite_colorkey *get = data;
	struct drm_mode_object *obj;
	struct drm_plane *plane;
	struct intel_plane *intel_plane;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	drm_modeset_lock_all(dev);

	obj = drm_mode_object_find(dev, get->plane_id, DRM_MODE_OBJECT_PLANE);
	if (!obj) {
		ret = -EINVAL;
		goto out_unlock;
	}

	plane = obj_to_plane(obj);
	intel_plane = to_intel_plane(plane);
	intel_plane->get_colorkey(plane, get);

out_unlock:
	drm_modeset_unlock_all(dev);
	return ret;
}

void intel_plane_restore(struct drm_plane *plane)
{
	struct intel_plane *intel_plane = to_intel_plane(plane);

	if (!plane->crtc || !plane->fb)
		return;

	intel_update_plane(plane, plane->crtc, plane->fb,
			   intel_plane->crtc_x, intel_plane->crtc_y,
			   intel_plane->crtc_w, intel_plane->crtc_h,
			   intel_plane->src_x, intel_plane->src_y,
			   intel_plane->src_w, intel_plane->src_h);
}

static const struct drm_plane_funcs intel_plane_funcs = {
	.update_plane = intel_update_plane,
	.disable_plane = intel_disable_plane,
	.destroy = intel_destroy_plane,
};

static uint32_t ilk_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
};

static uint32_t snb_plane_formats[] = {
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
};

static uint32_t vlv_plane_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
};

int
intel_plane_init(struct drm_device *dev, enum pipe pipe, int plane)
{
	struct intel_plane *intel_plane;
	unsigned long possible_crtcs;
	const uint32_t *plane_formats;
	int num_plane_formats;
	int ret;

	if (INTEL_INFO(dev)->gen < 5)
		return -ENODEV;

	intel_plane = kzalloc(sizeof(struct intel_plane), GFP_KERNEL);
	if (!intel_plane)
		return -ENOMEM;

	switch (INTEL_INFO(dev)->gen) {
	case 5:
	case 6:
		intel_plane->can_scale = true;
		intel_plane->max_downscale = 16;
		intel_plane->update_plane = ilk_update_plane;
		intel_plane->disable_plane = ilk_disable_plane;
		intel_plane->update_colorkey = ilk_update_colorkey;
		intel_plane->get_colorkey = ilk_get_colorkey;

		if (IS_GEN6(dev)) {
			plane_formats = snb_plane_formats;
			num_plane_formats = ARRAY_SIZE(snb_plane_formats);
		} else {
			plane_formats = ilk_plane_formats;
			num_plane_formats = ARRAY_SIZE(ilk_plane_formats);
		}
		break;

	case 7:
		if (IS_IVYBRIDGE(dev)) {
			intel_plane->can_scale = true;
			intel_plane->max_downscale = 2;
		} else {
			intel_plane->can_scale = false;
			intel_plane->max_downscale = 1;
		}

		if (IS_VALLEYVIEW(dev)) {
			intel_plane->update_plane = vlv_update_plane;
			intel_plane->disable_plane = vlv_disable_plane;
			intel_plane->update_colorkey = vlv_update_colorkey;
			intel_plane->get_colorkey = vlv_get_colorkey;

			plane_formats = vlv_plane_formats;
			num_plane_formats = ARRAY_SIZE(vlv_plane_formats);
		} else {
			intel_plane->update_plane = ivb_update_plane;
			intel_plane->disable_plane = ivb_disable_plane;
			intel_plane->update_colorkey = ivb_update_colorkey;
			intel_plane->get_colorkey = ivb_get_colorkey;

			plane_formats = snb_plane_formats;
			num_plane_formats = ARRAY_SIZE(snb_plane_formats);
		}
		break;

	default:
		kfree(intel_plane);
		return -ENODEV;
	}

	intel_plane->pipe = pipe;
	intel_plane->plane = plane;
	possible_crtcs = (1 << pipe);
	ret = drm_plane_init(dev, &intel_plane->base, possible_crtcs,
			     &intel_plane_funcs,
			     plane_formats, num_plane_formats,
			     false);
	if (ret)
		kfree(intel_plane);

	return ret;
}
