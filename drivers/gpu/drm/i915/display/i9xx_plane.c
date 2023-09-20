// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */
#include <linux/kernel.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_fourcc.h>

#include "i915_reg.h"
#include "i9xx_plane.h"
#include "intel_atomic.h"
#include "intel_atomic_plane.h"
#include "intel_de.h"
#include "intel_display_irq.h"
#include "intel_display_types.h"
#include "intel_fb.h"
#include "intel_fbc.h"
#include "intel_sprite.h"

/* Primary plane formats for gen <= 3 */
static const u32 i8xx_primary_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
};

/* Primary plane formats for ivb (no fp16 due to hw issue) */
static const u32 ivb_primary_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
};

/* Primary plane formats for gen >= 4, except ivb */
static const u32 i965_primary_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_XBGR16161616F,
};

/* Primary plane formats for vlv/chv */
static const u32 vlv_primary_formats[] = {
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
	DRM_FORMAT_XBGR16161616F,
};

static bool i8xx_plane_format_mod_supported(struct drm_plane *_plane,
					    u32 format, u64 modifier)
{
	if (!intel_fb_plane_supports_modifier(to_intel_plane(_plane), modifier))
		return false;

	switch (format) {
	case DRM_FORMAT_C8:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_XRGB8888:
		return modifier == DRM_FORMAT_MOD_LINEAR ||
			modifier == I915_FORMAT_MOD_X_TILED;
	default:
		return false;
	}
}

static bool i965_plane_format_mod_supported(struct drm_plane *_plane,
					    u32 format, u64 modifier)
{
	if (!intel_fb_plane_supports_modifier(to_intel_plane(_plane), modifier))
		return false;

	switch (format) {
	case DRM_FORMAT_C8:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_XBGR16161616F:
		return modifier == DRM_FORMAT_MOD_LINEAR ||
			modifier == I915_FORMAT_MOD_X_TILED;
	default:
		return false;
	}
}

static bool i9xx_plane_has_fbc(struct drm_i915_private *dev_priv,
			       enum i9xx_plane_id i9xx_plane)
{
	if (!HAS_FBC(dev_priv))
		return false;

	if (IS_BROADWELL(dev_priv) || IS_HASWELL(dev_priv))
		return i9xx_plane == PLANE_A; /* tied to pipe A */
	else if (IS_IVYBRIDGE(dev_priv))
		return i9xx_plane == PLANE_A || i9xx_plane == PLANE_B ||
			i9xx_plane == PLANE_C;
	else if (DISPLAY_VER(dev_priv) >= 4)
		return i9xx_plane == PLANE_A || i9xx_plane == PLANE_B;
	else
		return i9xx_plane == PLANE_A;
}

static struct intel_fbc *i9xx_plane_fbc(struct drm_i915_private *dev_priv,
					enum i9xx_plane_id i9xx_plane)
{
	if (i9xx_plane_has_fbc(dev_priv, i9xx_plane))
		return dev_priv->display.fbc[INTEL_FBC_A];
	else
		return NULL;
}

static bool i9xx_plane_has_windowing(struct intel_plane *plane)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum i9xx_plane_id i9xx_plane = plane->i9xx_plane;

	if (IS_CHERRYVIEW(dev_priv))
		return i9xx_plane == PLANE_B;
	else if (DISPLAY_VER(dev_priv) >= 5 || IS_G4X(dev_priv))
		return false;
	else if (DISPLAY_VER(dev_priv) == 4)
		return i9xx_plane == PLANE_C;
	else
		return i9xx_plane == PLANE_B ||
			i9xx_plane == PLANE_C;
}

