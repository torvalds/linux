/*
 * Copyright © 2014 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * DOC: atomic plane helpers
 *
 * The functions here are used by the atomic plane helper functions to
 * implement legacy plane updates (i.e., drm_plane->update_plane() and
 * drm_plane->disable_plane()).  This allows plane updates to use the
 * atomic state infrastructure and perform plane updates as separate
 * prepare/check/commit/cleanup steps.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_plane_helper.h>

#include "gt/intel_rps.h"

#include "intel_atomic_plane.h"
#include "intel_cdclk.h"
#include "intel_display_trace.h"
#include "intel_display_types.h"
#include "intel_fb.h"
#include "intel_fb_pin.h"
#include "intel_pm.h"
#include "intel_sprite.h"
#include "skl_scaler.h"

static void intel_plane_state_reset(struct intel_plane_state *plane_state,
				    struct intel_plane *plane)
{
	memset(plane_state, 0, sizeof(*plane_state));

	__drm_atomic_helper_plane_state_reset(&plane_state->uapi, &plane->base);

	plane_state->scaler_id = -1;
}

struct intel_plane *intel_plane_alloc(void)
{
	struct intel_plane_state *plane_state;
	struct intel_plane *plane;

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	plane_state = kzalloc(sizeof(*plane_state), GFP_KERNEL);
	if (!plane_state) {
		kfree(plane);
		return ERR_PTR(-ENOMEM);
	}

	intel_plane_state_reset(plane_state, plane);

	plane->base.state = &plane_state->uapi;

	return plane;
}

void intel_plane_free(struct intel_plane *plane)
{
	intel_plane_destroy_state(&plane->base, plane->base.state);
	kfree(plane);
}

/**
 * intel_plane_duplicate_state - duplicate plane state
 * @plane: drm plane
 *
 * Allocates and returns a copy of the plane state (both common and
 * Intel-specific) for the specified plane.
 *
 * Returns: The newly allocated plane state, or NULL on failure.
 */
struct drm_plane_state *
intel_plane_duplicate_state(struct drm_plane *plane)
{
	struct intel_plane_state *intel_state;

	intel_state = to_intel_plane_state(plane->state);
	intel_state = kmemdup(intel_state, sizeof(*intel_state), GFP_KERNEL);

	if (!intel_state)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &intel_state->uapi);

	intel_state->ggtt_vma = NULL;
	intel_state->dpt_vma = NULL;
	intel_state->flags = 0;
	intel_state->do_async_flip = false;

	/* add reference to fb */
	if (intel_state->hw.fb)
		drm_framebuffer_get(intel_state->hw.fb);

	return &intel_state->uapi;
}

/**
 * intel_plane_destroy_state - destroy plane state
 * @plane: drm plane
 * @state: state object to destroy
 *
 * Destroys the plane state (both common and Intel-specific) for the
 * specified plane.
 */
void
intel_plane_destroy_state(struct drm_plane *plane,
			  struct drm_plane_state *state)
{
	struct intel_plane_state *plane_state = to_intel_plane_state(state);

	drm_WARN_ON(plane->dev, plane_state->ggtt_vma);
	drm_WARN_ON(plane->dev, plane_state->dpt_vma);

	__drm_atomic_helper_plane_destroy_state(&plane_state->uapi);
	if (plane_state->hw.fb)
		drm_framebuffer_put(plane_state->hw.fb);
	kfree(plane_state);
}

unsigned int intel_adjusted_rate(const struct drm_rect *src,
				 const struct drm_rect *dst,
				 unsigned int rate)
{
	unsigned int src_w, src_h, dst_w, dst_h;

	src_w = drm_rect_width(src) >> 16;
	src_h = drm_rect_height(src) >> 16;
	dst_w = drm_rect_width(dst);
	dst_h = drm_rect_height(dst);

	/* Downscaling limits the maximum pixel rate */
	dst_w = min(src_w, dst_w);
	dst_h = min(src_h, dst_h);

	return DIV_ROUND_UP_ULL(mul_u32_u32(rate, src_w * src_h),
				dst_w * dst_h);
}

unsigned int intel_plane_pixel_rate(const struct intel_crtc_state *crtc_state,
				    const struct intel_plane_state *plane_state)
{
	/*
	 * Note we don't check for plane visibility here as
	 * we want to use this when calculating the cursor
	 * watermarks even if the cursor is fully offscreen.
	 * That depends on the src/dst rectangles being
	 * correctly populated whenever the watermark code
	 * considers the cursor to be visible, whether or not
	 * it is actually visible.
	 *
	 * See: intel_wm_plane_visible() and intel_check_cursor()
	 */

	return intel_adjusted_rate(&plane_state->uapi.src,
				   &plane_state->uapi.dst,
				   crtc_state->pixel_rate);
}

