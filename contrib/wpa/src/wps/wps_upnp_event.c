/*
 * UPnP WPS Device - Event processing
 * Copyright (c) 2000-2003 Intel Corporation
 * Copyright (c) 2006-2007 Sony Corporation
 * Copyright (c) 2008-2009 Atheros Communications
 * Copyright (c) 2009-2010, Jouni Malinen <j@w1.fi>
 *
 * See wps_upnp.c for more details on licensing and code history.
 */

#include "includes.h"
#include <assert.h>

#include "common.h"
#include "eloop.h"
#include "uuid.h"
#include "http_client.h"
#include "wps_defs.h"
#include "wps_upnp.h"
#include "wps_upnp_i.h"

/*
 * Event message generation (to subscribers)
 *
 * We make a separate copy for each message for each subscriber. This memory
 * wasted could be limited (adding code complexity) by sharing copies, keeping
 * a usage count and freeing when zero.
 *
 * Sending a message requires using a HTTP over TCP NOTIFY
 * (like a PUT) which requires a number of states..
 */

#define MAX_EVENTS_QUEUED 20   /* How far behind queued events */
#define MAX_FAILURES 10 /* Drop subscription after this many failures */

/* How long to wait before sending event */
#define EVENT_DELAY_SECONDS 0
#define EVENT_DELAY_MSEC 0

/*
 * Event information that we send to each subscriber is remembered in this
 * struct. The event cannot be sent by simple UDP; it has to be sent by a HTTP
 * over TCP transaction which requires various states.. It may also need to be
 * retried at a different address (if more than one is available).
 *
 * TODO: As an optimization we could share data between subscribers.
 */
struct wps_event_ {
	struct dl_list list;
	struct subscription *s;         /* parent */
	unsigned subscriber_sequence;   /* which event for this subscription*/
	unsigned int retry;             /* which retry */
	struct subscr_addr *addr;       /* address to connect to */
	struct wpabuf *data;            /* event data to send */
	struct http_client *http_event;
};


/* event_clean -- clean sockets etc. of event
 * Leaves data, retry count etc. alone.
 */
static void event_clean(struct wps_event_ *e)
{
	if (e->s->current_event == e)
		e->s->current_event = NULL;
	http_client_free(e->http_event);
	e->http_event = NULL;
}


/* event_delete -- delete single unqueued event
 * (be sure to dequeue first if need be)
 */
static void event_delete(struct wps_event_ *e)
{
	wpa_printf(MSG_DEBUG, "WPS UPnP: Delete event %p", e);
	event_clean(e);
	wpabuf_free(e->data);
	os_free(e);
}


/* event_dequeue -- get next event from the queue
 * Returns NULL if empty.
 */
static struct wps_event_ *event_dequeue(struct subscription *s)
{
	struct wps_event_ *e;
	e = dl_list_first(&s->event_queue, struct wps_event_, list);
	if (e) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: Dequeue event %p for "
			   "subscription %p", e, s);
		dl_list_del(&e->list);
	}
	return e;
}


/* event_delete_all -- delete entire event queue and current event */
void event_delete_all(struct subscription *s)
{
	struct wps_event_ *e;
	while ((e = event_dequeue(s)) != NULL)
		event_delete(e);
	if (s->current_event) {
		event_delete(s->current_event);
		/* will set: s->current_event = NULL;  */
	}
}


/**
 * event_retry - Called when we had a failure delivering event msg
 * @e: Event
 * @do_next_address: skip address e.g. on connect fail
 */
static void event_retry(struct wps_event_ *e, int do_next_address)
{
	struct subscription *s = e->s;
	struct upnp_wps_device_sm *sm = s->sm;

	wpa_printf(MSG_DEBUG, "WPS UPnP: Retry event %p for subscription %p",
		   e, s);
	event_clean(e);
	/* will set: s->current_event = NULL; */

	if (do_next_address) {
		e->retry++;
		wpa_printf(MSG_DEBUG, "WPS UPnP: Try address %d", e->retry);
	}
	if (e->retry >= dl_list_len(&s->addr_list)) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: Giving up on sending event "
			   "for %s", e->addr->domain_and_port);
		event_delete(e);
		s->last_event_failed = 1;
		if (!dl_list_empty(&s->event_queue))
			event_send_all_later(s->sm);
		return;
	}
	dl_list_add(&s->event_queue, &e->list);
	event_send_all_later(sm);
}


static struct wpabuf * event_build_message(struct wps_event_ *e)
{
	struct wpabuf *buf;
	char *b;

