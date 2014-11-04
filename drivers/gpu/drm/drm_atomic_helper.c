/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2014 Intel Corp.
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
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Daniel Vetter <daniel.vetter@ffwll.ch>
 */

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_crtc_helper.h>

static void
drm_atomic_helper_plane_changed(struct drm_atomic_state *state,
				struct drm_plane_state *plane_state,
				struct drm_plane *plane)
{
	struct drm_crtc_state *crtc_state;

	if (plane->state->crtc) {
		crtc_state = state->crtc_states[drm_crtc_index(plane->crtc)];

		if (WARN_ON(!crtc_state))
			return;

		crtc_state->planes_changed = true;
	}

	if (plane_state->crtc) {
		crtc_state =
			state->crtc_states[drm_crtc_index(plane_state->crtc)];

		if (WARN_ON(!crtc_state))
			return;

		crtc_state->planes_changed = true;
	}
}

/**
 * drm_atomic_helper_check - validate state object
 * @dev: DRM device
 * @state: the driver state object
 *
 * Check the state object to see if the requested state is physically possible.
 * Only crtcs and planes have check callbacks, so for any additional (global)
 * checking that a driver needs it can simply wrap that around this function.
 * Drivers without such needs can directly use this as their ->atomic_check()
 * callback.
 *
 * RETURNS
 * Zero for success or -errno
 */
int drm_atomic_helper_check(struct drm_device *dev,
			    struct drm_atomic_state *state)
{
	int nplanes = dev->mode_config.num_total_plane;
	int ncrtcs = dev->mode_config.num_crtc;
	int i, ret = 0;

	for (i = 0; i < nplanes; i++) {
		struct drm_plane_helper_funcs *funcs;
		struct drm_plane *plane = state->planes[i];
		struct drm_plane_state *plane_state = state->plane_states[i];

		if (!plane)
			continue;

		funcs = plane->helper_private;

		drm_atomic_helper_plane_changed(state, plane_state, plane);

		if (!funcs || !funcs->atomic_check)
			continue;

		ret = funcs->atomic_check(plane, plane_state);
		if (ret) {
			DRM_DEBUG_KMS("[PLANE:%d] atomic check failed\n",
				      plane->base.id);
			return ret;
		}
	}

	for (i = 0; i < ncrtcs; i++) {
		struct drm_crtc_helper_funcs *funcs;
		struct drm_crtc *crtc = state->crtcs[i];

		if (!crtc)
			continue;

		funcs = crtc->helper_private;

		if (!funcs || !funcs->atomic_check)
			continue;

		ret = funcs->atomic_check(crtc, state->crtc_states[i]);
		if (ret) {
			DRM_DEBUG_KMS("[CRTC:%d] atomic check failed\n",
				      crtc->base.id);
			return ret;
		}
	}

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_check);

/**
 * drm_atomic_helper_prepare_planes - prepare plane resources after commit
 * @dev: DRM device
 * @state: atomic state object with old state structures
 *
 * This function prepares plane state, specifically framebuffers, for the new
 * configuration. If any failure is encountered this function will call
 * ->cleanup_fb on any already successfully prepared framebuffer.
 *
 * Returns:
 * 0 on success, negative error code on failure.
 */
int drm_atomic_helper_prepare_planes(struct drm_device *dev,
				     struct drm_atomic_state *state)
{
	int nplanes = dev->mode_config.num_total_plane;
	int ret, i;

	for (i = 0; i < nplanes; i++) {
		struct drm_plane_helper_funcs *funcs;
		struct drm_plane *plane = state->planes[i];
		struct drm_framebuffer *fb;

		if (!plane)
			continue;

		funcs = plane->helper_private;

		fb = state->plane_states[i]->fb;

		if (fb && funcs->prepare_fb) {
			ret = funcs->prepare_fb(plane, fb);
			if (ret)
				goto fail;
		}
	}

	return 0;

fail:
	for (i--; i >= 0; i--) {
		struct drm_plane_helper_funcs *funcs;
		struct drm_plane *plane = state->planes[i];
		struct drm_framebuffer *fb;

		if (!plane)
			continue;

		funcs = plane->helper_private;

		fb = state->plane_states[i]->fb;

		if (fb && funcs->cleanup_fb)
			funcs->cleanup_fb(plane, fb);

	}

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_prepare_planes);

/**
 * drm_atomic_helper_commit_planes - commit plane state
 * @dev: DRM device
 * @state: atomic state
 *
 * This function commits the new plane state using the plane and atomic helper
 * functions for planes and crtcs. It assumes that the atomic state has already
 * been pushed into the relevant object state pointers, since this step can no
 * longer fail.
 *
 * It still requires the global state object @state to know which planes and
 * crtcs need to be updated though.
 */
