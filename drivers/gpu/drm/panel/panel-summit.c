// SPDX-License-Identifier: GPL-2.0-only

#include <linux/backlight.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_mode.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <video/mipi_display.h>

struct summit_data {
	struct mipi_dsi_device *dsi;
	struct backlight_device *bl;
	struct drm_panel panel;
};

static int summit_set_brightness(struct device *dev)
{
	struct summit_data *s_data = dev_get_drvdata(dev);
	int level = backlight_get_brightness(s_data->bl);

	return mipi_dsi_dcs_set_display_brightness(s_data->dsi, level);
}

static int summit_bl_update_status(struct backlight_device *dev)
{
	return summit_set_brightness(&dev->dev);
}

static const struct backlight_ops summit_bl_ops = {
	.update_status	= summit_bl_update_status,
};

static struct drm_display_mode summit_mode = {
	.vdisplay = 2008,
	.hdisplay = 60,
	.hsync_start = 60 + 8,
	.hsync_end = 60 + 8 + 80,
	.htotal = 60 + 8 + 80 + 40,
	.vsync_start = 2008 + 1,
	.vsync_end = 2008 + 1 + 15,
	.vtotal = 2008 + 1 + 15 + 6,
	.clock = ((60 + 8 + 80 + 40) * (2008 + 1 + 15 + 6) * 60) / 1000,
	.type = DRM_MODE_TYPE_DRIVER,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static int summit_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	connector->display_info.non_desktop = true;
	drm_object_property_set_value(&connector->base,
				      connector->dev->mode_config.non_desktop_property,
				      connector->display_info.non_desktop);

	return drm_connector_helper_get_modes_fixed(connector, &summit_mode);
}

static const struct drm_panel_funcs summit_panel_funcs = {
	.get_modes = summit_get_modes,
};

static int summit_probe(struct mipi_dsi_device *dsi)
{
	struct backlight_properties props = { 0 };
	struct device *dev = &dsi->dev;
	struct summit_data *s_data;
	int ret;

	s_data = devm_drm_panel_alloc(dev, struct summit_data, panel,
				      &summit_panel_funcs,
				      DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(s_data))
		return PTR_ERR(s_data);

	mipi_dsi_set_drvdata(dsi, s_data);
	s_data->dsi = dsi;

	ret = device_property_read_u32(dev, "max-brightness", &props.max_brightness);
	if (ret)
		return ret;
	props.type = BACKLIGHT_RAW;

	s_data->bl = devm_backlight_device_register(dev, dev_name(dev),
						    dev, s_data, &summit_bl_ops, &props);
	if (IS_ERR(s_data->bl))
		return PTR_ERR(s_data->bl);

	drm_panel_add(&s_data->panel);

	return mipi_dsi_attach(dsi);
}

static void summit_remove(struct mipi_dsi_device *dsi)
{
	struct summit_data *s_data = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&s_data->panel);
}

static int summit_suspend(struct device *dev)
{
	struct summit_data *s_data = dev_get_drvdata(dev);

	return mipi_dsi_dcs_set_display_brightness(s_data->dsi, 0);
}

static DEFINE_SIMPLE_DEV_PM_OPS(summit_pm_ops, summit_suspend,
				summit_set_brightness);

static const struct of_device_id summit_of_match[] = {
	{ .compatible = "apple,summit" },
	{},
};

MODULE_DEVICE_TABLE(of, summit_of_match);

static struct mipi_dsi_driver summit_driver = {
	.probe = summit_probe,
	.remove = summit_remove,
	.driver = {
		.name = "panel-summit",
		.of_match_table = summit_of_match,
		.pm = pm_sleep_ptr(&summit_pm_ops),
	},
};
module_mipi_dsi_driver(summit_driver);

MODULE_DESCRIPTION("Summit Display Panel Driver");
MODULE_LICENSE("GPL");
