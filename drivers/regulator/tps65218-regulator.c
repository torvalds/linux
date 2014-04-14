/*
 * tps65218-regulator.c
 *
 * Regulator driver for TPS65218 PMIC
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether expressed or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/tps65218.h>

static unsigned int tps65218_ramp_delay = 4000;

enum tps65218_regulators { DCDC1, DCDC2, DCDC3, DCDC4, DCDC5, DCDC6, LDO1 };

#define TPS65218_REGULATOR(_name, _id, _ops, _n, _vr, _vm, _er, _em, _t, \
			    _lr, _nlr)				\
	{							\
		.name			= _name,		\
		.id			= _id,			\
		.ops			= &_ops,		\
		.n_voltages		= _n,			\
		.type			= REGULATOR_VOLTAGE,	\
		.owner			= THIS_MODULE,		\
		.vsel_reg		= _vr,			\
		.vsel_mask		= _vm,			\
		.enable_reg		= _er,			\
		.enable_mask		= _em,			\
		.volt_table		= _t,			\
		.linear_ranges		= _lr,			\
		.n_linear_ranges	= _nlr,			\
	}							\

#define TPS65218_INFO(_id, _nm, _min, _max)	\
	{						\
		.id		= _id,			\
		.name		= _nm,			\
		.min_uV		= _min,			\
		.max_uV		= _max,			\
	}

static const struct regulator_linear_range dcdc1_dcdc2_ranges[] = {
	REGULATOR_LINEAR_RANGE(850000, 0x0, 0x32, 10000),
	REGULATOR_LINEAR_RANGE(1375000, 0x33, 0x3f, 25000),
};

static const struct regulator_linear_range ldo1_dcdc3_ranges[] = {
	REGULATOR_LINEAR_RANGE(900000, 0x0, 0x1a, 25000),
	REGULATOR_LINEAR_RANGE(1600000, 0x1b, 0x3f, 50000),
};

static const struct regulator_linear_range dcdc4_ranges[] = {
	REGULATOR_LINEAR_RANGE(1175000, 0x0, 0xf, 25000),
	REGULATOR_LINEAR_RANGE(1550000, 0x10, 0x34, 50000),
};

static struct tps_info tps65218_pmic_regs[] = {
	TPS65218_INFO(0, "DCDC1", 850000, 167500),
	TPS65218_INFO(1, "DCDC2", 850000, 1675000),
	TPS65218_INFO(2, "DCDC3", 900000, 3400000),
	TPS65218_INFO(3, "DCDC4", 1175000, 3400000),
	TPS65218_INFO(4, "DCDC5", 1000000, 1000000),
	TPS65218_INFO(5, "DCDC6", 1800000, 1800000),
	TPS65218_INFO(6, "LDO1", 900000, 3400000),
};

#define TPS65218_OF_MATCH(comp, label) \
	{ \
		.compatible = comp, \
		.data = &label, \
	}

static const struct of_device_id tps65218_of_match[] = {
	TPS65218_OF_MATCH("ti,tps65218-dcdc1", tps65218_pmic_regs[DCDC1]),
	TPS65218_OF_MATCH("ti,tps65218-dcdc2", tps65218_pmic_regs[DCDC2]),
	TPS65218_OF_MATCH("ti,tps65218-dcdc3", tps65218_pmic_regs[DCDC3]),
	TPS65218_OF_MATCH("ti,tps65218-dcdc4", tps65218_pmic_regs[DCDC4]),
	TPS65218_OF_MATCH("ti,tps65218-dcdc5", tps65218_pmic_regs[DCDC5]),
	TPS65218_OF_MATCH("ti,tps65218-dcdc6", tps65218_pmic_regs[DCDC6]),
	TPS65218_OF_MATCH("ti,tps65218-ldo1", tps65218_pmic_regs[LDO1]),
	{ }
};
MODULE_DEVICE_TABLE(of, tps65218_of_match);

static int tps65218_pmic_set_voltage_sel(struct regulator_dev *dev,
					 unsigned selector)
{
	int ret;
	struct tps65218 *tps = rdev_get_drvdata(dev);
	unsigned int rid = rdev_get_id(dev);

	/* Set the voltage based on vsel value and write protect level is 2 */
	ret = tps65218_set_bits(tps, dev->desc->vsel_reg, dev->desc->vsel_mask,
				selector, TPS65218_PROTECT_L1);

	/* Set GO bit for DCDC1/2 to initiate voltage transistion */
	switch (rid) {
	case TPS65218_DCDC_1:
	case TPS65218_DCDC_2:
		ret = tps65218_set_bits(tps, TPS65218_REG_CONTRL_SLEW_RATE,
					TPS65218_SLEW_RATE_GO,
					TPS65218_SLEW_RATE_GO,
					TPS65218_PROTECT_L1);
		break;
	}

	return ret;
}

