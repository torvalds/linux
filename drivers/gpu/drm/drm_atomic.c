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
#include <drm/drm_mode.h>
#include <drm/drm_print.h>
#include <linux/sync_file.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

void __drm_crtc_commit_free(struct kref *kref)
{
	struct drm_crtc_commit *commit =
		container_of(kref, struct drm_crtc_commit, ref);

	kfree(commit);
}
EXPORT_SYMBOL(__drm_crtc_commit_free);

/**
 * drm_atomic_state_default_release -
 * release memory initialized by drm_atomic_state_init
 * @state: atomic state
 *
 * Free all the memory allocated by drm_atomic_state_init.
 * This should only be used by drivers which are still subclassing
 * &drm_atomic_state and haven't switched to &drm_private_state yet.
 */
void drm_atomic_state_default_release(struct drm_atomic_state *state)
{
	kfree(state->connectors);
	kfree(state->crtcs);
	kfree(state->planes);
	kfree(state->private_objs);
}
EXPORT_SYMBOL(drm_atomic_state_default_release);

/**
 * drm_atomic_state_init - init new atomic state
 * @dev: DRM device
 * @state: atomic state
 *
 * Default implementation for filling in a new atomic state.
 * This should only be used by drivers which are still subclassing
 * &drm_atomic_state and haven't switched to &drm_private_state yet.
 */
int
drm_atomic_state_init(struct drm_device *dev, struct drm_atomic_state *state)
{
	kref_init(&state->ref);

	/* TODO legacy paths should maybe do a better job about
	 * setting this appropriately?
	 */
	state->allow_modeset = true;

	state->crtcs = kcalloc(dev->mode_config.num_crtc,
			       sizeof(*state->crtcs), GFP_KERNEL);
	if (!state->crtcs)
		goto fail;
	state->planes = kcalloc(dev->mode_config.num_total_plane,
				sizeof(*state->planes), GFP_KERNEL);
	if (!state->planes)
		goto fail;

	state->dev = dev;

	DRM_DEBUG_ATOMIC("Allocated atomic state %p\n", state);

	return 0;
fail:
	drm_atomic_state_default_release(state);
	return -ENOMEM;
}
EXPORT_SYMBOL(drm_atomic_state_init);

/**
 * drm_atomic_state_alloc - allocate atomic state
 * @dev: DRM device
 *
 * This allocates an empty atomic state to track updates.
 */
struct drm_atomic_state *
drm_atomic_state_alloc(struct drm_device *dev)
{
	struct drm_mode_config *config = &dev->mode_config;

	if (!config->funcs->atomic_state_alloc) {
		struct drm_atomic_state *state;

		state = kzalloc(sizeof(*state), GFP_KERNEL);
		if (!state)
			return NULL;
		if (drm_atomic_state_init(dev, state) < 0) {
			kfree(state);
			return NULL;
		}
		return state;
	}

	return config->funcs->atomic_state_alloc(dev);
}
EXPORT_SYMBOL(drm_atomic_state_alloc);

/**
 * drm_atomic_state_default_clear - clear base atomic state
 * @state: atomic state
 *
 * Default implementation for clearing atomic state.
 * This should only be used by drivers which are still subclassing
 * &drm_atomic_state and haven't switched to &drm_private_state yet.
 */
void drm_atomic_state_default_clear(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_mode_config *config = &dev->mode_config;
	int i;

	DRM_DEBUG_ATOMIC("Clearing atomic state %p\n", state);

	for (i = 0; i < state->num_connector; i++) {
		struct drm_connector *connector = state->connectors[i].ptr;

		if (!connector)
			continue;

		connector->funcs->atomic_destroy_state(connector,
						       state->connectors[i].state);
		state->connectors[i].ptr = NULL;
		state->connectors[i].state = NULL;
		state->connectors[i].old_state = NULL;
		state->connectors[i].new_state = NULL;
		drm_connector_put(connector);
	}

	for (i = 0; i < config->num_crtc; i++) {
		struct drm_crtc *crtc = state->crtcs[i].ptr;

		if (!crtc)
			continue;

		crtc->funcs->atomic_destroy_state(crtc,
						  state->crtcs[i].state);

		state->crtcs[i].ptr = NULL;
		state->crtcs[i].state = NULL;
		state->crtcs[i].old_state = NULL;
		state->crtcs[i].new_state = NULL;
	}

	for (i = 0; i < config->num_total_plane; i++) {
		struct drm_plane *plane = state->planes[i].ptr;

		if (!plane)
			continue;

		plane->funcs->atomic_destroy_state(plane,
						   state->planes[i].state);
		state->planes[i].ptr = NULL;
		state->planes[i].state = NULL;
		state->planes[i].old_state = NULL;
		state->planes[i].new_state = NULL;
	}

	for (i = 0; i < state->num_private_objs; i++) {
		struct drm_private_obj *obj = state->private_objs[i].ptr;

		obj->funcs->atomic_destroy_state(obj,
						 state->private_objs[i].state);
		state->private_objs[i].ptr = NULL;
		state->private_objs[i].state = NULL;
		state->private_objs[i].old_state = NULL;
		state->private_objs[i].new_state = NULL;
	}
	state->num_private_objs = 0;

	if (state->fake_commit) {
		drm_crtc_commit_put(state->fake_commit);
		state->fake_commit = NULL;
	}
}
EXPORT_SYMBOL(drm_atomic_state_default_clear);

/**
 * drm_atomic_state_clear - clear state object
 * @state: atomic state
 *
 * When the w/w mutex algorithm detects a deadlock we need to back off and drop
 * all locks. So someone else could sneak in and change the current modeset
 * configuration. Which means that all the state assembled in @state is no
 * longer an atomic update to the current state, but to some arbitrary earlier
 * state. Which could break assumptions the driver's
 * &drm_mode_config_funcs.atomic_check likely relies on.
 *
 * Hence we must clear all cached state and completely start over, using this
 * function.
 */
void drm_atomic_state_clear(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_mode_config *config = &dev->mode_config;

	if (config->funcs->atomic_state_clear)
		config->funcs->atomic_state_clear(state);
	else
		drm_atomic_state_default_clear(state);
}
EXPORT_SYMBOL(drm_atomic_state_clear);

/**
 * __drm_atomic_state_free - free all memory for an atomic state
 * @ref: This atomic state to deallocate
 *
 * This frees all memory associated with an atomic state, including all the
 * per-object state for planes, crtcs and connectors.
 */
void __drm_atomic_state_free(struct kref *ref)
{
	struct drm_atomic_state *state = container_of(ref, typeof(*state), ref);
	struct drm_mode_config *config = &state->dev->mode_config;

	drm_atomic_state_clear(state);

	DRM_DEBUG_ATOMIC("Freeing atomic state %p\n", state);

	if (config->funcs->atomic_state_free) {
		config->funcs->atomic_state_free(state);
	} else {
		drm_atomic_state_default_release(state);
		kfree(state);
	}
}
EXPORT_SYMBOL(__drm_atomic_state_free);

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
	int ret, index = drm_crtc_index(crtc);
	struct drm_crtc_state *crtc_state;

	WARN_ON(!state->acquire_ctx);

	crtc_state = drm_atomic_get_existing_crtc_state(state, crtc);
	if (crtc_state)
		return crtc_state;

	ret = drm_modeset_lock(&crtc->mutex, state->acquire_ctx);
	if (ret)
		return ERR_PTR(ret);

	crtc_state = crtc->funcs->atomic_duplicate_state(crtc);
	if (!crtc_state)
		return ERR_PTR(-ENOMEM);

	state->crtcs[index].state = crtc_state;
	state->crtcs[index].old_state = crtc->state;
	state->crtcs[index].new_state = crtc_state;
	state->crtcs[index].ptr = crtc;
	crtc_state->state = state;

	DRM_DEBUG_ATOMIC("Added [CRTC:%d:%s] %p state to %p\n",
			 crtc->base.id, crtc->name, crtc_state, state);

	return crtc_state;
}
EXPORT_SYMBOL(drm_atomic_get_crtc_state);

