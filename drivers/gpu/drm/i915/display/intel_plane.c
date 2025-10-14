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

#include <linux/dma-fence-chain.h>
#include <linux/dma-resv.h>
#include <linux/iosys-map.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_cache.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_panic.h>

#include "gem/i915_gem_object.h"
#include "i915_scheduler_types.h"
#include "i9xx_plane_regs.h"
#include "intel_cdclk.h"
#include "intel_cursor.h"
#include "intel_display_rps.h"
#include "intel_display_trace.h"
#include "intel_display_types.h"
#include "intel_fb.h"
#include "intel_fb_pin.h"
#include "intel_fbdev.h"
#include "intel_panic.h"
#include "intel_plane.h"
#include "intel_psr.h"
#include "skl_scaler.h"
#include "skl_universal_plane.h"
#include "skl_watermark.h"

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
 * intel_plane_destroy - destroy a plane
 * @plane: plane to destroy
 *
 * Common destruction function for all types of planes (primary, cursor,
 * sprite).
 */
void intel_plane_destroy(struct drm_plane *plane)
{
	drm_plane_cleanup(plane);
	kfree(to_intel_plane(plane));
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
	intel_state->damage = DRM_RECT_INIT(0, 0, 0, 0);

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

bool intel_plane_needs_physical(struct intel_plane *plane)
{
	struct intel_display *display = to_intel_display(plane);

	return plane->id == PLANE_CURSOR &&
		DISPLAY_INFO(display)->cursor_needs_physical;
}

bool intel_plane_can_async_flip(struct intel_plane *plane, u32 format,
				u64 modifier)
{
	if (intel_format_info_is_yuv_semiplanar(drm_format_info(format), modifier) ||
	    format == DRM_FORMAT_C8)
		return false;

	return plane->can_async_flip && plane->can_async_flip(modifier);
}

bool intel_plane_format_mod_supported_async(struct drm_plane *plane,
					    u32 format,
					    u64 modifier)
{
	if (!plane->funcs->format_mod_supported(plane, format, modifier))
		return false;

	return intel_plane_can_async_flip(to_intel_plane(plane),
					format, modifier);
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
				   const struct intel_plane_state *plane_state,
				   int color_plane)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;

	if (!plane_state->uapi.visible)
		return 0;

	return intel_plane_pixel_rate(crtc_state, plane_state) *
		fb->format->cpp[color_plane];
}

static unsigned int
intel_plane_relative_data_rate(const struct intel_crtc_state *crtc_state,
			       const struct intel_plane_state *plane_state,
			       int color_plane)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rel_data_rate;
	int width, height;

	if (plane->id == PLANE_CURSOR)
		return 0;

	if (!plane_state->uapi.visible)
		return 0;

	/*
	 * Src coordinates are already rotated by 270 degrees for
	 * the 90/270 degree plane rotation cases (to match the
	 * GTT mapping), hence no need to account for rotation here.
	 */
	width = drm_rect_width(&plane_state->uapi.src) >> 16;
	height = drm_rect_height(&plane_state->uapi.src) >> 16;

	/* UV plane does 1/2 pixel sub-sampling */
	if (color_plane == 1) {
		width /= 2;
		height /= 2;
	}

	rel_data_rate =
		skl_plane_relative_data_rate(crtc_state, plane, width, height,
					     fb->format->cpp[color_plane]);
	if (!rel_data_rate)
		return 0;

	return intel_adjusted_rate(&plane_state->uapi.src,
				   &plane_state->uapi.dst,
				   rel_data_rate);
}

int intel_plane_calc_min_cdclk(struct intel_atomic_state *state,
			       struct intel_plane *plane,
			       bool *need_cdclk_calc)
{
	struct intel_display *display = to_intel_display(plane);
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
	    intel_cdclk_min_cdclk(cdclk_state, crtc->pipe))
		return 0;

	drm_dbg_kms(display->drm,
		    "[PLANE:%d:%s] min cdclk (%d kHz) > [CRTC:%d:%s] min cdclk (%d kHz)\n",
		    plane->base.base.id, plane->base.name,
		    new_crtc_state->min_cdclk[plane->id],
		    crtc->base.base.id, crtc->base.name,
		    intel_cdclk_min_cdclk(cdclk_state, crtc->pipe));
	*need_cdclk_calc = true;

	return 0;
}

static void intel_plane_clear_hw_state(struct intel_plane_state *plane_state)
{
	if (plane_state->hw.fb)
		drm_framebuffer_put(plane_state->hw.fb);

	memset(&plane_state->hw, 0, sizeof(plane_state->hw));
}

static void
intel_plane_copy_uapi_plane_damage(struct intel_plane_state *new_plane_state,
				   const struct intel_plane_state *old_uapi_plane_state,
				   const struct intel_plane_state *new_uapi_plane_state)
{
	struct intel_display *display = to_intel_display(new_plane_state);
	struct drm_rect *damage = &new_plane_state->damage;

	/* damage property tracking enabled from display version 12 onwards */
	if (DISPLAY_VER(display) < 12)
		return;

	if (!drm_atomic_helper_damage_merged(&old_uapi_plane_state->uapi,
					     &new_uapi_plane_state->uapi,
					     damage))
		/* Incase helper fails, mark whole plane region as damage */
		*damage = drm_plane_state_src(&new_uapi_plane_state->uapi);
}

