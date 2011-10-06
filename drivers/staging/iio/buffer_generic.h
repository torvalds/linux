/* The industrial I/O core - generic buffer interfaces.
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _IIO_BUFFER_GENERIC_H_
#define _IIO_BUFFER_GENERIC_H_
#include <linux/sysfs.h>
#include "iio.h"
#include "chrdev.h"

#ifdef CONFIG_IIO_BUFFER

struct iio_buffer;

/**
 * struct iio_buffer_access_funcs - access functions for buffers.
 * @mark_in_use:	reference counting, typically to prevent module removal
 * @unmark_in_use:	reduce reference count when no longer using buffer
 * @store_to:		actually store stuff to the buffer
 * @read_last:		get the last element stored
 * @read_first_n:	try to get a specified number of elements (must exist)
 * @mark_param_change:	notify buffer that some relevant parameter has changed
 *			Often this means the underlying storage may need to
 *			change.
 * @request_update:	if a parameter change has been marked, update underlying
 *			storage.
 * @get_bytes_per_datum:get current bytes per datum
 * @set_bytes_per_datum:set number of bytes per datum
 * @get_length:		get number of datums in buffer
 * @set_length:		set number of datums in buffer
 * @is_enabled:		query if buffer is currently being used
 * @enable:		enable the buffer
 *
 * The purpose of this structure is to make the buffer element
 * modular as event for a given driver, different usecases may require
 * different buffer designs (space efficiency vs speed for example).
 *
 * It is worth noting that a given buffer implementation may only support a
 * small proportion of these functions.  The core code 'should' cope fine with
 * any of them not existing.
 **/
struct iio_buffer_access_funcs {
	void (*mark_in_use)(struct iio_buffer *buffer);
	void (*unmark_in_use)(struct iio_buffer *buffer);

	int (*store_to)(struct iio_buffer *buffer, u8 *data, s64 timestamp);
	int (*read_last)(struct iio_buffer *buffer, u8 *data);
	int (*read_first_n)(struct iio_buffer *buffer,
			    size_t n,
			    char __user *buf);

	int (*mark_param_change)(struct iio_buffer *buffer);
	int (*request_update)(struct iio_buffer *buffer);

	int (*get_bytes_per_datum)(struct iio_buffer *buffer);
	int (*set_bytes_per_datum)(struct iio_buffer *buffer, size_t bpd);
	int (*get_length)(struct iio_buffer *buffer);
	int (*set_length)(struct iio_buffer *buffer, int length);

	int (*is_enabled)(struct iio_buffer *buffer);
	int (*enable)(struct iio_buffer *buffer);
};

/**
 * struct iio_buffer_setup_ops - buffer setup related callbacks
 * @preenable:		[DRIVER] function to run prior to marking buffer enabled
 * @postenable:		[DRIVER] function to run after marking buffer enabled
 * @predisable:		[DRIVER] function to run prior to marking buffer
 *			disabled
 * @postdisable:	[DRIVER] function to run after marking buffer disabled
 */
struct iio_buffer_setup_ops {
	int				(*preenable)(struct iio_dev *);
	int				(*postenable)(struct iio_dev *);
	int				(*predisable)(struct iio_dev *);
	int				(*postdisable)(struct iio_dev *);
};

/**
 * struct iio_buffer - general buffer structure
 * @indio_dev:		industrial I/O device structure
 * @owner:		module that owns the buffer (for ref counting)
 * @length:		[DEVICE] number of datums in buffer
 * @bytes_per_datum:	[DEVICE] size of individual datum including timestamp
 * @bpe:		[DEVICE] size of individual channel value
 * @scan_el_attrs:	[DRIVER] control of scan elements if that scan mode
 *			control method is used
 * @scan_count:	[INTERN] the number of elements in the current scan mode
 * @scan_mask:		[INTERN] bitmask used in masking scan mode elements
 * @scan_timestamp:	[INTERN] does the scan mode include a timestamp
 * @access:		[DRIVER] buffer access functions associated with the
 *			implementation.
 * @flags:		[INTERN] file ops related flags including busy flag.
 **/
