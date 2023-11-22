// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * serdes-bridge.c  --  drm bridge access for different serdes chips
 *
 * Copyright (c) 2023-2028 Rockchip Electronics Co. Ltd.
 *
 * Author: luowei <lw@rock-chips.com>
 */

#include "core.h"

static struct serdes_bridge_split *to_serdes_bridge_split(struct drm_bridge *bridge)
{
	return container_of(bridge, struct serdes_bridge_split, base_bridge);
}

static struct mipi_dsi_device *serdes_attach_dsi(struct serdes_bridge_split *serdes_bridge_split,
						 struct device_node *remote_node)
{
	struct mipi_dsi_device_info info = { "serdes", 0, NULL };
	struct serdes *serdes = serdes_bridge_split->parent;
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	int ret;

	if (serdes->chip_data->name)
		memcpy(&info.type, serdes->chip_data->name, ARRAY_SIZE(info.type));

	SERDES_DBG_MFD("%s: type=%s, name=%s\n", __func__,
		       info.type, serdes->chip_data->name);

	host = of_find_mipi_dsi_host_by_node(remote_node);
	if (!host) {
		dev_err(serdes_bridge_split->dev, "failed to find serdes dsi host\n");
		return ERR_PTR(-EPROBE_DEFER);
	}

	dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dsi)) {
		dev_err(serdes_bridge_split->dev, "failed to create serdes dsi device\n");
		return dsi;
	}

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;

	if (serdes->chip_data->name) {
		if ((!strcmp(serdes->chip_data->name, "bu18tl82")) ||
		    (!strcmp(serdes->chip_data->name, "bu18rl82"))) {
			dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
					  MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET;
			SERDES_DBG_MFD("%s: %s dsi_mode MIPI_DSI_MODE_VIDEO_BURST 0x%lx\n",
				       __func__, serdes->chip_data->name, dsi->mode_flags);
		} else {
			dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
			SERDES_DBG_MFD("%s: %s dsi_mode MIPI_DSI_MODE_VIDEO_SYNC_PULSE 0x%lx\n",
			       __func__, serdes->chip_data->name, dsi->mode_flags);
		}
	} else {
		dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
		SERDES_DBG_MFD("%s: %s dsi_mode MIPI_DSI_MODE_VIDEO_SYNC_PULSE 0x%lx\n",
			   __func__, serdes->chip_data->name, dsi->mode_flags);
	}

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(serdes_bridge_split->dev, "failed to attach serdes dsi to host\n");
		mipi_dsi_device_unregister(dsi);
		return ERR_PTR(ret);
	}

	return dsi;
}

static int serdes_bridge_split_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	struct serdes_bridge_split *serdes_bridge_split = to_serdes_bridge_split(bridge);
	struct serdes *serdes = serdes_bridge_split->parent;
	int ret = 0;

	ret = drm_of_find_panel_or_bridge(bridge->of_node, 1, -1,
					  &serdes_bridge_split->panel,
					  &serdes_bridge_split->next_bridge);
	if (ret) {
		dev_err(serdes_bridge_split->dev->parent,
			"failed to find serdes bridge, ret=%d\n", ret);
		return ret;
	}

	if (serdes_bridge_split->sel_mipi) {
		dev_info(serdes_bridge_split->dev->parent, "serdes sel_mipi %d\n",
			 serdes_bridge_split->sel_mipi);
		/* Attach primary DSI */
		serdes_bridge_split->dsi = serdes_attach_dsi(serdes_bridge_split,
							     serdes_bridge_split->remote_node);
		if (IS_ERR(serdes_bridge_split->dsi))
			return PTR_ERR(serdes_bridge_split->dsi);
	}

	if (serdes_bridge_split->next_bridge) {
		ret = drm_bridge_attach(bridge->encoder, serdes_bridge_split->next_bridge,
					bridge, flags);
		if (ret) {
			if (serdes_bridge_split->sel_mipi)
				mipi_dsi_device_unregister(serdes_bridge_split->dsi);

			dev_err(serdes_bridge_split->dev->parent,
				"failed to attach bridge, ret=%d\n", ret);
			return ret;
		}
	}

	if (serdes->chip_data->bridge_ops->attach)
		ret = serdes->chip_data->bridge_ops->attach(serdes);

	SERDES_DBG_MFD("%s: ret=%d\n", __func__, ret);

	return ret;
}

static void serdes_bridge_split_detach(struct drm_bridge *bridge)
{
	struct serdes_bridge_split *serdes_bridge_split = to_serdes_bridge_split(bridge);

	if (serdes_bridge_split->sel_mipi) {
		mipi_dsi_detach(serdes_bridge_split->dsi);
		mipi_dsi_device_unregister(serdes_bridge_split->dsi);
	}

	SERDES_DBG_MFD("%s\n", __func__);
}

