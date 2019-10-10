/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2014 Intel Corp.
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

#include <drm/drm_atomic_uapi.h>
#include <drm/drm_atomic.h>
#include <drm/drm_print.h>
#include <drm/drm_drv.h>
#include <drm/drm_writeback.h>
#include <drm/drm_vblank.h>

#include <linux/dma-fence.h>
#include <linux/uaccess.h>
#include <linux/sync_file.h>
#include <linux/file.h>

#include "drm_crtc_internal.h"

/**
 * DOC: overview
 *
 * This file contains the marshalling and demarshalling glue for the atomic UAPI
 * in all its forms: The monster ATOMIC IOCTL itself, code for GET_PROPERTY and
 * SET_PROPERTY IOCTLs. Plus interface functions for compatibility helpers and
 * drivers which have special needs to construct their own atomic updates, e.g.
 * for load detect or similiar.
 */

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
	struct drm_crtc *crtc = state->crtc;
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
		DRM_DEBUG_ATOMIC("Set [MODE:%s] for [CRTC:%d:%s] state %p\n",
				 mode->name, crtc->base.id, crtc->name, state);
	} else {
		memset(&state->mode, 0, sizeof(state->mode));
		state->enable = false;
		DRM_DEBUG_ATOMIC("Set [NOMODE] for [CRTC:%d:%s] state %p\n",
				 crtc->base.id, crtc->name, state);
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
	struct drm_crtc *crtc = state->crtc;

	if (blob == state->mode_blob)
		return 0;

	drm_property_blob_put(state->mode_blob);
	state->mode_blob = NULL;

	memset(&state->mode, 0, sizeof(state->mode));

	if (blob) {
		int ret;

		if (blob->length != sizeof(struct drm_mode_modeinfo)) {
			DRM_DEBUG_ATOMIC("[CRTC:%d:%s] bad mode blob length: %zu\n",
					 crtc->base.id, crtc->name,
					 blob->length);
			return -EINVAL;
		}

		ret = drm_mode_convert_umode(crtc->dev,
					     &state->mode, blob->data);
		if (ret) {
			DRM_DEBUG_ATOMIC("[CRTC:%d:%s] invalid mode (ret=%d, status=%s):\n",
					 crtc->base.id, crtc->name,
					 ret, drm_get_mode_status_name(state->mode.status));
			drm_mode_debug_printmodeline(&state->mode);
			return -EINVAL;
		}

		state->mode_blob = drm_property_blob_get(blob);
		state->enable = true;
		DRM_DEBUG_ATOMIC("Set [MODE:%s] for [CRTC:%d:%s] state %p\n",
				 state->mode.name, crtc->base.id, crtc->name,
				 state);
	} else {
		state->enable = false;
		DRM_DEBUG_ATOMIC("Set [NOMODE] for [CRTC:%d:%s] state %p\n",
				 crtc->base.id, crtc->name, state);
	}

	return 0;
}
EXPORT_SYMBOL(drm_atomic_set_mode_prop_for_crtc);

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
	/* Nothing to do for same crtc*/
	if (plane_state->crtc == crtc)
		return 0;
	if (plane_state->crtc) {
		crtc_state = drm_atomic_get_crtc_state(plane_state->state,
						       plane_state->crtc);
		if (WARN_ON(IS_ERR(crtc_state)))
			return PTR_ERR(crtc_state);

		crtc_state->plane_mask &= ~drm_plane_mask(plane);
	}

	plane_state->crtc = crtc;

	if (crtc) {
		crtc_state = drm_atomic_get_crtc_state(plane_state->state,
						       crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);
		crtc_state->plane_mask |= drm_plane_mask(plane);
	}

	if (crtc)
		DRM_DEBUG_ATOMIC("Link [PLANE:%d:%s] state %p to [CRTC:%d:%s]\n",
				 plane->base.id, plane->name, plane_state,
				 crtc->base.id, crtc->name);
	else
		DRM_DEBUG_ATOMIC("Link [PLANE:%d:%s] state %p to [NOCRTC]\n",
				 plane->base.id, plane->name, plane_state);

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
	struct drm_plane *plane = plane_state->plane;

	if (fb)
		DRM_DEBUG_ATOMIC("Set [FB:%d] for [PLANE:%d:%s] state %p\n",
				 fb->base.id, plane->base.id, plane->name,
				 plane_state);
	else
		DRM_DEBUG_ATOMIC("Set [NOFB] for [PLANE:%d:%s] state %p\n",
				 plane->base.id, plane->name, plane_state);

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
 *
 * This way explicit fencing can be used to overrule implicit fencing, which is
 * important to make explicit fencing use-cases work: One example is using one
 * buffer for 2 screens with different refresh rates. Implicit fencing will
 * clamp rendering to the refresh rate of the slower screen, whereas explicit
 * fence allows 2 independent render and display loops on a single buffer. If a
 * driver allows obeys both implicit and explicit fences for plane updates, then
 * it will break all the benefits of explicit fencing.
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
	struct drm_connector *connector = conn_state->connector;
	struct drm_crtc_state *crtc_state;

	if (conn_state->crtc == crtc)
		return 0;

	if (conn_state->crtc) {
		crtc_state = drm_atomic_get_new_crtc_state(conn_state->state,
							   conn_state->crtc);

		crtc_state->connector_mask &=
			~drm_connector_mask(conn_state->connector);

		drm_connector_put(conn_state->connector);
		conn_state->crtc = NULL;
	}

	if (crtc) {
		crtc_state = drm_atomic_get_crtc_state(conn_state->state, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		crtc_state->connector_mask |=
			drm_connector_mask(conn_state->connector);

		drm_connector_get(conn_state->connector);
		conn_state->crtc = crtc;

		DRM_DEBUG_ATOMIC("Link [CONNECTOR:%d:%s] state %p to [CRTC:%d:%s]\n",
				 connector->base.id, connector->name,
				 conn_state, crtc->base.id, crtc->name);
	} else {
		DRM_DEBUG_ATOMIC("Link [CONNECTOR:%d:%s] state %p to [NOCRTC]\n",
				 connector->base.id, connector->name,
				 conn_state);
	}

	return 0;
}
EXPORT_SYMBOL(drm_atomic_set_crtc_for_connector);

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

