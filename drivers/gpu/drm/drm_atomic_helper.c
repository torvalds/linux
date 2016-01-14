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
#include <linux/fence.h>

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
 */
static void
drm_atomic_helper_plane_changed(struct drm_atomic_state *state,
				struct drm_plane_state *plane_state,
				struct drm_plane *plane)
{
	struct drm_crtc_state *crtc_state;

	if (plane->state->crtc) {
		crtc_state = state->crtc_states[drm_crtc_index(plane->state->crtc)];

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

static struct drm_crtc *
get_current_crtc_for_encoder(struct drm_device *dev,
			     struct drm_encoder *encoder)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_connector *connector;

	WARN_ON(!drm_modeset_is_locked(&config->connection_mutex));

	drm_for_each_connector(connector, dev) {
		if (connector->state->best_encoder != encoder)
			continue;

		return connector->state->crtc;
	}

	return NULL;
}

static int
steal_encoder(struct drm_atomic_state *state,
	      struct drm_encoder *encoder,
	      struct drm_crtc *encoder_crtc)
{
	struct drm_mode_config *config = &state->dev->mode_config;
	struct drm_crtc_state *crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	int ret;

	/*
	 * We can only steal an encoder coming from a connector, which means we
	 * must already hold the connection_mutex.
	 */
	WARN_ON(!drm_modeset_is_locked(&config->connection_mutex));

	DRM_DEBUG_ATOMIC("[ENCODER:%d:%s] in use on [CRTC:%d], stealing it\n",
			 encoder->base.id, encoder->name,
			 encoder_crtc->base.id);

	crtc_state = drm_atomic_get_crtc_state(state, encoder_crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	crtc_state->connectors_changed = true;

	list_for_each_entry(connector, &config->connector_list, head) {
		if (connector->state->best_encoder != encoder)
			continue;

		DRM_DEBUG_ATOMIC("Stealing encoder from [CONNECTOR:%d:%s]\n",
				 connector->base.id,
				 connector->name);

		connector_state = drm_atomic_get_connector_state(state,
								 connector);
		if (IS_ERR(connector_state))
			return PTR_ERR(connector_state);

		ret = drm_atomic_set_crtc_for_connector(connector_state, NULL);
		if (ret)
			return ret;
		connector_state->best_encoder = NULL;
	}

	return 0;
}

static int
update_connector_routing(struct drm_atomic_state *state, int conn_idx)
{
	const struct drm_connector_helper_funcs *funcs;
	struct drm_encoder *new_encoder;
	struct drm_crtc *encoder_crtc;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	struct drm_crtc_state *crtc_state;
	int idx, ret;

	connector = state->connectors[conn_idx];
	connector_state = state->connector_states[conn_idx];

	if (!connector)
		return 0;

	DRM_DEBUG_ATOMIC("Updating routing for [CONNECTOR:%d:%s]\n",
			 connector->base.id,
			 connector->name);

	if (connector->state->crtc != connector_state->crtc) {
		if (connector->state->crtc) {
			idx = drm_crtc_index(connector->state->crtc);

			crtc_state = state->crtc_states[idx];
			crtc_state->connectors_changed = true;
		}

		if (connector_state->crtc) {
			idx = drm_crtc_index(connector_state->crtc);

			crtc_state = state->crtc_states[idx];
			crtc_state->connectors_changed = true;
		}
	}

	if (!connector_state->crtc) {
		DRM_DEBUG_ATOMIC("Disabling [CONNECTOR:%d:%s]\n",
				connector->base.id,
				connector->name);

		connector_state->best_encoder = NULL;

		return 0;
	}

	funcs = connector->helper_private;

	if (funcs->atomic_best_encoder)
		new_encoder = funcs->atomic_best_encoder(connector,
							 connector_state);
	else
		new_encoder = funcs->best_encoder(connector);

	if (!new_encoder) {
		DRM_DEBUG_ATOMIC("No suitable encoder found for [CONNECTOR:%d:%s]\n",
				 connector->base.id,
				 connector->name);
		return -EINVAL;
	}

	if (!drm_encoder_crtc_ok(new_encoder, connector_state->crtc)) {
		DRM_DEBUG_ATOMIC("[ENCODER:%d:%s] incompatible with [CRTC:%d]\n",
				 new_encoder->base.id,
				 new_encoder->name,
				 connector_state->crtc->base.id);
		return -EINVAL;
	}

	if (new_encoder == connector_state->best_encoder) {
		DRM_DEBUG_ATOMIC("[CONNECTOR:%d:%s] keeps [ENCODER:%d:%s], now on [CRTC:%d]\n",
				 connector->base.id,
				 connector->name,
				 new_encoder->base.id,
				 new_encoder->name,
				 connector_state->crtc->base.id);

		return 0;
	}

	encoder_crtc = get_current_crtc_for_encoder(state->dev,
						    new_encoder);

	if (encoder_crtc) {
		ret = steal_encoder(state, new_encoder, encoder_crtc);
		if (ret) {
			DRM_DEBUG_ATOMIC("Encoder stealing failed for [CONNECTOR:%d:%s]\n",
					 connector->base.id,
					 connector->name);
			return ret;
		}
	}

	if (WARN_ON(!connector_state->crtc))
		return -EINVAL;

	connector_state->best_encoder = new_encoder;
	idx = drm_crtc_index(connector_state->crtc);

	crtc_state = state->crtc_states[idx];
	crtc_state->connectors_changed = true;

	DRM_DEBUG_ATOMIC("[CONNECTOR:%d:%s] using [ENCODER:%d:%s] on [CRTC:%d]\n",
			 connector->base.id,
			 connector->name,
			 new_encoder->base.id,
			 new_encoder->name,
			 connector_state->crtc->base.id);

	return 0;
}

static int
mode_fixup(struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *conn_state;
	int i;
	bool ret;

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		if (!crtc_state->mode_changed &&
		    !crtc_state->connectors_changed)
			continue;

		drm_mode_copy(&crtc_state->adjusted_mode, &crtc_state->mode);
	}

