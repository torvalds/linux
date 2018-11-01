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
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_rect.h>
#include <drm/drm_atomic.h>
#include <drm/drm_plane_helper.h>
#include "intel_drv.h"
#include "intel_frontbuffer.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include <drm/drm_color_mgmt.h>

int intel_usecs_to_scanlines(const struct drm_display_mode *adjusted_mode,
			     int usecs)
{
	/* paranoia */
	if (!adjusted_mode->crtc_htotal)
		return 1;

	return DIV_ROUND_UP(usecs * adjusted_mode->crtc_clock,
			    1000 * adjusted_mode->crtc_htotal);
}

/* FIXME: We should instead only take spinlocks once for the entire update
 * instead of once per mmio. */
#if IS_ENABLED(CONFIG_PROVE_LOCKING)
#define VBLANK_EVASION_TIME_US 250
#else
#define VBLANK_EVASION_TIME_US 100
#endif

/**
 * intel_pipe_update_start() - start update of a set of display registers
 * @new_crtc_state: the new crtc state
 *
 * Mark the start of an update to pipe registers that should be updated
 * atomically regarding vblank. If the next vblank will happens within
 * the next 100 us, this function waits until the vblank passes.
 *
 * After a successful call to this function, interrupts will be disabled
 * until a subsequent call to intel_pipe_update_end(). That is done to
 * avoid random delays.
 */
void intel_pipe_update_start(const struct intel_crtc_state *new_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(new_crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct drm_display_mode *adjusted_mode = &new_crtc_state->base.adjusted_mode;
	long timeout = msecs_to_jiffies_timeout(1);
	int scanline, min, max, vblank_start;
	wait_queue_head_t *wq = drm_crtc_vblank_waitqueue(&crtc->base);
	bool need_vlv_dsi_wa = (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
		intel_crtc_has_type(new_crtc_state, INTEL_OUTPUT_DSI);
	DEFINE_WAIT(wait);
	u32 psr_status;

	vblank_start = adjusted_mode->crtc_vblank_start;
	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
		vblank_start = DIV_ROUND_UP(vblank_start, 2);

	/* FIXME needs to be calibrated sensibly */
	min = vblank_start - intel_usecs_to_scanlines(adjusted_mode,
						      VBLANK_EVASION_TIME_US);
	max = vblank_start - 1;

	if (min <= 0 || max <= 0)
		goto irq_disable;

	if (WARN_ON(drm_crtc_vblank_get(&crtc->base)))
		goto irq_disable;

	/*
	 * Wait for psr to idle out after enabling the VBL interrupts
	 * VBL interrupts will start the PSR exit and prevent a PSR
	 * re-entry as well.
	 */
	if (intel_psr_wait_for_idle(new_crtc_state, &psr_status))
		DRM_ERROR("PSR idle timed out 0x%x, atomic update may fail\n",
			  psr_status);

	local_irq_disable();

	crtc->debug.min_vbl = min;
	crtc->debug.max_vbl = max;
	trace_i915_pipe_update_start(crtc);

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

		if (!timeout) {
			DRM_ERROR("Potential atomic update failure on pipe %c\n",
				  pipe_name(crtc->pipe));
			break;
		}

		local_irq_enable();

		timeout = schedule_timeout(timeout);

		local_irq_disable();
	}

	finish_wait(wq, &wait);

	drm_crtc_vblank_put(&crtc->base);

	/*
	 * On VLV/CHV DSI the scanline counter would appear to
	 * increment approx. 1/3 of a scanline before start of vblank.
	 * The registers still get latched at start of vblank however.
	 * This means we must not write any registers on the first
	 * line of vblank (since not the whole line is actually in
	 * vblank). And unfortunately we can't use the interrupt to
	 * wait here since it will fire too soon. We could use the
	 * frame start interrupt instead since it will fire after the
	 * critical scanline, but that would require more changes
	 * in the interrupt code. So for now we'll just do the nasty
	 * thing and poll for the bad scanline to pass us by.
	 *
	 * FIXME figure out if BXT+ DSI suffers from this as well
	 */
	while (need_vlv_dsi_wa && scanline == vblank_start)
		scanline = intel_get_crtc_scanline(crtc);

	crtc->debug.scanline_start = scanline;
	crtc->debug.start_vbl_time = ktime_get();
	crtc->debug.start_vbl_count = intel_crtc_get_vblank_counter(crtc);

	trace_i915_pipe_update_vblank_evaded(crtc);
	return;

irq_disable:
	local_irq_disable();
}

/**
 * intel_pipe_update_end() - end update of a set of display registers
 * @new_crtc_state: the new crtc state
 *
 * Mark the end of an update started with intel_pipe_update_start(). This
 * re-enables interrupts and verifies the update was actually completed
 * before a vblank.
 */
void intel_pipe_update_end(struct intel_crtc_state *new_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(new_crtc_state->base.crtc);
	enum pipe pipe = crtc->pipe;
	int scanline_end = intel_get_crtc_scanline(crtc);
	u32 end_vbl_count = intel_crtc_get_vblank_counter(crtc);
	ktime_t end_vbl_time = ktime_get();
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	trace_i915_pipe_update_end(crtc, end_vbl_count, scanline_end);

	/* We're still in the vblank-evade critical section, this can't race.
	 * Would be slightly nice to just grab the vblank count and arm the
	 * event outside of the critical section - the spinlock might spin for a
	 * while ... */
	if (new_crtc_state->base.event) {
		WARN_ON(drm_crtc_vblank_get(&crtc->base) != 0);

		spin_lock(&crtc->base.dev->event_lock);
		drm_crtc_arm_vblank_event(&crtc->base, new_crtc_state->base.event);
		spin_unlock(&crtc->base.dev->event_lock);

		new_crtc_state->base.event = NULL;
	}

	local_irq_enable();

	if (intel_vgpu_active(dev_priv))
		return;

	if (crtc->debug.start_vbl_count &&
	    crtc->debug.start_vbl_count != end_vbl_count) {
		DRM_ERROR("Atomic update failure on pipe %c (start=%u end=%u) time %lld us, min %d, max %d, scanline start %d, end %d\n",
			  pipe_name(pipe), crtc->debug.start_vbl_count,
			  end_vbl_count,
			  ktime_us_delta(end_vbl_time, crtc->debug.start_vbl_time),
			  crtc->debug.min_vbl, crtc->debug.max_vbl,
			  crtc->debug.scanline_start, scanline_end);
	}
#ifdef CONFIG_DRM_I915_DEBUG_VBLANK_EVADE
	else if (ktime_us_delta(end_vbl_time, crtc->debug.start_vbl_time) >
		 VBLANK_EVASION_TIME_US)
		DRM_WARN("Atomic update on pipe (%c) took %lld us, max time under evasion is %u us\n",
			 pipe_name(pipe),
			 ktime_us_delta(end_vbl_time, crtc->debug.start_vbl_time),
			 VBLANK_EVASION_TIME_US);
#endif
}

int intel_plane_check_stride(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->base.plane);
	const struct drm_framebuffer *fb = plane_state->base.fb;
	unsigned int rotation = plane_state->base.rotation;
	u32 stride, max_stride;

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
	const struct drm_framebuffer *fb = plane_state->base.fb;
	struct drm_rect *src = &plane_state->base.src;
	u32 src_x, src_y, src_w, src_h;

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

	src->x1 = src_x << 16;
	src->x2 = (src_x + src_w) << 16;
	src->y1 = src_y << 16;
	src->y2 = (src_y + src_h) << 16;

	if (fb->format->is_yuv &&
	    (src_x & 1 || src_w & 1)) {
		DRM_DEBUG_KMS("src x/w (%u, %u) must be a multiple of 2 for YUV planes\n",
			      src_x, src_w);
		return -EINVAL;
	}

	if (fb->format->is_yuv &&
	    fb->format->num_planes > 1 &&
	    (src_y & 1 || src_h & 1)) {
		DRM_DEBUG_KMS("src y/h (%u, %u) must be a multiple of 2 for planar YUV planes\n",
			      src_y, src_h);
		return -EINVAL;
	}

	return 0;
}