static u32 i9xx_plane_ctl(const struct intel_crtc_state *crtc_state,
			  const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rotation = plane_state->hw.rotation;
	u32 dspcntr;

	dspcntr = DISP_ENABLE;

	if (IS_G4X(dev_priv) || IS_IRONLAKE(dev_priv) ||
	    IS_SANDYBRIDGE(dev_priv) || IS_IVYBRIDGE(dev_priv))
		dspcntr |= DISP_TRICKLE_FEED_DISABLE;

	switch (fb->format->format) {
	case DRM_FORMAT_C8:
		dspcntr |= DISP_FORMAT_8BPP;
		break;
	case DRM_FORMAT_XRGB1555:
		dspcntr |= DISP_FORMAT_BGRX555;
		break;
	case DRM_FORMAT_ARGB1555:
		dspcntr |= DISP_FORMAT_BGRA555;
		break;
	case DRM_FORMAT_RGB565:
		dspcntr |= DISP_FORMAT_BGRX565;
		break;
	case DRM_FORMAT_XRGB8888:
		dspcntr |= DISP_FORMAT_BGRX888;
		break;
	case DRM_FORMAT_XBGR8888:
		dspcntr |= DISP_FORMAT_RGBX888;
		break;
	case DRM_FORMAT_ARGB8888:
		dspcntr |= DISP_FORMAT_BGRA888;
		break;
	case DRM_FORMAT_ABGR8888:
		dspcntr |= DISP_FORMAT_RGBA888;
		break;
	case DRM_FORMAT_XRGB2101010:
		dspcntr |= DISP_FORMAT_BGRX101010;
		break;
	case DRM_FORMAT_XBGR2101010:
		dspcntr |= DISP_FORMAT_RGBX101010;
		break;
	case DRM_FORMAT_ARGB2101010:
		dspcntr |= DISP_FORMAT_BGRA101010;
		break;
	case DRM_FORMAT_ABGR2101010:
		dspcntr |= DISP_FORMAT_RGBA101010;
		break;
	case DRM_FORMAT_XBGR16161616F:
		dspcntr |= DISP_FORMAT_RGBX161616;
		break;
	default:
		MISSING_CASE(fb->format->format);
		return 0;
	}

	if (DISPLAY_VER(dev_priv) >= 4 &&
	    fb->modifier == I915_FORMAT_MOD_X_TILED)
		dspcntr |= DISP_TILED;

	if (rotation & DRM_MODE_ROTATE_180)
		dspcntr |= DISP_ROTATE_180;

	if (rotation & DRM_MODE_REFLECT_X)
		dspcntr |= DISP_MIRROR;

	return dspcntr;
}

int i9xx_check_plane_surface(struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	int src_x, src_y, src_w;
	u32 offset;
	int ret;

	ret = intel_plane_compute_gtt(plane_state);
	if (ret)
		return ret;

	if (!plane_state->uapi.visible)
		return 0;

	src_w = drm_rect_width(&plane_state->uapi.src) >> 16;
	src_x = plane_state->uapi.src.x1 >> 16;
	src_y = plane_state->uapi.src.y1 >> 16;

	/* Undocumented hardware limit on i965/g4x/vlv/chv */
	if (HAS_GMCH(dev_priv) && fb->format->cpp[0] == 8 && src_w > 2048)
		return -EINVAL;

	intel_add_fb_offsets(&src_x, &src_y, plane_state, 0);

	if (DISPLAY_VER(dev_priv) >= 4)
		offset = intel_plane_compute_aligned_offset(&src_x, &src_y,
							    plane_state, 0);
	else
		offset = 0;

	/*
	 * When using an X-tiled surface the plane starts to
	 * misbehave if the x offset + width exceeds the stride.
	 * hsw/bdw: underrun galore
	 * ilk/snb/ivb: wrap to the next tile row mid scanout
	 * i965/g4x: so far appear immune to this
	 * vlv/chv: TODO check
	 *
	 * Linear surfaces seem to work just fine, even on hsw/bdw
	 * despite them not using the linear offset anymore.
	 */
	if (DISPLAY_VER(dev_priv) >= 4 && fb->modifier == I915_FORMAT_MOD_X_TILED) {
		u32 alignment = intel_surf_alignment(fb, 0);
		int cpp = fb->format->cpp[0];

		while ((src_x + src_w) * cpp > plane_state->view.color_plane[0].mapping_stride) {
			if (offset == 0) {
				drm_dbg_kms(&dev_priv->drm,
					    "Unable to find suitable display surface offset due to X-tiling\n");
				return -EINVAL;
			}

			offset = intel_plane_adjust_aligned_offset(&src_x, &src_y, plane_state, 0,
								   offset, offset - alignment);
		}
	}

	/*
	 * Put the final coordinates back so that the src
	 * coordinate checks will see the right values.
	 */
	drm_rect_translate_to(&plane_state->uapi.src,
			      src_x << 16, src_y << 16);

	/* HSW/BDW do this automagically in hardware */
	if (!IS_HASWELL(dev_priv) && !IS_BROADWELL(dev_priv)) {
		unsigned int rotation = plane_state->hw.rotation;
		int src_w = drm_rect_width(&plane_state->uapi.src) >> 16;
		int src_h = drm_rect_height(&plane_state->uapi.src) >> 16;

		if (rotation & DRM_MODE_ROTATE_180) {
			src_x += src_w - 1;
			src_y += src_h - 1;
		} else if (rotation & DRM_MODE_REFLECT_X) {
			src_x += src_w - 1;
		}
	}

	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		drm_WARN_ON(&dev_priv->drm, src_x > 8191 || src_y > 4095);
	} else if (DISPLAY_VER(dev_priv) >= 4 &&
		   fb->modifier == I915_FORMAT_MOD_X_TILED) {
		drm_WARN_ON(&dev_priv->drm, src_x > 4095 || src_y > 4095);
	}

	plane_state->view.color_plane[0].offset = offset;
	plane_state->view.color_plane[0].x = src_x;
	plane_state->view.color_plane[0].y = src_y;

	return 0;
}

