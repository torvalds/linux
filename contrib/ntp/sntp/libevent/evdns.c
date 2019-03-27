/* Copyright 2006-2007 Niels Provos
 * Copyright 2007-2012 Nick Mathewson and Niels Provos
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Based on software by Adam Langly. Adam's original message:
 *
 * Async DNS Library
 * Adam Langley <agl@imperialviolet.org>
 * http://www.imperialviolet.org/eventdns.html
 * Public Domain code
 *
 * This software is Public Domain. To view a copy of the public domain dedication,
 * visit http://creativecommons.org/licenses/publicdomain/ or send a letter to
 * Creative Commons, 559 Nathan Abbott Way, Stanford, California 94305, USA.
 *
 * I ask and expect, but do not require, that all derivative works contain an
 * attribution similar to:
 *	Parts developed by Adam Langley <agl@imperialviolet.org>
 *
 * You may wish to replace the word "Parts" with something else depending on
 * the amount of original code.
 *
 * (Derivative works does not include programs which link against, run or include
 * the source verbatim in their source distributions)
 *
 * Version: 0.1b
 */

#include "event2/event-config.h"
#include "evconfig-private.h"

#include <sys/types.h>

#ifndef _FORTIFY_SOURCE
#define _FORTIFY_SOURCE 3
#endif

#include <string.h>
#include <fcntl.h>
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef EVENT__HAVE_STDINT_H
#include <stdint.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef EVENT__HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <limits.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdarg.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef _WIN32_IE
#define _WIN32_IE 0x400
#endif
#include <shlobj.h>
#endif

#include "event2/dns.h"
#include "event2/dns_struct.h"
#include "event2/dns_compat.h"
#include "event2/util.h"
#include "event2/event.h"
#include "event2/event_struct.h"
#include "event2/thread.h"

#include "defer-internal.h"
#include "log-internal.h"
#include "mm-internal.h"
#include "strlcpy-internal.h"
#include "ipv6-internal.h"
#include "util-internal.h"
#include "evthread-internal.h"
#ifdef _WIN32
#include <ctype.h>
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <io.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#ifdef EVENT__HAVE_NETINET_IN6_H
#include <netinet/in6.h>
#endif

#define EVDNS_LOG_DEBUG EVENT_LOG_DEBUG
#define EVDNS_LOG_WARN EVENT_LOG_WARN
#define EVDNS_LOG_MSG EVENT_LOG_MSG

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

#include <stdio.h>

#undef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))

#define ASSERT_VALID_REQUEST(req) \
	EVUTIL_ASSERT((req)->handle && (req)->handle->current_req == (req))

#define u64 ev_uint64_t
#define u32 ev_uint32_t
#define u16 ev_uint16_t
#define u8  ev_uint8_t

/* maximum number of addresses from a single packet */
/* that we bother recording */
#define MAX_V4_ADDRS 32
#define MAX_V6_ADDRS 32


#define TYPE_A	       EVDNS_TYPE_A
#define TYPE_CNAME     5
#define TYPE_PTR       EVDNS_TYPE_PTR
#define TYPE_SOA       EVDNS_TYPE_SOA
#define TYPE_AAAA      EVDNS_TYPE_AAAA

#define CLASS_INET     EVDNS_CLASS_INET

/* Persistent handle.  We keep this separate from 'struct request' since we
 * need some object to last for as long as an evdns_request is outstanding so
 * that it can be canceled, whereas a search request can lead to multiple
 * 'struct request' instances being created over its lifetime. */
struct evdns_request {
	struct request *current_req;
	struct evdns_base *base;

	int pending_cb; /* Waiting for its callback to be invoked; not
			 * owned by event base any more. */

	/* elements used by the searching code */
	int search_index;
	struct search_state *search_state;
	char *search_origname;	/* needs to be free()ed */
	int search_flags;
};

struct request {
	u8 *request;  /* the dns packet data */
	u8 request_type; /* TYPE_PTR or TYPE_A or TYPE_AAAA */
	unsigned int request_len;
	int reissue_count;
	int tx_count;  /* the number of times that this packet has been sent */
	void *user_pointer;  /* the pointer given to us for this request */
	evdns_callback_type user_callback;
	struct nameserver *ns;	/* the server which we last sent it */

	/* these objects are kept in a circular list */
	/* XXX We could turn this into a CIRCLEQ. */
	struct request *next, *prev;

	struct event timeout_event;

	u16 trans_id;  /* the transaction id */
	unsigned request_appended :1;	/* true if the request pointer is data which follows this struct */
	unsigned transmit_me :1;  /* needs to be transmitted */

	/* XXXX This is a horrible hack. */
	char **put_cname_in_ptr; /* store the cname here if we get one. */

	struct evdns_base *base;

	struct evdns_request *handle;
};

struct reply {
	unsigned int type;
	unsigned int have_answer : 1;
	union {
		struct {
			u32 addrcount;
			u32 addresses[MAX_V4_ADDRS];
		} a;
		struct {
			u32 addrcount;
			struct in6_addr addresses[MAX_V6_ADDRS];
		} aaaa;
		struct {
			char name[HOST_NAME_MAX];
		} ptr;
	} data;
};

struct nameserver {
	evutil_socket_t socket;	 /* a connected UDP socket */
	struct sockaddr_storage address;
	ev_socklen_t addrlen;
	int failed_times;  /* number of times which we have given this server a chance */
	int timedout;  /* number of times in a row a request has timed out */
	struct event event;
	/* these objects are kept in a circular list */
	struct nameserver *next, *prev;
	struct event timeout_event;  /* used to keep the timeout for */
				     /* when we next probe this server. */
				     /* Valid if state == 0 */
	/* Outstanding probe request for this nameserver, if any */
	struct evdns_request *probe_request;
	char state;  /* zero if we think that this server is down */
	char choked;  /* true if we have an EAGAIN from this server's socket */
	char write_waiting;  /* true if we are waiting for EV_WRITE events */
	struct evdns_base *base;

	/* Number of currently inflight requests: used
	 * to track when we should add/del the event. */
	int requests_inflight;
};


/* Represents a local port where we're listening for DNS requests. Right now, */
/* only UDP is supported. */
struct evdns_server_port {
	evutil_socket_t socket; /* socket we use to read queries and write replies. */
	int refcnt; /* reference count. */
	char choked; /* Are we currently blocked from writing? */
	char closing; /* Are we trying to close this port, pending writes? */
	evdns_request_callback_fn_type user_callback; /* Fn to handle requests */
	void *user_data; /* Opaque pointer passed to user_callback */
	struct event event; /* Read/write event */
	/* circular list of replies that we want to write. */
	struct server_request *pending_replies;
	struct event_base *event_base;

#ifndef EVENT__DISABLE_THREAD_SUPPORT
	void *lock;
#endif
};

/* Represents part of a reply being built.	(That is, a single RR.) */
struct server_reply_item {
	struct server_reply_item *next; /* next item in sequence. */
	char *name; /* name part of the RR */
	u16 type; /* The RR type */
	u16 class; /* The RR class (usually CLASS_INET) */
	u32 ttl; /* The RR TTL */
	char is_name; /* True iff data is a label */
	u16 datalen; /* Length of data; -1 if data is a label */
	void *data; /* The contents of the RR */
};

/* Represents a request that we've received as a DNS server, and holds */
/* the components of the reply as we're constructing it. */
struct server_request {
	/* Pointers to the next and previous entries on the list of replies */
	/* that we're waiting to write.	 Only set if we have tried to respond */
	/* and gotten EAGAIN. */
	struct server_request *next_pending;
	struct server_request *prev_pending;

	u16 trans_id; /* Transaction id. */
	struct evdns_server_port *port; /* Which port received this request on? */
	struct sockaddr_storage addr; /* Where to send the response */
	ev_socklen_t addrlen; /* length of addr */

	int n_answer; /* how many answer RRs have been set? */
	int n_authority; /* how many authority RRs have been set? */
	int n_additional; /* how many additional RRs have been set? */

	struct server_reply_item *answer; /* linked list of answer RRs */
	struct server_reply_item *authority; /* linked list of authority RRs */
	struct server_reply_item *additional; /* linked list of additional RRs */

	/* Constructed response.  Only set once we're ready to send a reply. */
	/* Once this is set, the RR fields are cleared, and no more should be set. */
	char *response;
	size_t response_len;

	/* Caller-visible fields: flags, questions. */
	struct evdns_server_request base;
};

struct evdns_base {
	/* An array of n_req_heads circular lists for inflight requests.
	 * Each inflight request req is in req_heads[req->trans_id % n_req_heads].
	 */
	struct request **req_heads;
	/* A circular list of requests that we're waiting to send, but haven't
	 * sent yet because there are too many requests inflight */
	struct request *req_waiting_head;
	/* A circular list of nameservers. */
	struct nameserver *server_head;
	int n_req_heads;

	struct event_base *event_base;

	/* The number of good nameservers that we have */
	int global_good_nameservers;

	/* inflight requests are contained in the req_head list */
	/* and are actually going out across the network */
	int global_requests_inflight;
	/* requests which aren't inflight are in the waiting list */
	/* and are counted here */
	int global_requests_waiting;

	int global_max_requests_inflight;

	struct timeval global_timeout;	/* 5 seconds by default */
	int global_max_reissues;  /* a reissue occurs when we get some errors from the server */
	int global_max_retransmits;  /* number of times we'll retransmit a request which timed out */
	/* number of timeouts in a row before we consider this server to be down */
	int global_max_nameserver_timeout;
	/* true iff we will use the 0x20 hack to prevent poisoning attacks. */
	int global_randomize_case;

	/* The first time that a nameserver fails, how long do we wait before
	 * probing to see if it has returned?  */
	struct timeval global_nameserver_probe_initial_timeout;

	/** Port to bind to for outgoing DNS packets. */
	struct sockaddr_storage global_outgoing_address;
	/** ev_socklen_t for global_outgoing_address. 0 if it isn't set. */
	ev_socklen_t global_outgoing_addrlen;

	struct timeval global_getaddrinfo_allow_skew;

	int getaddrinfo_ipv4_timeouts;
	int getaddrinfo_ipv6_timeouts;
	int getaddrinfo_ipv4_answered;
	int getaddrinfo_ipv6_answered;

	struct search_state *global_search_state;

	TAILQ_HEAD(hosts_list, hosts_entry) hostsdb;

#ifndef EVENT__DISABLE_THREAD_SUPPORT
	void *lock;
#endif

	int disable_when_inactive;
};

struct hosts_entry {
	TAILQ_ENTRY(hosts_entry) next;
	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
		struct sockaddr_in6 sin6;
	} addr;
	int addrlen;
	char hostname[1];
};

static struct evdns_base *current_base = NULL;

struct evdns_base *
evdns_get_global_base(void)
{
	return current_base;
}

/* Given a pointer to an evdns_server_request, get the corresponding */
/* server_request. */
#define TO_SERVER_REQUEST(base_ptr)					\
	((struct server_request*)					\
	  (((char*)(base_ptr) - evutil_offsetof(struct server_request, base))))

#define REQ_HEAD(base, id) ((base)->req_heads[id % (base)->n_req_heads])

static struct nameserver *nameserver_pick(struct evdns_base *base);
static void evdns_request_insert(struct request *req, struct request **head);
static void evdns_request_remove(struct request *req, struct request **head);
static void nameserver_ready_callback(evutil_socket_t fd, short events, void *arg);
static int evdns_transmit(struct evdns_base *base);
static int evdns_request_transmit(struct request *req);
static void nameserver_send_probe(struct nameserver *const ns);
static void search_request_finished(struct evdns_request *const);
static int search_try_next(struct evdns_request *const req);
static struct request *search_request_new(struct evdns_base *base, struct evdns_request *handle, int type, const char *const name, int flags, evdns_callback_type user_callback, void *user_arg);
static void evdns_requests_pump_waiting_queue(struct evdns_base *base);
static u16 transaction_id_pick(struct evdns_base *base);
static struct request *request_new(struct evdns_base *base, struct evdns_request *handle, int type, const char *name, int flags, evdns_callback_type callback, void *ptr);
static void request_submit(struct request *const req);

static int server_request_free(struct server_request *req);
static void server_request_free_answers(struct server_request *req);
static void server_port_free(struct evdns_server_port *port);
static void server_port_ready_callback(evutil_socket_t fd, short events, void *arg);
static int evdns_base_resolv_conf_parse_impl(struct evdns_base *base, int flags, const char *const filename);
static int evdns_base_set_option_impl(struct evdns_base *base,
    const char *option, const char *val, int flags);
static void evdns_base_free_and_unlock(struct evdns_base *base, int fail_requests);
static void evdns_request_timeout_callback(evutil_socket_t fd, short events, void *arg);

static int strtoint(const char *const str);

#ifdef EVENT__DISABLE_THREAD_SUPPORT
#define EVDNS_LOCK(base)  EVUTIL_NIL_STMT_
#define EVDNS_UNLOCK(base) EVUTIL_NIL_STMT_
#define ASSERT_LOCKED(base) EVUTIL_NIL_STMT_
#else
#define EVDNS_LOCK(base)			\
	EVLOCK_LOCK((base)->lock, 0)
#define EVDNS_UNLOCK(base)			\
	EVLOCK_UNLOCK((base)->lock, 0)
#define ASSERT_LOCKED(base)			\
	EVLOCK_ASSERT_LOCKED((base)->lock)
#endif

static evdns_debug_log_fn_type evdns_log_fn = NULL;

void
evdns_set_log_fn(evdns_debug_log_fn_type fn)
{
	evdns_log_fn = fn;
}

#ifdef __GNUC__
#define EVDNS_LOG_CHECK	 __attribute__ ((format(printf, 2, 3)))
#else
#define EVDNS_LOG_CHECK
#endif

static void evdns_log_(int severity, const char *fmt, ...) EVDNS_LOG_CHECK;
static void
evdns_log_(int severity, const char *fmt, ...)
{
	va_list args;
	va_start(args,fmt);
	if (evdns_log_fn) {
		char buf[512];
		int is_warn = (severity == EVDNS_LOG_WARN);
		evutil_vsnprintf(buf, sizeof(buf), fmt, args);
		evdns_log_fn(is_warn, buf);
	} else {
		event_logv_(severity, NULL, fmt, args);
	}
	va_end(args);
}

#define log evdns_log_

/* This walks the list of inflight requests to find the */
/* one with a matching transaction id. Returns NULL on */
/* failure */
static struct request *
request_find_from_trans_id(struct evdns_base *base, u16 trans_id) {
	struct request *req = REQ_HEAD(base, trans_id);
	struct request *const started_at = req;

	ASSERT_LOCKED(base);

	if (req) {
		do {
			if (req->trans_id == trans_id) return req;
			req = req->next;
		} while (req != started_at);
	}

	return NULL;
}

/* a libevent callback function which is called when a nameserver */
/* has gone down and we want to test if it has came back to life yet */
static void
nameserver_prod_callback(evutil_socket_t fd, short events, void *arg) {
	struct nameserver *const ns = (struct nameserver *) arg;
	(void)fd;
	(void)events;

	EVDNS_LOCK(ns->base);
	nameserver_send_probe(ns);
	EVDNS_UNLOCK(ns->base);
}

/* a libevent callback which is called when a nameserver probe (to see if */
/* it has come back to life) times out. We increment the count of failed_times */
/* and wait longer to send the next probe packet. */
static void
nameserver_probe_failed(struct nameserver *const ns) {
	struct timeval timeout;
	int i;

	ASSERT_LOCKED(ns->base);
	(void) evtimer_del(&ns->timeout_event);
	if (ns->state == 1) {
		/* This can happen if the nameserver acts in a way which makes us mark */
		/* it as bad and then starts sending good replies. */
		return;
	}

#define MAX_PROBE_TIMEOUT 3600
#define TIMEOUT_BACKOFF_FACTOR 3

	memcpy(&timeout, &ns->base->global_nameserver_probe_initial_timeout,
	    sizeof(struct timeval));
	for (i=ns->failed_times; i > 0 && timeout.tv_sec < MAX_PROBE_TIMEOUT; --i) {
		timeout.tv_sec *= TIMEOUT_BACKOFF_FACTOR;
		timeout.tv_usec *= TIMEOUT_BACKOFF_FACTOR;
		if (timeout.tv_usec > 1000000) {
			timeout.tv_sec += timeout.tv_usec / 1000000;
			timeout.tv_usec %= 1000000;
		}
	}
	if (timeout.tv_sec > MAX_PROBE_TIMEOUT) {
		timeout.tv_sec = MAX_PROBE_TIMEOUT;
		timeout.tv_usec = 0;
	}

	ns->failed_times++;

	if (evtimer_add(&ns->timeout_event, &timeout) < 0) {
		char addrbuf[128];
		log(EVDNS_LOG_WARN,
		    "Error from libevent when adding timer event for %s",
		    evutil_format_sockaddr_port_(
			    (struct sockaddr *)&ns->address,
			    addrbuf, sizeof(addrbuf)));
	}
}

static void
request_swap_ns(struct request *req, struct nameserver *ns) {
	if (ns && req->ns != ns) {
		EVUTIL_ASSERT(req->ns->requests_inflight > 0);
		req->ns->requests_inflight--;
		ns->requests_inflight++;

		req->ns = ns;
	}
}

/* called when a nameserver has been deemed to have failed. For example, too */
/* many packets have timed out etc */
static void
nameserver_failed(struct nameserver *const ns, const char *msg) {
	struct request *req, *started_at;
	struct evdns_base *base = ns->base;
	int i;
	char addrbuf[128];

	ASSERT_LOCKED(base);
	/* if this nameserver has already been marked as failed */
	/* then don't do anything */
	if (!ns->state) return;

	log(EVDNS_LOG_MSG, "Nameserver %s has failed: %s",
	    evutil_format_sockaddr_port_(
		    (struct sockaddr *)&ns->address,
		    addrbuf, sizeof(addrbuf)),
	    msg);

	base->global_good_nameservers--;
	EVUTIL_ASSERT(base->global_good_nameservers >= 0);
	if (base->global_good_nameservers == 0) {
		log(EVDNS_LOG_MSG, "All nameservers have failed");
	}

	ns->state = 0;
	ns->failed_times = 1;

	if (evtimer_add(&ns->timeout_event,
		&base->global_nameserver_probe_initial_timeout) < 0) {
		log(EVDNS_LOG_WARN,
		    "Error from libevent when adding timer event for %s",
		    evutil_format_sockaddr_port_(
			    (struct sockaddr *)&ns->address,
			    addrbuf, sizeof(addrbuf)));
		/* ???? Do more? */
	}

	/* walk the list of inflight requests to see if any can be reassigned to */
	/* a different server. Requests in the waiting queue don't have a */
	/* nameserver assigned yet */

	/* if we don't have *any* good nameservers then there's no point */
	/* trying to reassign requests to one */
	if (!base->global_good_nameservers) return;

	for (i = 0; i < base->n_req_heads; ++i) {
		req = started_at = base->req_heads[i];
		if (req) {
			do {
				if (req->tx_count == 0 && req->ns == ns) {
					/* still waiting to go out, can be moved */
					/* to another server */
					request_swap_ns(req, nameserver_pick(base));
				}
				req = req->next;
			} while (req != started_at);
		}
	}
}