static unsigned int
skl_plane_max_stride(struct intel_plane *plane,
		     u32 pixel_format, u64 modifier,
		     unsigned int rotation)
{
	int cpp = drm_format_plane_cpp(pixel_format, 0);

	/*
	 * "The stride in bytes must not exceed the
	 * of the size of 8K pixels and 32K bytes."
	 */
	if (drm_rotation_90_or_270(rotation))
		return min(8192, 32768 / cpp);
	else
		return min(8192 * cpp, 32768);
}

static void
skl_program_scaler(struct intel_plane *plane,
		   const struct intel_crtc_state *crtc_state,
		   const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;
	int scaler_id = plane_state->scaler_id;
	const struct intel_scaler *scaler =
		&crtc_state->scaler_state.scalers[scaler_id];
	int crtc_x = plane_state->base.dst.x1;
	int crtc_y = plane_state->base.dst.y1;
	uint32_t crtc_w = drm_rect_width(&plane_state->base.dst);
	uint32_t crtc_h = drm_rect_height(&plane_state->base.dst);
	u16 y_hphase, uv_rgb_hphase;
	u16 y_vphase, uv_rgb_vphase;

	/* TODO: handle sub-pixel coordinates */
	if (plane_state->base.fb->format->format == DRM_FORMAT_NV12 &&
	    !icl_is_hdr_plane(plane)) {
		y_hphase = skl_scaler_calc_phase(1, false);
		y_vphase = skl_scaler_calc_phase(1, false);

		/* MPEG2 chroma siting convention */
		uv_rgb_hphase = skl_scaler_calc_phase(2, true);
		uv_rgb_vphase = skl_scaler_calc_phase(2, false);
	} else {
		/* not used */
		y_hphase = 0;
		y_vphase = 0;

		uv_rgb_hphase = skl_scaler_calc_phase(1, false);
		uv_rgb_vphase = skl_scaler_calc_phase(1, false);
	}

	I915_WRITE_FW(SKL_PS_CTRL(pipe, scaler_id),
		      PS_SCALER_EN | PS_PLANE_SEL(plane->id) | scaler->mode);
	I915_WRITE_FW(SKL_PS_VPHASE(pipe, scaler_id),
		      PS_Y_PHASE(y_vphase) | PS_UV_RGB_PHASE(uv_rgb_vphase));
	I915_WRITE_FW(SKL_PS_HPHASE(pipe, scaler_id),
		      PS_Y_PHASE(y_hphase) | PS_UV_RGB_PHASE(uv_rgb_hphase));
	I915_WRITE_FW(SKL_PS_WIN_POS(pipe, scaler_id), (crtc_x << 16) | crtc_y);
	I915_WRITE_FW(SKL_PS_WIN_SZ(pipe, scaler_id), (crtc_w << 16) | crtc_h);
}

/* Preoffset values for YUV to RGB Conversion */
#define PREOFF_YUV_TO_RGB_HI		0x1800
#define PREOFF_YUV_TO_RGB_ME		0x1F00
#define PREOFF_YUV_TO_RGB_LO		0x1800

#define  ROFF(x)          (((x) & 0xffff) << 16)
#define  GOFF(x)          (((x) & 0xffff) << 0)
#define  BOFF(x)          (((x) & 0xffff) << 16)

static void
icl_program_input_csc_coeff(const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->base.plane->dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	enum pipe pipe = crtc->pipe;
	struct intel_plane *plane = to_intel_plane(plane_state->base.plane);
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
			0x9EF8, 0x7800, 0xABF8,
			0x0, 0x7800,  0x7ED8,
		},
	};

	/* Matrix for Limited Range to Full Range Conversion */
	static const u16 input_csc_matrix_lr[][9] = {
		/*
		 * BT.601 Limted range YCbCr -> full range RGB
		 * The matrix required is :
		 * [1.164384, 0.000, 1.596370,
		 *  1.138393, -0.382500, -0.794598,
		 *  1.138393, 1.971696, 0.0000]
		 */
		[DRM_COLOR_YCBCR_BT601] = {
			0x7CC8, 0x7950, 0x0,
			0x8CB8, 0x7918, 0x9C40,
			0x0, 0x7918, 0x7FC8,
		},
		/*
		 * BT.709 Limited range YCbCr -> full range RGB
		 * The matrix required is :
		 * [1.164, 0.000, 1.833671,
		 *  1.138393, -0.213249, -0.532909,
		 *  1.138393, 2.112402, 0.0000]
		 */
		[DRM_COLOR_YCBCR_BT709] = {
			0x7EA8, 0x7950, 0x0,
			0x8888, 0x7918, 0xADA8,
			0x0, 0x7918,  0x6870,
		},
	};
	const u16 *csc;

	if (plane_state->base.color_range == DRM_COLOR_YCBCR_FULL_RANGE)
		csc = input_csc_matrix[plane_state->base.color_encoding];
	else
		csc = input_csc_matrix_lr[plane_state->base.color_encoding];

	I915_WRITE_FW(PLANE_INPUT_CSC_COEFF(pipe, plane_id, 0), ROFF(csc[0]) |
		      GOFF(csc[1]));
	I915_WRITE_FW(PLANE_INPUT_CSC_COEFF(pipe, plane_id, 1), BOFF(csc[2]));
	I915_WRITE_FW(PLANE_INPUT_CSC_COEFF(pipe, plane_id, 2), ROFF(csc[3]) |
		      GOFF(csc[4]));
	I915_WRITE_FW(PLANE_INPUT_CSC_COEFF(pipe, plane_id, 3), BOFF(csc[5]));
	I915_WRITE_FW(PLANE_INPUT_CSC_COEFF(pipe, plane_id, 4), ROFF(csc[6]) |
		      GOFF(csc[7]));
	I915_WRITE_FW(PLANE_INPUT_CSC_COEFF(pipe, plane_id, 5), BOFF(csc[8]));

	I915_WRITE_FW(PLANE_INPUT_CSC_PREOFF(pipe, plane_id, 0),
		      PREOFF_YUV_TO_RGB_HI);
	I915_WRITE_FW(PLANE_INPUT_CSC_PREOFF(pipe, plane_id, 1),
		      PREOFF_YUV_TO_RGB_ME);
	I915_WRITE_FW(PLANE_INPUT_CSC_PREOFF(pipe, plane_id, 2),
		      PREOFF_YUV_TO_RGB_LO);
	I915_WRITE_FW(PLANE_INPUT_CSC_POSTOFF(pipe, plane_id, 0), 0x0);
	I915_WRITE_FW(PLANE_INPUT_CSC_POSTOFF(pipe, plane_id, 1), 0x0);
	I915_WRITE_FW(PLANE_INPUT_CSC_POSTOFF(pipe, plane_id, 2), 0x0);
}

