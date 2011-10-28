/* The industrial I/O core - generic ring buffer interfaces.
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _IIO_RING_GENERIC_H_
#define _IIO_RING_GENERIC_H_
#include "iio.h"

#ifdef CONFIG_IIO_RING_BUFFER

struct iio_ring_buffer;

/**
 * struct iio_ring_access_funcs - access functions for ring buffers.
 * @mark_in_use:	reference counting, typically to prevent module removal
 * @unmark_in_use:	reduce reference count when no longer using ring buffer
 * @store_to:		actually store stuff to the ring buffer
 * @read_last:		get the last element stored
 * @read_first_n:	try to get a specified number of elements (must exist)
 * @mark_param_change:	notify ring that some relevant parameter has changed
 *			Often this means the underlying storage may need to
 *			change.
 * @request_update:	if a parameter change has been marked, update underlying
 *			storage.
 * @get_bytes_per_datum:get current bytes per datum
 * @set_bytes_per_datum:set number of bytes per datum
 * @get_length:		get number of datums in ring
 * @set_length:		set number of datums in ring
 * @is_enabled:		query if ring is currently being used
 * @enable:		enable the ring
 *
 * The purpose of this structure is to make the ring buffer element
 * modular as event for a given driver, different usecases may require
 * different ring designs (space efficiency vs speed for example).
 *
 * It is worth noting that a given ring implementation may only support a small
 * proportion of these functions.  The core code 'should' cope fine with any of
 * them not existing.
 **/
struct iio_ring_access_funcs {
	void (*mark_in_use)(struct iio_ring_buffer *ring);
	void (*unmark_in_use)(struct iio_ring_buffer *ring);

	int (*store_to)(struct iio_ring_buffer *ring, u8 *data, s64 timestamp);
	int (*read_last)(struct iio_ring_buffer *ring, u8 *data);
	int (*read_first_n)(struct iio_ring_buffer *ring,
			    size_t n,
			    char __user *buf);

	int (*mark_param_change)(struct iio_ring_buffer *ring);
	int (*request_update)(struct iio_ring_buffer *ring);

	int (*get_bytes_per_datum)(struct iio_ring_buffer *ring);
	int (*set_bytes_per_datum)(struct iio_ring_buffer *ring, size_t bpd);
	int (*get_length)(struct iio_ring_buffer *ring);
	int (*set_length)(struct iio_ring_buffer *ring, int length);

	int (*is_enabled)(struct iio_ring_buffer *ring);
	int (*enable)(struct iio_ring_buffer *ring);
};

struct iio_ring_setup_ops {
	int				(*preenable)(struct iio_dev *);
	int				(*postenable)(struct iio_dev *);
	int				(*predisable)(struct iio_dev *);
	int				(*postdisable)(struct iio_dev *);
};

/**
 * struct iio_ring_buffer - general ring buffer structure
 * @dev:		ring buffer device struct
 * @indio_dev:		industrial I/O device structure
 * @owner:		module that owns the ring buffer (for ref counting)
 * @length:		[DEVICE] number of datums in ring
 * @bytes_per_datum:	[DEVICE] size of individual datum including timestamp
 * @bpe:		[DEVICE] size of individual channel value
 * @scan_el_attrs:	[DRIVER] control of scan elements if that scan mode
 *			control method is used
 * @scan_count:	[INTERN] the number of elements in the current scan mode
 * @scan_mask:		[INTERN] bitmask used in masking scan mode elements
 * @scan_timestamp:	[INTERN] does the scan mode include a timestamp
 * @access_handler:	[INTERN] chrdev access handling
 * @access:		[DRIVER] ring access functions associated with the
 *			implementation.
 * @preenable:		[DRIVER] function to run prior to marking ring enabled
 * @postenable:		[DRIVER] function to run after marking ring enabled
 * @predisable:		[DRIVER] function to run prior to marking ring disabled
 * @postdisable:	[DRIVER] function to run after marking ring disabled
 **/
struct iio_ring_buffer {
	struct device				dev;
	struct iio_dev				*indio_dev;
	struct module				*owner;
	int					length;
	int					bytes_per_datum;
	int					bpe;
	struct attribute_group			*scan_el_attrs;
	int					scan_count;
	unsigned long				scan_mask;
	bool					scan_timestamp;
	struct iio_handler			access_handler;
	const struct iio_ring_access_funcs	*access;
	const struct iio_ring_setup_ops		*setup_ops;
	struct list_head			scan_el_dev_attr_list;

	wait_queue_head_t			pollq;
	bool					stufftoread;
};

/**
 * iio_ring_buffer_init() - Initialize the buffer structure
 * @ring: buffer to be initialized
 * @dev_info: the iio device the buffer is assocated with
 **/
void iio_ring_buffer_init(struct iio_ring_buffer *ring,
			  struct iio_dev *dev_info);

/**
 * __iio_update_ring_buffer() - update common elements of ring buffers
 * @ring:		ring buffer that is the event source
 * @bytes_per_datum:	size of individual datum including timestamp
 * @length:		number of datums in ring
 **/