void intel_plane_copy_uapi_to_hw_state(struct intel_plane_state *plane_state,
				       const struct intel_plane_state *from_plane_state,
				       struct intel_crtc *crtc)
{
	intel_plane_clear_hw_state(plane_state);

	/*
	 * For the joiner secondary uapi.crtc will point at
	 * the primary crtc. So we explicitly assign the right
	 * secondary crtc to hw.crtc. uapi.crtc!=NULL simply
	 * indicates the plane is logically enabled on the uapi level.
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
	crtc_state->async_flip_planes &= ~BIT(plane->id);
	crtc_state->data_rate[plane->id] = 0;
	crtc_state->data_rate_y[plane->id] = 0;
	crtc_state->rel_data_rate[plane->id] = 0;
	crtc_state->rel_data_rate_y[plane->id] = 0;
	crtc_state->min_cdclk[plane->id] = 0;

	plane_state->uapi.visible = false;
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
	struct intel_display *display = to_intel_display(plane);

	if (!plane->async_flip)
		return false;

	if (!new_crtc_state->uapi.async_flip)
		return false;

	/*
	 * In platforms after DISPLAY13, we might need to override
	 * first async flip in order to change watermark levels
	 * as part of optimization.
	 *
	 * And let's do this for all skl+ so that we can eg. change the
	 * modifier as well.
	 *
	 * TODO: For older platforms there is less reason to do this as
	 * only X-tile is supported with async flips, though we could
	 * extend this so other scanout parameters (stride/etc) could
	 * be changed as well...
	 */
	return DISPLAY_VER(display) < 9 || old_crtc_state->uapi.async_flip;
}

static bool i9xx_must_disable_cxsr(const struct intel_crtc_state *new_crtc_state,
				   const struct intel_plane_state *old_plane_state,
				   const struct intel_plane_state *new_plane_state)
{
	struct intel_plane *plane = to_intel_plane(new_plane_state->uapi.plane);
	bool old_visible = old_plane_state->uapi.visible;
	bool new_visible = new_plane_state->uapi.visible;
	u32 old_ctl = old_plane_state->ctl;
	u32 new_ctl = new_plane_state->ctl;
	bool modeset, turn_on, turn_off;

	if (plane->id == PLANE_CURSOR)
		return false;

	modeset = intel_crtc_needs_modeset(new_crtc_state);
	turn_off = old_visible && (!new_visible || modeset);
	turn_on = new_visible && (!old_visible || modeset);

	/* Must disable CxSR around plane enable/disable */
	if (turn_on || turn_off)
		return true;

	if (!old_visible || !new_visible)
		return false;

	/*
	 * Most plane control register updates are blocked while in CxSR.
	 *
	 * Tiling mode is one exception where the primary plane can
	 * apparently handle it, whereas the sprites can not (the
	 * sprite issue being only relevant on VLV/CHV where CxSR
	 * is actually possible with a sprite enabled).
	 */
	if (plane->id == PLANE_PRIMARY) {
		old_ctl &= ~DISP_TILED;
		new_ctl &= ~DISP_TILED;
	}

	return old_ctl != new_ctl;
}

static bool ilk_must_disable_cxsr(const struct intel_crtc_state *new_crtc_state,
				  const struct intel_plane_state *old_plane_state,
				  const struct intel_plane_state *new_plane_state)
{
	struct intel_plane *plane = to_intel_plane(new_plane_state->uapi.plane);
	bool old_visible = old_plane_state->uapi.visible;
	bool new_visible = new_plane_state->uapi.visible;
	bool modeset, turn_on;

	if (plane->id == PLANE_CURSOR)
		return false;

	modeset = intel_crtc_needs_modeset(new_crtc_state);
	turn_on = new_visible && (!old_visible || modeset);

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
	 * With experimental results seems this is needed also for primary
	 * plane, not only sprite plane.
	 */
	if (turn_on)
		return true;

	/*
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
	 */
	return !intel_plane_is_scaled(old_plane_state) &&
		intel_plane_is_scaled(new_plane_state);
}

