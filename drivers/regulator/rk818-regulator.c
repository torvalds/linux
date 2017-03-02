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
#define RK818_BOOST_ON_VSEL_MASK	0xe0

/* Ramp rate definitions for buck1 / buck2 only */
#define RK818_RAMP_RATE_OFFSET		3
#define RK818_RAMP_RATE_MASK		(3 << RK818_RAMP_RATE_OFFSET)
#define RK818_RAMP_RATE_2MV_PER_US	(0 << RK818_RAMP_RATE_OFFSET)
#define RK818_RAMP_RATE_4MV_PER_US	(1 << RK818_RAMP_RATE_OFFSET)
#define RK818_RAMP_RATE_6MV_PER_US	(2 << RK818_RAMP_RATE_OFFSET)
#define RK818_RAMP_RATE_10MV_PER_US	(3 << RK818_RAMP_RATE_OFFSET)

#define RK805_RAMP_RATE_OFFSET		3
#define RK805_RAMP_RATE_MASK		(3 << RK805_RAMP_RATE_OFFSET)
#define RK805_RAMP_RATE_3MV_PER_US	(0 << RK805_RAMP_RATE_OFFSET)
#define RK805_RAMP_RATE_6MV_PER_US	(1 << RK805_RAMP_RATE_OFFSET)
#define RK805_RAMP_RATE_12_5MV_PER_US	(2 << RK805_RAMP_RATE_OFFSET)
#define RK805_RAMP_RATE_25MV_PER_US	(3 << RK805_RAMP_RATE_OFFSET)

/* Offset from XXX_ON_VSEL to XXX_SLP_VSEL */
#define RK818_SLP_REG_OFFSET 1

/* Offset from XXX_EN_REG to SLEEP_SET_OFF_XXX */
#define RK818_SLP_SET_OFF_REG_OFFSET 2

#define RK805_SLP_LDO_EN_OFFSET		-1
#define RK805_SLP_DCDC_EN_OFFSET	2

/* max steps for increase voltage of Buck1/2, equal 100mv*/
#define MAX_STEPS_ONE_TIME 8

static const int rk818_buck_config_regs[] = {
	RK818_BUCK1_CONFIG_REG,
	RK818_BUCK2_CONFIG_REG,
	RK818_BUCK3_CONFIG_REG,
	RK818_BUCK4_CONFIG_REG,
};

/* rk805 */
#define ENABLE_MASK(id)	(BIT(id) | BIT(4 + (id)))
#define DISABLE_VAL(id)	(BIT(4 + (id)))

static const struct regulator_linear_range rk805_buck_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(712500, 0, 59, 12500),	/* 0.7125v - 1.45v */
	REGULATOR_LINEAR_RANGE(1800000, 60, 62, 200000),/* 1.8v - 2.2v */
	REGULATOR_LINEAR_RANGE(2300000, 63, 63, 0),	/* 2.3v - 2.3v */
};

static const struct regulator_linear_range rk805_buck4_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0, 26, 100000),	/* 0.8v - 3.4 */
	REGULATOR_LINEAR_RANGE(3500000, 27, 31, 0),	/* 3.5v */
};

static const struct regulator_linear_range rk805_ldo_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0, 26, 100000),	/* 0.8v - 3.4 */
};

/* rk818 */
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

static const struct regulator_linear_range rk818_boost_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(4700000, 0, 7, 100000),
};

