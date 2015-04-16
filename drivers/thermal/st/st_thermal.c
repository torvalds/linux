/*
 * ST Thermal Sensor Driver core routines
 * Author: Ajit Pal Singh <ajitpal.singh@st.com>
 *
 * Copyright (C) 2003-2014 STMicroelectronics (R&D) Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "st_thermal.h"

/* The Thermal Framework expects millidegrees */
#define mcelsius(temp)			((temp) * 1000)

/*
 * Function to allocate regfields which are common
 * between syscfg and memory mapped based sensors
 */
static int st_thermal_alloc_regfields(struct st_thermal_sensor *sensor)
{
	struct device *dev = sensor->dev;
	struct regmap *regmap = sensor->regmap;
	const struct reg_field *reg_fields = sensor->cdata->reg_fields;

	sensor->dcorrect = devm_regmap_field_alloc(dev, regmap,
						   reg_fields[DCORRECT]);

	sensor->overflow = devm_regmap_field_alloc(dev, regmap,
						   reg_fields[OVERFLOW]);

	sensor->temp_data = devm_regmap_field_alloc(dev, regmap,
						    reg_fields[DATA]);

	if (IS_ERR(sensor->dcorrect) ||
	    IS_ERR(sensor->overflow) ||
	    IS_ERR(sensor->temp_data)) {
		dev_err(dev, "failed to allocate common regfields\n");
		return -EINVAL;
	}

	return sensor->ops->alloc_regfields(sensor);
}

static int st_thermal_sensor_on(struct st_thermal_sensor *sensor)
{
	int ret;
	struct device *dev = sensor->dev;

	ret = clk_prepare_enable(sensor->clk);
	if (ret) {
		dev_err(dev, "failed to enable clk\n");
		return ret;
	}

	ret = sensor->ops->power_ctrl(sensor, POWER_ON);
	if (ret) {
		dev_err(dev, "failed to power on sensor\n");
		clk_disable_unprepare(sensor->clk);
	}

	return ret;
}

static int st_thermal_sensor_off(struct st_thermal_sensor *sensor)
{
	int ret;

	ret = sensor->ops->power_ctrl(sensor, POWER_OFF);
	if (ret)
		return ret;

	clk_disable_unprepare(sensor->clk);

	return 0;
}

static int st_thermal_calibration(struct st_thermal_sensor *sensor)
{
	int ret;
	unsigned int val;
	struct device *dev = sensor->dev;

	/* Check if sensor calibration data is already written */
	ret = regmap_field_read(sensor->dcorrect, &val);
	if (ret) {
		dev_err(dev, "failed to read calibration data\n");
		return ret;
	}

	if (!val) {
		/*
		 * Sensor calibration value not set by bootloader,
		 * default calibration data to be used
		 */
		ret = regmap_field_write(sensor->dcorrect,
					 sensor->cdata->calibration_val);
		if (ret)
			dev_err(dev, "failed to set calibration data\n");
	}

	return ret;
}

/* Callback to get temperature from HW*/
static int st_thermal_get_temp(struct thermal_zone_device *th,
		unsigned long *temperature)
{
	struct st_thermal_sensor *sensor = th->devdata;
	struct device *dev = sensor->dev;
	unsigned int temp;
	unsigned int overflow;
	int ret;

	ret = regmap_field_read(sensor->overflow, &overflow);
	if (ret)
		return ret;
	if (overflow)
		return -EIO;

	ret = regmap_field_read(sensor->temp_data, &temp);
	if (ret)
		return ret;

	temp += sensor->cdata->temp_adjust_val;
	temp = mcelsius(temp);

	dev_dbg(dev, "temperature: %d\n", temp);

	*temperature = temp;

	return 0;
}

static int st_thermal_get_trip_type(struct thermal_zone_device *th,
				int trip, enum thermal_trip_type *type)
{
	struct st_thermal_sensor *sensor = th->devdata;
	struct device *dev = sensor->dev;

	switch (trip) {
	case 0:
		*type = THERMAL_TRIP_CRITICAL;
		break;
	default:
		dev_err(dev, "invalid trip point\n");
		return -EINVAL;
	}

	return 0;
}

