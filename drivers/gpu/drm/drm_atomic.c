/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2014 Intel Corp.
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
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


#include <linux/sync_file.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_blend.h>
#include <drm/drm_bridge.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_mode.h>
#include <drm/drm_print.h>
#include <drm/drm_writeback.h>

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
 * drm_crtc_commit_wait - Waits for a commit to complete
 * @commit: &drm_crtc_commit to wait for
 *
 * Waits for a given &drm_crtc_commit to be programmed into the
 * hardware and flipped to.
 *
 * Returns:
 *
 * 0 on success, a negative error code otherwise.
 */
int drm_crtc_commit_wait(struct drm_crtc_commit *commit)
{
	unsigned long timeout = 10 * HZ;
	int ret;

	if (!commit)
		return 0;

	ret = wait_for_completion_timeout(&commit->hw_done, timeout);
	if (!ret) {
		drm_err(commit->crtc->dev, "hw_done timed out\n");
		return -ETIMEDOUT;
	}

	/*
	 * Currently no support for overwriting flips, hence
	 * stall for previous one to execute completely.
	 */
	ret = wait_for_completion_timeout(&commit->flip_done, timeout);
	if (!ret) {
		drm_err(commit->crtc->dev, "flip_done timed out\n");
		return -ETIMEDOUT;
	}

	return 0;
}
EXPORT_SYMBOL(drm_crtc_commit_wait);

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

	drm_dbg_atomic(dev, "Allocated atomic state %p\n", state);

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

	drm_dbg_atomic(dev, "Clearing atomic state %p\n", state);

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

		if (state->crtcs[i].commit) {
			drm_crtc_commit_put(state->crtcs[i].commit);
			state->crtcs[i].commit = NULL;
		}
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
 * per-object state for planes, CRTCs and connectors.
 */
void __drm_atomic_state_free(struct kref *ref)
{
	struct drm_atomic_state *state = container_of(ref, typeof(*state), ref);
	struct drm_mode_config *config = &state->dev->mode_config;

	drm_atomic_state_clear(state);

	drm_dbg_atomic(state->dev, "Freeing atomic state %p\n", state);

	if (config->funcs->atomic_state_free) {
		config->funcs->atomic_state_free(state);
	} else {
		drm_atomic_state_default_release(state);
		kfree(state);
	}
}
EXPORT_SYMBOL(__drm_atomic_state_free);

