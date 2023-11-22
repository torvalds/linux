// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * serdes-panel.c  --  drm panel access for different serdes chips
 *
 * Copyright (c) 2023-2028 Rockchip Electronics Co. Ltd.
 *
 * Author: luowei <lw@rock-chips.com>
 */

#include "core.h"

static inline struct serdes_panel *to_serdes_panel(struct drm_panel *panel)
{
	return container_of(panel, struct serdes_panel, panel);
}

static int serdes_panel_prepare(struct drm_panel *panel)
{
	struct serdes_panel *serdes_panel = to_serdes_panel(panel);
	struct serdes *serdes = serdes_panel->parent;
	int ret = 0;

	if (serdes->chip_data->panel_ops && serdes->chip_data->panel_ops->init)
		ret = serdes->chip_data->panel_ops->init(serdes);

	if (serdes->chip_data->serdes_type == TYPE_DES)
		serdes_i2c_set_sequence(serdes);

	if (serdes->chip_data->panel_ops && serdes->chip_data->panel_ops->prepare)
		ret = serdes->chip_data->panel_ops->prepare(serdes);

	serdes_set_pinctrl_default(serdes);

	SERDES_DBG_MFD("%s: %s\n", __func__, serdes->chip_data->name);

	return ret;
}

static int serdes_panel_unprepare(struct drm_panel *panel)
{
	struct serdes_panel *serdes_panel = to_serdes_panel(panel);
	struct serdes *serdes = serdes_panel->parent;
	int ret = 0;

	if (serdes->chip_data->panel_ops && serdes->chip_data->panel_ops->unprepare)
		ret = serdes->chip_data->panel_ops->unprepare(serdes);

	serdes_set_pinctrl_sleep(serdes);

	SERDES_DBG_MFD("%s: %s\n", __func__, serdes->chip_data->name);

	return ret;
}

static int serdes_panel_enable(struct drm_panel *panel)
{
	struct serdes_panel *serdes_panel = to_serdes_panel(panel);
	struct serdes *serdes = serdes_panel->parent;
	int ret = 0;

	if (serdes->chip_data->panel_ops && serdes->chip_data->panel_ops->enable)
		ret = serdes->chip_data->panel_ops->enable(serdes);

	backlight_enable(serdes_panel->backlight);

	SERDES_DBG_MFD("%s: %s\n", __func__, serdes->chip_data->name);

	return ret;
}

static int serdes_panel_disable(struct drm_panel *panel)
{
	struct serdes_panel *serdes_panel = to_serdes_panel(panel);
	struct serdes *serdes = serdes_panel->parent;
	int ret = 0;

	if (serdes->chip_data->panel_ops && serdes->chip_data->panel_ops->disable)
		ret = serdes->chip_data->panel_ops->disable(serdes);

	backlight_disable(serdes_panel->backlight);

	SERDES_DBG_MFD("%s: %s\n", __func__, serdes->chip_data->name);

	return ret;
}

static int serdes_panel_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	struct serdes_panel *serdes_panel = to_serdes_panel(panel);
	struct serdes *serdes = serdes_panel->parent;
	struct drm_display_mode *mode;
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	int ret = 1;

	connector->display_info.width_mm = serdes_panel->width_mm;	//323; //346;
	connector->display_info.height_mm = serdes_panel->height_mm;	//182; //194;
	drm_display_info_set_bus_formats(&connector->display_info, &bus_format, 1);

	mode = drm_mode_duplicate(connector->dev, &serdes_panel->mode);
	mode->width_mm = serdes_panel->width_mm;	//323; //346;
	mode->height_mm = serdes_panel->height_mm;	//182; //194;
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	if (serdes->chip_data->panel_ops && serdes->chip_data->panel_ops->get_modes)
		ret = serdes->chip_data->panel_ops->get_modes(serdes);

	dev_info(serdes->dev, "%s wxh=%dx%d mode clock %u kHz, flags[0x%x]\n"
	       "    H: %04d %04d %04d %04d\n"
	       "    V: %04d %04d %04d %04d\n"
	       "bus_format: 0x%x\n",
	       panel->dev->of_node->name,
	       serdes_panel->width_mm, serdes_panel->height_mm,
	       mode->clock, mode->flags,
	       mode->hdisplay, mode->hsync_start,
	       mode->hsync_end, mode->htotal,
	       mode->vdisplay, mode->vsync_start,
	       mode->vsync_end, mode->vtotal,
	       bus_format);

	return ret;
}

static const struct drm_panel_funcs serdes_panel_funcs = {
	.prepare = serdes_panel_prepare,
	.unprepare = serdes_panel_unprepare,
	.enable = serdes_panel_enable,
	.disable = serdes_panel_disable,
	.get_modes = serdes_panel_get_modes,
};

