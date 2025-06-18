// SPDX-License-Identifier: GPL-2.0-or-later
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
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/gpio-regulator.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/of.h>

struct gpio_regulator_data {
	struct regulator_desc desc;

	struct gpio_desc **gpiods;
	int nr_gpios;

	struct gpio_regulator_state *states;
	int nr_states;

	int state;
};

static int gpio_regulator_get_value(struct regulator_dev *dev)
{
	struct gpio_regulator_data *data = rdev_get_drvdata(dev);
	int ptr;

	for (ptr = 0; ptr < data->nr_states; ptr++)
		if (data->states[ptr].gpios == data->state)
			return data->states[ptr].value;

	return -EINVAL;
}

static int gpio_regulator_set_voltage(struct regulator_dev *dev,
					int min_uV, int max_uV,
					unsigned *selector)
{
	struct gpio_regulator_data *data = rdev_get_drvdata(dev);
	int ptr, target = 0, state, best_val = INT_MAX;

	for (ptr = 0; ptr < data->nr_states; ptr++)
		if (data->states[ptr].value < best_val &&
		    data->states[ptr].value >= min_uV &&
		    data->states[ptr].value <= max_uV) {
			target = data->states[ptr].gpios;
			best_val = data->states[ptr].value;
			if (selector)
				*selector = ptr;
		}

	if (best_val == INT_MAX)
		return -EINVAL;

	for (ptr = 0; ptr < data->nr_gpios; ptr++) {
		state = (target & (1 << ptr)) >> ptr;
		gpiod_set_value_cansleep(data->gpiods[ptr], state);
	}
	data->state = target;

	return 0;
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
	struct gpio_regulator_data *data = rdev_get_drvdata(dev);
	int ptr, target = 0, state, best_val = 0;

	for (ptr = 0; ptr < data->nr_states; ptr++)
		if (data->states[ptr].value > best_val &&
		    data->states[ptr].value >= min_uA &&
		    data->states[ptr].value <= max_uA) {
			target = data->states[ptr].gpios;
			best_val = data->states[ptr].value;
		}

	if (best_val == 0)
		return -EINVAL;

	for (ptr = 0; ptr < data->nr_gpios; ptr++) {
		state = (target & (1 << ptr)) >> ptr;
		gpiod_set_value_cansleep(data->gpiods[ptr], state);
	}
	data->state = target;

	return 0;
}

static const struct regulator_ops gpio_regulator_voltage_ops = {
	.get_voltage = gpio_regulator_get_value,
	.set_voltage = gpio_regulator_set_voltage,
	.list_voltage = gpio_regulator_list_voltage,
};

static struct gpio_regulator_config *
of_get_gpio_regulator_config(struct device *dev, struct device_node *np,
			     const struct regulator_desc *desc)
{
	struct gpio_regulator_config *config;
	const char *regtype;
	int proplen, i;
	int ngpios;
	int ret;

	config = devm_kzalloc(dev,
			sizeof(struct gpio_regulator_config),
			GFP_KERNEL);
	if (!config)
		return ERR_PTR(-ENOMEM);

	config->init_data = of_get_regulator_init_data(dev, np, desc);
	if (!config->init_data)
		return ERR_PTR(-EINVAL);

	config->supply_name = config->init_data->constraints.name;

	if (config->init_data->constraints.boot_on)
		config->enabled_at_boot = true;

	/*
	 * Do not use: undocumented device tree property.
	 * This is kept around solely for device tree ABI stability.
	 */
	if (of_property_read_bool(np, "enable-at-boot"))
		config->enabled_at_boot = true;

	of_property_read_u32(np, "startup-delay-us", &config->startup_delay);

	/* Fetch GPIO init levels */
	ngpios = gpiod_count(dev, NULL);
	if (ngpios > 0) {
		config->gflags = devm_kzalloc(dev,
					      sizeof(enum gpiod_flags)
					      * ngpios,
					      GFP_KERNEL);
		if (!config->gflags)
			return ERR_PTR(-ENOMEM);

		for (i = 0; i < ngpios; i++) {
			u32 val;

			ret = of_property_read_u32_index(np, "gpios-states", i,
							 &val);

			/* Default to high per specification */
			if (ret)
				config->gflags[i] = GPIOD_OUT_HIGH;
			else
				config->gflags[i] =
					val ? GPIOD_OUT_HIGH : GPIOD_OUT_LOW;
		}
	}
	config->ngpios = ngpios;

	/* Fetch states. */
	proplen = of_property_count_u32_elems(np, "states");
	if (proplen < 0) {
		dev_err(dev, "No 'states' property found\n");
		return ERR_PTR(-EINVAL);
	}

	config->states = devm_kcalloc(dev,
				proplen / 2,
				sizeof(struct gpio_regulator_state),
				GFP_KERNEL);
	if (!config->states)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < proplen / 2; i++) {
		of_property_read_u32_index(np, "states", i * 2,
					   &config->states[i].value);
		of_property_read_u32_index(np, "states", i * 2 + 1,
					   &config->states[i].gpios);
	}
	config->nr_states = i;

	config->type = REGULATOR_VOLTAGE;
	ret = of_property_read_string(np, "regulator-type", &regtype);
	if (ret >= 0) {
		if (!strncmp("voltage", regtype, 7))
			config->type = REGULATOR_VOLTAGE;
		else if (!strncmp("current", regtype, 7))
			config->type = REGULATOR_CURRENT;
		else
			dev_warn(dev, "Unknown regulator-type '%s'\n",
				 regtype);
	}

	if (of_property_present(np, "vin-supply"))
		config->input_supply = "vin";

	return config;
}

