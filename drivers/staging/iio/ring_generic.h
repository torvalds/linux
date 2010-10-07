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
 * iio_push_ring_event() - ring buffer specific push to event chrdev
 * @ring_buf:		ring buffer that is the event source
 * @event_code:		event indentification code
 * @timestamp:		time of event
 **/
int iio_push_ring_event(struct iio_ring_buffer *ring_buf,
			int event_code,
			s64 timestamp);
/**
 * iio_push_or_escallate_ring_event() -	escalate or add as appropriate
 * @ring_buf:		ring buffer that is the event source
 * @event_code:		event indentification code
 * @timestamp:		time of event
 *
 * Typical usecase is to escalate a 50% ring full to 75% full if noone has yet
 * read the first event. Clearly the 50% full is no longer of interest in
 * typical use case.
 **/
int iio_push_or_escallate_ring_event(struct iio_ring_buffer *ring_buf,
				     int event_code,
				     s64 timestamp);

/**
 * struct iio_ring_access_funcs - access functions for ring buffers.
 * @mark_in_use:	reference counting, typically to prevent module removal
 * @unmark_in_use:	reduce reference count when no longer using ring buffer
 * @store_to:		actually store stuff to the ring buffer
 * @read_last:		get the last element stored
 * @rip_lots:		try to get a specified number of elements (must exist)
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
	int (*rip_lots)(struct iio_ring_buffer *ring,
			size_t count,
			u8 **data,
			int *dead_offset);

	int (*mark_param_change)(struct iio_ring_buffer *ring);
	int (*request_update)(struct iio_ring_buffer *ring);

	int (*get_bytes_per_datum)(struct iio_ring_buffer *ring);
	int (*set_bytes_per_datum)(struct iio_ring_buffer *ring, size_t bpd);
	int (*get_length)(struct iio_ring_buffer *ring);
	int (*set_length)(struct iio_ring_buffer *ring, int length);

	int (*is_enabled)(struct iio_ring_buffer *ring);
	int (*enable)(struct iio_ring_buffer *ring);
};

/**
 * struct iio_ring_buffer - general ring buffer structure
 * @dev:		ring buffer device struct
 * @access_dev:		system device struct for the chrdev
 * @indio_dev:		industrial I/O device structure
 * @owner:		module that owns the ring buffer (for ref counting)
 * @id:			unique id number
 * @access_id:		device id number
 * @length:		[DEVICE] number of datums in ring
 * @bytes_per_datum:	[DEVICE] size of individual datum including timestamp
 * @bpe:		[DEVICE] size of individual channel value
 * @loopcount:		[INTERN] number of times the ring has looped
 * @scan_el_attrs:	[DRIVER] control of scan elements if that scan mode
 *			control method is used
 * @scan_count:	[INTERN] the number of elements in the current scan mode
 * @scan_mask:		[INTERN] bitmask used in masking scan mode elements
 * @scan_timestamp:	[INTERN] does the scan mode include a timestamp
 * @access_handler:	[INTERN] chrdev access handling
 * @ev_int:		[INTERN] chrdev interface for the event chrdev
 * @shared_ev_pointer:	[INTERN] the shared event pointer to allow escalation of
 *			events
 * @access:		[DRIVER] ring access functions associated with the
 *			implementation.
 * @preenable:		[DRIVER] function to run prior to marking ring enabled
 * @postenable:		[DRIVER] function to run after marking ring enabled
 * @predisable:		[DRIVER] function to run prior to marking ring disabled
 * @postdisable:	[DRIVER] function to run after marking ring disabled
  **/
struct iio_ring_buffer {
	struct device dev;
	struct device access_dev;
	struct iio_dev *indio_dev;
	struct module *owner;
	int				id;
	int				access_id;
	int				length;
	int				bytes_per_datum;
	int				bpe;
	int				loopcount;
	struct attribute_group		*scan_el_attrs;
	int				scan_count;
	u32				scan_mask;
	bool				scan_timestamp;
	struct iio_handler		access_handler;
	struct iio_event_interface	ev_int;
	struct iio_shared_ev_pointer	shared_ev_pointer;
	struct iio_ring_access_funcs	access;
	int				(*preenable)(struct iio_dev *);
	int				(*postenable)(struct iio_dev *);
	int				(*predisable)(struct iio_dev *);
	int				(*postdisable)(struct iio_dev *);

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
	ring->loopcount = 0;
}

