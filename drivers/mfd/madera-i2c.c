// SPDX-License-Identifier: GPL-2.0-only
/*
 * I2C bus interface to Cirrus Logic Madera codecs
 *
 * Copyright (C) 2015-2018 Cirrus Logic
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#include <linux/mfd/madera/core.h>

#include "madera.h"

static int madera_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct madera *madera;
	const struct regmap_config *regmap_16bit_config = NULL;
	const struct regmap_config *regmap_32bit_config = NULL;
	const void *of_data;
	unsigned long type;
	const char *name;
	int ret;

	of_data = of_device_get_match_data(&i2c->dev);
	if (of_data)
		type = (unsigned long)of_data;
	else
		type = id->driver_data;

	switch (type) {
	case CS47L15:
		if (IS_ENABLED(CONFIG_MFD_CS47L15)) {
			regmap_16bit_config = &cs47l15_16bit_i2c_regmap;
			regmap_32bit_config = &cs47l15_32bit_i2c_regmap;
		}
		break;
	case CS47L35:
		if (IS_ENABLED(CONFIG_MFD_CS47L35)) {
			regmap_16bit_config = &cs47l35_16bit_i2c_regmap;
			regmap_32bit_config = &cs47l35_32bit_i2c_regmap;
		}
		break;
	case CS47L85:
	case WM1840:
		if (IS_ENABLED(CONFIG_MFD_CS47L85)) {
			regmap_16bit_config = &cs47l85_16bit_i2c_regmap;
			regmap_32bit_config = &cs47l85_32bit_i2c_regmap;
		}
		break;
	case CS47L90:
	case CS47L91:
		if (IS_ENABLED(CONFIG_MFD_CS47L90)) {
			regmap_16bit_config = &cs47l90_16bit_i2c_regmap;
			regmap_32bit_config = &cs47l90_32bit_i2c_regmap;
		}
		break;
	case CS42L92:
	case CS47L92:
	case CS47L93:
		if (IS_ENABLED(CONFIG_MFD_CS47L92)) {
			regmap_16bit_config = &cs47l92_16bit_i2c_regmap;
			regmap_32bit_config = &cs47l92_32bit_i2c_regmap;
		}
		break;
	default:
		dev_err(&i2c->dev,
			"Unknown Madera I2C device type %ld\n", type);
		return -EINVAL;
	}

	name = madera_name_from_type(type);

	if (!regmap_16bit_config) {
		/* it's polite to say which codec isn't built into the kernel */
		dev_err(&i2c->dev,
			"Kernel does not include support for %s\n", name);
		return -EINVAL;
	}

	madera = devm_kzalloc(&i2c->dev, sizeof(*madera), GFP_KERNEL);
	if (!madera)
		return -ENOMEM;

	madera->regmap = devm_regmap_init_i2c(i2c, regmap_16bit_config);
	if (IS_ERR(madera->regmap)) {
		ret = PTR_ERR(madera->regmap);
		dev_err(&i2c->dev,
			"Failed to allocate 16-bit register map: %d\n",	ret);
		return ret;
	}

	madera->regmap_32bit = devm_regmap_init_i2c(i2c, regmap_32bit_config);
	if (IS_ERR(madera->regmap_32bit)) {
		ret = PTR_ERR(madera->regmap_32bit);
		dev_err(&i2c->dev,
			"Failed to allocate 32-bit register map: %d\n", ret);
		return ret;
	}

	madera->type = type;
	madera->type_name = name;
	madera->dev = &i2c->dev;
	madera->irq = i2c->irq;

	return madera_dev_init(madera);
}

static void madera_i2c_remove(struct i2c_client *i2c)
{
	struct madera *madera = dev_get_drvdata(&i2c->dev);

	madera_dev_exit(madera);
}

static const struct i2c_device_id madera_i2c_id[] = {
	{ "cs47l15", CS47L15 },
	{ "cs47l35", CS47L35 },
	{ "cs47l85", CS47L85 },
	{ "cs47l90", CS47L90 },
	{ "cs47l91", CS47L91 },
	{ "cs42l92", CS42L92 },
	{ "cs47l92", CS47L92 },
	{ "cs47l93", CS47L93 },
	{ "wm1840", WM1840 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, madera_i2c_id);

static struct i2c_driver madera_i2c_driver = {
	.driver = {
		.name	= "madera",
		.pm	= &madera_pm_ops,
		.of_match_table	= of_match_ptr(madera_of_match),
	},
	.probe		= madera_i2c_probe,
	.remove		= madera_i2c_remove,
	.id_table	= madera_i2c_id,
};

module_i2c_driver(madera_i2c_driver);

MODULE_DESCRIPTION("Madera I2C bus interface");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