static void
skl_program_plane(struct intel_plane *plane,
		  const struct intel_crtc_state *crtc_state,
		  const struct intel_plane_state *plane_state,
		  int color_plane, bool slave, u32 plane_ctl)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum plane_id plane_id = plane->id;
	enum pipe pipe = plane->pipe;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	u32 surf_addr = plane_state->color_plane[color_plane].offset;
	u32 stride = skl_plane_stride(plane_state, color_plane);
	u32 aux_stride = skl_plane_stride(plane_state, 1);
	int crtc_x = plane_state->base.dst.x1;
	int crtc_y = plane_state->base.dst.y1;
	uint32_t x = plane_state->color_plane[color_plane].x;
	uint32_t y = plane_state->color_plane[color_plane].y;
	uint32_t src_w = drm_rect_width(&plane_state->base.src) >> 16;
	uint32_t src_h = drm_rect_height(&plane_state->base.src) >> 16;
	struct intel_plane *linked = plane_state->linked_plane;
	const struct drm_framebuffer *fb = plane_state->base.fb;
	u8 alpha = plane_state->base.alpha >> 8;
	unsigned long irqflags;
	u32 keymsk, keymax;

	/* Sizes are 0 based */
	src_w--;
	src_h--;

	keymax = (key->max_value & 0xffffff) | PLANE_KEYMAX_ALPHA(alpha);

	keymsk = key->channel_mask & 0x3ffffff;
	if (alpha < 0xff)
		keymsk |= PLANE_KEYMSK_ALPHA_ENABLE;

	/* The scaler will handle the output position */
	if (plane_state->scaler_id >= 0) {
		crtc_x = 0;
		crtc_y = 0;
	}

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	if (INTEL_GEN(dev_priv) >= 10 || IS_GEMINILAKE(dev_priv))
		I915_WRITE_FW(PLANE_COLOR_CTL(pipe, plane_id),
			      plane_state->color_ctl);

	if (fb->format->is_yuv && icl_is_hdr_plane(plane))
		icl_program_input_csc_coeff(crtc_state, plane_state);

	I915_WRITE_FW(PLANE_KEYVAL(pipe, plane_id), key->min_value);
	I915_WRITE_FW(PLANE_KEYMAX(pipe, plane_id), keymax);
	I915_WRITE_FW(PLANE_KEYMSK(pipe, plane_id), keymsk);

	I915_WRITE_FW(PLANE_OFFSET(pipe, plane_id), (y << 16) | x);
	I915_WRITE_FW(PLANE_STRIDE(pipe, plane_id), stride);
	I915_WRITE_FW(PLANE_SIZE(pipe, plane_id), (src_h << 16) | src_w);
	I915_WRITE_FW(PLANE_AUX_DIST(pipe, plane_id),
		      (plane_state->color_plane[1].offset - surf_addr) | aux_stride);

	if (INTEL_GEN(dev_priv) < 11)
		I915_WRITE_FW(PLANE_AUX_OFFSET(pipe, plane_id),
			      (plane_state->color_plane[1].y << 16) |
			       plane_state->color_plane[1].x);

	if (icl_is_hdr_plane(plane)) {
		u32 cus_ctl = 0;

		if (linked) {
			/* Enable and use MPEG-2 chroma siting */
			cus_ctl = PLANE_CUS_ENABLE |
				PLANE_CUS_HPHASE_0 |
				PLANE_CUS_VPHASE_SIGN_NEGATIVE |
				PLANE_CUS_VPHASE_0_25;

			if (linked->id == PLANE_SPRITE5)
				cus_ctl |= PLANE_CUS_PLANE_7;
			else if (linked->id == PLANE_SPRITE4)
				cus_ctl |= PLANE_CUS_PLANE_6;
			else
				MISSING_CASE(linked->id);
		}

		I915_WRITE_FW(PLANE_CUS_CTL(pipe, plane_id), cus_ctl);
	}

	if (!slave && plane_state->scaler_id >= 0)
		skl_program_scaler(plane, crtc_state, plane_state);

	I915_WRITE_FW(PLANE_POS(pipe, plane_id), (crtc_y << 16) | crtc_x);

	I915_WRITE_FW(PLANE_CTL(pipe, plane_id), plane_ctl);
	I915_WRITE_FW(PLANE_SURF(pipe, plane_id),
		      intel_plane_ggtt_offset(plane_state) + surf_addr);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static void
skl_update_plane(struct intel_plane *plane,
		 const struct intel_crtc_state *crtc_state,
		 const struct intel_plane_state *plane_state)
{
	int color_plane = 0;

	if (plane_state->linked_plane) {
		/* Program the UV plane */
		color_plane = 1;
	}

	skl_program_plane(plane, crtc_state, plane_state,
			  color_plane, false, plane_state->ctl);
}

static void
icl_update_slave(struct intel_plane *plane,
		 const struct intel_crtc_state *crtc_state,
		 const struct intel_plane_state *plane_state)
{
	skl_program_plane(plane, crtc_state, plane_state, 0, true,
			  plane_state->ctl | PLANE_CTL_YUV420_Y_PLANE);
}

static void
skl_disable_plane(struct intel_plane *plane, struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum plane_id plane_id = plane->id;
	enum pipe pipe = plane->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	I915_WRITE_FW(PLANE_CTL(pipe, plane_id), 0);
	I915_WRITE_FW(PLANE_SURF(pipe, plane_id), 0);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static bool
skl_plane_get_hw_state(struct intel_plane *plane,
		       enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum intel_display_power_domain power_domain;
	enum plane_id plane_id = plane->id;
	bool ret;

	power_domain = POWER_DOMAIN_PIPE(plane->pipe);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain))
		return false;

	ret = I915_READ(PLANE_CTL(plane->pipe, plane_id)) & PLANE_CTL_ENABLE;

	*pipe = plane->pipe;

	intel_display_power_put(dev_priv, power_domain);

	return ret;
}

