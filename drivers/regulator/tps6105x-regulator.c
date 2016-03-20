/*
 * Driver for TPS61050/61052 boost converters, typically used for white LEDs
 * or audio amplifiers.
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tps6105x.h>

static const unsigned int tps6105x_voltages[] = {
	4500000,
	5000000,
	5250000,
	5000000, /* There is an additional 5V */
};

static struct regulator_ops tps6105x_regulator_ops = {
	.enable		= regulator_enable_regmap,
	.disable	= regulator_disable_regmap,
	.is_enabled	= regulator_is_enabled_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.list_voltage	= regulator_list_voltage_table,
};

static const struct regulator_desc tps6105x_regulator_desc = {
	.name		= "tps6105x-boost",
	.ops		= &tps6105x_regulator_ops,
	.type		= REGULATOR_VOLTAGE,
	.id		= 0,
	.owner		= THIS_MODULE,
	.n_voltages	= ARRAY_SIZE(tps6105x_voltages),
	.volt_table	= tps6105x_voltages,
	.vsel_reg	= TPS6105X_REG_0,
	.vsel_mask	= TPS6105X_REG0_VOLTAGE_MASK,
	.enable_reg	= TPS6105X_REG_0,
	.enable_mask	= TPS6105X_REG0_MODE_MASK,
	.enable_val	= TPS6105X_REG0_MODE_VOLTAGE <<
			  TPS6105X_REG0_MODE_SHIFT,
};

/*
 * Registers the chip as a voltage regulator
 */
static int tps6105x_regulator_probe(struct platform_device *pdev)
{
	struct tps6105x *tps6105x = dev_get_platdata(&pdev->dev);
	struct tps6105x_platform_data *pdata = tps6105x->pdata;
	struct regulator_config config = { };
	int ret;

	/* This instance is not set for regulator mode so bail out */
	if (pdata->mode != TPS6105X_MODE_VOLTAGE) {
		dev_info(&pdev->dev,
			"chip not in voltage mode mode, exit probe\n");
		return 0;
	}

	config.dev = &tps6105x->client->dev;
	config.init_data = pdata->regulator_data;
	config.driver_data = tps6105x;
	config.regmap = tps6105x->regmap;

	/* Register regulator with framework */
	tps6105x->regulator = devm_regulator_register(&pdev->dev,
						      &tps6105x_regulator_desc,
						      &config);
	if (IS_ERR(tps6105x->regulator)) {
		ret = PTR_ERR(tps6105x->regulator);
		dev_err(&tps6105x->client->dev,
			"failed to register regulator\n");
		return ret;
	}
	platform_set_drvdata(pdev, tps6105x);

	return 0;
}

static struct platform_driver tps6105x_regulator_driver = {
	.driver = {
		.name  = "tps6105x-regulator",
	},
	.probe = tps6105x_regulator_probe,
};

static __init int tps6105x_regulator_init(void)
{
	return platform_driver_register(&tps6105x_regulator_driver);
}
subsys_initcall(tps6105x_regulator_init);

static __exit void tps6105x_regulator_exit(void)
{
	platform_driver_unregister(&tps6105x_regulator_driver);
}
module_exit(tps6105x_regulator_exit);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("TPS6105x regulator driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:tps6105x-regulator");
