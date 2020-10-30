// SPDX-License-Identifier: GPL-2.0-only
/*
 * Simple MFD - I2C
 *
 * This driver creates a single register map with the intention for it to be
 * shared by all sub-devices.  Children can use their parent's device structure
 * (dev.parent) in order to reference it.
 *
 * Once the register map has been successfully initialised, any sub-devices
 * represented by child nodes in Device Tree will be subsequently registered.
 */

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>

static const struct regmap_config simple_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int simple_mfd_i2c_probe(struct i2c_client *i2c)
{
	const struct regmap_config *config;
	struct regmap *regmap;

	config = device_get_match_data(&i2c->dev);
	if (!config)
		config = &simple_regmap_config;

	regmap = devm_regmap_init_i2c(i2c, config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return devm_of_platform_populate(&i2c->dev);
}

static const struct of_device_id simple_mfd_i2c_of_match[] = {
	{ .compatible = "kontron,sl28cpld" },
	{}
};
MODULE_DEVICE_TABLE(of, simple_mfd_i2c_of_match);

static struct i2c_driver simple_mfd_i2c_driver = {
	.probe_new = simple_mfd_i2c_probe,
	.driver = {
		.name = "simple-mfd-i2c",
		.of_match_table = simple_mfd_i2c_of_match,
	},
};
module_i2c_driver(simple_mfd_i2c_driver);

MODULE_AUTHOR("Michael Walle <michael@walle.cc>");
MODULE_DESCRIPTION("Simple MFD - I2C driver");
MODULE_LICENSE("GPL v2");
