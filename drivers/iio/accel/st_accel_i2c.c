// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics accelerometers driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/property.h>

#include <linux/iio/common/st_sensors_i2c.h>
#include "st_accel.h"

static const struct of_device_id st_accel_of_match[] = {
	{
		/* An older compatible */
		.compatible = "st,lis3lv02d",
		.data = LIS3LV02DL_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis3lv02dl-accel",
		.data = LIS3LV02DL_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm303dlh-accel",
		.data = LSM303DLH_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm303dlhc-accel",
		.data = LSM303DLHC_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis3dh-accel",
		.data = LIS3DH_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm330d-accel",
		.data = LSM330D_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm330dl-accel",
		.data = LSM330DL_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm330dlc-accel",
		.data = LSM330DLC_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis331dl-accel",
		.data = LIS331DL_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis331dlh-accel",
		.data = LIS331DLH_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm303dl-accel",
		.data = LSM303DL_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm303dlm-accel",
		.data = LSM303DLM_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm330-accel",
		.data = LSM330_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm303agr-accel",
		.data = LSM303AGR_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis2dh12-accel",
		.data = LIS2DH12_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,h3lis331dl-accel",
		.data = H3LIS331DL_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis3l02dq",
		.data = LIS3L02DQ_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lng2dm-accel",
		.data = LNG2DM_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis2dw12",
		.data = LIS2DW12_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis3de",
		.data = LIS3DE_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis2de12",
		.data = LIS2DE12_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis2hh12",
		.data = LIS2HH12_ACCEL_DEV_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_accel_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id st_accel_acpi_match[] = {
	{"SMO8840", (kernel_ulong_t)LIS2DH12_ACCEL_DEV_NAME},
	{"SMO8A90", (kernel_ulong_t)LNG2DM_ACCEL_DEV_NAME},
	{ },
};
MODULE_DEVICE_TABLE(acpi, st_accel_acpi_match);
#endif

static const struct i2c_device_id st_accel_id_table[] = {
	{ LSM303DLH_ACCEL_DEV_NAME },
	{ LSM303DLHC_ACCEL_DEV_NAME },
	{ LIS3DH_ACCEL_DEV_NAME },
	{ LSM330D_ACCEL_DEV_NAME },
	{ LSM330DL_ACCEL_DEV_NAME },
	{ LSM330DLC_ACCEL_DEV_NAME },
	{ LIS331DLH_ACCEL_DEV_NAME },
	{ LSM303DL_ACCEL_DEV_NAME },
	{ LSM303DLM_ACCEL_DEV_NAME },
	{ LSM330_ACCEL_DEV_NAME },
	{ LSM303AGR_ACCEL_DEV_NAME },
	{ LIS2DH12_ACCEL_DEV_NAME },
	{ LIS3L02DQ_ACCEL_DEV_NAME },
	{ LNG2DM_ACCEL_DEV_NAME },
	{ H3LIS331DL_ACCEL_DEV_NAME },
	{ LIS331DL_ACCEL_DEV_NAME },
	{ LIS3LV02DL_ACCEL_DEV_NAME },
	{ LIS2DW12_ACCEL_DEV_NAME },
	{ LIS3DE_ACCEL_DEV_NAME },
	{ LIS2DE12_ACCEL_DEV_NAME },
	{ LIS2HH12_ACCEL_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_accel_id_table);

static int st_accel_i2c_probe(struct i2c_client *client)
{
	const struct st_sensor_settings *settings;
	struct st_sensor_data *adata;
	struct iio_dev *indio_dev;
	int ret;

	st_sensors_dev_name_probe(&client->dev, client->name, sizeof(client->name));

	settings = st_accel_get_settings(client->name);
	if (!settings) {
		dev_err(&client->dev, "device name %s not recognized.\n",
			client->name);
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*adata));
	if (!indio_dev)
		return -ENOMEM;

	adata = iio_priv(indio_dev);
	adata->sensor_settings = (struct st_sensor_settings *)settings;

	ret = st_sensors_i2c_configure(indio_dev, client);
	if (ret < 0)
		return ret;

	ret = st_sensors_power_enable(indio_dev);
	if (ret)
		return ret;

	ret = st_accel_common_probe(indio_dev);
	if (ret < 0)
		goto st_accel_power_off;

	return 0;

st_accel_power_off:
	st_sensors_power_disable(indio_dev);

	return ret;
}

static int st_accel_i2c_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	st_sensors_power_disable(indio_dev);

	st_accel_common_remove(indio_dev);

	return 0;
}

static struct i2c_driver st_accel_driver = {
	.driver = {
		.name = "st-accel-i2c",
		.of_match_table = st_accel_of_match,
		.acpi_match_table = ACPI_PTR(st_accel_acpi_match),
	},
	.probe_new = st_accel_i2c_probe,
	.remove = st_accel_i2c_remove,
	.id_table = st_accel_id_table,
};
module_i2c_driver(st_accel_driver);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics accelerometers i2c driver");
MODULE_LICENSE("GPL v2");