static void set_out_fence_for_crtc(struct drm_atomic_state *state,
				   struct drm_crtc *crtc, s32 __user *fence_ptr)
{
	state->crtcs[drm_crtc_index(crtc)].out_fence_ptr = fence_ptr;
}

static s32 __user *get_out_fence_for_crtc(struct drm_atomic_state *state,
					  struct drm_crtc *crtc)
{
	s32 __user *fence_ptr;

	fence_ptr = state->crtcs[drm_crtc_index(crtc)].out_fence_ptr;
	state->crtcs[drm_crtc_index(crtc)].out_fence_ptr = NULL;

	return fence_ptr;
}

/**
 * drm_atomic_set_mode_for_crtc - set mode for CRTC
 * @state: the CRTC whose incoming state to update
 * @mode: kernel-internal mode to use for the CRTC, or NULL to disable
 *
 * Set a mode (originating from the kernel) on the desired CRTC state and update
 * the enable property.
 *
 * RETURNS:
 * Zero on success, error code on failure. Cannot return -EDEADLK.
 */
int drm_atomic_set_mode_for_crtc(struct drm_crtc_state *state,
				 const struct drm_display_mode *mode)
{
	struct drm_mode_modeinfo umode;

	/* Early return for no change. */
	if (mode && memcmp(&state->mode, mode, sizeof(*mode)) == 0)
		return 0;

	drm_property_blob_put(state->mode_blob);
	state->mode_blob = NULL;

	if (mode) {
		drm_mode_convert_to_umode(&umode, mode);
		state->mode_blob =
			drm_property_create_blob(state->crtc->dev,
		                                 sizeof(umode),
		                                 &umode);
		if (IS_ERR(state->mode_blob))
			return PTR_ERR(state->mode_blob);

		drm_mode_copy(&state->mode, mode);
		state->enable = true;
		DRM_DEBUG_ATOMIC("Set [MODE:%s] for CRTC state %p\n",
				 mode->name, state);
	} else {
		memset(&state->mode, 0, sizeof(state->mode));
		state->enable = false;
		DRM_DEBUG_ATOMIC("Set [NOMODE] for CRTC state %p\n",
				 state);
	}

	return 0;
}
EXPORT_SYMBOL(drm_atomic_set_mode_for_crtc);

/**
 * drm_atomic_set_mode_prop_for_crtc - set mode for CRTC
 * @state: the CRTC whose incoming state to update
 * @blob: pointer to blob property to use for mode
 *
 * Set a mode (originating from a blob property) on the desired CRTC state.
 * This function will take a reference on the blob property for the CRTC state,
 * and release the reference held on the state's existing mode property, if any
 * was set.
 *
 * RETURNS:
 * Zero on success, error code on failure. Cannot return -EDEADLK.
 */
int drm_atomic_set_mode_prop_for_crtc(struct drm_crtc_state *state,
                                      struct drm_property_blob *blob)
{
	if (blob == state->mode_blob)
		return 0;

	drm_property_blob_put(state->mode_blob);
	state->mode_blob = NULL;

	memset(&state->mode, 0, sizeof(state->mode));

	if (blob) {
		if (blob->length != sizeof(struct drm_mode_modeinfo) ||
		    drm_mode_convert_umode(state->crtc->dev, &state->mode,
					   blob->data))
			return -EINVAL;

		state->mode_blob = drm_property_blob_get(blob);
		state->enable = true;
		DRM_DEBUG_ATOMIC("Set [MODE:%s] for CRTC state %p\n",
				 state->mode.name, state);
	} else {
		state->enable = false;
		DRM_DEBUG_ATOMIC("Set [NOMODE] for CRTC state %p\n",
				 state);
	}

	return 0;
}
EXPORT_SYMBOL(drm_atomic_set_mode_prop_for_crtc);

/**
 * drm_atomic_replace_property_blob_from_id - lookup the new blob and replace the old one with it
 * @dev: DRM device
 * @blob: a pointer to the member blob to be replaced
 * @blob_id: ID of the new blob
 * @expected_size: total expected size of the blob data (in bytes)
 * @expected_elem_size: expected element size of the blob data (in bytes)
 * @replaced: did the blob get replaced?
 *
 * Replace @blob with another blob with the ID @blob_id. If @blob_id is zero
 * @blob becomes NULL.
 *
 * If @expected_size is positive the new blob length is expected to be equal
 * to @expected_size bytes. If @expected_elem_size is positive the new blob
 * length is expected to be a multiple of @expected_elem_size bytes. Otherwise
 * an error is returned.
 *
 * @replaced will indicate to the caller whether the blob was replaced or not.
 * If the old and new blobs were in fact the same blob @replaced will be false
 * otherwise it will be true.
 *
 * RETURNS:
 * Zero on success, error code on failure.
 */
static int
drm_atomic_replace_property_blob_from_id(struct drm_device *dev,
					 struct drm_property_blob **blob,
					 uint64_t blob_id,
					 ssize_t expected_size,
					 ssize_t expected_elem_size,
					 bool *replaced)
{
	struct drm_property_blob *new_blob = NULL;

	if (blob_id != 0) {
		new_blob = drm_property_lookup_blob(dev, blob_id);
		if (new_blob == NULL)
			return -EINVAL;

		if (expected_size > 0 &&
		    new_blob->length != expected_size) {
			drm_property_blob_put(new_blob);
			return -EINVAL;
		}
		if (expected_elem_size > 0 &&
		    new_blob->length % expected_elem_size != 0) {
			drm_property_blob_put(new_blob);
			return -EINVAL;
		}
	}

	*replaced |= drm_property_replace_blob(blob, new_blob);
	drm_property_blob_put(new_blob);

	return 0;
}