/**
 * struct iio_scan_el - an individual element of a scan
 * @dev_attr:		control attribute (if directly controllable)
 * @number:		unique identifier of element (used for bit mask)
 * @label:		useful data for the scan el (often reg address)
 * @set_state:		for some devices datardy signals are generated
 *			for any enabled lines.  This allows unwanted lines
 *			to be disabled and hence not get in the way.
 **/
struct iio_scan_el {
	struct device_attribute		dev_attr;
	unsigned int			number;
	unsigned int			label;

	int (*set_state)(struct iio_scan_el *scanel,
			 struct iio_dev *dev_info,
			 bool state);
};

#define to_iio_scan_el(_dev_attr)				\
	container_of(_dev_attr, struct iio_scan_el, dev_attr);

/**
 * iio_scan_el_store() - sysfs scan element selection interface
 * @dev: the target device
 * @attr: the device attribute that is being processed
 * @buf: input from userspace
 * @len: length of input
 *
 * A generic function used to enable various scan elements.  In some
 * devices explicit read commands for each channel mean this is merely
 * a software switch.  In others this must actively disable the channel.
 * Complexities occur when this interacts with data ready type triggers
 * which may not reset unless every channel that is enabled is explicitly
 * read.
 **/
ssize_t iio_scan_el_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t len);
/**
 * iio_scan_el_show() -	sysfs interface to query whether a scan element
 *			is enabled or not
 * @dev: the target device
 * @attr: the device attribute that is being processed
 * @buf: output buffer
 **/
ssize_t iio_scan_el_show(struct device *dev, struct device_attribute *attr,
			 char *buf);

/**
 * iio_scan_el_ts_store() - sysfs interface to set whether a timestamp is included
 *			    in the scan.
 **/
ssize_t iio_scan_el_ts_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t len);
/**
 * iio_scan_el_ts_show() - sysfs interface to query if a timestamp is included
 *			   in the scan.
 **/
ssize_t iio_scan_el_ts_show(struct device *dev, struct device_attribute *attr,
			    char *buf);
/**
 * IIO_SCAN_EL_C - declare and initialize a scan element with a control func
 *
 * @_name:	identifying name. Resulting struct is iio_scan_el_##_name,
 *		sysfs element, _name##_en.
 * @_number:	unique id number for the scan element.
 *		length devices).
 * @_label:	indentification variable used by drivers.  Often a reg address.
 * @_controlfunc: function used to notify hardware of whether state changes
 **/
#define __IIO_SCAN_EL_C(_name, _number, _label, _controlfunc)	\
	struct iio_scan_el iio_scan_el_##_name = {			\
		.dev_attr = __ATTR(_name##_en,				\
				   S_IRUGO | S_IWUSR,			\
				   iio_scan_el_show,			\
				   iio_scan_el_store),			\
		.number =  _number,					\
		.label = _label,					\
		.set_state = _controlfunc,				\
	};								\
	static IIO_CONST_ATTR(_name##_index, #_number)

#define IIO_SCAN_EL_C(_name, _number, _label, _controlfunc)	\
	__IIO_SCAN_EL_C(_name, _number, _label, _controlfunc)

#define __IIO_SCAN_NAMED_EL_C(_name, _string, _number, _label, _cf)	\
	struct iio_scan_el iio_scan_el_##_name = {			\
		.dev_attr = __ATTR(_string##_en,			\
				   S_IRUGO | S_IWUSR,			\
				   iio_scan_el_show,			\
				   iio_scan_el_store),			\
		.number =  _number,					\
		.label = _label,					\
		.set_state = _cf,					\
	};								\
	static struct iio_const_attr iio_const_attr_##_name##_index = {	\
		.string = #_number,					\
		.dev_attr = __ATTR(_string##_index,			\
				   S_IRUGO, iio_read_const_attr, NULL)	\
	}


#define IIO_SCAN_NAMED_EL_C(_name, _string, _number, _label, _cf) \
	__IIO_SCAN_NAMED_EL_C(_name, _string, _number, _label, _cf)
