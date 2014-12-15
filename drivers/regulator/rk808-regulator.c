/*
 * Regulator driver for Rockchip RK808
 *
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Chris Zhong <zyw@rock-chips.com>
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

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/mfd/rk808.h>
#include <linux/of_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

/* Field Definitions */
#define RK808_BUCK_VSEL_MASK	0x3f
#define RK808_BUCK4_VSEL_MASK	0xf
#define RK808_LDO_VSEL_MASK	0x1f

/* Ramp rate definitions for buck1 / buck2 only */
#define RK808_RAMP_RATE_OFFSET		3
#define RK808_RAMP_RATE_MASK		(3 << RK808_RAMP_RATE_OFFSET)
#define RK808_RAMP_RATE_2MV_PER_US	(0 << RK808_RAMP_RATE_OFFSET)
#define RK808_RAMP_RATE_4MV_PER_US	(1 << RK808_RAMP_RATE_OFFSET)
#define RK808_RAMP_RATE_6MV_PER_US	(2 << RK808_RAMP_RATE_OFFSET)
#define RK808_RAMP_RATE_10MV_PER_US	(3 << RK808_RAMP_RATE_OFFSET)

/* Offset from XXX_ON_VSEL to XXX_SLP_VSEL */
#define RK808_SLP_REG_OFFSET 1

/* Offset from XXX_EN_REG to SLEEP_SET_OFF_XXX */
#define RK808_SLP_SET_OFF_REG_OFFSET 2

static const int rk808_buck_config_regs[] = {
	RK808_BUCK1_CONFIG_REG,
	RK808_BUCK2_CONFIG_REG,
	RK808_BUCK3_CONFIG_REG,
	RK808_BUCK4_CONFIG_REG,
};

static const struct regulator_linear_range rk808_buck_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(712500, 0, 63, 12500),
};

static const struct regulator_linear_range rk808_buck4_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0, 15, 100000),
};

static const struct regulator_linear_range rk808_ldo_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0, 16, 100000),
};

static const struct regulator_linear_range rk808_ldo3_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0, 13, 100000),
	REGULATOR_LINEAR_RANGE(2500000, 15, 15, 0),
};

static const struct regulator_linear_range rk808_ldo6_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0, 17, 100000),
};

static int rk808_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	unsigned int ramp_value = RK808_RAMP_RATE_10MV_PER_US;
	unsigned int reg = rk808_buck_config_regs[rdev->desc->id -
						  RK808_ID_DCDC1];

	switch (ramp_delay) {
	case 1 ... 2000:
		ramp_value = RK808_RAMP_RATE_2MV_PER_US;
		break;
	case 2001 ... 4000:
		ramp_value = RK808_RAMP_RATE_4MV_PER_US;
		break;
	case 4001 ... 6000:
		ramp_value = RK808_RAMP_RATE_6MV_PER_US;
		break;
	case 6001 ... 10000:
		break;
	default:
		pr_warn("%s ramp_delay: %d not supported, setting 10000\n",
			rdev->desc->name, ramp_delay);
	}

	return regmap_update_bits(rdev->regmap, reg,
				  RK808_RAMP_RATE_MASK, ramp_value);
}

int rk808_set_suspend_voltage(struct regulator_dev *rdev, int uv)
{
	unsigned int reg;
	int sel = regulator_map_voltage_linear_range(rdev, uv, uv);

	if (sel < 0)
		return -EINVAL;

	reg = rdev->desc->vsel_reg + RK808_SLP_REG_OFFSET;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->vsel_mask,
				  sel);
}

int rk808_set_suspend_enable(struct regulator_dev *rdev)
{
	unsigned int reg;

	reg = rdev->desc->enable_reg + RK808_SLP_SET_OFF_REG_OFFSET;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->enable_mask,
				  0);
}

int rk808_set_suspend_disable(struct regulator_dev *rdev)
{
	unsigned int reg;

	reg = rdev->desc->enable_reg + RK808_SLP_SET_OFF_REG_OFFSET;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->enable_mask,
				  rdev->desc->enable_mask);
}

static struct regulator_ops rk808_buck1_2_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_ramp_delay		= rk808_set_ramp_delay,
	.set_suspend_voltage	= rk808_set_suspend_voltage,
	.set_suspend_enable	= rk808_set_suspend_enable,
	.set_suspend_disable	= rk808_set_suspend_disable,
};

static struct regulator_ops rk808_reg_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_suspend_voltage	= rk808_set_suspend_voltage,
	.set_suspend_enable	= rk808_set_suspend_enable,
	.set_suspend_disable	= rk808_set_suspend_disable,
};

