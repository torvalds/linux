// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Marek Vasut <marex@denx.de>
 */

#include <linux/of_graph.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "mxsfb_drv.h"

static struct mxsfb_drm_private *
drm_connector_to_mxsfb_drm_private(struct drm_connector *connector)
{
	return container_of(connector, struct mxsfb_drm_private,
			    panel_connector);
}

static int mxsfb_panel_get_modes(struct drm_connector *connector)
{
	struct mxsfb_drm_private *mxsfb =
			drm_connector_to_mxsfb_drm_private(connector);

	if (mxsfb->panel)
		return drm_panel_get_modes(mxsfb->panel, connector);

	return 0;
}

static const struct
drm_connector_helper_funcs mxsfb_panel_connector_helper_funcs = {
	.get_modes = mxsfb_panel_get_modes,
};

static enum drm_connector_status
mxsfb_panel_connector_detect(struct drm_connector *connector, bool force)
{
	struct mxsfb_drm_private *mxsfb =
			drm_connector_to_mxsfb_drm_private(connector);

	if (mxsfb->panel)
		return connector_status_connected;

	return connector_status_disconnected;
}

static void mxsfb_panel_connector_destroy(struct drm_connector *connector)
{
	struct mxsfb_drm_private *mxsfb =
			drm_connector_to_mxsfb_drm_private(connector);

	if (mxsfb->panel)
		drm_panel_detach(mxsfb->panel);

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs mxsfb_panel_connector_funcs = {
	.detect			= mxsfb_panel_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= mxsfb_panel_connector_destroy,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

int mxsfb_create_output(struct drm_device *drm)
{
	struct mxsfb_drm_private *mxsfb = drm->dev_private;
	int ret;

	ret = drm_of_find_panel_or_bridge(drm->dev->of_node, 0, 0,
					  &mxsfb->panel, &mxsfb->bridge);
	if (ret)
		return ret;

	if (mxsfb->panel) {
		mxsfb->connector = &mxsfb->panel_connector;
		mxsfb->connector->dpms = DRM_MODE_DPMS_OFF;
		mxsfb->connector->polled = 0;
		drm_connector_helper_add(mxsfb->connector,
					 &mxsfb_panel_connector_helper_funcs);
		ret = drm_connector_init(drm, mxsfb->connector,
					 &mxsfb_panel_connector_funcs,
					 DRM_MODE_CONNECTOR_Unknown);
	}

	return ret;
}
