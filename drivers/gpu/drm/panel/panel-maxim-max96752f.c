// SPDX-License-Identifier: GPL-2.0+
/*
 * Maxim MAX96752F GMSL2 Deserializer
 *
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>

#include <video/videomode.h>
#include <video/of_display_timing.h>
#include <video/display_timing.h>
#include <uapi/linux/media-bus-format.h>

#include <drm/drm_device.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct max96752f;

struct panel_desc {
	const char *name;
	u32 width_mm;
	u32 height_mm;

	int (*prepare)(struct max96752f *max96752f);
	int (*unprepare)(struct max96752f *max96752f);
	int (*enable)(struct max96752f *max96752f);
	int (*disable)(struct max96752f *max96752f);
	int (*backlight_enable)(struct max96752f *max96752f);
	int (*backlight_disable)(struct max96752f *max96752f);
};

struct max96752f {
	struct drm_panel panel;
	struct device *dev;
	struct {
		struct regmap *serializer;
		struct regmap *deserializer;
	} regmap;
	struct regulator *supply;
	struct backlight_device *backlight;
	struct drm_display_mode mode;
	const struct panel_desc *desc;
};

static inline struct max96752f *to_max96752f(struct drm_panel *panel)
{
	return container_of(panel, struct max96752f, panel);
}

static int max96752f_panel_disable(struct drm_panel *panel)
{
	struct max96752f *max96752f = to_max96752f(panel);
	const struct panel_desc *desc = max96752f->desc;

	if (desc->backlight_disable)
		desc->backlight_disable(max96752f);

	backlight_disable(max96752f->backlight);

	if (desc->disable)
		desc->disable(max96752f);

	return 0;
}

static int max96752f_panel_enable(struct drm_panel *panel)
{
	struct max96752f *max96752f = to_max96752f(panel);
	const struct panel_desc *desc = max96752f->desc;

	if (desc->enable)
		desc->enable(max96752f);

	backlight_enable(max96752f->backlight);

	if (desc->backlight_enable)
		desc->backlight_enable(max96752f);

	return 0;
}

static int max96752f_panel_unprepare(struct drm_panel *panel)
{
	struct max96752f *max96752f = to_max96752f(panel);
	const struct panel_desc *desc = max96752f->desc;

	if (desc->unprepare)
		desc->unprepare(max96752f);

	pinctrl_pm_select_sleep_state(max96752f->dev);

	return 0;
}

static int max96752f_panel_prepare(struct drm_panel *panel)
{
	struct max96752f *max96752f = to_max96752f(panel);
	const struct panel_desc *desc = max96752f->desc;

	pinctrl_pm_select_default_state(max96752f->dev);

	if (desc->prepare)
		desc->prepare(max96752f);

	return 0;
}

static int max96752f_panel_get_modes(struct drm_panel *panel,
				     struct drm_connector *connector)
{
	struct max96752f *max96752f = to_max96752f(panel);
	const struct panel_desc *desc = max96752f->desc;
	struct drm_display_mode *mode;
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	connector->display_info.width_mm = desc->width_mm;
	connector->display_info.height_mm = desc->height_mm;
	drm_display_info_set_bus_formats(&connector->display_info, &bus_format, 1);

	mode = drm_mode_duplicate(connector->dev, &max96752f->mode);
	mode->width_mm = desc->width_mm;
	mode->height_mm = desc->height_mm;
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs max96752f_panel_funcs = {
	.disable = max96752f_panel_disable,
	.unprepare = max96752f_panel_unprepare,
	.prepare = max96752f_panel_prepare,
	.enable = max96752f_panel_enable,
	.get_modes = max96752f_panel_get_modes,
};

static int max96752f_parse_dt(struct max96752f *max96752f)
{
	struct device *dev = max96752f->dev;
	struct display_timing dt;
	struct videomode vm;
	int ret;

	ret = of_get_display_timing(dev->of_node, "panel-timing", &dt);
	if (ret < 0) {
		dev_err(dev, "%pOF: no panel-timing node found\n", dev->of_node);
		return ret;
	}

	videomode_from_timing(&dt, &vm);
	drm_display_mode_from_videomode(&vm, &max96752f->mode);

	return 0;
}

static const struct regmap_range max96752f_readable_ranges[] = {
	regmap_reg_range(0x0000, 0x0600),
};

static const struct regmap_access_table max96752f_readable_table = {
	.yes_ranges = max96752f_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(max96752f_readable_ranges),
};

static const struct regmap_config max96752f_regmap_config = {
	.name = "max96752f",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xffff,
	.rd_table = &max96752f_readable_table,
};

static void max96752f_power_off(void *data)
{
	struct max96752f *max96752f = data;

	if (max96752f->supply)
		regulator_disable(max96752f->supply);
}

static void max96752f_power_on(struct max96752f *max96752f)
{
	int ret;

	if (max96752f->supply) {
		ret = regulator_enable(max96752f->supply);
		if (ret)
			dev_err(max96752f->dev,
				"failed to enable power supply: %d\n", ret);
	}
}

static int max96752f_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct max96752f *max96752f;
	struct i2c_client *parent;
	int ret;

	max96752f = devm_kzalloc(dev, sizeof(*max96752f), GFP_KERNEL);
	if (!max96752f)
		return -ENOMEM;

	max96752f->dev = dev;
	max96752f->desc = of_device_get_match_data(dev);
	i2c_set_clientdata(client, max96752f);

	max96752f->supply = devm_regulator_get_optional(dev, "power");
	if (IS_ERR(max96752f->supply)) {
		if (PTR_ERR(max96752f->supply) != -ENODEV)
			return dev_err_probe(dev, PTR_ERR(max96752f->supply),
					     "failed to get regulator\n");

		max96752f->supply = NULL;
	} else {
		ret = regulator_enable(max96752f->supply);
		if (ret) {
			dev_err(dev, "failed to enable power supply: %d\n", ret);
			return ret;
		}

		ret = devm_add_action_or_reset(dev, max96752f_power_off, max96752f);
		if (ret) {
			regulator_disable(max96752f->supply);
			return ret;
		}
	}

	max96752f->regmap.deserializer =
			devm_regmap_init_i2c(client, &max96752f_regmap_config);
	if (IS_ERR(max96752f->regmap.deserializer))
		return dev_err_probe(dev, PTR_ERR(max96752f->regmap.deserializer),
				     "failed to initialize deserializer regmap\n");

	parent = of_find_i2c_device_by_node(dev->of_node->parent->parent);
	if (!parent)
		return dev_err_probe(dev, -ENODEV, "failed to find parent\n");

	max96752f->regmap.serializer = dev_get_regmap(&parent->dev, NULL);
	if (!max96752f->regmap.serializer)
		return dev_err_probe(dev, -ENODEV,
				     "failed to initialize serializer regmap\n");

	ret = max96752f_parse_dt(max96752f);
	if (ret)
		return dev_err_probe(dev, ret, "failed to parse DT\n");

	max96752f->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(max96752f->backlight))
		return dev_err_probe(dev, PTR_ERR(max96752f->backlight),
				     "failed to get backlight\n");

	drm_panel_init(&max96752f->panel, dev, &max96752f_panel_funcs,
		       DRM_MODE_CONNECTOR_LVDS);
	drm_panel_add(&max96752f->panel);

	return 0;
}

static int max96752f_remove(struct i2c_client *client)
{
	struct max96752f *max96752f = i2c_get_clientdata(client);

	drm_panel_remove(&max96752f->panel);

	return 0;
}

static int __maybe_unused max96752f_suspend(struct device *dev)
{
	struct max96752f *max96752f = dev_get_drvdata(dev);

	max96752f_power_off(max96752f);

	return 0;
}

static int __maybe_unused max96752f_resume(struct device *dev)
{
	struct max96752f *max96752f = dev_get_drvdata(dev);

	max96752f_power_on(max96752f);

	return 0;
}

static SIMPLE_DEV_PM_OPS(max96752f_pm_ops, max96752f_suspend, max96752f_resume);

#define maxim_serializer_write(max96752f, reg, val) do {		\
		int ret;						\
		ret = regmap_write(max96752f->regmap.serializer,	\
				   reg, val);				\
		if (ret)						\
			return ret;					\
	} while (0)

#define maxim_serializer_read(max96752f, reg, val) do {			\
		int ret;						\
		ret = regmap_read(max96752f->regmap.serializer,		\
				  reg, val);				\
		if (ret)                                                \
			return ret;                                     \
	} while (0)

#define maxim_deserializer_write(max96752f, reg, val) do {		\
		int ret;						\
		ret = regmap_write(max96752f->regmap.deserializer,	\
				   reg, val);				\
		if (ret)						\
			return ret;					\
	} while (0)

#define maxim_deserializer_read(max96752f, reg, val) do {		\
		int ret;						\
		ret = regmap_read(max96752f->regmap.deserializer,	\
				  reg, val);				\
		if (ret)                                                \
			return ret;                                     \
	} while (0)

static int boe_av156fht_l83_prepare(struct max96752f *max96752f)
{
	maxim_deserializer_write(max96752f, 0x0002, 0x43);
	maxim_deserializer_write(max96752f, 0x0140, 0x20);

	maxim_deserializer_write(max96752f, 0x01ce, 0x5e);	/* oldi */
	maxim_deserializer_write(max96752f, 0x020e, 0x40);	/* bl_pwm */
	maxim_deserializer_write(max96752f, 0x020c, 0x84);
	maxim_deserializer_write(max96752f, 0x0207, 0xa1);	/* tp_int */
	maxim_deserializer_write(max96752f, 0x0206, 0x83);

	maxim_deserializer_write(max96752f, 0x0215, 0x90);	/* lcd_en */
	msleep(20);

	return 0;
}