static void
chv_update_csc(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->base.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->base.fb;
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
	const s16 *csc = csc_matrix[plane_state->base.color_encoding];

	/* Seems RGB data bypasses the CSC always */
	if (!fb->format->is_yuv)
		return;

	I915_WRITE_FW(SPCSCYGOFF(plane_id), SPCSC_OOFF(0) | SPCSC_IOFF(0));
	I915_WRITE_FW(SPCSCCBOFF(plane_id), SPCSC_OOFF(0) | SPCSC_IOFF(0));
	I915_WRITE_FW(SPCSCCROFF(plane_id), SPCSC_OOFF(0) | SPCSC_IOFF(0));

	I915_WRITE_FW(SPCSCC01(plane_id), SPCSC_C1(csc[1]) | SPCSC_C0(csc[0]));
	I915_WRITE_FW(SPCSCC23(plane_id), SPCSC_C1(csc[3]) | SPCSC_C0(csc[2]));
	I915_WRITE_FW(SPCSCC45(plane_id), SPCSC_C1(csc[5]) | SPCSC_C0(csc[4]));
	I915_WRITE_FW(SPCSCC67(plane_id), SPCSC_C1(csc[7]) | SPCSC_C0(csc[6]));
	I915_WRITE_FW(SPCSCC8(plane_id), SPCSC_C0(csc[8]));

	I915_WRITE_FW(SPCSCYGICLAMP(plane_id), SPCSC_IMAX(1023) | SPCSC_IMIN(0));
	I915_WRITE_FW(SPCSCCBICLAMP(plane_id), SPCSC_IMAX(512) | SPCSC_IMIN(-512));
	I915_WRITE_FW(SPCSCCRICLAMP(plane_id), SPCSC_IMAX(512) | SPCSC_IMIN(-512));

	I915_WRITE_FW(SPCSCYGOCLAMP(plane_id), SPCSC_OMAX(1023) | SPCSC_OMIN(0));
	I915_WRITE_FW(SPCSCCBOCLAMP(plane_id), SPCSC_OMAX(1023) | SPCSC_OMIN(0));
	I915_WRITE_FW(SPCSCCROCLAMP(plane_id), SPCSC_OMAX(1023) | SPCSC_OMIN(0));
}

#define SIN_0 0
#define COS_0 1

