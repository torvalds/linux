/*
 * ntfy.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Manage lists of notification events.
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

#ifndef NTFY_
#define NTFY_

#include <dspbridge/host_os.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/sync.h>

/**
 * ntfy_object - head structure to nofify dspbridge events
 * @head:	List of notify objects
 * @ntfy_lock:	lock for list access.
 *
 */
struct ntfy_object {
	struct raw_notifier_head head;/* List of notifier objects */
	spinlock_t ntfy_lock;	/* For critical sections */
};

/**
 * ntfy_event - structure store specify event to be notified
 * @noti_block:	List of notify objects
 * @event:	event that it respond
 * @type: 	event type (only DSP_SIGNALEVENT supported)
 * @sync_obj:	sync_event used to set the event
 *
 */
struct ntfy_event {
	struct notifier_block noti_block;
	u32 event;	/* Events to be notified about */
	u32 type;	/* Type of notification to be sent */
	struct sync_object sync_obj;
};


/**
 * dsp_notifier_event() - callback function to nofity events
 * @this:		pointer to itself struct notifier_block
 * @event:	event to be notified.
 * @data:		Currently not used.
 *
 */
int dsp_notifier_event(struct notifier_block *this, unsigned long event,
			   void *data);

/**
 * ntfy_init() - Set the initial state of the ntfy_object structure.
 * @no:		pointer to ntfy_object structure.
 *
 * This function sets the initial state of the ntfy_object in order it
 * can be used by the other ntfy functions.
 */

static inline void ntfy_init(struct ntfy_object *no)
{
	spin_lock_init(&no->ntfy_lock);
	RAW_INIT_NOTIFIER_HEAD(&no->head);
}

/**
 * ntfy_delete() - delete list of nofy events registered.
 * @ntfy_obj:	Pointer to the ntfy object structure.
 *
 * This function is used to remove all the notify events  registered.
 * unregister function is not needed in this function, to unregister
 * a ntfy_event please look at ntfy_register function.
 *
 */
static inline void ntfy_delete(struct ntfy_object *ntfy_obj)
{
	struct ntfy_event *ne;
	struct notifier_block *nb;

	spin_lock_bh(&ntfy_obj->ntfy_lock);
	nb = ntfy_obj->head.head;
	while (nb) {
		ne = container_of(nb, struct ntfy_event, noti_block);
		nb = nb->next;
		kfree(ne);
	}
	spin_unlock_bh(&ntfy_obj->ntfy_lock);
}

/**
 * ntfy_notify() - nofity all event register for an specific event.
 * @ntfy_obj:	Pointer to the ntfy_object structure.
 * @event:	event to be notified.
 *
 * This function traverses all the ntfy events registers and
 * set the event with mach with @event.
 */
static inline void ntfy_notify(struct ntfy_object *ntfy_obj, u32 event)
{
	spin_lock_bh(&ntfy_obj->ntfy_lock);
	raw_notifier_call_chain(&ntfy_obj->head, event, NULL);
	spin_unlock_bh(&ntfy_obj->ntfy_lock);
}



/**
 * ntfy_init() - Create and initialize a ntfy_event structure.
 * @event:	event that the ntfy event will respond
 * @type		event type (only DSP_SIGNALEVENT supported)
 *
 * This function create a ntfy_event element and sets the event it will
 * respond the ntfy_event in order it can be used by the other ntfy functions.
 * In case of success it will return a pointer to the ntfy_event struct
 * created. Otherwise it will return NULL;
 */

static inline struct ntfy_event *ntfy_event_create(u32 event, u32 type)
{
	struct ntfy_event *ne;
	ne = kmalloc(sizeof(struct ntfy_event), GFP_KERNEL);
	if (ne) {
		sync_init_event(&ne->sync_obj);
		ne->noti_block.notifier_call = dsp_notifier_event;
		ne->event = event;
		ne->type = type;
	}
	return ne;
}

/**
 * ntfy_register() - register new ntfy_event into a given ntfy_object
 * @ntfy_obj:	Pointer to the ntfy_object structure.
 * @noti:		Pointer to the handle to be returned to the user space.
 * @event	event that the ntfy event will respond
 * @type		event type (only DSP_SIGNALEVENT supported)
 *
 * This function register a new ntfy_event into the ntfy_object list,
 * which will respond to the @event passed.
 * This function will return 0 in case of error.
 * -EFAULT in case of bad pointers and
 * DSP_EMemory in case of no memory to create ntfy_event.
 */
static  inline int ntfy_register(struct ntfy_object *ntfy_obj,
			 struct dsp_notification *noti,
			 u32 event, u32 type)
{
	struct ntfy_event *ne;
	int status = 0;

	if (!noti || !ntfy_obj) {
		status = -EFAULT;
		goto func_end;
	}
	if (!event) {
		status = -EINVAL;
		goto func_end;
	}
	ne = ntfy_event_create(event, type);
	if (!ne) {
		status = -ENOMEM;
		goto func_end;
	}
	noti->handle = &ne->sync_obj;

	spin_lock_bh(&ntfy_obj->ntfy_lock);
	raw_notifier_chain_register(&ntfy_obj->head, &ne->noti_block);
	spin_unlock_bh(&ntfy_obj->ntfy_lock);
func_end:
	return status;
}

/**
 * ntfy_unregister() - unregister a ntfy_event from a given ntfy_object
 * @ntfy_obj:	Pointer to the ntfy_object structure.
 * @noti:		Pointer to the event that will be removed.
 *
 * This function unregister a ntfy_event from the ntfy_object list,
 * @noti contains the event which is wanted to be removed.
 * This function will return 0 in case of error.
 * -EFAULT in case of bad pointers and
 * DSP_EMemory in case of no memory to create ntfy_event.
 */
static  inline int ntfy_unregister(struct ntfy_object *ntfy_obj,
			 struct dsp_notification *noti)
{
	int status = 0;
	struct ntfy_event *ne;

	if (!noti || !ntfy_obj) {
		status = -EFAULT;
		goto func_end;
	}

	ne = container_of((struct sync_object *)noti, struct ntfy_event,
								sync_obj);
	spin_lock_bh(&ntfy_obj->ntfy_lock);
	raw_notifier_chain_unregister(&ntfy_obj->head,
						&ne->noti_block);
	kfree(ne);
	spin_unlock_bh(&ntfy_obj->ntfy_lock);
func_end:
	return status;
}

#endif				/* NTFY_ */