static int intel_plane_atomic_calc_changes(const struct intel_crtc_state *old_crtc_state,
					   struct intel_crtc_state *new_crtc_state,
					   const struct intel_plane_state *old_plane_state,
					   struct intel_plane_state *new_plane_state)
{
	struct intel_display *display = to_intel_display(new_crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(new_crtc_state->uapi.crtc);
	struct intel_plane *plane = to_intel_plane(new_plane_state->uapi.plane);
	bool mode_changed = intel_crtc_needs_modeset(new_crtc_state);
	bool was_crtc_enabled = old_crtc_state->hw.active;
	bool is_crtc_enabled = new_crtc_state->hw.active;
	bool turn_off, turn_on, visible, was_visible;
	int ret;

	if (DISPLAY_VER(display) >= 9 && plane->id != PLANE_CURSOR) {
		ret = skl_update_scaler_plane(new_crtc_state, new_plane_state);
		if (ret)
			return ret;
	}

	was_visible = old_plane_state->uapi.visible;
	visible = new_plane_state->uapi.visible;

	if (!was_crtc_enabled && drm_WARN_ON(display->drm, was_visible))
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

	drm_dbg_atomic(display->drm,
		       "[CRTC:%d:%s] with [PLANE:%d:%s] visible %i -> %i, off %i, on %i, ms %i\n",
		       crtc->base.base.id, crtc->base.name,
		       plane->base.base.id, plane->base.name,
		       was_visible, visible,
		       turn_off, turn_on, mode_changed);

	if (visible || was_visible)
		new_crtc_state->fb_bits |= plane->frontbuffer_bit;

	if (HAS_GMCH(display) &&
	    i9xx_must_disable_cxsr(new_crtc_state, old_plane_state, new_plane_state))
		new_crtc_state->disable_cxsr = true;

	if ((display->platform.ironlake || display->platform.sandybridge || display->platform.ivybridge) &&
	    ilk_must_disable_cxsr(new_crtc_state, old_plane_state, new_plane_state))
		new_crtc_state->disable_cxsr = true;

	if (intel_plane_do_async_flip(plane, old_crtc_state, new_crtc_state)) {
		new_crtc_state->do_async_flip = true;
		new_crtc_state->async_flip_planes |= BIT(plane->id);
	} else if (plane->need_async_flip_toggle_wa &&
		   new_crtc_state->uapi.async_flip) {
		/*
		 * On platforms with double buffered async flip bit we
		 * set the bit already one frame early during the sync
		 * flip (see {i9xx,skl}_plane_update_arm()). The
		 * hardware will therefore be ready to perform a real
		 * async flip during the next commit, without having
		 * to wait yet another frame for the bit to latch.
		 */
		new_crtc_state->async_flip_planes |= BIT(plane->id);
	}

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

	if (new_plane_state->uapi.visible &&
	    intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier)) {
		new_crtc_state->data_rate_y[plane->id] =
			intel_plane_data_rate(new_crtc_state, new_plane_state, 0);
		new_crtc_state->data_rate[plane->id] =
			intel_plane_data_rate(new_crtc_state, new_plane_state, 1);

		new_crtc_state->rel_data_rate_y[plane->id] =
			intel_plane_relative_data_rate(new_crtc_state,
						       new_plane_state, 0);
		new_crtc_state->rel_data_rate[plane->id] =
			intel_plane_relative_data_rate(new_crtc_state,
						       new_plane_state, 1);
	} else if (new_plane_state->uapi.visible) {
		new_crtc_state->data_rate[plane->id] =
			intel_plane_data_rate(new_crtc_state, new_plane_state, 0);

		new_crtc_state->rel_data_rate[plane->id] =
			intel_plane_relative_data_rate(new_crtc_state,
						       new_plane_state, 0);
	}

	return intel_plane_atomic_calc_changes(old_crtc_state, new_crtc_state,
					       old_plane_state, new_plane_state);
}

struct intel_plane *
intel_crtc_get_plane(struct intel_crtc *crtc, enum plane_id plane_id)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_plane *plane;

	for_each_intel_plane_on_crtc(display->drm, crtc, plane) {
		if (plane->id == plane_id)
			return plane;
	}

	return NULL;
}

static int plane_atomic_check(struct intel_atomic_state *state,
			      struct intel_plane *plane)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_plane_state *new_plane_state =
		intel_atomic_get_new_plane_state(state, plane);
	const struct intel_plane_state *old_plane_state =
		intel_atomic_get_old_plane_state(state, plane);
	const struct intel_plane_state *new_primary_crtc_plane_state;
	const struct intel_plane_state *old_primary_crtc_plane_state;
	struct intel_crtc *crtc = intel_crtc_for_pipe(display, plane->pipe);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (new_crtc_state && intel_crtc_is_joiner_secondary(new_crtc_state)) {
		struct intel_crtc *primary_crtc =
			intel_primary_crtc(new_crtc_state);
		struct intel_plane *primary_crtc_plane =
			intel_crtc_get_plane(primary_crtc, plane->id);

		new_primary_crtc_plane_state =
			intel_atomic_get_new_plane_state(state, primary_crtc_plane);
		old_primary_crtc_plane_state =
			intel_atomic_get_old_plane_state(state, primary_crtc_plane);
	} else {
		new_primary_crtc_plane_state = new_plane_state;
		old_primary_crtc_plane_state = old_plane_state;
	}

	intel_plane_copy_uapi_plane_damage(new_plane_state,
					   old_primary_crtc_plane_state,
					   new_primary_crtc_plane_state);

	intel_plane_copy_uapi_to_hw_state(new_plane_state,
					  new_primary_crtc_plane_state,
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
			 struct skl_ddb_entry ddb[I915_MAX_PLANES],
			 struct skl_ddb_entry ddb_y[I915_MAX_PLANES],
			 unsigned int *update_mask)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_plane_state __maybe_unused *plane_state;
	struct intel_plane *plane;
	int i;

	if (*update_mask == 0)
		return NULL;

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		enum plane_id plane_id = plane->id;

		if (crtc->pipe != plane->pipe ||
		    !(*update_mask & BIT(plane_id)))
			continue;

		if (skl_ddb_allocation_overlaps(&crtc_state->wm.skl.plane_ddb[plane_id],
						ddb, I915_MAX_PLANES, plane_id) ||
		    skl_ddb_allocation_overlaps(&crtc_state->wm.skl.plane_ddb_y[plane_id],
						ddb_y, I915_MAX_PLANES, plane_id))
			continue;

		*update_mask &= ~BIT(plane_id);
		ddb[plane_id] = crtc_state->wm.skl.plane_ddb[plane_id];
		ddb_y[plane_id] = crtc_state->wm.skl.plane_ddb_y[plane_id];

		return plane;
	}

	/* should never happen */
	drm_WARN_ON(state->base.dev, 1);

	return NULL;
}

