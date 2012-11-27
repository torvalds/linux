/*
 * arizona-micsupp.c  --  Microphone supply for Arizona devices
 *
 * Copyright 2012 Wolfson Microelectronics PLC.
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
#include <linux/regulator/machine.h>
#include <linux/gpio.h>
#include <linux/slab.h>

#include <linux/mfd/arizona/core.h>
#include <linux/mfd/arizona/pdata.h>
#include <linux/mfd/arizona/registers.h>

#define ARIZONA_MICSUPP_MAX_SELECTOR 0x1f

struct arizona_micsupp {
	struct regulator_dev *regulator;
	struct arizona *arizona;

	struct regulator_consumer_supply supply;
	struct regulator_init_data init_data;
};

static int arizona_micsupp_list_voltage(struct regulator_dev *rdev,
					unsigned int selector)
{
	if (selector > ARIZONA_MICSUPP_MAX_SELECTOR)
		return -EINVAL;

	if (selector == ARIZONA_MICSUPP_MAX_SELECTOR)
		return 3300000;
	else
		return (selector * 50000) + 1700000;
}

static int arizona_micsupp_map_voltage(struct regulator_dev *rdev,
				       int min_uV, int max_uV)
{
	unsigned int voltage;
	int selector;

	if (min_uV < 1700000)
		min_uV = 1700000;

	if (min_uV > 3200000)
		selector = ARIZONA_MICSUPP_MAX_SELECTOR;
	else
		selector = DIV_ROUND_UP(min_uV - 1700000, 50000);

	if (selector < 0)
		return -EINVAL;

	voltage = arizona_micsupp_list_voltage(rdev, selector);
	if (voltage < min_uV || voltage > max_uV)
		return -EINVAL;

	return selector;
}

static struct regulator_ops arizona_micsupp_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,

	.list_voltage = arizona_micsupp_list_voltage,
	.map_voltage = arizona_micsupp_map_voltage,

	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,

	.get_bypass = regulator_get_bypass_regmap,
	.set_bypass = regulator_set_bypass_regmap,
};

static const struct regulator_desc arizona_micsupp = {
	.name = "MICVDD",
	.supply_name = "CPVDD",
	.type = REGULATOR_VOLTAGE,
	.n_voltages = ARIZONA_MICSUPP_MAX_SELECTOR + 1,
	.ops = &arizona_micsupp_ops,

	.vsel_reg = ARIZONA_LDO2_CONTROL_1,
	.vsel_mask = ARIZONA_LDO2_VSEL_MASK,
	.enable_reg = ARIZONA_MIC_CHARGE_PUMP_1,
	.enable_mask = ARIZONA_CPMIC_ENA,
	.bypass_reg = ARIZONA_MIC_CHARGE_PUMP_1,
	.bypass_mask = ARIZONA_CPMIC_BYPASS,

	.owner = THIS_MODULE,
};

static const struct regulator_init_data arizona_micsupp_default = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS |
				REGULATOR_CHANGE_VOLTAGE,
		.min_uV = 1700000,
		.max_uV = 3300000,
	},

	.num_consumer_supplies = 1,
};

static int arizona_micsupp_probe(struct platform_device *pdev)
{
	struct arizona *arizona = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = { };
	struct arizona_micsupp *micsupp;
	int ret;

	micsupp = devm_kzalloc(&pdev->dev, sizeof(*micsupp), GFP_KERNEL);
	if (micsupp == NULL) {
		dev_err(&pdev->dev, "Unable to allocate private data\n");
		return -ENOMEM;
	}

	micsupp->arizona = arizona;

	/*
	 * Since the chip usually supplies itself we provide some
	 * default init_data for it.  This will be overridden with
	 * platform data if provided.
	 */
	micsupp->init_data = arizona_micsupp_default;
	micsupp->init_data.consumer_supplies = &micsupp->supply;
	micsupp->supply.supply = "MICVDD";
	micsupp->supply.dev_name = dev_name(arizona->dev);

	config.dev = arizona->dev;
	config.driver_data = micsupp;
	config.regmap = arizona->regmap;

	if (arizona->pdata.micvdd)
		config.init_data = arizona->pdata.micvdd;
	else
		config.init_data = &micsupp->init_data;

	/* Default to regulated mode until the API supports bypass */
	regmap_update_bits(arizona->regmap, ARIZONA_MIC_CHARGE_PUMP_1,
			   ARIZONA_CPMIC_BYPASS, 0);

	micsupp->regulator = regulator_register(&arizona_micsupp, &config);
	if (IS_ERR(micsupp->regulator)) {
		ret = PTR_ERR(micsupp->regulator);
		dev_err(arizona->dev, "Failed to register mic supply: %d\n",
			ret);
		return ret;
	}

	platform_set_drvdata(pdev, micsupp);

	return 0;
}

static int arizona_micsupp_remove(struct platform_device *pdev)
{
	struct arizona_micsupp *micsupp = platform_get_drvdata(pdev);

	regulator_unregister(micsupp->regulator);

	return 0;
}

static struct platform_driver arizona_micsupp_driver = {
	.probe = arizona_micsupp_probe,
	.remove = arizona_micsupp_remove,
	.driver		= {
		.name	= "arizona-micsupp",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(arizona_micsupp_driver);

/* Module information */
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("Arizona microphone supply driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:arizona-micsupp");