static int rk818_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	struct rk808 *rk818 = rdev->reg_data;
	unsigned int ramp_value = RK818_RAMP_RATE_10MV_PER_US;
	unsigned int reg = rk818_buck_config_regs[rdev->desc->id -
						  RK818_ID_DCDC1];

	switch (rk818->variant) {
	case RK818_ID:
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
			pr_warn("%s ramp_delay: %d not supported, set 10000\n",
				rdev->desc->name, ramp_delay);
		}
		break;
	case RK805_ID:
		switch (ramp_delay) {
		case 3000:
			ramp_value = RK805_RAMP_RATE_3MV_PER_US;
			break;
		case 6000:
			ramp_value = RK805_RAMP_RATE_6MV_PER_US;
			break;
		case 12500:
			ramp_value = RK805_RAMP_RATE_12_5MV_PER_US;
			break;
		case 25000:
			ramp_value = RK805_RAMP_RATE_25MV_PER_US;
			break;
		default:
			pr_warn("%s ramp_delay: %d not supported\n",
				rdev->desc->name, ramp_delay);
		}
		break;
	default:
		dev_err(&rdev->dev, "%s: unsupported RK8XX ID %lu\n",
			__func__, rk818->variant);
		return -EINVAL;
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
	unsigned int reg, enable_val;
	int offset = 0;
	struct rk808 *rk818 = rdev->reg_data;

	switch (rk818->variant) {
	case RK818_ID:
		offset = RK818_SLP_SET_OFF_REG_OFFSET;
		enable_val = 0;
		break;
	case RK805_ID:
		if (rdev->desc->id >= RK805_ID_LDO1)
			offset = RK805_SLP_LDO_EN_OFFSET;
		else
			offset = RK805_SLP_DCDC_EN_OFFSET;
		enable_val = rdev->desc->enable_mask;
		break;
	default:
		dev_err(&rdev->dev, "not define sleep en reg offset!!\n");
		return -EINVAL;
	}

	reg = rdev->desc->enable_reg + offset;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->enable_mask,
				  enable_val);
}

static int rk818_set_suspend_disable(struct regulator_dev *rdev)
{
	int offset = 0;
	unsigned int reg, disable_val;
	struct rk808 *rk818 = rdev->reg_data;

	switch (rk818->variant) {
	case RK818_ID:
		offset = RK818_SLP_SET_OFF_REG_OFFSET;
		disable_val = rdev->desc->enable_mask;
		break;
	case RK805_ID:
		if (rdev->desc->id >= RK805_ID_LDO1)
			offset = RK805_SLP_LDO_EN_OFFSET;
		else
			offset = RK805_SLP_DCDC_EN_OFFSET;
		disable_val = 0;
		break;
	default:
		dev_err(&rdev->dev, "not define sleep en reg offset!!\n");
		return -EINVAL;
	}

	reg = rdev->desc->enable_reg + offset;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->enable_mask,
				  disable_val);
}

static int rk818_set_suspend_mode(struct regulator_dev *rdev, unsigned int mode)
{
	unsigned int reg;

	reg = rdev->desc->vsel_reg + RK818_SLP_REG_OFFSET;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		return regmap_update_bits(rdev->regmap, reg,
					  FPWM_MODE, FPWM_MODE);
	case REGULATOR_MODE_NORMAL:
		return regmap_update_bits(rdev->regmap, reg, FPWM_MODE, 0);
	default:
		pr_err("do not support this mode\n");
		return -EINVAL;
	}

	return 0;
}

static int rk818_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	switch (mode) {
	case REGULATOR_MODE_FAST:
		return regmap_update_bits(rdev->regmap, rdev->desc->vsel_reg,
					  FPWM_MODE, FPWM_MODE);
	case REGULATOR_MODE_NORMAL:
		return regmap_update_bits(rdev->regmap, rdev->desc->vsel_reg,
					  FPWM_MODE, 0);
	default:
		pr_err("do not support this mode\n");
		return -EINVAL;
	}

	return 0;
}

static unsigned int rk818_get_mode(struct regulator_dev *rdev)
{
	unsigned int val;
	int err;

	err = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &val);
	if (err)
		return err;

	if (val & FPWM_MODE)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;
}

static unsigned int rk818_regulator_of_map_mode(unsigned int mode)
{
	if (mode == 1)
		return REGULATOR_MODE_FAST;
	if (mode == 2)
		return REGULATOR_MODE_NORMAL;

	return -EINVAL;
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
	.set_mode		= rk818_set_mode,
	.get_mode		= rk818_get_mode,
	.set_ramp_delay		= rk818_set_ramp_delay,
	.set_suspend_mode	= rk818_set_suspend_mode,
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
	.set_mode		= rk818_set_mode,
	.get_mode		= rk818_get_mode,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_suspend_mode	= rk818_set_suspend_mode,
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
	.set_mode		= rk818_set_mode,
	.get_mode		= rk818_get_mode,
	.set_suspend_mode	= rk818_set_suspend_mode,
};