/**
 * drm_atomic_crtc_set_property - set property on CRTC
 * @crtc: the drm CRTC to set a property on
 * @state: the state object to update with the new property value
 * @property: the property to set
 * @val: the new property value
 *
 * This function handles generic/core properties and calls out to driver's
 * &drm_crtc_funcs.atomic_set_property for driver properties. To ensure
 * consistent behavior you must call this function rather than the driver hook
 * directly.
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
	bool replaced = false;
	int ret;

	if (property == config->prop_active)
		state->active = val;
	else if (property == config->prop_mode_id) {
		struct drm_property_blob *mode =
			drm_property_lookup_blob(dev, val);
		ret = drm_atomic_set_mode_prop_for_crtc(state, mode);
		drm_property_blob_put(mode);
		return ret;
	} else if (property == config->degamma_lut_property) {
		ret = drm_atomic_replace_property_blob_from_id(dev,
					&state->degamma_lut,
					val,
					-1, sizeof(struct drm_color_lut),
					&replaced);
		state->color_mgmt_changed |= replaced;
		return ret;
	} else if (property == config->ctm_property) {
		ret = drm_atomic_replace_property_blob_from_id(dev,
					&state->ctm,
					val,
					sizeof(struct drm_color_ctm), -1,
					&replaced);
		state->color_mgmt_changed |= replaced;
		return ret;
	} else if (property == config->gamma_lut_property) {
		ret = drm_atomic_replace_property_blob_from_id(dev,
					&state->gamma_lut,
					val,
					-1, sizeof(struct drm_color_lut),
					&replaced);
		state->color_mgmt_changed |= replaced;
		return ret;
	} else if (property == config->prop_out_fence_ptr) {
		s32 __user *fence_ptr = u64_to_user_ptr(val);

		if (!fence_ptr)
			return 0;

		if (put_user(-1, fence_ptr))
			return -EFAULT;

		set_out_fence_for_crtc(state->state, crtc, fence_ptr);
	} else if (crtc->funcs->atomic_set_property)
		return crtc->funcs->atomic_set_property(crtc, state, property, val);
	else
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(drm_atomic_crtc_set_property);

/**
 * drm_atomic_crtc_get_property - get property value from CRTC state
 * @crtc: the drm CRTC to set a property on
 * @state: the state object to get the property value from
 * @property: the property to set
 * @val: return location for the property value
 *
 * This function handles generic/core properties and calls out to driver's
 * &drm_crtc_funcs.atomic_get_property for driver properties. To ensure
 * consistent behavior you must call this function rather than the driver hook
 * directly.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
static int
drm_atomic_crtc_get_property(struct drm_crtc *crtc,
		const struct drm_crtc_state *state,
		struct drm_property *property, uint64_t *val)
{
	struct drm_device *dev = crtc->dev;
	struct drm_mode_config *config = &dev->mode_config;

	if (property == config->prop_active)
		*val = state->active;
	else if (property == config->prop_mode_id)
		*val = (state->mode_blob) ? state->mode_blob->base.id : 0;
	else if (property == config->degamma_lut_property)
		*val = (state->degamma_lut) ? state->degamma_lut->base.id : 0;
	else if (property == config->ctm_property)
		*val = (state->ctm) ? state->ctm->base.id : 0;
	else if (property == config->gamma_lut_property)
		*val = (state->gamma_lut) ? state->gamma_lut->base.id : 0;
	else if (property == config->prop_out_fence_ptr)
		*val = 0;
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
		DRM_DEBUG_ATOMIC("[CRTC:%d:%s] active without enabled\n",
				 crtc->base.id, crtc->name);
		return -EINVAL;
	}

	/* The state->enable vs. state->mode_blob checks can be WARN_ON,
	 * as this is a kernel-internal detail that userspace should never
	 * be able to trigger. */
	if (drm_core_check_feature(crtc->dev, DRIVER_ATOMIC) &&
	    WARN_ON(state->enable && !state->mode_blob)) {
		DRM_DEBUG_ATOMIC("[CRTC:%d:%s] enabled without mode blob\n",
				 crtc->base.id, crtc->name);
		return -EINVAL;
	}

	if (drm_core_check_feature(crtc->dev, DRIVER_ATOMIC) &&
	    WARN_ON(!state->enable && state->mode_blob)) {
		DRM_DEBUG_ATOMIC("[CRTC:%d:%s] disabled with mode blob\n",
				 crtc->base.id, crtc->name);
		return -EINVAL;
	}

	/*
	 * Reject event generation for when a CRTC is off and stays off.
	 * It wouldn't be hard to implement this, but userspace has a track
	 * record of happily burning through 100% cpu (or worse, crash) when the
	 * display pipe is suspended. To avoid all that fun just reject updates
	 * that ask for events since likely that indicates a bug in the
	 * compositor's drawing loop. This is consistent with the vblank IOCTL
	 * and legacy page_flip IOCTL which also reject service on a disabled
	 * pipe.
	 */
	if (state->event && !state->active && !crtc->state->active) {
		DRM_DEBUG_ATOMIC("[CRTC:%d:%s] requesting event but off\n",
				 crtc->base.id, crtc->name);
		return -EINVAL;
	}

	return 0;
}

static void drm_atomic_crtc_print_state(struct drm_printer *p,
		const struct drm_crtc_state *state)
{
	struct drm_crtc *crtc = state->crtc;

	drm_printf(p, "crtc[%u]: %s\n", crtc->base.id, crtc->name);
	drm_printf(p, "\tenable=%d\n", state->enable);
	drm_printf(p, "\tactive=%d\n", state->active);
	drm_printf(p, "\tplanes_changed=%d\n", state->planes_changed);
	drm_printf(p, "\tmode_changed=%d\n", state->mode_changed);
	drm_printf(p, "\tactive_changed=%d\n", state->active_changed);
	drm_printf(p, "\tconnectors_changed=%d\n", state->connectors_changed);
	drm_printf(p, "\tcolor_mgmt_changed=%d\n", state->color_mgmt_changed);
	drm_printf(p, "\tplane_mask=%x\n", state->plane_mask);
	drm_printf(p, "\tconnector_mask=%x\n", state->connector_mask);
	drm_printf(p, "\tencoder_mask=%x\n", state->encoder_mask);
	drm_printf(p, "\tmode: " DRM_MODE_FMT "\n", DRM_MODE_ARG(&state->mode));

	if (crtc->funcs->atomic_print_state)
		crtc->funcs->atomic_print_state(p, state);
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
	int ret, index = drm_plane_index(plane);
	struct drm_plane_state *plane_state;

	WARN_ON(!state->acquire_ctx);

	plane_state = drm_atomic_get_existing_plane_state(state, plane);
	if (plane_state)
		return plane_state;

	ret = drm_modeset_lock(&plane->mutex, state->acquire_ctx);
	if (ret)
		return ERR_PTR(ret);

	plane_state = plane->funcs->atomic_duplicate_state(plane);
	if (!plane_state)
		return ERR_PTR(-ENOMEM);

	state->planes[index].state = plane_state;
	state->planes[index].ptr = plane;
	state->planes[index].old_state = plane->state;
	state->planes[index].new_state = plane_state;
	plane_state->state = state;

	DRM_DEBUG_ATOMIC("Added [PLANE:%d:%s] %p state to %p\n",
			 plane->base.id, plane->name, plane_state, state);

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
 * This function handles generic/core properties and calls out to driver's
 * &drm_plane_funcs.atomic_set_property for driver properties.  To ensure
 * consistent behavior you must call this function rather than the driver hook
 * directly.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
static int drm_atomic_plane_set_property(struct drm_plane *plane,
		struct drm_plane_state *state, struct drm_property *property,
		uint64_t val)
{
	struct drm_device *dev = plane->dev;
	struct drm_mode_config *config = &dev->mode_config;

	if (property == config->prop_fb_id) {
		struct drm_framebuffer *fb = drm_framebuffer_lookup(dev, NULL, val);
		drm_atomic_set_fb_for_plane(state, fb);
		if (fb)
			drm_framebuffer_put(fb);
	} else if (property == config->prop_in_fence_fd) {
		if (state->fence)
			return -EINVAL;

		if (U642I64(val) == -1)
			return 0;

		state->fence = sync_file_get_fence(val);
		if (!state->fence)
			return -EINVAL;

	} else if (property == config->prop_crtc_id) {
		struct drm_crtc *crtc = drm_crtc_find(dev, NULL, val);
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
	} else if (property == plane->rotation_property) {
		if (!is_power_of_2(val & DRM_MODE_ROTATE_MASK))
			return -EINVAL;
		state->rotation = val;
	} else if (property == plane->zpos_property) {
		state->zpos = val;
	} else if (property == plane->color_encoding_property) {
		state->color_encoding = val;
	} else if (property == plane->color_range_property) {
		state->color_range = val;
	} else if (plane->funcs->atomic_set_property) {
		return plane->funcs->atomic_set_property(plane, state,
				property, val);
	} else {
		return -EINVAL;
	}

	return 0;
}

/**
 * drm_atomic_plane_get_property - get property value from plane state
 * @plane: the drm plane to set a property on
 * @state: the state object to get the property value from
 * @property: the property to set
 * @val: return location for the property value
 *
 * This function handles generic/core properties and calls out to driver's
 * &drm_plane_funcs.atomic_get_property for driver properties.  To ensure
 * consistent behavior you must call this function rather than the driver hook
 * directly.
 *
 * RETURNS:
 * Zero on success, error code on failure
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
	} else if (property == config->prop_in_fence_fd) {
		*val = -1;
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
	} else if (property == plane->rotation_property) {
		*val = state->rotation;
	} else if (property == plane->zpos_property) {
		*val = state->zpos;
	} else if (property == plane->color_encoding_property) {
		*val = state->color_encoding;
	} else if (property == plane->color_range_property) {
		*val = state->color_range;
	} else if (plane->funcs->atomic_get_property) {
		return plane->funcs->atomic_get_property(plane, state, property, val);
	} else {
		return -EINVAL;
	}

	return 0;
}

static bool
plane_switching_crtc(struct drm_atomic_state *state,
		     struct drm_plane *plane,
		     struct drm_plane_state *plane_state)
{
	if (!plane->state->crtc || !plane_state->crtc)
		return false;

	if (plane->state->crtc == plane_state->crtc)
		return false;

	/* This could be refined, but currently there's no helper or driver code
	 * to implement direct switching of active planes nor userspace to take
	 * advantage of more direct plane switching without the intermediate
	 * full OFF state.
	 */
	return true;
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
	if (state->crtc && !state->fb) {
		DRM_DEBUG_ATOMIC("CRTC set but no FB\n");
		return -EINVAL;
	} else if (state->fb && !state->crtc) {
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
	ret = drm_plane_check_pixel_format(plane, state->fb->format->format,
					   state->fb->modifier);
	if (ret) {
		struct drm_format_name_buf format_name;
		DRM_DEBUG_ATOMIC("Invalid pixel format %s, modifier 0x%llx\n",
				 drm_get_format_name(state->fb->format->format,
						     &format_name),
				 state->fb->modifier);
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
				 "%u.%06ux%u.%06u+%u.%06u+%u.%06u (fb %ux%u)\n",
				 state->src_w >> 16, ((state->src_w & 0xffff) * 15625) >> 10,
				 state->src_h >> 16, ((state->src_h & 0xffff) * 15625) >> 10,
				 state->src_x >> 16, ((state->src_x & 0xffff) * 15625) >> 10,
				 state->src_y >> 16, ((state->src_y & 0xffff) * 15625) >> 10,
				 state->fb->width, state->fb->height);
		return -ENOSPC;
	}

	if (plane_switching_crtc(state->state, plane, state)) {
		DRM_DEBUG_ATOMIC("[PLANE:%d:%s] switching CRTC directly\n",
				 plane->base.id, plane->name);
		return -EINVAL;
	}

	return 0;
}

