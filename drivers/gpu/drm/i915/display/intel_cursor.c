// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */
#include <linux/kernel.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_blend.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_fourcc.h>

#include "intel_atomic.h"
#include "intel_atomic_plane.h"
#include "intel_cursor.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_display.h"
#include "intel_fb.h"
#include "intel_fb_pin.h"
#include "intel_frontbuffer.h"
#include "intel_pm.h"
#include "intel_psr.h"
#include "intel_sprite.h"

/* Cursor formats */
static const u32 intel_cursor_formats[] = {
	DRM_FORMAT_ARGB8888,
};

static u32 intel_cursor_base(const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	const struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	u32 base;

	if (INTEL_INFO(dev_priv)->display.cursor_needs_physical)
		base = sg_dma_address(obj->mm.pages->sgl);
	else
		base = intel_plane_ggtt_offset(plane_state);

	return base + plane_state->view.color_plane[0].offset;
}

static u32 intel_cursor_position(const struct intel_plane_state *plane_state)
{
	int x = plane_state->uapi.dst.x1;
	int y = plane_state->uapi.dst.y1;
	u32 pos = 0;

	if (x < 0) {
		pos |= CURSOR_POS_X_SIGN;
		x = -x;
	}
	pos |= CURSOR_POS_X(x);

	if (y < 0) {
		pos |= CURSOR_POS_Y_SIGN;
		y = -y;
	}
	pos |= CURSOR_POS_Y(y);

	return pos;
}

static bool intel_cursor_size_ok(const struct intel_plane_state *plane_state)
{
	const struct drm_mode_config *config =
		&plane_state->uapi.plane->dev->mode_config;
	int width = drm_rect_width(&plane_state->uapi.dst);
	int height = drm_rect_height(&plane_state->uapi.dst);

	return width > 0 && width <= config->cursor_width &&
		height > 0 && height <= config->cursor_height;
}

static int intel_cursor_check_surface(struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->uapi.plane->dev);
	unsigned int rotation = plane_state->hw.rotation;
	int src_x, src_y;
	u32 offset;
	int ret;

	ret = intel_plane_compute_gtt(plane_state);
	if (ret)
		return ret;

	if (!plane_state->uapi.visible)
		return 0;

	src_x = plane_state->uapi.src.x1 >> 16;
	src_y = plane_state->uapi.src.y1 >> 16;

	intel_add_fb_offsets(&src_x, &src_y, plane_state, 0);
	offset = intel_plane_compute_aligned_offset(&src_x, &src_y,
						    plane_state, 0);

	if (src_x != 0 || src_y != 0) {
		drm_dbg_kms(&dev_priv->drm,
			    "Arbitrary cursor panning not supported\n");
		return -EINVAL;
	}

	/*
	 * Put the final coordinates back so that the src
	 * coordinate checks will see the right values.
	 */
	drm_rect_translate_to(&plane_state->uapi.src,
			      src_x << 16, src_y << 16);

	/* ILK+ do this automagically in hardware */
	if (HAS_GMCH(dev_priv) && rotation & DRM_MODE_ROTATE_180) {
		const struct drm_framebuffer *fb = plane_state->hw.fb;
		int src_w = drm_rect_width(&plane_state->uapi.src) >> 16;
		int src_h = drm_rect_height(&plane_state->uapi.src) >> 16;

		offset += (src_h * src_w - 1) * fb->format->cpp[0];
	}

	plane_state->view.color_plane[0].offset = offset;
	plane_state->view.color_plane[0].x = src_x;
	plane_state->view.color_plane[0].y = src_y;

	return 0;
}