void intel_plane_update_noarm(struct intel_dsb *dsb,
			      struct intel_plane *plane,
			      const struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	trace_intel_plane_update_noarm(plane_state, crtc);

	if (plane->fbc)
		intel_fbc_dirty_rect_update_noarm(dsb, plane);

	if (plane->update_noarm)
		plane->update_noarm(dsb, plane, crtc_state, plane_state);
}

void intel_plane_async_flip(struct intel_dsb *dsb,
			    struct intel_plane *plane,
			    const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state,
			    bool async_flip)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	trace_intel_plane_async_flip(plane, crtc, async_flip);
	plane->async_flip(dsb, plane, crtc_state, plane_state, async_flip);
}

void intel_plane_update_arm(struct intel_dsb *dsb,
			    struct intel_plane *plane,
			    const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (crtc_state->do_async_flip && plane->async_flip) {
		intel_plane_async_flip(dsb, plane, crtc_state, plane_state, true);
		return;
	}

	trace_intel_plane_update_arm(plane_state, crtc);
	plane->update_arm(dsb, plane, crtc_state, plane_state);
}

void intel_plane_disable_arm(struct intel_dsb *dsb,
			     struct intel_plane *plane,
			     const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	trace_intel_plane_disable_arm(plane, crtc);
	plane->disable_arm(dsb, plane, crtc_state);
}

void intel_crtc_planes_update_noarm(struct intel_dsb *dsb,
				    struct intel_atomic_state *state,
				    struct intel_crtc *crtc)
{
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	u32 update_mask = new_crtc_state->update_planes;
	struct intel_plane_state *new_plane_state;
	struct intel_plane *plane;
	int i;

	if (new_crtc_state->do_async_flip)
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
		    new_plane_state->is_y_plane)
			intel_plane_update_noarm(dsb, plane,
						 new_crtc_state, new_plane_state);
	}
}

static void skl_crtc_planes_update_arm(struct intel_dsb *dsb,
				       struct intel_atomic_state *state,
				       struct intel_crtc *crtc)
{
	struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct skl_ddb_entry ddb[I915_MAX_PLANES];
	struct skl_ddb_entry ddb_y[I915_MAX_PLANES];
	u32 update_mask = new_crtc_state->update_planes;
	struct intel_plane *plane;

	memcpy(ddb, old_crtc_state->wm.skl.plane_ddb,
	       sizeof(old_crtc_state->wm.skl.plane_ddb));
	memcpy(ddb_y, old_crtc_state->wm.skl.plane_ddb_y,
	       sizeof(old_crtc_state->wm.skl.plane_ddb_y));

	while ((plane = skl_next_plane_to_commit(state, crtc, ddb, ddb_y, &update_mask))) {
		struct intel_plane_state *new_plane_state =
			intel_atomic_get_new_plane_state(state, plane);

		/*
		 * TODO: for mailbox updates intel_plane_update_noarm()
		 * would have to be called here as well.
		 */
		if (new_plane_state->uapi.visible ||
		    new_plane_state->is_y_plane)
			intel_plane_update_arm(dsb, plane, new_crtc_state, new_plane_state);
		else
			intel_plane_disable_arm(dsb, plane, new_crtc_state);
	}
}

static void i9xx_crtc_planes_update_arm(struct intel_dsb *dsb,
					struct intel_atomic_state *state,
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
			intel_plane_update_arm(dsb, plane, new_crtc_state, new_plane_state);
		else
			intel_plane_disable_arm(dsb, plane, new_crtc_state);
	}
}

void intel_crtc_planes_update_arm(struct intel_dsb *dsb,
				  struct intel_atomic_state *state,
				  struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);

	if (DISPLAY_VER(display) >= 9)
		skl_crtc_planes_update_arm(dsb, state, crtc);
	else
		i9xx_crtc_planes_update_arm(dsb, state, crtc);
}

int intel_plane_check_clipping(struct intel_plane_state *plane_state,
			       struct intel_crtc_state *crtc_state,
			       int min_scale, int max_scale,
			       bool can_position)
{
	struct intel_display *display = to_intel_display(plane_state);
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_framebuffer *fb = plane_state->hw.fb;
	struct drm_rect *src = &plane_state->uapi.src;
	struct drm_rect *dst = &plane_state->uapi.dst;
	const struct drm_rect *clip = &crtc_state->pipe_src;
	unsigned int rotation = plane_state->hw.rotation;
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
		drm_dbg_kms(display->drm,
			    "[PLANE:%d:%s] invalid scaling "DRM_RECT_FP_FMT " -> " DRM_RECT_FMT "\n",
			    plane->base.base.id, plane->base.name,
			    DRM_RECT_FP_ARG(src), DRM_RECT_ARG(dst));
		return -ERANGE;
	}

	/*
	 * FIXME: This might need further adjustment for seamless scaling
	 * with phase information, for the 2p2 and 2p1 scenarios.
	 */
	plane_state->uapi.visible = drm_rect_clip_scaled(src, dst, clip);

	drm_rect_rotate_inv(src, fb->width << 16, fb->height << 16, rotation);

	if (!can_position && plane_state->uapi.visible &&
	    !drm_rect_equals(dst, clip)) {
		drm_dbg_kms(display->drm,
			    "[PLANE:%d:%s] plane (" DRM_RECT_FMT ") must cover entire CRTC (" DRM_RECT_FMT ")\n",
			    plane->base.base.id, plane->base.name,
			    DRM_RECT_ARG(dst), DRM_RECT_ARG(clip));
		return -EINVAL;
	}

	/* final plane coordinates will be relative to the plane's pipe */
	drm_rect_translate(dst, -clip->x1, -clip->y1);

	return 0;
}

