// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_print.h>

#include "intel_atomic.h"
#include "intel_crtc.h"
#include "intel_display_core.h"
#include "intel_display_types.h"
#include "intel_load_detect.h"

/* VESA 640x480x72Hz mode to set on the pipe */
static const struct drm_display_mode load_detect_mode = {
	DRM_MODE("640x480", DRM_MODE_TYPE_DEFAULT, 31500, 640, 664,
		 704, 832, 0, 480, 489, 491, 520, 0, DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
};

static int intel_modeset_disable_planes(struct drm_atomic_state *state,
					struct drm_crtc *crtc)
{
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	int ret, i;

	ret = drm_atomic_add_affected_planes(state, crtc);
	if (ret)
		return ret;

	for_each_new_plane_in_state(state, plane, plane_state, i) {
		if (plane_state->crtc != crtc)
			continue;

		ret = drm_atomic_set_crtc_for_plane(plane_state, NULL);
		if (ret)
			return ret;

		drm_atomic_set_fb_for_plane(plane_state, NULL);
	}

	return 0;
}

struct drm_atomic_state *
intel_load_detect_get_pipe(struct drm_connector *connector,
			   struct drm_modeset_acquire_ctx *ctx)
{
	struct intel_display *display = to_intel_display(connector->dev);
	struct intel_encoder *encoder =
		intel_attached_encoder(to_intel_connector(connector));
	struct intel_crtc *possible_crtc;
	struct intel_crtc *crtc = NULL;
	struct drm_mode_config *config = &display->drm->mode_config;
	struct drm_atomic_state *state = NULL, *restore_state = NULL;
	struct drm_connector_state *connector_state;
	struct intel_crtc_state *crtc_state;
	int ret;

	drm_dbg_kms(display->drm, "[CONNECTOR:%d:%s], [ENCODER:%d:%s]\n",
		    connector->base.id, connector->name,
		    encoder->base.base.id, encoder->base.name);

	drm_WARN_ON(display->drm, !drm_modeset_is_locked(&config->connection_mutex));

	/*
	 * Algorithm gets a little messy:
	 *
	 *   - if the connector already has an assigned crtc, use it (but make
	 *     sure it's on first)
	 *
	 *   - try to find the first unused crtc that can drive this connector,
	 *     and use that if we find one
	 */

	/* See if we already have a CRTC for this connector */
	if (connector->state->crtc) {
		crtc = to_intel_crtc(connector->state->crtc);

		ret = drm_modeset_lock(&crtc->base.mutex, ctx);
		if (ret)
			goto fail;

		/* Make sure the crtc and connector are running */
		goto found;
	}

	/* Find an unused one (if possible) */
	for_each_intel_crtc(display->drm, possible_crtc) {
		if (!(encoder->base.possible_crtcs &
		      drm_crtc_mask(&possible_crtc->base)))
			continue;

		ret = drm_modeset_lock(&possible_crtc->base.mutex, ctx);
		if (ret)
			goto fail;

		if (possible_crtc->base.state->enable) {
			drm_modeset_unlock(&possible_crtc->base.mutex);
			continue;
		}

		crtc = possible_crtc;
		break;
	}

	/*
	 * If we didn't find an unused CRTC, don't use any.
	 */
	if (!crtc) {
		drm_dbg_kms(display->drm,
			    "no pipe available for load-detect\n");
		ret = -ENODEV;
		goto fail;
	}

found:
	state = drm_atomic_state_alloc(display->drm);
	restore_state = drm_atomic_state_alloc(display->drm);
	if (!state || !restore_state) {
		ret = -ENOMEM;
		goto fail;
	}

	state->acquire_ctx = ctx;
	to_intel_atomic_state(state)->internal = true;

	restore_state->acquire_ctx = ctx;
	to_intel_atomic_state(restore_state)->internal = true;

	connector_state = drm_atomic_get_connector_state(state, connector);
	if (IS_ERR(connector_state)) {
		ret = PTR_ERR(connector_state);
		goto fail;
	}

	ret = drm_atomic_set_crtc_for_connector(connector_state, &crtc->base);
	if (ret)
		goto fail;

	crtc_state = intel_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto fail;
	}

	crtc_state->uapi.active = true;

	ret = drm_atomic_set_mode_for_crtc(&crtc_state->uapi,
					   &load_detect_mode);
	if (ret)
		goto fail;

	ret = intel_modeset_disable_planes(state, &crtc->base);
	if (ret)
		goto fail;

	ret = PTR_ERR_OR_ZERO(drm_atomic_get_connector_state(restore_state, connector));
	if (!ret)
		ret = PTR_ERR_OR_ZERO(drm_atomic_get_crtc_state(restore_state, &crtc->base));
	if (!ret)
		ret = drm_atomic_add_affected_planes(restore_state, &crtc->base);
	if (ret) {
		drm_dbg_kms(display->drm,
			    "Failed to create a copy of old state to restore: %i\n",
			    ret);
		goto fail;
	}

	ret = drm_atomic_commit(state);
	if (ret) {
		drm_dbg_kms(display->drm,
			    "failed to set mode on load-detect pipe\n");
		goto fail;
	}

	drm_atomic_state_put(state);

	/* let the connector get through one full cycle before testing */
	intel_crtc_wait_for_next_vblank(crtc);

	return restore_state;

fail:
	if (state) {
		drm_atomic_state_put(state);
		state = NULL;
	}
	if (restore_state) {
		drm_atomic_state_put(restore_state);
		restore_state = NULL;
	}

	if (ret == -EDEADLK)
		return ERR_PTR(ret);

	return NULL;
}

void intel_load_detect_release_pipe(struct drm_connector *connector,
				    struct drm_atomic_state *state,
				    struct drm_modeset_acquire_ctx *ctx)
{
	struct intel_display *display = to_intel_display(connector->dev);
	struct intel_encoder *intel_encoder =
		intel_attached_encoder(to_intel_connector(connector));
	struct drm_encoder *encoder = &intel_encoder->base;
	int ret;

	drm_dbg_kms(display->drm, "[CONNECTOR:%d:%s], [ENCODER:%d:%s]\n",
		    connector->base.id, connector->name,
		    encoder->base.id, encoder->name);

	if (IS_ERR_OR_NULL(state))
		return;

	ret = drm_atomic_helper_commit_duplicated_state(state, ctx);
	if (ret)
		drm_dbg_kms(display->drm,
			    "Couldn't release load detect pipe: %i\n", ret);
	drm_atomic_state_put(state);
}
