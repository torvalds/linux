/*
 * Regulator driver for ST's PWM Regulators
 *
 * Copyright (C) 2014 - STMicroelectronics Inc.
 *
 * Author: Lee Jones <lee.jones@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pwm.h>

#define ST_PWM_REG_PERIOD 8448

struct st_pwm_regulator_pdata {
	const struct regulator_desc *desc;
	struct st_pwm_voltages *duty_cycle_table;
};

struct st_pwm_regulator_data {
	const struct st_pwm_regulator_pdata *pdata;
	struct pwm_device *pwm;
	bool enabled;
	int state;
};

struct st_pwm_voltages {
	unsigned int uV;
	unsigned int dutycycle;
};

static int st_pwm_regulator_get_voltage_sel(struct regulator_dev *dev)
{
	struct st_pwm_regulator_data *drvdata = rdev_get_drvdata(dev);

	return drvdata->state;
}

static int st_pwm_regulator_set_voltage_sel(struct regulator_dev *dev,
					    unsigned selector)
{
	struct st_pwm_regulator_data *drvdata = rdev_get_drvdata(dev);
	int dutycycle;
	int ret;

	dutycycle = (ST_PWM_REG_PERIOD / 100) *
		drvdata->pdata->duty_cycle_table[selector].dutycycle;

	ret = pwm_config(drvdata->pwm, dutycycle, ST_PWM_REG_PERIOD);
	if (ret) {
		dev_err(&dev->dev, "Failed to configure PWM\n");
		return ret;
	}

	drvdata->state = selector;

	if (!drvdata->enabled) {
		ret = pwm_enable(drvdata->pwm);
		if (ret) {
			dev_err(&dev->dev, "Failed to enable PWM\n");
			return ret;
		}
		drvdata->enabled = true;
	}

	return 0;
}

static int st_pwm_regulator_list_voltage(struct regulator_dev *dev,
					 unsigned selector)
{
	struct st_pwm_regulator_data *drvdata = rdev_get_drvdata(dev);

	if (selector >= dev->desc->n_voltages)
		return -EINVAL;

	return drvdata->pdata->duty_cycle_table[selector].uV;
}

static struct regulator_ops st_pwm_regulator_voltage_ops = {
	.set_voltage_sel = st_pwm_regulator_set_voltage_sel,
	.get_voltage_sel = st_pwm_regulator_get_voltage_sel,
	.list_voltage    = st_pwm_regulator_list_voltage,
	.map_voltage     = regulator_map_voltage_iterate,
};

static struct st_pwm_voltages b2105_duty_cycle_table[] = {
	{ .uV = 1114000, .dutycycle = 0,  },
	{ .uV = 1095000, .dutycycle = 10, },
	{ .uV = 1076000, .dutycycle = 20, },
	{ .uV = 1056000, .dutycycle = 30, },
	{ .uV = 1036000, .dutycycle = 40, },
	{ .uV = 1016000, .dutycycle = 50, },
	/* WARNING: Values above 50% duty-cycle cause boot failures. */
};

static const struct regulator_desc b2105_desc = {
	.name		= "b2105-pwm-regulator",
	.ops		= &st_pwm_regulator_voltage_ops,
	.type		= REGULATOR_VOLTAGE,
	.owner		= THIS_MODULE,
	.n_voltages	= ARRAY_SIZE(b2105_duty_cycle_table),
	.supply_name    = "pwm",
};

static const struct st_pwm_regulator_pdata b2105_info = {
	.desc		  = &b2105_desc,
	.duty_cycle_table = b2105_duty_cycle_table,
};

static struct of_device_id st_pwm_of_match[] = {
	{ .compatible = "st,b2105-pwm-regulator", .data = &b2105_info, },
	{ },
};
MODULE_DEVICE_TABLE(of, st_pwm_of_match);

static int st_pwm_regulator_probe(struct platform_device *pdev)
{
	struct st_pwm_regulator_data *drvdata;
	struct regulator_dev *regulator;
	struct regulator_config config = { };
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *of_match;

	if (!np) {
		dev_err(&pdev->dev, "Device Tree node missing\n");
		return -EINVAL;
	}

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	of_match = of_match_device(st_pwm_of_match, &pdev->dev);
	if (!of_match) {
		dev_err(&pdev->dev, "failed to match of device\n");
		return -ENODEV;
	}
	drvdata->pdata = of_match->data;

	config.init_data = of_get_regulator_init_data(&pdev->dev, np);
	if (!config.init_data)
		return -ENOMEM;

	config.of_node = np;
	config.dev = &pdev->dev;
	config.driver_data = drvdata;

	drvdata->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(drvdata->pwm)) {
		dev_err(&pdev->dev, "Failed to get PWM\n");
		return PTR_ERR(drvdata->pwm);
	}

	regulator = devm_regulator_register(&pdev->dev,
					    drvdata->pdata->desc, &config);
	if (IS_ERR(regulator)) {
		dev_err(&pdev->dev, "Failed to register regulator %s\n",
			drvdata->pdata->desc->name);
		return PTR_ERR(regulator);
	}

	return 0;
}

static struct platform_driver st_pwm_regulator_driver = {
	.driver = {
		.name		= "st-pwm-regulator",
		.owner		= THIS_MODULE,
		.of_match_table = of_match_ptr(st_pwm_of_match),
	},
	.probe = st_pwm_regulator_probe,
};

module_platform_driver(st_pwm_regulator_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lee Jones <lee.jones@linaro.org>");
MODULE_DESCRIPTION("ST PWM Regulator Driver");
MODULE_ALIAS("platform:st_pwm-regulator");
