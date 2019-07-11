/* SPDX-License-Identifier: GPL-2.0-only */

/* The industrial I/O core, trigger consumer handling functions
 *
 * Copyright (c) 2008 Jonathan Cameron
 */

#ifdef CONFIG_IIO_TRIGGER
/**
 * iio_device_register_trigger_consumer() - set up an iio_dev to use triggers
 * @indio_dev: iio_dev associated with the device that will consume the trigger
 **/
void iio_device_register_trigger_consumer(struct iio_dev *indio_dev);

/**
 * iio_device_unregister_trigger_consumer() - reverse the registration process
 * @indio_dev: iio_dev associated with the device that consumed the trigger
 **/
void iio_device_unregister_trigger_consumer(struct iio_dev *indio_dev);

#else

/**
 * iio_device_register_trigger_consumer() - set up an iio_dev to use triggers
 * @indio_dev: iio_dev associated with the device that will consume the trigger
 **/
static int iio_device_register_trigger_consumer(struct iio_dev *indio_dev)
{
	return 0;
}

/**
 * iio_device_unregister_trigger_consumer() - reverse the registration process
 * @indio_dev: iio_dev associated with the device that consumed the trigger
 **/
static void iio_device_unregister_trigger_consumer(struct iio_dev *indio_dev)
{
}

#endif /* CONFIG_TRIGGER_CONSUMER */