unsigned int intel_plane_data_rate(const struct intel_crtc_state *crtc_state,
				   const struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int cpp;
	unsigned int pixel_rate;

	if (!plane_state->uapi.visible)
		return 0;

	pixel_rate = intel_plane_pixel_rate(crtc_state, plane_state);

	cpp = fb->format->cpp[0];

	/*
	 * Based on HSD#:1408715493
	 * NV12 cpp == 4, P010 cpp == 8
	 *
	 * FIXME what is the logic behind this?
	 */
	if (fb->format->is_yuv && fb->format->num_planes > 1)
		cpp *= 4;

	return pixel_rate * cpp;
}

int intel_plane_calc_min_cdclk(struct intel_atomic_state *state,
			       struct intel_plane *plane,
			       bool *need_cdclk_calc)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct intel_plane_state *plane_state =
		intel_atomic_get_new_plane_state(state, plane);
	struct intel_crtc *crtc = to_intel_crtc(plane_state->hw.crtc);
	const struct intel_cdclk_state *cdclk_state;
	const struct intel_crtc_state *old_crtc_state;
	struct intel_crtc_state *new_crtc_state;

	if (!plane_state->uapi.visible || !plane->min_cdclk)
		return 0;

	old_crtc_state = intel_atomic_get_old_crtc_state(state, crtc);
	new_crtc_state = intel_atomic_get_new_crtc_state(state, crtc);

	new_crtc_state->min_cdclk[plane->id] =
		plane->min_cdclk(new_crtc_state, plane_state);

	/*
	 * No need to check against the cdclk state if
	 * the min cdclk for the plane doesn't increase.
	 *
	 * Ie. we only ever increase the cdclk due to plane
	 * requirements. This can reduce back and forth
	 * display blinking due to constant cdclk changes.
	 */
	if (new_crtc_state->min_cdclk[plane->id] <=
	    old_crtc_state->min_cdclk[plane->id])
		return 0;

	cdclk_state = intel_atomic_get_cdclk_state(state);
	if (IS_ERR(cdclk_state))
		return PTR_ERR(cdclk_state);

	/*
	 * No need to recalculate the cdclk state if
	 * the min cdclk for the pipe doesn't increase.
	 *
	 * Ie. we only ever increase the cdclk due to plane
	 * requirements. This can reduce back and forth
	 * display blinking due to constant cdclk changes.
	 */
	if (new_crtc_state->min_cdclk[plane->id] <=
	    cdclk_state->min_cdclk[crtc->pipe])
		return 0;

	drm_dbg_kms(&dev_priv->drm,
		    "[PLANE:%d:%s] min cdclk (%d kHz) > [CRTC:%d:%s] min cdclk (%d kHz)\n",
		    plane->base.base.id, plane->base.name,
		    new_crtc_state->min_cdclk[plane->id],
		    crtc->base.base.id, crtc->base.name,
		    cdclk_state->min_cdclk[crtc->pipe]);
	*need_cdclk_calc = true;

	return 0;
}

static void intel_plane_clear_hw_state(struct intel_plane_state *plane_state)
{
	if (plane_state->hw.fb)
		drm_framebuffer_put(plane_state->hw.fb);

	memset(&plane_state->hw, 0, sizeof(plane_state->hw));
}

void intel_plane_copy_uapi_to_hw_state(struct intel_plane_state *plane_state,
				       const struct intel_plane_state *from_plane_state,
				       struct intel_crtc *crtc)
{
	intel_plane_clear_hw_state(plane_state);

	/*
	 * For the bigjoiner slave uapi.crtc will point at
	 * the master crtc. So we explicitly assign the right
	 * slave crtc to hw.crtc. uapi.crtc!=NULL simply indicates
	 * the plane is logically enabled on the uapi level.
	 */
	plane_state->hw.crtc = from_plane_state->uapi.crtc ? &crtc->base : NULL;

	plane_state->hw.fb = from_plane_state->uapi.fb;
	if (plane_state->hw.fb)
		drm_framebuffer_get(plane_state->hw.fb);

