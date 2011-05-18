#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/slab.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../ring_sw.h"
#include "../kfifo_buf.h"
#include "accel.h"
#include "../trigger.h"
#include "lis3l02dq.h"

/**
 * combine_8_to_16() utility function to munge to u8s into u16
 **/
static inline u16 combine_8_to_16(u8 lower, u8 upper)
{
	u16 _lower = lower;
	u16 _upper = upper;
	return _lower | (_upper << 8);
}

/**
 * lis3l02dq_data_rdy_trig_poll() the event handler for the data rdy trig
 **/
irqreturn_t lis3l02dq_data_rdy_trig_poll(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct iio_sw_ring_helper_state *h =  iio_dev_get_devdata(indio_dev);
	struct lis3l02dq_state *st = lis3l02dq_h_to_s(h);

	if (st->trigger_on) {
		iio_trigger_poll(st->trig, iio_get_time_ns());
		return IRQ_HANDLED;
	} else
		return IRQ_WAKE_THREAD;
}

/**
 * lis3l02dq_read_accel_from_ring() individual acceleration read from ring
 **/
ssize_t lis3l02dq_read_accel_from_ring(struct iio_ring_buffer *ring,
				       int index,
				       int *val)
{
	int ret;
	s16 *data;
	if (!iio_scan_mask_query(ring, index))
		return -EINVAL;

	data = kmalloc(ring->access.get_bytes_per_datum(ring),
		       GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	ret = ring->access.read_last(ring, (u8 *)data);
	if (ret)
		goto error_free_data;
	*val = data[iio_scan_mask_count_to_right(ring, index)];
error_free_data:
	kfree(data);
	return ret;
}

static const u8 read_all_tx_array[] = {
	LIS3L02DQ_READ_REG(LIS3L02DQ_REG_OUT_X_L_ADDR), 0,
	LIS3L02DQ_READ_REG(LIS3L02DQ_REG_OUT_X_H_ADDR), 0,
	LIS3L02DQ_READ_REG(LIS3L02DQ_REG_OUT_Y_L_ADDR), 0,
	LIS3L02DQ_READ_REG(LIS3L02DQ_REG_OUT_Y_H_ADDR), 0,
	LIS3L02DQ_READ_REG(LIS3L02DQ_REG_OUT_Z_L_ADDR), 0,
	LIS3L02DQ_READ_REG(LIS3L02DQ_REG_OUT_Z_H_ADDR), 0,
};

/**
 * lis3l02dq_read_all() Reads all channels currently selected
 * @st:		device specific state
 * @rx_array:	(dma capable) receive array, must be at least
 *		4*number of channels
 **/
static int lis3l02dq_read_all(struct lis3l02dq_state *st, u8 *rx_array)
{
	struct iio_ring_buffer *ring = st->help.indio_dev->ring;
	struct spi_transfer *xfers;
	struct spi_message msg;
	int ret, i, j = 0;

	xfers = kzalloc((ring->scan_count) * 2
			* sizeof(*xfers), GFP_KERNEL);
	if (!xfers)
		return -ENOMEM;

	mutex_lock(&st->buf_lock);

	for (i = 0; i < ARRAY_SIZE(read_all_tx_array)/4; i++)
		if (ring->scan_mask & (1 << i)) {
			/* lower byte */
			xfers[j].tx_buf = st->tx + 2*j;
			st->tx[2*j] = read_all_tx_array[i*4];
			st->tx[2*j + 1] = 0;
			if (rx_array)
				xfers[j].rx_buf = rx_array + j*2;
			xfers[j].bits_per_word = 8;
			xfers[j].len = 2;
			xfers[j].cs_change = 1;
			j++;

			/* upper byte */
			xfers[j].tx_buf = st->tx + 2*j;
			st->tx[2*j] = read_all_tx_array[i*4 + 2];
			st->tx[2*j + 1] = 0;
			if (rx_array)
				xfers[j].rx_buf = rx_array + j*2;
			xfers[j].bits_per_word = 8;
			xfers[j].len = 2;
			xfers[j].cs_change = 1;
			j++;
		}

	/* After these are transmitted, the rx_buff should have
	 * values in alternate bytes
	 */
	spi_message_init(&msg);
	for (j = 0; j < ring->scan_count * 2; j++)
		spi_message_add_tail(&xfers[j], &msg);

	ret = spi_sync(st->us, &msg);
	mutex_unlock(&st->buf_lock);
	kfree(xfers);

	return ret;
}

static irqreturn_t lis3l02dq_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->private_data;
	struct iio_sw_ring_helper_state *h = iio_dev_get_devdata(indio_dev);

	h->last_timestamp = pf->timestamp;
	iio_sw_trigger_to_ring(h);

	return IRQ_HANDLED;
}

