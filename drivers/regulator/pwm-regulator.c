/*
 * Regulator driver for PWM Regulators
 *
 * Copyright (C) 2014 - STMicroelectronics Inc.
 *
 * Author: Lee Jones <lee.jones@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pwm.h>

struct pwm_regulator_data {
	/*  Shared */
	struct pwm_device *pwm;

	/* Voltage table */
	struct pwm_voltages *duty_cycle_table;
	int state;

	/* Continuous voltage */
	int volt_uV;
};

struct pwm_voltages {
	unsigned int uV;
	unsigned int dutycycle;
};

/**
 * Voltage table call-backs
 */
static int pwm_regulator_get_voltage_sel(struct regulator_dev *rdev)
{
	struct pwm_regulator_data *drvdata = rdev_get_drvdata(rdev);

	return drvdata->state;
}

static int pwm_regulator_set_voltage_sel(struct regulator_dev *rdev,
					 unsigned selector)
{
	struct pwm_regulator_data *drvdata = rdev_get_drvdata(rdev);
	unsigned int pwm_reg_period;
	int dutycycle;
	int ret;

	pwm_reg_period = pwm_get_period(drvdata->pwm);

	dutycycle = (pwm_reg_period *
		    drvdata->duty_cycle_table[selector].dutycycle) / 100;

	ret = pwm_config(drvdata->pwm, dutycycle, pwm_reg_period);
	if (ret) {
		dev_err(&rdev->dev, "Failed to configure PWM\n");
		return ret;
	}

	drvdata->state = selector;

	return 0;
}

static int pwm_regulator_list_voltage(struct regulator_dev *rdev,
				      unsigned selector)
{
	struct pwm_regulator_data *drvdata = rdev_get_drvdata(rdev);

	if (selector >= rdev->desc->n_voltages)
		return -EINVAL;

	return drvdata->duty_cycle_table[selector].uV;
}

static int pwm_regulator_enable(struct regulator_dev *dev)
{
	struct pwm_regulator_data *drvdata = rdev_get_drvdata(dev);

	return pwm_enable(drvdata->pwm);
}

static int pwm_regulator_disable(struct regulator_dev *dev)
{
	struct pwm_regulator_data *drvdata = rdev_get_drvdata(dev);

	pwm_disable(drvdata->pwm);

	return 0;
}

static int pwm_regulator_is_enabled(struct regulator_dev *dev)
{
	struct pwm_regulator_data *drvdata = rdev_get_drvdata(dev);

	return pwm_is_enabled(drvdata->pwm);
}

/**
 * Continuous voltage call-backs
 */
static int pwm_voltage_to_duty_cycle_percentage(struct regulator_dev *rdev, int req_uV)
{
	int min_uV = rdev->constraints->min_uV;
	int max_uV = rdev->constraints->max_uV;
	int diff = max_uV - min_uV;

	return 100 - (((req_uV * 100) - (min_uV * 100)) / diff);
}

static int pwm_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct pwm_regulator_data *drvdata = rdev_get_drvdata(rdev);

	return drvdata->volt_uV;
}

static int pwm_regulator_set_voltage(struct regulator_dev *rdev,
					int min_uV, int max_uV,
					unsigned *selector)
{
	struct pwm_regulator_data *drvdata = rdev_get_drvdata(rdev);
	unsigned int ramp_delay = rdev->constraints->ramp_delay;
	unsigned int period = pwm_get_period(drvdata->pwm);
	int duty_cycle;
	int ret;

	duty_cycle = pwm_voltage_to_duty_cycle_percentage(rdev, min_uV);

	ret = pwm_config(drvdata->pwm, (period / 100) * duty_cycle, period);
	if (ret) {
		dev_err(&rdev->dev, "Failed to configure PWM\n");
		return ret;
	}

	ret = pwm_enable(drvdata->pwm);
	if (ret) {
		dev_err(&rdev->dev, "Failed to enable PWM\n");
		return ret;
	}
	drvdata->volt_uV = min_uV;

	/* Delay required by PWM regulator to settle to the new voltage */
	usleep_range(ramp_delay, ramp_delay + 1000);

	return 0;
}