static const struct regulator_desc rk818_desc[] = {
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
		.name = "DCDC_BOOST",
		.supply_name = "boost",
		.id = RK818_ID_BOOST,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 8,
		.linear_ranges = rk818_boost_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk818_boost_voltage_ranges),
		.vsel_reg = RK818_BOOST_LDO9_ON_VSEL_REG,
		.vsel_mask = RK818_BOOST_ON_VSEL_MASK,
		.enable_reg = RK818_DCDC_EN_REG,
		.enable_mask = BIT(4),
		.enable_time = 0,
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
	[RK818_ID_BOOST]	= { .name = "DCDC_BOOST" },
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

static const struct regulator_desc rk805_desc[] = {
	{
		.name = "DCDC_REG1",
		.supply_name = "vcc1",
		.id = RK805_ID_DCDC1,
		.ops = &rk818_buck1_2_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 64,
		.linear_ranges = rk805_buck_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_buck_voltage_ranges),
		.vsel_reg = RK805_BUCK1_ON_VSEL_REG,
		.vsel_mask = RK818_BUCK_VSEL_MASK,
		.enable_reg = RK805_DCDC_EN_REG,
		.enable_mask = ENABLE_MASK(0),
		.disable_val = DISABLE_VAL(0),
		.of_map_mode = rk818_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG2",
		.supply_name = "vcc2",
		.id = RK805_ID_DCDC2,
		.ops = &rk818_buck1_2_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 64,
		.linear_ranges = rk805_buck_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_buck_voltage_ranges),
		.vsel_reg = RK805_BUCK2_ON_VSEL_REG,
		.vsel_mask = RK818_BUCK_VSEL_MASK,
		.enable_reg = RK805_DCDC_EN_REG,
		.enable_mask = ENABLE_MASK(1),
		.disable_val = DISABLE_VAL(1),
		.of_map_mode = rk818_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG3",
		.supply_name = "vcc3",
		.id = RK805_ID_DCDC3,
		.ops = &rk818_switch_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 1,
		.enable_reg = RK805_DCDC_EN_REG,
		.enable_mask = ENABLE_MASK(2),
		.disable_val = DISABLE_VAL(2),
		.of_map_mode = rk818_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG4",
		.supply_name = "vcc4",
		.id = RK805_ID_DCDC4,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 32,
		.linear_ranges = rk805_buck4_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_buck4_voltage_ranges),
		.vsel_reg = RK805_BUCK4_ON_VSEL_REG,
		.vsel_mask = RK818_BUCK4_VSEL_MASK,
		.enable_reg = RK805_DCDC_EN_REG,
		.enable_mask = ENABLE_MASK(3),
		.disable_val = DISABLE_VAL(3),
		.of_map_mode = rk818_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG1",
		.supply_name = "vcc5",
		.id = RK805_ID_LDO1,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 27,
		.linear_ranges = rk805_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_ldo_voltage_ranges),
		.vsel_reg = RK805_LDO1_ON_VSEL_REG,
		.vsel_mask = RK818_LDO_VSEL_MASK,
		.enable_reg = RK805_LDO_EN_REG,
		.enable_mask = ENABLE_MASK(0),
		.disable_val = DISABLE_VAL(0),
		.enable_time = 400,
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG2",
		.supply_name = "vcc5",
		.id = RK805_ID_LDO2,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 27,
		.linear_ranges = rk805_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_ldo_voltage_ranges),
		.vsel_reg = RK805_LDO2_ON_VSEL_REG,
		.vsel_mask = RK818_LDO_VSEL_MASK,
		.enable_reg = RK805_LDO_EN_REG,
		.enable_mask = ENABLE_MASK(1),
		.disable_val = DISABLE_VAL(1),
		.enable_time = 400,
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG3",
		.supply_name = "vcc6",
		.id = RK805_ID_LDO3,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 27,
		.linear_ranges = rk805_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_ldo_voltage_ranges),
		.vsel_reg = RK805_LDO3_ON_VSEL_REG,
		.vsel_mask = RK818_LDO_VSEL_MASK,
		.enable_reg = RK805_LDO_EN_REG,
		.enable_mask = ENABLE_MASK(2),
		.disable_val = DISABLE_VAL(2),
		.enable_time = 400,
		.owner = THIS_MODULE,
	},
};