static void
vlv_update_clrc(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->base.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->base.fb;
	enum pipe pipe = plane->pipe;
	enum plane_id plane_id = plane->id;
	int contrast, brightness, sh_scale, sh_sin, sh_cos;

	if (fb->format->is_yuv &&
	    plane_state->base.color_range == DRM_COLOR_YCBCR_LIMITED_RANGE) {
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
	I915_WRITE_FW(SPCLRC0(pipe, plane_id),
		      SP_CONTRAST(contrast) | SP_BRIGHTNESS(brightness));
	I915_WRITE_FW(SPCLRC1(pipe, plane_id),
		      SP_SH_SIN(sh_sin) | SP_SH_COS(sh_cos));
}

static u32 vlv_sprite_ctl(const struct intel_crtc_state *crtc_state,
			  const struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->base.fb;
	unsigned int rotation = plane_state->base.rotation;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	u32 sprctl;

	sprctl = SP_ENABLE | SP_GAMMA_ENABLE;

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
		MISSING_CASE(fb->format->format);
		return 0;
	}

	if (plane_state->base.color_encoding == DRM_COLOR_YCBCR_BT709)
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

static void
vlv_update_plane(struct intel_plane *plane,
		 const struct intel_crtc_state *crtc_state,
		 const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->base.fb;
	enum pipe pipe = plane->pipe;
	enum plane_id plane_id = plane->id;
	u32 sprctl = plane_state->ctl;
	u32 sprsurf_offset = plane_state->color_plane[0].offset;
	u32 linear_offset;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	int crtc_x = plane_state->base.dst.x1;
	int crtc_y = plane_state->base.dst.y1;
	uint32_t crtc_w = drm_rect_width(&plane_state->base.dst);
	uint32_t crtc_h = drm_rect_height(&plane_state->base.dst);
	uint32_t x = plane_state->color_plane[0].x;
	uint32_t y = plane_state->color_plane[0].y;
	unsigned long irqflags;

	/* Sizes are 0 based */
	crtc_w--;
	crtc_h--;

	linear_offset = intel_fb_xy_to_linear(x, y, plane_state, 0);

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	vlv_update_clrc(plane_state);

	if (IS_CHERRYVIEW(dev_priv) && pipe == PIPE_B)
		chv_update_csc(plane_state);

	if (key->flags) {
		I915_WRITE_FW(SPKEYMINVAL(pipe, plane_id), key->min_value);
		I915_WRITE_FW(SPKEYMAXVAL(pipe, plane_id), key->max_value);
		I915_WRITE_FW(SPKEYMSK(pipe, plane_id), key->channel_mask);
	}
	I915_WRITE_FW(SPSTRIDE(pipe, plane_id),
		      plane_state->color_plane[0].stride);
	I915_WRITE_FW(SPPOS(pipe, plane_id), (crtc_y << 16) | crtc_x);

	if (fb->modifier == I915_FORMAT_MOD_X_TILED)
		I915_WRITE_FW(SPTILEOFF(pipe, plane_id), (y << 16) | x);
	else
		I915_WRITE_FW(SPLINOFF(pipe, plane_id), linear_offset);

	I915_WRITE_FW(SPCONSTALPHA(pipe, plane_id), 0);

	I915_WRITE_FW(SPSIZE(pipe, plane_id), (crtc_h << 16) | crtc_w);
	I915_WRITE_FW(SPCNTR(pipe, plane_id), sprctl);
	I915_WRITE_FW(SPSURF(pipe, plane_id),
		      intel_plane_ggtt_offset(plane_state) + sprsurf_offset);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static void
vlv_disable_plane(struct intel_plane *plane, struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;
	enum plane_id plane_id = plane->id;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	I915_WRITE_FW(SPCNTR(pipe, plane_id), 0);
	I915_WRITE_FW(SPSURF(pipe, plane_id), 0);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static bool
vlv_plane_get_hw_state(struct intel_plane *plane,
		       enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum intel_display_power_domain power_domain;
	enum plane_id plane_id = plane->id;
	bool ret;

	power_domain = POWER_DOMAIN_PIPE(plane->pipe);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain))
		return false;

	ret = I915_READ(SPCNTR(plane->pipe, plane_id)) & SP_ENABLE;

	*pipe = plane->pipe;

	intel_display_power_put(dev_priv, power_domain);

	return ret;
}

static u32 ivb_sprite_ctl(const struct intel_crtc_state *crtc_state,
			  const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->base.plane->dev);
	const struct drm_framebuffer *fb = plane_state->base.fb;
	unsigned int rotation = plane_state->base.rotation;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	u32 sprctl;

	sprctl = SPRITE_ENABLE | SPRITE_GAMMA_ENABLE;

	if (IS_IVYBRIDGE(dev_priv))
		sprctl |= SPRITE_TRICKLE_FEED_DISABLE;

	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		sprctl |= SPRITE_PIPE_CSC_ENABLE;

	switch (fb->format->format) {
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
		MISSING_CASE(fb->format->format);
		return 0;
	}

	if (plane_state->base.color_encoding == DRM_COLOR_YCBCR_BT709)
		sprctl |= SPRITE_YUV_TO_RGB_CSC_FORMAT_BT709;

	if (plane_state->base.color_range == DRM_COLOR_YCBCR_FULL_RANGE)
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

static void
ivb_update_plane(struct intel_plane *plane,
		 const struct intel_crtc_state *crtc_state,
		 const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->base.fb;
	enum pipe pipe = plane->pipe;
	u32 sprctl = plane_state->ctl, sprscale = 0;
	u32 sprsurf_offset = plane_state->color_plane[0].offset;
	u32 linear_offset;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	int crtc_x = plane_state->base.dst.x1;
	int crtc_y = plane_state->base.dst.y1;
	uint32_t crtc_w = drm_rect_width(&plane_state->base.dst);
	uint32_t crtc_h = drm_rect_height(&plane_state->base.dst);
	uint32_t x = plane_state->color_plane[0].x;
	uint32_t y = plane_state->color_plane[0].y;
	uint32_t src_w = drm_rect_width(&plane_state->base.src) >> 16;
	uint32_t src_h = drm_rect_height(&plane_state->base.src) >> 16;
	unsigned long irqflags;

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	crtc_w--;
	crtc_h--;

	if (crtc_w != src_w || crtc_h != src_h)
		sprscale = SPRITE_SCALE_ENABLE | (src_w << 16) | src_h;

	linear_offset = intel_fb_xy_to_linear(x, y, plane_state, 0);

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	if (key->flags) {
		I915_WRITE_FW(SPRKEYVAL(pipe), key->min_value);
		I915_WRITE_FW(SPRKEYMAX(pipe), key->max_value);
		I915_WRITE_FW(SPRKEYMSK(pipe), key->channel_mask);
	}

	I915_WRITE_FW(SPRSTRIDE(pipe), plane_state->color_plane[0].stride);
	I915_WRITE_FW(SPRPOS(pipe), (crtc_y << 16) | crtc_x);

	/* HSW consolidates SPRTILEOFF and SPRLINOFF into a single SPROFFSET
	 * register */
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		I915_WRITE_FW(SPROFFSET(pipe), (y << 16) | x);
	else if (fb->modifier == I915_FORMAT_MOD_X_TILED)
		I915_WRITE_FW(SPRTILEOFF(pipe), (y << 16) | x);
	else
		I915_WRITE_FW(SPRLINOFF(pipe), linear_offset);

	I915_WRITE_FW(SPRSIZE(pipe), (crtc_h << 16) | crtc_w);
	if (IS_IVYBRIDGE(dev_priv))
		I915_WRITE_FW(SPRSCALE(pipe), sprscale);
	I915_WRITE_FW(SPRCTL(pipe), sprctl);
	I915_WRITE_FW(SPRSURF(pipe),
		      intel_plane_ggtt_offset(plane_state) + sprsurf_offset);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static void
ivb_disable_plane(struct intel_plane *plane, struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	I915_WRITE_FW(SPRCTL(pipe), 0);
	/* Can't leave the scaler enabled... */
	if (IS_IVYBRIDGE(dev_priv))
		I915_WRITE_FW(SPRSCALE(pipe), 0);
	I915_WRITE_FW(SPRSURF(pipe), 0);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static bool
ivb_plane_get_hw_state(struct intel_plane *plane,
		       enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum intel_display_power_domain power_domain;
	bool ret;

	power_domain = POWER_DOMAIN_PIPE(plane->pipe);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain))
		return false;

	ret =  I915_READ(SPRCTL(plane->pipe)) & SPRITE_ENABLE;

	*pipe = plane->pipe;

	intel_display_power_put(dev_priv, power_domain);

	return ret;
}

static unsigned int
g4x_sprite_max_stride(struct intel_plane *plane,
		      u32 pixel_format, u64 modifier,
		      unsigned int rotation)
{
	return 16384;
}

static u32 g4x_sprite_ctl(const struct intel_crtc_state *crtc_state,
			  const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->base.plane->dev);
	const struct drm_framebuffer *fb = plane_state->base.fb;
	unsigned int rotation = plane_state->base.rotation;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	u32 dvscntr;

	dvscntr = DVS_ENABLE | DVS_GAMMA_ENABLE;

	if (IS_GEN6(dev_priv))
		dvscntr |= DVS_TRICKLE_FEED_DISABLE;

	switch (fb->format->format) {
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
		MISSING_CASE(fb->format->format);
		return 0;
	}

	if (plane_state->base.color_encoding == DRM_COLOR_YCBCR_BT709)
		dvscntr |= DVS_YUV_FORMAT_BT709;

	if (plane_state->base.color_range == DRM_COLOR_YCBCR_FULL_RANGE)
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

static void
g4x_update_plane(struct intel_plane *plane,
		 const struct intel_crtc_state *crtc_state,
		 const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->base.fb;
	enum pipe pipe = plane->pipe;
	u32 dvscntr = plane_state->ctl, dvsscale = 0;
	u32 dvssurf_offset = plane_state->color_plane[0].offset;
	u32 linear_offset;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	int crtc_x = plane_state->base.dst.x1;
	int crtc_y = plane_state->base.dst.y1;
	uint32_t crtc_w = drm_rect_width(&plane_state->base.dst);
	uint32_t crtc_h = drm_rect_height(&plane_state->base.dst);
	uint32_t x = plane_state->color_plane[0].x;
	uint32_t y = plane_state->color_plane[0].y;
	uint32_t src_w = drm_rect_width(&plane_state->base.src) >> 16;
	uint32_t src_h = drm_rect_height(&plane_state->base.src) >> 16;
	unsigned long irqflags;

	/* Sizes are 0 based */
	src_w--;
	src_h--;
	crtc_w--;
	crtc_h--;

	if (crtc_w != src_w || crtc_h != src_h)
		dvsscale = DVS_SCALE_ENABLE | (src_w << 16) | src_h;

	linear_offset = intel_fb_xy_to_linear(x, y, plane_state, 0);

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	if (key->flags) {
		I915_WRITE_FW(DVSKEYVAL(pipe), key->min_value);
		I915_WRITE_FW(DVSKEYMAX(pipe), key->max_value);
		I915_WRITE_FW(DVSKEYMSK(pipe), key->channel_mask);
	}

	I915_WRITE_FW(DVSSTRIDE(pipe), plane_state->color_plane[0].stride);
	I915_WRITE_FW(DVSPOS(pipe), (crtc_y << 16) | crtc_x);

	if (fb->modifier == I915_FORMAT_MOD_X_TILED)
		I915_WRITE_FW(DVSTILEOFF(pipe), (y << 16) | x);
	else
		I915_WRITE_FW(DVSLINOFF(pipe), linear_offset);

	I915_WRITE_FW(DVSSIZE(pipe), (crtc_h << 16) | crtc_w);
	I915_WRITE_FW(DVSSCALE(pipe), dvsscale);
	I915_WRITE_FW(DVSCNTR(pipe), dvscntr);
	I915_WRITE_FW(DVSSURF(pipe),
		      intel_plane_ggtt_offset(plane_state) + dvssurf_offset);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static void
g4x_disable_plane(struct intel_plane *plane, struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	I915_WRITE_FW(DVSCNTR(pipe), 0);
	/* Disable the scaler */
	I915_WRITE_FW(DVSSCALE(pipe), 0);
	I915_WRITE_FW(DVSSURF(pipe), 0);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static bool
g4x_plane_get_hw_state(struct intel_plane *plane,
		       enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum intel_display_power_domain power_domain;
	bool ret;

	power_domain = POWER_DOMAIN_PIPE(plane->pipe);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain))
		return false;

	ret = I915_READ(DVSCNTR(plane->pipe)) & DVS_ENABLE;

	*pipe = plane->pipe;

	intel_display_power_put(dev_priv, power_domain);

	return ret;
}

static bool intel_fb_scalable(const struct drm_framebuffer *fb)
{
	if (!fb)
		return false;

	switch (fb->format->format) {
	case DRM_FORMAT_C8:
		return false;
	default:
		return true;
	}
}

static int
g4x_sprite_check_scaling(struct intel_crtc_state *crtc_state,
			 struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->base.fb;
	const struct drm_rect *src = &plane_state->base.src;
	const struct drm_rect *dst = &plane_state->base.dst;
	int src_x, src_y, src_w, src_h, crtc_w, crtc_h;
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->base.adjusted_mode;
	unsigned int cpp = fb->format->cpp[0];
	unsigned int width_bytes;
	int min_width, min_height;

	crtc_w = drm_rect_width(dst);
	crtc_h = drm_rect_height(dst);

	src_x = src->x1 >> 16;
	src_y = src->y1 >> 16;
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

	if (width_bytes > 4096 || fb->pitches[0] > 4096) {
		DRM_DEBUG_KMS("Stride (%u) exceeds hardware max with scaling (%u)\n",
			      fb->pitches[0], 4096);
		return -EINVAL;
	}

	return 0;
}

static int
g4x_sprite_check(struct intel_crtc_state *crtc_state,
		 struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->base.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	int min_scale = DRM_PLANE_HELPER_NO_SCALING;
	int max_scale = DRM_PLANE_HELPER_NO_SCALING;
	int ret;

	if (intel_fb_scalable(plane_state->base.fb)) {
		if (INTEL_GEN(dev_priv) < 7) {
			min_scale = 1;
			max_scale = 16 << 16;
		} else if (IS_IVYBRIDGE(dev_priv)) {
			min_scale = 1;
			max_scale = 2 << 16;
		}
	}

	ret = drm_atomic_helper_check_plane_state(&plane_state->base,
						  &crtc_state->base,
						  min_scale, max_scale,
						  true, true);
	if (ret)
		return ret;

	if (!plane_state->base.visible)
		return 0;

	ret = intel_plane_check_src_coordinates(plane_state);
	if (ret)
		return ret;

	ret = g4x_sprite_check_scaling(crtc_state, plane_state);
	if (ret)
		return ret;

	ret = i9xx_check_plane_surface(plane_state);
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
	struct intel_plane *plane = to_intel_plane(plane_state->base.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	unsigned int rotation = plane_state->base.rotation;

	/* CHV ignores the mirror bit when the rotate bit is set :( */
	if (IS_CHERRYVIEW(dev_priv) &&
	    rotation & DRM_MODE_ROTATE_180 &&
	    rotation & DRM_MODE_REFLECT_X) {
		DRM_DEBUG_KMS("Cannot rotate and reflect at the same time\n");
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

	ret = drm_atomic_helper_check_plane_state(&plane_state->base,
						  &crtc_state->base,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  true, true);
	if (ret)
		return ret;

	if (!plane_state->base.visible)
		return 0;

	ret = intel_plane_check_src_coordinates(plane_state);
	if (ret)
		return ret;

	ret = i9xx_check_plane_surface(plane_state);
	if (ret)
		return ret;

	plane_state->ctl = vlv_sprite_ctl(crtc_state, plane_state);

	return 0;
}

static int skl_plane_check_fb(const struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->base.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->base.fb;
	unsigned int rotation = plane_state->base.rotation;
	struct drm_format_name_buf format_name;

	if (!fb)
		return 0;

	if (rotation & ~(DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_180) &&
	    is_ccs_modifier(fb->modifier)) {
		DRM_DEBUG_KMS("RC support only with 0/180 degree rotation (%x)\n",
			      rotation);
		return -EINVAL;
	}

	if (rotation & DRM_MODE_REFLECT_X &&
	    fb->modifier == DRM_FORMAT_MOD_LINEAR) {
		DRM_DEBUG_KMS("horizontal flip is not supported with linear surface formats\n");
		return -EINVAL;
	}

	if (drm_rotation_90_or_270(rotation)) {
		if (fb->modifier != I915_FORMAT_MOD_Y_TILED &&
		    fb->modifier != I915_FORMAT_MOD_Yf_TILED) {
			DRM_DEBUG_KMS("Y/Yf tiling required for 90/270!\n");
			return -EINVAL;
		}

		/*
		 * 90/270 is not allowed with RGB64 16:16:16:16 and
		 * Indexed 8-bit. RGB 16-bit 5:6:5 is allowed gen11 onwards.
		 * TBD: Add RGB64 case once its added in supported format
		 * list.
		 */
		switch (fb->format->format) {
		case DRM_FORMAT_RGB565:
			if (INTEL_GEN(dev_priv) >= 11)
				break;
			/* fall through */
		case DRM_FORMAT_C8:
			DRM_DEBUG_KMS("Unsupported pixel format %s for 90/270!\n",
				      drm_get_format_name(fb->format->format,
							  &format_name));
			return -EINVAL;
		default:
			break;
		}
	}

	/* Y-tiling is not supported in IF-ID Interlace mode */
	if (crtc_state->base.enable &&
	    crtc_state->base.adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE &&
	    (fb->modifier == I915_FORMAT_MOD_Y_TILED ||
	     fb->modifier == I915_FORMAT_MOD_Yf_TILED ||
	     fb->modifier == I915_FORMAT_MOD_Y_TILED_CCS ||
	     fb->modifier == I915_FORMAT_MOD_Yf_TILED_CCS)) {
		DRM_DEBUG_KMS("Y/Yf tiling not supported in IF-ID mode\n");
		return -EINVAL;
	}

	return 0;
}

static int skl_plane_check_dst_coordinates(const struct intel_crtc_state *crtc_state,
					   const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->base.plane->dev);
	int crtc_x = plane_state->base.dst.x1;
	int crtc_w = drm_rect_width(&plane_state->base.dst);
	int pipe_src_w = crtc_state->pipe_src_w;

	/*
	 * Display WA #1175: cnl,glk
	 * Planes other than the cursor may cause FIFO underflow and display
	 * corruption if starting less than 4 pixels from the right edge of
	 * the screen.
	 * Besides the above WA fix the similar problem, where planes other
	 * than the cursor ending less than 4 pixels from the left edge of the
	 * screen may cause FIFO underflow and display corruption.
	 */
	if ((IS_GEMINILAKE(dev_priv) || IS_CANNONLAKE(dev_priv)) &&
	    (crtc_x + crtc_w < 4 || crtc_x > pipe_src_w - 4)) {
		DRM_DEBUG_KMS("requested plane X %s position %d invalid (valid range %d-%d)\n",
			      crtc_x + crtc_w < 4 ? "end" : "start",
			      crtc_x + crtc_w < 4 ? crtc_x + crtc_w : crtc_x,
			      4, pipe_src_w - 4);
		return -ERANGE;
	}

	return 0;
}

static int skl_plane_check_nv12_rotation(const struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->base.fb;
	unsigned int rotation = plane_state->base.rotation;
	int src_w = drm_rect_width(&plane_state->base.src) >> 16;

	/* Display WA #1106 */
	if (fb->format->format == DRM_FORMAT_NV12 && src_w & 3 &&
	    (rotation == DRM_MODE_ROTATE_270 ||
	     rotation == (DRM_MODE_REFLECT_X | DRM_MODE_ROTATE_90))) {
		DRM_DEBUG_KMS("src width must be multiple of 4 for rotated NV12\n");
		return -EINVAL;
	}

	return 0;
}

static int skl_plane_check(struct intel_crtc_state *crtc_state,
			   struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->base.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->base.fb;
	int min_scale = DRM_PLANE_HELPER_NO_SCALING;
	int max_scale = DRM_PLANE_HELPER_NO_SCALING;
	int ret;

	ret = skl_plane_check_fb(crtc_state, plane_state);
	if (ret)
		return ret;

	/* use scaler when colorkey is not required */
	if (!plane_state->ckey.flags && intel_fb_scalable(fb)) {
		min_scale = 1;
		max_scale = skl_max_scale(crtc_state, fb->format->format);
	}

	ret = drm_atomic_helper_check_plane_state(&plane_state->base,
						  &crtc_state->base,
						  min_scale, max_scale,
						  true, true);
	if (ret)
		return ret;

	if (!plane_state->base.visible)
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

	ret = skl_check_plane_surface(plane_state);
	if (ret)
		return ret;

	/* HW only has 8 bits pixel precision, disable plane if invisible */
	if (!(plane_state->base.alpha >> 8))
		plane_state->base.visible = false;

	plane_state->ctl = skl_plane_ctl(crtc_state, plane_state);

	if (INTEL_GEN(dev_priv) >= 10 || IS_GEMINILAKE(dev_priv))
		plane_state->color_ctl = glk_plane_color_ctl(crtc_state,
							     plane_state);

	return 0;
}

static bool has_dst_key_in_primary_plane(struct drm_i915_private *dev_priv)
{
	return INTEL_GEN(dev_priv) >= 9;
}

static void intel_plane_set_ckey(struct intel_plane_state *plane_state,
				 const struct drm_intel_sprite_colorkey *set)
{
	struct intel_plane *plane = to_intel_plane(plane_state->base.plane);
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

static const uint32_t g4x_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
};

static const uint64_t i9xx_plane_format_modifiers[] = {
	I915_FORMAT_MOD_X_TILED,
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static const uint32_t snb_plane_formats[] = {
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
};

static const uint32_t vlv_plane_formats[] = {
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

static const uint32_t skl_plane_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
};

static const uint32_t skl_planar_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_NV12,
};

static const uint64_t skl_plane_format_modifiers_noccs[] = {
	I915_FORMAT_MOD_Yf_TILED,
	I915_FORMAT_MOD_Y_TILED,
	I915_FORMAT_MOD_X_TILED,
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static const uint64_t skl_plane_format_modifiers_ccs[] = {
	I915_FORMAT_MOD_Yf_TILED_CCS,
	I915_FORMAT_MOD_Y_TILED_CCS,
	I915_FORMAT_MOD_Yf_TILED,
	I915_FORMAT_MOD_Y_TILED,
	I915_FORMAT_MOD_X_TILED,
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
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
		/* fall through */
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
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		if (modifier == DRM_FORMAT_MOD_LINEAR ||
		    modifier == I915_FORMAT_MOD_X_TILED)
			return true;
		/* fall through */
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
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		if (modifier == DRM_FORMAT_MOD_LINEAR ||
		    modifier == I915_FORMAT_MOD_X_TILED)
			return true;
		/* fall through */
	default:
		return false;
	}
}

static bool skl_plane_format_mod_supported(struct drm_plane *_plane,
					   u32 format, u64 modifier)
{
	struct intel_plane *plane = to_intel_plane(_plane);

	switch (modifier) {
	case DRM_FORMAT_MOD_LINEAR:
	case I915_FORMAT_MOD_X_TILED:
	case I915_FORMAT_MOD_Y_TILED:
	case I915_FORMAT_MOD_Yf_TILED:
		break;
	case I915_FORMAT_MOD_Y_TILED_CCS:
	case I915_FORMAT_MOD_Yf_TILED_CCS:
		if (!plane->has_ccs)
			return false;
		break;
	default:
		return false;
	}

	switch (format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
		if (is_ccs_modifier(modifier))
			return true;
		/* fall through */
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_NV12:
		if (modifier == I915_FORMAT_MOD_Yf_TILED)
			return true;
		/* fall through */
	case DRM_FORMAT_C8:
		if (modifier == DRM_FORMAT_MOD_LINEAR ||
		    modifier == I915_FORMAT_MOD_X_TILED ||
		    modifier == I915_FORMAT_MOD_Y_TILED)
			return true;
		/* fall through */
	default:
		return false;
	}
}

static const struct drm_plane_funcs g4x_sprite_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = intel_plane_destroy,
	.atomic_get_property = intel_plane_atomic_get_property,
	.atomic_set_property = intel_plane_atomic_set_property,
	.atomic_duplicate_state = intel_plane_duplicate_state,
	.atomic_destroy_state = intel_plane_destroy_state,
	.format_mod_supported = g4x_sprite_format_mod_supported,
};

static const struct drm_plane_funcs snb_sprite_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = intel_plane_destroy,
	.atomic_get_property = intel_plane_atomic_get_property,
	.atomic_set_property = intel_plane_atomic_set_property,
	.atomic_duplicate_state = intel_plane_duplicate_state,
	.atomic_destroy_state = intel_plane_destroy_state,
	.format_mod_supported = snb_sprite_format_mod_supported,
};