static struct regulator_ops rk808_switch_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_suspend_enable	= rk808_set_suspend_enable,
	.set_suspend_disable	= rk808_set_suspend_disable,
};

static const struct regulator_desc rk808_reg[] = {
	{
		.name = "DCDC_REG1",
		.supply_name = "vcc1",
		.id = RK808_ID_DCDC1,
		.ops = &rk808_buck1_2_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 64,
		.linear_ranges = rk808_buck_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk808_buck_voltage_ranges),
		.vsel_reg = RK808_BUCK1_ON_VSEL_REG,
		.vsel_mask = RK808_BUCK_VSEL_MASK,
		.enable_reg = RK808_DCDC_EN_REG,
		.enable_mask = BIT(0),
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG2",
		.supply_name = "vcc2",
		.id = RK808_ID_DCDC2,
		.ops = &rk808_buck1_2_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 64,
		.linear_ranges = rk808_buck_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk808_buck_voltage_ranges),
		.vsel_reg = RK808_BUCK2_ON_VSEL_REG,
		.vsel_mask = RK808_BUCK_VSEL_MASK,
		.enable_reg = RK808_DCDC_EN_REG,
		.enable_mask = BIT(1),
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG3",
		.supply_name = "vcc3",
		.id = RK808_ID_DCDC3,
		.ops = &rk808_switch_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 1,
		.enable_reg = RK808_DCDC_EN_REG,
		.enable_mask = BIT(2),
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG4",
		.supply_name = "vcc4",
		.id = RK808_ID_DCDC4,
		.ops = &rk808_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 16,
		.linear_ranges = rk808_buck4_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk808_buck4_voltage_ranges),
		.vsel_reg = RK808_BUCK4_ON_VSEL_REG,
		.vsel_mask = RK808_BUCK4_VSEL_MASK,
		.enable_reg = RK808_DCDC_EN_REG,
		.enable_mask = BIT(3),
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG1",
		.supply_name = "vcc6",
		.id = RK808_ID_LDO1,
		.ops = &rk808_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 17,
		.linear_ranges = rk808_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk808_ldo_voltage_ranges),
		.vsel_reg = RK808_LDO1_ON_VSEL_REG,
		.vsel_mask = RK808_LDO_VSEL_MASK,
		.enable_reg = RK808_LDO_EN_REG,
		.enable_mask = BIT(0),
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG2",
		.supply_name = "vcc6",
		.id = RK808_ID_LDO2,
		.ops = &rk808_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 17,
		.linear_ranges = rk808_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk808_ldo_voltage_ranges),
		.vsel_reg = RK808_LDO2_ON_VSEL_REG,
		.vsel_mask = RK808_LDO_VSEL_MASK,
		.enable_reg = RK808_LDO_EN_REG,
		.enable_mask = BIT(1),
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG3",
		.supply_name = "vcc7",
		.id = RK808_ID_LDO3,
		.ops = &rk808_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 16,
		.linear_ranges = rk808_ldo3_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk808_ldo3_voltage_ranges),
		.vsel_reg = RK808_LDO3_ON_VSEL_REG,
		.vsel_mask = RK808_BUCK4_VSEL_MASK,
		.enable_reg = RK808_LDO_EN_REG,
		.enable_mask = BIT(2),
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG4",
		.supply_name = "vcc9",
		.id = RK808_ID_LDO4,
		.ops = &rk808_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 17,
		.linear_ranges = rk808_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk808_ldo_voltage_ranges),
		.vsel_reg = RK808_LDO4_ON_VSEL_REG,
		.vsel_mask = RK808_LDO_VSEL_MASK,
		.enable_reg = RK808_LDO_EN_REG,
		.enable_mask = BIT(3),
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG5",
		.supply_name = "vcc9",
		.id = RK808_ID_LDO5,
		.ops = &rk808_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 17,
		.linear_ranges = rk808_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk808_ldo_voltage_ranges),
		.vsel_reg = RK808_LDO5_ON_VSEL_REG,
		.vsel_mask = RK808_LDO_VSEL_MASK,
		.enable_reg = RK808_LDO_EN_REG,
		.enable_mask = BIT(4),
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG6",
		.supply_name = "vcc10",
		.id = RK808_ID_LDO6,
		.ops = &rk808_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 18,
		.linear_ranges = rk808_ldo6_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk808_ldo6_voltage_ranges),
		.vsel_reg = RK808_LDO6_ON_VSEL_REG,
		.vsel_mask = RK808_LDO_VSEL_MASK,
		.enable_reg = RK808_LDO_EN_REG,
		.enable_mask = BIT(5),
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG7",
		.supply_name = "vcc7",
		.id = RK808_ID_LDO7,
		.ops = &rk808_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 18,
		.linear_ranges = rk808_ldo6_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk808_ldo6_voltage_ranges),
		.vsel_reg = RK808_LDO7_ON_VSEL_REG,
		.vsel_mask = RK808_LDO_VSEL_MASK,
		.enable_reg = RK808_LDO_EN_REG,
		.enable_mask = BIT(6),
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG8",
		.supply_name = "vcc11",
		.id = RK808_ID_LDO8,
		.ops = &rk808_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 17,
		.linear_ranges = rk808_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk808_ldo_voltage_ranges),
		.vsel_reg = RK808_LDO8_ON_VSEL_REG,
		.vsel_mask = RK808_LDO_VSEL_MASK,
		.enable_reg = RK808_LDO_EN_REG,
		.enable_mask = BIT(7),
		.owner = THIS_MODULE,
	}, {
		.name = "SWITCH_REG1",
		.supply_name = "vcc8",
		.id = RK808_ID_SWITCH1,
		.ops = &rk808_switch_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = RK808_DCDC_EN_REG,
		.enable_mask = BIT(5),
		.owner = THIS_MODULE,
	}, {
		.name = "SWITCH_REG2",
		.supply_name = "vcc12",
		.id = RK808_ID_SWITCH2,
		.ops = &rk808_switch_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = RK808_DCDC_EN_REG,
		.enable_mask = BIT(6),
		.owner = THIS_MODULE,
	},
};

