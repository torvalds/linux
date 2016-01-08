/*
 * Regulator driver for Rockchip RK818
 *
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: xsf <xsf@rock-chips.com>
 * Author: Zhang Qing <zhangqing@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/mfd/rk808.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/gpio/consumer.h>

/* Field Definitions */
#define RK818_BUCK_VSEL_MASK	0x3f
#define RK818_BUCK4_VSEL_MASK	0x1f
#define RK818_LDO_VSEL_MASK	0x1f
#define RK818_LDO3_VSEL_MASK	0x0f

/* Ramp rate definitions for buck1 / buck2 only */
#define RK818_RAMP_RATE_OFFSET		3
#define RK818_RAMP_RATE_MASK		(3 << RK818_RAMP_RATE_OFFSET)
#define RK818_RAMP_RATE_2MV_PER_US	(0 << RK818_RAMP_RATE_OFFSET)
#define RK818_RAMP_RATE_4MV_PER_US	(1 << RK818_RAMP_RATE_OFFSET)
#define RK818_RAMP_RATE_6MV_PER_US	(2 << RK818_RAMP_RATE_OFFSET)
#define RK818_RAMP_RATE_10MV_PER_US	(3 << RK818_RAMP_RATE_OFFSET)

/* Offset from XXX_ON_VSEL to XXX_SLP_VSEL */
#define RK818_SLP_REG_OFFSET 1

/* Offset from XXX_EN_REG to SLEEP_SET_OFF_XXX */
#define RK818_SLP_SET_OFF_REG_OFFSET 2

/* max steps for increase voltage of Buck1/2, equal 100mv*/
#define MAX_STEPS_ONE_TIME 8

static const int rk818_buck_config_regs[] = {
	RK818_BUCK1_CONFIG_REG,
	RK818_BUCK2_CONFIG_REG,
	RK818_BUCK3_CONFIG_REG,
	RK818_BUCK4_CONFIG_REG,
};

static const struct regulator_linear_range rk818_buck_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(712500, 0, 63, 12500),
};

static const struct regulator_linear_range rk818_buck4_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0, 15, 100000),
};

static const struct regulator_linear_range rk818_ldo_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0, 16, 100000),
};

static const struct regulator_linear_range rk818_ldo3_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0, 13, 100000),
	REGULATOR_LINEAR_RANGE(2500000, 15, 15, 0),
};

static const struct regulator_linear_range rk818_ldo6_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0, 17, 100000),
};

static int rk818_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	unsigned int ramp_value = RK818_RAMP_RATE_10MV_PER_US;
	unsigned int reg = rk818_buck_config_regs[rdev->desc->id -
						  RK818_ID_DCDC1];

	switch (ramp_delay) {
	case 1 ... 2000:
		ramp_value = RK818_RAMP_RATE_2MV_PER_US;
		break;
	case 2001 ... 4000:
		ramp_value = RK818_RAMP_RATE_4MV_PER_US;
		break;
	case 4001 ... 6000:
		ramp_value = RK818_RAMP_RATE_6MV_PER_US;
		break;
	case 6001 ... 10000:
		break;
	default:
		pr_warn("%s ramp_delay: %d not supported, setting 10000\n",
			rdev->desc->name, ramp_delay);
	}

	return regmap_update_bits(rdev->regmap, reg,
				  RK818_RAMP_RATE_MASK, ramp_value);
}

static int rk818_set_suspend_voltage(struct regulator_dev *rdev, int uv)
{
	unsigned int reg;
	int sel = regulator_map_voltage_linear_range(rdev, uv, uv);

	if (sel < 0)
		return -EINVAL;

	reg = rdev->desc->vsel_reg + RK818_SLP_REG_OFFSET;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->vsel_mask,
				  sel);
}

static int rk818_set_suspend_enable(struct regulator_dev *rdev)
{
	unsigned int reg;

	reg = rdev->desc->enable_reg + RK818_SLP_SET_OFF_REG_OFFSET;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->enable_mask,
				  0);
}

static int rk818_set_suspend_disable(struct regulator_dev *rdev)
{
	unsigned int reg;

	reg = rdev->desc->enable_reg + RK818_SLP_SET_OFF_REG_OFFSET;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->enable_mask,
				  rdev->desc->enable_mask);
}

static struct regulator_ops rk818_buck1_2_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_ramp_delay		= rk818_set_ramp_delay,
	.set_suspend_voltage	= rk818_set_suspend_voltage,
	.set_suspend_enable	= rk818_set_suspend_enable,
	.set_suspend_disable	= rk818_set_suspend_disable,
};

static struct regulator_ops rk818_reg_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_suspend_voltage	= rk818_set_suspend_voltage,
	.set_suspend_enable	= rk818_set_suspend_enable,
	.set_suspend_disable	= rk818_set_suspend_disable,
};

