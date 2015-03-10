/*
 * max77843.c - Regulator driver for the Maxim MAX77843
 *
 * Copyright (C) 2015 Samsung Electronics
 * Author: Jaewon Kim <jaewon02.kim@samsung.com>
 * Author: Beomho Seo <beomho.seo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/max77843-private.h>
#include <linux/regulator/of_regulator.h>

enum max77843_regulator_type {
	MAX77843_SAFEOUT1 = 0,
	MAX77843_SAFEOUT2,
	MAX77843_CHARGER,

	MAX77843_NUM,
};

static const unsigned int max77843_safeout_voltage_table[] = {
	4850000,
	4900000,
	4950000,
	3300000,
};

static int max77843_reg_is_enabled(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev->regmap;
	int ret;
	unsigned int reg;

	ret = regmap_read(regmap, rdev->desc->enable_reg, &reg);
	if (ret) {
		dev_err(&rdev->dev, "Fialed to read charger register\n");
		return ret;
	}

	return (reg & rdev->desc->enable_mask) == rdev->desc->enable_mask;
}

static int max77843_reg_get_current_limit(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev->regmap;
	unsigned int chg_min_uA = rdev->constraints->min_uA;
	unsigned int chg_max_uA = rdev->constraints->max_uA;
	unsigned int val;
	int ret;
	unsigned int reg, sel;

	ret = regmap_read(regmap, MAX77843_CHG_REG_CHG_CNFG_02, &reg);
	if (ret) {
		dev_err(&rdev->dev, "Failed to read charger register\n");
		return ret;
	}

	sel = reg & MAX77843_CHG_FAST_CHG_CURRENT_MASK;

	if (sel < 0x03)
		sel = 0;
	else
		sel -= 2;

	val = chg_min_uA + MAX77843_CHG_FAST_CHG_CURRENT_STEP * sel;
	if (val > chg_max_uA)
		return -EINVAL;

	return val;
}

static int max77843_reg_set_current_limit(struct regulator_dev *rdev,
		int min_uA, int max_uA)
{
	struct regmap *regmap = rdev->regmap;
	unsigned int chg_min_uA = rdev->constraints->min_uA;
	int sel = 0;

	while (chg_min_uA + MAX77843_CHG_FAST_CHG_CURRENT_STEP * sel < min_uA)
		sel++;

	if (chg_min_uA + MAX77843_CHG_FAST_CHG_CURRENT_STEP * sel > max_uA)
		return -EINVAL;

	sel += 2;

	return regmap_write(regmap, MAX77843_CHG_REG_CHG_CNFG_02, sel);
}

static struct regulator_ops max77843_charger_ops = {
	.is_enabled		= max77843_reg_is_enabled,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_current_limit	= max77843_reg_get_current_limit,
	.set_current_limit	= max77843_reg_set_current_limit,
};

static struct regulator_ops max77843_regulator_ops = {
	.is_enabled             = regulator_is_enabled_regmap,
	.enable                 = regulator_enable_regmap,
	.disable                = regulator_disable_regmap,
	.list_voltage		= regulator_list_voltage_table,
	.get_voltage_sel        = regulator_get_voltage_sel_regmap,
	.set_voltage_sel        = regulator_set_voltage_sel_regmap,
};

static const struct regulator_desc max77843_supported_regulators[] = {
	[MAX77843_SAFEOUT1] = {
		.name		= "SAFEOUT1",
		.id		= MAX77843_SAFEOUT1,
		.ops		= &max77843_regulator_ops,
		.of_match	= of_match_ptr("SAFEOUT1"),
		.regulators_node = of_match_ptr("regulators"),
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
		.n_voltages	= ARRAY_SIZE(max77843_safeout_voltage_table),
		.volt_table	= max77843_safeout_voltage_table,
		.enable_reg	= MAX77843_SYS_REG_SAFEOUTCTRL,
		.enable_mask	= MAX77843_REG_SAFEOUTCTRL_ENSAFEOUT1,
		.vsel_reg	= MAX77843_SYS_REG_SAFEOUTCTRL,
		.vsel_mask	= MAX77843_REG_SAFEOUTCTRL_SAFEOUT1_MASK,
	},
	[MAX77843_SAFEOUT2] = {
		.name           = "SAFEOUT2",
		.id             = MAX77843_SAFEOUT2,
		.ops            = &max77843_regulator_ops,
		.of_match	= of_match_ptr("SAFEOUT2"),
		.regulators_node = of_match_ptr("regulators"),
		.type           = REGULATOR_VOLTAGE,
		.owner          = THIS_MODULE,
		.n_voltages	= ARRAY_SIZE(max77843_safeout_voltage_table),
		.volt_table	= max77843_safeout_voltage_table,
		.enable_reg     = MAX77843_SYS_REG_SAFEOUTCTRL,
		.enable_mask    = MAX77843_REG_SAFEOUTCTRL_ENSAFEOUT2,
		.vsel_reg	= MAX77843_SYS_REG_SAFEOUTCTRL,
		.vsel_mask	= MAX77843_REG_SAFEOUTCTRL_SAFEOUT2_MASK,
	},
	[MAX77843_CHARGER] = {
		.name		= "CHARGER",
		.id		= MAX77843_CHARGER,
		.ops		= &max77843_charger_ops,
		.of_match	= of_match_ptr("CHARGER"),
		.regulators_node = of_match_ptr("regulators"),
		.type		= REGULATOR_CURRENT,
		.owner		= THIS_MODULE,
		.enable_reg	= MAX77843_CHG_REG_CHG_CNFG_00,
		.enable_mask	= MAX77843_CHG_MASK,
	},
};

static struct regmap *max77843_get_regmap(struct max77843 *max77843, int reg_id)
{
	switch (reg_id) {
	case MAX77843_SAFEOUT1:
	case MAX77843_SAFEOUT2:
		return max77843->regmap;
	case MAX77843_CHARGER:
		return max77843->regmap_chg;
	default:
		return max77843->regmap;
	}
}

static int max77843_regulator_probe(struct platform_device *pdev)
{
	struct max77843 *max77843 = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = {};
	int i;

	config.dev = max77843->dev;
	config.driver_data = max77843;

	for (i = 0; i < ARRAY_SIZE(max77843_supported_regulators); i++) {
		struct regulator_dev *regulator;

		config.regmap = max77843_get_regmap(max77843,
				max77843_supported_regulators[i].id);

		regulator = devm_regulator_register(&pdev->dev,
				&max77843_supported_regulators[i], &config);
		if (IS_ERR(regulator)) {
			dev_err(&pdev->dev,
					"Failed to regiser regulator-%d\n", i);
			return PTR_ERR(regulator);
		}
	}

	return 0;
}

static const struct platform_device_id max77843_regulator_id[] = {
	{ "max77843-regulator", },
	{ /* sentinel */ },
};

static struct platform_driver max77843_regulator_driver = {
	.driver	= {
		.name = "max77843-regulator",
	},
	.probe		= max77843_regulator_probe,
	.id_table	= max77843_regulator_id,
};

static int __init max77843_regulator_init(void)
{
	return platform_driver_register(&max77843_regulator_driver);
}
subsys_initcall(max77843_regulator_init);

static void __exit max77843_regulator_exit(void)
{
	platform_driver_unregister(&max77843_regulator_driver);
}
module_exit(max77843_regulator_exit);

MODULE_AUTHOR("Jaewon Kim <jaewon02.kim@samsung.com>");
MODULE_AUTHOR("Beomho Seo <beomho.seo@samsung.com>");
MODULE_DESCRIPTION("Maxim MAX77843 regulator driver");
MODULE_LICENSE("GPL");
