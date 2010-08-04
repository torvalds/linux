#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/list.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../ring_sw.h"
#include "accel.h"
#include "../trigger.h"
#include "adis16209.h"

/**
 * combine_8_to_16() utility function to munge to u8s into u16
 **/
static inline u16 combine_8_to_16(u8 lower, u8 upper)
{
	u16 _lower = lower;
	u16 _upper = upper;
	return _lower | (_upper << 8);
}

static IIO_SCAN_EL_C(supply, ADIS16209_SCAN_SUPPLY, IIO_UNSIGNED(14),
		     ADIS16209_SUPPLY_OUT, NULL);
static IIO_SCAN_EL_C(accel_x, ADIS16209_SCAN_ACC_X, IIO_SIGNED(14),
		     ADIS16209_XACCL_OUT, NULL);
static IIO_SCAN_EL_C(accel_y, ADIS16209_SCAN_ACC_Y, IIO_SIGNED(14),
		     ADIS16209_YACCL_OUT, NULL);
static IIO_SCAN_EL_C(aux_adc, ADIS16209_SCAN_AUX_ADC, IIO_UNSIGNED(12),
		     ADIS16209_AUX_ADC, NULL);
static IIO_SCAN_EL_C(temp, ADIS16209_SCAN_TEMP, IIO_UNSIGNED(12),
		     ADIS16209_TEMP_OUT, NULL);
static IIO_SCAN_EL_C(incli_x, ADIS16209_SCAN_INCLI_X, IIO_SIGNED(14),
		     ADIS16209_XINCL_OUT, NULL);
static IIO_SCAN_EL_C(incli_y, ADIS16209_SCAN_INCLI_Y, IIO_SIGNED(14),
		     ADIS16209_YINCL_OUT, NULL);
static IIO_SCAN_EL_C(rot, ADIS16209_SCAN_ROT, IIO_SIGNED(14),
		     ADIS16209_ROT_OUT, NULL);

static IIO_SCAN_EL_TIMESTAMP(8);

static struct attribute *adis16209_scan_el_attrs[] = {
	&iio_scan_el_supply.dev_attr.attr,
	&iio_scan_el_accel_x.dev_attr.attr,
	&iio_scan_el_accel_y.dev_attr.attr,
	&iio_scan_el_aux_adc.dev_attr.attr,
	&iio_scan_el_temp.dev_attr.attr,
	&iio_scan_el_incli_x.dev_attr.attr,
	&iio_scan_el_incli_y.dev_attr.attr,
	&iio_scan_el_rot.dev_attr.attr,
	&iio_scan_el_timestamp.dev_attr.attr,
	NULL,
};

static struct attribute_group adis16209_scan_el_group = {
	.attrs = adis16209_scan_el_attrs,
	.name = "scan_elements",
};

/**
 * adis16209_poll_func_th() top half interrupt handler called by trigger
 * @private_data:	iio_dev
 **/
static void adis16209_poll_func_th(struct iio_dev *indio_dev)
{
	struct adis16209_state *st = iio_dev_get_devdata(indio_dev);
	st->last_timestamp = indio_dev->trig->timestamp;
	schedule_work(&st->work_trigger_to_ring);
}

/**
 * adis16209_read_ring_data() read data registers which will be placed into ring
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @rx: somewhere to pass back the value read
 **/
