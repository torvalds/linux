/*
 * UPnP WPS Device
 * Copyright (c) 2000-2003 Intel Corporation
 * Copyright (c) 2006-2007 Sony Corporation
 * Copyright (c) 2008-2009 Atheros Communications
 * Copyright (c) 2009-2010, Jouni Malinen <j@w1.fi>
 *
 * See below for more details on licensing and code history.
 */

/*
 * This has been greatly stripped down from the original file
 * (upnp_wps_device.c) by Ted Merrill, Atheros Communications
 * in order to eliminate use of the bulky libupnp library etc.
 *
 * History:
 * upnp_wps_device.c is/was a shim layer between wps_opt_upnp.c and
 * the libupnp library.
 * The layering (by Sony) was well done; only a very minor modification
 * to API of upnp_wps_device.c was required.
 * libupnp was found to be undesirable because:
 * -- It consumed too much code and data space
 * -- It uses multiple threads, making debugging more difficult
 *      and possibly reducing reliability.
 * -- It uses static variables and only supports one instance.
 * The shim and libupnp are here replaced by special code written
 * specifically for the needs of hostapd.
 * Various shortcuts can and are taken to keep the code size small.
 * Generally, execution time is not as crucial.
 *
 * BUGS:
 * -- UPnP requires that we be able to resolve domain names.
 * While uncommon, if we have to do it then it will stall the entire
 * hostapd program, which is bad.
 * This is because we use the standard linux getaddrinfo() function
 * which is syncronous.
 * An asyncronous solution would be to use the free "ares" library.
 * -- Does not have a robust output buffering scheme.  Uses a single
 * fixed size output buffer per TCP/HTTP connection, with possible (although
 * unlikely) possibility of overflow and likely excessive use of RAM.
 * A better solution would be to write the HTTP output as a buffered stream,
 * using chunking: (handle header specially, then) generate data with
 * a printf-like function into a buffer, catching buffer full condition,
 * then send it out surrounded by http chunking.
 * -- There is some code that could be separated out into the common
 * library to be shared with wpa_supplicant.
 * -- Needs renaming with module prefix to avoid polluting the debugger
 * namespace and causing possible collisions with other static fncs
 * and structure declarations when using the debugger.
 * -- The http error code generation is pretty bogus, hopefully noone cares.
 *
 * Author: Ted Merrill, Atheros Communications, based upon earlier work
 * as explained above and below.
 *
 * Copyright:
 * Copyright 2008 Atheros Communications.
 *
 * The original header (of upnp_wps_device.c) reads:
 *
 *  Copyright (c) 2006-2007 Sony Corporation. All Rights Reserved.
 *
 *  File Name: upnp_wps_device.c
 *  Description: EAP-WPS UPnP device source
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Sony Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Portions from Intel libupnp files, e.g. genlib/net/http/httpreadwrite.c
 * typical header:
 *
 * Copyright (c) 2000-2003 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither name of Intel Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
 * Overview of WPS over UPnP:
 *
 * UPnP is a protocol that allows devices to discover each other and control
 * each other. In UPnP terminology, a device is either a "device" (a server
 * that provides information about itself and allows itself to be controlled)
 * or a "control point" (a client that controls "devices") or possibly both.
 * This file implements a UPnP "device".
 *
 * For us, we use mostly basic UPnP discovery, but the control part of interest
 * is WPS carried via UPnP messages. There is quite a bit of basic UPnP
 * discovery to do before we can get to WPS, however.
 *
 * UPnP discovery begins with "devices" send out multicast UDP packets to a
 * certain fixed multicast IP address and port, and "control points" sending
 * out other such UDP packets.
 *
 * The packets sent by devices are NOTIFY packets (not to be confused with TCP
 * NOTIFY packets that are used later) and those sent by control points are
 * M-SEARCH packets. These packets contain a simple HTTP style header. The
 * packets are sent redundantly to get around packet loss. Devices respond to
 * M-SEARCH packets with HTTP-like UDP packets containing HTTP/1.1 200 OK
 * messages, which give similar information as the UDP NOTIFY packets.
 *
 * The above UDP packets advertise the (arbitrary) TCP ports that the
 * respective parties will listen to. The control point can then do a HTTP
 * SUBSCRIBE (something like an HTTP PUT) after which the device can do a
 * separate HTTP NOTIFY (also like an HTTP PUT) to do event messaging.
 *
 * The control point will also do HTTP GET of the "device file" listed in the
 * original UDP information from the device (see UPNP_WPS_DEVICE_XML_FILE
 * data), and based on this will do additional GETs... HTTP POSTs are done to
 * cause an action.
 *
 * Beyond some basic information in HTTP headers, additional information is in
 * the HTTP bodies, in a format set by the SOAP and XML standards, a markup
 * language related to HTML used for web pages. This language is intended to
 * provide the ultimate in self-documentation by providing a universal
 * namespace based on pseudo-URLs called URIs. Note that although a URI looks
 * like a URL (a web address), they are never accessed as such but are used
 * only as identifiers.
 *
 * The POST of a GetDeviceInfo gets information similar to what might be
 * obtained from a probe request or response on Wi-Fi. WPS messages M1-M8
 * are passed via a POST of a PutMessage; the M1-M8 WPS messages are converted
 * to a bin64 ascii representation for encapsulation. When proxying messages,
 * WLANEvent and PutWLANResponse are used.
 *
 * This of course glosses over a lot of details.
 */

