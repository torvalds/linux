/*
 * tps65217-regulator.c
 *
 * Regulator driver for TPS65217 PMIC
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>

#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/tps65217.h>

#define TPS65217_REGULATOR(_name, _id, _of_match, _ops, _n, _vr, _vm, _em, \
			   _t, _lr, _nlr,  _sr, _sm)	\
	{						\
		.name		= _name,		\
		.id		= _id,			\
		.of_match       = of_match_ptr(_of_match),    \
		.regulators_node= of_match_ptr("regulators"), \
		.ops		= &_ops,		\
		.n_voltages	= _n,			\
		.type		= REGULATOR_VOLTAGE,	\
		.owner		= THIS_MODULE,		\
		.vsel_reg	= _vr,			\
		.vsel_mask	= _vm,			\
		.enable_reg	= TPS65217_REG_ENABLE,	\
		.enable_mask	= _em,			\
		.volt_table	= _t,			\
		.linear_ranges	= _lr,			\
		.n_linear_ranges = _nlr,		\
		.bypass_reg	= _sr,			\
		.bypass_mask	= _sm,			\
	}						\

static const unsigned int LDO1_VSEL_table[] = {
	1000000, 1100000, 1200000, 1250000,
	1300000, 1350000, 1400000, 1500000,
	1600000, 1800000, 2500000, 2750000,
	2800000, 3000000, 3100000, 3300000,
};

static const struct regulator_linear_range tps65217_uv1_ranges[] = {
	REGULATOR_LINEAR_RANGE(900000, 0, 24, 25000),
	REGULATOR_LINEAR_RANGE(1550000, 25, 52, 50000),
	REGULATOR_LINEAR_RANGE(3000000, 53, 55, 100000),
	REGULATOR_LINEAR_RANGE(3300000, 56, 63, 0),
};

static const struct regulator_linear_range tps65217_uv2_ranges[] = {
	REGULATOR_LINEAR_RANGE(1500000, 0, 8, 50000),
	REGULATOR_LINEAR_RANGE(2000000, 9, 13, 100000),
	REGULATOR_LINEAR_RANGE(2450000, 14, 31, 50000),
};

static int tps65217_pmic_enable(struct regulator_dev *dev)
{
	struct tps65217 *tps = rdev_get_drvdata(dev);
	int rid = rdev_get_id(dev);

	if (rid < TPS65217_DCDC_1 || rid > TPS65217_LDO_4)
		return -EINVAL;

	/* Enable the regulator and password protection is level 1 */
	return tps65217_set_bits(tps, TPS65217_REG_ENABLE,
				 dev->desc->enable_mask, dev->desc->enable_mask,
				 TPS65217_PROTECT_L1);
}

static int tps65217_pmic_disable(struct regulator_dev *dev)
{
	struct tps65217 *tps = rdev_get_drvdata(dev);
	int rid = rdev_get_id(dev);

	if (rid < TPS65217_DCDC_1 || rid > TPS65217_LDO_4)
		return -EINVAL;

	/* Disable the regulator and password protection is level 1 */
	return tps65217_clear_bits(tps, TPS65217_REG_ENABLE,
				   dev->desc->enable_mask, TPS65217_PROTECT_L1);
}

static int tps65217_pmic_set_voltage_sel(struct regulator_dev *dev,
					 unsigned selector)
{
	int ret;
	struct tps65217 *tps = rdev_get_drvdata(dev);
	unsigned int rid = rdev_get_id(dev);

	/* Set the voltage based on vsel value and write protect level is 2 */
	ret = tps65217_set_bits(tps, dev->desc->vsel_reg, dev->desc->vsel_mask,
				selector, TPS65217_PROTECT_L2);

	/* Set GO bit for DCDCx to initiate voltage transistion */
	switch (rid) {
	case TPS65217_DCDC_1 ... TPS65217_DCDC_3:
		ret = tps65217_set_bits(tps, TPS65217_REG_DEFSLEW,
				       TPS65217_DEFSLEW_GO, TPS65217_DEFSLEW_GO,
				       TPS65217_PROTECT_L2);
		break;
	}

	return ret;
}