	for_each_connector_in_state(state, connector, conn_state, i) {
		const struct drm_encoder_helper_funcs *funcs;
		struct drm_encoder *encoder;

		WARN_ON(!!conn_state->best_encoder != !!conn_state->crtc);

		if (!conn_state->crtc || !conn_state->best_encoder)
			continue;

		crtc_state =
			state->crtc_states[drm_crtc_index(conn_state->crtc)];

		/*
		 * Each encoder has at most one connector (since we always steal
		 * it away), so we won't call ->mode_fixup twice.
		 */
		encoder = conn_state->best_encoder;
		funcs = encoder->helper_private;
		if (!funcs)
			continue;

		ret = drm_bridge_mode_fixup(encoder->bridge, &crtc_state->mode,
				&crtc_state->adjusted_mode);
		if (!ret) {
			DRM_DEBUG_ATOMIC("Bridge fixup failed\n");
			return -EINVAL;
		}

		if (funcs->atomic_check) {
			ret = funcs->atomic_check(encoder, crtc_state,
						  conn_state);
			if (ret) {
				DRM_DEBUG_ATOMIC("[ENCODER:%d:%s] check failed\n",
						 encoder->base.id, encoder->name);
				return ret;
			}
		} else if (funcs->mode_fixup) {
			ret = funcs->mode_fixup(encoder, &crtc_state->mode,
						&crtc_state->adjusted_mode);
			if (!ret) {
				DRM_DEBUG_ATOMIC("[ENCODER:%d:%s] fixup failed\n",
						 encoder->base.id, encoder->name);
				return -EINVAL;
			}
		}
	}

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		if (!crtc_state->mode_changed &&
		    !crtc_state->connectors_changed)
			continue;

		funcs = crtc->helper_private;
		if (!funcs->mode_fixup)
			continue;

