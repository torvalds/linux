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

static void kfree_state(struct drm_atomic_state *state)
{
	kfree(state->connectors);
	kfree(state->connector_states);
	kfree(state->crtcs);
	kfree(state->crtc_states);
	kfree(state->planes);
	kfree(state->plane_states);
	kfree(state);
}

/**
 * drm_atomic_state_alloc - allocate atomic state
 * @dev: DRM device
 *
 * This allocates an empty atomic state to track updates.
 */
struct drm_atomic_state *
drm_atomic_state_alloc(struct drm_device *dev)
{
	struct drm_atomic_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	state->num_connector = ACCESS_ONCE(dev->mode_config.num_connector);

	state->crtcs = kcalloc(dev->mode_config.num_crtc,
			       sizeof(*state->crtcs), GFP_KERNEL);
	if (!state->crtcs)
		goto fail;
	state->crtc_states = kcalloc(dev->mode_config.num_crtc,
				     sizeof(*state->crtc_states), GFP_KERNEL);
	if (!state->crtc_states)
		goto fail;
	state->planes = kcalloc(dev->mode_config.num_total_plane,
				sizeof(*state->planes), GFP_KERNEL);
	if (!state->planes)
		goto fail;
	state->plane_states = kcalloc(dev->mode_config.num_total_plane,
				      sizeof(*state->plane_states), GFP_KERNEL);
	if (!state->plane_states)
		goto fail;
	state->connectors = kcalloc(state->num_connector,
				    sizeof(*state->connectors),
				    GFP_KERNEL);
	if (!state->connectors)
		goto fail;
	state->connector_states = kcalloc(state->num_connector,
					  sizeof(*state->connector_states),
					  GFP_KERNEL);
	if (!state->connector_states)
		goto fail;

	state->dev = dev;

	DRM_DEBUG_KMS("Allocate atomic state %p\n", state);

	return state;
fail:
	kfree_state(state);

	return NULL;
}
EXPORT_SYMBOL(drm_atomic_state_alloc);

/**
 * drm_atomic_state_clear - clear state object
 * @state: atomic state
 *
 * When the w/w mutex algorithm detects a deadlock we need to back off and drop
 * all locks. So someone else could sneak in and change the current modeset
 * configuration. Which means that all the state assembled in @state is no
 * longer an atomic update to the current state, but to some arbitrary earlier
 * state. Which could break assumptions the driver's ->atomic_check likely
 * relies on.
 *
 * Hence we must clear all cached state and completely start over, using this
 * function.
 */
void drm_atomic_state_clear(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_mode_config *config = &dev->mode_config;
	int i;

	DRM_DEBUG_KMS("Clearing atomic state %p\n", state);

	for (i = 0; i < state->num_connector; i++) {
		struct drm_connector *connector = state->connectors[i];

		if (!connector)
			continue;

		WARN_ON(!drm_modeset_is_locked(&config->connection_mutex));

		connector->funcs->atomic_destroy_state(connector,
						       state->connector_states[i]);
	}

	for (i = 0; i < config->num_crtc; i++) {
		struct drm_crtc *crtc = state->crtcs[i];

		if (!crtc)
			continue;

		crtc->funcs->atomic_destroy_state(crtc,
						  state->crtc_states[i]);
	}

	for (i = 0; i < config->num_total_plane; i++) {
		struct drm_plane *plane = state->planes[i];

		if (!plane)
			continue;

		plane->funcs->atomic_destroy_state(plane,
						   state->plane_states[i]);
	}
}
EXPORT_SYMBOL(drm_atomic_state_clear);

/**
 * drm_atomic_state_free - free all memory for an atomic state
 * @state: atomic state to deallocate
 *
 * This frees all memory associated with an atomic state, including all the
 * per-object state for planes, crtcs and connectors.
 */
void drm_atomic_state_free(struct drm_atomic_state *state)
{
	drm_atomic_state_clear(state);

	DRM_DEBUG_KMS("Freeing atomic state %p\n", state);

	kfree_state(state);
}
EXPORT_SYMBOL(drm_atomic_state_free);

