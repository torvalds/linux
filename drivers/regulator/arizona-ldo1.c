/*
 * arizona-ldo1.c  --  LDO1 supply for Arizona devices
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
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/gpio.h>
#include <linux/slab.h>

#include <linux/mfd/arizona/core.h>
#include <linux/mfd/arizona/pdata.h>
#include <linux/mfd/arizona/registers.h>

struct arizona_ldo1 {
	struct regulator_dev *regulator;
	struct arizona *arizona;

	struct regulator_consumer_supply supply;
	struct regulator_init_data init_data;
};

static int arizona_ldo1_hc_list_voltage(struct regulator_dev *rdev,
					unsigned int selector)
{
	if (selector >= rdev->desc->n_voltages)
		return -EINVAL;

	if (selector == rdev->desc->n_voltages - 1)
		return 1800000;
	else
		return rdev->desc->min_uV + (rdev->desc->uV_step * selector);
}

static int arizona_ldo1_hc_map_voltage(struct regulator_dev *rdev,
				       int min_uV, int max_uV)
{
	int sel;

	sel = DIV_ROUND_UP(min_uV - rdev->desc->min_uV, rdev->desc->uV_step);
	if (sel >= rdev->desc->n_voltages)
		sel = rdev->desc->n_voltages - 1;

	return sel;
}

static int arizona_ldo1_hc_set_voltage_sel(struct regulator_dev *rdev,
					   unsigned sel)
{
	struct arizona_ldo1 *ldo = rdev_get_drvdata(rdev);
	struct regmap *regmap = ldo->arizona->regmap;
	unsigned int val;
	int ret;

	if (sel == rdev->desc->n_voltages - 1)
		val = ARIZONA_LDO1_HI_PWR;
	else
		val = 0;

	ret = regmap_update_bits(regmap, ARIZONA_LDO1_CONTROL_2,
				 ARIZONA_LDO1_HI_PWR, val);
	if (ret != 0)
		return ret;

	ret = regmap_update_bits(regmap, ARIZONA_DYNAMIC_FREQUENCY_SCALING_1,
				 ARIZONA_SUBSYS_MAX_FREQ, val);
	if (ret != 0)
		return ret;

	if (val)
		return 0;

	val = sel << ARIZONA_LDO1_VSEL_SHIFT;

	return regmap_update_bits(regmap, ARIZONA_LDO1_CONTROL_1,
				  ARIZONA_LDO1_VSEL_MASK, val);
}

static int arizona_ldo1_hc_get_voltage_sel(struct regulator_dev *rdev)
{
	struct arizona_ldo1 *ldo = rdev_get_drvdata(rdev);
	struct regmap *regmap = ldo->arizona->regmap;
	unsigned int val;
	int ret;

	ret = regmap_read(regmap, ARIZONA_LDO1_CONTROL_2, &val);
	if (ret != 0)
		return ret;

	if (val & ARIZONA_LDO1_HI_PWR)
		return rdev->desc->n_voltages - 1;

	ret = regmap_read(regmap, ARIZONA_LDO1_CONTROL_1, &val);
	if (ret != 0)
		return ret;

	return (val & ARIZONA_LDO1_VSEL_MASK) >> ARIZONA_LDO1_VSEL_SHIFT;
}

static struct regulator_ops arizona_ldo1_hc_ops = {
	.list_voltage = arizona_ldo1_hc_list_voltage,
	.map_voltage = arizona_ldo1_hc_map_voltage,
	.get_voltage_sel = arizona_ldo1_hc_get_voltage_sel,
	.set_voltage_sel = arizona_ldo1_hc_set_voltage_sel,
	.get_bypass = regulator_get_bypass_regmap,
	.set_bypass = regulator_set_bypass_regmap,
};

static const struct regulator_desc arizona_ldo1_hc = {
	.name = "LDO1",
	.supply_name = "LDOVDD",
	.type = REGULATOR_VOLTAGE,
	.ops = &arizona_ldo1_hc_ops,

	.bypass_reg = ARIZONA_LDO1_CONTROL_1,
	.bypass_mask = ARIZONA_LDO1_BYPASS,
	.min_uV = 900000,
	.uV_step = 50000,
	.n_voltages = 8,
	.enable_time = 1500,

	.owner = THIS_MODULE,
};

static struct regulator_ops arizona_ldo1_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
};

static const struct regulator_desc arizona_ldo1 = {
	.name = "LDO1",
	.supply_name = "LDOVDD",
	.type = REGULATOR_VOLTAGE,
	.ops = &arizona_ldo1_ops,

	.vsel_reg = ARIZONA_LDO1_CONTROL_1,
	.vsel_mask = ARIZONA_LDO1_VSEL_MASK,
	.min_uV = 900000,
	.uV_step = 25000,
	.n_voltages = 13,
	.enable_time = 500,

	.owner = THIS_MODULE,
};

static const struct regulator_init_data arizona_ldo1_dvfs = {
	.constraints = {
		.min_uV = 1200000,
		.max_uV = 1800000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS |
				  REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = 1,
};

static const struct regulator_init_data arizona_ldo1_default = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = 1,
};

static int arizona_ldo1_of_get_pdata(struct arizona *arizona,
				     struct regulator_config *config,
				     const struct regulator_desc *desc)
{
	struct arizona_pdata *pdata = &arizona->pdata;
	struct arizona_ldo1 *ldo1 = config->driver_data;
	struct device_node *init_node, *dcvdd_node;
	struct regulator_init_data *init_data;

	pdata->ldoena = arizona_of_get_named_gpio(arizona, "wlf,ldoena", true);

	init_node = of_get_child_by_name(arizona->dev->of_node, "ldo1");
	dcvdd_node = of_parse_phandle(arizona->dev->of_node, "DCVDD-supply", 0);

	if (init_node) {
		config->of_node = init_node;

		init_data = of_get_regulator_init_data(arizona->dev, init_node,
						       desc);

		if (init_data) {
			init_data->consumer_supplies = &ldo1->supply;
			init_data->num_consumer_supplies = 1;

			if (dcvdd_node && dcvdd_node != init_node)
				arizona->external_dcvdd = true;

			pdata->ldo1 = init_data;
		}
	} else if (dcvdd_node) {
		arizona->external_dcvdd = true;
	}

	of_node_put(dcvdd_node);

	return 0;
}

static int arizona_ldo1_probe(struct platform_device *pdev)
{
	struct arizona *arizona = dev_get_drvdata(pdev->dev.parent);
	const struct regulator_desc *desc;
	struct regulator_config config = { };
	struct arizona_ldo1 *ldo1;
	int ret;

	arizona->external_dcvdd = false;

	ldo1 = devm_kzalloc(&pdev->dev, sizeof(*ldo1), GFP_KERNEL);
	if (!ldo1)
		return -ENOMEM;

	ldo1->arizona = arizona;

	/*
	 * Since the chip usually supplies itself we provide some
	 * default init_data for it.  This will be overridden with
	 * platform data if provided.
	 */
	switch (arizona->type) {
	case WM5102:
	case WM8997:
		desc = &arizona_ldo1_hc;
		ldo1->init_data = arizona_ldo1_dvfs;
		break;
	default:
		desc = &arizona_ldo1;
		ldo1->init_data = arizona_ldo1_default;
		break;
	}

	ldo1->init_data.consumer_supplies = &ldo1->supply;
	ldo1->supply.supply = "DCVDD";
	ldo1->supply.dev_name = dev_name(arizona->dev);

	config.dev = arizona->dev;
	config.driver_data = ldo1;
	config.regmap = arizona->regmap;

	if (IS_ENABLED(CONFIG_OF)) {
		if (!dev_get_platdata(arizona->dev)) {
			ret = arizona_ldo1_of_get_pdata(arizona, &config, desc);
			if (ret < 0)
				return ret;
		}
	}

	config.ena_gpio = arizona->pdata.ldoena;

	if (arizona->pdata.ldo1)
		config.init_data = arizona->pdata.ldo1;
	else
		config.init_data = &ldo1->init_data;

	/*
	 * LDO1 can only be used to supply DCVDD so if it has no
	 * consumers then DCVDD is supplied externally.
	 */
	if (config.init_data->num_consumer_supplies == 0)
		arizona->external_dcvdd = true;

	ldo1->regulator = devm_regulator_register(&pdev->dev, desc, &config);
	if (IS_ERR(ldo1->regulator)) {
		ret = PTR_ERR(ldo1->regulator);
		dev_err(arizona->dev, "Failed to register LDO1 supply: %d\n",
			ret);
		return ret;
	}

	of_node_put(config.of_node);

	platform_set_drvdata(pdev, ldo1);

	return 0;
}

static struct platform_driver arizona_ldo1_driver = {
	.probe = arizona_ldo1_probe,
	.driver		= {
		.name	= "arizona-ldo1",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(arizona_ldo1_driver);

/* Module information */
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("Arizona LDO1 driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:arizona-ldo1");