		ret = funcs->mode_fixup(crtc, &crtc_state->mode,
					&crtc_state->adjusted_mode);
		if (!ret) {
			DRM_DEBUG_ATOMIC("[CRTC:%d] fixup failed\n",
					 crtc->base.id);
			return -EINVAL;
		}
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
 * update and adds any additional connectors needed for full modesets and calls
 * down into ->mode_fixup functions of the driver backend.
 *
 * crtc_state->mode_changed is set when the input mode is changed.
 * crtc_state->connectors_changed is set when a connector is added or
 * removed from the crtc.
 * crtc_state->active_changed is set when crtc_state->active changes,
 * which is used for dpms.
 *
 * IMPORTANT:
 *
 * Drivers which update ->mode_changed (e.g. in their ->atomic_check hooks if a
 * plane update can't be done without a full modeset) _must_ call this function
 * afterwards after that change. It is permitted to call this function multiple
 * times for the same update, e.g. when the ->atomic_check functions depend upon
 * the adjusted dotclock for fifo space allocation and watermark computation.
 *
 * RETURNS
 * Zero for success or -errno
 */
int
drm_atomic_helper_check_modeset(struct drm_device *dev,
				struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	int i, ret;

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		if (!drm_mode_equal(&crtc->state->mode, &crtc_state->mode)) {
			DRM_DEBUG_ATOMIC("[CRTC:%d] mode changed\n",
					 crtc->base.id);
			crtc_state->mode_changed = true;
		}

		if (crtc->state->enable != crtc_state->enable) {
			DRM_DEBUG_ATOMIC("[CRTC:%d] enable changed\n",
					 crtc->base.id);

			/*
			 * For clarity this assignment is done here, but
			 * enable == 0 is only true when there are no
			 * connectors and a NULL mode.
			 *
			 * The other way around is true as well. enable != 0
			 * iff connectors are attached and a mode is set.
			 */
			crtc_state->mode_changed = true;
			crtc_state->connectors_changed = true;
		}
	}

	for_each_connector_in_state(state, connector, connector_state, i) {
		/*
		 * This only sets crtc->mode_changed for routing changes,
		 * drivers must set crtc->mode_changed themselves when connector
		 * properties need to be updated.
		 */
		ret = update_connector_routing(state, i);
		if (ret)
			return ret;
	}

	/*
	 * After all the routing has been prepared we need to add in any
	 * connector which is itself unchanged, but who's crtc changes it's
	 * configuration. This must be done before calling mode_fixup in case a
	 * crtc only changed its mode but has the same set of connectors.
	 */
	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		int num_connectors;

		/*
		 * We must set ->active_changed after walking connectors for
		 * otherwise an update that only changes active would result in
		 * a full modeset because update_connector_routing force that.
		 */
		if (crtc->state->active != crtc_state->active) {
			DRM_DEBUG_ATOMIC("[CRTC:%d] active changed\n",
					 crtc->base.id);
			crtc_state->active_changed = true;
		}

		if (!drm_atomic_crtc_needs_modeset(crtc_state))
			continue;

		DRM_DEBUG_ATOMIC("[CRTC:%d] needs all connectors, enable: %c, active: %c\n",
				 crtc->base.id,
				 crtc_state->enable ? 'y' : 'n',
			      crtc_state->active ? 'y' : 'n');

		ret = drm_atomic_add_affected_connectors(state, crtc);
		if (ret != 0)
			return ret;

		ret = drm_atomic_add_affected_planes(state, crtc);
		if (ret != 0)
			return ret;

		num_connectors = drm_atomic_connectors_for_crtc(state,
								crtc);

		if (crtc_state->enable != !!num_connectors) {
			DRM_DEBUG_ATOMIC("[CRTC:%d] enabled/connectors mismatch\n",
					 crtc->base.id);

			return -EINVAL;
		}
	}

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
 * ->atomic_check hooks provided by the driver.
 *
 * It also sets crtc_state->planes_changed to indicate that a crtc has
 * updated planes.
 *
 * RETURNS
 * Zero for success or -errno
 */
int
drm_atomic_helper_check_planes(struct drm_device *dev,
			       struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	int i, ret = 0;

	for_each_plane_in_state(state, plane, plane_state, i) {
		const struct drm_plane_helper_funcs *funcs;

		funcs = plane->helper_private;

		drm_atomic_helper_plane_changed(state, plane_state, plane);

		if (!funcs || !funcs->atomic_check)
			continue;

		ret = funcs->atomic_check(plane, plane_state);
		if (ret) {
			DRM_DEBUG_ATOMIC("[PLANE:%d] atomic driver check failed\n",
					 plane->base.id);
			return ret;
		}
	}

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		funcs = crtc->helper_private;

		if (!funcs || !funcs->atomic_check)
			continue;

		ret = funcs->atomic_check(crtc, state->crtc_states[i]);
		if (ret) {
			DRM_DEBUG_ATOMIC("[CRTC:%d] atomic driver check failed\n",
					 crtc->base.id);
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
 * Drivers without such needs can directly use this as their ->atomic_check()
 * callback.
 *
 * This just wraps the two parts of the state checking for planes and modeset
 * state in the default order: First it calls drm_atomic_helper_check_modeset()
 * and then drm_atomic_helper_check_planes(). The assumption is that the
 * ->atomic_check functions depend upon an updated adjusted_mode.clock to
 * e.g. properly compute watermarks.
 *
 * RETURNS
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

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_check);

static void
disable_outputs(struct drm_device *dev, struct drm_atomic_state *old_state)
{
	struct drm_connector *connector;
	struct drm_connector_state *old_conn_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	int i;

	for_each_connector_in_state(old_state, connector, old_conn_state, i) {
		const struct drm_encoder_helper_funcs *funcs;
		struct drm_encoder *encoder;
		struct drm_crtc_state *old_crtc_state;

		/* Shut down everything that's in the changeset and currently
		 * still on. So need to check the old, saved state. */
		if (!old_conn_state->crtc)
			continue;

		old_crtc_state = old_state->crtc_states[drm_crtc_index(old_conn_state->crtc)];

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
		if (connector->state->crtc && funcs->prepare)
			funcs->prepare(encoder);
		else if (funcs->disable)
			funcs->disable(encoder);
		else
			funcs->dpms(encoder, DRM_MODE_DPMS_OFF);

		drm_bridge_post_disable(encoder->bridge);
	}

	for_each_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		/* Shut down everything that needs a full modeset. */
		if (!drm_atomic_crtc_needs_modeset(crtc->state))
			continue;

		if (!old_crtc_state->active)
			continue;

		funcs = crtc->helper_private;

		DRM_DEBUG_ATOMIC("disabling [CRTC:%d]\n",
				 crtc->base.id);


		/* Right function depends upon target state. */
		if (crtc->state->enable && funcs->prepare)
			funcs->prepare(crtc);
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
	struct drm_connector_state *old_conn_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	int i;

	/* clear out existing links and update dpms */
	for_each_connector_in_state(old_state, connector, old_conn_state, i) {
		if (connector->encoder) {
			WARN_ON(!connector->encoder->crtc);

			connector->encoder->crtc = NULL;
			connector->encoder = NULL;
		}

		crtc = connector->state->crtc;
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
	for_each_connector_in_state(old_state, connector, old_conn_state, i) {
		if (!connector->state->crtc)
			continue;

		if (WARN_ON(!connector->state->best_encoder))
			continue;

		connector->encoder = connector->state->best_encoder;
		connector->encoder->crtc = connector->state->crtc;
	}

	/* set legacy state in the crtc structure */
	for_each_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		struct drm_plane *primary = crtc->primary;

		crtc->mode = crtc->state->mode;
		crtc->enabled = crtc->state->enable;

		if (drm_atomic_get_existing_plane_state(old_state, primary) &&
		    primary->state->crtc == crtc) {
			crtc->x = primary->state->src_x >> 16;
			crtc->y = primary->state->src_y >> 16;
		}

		if (crtc->state->enable)
			drm_calc_timestamping_constants(crtc,
							&crtc->state->adjusted_mode);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_update_legacy_modeset_state);

static void
crtc_set_mode(struct drm_device *dev, struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *old_conn_state;
	int i;

	for_each_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		if (!crtc->state->mode_changed)
			continue;

		funcs = crtc->helper_private;

		if (crtc->state->enable && funcs->mode_set_nofb) {
			DRM_DEBUG_ATOMIC("modeset on [CRTC:%d]\n",
					 crtc->base.id);

			funcs->mode_set_nofb(crtc);
		}
	}

	for_each_connector_in_state(old_state, connector, old_conn_state, i) {
		const struct drm_encoder_helper_funcs *funcs;
		struct drm_crtc_state *new_crtc_state;
		struct drm_encoder *encoder;
		struct drm_display_mode *mode, *adjusted_mode;

		if (!connector->state->best_encoder)
			continue;

		encoder = connector->state->best_encoder;
		funcs = encoder->helper_private;
		new_crtc_state = connector->state->crtc->state;
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
		if (funcs->mode_set)
			funcs->mode_set(encoder, mode, adjusted_mode);

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
	struct drm_connector *connector;
	struct drm_connector_state *old_conn_state;
	int i;

	for_each_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		/* Need to filter out CRTCs where only planes change. */
		if (!drm_atomic_crtc_needs_modeset(crtc->state))
			continue;

		if (!crtc->state->active)
			continue;

		funcs = crtc->helper_private;

		if (crtc->state->enable) {
			DRM_DEBUG_ATOMIC("enabling [CRTC:%d]\n",
					 crtc->base.id);

			if (funcs->enable)
				funcs->enable(crtc);
			else
				funcs->commit(crtc);
		}
	}

	for_each_connector_in_state(old_state, connector, old_conn_state, i) {
		const struct drm_encoder_helper_funcs *funcs;
		struct drm_encoder *encoder;

		if (!connector->state->best_encoder)
			continue;

		if (!connector->state->crtc->state->active ||
		    !drm_atomic_crtc_needs_modeset(connector->state->crtc->state))
			continue;

		encoder = connector->state->best_encoder;
		funcs = encoder->helper_private;

		DRM_DEBUG_ATOMIC("enabling [ENCODER:%d:%s]\n",
				 encoder->base.id, encoder->name);

		/*
		 * Each encoder has at most one connector (since we always steal
		 * it away), so we won't call enable hooks twice.
		 */
		drm_bridge_pre_enable(encoder->bridge);

		if (funcs->enable)
			funcs->enable(encoder);
		else
			funcs->commit(encoder);

		drm_bridge_enable(encoder->bridge);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_commit_modeset_enables);

static void wait_for_fences(struct drm_device *dev,
			    struct drm_atomic_state *state)
{
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	int i;

	for_each_plane_in_state(state, plane, plane_state, i) {
		if (!plane->state->fence)
			continue;

		WARN_ON(!plane->state->fb);

		fence_wait(plane->state->fence, false);
		fence_put(plane->state->fence);
		plane->state->fence = NULL;
	}
}

static bool framebuffer_changed(struct drm_device *dev,
				struct drm_atomic_state *old_state,
				struct drm_crtc *crtc)
{
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state;
	int i;

	for_each_plane_in_state(old_state, plane, old_plane_state, i) {
		if (plane->state->crtc != crtc &&
		    old_plane_state->crtc != crtc)
			continue;

		if (plane->state->fb != old_plane_state->fb)
			return true;
	}

	return false;
}

/**
 * drm_atomic_helper_wait_for_vblanks - wait for vblank on crtcs
 * @dev: DRM device
 * @old_state: atomic state object with old state structures
 *
 * Helper to, after atomic commit, wait for vblanks on all effected
 * crtcs (ie. before cleaning up old framebuffers using
 * drm_atomic_helper_cleanup_planes()). It will only wait on crtcs where the
 * framebuffers have actually changed to optimize for the legacy cursor and
 * plane update use-case.
 */
void
drm_atomic_helper_wait_for_vblanks(struct drm_device *dev,
		struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	int i, ret;

	for_each_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		/* No one cares about the old state, so abuse it for tracking
		 * and store whether we hold a vblank reference (and should do a
		 * vblank wait) in the ->enable boolean. */
		old_crtc_state->enable = false;

		if (!crtc->state->enable)
			continue;

		/* Legacy cursor ioctls are completely unsynced, and userspace
		 * relies on that (by doing tons of cursor updates). */
		if (old_state->legacy_cursor_update)
			continue;

		if (!framebuffer_changed(dev, old_state, crtc))
			continue;

		ret = drm_crtc_vblank_get(crtc);
		if (ret != 0)
			continue;

		old_crtc_state->enable = true;
		old_crtc_state->last_vblank_count = drm_crtc_vblank_count(crtc);
	}

	for_each_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		if (!old_crtc_state->enable)
			continue;

		ret = wait_event_timeout(dev->vblank[i].queue,
				old_crtc_state->last_vblank_count !=
					drm_crtc_vblank_count(crtc),
				msecs_to_jiffies(50));

		drm_crtc_vblank_put(crtc);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_wait_for_vblanks);

/**
 * drm_atomic_helper_commit - commit validated state object
 * @dev: DRM device
 * @state: the driver state object
 * @async: asynchronous commit
 *
 * This function commits a with drm_atomic_helper_check() pre-validated state
 * object. This can still fail when e.g. the framebuffer reservation fails. For
 * now this doesn't implement asynchronous commits.
 *
 * Note that right now this function does not support async commits, and hence
 * driver writers must implement their own version for now. Also note that the
 * default ordering of how the various stages are called is to match the legacy
 * modeset helper library closest. One peculiarity of that is that it doesn't
 * mesh well with runtime PM at all.
 *
 * For drivers supporting runtime PM the recommended sequence is
 *
 *     drm_atomic_helper_commit_modeset_disables(dev, state);
 *
 *     drm_atomic_helper_commit_modeset_enables(dev, state);
 *
 *     drm_atomic_helper_commit_planes(dev, state, true);
 *
 * See the kerneldoc entries for these three functions for more details.
 *
 * RETURNS
 * Zero for success or -errno.
 */
int drm_atomic_helper_commit(struct drm_device *dev,
			     struct drm_atomic_state *state,
			     bool async)
{
	int ret;

	if (async)
		return -EBUSY;

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret)
		return ret;

	/*
	 * This is the point of no return - everything below never fails except
	 * when the hw goes bonghits. Which means we can commit the new state on
	 * the software side now.
	 */

	drm_atomic_helper_swap_state(dev, state);

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
	 */

	wait_for_fences(dev, state);

	drm_atomic_helper_commit_modeset_disables(dev, state);

	drm_atomic_helper_commit_planes(dev, state, false);

	drm_atomic_helper_commit_modeset_enables(dev, state);

	drm_atomic_helper_wait_for_vblanks(dev, state);

	drm_atomic_helper_cleanup_planes(dev, state);

	drm_atomic_state_free(state);

	return 0;
}
EXPORT_SYMBOL(drm_atomic_helper_commit);

/**
 * DOC: implementing async commit
 *
 * For now the atomic helpers don't support async commit directly. If there is
 * real need it could be added though, using the dma-buf fence infrastructure
 * for generic synchronization with outstanding rendering.
 *
 * For now drivers have to implement async commit themselves, with the following
 * sequence being the recommended one:
 *
 * 1. Run drm_atomic_helper_prepare_planes() first. This is the only function
 * which commit needs to call which can fail, so we want to run it first and
 * synchronously.
 *
 * 2. Synchronize with any outstanding asynchronous commit worker threads which
 * might be affected the new state update. This can be done by either cancelling
 * or flushing the work items, depending upon whether the driver can deal with
 * cancelled updates. Note that it is important to ensure that the framebuffer
 * cleanup is still done when cancelling.
 *
 * For sufficient parallelism it is recommended to have a work item per crtc
 * (for updates which don't touch global state) and a global one. Then we only
 * need to synchronize with the crtc work items for changed crtcs and the global
 * work item, which allows nice concurrent updates on disjoint sets of crtcs.
 *
 * 3. The software state is updated synchronously with
 * drm_atomic_helper_swap_state(). Doing this under the protection of all modeset
 * locks means concurrent callers never see inconsistent state. And doing this
 * while it's guaranteed that no relevant async worker runs means that async
 * workers do not need grab any locks. Actually they must not grab locks, for
 * otherwise the work flushing will deadlock.
 *
 * 4. Schedule a work item to do all subsequent steps, using the split-out
 * commit helpers: a) pre-plane commit b) plane commit c) post-plane commit and
 * then cleaning up the framebuffers after the old framebuffer is no longer
 * being displayed.
 */

/**
 * drm_atomic_helper_prepare_planes - prepare plane resources before commit
 * @dev: DRM device
 * @state: atomic state object with new state structures
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
		const struct drm_plane_helper_funcs *funcs;
		struct drm_plane *plane = state->planes[i];
		struct drm_plane_state *plane_state = state->plane_states[i];

		if (!plane)
			continue;

		funcs = plane->helper_private;

		if (funcs->prepare_fb) {
			ret = funcs->prepare_fb(plane, plane_state);
			if (ret)
				goto fail;
		}
	}

	return 0;

fail:
	for (i--; i >= 0; i--) {
		const struct drm_plane_helper_funcs *funcs;
		struct drm_plane *plane = state->planes[i];
		struct drm_plane_state *plane_state = state->plane_states[i];

		if (!plane)
			continue;

		funcs = plane->helper_private;

		if (funcs->cleanup_fb)
			funcs->cleanup_fb(plane, plane_state);

	}

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_prepare_planes);

bool plane_crtc_active(struct drm_plane_state *state)
{
	return state->crtc && state->crtc->state->active;
}

/**
 * drm_atomic_helper_commit_planes - commit plane state
 * @dev: DRM device
 * @old_state: atomic state object with old state structures
 * @active_only: Only commit on active CRTC if set
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
 * Unless otherwise needed, drivers are advised to set the @active_only
 * parameters to true in order not to receive plane update notifications related
 * to a disabled CRTC. This avoids the need to manually ignore plane updates in
 * driver code when the driver and/or hardware can't or just don't need to deal
 * with updates on disabled CRTCs, for example when supporting runtime PM.
 *
 * The drm_atomic_helper_commit() default implementation only sets @active_only
 * to false to most closely match the behaviour of the legacy helpers. This should
 * not be copied blindly by drivers.
 */
void drm_atomic_helper_commit_planes(struct drm_device *dev,
				     struct drm_atomic_state *old_state,
				     bool active_only)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state;
	int i;