static int adis16209_read_ring_data(struct device *dev, u8 *rx)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16209_state *st = iio_dev_get_devdata(indio_dev);
	struct spi_transfer xfers[ADIS16209_OUTPUTS + 1];
	int ret;
	int i;

	mutex_lock(&st->buf_lock);

	spi_message_init(&msg);

	memset(xfers, 0, sizeof(xfers));
	for (i = 0; i <= ADIS16209_OUTPUTS; i++) {
		xfers[i].bits_per_word = 8;
		xfers[i].cs_change = 1;
		xfers[i].len = 2;
		xfers[i].delay_usecs = 20;
		xfers[i].tx_buf = st->tx + 2 * i;
		st->tx[2 * i]
			= ADIS16209_READ_REG(ADIS16209_SUPPLY_OUT + 2 * i);
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
static void adis16209_trigger_bh_to_ring(struct work_struct *work_s)
{
	struct adis16209_state *st
		= container_of(work_s, struct adis16209_state,
			       work_trigger_to_ring);

	int i = 0;
	s16 *data;
	size_t datasize = st->indio_dev
		->ring->access.get_bpd(st->indio_dev->ring);

	data = kmalloc(datasize , GFP_KERNEL);
	if (data == NULL) {
		dev_err(&st->us->dev, "memory alloc failed in ring bh");
		return;
	}

	if (st->indio_dev->scan_count)
		if (adis16209_read_ring_data(&st->indio_dev->dev, st->rx) >= 0)
			for (; i < st->indio_dev->scan_count; i++) {
				data[i] = combine_8_to_16(st->rx[i*2+1],
							  st->rx[i*2]);
			}

	/* Guaranteed to be aligned with 8 byte boundary */
	if (st->indio_dev->scan_timestamp)
		*((s64 *)(data + ((i + 3)/4)*4)) = st->last_timestamp;

	st->indio_dev->ring->access.store_to(st->indio_dev->ring,
					    (u8 *)data,
					    st->last_timestamp);

	iio_trigger_notify_done(st->indio_dev->trig);
	kfree(data);

	return;
}

/* in these circumstances is it better to go with unaligned packing and
 * deal with the cost?*/
static int adis16209_data_rdy_ring_preenable(struct iio_dev *indio_dev)
{
	size_t size;
	dev_dbg(&indio_dev->dev, "%s\n", __func__);
	/* Check if there are any scan elements enabled, if not fail*/
	if (!(indio_dev->scan_count || indio_dev->scan_timestamp))
		return -EINVAL;

	if (indio_dev->ring->access.set_bpd) {
		if (indio_dev->scan_timestamp)
			if (indio_dev->scan_count)
				/* Timestamp (aligned to s64) and data */
				size = (((indio_dev->scan_count * sizeof(s16))
					 + sizeof(s64) - 1)
					& ~(sizeof(s64) - 1))
					+ sizeof(s64);
			else /* Timestamp only  */
				size = sizeof(s64);
		else /* Data only */
			size = indio_dev->scan_count*sizeof(s16);
		indio_dev->ring->access.set_bpd(indio_dev->ring, size);
	}

	return 0;
}

static int adis16209_data_rdy_ring_postenable(struct iio_dev *indio_dev)
{
	return indio_dev->trig
		? iio_trigger_attach_poll_func(indio_dev->trig,
					       indio_dev->pollfunc)
		: 0;
}

static int adis16209_data_rdy_ring_predisable(struct iio_dev *indio_dev)
{
	return indio_dev->trig
		? iio_trigger_dettach_poll_func(indio_dev->trig,
						indio_dev->pollfunc)
		: 0;
}

void adis16209_unconfigure_ring(struct iio_dev *indio_dev)
{
	kfree(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->ring);
}

int adis16209_configure_ring(struct iio_dev *indio_dev)
{
	int ret = 0;
	struct adis16209_state *st = indio_dev->dev_data;
	struct iio_ring_buffer *ring;
	INIT_WORK(&st->work_trigger_to_ring, adis16209_trigger_bh_to_ring);
	/* Set default scan mode */

	iio_scan_mask_set(indio_dev, iio_scan_el_supply.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_rot.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_accel_x.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_accel_y.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_temp.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_aux_adc.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_incli_x.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_incli_y.number);
	indio_dev->scan_timestamp = true;

	indio_dev->scan_el_attrs = &adis16209_scan_el_group;

	ring = iio_sw_rb_allocate(indio_dev);
	if (!ring) {
		ret = -ENOMEM;
		return ret;
	}
	indio_dev->ring = ring;
	/* Effectively select the ring buffer implementation */
	iio_ring_sw_register_funcs(&ring->access);
	ring->preenable = &adis16209_data_rdy_ring_preenable;
	ring->postenable = &adis16209_data_rdy_ring_postenable;
	ring->predisable = &adis16209_data_rdy_ring_predisable;
	ring->owner = THIS_MODULE;

	indio_dev->pollfunc = kzalloc(sizeof(*indio_dev->pollfunc), GFP_KERNEL);
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_iio_sw_rb_free;;
	}
	indio_dev->pollfunc->poll_func_main = &adis16209_poll_func_th;
	indio_dev->pollfunc->private_data = indio_dev;
	indio_dev->modes |= INDIO_RING_TRIGGERED;
	return 0;

error_iio_sw_rb_free:
	iio_sw_rb_free(indio_dev->ring);
	return ret;
}

int adis16209_initialize_ring(struct iio_ring_buffer *ring)
{
	return iio_ring_buffer_register(ring, 0);
}

void adis16209_uninitialize_ring(struct iio_ring_buffer *ring)
{
	iio_ring_buffer_unregister(ring);
}