static int intel_check_cursor(struct intel_crtc_state *crtc_state,
			      struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	const struct drm_rect src = plane_state->uapi.src;
	const struct drm_rect dst = plane_state->uapi.dst;
	int ret;

	if (fb && fb->modifier != DRM_FORMAT_MOD_LINEAR) {
		drm_dbg_kms(&i915->drm, "cursor cannot be tiled\n");
		return -EINVAL;
	}

	ret = intel_atomic_plane_check_clipping(plane_state, crtc_state,
						DRM_PLANE_HELPER_NO_SCALING,
						DRM_PLANE_HELPER_NO_SCALING,
						true);
	if (ret)
		return ret;

	/* Use the unclipped src/dst rectangles, which we program to hw */
	plane_state->uapi.src = src;
	plane_state->uapi.dst = dst;

	/* final plane coordinates will be relative to the plane's pipe */
	drm_rect_translate(&plane_state->uapi.dst,
			   -crtc_state->pipe_src.x1,
			   -crtc_state->pipe_src.y1);

	ret = intel_cursor_check_surface(plane_state);
	if (ret)
		return ret;

	if (!plane_state->uapi.visible)
		return 0;

	ret = intel_plane_check_src_coordinates(plane_state);
	if (ret)
		return ret;

	return 0;
}

static unsigned int
i845_cursor_max_stride(struct intel_plane *plane,
		       u32 pixel_format, u64 modifier,
		       unsigned int rotation)
{
	return 2048;
}

static u32 i845_cursor_ctl_crtc(const struct intel_crtc_state *crtc_state)
{
	u32 cntl = 0;

	if (crtc_state->gamma_enable)
		cntl |= CURSOR_PIPE_GAMMA_ENABLE;

	return cntl;
}

static u32 i845_cursor_ctl(const struct intel_crtc_state *crtc_state,
			   const struct intel_plane_state *plane_state)
{
	return CURSOR_ENABLE |
		CURSOR_FORMAT_ARGB |
		CURSOR_STRIDE(plane_state->view.color_plane[0].mapping_stride);
}

static bool i845_cursor_size_ok(const struct intel_plane_state *plane_state)
{
	int width = drm_rect_width(&plane_state->uapi.dst);

	/*
	 * 845g/865g are only limited by the width of their cursors,
	 * the height is arbitrary up to the precision of the register.
	 */
	return intel_cursor_size_ok(plane_state) && IS_ALIGNED(width, 64);
}

static int i845_check_cursor(struct intel_crtc_state *crtc_state,
			     struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	int ret;

	ret = intel_check_cursor(crtc_state, plane_state);
	if (ret)
		return ret;

	/* if we want to turn off the cursor ignore width and height */
	if (!fb)
		return 0;

	/* Check for which cursor types we support */
	if (!i845_cursor_size_ok(plane_state)) {
		drm_dbg_kms(&i915->drm,
			    "Cursor dimension %dx%d not supported\n",
			    drm_rect_width(&plane_state->uapi.dst),
			    drm_rect_height(&plane_state->uapi.dst));
		return -EINVAL;
	}

	drm_WARN_ON(&i915->drm, plane_state->uapi.visible &&
		    plane_state->view.color_plane[0].mapping_stride != fb->pitches[0]);

	switch (fb->pitches[0]) {
	case 256:
	case 512:
	case 1024:
	case 2048:
		break;
	default:
		 drm_dbg_kms(&i915->drm, "Invalid cursor stride (%u)\n",
			     fb->pitches[0]);
		return -EINVAL;
	}

	plane_state->ctl = i845_cursor_ctl(crtc_state, plane_state);

	return 0;
}

