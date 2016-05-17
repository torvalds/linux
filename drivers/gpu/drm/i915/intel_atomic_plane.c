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

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include "intel_drv.h"

/**
 * intel_create_plane_state - create plane state object
 * @plane: drm plane
 *
 * Allocates a fresh plane state for the given plane and sets some of
 * the state values to sensible initial values.
 *
 * Returns: A newly allocated plane state, or NULL on failure
 */
struct intel_plane_state *
intel_create_plane_state(struct drm_plane *plane)
{
	struct intel_plane_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	state->base.plane = plane;
	state->base.rotation = BIT(DRM_ROTATE_0);
	state->ckey.flags = I915_SET_COLORKEY_NONE;

	return state;
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
	struct drm_plane_state *state;
	struct intel_plane_state *intel_state;

	intel_state = kmemdup(plane->state, sizeof(*intel_state), GFP_KERNEL);

	if (!intel_state)
		return NULL;

	state = &intel_state->base;

	__drm_atomic_helper_plane_duplicate_state(plane, state);
	intel_state->wait_req = NULL;

	return state;
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
	WARN_ON(state && to_intel_plane_state(state)->wait_req);
	WARN_ON(state && state->fence);
	drm_atomic_helper_plane_destroy_state(plane, state);
}

static int intel_plane_atomic_check(struct drm_plane *plane,
				    struct drm_plane_state *state)
{
	struct drm_crtc *crtc = state->crtc;
	struct intel_crtc *intel_crtc;
	struct intel_crtc_state *crtc_state;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct intel_plane_state *intel_state = to_intel_plane_state(state);
	struct drm_crtc_state *drm_crtc_state;
	int ret;

	crtc = crtc ? crtc : plane->state->crtc;
	intel_crtc = to_intel_crtc(crtc);

	/*
	 * Both crtc and plane->crtc could be NULL if we're updating a
	 * property while the plane is disabled.  We don't actually have
	 * anything driver-specific we need to test in that case, so
	 * just return success.
	 */
	if (!crtc)
		return 0;

	drm_crtc_state = drm_atomic_get_existing_crtc_state(state->state, crtc);
	if (WARN_ON(!drm_crtc_state))
		return -EINVAL;

	crtc_state = to_intel_crtc_state(drm_crtc_state);

	/*
	 * The original src/dest coordinates are stored in state->base, but
	 * we want to keep another copy internal to our driver that we can
	 * clip/modify ourselves.
	 */
	intel_state->src.x1 = state->src_x;
	intel_state->src.y1 = state->src_y;
	intel_state->src.x2 = state->src_x + state->src_w;
	intel_state->src.y2 = state->src_y + state->src_h;
	intel_state->dst.x1 = state->crtc_x;
	intel_state->dst.y1 = state->crtc_y;
	intel_state->dst.x2 = state->crtc_x + state->crtc_w;
	intel_state->dst.y2 = state->crtc_y + state->crtc_h;

	/* Clip all planes to CRTC size, or 0x0 if CRTC is disabled */
	intel_state->clip.x1 = 0;
	intel_state->clip.y1 = 0;
	intel_state->clip.x2 =
		crtc_state->base.enable ? crtc_state->pipe_src_w : 0;
	intel_state->clip.y2 =
		crtc_state->base.enable ? crtc_state->pipe_src_h : 0;

	if (state->fb && intel_rotation_90_or_270(state->rotation)) {
		if (!(state->fb->modifier[0] == I915_FORMAT_MOD_Y_TILED ||
			state->fb->modifier[0] == I915_FORMAT_MOD_Yf_TILED)) {
			DRM_DEBUG_KMS("Y/Yf tiling required for 90/270!\n");
			return -EINVAL;
		}

		/*
		 * 90/270 is not allowed with RGB64 16:16:16:16,
		 * RGB 16-bit 5:6:5, and Indexed 8-bit.
		 * TBD: Add RGB64 case once its added in supported format list.
		 */
		switch (state->fb->pixel_format) {
		case DRM_FORMAT_C8:
		case DRM_FORMAT_RGB565:
			DRM_DEBUG_KMS("Unsupported pixel format %s for 90/270!\n",
					drm_get_format_name(state->fb->pixel_format));
			return -EINVAL;

		default:
			break;
		}
	}

	intel_state->visible = false;
	ret = intel_plane->check_plane(plane, crtc_state, intel_state);
	if (ret)
		return ret;

	return intel_plane_atomic_calc_changes(&crtc_state->base, state);
}

static void intel_plane_atomic_update(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct intel_plane_state *intel_state =
		to_intel_plane_state(plane->state);
	struct drm_crtc *crtc = plane->state->crtc ?: old_state->crtc;

	if (intel_state->visible)
		intel_plane->update_plane(plane,
					  to_intel_crtc_state(crtc->state),
					  intel_state);
	else
		intel_plane->disable_plane(plane, crtc);
}

const struct drm_plane_helper_funcs intel_plane_helper_funcs = {
	.prepare_fb = intel_prepare_plane_fb,
	.cleanup_fb = intel_cleanup_plane_fb,
	.atomic_check = intel_plane_atomic_check,
	.atomic_update = intel_plane_atomic_update,
};

/**
 * intel_plane_atomic_get_property - fetch plane property value
 * @plane: plane to fetch property for
 * @state: state containing the property value
 * @property: property to look up
 * @val: pointer to write property value into
 *
 * The DRM core does not store shadow copies of properties for
 * atomic-capable drivers.  This entrypoint is used to fetch
 * the current value of a driver-specific plane property.
 */
int
intel_plane_atomic_get_property(struct drm_plane *plane,
				const struct drm_plane_state *state,
				struct drm_property *property,
				uint64_t *val)
{
	DRM_DEBUG_KMS("Unknown plane property '%s'\n", property->name);
	return -EINVAL;
}

/**
 * intel_plane_atomic_set_property - set plane property value
 * @plane: plane to set property for
 * @state: state to update property value in
 * @property: property to set
 * @val: value to set property to
 *
 * Writes the specified property value for a plane into the provided atomic
 * state object.
 *
 * Returns 0 on success, -EINVAL on unrecognized properties
 */
int
intel_plane_atomic_set_property(struct drm_plane *plane,
				struct drm_plane_state *state,
				struct drm_property *property,
				uint64_t val)
{
	DRM_DEBUG_KMS("Unknown plane property '%s'\n", property->name);
	return -EINVAL;
}
