/*
 * R-Car Display Unit HDMI Connector
 *
 * Copyright (C) 2014 Renesas Electronics Corporation
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
#include <drm/drm_encoder_slave.h>

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_hdmicon.h"
#include "rcar_du_kms.h"

#define to_slave_funcs(e)	(to_rcar_encoder(e)->slave.slave_funcs)

static int rcar_du_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct rcar_du_connector *con = to_rcar_connector(connector);
	struct drm_encoder *encoder = rcar_encoder_to_drm_encoder(con->encoder);
	const struct drm_encoder_slave_funcs *sfuncs = to_slave_funcs(encoder);

	if (sfuncs->get_modes == NULL)
		return 0;

	return sfuncs->get_modes(encoder, connector);
}

static int rcar_du_hdmi_connector_mode_valid(struct drm_connector *connector,
					     struct drm_display_mode *mode)
{
	struct rcar_du_connector *con = to_rcar_connector(connector);
	struct drm_encoder *encoder = rcar_encoder_to_drm_encoder(con->encoder);
	const struct drm_encoder_slave_funcs *sfuncs = to_slave_funcs(encoder);

	if (sfuncs->mode_valid == NULL)
		return MODE_OK;

	return sfuncs->mode_valid(encoder, mode);
}

static const struct drm_connector_helper_funcs connector_helper_funcs = {
	.get_modes = rcar_du_hdmi_connector_get_modes,
	.mode_valid = rcar_du_hdmi_connector_mode_valid,
	.best_encoder = rcar_du_connector_best_encoder,
};

static enum drm_connector_status
rcar_du_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	struct rcar_du_connector *con = to_rcar_connector(connector);
	struct drm_encoder *encoder = rcar_encoder_to_drm_encoder(con->encoder);
	const struct drm_encoder_slave_funcs *sfuncs = to_slave_funcs(encoder);

	if (sfuncs->detect == NULL)
		return connector_status_unknown;

	return sfuncs->detect(encoder, connector);
}

static const struct drm_connector_funcs connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.reset = drm_atomic_helper_connector_reset,
	.detect = rcar_du_hdmi_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

int rcar_du_hdmi_connector_init(struct rcar_du_device *rcdu,
				struct rcar_du_encoder *renc)
{
	struct drm_encoder *encoder = rcar_encoder_to_drm_encoder(renc);
	struct rcar_du_connector *rcon;
	struct drm_connector *connector;
	int ret;

	rcon = devm_kzalloc(rcdu->dev, sizeof(*rcon), GFP_KERNEL);
	if (rcon == NULL)
		return -ENOMEM;

	connector = &rcon->connector;
	connector->display_info.width_mm = 0;
	connector->display_info.height_mm = 0;
	connector->interlace_allowed = true;
	connector->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(rcdu->ddev, connector, &connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret < 0)
		return ret;

	drm_connector_helper_add(connector, &connector_helper_funcs);

	connector->dpms = DRM_MODE_DPMS_OFF;
	drm_object_property_set_value(&connector->base,
		rcdu->ddev->mode_config.dpms_property, DRM_MODE_DPMS_OFF);

	ret = drm_mode_connector_attach_encoder(connector, encoder);
	if (ret < 0)
		return ret;

	rcon->encoder = renc;

	return 0;
}