static int lis3l02dq_get_ring_element(struct iio_sw_ring_helper_state *h,
				u8 *buf)
{
	int ret, i;
	u8 *rx_array ;
	s16 *data = (s16 *)buf;

	rx_array = kzalloc(4 * (h->indio_dev->ring->scan_count), GFP_KERNEL);
	if (rx_array == NULL)
		return -ENOMEM;
	ret = lis3l02dq_read_all(lis3l02dq_h_to_s(h), rx_array);
	if (ret < 0)
		return ret;
	for (i = 0; i < h->indio_dev->ring->scan_count; i++)
		data[i] = combine_8_to_16(rx_array[i*4+1],
					rx_array[i*4+3]);
	kfree(rx_array);

	return i*sizeof(data[0]);
}

/* Caller responsible for locking as necessary. */
static int
__lis3l02dq_write_data_ready_config(struct device *dev, bool state)
{
	int ret;
	u8 valold;
	bool currentlyset;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_sw_ring_helper_state *h
				= iio_dev_get_devdata(indio_dev);
	struct lis3l02dq_state *st = lis3l02dq_h_to_s(h);

/* Get the current event mask register */
	ret = lis3l02dq_spi_read_reg_8(indio_dev,
				       LIS3L02DQ_REG_CTRL_2_ADDR,
				       &valold);
	if (ret)
		goto error_ret;
/* Find out if data ready is already on */
	currentlyset
		= valold & LIS3L02DQ_REG_CTRL_2_ENABLE_DATA_READY_GENERATION;

/* Disable requested */
	if (!state && currentlyset) {
		/* disable the data ready signal */
		valold &= ~LIS3L02DQ_REG_CTRL_2_ENABLE_DATA_READY_GENERATION;

		/* The double write is to overcome a hardware bug?*/
		ret = lis3l02dq_spi_write_reg_8(indio_dev,
						LIS3L02DQ_REG_CTRL_2_ADDR,
						&valold);
		if (ret)
			goto error_ret;
		ret = lis3l02dq_spi_write_reg_8(indio_dev,
						LIS3L02DQ_REG_CTRL_2_ADDR,
						&valold);
		if (ret)
			goto error_ret;
		st->trigger_on = false;
/* Enable requested */
	} else if (state && !currentlyset) {
		/* if not set, enable requested */
		/* first disable all events */
		ret = lis3l02dq_disable_all_events(indio_dev);
		if (ret < 0)
			goto error_ret;

		valold = ret |
			LIS3L02DQ_REG_CTRL_2_ENABLE_DATA_READY_GENERATION;

		st->trigger_on = true;
		ret = lis3l02dq_spi_write_reg_8(indio_dev,
						LIS3L02DQ_REG_CTRL_2_ADDR,
						&valold);
		if (ret)
			goto error_ret;
	}

	return 0;
error_ret:
	return ret;
}

/**
 * lis3l02dq_data_rdy_trigger_set_state() set datardy interrupt state
 *
 * If disabling the interrupt also does a final read to ensure it is clear.
 * This is only important in some cases where the scan enable elements are
 * switched before the ring is reenabled.
 **/