static void serdes_bridge_split_disable(struct drm_bridge *bridge)
{
	struct serdes_bridge_split *serdes_bridge_split = to_serdes_bridge_split(bridge);
	struct serdes *serdes = serdes_bridge_split->parent;
	int ret = 0;

	if (serdes_bridge_split->panel)
		drm_panel_disable(serdes_bridge_split->panel);

	if (serdes->chip_data->bridge_ops->disable)
		ret = serdes->chip_data->bridge_ops->disable(serdes);

	extcon_set_state_sync(serdes->extcon, EXTCON_JACK_VIDEO_OUT, false);

	SERDES_DBG_MFD("%s: ret=%d\n", __func__, ret);
}

static void serdes_bridge_split_post_disable(struct drm_bridge *bridge)
{
	struct serdes_bridge_split *serdes_bridge_split = to_serdes_bridge_split(bridge);
	struct serdes *serdes = serdes_bridge_split->parent;
	int ret = 0;

	serdes_set_pinctrl_sleep(serdes);

	if (serdes_bridge_split->panel)
		ret = drm_panel_unprepare(serdes_bridge_split->panel);

	if (serdes->chip_data->bridge_ops->post_disable)
		ret = serdes->chip_data->bridge_ops->post_disable(serdes);

	SERDES_DBG_MFD("%s: ret=%d\n", __func__, ret);
}

static void serdes_bridge_split_pre_enable(struct drm_bridge *bridge)
{
	struct serdes_bridge_split *serdes_bridge_split = to_serdes_bridge_split(bridge);
	struct serdes *serdes = serdes_bridge_split->parent;
	int ret = 0;

	if (serdes->chip_data->bridge_ops->init)
		ret = serdes->chip_data->bridge_ops->init(serdes);

	if (serdes->chip_data->serdes_type == TYPE_DES) {
		if (serdes->chip_data->chip_init)
			serdes->chip_data->chip_init(serdes);
		ret = serdes_i2c_set_sequence(serdes);
	}

	if (serdes->chip_data->bridge_ops->pre_enable)
		ret = serdes->chip_data->bridge_ops->pre_enable(serdes);

	if (serdes_bridge_split->panel)
		ret = drm_panel_prepare(serdes_bridge_split->panel);

	serdes_set_pinctrl_default(serdes);

	SERDES_DBG_MFD("%s: %s ret=%d\n", __func__, dev_name(serdes->dev), ret);
}

static void serdes_bridge_split_enable(struct drm_bridge *bridge)
{
	struct serdes_bridge_split *serdes_bridge_split = to_serdes_bridge_split(bridge);
	struct serdes *serdes = serdes_bridge_split->parent;
	int ret = 0;

	if (serdes_bridge_split->panel)
		ret = drm_panel_enable(serdes_bridge_split->panel);

	if (serdes->chip_data->bridge_ops->enable)
		ret = serdes->chip_data->bridge_ops->enable(serdes);

	if (!ret) {
		extcon_set_state_sync(serdes->extcon, EXTCON_JACK_VIDEO_OUT, true);
		SERDES_DBG_MFD("%s: extcon is true\n", __func__);
	}

	SERDES_DBG_MFD("%s: %s-%s ret=%d\n", __func__, dev_name(serdes->dev),
		       serdes->chip_data->name, ret);
}

static enum drm_connector_status
serdes_bridge_split_detect(struct drm_bridge *bridge)
{
	struct serdes_bridge_split *serdes_bridge_split = to_serdes_bridge_split(bridge);
	struct serdes *serdes = serdes_bridge_split->parent;
	enum drm_connector_status status = connector_status_connected;

	if (serdes->chip_data->bridge_ops->detect)
		status = serdes->chip_data->bridge_ops->detect(serdes);

	return status;
}

static int serdes_bridge_split_get_modes(struct drm_bridge *bridge,
				   struct drm_connector *connector)
{
	struct serdes_bridge_split *serdes_bridge_split = to_serdes_bridge_split(bridge);
	struct serdes *serdes = serdes_bridge_split->parent;
	int ret = 0;

	if (serdes->chip_data->bridge_ops->get_modes)
		ret = serdes->chip_data->bridge_ops->get_modes(serdes);

	if (serdes_bridge_split->next_bridge)
		ret = drm_bridge_get_modes(serdes_bridge_split->next_bridge, connector);

	if (serdes_bridge_split->panel)
		ret = drm_panel_get_modes(serdes_bridge_split->panel, connector);

	SERDES_DBG_MFD("%s:name=%s, node=%s\n", __func__,
		       serdes->chip_data->name, serdes_bridge_split->dev->of_node->name);

	return ret;
}

