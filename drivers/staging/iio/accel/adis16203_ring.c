#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../ring_sw.h"
#include "accel.h"
#include "../trigger.h"
#include "adis16203.h"

static IIO_SCAN_EL_C(in_supply, ADIS16203_SCAN_SUPPLY, ADIS16203_SUPPLY_OUT, NULL);
static IIO_CONST_ATTR_SCAN_EL_TYPE(in_supply, u, 12, 16);
static IIO_SCAN_EL_C(in0, ADIS16203_SCAN_AUX_ADC, ADIS16203_AUX_ADC, NULL);
static IIO_CONST_ATTR_SCAN_EL_TYPE(in0, u, 12, 16);
static IIO_SCAN_EL_C(temp, ADIS16203_SCAN_TEMP, ADIS16203_TEMP_OUT, NULL);
static IIO_CONST_ATTR_SCAN_EL_TYPE(temp, u, 12, 16);
static IIO_SCAN_EL_C(incli_x, ADIS16203_SCAN_INCLI_X,
		     ADIS16203_XINCL_OUT, NULL);
static IIO_SCAN_EL_C(incli_y, ADIS16203_SCAN_INCLI_Y,
		     ADIS16203_YINCL_OUT, NULL);
static IIO_CONST_ATTR_SCAN_EL_TYPE(incli, s, 14, 16);
static IIO_SCAN_EL_TIMESTAMP(5);
static IIO_CONST_ATTR_SCAN_EL_TYPE(timestamp, s, 64, 64);

static struct attribute *adis16203_scan_el_attrs[] = {
	&iio_scan_el_in_supply.dev_attr.attr,
	&iio_const_attr_in_supply_index.dev_attr.attr,
	&iio_const_attr_in_supply_type.dev_attr.attr,
	&iio_scan_el_in0.dev_attr.attr,
	&iio_const_attr_in0_index.dev_attr.attr,
	&iio_const_attr_in0_type.dev_attr.attr,
	&iio_scan_el_temp.dev_attr.attr,
	&iio_const_attr_temp_index.dev_attr.attr,
	&iio_const_attr_temp_type.dev_attr.attr,
	&iio_scan_el_incli_x.dev_attr.attr,
	&iio_const_attr_incli_x_index.dev_attr.attr,
	&iio_scan_el_incli_y.dev_attr.attr,
	&iio_const_attr_incli_y_index.dev_attr.attr,
	&iio_const_attr_incli_type.dev_attr.attr,
	&iio_scan_el_timestamp.dev_attr.attr,
	&iio_const_attr_timestamp_index.dev_attr.attr,
	&iio_const_attr_timestamp_type.dev_attr.attr,
	NULL,
};

static struct attribute_group adis16203_scan_el_group = {
	.attrs = adis16203_scan_el_attrs,
	.name = "scan_elements",
};

/**
 * adis16203_poll_func_th() top half interrupt handler called by trigger
 * @private_data:	iio_dev
 **/
static void adis16203_poll_func_th(struct iio_dev *indio_dev, s64 timestamp)
{
	struct adis16203_state *st = iio_dev_get_devdata(indio_dev);
	st->last_timestamp = timestamp;
	schedule_work(&st->work_trigger_to_ring);
}

/**
 * adis16203_read_ring_data() read data registers which will be placed into ring
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @rx: somewhere to pass back the value read
 **/
static int adis16203_read_ring_data(struct device *dev, u8 *rx)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16203_state *st = iio_dev_get_devdata(indio_dev);
	struct spi_transfer xfers[ADIS16203_OUTPUTS + 1];
	int ret;
	int i;

	mutex_lock(&st->buf_lock);

	spi_message_init(&msg);

	memset(xfers, 0, sizeof(xfers));
	for (i = 0; i <= ADIS16203_OUTPUTS; i++) {
		xfers[i].bits_per_word = 8;
		xfers[i].cs_change = 1;
		xfers[i].len = 2;
		xfers[i].delay_usecs = 20;
		xfers[i].tx_buf = st->tx + 2 * i;
		if (i < 1) /* SUPPLY_OUT: 0x02, AUX_ADC: 0x08 */
			st->tx[2 * i] = ADIS16203_READ_REG(ADIS16203_SUPPLY_OUT + 2 * i);
		else
			st->tx[2 * i] = ADIS16203_READ_REG(ADIS16203_SUPPLY_OUT + 2 * i + 6);
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
static void adis16203_trigger_bh_to_ring(struct work_struct *work_s)
{
	struct adis16203_state *st
		= container_of(work_s, struct adis16203_state,
			       work_trigger_to_ring);
	struct iio_ring_buffer *ring = st->indio_dev->ring;

	int i = 0;
	s16 *data;
	size_t datasize = ring->access.get_bytes_per_datum(ring);

	data = kmalloc(datasize, GFP_KERNEL);
	if (data == NULL) {
		dev_err(&st->us->dev, "memory alloc failed in ring bh");
		return;
	}

	if (ring->scan_count)
		if (adis16203_read_ring_data(&st->indio_dev->dev, st->rx) >= 0)
			for (; i < ring->scan_count; i++)
				data[i] = be16_to_cpup(
					(__be16 *)&(st->rx[i*2]));

	/* Guaranteed to be aligned with 8 byte boundary */
	if (ring->scan_timestamp)
		*((s64 *)(data + ((i + 3)/4)*4)) = st->last_timestamp;

	ring->access.store_to(ring,
			      (u8 *)data,
			      st->last_timestamp);

	iio_trigger_notify_done(st->indio_dev->trig);
	kfree(data);

	return;
}

void adis16203_unconfigure_ring(struct iio_dev *indio_dev)
{
	kfree(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->ring);
}

int adis16203_configure_ring(struct iio_dev *indio_dev)
{
	int ret = 0;
	struct adis16203_state *st = indio_dev->dev_data;
	struct iio_ring_buffer *ring;
	INIT_WORK(&st->work_trigger_to_ring, adis16203_trigger_bh_to_ring);

	ring = iio_sw_rb_allocate(indio_dev);
	if (!ring) {
		ret = -ENOMEM;
		return ret;
	}
	indio_dev->ring = ring;
	/* Effectively select the ring buffer implementation */
	iio_ring_sw_register_funcs(&ring->access);
	ring->bpe = 2;
	ring->scan_el_attrs = &adis16203_scan_el_group;
	ring->scan_timestamp = true;
	ring->preenable = &iio_sw_ring_preenable;
	ring->postenable = &iio_triggered_ring_postenable;
	ring->predisable = &iio_triggered_ring_predisable;
	ring->owner = THIS_MODULE;

	/* Set default scan mode */
	iio_scan_mask_set(ring, iio_scan_el_in_supply.number);
	iio_scan_mask_set(ring, iio_scan_el_temp.number);
	iio_scan_mask_set(ring, iio_scan_el_in0.number);
	iio_scan_mask_set(ring, iio_scan_el_incli_x.number);
	iio_scan_mask_set(ring, iio_scan_el_incli_y.number);

	ret = iio_alloc_pollfunc(indio_dev, NULL, &adis16203_poll_func_th);
	if (ret)
		goto error_iio_sw_rb_free;

	indio_dev->modes |= INDIO_RING_TRIGGERED;
	return 0;

error_iio_sw_rb_free:
	iio_sw_rb_free(indio_dev->ring);
	return ret;
}

int adis16203_initialize_ring(struct iio_ring_buffer *ring)
{
	return iio_ring_buffer_register(ring, 0);
}

void adis16203_uninitialize_ring(struct iio_ring_buffer *ring)
{
	iio_ring_buffer_unregister(ring);
}
