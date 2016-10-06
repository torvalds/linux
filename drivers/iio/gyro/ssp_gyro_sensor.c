/*
 *  Copyright (C) 2014, Samsung Electronics Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include <linux/iio/common/ssp_sensors.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "../common/ssp_sensors/ssp_iio_sensor.h"

#define SSP_CHANNEL_COUNT 3

#define SSP_GYROSCOPE_NAME "ssp-gyroscope"
static const char ssp_gyro_name[] = SSP_GYROSCOPE_NAME;

enum ssp_gyro_3d_channel {
	SSP_CHANNEL_SCAN_INDEX_X,
	SSP_CHANNEL_SCAN_INDEX_Y,
	SSP_CHANNEL_SCAN_INDEX_Z,
	SSP_CHANNEL_SCAN_INDEX_TIME,
};

static int ssp_gyro_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int *val,
			     int *val2, long mask)
{
	u32 t;
	struct ssp_data *data = dev_get_drvdata(indio_dev->dev.parent->parent);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		t = ssp_get_sensor_delay(data, SSP_GYROSCOPE_SENSOR);
		ssp_convert_to_freq(t, val, val2);
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		break;
	}

	return -EINVAL;
}

static int ssp_gyro_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan, int val,
			      int val2, long mask)
{
	int ret;
	struct ssp_data *data = dev_get_drvdata(indio_dev->dev.parent->parent);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = ssp_convert_to_time(val, val2);
		ret = ssp_change_delay(data, SSP_GYROSCOPE_SENSOR, ret);
		if (ret < 0)
			dev_err(&indio_dev->dev, "gyro sensor enable fail\n");

		return ret;
	default:
		break;
	}

	return -EINVAL;
}

static const struct iio_info ssp_gyro_iio_info = {
	.read_raw = &ssp_gyro_read_raw,
	.write_raw = &ssp_gyro_write_raw,
};

static const unsigned long ssp_gyro_scan_mask[] = { 0x07, 0, };

static const struct iio_chan_spec ssp_gyro_channels[] = {
	SSP_CHANNEL_AG(IIO_ANGL_VEL, IIO_MOD_X, SSP_CHANNEL_SCAN_INDEX_X),
	SSP_CHANNEL_AG(IIO_ANGL_VEL, IIO_MOD_Y, SSP_CHANNEL_SCAN_INDEX_Y),
	SSP_CHANNEL_AG(IIO_ANGL_VEL, IIO_MOD_Z, SSP_CHANNEL_SCAN_INDEX_Z),
	SSP_CHAN_TIMESTAMP(SSP_CHANNEL_SCAN_INDEX_TIME),
};

static int ssp_process_gyro_data(struct iio_dev *indio_dev, void *buf,
				 int64_t timestamp)
{
	return ssp_common_process_data(indio_dev, buf, SSP_GYROSCOPE_SIZE,
				       timestamp);
}

static const struct iio_buffer_setup_ops ssp_gyro_buffer_ops = {
	.postenable = &ssp_common_buffer_postenable,
	.postdisable = &ssp_common_buffer_postdisable,
};

static int ssp_gyro_probe(struct platform_device *pdev)
{
	int ret;
	struct iio_dev *indio_dev;
	struct ssp_sensor_data *spd;
	struct iio_buffer *buffer;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*spd));
	if (!indio_dev)
		return -ENOMEM;

	spd = iio_priv(indio_dev);

	spd->process_data = ssp_process_gyro_data;
	spd->type = SSP_GYROSCOPE_SENSOR;

	indio_dev->name = ssp_gyro_name;
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->info = &ssp_gyro_iio_info;
	indio_dev->modes = INDIO_BUFFER_SOFTWARE;
	indio_dev->channels = ssp_gyro_channels;
	indio_dev->num_channels = ARRAY_SIZE(ssp_gyro_channels);
	indio_dev->available_scan_masks = ssp_gyro_scan_mask;

	buffer = devm_iio_kfifo_allocate(&pdev->dev);
	if (!buffer)
		return -ENOMEM;

	iio_device_attach_buffer(indio_dev, buffer);

	indio_dev->setup_ops = &ssp_gyro_buffer_ops;

	platform_set_drvdata(pdev, indio_dev);

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		return ret;

	/* ssp registering should be done after all iio setup */
	ssp_register_consumer(indio_dev, SSP_GYROSCOPE_SENSOR);

	return 0;
}

static int ssp_gyro_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);

	iio_device_unregister(indio_dev);

	return 0;
}

static struct platform_driver ssp_gyro_driver = {
	.driver = {
		.name = SSP_GYROSCOPE_NAME,
	},
	.probe = ssp_gyro_probe,
	.remove = ssp_gyro_remove,
};

module_platform_driver(ssp_gyro_driver);

MODULE_AUTHOR("Karol Wrona <k.wrona@samsung.com>");
MODULE_DESCRIPTION("Samsung sensorhub gyroscopes driver");
MODULE_LICENSE("GPL");
