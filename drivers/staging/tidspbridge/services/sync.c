/*
 * sync.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Synchronization services.
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

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- This */
#include <dspbridge/sync.h>

DEFINE_SPINLOCK(sync_lock);

/**
 * sync_set_event() - set or signal and specified event
 * @event:	Event to be set..
 *
 * set the @event, if there is an thread waiting for the event
 * it will be waken up, this function only wakes one thread.
 */

void sync_set_event(struct sync_object *event)
{
	spin_lock_bh(&sync_lock);
	complete(&event->comp);
	if (event->multi_comp)
		complete(event->multi_comp);
	spin_unlock_bh(&sync_lock);
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
 * @pu_index will store the index of the array element set or in case
 * of timeout the function will return -ETIME or in case of
 * interrupting by a signal it will return -EPERM.
 */

int sync_wait_on_multiple_events(struct sync_object **events,
				     unsigned count, unsigned timeout,
				     unsigned *index)
{
	unsigned i;
	int status = -EPERM;
	struct completion m_comp;

	init_completion(&m_comp);

	if (SYNC_INFINITE == timeout)
		timeout = MAX_SCHEDULE_TIMEOUT;

	spin_lock_bh(&sync_lock);
	for (i = 0; i < count; i++) {
		if (completion_done(&events[i]->comp)) {
			INIT_COMPLETION(events[i]->comp);
			*index = i;
			spin_unlock_bh(&sync_lock);
			status = 0;
			goto func_end;
		}
	}

	for (i = 0; i < count; i++)
		events[i]->multi_comp = &m_comp;

	spin_unlock_bh(&sync_lock);

	if (!wait_for_completion_interruptible_timeout(&m_comp,
					msecs_to_jiffies(timeout)))
		status = -ETIME;

	spin_lock_bh(&sync_lock);
	for (i = 0; i < count; i++) {
		if (completion_done(&events[i]->comp)) {
			INIT_COMPLETION(events[i]->comp);
			*index = i;
			status = 0;
		}
		events[i]->multi_comp = NULL;
	}
	spin_unlock_bh(&sync_lock);
func_end:
	return status;
}

