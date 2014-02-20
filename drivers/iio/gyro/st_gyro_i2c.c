/*
 * STMicroelectronics gyroscopes driver
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
#include "st_gyro.h"

static int st_gyro_i2c_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct st_sensor_data *gdata;
	int err;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*gdata));
	if (!indio_dev)
		return -ENOMEM;

	gdata = iio_priv(indio_dev);
	gdata->dev = &client->dev;

	st_sensors_i2c_configure(indio_dev, client, gdata);

	err = st_gyro_common_probe(indio_dev,
				(struct st_sensors_platform_data *)&gyro_pdata);
	if (err < 0)
		return err;

	return 0;
}

static int st_gyro_i2c_remove(struct i2c_client *client)
{
	st_gyro_common_remove(i2c_get_clientdata(client));

	return 0;
}

static const struct i2c_device_id st_gyro_id_table[] = {
	{ L3G4200D_GYRO_DEV_NAME },
	{ LSM330D_GYRO_DEV_NAME },
	{ LSM330DL_GYRO_DEV_NAME },
	{ LSM330DLC_GYRO_DEV_NAME },
	{ L3GD20_GYRO_DEV_NAME },
	{ L3G4IS_GYRO_DEV_NAME },
	{ LSM330_GYRO_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_gyro_id_table);

static struct i2c_driver st_gyro_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "st-gyro-i2c",
	},
	.probe = st_gyro_i2c_probe,
	.remove = st_gyro_i2c_remove,
	.id_table = st_gyro_id_table,
};
module_i2c_driver(st_gyro_driver);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics gyroscopes i2c driver");
MODULE_LICENSE("GPL v2");
