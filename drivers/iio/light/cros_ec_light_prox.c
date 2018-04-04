/*
 * cros_ec_light_prox - Driver for light and prox sensors behing CrosEC.
 *
 * Copyright (C) 2017 Google, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/kernel.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "../common/cros_ec_sensors/cros_ec_sensors_core.h"

/*
 * We only represent one entry for light or proximity. EC is merging different
 * light sensors to return the what the eye would see. For proximity, we
 * currently support only one light source.
 */
#define CROS_EC_LIGHT_PROX_MAX_CHANNELS (1 + 1)

/* State data for ec_sensors iio driver. */
struct cros_ec_light_prox_state {
	/* Shared by all sensors */
	struct cros_ec_sensors_core_state core;

	struct iio_chan_spec channels[CROS_EC_LIGHT_PROX_MAX_CHANNELS];
};

static int cros_ec_light_prox_read(struct iio_dev *indio_dev,
				   struct iio_chan_spec const *chan,
				   int *val, int *val2, long mask)
{
	struct cros_ec_light_prox_state *st = iio_priv(indio_dev);
	u16 data = 0;
	s64 val64;
	int ret = IIO_VAL_INT;
	int idx = chan->scan_index;

	mutex_lock(&st->core.cmd_lock);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type == IIO_PROXIMITY) {
			if (cros_ec_sensors_read_cmd(indio_dev, 1 << idx,
						     (s16 *)&data) < 0) {
				ret = -EIO;
				break;
			}
			*val = data;
		} else {
			ret = -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_PROCESSED:
		if (chan->type == IIO_LIGHT) {
			if (cros_ec_sensors_read_cmd(indio_dev, 1 << idx,
						     (s16 *)&data) < 0) {
				ret = -EIO;
				break;
			}
			/*
			 * The data coming from the light sensor is
			 * pre-processed and represents the ambient light
			 * illuminance reading expressed in lux.
			 */
			*val = data;
			ret = IIO_VAL_INT;
		} else {
			ret = -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		st->core.param.cmd = MOTIONSENSE_CMD_SENSOR_OFFSET;
		st->core.param.sensor_offset.flags = 0;

		if (cros_ec_motion_send_host_cmd(&st->core, 0)) {
			ret = -EIO;
			break;
		}

		/* Save values */
		st->core.calib[0] = st->core.resp->sensor_offset.offset[0];

		*val = st->core.calib[idx];
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		/*
		 * RANGE is used for calibration
		 * scale is a number x.y, where x is coded on 16 bits,
		 * y coded on 16 bits, between 0 and 9999.
		 */
		st->core.param.cmd = MOTIONSENSE_CMD_SENSOR_RANGE;
		st->core.param.sensor_range.data = EC_MOTION_SENSE_NO_VALUE;

		if (cros_ec_motion_send_host_cmd(&st->core, 0)) {
			ret = -EIO;
			break;
		}

		val64 = st->core.resp->sensor_range.ret;
		*val = val64 >> 16;
		*val2 = (val64 & 0xffff) * 100;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	default:
		ret = cros_ec_sensors_core_read(&st->core, chan, val, val2,
						mask);
		break;
	}

	mutex_unlock(&st->core.cmd_lock);

	return ret;
}

static int cros_ec_light_prox_write(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	struct cros_ec_light_prox_state *st = iio_priv(indio_dev);
	int ret = 0;
	int idx = chan->scan_index;

	mutex_lock(&st->core.cmd_lock);

	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		st->core.calib[idx] = val;
		/* Send to EC for each axis, even if not complete */
		st->core.param.cmd = MOTIONSENSE_CMD_SENSOR_OFFSET;
		st->core.param.sensor_offset.flags = MOTION_SENSE_SET_OFFSET;
		st->core.param.sensor_offset.offset[0] = st->core.calib[0];
		st->core.param.sensor_offset.temp =
					EC_MOTION_SENSE_INVALID_CALIB_TEMP;
		if (cros_ec_motion_send_host_cmd(&st->core, 0))
			ret = -EIO;
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		st->core.param.cmd = MOTIONSENSE_CMD_SENSOR_RANGE;
		st->core.param.sensor_range.data = (val << 16) | (val2 / 100);
		if (cros_ec_motion_send_host_cmd(&st->core, 0))
			ret = -EIO;
		break;
	default:
		ret = cros_ec_sensors_core_write(&st->core, chan, val, val2,
						 mask);
		break;
	}

	mutex_unlock(&st->core.cmd_lock);

	return ret;
}

