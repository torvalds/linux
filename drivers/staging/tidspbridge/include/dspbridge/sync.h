/*
 * sync.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Provide synchronization services.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _SYNC_H
#define _SYNC_H

#include <dspbridge/dbdefs.h>
#include <dspbridge/host_os.h>


/* Special timeout value indicating an infinite wait: */
#define SYNC_INFINITE  0xffffffff

/**
 * struct sync_object - the basic sync_object structure
 * @comp:	use to signal events
 * @multi_comp:	use to signal multiple events.
 *
 */
struct sync_object{
	struct completion comp;
	struct completion *multi_comp;
};

/**
 * sync_init_event() - set initial state for a sync_event element
 * @event:	event to be initialized.
 *
 * Set the initial state for a sync_event element.
 */

static inline void sync_init_event(struct sync_object *event)
{
	init_completion(&event->comp);
	event->multi_comp = NULL;
}

/**
 * sync_reset_event() - reset a sync_event element
 * @event:	event to be reset.
 *
 * This function reset to the initial state to @event.
 */

static inline void sync_reset_event(struct sync_object *event)
{
	INIT_COMPLETION(event->comp);
	event->multi_comp = NULL;
}

/**
 * sync_set_event() - set or signal and specified event
 * @event:	Event to be set..
 *
 * set the @event, if there is an thread waiting for the event
 * it will be waken up, this function only wakes one thread.
 */

void sync_set_event(struct sync_object *event);

/**
 * sync_wait_on_event() - waits for a event to be set.
 * @event:	events to wait for it.
 * @timeout	timeout on waiting for the evetn.
 *
 * This functios will wait until @event is set or until timeout. In case of
 * success the function will return 0 and
 * in case of timeout the function will return -ETIME
 * in case of signal the function will return -ERESTARTSYS
 */

static inline int sync_wait_on_event(struct sync_object *event,
							unsigned timeout)
{
	int res;

	res = wait_for_completion_interruptible_timeout(&event->comp,
						msecs_to_jiffies(timeout));
	if (!res)
		res = -ETIME;
	else if (res > 0)
		res = 0;

	return res;
}

/**
 * sync_wait_on_multiple_events() - waits for multiple events to be set.
 * @events:	Array of events to wait for them.
 * @count:	number of elements of the array.
 * @timeout	timeout on waiting for the evetns.
 * @pu_index	index of the event set.
 *
 * This functios will wait until any of the array element is set or until
 * timeout. In case of success the function will return 0 and
 * @pu_index will store the index of the array element set and in case
 * of timeout the function will return -ETIME.
 */

int sync_wait_on_multiple_events(struct sync_object **events,
				     unsigned count, unsigned timeout,
				     unsigned *index);

#endif /* _SYNC_H */
