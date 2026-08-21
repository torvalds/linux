// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_plane.h>
#include <drm/drm_colorop.h>

#include "dc.h"
#include "dal_asic_id.h"
#include "amdgpu.h"
#include "amdgpu_display.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_plane.h"
#include "amdgpu_dm_cursor.h"
#include "dm_helpers.h"

static int dm_check_cursor_fb(struct amdgpu_crtc *new_acrtc,
			      struct drm_plane_state *new_plane_state,
			      struct drm_framebuffer *fb)
{
	struct amdgpu_device *adev = drm_to_adev(new_acrtc->base.dev);
	struct amdgpu_framebuffer *afb = to_amdgpu_framebuffer(fb);
	unsigned int pitch;
	bool linear;

	if (fb->width > new_acrtc->max_cursor_width ||
	    fb->height > new_acrtc->max_cursor_height) {
		drm_dbg_atomic(adev_to_drm(adev), "Bad cursor FB size %dx%d\n",
				 new_plane_state->fb->width,
				 new_plane_state->fb->height);
		return -EINVAL;
	}
	if (new_plane_state->src_w != fb->width << 16 ||
	    new_plane_state->src_h != fb->height << 16) {
		drm_dbg_atomic(adev_to_drm(adev), "Cropping not supported for cursor plane\n");
		return -EINVAL;
	}

	/* Pitch in pixels */
	pitch = fb->pitches[0] / fb->format->cpp[0];

	if (fb->width != pitch) {
		drm_dbg_atomic(adev_to_drm(adev), "Cursor FB width %d doesn't match pitch %d",
				 fb->width, pitch);
		return -EINVAL;
	}

	switch (pitch) {
	case 64:
	case 128:
	case 256:
		/* FB pitch is supported by cursor plane */
		break;
	default:
		drm_dbg_atomic(adev_to_drm(adev), "Bad cursor FB pitch %d px\n", pitch);
		return -EINVAL;
	}

