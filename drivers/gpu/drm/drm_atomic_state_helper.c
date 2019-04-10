/*
 * Copyright (C) 2018 Intel Corp.
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

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_plane.h>
#include <drm/drm_connector.h>
#include <drm/drm_atomic.h>
#include <drm/drm_device.h>
#include <drm/drm_writeback.h>

#include <linux/slab.h>
#include <linux/dma-fence.h>

/**
 * DOC: atomic state reset and initialization
 *
 * Both the drm core and the atomic helpers assume that there is always the full
 * and correct atomic software state for all connectors, CRTCs and planes
 * available. Which is a bit a problem on driver load and also after system
 * suspend. One way to solve this is to have a hardware state read-out
 * infrastructure which reconstructs the full software state (e.g. the i915
 * driver).
 *
 * The simpler solution is to just reset the software state to everything off,
 * which is easiest to do by calling drm_mode_config_reset(). To facilitate this
 * the atomic helpers provide default reset implementations for all hooks.
 *
 * On the upside the precise state tracking of atomic simplifies system suspend
 * and resume a lot. For drivers using drm_mode_config_reset() a complete recipe
 * is implemented in drm_atomic_helper_suspend() and drm_atomic_helper_resume().
 * For other drivers the building blocks are split out, see the documentation
 * for these functions.
 */

/**
 * drm_atomic_helper_crtc_reset - default &drm_crtc_funcs.reset hook for CRTCs
 * @crtc: drm CRTC
 *
 * Resets the atomic state for @crtc by freeing the state pointer (which might
 * be NULL, e.g. at driver load time) and allocating a new empty state object.
 */
void drm_atomic_helper_crtc_reset(struct drm_crtc *crtc)
{
	if (crtc->state)
		__drm_atomic_helper_crtc_destroy_state(crtc->state);

	kfree(crtc->state);
	crtc->state = kzalloc(sizeof(*crtc->state), GFP_KERNEL);

	if (crtc->state)
		crtc->state->crtc = crtc;
}
EXPORT_SYMBOL(drm_atomic_helper_crtc_reset);

/**
 * __drm_atomic_helper_crtc_duplicate_state - copy atomic CRTC state
 * @crtc: CRTC object
 * @state: atomic CRTC state
 *
 * Copies atomic state from a CRTC's current state and resets inferred values.
 * This is useful for drivers that subclass the CRTC state.
 */
void __drm_atomic_helper_crtc_duplicate_state(struct drm_crtc *crtc,
					      struct drm_crtc_state *state)
{
	memcpy(state, crtc->state, sizeof(*state));

	if (state->mode_blob)
		drm_property_blob_get(state->mode_blob);
	if (state->degamma_lut)
		drm_property_blob_get(state->degamma_lut);
	if (state->ctm)
		drm_property_blob_get(state->ctm);
	if (state->gamma_lut)
		drm_property_blob_get(state->gamma_lut);
	state->mode_changed = false;
	state->active_changed = false;
	state->planes_changed = false;
	state->connectors_changed = false;
	state->color_mgmt_changed = false;
	state->zpos_changed = false;
	state->commit = NULL;
	state->event = NULL;
	state->pageflip_flags = 0;
}
EXPORT_SYMBOL(__drm_atomic_helper_crtc_duplicate_state);

/**
 * drm_atomic_helper_crtc_duplicate_state - default state duplicate hook
 * @crtc: drm CRTC
 *
 * Default CRTC state duplicate hook for drivers which don't have their own
 * subclassed CRTC state structure.
 */
struct drm_crtc_state *
drm_atomic_helper_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct drm_crtc_state *state;

	if (WARN_ON(!crtc->state))
		return NULL;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (state)
		__drm_atomic_helper_crtc_duplicate_state(crtc, state);

	return state;
}
EXPORT_SYMBOL(drm_atomic_helper_crtc_duplicate_state);

/**
 * __drm_atomic_helper_crtc_destroy_state - release CRTC state
 * @state: CRTC state object to release
 *
 * Releases all resources stored in the CRTC state without actually freeing
 * the memory of the CRTC state. This is useful for drivers that subclass the
 * CRTC state.
 */
void __drm_atomic_helper_crtc_destroy_state(struct drm_crtc_state *state)
{
	if (state->commit) {
		/*
		 * In the event that a non-blocking commit returns
		 * -ERESTARTSYS before the commit_tail work is queued, we will
		 * have an extra reference to the commit object. Release it, if
		 * the event has not been consumed by the worker.
		 *
		 * state->event may be freed, so we can't directly look at
		 * state->event->base.completion.
		 */
		if (state->event && state->commit->abort_completion)
			drm_crtc_commit_put(state->commit);

		kfree(state->commit->event);
		state->commit->event = NULL;

		drm_crtc_commit_put(state->commit);
	}

	drm_property_blob_put(state->mode_blob);
	drm_property_blob_put(state->degamma_lut);
	drm_property_blob_put(state->ctm);
	drm_property_blob_put(state->gamma_lut);
}
EXPORT_SYMBOL(__drm_atomic_helper_crtc_destroy_state);

