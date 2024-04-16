// SPDX-License-Identifier: GPL-2.0-only
/*
 * Regulator driver for the Richtek RT5033
 *
 * Copyright (C) 2014 Samsung Electronics, Co., Ltd.
 * Author: Beomho Seo <beomho.seo@samsung.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/mfd/rt5033.h>
#include <linux/mfd/rt5033-private.h>
#include <linux/regulator/of_regulator.h>

static const struct linear_range rt5033_buck_ranges[] = {
	REGULATOR_LINEAR_RANGE(1000000, 0, 20, 100000),
	REGULATOR_LINEAR_RANGE(3000000, 21, 31, 0),
};

static const struct linear_range rt5033_ldo_ranges[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0, 18, 100000),
	REGULATOR_LINEAR_RANGE(3000000, 19, 31, 0),
};

static const struct regulator_ops rt5033_safe_ldo_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.list_voltage		= regulator_list_voltage_linear,
};

static const struct regulator_ops rt5033_buck_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
};

static const struct regulator_desc rt5033_supported_regulators[] = {
	[RT5033_BUCK] = {
		.name		= "BUCK",
		.of_match	= of_match_ptr("BUCK"),
		.regulators_node = of_match_ptr("regulators"),
		.id		= RT5033_BUCK,
		.ops		= &rt5033_buck_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
		.n_voltages	= RT5033_REGULATOR_BUCK_VOLTAGE_STEP_NUM,
		.linear_ranges	= rt5033_buck_ranges,
		.n_linear_ranges = ARRAY_SIZE(rt5033_buck_ranges),
		.enable_reg	= RT5033_REG_CTRL,
		.enable_mask	= RT5033_CTRL_EN_BUCK_MASK,
		.vsel_reg	= RT5033_REG_BUCK_CTRL,
		.vsel_mask	= RT5033_BUCK_CTRL_MASK,
	},
	[RT5033_LDO] = {
		.name		= "LDO",
		.of_match	= of_match_ptr("LDO"),
		.regulators_node = of_match_ptr("regulators"),
		.id		= RT5033_LDO,
		.ops		= &rt5033_buck_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
		.n_voltages	= RT5033_REGULATOR_LDO_VOLTAGE_STEP_NUM,
		.linear_ranges	= rt5033_ldo_ranges,
		.n_linear_ranges = ARRAY_SIZE(rt5033_ldo_ranges),
		.enable_reg	= RT5033_REG_CTRL,
		.enable_mask	= RT5033_CTRL_EN_LDO_MASK,
		.vsel_reg	= RT5033_REG_LDO_CTRL,
		.vsel_mask	= RT5033_LDO_CTRL_MASK,
	},
	[RT5033_SAFE_LDO] = {
		.name		= "SAFE_LDO",
		.of_match	= of_match_ptr("SAFE_LDO"),
		.regulators_node = of_match_ptr("regulators"),
		.id		= RT5033_SAFE_LDO,
		.ops		= &rt5033_safe_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
		.n_voltages	= 1,
		.min_uV		= RT5033_REGULATOR_SAFE_LDO_VOLTAGE,
		.enable_reg	= RT5033_REG_CTRL,
		.enable_mask	= RT5033_CTRL_EN_SAFE_LDO_MASK,
	},
};

static int rt5033_regulator_probe(struct platform_device *pdev)
{
	struct rt5033_dev *rt5033 = dev_get_drvdata(pdev->dev.parent);
	int ret, i;
	struct regulator_config config = {};

	config.dev = rt5033->dev;
	config.driver_data = rt5033;

	for (i = 0; i < ARRAY_SIZE(rt5033_supported_regulators); i++) {
		struct regulator_dev *regulator;

		config.regmap = rt5033->regmap;

		regulator = devm_regulator_register(&pdev->dev,
				&rt5033_supported_regulators[i], &config);
		if (IS_ERR(regulator)) {
			ret = PTR_ERR(regulator);
			dev_err(&pdev->dev,
				"Regulator init failed %d: with error: %d\n",
				i, ret);
			return ret;
		}
	}

	return 0;
}

static const struct platform_device_id rt5033_regulator_id[] = {
	{ "rt5033-regulator", },
	{ }
};
MODULE_DEVICE_TABLE(platform, rt5033_regulator_id);

static struct platform_driver rt5033_regulator_driver = {
	.driver = {
		.name = "rt5033-regulator",
	},
	.probe		= rt5033_regulator_probe,
	.id_table	= rt5033_regulator_id,
};
module_platform_driver(rt5033_regulator_driver);

MODULE_DESCRIPTION("Richtek RT5033 Regulator driver");
MODULE_AUTHOR("Beomho Seo <beomho.seo@samsung.com>");
MODULE_LICENSE("GPL");
