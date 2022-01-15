// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics accelerometers driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>

#include <linux/iio/common/st_sensors.h>
#include "st_accel.h"

int st_accel_trig_set_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);

	return st_sensors_set_dataready_irq(indio_dev, state);
}

static int st_accel_buffer_postenable(struct iio_dev *indio_dev)
{
	int err;

	err = st_sensors_set_axis_enable(indio_dev, indio_dev->active_scan_mask[0]);
	if (err < 0)
		return err;

	err = st_sensors_set_enable(indio_dev, true);
	if (err < 0)
		goto st_accel_buffer_enable_all_axis;

	return 0;

st_accel_buffer_enable_all_axis:
	st_sensors_set_axis_enable(indio_dev, ST_SENSORS_ENABLE_ALL_AXIS);
	return err;
}

static int st_accel_buffer_predisable(struct iio_dev *indio_dev)
{
	int err;

	err = st_sensors_set_enable(indio_dev, false);
	if (err < 0)
		return err;

	return st_sensors_set_axis_enable(indio_dev,
					  ST_SENSORS_ENABLE_ALL_AXIS);
}

static const struct iio_buffer_setup_ops st_accel_buffer_setup_ops = {
	.postenable = &st_accel_buffer_postenable,
	.predisable = &st_accel_buffer_predisable,
};

int st_accel_allocate_ring(struct iio_dev *indio_dev)
{
	return devm_iio_triggered_buffer_setup(indio_dev->dev.parent, indio_dev,
		NULL, &st_sensors_trigger_handler, &st_accel_buffer_setup_ops);
}

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics accelerometers buffer");
MODULE_LICENSE("GPL v2");
