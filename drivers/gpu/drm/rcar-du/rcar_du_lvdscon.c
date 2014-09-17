/*
 * rcar_du_lvdscon.c  --  R-Car Display Unit LVDS Connector
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include <video/display_timing.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_kms.h"
#include "rcar_du_lvdscon.h"

struct rcar_du_lvds_connector {
	struct rcar_du_connector connector;

	struct rcar_du_panel_data panel;
};

#define to_rcar_lvds_connector(c) \
	container_of(c, struct rcar_du_lvds_connector, connector.connector)

static int rcar_du_lvds_connector_get_modes(struct drm_connector *connector)
{
	struct rcar_du_lvds_connector *lvdscon =
		to_rcar_lvds_connector(connector);
	struct drm_display_mode *mode;

	mode = drm_mode_create(connector->dev);
	if (mode == NULL)
		return 0;

	mode->type = DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER;

	drm_display_mode_from_videomode(&lvdscon->panel.mode, mode);

	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_connector_helper_funcs connector_helper_funcs = {
	.get_modes = rcar_du_lvds_connector_get_modes,
	.best_encoder = rcar_du_connector_best_encoder,
};

static void rcar_du_lvds_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static enum drm_connector_status
rcar_du_lvds_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = rcar_du_lvds_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rcar_du_lvds_connector_destroy,
};

int rcar_du_lvds_connector_init(struct rcar_du_device *rcdu,
				struct rcar_du_encoder *renc,
				const struct rcar_du_panel_data *panel,
				/* TODO const */ struct device_node *np)
{
	struct rcar_du_lvds_connector *lvdscon;
	struct drm_connector *connector;
	int ret;

	lvdscon = devm_kzalloc(rcdu->dev, sizeof(*lvdscon), GFP_KERNEL);
	if (lvdscon == NULL)
		return -ENOMEM;

	if (panel) {
		lvdscon->panel = *panel;
	} else {
		struct display_timing timing;

		ret = of_get_display_timing(np, "panel-timing", &timing);
		if (ret < 0)
			return ret;

		videomode_from_timing(&timing, &lvdscon->panel.mode);

		of_property_read_u32(np, "width-mm", &lvdscon->panel.width_mm);
		of_property_read_u32(np, "height-mm", &lvdscon->panel.height_mm);
	}

	connector = &lvdscon->connector.connector;
	connector->display_info.width_mm = lvdscon->panel.width_mm;
	connector->display_info.height_mm = lvdscon->panel.height_mm;

	ret = drm_connector_init(rcdu->ddev, connector, &connector_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret < 0)
		return ret;

	drm_connector_helper_add(connector, &connector_helper_funcs);
	ret = drm_connector_register(connector);
	if (ret < 0)
		return ret;

	drm_helper_connector_dpms(connector, DRM_MODE_DPMS_OFF);
	drm_object_property_set_value(&connector->base,
		rcdu->ddev->mode_config.dpms_property, DRM_MODE_DPMS_OFF);

	ret = drm_mode_connector_attach_encoder(connector, &renc->encoder);
	if (ret < 0)
		return ret;

	connector->encoder = &renc->encoder;
	lvdscon->connector.encoder = renc;

	return 0;
}
