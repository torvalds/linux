// SPDX-License-Identifier: MIT
/*
 * Copyright 2018 Noralf Trønnes
 * Copyright (c) 2006-2009 Red Hat Inc.
 * Copyright (c) 2006-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <drm/drm_atomic.h>
#include <drm/drm_client.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

int drm_client_modeset_create(struct drm_client_dev *client)
{
	struct drm_device *dev = client->dev;
	unsigned int num_crtc = dev->mode_config.num_crtc;
	unsigned int max_connector_count = 1;
	struct drm_mode_set *modeset;
	struct drm_crtc *crtc;
	unsigned int i = 0;

	/* Add terminating zero entry to enable index less iteration */
	client->modesets = kcalloc(num_crtc + 1, sizeof(*client->modesets), GFP_KERNEL);
	if (!client->modesets)
		return -ENOMEM;

	mutex_init(&client->modeset_mutex);

	drm_for_each_crtc(crtc, dev)
		client->modesets[i++].crtc = crtc;

	/* Cloning is only supported in the single crtc case. */
	if (num_crtc == 1)
		max_connector_count = DRM_CLIENT_MAX_CLONED_CONNECTORS;

	for (modeset = client->modesets; modeset->crtc; modeset++) {
		modeset->connectors = kcalloc(max_connector_count,
					      sizeof(*modeset->connectors), GFP_KERNEL);
		if (!modeset->connectors)
			goto err_free;
	}

	return 0;

err_free:
	drm_client_modeset_free(client);

	return -ENOMEM;
}

void drm_client_modeset_release(struct drm_client_dev *client)
{
	struct drm_mode_set *modeset;
	unsigned int i;

	drm_client_for_each_modeset(modeset, client) {
		drm_mode_destroy(client->dev, modeset->mode);
		modeset->mode = NULL;
		modeset->fb = NULL;

		for (i = 0; i < modeset->num_connectors; i++) {
			drm_connector_put(modeset->connectors[i]);
			modeset->connectors[i] = NULL;
		}
		modeset->num_connectors = 0;
	}
}
/* TODO: Remove export when modeset code has been moved over */
EXPORT_SYMBOL(drm_client_modeset_release);

void drm_client_modeset_free(struct drm_client_dev *client)
{
	struct drm_mode_set *modeset;

	mutex_lock(&client->modeset_mutex);

	drm_client_modeset_release(client);

	drm_client_for_each_modeset(modeset, client)
		kfree(modeset->connectors);

	mutex_unlock(&client->modeset_mutex);

	mutex_destroy(&client->modeset_mutex);
	kfree(client->modesets);
}

struct drm_mode_set *drm_client_find_modeset(struct drm_client_dev *client, struct drm_crtc *crtc)
{
	struct drm_mode_set *modeset;

	drm_client_for_each_modeset(modeset, client)
		if (modeset->crtc == crtc)
			return modeset;

	return NULL;
}
/* TODO: Remove export when modeset code has been moved over */
EXPORT_SYMBOL(drm_client_find_modeset);

/**
 * drm_client_panel_rotation() - Check panel orientation
 * @modeset: DRM modeset
 * @rotation: Returned rotation value
 *
 * This function checks if the primary plane in @modeset can hw rotate to match
 * the panel orientation on its connector.
 *
 * Note: Currently only 0 and 180 degrees are supported.
 *
 * Return:
 * True if the plane can do the rotation, false otherwise.
 */
bool drm_client_panel_rotation(struct drm_mode_set *modeset, unsigned int *rotation)
{
	struct drm_connector *connector = modeset->connectors[0];
	struct drm_plane *plane = modeset->crtc->primary;
	u64 valid_mask = 0;
	unsigned int i;

	if (!modeset->num_connectors)
		return false;

	switch (connector->display_info.panel_orientation) {
	case DRM_MODE_PANEL_ORIENTATION_BOTTOM_UP:
		*rotation = DRM_MODE_ROTATE_180;
		break;
	case DRM_MODE_PANEL_ORIENTATION_LEFT_UP:
		*rotation = DRM_MODE_ROTATE_90;
		break;
	case DRM_MODE_PANEL_ORIENTATION_RIGHT_UP:
		*rotation = DRM_MODE_ROTATE_270;
		break;
	default:
		*rotation = DRM_MODE_ROTATE_0;
	}

	/*
	 * TODO: support 90 / 270 degree hardware rotation,
	 * depending on the hardware this may require the framebuffer
	 * to be in a specific tiling format.
	 */
	if (*rotation != DRM_MODE_ROTATE_180 || !plane->rotation_property)
		return false;

	for (i = 0; i < plane->rotation_property->num_values; i++)
		valid_mask |= (1ULL << plane->rotation_property->values[i]);

	if (!(*rotation & valid_mask))
		return false;

	return true;
}
EXPORT_SYMBOL(drm_client_panel_rotation);

