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
#include <linux/slab.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../ring_sw.h"
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
 * lis3l02dq_scan_el_set_state() set whether a scan contains a given channel
 * @scan_el:	associtate iio scan element attribute
 * @indio_dev:	the device structure
 * @bool:	desired state
 *
 * mlock already held when this is called.
 **/
static int lis3l02dq_scan_el_set_state(struct iio_scan_el *scan_el,
				       struct iio_dev *indio_dev,
				       bool state)
{
	u8 t, mask;
	int ret;

	ret = lis3l02dq_spi_read_reg_8(&indio_dev->dev,
				       LIS3L02DQ_REG_CTRL_1_ADDR,
				       &t);
	if (ret)
		goto error_ret;
	switch (scan_el->label) {
	case LIS3L02DQ_REG_OUT_X_L_ADDR:
		mask = LIS3L02DQ_REG_CTRL_1_AXES_X_ENABLE;
		break;
	case LIS3L02DQ_REG_OUT_Y_L_ADDR:
		mask = LIS3L02DQ_REG_CTRL_1_AXES_Y_ENABLE;
		break;
	case LIS3L02DQ_REG_OUT_Z_L_ADDR:
		mask = LIS3L02DQ_REG_CTRL_1_AXES_Z_ENABLE;
		break;
	default:
		ret = -EINVAL;
		goto error_ret;
	}

	if (!(mask & t) == state) {
		if (state)
			t |= mask;
		else
			t &= ~mask;
		ret = lis3l02dq_spi_write_reg_8(&indio_dev->dev,
						LIS3L02DQ_REG_CTRL_1_ADDR,
						&t);
	}
error_ret:
	return ret;

}
static IIO_SCAN_EL_C(accel_x, LIS3L02DQ_SCAN_ACC_X, IIO_SIGNED(16),
		     LIS3L02DQ_REG_OUT_X_L_ADDR,
		     &lis3l02dq_scan_el_set_state);
static IIO_SCAN_EL_C(accel_y, LIS3L02DQ_SCAN_ACC_Y, IIO_SIGNED(16),
		     LIS3L02DQ_REG_OUT_Y_L_ADDR,
		     &lis3l02dq_scan_el_set_state);
static IIO_SCAN_EL_C(accel_z, LIS3L02DQ_SCAN_ACC_Z, IIO_SIGNED(16),
		     LIS3L02DQ_REG_OUT_Z_L_ADDR,
		     &lis3l02dq_scan_el_set_state);
static IIO_SCAN_EL_TIMESTAMP;

static struct attribute *lis3l02dq_scan_el_attrs[] = {
	&iio_scan_el_accel_x.dev_attr.attr,
	&iio_scan_el_accel_y.dev_attr.attr,
	&iio_scan_el_accel_z.dev_attr.attr,
	&iio_scan_el_timestamp.dev_attr.attr,
	NULL,
};

static struct attribute_group lis3l02dq_scan_el_group = {
	.attrs = lis3l02dq_scan_el_attrs,
	.name = "scan_elements",
};

/**
 * lis3l02dq_poll_func_th() top half interrupt handler called by trigger
 * @private_data:	iio_dev
 **/
static void lis3l02dq_poll_func_th(struct iio_dev *indio_dev)
{
  struct lis3l02dq_state *st = iio_dev_get_devdata(indio_dev);
	st->last_timestamp = indio_dev->trig->timestamp;
	schedule_work(&st->work_trigger_to_ring);
	/* Indicate that this interrupt is being handled */

	/* Technically this is trigger related, but without this
	 * handler running there is currently now way for the interrupt
	 * to clear.
	 */
	st->inter = 1;
}

/**
 * lis3l02dq_data_rdy_trig_poll() the event handler for the data rdy trig
 **/
static int lis3l02dq_data_rdy_trig_poll(struct iio_dev *dev_info,
				       int index,
				       s64 timestamp,
				       int no_test)
{
	struct lis3l02dq_state *st = iio_dev_get_devdata(dev_info);
	struct iio_trigger *trig = st->trig;

	trig->timestamp = timestamp;
	iio_trigger_poll(trig);

	return IRQ_HANDLED;
}

/* This is an event as it is a response to a physical interrupt */
IIO_EVENT_SH(data_rdy_trig, &lis3l02dq_data_rdy_trig_poll);

/**
 * lis3l02dq_read_accel_from_ring() individual acceleration read from ring
 **/
