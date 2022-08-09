// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics pressures driver
 *
 * Copyright 2013 STMicroelectronics Inc.
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
#include "st_pressure.h"

int st_press_trig_set_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);

	return st_sensors_set_dataready_irq(indio_dev, state);
}

static int st_press_buffer_postenable(struct iio_dev *indio_dev)
{
	return st_sensors_set_enable(indio_dev, true);
}

static int st_press_buffer_predisable(struct iio_dev *indio_dev)
{
	return st_sensors_set_enable(indio_dev, false);
}

static const struct iio_buffer_setup_ops st_press_buffer_setup_ops = {
	.postenable = &st_press_buffer_postenable,
	.predisable = &st_press_buffer_predisable,
};

int st_press_allocate_ring(struct iio_dev *indio_dev)
{
	return devm_iio_triggered_buffer_setup(indio_dev->dev.parent, indio_dev,
		NULL, &st_sensors_trigger_handler, &st_press_buffer_setup_ops);
}

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics pressures buffer");
MODULE_LICENSE("GPL v2");