	plane_state->hw.alpha = from_plane_state->uapi.alpha;
	plane_state->hw.pixel_blend_mode =
		from_plane_state->uapi.pixel_blend_mode;
	plane_state->hw.rotation = from_plane_state->uapi.rotation;
	plane_state->hw.color_encoding = from_plane_state->uapi.color_encoding;
	plane_state->hw.color_range = from_plane_state->uapi.color_range;
	plane_state->hw.scaling_filter = from_plane_state->uapi.scaling_filter;

	plane_state->uapi.src = drm_plane_state_src(&from_plane_state->uapi);
	plane_state->uapi.dst = drm_plane_state_dest(&from_plane_state->uapi);
}

void intel_plane_copy_hw_state(struct intel_plane_state *plane_state,
			       const struct intel_plane_state *from_plane_state)
{
	intel_plane_clear_hw_state(plane_state);

	memcpy(&plane_state->hw, &from_plane_state->hw,
	       sizeof(plane_state->hw));

	if (plane_state->hw.fb)
		drm_framebuffer_get(plane_state->hw.fb);
}

void intel_plane_set_invisible(struct intel_crtc_state *crtc_state,
			       struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);

	crtc_state->active_planes &= ~BIT(plane->id);
	crtc_state->scaled_planes &= ~BIT(plane->id);
	crtc_state->nv12_planes &= ~BIT(plane->id);
	crtc_state->c8_planes &= ~BIT(plane->id);
	crtc_state->data_rate[plane->id] = 0;
	crtc_state->min_cdclk[plane->id] = 0;

	plane_state->uapi.visible = false;
}

/* FIXME nuke when all wm code is atomic */
static bool intel_wm_need_update(const struct intel_plane_state *cur,
				 struct intel_plane_state *new)
{
	/* Update watermarks on tiling or size changes. */
	if (new->uapi.visible != cur->uapi.visible)
		return true;

	if (!cur->hw.fb || !new->hw.fb)
		return false;

	if (cur->hw.fb->modifier != new->hw.fb->modifier ||
	    cur->hw.rotation != new->hw.rotation ||
	    drm_rect_width(&new->uapi.src) != drm_rect_width(&cur->uapi.src) ||
	    drm_rect_height(&new->uapi.src) != drm_rect_height(&cur->uapi.src) ||
	    drm_rect_width(&new->uapi.dst) != drm_rect_width(&cur->uapi.dst) ||
	    drm_rect_height(&new->uapi.dst) != drm_rect_height(&cur->uapi.dst))
		return true;

	return false;
}

static bool intel_plane_is_scaled(const struct intel_plane_state *plane_state)
{
	int src_w = drm_rect_width(&plane_state->uapi.src) >> 16;
	int src_h = drm_rect_height(&plane_state->uapi.src) >> 16;
	int dst_w = drm_rect_width(&plane_state->uapi.dst);
	int dst_h = drm_rect_height(&plane_state->uapi.dst);

	return src_w != dst_w || src_h != dst_h;
}

static bool intel_plane_do_async_flip(struct intel_plane *plane,
				      const struct intel_crtc_state *old_crtc_state,
				      const struct intel_crtc_state *new_crtc_state)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);

	if (!plane->async_flip)
		return false;

	if (!new_crtc_state->uapi.async_flip)
		return false;

	/*
	 * In platforms after DISPLAY13, we might need to override
	 * first async flip in order to change watermark levels
	 * as part of optimization.
	 * So for those, we are checking if this is a first async flip.
	 * For platforms earlier than DISPLAY13 we always do async flip.
	 */
	return DISPLAY_VER(i915) < 13 || old_crtc_state->uapi.async_flip;
}

