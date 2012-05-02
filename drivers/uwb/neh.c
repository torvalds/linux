/*
 * WUSB Wire Adapter: Radio Control Interface (WUSB[8])
 * Notification and Event Handling
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
 * The RC interface of the Host Wire Adapter (USB dongle) or WHCI PCI
 * card delivers a stream of notifications and events to the
 * notification end event endpoint or area. This code takes care of
 * getting a buffer with that data, breaking it up in separate
 * notifications and events and then deliver those.
 *
 * Events are answers to commands and they carry a context ID that
 * associates them to the command. Notifications are that,
 * notifications, they come out of the blue and have a context ID of
 * zero. Think of the context ID kind of like a handler. The
 * uwb_rc_neh_* code deals with managing context IDs.
 *
 * This is why you require a handle to operate on a UWB host. When you
 * open a handle a context ID is assigned to you.
 *
 * So, as it is done is:
 *
 * 1. Add an event handler [uwb_rc_neh_add()] (assigns a ctx id)
 * 2. Issue command [rc->cmd(rc, ...)]
 * 3. Arm the timeout timer [uwb_rc_neh_arm()]
 * 4, Release the reference to the neh [uwb_rc_neh_put()]
 * 5. Wait for the callback
 * 6. Command result (RCEB) is passed to the callback
 *
 * If (2) fails, you should remove the handle [uwb_rc_neh_rm()]
 * instead of arming the timer.
 *
 * Handles are for using in *serialized* code, single thread.
 *
 * When the notification/event comes, the IRQ handler/endpoint
 * callback passes the data read to uwb_rc_neh_grok() which will break
 * it up in a discrete series of events, look up who is listening for
 * them and execute the pertinent callbacks.
 *
 * If the reader detects an error while reading the data stream, call
 * uwb_rc_neh_error().
 *
 * CONSTRAINTS/ASSUMPTIONS:
 *
 * - Most notifications/events are small (less thank .5k), copying
 *   around is ok.
 *
 * - Notifications/events are ALWAYS smaller than PAGE_SIZE
 *
 * - Notifications/events always come in a single piece (ie: a buffer
 *   will always contain entire notifications/events).
 *
 * - we cannot know in advance how long each event is (because they
 *   lack a length field in their header--smart move by the standards
 *   body, btw). So we need a facility to get the event size given the
 *   header. This is what the EST code does (notif/Event Size
 *   Tables), check nest.c--as well, you can associate the size to
 *   the handle [w/ neh->extra_size()].
 *
 * - Most notifications/events are fixed size; only a few are variable
 *   size (NEST takes care of that).
 *
 * - Listeners of events expect them, so they usually provide a
 *   buffer, as they know the size. Listeners to notifications don't,
 *   so we allocate their buffers dynamically.
 */
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/export.h>

#include "uwb-internal.h"

/*
 * UWB Radio Controller Notification/Event Handle
 *
 * Represents an entity waiting for an event coming from the UWB Radio
 * Controller with a given context id (context) and type (evt_type and
 * evt). On reception of the notification/event, the callback (cb) is
 * called with the event.
 *
 * If the timer expires before the event is received, the callback is
 * called with -ETIMEDOUT as the event size.
 */
struct uwb_rc_neh {
	struct kref kref;

	struct uwb_rc *rc;
	u8 evt_type;
	__le16 evt;
	u8 context;
	u8 completed;
	uwb_rc_cmd_cb_f cb;
	void *arg;

	struct timer_list timer;
	struct list_head list_node;
};

static void uwb_rc_neh_timer(unsigned long arg);

static void uwb_rc_neh_release(struct kref *kref)
{
	struct uwb_rc_neh *neh = container_of(kref, struct uwb_rc_neh, kref);

	kfree(neh);
}

static void uwb_rc_neh_get(struct uwb_rc_neh *neh)
{
	kref_get(&neh->kref);
}

/**
 * uwb_rc_neh_put - release reference to a neh
 * @neh: the neh
 */
void uwb_rc_neh_put(struct uwb_rc_neh *neh)
{
	kref_put(&neh->kref, uwb_rc_neh_release);
}


