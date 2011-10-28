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
 * @timestamp:	best estimate of time of event occurrence (often from
 *		the interrupt handler)
 */
struct iio_event_data {
	int	id;
	s64	timestamp;
};

/**
 * struct iio_detected_event_list - list element for events that have occurred
 * @list:		linked list header
 * @ev:			the event itself
 */
struct iio_detected_event_list {
	struct list_head		list;
	struct iio_event_data		ev;
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
 */
struct iio_event_interface {
	struct device				dev;
	struct iio_handler			handler;
	wait_queue_head_t			wait;
	struct mutex				event_list_lock;
	struct list_head			det_events;
	int					max_events;
	int					current_events;
	struct list_head dev_attr_list;
};

#endif
