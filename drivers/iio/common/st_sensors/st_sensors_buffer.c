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
	u8 *addr;
	int i, n = 0, len;
	struct st_sensor_data *sdata = iio_priv(indio_dev);
	unsigned int num_data_channels = sdata->num_data_channels;
	unsigned int byte_for_channel =
			indio_dev->channels[0].scan_type.storagebits >> 3;

	addr = kmalloc(num_data_channels, GFP_KERNEL);
	if (!addr) {
		len = -ENOMEM;
		goto st_sensors_get_buffer_element_error;
	}

	for (i = 0; i < num_data_channels; i++) {
		if (test_bit(i, indio_dev->active_scan_mask)) {
			addr[n] = indio_dev->channels[i].address;
			n++;
		}
	}
	switch (n) {
	case 1:
		len = sdata->tf->read_multiple_byte(&sdata->tb, sdata->dev,
			addr[0], byte_for_channel, buf, sdata->multiread_bit);
		break;
	case 2:
		if ((addr[1] - addr[0]) == byte_for_channel) {
			len = sdata->tf->read_multiple_byte(&sdata->tb,
				sdata->dev, addr[0], byte_for_channel * n,
				buf, sdata->multiread_bit);
		} else {
			u8 *rx_array;
			rx_array = kmalloc(byte_for_channel * num_data_channels,
					   GFP_KERNEL);
			if (!rx_array) {
				len = -ENOMEM;
				goto st_sensors_free_memory;
			}

			len = sdata->tf->read_multiple_byte(&sdata->tb,
				sdata->dev, addr[0],
				byte_for_channel * num_data_channels,
				rx_array, sdata->multiread_bit);
			if (len < 0) {
				kfree(rx_array);
				goto st_sensors_free_memory;
			}

			for (i = 0; i < n * num_data_channels; i++) {
				if (i < n)
					buf[i] = rx_array[i];
				else
					buf[i] = rx_array[n + i];
			}
			kfree(rx_array);
			len = byte_for_channel * n;
		}
		break;
	case 3:
		len = sdata->tf->read_multiple_byte(&sdata->tb, sdata->dev,
			addr[0], byte_for_channel * num_data_channels,
			buf, sdata->multiread_bit);
		break;
	default:
		len = -EINVAL;
		goto st_sensors_free_memory;
	}
	if (len != byte_for_channel * n) {
		len = -EIO;
		goto st_sensors_free_memory;
	}

st_sensors_free_memory:
	kfree(addr);
st_sensors_get_buffer_element_error:
	return len;
}
EXPORT_SYMBOL(st_sensors_get_buffer_element);

irqreturn_t st_sensors_trigger_handler(int irq, void *p)
{
	int len;
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	len = st_sensors_get_buffer_element(indio_dev, sdata->buffer_data);
	if (len < 0)
		goto st_sensors_get_buffer_element_error;

	iio_push_to_buffers_with_timestamp(indio_dev, sdata->buffer_data,
		pf->timestamp);

st_sensors_get_buffer_element_error:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL(st_sensors_trigger_handler);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics ST-sensors buffer");
MODULE_LICENSE("GPL v2");