ssize_t lis3l02dq_read_accel_from_ring(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct iio_scan_el *el = NULL;
	int ret, len = 0, i = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	s16 *data;

	while (dev_info->scan_el_attrs->attrs[i]) {
		el = to_iio_scan_el((struct device_attribute *)
				    (dev_info->scan_el_attrs->attrs[i]));
		/* label is in fact the address */
		if (el->label == this_attr->address)
			break;
		i++;
	}
	if (!dev_info->scan_el_attrs->attrs[i]) {
		ret = -EINVAL;
		goto error_ret;
	}
	/* If this element is in the scan mask */
	ret = iio_scan_mask_query(dev_info, el->number);
	if (ret < 0)
		goto error_ret;
	if (ret) {
		data = kmalloc(dev_info->ring->access.get_bpd(dev_info->ring),
			       GFP_KERNEL);
		if (data == NULL)
			return -ENOMEM;
		ret = dev_info->ring->access.read_last(dev_info->ring,
						      (u8 *)data);
		if (ret)
			goto error_free_data;
	} else {
		ret = -EINVAL;
		goto error_ret;
	}
	len = iio_scan_mask_count_to_right(dev_info, el->number);
	if (len < 0) {
		ret = len;
		goto error_free_data;
	}
	len = sprintf(buf, "ring %d\n", data[len]);
error_free_data:
	kfree(data);
error_ret:
	return ret ? ret : len;

}

