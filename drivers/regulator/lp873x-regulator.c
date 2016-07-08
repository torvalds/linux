/*
 * Regulator driver for LP873X PMIC
 *
 * Copyright (C) 2016 Texas Instruments Incorporated - http://www.ti.com/
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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <linux/mfd/lp873x.h>

#define LP873X_REGULATOR(_name, _id, _of, _ops, _n, _vr, _vm, _er, _em, \
			 _delay, _lr, _nlr, _cr)			\
	[_id] = {							\
		.desc = {						\
			.name			= _name,		\
			.id			= _id,			\
			.of_match		= of_match_ptr(_of),	\
			.regulators_node	= of_match_ptr("regulators"),\
			.ops			= &_ops,		\
			.n_voltages		= _n,			\
			.type			= REGULATOR_VOLTAGE,	\
			.owner			= THIS_MODULE,		\
			.vsel_reg		= _vr,			\
			.vsel_mask		= _vm,			\
			.enable_reg		= _er,			\
			.enable_mask		= _em,			\
			.ramp_delay		= _delay,		\
			.linear_ranges		= _lr,			\
			.n_linear_ranges	= _nlr,			\
		},							\
		.ctrl2_reg = _cr,					\
	}

struct lp873x_regulator {
	struct regulator_desc desc;
	unsigned int ctrl2_reg;
};

static const struct lp873x_regulator regulators[];

static const struct regulator_linear_range buck0_buck1_ranges[] = {
	REGULATOR_LINEAR_RANGE(0, 0x0, 0x13, 0),
	REGULATOR_LINEAR_RANGE(700000, 0x14, 0x17, 10000),
	REGULATOR_LINEAR_RANGE(735000, 0x18, 0x9d, 5000),
	REGULATOR_LINEAR_RANGE(1420000, 0x9e, 0xff, 20000),
};

static const struct regulator_linear_range ldo0_ldo1_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0x0, 0x19, 100000),
};

static unsigned int lp873x_buck_ramp_delay[] = {
	30000, 15000, 10000, 7500, 3800, 1900, 940, 470
};

/* LP873X BUCK current limit */
static const unsigned int lp873x_buck_uA[] = {
	1500000, 2000000, 2500000, 3000000, 3500000, 4000000,
};

static int lp873x_buck_set_ramp_delay(struct regulator_dev *rdev,
				      int ramp_delay)
{
	int id = rdev_get_id(rdev);
	struct lp873x *lp873 = rdev_get_drvdata(rdev);
	unsigned int reg;
	int ret;

	if (ramp_delay <= 470)
		reg = 7;
	else if (ramp_delay <= 940)
		reg = 6;
	else if (ramp_delay <= 1900)
		reg = 5;
	else if (ramp_delay <= 3800)
		reg = 4;
	else if (ramp_delay <= 7500)
		reg = 3;
	else if (ramp_delay <= 10000)
		reg = 2;
	else if (ramp_delay <= 15000)
		reg = 1;
	else
		reg = 0;

	ret = regmap_update_bits(lp873->regmap, regulators[id].ctrl2_reg,
				 LP873X_BUCK0_CTRL_2_BUCK0_SLEW_RATE,
				 reg << __ffs(LP873X_BUCK0_CTRL_2_BUCK0_SLEW_RATE));
	if (ret) {
		dev_err(lp873->dev, "SLEW RATE write failed: %d\n", ret);
		return ret;
	}

	rdev->constraints->ramp_delay = lp873x_buck_ramp_delay[reg];

	return 0;
}

static int lp873x_buck_set_current_limit(struct regulator_dev *rdev,
					 int min_uA, int max_uA)
{
	int id = rdev_get_id(rdev);
	struct lp873x *lp873 = rdev_get_drvdata(rdev);
	int i;

	for (i = ARRAY_SIZE(lp873x_buck_uA) - 1; i >= 0; i--) {
		if (lp873x_buck_uA[i] >= min_uA &&
		    lp873x_buck_uA[i] <= max_uA)
			return regmap_update_bits(lp873->regmap,
						  regulators[id].ctrl2_reg,
						  LP873X_BUCK0_CTRL_2_BUCK0_ILIM,
						  i << __ffs(LP873X_BUCK0_CTRL_2_BUCK0_ILIM));
	}

	return -EINVAL;
}

