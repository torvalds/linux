/*
 * wm8994-regulator.c  --  Regulator driver for the WM8994
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/gpio.h>
#include <linux/slab.h>

#include <linux/mfd/wm8994/core.h>
#include <linux/mfd/wm8994/registers.h>
#include <linux/mfd/wm8994/pdata.h>

struct wm8994_ldo {
	int enable;
	bool is_enabled;
	struct regulator_dev *regulator;
	struct wm8994 *wm8994;
};

#define WM8994_LDO1_MAX_SELECTOR 0x7
#define WM8994_LDO2_MAX_SELECTOR 0x3

static int wm8994_ldo_enable(struct regulator_dev *rdev)
{
	struct wm8994_ldo *ldo = rdev_get_drvdata(rdev);

	/* If we have no soft control assume that the LDO is always enabled. */
	if (!ldo->enable)
		return 0;

	gpio_set_value_cansleep(ldo->enable, 1);
	ldo->is_enabled = true;

	return 0;
}

static int wm8994_ldo_disable(struct regulator_dev *rdev)
{
	struct wm8994_ldo *ldo = rdev_get_drvdata(rdev);

	/* If we have no soft control assume that the LDO is always enabled. */
	if (!ldo->enable)
		return -EINVAL;

	gpio_set_value_cansleep(ldo->enable, 0);
	ldo->is_enabled = false;

	return 0;
}

static int wm8994_ldo_is_enabled(struct regulator_dev *rdev)
{
	struct wm8994_ldo *ldo = rdev_get_drvdata(rdev);

	return ldo->is_enabled;
}

static int wm8994_ldo_enable_time(struct regulator_dev *rdev)
{
	/* 3ms is fairly conservative but this shouldn't be too performance
	 * critical; can be tweaked per-system if required. */
	return 3000;
}

static int wm8994_ldo1_list_voltage(struct regulator_dev *rdev,
				    unsigned int selector)
{
	if (selector > WM8994_LDO1_MAX_SELECTOR)
		return -EINVAL;

	return (selector * 100000) + 2400000;
}

static int wm8994_ldo1_get_voltage_sel(struct regulator_dev *rdev)
{
	struct wm8994_ldo *ldo = rdev_get_drvdata(rdev);
	int val;

	val = wm8994_reg_read(ldo->wm8994, WM8994_LDO_1);
	if (val < 0)
		return val;

	return (val & WM8994_LDO1_VSEL_MASK) >> WM8994_LDO1_VSEL_SHIFT;
}

static int wm8994_ldo1_set_voltage(struct regulator_dev *rdev,
				   int min_uV, int max_uV, unsigned *s)
{
	struct wm8994_ldo *ldo = rdev_get_drvdata(rdev);
	int selector, v;

	selector = (min_uV - 2400000) / 100000;
	v = wm8994_ldo1_list_voltage(rdev, selector);
	if (v < 0 || v > max_uV)
		return -EINVAL;

	*s = selector;
	selector <<= WM8994_LDO1_VSEL_SHIFT;

	return wm8994_set_bits(ldo->wm8994, WM8994_LDO_1,
			       WM8994_LDO1_VSEL_MASK, selector);
}

static struct regulator_ops wm8994_ldo1_ops = {
	.enable = wm8994_ldo_enable,
	.disable = wm8994_ldo_disable,
	.is_enabled = wm8994_ldo_is_enabled,
	.enable_time = wm8994_ldo_enable_time,

	.list_voltage = wm8994_ldo1_list_voltage,
	.get_voltage_sel = wm8994_ldo1_get_voltage_sel,
	.set_voltage = wm8994_ldo1_set_voltage,
};

static int wm8994_ldo2_list_voltage(struct regulator_dev *rdev,
				    unsigned int selector)
{
	struct wm8994_ldo *ldo = rdev_get_drvdata(rdev);

	if (selector > WM8994_LDO2_MAX_SELECTOR)
		return -EINVAL;

	switch (ldo->wm8994->type) {
	case WM8994:
		return (selector * 100000) + 900000;
	case WM8958:
		return (selector * 100000) + 1000000;
	case WM1811:
		switch (selector) {
		case 0:
			return -EINVAL;
		default:
			return (selector * 100000) + 950000;
		}
		break;
	default:
		return -EINVAL;
	}
}

static int wm8994_ldo2_get_voltage_sel(struct regulator_dev *rdev)
{
	struct wm8994_ldo *ldo = rdev_get_drvdata(rdev);
	int val;

	val = wm8994_reg_read(ldo->wm8994, WM8994_LDO_2);
	if (val < 0)
		return val;

	return (val & WM8994_LDO2_VSEL_MASK) >> WM8994_LDO2_VSEL_SHIFT;
}

