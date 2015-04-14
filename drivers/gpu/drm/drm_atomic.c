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

	/* TODO legacy paths should maybe do a better job about
	 * setting this appropriately?
	 */
	state->allow_modeset = true;

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

	DRM_DEBUG_ATOMIC("Allocate atomic state %p\n", state);

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

	DRM_DEBUG_ATOMIC("Clearing atomic state %p\n", state);

	for (i = 0; i < state->num_connector; i++) {
		struct drm_connector *connector = state->connectors[i];

		if (!connector)
			continue;

		WARN_ON(!drm_modeset_is_locked(&config->connection_mutex));

		connector->funcs->atomic_destroy_state(connector,
						       state->connector_states[i]);
		state->connectors[i] = NULL;
		state->connector_states[i] = NULL;
	}

	for (i = 0; i < config->num_crtc; i++) {
		struct drm_crtc *crtc = state->crtcs[i];

		if (!crtc)
			continue;

		crtc->funcs->atomic_destroy_state(crtc,
						  state->crtc_states[i]);
		state->crtcs[i] = NULL;
		state->crtc_states[i] = NULL;
	}

	for (i = 0; i < config->num_total_plane; i++) {
		struct drm_plane *plane = state->planes[i];

		if (!plane)
			continue;

		plane->funcs->atomic_destroy_state(plane,
						   state->plane_states[i]);
		state->planes[i] = NULL;
		state->plane_states[i] = NULL;
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
	if (!state)
		return;

	drm_atomic_state_clear(state);

	DRM_DEBUG_ATOMIC("Freeing atomic state %p\n", state);

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

	DRM_DEBUG_ATOMIC("Added [CRTC:%d] %p state to %p\n",
			 crtc->base.id, crtc_state, state);

	return crtc_state;
}
EXPORT_SYMBOL(drm_atomic_get_crtc_state);

/**
 * drm_atomic_crtc_set_property - set property on CRTC
 * @crtc: the drm CRTC to set a property on
 * @state: the state object to update with the new property value
 * @property: the property to set
 * @val: the new property value
 *
 * Use this instead of calling crtc->atomic_set_property directly.
 * This function handles generic/core properties and calls out to
 * driver's ->atomic_set_property() for driver properties.  To ensure
 * consistent behavior you must call this function rather than the
 * driver hook directly.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
int drm_atomic_crtc_set_property(struct drm_crtc *crtc,
		struct drm_crtc_state *state, struct drm_property *property,
		uint64_t val)
{
	struct drm_device *dev = crtc->dev;
	struct drm_mode_config *config = &dev->mode_config;

	/* FIXME: Mode prop is missing, which also controls ->enable. */
	if (property == config->prop_active)
		state->active = val;
	else if (crtc->funcs->atomic_set_property)
		return crtc->funcs->atomic_set_property(crtc, state, property, val);
	else
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(drm_atomic_crtc_set_property);

/*
 * This function handles generic/core properties and calls out to
 * driver's ->atomic_get_property() for driver properties.  To ensure
 * consistent behavior you must call this function rather than the
 * driver hook directly.
 */
int drm_atomic_crtc_get_property(struct drm_crtc *crtc,
		const struct drm_crtc_state *state,
		struct drm_property *property, uint64_t *val)
{
	struct drm_device *dev = crtc->dev;
	struct drm_mode_config *config = &dev->mode_config;

	if (property == config->prop_active)
		*val = state->active;
	else if (crtc->funcs->atomic_get_property)
		return crtc->funcs->atomic_get_property(crtc, state, property, val);
	else
		return -EINVAL;

	return 0;
}