	for_each_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		funcs = crtc->helper_private;

		if (!funcs || !funcs->atomic_begin)
			continue;

		if (active_only && !crtc->state->active)
			continue;

		funcs->atomic_begin(crtc, old_crtc_state);
	}

	for_each_plane_in_state(old_state, plane, old_plane_state, i) {
		const struct drm_plane_helper_funcs *funcs;
		bool disabling;

		funcs = plane->helper_private;

		if (!funcs)
			continue;

		disabling = drm_atomic_plane_disabling(plane, old_plane_state);

		if (active_only) {
			/*
			 * Skip planes related to inactive CRTCs. If the plane
			 * is enabled use the state of the current CRTC. If the
			 * plane is being disabled use the state of the old
			 * CRTC to avoid skipping planes being disabled on an
			 * active CRTC.
			 */
			if (!disabling && !plane_crtc_active(plane->state))
				continue;
			if (disabling && !plane_crtc_active(old_plane_state))
				continue;
		}

		/*
		 * Special-case disabling the plane if drivers support it.
		 */
		if (disabling && funcs->atomic_disable)
			funcs->atomic_disable(plane, old_plane_state);
		else if (plane->state->crtc || disabling)
			funcs->atomic_update(plane, old_plane_state);
	}

	for_each_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		funcs = crtc->helper_private;

