// SPDX-License-Identifier: GPL-2.0-only
 /*
 * Copyright (c) 2012 Analog Devices, Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/buffer_impl.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

/**
 * iio_triggered_buffer_setup_ext() - Setup triggered buffer and pollfunc
 * @indio_dev:		IIO device structure
 * @h:			Function which will be used as pollfunc top half
 * @thread:		Function which will be used as pollfunc bottom half
 * @direction:		Direction of the data stream (in/out).
 * @setup_ops:		Buffer setup functions to use for this device.
 *			If NULL the default setup functions for triggered
 *			buffers will be used.
 * @buffer_attrs:	Extra sysfs buffer attributes for this IIO buffer
 *
 * This function combines some common tasks which will normally be performed
 * when setting up a triggered buffer. It will allocate the buffer and the
 * pollfunc.
 *
 * Before calling this function the indio_dev structure should already be
 * completely initialized, but not yet registered. In practice this means that
 * this function should be called right before iio_device_register().
 *
 * To free the resources allocated by this function call
 * iio_triggered_buffer_cleanup().
 */
int iio_triggered_buffer_setup_ext(struct iio_dev *indio_dev,
	irqreturn_t (*h)(int irq, void *p),
	irqreturn_t (*thread)(int irq, void *p),
	enum iio_buffer_direction direction,
	const struct iio_buffer_setup_ops *setup_ops,
	const struct attribute **buffer_attrs)
{
	struct iio_buffer *buffer;
	int ret;

	buffer = iio_kfifo_allocate();
	if (!buffer) {
		ret = -ENOMEM;
		goto error_ret;
	}

	indio_dev->pollfunc = iio_alloc_pollfunc(h,
						 thread,
						 IRQF_ONESHOT,
						 indio_dev,
						 "%s_consumer%d",
						 indio_dev->name,
						 iio_device_id(indio_dev));
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_kfifo_free;
	}

	/* Ring buffer functions - here trigger setup related */
	indio_dev->setup_ops = setup_ops;

	/* Flag that polled ring buffering is possible */
	indio_dev->modes |= INDIO_BUFFER_TRIGGERED;

	buffer->direction = direction;
	buffer->attrs = buffer_attrs;

	ret = iio_device_attach_buffer(indio_dev, buffer);
	if (ret < 0)
		goto error_dealloc_pollfunc;

	return 0;

error_dealloc_pollfunc:
	iio_dealloc_pollfunc(indio_dev->pollfunc);
error_kfifo_free:
	iio_kfifo_free(buffer);
error_ret:
	return ret;
}
EXPORT_SYMBOL(iio_triggered_buffer_setup_ext);

/**
 * iio_triggered_buffer_cleanup() - Free resources allocated by iio_triggered_buffer_setup_ext()
 * @indio_dev: IIO device structure
 */
void iio_triggered_buffer_cleanup(struct iio_dev *indio_dev)
{
	iio_dealloc_pollfunc(indio_dev->pollfunc);
	iio_kfifo_free(indio_dev->buffer);
}
EXPORT_SYMBOL(iio_triggered_buffer_cleanup);

static void devm_iio_triggered_buffer_clean(void *indio_dev)
{
	iio_triggered_buffer_cleanup(indio_dev);
}

int devm_iio_triggered_buffer_setup_ext(struct device *dev,
					struct iio_dev *indio_dev,
					irqreturn_t (*h)(int irq, void *p),
					irqreturn_t (*thread)(int irq, void *p),
					enum iio_buffer_direction direction,
					const struct iio_buffer_setup_ops *ops,
					const struct attribute **buffer_attrs)
{
	int ret;

	ret = iio_triggered_buffer_setup_ext(indio_dev, h, thread, direction,
					     ops, buffer_attrs);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, devm_iio_triggered_buffer_clean,
					indio_dev);
}
EXPORT_SYMBOL_GPL(devm_iio_triggered_buffer_setup_ext);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("IIO helper functions for setting up triggered buffers");
MODULE_LICENSE("GPL");
