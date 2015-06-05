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
 * intel_atomic_check - validate state object
 * @dev: drm device
 * @state: state to validate
 */
int intel_atomic_check(struct drm_device *dev,
		       struct drm_atomic_state *state)
{
	int nplanes = dev->mode_config.num_total_plane;
	int ncrtcs = dev->mode_config.num_crtc;
	int nconnectors = dev->mode_config.num_connector;
	enum pipe nuclear_pipe = INVALID_PIPE;
	int ret;
	int i;
	bool not_nuclear = false;

	/*
	 * FIXME:  At the moment, we only support "nuclear pageflip" on a
	 * single CRTC.  Cross-crtc updates will be added later.
	 */
	for (i = 0; i < nplanes; i++) {
		struct intel_plane *plane = to_intel_plane(state->planes[i]);
		if (!plane)
			continue;

		if (nuclear_pipe == INVALID_PIPE) {
			nuclear_pipe = plane->pipe;
		} else if (nuclear_pipe != plane->pipe) {
			DRM_DEBUG_KMS("i915 only support atomic plane operations on a single CRTC at the moment\n");
			return -EINVAL;
		}
	}

	/*
	 * FIXME:  We only handle planes for now; make sure there are no CRTC's
	 * or connectors involved.
	 */
	state->allow_modeset = false;
	for (i = 0; i < ncrtcs; i++) {
		struct intel_crtc *crtc = to_intel_crtc(state->crtcs[i]);
		if (crtc && crtc->pipe != nuclear_pipe)
			not_nuclear = true;
	}
	for (i = 0; i < nconnectors; i++)
		if (state->connectors[i] != NULL)
			not_nuclear = true;

	if (not_nuclear) {
		DRM_DEBUG_KMS("i915 only supports atomic plane operations at the moment\n");
		return -EINVAL;
	}

	ret = drm_atomic_helper_check_planes(dev, state);
	if (ret)
		return ret;

	return ret;
}


/**
 * intel_atomic_commit - commit validated state object
 * @dev: DRM device
 * @state: the top-level driver state object
 * @async: asynchronous commit
 *
 * This function commits a top-level state object that has been validated
 * with drm_atomic_helper_check().
 *
 * FIXME:  Atomic modeset support for i915 is not yet complete.  At the moment
 * we can only handle plane-related operations and do not yet support
 * asynchronous commit.
 *
 * RETURNS
 * Zero for success or -errno.
 */
int intel_atomic_commit(struct drm_device *dev,
			struct drm_atomic_state *state,
			bool async)
{
	int ret;
	int i;

	if (async) {
		DRM_DEBUG_KMS("i915 does not yet support async commit\n");
		return -EINVAL;
	}

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret)
		return ret;

	/* Point of no return */

	/*
	 * FIXME:  The proper sequence here will eventually be:
	 *
	 * drm_atomic_helper_swap_state(dev, state)
	 * drm_atomic_helper_commit_modeset_disables(dev, state);
	 * drm_atomic_helper_commit_planes(dev, state);
	 * drm_atomic_helper_commit_modeset_enables(dev, state);
	 * drm_atomic_helper_wait_for_vblanks(dev, state);
	 * drm_atomic_helper_cleanup_planes(dev, state);
	 * drm_atomic_state_free(state);
	 *
	 * once we have full atomic modeset.  For now, just manually update
	 * plane states to avoid clobbering good states with dummy states
	 * while nuclear pageflipping.
	 */
	for (i = 0; i < dev->mode_config.num_total_plane; i++) {
		struct drm_plane *plane = state->planes[i];

		if (!plane)
			continue;

		plane->state->state = state;
		swap(state->plane_states[i], plane->state);
		plane->state->state = NULL;
	}
	drm_atomic_helper_commit_planes(dev, state);
	drm_atomic_helper_wait_for_vblanks(dev, state);
	drm_atomic_helper_cleanup_planes(dev, state);
	drm_atomic_state_free(state);

	return 0;
}

/**
 * intel_connector_atomic_get_property - fetch connector property value
 * @connector: connector to fetch property for
 * @state: state containing the property value
 * @property: property to look up
 * @val: pointer to write property value into
 *
 * The DRM core does not store shadow copies of properties for
 * atomic-capable drivers.  This entrypoint is used to fetch
 * the current value of a driver-specific connector property.
 */
int
intel_connector_atomic_get_property(struct drm_connector *connector,
				    const struct drm_connector_state *state,
				    struct drm_property *property,
				    uint64_t *val)
{
	int i;

	/*
	 * TODO: We only have atomic modeset for planes at the moment, so the
	 * crtc/connector code isn't quite ready yet.  Until it's ready,
	 * continue to look up all property values in the DRM's shadow copy
	 * in obj->properties->values[].
	 *
	 * When the crtc/connector state work matures, this function should
	 * be updated to read the values out of the state structure instead.
	 */
	for (i = 0; i < connector->base.properties->count; i++) {
		if (connector->base.properties->properties[i] == property) {
			*val = connector->base.properties->values[i];
			return 0;
		}
	}

	return -EINVAL;
}

/*
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
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_crtc_state *crtc_state;

	if (WARN_ON(!intel_crtc->config))
		crtc_state = kzalloc(sizeof(*crtc_state), GFP_KERNEL);
	else
		crtc_state = kmemdup(intel_crtc->config,
				     sizeof(*intel_crtc->config), GFP_KERNEL);

	if (crtc_state)
		crtc_state->base.crtc = crtc;

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
