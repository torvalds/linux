// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for older Chrome OS EC accelerometer
 *
 * Copyright 2017 Google, Inc
 *
 * This driver uses the memory mapper cros-ec interface to communicate
 * with the Chrome OS EC about accelerometer data or older commands.
 * Accelerometer access is presented through iio sysfs.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iio/buffer.h>
#include <linux/iio/common/cros_ec_sensors_core.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/kernel.h>
#include <linux/mfd/cros_ec.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>

#define DRV_NAME	"cros-ec-accel-legacy"

#define CROS_EC_SENSOR_LEGACY_NUM 2
/*
 * Sensor scale hard coded at 10 bits per g, computed as:
 * g / (2^10 - 1) = 0.009586168; with g = 9.80665 m.s^-2
 */
#define ACCEL_LEGACY_NSCALE 9586168

static int cros_ec_accel_legacy_read_cmd(struct iio_dev *indio_dev,
				  unsigned long scan_mask, s16 *data)
{
	struct cros_ec_sensors_core_state *st = iio_priv(indio_dev);
	int ret;
	unsigned int i;
	u8 sensor_num;

	/*
	 * Read all sensor data through a command.
	 * Save sensor_num, it is assumed to stay.
	 */
	sensor_num = st->param.info.sensor_num;
	st->param.cmd = MOTIONSENSE_CMD_DUMP;
	st->param.dump.max_sensor_count = CROS_EC_SENSOR_LEGACY_NUM;
	ret = cros_ec_motion_send_host_cmd(st,
			sizeof(st->resp->dump) + CROS_EC_SENSOR_LEGACY_NUM *
			sizeof(struct ec_response_motion_sensor_data));
	st->param.info.sensor_num = sensor_num;
	if (ret != 0) {
		dev_warn(&indio_dev->dev, "Unable to read sensor data\n");
		return ret;
	}

	for_each_set_bit(i, &scan_mask, indio_dev->masklength) {
		*data = st->resp->dump.sensor[sensor_num].data[i] *
			st->sign[i];
		data++;
	}

	return 0;
}

static int cros_ec_accel_legacy_read(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     int *val, int *val2, long mask)
{
	struct cros_ec_sensors_core_state *st = iio_priv(indio_dev);
	s16 data = 0;
	int ret;
	int idx = chan->scan_index;

	mutex_lock(&st->cmd_lock);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = st->read_ec_sensors_data(indio_dev, 1 << idx, &data);
		if (ret < 0)
			break;
		ret = IIO_VAL_INT;
		*val = data;
		break;
	case IIO_CHAN_INFO_SCALE:
		WARN_ON(st->type != MOTIONSENSE_TYPE_ACCEL);
		*val = 0;
		*val2 = ACCEL_LEGACY_NSCALE;
		ret = IIO_VAL_INT_PLUS_NANO;
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		/* Calibration not supported. */
		*val = 0;
		ret = IIO_VAL_INT;
		break;
	default:
		ret = cros_ec_sensors_core_read(st, chan, val, val2,
				mask);
		break;
	}
	mutex_unlock(&st->cmd_lock);

	return ret;
}

static int cros_ec_accel_legacy_write(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      int val, int val2, long mask)
{
	/*
	 * Do nothing but don't return an error code to allow calibration
	 * script to work.
	 */
	if (mask == IIO_CHAN_INFO_CALIBBIAS)
		return 0;

	return -EINVAL;
}

static const struct iio_info cros_ec_accel_legacy_info = {
	.read_raw = &cros_ec_accel_legacy_read,
	.write_raw = &cros_ec_accel_legacy_write,
};

/*
 * Present the channel using HTML5 standard:
 * need to invert X and Y and invert some lid axis.
 */
#define CROS_EC_ACCEL_ROTATE_AXIS(_axis)				\
	((_axis) == CROS_EC_SENSOR_Z ? CROS_EC_SENSOR_Z :		\
	 ((_axis) == CROS_EC_SENSOR_X ? CROS_EC_SENSOR_Y :		\
	  CROS_EC_SENSOR_X))

#define CROS_EC_ACCEL_LEGACY_CHAN(_axis)				\
	{								\
		.type = IIO_ACCEL,					\
		.channel2 = IIO_MOD_X + (_axis),			\
		.modified = 1,					        \
		.info_mask_separate =					\
			BIT(IIO_CHAN_INFO_RAW) |			\
			BIT(IIO_CHAN_INFO_CALIBBIAS),			\
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SCALE),	\
		.ext_info = cros_ec_sensors_ext_info,			\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = CROS_EC_SENSOR_BITS,		\
			.storagebits = CROS_EC_SENSOR_BITS,		\
		},							\
		.scan_index = CROS_EC_ACCEL_ROTATE_AXIS(_axis),		\
	}								\

static const struct iio_chan_spec cros_ec_accel_legacy_channels[] = {
		CROS_EC_ACCEL_LEGACY_CHAN(CROS_EC_SENSOR_X),
		CROS_EC_ACCEL_LEGACY_CHAN(CROS_EC_SENSOR_Y),
		CROS_EC_ACCEL_LEGACY_CHAN(CROS_EC_SENSOR_Z),
		IIO_CHAN_SOFT_TIMESTAMP(CROS_EC_SENSOR_MAX_AXIS)
};

static int cros_ec_accel_legacy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct cros_ec_sensors_core_state *state;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*state));
	if (!indio_dev)
		return -ENOMEM;

	ret = cros_ec_sensors_core_init(pdev, indio_dev, true);
	if (ret)
		return ret;

	indio_dev->info = &cros_ec_accel_legacy_info;
	state = iio_priv(indio_dev);

	if (state->ec->cmd_readmem != NULL)
		state->read_ec_sensors_data = cros_ec_sensors_read_lpc;
	else
		state->read_ec_sensors_data = cros_ec_accel_legacy_read_cmd;

	indio_dev->channels = cros_ec_accel_legacy_channels;
	indio_dev->num_channels = ARRAY_SIZE(cros_ec_accel_legacy_channels);
	/* The lid sensor needs to be presented inverted. */
	if (state->loc == MOTIONSENSE_LOC_LID) {
		state->sign[CROS_EC_SENSOR_X] = -1;
		state->sign[CROS_EC_SENSOR_Z] = -1;
	}

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
			cros_ec_sensors_capture, NULL);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static struct platform_driver cros_ec_accel_platform_driver = {
	.driver = {
		.name	= DRV_NAME,
	},
	.probe		= cros_ec_accel_legacy_probe,
};
module_platform_driver(cros_ec_accel_platform_driver);

MODULE_DESCRIPTION("ChromeOS EC legacy accelerometer driver");
MODULE_AUTHOR("Gwendal Grignou <gwendal@chromium.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