static const struct drm_plane_funcs vlv_sprite_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = intel_plane_destroy,
	.atomic_get_property = intel_plane_atomic_get_property,
	.atomic_set_property = intel_plane_atomic_set_property,
	.atomic_duplicate_state = intel_plane_duplicate_state,
	.atomic_destroy_state = intel_plane_destroy_state,
	.format_mod_supported = vlv_sprite_format_mod_supported,
};

static const struct drm_plane_funcs skl_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = intel_plane_destroy,
	.atomic_get_property = intel_plane_atomic_get_property,
	.atomic_set_property = intel_plane_atomic_set_property,
	.atomic_duplicate_state = intel_plane_duplicate_state,
	.atomic_destroy_state = intel_plane_destroy_state,
	.format_mod_supported = skl_plane_format_mod_supported,
};

static bool skl_plane_has_fbc(struct drm_i915_private *dev_priv,
			      enum pipe pipe, enum plane_id plane_id)
{
	if (!HAS_FBC(dev_priv))
		return false;

	return pipe == PIPE_A && plane_id == PLANE_PRIMARY;
}

static bool skl_plane_has_planar(struct drm_i915_private *dev_priv,
				 enum pipe pipe, enum plane_id plane_id)
{
	if (INTEL_GEN(dev_priv) >= 11)
		return plane_id <= PLANE_SPRITE3;

