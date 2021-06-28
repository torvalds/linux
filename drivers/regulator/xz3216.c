/*
 * Regulator driver for xz3216 DCDC chip for rk32xx
 *
 * Copyright (C) 2010, 2011 ROCKCHIP, Inc.

 * Based on xz3216.c that is work by zhangqing<zhangqing@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/bug.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/regulator/driver.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regmap.h>

#if 0
#define DBG(x...)	pr_info(x)
#else
#define DBG(x...)
#endif

#define DBG_ERR(x...)	pr_err(x)

#define XZ3216_NUM_REGULATORS 1

#define XZ3216_BUCK1_SET_VOL_BASE 0x00
#define XZ3216_BUCK1_SLP_VOL_BASE 0x01
#define XZ3216_CONTR_REG1 0x02
#define XZ3216_ID1_REG 0x03
#define BUCK_VOL_MASK 0x3f
#define VOL_MIN_IDX 0x00
#define VOL_MAX_IDX 0x3

/* VSEL bit definitions */
#define VSEL_BUCK_EN	BIT(7)
#define VSEL_MODE	BIT(6)
#define VSEL_NSEL_MASK	0x3F

/* Control bit definitions */
#define CTL_OUTPUT_DISCHG	BIT(7)
#define CTL_SLEW_MASK		(0x7 << 4)
#define CTL_SLEW_SHIFT		4
#define CTL_RESET		BIT(2)

struct xz3216 {
	struct device *dev;
	struct i2c_client *i2c;
	int num_regulators;
	struct regulator_dev *rdev;
	struct regulator_init_data *regulator;
	struct regmap *regmap;
	/* Voltage setting register */
	unsigned int vol_reg;
	unsigned int sleep_reg;
	/* Voltage range and step(linear) */
	unsigned int vsel_min;
	unsigned int vsel_step;
	unsigned int sleep_vol_cache;
};

struct xz3216_regulator {
	struct device		*dev;
	struct regulator_desc	*desc;
	struct regulator_dev	*rdev;
};

struct xz3216_board {
	struct regulator_init_data *xz3216_init_data;
	struct device_node *of_node;
};

static unsigned int xz3216_dcdc_get_mode(struct regulator_dev *dev)
{
	struct xz3216 *xz3216 = rdev_get_drvdata(dev);
	unsigned int val;
	int ret = 0;

	ret = regmap_read(xz3216->regmap, xz3216->vol_reg, &val);
	if (ret < 0)
		return ret;
	if (val & VSEL_MODE)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;
}

static int xz3216_dcdc_set_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct xz3216 *xz3216 = rdev_get_drvdata(dev);

	switch (mode) {
	case REGULATOR_MODE_FAST:
		return regmap_update_bits(xz3216->regmap, xz3216->vol_reg,
					  VSEL_MODE, VSEL_MODE);
	case REGULATOR_MODE_NORMAL:
		return regmap_update_bits(xz3216->regmap, xz3216->vol_reg,
					  VSEL_MODE, 0);
	default:
		DBG("error:dcdc_xz3216 only auto and pwm mode\n");
		return -EINVAL;
	}
}

static int xz3216_dcdc_suspend_enable(struct regulator_dev *dev)
{
	struct xz3216 *xz3216 = rdev_get_drvdata(dev);
	return regmap_update_bits(xz3216->regmap, XZ3216_BUCK1_SLP_VOL_BASE,
				  VSEL_BUCK_EN, VSEL_BUCK_EN);
}

static int xz3216_dcdc_suspend_disable(struct regulator_dev *dev)
{
	struct xz3216 *xz3216 = rdev_get_drvdata(dev);
	return regmap_update_bits(xz3216->regmap, XZ3216_BUCK1_SLP_VOL_BASE,
				  VSEL_BUCK_EN, 0);

}

