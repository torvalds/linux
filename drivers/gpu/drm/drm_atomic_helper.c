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
#include <drm/drm_atomic_helper.h>
#include <linux/dma-fence.h>

#include "drm_crtc_helper_internal.h"
#include "drm_crtc_internal.h"

/**
 * DOC: overview
 *
 * This helper library provides implementations of check and commit functions on
 * top of the CRTC modeset helper callbacks and the plane helper callbacks. It
 * also provides convenience implementations for the atomic state handling
 * callbacks for drivers which don't need to subclass the drm core structures to
 * add their own additional internal state.
 *
 * This library also provides default implementations for the check callback in
 * drm_atomic_helper_check() and for the commit callback with
 * drm_atomic_helper_commit(). But the individual stages and callbacks are
 * exposed to allow drivers to mix and match and e.g. use the plane helpers only
 * together with a driver private modeset implementation.
 *
 * This library also provides implementations for all the legacy driver
 * interfaces on top of the atomic interface. See drm_atomic_helper_set_config(),
 * drm_atomic_helper_disable_plane(), drm_atomic_helper_disable_plane() and the
 * various functions to implement set_property callbacks. New drivers must not
 * implement these functions themselves but must use the provided helpers.
 *
 * The atomic helper uses the same function table structures as all other
 * modesetting helpers. See the documentation for &struct drm_crtc_helper_funcs,
 * struct &drm_encoder_helper_funcs and &struct drm_connector_helper_funcs. It
 * also shares the &struct drm_plane_helper_funcs function table with the plane
 * helpers.
 */
static void
drm_atomic_helper_plane_changed(struct drm_atomic_state *state,
				struct drm_plane_state *old_plane_state,
				struct drm_plane_state *plane_state,
				struct drm_plane *plane)
{
	struct drm_crtc_state *crtc_state;

	if (old_plane_state->crtc) {
		crtc_state = drm_atomic_get_new_crtc_state(state,
							   old_plane_state->crtc);

		if (WARN_ON(!crtc_state))
			return;

		crtc_state->planes_changed = true;
	}

	if (plane_state->crtc) {
		crtc_state = drm_atomic_get_new_crtc_state(state, plane_state->crtc);

		if (WARN_ON(!crtc_state))
			return;

		crtc_state->planes_changed = true;
	}
}

static int handle_conflicting_encoders(struct drm_atomic_state *state,
				       bool disable_conflicting_encoders)
{
	struct drm_connector_state *new_conn_state;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct drm_encoder *encoder;
	unsigned encoder_mask = 0;
	int i, ret = 0;

	/*
	 * First loop, find all newly assigned encoders from the connectors
	 * part of the state. If the same encoder is assigned to multiple
	 * connectors bail out.
	 */
	for_each_new_connector_in_state(state, connector, new_conn_state, i) {
		const struct drm_connector_helper_funcs *funcs = connector->helper_private;
		struct drm_encoder *new_encoder;

		if (!new_conn_state->crtc)
			continue;

		if (funcs->atomic_best_encoder)
			new_encoder = funcs->atomic_best_encoder(connector, new_conn_state);
		else if (funcs->best_encoder)
			new_encoder = funcs->best_encoder(connector);
		else
			new_encoder = drm_atomic_helper_best_encoder(connector);

		if (new_encoder) {
			if (encoder_mask & (1 << drm_encoder_index(new_encoder))) {
				DRM_DEBUG_ATOMIC("[ENCODER:%d:%s] on [CONNECTOR:%d:%s] already assigned\n",
					new_encoder->base.id, new_encoder->name,
					connector->base.id, connector->name);

				return -EINVAL;
			}

			encoder_mask |= 1 << drm_encoder_index(new_encoder);
		}
	}

	if (!encoder_mask)
		return 0;

	/*
	 * Second loop, iterate over all connectors not part of the state.
	 *
	 * If a conflicting encoder is found and disable_conflicting_encoders
	 * is not set, an error is returned. Userspace can provide a solution
	 * through the atomic ioctl.
	 *
	 * If the flag is set conflicting connectors are removed from the crtc
	 * and the crtc is disabled if no encoder is left. This preserves
	 * compatibility with the legacy set_config behavior.
	 */
	drm_connector_list_iter_begin(state->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct drm_crtc_state *crtc_state;

		if (drm_atomic_get_new_connector_state(state, connector))
			continue;

		encoder = connector->state->best_encoder;
		if (!encoder || !(encoder_mask & (1 << drm_encoder_index(encoder))))
			continue;

		if (!disable_conflicting_encoders) {
			DRM_DEBUG_ATOMIC("[ENCODER:%d:%s] in use on [CRTC:%d:%s] by [CONNECTOR:%d:%s]\n",
					 encoder->base.id, encoder->name,
					 connector->state->crtc->base.id,
					 connector->state->crtc->name,
					 connector->base.id, connector->name);
			ret = -EINVAL;
			goto out;
		}

		new_conn_state = drm_atomic_get_connector_state(state, connector);
		if (IS_ERR(new_conn_state)) {
			ret = PTR_ERR(new_conn_state);
			goto out;
		}

		DRM_DEBUG_ATOMIC("[ENCODER:%d:%s] in use on [CRTC:%d:%s], disabling [CONNECTOR:%d:%s]\n",
				 encoder->base.id, encoder->name,
				 new_conn_state->crtc->base.id, new_conn_state->crtc->name,
				 connector->base.id, connector->name);

		crtc_state = drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);

		ret = drm_atomic_set_crtc_for_connector(new_conn_state, NULL);
		if (ret)
			goto out;

		if (!crtc_state->connector_mask) {
			ret = drm_atomic_set_mode_prop_for_crtc(crtc_state,
								NULL);
			if (ret < 0)
				goto out;

			crtc_state->active = false;
		}
	}
out:
	drm_connector_list_iter_end(&conn_iter);

	return ret;
}

static void
set_best_encoder(struct drm_atomic_state *state,
		 struct drm_connector_state *conn_state,
		 struct drm_encoder *encoder)
{
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;

	if (conn_state->best_encoder) {
		/* Unset the encoder_mask in the old crtc state. */
		crtc = conn_state->connector->state->crtc;

		/* A NULL crtc is an error here because we should have
		 *  duplicated a NULL best_encoder when crtc was NULL.
		 * As an exception restoring duplicated atomic state
		 * during resume is allowed, so don't warn when
		 * best_encoder is equal to encoder we intend to set.
		 */
		WARN_ON(!crtc && encoder != conn_state->best_encoder);
		if (crtc) {
			crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

			crtc_state->encoder_mask &=
				~(1 << drm_encoder_index(conn_state->best_encoder));
		}
	}

	if (encoder) {
		crtc = conn_state->crtc;
		WARN_ON(!crtc);
		if (crtc) {
			crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

			crtc_state->encoder_mask |=
				1 << drm_encoder_index(encoder);
		}
	}

	conn_state->best_encoder = encoder;
}

static void
steal_encoder(struct drm_atomic_state *state,
	      struct drm_encoder *encoder)
{
	struct drm_crtc_state *crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *old_connector_state, *new_connector_state;
	int i;

	for_each_oldnew_connector_in_state(state, connector, old_connector_state, new_connector_state, i) {
		struct drm_crtc *encoder_crtc;

		if (new_connector_state->best_encoder != encoder)
			continue;

		encoder_crtc = old_connector_state->crtc;

		DRM_DEBUG_ATOMIC("[ENCODER:%d:%s] in use on [CRTC:%d:%s], stealing it\n",
				 encoder->base.id, encoder->name,
				 encoder_crtc->base.id, encoder_crtc->name);

		set_best_encoder(state, new_connector_state, NULL);

		crtc_state = drm_atomic_get_new_crtc_state(state, encoder_crtc);
		crtc_state->connectors_changed = true;

		return;
	}
}

static int
update_connector_routing(struct drm_atomic_state *state,
			 struct drm_connector *connector,
			 struct drm_connector_state *old_connector_state,
			 struct drm_connector_state *new_connector_state)
{
	const struct drm_connector_helper_funcs *funcs;
	struct drm_encoder *new_encoder;
	struct drm_crtc_state *crtc_state;

	DRM_DEBUG_ATOMIC("Updating routing for [CONNECTOR:%d:%s]\n",
			 connector->base.id,
			 connector->name);

	if (old_connector_state->crtc != new_connector_state->crtc) {
		if (old_connector_state->crtc) {
			crtc_state = drm_atomic_get_new_crtc_state(state, old_connector_state->crtc);
			crtc_state->connectors_changed = true;
		}

		if (new_connector_state->crtc) {
			crtc_state = drm_atomic_get_new_crtc_state(state, new_connector_state->crtc);
			crtc_state->connectors_changed = true;
		}
	}

	if (!new_connector_state->crtc) {
		DRM_DEBUG_ATOMIC("Disabling [CONNECTOR:%d:%s]\n",
				connector->base.id,
				connector->name);

		set_best_encoder(state, new_connector_state, NULL);

		return 0;
	}

	funcs = connector->helper_private;

	if (funcs->atomic_best_encoder)
		new_encoder = funcs->atomic_best_encoder(connector,
							 new_connector_state);
	else if (funcs->best_encoder)
		new_encoder = funcs->best_encoder(connector);
	else
		new_encoder = drm_atomic_helper_best_encoder(connector);

	if (!new_encoder) {
		DRM_DEBUG_ATOMIC("No suitable encoder found for [CONNECTOR:%d:%s]\n",
				 connector->base.id,
				 connector->name);
		return -EINVAL;
	}

	if (!drm_encoder_crtc_ok(new_encoder, new_connector_state->crtc)) {
		DRM_DEBUG_ATOMIC("[ENCODER:%d:%s] incompatible with [CRTC:%d:%s]\n",
				 new_encoder->base.id,
				 new_encoder->name,
				 new_connector_state->crtc->base.id,
				 new_connector_state->crtc->name);
		return -EINVAL;
	}

	if (new_encoder == new_connector_state->best_encoder) {
		set_best_encoder(state, new_connector_state, new_encoder);

		DRM_DEBUG_ATOMIC("[CONNECTOR:%d:%s] keeps [ENCODER:%d:%s], now on [CRTC:%d:%s]\n",
				 connector->base.id,
				 connector->name,
				 new_encoder->base.id,
				 new_encoder->name,
				 new_connector_state->crtc->base.id,
				 new_connector_state->crtc->name);

		return 0;
	}

	steal_encoder(state, new_encoder);

	set_best_encoder(state, new_connector_state, new_encoder);

	crtc_state = drm_atomic_get_new_crtc_state(state, new_connector_state->crtc);
	crtc_state->connectors_changed = true;

	DRM_DEBUG_ATOMIC("[CONNECTOR:%d:%s] using [ENCODER:%d:%s] on [CRTC:%d:%s]\n",
			 connector->base.id,
			 connector->name,
			 new_encoder->base.id,
			 new_encoder->name,
			 new_connector_state->crtc->base.id,
			 new_connector_state->crtc->name);

	return 0;
}

static int
mode_fixup(struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *new_conn_state;
	int i;
	int ret;

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (!new_crtc_state->mode_changed &&
		    !new_crtc_state->connectors_changed)
			continue;

		drm_mode_copy(&new_crtc_state->adjusted_mode, &new_crtc_state->mode);
	}

	for_each_new_connector_in_state(state, connector, new_conn_state, i) {
		const struct drm_encoder_helper_funcs *funcs;
		struct drm_encoder *encoder;

		WARN_ON(!!new_conn_state->best_encoder != !!new_conn_state->crtc);

		if (!new_conn_state->crtc || !new_conn_state->best_encoder)
			continue;

		new_crtc_state =
			drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);

		/*
		 * Each encoder has at most one connector (since we always steal
		 * it away), so we won't call ->mode_fixup twice.
		 */
		encoder = new_conn_state->best_encoder;
		funcs = encoder->helper_private;

		ret = drm_bridge_mode_fixup(encoder->bridge, &new_crtc_state->mode,
				&new_crtc_state->adjusted_mode);
		if (!ret) {
			DRM_DEBUG_ATOMIC("Bridge fixup failed\n");
			return -EINVAL;
		}

		if (funcs && funcs->atomic_check) {
			ret = funcs->atomic_check(encoder, new_crtc_state,
						  new_conn_state);
			if (ret) {
				DRM_DEBUG_ATOMIC("[ENCODER:%d:%s] check failed\n",
						 encoder->base.id, encoder->name);
				return ret;
			}
		} else if (funcs && funcs->mode_fixup) {
			ret = funcs->mode_fixup(encoder, &new_crtc_state->mode,
						&new_crtc_state->adjusted_mode);
			if (!ret) {
				DRM_DEBUG_ATOMIC("[ENCODER:%d:%s] fixup failed\n",
						 encoder->base.id, encoder->name);
				return -EINVAL;
			}
		}
	}

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		if (!new_crtc_state->enable)
			continue;

		if (!new_crtc_state->mode_changed &&
		    !new_crtc_state->connectors_changed)
			continue;

		funcs = crtc->helper_private;
		if (!funcs->mode_fixup)
			continue;

		ret = funcs->mode_fixup(crtc, &new_crtc_state->mode,
					&new_crtc_state->adjusted_mode);
		if (!ret) {
			DRM_DEBUG_ATOMIC("[CRTC:%d:%s] fixup failed\n",
					 crtc->base.id, crtc->name);
			return -EINVAL;
		}
	}

	return 0;
}

