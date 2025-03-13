// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics LSM9DS0 IMU driver
 *
 * Copyright (C) 2021, Intel Corporation
 *
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/array_size.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/common/st_sensors.h>
#include <linux/iio/iio.h>

#include "st_lsm9ds0.h"

static int st_lsm9ds0_probe_accel(struct st_lsm9ds0 *lsm9ds0, struct regmap *regmap)
{
	const struct st_sensor_settings *settings;
	struct device *dev = lsm9ds0->dev;
	struct st_sensor_data *data;

	settings = st_accel_get_settings(lsm9ds0->name);
	if (!settings)
		return dev_err_probe(dev, -ENODEV, "device name %s not recognized.\n",
				     lsm9ds0->name);

	lsm9ds0->accel = devm_iio_device_alloc(dev, sizeof(*data));
	if (!lsm9ds0->accel)
		return -ENOMEM;

	lsm9ds0->accel->name = lsm9ds0->name;

	data = iio_priv(lsm9ds0->accel);
	data->sensor_settings = (struct st_sensor_settings *)settings;
	data->irq = lsm9ds0->irq;
	data->regmap = regmap;

	return st_accel_common_probe(lsm9ds0->accel);
}

static int st_lsm9ds0_probe_magn(struct st_lsm9ds0 *lsm9ds0, struct regmap *regmap)
{
	const struct st_sensor_settings *settings;
	struct device *dev = lsm9ds0->dev;
	struct st_sensor_data *data;

	settings = st_magn_get_settings(lsm9ds0->name);
	if (!settings)
		return dev_err_probe(dev, -ENODEV, "device name %s not recognized.\n",
				     lsm9ds0->name);

	lsm9ds0->magn = devm_iio_device_alloc(dev, sizeof(*data));
	if (!lsm9ds0->magn)
		return -ENOMEM;

	lsm9ds0->magn->name = lsm9ds0->name;

	data = iio_priv(lsm9ds0->magn);
	data->sensor_settings = (struct st_sensor_settings *)settings;
	data->irq = lsm9ds0->irq;
	data->regmap = regmap;

	return st_magn_common_probe(lsm9ds0->magn);
}

int st_lsm9ds0_probe(struct st_lsm9ds0 *lsm9ds0, struct regmap *regmap)
{
	struct device *dev = lsm9ds0->dev;
	static const char * const regulator_names[] = { "vdd", "vddio" };
	int ret;

	/* Regulators not mandatory, but if requested we should enable them. */
	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(regulator_names),
					     regulator_names);
	if (ret)
		return dev_err_probe(dev, ret, "unable to enable Vdd supply\n");

	/* Setup accelerometer device */
	ret = st_lsm9ds0_probe_accel(lsm9ds0, regmap);
	if (ret)
		return ret;

	/* Setup magnetometer device */
	return st_lsm9ds0_probe_magn(lsm9ds0, regmap);
}
EXPORT_SYMBOL_NS_GPL(st_lsm9ds0_probe, "IIO_ST_SENSORS");

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_DESCRIPTION("STMicroelectronics LSM9DS0 IMU core driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_ST_SENSORS");
