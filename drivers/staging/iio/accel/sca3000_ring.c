/*
 * sca3000_ring.c -- support VTI sca3000 series accelerometers via SPI
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Copyright (c) 2009 Jonathan Cameron <jic23@kernel.org>
 *
 */

#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/sched.h>
#include <linux/poll.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include "../ring_hw.h"
#include "sca3000.h"

/* RFC / future work
 *
 * The internal ring buffer doesn't actually change what it holds depending
 * on which signals are enabled etc, merely whether you can read them.
 * As such the scan mode selection is somewhat different than for a software
 * ring buffer and changing it actually covers any data already in the buffer.
 * Currently scan elements aren't configured so it doesn't matter.
 */

static int sca3000_read_data(struct sca3000_state *st,
			    uint8_t reg_address_high,
			    u8 **rx_p,
			    int len)
{
	int ret;
	struct spi_transfer xfer[2] = {
		{
			.len = 1,
			.tx_buf = st->tx,
		}, {
			.len = len,
		}
	};
	*rx_p = kmalloc(len, GFP_KERNEL);
	if (*rx_p == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	xfer[1].rx_buf = *rx_p;
	st->tx[0] = SCA3000_READ_REG(reg_address_high);
	ret = spi_sync_transfer(st->us, xfer, ARRAY_SIZE(xfer));
	if (ret) {
		dev_err(get_device(&st->us->dev), "problem reading register");
		goto error_free_rx;
	}

	return 0;
error_free_rx:
	kfree(*rx_p);
error_ret:
	return ret;
}

/**
 * sca3000_read_first_n_hw_rb() - main ring access, pulls data from ring
 * @r:			the ring
 * @count:		number of samples to try and pull
 * @data:		output the actual samples pulled from the hw ring
 *
 * Currently does not provide timestamps.  As the hardware doesn't add them they
 * can only be inferred approximately from ring buffer events such as 50% full
 * and knowledge of when buffer was last emptied.  This is left to userspace.
 **/
static int sca3000_read_first_n_hw_rb(struct iio_buffer *r,
				      size_t count, char __user *buf)
{
	struct iio_hw_buffer *hw_ring = iio_to_hw_buf(r);
	struct iio_dev *indio_dev = hw_ring->private;
	struct sca3000_state *st = iio_priv(indio_dev);
	u8 *rx;
	int ret, i, num_available, num_read = 0;
	int bytes_per_sample = 1;

	if (st->bpse == 11)
		bytes_per_sample = 2;

	mutex_lock(&st->lock);
	if (count % bytes_per_sample) {
		ret = -EINVAL;
		goto error_ret;
	}

	ret = sca3000_read_data_short(st, SCA3000_REG_ADDR_BUF_COUNT, 1);
	if (ret)
		goto error_ret;
	else
		num_available = st->rx[0];
	/*
	 * num_available is the total number of samples available
	 * i.e. number of time points * number of channels.
	 */
	if (count > num_available * bytes_per_sample)
		num_read = num_available*bytes_per_sample;
	else
		num_read = count;

	ret = sca3000_read_data(st,
				SCA3000_REG_ADDR_RING_OUT,
				&rx, num_read);
	if (ret)
		goto error_ret;

	for (i = 0; i < num_read; i++)
		*(((u16 *)rx) + i) = be16_to_cpup((__be16 *)rx + i);

	if (copy_to_user(buf, rx, num_read))
		ret = -EFAULT;
	kfree(rx);
	r->stufftoread = 0;
error_ret:
	mutex_unlock(&st->lock);

	return ret ? ret : num_read;
}

/* This is only valid with all 3 elements enabled */
static int sca3000_ring_get_length(struct iio_buffer *r)
{
	return 64;
}

static bool sca3000_ring_buf_data_available(struct iio_buffer *r)
{
	return r->stufftoread;
}

static IIO_BUFFER_ENABLE_ATTR;
static IIO_BUFFER_LENGTH_ATTR;

/**
 * sca3000_query_ring_int() is the hardware ring status interrupt enabled
 **/
static ssize_t sca3000_query_ring_int(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret, val;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sca3000_state *st = iio_priv(indio_dev);

	mutex_lock(&st->lock);
	ret = sca3000_read_data_short(st, SCA3000_REG_ADDR_INT_MASK, 1);
	val = st->rx[0];
	mutex_unlock(&st->lock);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", !!(val & this_attr->address));
}

/**
 * sca3000_set_ring_int() set state of ring status interrupt
 **/
static ssize_t sca3000_set_ring_int(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sca3000_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	u8 val;
	int ret;

	mutex_lock(&st->lock);
	ret = kstrtou8(buf, 10, &val);
	if (ret)
		goto error_ret;
	ret = sca3000_read_data_short(st, SCA3000_REG_ADDR_INT_MASK, 1);
	if (ret)
		goto error_ret;
	if (val)
		ret = sca3000_write_reg(st,
					SCA3000_REG_ADDR_INT_MASK,
					st->rx[0] | this_attr->address);
	else
		ret = sca3000_write_reg(st,
					SCA3000_REG_ADDR_INT_MASK,
					st->rx[0] & ~this_attr->address);
error_ret:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

static IIO_DEVICE_ATTR(50_percent, S_IRUGO | S_IWUSR,
		       sca3000_query_ring_int,
		       sca3000_set_ring_int,
		       SCA3000_INT_MASK_RING_HALF);

static IIO_DEVICE_ATTR(75_percent, S_IRUGO | S_IWUSR,
		       sca3000_query_ring_int,
		       sca3000_set_ring_int,
		       SCA3000_INT_MASK_RING_THREE_QUARTER);

static ssize_t sca3000_show_buffer_scale(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sca3000_state *st = iio_priv(indio_dev);

	return sprintf(buf, "0.%06d\n", 4*st->info->scale);
}

static IIO_DEVICE_ATTR(in_accel_scale,
		       S_IRUGO,
		       sca3000_show_buffer_scale,
		       NULL,
		       0);

/*
 * Ring buffer attributes
 * This device is a bit unusual in that the sampling frequency and bpse
 * only apply to the ring buffer.  At all times full rate and accuracy
 * is available via direct reading from registers.
 */
static struct attribute *sca3000_ring_attributes[] = {
	&dev_attr_length.attr,
	&dev_attr_enable.attr,
	&iio_dev_attr_50_percent.dev_attr.attr,
	&iio_dev_attr_75_percent.dev_attr.attr,
	&iio_dev_attr_in_accel_scale.dev_attr.attr,
	NULL,
};

static struct attribute_group sca3000_ring_attr = {
	.attrs = sca3000_ring_attributes,
	.name = "buffer",
};

static struct iio_buffer *sca3000_rb_allocate(struct iio_dev *indio_dev)
{
	struct iio_buffer *buf;
	struct iio_hw_buffer *ring;

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring)
		return NULL;

	ring->private = indio_dev;
	buf = &ring->buf;
	buf->stufftoread = 0;
	buf->attrs = &sca3000_ring_attr;
	iio_buffer_init(buf);

	return buf;
}

static void sca3000_ring_release(struct iio_buffer *r)
{
	kfree(iio_to_hw_buf(r));
}

static const struct iio_buffer_access_funcs sca3000_ring_access_funcs = {
	.read_first_n = &sca3000_read_first_n_hw_rb,
	.get_length = &sca3000_ring_get_length,
	.data_available = sca3000_ring_buf_data_available,
	.release = sca3000_ring_release,
};

int sca3000_configure_ring(struct iio_dev *indio_dev)
{
	struct iio_buffer *buffer;

	buffer = sca3000_rb_allocate(indio_dev);
	if (buffer == NULL)
		return -ENOMEM;
	indio_dev->modes |= INDIO_BUFFER_HARDWARE;

	indio_dev->buffer->access = &sca3000_ring_access_funcs;

	iio_device_attach_buffer(indio_dev, buffer);

	return 0;
}

void sca3000_unconfigure_ring(struct iio_dev *indio_dev)
{
	iio_buffer_put(indio_dev->buffer);
}

static inline
int __sca3000_hw_ring_state_set(struct iio_dev *indio_dev, bool state)
{
	struct sca3000_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);
	ret = sca3000_read_data_short(st, SCA3000_REG_ADDR_MODE, 1);
	if (ret)
		goto error_ret;
	if (state) {
		dev_info(&indio_dev->dev, "supposedly enabling ring buffer\n");
		ret = sca3000_write_reg(st,
					SCA3000_REG_ADDR_MODE,
					(st->rx[0] | SCA3000_RING_BUF_ENABLE));
	} else
		ret = sca3000_write_reg(st,
					SCA3000_REG_ADDR_MODE,
					(st->rx[0] & ~SCA3000_RING_BUF_ENABLE));
error_ret:
	mutex_unlock(&st->lock);

	return ret;
}
/**
 * sca3000_hw_ring_preenable() hw ring buffer preenable function
 *
 * Very simple enable function as the chip will allows normal reads
 * during ring buffer operation so as long as it is indeed running
 * before we notify the core, the precise ordering does not matter.
 **/
static int sca3000_hw_ring_preenable(struct iio_dev *indio_dev)
{
	return __sca3000_hw_ring_state_set(indio_dev, 1);
}

static int sca3000_hw_ring_postdisable(struct iio_dev *indio_dev)
{
	return __sca3000_hw_ring_state_set(indio_dev, 0);
}

static const struct iio_buffer_setup_ops sca3000_ring_setup_ops = {
	.preenable = &sca3000_hw_ring_preenable,
	.postdisable = &sca3000_hw_ring_postdisable,
};

void sca3000_register_ring_funcs(struct iio_dev *indio_dev)
{
	indio_dev->setup_ops = &sca3000_ring_setup_ops;
}

/**
 * sca3000_ring_int_process() ring specific interrupt handling.
 *
 * This is only split from the main interrupt handler so as to
 * reduce the amount of code if the ring buffer is not enabled.
 **/
void sca3000_ring_int_process(u8 val, struct iio_buffer *ring)
{
	if (val & (SCA3000_INT_STATUS_THREE_QUARTERS |
		   SCA3000_INT_STATUS_HALF)) {
		ring->stufftoread = true;
		wake_up_interruptible(&ring->pollq);
	}
}