static int boe_av156fht_l83_unprepare(struct max96752f *max96752f)
{
	maxim_deserializer_write(max96752f, 0x0215, 0x80);	/* lcd_en */

	return 0;
}

static int boe_av156fht_l83_enable(struct max96752f *max96752f)
{
	maxim_deserializer_write(max96752f, 0x0227, 0x90);	/* lcd_rst */
	msleep(20);
	maxim_deserializer_write(max96752f, 0x020f, 0x90);	/* tp_rst */
	msleep(100);
	maxim_deserializer_write(max96752f, 0x0221, 0x90);	/* lcd_stb */
	msleep(60);

	return 0;
}

static int boe_av156fht_l83_disable(struct max96752f *max96752f)
{
	maxim_deserializer_write(max96752f, 0x0221, 0x80);	/* lcd_stb */
	maxim_deserializer_write(max96752f, 0x020f, 0x80);	/* tp_rst */
	maxim_deserializer_write(max96752f, 0x0227, 0x80);	/* lcd_rst */

	return 0;
}

static int boe_av156fht_l83_backlight_enable(struct max96752f *max96752f)
{
	maxim_deserializer_write(max96752f, 0x0212, 0x90);	/* bl_current_ctl */
	maxim_deserializer_write(max96752f, 0x0209, 0x90);	/* bl_en */

	return 0;
}

