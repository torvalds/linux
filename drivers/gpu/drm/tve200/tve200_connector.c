/*
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 * Parts of this file were based on sources as follows:
 *
 * Copyright (C) 2006-2008 Intel Corporation
 * Copyright (C) 2007 Amos Lee <amos_lee@storlinksemi.com>
 * Copyright (C) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (C) 2011 Texas Instruments
 * Copyright (C) 2017 Eric Anholt
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms of
 * such GNU licence.
 */

/**
 * tve200_drm_connector.c
 * Implementation of the connector functions for the Faraday TV Encoder
 */
#include <linux/version.h>
#include <linux/shmem_fs.h>
#include <linux/dma-buf.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>

#include "tve200_drm.h"

static void tve200_connector_destroy(struct drm_connector *connector)
{
	struct tve200_drm_connector *tve200con =
		to_tve200_connector(connector);

	if (tve200con->panel)
		drm_panel_detach(tve200con->panel);

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static enum drm_connector_status tve200_connector_detect(struct drm_connector
							*connector, bool force)
{
	struct tve200_drm_connector *tve200con =
		to_tve200_connector(connector);

	return (tve200con->panel ?
		connector_status_connected :
		connector_status_disconnected);
}

static int tve200_connector_helper_get_modes(struct drm_connector *connector)
{
	struct tve200_drm_connector *tve200con =
		to_tve200_connector(connector);

	if (!tve200con->panel)
		return 0;

	return drm_panel_get_modes(tve200con->panel);
}

static const struct drm_connector_funcs connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = tve200_connector_destroy,
	.detect = tve200_connector_detect,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs connector_helper_funcs = {
	.get_modes = tve200_connector_helper_get_modes,
};

/*
 * Walks the OF graph to find the panel node and then asks DRM to look
 * up the panel.
 */
static struct drm_panel *tve200_get_panel(struct device *dev)
{
	struct device_node *endpoint, *panel_node;
	struct device_node *np = dev->of_node;
	struct drm_panel *panel;

	endpoint = of_graph_get_next_endpoint(np, NULL);
	if (!endpoint) {
		dev_err(dev, "no endpoint to fetch panel\n");
		return NULL;
	}

	/* Don't proceed if we have an endpoint but no panel_node tied to it */
	panel_node = of_graph_get_remote_port_parent(endpoint);
	of_node_put(endpoint);
	if (!panel_node) {
		dev_err(dev, "no valid panel node\n");
		return NULL;
	}

	panel = of_drm_find_panel(panel_node);
	of_node_put(panel_node);

	return panel;
}

int tve200_connector_init(struct drm_device *dev)
{
	struct tve200_drm_dev_private *priv = dev->dev_private;
	struct tve200_drm_connector *tve200con = &priv->connector;
	struct drm_connector *connector = &tve200con->connector;

	drm_connector_init(dev, connector, &connector_funcs,
			   DRM_MODE_CONNECTOR_DPI);
	drm_connector_helper_add(connector, &connector_helper_funcs);

	tve200con->panel = tve200_get_panel(dev->dev);
	if (tve200con->panel)
		drm_panel_attach(tve200con->panel, connector);

	return 0;
}