int intel_plane_check_src_coordinates(struct intel_plane_state *plane_state)
{
	struct intel_display *display = to_intel_display(plane_state);
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
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
	} else if (DISPLAY_VER(display) >= 20 &&
		   intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier)) {
		/*
		 * This allows NV12 and P0xx formats to have odd size and/or odd
		 * source coordinates on DISPLAY_VER(display) >= 20
		 */
		hsub = 1;
		vsub = 1;

		/* Wa_16023981245 */
		if ((DISPLAY_VERx100(display) == 2000 ||
		     DISPLAY_VERx100(display) == 3000 ||
		     DISPLAY_VERx100(display) == 3002) &&
		     src_x % 2 != 0)
			hsub = 2;
	} else {
		hsub = fb->format->hsub;
		vsub = fb->format->vsub;
	}

	if (rotated)
		hsub = vsub = max(hsub, vsub);

	if (src_x % hsub || src_w % hsub) {
		drm_dbg_kms(display->drm,
			    "[PLANE:%d:%s] src x/w (%u, %u) must be a multiple of %u (rotated: %s)\n",
			    plane->base.base.id, plane->base.name,
			    src_x, src_w, hsub, str_yes_no(rotated));
		return -EINVAL;
	}

	if (src_y % vsub || src_h % vsub) {
		drm_dbg_kms(display->drm,
			    "[PLANE:%d:%s] src y/h (%u, %u) must be a multiple of %u (rotated: %s)\n",
			    plane->base.base.id, plane->base.name,
			    src_y, src_h, vsub, str_yes_no(rotated));
		return -EINVAL;
	}

	return 0;
}

static int add_dma_resv_fences(struct dma_resv *resv,
			       struct drm_plane_state *new_plane_state)
{
	struct dma_fence *fence = dma_fence_get(new_plane_state->fence);
	struct dma_fence *new;
	int ret;

	ret = dma_resv_get_singleton(resv, dma_resv_usage_rw(false), &new);
	if (ret)
		goto error;

	if (new && fence) {
		struct dma_fence_chain *chain = dma_fence_chain_alloc();

		if (!chain) {
			ret = -ENOMEM;
			goto error;
		}

		dma_fence_chain_init(chain, fence, new, 1);
		fence = &chain->base;

	} else if (new) {
		fence = new;
	}

	dma_fence_put(new_plane_state->fence);
	new_plane_state->fence = fence;
	return 0;

error:
	dma_fence_put(fence);
	return ret;
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
	struct intel_display *display = to_intel_display(plane);
	struct intel_plane_state *new_plane_state =
		to_intel_plane_state(_new_plane_state);
	struct intel_atomic_state *state =
		to_intel_atomic_state(new_plane_state->uapi.state);
	struct intel_plane_state *old_plane_state =
		intel_atomic_get_old_plane_state(state, plane);
	struct drm_gem_object *obj = intel_fb_bo(new_plane_state->hw.fb);
	struct drm_gem_object *old_obj = intel_fb_bo(old_plane_state->hw.fb);
	int ret;

	if (old_obj) {
		const struct intel_crtc_state *new_crtc_state =
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
		if (intel_crtc_needs_modeset(new_crtc_state)) {
			ret = add_dma_resv_fences(old_obj->resv,
						  &new_plane_state->uapi);
			if (ret < 0)
				return ret;
		}
	}

	if (!obj)
		return 0;

	ret = intel_plane_pin_fb(new_plane_state, old_plane_state);
	if (ret)
		return ret;

	ret = drm_gem_plane_helper_prepare_fb(&plane->base, &new_plane_state->uapi);
	if (ret < 0)
		goto unpin_fb;