static int tps65218_pmic_enable(struct regulator_dev *dev)
{
	struct tps65218 *tps = rdev_get_drvdata(dev);
	unsigned int rid = rdev_get_id(dev);

	if (rid < TPS65218_DCDC_1 || rid > TPS65218_LDO_1)
		return -EINVAL;

	/* Enable the regulator and password protection is level 1 */
	return tps65218_set_bits(tps, dev->desc->enable_reg,
				 dev->desc->enable_mask, dev->desc->enable_mask,
				 TPS65218_PROTECT_L1);
}

static int tps65218_pmic_disable(struct regulator_dev *dev)
{
	struct tps65218 *tps = rdev_get_drvdata(dev);
	unsigned int rid = rdev_get_id(dev);

	if (rid < TPS65218_DCDC_1 || rid > TPS65218_LDO_1)
		return -EINVAL;

	/* Disable the regulator and password protection is level 1 */
	return tps65218_clear_bits(tps, dev->desc->enable_reg,
				   dev->desc->enable_mask, TPS65218_PROTECT_L1);
}

static int tps65218_set_voltage_time_sel(struct regulator_dev *rdev,
	unsigned int old_selector, unsigned int new_selector)
{
	int old_uv, new_uv;

	old_uv = regulator_list_voltage_linear_range(rdev, old_selector);
	if (old_uv < 0)
		return old_uv;

	new_uv = regulator_list_voltage_linear_range(rdev, new_selector);
	if (new_uv < 0)
		return new_uv;

	return DIV_ROUND_UP(abs(old_uv - new_uv), tps65218_ramp_delay);
}

/* Operations permitted on DCDC1, DCDC2 */
static struct regulator_ops tps65218_dcdc12_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= tps65218_pmic_enable,
	.disable		= tps65218_pmic_disable,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= tps65218_pmic_set_voltage_sel,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.set_voltage_time_sel	= tps65218_set_voltage_time_sel,
};

/* Operations permitted on DCDC3, DCDC4 and LDO1 */
static struct regulator_ops tps65218_ldo1_dcdc34_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= tps65218_pmic_enable,
	.disable		= tps65218_pmic_disable,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= tps65218_pmic_set_voltage_sel,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
};

/* Operations permitted on DCDC5, DCDC6 */
static struct regulator_ops tps65218_dcdc56_pmic_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= tps65218_pmic_enable,
	.disable		= tps65218_pmic_disable,
};

