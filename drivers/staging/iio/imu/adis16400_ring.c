#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/export.h>

#include "../iio.h"
#include "../ring_sw.h"
#include "../trigger_consumer.h"
#include "adis16400.h"

/**
 * adis16400_spi_read_burst() - read all data registers
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @rx: somewhere to pass back the value read (min size is 24 bytes)
 **/
static int adis16400_spi_read_burst(struct device *dev, u8 *rx)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16400_state *st = iio_priv(indio_dev);
	u32 old_speed_hz = st->us->max_speed_hz;
	int ret;

	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
		}, {
			.rx_buf = rx,
			.bits_per_word = 8,
			.len = 24,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16400_READ_REG(ADIS16400_GLOB_CMD);
	st->tx[1] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);

	st->us->max_speed_hz = min(ADIS16400_SPI_BURST, old_speed_hz);
	spi_setup(st->us);

	ret = spi_sync(st->us, &msg);
	if (ret)
		dev_err(&st->us->dev, "problem when burst reading");

	st->us->max_speed_hz = old_speed_hz;
	spi_setup(st->us);
	mutex_unlock(&st->buf_lock);
	return ret;
}

static const u16 read_all_tx_array[] = {
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_SUPPLY_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_XGYRO_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_YGYRO_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_ZGYRO_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_XACCL_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_YACCL_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_ZACCL_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16350_XTEMP_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16350_YTEMP_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16350_ZTEMP_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_AUX_ADC)),
};

static int adis16350_spi_read_all(struct device *dev, u8 *rx)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16400_state *st = iio_priv(indio_dev);

	struct spi_message msg;
	int i, j = 0, ret;
	struct spi_transfer *xfers;
	int scan_count = bitmap_weight(indio_dev->active_scan_mask,
				       indio_dev->masklength);

	xfers = kzalloc(sizeof(*xfers)*(scan_count + 1),
			GFP_KERNEL);
	if (xfers == NULL)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(read_all_tx_array); i++)
		if (test_bit(i, indio_dev->active_scan_mask)) {
			xfers[j].tx_buf = &read_all_tx_array[i];
			xfers[j].bits_per_word = 16;
			xfers[j].len = 2;
			xfers[j + 1].rx_buf = rx + j*2;
			j++;
		}
	xfers[j].bits_per_word = 16;
	xfers[j].len = 2;

	spi_message_init(&msg);
	for (j = 0; j < scan_count + 1; j++)
		spi_message_add_tail(&xfers[j], &msg);

	ret = spi_sync(st->us, &msg);
	kfree(xfers);

	return ret;
}

/* Whilst this makes a lot of calls to iio_sw_ring functions - it is to device
 * specific to be rolled into the core.
 */
static irqreturn_t adis16400_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct adis16400_state *st = iio_priv(indio_dev);
	struct iio_buffer *ring = indio_dev->buffer;
	int i = 0, j, ret = 0;
	s16 *data;
	size_t datasize = ring->access->get_bytes_per_datum(ring);
	/* Asumption that long is enough for maximum channels */
	unsigned long mask = *indio_dev->active_scan_mask;
	int scan_count = bitmap_weight(indio_dev->active_scan_mask,
				       indio_dev->masklength);
	data = kmalloc(datasize , GFP_KERNEL);
	if (data == NULL) {
		dev_err(&st->us->dev, "memory alloc failed in ring bh");
		return -ENOMEM;
	}

	if (scan_count) {
		if (st->variant->flags & ADIS16400_NO_BURST) {
			ret = adis16350_spi_read_all(&indio_dev->dev, st->rx);
			if (ret < 0)
				goto err;
			for (; i < scan_count; i++)
				data[i]	= *(s16 *)(st->rx + i*2);
		} else {
			ret = adis16400_spi_read_burst(&indio_dev->dev, st->rx);
			if (ret < 0)
				goto err;
			for (; i < scan_count; i++) {
				j = __ffs(mask);
				mask &= ~(1 << j);
				data[i] = be16_to_cpup(
					(__be16 *)&(st->rx[j*2]));
			}
		}
	}
	/* Guaranteed to be aligned with 8 byte boundary */
	if (ring->scan_timestamp)
		*((s64 *)(data + ((i + 3)/4)*4)) = pf->timestamp;
	ring->access->store_to(indio_dev->buffer, (u8 *) data, pf->timestamp);

	iio_trigger_notify_done(indio_dev->trig);

	kfree(data);
	return IRQ_HANDLED;

err:
	kfree(data);
	return ret;
}

void adis16400_unconfigure_ring(struct iio_dev *indio_dev)
{
	iio_dealloc_pollfunc(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->buffer);
}

static const struct iio_buffer_setup_ops adis16400_ring_setup_ops = {
	.preenable = &iio_sw_buffer_preenable,
	.postenable = &iio_triggered_buffer_postenable,
	.predisable = &iio_triggered_buffer_predisable,
};

int adis16400_configure_ring(struct iio_dev *indio_dev)
{
	int ret = 0;
	struct iio_buffer *ring;

	ring = iio_sw_rb_allocate(indio_dev);
	if (!ring) {
		ret = -ENOMEM;
		return ret;
	}
	indio_dev->buffer = ring;
	/* Effectively select the ring buffer implementation */
	ring->access = &ring_sw_access_funcs;
	ring->scan_timestamp = true;
	indio_dev->setup_ops = &adis16400_ring_setup_ops;

	indio_dev->pollfunc = iio_alloc_pollfunc(&iio_pollfunc_store_time,
						 &adis16400_trigger_handler,
						 IRQF_ONESHOT,
						 indio_dev,
						 "%s_consumer%d",
						 indio_dev->name,
						 indio_dev->id);
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_iio_sw_rb_free;
	}

	indio_dev->modes |= INDIO_BUFFER_TRIGGERED;
	return 0;
error_iio_sw_rb_free:
	iio_sw_rb_free(indio_dev->buffer);
	return ret;
}