static struct of_regulator_match rk808_reg_matches[] = {
	[RK808_ID_DCDC1]	= { .name = "DCDC_REG1" },
	[RK808_ID_DCDC2]	= { .name = "DCDC_REG2" },
	[RK808_ID_DCDC3]	= { .name = "DCDC_REG3" },
	[RK808_ID_DCDC4]	= { .name = "DCDC_REG4" },
	[RK808_ID_LDO1]		= { .name = "LDO_REG1" },
	[RK808_ID_LDO2]		= { .name = "LDO_REG2" },
	[RK808_ID_LDO3]		= { .name = "LDO_REG3" },
	[RK808_ID_LDO4]		= { .name = "LDO_REG4" },
	[RK808_ID_LDO5]		= { .name = "LDO_REG5" },
	[RK808_ID_LDO6]		= { .name = "LDO_REG6" },
	[RK808_ID_LDO7]		= { .name = "LDO_REG7" },
	[RK808_ID_LDO8]		= { .name = "LDO_REG8" },
	[RK808_ID_SWITCH1]	= { .name = "SWITCH_REG1" },
	[RK808_ID_SWITCH2]	= { .name = "SWITCH_REG2" },
};

static int rk808_regulator_probe(struct platform_device *pdev)
{
	struct rk808 *rk808 = dev_get_drvdata(pdev->dev.parent);
	struct i2c_client *client = rk808->i2c;
	struct device_node *reg_np;
	struct regulator_config config = {};
	struct regulator_dev *rk808_rdev;
	int ret, i;

	reg_np = of_get_child_by_name(client->dev.of_node, "regulators");
	if (!reg_np)
		return -ENXIO;

	ret = of_regulator_match(&pdev->dev, reg_np, rk808_reg_matches,
				 RK808_NUM_REGULATORS);
	of_node_put(reg_np);
	if (ret < 0)
		return ret;

	/* Instantiate the regulators */
	for (i = 0; i < RK808_NUM_REGULATORS; i++) {
		if (!rk808_reg_matches[i].init_data ||
		    !rk808_reg_matches[i].of_node)
			continue;

		config.dev = &client->dev;
		config.driver_data = rk808;
		config.regmap = rk808->regmap;
		config.of_node = rk808_reg_matches[i].of_node;
		config.init_data = rk808_reg_matches[i].init_data;

		rk808_rdev = devm_regulator_register(&pdev->dev,
						     &rk808_reg[i], &config);
		if (IS_ERR(rk808_rdev)) {
			dev_err(&client->dev,
				"failed to register %d regulator\n", i);
			return PTR_ERR(rk808_rdev);
		}
	}

	return 0;
}

static struct platform_driver rk808_regulator_driver = {
	.probe = rk808_regulator_probe,
	.driver = {
		.name = "rk808-regulator",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(rk808_regulator_driver);

MODULE_DESCRIPTION("regulator driver for the rk808 series PMICs");
MODULE_AUTHOR("Chris Zhong<zyw@rock-chips.com>");
MODULE_AUTHOR("Zhang Qing<zhangqing@rock-chips.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rk808-regulator");