#include "includes.h"

#include <assert.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/ioctl.h>

#include "common.h"
#include "uuid.h"
#include "base64.h"
#include "wps.h"
#include "wps_i.h"
#include "wps_upnp.h"
#include "wps_upnp_i.h"


/*
 * UPnP allows a client ("control point") to send a server like us ("device")
 * a domain name for registration, and we are supposed to resolve it. This is
 * bad because, using the standard Linux library, we will stall the entire
 * hostapd waiting for resolution.
 *
 * The "correct" solution would be to use an event driven library for domain
 * name resolution such as "ares". However, this would increase code size
 * further. Since it is unlikely that we'll actually see such domain names, we
 * can just refuse to accept them.
 */
#define NO_DOMAIN_NAME_RESOLUTION 1  /* 1 to allow only dotted ip addresses */


/*
 * UPnP does not scale well. If we were in a room with thousands of control
 * points then potentially we could be expected to handle subscriptions for
 * each of them, which would exhaust our memory. So we must set a limit. In
 * practice we are unlikely to see more than one or two.
 */
#define MAX_SUBSCRIPTIONS 4    /* how many subscribing clients we handle */
#define MAX_ADDR_PER_SUBSCRIPTION 8

/* Maximum number of Probe Request events per second */
#define MAX_EVENTS_PER_SEC 5


static struct upnp_wps_device_sm *shared_upnp_device = NULL;


/* Write the current date/time per RFC */
void format_date(struct wpabuf *buf)
{
	const char *weekday_str = "Sun\0Mon\0Tue\0Wed\0Thu\0Fri\0Sat";
	const char *month_str = "Jan\0Feb\0Mar\0Apr\0May\0Jun\0"
		"Jul\0Aug\0Sep\0Oct\0Nov\0Dec";
	struct tm *date;
	time_t t;

	t = time(NULL);
	date = gmtime(&t);
	wpabuf_printf(buf, "%s, %02d %s %d %02d:%02d:%02d GMT",
		      &weekday_str[date->tm_wday * 4], date->tm_mday,
		      &month_str[date->tm_mon * 4], date->tm_year + 1900,
		      date->tm_hour, date->tm_min, date->tm_sec);
}


/***************************************************************************
 * UUIDs (unique identifiers)
 *
 * These are supposed to be unique in all the world.
 * Sometimes permanent ones are used, sometimes temporary ones
 * based on random numbers... there are different rules for valid content
 * of different types.
 * Each uuid is 16 bytes long.
 **************************************************************************/

/* uuid_make -- construct a random UUID
 * The UPnP documents don't seem to offer any guidelines as to which method to
 * use for constructing UUIDs for subscriptions. Presumably any method from
 * rfc4122 is good enough; I've chosen random number method.
 */
static void uuid_make(u8 uuid[UUID_LEN])
{
	os_get_random(uuid, UUID_LEN);

	/* Replace certain bits as specified in rfc4122 or X.667 */
	uuid[6] &= 0x0f; uuid[6] |= (4 << 4);   /* version 4 == random gen */
	uuid[8] &= 0x3f; uuid[8] |= 0x80;
}


/*
 * Subscriber address handling.
 * Since a subscriber may have an arbitrary number of addresses, we have to
 * add a bunch of code to handle them.
 *
 * Addresses are passed in text, and MAY be domain names instead of the (usual
 * and expected) dotted IP addresses. Resolving domain names consumes a lot of
 * resources. Worse, we are currently using the standard Linux getaddrinfo()
 * which will block the entire program until complete or timeout! The proper
 * solution would be to use the "ares" library or similar with more state
 * machine steps etc. or just disable domain name resolution by setting
 * NO_DOMAIN_NAME_RESOLUTION to 1 at top of this file.
 */

/* subscr_addr_delete -- delete single unlinked subscriber address
 * (be sure to unlink first if need be)
 */
void subscr_addr_delete(struct subscr_addr *a)
{
	/*
	 * Note: do NOT free domain_and_port or path because they point to
	 * memory within the allocation of "a".
	 */
	os_free(a);
}


/* subscr_addr_free_all -- unlink and delete list of subscriber addresses. */
static void subscr_addr_free_all(struct subscription *s)
{
	struct subscr_addr *a, *tmp;
	dl_list_for_each_safe(a, tmp, &s->addr_list, struct subscr_addr, list)
	{
		dl_list_del(&a->list);
		subscr_addr_delete(a);
	}
}