static int set_out_fence_for_connector(struct drm_atomic_state *state,
					struct drm_connector *connector,
					s32 __user *fence_ptr)
{
	unsigned int index = drm_connector_index(connector);

	if (!fence_ptr)
		return 0;

	if (put_user(-1, fence_ptr))
		return -EFAULT;

	state->connectors[index].out_fence_ptr = fence_ptr;

	return 0;
}

static s32 __user *get_out_fence_for_connector(struct drm_atomic_state *state,
					       struct drm_connector *connector)
{
	unsigned int index = drm_connector_index(connector);
	s32 __user *fence_ptr;

	fence_ptr = state->connectors[index].out_fence_ptr;
	state->connectors[index].out_fence_ptr = NULL;

	return fence_ptr;
}

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

static int drm_atomic_crtc_set_property(struct drm_crtc *crtc,
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
	} else if (property == config->prop_vrr_enabled) {
		state->vrr_enabled = val;
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
	} else if (crtc->funcs->atomic_set_property) {
		return crtc->funcs->atomic_set_property(crtc, state, property, val);
	} else {
		DRM_DEBUG_ATOMIC("[CRTC:%d:%s] unknown property [PROP:%d:%s]]\n",
				 crtc->base.id, crtc->name,
				 property->base.id, property->name);
		return -EINVAL;
	}

	return 0;
}

static int
drm_atomic_crtc_get_property(struct drm_crtc *crtc,
		const struct drm_crtc_state *state,
		struct drm_property *property, uint64_t *val)
{
	struct drm_device *dev = crtc->dev;
	struct drm_mode_config *config = &dev->mode_config;

	if (property == config->prop_active)
		*val = drm_atomic_crtc_effectively_active(state);
	else if (property == config->prop_mode_id)
		*val = (state->mode_blob) ? state->mode_blob->base.id : 0;
	else if (property == config->prop_vrr_enabled)
		*val = state->vrr_enabled;
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

static int drm_atomic_plane_set_property(struct drm_plane *plane,
		struct drm_plane_state *state, struct drm_file *file_priv,
		struct drm_property *property, uint64_t val)
{
	struct drm_device *dev = plane->dev;
	struct drm_mode_config *config = &dev->mode_config;
	bool replaced = false;
	int ret;

	if (property == config->prop_fb_id) {
		struct drm_framebuffer *fb;
		fb = drm_framebuffer_lookup(dev, file_priv, val);
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
		struct drm_crtc *crtc = drm_crtc_find(dev, file_priv, val);
		if (val && !crtc)
			return -EACCES;
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
	} else if (property == plane->alpha_property) {
		state->alpha = val;
	} else if (property == plane->blend_mode_property) {
		state->pixel_blend_mode = val;
	} else if (property == plane->rotation_property) {
		if (!is_power_of_2(val & DRM_MODE_ROTATE_MASK)) {
			DRM_DEBUG_ATOMIC("[PLANE:%d:%s] bad rotation bitmask: 0x%llx\n",
					 plane->base.id, plane->name, val);
			return -EINVAL;
		}
		state->rotation = val;
	} else if (property == plane->zpos_property) {
		state->zpos = val;
	} else if (property == plane->color_encoding_property) {
		state->color_encoding = val;
	} else if (property == plane->color_range_property) {
		state->color_range = val;
	} else if (property == config->prop_fb_damage_clips) {
		ret = drm_atomic_replace_property_blob_from_id(dev,
					&state->fb_damage_clips,
					val,
					-1,
					sizeof(struct drm_rect),
					&replaced);
		return ret;
	} else if (plane->funcs->atomic_set_property) {
		return plane->funcs->atomic_set_property(plane, state,
				property, val);
	} else {
		DRM_DEBUG_ATOMIC("[PLANE:%d:%s] unknown property [PROP:%d:%s]]\n",
				 plane->base.id, plane->name,
				 property->base.id, property->name);
		return -EINVAL;
	}