/**
 * drm_atomic_get_crtc_state - get CRTC state
 * @state: global atomic state object
 * @crtc: CRTC to get state object for
 *
 * This function returns the CRTC state for the given CRTC, allocating it if
 * needed. It will also grab the relevant CRTC lock to make sure that the state
 * is consistent.
 *
 * WARNING: Drivers may only add new CRTC states to a @state if
 * drm_atomic_state.allow_modeset is set, or if it's a driver-internal commit
 * not created by userspace through an IOCTL call.
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

	drm_dbg_atomic(state->dev, "Added [CRTC:%d:%s] %p state to %p\n",
		       crtc->base.id, crtc->name, crtc_state, state);

	return crtc_state;
}
EXPORT_SYMBOL(drm_atomic_get_crtc_state);

static int drm_atomic_crtc_check(const struct drm_crtc_state *old_crtc_state,
				 const struct drm_crtc_state *new_crtc_state)
{
	struct drm_crtc *crtc = new_crtc_state->crtc;

	/* NOTE: we explicitly don't enforce constraints such as primary
	 * layer covering entire screen, since that is something we want
	 * to allow (on hw that supports it).  For hw that does not, it
	 * should be checked in driver's crtc->atomic_check() vfunc.
	 *
	 * TODO: Add generic modeset state checks once we support those.
	 */

	if (new_crtc_state->active && !new_crtc_state->enable) {
		drm_dbg_atomic(crtc->dev,
			       "[CRTC:%d:%s] active without enabled\n",
			       crtc->base.id, crtc->name);
		return -EINVAL;
	}

	/* The state->enable vs. state->mode_blob checks can be WARN_ON,
	 * as this is a kernel-internal detail that userspace should never
	 * be able to trigger.
	 */
	if (drm_core_check_feature(crtc->dev, DRIVER_ATOMIC) &&
	    WARN_ON(new_crtc_state->enable && !new_crtc_state->mode_blob)) {
		drm_dbg_atomic(crtc->dev,
			       "[CRTC:%d:%s] enabled without mode blob\n",
			       crtc->base.id, crtc->name);
		return -EINVAL;
	}

	if (drm_core_check_feature(crtc->dev, DRIVER_ATOMIC) &&
	    WARN_ON(!new_crtc_state->enable && new_crtc_state->mode_blob)) {
		drm_dbg_atomic(crtc->dev,
			       "[CRTC:%d:%s] disabled with mode blob\n",
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
	if (new_crtc_state->event &&
	    !new_crtc_state->active && !old_crtc_state->active) {
		drm_dbg_atomic(crtc->dev,
			       "[CRTC:%d:%s] requesting event but off\n",
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
	drm_printf(p, "\tself_refresh_active=%d\n", state->self_refresh_active);
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

static int drm_atomic_connector_check(struct drm_connector *connector,
		struct drm_connector_state *state)
{
	struct drm_crtc_state *crtc_state;
	struct drm_writeback_job *writeback_job = state->writeback_job;
	const struct drm_display_info *info = &connector->display_info;

	state->max_bpc = info->bpc ? info->bpc : 8;
	if (connector->max_bpc_property)
		state->max_bpc = min(state->max_bpc, state->max_requested_bpc);

	if ((connector->connector_type != DRM_MODE_CONNECTOR_WRITEBACK) || !writeback_job)
		return 0;

	if (writeback_job->fb && !state->crtc) {
		drm_dbg_atomic(connector->dev,
			       "[CONNECTOR:%d:%s] framebuffer without CRTC\n",
			       connector->base.id, connector->name);
		return -EINVAL;
	}

	if (state->crtc)
		crtc_state = drm_atomic_get_existing_crtc_state(state->state,
								state->crtc);

	if (writeback_job->fb && !crtc_state->active) {
		drm_dbg_atomic(connector->dev,
			       "[CONNECTOR:%d:%s] has framebuffer, but [CRTC:%d] is off\n",
			       connector->base.id, connector->name,
			       state->crtc->base.id);
		return -EINVAL;
	}

	if (!writeback_job->fb) {
		if (writeback_job->out_fence) {
			drm_dbg_atomic(connector->dev,
				       "[CONNECTOR:%d:%s] requesting out-fence without framebuffer\n",
				       connector->base.id, connector->name);
			return -EINVAL;
		}

		drm_writeback_cleanup_job(writeback_job);
		state->writeback_job = NULL;
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
	int ret, index = drm_plane_index(plane);
	struct drm_plane_state *plane_state;

	WARN_ON(!state->acquire_ctx);

	/* the legacy pointers should never be set */
	WARN_ON(plane->fb);
	WARN_ON(plane->old_fb);
	WARN_ON(plane->crtc);

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

	drm_dbg_atomic(plane->dev, "Added [PLANE:%d:%s] %p state to %p\n",
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

static bool
plane_switching_crtc(const struct drm_plane_state *old_plane_state,
		     const struct drm_plane_state *new_plane_state)
{
	if (!old_plane_state->crtc || !new_plane_state->crtc)
		return false;

	if (old_plane_state->crtc == new_plane_state->crtc)
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
 * @old_plane_state: old plane state to check
 * @new_plane_state: new plane state to check
 *
 * Provides core sanity checks for plane state.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
static int drm_atomic_plane_check(const struct drm_plane_state *old_plane_state,
				  const struct drm_plane_state *new_plane_state)
{
	struct drm_plane *plane = new_plane_state->plane;
	struct drm_crtc *crtc = new_plane_state->crtc;
	const struct drm_framebuffer *fb = new_plane_state->fb;
	unsigned int fb_width, fb_height;
	struct drm_mode_rect *clips;
	uint32_t num_clips;
	int ret;

	/* either *both* CRTC and FB must be set, or neither */
	if (crtc && !fb) {
		drm_dbg_atomic(plane->dev, "[PLANE:%d:%s] CRTC set but no FB\n",
			       plane->base.id, plane->name);
		return -EINVAL;
	} else if (fb && !crtc) {
		drm_dbg_atomic(plane->dev, "[PLANE:%d:%s] FB set but no CRTC\n",
			       plane->base.id, plane->name);
		return -EINVAL;
	}

	/* if disabled, we don't care about the rest of the state: */
	if (!crtc)
		return 0;

	/* Check whether this plane is usable on this CRTC */
	if (!(plane->possible_crtcs & drm_crtc_mask(crtc))) {
		drm_dbg_atomic(plane->dev,
			       "Invalid [CRTC:%d:%s] for [PLANE:%d:%s]\n",
			       crtc->base.id, crtc->name,
			       plane->base.id, plane->name);
		return -EINVAL;
	}

	/* Check whether this plane supports the fb pixel format. */
	ret = drm_plane_check_pixel_format(plane, fb->format->format,
					   fb->modifier);
	if (ret) {
		drm_dbg_atomic(plane->dev,
			       "[PLANE:%d:%s] invalid pixel format %p4cc, modifier 0x%llx\n",
			       plane->base.id, plane->name,
			       &fb->format->format, fb->modifier);
		return ret;
	}

	/* Give drivers some help against integer overflows */
	if (new_plane_state->crtc_w > INT_MAX ||
	    new_plane_state->crtc_x > INT_MAX - (int32_t) new_plane_state->crtc_w ||
	    new_plane_state->crtc_h > INT_MAX ||
	    new_plane_state->crtc_y > INT_MAX - (int32_t) new_plane_state->crtc_h) {
		drm_dbg_atomic(plane->dev,
			       "[PLANE:%d:%s] invalid CRTC coordinates %ux%u+%d+%d\n",
			       plane->base.id, plane->name,
			       new_plane_state->crtc_w, new_plane_state->crtc_h,
			       new_plane_state->crtc_x, new_plane_state->crtc_y);
		return -ERANGE;
	}

	fb_width = fb->width << 16;
	fb_height = fb->height << 16;

	/* Make sure source coordinates are inside the fb. */
	if (new_plane_state->src_w > fb_width ||
	    new_plane_state->src_x > fb_width - new_plane_state->src_w ||
	    new_plane_state->src_h > fb_height ||
	    new_plane_state->src_y > fb_height - new_plane_state->src_h) {
		drm_dbg_atomic(plane->dev,
			       "[PLANE:%d:%s] invalid source coordinates "
			       "%u.%06ux%u.%06u+%u.%06u+%u.%06u (fb %ux%u)\n",
			       plane->base.id, plane->name,
			       new_plane_state->src_w >> 16,
			       ((new_plane_state->src_w & 0xffff) * 15625) >> 10,
			       new_plane_state->src_h >> 16,
			       ((new_plane_state->src_h & 0xffff) * 15625) >> 10,
			       new_plane_state->src_x >> 16,
			       ((new_plane_state->src_x & 0xffff) * 15625) >> 10,
			       new_plane_state->src_y >> 16,
			       ((new_plane_state->src_y & 0xffff) * 15625) >> 10,
			       fb->width, fb->height);
		return -ENOSPC;
	}

	clips = __drm_plane_get_damage_clips(new_plane_state);
	num_clips = drm_plane_get_damage_clips_count(new_plane_state);

	/* Make sure damage clips are valid and inside the fb. */
	while (num_clips > 0) {
		if (clips->x1 >= clips->x2 ||
		    clips->y1 >= clips->y2 ||
		    clips->x1 < 0 ||
		    clips->y1 < 0 ||
		    clips->x2 > fb_width ||
		    clips->y2 > fb_height) {
			drm_dbg_atomic(plane->dev,
				       "[PLANE:%d:%s] invalid damage clip %d %d %d %d\n",
				       plane->base.id, plane->name, clips->x1,
				       clips->y1, clips->x2, clips->y2);
			return -EINVAL;
		}
		clips++;
		num_clips--;
	}

	if (plane_switching_crtc(old_plane_state, new_plane_state)) {
		drm_dbg_atomic(plane->dev,
			       "[PLANE:%d:%s] switching CRTC directly\n",
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
	drm_printf(p, "\tnormalized-zpos=%x\n", state->normalized_zpos);
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
 * such private (since not exposed to userspace) objects as the atomic core and
 * helpers already provide for connectors, planes and CRTCs.
 *
 * To make this easier on drivers the atomic core provides some support to track
 * driver private state objects using struct &drm_private_obj, with the
 * associated state struct &drm_private_state.
 *
 * Similar to userspace-exposed objects, private state structures can be
 * acquired by calling drm_atomic_get_private_obj_state(). This also takes care
 * of locking, hence drivers should not have a need to call drm_modeset_lock()
 * directly. Sequence of the actual hardware state commit is not handled,
 * drivers might need to keep track of struct drm_crtc_commit within subclassed
 * structure of &drm_private_state as necessary, e.g. similar to
 * &drm_plane_state.commit. See also &drm_atomic_state.fake_commit.
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
 * @dev: DRM device this object will be attached to
 * @obj: private object
 * @state: initial private object state
 * @funcs: pointer to the struct of function pointers that identify the object
 * type
 *
 * Initialize the private object, which can be embedded into any
 * driver private object that needs its own atomic state.
 */
void
drm_atomic_private_obj_init(struct drm_device *dev,
			    struct drm_private_obj *obj,
			    struct drm_private_state *state,
			    const struct drm_private_state_funcs *funcs)
{
	memset(obj, 0, sizeof(*obj));

	drm_modeset_lock_init(&obj->lock);

	obj->state = state;
	obj->funcs = funcs;
	list_add_tail(&obj->head, &dev->mode_config.privobj_list);

	state->obj = obj;
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
	list_del(&obj->head);
	obj->funcs->atomic_destroy_state(obj, obj->state);
	drm_modeset_lock_fini(&obj->lock);
}
EXPORT_SYMBOL(drm_atomic_private_obj_fini);

/**
 * drm_atomic_get_private_obj_state - get private object state
 * @state: global atomic state
 * @obj: private object to get the state for
 *
 * This function returns the private object state for the given private object,
 * allocating the state if needed. It will also grab the relevant private
 * object lock to make sure that the state is consistent.
 *
 * RETURNS:
 *
 * Either the allocated state or the error code encoded into a pointer.
 */
struct drm_private_state *
drm_atomic_get_private_obj_state(struct drm_atomic_state *state,
				 struct drm_private_obj *obj)
{
	int index, num_objs, i, ret;
	size_t size;
	struct __drm_private_objs_state *arr;
	struct drm_private_state *obj_state;

	for (i = 0; i < state->num_private_objs; i++)
		if (obj == state->private_objs[i].ptr)
			return state->private_objs[i].state;

	ret = drm_modeset_lock(&obj->lock, state->acquire_ctx);
	if (ret)
		return ERR_PTR(ret);

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
	obj_state->state = state;

	state->num_private_objs = num_objs;

	drm_dbg_atomic(state->dev,
		       "Added new private object %p state %p to %p\n",
		       obj, obj_state, state);

	return obj_state;
}
EXPORT_SYMBOL(drm_atomic_get_private_obj_state);

/**
 * drm_atomic_get_old_private_obj_state
 * @state: global atomic state object
 * @obj: private_obj to grab
 *
 * This function returns the old private object state for the given private_obj,
 * or NULL if the private_obj is not part of the global atomic state.
 */
struct drm_private_state *
drm_atomic_get_old_private_obj_state(struct drm_atomic_state *state,
				     struct drm_private_obj *obj)
{
	int i;

	for (i = 0; i < state->num_private_objs; i++)
		if (obj == state->private_objs[i].ptr)
			return state->private_objs[i].old_state;

	return NULL;
}
EXPORT_SYMBOL(drm_atomic_get_old_private_obj_state);

/**
 * drm_atomic_get_new_private_obj_state
 * @state: global atomic state object
 * @obj: private_obj to grab
 *
 * This function returns the new private object state for the given private_obj,
 * or NULL if the private_obj is not part of the global atomic state.
 */
struct drm_private_state *
drm_atomic_get_new_private_obj_state(struct drm_atomic_state *state,
				     struct drm_private_obj *obj)
{
	int i;

	for (i = 0; i < state->num_private_objs; i++)
		if (obj == state->private_objs[i].ptr)
			return state->private_objs[i].new_state;

	return NULL;
}
EXPORT_SYMBOL(drm_atomic_get_new_private_obj_state);

/**
 * drm_atomic_get_old_connector_for_encoder - Get old connector for an encoder
 * @state: Atomic state
 * @encoder: The encoder to fetch the connector state for
 *
 * This function finds and returns the connector that was connected to @encoder
 * as specified by the @state.
 *
 * If there is no connector in @state which previously had @encoder connected to
 * it, this function will return NULL. While this may seem like an invalid use
 * case, it is sometimes useful to differentiate commits which had no prior
 * connectors attached to @encoder vs ones that did (and to inspect their
 * state). This is especially true in enable hooks because the pipeline has
 * changed.
 *
 * Returns: The old connector connected to @encoder, or NULL if the encoder is
 * not connected.
 */
struct drm_connector *
drm_atomic_get_old_connector_for_encoder(struct drm_atomic_state *state,
					 struct drm_encoder *encoder)
{
	struct drm_connector_state *conn_state;
	struct drm_connector *connector;
	unsigned int i;

	for_each_old_connector_in_state(state, connector, conn_state, i) {
		if (conn_state->best_encoder == encoder)
			return connector;
	}

	return NULL;
}
EXPORT_SYMBOL(drm_atomic_get_old_connector_for_encoder);

/**
 * drm_atomic_get_new_connector_for_encoder - Get new connector for an encoder
 * @state: Atomic state
 * @encoder: The encoder to fetch the connector state for
 *
 * This function finds and returns the connector that will be connected to
 * @encoder as specified by the @state.
 *
 * If there is no connector in @state which will have @encoder connected to it,
 * this function will return NULL. While this may seem like an invalid use case,
 * it is sometimes useful to differentiate commits which have no connectors
 * attached to @encoder vs ones that do (and to inspect their state). This is
 * especially true in disable hooks because the pipeline will change.
 *
 * Returns: The new connector connected to @encoder, or NULL if the encoder is
 * not connected.
 */
struct drm_connector *
drm_atomic_get_new_connector_for_encoder(struct drm_atomic_state *state,
					 struct drm_encoder *encoder)
{
	struct drm_connector_state *conn_state;
	struct drm_connector *connector;
	unsigned int i;

	for_each_new_connector_in_state(state, connector, conn_state, i) {
		if (conn_state->best_encoder == encoder)
			return connector;
	}

	return NULL;
}
EXPORT_SYMBOL(drm_atomic_get_new_connector_for_encoder);

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

		c = krealloc_array(state->connectors, alloc,
				   sizeof(*state->connectors), GFP_KERNEL);
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

	drm_dbg_atomic(connector->dev, "Added [CONNECTOR:%d:%s] %p state to %p\n",
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

static void drm_atomic_connector_print_state(struct drm_printer *p,
		const struct drm_connector_state *state)
{
	struct drm_connector *connector = state->connector;

	drm_printf(p, "connector[%u]: %s\n", connector->base.id, connector->name);
	drm_printf(p, "\tcrtc=%s\n", state->crtc ? state->crtc->name : "(null)");
	drm_printf(p, "\tself_refresh_aware=%d\n", state->self_refresh_aware);
	drm_printf(p, "\tmax_requested_bpc=%d\n", state->max_requested_bpc);

	if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
		if (state->writeback_job && state->writeback_job->fb)
			drm_printf(p, "\tfb=%d\n", state->writeback_job->fb->base.id);

	if (connector->funcs->atomic_print_state)
		connector->funcs->atomic_print_state(p, state);
}

/**
 * drm_atomic_get_bridge_state - get bridge state
 * @state: global atomic state object
 * @bridge: bridge to get state object for
 *
 * This function returns the bridge state for the given bridge, allocating it
 * if needed. It will also grab the relevant bridge lock to make sure that the
 * state is consistent.
 *
 * Returns:
 *
 * Either the allocated state or the error code encoded into the pointer. When
 * the error is EDEADLK then the w/w mutex code has detected a deadlock and the
 * entire atomic sequence must be restarted.
 */
struct drm_bridge_state *
drm_atomic_get_bridge_state(struct drm_atomic_state *state,
			    struct drm_bridge *bridge)
{
	struct drm_private_state *obj_state;

	obj_state = drm_atomic_get_private_obj_state(state, &bridge->base);
	if (IS_ERR(obj_state))
		return ERR_CAST(obj_state);

	return drm_priv_to_bridge_state(obj_state);
}
EXPORT_SYMBOL(drm_atomic_get_bridge_state);

/**
 * drm_atomic_get_old_bridge_state - get old bridge state, if it exists
 * @state: global atomic state object
 * @bridge: bridge to grab
 *
 * This function returns the old bridge state for the given bridge, or NULL if
 * the bridge is not part of the global atomic state.
 */
struct drm_bridge_state *
drm_atomic_get_old_bridge_state(struct drm_atomic_state *state,
				struct drm_bridge *bridge)
{
	struct drm_private_state *obj_state;

	obj_state = drm_atomic_get_old_private_obj_state(state, &bridge->base);
	if (!obj_state)
		return NULL;

	return drm_priv_to_bridge_state(obj_state);
}
EXPORT_SYMBOL(drm_atomic_get_old_bridge_state);

/**
 * drm_atomic_get_new_bridge_state - get new bridge state, if it exists
 * @state: global atomic state object
 * @bridge: bridge to grab
 *
 * This function returns the new bridge state for the given bridge, or NULL if
 * the bridge is not part of the global atomic state.
 */
struct drm_bridge_state *
drm_atomic_get_new_bridge_state(struct drm_atomic_state *state,
				struct drm_bridge *bridge)
{
	struct drm_private_state *obj_state;

	obj_state = drm_atomic_get_new_private_obj_state(state, &bridge->base);
	if (!obj_state)
		return NULL;

	return drm_priv_to_bridge_state(obj_state);
}
EXPORT_SYMBOL(drm_atomic_get_new_bridge_state);

/**
 * drm_atomic_add_encoder_bridges - add bridges attached to an encoder
 * @state: atomic state
 * @encoder: DRM encoder
 *
 * This function adds all bridges attached to @encoder. This is needed to add
 * bridge states to @state and make them available when
 * &drm_bridge_funcs.atomic_check(), &drm_bridge_funcs.atomic_pre_enable(),
 * &drm_bridge_funcs.atomic_enable(),
 * &drm_bridge_funcs.atomic_disable_post_disable() are called.
 *
 * Returns:
 * 0 on success or can fail with -EDEADLK or -ENOMEM. When the error is EDEADLK
 * then the w/w mutex code has detected a deadlock and the entire atomic
 * sequence must be restarted. All other errors are fatal.
 */
int
drm_atomic_add_encoder_bridges(struct drm_atomic_state *state,
			       struct drm_encoder *encoder)
{
	struct drm_bridge_state *bridge_state;
	struct drm_bridge *bridge;

	if (!encoder)
		return 0;

	drm_dbg_atomic(encoder->dev,
		       "Adding all bridges for [encoder:%d:%s] to %p\n",
		       encoder->base.id, encoder->name, state);

	drm_for_each_bridge_in_chain(encoder, bridge) {
		/* Skip bridges that don't implement the atomic state hooks. */
		if (!bridge->funcs->atomic_duplicate_state)
			continue;

		bridge_state = drm_atomic_get_bridge_state(state, bridge);
		if (IS_ERR(bridge_state))
			return PTR_ERR(bridge_state);
	}

	return 0;
}
EXPORT_SYMBOL(drm_atomic_add_encoder_bridges);

/**
 * drm_atomic_add_affected_connectors - add connectors for CRTC
 * @state: atomic state
 * @crtc: DRM CRTC
 *
 * This function walks the current configuration and adds all connectors
 * currently using @crtc to the atomic configuration @state. Note that this
 * function must acquire the connection mutex. This can potentially cause
 * unneeded serialization if the update is just for the planes on one CRTC. Hence
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

	drm_dbg_atomic(crtc->dev,
		       "Adding all current connectors for [CRTC:%d:%s] to %p\n",
		       crtc->base.id, crtc->name, state);

	/*
	 * Changed connectors are already in @state, so only need to look
	 * at the connector_mask in crtc_state.
	 */
	drm_connector_list_iter_begin(state->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (!(crtc_state->connector_mask & drm_connector_mask(connector)))
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
 * drm_atomic_add_affected_planes - add planes for CRTC
 * @state: atomic state
 * @crtc: DRM CRTC
 *
 * This function walks the current configuration and adds all planes
 * currently used by @crtc to the atomic configuration @state. This is useful
 * when an atomic commit also needs to check all currently enabled plane on
 * @crtc, e.g. when changing the mode. It's also useful when re-enabling a CRTC
 * to avoid special code to force-enable all planes.
 *
 * Since acquiring a plane state will always also acquire the w/w mutex of the
 * current CRTC for that plane (if there is any) adding all the plane states for
 * a CRTC will not reduce parallelism of atomic updates.
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
	const struct drm_crtc_state *old_crtc_state =
		drm_atomic_get_old_crtc_state(state, crtc);
	struct drm_plane *plane;

	WARN_ON(!drm_atomic_get_new_crtc_state(state, crtc));

	drm_dbg_atomic(crtc->dev,
		       "Adding all current planes for [CRTC:%d:%s] to %p\n",
		       crtc->base.id, crtc->name, state);

	drm_for_each_plane_mask(plane, state->dev, old_crtc_state->plane_mask) {
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
	struct drm_plane_state *old_plane_state;
	struct drm_plane_state *new_plane_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	struct drm_crtc_state *new_crtc_state;
	struct drm_connector *conn;
	struct drm_connector_state *conn_state;
	unsigned int requested_crtc = 0;
	unsigned int affected_crtc = 0;
	int i, ret = 0;

	drm_dbg_atomic(dev, "checking %p\n", state);

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (new_crtc_state->enable)
			requested_crtc |= drm_crtc_mask(crtc);
	}

	for_each_oldnew_plane_in_state(state, plane, old_plane_state, new_plane_state, i) {
		ret = drm_atomic_plane_check(old_plane_state, new_plane_state);
		if (ret) {
			drm_dbg_atomic(dev, "[PLANE:%d:%s] atomic core check failed\n",
				       plane->base.id, plane->name);
			return ret;
		}
	}

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		ret = drm_atomic_crtc_check(old_crtc_state, new_crtc_state);
		if (ret) {
			drm_dbg_atomic(dev, "[CRTC:%d:%s] atomic core check failed\n",
				       crtc->base.id, crtc->name);
			return ret;
		}
	}

	for_each_new_connector_in_state(state, conn, conn_state, i) {
		ret = drm_atomic_connector_check(conn, conn_state);
		if (ret) {
			drm_dbg_atomic(dev, "[CONNECTOR:%d:%s] atomic core check failed\n",
				       conn->base.id, conn->name);
			return ret;
		}
	}

	if (config->funcs->atomic_check) {
		ret = config->funcs->atomic_check(state->dev, state);

		if (ret) {
			drm_dbg_atomic(dev, "atomic driver check for %p failed: %d\n",
				       state, ret);
			return ret;
		}
	}

	if (!state->allow_modeset) {
		for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
			if (drm_atomic_crtc_needs_modeset(new_crtc_state)) {
				drm_dbg_atomic(dev, "[CRTC:%d:%s] requires full modeset\n",
					       crtc->base.id, crtc->name);
				return -EINVAL;
			}
		}
	}

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (new_crtc_state->enable)
			affected_crtc |= drm_crtc_mask(crtc);
	}

	/*
	 * For commits that allow modesets drivers can add other CRTCs to the
	 * atomic commit, e.g. when they need to reallocate global resources.
	 * This can cause spurious EBUSY, which robs compositors of a very
	 * effective sanity check for their drawing loop. Therefor only allow
	 * drivers to add unrelated CRTC states for modeset commits.
	 *
	 * FIXME: Should add affected_crtc mask to the ATOMIC IOCTL as an output
	 * so compositors know what's going on.
	 */
	if (affected_crtc != requested_crtc) {
		drm_dbg_atomic(dev,
			       "driver added CRTC to commit: requested 0x%x, affected 0x%0x\n",
			       requested_crtc, affected_crtc);
		WARN(!state->allow_modeset, "adding CRTC not allowed without modesets: requested 0x%x, affected 0x%0x\n",
		     requested_crtc, affected_crtc);
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
	struct drm_printer p = drm_info_printer(state->dev->dev);
	int ret;

	if (drm_debug_enabled(DRM_UT_STATE))
		drm_atomic_print_new_state(state, &p);

	ret = drm_atomic_check_only(state);
	if (ret)
		return ret;

	drm_dbg_atomic(state->dev, "committing %p\n", state);

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

	drm_dbg_atomic(state->dev, "committing %p nonblocking\n", state);

	return config->funcs->atomic_commit(state->dev, state, true);
}
EXPORT_SYMBOL(drm_atomic_nonblocking_commit);

/* just used from drm-client and atomic-helper: */
int __drm_atomic_helper_disable_plane(struct drm_plane *plane,
				      struct drm_plane_state *plane_state)
{
	int ret;

	ret = drm_atomic_set_crtc_for_plane(plane_state, NULL);
	if (ret != 0)
		return ret;

	drm_atomic_set_fb_for_plane(plane_state, NULL);
	plane_state->crtc_x = 0;
	plane_state->crtc_y = 0;
	plane_state->crtc_w = 0;
	plane_state->crtc_h = 0;
	plane_state->src_x = 0;
	plane_state->src_y = 0;
	plane_state->src_w = 0;
	plane_state->src_h = 0;

	return 0;
}
EXPORT_SYMBOL(__drm_atomic_helper_disable_plane);

static int update_output_state(struct drm_atomic_state *state,
			       struct drm_mode_set *set)
{
	struct drm_device *dev = set->crtc->dev;
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *new_conn_state;
	int ret, i;

	ret = drm_modeset_lock(&dev->mode_config.connection_mutex,
			       state->acquire_ctx);
	if (ret)
		return ret;

	/* First disable all connectors on the target crtc. */
	ret = drm_atomic_add_affected_connectors(state, set->crtc);
	if (ret)
		return ret;

	for_each_new_connector_in_state(state, connector, new_conn_state, i) {
		if (new_conn_state->crtc == set->crtc) {
			ret = drm_atomic_set_crtc_for_connector(new_conn_state,
								NULL);
			if (ret)
				return ret;

			/* Make sure legacy setCrtc always re-trains */
			new_conn_state->link_status = DRM_LINK_STATUS_GOOD;
		}
	}

	/* Then set all connectors from set->connectors on the target crtc */
	for (i = 0; i < set->num_connectors; i++) {
		new_conn_state = drm_atomic_get_connector_state(state,
								set->connectors[i]);
		if (IS_ERR(new_conn_state))
			return PTR_ERR(new_conn_state);

		ret = drm_atomic_set_crtc_for_connector(new_conn_state,
							set->crtc);
		if (ret)
			return ret;
	}

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		/*
		 * Don't update ->enable for the CRTC in the set_config request,
		 * since a mismatch would indicate a bug in the upper layers.
		 * The actual modeset code later on will catch any
		 * inconsistencies here.
		 */
		if (crtc == set->crtc)
			continue;

		if (!new_crtc_state->connector_mask) {
			ret = drm_atomic_set_mode_prop_for_crtc(new_crtc_state,
								NULL);
			if (ret < 0)
				return ret;

			new_crtc_state->active = false;
		}
	}

	return 0;
}

/* just used from drm-client and atomic-helper: */
int __drm_atomic_helper_set_config(struct drm_mode_set *set,
				   struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state;
	struct drm_plane_state *primary_state;
	struct drm_crtc *crtc = set->crtc;
	int hdisplay, vdisplay;
	int ret;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	primary_state = drm_atomic_get_plane_state(state, crtc->primary);
	if (IS_ERR(primary_state))
		return PTR_ERR(primary_state);

	if (!set->mode) {
		WARN_ON(set->fb);
		WARN_ON(set->num_connectors);

		ret = drm_atomic_set_mode_for_crtc(crtc_state, NULL);
		if (ret != 0)
			return ret;

		crtc_state->active = false;

		ret = drm_atomic_set_crtc_for_plane(primary_state, NULL);
		if (ret != 0)
			return ret;

		drm_atomic_set_fb_for_plane(primary_state, NULL);

		goto commit;
	}

	WARN_ON(!set->fb);
	WARN_ON(!set->num_connectors);

	ret = drm_atomic_set_mode_for_crtc(crtc_state, set->mode);
	if (ret != 0)
		return ret;

	crtc_state->active = true;

	ret = drm_atomic_set_crtc_for_plane(primary_state, crtc);
	if (ret != 0)
		return ret;

	drm_mode_get_hv_timing(set->mode, &hdisplay, &vdisplay);

	drm_atomic_set_fb_for_plane(primary_state, set->fb);
	primary_state->crtc_x = 0;
	primary_state->crtc_y = 0;
	primary_state->crtc_w = hdisplay;
	primary_state->crtc_h = vdisplay;
	primary_state->src_x = set->x << 16;
	primary_state->src_y = set->y << 16;
	if (drm_rotation_90_or_270(primary_state->rotation)) {
		primary_state->src_w = vdisplay << 16;
		primary_state->src_h = hdisplay << 16;
	} else {
		primary_state->src_w = hdisplay << 16;
		primary_state->src_h = vdisplay << 16;
	}

commit:
	ret = update_output_state(state, set);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(__drm_atomic_helper_set_config);

static void drm_atomic_private_obj_print_state(struct drm_printer *p,
					       const struct drm_private_state *state)
{
	struct drm_private_obj *obj = state->obj;

	if (obj->funcs->atomic_print_state)
		obj->funcs->atomic_print_state(p, state);
}

/**
 * drm_atomic_print_new_state - prints drm atomic state
 * @state: atomic configuration to check
 * @p: drm printer
 *
 * This functions prints the drm atomic state snapshot using the drm printer
 * which is passed to it. This snapshot can be used for debugging purposes.
 *
 * Note that this function looks into the new state objects and hence its not
 * safe to be used after the call to drm_atomic_helper_commit_hw_done().
 */
void drm_atomic_print_new_state(const struct drm_atomic_state *state,
		struct drm_printer *p)
{
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	struct drm_private_obj *obj;
	struct drm_private_state *obj_state;
	int i;

	if (!p) {
		drm_err(state->dev, "invalid drm printer\n");
		return;
	}

	drm_dbg_atomic(state->dev, "checking %p\n", state);

	for_each_new_plane_in_state(state, plane, plane_state, i)
		drm_atomic_plane_print_state(p, plane_state);

	for_each_new_crtc_in_state(state, crtc, crtc_state, i)
		drm_atomic_crtc_print_state(p, crtc_state);

	for_each_new_connector_in_state(state, connector, connector_state, i)
		drm_atomic_connector_print_state(p, connector_state);

	for_each_new_private_obj_in_state(state, obj, obj_state, i)
		drm_atomic_private_obj_print_state(p, obj_state);
}
EXPORT_SYMBOL(drm_atomic_print_new_state);

static void __drm_state_dump(struct drm_device *dev, struct drm_printer *p,
			     bool take_locks)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	if (!drm_drv_uses_atomic_modeset(dev))
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
 * The caller must wrap this drm_modeset_lock_all_ctx() and
 * drm_modeset_drop_locks(). If this is called from error irq handler, it should
 * not be enabled by default - if you are debugging errors you might
 * not care that this is racey, but calling this without all modeset locks held
 * is inherently unsafe.
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

void drm_atomic_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(drm_atomic_debugfs_list,
				 ARRAY_SIZE(drm_atomic_debugfs_list),
				 minor->debugfs_root, minor);
}
#endif
