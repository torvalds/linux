/*
 * STMicroelectronics accelerometers driver
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
#include <linux/iio/trigger.h>

#include <linux/iio/common/st_sensors.h>
#include <linux/iio/common/st_sensors_i2c.h>
#include "st_accel.h"

static int st_accel_i2c_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct st_sensor_data *adata;
	int err;

	indio_dev = iio_device_alloc(sizeof(*adata));
	if (indio_dev == NULL) {
		err = -ENOMEM;
		goto iio_device_alloc_error;
	}

	adata = iio_priv(indio_dev);
	adata->dev = &client->dev;

	st_sensors_i2c_configure(indio_dev, client, adata);

	err = st_accel_common_probe(indio_dev);
	if (err < 0)
		goto st_accel_common_probe_error;

	return 0;

st_accel_common_probe_error:
	iio_device_free(indio_dev);
iio_device_alloc_error:
	return err;
}

static int st_accel_i2c_remove(struct i2c_client *client)
{
	st_accel_common_remove(i2c_get_clientdata(client));

	return 0;
}

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
	{},
};
MODULE_DEVICE_TABLE(i2c, st_accel_id_table);

static struct i2c_driver st_accel_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "st-accel-i2c",
	},
	.probe = st_accel_i2c_probe,
	.remove = st_accel_i2c_remove,
	.id_table = st_accel_id_table,
};
module_i2c_driver(st_accel_driver);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics accelerometers i2c driver");
MODULE_LICENSE("GPL v2");
