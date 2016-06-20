/*
 * STMicroelectronics sensors buffer library driver
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
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/interrupt.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/irqreturn.h>

#include <linux/iio/common/st_sensors.h>


int st_sensors_get_buffer_element(struct iio_dev *indio_dev, u8 *buf)
{
	int i, len;
	int total = 0;
	struct st_sensor_data *sdata = iio_priv(indio_dev);
	unsigned int num_data_channels = sdata->num_data_channels;

	for (i = 0; i < num_data_channels; i++) {
		unsigned int bytes_to_read;

		if (test_bit(i, indio_dev->active_scan_mask)) {
			bytes_to_read = indio_dev->channels[i].scan_type.storagebits >> 3;
			len = sdata->tf->read_multiple_byte(&sdata->tb,
				sdata->dev, indio_dev->channels[i].address,
				bytes_to_read,
				buf + total, sdata->multiread_bit);

			if (len < bytes_to_read)
				return -EIO;

			/* Advance the buffer pointer */
			total += len;
		}
	}

	return total;
}
EXPORT_SYMBOL(st_sensors_get_buffer_element);

irqreturn_t st_sensors_trigger_handler(int irq, void *p)
{
	int len;
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct st_sensor_data *sdata = iio_priv(indio_dev);
	s64 timestamp;

	/* If we do timetamping here, do it before reading the values */
	if (sdata->hw_irq_trigger)
		timestamp = sdata->hw_timestamp;
	else
		timestamp = iio_get_time_ns();

	len = st_sensors_get_buffer_element(indio_dev, sdata->buffer_data);
	if (len < 0)
		goto st_sensors_get_buffer_element_error;

	iio_push_to_buffers_with_timestamp(indio_dev, sdata->buffer_data,
					   timestamp);

st_sensors_get_buffer_element_error:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL(st_sensors_trigger_handler);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics ST-sensors buffer");
MODULE_LICENSE("GPL v2");