/**
 * drm_atomic_helper_crtc_destroy_state - default state destroy hook
 * @crtc: drm CRTC
 * @state: CRTC state object to release
 *
 * Default CRTC state destroy hook for drivers which don't have their own
 * subclassed CRTC state structure.
 */
void drm_atomic_helper_crtc_destroy_state(struct drm_crtc *crtc,
					  struct drm_crtc_state *state)
{
	__drm_atomic_helper_crtc_destroy_state(state);
	kfree(state);
}
EXPORT_SYMBOL(drm_atomic_helper_crtc_destroy_state);

/**
 * __drm_atomic_helper_plane_reset - resets planes state to default values
 * @plane: plane object, must not be NULL
 * @state: atomic plane state, must not be NULL
 *
 * Initializes plane state to default. This is useful for drivers that subclass
 * the plane state.
 */
void __drm_atomic_helper_plane_reset(struct drm_plane *plane,
				     struct drm_plane_state *state)
{
	state->plane = plane;
	state->rotation = DRM_MODE_ROTATE_0;

	state->alpha = DRM_BLEND_ALPHA_OPAQUE;
	state->pixel_blend_mode = DRM_MODE_BLEND_PREMULTI;

	plane->state = state;
}
EXPORT_SYMBOL(__drm_atomic_helper_plane_reset);

/**
 * drm_atomic_helper_plane_reset - default &drm_plane_funcs.reset hook for planes
 * @plane: drm plane
 *
 * Resets the atomic state for @plane by freeing the state pointer (which might
 * be NULL, e.g. at driver load time) and allocating a new empty state object.
 */
void drm_atomic_helper_plane_reset(struct drm_plane *plane)
{
	if (plane->state)
		__drm_atomic_helper_plane_destroy_state(plane->state);

	kfree(plane->state);
	plane->state = kzalloc(sizeof(*plane->state), GFP_KERNEL);
	if (plane->state)
		__drm_atomic_helper_plane_reset(plane, plane->state);
}
EXPORT_SYMBOL(drm_atomic_helper_plane_reset);

/**
 * __drm_atomic_helper_plane_duplicate_state - copy atomic plane state
 * @plane: plane object
 * @state: atomic plane state
 *
 * Copies atomic state from a plane's current state. This is useful for
 * drivers that subclass the plane state.
 */
void __drm_atomic_helper_plane_duplicate_state(struct drm_plane *plane,
					       struct drm_plane_state *state)
{
	memcpy(state, plane->state, sizeof(*state));

	if (state->fb)
		drm_framebuffer_get(state->fb);

	state->fence = NULL;
	state->commit = NULL;
	state->fb_damage_clips = NULL;
}
EXPORT_SYMBOL(__drm_atomic_helper_plane_duplicate_state);

/**
 * drm_atomic_helper_plane_duplicate_state - default state duplicate hook
 * @plane: drm plane
 *
 * Default plane state duplicate hook for drivers which don't have their own
 * subclassed plane state structure.
 */
struct drm_plane_state *
drm_atomic_helper_plane_duplicate_state(struct drm_plane *plane)
{
	struct drm_plane_state *state;

	if (WARN_ON(!plane->state))
		return NULL;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (state)
		__drm_atomic_helper_plane_duplicate_state(plane, state);

	return state;
}
EXPORT_SYMBOL(drm_atomic_helper_plane_duplicate_state);

/**
 * __drm_atomic_helper_plane_destroy_state - release plane state
 * @state: plane state object to release
 *
 * Releases all resources stored in the plane state without actually freeing
 * the memory of the plane state. This is useful for drivers that subclass the
 * plane state.
 */
void __drm_atomic_helper_plane_destroy_state(struct drm_plane_state *state)
{
	if (state->fb)
		drm_framebuffer_put(state->fb);

	if (state->fence)
		dma_fence_put(state->fence);

	if (state->commit)
		drm_crtc_commit_put(state->commit);

	drm_property_blob_put(state->fb_damage_clips);
}
EXPORT_SYMBOL(__drm_atomic_helper_plane_destroy_state);

/**
 * drm_atomic_helper_plane_destroy_state - default state destroy hook
 * @plane: drm plane
 * @state: plane state object to release
 *
 * Default plane state destroy hook for drivers which don't have their own
 * subclassed plane state structure.
 */
