// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2024 Linaro Ltd
 */

#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_managed.h>
#include <drm/display/drm_hdmi_cec_helper.h>

#include <linux/export.h>
#include <linux/mutex.h>

#include <media/cec.h>
#include <media/cec-notifier.h>

static void drm_connector_hdmi_cec_notifier_phys_addr_invalidate(struct drm_connector *connector)
{
	cec_notifier_phys_addr_invalidate(connector->cec.data);
}

static void drm_connector_hdmi_cec_notifier_phys_addr_set(struct drm_connector *connector,
							  u16 addr)
{
	cec_notifier_set_phys_addr(connector->cec.data, addr);
}

static void drm_connector_hdmi_cec_notifier_unregister(struct drm_device *dev, void *res)
{
	struct drm_connector *connector = res;

	cec_notifier_conn_unregister(connector->cec.data);
	connector->cec.data = NULL;
}

static const struct drm_connector_cec_funcs drm_connector_cec_notifier_funcs = {
	.phys_addr_invalidate = drm_connector_hdmi_cec_notifier_phys_addr_invalidate,
	.phys_addr_set = drm_connector_hdmi_cec_notifier_phys_addr_set,
};

int drmm_connector_hdmi_cec_notifier_register(struct drm_connector *connector,
					      const char *port_name,
					      struct device *dev)
{
	struct cec_connector_info conn_info;
	struct cec_notifier *notifier;

	cec_fill_conn_info_from_drm(&conn_info, connector);

	notifier = cec_notifier_conn_register(dev, port_name, &conn_info);
	if (!notifier)
		return -ENOMEM;

	mutex_lock(&connector->cec.mutex);

	connector->cec.data = notifier;
	connector->cec.funcs = &drm_connector_cec_notifier_funcs;

	mutex_unlock(&connector->cec.mutex);

	return drmm_add_action_or_reset(connector->dev,
					drm_connector_hdmi_cec_notifier_unregister,
					connector);
}
EXPORT_SYMBOL(drmm_connector_hdmi_cec_notifier_register);