	buf = wpabuf_alloc(1000 + wpabuf_len(e->data));
	if (buf == NULL)
		return NULL;
	wpabuf_printf(buf, "NOTIFY %s HTTP/1.1\r\n", e->addr->path);
	wpabuf_put_str(buf, "SERVER: Unspecified, UPnP/1.0, Unspecified\r\n");
	wpabuf_printf(buf, "HOST: %s\r\n", e->addr->domain_and_port);
	wpabuf_put_str(buf, "CONTENT-TYPE: text/xml; charset=\"utf-8\"\r\n"
		       "NT: upnp:event\r\n"
		       "NTS: upnp:propchange\r\n");
	wpabuf_put_str(buf, "SID: uuid:");
	b = wpabuf_put(buf, 0);
	uuid_bin2str(e->s->uuid, b, 80);
	wpabuf_put(buf, os_strlen(b));
	wpabuf_put_str(buf, "\r\n");
	wpabuf_printf(buf, "SEQ: %u\r\n", e->subscriber_sequence);
	wpabuf_printf(buf, "CONTENT-LENGTH: %d\r\n",
		      (int) wpabuf_len(e->data));
	wpabuf_put_str(buf, "\r\n"); /* terminating empty line */
	wpabuf_put_buf(buf, e->data);
	return buf;
}


static void event_addr_failure(struct wps_event_ *e)
{
	struct subscription *s = e->s;

	e->addr->num_failures++;
	wpa_printf(MSG_DEBUG, "WPS UPnP: Failed to send event %p to %s "
		   "(num_failures=%u)",
		   e, e->addr->domain_and_port, e->addr->num_failures);

	if (e->addr->num_failures < MAX_FAILURES) {
		/* Try other addresses, if available */
		event_retry(e, 1);
		return;
	}

	/*
	 * If other side doesn't like what we say, forget about them.
	 * (There is no way to tell other side that we are dropping them...).
	 */
	wpa_printf(MSG_DEBUG, "WPS UPnP: Deleting subscription %p "
		   "address %s due to errors", s, e->addr->domain_and_port);
	dl_list_del(&e->addr->list);
	subscr_addr_delete(e->addr);
	e->addr = NULL;

	if (dl_list_empty(&s->addr_list)) {
		/* if we've given up on all addresses */
		wpa_printf(MSG_DEBUG, "WPS UPnP: Removing subscription %p "
			   "with no addresses", s);
		dl_list_del(&s->list);
		subscription_destroy(s);
		return;
	}

	/* Try other addresses, if available */
	event_retry(e, 0);
}


static void event_http_cb(void *ctx, struct http_client *c,
			  enum http_client_event event)
{
	struct wps_event_ *e = ctx;
	struct subscription *s = e->s;

	wpa_printf(MSG_DEBUG, "WPS UPnP: HTTP client callback: e=%p c=%p "
		   "event=%d", e, c, event);
	switch (event) {
	case HTTP_CLIENT_OK:
		wpa_printf(MSG_DEBUG,
			   "WPS UPnP: Got event %p reply OK from %s",
			   e, e->addr->domain_and_port);
		e->addr->num_failures = 0;
		s->last_event_failed = 0;
		event_delete(e);

		/* Schedule sending more if there is more to send */
		if (!dl_list_empty(&s->event_queue))
			event_send_all_later(s->sm);
		break;
	case HTTP_CLIENT_FAILED:
		wpa_printf(MSG_DEBUG, "WPS UPnP: Event send failure");
		event_addr_failure(e);
		break;
	case HTTP_CLIENT_INVALID_REPLY:
		wpa_printf(MSG_DEBUG, "WPS UPnP: Invalid reply");
		event_addr_failure(e);
		break;
	case HTTP_CLIENT_TIMEOUT:
		wpa_printf(MSG_DEBUG, "WPS UPnP: Event send timeout");
		event_addr_failure(e);
		break;
	}
}


/* event_send_start -- prepare to send a event message to subscriber
 *
 * This gets complicated because:
 * -- The message is sent via TCP and we have to keep the stream open
 *      for 30 seconds to get a response... then close it.
 * -- But we might have other event happen in the meantime...
 *      we have to queue them, if we lose them then the subscriber will
 *      be forced to unsubscribe and subscribe again.
 * -- If multiple URLs are provided then we are supposed to try successive
 *      ones after 30 second timeout.
 * -- The URLs might use domain names instead of dotted decimal addresses,
 *      and resolution of those may cause unwanted sleeping.
 * -- Doing the initial TCP connect can take a while, so we have to come
 *      back after connection and then send the data.
 *
 * Returns nonzero on error;
 *
 * Prerequisite: No current event send (s->current_event == NULL)
 *      and non-empty queue.
 */