	/* Core DRM takes care of checking FB modifiers, so we only need to
	 * check tiling flags when the FB doesn't have a modifier.
	 */
	if (!(fb->flags & DRM_MODE_FB_MODIFIERS)) {
#if defined(CONFIG_DRM_AMD_DC_DCN6_0) || defined(CONFIG_DRM_AMD_DC_DCN5_0)
		if (adev->family == AMDGPU_FAMILY_GC_12_0_0
		    || adev->family == AMDGPU_FAMILY_GC_13_0_1) {
#else
		if (adev->family == AMDGPU_FAMILY_GC_12_0_0) {
#endif
			linear = AMDGPU_TILING_GET(afb->tiling_flags, GFX12_SWIZZLE_MODE) == 0;
		} else if (adev->family >= AMDGPU_FAMILY_AI) {
			linear = AMDGPU_TILING_GET(afb->tiling_flags, SWIZZLE_MODE) == 0;
		} else {
			linear = AMDGPU_TILING_GET(afb->tiling_flags, ARRAY_MODE) != DC_ARRAY_2D_TILED_THIN1 &&
				 AMDGPU_TILING_GET(afb->tiling_flags, ARRAY_MODE) != DC_ARRAY_1D_TILED_THIN1 &&
				 AMDGPU_TILING_GET(afb->tiling_flags, MICRO_TILE_MODE) == 0;
		}
		if (!linear) {
			drm_dbg_atomic(adev_to_drm(adev), "Cursor FB not linear");
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * Helper function for checking the cursor in native mode
 */
int amdgpu_dm_check_native_cursor_state(struct drm_crtc *new_plane_crtc,
					struct drm_plane *plane,
					struct drm_plane_state *new_plane_state,
					bool enable)
{

	struct amdgpu_crtc *new_acrtc;
	int ret;

	if (!enable || !new_plane_crtc ||
	    drm_atomic_plane_disabling(plane->state, new_plane_state))
		return 0;

	new_acrtc = to_amdgpu_crtc(new_plane_crtc);

	if (new_plane_state->src_x != 0 || new_plane_state->src_y != 0) {
		drm_dbg_atomic(new_plane_crtc->dev, "Cropping not supported for cursor plane\n");
		return -EINVAL;
	}

	if (new_plane_state->fb) {
		ret = dm_check_cursor_fb(new_acrtc, new_plane_state,
						new_plane_state->fb);
		if (ret)
			return ret;
	}

	return 0;
}

bool amdgpu_dm_should_update_native_cursor(struct drm_atomic_commit *state,
					   struct drm_crtc *old_plane_crtc,
					   struct drm_crtc *new_plane_crtc,
					   bool enable)
{
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct dm_crtc_state *dm_old_crtc_state, *dm_new_crtc_state;

	if (!enable) {
		if (old_plane_crtc == NULL)
			return true;

		old_crtc_state = drm_atomic_get_old_crtc_state(
			state, old_plane_crtc);
		dm_old_crtc_state = to_dm_crtc_state(old_crtc_state);

		return dm_old_crtc_state->cursor_mode == DM_CURSOR_NATIVE_MODE;
	}

	if (new_plane_crtc == NULL)
		return true;

	new_crtc_state = drm_atomic_get_new_crtc_state(
		state, new_plane_crtc);
	dm_new_crtc_state = to_dm_crtc_state(new_crtc_state);

	return dm_new_crtc_state->cursor_mode == DM_CURSOR_NATIVE_MODE;
}
EXPORT_IF_KUNIT(amdgpu_dm_should_update_native_cursor);

STATIC_IFN_KUNIT void dm_get_oriented_plane_size(struct drm_plane_state *plane_state,
					 int *src_w, int *src_h)
{
	switch (plane_state->rotation & DRM_MODE_ROTATE_MASK) {
	case DRM_MODE_ROTATE_90:
	case DRM_MODE_ROTATE_270:
		*src_w = plane_state->src_h >> 16;
		*src_h = plane_state->src_w >> 16;
		break;
	case DRM_MODE_ROTATE_0:
	case DRM_MODE_ROTATE_180:
	default:
		*src_w = plane_state->src_w >> 16;
		*src_h = plane_state->src_h >> 16;
		break;
	}
}
EXPORT_IF_KUNIT(dm_get_oriented_plane_size);

STATIC_IFN_KUNIT void
dm_get_plane_scale(struct drm_plane_state *plane_state,
		   int *out_plane_scale_w, int *out_plane_scale_h)
{
	int plane_src_w, plane_src_h;

	dm_get_oriented_plane_size(plane_state, &plane_src_w, &plane_src_h);
	*out_plane_scale_w = plane_src_w ? plane_state->crtc_w * 1000 / plane_src_w : 0;
	*out_plane_scale_h = plane_src_h ? plane_state->crtc_h * 1000 / plane_src_h : 0;
}
EXPORT_IF_KUNIT(dm_get_plane_scale);

/**
 * DOC: Cursor Modes - Native vs Overlay
 *
 * In native mode, the cursor uses a integrated cursor pipe within each DCN hw
 * plane. It does not require a dedicated hw plane to enable, but it is
 * subjected to the same z-order and scaling as the hw plane. It also has format
 * restrictions, a RGB cursor in native mode cannot be enabled within a non-RGB
 * hw plane.
 *
 * In overlay mode, the cursor uses a separate DCN hw plane, and thus has its
 * own scaling and z-pos. It also has no blending restrictions. It lends to a
 * cursor behavior more akin to a DRM client's expectations. However, it does
 * occupy an extra DCN plane, and therefore will only be used if a DCN plane is
 * available.
 */

/**
 * dm_plane_color_pipeline_active() - Check if a plane's color pipeline active.
 * @state: DRM atomic state
 * @plane: DRM plane to check
 * @use_old: if true, inspect the old colorop states; otherwise the new ones
 *
 * A color pipeline may be selected (color_pipeline != NULL) but still is
 * inactive if every colorop in the chain is bypassed.  Only return
 * true when at least one colorop has bypass == false, meaning the cursor
 * would be subjected to the transformation in native mode.
 *
 * Return: true if the pipeline modifies pixels, false otherwise.
 */
static bool dm_plane_color_pipeline_active(struct drm_atomic_commit *state,
					   struct drm_plane *plane,
					   bool use_old)
{
	struct drm_colorop *colorop;
	struct drm_colorop_state *old_colorop_state, *new_colorop_state;
	int i;

	for_each_oldnew_colorop_in_state(state, colorop, old_colorop_state, new_colorop_state, i) {
		struct drm_colorop_state *cstate = use_old ? old_colorop_state : new_colorop_state;

		if (cstate->colorop->plane != plane)
			continue;
		if (!cstate->bypass)
			return true;
	}
	return false;
}

/**
 * amdgpu_dm_crtc_get_cursor_mode() - Determine the required cursor mode on crtc
 * @adev: amdgpu device
 * @state: DRM atomic state
 * @dm_crtc_state: amdgpu state for the CRTC containing the cursor
 * @cursor_mode: Returns the required cursor mode on dm_crtc_state
 *
 * Get whether the cursor should be enabled in native mode, or overlay mode, on
 * the dm_crtc_state.
 *
 * The cursor should be enabled in overlay mode if there exists an underlying
 * plane - on which the cursor may be blended - that is either YUV formatted,
 * scaled differently from the cursor, or has a color pipeline active.
 *
 * Since zpos info is required, drm_atomic_normalize_zpos must be called before
 * calling this function.
 *
 * Return: 0 on success, or an error code if getting the cursor plane state
 * failed.
 */
int amdgpu_dm_crtc_get_cursor_mode(struct amdgpu_device *adev,
				   struct drm_atomic_commit *state,
				   struct dm_crtc_state *dm_crtc_state,
				   enum amdgpu_dm_cursor_mode *cursor_mode)
{
	struct drm_plane_state *old_plane_state, *plane_state, *cursor_state;
	struct drm_crtc_state *crtc_state = &dm_crtc_state->base;
	struct drm_plane *plane;
	bool consider_mode_change = false;
	bool entire_crtc_covered = false;
	bool cursor_changed = false;
	int underlying_scale_w, underlying_scale_h;
	int cursor_scale_w, cursor_scale_h;
	int i;

	/* Overlay cursor not supported on HW before DCN
	 * DCN401/420 does not have the cursor-on-scaled-plane or cursor-on-yuv-plane restrictions
	 * as previous DCN generations, so enable native mode on DCN401/420
	 *
	 * Always set native cursor mode when the CRTC is disabled,
	 * to make sure it doesn't cause atomic commits to fail when
	 * they are trying to disable the CRTC.
	 */
	if (amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(4, 0, 1) ||
	    amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(4, 2, 0) ||
#if defined(CONFIG_DRM_AMD_DC_DCN6_0)
	    amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(4, 2, 1) ||
	    amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(6, 0, 0) ||
#else
	    amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(4, 2, 1) ||
#endif
	    !dm_crtc_state->base.enable) {
		*cursor_mode = DM_CURSOR_NATIVE_MODE;
		return 0;
	}

	/* Init cursor_mode to be the same as current */
	*cursor_mode = dm_crtc_state->cursor_mode;

	/*
	 * Cursor mode can change if a plane's format changes, scale changes, is
	 * enabled/disabled, z-order changes, or color management properties change.
	 */
	for_each_oldnew_plane_in_state(state, plane, old_plane_state, plane_state, i) {
		int new_scale_w, new_scale_h, old_scale_w, old_scale_h;

		/* Only care about planes on this CRTC */
		if ((drm_plane_mask(plane) & crtc_state->plane_mask) == 0)
			continue;

		if (plane->type == DRM_PLANE_TYPE_CURSOR)
			cursor_changed = true;

		if (drm_atomic_plane_enabling(old_plane_state, plane_state) ||
		    drm_atomic_plane_disabling(old_plane_state, plane_state) ||
		    old_plane_state->fb->format != plane_state->fb->format) {
			consider_mode_change = true;
			break;
		}

		dm_get_plane_scale(plane_state, &new_scale_w, &new_scale_h);
		dm_get_plane_scale(old_plane_state, &old_scale_w, &old_scale_h);
		if (new_scale_w != old_scale_w || new_scale_h != old_scale_h) {
			consider_mode_change = true;
			break;
		}

		/*
		 * A non-cursor plane moving or resizing (without a scale change)
		 * changes how much of the CRTC it covers. This can create or
		 * remove a hole under the cursor and thus flip the required
		 * cursor mode (native vs overlay), so its destination rect must
		 * be re-evaluated too.
		 *
		 * The cursor plane itself is deliberately excluded: the cursor
		 * mode depends on the underlying planes' coverage, not on the
		 * cursor's position (see the entire_crtc_covered logic below).
		 * Triggering on cursor movement would force every legacy cursor
		 * update off its fast path, and in a cursor-only commit - where
		 * the underlying planes are not part of the state - the coverage
		 * loop would see no covering plane and misevaluate the mode as
		 * overlay, regressing flip-vs-cursor-legacy.
		 */
		if (plane->type != DRM_PLANE_TYPE_CURSOR &&
		    (old_plane_state->crtc_x != plane_state->crtc_x ||
		     old_plane_state->crtc_y != plane_state->crtc_y ||
		     old_plane_state->crtc_w != plane_state->crtc_w ||
		     old_plane_state->crtc_h != plane_state->crtc_h)) {
			consider_mode_change = true;
			break;
		}

		if (dm_plane_color_pipeline_active(state, plane, true) !=
		    dm_plane_color_pipeline_active(state, plane, false)) {
			consider_mode_change = true;
			break;
		}
	}

	if (!consider_mode_change && !crtc_state->zpos_changed)
		return 0;

	/*
	 * If no cursor change on this CRTC, and not enabled on this CRTC, then
	 * no need to set cursor mode. This avoids needlessly locking the cursor
	 * state.
	 */
	if (!cursor_changed &&
	    !(drm_plane_mask(crtc_state->crtc->cursor) & crtc_state->plane_mask)) {
		return 0;
	}

	cursor_state = drm_atomic_get_plane_state(state,
						  crtc_state->crtc->cursor);
	if (IS_ERR(cursor_state))
		return PTR_ERR(cursor_state);

	/* Cursor is disabled */
	if (!cursor_state->fb)
		return 0;

	/* For all planes in descending z-order (all of which are below cursor
	 * as per zpos definitions), check their scaling and format
	 */
	for_each_oldnew_plane_in_descending_zpos(state, plane, old_plane_state, plane_state) {

		/* Only care about non-cursor planes on this CRTC */
		if ((drm_plane_mask(plane) & crtc_state->plane_mask) == 0 ||
		    plane->type == DRM_PLANE_TYPE_CURSOR)
			continue;

		/* Underlying plane is YUV format - use overlay cursor */
		if (amdgpu_dm_plane_is_video_format(plane_state->fb->format->format)) {
			*cursor_mode = DM_CURSOR_OVERLAY_MODE;
			return 0;
		}

		/* Underlying plane has an active color pipeline - cursor would be transformed */
		if (dm_plane_color_pipeline_active(state, plane, false)) {
			*cursor_mode = DM_CURSOR_OVERLAY_MODE;
			return 0;
		}

		dm_get_plane_scale(plane_state,
				   &underlying_scale_w, &underlying_scale_h);
		dm_get_plane_scale(cursor_state,
				   &cursor_scale_w, &cursor_scale_h);

		/* Underlying plane has different scale - use overlay cursor */
		if (cursor_scale_w != underlying_scale_w &&
		    cursor_scale_h != underlying_scale_h) {
			*cursor_mode = DM_CURSOR_OVERLAY_MODE;
			return 0;
		}

		/* If this plane covers the whole CRTC, no need to check planes underneath */
		if (plane_state->crtc_x <= 0 && plane_state->crtc_y <= 0 &&
		    plane_state->crtc_x + plane_state->crtc_w >= crtc_state->mode.hdisplay &&
		    plane_state->crtc_y + plane_state->crtc_h >= crtc_state->mode.vdisplay) {
			entire_crtc_covered = true;
			break;
		}
	}

	/* If planes do not cover the entire CRTC, use overlay mode to enable
	 * cursor over holes
	 */
	if (entire_crtc_covered)
		*cursor_mode = DM_CURSOR_NATIVE_MODE;
	else
		*cursor_mode = DM_CURSOR_OVERLAY_MODE;

	return 0;
}