/* TODO: split into noarm+arm pair */
static void i845_cursor_update_arm(struct intel_plane *plane,
				   const struct intel_crtc_state *crtc_state,
				   const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	u32 cntl = 0, base = 0, pos = 0, size = 0;

	if (plane_state && plane_state->uapi.visible) {
		unsigned int width = drm_rect_width(&plane_state->uapi.dst);
		unsigned int height = drm_rect_height(&plane_state->uapi.dst);

		cntl = plane_state->ctl |
			i845_cursor_ctl_crtc(crtc_state);

		size = CURSOR_HEIGHT(height) | CURSOR_WIDTH(width);

		base = intel_cursor_base(plane_state);
		pos = intel_cursor_position(plane_state);
	}

	/* On these chipsets we can only modify the base/size/stride
	 * whilst the cursor is disabled.
	 */
	if (plane->cursor.base != base ||
	    plane->cursor.size != size ||
	    plane->cursor.cntl != cntl) {
		intel_de_write_fw(dev_priv, CURCNTR(PIPE_A), 0);
		intel_de_write_fw(dev_priv, CURBASE(PIPE_A), base);
		intel_de_write_fw(dev_priv, CURSIZE(PIPE_A), size);
		intel_de_write_fw(dev_priv, CURPOS(PIPE_A), pos);
		intel_de_write_fw(dev_priv, CURCNTR(PIPE_A), cntl);

		plane->cursor.base = base;
		plane->cursor.size = size;
		plane->cursor.cntl = cntl;
	} else {
		intel_de_write_fw(dev_priv, CURPOS(PIPE_A), pos);
	}
}

static void i845_cursor_disable_arm(struct intel_plane *plane,
				    const struct intel_crtc_state *crtc_state)
{
	i845_cursor_update_arm(plane, crtc_state, NULL);
}

static bool i845_cursor_get_hw_state(struct intel_plane *plane,
				     enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum intel_display_power_domain power_domain;
	intel_wakeref_t wakeref;
	bool ret;

	power_domain = POWER_DOMAIN_PIPE(PIPE_A);
	wakeref = intel_display_power_get_if_enabled(dev_priv, power_domain);
	if (!wakeref)
		return false;

	ret = intel_de_read(dev_priv, CURCNTR(PIPE_A)) & CURSOR_ENABLE;

	*pipe = PIPE_A;

	intel_display_power_put(dev_priv, power_domain, wakeref);

	return ret;
}

static unsigned int
i9xx_cursor_max_stride(struct intel_plane *plane,
		       u32 pixel_format, u64 modifier,
		       unsigned int rotation)
{
	return plane->base.dev->mode_config.cursor_width * 4;
}

static u32 i9xx_cursor_ctl_crtc(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 cntl = 0;

	if (DISPLAY_VER(dev_priv) >= 11)
		return cntl;

	if (crtc_state->gamma_enable)
		cntl = MCURSOR_PIPE_GAMMA_ENABLE;

	if (crtc_state->csc_enable)
		cntl |= MCURSOR_PIPE_CSC_ENABLE;

	if (DISPLAY_VER(dev_priv) < 5 && !IS_G4X(dev_priv))
		cntl |= MCURSOR_PIPE_SEL(crtc->pipe);

	return cntl;
}

static u32 i9xx_cursor_ctl(const struct intel_crtc_state *crtc_state,
			   const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->uapi.plane->dev);
	u32 cntl = 0;

	if (IS_SANDYBRIDGE(dev_priv) || IS_IVYBRIDGE(dev_priv))
		cntl |= MCURSOR_TRICKLE_FEED_DISABLE;

	switch (drm_rect_width(&plane_state->uapi.dst)) {
	case 64:
		cntl |= MCURSOR_MODE_64_ARGB_AX;
		break;
	case 128:
		cntl |= MCURSOR_MODE_128_ARGB_AX;
		break;
	case 256:
		cntl |= MCURSOR_MODE_256_ARGB_AX;
		break;
	default:
		MISSING_CASE(drm_rect_width(&plane_state->uapi.dst));
		return 0;
	}

	if (plane_state->hw.rotation & DRM_MODE_ROTATE_180)
		cntl |= MCURSOR_ROTATE_180;

	/* Wa_22012358565:adl-p */
	if (DISPLAY_VER(dev_priv) == 13)
		cntl |= MCURSOR_ARB_SLOTS(1);

	return cntl;
}