static enum drm_mode_status mode_valid_path(struct drm_connector *connector,
					    struct drm_encoder *encoder,
					    struct drm_crtc *crtc,
					    struct drm_display_mode *mode)
{
	enum drm_mode_status ret;

	ret = drm_encoder_mode_valid(encoder, mode);
	if (ret != MODE_OK) {
		DRM_DEBUG_ATOMIC("[ENCODER:%d:%s] mode_valid() failed\n",
				encoder->base.id, encoder->name);
		return ret;
	}

	ret = drm_bridge_mode_valid(encoder->bridge, mode);
	if (ret != MODE_OK) {
		DRM_DEBUG_ATOMIC("[BRIDGE] mode_valid() failed\n");
		return ret;
	}

	ret = drm_crtc_mode_valid(crtc, mode);
	if (ret != MODE_OK) {
		DRM_DEBUG_ATOMIC("[CRTC:%d:%s] mode_valid() failed\n",
				crtc->base.id, crtc->name);
		return ret;
	}

	return ret;
}

static int
mode_valid(struct drm_atomic_state *state)
{
	struct drm_connector_state *conn_state;
	struct drm_connector *connector;
	int i;

	for_each_new_connector_in_state(state, connector, conn_state, i) {
		struct drm_encoder *encoder = conn_state->best_encoder;
		struct drm_crtc *crtc = conn_state->crtc;
		struct drm_crtc_state *crtc_state;
		enum drm_mode_status mode_status;
		struct drm_display_mode *mode;

		if (!crtc || !encoder)
			continue;

		crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
		if (!crtc_state)
			continue;
		if (!crtc_state->mode_changed && !crtc_state->connectors_changed)
			continue;

		mode = &crtc_state->mode;

		mode_status = mode_valid_path(connector, encoder, crtc, mode);
		if (mode_status != MODE_OK)
			return -EINVAL;
	}

	return 0;
}

/**
 * drm_atomic_helper_check_modeset - validate state object for modeset changes
 * @dev: DRM device
 * @state: the driver state object
 *
 * Check the state object to see if the requested state is physically possible.
 * This does all the crtc and connector related computations for an atomic
 * update and adds any additional connectors needed for full modesets. It calls
 * the various per-object callbacks in the follow order:
 *
 * 1. &drm_connector_helper_funcs.atomic_best_encoder for determining the new encoder.
 * 2. &drm_connector_helper_funcs.atomic_check to validate the connector state.
 * 3. If it's determined a modeset is needed then all connectors on the affected crtc
 *    crtc are added and &drm_connector_helper_funcs.atomic_check is run on them.
 * 4. &drm_encoder_helper_funcs.mode_valid, &drm_bridge_funcs.mode_valid and
 *    &drm_crtc_helper_funcs.mode_valid are called on the affected components.
 * 5. &drm_bridge_funcs.mode_fixup is called on all encoder bridges.
 * 6. &drm_encoder_helper_funcs.atomic_check is called to validate any encoder state.
 *    This function is only called when the encoder will be part of a configured crtc,
 *    it must not be used for implementing connector property validation.
 *    If this function is NULL, &drm_atomic_encoder_helper_funcs.mode_fixup is called
 *    instead.
 * 7. &drm_crtc_helper_funcs.mode_fixup is called last, to fix up the mode with crtc constraints.
 *
 * &drm_crtc_state.mode_changed is set when the input mode is changed.
 * &drm_crtc_state.connectors_changed is set when a connector is added or
 * removed from the crtc.  &drm_crtc_state.active_changed is set when
 * &drm_crtc_state.active changes, which is used for DPMS.
 * See also: drm_atomic_crtc_needs_modeset()
 *
 * IMPORTANT:
 *
 * Drivers which set &drm_crtc_state.mode_changed (e.g. in their
 * &drm_plane_helper_funcs.atomic_check hooks if a plane update can't be done
 * without a full modeset) _must_ call this function afterwards after that
 * change. It is permitted to call this function multiple times for the same
 * update, e.g. when the &drm_crtc_helper_funcs.atomic_check functions depend
 * upon the adjusted dotclock for fifo space allocation and watermark
 * computation.
 *
 * RETURNS:
 * Zero for success or -errno
 */
int
drm_atomic_helper_check_modeset(struct drm_device *dev,
				struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *old_connector_state, *new_connector_state;
	int i, ret;
	unsigned connectors_mask = 0;

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		bool has_connectors =
			!!new_crtc_state->connector_mask;

		WARN_ON(!drm_modeset_is_locked(&crtc->mutex));

		if (!drm_mode_equal(&old_crtc_state->mode, &new_crtc_state->mode)) {
			DRM_DEBUG_ATOMIC("[CRTC:%d:%s] mode changed\n",
					 crtc->base.id, crtc->name);
			new_crtc_state->mode_changed = true;
		}

		if (old_crtc_state->enable != new_crtc_state->enable) {
			DRM_DEBUG_ATOMIC("[CRTC:%d:%s] enable changed\n",
					 crtc->base.id, crtc->name);

			/*
			 * For clarity this assignment is done here, but
			 * enable == 0 is only true when there are no
			 * connectors and a NULL mode.
			 *
			 * The other way around is true as well. enable != 0
			 * iff connectors are attached and a mode is set.
			 */
			new_crtc_state->mode_changed = true;
			new_crtc_state->connectors_changed = true;
		}

		if (old_crtc_state->active != new_crtc_state->active) {
			DRM_DEBUG_ATOMIC("[CRTC:%d:%s] active changed\n",
					 crtc->base.id, crtc->name);
			new_crtc_state->active_changed = true;
		}

		if (new_crtc_state->enable != has_connectors) {
			DRM_DEBUG_ATOMIC("[CRTC:%d:%s] enabled/connectors mismatch\n",
					 crtc->base.id, crtc->name);

			return -EINVAL;
		}
	}

	ret = handle_conflicting_encoders(state, false);
	if (ret)
		return ret;

	for_each_oldnew_connector_in_state(state, connector, old_connector_state, new_connector_state, i) {
		const struct drm_connector_helper_funcs *funcs = connector->helper_private;

		WARN_ON(!drm_modeset_is_locked(&dev->mode_config.connection_mutex));

		/*
		 * This only sets crtc->connectors_changed for routing changes,
		 * drivers must set crtc->connectors_changed themselves when
		 * connector properties need to be updated.
		 */
		ret = update_connector_routing(state, connector,
					       old_connector_state,
					       new_connector_state);
		if (ret)
			return ret;
		if (old_connector_state->crtc) {
			new_crtc_state = drm_atomic_get_new_crtc_state(state,
								       old_connector_state->crtc);
			if (old_connector_state->link_status !=
			    new_connector_state->link_status)
				new_crtc_state->connectors_changed = true;
		}

		if (funcs->atomic_check)
			ret = funcs->atomic_check(connector, new_connector_state);
		if (ret)
			return ret;

		connectors_mask += BIT(i);
	}

	/*
	 * After all the routing has been prepared we need to add in any
	 * connector which is itself unchanged, but who's crtc changes it's
	 * configuration. This must be done before calling mode_fixup in case a
	 * crtc only changed its mode but has the same set of connectors.
	 */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;

		DRM_DEBUG_ATOMIC("[CRTC:%d:%s] needs all connectors, enable: %c, active: %c\n",
				 crtc->base.id, crtc->name,
				 new_crtc_state->enable ? 'y' : 'n',
				 new_crtc_state->active ? 'y' : 'n');

		ret = drm_atomic_add_affected_connectors(state, crtc);
		if (ret != 0)
			return ret;

		ret = drm_atomic_add_affected_planes(state, crtc);
		if (ret != 0)
			return ret;
	}

	/*
	 * Iterate over all connectors again, to make sure atomic_check()
	 * has been called on them when a modeset is forced.
	 */
	for_each_oldnew_connector_in_state(state, connector, old_connector_state, new_connector_state, i) {
		const struct drm_connector_helper_funcs *funcs = connector->helper_private;

		if (connectors_mask & BIT(i))
			continue;

		if (funcs->atomic_check)
			ret = funcs->atomic_check(connector, new_connector_state);
		if (ret)
			return ret;
	}

	ret = mode_valid(state);
	if (ret)
		return ret;

	return mode_fixup(state);
}
EXPORT_SYMBOL(drm_atomic_helper_check_modeset);

/**
 * drm_atomic_helper_check_planes - validate state object for planes changes
 * @dev: DRM device
 * @state: the driver state object
 *
 * Check the state object to see if the requested state is physically possible.
 * This does all the plane update related checks using by calling into the
 * &drm_crtc_helper_funcs.atomic_check and &drm_plane_helper_funcs.atomic_check
 * hooks provided by the driver.
 *
 * It also sets &drm_crtc_state.planes_changed to indicate that a crtc has
 * updated planes.
 *
 * RETURNS:
 * Zero for success or -errno
 */
int
drm_atomic_helper_check_planes(struct drm_device *dev,
			       struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *new_plane_state, *old_plane_state;
	int i, ret = 0;

	for_each_oldnew_plane_in_state(state, plane, old_plane_state, new_plane_state, i) {
		const struct drm_plane_helper_funcs *funcs;

		WARN_ON(!drm_modeset_is_locked(&plane->mutex));

		funcs = plane->helper_private;

		drm_atomic_helper_plane_changed(state, old_plane_state, new_plane_state, plane);

		if (!funcs || !funcs->atomic_check)
			continue;

		ret = funcs->atomic_check(plane, new_plane_state);
		if (ret) {
			DRM_DEBUG_ATOMIC("[PLANE:%d:%s] atomic driver check failed\n",
					 plane->base.id, plane->name);
			return ret;
		}
	}

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		funcs = crtc->helper_private;

		if (!funcs || !funcs->atomic_check)
			continue;

		ret = funcs->atomic_check(crtc, new_crtc_state);
		if (ret) {
			DRM_DEBUG_ATOMIC("[CRTC:%d:%s] atomic driver check failed\n",
					 crtc->base.id, crtc->name);
			return ret;
		}
	}

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_check_planes);

/**
 * drm_atomic_helper_check - validate state object
 * @dev: DRM device
 * @state: the driver state object
 *
 * Check the state object to see if the requested state is physically possible.
 * Only crtcs and planes have check callbacks, so for any additional (global)
 * checking that a driver needs it can simply wrap that around this function.
 * Drivers without such needs can directly use this as their
 * &drm_mode_config_funcs.atomic_check callback.
 *
 * This just wraps the two parts of the state checking for planes and modeset
 * state in the default order: First it calls drm_atomic_helper_check_modeset()
 * and then drm_atomic_helper_check_planes(). The assumption is that the
 * @drm_plane_helper_funcs.atomic_check and @drm_crtc_helper_funcs.atomic_check
 * functions depend upon an updated adjusted_mode.clock to e.g. properly compute
 * watermarks.
 *
 * RETURNS:
 * Zero for success or -errno
 */
int drm_atomic_helper_check(struct drm_device *dev,
			    struct drm_atomic_state *state)
{
	int ret;

	ret = drm_atomic_helper_check_modeset(dev, state);
	if (ret)
		return ret;

	ret = drm_atomic_helper_check_planes(dev, state);
	if (ret)
		return ret;

	if (state->legacy_cursor_update)
		state->async_update = !drm_atomic_helper_async_check(dev, state);

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_check);