	/* Display WA #0870: skl, bxt */
	if (IS_SKYLAKE(dev_priv) || IS_BROXTON(dev_priv))
		return false;

	if (IS_GEN9(dev_priv) && !IS_GEMINILAKE(dev_priv) && pipe == PIPE_C)
		return false;

	if (plane_id != PLANE_PRIMARY && plane_id != PLANE_SPRITE0)
		return false;

	return true;
}

static bool skl_plane_has_ccs(struct drm_i915_private *dev_priv,
			      enum pipe pipe, enum plane_id plane_id)
{
	if (plane_id == PLANE_CURSOR)
		return false;

	if (INTEL_GEN(dev_priv) >= 10)
		return true;

	if (IS_GEMINILAKE(dev_priv))
		return pipe != PIPE_C;

	return pipe != PIPE_C &&
		(plane_id == PLANE_PRIMARY ||
		 plane_id == PLANE_SPRITE0);
}

struct intel_plane *intel_plane_alloc(void)
{
	struct intel_plane_state *plane_state;
	struct intel_plane *plane;

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	plane_state = intel_create_plane_state(&plane->base);
	if (!plane_state) {
		kfree(plane);
		return ERR_PTR(-ENOMEM);
	}

	plane->base.state = &plane_state->base;

	return plane;
}

