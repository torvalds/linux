// SPDX-License-Identifier: GPL-2.0+
/*
 * Generic LVDS panel driver
 *
 * Copyright (C) 2016 Laurent Pinchart
 * Copyright (C) 2016 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <video/display_timing.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include <drm/drm_crtc.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>

struct panel_lvds {
	struct drm_panel panel;
	struct device *dev;

	const char *label;
	struct drm_display_mode dmode;
	u32 bus_flags;
	unsigned int bus_format;

	struct regulator *supply;

	struct gpio_desc *enable_gpio;
	struct gpio_desc *reset_gpio;

	enum drm_panel_orientation orientation;
};

static inline struct panel_lvds *to_panel_lvds(struct drm_panel *panel)
{
	return container_of(panel, struct panel_lvds, panel);
}

static int panel_lvds_unprepare(struct drm_panel *panel)
{
	struct panel_lvds *lvds = to_panel_lvds(panel);

	if (lvds->enable_gpio)
		gpiod_set_value_cansleep(lvds->enable_gpio, 0);

	if (lvds->supply)
		regulator_disable(lvds->supply);

	return 0;
}

static int panel_lvds_prepare(struct drm_panel *panel)
{
	struct panel_lvds *lvds = to_panel_lvds(panel);

	if (lvds->supply) {
		int err;

		err = regulator_enable(lvds->supply);
		if (err < 0) {
			dev_err(lvds->dev, "failed to enable supply: %d\n",
				err);
			return err;
		}
	}

	if (lvds->enable_gpio)
		gpiod_set_value_cansleep(lvds->enable_gpio, 1);

	return 0;
}

static int panel_lvds_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	struct panel_lvds *lvds = to_panel_lvds(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &lvds->dmode);
	if (!mode)
		return 0;

	mode->type |= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = lvds->dmode.width_mm;
	connector->display_info.height_mm = lvds->dmode.height_mm;
	drm_display_info_set_bus_formats(&connector->display_info,
					 &lvds->bus_format, 1);
	connector->display_info.bus_flags = lvds->bus_flags;

	/*
	 * TODO: Remove once all drm drivers call
	 * drm_connector_set_orientation_from_panel()
	 */
	drm_connector_set_panel_orientation(connector, lvds->orientation);

	return 1;
}

static enum drm_panel_orientation panel_lvds_get_orientation(struct drm_panel *panel)
{
	struct panel_lvds *lvds = to_panel_lvds(panel);

	return lvds->orientation;
}

static const struct drm_panel_funcs panel_lvds_funcs = {
	.unprepare = panel_lvds_unprepare,
	.prepare = panel_lvds_prepare,
	.get_modes = panel_lvds_get_modes,
	.get_orientation = panel_lvds_get_orientation,
};

static int panel_lvds_parse_dt(struct panel_lvds *lvds)
{
	struct device_node *np = lvds->dev->of_node;
	int ret;

	ret = of_drm_get_panel_orientation(np, &lvds->orientation);
	if (ret < 0) {
		dev_err(lvds->dev, "%pOF: failed to get orientation %d\n", np, ret);
		return ret;
	}

	ret = of_get_drm_panel_display_mode(np, &lvds->dmode, &lvds->bus_flags);
	if (ret < 0) {
		dev_err(lvds->dev, "%pOF: problems parsing panel-timing (%d)\n",
			np, ret);
		return ret;
	}

	of_property_read_string(np, "label", &lvds->label);

	ret = drm_of_lvds_get_data_mapping(np);
	if (ret < 0) {
		dev_err(lvds->dev, "%pOF: invalid or missing %s DT property\n",
			np, "data-mapping");
		return ret;
	}

	lvds->bus_format = ret;

	lvds->bus_flags |= of_property_read_bool(np, "data-mirror") ?
			   DRM_BUS_FLAG_DATA_LSB_TO_MSB :
			   DRM_BUS_FLAG_DATA_MSB_TO_LSB;

	return 0;
}

static int panel_lvds_probe(struct platform_device *pdev)
{
	struct panel_lvds *lvds;
	int ret;

	lvds = devm_drm_panel_alloc(&pdev->dev, struct panel_lvds, panel,
				    &panel_lvds_funcs,
				    DRM_MODE_CONNECTOR_LVDS);
	if (IS_ERR(lvds))
		return PTR_ERR(lvds);

	lvds->dev = &pdev->dev;

	ret = panel_lvds_parse_dt(lvds);
	if (ret < 0)
		return ret;

	lvds->supply = devm_regulator_get_optional(lvds->dev, "power");
	if (IS_ERR(lvds->supply)) {
		ret = PTR_ERR(lvds->supply);

		if (ret != -ENODEV) {
			if (ret != -EPROBE_DEFER)
				dev_err(lvds->dev, "failed to request regulator: %d\n",
					ret);
			return ret;
		}

		lvds->supply = NULL;
	}

	/* Get GPIOs and backlight controller. */
	lvds->enable_gpio = devm_gpiod_get_optional(lvds->dev, "enable",
						     GPIOD_OUT_LOW);
	if (IS_ERR(lvds->enable_gpio)) {
		ret = PTR_ERR(lvds->enable_gpio);
		dev_err(lvds->dev, "failed to request %s GPIO: %d\n",
			"enable", ret);
		return ret;
	}

	lvds->reset_gpio = devm_gpiod_get_optional(lvds->dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(lvds->reset_gpio)) {
		ret = PTR_ERR(lvds->reset_gpio);
		dev_err(lvds->dev, "failed to request %s GPIO: %d\n",
			"reset", ret);
		return ret;
	}

	/*
	 * TODO: Handle all power supplies specified in the DT node in a generic
	 * way for panels that don't care about power supply ordering. LVDS
	 * panels that require a specific power sequence will need a dedicated
	 * driver.
	 */

	ret = drm_panel_of_backlight(&lvds->panel);
	if (ret)
		return ret;

	drm_panel_add(&lvds->panel);

	dev_set_drvdata(lvds->dev, lvds);
	return 0;
}

static void panel_lvds_remove(struct platform_device *pdev)
{
	struct panel_lvds *lvds = platform_get_drvdata(pdev);

	drm_panel_remove(&lvds->panel);

	drm_panel_disable(&lvds->panel);
}

static const struct of_device_id panel_lvds_of_table[] = {
	{ .compatible = "panel-lvds", },
	{ /* Sentinel */ },
};

MODULE_DEVICE_TABLE(of, panel_lvds_of_table);

static struct platform_driver panel_lvds_driver = {
	.probe		= panel_lvds_probe,
	.remove		= panel_lvds_remove,
	.driver		= {
		.name	= "panel-lvds",
		.of_match_table = panel_lvds_of_table,
	},
};

module_platform_driver(panel_lvds_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("LVDS Panel Driver");
MODULE_LICENSE("GPL");
