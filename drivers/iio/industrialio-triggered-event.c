/*
 * Copyright (C) 2015 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/iio/triggered_event.h>
#include <linux/iio/trigger_consumer.h>

/**
 * iio_triggered_event_setup() - Setup pollfunc_event for triggered event
 * @indio_dev:	IIO device structure
 * @h:		Function which will be used as pollfunc_event top half
 * @thread:	Function which will be used as pollfunc_event bottom half
 *
 * This function combines some common tasks which will normally be performed
 * when setting up a triggered event. It will allocate the pollfunc_event and
 * set mode to use it for triggered event.
 *
 * Before calling this function the indio_dev structure should already be
 * completely initialized, but not yet registered. In practice this means that
 * this function should be called right before iio_device_register().
 *
 * To free the resources allocated by this function call
 * iio_triggered_event_cleanup().
 */
int iio_triggered_event_setup(struct iio_dev *indio_dev,
			      irqreturn_t (*h)(int irq, void *p),
			      irqreturn_t (*thread)(int irq, void *p))
{
	indio_dev->pollfunc_event = iio_alloc_pollfunc(h,
						       thread,
						       IRQF_ONESHOT,
						       indio_dev,
						       "%s_consumer%d",
						       indio_dev->name,
						       indio_dev->id);
	if (indio_dev->pollfunc_event == NULL)
		return -ENOMEM;

	/* Flag that events polling is possible */
	indio_dev->modes |= INDIO_EVENT_TRIGGERED;

	return 0;
}
EXPORT_SYMBOL(iio_triggered_event_setup);

/**
 * iio_triggered_event_cleanup() - Free resources allocated by iio_triggered_event_setup()
 * @indio_dev: IIO device structure
 */
void iio_triggered_event_cleanup(struct iio_dev *indio_dev)
{
	indio_dev->modes &= ~INDIO_EVENT_TRIGGERED;
	iio_dealloc_pollfunc(indio_dev->pollfunc_event);
}
EXPORT_SYMBOL(iio_triggered_event_cleanup);

MODULE_AUTHOR("Vladimir Barinov");
MODULE_DESCRIPTION("IIO helper functions for setting up triggered events");
MODULE_LICENSE("GPL");