static int drm_client_modeset_commit_atomic(struct drm_client_dev *client, bool active)
{
	struct drm_device *dev = client->dev;
	struct drm_plane_state *plane_state;
	struct drm_plane *plane;
	struct drm_atomic_state *state;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_mode_set *mode_set;
	int ret;

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_atomic_state_alloc(dev);
	if (!state) {
		ret = -ENOMEM;
		goto out_ctx;
	}

	state->acquire_ctx = &ctx;
retry:
	drm_for_each_plane(plane, dev) {
		plane_state = drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(plane_state)) {
			ret = PTR_ERR(plane_state);
			goto out_state;
		}

		plane_state->rotation = DRM_MODE_ROTATE_0;

		/* disable non-primary: */
		if (plane->type == DRM_PLANE_TYPE_PRIMARY)
			continue;

		ret = __drm_atomic_helper_disable_plane(plane, plane_state);
		if (ret != 0)
			goto out_state;
	}

	drm_client_for_each_modeset(mode_set, client) {
		struct drm_plane *primary = mode_set->crtc->primary;
		unsigned int rotation;

		if (drm_client_panel_rotation(mode_set, &rotation)) {
			/* Cannot fail as we've already gotten the plane state above */
			plane_state = drm_atomic_get_new_plane_state(state, primary);
			plane_state->rotation = rotation;
		}

		ret = __drm_atomic_helper_set_config(mode_set, state);
		if (ret != 0)
			goto out_state;

		/*
		 * __drm_atomic_helper_set_config() sets active when a
		 * mode is set, unconditionally clear it if we force DPMS off
		 */
		if (!active) {
			struct drm_crtc *crtc = mode_set->crtc;
			struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

			crtc_state->active = false;
		}
	}

	ret = drm_atomic_commit(state);

out_state:
	if (ret == -EDEADLK)
		goto backoff;

	drm_atomic_state_put(state);
out_ctx:
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;

backoff:
	drm_atomic_state_clear(state);
	drm_modeset_backoff(&ctx);

	goto retry;
}

static int drm_client_modeset_commit_legacy(struct drm_client_dev *client)
{
	struct drm_device *dev = client->dev;
	struct drm_mode_set *mode_set;
	struct drm_plane *plane;
	int ret = 0;

	drm_modeset_lock_all(dev);
	drm_for_each_plane(plane, dev) {
		if (plane->type != DRM_PLANE_TYPE_PRIMARY)
			drm_plane_force_disable(plane);

		if (plane->rotation_property)
			drm_mode_plane_set_obj_prop(plane,
						    plane->rotation_property,
						    DRM_MODE_ROTATE_0);
	}

	drm_client_for_each_modeset(mode_set, client) {
		struct drm_crtc *crtc = mode_set->crtc;

		if (crtc->funcs->cursor_set2) {
			ret = crtc->funcs->cursor_set2(crtc, NULL, 0, 0, 0, 0, 0);
			if (ret)
				goto out;
		} else if (crtc->funcs->cursor_set) {
			ret = crtc->funcs->cursor_set(crtc, NULL, 0, 0, 0);
			if (ret)
				goto out;
		}

		ret = drm_mode_set_config_internal(mode_set);
		if (ret)
			goto out;
	}
out:
	drm_modeset_unlock_all(dev);

	return ret;
}

/**
 * drm_client_modeset_commit_force() - Force commit CRTC configuration
 * @client: DRM client
 *
 * Commit modeset configuration to crtcs without checking if there is a DRM master.
 *
 * Returns:
 * Zero on success or negative error code on failure.
 */
int drm_client_modeset_commit_force(struct drm_client_dev *client)
{
	struct drm_device *dev = client->dev;
	int ret;

	mutex_lock(&client->modeset_mutex);
	if (drm_drv_uses_atomic_modeset(dev))
		ret = drm_client_modeset_commit_atomic(client, true);
	else
		ret = drm_client_modeset_commit_legacy(client);
	mutex_unlock(&client->modeset_mutex);

	return ret;
}
EXPORT_SYMBOL(drm_client_modeset_commit_force);

/**
 * drm_client_modeset_commit() - Commit CRTC configuration
 * @client: DRM client
 *
 * Commit modeset configuration to crtcs.
 *
 * Returns:
 * Zero on success or negative error code on failure.
 */
int drm_client_modeset_commit(struct drm_client_dev *client)
{
	struct drm_device *dev = client->dev;
	int ret;

	if (!drm_master_internal_acquire(dev))
		return -EBUSY;

	ret = drm_client_modeset_commit_force(client);

	drm_master_internal_release(dev);

	return ret;
}
EXPORT_SYMBOL(drm_client_modeset_commit);

static void drm_client_modeset_dpms_legacy(struct drm_client_dev *client, int dpms_mode)
{
	struct drm_device *dev = client->dev;
	struct drm_connector *connector;
	struct drm_mode_set *modeset;
	int j;

	drm_modeset_lock_all(dev);
	drm_client_for_each_modeset(modeset, client) {
		if (!modeset->crtc->enabled)
			continue;

		for (j = 0; j < modeset->num_connectors; j++) {
			connector = modeset->connectors[j];
			connector->funcs->dpms(connector, dpms_mode);
			drm_object_property_set_value(&connector->base,
				dev->mode_config.dpms_property, dpms_mode);
		}
	}
	drm_modeset_unlock_all(dev);
}

/**
 * drm_client_modeset_dpms() - Set DPMS mode
 * @client: DRM client
 * @mode: DPMS mode
 *
 * Note: For atomic drivers @mode is reduced to on/off.
 *
 * Returns:
 * Zero on success or negative error code on failure.
 */
int drm_client_modeset_dpms(struct drm_client_dev *client, int mode)
{
	struct drm_device *dev = client->dev;
	int ret = 0;

	if (!drm_master_internal_acquire(dev))
		return -EBUSY;

	mutex_lock(&client->modeset_mutex);
	if (drm_drv_uses_atomic_modeset(dev))
		ret = drm_client_modeset_commit_atomic(client, mode == DRM_MODE_DPMS_ON);
	else
		drm_client_modeset_dpms_legacy(client, mode);
	mutex_unlock(&client->modeset_mutex);

	drm_master_internal_release(dev);

	return ret;
}
EXPORT_SYMBOL(drm_client_modeset_dpms);
