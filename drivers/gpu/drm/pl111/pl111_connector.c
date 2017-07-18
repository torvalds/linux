/*
 * (C) COPYRIGHT 2012-2013 ARM Limited. All rights reserved.
 *
 * Parts of this file were based on sources as follows:
 *
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (C) 2011 Texas Instruments
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms of
 * such GNU licence.
 *
 */

/**
 * pl111_drm_connector.c
 * Implementation of the connector functions for PL111 DRM
 */
#include <linux/amba/clcd-regs.h>
#include <linux/version.h>
#include <linux/shmem_fs.h>
#include <linux/dma-buf.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>

#include "pl111_drm.h"

static void pl111_connector_destroy(struct drm_connector *connector)
{
	struct pl111_drm_connector *pl111_connector =
		to_pl111_connector(connector);

	if (pl111_connector->panel)
		drm_panel_detach(pl111_connector->panel);

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static enum drm_connector_status pl111_connector_detect(struct drm_connector
							*connector, bool force)
{
	struct pl111_drm_connector *pl111_connector =
		to_pl111_connector(connector);

	return (pl111_connector->panel ?
		connector_status_connected :
		connector_status_disconnected);
}

static int pl111_connector_helper_get_modes(struct drm_connector *connector)
{
	struct pl111_drm_connector *pl111_connector =
		to_pl111_connector(connector);

	if (!pl111_connector->panel)
		return 0;

	return drm_panel_get_modes(pl111_connector->panel);
}

const struct drm_connector_funcs connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = pl111_connector_destroy,
	.detect = pl111_connector_detect,
	.dpms = drm_atomic_helper_connector_dpms,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

const struct drm_connector_helper_funcs connector_helper_funcs = {
	.get_modes = pl111_connector_helper_get_modes,
};

/* Walks the OF graph to find the panel node and then asks DRM to look
 * up the panel.
 */
static struct drm_panel *pl111_get_panel(struct device *dev)
{
	struct device_node *endpoint, *panel_node;
	struct device_node *np = dev->of_node;
	struct drm_panel *panel;

	endpoint = of_graph_get_next_endpoint(np, NULL);
	if (!endpoint) {
		dev_err(dev, "no endpoint to fetch panel\n");
		return NULL;
	}

	/* don't proceed if we have an endpoint but no panel_node tied to it */
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

int pl111_connector_init(struct drm_device *dev)
{
	struct pl111_drm_dev_private *priv = dev->dev_private;
	struct pl111_drm_connector *pl111_connector = &priv->connector;
	struct drm_connector *connector = &pl111_connector->connector;

	drm_connector_init(dev, connector, &connector_funcs,
			   DRM_MODE_CONNECTOR_DPI);
	drm_connector_helper_add(connector, &connector_helper_funcs);

	pl111_connector->panel = pl111_get_panel(dev->dev);
	if (pl111_connector->panel)
		drm_panel_attach(pl111_connector->panel, connector);

	return 0;
}