static int wm8994_ldo2_set_voltage(struct regulator_dev *rdev,
				   int min_uV, int max_uV, unsigned *s)
{
	struct wm8994_ldo *ldo = rdev_get_drvdata(rdev);
	int selector, v;

	switch (ldo->wm8994->type) {
	case WM8994:
		selector = (min_uV - 900000) / 100000;
		break;
	case WM8958:
		selector = (min_uV - 1000000) / 100000;
		break;
	case WM1811:
		selector = (min_uV - 950000) / 100000;
		if (selector == 0)
			selector = 1;
		break;
	default:
		return -EINVAL;
	}

	v = wm8994_ldo2_list_voltage(rdev, selector);
	if (v < 0 || v > max_uV)
		return -EINVAL;

	*s = selector;
	selector <<= WM8994_LDO2_VSEL_SHIFT;

	return wm8994_set_bits(ldo->wm8994, WM8994_LDO_2,
			       WM8994_LDO2_VSEL_MASK, selector);
}

static struct regulator_ops wm8994_ldo2_ops = {
	.enable = wm8994_ldo_enable,
	.disable = wm8994_ldo_disable,
	.is_enabled = wm8994_ldo_is_enabled,
	.enable_time = wm8994_ldo_enable_time,

	.list_voltage = wm8994_ldo2_list_voltage,
	.get_voltage_sel = wm8994_ldo2_get_voltage_sel,
	.set_voltage = wm8994_ldo2_set_voltage,
};

static struct regulator_desc wm8994_ldo_desc[] = {
	{
		.name = "LDO1",
		.id = 1,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = WM8994_LDO1_MAX_SELECTOR + 1,
		.ops = &wm8994_ldo1_ops,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO2",
		.id = 2,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = WM8994_LDO2_MAX_SELECTOR + 1,
		.ops = &wm8994_ldo2_ops,
		.owner = THIS_MODULE,
	},
};

static __devinit int wm8994_ldo_probe(struct platform_device *pdev)
{
	struct wm8994 *wm8994 = dev_get_drvdata(pdev->dev.parent);
	struct wm8994_pdata *pdata = wm8994->dev->platform_data;
	int id = pdev->id % ARRAY_SIZE(pdata->ldo);
	struct wm8994_ldo *ldo;
	int ret;

	dev_dbg(&pdev->dev, "Probing LDO%d\n", id + 1);

	if (!pdata)
		return -ENODEV;

	ldo = kzalloc(sizeof(struct wm8994_ldo), GFP_KERNEL);
	if (ldo == NULL) {
		dev_err(&pdev->dev, "Unable to allocate private data\n");
		return -ENOMEM;
	}

	ldo->wm8994 = wm8994;

	if (pdata->ldo[id].enable && gpio_is_valid(pdata->ldo[id].enable)) {
		ldo->enable = pdata->ldo[id].enable;

		ret = gpio_request(ldo->enable, "WM8994 LDO enable");
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to get enable GPIO: %d\n",
				ret);
			goto err;
		}

		ret = gpio_direction_output(ldo->enable, ldo->is_enabled);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to set GPIO up: %d\n",
				ret);
			goto err_gpio;
		}
	} else
		ldo->is_enabled = true;

	ldo->regulator = regulator_register(&wm8994_ldo_desc[id], &pdev->dev,
					     pdata->ldo[id].init_data, ldo);
	if (IS_ERR(ldo->regulator)) {
		ret = PTR_ERR(ldo->regulator);
		dev_err(wm8994->dev, "Failed to register LDO%d: %d\n",
			id + 1, ret);
		goto err_gpio;
	}

	platform_set_drvdata(pdev, ldo);

	return 0;

err_gpio:
	if (gpio_is_valid(ldo->enable))
		gpio_free(ldo->enable);
err:
	kfree(ldo);
	return ret;
}

static __devexit int wm8994_ldo_remove(struct platform_device *pdev)
{
	struct wm8994_ldo *ldo = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	regulator_unregister(ldo->regulator);
	if (gpio_is_valid(ldo->enable))
		gpio_free(ldo->enable);
	kfree(ldo);

	return 0;
}

static struct platform_driver wm8994_ldo_driver = {
	.probe = wm8994_ldo_probe,
	.remove = __devexit_p(wm8994_ldo_remove),
	.driver		= {
		.name	= "wm8994-ldo",
		.owner	= THIS_MODULE,
	},
};

static int __init wm8994_ldo_init(void)
{
	int ret;

	ret = platform_driver_register(&wm8994_ldo_driver);
	if (ret != 0)
		pr_err("Failed to register Wm8994 GP LDO driver: %d\n", ret);

	return ret;
}
subsys_initcall(wm8994_ldo_init);

static void __exit wm8994_ldo_exit(void)
{
	platform_driver_unregister(&wm8994_ldo_driver);
}
module_exit(wm8994_ldo_exit);

/* Module information */
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("WM8994 LDO driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm8994-ldo");