static void
nameserver_up(struct nameserver *const ns)
{
	char addrbuf[128];
	ASSERT_LOCKED(ns->base);
	if (ns->state) return;
	log(EVDNS_LOG_MSG, "Nameserver %s is back up",
	    evutil_format_sockaddr_port_(
		    (struct sockaddr *)&ns->address,
		    addrbuf, sizeof(addrbuf)));
	evtimer_del(&ns->timeout_event);
	if (ns->probe_request) {
		evdns_cancel_request(ns->base, ns->probe_request);
		ns->probe_request = NULL;
	}
	ns->state = 1;
	ns->failed_times = 0;
	ns->timedout = 0;
	ns->base->global_good_nameservers++;
}

static void
request_trans_id_set(struct request *const req, const u16 trans_id) {
	req->trans_id = trans_id;
	*((u16 *) req->request) = htons(trans_id);
}

/* Called to remove a request from a list and dealloc it. */
/* head is a pointer to the head of the list it should be */
/* removed from or NULL if the request isn't in a list. */
/* when free_handle is one, free the handle as well. */
static void
request_finished(struct request *const req, struct request **head, int free_handle) {
	struct evdns_base *base = req->base;
	int was_inflight = (head != &base->req_waiting_head);
	EVDNS_LOCK(base);
	ASSERT_VALID_REQUEST(req);

	if (head)
		evdns_request_remove(req, head);

	log(EVDNS_LOG_DEBUG, "Removing timeout for request %p", req);
	if (was_inflight) {
		evtimer_del(&req->timeout_event);
		base->global_requests_inflight--;
		req->ns->requests_inflight--;
	} else {
		base->global_requests_waiting--;
	}
	/* it was initialized during request_new / evtimer_assign */
	event_debug_unassign(&req->timeout_event);

	if (req->ns &&
	    req->ns->requests_inflight == 0 &&
	    req->base->disable_when_inactive) {
		event_del(&req->ns->event);
		evtimer_del(&req->ns->timeout_event);
	}

	if (!req->request_appended) {
		/* need to free the request data on it's own */
		mm_free(req->request);
	} else {
		/* the request data is appended onto the header */
		/* so everything gets free()ed when we: */
	}

	if (req->handle) {
		EVUTIL_ASSERT(req->handle->current_req == req);

		if (free_handle) {
			search_request_finished(req->handle);
			req->handle->current_req = NULL;
			if (! req->handle->pending_cb) {
				/* If we're planning to run the callback,
				 * don't free the handle until later. */
				mm_free(req->handle);
			}
			req->handle = NULL; /* If we have a bug, let's crash
					     * early */
		} else {
			req->handle->current_req = NULL;
		}
	}

	mm_free(req);

	evdns_requests_pump_waiting_queue(base);
	EVDNS_UNLOCK(base);
}

/* This is called when a server returns a funny error code. */
/* We try the request again with another server. */
/* */
/* return: */
/*   0 ok */
/*   1 failed/reissue is pointless */
static int
request_reissue(struct request *req) {
	const struct nameserver *const last_ns = req->ns;
	ASSERT_LOCKED(req->base);
	ASSERT_VALID_REQUEST(req);
	/* the last nameserver should have been marked as failing */
	/* by the caller of this function, therefore pick will try */
	/* not to return it */
	request_swap_ns(req, nameserver_pick(req->base));
	if (req->ns == last_ns) {
		/* ... but pick did return it */
		/* not a lot of point in trying again with the */
		/* same server */
		return 1;
	}

	req->reissue_count++;
	req->tx_count = 0;
	req->transmit_me = 1;

	return 0;
}

/* this function looks for space on the inflight queue and promotes */
/* requests from the waiting queue if it can. */
/* */
/* TODO: */
/* add return code, see at nameserver_pick() and other functions. */
static void
evdns_requests_pump_waiting_queue(struct evdns_base *base) {
	ASSERT_LOCKED(base);
	while (base->global_requests_inflight < base->global_max_requests_inflight &&
		   base->global_requests_waiting) {
		struct request *req;

		EVUTIL_ASSERT(base->req_waiting_head);
		req = base->req_waiting_head;

		req->ns = nameserver_pick(base);
		if (!req->ns)
			return;

		/* move a request from the waiting queue to the inflight queue */
		req->ns->requests_inflight++;

		evdns_request_remove(req, &base->req_waiting_head);

		base->global_requests_waiting--;
		base->global_requests_inflight++;

		request_trans_id_set(req, transaction_id_pick(base));

		evdns_request_insert(req, &REQ_HEAD(base, req->trans_id));
		evdns_request_transmit(req);
		evdns_transmit(base);
	}
}

/* TODO(nickm) document */
struct deferred_reply_callback {
	struct event_callback deferred;
	struct evdns_request *handle;
	u8 request_type;
	u8 have_reply;
	u32 ttl;
	u32 err;
	evdns_callback_type user_callback;
	struct reply reply;
};

static void
reply_run_callback(struct event_callback *d, void *user_pointer)
{
	struct deferred_reply_callback *cb =
	    EVUTIL_UPCAST(d, struct deferred_reply_callback, deferred);

	switch (cb->request_type) {
	case TYPE_A:
		if (cb->have_reply)
			cb->user_callback(DNS_ERR_NONE, DNS_IPv4_A,
			    cb->reply.data.a.addrcount, cb->ttl,
			    cb->reply.data.a.addresses,
			    user_pointer);
		else
			cb->user_callback(cb->err, 0, 0, cb->ttl, NULL, user_pointer);
		break;
	case TYPE_PTR:
		if (cb->have_reply) {
			char *name = cb->reply.data.ptr.name;
			cb->user_callback(DNS_ERR_NONE, DNS_PTR, 1, cb->ttl,
			    &name, user_pointer);
		} else {
			cb->user_callback(cb->err, 0, 0, cb->ttl, NULL, user_pointer);
		}
		break;
	case TYPE_AAAA:
		if (cb->have_reply)
			cb->user_callback(DNS_ERR_NONE, DNS_IPv6_AAAA,
			    cb->reply.data.aaaa.addrcount, cb->ttl,
			    cb->reply.data.aaaa.addresses,
			    user_pointer);
		else
			cb->user_callback(cb->err, 0, 0, cb->ttl, NULL, user_pointer);
		break;
	default:
		EVUTIL_ASSERT(0);
	}

	if (cb->handle && cb->handle->pending_cb) {
		mm_free(cb->handle);
	}

	mm_free(cb);
}

static void
reply_schedule_callback(struct request *const req, u32 ttl, u32 err, struct reply *reply)
{
	struct deferred_reply_callback *d = mm_calloc(1, sizeof(*d));

	if (!d) {
		event_warn("%s: Couldn't allocate space for deferred callback.",
		    __func__);
		return;
	}

	ASSERT_LOCKED(req->base);

	d->request_type = req->request_type;
	d->user_callback = req->user_callback;
	d->ttl = ttl;
	d->err = err;
	if (reply) {
		d->have_reply = 1;
		memcpy(&d->reply, reply, sizeof(struct reply));
	}

	if (req->handle) {
		req->handle->pending_cb = 1;
		d->handle = req->handle;
	}

	event_deferred_cb_init_(
	    &d->deferred,
	    event_get_priority(&req->timeout_event),
	    reply_run_callback,
	    req->user_pointer);
	event_deferred_cb_schedule_(
		req->base->event_base,
		&d->deferred);
}

/* this processes a parsed reply packet */
static void
reply_handle(struct request *const req, u16 flags, u32 ttl, struct reply *reply) {
	int error;
	char addrbuf[128];
	static const int error_codes[] = {
		DNS_ERR_FORMAT, DNS_ERR_SERVERFAILED, DNS_ERR_NOTEXIST,
		DNS_ERR_NOTIMPL, DNS_ERR_REFUSED
	};

	ASSERT_LOCKED(req->base);
	ASSERT_VALID_REQUEST(req);

	if (flags & 0x020f || !reply || !reply->have_answer) {
		/* there was an error */
		if (flags & 0x0200) {
			error = DNS_ERR_TRUNCATED;
		} else if (flags & 0x000f) {
			u16 error_code = (flags & 0x000f) - 1;
			if (error_code > 4) {
				error = DNS_ERR_UNKNOWN;
			} else {
				error = error_codes[error_code];
			}
		} else if (reply && !reply->have_answer) {
			error = DNS_ERR_NODATA;
		} else {
			error = DNS_ERR_UNKNOWN;
		}

		switch (error) {
		case DNS_ERR_NOTIMPL:
		case DNS_ERR_REFUSED:
			/* we regard these errors as marking a bad nameserver */
			if (req->reissue_count < req->base->global_max_reissues) {
				char msg[64];
				evutil_snprintf(msg, sizeof(msg), "Bad response %d (%s)",
					 error, evdns_err_to_string(error));
				nameserver_failed(req->ns, msg);
				if (!request_reissue(req)) return;
			}
			break;
		case DNS_ERR_SERVERFAILED:
			/* rcode 2 (servfailed) sometimes means "we
			 * are broken" and sometimes (with some binds)
			 * means "that request was very confusing."
			 * Treat this as a timeout, not a failure.
			 */
			log(EVDNS_LOG_DEBUG, "Got a SERVERFAILED from nameserver"
				"at %s; will allow the request to time out.",
			    evutil_format_sockaddr_port_(
				    (struct sockaddr *)&req->ns->address,
				    addrbuf, sizeof(addrbuf)));
			/* Call the timeout function */
			evdns_request_timeout_callback(0, 0, req);
			return;
		default:
			/* we got a good reply from the nameserver: it is up. */
			if (req->handle == req->ns->probe_request) {
				/* Avoid double-free */
				req->ns->probe_request = NULL;
			}

			nameserver_up(req->ns);
		}

		if (req->handle->search_state &&
		    req->request_type != TYPE_PTR) {
			/* if we have a list of domains to search in,
			 * try the next one */
			if (!search_try_next(req->handle)) {
				/* a new request was issued so this
				 * request is finished and */
				/* the user callback will be made when
				 * that request (or a */
				/* child of it) finishes. */
				return;
			}
		}

		/* all else failed. Pass the failure up */
		reply_schedule_callback(req, ttl, error, NULL);
		request_finished(req, &REQ_HEAD(req->base, req->trans_id), 1);
	} else {
		/* all ok, tell the user */
		reply_schedule_callback(req, ttl, 0, reply);
		if (req->handle == req->ns->probe_request)
			req->ns->probe_request = NULL; /* Avoid double-free */
		nameserver_up(req->ns);
		request_finished(req, &REQ_HEAD(req->base, req->trans_id), 1);
	}
}

static int
name_parse(u8 *packet, int length, int *idx, char *name_out, int name_out_len) {
	int name_end = -1;
	int j = *idx;
	int ptr_count = 0;
#define GET32(x) do { if (j + 4 > length) goto err; memcpy(&t32_, packet + j, 4); j += 4; x = ntohl(t32_); } while (0)
#define GET16(x) do { if (j + 2 > length) goto err; memcpy(&t_, packet + j, 2); j += 2; x = ntohs(t_); } while (0)
#define GET8(x) do { if (j >= length) goto err; x = packet[j++]; } while (0)

	char *cp = name_out;
	const char *const end = name_out + name_out_len;

	/* Normally, names are a series of length prefixed strings terminated */
	/* with a length of 0 (the lengths are u8's < 63). */
	/* However, the length can start with a pair of 1 bits and that */
	/* means that the next 14 bits are a pointer within the current */
	/* packet. */

	for (;;) {
		u8 label_len;
		if (j >= length) return -1;
		GET8(label_len);
		if (!label_len) break;
		if (label_len & 0xc0) {
			u8 ptr_low;
			GET8(ptr_low);
			if (name_end < 0) name_end = j;
			j = (((int)label_len & 0x3f) << 8) + ptr_low;
			/* Make sure that the target offset is in-bounds. */
			if (j < 0 || j >= length) return -1;
			/* If we've jumped more times than there are characters in the
			 * message, we must have a loop. */
			if (++ptr_count > length) return -1;
			continue;
		}
		if (label_len > 63) return -1;
		if (cp != name_out) {
			if (cp + 1 >= end) return -1;
			*cp++ = '.';
		}
		if (cp + label_len >= end) return -1;
		memcpy(cp, packet + j, label_len);
		cp += label_len;
		j += label_len;
	}
	if (cp >= end) return -1;
	*cp = '\0';
	if (name_end < 0)
		*idx = j;
	else
		*idx = name_end;
	return 0;
 err:
	return -1;
}

/* parses a raw request from a nameserver */
static int
reply_parse(struct evdns_base *base, u8 *packet, int length) {
	int j = 0, k = 0;  /* index into packet */
	u16 t_;	 /* used by the macros */
	u32 t32_;  /* used by the macros */
	char tmp_name[256], cmp_name[256]; /* used by the macros */
	int name_matches = 0;

	u16 trans_id, questions, answers, authority, additional, datalength;
	u16 flags = 0;
	u32 ttl, ttl_r = 0xffffffff;
	struct reply reply;
	struct request *req = NULL;
	unsigned int i;

	ASSERT_LOCKED(base);

	GET16(trans_id);
	GET16(flags);
	GET16(questions);
	GET16(answers);
	GET16(authority);
	GET16(additional);
	(void) authority; /* suppress "unused variable" warnings. */
	(void) additional; /* suppress "unused variable" warnings. */

	req = request_find_from_trans_id(base, trans_id);
	if (!req) return -1;
	EVUTIL_ASSERT(req->base == base);

	memset(&reply, 0, sizeof(reply));

	/* If it's not an answer, it doesn't correspond to any request. */
	if (!(flags & 0x8000)) return -1;  /* must be an answer */
	if ((flags & 0x020f) && (flags & 0x020f) != DNS_ERR_NOTEXIST) {
		/* there was an error and it's not NXDOMAIN */
		goto err;
	}
	/* if (!answers) return; */  /* must have an answer of some form */

	/* This macro skips a name in the DNS reply. */
#define SKIP_NAME						\
	do { tmp_name[0] = '\0';				\
		if (name_parse(packet, length, &j, tmp_name,	\
			sizeof(tmp_name))<0)			\
			goto err;				\
	} while (0)
#define TEST_NAME							\
	do { tmp_name[0] = '\0';					\
		cmp_name[0] = '\0';					\
		k = j;							\
		if (name_parse(packet, length, &j, tmp_name,		\
			sizeof(tmp_name))<0)				\
			goto err;					\
		if (name_parse(req->request, req->request_len, &k,	\
			cmp_name, sizeof(cmp_name))<0)			\
			goto err;					\
		if (base->global_randomize_case) {			\
			if (strcmp(tmp_name, cmp_name) == 0)		\
				name_matches = 1;			\
		} else {						\
			if (evutil_ascii_strcasecmp(tmp_name, cmp_name) == 0) \
				name_matches = 1;			\
		}							\
	} while (0)

	reply.type = req->request_type;

	/* skip over each question in the reply */
	for (i = 0; i < questions; ++i) {
		/* the question looks like
		 *   <label:name><u16:type><u16:class>
		 */
		TEST_NAME;
		j += 4;
		if (j > length) goto err;
	}

	if (!name_matches)
		goto err;

	/* now we have the answer section which looks like
	 * <label:name><u16:type><u16:class><u32:ttl><u16:len><data...>
	 */

	for (i = 0; i < answers; ++i) {
		u16 type, class;

		SKIP_NAME;
		GET16(type);
		GET16(class);
		GET32(ttl);
		GET16(datalength);

		if (type == TYPE_A && class == CLASS_INET) {
			int addrcount, addrtocopy;
			if (req->request_type != TYPE_A) {
				j += datalength; continue;
			}
			if ((datalength & 3) != 0) /* not an even number of As. */
			    goto err;
			addrcount = datalength >> 2;
			addrtocopy = MIN(MAX_V4_ADDRS - reply.data.a.addrcount, (unsigned)addrcount);

			ttl_r = MIN(ttl_r, ttl);
			/* we only bother with the first four addresses. */
			if (j + 4*addrtocopy > length) goto err;
			memcpy(&reply.data.a.addresses[reply.data.a.addrcount],
				   packet + j, 4*addrtocopy);
			j += 4*addrtocopy;
			reply.data.a.addrcount += addrtocopy;
			reply.have_answer = 1;
			if (reply.data.a.addrcount == MAX_V4_ADDRS) break;
		} else if (type == TYPE_PTR && class == CLASS_INET) {
			if (req->request_type != TYPE_PTR) {
				j += datalength; continue;
			}
			if (name_parse(packet, length, &j, reply.data.ptr.name,
						   sizeof(reply.data.ptr.name))<0)
				goto err;
			ttl_r = MIN(ttl_r, ttl);
			reply.have_answer = 1;
			break;
		} else if (type == TYPE_CNAME) {
			char cname[HOST_NAME_MAX];
			if (!req->put_cname_in_ptr || *req->put_cname_in_ptr) {
				j += datalength; continue;
			}
			if (name_parse(packet, length, &j, cname,
				sizeof(cname))<0)
				goto err;
			*req->put_cname_in_ptr = mm_strdup(cname);
		} else if (type == TYPE_AAAA && class == CLASS_INET) {
			int addrcount, addrtocopy;
			if (req->request_type != TYPE_AAAA) {
				j += datalength; continue;
			}
			if ((datalength & 15) != 0) /* not an even number of AAAAs. */
				goto err;
			addrcount = datalength >> 4;  /* each address is 16 bytes long */
			addrtocopy = MIN(MAX_V6_ADDRS - reply.data.aaaa.addrcount, (unsigned)addrcount);
			ttl_r = MIN(ttl_r, ttl);

			/* we only bother with the first four addresses. */
			if (j + 16*addrtocopy > length) goto err;
			memcpy(&reply.data.aaaa.addresses[reply.data.aaaa.addrcount],
				   packet + j, 16*addrtocopy);
			reply.data.aaaa.addrcount += addrtocopy;
			j += 16*addrtocopy;
			reply.have_answer = 1;
			if (reply.data.aaaa.addrcount == MAX_V6_ADDRS) break;
		} else {
			/* skip over any other type of resource */
			j += datalength;
		}
	}

	if (!reply.have_answer) {
		for (i = 0; i < authority; ++i) {
			u16 type, class;
			SKIP_NAME;
			GET16(type);
			GET16(class);
			GET32(ttl);
			GET16(datalength);
			if (type == TYPE_SOA && class == CLASS_INET) {
				u32 serial, refresh, retry, expire, minimum;
				SKIP_NAME;
				SKIP_NAME;
				GET32(serial);
				GET32(refresh);
				GET32(retry);
				GET32(expire);
				GET32(minimum);
				(void)expire;
				(void)retry;
				(void)refresh;
				(void)serial;
				ttl_r = MIN(ttl_r, ttl);
				ttl_r = MIN(ttl_r, minimum);
			} else {
				/* skip over any other type of resource */
				j += datalength;
			}
		}
	}

	if (ttl_r == 0xffffffff)
		ttl_r = 0;

	reply_handle(req, flags, ttl_r, &reply);
	return 0;
 err:
	if (req)
		reply_handle(req, flags, 0, NULL);
	return -1;
}