static int
i9xx_plane_check(struct intel_crtc_state *crtc_state,
		 struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	int ret;

	ret = chv_plane_check_rotation(plane_state);
	if (ret)
		return ret;

	ret = intel_atomic_plane_check_clipping(plane_state, crtc_state,
						DRM_PLANE_NO_SCALING,
						DRM_PLANE_NO_SCALING,
						i9xx_plane_has_windowing(plane));
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

	plane_state->ctl = i9xx_plane_ctl(crtc_state, plane_state);

	return 0;
}

static u32 i9xx_plane_ctl_crtc(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 dspcntr = 0;

	if (crtc_state->gamma_enable)
		dspcntr |= DISP_PIPE_GAMMA_ENABLE;

	if (crtc_state->csc_enable)
		dspcntr |= DISP_PIPE_CSC_ENABLE;

	if (DISPLAY_VER(dev_priv) < 5)
		dspcntr |= DISP_PIPE_SEL(crtc->pipe);

	return dspcntr;
}

static void i9xx_plane_ratio(const struct intel_crtc_state *crtc_state,
			     const struct intel_plane_state *plane_state,
			     unsigned int *num, unsigned int *den)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int cpp = fb->format->cpp[0];

	/*
	 * g4x bspec says 64bpp pixel rate can't exceed 80%
	 * of cdclk when the sprite plane is enabled on the
	 * same pipe. ilk/snb bspec says 64bpp pixel rate is
	 * never allowed to exceed 80% of cdclk. Let's just go
	 * with the ilk/snb limit always.
	 */
	if (cpp == 8) {
		*num = 10;
		*den = 8;
	} else {
		*num = 1;
		*den = 1;
	}
}

static int i9xx_plane_min_cdclk(const struct intel_crtc_state *crtc_state,
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

	i9xx_plane_ratio(crtc_state, plane_state, &num, &den);

	/* two pixels per clock with double wide pipe */
	if (crtc_state->double_wide)
		den *= 2;

	return DIV_ROUND_UP(pixel_rate * num, den);
}

static void i9xx_plane_update_noarm(struct intel_plane *plane,
				    const struct intel_crtc_state *crtc_state,
				    const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum i9xx_plane_id i9xx_plane = plane->i9xx_plane;

	intel_de_write_fw(dev_priv, DSPSTRIDE(i9xx_plane),
			  plane_state->view.color_plane[0].mapping_stride);

	if (DISPLAY_VER(dev_priv) < 4) {
		int crtc_x = plane_state->uapi.dst.x1;
		int crtc_y = plane_state->uapi.dst.y1;
		int crtc_w = drm_rect_width(&plane_state->uapi.dst);
		int crtc_h = drm_rect_height(&plane_state->uapi.dst);

		/*
		 * PLANE_A doesn't actually have a full window
		 * generator but let's assume we still need to
		 * program whatever is there.
		 */
		intel_de_write_fw(dev_priv, DSPPOS(i9xx_plane),
				  DISP_POS_Y(crtc_y) | DISP_POS_X(crtc_x));
		intel_de_write_fw(dev_priv, DSPSIZE(i9xx_plane),
				  DISP_HEIGHT(crtc_h - 1) | DISP_WIDTH(crtc_w - 1));
	}
}

