// SPDX-License-Identifier: GPL-2.0-only
/*
 * Device driver for RT5739 regulator
 *
 * Copyright (C) 2023 Richtek Technology Corp.
 *
 * Author: ChiYuan Huang <cy_huang@richtek.com>
 */

#include <linux/bits.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define RT5739_AUTO_MODE	0
#define RT5739_FPWM_MODE	1

#define RT5739_REG_NSEL0	0x00
#define RT5739_REG_NSEL1	0x01
#define RT5739_REG_CNTL1	0x02
#define RT5739_REG_ID1		0x03
#define RT5739_REG_CNTL2	0x06
#define RT5739_REG_CNTL4	0x08

#define RT5739_VSEL_MASK	GENMASK(7, 0)
#define RT5739_MODEVSEL1_MASK	BIT(1)
#define RT5739_MODEVSEL0_MASK	BIT(0)
#define RT5739_VID_MASK		GENMASK(7, 5)
#define RT5739_ACTD_MASK	BIT(7)
#define RT5739_ENVSEL1_MASK	BIT(1)
#define RT5739_ENVSEL0_MASK	BIT(0)

#define RT5739_VOLT_MINUV	300000
#define RT5739_VOLT_MAXUV	1300000
#define RT5739_VOLT_STPUV	5000
#define RT5739_N_VOLTS		201
#define RT5739_I2CRDY_TIMEUS	1000

static int rt5739_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	const struct regulator_desc *desc = rdev->desc;
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int mask, val;

	if (desc->vsel_reg == RT5739_REG_NSEL0)
		mask = RT5739_MODEVSEL0_MASK;
	else
		mask = RT5739_MODEVSEL1_MASK;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = mask;
		break;
	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(regmap, RT5739_REG_CNTL1, mask, val);
}

static unsigned int rt5739_get_mode(struct regulator_dev *rdev)
{
	const struct regulator_desc *desc = rdev->desc;
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int mask, val;
	int ret;

	if (desc->vsel_reg == RT5739_REG_NSEL0)
		mask = RT5739_MODEVSEL0_MASK;
	else
		mask = RT5739_MODEVSEL1_MASK;

	ret = regmap_read(regmap, RT5739_REG_CNTL1, &val);
	if (ret)
		return REGULATOR_MODE_INVALID;

	if (val & mask)
		return REGULATOR_MODE_FAST;

	return REGULATOR_MODE_NORMAL;
}

static int rt5739_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	const struct regulator_desc *desc = rdev->desc;
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int reg, vsel;

	if (uV < RT5739_VOLT_MINUV || uV > RT5739_VOLT_MAXUV)
		return -EINVAL;

	if (desc->vsel_reg == RT5739_REG_NSEL0)
		reg = RT5739_REG_NSEL1;
	else
		reg = RT5739_REG_NSEL0;

	vsel = (uV - RT5739_VOLT_MINUV) / RT5739_VOLT_STPUV;
	return regmap_write(regmap, reg, vsel);
}

static int rt5739_set_suspend_enable(struct regulator_dev *rdev)
{
	const struct regulator_desc *desc = rdev->desc;
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int mask;

	if (desc->vsel_reg == RT5739_REG_NSEL0)
		mask = RT5739_ENVSEL1_MASK;
	else
		mask = RT5739_ENVSEL0_MASK;

	return regmap_update_bits(regmap, desc->enable_reg, mask, mask);
}

static int rt5739_set_suspend_disable(struct regulator_dev *rdev)
{
	const struct regulator_desc *desc = rdev->desc;
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int mask;

	if (desc->vsel_reg == RT5739_REG_NSEL0)
		mask = RT5739_ENVSEL1_MASK;
	else
		mask = RT5739_ENVSEL0_MASK;

	return regmap_update_bits(regmap, desc->enable_reg, mask, 0);
}

static int rt5739_set_suspend_mode(struct regulator_dev *rdev,
				   unsigned int mode)
{
	const struct regulator_desc *desc = rdev->desc;
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int mask, val;

	if (desc->vsel_reg == RT5739_REG_NSEL0)
		mask = RT5739_MODEVSEL1_MASK;
	else
		mask = RT5739_MODEVSEL0_MASK;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = mask;
		break;
	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(regmap, RT5739_REG_CNTL1, mask, val);
}

