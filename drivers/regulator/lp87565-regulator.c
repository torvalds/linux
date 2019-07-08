// SPDX-License-Identifier: GPL-2.0-only
/*
 * Regulator driver for LP87565 PMIC
 *
 * Copyright (C) 2017 Texas Instruments Incorporated - http://www.ti.com/
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <linux/mfd/lp87565.h>

#define LP87565_REGULATOR(_name, _id, _of, _ops, _n, _vr, _vm, _er, _em, \
			 _delay, _lr, _cr)				\
	[_id] = {							\
		.desc = {						\
			.name			= _name,		\
			.supply_name		= _of "-in",		\
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
			.n_linear_ranges	= ARRAY_SIZE(_lr),	\
			.curr_table = lp87565_buck_uA,			\
			.n_current_limits = ARRAY_SIZE(lp87565_buck_uA),\
			.csel_reg = (_cr),				\
			.csel_mask = LP87565_BUCK_CTRL_2_ILIM,		\
		},							\
		.ctrl2_reg = _cr,					\
	}

struct lp87565_regulator {
	struct regulator_desc desc;
	unsigned int ctrl2_reg;
};

static const struct lp87565_regulator regulators[];

static const struct regulator_linear_range buck0_1_2_3_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0xA, 0x17, 10000),
	REGULATOR_LINEAR_RANGE(735000, 0x18, 0x9d, 5000),
	REGULATOR_LINEAR_RANGE(1420000, 0x9e, 0xff, 20000),
};

static const unsigned int lp87565_buck_ramp_delay[] = {
	30000, 15000, 10000, 7500, 3800, 1900, 940, 470
};

/* LP87565 BUCK current limit */
static const unsigned int lp87565_buck_uA[] = {
	1500000, 2000000, 2500000, 3000000, 3500000, 4000000, 4500000, 5000000,
};

static int lp87565_buck_set_ramp_delay(struct regulator_dev *rdev,
				       int ramp_delay)
{
	int id = rdev_get_id(rdev);
	struct lp87565 *lp87565 = rdev_get_drvdata(rdev);
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

	ret = regmap_update_bits(lp87565->regmap, regulators[id].ctrl2_reg,
				 LP87565_BUCK_CTRL_2_SLEW_RATE,
				 reg << __ffs(LP87565_BUCK_CTRL_2_SLEW_RATE));
	if (ret) {
		dev_err(lp87565->dev, "SLEW RATE write failed: %d\n", ret);
		return ret;
	}

	rdev->constraints->ramp_delay = lp87565_buck_ramp_delay[reg];

	/* Conservatively give a 15% margin */
	rdev->constraints->ramp_delay =
				rdev->constraints->ramp_delay * 85 / 100;

	return 0;
}

/* Operations permitted on BUCKs */
static const struct regulator_ops lp87565_buck_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_ramp_delay		= lp87565_buck_set_ramp_delay,
	.set_current_limit	= regulator_set_current_limit_regmap,
	.get_current_limit	= regulator_get_current_limit_regmap,
};

static const struct lp87565_regulator regulators[] = {
	LP87565_REGULATOR("BUCK0", LP87565_BUCK_0, "buck0", lp87565_buck_ops,
			  256, LP87565_REG_BUCK0_VOUT, LP87565_BUCK_VSET,
			  LP87565_REG_BUCK0_CTRL_1,
			  LP87565_BUCK_CTRL_1_EN, 3230,
			  buck0_1_2_3_ranges, LP87565_REG_BUCK0_CTRL_2),
	LP87565_REGULATOR("BUCK1", LP87565_BUCK_1, "buck1", lp87565_buck_ops,
			  256, LP87565_REG_BUCK1_VOUT, LP87565_BUCK_VSET,
			  LP87565_REG_BUCK1_CTRL_1,
			  LP87565_BUCK_CTRL_1_EN, 3230,
			  buck0_1_2_3_ranges, LP87565_REG_BUCK1_CTRL_2),
	LP87565_REGULATOR("BUCK2", LP87565_BUCK_2, "buck2", lp87565_buck_ops,
			  256, LP87565_REG_BUCK2_VOUT, LP87565_BUCK_VSET,
			  LP87565_REG_BUCK2_CTRL_1,
			  LP87565_BUCK_CTRL_1_EN, 3230,
			  buck0_1_2_3_ranges, LP87565_REG_BUCK2_CTRL_2),
	LP87565_REGULATOR("BUCK3", LP87565_BUCK_3, "buck3", lp87565_buck_ops,
			  256, LP87565_REG_BUCK3_VOUT, LP87565_BUCK_VSET,
			  LP87565_REG_BUCK3_CTRL_1,
			  LP87565_BUCK_CTRL_1_EN, 3230,
			  buck0_1_2_3_ranges, LP87565_REG_BUCK3_CTRL_2),
	LP87565_REGULATOR("BUCK10", LP87565_BUCK_10, "buck10", lp87565_buck_ops,
			  256, LP87565_REG_BUCK0_VOUT, LP87565_BUCK_VSET,
			  LP87565_REG_BUCK0_CTRL_1,
			  LP87565_BUCK_CTRL_1_EN |
			  LP87565_BUCK_CTRL_1_FPWM_MP_0_2, 3230,
			  buck0_1_2_3_ranges, LP87565_REG_BUCK0_CTRL_2),
	LP87565_REGULATOR("BUCK23", LP87565_BUCK_23, "buck23", lp87565_buck_ops,
			  256, LP87565_REG_BUCK2_VOUT, LP87565_BUCK_VSET,
			  LP87565_REG_BUCK2_CTRL_1,
			  LP87565_BUCK_CTRL_1_EN, 3230,
			  buck0_1_2_3_ranges, LP87565_REG_BUCK2_CTRL_2),
};

static int lp87565_regulator_probe(struct platform_device *pdev)
{
	struct lp87565 *lp87565 = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	int i, min_idx = LP87565_BUCK_0, max_idx = LP87565_BUCK_3;

	platform_set_drvdata(pdev, lp87565);

	config.dev = &pdev->dev;
	config.dev->of_node = lp87565->dev->of_node;
	config.driver_data = lp87565;
	config.regmap = lp87565->regmap;

	if (lp87565->dev_type == LP87565_DEVICE_TYPE_LP87565_Q1) {
		min_idx = LP87565_BUCK_10;
		max_idx = LP87565_BUCK_23;
	}

	for (i = min_idx; i <= max_idx; i++) {
		rdev = devm_regulator_register(&pdev->dev, &regulators[i].desc,
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(lp87565->dev, "failed to register %s regulator\n",
				pdev->name);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct platform_device_id lp87565_regulator_id_table[] = {
	{ "lp87565-regulator", },
	{ "lp87565-q1-regulator", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, lp87565_regulator_id_table);

static struct platform_driver lp87565_regulator_driver = {
	.driver = {
		.name = "lp87565-pmic",
	},
	.probe = lp87565_regulator_probe,
	.id_table = lp87565_regulator_id_table,
};
module_platform_driver(lp87565_regulator_driver);

MODULE_AUTHOR("J Keerthy <j-keerthy@ti.com>");
MODULE_DESCRIPTION("LP87565 voltage regulator driver");
MODULE_LICENSE("GPL v2");
