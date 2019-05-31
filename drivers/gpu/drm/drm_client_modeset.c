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

#include <drm/drm_client.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>

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
