/*
 * Copyright (C) 2015 Texas Instruments
 * Author: Jyri Sarha <jsarha@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */

#include <linux/component.h>
#include <linux/of_graph.h>
#include <drm/drm_of.h>

#include "tilcdc_drv.h"
#include "tilcdc_external.h"

static const struct tilcdc_panel_info panel_info_tda998x = {
		.ac_bias                = 255,
		.ac_bias_intrpt         = 0,
		.dma_burst_sz           = 16,
		.bpp                    = 16,
		.fdd                    = 0x80,
		.tft_alt_mode           = 0,
		.invert_pxl_clk		= 1,
		.sync_edge              = 1,
		.sync_ctrl              = 1,
		.raster_order           = 0,
};

static int tilcdc_external_mode_valid(struct drm_connector *connector,
				      struct drm_display_mode *mode)
{
	struct tilcdc_drm_private *priv = connector->dev->dev_private;
	int ret, i;

	ret = tilcdc_crtc_mode_valid(priv->crtc, mode);
	if (ret != MODE_OK)
		return ret;

	for (i = 0; i < priv->num_connectors &&
		     priv->connectors[i] != connector; i++)
		;

	BUG_ON(priv->connectors[i] != connector);
	BUG_ON(!priv->connector_funcs[i]);

	/* If the connector has its own mode_valid call it. */
	if (!IS_ERR(priv->connector_funcs[i]) &&
	    priv->connector_funcs[i]->mode_valid)
		return priv->connector_funcs[i]->mode_valid(connector, mode);

	return MODE_OK;
}

static int tilcdc_add_external_encoder(struct drm_device *dev,
				       struct drm_connector *connector)
{
	struct tilcdc_drm_private *priv = dev->dev_private;
	struct drm_connector_helper_funcs *connector_funcs;

	priv->connectors[priv->num_connectors] = connector;
	priv->encoders[priv->num_encoders++] = connector->encoder;

	/* Only tda998x is supported at the moment. */
	tilcdc_crtc_set_simulate_vesa_sync(priv->crtc, true);
	tilcdc_crtc_set_panel_info(priv->crtc, &panel_info_tda998x);

	connector_funcs = devm_kzalloc(dev->dev, sizeof(*connector_funcs),
				       GFP_KERNEL);
	if (!connector_funcs)
		return -ENOMEM;

	/* connector->helper_private contains always struct
	 * connector_helper_funcs pointer. For tilcdc crtc to have a
	 * say if a specific mode is Ok, we need to install our own
	 * helper functions. In our helper functions we copy
	 * everything else but use our own mode_valid() (above).
	 */
	if (connector->helper_private) {
		priv->connector_funcs[priv->num_connectors] =
			connector->helper_private;
		*connector_funcs = *priv->connector_funcs[priv->num_connectors];
	} else {
		priv->connector_funcs[priv->num_connectors] = ERR_PTR(-ENOENT);
	}
	connector_funcs->mode_valid = tilcdc_external_mode_valid;
	drm_connector_helper_add(connector, connector_funcs);
	priv->num_connectors++;

	dev_dbg(dev->dev, "External encoder '%s' connected\n",
		connector->encoder->name);

	return 0;
}

int tilcdc_add_external_encoders(struct drm_device *dev)
{
	struct tilcdc_drm_private *priv = dev->dev_private;
	struct drm_connector *connector;
	int num_internal_connectors = priv->num_connectors;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		bool found = false;
		int i, ret;

		for (i = 0; i < num_internal_connectors; i++)
			if (connector == priv->connectors[i])
				found = true;
		if (!found) {
			ret = tilcdc_add_external_encoder(dev, connector);
			if (ret)
				return ret;
		}
	}
	return 0;
}

void tilcdc_remove_external_encoders(struct drm_device *dev)
{
	struct tilcdc_drm_private *priv = dev->dev_private;
	int i;

	/* Restore the original helper functions, if any. */
	for (i = 0; i < priv->num_connectors; i++)
		if (IS_ERR(priv->connector_funcs[i]))
			drm_connector_helper_add(priv->connectors[i], NULL);
		else if (priv->connector_funcs[i])
			drm_connector_helper_add(priv->connectors[i],
						 priv->connector_funcs[i]);
}

static int dev_match_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

int tilcdc_get_external_components(struct device *dev,
				   struct component_match **match)
{
	struct device_node *node;
	struct device_node *ep = NULL;
	int count = 0;

	/* Avoid error print by of_graph_get_next_endpoint() if there
	 * is no ports present.
	 */
	node = of_get_child_by_name(dev->of_node, "ports");
	if (!node)
		node = of_get_child_by_name(dev->of_node, "port");
	if (!node)
		return 0;
	of_node_put(node);

	while ((ep = of_graph_get_next_endpoint(dev->of_node, ep))) {
		node = of_graph_get_remote_port_parent(ep);
		if (!node || !of_device_is_available(node)) {
			of_node_put(node);
			continue;
		}

		dev_dbg(dev, "Subdevice node '%s' found\n", node->name);
		if (match)
			drm_of_component_match_add(dev, match, dev_match_of,
						   node);
		of_node_put(node);
		count++;
	}

	if (count > 1) {
		dev_err(dev, "Only one external encoder is supported\n");
		return -EINVAL;
	}

	return count;
}