/**
 * drm_atomic_get_crtc_state - get crtc state
 * @state: global atomic state object
 * @crtc: crtc to get state object for
 *
 * This function returns the crtc state for the given crtc, allocating it if
 * needed. It will also grab the relevant crtc lock to make sure that the state
 * is consistent.
 *
 * Returns:
 *
 * Either the allocated state or the error code encoded into the pointer. When
 * the error is EDEADLK then the w/w mutex code has detected a deadlock and the
 * entire atomic sequence must be restarted. All other errors are fatal.
 */
struct drm_crtc_state *
drm_atomic_get_crtc_state(struct drm_atomic_state *state,
			  struct drm_crtc *crtc)
{
	int ret, index;
	struct drm_crtc_state *crtc_state;

	index = drm_crtc_index(crtc);

	if (state->crtc_states[index])
		return state->crtc_states[index];

	ret = drm_modeset_lock(&crtc->mutex, state->acquire_ctx);
	if (ret)
		return ERR_PTR(ret);

	crtc_state = crtc->funcs->atomic_duplicate_state(crtc);
	if (!crtc_state)
		return ERR_PTR(-ENOMEM);

	state->crtc_states[index] = crtc_state;
	state->crtcs[index] = crtc;
	crtc_state->state = state;

	DRM_DEBUG_KMS("Added [CRTC:%d] %p state to %p\n",
		      crtc->base.id, crtc_state, state);

	return crtc_state;
}
EXPORT_SYMBOL(drm_atomic_get_crtc_state);

/**
 * drm_atomic_get_plane_state - get plane state
 * @state: global atomic state object
 * @plane: plane to get state object for
 *
 * This function returns the plane state for the given plane, allocating it if
 * needed. It will also grab the relevant plane lock to make sure that the state
 * is consistent.
 *
 * Returns:
 *
 * Either the allocated state or the error code encoded into the pointer. When
 * the error is EDEADLK then the w/w mutex code has detected a deadlock and the
 * entire atomic sequence must be restarted. All other errors are fatal.
 */
struct drm_plane_state *
drm_atomic_get_plane_state(struct drm_atomic_state *state,
			  struct drm_plane *plane)
{
	int ret, index;
	struct drm_plane_state *plane_state;

	index = drm_plane_index(plane);

	if (state->plane_states[index])
		return state->plane_states[index];

	ret = drm_modeset_lock(&plane->mutex, state->acquire_ctx);
	if (ret)
		return ERR_PTR(ret);

	plane_state = plane->funcs->atomic_duplicate_state(plane);
	if (!plane_state)
		return ERR_PTR(-ENOMEM);

	state->plane_states[index] = plane_state;
	state->planes[index] = plane;
	plane_state->state = state;

	DRM_DEBUG_KMS("Added [PLANE:%d] %p state to %p\n",
		      plane->base.id, plane_state, state);

	if (plane_state->crtc) {
		struct drm_crtc_state *crtc_state;

		crtc_state = drm_atomic_get_crtc_state(state,
						       plane_state->crtc);
		if (IS_ERR(crtc_state))
			return ERR_CAST(crtc_state);
	}

	return plane_state;
}
EXPORT_SYMBOL(drm_atomic_get_plane_state);

/**
 * drm_atomic_get_connector_state - get connector state
 * @state: global atomic state object
 * @connector: connector to get state object for
 *
 * This function returns the connector state for the given connector,
 * allocating it if needed. It will also grab the relevant connector lock to
 * make sure that the state is consistent.
 *
 * Returns:
 *
 * Either the allocated state or the error code encoded into the pointer. When
 * the error is EDEADLK then the w/w mutex code has detected a deadlock and the
 * entire atomic sequence must be restarted. All other errors are fatal.
 */
struct drm_connector_state *
drm_atomic_get_connector_state(struct drm_atomic_state *state,
			  struct drm_connector *connector)
{
	int ret, index;
	struct drm_mode_config *config = &connector->dev->mode_config;
	struct drm_connector_state *connector_state;

