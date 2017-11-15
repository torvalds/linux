/*
 * Ultra Wide Band
 * Neighborhood Management Daemon
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * This daemon takes care of maintaing information that describes the
 * UWB neighborhood that the radios in this machine can see. It also
 * keeps a tab of which devices are visible, makes sure each HC sits
 * on a different channel to avoid interfering, etc.
 *
 * Different drivers (radio controller, device, any API in general)
 * communicate with this daemon through an event queue. Daemon wakes
 * up, takes a list of events and handles them one by one; handling
 * function is extracted from a table based on the event's type and
 * subtype. Events are freed only if the handling function says so.
 *
 *   . Lock protecting the event list has to be an spinlock and locked
 *     with IRQSAVE because it might be called from an interrupt
 *     context (ie: when events arrive and the notification drops
 *     down from the ISR).
 *
 *   . UWB radio controller drivers queue events to the daemon using
 *     uwbd_event_queue(). They just get the event, chew it to make it
 *     look like UWBD likes it and pass it in a buffer allocated with
 *     uwb_event_alloc().
 *
 * EVENTS
 *
 * Events have a type, a subtype, a length, some other stuff and the
 * data blob, which depends on the event. The header is 'struct
 * uwb_event'; for payloads, see 'struct uwbd_evt_*'.
 *
 * EVENT HANDLER TABLES
 *
 * To find a handling function for an event, the type is used to index
 * a subtype-table in the type-table. The subtype-table is indexed
 * with the subtype to get the function that handles the event. Start
 * with the main type-table 'uwbd_evt_type_handler'.
 *
 * DEVICES
 *
 * Devices are created when a bunch of beacons have been received and
 * it is stablished that the device has stable radio presence. CREATED
 * only, not configured. Devices are ONLY configured when an
 * Application-Specific IE Probe is receieved, in which the device
 * declares which Protocol ID it groks. Then the device is CONFIGURED
 * (and the driver->probe() stuff of the device model is invoked).
 *
 * Devices are considered disconnected when a certain number of
 * beacons are not received in an amount of time.
 *
 * Handler functions are called normally uwbd_evt_handle_*().
 */
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/freezer.h>

#include "uwb-internal.h"

/*
 * UWBD Event handler function signature
 *
 * Return !0 if the event needs not to be freed (ie the handler
 * takes/took care of it). 0 means the daemon code will free the
 * event.
 *
 * @evt->rc is already referenced and guaranteed to exist. See
 * uwb_evt_handle().
 */
typedef int (*uwbd_evt_handler_f)(struct uwb_event *);

/**
 * Properties of a UWBD event
 *
 * @handler:    the function that will handle this event
 * @name:       text name of event
 */
struct uwbd_event {
	uwbd_evt_handler_f handler;
	const char *name;
};

/* Table of handlers for and properties of the UWBD Radio Control Events */
static struct uwbd_event uwbd_urc_events[] = {
	[UWB_RC_EVT_IE_RCV] = {
		.handler = uwbd_evt_handle_rc_ie_rcv,
		.name = "IE_RECEIVED"
	},
	[UWB_RC_EVT_BEACON] = {
		.handler = uwbd_evt_handle_rc_beacon,
		.name = "BEACON_RECEIVED"
	},
	[UWB_RC_EVT_BEACON_SIZE] = {
		.handler = uwbd_evt_handle_rc_beacon_size,
		.name = "BEACON_SIZE_CHANGE"
	},
	[UWB_RC_EVT_BPOIE_CHANGE] = {
		.handler = uwbd_evt_handle_rc_bpoie_change,
		.name = "BPOIE_CHANGE"
	},
	[UWB_RC_EVT_BP_SLOT_CHANGE] = {
		.handler = uwbd_evt_handle_rc_bp_slot_change,
		.name = "BP_SLOT_CHANGE"
	},
	[UWB_RC_EVT_DRP_AVAIL] = {
		.handler = uwbd_evt_handle_rc_drp_avail,
		.name = "DRP_AVAILABILITY_CHANGE"
	},
	[UWB_RC_EVT_DRP] = {
		.handler = uwbd_evt_handle_rc_drp,
		.name = "DRP"
	},
	[UWB_RC_EVT_DEV_ADDR_CONFLICT] = {
		.handler = uwbd_evt_handle_rc_dev_addr_conflict,
		.name = "DEV_ADDR_CONFLICT",
	},
};



