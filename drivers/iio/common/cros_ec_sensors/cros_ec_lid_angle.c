// SPDX-License-Identifier: GPL-2.0

/*
 * cros_ec_lid_angle - Driver for CrOS EC lid angle sensor.
 *
 * Copyright 2018 Google, Inc
 *
 * This driver uses the cros-ec interface to communicate with the Chrome OS
 * EC about counter sensors. Counters are presented through
 * iio sysfs.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iio/buffer.h>
#include <linux/iio/common/cros_ec_sensors_core.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define DRV_NAME "cros-ec-lid-angle"

/*
 * One channel for the lid angle, the other for timestamp.
 */
static const struct iio_chan_spec cros_ec_lid_angle_channels[] = {
	{
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_type.realbits = CROS_EC_SENSOR_BITS,
		.scan_type.storagebits = CROS_EC_SENSOR_BITS,
		.scan_type.sign = 'u',
		.type = IIO_ANGL
	},
	IIO_CHAN_SOFT_TIMESTAMP(1)
};

/* State data for ec_sensors iio driver. */
struct cros_ec_lid_angle_state {
	/* Shared by all sensors */
	struct cros_ec_sensors_core_state core;
};

static int cros_ec_sensors_read_lid_angle(struct iio_dev *indio_dev,
					  unsigned long scan_mask, s16 *data)
{
	struct cros_ec_sensors_core_state *st = iio_priv(indio_dev);
	int ret;

	st->param.cmd = MOTIONSENSE_CMD_LID_ANGLE;
	ret = cros_ec_motion_send_host_cmd(st, sizeof(st->resp->lid_angle));
	if (ret) {
		dev_warn(&indio_dev->dev, "Unable to read lid angle\n");
		return ret;
	}

	*data = st->resp->lid_angle.value;
	return 0;
}

static int cros_ec_lid_angle_read(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    int *val, int *val2, long mask)
{
	struct cros_ec_lid_angle_state *st = iio_priv(indio_dev);
	s16 data;
	int ret;

	mutex_lock(&st->core.cmd_lock);
	ret = cros_ec_sensors_read_lid_angle(indio_dev, 1, &data);
	if (ret == 0) {
		*val = data;
		ret = IIO_VAL_INT;
	}
	mutex_unlock(&st->core.cmd_lock);
	return ret;
}

static const struct iio_info cros_ec_lid_angle_info = {
	.read_raw = &cros_ec_lid_angle_read,
};

static int cros_ec_lid_angle_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct cros_ec_lid_angle_state *state;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*state));
	if (!indio_dev)
		return -ENOMEM;

	ret = cros_ec_sensors_core_init(pdev, indio_dev, false, NULL,
					NULL, false);
	if (ret)
		return ret;

	indio_dev->info = &cros_ec_lid_angle_info;
	state = iio_priv(indio_dev);
	indio_dev->channels = cros_ec_lid_angle_channels;
	indio_dev->num_channels = ARRAY_SIZE(cros_ec_lid_angle_channels);

	state->core.read_ec_sensors_data = cros_ec_sensors_read_lid_angle;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
			cros_ec_sensors_capture, NULL);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct platform_device_id cros_ec_lid_angle_ids[] = {
	{
		.name = DRV_NAME,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, cros_ec_lid_angle_ids);

static struct platform_driver cros_ec_lid_angle_platform_driver = {
	.driver = {
		.name	= DRV_NAME,
	},
	.probe		= cros_ec_lid_angle_probe,
	.id_table	= cros_ec_lid_angle_ids,
};
module_platform_driver(cros_ec_lid_angle_platform_driver);

MODULE_DESCRIPTION("ChromeOS EC driver for reporting convertible lid angle.");
MODULE_LICENSE("GPL v2");
