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

static const struct tilcdc_panel_info panel_info_default = {
		.ac_bias                = 255,
		.ac_bias_intrpt         = 0,
		.dma_burst_sz           = 16,
		.bpp                    = 16,
		.fdd                    = 0x80,
		.tft_alt_mode           = 0,
		.sync_edge              = 0,
		.sync_ctrl              = 1,
		.raster_order           = 0,
};

static int tilcdc_external_mode_valid(struct drm_connector *connector,
				      struct drm_display_mode *mode)
{
	struct tilcdc_drm_private *priv = connector->dev->dev_private;
	int ret;

	ret = tilcdc_crtc_mode_valid(priv->crtc, mode);
	if (ret != MODE_OK)
		return ret;

	BUG_ON(priv->external_connector != connector);
	BUG_ON(!priv->connector_funcs);

	/* If the connector has its own mode_valid call it. */
	if (!IS_ERR(priv->connector_funcs) &&
	    priv->connector_funcs->mode_valid)
		return priv->connector_funcs->mode_valid(connector, mode);

	return MODE_OK;
}

static int tilcdc_add_external_connector(struct drm_device *dev,
					 struct drm_connector *connector)
{
	struct tilcdc_drm_private *priv = dev->dev_private;
	struct drm_connector_helper_funcs *connector_funcs;

	/* There should never be more than one connector */
	if (WARN_ON(priv->external_connector))
		return -EINVAL;

	priv->external_connector = connector;
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
		priv->connector_funcs =	connector->helper_private;
		*connector_funcs = *priv->connector_funcs;
	} else {
		priv->connector_funcs = ERR_PTR(-ENOENT);
	}
	connector_funcs->mode_valid = tilcdc_external_mode_valid;
	drm_connector_helper_add(connector, connector_funcs);

	dev_dbg(dev->dev, "External connector '%s' connected\n",
		connector->name);

	return 0;
}

static
struct drm_connector *tilcdc_encoder_find_connector(struct drm_device *ddev,
						    struct drm_encoder *encoder)
{
	struct drm_connector *connector;
	int i;

	list_for_each_entry(connector, &ddev->mode_config.connector_list, head)
		for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++)
			if (connector->encoder_ids[i] == encoder->base.id)
				return connector;

	dev_err(ddev->dev, "No connector found for %s encoder (id %d)\n",
		encoder->name, encoder->base.id);

	return NULL;
}

int tilcdc_add_component_encoder(struct drm_device *ddev)
{
	struct tilcdc_drm_private *priv = ddev->dev_private;
	struct drm_connector *connector;
	struct drm_encoder *encoder;

	list_for_each_entry(encoder, &ddev->mode_config.encoder_list, head)
		if (encoder->possible_crtcs & (1 << priv->crtc->index))
			break;

	if (!encoder) {
		dev_err(ddev->dev, "%s: No suitable encoder found\n", __func__);
		return -ENODEV;
	}

	connector = tilcdc_encoder_find_connector(ddev, encoder);

	if (!connector)
		return -ENODEV;

	/* Only tda998x is supported at the moment. */
	tilcdc_crtc_set_simulate_vesa_sync(priv->crtc, true);
	tilcdc_crtc_set_panel_info(priv->crtc, &panel_info_tda998x);

	return tilcdc_add_external_connector(ddev, connector);
}

void tilcdc_remove_external_device(struct drm_device *dev)
{
	struct tilcdc_drm_private *priv = dev->dev_private;

	/* Restore the original helper functions, if any. */
	if (IS_ERR(priv->connector_funcs))
		drm_connector_helper_add(priv->external_connector, NULL);
	else if (priv->connector_funcs)
		drm_connector_helper_add(priv->external_connector,
					 priv->connector_funcs);
}

static const struct drm_encoder_funcs tilcdc_external_encoder_funcs = {
	.destroy	= drm_encoder_cleanup,
};

static
int tilcdc_attach_bridge(struct drm_device *ddev, struct drm_bridge *bridge)
{
	struct tilcdc_drm_private *priv = ddev->dev_private;
	struct drm_connector *connector;
	int ret;

	priv->external_encoder->possible_crtcs = BIT(0);

	ret = drm_bridge_attach(priv->external_encoder, bridge, NULL);
	if (ret) {
		dev_err(ddev->dev, "drm_bridge_attach() failed %d\n", ret);
		return ret;
	}

	tilcdc_crtc_set_panel_info(priv->crtc, &panel_info_default);

	connector = tilcdc_encoder_find_connector(ddev, priv->external_encoder);
	if (!connector)
		return -ENODEV;

	ret = tilcdc_add_external_connector(ddev, connector);

	return ret;
}

int tilcdc_attach_external_device(struct drm_device *ddev)
{
	struct tilcdc_drm_private *priv = ddev->dev_private;
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	int ret;

	ret = drm_of_find_panel_or_bridge(ddev->dev->of_node, 0, 0,
					  &panel, &bridge);
	if (ret == -ENODEV)
		return 0;
	else if (ret)
		return ret;

	priv->external_encoder = devm_kzalloc(ddev->dev,
					      sizeof(*priv->external_encoder),
					      GFP_KERNEL);
	if (!priv->external_encoder)
		return -ENOMEM;

	ret = drm_encoder_init(ddev, priv->external_encoder,
			       &tilcdc_external_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret) {
		dev_err(ddev->dev, "drm_encoder_init() failed %d\n", ret);
		return ret;
	}

	if (panel) {
		bridge = devm_drm_panel_bridge_add(ddev->dev, panel,
						   DRM_MODE_CONNECTOR_DPI);
		if (IS_ERR(bridge)) {
			ret = PTR_ERR(bridge);
			goto err_encoder_cleanup;
		}
	}

	ret = tilcdc_attach_bridge(ddev, bridge);
	if (ret)
		goto err_encoder_cleanup;

	return 0;

err_encoder_cleanup:
	drm_encoder_cleanup(priv->external_encoder);
	return ret;
}

static int dev_match_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

int tilcdc_get_external_components(struct device *dev,
				   struct component_match **match)
{
	struct device_node *node;

	node = of_graph_get_remote_node(dev->of_node, 0, 0);

	if (!of_device_is_compatible(node, "nxp,tda998x")) {
		of_node_put(node);
		return 0;
	}

	if (match)
		drm_of_component_match_add(dev, match, dev_match_of, node);
	of_node_put(node);
	return 1;
}