/**
 * Assigns @neh a context id from @rc's pool
 *
 * @rc:	    UWB Radio Controller descriptor; @rc->neh_lock taken
 * @neh:    Notification/Event Handle
 * @returns 0 if context id was assigned ok; < 0 errno on error (if
 *	    all the context IDs are taken).
 *
 * (assumes @wa is locked).
 *
 * NOTE: WUSB spec reserves context ids 0x00 for notifications and
 *	 0xff is invalid, so they must not be used. Initialization
 *	 fills up those two in the bitmap so they are not allocated.
 *
 * We spread the allocation around to reduce the possibility of two
 * consecutive opened @neh's getting the same context ID assigned (to
 * avoid surprises with late events that timed out long time ago). So
 * first we search from where @rc->ctx_roll is, if not found, we
 * search from zero.
 */
static
int __uwb_rc_ctx_get(struct uwb_rc *rc, struct uwb_rc_neh *neh)
{
	int result;
	result = find_next_zero_bit(rc->ctx_bm, UWB_RC_CTX_MAX,
				    rc->ctx_roll++);
	if (result < UWB_RC_CTX_MAX)
		goto found;
	result = find_first_zero_bit(rc->ctx_bm, UWB_RC_CTX_MAX);
	if (result < UWB_RC_CTX_MAX)
		goto found;
	return -ENFILE;
found:
	set_bit(result, rc->ctx_bm);
	neh->context = result;
	return 0;
}


/** Releases @neh's context ID back to @rc (@rc->neh_lock is locked). */
static
void __uwb_rc_ctx_put(struct uwb_rc *rc, struct uwb_rc_neh *neh)
{
	struct device *dev = &rc->uwb_dev.dev;
	if (neh->context == 0)
		return;
	if (test_bit(neh->context, rc->ctx_bm) == 0) {
		dev_err(dev, "context %u not set in bitmap\n",
			neh->context);
		WARN_ON(1);
	}
	clear_bit(neh->context, rc->ctx_bm);
	neh->context = 0;
}

/**
 * uwb_rc_neh_add - add a neh for a radio controller command
 * @rc:             the radio controller
 * @cmd:            the radio controller command
 * @expected_type:  the type of the expected response event
 * @expected_event: the expected event ID
 * @cb:             callback for when the event is received
 * @arg:            argument for the callback
 *
 * Creates a neh and adds it to the list of those waiting for an
 * event.  A context ID will be assigned to the command.
 */
struct uwb_rc_neh *uwb_rc_neh_add(struct uwb_rc *rc, struct uwb_rccb *cmd,
				  u8 expected_type, u16 expected_event,
				  uwb_rc_cmd_cb_f cb, void *arg)
{
	int result;
	unsigned long flags;
	struct device *dev = &rc->uwb_dev.dev;
	struct uwb_rc_neh *neh;

	neh = kzalloc(sizeof(*neh), GFP_KERNEL);
	if (neh == NULL) {
		result = -ENOMEM;
		goto error_kzalloc;
	}

	kref_init(&neh->kref);
	INIT_LIST_HEAD(&neh->list_node);
	init_timer(&neh->timer);
	neh->timer.function = uwb_rc_neh_timer;
	neh->timer.data     = (unsigned long)neh;

	neh->rc = rc;
	neh->evt_type = expected_type;
	neh->evt = cpu_to_le16(expected_event);
	neh->cb = cb;
	neh->arg = arg;

	spin_lock_irqsave(&rc->neh_lock, flags);
	result = __uwb_rc_ctx_get(rc, neh);
	if (result >= 0) {
		cmd->bCommandContext = neh->context;
		list_add_tail(&neh->list_node, &rc->neh_list);
		uwb_rc_neh_get(neh);
	}
	spin_unlock_irqrestore(&rc->neh_lock, flags);
	if (result < 0)
		goto error_ctx_get;

	return neh;

error_ctx_get:
	kfree(neh);
error_kzalloc:
	dev_err(dev, "cannot open handle to radio controller: %d\n", result);
	return ERR_PTR(result);
}

static void __uwb_rc_neh_rm(struct uwb_rc *rc, struct uwb_rc_neh *neh)
{
	__uwb_rc_ctx_put(rc, neh);
	list_del(&neh->list_node);
}

/**
 * uwb_rc_neh_rm - remove a neh.
 * @rc:  the radio controller
 * @neh: the neh to remove
 *
 * Remove an active neh immediately instead of waiting for the event
 * (or a time out).
 */
