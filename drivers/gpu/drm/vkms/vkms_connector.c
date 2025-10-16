// SPDX-License-Identifier: GPL-2.0+

#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>

#include "vkms_config.h"
#include "vkms_connector.h"

static enum drm_connector_status vkms_connector_detect(struct drm_connector *connector,
						       bool force)
{
	struct drm_device *dev = connector->dev;
	struct vkms_device *vkmsdev = drm_device_to_vkms_device(dev);
	struct vkms_connector *vkms_connector;
	enum drm_connector_status status;
	struct vkms_config_connector *connector_cfg;

	vkms_connector = drm_connector_to_vkms_connector(connector);

	/*
	 * The connector configuration might not exist if its configfs directory
	 * was deleted. Therefore, use the configuration if present or keep the
	 * current status if we can not access it anymore.
	 */
	status = connector->status;

	vkms_config_for_each_connector(vkmsdev->config, connector_cfg) {
		if (connector_cfg->connector == vkms_connector)
			status = vkms_config_connector_get_status(connector_cfg);
	}

	return status;
}

static const struct drm_connector_funcs vkms_connector_funcs = {
	.detect = vkms_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int vkms_conn_get_modes(struct drm_connector *connector)
{
	int count;

	/* Use the default modes list from DRM */
	count = drm_add_modes_noedid(connector, XRES_MAX, YRES_MAX);
	drm_set_preferred_mode(connector, XRES_DEF, YRES_DEF);

	return count;
}

static struct drm_encoder *vkms_conn_best_encoder(struct drm_connector *connector)
{
	struct drm_encoder *encoder;

	drm_connector_for_each_possible_encoder(connector, encoder)
		return encoder;

	return NULL;
}

static const struct drm_connector_helper_funcs vkms_conn_helper_funcs = {
	.get_modes    = vkms_conn_get_modes,
	.best_encoder = vkms_conn_best_encoder,
};

struct vkms_connector *vkms_connector_init(struct vkms_device *vkmsdev)
{
	struct drm_device *dev = &vkmsdev->drm;
	struct vkms_connector *connector;
	int ret;

	connector = drmm_kzalloc(dev, sizeof(*connector), GFP_KERNEL);
	if (!connector)
		return ERR_PTR(-ENOMEM);

	ret = drmm_connector_init(dev, &connector->base, &vkms_connector_funcs,
				  DRM_MODE_CONNECTOR_VIRTUAL, NULL);
	if (ret)
		return ERR_PTR(ret);

	drm_connector_helper_add(&connector->base, &vkms_conn_helper_funcs);

	return connector;
}

void vkms_trigger_connector_hotplug(struct vkms_device *vkmsdev)
{
	struct drm_device *dev = &vkmsdev->drm;

	drm_kms_helper_hotplug_event(dev);
}