static int intel_plane_atomic_calc_changes(const struct intel_crtc_state *old_crtc_state,
					   struct intel_crtc_state *new_crtc_state,
					   const struct intel_plane_state *old_plane_state,
					   struct intel_plane_state *new_plane_state)
{
	struct intel_crtc *crtc = to_intel_crtc(new_crtc_state->uapi.crtc);
	struct intel_plane *plane = to_intel_plane(new_plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	bool mode_changed = intel_crtc_needs_modeset(new_crtc_state);
	bool was_crtc_enabled = old_crtc_state->hw.active;
	bool is_crtc_enabled = new_crtc_state->hw.active;
	bool turn_off, turn_on, visible, was_visible;
	int ret;

	if (DISPLAY_VER(dev_priv) >= 9 && plane->id != PLANE_CURSOR) {
		ret = skl_update_scaler_plane(new_crtc_state, new_plane_state);
		if (ret)
			return ret;
	}

	was_visible = old_plane_state->uapi.visible;
	visible = new_plane_state->uapi.visible;

	if (!was_crtc_enabled && drm_WARN_ON(&dev_priv->drm, was_visible))
		was_visible = false;

	/*
	 * Visibility is calculated as if the crtc was on, but
	 * after scaler setup everything depends on it being off
	 * when the crtc isn't active.
	 *
	 * FIXME this is wrong for watermarks. Watermarks should also
	 * be computed as if the pipe would be active. Perhaps move
	 * per-plane wm computation to the .check_plane() hook, and
	 * only combine the results from all planes in the current place?
	 */
	if (!is_crtc_enabled) {
		intel_plane_set_invisible(new_crtc_state, new_plane_state);
		visible = false;
	}

	if (!was_visible && !visible)
		return 0;

	turn_off = was_visible && (!visible || mode_changed);
	turn_on = visible && (!was_visible || mode_changed);

	drm_dbg_atomic(&dev_priv->drm,
		       "[CRTC:%d:%s] with [PLANE:%d:%s] visible %i -> %i, off %i, on %i, ms %i\n",
		       crtc->base.base.id, crtc->base.name,
		       plane->base.base.id, plane->base.name,
		       was_visible, visible,
		       turn_off, turn_on, mode_changed);

	if (turn_on) {
		if (DISPLAY_VER(dev_priv) < 5 && !IS_G4X(dev_priv))
			new_crtc_state->update_wm_pre = true;

		/* must disable cxsr around plane enable/disable */
		if (plane->id != PLANE_CURSOR)
			new_crtc_state->disable_cxsr = true;
	} else if (turn_off) {
		if (DISPLAY_VER(dev_priv) < 5 && !IS_G4X(dev_priv))
			new_crtc_state->update_wm_post = true;

		/* must disable cxsr around plane enable/disable */
		if (plane->id != PLANE_CURSOR)
			new_crtc_state->disable_cxsr = true;
	} else if (intel_wm_need_update(old_plane_state, new_plane_state)) {
		if (DISPLAY_VER(dev_priv) < 5 && !IS_G4X(dev_priv)) {
			/* FIXME bollocks */
			new_crtc_state->update_wm_pre = true;
			new_crtc_state->update_wm_post = true;
		}
	}

	if (visible || was_visible)
		new_crtc_state->fb_bits |= plane->frontbuffer_bit;

	/*
	 * ILK/SNB DVSACNTR/Sprite Enable
	 * IVB SPR_CTL/Sprite Enable
	 * "When in Self Refresh Big FIFO mode, a write to enable the
	 *  plane will be internally buffered and delayed while Big FIFO
	 *  mode is exiting."
	 *
	 * Which means that enabling the sprite can take an extra frame
	 * when we start in big FIFO mode (LP1+). Thus we need to drop
	 * down to LP0 and wait for vblank in order to make sure the
	 * sprite gets enabled on the next vblank after the register write.
	 * Doing otherwise would risk enabling the sprite one frame after
	 * we've already signalled flip completion. We can resume LP1+
	 * once the sprite has been enabled.
	 *
	 *
	 * WaCxSRDisabledForSpriteScaling:ivb
	 * IVB SPR_SCALE/Scaling Enable
	 * "Low Power watermarks must be disabled for at least one
	 *  frame before enabling sprite scaling, and kept disabled
	 *  until sprite scaling is disabled."
	 *
	 * ILK/SNB DVSASCALE/Scaling Enable
	 * "When in Self Refresh Big FIFO mode, scaling enable will be
	 *  masked off while Big FIFO mode is exiting."
	 *
	 * Despite the w/a only being listed for IVB we assume that
	 * the ILK/SNB note has similar ramifications, hence we apply
	 * the w/a on all three platforms.
	 *
	 * With experimental results seems this is needed also for primary
	 * plane, not only sprite plane.
	 */
	if (plane->id != PLANE_CURSOR &&
	    (IS_IRONLAKE(dev_priv) || IS_SANDYBRIDGE(dev_priv) ||
	     IS_IVYBRIDGE(dev_priv)) &&
	    (turn_on || (!intel_plane_is_scaled(old_plane_state) &&
			 intel_plane_is_scaled(new_plane_state))))
		new_crtc_state->disable_lp_wm = true;

	if (intel_plane_do_async_flip(plane, old_crtc_state, new_crtc_state))
		new_plane_state->do_async_flip = true;

	return 0;
}

int intel_plane_atomic_check_with_state(const struct intel_crtc_state *old_crtc_state,
					struct intel_crtc_state *new_crtc_state,
					const struct intel_plane_state *old_plane_state,
					struct intel_plane_state *new_plane_state)
{
	struct intel_plane *plane = to_intel_plane(new_plane_state->uapi.plane);
	const struct drm_framebuffer *fb = new_plane_state->hw.fb;
	int ret;