static int xz3216_dcdc_set_sleep_voltage(struct regulator_dev *dev,
					 int uV)
{
	struct xz3216 *xz3216 = rdev_get_drvdata(dev);
	int ret;

	if (xz3216->sleep_vol_cache == uV)
		return 0;
	ret = regulator_map_voltage_linear(dev, uV, uV);
	if (ret < 0)
		return ret;
	ret = regmap_update_bits(xz3216->regmap, XZ3216_BUCK1_SLP_VOL_BASE,
					VSEL_NSEL_MASK, ret);
	if (ret < 0)
		return ret;
	xz3216->sleep_vol_cache = uV;
	return 0;

}

static int xz3216_dcdc_set_suspend_mode(struct regulator_dev *dev,
					 unsigned int mode)
{
	struct xz3216 *xz3216 = rdev_get_drvdata(dev);

	switch (mode) {
	case REGULATOR_MODE_FAST:
		return regmap_update_bits(xz3216->regmap, xz3216->vol_reg,
					  VSEL_MODE, VSEL_MODE);
	case REGULATOR_MODE_NORMAL:
		return regmap_update_bits(xz3216->regmap, xz3216->vol_reg,
					  VSEL_MODE, 0);
	default:
		DBG_ERR("error:dcdc_xz3216 only auto and pwm mode\n");
		return -EINVAL;
	}
}

static const int slew_rates[] = {
	64000,
	32000,
	16000,
	 8000,
	 4000,
	 2000,
	 1000,
	  500,
};

static int xz3216_set_ramp(struct regulator_dev *rdev, int ramp)
{
	struct xz3216 *xz3216 = rdev_get_drvdata(rdev);
	int regval = -1, i;

	for (i = 0; i < ARRAY_SIZE(slew_rates); i++) {
		if (ramp <= slew_rates[i])
			regval = i;
		else
			break;
	}
	if (regval < 0) {
		dev_err(xz3216->dev, "unsupported ramp value %d\n", ramp);
		return -EINVAL;
	}

	return regmap_update_bits(xz3216->regmap, XZ3216_CONTR_REG1,
				  CTL_SLEW_MASK, regval << CTL_SLEW_SHIFT);
}

static struct regulator_ops xz3216_dcdc_ops = {
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.get_mode = xz3216_dcdc_get_mode,
	.set_mode = xz3216_dcdc_set_mode,
	.set_suspend_voltage = xz3216_dcdc_set_sleep_voltage,
	.set_suspend_enable = xz3216_dcdc_suspend_enable,
	.set_suspend_disable = xz3216_dcdc_suspend_disable,
	.set_suspend_mode = xz3216_dcdc_set_suspend_mode,
	.set_ramp_delay = xz3216_set_ramp,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
};

static struct regulator_desc regulators[] = {
	{
		.name = "XZ_DCDC1",
		.supply_name = "vin",
		.id = 0,
		.ops = &xz3216_dcdc_ops,
		.n_voltages = 64,
		.type = REGULATOR_VOLTAGE,
		.enable_time = 400,
		.enable_reg = XZ3216_BUCK1_SET_VOL_BASE,
		.enable_mask = VSEL_BUCK_EN,
		.min_uV = 600000,
		.uV_step = 12500,
		.vsel_reg = XZ3216_BUCK1_SET_VOL_BASE,
		.vsel_mask = VSEL_NSEL_MASK,
		.owner = THIS_MODULE,
	},
};

static const struct regmap_config xz3216_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

#ifdef CONFIG_OF
static struct of_device_id xz3216_of_match[] = {
	{ .compatible = "xz3216"},
	{ },
};
MODULE_DEVICE_TABLE(of, xz3216_of_match);
#endif

#ifdef CONFIG_OF
static struct of_regulator_match xz3216_reg_matches[] = {
	{ .name = "xz_dcdc1", .driver_data = (void *)0},
};

static struct xz3216_board *xz3216_parse_dt(struct xz3216 *xz3216)
{
	struct xz3216_board *pdata;
	struct device_node *regs;
	struct device_node *xz3216_np;
	int count;

