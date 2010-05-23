/* The industrial I/O core - character device related
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _IIO_CHRDEV_H_
#define _IIO_CHRDEV_H_
struct iio_dev;

/**
 * struct iio_handler - Structure used to specify file operations
 *			for a particular chrdev
 * @chrdev:	character device structure
 * @id:		the location in the handler table - used for deallocation.
 * @flags:	file operations related flags including busy flag.
 * @private:	handler specific data used by the fileops registered with
 *		the chrdev.
 */
struct iio_handler {
	struct cdev	chrdev;
	int		id;
	unsigned long	flags;
	void		*private;
};

#define iio_cdev_to_handler(cd)				\
	container_of(cd, struct iio_handler, chrdev)

/**
 * struct iio_event_data - The actual event being pushed to userspace
 * @id:		event identifier
 * @timestamp:	best estimate of time of event occurance (often from
 *		the interrupt handler)
 */
struct iio_event_data {
	int	id;
	s64	timestamp;
};

/**
 * struct iio_detected_event_list - list element for events that have occured
 * @list:		linked list header
 * @ev:			the event itself
 * @shared_pointer:	used when the event is shared - i.e. can be escallated
 *			on demand (eg ring buffer 50%->100% full)
 */
struct iio_detected_event_list {
	struct list_head		list;
	struct iio_event_data		ev;
	struct iio_shared_ev_pointer	*shared_pointer;
};
/**
 * struct iio_shared_ev_pointer - allows shared events to identify if currently
 *				in the detected event list
 * @ev_p:	pointer to detected event list element (null if not in list)
 * @lock:	protect this element to prevent simultaneous edit and remove
 */
struct iio_shared_ev_pointer {
	struct iio_detected_event_list	*ev_p;
	spinlock_t			lock;
};

/**
 * struct iio_event_interface - chrdev interface for an event line
 * @dev:		device assocated with event interface
 * @handler:		fileoperations and related control for the chrdev
 * @wait:		wait queue to allow blocking reads of events
 * @event_list_lock:	mutex to protect the list of detected events
 * @det_events:		list of detected events
 * @max_events:		maximum number of events before new ones are dropped
 * @current_events:	number of events in detected list
 * @id:			indentifier to allow the event interface to know which
 *			physical line it corresponds to
 * @attr:		this chrdev's minor number sysfs attribute
 * @owner:		ensure the driver module owns the file, not iio
 * @private:		driver specific data
 * @_name:		used internally to store the sysfs name for minor id
 *			attribute
 * @_attrname:		the event interface's attribute name
 */
struct iio_event_interface {
	struct device				dev;
	struct iio_handler			handler;
	wait_queue_head_t			wait;
	struct mutex				event_list_lock;
	struct iio_detected_event_list		det_events;
	int					max_events;
	int					current_events;
	int					id;
	struct iio_chrdev_minor_attr		attr;
	struct module				*owner;
	void					*private;
	char					_name[35];
	char					_attrname[20];
};

/**
 * struct iio_event_handler_list - element in list of handlers for events
 * @list:		list header
 * @refcount:		as the handler may be shared between multiple device
 *			side events, reference counting ensures clean removal
 * @exist_lock:		prevents race conditions related to refcount useage.
 * @handler:		event handler function - called on event if this
 *			event_handler is enabled.
 *
 * Each device has one list of these per interrupt line.
 **/
struct iio_event_handler_list {
	struct list_head	list;
	int			refcount;
	struct mutex		exist_lock;
	int (*handler)(struct iio_dev *dev_info, int index, s64 timestamp,
		       int no_test);
};

#endif