static void i9xx_plane_update_arm(struct intel_plane *plane,
				  const struct intel_crtc_state *crtc_state,
				  const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum i9xx_plane_id i9xx_plane = plane->i9xx_plane;
	int x = plane_state->view.color_plane[0].x;
	int y = plane_state->view.color_plane[0].y;
	u32 dspcntr, dspaddr_offset, linear_offset;

	dspcntr = plane_state->ctl | i9xx_plane_ctl_crtc(crtc_state);

	linear_offset = intel_fb_xy_to_linear(x, y, plane_state, 0);

	if (DISPLAY_VER(dev_priv) >= 4)
		dspaddr_offset = plane_state->view.color_plane[0].offset;
	else
		dspaddr_offset = linear_offset;

	if (IS_CHERRYVIEW(dev_priv) && i9xx_plane == PLANE_B) {
		int crtc_x = plane_state->uapi.dst.x1;
		int crtc_y = plane_state->uapi.dst.y1;
		int crtc_w = drm_rect_width(&plane_state->uapi.dst);
		int crtc_h = drm_rect_height(&plane_state->uapi.dst);

		intel_de_write_fw(dev_priv, PRIMPOS(i9xx_plane),
				  PRIM_POS_Y(crtc_y) | PRIM_POS_X(crtc_x));
		intel_de_write_fw(dev_priv, PRIMSIZE(i9xx_plane),
				  PRIM_HEIGHT(crtc_h - 1) | PRIM_WIDTH(crtc_w - 1));
		intel_de_write_fw(dev_priv, PRIMCNSTALPHA(i9xx_plane), 0);
	}

	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		intel_de_write_fw(dev_priv, DSPOFFSET(i9xx_plane),
				  DISP_OFFSET_Y(y) | DISP_OFFSET_X(x));
	} else if (DISPLAY_VER(dev_priv) >= 4) {
		intel_de_write_fw(dev_priv, DSPLINOFF(i9xx_plane),
				  linear_offset);
		intel_de_write_fw(dev_priv, DSPTILEOFF(i9xx_plane),
				  DISP_OFFSET_Y(y) | DISP_OFFSET_X(x));
	}

	/*
	 * The control register self-arms if the plane was previously
	 * disabled. Try to make the plane enable atomic by writing
	 * the control register just before the surface register.
	 */
	intel_de_write_fw(dev_priv, DSPCNTR(i9xx_plane), dspcntr);

	if (DISPLAY_VER(dev_priv) >= 4)
		intel_de_write_fw(dev_priv, DSPSURF(i9xx_plane),
				  intel_plane_ggtt_offset(plane_state) + dspaddr_offset);
	else
		intel_de_write_fw(dev_priv, DSPADDR(i9xx_plane),
				  intel_plane_ggtt_offset(plane_state) + dspaddr_offset);
}

static void i830_plane_update_arm(struct intel_plane *plane,
				  const struct intel_crtc_state *crtc_state,
				  const struct intel_plane_state *plane_state)
{
	/*
	 * On i830/i845 all registers are self-arming [ALM040].
	 *
	 * Additional breakage on i830 causes register reads to return
	 * the last latched value instead of the last written value [ALM026].
	 */
	i9xx_plane_update_noarm(plane, crtc_state, plane_state);
	i9xx_plane_update_arm(plane, crtc_state, plane_state);
}

static void i9xx_plane_disable_arm(struct intel_plane *plane,
				   const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum i9xx_plane_id i9xx_plane = plane->i9xx_plane;
	u32 dspcntr;

	/*
	 * DSPCNTR pipe gamma enable on g4x+ and pipe csc
	 * enable on ilk+ affect the pipe bottom color as
	 * well, so we must configure them even if the plane
	 * is disabled.
	 *
	 * On pre-g4x there is no way to gamma correct the
	 * pipe bottom color but we'll keep on doing this
	 * anyway so that the crtc state readout works correctly.
	 */
	dspcntr = i9xx_plane_ctl_crtc(crtc_state);

	intel_de_write_fw(dev_priv, DSPCNTR(i9xx_plane), dspcntr);

	if (DISPLAY_VER(dev_priv) >= 4)
		intel_de_write_fw(dev_priv, DSPSURF(i9xx_plane), 0);
	else
		intel_de_write_fw(dev_priv, DSPADDR(i9xx_plane), 0);
}

static void
g4x_primary_async_flip(struct intel_plane *plane,
		       const struct intel_crtc_state *crtc_state,
		       const struct intel_plane_state *plane_state,
		       bool async_flip)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	u32 dspcntr = plane_state->ctl | i9xx_plane_ctl_crtc(crtc_state);
	u32 dspaddr_offset = plane_state->view.color_plane[0].offset;
	enum i9xx_plane_id i9xx_plane = plane->i9xx_plane;

	if (async_flip)
		dspcntr |= DISP_ASYNC_FLIP;

	intel_de_write_fw(dev_priv, DSPCNTR(i9xx_plane), dspcntr);

	intel_de_write_fw(dev_priv, DSPSURF(i9xx_plane),
			  intel_plane_ggtt_offset(plane_state) + dspaddr_offset);
}