void drm_atomic_helper_plane_destroy_state(struct drm_plane *plane,
					   struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(state);
	kfree(state);
}
EXPORT_SYMBOL(drm_atomic_helper_plane_destroy_state);

/**
 * __drm_atomic_helper_connector_reset - reset state on connector
 * @connector: drm connector
 * @conn_state: connector state to assign
 *
 * Initializes the newly allocated @conn_state and assigns it to
 * the &drm_conector->state pointer of @connector, usually required when
 * initializing the drivers or when called from the &drm_connector_funcs.reset
 * hook.
 *
 * This is useful for drivers that subclass the connector state.
 */
void
__drm_atomic_helper_connector_reset(struct drm_connector *connector,
				    struct drm_connector_state *conn_state)
{
	if (conn_state)
		conn_state->connector = connector;

	connector->state = conn_state;
}
EXPORT_SYMBOL(__drm_atomic_helper_connector_reset);

/**
 * drm_atomic_helper_connector_reset - default &drm_connector_funcs.reset hook for connectors
 * @connector: drm connector
 *
 * Resets the atomic state for @connector by freeing the state pointer (which
 * might be NULL, e.g. at driver load time) and allocating a new empty state
 * object.
 */
void drm_atomic_helper_connector_reset(struct drm_connector *connector)
{
	struct drm_connector_state *conn_state =
		kzalloc(sizeof(*conn_state), GFP_KERNEL);

	if (connector->state)
		__drm_atomic_helper_connector_destroy_state(connector->state);

	kfree(connector->state);
	__drm_atomic_helper_connector_reset(connector, conn_state);
}
EXPORT_SYMBOL(drm_atomic_helper_connector_reset);

/**
 * __drm_atomic_helper_connector_duplicate_state - copy atomic connector state
 * @connector: connector object
 * @state: atomic connector state
 *
 * Copies atomic state from a connector's current state. This is useful for
 * drivers that subclass the connector state.
 */
void
__drm_atomic_helper_connector_duplicate_state(struct drm_connector *connector,
					    struct drm_connector_state *state)
{
	memcpy(state, connector->state, sizeof(*state));
	if (state->crtc)
		drm_connector_get(connector);
	state->commit = NULL;

	/* Don't copy over a writeback job, they are used only once */
	state->writeback_job = NULL;
}
EXPORT_SYMBOL(__drm_atomic_helper_connector_duplicate_state);

/**
 * drm_atomic_helper_connector_duplicate_state - default state duplicate hook
 * @connector: drm connector
 *
 * Default connector state duplicate hook for drivers which don't have their own
 * subclassed connector state structure.
 */
struct drm_connector_state *
drm_atomic_helper_connector_duplicate_state(struct drm_connector *connector)
{
	struct drm_connector_state *state;

	if (WARN_ON(!connector->state))
		return NULL;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (state)
		__drm_atomic_helper_connector_duplicate_state(connector, state);

	return state;
}
EXPORT_SYMBOL(drm_atomic_helper_connector_duplicate_state);

/**
 * __drm_atomic_helper_connector_destroy_state - release connector state
 * @state: connector state object to release
 *
 * Releases all resources stored in the connector state without actually
 * freeing the memory of the connector state. This is useful for drivers that
 * subclass the connector state.
 */
void
__drm_atomic_helper_connector_destroy_state(struct drm_connector_state *state)
{
	if (state->crtc)
		drm_connector_put(state->connector);

	if (state->commit)
		drm_crtc_commit_put(state->commit);

	if (state->writeback_job)
		drm_writeback_cleanup_job(state->writeback_job);
}
EXPORT_SYMBOL(__drm_atomic_helper_connector_destroy_state);

/**
 * drm_atomic_helper_connector_destroy_state - default state destroy hook
 * @connector: drm connector
 * @state: connector state object to release
 *
 * Default connector state destroy hook for drivers which don't have their own
 * subclassed connector state structure.
 */
void drm_atomic_helper_connector_destroy_state(struct drm_connector *connector,
					  struct drm_connector_state *state)
{
	__drm_atomic_helper_connector_destroy_state(state);
	kfree(state);
}
EXPORT_SYMBOL(drm_atomic_helper_connector_destroy_state);

/**
 * __drm_atomic_helper_private_duplicate_state - copy atomic private state
 * @obj: CRTC object
 * @state: new private object state
 *
 * Copies atomic state from a private objects's current state and resets inferred values.
 * This is useful for drivers that subclass the private state.
 */
void __drm_atomic_helper_private_obj_duplicate_state(struct drm_private_obj *obj,
						     struct drm_private_state *state)
{
	memcpy(state, obj->state, sizeof(*state));
}
EXPORT_SYMBOL(__drm_atomic_helper_private_obj_duplicate_state);
