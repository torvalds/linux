#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../ring_sw.h"
#include "accel.h"
#include "../trigger.h"
#include "adis16201.h"


/**
 * adis16201_read_ring_data() read data registers which will be placed into ring
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @rx: somewhere to pass back the value read
 **/
static int adis16201_read_ring_data(struct iio_dev *indio_dev, u8 *rx)
{
	struct spi_message msg;
	struct adis16201_state *st = iio_priv(indio_dev);
	struct spi_transfer xfers[ADIS16201_OUTPUTS + 1];
	int ret;
	int i;

	mutex_lock(&st->buf_lock);

	spi_message_init(&msg);

	memset(xfers, 0, sizeof(xfers));
	for (i = 0; i <= ADIS16201_OUTPUTS; i++) {
		xfers[i].bits_per_word = 8;
		xfers[i].cs_change = 1;
		xfers[i].len = 2;
		xfers[i].delay_usecs = 20;
		xfers[i].tx_buf = st->tx + 2 * i;
		st->tx[2 * i] = ADIS16201_READ_REG(ADIS16201_SUPPLY_OUT +
						   2 * i);
		st->tx[2 * i + 1] = 0;
		if (i >= 1)
			xfers[i].rx_buf = rx + 2 * (i - 1);
		spi_message_add_tail(&xfers[i], &msg);
	}

	ret = spi_sync(st->us, &msg);
	if (ret)
		dev_err(&st->us->dev, "problem when burst reading");

	mutex_unlock(&st->buf_lock);

	return ret;
}

/* Whilst this makes a lot of calls to iio_sw_ring functions - it is to device
 * specific to be rolled into the core.
 */
static irqreturn_t adis16201_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->private_data;
	struct adis16201_state *st = iio_priv(indio_dev);
	struct iio_ring_buffer *ring = indio_dev->ring;

	int i = 0;
	s16 *data;
	size_t datasize = ring->access->get_bytes_per_datum(ring);

	data = kmalloc(datasize, GFP_KERNEL);
	if (data == NULL) {
		dev_err(&st->us->dev, "memory alloc failed in ring bh");
		return -ENOMEM;
	}

	if (ring->scan_count)
		if (adis16201_read_ring_data(indio_dev, st->rx) >= 0)
			for (; i < ring->scan_count; i++)
				data[i] = be16_to_cpup(
					(__be16 *)&(st->rx[i*2]));

	/* Guaranteed to be aligned with 8 byte boundary */
	if (ring->scan_timestamp)
		*((s64 *)(data + ((i + 3)/4)*4)) = pf->timestamp;

	ring->access->store_to(ring, (u8 *)data, pf->timestamp);

	iio_trigger_notify_done(indio_dev->trig);
	kfree(data);

	return IRQ_HANDLED;
}

void adis16201_unconfigure_ring(struct iio_dev *indio_dev)
{
	iio_dealloc_pollfunc(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->ring);
}

static const struct iio_ring_setup_ops adis16201_ring_setup_ops = {
	.preenable = &iio_sw_ring_preenable,
	.postenable = &iio_triggered_ring_postenable,
	.predisable = &iio_triggered_ring_predisable,
};

int adis16201_configure_ring(struct iio_dev *indio_dev)
{
	int ret = 0;
	struct iio_ring_buffer *ring;

	ring = iio_sw_rb_allocate(indio_dev);
	if (!ring) {
		ret = -ENOMEM;
		return ret;
	}
	indio_dev->ring = ring;
	/* Effectively select the ring buffer implementation */
	ring->bpe = 2;
	ring->scan_timestamp = true;
	ring->access = &ring_sw_access_funcs;
	ring->setup_ops = &adis16201_ring_setup_ops;
	ring->owner = THIS_MODULE;

	/* Set default scan mode */
	iio_scan_mask_set(ring,	ADIS16201_SCAN_SUPPLY);
	iio_scan_mask_set(ring, ADIS16201_SCAN_ACC_X);
	iio_scan_mask_set(ring, ADIS16201_SCAN_ACC_Y);
	iio_scan_mask_set(ring, ADIS16201_SCAN_AUX_ADC);
	iio_scan_mask_set(ring, ADIS16201_SCAN_TEMP);
	iio_scan_mask_set(ring, ADIS16201_SCAN_INCLI_X);
	iio_scan_mask_set(ring, ADIS16201_SCAN_INCLI_Y);

	indio_dev->pollfunc = iio_alloc_pollfunc(&iio_pollfunc_store_time,
						 &adis16201_trigger_handler,
						 IRQF_ONESHOT,
						 indio_dev,
						 "adis16201_consumer%d",
						 indio_dev->id);
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_iio_sw_rb_free;
	}

	indio_dev->modes |= INDIO_RING_TRIGGERED;
	return 0;
error_iio_sw_rb_free:
	iio_sw_rb_free(indio_dev->ring);
	return ret;
}