static bool i9xx_cursor_size_ok(const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->uapi.plane->dev);
	int width = drm_rect_width(&plane_state->uapi.dst);
	int height = drm_rect_height(&plane_state->uapi.dst);

	if (!intel_cursor_size_ok(plane_state))
		return false;

	/* Cursor width is limited to a few power-of-two sizes */
	switch (width) {
	case 256:
	case 128:
	case 64:
		break;
	default:
		return false;
	}

	/*
	 * IVB+ have CUR_FBC_CTL which allows an arbitrary cursor
	 * height from 8 lines up to the cursor width, when the
	 * cursor is not rotated. Everything else requires square
	 * cursors.
	 */
	if (HAS_CUR_FBC(dev_priv) &&
	    plane_state->hw.rotation & DRM_MODE_ROTATE_0) {
		if (height < 8 || height > width)
			return false;
	} else {
		if (height != width)
			return false;
	}

	return true;
}

static int i9xx_check_cursor(struct intel_crtc_state *crtc_state,
			     struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	enum pipe pipe = plane->pipe;
	int ret;

	ret = intel_check_cursor(crtc_state, plane_state);
	if (ret)
		return ret;

	/* if we want to turn off the cursor ignore width and height */
	if (!fb)
		return 0;

	/* Check for which cursor types we support */
	if (!i9xx_cursor_size_ok(plane_state)) {
		drm_dbg(&dev_priv->drm,
			"Cursor dimension %dx%d not supported\n",
			drm_rect_width(&plane_state->uapi.dst),
			drm_rect_height(&plane_state->uapi.dst));
		return -EINVAL;
	}

	drm_WARN_ON(&dev_priv->drm, plane_state->uapi.visible &&
		    plane_state->view.color_plane[0].mapping_stride != fb->pitches[0]);

	if (fb->pitches[0] !=
	    drm_rect_width(&plane_state->uapi.dst) * fb->format->cpp[0]) {
		drm_dbg_kms(&dev_priv->drm,
			    "Invalid cursor stride (%u) (cursor width %d)\n",
			    fb->pitches[0],
			    drm_rect_width(&plane_state->uapi.dst));
		return -EINVAL;
	}

	/*
	 * There's something wrong with the cursor on CHV pipe C.
	 * If it straddles the left edge of the screen then
	 * moving it away from the edge or disabling it often
	 * results in a pipe underrun, and often that can lead to
	 * dead pipe (constant underrun reported, and it scans
	 * out just a solid color). To recover from that, the
	 * display power well must be turned off and on again.
	 * Refuse the put the cursor into that compromised position.
	 */
	if (IS_CHERRYVIEW(dev_priv) && pipe == PIPE_C &&
	    plane_state->uapi.visible && plane_state->uapi.dst.x1 < 0) {
		drm_dbg_kms(&dev_priv->drm,
			    "CHV cursor C not allowed to straddle the left screen edge\n");
		return -EINVAL;
	}

	plane_state->ctl = i9xx_cursor_ctl(crtc_state, plane_state);

	return 0;
}

