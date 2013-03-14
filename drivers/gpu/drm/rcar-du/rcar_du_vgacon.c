/*
 * rcar_du_vgacon.c  --  R-Car Display Unit VGA Connector
 *
 * Copyright (C) 2013 Renesas Corporation
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

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_kms.h"
#include "rcar_du_vgacon.h"

static int rcar_du_vga_connector_get_modes(struct drm_connector *connector)
{
	return 0;
}

static int rcar_du_vga_connector_mode_valid(struct drm_connector *connector,
					    struct drm_display_mode *mode)
{
	return MODE_OK;
}

static const struct drm_connector_helper_funcs connector_helper_funcs = {
	.get_modes = rcar_du_vga_connector_get_modes,
	.mode_valid = rcar_du_vga_connector_mode_valid,
	.best_encoder = rcar_du_connector_best_encoder,
};

static void rcar_du_vga_connector_destroy(struct drm_connector *connector)
{
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
}

static enum drm_connector_status
rcar_du_vga_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = rcar_du_vga_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rcar_du_vga_connector_destroy,
};

int rcar_du_vga_connector_init(struct rcar_du_device *rcdu,
			       struct rcar_du_encoder *renc)
{
	struct rcar_du_connector *rcon;
	struct drm_connector *connector;
	int ret;

	rcon = devm_kzalloc(rcdu->dev, sizeof(*rcon), GFP_KERNEL);
	if (rcon == NULL)
		return -ENOMEM;

	connector = &rcon->connector;
	connector->display_info.width_mm = 0;
	connector->display_info.height_mm = 0;

	ret = drm_connector_init(rcdu->ddev, connector, &connector_funcs,
				 DRM_MODE_CONNECTOR_VGA);
	if (ret < 0)
		return ret;

	drm_connector_helper_add(connector, &connector_helper_funcs);
	ret = drm_sysfs_connector_add(connector);
	if (ret < 0)
		return ret;

	drm_helper_connector_dpms(connector, DRM_MODE_DPMS_OFF);
	drm_object_property_set_value(&connector->base,
		rcdu->ddev->mode_config.dpms_property, DRM_MODE_DPMS_OFF);

	ret = drm_mode_connector_attach_encoder(connector, &renc->encoder);
	if (ret < 0)
		return ret;

	connector->encoder = &renc->encoder;
	rcon->encoder = renc;

	return 0;
}
