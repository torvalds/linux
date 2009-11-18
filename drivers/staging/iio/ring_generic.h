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

struct iio_handler;
struct iio_ring_buffer;
struct iio_dev;

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
 * iio_push_or_escallate_ring_event() -	escallate or add as appropriate
 *
 * Typical usecase is to escallate a 50% ring full to 75% full if noone has yet
 * read the first event. Clearly the 50% full is no longer of interest in
 * typical use case.
 **/
int iio_push_or_escallate_ring_event(struct iio_ring_buffer *ring_buf,
				     int event_code,
				     s64 timestamp);

/**
 * struct iio_ring_access_funcs - access functions for ring buffers.
 * @create:		perform allocation
 * @init:		get ring buffer ready for use
 * @_exit:		reverse steps in init
 * @_free:		deallocate ring buffer
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
 * @get_bpd:		get current bytes per datum
 * @set_bpd:		set number of bytes per datum
 * @get_length:		get number of datums in ring
 * @set_length:		set number of datums in ring
 * @is_enabled:		query if ring is currently being used
 * @enable:		enable the ring
 *
 * The purpose of this structure is to make the ring buffer element
 * modular as event for a given driver, different usecases may require
 * different ring designs (space efficiency vs speed for example.
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

	int (*get_bpd)(struct iio_ring_buffer *ring);
	int (*set_bpd)(struct iio_ring_buffer *ring, size_t bpd);
	int (*get_length)(struct iio_ring_buffer *ring);
	int (*set_length)(struct iio_ring_buffer *ring, int length);

	int (*is_enabled)(struct iio_ring_buffer *ring);
	int (*enable)(struct iio_ring_buffer *ring);
};

/**
 * struct iio_ring_buffer - general ring buffer structure
 * @length:		[DEVICE]number of datums in ring
 * @bpd:		[DEVICE]size of individual datum including timestamp
 * @loopcount:		[INTERN]number of times the ring has looped
 * @access_minor_name:	[INTERN]store of name of the access chrdev minor number
 *			sysfs attribute
 * @access_handler:	[INTERN]chrdev access handling
 * @event_minor_name:	[INTERN]store of name of the event chrdev minor number
 *			sysfs attribute
 * @ev_int:		[INTERN]chrdev interface for the event chrdev
 * @shared_ev_pointer:	[INTERN]the shared event pointer to allow escalation of
 *			events
 * @ring_access:	[DRIVER]ring access functions associated with the
 *			implementation.
 * @ring_prenable:	[DRIVER] function to run prior to marking ring enabled
 * @ring_postenable:	[DRIVER] function to run after marking ring enabled
 * @ring_predisable:	[DRIVER] function to run prior to marking ring disabled
 * @ring_postdisable:	[DRIVER] function to run after marking ring disabled
  **/
struct iio_ring_buffer {
	struct device dev;
	struct device access_dev;
	struct iio_dev *indio_dev;
	struct module *owner;
	int				id;
	int				access_id;
	int				length;
	int				bpd;
	int				loopcount;
	struct iio_handler		access_handler;
	struct iio_event_interface	ev_int;
	struct iio_shared_ev_pointer	shared_ev_pointer;
	struct iio_ring_access_funcs	access;
	int				(*preenable)(struct iio_dev *);
	int				(*postenable)(struct iio_dev *);
	int				(*predisable)(struct iio_dev *);
	int				(*postdisable)(struct iio_dev *);

};
void iio_ring_buffer_init(struct iio_ring_buffer *ring,
			  struct iio_dev *dev_info);

/**
 * __iio_init_ring_buffer() - initialize common elements of ring buffers.
 **/
static inline void __iio_init_ring_buffer(struct iio_ring_buffer *ring,
				 int bytes_per_datum, int length)
{
	ring->bpd = bytes_per_datum;
	ring->length = length;
	ring->loopcount = 0;
	ring->shared_ev_pointer.ev_p = 0;
	ring->shared_ev_pointer.lock =
		__SPIN_LOCK_UNLOCKED(ring->shared_ev_pointer->loc);
}

/**
 * struct iio_scan_el - an individual element of a scan
 * @dev_attr:		control attribute (if directly controllable)
 * @number:		unique identifier of element (used for bit mask)
 * @bit_count:		number of bits in scan element
 * @label:		useful data for the scan el (often reg address)
 * @set_state:		for some devices datardy signals are generated
 *			for any enabled lines.  This allows unwanted lines
 *			to be disabled and hence not get in the way.
 **/