void intel_plane_free(struct intel_plane *plane)
{
	struct intel_plane_state *plane_state =
		to_intel_plane_state(plane->base.state);

	kfree(plane_state);
	kfree(plane);
}

struct intel_plane *
skl_universal_plane_create(struct drm_i915_private *dev_priv,
			   enum pipe pipe, enum plane_id plane_id)
{
	struct intel_plane *plane;
	enum drm_plane_type plane_type;
	unsigned int supported_rotations;
	unsigned int possible_crtcs;
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

	plane->has_fbc = skl_plane_has_fbc(dev_priv, pipe, plane_id);
	if (plane->has_fbc) {
		struct intel_fbc *fbc = &dev_priv->fbc;

		fbc->possible_framebuffer_bits |= plane->frontbuffer_bit;
	}

	plane->max_stride = skl_plane_max_stride;
	plane->update_plane = skl_update_plane;
	plane->disable_plane = skl_disable_plane;
	plane->get_hw_state = skl_plane_get_hw_state;
	plane->check_plane = skl_plane_check;
	if (icl_is_nv12_y_plane(plane_id))
		plane->update_slave = icl_update_slave;

	if (skl_plane_has_planar(dev_priv, pipe, plane_id)) {
		formats = skl_planar_formats;
		num_formats = ARRAY_SIZE(skl_planar_formats);
	} else {
		formats = skl_plane_formats;
		num_formats = ARRAY_SIZE(skl_plane_formats);
	}

	plane->has_ccs = skl_plane_has_ccs(dev_priv, pipe, plane_id);
	if (plane->has_ccs)
		modifiers = skl_plane_format_modifiers_ccs;
	else
		modifiers = skl_plane_format_modifiers_noccs;

	if (plane_id == PLANE_PRIMARY)
		plane_type = DRM_PLANE_TYPE_PRIMARY;
	else
		plane_type = DRM_PLANE_TYPE_OVERLAY;

	possible_crtcs = BIT(pipe);

	ret = drm_universal_plane_init(&dev_priv->drm, &plane->base,
				       possible_crtcs, &skl_plane_funcs,
				       formats, num_formats, modifiers,
				       plane_type,
				       "plane %d%c", plane_id + 1,
				       pipe_name(pipe));
	if (ret)
		goto fail;

	supported_rotations =
		DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_90 |
		DRM_MODE_ROTATE_180 | DRM_MODE_ROTATE_270;

	if (INTEL_GEN(dev_priv) >= 10)
		supported_rotations |= DRM_MODE_REFLECT_X;

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

	drm_plane_create_alpha_property(&plane->base);
	drm_plane_create_blend_mode_property(&plane->base,
					     BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					     BIT(DRM_MODE_BLEND_PREMULTI) |
					     BIT(DRM_MODE_BLEND_COVERAGE));

	drm_plane_helper_add(&plane->base, &intel_plane_helper_funcs);

	return plane;

fail:
	intel_plane_free(plane);

	return ERR_PTR(ret);
}

struct intel_plane *
intel_sprite_plane_create(struct drm_i915_private *dev_priv,
			  enum pipe pipe, int sprite)
{
	struct intel_plane *plane;
	const struct drm_plane_funcs *plane_funcs;
	unsigned long possible_crtcs;
	unsigned int supported_rotations;
	const u64 *modifiers;
	const u32 *formats;
	int num_formats;
	int ret;

	if (INTEL_GEN(dev_priv) >= 9)
		return skl_universal_plane_create(dev_priv, pipe,
						  PLANE_SPRITE0 + sprite);

	plane = intel_plane_alloc();
	if (IS_ERR(plane))
		return plane;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		plane->max_stride = i9xx_plane_max_stride;
		plane->update_plane = vlv_update_plane;
		plane->disable_plane = vlv_disable_plane;
		plane->get_hw_state = vlv_plane_get_hw_state;
		plane->check_plane = vlv_sprite_check;

		formats = vlv_plane_formats;
		num_formats = ARRAY_SIZE(vlv_plane_formats);
		modifiers = i9xx_plane_format_modifiers;

		plane_funcs = &vlv_sprite_funcs;
	} else if (INTEL_GEN(dev_priv) >= 7) {
		plane->max_stride = g4x_sprite_max_stride;
		plane->update_plane = ivb_update_plane;
		plane->disable_plane = ivb_disable_plane;
		plane->get_hw_state = ivb_plane_get_hw_state;
		plane->check_plane = g4x_sprite_check;

		formats = snb_plane_formats;
		num_formats = ARRAY_SIZE(snb_plane_formats);
		modifiers = i9xx_plane_format_modifiers;

		plane_funcs = &snb_sprite_funcs;
	} else {
		plane->max_stride = g4x_sprite_max_stride;
		plane->update_plane = g4x_update_plane;
		plane->disable_plane = g4x_disable_plane;
		plane->get_hw_state = g4x_plane_get_hw_state;
		plane->check_plane = g4x_sprite_check;

		modifiers = i9xx_plane_format_modifiers;
		if (IS_GEN6(dev_priv)) {
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

	possible_crtcs = BIT(pipe);

	ret = drm_universal_plane_init(&dev_priv->drm, &plane->base,
				       possible_crtcs, plane_funcs,
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

	drm_plane_helper_add(&plane->base, &intel_plane_helper_funcs);

	return plane;

fail:
	intel_plane_free(plane);

	return ERR_PTR(ret);
}