/* Parse a raw request (packet,length) sent to a nameserver port (port) from */
/* a DNS client (addr,addrlen), and if it's well-formed, call the corresponding */
/* callback. */
static int
request_parse(u8 *packet, int length, struct evdns_server_port *port, struct sockaddr *addr, ev_socklen_t addrlen)
{
	int j = 0;	/* index into packet */
	u16 t_;	 /* used by the macros */
	char tmp_name[256]; /* used by the macros */

	int i;
	u16 trans_id, flags, questions, answers, authority, additional;
	struct server_request *server_req = NULL;

	ASSERT_LOCKED(port);

	/* Get the header fields */
	GET16(trans_id);
	GET16(flags);
	GET16(questions);
	GET16(answers);
	GET16(authority);
	GET16(additional);
	(void)answers;
	(void)additional;
	(void)authority;

	if (flags & 0x8000) return -1; /* Must not be an answer. */
	flags &= 0x0110; /* Only RD and CD get preserved. */

	server_req = mm_malloc(sizeof(struct server_request));
	if (server_req == NULL) return -1;
	memset(server_req, 0, sizeof(struct server_request));

	server_req->trans_id = trans_id;
	memcpy(&server_req->addr, addr, addrlen);
	server_req->addrlen = addrlen;

	server_req->base.flags = flags;
	server_req->base.nquestions = 0;
	server_req->base.questions = mm_calloc(sizeof(struct evdns_server_question *), questions);
	if (server_req->base.questions == NULL)
		goto err;

	for (i = 0; i < questions; ++i) {
		u16 type, class;
		struct evdns_server_question *q;
		int namelen;
		if (name_parse(packet, length, &j, tmp_name, sizeof(tmp_name))<0)
			goto err;
		GET16(type);
		GET16(class);
		namelen = (int)strlen(tmp_name);
		q = mm_malloc(sizeof(struct evdns_server_question) + namelen);
		if (!q)
			goto err;
		q->type = type;
		q->dns_question_class = class;
		memcpy(q->name, tmp_name, namelen+1);
		server_req->base.questions[server_req->base.nquestions++] = q;
	}

	/* Ignore answers, authority, and additional. */

	server_req->port = port;
	port->refcnt++;

	/* Only standard queries are supported. */
	if (flags & 0x7800) {
		evdns_server_request_respond(&(server_req->base), DNS_ERR_NOTIMPL);
		return -1;
	}

	port->user_callback(&(server_req->base), port->user_data);

	return 0;
err:
	if (server_req) {
		if (server_req->base.questions) {
			for (i = 0; i < server_req->base.nquestions; ++i)
				mm_free(server_req->base.questions[i]);
			mm_free(server_req->base.questions);
		}
		mm_free(server_req);
	}
	return -1;

#undef SKIP_NAME
#undef GET32
#undef GET16
#undef GET8
}


void
evdns_set_transaction_id_fn(ev_uint16_t (*fn)(void))
{
}

void
evdns_set_random_bytes_fn(void (*fn)(char *, size_t))
{
}

/* Try to choose a strong transaction id which isn't already in flight */
static u16
transaction_id_pick(struct evdns_base *base) {
	ASSERT_LOCKED(base);
	for (;;) {
		u16 trans_id;
		evutil_secure_rng_get_bytes(&trans_id, sizeof(trans_id));

		if (trans_id == 0xffff) continue;
		/* now check to see if that id is already inflight */
		if (request_find_from_trans_id(base, trans_id) == NULL)
			return trans_id;
	}
}

/* choose a namesever to use. This function will try to ignore */
/* nameservers which we think are down and load balance across the rest */
/* by updating the server_head global each time. */
static struct nameserver *
nameserver_pick(struct evdns_base *base) {
	struct nameserver *started_at = base->server_head, *picked;
	ASSERT_LOCKED(base);
	if (!base->server_head) return NULL;

	/* if we don't have any good nameservers then there's no */
	/* point in trying to find one. */
	if (!base->global_good_nameservers) {
		base->server_head = base->server_head->next;
		return base->server_head;
	}

	/* remember that nameservers are in a circular list */
	for (;;) {
		if (base->server_head->state) {
			/* we think this server is currently good */
			picked = base->server_head;
			base->server_head = base->server_head->next;
			return picked;
		}

		base->server_head = base->server_head->next;
		if (base->server_head == started_at) {
			/* all the nameservers seem to be down */
			/* so we just return this one and hope for the */
			/* best */
			EVUTIL_ASSERT(base->global_good_nameservers == 0);
			picked = base->server_head;
			base->server_head = base->server_head->next;
			return picked;
		}
	}
}

/* this is called when a namesever socket is ready for reading */
static void
nameserver_read(struct nameserver *ns) {
	struct sockaddr_storage ss;
	ev_socklen_t addrlen = sizeof(ss);
	u8 packet[1500];
	char addrbuf[128];
	ASSERT_LOCKED(ns->base);

	for (;;) {
		const int r = recvfrom(ns->socket, (void*)packet,
		    sizeof(packet), 0,
		    (struct sockaddr*)&ss, &addrlen);
		if (r < 0) {
			int err = evutil_socket_geterror(ns->socket);
			if (EVUTIL_ERR_RW_RETRIABLE(err))
				return;
			nameserver_failed(ns,
			    evutil_socket_error_to_string(err));
			return;
		}
		if (evutil_sockaddr_cmp((struct sockaddr*)&ss,
			(struct sockaddr*)&ns->address, 0)) {
			log(EVDNS_LOG_WARN, "Address mismatch on received "
			    "DNS packet.  Apparent source was %s",
			    evutil_format_sockaddr_port_(
				    (struct sockaddr *)&ss,
				    addrbuf, sizeof(addrbuf)));
			return;
		}

		ns->timedout = 0;
		reply_parse(ns->base, packet, r);
	}
}

/* Read a packet from a DNS client on a server port s, parse it, and */
/* act accordingly. */
static void
server_port_read(struct evdns_server_port *s) {
	u8 packet[1500];
	struct sockaddr_storage addr;
	ev_socklen_t addrlen;
	int r;
	ASSERT_LOCKED(s);

	for (;;) {
		addrlen = sizeof(struct sockaddr_storage);
		r = recvfrom(s->socket, (void*)packet, sizeof(packet), 0,
					 (struct sockaddr*) &addr, &addrlen);
		if (r < 0) {
			int err = evutil_socket_geterror(s->socket);
			if (EVUTIL_ERR_RW_RETRIABLE(err))
				return;
			log(EVDNS_LOG_WARN,
			    "Error %s (%d) while reading request.",
			    evutil_socket_error_to_string(err), err);
			return;
		}
		request_parse(packet, r, s, (struct sockaddr*) &addr, addrlen);
	}
}

/* Try to write all pending replies on a given DNS server port. */
static void
server_port_flush(struct evdns_server_port *port)
{
	struct server_request *req = port->pending_replies;
	ASSERT_LOCKED(port);
	while (req) {
		int r = sendto(port->socket, req->response, (int)req->response_len, 0,
			   (struct sockaddr*) &req->addr, (ev_socklen_t)req->addrlen);
		if (r < 0) {
			int err = evutil_socket_geterror(port->socket);
			if (EVUTIL_ERR_RW_RETRIABLE(err))
				return;
			log(EVDNS_LOG_WARN, "Error %s (%d) while writing response to port; dropping", evutil_socket_error_to_string(err), err);
		}
		if (server_request_free(req)) {
			/* we released the last reference to req->port. */
			return;
		} else {
			EVUTIL_ASSERT(req != port->pending_replies);
			req = port->pending_replies;
		}
	}

	/* We have no more pending requests; stop listening for 'writeable' events. */
	(void) event_del(&port->event);
	event_assign(&port->event, port->event_base,
				 port->socket, EV_READ | EV_PERSIST,
				 server_port_ready_callback, port);

	if (event_add(&port->event, NULL) < 0) {
		log(EVDNS_LOG_WARN, "Error from libevent when adding event for DNS server.");
		/* ???? Do more? */
	}
}

/* set if we are waiting for the ability to write to this server. */
/* if waiting is true then we ask libevent for EV_WRITE events, otherwise */
/* we stop these events. */
static void
nameserver_write_waiting(struct nameserver *ns, char waiting) {
	ASSERT_LOCKED(ns->base);
	if (ns->write_waiting == waiting) return;

	ns->write_waiting = waiting;
	(void) event_del(&ns->event);
	event_assign(&ns->event, ns->base->event_base,
	    ns->socket, EV_READ | (waiting ? EV_WRITE : 0) | EV_PERSIST,
	    nameserver_ready_callback, ns);
	if (event_add(&ns->event, NULL) < 0) {
		char addrbuf[128];
		log(EVDNS_LOG_WARN, "Error from libevent when adding event for %s",
		    evutil_format_sockaddr_port_(
			    (struct sockaddr *)&ns->address,
			    addrbuf, sizeof(addrbuf)));
		/* ???? Do more? */
	}
}

/* a callback function. Called by libevent when the kernel says that */
/* a nameserver socket is ready for writing or reading */
static void
nameserver_ready_callback(evutil_socket_t fd, short events, void *arg) {
	struct nameserver *ns = (struct nameserver *) arg;
	(void)fd;

	EVDNS_LOCK(ns->base);
	if (events & EV_WRITE) {
		ns->choked = 0;
		if (!evdns_transmit(ns->base)) {
			nameserver_write_waiting(ns, 0);
		}
	}
	if (events & EV_READ) {
		nameserver_read(ns);
	}
	EVDNS_UNLOCK(ns->base);
}

/* a callback function. Called by libevent when the kernel says that */
/* a server socket is ready for writing or reading. */
static void
server_port_ready_callback(evutil_socket_t fd, short events, void *arg) {
	struct evdns_server_port *port = (struct evdns_server_port *) arg;
	(void) fd;

	EVDNS_LOCK(port);
	if (events & EV_WRITE) {
		port->choked = 0;
		server_port_flush(port);
	}
	if (events & EV_READ) {
		server_port_read(port);
	}
	EVDNS_UNLOCK(port);
}

/* This is an inefficient representation; only use it via the dnslabel_table_*
 * functions, so that is can be safely replaced with something smarter later. */
#define MAX_LABELS 128
/* Structures used to implement name compression */
struct dnslabel_entry { char *v; off_t pos; };
struct dnslabel_table {
	int n_labels; /* number of current entries */
	/* map from name to position in message */
	struct dnslabel_entry labels[MAX_LABELS];
};

/* Initialize dnslabel_table. */
static void
dnslabel_table_init(struct dnslabel_table *table)
{
	table->n_labels = 0;
}

/* Free all storage held by table, but not the table itself. */
static void
dnslabel_clear(struct dnslabel_table *table)
{
	int i;
	for (i = 0; i < table->n_labels; ++i)
		mm_free(table->labels[i].v);
	table->n_labels = 0;
}

/* return the position of the label in the current message, or -1 if the label */
/* hasn't been used yet. */
static int
dnslabel_table_get_pos(const struct dnslabel_table *table, const char *label)
{
	int i;
	for (i = 0; i < table->n_labels; ++i) {
		if (!strcmp(label, table->labels[i].v))
			return table->labels[i].pos;
	}
	return -1;
}

/* remember that we've used the label at position pos */
static int
dnslabel_table_add(struct dnslabel_table *table, const char *label, off_t pos)
{
	char *v;
	int p;
	if (table->n_labels == MAX_LABELS)
		return (-1);
	v = mm_strdup(label);
	if (v == NULL)
		return (-1);
	p = table->n_labels++;
	table->labels[p].v = v;
	table->labels[p].pos = pos;

	return (0);
}

/* Converts a string to a length-prefixed set of DNS labels, starting */
/* at buf[j]. name and buf must not overlap. name_len should be the length */
/* of name.	 table is optional, and is used for compression. */
/* */
/* Input: abc.def */
/* Output: <3>abc<3>def<0> */
/* */
/* Returns the first index after the encoded name, or negative on error. */
/*	 -1	 label was > 63 bytes */
/*	 -2	 name too long to fit in buffer. */
/* */
static off_t
dnsname_to_labels(u8 *const buf, size_t buf_len, off_t j,
				  const char *name, const size_t name_len,
				  struct dnslabel_table *table) {
	const char *end = name + name_len;
	int ref = 0;
	u16 t_;

#define APPEND16(x) do {						\
		if (j + 2 > (off_t)buf_len)				\
			goto overflow;					\
		t_ = htons(x);						\
		memcpy(buf + j, &t_, 2);				\
		j += 2;							\
	} while (0)
#define APPEND32(x) do {						\
		if (j + 4 > (off_t)buf_len)				\
			goto overflow;					\
		t32_ = htonl(x);					\
		memcpy(buf + j, &t32_, 4);				\
		j += 4;							\
	} while (0)

	if (name_len > 255) return -2;

	for (;;) {
		const char *const start = name;
		if (table && (ref = dnslabel_table_get_pos(table, name)) >= 0) {
			APPEND16(ref | 0xc000);
			return j;
		}
		name = strchr(name, '.');
		if (!name) {
			const size_t label_len = end - start;
			if (label_len > 63) return -1;
			if ((size_t)(j+label_len+1) > buf_len) return -2;
			if (table) dnslabel_table_add(table, start, j);
			buf[j++] = (ev_uint8_t)label_len;

			memcpy(buf + j, start, label_len);
			j += (int) label_len;
			break;
		} else {
			/* append length of the label. */
			const size_t label_len = name - start;
			if (label_len > 63) return -1;
			if ((size_t)(j+label_len+1) > buf_len) return -2;
			if (table) dnslabel_table_add(table, start, j);
			buf[j++] = (ev_uint8_t)label_len;

			memcpy(buf + j, start, label_len);
			j += (int) label_len;
			/* hop over the '.' */
			name++;
		}
	}

	/* the labels must be terminated by a 0. */
	/* It's possible that the name ended in a . */
	/* in which case the zero is already there */
	if (!j || buf[j-1]) buf[j++] = 0;
	return j;
 overflow:
	return (-2);
}

/* Finds the length of a dns request for a DNS name of the given */
/* length. The actual request may be smaller than the value returned */
/* here */
static size_t
evdns_request_len(const size_t name_len) {
	return 96 + /* length of the DNS standard header */
		name_len + 2 +
		4;  /* space for the resource type */
}

/* build a dns request packet into buf. buf should be at least as long */
/* as evdns_request_len told you it should be. */
/* */
/* Returns the amount of space used. Negative on error. */
static int
evdns_request_data_build(const char *const name, const size_t name_len,
    const u16 trans_id, const u16 type, const u16 class,
    u8 *const buf, size_t buf_len) {
	off_t j = 0;  /* current offset into buf */
	u16 t_;	 /* used by the macros */

	APPEND16(trans_id);
	APPEND16(0x0100);  /* standard query, recusion needed */
	APPEND16(1);  /* one question */
	APPEND16(0);  /* no answers */
	APPEND16(0);  /* no authority */
	APPEND16(0);  /* no additional */

	j = dnsname_to_labels(buf, buf_len, j, name, name_len, NULL);
	if (j < 0) {
		return (int)j;
	}

	APPEND16(type);
	APPEND16(class);

	return (int)j;
 overflow:
	return (-1);
}

/* exported function */
struct evdns_server_port *
evdns_add_server_port_with_base(struct event_base *base, evutil_socket_t socket, int flags, evdns_request_callback_fn_type cb, void *user_data)
{
	struct evdns_server_port *port;
	if (flags)
		return NULL; /* flags not yet implemented */
	if (!(port = mm_malloc(sizeof(struct evdns_server_port))))
		return NULL;
	memset(port, 0, sizeof(struct evdns_server_port));


	port->socket = socket;
	port->refcnt = 1;
	port->choked = 0;
	port->closing = 0;
	port->user_callback = cb;
	port->user_data = user_data;
	port->pending_replies = NULL;
	port->event_base = base;

	event_assign(&port->event, port->event_base,
				 port->socket, EV_READ | EV_PERSIST,
				 server_port_ready_callback, port);
	if (event_add(&port->event, NULL) < 0) {
		mm_free(port);
		return NULL;
	}
	EVTHREAD_ALLOC_LOCK(port->lock, EVTHREAD_LOCKTYPE_RECURSIVE);
	return port;
}

struct evdns_server_port *
evdns_add_server_port(evutil_socket_t socket, int flags, evdns_request_callback_fn_type cb, void *user_data)
{
	return evdns_add_server_port_with_base(NULL, socket, flags, cb, user_data);
}

/* exported function */
void
evdns_close_server_port(struct evdns_server_port *port)
{
	EVDNS_LOCK(port);
	if (--port->refcnt == 0) {
		EVDNS_UNLOCK(port);
		server_port_free(port);
	} else {
		port->closing = 1;
	}
}

