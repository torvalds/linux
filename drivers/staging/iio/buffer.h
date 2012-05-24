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

#ifdef CONFIG_IIO_BUFFER

struct iio_buffer;

/**
 * struct iio_buffer_access_funcs - access functions for buffers.
 * @store_to:		actually store stuff to the buffer
 * @read_first_n:	try to get a specified number of bytes (must exist)
 * @request_update:	if a parameter change has been marked, update underlying
 *			storage.
 * @get_bytes_per_datum:get current bytes per datum
 * @set_bytes_per_datum:set number of bytes per datum
 * @get_length:		get number of datums in buffer
 * @set_length:		set number of datums in buffer
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
	int (*store_to)(struct iio_buffer *buffer, u8 *data, s64 timestamp);
	int (*read_first_n)(struct iio_buffer *buffer,
			    size_t n,
			    char __user *buf);

	int (*request_update)(struct iio_buffer *buffer);

	int (*get_bytes_per_datum)(struct iio_buffer *buffer);
	int (*set_bytes_per_datum)(struct iio_buffer *buffer, size_t bpd);
	int (*get_length)(struct iio_buffer *buffer);
	int (*set_length)(struct iio_buffer *buffer, int length);
};

/**
 * struct iio_buffer - general buffer structure
 * @length:		[DEVICE] number of datums in buffer
 * @bytes_per_datum:	[DEVICE] size of individual datum including timestamp
 * @scan_el_attrs:	[DRIVER] control of scan elements if that scan mode
 *			control method is used
 * @scan_mask:		[INTERN] bitmask used in masking scan mode elements
 * @scan_index_timestamp:[INTERN] cache of the index to the timestamp
 * @scan_timestamp:	[INTERN] does the scan mode include a timestamp
 * @access:		[DRIVER] buffer access functions associated with the
 *			implementation.
 * @scan_el_dev_attr_list:[INTERN] list of scan element related attributes.
 * @scan_el_group:	[DRIVER] attribute group for those attributes not
 *			created from the iio_chan_info array.
 * @pollq:		[INTERN] wait queue to allow for polling on the buffer.
 * @stufftoread:	[INTERN] flag to indicate new data.
 * @demux_list:		[INTERN] list of operations required to demux the scan.
 * @demux_bounce:	[INTERN] buffer for doing gather from incoming scan.
 **/
struct iio_buffer {
	int					length;
	int					bytes_per_datum;
	struct attribute_group			*scan_el_attrs;
	long					*scan_mask;
	bool					scan_timestamp;
	unsigned				scan_index_timestamp;
	const struct iio_buffer_access_funcs	*access;
	struct list_head			scan_el_dev_attr_list;
	struct attribute_group			scan_el_group;
	wait_queue_head_t			pollq;
	bool					stufftoread;
	const struct attribute_group *attrs;
	struct list_head			demux_list;
	unsigned char				*demux_bounce;
};

/**
 * iio_buffer_init() - Initialize the buffer structure
 * @buffer: buffer to be initialized
 **/
void iio_buffer_init(struct iio_buffer *buffer);

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

int iio_scan_mask_query(struct iio_dev *indio_dev,
			struct iio_buffer *buffer, int bit);

/**
 * iio_scan_mask_set() - set particular bit in the scan mask
 * @buffer: the buffer whose scan mask we are interested in
 * @bit: the bit to be set.
 **/
int iio_scan_mask_set(struct iio_dev *indio_dev,
		      struct iio_buffer *buffer, int bit);

/**
 * iio_push_to_buffer() - push to a registered buffer.
 * @buffer:		IIO buffer structure for device
 * @scan:		Full scan.
 * @timestamp:
 */
int iio_push_to_buffer(struct iio_buffer *buffer, unsigned char *data,
		       s64 timestamp);

int iio_update_demux(struct iio_dev *indio_dev);

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
