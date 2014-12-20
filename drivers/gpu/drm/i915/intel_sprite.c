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
#include <drm/drm_rect.h>
#include "intel_drv.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"

static bool
format_is_yuv(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_YVYU:
		return true;
	default:
		return false;
	}
}

static int usecs_to_scanlines(const struct drm_display_mode *mode, int usecs)
{
	/* paranoia */
	if (!mode->crtc_htotal)
		return 1;

	return DIV_ROUND_UP(usecs * mode->crtc_clock, 1000 * mode->crtc_htotal);
}

/**
 * intel_pipe_update_start() - start update of a set of display registers
 * @crtc: the crtc of which the registers are going to be updated
 * @start_vbl_count: vblank counter return pointer used for error checking
 *
 * Mark the start of an update to pipe registers that should be updated
 * atomically regarding vblank. If the next vblank will happens within
 * the next 100 us, this function waits until the vblank passes.
 *
 * After a successful call to this function, interrupts will be disabled
 * until a subsequent call to intel_pipe_update_end(). That is done to
 * avoid random delays. The value written to @start_vbl_count should be
 * supplied to intel_pipe_update_end() for error checking.
 *
 * Return: true if the call was successful
 */
bool intel_pipe_update_start(struct intel_crtc *crtc, uint32_t *start_vbl_count)
{
	struct drm_device *dev = crtc->base.dev;
	const struct drm_display_mode *mode = &crtc->config.adjusted_mode;
	enum pipe pipe = crtc->pipe;
	long timeout = msecs_to_jiffies_timeout(1);
	int scanline, min, max, vblank_start;
	wait_queue_head_t *wq = drm_crtc_vblank_waitqueue(&crtc->base);
	DEFINE_WAIT(wait);

	vblank_start = mode->crtc_vblank_start;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vblank_start = DIV_ROUND_UP(vblank_start, 2);

	/* FIXME needs to be calibrated sensibly */
	min = vblank_start - usecs_to_scanlines(mode, 100);
	max = vblank_start - 1;

	if (min <= 0 || max <= 0)
		return false;

	if (WARN_ON(drm_vblank_get(dev, pipe)))
		return false;

	local_irq_disable();

	trace_i915_pipe_update_start(crtc, min, max);

	for (;;) {
		/*
		 * prepare_to_wait() has a memory barrier, which guarantees
		 * other CPUs can see the task state update by the time we
		 * read the scanline.
		 */
		prepare_to_wait(wq, &wait, TASK_UNINTERRUPTIBLE);

		scanline = intel_get_crtc_scanline(crtc);
		if (scanline < min || scanline > max)
			break;

		if (timeout <= 0) {
			DRM_ERROR("Potential atomic update failure on pipe %c\n",
				  pipe_name(crtc->pipe));
			break;
		}

		local_irq_enable();

		timeout = schedule_timeout(timeout);

		local_irq_disable();
	}

	finish_wait(wq, &wait);

	drm_vblank_put(dev, pipe);

	*start_vbl_count = dev->driver->get_vblank_counter(dev, pipe);

	trace_i915_pipe_update_vblank_evaded(crtc, min, max, *start_vbl_count);

	return true;
}

/**
 * intel_pipe_update_end() - end update of a set of display registers
 * @crtc: the crtc of which the registers were updated
 * @start_vbl_count: start vblank counter (used for error checking)
 *
 * Mark the end of an update started with intel_pipe_update_start(). This
 * re-enables interrupts and verifies the update was actually completed
 * before a vblank using the value of @start_vbl_count.
 */
void intel_pipe_update_end(struct intel_crtc *crtc, u32 start_vbl_count)
{
	struct drm_device *dev = crtc->base.dev;
	enum pipe pipe = crtc->pipe;
	u32 end_vbl_count = dev->driver->get_vblank_counter(dev, pipe);

	trace_i915_pipe_update_end(crtc, end_vbl_count);

	local_irq_enable();

	if (start_vbl_count != end_vbl_count)
		DRM_ERROR("Atomic update failure on pipe %c (start=%u end=%u)\n",
			  pipe_name(pipe), start_vbl_count, end_vbl_count);
}

static void intel_update_primary_plane(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = crtc->base.dev->dev_private;
	int reg = DSPCNTR(crtc->plane);

	if (crtc->primary_enabled)
		I915_WRITE(reg, I915_READ(reg) | DISPLAY_PLANE_ENABLE);
	else
		I915_WRITE(reg, I915_READ(reg) & ~DISPLAY_PLANE_ENABLE);
}

