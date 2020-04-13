// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Common library for ADIS16XXX devices
 *
 * Copyright 2012 Analog Devices Inc.
 *   Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/imu/adis.h>

static int adis_update_scan_mode_burst(struct iio_dev *indio_dev,
	const unsigned long *scan_mask)
{
	struct adis *adis = iio_device_get_drvdata(indio_dev);
	unsigned int burst_length;
	u8 *tx;

	/* All but the timestamp channel */
	burst_length = (indio_dev->num_channels - 1) * sizeof(u16);
	burst_length += adis->burst->extra_len;

	adis->xfer = kcalloc(2, sizeof(*adis->xfer), GFP_KERNEL);
	if (!adis->xfer)
		return -ENOMEM;

	adis->buffer = kzalloc(burst_length + sizeof(u16), GFP_KERNEL);
	if (!adis->buffer) {
		kfree(adis->xfer);
		adis->xfer = NULL;
		return -ENOMEM;
	}

	tx = adis->buffer + burst_length;
	tx[0] = ADIS_READ_REG(adis->burst->reg_cmd);
	tx[1] = 0;

	adis->xfer[0].tx_buf = tx;
	adis->xfer[0].bits_per_word = 8;
	adis->xfer[0].len = 2;
	adis->xfer[1].rx_buf = adis->buffer;
	adis->xfer[1].bits_per_word = 8;
	adis->xfer[1].len = burst_length;

	spi_message_init(&adis->msg);
	spi_message_add_tail(&adis->xfer[0], &adis->msg);
	spi_message_add_tail(&adis->xfer[1], &adis->msg);

	return 0;
}

int adis_update_scan_mode(struct iio_dev *indio_dev,
	const unsigned long *scan_mask)
{
	struct adis *adis = iio_device_get_drvdata(indio_dev);
	const struct iio_chan_spec *chan;
	unsigned int scan_count;
	unsigned int i, j;
	__be16 *tx, *rx;

	kfree(adis->xfer);
	kfree(adis->buffer);

	if (adis->burst && adis->burst->en)
		return adis_update_scan_mode_burst(indio_dev, scan_mask);

	scan_count = indio_dev->scan_bytes / 2;

	adis->xfer = kcalloc(scan_count + 1, sizeof(*adis->xfer), GFP_KERNEL);
	if (!adis->xfer)
		return -ENOMEM;

	adis->buffer = kcalloc(indio_dev->scan_bytes, 2, GFP_KERNEL);
	if (!adis->buffer) {
		kfree(adis->xfer);
		adis->xfer = NULL;
		return -ENOMEM;
	}

	rx = adis->buffer;
	tx = rx + scan_count;

	spi_message_init(&adis->msg);

	for (j = 0; j <= scan_count; j++) {
		adis->xfer[j].bits_per_word = 8;
		if (j != scan_count)
			adis->xfer[j].cs_change = 1;
		adis->xfer[j].len = 2;
		adis->xfer[j].delay.value = adis->data->read_delay;
		adis->xfer[j].delay.unit = SPI_DELAY_UNIT_USECS;
		if (j < scan_count)
			adis->xfer[j].tx_buf = &tx[j];
		if (j >= 1)
			adis->xfer[j].rx_buf = &rx[j - 1];
		spi_message_add_tail(&adis->xfer[j], &adis->msg);
	}

	chan = indio_dev->channels;
	for (i = 0; i < indio_dev->num_channels; i++, chan++) {
		if (!test_bit(chan->scan_index, scan_mask))
			continue;
		if (chan->scan_type.storagebits == 32)
			*tx++ = cpu_to_be16((chan->address + 2) << 8);
		*tx++ = cpu_to_be16(chan->address << 8);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(adis_update_scan_mode);

static irqreturn_t adis_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct adis *adis = iio_device_get_drvdata(indio_dev);
	int ret;

	if (!adis->buffer)
		return -ENOMEM;

	if (adis->data->has_paging) {
		mutex_lock(&adis->state_lock);
		if (adis->current_page != 0) {
			adis->tx[0] = ADIS_WRITE_REG(ADIS_REG_PAGE_ID);
			adis->tx[1] = 0;
			spi_write(adis->spi, adis->tx, 2);
		}
	}

	ret = spi_sync(adis->spi, &adis->msg);
	if (ret)
		dev_err(&adis->spi->dev, "Failed to read data: %d", ret);


	if (adis->data->has_paging) {
		adis->current_page = 0;
		mutex_unlock(&adis->state_lock);
	}

	iio_push_to_buffers_with_timestamp(indio_dev, adis->buffer,
		pf->timestamp);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

/**
 * adis_setup_buffer_and_trigger() - Sets up buffer and trigger for the adis device
 * @adis: The adis device.
 * @indio_dev: The IIO device.
 * @trigger_handler: Optional trigger handler, may be NULL.
 *
 * Returns 0 on success, a negative error code otherwise.
 *
 * This function sets up the buffer and trigger for a adis devices.  If
 * 'trigger_handler' is NULL the default trigger handler will be used. The
 * default trigger handler will simply read the registers assigned to the
 * currently active channels.
 *
 * adis_cleanup_buffer_and_trigger() should be called to free the resources
 * allocated by this function.
 */
int adis_setup_buffer_and_trigger(struct adis *adis, struct iio_dev *indio_dev,
	irqreturn_t (*trigger_handler)(int, void *))
{
	int ret;

	if (!trigger_handler)
		trigger_handler = adis_trigger_handler;

	ret = iio_triggered_buffer_setup(indio_dev, &iio_pollfunc_store_time,
		trigger_handler, NULL);
	if (ret)
		return ret;

	if (adis->spi->irq) {
		ret = adis_probe_trigger(adis, indio_dev);
		if (ret)
			goto error_buffer_cleanup;
	}
	return 0;

error_buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
	return ret;
}
EXPORT_SYMBOL_GPL(adis_setup_buffer_and_trigger);

/**
 * adis_cleanup_buffer_and_trigger() - Free buffer and trigger resources
 * @adis: The adis device.
 * @indio_dev: The IIO device.
 *
 * Frees resources allocated by adis_setup_buffer_and_trigger()
 */
void adis_cleanup_buffer_and_trigger(struct adis *adis,
	struct iio_dev *indio_dev)
{
	if (adis->spi->irq)
		adis_remove_trigger(adis);
	kfree(adis->buffer);
	kfree(adis->xfer);
	iio_triggered_buffer_cleanup(indio_dev);
}
EXPORT_SYMBOL_GPL(adis_cleanup_buffer_and_trigger);