static int lis3l02dq_data_rdy_trigger_set_state(struct iio_trigger *trig,
						bool state)
{
	struct lis3l02dq_state *st = trig->private_data;
	int ret = 0;
	u8 t;

	__lis3l02dq_write_data_ready_config(&st->help.indio_dev->dev, state);
	if (state == false) {
		/*
		 * A possible quirk with teh handler is currently worked around
		 *  by ensuring outstanding read events are cleared.
		 */
		ret = lis3l02dq_read_all(st, NULL);
	}
	lis3l02dq_spi_read_reg_8(st->help.indio_dev,
				 LIS3L02DQ_REG_WAKE_UP_SRC_ADDR,
				 &t);
	return ret;
}

static IIO_TRIGGER_NAME_ATTR;

static struct attribute *lis3l02dq_trigger_attrs[] = {
	&dev_attr_name.attr,
	NULL,
};

static const struct attribute_group lis3l02dq_trigger_attr_group = {
	.attrs = lis3l02dq_trigger_attrs,
};

/**
 * lis3l02dq_trig_try_reen() try renabling irq for data rdy trigger
 * @trig:	the datardy trigger
 */
static int lis3l02dq_trig_try_reen(struct iio_trigger *trig)
{
	struct lis3l02dq_state *st = trig->private_data;
	int i;

	/* If gpio still high (or high again) */
	/* In theory possible we will need to do this several times */
	for (i = 0; i < 5; i++)
		if (gpio_get_value(irq_to_gpio(st->us->irq)))
			lis3l02dq_read_all(st, NULL);
		else
			break;
	if (i == 5)
		printk(KERN_INFO
		       "Failed to clear the interrupt for lis3l02dq\n");

	/* irq reenabled so success! */
	return 0;
}

