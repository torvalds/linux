/*
 * util/ub_event.c - directly call libevent (compatability) functions
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains and implementation for the indirection layer for pluggable
 * events that transparently passes it either directly to libevent, or calls
 * the libevent compatibility layer functions.
 */
#include "config.h"
#include <sys/time.h>
#include "util/ub_event.h"
#include "util/log.h"
#include "util/netevent.h"
#include "util/tube.h"

/* We define libevent structures here to hide the libevent stuff. */

#ifdef USE_MINI_EVENT
#  ifdef USE_WINSOCK
#    include "util/winsock_event.h"
#  else
#    include "util/mini_event.h"
#  endif /* USE_WINSOCK */
#else /* USE_MINI_EVENT */
   /* we use libevent */
#  ifdef HAVE_EVENT_H
#    include <event.h>
#  else
#    include "event2/event.h"
#    include "event2/event_struct.h"
#    include "event2/event_compat.h"
#  endif
#endif /* USE_MINI_EVENT */

#if UB_EV_TIMEOUT != EV_TIMEOUT || UB_EV_READ != EV_READ || \
    UB_EV_WRITE != EV_WRITE || UB_EV_SIGNAL != EV_SIGNAL || \
    UB_EV_PERSIST != EV_PERSIST 
/* Only necessary for libev */ 
#  define NATIVE_BITS(b) ( \
	  (((b) & UB_EV_TIMEOUT) ? EV_TIMEOUT : 0) \
	| (((b) & UB_EV_READ   ) ? EV_READ    : 0) \
	| (((b) & UB_EV_WRITE  ) ? EV_WRITE   : 0) \
	| (((b) & UB_EV_SIGNAL ) ? EV_SIGNAL  : 0) \
	| (((b) & UB_EV_PERSIST) ? EV_PERSIST : 0))

#  define UB_EV_BITS(b) ( \
	  (((b) & EV_TIMEOUT) ? UB_EV_TIMEOUT : 0) \
	| (((b) & EV_READ   ) ? UB_EV_READ    : 0) \
	| (((b) & EV_WRITE  ) ? UB_EV_WRITE   : 0) \
	| (((b) & EV_SIGNAL ) ? UB_EV_SIGNAL  : 0) \
	| (((b) & EV_PERSIST) ? UB_EV_PERSIST : 0))

#  define UB_EV_BITS_CB(C) void my_ ## C (int fd, short bits, void *arg) \
	{ (C)(fd, UB_EV_BITS(bits), arg); }

UB_EV_BITS_CB(comm_point_udp_callback);
UB_EV_BITS_CB(comm_point_udp_ancil_callback)
UB_EV_BITS_CB(comm_point_tcp_accept_callback)
UB_EV_BITS_CB(comm_point_tcp_handle_callback)
UB_EV_BITS_CB(comm_timer_callback)
UB_EV_BITS_CB(comm_signal_callback)
UB_EV_BITS_CB(comm_point_local_handle_callback)
UB_EV_BITS_CB(comm_point_raw_handle_callback)
UB_EV_BITS_CB(comm_point_http_handle_callback)
UB_EV_BITS_CB(tube_handle_signal)
UB_EV_BITS_CB(comm_base_handle_slow_accept)

static void (*NATIVE_BITS_CB(void (*cb)(int, short, void*)))(int, short, void*)
{
	if(cb == comm_point_udp_callback)
		return my_comm_point_udp_callback;
	else if(cb == comm_point_udp_ancil_callback)
		return my_comm_point_udp_ancil_callback;
	else if(cb == comm_point_tcp_accept_callback)
		return my_comm_point_tcp_accept_callback;
	else if(cb == comm_point_tcp_handle_callback)
		return my_comm_point_tcp_handle_callback;
	else if(cb == comm_timer_callback)
		return my_comm_timer_callback;
	else if(cb == comm_signal_callback)
		return my_comm_signal_callback;
	else if(cb == comm_point_local_handle_callback)
		return my_comm_point_local_handle_callback;
	else if(cb == comm_point_raw_handle_callback)
		return my_comm_point_raw_handle_callback;
	else if(cb == comm_point_http_handle_callback)
		return my_comm_point_http_handle_callback;
	else if(cb == tube_handle_signal)
		return my_tube_handle_signal;
	else if(cb == comm_base_handle_slow_accept)
		return my_comm_base_handle_slow_accept;
	else {
		log_assert(0); /* this NULL callback pointer should not happen,
			we should have the necessary routine listed above */
		return NULL;
	}
}
#else 
#  define NATIVE_BITS(b) (b)
#  define NATIVE_BITS_CB(c) (c)
#endif

