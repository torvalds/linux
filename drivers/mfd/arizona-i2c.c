// SPDX-License-Identifier: GPL-2.0-only
/*
 * Arizona-i2c.c  --  Arizona I2C bus interface
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/of.h>

#include <linux/mfd/arizona/core.h>

#include "arizona.h"

static int arizona_i2c_probe(struct i2c_client *i2c)
{
	struct arizona *arizona;
	const struct regmap_config *regmap_config = NULL;
	unsigned long type;
	int ret;

	type = (uintptr_t)i2c_get_match_data(i2c);
	switch (type) {
	case WM5102:
		if (IS_ENABLED(CONFIG_MFD_WM5102))
			regmap_config = &wm5102_i2c_regmap;
		break;
	case WM5110:
	case WM8280:
		if (IS_ENABLED(CONFIG_MFD_WM5110))
			regmap_config = &wm5110_i2c_regmap;
		break;
	case WM8997:
		if (IS_ENABLED(CONFIG_MFD_WM8997))
			regmap_config = &wm8997_i2c_regmap;
		break;
	case WM8998:
	case WM1814:
		if (IS_ENABLED(CONFIG_MFD_WM8998))
			regmap_config = &wm8998_i2c_regmap;
		break;
	default:
		dev_err(&i2c->dev, "Unknown device type %ld\n", type);
		return -EINVAL;
	}

	if (!regmap_config) {
		dev_err(&i2c->dev,
			"No kernel support for device type %ld\n", type);
		return -EINVAL;
	}

	arizona = devm_kzalloc(&i2c->dev, sizeof(*arizona), GFP_KERNEL);
	if (arizona == NULL)
		return -ENOMEM;

	arizona->regmap = devm_regmap_init_i2c(i2c, regmap_config);
	if (IS_ERR(arizona->regmap)) {
		ret = PTR_ERR(arizona->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	arizona->type = type;
	arizona->dev = &i2c->dev;
	arizona->irq = i2c->irq;

	return arizona_dev_init(arizona);
}

static void arizona_i2c_remove(struct i2c_client *i2c)
{
	struct arizona *arizona = dev_get_drvdata(&i2c->dev);

	arizona_dev_exit(arizona);
}

static const struct i2c_device_id arizona_i2c_id[] = {
	{ "wm5102", WM5102 },
	{ "wm5110", WM5110 },
	{ "wm8280", WM8280 },
	{ "wm8997", WM8997 },
	{ "wm8998", WM8998 },
	{ "wm1814", WM1814 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, arizona_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id arizona_i2c_of_match[] = {
	{ .compatible = "wlf,wm5102", .data = (void *)WM5102 },
	{ .compatible = "wlf,wm5110", .data = (void *)WM5110 },
	{ .compatible = "wlf,wm8280", .data = (void *)WM8280 },
	{ .compatible = "wlf,wm8997", .data = (void *)WM8997 },
	{ .compatible = "wlf,wm8998", .data = (void *)WM8998 },
	{ .compatible = "wlf,wm1814", .data = (void *)WM1814 },
	{},
};
MODULE_DEVICE_TABLE(of, arizona_i2c_of_match);
#endif

static struct i2c_driver arizona_i2c_driver = {
	.driver = {
		.name	= "arizona",
		.pm	= pm_ptr(&arizona_pm_ops),
		.of_match_table	= of_match_ptr(arizona_i2c_of_match),
	},
	.probe		= arizona_i2c_probe,
	.remove		= arizona_i2c_remove,
	.id_table	= arizona_i2c_id,
};

module_i2c_driver(arizona_i2c_driver);

MODULE_SOFTDEP("pre: arizona_ldo1");
MODULE_DESCRIPTION("Arizona I2C bus interface");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
