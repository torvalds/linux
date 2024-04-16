// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <linux/types.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "logicvc_crtc.h"
#include "logicvc_drm.h"
#include "logicvc_interface.h"
#include "logicvc_regs.h"

#define logicvc_interface_from_drm_encoder(c) \
	container_of(c, struct logicvc_interface, drm_encoder)
#define logicvc_interface_from_drm_connector(c) \
	container_of(c, struct logicvc_interface, drm_connector)

static void logicvc_encoder_enable(struct drm_encoder *drm_encoder)
{
	struct logicvc_drm *logicvc = logicvc_drm(drm_encoder->dev);
	struct logicvc_interface *interface =
		logicvc_interface_from_drm_encoder(drm_encoder);

	regmap_update_bits(logicvc->regmap, LOGICVC_POWER_CTRL_REG,
			   LOGICVC_POWER_CTRL_VIDEO_ENABLE,
			   LOGICVC_POWER_CTRL_VIDEO_ENABLE);

	if (interface->drm_panel) {
		drm_panel_prepare(interface->drm_panel);
		drm_panel_enable(interface->drm_panel);
	}
}

static void logicvc_encoder_disable(struct drm_encoder *drm_encoder)
{
	struct logicvc_interface *interface =
		logicvc_interface_from_drm_encoder(drm_encoder);

	if (interface->drm_panel) {
		drm_panel_disable(interface->drm_panel);
		drm_panel_unprepare(interface->drm_panel);
	}
}

static const struct drm_encoder_helper_funcs logicvc_encoder_helper_funcs = {
	.enable			= logicvc_encoder_enable,
	.disable		= logicvc_encoder_disable,
};

static const struct drm_encoder_funcs logicvc_encoder_funcs = {
	.destroy		= drm_encoder_cleanup,
};

static int logicvc_connector_get_modes(struct drm_connector *drm_connector)
{
	struct logicvc_interface *interface =
		logicvc_interface_from_drm_connector(drm_connector);

	if (interface->drm_panel)
		return drm_panel_get_modes(interface->drm_panel, drm_connector);

	WARN_ONCE(1, "Retrieving modes from a native connector is not implemented.");

	return 0;
}

static const struct drm_connector_helper_funcs logicvc_connector_helper_funcs = {
	.get_modes		= logicvc_connector_get_modes,
};

static const struct drm_connector_funcs logicvc_connector_funcs = {
	.reset			= drm_atomic_helper_connector_reset,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= drm_connector_cleanup,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static int logicvc_interface_encoder_type(struct logicvc_drm *logicvc)
{
	switch (logicvc->config.display_interface) {
	case LOGICVC_DISPLAY_INTERFACE_LVDS_4BITS:
	case LOGICVC_DISPLAY_INTERFACE_LVDS_4BITS_CAMERA:
	case LOGICVC_DISPLAY_INTERFACE_LVDS_3BITS:
		return DRM_MODE_ENCODER_LVDS;
	case LOGICVC_DISPLAY_INTERFACE_DVI:
		return DRM_MODE_ENCODER_TMDS;
	case LOGICVC_DISPLAY_INTERFACE_RGB:
		return DRM_MODE_ENCODER_DPI;
	default:
		return DRM_MODE_ENCODER_NONE;
	}
}

static int logicvc_interface_connector_type(struct logicvc_drm *logicvc)
{
	switch (logicvc->config.display_interface) {
	case LOGICVC_DISPLAY_INTERFACE_LVDS_4BITS:
	case LOGICVC_DISPLAY_INTERFACE_LVDS_4BITS_CAMERA:
	case LOGICVC_DISPLAY_INTERFACE_LVDS_3BITS:
		return DRM_MODE_CONNECTOR_LVDS;
	case LOGICVC_DISPLAY_INTERFACE_DVI:
		return DRM_MODE_CONNECTOR_DVID;
	case LOGICVC_DISPLAY_INTERFACE_RGB:
		return DRM_MODE_CONNECTOR_DPI;
	default:
		return DRM_MODE_CONNECTOR_Unknown;
	}
}

static bool logicvc_interface_native_connector(struct logicvc_drm *logicvc)
{
	switch (logicvc->config.display_interface) {
	case LOGICVC_DISPLAY_INTERFACE_DVI:
		return true;
	default:
		return false;
	}
}

void logicvc_interface_attach_crtc(struct logicvc_drm *logicvc)
{
	uint32_t possible_crtcs = drm_crtc_mask(&logicvc->crtc->drm_crtc);

	logicvc->interface->drm_encoder.possible_crtcs = possible_crtcs;
}

int logicvc_interface_init(struct logicvc_drm *logicvc)
{
	struct logicvc_interface *interface;
	struct drm_device *drm_dev = &logicvc->drm_dev;
	struct device *dev = drm_dev->dev;
	struct device_node *of_node = dev->of_node;
	int encoder_type = logicvc_interface_encoder_type(logicvc);
	int connector_type = logicvc_interface_connector_type(logicvc);
	bool native_connector = logicvc_interface_native_connector(logicvc);
	int ret;

	interface = devm_kzalloc(dev, sizeof(*interface), GFP_KERNEL);
	if (!interface) {
		ret = -ENOMEM;
		goto error_early;
	}

	ret = drm_of_find_panel_or_bridge(of_node, 0, 0, &interface->drm_panel,
					  &interface->drm_bridge);
	if (ret == -EPROBE_DEFER)
		goto error_early;

	ret = drm_encoder_init(drm_dev, &interface->drm_encoder,
			       &logicvc_encoder_funcs, encoder_type, NULL);
	if (ret) {
		drm_err(drm_dev, "Failed to initialize encoder\n");
		goto error_early;
	}

	drm_encoder_helper_add(&interface->drm_encoder,
			       &logicvc_encoder_helper_funcs);

	if (native_connector || interface->drm_panel) {
		ret = drm_connector_init(drm_dev, &interface->drm_connector,
					 &logicvc_connector_funcs,
					 connector_type);
		if (ret) {
			drm_err(drm_dev, "Failed to initialize connector\n");
			goto error_encoder;
		}

		drm_connector_helper_add(&interface->drm_connector,
					 &logicvc_connector_helper_funcs);

		ret = drm_connector_attach_encoder(&interface->drm_connector,
						   &interface->drm_encoder);
		if (ret) {
			drm_err(drm_dev,
				"Failed to attach connector to encoder\n");
			goto error_encoder;
		}
	}

	if (interface->drm_bridge) {
		ret = drm_bridge_attach(&interface->drm_encoder,
					interface->drm_bridge, NULL, 0);
		if (ret) {
			drm_err(drm_dev,
				"Failed to attach bridge to encoder\n");
			goto error_encoder;
		}
	}

	logicvc->interface = interface;

	return 0;

error_encoder:
	drm_encoder_cleanup(&interface->drm_encoder);

error_early:
	return ret;
}
