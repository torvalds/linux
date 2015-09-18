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

#define RK808_DVS2_POL		BIT(2)
#define RK808_DVS1_POL		BIT(1)

/* Offset from XXX_ON_VSEL to XXX_SLP_VSEL */
#define RK808_SLP_REG_OFFSET 1

/* Offset from XXX_ON_VSEL to XXX_DVS_VSEL */
#define RK808_DVS_REG_OFFSET 2

/* Offset from XXX_EN_REG to SLEEP_SET_OFF_XXX */
#define RK808_SLP_SET_OFF_REG_OFFSET 2

/* max steps for increase voltage of Buck1/2, equal 100mv*/
#define MAX_STEPS_ONE_TIME 8

struct rk808_regulator_data {
	struct gpio_desc *dvs_gpio[2];
};

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

static int rk808_buck1_2_get_voltage_sel_regmap(struct regulator_dev *rdev)
{
	struct rk808_regulator_data *pdata = rdev_get_drvdata(rdev);
	int id = rdev->desc->id - RK808_ID_DCDC1;
	struct gpio_desc *gpio = pdata->dvs_gpio[id];
	unsigned int val;
	int ret;

	if (!gpio || gpiod_get_value(gpio) == 0)
		return regulator_get_voltage_sel_regmap(rdev);

	ret = regmap_read(rdev->regmap,
			  rdev->desc->vsel_reg + RK808_DVS_REG_OFFSET,
			  &val);
	if (ret != 0)
		return ret;

	val &= rdev->desc->vsel_mask;
	val >>= ffs(rdev->desc->vsel_mask) - 1;

	return val;
}

static int rk808_buck1_2_i2c_set_voltage_sel(struct regulator_dev *rdev,
					     unsigned sel)
{
	int ret, delta_sel;
	unsigned int old_sel, tmp, val, mask = rdev->desc->vsel_mask;

	ret = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &val);
	if (ret != 0)
		return ret;

	tmp = val & ~mask;
	old_sel = val & mask;
	old_sel >>= ffs(mask) - 1;
	delta_sel = sel - old_sel;

	/*
	 * If directly modify the register to change the voltage, we will face
	 * the risk of overshoot. Put it into a multi-step, can effectively
	 * avoid this problem, a step is 100mv here.
	 */
	while (delta_sel > MAX_STEPS_ONE_TIME) {
		old_sel += MAX_STEPS_ONE_TIME;
		val = old_sel << (ffs(mask) - 1);
		val |= tmp;

		/*
		 * i2c is 400kHz (2.5us per bit) and we must transmit _at least_
		 * 3 bytes (24 bits) plus start and stop so 26 bits.  So we've
		 * got more than 65 us between each voltage change and thus
		 * won't ramp faster than ~1500 uV / us.
		 */
		ret = regmap_write(rdev->regmap, rdev->desc->vsel_reg, val);
		delta_sel = sel - old_sel;
	}

	sel <<= ffs(mask) - 1;
	val = tmp | sel;
	ret = regmap_write(rdev->regmap, rdev->desc->vsel_reg, val);

	/*
	 * When we change the voltage register directly, the ramp rate is about
	 * 100000uv/us, wait 1us to make sure the target voltage to be stable,
	 * so we needn't wait extra time after that.
	 */
	udelay(1);

	return ret;
}

static int rk808_buck1_2_set_voltage_sel(struct regulator_dev *rdev,
					 unsigned sel)
{
	struct rk808_regulator_data *pdata = rdev_get_drvdata(rdev);
	int id = rdev->desc->id - RK808_ID_DCDC1;
	struct gpio_desc *gpio = pdata->dvs_gpio[id];
	unsigned int reg = rdev->desc->vsel_reg;
	unsigned old_sel;
	int ret, gpio_level;

	if (!gpio)
		return rk808_buck1_2_i2c_set_voltage_sel(rdev, sel);

	gpio_level = gpiod_get_value(gpio);
	if (gpio_level == 0) {
		reg += RK808_DVS_REG_OFFSET;
		ret = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &old_sel);
	} else {
		ret = regmap_read(rdev->regmap,
				  reg + RK808_DVS_REG_OFFSET,
				  &old_sel);
	}

	if (ret != 0)
		return ret;

	sel <<= ffs(rdev->desc->vsel_mask) - 1;
	sel |= old_sel & ~rdev->desc->vsel_mask;

	ret = regmap_write(rdev->regmap, reg, sel);
	if (ret)
		return ret;

	gpiod_set_value(gpio, !gpio_level);

	return ret;
}