static int lp873x_buck_get_current_limit(struct regulator_dev *rdev)
{
	int id = rdev_get_id(rdev);
	struct lp873x *lp873 = rdev_get_drvdata(rdev);
	int ret;
	unsigned int val;

	ret = regmap_read(lp873->regmap, regulators[id].ctrl2_reg, &val);
	if (ret)
		return ret;

	val = (val & LP873X_BUCK0_CTRL_2_BUCK0_ILIM) >>
	       __ffs(LP873X_BUCK0_CTRL_2_BUCK0_ILIM);

	return (val < ARRAY_SIZE(lp873x_buck_uA)) ?
			lp873x_buck_uA[val] : -EINVAL;
}

/* Operations permitted on BUCK0, BUCK1 */
static struct regulator_ops lp873x_buck01_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_ramp_delay		= lp873x_buck_set_ramp_delay,
	.set_current_limit	= lp873x_buck_set_current_limit,
	.get_current_limit	= lp873x_buck_get_current_limit,
};

/* Operations permitted on LDO0 and LDO1 */
static struct regulator_ops lp873x_ldo01_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
};

static const struct lp873x_regulator regulators[] = {
	LP873X_REGULATOR("BUCK0", LP873X_BUCK_0, "buck0", lp873x_buck01_ops,
			 256, LP873X_REG_BUCK0_VOUT,
			 LP873X_BUCK0_VOUT_BUCK0_VSET, LP873X_REG_BUCK0_CTRL_1,
			 LP873X_BUCK0_CTRL_1_BUCK0_EN, 10000,
			 buck0_buck1_ranges, 4, LP873X_REG_BUCK0_CTRL_2),
	LP873X_REGULATOR("BUCK1", LP873X_BUCK_1, "buck1", lp873x_buck01_ops,
			 256, LP873X_REG_BUCK1_VOUT,
			 LP873X_BUCK1_VOUT_BUCK1_VSET, LP873X_REG_BUCK1_CTRL_1,
			 LP873X_BUCK1_CTRL_1_BUCK1_EN, 10000,
			 buck0_buck1_ranges, 4, LP873X_REG_BUCK1_CTRL_2),
	LP873X_REGULATOR("LDO0", LP873X_LDO_0, "ldo0", lp873x_ldo01_ops, 26,
			 LP873X_REG_LDO0_VOUT, LP873X_LDO0_VOUT_LDO0_VSET,
			 LP873X_REG_LDO0_CTRL,
			 LP873X_LDO0_CTRL_LDO0_EN, 0, ldo0_ldo1_ranges, 1,
			 0xFF),
	LP873X_REGULATOR("LDO1", LP873X_LDO_1, "ldo1", lp873x_ldo01_ops, 26,
			 LP873X_REG_LDO1_VOUT, LP873X_LDO1_VOUT_LDO1_VSET,
			 LP873X_REG_LDO1_CTRL,
			 LP873X_LDO1_CTRL_LDO1_EN, 0, ldo0_ldo1_ranges, 1,
			 0xFF),
};

static int lp873x_regulator_probe(struct platform_device *pdev)
{
	struct lp873x *lp873 = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	int i;

	platform_set_drvdata(pdev, lp873);

	config.dev = &pdev->dev;
	config.dev->of_node = lp873->dev->of_node;
	config.driver_data = lp873;
	config.regmap = lp873->regmap;

	for (i = 0; i < ARRAY_SIZE(regulators); i++) {
		rdev = devm_regulator_register(&pdev->dev, &regulators[i].desc,
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(lp873->dev, "failed to register %s regulator\n",
				pdev->name);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct platform_device_id lp873x_regulator_id_table[] = {
	{ "lp873x-regulator", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, lp873x_regulator_id_table);

static struct platform_driver lp873x_regulator_driver = {
	.driver = {
		.name = "lp873x-pmic",
	},
	.probe = lp873x_regulator_probe,
	.id_table = lp873x_regulator_id_table,
};
module_platform_driver(lp873x_regulator_driver);

MODULE_AUTHOR("J Keerthy <j-keerthy@ti.com>");
MODULE_DESCRIPTION("LP873X voltage regulator driver");
MODULE_ALIAS("platform:lp873x-pmic");
MODULE_LICENSE("GPL v2");