static void
vlv_primary_async_flip(struct intel_plane *plane,
		       const struct intel_crtc_state *crtc_state,
		       const struct intel_plane_state *plane_state,
		       bool async_flip)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	u32 dspaddr_offset = plane_state->view.color_plane[0].offset;
	enum i9xx_plane_id i9xx_plane = plane->i9xx_plane;

	intel_de_write_fw(dev_priv, DSPADDR_VLV(i9xx_plane),
			  intel_plane_ggtt_offset(plane_state) + dspaddr_offset);
}

static void
bdw_primary_enable_flip_done(struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;

	spin_lock_irq(&i915->irq_lock);
	bdw_enable_pipe_irq(i915, pipe, GEN8_PIPE_PRIMARY_FLIP_DONE);
	spin_unlock_irq(&i915->irq_lock);
}

static void
bdw_primary_disable_flip_done(struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;

	spin_lock_irq(&i915->irq_lock);
	bdw_disable_pipe_irq(i915, pipe, GEN8_PIPE_PRIMARY_FLIP_DONE);
	spin_unlock_irq(&i915->irq_lock);
}

static void
ivb_primary_enable_flip_done(struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);

	spin_lock_irq(&i915->irq_lock);
	ilk_enable_display_irq(i915, DE_PLANE_FLIP_DONE_IVB(plane->i9xx_plane));
	spin_unlock_irq(&i915->irq_lock);
}

static void
ivb_primary_disable_flip_done(struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);

	spin_lock_irq(&i915->irq_lock);
	ilk_disable_display_irq(i915, DE_PLANE_FLIP_DONE_IVB(plane->i9xx_plane));
	spin_unlock_irq(&i915->irq_lock);
}

static void
ilk_primary_enable_flip_done(struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);

	spin_lock_irq(&i915->irq_lock);
	ilk_enable_display_irq(i915, DE_PLANE_FLIP_DONE(plane->i9xx_plane));
	spin_unlock_irq(&i915->irq_lock);
}

static void
ilk_primary_disable_flip_done(struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);

	spin_lock_irq(&i915->irq_lock);
	ilk_disable_display_irq(i915, DE_PLANE_FLIP_DONE(plane->i9xx_plane));
	spin_unlock_irq(&i915->irq_lock);
}

static void
vlv_primary_enable_flip_done(struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;

	spin_lock_irq(&i915->irq_lock);
	i915_enable_pipestat(i915, pipe, PLANE_FLIP_DONE_INT_STATUS_VLV);
	spin_unlock_irq(&i915->irq_lock);
}

static void
vlv_primary_disable_flip_done(struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;

	spin_lock_irq(&i915->irq_lock);
	i915_disable_pipestat(i915, pipe, PLANE_FLIP_DONE_INT_STATUS_VLV);
	spin_unlock_irq(&i915->irq_lock);
}

static bool i9xx_plane_get_hw_state(struct intel_plane *plane,
				    enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum intel_display_power_domain power_domain;
	enum i9xx_plane_id i9xx_plane = plane->i9xx_plane;
	intel_wakeref_t wakeref;
	bool ret;
	u32 val;

	/*
	 * Not 100% correct for planes that can move between pipes,
	 * but that's only the case for gen2-4 which don't have any
	 * display power wells.
	 */
	power_domain = POWER_DOMAIN_PIPE(plane->pipe);
	wakeref = intel_display_power_get_if_enabled(dev_priv, power_domain);
	if (!wakeref)
		return false;

	val = intel_de_read(dev_priv, DSPCNTR(i9xx_plane));

	ret = val & DISP_ENABLE;

	if (DISPLAY_VER(dev_priv) >= 5)
		*pipe = plane->pipe;
	else
		*pipe = REG_FIELD_GET(DISP_PIPE_SEL_MASK, val);

	intel_display_power_put(dev_priv, power_domain, wakeref);

	return ret;
}

static unsigned int
hsw_primary_max_stride(struct intel_plane *plane,
		       u32 pixel_format, u64 modifier,
		       unsigned int rotation)
{
	const struct drm_format_info *info = drm_format_info(pixel_format);
	int cpp = info->cpp[0];

	/* Limit to 8k pixels to guarantee OFFSET.x doesn't get too big. */
	return min(8192 * cpp, 32 * 1024);
}

