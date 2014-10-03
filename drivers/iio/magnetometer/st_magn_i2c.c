/*
 * STMicroelectronics magnetometers driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>

#include <linux/iio/common/st_sensors.h>
#include <linux/iio/common/st_sensors_i2c.h>
#include "st_magn.h"

#ifdef CONFIG_OF
static const struct of_device_id st_magn_of_match[] = {
	{
		.compatible = "st,lsm303dlhc-magn",
		.data = LSM303DLHC_MAGN_DEV_NAME,
	},
	{
		.compatible = "st,lsm303dlm-magn",
		.data = LSM303DLM_MAGN_DEV_NAME,
	},
	{
		.compatible = "st,lis3mdl-magn",
		.data = LIS3MDL_MAGN_DEV_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_magn_of_match);
#else
#define st_magn_of_match NULL
#endif

static int st_magn_i2c_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct st_sensor_data *mdata;
	int err;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*mdata));
	if (!indio_dev)
		return -ENOMEM;

	mdata = iio_priv(indio_dev);
	mdata->dev = &client->dev;
	st_sensors_of_i2c_probe(client, st_magn_of_match);

	st_sensors_i2c_configure(indio_dev, client, mdata);

	err = st_magn_common_probe(indio_dev);
	if (err < 0)
		return err;

	return 0;
}

static int st_magn_i2c_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	st_magn_common_remove(indio_dev);

	return 0;
}

static const struct i2c_device_id st_magn_id_table[] = {
	{ LSM303DLHC_MAGN_DEV_NAME },
	{ LSM303DLM_MAGN_DEV_NAME },
	{ LIS3MDL_MAGN_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_magn_id_table);

static struct i2c_driver st_magn_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "st-magn-i2c",
		.of_match_table = of_match_ptr(st_magn_of_match),
	},
	.probe = st_magn_i2c_probe,
	.remove = st_magn_i2c_remove,
	.id_table = st_magn_id_table,
};
module_i2c_driver(st_magn_driver);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics magnetometers i2c driver");
MODULE_LICENSE("GPL v2");