static const struct regulator_desc regulators[] = {
	TPS65218_REGULATOR("DCDC1", TPS65218_DCDC_1, tps65218_dcdc12_ops, 64,
			   TPS65218_REG_CONTROL_DCDC1,
			   TPS65218_CONTROL_DCDC1_MASK,
			   TPS65218_REG_ENABLE1, TPS65218_ENABLE1_DC1_EN, NULL,
			   dcdc1_dcdc2_ranges, 2),
	TPS65218_REGULATOR("DCDC2", TPS65218_DCDC_2, tps65218_dcdc12_ops, 64,
			   TPS65218_REG_CONTROL_DCDC2,
			   TPS65218_CONTROL_DCDC2_MASK,
			   TPS65218_REG_ENABLE1, TPS65218_ENABLE1_DC2_EN, NULL,
			   dcdc1_dcdc2_ranges, 2),
	TPS65218_REGULATOR("DCDC3", TPS65218_DCDC_3, tps65218_ldo1_dcdc34_ops,
			   64, TPS65218_REG_CONTROL_DCDC3,
			   TPS65218_CONTROL_DCDC3_MASK, TPS65218_REG_ENABLE1,
			   TPS65218_ENABLE1_DC3_EN, NULL,
			   ldo1_dcdc3_ranges, 2),
	TPS65218_REGULATOR("DCDC4", TPS65218_DCDC_4, tps65218_ldo1_dcdc34_ops,
			   53, TPS65218_REG_CONTROL_DCDC4,
			   TPS65218_CONTROL_DCDC4_MASK,
			   TPS65218_REG_ENABLE1, TPS65218_ENABLE1_DC4_EN, NULL,
			   dcdc4_ranges, 2),
	TPS65218_REGULATOR("DCDC5", TPS65218_DCDC_5, tps65218_dcdc56_pmic_ops,
			   1, -1, -1, TPS65218_REG_ENABLE1,
			   TPS65218_ENABLE1_DC5_EN, NULL, NULL, 0),
	TPS65218_REGULATOR("DCDC6", TPS65218_DCDC_6, tps65218_dcdc56_pmic_ops,
			   1, -1, -1, TPS65218_REG_ENABLE1,
			   TPS65218_ENABLE1_DC6_EN, NULL, NULL, 0),
	TPS65218_REGULATOR("LDO1", TPS65218_LDO_1, tps65218_ldo1_dcdc34_ops, 64,
			   TPS65218_REG_CONTROL_DCDC4,
			   TPS65218_CONTROL_LDO1_MASK, TPS65218_REG_ENABLE2,
			   TPS65218_ENABLE2_LDO1_EN, NULL, ldo1_dcdc3_ranges,
			   2),
};

static int tps65218_regulator_probe(struct platform_device *pdev)
{
	struct tps65218 *tps = dev_get_drvdata(pdev->dev.parent);
	struct regulator_init_data *init_data;
	const struct tps_info	*template;
	struct regulator_dev *rdev;
	const struct of_device_id	*match;
	struct regulator_config config = { };
	int id;

	match = of_match_device(tps65218_of_match, &pdev->dev);
	if (!match)
		return -ENODEV;

	template = match->data;
	id = template->id;
	init_data = of_get_regulator_init_data(&pdev->dev, pdev->dev.of_node);

	platform_set_drvdata(pdev, tps);

	tps->info[id] = &tps65218_pmic_regs[id];
	config.dev = &pdev->dev;
	config.init_data = init_data;
	config.driver_data = tps;
	config.regmap = tps->regmap;

	rdev = devm_regulator_register(&pdev->dev, &regulators[id], &config);
	if (IS_ERR(rdev)) {
		dev_err(tps->dev, "failed to register %s regulator\n",
			pdev->name);
		return PTR_ERR(rdev);
	}

	return 0;
}

static struct platform_driver tps65218_regulator_driver = {
	.driver = {
		.name = "tps65218-pmic",
		.owner = THIS_MODULE,
		.of_match_table = tps65218_of_match,
	},
	.probe = tps65218_regulator_probe,
};

module_platform_driver(tps65218_regulator_driver);

MODULE_AUTHOR("J Keerthy <j-keerthy@ti.com>");
MODULE_DESCRIPTION("TPS65218 voltage regulator driver");
MODULE_ALIAS("platform:tps65218-pmic");
MODULE_LICENSE("GPL v2");