static struct regulator_ops rk818_switch_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_suspend_enable	= rk818_set_suspend_enable,
	.set_suspend_disable	= rk818_set_suspend_disable,
};

static const struct regulator_desc rk818_reg[] = {
	{
		.name = "DCDC_REG1",
		.supply_name = "vcc1",
		.id = RK818_ID_DCDC1,
		.ops = &rk818_buck1_2_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 64,
		.linear_ranges = rk818_buck_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk818_buck_voltage_ranges),
		.vsel_reg = RK818_BUCK1_ON_VSEL_REG,
		.vsel_mask = RK818_BUCK_VSEL_MASK,
		.enable_reg = RK818_DCDC_EN_REG,
		.enable_mask = BIT(0),
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG2",
		.supply_name = "vcc2",
		.id = RK818_ID_DCDC2,
		.ops = &rk818_buck1_2_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 64,
		.linear_ranges = rk818_buck_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk818_buck_voltage_ranges),
		.vsel_reg = RK818_BUCK2_ON_VSEL_REG,
		.vsel_mask = RK818_BUCK_VSEL_MASK,
		.enable_reg = RK818_DCDC_EN_REG,
		.enable_mask = BIT(1),
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG3",
		.supply_name = "vcc3",
		.id = RK818_ID_DCDC3,
		.ops = &rk818_switch_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 1,
		.enable_reg = RK818_DCDC_EN_REG,
		.enable_mask = BIT(2),
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG4",
		.supply_name = "vcc4",
		.id = RK818_ID_DCDC4,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 16,
		.linear_ranges = rk818_buck4_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk818_buck4_voltage_ranges),
		.vsel_reg = RK818_BUCK4_ON_VSEL_REG,
		.vsel_mask = RK818_BUCK4_VSEL_MASK,
		.enable_reg = RK818_DCDC_EN_REG,
		.enable_mask = BIT(3),
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG1",
		.supply_name = "vcc6",
		.id = RK818_ID_LDO1,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 17,
		.linear_ranges = rk818_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk818_ldo_voltage_ranges),
		.vsel_reg = RK818_LDO1_ON_VSEL_REG,
		.vsel_mask = RK818_LDO_VSEL_MASK,
		.enable_reg = RK818_LDO_EN_REG,
		.enable_mask = BIT(0),
		.enable_time = 400,
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG2",
		.supply_name = "vcc6",
		.id = RK818_ID_LDO2,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 17,
		.linear_ranges = rk818_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk818_ldo_voltage_ranges),
		.vsel_reg = RK818_LDO2_ON_VSEL_REG,
		.vsel_mask = RK818_LDO_VSEL_MASK,
		.enable_reg = RK818_LDO_EN_REG,
		.enable_mask = BIT(1),
		.enable_time = 400,
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG3",
		.supply_name = "vcc7",
		.id = RK818_ID_LDO3,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 16,
		.linear_ranges = rk818_ldo3_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk818_ldo3_voltage_ranges),
		.vsel_reg = RK818_LDO3_ON_VSEL_REG,
		.vsel_mask = RK818_LDO3_VSEL_MASK,
		.enable_reg = RK818_LDO_EN_REG,
		.enable_mask = BIT(2),
		.enable_time = 400,
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG4",
		.supply_name = "vcc8",
		.id = RK818_ID_LDO4,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 17,
		.linear_ranges = rk818_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk818_ldo_voltage_ranges),
		.vsel_reg = RK818_LDO4_ON_VSEL_REG,
		.vsel_mask = RK818_LDO_VSEL_MASK,
		.enable_reg = RK818_LDO_EN_REG,
		.enable_mask = BIT(3),
		.enable_time = 400,
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG5",
		.supply_name = "vcc7",
		.id = RK818_ID_LDO5,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 17,
		.linear_ranges = rk818_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk818_ldo_voltage_ranges),
		.vsel_reg = RK818_LDO5_ON_VSEL_REG,
		.vsel_mask = RK818_LDO_VSEL_MASK,
		.enable_reg = RK818_LDO_EN_REG,
		.enable_mask = BIT(4),
		.enable_time = 400,
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG6",
		.supply_name = "vcc8",
		.id = RK818_ID_LDO6,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 18,
		.linear_ranges = rk818_ldo6_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk818_ldo6_voltage_ranges),
		.vsel_reg = RK818_LDO6_ON_VSEL_REG,
		.vsel_mask = RK818_LDO_VSEL_MASK,
		.enable_reg = RK818_LDO_EN_REG,
		.enable_mask = BIT(5),
		.enable_time = 400,
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG7",
		.supply_name = "vcc7",
		.id = RK818_ID_LDO7,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 18,
		.linear_ranges = rk818_ldo6_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk818_ldo6_voltage_ranges),
		.vsel_reg = RK818_LDO7_ON_VSEL_REG,
		.vsel_mask = RK818_LDO_VSEL_MASK,
		.enable_reg = RK818_LDO_EN_REG,
		.enable_mask = BIT(6),
		.enable_time = 400,
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG8",
		.supply_name = "vcc8",
		.id = RK818_ID_LDO8,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 17,
		.linear_ranges = rk818_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk818_ldo_voltage_ranges),
		.vsel_reg =  RK818_LDO8_ON_VSEL_REG,
		.vsel_mask = RK818_LDO_VSEL_MASK,
		.enable_reg = RK818_LDO_EN_REG,
		.enable_mask = BIT(7),
		.enable_time = 400,
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG9",
		.supply_name = "vcc9",
		.id = RK818_ID_LDO9,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 17,
		.linear_ranges = rk818_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk818_ldo_voltage_ranges),
		.vsel_reg = RK818_BOOST_LDO9_ON_VSEL_REG,
		.vsel_mask = RK818_LDO_VSEL_MASK,
		.enable_reg = RK818_DCDC_EN_REG,
		.enable_mask = BIT(5),
		.enable_time = 400,
		.owner = THIS_MODULE,
	}, {
		.name = "SWITCH_REG",
		.supply_name = "vcc9",
		.id = RK818_ID_SWITCH,
		.ops = &rk818_switch_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = RK818_DCDC_EN_REG,
		.enable_mask = BIT(6),
		.owner = THIS_MODULE,
	},
};