static void
disable_outputs(struct drm_device *dev, struct drm_atomic_state *old_state)
{
	struct drm_connector *connector;
	struct drm_connector_state *old_conn_state, *new_conn_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	int i;

	for_each_oldnew_connector_in_state(old_state, connector, old_conn_state, new_conn_state, i) {
		const struct drm_encoder_helper_funcs *funcs;
		struct drm_encoder *encoder;

		/* Shut down everything that's in the changeset and currently
		 * still on. So need to check the old, saved state. */
		if (!old_conn_state->crtc)
			continue;

		old_crtc_state = drm_atomic_get_old_crtc_state(old_state, old_conn_state->crtc);

		if (!old_crtc_state->active ||
		    !drm_atomic_crtc_needs_modeset(old_conn_state->crtc->state))
			continue;

		encoder = old_conn_state->best_encoder;

		/* We shouldn't get this far if we didn't previously have
		 * an encoder.. but WARN_ON() rather than explode.
		 */
		if (WARN_ON(!encoder))
			continue;

		funcs = encoder->helper_private;

		DRM_DEBUG_ATOMIC("disabling [ENCODER:%d:%s]\n",
				 encoder->base.id, encoder->name);

		/*
		 * Each encoder has at most one connector (since we always steal
		 * it away), so we won't call disable hooks twice.
		 */
		drm_bridge_disable(encoder->bridge);

		/* Right function depends upon target state. */
		if (funcs) {
			if (new_conn_state->crtc && funcs->prepare)
				funcs->prepare(encoder);
			else if (funcs->disable)
				funcs->disable(encoder);
			else if (funcs->dpms)
				funcs->dpms(encoder, DRM_MODE_DPMS_OFF);
		}

		drm_bridge_post_disable(encoder->bridge);
	}

	for_each_oldnew_crtc_in_state(old_state, crtc, old_crtc_state, new_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		/* Shut down everything that needs a full modeset. */
		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;

		if (!old_crtc_state->active)
			continue;

		funcs = crtc->helper_private;

		DRM_DEBUG_ATOMIC("disabling [CRTC:%d:%s]\n",
				 crtc->base.id, crtc->name);


		/* Right function depends upon target state. */
		if (new_crtc_state->enable && funcs->prepare)
			funcs->prepare(crtc);
		else if (funcs->atomic_disable)
			funcs->atomic_disable(crtc, old_crtc_state);
		else if (funcs->disable)
			funcs->disable(crtc);
		else
			funcs->dpms(crtc, DRM_MODE_DPMS_OFF);
	}
}

/**
 * drm_atomic_helper_update_legacy_modeset_state - update legacy modeset state
 * @dev: DRM device
 * @old_state: atomic state object with old state structures
 *
 * This function updates all the various legacy modeset state pointers in
 * connectors, encoders and crtcs. It also updates the timestamping constants
 * used for precise vblank timestamps by calling
 * drm_calc_timestamping_constants().
 *
 * Drivers can use this for building their own atomic commit if they don't have
 * a pure helper-based modeset implementation.
 */
void
drm_atomic_helper_update_legacy_modeset_state(struct drm_device *dev,
					      struct drm_atomic_state *old_state)
{
	struct drm_connector *connector;
	struct drm_connector_state *old_conn_state, *new_conn_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	int i;

	/* clear out existing links and update dpms */
	for_each_oldnew_connector_in_state(old_state, connector, old_conn_state, new_conn_state, i) {
		if (connector->encoder) {
			WARN_ON(!connector->encoder->crtc);

			connector->encoder->crtc = NULL;
			connector->encoder = NULL;
		}

		crtc = new_conn_state->crtc;
		if ((!crtc && old_conn_state->crtc) ||
		    (crtc && drm_atomic_crtc_needs_modeset(crtc->state))) {
			struct drm_property *dpms_prop =
				dev->mode_config.dpms_property;
			int mode = DRM_MODE_DPMS_OFF;

			if (crtc && crtc->state->active)
				mode = DRM_MODE_DPMS_ON;

			connector->dpms = mode;
			drm_object_property_set_value(&connector->base,
						      dpms_prop, mode);
		}
	}

	/* set new links */
	for_each_new_connector_in_state(old_state, connector, new_conn_state, i) {
		if (!new_conn_state->crtc)
			continue;

		if (WARN_ON(!new_conn_state->best_encoder))
			continue;

		connector->encoder = new_conn_state->best_encoder;
		connector->encoder->crtc = new_conn_state->crtc;
	}

	/* set legacy state in the crtc structure */
	for_each_new_crtc_in_state(old_state, crtc, new_crtc_state, i) {
		struct drm_plane *primary = crtc->primary;
		struct drm_plane_state *new_plane_state;

		crtc->mode = new_crtc_state->mode;
		crtc->enabled = new_crtc_state->enable;

		new_plane_state =
			drm_atomic_get_new_plane_state(old_state, primary);

		if (new_plane_state && new_plane_state->crtc == crtc) {
			crtc->x = new_plane_state->src_x >> 16;
			crtc->y = new_plane_state->src_y >> 16;
		}

		if (new_crtc_state->enable)
			drm_calc_timestamping_constants(crtc,
							&new_crtc_state->adjusted_mode);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_update_legacy_modeset_state);

static void
crtc_set_mode(struct drm_device *dev, struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *new_conn_state;
	int i;

	for_each_new_crtc_in_state(old_state, crtc, new_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		if (!new_crtc_state->mode_changed)
			continue;

		funcs = crtc->helper_private;

		if (new_crtc_state->enable && funcs->mode_set_nofb) {
			DRM_DEBUG_ATOMIC("modeset on [CRTC:%d:%s]\n",
					 crtc->base.id, crtc->name);

			funcs->mode_set_nofb(crtc);
		}
	}

	for_each_new_connector_in_state(old_state, connector, new_conn_state, i) {
		const struct drm_encoder_helper_funcs *funcs;
		struct drm_encoder *encoder;
		struct drm_display_mode *mode, *adjusted_mode;

		if (!new_conn_state->best_encoder)
			continue;

		encoder = new_conn_state->best_encoder;
		funcs = encoder->helper_private;
		new_crtc_state = new_conn_state->crtc->state;
		mode = &new_crtc_state->mode;
		adjusted_mode = &new_crtc_state->adjusted_mode;

		if (!new_crtc_state->mode_changed)
			continue;

		DRM_DEBUG_ATOMIC("modeset on [ENCODER:%d:%s]\n",
				 encoder->base.id, encoder->name);

		/*
		 * Each encoder has at most one connector (since we always steal
		 * it away), so we won't call mode_set hooks twice.
		 */
		if (funcs && funcs->atomic_mode_set) {
			funcs->atomic_mode_set(encoder, new_crtc_state,
					       new_conn_state);
		} else if (funcs && funcs->mode_set) {
			funcs->mode_set(encoder, mode, adjusted_mode);
		}

		drm_bridge_mode_set(encoder->bridge, mode, adjusted_mode);
	}
}

/**
 * drm_atomic_helper_commit_modeset_disables - modeset commit to disable outputs
 * @dev: DRM device
 * @old_state: atomic state object with old state structures
 *
 * This function shuts down all the outputs that need to be shut down and
 * prepares them (if required) with the new mode.
 *
 * For compatibility with legacy crtc helpers this should be called before
 * drm_atomic_helper_commit_planes(), which is what the default commit function
 * does. But drivers with different needs can group the modeset commits together
 * and do the plane commits at the end. This is useful for drivers doing runtime
 * PM since planes updates then only happen when the CRTC is actually enabled.
 */
void drm_atomic_helper_commit_modeset_disables(struct drm_device *dev,
					       struct drm_atomic_state *old_state)
{
	disable_outputs(dev, old_state);

	drm_atomic_helper_update_legacy_modeset_state(dev, old_state);

