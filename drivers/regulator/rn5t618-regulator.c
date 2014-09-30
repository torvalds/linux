/*
 * Regulator driver for Ricoh RN5T618 PMIC
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/mfd/rn5t618.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

static struct regulator_ops rn5t618_reg_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
};

#define REG(rid, ereg, emask, vreg, vmask, min, max, step)		\
	[RN5T618_##rid] = {						\
		.name		= #rid,					\
		.id		= RN5T618_##rid,			\
		.type		= REGULATOR_VOLTAGE,			\
		.owner		= THIS_MODULE,				\
		.ops		= &rn5t618_reg_ops,			\
		.n_voltages	= ((max) - (min)) / (step) + 1,		\
		.min_uV		= (min),				\
		.uV_step	= (step),				\
		.enable_reg	= RN5T618_##ereg,			\
		.enable_mask	= (emask),				\
		.vsel_reg	= RN5T618_##vreg,			\
		.vsel_mask	= (vmask),				\
	}

static struct regulator_desc rn5t618_regulators[] = {
	/* DCDC */
	REG(DCDC1, DC1CTL, BIT(0), DC1DAC, 0xff, 600000, 3500000, 12500),
	REG(DCDC2, DC2CTL, BIT(0), DC2DAC, 0xff, 600000, 3500000, 12500),
	REG(DCDC3, DC3CTL, BIT(0), DC3DAC, 0xff, 600000, 3500000, 12500),
	/* LDO */
	REG(LDO1, LDOEN1, BIT(0), LDO1DAC, 0x7f, 900000, 3500000, 25000),
	REG(LDO2, LDOEN1, BIT(1), LDO2DAC, 0x7f, 900000, 3500000, 25000),
	REG(LDO3, LDOEN1, BIT(2), LDO3DAC, 0x7f, 600000, 3500000, 25000),
	REG(LDO4, LDOEN1, BIT(3), LDO4DAC, 0x7f, 900000, 3500000, 25000),
	REG(LDO5, LDOEN1, BIT(4), LDO5DAC, 0x7f, 900000, 3500000, 25000),
	/* LDO RTC */
	REG(LDORTC1, LDOEN2, BIT(4), LDORTCDAC, 0x7f, 1700000, 3500000, 25000),
	REG(LDORTC2, LDOEN2, BIT(5), LDORTC2DAC, 0x7f, 900000, 3500000, 25000),
};

static struct of_regulator_match rn5t618_matches[] = {
	[RN5T618_DCDC1]		= { .name = "DCDC1" },
	[RN5T618_DCDC2]		= { .name = "DCDC2" },
	[RN5T618_DCDC3]		= { .name = "DCDC3" },
	[RN5T618_LDO1]		= { .name = "LDO1" },
	[RN5T618_LDO2]		= { .name = "LDO2" },
	[RN5T618_LDO3]		= { .name = "LDO3" },
	[RN5T618_LDO4]		= { .name = "LDO4" },
	[RN5T618_LDO5]		= { .name = "LDO5" },
	[RN5T618_LDORTC1]	= { .name = "LDORTC1" },
	[RN5T618_LDORTC2]	= { .name = "LDORTC2" },
};

static int rn5t618_regulator_parse_dt(struct platform_device *pdev)
{
	struct device_node *np, *regulators;
	int ret;

	np = of_node_get(pdev->dev.parent->of_node);
	if (!np)
		return 0;

	regulators = of_get_child_by_name(np, "regulators");
	if (!regulators) {
		dev_err(&pdev->dev, "regulators node not found\n");
		return -EINVAL;
	}

	ret = of_regulator_match(&pdev->dev, regulators, rn5t618_matches,
				 ARRAY_SIZE(rn5t618_matches));
	of_node_put(regulators);
	if (ret < 0) {
		dev_err(&pdev->dev, "error parsing regulator init data: %d\n",
			ret);
	}

	return 0;
}

static int rn5t618_regulator_probe(struct platform_device *pdev)
{
	struct rn5t618 *rn5t618 = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	int ret, i;

	ret = rn5t618_regulator_parse_dt(pdev);
	if (ret)
		return ret;

	for (i = 0; i < RN5T618_REG_NUM; i++) {
		config.dev = &pdev->dev;
		config.init_data = rn5t618_matches[i].init_data;
		config.of_node = rn5t618_matches[i].of_node;
		config.regmap = rn5t618->regmap;

		rdev = devm_regulator_register(&pdev->dev,
					       &rn5t618_regulators[i],
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "failed to register %s regulator\n",
				rn5t618_regulators[i].name);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static struct platform_driver rn5t618_regulator_driver = {
	.probe = rn5t618_regulator_probe,
	.driver = {
		.name	= "rn5t618-regulator",
	},
};

module_platform_driver(rn5t618_regulator_driver);

MODULE_AUTHOR("Beniamino Galvani <b.galvani@gmail.com>");
MODULE_DESCRIPTION("RN5T618 regulator driver");
MODULE_LICENSE("GPL v2");