	intel_plane_set_invisible(new_crtc_state, new_plane_state);
	new_crtc_state->enabled_planes &= ~BIT(plane->id);

	if (!new_plane_state->hw.crtc && !old_plane_state->hw.crtc)
		return 0;

	ret = plane->check_plane(new_crtc_state, new_plane_state);
	if (ret)
		return ret;

	if (fb)
		new_crtc_state->enabled_planes |= BIT(plane->id);

	/* FIXME pre-g4x don't work like this */
	if (new_plane_state->uapi.visible)
		new_crtc_state->active_planes |= BIT(plane->id);

	if (new_plane_state->uapi.visible &&
	    intel_plane_is_scaled(new_plane_state))
		new_crtc_state->scaled_planes |= BIT(plane->id);

	if (new_plane_state->uapi.visible &&
	    intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier))
		new_crtc_state->nv12_planes |= BIT(plane->id);

	if (new_plane_state->uapi.visible &&
	    fb->format->format == DRM_FORMAT_C8)
		new_crtc_state->c8_planes |= BIT(plane->id);

	if (new_plane_state->uapi.visible || old_plane_state->uapi.visible)
		new_crtc_state->update_planes |= BIT(plane->id);

	new_crtc_state->data_rate[plane->id] =
		intel_plane_data_rate(new_crtc_state, new_plane_state);

	return intel_plane_atomic_calc_changes(old_crtc_state, new_crtc_state,
					       old_plane_state, new_plane_state);
}

static struct intel_plane *
intel_crtc_get_plane(struct intel_crtc *crtc, enum plane_id plane_id)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct intel_plane *plane;

	for_each_intel_plane_on_crtc(&i915->drm, crtc, plane) {
		if (plane->id == plane_id)
			return plane;
	}

	return NULL;
}

int intel_plane_atomic_check(struct intel_atomic_state *state,
			     struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_plane_state *new_plane_state =
		intel_atomic_get_new_plane_state(state, plane);
	const struct intel_plane_state *old_plane_state =
		intel_atomic_get_old_plane_state(state, plane);
	const struct intel_plane_state *new_master_plane_state;
	struct intel_crtc *crtc = intel_crtc_for_pipe(i915, plane->pipe);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (new_crtc_state && intel_crtc_is_bigjoiner_slave(new_crtc_state)) {
		struct intel_crtc *master_crtc =
			intel_master_crtc(new_crtc_state);
		struct intel_plane *master_plane =
			intel_crtc_get_plane(master_crtc, plane->id);

		new_master_plane_state =
			intel_atomic_get_new_plane_state(state, master_plane);
	} else {
		new_master_plane_state = new_plane_state;
	}

	intel_plane_copy_uapi_to_hw_state(new_plane_state,
					  new_master_plane_state,
					  crtc);

	new_plane_state->uapi.visible = false;
	if (!new_crtc_state)
		return 0;

	return intel_plane_atomic_check_with_state(old_crtc_state,
						   new_crtc_state,
						   old_plane_state,
						   new_plane_state);
}

static struct intel_plane *
skl_next_plane_to_commit(struct intel_atomic_state *state,
			 struct intel_crtc *crtc,
			 struct skl_ddb_entry entries_y[I915_MAX_PLANES],
			 struct skl_ddb_entry entries_uv[I915_MAX_PLANES],
			 unsigned int *update_mask)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_plane_state *plane_state;
	struct intel_plane *plane;
	int i;

	if (*update_mask == 0)
		return NULL;

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		enum plane_id plane_id = plane->id;

		if (crtc->pipe != plane->pipe ||
		    !(*update_mask & BIT(plane_id)))
			continue;

		if (skl_ddb_allocation_overlaps(&crtc_state->wm.skl.plane_ddb_y[plane_id],
						entries_y,
						I915_MAX_PLANES, plane_id) ||
		    skl_ddb_allocation_overlaps(&crtc_state->wm.skl.plane_ddb_uv[plane_id],
						entries_uv,
						I915_MAX_PLANES, plane_id))
			continue;

		*update_mask &= ~BIT(plane_id);
		entries_y[plane_id] = crtc_state->wm.skl.plane_ddb_y[plane_id];
		entries_uv[plane_id] = crtc_state->wm.skl.plane_ddb_uv[plane_id];

		return plane;
	}

	/* should never happen */
	drm_WARN_ON(state->base.dev, 1);

	return NULL;
}