	crtc_set_mode(dev, old_state);
}
EXPORT_SYMBOL(drm_atomic_helper_commit_modeset_disables);

/**
 * drm_atomic_helper_commit_modeset_enables - modeset commit to enable outputs
 * @dev: DRM device
 * @old_state: atomic state object with old state structures
 *
 * This function enables all the outputs with the new configuration which had to
 * be turned off for the update.
 *
 * For compatibility with legacy crtc helpers this should be called after
 * drm_atomic_helper_commit_planes(), which is what the default commit function
 * does. But drivers with different needs can group the modeset commits together
 * and do the plane commits at the end. This is useful for drivers doing runtime
 * PM since planes updates then only happen when the CRTC is actually enabled.
 */
void drm_atomic_helper_commit_modeset_enables(struct drm_device *dev,
					      struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	struct drm_crtc_state *new_crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *new_conn_state;
	int i;

	for_each_oldnew_crtc_in_state(old_state, crtc, old_crtc_state, new_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		/* Need to filter out CRTCs where only planes change. */
		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;

		if (!new_crtc_state->active)
			continue;

		funcs = crtc->helper_private;

		if (new_crtc_state->enable) {
			DRM_DEBUG_ATOMIC("enabling [CRTC:%d:%s]\n",
					 crtc->base.id, crtc->name);

			if (funcs->atomic_enable)
				funcs->atomic_enable(crtc, old_crtc_state);
			else
				funcs->commit(crtc);
		}
	}

	for_each_new_connector_in_state(old_state, connector, new_conn_state, i) {
		const struct drm_encoder_helper_funcs *funcs;
		struct drm_encoder *encoder;

		if (!new_conn_state->best_encoder)
			continue;

		if (!new_conn_state->crtc->state->active ||
		    !drm_atomic_crtc_needs_modeset(new_conn_state->crtc->state))
			continue;

		encoder = new_conn_state->best_encoder;
		funcs = encoder->helper_private;

		DRM_DEBUG_ATOMIC("enabling [ENCODER:%d:%s]\n",
				 encoder->base.id, encoder->name);

		/*
		 * Each encoder has at most one connector (since we always steal
		 * it away), so we won't call enable hooks twice.
		 */
		drm_bridge_pre_enable(encoder->bridge);

		if (funcs) {
			if (funcs->enable)
				funcs->enable(encoder);
			else if (funcs->commit)
				funcs->commit(encoder);
		}

		drm_bridge_enable(encoder->bridge);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_commit_modeset_enables);

/**
 * drm_atomic_helper_wait_for_fences - wait for fences stashed in plane state
 * @dev: DRM device
 * @state: atomic state object with old state structures
 * @pre_swap: If true, do an interruptible wait, and @state is the new state.
 * 	Otherwise @state is the old state.
 *
 * For implicit sync, driver should fish the exclusive fence out from the
 * incoming fb's and stash it in the drm_plane_state.  This is called after
 * drm_atomic_helper_swap_state() so it uses the current plane state (and
 * just uses the atomic state to find the changed planes)
 *
 * Note that @pre_swap is needed since the point where we block for fences moves
 * around depending upon whether an atomic commit is blocking or
 * non-blocking. For non-blocking commit all waiting needs to happen after
 * drm_atomic_helper_swap_state() is called, but for blocking commits we want
 * to wait **before** we do anything that can't be easily rolled back. That is
 * before we call drm_atomic_helper_swap_state().
 *
 * Returns zero if success or < 0 if dma_fence_wait() fails.
 */
int drm_atomic_helper_wait_for_fences(struct drm_device *dev,
				      struct drm_atomic_state *state,
				      bool pre_swap)
{
	struct drm_plane *plane;
	struct drm_plane_state *new_plane_state;
	int i, ret;

	for_each_new_plane_in_state(state, plane, new_plane_state, i) {
		if (!new_plane_state->fence)
			continue;

		WARN_ON(!new_plane_state->fb);

		/*
		 * If waiting for fences pre-swap (ie: nonblock), userspace can
		 * still interrupt the operation. Instead of blocking until the
		 * timer expires, make the wait interruptible.
		 */
		ret = dma_fence_wait(new_plane_state->fence, pre_swap);
		if (ret)
			return ret;

		dma_fence_put(new_plane_state->fence);
		new_plane_state->fence = NULL;
	}

	return 0;
}
EXPORT_SYMBOL(drm_atomic_helper_wait_for_fences);

/**
 * drm_atomic_helper_wait_for_vblanks - wait for vblank on crtcs
 * @dev: DRM device
 * @old_state: atomic state object with old state structures
 *
 * Helper to, after atomic commit, wait for vblanks on all effected
 * crtcs (ie. before cleaning up old framebuffers using
 * drm_atomic_helper_cleanup_planes()). It will only wait on CRTCs where the
 * framebuffers have actually changed to optimize for the legacy cursor and
 * plane update use-case.
 *
 * Drivers using the nonblocking commit tracking support initialized by calling
 * drm_atomic_helper_setup_commit() should look at
 * drm_atomic_helper_wait_for_flip_done() as an alternative.
 */
void
drm_atomic_helper_wait_for_vblanks(struct drm_device *dev,
		struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	int i, ret;
	unsigned crtc_mask = 0;

	 /*
	  * Legacy cursor ioctls are completely unsynced, and userspace
	  * relies on that (by doing tons of cursor updates).
	  */
	if (old_state->legacy_cursor_update)
		return;

	for_each_oldnew_crtc_in_state(old_state, crtc, old_crtc_state, new_crtc_state, i) {
		if (!new_crtc_state->active || !new_crtc_state->planes_changed)
			continue;

		ret = drm_crtc_vblank_get(crtc);
		if (ret != 0)
			continue;

		crtc_mask |= drm_crtc_mask(crtc);
		old_state->crtcs[i].last_vblank_count = drm_crtc_vblank_count(crtc);
	}

	for_each_old_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		if (!(crtc_mask & drm_crtc_mask(crtc)))
			continue;

		ret = wait_event_timeout(dev->vblank[i].queue,
				old_state->crtcs[i].last_vblank_count !=
					drm_crtc_vblank_count(crtc),
				msecs_to_jiffies(50));

		WARN(!ret, "[CRTC:%d:%s] vblank wait timed out\n",
		     crtc->base.id, crtc->name);

		drm_crtc_vblank_put(crtc);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_wait_for_vblanks);

/**
 * drm_atomic_helper_wait_for_flip_done - wait for all page flips to be done
 * @dev: DRM device
 * @old_state: atomic state object with old state structures
 *
 * Helper to, after atomic commit, wait for page flips on all effected
 * crtcs (ie. before cleaning up old framebuffers using
 * drm_atomic_helper_cleanup_planes()). Compared to
 * drm_atomic_helper_wait_for_vblanks() this waits for the completion of on all
 * CRTCs, assuming that cursors-only updates are signalling their completion
 * immediately (or using a different path).
 *
 * This requires that drivers use the nonblocking commit tracking support
 * initialized using drm_atomic_helper_setup_commit().
 */
void drm_atomic_helper_wait_for_flip_done(struct drm_device *dev,
					  struct drm_atomic_state *old_state)
{
	struct drm_crtc_state *unused;
	struct drm_crtc *crtc;
	int i;

	for_each_crtc_in_state(old_state, crtc, unused, i) {
		struct drm_crtc_commit *commit = old_state->crtcs[i].commit;
		int ret;

		if (!commit)
			continue;

		ret = wait_for_completion_timeout(&commit->flip_done, 10 * HZ);
		if (ret == 0)
			DRM_ERROR("[CRTC:%d:%s] flip_done timed out\n",
				  crtc->base.id, crtc->name);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_wait_for_flip_done);

/**
 * drm_atomic_helper_commit_tail - commit atomic update to hardware
 * @old_state: atomic state object with old state structures
 *
 * This is the default implementation for the
 * &drm_mode_config_helper_funcs.atomic_commit_tail hook.
 *
 * Note that the default ordering of how the various stages are called is to
 * match the legacy modeset helper library closest. One peculiarity of that is
 * that it doesn't mesh well with runtime PM at all.
 *
 * For drivers supporting runtime PM the recommended sequence is instead ::
 *
 *     drm_atomic_helper_commit_modeset_disables(dev, old_state);
 *
 *     drm_atomic_helper_commit_modeset_enables(dev, old_state);
 *
 *     drm_atomic_helper_commit_planes(dev, old_state,
 *                                     DRM_PLANE_COMMIT_ACTIVE_ONLY);
 *
 * for committing the atomic update to hardware.  See the kerneldoc entries for
 * these three functions for more details.
 */
void drm_atomic_helper_commit_tail(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;

	drm_atomic_helper_commit_modeset_disables(dev, old_state);

	drm_atomic_helper_commit_planes(dev, old_state, 0);

	drm_atomic_helper_commit_modeset_enables(dev, old_state);

	drm_atomic_helper_commit_hw_done(old_state);

	drm_atomic_helper_wait_for_vblanks(dev, old_state);

	drm_atomic_helper_cleanup_planes(dev, old_state);
}
EXPORT_SYMBOL(drm_atomic_helper_commit_tail);

static void commit_tail(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;
	const struct drm_mode_config_helper_funcs *funcs;

	funcs = dev->mode_config.helper_private;

	drm_atomic_helper_wait_for_fences(dev, old_state, false);

	drm_atomic_helper_wait_for_dependencies(old_state);

	if (funcs && funcs->atomic_commit_tail)
		funcs->atomic_commit_tail(old_state);
	else
		drm_atomic_helper_commit_tail(old_state);

	drm_atomic_helper_commit_cleanup_done(old_state);

	drm_atomic_state_put(old_state);
}

static void commit_work(struct work_struct *work)
{
	struct drm_atomic_state *state = container_of(work,
						      struct drm_atomic_state,
						      commit_work);
	commit_tail(state);
}

/**
 * drm_atomic_helper_async_check - check if state can be commited asynchronously
 * @dev: DRM device
 * @state: the driver state object
 *
 * This helper will check if it is possible to commit the state asynchronously.
 * Async commits are not supposed to swap the states like normal sync commits
 * but just do in-place changes on the current state.
 *
 * It will return 0 if the commit can happen in an asynchronous fashion or error
 * if not. Note that error just mean it can't be commited asynchronously, if it
 * fails the commit should be treated like a normal synchronous commit.
 */
int drm_atomic_helper_async_check(struct drm_device *dev,
				   struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_crtc_commit *commit;
	struct drm_plane *__plane, *plane = NULL;
	struct drm_plane_state *__plane_state, *plane_state = NULL;
	const struct drm_plane_helper_funcs *funcs;
	int i, j, n_planes = 0;

	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		if (drm_atomic_crtc_needs_modeset(crtc_state))
			return -EINVAL;
	}

	for_each_new_plane_in_state(state, __plane, __plane_state, i) {
		n_planes++;
		plane = __plane;
		plane_state = __plane_state;
	}

	/* FIXME: we support only single plane updates for now */
	if (!plane || n_planes != 1)
		return -EINVAL;

	if (!plane_state->crtc)
		return -EINVAL;

	funcs = plane->helper_private;
	if (!funcs->atomic_async_update)
		return -EINVAL;

	if (plane_state->fence)
		return -EINVAL;

	/*
	 * Don't do an async update if there is an outstanding commit modifying
	 * the plane.  This prevents our async update's changes from getting
	 * overridden by a previous synchronous update's state.
	 */
	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		if (plane->crtc != crtc)
			continue;

		spin_lock(&crtc->commit_lock);
		commit = list_first_entry_or_null(&crtc->commit_list,
						  struct drm_crtc_commit,
						  commit_entry);
		if (!commit) {
			spin_unlock(&crtc->commit_lock);
			continue;
		}
		spin_unlock(&crtc->commit_lock);

		if (!crtc->state->state)
			continue;

		for_each_plane_in_state(crtc->state->state, __plane,
					__plane_state, j) {
			if (__plane == plane)
				return -EINVAL;
		}
	}

	return funcs->atomic_async_check(plane, plane_state);
}
EXPORT_SYMBOL(drm_atomic_helper_async_check);

/**
 * drm_atomic_helper_async_commit - commit state asynchronously
 * @dev: DRM device
 * @state: the driver state object
 *
 * This function commits a state asynchronously, i.e., not vblank
 * synchronized. It should be used on a state only when
 * drm_atomic_async_check() succeeds. Async commits are not supposed to swap
 * the states like normal sync commits, but just do in-place changes on the
 * current state.
 */
void drm_atomic_helper_async_commit(struct drm_device *dev,
				    struct drm_atomic_state *state)
{
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	const struct drm_plane_helper_funcs *funcs;
	int i;

	for_each_new_plane_in_state(state, plane, plane_state, i) {
		funcs = plane->helper_private;
		funcs->atomic_async_update(plane, plane_state);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_async_commit);

/**
 * drm_atomic_helper_commit - commit validated state object
 * @dev: DRM device
 * @state: the driver state object
 * @nonblock: whether nonblocking behavior is requested.
 *
 * This function commits a with drm_atomic_helper_check() pre-validated state
 * object. This can still fail when e.g. the framebuffer reservation fails. This
 * function implements nonblocking commits, using
 * drm_atomic_helper_setup_commit() and related functions.
 *
 * Committing the actual hardware state is done through the
 * &drm_mode_config_helper_funcs.atomic_commit_tail callback, or it's default
 * implementation drm_atomic_helper_commit_tail().
 *
 * RETURNS:
 * Zero for success or -errno.
 */
int drm_atomic_helper_commit(struct drm_device *dev,
			     struct drm_atomic_state *state,
			     bool nonblock)
{
	int ret;

	if (state->async_update) {
		ret = drm_atomic_helper_prepare_planes(dev, state);
		if (ret)
			return ret;

		drm_atomic_helper_async_commit(dev, state);
		drm_atomic_helper_cleanup_planes(dev, state);

		return 0;
	}

	ret = drm_atomic_helper_setup_commit(state, nonblock);
	if (ret)
		return ret;

	INIT_WORK(&state->commit_work, commit_work);

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret)
		return ret;

	if (!nonblock) {
		ret = drm_atomic_helper_wait_for_fences(dev, state, true);
		if (ret) {
			drm_atomic_helper_cleanup_planes(dev, state);
			return ret;
		}
	}

	/*
	 * This is the point of no return - everything below never fails except
	 * when the hw goes bonghits. Which means we can commit the new state on
	 * the software side now.
	 */

	drm_atomic_helper_swap_state(state, true);

	/*
	 * Everything below can be run asynchronously without the need to grab
	 * any modeset locks at all under one condition: It must be guaranteed
	 * that the asynchronous work has either been cancelled (if the driver
	 * supports it, which at least requires that the framebuffers get
	 * cleaned up with drm_atomic_helper_cleanup_planes()) or completed
	 * before the new state gets committed on the software side with
	 * drm_atomic_helper_swap_state().
	 *
	 * This scheme allows new atomic state updates to be prepared and
	 * checked in parallel to the asynchronous completion of the previous
	 * update. Which is important since compositors need to figure out the
	 * composition of the next frame right after having submitted the
	 * current layout.
	 *
	 * NOTE: Commit work has multiple phases, first hardware commit, then
	 * cleanup. We want them to overlap, hence need system_unbound_wq to
	 * make sure work items don't artifically stall on each another.
	 */

	drm_atomic_state_get(state);
	if (nonblock)
		queue_work(system_unbound_wq, &state->commit_work);
	else
		commit_tail(state);