void uwb_rc_neh_rm(struct uwb_rc *rc, struct uwb_rc_neh *neh)
{
	unsigned long flags;

	spin_lock_irqsave(&rc->neh_lock, flags);
	__uwb_rc_neh_rm(rc, neh);
	spin_unlock_irqrestore(&rc->neh_lock, flags);

	del_timer_sync(&neh->timer);
	uwb_rc_neh_put(neh);
}

/**
 * uwb_rc_neh_arm - arm an event handler timeout timer
 *
 * @rc:     UWB Radio Controller
 * @neh:    Notification/event handler for @rc
 *
 * The timer is only armed if the neh is active.
 */
void uwb_rc_neh_arm(struct uwb_rc *rc, struct uwb_rc_neh *neh)
{
	unsigned long flags;

	spin_lock_irqsave(&rc->neh_lock, flags);
	if (neh->context)
		mod_timer(&neh->timer,
			  jiffies + msecs_to_jiffies(UWB_RC_CMD_TIMEOUT_MS));
	spin_unlock_irqrestore(&rc->neh_lock, flags);
}

static void uwb_rc_neh_cb(struct uwb_rc_neh *neh, struct uwb_rceb *rceb, size_t size)
{
	(*neh->cb)(neh->rc, neh->arg, rceb, size);
	uwb_rc_neh_put(neh);
}

static bool uwb_rc_neh_match(struct uwb_rc_neh *neh, const struct uwb_rceb *rceb)
{
	return neh->evt_type == rceb->bEventType
		&& neh->evt == rceb->wEvent
		&& neh->context == rceb->bEventContext;
}

/**
 * Find the handle waiting for a RC Radio Control Event
 *
 * @rc:         UWB Radio Controller
 * @rceb:       Pointer to the RCEB buffer
 * @event_size: Pointer to the size of the RCEB buffer. Might be
 *              adjusted to take into account the @neh->extra_size
 *              settings.
 *
 * If the listener has no buffer (NULL buffer), one is allocated for
 * the right size (the amount of data received). @neh->ptr will point
 * to the event payload, which always starts with a 'struct
 * uwb_rceb'. kfree() it when done.
 */
static
struct uwb_rc_neh *uwb_rc_neh_lookup(struct uwb_rc *rc,
				     const struct uwb_rceb *rceb)
{
	struct uwb_rc_neh *neh = NULL, *h;
	unsigned long flags;

	spin_lock_irqsave(&rc->neh_lock, flags);

	list_for_each_entry(h, &rc->neh_list, list_node) {
		if (uwb_rc_neh_match(h, rceb)) {
			neh = h;
			break;
		}
	}

	if (neh)
		__uwb_rc_neh_rm(rc, neh);

	spin_unlock_irqrestore(&rc->neh_lock, flags);

	return neh;
}


/*
 * Process notifications coming from the radio control interface
 *
 * @rc:    UWB Radio Control Interface descriptor
 * @neh:   Notification/Event Handler @neh->ptr points to
 *         @uwb_evt->buffer.
 *
 * This function is called by the event/notif handling subsystem when
 * notifications arrive (hwarc_probe() arms a notification/event handle
 * that calls back this function for every received notification; this
 * function then will rearm itself).
 *
 * Notification data buffers are dynamically allocated by the NEH
 * handling code in neh.c [uwb_rc_neh_lookup()]. What is actually
 * allocated is space to contain the notification data.
 *
 * Buffers are prefixed with a Radio Control Event Block (RCEB) as
 * defined by the WUSB Wired-Adapter Radio Control interface. We
 * just use it for the notification code.
 *
 * On each case statement we just transcode endianess of the different
 * fields. We declare a pointer to a RCI definition of an event, and
 * then to a UWB definition of the same event (which are the same,
 * remember). Event if we use different pointers
 */
