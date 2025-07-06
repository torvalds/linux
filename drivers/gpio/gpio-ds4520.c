// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2023 Analog Devices, Inc.
 * Driver for the DS4520 I/O Expander
 */

#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/regmap.h>
#include <linux/i2c.h>
#include <linux/property.h>
#include <linux/regmap.h>

#define DS4520_PULLUP0		0xF0
#define DS4520_IO_CONTROL0	0xF2
#define DS4520_IO_STATUS0	0xF8

static const struct regmap_config ds4520_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int ds4520_gpio_probe(struct i2c_client *client)
{
	struct gpio_regmap_config config = { };
	struct device *dev = &client->dev;
	struct regmap *regmap;
	u32 base;
	int ret;

	ret = device_property_read_u32(dev, "reg", &base);
	if (ret)
		return dev_err_probe(dev, ret, "Missing 'reg' property.\n");

	regmap = devm_regmap_init_i2c(client, &ds4520_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to allocate register map\n");

	config.regmap = regmap;
	config.parent = dev;

	config.reg_dat_base = base + DS4520_IO_STATUS0;
	config.reg_set_base = base + DS4520_PULLUP0;
	config.reg_dir_out_base = base + DS4520_IO_CONTROL0;

	return PTR_ERR_OR_ZERO(devm_gpio_regmap_register(dev, &config));
}

static const struct of_device_id ds4520_gpio_of_match_table[] = {
	{ .compatible = "adi,ds4520-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, ds4520_gpio_of_match_table);

static const struct i2c_device_id ds4520_gpio_id_table[] = {
	{ "ds4520-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ds4520_gpio_id_table);

static struct i2c_driver ds4520_gpio_driver = {
	.driver = {
		.name = "ds4520-gpio",
		.of_match_table = ds4520_gpio_of_match_table,
	},
	.probe = ds4520_gpio_probe,
	.id_table = ds4520_gpio_id_table,
};
module_i2c_driver(ds4520_gpio_driver);

MODULE_DESCRIPTION("DS4520 I/O Expander");
MODULE_AUTHOR("Okan Sahin <okan.sahin@analog.com>");
MODULE_LICENSE("GPL");