/* TODO: split into noarm+arm pair */
static void i9xx_cursor_update_arm(struct intel_plane *plane,
				   const struct intel_crtc_state *crtc_state,
				   const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;
	u32 cntl = 0, base = 0, pos = 0, fbc_ctl = 0;

	if (plane_state && plane_state->uapi.visible) {
		int width = drm_rect_width(&plane_state->uapi.dst);
		int height = drm_rect_height(&plane_state->uapi.dst);

		cntl = plane_state->ctl |
			i9xx_cursor_ctl_crtc(crtc_state);

		if (width != height)
			fbc_ctl = CUR_FBC_EN | CUR_FBC_HEIGHT(height - 1);

		base = intel_cursor_base(plane_state);
		pos = intel_cursor_position(plane_state);
	}

	/*
	 * On some platforms writing CURCNTR first will also
	 * cause CURPOS to be armed by the CURBASE write.
	 * Without the CURCNTR write the CURPOS write would
	 * arm itself. Thus we always update CURCNTR before
	 * CURPOS.
	 *
	 * On other platforms CURPOS always requires the
	 * CURBASE write to arm the update. Additonally
	 * a write to any of the cursor register will cancel
	 * an already armed cursor update. Thus leaving out
	 * the CURBASE write after CURPOS could lead to a
	 * cursor that doesn't appear to move, or even change
	 * shape. Thus we always write CURBASE.
	 *
	 * The other registers are armed by the CURBASE write
	 * except when the plane is getting enabled at which time
	 * the CURCNTR write arms the update.
	 */

	if (DISPLAY_VER(dev_priv) >= 9)
		skl_write_cursor_wm(plane, crtc_state);

	if (plane_state)
		intel_psr2_program_plane_sel_fetch(plane, crtc_state, plane_state, 0);
	else
		intel_psr2_disable_plane_sel_fetch(plane, crtc_state);

	if (plane->cursor.base != base ||
	    plane->cursor.size != fbc_ctl ||
	    plane->cursor.cntl != cntl) {
		if (HAS_CUR_FBC(dev_priv))
			intel_de_write_fw(dev_priv, CUR_FBC_CTL(pipe),
					  fbc_ctl);
		intel_de_write_fw(dev_priv, CURCNTR(pipe), cntl);
		intel_de_write_fw(dev_priv, CURPOS(pipe), pos);
		intel_de_write_fw(dev_priv, CURBASE(pipe), base);

		plane->cursor.base = base;
		plane->cursor.size = fbc_ctl;
		plane->cursor.cntl = cntl;
	} else {
		intel_de_write_fw(dev_priv, CURPOS(pipe), pos);
		intel_de_write_fw(dev_priv, CURBASE(pipe), base);
	}
}

static void i9xx_cursor_disable_arm(struct intel_plane *plane,
				    const struct intel_crtc_state *crtc_state)
{
	i9xx_cursor_update_arm(plane, crtc_state, NULL);
}

static bool i9xx_cursor_get_hw_state(struct intel_plane *plane,
				     enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum intel_display_power_domain power_domain;
	intel_wakeref_t wakeref;
	bool ret;
	u32 val;

	/*
	 * Not 100% correct for planes that can move between pipes,
	 * but that's only the case for gen2-3 which don't have any
	 * display power wells.
	 */
	power_domain = POWER_DOMAIN_PIPE(plane->pipe);
	wakeref = intel_display_power_get_if_enabled(dev_priv, power_domain);
	if (!wakeref)
		return false;

	val = intel_de_read(dev_priv, CURCNTR(plane->pipe));

	ret = val & MCURSOR_MODE_MASK;

	if (DISPLAY_VER(dev_priv) >= 5 || IS_G4X(dev_priv))
		*pipe = plane->pipe;
	else
		*pipe = REG_FIELD_GET(MCURSOR_PIPE_SEL_MASK, val);

	intel_display_power_put(dev_priv, power_domain, wakeref);

	return ret;
}

static bool intel_cursor_format_mod_supported(struct drm_plane *_plane,
					      u32 format, u64 modifier)
{
	if (!intel_fb_plane_supports_modifier(to_intel_plane(_plane), modifier))
		return false;

	return format == DRM_FORMAT_ARGB8888;
}

static int
intel_legacy_cursor_update(struct drm_plane *_plane,
			   struct drm_crtc *_crtc,
			   struct drm_framebuffer *fb,
			   int crtc_x, int crtc_y,
			   unsigned int crtc_w, unsigned int crtc_h,
			   u32 src_x, u32 src_y,
			   u32 src_w, u32 src_h,
			   struct drm_modeset_acquire_ctx *ctx)
{
	struct intel_plane *plane = to_intel_plane(_plane);
	struct intel_crtc *crtc = to_intel_crtc(_crtc);
	struct intel_plane_state *old_plane_state =
		to_intel_plane_state(plane->base.state);
	struct intel_plane_state *new_plane_state;
	struct intel_crtc_state *crtc_state =
		to_intel_crtc_state(crtc->base.state);
	struct intel_crtc_state *new_crtc_state;
	int ret;