static struct regulator_ops pwm_regulator_voltage_table_ops = {
	.set_voltage_sel = pwm_regulator_set_voltage_sel,
	.get_voltage_sel = pwm_regulator_get_voltage_sel,
	.list_voltage    = pwm_regulator_list_voltage,
	.map_voltage     = regulator_map_voltage_iterate,
	.enable          = pwm_regulator_enable,
	.disable         = pwm_regulator_disable,
	.is_enabled      = pwm_regulator_is_enabled,
};

static struct regulator_ops pwm_regulator_voltage_continuous_ops = {
	.get_voltage = pwm_regulator_get_voltage,
	.set_voltage = pwm_regulator_set_voltage,
	.enable          = pwm_regulator_enable,
	.disable         = pwm_regulator_disable,
	.is_enabled      = pwm_regulator_is_enabled,
};

static struct regulator_desc pwm_regulator_desc = {
	.name		= "pwm-regulator",
	.type		= REGULATOR_VOLTAGE,
	.owner		= THIS_MODULE,
	.supply_name    = "pwm",
};

static int pwm_regulator_init_table(struct platform_device *pdev,
				    struct pwm_regulator_data *drvdata)
{
	struct device_node *np = pdev->dev.of_node;
	struct pwm_voltages *duty_cycle_table;
	unsigned int length = 0;
	int ret;

	of_find_property(np, "voltage-table", &length);

	if ((length < sizeof(*duty_cycle_table)) ||
	    (length % sizeof(*duty_cycle_table))) {
		dev_err(&pdev->dev,
			"voltage-table length(%d) is invalid\n",
			length);
		return -EINVAL;
	}

	duty_cycle_table = devm_kzalloc(&pdev->dev, length, GFP_KERNEL);
	if (!duty_cycle_table)
		return -ENOMEM;

	ret = of_property_read_u32_array(np, "voltage-table",
					 (u32 *)duty_cycle_table,
					 length / sizeof(u32));
	if (ret) {
		dev_err(&pdev->dev, "Failed to read voltage-table\n");
		return ret;
	}

	drvdata->duty_cycle_table	= duty_cycle_table;
	pwm_regulator_desc.ops		= &pwm_regulator_voltage_table_ops;
	pwm_regulator_desc.n_voltages	= length / sizeof(*duty_cycle_table);

	return 0;
}

static int pwm_regulator_init_continuous(struct platform_device *pdev,
					 struct pwm_regulator_data *drvdata)
{
	pwm_regulator_desc.ops = &pwm_regulator_voltage_continuous_ops;
	pwm_regulator_desc.continuous_voltage_range = true;

	return 0;
}

static int pwm_regulator_probe(struct platform_device *pdev)
{
	const struct regulator_init_data *init_data;
	struct pwm_regulator_data *drvdata;
	struct regulator_dev *regulator;
	struct regulator_config config = { };
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "Device Tree node missing\n");
		return -EINVAL;
	}

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	if (of_find_property(np, "voltage-table", NULL))
		ret = pwm_regulator_init_table(pdev, drvdata);
	else
		ret = pwm_regulator_init_continuous(pdev, drvdata);
	if (ret)
		return ret;

	init_data = of_get_regulator_init_data(&pdev->dev, np,
					       &pwm_regulator_desc);
	if (!init_data)
		return -ENOMEM;

	config.of_node = np;
	config.dev = &pdev->dev;
	config.driver_data = drvdata;
	config.init_data = init_data;

	drvdata->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(drvdata->pwm)) {
		dev_err(&pdev->dev, "Failed to get PWM\n");
		return PTR_ERR(drvdata->pwm);
	}

	regulator = devm_regulator_register(&pdev->dev,
					    &pwm_regulator_desc, &config);
	if (IS_ERR(regulator)) {
		dev_err(&pdev->dev, "Failed to register regulator %s\n",
			pwm_regulator_desc.name);
		return PTR_ERR(regulator);
	}

	return 0;
}

static const struct of_device_id pwm_of_match[] = {
	{ .compatible = "pwm-regulator" },
	{ },
};
MODULE_DEVICE_TABLE(of, pwm_of_match);

static struct platform_driver pwm_regulator_driver = {
	.driver = {
		.name		= "pwm-regulator",
		.of_match_table = of_match_ptr(pwm_of_match),
	},
	.probe = pwm_regulator_probe,
};

module_platform_driver(pwm_regulator_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lee Jones <lee.jones@linaro.org>");
MODULE_DESCRIPTION("PWM Regulator Driver");
MODULE_ALIAS("platform:pwm-regulator");
