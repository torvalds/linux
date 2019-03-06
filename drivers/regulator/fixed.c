/*
 * fixed.c
 *
 * Copyright 2008 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * Copyright (c) 2009 Nokia Corporation
 * Roger Quadros <ext-roger.quadros@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This is useful for systems with mixed controllable and
 * non-controllable regulators, as well as for allowing testing on
 * systems with no controllable regulators.
 */

#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/fixed.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>

struct fixed_voltage_data {
	struct regulator_desc desc;
	struct regulator_dev *dev;
};


/**
 * of_get_fixed_voltage_config - extract fixed_voltage_config structure info
 * @dev: device requesting for fixed_voltage_config
 * @desc: regulator description
 *
 * Populates fixed_voltage_config structure by extracting data from device
 * tree node, returns a pointer to the populated structure of NULL if memory
 * alloc fails.
 */
static struct fixed_voltage_config *
of_get_fixed_voltage_config(struct device *dev,
			    const struct regulator_desc *desc)
{
	struct fixed_voltage_config *config;
	struct device_node *np = dev->of_node;
	struct regulator_init_data *init_data;

	config = devm_kzalloc(dev, sizeof(struct fixed_voltage_config),
								 GFP_KERNEL);
	if (!config)
		return ERR_PTR(-ENOMEM);

	config->init_data = of_get_regulator_init_data(dev, dev->of_node, desc);
	if (!config->init_data)
		return ERR_PTR(-EINVAL);

	init_data = config->init_data;
	init_data->constraints.apply_uV = 0;

	config->supply_name = init_data->constraints.name;
	if (init_data->constraints.min_uV == init_data->constraints.max_uV) {
		config->microvolts = init_data->constraints.min_uV;
	} else {
		dev_err(dev,
			 "Fixed regulator specified with variable voltages\n");
		return ERR_PTR(-EINVAL);
	}

	if (init_data->constraints.boot_on)
		config->enabled_at_boot = true;

	of_property_read_u32(np, "startup-delay-us", &config->startup_delay);

	/*
	 * FIXME: we pulled active low/high and open drain handling into
	 * gpiolib so it will be handled there. Delete this in the second
	 * step when we also remove the custom inversion handling for all
	 * legacy boardfiles.
	 */
	config->enable_high = 1;
	config->gpio_is_open_drain = 0;

	if (of_find_property(np, "vin-supply", NULL))
		config->input_supply = "vin";

	return config;
}

static struct regulator_ops fixed_voltage_ops = {
};

static int reg_fixed_voltage_probe(struct platform_device *pdev)
{
	struct fixed_voltage_config *config;
	struct fixed_voltage_data *drvdata;
	struct regulator_config cfg = { };
	enum gpiod_flags gflags;
	int ret;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct fixed_voltage_data),
			       GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	if (pdev->dev.of_node) {
		config = of_get_fixed_voltage_config(&pdev->dev,
						     &drvdata->desc);
		if (IS_ERR(config))
			return PTR_ERR(config);
	} else {
		config = dev_get_platdata(&pdev->dev);
	}

	if (!config)
		return -ENOMEM;

	drvdata->desc.name = devm_kstrdup(&pdev->dev,
					  config->supply_name,
					  GFP_KERNEL);
	if (drvdata->desc.name == NULL) {
		dev_err(&pdev->dev, "Failed to allocate supply name\n");
		return -ENOMEM;
	}
	drvdata->desc.type = REGULATOR_VOLTAGE;
	drvdata->desc.owner = THIS_MODULE;
	drvdata->desc.ops = &fixed_voltage_ops;

	drvdata->desc.enable_time = config->startup_delay;

	if (config->input_supply) {
		drvdata->desc.supply_name = devm_kstrdup(&pdev->dev,
					    config->input_supply,
					    GFP_KERNEL);
		if (!drvdata->desc.supply_name) {
			dev_err(&pdev->dev,
				"Failed to allocate input supply\n");
			return -ENOMEM;
		}
	}

	if (config->microvolts)
		drvdata->desc.n_voltages = 1;

	drvdata->desc.fixed_uV = config->microvolts;

	cfg.ena_gpio_invert = !config->enable_high;
	if (config->enabled_at_boot) {
		if (config->enable_high)
			gflags = GPIOD_OUT_HIGH;
		else
			gflags = GPIOD_OUT_LOW;
	} else {
		if (config->enable_high)
			gflags = GPIOD_OUT_LOW;
		else
			gflags = GPIOD_OUT_HIGH;
	}
	if (config->gpio_is_open_drain) {
		if (gflags == GPIOD_OUT_HIGH)
			gflags = GPIOD_OUT_HIGH_OPEN_DRAIN;
		else
			gflags = GPIOD_OUT_LOW_OPEN_DRAIN;
	}

	/*
	 * Some fixed regulators share the enable line between two
	 * regulators which makes it necessary to get a handle on the
	 * same descriptor for two different consumers. This will get
	 * the GPIO descriptor, but only the first call will initialize
	 * it so any flags such as inversion or open drain will only
	 * be set up by the first caller and assumed identical on the
	 * next caller.
	 *
	 * FIXME: find a better way to deal with this.
	 */
	gflags |= GPIOD_FLAGS_BIT_NONEXCLUSIVE;

	/*
	 * Do not use devm* here: the regulator core takes over the
	 * lifecycle management of the GPIO descriptor.
	 */
	cfg.ena_gpiod = gpiod_get_optional(&pdev->dev, NULL, gflags);
	if (IS_ERR(cfg.ena_gpiod))
		return PTR_ERR(cfg.ena_gpiod);

	cfg.dev = &pdev->dev;
	cfg.init_data = config->init_data;
	cfg.driver_data = drvdata;
	cfg.of_node = pdev->dev.of_node;

	drvdata->dev = devm_regulator_register(&pdev->dev, &drvdata->desc,
					       &cfg);
	if (IS_ERR(drvdata->dev)) {
		ret = PTR_ERR(drvdata->dev);
		dev_err(&pdev->dev, "Failed to register regulator: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, drvdata);

	dev_dbg(&pdev->dev, "%s supplying %duV\n", drvdata->desc.name,
		drvdata->desc.fixed_uV);

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id fixed_of_match[] = {
	{ .compatible = "regulator-fixed", },
	{},
};
MODULE_DEVICE_TABLE(of, fixed_of_match);
#endif

static struct platform_driver regulator_fixed_voltage_driver = {
	.probe		= reg_fixed_voltage_probe,
	.driver		= {
		.name		= "reg-fixed-voltage",
		.of_match_table = of_match_ptr(fixed_of_match),
	},
};

static int __init regulator_fixed_voltage_init(void)
{
	return platform_driver_register(&regulator_fixed_voltage_driver);
}
subsys_initcall(regulator_fixed_voltage_init);

static void __exit regulator_fixed_voltage_exit(void)
{
	platform_driver_unregister(&regulator_fixed_voltage_driver);
}
module_exit(regulator_fixed_voltage_exit);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("Fixed voltage regulator");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:reg-fixed-voltage");