		if (!funcs || !funcs->atomic_flush)
			continue;

		if (active_only && !crtc->state->active)
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
			drm_atomic_get_existing_plane_state(old_state, plane);
		const struct drm_plane_helper_funcs *plane_funcs;

		plane_funcs = plane->helper_private;

		if (!old_plane_state || !plane_funcs)
			continue;

		WARN_ON(plane->state->crtc && plane->state->crtc != crtc);

		if (drm_atomic_plane_disabling(plane, old_plane_state) &&
		    plane_funcs->atomic_disable)
			plane_funcs->atomic_disable(plane, old_plane_state);
		else if (plane->state->crtc ||
			 drm_atomic_plane_disabling(plane, old_plane_state))
			plane_funcs->atomic_update(plane, old_plane_state);
	}

	if (crtc_funcs && crtc_funcs->atomic_flush)
		crtc_funcs->atomic_flush(crtc, old_crtc_state);
}
EXPORT_SYMBOL(drm_atomic_helper_commit_planes_on_crtc);

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
	struct drm_plane_state *plane_state;
	int i;

	for_each_plane_in_state(old_state, plane, plane_state, i) {
		const struct drm_plane_helper_funcs *funcs;

		funcs = plane->helper_private;

		if (funcs->cleanup_fb)
			funcs->cleanup_fb(plane, plane_state);
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
 * 5. Call drm_atomic_helper_cleanup_planes() with @state, which since step 3
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
				   uint32_t src_w, uint32_t src_h)
{
	struct drm_atomic_state *state;
	struct drm_plane_state *plane_state;
	int ret = 0;

	state = drm_atomic_state_alloc(plane->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = drm_modeset_legacy_acquire_ctx(crtc);
retry:
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
	plane_state->crtc_h = crtc_h;
	plane_state->crtc_w = crtc_w;
	plane_state->src_x = src_x;
	plane_state->src_y = src_y;
	plane_state->src_h = src_h;
	plane_state->src_w = src_w;

	if (plane == crtc->cursor)
		state->legacy_cursor_update = true;

	ret = drm_atomic_commit(state);
	if (ret != 0)
		goto fail;

	/* Driver takes ownership of state on successful commit. */
	return 0;
fail:
	if (ret == -EDEADLK)
		goto backoff;

	drm_atomic_state_free(state);

	return ret;
backoff:
	drm_atomic_state_clear(state);
	drm_atomic_legacy_backoff(state);

	/*
	 * Someone might have exchanged the framebuffer while we dropped locks
	 * in the backoff code. We need to fix up the fb refcount tracking the
	 * core does for us.
	 */
	plane->old_fb = plane->fb;

	goto retry;
}
EXPORT_SYMBOL(drm_atomic_helper_update_plane);

/**
 * drm_atomic_helper_disable_plane - Helper for primary plane disable using * atomic
 * @plane: plane to disable
 *
 * Provides a default plane disable handler using the atomic driver interface.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
int drm_atomic_helper_disable_plane(struct drm_plane *plane)
{
	struct drm_atomic_state *state;
	struct drm_plane_state *plane_state;
	int ret = 0;

	/*
	 * FIXME: Without plane->crtc set we can't get at the implicit legacy
	 * acquire context. The real fix will be to wire the acquire ctx through
	 * everywhere we need it, but meanwhile prevent chaos by just skipping
	 * this noop. The critical case is the cursor ioctls which a) only grab
	 * crtc/cursor-plane locks (so we need the crtc to get at the right
	 * acquire context) and b) can try to disable the plane multiple times.
	 */
	if (!plane->crtc)
		return 0;

	state = drm_atomic_state_alloc(plane->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = drm_modeset_legacy_acquire_ctx(plane->crtc);
retry:
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
	if (ret != 0)
		goto fail;

	/* Driver takes ownership of state on successful commit. */
	return 0;
fail:
	if (ret == -EDEADLK)
		goto backoff;

	drm_atomic_state_free(state);

	return ret;
backoff:
	drm_atomic_state_clear(state);
	drm_atomic_legacy_backoff(state);

	/*
	 * Someone might have exchanged the framebuffer while we dropped locks
	 * in the backoff code. We need to fix up the fb refcount tracking the
	 * core does for us.
	 */
	plane->old_fb = plane->fb;

	goto retry;
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
	plane_state->crtc_h = 0;
	plane_state->crtc_w = 0;
	plane_state->src_x = 0;
	plane_state->src_y = 0;
	plane_state->src_h = 0;
	plane_state->src_w = 0;

	return 0;
}

static int update_output_state(struct drm_atomic_state *state,
			       struct drm_mode_set *set)
{
	struct drm_device *dev = set->crtc->dev;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *conn_state;
	int ret, i, j;

	ret = drm_modeset_lock(&dev->mode_config.connection_mutex,
			       state->acquire_ctx);
	if (ret)
		return ret;

	/* First grab all affected connector/crtc states. */
	for (i = 0; i < set->num_connectors; i++) {
		conn_state = drm_atomic_get_connector_state(state,
							    set->connectors[i]);
		if (IS_ERR(conn_state))
			return PTR_ERR(conn_state);
	}

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		ret = drm_atomic_add_affected_connectors(state, crtc);
		if (ret)
			return ret;
	}

	/* Then recompute connector->crtc links and crtc enabling state. */
	for_each_connector_in_state(state, connector, conn_state, i) {
		if (conn_state->crtc == set->crtc) {
			ret = drm_atomic_set_crtc_for_connector(conn_state,
								NULL);
			if (ret)
				return ret;
		}

		for (j = 0; j < set->num_connectors; j++) {
			if (set->connectors[j] == connector) {
				ret = drm_atomic_set_crtc_for_connector(conn_state,
									set->crtc);
				if (ret)
					return ret;
				break;
			}
		}
	}

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		/* Don't update ->enable for the CRTC in the set_config request,
		 * since a mismatch would indicate a bug in the upper layers.
		 * The actual modeset code later on will catch any
		 * inconsistencies here. */
		if (crtc == set->crtc)
			continue;

		if (!drm_atomic_connectors_for_crtc(state, crtc)) {
			ret = drm_atomic_set_mode_prop_for_crtc(crtc_state,
								NULL);
			if (ret < 0)
				return ret;

			crtc_state->active = false;
		}
	}

	return 0;
}