static const struct drm_bridge_funcs serdes_bridge_split_funcs = {
	.attach = serdes_bridge_split_attach,
	.detach = serdes_bridge_split_detach,
	.disable = serdes_bridge_split_disable,
	.post_disable = serdes_bridge_split_post_disable,
	.pre_enable = serdes_bridge_split_pre_enable,
	.enable = serdes_bridge_split_enable,
	.detect = serdes_bridge_split_detect,
	.get_modes = serdes_bridge_split_get_modes,
	.atomic_get_input_bus_fmts = drm_atomic_helper_bridge_propagate_bus_fmt,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

static int serdes_bridge_split_probe(struct platform_device *pdev)
{
	struct serdes *serdes = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct serdes_bridge_split *serdes_bridge_split;

	if (!serdes->dev)
		return -1;

	serdes_bridge_split = devm_kzalloc(dev, sizeof(*serdes_bridge_split), GFP_KERNEL);
	if (!serdes_bridge_split)
		return -ENOMEM;

	serdes->serdes_bridge_split = serdes_bridge_split;
	serdes_bridge_split->dev = dev;
	serdes_bridge_split->parent = dev_get_drvdata(dev->parent);
	platform_set_drvdata(pdev, serdes_bridge_split);
	serdes_bridge_split->regmap = dev_get_regmap(dev->parent, NULL);
	if (!serdes_bridge_split->regmap)
		return dev_err_probe(dev, -ENODEV, "failed to get serdes regmap\n");

	serdes_bridge_split->sel_mipi = of_property_read_bool(dev->parent->of_node, "sel-mipi");
	SERDES_DBG_MFD("%s: sel_mipi=%d\n", __func__, serdes_bridge_split->sel_mipi);

	serdes_bridge_split->base_bridge.of_node = dev->parent->of_node;
	serdes_bridge_split->remote_node = of_graph_get_remote_node(dev->parent->of_node, 0, -1);
	if (!serdes_bridge_split->remote_node) {
		serdes_bridge_split->base_bridge.of_node = dev->of_node;
		SERDES_DBG_MFD("warning: failed to get remote node for serdes on %s\n",
			       dev_name(dev->parent));
		serdes_bridge_split->remote_node = of_graph_get_remote_node(dev->of_node, 0, -1);
		if (!serdes_bridge_split->remote_node) {
			return dev_err_probe(dev, -ENODEV,
				     "failed to get remote node for serdes dsi\n");
		}
	}

	serdes_bridge_split->base_bridge.funcs = &serdes_bridge_split_funcs;
	serdes_bridge_split->base_bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_MODES;

	if (serdes_bridge_split->sel_mipi) {
		serdes_bridge_split->base_bridge.type = DRM_MODE_CONNECTOR_DSI;
		SERDES_DBG_MFD("%s: type DRM_MODE_CONNECTOR_DSI\n", __func__);
	} else if (serdes_bridge_split->parent->chip_data->connector_type) {
		serdes_bridge_split->base_bridge.type =
			serdes_bridge_split->parent->chip_data->connector_type;
		SERDES_DBG_MFD("%s: type %d\n", __func__, serdes_bridge_split->base_bridge.type);
	} else {
		serdes_bridge_split->base_bridge.type = DRM_MODE_CONNECTOR_eDP;
		SERDES_DBG_MFD("%s: type DRM_MODE_CONNECTOR_LVDS\n", __func__);
	}

	drm_bridge_add(&serdes_bridge_split->base_bridge);

	dev_info(dev, "serdes %s, %s successful mipi=%d, of_node=%s\n",
		 serdes->chip_data->name, __func__, serdes_bridge_split->sel_mipi,
		 serdes_bridge_split->base_bridge.of_node->name);

	return 0;
}

static int serdes_bridge_split_remove(struct platform_device *pdev)
{
	struct serdes_bridge_split *serdes_bridge_split = platform_get_drvdata(pdev);

	drm_bridge_remove(&serdes_bridge_split->base_bridge);

	return 0;
}

static const struct of_device_id serdes_bridge_split_of_match[] = {
	{ .compatible = "rohm,bu18tl82-bridge-split", },
	{ .compatible = "rohm,bu18rl82-bridge-split", },
	{ .compatible = "maxim,max96745-bridge-split", },
	{ .compatible = "maxim,max96755-bridge-split", },
	{ .compatible = "maxim,max96752-bridge-split", },
	{ .compatible = "maxim,max96789-bridge-split", },
	{ .compatible = "rockchip,rkx111-bridge-split", },
	{ .compatible = "rockchip,rkx121-bridge-split", },
	{ }
};

static struct platform_driver serdes_bridge_split_driver = {
	.driver = {
		.name = "serdes-bridge-split",
		.of_match_table = of_match_ptr(serdes_bridge_split_of_match),
	},
	.probe = serdes_bridge_split_probe,
	.remove = serdes_bridge_split_remove,
};

static int __init serdes_bridge_split_init(void)
{
	return platform_driver_register(&serdes_bridge_split_driver);
}
device_initcall(serdes_bridge_split_init);

static void __exit serdes_bridge_split_exit(void)
{
	platform_driver_unregister(&serdes_bridge_split_driver);
}
module_exit(serdes_bridge_split_exit);

MODULE_AUTHOR("Luo Wei <lw@rock-chips.com>");
MODULE_DESCRIPTION("display bridge interface for different serdes");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:serdes-bridge-split");