#ifndef EVFLAG_AUTO
#define EVFLAG_AUTO 0
#endif

#define AS_EVENT_BASE(x) ((struct event_base*)x)
#define AS_UB_EVENT_BASE(x) ((struct ub_event_base*)x)
#define AS_EVENT(x) ((struct event*)x)
#define AS_UB_EVENT(x) ((struct ub_event*)x)

const char* ub_event_get_version(void)
{
	return event_get_version();
}

#if (defined(HAVE_EV_LOOP) || defined(HAVE_EV_DEFAULT_LOOP)) && defined(EVBACKEND_SELECT)
static const char* ub_ev_backend2str(int b)
{
	switch(b) {
	case EVBACKEND_SELECT:	return "select";
	case EVBACKEND_POLL:	return "poll";
	case EVBACKEND_EPOLL:	return "epoll";
	case EVBACKEND_KQUEUE:	return "kqueue";
	case EVBACKEND_DEVPOLL: return "devpoll";
	case EVBACKEND_PORT:	return "evport";
	}
	return "unknown";
}
#endif

void
ub_get_event_sys(struct ub_event_base* base, const char** n, const char** s,
	const char** m)
{
#ifdef USE_WINSOCK
	(void)base;
	*n = "event";
	*s = "winsock";
	*m = "WSAWaitForMultipleEvents";
#elif defined(USE_MINI_EVENT)
	(void)base;
	*n = "mini-event";
	*s = "internal";
	*m = "select";
#else
	struct event_base* b = AS_EVENT_BASE(base);
	*s = event_get_version();
#  if defined(HAVE_EV_LOOP) || defined(HAVE_EV_DEFAULT_LOOP)
	*n = "libev";
	if (!b)
		b = (struct event_base*)ev_default_loop(EVFLAG_AUTO);
#    ifdef EVBACKEND_SELECT
	*m = ub_ev_backend2str(ev_backend((struct ev_loop*)b));
#    else
	*m = "not obtainable";
#    endif
#  elif defined(HAVE_EVENT_BASE_GET_METHOD)
	*n = "libevent";
	if (!b)
		b = event_base_new();
	*m = event_base_get_method(b);
#  else
	*n = "unknown";
	*m = "not obtainable";
	(void)b;
#  endif
#  ifdef HAVE_EVENT_BASE_FREE
	if (b && b != AS_EVENT_BASE(base))
		event_base_free(b);
#  endif
#endif
}

struct ub_event_base*
ub_default_event_base(int sigs, time_t* time_secs, struct timeval* time_tv)
{
	void* base;

	(void)base;
#ifdef USE_MINI_EVENT
	(void)sigs;
	/* use mini event time-sharing feature */
	base = event_init(time_secs, time_tv);
#else
	(void)time_secs;
	(void)time_tv;
#  if defined(HAVE_EV_LOOP) || defined(HAVE_EV_DEFAULT_LOOP)
	/* libev */
	if(sigs)
		base = ev_default_loop(EVFLAG_AUTO);
	else
		base = ev_loop_new(EVFLAG_AUTO);
#  else
	(void)sigs;
#    ifdef HAVE_EVENT_BASE_NEW
	base = event_base_new();
#    else
	base = event_init();
#    endif
#  endif
#endif
	return (struct ub_event_base*)base;
}

struct ub_event_base *
ub_libevent_event_base(struct event_base* libevent_base)
{
#ifdef USE_MINI_EVENT
	(void)libevent_base;
	return NULL;
#else
	return AS_UB_EVENT_BASE(libevent_base);
#endif
}

struct event_base *
ub_libevent_get_event_base(struct ub_event_base* base)
{
#ifdef USE_MINI_EVENT
	(void)base;
	return NULL;
#else
	return AS_EVENT_BASE(base);
#endif
}

void
ub_event_base_free(struct ub_event_base* base)
{
#ifdef USE_MINI_EVENT
	event_base_free(AS_EVENT_BASE(base));
#elif defined(HAVE_EVENT_BASE_FREE) && defined(HAVE_EVENT_BASE_ONCE)
	/* only libevent 1.2+ has it, but in 1.2 it is broken - 
	   assertion fails on signal handling ev that is not deleted
 	   in libevent 1.3c (event_base_once appears) this is fixed. */
	event_base_free(AS_EVENT_BASE(base));
#else
	(void)base;
#endif /* HAVE_EVENT_BASE_FREE and HAVE_EVENT_BASE_ONCE */
}

int
ub_event_base_dispatch(struct ub_event_base* base)
{
	return event_base_dispatch(AS_EVENT_BASE(base));
}

int
ub_event_base_loopexit(struct ub_event_base* base)
{
	return event_base_loopexit(AS_EVENT_BASE(base), NULL);
}

