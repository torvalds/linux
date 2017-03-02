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
	state->base.rotation = DRM_ROTATE_0;
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

	intel_state->vma = NULL;

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
	struct i915_vma *vma;

	vma = fetch_and_zero(&to_intel_plane_state(state)->vma);

	/*
	 * FIXME: Normally intel_cleanup_plane_fb handles destruction of vma.
	 * We currently don't clear all planes during driver unload, so we have
	 * to be able to unpin vma here for now.
	 *
	 * Normally this can only happen during unload when kmscon is disabled
	 * and userspace doesn't attempt to set a framebuffer at all.
	 */
	if (vma) {
		mutex_lock(&plane->dev->struct_mutex);
		intel_unpin_fb_vma(vma);
		mutex_unlock(&plane->dev->struct_mutex);
	}

	drm_atomic_helper_plane_destroy_state(plane, state);
}

int intel_plane_atomic_check_with_state(struct intel_crtc_state *crtc_state,
					struct intel_plane_state *intel_state)
{
	struct drm_plane *plane = intel_state->base.plane;
	struct drm_i915_private *dev_priv = to_i915(plane->dev);
	struct drm_plane_state *state = &intel_state->base;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	int ret;

	/*
	 * Both crtc and plane->crtc could be NULL if we're updating a
	 * property while the plane is disabled.  We don't actually have
	 * anything driver-specific we need to test in that case, so
	 * just return success.
	 */
	if (!intel_state->base.crtc && !plane->state->crtc)
		return 0;

	/* Clip all planes to CRTC size, or 0x0 if CRTC is disabled */
	intel_state->clip.x1 = 0;
	intel_state->clip.y1 = 0;
	intel_state->clip.x2 =
		crtc_state->base.enable ? crtc_state->pipe_src_w : 0;
	intel_state->clip.y2 =
		crtc_state->base.enable ? crtc_state->pipe_src_h : 0;

	if (state->fb && drm_rotation_90_or_270(state->rotation)) {
		struct drm_format_name_buf format_name;

		if (state->fb->modifier != I915_FORMAT_MOD_Y_TILED &&
		    state->fb->modifier != I915_FORMAT_MOD_Yf_TILED) {
			DRM_DEBUG_KMS("Y/Yf tiling required for 90/270!\n");
			return -EINVAL;
		}

		/*
		 * 90/270 is not allowed with RGB64 16:16:16:16,
		 * RGB 16-bit 5:6:5, and Indexed 8-bit.
		 * TBD: Add RGB64 case once its added in supported format list.
		 */
		switch (state->fb->format->format) {
		case DRM_FORMAT_C8:
		case DRM_FORMAT_RGB565:
			DRM_DEBUG_KMS("Unsupported pixel format %s for 90/270!\n",
			              drm_get_format_name(state->fb->format->format,
			                                  &format_name));
			return -EINVAL;

		default:
			break;
		}
	}

	/* CHV ignores the mirror bit when the rotate bit is set :( */
	if (IS_CHERRYVIEW(dev_priv) &&
	    state->rotation & DRM_ROTATE_180 &&
	    state->rotation & DRM_REFLECT_X) {
		DRM_DEBUG_KMS("Cannot rotate and reflect at the same time\n");
		return -EINVAL;
	}

	intel_state->base.visible = false;
	ret = intel_plane->check_plane(plane, crtc_state, intel_state);
	if (ret)
		return ret;

	/* FIXME pre-g4x don't work like this */
	if (intel_state->base.visible)
		crtc_state->active_planes |= BIT(intel_plane->id);
	else
		crtc_state->active_planes &= ~BIT(intel_plane->id);

	return intel_plane_atomic_calc_changes(&crtc_state->base, state);
}

static int intel_plane_atomic_check(struct drm_plane *plane,
				    struct drm_plane_state *state)
{
	struct drm_crtc *crtc = state->crtc;
	struct drm_crtc_state *drm_crtc_state;

	crtc = crtc ? crtc : plane->state->crtc;

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

	return intel_plane_atomic_check_with_state(to_intel_crtc_state(drm_crtc_state),
						   to_intel_plane_state(state));
}

static void intel_plane_atomic_update(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct intel_plane_state *intel_state =
		to_intel_plane_state(plane->state);
	struct drm_crtc *crtc = plane->state->crtc ?: old_state->crtc;

	if (intel_state->base.visible)
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