	return 0;
}
EXPORT_SYMBOL(drm_atomic_helper_commit);

/**
 * DOC: implementing nonblocking commit
 *
 * Nonblocking atomic commits have to be implemented in the following sequence:
 *
 * 1. Run drm_atomic_helper_prepare_planes() first. This is the only function
 * which commit needs to call which can fail, so we want to run it first and
 * synchronously.
 *
 * 2. Synchronize with any outstanding nonblocking commit worker threads which
 * might be affected the new state update. This can be done by either cancelling
 * or flushing the work items, depending upon whether the driver can deal with
 * cancelled updates. Note that it is important to ensure that the framebuffer
 * cleanup is still done when cancelling.
 *
 * Asynchronous workers need to have sufficient parallelism to be able to run
 * different atomic commits on different CRTCs in parallel. The simplest way to
 * achive this is by running them on the &system_unbound_wq work queue. Note
 * that drivers are not required to split up atomic commits and run an
 * individual commit in parallel - userspace is supposed to do that if it cares.
 * But it might be beneficial to do that for modesets, since those necessarily
 * must be done as one global operation, and enabling or disabling a CRTC can
 * take a long time. But even that is not required.
 *
 * 3. The software state is updated synchronously with
 * drm_atomic_helper_swap_state(). Doing this under the protection of all modeset
 * locks means concurrent callers never see inconsistent state. And doing this
 * while it's guaranteed that no relevant nonblocking worker runs means that
 * nonblocking workers do not need grab any locks. Actually they must not grab
 * locks, for otherwise the work flushing will deadlock.
 *
 * 4. Schedule a work item to do all subsequent steps, using the split-out
 * commit helpers: a) pre-plane commit b) plane commit c) post-plane commit and
 * then cleaning up the framebuffers after the old framebuffer is no longer
 * being displayed.
 *
 * The above scheme is implemented in the atomic helper libraries in
 * drm_atomic_helper_commit() using a bunch of helper functions. See
 * drm_atomic_helper_setup_commit() for a starting point.
 */

static int stall_checks(struct drm_crtc *crtc, bool nonblock)
{
	struct drm_crtc_commit *commit, *stall_commit = NULL;
	bool completed = true;
	int i;
	long ret = 0;

	spin_lock(&crtc->commit_lock);
	i = 0;
	list_for_each_entry(commit, &crtc->commit_list, commit_entry) {
		if (i == 0) {
			completed = try_wait_for_completion(&commit->flip_done);
			/* Userspace is not allowed to get ahead of the previous
			 * commit with nonblocking ones. */
			if (!completed && nonblock) {
				spin_unlock(&crtc->commit_lock);
				return -EBUSY;
			}
		} else if (i == 1) {
			stall_commit = commit;
			drm_crtc_commit_get(stall_commit);
			break;
		}

		i++;
	}
	spin_unlock(&crtc->commit_lock);

	if (!stall_commit)
		return 0;

	/* We don't want to let commits get ahead of cleanup work too much,
	 * stalling on 2nd previous commit means triple-buffer won't ever stall.
	 */
	ret = wait_for_completion_interruptible_timeout(&stall_commit->cleanup_done,
							10*HZ);
	if (ret == 0)
		DRM_ERROR("[CRTC:%d:%s] cleanup_done timed out\n",
			  crtc->base.id, crtc->name);

	drm_crtc_commit_put(stall_commit);

	return ret < 0 ? ret : 0;
}

static void release_crtc_commit(struct completion *completion)
{
	struct drm_crtc_commit *commit = container_of(completion,
						      typeof(*commit),
						      flip_done);

	drm_crtc_commit_put(commit);
}

/**
 * drm_atomic_helper_setup_commit - setup possibly nonblocking commit
 * @state: new modeset state to be committed
 * @nonblock: whether nonblocking behavior is requested.
 *
 * This function prepares @state to be used by the atomic helper's support for
 * nonblocking commits. Drivers using the nonblocking commit infrastructure
 * should always call this function from their
 * &drm_mode_config_funcs.atomic_commit hook.
 *
 * To be able to use this support drivers need to use a few more helper
 * functions. drm_atomic_helper_wait_for_dependencies() must be called before
 * actually committing the hardware state, and for nonblocking commits this call
 * must be placed in the async worker. See also drm_atomic_helper_swap_state()
 * and it's stall parameter, for when a driver's commit hooks look at the
 * &drm_crtc.state, &drm_plane.state or &drm_connector.state pointer directly.
 *
 * Completion of the hardware commit step must be signalled using
 * drm_atomic_helper_commit_hw_done(). After this step the driver is not allowed
 * to read or change any permanent software or hardware modeset state. The only
 * exception is state protected by other means than &drm_modeset_lock locks.
 * Only the free standing @state with pointers to the old state structures can
 * be inspected, e.g. to clean up old buffers using
 * drm_atomic_helper_cleanup_planes().
 *
 * At the very end, before cleaning up @state drivers must call
 * drm_atomic_helper_commit_cleanup_done().
 *
 * This is all implemented by in drm_atomic_helper_commit(), giving drivers a
 * complete and esay-to-use default implementation of the atomic_commit() hook.
 *
 * The tracking of asynchronously executed and still pending commits is done
 * using the core structure &drm_crtc_commit.
 *
 * By default there's no need to clean up resources allocated by this function
 * explicitly: drm_atomic_state_default_clear() will take care of that
 * automatically.
 *
 * Returns:
 *
 * 0 on success. -EBUSY when userspace schedules nonblocking commits too fast,
 * -ENOMEM on allocation failures and -EINTR when a signal is pending.
 */
int drm_atomic_helper_setup_commit(struct drm_atomic_state *state,
				   bool nonblock)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_crtc_commit *commit;
	int i, ret;

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		commit = kzalloc(sizeof(*commit), GFP_KERNEL);
		if (!commit)
			return -ENOMEM;

		init_completion(&commit->flip_done);
		init_completion(&commit->hw_done);
		init_completion(&commit->cleanup_done);
		INIT_LIST_HEAD(&commit->commit_entry);
		kref_init(&commit->ref);
		commit->crtc = crtc;

		state->crtcs[i].commit = commit;

		ret = stall_checks(crtc, nonblock);
		if (ret)
			return ret;

		/* Drivers only send out events when at least either current or
		 * new CRTC state is active. Complete right away if everything
		 * stays off. */
		if (!old_crtc_state->active && !new_crtc_state->active) {
			complete_all(&commit->flip_done);
			continue;
		}

		/* Legacy cursor updates are fully unsynced. */
		if (state->legacy_cursor_update) {
			complete_all(&commit->flip_done);
			continue;
		}

		if (!new_crtc_state->event) {
			commit->event = kzalloc(sizeof(*commit->event),
						GFP_KERNEL);
			if (!commit->event)
				return -ENOMEM;

			new_crtc_state->event = commit->event;
		}

		new_crtc_state->event->base.completion = &commit->flip_done;
		new_crtc_state->event->base.completion_release = release_crtc_commit;
		drm_crtc_commit_get(commit);
	}

	return 0;
}
EXPORT_SYMBOL(drm_atomic_helper_setup_commit);


static struct drm_crtc_commit *preceeding_commit(struct drm_crtc *crtc)
{
	struct drm_crtc_commit *commit;
	int i = 0;

	list_for_each_entry(commit, &crtc->commit_list, commit_entry) {
		/* skip the first entry, that's the current commit */
		if (i == 1)
			return commit;
		i++;
	}

	return NULL;
}

/**
 * drm_atomic_helper_wait_for_dependencies - wait for required preceeding commits
 * @old_state: atomic state object with old state structures
 *
 * This function waits for all preceeding commits that touch the same CRTC as
 * @old_state to both be committed to the hardware (as signalled by
 * drm_atomic_helper_commit_hw_done) and executed by the hardware (as signalled
 * by calling drm_crtc_vblank_send_event() on the &drm_crtc_state.event).
 *
 * This is part of the atomic helper support for nonblocking commits, see
 * drm_atomic_helper_setup_commit() for an overview.
 */