static int tps65217_pmic_set_suspend_enable(struct regulator_dev *dev)
{
	struct tps65217 *tps = rdev_get_drvdata(dev);
	unsigned int rid = rdev_get_id(dev);

	if (rid < TPS65217_DCDC_1 || rid > TPS65217_LDO_4)
		return -EINVAL;

	return tps65217_clear_bits(tps, dev->desc->bypass_reg,
				   dev->desc->bypass_mask,
				   TPS65217_PROTECT_L1);
}

static int tps65217_pmic_set_suspend_disable(struct regulator_dev *dev)
{
	struct tps65217 *tps = rdev_get_drvdata(dev);
	unsigned int rid = rdev_get_id(dev);

	if (rid < TPS65217_DCDC_1 || rid > TPS65217_LDO_4)
		return -EINVAL;

	if (!tps->strobes[rid])
		return -EINVAL;

	return tps65217_set_bits(tps, dev->desc->bypass_reg,
				 dev->desc->bypass_mask,
				 tps->strobes[rid], TPS65217_PROTECT_L1);
}

/* Operations permitted on DCDCx, LDO2, LDO3 and LDO4 */
static const struct regulator_ops tps65217_pmic_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= tps65217_pmic_enable,
	.disable		= tps65217_pmic_disable,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= tps65217_pmic_set_voltage_sel,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.set_suspend_enable	= tps65217_pmic_set_suspend_enable,
	.set_suspend_disable	= tps65217_pmic_set_suspend_disable,
};

/* Operations permitted on LDO1 */
static const struct regulator_ops tps65217_pmic_ldo1_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= tps65217_pmic_enable,
	.disable		= tps65217_pmic_disable,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= tps65217_pmic_set_voltage_sel,
	.list_voltage		= regulator_list_voltage_table,
	.map_voltage		= regulator_map_voltage_ascend,
	.set_suspend_enable	= tps65217_pmic_set_suspend_enable,
	.set_suspend_disable	= tps65217_pmic_set_suspend_disable,
};

static const struct regulator_desc regulators[] = {
	TPS65217_REGULATOR("DCDC1", TPS65217_DCDC_1, "dcdc1",
			   tps65217_pmic_ops, 64, TPS65217_REG_DEFDCDC1,
			   TPS65217_DEFDCDCX_DCDC_MASK, TPS65217_ENABLE_DC1_EN,
			   NULL, tps65217_uv1_ranges,
			   ARRAY_SIZE(tps65217_uv1_ranges), TPS65217_REG_SEQ1,
			   TPS65217_SEQ1_DC1_SEQ_MASK),
	TPS65217_REGULATOR("DCDC2", TPS65217_DCDC_2, "dcdc2",
			   tps65217_pmic_ops, 64, TPS65217_REG_DEFDCDC2,
			   TPS65217_DEFDCDCX_DCDC_MASK, TPS65217_ENABLE_DC2_EN,
			   NULL, tps65217_uv1_ranges,
			   ARRAY_SIZE(tps65217_uv1_ranges), TPS65217_REG_SEQ1,
			   TPS65217_SEQ1_DC2_SEQ_MASK),
	TPS65217_REGULATOR("DCDC3", TPS65217_DCDC_3, "dcdc3",
			   tps65217_pmic_ops, 64, TPS65217_REG_DEFDCDC3,
			   TPS65217_DEFDCDCX_DCDC_MASK, TPS65217_ENABLE_DC3_EN,
			   NULL, tps65217_uv1_ranges,
			   ARRAY_SIZE(tps65217_uv1_ranges), TPS65217_REG_SEQ2,
			   TPS65217_SEQ2_DC3_SEQ_MASK),
	TPS65217_REGULATOR("LDO1", TPS65217_LDO_1, "ldo1",
			   tps65217_pmic_ldo1_ops, 16, TPS65217_REG_DEFLDO1,
			   TPS65217_DEFLDO1_LDO1_MASK, TPS65217_ENABLE_LDO1_EN,
			   LDO1_VSEL_table, NULL, 0, TPS65217_REG_SEQ2,
			   TPS65217_SEQ2_LDO1_SEQ_MASK),
	TPS65217_REGULATOR("LDO2", TPS65217_LDO_2, "ldo2", tps65217_pmic_ops,
			   64, TPS65217_REG_DEFLDO2,
			   TPS65217_DEFLDO2_LDO2_MASK, TPS65217_ENABLE_LDO2_EN,
			   NULL, tps65217_uv1_ranges,
			   ARRAY_SIZE(tps65217_uv1_ranges), TPS65217_REG_SEQ3,
			   TPS65217_SEQ3_LDO2_SEQ_MASK),
	TPS65217_REGULATOR("LDO3", TPS65217_LDO_3, "ldo3", tps65217_pmic_ops,
			   32, TPS65217_REG_DEFLS1, TPS65217_DEFLDO3_LDO3_MASK,
			   TPS65217_ENABLE_LS1_EN | TPS65217_DEFLDO3_LDO3_EN,
			   NULL, tps65217_uv2_ranges,
			   ARRAY_SIZE(tps65217_uv2_ranges), TPS65217_REG_SEQ3,
			   TPS65217_SEQ3_LDO3_SEQ_MASK),
	TPS65217_REGULATOR("LDO4", TPS65217_LDO_4, "ldo4", tps65217_pmic_ops,
			   32, TPS65217_REG_DEFLS2, TPS65217_DEFLDO4_LDO4_MASK,
			   TPS65217_ENABLE_LS2_EN | TPS65217_DEFLDO4_LDO4_EN,
			   NULL, tps65217_uv2_ranges,
			   ARRAY_SIZE(tps65217_uv2_ranges), TPS65217_REG_SEQ4,
			   TPS65217_SEQ4_LDO4_SEQ_MASK),
};

