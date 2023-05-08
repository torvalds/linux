// SPDX-License-Identifier: GPL-2.0+
//
// wm8994-regulator.c  --  Regulator driver for the WM8994
//
// Copyright 2009 Wolfson Microelectronics PLC.
//
// Author: Mark Brown <broonie@opensource.wolfsonmicro.com>

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>

#include <linux/mfd/wm8994/core.h>
#include <linux/mfd/wm8994/registers.h>
#include <linux/mfd/wm8994/pdata.h>

struct wm8994_ldo {
	struct regulator_dev *regulator;
	struct wm8994 *wm8994;
	struct regulator_consumer_supply supply;
	struct regulator_init_data init_data;
};

#define WM8994_LDO1_MAX_SELECTOR 0x7
#define WM8994_LDO2_MAX_SELECTOR 0x3

static const struct regulator_ops wm8994_ldo1_ops = {
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

static const struct regulator_ops wm8994_ldo2_ops = {
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
		.off_on_delay = 36000,
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
		.off_on_delay = 36000,
		.owner = THIS_MODULE,
	},
};

static const struct regulator_desc wm8958_ldo_desc[] = {
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

static const struct regulator_consumer_supply wm8994_ldo_consumer[] = {
	{ .supply = "AVDD1" },
	{ .supply = "DCVDD" },
};

static const struct regulator_init_data wm8994_ldo_default[] = {
	{
		.constraints = {
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = 1,
	},
	{
		.constraints = {
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = 1,
	},
};

static int wm8994_ldo_probe(struct platform_device *pdev)
{
	struct wm8994 *wm8994 = dev_get_drvdata(pdev->dev.parent);
	struct wm8994_pdata *pdata = dev_get_platdata(wm8994->dev);
	int id = pdev->id % ARRAY_SIZE(pdata->ldo);
	struct regulator_config config = { };
	struct wm8994_ldo *ldo;
	struct gpio_desc *gpiod;
	int ret;

	dev_dbg(&pdev->dev, "Probing LDO%d\n", id + 1);

	ldo = devm_kzalloc(&pdev->dev, sizeof(struct wm8994_ldo), GFP_KERNEL);
	if (!ldo)
		return -ENOMEM;

	ldo->wm8994 = wm8994;
	ldo->supply = wm8994_ldo_consumer[id];
	ldo->supply.dev_name = dev_name(wm8994->dev);

	config.dev = wm8994->dev;
	config.driver_data = ldo;
	config.regmap = wm8994->regmap;
	config.init_data = &ldo->init_data;

	/*
	 * Look up LDO enable GPIO from the parent device node, we don't
	 * use devm because the regulator core will free the GPIO
	 */
	gpiod = gpiod_get_optional(pdev->dev.parent,
				   id ? "wlf,ldo2ena" : "wlf,ldo1ena",
				   GPIOD_OUT_LOW |
				   GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(gpiod))
		return PTR_ERR(gpiod);
	config.ena_gpiod = gpiod;

	/* Use default constraints if none set up */
	if (!pdata || !pdata->ldo[id].init_data || wm8994->dev->of_node) {
		dev_dbg(wm8994->dev, "Using default init data, supply %s %s\n",
			ldo->supply.dev_name, ldo->supply.supply);

		ldo->init_data = wm8994_ldo_default[id];
		ldo->init_data.consumer_supplies = &ldo->supply;
		if (!gpiod)
			ldo->init_data.constraints.valid_ops_mask = 0;
	} else {
		ldo->init_data = *pdata->ldo[id].init_data;
	}

	/*
	 * At this point the GPIO descriptor is handled over to the
	 * regulator core and we need not worry about it on the
	 * error path.
	 */
	if (ldo->wm8994->type == WM8994) {
		ldo->regulator = devm_regulator_register(&pdev->dev,
							 &wm8994_ldo_desc[id],
							 &config);
	} else {
		ldo->regulator = devm_regulator_register(&pdev->dev,
							 &wm8958_ldo_desc[id],
							 &config);
	}

	if (IS_ERR(ldo->regulator)) {
		ret = PTR_ERR(ldo->regulator);
		dev_err(wm8994->dev, "Failed to register LDO%d: %d\n",
			id + 1, ret);
		return ret;
	}

	platform_set_drvdata(pdev, ldo);

	return 0;
}

static struct platform_driver wm8994_ldo_driver = {
	.probe = wm8994_ldo_probe,
	.driver		= {
		.name	= "wm8994-ldo",
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
	},
};

module_platform_driver(wm8994_ldo_driver);

/* Module information */
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("WM8994 LDO driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm8994-ldo");