	ret = drm_modeset_lock(&config->connection_mutex, state->acquire_ctx);
	if (ret)
		return ERR_PTR(ret);

	index = drm_connector_index(connector);

	/*
	 * Construction of atomic state updates can race with a connector
	 * hot-add which might overflow. In this case flip the table and just
	 * restart the entire ioctl - no one is fast enough to livelock a cpu
	 * with physical hotplug events anyway.
	 *
	 * Note that we only grab the indexes once we have the right lock to
	 * prevent hotplug/unplugging of connectors. So removal is no problem,
	 * at most the array is a bit too large.
	 */
	if (index >= state->num_connector) {
		DRM_DEBUG_KMS("Hot-added connector would overflow state array, restarting\n");
		return ERR_PTR(-EAGAIN);
	}

	if (state->connector_states[index])
		return state->connector_states[index];

	connector_state = connector->funcs->atomic_duplicate_state(connector);
	if (!connector_state)
		return ERR_PTR(-ENOMEM);

	state->connector_states[index] = connector_state;
	state->connectors[index] = connector;
	connector_state->state = state;

	DRM_DEBUG_KMS("Added [CONNECTOR:%d] %p state to %p\n",
		      connector->base.id, connector_state, state);

	if (connector_state->crtc) {
		struct drm_crtc_state *crtc_state;

		crtc_state = drm_atomic_get_crtc_state(state,
						       connector_state->crtc);
		if (IS_ERR(crtc_state))
			return ERR_CAST(crtc_state);
	}

	return connector_state;
}
EXPORT_SYMBOL(drm_atomic_get_connector_state);

/**
 * drm_atomic_set_crtc_for_plane - set crtc for plane
 * @plane_state: the plane whose incoming state to update
 * @crtc: crtc to use for the plane
 *
 * Changing the assigned crtc for a plane requires us to grab the lock and state
 * for the new crtc, as needed. This function takes care of all these details
 * besides updating the pointer in the state object itself.
 *
 * Returns:
 * 0 on success or can fail with -EDEADLK or -ENOMEM. When the error is EDEADLK
 * then the w/w mutex code has detected a deadlock and the entire atomic
 * sequence must be restarted. All other errors are fatal.
 */
int
drm_atomic_set_crtc_for_plane(struct drm_plane_state *plane_state,
			      struct drm_crtc *crtc)
{
	struct drm_plane *plane = plane_state->plane;
	struct drm_crtc_state *crtc_state;

	if (plane_state->crtc) {
		crtc_state = drm_atomic_get_crtc_state(plane_state->state,
						       plane_state->crtc);
		if (WARN_ON(IS_ERR(crtc_state)))
			return PTR_ERR(crtc_state);

		crtc_state->plane_mask &= ~(1 << drm_plane_index(plane));
	}

	plane_state->crtc = crtc;

	if (crtc) {
		crtc_state = drm_atomic_get_crtc_state(plane_state->state,
						       crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);
		crtc_state->plane_mask |= (1 << drm_plane_index(plane));
	}

	if (crtc)
		DRM_DEBUG_KMS("Link plane state %p to [CRTC:%d]\n",
			      plane_state, crtc->base.id);
	else
		DRM_DEBUG_KMS("Link plane state %p to [NOCRTC]\n", plane_state);

	return 0;
}
EXPORT_SYMBOL(drm_atomic_set_crtc_for_plane);

/**
 * drm_atomic_set_fb_for_plane - set crtc for plane
 * @plane_state: atomic state object for the plane
 * @fb: fb to use for the plane
 *
 * Changing the assigned framebuffer for a plane requires us to grab a reference
 * to the new fb and drop the reference to the old fb, if there is one. This
 * function takes care of all these details besides updating the pointer in the
 * state object itself.
 */
void
drm_atomic_set_fb_for_plane(struct drm_plane_state *plane_state,
			    struct drm_framebuffer *fb)
{
	if (plane_state->fb)
		drm_framebuffer_unreference(plane_state->fb);
	if (fb)
		drm_framebuffer_reference(fb);
	plane_state->fb = fb;

