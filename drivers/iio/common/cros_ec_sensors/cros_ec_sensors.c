/*
 * cros_ec_sensors - Driver for Chrome OS Embedded Controller sensors.
 *
 * Copyright (C) 2016 Google, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This driver uses the cros-ec interface to communicate with the Chrome OS
 * EC about sensors data. Data access is presented through iio sysfs.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/kernel.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "cros_ec_sensors_core.h"

#define CROS_EC_SENSORS_MAX_CHANNELS 4

/* State data for ec_sensors iio driver. */
struct cros_ec_sensors_state {
	/* Shared by all sensors */
	struct cros_ec_sensors_core_state core;

	struct iio_chan_spec channels[CROS_EC_SENSORS_MAX_CHANNELS];
};

static int cros_ec_sensors_read(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan,
			  int *val, int *val2, long mask)
{
	struct cros_ec_sensors_state *st = iio_priv(indio_dev);
	s16 data = 0;
	s64 val64;
	int i;
	int ret;
	int idx = chan->scan_index;

	mutex_lock(&st->core.cmd_lock);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = st->core.read_ec_sensors_data(indio_dev, 1 << idx, &data);
		if (ret < 0)
			break;
		ret = IIO_VAL_INT;
		*val = data;
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		st->core.param.cmd = MOTIONSENSE_CMD_SENSOR_OFFSET;
		st->core.param.sensor_offset.flags = 0;

		ret = cros_ec_motion_send_host_cmd(&st->core, 0);
		if (ret < 0)
			break;

		/* Save values */
		for (i = CROS_EC_SENSOR_X; i < CROS_EC_SENSOR_MAX_AXIS; i++)
			st->core.calib[i] =
				st->core.resp->sensor_offset.offset[i];
		ret = IIO_VAL_INT;
		*val = st->core.calib[idx];
		break;
	case IIO_CHAN_INFO_SCALE:
		st->core.param.cmd = MOTIONSENSE_CMD_SENSOR_RANGE;
		st->core.param.sensor_range.data = EC_MOTION_SENSE_NO_VALUE;

		ret = cros_ec_motion_send_host_cmd(&st->core, 0);
		if (ret < 0)
			break;

		val64 = st->core.resp->sensor_range.ret;
		switch (st->core.type) {
		case MOTIONSENSE_TYPE_ACCEL:
			/*
			 * EC returns data in g, iio exepects m/s^2.
			 * Do not use IIO_G_TO_M_S_2 to avoid precision loss.
			 */
			*val = div_s64(val64 * 980665, 10);
			*val2 = 10000 << (CROS_EC_SENSOR_BITS - 1);
			ret = IIO_VAL_FRACTIONAL;
			break;
		case MOTIONSENSE_TYPE_GYRO:
			/*
			 * EC returns data in dps, iio expects rad/s.
			 * Do not use IIO_DEGREE_TO_RAD to avoid precision
			 * loss. Round to the nearest integer.
			 */
			*val = div_s64(val64 * 314159 + 9000000ULL, 1000);
			*val2 = 18000 << (CROS_EC_SENSOR_BITS - 1);
			ret = IIO_VAL_FRACTIONAL;
			break;
		case MOTIONSENSE_TYPE_MAG:
			/*
			 * EC returns data in 16LSB / uT,
			 * iio expects Gauss
			 */
			*val = val64;
			*val2 = 100 << (CROS_EC_SENSOR_BITS - 1);
			ret = IIO_VAL_FRACTIONAL;
			break;
		default:
			ret = -EINVAL;
		}
		break;
	default:
		ret = cros_ec_sensors_core_read(&st->core, chan, val, val2,
						mask);
		break;
	}
	mutex_unlock(&st->core.cmd_lock);

	return ret;
}

static int cros_ec_sensors_write(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	struct cros_ec_sensors_state *st = iio_priv(indio_dev);
	int i;
	int ret;
	int idx = chan->scan_index;

	mutex_lock(&st->core.cmd_lock);

	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		st->core.calib[idx] = val;

		/* Send to EC for each axis, even if not complete */
		st->core.param.cmd = MOTIONSENSE_CMD_SENSOR_OFFSET;
		st->core.param.sensor_offset.flags =
			MOTION_SENSE_SET_OFFSET;
		for (i = CROS_EC_SENSOR_X; i < CROS_EC_SENSOR_MAX_AXIS; i++)
			st->core.param.sensor_offset.offset[i] =
				st->core.calib[i];
		st->core.param.sensor_offset.temp =
			EC_MOTION_SENSE_INVALID_CALIB_TEMP;

		ret = cros_ec_motion_send_host_cmd(&st->core, 0);
		break;
	case IIO_CHAN_INFO_SCALE:
		if (st->core.type == MOTIONSENSE_TYPE_MAG) {
			ret = -EINVAL;
			break;
		}
		st->core.param.cmd = MOTIONSENSE_CMD_SENSOR_RANGE;
		st->core.param.sensor_range.data = val;

		/* Always roundup, so caller gets at least what it asks for. */
		st->core.param.sensor_range.roundup = 1;

		ret = cros_ec_motion_send_host_cmd(&st->core, 0);
		break;
	default:
		ret = cros_ec_sensors_core_write(
				&st->core, chan, val, val2, mask);
		break;
	}

	mutex_unlock(&st->core.cmd_lock);

	return ret;
}