static void drm_atomic_plane_print_state(struct drm_printer *p,
		const struct drm_plane_state *state)
{
	struct drm_plane *plane = state->plane;
	struct drm_rect src  = drm_plane_state_src(state);
	struct drm_rect dest = drm_plane_state_dest(state);

	drm_printf(p, "plane[%u]: %s\n", plane->base.id, plane->name);
	drm_printf(p, "\tcrtc=%s\n", state->crtc ? state->crtc->name : "(null)");
	drm_printf(p, "\tfb=%u\n", state->fb ? state->fb->base.id : 0);
	if (state->fb)
		drm_framebuffer_print_info(p, 2, state->fb);
	drm_printf(p, "\tcrtc-pos=" DRM_RECT_FMT "\n", DRM_RECT_ARG(&dest));
	drm_printf(p, "\tsrc-pos=" DRM_RECT_FP_FMT "\n", DRM_RECT_FP_ARG(&src));
	drm_printf(p, "\trotation=%x\n", state->rotation);
	drm_printf(p, "\tcolor-encoding=%s\n",
		   drm_get_color_encoding_name(state->color_encoding));
	drm_printf(p, "\tcolor-range=%s\n",
		   drm_get_color_range_name(state->color_range));

	if (plane->funcs->atomic_print_state)
		plane->funcs->atomic_print_state(p, state);
}

/**
 * DOC: handling driver private state
 *
 * Very often the DRM objects exposed to userspace in the atomic modeset api
 * (&drm_connector, &drm_crtc and &drm_plane) do not map neatly to the
 * underlying hardware. Especially for any kind of shared resources (e.g. shared
 * clocks, scaler units, bandwidth and fifo limits shared among a group of
 * planes or CRTCs, and so on) it makes sense to model these as independent
 * objects. Drivers then need to do similar state tracking and commit ordering for
 * such private (since not exposed to userpace) objects as the atomic core and
 * helpers already provide for connectors, planes and CRTCs.
 *
 * To make this easier on drivers the atomic core provides some support to track
 * driver private state objects using struct &drm_private_obj, with the
 * associated state struct &drm_private_state.
 *
 * Similar to userspace-exposed objects, private state structures can be
 * acquired by calling drm_atomic_get_private_obj_state(). Since this function
 * does not take care of locking, drivers should wrap it for each type of
 * private state object they have with the required call to drm_modeset_lock()
 * for the corresponding &drm_modeset_lock.
 *
 * All private state structures contained in a &drm_atomic_state update can be
 * iterated using for_each_oldnew_private_obj_in_state(),
 * for_each_new_private_obj_in_state() and for_each_old_private_obj_in_state().
 * Drivers are recommended to wrap these for each type of driver private state
 * object they have, filtering on &drm_private_obj.funcs using for_each_if(), at
 * least if they want to iterate over all objects of a given type.
 *
 * An earlier way to handle driver private state was by subclassing struct
 * &drm_atomic_state. But since that encourages non-standard ways to implement
 * the check/commit split atomic requires (by using e.g. "check and rollback or
 * commit instead" of "duplicate state, check, then either commit or release
 * duplicated state) it is deprecated in favour of using &drm_private_state.
 */

/**
 * drm_atomic_private_obj_init - initialize private object
 * @obj: private object
 * @state: initial private object state
 * @funcs: pointer to the struct of function pointers that identify the object
 * type
 *
 * Initialize the private object, which can be embedded into any
 * driver private object that needs its own atomic state.
 */
void
drm_atomic_private_obj_init(struct drm_private_obj *obj,
			    struct drm_private_state *state,
			    const struct drm_private_state_funcs *funcs)
{
	memset(obj, 0, sizeof(*obj));

	obj->state = state;
	obj->funcs = funcs;
}
EXPORT_SYMBOL(drm_atomic_private_obj_init);

/**
 * drm_atomic_private_obj_fini - finalize private object
 * @obj: private object
 *
 * Finalize the private object.
 */
void
drm_atomic_private_obj_fini(struct drm_private_obj *obj)
{
	obj->funcs->atomic_destroy_state(obj, obj->state);
}
EXPORT_SYMBOL(drm_atomic_private_obj_fini);

/**
 * drm_atomic_get_private_obj_state - get private object state
 * @state: global atomic state
 * @obj: private object to get the state for
 *
 * This function returns the private object state for the given private object,
 * allocating the state if needed. It does not grab any locks as the caller is
 * expected to care of any required locking.
 *
 * RETURNS:
 *
 * Either the allocated state or the error code encoded into a pointer.
 */
struct drm_private_state *
drm_atomic_get_private_obj_state(struct drm_atomic_state *state,
				 struct drm_private_obj *obj)
{
	int index, num_objs, i;
	size_t size;
	struct __drm_private_objs_state *arr;
	struct drm_private_state *obj_state;

	for (i = 0; i < state->num_private_objs; i++)
		if (obj == state->private_objs[i].ptr)
			return state->private_objs[i].state;

	num_objs = state->num_private_objs + 1;
	size = sizeof(*state->private_objs) * num_objs;
	arr = krealloc(state->private_objs, size, GFP_KERNEL);
	if (!arr)
		return ERR_PTR(-ENOMEM);

	state->private_objs = arr;
	index = state->num_private_objs;
	memset(&state->private_objs[index], 0, sizeof(*state->private_objs));

	obj_state = obj->funcs->atomic_duplicate_state(obj);
	if (!obj_state)
		return ERR_PTR(-ENOMEM);

	state->private_objs[index].state = obj_state;
	state->private_objs[index].old_state = obj->state;
	state->private_objs[index].new_state = obj_state;
	state->private_objs[index].ptr = obj;

	state->num_private_objs = num_objs;

	DRM_DEBUG_ATOMIC("Added new private object %p state %p to %p\n",
			 obj, obj_state, state);

