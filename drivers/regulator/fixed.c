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
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/fixed.h>
#include <linux/gpio.h>

struct fixed_voltage_data {
	struct regulator_desc desc;
	struct regulator_dev *dev;
	int microvolts;
	int gpio;
	unsigned enable_high:1;
	unsigned is_enabled:1;
};

static int fixed_voltage_is_enabled(struct regulator_dev *dev)
{
	struct fixed_voltage_data *data = rdev_get_drvdata(dev);

	return data->is_enabled;
}

static int fixed_voltage_enable(struct regulator_dev *dev)
{
	struct fixed_voltage_data *data = rdev_get_drvdata(dev);

	if (gpio_is_valid(data->gpio)) {
		gpio_set_value_cansleep(data->gpio, data->enable_high);
		data->is_enabled = 1;
	}

	return 0;
}

static int fixed_voltage_disable(struct regulator_dev *dev)
{
	struct fixed_voltage_data *data = rdev_get_drvdata(dev);

	if (gpio_is_valid(data->gpio)) {
		gpio_set_value_cansleep(data->gpio, !data->enable_high);
		data->is_enabled = 0;
	}

	return 0;
}

static int fixed_voltage_get_voltage(struct regulator_dev *dev)
{
	struct fixed_voltage_data *data = rdev_get_drvdata(dev);

	return data->microvolts;
}

static int fixed_voltage_list_voltage(struct regulator_dev *dev,
				      unsigned selector)
{
	struct fixed_voltage_data *data = rdev_get_drvdata(dev);

	if (selector != 0)
		return -EINVAL;

	return data->microvolts;
}

static struct regulator_ops fixed_voltage_ops = {
	.is_enabled = fixed_voltage_is_enabled,
	.enable = fixed_voltage_enable,
	.disable = fixed_voltage_disable,
	.get_voltage = fixed_voltage_get_voltage,
	.list_voltage = fixed_voltage_list_voltage,
};

static int regulator_fixed_voltage_probe(struct platform_device *pdev)
{
	struct fixed_voltage_config *config = pdev->dev.platform_data;
	struct fixed_voltage_data *drvdata;
	int ret;

	drvdata = kzalloc(sizeof(struct fixed_voltage_data), GFP_KERNEL);
	if (drvdata == NULL) {
		dev_err(&pdev->dev, "Failed to allocate device data\n");
		ret = -ENOMEM;
		goto err;
	}

	drvdata->desc.name = kstrdup(config->supply_name, GFP_KERNEL);
	if (drvdata->desc.name == NULL) {
		dev_err(&pdev->dev, "Failed to allocate supply name\n");
		ret = -ENOMEM;
		goto err;
	}
	drvdata->desc.type = REGULATOR_VOLTAGE;
	drvdata->desc.owner = THIS_MODULE;
	drvdata->desc.ops = &fixed_voltage_ops;
	drvdata->desc.n_voltages = 1;

	drvdata->microvolts = config->microvolts;
	drvdata->gpio = config->gpio;

	if (gpio_is_valid(config->gpio)) {
		drvdata->enable_high = config->enable_high;

		/* FIXME: Remove below print warning
		 *
		 * config->gpio must be set to -EINVAL by platform code if
		 * GPIO control is not required. However, early adopters
		 * not requiring GPIO control may forget to initialize
		 * config->gpio to -EINVAL. This will cause GPIO 0 to be used
		 * for GPIO control.
		 *
		 * This warning will be removed once there are a couple of users
		 * for this driver.
		 */
		if (!config->gpio)
			dev_warn(&pdev->dev,
				"using GPIO 0 for regulator enable control\n");

		ret = gpio_request(config->gpio, config->supply_name);
		if (ret) {
			dev_err(&pdev->dev,
			   "Could not obtain regulator enable GPIO %d: %d\n",
							config->gpio, ret);
			goto err_name;
		}

		/* set output direction without changing state
		 * to prevent glitch
		 */
		drvdata->is_enabled = config->enabled_at_boot;
		ret = drvdata->is_enabled ?
				config->enable_high : !config->enable_high;

		ret = gpio_direction_output(config->gpio, ret);
		if (ret) {
			dev_err(&pdev->dev,
			   "Could not configure regulator enable GPIO %d direction: %d\n",
							config->gpio, ret);
			goto err_gpio;
		}

	} else {
		/* Regulator without GPIO control is considered
		 * always enabled
		 */
		drvdata->is_enabled = 1;
	}

	drvdata->dev = regulator_register(&drvdata->desc, &pdev->dev,
					  config->init_data, drvdata);
	if (IS_ERR(drvdata->dev)) {
		ret = PTR_ERR(drvdata->dev);
		dev_err(&pdev->dev, "Failed to register regulator: %d\n", ret);
		goto err_gpio;
	}

	platform_set_drvdata(pdev, drvdata);

	dev_dbg(&pdev->dev, "%s supplying %duV\n", drvdata->desc.name,
		drvdata->microvolts);

	return 0;

err_gpio:
	if (gpio_is_valid(config->gpio))
		gpio_free(config->gpio);
err_name:
	kfree(drvdata->desc.name);
err:
	kfree(drvdata);
	return ret;
}

static int regulator_fixed_voltage_remove(struct platform_device *pdev)
{
	struct fixed_voltage_data *drvdata = platform_get_drvdata(pdev);

	regulator_unregister(drvdata->dev);
	if (gpio_is_valid(drvdata->gpio))
		gpio_free(drvdata->gpio);
	kfree(drvdata->desc.name);
	kfree(drvdata);

	return 0;
}

static struct platform_driver regulator_fixed_voltage_driver = {
	.probe		= regulator_fixed_voltage_probe,
	.remove		= regulator_fixed_voltage_remove,
	.driver		= {
		.name		= "reg-fixed-voltage",
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