static const struct iio_info ec_sensors_info = {
	.read_raw = &cros_ec_sensors_read,
	.write_raw = &cros_ec_sensors_write,
	.driver_module = THIS_MODULE,
};

static int cros_ec_sensors_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ec_dev = dev_get_drvdata(dev->parent);
	struct cros_ec_device *ec_device;
	struct iio_dev *indio_dev;
	struct cros_ec_sensors_state *state;
	struct iio_chan_spec *channel;
	int ret, i;

	if (!ec_dev || !ec_dev->ec_dev) {
		dev_warn(&pdev->dev, "No CROS EC device found.\n");
		return -EINVAL;
	}
	ec_device = ec_dev->ec_dev;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*state));
	if (!indio_dev)
		return -ENOMEM;

	ret = cros_ec_sensors_core_init(pdev, indio_dev, true);
	if (ret)
		return ret;

	indio_dev->info = &ec_sensors_info;
	state = iio_priv(indio_dev);
	for (channel = state->channels, i = CROS_EC_SENSOR_X;
	     i < CROS_EC_SENSOR_MAX_AXIS; i++, channel++) {
		/* Common part */
		channel->info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_CALIBBIAS);
		channel->info_mask_shared_by_all =
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_FREQUENCY) |
			BIT(IIO_CHAN_INFO_SAMP_FREQ);
		channel->scan_type.realbits = CROS_EC_SENSOR_BITS;
		channel->scan_type.storagebits = CROS_EC_SENSOR_BITS;
		channel->scan_index = i;
		channel->ext_info = cros_ec_sensors_ext_info;
		channel->modified = 1;
		channel->channel2 = IIO_MOD_X + i;
		channel->scan_type.sign = 's';

		/* Sensor specific */
		switch (state->core.type) {
		case MOTIONSENSE_TYPE_ACCEL:
			channel->type = IIO_ACCEL;
			break;
		case MOTIONSENSE_TYPE_GYRO:
			channel->type = IIO_ANGL_VEL;
			break;
		case MOTIONSENSE_TYPE_MAG:
			channel->type = IIO_MAGN;
			break;
		default:
			dev_err(&pdev->dev, "Unknown motion sensor\n");
			return -EINVAL;
		}
	}

	/* Timestamp */
	channel->type = IIO_TIMESTAMP;
	channel->channel = -1;
	channel->scan_index = CROS_EC_SENSOR_MAX_AXIS;
	channel->scan_type.sign = 's';
	channel->scan_type.realbits = 64;
	channel->scan_type.storagebits = 64;

	indio_dev->channels = state->channels;
	indio_dev->num_channels = CROS_EC_SENSORS_MAX_CHANNELS;

	/* There is only enough room for accel and gyro in the io space */
	if ((state->core.ec->cmd_readmem != NULL) &&
	    (state->core.type != MOTIONSENSE_TYPE_MAG))
		state->core.read_ec_sensors_data = cros_ec_sensors_read_lpc;
	else
		state->core.read_ec_sensors_data = cros_ec_sensors_read_cmd;

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 cros_ec_sensors_capture, NULL);
	if (ret)
		return ret;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_uninit_buffer;

	return 0;

error_uninit_buffer:
	iio_triggered_buffer_cleanup(indio_dev);

	return ret;
}

static int cros_ec_sensors_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);

	return 0;
}

static const struct platform_device_id cros_ec_sensors_ids[] = {
	{
		.name = "cros-ec-accel",
	},
	{
		.name = "cros-ec-gyro",
	},
	{
		.name = "cros-ec-mag",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, cros_ec_sensors_ids);

static struct platform_driver cros_ec_sensors_platform_driver = {
	.driver = {
		.name	= "cros-ec-sensors",
	},
	.probe		= cros_ec_sensors_probe,
	.remove		= cros_ec_sensors_remove,
	.id_table	= cros_ec_sensors_ids,
};
module_platform_driver(cros_ec_sensors_platform_driver);

MODULE_DESCRIPTION("ChromeOS EC 3-axis sensors driver");
MODULE_LICENSE("GPL v2");