void drm_atomic_helper_wait_for_dependencies(struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc_commit *commit;
	int i;
	long ret;

	for_each_new_crtc_in_state(old_state, crtc, new_crtc_state, i) {
		spin_lock(&crtc->commit_lock);
		commit = preceeding_commit(crtc);
		if (commit)
			drm_crtc_commit_get(commit);
		spin_unlock(&crtc->commit_lock);

		if (!commit)
			continue;

		ret = wait_for_completion_timeout(&commit->hw_done,
						  10*HZ);
		if (ret == 0)
			DRM_ERROR("[CRTC:%d:%s] hw_done timed out\n",
				  crtc->base.id, crtc->name);

		/* Currently no support for overwriting flips, hence
		 * stall for previous one to execute completely. */
		ret = wait_for_completion_timeout(&commit->flip_done,
						  10*HZ);
		if (ret == 0)
			DRM_ERROR("[CRTC:%d:%s] flip_done timed out\n",
				  crtc->base.id, crtc->name);

		drm_crtc_commit_put(commit);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_wait_for_dependencies);

/**
 * drm_atomic_helper_commit_hw_done - setup possible nonblocking commit
 * @old_state: atomic state object with old state structures
 *
 * This function is used to signal completion of the hardware commit step. After
 * this step the driver is not allowed to read or change any permanent software
 * or hardware modeset state. The only exception is state protected by other
 * means than &drm_modeset_lock locks.
 *
 * Drivers should try to postpone any expensive or delayed cleanup work after
 * this function is called.
 *
 * This is part of the atomic helper support for nonblocking commits, see
 * drm_atomic_helper_setup_commit() for an overview.
 */
void drm_atomic_helper_commit_hw_done(struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc_commit *commit;
	int i;

	for_each_new_crtc_in_state(old_state, crtc, new_crtc_state, i) {
		commit = old_state->crtcs[i].commit;
		if (!commit)
			continue;

		/* backend must have consumed any event by now */
		WARN_ON(new_crtc_state->event);
		complete_all(&commit->hw_done);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_commit_hw_done);

/**
 * drm_atomic_helper_commit_cleanup_done - signal completion of commit
 * @old_state: atomic state object with old state structures
 *
 * This signals completion of the atomic update @old_state, including any
 * cleanup work. If used, it must be called right before calling
 * drm_atomic_state_put().
 *
 * This is part of the atomic helper support for nonblocking commits, see
 * drm_atomic_helper_setup_commit() for an overview.
 */
void drm_atomic_helper_commit_cleanup_done(struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc_commit *commit;
	int i;
	long ret;

	for_each_new_crtc_in_state(old_state, crtc, new_crtc_state, i) {
		commit = old_state->crtcs[i].commit;
		if (WARN_ON(!commit))
			continue;

		complete_all(&commit->cleanup_done);
		WARN_ON(!try_wait_for_completion(&commit->hw_done));

		/* commit_list borrows our reference, need to remove before we
		 * clean up our drm_atomic_state. But only after it actually
		 * completed, otherwise subsequent commits won't stall properly. */
		if (try_wait_for_completion(&commit->flip_done))
			goto del_commit;

		/* We must wait for the vblank event to signal our completion
		 * before releasing our reference, since the vblank work does
		 * not hold a reference of its own. */
		ret = wait_for_completion_timeout(&commit->flip_done,
						  10*HZ);
		if (ret == 0)
			DRM_ERROR("[CRTC:%d:%s] flip_done timed out\n",
				  crtc->base.id, crtc->name);

del_commit:
		spin_lock(&crtc->commit_lock);
		list_del(&commit->commit_entry);
		spin_unlock(&crtc->commit_lock);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_commit_cleanup_done);

/**
 * drm_atomic_helper_prepare_planes - prepare plane resources before commit
 * @dev: DRM device
 * @state: atomic state object with new state structures
 *
 * This function prepares plane state, specifically framebuffers, for the new
 * configuration, by calling &drm_plane_helper_funcs.prepare_fb. If any failure
 * is encountered this function will call &drm_plane_helper_funcs.cleanup_fb on
 * any already successfully prepared framebuffer.
 *
 * Returns:
 * 0 on success, negative error code on failure.
 */
int drm_atomic_helper_prepare_planes(struct drm_device *dev,
				     struct drm_atomic_state *state)
{
	struct drm_plane *plane;
	struct drm_plane_state *new_plane_state;
	int ret, i, j;

	for_each_new_plane_in_state(state, plane, new_plane_state, i) {
		const struct drm_plane_helper_funcs *funcs;

		funcs = plane->helper_private;

		if (funcs->prepare_fb) {
			ret = funcs->prepare_fb(plane, new_plane_state);
			if (ret)
				goto fail;
		}
	}

	return 0;

fail:
	for_each_new_plane_in_state(state, plane, new_plane_state, j) {
		const struct drm_plane_helper_funcs *funcs;

		if (j >= i)
			continue;

		funcs = plane->helper_private;

		if (funcs->cleanup_fb)
			funcs->cleanup_fb(plane, new_plane_state);
	}

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_prepare_planes);

static bool plane_crtc_active(const struct drm_plane_state *state)
{
	return state->crtc && state->crtc->state->active;
}

/**
 * drm_atomic_helper_commit_planes - commit plane state
 * @dev: DRM device
 * @old_state: atomic state object with old state structures
 * @flags: flags for committing plane state
 *
 * This function commits the new plane state using the plane and atomic helper
 * functions for planes and crtcs. It assumes that the atomic state has already
 * been pushed into the relevant object state pointers, since this step can no
 * longer fail.
 *
 * It still requires the global state object @old_state to know which planes and
 * crtcs need to be updated though.
 *
 * Note that this function does all plane updates across all CRTCs in one step.
 * If the hardware can't support this approach look at
 * drm_atomic_helper_commit_planes_on_crtc() instead.
 *
 * Plane parameters can be updated by applications while the associated CRTC is
 * disabled. The DRM/KMS core will store the parameters in the plane state,
 * which will be available to the driver when the CRTC is turned on. As a result
 * most drivers don't need to be immediately notified of plane updates for a
 * disabled CRTC.
 *
 * Unless otherwise needed, drivers are advised to set the ACTIVE_ONLY flag in
 * @flags in order not to receive plane update notifications related to a
 * disabled CRTC. This avoids the need to manually ignore plane updates in
 * driver code when the driver and/or hardware can't or just don't need to deal
 * with updates on disabled CRTCs, for example when supporting runtime PM.
 *
 * Drivers may set the NO_DISABLE_AFTER_MODESET flag in @flags if the relevant
 * display controllers require to disable a CRTC's planes when the CRTC is
 * disabled. This function would skip the &drm_plane_helper_funcs.atomic_disable
 * call for a plane if the CRTC of the old plane state needs a modesetting
 * operation. Of course, the drivers need to disable the planes in their CRTC
 * disable callbacks since no one else would do that.
 *
 * The drm_atomic_helper_commit() default implementation doesn't set the
 * ACTIVE_ONLY flag to most closely match the behaviour of the legacy helpers.
 * This should not be copied blindly by drivers.
 */
void drm_atomic_helper_commit_planes(struct drm_device *dev,
				     struct drm_atomic_state *old_state,
				     uint32_t flags)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	int i;
	bool active_only = flags & DRM_PLANE_COMMIT_ACTIVE_ONLY;
	bool no_disable = flags & DRM_PLANE_COMMIT_NO_DISABLE_AFTER_MODESET;

	for_each_oldnew_crtc_in_state(old_state, crtc, old_crtc_state, new_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		funcs = crtc->helper_private;

		if (!funcs || !funcs->atomic_begin)
			continue;

		if (active_only && !new_crtc_state->active)
			continue;

		funcs->atomic_begin(crtc, old_crtc_state);
	}

	for_each_oldnew_plane_in_state(old_state, plane, old_plane_state, new_plane_state, i) {
		const struct drm_plane_helper_funcs *funcs;
		bool disabling;

		funcs = plane->helper_private;

		if (!funcs)
			continue;

		disabling = drm_atomic_plane_disabling(old_plane_state,
						       new_plane_state);

		if (active_only) {
			/*
			 * Skip planes related to inactive CRTCs. If the plane
			 * is enabled use the state of the current CRTC. If the
			 * plane is being disabled use the state of the old
			 * CRTC to avoid skipping planes being disabled on an
			 * active CRTC.
			 */
			if (!disabling && !plane_crtc_active(new_plane_state))
				continue;
			if (disabling && !plane_crtc_active(old_plane_state))
				continue;
		}

		/*
		 * Special-case disabling the plane if drivers support it.
		 */
		if (disabling && funcs->atomic_disable) {
			struct drm_crtc_state *crtc_state;

			crtc_state = old_plane_state->crtc->state;

			if (drm_atomic_crtc_needs_modeset(crtc_state) &&
			    no_disable)
				continue;

			funcs->atomic_disable(plane, old_plane_state);
		} else if (new_plane_state->crtc || disabling) {
			funcs->atomic_update(plane, old_plane_state);
		}
	}

	for_each_oldnew_crtc_in_state(old_state, crtc, old_crtc_state, new_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		funcs = crtc->helper_private;

		if (!funcs || !funcs->atomic_flush)
			continue;

		if (active_only && !new_crtc_state->active)
			continue;

		funcs->atomic_flush(crtc, old_crtc_state);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_commit_planes);

/**
 * drm_atomic_helper_commit_planes_on_crtc - commit plane state for a crtc
 * @old_crtc_state: atomic state object with the old crtc state
 *
 * This function commits the new plane state using the plane and atomic helper
 * functions for planes on the specific crtc. It assumes that the atomic state
 * has already been pushed into the relevant object state pointers, since this
 * step can no longer fail.
 *
 * This function is useful when plane updates should be done crtc-by-crtc
 * instead of one global step like drm_atomic_helper_commit_planes() does.
 *
 * This function can only be savely used when planes are not allowed to move
 * between different CRTCs because this function doesn't handle inter-CRTC
 * depencies. Callers need to ensure that either no such depencies exist,
 * resolve them through ordering of commit calls or through some other means.
 */
void
drm_atomic_helper_commit_planes_on_crtc(struct drm_crtc_state *old_crtc_state)
{
	const struct drm_crtc_helper_funcs *crtc_funcs;
	struct drm_crtc *crtc = old_crtc_state->crtc;
	struct drm_atomic_state *old_state = old_crtc_state->state;
	struct drm_plane *plane;
	unsigned plane_mask;

	plane_mask = old_crtc_state->plane_mask;
	plane_mask |= crtc->state->plane_mask;

	crtc_funcs = crtc->helper_private;
	if (crtc_funcs && crtc_funcs->atomic_begin)
		crtc_funcs->atomic_begin(crtc, old_crtc_state);

	drm_for_each_plane_mask(plane, crtc->dev, plane_mask) {
		struct drm_plane_state *old_plane_state =
			drm_atomic_get_old_plane_state(old_state, plane);
		const struct drm_plane_helper_funcs *plane_funcs;

		plane_funcs = plane->helper_private;

		if (!old_plane_state || !plane_funcs)
			continue;

		WARN_ON(plane->state->crtc && plane->state->crtc != crtc);

		if (drm_atomic_plane_disabling(old_plane_state, plane->state) &&
		    plane_funcs->atomic_disable)
			plane_funcs->atomic_disable(plane, old_plane_state);
		else if (plane->state->crtc ||
			 drm_atomic_plane_disabling(old_plane_state, plane->state))
			plane_funcs->atomic_update(plane, old_plane_state);
	}

	if (crtc_funcs && crtc_funcs->atomic_flush)
		crtc_funcs->atomic_flush(crtc, old_crtc_state);
}
EXPORT_SYMBOL(drm_atomic_helper_commit_planes_on_crtc);

/**
 * drm_atomic_helper_disable_planes_on_crtc - helper to disable CRTC's planes
 * @old_crtc_state: atomic state object with the old CRTC state
 * @atomic: if set, synchronize with CRTC's atomic_begin/flush hooks
 *
 * Disables all planes associated with the given CRTC. This can be
 * used for instance in the CRTC helper atomic_disable callback to disable
 * all planes.
 *
 * If the atomic-parameter is set the function calls the CRTC's
 * atomic_begin hook before and atomic_flush hook after disabling the
 * planes.
 *
 * It is a bug to call this function without having implemented the
 * &drm_plane_helper_funcs.atomic_disable plane hook.
 */
void
drm_atomic_helper_disable_planes_on_crtc(struct drm_crtc_state *old_crtc_state,
					 bool atomic)
{
	struct drm_crtc *crtc = old_crtc_state->crtc;
	const struct drm_crtc_helper_funcs *crtc_funcs =
		crtc->helper_private;
	struct drm_plane *plane;

	if (atomic && crtc_funcs && crtc_funcs->atomic_begin)
		crtc_funcs->atomic_begin(crtc, NULL);

	drm_atomic_crtc_state_for_each_plane(plane, old_crtc_state) {
		const struct drm_plane_helper_funcs *plane_funcs =
			plane->helper_private;

		if (!plane_funcs)
			continue;

		WARN_ON(!plane_funcs->atomic_disable);
		if (plane_funcs->atomic_disable)
			plane_funcs->atomic_disable(plane, NULL);
	}

	if (atomic && crtc_funcs && crtc_funcs->atomic_flush)
		crtc_funcs->atomic_flush(crtc, NULL);
}
EXPORT_SYMBOL(drm_atomic_helper_disable_planes_on_crtc);

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
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	int i;

	for_each_oldnew_plane_in_state(old_state, plane, old_plane_state, new_plane_state, i) {
		const struct drm_plane_helper_funcs *funcs;
		struct drm_plane_state *plane_state;

		/*
		 * This might be called before swapping when commit is aborted,
		 * in which case we have to cleanup the new state.
		 */
		if (old_plane_state == plane->state)
			plane_state = new_plane_state;
		else
			plane_state = old_plane_state;

		funcs = plane->helper_private;

		if (funcs->cleanup_fb)
			funcs->cleanup_fb(plane, plane_state);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_cleanup_planes);

/**
 * drm_atomic_helper_swap_state - store atomic state into current sw state
 * @state: atomic state
 * @stall: stall for proceeding commits
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
 * 5. Call drm_atomic_helper_cleanup_planes() with @state, which since step 3
 * contains the old state. Also do any other cleanup required with that state.
 *
 * @stall must be set when nonblocking commits for this driver directly access
 * the &drm_plane.state, &drm_crtc.state or &drm_connector.state pointer. With
 * the current atomic helpers this is almost always the case, since the helpers
 * don't pass the right state structures to the callbacks.
 */
void drm_atomic_helper_swap_state(struct drm_atomic_state *state,
				  bool stall)
{
	int i;
	long ret;
	struct drm_connector *connector;
	struct drm_connector_state *old_conn_state, *new_conn_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	struct drm_crtc_commit *commit;
	void *obj, *obj_state;
	const struct drm_private_state_funcs *funcs;

	if (stall) {
		for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
			spin_lock(&crtc->commit_lock);
			commit = list_first_entry_or_null(&crtc->commit_list,
					struct drm_crtc_commit, commit_entry);
			if (commit)
				drm_crtc_commit_get(commit);
			spin_unlock(&crtc->commit_lock);

			if (!commit)
				continue;

			ret = wait_for_completion_timeout(&commit->hw_done,
							  10*HZ);
			if (ret == 0)
				DRM_ERROR("[CRTC:%d:%s] hw_done timed out\n",
					  crtc->base.id, crtc->name);
			drm_crtc_commit_put(commit);
		}
	}

	for_each_oldnew_connector_in_state(state, connector, old_conn_state, new_conn_state, i) {
		WARN_ON(connector->state != old_conn_state);

		old_conn_state->state = state;
		new_conn_state->state = NULL;

		state->connectors[i].state = old_conn_state;
		connector->state = new_conn_state;
	}

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		WARN_ON(crtc->state != old_crtc_state);

		old_crtc_state->state = state;
		new_crtc_state->state = NULL;

		state->crtcs[i].state = old_crtc_state;
		crtc->state = new_crtc_state;

		if (state->crtcs[i].commit) {
			spin_lock(&crtc->commit_lock);
			list_add(&state->crtcs[i].commit->commit_entry,
				 &crtc->commit_list);
			spin_unlock(&crtc->commit_lock);

			state->crtcs[i].commit->event = NULL;
		}
	}

	for_each_oldnew_plane_in_state(state, plane, old_plane_state, new_plane_state, i) {
		WARN_ON(plane->state != old_plane_state);

		old_plane_state->state = state;
		new_plane_state->state = NULL;

		state->planes[i].state = old_plane_state;
		plane->state = new_plane_state;
	}

	__for_each_private_obj(state, obj, obj_state, i, funcs)
		funcs->swap_state(obj, &state->private_objs[i].obj_state);
}
EXPORT_SYMBOL(drm_atomic_helper_swap_state);

/**
 * drm_atomic_helper_update_plane - Helper for primary plane update using atomic
 * @plane: plane object to update
 * @crtc: owning CRTC of owning plane
 * @fb: framebuffer to flip onto plane
 * @crtc_x: x offset of primary plane on crtc
 * @crtc_y: y offset of primary plane on crtc
 * @crtc_w: width of primary plane rectangle on crtc
 * @crtc_h: height of primary plane rectangle on crtc
 * @src_x: x offset of @fb for panning
 * @src_y: y offset of @fb for panning
 * @src_w: width of source rectangle in @fb
 * @src_h: height of source rectangle in @fb
 * @ctx: lock acquire context
 *
 * Provides a default plane update handler using the atomic driver interface.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
int drm_atomic_helper_update_plane(struct drm_plane *plane,
				   struct drm_crtc *crtc,
				   struct drm_framebuffer *fb,
				   int crtc_x, int crtc_y,
				   unsigned int crtc_w, unsigned int crtc_h,
				   uint32_t src_x, uint32_t src_y,
				   uint32_t src_w, uint32_t src_h,
				   struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_plane_state *plane_state;
	int ret = 0;

	state = drm_atomic_state_alloc(plane->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;
	plane_state = drm_atomic_get_plane_state(state, plane);
	if (IS_ERR(plane_state)) {
		ret = PTR_ERR(plane_state);
		goto fail;
	}

	ret = drm_atomic_set_crtc_for_plane(plane_state, crtc);
	if (ret != 0)
		goto fail;
	drm_atomic_set_fb_for_plane(plane_state, fb);
	plane_state->crtc_x = crtc_x;
	plane_state->crtc_y = crtc_y;
	plane_state->crtc_w = crtc_w;
	plane_state->crtc_h = crtc_h;
	plane_state->src_x = src_x;
	plane_state->src_y = src_y;
	plane_state->src_w = src_w;
	plane_state->src_h = src_h;

	if (plane == crtc->cursor)
		state->legacy_cursor_update = true;

	ret = drm_atomic_commit(state);
fail:
	drm_atomic_state_put(state);
	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_update_plane);

/**
 * drm_atomic_helper_disable_plane - Helper for primary plane disable using * atomic
 * @plane: plane to disable
 * @ctx: lock acquire context
 *
 * Provides a default plane disable handler using the atomic driver interface.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
int drm_atomic_helper_disable_plane(struct drm_plane *plane,
				    struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_plane_state *plane_state;
	int ret = 0;

	state = drm_atomic_state_alloc(plane->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;
	plane_state = drm_atomic_get_plane_state(state, plane);
	if (IS_ERR(plane_state)) {
		ret = PTR_ERR(plane_state);
		goto fail;
	}

	if (plane_state->crtc && (plane == plane->crtc->cursor))
		plane_state->state->legacy_cursor_update = true;

	ret = __drm_atomic_helper_disable_plane(plane, plane_state);
	if (ret != 0)
		goto fail;

	ret = drm_atomic_commit(state);
fail:
	drm_atomic_state_put(state);
	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_disable_plane);

/* just used from fb-helper and atomic-helper: */
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
		/* Don't update ->enable for the CRTC in the set_config request,
		 * since a mismatch would indicate a bug in the upper layers.
		 * The actual modeset code later on will catch any
		 * inconsistencies here. */
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

/**
 * drm_atomic_helper_set_config - set a new config from userspace
 * @set: mode set configuration
 * @ctx: lock acquisition context
 *
 * Provides a default crtc set_config handler using the atomic driver interface.
 *
 * NOTE: For backwards compatibility with old userspace this automatically
 * resets the "link-status" property to GOOD, to force any link
 * re-training. The SETCRTC ioctl does not define whether an update does
 * need a full modeset or just a plane update, hence we're allowed to do
 * that. See also drm_mode_connector_set_link_status_property().
 *
 * Returns:
 * Returns 0 on success, negative errno numbers on failure.
 */
int drm_atomic_helper_set_config(struct drm_mode_set *set,
				 struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_crtc *crtc = set->crtc;
	int ret = 0;

	state = drm_atomic_state_alloc(crtc->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;
	ret = __drm_atomic_helper_set_config(set, state);
	if (ret != 0)
		goto fail;

	ret = handle_conflicting_encoders(state, true);
	if (ret)
		return ret;

	ret = drm_atomic_commit(state);

fail:
	drm_atomic_state_put(state);
	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_set_config);

/* just used from fb-helper and atomic-helper: */
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

/**
 * drm_atomic_helper_disable_all - disable all currently active outputs
 * @dev: DRM device
 * @ctx: lock acquisition context
 *
 * Loops through all connectors, finding those that aren't turned off and then
 * turns them off by setting their DPMS mode to OFF and deactivating the CRTC
 * that they are connected to.
 *
 * This is used for example in suspend/resume to disable all currently active
 * functions when suspending. If you just want to shut down everything at e.g.
 * driver unload, look at drm_atomic_helper_shutdown().
 *
 * Note that if callers haven't already acquired all modeset locks this might
 * return -EDEADLK, which must be handled by calling drm_modeset_backoff().
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 *
 * See also:
 * drm_atomic_helper_suspend(), drm_atomic_helper_resume() and
 * drm_atomic_helper_shutdown().
 */
int drm_atomic_helper_disable_all(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_connector_state *conn_state;
	struct drm_connector *conn;
	struct drm_plane_state *plane_state;
	struct drm_plane *plane;
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	int ret, i;

	state = drm_atomic_state_alloc(dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;

	drm_for_each_crtc(crtc, dev) {
		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state)) {
			ret = PTR_ERR(crtc_state);
			goto free;
		}

		crtc_state->active = false;

		ret = drm_atomic_set_mode_prop_for_crtc(crtc_state, NULL);
		if (ret < 0)
			goto free;

		ret = drm_atomic_add_affected_planes(state, crtc);
		if (ret < 0)
			goto free;

		ret = drm_atomic_add_affected_connectors(state, crtc);
		if (ret < 0)
			goto free;
	}

	for_each_new_connector_in_state(state, conn, conn_state, i) {
		ret = drm_atomic_set_crtc_for_connector(conn_state, NULL);
		if (ret < 0)
			goto free;
	}

	for_each_new_plane_in_state(state, plane, plane_state, i) {
		ret = drm_atomic_set_crtc_for_plane(plane_state, NULL);
		if (ret < 0)
			goto free;

		drm_atomic_set_fb_for_plane(plane_state, NULL);
	}

	ret = drm_atomic_commit(state);
free:
	drm_atomic_state_put(state);
	return ret;
}

EXPORT_SYMBOL(drm_atomic_helper_disable_all);

/**
 * drm_atomic_helper_shutdown - shutdown all CRTC
 * @dev: DRM device
 *
 * This shuts down all CRTC, which is useful for driver unloading. Shutdown on
 * suspend should instead be handled with drm_atomic_helper_suspend(), since
 * that also takes a snapshot of the modeset state to be restored on resume.
 *
 * This is just a convenience wrapper around drm_atomic_helper_disable_all(),
 * and it is the atomic version of drm_crtc_force_disable_all().
 */
void drm_atomic_helper_shutdown(struct drm_device *dev)
{
	struct drm_modeset_acquire_ctx ctx;
	int ret;

	drm_modeset_acquire_init(&ctx, 0);
	while (1) {
		ret = drm_modeset_lock_all_ctx(dev, &ctx);
		if (!ret)
			ret = drm_atomic_helper_disable_all(dev, &ctx);

		if (ret != -EDEADLK)
			break;

		drm_modeset_backoff(&ctx);
	}

	if (ret)
		DRM_ERROR("Disabling all crtc's during unload failed with %i\n", ret);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}
EXPORT_SYMBOL(drm_atomic_helper_shutdown);

/**
 * drm_atomic_helper_suspend - subsystem-level suspend helper
 * @dev: DRM device
 *
 * Duplicates the current atomic state, disables all active outputs and then
 * returns a pointer to the original atomic state to the caller. Drivers can
 * pass this pointer to the drm_atomic_helper_resume() helper upon resume to
 * restore the output configuration that was active at the time the system
 * entered suspend.
 *
 * Note that it is potentially unsafe to use this. The atomic state object
 * returned by this function is assumed to be persistent. Drivers must ensure
 * that this holds true. Before calling this function, drivers must make sure
 * to suspend fbdev emulation so that nothing can be using the device.
 *
 * Returns:
 * A pointer to a copy of the state before suspend on success or an ERR_PTR()-
 * encoded error code on failure. Drivers should store the returned atomic
 * state object and pass it to the drm_atomic_helper_resume() helper upon
 * resume.
 *
 * See also:
 * drm_atomic_helper_duplicate_state(), drm_atomic_helper_disable_all(),
 * drm_atomic_helper_resume(), drm_atomic_helper_commit_duplicated_state()
 */
struct drm_atomic_state *drm_atomic_helper_suspend(struct drm_device *dev)
{
	struct drm_modeset_acquire_ctx ctx;
	struct drm_atomic_state *state;
	int err;

	drm_modeset_acquire_init(&ctx, 0);

retry:
	err = drm_modeset_lock_all_ctx(dev, &ctx);
	if (err < 0) {
		state = ERR_PTR(err);
		goto unlock;
	}

	state = drm_atomic_helper_duplicate_state(dev, &ctx);
	if (IS_ERR(state))
		goto unlock;

	err = drm_atomic_helper_disable_all(dev, &ctx);
	if (err < 0) {
		drm_atomic_state_put(state);
		state = ERR_PTR(err);
		goto unlock;
	}

unlock:
	if (PTR_ERR(state) == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry;
	}

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	return state;
}
EXPORT_SYMBOL(drm_atomic_helper_suspend);

/**
 * drm_atomic_helper_commit_duplicated_state - commit duplicated state
 * @state: duplicated atomic state to commit
 * @ctx: pointer to acquire_ctx to use for commit.
 *
 * The state returned by drm_atomic_helper_duplicate_state() and
 * drm_atomic_helper_suspend() is partially invalid, and needs to
 * be fixed up before commit.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 *
 * See also:
 * drm_atomic_helper_suspend()
 */
int drm_atomic_helper_commit_duplicated_state(struct drm_atomic_state *state,
					      struct drm_modeset_acquire_ctx *ctx)
{
	int i;
	struct drm_plane *plane;
	struct drm_plane_state *new_plane_state;
	struct drm_connector *connector;
	struct drm_connector_state *new_conn_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;

	state->acquire_ctx = ctx;

	for_each_new_plane_in_state(state, plane, new_plane_state, i)
		state->planes[i].old_state = plane->state;

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i)
		state->crtcs[i].old_state = crtc->state;

	for_each_new_connector_in_state(state, connector, new_conn_state, i)
		state->connectors[i].old_state = connector->state;

	return drm_atomic_commit(state);
}
EXPORT_SYMBOL(drm_atomic_helper_commit_duplicated_state);