	return obj_state;
}
EXPORT_SYMBOL(drm_atomic_get_private_obj_state);

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

	WARN_ON(!state->acquire_ctx);

	ret = drm_modeset_lock(&config->connection_mutex, state->acquire_ctx);
	if (ret)
		return ERR_PTR(ret);

	index = drm_connector_index(connector);

	if (index >= state->num_connector) {
		struct __drm_connnectors_state *c;
		int alloc = max(index + 1, config->num_connector);

		c = krealloc(state->connectors, alloc * sizeof(*state->connectors), GFP_KERNEL);
		if (!c)
			return ERR_PTR(-ENOMEM);

		state->connectors = c;
		memset(&state->connectors[state->num_connector], 0,
		       sizeof(*state->connectors) * (alloc - state->num_connector));

		state->num_connector = alloc;
	}

	if (state->connectors[index].state)
		return state->connectors[index].state;

	connector_state = connector->funcs->atomic_duplicate_state(connector);
	if (!connector_state)
		return ERR_PTR(-ENOMEM);

	drm_connector_get(connector);
	state->connectors[index].state = connector_state;
	state->connectors[index].old_state = connector->state;
	state->connectors[index].new_state = connector_state;
	state->connectors[index].ptr = connector;
	connector_state->state = state;

	DRM_DEBUG_ATOMIC("Added [CONNECTOR:%d:%s] %p state to %p\n",
			 connector->base.id, connector->name,
			 connector_state, state);

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
 * This function handles generic/core properties and calls out to driver's
 * &drm_connector_funcs.atomic_set_property for driver properties.  To ensure
 * consistent behavior you must call this function rather than the driver hook
 * directly.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
static int drm_atomic_connector_set_property(struct drm_connector *connector,
		struct drm_connector_state *state, struct drm_property *property,
		uint64_t val)
{
	struct drm_device *dev = connector->dev;
	struct drm_mode_config *config = &dev->mode_config;

	if (property == config->prop_crtc_id) {
		struct drm_crtc *crtc = drm_crtc_find(dev, NULL, val);
		return drm_atomic_set_crtc_for_connector(state, crtc);
	} else if (property == config->dpms_property) {
		/* setting DPMS property requires special handling, which
		 * is done in legacy setprop path for us.  Disallow (for
		 * now?) atomic writes to DPMS property:
		 */
		return -EINVAL;
	} else if (property == config->tv_select_subconnector_property) {
		state->tv.subconnector = val;
	} else if (property == config->tv_left_margin_property) {
		state->tv.margins.left = val;
	} else if (property == config->tv_right_margin_property) {
		state->tv.margins.right = val;
	} else if (property == config->tv_top_margin_property) {
		state->tv.margins.top = val;
	} else if (property == config->tv_bottom_margin_property) {
		state->tv.margins.bottom = val;
	} else if (property == config->tv_mode_property) {
		state->tv.mode = val;
	} else if (property == config->tv_brightness_property) {
		state->tv.brightness = val;
	} else if (property == config->tv_contrast_property) {
		state->tv.contrast = val;
	} else if (property == config->tv_flicker_reduction_property) {
		state->tv.flicker_reduction = val;
	} else if (property == config->tv_overscan_property) {
		state->tv.overscan = val;
	} else if (property == config->tv_saturation_property) {
		state->tv.saturation = val;
	} else if (property == config->tv_hue_property) {
		state->tv.hue = val;
	} else if (property == config->link_status_property) {
		/* Never downgrade from GOOD to BAD on userspace's request here,
		 * only hw issues can do that.
		 *
		 * For an atomic property the userspace doesn't need to be able
		 * to understand all the properties, but needs to be able to
		 * restore the state it wants on VT switch. So if the userspace
		 * tries to change the link_status from GOOD to BAD, driver
		 * silently rejects it and returns a 0. This prevents userspace
		 * from accidently breaking  the display when it restores the
		 * state.
		 */
		if (state->link_status != DRM_LINK_STATUS_GOOD)
			state->link_status = val;
	} else if (property == config->aspect_ratio_property) {
		state->picture_aspect_ratio = val;
	} else if (property == connector->scaling_mode_property) {
		state->scaling_mode = val;
	} else if (property == connector->content_protection_property) {
		if (val == DRM_MODE_CONTENT_PROTECTION_ENABLED) {
			DRM_DEBUG_KMS("only drivers can set CP Enabled\n");
			return -EINVAL;
		}
		state->content_protection = val;
	} else if (connector->funcs->atomic_set_property) {
		return connector->funcs->atomic_set_property(connector,
				state, property, val);
	} else {
		return -EINVAL;
	}

	return 0;
}

static void drm_atomic_connector_print_state(struct drm_printer *p,
		const struct drm_connector_state *state)
{
	struct drm_connector *connector = state->connector;

	drm_printf(p, "connector[%u]: %s\n", connector->base.id, connector->name);
	drm_printf(p, "\tcrtc=%s\n", state->crtc ? state->crtc->name : "(null)");

	if (connector->funcs->atomic_print_state)
		connector->funcs->atomic_print_state(p, state);
}

/**
 * drm_atomic_connector_get_property - get property value from connector state
 * @connector: the drm connector to set a property on
 * @state: the state object to get the property value from
 * @property: the property to set
 * @val: return location for the property value
 *
 * This function handles generic/core properties and calls out to driver's
 * &drm_connector_funcs.atomic_get_property for driver properties.  To ensure
 * consistent behavior you must call this function rather than the driver hook
 * directly.
 *
 * RETURNS:
 * Zero on success, error code on failure
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
	} else if (property == config->tv_select_subconnector_property) {
		*val = state->tv.subconnector;
	} else if (property == config->tv_left_margin_property) {
		*val = state->tv.margins.left;
	} else if (property == config->tv_right_margin_property) {
		*val = state->tv.margins.right;
	} else if (property == config->tv_top_margin_property) {
		*val = state->tv.margins.top;
	} else if (property == config->tv_bottom_margin_property) {
		*val = state->tv.margins.bottom;
	} else if (property == config->tv_mode_property) {
		*val = state->tv.mode;
	} else if (property == config->tv_brightness_property) {
		*val = state->tv.brightness;
	} else if (property == config->tv_contrast_property) {
		*val = state->tv.contrast;
	} else if (property == config->tv_flicker_reduction_property) {
		*val = state->tv.flicker_reduction;
	} else if (property == config->tv_overscan_property) {
		*val = state->tv.overscan;
	} else if (property == config->tv_saturation_property) {
		*val = state->tv.saturation;
	} else if (property == config->tv_hue_property) {
		*val = state->tv.hue;
	} else if (property == config->link_status_property) {
		*val = state->link_status;
	} else if (property == config->aspect_ratio_property) {
		*val = state->picture_aspect_ratio;
	} else if (property == connector->scaling_mode_property) {
		*val = state->scaling_mode;
	} else if (property == connector->content_protection_property) {
		*val = state->content_protection;
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
		DRM_DEBUG_ATOMIC("Link plane state %p to [CRTC:%d:%s]\n",
				 plane_state, crtc->base.id, crtc->name);
	else
		DRM_DEBUG_ATOMIC("Link plane state %p to [NOCRTC]\n",
				 plane_state);

	return 0;
}
EXPORT_SYMBOL(drm_atomic_set_crtc_for_plane);

/**
 * drm_atomic_set_fb_for_plane - set framebuffer for plane
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
	if (fb)
		DRM_DEBUG_ATOMIC("Set [FB:%d] for plane state %p\n",
				 fb->base.id, plane_state);
	else
		DRM_DEBUG_ATOMIC("Set [NOFB] for plane state %p\n",
				 plane_state);

	drm_framebuffer_assign(&plane_state->fb, fb);
}
EXPORT_SYMBOL(drm_atomic_set_fb_for_plane);

/**
 * drm_atomic_set_fence_for_plane - set fence for plane
 * @plane_state: atomic state object for the plane
 * @fence: dma_fence to use for the plane
 *
 * Helper to setup the plane_state fence in case it is not set yet.
 * By using this drivers doesn't need to worry if the user choose
 * implicit or explicit fencing.
 *
 * This function will not set the fence to the state if it was set
 * via explicit fencing interfaces on the atomic ioctl. In that case it will
 * drop the reference to the fence as we are not storing it anywhere.
 * Otherwise, if &drm_plane_state.fence is not set this function we just set it
 * with the received implicit fence. In both cases this function consumes a
 * reference for @fence.
 */