	xz3216_np = of_node_get(xz3216->dev->of_node);
	if (!xz3216_np) {
		DBG_ERR("could not find pmic sub-node\n");
		return NULL;
	}
	regs = of_find_node_by_name(xz3216_np, "regulators");
	if (!regs)
		return NULL;
	count = of_regulator_match(xz3216->dev, regs, xz3216_reg_matches,
				   XZ3216_NUM_REGULATORS);
	of_node_put(regs);
	pdata = devm_kzalloc(xz3216->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;
	pdata->xz3216_init_data = xz3216_reg_matches[0].init_data;
	pdata->of_node = xz3216_reg_matches[0].of_node;
	return pdata;
}

#else
static struct xz3216_board *xz3216_parse_dt(struct i2c_client *i2c)
{
	return NULL;
}
#endif

static int xz3216_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct xz3216 *xz3216;
	struct xz3216_board *pdev;
	const struct of_device_id *match;
	struct regulator_config config = { };
	int ret;
	DBG("%s, line=%d\n", __func__, __LINE__);
	xz3216 = devm_kzalloc(&i2c->dev, sizeof(struct xz3216),
						GFP_KERNEL);
	if (!xz3216) {
		ret = -ENOMEM;
		goto err;
	}

	if (i2c->dev.of_node) {
		match = of_match_device(xz3216_of_match, &i2c->dev);
		if (!match) {
			DBG_ERR("Failed to find matching dt id\n");
			return -EINVAL;
		}
	}

	xz3216->regmap = devm_regmap_init_i2c(i2c, &xz3216_regmap_config);
	if (IS_ERR(xz3216->regmap)) {
		dev_err(&i2c->dev, "Failed to allocate regmap!\n");
		return PTR_ERR(xz3216->regmap);
	}

	xz3216->i2c = i2c;
	xz3216->dev = &i2c->dev;
	i2c_set_clientdata(i2c, xz3216);
	pdev = dev_get_platdata(&i2c->dev);
	if (!pdev)
		pdev = xz3216_parse_dt(xz3216);
	if (pdev) {
		xz3216->num_regulators = XZ3216_NUM_REGULATORS;
		xz3216->rdev = kcalloc(XZ3216_NUM_REGULATORS,
					sizeof(struct regulator_dev),
					GFP_KERNEL);
		if (!xz3216->rdev)
			return -ENOMEM;
		/* Instantiate the regulators */
		xz3216->regulator = pdev->xz3216_init_data;
		if (xz3216->dev->of_node)
			config.of_node = pdev->of_node;
		config.dev = xz3216->dev;
		config.driver_data = xz3216;
		config.init_data = xz3216->regulator;
		xz3216->rdev = devm_regulator_register(xz3216->dev,
						&regulators[0], &config);
		ret = PTR_ERR_OR_ZERO(xz3216->rdev);
		if (ret < 0)
			dev_err(&i2c->dev, "Failed to register regulator!\n");
		return ret;
	}
	return 0;
err:
	return ret;
}

static int xz3216_i2c_remove(struct i2c_client *i2c)
{
	struct xz3216 *xz3216 = i2c_get_clientdata(i2c);

	if (xz3216->rdev)
		regulator_unregister(xz3216->rdev);
	i2c_set_clientdata(i2c, NULL);
	return 0;
}

static const struct i2c_device_id xz3216_i2c_id[] = {
	{ "xz3216", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, xz3216_i2c_id);

static struct i2c_driver xz3216_i2c_driver = {
	.driver = {
		.name = "xz3216",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(xz3216_of_match),
	},
	.probe = xz3216_i2c_probe,
	.remove = xz3216_i2c_remove,
	.id_table = xz3216_i2c_id,
};

static int __init xz3216_module_init(void)
{
	int ret;

	ret = i2c_add_driver(&xz3216_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register I2C driver: %d\n", ret);
	return ret;
}
subsys_initcall_sync(xz3216_module_init);

static void __exit xz3216_module_exit(void)
{
	i2c_del_driver(&xz3216_i2c_driver);
}
module_exit(xz3216_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhangqing <zhangqing@rock-chips.com>");
MODULE_DESCRIPTION("xz3216 PMIC driver");