static unsigned int
ilk_primary_max_stride(struct intel_plane *plane,
		       u32 pixel_format, u64 modifier,
		       unsigned int rotation)
{
	const struct drm_format_info *info = drm_format_info(pixel_format);
	int cpp = info->cpp[0];

	/* Limit to 4k pixels to guarantee TILEOFF.x doesn't get too big. */
	if (modifier == I915_FORMAT_MOD_X_TILED)
		return min(4096 * cpp, 32 * 1024);
	else
		return 32 * 1024;
}

unsigned int
i965_plane_max_stride(struct intel_plane *plane,
		      u32 pixel_format, u64 modifier,
		      unsigned int rotation)
{
	const struct drm_format_info *info = drm_format_info(pixel_format);
	int cpp = info->cpp[0];

	/* Limit to 4k pixels to guarantee TILEOFF.x doesn't get too big. */
	if (modifier == I915_FORMAT_MOD_X_TILED)
		return min(4096 * cpp, 16 * 1024);
	else
		return 32 * 1024;
}

static unsigned int
i9xx_plane_max_stride(struct intel_plane *plane,
		      u32 pixel_format, u64 modifier,
		      unsigned int rotation)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);

	if (DISPLAY_VER(dev_priv) >= 3) {
		if (modifier == I915_FORMAT_MOD_X_TILED)
			return 8*1024;
		else
			return 16*1024;
	} else {
		if (plane->i9xx_plane == PLANE_C)
			return 4*1024;
		else
			return 8*1024;
	}
}

static const struct drm_plane_funcs i965_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = intel_plane_destroy,
	.atomic_duplicate_state = intel_plane_duplicate_state,
	.atomic_destroy_state = intel_plane_destroy_state,
	.format_mod_supported = i965_plane_format_mod_supported,
};

static const struct drm_plane_funcs i8xx_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = intel_plane_destroy,
	.atomic_duplicate_state = intel_plane_duplicate_state,
	.atomic_destroy_state = intel_plane_destroy_state,
	.format_mod_supported = i8xx_plane_format_mod_supported,
};