static
void uwb_rc_notif(struct uwb_rc *rc, struct uwb_rceb *rceb, ssize_t size)
{
	struct device *dev = &rc->uwb_dev.dev;
	struct uwb_event *uwb_evt;

	if (size == -ESHUTDOWN)
		return;
	if (size < 0) {
		dev_err(dev, "ignoring event with error code %zu\n",
			size);
		return;
	}

	uwb_evt = kzalloc(sizeof(*uwb_evt), GFP_ATOMIC);
	if (unlikely(uwb_evt == NULL)) {
		dev_err(dev, "no memory to queue event 0x%02x/%04x/%02x\n",
			rceb->bEventType, le16_to_cpu(rceb->wEvent),
			rceb->bEventContext);
		return;
	}
	uwb_evt->rc = __uwb_rc_get(rc);	/* will be put by uwbd's uwbd_event_handle() */
	uwb_evt->ts_jiffies = jiffies;
	uwb_evt->type = UWB_EVT_TYPE_NOTIF;
	uwb_evt->notif.size = size;
	uwb_evt->notif.rceb = rceb;

	uwbd_event_queue(uwb_evt);
}

static void uwb_rc_neh_grok_event(struct uwb_rc *rc, struct uwb_rceb *rceb, size_t size)
{
	struct device *dev = &rc->uwb_dev.dev;
	struct uwb_rc_neh *neh;
	struct uwb_rceb *notif;
	unsigned long flags;

	if (rceb->bEventContext == 0) {
		notif = kmalloc(size, GFP_ATOMIC);
		if (notif) {
			memcpy(notif, rceb, size);
			uwb_rc_notif(rc, notif, size);
		} else
			dev_err(dev, "event 0x%02x/%04x/%02x (%zu bytes): no memory\n",
				rceb->bEventType, le16_to_cpu(rceb->wEvent),
				rceb->bEventContext, size);
	} else {
		neh = uwb_rc_neh_lookup(rc, rceb);
		if (neh) {
			spin_lock_irqsave(&rc->neh_lock, flags);
			/* to guard against a timeout */
			neh->completed = 1;
			del_timer(&neh->timer);
			spin_unlock_irqrestore(&rc->neh_lock, flags);
			uwb_rc_neh_cb(neh, rceb, size);
		} else
			dev_warn(dev, "event 0x%02x/%04x/%02x (%zu bytes): nobody cared\n",
				 rceb->bEventType, le16_to_cpu(rceb->wEvent),
				 rceb->bEventContext, size);
	}
}

/**
 * Given a buffer with one or more UWB RC events/notifications, break
 * them up and dispatch them.
 *
 * @rc:	      UWB Radio Controller
 * @buf:      Buffer with the stream of notifications/events
 * @buf_size: Amount of data in the buffer
 *
 * Note each notification/event starts always with a 'struct
 * uwb_rceb', so the minimum size if 4 bytes.
 *
 * The device may pass us events formatted differently than expected.
 * These are first filtered, potentially creating a new event in a new
 * memory location. If a new event is created by the filter it is also
 * freed here.
 *
 * For each notif/event, tries to guess the size looking at the EST
 * tables, then looks for a neh that is waiting for that event and if
 * found, copies the payload to the neh's buffer and calls it back. If
 * not, the data is ignored.
 *
 * Note that if we can't find a size description in the EST tables, we
 * still might find a size in the 'neh' handle in uwb_rc_neh_lookup().
 *
 * Assumptions:
 *
 *   @rc->neh_lock is NOT taken
 *
 * We keep track of various sizes here:
 * size:      contains the size of the buffer that is processed for the
 *            incoming event. this buffer may contain events that are not
 *            formatted as WHCI.
 * real_size: the actual space taken by this event in the buffer.
 *            We need to keep track of the real size of an event to be able to
 *            advance the buffer correctly.
 * event_size: the size of the event as expected by the core layer
 *            [OR] the size of the event after filtering. if the filtering
 *            created a new event in a new memory location then this is
 *            effectively the size of a new event buffer
 */
