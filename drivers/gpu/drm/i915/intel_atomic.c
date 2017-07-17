/*
 * Copyright © 2015 Intel Corporation
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
 * DOC: atomic modeset support
 *
 * The functions here implement the state management and hardware programming
 * dispatch required by the atomic modeset infrastructure.
 * See intel_atomic_plane.c for the plane-specific atomic functionality.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include "intel_drv.h"

/**
 * intel_digital_connector_atomic_get_property - hook for connector->atomic_get_property.
 * @connector: Connector to get the property for.
 * @state: Connector state to retrieve the property from.
 * @property: Property to retrieve.
 * @val: Return value for the property.
 *
 * Returns the atomic property value for a digital connector.
 */
int intel_digital_connector_atomic_get_property(struct drm_connector *connector,
						const struct drm_connector_state *state,
						struct drm_property *property,
						uint64_t *val)
{
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_digital_connector_state *intel_conn_state =
		to_intel_digital_connector_state(state);

	if (property == dev_priv->force_audio_property)
		*val = intel_conn_state->force_audio;
	else if (property == dev_priv->broadcast_rgb_property)
		*val = intel_conn_state->broadcast_rgb;
	else {
		DRM_DEBUG_ATOMIC("Unknown property %s\n", property->name);
		return -EINVAL;
	}

	return 0;
}

/**
 * intel_digital_connector_atomic_set_property - hook for connector->atomic_set_property.
 * @connector: Connector to set the property for.
 * @state: Connector state to set the property on.
 * @property: Property to set.
 * @val: New value for the property.
 *
 * Sets the atomic property value for a digital connector.
 */
int intel_digital_connector_atomic_set_property(struct drm_connector *connector,
						struct drm_connector_state *state,
						struct drm_property *property,
						uint64_t val)
{
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_digital_connector_state *intel_conn_state =
		to_intel_digital_connector_state(state);

	if (property == dev_priv->force_audio_property) {
		intel_conn_state->force_audio = val;
		return 0;
	}

	if (property == dev_priv->broadcast_rgb_property) {
		intel_conn_state->broadcast_rgb = val;
		return 0;
	}

	DRM_DEBUG_ATOMIC("Unknown property %s\n", property->name);
	return -EINVAL;
}

int intel_digital_connector_atomic_check(struct drm_connector *conn,
					 struct drm_connector_state *new_state)
{
	struct intel_digital_connector_state *new_conn_state =
		to_intel_digital_connector_state(new_state);
	struct drm_connector_state *old_state =
		drm_atomic_get_old_connector_state(new_state->state, conn);
	struct intel_digital_connector_state *old_conn_state =
		to_intel_digital_connector_state(old_state);
	struct drm_crtc_state *crtc_state;

	if (!new_state->crtc)
		return 0;

	crtc_state = drm_atomic_get_new_crtc_state(new_state->state, new_state->crtc);

	/*
	 * These properties are handled by fastset, and might not end
	 * up in a modeset.
	 */
	if (new_conn_state->force_audio != old_conn_state->force_audio ||
	    new_conn_state->broadcast_rgb != old_conn_state->broadcast_rgb ||
	    new_conn_state->base.picture_aspect_ratio != old_conn_state->base.picture_aspect_ratio ||
	    new_conn_state->base.scaling_mode != old_conn_state->base.scaling_mode)
		crtc_state->mode_changed = true;

	return 0;
}

/**
 * intel_digital_connector_duplicate_state - duplicate connector state
 * @connector: digital connector
 *
 * Allocates and returns a copy of the connector state (both common and
 * digital connector specific) for the specified connector.
 *
 * Returns: The newly allocated connector state, or NULL on failure.
 */
struct drm_connector_state *
intel_digital_connector_duplicate_state(struct drm_connector *connector)
{
	struct intel_digital_connector_state *state;

	state = kmemdup(connector->state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_connector_duplicate_state(connector, &state->base);
	return &state->base;
}

/**
 * intel_crtc_duplicate_state - duplicate crtc state
 * @crtc: drm crtc
 *
 * Allocates and returns a copy of the crtc state (both common and
 * Intel-specific) for the specified crtc.
 *
 * Returns: The newly allocated crtc state, or NULL on failure.
 */
struct drm_crtc_state *
intel_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct intel_crtc_state *crtc_state;

	crtc_state = kmemdup(crtc->state, sizeof(*crtc_state), GFP_KERNEL);
	if (!crtc_state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &crtc_state->base);

	crtc_state->update_pipe = false;
	crtc_state->disable_lp_wm = false;
	crtc_state->disable_cxsr = false;
	crtc_state->update_wm_pre = false;
	crtc_state->update_wm_post = false;
	crtc_state->fb_changed = false;
	crtc_state->fifo_changed = false;
	crtc_state->wm.need_postvbl_update = false;
	crtc_state->fb_bits = 0;

	return &crtc_state->base;
}

/**
 * intel_crtc_destroy_state - destroy crtc state
 * @crtc: drm crtc
 *
 * Destroys the crtc state (both common and Intel-specific) for the
 * specified crtc.
 */
void
intel_crtc_destroy_state(struct drm_crtc *crtc,
			  struct drm_crtc_state *state)
{
	drm_atomic_helper_crtc_destroy_state(crtc, state);
}

