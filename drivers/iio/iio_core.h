/* The industrial I/O core function defs.
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * These definitions are meant for use only within the IIO core, not individual
 * drivers.
 */

#ifndef _IIO_CORE_H_
#define _IIO_CORE_H_
#include <linux/kernel.h>
#include <linux/device.h>

struct iio_chan_spec;
struct iio_dev;

extern struct device_type iio_device_type;

int __iio_add_chan_devattr(const char *postfix,
			   struct iio_chan_spec const *chan,
			   ssize_t (*func)(struct device *dev,
					   struct device_attribute *attr,
					   char *buf),
			   ssize_t (*writefunc)(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t len),
			   u64 mask,
			   bool generic,
			   struct device *dev,
			   struct list_head *attr_list);

/* Event interface flags */
#define IIO_BUSY_BIT_POS 1

#ifdef CONFIG_IIO_BUFFER
struct poll_table_struct;

unsigned int iio_buffer_poll(struct file *filp,
			     struct poll_table_struct *wait);
ssize_t iio_buffer_read_first_n_outer(struct file *filp, char __user *buf,
				      size_t n, loff_t *f_ps);


#define iio_buffer_poll_addr (&iio_buffer_poll)
#define iio_buffer_read_first_n_outer_addr (&iio_buffer_read_first_n_outer)

#else

#define iio_buffer_poll_addr NULL
#define iio_buffer_read_first_n_outer_addr NULL

#endif

int iio_device_register_eventset(struct iio_dev *indio_dev);
void iio_device_unregister_eventset(struct iio_dev *indio_dev);
int iio_event_getfd(struct iio_dev *indio_dev);

#endif