static const struct regulator_ops gpio_regulator_current_ops = {
	.get_current_limit = gpio_regulator_get_value,
	.set_current_limit = gpio_regulator_set_current_limit,
};

static int gpio_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_regulator_config *config = dev_get_platdata(dev);
	struct device_node *np = dev->of_node;
	struct gpio_regulator_data *drvdata;
	struct regulator_config cfg = { };
	struct regulator_dev *rdev;
	enum gpiod_flags gflags;
	int ptr, state, i;

	drvdata = devm_kzalloc(dev, sizeof(struct gpio_regulator_data),
			       GFP_KERNEL);
	if (drvdata == NULL)
		return -ENOMEM;

	if (np) {
		config = of_get_gpio_regulator_config(dev, np,
						      &drvdata->desc);
		if (IS_ERR(config))
			return PTR_ERR(config);
	}

	drvdata->desc.name = devm_kstrdup(dev, config->supply_name, GFP_KERNEL);
	if (drvdata->desc.name == NULL) {
		dev_err(dev, "Failed to allocate supply name\n");
		return -ENOMEM;
	}

	drvdata->gpiods = devm_kzalloc(dev, sizeof(struct gpio_desc *),
				       GFP_KERNEL);

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

	if (!drvdata->gpiods)
		return -ENOMEM;
	for (i = 0; i < config->ngpios; i++) {
		drvdata->gpiods[i] = devm_gpiod_get_index(dev,
							  NULL,
							  i,
							  config->gflags[i]);
		if (IS_ERR(drvdata->gpiods[i]))
			return PTR_ERR(drvdata->gpiods[i]);
		/* This is good to know */
		gpiod_set_consumer_name(drvdata->gpiods[i], drvdata->desc.name);
	}
	drvdata->nr_gpios = config->ngpios;

	drvdata->states = devm_kmemdup(dev,
				       config->states,
				       config->nr_states *
				       sizeof(struct gpio_regulator_state),
				       GFP_KERNEL);
	if (drvdata->states == NULL) {
		dev_err(dev, "Failed to allocate state data\n");
		return -ENOMEM;
	}
	drvdata->nr_states = config->nr_states;

	drvdata->desc.owner = THIS_MODULE;
	drvdata->desc.enable_time = config->startup_delay;

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
		dev_err(dev, "No regulator type set\n");
		return -EINVAL;
	}

	/* build initial state from gpio init data. */
	state = 0;
	for (ptr = 0; ptr < drvdata->nr_gpios; ptr++) {
		if (config->gflags[ptr] == GPIOD_OUT_HIGH)
			state |= (1 << ptr);
	}
	drvdata->state = state;

	cfg.dev = dev;
	cfg.init_data = config->init_data;
	cfg.driver_data = drvdata;
	cfg.of_node = np;

	/*
	 * The signal will be inverted by the GPIO core if flagged so in the
	 * descriptor.
	 */
	if (config->enabled_at_boot)
		gflags = GPIOD_OUT_HIGH | GPIOD_FLAGS_BIT_NONEXCLUSIVE;
	else
		gflags = GPIOD_OUT_LOW | GPIOD_FLAGS_BIT_NONEXCLUSIVE;

	cfg.ena_gpiod = gpiod_get_optional(dev, "enable", gflags);
	if (IS_ERR(cfg.ena_gpiod))
		return PTR_ERR(cfg.ena_gpiod);

	rdev = devm_regulator_register(dev, &drvdata->desc, &cfg);
	if (IS_ERR(rdev))
		return dev_err_probe(dev, PTR_ERR(rdev),
				     "Failed to register regulator\n");

	platform_set_drvdata(pdev, drvdata);

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id regulator_gpio_of_match[] = {
	{ .compatible = "regulator-gpio", },
	{},
};
MODULE_DEVICE_TABLE(of, regulator_gpio_of_match);
#endif

static struct platform_driver gpio_regulator_driver = {
	.probe		= gpio_regulator_probe,
	.driver		= {
		.name		= "gpio-regulator",
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(regulator_gpio_of_match),
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