struct iio_scan_el {
	struct device_attribute		dev_attr;
	unsigned int			number;
	int				bit_count;
	unsigned int			label;

	int (*set_state)(struct iio_scan_el *scanel,
			 struct iio_dev *dev_info,
			 bool state);
};

#define to_iio_scan_el(_dev_attr)				\
	container_of(_dev_attr, struct iio_scan_el, dev_attr);

/**
 * iio_scan_el_store() - sysfs scan element selection interface.
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
 * iio_scal_el_show() -	sysfs interface to query whether a scan element is
 *			is enabled or not.
 **/
ssize_t iio_scan_el_show(struct device *dev, struct device_attribute *attr,
			 char *buf);
/**
 * IIO_SCAN_EL: - declare and initialize a scan element without control func
 * @_name:	identifying name. Resulting struct is iio_scan_el_##_name,
 *		sysfs element, scan_en_##_name.
 * @_number:	unique id number for the scan element.
 * @_bits:	number of bits in the scan element result (used in mixed bit
 *		length devices).
 * @_label:	indentification variable used by drivers.  Often a reg address.
 **/
#define IIO_SCAN_EL(_name, _number, _bits, _label)			\
	struct iio_scan_el iio_scan_el_##_name = {			\
		.dev_attr = __ATTR(scan_en_##_name,			\
				   S_IRUGO | S_IWUSR,			\
				   iio_scan_el_show,			\
				   iio_scan_el_store),			\
		.mask = (1 << _number),					\
		.bit_count = _bits,					\
		.label = _label,					\
	}

ssize_t iio_scan_el_ts_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t len);

ssize_t iio_scan_el_ts_show(struct device *dev, struct device_attribute *attr,
			    char *buf);
/**
 * IIO_SCAN_EL_C: - declare and initialize a scan element with a control func
 *
 * @_controlfunc: function used to notify hardware of whether state changes
 **/
#define IIO_SCAN_EL_C(_name, _number, _bits, _label, _controlfunc)	\
	struct iio_scan_el iio_scan_el_##_name = {			\
		.dev_attr = __ATTR(scan_en_##_name,			\
				   S_IRUGO | S_IWUSR,			\
				   iio_scan_el_show,			\
				   iio_scan_el_store),			\
		.number =  _number,					\
		.bit_count = _bits,					\
		.label = _label,					\
		.set_state = _controlfunc,				\
	}
/**
 * IIO_SCAN_EL_TIMESTAMP: - declare a special scan element for timestamps
 *
 * Odd one out. Handled slightly differently from other scan elements.
 **/
#define IIO_SCAN_EL_TIMESTAMP					\
	struct iio_scan_el iio_scan_el_timestamp = {		\
		.dev_attr = __ATTR(scan_en_timestamp,		\
				   S_IRUGO | S_IWUSR,		\
				   iio_scan_el_ts_show,		\
				   iio_scan_el_ts_store),	\
	}

static inline void iio_put_ring_buffer(struct iio_ring_buffer *ring)
{
	put_device(&ring->dev);
};

#define to_iio_ring_buffer(d)			\
	container_of(d, struct iio_ring_buffer, dev)
#define access_dev_to_iio_ring_buffer(d)			\
	container_of(d, struct iio_ring_buffer, access_dev)
int iio_ring_buffer_register(struct iio_ring_buffer *ring);
void iio_ring_buffer_unregister(struct iio_ring_buffer *ring);

ssize_t iio_read_ring_length(struct device *dev,
			     struct device_attribute *attr,
			     char *buf);
ssize_t iio_write_ring_length(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t len);
ssize_t iio_read_ring_bps(struct device *dev,
			  struct device_attribute *attr,
			  char *buf);
ssize_t iio_store_ring_enable(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t len);
ssize_t iio_show_ring_enable(struct device *dev,
			     struct device_attribute *attr,
			     char *buf);
#define IIO_RING_LENGTH_ATTR DEVICE_ATTR(length, S_IRUGO | S_IWUSR,	\
					 iio_read_ring_length,		\
					 iio_write_ring_length)
#define IIO_RING_BPS_ATTR DEVICE_ATTR(bps, S_IRUGO | S_IWUSR,	\
				      iio_read_ring_bps, NULL)
#define IIO_RING_ENABLE_ATTR DEVICE_ATTR(ring_enable, S_IRUGO | S_IWUSR, \
					 iio_show_ring_enable,		\
					 iio_store_ring_enable)

#endif /* _IIO_RING_GENERIC_H_ */