static void
skl_update_plane(struct drm_plane *drm_plane, struct drm_crtc *crtc,
		 struct drm_framebuffer *fb,
		 struct drm_i915_gem_object *obj, int crtc_x, int crtc_y,
		 unsigned int crtc_w, unsigned int crtc_h,
		 uint32_t x, uint32_t y,
		 uint32_t src_w, uint32_t src_h)
{
	struct drm_device *dev = drm_plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(drm_plane);
	const int pipe = intel_plane->pipe;
	const int plane = intel_plane->plane + 1;
	u32 plane_ctl, stride;
	int pixel_size = drm_format_plane_cpp(fb->pixel_format, 0);

	plane_ctl = I915_READ(PLANE_CTL(pipe, plane));

	/* Mask out pixel format bits in case we change it */
	plane_ctl &= ~PLANE_CTL_FORMAT_MASK;
	plane_ctl &= ~PLANE_CTL_ORDER_RGBX;
	plane_ctl &= ~PLANE_CTL_YUV422_ORDER_MASK;
	plane_ctl &= ~PLANE_CTL_TILED_MASK;
	plane_ctl &= ~PLANE_CTL_ALPHA_MASK;
	plane_ctl &= ~PLANE_CTL_ROTATE_MASK;

	/* Trickle feed has to be enabled */
	plane_ctl &= ~PLANE_CTL_TRICKLE_FEED_DISABLE;

	switch (fb->pixel_format) {
	case DRM_FORMAT_RGB565:
		plane_ctl |= PLANE_CTL_FORMAT_RGB_565;
		break;
	case DRM_FORMAT_XBGR8888:
		plane_ctl |= PLANE_CTL_FORMAT_XRGB_8888 | PLANE_CTL_ORDER_RGBX;
		break;
	case DRM_FORMAT_XRGB8888:
		plane_ctl |= PLANE_CTL_FORMAT_XRGB_8888;
		break;
	/*
	 * XXX: For ARBG/ABGR formats we default to expecting scanout buffers
	 * to be already pre-multiplied. We need to add a knob (or a different
	 * DRM_FORMAT) for user-space to configure that.
	 */
	case DRM_FORMAT_ABGR8888:
		plane_ctl |= PLANE_CTL_FORMAT_XRGB_8888 |
			     PLANE_CTL_ORDER_RGBX |
			     PLANE_CTL_ALPHA_SW_PREMULTIPLY;
		break;
	case DRM_FORMAT_ARGB8888:
		plane_ctl |= PLANE_CTL_FORMAT_XRGB_8888 |
			     PLANE_CTL_ALPHA_SW_PREMULTIPLY;
		break;
	case DRM_FORMAT_YUYV:
		plane_ctl |= PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_YUYV;
		break;
	case DRM_FORMAT_YVYU:
		plane_ctl |= PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_YVYU;
		break;
	case DRM_FORMAT_UYVY:
		plane_ctl |= PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_UYVY;
		break;
	case DRM_FORMAT_VYUY:
		plane_ctl |= PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_VYUY;
		break;
	default:
		BUG();
	}

	switch (obj->tiling_mode) {
	case I915_TILING_NONE:
		stride = fb->pitches[0] >> 6;
		break;
	case I915_TILING_X:
		plane_ctl |= PLANE_CTL_TILED_X;
		stride = fb->pitches[0] >> 9;
		break;
	default:
		BUG();
	}
	if (intel_plane->rotation == BIT(DRM_ROTATE_180))
		plane_ctl |= PLANE_CTL_ROTATE_180;

	plane_ctl |= PLANE_CTL_ENABLE;
	plane_ctl |= PLANE_CTL_PIPE_CSC_ENABLE;

	intel_update_sprite_watermarks(drm_plane, crtc, src_w, src_h,
				       pixel_size, true,
				       src_w != crtc_w || src_h != crtc_h);

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	crtc_w--;
	crtc_h--;

	I915_WRITE(PLANE_OFFSET(pipe, plane), (y << 16) | x);
	I915_WRITE(PLANE_STRIDE(pipe, plane), stride);
	I915_WRITE(PLANE_POS(pipe, plane), (crtc_y << 16) | crtc_x);
	I915_WRITE(PLANE_SIZE(pipe, plane), (crtc_h << 16) | crtc_w);
	I915_WRITE(PLANE_CTL(pipe, plane), plane_ctl);
	I915_WRITE(PLANE_SURF(pipe, plane), i915_gem_obj_ggtt_offset(obj));
	POSTING_READ(PLANE_SURF(pipe, plane));
}

static void
skl_disable_plane(struct drm_plane *drm_plane, struct drm_crtc *crtc)
{
	struct drm_device *dev = drm_plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(drm_plane);
	const int pipe = intel_plane->pipe;
	const int plane = intel_plane->plane + 1;

	I915_WRITE(PLANE_CTL(pipe, plane),
		   I915_READ(PLANE_CTL(pipe, plane)) & ~PLANE_CTL_ENABLE);

	/* Activate double buffered register update */
	I915_WRITE(PLANE_CTL(pipe, plane), 0);
	POSTING_READ(PLANE_CTL(pipe, plane));

	intel_update_sprite_watermarks(drm_plane, crtc, 0, 0, 0, false, false);
}

static int
skl_update_colorkey(struct drm_plane *drm_plane,
		    struct drm_intel_sprite_colorkey *key)
{
	struct drm_device *dev = drm_plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(drm_plane);
	const int pipe = intel_plane->pipe;
	const int plane = intel_plane->plane;
	u32 plane_ctl;

	I915_WRITE(PLANE_KEYVAL(pipe, plane), key->min_value);
	I915_WRITE(PLANE_KEYMAX(pipe, plane), key->max_value);
	I915_WRITE(PLANE_KEYMSK(pipe, plane), key->channel_mask);

	plane_ctl = I915_READ(PLANE_CTL(pipe, plane));
	plane_ctl &= ~PLANE_CTL_KEY_ENABLE_MASK;
	if (key->flags & I915_SET_COLORKEY_DESTINATION)
		plane_ctl |= PLANE_CTL_KEY_ENABLE_DESTINATION;
	else if (key->flags & I915_SET_COLORKEY_SOURCE)
		plane_ctl |= PLANE_CTL_KEY_ENABLE_SOURCE;
	I915_WRITE(PLANE_CTL(pipe, plane), plane_ctl);

	POSTING_READ(PLANE_CTL(pipe, plane));

	return 0;
}

static void
skl_get_colorkey(struct drm_plane *drm_plane,
		 struct drm_intel_sprite_colorkey *key)
{
	struct drm_device *dev = drm_plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(drm_plane);
	const int pipe = intel_plane->pipe;
	const int plane = intel_plane->plane;
	u32 plane_ctl;

	key->min_value = I915_READ(PLANE_KEYVAL(pipe, plane));
	key->max_value = I915_READ(PLANE_KEYMAX(pipe, plane));
	key->channel_mask = I915_READ(PLANE_KEYMSK(pipe, plane));

	plane_ctl = I915_READ(PLANE_CTL(pipe, plane));

