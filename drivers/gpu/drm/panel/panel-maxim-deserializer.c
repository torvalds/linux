// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/of.h>

#include <video/videomode.h>
#include <video/of_display_timing.h>
#include <video/display_timing.h>
#include <uapi/linux/media-bus-format.h>

#include <drm/drm_device.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct maxim_deserializer_panel;

struct maxim_deserializer_panel_desc {
	const char *name;
	u16 id;
	struct drm_display_mode mode;
	u32 width_mm;
	u32 height_mm;

	struct {
		const char *name;
		u8 addr;
		u8 dev_id;
	} deserializer;

	void (*prepare)(struct maxim_deserializer_panel *p);
	void (*unprepare)(struct maxim_deserializer_panel *p);
	void (*enable)(struct maxim_deserializer_panel *p);
	void (*disable)(struct maxim_deserializer_panel *p);
};

struct maxim_deserializer_panel {
	struct drm_panel panel;
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct gpio_desc *enable_gpio;
	struct pinctrl *pinctrl;
	struct pinctrl_state *state;

	/* the panel desc as detected */
	const struct maxim_deserializer_panel_desc *desc;
};

static void maxim_max96752f_panel_prepare(struct maxim_deserializer_panel *p)
{
	regmap_write(p->regmap, 0x0002, 0x43);
	regmap_write(p->regmap, 0x0140, 0x20);

	regmap_write(p->regmap, 0x01ce, 0x5e);	/* oldi */
	regmap_write(p->regmap, 0x020c, 0x84);	/* bl_pwm */
	regmap_write(p->regmap, 0x0206, 0x83);	/* tp_int */

	regmap_write(p->regmap, 0x0215, 0x90);	/* lcd_en */
	msleep(20);
}

static void maxim_max96752f_panel_unprepare(struct maxim_deserializer_panel *p)
{
	regmap_write(p->regmap, 0x0215, 0x80);	/* lcd_en */
}

static void maxim_max96752f_panel_enable(struct maxim_deserializer_panel *p)
{
	regmap_write(p->regmap, 0x0227, 0x90);	/* lcd_rst */
	msleep(20);
	regmap_write(p->regmap, 0x020f, 0x90);	/* tp_rst */
	msleep(100);
	regmap_write(p->regmap, 0x0221, 0x90);	/* lcd_stb */
	msleep(60);
	regmap_write(p->regmap, 0x0212, 0x90);	/* bl_current_ctl */
	regmap_write(p->regmap, 0x0209, 0x90);	/* bl_en */
}

static void maxim_max96752f_panel_disable(struct maxim_deserializer_panel *p)
{
	regmap_write(p->regmap, 0x0209, 0x80);  /* bl_en */
	regmap_write(p->regmap, 0x0212, 0x80);	/* bl_current_ctl */
	regmap_write(p->regmap, 0x0221, 0x80);	/* lcd_stb */
	regmap_write(p->regmap, 0x020f, 0x80);	/* tp_rst */
	regmap_write(p->regmap, 0x0227, 0x80);	/* lcd_rst */
}

static const struct maxim_deserializer_panel_desc maxim_deserializer_default_panels[] = {
	{
		.deserializer = {
			.name = "max96752f",
			.addr = 0x48,
			.dev_id = 0x82,
		},

		.mode = {
			.clock = 148500,
			.hdisplay = 1920,
			.hsync_start = 1920 + 20,
			.hsync_end = 1920 + 20 + 20,
			.htotal = 1920 + 20 + 20 + 20,
			.vdisplay = 1080,
			.vsync_start = 1080 + 250,
			.vsync_end = 1080 + 250 + 2,
			.vtotal = 1080 + 250 + 2 + 8,
			.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
		},

		.prepare = maxim_max96752f_panel_prepare,
		.enable = maxim_max96752f_panel_enable,
		.disable = maxim_max96752f_panel_disable,
		.unprepare = maxim_max96752f_panel_unprepare,
	},
};