static int rk808_buck1_2_set_voltage_time_sel(struct regulator_dev *rdev,
				       unsigned int old_selector,
				       unsigned int new_selector)
{
	struct rk808_regulator_data *pdata = rdev_get_drvdata(rdev);
	int id = rdev->desc->id - RK808_ID_DCDC1;
	struct gpio_desc *gpio = pdata->dvs_gpio[id];

	/* if there is no dvs1/2 pin, we don't need wait extra time here. */
	if (!gpio)
		return 0;

	return regulator_set_voltage_time_sel(rdev, old_selector, new_selector);
}

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

static int rk808_set_suspend_voltage(struct regulator_dev *rdev, int uv)
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

static int rk808_set_suspend_enable(struct regulator_dev *rdev)
{
	unsigned int reg;

	reg = rdev->desc->enable_reg + RK808_SLP_SET_OFF_REG_OFFSET;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->enable_mask,
				  0);
}

static int rk808_set_suspend_disable(struct regulator_dev *rdev)
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
	.get_voltage_sel	= rk808_buck1_2_get_voltage_sel_regmap,
	.set_voltage_sel	= rk808_buck1_2_set_voltage_sel,
	.set_voltage_time_sel	= rk808_buck1_2_set_voltage_time_sel,
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
		.enable_time = 400,
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
		.enable_time = 400,
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
		.enable_time = 400,
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
		.enable_time = 400,
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
		.enable_time = 400,
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
		.enable_time = 400,
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
		.enable_time = 400,
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
		.enable_time = 400,
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

static int rk808_regulator_dt_parse_pdata(struct device *dev,
				   struct device *client_dev,
				   struct regmap *map,
				   struct rk808_regulator_data *pdata)
{
	struct device_node *np;
	int tmp, ret, i;

	np = of_get_child_by_name(client_dev->of_node, "regulators");
	if (!np)
		return -ENXIO;

	ret = of_regulator_match(dev, np, rk808_reg_matches,
				 RK808_NUM_REGULATORS);
	if (ret < 0)
		goto dt_parse_end;

	for (i = 0; i < ARRAY_SIZE(pdata->dvs_gpio); i++) {
		pdata->dvs_gpio[i] =
			devm_gpiod_get_index_optional(client_dev, "dvs", i,
						      GPIOD_OUT_LOW);
		if (IS_ERR(pdata->dvs_gpio[i])) {
			ret = PTR_ERR(pdata->dvs_gpio[i]);
			dev_err(dev, "failed to get dvs%d gpio (%d)\n", i, ret);
			goto dt_parse_end;
		}

		if (!pdata->dvs_gpio[i]) {
			dev_warn(dev, "there is no dvs%d gpio\n", i);
			continue;
		}

		tmp = i ? RK808_DVS2_POL : RK808_DVS1_POL;
		ret = regmap_update_bits(map, RK808_IO_POL_REG, tmp,
				gpiod_is_active_low(pdata->dvs_gpio[i]) ?
				0 : tmp);
	}

dt_parse_end:
	of_node_put(np);
	return ret;
}

static int rk808_regulator_probe(struct platform_device *pdev)
{
	struct rk808 *rk808 = dev_get_drvdata(pdev->dev.parent);
	struct i2c_client *client = rk808->i2c;
	struct regulator_config config = {};
	struct regulator_dev *rk808_rdev;
	struct rk808_regulator_data *pdata;
	int ret, i;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	ret = rk808_regulator_dt_parse_pdata(&pdev->dev, &client->dev,
					     rk808->regmap, pdata);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, pdata);

	/* Instantiate the regulators */
	for (i = 0; i < RK808_NUM_REGULATORS; i++) {
		if (!rk808_reg_matches[i].init_data ||
		    !rk808_reg_matches[i].of_node)
			continue;

		config.dev = &client->dev;
		config.driver_data = pdata;
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
