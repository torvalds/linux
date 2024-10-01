/* SPDX-License-Identifier: GPL-2.0-only */
/* The industrial I/O core function defs.
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * These definitions are meant for use only within the IIO core, not individual
 * drivers.
 */

#ifndef _IIO_CORE_H_
#define _IIO_CORE_H_
#include <linux/kernel.h>
#include <linux/device.h>

struct iio_buffer;
struct iio_chan_spec;
struct iio_dev;

extern const struct device_type iio_device_type;

struct iio_dev_buffer_pair {
	struct iio_dev		*indio_dev;
	struct iio_buffer	*buffer;
};

#define IIO_IOCTL_UNHANDLED	1
struct iio_ioctl_handler {
	struct list_head entry;
	long (*ioctl)(struct iio_dev *indio_dev, struct file *filp,
		      unsigned int cmd, unsigned long arg);
};

void iio_device_ioctl_handler_register(struct iio_dev *indio_dev,
				       struct iio_ioctl_handler *h);
void iio_device_ioctl_handler_unregister(struct iio_ioctl_handler *h);

ssize_t do_iio_read_channel_label(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *c,
				  char *buf);

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
			   enum iio_shared_by shared_by,
			   struct device *dev,
			   struct iio_buffer *buffer,
			   struct list_head *attr_list);
void iio_free_chan_devattr_list(struct list_head *attr_list);

int iio_device_register_sysfs_group(struct iio_dev *indio_dev,
				    const struct attribute_group *group);

ssize_t iio_format_value(char *buf, unsigned int type, int size, int *vals);

/* Event interface flags */
#define IIO_BUSY_BIT_POS 1

#ifdef CONFIG_IIO_BUFFER
struct poll_table_struct;

__poll_t iio_buffer_poll_wrapper(struct file *filp,
				 struct poll_table_struct *wait);
ssize_t iio_buffer_read_wrapper(struct file *filp, char __user *buf,
				size_t n, loff_t *f_ps);
ssize_t iio_buffer_write_wrapper(struct file *filp, const char __user *buf,
				 size_t n, loff_t *f_ps);

int iio_buffers_alloc_sysfs_and_mask(struct iio_dev *indio_dev);
void iio_buffers_free_sysfs_and_mask(struct iio_dev *indio_dev);

#define iio_buffer_poll_addr (&iio_buffer_poll_wrapper)
#define iio_buffer_read_outer_addr (&iio_buffer_read_wrapper)
#define iio_buffer_write_outer_addr (&iio_buffer_write_wrapper)

void iio_disable_all_buffers(struct iio_dev *indio_dev);
void iio_buffer_wakeup_poll(struct iio_dev *indio_dev);
void iio_device_detach_buffers(struct iio_dev *indio_dev);

#else

#define iio_buffer_poll_addr NULL
#define iio_buffer_read_outer_addr NULL
#define iio_buffer_write_outer_addr NULL

static inline int iio_buffers_alloc_sysfs_and_mask(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void iio_buffers_free_sysfs_and_mask(struct iio_dev *indio_dev) {}

static inline void iio_disable_all_buffers(struct iio_dev *indio_dev) {}
static inline void iio_buffer_wakeup_poll(struct iio_dev *indio_dev) {}
static inline void iio_device_detach_buffers(struct iio_dev *indio_dev) {}

#endif

int iio_device_register_eventset(struct iio_dev *indio_dev);
void iio_device_unregister_eventset(struct iio_dev *indio_dev);
void iio_device_wakeup_eventset(struct iio_dev *indio_dev);

struct iio_event_interface;
bool iio_event_enabled(const struct iio_event_interface *ev_int);

#endif
