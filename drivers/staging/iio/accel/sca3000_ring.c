/*
 * sca3000_ring.c -- support VTI sca3000 series accelerometers via SPI
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Copyright (c) 2009 Jonathan Cameron <jic23@cam.ac.uk>
 *
 */

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../ring_generic.h"
#include "../ring_hw.h"
#include "accel.h"
#include "sca3000.h"

/* RFC / future work
 *
 * The internal ring buffer doesn't actually change what it holds depending
 * on which signals are enabled etc, merely whether you can read them.
 * As such the scan mode selection is somewhat different than for a software
 * ring buffer and changing it actually covers any data already in the buffer.
 * Currently scan elements aren't configured so it doesn't matter.
 */

/**
 * sca3000_rip_hw_rb() - main ring access function, pulls data from ring
 * @r:			the ring
 * @count:		number of samples to try and pull
 * @data:		output the actual samples pulled from the hw ring
 * @dead_offset:	cheating a bit here: Set to 1 so as to allow for the
 *			leading byte used in bus comms.
 *
 * Currently does not provide timestamps.  As the hardware doesn't add them they
 * can only be inferred aproximately from ring buffer events such as 50% full
 * and knowledge of when buffer was last emptied.  This is left to userspace.
 **/
static int sca3000_rip_hw_rb(struct iio_ring_buffer *r,
			     size_t count, u8 **data, int *dead_offset)
{
	struct iio_hw_ring_buffer *hw_ring = iio_to_hw_ring_buf(r);
	struct iio_dev *indio_dev = hw_ring->private;
	struct sca3000_state *st = indio_dev->dev_data;
	u8 *rx;
	int ret, num_available, num_read = 0;
	int bytes_per_sample = 1;

	if (st->bpse == 11)
		bytes_per_sample = 2;

	mutex_lock(&st->lock);
	/* Check how much data is available:
	 * RFC: Implement an ioctl to not bother checking whether there
	 * is enough data in the ring?  Afterall, if we are responding
	 * to an interrupt we have a minimum content guaranteed so it
	 * seems slight silly to waste time checking it is there.
	 */
	ret = sca3000_read_data(st,
				SCA3000_REG_ADDR_BUF_COUNT,
				&rx, 1);
	if (ret)
		goto error_ret;
	else
		num_available = rx[1];
	/* num_available is the total number of samples available
	 * i.e. number of time points * number of channels.
	 */
	kfree(rx);
	if (count > num_available * bytes_per_sample)
		num_read = num_available*bytes_per_sample;
	else
		num_read = count - (count % (bytes_per_sample));

	/* Avoid the read request byte */
	*dead_offset = 1;
	ret = sca3000_read_data(st,
				SCA3000_REG_ADDR_RING_OUT,
				data, num_read);
error_ret:
	mutex_unlock(&st->lock);

	return ret ? ret : num_read;
}

/* This is only valid with all 3 elements enabled */
static int sca3000_ring_get_length(struct iio_ring_buffer *r)
{
	return 64;
}

/* only valid if resolution is kept at 11bits */
static int sca3000_ring_get_bpd(struct iio_ring_buffer *r)
{
	return 6;
}
static void sca3000_ring_release(struct device *dev)
{
	struct iio_ring_buffer *r = to_iio_ring_buffer(dev);
	kfree(iio_to_hw_ring_buf(r));
}

static IIO_RING_ENABLE_ATTR;
static IIO_RING_BPS_ATTR;
static IIO_RING_LENGTH_ATTR;

/**
 * sca3000_show_ring_bpse() -sysfs function to query bits per sample from ring
 * @dev: ring buffer device
 * @attr: this device attribute
 * @buf: buffer to write to
 **/
static ssize_t sca3000_show_ring_bpse(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	int len = 0, ret;
	u8 *rx;
	struct iio_ring_buffer *r = dev_get_drvdata(dev);
	struct sca3000_state *st = r->indio_dev->dev_data;

	mutex_lock(&st->lock);
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_MODE, &rx, 1);
	if (ret)
		goto error_ret;
	len = sprintf(buf, "%d\n", (rx[1] & SCA3000_RING_BUF_8BIT) ? 8 : 11);
	kfree(rx);
error_ret:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

/**
 * sca3000_store_ring_bpse() - bits per scan element
 * @dev: ring buffer device
 * @attr: attribute called from
 * @buf: input from userspace
 * @len: length of input
 **/