static int boe_av156fht_l83_backlight_disable(struct max96752f *max96752f)
{
	maxim_deserializer_write(max96752f, 0x0209, 0x80);	/* bl_en */
	maxim_deserializer_write(max96752f, 0x0212, 0x80);	/* bl_current_ctl */

	return 0;
}

static const struct panel_desc boe_av156fht_l83 = {
	.name			= "boe-av156fht-l83",
	.width_mm		= 346,
	.height_mm		= 194,
	.prepare		= boe_av156fht_l83_prepare,
	.unprepare		= boe_av156fht_l83_unprepare,
	.enable			= boe_av156fht_l83_enable,
	.disable		= boe_av156fht_l83_disable,
	.backlight_enable	= boe_av156fht_l83_backlight_enable,
	.backlight_disable	= boe_av156fht_l83_backlight_disable,
};

static int hannstar_hsd123jpw3_a15_prepare(struct max96752f *max96752f)
{
	maxim_deserializer_write(max96752f, 0x0002, 0x43);
	maxim_deserializer_write(max96752f, 0x0140, 0x20);
	maxim_deserializer_write(max96752f, 0x01ce, 0x5e);

	maxim_deserializer_write(max96752f, 0x0203, 0x83);	/* GPIO1  <- TP_INT */
	maxim_deserializer_write(max96752f, 0x0206, 0x84);      /* GPIO2  -> TP_RST */
	maxim_deserializer_write(max96752f, 0x0224, 0x84);	/* GPIO12 -> LCD_BL_PWM */

	return 0;
}