	switch (plane_ctl & PLANE_CTL_KEY_ENABLE_MASK) {
	case PLANE_CTL_KEY_ENABLE_DESTINATION:
		key->flags = I915_SET_COLORKEY_DESTINATION;
		break;
	case PLANE_CTL_KEY_ENABLE_SOURCE:
		key->flags = I915_SET_COLORKEY_SOURCE;
		break;
	default:
		key->flags = I915_SET_COLORKEY_NONE;
	}
}

static void
chv_update_csc(struct intel_plane *intel_plane, uint32_t format)
{
	struct drm_i915_private *dev_priv = intel_plane->base.dev->dev_private;
	int plane = intel_plane->plane;

	/* Seems RGB data bypasses the CSC always */
	if (!format_is_yuv(format))
		return;

	/*
	 * BT.601 limited range YCbCr -> full range RGB
	 *
	 * |r|   | 6537 4769     0|   |cr  |
	 * |g| = |-3330 4769 -1605| x |y-64|
	 * |b|   |    0 4769  8263|   |cb  |
	 *
	 * Cb and Cr apparently come in as signed already, so no
	 * need for any offset. For Y we need to remove the offset.
	 */
	I915_WRITE(SPCSCYGOFF(plane), SPCSC_OOFF(0) | SPCSC_IOFF(-64));
	I915_WRITE(SPCSCCBOFF(plane), SPCSC_OOFF(0) | SPCSC_IOFF(0));
	I915_WRITE(SPCSCCROFF(plane), SPCSC_OOFF(0) | SPCSC_IOFF(0));

	I915_WRITE(SPCSCC01(plane), SPCSC_C1(4769) | SPCSC_C0(6537));
	I915_WRITE(SPCSCC23(plane), SPCSC_C1(-3330) | SPCSC_C0(0));
	I915_WRITE(SPCSCC45(plane), SPCSC_C1(-1605) | SPCSC_C0(4769));
	I915_WRITE(SPCSCC67(plane), SPCSC_C1(4769) | SPCSC_C0(0));
	I915_WRITE(SPCSCC8(plane), SPCSC_C0(8263));

	I915_WRITE(SPCSCYGICLAMP(plane), SPCSC_IMAX(940) | SPCSC_IMIN(64));
	I915_WRITE(SPCSCCBICLAMP(plane), SPCSC_IMAX(448) | SPCSC_IMIN(-448));
	I915_WRITE(SPCSCCRICLAMP(plane), SPCSC_IMAX(448) | SPCSC_IMIN(-448));

	I915_WRITE(SPCSCYGOCLAMP(plane), SPCSC_OMAX(1023) | SPCSC_OMIN(0));
	I915_WRITE(SPCSCCBOCLAMP(plane), SPCSC_OMAX(1023) | SPCSC_OMIN(0));
	I915_WRITE(SPCSCCROCLAMP(plane), SPCSC_OMAX(1023) | SPCSC_OMIN(0));
}

static void
vlv_update_plane(struct drm_plane *dplane, struct drm_crtc *crtc,
		 struct drm_framebuffer *fb,
		 struct drm_i915_gem_object *obj, int crtc_x, int crtc_y,
		 unsigned int crtc_w, unsigned int crtc_h,
		 uint32_t x, uint32_t y,
		 uint32_t src_w, uint32_t src_h)
{
	struct drm_device *dev = dplane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(dplane);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_plane->pipe;
	int plane = intel_plane->plane;
	u32 sprctl;
	unsigned long sprsurf_offset, linear_offset;
	int pixel_size = drm_format_plane_cpp(fb->pixel_format, 0);
	u32 start_vbl_count;
	bool atomic_update;

	sprctl = I915_READ(SPCNTR(pipe, plane));

	/* Mask out pixel format bits in case we change it */
	sprctl &= ~SP_PIXFORMAT_MASK;
	sprctl &= ~SP_YUV_BYTE_ORDER_MASK;
	sprctl &= ~SP_TILED;
	sprctl &= ~SP_ROTATE_180;

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

	/*
	 * Enable gamma to match primary/cursor plane behaviour.
	 * FIXME should be user controllable via propertiesa.
	 */
	sprctl |= SP_GAMMA_ENABLE;

	if (obj->tiling_mode != I915_TILING_NONE)
		sprctl |= SP_TILED;

	sprctl |= SP_ENABLE;

	intel_update_sprite_watermarks(dplane, crtc, src_w, src_h,
				       pixel_size, true,
				       src_w != crtc_w || src_h != crtc_h);

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	crtc_w--;
	crtc_h--;

	linear_offset = y * fb->pitches[0] + x * pixel_size;
	sprsurf_offset = intel_gen4_compute_page_offset(&x, &y,
							obj->tiling_mode,
							pixel_size,
							fb->pitches[0]);
	linear_offset -= sprsurf_offset;

	if (intel_plane->rotation == BIT(DRM_ROTATE_180)) {
		sprctl |= SP_ROTATE_180;

		x += src_w;
		y += src_h;
		linear_offset += src_h * fb->pitches[0] + src_w * pixel_size;
	}

	atomic_update = intel_pipe_update_start(intel_crtc, &start_vbl_count);

	intel_update_primary_plane(intel_crtc);

	if (IS_CHERRYVIEW(dev) && pipe == PIPE_B)
		chv_update_csc(intel_plane, fb->pixel_format);

	I915_WRITE(SPSTRIDE(pipe, plane), fb->pitches[0]);
	I915_WRITE(SPPOS(pipe, plane), (crtc_y << 16) | crtc_x);

	if (obj->tiling_mode != I915_TILING_NONE)
		I915_WRITE(SPTILEOFF(pipe, plane), (y << 16) | x);
	else
		I915_WRITE(SPLINOFF(pipe, plane), linear_offset);

	I915_WRITE(SPCONSTALPHA(pipe, plane), 0);

	I915_WRITE(SPSIZE(pipe, plane), (crtc_h << 16) | crtc_w);
	I915_WRITE(SPCNTR(pipe, plane), sprctl);
	I915_WRITE(SPSURF(pipe, plane), i915_gem_obj_ggtt_offset(obj) +
		   sprsurf_offset);

	intel_flush_primary_plane(dev_priv, intel_crtc->plane);

	if (atomic_update)
		intel_pipe_update_end(intel_crtc, start_vbl_count);
}