/* exported function */
int
evdns_server_request_add_reply(struct evdns_server_request *req_, int section, const char *name, int type, int class, int ttl, int datalen, int is_name, const char *data)
{
	struct server_request *req = TO_SERVER_REQUEST(req_);
	struct server_reply_item **itemp, *item;
	int *countp;
	int result = -1;

	EVDNS_LOCK(req->port);
	if (req->response) /* have we already answered? */
		goto done;

	switch (section) {
	case EVDNS_ANSWER_SECTION:
		itemp = &req->answer;
		countp = &req->n_answer;
		break;
	case EVDNS_AUTHORITY_SECTION:
		itemp = &req->authority;
		countp = &req->n_authority;
		break;
	case EVDNS_ADDITIONAL_SECTION:
		itemp = &req->additional;
		countp = &req->n_additional;
		break;
	default:
		goto done;
	}
	while (*itemp) {
		itemp = &((*itemp)->next);
	}
	item = mm_malloc(sizeof(struct server_reply_item));
	if (!item)
		goto done;
	item->next = NULL;
	if (!(item->name = mm_strdup(name))) {
		mm_free(item);
		goto done;
	}
	item->type = type;
	item->dns_question_class = class;
	item->ttl = ttl;
	item->is_name = is_name != 0;
	item->datalen = 0;
	item->data = NULL;
	if (data) {
		if (item->is_name) {
			if (!(item->data = mm_strdup(data))) {
				mm_free(item->name);
				mm_free(item);
				goto done;
			}
			item->datalen = (u16)-1;
		} else {
			if (!(item->data = mm_malloc(datalen))) {
				mm_free(item->name);
				mm_free(item);
				goto done;
			}
			item->datalen = datalen;
			memcpy(item->data, data, datalen);
		}
	}

	*itemp = item;
	++(*countp);
	result = 0;
done:
	EVDNS_UNLOCK(req->port);
	return result;
}

/* exported function */
int
evdns_server_request_add_a_reply(struct evdns_server_request *req, const char *name, int n, const void *addrs, int ttl)
{
	return evdns_server_request_add_reply(
		  req, EVDNS_ANSWER_SECTION, name, TYPE_A, CLASS_INET,
		  ttl, n*4, 0, addrs);
}

/* exported function */
int
evdns_server_request_add_aaaa_reply(struct evdns_server_request *req, const char *name, int n, const void *addrs, int ttl)
{
	return evdns_server_request_add_reply(
		  req, EVDNS_ANSWER_SECTION, name, TYPE_AAAA, CLASS_INET,
		  ttl, n*16, 0, addrs);
}

/* exported function */
int
evdns_server_request_add_ptr_reply(struct evdns_server_request *req, struct in_addr *in, const char *inaddr_name, const char *hostname, int ttl)
{
	u32 a;
	char buf[32];
	if (in && inaddr_name)
		return -1;
	else if (!in && !inaddr_name)
		return -1;
	if (in) {
		a = ntohl(in->s_addr);
		evutil_snprintf(buf, sizeof(buf), "%d.%d.%d.%d.in-addr.arpa",
				(int)(u8)((a	)&0xff),
				(int)(u8)((a>>8 )&0xff),
				(int)(u8)((a>>16)&0xff),
				(int)(u8)((a>>24)&0xff));
		inaddr_name = buf;
	}
	return evdns_server_request_add_reply(
		  req, EVDNS_ANSWER_SECTION, inaddr_name, TYPE_PTR, CLASS_INET,
		  ttl, -1, 1, hostname);
}

/* exported function */
int
evdns_server_request_add_cname_reply(struct evdns_server_request *req, const char *name, const char *cname, int ttl)
{
	return evdns_server_request_add_reply(
		  req, EVDNS_ANSWER_SECTION, name, TYPE_CNAME, CLASS_INET,
		  ttl, -1, 1, cname);
}

/* exported function */
void
evdns_server_request_set_flags(struct evdns_server_request *exreq, int flags)
{
	struct server_request *req = TO_SERVER_REQUEST(exreq);
	req->base.flags &= ~(EVDNS_FLAGS_AA|EVDNS_FLAGS_RD);
	req->base.flags |= flags;
}

static int
evdns_server_request_format_response(struct server_request *req, int err)
{
	unsigned char buf[1500];
	size_t buf_len = sizeof(buf);
	off_t j = 0, r;
	u16 t_;
	u32 t32_;
	int i;
	u16 flags;
	struct dnslabel_table table;

	if (err < 0 || err > 15) return -1;

	/* Set response bit and error code; copy OPCODE and RD fields from
	 * question; copy RA and AA if set by caller. */
	flags = req->base.flags;
	flags |= (0x8000 | err);

	dnslabel_table_init(&table);
	APPEND16(req->trans_id);
	APPEND16(flags);
	APPEND16(req->base.nquestions);
	APPEND16(req->n_answer);
	APPEND16(req->n_authority);
	APPEND16(req->n_additional);

	/* Add questions. */
	for (i=0; i < req->base.nquestions; ++i) {
		const char *s = req->base.questions[i]->name;
		j = dnsname_to_labels(buf, buf_len, j, s, strlen(s), &table);
		if (j < 0) {
			dnslabel_clear(&table);
			return (int) j;
		}
		APPEND16(req->base.questions[i]->type);
		APPEND16(req->base.questions[i]->dns_question_class);
	}

	/* Add answer, authority, and additional sections. */
	for (i=0; i<3; ++i) {
		struct server_reply_item *item;
		if (i==0)
			item = req->answer;
		else if (i==1)
			item = req->authority;
		else
			item = req->additional;
		while (item) {
			r = dnsname_to_labels(buf, buf_len, j, item->name, strlen(item->name), &table);
			if (r < 0)
				goto overflow;
			j = r;

			APPEND16(item->type);
			APPEND16(item->dns_question_class);
			APPEND32(item->ttl);
			if (item->is_name) {
				off_t len_idx = j, name_start;
				j += 2;
				name_start = j;
				r = dnsname_to_labels(buf, buf_len, j, item->data, strlen(item->data), &table);
				if (r < 0)
					goto overflow;
				j = r;
				t_ = htons( (short) (j-name_start) );
				memcpy(buf+len_idx, &t_, 2);
			} else {
				APPEND16(item->datalen);
				if (j+item->datalen > (off_t)buf_len)
					goto overflow;
				memcpy(buf+j, item->data, item->datalen);
				j += item->datalen;
			}
			item = item->next;
		}
	}

	if (j > 512) {
overflow:
		j = 512;
		buf[2] |= 0x02; /* set the truncated bit. */
	}

	req->response_len = j;

	if (!(req->response = mm_malloc(req->response_len))) {
		server_request_free_answers(req);
		dnslabel_clear(&table);
		return (-1);
	}
	memcpy(req->response, buf, req->response_len);
	server_request_free_answers(req);
	dnslabel_clear(&table);
	return (0);
}

/* exported function */
int
evdns_server_request_respond(struct evdns_server_request *req_, int err)
{
	struct server_request *req = TO_SERVER_REQUEST(req_);
	struct evdns_server_port *port = req->port;
	int r = -1;

	EVDNS_LOCK(port);
	if (!req->response) {
		if ((r = evdns_server_request_format_response(req, err))<0)
			goto done;
	}

	r = sendto(port->socket, req->response, (int)req->response_len, 0,
			   (struct sockaddr*) &req->addr, (ev_socklen_t)req->addrlen);
	if (r<0) {
		int sock_err = evutil_socket_geterror(port->socket);
		if (EVUTIL_ERR_RW_RETRIABLE(sock_err))
			goto done;

		if (port->pending_replies) {
			req->prev_pending = port->pending_replies->prev_pending;
			req->next_pending = port->pending_replies;
			req->prev_pending->next_pending =
				req->next_pending->prev_pending = req;
		} else {
			req->prev_pending = req->next_pending = req;
			port->pending_replies = req;
			port->choked = 1;

			(void) event_del(&port->event);
			event_assign(&port->event, port->event_base, port->socket, (port->closing?0:EV_READ) | EV_WRITE | EV_PERSIST, server_port_ready_callback, port);

			if (event_add(&port->event, NULL) < 0) {
				log(EVDNS_LOG_WARN, "Error from libevent when adding event for DNS server");
			}

		}

		r = 1;
		goto done;
	}
	if (server_request_free(req)) {
		r = 0;
		goto done;
	}

	if (port->pending_replies)
		server_port_flush(port);

	r = 0;
done:
	EVDNS_UNLOCK(port);
	return r;
}

/* Free all storage held by RRs in req. */
static void
server_request_free_answers(struct server_request *req)
{
	struct server_reply_item *victim, *next, **list;
	int i;
	for (i = 0; i < 3; ++i) {
		if (i==0)
			list = &req->answer;
		else if (i==1)
			list = &req->authority;
		else
			list = &req->additional;

		victim = *list;
		while (victim) {
			next = victim->next;
			mm_free(victim->name);
			if (victim->data)
				mm_free(victim->data);
			mm_free(victim);
			victim = next;
		}
		*list = NULL;
	}
}

/* Free all storage held by req, and remove links to it. */
/* return true iff we just wound up freeing the server_port. */
static int
server_request_free(struct server_request *req)
{
	int i, rc=1, lock=0;
	if (req->base.questions) {
		for (i = 0; i < req->base.nquestions; ++i)
			mm_free(req->base.questions[i]);
		mm_free(req->base.questions);
	}

	if (req->port) {
		EVDNS_LOCK(req->port);
		lock=1;
		if (req->port->pending_replies == req) {
			if (req->next_pending && req->next_pending != req)
				req->port->pending_replies = req->next_pending;
			else
				req->port->pending_replies = NULL;
		}
		rc = --req->port->refcnt;
	}

	if (req->response) {
		mm_free(req->response);
	}

	server_request_free_answers(req);

	if (req->next_pending && req->next_pending != req) {
		req->next_pending->prev_pending = req->prev_pending;
		req->prev_pending->next_pending = req->next_pending;
	}

	if (rc == 0) {
		EVDNS_UNLOCK(req->port); /* ????? nickm */
		server_port_free(req->port);
		mm_free(req);
		return (1);
	}
	if (lock)
		EVDNS_UNLOCK(req->port);
	mm_free(req);
	return (0);
}

/* Free all storage held by an evdns_server_port.  Only called when  */
static void
server_port_free(struct evdns_server_port *port)
{
	EVUTIL_ASSERT(port);
	EVUTIL_ASSERT(!port->refcnt);
	EVUTIL_ASSERT(!port->pending_replies);
	if (port->socket > 0) {
		evutil_closesocket(port->socket);
		port->socket = -1;
	}
	(void) event_del(&port->event);
	event_debug_unassign(&port->event);
	EVTHREAD_FREE_LOCK(port->lock, EVTHREAD_LOCKTYPE_RECURSIVE);
	mm_free(port);
}

/* exported function */
int
evdns_server_request_drop(struct evdns_server_request *req_)
{
	struct server_request *req = TO_SERVER_REQUEST(req_);
	server_request_free(req);
	return 0;
}

/* exported function */
int
evdns_server_request_get_requesting_addr(struct evdns_server_request *req_, struct sockaddr *sa, int addr_len)
{
	struct server_request *req = TO_SERVER_REQUEST(req_);
	if (addr_len < (int)req->addrlen)
		return -1;
	memcpy(sa, &(req->addr), req->addrlen);
	return req->addrlen;
}

#undef APPEND16
#undef APPEND32

/* this is a libevent callback function which is called when a request */
/* has timed out. */
static void
evdns_request_timeout_callback(evutil_socket_t fd, short events, void *arg) {
	struct request *const req = (struct request *) arg;
	struct evdns_base *base = req->base;

	(void) fd;
	(void) events;

	log(EVDNS_LOG_DEBUG, "Request %p timed out", arg);
	EVDNS_LOCK(base);

	if (req->tx_count >= req->base->global_max_retransmits) {
		struct nameserver *ns = req->ns;
		/* this request has failed */
		log(EVDNS_LOG_DEBUG, "Giving up on request %p; tx_count==%d",
		    arg, req->tx_count);
		reply_schedule_callback(req, 0, DNS_ERR_TIMEOUT, NULL);

		request_finished(req, &REQ_HEAD(req->base, req->trans_id), 1);
		nameserver_failed(ns, "request timed out.");
	} else {
		/* retransmit it */
		log(EVDNS_LOG_DEBUG, "Retransmitting request %p; tx_count==%d",
		    arg, req->tx_count);
		(void) evtimer_del(&req->timeout_event);
		request_swap_ns(req, nameserver_pick(base));
		evdns_request_transmit(req);

		req->ns->timedout++;
		if (req->ns->timedout > req->base->global_max_nameserver_timeout) {
			req->ns->timedout = 0;
			nameserver_failed(req->ns, "request timed out.");
		}
	}

	EVDNS_UNLOCK(base);
}

/* try to send a request to a given server. */
/* */
/* return: */
/*   0 ok */
/*   1 temporary failure */
/*   2 other failure */
static int
evdns_request_transmit_to(struct request *req, struct nameserver *server) {
	int r;
	ASSERT_LOCKED(req->base);
	ASSERT_VALID_REQUEST(req);

	if (server->requests_inflight == 1 &&
		req->base->disable_when_inactive &&
		event_add(&server->event, NULL) < 0) {
		return 1;
	}

	r = sendto(server->socket, (void*)req->request, req->request_len, 0,
	    (struct sockaddr *)&server->address, server->addrlen);
	if (r < 0) {
		int err = evutil_socket_geterror(server->socket);
		if (EVUTIL_ERR_RW_RETRIABLE(err))
			return 1;
		nameserver_failed(req->ns, evutil_socket_error_to_string(err));
		return 2;
	} else if (r != (int)req->request_len) {
		return 1;  /* short write */
	} else {
		return 0;
	}
}

/* try to send a request, updating the fields of the request */
/* as needed */
/* */
/* return: */
/*   0 ok */
/*   1 failed */
static int
evdns_request_transmit(struct request *req) {
	int retcode = 0, r;

	ASSERT_LOCKED(req->base);
	ASSERT_VALID_REQUEST(req);
	/* if we fail to send this packet then this flag marks it */
	/* for evdns_transmit */
	req->transmit_me = 1;
	EVUTIL_ASSERT(req->trans_id != 0xffff);

	if (!req->ns)
	{
		/* unable to transmit request if no nameservers */
		return 1;
	}

	if (req->ns->choked) {
		/* don't bother trying to write to a socket */
		/* which we have had EAGAIN from */
		return 1;
	}

	r = evdns_request_transmit_to(req, req->ns);
	switch (r) {
	case 1:
		/* temp failure */
		req->ns->choked = 1;
		nameserver_write_waiting(req->ns, 1);
		return 1;
	case 2:
		/* failed to transmit the request entirely. */
		retcode = 1;
		/* fall through: we'll set a timeout, which will time out,
		 * and make us retransmit the request anyway. */
	default:
		/* all ok */
		log(EVDNS_LOG_DEBUG,
		    "Setting timeout for request %p, sent to nameserver %p", req, req->ns);
		if (evtimer_add(&req->timeout_event, &req->base->global_timeout) < 0) {
			log(EVDNS_LOG_WARN,
		      "Error from libevent when adding timer for request %p",
			    req);
			/* ???? Do more? */
		}
		req->tx_count++;
		req->transmit_me = 0;
		return retcode;
	}
}

static void
nameserver_probe_callback(int result, char type, int count, int ttl, void *addresses, void *arg) {
	struct nameserver *const ns = (struct nameserver *) arg;
	(void) type;
	(void) count;
	(void) ttl;
	(void) addresses;

	if (result == DNS_ERR_CANCEL) {
		/* We canceled this request because the nameserver came up
		 * for some other reason.  Do not change our opinion about
		 * the nameserver. */
		return;
	}

	EVDNS_LOCK(ns->base);
	ns->probe_request = NULL;
	if (result == DNS_ERR_NONE || result == DNS_ERR_NOTEXIST) {
		/* this is a good reply */
		nameserver_up(ns);
	} else {
		nameserver_probe_failed(ns);
	}
	EVDNS_UNLOCK(ns->base);
}

static void
nameserver_send_probe(struct nameserver *const ns) {
	struct evdns_request *handle;
	struct request *req;
	char addrbuf[128];
	/* here we need to send a probe to a given nameserver */
	/* in the hope that it is up now. */

	ASSERT_LOCKED(ns->base);
	log(EVDNS_LOG_DEBUG, "Sending probe to %s",
	    evutil_format_sockaddr_port_(
		    (struct sockaddr *)&ns->address,
		    addrbuf, sizeof(addrbuf)));
	handle = mm_calloc(1, sizeof(*handle));
	if (!handle) return;
	req = request_new(ns->base, handle, TYPE_A, "google.com", DNS_QUERY_NO_SEARCH, nameserver_probe_callback, ns);
	if (!req) {
		mm_free(handle);
		return;
	}
	ns->probe_request = handle;
	/* we force this into the inflight queue no matter what */
	request_trans_id_set(req, transaction_id_pick(ns->base));
	req->ns = ns;
	request_submit(req);
}

/* returns: */
/*   0 didn't try to transmit anything */
/*   1 tried to transmit something */
static int
evdns_transmit(struct evdns_base *base) {
	char did_try_to_transmit = 0;
	int i;

	ASSERT_LOCKED(base);
	for (i = 0; i < base->n_req_heads; ++i) {
		if (base->req_heads[i]) {
			struct request *const started_at = base->req_heads[i], *req = started_at;
			/* first transmit all the requests which are currently waiting */
			do {
				if (req->transmit_me) {
					did_try_to_transmit = 1;
					evdns_request_transmit(req);
				}

				req = req->next;
			} while (req != started_at);
		}
	}

	return did_try_to_transmit;
}

/* exported function */
int
evdns_base_count_nameservers(struct evdns_base *base)
{
	const struct nameserver *server;
	int n = 0;

	EVDNS_LOCK(base);
	server = base->server_head;
	if (!server)
		goto done;
	do {
		++n;
		server = server->next;
	} while (server != base->server_head);
done:
	EVDNS_UNLOCK(base);
	return n;
}

int
evdns_count_nameservers(void)
{
	return evdns_base_count_nameservers(current_base);
}

/* exported function */
int
evdns_base_clear_nameservers_and_suspend(struct evdns_base *base)
{
	struct nameserver *server, *started_at;
	int i;

	EVDNS_LOCK(base);
	server = base->server_head;
	started_at = base->server_head;
	if (!server) {
		EVDNS_UNLOCK(base);
		return 0;
	}
	while (1) {
		struct nameserver *next = server->next;
		(void) event_del(&server->event);
		if (evtimer_initialized(&server->timeout_event))
			(void) evtimer_del(&server->timeout_event);
		if (server->probe_request) {
			evdns_cancel_request(server->base, server->probe_request);
			server->probe_request = NULL;
		}
		if (server->socket >= 0)
			evutil_closesocket(server->socket);
		mm_free(server);
		if (next == started_at)
			break;
		server = next;
	}
	base->server_head = NULL;
	base->global_good_nameservers = 0;

	for (i = 0; i < base->n_req_heads; ++i) {
		struct request *req, *req_started_at;
		req = req_started_at = base->req_heads[i];
		while (req) {
			struct request *next = req->next;
			req->tx_count = req->reissue_count = 0;
			req->ns = NULL;
			/* ???? What to do about searches? */
			(void) evtimer_del(&req->timeout_event);
			req->trans_id = 0;
			req->transmit_me = 0;

			base->global_requests_waiting++;
			evdns_request_insert(req, &base->req_waiting_head);
			/* We want to insert these suspended elements at the front of
			 * the waiting queue, since they were pending before any of
			 * the waiting entries were added.  This is a circular list,
			 * so we can just shift the start back by one.*/
			base->req_waiting_head = base->req_waiting_head->prev;

			if (next == req_started_at)
				break;
			req = next;
		}
		base->req_heads[i] = NULL;
	}

	base->global_requests_inflight = 0;

	EVDNS_UNLOCK(base);
	return 0;
}