static int hannstar_hsd123jpw3_a15_unprepare(struct max96752f *max96752f)
{
	return 0;
}

static int hannstar_hsd123jpw3_a15_enable(struct max96752f *max96752f)
{
	maxim_deserializer_write(max96752f, 0x0221, 0x90);	/* GPIO11 -> LCD_RESET */
	msleep(20);

	return 0;
}

static int hannstar_hsd123jpw3_a15_disable(struct max96752f *max96752f)
{
	maxim_deserializer_write(max96752f, 0x0221, 0x80);	/* GPIO11 -> LCD_RESET */
	msleep(20);

	return 0;
}

static const struct panel_desc hannstar_hsd123jpw3_a15 = {
	.name			= "hannstar,hsd123jpw3-a15",
	.width_mm		= 292,
	.height_mm		= 110,
	.prepare		= hannstar_hsd123jpw3_a15_prepare,
	.unprepare		= hannstar_hsd123jpw3_a15_unprepare,
	.enable			= hannstar_hsd123jpw3_a15_enable,
	.disable		= hannstar_hsd123jpw3_a15_disable,
};

static int ogm_101fhbllm01_prepare(struct max96752f *max96752f)
{
	maxim_deserializer_write(max96752f, 0x01ce, 0x5e);

	maxim_deserializer_write(max96752f, 0x0203, 0x84);	/* GPIO1 -> BL_PWM */
	maxim_deserializer_write(max96752f, 0x0206, 0x84);	/* GPIO2 -> TP_RST */
	maxim_deserializer_write(max96752f, 0x0209, 0x83);	/* GPIO3 <- TP_INT */

	maxim_deserializer_write(max96752f, 0x0001, 0x02);

	return 0;
}

static int ogm_101fhbllm01_unprepare(struct max96752f *max96752f)
{
	maxim_deserializer_write(max96752f, 0x0001, 0x01);

	return 0;
}

static const struct panel_desc ogm_101fhbllm01 = {
	.name			= "ogm,101fhbllm01",
	.width_mm		= 126,
	.height_mm		= 223,
	.prepare		= ogm_101fhbllm01_prepare,
	.unprepare		= ogm_101fhbllm01_unprepare,
};

static const struct of_device_id max96752f_of_match[] = {
	{ .compatible = "boe,av156fht-l83", &boe_av156fht_l83 },
	{ .compatible = "hannstar,hsd123jpw3-a15", &hannstar_hsd123jpw3_a15 },
	{ .compatible = "ogm,101fhbllm01", &ogm_101fhbllm01 },
	{ }
};
MODULE_DEVICE_TABLE(of, max96752f_of_match);

static struct i2c_driver max96752f_driver = {
	.driver = {
		.name = "panel-maxim-max96752f",
		.of_match_table = max96752f_of_match,
		.pm = &max96752f_pm_ops,
	},
	.probe_new = max96752f_probe,
	.remove = max96752f_remove,
};

module_i2c_driver(max96752f_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Maxim MAX96752F based panel driver");
MODULE_LICENSE("GPL");
