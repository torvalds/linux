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
#include "../accel/accel.h"
#include "../trigger.h"
#include "adis16400.h"

static IIO_SCAN_EL_C(supply, ADIS16400_SCAN_SUPPLY, IIO_SIGNED(14),
		     ADIS16400_SUPPLY_OUT, NULL);

static IIO_SCAN_EL_C(gyro_x, ADIS16400_SCAN_GYRO_X, IIO_SIGNED(14),
		     ADIS16400_XGYRO_OUT, NULL);
static IIO_SCAN_EL_C(gyro_y, ADIS16400_SCAN_GYRO_Y, IIO_SIGNED(14),
		     ADIS16400_YGYRO_OUT, NULL);
static IIO_SCAN_EL_C(gyro_z, ADIS16400_SCAN_GYRO_Z, IIO_SIGNED(14),
		     ADIS16400_ZGYRO_OUT, NULL);

static IIO_SCAN_EL_C(accel_x, ADIS16400_SCAN_ACC_X, IIO_SIGNED(14),
		     ADIS16400_XACCL_OUT, NULL);
static IIO_SCAN_EL_C(accel_y, ADIS16400_SCAN_ACC_Y, IIO_SIGNED(14),
		     ADIS16400_YACCL_OUT, NULL);
static IIO_SCAN_EL_C(accel_z, ADIS16400_SCAN_ACC_Z, IIO_SIGNED(14),
		     ADIS16400_ZACCL_OUT, NULL);

static IIO_SCAN_EL_C(magn_x, ADIS16400_SCAN_MAGN_X, IIO_SIGNED(14),
		     ADIS16400_XMAGN_OUT, NULL);
static IIO_SCAN_EL_C(magn_y, ADIS16400_SCAN_MAGN_Y, IIO_SIGNED(14),
		     ADIS16400_YMAGN_OUT, NULL);
static IIO_SCAN_EL_C(magn_z, ADIS16400_SCAN_MAGN_Z, IIO_SIGNED(14),
		     ADIS16400_ZMAGN_OUT, NULL);

static IIO_SCAN_EL_C(temp, ADIS16400_SCAN_TEMP, IIO_SIGNED(12),
		     ADIS16400_TEMP_OUT, NULL);
static IIO_SCAN_EL_C(adc_0, ADIS16400_SCAN_ADC_0, IIO_SIGNED(12),
		     ADIS16400_AUX_ADC, NULL);

static IIO_SCAN_EL_TIMESTAMP(12);

static struct attribute *adis16400_scan_el_attrs[] = {
	&iio_scan_el_supply.dev_attr.attr,
	&iio_scan_el_gyro_x.dev_attr.attr,
	&iio_scan_el_gyro_y.dev_attr.attr,
	&iio_scan_el_gyro_z.dev_attr.attr,
	&iio_scan_el_accel_x.dev_attr.attr,
	&iio_scan_el_accel_y.dev_attr.attr,
	&iio_scan_el_accel_z.dev_attr.attr,
	&iio_scan_el_magn_x.dev_attr.attr,
	&iio_scan_el_magn_y.dev_attr.attr,
	&iio_scan_el_magn_z.dev_attr.attr,
	&iio_scan_el_temp.dev_attr.attr,
	&iio_scan_el_adc_0.dev_attr.attr,
	&iio_scan_el_timestamp.dev_attr.attr,
	NULL,
};

static struct attribute_group adis16400_scan_el_group = {
	.attrs = adis16400_scan_el_attrs,
	.name = "scan_elements",
};

/**
 * adis16400_poll_func_th() top half interrupt handler called by trigger
 * @private_data:	iio_dev
 **/
static void adis16400_poll_func_th(struct iio_dev *indio_dev, s64 time)
{
	struct adis16400_state *st = iio_dev_get_devdata(indio_dev);
	st->last_timestamp = time;
	schedule_work(&st->work_trigger_to_ring);
	/* Indicate that this interrupt is being handled */

	/* Technically this is trigger related, but without this
	 * handler running there is currently no way for the interrupt
	 * to clear.
	 */
}

/**
 * adis16400_spi_read_burst() - read all data registers
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @rx: somewhere to pass back the value read (min size is 24 bytes)
 **/
static int adis16400_spi_read_burst(struct device *dev, u8 *rx)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16400_state *st = iio_dev_get_devdata(indio_dev);
	u32 old_speed_hz = st->us->max_speed_hz;
	int ret;

	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 0,
		}, {
			.rx_buf = rx,
			.bits_per_word = 8,
			.len = 24,
			.cs_change = 1,
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

/* Whilst this makes a lot of calls to iio_sw_ring functions - it is to device
 * specific to be rolled into the core.
 */
static void adis16400_trigger_bh_to_ring(struct work_struct *work_s)
{
	struct adis16400_state *st
		= container_of(work_s, struct adis16400_state,
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
		if (adis16400_spi_read_burst(&st->indio_dev->dev, st->rx) >= 0)
			for (; i < st->indio_dev->scan_count; i++)
				data[i]	= be16_to_cpup(
					(__be16 *)&(st->rx[i*2]));

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

void adis16400_unconfigure_ring(struct iio_dev *indio_dev)
{
	kfree(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->ring);
}

int adis16400_configure_ring(struct iio_dev *indio_dev)
{
	int ret = 0;
	struct adis16400_state *st = indio_dev->dev_data;
	struct iio_ring_buffer *ring;
	INIT_WORK(&st->work_trigger_to_ring, adis16400_trigger_bh_to_ring);
	/* Set default scan mode */

	iio_scan_mask_set(indio_dev, iio_scan_el_supply.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_gyro_x.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_gyro_y.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_gyro_z.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_accel_x.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_accel_y.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_accel_z.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_magn_x.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_magn_y.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_magn_z.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_temp.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_adc_0.number);
	indio_dev->scan_timestamp = true;

	indio_dev->scan_el_attrs = &adis16400_scan_el_group;

	ring = iio_sw_rb_allocate(indio_dev);
	if (!ring) {
		ret = -ENOMEM;
		return ret;
	}
	indio_dev->ring = ring;
	/* Effectively select the ring buffer implementation */
	iio_ring_sw_register_funcs(&ring->access);
	ring->bpe = 2;
	ring->preenable = &iio_sw_ring_preenable;
	ring->postenable = &iio_triggered_ring_postenable;
	ring->predisable = &iio_triggered_ring_predisable;
	ring->owner = THIS_MODULE;

	ret = iio_alloc_pollfunc(indio_dev, NULL, &adis16400_poll_func_th);
	if (ret)
		goto error_iio_sw_rb_free;

	indio_dev->modes |= INDIO_RING_TRIGGERED;
	return 0;

error_iio_sw_rb_free:
	iio_sw_rb_free(indio_dev->ring);
	return ret;
}