void drm_atomic_helper_commit_planes(struct drm_device *dev,
				     struct drm_atomic_state *state)
{
	int nplanes = dev->mode_config.num_total_plane;
	int ncrtcs = dev->mode_config.num_crtc;
	int i;

	for (i = 0; i < ncrtcs; i++) {
		struct drm_crtc_helper_funcs *funcs;
		struct drm_crtc *crtc = state->crtcs[i];

		if (!crtc)
			continue;

		funcs = crtc->helper_private;

		if (!funcs || !funcs->atomic_begin)
			continue;

		funcs->atomic_begin(crtc);
	}

	for (i = 0; i < nplanes; i++) {
		struct drm_plane_helper_funcs *funcs;
		struct drm_plane *plane = state->planes[i];

		if (!plane)
			continue;

		funcs = plane->helper_private;

		if (!funcs || !funcs->atomic_update)
			continue;

		funcs->atomic_update(plane);
	}

	for (i = 0; i < ncrtcs; i++) {
		struct drm_crtc_helper_funcs *funcs;
		struct drm_crtc *crtc = state->crtcs[i];

		if (!crtc)
			continue;

		funcs = crtc->helper_private;

		if (!funcs || !funcs->atomic_flush)
			continue;

		funcs->atomic_flush(crtc);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_commit_planes);

/**
 * drm_atomic_helper_cleanup_planes - cleanup plane resources after commit
 * @dev: DRM device
 * @old_state: atomic state object with old state structures
 *
 * This function cleans up plane state, specifically framebuffers, from the old
 * configuration. Hence the old configuration must be perserved in @old_state to
 * be able to call this function.
 *
 * This function must also be called on the new state when the atomic update
 * fails at any point after calling drm_atomic_helper_prepare_planes().
 */
void drm_atomic_helper_cleanup_planes(struct drm_device *dev,
				      struct drm_atomic_state *old_state)
{
	int nplanes = dev->mode_config.num_total_plane;
	int i;

	for (i = 0; i < nplanes; i++) {
		struct drm_plane_helper_funcs *funcs;
		struct drm_plane *plane = old_state->planes[i];
		struct drm_framebuffer *old_fb;

		if (!plane)
			continue;

		funcs = plane->helper_private;

		old_fb = old_state->plane_states[i]->fb;

		if (old_fb && funcs->cleanup_fb)
			funcs->cleanup_fb(plane, old_fb);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_cleanup_planes);

/**
 * drm_atomic_helper_swap_state - store atomic state into current sw state
 * @dev: DRM device
 * @state: atomic state
 *
 * This function stores the atomic state into the current state pointers in all
 * driver objects. It should be called after all failing steps have been done
 * and succeeded, but before the actual hardware state is committed.
 *
 * For cleanup and error recovery the current state for all changed objects will
 * be swaped into @state.
 *
 * With that sequence it fits perfectly into the plane prepare/cleanup sequence:
 *
 * 1. Call drm_atomic_helper_prepare_planes() with the staged atomic state.
 *
 * 2. Do any other steps that might fail.
 *
 * 3. Put the staged state into the current state pointers with this function.
 *
 * 4. Actually commit the hardware state.
 *
 * 5. Call drm_atomic_helper_cleanup_planes with @state, which since step 3
 * contains the old state. Also do any other cleanup required with that state.
 */
void drm_atomic_helper_swap_state(struct drm_device *dev,
				  struct drm_atomic_state *state)
{
	int i;

	for (i = 0; i < dev->mode_config.num_connector; i++) {
		struct drm_connector *connector = state->connectors[i];

		if (!connector)
			continue;

		connector->state->state = state;
		swap(state->connector_states[i], connector->state);
		connector->state->state = NULL;
	}

	for (i = 0; i < dev->mode_config.num_crtc; i++) {
		struct drm_crtc *crtc = state->crtcs[i];

		if (!crtc)
			continue;

		crtc->state->state = state;
		swap(state->crtc_states[i], crtc->state);
		crtc->state->state = NULL;
	}

	for (i = 0; i < dev->mode_config.num_total_plane; i++) {
		struct drm_plane *plane = state->planes[i];

		if (!plane)
			continue;

		plane->state->state = state;
		swap(state->plane_states[i], plane->state);
		plane->state->state = NULL;
	}
}
EXPORT_SYMBOL(drm_atomic_helper_swap_state);
