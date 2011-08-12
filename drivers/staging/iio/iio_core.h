/* The industrial I/O core function defs.
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * These definitions are meant for use only within the IIO core, not indvidual
 * drivers.
 */

/**
 * iio_device_get_chrdev_minor() - get an unused minor number
 **/
int iio_device_get_chrdev_minor(void);
void iio_device_free_chrdev_minor(int val);


/**
 * iio_put() - internal module reference count reduce
 **/
void iio_put(void);

/**
 * iio_get() - internal module reference count increase
 **/
void iio_get(void);

extern dev_t iio_devt;

int __iio_add_chan_devattr(const char *postfix,
			   const char *group,
			   struct iio_chan_spec const *chan,
			   ssize_t (*func)(struct device *dev,
					   struct device_attribute *attr,
					   char *buf),
			   ssize_t (*writefunc)(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t len),
			   int mask,
			   bool generic,
			   struct device *dev,
			   struct list_head *attr_list);

/* Event interface flags */
#define IIO_BUSY_BIT_POS 1