/**
 * intel_atomic_setup_scalers() - setup scalers for crtc per staged requests
 * @dev_priv: i915 device
 * @crtc: intel crtc
 * @crtc_state: incoming crtc_state to validate and setup scalers
 *
 * This function sets up scalers based on staged scaling requests for
 * a @crtc and its planes. It is called from crtc level check path. If request
 * is a supportable request, it attaches scalers to requested planes and crtc.
 *
 * This function takes into account the current scaler(s) in use by any planes
 * not being part of this atomic state
 *
 *  Returns:
 *         0 - scalers were setup succesfully
 *         error code - otherwise
 */
int intel_atomic_setup_scalers(struct drm_i915_private *dev_priv,
			       struct intel_crtc *intel_crtc,
			       struct intel_crtc_state *crtc_state)
{
	struct drm_plane *plane = NULL;
	struct intel_plane *intel_plane;
	struct intel_plane_state *plane_state = NULL;
	struct intel_crtc_scaler_state *scaler_state =
		&crtc_state->scaler_state;
	struct drm_atomic_state *drm_state = crtc_state->base.state;
	int num_scalers_need;
	int i, j;

	num_scalers_need = hweight32(scaler_state->scaler_users);

	/*
	 * High level flow:
	 * - staged scaler requests are already in scaler_state->scaler_users
	 * - check whether staged scaling requests can be supported
	 * - add planes using scalers that aren't in current transaction
	 * - assign scalers to requested users
	 * - as part of plane commit, scalers will be committed
	 *   (i.e., either attached or detached) to respective planes in hw
	 * - as part of crtc_commit, scaler will be either attached or detached
	 *   to crtc in hw
	 */

	/* fail if required scalers > available scalers */
	if (num_scalers_need > intel_crtc->num_scalers){
		DRM_DEBUG_KMS("Too many scaling requests %d > %d\n",
			num_scalers_need, intel_crtc->num_scalers);
		return -EINVAL;
	}

	/* walkthrough scaler_users bits and start assigning scalers */
	for (i = 0; i < sizeof(scaler_state->scaler_users) * 8; i++) {
		int *scaler_id;
		const char *name;
		int idx;

		/* skip if scaler not required */
		if (!(scaler_state->scaler_users & (1 << i)))
			continue;

		if (i == SKL_CRTC_INDEX) {
			name = "CRTC";
			idx = intel_crtc->base.base.id;

			/* panel fitter case: assign as a crtc scaler */
			scaler_id = &scaler_state->scaler_id;
		} else {
			name = "PLANE";

			/* plane scaler case: assign as a plane scaler */
			/* find the plane that set the bit as scaler_user */
			plane = drm_state->planes[i].ptr;

			/*
			 * to enable/disable hq mode, add planes that are using scaler
			 * into this transaction
			 */
			if (!plane) {
				struct drm_plane_state *state;
				plane = drm_plane_from_index(&dev_priv->drm, i);
				state = drm_atomic_get_plane_state(drm_state, plane);
				if (IS_ERR(state)) {
					DRM_DEBUG_KMS("Failed to add [PLANE:%d] to drm_state\n",
						plane->base.id);
					return PTR_ERR(state);
				}

				/*
				 * the plane is added after plane checks are run,
				 * but since this plane is unchanged just do the
				 * minimum required validation.
				 */
				crtc_state->base.planes_changed = true;
			}

			intel_plane = to_intel_plane(plane);
			idx = plane->base.id;

			/* plane on different crtc cannot be a scaler user of this crtc */
			if (WARN_ON(intel_plane->pipe != intel_crtc->pipe)) {
				continue;
			}

			plane_state = intel_atomic_get_existing_plane_state(drm_state,
									    intel_plane);
			scaler_id = &plane_state->scaler_id;
		}

		if (*scaler_id < 0) {
			/* find a free scaler */
			for (j = 0; j < intel_crtc->num_scalers; j++) {
				if (!scaler_state->scalers[j].in_use) {
					scaler_state->scalers[j].in_use = 1;
					*scaler_id = j;
					DRM_DEBUG_KMS("Attached scaler id %u.%u to %s:%d\n",
						intel_crtc->pipe, *scaler_id, name, idx);
					break;
				}
			}
		}

		if (WARN_ON(*scaler_id < 0)) {
			DRM_DEBUG_KMS("Cannot find scaler for %s:%d\n", name, idx);
			continue;
		}

		/* set scaler mode */
		if (IS_GEMINILAKE(dev_priv) || IS_CANNONLAKE(dev_priv)) {
			scaler_state->scalers[*scaler_id].mode = 0;
		} else if (num_scalers_need == 1 && intel_crtc->pipe != PIPE_C) {
			/*
			 * when only 1 scaler is in use on either pipe A or B,
			 * scaler 0 operates in high quality (HQ) mode.
			 * In this case use scaler 0 to take advantage of HQ mode
			 */
			*scaler_id = 0;
			scaler_state->scalers[0].in_use = 1;
			scaler_state->scalers[0].mode = PS_SCALER_MODE_HQ;
			scaler_state->scalers[1].in_use = 0;
		} else {
			scaler_state->scalers[*scaler_id].mode = PS_SCALER_MODE_DYN;
		}
	}

	return 0;
}

struct drm_atomic_state *
intel_atomic_state_alloc(struct drm_device *dev)
{
	struct intel_atomic_state *state = kzalloc(sizeof(*state), GFP_KERNEL);

	if (!state || drm_atomic_state_init(dev, &state->base) < 0) {
		kfree(state);
		return NULL;
	}

	return &state->base;
}

void intel_atomic_state_clear(struct drm_atomic_state *s)
{
	struct intel_atomic_state *state = to_intel_atomic_state(s);
	drm_atomic_state_default_clear(&state->base);
	state->dpll_set = state->modeset = false;
}