static inline void __iio_update_ring_buffer(struct iio_ring_buffer *ring,
					    int bytes_per_datum, int length)
{
	ring->bytes_per_datum = bytes_per_datum;
	ring->length = length;
}

/*
 * These are mainly provided to allow for a change of implementation if a device
 * has a large number of scan elements
 */
#define IIO_MAX_SCAN_LENGTH 31

/* note 0 used as error indicator as it doesn't make sense. */
static inline u32 iio_scan_mask_match(u32 *av_masks, u32 mask)
{
	while (*av_masks) {
		if (!(~*av_masks & mask))
			return *av_masks;
		av_masks++;
	}
	return 0;
}

static inline int iio_scan_mask_query(struct iio_ring_buffer *ring, int bit)
{
	struct iio_dev *dev_info = ring->indio_dev;
	u32 mask;

	if (bit > IIO_MAX_SCAN_LENGTH)
		return -EINVAL;

	if (!ring->scan_mask)
		return 0;

	if (dev_info->available_scan_masks)
		mask = iio_scan_mask_match(dev_info->available_scan_masks,
					ring->scan_mask);
	else
		mask = ring->scan_mask;

	if (!mask)
		return -EINVAL;

	return !!(mask & (1 << bit));
};

/**
 * iio_scan_mask_set() - set particular bit in the scan mask
 * @ring: the ring buffer whose scan mask we are interested in
 * @bit: the bit to be set.
 **/
static inline int iio_scan_mask_set(struct iio_ring_buffer *ring, int bit)
{
	struct iio_dev *dev_info = ring->indio_dev;
	u32 mask;
	u32 trialmask = ring->scan_mask | (1 << bit);

	if (bit > IIO_MAX_SCAN_LENGTH)
		return -EINVAL;
	if (dev_info->available_scan_masks) {
		mask = iio_scan_mask_match(dev_info->available_scan_masks,
					trialmask);
		if (!mask)
			return -EINVAL;
	}
	ring->scan_mask = trialmask;
	ring->scan_count++;

	return 0;
};

/**
 * iio_put_ring_buffer() - notify done with buffer
 * @ring: the buffer we are done with.
 **/
static inline void iio_put_ring_buffer(struct iio_ring_buffer *ring)
{
	put_device(&ring->dev);
};

#define to_iio_ring_buffer(d)				\
	container_of(d, struct iio_ring_buffer, dev)

/**
 * iio_ring_buffer_register_ex() - register the buffer with IIO core
 * @ring: the buffer to be registered
 * @id: the id of the buffer (typically 0)
 **/
int iio_ring_buffer_register_ex(struct iio_ring_buffer *ring, int id,
				const struct iio_chan_spec *channels,
				int num_channels);

void iio_ring_access_release(struct device *dev);

/**
 * iio_ring_buffer_unregister() - unregister the buffer from IIO core
 * @ring: the buffer to be unregistered
 **/
void iio_ring_buffer_unregister(struct iio_ring_buffer *ring);

/**
 * iio_read_ring_length() - attr func to get number of datums in the buffer
 **/
ssize_t iio_read_ring_length(struct device *dev,
			     struct device_attribute *attr,
			     char *buf);
/**
 * iio_write_ring_length() - attr func to set number of datums in the buffer
 **/
ssize_t iio_write_ring_length(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t len);
/**
 * iio_read_ring_bytes_per_datum() - attr for number of bytes in whole datum
 **/
ssize_t iio_read_ring_bytes_per_datum(struct device *dev,
			  struct device_attribute *attr,
			  char *buf);
/**
 * iio_store_ring_enable() - attr to turn the buffer on
 **/
ssize_t iio_store_ring_enable(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t len);
/**
 * iio_show_ring_enable() - attr to see if the buffer is on
 **/
ssize_t iio_show_ring_enable(struct device *dev,
			     struct device_attribute *attr,
			     char *buf);
#define IIO_RING_LENGTH_ATTR DEVICE_ATTR(length, S_IRUGO | S_IWUSR,	\
					 iio_read_ring_length,		\
					 iio_write_ring_length)
#define IIO_RING_BYTES_PER_DATUM_ATTR DEVICE_ATTR(bytes_per_datum, S_IRUGO | S_IWUSR,	\
				      iio_read_ring_bytes_per_datum, NULL)
#define IIO_RING_ENABLE_ATTR DEVICE_ATTR(enable, S_IRUGO | S_IWUSR, \
					 iio_show_ring_enable,		\
					 iio_store_ring_enable)

int iio_sw_ring_preenable(struct iio_dev *indio_dev);

#else /* CONFIG_IIO_RING_BUFFER */

static inline int iio_ring_buffer_register_ex(struct iio_ring_buffer *ring,
					      int id,
					      struct iio_chan_spec *channels,
					      int num_channels)
{
	return 0;
}

static inline void iio_ring_buffer_unregister(struct iio_ring_buffer *ring)
{};

#endif /* CONFIG_IIO_RING_BUFFER */

#endif /* _IIO_RING_GENERIC_H_ */