void intel_plane_update_noarm(struct intel_plane *plane,
			      const struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	trace_intel_plane_update_noarm(&plane->base, crtc);

	if (plane->update_noarm)
		plane->update_noarm(plane, crtc_state, plane_state);
}

void intel_plane_update_arm(struct intel_plane *plane,
			    const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	trace_intel_plane_update_arm(&plane->base, crtc);

	if (plane_state->do_async_flip)
		plane->async_flip(plane, crtc_state, plane_state, true);
	else
		plane->update_arm(plane, crtc_state, plane_state);
}

void intel_plane_disable_arm(struct intel_plane *plane,
			     const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	trace_intel_plane_disable_arm(&plane->base, crtc);
	plane->disable_arm(plane, crtc_state);
}

void intel_update_planes_on_crtc(struct intel_atomic_state *state,
				 struct intel_crtc *crtc)
{
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	u32 update_mask = new_crtc_state->update_planes;
	struct intel_plane_state *new_plane_state;
	struct intel_plane *plane;
	int i;

	if (new_crtc_state->uapi.async_flip)
		return;

	/*
	 * Since we only write non-arming registers here,
	 * the order does not matter even for skl+.
	 */
	for_each_new_intel_plane_in_state(state, plane, new_plane_state, i) {
		if (crtc->pipe != plane->pipe ||
		    !(update_mask & BIT(plane->id)))
			continue;

		/* TODO: for mailbox updates this should be skipped */
		if (new_plane_state->uapi.visible ||
		    new_plane_state->planar_slave)
			intel_plane_update_noarm(plane, new_crtc_state, new_plane_state);
	}
}

void skl_arm_planes_on_crtc(struct intel_atomic_state *state,
			    struct intel_crtc *crtc)
{
	struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct skl_ddb_entry entries_y[I915_MAX_PLANES];
	struct skl_ddb_entry entries_uv[I915_MAX_PLANES];
	u32 update_mask = new_crtc_state->update_planes;
	struct intel_plane *plane;

	memcpy(entries_y, old_crtc_state->wm.skl.plane_ddb_y,
	       sizeof(old_crtc_state->wm.skl.plane_ddb_y));
	memcpy(entries_uv, old_crtc_state->wm.skl.plane_ddb_uv,
	       sizeof(old_crtc_state->wm.skl.plane_ddb_uv));

	while ((plane = skl_next_plane_to_commit(state, crtc,
						 entries_y, entries_uv,
						 &update_mask))) {
		struct intel_plane_state *new_plane_state =
			intel_atomic_get_new_plane_state(state, plane);

		/*
		 * TODO: for mailbox updates intel_plane_update_noarm()
		 * would have to be called here as well.
		 */
		if (new_plane_state->uapi.visible ||
		    new_plane_state->planar_slave)
			intel_plane_update_arm(plane, new_crtc_state, new_plane_state);
		else
			intel_plane_disable_arm(plane, new_crtc_state);
	}
}

void i9xx_arm_planes_on_crtc(struct intel_atomic_state *state,
			     struct intel_crtc *crtc)
{
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	u32 update_mask = new_crtc_state->update_planes;
	struct intel_plane_state *new_plane_state;
	struct intel_plane *plane;
	int i;

	for_each_new_intel_plane_in_state(state, plane, new_plane_state, i) {
		if (crtc->pipe != plane->pipe ||
		    !(update_mask & BIT(plane->id)))
			continue;

		/*
		 * TODO: for mailbox updates intel_plane_update_noarm()
		 * would have to be called here as well.
		 */
		if (new_plane_state->uapi.visible)
			intel_plane_update_arm(plane, new_crtc_state, new_plane_state);
		else
			intel_plane_disable_arm(plane, new_crtc_state);
	}
}