	/*
	 * When crtc is inactive or there is a modeset pending,
	 * wait for it to complete in the slowpath.
	 * PSR2 selective fetch also requires the slow path as
	 * PSR2 plane and transcoder registers can only be updated during
	 * vblank.
	 *
	 * FIXME bigjoiner fastpath would be good
	 */
	if (!crtc_state->hw.active || intel_crtc_needs_modeset(crtc_state) ||
	    crtc_state->update_pipe || crtc_state->bigjoiner_pipes)
		goto slow;

	/*
	 * Don't do an async update if there is an outstanding commit modifying
	 * the plane.  This prevents our async update's changes from getting
	 * overridden by a previous synchronous update's state.
	 */
	if (old_plane_state->uapi.commit &&
	    !try_wait_for_completion(&old_plane_state->uapi.commit->hw_done))
		goto slow;

	/*
	 * If any parameters change that may affect watermarks,
	 * take the slowpath. Only changing fb or position should be
	 * in the fastpath.
	 */
	if (old_plane_state->uapi.crtc != &crtc->base ||
	    old_plane_state->uapi.src_w != src_w ||
	    old_plane_state->uapi.src_h != src_h ||
	    old_plane_state->uapi.crtc_w != crtc_w ||
	    old_plane_state->uapi.crtc_h != crtc_h ||
	    !old_plane_state->uapi.fb != !fb)
		goto slow;

	new_plane_state = to_intel_plane_state(intel_plane_duplicate_state(&plane->base));
	if (!new_plane_state)
		return -ENOMEM;

	new_crtc_state = to_intel_crtc_state(intel_crtc_duplicate_state(&crtc->base));
	if (!new_crtc_state) {
		ret = -ENOMEM;
		goto out_free;
	}

	drm_atomic_set_fb_for_plane(&new_plane_state->uapi, fb);

	new_plane_state->uapi.src_x = src_x;
	new_plane_state->uapi.src_y = src_y;
	new_plane_state->uapi.src_w = src_w;
	new_plane_state->uapi.src_h = src_h;
	new_plane_state->uapi.crtc_x = crtc_x;
	new_plane_state->uapi.crtc_y = crtc_y;
	new_plane_state->uapi.crtc_w = crtc_w;
	new_plane_state->uapi.crtc_h = crtc_h;

	intel_plane_copy_uapi_to_hw_state(new_plane_state, new_plane_state, crtc);

	ret = intel_plane_atomic_check_with_state(crtc_state, new_crtc_state,
						  old_plane_state, new_plane_state);
	if (ret)
		goto out_free;

	ret = intel_plane_pin_fb(new_plane_state);
	if (ret)
		goto out_free;

	intel_frontbuffer_flush(to_intel_frontbuffer(new_plane_state->hw.fb),
				ORIGIN_CURSOR_UPDATE);
	intel_frontbuffer_track(to_intel_frontbuffer(old_plane_state->hw.fb),
				to_intel_frontbuffer(new_plane_state->hw.fb),
				plane->frontbuffer_bit);

	/* Swap plane state */
	plane->base.state = &new_plane_state->uapi;

	/*
	 * We cannot swap crtc_state as it may be in use by an atomic commit or
	 * page flip that's running simultaneously. If we swap crtc_state and
	 * destroy the old state, we will cause a use-after-free there.
	 *
	 * Only update active_planes, which is needed for our internal
	 * bookkeeping. Either value will do the right thing when updating
	 * planes atomically. If the cursor was part of the atomic update then
	 * we would have taken the slowpath.
	 */
	crtc_state->active_planes = new_crtc_state->active_planes;

	/*
	 * Technically we should do a vblank evasion here to make
	 * sure all the cursor registers update on the same frame.
	 * For now just make sure the register writes happen as
	 * quickly as possible to minimize the race window.
	 */
	local_irq_disable();