/* subscr_addr_add_url -- add address(es) for one url to subscription */
static void subscr_addr_add_url(struct subscription *s, const char *url,
				size_t url_len)
{
	int alloc_len;
	char *scratch_mem = NULL;
	char *mem;
	char *domain_and_port;
	char *delim;
	char *path;
	char *domain;
	int port = 80;  /* port to send to (default is port 80) */
	struct addrinfo hints;
	struct addrinfo *result = NULL;
	struct addrinfo *rp;
	int rerr;

	/* url MUST begin with http: */
	if (url_len < 7 || os_strncasecmp(url, "http://", 7))
		goto fail;
	url += 7;
	url_len -= 7;

	/* allocate memory for the extra stuff we need */
	alloc_len = 2 * (url_len + 1);
	scratch_mem = os_zalloc(alloc_len);
	if (scratch_mem == NULL)
		goto fail;
	mem = scratch_mem;
	os_strncpy(mem, url, url_len);
	wpa_printf(MSG_DEBUG, "WPS UPnP: Adding URL '%s'", mem);
	domain_and_port = mem;
	mem += 1 + os_strlen(mem);
	delim = os_strchr(domain_and_port, '/');
	if (delim) {
		*delim++ = 0;   /* null terminate domain and port */
		path = delim;
	} else {
		path = domain_and_port + os_strlen(domain_and_port);
	}
	domain = mem;
	strcpy(domain, domain_and_port);
	delim = os_strchr(domain, ':');
	if (delim) {
		*delim++ = 0;   /* null terminate domain */
		if (isdigit(*delim))
			port = atol(delim);
	}

	/*
	 * getaddrinfo does the right thing with dotted decimal notations, or
	 * will resolve domain names. Resolving domain names will unfortunately
	 * hang the entire program until it is resolved or it times out
	 * internal to getaddrinfo; fortunately we think that the use of actual
	 * domain names (vs. dotted decimal notations) should be uncommon.
	 */
	os_memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;      /* IPv4 */
	hints.ai_socktype = SOCK_STREAM;
#if NO_DOMAIN_NAME_RESOLUTION
	/* Suppress domain name resolutions that would halt
	 * the program for periods of time
	 */
	hints.ai_flags = AI_NUMERICHOST;
#else
	/* Allow domain name resolution. */
	hints.ai_flags = 0;
#endif
	hints.ai_protocol = 0;          /* Any protocol? */
	rerr = getaddrinfo(domain, NULL /* fill in port ourselves */,
			   &hints, &result);
	if (rerr) {
		wpa_printf(MSG_INFO, "WPS UPnP: Resolve error %d (%s) on: %s",
			   rerr, gai_strerror(rerr), domain);
		goto fail;
	}
	for (rp = result; rp; rp = rp->ai_next) {
		struct subscr_addr *a;

		/* Limit no. of address to avoid denial of service attack */
		if (dl_list_len(&s->addr_list) >= MAX_ADDR_PER_SUBSCRIPTION) {
			wpa_printf(MSG_INFO, "WPS UPnP: subscr_addr_add_url: "
				   "Ignoring excessive addresses");
			break;
		}

		a = os_zalloc(sizeof(*a) + alloc_len);
		if (a == NULL)
			continue;
		mem = (void *) (a + 1);
		a->domain_and_port = mem;
		strcpy(mem, domain_and_port);
		mem += 1 + strlen(mem);
		a->path = mem;
		if (path[0] != '/')
			*mem++ = '/';
		strcpy(mem, path);
		mem += 1 + os_strlen(mem);
		os_memcpy(&a->saddr, rp->ai_addr, sizeof(a->saddr));
		a->saddr.sin_port = htons(port);

		dl_list_add(&s->addr_list, &a->list);
	}

fail:
	if (result)
		freeaddrinfo(result);
	os_free(scratch_mem);
}


/* subscr_addr_list_create -- create list from urls in string.
 *      Each url is enclosed by angle brackets.
 */
static void subscr_addr_list_create(struct subscription *s,
				    const char *url_list)
{
	const char *end;
	wpa_printf(MSG_DEBUG, "WPS UPnP: Parsing URL list '%s'", url_list);
	for (;;) {
		while (*url_list == ' ' || *url_list == '\t')
			url_list++;
		if (*url_list != '<')
			break;
		url_list++;
		end = os_strchr(url_list, '>');
		if (end == NULL)
			break;
		subscr_addr_add_url(s, url_list, end - url_list);
		url_list = end + 1;
	}
}


int send_wpabuf(int fd, struct wpabuf *buf)
{
	wpa_printf(MSG_DEBUG, "WPS UPnP: Send %lu byte message",
		   (unsigned long) wpabuf_len(buf));
	errno = 0;
	if (write(fd, wpabuf_head(buf), wpabuf_len(buf)) !=
	    (int) wpabuf_len(buf)) {
		wpa_printf(MSG_ERROR, "WPS UPnP: Failed to send buffer: "
			   "errno=%d (%s)",
			   errno, strerror(errno));
		return -1;
	}

	return 0;
}