int
evdns_clear_nameservers_and_suspend(void)
{
	return evdns_base_clear_nameservers_and_suspend(current_base);
}


/* exported function */
int
evdns_base_resume(struct evdns_base *base)
{
	EVDNS_LOCK(base);
	evdns_requests_pump_waiting_queue(base);
	EVDNS_UNLOCK(base);

	return 0;
}

int
evdns_resume(void)
{
	return evdns_base_resume(current_base);
}

static int
evdns_nameserver_add_impl_(struct evdns_base *base, const struct sockaddr *address, int addrlen) {
	/* first check to see if we already have this nameserver */

	const struct nameserver *server = base->server_head, *const started_at = base->server_head;
	struct nameserver *ns;
	int err = 0;
	char addrbuf[128];

	ASSERT_LOCKED(base);
	if (server) {
		do {
			if (!evutil_sockaddr_cmp((struct sockaddr*)&server->address, address, 1)) return 3;
			server = server->next;
		} while (server != started_at);
	}
	if (addrlen > (int)sizeof(ns->address)) {
		log(EVDNS_LOG_DEBUG, "Addrlen %d too long.", (int)addrlen);
		return 2;
	}

	ns = (struct nameserver *) mm_malloc(sizeof(struct nameserver));
	if (!ns) return -1;

	memset(ns, 0, sizeof(struct nameserver));
	ns->base = base;

	evtimer_assign(&ns->timeout_event, ns->base->event_base, nameserver_prod_callback, ns);

	ns->socket = evutil_socket_(address->sa_family,
	    SOCK_DGRAM|EVUTIL_SOCK_NONBLOCK|EVUTIL_SOCK_CLOEXEC, 0);
	if (ns->socket < 0) { err = 1; goto out1; }

	if (base->global_outgoing_addrlen &&
	    !evutil_sockaddr_is_loopback_(address)) {
		if (bind(ns->socket,
			(struct sockaddr*)&base->global_outgoing_address,
			base->global_outgoing_addrlen) < 0) {
			log(EVDNS_LOG_WARN,"Couldn't bind to outgoing address");
			err = 2;
			goto out2;
		}
	}

	memcpy(&ns->address, address, addrlen);
	ns->addrlen = addrlen;
	ns->state = 1;
	event_assign(&ns->event, ns->base->event_base, ns->socket,
				 EV_READ | EV_PERSIST, nameserver_ready_callback, ns);
	if (!base->disable_when_inactive && event_add(&ns->event, NULL) < 0) {
		err = 2;
		goto out2;
	}

	log(EVDNS_LOG_DEBUG, "Added nameserver %s as %p",
	    evutil_format_sockaddr_port_(address, addrbuf, sizeof(addrbuf)), ns);

	/* insert this nameserver into the list of them */
	if (!base->server_head) {
		ns->next = ns->prev = ns;
		base->server_head = ns;
	} else {
		ns->next = base->server_head->next;
		ns->prev = base->server_head;
		base->server_head->next = ns;
		ns->next->prev = ns;
	}

	base->global_good_nameservers++;

	return 0;

out2:
	evutil_closesocket(ns->socket);
out1:
	event_debug_unassign(&ns->event);
	mm_free(ns);
	log(EVDNS_LOG_WARN, "Unable to add nameserver %s: error %d",
	    evutil_format_sockaddr_port_(address, addrbuf, sizeof(addrbuf)), err);
	return err;
}

/* exported function */
int
evdns_base_nameserver_add(struct evdns_base *base, unsigned long int address)
{
	struct sockaddr_in sin;
	int res;
	memset(&sin, 0, sizeof(sin));
	sin.sin_addr.s_addr = address;
	sin.sin_port = htons(53);
	sin.sin_family = AF_INET;
	EVDNS_LOCK(base);
	res = evdns_nameserver_add_impl_(base, (struct sockaddr*)&sin, sizeof(sin));
	EVDNS_UNLOCK(base);
	return res;
}

int
evdns_nameserver_add(unsigned long int address) {
	if (!current_base)
		current_base = evdns_base_new(NULL, 0);
	return evdns_base_nameserver_add(current_base, address);
}

static void
sockaddr_setport(struct sockaddr *sa, ev_uint16_t port)
{
	if (sa->sa_family == AF_INET) {
		((struct sockaddr_in *)sa)->sin_port = htons(port);
	} else if (sa->sa_family == AF_INET6) {
		((struct sockaddr_in6 *)sa)->sin6_port = htons(port);
	}
}

static ev_uint16_t
sockaddr_getport(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return ntohs(((struct sockaddr_in *)sa)->sin_port);
	} else if (sa->sa_family == AF_INET6) {
		return ntohs(((struct sockaddr_in6 *)sa)->sin6_port);
	} else {
		return 0;
	}
}

/* exported function */
int
evdns_base_nameserver_ip_add(struct evdns_base *base, const char *ip_as_string) {
	struct sockaddr_storage ss;
	struct sockaddr *sa;
	int len = sizeof(ss);
	int res;
	if (evutil_parse_sockaddr_port(ip_as_string, (struct sockaddr *)&ss,
		&len)) {
		log(EVDNS_LOG_WARN, "Unable to parse nameserver address %s",
			ip_as_string);
		return 4;
	}
	sa = (struct sockaddr *) &ss;
	if (sockaddr_getport(sa) == 0)
		sockaddr_setport(sa, 53);

	EVDNS_LOCK(base);
	res = evdns_nameserver_add_impl_(base, sa, len);
	EVDNS_UNLOCK(base);
	return res;
}

int
evdns_nameserver_ip_add(const char *ip_as_string) {
	if (!current_base)
		current_base = evdns_base_new(NULL, 0);
	return evdns_base_nameserver_ip_add(current_base, ip_as_string);
}

int
evdns_base_nameserver_sockaddr_add(struct evdns_base *base,
    const struct sockaddr *sa, ev_socklen_t len, unsigned flags)
{
	int res;
	EVUTIL_ASSERT(base);
	EVDNS_LOCK(base);
	res = evdns_nameserver_add_impl_(base, sa, len);
	EVDNS_UNLOCK(base);
	return res;
}

int
evdns_base_get_nameserver_addr(struct evdns_base *base, int idx,
    struct sockaddr *sa, ev_socklen_t len)
{
	int result = -1;
	int i;
	struct nameserver *server;
	EVDNS_LOCK(base);
	server = base->server_head;
	for (i = 0; i < idx && server; ++i, server = server->next) {
		if (server->next == base->server_head)
			goto done;
	}
	if (! server)
		goto done;

	if (server->addrlen > len) {
		result = (int) server->addrlen;
		goto done;
	}

	memcpy(sa, &server->address, server->addrlen);
	result = (int) server->addrlen;
done:
	EVDNS_UNLOCK(base);
	return result;
}

/* remove from the queue */
static void
evdns_request_remove(struct request *req, struct request **head)
{
	ASSERT_LOCKED(req->base);
	ASSERT_VALID_REQUEST(req);

#if 0
	{
		struct request *ptr;
		int found = 0;
		EVUTIL_ASSERT(*head != NULL);

		ptr = *head;
		do {
			if (ptr == req) {
				found = 1;
				break;
			}
			ptr = ptr->next;
		} while (ptr != *head);
		EVUTIL_ASSERT(found);

		EVUTIL_ASSERT(req->next);
	}
#endif

	if (req->next == req) {
		/* only item in the list */
		*head = NULL;
	} else {
		req->next->prev = req->prev;
		req->prev->next = req->next;
		if (*head == req) *head = req->next;
	}
	req->next = req->prev = NULL;
}

/* insert into the tail of the queue */
static void
evdns_request_insert(struct request *req, struct request **head) {
	ASSERT_LOCKED(req->base);
	ASSERT_VALID_REQUEST(req);
	if (!*head) {
		*head = req;
		req->next = req->prev = req;
		return;
	}

	req->prev = (*head)->prev;
	req->prev->next = req;
	req->next = *head;
	(*head)->prev = req;
}

static int
string_num_dots(const char *s) {
	int count = 0;
	while ((s = strchr(s, '.'))) {
		s++;
		count++;
	}
	return count;
}

static struct request *
request_new(struct evdns_base *base, struct evdns_request *handle, int type,
	    const char *name, int flags, evdns_callback_type callback,
	    void *user_ptr) {

	const char issuing_now =
	    (base->global_requests_inflight < base->global_max_requests_inflight) ? 1 : 0;

	const size_t name_len = strlen(name);
	const size_t request_max_len = evdns_request_len(name_len);
	const u16 trans_id = issuing_now ? transaction_id_pick(base) : 0xffff;
	/* the request data is alloced in a single block with the header */
	struct request *const req =
	    mm_malloc(sizeof(struct request) + request_max_len);
	int rlen;
	char namebuf[256];
	(void) flags;

	ASSERT_LOCKED(base);

	if (!req) return NULL;

	if (name_len >= sizeof(namebuf)) {
		mm_free(req);
		return NULL;
	}

	memset(req, 0, sizeof(struct request));
	req->base = base;

	evtimer_assign(&req->timeout_event, req->base->event_base, evdns_request_timeout_callback, req);

	if (base->global_randomize_case) {
		unsigned i;
		char randbits[(sizeof(namebuf)+7)/8];
		strlcpy(namebuf, name, sizeof(namebuf));
		evutil_secure_rng_get_bytes(randbits, (name_len+7)/8);
		for (i = 0; i < name_len; ++i) {
			if (EVUTIL_ISALPHA_(namebuf[i])) {
				if ((randbits[i >> 3] & (1<<(i & 7))))
					namebuf[i] |= 0x20;
				else
					namebuf[i] &= ~0x20;
			}
		}
		name = namebuf;
	}

	/* request data lives just after the header */
	req->request = ((u8 *) req) + sizeof(struct request);
	/* denotes that the request data shouldn't be free()ed */
	req->request_appended = 1;
	rlen = evdns_request_data_build(name, name_len, trans_id,
	    type, CLASS_INET, req->request, request_max_len);
	if (rlen < 0)
		goto err1;

	req->request_len = rlen;
	req->trans_id = trans_id;
	req->tx_count = 0;
	req->request_type = type;
	req->user_pointer = user_ptr;
	req->user_callback = callback;
	req->ns = issuing_now ? nameserver_pick(base) : NULL;
	req->next = req->prev = NULL;
	req->handle = handle;
	if (handle) {
		handle->current_req = req;
		handle->base = base;
	}

	return req;
err1:
	mm_free(req);
	return NULL;
}

static void
request_submit(struct request *const req) {
	struct evdns_base *base = req->base;
	ASSERT_LOCKED(base);
	ASSERT_VALID_REQUEST(req);
	if (req->ns) {
		/* if it has a nameserver assigned then this is going */
		/* straight into the inflight queue */
		evdns_request_insert(req, &REQ_HEAD(base, req->trans_id));

		base->global_requests_inflight++;
		req->ns->requests_inflight++;

		evdns_request_transmit(req);
	} else {
		evdns_request_insert(req, &base->req_waiting_head);
		base->global_requests_waiting++;
	}
}

/* exported function */
void
evdns_cancel_request(struct evdns_base *base, struct evdns_request *handle)
{
	struct request *req;

	if (!handle->current_req)
		return;

	if (!base) {
		/* This redundancy is silly; can we fix it? (Not for 2.0) XXXX */
		base = handle->base;
		if (!base)
			base = handle->current_req->base;
	}

	EVDNS_LOCK(base);
	if (handle->pending_cb) {
		EVDNS_UNLOCK(base);
		return;
	}

	req = handle->current_req;
	ASSERT_VALID_REQUEST(req);

	reply_schedule_callback(req, 0, DNS_ERR_CANCEL, NULL);
	if (req->ns) {
		/* remove from inflight queue */
		request_finished(req, &REQ_HEAD(base, req->trans_id), 1);
	} else {
		/* remove from global_waiting head */
		request_finished(req, &base->req_waiting_head, 1);
	}
	EVDNS_UNLOCK(base);
}

/* exported function */
struct evdns_request *
evdns_base_resolve_ipv4(struct evdns_base *base, const char *name, int flags,
    evdns_callback_type callback, void *ptr) {
	struct evdns_request *handle;
	struct request *req;
	log(EVDNS_LOG_DEBUG, "Resolve requested for %s", name);
	handle = mm_calloc(1, sizeof(*handle));
	if (handle == NULL)
		return NULL;
	EVDNS_LOCK(base);
	if (flags & DNS_QUERY_NO_SEARCH) {
		req =
			request_new(base, handle, TYPE_A, name, flags,
				    callback, ptr);
		if (req)
			request_submit(req);
	} else {
		search_request_new(base, handle, TYPE_A, name, flags,
		    callback, ptr);
	}
	if (handle->current_req == NULL) {
		mm_free(handle);
		handle = NULL;
	}
	EVDNS_UNLOCK(base);
	return handle;
}

int evdns_resolve_ipv4(const char *name, int flags,
					   evdns_callback_type callback, void *ptr)
{
	return evdns_base_resolve_ipv4(current_base, name, flags, callback, ptr)
		? 0 : -1;
}


/* exported function */
struct evdns_request *
evdns_base_resolve_ipv6(struct evdns_base *base,
    const char *name, int flags,
    evdns_callback_type callback, void *ptr)
{
	struct evdns_request *handle;
	struct request *req;
	log(EVDNS_LOG_DEBUG, "Resolve requested for %s", name);
	handle = mm_calloc(1, sizeof(*handle));
	if (handle == NULL)
		return NULL;
	EVDNS_LOCK(base);
	if (flags & DNS_QUERY_NO_SEARCH) {
		req = request_new(base, handle, TYPE_AAAA, name, flags,
				  callback, ptr);
		if (req)
			request_submit(req);
	} else {
		search_request_new(base, handle, TYPE_AAAA, name, flags,
		    callback, ptr);
	}
	if (handle->current_req == NULL) {
		mm_free(handle);
		handle = NULL;
	}
	EVDNS_UNLOCK(base);
	return handle;
}

int evdns_resolve_ipv6(const char *name, int flags,
    evdns_callback_type callback, void *ptr) {
	return evdns_base_resolve_ipv6(current_base, name, flags, callback, ptr)
		? 0 : -1;
}

struct evdns_request *
evdns_base_resolve_reverse(struct evdns_base *base, const struct in_addr *in, int flags, evdns_callback_type callback, void *ptr) {
	char buf[32];
	struct evdns_request *handle;
	struct request *req;
	u32 a;
	EVUTIL_ASSERT(in);
	a = ntohl(in->s_addr);
	evutil_snprintf(buf, sizeof(buf), "%d.%d.%d.%d.in-addr.arpa",
			(int)(u8)((a	)&0xff),
			(int)(u8)((a>>8 )&0xff),
			(int)(u8)((a>>16)&0xff),
			(int)(u8)((a>>24)&0xff));
	handle = mm_calloc(1, sizeof(*handle));
	if (handle == NULL)
		return NULL;
	log(EVDNS_LOG_DEBUG, "Resolve requested for %s (reverse)", buf);
	EVDNS_LOCK(base);
	req = request_new(base, handle, TYPE_PTR, buf, flags, callback, ptr);
	if (req)
		request_submit(req);
	if (handle->current_req == NULL) {
		mm_free(handle);
		handle = NULL;
	}
	EVDNS_UNLOCK(base);
	return (handle);
}

int evdns_resolve_reverse(const struct in_addr *in, int flags, evdns_callback_type callback, void *ptr) {
	return evdns_base_resolve_reverse(current_base, in, flags, callback, ptr)
		? 0 : -1;
}

struct evdns_request *
evdns_base_resolve_reverse_ipv6(struct evdns_base *base, const struct in6_addr *in, int flags, evdns_callback_type callback, void *ptr) {
	/* 32 nybbles, 32 periods, "ip6.arpa", NUL. */
	char buf[73];
	char *cp;
	struct evdns_request *handle;
	struct request *req;
	int i;
	EVUTIL_ASSERT(in);
	cp = buf;
	for (i=15; i >= 0; --i) {
		u8 byte = in->s6_addr[i];
		*cp++ = "0123456789abcdef"[byte & 0x0f];
		*cp++ = '.';
		*cp++ = "0123456789abcdef"[byte >> 4];
		*cp++ = '.';
	}
	EVUTIL_ASSERT(cp + strlen("ip6.arpa") < buf+sizeof(buf));
	memcpy(cp, "ip6.arpa", strlen("ip6.arpa")+1);
	handle = mm_calloc(1, sizeof(*handle));
	if (handle == NULL)
		return NULL;
	log(EVDNS_LOG_DEBUG, "Resolve requested for %s (reverse)", buf);
	EVDNS_LOCK(base);
	req = request_new(base, handle, TYPE_PTR, buf, flags, callback, ptr);
	if (req)
		request_submit(req);
	if (handle->current_req == NULL) {
		mm_free(handle);
		handle = NULL;
	}
	EVDNS_UNLOCK(base);
	return (handle);
}

int evdns_resolve_reverse_ipv6(const struct in6_addr *in, int flags, evdns_callback_type callback, void *ptr) {
	return evdns_base_resolve_reverse_ipv6(current_base, in, flags, callback, ptr)
		? 0 : -1;
}

/* ================================================================= */
/* Search support */
/* */
/* the libc resolver has support for searching a number of domains */
/* to find a name. If nothing else then it takes the single domain */
/* from the gethostname() call. */
/* */
/* It can also be configured via the domain and search options in a */
/* resolv.conf. */
/* */
/* The ndots option controls how many dots it takes for the resolver */
/* to decide that a name is non-local and so try a raw lookup first. */

struct search_domain {
	int len;
	struct search_domain *next;
	/* the text string is appended to this structure */
};

struct search_state {
	int refcount;
	int ndots;
	int num_domains;
	struct search_domain *head;
};