	return 0;
}

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
	} else if (property == plane->alpha_property) {
		*val = state->alpha;
	} else if (property == plane->blend_mode_property) {
		*val = state->pixel_blend_mode;
	} else if (property == plane->rotation_property) {
		*val = state->rotation;
	} else if (property == plane->zpos_property) {
		*val = state->zpos;
	} else if (property == plane->color_encoding_property) {
		*val = state->color_encoding;
	} else if (property == plane->color_range_property) {
		*val = state->color_range;
	} else if (property == config->prop_fb_damage_clips) {
		*val = (state->fb_damage_clips) ?
			state->fb_damage_clips->base.id : 0;
	} else if (plane->funcs->atomic_get_property) {
		return plane->funcs->atomic_get_property(plane, state, property, val);
	} else {
		return -EINVAL;
	}

	return 0;
}

static int drm_atomic_set_writeback_fb_for_connector(
		struct drm_connector_state *conn_state,
		struct drm_framebuffer *fb)
{
	int ret;

	ret = drm_writeback_set_fb(conn_state, fb);
	if (ret < 0)
		return ret;

	if (fb)
		DRM_DEBUG_ATOMIC("Set [FB:%d] for connector state %p\n",
				 fb->base.id, conn_state);
	else
		DRM_DEBUG_ATOMIC("Set [NOFB] for connector state %p\n",
				 conn_state);

	return 0;
}

static int drm_atomic_connector_set_property(struct drm_connector *connector,
		struct drm_connector_state *state, struct drm_file *file_priv,
		struct drm_property *property, uint64_t val)
{
	struct drm_device *dev = connector->dev;
	struct drm_mode_config *config = &dev->mode_config;
	bool replaced = false;
	int ret;

	if (property == config->prop_crtc_id) {
		struct drm_crtc *crtc = drm_crtc_find(dev, file_priv, val);
		if (val && !crtc)
			return -EACCES;
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
	} else if (property == config->hdr_output_metadata_property) {
		ret = drm_atomic_replace_property_blob_from_id(dev,
				&state->hdr_output_metadata,
				val,
				sizeof(struct hdr_output_metadata), -1,
				&replaced);
		return ret;
	} else if (property == config->aspect_ratio_property) {
		state->picture_aspect_ratio = val;
	} else if (property == config->content_type_property) {
		state->content_type = val;
	} else if (property == connector->scaling_mode_property) {
		state->scaling_mode = val;
	} else if (property == config->content_protection_property) {
		if (val == DRM_MODE_CONTENT_PROTECTION_ENABLED) {
			DRM_DEBUG_KMS("only drivers can set CP Enabled\n");
			return -EINVAL;
		}
		state->content_protection = val;
	} else if (property == config->hdcp_content_type_property) {
		state->hdcp_content_type = val;
	} else if (property == connector->colorspace_property) {
		state->colorspace = val;
	} else if (property == config->writeback_fb_id_property) {
		struct drm_framebuffer *fb;
		int ret;
		fb = drm_framebuffer_lookup(dev, file_priv, val);
		ret = drm_atomic_set_writeback_fb_for_connector(state, fb);
		if (fb)
			drm_framebuffer_put(fb);
		return ret;
	} else if (property == config->writeback_out_fence_ptr_property) {
		s32 __user *fence_ptr = u64_to_user_ptr(val);

		return set_out_fence_for_connector(state->state, connector,
						   fence_ptr);
	} else if (property == connector->max_bpc_property) {
		state->max_requested_bpc = val;
	} else if (connector->funcs->atomic_set_property) {
		return connector->funcs->atomic_set_property(connector,
				state, property, val);
	} else {
		DRM_DEBUG_ATOMIC("[CONNECTOR:%d:%s] unknown property [PROP:%d:%s]]\n",
				 connector->base.id, connector->name,
				 property->base.id, property->name);
		return -EINVAL;
	}

