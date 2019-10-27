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

#include "i915_trace.h"
#include "intel_atomic_plane.h"
#include "intel_display_types.h"
#include "intel_pm.h"
#include "intel_sprite.h"

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

	__drm_atomic_helper_plane_reset(&plane->base, &plane_state->base);
	plane_state->scaler_id = -1;

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

unsigned int intel_plane_data_rate(const struct intel_crtc_state *crtc_state,
				   const struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->base.fb;
	unsigned int cpp;

	if (!plane_state->base.visible)
		return 0;

	cpp = fb->format->cpp[0];

	/*
	 * Based on HSD#:1408715493
	 * NV12 cpp == 4, P010 cpp == 8
	 *
	 * FIXME what is the logic behind this?
	 */
	if (fb->format->is_yuv && fb->format->num_planes > 1)
		cpp *= 4;

	return cpp * crtc_state->pixel_rate;
}

int intel_plane_atomic_check_with_state(const struct intel_crtc_state *old_crtc_state,
					struct intel_crtc_state *new_crtc_state,
					const struct intel_plane_state *old_plane_state,
					struct intel_plane_state *new_plane_state)
{
	struct intel_plane *plane = to_intel_plane(new_plane_state->base.plane);
	int ret;

	new_crtc_state->active_planes &= ~BIT(plane->id);
	new_crtc_state->nv12_planes &= ~BIT(plane->id);
	new_crtc_state->c8_planes &= ~BIT(plane->id);
	new_crtc_state->data_rate[plane->id] = 0;
	new_plane_state->base.visible = false;

	if (!new_plane_state->base.crtc && !old_plane_state->base.crtc)
		return 0;

	ret = plane->check_plane(new_crtc_state, new_plane_state);
	if (ret)
		return ret;

	/* FIXME pre-g4x don't work like this */
	if (new_plane_state->base.visible)
		new_crtc_state->active_planes |= BIT(plane->id);

	if (new_plane_state->base.visible &&
	    is_planar_yuv_format(new_plane_state->base.fb->format->format))
		new_crtc_state->nv12_planes |= BIT(plane->id);

	if (new_plane_state->base.visible &&
	    new_plane_state->base.fb->format->format == DRM_FORMAT_C8)
		new_crtc_state->c8_planes |= BIT(plane->id);

	if (new_plane_state->base.visible || old_plane_state->base.visible)
		new_crtc_state->update_planes |= BIT(plane->id);

	new_crtc_state->data_rate[plane->id] =
		intel_plane_data_rate(new_crtc_state, new_plane_state);

	return intel_plane_atomic_calc_changes(old_crtc_state, new_crtc_state,
					       old_plane_state, new_plane_state);
}

static struct intel_crtc *
get_crtc_from_states(const struct intel_plane_state *old_plane_state,
		     const struct intel_plane_state *new_plane_state)
{
	if (new_plane_state->base.crtc)
		return to_intel_crtc(new_plane_state->base.crtc);

	if (old_plane_state->base.crtc)
		return to_intel_crtc(old_plane_state->base.crtc);

	return NULL;
}

static int intel_plane_atomic_check(struct drm_plane *_plane,
				    struct drm_plane_state *_new_plane_state)
{
	struct intel_plane *plane = to_intel_plane(_plane);
	struct intel_atomic_state *state =
		to_intel_atomic_state(_new_plane_state->state);
	struct intel_plane_state *new_plane_state =
		to_intel_plane_state(_new_plane_state);
	const struct intel_plane_state *old_plane_state =
		intel_atomic_get_old_plane_state(state, plane);
	struct intel_crtc *crtc =
		get_crtc_from_states(old_plane_state, new_plane_state);
	const struct intel_crtc_state *old_crtc_state;
	struct intel_crtc_state *new_crtc_state;

	new_plane_state->base.visible = false;
	if (!crtc)
		return 0;

	old_crtc_state = intel_atomic_get_old_crtc_state(state, crtc);
	new_crtc_state = intel_atomic_get_new_crtc_state(state, crtc);

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
	WARN_ON(1);

	return NULL;
}

void intel_update_plane(struct intel_plane *plane,
			const struct intel_crtc_state *crtc_state,
			const struct intel_plane_state *plane_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);

	trace_intel_update_plane(&plane->base, crtc);
	plane->update_plane(plane, crtc_state, plane_state);
}

void intel_update_slave(struct intel_plane *plane,
			const struct intel_crtc_state *crtc_state,
			const struct intel_plane_state *plane_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);

	trace_intel_update_plane(&plane->base, crtc);
	plane->update_slave(plane, crtc_state, plane_state);
}

void intel_disable_plane(struct intel_plane *plane,
			 const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);

	trace_intel_disable_plane(&plane->base, crtc);
	plane->disable_plane(plane, crtc_state);
}

void skl_update_planes_on_crtc(struct intel_atomic_state *state,
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

		if (new_plane_state->base.visible) {
			intel_update_plane(plane, new_crtc_state, new_plane_state);
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
				intel_atomic_get_new_plane_state(state, master);

			intel_update_slave(plane, new_crtc_state, new_plane_state);
		} else {
			intel_disable_plane(plane, new_crtc_state);
		}
	}
}

void i9xx_update_planes_on_crtc(struct intel_atomic_state *state,
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

		if (new_plane_state->base.visible)
			intel_update_plane(plane, new_crtc_state, new_plane_state);
		else
			intel_disable_plane(plane, new_crtc_state);
	}
}

const struct drm_plane_helper_funcs intel_plane_helper_funcs = {
	.prepare_fb = intel_prepare_plane_fb,
	.cleanup_fb = intel_cleanup_plane_fb,
	.atomic_check = intel_plane_atomic_check,
};