void
drm_atomic_set_fence_for_plane(struct drm_plane_state *plane_state,
			       struct dma_fence *fence)
{
	if (plane_state->fence) {
		dma_fence_put(fence);
		return;
	}

	plane_state->fence = fence;
}
EXPORT_SYMBOL(drm_atomic_set_fence_for_plane);

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

	if (conn_state->crtc == crtc)
		return 0;

	if (conn_state->crtc) {
		crtc_state = drm_atomic_get_new_crtc_state(conn_state->state,
							   conn_state->crtc);

		crtc_state->connector_mask &=
			~(1 << drm_connector_index(conn_state->connector));

		drm_connector_put(conn_state->connector);
		conn_state->crtc = NULL;
	}

	if (crtc) {
		crtc_state = drm_atomic_get_crtc_state(conn_state->state, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		crtc_state->connector_mask |=
			1 << drm_connector_index(conn_state->connector);

		drm_connector_get(conn_state->connector);
		conn_state->crtc = crtc;

		DRM_DEBUG_ATOMIC("Link connector state %p to [CRTC:%d:%s]\n",
				 conn_state, crtc->base.id, crtc->name);
	} else {
		DRM_DEBUG_ATOMIC("Link connector state %p to [NOCRTC]\n",
				 conn_state);
	}

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
	struct drm_connector_list_iter conn_iter;
	struct drm_crtc_state *crtc_state;
	int ret;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	ret = drm_modeset_lock(&config->connection_mutex, state->acquire_ctx);
	if (ret)
		return ret;

	DRM_DEBUG_ATOMIC("Adding all current connectors for [CRTC:%d:%s] to %p\n",
			 crtc->base.id, crtc->name, state);

	/*
	 * Changed connectors are already in @state, so only need to look
	 * at the connector_mask in crtc_state.
	 */
	drm_connector_list_iter_begin(state->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (!(crtc_state->connector_mask & (1 << drm_connector_index(connector))))
			continue;

		conn_state = drm_atomic_get_connector_state(state, connector);
		if (IS_ERR(conn_state)) {
			drm_connector_list_iter_end(&conn_iter);
			return PTR_ERR(conn_state);
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}
EXPORT_SYMBOL(drm_atomic_add_affected_connectors);

/**
 * drm_atomic_add_affected_planes - add planes for crtc
 * @state: atomic state
 * @crtc: DRM crtc
 *
 * This function walks the current configuration and adds all planes
 * currently used by @crtc to the atomic configuration @state. This is useful
 * when an atomic commit also needs to check all currently enabled plane on
 * @crtc, e.g. when changing the mode. It's also useful when re-enabling a CRTC
 * to avoid special code to force-enable all planes.
 *
 * Since acquiring a plane state will always also acquire the w/w mutex of the
 * current CRTC for that plane (if there is any) adding all the plane states for
 * a CRTC will not reduce parallism of atomic updates.
 *
 * Returns:
 * 0 on success or can fail with -EDEADLK or -ENOMEM. When the error is EDEADLK
 * then the w/w mutex code has detected a deadlock and the entire atomic
 * sequence must be restarted. All other errors are fatal.
 */
int
drm_atomic_add_affected_planes(struct drm_atomic_state *state,
			       struct drm_crtc *crtc)
{
	struct drm_plane *plane;

	WARN_ON(!drm_atomic_get_new_crtc_state(state, crtc));

	drm_for_each_plane_mask(plane, state->dev, crtc->state->plane_mask) {
		struct drm_plane_state *plane_state =
			drm_atomic_get_plane_state(state, plane);

		if (IS_ERR(plane_state))
			return PTR_ERR(plane_state);
	}
	return 0;
}
EXPORT_SYMBOL(drm_atomic_add_affected_planes);

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
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int i, ret = 0;

	DRM_DEBUG_ATOMIC("checking %p\n", state);

	for_each_new_plane_in_state(state, plane, plane_state, i) {
		ret = drm_atomic_plane_check(plane, plane_state);
		if (ret) {
			DRM_DEBUG_ATOMIC("[PLANE:%d:%s] atomic core check failed\n",
					 plane->base.id, plane->name);
			return ret;
		}
	}

	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		ret = drm_atomic_crtc_check(crtc, crtc_state);
		if (ret) {
			DRM_DEBUG_ATOMIC("[CRTC:%d:%s] atomic core check failed\n",
					 crtc->base.id, crtc->name);
			return ret;
		}
	}

	if (config->funcs->atomic_check)
		ret = config->funcs->atomic_check(state->dev, state);

	if (ret)
		return ret;

	if (!state->allow_modeset) {
		for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
			if (drm_atomic_crtc_needs_modeset(crtc_state)) {
				DRM_DEBUG_ATOMIC("[CRTC:%d:%s] requires full modeset\n",
						 crtc->base.id, crtc->name);
				return -EINVAL;
			}
		}
	}

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
 * This function will take its own reference on @state.
 * Callers should always release their reference with drm_atomic_state_put().
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

	DRM_DEBUG_ATOMIC("committing %p\n", state);

	return config->funcs->atomic_commit(state->dev, state, false);
}
EXPORT_SYMBOL(drm_atomic_commit);

/**
 * drm_atomic_nonblocking_commit - atomic nonblocking commit
 * @state: atomic configuration to check
 *
 * Note that this function can return -EDEADLK if the driver needed to acquire
 * more locks but encountered a deadlock. The caller must then do the usual w/w
 * backoff dance and restart. All other errors are fatal.
 *
 * This function will take its own reference on @state.
 * Callers should always release their reference with drm_atomic_state_put().
 *
 * Returns:
 * 0 on success, negative error code on failure.
 */
int drm_atomic_nonblocking_commit(struct drm_atomic_state *state)
{
	struct drm_mode_config *config = &state->dev->mode_config;
	int ret;

	ret = drm_atomic_check_only(state);
	if (ret)
		return ret;

	DRM_DEBUG_ATOMIC("committing %p nonblocking\n", state);

	return config->funcs->atomic_commit(state->dev, state, true);
}
EXPORT_SYMBOL(drm_atomic_nonblocking_commit);

static void drm_atomic_print_state(const struct drm_atomic_state *state)
{
	struct drm_printer p = drm_info_printer(state->dev->dev);
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	int i;

	DRM_DEBUG_ATOMIC("checking %p\n", state);

	for_each_new_plane_in_state(state, plane, plane_state, i)
		drm_atomic_plane_print_state(&p, plane_state);

	for_each_new_crtc_in_state(state, crtc, crtc_state, i)
		drm_atomic_crtc_print_state(&p, crtc_state);

	for_each_new_connector_in_state(state, connector, connector_state, i)
		drm_atomic_connector_print_state(&p, connector_state);
}