struct uwbd_evt_type_handler {
	const char *name;
	struct uwbd_event *uwbd_events;
	size_t size;
};

/* Table of handlers for each UWBD Event type. */
static struct uwbd_evt_type_handler uwbd_urc_evt_type_handlers[] = {
	[UWB_RC_CET_GENERAL] = {
		.name        = "URC",
		.uwbd_events = uwbd_urc_events,
		.size        = ARRAY_SIZE(uwbd_urc_events),
	},
};

static const struct uwbd_event uwbd_message_handlers[] = {
	[UWB_EVT_MSG_RESET] = {
		.handler = uwbd_msg_handle_reset,
		.name = "reset",
	},
};

/*
 * Handle an URC event passed to the UWB Daemon
 *
 * @evt: the event to handle
 * @returns: 0 if the event can be kfreed, !0 on the contrary
 *           (somebody else took ownership) [coincidentally, returning
 *           a <0 errno code will free it :)].
 *
 * Looks up the two indirection tables (one for the type, one for the
 * subtype) to decide which function handles it and then calls the
 * handler.
 *
 * The event structure passed to the event handler has the radio
 * controller in @evt->rc referenced. The reference will be dropped
 * once the handler returns, so if it needs it for longer (async),
 * it'll need to take another one.
 */
static
int uwbd_event_handle_urc(struct uwb_event *evt)
{
	int result = -EINVAL;
	struct uwbd_evt_type_handler *type_table;
	uwbd_evt_handler_f handler;
	u8 type, context;
	u16 event;

	type = evt->notif.rceb->bEventType;
	event = le16_to_cpu(evt->notif.rceb->wEvent);
	context = evt->notif.rceb->bEventContext;

	if (type >= ARRAY_SIZE(uwbd_urc_evt_type_handlers))
		goto out;
	type_table = &uwbd_urc_evt_type_handlers[type];
	if (type_table->uwbd_events == NULL)
		goto out;
	if (event >= type_table->size)
		goto out;
	handler = type_table->uwbd_events[event].handler;
	if (handler == NULL)
		goto out;

	result = (*handler)(evt);
out:
	if (result < 0)
		dev_err(&evt->rc->uwb_dev.dev,
			"UWBD: event 0x%02x/%04x/%02x, handling failed: %d\n",
			type, event, context, result);
	return result;
}

static void uwbd_event_handle_message(struct uwb_event *evt)
{
	struct uwb_rc *rc;
	int result;

	rc = evt->rc;

	if (evt->message < 0 || evt->message >= ARRAY_SIZE(uwbd_message_handlers)) {
		dev_err(&rc->uwb_dev.dev, "UWBD: invalid message type %d\n", evt->message);
		return;
	}

	result = uwbd_message_handlers[evt->message].handler(evt);
	if (result < 0)
		dev_err(&rc->uwb_dev.dev, "UWBD: '%s' message failed: %d\n",
			uwbd_message_handlers[evt->message].name, result);
}

static void uwbd_event_handle(struct uwb_event *evt)
{
	struct uwb_rc *rc;
	int should_keep;

	rc = evt->rc;

	if (rc->ready) {
		switch (evt->type) {
		case UWB_EVT_TYPE_NOTIF:
			should_keep = uwbd_event_handle_urc(evt);
			if (should_keep <= 0)
				kfree(evt->notif.rceb);
			break;
		case UWB_EVT_TYPE_MSG:
			uwbd_event_handle_message(evt);
			break;
		default:
			dev_err(&rc->uwb_dev.dev, "UWBD: invalid event type %d\n", evt->type);
			break;
		}
	}

	__uwb_rc_put(rc);	/* for the __uwb_rc_get() in uwb_rc_notif_cb() */
}