void uwb_rc_neh_grok(struct uwb_rc *rc, void *buf, size_t buf_size)
{
	struct device *dev = &rc->uwb_dev.dev;
	void *itr;
	struct uwb_rceb *rceb;
	size_t size, real_size, event_size;
	int needtofree;

	itr = buf;
	size = buf_size;
	while (size > 0) {
		if (size < sizeof(*rceb)) {
			dev_err(dev, "not enough data in event buffer to "
				"process incoming events (%zu left, minimum is "
				"%zu)\n", size, sizeof(*rceb));
			break;
		}

		rceb = itr;
		if (rc->filter_event) {
			needtofree = rc->filter_event(rc, &rceb, size,
						      &real_size, &event_size);
			if (needtofree < 0 && needtofree != -ENOANO) {
				dev_err(dev, "BUG: Unable to filter event "
					"(0x%02x/%04x/%02x) from "
					"device. \n", rceb->bEventType,
					le16_to_cpu(rceb->wEvent),
					rceb->bEventContext);
				break;
			}
		} else
			needtofree = -ENOANO;
		/* do real processing if there was no filtering or the
		 * filtering didn't act */
		if (needtofree == -ENOANO) {
			ssize_t ret = uwb_est_find_size(rc, rceb, size);
			if (ret < 0)
				break;
			if (ret > size) {
				dev_err(dev, "BUG: hw sent incomplete event "
					"0x%02x/%04x/%02x (%zd bytes), only got "
					"%zu bytes. We don't handle that.\n",
					rceb->bEventType, le16_to_cpu(rceb->wEvent),
					rceb->bEventContext, ret, size);
				break;
			}
			real_size = event_size = ret;
		}
		uwb_rc_neh_grok_event(rc, rceb, event_size);

		if (needtofree == 1)
			kfree(rceb);

		itr += real_size;
		size -= real_size;
	}
}
EXPORT_SYMBOL_GPL(uwb_rc_neh_grok);


/**
 * The entity that reads from the device notification/event channel has
 * detected an error.
 *
 * @rc:    UWB Radio Controller
 * @error: Errno error code
 *
 */
void uwb_rc_neh_error(struct uwb_rc *rc, int error)
{
	struct uwb_rc_neh *neh;
	unsigned long flags;

	for (;;) {
		spin_lock_irqsave(&rc->neh_lock, flags);
		if (list_empty(&rc->neh_list)) {
			spin_unlock_irqrestore(&rc->neh_lock, flags);
			break;
		}
		neh = list_first_entry(&rc->neh_list, struct uwb_rc_neh, list_node);
		__uwb_rc_neh_rm(rc, neh);
		spin_unlock_irqrestore(&rc->neh_lock, flags);

		del_timer_sync(&neh->timer);
		uwb_rc_neh_cb(neh, NULL, error);
	}
}
EXPORT_SYMBOL_GPL(uwb_rc_neh_error);


static void uwb_rc_neh_timer(unsigned long arg)
{
	struct uwb_rc_neh *neh = (struct uwb_rc_neh *)arg;
	struct uwb_rc *rc = neh->rc;
	unsigned long flags;

	spin_lock_irqsave(&rc->neh_lock, flags);
	if (neh->completed) {
		spin_unlock_irqrestore(&rc->neh_lock, flags);
		return;
	}
	if (neh->context)
		__uwb_rc_neh_rm(rc, neh);
	else
		neh = NULL;
	spin_unlock_irqrestore(&rc->neh_lock, flags);

	if (neh)
		uwb_rc_neh_cb(neh, NULL, -ETIMEDOUT);
}

/** Initializes the @rc's neh subsystem
 */
void uwb_rc_neh_create(struct uwb_rc *rc)
{
	spin_lock_init(&rc->neh_lock);
	INIT_LIST_HEAD(&rc->neh_list);
	set_bit(0, rc->ctx_bm);		/* 0 is reserved (see [WUSB] table 8-65) */
	set_bit(0xff, rc->ctx_bm);	/* and 0xff is invalid */
	rc->ctx_roll = 1;
}


/** Release's the @rc's neh subsystem */
void uwb_rc_neh_destroy(struct uwb_rc *rc)
{
	unsigned long flags;
	struct uwb_rc_neh *neh;

	for (;;) {
		spin_lock_irqsave(&rc->neh_lock, flags);
		if (list_empty(&rc->neh_list)) {
			spin_unlock_irqrestore(&rc->neh_lock, flags);
			break;
		}
		neh = list_first_entry(&rc->neh_list, struct uwb_rc_neh, list_node);
		__uwb_rc_neh_rm(rc, neh);
		spin_unlock_irqrestore(&rc->neh_lock, flags);

		del_timer_sync(&neh->timer);
		uwb_rc_neh_put(neh);
	}
}