static void wpabuf_put_property(struct wpabuf *buf, const char *name,
				const char *value)
{
	wpabuf_put_str(buf, "<e:property>");
	wpabuf_printf(buf, "<%s>", name);
	if (value)
		wpabuf_put_str(buf, value);
	wpabuf_printf(buf, "</%s>", name);
	wpabuf_put_str(buf, "</e:property>\n");
}


/**
 * upnp_wps_device_send_event - Queue event messages for subscribers
 * @sm: WPS UPnP state machine from upnp_wps_device_init()
 *
 * This function queues the last WLANEvent to be sent for all currently
 * subscribed UPnP control points. sm->wlanevent must have been set with the
 * encoded data before calling this function.
 */
static void upnp_wps_device_send_event(struct upnp_wps_device_sm *sm)
{
	/* Enqueue event message for all subscribers */
	struct wpabuf *buf; /* holds event message */
	int buf_size = 0;
	struct subscription *s, *tmp;
	/* Actually, utf-8 is the default, but it doesn't hurt to specify it */
	const char *format_head =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\">\n";
	const char *format_tail = "</e:propertyset>\n";
	struct os_time now;

	if (dl_list_empty(&sm->subscriptions)) {
		/* optimize */
		return;
	}

	if (os_get_time(&now) == 0) {
		if (now.sec != sm->last_event_sec) {
			sm->last_event_sec = now.sec;
			sm->num_events_in_sec = 1;
		} else {
			sm->num_events_in_sec++;
			/*
			 * In theory, this should apply to all WLANEvent
			 * notifications, but EAP messages are of much higher
			 * priority and Probe Request notifications should not
			 * be allowed to drop EAP messages, so only throttle
			 * Probe Request notifications.
			 */
			if (sm->num_events_in_sec > MAX_EVENTS_PER_SEC &&
			    sm->wlanevent_type ==
			    UPNP_WPS_WLANEVENT_TYPE_PROBE) {
				wpa_printf(MSG_DEBUG, "WPS UPnP: Throttle "
					   "event notifications (%u seen "
					   "during one second)",
					   sm->num_events_in_sec);
				return;
			}
		}
	}

	/* Determine buffer size needed first */
	buf_size += os_strlen(format_head);
	buf_size += 50 + 2 * os_strlen("WLANEvent");
	if (sm->wlanevent)
		buf_size += os_strlen(sm->wlanevent);
	buf_size += os_strlen(format_tail);

	buf = wpabuf_alloc(buf_size);
	if (buf == NULL)
		return;
	wpabuf_put_str(buf, format_head);
	wpabuf_put_property(buf, "WLANEvent", sm->wlanevent);
	wpabuf_put_str(buf, format_tail);

	wpa_printf(MSG_MSGDUMP, "WPS UPnP: WLANEvent message:\n%s",
		   (char *) wpabuf_head(buf));

	dl_list_for_each_safe(s, tmp, &sm->subscriptions, struct subscription,
			      list) {
		event_add(s, buf,
			  sm->wlanevent_type == UPNP_WPS_WLANEVENT_TYPE_PROBE);
	}

	wpabuf_free(buf);
}


/*
 * Event subscription (subscriber machines register with us to receive event
 * messages).
 * This is the result of an incoming HTTP over TCP SUBSCRIBE request.
 */

/* subscription_destroy -- destroy an unlinked subscription
 * Be sure to unlink first if necessary.
 */
void subscription_destroy(struct subscription *s)
{
	wpa_printf(MSG_DEBUG, "WPS UPnP: Destroy subscription %p", s);
	subscr_addr_free_all(s);
	event_delete_all(s);
	upnp_er_remove_notification(s);
	os_free(s);
}


/* subscription_list_age -- remove expired subscriptions */
static void subscription_list_age(struct upnp_wps_device_sm *sm, time_t now)
{
	struct subscription *s, *tmp;
	dl_list_for_each_safe(s, tmp, &sm->subscriptions,
			      struct subscription, list) {
		if (s->timeout_time > now)
			break;
		wpa_printf(MSG_DEBUG, "WPS UPnP: Removing aged subscription");
		dl_list_del(&s->list);
		subscription_destroy(s);
	}
}


/* subscription_find -- return existing subscription matching uuid, if any
 * returns NULL if not found
 */
struct subscription * subscription_find(struct upnp_wps_device_sm *sm,
					const u8 uuid[UUID_LEN])
{
	struct subscription *s;
	dl_list_for_each(s, &sm->subscriptions, struct subscription, list) {
		if (os_memcmp(s->uuid, uuid, UUID_LEN) == 0)
			return s; /* Found match */
	}
	return NULL;
}