	if (new_plane_state->uapi.fence) {
		i915_gem_fence_wait_priority(new_plane_state->uapi.fence,
					     &attr);

		intel_display_rps_boost_after_vblank(new_plane_state->hw.crtc,
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
	intel_display_rps_mark_interactive(display, state, true);

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
	struct intel_display *display = to_intel_display(plane->dev);
	struct intel_plane_state *old_plane_state =
		to_intel_plane_state(_old_plane_state);
	struct intel_atomic_state *state =
		to_intel_atomic_state(old_plane_state->uapi.state);
	struct drm_gem_object *obj = intel_fb_bo(old_plane_state->hw.fb);

	if (!obj)
		return;

	intel_display_rps_mark_interactive(display, state, false);

	intel_plane_unpin_fb(old_plane_state);
}

/* Handle Y-tiling, only if DPT is enabled (otherwise disabling tiling is easier)
 * All DPT hardware have 128-bytes width tiling, so Y-tile dimension is 32x32
 * pixels for 32bits pixels.
 */
#define YTILE_WIDTH	32
#define YTILE_HEIGHT	32
#define YTILE_SIZE (YTILE_WIDTH * YTILE_HEIGHT * 4)

static unsigned int intel_ytile_get_offset(unsigned int width, unsigned int x, unsigned int y)
{
	u32 offset;
	unsigned int swizzle;
	unsigned int width_in_blocks = DIV_ROUND_UP(width, 32);

	/* Block offset */
	offset = ((y / YTILE_HEIGHT) * width_in_blocks + (x / YTILE_WIDTH)) * YTILE_SIZE;

	x = x % YTILE_WIDTH;
	y = y % YTILE_HEIGHT;

	/* bit order inside a block is x4 x3 x2 y4 y3 y2 y1 y0 x1 x0 */
	swizzle = (x & 3) | ((y & 0x1f) << 2) | ((x & 0x1c) << 5);
	offset += swizzle * 4;
	return offset;
}

static unsigned int intel_4tile_get_offset(unsigned int width, unsigned int x, unsigned int y)
{
	u32 offset;
	unsigned int swizzle;
	unsigned int width_in_blocks = DIV_ROUND_UP(width, 32);

	/* Block offset */
	offset = ((y / YTILE_HEIGHT) * width_in_blocks + (x / YTILE_WIDTH)) * YTILE_SIZE;

	x = x % YTILE_WIDTH;
	y = y % YTILE_HEIGHT;

	/* bit order inside a block is y4 y3 x4 y2 x3 x2 y1 y0 x1 x0 */
	swizzle = (x & 3) | ((y & 3) << 2) | ((x & 0xc) << 2) | (y & 4) << 4 |
		  ((x & 0x10) << 3) | ((y & 0x18) << 5);
	offset += swizzle * 4;
	return offset;
}

static void intel_panic_flush(struct drm_plane *plane)
{
	struct intel_plane_state *plane_state = to_intel_plane_state(plane->state);
	struct intel_crtc_state *crtc_state = to_intel_crtc_state(plane->state->crtc->state);
	struct intel_plane *iplane = to_intel_plane(plane);
	struct intel_display *display = to_intel_display(iplane);
	struct drm_framebuffer *fb = plane_state->hw.fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);

	intel_panic_finish(intel_fb->panic);

	if (crtc_state->enable_psr2_sel_fetch) {
		/* Force a full update for psr2 */
		intel_psr2_panic_force_full_update(display, crtc_state);
	}

	/* Flush the cache and don't disable tiling if it's the fbdev framebuffer.*/
	if (intel_fb == intel_fbdev_framebuffer(display->fbdev.fbdev)) {
		struct iosys_map map;

		intel_fbdev_get_map(display->fbdev.fbdev, &map);
		drm_clflush_virt_range(map.vaddr, fb->pitches[0] * fb->height);
		return;
	}

	if (fb->modifier && iplane->disable_tiling)
		iplane->disable_tiling(iplane);
}

static unsigned int (*intel_get_tiling_func(u64 fb_modifier))(unsigned int width,
							      unsigned int x,
							      unsigned int y)
{
	switch (fb_modifier) {
	case I915_FORMAT_MOD_Y_TILED:
	case I915_FORMAT_MOD_Y_TILED_CCS:
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC:
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS:
	case I915_FORMAT_MOD_Y_TILED_GEN12_MC_CCS:
		return intel_ytile_get_offset;
	case I915_FORMAT_MOD_4_TILED:
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS:
	case I915_FORMAT_MOD_4_TILED_DG2_MC_CCS:
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC:
	case I915_FORMAT_MOD_4_TILED_MTL_RC_CCS:
	case I915_FORMAT_MOD_4_TILED_MTL_RC_CCS_CC:
	case I915_FORMAT_MOD_4_TILED_MTL_MC_CCS:
	case I915_FORMAT_MOD_4_TILED_BMG_CCS:
	case I915_FORMAT_MOD_4_TILED_LNL_CCS:
		return intel_4tile_get_offset;
	case I915_FORMAT_MOD_X_TILED:
	case I915_FORMAT_MOD_Yf_TILED:
	case I915_FORMAT_MOD_Yf_TILED_CCS:
	default:
	/* Not supported yet */
		return NULL;
	}
}

static int intel_get_scanout_buffer(struct drm_plane *plane,
				    struct drm_scanout_buffer *sb)
{
	struct intel_plane_state *plane_state;
	struct drm_gem_object *obj;
	struct drm_framebuffer *fb;
	struct intel_framebuffer *intel_fb;
	struct intel_display *display = to_intel_display(plane->dev);

	if (!plane->state || !plane->state->fb || !plane->state->visible)
		return -ENODEV;

	plane_state = to_intel_plane_state(plane->state);
	fb = plane_state->hw.fb;
	intel_fb = to_intel_framebuffer(fb);

	obj = intel_fb_bo(fb);
	if (!obj)
		return -ENODEV;