static int event_send_start(struct subscription *s)
{
	struct wps_event_ *e;
	unsigned int itry;
	struct wpabuf *buf;

	/*
	 * Assume we are called ONLY with no current event and ONLY with
	 * nonempty event queue and ONLY with at least one address to send to.
	 */
	if (dl_list_empty(&s->addr_list) ||
	    s->current_event ||
	    dl_list_empty(&s->event_queue))
		return -1;

	s->current_event = e = event_dequeue(s);

	/* Use address according to number of retries */
	itry = 0;
	dl_list_for_each(e->addr, &s->addr_list, struct subscr_addr, list)
		if (itry++ == e->retry)
			break;
	if (itry < e->retry)
		return -1;

	buf = event_build_message(e);
	if (buf == NULL) {
		event_retry(e, 0);
		return -1;
	}

	e->http_event = http_client_addr(&e->addr->saddr, buf, 0,
					 event_http_cb, e);
	if (e->http_event == NULL) {
		wpabuf_free(buf);
		event_retry(e, 0);
		return -1;
	}

	return 0;
}


/* event_send_all_later_handler -- actually send events as needed */
static void event_send_all_later_handler(void *eloop_data, void *user_ctx)
{
	struct upnp_wps_device_sm *sm = user_ctx;
	struct subscription *s, *tmp;
	int nerrors = 0;

	sm->event_send_all_queued = 0;
	dl_list_for_each_safe(s, tmp, &sm->subscriptions, struct subscription,
			      list) {
		if (s->current_event == NULL /* not busy */ &&
		    !dl_list_empty(&s->event_queue) /* more to do */) {
			if (event_send_start(s))
				nerrors++;
		}
	}

	if (nerrors) {
		/* Try again later */
		event_send_all_later(sm);
	}
}


/* event_send_all_later -- schedule sending events to all subscribers
 * that need it.
 * This avoids two problems:
 * -- After getting a subscription, we should not send the first event
 *      until after our reply is fully queued to be sent back,
 * -- Possible stack depth or infinite recursion issues.
 */
void event_send_all_later(struct upnp_wps_device_sm *sm)
{
	/*
	 * The exact time in the future isn't too important. Waiting a bit
	 * might let us do several together.
	 */
	if (sm->event_send_all_queued)
		return;
	sm->event_send_all_queued = 1;
	eloop_register_timeout(EVENT_DELAY_SECONDS, EVENT_DELAY_MSEC,
			       event_send_all_later_handler, NULL, sm);
}


/* event_send_stop_all -- cleanup */
void event_send_stop_all(struct upnp_wps_device_sm *sm)
{
	if (sm->event_send_all_queued)
		eloop_cancel_timeout(event_send_all_later_handler, NULL, sm);
	sm->event_send_all_queued = 0;
}


/**
 * event_add - Add a new event to a queue
 * @s: Subscription
 * @data: Event data (is copied; caller retains ownership)
 * @probereq: Whether this is a Probe Request event
 * Returns: 0 on success, -1 on error, 1 on max event queue limit reached
 */
int event_add(struct subscription *s, const struct wpabuf *data, int probereq)
{
	struct wps_event_ *e;
	unsigned int len;

	len = dl_list_len(&s->event_queue);
	if (len >= MAX_EVENTS_QUEUED) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: Too many events queued for "
			   "subscriber %p", s);
		if (probereq)
			return 1;

		/* Drop oldest entry to allow EAP event to be stored. */
		e = event_dequeue(s);
		if (!e)
			return 1;
		event_delete(e);
	}

	if (s->last_event_failed && probereq && len > 0) {
		/*
		 * Avoid queuing frames for subscribers that may have left
		 * without unsubscribing.
		 */
		wpa_printf(MSG_DEBUG, "WPS UPnP: Do not queue more Probe "
			   "Request frames for subscription %p since last "
			   "delivery failed", s);
		return -1;
	}

	e = os_zalloc(sizeof(*e));
	if (e == NULL)
		return -1;
	dl_list_init(&e->list);
	e->s = s;
	e->data = wpabuf_dup(data);
	if (e->data == NULL) {
		os_free(e);
		return -1;
	}
	e->subscriber_sequence = s->next_subscriber_sequence++;
	if (s->next_subscriber_sequence == 0)
		s->next_subscriber_sequence++;
	wpa_printf(MSG_DEBUG, "WPS UPnP: Queue event %p for subscriber %p "
		   "(queue len %u)", e, s, len + 1);
	dl_list_add_tail(&s->event_queue, &e->list);
	event_send_all_later(s->sm);
	return 0;
}