int lis3l02dq_probe_trigger(struct iio_dev *indio_dev)
{
	int ret;
	struct iio_sw_ring_helper_state *h
		= iio_dev_get_devdata(indio_dev);
	struct lis3l02dq_state *st = lis3l02dq_h_to_s(h);
	char *name;

	name = kasprintf(GFP_KERNEL,
			 "lis3l02dq-dev%d",
			 indio_dev->id);
	if (name == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	st->trig = iio_allocate_trigger_named(name);
	if (!st->trig) {
		ret = -ENOMEM;
		goto error_free_name;
	}

	st->trig->dev.parent = &st->us->dev;
	st->trig->owner = THIS_MODULE;
	st->trig->private_data = st;
	st->trig->set_trigger_state = &lis3l02dq_data_rdy_trigger_set_state;
	st->trig->try_reenable = &lis3l02dq_trig_try_reen;
	st->trig->control_attrs = &lis3l02dq_trigger_attr_group;
	ret = iio_trigger_register(st->trig);
	if (ret)
		goto error_free_trig;

	return 0;

error_free_trig:
	iio_free_trigger(st->trig);
error_free_name:
	kfree(name);
error_ret:
	return ret;
}

void lis3l02dq_remove_trigger(struct iio_dev *indio_dev)
{
	struct iio_sw_ring_helper_state *h
		= iio_dev_get_devdata(indio_dev);
	struct lis3l02dq_state *st = lis3l02dq_h_to_s(h);

	iio_trigger_unregister(st->trig);
	kfree(st->trig->name);
	iio_free_trigger(st->trig);
}

void lis3l02dq_unconfigure_ring(struct iio_dev *indio_dev)
{
	kfree(indio_dev->pollfunc->name);
	kfree(indio_dev->pollfunc);
	lis3l02dq_free_buf(indio_dev->ring);
}

static int lis3l02dq_ring_postenable(struct iio_dev *indio_dev)
{
	/* Disable unwanted channels otherwise the interrupt will not clear */
	u8 t;
	int ret;
	bool oneenabled = false;

	ret = lis3l02dq_spi_read_reg_8(indio_dev,
				       LIS3L02DQ_REG_CTRL_1_ADDR,
				       &t);
	if (ret)
		goto error_ret;

	if (iio_scan_mask_query(indio_dev->ring, 0)) {
		t |= LIS3L02DQ_REG_CTRL_1_AXES_X_ENABLE;
		oneenabled = true;
	} else
		t &= ~LIS3L02DQ_REG_CTRL_1_AXES_X_ENABLE;
	if (iio_scan_mask_query(indio_dev->ring, 1)) {
		t |= LIS3L02DQ_REG_CTRL_1_AXES_Y_ENABLE;
		oneenabled = true;
	} else
		t &= ~LIS3L02DQ_REG_CTRL_1_AXES_Y_ENABLE;
	if (iio_scan_mask_query(indio_dev->ring, 2)) {
		t |= LIS3L02DQ_REG_CTRL_1_AXES_Z_ENABLE;
		oneenabled = true;
	} else
		t &= ~LIS3L02DQ_REG_CTRL_1_AXES_Z_ENABLE;

	if (!oneenabled) /* what happens in this case is unknown */
		return -EINVAL;
	ret = lis3l02dq_spi_write_reg_8(indio_dev,
					LIS3L02DQ_REG_CTRL_1_ADDR,
					&t);
	if (ret)
		goto error_ret;

	return iio_triggered_ring_postenable(indio_dev);
error_ret:
	return ret;
}

/* Turn all channels on again */
static int lis3l02dq_ring_predisable(struct iio_dev *indio_dev)
{
	u8 t;
	int ret;

	ret = iio_triggered_ring_predisable(indio_dev);
	if (ret)
		goto error_ret;

	ret = lis3l02dq_spi_read_reg_8(indio_dev,
				       LIS3L02DQ_REG_CTRL_1_ADDR,
				       &t);
	if (ret)
		goto error_ret;
	t |= LIS3L02DQ_REG_CTRL_1_AXES_X_ENABLE |
		LIS3L02DQ_REG_CTRL_1_AXES_Y_ENABLE |
		LIS3L02DQ_REG_CTRL_1_AXES_Z_ENABLE;

	ret = lis3l02dq_spi_write_reg_8(indio_dev,
					LIS3L02DQ_REG_CTRL_1_ADDR,
					&t);

error_ret:
	return ret;
}


int lis3l02dq_configure_ring(struct iio_dev *indio_dev)
{
	int ret;
	struct iio_sw_ring_helper_state *h = iio_dev_get_devdata(indio_dev);
	struct iio_ring_buffer *ring;

	h->get_ring_element = &lis3l02dq_get_ring_element;

	ring = lis3l02dq_alloc_buf(indio_dev);
	if (!ring)
		return -ENOMEM;

	indio_dev->ring = ring;
	/* Effectively select the ring buffer implementation */
	lis3l02dq_register_buf_funcs(&ring->access);
	ring->bpe = 2;

	ring->scan_timestamp = true;
	ring->preenable = &iio_sw_ring_preenable;
	ring->postenable = &lis3l02dq_ring_postenable;
	ring->predisable = &lis3l02dq_ring_predisable;
	ring->owner = THIS_MODULE;

	/* Set default scan mode */
	iio_scan_mask_set(ring, 0);
	iio_scan_mask_set(ring, 1);
	iio_scan_mask_set(ring, 2);

	/* Functions are NULL as we set handler below */
	indio_dev->pollfunc = kzalloc(sizeof(*indio_dev->pollfunc), GFP_KERNEL);

	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_iio_sw_rb_free;
	}
	indio_dev->pollfunc->private_data = indio_dev;
	indio_dev->pollfunc->thread = &lis3l02dq_trigger_handler;
	indio_dev->pollfunc->h = &iio_pollfunc_store_time;
	indio_dev->pollfunc->type = 0;
	indio_dev->pollfunc->name
		= kasprintf(GFP_KERNEL, "lis3l02dq_consumer%d", indio_dev->id);

	indio_dev->modes |= INDIO_RING_TRIGGERED;
	return 0;

error_iio_sw_rb_free:
	lis3l02dq_free_buf(indio_dev->ring);
	return ret;
}