static const u8 read_all_tx_array[] =
{
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
 * @rx_array:	(dma capable) recieve array, must be at least
 *		4*number of channels
 **/
int lis3l02dq_read_all(struct lis3l02dq_state *st, u8 *rx_array)
{
	struct spi_transfer *xfers;
	struct spi_message msg;
	int ret, i, j = 0;

	xfers = kzalloc((st->indio_dev->scan_count) * 2
			* sizeof(*xfers), GFP_KERNEL);
	if (!xfers)
		return -ENOMEM;

	mutex_lock(&st->buf_lock);

	for (i = 0; i < ARRAY_SIZE(read_all_tx_array)/4; i++) {
		if (st->indio_dev->scan_mask & (1 << i)) {
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
	}
	/* After these are transmitted, the rx_buff should have
	 * values in alternate bytes
	 */
	spi_message_init(&msg);
	for (j = 0; j < st->indio_dev->scan_count * 2; j++)
		spi_message_add_tail(&xfers[j], &msg);

	ret = spi_sync(st->us, &msg);
	mutex_unlock(&st->buf_lock);
	kfree(xfers);

	return ret;
}


/* Whilst this makes a lot of calls to iio_sw_ring functions - it is to device
 * specific to be rolled into the core.
 */
static void lis3l02dq_trigger_bh_to_ring(struct work_struct *work_s)
{
	struct lis3l02dq_state *st
		= container_of(work_s, struct lis3l02dq_state,
			       work_trigger_to_ring);

	u8 *rx_array;
	int i = 0;
	u16 *data;
	size_t datasize = st->indio_dev
		->ring->access.get_bpd(st->indio_dev->ring);

	data = kmalloc(datasize , GFP_KERNEL);
	if (data == NULL) {
		dev_err(&st->us->dev, "memory alloc failed in ring bh");
		return;
	}
	/* Due to interleaved nature of transmission this buffer must be
	 * twice the number of bytes, or 4 times the number of channels
	 */
	rx_array = kmalloc(4 * (st->indio_dev->scan_count), GFP_KERNEL);
	if (rx_array == NULL) {
		dev_err(&st->us->dev, "memory alloc failed in ring bh");
		kfree(data);
		return;
	}

	/* whilst trigger specific, if this read does nto occur the data
	   ready interrupt will not be cleared.  Need to add a mechanism
	   to provide a dummy read function if this is not triggering on
	   the data ready function but something else is.
	*/
	st->inter = 0;

	if (st->indio_dev->scan_count)
		if (lis3l02dq_read_all(st, rx_array) >= 0)
			for (; i < st->indio_dev->scan_count; i++)
				data[i] = combine_8_to_16(rx_array[i*4+1],
							  rx_array[i*4+3]);
	/* Guaranteed to be aligned with 8 byte boundary */
	if (st->indio_dev->scan_timestamp)
		*((s64 *)(data + ((i + 3)/4)*4)) = st->last_timestamp;

	st->indio_dev->ring->access.store_to(st->indio_dev->ring,
					    (u8 *)data,
					    st->last_timestamp);

	iio_trigger_notify_done(st->indio_dev->trig);
	kfree(rx_array);
	kfree(data);

	return;
}
/* in these circumstances is it better to go with unaligned packing and
 * deal with the cost?*/
static int lis3l02dq_data_rdy_ring_preenable(struct iio_dev *indio_dev)
{
	size_t size;
	/* Check if there are any scan elements enabled, if not fail*/
	if (!(indio_dev->scan_count || indio_dev->scan_timestamp))
		return -EINVAL;

	if (indio_dev->ring->access.set_bpd) {
		if (indio_dev->scan_timestamp)
			if (indio_dev->scan_count) /* Timestamp and data */
				size = 2*sizeof(s64);
			else /* Timestamp only  */
				size = sizeof(s64);
		else /* Data only */
			size = indio_dev->scan_count*sizeof(s16);
		indio_dev->ring->access.set_bpd(indio_dev->ring, size);
	}

	return 0;
}

static int lis3l02dq_data_rdy_ring_postenable(struct iio_dev *indio_dev)
{
	return indio_dev->trig
		? iio_trigger_attach_poll_func(indio_dev->trig,
					       indio_dev->pollfunc)
		: 0;
}

static int lis3l02dq_data_rdy_ring_predisable(struct iio_dev *indio_dev)
{
	return indio_dev->trig
		? iio_trigger_dettach_poll_func(indio_dev->trig,
						indio_dev->pollfunc)
		: 0;
}


/* Caller responsible for locking as necessary. */
static int __lis3l02dq_write_data_ready_config(struct device *dev,
					       struct
					       iio_event_handler_list *list,
					       bool state)
{
	int ret;
	u8 valold;
	bool currentlyset;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);

/* Get the current event mask register */
	ret = lis3l02dq_spi_read_reg_8(dev,
				       LIS3L02DQ_REG_CTRL_2_ADDR,
				       &valold);
	if (ret)
		goto error_ret;
/* Find out if data ready is already on */
	currentlyset
		= valold & LIS3L02DQ_REG_CTRL_2_ENABLE_DATA_READY_GENERATION;

/* Disable requested */
	if (!state && currentlyset) {

		valold &= ~LIS3L02DQ_REG_CTRL_2_ENABLE_DATA_READY_GENERATION;
		/* The double write is to overcome a hardware bug?*/
		ret = lis3l02dq_spi_write_reg_8(dev,
						LIS3L02DQ_REG_CTRL_2_ADDR,
						&valold);
		if (ret)
			goto error_ret;
		ret = lis3l02dq_spi_write_reg_8(dev,
						LIS3L02DQ_REG_CTRL_2_ADDR,
						&valold);
		if (ret)
			goto error_ret;

		iio_remove_event_from_list(list,
					   &indio_dev->interrupts[0]
					   ->ev_list);

/* Enable requested */
	} else if (state && !currentlyset) {
		/* if not set, enable requested */
		valold |= LIS3L02DQ_REG_CTRL_2_ENABLE_DATA_READY_GENERATION;
		iio_add_event_to_list(list, &indio_dev->interrupts[0]->ev_list);
		ret = lis3l02dq_spi_write_reg_8(dev,
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
	__lis3l02dq_write_data_ready_config(&st->indio_dev->dev,
					    &iio_event_data_rdy_trig,
					    state);
	if (state == false) {
		/* possible quirk with handler currently worked around
		   by ensuring the work queue is empty */
		flush_scheduled_work();
		/* Clear any outstanding ready events */
		ret = lis3l02dq_read_all(st, NULL);
	}
	lis3l02dq_spi_read_reg_8(&st->indio_dev->dev,
				 LIS3L02DQ_REG_WAKE_UP_SRC_ADDR,
				 &t);
	return ret;
}
static DEVICE_ATTR(name, S_IRUGO, iio_trigger_read_name, NULL);

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
 *
 * As the trigger may occur on any data element being updated it is
 * really rather likely to occur during the read from the previous
 * trigger event.  The only way to discover if this has occured on
 * boards not supporting level interrupts is to take a look at the line.
 * If it is indicating another interrupt and we don't seem to have a
 * handler looking at it, then we need to notify the core that we need
 * to tell the triggering core to try reading all these again.
 **/
static int lis3l02dq_trig_try_reen(struct iio_trigger *trig)
{
	struct lis3l02dq_state *st = trig->private_data;
	enable_irq(st->us->irq);
	/* If gpio still high (or high again) */
	if (gpio_get_value(irq_to_gpio(st->us->irq)))
		if (st->inter == 0) {
			/* already interrupt handler dealing with it */
			disable_irq_nosync(st->us->irq);
			if (st->inter == 1) {
				/* interrupt handler snuck in between test
				 * and disable */
				enable_irq(st->us->irq);
				return 0;
			}
			return -EAGAIN;
		}
	/* irq reenabled so success! */
	return 0;
}

int lis3l02dq_probe_trigger(struct iio_dev *indio_dev)
{
	int ret;
	struct lis3l02dq_state *state = indio_dev->dev_data;

	state->trig = iio_allocate_trigger();
	if (!state->trig)
		return -ENOMEM;

	state->trig->name = kmalloc(IIO_TRIGGER_NAME_LENGTH, GFP_KERNEL);
	if (!state->trig->name) {
		ret = -ENOMEM;
		goto error_free_trig;
	}
	snprintf((char *)state->trig->name,
		 IIO_TRIGGER_NAME_LENGTH,
		 "lis3l02dq-dev%d", indio_dev->id);
	state->trig->dev.parent = &state->us->dev;
	state->trig->owner = THIS_MODULE;
	state->trig->private_data = state;
	state->trig->set_trigger_state = &lis3l02dq_data_rdy_trigger_set_state;
	state->trig->try_reenable = &lis3l02dq_trig_try_reen;
	state->trig->control_attrs = &lis3l02dq_trigger_attr_group;
	ret = iio_trigger_register(state->trig);
	if (ret)
		goto error_free_trig_name;

	return 0;

error_free_trig_name:
	kfree(state->trig->name);
error_free_trig:
	iio_free_trigger(state->trig);

	return ret;
}

void lis3l02dq_remove_trigger(struct iio_dev *indio_dev)
{
	struct lis3l02dq_state *state = indio_dev->dev_data;

	iio_trigger_unregister(state->trig);
	kfree(state->trig->name);
	iio_free_trigger(state->trig);
}

void lis3l02dq_unconfigure_ring(struct iio_dev *indio_dev)
{
	kfree(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->ring);
}

int lis3l02dq_configure_ring(struct iio_dev *indio_dev)
{
	int ret = 0;
	struct lis3l02dq_state *st = indio_dev->dev_data;
	struct iio_ring_buffer *ring;
	INIT_WORK(&st->work_trigger_to_ring, lis3l02dq_trigger_bh_to_ring);
	/* Set default scan mode */

	iio_scan_mask_set(indio_dev, iio_scan_el_accel_x.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_accel_y.number);
	iio_scan_mask_set(indio_dev, iio_scan_el_accel_z.number);
	indio_dev->scan_timestamp = true;

	indio_dev->scan_el_attrs = &lis3l02dq_scan_el_group;

	ring = iio_sw_rb_allocate(indio_dev);
	if (!ring) {
		ret = -ENOMEM;
		return ret;
	}
	indio_dev->ring = ring;
	/* Effectively select the ring buffer implementation */
	iio_ring_sw_register_funcs(&ring->access);
	ring->preenable = &lis3l02dq_data_rdy_ring_preenable;
	ring->postenable = &lis3l02dq_data_rdy_ring_postenable;
	ring->predisable = &lis3l02dq_data_rdy_ring_predisable;
	ring->owner = THIS_MODULE;

	indio_dev->pollfunc = kzalloc(sizeof(*indio_dev->pollfunc), GFP_KERNEL);
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_iio_sw_rb_free;;
	}
	indio_dev->pollfunc->poll_func_main = &lis3l02dq_poll_func_th;
	indio_dev->pollfunc->private_data = indio_dev;
	indio_dev->modes |= INDIO_RING_TRIGGERED;
	return 0;

error_iio_sw_rb_free:
	iio_sw_rb_free(indio_dev->ring);
	return ret;
}

int lis3l02dq_initialize_ring(struct iio_ring_buffer *ring)
{
	return iio_ring_buffer_register(ring);
}

void lis3l02dq_uninitialize_ring(struct iio_ring_buffer *ring)
{
	iio_ring_buffer_unregister(ring);
}


int lis3l02dq_set_ring_length(struct iio_dev *indio_dev, int length)
{
	/* Set sensible defaults for the ring buffer */
	if (indio_dev->ring->access.set_length)
		return indio_dev->ring->access.set_length(indio_dev->ring, 500);
	return 0;
}


