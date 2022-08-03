// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim MAX96776 MFD driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max96752f.h>

struct max96776 {
	struct device *dev;
	struct regmap *regmap;
	struct i2c_client *client;
	struct i2c_mux_core *muxc;
	struct gpio_desc *enable_gpio;
	u32 stream_id;
};

static const struct mfd_cell max96776_devs[] = {
	{
		.name = "max96776-bridge",
		.of_compatible = "maxim,max96776-bridge",
	},
};

static int max96776_select(struct i2c_mux_core *muxc, u32 chan)
{
	return 0;
}

static const struct regmap_range max96776_readable_ranges[] = {
	regmap_reg_range(0x0000, 0x0026),
	regmap_reg_range(0x0029, 0x002c),
	regmap_reg_range(0x0050, 0x0050),
	regmap_reg_range(0x0100, 0x0100),
	regmap_reg_range(0x0103, 0x0103),
	regmap_reg_range(0x0108, 0x0108),
	regmap_reg_range(0x0600, 0x0600),
	regmap_reg_range(0x07f0, 0x07f1),
	regmap_reg_range(0x1700, 0x1700),
	regmap_reg_range(0x4100, 0x4100),
	regmap_reg_range(0x6230, 0x6230),
	regmap_reg_range(0xe75e, 0xe75e),
	regmap_reg_range(0xe776, 0xe7bf),
};

static const struct regmap_access_table max96776_readable_table = {
	.yes_ranges = max96776_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(max96776_readable_ranges),
};

static const struct regmap_config max96776_regmap_config = {
	.name = "max96776",
	.reg_bits = 16,
	.val_bits = 8,
	.rd_table = &max96776_readable_table,
	.max_register = 0xff02,
};

static void max96776_power_on(struct max96776 *max96776)
{
	if (max96776->enable_gpio) {
		gpiod_direction_output(max96776->enable_gpio, 1);
		msleep(500);
	}
}

static void max96776_power_off(struct max96776 *max96776)
{
	if (max96776->enable_gpio)
		gpiod_direction_output(max96776->enable_gpio, 0);
}

static int max96776_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *child;
	struct max96776 *max96776;
	unsigned int nr = 0;
	int ret;

	for_each_available_child_of_node(dev->of_node, child) {
		if (!of_find_property(child, "reg", NULL))
			continue;

		nr++;
	}

	max96776 = devm_kzalloc(dev, sizeof(*max96776), GFP_KERNEL);
	if (!max96776)
		return -ENOMEM;

	max96776->muxc = i2c_mux_alloc(client->adapter, dev, nr, 0,
				       I2C_MUX_LOCKED, max96776_select, NULL);
	if (!max96776->muxc)
		return -ENOMEM;

	max96776->dev = dev;
	max96776->client = client;

	max96776->enable_gpio = devm_gpiod_get_optional(dev, "enable",
							 GPIOD_ASIS);
	if (IS_ERR(max96776->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(max96776->enable_gpio),
				     "failed to get enable GPIO\n");

	ret = device_property_read_u32(dev->parent, "reg", &max96776->stream_id);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get gmsl id\n");

	i2c_set_clientdata(client, max96776);

	max96776->regmap = devm_regmap_init_i2c(client,
						 &max96776_regmap_config);
	if (IS_ERR(max96776->regmap))
		return dev_err_probe(dev, PTR_ERR(max96776->regmap),
				     "failed to initialize regmap\n");

	max96776_power_on(max96776);

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, max96776_devs,
				   ARRAY_SIZE(max96776_devs), NULL, 0, NULL);
	if (ret)
		return ret;

	for_each_available_child_of_node(dev->of_node, child) {
		if (of_property_read_u32(child, "reg", &nr))
			continue;

		ret = i2c_mux_add_adapter(max96776->muxc, 0, nr, 0);
		if (ret) {
			i2c_mux_del_adapters(max96776->muxc);
			return ret;
		}
	}

	return 0;
}

static void max96776_i2c_shutdown(struct i2c_client *client)
{
	struct max96776 *max96776 = i2c_get_clientdata(client);

	max96776_power_off(max96776);
}

static int __maybe_unused max96776_suspend(struct device *dev)
{
	struct max96776 *max96776 = dev_get_drvdata(dev);

	max96776_power_off(max96776);

	return 0;
}

static int __maybe_unused max96776_resume(struct device *dev)
{
	struct max96776 *max96776 = dev_get_drvdata(dev);

	max96776_power_on(max96776);

	return 0;
}

static SIMPLE_DEV_PM_OPS(max96776_pm_ops, max96776_suspend, max96776_resume);

static const struct of_device_id max96776_of_match[] = {
	{ .compatible = "maxim,max96776", },
	{}
};
MODULE_DEVICE_TABLE(of, max96776_of_match);

static struct i2c_driver max96776_i2c_driver = {
	.driver = {
		.name = "max96776",
		.of_match_table = of_match_ptr(max96776_of_match),
		.pm = &max96776_pm_ops,
	},
	.probe_new = max96776_i2c_probe,
	.shutdown = max96776_i2c_shutdown,
};

module_i2c_driver(max96776_i2c_driver);

MODULE_AUTHOR("Guochun Huang <hero.huang@rock-chips.com>");
MODULE_DESCRIPTION("Maxim max96776 MFD driver");
MODULE_LICENSE("GPL");