static inline struct maxim_deserializer_panel *
to_maxim_deserializer_panel(struct drm_panel *panel)
{
	return container_of(panel, struct maxim_deserializer_panel, panel);
}

static int maxim_deserializer_panel_disable(struct drm_panel *panel)
{
	struct maxim_deserializer_panel *p = to_maxim_deserializer_panel(panel);

	if (!p->desc)
		return 0;

	if (p->desc->disable)
		p->desc->disable(p);

	return 0;
}

static int maxim_deserializer_panel_enable(struct drm_panel *panel)
{
	struct maxim_deserializer_panel *p = to_maxim_deserializer_panel(panel);

	if (!p->desc)
		return 0;

	if (p->desc->enable)
		p->desc->enable(p);

	return 0;
}

static int maxim_deserializer_panel_unprepare(struct drm_panel *panel)
{
	struct maxim_deserializer_panel *p = to_maxim_deserializer_panel(panel);

	if (!p->desc)
		return 0;

	if (p->desc->unprepare)
		p->desc->unprepare(p);

	return 0;
}

static int maxim_deserializer_panel_prepare(struct drm_panel *panel)
{
	struct maxim_deserializer_panel *p = to_maxim_deserializer_panel(panel);

	if (!p->desc)
		return 0;

	if (!IS_ERR(p->state))
		pinctrl_select_state(p->pinctrl, p->state);

	if (p->desc->prepare)
		p->desc->prepare(p);

	return 0;
}

static int maxim_deserializer_panel_get_id(struct maxim_deserializer_panel *p)
{
	/* TODO */
	return 0;
}

static int maxim_deserializer_panel_detect(struct maxim_deserializer_panel *p)
{
	struct i2c_client *client = p->client;
	const struct maxim_deserializer_panel_desc *desc = NULL;
	int id = maxim_deserializer_panel_get_id(p);
	int i, ret;

	if (id) {
		/* TODO */
	} else {
		u32 dev_id;

		for (i = 0; i < ARRAY_SIZE(maxim_deserializer_default_panels); i++) {
			client->addr = maxim_deserializer_default_panels[i].deserializer.addr;

			ret = regmap_read(p->regmap, 0x000d, &dev_id);
			if (ret < 0)
				continue;

			if (maxim_deserializer_default_panels[i].deserializer.dev_id == dev_id) {
				desc = &maxim_deserializer_default_panels[i];
				break;
			}
		}
	}

	if (!desc)
		return -ENODEV;

	p->desc = desc;
	p->state = pinctrl_lookup_state(p->pinctrl,
			desc->name ? desc->name : desc->deserializer.name);
	client->addr = desc->deserializer.addr;

	return 0;
}

static int maxim_deserializer_panel_get_modes(struct drm_panel *panel,
					      struct drm_connector *connector)
{
	struct maxim_deserializer_panel *p = to_maxim_deserializer_panel(panel);
	struct drm_display_mode *mode;
	int ret;

	ret = maxim_deserializer_panel_detect(p);
	if (ret)
		return 0;

	connector->display_info.width_mm = p->desc->width_mm;
	connector->display_info.height_mm = p->desc->height_mm;

