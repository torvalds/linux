/*
 * gpio-regulator.c
 *
 * Copyright 2011 Heiko Stuebner <heiko@sntech.de>
 *
 * based on fixed.c
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
#include <linux/regulator/machine.h>
#include <linux/regulator/gpio-regulator.h>
#include <linux/gpio.h>
#include <linux/slab.h>

struct gpio_regulator_data {
	struct regulator_desc desc;
	struct regulator_dev *dev;

	int enable_gpio;
	bool enable_high;
	bool is_enabled;
	unsigned startup_delay;

	struct gpio *gpios;
	int nr_gpios;

	struct gpio_regulator_state *states;
	int nr_states;

	int state;
};

static int gpio_regulator_is_enabled(struct regulator_dev *dev)
{
	struct gpio_regulator_data *data = rdev_get_drvdata(dev);

	return data->is_enabled;
}

static int gpio_regulator_enable(struct regulator_dev *dev)
{
	struct gpio_regulator_data *data = rdev_get_drvdata(dev);

	if (gpio_is_valid(data->enable_gpio)) {
		gpio_set_value_cansleep(data->enable_gpio, data->enable_high);
		data->is_enabled = true;
	}

	return 0;
}

static int gpio_regulator_disable(struct regulator_dev *dev)
{
	struct gpio_regulator_data *data = rdev_get_drvdata(dev);

	if (gpio_is_valid(data->enable_gpio)) {
		gpio_set_value_cansleep(data->enable_gpio, !data->enable_high);
		data->is_enabled = false;
	}

	return 0;
}

static int gpio_regulator_enable_time(struct regulator_dev *dev)
{
	struct gpio_regulator_data *data = rdev_get_drvdata(dev);

	return data->startup_delay;
}

static int gpio_regulator_get_value(struct regulator_dev *dev)
{
	struct gpio_regulator_data *data = rdev_get_drvdata(dev);
	int ptr;

	for (ptr = 0; ptr < data->nr_states; ptr++)
		if (data->states[ptr].gpios == data->state)
			return data->states[ptr].value;

	return -EINVAL;
}

static int gpio_regulator_set_value(struct regulator_dev *dev,
					int min, int max)
{
	struct gpio_regulator_data *data = rdev_get_drvdata(dev);
	int ptr, target, state, best_val = INT_MAX;

	for (ptr = 0; ptr < data->nr_states; ptr++)
		if (data->states[ptr].value < best_val &&
		    data->states[ptr].value >= min &&
		    data->states[ptr].value <= max)
			target = data->states[ptr].gpios;

	if (best_val == INT_MAX)
		return -EINVAL;

	for (ptr = 0; ptr < data->nr_gpios; ptr++) {
		state = (target & (1 << ptr)) >> ptr;
		gpio_set_value(data->gpios[ptr].gpio, state);
	}
	data->state = target;

	return 0;
}

static int gpio_regulator_set_voltage(struct regulator_dev *dev,
					int min_uV, int max_uV,
					unsigned *selector)
{
	return gpio_regulator_set_value(dev, min_uV, max_uV);
}

static int gpio_regulator_list_voltage(struct regulator_dev *dev,
				      unsigned selector)
{
	struct gpio_regulator_data *data = rdev_get_drvdata(dev);

	if (selector >= data->nr_states)
		return -EINVAL;

	return data->states[selector].value;
}

static int gpio_regulator_set_current_limit(struct regulator_dev *dev,
					int min_uA, int max_uA)
{
	return gpio_regulator_set_value(dev, min_uA, max_uA);
}

static struct regulator_ops gpio_regulator_voltage_ops = {
	.is_enabled = gpio_regulator_is_enabled,
	.enable = gpio_regulator_enable,
	.disable = gpio_regulator_disable,
	.enable_time = gpio_regulator_enable_time,
	.get_voltage = gpio_regulator_get_value,
	.set_voltage = gpio_regulator_set_voltage,
	.list_voltage = gpio_regulator_list_voltage,
};

static struct regulator_ops gpio_regulator_current_ops = {
	.is_enabled = gpio_regulator_is_enabled,
	.enable = gpio_regulator_enable,
	.disable = gpio_regulator_disable,
	.enable_time = gpio_regulator_enable_time,
	.get_current_limit = gpio_regulator_get_value,
	.set_current_limit = gpio_regulator_set_current_limit,
};

static int __devinit gpio_regulator_probe(struct platform_device *pdev)
{
	struct gpio_regulator_config *config = pdev->dev.platform_data;
	struct gpio_regulator_data *drvdata;
	struct regulator_config cfg = { };
	int ptr, ret, state;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct gpio_regulator_data),
			       GFP_KERNEL);
	if (drvdata == NULL) {
		dev_err(&pdev->dev, "Failed to allocate device data\n");
		return -ENOMEM;
	}

	drvdata->desc.name = kstrdup(config->supply_name, GFP_KERNEL);
	if (drvdata->desc.name == NULL) {
		dev_err(&pdev->dev, "Failed to allocate supply name\n");
		ret = -ENOMEM;
		goto err;
	}

	drvdata->gpios = kmemdup(config->gpios,
				 config->nr_gpios * sizeof(struct gpio),
				 GFP_KERNEL);
	if (drvdata->gpios == NULL) {
		dev_err(&pdev->dev, "Failed to allocate gpio data\n");
		ret = -ENOMEM;
		goto err_name;
	}

	drvdata->states = kmemdup(config->states,
				  config->nr_states *
					 sizeof(struct gpio_regulator_state),
				  GFP_KERNEL);
	if (drvdata->states == NULL) {
		dev_err(&pdev->dev, "Failed to allocate state data\n");
		ret = -ENOMEM;
		goto err_memgpio;
	}
	drvdata->nr_states = config->nr_states;

	drvdata->desc.owner = THIS_MODULE;

	/* handle regulator type*/
	switch (config->type) {
	case REGULATOR_VOLTAGE:
		drvdata->desc.type = REGULATOR_VOLTAGE;
		drvdata->desc.ops = &gpio_regulator_voltage_ops;
		drvdata->desc.n_voltages = config->nr_states;
		break;
	case REGULATOR_CURRENT:
		drvdata->desc.type = REGULATOR_CURRENT;
		drvdata->desc.ops = &gpio_regulator_current_ops;
		break;
	default:
		dev_err(&pdev->dev, "No regulator type set\n");
		ret = -EINVAL;
		goto err_memgpio;
		break;
	}

	drvdata->enable_gpio = config->enable_gpio;
	drvdata->startup_delay = config->startup_delay;

	if (gpio_is_valid(config->enable_gpio)) {
		drvdata->enable_high = config->enable_high;

		ret = gpio_request(config->enable_gpio, config->supply_name);
		if (ret) {
			dev_err(&pdev->dev,
			   "Could not obtain regulator enable GPIO %d: %d\n",
						config->enable_gpio, ret);
			goto err_memstate;
		}

		/* set output direction without changing state
		 * to prevent glitch
		 */
		if (config->enabled_at_boot) {
			drvdata->is_enabled = true;
			ret = gpio_direction_output(config->enable_gpio,
						    config->enable_high);
		} else {
			drvdata->is_enabled = false;
			ret = gpio_direction_output(config->enable_gpio,
						    !config->enable_high);
		}

		if (ret) {
			dev_err(&pdev->dev,
			   "Could not configure regulator enable GPIO %d direction: %d\n",
						config->enable_gpio, ret);
			goto err_enablegpio;
		}
	} else {
		/* Regulator without GPIO control is considered
		 * always enabled
		 */
		drvdata->is_enabled = true;
	}

	drvdata->nr_gpios = config->nr_gpios;
	ret = gpio_request_array(drvdata->gpios, drvdata->nr_gpios);
	if (ret) {
		dev_err(&pdev->dev,
		   "Could not obtain regulator setting GPIOs: %d\n", ret);
		goto err_enablegpio;
	}

	/* build initial state from gpio init data. */
	state = 0;
	for (ptr = 0; ptr < drvdata->nr_gpios; ptr++) {
		if (config->gpios[ptr].flags & GPIOF_OUT_INIT_HIGH)
			state |= (1 << ptr);
	}
	drvdata->state = state;

	cfg.dev = &pdev->dev;
	cfg.init_data = config->init_data;
	cfg.driver_data = &drvdata;

	drvdata->dev = regulator_register(&drvdata->desc, &cfg);
	if (IS_ERR(drvdata->dev)) {
		ret = PTR_ERR(drvdata->dev);
		dev_err(&pdev->dev, "Failed to register regulator: %d\n", ret);
		goto err_stategpio;
	}

	platform_set_drvdata(pdev, drvdata);

	return 0;