static int serdes_panel_parse_dt(struct serdes_panel *serdes_panel)
{
	struct device *dev = serdes_panel->dev;
	struct display_timing dt;
	struct videomode vm;
	int ret, len;
	unsigned int panel_size[2] = {320, 180};
	unsigned int link_rate_count_ssc[3] = {DP_LINK_BW_2_7, 4, 0};

	//pr_info("%s: node=%s\n", __func__, dev->of_node->name);

	serdes_panel->width_mm = panel_size[0];
	serdes_panel->height_mm = panel_size[1];

	serdes_panel->link_rate = link_rate_count_ssc[0];
	serdes_panel->lane_count = link_rate_count_ssc[1];
	serdes_panel->ssc = link_rate_count_ssc[2];

	if (of_find_property(dev->of_node, "panel-size", &len)) {
		len /= sizeof(unsigned int);
		if (len != 2) {
			dev_err(dev, "panel-size length is error, set 2 default\n",
				dev->of_node);
			len = 2;
		}
		ret = of_property_read_u32_array(dev->of_node, "panel-size",
						 panel_size, len);
		if (!ret) {
			serdes_panel->width_mm = panel_size[0];
			serdes_panel->height_mm = panel_size[1];
		}
	}

	if (of_find_property(dev->of_node, "rate-count-ssc", &len)) {
		len /= sizeof(unsigned int);
		if (len != 3) {
			dev_err(dev, "rate-count-ssc length is error, set 3 default\n",
				dev->of_node);
			len = 3;
		}
		ret = of_property_read_u32_array(dev->of_node, "rate-count-ssc",
						 link_rate_count_ssc, len);
		if (!ret) {
			serdes_panel->link_rate = link_rate_count_ssc[0];
			serdes_panel->lane_count = link_rate_count_ssc[1];
			serdes_panel->ssc = link_rate_count_ssc[2];
		}
	}

	dev_info(dev, "panle size %dx%d, rate=%d, cnt=%d, ssc=%d\n",
		 serdes_panel->width_mm, serdes_panel->height_mm,
		 serdes_panel->link_rate, serdes_panel->lane_count, serdes_panel->ssc);

	ret = of_get_display_timing(dev->of_node, "panel-timing", &dt);
	if (ret < 0) {
		dev_err(dev, "%pOF:serdes no panel-timing node found\n", dev->of_node);
		return ret;
	}

	videomode_from_timing(&dt, &vm);
	drm_display_mode_from_videomode(&vm, &serdes_panel->mode);

	return 0;
}

static int serdes_panel_probe(struct platform_device *pdev)
{
	struct serdes *serdes = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct serdes_panel *serdes_panel;
	int ret;

	serdes_panel = devm_kzalloc(dev, sizeof(*serdes_panel), GFP_KERNEL);
	if (!serdes_panel)
		return -ENOMEM;

	serdes->serdes_panel = serdes_panel;
	serdes_panel->dev = dev;
	serdes_panel->parent = dev_get_drvdata(dev->parent);
	platform_set_drvdata(pdev, serdes_panel);

	serdes_panel->regmap = dev_get_regmap(dev->parent, NULL);
	if (!serdes_panel->regmap)
		return dev_err_probe(dev, -ENODEV, "failed to get serdes regmap\n");

	ret = serdes_panel_parse_dt(serdes_panel);
	if (ret)
		return dev_err_probe(dev, ret, "failed to parse serdes DT\n");

	serdes_panel->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(serdes_panel->backlight))
		return dev_err_probe(dev, PTR_ERR(serdes_panel->backlight),
				     "failed to get serdes backlight\n");

	if (serdes_panel->parent->chip_data->connector_type) {
		drm_panel_init(&serdes_panel->panel, dev, &serdes_panel_funcs,
			       serdes_panel->parent->chip_data->connector_type);
	} else {
		drm_panel_init(&serdes_panel->panel, dev, &serdes_panel_funcs,
			       DRM_MODE_CONNECTOR_LVDS);
	}
	drm_panel_add(&serdes_panel->panel);

	dev_info(dev, "serdes %s-%s serdes_panel_probe successful\n",
		 dev_name(serdes->dev), serdes->chip_data->name);

	return 0;
}

static int serdes_panel_remove(struct platform_device *pdev)
{
	struct serdes_panel *serdes_panel = platform_get_drvdata(pdev);

	drm_panel_remove(&serdes_panel->panel);

	return 0;
}

static const struct of_device_id serdes_panel_of_match[] = {
	{ .compatible = "rohm,bu18rl82-panel" },
	{ .compatible = "maxim,max96752-panel" },
	{ .compatible = "maxim,max96772-panel" },
	{ .compatible = "rockchip,rkx121-panel" },
	{ }
};

static struct platform_driver serdes_panel_driver = {
	.driver = {
		.name = "serdes-panel",
		.of_match_table = of_match_ptr(serdes_panel_of_match),
	},
	.probe = serdes_panel_probe,
	.remove = serdes_panel_remove,
};

static int __init serdes_panel_init(void)
{
	return platform_driver_register(&serdes_panel_driver);
}
device_initcall(serdes_panel_init);

static void __exit serdes_panel_exit(void)
{
	platform_driver_unregister(&serdes_panel_driver);
}
module_exit(serdes_panel_exit);

MODULE_AUTHOR("Luo Wei <lw@rock-chips.com>");
MODULE_DESCRIPTION("display panel interface for different serdes");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:serdes-panel");
