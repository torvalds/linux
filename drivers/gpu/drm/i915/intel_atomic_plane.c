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
	state->base.rotation = DRM_MODE_ROTATE_0;
	state->scaler_id = -1;

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
	intel_state->flags = 0;

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
	WARN_ON(to_intel_plane_state(state)->vma);

	drm_atomic_helper_plane_destroy_state(plane, state);
}

int intel_plane_atomic_check_with_state(const struct intel_crtc_state *old_crtc_state,
					struct intel_crtc_state *crtc_state,
					const struct intel_plane_state *old_plane_state,
					struct intel_plane_state *intel_state)
{
	struct drm_plane *plane = intel_state->base.plane;
	struct drm_plane_state *state = &intel_state->base;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	int ret;

	crtc_state->active_planes &= ~BIT(intel_plane->id);
	crtc_state->nv12_planes &= ~BIT(intel_plane->id);
	intel_state->base.visible = false;

	/* If this is a cursor plane, no further checks are needed. */
	if (!intel_state->base.crtc && !old_plane_state->base.crtc)
		return 0;

	ret = intel_plane->check_plane(crtc_state, intel_state);
	if (ret)
		return ret;

	/* FIXME pre-g4x don't work like this */
	if (state->visible)
		crtc_state->active_planes |= BIT(intel_plane->id);

	if (state->visible && state->fb->format->format == DRM_FORMAT_NV12)
		crtc_state->nv12_planes |= BIT(intel_plane->id);

	return intel_plane_atomic_calc_changes(old_crtc_state,
					       &crtc_state->base,
					       old_plane_state,
					       state);
}

static int intel_plane_atomic_check(struct drm_plane *plane,
				    struct drm_plane_state *new_plane_state)
{
	struct drm_atomic_state *state = new_plane_state->state;
	const struct drm_plane_state *old_plane_state =
		drm_atomic_get_old_plane_state(state, plane);
	struct drm_crtc *crtc = new_plane_state->crtc ?: old_plane_state->crtc;
	const struct drm_crtc_state *old_crtc_state;
	struct drm_crtc_state *new_crtc_state;

	new_plane_state->visible = false;
	if (!crtc)
		return 0;

	old_crtc_state = drm_atomic_get_old_crtc_state(state, crtc);
	new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	return intel_plane_atomic_check_with_state(to_intel_crtc_state(old_crtc_state),
						   to_intel_crtc_state(new_crtc_state),
						   to_intel_plane_state(old_plane_state),
						   to_intel_plane_state(new_plane_state));
}

void intel_update_planes_on_crtc(struct intel_atomic_state *old_state,
				 struct intel_crtc *crtc,
				 struct intel_crtc_state *old_crtc_state,
				 struct intel_crtc_state *new_crtc_state)
{
	struct intel_plane_state *new_plane_state;
	struct intel_plane *plane;
	u32 update_mask;
	int i;

	update_mask = old_crtc_state->active_planes;
	update_mask |= new_crtc_state->active_planes;

	for_each_new_intel_plane_in_state(old_state, plane, new_plane_state, i) {
		if (crtc->pipe != plane->pipe ||
		    !(update_mask & BIT(plane->id)))
			continue;

		if (new_plane_state->base.visible) {
			trace_intel_update_plane(&plane->base, crtc);

			plane->update_plane(plane, new_crtc_state, new_plane_state);
		} else if (new_plane_state->slave) {
			struct intel_plane *master =
				new_plane_state->linked_plane;

			/*
			 * We update the slave plane from this function because
			 * programming it from the master plane's update_plane
			 * callback runs into issues when the Y plane is
			 * reassigned, disabled or used by a different plane.
			 *
			 * The slave plane is updated with the master plane's
			 * plane_state.
			 */
			new_plane_state =
				intel_atomic_get_new_plane_state(old_state, master);

			trace_intel_update_plane(&plane->base, crtc);

			plane->update_slave(plane, new_crtc_state, new_plane_state);
		} else {
			trace_intel_disable_plane(&plane->base, crtc);

			plane->disable_plane(plane, crtc);
		}
	}
}

const struct drm_plane_helper_funcs intel_plane_helper_funcs = {
	.prepare_fb = intel_prepare_plane_fb,
	.cleanup_fb = intel_cleanup_plane_fb,
	.atomic_check = intel_plane_atomic_check,
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
	DRM_DEBUG_KMS("Unknown property [PROP:%d:%s]\n",
		      property->base.id, property->name);
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
	DRM_DEBUG_KMS("Unknown property [PROP:%d:%s]\n",
		      property->base.id, property->name);
	return -EINVAL;
}
