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
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>

#include <linux/iio/common/st_sensors.h>
#include <linux/iio/common/st_sensors_spi.h>
#include "st_magn.h"

static int st_magn_spi_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct st_sensor_data *mdata;
	int err;

	indio_dev = iio_device_alloc(sizeof(*mdata));
	if (indio_dev == NULL) {
		err = -ENOMEM;
		goto iio_device_alloc_error;
	}

	mdata = iio_priv(indio_dev);
	mdata->dev = &spi->dev;

	st_sensors_spi_configure(indio_dev, spi, mdata);

	err = st_magn_common_probe(indio_dev);
	if (err < 0)
		goto st_magn_common_probe_error;

	return 0;

st_magn_common_probe_error:
	iio_device_free(indio_dev);
iio_device_alloc_error:
	return err;
}

static int st_magn_spi_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	st_magn_common_remove(indio_dev);

	return 0;
}

static const struct spi_device_id st_magn_id_table[] = {
	{ LSM303DLHC_MAGN_DEV_NAME },
	{ LSM303DLM_MAGN_DEV_NAME },
	{ LIS3MDL_MAGN_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(spi, st_magn_id_table);

static struct spi_driver st_magn_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "st-magn-spi",
	},
	.probe = st_magn_spi_probe,
	.remove = st_magn_spi_remove,
	.id_table = st_magn_id_table,
};
module_spi_driver(st_magn_driver);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics magnetometers spi driver");
MODULE_LICENSE("GPL v2");