	if (intel_fb == intel_fbdev_framebuffer(display->fbdev.fbdev)) {
		intel_fbdev_get_map(display->fbdev.fbdev, &sb->map[0]);
	} else {
		int ret;
		/* Can't disable tiling if DPT is in use */
		if (intel_fb_uses_dpt(fb)) {
			if (fb->format->cpp[0] != 4)
				return -EOPNOTSUPP;
			intel_fb->panic_tiling = intel_get_tiling_func(fb->modifier);
			if (!intel_fb->panic_tiling)
				return -EOPNOTSUPP;
		}
		sb->private = intel_fb;
		ret = intel_panic_setup(intel_fb->panic, sb);
		if (ret)
			return ret;
	}
	sb->width = fb->width;
	sb->height = fb->height;
	/* Use the generic linear format, because tiling, RC, CCS, CC
	 * will be disabled in disable_tiling()
	 */
	sb->format = drm_format_info(fb->format->format);
	sb->pitch[0] = fb->pitches[0];

	return 0;
}

static const struct drm_plane_helper_funcs intel_plane_helper_funcs = {
	.prepare_fb = intel_prepare_plane_fb,
	.cleanup_fb = intel_cleanup_plane_fb,
};

static const struct drm_plane_helper_funcs intel_primary_plane_helper_funcs = {
	.prepare_fb = intel_prepare_plane_fb,
	.cleanup_fb = intel_cleanup_plane_fb,
	.get_scanout_buffer = intel_get_scanout_buffer,
	.panic_flush = intel_panic_flush,
};

void intel_plane_helper_add(struct intel_plane *plane)
{
	if (plane->base.type == DRM_PLANE_TYPE_PRIMARY)
		drm_plane_helper_add(&plane->base, &intel_primary_plane_helper_funcs);
	else
		drm_plane_helper_add(&plane->base, &intel_plane_helper_funcs);
}

void intel_plane_init_cursor_vblank_work(struct intel_plane_state *old_plane_state,
					 struct intel_plane_state *new_plane_state)
{
	if (!old_plane_state->ggtt_vma ||
	    old_plane_state->ggtt_vma == new_plane_state->ggtt_vma)
		return;

	drm_vblank_work_init(&old_plane_state->unpin_work, old_plane_state->uapi.crtc,
			     intel_cursor_unpin_work);
}

static void link_nv12_planes(struct intel_crtc_state *crtc_state,
			     struct intel_plane_state *uv_plane_state,
			     struct intel_plane_state *y_plane_state)
{
	struct intel_display *display = to_intel_display(uv_plane_state);
	struct intel_plane *uv_plane = to_intel_plane(uv_plane_state->uapi.plane);
	struct intel_plane *y_plane = to_intel_plane(y_plane_state->uapi.plane);

	drm_dbg_kms(display->drm, "UV plane [PLANE:%d:%s] using Y plane [PLANE:%d:%s]\n",
		    uv_plane->base.base.id, uv_plane->base.name,
		    y_plane->base.base.id, y_plane->base.name);

	uv_plane_state->planar_linked_plane = y_plane;

	y_plane_state->is_y_plane = true;
	y_plane_state->planar_linked_plane = uv_plane;

	crtc_state->enabled_planes |= BIT(y_plane->id);
	crtc_state->active_planes |= BIT(y_plane->id);
	crtc_state->update_planes |= BIT(y_plane->id);

	crtc_state->data_rate[y_plane->id] = crtc_state->data_rate_y[uv_plane->id];
	crtc_state->rel_data_rate[y_plane->id] = crtc_state->rel_data_rate_y[uv_plane->id];

	/* Copy parameters to Y plane */
	intel_plane_copy_hw_state(y_plane_state, uv_plane_state);
	y_plane_state->uapi.src = uv_plane_state->uapi.src;
	y_plane_state->uapi.dst = uv_plane_state->uapi.dst;

	y_plane_state->ctl = uv_plane_state->ctl;
	y_plane_state->color_ctl = uv_plane_state->color_ctl;
	y_plane_state->view = uv_plane_state->view;
	y_plane_state->decrypt = uv_plane_state->decrypt;

	icl_link_nv12_planes(uv_plane_state, y_plane_state);
}

static void unlink_nv12_plane(struct intel_crtc_state *crtc_state,
			      struct intel_plane_state *plane_state)
{
	struct intel_display *display = to_intel_display(plane_state);
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);

	plane_state->planar_linked_plane = NULL;

	if (!plane_state->is_y_plane)
		return;

	drm_WARN_ON(display->drm, plane_state->uapi.visible);

	plane_state->is_y_plane = false;

	crtc_state->enabled_planes &= ~BIT(plane->id);
	crtc_state->active_planes &= ~BIT(plane->id);
	crtc_state->update_planes |= BIT(plane->id);
	crtc_state->data_rate[plane->id] = 0;
	crtc_state->rel_data_rate[plane->id] = 0;
}

static int icl_check_nv12_planes(struct intel_atomic_state *state,
				 struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_plane_state *plane_state;
	struct intel_plane *plane;
	int i;

	if (DISPLAY_VER(display) < 11)
		return 0;

	/*
	 * Destroy all old plane links and make the Y plane invisible
	 * in the crtc_state->active_planes mask.
	 */
	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		if (plane->pipe != crtc->pipe)
			continue;

		if (plane_state->planar_linked_plane)
			unlink_nv12_plane(crtc_state, plane_state);
	}

	if (!crtc_state->nv12_planes)
		return 0;

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		struct intel_plane_state *y_plane_state = NULL;
		struct intel_plane *y_plane;

		if (plane->pipe != crtc->pipe)
			continue;

		if ((crtc_state->nv12_planes & BIT(plane->id)) == 0)
			continue;

		for_each_intel_plane_on_crtc(display->drm, crtc, y_plane) {
			if (!icl_is_nv12_y_plane(display, y_plane->id))
				continue;

			if (crtc_state->active_planes & BIT(y_plane->id))
				continue;

			y_plane_state = intel_atomic_get_plane_state(state, y_plane);
			if (IS_ERR(y_plane_state))
				return PTR_ERR(y_plane_state);

			break;
		}

		if (!y_plane_state) {
			drm_dbg_kms(display->drm,
				    "[CRTC:%d:%s] need %d free Y planes for planar YUV\n",
				    crtc->base.base.id, crtc->base.name,
				    hweight8(crtc_state->nv12_planes));
			return -EINVAL;
		}

		link_nv12_planes(crtc_state, plane_state, y_plane_state);
	}

	return 0;
}