static void
vlv_disable_plane(struct drm_plane *dplane, struct drm_crtc *crtc)
{
	struct drm_device *dev = dplane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(dplane);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_plane->pipe;
	int plane = intel_plane->plane;
	u32 start_vbl_count;
	bool atomic_update;

	atomic_update = intel_pipe_update_start(intel_crtc, &start_vbl_count);

	intel_update_primary_plane(intel_crtc);

	I915_WRITE(SPCNTR(pipe, plane), I915_READ(SPCNTR(pipe, plane)) &
		   ~SP_ENABLE);
	/* Activate double buffered register update */
	I915_WRITE(SPSURF(pipe, plane), 0);

	intel_flush_primary_plane(dev_priv, intel_crtc->plane);

	if (atomic_update)
		intel_pipe_update_end(intel_crtc, start_vbl_count);

	intel_update_sprite_watermarks(dplane, crtc, 0, 0, 0, false, false);
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
ivb_update_plane(struct drm_plane *plane, struct drm_crtc *crtc,
		 struct drm_framebuffer *fb,
		 struct drm_i915_gem_object *obj, int crtc_x, int crtc_y,
		 unsigned int crtc_w, unsigned int crtc_h,
		 uint32_t x, uint32_t y,
		 uint32_t src_w, uint32_t src_h)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_plane->pipe;
	u32 sprctl, sprscale = 0;
	unsigned long sprsurf_offset, linear_offset;
	int pixel_size = drm_format_plane_cpp(fb->pixel_format, 0);
	u32 start_vbl_count;
	bool atomic_update;

	sprctl = I915_READ(SPRCTL(pipe));

	/* Mask out pixel format bits in case we change it */
	sprctl &= ~SPRITE_PIXFORMAT_MASK;
	sprctl &= ~SPRITE_RGB_ORDER_RGBX;
	sprctl &= ~SPRITE_YUV_BYTE_ORDER_MASK;
	sprctl &= ~SPRITE_TILED;
	sprctl &= ~SPRITE_ROTATE_180;

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

	/*
	 * Enable gamma to match primary/cursor plane behaviour.
	 * FIXME should be user controllable via propertiesa.
	 */
	sprctl |= SPRITE_GAMMA_ENABLE;

	if (obj->tiling_mode != I915_TILING_NONE)
		sprctl |= SPRITE_TILED;

	if (IS_HASWELL(dev) || IS_BROADWELL(dev))
		sprctl &= ~SPRITE_TRICKLE_FEED_DISABLE;
	else
		sprctl |= SPRITE_TRICKLE_FEED_DISABLE;

	sprctl |= SPRITE_ENABLE;

	if (IS_HASWELL(dev) || IS_BROADWELL(dev))
		sprctl |= SPRITE_PIPE_CSC_ENABLE;

	intel_update_sprite_watermarks(plane, crtc, src_w, src_h, pixel_size,
				       true,
				       src_w != crtc_w || src_h != crtc_h);

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	crtc_w--;
	crtc_h--;

	if (crtc_w != src_w || crtc_h != src_h)
		sprscale = SPRITE_SCALE_ENABLE | (src_w << 16) | src_h;

	linear_offset = y * fb->pitches[0] + x * pixel_size;
	sprsurf_offset =
		intel_gen4_compute_page_offset(&x, &y, obj->tiling_mode,
					       pixel_size, fb->pitches[0]);
	linear_offset -= sprsurf_offset;

	if (intel_plane->rotation == BIT(DRM_ROTATE_180)) {
		sprctl |= SPRITE_ROTATE_180;

		/* HSW and BDW does this automagically in hardware */
		if (!IS_HASWELL(dev) && !IS_BROADWELL(dev)) {
			x += src_w;
			y += src_h;
			linear_offset += src_h * fb->pitches[0] +
				src_w * pixel_size;
		}
	}

	atomic_update = intel_pipe_update_start(intel_crtc, &start_vbl_count);

	intel_update_primary_plane(intel_crtc);

	I915_WRITE(SPRSTRIDE(pipe), fb->pitches[0]);
	I915_WRITE(SPRPOS(pipe), (crtc_y << 16) | crtc_x);

	/* HSW consolidates SPRTILEOFF and SPRLINOFF into a single SPROFFSET
	 * register */
	if (IS_HASWELL(dev) || IS_BROADWELL(dev))
		I915_WRITE(SPROFFSET(pipe), (y << 16) | x);
	else if (obj->tiling_mode != I915_TILING_NONE)
		I915_WRITE(SPRTILEOFF(pipe), (y << 16) | x);
	else
		I915_WRITE(SPRLINOFF(pipe), linear_offset);

	I915_WRITE(SPRSIZE(pipe), (crtc_h << 16) | crtc_w);
	if (intel_plane->can_scale)
		I915_WRITE(SPRSCALE(pipe), sprscale);
	I915_WRITE(SPRCTL(pipe), sprctl);
	I915_WRITE(SPRSURF(pipe),
		   i915_gem_obj_ggtt_offset(obj) + sprsurf_offset);

	intel_flush_primary_plane(dev_priv, intel_crtc->plane);

	if (atomic_update)
		intel_pipe_update_end(intel_crtc, start_vbl_count);
}