/**
 * drm_atomic_helper_set_config - set a new config from userspace
 * @set: mode set configuration
 *
 * Provides a default crtc set_config handler using the atomic driver interface.
 *
 * Returns:
 * Returns 0 on success, negative errno numbers on failure.
 */
int drm_atomic_helper_set_config(struct drm_mode_set *set)
{
	struct drm_atomic_state *state;
	struct drm_crtc *crtc = set->crtc;
	int ret = 0;

	state = drm_atomic_state_alloc(crtc->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = drm_modeset_legacy_acquire_ctx(crtc);
retry:
	ret = __drm_atomic_helper_set_config(set, state);
	if (ret != 0)
		goto fail;

	ret = drm_atomic_commit(state);
	if (ret != 0)
		goto fail;

	/* Driver takes ownership of state on successful commit. */
	return 0;
fail:
	if (ret == -EDEADLK)
		goto backoff;

	drm_atomic_state_free(state);

	return ret;
backoff:
	drm_atomic_state_clear(state);
	drm_atomic_legacy_backoff(state);

	/*
	 * Someone might have exchanged the framebuffer while we dropped locks
	 * in the backoff code. We need to fix up the fb refcount tracking the
	 * core does for us.
	 */
	crtc->primary->old_fb = crtc->primary->fb;

	goto retry;
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

	drm_crtc_get_hv_timing(set->mode, &hdisplay, &vdisplay);

	drm_atomic_set_fb_for_plane(primary_state, set->fb);
	primary_state->crtc_x = 0;
	primary_state->crtc_y = 0;
	primary_state->crtc_h = vdisplay;
	primary_state->crtc_w = hdisplay;
	primary_state->src_x = set->x << 16;
	primary_state->src_y = set->y << 16;
	if (primary_state->rotation & (BIT(DRM_ROTATE_90) | BIT(DRM_ROTATE_270))) {
		primary_state->src_h = hdisplay << 16;
		primary_state->src_w = vdisplay << 16;
	} else {
		primary_state->src_h = vdisplay << 16;
		primary_state->src_w = hdisplay << 16;
	}

commit:
	ret = update_output_state(state, set);
	if (ret)
		return ret;

	return 0;
}

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
	if (ret != 0)
		goto fail;

	/* Driver takes ownership of state on successful commit. */
	return 0;
fail:
	if (ret == -EDEADLK)
		goto backoff;

	drm_atomic_state_free(state);

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
	if (ret != 0)
		goto fail;

	/* Driver takes ownership of state on successful commit. */
	return 0;
fail:
	if (ret == -EDEADLK)
		goto backoff;

	drm_atomic_state_free(state);

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
	if (ret != 0)
		goto fail;

	/* Driver takes ownership of state on successful commit. */
	return 0;
fail:
	if (ret == -EDEADLK)
		goto backoff;

	drm_atomic_state_free(state);

	return ret;
backoff:
	drm_atomic_state_clear(state);
	drm_atomic_legacy_backoff(state);

	goto retry;
}
EXPORT_SYMBOL(drm_atomic_helper_connector_set_property);