static int intel_crtc_add_planes_to_state(struct intel_atomic_state *state,
					  struct intel_crtc *crtc,
					  u8 plane_ids_mask)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_plane *plane;

	for_each_intel_plane_on_crtc(display->drm, crtc, plane) {
		struct intel_plane_state *plane_state;

		if ((plane_ids_mask & BIT(plane->id)) == 0)
			continue;

		plane_state = intel_atomic_get_plane_state(state, plane);
		if (IS_ERR(plane_state))
			return PTR_ERR(plane_state);
	}

	return 0;
}

int intel_plane_add_affected(struct intel_atomic_state *state,
			     struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	return intel_crtc_add_planes_to_state(state, crtc,
					      old_crtc_state->enabled_planes |
					      new_crtc_state->enabled_planes);
}

static bool active_planes_affects_min_cdclk(struct intel_display *display)
{
	/* See {hsw,vlv,ivb}_plane_ratio() */
	return display->platform.broadwell || display->platform.haswell ||
		display->platform.cherryview || display->platform.valleyview ||
		display->platform.ivybridge;
}

static u8 intel_joiner_affected_planes(struct intel_atomic_state *state,
				       u8 joined_pipes)
{
	const struct intel_plane_state *plane_state;
	struct intel_plane *plane;
	u8 affected_planes = 0;
	int i;

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		struct intel_plane *linked = plane_state->planar_linked_plane;

		if ((joined_pipes & BIT(plane->pipe)) == 0)
			continue;

		affected_planes |= BIT(plane->id);
		if (linked)
			affected_planes |= BIT(linked->id);
	}

	return affected_planes;
}

static int intel_joiner_add_affected_planes(struct intel_atomic_state *state,
					    u8 joined_pipes)
{
	u8 prev_affected_planes, affected_planes = 0;

	/*
	 * We want all the joined pipes to have the same
	 * set of planes in the atomic state, to make sure
	 * state copying always works correctly, and the
	 * UV<->Y plane linkage is always up to date.
	 * Keep pulling planes in until we've determined
	 * the full set of affected planes. A bit complicated
	 * on account of each pipe being capable of selecting
	 * their own Y planes independently of the other pipes,
	 * and the selection being done from the set of
	 * inactive planes.
	 */
	do {
		struct intel_crtc *crtc;

		for_each_intel_crtc_in_pipe_mask(state->base.dev, crtc, joined_pipes) {
			int ret;

			ret = intel_crtc_add_planes_to_state(state, crtc, affected_planes);
			if (ret)
				return ret;
		}

		prev_affected_planes = affected_planes;
		affected_planes = intel_joiner_affected_planes(state, joined_pipes);
	} while (affected_planes != prev_affected_planes);

	return 0;
}

static int intel_add_affected_planes(struct intel_atomic_state *state)
{
	const struct intel_crtc_state *crtc_state;
	struct intel_crtc *crtc;
	int i;

	for_each_new_intel_crtc_in_state(state, crtc, crtc_state, i) {
		int ret;

		ret = intel_joiner_add_affected_planes(state, intel_crtc_joined_pipe_mask(crtc_state));
		if (ret)
			return ret;
	}

	return 0;
}

int intel_plane_atomic_check(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_crtc_state *old_crtc_state, *new_crtc_state;
	struct intel_plane_state __maybe_unused *plane_state;
	struct intel_plane *plane;
	struct intel_crtc *crtc;
	int i, ret;

	ret = intel_add_affected_planes(state);
	if (ret)
		return ret;

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		ret = plane_atomic_check(state, plane);
		if (ret) {
			drm_dbg_atomic(display->drm,
				       "[PLANE:%d:%s] atomic driver check failed\n",
				       plane->base.base.id, plane->base.name);
			return ret;
		}
	}

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i) {
		u8 old_active_planes, new_active_planes;

		ret = icl_check_nv12_planes(state, crtc);
		if (ret)
			return ret;

		/*
		 * On some platforms the number of active planes affects
		 * the planes' minimum cdclk calculation. Add such planes
		 * to the state before we compute the minimum cdclk.
		 */
		if (!active_planes_affects_min_cdclk(display))
			continue;

		old_active_planes = old_crtc_state->active_planes & ~BIT(PLANE_CURSOR);
		new_active_planes = new_crtc_state->active_planes & ~BIT(PLANE_CURSOR);

		if (hweight8(old_active_planes) == hweight8(new_active_planes))
			continue;

		ret = intel_crtc_add_planes_to_state(state, crtc, new_active_planes);
		if (ret)
			return ret;
	}

	return 0;
}