static struct of_regulator_match rk818_reg_matches[] = {
	[RK818_ID_DCDC1]	= { .name = "DCDC_REG1" },
	[RK818_ID_DCDC2]	= { .name = "DCDC_REG2" },
	[RK818_ID_DCDC3]	= { .name = "DCDC_REG3" },
	[RK818_ID_DCDC4]	= { .name = "DCDC_REG4" },
	[RK818_ID_LDO1]		= { .name = "LDO_REG1" },
	[RK818_ID_LDO2]		= { .name = "LDO_REG2" },
	[RK818_ID_LDO3]		= { .name = "LDO_REG3" },
	[RK818_ID_LDO4]		= { .name = "LDO_REG4" },
	[RK818_ID_LDO5]		= { .name = "LDO_REG5" },
	[RK818_ID_LDO6]		= { .name = "LDO_REG6" },
	[RK818_ID_LDO7]		= { .name = "LDO_REG7" },
	[RK818_ID_LDO8]		= { .name = "LDO_REG8" },
	[RK818_ID_LDO9]		= { .name = "LDO_REG9" },
	[RK818_ID_SWITCH]	= { .name = "SWITCH_REG" },
};

static int rk818_regulator_dt_parse_pdata(struct device *dev,
					  struct device *client_dev,
					  struct regmap *map)
{
	struct device_node *np;
	int ret;

	np = of_get_child_by_name(client_dev->of_node, "regulators");
	if (!np)
		return -ENXIO;

	ret = of_regulator_match(dev, np, rk818_reg_matches,
				 RK818_NUM_REGULATORS);

	of_node_put(np);
	return ret;
}

static int rk818_regulator_probe(struct platform_device *pdev)
{
	struct rk808 *rk818 = dev_get_drvdata(pdev->dev.parent);
	struct i2c_client *client = rk818->i2c;
	struct regulator_config config = {};
	struct regulator_dev *rk818_rdev;
	int ret, i;

	ret = rk818_regulator_dt_parse_pdata(&pdev->dev, &client->dev,
					     rk818->regmap);
	if (ret < 0)
		return ret;

	/* Instantiate the regulators */
	for (i = 0; i < RK818_NUM_REGULATORS; i++) {
		if (!rk818_reg_matches[i].init_data ||
		    !rk818_reg_matches[i].of_node)
			continue;

		config.dev = &client->dev;
		config.regmap = rk818->regmap;
		config.of_node = rk818_reg_matches[i].of_node;
		config.init_data = rk818_reg_matches[i].init_data;

		rk818_rdev = devm_regulator_register(&pdev->dev,
						     &rk818_reg[i], &config);
		if (IS_ERR(rk818_rdev)) {
			dev_err(&client->dev,
				"failed to register %d regulator\n", i);
			return PTR_ERR(rk818_rdev);
		}
	}

	return 0;
}

static struct platform_driver rk818_regulator_driver = {
	.probe = rk818_regulator_probe,
	.driver = {
		.name = "rk818-regulator",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(rk818_regulator_driver);

MODULE_DESCRIPTION("regulator driver for the rk818 series PMICs");
MODULE_AUTHOR("xsf<xsf@rock-chips.com>");
MODULE_AUTHOR("Zhang Qing<zhangqing@rock-chips.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rk818-regulator");