	if (new_plane_state->uapi.visible) {
		intel_plane_update_noarm(plane, crtc_state, new_plane_state);
		intel_plane_update_arm(plane, crtc_state, new_plane_state);
	} else {
		intel_plane_disable_arm(plane, crtc_state);
	}

	local_irq_enable();

	intel_plane_unpin_fb(old_plane_state);

out_free:
	if (new_crtc_state)
		intel_crtc_destroy_state(&crtc->base, &new_crtc_state->uapi);
	if (ret)
		intel_plane_destroy_state(&plane->base, &new_plane_state->uapi);
	else
		intel_plane_destroy_state(&plane->base, &old_plane_state->uapi);
	return ret;

slow:
	return drm_atomic_helper_update_plane(&plane->base, &crtc->base, fb,
					      crtc_x, crtc_y, crtc_w, crtc_h,
					      src_x, src_y, src_w, src_h, ctx);
}

static const struct drm_plane_funcs intel_cursor_plane_funcs = {
	.update_plane = intel_legacy_cursor_update,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = intel_plane_destroy,
	.atomic_duplicate_state = intel_plane_duplicate_state,
	.atomic_destroy_state = intel_plane_destroy_state,
	.format_mod_supported = intel_cursor_format_mod_supported,
};

struct intel_plane *
intel_cursor_plane_create(struct drm_i915_private *dev_priv,
			  enum pipe pipe)
{
	struct intel_plane *cursor;
	int ret, zpos;
	u64 *modifiers;

	cursor = intel_plane_alloc();
	if (IS_ERR(cursor))
		return cursor;

	cursor->pipe = pipe;
	cursor->i9xx_plane = (enum i9xx_plane_id) pipe;
	cursor->id = PLANE_CURSOR;
	cursor->frontbuffer_bit = INTEL_FRONTBUFFER(pipe, cursor->id);

	if (IS_I845G(dev_priv) || IS_I865G(dev_priv)) {
		cursor->max_stride = i845_cursor_max_stride;
		cursor->update_arm = i845_cursor_update_arm;
		cursor->disable_arm = i845_cursor_disable_arm;
		cursor->get_hw_state = i845_cursor_get_hw_state;
		cursor->check_plane = i845_check_cursor;
	} else {
		cursor->max_stride = i9xx_cursor_max_stride;
		cursor->update_arm = i9xx_cursor_update_arm;
		cursor->disable_arm = i9xx_cursor_disable_arm;
		cursor->get_hw_state = i9xx_cursor_get_hw_state;
		cursor->check_plane = i9xx_check_cursor;
	}

	cursor->cursor.base = ~0;
	cursor->cursor.cntl = ~0;

	if (IS_I845G(dev_priv) || IS_I865G(dev_priv) || HAS_CUR_FBC(dev_priv))
		cursor->cursor.size = ~0;

	modifiers = intel_fb_plane_get_modifiers(dev_priv, INTEL_PLANE_CAP_NONE);

	ret = drm_universal_plane_init(&dev_priv->drm, &cursor->base,
				       0, &intel_cursor_plane_funcs,
				       intel_cursor_formats,
				       ARRAY_SIZE(intel_cursor_formats),
				       modifiers,
				       DRM_PLANE_TYPE_CURSOR,
				       "cursor %c", pipe_name(pipe));

	kfree(modifiers);

	if (ret)
		goto fail;

	if (DISPLAY_VER(dev_priv) >= 4)
		drm_plane_create_rotation_property(&cursor->base,
						   DRM_MODE_ROTATE_0,
						   DRM_MODE_ROTATE_0 |
						   DRM_MODE_ROTATE_180);

	zpos = RUNTIME_INFO(dev_priv)->num_sprites[pipe] + 1;
	drm_plane_create_zpos_immutable_property(&cursor->base, zpos);

	if (DISPLAY_VER(dev_priv) >= 12)
		drm_plane_enable_fb_damage_clips(&cursor->base);

	intel_plane_helper_add(cursor);

	return cursor;

fail:
	intel_plane_free(cursor);

	return ERR_PTR(ret);
}