static void
ivb_disable_plane(struct drm_plane *plane, struct drm_crtc *crtc)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_plane->pipe;
	u32 start_vbl_count;
	bool atomic_update;

	atomic_update = intel_pipe_update_start(intel_crtc, &start_vbl_count);

	intel_update_primary_plane(intel_crtc);

	I915_WRITE(SPRCTL(pipe), I915_READ(SPRCTL(pipe)) & ~SPRITE_ENABLE);
	/* Can't leave the scaler enabled... */
	if (intel_plane->can_scale)
		I915_WRITE(SPRSCALE(pipe), 0);
	/* Activate double buffered register update */
	I915_WRITE(SPRSURF(pipe), 0);

	intel_flush_primary_plane(dev_priv, intel_crtc->plane);

	if (atomic_update)
		intel_pipe_update_end(intel_crtc, start_vbl_count);

	/*
	 * Avoid underruns when disabling the sprite.
	 * FIXME remove once watermark updates are done properly.
	 */
	intel_wait_for_vblank(dev, pipe);

	intel_update_sprite_watermarks(plane, crtc, 0, 0, 0, false, false);
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
ilk_update_plane(struct drm_plane *plane, struct drm_crtc *crtc,
		 struct drm_framebuffer *fb,
		 struct drm_i915_gem_object *obj, int crtc_x, int crtc_y,
		 unsigned int crtc_w, unsigned int crtc_h,
		 uint32_t x, uint32_t y,
		 uint32_t src_w, uint32_t src_h)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_plane->pipe;
	unsigned long dvssurf_offset, linear_offset;
	u32 dvscntr, dvsscale;
	int pixel_size = drm_format_plane_cpp(fb->pixel_format, 0);
	u32 start_vbl_count;
	bool atomic_update;

	dvscntr = I915_READ(DVSCNTR(pipe));

	/* Mask out pixel format bits in case we change it */
	dvscntr &= ~DVS_PIXFORMAT_MASK;
	dvscntr &= ~DVS_RGB_ORDER_XBGR;
	dvscntr &= ~DVS_YUV_BYTE_ORDER_MASK;
	dvscntr &= ~DVS_TILED;
	dvscntr &= ~DVS_ROTATE_180;

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

	/*
	 * Enable gamma to match primary/cursor plane behaviour.
	 * FIXME should be user controllable via propertiesa.
	 */
	dvscntr |= DVS_GAMMA_ENABLE;

	if (obj->tiling_mode != I915_TILING_NONE)
		dvscntr |= DVS_TILED;

	if (IS_GEN6(dev))
		dvscntr |= DVS_TRICKLE_FEED_DISABLE; /* must disable */
	dvscntr |= DVS_ENABLE;

	intel_update_sprite_watermarks(plane, crtc, src_w, src_h,
				       pixel_size, true,
				       src_w != crtc_w || src_h != crtc_h);

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	crtc_w--;
	crtc_h--;

	dvsscale = 0;
	if (crtc_w != src_w || crtc_h != src_h)
		dvsscale = DVS_SCALE_ENABLE | (src_w << 16) | src_h;

	linear_offset = y * fb->pitches[0] + x * pixel_size;
	dvssurf_offset =
		intel_gen4_compute_page_offset(&x, &y, obj->tiling_mode,
					       pixel_size, fb->pitches[0]);
	linear_offset -= dvssurf_offset;

	if (intel_plane->rotation == BIT(DRM_ROTATE_180)) {
		dvscntr |= DVS_ROTATE_180;

		x += src_w;
		y += src_h;
		linear_offset += src_h * fb->pitches[0] + src_w * pixel_size;
	}

	atomic_update = intel_pipe_update_start(intel_crtc, &start_vbl_count);

	intel_update_primary_plane(intel_crtc);

	I915_WRITE(DVSSTRIDE(pipe), fb->pitches[0]);
	I915_WRITE(DVSPOS(pipe), (crtc_y << 16) | crtc_x);

	if (obj->tiling_mode != I915_TILING_NONE)
		I915_WRITE(DVSTILEOFF(pipe), (y << 16) | x);
	else
		I915_WRITE(DVSLINOFF(pipe), linear_offset);

	I915_WRITE(DVSSIZE(pipe), (crtc_h << 16) | crtc_w);
	I915_WRITE(DVSSCALE(pipe), dvsscale);
	I915_WRITE(DVSCNTR(pipe), dvscntr);
	I915_WRITE(DVSSURF(pipe),
		   i915_gem_obj_ggtt_offset(obj) + dvssurf_offset);

	intel_flush_primary_plane(dev_priv, intel_crtc->plane);

	if (atomic_update)
		intel_pipe_update_end(intel_crtc, start_vbl_count);
}

static void
ilk_disable_plane(struct drm_plane *plane, struct drm_crtc *crtc)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int pipe = intel_plane->pipe;
	u32 start_vbl_count;
	bool atomic_update;

	atomic_update = intel_pipe_update_start(intel_crtc, &start_vbl_count);

	intel_update_primary_plane(intel_crtc);

	I915_WRITE(DVSCNTR(pipe), I915_READ(DVSCNTR(pipe)) & ~DVS_ENABLE);
	/* Disable the scaler */
	I915_WRITE(DVSSCALE(pipe), 0);
	/* Flush double buffered register updates */
	I915_WRITE(DVSSURF(pipe), 0);

	intel_flush_primary_plane(dev_priv, intel_crtc->plane);

	if (atomic_update)
		intel_pipe_update_end(intel_crtc, start_vbl_count);

	/*
	 * Avoid underruns when disabling the sprite.
	 * FIXME remove once watermark updates are done properly.
	 */
	intel_wait_for_vblank(dev, pipe);

	intel_update_sprite_watermarks(plane, crtc, 0, 0, 0, false, false);
}

static void
intel_post_enable_primary(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	/*
	 * BDW signals flip done immediately if the plane
	 * is disabled, even if the plane enable is already
	 * armed to occur at the next vblank :(
	 */
	if (IS_BROADWELL(dev))
		intel_wait_for_vblank(dev, intel_crtc->pipe);

	/*
	 * FIXME IPS should be fine as long as one plane is
	 * enabled, but in practice it seems to have problems
	 * when going from primary only to sprite only and vice
	 * versa.
	 */
	hsw_enable_ips(intel_crtc);

	mutex_lock(&dev->struct_mutex);
	intel_update_fbc(dev);
	mutex_unlock(&dev->struct_mutex);
}