static void __drm_state_dump(struct drm_device *dev, struct drm_printer *p,
			     bool take_locks)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	if (!drm_core_check_feature(dev, DRIVER_ATOMIC))
		return;

	list_for_each_entry(plane, &config->plane_list, head) {
		if (take_locks)
			drm_modeset_lock(&plane->mutex, NULL);
		drm_atomic_plane_print_state(p, plane->state);
		if (take_locks)
			drm_modeset_unlock(&plane->mutex);
	}

	list_for_each_entry(crtc, &config->crtc_list, head) {
		if (take_locks)
			drm_modeset_lock(&crtc->mutex, NULL);
		drm_atomic_crtc_print_state(p, crtc->state);
		if (take_locks)
			drm_modeset_unlock(&crtc->mutex);
	}

	drm_connector_list_iter_begin(dev, &conn_iter);
	if (take_locks)
		drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	drm_for_each_connector_iter(connector, &conn_iter)
		drm_atomic_connector_print_state(p, connector->state);
	if (take_locks)
		drm_modeset_unlock(&dev->mode_config.connection_mutex);
	drm_connector_list_iter_end(&conn_iter);
}

/**
 * drm_state_dump - dump entire device atomic state
 * @dev: the drm device
 * @p: where to print the state to
 *
 * Just for debugging.  Drivers might want an option to dump state
 * to dmesg in case of error irq's.  (Hint, you probably want to
 * ratelimit this!)
 *
 * The caller must drm_modeset_lock_all(), or if this is called
 * from error irq handler, it should not be enabled by default.
 * (Ie. if you are debugging errors you might not care that this
 * is racey.  But calling this without all modeset locks held is
 * not inherently safe.)
 */
void drm_state_dump(struct drm_device *dev, struct drm_printer *p)
{
	__drm_state_dump(dev, p, false);
}
EXPORT_SYMBOL(drm_state_dump);

#ifdef CONFIG_DEBUG_FS
static int drm_state_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_printer p = drm_seq_file_printer(m);

	__drm_state_dump(dev, &p, true);

	return 0;
}

/* any use in debugfs files to dump individual planes/crtc/etc? */
static const struct drm_info_list drm_atomic_debugfs_list[] = {
	{"state", drm_state_info, 0},
};

int drm_atomic_debugfs_init(struct drm_minor *minor)
{
	return drm_debugfs_create_files(drm_atomic_debugfs_list,
			ARRAY_SIZE(drm_atomic_debugfs_list),
			minor->debugfs_root, minor);
}
#endif

/*
 * The big monster ioctl
 */

static struct drm_pending_vblank_event *create_vblank_event(
		struct drm_crtc *crtc, uint64_t user_data)
{
	struct drm_pending_vblank_event *e = NULL;

	e = kzalloc(sizeof *e, GFP_KERNEL);
	if (!e)
		return NULL;

	e->event.base.type = DRM_EVENT_FLIP_COMPLETE;
	e->event.base.length = sizeof(e->event);
	e->event.vbl.crtc_id = crtc->base.id;
	e->event.vbl.user_data = user_data;

	return e;
}

int drm_atomic_connector_commit_dpms(struct drm_atomic_state *state,
				     struct drm_connector *connector,
				     int mode)
{
	struct drm_connector *tmp_connector;
	struct drm_connector_state *new_conn_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int i, ret, old_mode = connector->dpms;
	bool active = false;

	ret = drm_modeset_lock(&state->dev->mode_config.connection_mutex,
			       state->acquire_ctx);
	if (ret)
		return ret;

	if (mode != DRM_MODE_DPMS_ON)
		mode = DRM_MODE_DPMS_OFF;
	connector->dpms = mode;

	crtc = connector->state->crtc;
	if (!crtc)
		goto out;
	ret = drm_atomic_add_affected_connectors(state, crtc);
	if (ret)
		goto out;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto out;
	}

	for_each_new_connector_in_state(state, tmp_connector, new_conn_state, i) {
		if (new_conn_state->crtc != crtc)
			continue;
		if (tmp_connector->dpms == DRM_MODE_DPMS_ON) {
			active = true;
			break;
		}
	}

	crtc_state->active = active;
	ret = drm_atomic_commit(state);
out:
	if (ret != 0)
		connector->dpms = old_mode;
	return ret;
}

int drm_atomic_set_property(struct drm_atomic_state *state,
			    struct drm_mode_object *obj,
			    struct drm_property *prop,
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

/**
 * drm_atomic_clean_old_fb -- Unset old_fb pointers and set plane->fb pointers.
 *
 * @dev: drm device to check.
 * @plane_mask: plane mask for planes that were updated.
 * @ret: return value, can be -EDEADLK for a retry.
 *
 * Before doing an update &drm_plane.old_fb is set to &drm_plane.fb, but before
 * dropping the locks old_fb needs to be set to NULL and plane->fb updated. This
 * is a common operation for each atomic update, so this call is split off as a
 * helper.
 */
void drm_atomic_clean_old_fb(struct drm_device *dev,
			     unsigned plane_mask,
			     int ret)
{
	struct drm_plane *plane;

	/* if succeeded, fixup legacy plane crtc/fb ptrs before dropping
	 * locks (ie. while it is still safe to deref plane->state).  We
	 * need to do this here because the driver entry points cannot
	 * distinguish between legacy and atomic ioctls.
	 */
	drm_for_each_plane_mask(plane, dev, plane_mask) {
		if (ret == 0) {
			struct drm_framebuffer *new_fb = plane->state->fb;
			if (new_fb)
				drm_framebuffer_get(new_fb);
			plane->fb = new_fb;
			plane->crtc = plane->state->crtc;

			if (plane->old_fb)
				drm_framebuffer_put(plane->old_fb);
		}
		plane->old_fb = NULL;
	}
}
EXPORT_SYMBOL(drm_atomic_clean_old_fb);

/**
 * DOC: explicit fencing properties
 *
 * Explicit fencing allows userspace to control the buffer synchronization
 * between devices. A Fence or a group of fences are transfered to/from
 * userspace using Sync File fds and there are two DRM properties for that.
 * IN_FENCE_FD on each DRM Plane to send fences to the kernel and
 * OUT_FENCE_PTR on each DRM CRTC to receive fences from the kernel.
 *
 * As a contrast, with implicit fencing the kernel keeps track of any
 * ongoing rendering, and automatically ensures that the atomic update waits
 * for any pending rendering to complete. For shared buffers represented with
 * a &struct dma_buf this is tracked in &struct reservation_object.
 * Implicit syncing is how Linux traditionally worked (e.g. DRI2/3 on X.org),
 * whereas explicit fencing is what Android wants.
 *
 * "IN_FENCE_FD”:
 *	Use this property to pass a fence that DRM should wait on before
 *	proceeding with the Atomic Commit request and show the framebuffer for
 *	the plane on the screen. The fence can be either a normal fence or a
 *	merged one, the sync_file framework will handle both cases and use a
 *	fence_array if a merged fence is received. Passing -1 here means no
 *	fences to wait on.
 *
 *	If the Atomic Commit request has the DRM_MODE_ATOMIC_TEST_ONLY flag
 *	it will only check if the Sync File is a valid one.
 *
 *	On the driver side the fence is stored on the @fence parameter of
 *	&struct drm_plane_state. Drivers which also support implicit fencing
 *	should set the implicit fence using drm_atomic_set_fence_for_plane(),
 *	to make sure there's consistent behaviour between drivers in precedence
 *	of implicit vs. explicit fencing.
 *
 * "OUT_FENCE_PTR”:
 *	Use this property to pass a file descriptor pointer to DRM. Once the
 *	Atomic Commit request call returns OUT_FENCE_PTR will be filled with
 *	the file descriptor number of a Sync File. This Sync File contains the
 *	CRTC fence that will be signaled when all framebuffers present on the
 *	Atomic Commit * request for that given CRTC are scanned out on the
 *	screen.
 *
 *	The Atomic Commit request fails if a invalid pointer is passed. If the
 *	Atomic Commit request fails for any other reason the out fence fd
 *	returned will be -1. On a Atomic Commit with the
 *	DRM_MODE_ATOMIC_TEST_ONLY flag the out fence will also be set to -1.
 *
 *	Note that out-fences don't have a special interface to drivers and are
 *	internally represented by a &struct drm_pending_vblank_event in struct
 *	&drm_crtc_state, which is also used by the nonblocking atomic commit
 *	helpers and for the DRM event handling for existing userspace.
 */

struct drm_out_fence_state {
	s32 __user *out_fence_ptr;
	struct sync_file *sync_file;
	int fd;
};

static int setup_out_fence(struct drm_out_fence_state *fence_state,
			   struct dma_fence *fence)
{
	fence_state->fd = get_unused_fd_flags(O_CLOEXEC);
	if (fence_state->fd < 0)
		return fence_state->fd;

	if (put_user(fence_state->fd, fence_state->out_fence_ptr))
		return -EFAULT;

	fence_state->sync_file = sync_file_create(fence);
	if (!fence_state->sync_file)
		return -ENOMEM;

	return 0;
}

static int prepare_crtc_signaling(struct drm_device *dev,
				  struct drm_atomic_state *state,
				  struct drm_mode_atomic *arg,
				  struct drm_file *file_priv,
				  struct drm_out_fence_state **fence_state,
				  unsigned int *num_fences)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int i, c = 0, ret;

	if (arg->flags & DRM_MODE_ATOMIC_TEST_ONLY)
		return 0;

	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		s32 __user *fence_ptr;

		fence_ptr = get_out_fence_for_crtc(crtc_state->state, crtc);

		if (arg->flags & DRM_MODE_PAGE_FLIP_EVENT || fence_ptr) {
			struct drm_pending_vblank_event *e;

			e = create_vblank_event(crtc, arg->user_data);
			if (!e)
				return -ENOMEM;

			crtc_state->event = e;
		}

		if (arg->flags & DRM_MODE_PAGE_FLIP_EVENT) {
			struct drm_pending_vblank_event *e = crtc_state->event;

			if (!file_priv)
				continue;

			ret = drm_event_reserve_init(dev, file_priv, &e->base,
						     &e->event.base);
			if (ret) {
				kfree(e);
				crtc_state->event = NULL;
				return ret;
			}
		}

		if (fence_ptr) {
			struct dma_fence *fence;
			struct drm_out_fence_state *f;

			f = krealloc(*fence_state, sizeof(**fence_state) *
				     (*num_fences + 1), GFP_KERNEL);
			if (!f)
				return -ENOMEM;

			memset(&f[*num_fences], 0, sizeof(*f));

			f[*num_fences].out_fence_ptr = fence_ptr;
			*fence_state = f;

			fence = drm_crtc_create_fence(crtc);
			if (!fence)
				return -ENOMEM;

			ret = setup_out_fence(&f[(*num_fences)++], fence);
			if (ret) {
				dma_fence_put(fence);
				return ret;
			}

			crtc_state->event->base.fence = fence;
		}

		c++;
	}

	/*
	 * Having this flag means user mode pends on event which will never
	 * reach due to lack of at least one CRTC for signaling
	 */
	if (c == 0 && (arg->flags & DRM_MODE_PAGE_FLIP_EVENT))
		return -EINVAL;

	return 0;
}

