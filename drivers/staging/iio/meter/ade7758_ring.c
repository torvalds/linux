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
#include "ade7758.h"

/**
 * combine_8_to_32() utility function to munge to u8s into u32
 **/
static inline u32 combine_8_to_32(u8 lower, u8 mid, u8 upper)
{
	u32 _lower = lower;
	u32 _mid = mid;
	u32 _upper = upper;

	return _lower | (_mid << 8) | (_upper << 16);
}

static IIO_SCAN_EL_C(wform, ADE7758_SCAN_WFORM, ADE7758_WFORM, NULL);
static IIO_CONST_ATTR_SCAN_EL_TYPE(wform, s, 24, 32);
static IIO_SCAN_EL_TIMESTAMP(1);
static IIO_CONST_ATTR_SCAN_EL_TYPE(timestamp, s, 64, 64);

static struct attribute *ade7758_scan_el_attrs[] = {
	&iio_scan_el_wform.dev_attr.attr,
	&iio_const_attr_wform_index.dev_attr.attr,
	&iio_const_attr_wform_type.dev_attr.attr,
	&iio_scan_el_timestamp.dev_attr.attr,
	&iio_const_attr_timestamp_index.dev_attr.attr,
	&iio_const_attr_timestamp_type.dev_attr.attr,
	NULL,
};

static struct attribute_group ade7758_scan_el_group = {
	.attrs = ade7758_scan_el_attrs,
	.name = "scan_elements",
};

/**
 * ade7758_poll_func_th() top half interrupt handler called by trigger
 * @private_data:	iio_dev
 **/
static void ade7758_poll_func_th(struct iio_dev *indio_dev, s64 time)
{
	struct ade7758_state *st = iio_dev_get_devdata(indio_dev);
	st->last_timestamp = time;
	schedule_work(&st->work_trigger_to_ring);
	/* Indicate that this interrupt is being handled */

	/* Technically this is trigger related, but without this
	 * handler running there is currently no way for the interrupt
	 * to clear.
	 */
}

/**
 * ade7758_spi_read_burst() - read all data registers
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @rx: somewhere to pass back the value read (min size is 24 bytes)
 **/
static int ade7758_spi_read_burst(struct device *dev, u8 *rx)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ade7758_state *st = iio_dev_get_devdata(indio_dev);
	int ret;

	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.rx_buf = rx,
			.bits_per_word = 8,
			.len = 4,
		}, {
			.tx_buf = st->tx + 4,
			.rx_buf = rx,
			.bits_per_word = 8,
			.len = 4,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7758_READ_REG(ADE7758_RSTATUS);
	st->tx[1] = 0;
	st->tx[2] = 0;
	st->tx[3] = 0;
	st->tx[4] = ADE7758_READ_REG(ADE7758_WFORM);
	st->tx[5] = 0;
	st->tx[6] = 0;
	st->tx[7] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	if (ret)
		dev_err(&st->us->dev, "problem when reading WFORM value\n");

	mutex_unlock(&st->buf_lock);

	return ret;
}

/* Whilst this makes a lot of calls to iio_sw_ring functions - it is to device
 * specific to be rolled into the core.
 */
static void ade7758_trigger_bh_to_ring(struct work_struct *work_s)
{
	struct ade7758_state *st
		= container_of(work_s, struct ade7758_state,
			       work_trigger_to_ring);
	struct iio_ring_buffer *ring = st->indio_dev->ring;

	int i = 0;
	s32 *data;
	size_t datasize = ring->access.get_bytes_per_datum(ring);

	data = kmalloc(datasize, GFP_KERNEL);
	if (data == NULL) {
		dev_err(&st->us->dev, "memory alloc failed in ring bh");
		return;
	}

	if (ring->scan_count)
		if (ade7758_spi_read_burst(&st->indio_dev->dev, st->rx) >= 0)
			for (; i < ring->scan_count; i++)
				data[i] = combine_8_to_32(st->rx[i*2+2],
						st->rx[i*2+1],
						st->rx[i*2]);

	/* Guaranteed to be aligned with 8 byte boundary */
	if (ring->scan_timestamp)
		*((s64 *)
		(((u32)data + 4 * ring->scan_count + 4) & ~0x7)) =
			st->last_timestamp;

	ring->access.store_to(ring,
			      (u8 *)data,
			      st->last_timestamp);

	iio_trigger_notify_done(st->indio_dev->trig);
	kfree(data);

	return;
}

void ade7758_unconfigure_ring(struct iio_dev *indio_dev)
{
	kfree(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->ring);
}

int ade7758_configure_ring(struct iio_dev *indio_dev)
{
	int ret = 0;
	struct ade7758_state *st = indio_dev->dev_data;
	struct iio_ring_buffer *ring;
	INIT_WORK(&st->work_trigger_to_ring, ade7758_trigger_bh_to_ring);

	ring = iio_sw_rb_allocate(indio_dev);
	if (!ring) {
		ret = -ENOMEM;
		return ret;
	}
	indio_dev->ring = ring;
	/* Effectively select the ring buffer implementation */
	iio_ring_sw_register_funcs(&ring->access);
	ring->bpe = 4;
	ring->scan_el_attrs = &ade7758_scan_el_group;
	ring->scan_timestamp = true;
	ring->preenable = &iio_sw_ring_preenable;
	ring->postenable = &iio_triggered_ring_postenable;
	ring->predisable = &iio_triggered_ring_predisable;
	ring->owner = THIS_MODULE;

	/* Set default scan mode */
	iio_scan_mask_set(ring, iio_scan_el_wform.number);

	ret = iio_alloc_pollfunc(indio_dev, NULL, &ade7758_poll_func_th);
	if (ret)
		goto error_iio_sw_rb_free;

	indio_dev->modes |= INDIO_RING_TRIGGERED;
	return 0;

error_iio_sw_rb_free:
	iio_sw_rb_free(indio_dev->ring);
	return ret;
}

int ade7758_initialize_ring(struct iio_ring_buffer *ring)
{
	return iio_ring_buffer_register(ring, 0);
}

void ade7758_uninitialize_ring(struct iio_ring_buffer *ring)
{
	iio_ring_buffer_unregister(ring);
}