int intel_atomic_plane_check_clipping(struct intel_plane_state *plane_state,
				      struct intel_crtc_state *crtc_state,
				      int min_scale, int max_scale,
				      bool can_position)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	struct drm_framebuffer *fb = plane_state->hw.fb;
	struct drm_rect *src = &plane_state->uapi.src;
	struct drm_rect *dst = &plane_state->uapi.dst;
	unsigned int rotation = plane_state->hw.rotation;
	struct drm_rect clip = {};
	int hscale, vscale;

	if (!fb) {
		plane_state->uapi.visible = false;
		return 0;
	}

	drm_rect_rotate(src, fb->width << 16, fb->height << 16, rotation);

	/* Check scaling */
	hscale = drm_rect_calc_hscale(src, dst, min_scale, max_scale);
	vscale = drm_rect_calc_vscale(src, dst, min_scale, max_scale);
	if (hscale < 0 || vscale < 0) {
		drm_dbg_kms(&i915->drm, "Invalid scaling of plane\n");
		drm_rect_debug_print("src: ", src, true);
		drm_rect_debug_print("dst: ", dst, false);
		return -ERANGE;
	}

	if (crtc_state->hw.enable) {
		clip.x2 = crtc_state->pipe_src_w;
		clip.y2 = crtc_state->pipe_src_h;
	}

	/* right side of the image is on the slave crtc, adjust dst to match */
	if (intel_crtc_is_bigjoiner_slave(crtc_state))
		drm_rect_translate(dst, -crtc_state->pipe_src_w, 0);

	/*
	 * FIXME: This might need further adjustment for seamless scaling
	 * with phase information, for the 2p2 and 2p1 scenarios.
	 */
	plane_state->uapi.visible = drm_rect_clip_scaled(src, dst, &clip);

	drm_rect_rotate_inv(src, fb->width << 16, fb->height << 16, rotation);

	if (!can_position && plane_state->uapi.visible &&
	    !drm_rect_equals(dst, &clip)) {
		drm_dbg_kms(&i915->drm, "Plane must cover entire CRTC\n");
		drm_rect_debug_print("dst: ", dst, false);
		drm_rect_debug_print("clip: ", &clip, false);
		return -EINVAL;
	}

	return 0;
}

struct wait_rps_boost {
	struct wait_queue_entry wait;

	struct drm_crtc *crtc;
	struct i915_request *request;
};

static int do_rps_boost(struct wait_queue_entry *_wait,
			unsigned mode, int sync, void *key)
{
	struct wait_rps_boost *wait = container_of(_wait, typeof(*wait), wait);
	struct i915_request *rq = wait->request;

	/*
	 * If we missed the vblank, but the request is already running it
	 * is reasonable to assume that it will complete before the next
	 * vblank without our intervention, so leave RPS alone.
	 */
	if (!i915_request_started(rq))
		intel_rps_boost(rq);
	i915_request_put(rq);

	drm_crtc_vblank_put(wait->crtc);

	list_del(&wait->wait.entry);
	kfree(wait);
	return 1;
}

static void add_rps_boost_after_vblank(struct drm_crtc *crtc,
				       struct dma_fence *fence)
{
	struct wait_rps_boost *wait;

	if (!dma_fence_is_i915(fence))
		return;

	if (DISPLAY_VER(to_i915(crtc->dev)) < 6)
		return;

	if (drm_crtc_vblank_get(crtc))
		return;

	wait = kmalloc(sizeof(*wait), GFP_KERNEL);
	if (!wait) {
		drm_crtc_vblank_put(crtc);
		return;
	}

	wait->request = to_request(dma_fence_get(fence));
	wait->crtc = crtc;

	wait->wait.func = do_rps_boost;
	wait->wait.flags = 0;

	add_wait_queue(drm_crtc_vblank_waitqueue(crtc), &wait->wait);
}

