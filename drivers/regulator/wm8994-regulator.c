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
	struct regulator_dev *regulator;
	struct wm8994 *wm8994;
};

#define WM8994_LDO1_MAX_SELECTOR 0x7
#define WM8994_LDO2_MAX_SELECTOR 0x3

static struct regulator_ops wm8994_ldo1_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
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

static struct regulator_ops wm8994_ldo2_ops = {
	.list_voltage = wm8994_ldo2_list_voltage,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
};

static const struct regulator_desc wm8994_ldo_desc[] = {
	{
		.name = "LDO1",
		.id = 1,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = WM8994_LDO1_MAX_SELECTOR + 1,
		.vsel_reg = WM8994_LDO_1,
		.vsel_mask = WM8994_LDO1_VSEL_MASK,
		.ops = &wm8994_ldo1_ops,
		.min_uV = 2400000,
		.uV_step = 100000,
		.enable_time = 3000,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO2",
		.id = 2,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = WM8994_LDO2_MAX_SELECTOR + 1,
		.vsel_reg = WM8994_LDO_2,
		.vsel_mask = WM8994_LDO2_VSEL_MASK,
		.ops = &wm8994_ldo2_ops,
		.enable_time = 3000,
		.owner = THIS_MODULE,
	},
};

static __devinit int wm8994_ldo_probe(struct platform_device *pdev)
{
	struct wm8994 *wm8994 = dev_get_drvdata(pdev->dev.parent);
	struct wm8994_pdata *pdata = wm8994->dev->platform_data;
	int id = pdev->id % ARRAY_SIZE(pdata->ldo);
	struct regulator_config config = { };
	struct wm8994_ldo *ldo;
	int ret;

	dev_dbg(&pdev->dev, "Probing LDO%d\n", id + 1);

	ldo = devm_kzalloc(&pdev->dev, sizeof(struct wm8994_ldo), GFP_KERNEL);
	if (ldo == NULL) {
		dev_err(&pdev->dev, "Unable to allocate private data\n");
		return -ENOMEM;
	}

	ldo->wm8994 = wm8994;

	config.dev = wm8994->dev;
	config.driver_data = ldo;
	config.regmap = wm8994->regmap;
	if (pdata) {
		config.init_data = pdata->ldo[id].init_data;
		config.ena_gpio = pdata->ldo[id].enable;
	}

	ldo->regulator = regulator_register(&wm8994_ldo_desc[id], &config);
	if (IS_ERR(ldo->regulator)) {
		ret = PTR_ERR(ldo->regulator);
		dev_err(wm8994->dev, "Failed to register LDO%d: %d\n",
			id + 1, ret);
		goto err;
	}

	platform_set_drvdata(pdev, ldo);

	return 0;

err:
	return ret;
}

static __devexit int wm8994_ldo_remove(struct platform_device *pdev)
{
	struct wm8994_ldo *ldo = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	regulator_unregister(ldo->regulator);

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