static void
search_state_decref(struct search_state *const state) {
	if (!state) return;
	state->refcount--;
	if (!state->refcount) {
		struct search_domain *next, *dom;
		for (dom = state->head; dom; dom = next) {
			next = dom->next;
			mm_free(dom);
		}
		mm_free(state);
	}
}

static struct search_state *
search_state_new(void) {
	struct search_state *state = (struct search_state *) mm_malloc(sizeof(struct search_state));
	if (!state) return NULL;
	memset(state, 0, sizeof(struct search_state));
	state->refcount = 1;
	state->ndots = 1;

	return state;
}

static void
search_postfix_clear(struct evdns_base *base) {
	search_state_decref(base->global_search_state);

	base->global_search_state = search_state_new();
}

/* exported function */
void
evdns_base_search_clear(struct evdns_base *base)
{
	EVDNS_LOCK(base);
	search_postfix_clear(base);
	EVDNS_UNLOCK(base);
}

void
evdns_search_clear(void) {
	evdns_base_search_clear(current_base);
}

static void
search_postfix_add(struct evdns_base *base, const char *domain) {
	size_t domain_len;
	struct search_domain *sdomain;
	while (domain[0] == '.') domain++;
	domain_len = strlen(domain);

	ASSERT_LOCKED(base);
	if (!base->global_search_state) base->global_search_state = search_state_new();
	if (!base->global_search_state) return;
	base->global_search_state->num_domains++;

	sdomain = (struct search_domain *) mm_malloc(sizeof(struct search_domain) + domain_len);
	if (!sdomain) return;
	memcpy( ((u8 *) sdomain) + sizeof(struct search_domain), domain, domain_len);
	sdomain->next = base->global_search_state->head;
	sdomain->len = (int) domain_len;

	base->global_search_state->head = sdomain;
}

/* reverse the order of members in the postfix list. This is needed because, */
/* when parsing resolv.conf we push elements in the wrong order */
static void
search_reverse(struct evdns_base *base) {
	struct search_domain *cur, *prev = NULL, *next;
	ASSERT_LOCKED(base);
	cur = base->global_search_state->head;
	while (cur) {
		next = cur->next;
		cur->next = prev;
		prev = cur;
		cur = next;
	}

	base->global_search_state->head = prev;
}

/* exported function */
void
evdns_base_search_add(struct evdns_base *base, const char *domain) {
	EVDNS_LOCK(base);
	search_postfix_add(base, domain);
	EVDNS_UNLOCK(base);
}
void
evdns_search_add(const char *domain) {
	evdns_base_search_add(current_base, domain);
}

/* exported function */
void
evdns_base_search_ndots_set(struct evdns_base *base, const int ndots) {
	EVDNS_LOCK(base);
	if (!base->global_search_state) base->global_search_state = search_state_new();
	if (base->global_search_state)
		base->global_search_state->ndots = ndots;
	EVDNS_UNLOCK(base);
}
void
evdns_search_ndots_set(const int ndots) {
	evdns_base_search_ndots_set(current_base, ndots);
}

static void
search_set_from_hostname(struct evdns_base *base) {
	char hostname[HOST_NAME_MAX + 1], *domainname;

	ASSERT_LOCKED(base);
	search_postfix_clear(base);
	if (gethostname(hostname, sizeof(hostname))) return;
	domainname = strchr(hostname, '.');
	if (!domainname) return;
	search_postfix_add(base, domainname);
}

/* warning: returns malloced string */
static char *
search_make_new(const struct search_state *const state, int n, const char *const base_name) {
	const size_t base_len = strlen(base_name);
	const char need_to_append_dot = base_name[base_len - 1] == '.' ? 0 : 1;
	struct search_domain *dom;

	for (dom = state->head; dom; dom = dom->next) {
		if (!n--) {
			/* this is the postfix we want */
			/* the actual postfix string is kept at the end of the structure */
			const u8 *const postfix = ((u8 *) dom) + sizeof(struct search_domain);
			const int postfix_len = dom->len;
			char *const newname = (char *) mm_malloc(base_len + need_to_append_dot + postfix_len + 1);
			if (!newname) return NULL;
			memcpy(newname, base_name, base_len);
			if (need_to_append_dot) newname[base_len] = '.';
			memcpy(newname + base_len + need_to_append_dot, postfix, postfix_len);
			newname[base_len + need_to_append_dot + postfix_len] = 0;
			return newname;
		}
	}

	/* we ran off the end of the list and still didn't find the requested string */
	EVUTIL_ASSERT(0);
	return NULL; /* unreachable; stops warnings in some compilers. */
}

static struct request *
search_request_new(struct evdns_base *base, struct evdns_request *handle,
		   int type, const char *const name, int flags,
		   evdns_callback_type user_callback, void *user_arg) {
	ASSERT_LOCKED(base);
	EVUTIL_ASSERT(type == TYPE_A || type == TYPE_AAAA);
	EVUTIL_ASSERT(handle->current_req == NULL);
	if ( ((flags & DNS_QUERY_NO_SEARCH) == 0) &&
	     base->global_search_state &&
		 base->global_search_state->num_domains) {
		/* we have some domains to search */
		struct request *req;
		if (string_num_dots(name) >= base->global_search_state->ndots) {
			req = request_new(base, handle, type, name, flags, user_callback, user_arg);
			if (!req) return NULL;
			handle->search_index = -1;
		} else {
			char *const new_name = search_make_new(base->global_search_state, 0, name);
			if (!new_name) return NULL;
			req = request_new(base, handle, type, new_name, flags, user_callback, user_arg);
			mm_free(new_name);
			if (!req) return NULL;
			handle->search_index = 0;
		}
		EVUTIL_ASSERT(handle->search_origname == NULL);
		handle->search_origname = mm_strdup(name);
		if (handle->search_origname == NULL) {
			/* XXX Should we dealloc req? If yes, how? */
			if (req)
				mm_free(req);
			return NULL;
		}
		handle->search_state = base->global_search_state;
		handle->search_flags = flags;
		base->global_search_state->refcount++;
		request_submit(req);
		return req;
	} else {
		struct request *const req = request_new(base, handle, type, name, flags, user_callback, user_arg);
		if (!req) return NULL;
		request_submit(req);
		return req;
	}
}

/* this is called when a request has failed to find a name. We need to check */
/* if it is part of a search and, if so, try the next name in the list */
/* returns: */
/*   0 another request has been submitted */
/*   1 no more requests needed */
static int
search_try_next(struct evdns_request *const handle) {
	struct request *req = handle->current_req;
	struct evdns_base *base = req->base;
	struct request *newreq;
	ASSERT_LOCKED(base);
	if (handle->search_state) {
		/* it is part of a search */
		char *new_name;
		handle->search_index++;
		if (handle->search_index >= handle->search_state->num_domains) {
			/* no more postfixes to try, however we may need to try */
			/* this name without a postfix */
			if (string_num_dots(handle->search_origname) < handle->search_state->ndots) {
				/* yep, we need to try it raw */
				newreq = request_new(base, NULL, req->request_type, handle->search_origname, handle->search_flags, req->user_callback, req->user_pointer);
				log(EVDNS_LOG_DEBUG, "Search: trying raw query %s", handle->search_origname);
				if (newreq) {
					search_request_finished(handle);
					goto submit_next;
				}
			}
			return 1;
		}

		new_name = search_make_new(handle->search_state, handle->search_index, handle->search_origname);
		if (!new_name) return 1;
		log(EVDNS_LOG_DEBUG, "Search: now trying %s (%d)", new_name, handle->search_index);
		newreq = request_new(base, NULL, req->request_type, new_name, handle->search_flags, req->user_callback, req->user_pointer);
		mm_free(new_name);
		if (!newreq) return 1;
		goto submit_next;
	}
	return 1;

submit_next:
	request_finished(req, &REQ_HEAD(req->base, req->trans_id), 0);
	handle->current_req = newreq;
	newreq->handle = handle;
	request_submit(newreq);
	return 0;
}

static void
search_request_finished(struct evdns_request *const handle) {
	ASSERT_LOCKED(handle->current_req->base);
	if (handle->search_state) {
		search_state_decref(handle->search_state);
		handle->search_state = NULL;
	}
	if (handle->search_origname) {
		mm_free(handle->search_origname);
		handle->search_origname = NULL;
	}
}

/* ================================================================= */
/* Parsing resolv.conf files */

static void
evdns_resolv_set_defaults(struct evdns_base *base, int flags) {
	/* if the file isn't found then we assume a local resolver */
	ASSERT_LOCKED(base);
	if (flags & DNS_OPTION_SEARCH) search_set_from_hostname(base);
	if (flags & DNS_OPTION_NAMESERVERS) evdns_base_nameserver_ip_add(base,"127.0.0.1");
}

#ifndef EVENT__HAVE_STRTOK_R
static char *
strtok_r(char *s, const char *delim, char **state) {
	char *cp, *start;
	start = cp = s ? s : *state;
	if (!cp)
		return NULL;
	while (*cp && !strchr(delim, *cp))
		++cp;
	if (!*cp) {
		if (cp == start)
			return NULL;
		*state = NULL;
		return start;
	} else {
		*cp++ = '\0';
		*state = cp;
		return start;
	}
}
#endif

/* helper version of atoi which returns -1 on error */
static int
strtoint(const char *const str)
{
	char *endptr;
	const int r = strtol(str, &endptr, 10);
	if (*endptr) return -1;
	return r;
}

/* Parse a number of seconds into a timeval; return -1 on error. */
static int
evdns_strtotimeval(const char *const str, struct timeval *out)
{
	double d;
	char *endptr;
	d = strtod(str, &endptr);
	if (*endptr) return -1;
	if (d < 0) return -1;
	out->tv_sec = (int) d;
	out->tv_usec = (int) ((d - (int) d)*1000000);
	if (out->tv_sec == 0 && out->tv_usec < 1000) /* less than 1 msec */
		return -1;
	return 0;
}

/* helper version of atoi that returns -1 on error and clips to bounds. */
static int
strtoint_clipped(const char *const str, int min, int max)
{
	int r = strtoint(str);
	if (r == -1)
		return r;
	else if (r<min)
		return min;
	else if (r>max)
		return max;
	else
		return r;
}

static int
evdns_base_set_max_requests_inflight(struct evdns_base *base, int maxinflight)
{
	int old_n_heads = base->n_req_heads, n_heads;
	struct request **old_heads = base->req_heads, **new_heads, *req;
	int i;

	ASSERT_LOCKED(base);
	if (maxinflight < 1)
		maxinflight = 1;
	n_heads = (maxinflight+4) / 5;
	EVUTIL_ASSERT(n_heads > 0);
	new_heads = mm_calloc(n_heads, sizeof(struct request*));
	if (!new_heads)
		return (-1);
	if (old_heads) {
		for (i = 0; i < old_n_heads; ++i) {
			while (old_heads[i]) {
				req = old_heads[i];
				evdns_request_remove(req, &old_heads[i]);
				evdns_request_insert(req, &new_heads[req->trans_id % n_heads]);
			}
		}
		mm_free(old_heads);
	}
	base->req_heads = new_heads;
	base->n_req_heads = n_heads;
	base->global_max_requests_inflight = maxinflight;
	return (0);
}

/* exported function */
int
evdns_base_set_option(struct evdns_base *base,
    const char *option, const char *val)
{
	int res;
	EVDNS_LOCK(base);
	res = evdns_base_set_option_impl(base, option, val, DNS_OPTIONS_ALL);
	EVDNS_UNLOCK(base);
	return res;
}

static inline int
str_matches_option(const char *s1, const char *optionname)
{
	/* Option names are given as "option:" We accept either 'option' in
	 * s1, or 'option:randomjunk'.  The latter form is to implement the
	 * resolv.conf parser. */
	size_t optlen = strlen(optionname);
	size_t slen = strlen(s1);
	if (slen == optlen || slen == optlen - 1)
		return !strncmp(s1, optionname, slen);
	else if (slen > optlen)
		return !strncmp(s1, optionname, optlen);
	else
		return 0;
}

static int
evdns_base_set_option_impl(struct evdns_base *base,
    const char *option, const char *val, int flags)
{
	ASSERT_LOCKED(base);
	if (str_matches_option(option, "ndots:")) {
		const int ndots = strtoint(val);
		if (ndots == -1) return -1;
		if (!(flags & DNS_OPTION_SEARCH)) return 0;
		log(EVDNS_LOG_DEBUG, "Setting ndots to %d", ndots);
		if (!base->global_search_state) base->global_search_state = search_state_new();
		if (!base->global_search_state) return -1;
		base->global_search_state->ndots = ndots;
	} else if (str_matches_option(option, "timeout:")) {
		struct timeval tv;
		if (evdns_strtotimeval(val, &tv) == -1) return -1;
		if (!(flags & DNS_OPTION_MISC)) return 0;
		log(EVDNS_LOG_DEBUG, "Setting timeout to %s", val);
		memcpy(&base->global_timeout, &tv, sizeof(struct timeval));
	} else if (str_matches_option(option, "getaddrinfo-allow-skew:")) {
		struct timeval tv;
		if (evdns_strtotimeval(val, &tv) == -1) return -1;
		if (!(flags & DNS_OPTION_MISC)) return 0;
		log(EVDNS_LOG_DEBUG, "Setting getaddrinfo-allow-skew to %s",
		    val);
		memcpy(&base->global_getaddrinfo_allow_skew, &tv,
		    sizeof(struct timeval));
	} else if (str_matches_option(option, "max-timeouts:")) {
		const int maxtimeout = strtoint_clipped(val, 1, 255);
		if (maxtimeout == -1) return -1;
		if (!(flags & DNS_OPTION_MISC)) return 0;
		log(EVDNS_LOG_DEBUG, "Setting maximum allowed timeouts to %d",
			maxtimeout);
		base->global_max_nameserver_timeout = maxtimeout;
	} else if (str_matches_option(option, "max-inflight:")) {
		const int maxinflight = strtoint_clipped(val, 1, 65000);
		if (maxinflight == -1) return -1;
		if (!(flags & DNS_OPTION_MISC)) return 0;
		log(EVDNS_LOG_DEBUG, "Setting maximum inflight requests to %d",
			maxinflight);
		evdns_base_set_max_requests_inflight(base, maxinflight);
	} else if (str_matches_option(option, "attempts:")) {
		int retries = strtoint(val);
		if (retries == -1) return -1;
		if (retries > 255) retries = 255;
		if (!(flags & DNS_OPTION_MISC)) return 0;
		log(EVDNS_LOG_DEBUG, "Setting retries to %d", retries);
		base->global_max_retransmits = retries;
	} else if (str_matches_option(option, "randomize-case:")) {
		int randcase = strtoint(val);
		if (!(flags & DNS_OPTION_MISC)) return 0;
		base->global_randomize_case = randcase;
	} else if (str_matches_option(option, "bind-to:")) {
		/* XXX This only applies to successive nameservers, not
		 * to already-configured ones.	We might want to fix that. */
		int len = sizeof(base->global_outgoing_address);
		if (!(flags & DNS_OPTION_NAMESERVERS)) return 0;
		if (evutil_parse_sockaddr_port(val,
			(struct sockaddr*)&base->global_outgoing_address, &len))
			return -1;
		base->global_outgoing_addrlen = len;
	} else if (str_matches_option(option, "initial-probe-timeout:")) {
		struct timeval tv;
		if (evdns_strtotimeval(val, &tv) == -1) return -1;
		if (tv.tv_sec > 3600)
			tv.tv_sec = 3600;
		if (!(flags & DNS_OPTION_MISC)) return 0;
		log(EVDNS_LOG_DEBUG, "Setting initial probe timeout to %s",
		    val);
		memcpy(&base->global_nameserver_probe_initial_timeout, &tv,
		    sizeof(tv));
	}
	return 0;
}

int
evdns_set_option(const char *option, const char *val, int flags)
{
	if (!current_base)
		current_base = evdns_base_new(NULL, 0);
	return evdns_base_set_option(current_base, option, val);
}

static void
resolv_conf_parse_line(struct evdns_base *base, char *const start, int flags) {
	char *strtok_state;
	static const char *const delims = " \t";
#define NEXT_TOKEN strtok_r(NULL, delims, &strtok_state)


	char *const first_token = strtok_r(start, delims, &strtok_state);
	ASSERT_LOCKED(base);
	if (!first_token) return;

	if (!strcmp(first_token, "nameserver") && (flags & DNS_OPTION_NAMESERVERS)) {
		const char *const nameserver = NEXT_TOKEN;

		if (nameserver)
			evdns_base_nameserver_ip_add(base, nameserver);
	} else if (!strcmp(first_token, "domain") && (flags & DNS_OPTION_SEARCH)) {
		const char *const domain = NEXT_TOKEN;
		if (domain) {
			search_postfix_clear(base);
			search_postfix_add(base, domain);
		}
	} else if (!strcmp(first_token, "search") && (flags & DNS_OPTION_SEARCH)) {
		const char *domain;
		search_postfix_clear(base);

		while ((domain = NEXT_TOKEN)) {
			search_postfix_add(base, domain);
		}
		search_reverse(base);
	} else if (!strcmp(first_token, "options")) {
		const char *option;
		while ((option = NEXT_TOKEN)) {
			const char *val = strchr(option, ':');
			evdns_base_set_option_impl(base, option, val ? val+1 : "", flags);
		}
	}
#undef NEXT_TOKEN
}

/* exported function */
/* returns: */
/*   0 no errors */
/*   1 failed to open file */
/*   2 failed to stat file */
/*   3 file too large */
/*   4 out of memory */
/*   5 short read from file */
int
evdns_base_resolv_conf_parse(struct evdns_base *base, int flags, const char *const filename) {
	int res;
	EVDNS_LOCK(base);
	res = evdns_base_resolv_conf_parse_impl(base, flags, filename);
	EVDNS_UNLOCK(base);
	return res;
}

static char *
evdns_get_default_hosts_filename(void)
{
#ifdef _WIN32
	/* Windows is a little coy about where it puts its configuration
	 * files.  Sure, they're _usually_ in C:\windows\system32, but
	 * there's no reason in principle they couldn't be in
	 * W:\hoboken chicken emergency\
	 */
	char path[MAX_PATH+1];
	static const char hostfile[] = "\\drivers\\etc\\hosts";
	char *path_out;
	size_t len_out;

	if (! SHGetSpecialFolderPathA(NULL, path, CSIDL_SYSTEM, 0))
		return NULL;
	len_out = strlen(path)+strlen(hostfile)+1;
	path_out = mm_malloc(len_out);
	evutil_snprintf(path_out, len_out, "%s%s", path, hostfile);
	return path_out;
#else
	return mm_strdup("/etc/hosts");
#endif
}