struct intel_plane *
intel_primary_plane_create(struct drm_i915_private *dev_priv, enum pipe pipe)
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

	plane->pipe = pipe;
	/*
	 * On gen2/3 only plane A can do FBC, but the panel fitter and LVDS
	 * port is hooked to pipe B. Hence we want plane A feeding pipe B.
	 */
	if (HAS_FBC(dev_priv) && DISPLAY_VER(dev_priv) < 4 &&
	    INTEL_NUM_PIPES(dev_priv) == 2)
		plane->i9xx_plane = (enum i9xx_plane_id) !pipe;
	else
		plane->i9xx_plane = (enum i9xx_plane_id) pipe;
	plane->id = PLANE_PRIMARY;
	plane->frontbuffer_bit = INTEL_FRONTBUFFER(pipe, plane->id);

	intel_fbc_add_plane(i9xx_plane_fbc(dev_priv, plane->i9xx_plane), plane);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		formats = vlv_primary_formats;
		num_formats = ARRAY_SIZE(vlv_primary_formats);
	} else if (DISPLAY_VER(dev_priv) >= 4) {
		/*
		 * WaFP16GammaEnabling:ivb
		 * "Workaround : When using the 64-bit format, the plane
		 *  output on each color channel has one quarter amplitude.
		 *  It can be brought up to full amplitude by using pipe
		 *  gamma correction or pipe color space conversion to
		 *  multiply the plane output by four."
		 *
		 * There is no dedicated plane gamma for the primary plane,
		 * and using the pipe gamma/csc could conflict with other
		 * planes, so we choose not to expose fp16 on IVB primary
		 * planes. HSW primary planes no longer have this problem.
		 */
		if (IS_IVYBRIDGE(dev_priv)) {
			formats = ivb_primary_formats;
			num_formats = ARRAY_SIZE(ivb_primary_formats);
		} else {
			formats = i965_primary_formats;
			num_formats = ARRAY_SIZE(i965_primary_formats);
		}
	} else {
		formats = i8xx_primary_formats;
		num_formats = ARRAY_SIZE(i8xx_primary_formats);
	}

	if (DISPLAY_VER(dev_priv) >= 4)
		plane_funcs = &i965_plane_funcs;
	else
		plane_funcs = &i8xx_plane_funcs;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		plane->min_cdclk = vlv_plane_min_cdclk;
	else if (IS_BROADWELL(dev_priv) || IS_HASWELL(dev_priv))
		plane->min_cdclk = hsw_plane_min_cdclk;
	else if (IS_IVYBRIDGE(dev_priv))
		plane->min_cdclk = ivb_plane_min_cdclk;
	else
		plane->min_cdclk = i9xx_plane_min_cdclk;

	if (HAS_GMCH(dev_priv)) {
		if (DISPLAY_VER(dev_priv) >= 4)
			plane->max_stride = i965_plane_max_stride;
		else
			plane->max_stride = i9xx_plane_max_stride;
	} else {
		if (IS_BROADWELL(dev_priv) || IS_HASWELL(dev_priv))
			plane->max_stride = hsw_primary_max_stride;
		else
			plane->max_stride = ilk_primary_max_stride;
	}

	if (IS_I830(dev_priv) || IS_I845G(dev_priv)) {
		plane->update_arm = i830_plane_update_arm;
	} else {
		plane->update_noarm = i9xx_plane_update_noarm;
		plane->update_arm = i9xx_plane_update_arm;
	}
	plane->disable_arm = i9xx_plane_disable_arm;
	plane->get_hw_state = i9xx_plane_get_hw_state;
	plane->check_plane = i9xx_plane_check;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		plane->async_flip = vlv_primary_async_flip;
		plane->enable_flip_done = vlv_primary_enable_flip_done;
		plane->disable_flip_done = vlv_primary_disable_flip_done;
	} else if (IS_BROADWELL(dev_priv)) {
		plane->need_async_flip_disable_wa = true;
		plane->async_flip = g4x_primary_async_flip;
		plane->enable_flip_done = bdw_primary_enable_flip_done;
		plane->disable_flip_done = bdw_primary_disable_flip_done;
	} else if (DISPLAY_VER(dev_priv) >= 7) {
		plane->async_flip = g4x_primary_async_flip;
		plane->enable_flip_done = ivb_primary_enable_flip_done;
		plane->disable_flip_done = ivb_primary_disable_flip_done;
	} else if (DISPLAY_VER(dev_priv) >= 5) {
		plane->async_flip = g4x_primary_async_flip;
		plane->enable_flip_done = ilk_primary_enable_flip_done;
		plane->disable_flip_done = ilk_primary_disable_flip_done;
	}

	modifiers = intel_fb_plane_get_modifiers(dev_priv, INTEL_PLANE_CAP_TILING_X);

	if (DISPLAY_VER(dev_priv) >= 5 || IS_G4X(dev_priv))
		ret = drm_universal_plane_init(&dev_priv->drm, &plane->base,
					       0, plane_funcs,
					       formats, num_formats,
					       modifiers,
					       DRM_PLANE_TYPE_PRIMARY,
					       "primary %c", pipe_name(pipe));
	else
		ret = drm_universal_plane_init(&dev_priv->drm, &plane->base,
					       0, plane_funcs,
					       formats, num_formats,
					       modifiers,
					       DRM_PLANE_TYPE_PRIMARY,
					       "plane %c",
					       plane_name(plane->i9xx_plane));

	kfree(modifiers);

	if (ret)
		goto fail;

	if (IS_CHERRYVIEW(dev_priv) && pipe == PIPE_B) {
		supported_rotations =
			DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_180 |
			DRM_MODE_REFLECT_X;
	} else if (DISPLAY_VER(dev_priv) >= 4) {
		supported_rotations =
			DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_180;
	} else {
		supported_rotations = DRM_MODE_ROTATE_0;
	}

	if (DISPLAY_VER(dev_priv) >= 4)
		drm_plane_create_rotation_property(&plane->base,
						   DRM_MODE_ROTATE_0,
						   supported_rotations);

	zpos = 0;
	drm_plane_create_zpos_immutable_property(&plane->base, zpos);

	intel_plane_helper_add(plane);

	return plane;

fail:
	intel_plane_free(plane);

	return ERR_PTR(ret);
}