	return 0;
}

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
		if (state->crtc && state->crtc->state->self_refresh_active)
			*val = DRM_MODE_DPMS_ON;
		else
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
	} else if (property == config->content_type_property) {
		*val = state->content_type;
	} else if (property == connector->colorspace_property) {
		*val = state->colorspace;
	} else if (property == connector->scaling_mode_property) {
		*val = state->scaling_mode;
	} else if (property == config->hdr_output_metadata_property) {
		*val = state->hdr_output_metadata ?
			state->hdr_output_metadata->base.id : 0;
	} else if (property == config->content_protection_property) {
		*val = state->content_protection;
	} else if (property == config->hdcp_content_type_property) {
		*val = state->hdcp_content_type;
	} else if (property == config->writeback_fb_id_property) {
		/* Writeback framebuffer is one-shot, write and forget */
		*val = 0;
	} else if (property == config->writeback_out_fence_ptr_property) {
		*val = 0;
	} else if (property == connector->max_bpc_property) {
		*val = state->max_requested_bpc;
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
			    struct drm_file *file_priv,
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
				connector_state, file_priv,
				prop, prop_value);
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
				plane_state, file_priv,
				prop, prop_value);
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
 * a &struct dma_buf this is tracked in &struct dma_resv.
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

static int prepare_signaling(struct drm_device *dev,
				  struct drm_atomic_state *state,
				  struct drm_mode_atomic *arg,
				  struct drm_file *file_priv,
				  struct drm_out_fence_state **fence_state,
				  unsigned int *num_fences)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_connector *conn;
	struct drm_connector_state *conn_state;
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

	for_each_new_connector_in_state(state, conn, conn_state, i) {
		struct drm_writeback_connector *wb_conn;
		struct drm_out_fence_state *f;
		struct dma_fence *fence;
		s32 __user *fence_ptr;

		if (!conn_state->writeback_job)
			continue;

		fence_ptr = get_out_fence_for_connector(state, conn);
		if (!fence_ptr)
			continue;

		f = krealloc(*fence_state, sizeof(**fence_state) *
			     (*num_fences + 1), GFP_KERNEL);
		if (!f)
			return -ENOMEM;

		memset(&f[*num_fences], 0, sizeof(*f));

		f[*num_fences].out_fence_ptr = fence_ptr;
		*fence_state = f;

		wb_conn = drm_connector_to_writeback(conn);
		fence = drm_writeback_get_out_fence(wb_conn);
		if (!fence)
			return -ENOMEM;

		ret = setup_out_fence(&f[(*num_fences)++], fence);
		if (ret) {
			dma_fence_put(fence);
			return ret;
		}

		conn_state->writeback_job->out_fence = fence;
	}

	/*
	 * Having this flag means user mode pends on event which will never
	 * reach due to lack of at least one CRTC for signaling
	 */
	if (c == 0 && (arg->flags & DRM_MODE_PAGE_FLIP_EVENT))
		return -EINVAL;

	return 0;
}

static void complete_signaling(struct drm_device *dev,
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
	struct drm_out_fence_state *fence_state;
	int ret = 0;
	unsigned int i, j, num_fences;

	/* disallow for drivers not supporting atomic: */
	if (!drm_core_check_feature(dev, DRIVER_ATOMIC))
		return -EOPNOTSUPP;

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

	if (arg->flags & DRM_MODE_PAGE_FLIP_ASYNC)
		return -EINVAL;

	/* can't test and expect an event at the same time. */
	if ((arg->flags & DRM_MODE_ATOMIC_TEST_ONLY) &&
			(arg->flags & DRM_MODE_PAGE_FLIP_EVENT))
		return -EINVAL;

	state = drm_atomic_state_alloc(dev);
	if (!state)
		return -ENOMEM;

	drm_modeset_acquire_init(&ctx, DRM_MODESET_ACQUIRE_INTERRUPTIBLE);
	state->acquire_ctx = &ctx;
	state->allow_modeset = !!(arg->flags & DRM_MODE_ATOMIC_ALLOW_MODESET);

retry:
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

			ret = drm_atomic_set_property(state, file_priv,
						      obj, prop, prop_value);
			if (ret) {
				drm_mode_object_put(obj);
				goto out;
			}

			copied_props++;
		}

		drm_mode_object_put(obj);
	}

	ret = prepare_signaling(dev, state, arg, file_priv, &fence_state,
				&num_fences);
	if (ret)
		goto out;

	if (arg->flags & DRM_MODE_ATOMIC_TEST_ONLY) {
		ret = drm_atomic_check_only(state);
	} else if (arg->flags & DRM_MODE_ATOMIC_NONBLOCK) {
		ret = drm_atomic_nonblocking_commit(state);
	} else {
		if (drm_debug_enabled(DRM_UT_STATE))
			drm_atomic_print_state(state);

		ret = drm_atomic_commit(state);
	}

out:
	complete_signaling(dev, state, fence_state, num_fences, !ret);

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