struct iio_buffer {
	struct iio_dev				*indio_dev;
	struct module				*owner;
	int					length;
	int					bytes_per_datum;
	int					bpe;
	struct attribute_group			*scan_el_attrs;
	int					scan_count;
	long					*scan_mask;
	bool					scan_timestamp;
	const struct iio_buffer_access_funcs	*access;
	const struct iio_buffer_setup_ops		*setup_ops;
	struct list_head			scan_el_dev_attr_list;
	struct attribute_group			scan_el_group;
	wait_queue_head_t			pollq;
	bool					stufftoread;
	unsigned long				flags;
	const struct attribute_group *attrs;
};

/**
 * iio_buffer_init() - Initialize the buffer structure
 * @buffer: buffer to be initialized
 * @indio_dev: the iio device the buffer is assocated with
 **/
void iio_buffer_init(struct iio_buffer *buffer,
			  struct iio_dev *indio_dev);

void iio_buffer_deinit(struct iio_buffer *buffer);

/**
 * __iio_update_buffer() - update common elements of buffers
 * @buffer:		buffer that is the event source
 * @bytes_per_datum:	size of individual datum including timestamp
 * @length:		number of datums in buffer
 **/
static inline void __iio_update_buffer(struct iio_buffer *buffer,
				       int bytes_per_datum, int length)
{
	buffer->bytes_per_datum = bytes_per_datum;
	buffer->length = length;
}

int iio_scan_mask_query(struct iio_buffer *buffer, int bit);

/**
 * iio_scan_mask_set() - set particular bit in the scan mask
 * @buffer: the buffer whose scan mask we are interested in
 * @bit: the bit to be set.
 **/
int iio_scan_mask_set(struct iio_buffer *buffer, int bit);

#define to_iio_buffer(d)				\
	container_of(d, struct iio_buffer, dev)

/**
 * iio_buffer_register() - register the buffer with IIO core
 * @indio_dev: device with the buffer to be registered
 **/
int iio_buffer_register(struct iio_dev *indio_dev,
			const struct iio_chan_spec *channels,
			int num_channels);

/**
 * iio_buffer_unregister() - unregister the buffer from IIO core
 * @indio_dev: the device with the buffer to be unregistered
 **/
void iio_buffer_unregister(struct iio_dev *indio_dev);

/**
 * iio_buffer_read_length() - attr func to get number of datums in the buffer
 **/
ssize_t iio_buffer_read_length(struct device *dev,
			       struct device_attribute *attr,
			       char *buf);
/**
 * iio_buffer_write_length() - attr func to set number of datums in the buffer
 **/
ssize_t iio_buffer_write_length(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t len);
/**
 * iio_buffer_read_bytes_per_datum() - attr for number of bytes in whole datum
 **/
ssize_t iio_buffer_read_bytes_per_datum(struct device *dev,
					struct device_attribute *attr,
					char *buf);
/**
 * iio_buffer_store_enable() - attr to turn the buffer on
 **/
ssize_t iio_buffer_store_enable(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t len);
/**
 * iio_buffer_show_enable() - attr to see if the buffer is on
 **/
ssize_t iio_buffer_show_enable(struct device *dev,
			       struct device_attribute *attr,
			       char *buf);
#define IIO_BUFFER_LENGTH_ATTR DEVICE_ATTR(length, S_IRUGO | S_IWUSR,	\
					   iio_buffer_read_length,	\
					   iio_buffer_write_length)
#define IIO_BUFFER_BYTES_PER_DATUM_ATTR					\
	DEVICE_ATTR(bytes_per_datum, S_IRUGO | S_IWUSR,			\
		    iio_buffer_read_bytes_per_datum, NULL)

#define IIO_BUFFER_ENABLE_ATTR DEVICE_ATTR(enable, S_IRUGO | S_IWUSR,	\
					   iio_buffer_show_enable,	\
					   iio_buffer_store_enable)

int iio_sw_buffer_preenable(struct iio_dev *indio_dev);

#else /* CONFIG_IIO_BUFFER */

static inline int iio_buffer_register(struct iio_dev *indio_dev,
					   struct iio_chan_spec *channels,
					   int num_channels)
{
	return 0;
}

static inline void iio_buffer_unregister(struct iio_dev *indio_dev)
{};

#endif /* CONFIG_IIO_BUFFER */

#endif /* _IIO_BUFFER_GENERIC_H_ */