	mode = drm_mode_duplicate(connector->dev, &p->desc->mode);
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	mode->width_mm = p->desc->width_mm;
	mode->height_mm = p->desc->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs maxim_deserializer_panel_funcs = {
	.disable = maxim_deserializer_panel_disable,
	.unprepare = maxim_deserializer_panel_unprepare,
	.prepare = maxim_deserializer_panel_prepare,
	.enable = maxim_deserializer_panel_enable,
	.get_modes = maxim_deserializer_panel_get_modes,
};

static void maxim_deserializer_panel_power_on(struct maxim_deserializer_panel *p)
{
	if (p->enable_gpio) {
		gpiod_direction_output(p->enable_gpio, 1);
		msleep(500);
	}
}

static void maxim_deserializer_panel_power_off(struct maxim_deserializer_panel *p)
{
	if (p->enable_gpio)
		gpiod_direction_output(p->enable_gpio, 0);
}

static const struct regmap_range maxim_deserializer_readable_ranges[] = {
	regmap_reg_range(0x0000, 0x0003),
	regmap_reg_range(0x000d, 0x000e),
	regmap_reg_range(0x0010, 0x0010),
	regmap_reg_range(0x0013, 0x0013),
	regmap_reg_range(0x0140, 0x0140),
	regmap_reg_range(0x01cd, 0x01d4),
	regmap_reg_range(0x0200, 0x022f),
};

static const struct regmap_access_table maxim_deserializer_readable_table = {
	.yes_ranges = maxim_deserializer_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(maxim_deserializer_readable_ranges),
};

static const struct regmap_config maxim_deserializer_regmap_config = {
	.name = "maxim-deserializer-panel",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xffff,
	.rd_table = &maxim_deserializer_readable_table,
};

static int maxim_deserializer_panel_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct maxim_deserializer_panel *p;
	int ret;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->dev = dev;
	p->client = client;
	i2c_set_clientdata(client, p);

	p->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(p->pinctrl))
		return dev_err_probe(dev, PTR_ERR(p->pinctrl),
				     "failed to get pinctrl\n");

	p->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_ASIS);
	if (IS_ERR(p->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(p->enable_gpio),
				     "failed to get enable GPIO\n");

	maxim_deserializer_panel_power_on(p);

	p->regmap = devm_regmap_init_i2c(client,
					 &maxim_deserializer_regmap_config);
	if (IS_ERR(p->regmap))
		return dev_err_probe(dev, PTR_ERR(p->regmap),
				     "failed to initialize regmap\n");

	drm_panel_init(&p->panel, dev, &maxim_deserializer_panel_funcs,
		       DRM_MODE_CONNECTOR_LVDS);

	ret = drm_panel_of_backlight(&p->panel);
	if (ret)
		return ret;

	drm_panel_add(&p->panel);

	return 0;
}

static int maxim_deserializer_panel_remove(struct i2c_client *client)
{
	struct maxim_deserializer_panel *p = i2c_get_clientdata(client);

	drm_panel_remove(&p->panel);

	return 0;
}

static int __maybe_unused maxim_deserializer_panel_suspend(struct device *dev)
{
	struct maxim_deserializer_panel *p = dev_get_drvdata(dev);

	maxim_deserializer_panel_power_off(p);

	return 0;
}

static int __maybe_unused maxim_deserializer_panel_resume(struct device *dev)
{
	struct maxim_deserializer_panel *p = dev_get_drvdata(dev);

	maxim_deserializer_panel_power_on(p);

	return 0;
}

static void maxim_deserializer_panel_shutdown(struct i2c_client *client)
{
	struct maxim_deserializer_panel *p = i2c_get_clientdata(client);

	maxim_deserializer_panel_power_off(p);
}

static SIMPLE_DEV_PM_OPS(maxim_deserializer_panel_pm_ops,
			 maxim_deserializer_panel_suspend,
			 maxim_deserializer_panel_resume);

static const struct of_device_id maxim_deserializer_panel_of_match[] = {
	{ .compatible = "maxim,deserializer-panel" },
	{ }
};
MODULE_DEVICE_TABLE(of, maxim_deserializer_panel_of_match);

static struct i2c_driver maxim_deserializer_panel_driver = {
	.driver = {
		.name = "maxim-deserializer-panel",
		.of_match_table = maxim_deserializer_panel_of_match,
		.pm = &maxim_deserializer_panel_pm_ops,
	},
	.probe_new = maxim_deserializer_panel_probe,
	.remove = maxim_deserializer_panel_remove,
	.shutdown = maxim_deserializer_panel_shutdown,
};

module_i2c_driver(maxim_deserializer_panel_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Maxim deserializer panel driver");
MODULE_LICENSE("GPL");