static struct wpabuf * build_fake_wsc_ack(void)
{
	struct wpabuf *msg = wpabuf_alloc(100);
	if (msg == NULL)
		return NULL;
	wpabuf_put_u8(msg, UPNP_WPS_WLANEVENT_TYPE_EAP);
	wpabuf_put_str(msg, "00:00:00:00:00:00");
	if (wps_build_version(msg) ||
	    wps_build_msg_type(msg, WPS_WSC_ACK)) {
		wpabuf_free(msg);
		return NULL;
	}
	/* Enrollee Nonce */
	wpabuf_put_be16(msg, ATTR_ENROLLEE_NONCE);
	wpabuf_put_be16(msg, WPS_NONCE_LEN);
	wpabuf_put(msg, WPS_NONCE_LEN);
	/* Registrar Nonce */
	wpabuf_put_be16(msg, ATTR_REGISTRAR_NONCE);
	wpabuf_put_be16(msg, WPS_NONCE_LEN);
	wpabuf_put(msg, WPS_NONCE_LEN);
	wps_build_wfa_ext(msg, 0, NULL, 0);
	return msg;
}


/* subscription_first_event -- send format/queue event that is automatically
 * sent on a new subscription.
 */
static int subscription_first_event(struct subscription *s)
{
	/*
	 * Actually, utf-8 is the default, but it doesn't hurt to specify it.
	 *
	 * APStatus is apparently a bit set,
	 * 0x1 = configuration change (but is always set?)
	 * 0x10 = ap is locked
	 *
	 * Per UPnP spec, we send out the last value of each variable, even
	 * for WLANEvent, whatever it was.
	 */
	char *wlan_event;
	struct wpabuf *buf;
	int ap_status = 1;      /* TODO: add 0x10 if access point is locked */
	const char *head =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\">\n";
	const char *tail = "</e:propertyset>\n";
	char txt[10];
	int ret;

	if (s->sm->wlanevent == NULL) {
		/*
		 * There has been no events before the subscription. However,
		 * UPnP device architecture specification requires all the
		 * evented variables to be included, so generate a dummy event
		 * for this particular case using a WSC_ACK and all-zeros
		 * nonces. The ER (UPnP control point) will ignore this, but at
		 * least it will learn that WLANEvent variable will be used in
		 * event notifications in the future.
		 */
		struct wpabuf *msg;
		wpa_printf(MSG_DEBUG, "WPS UPnP: Use a fake WSC_ACK as the "
			   "initial WLANEvent");
		msg = build_fake_wsc_ack();
		if (msg) {
			s->sm->wlanevent = (char *)
				base64_encode(wpabuf_head(msg),
					      wpabuf_len(msg), NULL);
			wpabuf_free(msg);
		}
	}

	wlan_event = s->sm->wlanevent;
	if (wlan_event == NULL || *wlan_event == '\0') {
		wpa_printf(MSG_DEBUG, "WPS UPnP: WLANEvent not known for "
			   "initial event message");
		wlan_event = "";
	}
	buf = wpabuf_alloc(500 + os_strlen(wlan_event));
	if (buf == NULL)
		return -1;

	wpabuf_put_str(buf, head);
	wpabuf_put_property(buf, "STAStatus", "1");
	os_snprintf(txt, sizeof(txt), "%d", ap_status);
	wpabuf_put_property(buf, "APStatus", txt);
	if (*wlan_event)
		wpabuf_put_property(buf, "WLANEvent", wlan_event);
	wpabuf_put_str(buf, tail);

	ret = event_add(s, buf, 0);
	if (ret) {
		wpabuf_free(buf);
		return ret;
	}
	wpabuf_free(buf);

	return 0;
}


/**
 * subscription_start - Remember a UPnP control point to send events to.
 * @sm: WPS UPnP state machine from upnp_wps_device_init()
 * @callback_urls: Callback URLs
 * Returns: %NULL on error, or pointer to new subscription structure.
 */
struct subscription * subscription_start(struct upnp_wps_device_sm *sm,
					 const char *callback_urls)
{
	struct subscription *s;
	time_t now = time(NULL);
	time_t expire = now + UPNP_SUBSCRIBE_SEC;

	/* Get rid of expired subscriptions so we have room */
	subscription_list_age(sm, now);

	/* If too many subscriptions, remove oldest */
	if (dl_list_len(&sm->subscriptions) >= MAX_SUBSCRIPTIONS) {
		s = dl_list_first(&sm->subscriptions, struct subscription,
				  list);
		wpa_printf(MSG_INFO, "WPS UPnP: Too many subscriptions, "
			   "trashing oldest");
		dl_list_del(&s->list);
		subscription_destroy(s);
	}

	s = os_zalloc(sizeof(*s));
	if (s == NULL)
		return NULL;
	dl_list_init(&s->addr_list);
	dl_list_init(&s->event_queue);

	s->sm = sm;
	s->timeout_time = expire;
	uuid_make(s->uuid);
	subscr_addr_list_create(s, callback_urls);
	if (dl_list_empty(&s->addr_list)) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: No valid callback URLs in "
			   "'%s' - drop subscription", callback_urls);
		subscription_destroy(s);
		return NULL;
	}

	/* Add to end of list, since it has the highest expiration time */
	dl_list_add_tail(&sm->subscriptions, &s->list);
	/* Queue up immediate event message (our last event)
	 * as required by UPnP spec.
	 */
	if (subscription_first_event(s)) {
		wpa_printf(MSG_INFO, "WPS UPnP: Dropping subscriber due to "
			   "event backlog");
		dl_list_del(&s->list);
		subscription_destroy(s);
		return NULL;
	}
	wpa_printf(MSG_DEBUG, "WPS UPnP: Subscription %p started with %s",
		   s, callback_urls);
	/* Schedule sending this */
	event_send_all_later(sm);
	return s;
}


