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
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_panel.h>

#include <video/display_timing.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_kms.h"
#include "rcar_du_lvdscon.h"

static int rcar_du_lvds_connector_get_modes(struct drm_connector *connector)
{
	struct rcar_du_connector *rcon = to_rcar_connector(connector);

	return drm_panel_get_modes(rcon->panel);
}

static const struct drm_connector_helper_funcs connector_helper_funcs = {
	.get_modes = rcar_du_lvds_connector_get_modes,
};

static void rcar_du_lvds_connector_destroy(struct drm_connector *connector)
{
	struct rcar_du_connector *rcon = to_rcar_connector(connector);

	drm_panel_detach(rcon->panel);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rcar_du_lvds_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

int rcar_du_lvds_connector_init(struct rcar_du_device *rcdu,
				struct rcar_du_encoder *renc,
				const struct device_node *np)
{
	struct drm_encoder *encoder = rcar_encoder_to_drm_encoder(renc);
	struct rcar_du_connector *rcon;
	struct drm_connector *connector;
	int ret;

	rcon = devm_kzalloc(rcdu->dev, sizeof(*rcon), GFP_KERNEL);
	if (rcon == NULL)
		return -ENOMEM;

	connector = &rcon->connector;

	rcon->panel = of_drm_find_panel(np);
	if (!rcon->panel)
		return -EPROBE_DEFER;

	ret = drm_connector_init(rcdu->ddev, connector, &connector_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret < 0)
		return ret;

	drm_connector_helper_add(connector, &connector_helper_funcs);

	connector->dpms = DRM_MODE_DPMS_OFF;
	drm_object_property_set_value(&connector->base,
		rcdu->ddev->mode_config.dpms_property, DRM_MODE_DPMS_OFF);

	ret = drm_mode_connector_attach_encoder(connector, encoder);
	if (ret < 0)
		return ret;

	ret = drm_panel_attach(rcon->panel, connector);
	if (ret < 0)
		return ret;

	rcon->encoder = renc;

	return 0;
}
