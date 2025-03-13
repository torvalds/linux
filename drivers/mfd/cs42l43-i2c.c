// SPDX-License-Identifier: GPL-2.0
/*
 * CS42L43 I2C driver
 *
 * Copyright (C) 2022-2023 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 */

#include <linux/array_size.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mfd/cs42l43.h>
#include <linux/mfd/cs42l43-regs.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/regmap.h>

#include "cs42l43.h"

static const struct regmap_config cs42l43_i2c_regmap = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.reg_format_endian	= REGMAP_ENDIAN_BIG,
	.val_format_endian	= REGMAP_ENDIAN_BIG,

	.max_register		= CS42L43_MCU_RAM_MAX,
	.readable_reg		= cs42l43_readable_register,
	.volatile_reg		= cs42l43_volatile_register,
	.precious_reg		= cs42l43_precious_register,

	.cache_type		= REGCACHE_MAPLE,
	.reg_defaults		= cs42l43_reg_default,
	.num_reg_defaults	= ARRAY_SIZE(cs42l43_reg_default),
};

static int cs42l43_i2c_probe(struct i2c_client *i2c)
{
	struct cs42l43 *cs42l43;

	cs42l43 = devm_kzalloc(&i2c->dev, sizeof(*cs42l43), GFP_KERNEL);
	if (!cs42l43)
		return -ENOMEM;

	cs42l43->dev = &i2c->dev;
	cs42l43->irq = i2c->irq;
	/* A device on an I2C is always attached by definition. */
	cs42l43->attached = true;

	cs42l43->regmap = devm_regmap_init_i2c(i2c, &cs42l43_i2c_regmap);
	if (IS_ERR(cs42l43->regmap))
		return dev_err_probe(cs42l43->dev, PTR_ERR(cs42l43->regmap),
				     "Failed to allocate regmap\n");

	return cs42l43_dev_probe(cs42l43);
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id cs42l43_of_match[] = {
	{ .compatible = "cirrus,cs42l43", },
	{}
};
MODULE_DEVICE_TABLE(of, cs42l43_of_match);
#endif

#if IS_ENABLED(CONFIG_ACPI)
static const struct acpi_device_id cs42l43_acpi_match[] = {
	{ "CSC4243", 0 },
	{}
};
MODULE_DEVICE_TABLE(acpi, cs42l43_acpi_match);
#endif

static struct i2c_driver cs42l43_i2c_driver = {
	.driver = {
		.name			= "cs42l43",
		.pm			= pm_ptr(&cs42l43_pm_ops),
		.of_match_table		= of_match_ptr(cs42l43_of_match),
		.acpi_match_table	= ACPI_PTR(cs42l43_acpi_match),
	},

	.probe		= cs42l43_i2c_probe,
};
module_i2c_driver(cs42l43_i2c_driver);

MODULE_IMPORT_NS("MFD_CS42L43");

MODULE_DESCRIPTION("CS42L43 I2C Driver");
MODULE_AUTHOR("Charles Keepax <ckeepax@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