/* subscription_renew -- find subscription and reset timeout */
struct subscription * subscription_renew(struct upnp_wps_device_sm *sm,
					 const u8 uuid[UUID_LEN])
{
	time_t now = time(NULL);
	time_t expire = now + UPNP_SUBSCRIBE_SEC;
	struct subscription *s = subscription_find(sm, uuid);
	if (s == NULL)
		return NULL;
	wpa_printf(MSG_DEBUG, "WPS UPnP: Subscription renewed");
	dl_list_del(&s->list);
	s->timeout_time = expire;
	/* add back to end of list, since it now has highest expiry */
	dl_list_add_tail(&sm->subscriptions, &s->list);
	return s;
}


/**
 * upnp_wps_device_send_wlan_event - Event notification
 * @sm: WPS UPnP state machine from upnp_wps_device_init()
 * @from_mac_addr: Source (Enrollee) MAC address for the event
 * @ev_type: Event type
 * @msg: Event data
 * Returns: 0 on success, -1 on failure
 *
 * Tell external Registrars (UPnP control points) that something happened. In
 * particular, events include WPS messages from clients that are proxied to
 * external Registrars.
 */
int upnp_wps_device_send_wlan_event(struct upnp_wps_device_sm *sm,
				    const u8 from_mac_addr[ETH_ALEN],
				    enum upnp_wps_wlanevent_type ev_type,
				    const struct wpabuf *msg)
{
	int ret = -1;
	char type[2];
	const u8 *mac = from_mac_addr;
	char mac_text[18];
	u8 *raw = NULL;
	size_t raw_len;
	char *val;
	size_t val_len;
	int pos = 0;

	if (!sm)
		goto fail;

	os_snprintf(type, sizeof(type), "%1u", ev_type);

	raw_len = 1 + 17 + (msg ? wpabuf_len(msg) : 0);
	raw = os_zalloc(raw_len);
	if (!raw)
		goto fail;

	*(raw + pos) = (u8) ev_type;
	pos += 1;
	os_snprintf(mac_text, sizeof(mac_text), MACSTR, MAC2STR(mac));
	wpa_printf(MSG_DEBUG, "WPS UPnP: Proxying WLANEvent from %s",
		   mac_text);
	os_memcpy(raw + pos, mac_text, 17);
	pos += 17;
	if (msg) {
		os_memcpy(raw + pos, wpabuf_head(msg), wpabuf_len(msg));
		pos += wpabuf_len(msg);
	}
	raw_len = pos;

	val = (char *) base64_encode(raw, raw_len, &val_len);
	if (val == NULL)
		goto fail;

	os_free(sm->wlanevent);
	sm->wlanevent = val;
	sm->wlanevent_type = ev_type;
	upnp_wps_device_send_event(sm);

	ret = 0;

fail:
	os_free(raw);

	return ret;
}


#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <sys/sysctl.h>
#include <net/route.h>
#include <net/if_dl.h>

static int eth_get(const char *device, u8 ea[ETH_ALEN])
{
	struct if_msghdr *ifm;
	struct sockaddr_dl *sdl;
	u_char *p, *buf;
	size_t len;
	int mib[] = { CTL_NET, AF_ROUTE, 0, AF_LINK, NET_RT_IFLIST, 0 };

	if (sysctl(mib, 6, NULL, &len, NULL, 0) < 0)
		return -1;
	if ((buf = os_malloc(len)) == NULL)
		return -1;
	if (sysctl(mib, 6, buf, &len, NULL, 0) < 0) {
		os_free(buf);
		return -1;
	}
	for (p = buf; p < buf + len; p += ifm->ifm_msglen) {
		ifm = (struct if_msghdr *)p;
		sdl = (struct sockaddr_dl *)(ifm + 1);
		if (ifm->ifm_type != RTM_IFINFO ||
		    (ifm->ifm_addrs & RTA_IFP) == 0)
			continue;
		if (sdl->sdl_family != AF_LINK || sdl->sdl_nlen == 0 ||
		    os_memcmp(sdl->sdl_data, device, sdl->sdl_nlen) != 0)
			continue;
		os_memcpy(ea, LLADDR(sdl), sdl->sdl_alen);
		break;
	}
	os_free(buf);

	if (p >= buf + len) {
		errno = ESRCH;
		return -1;
	}
	return 0;
}
#endif /* __FreeBSD__ */


/**
 * get_netif_info - Get hw and IP addresses for network device
 * @net_if: Selected network interface name
 * @ip_addr: Buffer for returning IP address in network byte order
 * @ip_addr_text: Buffer for returning a pointer to allocated IP address text
 * @mac: Buffer for returning MAC address
 * Returns: 0 on success, -1 on failure
 */