static void
intel_pre_disable_primary(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	mutex_lock(&dev->struct_mutex);
	if (dev_priv->fbc.plane == intel_crtc->plane)
		intel_disable_fbc(dev);
	mutex_unlock(&dev->struct_mutex);

	/*
	 * FIXME IPS should be fine as long as one plane is
	 * enabled, but in practice it seems to have problems
	 * when going from primary only to sprite only and vice
	 * versa.
	 */
	hsw_disable_ips(intel_crtc);
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

static bool colorkey_enabled(struct intel_plane *intel_plane)
{
	struct drm_intel_sprite_colorkey key;

	intel_plane->get_colorkey(&intel_plane->base, &key);

	return key.flags != I915_SET_COLORKEY_NONE;
}

static int
intel_check_sprite_plane(struct drm_plane *plane,
			 struct intel_plane_state *state)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(state->crtc);
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct drm_framebuffer *fb = state->fb;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	int crtc_x, crtc_y;
	unsigned int crtc_w, crtc_h;
	uint32_t src_x, src_y, src_w, src_h;
	struct drm_rect *src = &state->src;
	struct drm_rect *dst = &state->dst;
	struct drm_rect *orig_src = &state->orig_src;
	const struct drm_rect *clip = &state->clip;
	int hscale, vscale;
	int max_scale, min_scale;
	int pixel_size = drm_format_plane_cpp(fb->pixel_format, 0);

	/* Don't modify another pipe's plane */
	if (intel_plane->pipe != intel_crtc->pipe) {
		DRM_DEBUG_KMS("Wrong plane <-> crtc mapping\n");
		return -EINVAL;
	}

	/* FIXME check all gen limits */
	if (fb->width < 3 || fb->height < 3 || fb->pitches[0] > 16384) {
		DRM_DEBUG_KMS("Unsuitable framebuffer for plane\n");
		return -EINVAL;
	}

	/* Sprite planes can be linear or x-tiled surfaces */
	switch (obj->tiling_mode) {
		case I915_TILING_NONE:
		case I915_TILING_X:
			break;
		default:
			DRM_DEBUG_KMS("Unsupported tiling mode\n");
			return -EINVAL;
	}

	/*
	 * FIXME the following code does a bunch of fuzzy adjustments to the
	 * coordinates and sizes. We probably need some way to decide whether
	 * more strict checking should be done instead.
	 */
	max_scale = intel_plane->max_downscale << 16;
	min_scale = intel_plane->can_scale ? 1 : (1 << 16);

	drm_rect_rotate(src, fb->width << 16, fb->height << 16,
			intel_plane->rotation);

	hscale = drm_rect_calc_hscale_relaxed(src, dst, min_scale, max_scale);
	BUG_ON(hscale < 0);

	vscale = drm_rect_calc_vscale_relaxed(src, dst, min_scale, max_scale);
	BUG_ON(vscale < 0);

	state->visible =  drm_rect_clip_scaled(src, dst, clip, hscale, vscale);

	crtc_x = dst->x1;
	crtc_y = dst->y1;
	crtc_w = drm_rect_width(dst);
	crtc_h = drm_rect_height(dst);

	if (state->visible) {
		/* check again in case clipping clamped the results */
		hscale = drm_rect_calc_hscale(src, dst, min_scale, max_scale);
		if (hscale < 0) {
			DRM_DEBUG_KMS("Horizontal scaling factor out of limits\n");
			drm_rect_debug_print(src, true);
			drm_rect_debug_print(dst, false);

			return hscale;
		}

		vscale = drm_rect_calc_vscale(src, dst, min_scale, max_scale);
		if (vscale < 0) {
			DRM_DEBUG_KMS("Vertical scaling factor out of limits\n");
			drm_rect_debug_print(src, true);
			drm_rect_debug_print(dst, false);

			return vscale;
		}

		/* Make the source viewport size an exact multiple of the scaling factors. */
		drm_rect_adjust_size(src,
				     drm_rect_width(dst) * hscale - drm_rect_width(src),
				     drm_rect_height(dst) * vscale - drm_rect_height(src));

		drm_rect_rotate_inv(src, fb->width << 16, fb->height << 16,
				    intel_plane->rotation);

		/* sanity check to make sure the src viewport wasn't enlarged */
		WARN_ON(src->x1 < (int) orig_src->x1 ||
			src->y1 < (int) orig_src->y1 ||
			src->x2 > (int) orig_src->x2 ||
			src->y2 > (int) orig_src->y2);

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

		if (format_is_yuv(fb->pixel_format)) {
			src_x &= ~1;
			src_w &= ~1;

			/*
			 * Must keep src and dst the
			 * same if we can't scale.
			 */
			if (!intel_plane->can_scale)
				crtc_w &= ~1;

			if (crtc_w == 0)
				state->visible = false;
		}
	}

	/* Check size restrictions when scaling */
	if (state->visible && (src_w != crtc_w || src_h != crtc_h)) {
		unsigned int width_bytes;

		WARN_ON(!intel_plane->can_scale);

		/* FIXME interlacing min height is 6 */

		if (crtc_w < 3 || crtc_h < 3)
			state->visible = false;

		if (src_w < 3 || src_h < 3)
			state->visible = false;

		width_bytes = ((src_x * pixel_size) & 63) +
					src_w * pixel_size;

		if (src_w > 2048 || src_h > 2048 ||
		    width_bytes > 4096 || fb->pitches[0] > 4096) {
			DRM_DEBUG_KMS("Source dimensions exceed hardware limits\n");
			return -EINVAL;
		}
	}

	if (state->visible) {
		src->x1 = src_x;
		src->x2 = src_x + src_w;
		src->y1 = src_y;
		src->y2 = src_y + src_h;
	}

	dst->x1 = crtc_x;
	dst->x2 = crtc_x + crtc_w;
	dst->y1 = crtc_y;
	dst->y2 = crtc_y + crtc_h;

	return 0;
}

