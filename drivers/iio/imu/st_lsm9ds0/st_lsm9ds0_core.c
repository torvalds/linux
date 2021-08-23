// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics LSM9DS0 IMU driver
 *
 * Copyright (C) 2021, Intel Corporation
 *
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/common/st_sensors.h>
#include <linux/iio/iio.h>

#include "st_lsm9ds0.h"

static int st_lsm9ds0_power_enable(struct device *dev, struct st_lsm9ds0 *lsm9ds0)
{
	int ret;

	/* Regulators not mandatory, but if requested we should enable them. */
	lsm9ds0->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(lsm9ds0->vdd)) {
		dev_err(dev, "unable to get Vdd supply\n");
		return PTR_ERR(lsm9ds0->vdd);
	}
	ret = regulator_enable(lsm9ds0->vdd);
	if (ret) {
		dev_warn(dev, "Failed to enable specified Vdd supply\n");
		return ret;
	}

	lsm9ds0->vdd_io = devm_regulator_get(dev, "vddio");
	if (IS_ERR(lsm9ds0->vdd_io)) {
		dev_err(dev, "unable to get Vdd_IO supply\n");
		regulator_disable(lsm9ds0->vdd);
		return PTR_ERR(lsm9ds0->vdd_io);
	}
	ret = regulator_enable(lsm9ds0->vdd_io);
	if (ret) {
		dev_warn(dev, "Failed to enable specified Vdd_IO supply\n");
		regulator_disable(lsm9ds0->vdd);
		return ret;
	}

	return 0;
}

static void st_lsm9ds0_power_disable(void *data)
{
	struct st_lsm9ds0 *lsm9ds0 = data;

	regulator_disable(lsm9ds0->vdd_io);
	regulator_disable(lsm9ds0->vdd);
}

static int devm_st_lsm9ds0_power_enable(struct st_lsm9ds0 *lsm9ds0)
{
	struct device *dev = lsm9ds0->dev;
	int ret;

	ret = st_lsm9ds0_power_enable(dev, lsm9ds0);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, st_lsm9ds0_power_disable, lsm9ds0);
}

static int st_lsm9ds0_probe_accel(struct st_lsm9ds0 *lsm9ds0, struct regmap *regmap)
{
	const struct st_sensor_settings *settings;
	struct device *dev = lsm9ds0->dev;
	struct st_sensor_data *data;

	settings = st_accel_get_settings(lsm9ds0->name);
	if (!settings) {
		dev_err(dev, "device name %s not recognized.\n", lsm9ds0->name);
		return -ENODEV;
	}

	lsm9ds0->accel = devm_iio_device_alloc(dev, sizeof(*data));
	if (!lsm9ds0->accel)
		return -ENOMEM;

	lsm9ds0->accel->name = lsm9ds0->name;

	data = iio_priv(lsm9ds0->accel);
	data->sensor_settings = (struct st_sensor_settings *)settings;
	data->dev = dev;
	data->irq = lsm9ds0->irq;
	data->regmap = regmap;
	data->vdd = lsm9ds0->vdd;
	data->vdd_io = lsm9ds0->vdd_io;

	return st_accel_common_probe(lsm9ds0->accel);
}

static int st_lsm9ds0_probe_magn(struct st_lsm9ds0 *lsm9ds0, struct regmap *regmap)
{
	const struct st_sensor_settings *settings;
	struct device *dev = lsm9ds0->dev;
	struct st_sensor_data *data;

	settings = st_magn_get_settings(lsm9ds0->name);
	if (!settings) {
		dev_err(dev, "device name %s not recognized.\n", lsm9ds0->name);
		return -ENODEV;
	}

	lsm9ds0->magn = devm_iio_device_alloc(dev, sizeof(*data));
	if (!lsm9ds0->magn)
		return -ENOMEM;

	lsm9ds0->magn->name = lsm9ds0->name;

	data = iio_priv(lsm9ds0->magn);
	data->sensor_settings = (struct st_sensor_settings *)settings;
	data->dev = dev;
	data->irq = lsm9ds0->irq;
	data->regmap = regmap;
	data->vdd = lsm9ds0->vdd;
	data->vdd_io = lsm9ds0->vdd_io;

	return st_magn_common_probe(lsm9ds0->magn);
}

int st_lsm9ds0_probe(struct st_lsm9ds0 *lsm9ds0, struct regmap *regmap)
{
	int ret;

	ret = devm_st_lsm9ds0_power_enable(lsm9ds0);
	if (ret)
		return ret;

	/* Setup accelerometer device */
	ret = st_lsm9ds0_probe_accel(lsm9ds0, regmap);
	if (ret)
		return ret;

	/* Setup magnetometer device */
	return st_lsm9ds0_probe_magn(lsm9ds0, regmap);
}
EXPORT_SYMBOL_GPL(st_lsm9ds0_probe);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_DESCRIPTION("STMicroelectronics LSM9DS0 IMU core driver");
MODULE_LICENSE("GPL v2");