int get_netif_info(const char *net_if, unsigned *ip_addr, char **ip_addr_text,
		   u8 mac[ETH_ALEN])
{
	struct ifreq req;
	int sock = -1;
	struct sockaddr_in *addr;
	struct in_addr in_addr;

	*ip_addr_text = os_zalloc(16);
	if (*ip_addr_text == NULL)
		goto fail;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		goto fail;

	os_strlcpy(req.ifr_name, net_if, sizeof(req.ifr_name));
	if (ioctl(sock, SIOCGIFADDR, &req) < 0) {
		wpa_printf(MSG_ERROR, "WPS UPnP: SIOCGIFADDR failed: %d (%s)",
			   errno, strerror(errno));
		goto fail;
	}
	addr = (void *) &req.ifr_addr;
	*ip_addr = addr->sin_addr.s_addr;
	in_addr.s_addr = *ip_addr;
	os_snprintf(*ip_addr_text, 16, "%s", inet_ntoa(in_addr));

#ifdef __linux__
	os_strlcpy(req.ifr_name, net_if, sizeof(req.ifr_name));
	if (ioctl(sock, SIOCGIFHWADDR, &req) < 0) {
		wpa_printf(MSG_ERROR, "WPS UPnP: SIOCGIFHWADDR failed: "
			   "%d (%s)", errno, strerror(errno));
		goto fail;
	}
	os_memcpy(mac, req.ifr_addr.sa_data, 6);
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
	if (eth_get(net_if, mac) < 0) {
		wpa_printf(MSG_ERROR, "WPS UPnP: Failed to get MAC address");
		goto fail;
	}
#else
#error MAC address fetch not implemented
#endif

	close(sock);
	return 0;

fail:
	if (sock >= 0)
		close(sock);
	os_free(*ip_addr_text);
	*ip_addr_text = NULL;
	return -1;
}


static void upnp_wps_free_msearchreply(struct dl_list *head)
{
	struct advertisement_state_machine *a, *tmp;
	dl_list_for_each_safe(a, tmp, head, struct advertisement_state_machine,
			      list)
		msearchreply_state_machine_stop(a);
}


static void upnp_wps_free_subscriptions(struct dl_list *head,
					struct wps_registrar *reg)
{
	struct subscription *s, *tmp;
	dl_list_for_each_safe(s, tmp, head, struct subscription, list) {
		if (reg && s->reg != reg)
			continue;
		dl_list_del(&s->list);
		subscription_destroy(s);
	}
}


/**
 * upnp_wps_device_stop - Stop WPS UPnP operations on an interface
 * @sm: WPS UPnP state machine from upnp_wps_device_init()
 */
static void upnp_wps_device_stop(struct upnp_wps_device_sm *sm)
{
	if (!sm || !sm->started)
		return;

	wpa_printf(MSG_DEBUG, "WPS UPnP: Stop device");
	web_listener_stop(sm);
	upnp_wps_free_msearchreply(&sm->msearch_replies);
	upnp_wps_free_subscriptions(&sm->subscriptions, NULL);

	advertisement_state_machine_stop(sm, 1);

	event_send_stop_all(sm);
	os_free(sm->wlanevent);
	sm->wlanevent = NULL;
	os_free(sm->ip_addr_text);
	sm->ip_addr_text = NULL;
	if (sm->multicast_sd >= 0)
		close(sm->multicast_sd);
	sm->multicast_sd = -1;
	ssdp_listener_stop(sm);

	sm->started = 0;
}


/**
 * upnp_wps_device_start - Start WPS UPnP operations on an interface
 * @sm: WPS UPnP state machine from upnp_wps_device_init()
 * @net_if: Selected network interface name
 * Returns: 0 on success, -1 on failure
 */
static int upnp_wps_device_start(struct upnp_wps_device_sm *sm, char *net_if)
{
	if (!sm || !net_if)
		return -1;

	if (sm->started)
		upnp_wps_device_stop(sm);

	sm->multicast_sd = -1;
	sm->ssdp_sd = -1;
	sm->started = 1;
	sm->advertise_count = 0;

	/* Fix up linux multicast handling */
	if (add_ssdp_network(net_if))
		goto fail;

	/* Determine which IP and mac address we're using */
	if (get_netif_info(net_if, &sm->ip_addr, &sm->ip_addr_text,
			   sm->mac_addr)) {
		wpa_printf(MSG_INFO, "WPS UPnP: Could not get IP/MAC address "
			   "for %s. Does it have IP address?", net_if);
		goto fail;
	}

	/* Listen for incoming TCP connections so that others
	 * can fetch our "xml files" from us.
	 */
	if (web_listener_start(sm))
		goto fail;

	/* Set up for receiving discovery (UDP) packets */
	if (ssdp_listener_start(sm))
		goto fail;

	/* Set up for sending multicast */
	if (ssdp_open_multicast(sm) < 0)
		goto fail;

	/*
	 * Broadcast NOTIFY messages to let the world know we exist.
	 * This is done via a state machine since the messages should not be
	 * all sent out at once.
	 */
	if (advertisement_state_machine_start(sm))
		goto fail;

	return 0;

fail:
	upnp_wps_device_stop(sm);
	return -1;
}