static ssize_t sca3000_store_ring_bpse(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t len)
{
	struct iio_ring_buffer *r = dev_get_drvdata(dev);
	struct sca3000_state *st = r->indio_dev->dev_data;
	int ret;
	u8 *rx;
	long val;
	ret = strict_strtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&st->lock);

	ret = sca3000_read_data(st, SCA3000_REG_ADDR_MODE, &rx, 1);
	if (!ret)
		switch (val) {
		case 8:
			ret = sca3000_write_reg(st, SCA3000_REG_ADDR_MODE,
						rx[1] | SCA3000_RING_BUF_8BIT);
			st->bpse = 8;
			break;
		case 11:
			ret = sca3000_write_reg(st, SCA3000_REG_ADDR_MODE,
						rx[1] & ~SCA3000_RING_BUF_8BIT);
			st->bpse = 11;
			break;
		default:
			ret = -EINVAL;
			break;
		}
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

static IIO_CONST_ATTR(bpse_available, "8 11");

static IIO_DEV_ATTR_BPSE(S_IRUGO | S_IWUSR,
			      sca3000_show_ring_bpse,
			      sca3000_store_ring_bpse);

/*
 * Ring buffer attributes
 * This device is a bit unusual in that the sampling frequency and bpse
 * only apply to the ring buffer.  At all times full rate and accuracy
 * is available via direct reading from registers.
 */
static struct attribute *iio_ring_attributes[] = {
	&dev_attr_length.attr,
	&dev_attr_bps.attr,
	&dev_attr_ring_enable.attr,
	&iio_dev_attr_bpse.dev_attr.attr,
	&iio_const_attr_bpse_available.dev_attr.attr,
	NULL,
};

static struct attribute_group sca3000_ring_attr = {
	.attrs = iio_ring_attributes,
};

static const struct attribute_group *sca3000_ring_attr_groups[] = {
	&sca3000_ring_attr,
	NULL
};

static struct device_type sca3000_ring_type = {
	.release = sca3000_ring_release,
	.groups = sca3000_ring_attr_groups,
};

static struct iio_ring_buffer *sca3000_rb_allocate(struct iio_dev *indio_dev)
{
	struct iio_ring_buffer *buf;
	struct iio_hw_ring_buffer *ring;

	ring = kzalloc(sizeof *ring, GFP_KERNEL);
	if (!ring)
		return 0;
	ring->private = indio_dev;
	buf = &ring->buf;
	iio_ring_buffer_init(buf, indio_dev);
	buf->dev.type = &sca3000_ring_type;
	device_initialize(&buf->dev);
	buf->dev.parent = &indio_dev->dev;
	dev_set_drvdata(&buf->dev, (void *)buf);

	return buf;
}

static inline void sca3000_rb_free(struct iio_ring_buffer *r)
{
	if (r)
		iio_put_ring_buffer(r);
}

int sca3000_configure_ring(struct iio_dev *indio_dev)
{
	indio_dev->ring = sca3000_rb_allocate(indio_dev);
	if (indio_dev->ring == NULL)
		return -ENOMEM;
	indio_dev->modes |= INDIO_RING_HARDWARE_BUFFER;

	indio_dev->ring->access.rip_lots = &sca3000_rip_hw_rb;
	indio_dev->ring->access.get_length = &sca3000_ring_get_length;
	indio_dev->ring->access.get_bpd = &sca3000_ring_get_bpd;

	return 0;
}

void sca3000_unconfigure_ring(struct iio_dev *indio_dev)
{
	sca3000_rb_free(indio_dev->ring);
}

static inline
int __sca3000_hw_ring_state_set(struct iio_dev *indio_dev, bool state)
{
	struct sca3000_state *st = indio_dev->dev_data;
	int ret;
	u8 *rx;

	mutex_lock(&st->lock);
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_MODE, &rx, 1);
	if (ret)
		goto error_ret;
	if (state) {
		printk(KERN_INFO "supposedly enabling ring buffer\n");
		ret = sca3000_write_reg(st,
					SCA3000_REG_ADDR_MODE,
					(rx[1] | SCA3000_RING_BUF_ENABLE));
	} else
		ret = sca3000_write_reg(st,
					SCA3000_REG_ADDR_MODE,
					(rx[1] & ~SCA3000_RING_BUF_ENABLE));
	kfree(rx);
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

void sca3000_register_ring_funcs(struct iio_dev *indio_dev)
{
	indio_dev->ring->preenable = &sca3000_hw_ring_preenable;
	indio_dev->ring->postdisable = &sca3000_hw_ring_postdisable;
}

/**
 * sca3000_ring_int_process() ring specific interrupt handling.
 *
 * This is only split from the main interrupt handler so as to
 * reduce the amount of code if the ring buffer is not enabled.
 **/
void sca3000_ring_int_process(u8 val, struct iio_ring_buffer *ring)
{
	if (val & SCA3000_INT_STATUS_THREE_QUARTERS)
		iio_push_or_escallate_ring_event(ring,
						 IIO_EVENT_CODE_RING_75_FULL,
						 0);
	else if (val & SCA3000_INT_STATUS_HALF)
		iio_push_ring_event(ring,
				    IIO_EVENT_CODE_RING_50_FULL, 0);
}
