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
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include <linux/iio/common/st_sensors.h>
#include "st_pressure.h"

int st_press_trig_set_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);

	return st_sensors_set_dataready_irq(indio_dev, state);
}

static int st_press_buffer_preenable(struct iio_dev *indio_dev)
{
	return st_sensors_set_enable(indio_dev, true);
}

static int st_press_buffer_postenable(struct iio_dev *indio_dev)
{
	int err;
	struct st_sensor_data *press_data = iio_priv(indio_dev);

	press_data->buffer_data = kmalloc(indio_dev->scan_bytes, GFP_KERNEL);
	if (press_data->buffer_data == NULL) {
		err = -ENOMEM;
		goto allocate_memory_error;
	}

	err = iio_triggered_buffer_postenable(indio_dev);
	if (err < 0)
		goto st_press_buffer_postenable_error;

	return err;

st_press_buffer_postenable_error:
	kfree(press_data->buffer_data);
allocate_memory_error:
	return err;
}

static int st_press_buffer_predisable(struct iio_dev *indio_dev)
{
	int err;
	struct st_sensor_data *press_data = iio_priv(indio_dev);

	err = iio_triggered_buffer_predisable(indio_dev);
	if (err < 0)
		goto st_press_buffer_predisable_error;

	err = st_sensors_set_enable(indio_dev, false);

st_press_buffer_predisable_error:
	kfree(press_data->buffer_data);
	return err;
}

static const struct iio_buffer_setup_ops st_press_buffer_setup_ops = {
	.preenable = &st_press_buffer_preenable,
	.postenable = &st_press_buffer_postenable,
	.predisable = &st_press_buffer_predisable,
};

int st_press_allocate_ring(struct iio_dev *indio_dev)
{
	return iio_triggered_buffer_setup(indio_dev, NULL,
		&st_sensors_trigger_handler, &st_press_buffer_setup_ops);
}

void st_press_deallocate_ring(struct iio_dev *indio_dev)
{
	iio_triggered_buffer_cleanup(indio_dev);
}

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics pressures buffer");
MODULE_LICENSE("GPL v2");
