// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ST Thermal Sensor Driver core routines
 * Author: Ajit Pal Singh <ajitpal.singh@st.com>
 *
 * Copyright (C) 2003-2014 STMicroelectronics (R&D) Limited
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "st_thermal.h"
#include "../thermal_hwmon.h"

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
static int st_thermal_get_temp(struct thermal_zone_device *th, int *temperature)
{
	struct st_thermal_sensor *sensor = thermal_zone_device_priv(th);
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

	*temperature = temp;

	return 0;
}

static const struct thermal_zone_device_ops st_tz_ops = {
	.get_temp	= st_thermal_get_temp,
};

int st_thermal_register(struct platform_device *pdev,
			const struct of_device_id *st_thermal_of_match)
{
	struct st_thermal_sensor *sensor;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct of_device_id *match;

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

	ret = (sensor->ops->regmap_init)(sensor);
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

	sensor->thermal_dev =
		devm_thermal_of_zone_register(dev, 0, sensor, &st_tz_ops);
	if (IS_ERR(sensor->thermal_dev)) {
		dev_err(dev, "failed to register thermal of zone\n");
		ret = PTR_ERR(sensor->thermal_dev);
		goto sensor_off;
	}

	platform_set_drvdata(pdev, sensor);

	/*
	 * devm_thermal_of_zone_register() doesn't enable hwmon by default
	 * Enable it here
	 */
	devm_thermal_add_hwmon_sysfs(dev, sensor->thermal_dev);

	return 0;

sensor_off:
	st_thermal_sensor_off(sensor);

	return ret;
}
EXPORT_SYMBOL_GPL(st_thermal_register);

void st_thermal_unregister(struct platform_device *pdev)
{
	struct st_thermal_sensor *sensor = platform_get_drvdata(pdev);

	st_thermal_sensor_off(sensor);
	thermal_remove_hwmon_sysfs(sensor->thermal_dev);
	devm_thermal_of_zone_unregister(sensor->dev, sensor->thermal_dev);
}
EXPORT_SYMBOL_GPL(st_thermal_unregister);

static int st_thermal_suspend(struct device *dev)
{
	struct st_thermal_sensor *sensor = dev_get_drvdata(dev);

	return st_thermal_sensor_off(sensor);
}

static int st_thermal_resume(struct device *dev)
{
	int ret;
	struct st_thermal_sensor *sensor = dev_get_drvdata(dev);

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

DEFINE_SIMPLE_DEV_PM_OPS(st_thermal_pm_ops, st_thermal_suspend, st_thermal_resume);
EXPORT_SYMBOL_GPL(st_thermal_pm_ops);

MODULE_AUTHOR("STMicroelectronics (R&D) Limited <ajitpal.singh@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STi SoC Thermal Sensor Driver");
MODULE_LICENSE("GPL v2");
