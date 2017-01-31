/*
 * Copyright (C) 2016 Marek Vasut <marex@denx.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/of_graph.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drmP.h>

#include "mxsfb_drv.h"

static struct mxsfb_drm_private *
drm_connector_to_mxsfb_drm_private(struct drm_connector *connector)
{
	return container_of(connector, struct mxsfb_drm_private, connector);
}

static int mxsfb_panel_get_modes(struct drm_connector *connector)
{
	struct mxsfb_drm_private *mxsfb =
			drm_connector_to_mxsfb_drm_private(connector);

	if (mxsfb->panel)
		return mxsfb->panel->funcs->get_modes(mxsfb->panel);

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
	.dpms			= drm_atomic_helper_connector_dpms,
	.detect			= mxsfb_panel_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= mxsfb_panel_connector_destroy,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static int mxsfb_attach_endpoint(struct drm_device *drm,
				 const struct of_endpoint *ep)
{
	struct mxsfb_drm_private *mxsfb = drm->dev_private;
	struct device_node *np;
	struct drm_panel *panel;
	int ret = -EPROBE_DEFER;

	np = of_graph_get_remote_port_parent(ep->local_node);
	panel = of_drm_find_panel(np);
	of_node_put(np);

	if (!panel)
		return -EPROBE_DEFER;

	mxsfb->connector.dpms = DRM_MODE_DPMS_OFF;
	mxsfb->connector.polled = 0;
	drm_connector_helper_add(&mxsfb->connector,
			&mxsfb_panel_connector_helper_funcs);
	ret = drm_connector_init(drm, &mxsfb->connector,
				 &mxsfb_panel_connector_funcs,
				 DRM_MODE_CONNECTOR_Unknown);
	if (!ret)
		mxsfb->panel = panel;

	return ret;
}

int mxsfb_create_output(struct drm_device *drm)
{
	struct device_node *ep_np = NULL;
	struct of_endpoint ep;
	int ret;

	for_each_endpoint_of_node(drm->dev->of_node, ep_np) {
		ret = of_graph_parse_endpoint(ep_np, &ep);
		if (!ret)
			ret = mxsfb_attach_endpoint(drm, &ep);

		if (ret) {
			of_node_put(ep_np);
			return ret;
		}
	}

	return 0;
}
