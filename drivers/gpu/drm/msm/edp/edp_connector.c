/*
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "drm/drm_edid.h"
#include "msm_kms.h"
#include "edp.h"

struct edp_connector {
	struct drm_connector base;
	struct msm_edp *edp;
};
#define to_edp_connector(x) container_of(x, struct edp_connector, base)

static enum drm_connector_status edp_connector_detect(
		struct drm_connector *connector, bool force)
{
	struct edp_connector *edp_connector = to_edp_connector(connector);
	struct msm_edp *edp = edp_connector->edp;

	DBG("");
	return msm_edp_ctrl_panel_connected(edp->ctrl) ?
		connector_status_connected : connector_status_disconnected;
}

static void edp_connector_destroy(struct drm_connector *connector)
{
	struct edp_connector *edp_connector = to_edp_connector(connector);

	DBG("");

	drm_connector_cleanup(connector);

	kfree(edp_connector);
}

static int edp_connector_get_modes(struct drm_connector *connector)
{
	struct edp_connector *edp_connector = to_edp_connector(connector);
	struct msm_edp *edp = edp_connector->edp;

	struct edid *drm_edid = NULL;
	int ret = 0;

	DBG("");
	ret = msm_edp_ctrl_get_panel_info(edp->ctrl, connector, &drm_edid);
	if (ret)
		return ret;

	drm_mode_connector_update_edid_property(connector, drm_edid);
	if (drm_edid)
		ret = drm_add_edid_modes(connector, drm_edid);

	return ret;
}

static int edp_connector_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct edp_connector *edp_connector = to_edp_connector(connector);
	struct msm_edp *edp = edp_connector->edp;
	struct msm_drm_private *priv = connector->dev->dev_private;
	struct msm_kms *kms = priv->kms;
	long actual, requested;

	requested = 1000 * mode->clock;
	actual = kms->funcs->round_pixclk(kms,
			requested, edp_connector->edp->encoder);

	DBG("requested=%ld, actual=%ld", requested, actual);
	if (actual != requested)
		return MODE_CLOCK_RANGE;

	if (!msm_edp_ctrl_pixel_clock_valid(
		edp->ctrl, mode->clock, NULL, NULL))
		return MODE_CLOCK_RANGE;

	/* Invalidate all modes if color format is not supported */
	if (connector->display_info.bpc > 8)
		return MODE_BAD;

	return MODE_OK;
}

static const struct drm_connector_funcs edp_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = edp_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = edp_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs edp_connector_helper_funcs = {
	.get_modes = edp_connector_get_modes,
	.mode_valid = edp_connector_mode_valid,
};

/* initialize connector */
struct drm_connector *msm_edp_connector_init(struct msm_edp *edp)
{
	struct drm_connector *connector = NULL;
	struct edp_connector *edp_connector;
	int ret;

	edp_connector = kzalloc(sizeof(*edp_connector), GFP_KERNEL);
	if (!edp_connector)
		return ERR_PTR(-ENOMEM);

	edp_connector->edp = edp;

	connector = &edp_connector->base;

	ret = drm_connector_init(edp->dev, connector, &edp_connector_funcs,
			DRM_MODE_CONNECTOR_eDP);
	if (ret)
		return ERR_PTR(ret);

	drm_connector_helper_add(connector, &edp_connector_helper_funcs);

	/* We don't support HPD, so only poll status until connected. */
	connector->polled = DRM_CONNECTOR_POLL_CONNECT;

	/* Display driver doesn't support interlace now. */
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;

	drm_mode_connector_attach_encoder(connector, edp->encoder);

	return connector;
}