static int tps65217_regulator_probe(struct platform_device *pdev)
{
	struct tps65217 *tps = dev_get_drvdata(pdev->dev.parent);
	struct tps65217_board *pdata = dev_get_platdata(tps->dev);
	struct regulator_dev *rdev;
	struct regulator_config config = { };
	int i, ret;
	unsigned int val;

	/* Allocate memory for strobes */
	tps->strobes = devm_kcalloc(&pdev->dev,
				    TPS65217_NUM_REGULATOR, sizeof(u8),
				    GFP_KERNEL);
	if (!tps->strobes)
		return -ENOMEM;

	platform_set_drvdata(pdev, tps);

	for (i = 0; i < TPS65217_NUM_REGULATOR; i++) {
		/* Register the regulators */
		config.dev = tps->dev;
		if (pdata)
			config.init_data = pdata->tps65217_init_data[i];
		config.driver_data = tps;
		config.regmap = tps->regmap;

		rdev = devm_regulator_register(&pdev->dev, &regulators[i],
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(tps->dev, "failed to register %s regulator\n",
				pdev->name);
			return PTR_ERR(rdev);
		}

		/* Store default strobe info */
		ret = tps65217_reg_read(tps, regulators[i].bypass_reg, &val);
		tps->strobes[i] = val & regulators[i].bypass_mask;
	}

	return 0;
}

static struct platform_driver tps65217_regulator_driver = {
	.driver = {
		.name = "tps65217-pmic",
	},
	.probe = tps65217_regulator_probe,
};

static int __init tps65217_regulator_init(void)
{
	return platform_driver_register(&tps65217_regulator_driver);
}
subsys_initcall(tps65217_regulator_init);

static void __exit tps65217_regulator_exit(void)
{
	platform_driver_unregister(&tps65217_regulator_driver);
}
module_exit(tps65217_regulator_exit);

MODULE_AUTHOR("AnilKumar Ch <anilkumar@ti.com>");
MODULE_DESCRIPTION("TPS65217 voltage regulator driver");
MODULE_ALIAS("platform:tps65217-pmic");
MODULE_LICENSE("GPL v2");