/**
 * drm_atomic_crtc_check - check crtc state
 * @crtc: crtc to check
 * @state: crtc state to check
 *
 * Provides core sanity checks for crtc state.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
static int drm_atomic_crtc_check(struct drm_crtc *crtc,
		struct drm_crtc_state *state)
{
	/* NOTE: we explicitly don't enforce constraints such as primary
	 * layer covering entire screen, since that is something we want
	 * to allow (on hw that supports it).  For hw that does not, it
	 * should be checked in driver's crtc->atomic_check() vfunc.
	 *
	 * TODO: Add generic modeset state checks once we support those.
	 */

	if (state->active && !state->enable) {
		DRM_DEBUG_ATOMIC("[CRTC:%d] active without enabled\n",
				 crtc->base.id);
		return -EINVAL;
	}

	return 0;
}

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

	DRM_DEBUG_ATOMIC("Added [PLANE:%d] %p state to %p\n",
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
 * drm_atomic_plane_set_property - set property on plane
 * @plane: the drm plane to set a property on
 * @state: the state object to update with the new property value
 * @property: the property to set
 * @val: the new property value
 *
 * Use this instead of calling plane->atomic_set_property directly.
 * This function handles generic/core properties and calls out to
 * driver's ->atomic_set_property() for driver properties.  To ensure
 * consistent behavior you must call this function rather than the
 * driver hook directly.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
int drm_atomic_plane_set_property(struct drm_plane *plane,
		struct drm_plane_state *state, struct drm_property *property,
		uint64_t val)
{
	struct drm_device *dev = plane->dev;
	struct drm_mode_config *config = &dev->mode_config;

	if (property == config->prop_fb_id) {
		struct drm_framebuffer *fb = drm_framebuffer_lookup(dev, val);
		drm_atomic_set_fb_for_plane(state, fb);
		if (fb)
			drm_framebuffer_unreference(fb);
	} else if (property == config->prop_crtc_id) {
		struct drm_crtc *crtc = drm_crtc_find(dev, val);
		return drm_atomic_set_crtc_for_plane(state, crtc);
	} else if (property == config->prop_crtc_x) {
		state->crtc_x = U642I64(val);
	} else if (property == config->prop_crtc_y) {
		state->crtc_y = U642I64(val);
	} else if (property == config->prop_crtc_w) {
		state->crtc_w = val;
	} else if (property == config->prop_crtc_h) {
		state->crtc_h = val;
	} else if (property == config->prop_src_x) {
		state->src_x = val;
	} else if (property == config->prop_src_y) {
		state->src_y = val;
	} else if (property == config->prop_src_w) {
		state->src_w = val;
	} else if (property == config->prop_src_h) {
		state->src_h = val;
	} else if (property == config->rotation_property) {
		state->rotation = val;
	} else if (plane->funcs->atomic_set_property) {
		return plane->funcs->atomic_set_property(plane, state,
				property, val);
	} else {
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(drm_atomic_plane_set_property);

/*
 * This function handles generic/core properties and calls out to
 * driver's ->atomic_get_property() for driver properties.  To ensure
 * consistent behavior you must call this function rather than the
 * driver hook directly.
 */
static int
drm_atomic_plane_get_property(struct drm_plane *plane,
		const struct drm_plane_state *state,
		struct drm_property *property, uint64_t *val)
{
	struct drm_device *dev = plane->dev;
	struct drm_mode_config *config = &dev->mode_config;

	if (property == config->prop_fb_id) {
		*val = (state->fb) ? state->fb->base.id : 0;
	} else if (property == config->prop_crtc_id) {
		*val = (state->crtc) ? state->crtc->base.id : 0;
	} else if (property == config->prop_crtc_x) {
		*val = I642U64(state->crtc_x);
	} else if (property == config->prop_crtc_y) {
		*val = I642U64(state->crtc_y);
	} else if (property == config->prop_crtc_w) {
		*val = state->crtc_w;
	} else if (property == config->prop_crtc_h) {
		*val = state->crtc_h;
	} else if (property == config->prop_src_x) {
		*val = state->src_x;
	} else if (property == config->prop_src_y) {
		*val = state->src_y;
	} else if (property == config->prop_src_w) {
		*val = state->src_w;
	} else if (property == config->prop_src_h) {
		*val = state->src_h;
	} else if (property == config->rotation_property) {
		*val = state->rotation;
	} else if (plane->funcs->atomic_get_property) {
		return plane->funcs->atomic_get_property(plane, state, property, val);
	} else {
		return -EINVAL;
	}

	return 0;
}

/**
 * drm_atomic_plane_check - check plane state
 * @plane: plane to check
 * @state: plane state to check
 *
 * Provides core sanity checks for plane state.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
static int drm_atomic_plane_check(struct drm_plane *plane,
		struct drm_plane_state *state)
{
	unsigned int fb_width, fb_height;
	int ret;

	/* either *both* CRTC and FB must be set, or neither */
	if (WARN_ON(state->crtc && !state->fb)) {
		DRM_DEBUG_ATOMIC("CRTC set but no FB\n");
		return -EINVAL;
	} else if (WARN_ON(state->fb && !state->crtc)) {
		DRM_DEBUG_ATOMIC("FB set but no CRTC\n");
		return -EINVAL;
	}

	/* if disabled, we don't care about the rest of the state: */
	if (!state->crtc)
		return 0;

	/* Check whether this plane is usable on this CRTC */
	if (!(plane->possible_crtcs & drm_crtc_mask(state->crtc))) {
		DRM_DEBUG_ATOMIC("Invalid crtc for plane\n");
		return -EINVAL;
	}

	/* Check whether this plane supports the fb pixel format. */
	ret = drm_plane_check_pixel_format(plane, state->fb->pixel_format);
	if (ret) {
		DRM_DEBUG_ATOMIC("Invalid pixel format %s\n",
				 drm_get_format_name(state->fb->pixel_format));
		return ret;
	}

	/* Give drivers some help against integer overflows */
	if (state->crtc_w > INT_MAX ||
	    state->crtc_x > INT_MAX - (int32_t) state->crtc_w ||
	    state->crtc_h > INT_MAX ||
	    state->crtc_y > INT_MAX - (int32_t) state->crtc_h) {
		DRM_DEBUG_ATOMIC("Invalid CRTC coordinates %ux%u+%d+%d\n",
				 state->crtc_w, state->crtc_h,
				 state->crtc_x, state->crtc_y);
		return -ERANGE;
	}

	fb_width = state->fb->width << 16;
	fb_height = state->fb->height << 16;

	/* Make sure source coordinates are inside the fb. */
	if (state->src_w > fb_width ||
	    state->src_x > fb_width - state->src_w ||
	    state->src_h > fb_height ||
	    state->src_y > fb_height - state->src_h) {
		DRM_DEBUG_ATOMIC("Invalid source coordinates "
				 "%u.%06ux%u.%06u+%u.%06u+%u.%06u\n",
				 state->src_w >> 16, ((state->src_w & 0xffff) * 15625) >> 10,
				 state->src_h >> 16, ((state->src_h & 0xffff) * 15625) >> 10,
				 state->src_x >> 16, ((state->src_x & 0xffff) * 15625) >> 10,
				 state->src_y >> 16, ((state->src_y & 0xffff) * 15625) >> 10);
		return -ENOSPC;
	}

	return 0;
}

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
		DRM_DEBUG_ATOMIC("Hot-added connector would overflow state array, restarting\n");
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

	DRM_DEBUG_ATOMIC("Added [CONNECTOR:%d] %p state to %p\n",
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
 * drm_atomic_connector_set_property - set property on connector.
 * @connector: the drm connector to set a property on
 * @state: the state object to update with the new property value
 * @property: the property to set
 * @val: the new property value
 *
 * Use this instead of calling connector->atomic_set_property directly.
 * This function handles generic/core properties and calls out to
 * driver's ->atomic_set_property() for driver properties.  To ensure
 * consistent behavior you must call this function rather than the
 * driver hook directly.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
int drm_atomic_connector_set_property(struct drm_connector *connector,
		struct drm_connector_state *state, struct drm_property *property,
		uint64_t val)
{
	struct drm_device *dev = connector->dev;
	struct drm_mode_config *config = &dev->mode_config;

	if (property == config->prop_crtc_id) {
		struct drm_crtc *crtc = drm_crtc_find(dev, val);
		return drm_atomic_set_crtc_for_connector(state, crtc);
	} else if (property == config->dpms_property) {
		/* setting DPMS property requires special handling, which
		 * is done in legacy setprop path for us.  Disallow (for
		 * now?) atomic writes to DPMS property:
		 */
		return -EINVAL;
	} else if (connector->funcs->atomic_set_property) {
		return connector->funcs->atomic_set_property(connector,
				state, property, val);
	} else {
		return -EINVAL;
	}
}
EXPORT_SYMBOL(drm_atomic_connector_set_property);

/*
 * This function handles generic/core properties and calls out to
 * driver's ->atomic_get_property() for driver properties.  To ensure
 * consistent behavior you must call this function rather than the
 * driver hook directly.
 */
static int
drm_atomic_connector_get_property(struct drm_connector *connector,
		const struct drm_connector_state *state,
		struct drm_property *property, uint64_t *val)
{
	struct drm_device *dev = connector->dev;
	struct drm_mode_config *config = &dev->mode_config;

	if (property == config->prop_crtc_id) {
		*val = (state->crtc) ? state->crtc->base.id : 0;
	} else if (property == config->dpms_property) {
		*val = connector->dpms;
	} else if (connector->funcs->atomic_get_property) {
		return connector->funcs->atomic_get_property(connector,
				state, property, val);
	} else {
		return -EINVAL;
	}

	return 0;
}

int drm_atomic_get_property(struct drm_mode_object *obj,
		struct drm_property *property, uint64_t *val)
{
	struct drm_device *dev = property->dev;
	int ret;

	switch (obj->type) {
	case DRM_MODE_OBJECT_CONNECTOR: {
		struct drm_connector *connector = obj_to_connector(obj);
		WARN_ON(!drm_modeset_is_locked(&dev->mode_config.connection_mutex));
		ret = drm_atomic_connector_get_property(connector,
				connector->state, property, val);
		break;
	}
	case DRM_MODE_OBJECT_CRTC: {
		struct drm_crtc *crtc = obj_to_crtc(obj);
		WARN_ON(!drm_modeset_is_locked(&crtc->mutex));
		ret = drm_atomic_crtc_get_property(crtc,
				crtc->state, property, val);
		break;
	}
	case DRM_MODE_OBJECT_PLANE: {
		struct drm_plane *plane = obj_to_plane(obj);
		WARN_ON(!drm_modeset_is_locked(&plane->mutex));
		ret = drm_atomic_plane_get_property(plane,
				plane->state, property, val);
		break;
	}
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

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
		DRM_DEBUG_ATOMIC("Link plane state %p to [CRTC:%d]\n",
				 plane_state, crtc->base.id);
	else
		DRM_DEBUG_ATOMIC("Link plane state %p to [NOCRTC]\n",
				 plane_state);

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
		DRM_DEBUG_ATOMIC("Set [FB:%d] for plane state %p\n",
				 fb->base.id, plane_state);
	else
		DRM_DEBUG_ATOMIC("Set [NOFB] for plane state %p\n",
				 plane_state);
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
		DRM_DEBUG_ATOMIC("Link connector state %p to [CRTC:%d]\n",
				 conn_state, crtc->base.id);
	else
		DRM_DEBUG_ATOMIC("Link connector state %p to [NOCRTC]\n",
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

	DRM_DEBUG_ATOMIC("Adding all current connectors for [CRTC:%d] to %p\n",
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

	DRM_DEBUG_ATOMIC("State %p has %i connectors for [CRTC:%d]\n",
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
	struct drm_device *dev = state->dev;
	struct drm_mode_config *config = &dev->mode_config;
	int nplanes = config->num_total_plane;
	int ncrtcs = config->num_crtc;
	int i, ret = 0;

	DRM_DEBUG_ATOMIC("checking %p\n", state);

	for (i = 0; i < nplanes; i++) {
		struct drm_plane *plane = state->planes[i];

		if (!plane)
			continue;

		ret = drm_atomic_plane_check(plane, state->plane_states[i]);
		if (ret) {
			DRM_DEBUG_ATOMIC("[PLANE:%d] atomic core check failed\n",
					 plane->base.id);
			return ret;
		}
	}

	for (i = 0; i < ncrtcs; i++) {
		struct drm_crtc *crtc = state->crtcs[i];

		if (!crtc)
			continue;

		ret = drm_atomic_crtc_check(crtc, state->crtc_states[i]);
		if (ret) {
			DRM_DEBUG_ATOMIC("[CRTC:%d] atomic core check failed\n",
					 crtc->base.id);
			return ret;
		}
	}

	if (config->funcs->atomic_check)
		ret = config->funcs->atomic_check(state->dev, state);

	if (!state->allow_modeset) {
		for (i = 0; i < ncrtcs; i++) {
			struct drm_crtc *crtc = state->crtcs[i];
			struct drm_crtc_state *crtc_state = state->crtc_states[i];

			if (!crtc)
				continue;

			if (crtc_state->mode_changed ||
			    crtc_state->active_changed) {
				DRM_DEBUG_ATOMIC("[CRTC:%d] requires full modeset\n",
						 crtc->base.id);
				return -EINVAL;
			}
		}
	}

	return ret;
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

	DRM_DEBUG_ATOMIC("commiting %p\n", state);

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

	DRM_DEBUG_ATOMIC("commiting %p asynchronously\n", state);

	return config->funcs->atomic_commit(state->dev, state, true);
}
EXPORT_SYMBOL(drm_atomic_async_commit);

/*
 * The big monstor ioctl
 */

static struct drm_pending_vblank_event *create_vblank_event(
		struct drm_device *dev, struct drm_file *file_priv, uint64_t user_data)
{
	struct drm_pending_vblank_event *e = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (file_priv->event_space < sizeof e->event) {
		spin_unlock_irqrestore(&dev->event_lock, flags);
		goto out;
	}
	file_priv->event_space -= sizeof e->event;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	e = kzalloc(sizeof *e, GFP_KERNEL);
	if (e == NULL) {
		spin_lock_irqsave(&dev->event_lock, flags);
		file_priv->event_space += sizeof e->event;
		spin_unlock_irqrestore(&dev->event_lock, flags);
		goto out;
	}

	e->event.base.type = DRM_EVENT_FLIP_COMPLETE;
	e->event.base.length = sizeof e->event;
	e->event.user_data = user_data;
	e->base.event = &e->event.base;
	e->base.file_priv = file_priv;
	e->base.destroy = (void (*) (struct drm_pending_event *)) kfree;

out:
	return e;
}

static void destroy_vblank_event(struct drm_device *dev,
		struct drm_file *file_priv, struct drm_pending_vblank_event *e)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	file_priv->event_space += sizeof e->event;
	spin_unlock_irqrestore(&dev->event_lock, flags);
	kfree(e);
}

static int atomic_set_prop(struct drm_atomic_state *state,
		struct drm_mode_object *obj, struct drm_property *prop,
		uint64_t prop_value)
{
	struct drm_mode_object *ref;
	int ret;

	if (!drm_property_change_valid_get(prop, prop_value, &ref))
		return -EINVAL;

	switch (obj->type) {
	case DRM_MODE_OBJECT_CONNECTOR: {
		struct drm_connector *connector = obj_to_connector(obj);
		struct drm_connector_state *connector_state;

		connector_state = drm_atomic_get_connector_state(state, connector);
		if (IS_ERR(connector_state)) {
			ret = PTR_ERR(connector_state);
			break;
		}

		ret = drm_atomic_connector_set_property(connector,
				connector_state, prop, prop_value);
		break;
	}
	case DRM_MODE_OBJECT_CRTC: {
		struct drm_crtc *crtc = obj_to_crtc(obj);
		struct drm_crtc_state *crtc_state;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state)) {
			ret = PTR_ERR(crtc_state);
			break;
		}

		ret = drm_atomic_crtc_set_property(crtc,
				crtc_state, prop, prop_value);
		break;
	}
	case DRM_MODE_OBJECT_PLANE: {
		struct drm_plane *plane = obj_to_plane(obj);
		struct drm_plane_state *plane_state;

		plane_state = drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(plane_state)) {
			ret = PTR_ERR(plane_state);
			break;
		}

		ret = drm_atomic_plane_set_property(plane,
				plane_state, prop, prop_value);
		break;
	}
	default:
		ret = -EINVAL;
		break;
	}

	drm_property_change_valid_put(prop, ref);
	return ret;
}

int drm_mode_atomic_ioctl(struct drm_device *dev,
			  void *data, struct drm_file *file_priv)
{
	struct drm_mode_atomic *arg = data;
	uint32_t __user *objs_ptr = (uint32_t __user *)(unsigned long)(arg->objs_ptr);
	uint32_t __user *count_props_ptr = (uint32_t __user *)(unsigned long)(arg->count_props_ptr);
	uint32_t __user *props_ptr = (uint32_t __user *)(unsigned long)(arg->props_ptr);
	uint64_t __user *prop_values_ptr = (uint64_t __user *)(unsigned long)(arg->prop_values_ptr);
	unsigned int copied_objs, copied_props;
	struct drm_atomic_state *state;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_plane *plane;
	unsigned plane_mask = 0;
	int ret = 0;
	unsigned int i, j;

	/* disallow for drivers not supporting atomic: */
	if (!drm_core_check_feature(dev, DRIVER_ATOMIC))
		return -EINVAL;

	/* disallow for userspace that has not enabled atomic cap (even
	 * though this may be a bit overkill, since legacy userspace
	 * wouldn't know how to call this ioctl)
	 */
	if (!file_priv->atomic)
		return -EINVAL;

	if (arg->flags & ~DRM_MODE_ATOMIC_FLAGS)
		return -EINVAL;

	if (arg->reserved)
		return -EINVAL;

	if ((arg->flags & DRM_MODE_PAGE_FLIP_ASYNC) &&
			!dev->mode_config.async_page_flip)
		return -EINVAL;

	/* can't test and expect an event at the same time. */
	if ((arg->flags & DRM_MODE_ATOMIC_TEST_ONLY) &&
			(arg->flags & DRM_MODE_PAGE_FLIP_EVENT))
		return -EINVAL;

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_atomic_state_alloc(dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = &ctx;
	state->allow_modeset = !!(arg->flags & DRM_MODE_ATOMIC_ALLOW_MODESET);

retry:
	copied_objs = 0;
	copied_props = 0;

	for (i = 0; i < arg->count_objs; i++) {
		uint32_t obj_id, count_props;
		struct drm_mode_object *obj;

		if (get_user(obj_id, objs_ptr + copied_objs)) {
			ret = -EFAULT;
			goto fail;
		}

		obj = drm_mode_object_find(dev, obj_id, DRM_MODE_OBJECT_ANY);
		if (!obj || !obj->properties) {
			ret = -ENOENT;
			goto fail;
		}

		if (obj->type == DRM_MODE_OBJECT_PLANE) {
			plane = obj_to_plane(obj);
			plane_mask |= (1 << drm_plane_index(plane));
			plane->old_fb = plane->fb;
		}

		if (get_user(count_props, count_props_ptr + copied_objs)) {
			ret = -EFAULT;
			goto fail;
		}

		copied_objs++;

		for (j = 0; j < count_props; j++) {
			uint32_t prop_id;
			uint64_t prop_value;
			struct drm_property *prop;

			if (get_user(prop_id, props_ptr + copied_props)) {
				ret = -EFAULT;
				goto fail;
			}

			prop = drm_property_find(dev, prop_id);
			if (!prop) {
				ret = -ENOENT;
				goto fail;
			}

			if (copy_from_user(&prop_value,
					   prop_values_ptr + copied_props,
					   sizeof(prop_value))) {
				ret = -EFAULT;
				goto fail;
			}

			ret = atomic_set_prop(state, obj, prop, prop_value);
			if (ret)
				goto fail;

			copied_props++;
		}
	}

	if (arg->flags & DRM_MODE_PAGE_FLIP_EVENT) {
		int ncrtcs = dev->mode_config.num_crtc;

		for (i = 0; i < ncrtcs; i++) {
			struct drm_crtc_state *crtc_state = state->crtc_states[i];
			struct drm_pending_vblank_event *e;

			if (!crtc_state)
				continue;

			e = create_vblank_event(dev, file_priv, arg->user_data);
			if (!e) {
				ret = -ENOMEM;
				goto fail;
			}

			crtc_state->event = e;
		}
	}

	if (arg->flags & DRM_MODE_ATOMIC_TEST_ONLY) {
		ret = drm_atomic_check_only(state);
		/* _check_only() does not free state, unlike _commit() */
		drm_atomic_state_free(state);
	} else if (arg->flags & DRM_MODE_ATOMIC_NONBLOCK) {
		ret = drm_atomic_async_commit(state);
	} else {
		ret = drm_atomic_commit(state);
	}

	/* if succeeded, fixup legacy plane crtc/fb ptrs before dropping
	 * locks (ie. while it is still safe to deref plane->state).  We
	 * need to do this here because the driver entry points cannot
	 * distinguish between legacy and atomic ioctls.
	 */
	drm_for_each_plane_mask(plane, dev, plane_mask) {
		if (ret == 0) {
			struct drm_framebuffer *new_fb = plane->state->fb;
			if (new_fb)
				drm_framebuffer_reference(new_fb);
			plane->fb = new_fb;
			plane->crtc = plane->state->crtc;
		} else {
			plane->old_fb = NULL;
		}
		if (plane->old_fb) {
			drm_framebuffer_unreference(plane->old_fb);
			plane->old_fb = NULL;
		}
	}

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;

fail:
	if (ret == -EDEADLK)
		goto backoff;

	if (arg->flags & DRM_MODE_PAGE_FLIP_EVENT) {
		int ncrtcs = dev->mode_config.num_crtc;

		for (i = 0; i < ncrtcs; i++) {
			struct drm_crtc_state *crtc_state = state->crtc_states[i];

			if (!crtc_state)
				continue;

			destroy_vblank_event(dev, file_priv, crtc_state->event);
			crtc_state->event = NULL;
		}
	}

	drm_atomic_state_free(state);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;

backoff:
	drm_atomic_state_clear(state);
	drm_modeset_backoff(&ctx);

	goto retry;
}