static const struct iio_info cros_ec_light_prox_info = {
	.read_raw = &cros_ec_light_prox_read,
	.write_raw = &cros_ec_light_prox_write,
};

static int cros_ec_light_prox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ec_dev = dev_get_drvdata(dev->parent);
	struct iio_dev *indio_dev;
	struct cros_ec_light_prox_state *state;
	struct iio_chan_spec *channel;
	int ret;

	if (!ec_dev || !ec_dev->ec_dev) {
		dev_warn(dev, "No CROS EC device found.\n");
		return -EINVAL;
	}

	indio_dev = devm_iio_device_alloc(dev, sizeof(*state));
	if (!indio_dev)
		return -ENOMEM;

	ret = cros_ec_sensors_core_init(pdev, indio_dev, true);
	if (ret)
		return ret;

	indio_dev->info = &cros_ec_light_prox_info;
	state = iio_priv(indio_dev);
	state->core.type = state->core.resp->info.type;
	state->core.loc = state->core.resp->info.location;
	channel = state->channels;

	/* Common part */
	channel->info_mask_shared_by_all =
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_FREQUENCY);
	channel->scan_type.realbits = CROS_EC_SENSOR_BITS;
	channel->scan_type.storagebits = CROS_EC_SENSOR_BITS;
	channel->scan_type.shift = 0;
	channel->scan_index = 0;
	channel->ext_info = cros_ec_sensors_ext_info;
	channel->scan_type.sign = 'u';

	state->core.calib[0] = 0;

	/* Sensor specific */
	switch (state->core.type) {
	case MOTIONSENSE_TYPE_LIGHT:
		channel->type = IIO_LIGHT;
		channel->info_mask_separate =
			BIT(IIO_CHAN_INFO_PROCESSED) |
			BIT(IIO_CHAN_INFO_CALIBBIAS) |
			BIT(IIO_CHAN_INFO_CALIBSCALE);
		break;
	case MOTIONSENSE_TYPE_PROX:
		channel->type = IIO_PROXIMITY;
		channel->info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_CALIBBIAS) |
			BIT(IIO_CHAN_INFO_CALIBSCALE);
		break;
	default:
		dev_warn(dev, "Unknown motion sensor\n");
		return -EINVAL;
	}

	/* Timestamp */
	channel++;
	channel->type = IIO_TIMESTAMP;
	channel->channel = -1;
	channel->scan_index = 1;
	channel->scan_type.sign = 's';
	channel->scan_type.realbits = 64;
	channel->scan_type.storagebits = 64;

	indio_dev->channels = state->channels;

	indio_dev->num_channels = CROS_EC_LIGHT_PROX_MAX_CHANNELS;

	state->core.read_ec_sensors_data = cros_ec_sensors_read_cmd;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      cros_ec_sensors_capture, NULL);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct platform_device_id cros_ec_light_prox_ids[] = {
	{
		.name = "cros-ec-prox",
	},
	{
		.name = "cros-ec-light",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, cros_ec_light_prox_ids);

static struct platform_driver cros_ec_light_prox_platform_driver = {
	.driver = {
		.name	= "cros-ec-light-prox",
	},
	.probe		= cros_ec_light_prox_probe,
	.id_table	= cros_ec_light_prox_ids,
};
module_platform_driver(cros_ec_light_prox_platform_driver);

MODULE_DESCRIPTION("ChromeOS EC light/proximity sensors driver");
MODULE_LICENSE("GPL v2");