static struct upnp_wps_device_interface *
upnp_wps_get_iface(struct upnp_wps_device_sm *sm, void *priv)
{
	struct upnp_wps_device_interface *iface;
	dl_list_for_each(iface, &sm->interfaces,
			 struct upnp_wps_device_interface, list) {
		if (iface->priv == priv)
			return iface;
	}
	return NULL;
}


/**
 * upnp_wps_device_deinit - Deinitialize WPS UPnP
 * @sm: WPS UPnP state machine from upnp_wps_device_init()
 * @priv: External context data that was used in upnp_wps_device_init() call
 */
void upnp_wps_device_deinit(struct upnp_wps_device_sm *sm, void *priv)
{
	struct upnp_wps_device_interface *iface;

	if (!sm)
		return;

	iface = upnp_wps_get_iface(sm, priv);
	if (iface == NULL) {
		wpa_printf(MSG_ERROR, "WPS UPnP: Could not find the interface "
			   "instance to deinit");
		return;
	}
	wpa_printf(MSG_DEBUG, "WPS UPnP: Deinit interface instance %p", iface);
	if (dl_list_len(&sm->interfaces) == 1) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: Deinitializing last instance "
			   "- free global device instance");
		upnp_wps_device_stop(sm);
	} else
		upnp_wps_free_subscriptions(&sm->subscriptions,
					    iface->wps->registrar);
	dl_list_del(&iface->list);

	if (iface->peer.wps)
		wps_deinit(iface->peer.wps);
	os_free(iface->ctx->ap_pin);
	os_free(iface->ctx);
	os_free(iface);

	if (dl_list_empty(&sm->interfaces)) {
		os_free(sm->root_dir);
		os_free(sm->desc_url);
		os_free(sm);
		shared_upnp_device = NULL;
	}
}


/**
 * upnp_wps_device_init - Initialize WPS UPnP
 * @ctx: callback table; we must eventually free it
 * @wps: Pointer to longterm WPS context
 * @priv: External context data that will be used in callbacks
 * @net_if: Selected network interface name
 * Returns: WPS UPnP state or %NULL on failure
 */
struct upnp_wps_device_sm *
upnp_wps_device_init(struct upnp_wps_device_ctx *ctx, struct wps_context *wps,
		     void *priv, char *net_if)
{
	struct upnp_wps_device_sm *sm;
	struct upnp_wps_device_interface *iface;
	int start = 0;

	iface = os_zalloc(sizeof(*iface));
	if (iface == NULL) {
		os_free(ctx->ap_pin);
		os_free(ctx);
		return NULL;
	}
	wpa_printf(MSG_DEBUG, "WPS UPnP: Init interface instance %p", iface);

	iface->ctx = ctx;
	iface->wps = wps;
	iface->priv = priv;

	if (shared_upnp_device) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: Share existing device "
			   "context");
		sm = shared_upnp_device;
	} else {
		wpa_printf(MSG_DEBUG, "WPS UPnP: Initialize device context");
		sm = os_zalloc(sizeof(*sm));
		if (!sm) {
			wpa_printf(MSG_ERROR, "WPS UPnP: upnp_wps_device_init "
				   "failed");
			os_free(iface);
			os_free(ctx->ap_pin);
			os_free(ctx);
			return NULL;
		}
		shared_upnp_device = sm;

		dl_list_init(&sm->msearch_replies);
		dl_list_init(&sm->subscriptions);
		dl_list_init(&sm->interfaces);
		start = 1;
	}

	dl_list_add(&sm->interfaces, &iface->list);

	if (start && upnp_wps_device_start(sm, net_if)) {
		upnp_wps_device_deinit(sm, priv);
		return NULL;
	}


	return sm;
}


/**
 * upnp_wps_subscribers - Check whether there are any event subscribers
 * @sm: WPS UPnP state machine from upnp_wps_device_init()
 * Returns: 0 if no subscribers, 1 if subscribers
 */
int upnp_wps_subscribers(struct upnp_wps_device_sm *sm)
{
	return !dl_list_empty(&sm->subscriptions);
}


int upnp_wps_set_ap_pin(struct upnp_wps_device_sm *sm, const char *ap_pin)
{
	struct upnp_wps_device_interface *iface;
	if (sm == NULL)
		return 0;

	dl_list_for_each(iface, &sm->interfaces,
			 struct upnp_wps_device_interface, list) {
		os_free(iface->ctx->ap_pin);
		if (ap_pin) {
			iface->ctx->ap_pin = os_strdup(ap_pin);
			if (iface->ctx->ap_pin == NULL)
				return -1;
		} else
			iface->ctx->ap_pin = NULL;
	}

	return 0;
}