static int
intel_prepare_sprite_plane(struct drm_plane *plane,
			   struct intel_plane_state *state)
{
	struct drm_device *dev = plane->dev;
	struct drm_crtc *crtc = state->crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_plane *intel_plane = to_intel_plane(plane);
	enum pipe pipe = intel_crtc->pipe;
	struct drm_framebuffer *fb = state->fb;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct drm_i915_gem_object *old_obj = intel_plane->obj;
	int ret;

	if (old_obj != obj) {
		mutex_lock(&dev->struct_mutex);

		/* Note that this will apply the VT-d workaround for scanouts,
		 * which is more restrictive than required for sprites. (The
		 * primary plane requires 256KiB alignment with 64 PTE padding,
		 * the sprite planes only require 128KiB alignment and 32 PTE
		 * padding.
		 */
		ret = intel_pin_and_fence_fb_obj(plane, fb, NULL);
		if (ret == 0)
			i915_gem_track_fb(old_obj, obj,
					  INTEL_FRONTBUFFER_SPRITE(pipe));
		mutex_unlock(&dev->struct_mutex);
		if (ret)
			return ret;
	}

	return 0;
}

static void
intel_commit_sprite_plane(struct drm_plane *plane,
			  struct intel_plane_state *state)
{
	struct drm_device *dev = plane->dev;
	struct drm_crtc *crtc = state->crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_plane *intel_plane = to_intel_plane(plane);
	enum pipe pipe = intel_crtc->pipe;
	struct drm_framebuffer *fb = state->fb;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct drm_i915_gem_object *old_obj = intel_plane->obj;
	int crtc_x, crtc_y;
	unsigned int crtc_w, crtc_h;
	uint32_t src_x, src_y, src_w, src_h;
	struct drm_rect *dst = &state->dst;
	const struct drm_rect *clip = &state->clip;
	bool primary_enabled;

	/*
	 * If the sprite is completely covering the primary plane,
	 * we can disable the primary and save power.
	 */
	primary_enabled = !drm_rect_equals(dst, clip) || colorkey_enabled(intel_plane);
	WARN_ON(!primary_enabled && !state->visible && intel_crtc->active);

	intel_plane->crtc_x = state->orig_dst.x1;
	intel_plane->crtc_y = state->orig_dst.y1;
	intel_plane->crtc_w = drm_rect_width(&state->orig_dst);
	intel_plane->crtc_h = drm_rect_height(&state->orig_dst);
	intel_plane->src_x = state->orig_src.x1;
	intel_plane->src_y = state->orig_src.y1;
	intel_plane->src_w = drm_rect_width(&state->orig_src);
	intel_plane->src_h = drm_rect_height(&state->orig_src);
	intel_plane->obj = obj;

	if (intel_crtc->active) {
		bool primary_was_enabled = intel_crtc->primary_enabled;

		intel_crtc->primary_enabled = primary_enabled;

		if (primary_was_enabled != primary_enabled)
			intel_crtc_wait_for_pending_flips(crtc);

		if (primary_was_enabled && !primary_enabled)
			intel_pre_disable_primary(crtc);

		if (state->visible) {
			crtc_x = state->dst.x1;
			crtc_y = state->dst.y1;
			crtc_w = drm_rect_width(&state->dst);
			crtc_h = drm_rect_height(&state->dst);
			src_x = state->src.x1;
			src_y = state->src.y1;
			src_w = drm_rect_width(&state->src);
			src_h = drm_rect_height(&state->src);
			intel_plane->update_plane(plane, crtc, fb, obj,
						  crtc_x, crtc_y, crtc_w, crtc_h,
						  src_x, src_y, src_w, src_h);
		} else {
			intel_plane->disable_plane(plane, crtc);
		}


		intel_frontbuffer_flip(dev, INTEL_FRONTBUFFER_SPRITE(pipe));

		if (!primary_was_enabled && primary_enabled)
			intel_post_enable_primary(crtc);
	}

	/* Unpin old obj after new one is active to avoid ugliness */
	if (old_obj && old_obj != obj) {

		/*
		 * It's fairly common to simply update the position of
		 * an existing object.  In that case, we don't need to
		 * wait for vblank to avoid ugliness, we only need to
		 * do the pin & ref bookkeeping.
		 */
		if (intel_crtc->active)
			intel_wait_for_vblank(dev, intel_crtc->pipe);

		mutex_lock(&dev->struct_mutex);
		intel_unpin_fb_obj(old_obj);
		mutex_unlock(&dev->struct_mutex);
	}
}

static int
intel_update_plane(struct drm_plane *plane, struct drm_crtc *crtc,
		   struct drm_framebuffer *fb, int crtc_x, int crtc_y,
		   unsigned int crtc_w, unsigned int crtc_h,
		   uint32_t src_x, uint32_t src_y,
		   uint32_t src_w, uint32_t src_h)
{
	struct intel_plane_state state;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int ret;

	state.crtc = crtc;
	state.fb = fb;

	/* sample coordinates in 16.16 fixed point */
	state.src.x1 = src_x;
	state.src.x2 = src_x + src_w;
	state.src.y1 = src_y;
	state.src.y2 = src_y + src_h;

	/* integer pixels */
	state.dst.x1 = crtc_x;
	state.dst.x2 = crtc_x + crtc_w;
	state.dst.y1 = crtc_y;
	state.dst.y2 = crtc_y + crtc_h;

	state.clip.x1 = 0;
	state.clip.y1 = 0;
	state.clip.x2 = intel_crtc->active ? intel_crtc->config.pipe_src_w : 0;
	state.clip.y2 = intel_crtc->active ? intel_crtc->config.pipe_src_h : 0;
	state.orig_src = state.src;
	state.orig_dst = state.dst;

	ret = intel_check_sprite_plane(plane, &state);
	if (ret)
		return ret;

	ret = intel_prepare_sprite_plane(plane, &state);
	if (ret)
		return ret;

	intel_commit_sprite_plane(plane, &state);
	return 0;
}