	if (fb)
		DRM_DEBUG_KMS("Set [FB:%d] for plane state %p\n",
			      fb->base.id, plane_state);
	else
		DRM_DEBUG_KMS("Set [NOFB] for plane state %p\n", plane_state);
}
EXPORT_SYMBOL(drm_atomic_set_fb_for_plane);

/**
 * drm_atomic_set_crtc_for_connector - set crtc for connector
 * @conn_state: atomic state object for the connector
 * @crtc: crtc to use for the connector
 *
 * Changing the assigned crtc for a connector requires us to grab the lock and
 * state for the new crtc, as needed. This function takes care of all these
 * details besides updating the pointer in the state object itself.
 *
 * Returns:
 * 0 on success or can fail with -EDEADLK or -ENOMEM. When the error is EDEADLK
 * then the w/w mutex code has detected a deadlock and the entire atomic
 * sequence must be restarted. All other errors are fatal.
 */
int
drm_atomic_set_crtc_for_connector(struct drm_connector_state *conn_state,
				  struct drm_crtc *crtc)
{
	struct drm_crtc_state *crtc_state;

	if (crtc) {
		crtc_state = drm_atomic_get_crtc_state(conn_state->state, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);
	}

	conn_state->crtc = crtc;

	if (crtc)
		DRM_DEBUG_KMS("Link connector state %p to [CRTC:%d]\n",
			      conn_state, crtc->base.id);
	else
		DRM_DEBUG_KMS("Link connector state %p to [NOCRTC]\n",
			      conn_state);

	return 0;
}
EXPORT_SYMBOL(drm_atomic_set_crtc_for_connector);

/**
 * drm_atomic_add_affected_connectors - add connectors for crtc
 * @state: atomic state
 * @crtc: DRM crtc
 *
 * This function walks the current configuration and adds all connectors
 * currently using @crtc to the atomic configuration @state. Note that this
 * function must acquire the connection mutex. This can potentially cause
 * unneeded seralization if the update is just for the planes on one crtc. Hence
 * drivers and helpers should only call this when really needed (e.g. when a
 * full modeset needs to happen due to some change).
 *
 * Returns:
 * 0 on success or can fail with -EDEADLK or -ENOMEM. When the error is EDEADLK
 * then the w/w mutex code has detected a deadlock and the entire atomic
 * sequence must be restarted. All other errors are fatal.
 */
int
drm_atomic_add_affected_connectors(struct drm_atomic_state *state,
				   struct drm_crtc *crtc)
{
	struct drm_mode_config *config = &state->dev->mode_config;
	struct drm_connector *connector;
	struct drm_connector_state *conn_state;
	int ret;

	ret = drm_modeset_lock(&config->connection_mutex, state->acquire_ctx);
	if (ret)
		return ret;

	DRM_DEBUG_KMS("Adding all current connectors for [CRTC:%d] to %p\n",
		      crtc->base.id, state);

	/*
	 * Changed connectors are already in @state, so only need to look at the
	 * current configuration.
	 */
	list_for_each_entry(connector, &config->connector_list, head) {
		if (connector->state->crtc != crtc)
			continue;

		conn_state = drm_atomic_get_connector_state(state, connector);
		if (IS_ERR(conn_state))
			return PTR_ERR(conn_state);
	}

	return 0;
}
EXPORT_SYMBOL(drm_atomic_add_affected_connectors);

/**
 * drm_atomic_connectors_for_crtc - count number of connected outputs
 * @state: atomic state
 * @crtc: DRM crtc
 *
 * This function counts all connectors which will be connected to @crtc
 * according to @state. Useful to recompute the enable state for @crtc.
 */
int
drm_atomic_connectors_for_crtc(struct drm_atomic_state *state,
			       struct drm_crtc *crtc)
{
	int i, num_connected_connectors = 0;