/**
 * drm_atomic_helper_resume - subsystem-level resume helper
 * @dev: DRM device
 * @state: atomic state to resume to
 *
 * Calls drm_mode_config_reset() to synchronize hardware and software states,
 * grabs all modeset locks and commits the atomic state object. This can be
 * used in conjunction with the drm_atomic_helper_suspend() helper to
 * implement suspend/resume for drivers that support atomic mode-setting.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 *
 * See also:
 * drm_atomic_helper_suspend()
 */
int drm_atomic_helper_resume(struct drm_device *dev,
			     struct drm_atomic_state *state)
{
	struct drm_modeset_acquire_ctx ctx;
	int err;

	drm_mode_config_reset(dev);

	drm_modeset_acquire_init(&ctx, 0);
	while (1) {
		err = drm_modeset_lock_all_ctx(dev, &ctx);
		if (err)
			goto out;

		err = drm_atomic_helper_commit_duplicated_state(state, &ctx);
out:
		if (err != -EDEADLK)
			break;

		drm_modeset_backoff(&ctx);
	}

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return err;
}
EXPORT_SYMBOL(drm_atomic_helper_resume);

/**
 * drm_atomic_helper_crtc_set_property - helper for crtc properties
 * @crtc: DRM crtc
 * @property: DRM property
 * @val: value of property
 *
 * Provides a default crtc set_property handler using the atomic driver
 * interface.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
int
drm_atomic_helper_crtc_set_property(struct drm_crtc *crtc,
				    struct drm_property *property,
				    uint64_t val)
{
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	int ret = 0;

	state = drm_atomic_state_alloc(crtc->dev);
	if (!state)
		return -ENOMEM;

	/* ->set_property is always called with all locks held. */
	state->acquire_ctx = crtc->dev->mode_config.acquire_ctx;
retry:
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto fail;
	}

	ret = drm_atomic_crtc_set_property(crtc, crtc_state,
			property, val);
	if (ret)
		goto fail;

	ret = drm_atomic_commit(state);
fail:
	if (ret == -EDEADLK)
		goto backoff;

	drm_atomic_state_put(state);
	return ret;

backoff:
	drm_atomic_state_clear(state);
	drm_atomic_legacy_backoff(state);

	goto retry;
}
EXPORT_SYMBOL(drm_atomic_helper_crtc_set_property);

/**
 * drm_atomic_helper_plane_set_property - helper for plane properties
 * @plane: DRM plane
 * @property: DRM property
 * @val: value of property
 *
 * Provides a default plane set_property handler using the atomic driver
 * interface.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
int
drm_atomic_helper_plane_set_property(struct drm_plane *plane,
				    struct drm_property *property,
				    uint64_t val)
{
	struct drm_atomic_state *state;
	struct drm_plane_state *plane_state;
	int ret = 0;

	state = drm_atomic_state_alloc(plane->dev);
	if (!state)
		return -ENOMEM;

	/* ->set_property is always called with all locks held. */
	state->acquire_ctx = plane->dev->mode_config.acquire_ctx;
retry:
	plane_state = drm_atomic_get_plane_state(state, plane);
	if (IS_ERR(plane_state)) {
		ret = PTR_ERR(plane_state);
		goto fail;
	}

	ret = drm_atomic_plane_set_property(plane, plane_state,
			property, val);
	if (ret)
		goto fail;

	ret = drm_atomic_commit(state);