/**
 * intel_prepare_plane_fb - Prepare fb for usage on plane
 * @_plane: drm plane to prepare for
 * @_new_plane_state: the plane state being prepared
 *
 * Prepares a framebuffer for usage on a display plane.  Generally this
 * involves pinning the underlying object and updating the frontbuffer tracking
 * bits.  Some older platforms need special physical address handling for
 * cursor planes.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int
intel_prepare_plane_fb(struct drm_plane *_plane,
		       struct drm_plane_state *_new_plane_state)
{
	struct i915_sched_attr attr = { .priority = I915_PRIORITY_DISPLAY };
	struct intel_plane *plane = to_intel_plane(_plane);
	struct intel_plane_state *new_plane_state =
		to_intel_plane_state(_new_plane_state);
	struct intel_atomic_state *state =
		to_intel_atomic_state(new_plane_state->uapi.state);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct intel_plane_state *old_plane_state =
		intel_atomic_get_old_plane_state(state, plane);
	struct drm_i915_gem_object *obj = intel_fb_obj(new_plane_state->hw.fb);
	struct drm_i915_gem_object *old_obj = intel_fb_obj(old_plane_state->hw.fb);
	int ret;

	if (old_obj) {
		const struct intel_crtc_state *crtc_state =
			intel_atomic_get_new_crtc_state(state,
							to_intel_crtc(old_plane_state->hw.crtc));

		/* Big Hammer, we also need to ensure that any pending
		 * MI_WAIT_FOR_EVENT inside a user batch buffer on the
		 * current scanout is retired before unpinning the old
		 * framebuffer. Note that we rely on userspace rendering
		 * into the buffer attached to the pipe they are waiting
		 * on. If not, userspace generates a GPU hang with IPEHR
		 * point to the MI_WAIT_FOR_EVENT.
		 *
		 * This should only fail upon a hung GPU, in which case we
		 * can safely continue.
		 */
		if (intel_crtc_needs_modeset(crtc_state)) {
			ret = i915_sw_fence_await_reservation(&state->commit_ready,
							      old_obj->base.resv, NULL,
							      false, 0,
							      GFP_KERNEL);
			if (ret < 0)
				return ret;
		}
	}

	if (new_plane_state->uapi.fence) { /* explicit fencing */
		i915_gem_fence_wait_priority(new_plane_state->uapi.fence,
					     &attr);
		ret = i915_sw_fence_await_dma_fence(&state->commit_ready,
						    new_plane_state->uapi.fence,
						    i915_fence_timeout(dev_priv),
						    GFP_KERNEL);
		if (ret < 0)
			return ret;
	}

	if (!obj)
		return 0;


	ret = intel_plane_pin_fb(new_plane_state);
	if (ret)
		return ret;

	i915_gem_object_wait_priority(obj, 0, &attr);

	if (!new_plane_state->uapi.fence) { /* implicit fencing */
		struct dma_resv_iter cursor;
		struct dma_fence *fence;

		ret = i915_sw_fence_await_reservation(&state->commit_ready,
						      obj->base.resv, NULL,
						      false,
						      i915_fence_timeout(dev_priv),
						      GFP_KERNEL);
		if (ret < 0)
			goto unpin_fb;

		dma_resv_iter_begin(&cursor, obj->base.resv, false);
		dma_resv_for_each_fence_unlocked(&cursor, fence) {
			add_rps_boost_after_vblank(new_plane_state->hw.crtc,
						   fence);
		}
		dma_resv_iter_end(&cursor);
	} else {
		add_rps_boost_after_vblank(new_plane_state->hw.crtc,
					   new_plane_state->uapi.fence);
	}

	/*
	 * We declare pageflips to be interactive and so merit a small bias
	 * towards upclocking to deliver the frame on time. By only changing
	 * the RPS thresholds to sample more regularly and aim for higher
	 * clocks we can hopefully deliver low power workloads (like kodi)
	 * that are not quite steady state without resorting to forcing
	 * maximum clocks following a vblank miss (see do_rps_boost()).
	 */
	if (!state->rps_interactive) {
		intel_rps_mark_interactive(&to_gt(dev_priv)->rps, true);
		state->rps_interactive = true;
	}

	return 0;

unpin_fb:
	intel_plane_unpin_fb(new_plane_state);

	return ret;
}

/**
 * intel_cleanup_plane_fb - Cleans up an fb after plane use
 * @plane: drm plane to clean up for
 * @_old_plane_state: the state from the previous modeset
 *
 * Cleans up a framebuffer that has just been removed from a plane.
 */
static void
intel_cleanup_plane_fb(struct drm_plane *plane,
		       struct drm_plane_state *_old_plane_state)
{
	struct intel_plane_state *old_plane_state =
		to_intel_plane_state(_old_plane_state);
	struct intel_atomic_state *state =
		to_intel_atomic_state(old_plane_state->uapi.state);
	struct drm_i915_private *dev_priv = to_i915(plane->dev);
	struct drm_i915_gem_object *obj = intel_fb_obj(old_plane_state->hw.fb);

	if (!obj)
		return;

	if (state->rps_interactive) {
		intel_rps_mark_interactive(&to_gt(dev_priv)->rps, false);
		state->rps_interactive = false;
	}

	/* Should only be called after a successful intel_prepare_plane_fb()! */
	intel_plane_unpin_fb(old_plane_state);
}

static const struct drm_plane_helper_funcs intel_plane_helper_funcs = {
	.prepare_fb = intel_prepare_plane_fb,
	.cleanup_fb = intel_cleanup_plane_fb,
};

void intel_plane_helper_add(struct intel_plane *plane)
{
	drm_plane_helper_add(&plane->base, &intel_plane_helper_funcs);
}