	for (i = 0; i < state->num_connector; i++) {
		struct drm_connector_state *conn_state;

		conn_state = state->connector_states[i];

		if (conn_state && conn_state->crtc == crtc)
			num_connected_connectors++;
	}

	DRM_DEBUG_KMS("State %p has %i connectors for [CRTC:%d]\n",
		      state, num_connected_connectors, crtc->base.id);

	return num_connected_connectors;
}
EXPORT_SYMBOL(drm_atomic_connectors_for_crtc);

/**
 * drm_atomic_legacy_backoff - locking backoff for legacy ioctls
 * @state: atomic state
 *
 * This function should be used by legacy entry points which don't understand
 * -EDEADLK semantics. For simplicity this one will grab all modeset locks after
 *  the slowpath completed.
 */
void drm_atomic_legacy_backoff(struct drm_atomic_state *state)
{
	int ret;

retry:
	drm_modeset_backoff(state->acquire_ctx);

	ret = drm_modeset_lock(&state->dev->mode_config.connection_mutex,
			       state->acquire_ctx);
	if (ret)
		goto retry;
	ret = drm_modeset_lock_all_crtcs(state->dev,
					 state->acquire_ctx);
	if (ret)
		goto retry;
}
EXPORT_SYMBOL(drm_atomic_legacy_backoff);

/**
 * drm_atomic_check_only - check whether a given config would work
 * @state: atomic configuration to check
 *
 * Note that this function can return -EDEADLK if the driver needed to acquire
 * more locks but encountered a deadlock. The caller must then do the usual w/w
 * backoff dance and restart. All other errors are fatal.
 *
 * Returns:
 * 0 on success, negative error code on failure.
 */
int drm_atomic_check_only(struct drm_atomic_state *state)
{
	struct drm_mode_config *config = &state->dev->mode_config;

	DRM_DEBUG_KMS("checking %p\n", state);

	if (config->funcs->atomic_check)
		return config->funcs->atomic_check(state->dev, state);
	else
		return 0;
}
EXPORT_SYMBOL(drm_atomic_check_only);

/**
 * drm_atomic_commit - commit configuration atomically
 * @state: atomic configuration to check
 *
 * Note that this function can return -EDEADLK if the driver needed to acquire
 * more locks but encountered a deadlock. The caller must then do the usual w/w
 * backoff dance and restart. All other errors are fatal.
 *
 * Also note that on successful execution ownership of @state is transferred
 * from the caller of this function to the function itself. The caller must not
 * free or in any other way access @state. If the function fails then the caller
 * must clean up @state itself.
 *
 * Returns:
 * 0 on success, negative error code on failure.
 */
int drm_atomic_commit(struct drm_atomic_state *state)
{
	struct drm_mode_config *config = &state->dev->mode_config;
	int ret;

	ret = drm_atomic_check_only(state);
	if (ret)
		return ret;

	DRM_DEBUG_KMS("commiting %p\n", state);

	return config->funcs->atomic_commit(state->dev, state, false);
}
EXPORT_SYMBOL(drm_atomic_commit);

/**
 * drm_atomic_async_commit - atomic&async configuration commit
 * @state: atomic configuration to check
 *
 * Note that this function can return -EDEADLK if the driver needed to acquire
 * more locks but encountered a deadlock. The caller must then do the usual w/w
 * backoff dance and restart. All other errors are fatal.
 *
 * Also note that on successful execution ownership of @state is transferred
 * from the caller of this function to the function itself. The caller must not
 * free or in any other way access @state. If the function fails then the caller
 * must clean up @state itself.
 *
 * Returns:
 * 0 on success, negative error code on failure.
 */
int drm_atomic_async_commit(struct drm_atomic_state *state)
{
	struct drm_mode_config *config = &state->dev->mode_config;
	int ret;

	ret = drm_atomic_check_only(state);
	if (ret)
		return ret;

	DRM_DEBUG_KMS("commiting %p asynchronously\n", state);

	return config->funcs->atomic_commit(state->dev, state, true);
}
EXPORT_SYMBOL(drm_atomic_async_commit);