static int st_thermal_get_trip_temp(struct thermal_zone_device *th,
				    int trip, unsigned long *temp)
{
	struct st_thermal_sensor *sensor = th->devdata;
	struct device *dev = sensor->dev;

	switch (trip) {
	case 0:
		*temp = mcelsius(sensor->cdata->crit_temp);
		break;
	default:
		dev_err(dev, "Invalid trip point\n");
		return -EINVAL;
	}

	return 0;
}

static struct thermal_zone_device_ops st_tz_ops = {
	.get_temp	= st_thermal_get_temp,
	.get_trip_type	= st_thermal_get_trip_type,
	.get_trip_temp	= st_thermal_get_trip_temp,
};

int st_thermal_register(struct platform_device *pdev,
			const struct of_device_id *st_thermal_of_match)
{
	struct st_thermal_sensor *sensor;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct of_device_id *match;

	int polling_delay;
	int ret;

	if (!np) {
		dev_err(dev, "device tree node not found\n");
		return -EINVAL;
	}

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->dev = dev;

	match = of_match_device(st_thermal_of_match, dev);
	if (!(match && match->data))
		return -EINVAL;

	sensor->cdata = match->data;
	if (!sensor->cdata->ops)
		return -EINVAL;

	sensor->ops = sensor->cdata->ops;

	ret = sensor->ops->regmap_init(sensor);
	if (ret)
		return ret;

	ret = st_thermal_alloc_regfields(sensor);
	if (ret)
		return ret;

	sensor->clk = devm_clk_get(dev, "thermal");
	if (IS_ERR(sensor->clk)) {
		dev_err(dev, "failed to fetch clock\n");
		return PTR_ERR(sensor->clk);
	}

	if (sensor->ops->register_enable_irq) {
		ret = sensor->ops->register_enable_irq(sensor);
		if (ret)
			return ret;
	}

	ret = st_thermal_sensor_on(sensor);
	if (ret)
		return ret;

	ret = st_thermal_calibration(sensor);
	if (ret)
		goto sensor_off;

	polling_delay = sensor->ops->register_enable_irq ? 0 : 1000;

	sensor->thermal_dev =
		thermal_zone_device_register(dev_name(dev), 1, 0, sensor,
					     &st_tz_ops, NULL, 0, polling_delay);
	if (IS_ERR(sensor->thermal_dev)) {
		dev_err(dev, "failed to register thermal zone device\n");
		ret = PTR_ERR(sensor->thermal_dev);
		goto sensor_off;
	}

	platform_set_drvdata(pdev, sensor);

	return 0;

sensor_off:
	st_thermal_sensor_off(sensor);

	return ret;
}
EXPORT_SYMBOL_GPL(st_thermal_register);

int st_thermal_unregister(struct platform_device *pdev)
{
	struct st_thermal_sensor *sensor = platform_get_drvdata(pdev);

	st_thermal_sensor_off(sensor);
	thermal_zone_device_unregister(sensor->thermal_dev);

	return 0;
}
EXPORT_SYMBOL_GPL(st_thermal_unregister);

#ifdef CONFIG_PM_SLEEP
static int st_thermal_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct st_thermal_sensor *sensor = platform_get_drvdata(pdev);

	return st_thermal_sensor_off(sensor);
}

static int st_thermal_resume(struct device *dev)
{
	int ret;
	struct platform_device *pdev = to_platform_device(dev);
	struct st_thermal_sensor *sensor = platform_get_drvdata(pdev);

	ret = st_thermal_sensor_on(sensor);
	if (ret)
		return ret;

	ret = st_thermal_calibration(sensor);
	if (ret)
		return ret;

	if (sensor->ops->enable_irq) {
		ret = sensor->ops->enable_irq(sensor);
		if (ret)
			return ret;
	}

	return 0;
}
#endif

SIMPLE_DEV_PM_OPS(st_thermal_pm_ops, st_thermal_suspend, st_thermal_resume);
EXPORT_SYMBOL_GPL(st_thermal_pm_ops);

MODULE_AUTHOR("STMicroelectronics (R&D) Limited <ajitpal.singh@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STi SoC Thermal Sensor Driver");
MODULE_LICENSE("GPL v2");