/**
 * drm_atomic_helper_page_flip - execute a legacy page flip
 * @crtc: DRM crtc
 * @fb: DRM framebuffer
 * @event: optional DRM event to signal upon completion
 * @flags: flip flags for non-vblank sync'ed updates
 *
 * Provides a default page flip implementation using the atomic driver interface.
 *
 * Note that for now so called async page flips (i.e. updates which are not
 * synchronized to vblank) are not supported, since the atomic interfaces have
 * no provisions for this yet.
 *
 * Returns:
 * Returns 0 on success, negative errno numbers on failure.
 */
int drm_atomic_helper_page_flip(struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				struct drm_pending_vblank_event *event,
				uint32_t flags)
{
	struct drm_plane *plane = crtc->primary;
	struct drm_atomic_state *state;
	struct drm_plane_state *plane_state;
	struct drm_crtc_state *crtc_state;
	int ret = 0;

	if (flags & DRM_MODE_PAGE_FLIP_ASYNC)
		return -EINVAL;

	state = drm_atomic_state_alloc(plane->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = drm_modeset_legacy_acquire_ctx(crtc);
retry:
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto fail;
	}
	crtc_state->event = event;

	plane_state = drm_atomic_get_plane_state(state, plane);
	if (IS_ERR(plane_state)) {
		ret = PTR_ERR(plane_state);
		goto fail;
	}

	ret = drm_atomic_set_crtc_for_plane(plane_state, crtc);
	if (ret != 0)
		goto fail;
	drm_atomic_set_fb_for_plane(plane_state, fb);

	ret = drm_atomic_async_commit(state);
	if (ret != 0)
		goto fail;

	/* Driver takes ownership of state on successful async commit. */
	return 0;
fail:
	if (ret == -EDEADLK)
		goto backoff;

	drm_atomic_state_free(state);

	return ret;
backoff:
	drm_atomic_state_clear(state);
	drm_atomic_legacy_backoff(state);

	/*
	 * Someone might have exchanged the framebuffer while we dropped locks
	 * in the backoff code. We need to fix up the fb refcount tracking the
	 * core does for us.
	 */
	plane->old_fb = plane->fb;

	goto retry;
}
EXPORT_SYMBOL(drm_atomic_helper_page_flip);