static void complete_crtc_signaling(struct drm_device *dev,
				    struct drm_atomic_state *state,
				    struct drm_out_fence_state *fence_state,
				    unsigned int num_fences,
				    bool install_fds)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int i;

	if (install_fds) {
		for (i = 0; i < num_fences; i++)
			fd_install(fence_state[i].fd,
				   fence_state[i].sync_file->file);

		kfree(fence_state);
		return;
	}

	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		struct drm_pending_vblank_event *event = crtc_state->event;
		/*
		 * Free the allocated event. drm_atomic_helper_setup_commit
		 * can allocate an event too, so only free it if it's ours
		 * to prevent a double free in drm_atomic_state_clear.
		 */
		if (event && (event->base.fence || event->base.file_priv)) {
			drm_event_cancel_free(dev, &event->base);
			crtc_state->event = NULL;
		}
	}

	if (!fence_state)
		return;

	for (i = 0; i < num_fences; i++) {
		if (fence_state[i].sync_file)
			fput(fence_state[i].sync_file->file);
		if (fence_state[i].fd >= 0)
			put_unused_fd(fence_state[i].fd);

		/* If this fails log error to the user */
		if (fence_state[i].out_fence_ptr &&
		    put_user(-1, fence_state[i].out_fence_ptr))
			DRM_DEBUG_ATOMIC("Couldn't clear out_fence_ptr\n");
	}

	kfree(fence_state);
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
	struct drm_out_fence_state *fence_state;
	unsigned plane_mask;
	int ret = 0;
	unsigned int i, j, num_fences;

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

	drm_modeset_acquire_init(&ctx, DRM_MODESET_ACQUIRE_INTERRUPTIBLE);

	state = drm_atomic_state_alloc(dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = &ctx;
	state->allow_modeset = !!(arg->flags & DRM_MODE_ATOMIC_ALLOW_MODESET);

retry:
	plane_mask = 0;
	copied_objs = 0;
	copied_props = 0;
	fence_state = NULL;
	num_fences = 0;

	for (i = 0; i < arg->count_objs; i++) {
		uint32_t obj_id, count_props;
		struct drm_mode_object *obj;

		if (get_user(obj_id, objs_ptr + copied_objs)) {
			ret = -EFAULT;
			goto out;
		}

		obj = drm_mode_object_find(dev, file_priv, obj_id, DRM_MODE_OBJECT_ANY);
		if (!obj) {
			ret = -ENOENT;
			goto out;
		}

		if (!obj->properties) {
			drm_mode_object_put(obj);
			ret = -ENOENT;
			goto out;
		}

		if (get_user(count_props, count_props_ptr + copied_objs)) {
			drm_mode_object_put(obj);
			ret = -EFAULT;
			goto out;
		}

		copied_objs++;

		for (j = 0; j < count_props; j++) {
			uint32_t prop_id;
			uint64_t prop_value;
			struct drm_property *prop;

			if (get_user(prop_id, props_ptr + copied_props)) {
				drm_mode_object_put(obj);
				ret = -EFAULT;
				goto out;
			}

			prop = drm_mode_obj_find_prop_id(obj, prop_id);
			if (!prop) {
				drm_mode_object_put(obj);
				ret = -ENOENT;
				goto out;
			}

			if (copy_from_user(&prop_value,
					   prop_values_ptr + copied_props,
					   sizeof(prop_value))) {
				drm_mode_object_put(obj);
				ret = -EFAULT;
				goto out;
			}

			ret = drm_atomic_set_property(state, obj, prop,
						      prop_value);
			if (ret) {
				drm_mode_object_put(obj);
				goto out;
			}

			copied_props++;
		}

		if (obj->type == DRM_MODE_OBJECT_PLANE && count_props &&
		    !(arg->flags & DRM_MODE_ATOMIC_TEST_ONLY)) {
			plane = obj_to_plane(obj);
			plane_mask |= (1 << drm_plane_index(plane));
			plane->old_fb = plane->fb;
		}
		drm_mode_object_put(obj);
	}

	ret = prepare_crtc_signaling(dev, state, arg, file_priv, &fence_state,
				     &num_fences);
	if (ret)
		goto out;

	if (arg->flags & DRM_MODE_ATOMIC_TEST_ONLY) {
		ret = drm_atomic_check_only(state);
	} else if (arg->flags & DRM_MODE_ATOMIC_NONBLOCK) {
		ret = drm_atomic_nonblocking_commit(state);
	} else {
		if (unlikely(drm_debug & DRM_UT_STATE))
			drm_atomic_print_state(state);

		ret = drm_atomic_commit(state);
	}

out:
	drm_atomic_clean_old_fb(dev, plane_mask, ret);

	complete_crtc_signaling(dev, state, fence_state, num_fences, !ret);

	if (ret == -EDEADLK) {
		drm_atomic_state_clear(state);
		ret = drm_modeset_backoff(&ctx);
		if (!ret)
			goto retry;
	}

	drm_atomic_state_put(state);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;
}