static const struct regulator_ops rt5739_regulator_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.enable	= regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.set_mode = rt5739_set_mode,
	.get_mode = rt5739_get_mode,
	.set_suspend_voltage = rt5739_set_suspend_voltage,
	.set_suspend_enable = rt5739_set_suspend_enable,
	.set_suspend_disable = rt5739_set_suspend_disable,
	.set_suspend_mode = rt5739_set_suspend_mode,
};

static unsigned int rt5739_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case RT5739_AUTO_MODE:
		return REGULATOR_MODE_NORMAL;
	case RT5739_FPWM_MODE:
		return REGULATOR_MODE_FAST;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static void rt5739_init_regulator_desc(struct regulator_desc *desc,
				       bool vsel_active_high)
{
	/* Fixed */
	desc->name = "rt5739-regulator";
	desc->owner = THIS_MODULE;
	desc->ops = &rt5739_regulator_ops;
	desc->n_voltages = RT5739_N_VOLTS;
	desc->min_uV = RT5739_VOLT_MINUV;
	desc->uV_step = RT5739_VOLT_STPUV;
	desc->vsel_mask = RT5739_VSEL_MASK;
	desc->enable_reg = RT5739_REG_CNTL2;
	desc->active_discharge_reg = RT5739_REG_CNTL1;
	desc->active_discharge_mask = RT5739_ACTD_MASK;
	desc->active_discharge_on = RT5739_ACTD_MASK;
	desc->of_map_mode = rt5739_of_map_mode;

	/* Assigned by vsel level */
	if (vsel_active_high) {
		desc->vsel_reg = RT5739_REG_NSEL1;
		desc->enable_mask = RT5739_ENVSEL1_MASK;
	} else {
		desc->vsel_reg = RT5739_REG_NSEL0;
		desc->enable_mask = RT5739_ENVSEL0_MASK;
	}
}

static const struct regmap_config rt5739_regmap_config = {
	.name = "rt5739",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RT5739_REG_CNTL4,
};

static int rt5739_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct regulator_desc *desc;
	struct regmap *regmap;
	struct gpio_desc *enable_gpio;
	struct regulator_config cfg = {};
	struct regulator_dev *rdev;
	bool vsel_acth;
	unsigned int vid;
	int ret;

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(enable_gpio))
		return dev_err_probe(dev, PTR_ERR(enable_gpio), "Failed to get 'enable' gpio\n");
	else if (enable_gpio)
		usleep_range(RT5739_I2CRDY_TIMEUS, RT5739_I2CRDY_TIMEUS + 1000);

	regmap = devm_regmap_init_i2c(i2c, &rt5739_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Failed to init regmap\n");

	ret = regmap_read(regmap, RT5739_REG_ID1, &vid);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read VID\n");

	/* RT5739: (VID & MASK) must be 0 */
	if (vid & RT5739_VID_MASK)
		return dev_err_probe(dev, -ENODEV, "Incorrect VID (0x%02x)\n", vid);

	vsel_acth = device_property_read_bool(dev, "richtek,vsel-active-high");

	rt5739_init_regulator_desc(desc, vsel_acth);

	cfg.dev = dev;
	cfg.of_node = dev_of_node(dev);
	cfg.init_data = of_get_regulator_init_data(dev, dev_of_node(dev), desc);
	rdev = devm_regulator_register(dev, desc, &cfg);
	if (IS_ERR(rdev))
		return dev_err_probe(dev, PTR_ERR(rdev), "Failed to register regulator\n");

	return 0;
}

static const struct of_device_id rt5739_device_table[] = {
	{ .compatible = "richtek,rt5739" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rt5739_device_table);

static struct i2c_driver rt5739_driver = {
	.driver = {
		.name = "rt5739",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = rt5739_device_table,
	},
	.probe_new = rt5739_probe,
};
module_i2c_driver(rt5739_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("Richtek RT5739 regulator driver");
MODULE_LICENSE("GPL");