err_stategpio:
	gpio_free_array(drvdata->gpios, drvdata->nr_gpios);
err_enablegpio:
	if (gpio_is_valid(config->enable_gpio))
		gpio_free(config->enable_gpio);
err_memstate:
	kfree(drvdata->states);
err_memgpio:
	kfree(drvdata->gpios);
err_name:
	kfree(drvdata->desc.name);
err:
	return ret;
}

static int __devexit gpio_regulator_remove(struct platform_device *pdev)
{
	struct gpio_regulator_data *drvdata = platform_get_drvdata(pdev);

	regulator_unregister(drvdata->dev);

	gpio_free_array(drvdata->gpios, drvdata->nr_gpios);

	kfree(drvdata->states);
	kfree(drvdata->gpios);

	if (gpio_is_valid(drvdata->enable_gpio))
		gpio_free(drvdata->enable_gpio);

	kfree(drvdata->desc.name);

	return 0;
}

static struct platform_driver gpio_regulator_driver = {
	.probe		= gpio_regulator_probe,
	.remove		= __devexit_p(gpio_regulator_remove),
	.driver		= {
		.name		= "gpio-regulator",
		.owner		= THIS_MODULE,
	},
};

static int __init gpio_regulator_init(void)
{
	return platform_driver_register(&gpio_regulator_driver);
}
subsys_initcall(gpio_regulator_init);

static void __exit gpio_regulator_exit(void)
{
	platform_driver_unregister(&gpio_regulator_driver);
}
module_exit(gpio_regulator_exit);

MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("gpio voltage regulator");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio-regulator");
