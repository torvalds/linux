// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */
#include <linux/kernel.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_plane_helper.h>

#include "intel_atomic.h"
#include "intel_atomic_plane.h"
#include "intel_display_types.h"
#include "intel_sprite.h"
#include "i9xx_plane.h"

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

static const u64 i9xx_format_modifiers[] = {
	I915_FORMAT_MOD_X_TILED,
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static bool i8xx_plane_format_mod_supported(struct drm_plane *_plane,
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
	else if (INTEL_GEN(dev_priv) >= 4)
		return i9xx_plane == PLANE_A || i9xx_plane == PLANE_B;
	else
		return i9xx_plane == PLANE_A;
}

static bool i9xx_plane_has_windowing(struct intel_plane *plane)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum i9xx_plane_id i9xx_plane = plane->i9xx_plane;

	if (IS_CHERRYVIEW(dev_priv))
		return i9xx_plane == PLANE_B;
	else if (INTEL_GEN(dev_priv) >= 5 || IS_G4X(dev_priv))
		return false;
	else if (IS_GEN(dev_priv, 4))
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

	dspcntr = DISPLAY_PLANE_ENABLE;

	if (IS_G4X(dev_priv) || IS_GEN(dev_priv, 5) ||
	    IS_GEN(dev_priv, 6) || IS_IVYBRIDGE(dev_priv))
		dspcntr |= DISPPLANE_TRICKLE_FEED_DISABLE;

	switch (fb->format->format) {
	case DRM_FORMAT_C8:
		dspcntr |= DISPPLANE_8BPP;
		break;
	case DRM_FORMAT_XRGB1555:
		dspcntr |= DISPPLANE_BGRX555;
		break;
	case DRM_FORMAT_ARGB1555:
		dspcntr |= DISPPLANE_BGRA555;
		break;
	case DRM_FORMAT_RGB565:
		dspcntr |= DISPPLANE_BGRX565;
		break;
	case DRM_FORMAT_XRGB8888:
		dspcntr |= DISPPLANE_BGRX888;
		break;
	case DRM_FORMAT_XBGR8888:
		dspcntr |= DISPPLANE_RGBX888;
		break;
	case DRM_FORMAT_ARGB8888:
		dspcntr |= DISPPLANE_BGRA888;
		break;
	case DRM_FORMAT_ABGR8888:
		dspcntr |= DISPPLANE_RGBA888;
		break;
	case DRM_FORMAT_XRGB2101010:
		dspcntr |= DISPPLANE_BGRX101010;
		break;
	case DRM_FORMAT_XBGR2101010:
		dspcntr |= DISPPLANE_RGBX101010;
		break;
	case DRM_FORMAT_ARGB2101010:
		dspcntr |= DISPPLANE_BGRA101010;
		break;
	case DRM_FORMAT_ABGR2101010:
		dspcntr |= DISPPLANE_RGBA101010;
		break;
	case DRM_FORMAT_XBGR16161616F:
		dspcntr |= DISPPLANE_RGBX161616;
		break;
	default:
		MISSING_CASE(fb->format->format);
		return 0;
	}

	if (INTEL_GEN(dev_priv) >= 4 &&
	    fb->modifier == I915_FORMAT_MOD_X_TILED)
		dspcntr |= DISPPLANE_TILED;

	if (rotation & DRM_MODE_ROTATE_180)
		dspcntr |= DISPPLANE_ROTATE_180;

	if (rotation & DRM_MODE_REFLECT_X)
		dspcntr |= DISPPLANE_MIRROR;

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

	if (INTEL_GEN(dev_priv) >= 4)
		offset = intel_plane_compute_aligned_offset(&src_x, &src_y,
							    plane_state, 0);
	else
		offset = 0;

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

	plane_state->color_plane[0].offset = offset;
	plane_state->color_plane[0].x = src_x;
	plane_state->color_plane[0].y = src_y;

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
						DRM_PLANE_HELPER_NO_SCALING,
						DRM_PLANE_HELPER_NO_SCALING,
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
		dspcntr |= DISPPLANE_GAMMA_ENABLE;

	if (crtc_state->csc_enable)
		dspcntr |= DISPPLANE_PIPE_CSC_ENABLE;

	if (INTEL_GEN(dev_priv) < 5)
		dspcntr |= DISPPLANE_SEL_PIPE(crtc->pipe);

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

static void i9xx_update_plane(struct intel_plane *plane,
			      const struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum i9xx_plane_id i9xx_plane = plane->i9xx_plane;
	u32 linear_offset;
	int x = plane_state->color_plane[0].x;
	int y = plane_state->color_plane[0].y;
	int crtc_x = plane_state->uapi.dst.x1;
	int crtc_y = plane_state->uapi.dst.y1;
	int crtc_w = drm_rect_width(&plane_state->uapi.dst);
	int crtc_h = drm_rect_height(&plane_state->uapi.dst);
	unsigned long irqflags;
	u32 dspaddr_offset;
	u32 dspcntr;

	dspcntr = plane_state->ctl | i9xx_plane_ctl_crtc(crtc_state);

	linear_offset = intel_fb_xy_to_linear(x, y, plane_state, 0);

	if (INTEL_GEN(dev_priv) >= 4)
		dspaddr_offset = plane_state->color_plane[0].offset;
	else
		dspaddr_offset = linear_offset;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	intel_de_write_fw(dev_priv, DSPSTRIDE(i9xx_plane),
			  plane_state->color_plane[0].stride);

	if (INTEL_GEN(dev_priv) < 4) {
		/*
		 * PLANE_A doesn't actually have a full window
		 * generator but let's assume we still need to
		 * program whatever is there.
		 */
		intel_de_write_fw(dev_priv, DSPPOS(i9xx_plane),
				  (crtc_y << 16) | crtc_x);
		intel_de_write_fw(dev_priv, DSPSIZE(i9xx_plane),
				  ((crtc_h - 1) << 16) | (crtc_w - 1));
	} else if (IS_CHERRYVIEW(dev_priv) && i9xx_plane == PLANE_B) {
		intel_de_write_fw(dev_priv, PRIMPOS(i9xx_plane),
				  (crtc_y << 16) | crtc_x);
		intel_de_write_fw(dev_priv, PRIMSIZE(i9xx_plane),
				  ((crtc_h - 1) << 16) | (crtc_w - 1));
		intel_de_write_fw(dev_priv, PRIMCNSTALPHA(i9xx_plane), 0);
	}

	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		intel_de_write_fw(dev_priv, DSPOFFSET(i9xx_plane),
				  (y << 16) | x);
	} else if (INTEL_GEN(dev_priv) >= 4) {
		intel_de_write_fw(dev_priv, DSPLINOFF(i9xx_plane),
				  linear_offset);
		intel_de_write_fw(dev_priv, DSPTILEOFF(i9xx_plane),
				  (y << 16) | x);
	}

	/*
	 * The control register self-arms if the plane was previously
	 * disabled. Try to make the plane enable atomic by writing
	 * the control register just before the surface register.
	 */
	intel_de_write_fw(dev_priv, DSPCNTR(i9xx_plane), dspcntr);
	if (INTEL_GEN(dev_priv) >= 4)
		intel_de_write_fw(dev_priv, DSPSURF(i9xx_plane),
				  intel_plane_ggtt_offset(plane_state) + dspaddr_offset);
	else
		intel_de_write_fw(dev_priv, DSPADDR(i9xx_plane),
				  intel_plane_ggtt_offset(plane_state) + dspaddr_offset);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static void i9xx_disable_plane(struct intel_plane *plane,
			       const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum i9xx_plane_id i9xx_plane = plane->i9xx_plane;
	unsigned long irqflags;
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

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	intel_de_write_fw(dev_priv, DSPCNTR(i9xx_plane), dspcntr);
	if (INTEL_GEN(dev_priv) >= 4)
		intel_de_write_fw(dev_priv, DSPSURF(i9xx_plane), 0);
	else
		intel_de_write_fw(dev_priv, DSPADDR(i9xx_plane), 0);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
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

	ret = val & DISPLAY_PLANE_ENABLE;

	if (INTEL_GEN(dev_priv) >= 5)
		*pipe = plane->pipe;
	else
		*pipe = (val & DISPPLANE_SEL_PIPE_MASK) >>
			DISPPLANE_SEL_PIPE_SHIFT;

	intel_display_power_put(dev_priv, power_domain, wakeref);

	return ret;
}

unsigned int
i9xx_plane_max_stride(struct intel_plane *plane,
		      u32 pixel_format, u64 modifier,
		      unsigned int rotation)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);

	if (!HAS_GMCH(dev_priv)) {
		return 32*1024;
	} else if (INTEL_GEN(dev_priv) >= 4) {
		if (modifier == I915_FORMAT_MOD_X_TILED)
			return 16*1024;
		else
			return 32*1024;
	} else if (INTEL_GEN(dev_priv) >= 3) {
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
	const u32 *formats;
	int num_formats;
	int ret, zpos;

	if (INTEL_GEN(dev_priv) >= 9)
		return skl_universal_plane_create(dev_priv, pipe,
						  PLANE_PRIMARY);

	plane = intel_plane_alloc();
	if (IS_ERR(plane))
		return plane;

	plane->pipe = pipe;
	/*
	 * On gen2/3 only plane A can do FBC, but the panel fitter and LVDS
	 * port is hooked to pipe B. Hence we want plane A feeding pipe B.
	 */
	if (HAS_FBC(dev_priv) && INTEL_GEN(dev_priv) < 4 &&
	    INTEL_NUM_PIPES(dev_priv) == 2)
		plane->i9xx_plane = (enum i9xx_plane_id) !pipe;
	else
		plane->i9xx_plane = (enum i9xx_plane_id) pipe;
	plane->id = PLANE_PRIMARY;
	plane->frontbuffer_bit = INTEL_FRONTBUFFER(pipe, plane->id);

	plane->has_fbc = i9xx_plane_has_fbc(dev_priv, plane->i9xx_plane);
	if (plane->has_fbc) {
		struct intel_fbc *fbc = &dev_priv->fbc;

		fbc->possible_framebuffer_bits |= plane->frontbuffer_bit;
	}

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		formats = vlv_primary_formats;
		num_formats = ARRAY_SIZE(vlv_primary_formats);
	} else if (INTEL_GEN(dev_priv) >= 4) {
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

	if (INTEL_GEN(dev_priv) >= 4)
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

	plane->max_stride = i9xx_plane_max_stride;
	plane->update_plane = i9xx_update_plane;
	plane->disable_plane = i9xx_disable_plane;
	plane->get_hw_state = i9xx_plane_get_hw_state;
	plane->check_plane = i9xx_plane_check;

	if (INTEL_GEN(dev_priv) >= 5 || IS_G4X(dev_priv))
		ret = drm_universal_plane_init(&dev_priv->drm, &plane->base,
					       0, plane_funcs,
					       formats, num_formats,
					       i9xx_format_modifiers,
					       DRM_PLANE_TYPE_PRIMARY,
					       "primary %c", pipe_name(pipe));
	else
		ret = drm_universal_plane_init(&dev_priv->drm, &plane->base,
					       0, plane_funcs,
					       formats, num_formats,
					       i9xx_format_modifiers,
					       DRM_PLANE_TYPE_PRIMARY,
					       "plane %c",
					       plane_name(plane->i9xx_plane));
	if (ret)
		goto fail;

	if (IS_CHERRYVIEW(dev_priv) && pipe == PIPE_B) {
		supported_rotations =
			DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_180 |
			DRM_MODE_REFLECT_X;
	} else if (INTEL_GEN(dev_priv) >= 4) {
		supported_rotations =
			DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_180;
	} else {
		supported_rotations = DRM_MODE_ROTATE_0;
	}

	if (INTEL_GEN(dev_priv) >= 4)
		drm_plane_create_rotation_property(&plane->base,
						   DRM_MODE_ROTATE_0,
						   supported_rotations);

	zpos = 0;
	drm_plane_create_zpos_immutable_property(&plane->base, zpos);

	drm_plane_helper_add(&plane->base, &intel_plane_helper_funcs);

	return plane;

fail:
	intel_plane_free(plane);

	return ERR_PTR(ret);
}