/**
 * IIO_SCAN_EL_TIMESTAMP - declare a special scan element for timestamps
 * @number: specify where in the scan order this is stored.
 *
 * Odd one out. Handled slightly differently from other scan elements.
 **/
#define IIO_SCAN_EL_TIMESTAMP(number)				\
	struct iio_scan_el iio_scan_el_timestamp = {		\
		.dev_attr = __ATTR(timestamp_en,		\
				   S_IRUGO | S_IWUSR,		\
				   iio_scan_el_ts_show,		\
				   iio_scan_el_ts_store),	\
	};							\
	static IIO_CONST_ATTR(timestamp_index, #number)

/**
 * IIO_CONST_ATTR_SCAN_EL_TYPE - attr to specify the data format of a scan el
 * @name: the scan el name (may be more general and cover a set of scan elements
 * @_sign: either s or u for signed or unsigned
 * @_bits: number of actual bits occuplied by the value
 * @_storagebits: number of bits _bits is padded to when read out of buffer
 **/
#define IIO_CONST_ATTR_SCAN_EL_TYPE(_name, _sign, _bits, _storagebits) \
	IIO_CONST_ATTR(_name##_type, #_sign#_bits"/"#_storagebits);

/**
 * IIO_CONST_ATTR_SCAN_EL_TYPE_WITH_SHIFT - attr to specify the data format of a scan el
 * @name: the scan el name (may be more general and cover a set of scan elements
 * @_sign: either s or u for signed or unsigned
 * @_bits: number of actual bits occuplied by the value
 * @_storagebits: number of bits _bits is padded to when read out of buffer
 * @_shiftbits: number of bits _shiftbits the result must be shifted
 **/
#define IIO_CONST_ATTR_SCAN_EL_TYPE_WITH_SHIFT(_name, _sign, _bits, \
					       _storagebits, _shiftbits) \
	IIO_CONST_ATTR(_name##_type, #_sign#_bits"/"#_storagebits \
		       ">>"#_shiftbits);

#define IIO_SCAN_EL_TYPE_SIGNED         's'
#define IIO_SCAN_EL_TYPE_UNSIGNED       'u'

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
 * iio_scan_mask_clear() - clear a particular element from the scan mask
 * @ring: the ring buffer whose scan mask we are interested in
 * @bit: the bit to clear
 **/
static inline int iio_scan_mask_clear(struct iio_ring_buffer *ring, int bit)
{
	if (bit > IIO_MAX_SCAN_LENGTH)
		return -EINVAL;
	ring->scan_mask &= ~(1 << bit);
	ring->scan_count--;
	return 0;
};

/**
 * iio_scan_mask_count_to_right() - how many scan elements occur before here
 * @ring: the ring buffer whose scan mask we interested in
 * @bit: which number scan element is this
 **/
static inline int iio_scan_mask_count_to_right(struct iio_ring_buffer *ring,
						int bit)
{
	int count = 0;
	int mask = (1 << bit);
	if (bit > IIO_MAX_SCAN_LENGTH)
		return -EINVAL;
	while (mask) {
		mask >>= 1;
		if (mask & ring->scan_mask)
			count++;
	}

	return count;
}

/**
 * iio_put_ring_buffer() - notify done with buffer
 * @ring: the buffer we are done with.
 **/
static inline void iio_put_ring_buffer(struct iio_ring_buffer *ring)
{
	put_device(&ring->dev);
};

#define to_iio_ring_buffer(d)			\
	container_of(d, struct iio_ring_buffer, dev)
#define access_dev_to_iio_ring_buffer(d)			\
	container_of(d, struct iio_ring_buffer, access_dev)

/**
 * iio_ring_buffer_register() - register the buffer with IIO core
 * @ring: the buffer to be registered
 * @id: the id of the buffer (typically 0)
 **/
int iio_ring_buffer_register(struct iio_ring_buffer *ring, int id);

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
#else /* CONFIG_IIO_RING_BUFFER */
static inline int iio_ring_buffer_register(struct iio_ring_buffer *ring, int id)
{
	return 0;
};
static inline void iio_ring_buffer_unregister(struct iio_ring_buffer *ring)
{};

#endif /* CONFIG_IIO_RING_BUFFER */

#endif /* _IIO_RING_GENERIC_H_ */