fail:
	if (ret == -EDEADLK)
		goto backoff;

	drm_atomic_state_put(state);
	return ret;

backoff:
	drm_atomic_state_clear(state);
	drm_atomic_legacy_backoff(state);

	goto retry;
}
EXPORT_SYMBOL(drm_atomic_helper_plane_set_property);

/**
 * drm_atomic_helper_connector_set_property - helper for connector properties
 * @connector: DRM connector
 * @property: DRM property
 * @val: value of property
 *
 * Provides a default connector set_property handler using the atomic driver
 * interface.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
int
drm_atomic_helper_connector_set_property(struct drm_connector *connector,
				    struct drm_property *property,
				    uint64_t val)
{
	struct drm_atomic_state *state;
	struct drm_connector_state *connector_state;
	int ret = 0;

	state = drm_atomic_state_alloc(connector->dev);
	if (!state)
		return -ENOMEM;

	/* ->set_property is always called with all locks held. */
	state->acquire_ctx = connector->dev->mode_config.acquire_ctx;
retry:
	connector_state = drm_atomic_get_connector_state(state, connector);
	if (IS_ERR(connector_state)) {
		ret = PTR_ERR(connector_state);
		goto fail;
	}

	ret = drm_atomic_connector_set_property(connector, connector_state,
			property, val);
	if (ret)
		goto fail;

	ret = drm_atomic_commit(state);
fail:
	if (ret == -EDEADLK)
		goto backoff;

	drm_atomic_state_put(state);
	return ret;

backoff:
	drm_atomic_state_clear(state);
	drm_atomic_legacy_backoff(state);

	goto retry;
}
EXPORT_SYMBOL(drm_atomic_helper_connector_set_property);

static int page_flip_common(struct drm_atomic_state *state,
			    struct drm_crtc *crtc,
			    struct drm_framebuffer *fb,
			    struct drm_pending_vblank_event *event,
			    uint32_t flags)
{
	struct drm_plane *plane = crtc->primary;
	struct drm_plane_state *plane_state;
	struct drm_crtc_state *crtc_state;
	int ret = 0;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	crtc_state->event = event;
	crtc_state->pageflip_flags = flags;

	plane_state = drm_atomic_get_plane_state(state, plane);
	if (IS_ERR(plane_state))
		return PTR_ERR(plane_state);

	ret = drm_atomic_set_crtc_for_plane(plane_state, crtc);
	if (ret != 0)
		return ret;
	drm_atomic_set_fb_for_plane(plane_state, fb);

	/* Make sure we don't accidentally do a full modeset. */
	state->allow_modeset = false;
	if (!crtc_state->active) {
		DRM_DEBUG_ATOMIC("[CRTC:%d:%s] disabled, rejecting legacy flip\n",
				 crtc->base.id, crtc->name);
		return -EINVAL;
	}

	return ret;
}

/**
 * drm_atomic_helper_page_flip - execute a legacy page flip
 * @crtc: DRM crtc
 * @fb: DRM framebuffer
 * @event: optional DRM event to signal upon completion
 * @flags: flip flags for non-vblank sync'ed updates
 * @ctx: lock acquisition context
 *
 * Provides a default &drm_crtc_funcs.page_flip implementation
 * using the atomic driver interface.
 *
 * Returns:
 * Returns 0 on success, negative errno numbers on failure.
 *
 * See also:
 * drm_atomic_helper_page_flip_target()
 */
int drm_atomic_helper_page_flip(struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				struct drm_pending_vblank_event *event,
				uint32_t flags,
				struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_plane *plane = crtc->primary;
	struct drm_atomic_state *state;
	int ret = 0;

	state = drm_atomic_state_alloc(plane->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;

	ret = page_flip_common(state, crtc, fb, event, flags);
	if (ret != 0)
		goto fail;

	ret = drm_atomic_nonblocking_commit(state);
fail:
	drm_atomic_state_put(state);
	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_page_flip);

/**
 * drm_atomic_helper_page_flip_target - do page flip on target vblank period.
 * @crtc: DRM crtc
 * @fb: DRM framebuffer
 * @event: optional DRM event to signal upon completion
 * @flags: flip flags for non-vblank sync'ed updates
 * @target: specifying the target vblank period when the flip to take effect
 * @ctx: lock acquisition context
 *
 * Provides a default &drm_crtc_funcs.page_flip_target implementation.
 * Similar to drm_atomic_helper_page_flip() with extra parameter to specify
 * target vblank period to flip.
 *
 * Returns:
 * Returns 0 on success, negative errno numbers on failure.
 */
int drm_atomic_helper_page_flip_target(struct drm_crtc *crtc,
				       struct drm_framebuffer *fb,
				       struct drm_pending_vblank_event *event,
				       uint32_t flags,
				       uint32_t target,
				       struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_plane *plane = crtc->primary;
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	int ret = 0;

	state = drm_atomic_state_alloc(plane->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;

	ret = page_flip_common(state, crtc, fb, event, flags);
	if (ret != 0)
		goto fail;

	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	if (WARN_ON(!crtc_state)) {
		ret = -EINVAL;
		goto fail;
	}
	crtc_state->target_vblank = target;

	ret = drm_atomic_nonblocking_commit(state);
fail:
	drm_atomic_state_put(state);
	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_page_flip_target);

/**
 * drm_atomic_helper_connector_dpms() - connector dpms helper implementation
 * @connector: affected connector
 * @mode: DPMS mode
 *
 * This is the main helper function provided by the atomic helper framework for
 * implementing the legacy DPMS connector interface. It computes the new desired
 * &drm_crtc_state.active state for the corresponding CRTC (if the connector is
 * enabled) and updates it.
 *
 * Returns:
 * Returns 0 on success, negative errno numbers on failure.
 */
int drm_atomic_helper_connector_dpms(struct drm_connector *connector,
				     int mode)
{
	struct drm_mode_config *config = &connector->dev->mode_config;
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	struct drm_connector *tmp_connector;
	struct drm_connector_list_iter conn_iter;
	int ret;
	bool active = false;
	int old_mode = connector->dpms;

	if (mode != DRM_MODE_DPMS_ON)
		mode = DRM_MODE_DPMS_OFF;

	connector->dpms = mode;
	crtc = connector->state->crtc;

	if (!crtc)
		return 0;

	state = drm_atomic_state_alloc(connector->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = crtc->dev->mode_config.acquire_ctx;
retry:
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto fail;
	}

	WARN_ON(!drm_modeset_is_locked(&config->connection_mutex));

	drm_connector_list_iter_begin(connector->dev, &conn_iter);
	drm_for_each_connector_iter(tmp_connector, &conn_iter) {
		if (tmp_connector->state->crtc != crtc)
			continue;

		if (tmp_connector->dpms == DRM_MODE_DPMS_ON) {
			active = true;
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);
	crtc_state->active = active;

	ret = drm_atomic_commit(state);
fail:
	if (ret == -EDEADLK)
		goto backoff;
	if (ret != 0)
		connector->dpms = old_mode;
	drm_atomic_state_put(state);
	return ret;

backoff:
	drm_atomic_state_clear(state);
	drm_atomic_legacy_backoff(state);

	goto retry;
}
EXPORT_SYMBOL(drm_atomic_helper_connector_dpms);

/**
 * drm_atomic_helper_best_encoder - Helper for
 * 	&drm_connector_helper_funcs.best_encoder callback
 * @connector: Connector control structure
 *
 * This is a &drm_connector_helper_funcs.best_encoder callback helper for
 * connectors that support exactly 1 encoder, statically determined at driver
 * init time.
 */
struct drm_encoder *
drm_atomic_helper_best_encoder(struct drm_connector *connector)
{
	WARN_ON(connector->encoder_ids[1]);
	return drm_encoder_find(connector->dev, connector->encoder_ids[0]);
}
EXPORT_SYMBOL(drm_atomic_helper_best_encoder);

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

	if (plane->state) {
		plane->state->plane = plane;
		plane->state->rotation = DRM_MODE_ROTATE_0;
	}
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
 * drm_atomic_helper_duplicate_state - duplicate an atomic state object
 * @dev: DRM device
 * @ctx: lock acquisition context
 *
 * Makes a copy of the current atomic state by looping over all objects and
 * duplicating their respective states. This is used for example by suspend/
 * resume support code to save the state prior to suspend such that it can
 * be restored upon resume.
 *
 * Note that this treats atomic state as persistent between save and restore.
 * Drivers must make sure that this is possible and won't result in confusion
 * or erroneous behaviour.
 *
 * Note that if callers haven't already acquired all modeset locks this might
 * return -EDEADLK, which must be handled by calling drm_modeset_backoff().
 *
 * Returns:
 * A pointer to the copy of the atomic state object on success or an
 * ERR_PTR()-encoded error code on failure.
 *
 * See also:
 * drm_atomic_helper_suspend(), drm_atomic_helper_resume()
 */
struct drm_atomic_state *
drm_atomic_helper_duplicate_state(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_connector *conn;
	struct drm_connector_list_iter conn_iter;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	int err = 0;

	state = drm_atomic_state_alloc(dev);
	if (!state)
		return ERR_PTR(-ENOMEM);

	state->acquire_ctx = ctx;

	drm_for_each_crtc(crtc, dev) {
		struct drm_crtc_state *crtc_state;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state)) {
			err = PTR_ERR(crtc_state);
			goto free;
		}
	}

	drm_for_each_plane(plane, dev) {
		struct drm_plane_state *plane_state;

		plane_state = drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(plane_state)) {
			err = PTR_ERR(plane_state);
			goto free;
		}
	}

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(conn, &conn_iter) {
		struct drm_connector_state *conn_state;

		conn_state = drm_atomic_get_connector_state(state, conn);
		if (IS_ERR(conn_state)) {
			err = PTR_ERR(conn_state);
			drm_connector_list_iter_end(&conn_iter);
			goto free;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	/* clear the acquire context so that it isn't accidentally reused */
	state->acquire_ctx = NULL;

free:
	if (err < 0) {
		drm_atomic_state_put(state);
		state = ERR_PTR(err);
	}

	return state;
}
EXPORT_SYMBOL(drm_atomic_helper_duplicate_state);

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
 * drm_atomic_helper_legacy_gamma_set - set the legacy gamma correction table
 * @crtc: CRTC object
 * @red: red correction table
 * @green: green correction table
 * @blue: green correction table
 * @size: size of the tables
 * @ctx: lock acquire context
 *
 * Implements support for legacy gamma correction table for drivers
 * that support color management through the DEGAMMA_LUT/GAMMA_LUT
 * properties. See drm_crtc_enable_color_mgmt() and the containing chapter for
 * how the atomic color management and gamma tables work.
 */
int drm_atomic_helper_legacy_gamma_set(struct drm_crtc *crtc,
				       u16 *red, u16 *green, u16 *blue,
				       uint32_t size,
				       struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_device *dev = crtc->dev;
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	struct drm_property_blob *blob = NULL;
	struct drm_color_lut *blob_data;
	int i, ret = 0;

	state = drm_atomic_state_alloc(crtc->dev);
	if (!state)
		return -ENOMEM;

	blob = drm_property_create_blob(dev,
					sizeof(struct drm_color_lut) * size,
					NULL);
	if (IS_ERR(blob)) {
		ret = PTR_ERR(blob);
		blob = NULL;
		goto fail;
	}

	/* Prepare GAMMA_LUT with the legacy values. */
	blob_data = (struct drm_color_lut *) blob->data;
	for (i = 0; i < size; i++) {
		blob_data[i].red = red[i];
		blob_data[i].green = green[i];
		blob_data[i].blue = blue[i];
	}

	state->acquire_ctx = ctx;
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto fail;
	}

	/* Reset DEGAMMA_LUT and CTM properties. */
	ret = drm_atomic_crtc_set_property(crtc, crtc_state,
			config->degamma_lut_property, 0);
	if (ret)
		goto fail;

	ret = drm_atomic_crtc_set_property(crtc, crtc_state,
			config->ctm_property, 0);
	if (ret)
		goto fail;

	ret = drm_atomic_crtc_set_property(crtc, crtc_state,
			config->gamma_lut_property, blob->base.id);
	if (ret)
		goto fail;

	ret = drm_atomic_commit(state);

fail:
	drm_atomic_state_put(state);
	drm_property_blob_put(blob);
	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_legacy_gamma_set);