static struct of_regulator_match rk805_reg_matches[] = {
	[RK805_ID_DCDC1] = {
		.name = "RK805_DCDC1",
		.desc = &rk805_desc[RK805_ID_DCDC1] /* for of_map_node */
	},
	[RK805_ID_DCDC2] = {
		.name = "RK805_DCDC2",
		.desc = &rk805_desc[RK805_ID_DCDC2]
	},
	[RK805_ID_DCDC3] = {
		.name = "RK805_DCDC3",
		.desc = &rk805_desc[RK805_ID_DCDC3]
	},
	[RK805_ID_DCDC4] = {
		.name = "RK805_DCDC4",
		.desc = &rk805_desc[RK805_ID_DCDC4]
	},
	[RK805_ID_LDO1]	= { .name = "RK805_LDO1", },
	[RK805_ID_LDO2]	= { .name = "RK805_LDO2", },
	[RK805_ID_LDO3]	= { .name = "RK805_LDO3", },
};

static int rk818_regulator_dt_parse_pdata(struct device *dev,
					  struct device *client_dev,
					  struct regmap *map,
					  struct of_regulator_match *reg_matches,
					  int regulator_nr)
{
	struct device_node *np;
	int ret;

	np = of_get_child_by_name(client_dev->of_node, "regulators");
	if (!np)
		return -ENXIO;

	ret = of_regulator_match(dev, np, reg_matches, regulator_nr);

	of_node_put(np);
	return ret;
}

static int rk818_regulator_probe(struct platform_device *pdev)
{
	struct rk808 *rk818 = dev_get_drvdata(pdev->dev.parent);
	struct i2c_client *client = rk818->i2c;
	struct regulator_config config = {};
	struct regulator_dev *rk818_rdev;
	int ret, i, reg_nr;
	const struct regulator_desc *reg_desc;
	struct of_regulator_match *reg_matches;

	switch (rk818->variant) {
	case RK818_ID:
		reg_desc = rk818_desc;
		reg_matches = rk818_reg_matches;
		reg_nr = ARRAY_SIZE(rk818_reg_matches);
		break;
	case RK805_ID:
		reg_desc = rk805_desc;
		reg_matches = rk805_reg_matches;
		reg_nr = RK805_NUM_REGULATORS;
		break;
	default:
		dev_err(&client->dev, "unsupported RK8XX ID %lu\n",
			rk818->variant);
		return -EINVAL;
	}

	ret = rk818_regulator_dt_parse_pdata(&pdev->dev, &client->dev,
					     rk818->regmap,
					     reg_matches, reg_nr);
	if (ret < 0)
		return ret;

	/* Instantiate the regulators */
	for (i = 0; i < reg_nr; i++) {
		if (!reg_matches[i].init_data ||
		    !reg_matches[i].of_node)
			continue;

		config.driver_data = rk818;
		config.dev = &client->dev;
		config.regmap = rk818->regmap;
		config.of_node = reg_matches[i].of_node;
		config.init_data = reg_matches[i].init_data;
		rk818_rdev = devm_regulator_register(&pdev->dev,
						     &reg_desc[i],
						     &config);
		if (IS_ERR(rk818_rdev)) {
			dev_err(&client->dev,
				"failed to register %d regulator\n", i);
			return PTR_ERR(rk818_rdev);
		}
	}

	dev_info(&client->dev, "register rk%lx regulators\n", rk818->variant);

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
MODULE_AUTHOR("chen Jianhong<chenjh@rock-chips.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rk818-regulator");