/**
 * drm_atomic_helper_connector_dpms() - connector dpms helper implementation
 * @connector: affected connector
 * @mode: DPMS mode
 *
 * This is the main helper function provided by the atomic helper framework for
 * implementing the legacy DPMS connector interface. It computes the new desired
 * ->active state for the corresponding CRTC (if the connector is enabled) and
 *  updates it.
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

	state->acquire_ctx = drm_modeset_legacy_acquire_ctx(crtc);
retry:
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto fail;
	}

	WARN_ON(!drm_modeset_is_locked(&config->connection_mutex));

	drm_for_each_connector(tmp_connector, connector->dev) {
		if (tmp_connector->state->crtc != crtc)
			continue;

		if (tmp_connector->dpms == DRM_MODE_DPMS_ON) {
			active = true;
			break;
		}
	}
	crtc_state->active = active;

	ret = drm_atomic_commit(state);
	if (ret != 0)
		goto fail;

	/* Driver takes ownership of state on successful commit. */
	return 0;
fail:
	if (ret == -EDEADLK)
		goto backoff;

	connector->dpms = old_mode;
	drm_atomic_state_free(state);

	return ret;
backoff:
	drm_atomic_state_clear(state);
	drm_atomic_legacy_backoff(state);

	goto retry;
}
EXPORT_SYMBOL(drm_atomic_helper_connector_dpms);

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
 */

/**
 * drm_atomic_helper_crtc_reset - default ->reset hook for CRTCs
 * @crtc: drm CRTC
 *
 * Resets the atomic state for @crtc by freeing the state pointer (which might
 * be NULL, e.g. at driver load time) and allocating a new empty state object.
 */
void drm_atomic_helper_crtc_reset(struct drm_crtc *crtc)
{
	if (crtc->state && crtc->state->mode_blob)
		drm_property_unreference_blob(crtc->state->mode_blob);
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
		drm_property_reference_blob(state->mode_blob);
	state->mode_changed = false;
	state->active_changed = false;
	state->planes_changed = false;
	state->connectors_changed = false;
	state->event = NULL;
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
 * @crtc: CRTC object
 * @state: CRTC state object to release
 *
 * Releases all resources stored in the CRTC state without actually freeing
 * the memory of the CRTC state. This is useful for drivers that subclass the
 * CRTC state.
 */
void __drm_atomic_helper_crtc_destroy_state(struct drm_crtc *crtc,
					    struct drm_crtc_state *state)
{
	if (state->mode_blob)
		drm_property_unreference_blob(state->mode_blob);
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
	__drm_atomic_helper_crtc_destroy_state(crtc, state);
	kfree(state);
}
EXPORT_SYMBOL(drm_atomic_helper_crtc_destroy_state);

/**
 * drm_atomic_helper_plane_reset - default ->reset hook for planes
 * @plane: drm plane
 *
 * Resets the atomic state for @plane by freeing the state pointer (which might
 * be NULL, e.g. at driver load time) and allocating a new empty state object.
 */
void drm_atomic_helper_plane_reset(struct drm_plane *plane)
{
	if (plane->state && plane->state->fb)
		drm_framebuffer_unreference(plane->state->fb);

	kfree(plane->state);
	plane->state = kzalloc(sizeof(*plane->state), GFP_KERNEL);

	if (plane->state)
		plane->state->plane = plane;
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
		drm_framebuffer_reference(state->fb);
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
 * @plane: plane object
 * @state: plane state object to release
 *
 * Releases all resources stored in the plane state without actually freeing
 * the memory of the plane state. This is useful for drivers that subclass the
 * plane state.
 */
void __drm_atomic_helper_plane_destroy_state(struct drm_plane *plane,
					     struct drm_plane_state *state)
{
	if (state->fb)
		drm_framebuffer_unreference(state->fb);
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
	__drm_atomic_helper_plane_destroy_state(plane, state);
	kfree(state);
}
EXPORT_SYMBOL(drm_atomic_helper_plane_destroy_state);

/**
 * drm_atomic_helper_connector_reset - default ->reset hook for connectors
 * @connector: drm connector
 *
 * Resets the atomic state for @connector by freeing the state pointer (which
 * might be NULL, e.g. at driver load time) and allocating a new empty state
 * object.
 */
void drm_atomic_helper_connector_reset(struct drm_connector *connector)
{
	kfree(connector->state);
	connector->state = kzalloc(sizeof(*connector->state), GFP_KERNEL);

	if (connector->state)
		connector->state->connector = connector;
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
 * duplicating their respective states.
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
 */
struct drm_atomic_state *
drm_atomic_helper_duplicate_state(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_connector *conn;
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

	drm_for_each_connector(conn, dev) {
		struct drm_connector_state *conn_state;

		conn_state = drm_atomic_get_connector_state(state, conn);
		if (IS_ERR(conn_state)) {
			err = PTR_ERR(conn_state);
			goto free;
		}
	}

	/* clear the acquire context so that it isn't accidentally reused */
	state->acquire_ctx = NULL;

free:
	if (err < 0) {
		drm_atomic_state_free(state);
		state = ERR_PTR(err);
	}

	return state;
}
EXPORT_SYMBOL(drm_atomic_helper_duplicate_state);

/**
 * __drm_atomic_helper_connector_destroy_state - release connector state
 * @connector: connector object
 * @state: connector state object to release
 *
 * Releases all resources stored in the connector state without actually
 * freeing the memory of the connector state. This is useful for drivers that
 * subclass the connector state.
 */
void
__drm_atomic_helper_connector_destroy_state(struct drm_connector *connector,
					    struct drm_connector_state *state)
{
	/*
	 * This is currently a placeholder so that drivers that subclass the
	 * state will automatically do the right thing if code is ever added
	 * to this function.
	 */
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
	__drm_atomic_helper_connector_destroy_state(connector, state);
	kfree(state);
}
EXPORT_SYMBOL(drm_atomic_helper_connector_destroy_state);