static int
intel_disable_plane(struct drm_plane *plane)
{
	struct drm_device *dev = plane->dev;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct intel_crtc *intel_crtc;
	enum pipe pipe;

	if (!plane->fb)
		return 0;

	if (WARN_ON(!plane->crtc))
		return -EINVAL;

	intel_crtc = to_intel_crtc(plane->crtc);
	pipe = intel_crtc->pipe;

	if (intel_crtc->active) {
		bool primary_was_enabled = intel_crtc->primary_enabled;

		intel_crtc->primary_enabled = true;

		intel_plane->disable_plane(plane, plane->crtc);

		if (!primary_was_enabled && intel_crtc->primary_enabled)
			intel_post_enable_primary(plane->crtc);
	}

	if (intel_plane->obj) {
		if (intel_crtc->active)
			intel_wait_for_vblank(dev, intel_plane->pipe);

		mutex_lock(&dev->struct_mutex);
		intel_unpin_fb_obj(intel_plane->obj);
		i915_gem_track_fb(intel_plane->obj, NULL,
				  INTEL_FRONTBUFFER_SPRITE(pipe));
		mutex_unlock(&dev->struct_mutex);

		intel_plane->obj = NULL;
	}

	return 0;
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
	struct drm_plane *plane;
	struct intel_plane *intel_plane;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	/* Make sure we don't try to enable both src & dest simultaneously */
	if ((set->flags & (I915_SET_COLORKEY_DESTINATION | I915_SET_COLORKEY_SOURCE)) == (I915_SET_COLORKEY_DESTINATION | I915_SET_COLORKEY_SOURCE))
		return -EINVAL;

	drm_modeset_lock_all(dev);

	plane = drm_plane_find(dev, set->plane_id);
	if (!plane) {
		ret = -ENOENT;
		goto out_unlock;
	}

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
	struct drm_plane *plane;
	struct intel_plane *intel_plane;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	drm_modeset_lock_all(dev);

	plane = drm_plane_find(dev, get->plane_id);
	if (!plane) {
		ret = -ENOENT;
		goto out_unlock;
	}

	intel_plane = to_intel_plane(plane);
	intel_plane->get_colorkey(plane, get);

out_unlock:
	drm_modeset_unlock_all(dev);
	return ret;
}

int intel_plane_set_property(struct drm_plane *plane,
			     struct drm_property *prop,
			     uint64_t val)
{
	struct drm_device *dev = plane->dev;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	uint64_t old_val;
	int ret = -ENOENT;

	if (prop == dev->mode_config.rotation_property) {
		/* exactly one rotation angle please */
		if (hweight32(val & 0xf) != 1)
			return -EINVAL;

		if (intel_plane->rotation == val)
			return 0;

		old_val = intel_plane->rotation;
		intel_plane->rotation = val;
		ret = intel_plane_restore(plane);
		if (ret)
			intel_plane->rotation = old_val;
	}

	return ret;
}

int intel_plane_restore(struct drm_plane *plane)
{
	struct intel_plane *intel_plane = to_intel_plane(plane);

	if (!plane->crtc || !plane->fb)
		return 0;

	return plane->funcs->update_plane(plane, plane->crtc, plane->fb,
				  intel_plane->crtc_x, intel_plane->crtc_y,
				  intel_plane->crtc_w, intel_plane->crtc_h,
				  intel_plane->src_x, intel_plane->src_y,
				  intel_plane->src_w, intel_plane->src_h);
}

void intel_plane_disable(struct drm_plane *plane)
{
	if (!plane->crtc || !plane->fb)
		return;

	intel_disable_plane(plane);
}

static const struct drm_plane_funcs intel_plane_funcs = {
	.update_plane = intel_update_plane,
	.disable_plane = intel_disable_plane,
	.destroy = intel_destroy_plane,
	.set_property = intel_plane_set_property,
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

static uint32_t skl_plane_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,
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

	intel_plane = kzalloc(sizeof(*intel_plane), GFP_KERNEL);
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
	case 8:
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
	case 9:
		/*
		 * FIXME: Skylake planes can be scaled (with some restrictions),
		 * but this is for another time.
		 */
		intel_plane->can_scale = false;
		intel_plane->max_downscale = 1;
		intel_plane->update_plane = skl_update_plane;
		intel_plane->disable_plane = skl_disable_plane;
		intel_plane->update_colorkey = skl_update_colorkey;
		intel_plane->get_colorkey = skl_get_colorkey;

		plane_formats = skl_plane_formats;
		num_plane_formats = ARRAY_SIZE(skl_plane_formats);
		break;
	default:
		kfree(intel_plane);
		return -ENODEV;
	}

	intel_plane->pipe = pipe;
	intel_plane->plane = plane;
	intel_plane->rotation = BIT(DRM_ROTATE_0);
	possible_crtcs = (1 << pipe);
	ret = drm_universal_plane_init(dev, &intel_plane->base, possible_crtcs,
				       &intel_plane_funcs,
				       plane_formats, num_plane_formats,
				       DRM_PLANE_TYPE_OVERLAY);
	if (ret) {
		kfree(intel_plane);
		goto out;
	}

	if (!dev->mode_config.rotation_property)
		dev->mode_config.rotation_property =
			drm_mode_create_rotation_property(dev,
							  BIT(DRM_ROTATE_0) |
							  BIT(DRM_ROTATE_180));

	if (dev->mode_config.rotation_property)
		drm_object_attach_property(&intel_plane->base.base,
					   dev->mode_config.rotation_property,
					   intel_plane->rotation);

 out:
	return ret;
}
