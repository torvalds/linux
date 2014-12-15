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
	struct regulator_desc desc;
	struct pwm_voltages *duty_cycle_table;
	struct pwm_device *pwm;
	bool enabled;
	int state;
};

struct pwm_voltages {
	unsigned int uV;
	unsigned int dutycycle;
};

static int pwm_regulator_get_voltage_sel(struct regulator_dev *dev)
{
	struct pwm_regulator_data *drvdata = rdev_get_drvdata(dev);

	return drvdata->state;
}

static int pwm_regulator_set_voltage_sel(struct regulator_dev *dev,
					 unsigned selector)
{
	struct pwm_regulator_data *drvdata = rdev_get_drvdata(dev);
	unsigned int pwm_reg_period;
	int dutycycle;
	int ret;

	pwm_reg_period = pwm_get_period(drvdata->pwm);

	dutycycle = (pwm_reg_period *
		    drvdata->duty_cycle_table[selector].dutycycle) / 100;

	ret = pwm_config(drvdata->pwm, dutycycle, pwm_reg_period);
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

static int pwm_regulator_list_voltage(struct regulator_dev *dev,
				      unsigned selector)
{
	struct pwm_regulator_data *drvdata = rdev_get_drvdata(dev);

	if (selector >= drvdata->desc.n_voltages)
		return -EINVAL;

	return drvdata->duty_cycle_table[selector].uV;
}

static struct regulator_ops pwm_regulator_voltage_ops = {
	.set_voltage_sel = pwm_regulator_set_voltage_sel,
	.get_voltage_sel = pwm_regulator_get_voltage_sel,
	.list_voltage    = pwm_regulator_list_voltage,
	.map_voltage     = regulator_map_voltage_iterate,
};

static const struct regulator_desc pwm_regulator_desc = {
	.name		= "pwm-regulator",
	.ops		= &pwm_regulator_voltage_ops,
	.type		= REGULATOR_VOLTAGE,
	.owner		= THIS_MODULE,
	.supply_name    = "pwm",
};

static int pwm_regulator_probe(struct platform_device *pdev)
{
	struct pwm_regulator_data *drvdata;
	struct property *prop;
	struct regulator_dev *regulator;
	struct regulator_config config = { };
	struct device_node *np = pdev->dev.of_node;
	int length, ret;

	if (!np) {
		dev_err(&pdev->dev, "Device Tree node missing\n");
		return -EINVAL;
	}

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	memcpy(&drvdata->desc, &pwm_regulator_desc, sizeof(pwm_regulator_desc));

	/* determine the number of voltage-table */
	prop = of_find_property(np, "voltage-table", &length);
	if (!prop) {
		dev_err(&pdev->dev, "No voltage-table\n");
		return -EINVAL;
	}

	if ((length < sizeof(*drvdata->duty_cycle_table)) ||
	    (length % sizeof(*drvdata->duty_cycle_table))) {
		dev_err(&pdev->dev, "voltage-table length(%d) is invalid\n",
			length);
		return -EINVAL;
	}

	drvdata->desc.n_voltages = length / sizeof(*drvdata->duty_cycle_table);

	drvdata->duty_cycle_table = devm_kzalloc(&pdev->dev,
						 length, GFP_KERNEL);
	if (!drvdata->duty_cycle_table)
		return -ENOMEM;

	/* read voltage table from DT property */
	ret = of_property_read_u32_array(np, "voltage-table",
					 (u32 *)drvdata->duty_cycle_table,
					 length / sizeof(u32));
	if (ret < 0) {
		dev_err(&pdev->dev, "read voltage-table failed\n");
		return ret;
	}

	config.init_data = of_get_regulator_init_data(&pdev->dev, np,
						      &drvdata->desc);
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
					    &drvdata->desc, &config);
	if (IS_ERR(regulator)) {
		dev_err(&pdev->dev, "Failed to register regulator %s\n",
			drvdata->desc.name);
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