static int i9xx_format_to_fourcc(int format)
{
	switch (format) {
	case DISP_FORMAT_8BPP:
		return DRM_FORMAT_C8;
	case DISP_FORMAT_BGRA555:
		return DRM_FORMAT_ARGB1555;
	case DISP_FORMAT_BGRX555:
		return DRM_FORMAT_XRGB1555;
	case DISP_FORMAT_BGRX565:
		return DRM_FORMAT_RGB565;
	default:
	case DISP_FORMAT_BGRX888:
		return DRM_FORMAT_XRGB8888;
	case DISP_FORMAT_RGBX888:
		return DRM_FORMAT_XBGR8888;
	case DISP_FORMAT_BGRA888:
		return DRM_FORMAT_ARGB8888;
	case DISP_FORMAT_RGBA888:
		return DRM_FORMAT_ABGR8888;
	case DISP_FORMAT_BGRX101010:
		return DRM_FORMAT_XRGB2101010;
	case DISP_FORMAT_RGBX101010:
		return DRM_FORMAT_XBGR2101010;
	case DISP_FORMAT_BGRA101010:
		return DRM_FORMAT_ARGB2101010;
	case DISP_FORMAT_RGBA101010:
		return DRM_FORMAT_ABGR2101010;
	case DISP_FORMAT_RGBX161616:
		return DRM_FORMAT_XBGR16161616F;
	}
}

void
i9xx_get_initial_plane_config(struct intel_crtc *crtc,
			      struct intel_initial_plane_config *plane_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_plane *plane = to_intel_plane(crtc->base.primary);
	enum i9xx_plane_id i9xx_plane = plane->i9xx_plane;
	enum pipe pipe;
	u32 val, base, offset;
	int fourcc, pixel_format;
	unsigned int aligned_height;
	struct drm_framebuffer *fb;
	struct intel_framebuffer *intel_fb;

	if (!plane->get_hw_state(plane, &pipe))
		return;

	drm_WARN_ON(dev, pipe != crtc->pipe);

	intel_fb = kzalloc(sizeof(*intel_fb), GFP_KERNEL);
	if (!intel_fb) {
		drm_dbg_kms(&dev_priv->drm, "failed to alloc fb\n");
		return;
	}

	fb = &intel_fb->base;

	fb->dev = dev;

	val = intel_de_read(dev_priv, DSPCNTR(i9xx_plane));

	if (DISPLAY_VER(dev_priv) >= 4) {
		if (val & DISP_TILED) {
			plane_config->tiling = I915_TILING_X;
			fb->modifier = I915_FORMAT_MOD_X_TILED;
		}

		if (val & DISP_ROTATE_180)
			plane_config->rotation = DRM_MODE_ROTATE_180;
	}

	if (IS_CHERRYVIEW(dev_priv) && pipe == PIPE_B &&
	    val & DISP_MIRROR)
		plane_config->rotation |= DRM_MODE_REFLECT_X;

	pixel_format = val & DISP_FORMAT_MASK;
	fourcc = i9xx_format_to_fourcc(pixel_format);
	fb->format = drm_format_info(fourcc);

	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		offset = intel_de_read(dev_priv, DSPOFFSET(i9xx_plane));
		base = intel_de_read(dev_priv, DSPSURF(i9xx_plane)) & DISP_ADDR_MASK;
	} else if (DISPLAY_VER(dev_priv) >= 4) {
		if (plane_config->tiling)
			offset = intel_de_read(dev_priv,
					       DSPTILEOFF(i9xx_plane));
		else
			offset = intel_de_read(dev_priv,
					       DSPLINOFF(i9xx_plane));
		base = intel_de_read(dev_priv, DSPSURF(i9xx_plane)) & DISP_ADDR_MASK;
	} else {
		offset = 0;
		base = intel_de_read(dev_priv, DSPADDR(i9xx_plane));
	}
	plane_config->base = base;

	drm_WARN_ON(&dev_priv->drm, offset != 0);

	val = intel_de_read(dev_priv, PIPESRC(pipe));
	fb->width = REG_FIELD_GET(PIPESRC_WIDTH_MASK, val) + 1;
	fb->height = REG_FIELD_GET(PIPESRC_HEIGHT_MASK, val) + 1;

	val = intel_de_read(dev_priv, DSPSTRIDE(i9xx_plane));
	fb->pitches[0] = val & 0xffffffc0;

	aligned_height = intel_fb_align_height(fb, 0, fb->height);

	plane_config->size = fb->pitches[0] * aligned_height;

	drm_dbg_kms(&dev_priv->drm,
		    "%s/%s with fb: size=%dx%d@%d, offset=%x, pitch %d, size 0x%x\n",
		    crtc->base.name, plane->base.name, fb->width, fb->height,
		    fb->format->cpp[0] * 8, base, fb->pitches[0],
		    plane_config->size);

	plane_config->fb = intel_fb;
}