static int
evdns_base_resolv_conf_parse_impl(struct evdns_base *base, int flags, const char *const filename) {
	size_t n;
	char *resolv;
	char *start;
	int err = 0;

	log(EVDNS_LOG_DEBUG, "Parsing resolv.conf file %s", filename);

	if (flags & DNS_OPTION_HOSTSFILE) {
		char *fname = evdns_get_default_hosts_filename();
		evdns_base_load_hosts(base, fname);
		if (fname)
			mm_free(fname);
	}

	if ((err = evutil_read_file_(filename, &resolv, &n, 0)) < 0) {
		if (err == -1) {
			/* No file. */
			evdns_resolv_set_defaults(base, flags);
			return 1;
		} else {
			return 2;
		}
	}

	start = resolv;
	for (;;) {
		char *const newline = strchr(start, '\n');
		if (!newline) {
			resolv_conf_parse_line(base, start, flags);
			break;
		} else {
			*newline = 0;
			resolv_conf_parse_line(base, start, flags);
			start = newline + 1;
		}
	}

	if (!base->server_head && (flags & DNS_OPTION_NAMESERVERS)) {
		/* no nameservers were configured. */
		evdns_base_nameserver_ip_add(base, "127.0.0.1");
		err = 6;
	}
	if (flags & DNS_OPTION_SEARCH && (!base->global_search_state || base->global_search_state->num_domains == 0)) {
		search_set_from_hostname(base);
	}

	mm_free(resolv);
	return err;
}

int
evdns_resolv_conf_parse(int flags, const char *const filename) {
	if (!current_base)
		current_base = evdns_base_new(NULL, 0);
	return evdns_base_resolv_conf_parse(current_base, flags, filename);
}


#ifdef _WIN32
/* Add multiple nameservers from a space-or-comma-separated list. */
static int
evdns_nameserver_ip_add_line(struct evdns_base *base, const char *ips) {
	const char *addr;
	char *buf;
	int r;
	ASSERT_LOCKED(base);
	while (*ips) {
		while (isspace(*ips) || *ips == ',' || *ips == '\t')
			++ips;
		addr = ips;
		while (isdigit(*ips) || *ips == '.' || *ips == ':' ||
		    *ips=='[' || *ips==']')
			++ips;
		buf = mm_malloc(ips-addr+1);
		if (!buf) return 4;
		memcpy(buf, addr, ips-addr);
		buf[ips-addr] = '\0';
		r = evdns_base_nameserver_ip_add(base, buf);
		mm_free(buf);
		if (r) return r;
	}
	return 0;
}

typedef DWORD(WINAPI *GetNetworkParams_fn_t)(FIXED_INFO *, DWORD*);

/* Use the windows GetNetworkParams interface in iphlpapi.dll to */
/* figure out what our nameservers are. */
static int
load_nameservers_with_getnetworkparams(struct evdns_base *base)
{
	/* Based on MSDN examples and inspection of  c-ares code. */
	FIXED_INFO *fixed;
	HMODULE handle = 0;
	ULONG size = sizeof(FIXED_INFO);
	void *buf = NULL;
	int status = 0, r, added_any;
	IP_ADDR_STRING *ns;
	GetNetworkParams_fn_t fn;

	ASSERT_LOCKED(base);
	if (!(handle = evutil_load_windows_system_library_(
			TEXT("iphlpapi.dll")))) {
		log(EVDNS_LOG_WARN, "Could not open iphlpapi.dll");
		status = -1;
		goto done;
	}
	if (!(fn = (GetNetworkParams_fn_t) GetProcAddress(handle, "GetNetworkParams"))) {
		log(EVDNS_LOG_WARN, "Could not get address of function.");
		status = -1;
		goto done;
	}

	buf = mm_malloc(size);
	if (!buf) { status = 4; goto done; }
	fixed = buf;
	r = fn(fixed, &size);
	if (r != ERROR_SUCCESS && r != ERROR_BUFFER_OVERFLOW) {
		status = -1;
		goto done;
	}
	if (r != ERROR_SUCCESS) {
		mm_free(buf);
		buf = mm_malloc(size);
		if (!buf) { status = 4; goto done; }
		fixed = buf;
		r = fn(fixed, &size);
		if (r != ERROR_SUCCESS) {
			log(EVDNS_LOG_DEBUG, "fn() failed.");
			status = -1;
			goto done;
		}
	}

	EVUTIL_ASSERT(fixed);
	added_any = 0;
	ns = &(fixed->DnsServerList);
	while (ns) {
		r = evdns_nameserver_ip_add_line(base, ns->IpAddress.String);
		if (r) {
			log(EVDNS_LOG_DEBUG,"Could not add nameserver %s to list,error: %d",
				(ns->IpAddress.String),(int)GetLastError());
			status = r;
		} else {
			++added_any;
			log(EVDNS_LOG_DEBUG,"Successfully added %s as nameserver",ns->IpAddress.String);
		}

		ns = ns->Next;
	}

	if (!added_any) {
		log(EVDNS_LOG_DEBUG, "No nameservers added.");
		if (status == 0)
			status = -1;
	} else {
		status = 0;
	}

 done:
	if (buf)
		mm_free(buf);
	if (handle)
		FreeLibrary(handle);
	return status;
}

static int
config_nameserver_from_reg_key(struct evdns_base *base, HKEY key, const TCHAR *subkey)
{
	char *buf;
	DWORD bufsz = 0, type = 0;
	int status = 0;

	ASSERT_LOCKED(base);
	if (RegQueryValueEx(key, subkey, 0, &type, NULL, &bufsz)
	    != ERROR_MORE_DATA)
		return -1;
	if (!(buf = mm_malloc(bufsz)))
		return -1;

	if (RegQueryValueEx(key, subkey, 0, &type, (LPBYTE)buf, &bufsz)
	    == ERROR_SUCCESS && bufsz > 1) {
		status = evdns_nameserver_ip_add_line(base,buf);
	}

	mm_free(buf);
	return status;
}

#define SERVICES_KEY TEXT("System\\CurrentControlSet\\Services\\")
#define WIN_NS_9X_KEY  SERVICES_KEY TEXT("VxD\\MSTCP")
#define WIN_NS_NT_KEY  SERVICES_KEY TEXT("Tcpip\\Parameters")

static int
load_nameservers_from_registry(struct evdns_base *base)
{
	int found = 0;
	int r;
#define TRY(k, name) \
	if (!found && config_nameserver_from_reg_key(base,k,TEXT(name)) == 0) { \
		log(EVDNS_LOG_DEBUG,"Found nameservers in %s/%s",#k,name); \
		found = 1;						\
	} else if (!found) {						\
		log(EVDNS_LOG_DEBUG,"Didn't find nameservers in %s/%s", \
		    #k,#name);						\
	}

	ASSERT_LOCKED(base);

	if (((int)GetVersion()) > 0) { /* NT */
		HKEY nt_key = 0, interfaces_key = 0;

		if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, WIN_NS_NT_KEY, 0,
				 KEY_READ, &nt_key) != ERROR_SUCCESS) {
			log(EVDNS_LOG_DEBUG,"Couldn't open nt key, %d",(int)GetLastError());
			return -1;
		}
		r = RegOpenKeyEx(nt_key, TEXT("Interfaces"), 0,
			     KEY_QUERY_VALUE|KEY_ENUMERATE_SUB_KEYS,
			     &interfaces_key);
		if (r != ERROR_SUCCESS) {
			log(EVDNS_LOG_DEBUG,"Couldn't open interfaces key, %d",(int)GetLastError());
			return -1;
		}
		TRY(nt_key, "NameServer");
		TRY(nt_key, "DhcpNameServer");
		TRY(interfaces_key, "NameServer");
		TRY(interfaces_key, "DhcpNameServer");
		RegCloseKey(interfaces_key);
		RegCloseKey(nt_key);
	} else {
		HKEY win_key = 0;
		if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, WIN_NS_9X_KEY, 0,
				 KEY_READ, &win_key) != ERROR_SUCCESS) {
			log(EVDNS_LOG_DEBUG, "Couldn't open registry key, %d", (int)GetLastError());
			return -1;
		}
		TRY(win_key, "NameServer");
		RegCloseKey(win_key);
	}

	if (found == 0) {
		log(EVDNS_LOG_WARN,"Didn't find any nameservers.");
	}

	return found ? 0 : -1;
#undef TRY
}

int
evdns_base_config_windows_nameservers(struct evdns_base *base)
{
	int r;
	char *fname;
	if (base == NULL)
		base = current_base;
	if (base == NULL)
		return -1;
	EVDNS_LOCK(base);
	fname = evdns_get_default_hosts_filename();
	log(EVDNS_LOG_DEBUG, "Loading hosts entries from %s", fname);
	evdns_base_load_hosts(base, fname);
	if (fname)
		mm_free(fname);

	if (load_nameservers_with_getnetworkparams(base) == 0) {
		EVDNS_UNLOCK(base);
		return 0;
	}
	r = load_nameservers_from_registry(base);

	EVDNS_UNLOCK(base);
	return r;
}

int
evdns_config_windows_nameservers(void)
{
	if (!current_base) {
		current_base = evdns_base_new(NULL, 1);
		return current_base == NULL ? -1 : 0;
	} else {
		return evdns_base_config_windows_nameservers(current_base);
	}
}
#endif

struct evdns_base *
evdns_base_new(struct event_base *event_base, int flags)
{
	struct evdns_base *base;

	if (evutil_secure_rng_init() < 0) {
		log(EVDNS_LOG_WARN, "Unable to seed random number generator; "
		    "DNS can't run.");
		return NULL;
	}

	/* Give the evutil library a hook into its evdns-enabled
	 * functionality.  We can't just call evdns_getaddrinfo directly or
	 * else libevent-core will depend on libevent-extras. */
	evutil_set_evdns_getaddrinfo_fn_(evdns_getaddrinfo);

	base = mm_malloc(sizeof(struct evdns_base));
	if (base == NULL)
		return (NULL);
	memset(base, 0, sizeof(struct evdns_base));
	base->req_waiting_head = NULL;

	EVTHREAD_ALLOC_LOCK(base->lock, EVTHREAD_LOCKTYPE_RECURSIVE);
	EVDNS_LOCK(base);

	/* Set max requests inflight and allocate req_heads. */
	base->req_heads = NULL;

	evdns_base_set_max_requests_inflight(base, 64);

	base->server_head = NULL;
	base->event_base = event_base;
	base->global_good_nameservers = base->global_requests_inflight =
		base->global_requests_waiting = 0;

	base->global_timeout.tv_sec = 5;
	base->global_timeout.tv_usec = 0;
	base->global_max_reissues = 1;
	base->global_max_retransmits = 3;
	base->global_max_nameserver_timeout = 3;
	base->global_search_state = NULL;
	base->global_randomize_case = 1;
	base->global_getaddrinfo_allow_skew.tv_sec = 3;
	base->global_getaddrinfo_allow_skew.tv_usec = 0;
	base->global_nameserver_probe_initial_timeout.tv_sec = 10;
	base->global_nameserver_probe_initial_timeout.tv_usec = 0;

	TAILQ_INIT(&base->hostsdb);

#define EVDNS_BASE_ALL_FLAGS (0x8001)
	if (flags & ~EVDNS_BASE_ALL_FLAGS) {
		flags = EVDNS_BASE_INITIALIZE_NAMESERVERS;
		log(EVDNS_LOG_WARN,
		    "Unrecognized flag passed to evdns_base_new(). Assuming "
		    "you meant EVDNS_BASE_INITIALIZE_NAMESERVERS.");
	}
#undef EVDNS_BASE_ALL_FLAGS

	if (flags & EVDNS_BASE_INITIALIZE_NAMESERVERS) {
		int r;
#ifdef _WIN32
		r = evdns_base_config_windows_nameservers(base);
#else
		r = evdns_base_resolv_conf_parse(base, DNS_OPTIONS_ALL, "/etc/resolv.conf");
#endif
		if (r == -1) {
			evdns_base_free_and_unlock(base, 0);
			return NULL;
		}
	}
	if (flags & EVDNS_BASE_DISABLE_WHEN_INACTIVE) {
		base->disable_when_inactive = 1;
	}

	EVDNS_UNLOCK(base);
	return base;
}

int
evdns_init(void)
{
	struct evdns_base *base = evdns_base_new(NULL, 1);
	if (base) {
		current_base = base;
		return 0;
	} else {
		return -1;
	}
}

const char *
evdns_err_to_string(int err)
{
    switch (err) {
	case DNS_ERR_NONE: return "no error";
	case DNS_ERR_FORMAT: return "misformatted query";
	case DNS_ERR_SERVERFAILED: return "server failed";
	case DNS_ERR_NOTEXIST: return "name does not exist";
	case DNS_ERR_NOTIMPL: return "query not implemented";
	case DNS_ERR_REFUSED: return "refused";

	case DNS_ERR_TRUNCATED: return "reply truncated or ill-formed";
	case DNS_ERR_UNKNOWN: return "unknown";
	case DNS_ERR_TIMEOUT: return "request timed out";
	case DNS_ERR_SHUTDOWN: return "dns subsystem shut down";
	case DNS_ERR_CANCEL: return "dns request canceled";
	case DNS_ERR_NODATA: return "no records in the reply";
	default: return "[Unknown error code]";
    }
}

static void
evdns_nameserver_free(struct nameserver *server)
{
	if (server->socket >= 0)
	evutil_closesocket(server->socket);
	(void) event_del(&server->event);
	event_debug_unassign(&server->event);
	if (server->state == 0)
		(void) event_del(&server->timeout_event);
	if (server->probe_request) {
		evdns_cancel_request(server->base, server->probe_request);
		server->probe_request = NULL;
	}
	event_debug_unassign(&server->timeout_event);
	mm_free(server);
}

static void
evdns_base_free_and_unlock(struct evdns_base *base, int fail_requests)
{
	struct nameserver *server, *server_next;
	struct search_domain *dom, *dom_next;
	int i;

	/* Requires that we hold the lock. */

	/* TODO(nickm) we might need to refcount here. */

	for (server = base->server_head; server; server = server_next) {
		server_next = server->next;
		evdns_nameserver_free(server);
		if (server_next == base->server_head)
			break;
	}
	base->server_head = NULL;
	base->global_good_nameservers = 0;

	for (i = 0; i < base->n_req_heads; ++i) {
		while (base->req_heads[i]) {
			if (fail_requests)
				reply_schedule_callback(base->req_heads[i], 0, DNS_ERR_SHUTDOWN, NULL);
			request_finished(base->req_heads[i], &REQ_HEAD(base, base->req_heads[i]->trans_id), 1);
		}
	}
	while (base->req_waiting_head) {
		if (fail_requests)
			reply_schedule_callback(base->req_waiting_head, 0, DNS_ERR_SHUTDOWN, NULL);
		request_finished(base->req_waiting_head, &base->req_waiting_head, 1);
	}
	base->global_requests_inflight = base->global_requests_waiting = 0;


	if (base->global_search_state) {
		for (dom = base->global_search_state->head; dom; dom = dom_next) {
			dom_next = dom->next;
			mm_free(dom);
		}
		mm_free(base->global_search_state);
		base->global_search_state = NULL;
	}

	{
		struct hosts_entry *victim;
		while ((victim = TAILQ_FIRST(&base->hostsdb))) {
			TAILQ_REMOVE(&base->hostsdb, victim, next);
			mm_free(victim);
		}
	}

	mm_free(base->req_heads);

	EVDNS_UNLOCK(base);
	EVTHREAD_FREE_LOCK(base->lock, EVTHREAD_LOCKTYPE_RECURSIVE);

	mm_free(base);
}

void
evdns_base_free(struct evdns_base *base, int fail_requests)
{
	EVDNS_LOCK(base);
	evdns_base_free_and_unlock(base, fail_requests);
}

void
evdns_base_clear_host_addresses(struct evdns_base *base)
{
	struct hosts_entry *victim;
	EVDNS_LOCK(base);
	while ((victim = TAILQ_FIRST(&base->hostsdb))) {
		TAILQ_REMOVE(&base->hostsdb, victim, next);
		mm_free(victim);
	}
	EVDNS_UNLOCK(base);
}

void
evdns_shutdown(int fail_requests)
{
	if (current_base) {
		struct evdns_base *b = current_base;
		current_base = NULL;
		evdns_base_free(b, fail_requests);
	}
	evdns_log_fn = NULL;
}

static int
evdns_base_parse_hosts_line(struct evdns_base *base, char *line)
{
	char *strtok_state;
	static const char *const delims = " \t";
	char *const addr = strtok_r(line, delims, &strtok_state);
	char *hostname, *hash;
	struct sockaddr_storage ss;
	int socklen = sizeof(ss);
	ASSERT_LOCKED(base);

#define NEXT_TOKEN strtok_r(NULL, delims, &strtok_state)

	if (!addr || *addr == '#')
		return 0;

	memset(&ss, 0, sizeof(ss));
	if (evutil_parse_sockaddr_port(addr, (struct sockaddr*)&ss, &socklen)<0)
		return -1;
	if (socklen > (int)sizeof(struct sockaddr_in6))
		return -1;

	if (sockaddr_getport((struct sockaddr*)&ss))
		return -1;

	while ((hostname = NEXT_TOKEN)) {
		struct hosts_entry *he;
		size_t namelen;
		if ((hash = strchr(hostname, '#'))) {
			if (hash == hostname)
				return 0;
			*hash = '\0';
		}

		namelen = strlen(hostname);

		he = mm_calloc(1, sizeof(struct hosts_entry)+namelen);
		if (!he)
			return -1;
		EVUTIL_ASSERT(socklen <= (int)sizeof(he->addr));
		memcpy(&he->addr, &ss, socklen);
		memcpy(he->hostname, hostname, namelen+1);
		he->addrlen = socklen;

		TAILQ_INSERT_TAIL(&base->hostsdb, he, next);

		if (hash)
			return 0;
	}

	return 0;
#undef NEXT_TOKEN
}

static int
evdns_base_load_hosts_impl(struct evdns_base *base, const char *hosts_fname)
{
	char *str=NULL, *cp, *eol;
	size_t len;
	int err=0;

	ASSERT_LOCKED(base);

	if (hosts_fname == NULL ||
	    (err = evutil_read_file_(hosts_fname, &str, &len, 0)) < 0) {
		char tmp[64];
		strlcpy(tmp, "127.0.0.1   localhost", sizeof(tmp));
		evdns_base_parse_hosts_line(base, tmp);
		strlcpy(tmp, "::1   localhost", sizeof(tmp));
		evdns_base_parse_hosts_line(base, tmp);
		return err ? -1 : 0;
	}

	/* This will break early if there is a NUL in the hosts file.
	 * Probably not a problem.*/
	cp = str;
	for (;;) {
		eol = strchr(cp, '\n');

		if (eol) {
			*eol = '\0';
			evdns_base_parse_hosts_line(base, cp);
			cp = eol+1;
		} else {
			evdns_base_parse_hosts_line(base, cp);
			break;
		}
	}

	mm_free(str);
	return 0;
}

