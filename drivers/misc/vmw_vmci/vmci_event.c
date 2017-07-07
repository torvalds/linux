/*
 * VMware VMCI Driver
 *
 * Copyright (C) 2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation version 2 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <linux/vmw_vmci_defs.h>
#include <linux/vmw_vmci_api.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/rculist.h>

#include "vmci_driver.h"
#include "vmci_event.h"

#define EVENT_MAGIC 0xEABE0000
#define VMCI_EVENT_MAX_ATTEMPTS 10

struct vmci_subscription {
	u32 id;
	u32 event;
	vmci_event_cb callback;
	void *callback_data;
	struct list_head node;	/* on one of subscriber lists */
};

static struct list_head subscriber_array[VMCI_EVENT_MAX];
static DEFINE_MUTEX(subscriber_mutex);

int __init vmci_event_init(void)
{
	int i;

	for (i = 0; i < VMCI_EVENT_MAX; i++)
		INIT_LIST_HEAD(&subscriber_array[i]);

	return VMCI_SUCCESS;
}

void vmci_event_exit(void)
{
	int e;

	/* We free all memory at exit. */
	for (e = 0; e < VMCI_EVENT_MAX; e++) {
		struct vmci_subscription *cur, *p2;
		list_for_each_entry_safe(cur, p2, &subscriber_array[e], node) {

			/*
			 * We should never get here because all events
			 * should have been unregistered before we try
			 * to unload the driver module.
			 */
			pr_warn("Unexpected free events occurring\n");
			list_del(&cur->node);
			kfree(cur);
		}
	}
}

/*
 * Find entry. Assumes subscriber_mutex is held.
 */
static struct vmci_subscription *event_find(u32 sub_id)
{
	int e;

	for (e = 0; e < VMCI_EVENT_MAX; e++) {
		struct vmci_subscription *cur;
		list_for_each_entry(cur, &subscriber_array[e], node) {
			if (cur->id == sub_id)
				return cur;
		}
	}
	return NULL;
}

/*
 * Actually delivers the events to the subscribers.
 * The callback function for each subscriber is invoked.
 */
static void event_deliver(struct vmci_event_msg *event_msg)
{
	struct vmci_subscription *cur;
	struct list_head *subscriber_list;

	rcu_read_lock();
	subscriber_list = &subscriber_array[event_msg->event_data.event];
	list_for_each_entry_rcu(cur, subscriber_list, node) {
		cur->callback(cur->id, &event_msg->event_data,
			      cur->callback_data);
	}
	rcu_read_unlock();
}

/*
 * Dispatcher for the VMCI_EVENT_RECEIVE datagrams. Calls all
 * subscribers for given event.
 */
int vmci_event_dispatch(struct vmci_datagram *msg)
{
	struct vmci_event_msg *event_msg = (struct vmci_event_msg *)msg;

	if (msg->payload_size < sizeof(u32) ||
	    msg->payload_size > sizeof(struct vmci_event_data_max))
		return VMCI_ERROR_INVALID_ARGS;

	if (!VMCI_EVENT_VALID(event_msg->event_data.event))
		return VMCI_ERROR_EVENT_UNKNOWN;

	event_deliver(event_msg);
	return VMCI_SUCCESS;
}

/*
 * vmci_event_subscribe() - Subscribe to a given event.
 * @event:      The event to subscribe to.
 * @callback:   The callback to invoke upon the event.
 * @callback_data:      Data to pass to the callback.
 * @subscription_id:    ID used to track subscription.  Used with
 *              vmci_event_unsubscribe()
 *
 * Subscribes to the provided event. The callback specified will be
 * fired from RCU critical section and therefore must not sleep.
 */
int vmci_event_subscribe(u32 event,
			 vmci_event_cb callback,
			 void *callback_data,
			 u32 *new_subscription_id)
{
	struct vmci_subscription *sub;
	int attempts;
	int retval;
	bool have_new_id = false;

	if (!new_subscription_id) {
		pr_devel("%s: Invalid subscription (NULL)\n", __func__);
		return VMCI_ERROR_INVALID_ARGS;
	}

	if (!VMCI_EVENT_VALID(event) || !callback) {
		pr_devel("%s: Failed to subscribe to event (type=%d) (callback=%p) (data=%p)\n",
			 __func__, event, callback, callback_data);
		return VMCI_ERROR_INVALID_ARGS;
	}

	sub = kzalloc(sizeof(*sub), GFP_KERNEL);
	if (!sub)
		return VMCI_ERROR_NO_MEM;

	sub->id = VMCI_EVENT_MAX;
	sub->event = event;
	sub->callback = callback;
	sub->callback_data = callback_data;
	INIT_LIST_HEAD(&sub->node);

	mutex_lock(&subscriber_mutex);

	/* Creation of a new event is always allowed. */
	for (attempts = 0; attempts < VMCI_EVENT_MAX_ATTEMPTS; attempts++) {
		static u32 subscription_id;
		/*
		 * We try to get an id a couple of time before
		 * claiming we are out of resources.
		 */

		/* Test for duplicate id. */
		if (!event_find(++subscription_id)) {
			sub->id = subscription_id;
			have_new_id = true;
			break;
		}
	}

	if (have_new_id) {
		list_add_rcu(&sub->node, &subscriber_array[event]);
		retval = VMCI_SUCCESS;
	} else {
		retval = VMCI_ERROR_NO_RESOURCES;
	}

	mutex_unlock(&subscriber_mutex);

	*new_subscription_id = sub->id;
	return retval;
}
EXPORT_SYMBOL_GPL(vmci_event_subscribe);

/*
 * vmci_event_unsubscribe() - unsubscribe from an event.
 * @sub_id:     A subscription ID as provided by vmci_event_subscribe()
 *
 * Unsubscribe from given event. Removes it from list and frees it.
 * Will return callback_data if requested by caller.
 */
int vmci_event_unsubscribe(u32 sub_id)
{
	struct vmci_subscription *s;

	mutex_lock(&subscriber_mutex);
	s = event_find(sub_id);
	if (s)
		list_del_rcu(&s->node);
	mutex_unlock(&subscriber_mutex);

	if (!s)
		return VMCI_ERROR_NOT_FOUND;

	synchronize_rcu();
	kfree(s);

	return VMCI_SUCCESS;
}
EXPORT_SYMBOL_GPL(vmci_event_unsubscribe);