struct ub_event*
ub_event_new(struct ub_event_base* base, int fd, short bits,
	void (*cb)(int, short, void*), void* arg)
{
	struct event *ev = (struct event*)calloc(1, sizeof(struct event));

	if (!ev)
		return NULL;

	event_set(ev, fd, NATIVE_BITS(bits), NATIVE_BITS_CB(cb), arg);
	if (event_base_set(AS_EVENT_BASE(base), ev) != 0) {
		free(ev);
		return NULL;
	}
	return AS_UB_EVENT(ev);
}

struct ub_event*
ub_signal_new(struct ub_event_base* base, int fd,
	void (*cb)(int, short, void*), void* arg)
{
	struct event *ev = (struct event*)calloc(1, sizeof(struct event));

	if (!ev)
		return NULL;

	signal_set(ev, fd, NATIVE_BITS_CB(cb), arg);
	if (event_base_set(AS_EVENT_BASE(base), ev) != 0) {
		free(ev);
		return NULL;
	}
	return AS_UB_EVENT(ev);
}

struct ub_event*
ub_winsock_register_wsaevent(struct ub_event_base* base, void* wsaevent,
	void (*cb)(int, short, void*), void* arg)
{
#if defined(USE_MINI_EVENT) && defined(USE_WINSOCK)
	struct event *ev = (struct event*)calloc(1, sizeof(struct event));

	if (!ev)
		return NULL;

	if (winsock_register_wsaevent(AS_EVENT_BASE(base), ev, wsaevent, cb,
				arg))
		return AS_UB_EVENT(ev);
	free(ev);
	return NULL;
#else
	(void)base;
	(void)wsaevent;
	(void)cb;
	(void)arg;
	return NULL;
#endif
}

void
ub_event_add_bits(struct ub_event* ev, short bits)
{
	AS_EVENT(ev)->ev_events |= NATIVE_BITS(bits);
}

void
ub_event_del_bits(struct ub_event* ev, short bits)
{
	AS_EVENT(ev)->ev_events &= ~NATIVE_BITS(bits);
}

void
ub_event_set_fd(struct ub_event* ev, int fd)
{
	AS_EVENT(ev)->ev_fd = fd;
}

void
ub_event_free(struct ub_event* ev)
{
	if (ev)
		free(AS_EVENT(ev));
}

int
ub_event_add(struct ub_event* ev, struct timeval* tv)
{
	return event_add(AS_EVENT(ev), tv);
}

int
ub_event_del(struct ub_event* ev)
{
	return event_del(AS_EVENT(ev));
}

int
ub_timer_add(struct ub_event* ev, struct ub_event_base* base,
	void (*cb)(int, short, void*), void* arg, struct timeval* tv)
{
	event_set(AS_EVENT(ev), -1, EV_TIMEOUT, NATIVE_BITS_CB(cb), arg);
	if (event_base_set(AS_EVENT_BASE(base), AS_EVENT(ev)) != 0)
		return -1;
	return evtimer_add(AS_EVENT(ev), tv);
}

int
ub_timer_del(struct ub_event* ev)
{
	return evtimer_del(AS_EVENT(ev));
}

int
ub_signal_add(struct ub_event* ev, struct timeval* tv)
{
	return signal_add(AS_EVENT(ev), tv);
}

int
ub_signal_del(struct ub_event* ev)
{
	return signal_del(AS_EVENT(ev));
}

void
ub_winsock_unregister_wsaevent(struct ub_event* ev)
{
#if defined(USE_MINI_EVENT) && defined(USE_WINSOCK)
	winsock_unregister_wsaevent(AS_EVENT(ev));
	free(AS_EVENT(ev));
#else
	(void)ev;
#endif
}

void
ub_winsock_tcp_wouldblock(struct ub_event* ev, int eventbits)
{
#if defined(USE_MINI_EVENT) && defined(USE_WINSOCK)
	winsock_tcp_wouldblock(AS_EVENT(ev), NATIVE_BITS(eventbits));
#else
	(void)ev;
	(void)eventbits;
#endif
}

void ub_comm_base_now(struct comm_base* cb)
{
#ifdef USE_MINI_EVENT
/** minievent updates the time when it blocks. */
	(void)cb; /* nothing to do */
#else /* !USE_MINI_EVENT */
/** fillup the time values in the event base */
	time_t *tt;
	struct timeval *tv;
	comm_base_timept(cb, &tt, &tv);
	if(gettimeofday(tv, NULL) < 0) {
		log_err("gettimeofday: %s", strerror(errno));
	}
	*tt = tv->tv_sec;
#endif /* USE_MINI_EVENT */
}

