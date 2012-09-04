 /*
 * Copyright (c) 2012 Analog Devices, Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

static const struct iio_buffer_setup_ops iio_triggered_buffer_setup_ops = {
	.preenable = &iio_sw_buffer_preenable,
	.postenable = &iio_triggered_buffer_postenable,
	.predisable = &iio_triggered_buffer_predisable,
};

/**
 * iio_triggered_buffer_setup() - Setup triggered buffer and pollfunc
 * @indio_dev:		IIO device structure
 * @pollfunc_bh:	Function which will be used as pollfunc bottom half
 * @pollfunc_th:	Function which will be used as pollfunc top half
 * @setup_ops:		Buffer setup functions to use for this device.
 *			If NULL the default setup functions for triggered
 *			buffers will be used.
 *
 * This function combines some common tasks which will normally be performed
 * when setting up a triggered buffer. It will allocate the buffer and the
 * pollfunc, as well as register the buffer with the IIO core.
 *
 * Before calling this function the indio_dev structure should already be
 * completely initialized, but not yet registered. In practice this means that
 * this function should be called right before iio_device_register().
 *
 * To free the resources allocated by this function call
 * iio_triggered_buffer_cleanup().
 */
int iio_triggered_buffer_setup(struct iio_dev *indio_dev,
	irqreturn_t (*pollfunc_bh)(int irq, void *p),
	irqreturn_t (*pollfunc_th)(int irq, void *p),
	const struct iio_buffer_setup_ops *setup_ops)
{
	int ret;

	indio_dev->buffer = iio_kfifo_allocate(indio_dev);
	if (!indio_dev->buffer) {
		ret = -ENOMEM;
		goto error_ret;
	}

	indio_dev->pollfunc = iio_alloc_pollfunc(pollfunc_bh,
						 pollfunc_th,
						 IRQF_ONESHOT,
						 indio_dev,
						 "%s_consumer%d",
						 indio_dev->name,
						 indio_dev->id);
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_kfifo_free;
	}

	/* Ring buffer functions - here trigger setup related */
	if (setup_ops)
		indio_dev->setup_ops = setup_ops;
	else
		indio_dev->setup_ops = &iio_triggered_buffer_setup_ops;

	/* Flag that polled ring buffering is possible */
	indio_dev->modes |= INDIO_BUFFER_TRIGGERED;

	ret = iio_buffer_register(indio_dev,
				  indio_dev->channels,
				  indio_dev->num_channels);
	if (ret)
		goto error_dealloc_pollfunc;

	return 0;

error_dealloc_pollfunc:
	iio_dealloc_pollfunc(indio_dev->pollfunc);
error_kfifo_free:
	iio_kfifo_free(indio_dev->buffer);
error_ret:
	return ret;
}
EXPORT_SYMBOL(iio_triggered_buffer_setup);

/**
 * iio_triggered_buffer_cleanup() - Free resources allocated by iio_triggered_buffer_setup()
 * @indio_dev: IIO device structure
 */
void iio_triggered_buffer_cleanup(struct iio_dev *indio_dev)
{
	iio_buffer_unregister(indio_dev);
	iio_dealloc_pollfunc(indio_dev->pollfunc);
	iio_kfifo_free(indio_dev->buffer);
}
EXPORT_SYMBOL(iio_triggered_buffer_cleanup);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("IIO helper functions for setting up triggered buffers");
MODULE_LICENSE("GPL");