int
evdns_base_load_hosts(struct evdns_base *base, const char *hosts_fname)
{
	int res;
	if (!base)
		base = current_base;
	EVDNS_LOCK(base);
	res = evdns_base_load_hosts_impl(base, hosts_fname);
	EVDNS_UNLOCK(base);
	return res;
}

/* A single request for a getaddrinfo, either v4 or v6. */
struct getaddrinfo_subrequest {
	struct evdns_request *r;
	ev_uint32_t type;
};

/* State data used to implement an in-progress getaddrinfo. */
struct evdns_getaddrinfo_request {
	struct evdns_base *evdns_base;
	/* Copy of the modified 'hints' data that we'll use to build
	 * answers. */
	struct evutil_addrinfo hints;
	/* The callback to invoke when we're done */
	evdns_getaddrinfo_cb user_cb;
	/* User-supplied data to give to the callback. */
	void *user_data;
	/* The port to use when building sockaddrs. */
	ev_uint16_t port;
	/* The sub_request for an A record (if any) */
	struct getaddrinfo_subrequest ipv4_request;
	/* The sub_request for an AAAA record (if any) */
	struct getaddrinfo_subrequest ipv6_request;

	/* The cname result that we were told (if any) */
	char *cname_result;

	/* If we have one request answered and one request still inflight,
	 * then this field holds the answer from the first request... */
	struct evutil_addrinfo *pending_result;
	/* And this event is a timeout that will tell us to cancel the second
	 * request if it's taking a long time. */
	struct event timeout;

	/* And this field holds the error code from the first request... */
	int pending_error;
	/* If this is set, the user canceled this request. */
	unsigned user_canceled : 1;
	/* If this is set, the user can no longer cancel this request; we're
	 * just waiting for the free. */
	unsigned request_done : 1;
};

/* Convert an evdns errors to the equivalent getaddrinfo error. */
static int
evdns_err_to_getaddrinfo_err(int e1)
{
	/* XXX Do this better! */
	if (e1 == DNS_ERR_NONE)
		return 0;
	else if (e1 == DNS_ERR_NOTEXIST)
		return EVUTIL_EAI_NONAME;
	else
		return EVUTIL_EAI_FAIL;
}

/* Return the more informative of two getaddrinfo errors. */
static int
getaddrinfo_merge_err(int e1, int e2)
{
	/* XXXX be cleverer here. */
	if (e1 == 0)
		return e2;
	else
		return e1;
}

static void
free_getaddrinfo_request(struct evdns_getaddrinfo_request *data)
{
	/* DO NOT CALL this if either of the requests is pending.  Only once
	 * both callbacks have been invoked is it safe to free the request */
	if (data->pending_result)
		evutil_freeaddrinfo(data->pending_result);
	if (data->cname_result)
		mm_free(data->cname_result);
	event_del(&data->timeout);
	mm_free(data);
	return;
}

static void
add_cname_to_reply(struct evdns_getaddrinfo_request *data,
    struct evutil_addrinfo *ai)
{
	if (data->cname_result && ai) {
		ai->ai_canonname = data->cname_result;
		data->cname_result = NULL;
	}
}

/* Callback: invoked when one request in a mixed-format A/AAAA getaddrinfo
 * request has finished, but the other one took too long to answer. Pass
 * along the answer we got, and cancel the other request.
 */
static void
evdns_getaddrinfo_timeout_cb(evutil_socket_t fd, short what, void *ptr)
{
	int v4_timedout = 0, v6_timedout = 0;
	struct evdns_getaddrinfo_request *data = ptr;

	/* Cancel any pending requests, and note which one */
	if (data->ipv4_request.r) {
		/* XXXX This does nothing if the request's callback is already
		 * running (pending_cb is set). */
		evdns_cancel_request(NULL, data->ipv4_request.r);
		v4_timedout = 1;
		EVDNS_LOCK(data->evdns_base);
		++data->evdns_base->getaddrinfo_ipv4_timeouts;
		EVDNS_UNLOCK(data->evdns_base);
	}
	if (data->ipv6_request.r) {
		/* XXXX This does nothing if the request's callback is already
		 * running (pending_cb is set). */
		evdns_cancel_request(NULL, data->ipv6_request.r);
		v6_timedout = 1;
		EVDNS_LOCK(data->evdns_base);
		++data->evdns_base->getaddrinfo_ipv6_timeouts;
		EVDNS_UNLOCK(data->evdns_base);
	}

	/* We only use this timeout callback when we have an answer for
	 * one address. */
	EVUTIL_ASSERT(!v4_timedout || !v6_timedout);

	/* Report the outcome of the other request that didn't time out. */
	if (data->pending_result) {
		add_cname_to_reply(data, data->pending_result);
		data->user_cb(0, data->pending_result, data->user_data);
		data->pending_result = NULL;
	} else {
		int e = data->pending_error;
		if (!e)
			e = EVUTIL_EAI_AGAIN;
		data->user_cb(e, NULL, data->user_data);
	}

	data->user_cb = NULL; /* prevent double-call if evdns callbacks are
			       * in-progress. XXXX It would be better if this
			       * weren't necessary. */

	if (!v4_timedout && !v6_timedout) {
		/* should be impossible? XXXX */
		free_getaddrinfo_request(data);
	}
}

static int
evdns_getaddrinfo_set_timeout(struct evdns_base *evdns_base,
    struct evdns_getaddrinfo_request *data)
{
	return event_add(&data->timeout, &evdns_base->global_getaddrinfo_allow_skew);
}

static inline int
evdns_result_is_answer(int result)
{
	return (result != DNS_ERR_NOTIMPL && result != DNS_ERR_REFUSED &&
	    result != DNS_ERR_SERVERFAILED && result != DNS_ERR_CANCEL);
}

static void
evdns_getaddrinfo_gotresolve(int result, char type, int count,
    int ttl, void *addresses, void *arg)
{
	int i;
	struct getaddrinfo_subrequest *req = arg;
	struct getaddrinfo_subrequest *other_req;
	struct evdns_getaddrinfo_request *data;

	struct evutil_addrinfo *res;

	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
	struct sockaddr *sa;
	int socklen, addrlen;
	void *addrp;
	int err;
	int user_canceled;

	EVUTIL_ASSERT(req->type == DNS_IPv4_A || req->type == DNS_IPv6_AAAA);
	if (req->type == DNS_IPv4_A) {
		data = EVUTIL_UPCAST(req, struct evdns_getaddrinfo_request, ipv4_request);
		other_req = &data->ipv6_request;
	} else {
		data = EVUTIL_UPCAST(req, struct evdns_getaddrinfo_request, ipv6_request);
		other_req = &data->ipv4_request;
	}

	EVDNS_LOCK(data->evdns_base);
	if (evdns_result_is_answer(result)) {
		if (req->type == DNS_IPv4_A)
			++data->evdns_base->getaddrinfo_ipv4_answered;
		else
			++data->evdns_base->getaddrinfo_ipv6_answered;
	}
	user_canceled = data->user_canceled;
	if (other_req->r == NULL)
		data->request_done = 1;
	EVDNS_UNLOCK(data->evdns_base);

	req->r = NULL;

	if (result == DNS_ERR_CANCEL && ! user_canceled) {
		/* Internal cancel request from timeout or internal error.
		 * we already answered the user. */
		if (other_req->r == NULL)
			free_getaddrinfo_request(data);
		return;
	}

	if (data->user_cb == NULL) {
		/* We already answered.  XXXX This shouldn't be needed; see
		 * comments in evdns_getaddrinfo_timeout_cb */
		free_getaddrinfo_request(data);
		return;
	}

	if (result == DNS_ERR_NONE) {
		if (count == 0)
			err = EVUTIL_EAI_NODATA;
		else
			err = 0;
	} else {
		err = evdns_err_to_getaddrinfo_err(result);
	}

	if (err) {
		/* Looks like we got an error. */
		if (other_req->r) {
			/* The other request is still working; maybe it will
			 * succeed. */
			/* XXXX handle failure from set_timeout */
			evdns_getaddrinfo_set_timeout(data->evdns_base, data);
			data->pending_error = err;
			return;
		}

		if (user_canceled) {
			data->user_cb(EVUTIL_EAI_CANCEL, NULL, data->user_data);
		} else if (data->pending_result) {
			/* If we have an answer waiting, and we weren't
			 * canceled, ignore this error. */
			add_cname_to_reply(data, data->pending_result);
			data->user_cb(0, data->pending_result, data->user_data);
			data->pending_result = NULL;
		} else {
			if (data->pending_error)
				err = getaddrinfo_merge_err(err,
				    data->pending_error);
			data->user_cb(err, NULL, data->user_data);
		}
		free_getaddrinfo_request(data);
		return;
	} else if (user_canceled) {
		if (other_req->r) {
			/* The other request is still working; let it hit this
			 * callback with EVUTIL_EAI_CANCEL callback and report
			 * the failure. */
			return;
		}
		data->user_cb(EVUTIL_EAI_CANCEL, NULL, data->user_data);
		free_getaddrinfo_request(data);
		return;
	}

	/* Looks like we got some answers. We should turn them into addrinfos
	 * and then either queue those or return them all. */
	EVUTIL_ASSERT(type == DNS_IPv4_A || type == DNS_IPv6_AAAA);

	if (type == DNS_IPv4_A) {
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(data->port);

		sa = (struct sockaddr *)&sin;
		socklen = sizeof(sin);
		addrlen = 4;
		addrp = &sin.sin_addr.s_addr;
	} else {
		memset(&sin6, 0, sizeof(sin6));
		sin6.sin6_family = AF_INET6;
		sin6.sin6_port = htons(data->port);

		sa = (struct sockaddr *)&sin6;
		socklen = sizeof(sin6);
		addrlen = 16;
		addrp = &sin6.sin6_addr.s6_addr;
	}

	res = NULL;
	for (i=0; i < count; ++i) {
		struct evutil_addrinfo *ai;
		memcpy(addrp, ((char*)addresses)+i*addrlen, addrlen);
		ai = evutil_new_addrinfo_(sa, socklen, &data->hints);
		if (!ai) {
			if (other_req->r) {
				evdns_cancel_request(NULL, other_req->r);
			}
			data->user_cb(EVUTIL_EAI_MEMORY, NULL, data->user_data);
			if (res)
				evutil_freeaddrinfo(res);

			if (other_req->r == NULL)
				free_getaddrinfo_request(data);
			return;
		}
		res = evutil_addrinfo_append_(res, ai);
	}

	if (other_req->r) {
		/* The other request is still in progress; wait for it */
		/* XXXX handle failure from set_timeout */
		evdns_getaddrinfo_set_timeout(data->evdns_base, data);
		data->pending_result = res;
		return;
	} else {
		/* The other request is done or never started; append its
		 * results (if any) and return them. */
		if (data->pending_result) {
			if (req->type == DNS_IPv4_A)
				res = evutil_addrinfo_append_(res,
				    data->pending_result);
			else
				res = evutil_addrinfo_append_(
				    data->pending_result, res);
			data->pending_result = NULL;
		}

		/* Call the user callback. */
		add_cname_to_reply(data, res);
		data->user_cb(0, res, data->user_data);

		/* Free data. */
		free_getaddrinfo_request(data);
	}
}

static struct hosts_entry *
find_hosts_entry(struct evdns_base *base, const char *hostname,
    struct hosts_entry *find_after)
{
	struct hosts_entry *e;

	if (find_after)
		e = TAILQ_NEXT(find_after, next);
	else
		e = TAILQ_FIRST(&base->hostsdb);

	for (; e; e = TAILQ_NEXT(e, next)) {
		if (!evutil_ascii_strcasecmp(e->hostname, hostname))
			return e;
	}
	return NULL;
}

static int
evdns_getaddrinfo_fromhosts(struct evdns_base *base,
    const char *nodename, struct evutil_addrinfo *hints, ev_uint16_t port,
    struct evutil_addrinfo **res)
{
	int n_found = 0;
	struct hosts_entry *e;
	struct evutil_addrinfo *ai=NULL;
	int f = hints->ai_family;

	EVDNS_LOCK(base);
	for (e = find_hosts_entry(base, nodename, NULL); e;
	    e = find_hosts_entry(base, nodename, e)) {
		struct evutil_addrinfo *ai_new;
		++n_found;
		if ((e->addr.sa.sa_family == AF_INET && f == PF_INET6) ||
		    (e->addr.sa.sa_family == AF_INET6 && f == PF_INET))
			continue;
		ai_new = evutil_new_addrinfo_(&e->addr.sa, e->addrlen, hints);
		if (!ai_new) {
			n_found = 0;
			goto out;
		}
		sockaddr_setport(ai_new->ai_addr, port);
		ai = evutil_addrinfo_append_(ai, ai_new);
	}
	EVDNS_UNLOCK(base);
out:
	if (n_found) {
		/* Note that we return an empty answer if we found entries for
		 * this hostname but none were of the right address type. */
		*res = ai;
		return 0;
	} else {
		if (ai)
			evutil_freeaddrinfo(ai);
		return -1;
	}
}

struct evdns_getaddrinfo_request *
evdns_getaddrinfo(struct evdns_base *dns_base,
    const char *nodename, const char *servname,
    const struct evutil_addrinfo *hints_in,
    evdns_getaddrinfo_cb cb, void *arg)
{
	struct evdns_getaddrinfo_request *data;
	struct evutil_addrinfo hints;
	struct evutil_addrinfo *res = NULL;
	int err;
	int port = 0;
	int want_cname = 0;

	if (!dns_base) {
		dns_base = current_base;
		if (!dns_base) {
			log(EVDNS_LOG_WARN,
			    "Call to getaddrinfo_async with no "
			    "evdns_base configured.");
			cb(EVUTIL_EAI_FAIL, NULL, arg); /* ??? better error? */
			return NULL;
		}
	}

	/* If we _must_ answer this immediately, do so. */
	if ((hints_in && (hints_in->ai_flags & EVUTIL_AI_NUMERICHOST))) {
		res = NULL;
		err = evutil_getaddrinfo(nodename, servname, hints_in, &res);
		cb(err, res, arg);
		return NULL;
	}

	if (hints_in) {
		memcpy(&hints, hints_in, sizeof(hints));
	} else {
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = PF_UNSPEC;
	}

	evutil_adjust_hints_for_addrconfig_(&hints);

	/* Now try to see if we _can_ answer immediately. */
	/* (It would be nice to do this by calling getaddrinfo directly, with
	 * AI_NUMERICHOST, on plaforms that have it, but we can't: there isn't
	 * a reliable way to distinguish the "that wasn't a numeric host!" case
	 * from any other EAI_NONAME cases.) */
	err = evutil_getaddrinfo_common_(nodename, servname, &hints, &res, &port);
	if (err != EVUTIL_EAI_NEED_RESOLVE) {
		cb(err, res, arg);
		return NULL;
	}

	/* If there is an entry in the hosts file, we should give it now. */
	if (!evdns_getaddrinfo_fromhosts(dns_base, nodename, &hints, port, &res)) {
		cb(0, res, arg);
		return NULL;
	}

	/* Okay, things are serious now. We're going to need to actually
	 * launch a request.
	 */
	data = mm_calloc(1,sizeof(struct evdns_getaddrinfo_request));
	if (!data) {
		cb(EVUTIL_EAI_MEMORY, NULL, arg);
		return NULL;
	}

	memcpy(&data->hints, &hints, sizeof(data->hints));
	data->port = (ev_uint16_t)port;
	data->ipv4_request.type = DNS_IPv4_A;
	data->ipv6_request.type = DNS_IPv6_AAAA;
	data->user_cb = cb;
	data->user_data = arg;
	data->evdns_base = dns_base;

	want_cname = (hints.ai_flags & EVUTIL_AI_CANONNAME);

	/* If we are asked for a PF_UNSPEC address, we launch two requests in
	 * parallel: one for an A address and one for an AAAA address.  We
	 * can't send just one request, since many servers only answer one
	 * question per DNS request.
	 *
	 * Once we have the answer to one request, we allow for a short
	 * timeout before we report it, to see if the other one arrives.  If
	 * they both show up in time, then we report both the answers.
	 *
	 * If too many addresses of one type time out or fail, we should stop
	 * launching those requests. (XXX we don't do that yet.)
	 */

	if (hints.ai_family != PF_INET6) {
		log(EVDNS_LOG_DEBUG, "Sending request for %s on ipv4 as %p",
		    nodename, &data->ipv4_request);

		data->ipv4_request.r = evdns_base_resolve_ipv4(dns_base,
		    nodename, 0, evdns_getaddrinfo_gotresolve,
		    &data->ipv4_request);
		if (want_cname && data->ipv4_request.r)
			data->ipv4_request.r->current_req->put_cname_in_ptr =
			    &data->cname_result;
	}
	if (hints.ai_family != PF_INET) {
		log(EVDNS_LOG_DEBUG, "Sending request for %s on ipv6 as %p",
		    nodename, &data->ipv6_request);

		data->ipv6_request.r = evdns_base_resolve_ipv6(dns_base,
		    nodename, 0, evdns_getaddrinfo_gotresolve,
		    &data->ipv6_request);
		if (want_cname && data->ipv6_request.r)
			data->ipv6_request.r->current_req->put_cname_in_ptr =
			    &data->cname_result;
	}

	evtimer_assign(&data->timeout, dns_base->event_base,
	    evdns_getaddrinfo_timeout_cb, data);

	if (data->ipv4_request.r || data->ipv6_request.r) {
		return data;
	} else {
		mm_free(data);
		cb(EVUTIL_EAI_FAIL, NULL, arg);
		return NULL;
	}
}

void
evdns_getaddrinfo_cancel(struct evdns_getaddrinfo_request *data)
{
	EVDNS_LOCK(data->evdns_base);
	if (data->request_done) {
		EVDNS_UNLOCK(data->evdns_base);
		return;
	}
	event_del(&data->timeout);
	data->user_canceled = 1;
	if (data->ipv4_request.r)
		evdns_cancel_request(data->evdns_base, data->ipv4_request.r);
	if (data->ipv6_request.r)
		evdns_cancel_request(data->evdns_base, data->ipv6_request.r);
	EVDNS_UNLOCK(data->evdns_base);
}