/**
 * UWB Daemon
 *
 * Listens to all UWB notifications and takes care to track the state
 * of the UWB neighbourhood for the kernel. When we do a run, we
 * spinlock, move the list to a private copy and release the
 * lock. Hold it as little as possible. Not a conflict: it is
 * guaranteed we own the events in the private list.
 *
 * FIXME: should change so we don't have a 1HZ timer all the time, but
 *        only if there are devices.
 */
static int uwbd(void *param)
{
	struct uwb_rc *rc = param;
	unsigned long flags;
	struct uwb_event *evt;
	int should_stop = 0;

	while (1) {
		wait_event_interruptible_timeout(
			rc->uwbd.wq,
			!list_empty(&rc->uwbd.event_list)
			  || (should_stop = kthread_should_stop()),
			HZ);
		if (should_stop)
			break;
		try_to_freeze();

		spin_lock_irqsave(&rc->uwbd.event_list_lock, flags);
		if (!list_empty(&rc->uwbd.event_list)) {
			evt = list_first_entry(&rc->uwbd.event_list, struct uwb_event, list_node);
			list_del(&evt->list_node);
		} else
			evt = NULL;
		spin_unlock_irqrestore(&rc->uwbd.event_list_lock, flags);

		if (evt) {
			uwbd_event_handle(evt);
			kfree(evt);
		}

		uwb_beca_purge(rc);	/* Purge devices that left */
	}
	return 0;
}


/** Start the UWB daemon */
void uwbd_start(struct uwb_rc *rc)
{
	struct task_struct *task = kthread_run(uwbd, rc, "uwbd");
	if (IS_ERR(task)) {
		rc->uwbd.task = NULL;
		printk(KERN_ERR "UWB: Cannot start management daemon; "
		       "UWB won't work\n");
	} else {
		rc->uwbd.task = task;
		rc->uwbd.pid = rc->uwbd.task->pid;
	}
}

/* Stop the UWB daemon and free any unprocessed events */
void uwbd_stop(struct uwb_rc *rc)
{
	if (rc->uwbd.task)
		kthread_stop(rc->uwbd.task);
	uwbd_flush(rc);
}

/*
 * Queue an event for the management daemon
 *
 * When some lower layer receives an event, it uses this function to
 * push it forward to the UWB daemon.
 *
 * Once you pass the event, you don't own it any more, but the daemon
 * does. It will uwb_event_free() it when done, so make sure you
 * uwb_event_alloc()ed it or bad things will happen.
 *
 * If the daemon is not running, we just free the event.
 */
void uwbd_event_queue(struct uwb_event *evt)
{
	struct uwb_rc *rc = evt->rc;
	unsigned long flags;

	spin_lock_irqsave(&rc->uwbd.event_list_lock, flags);
	if (rc->uwbd.pid != 0) {
		list_add(&evt->list_node, &rc->uwbd.event_list);
		wake_up_all(&rc->uwbd.wq);
	} else {
		__uwb_rc_put(evt->rc);
		if (evt->type == UWB_EVT_TYPE_NOTIF)
			kfree(evt->notif.rceb);
		kfree(evt);
	}
	spin_unlock_irqrestore(&rc->uwbd.event_list_lock, flags);
	return;
}

void uwbd_flush(struct uwb_rc *rc)
{
	struct uwb_event *evt, *nxt;

	spin_lock_irq(&rc->uwbd.event_list_lock);
	list_for_each_entry_safe(evt, nxt, &rc->uwbd.event_list, list_node) {
		if (evt->rc == rc) {
			__uwb_rc_put(rc);
			list_del(&evt->list_node);
			if (evt->type == UWB_EVT_TYPE_NOTIF)
				kfree(evt->notif.rceb);
			kfree(evt);
		}
	}
	spin_unlock_irq(&rc->uwbd.event_list_lock);
}
