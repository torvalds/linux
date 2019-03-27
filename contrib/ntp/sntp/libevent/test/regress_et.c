/*
 * Copyright (c) 2009-2012 Niels Provos and Nick Mathewson
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
#include "../util-internal.h"
#include "event2/event-config.h"

#ifdef _WIN32
#include <winsock2.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#ifdef EVENT__HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <sys/time.h>
#include <unistd.h>
#endif
#include <errno.h>

#include "event2/event.h"
#include "event2/util.h"

#include "regress.h"

static int was_et = 0;

static void
read_cb(evutil_socket_t fd, short event, void *arg)
{
	char buf;
	int len;

	len = recv(fd, &buf, sizeof(buf), 0);

	called++;
	if (event & EV_ET)
		was_et = 1;

	if (!len)
		event_del(arg);
}

#ifndef SHUT_WR
#define SHUT_WR 1
#endif

#ifdef _WIN32
#define LOCAL_SOCKETPAIR_AF AF_INET
#else
#define LOCAL_SOCKETPAIR_AF AF_UNIX
#endif

static void
test_edgetriggered(void *et)
{
	struct event *ev = NULL;
	struct event_base *base = NULL;
	const char *test = "test string";
	evutil_socket_t pair[2] = {-1,-1};
	int supports_et;

	/* On Linux 3.2.1 (at least, as patched by Fedora and tested by Nick),
	 * doing a "recv" on an AF_UNIX socket resets the readability of the
	 * socket, even though there is no state change, so we don't actually
	 * get edge-triggered behavior.  Yuck!  Linux 3.1.9 didn't have this
	 * problem.
	 */
#ifdef __linux__
	if (evutil_ersatz_socketpair_(AF_INET, SOCK_STREAM, 0, pair) == -1) {
		tt_abort_perror("socketpair");
	}
#else
	if (evutil_socketpair(LOCAL_SOCKETPAIR_AF, SOCK_STREAM, 0, pair) == -1) {
		tt_abort_perror("socketpair");
	}
#endif

	called = was_et = 0;

	tt_int_op(send(pair[0], test, (int)strlen(test)+1, 0), >, 0);
	shutdown(pair[0], SHUT_WR);

	/* Initalize the event library */
	base = event_base_new();

	if (!strcmp(event_base_get_method(base), "epoll") ||
	    !strcmp(event_base_get_method(base), "epoll (with changelist)") ||
	    !strcmp(event_base_get_method(base), "kqueue"))
		supports_et = 1;
	else
		supports_et = 0;

	TT_BLATHER(("Checking for edge-triggered events with %s, which should %s"
				"support edge-triggering", event_base_get_method(base),
				supports_et?"":"not "));

	/* Initalize one event */
	ev = event_new(base, pair[1], EV_READ|EV_ET|EV_PERSIST, read_cb, &ev);

	event_add(ev, NULL);

	/* We're going to call the dispatch function twice.  The first invocation
	 * will read a single byte from pair[1] in either case.  If we're edge
	 * triggered, we'll only see the event once (since we only see transitions
	 * from no data to data), so the second invocation of event_base_loop will
	 * do nothing.  If we're level triggered, the second invocation of
	 * event_base_loop will also activate the event (because there's still
	 * data to read). */
	event_base_loop(base,EVLOOP_NONBLOCK|EVLOOP_ONCE);
	event_base_loop(base,EVLOOP_NONBLOCK|EVLOOP_ONCE);

	if (supports_et) {
		tt_int_op(called, ==, 1);
		tt_assert(was_et);
	} else {
		tt_int_op(called, ==, 2);
		tt_assert(!was_et);
	}

 end:
	if (ev) {
		event_del(ev);
		event_free(ev);
	}
	if (base)
		event_base_free(base);
	evutil_closesocket(pair[0]);
	evutil_closesocket(pair[1]);
}

static void
test_edgetriggered_mix_error(void *data_)
{
	struct basic_test_data *data = data_;
	struct event_base *base = NULL;
	struct event *ev_et=NULL, *ev_lt=NULL;

#ifdef EVENT__DISABLE_DEBUG_MODE
	if (1)
		tt_skip();
#endif

	if (!libevent_tests_running_in_debug_mode)
		event_enable_debug_mode();

	base = event_base_new();

	/* try mixing edge-triggered and level-triggered to make sure it fails*/
	ev_et = event_new(base, data->pair[0], EV_READ|EV_ET, read_cb, ev_et);
	tt_assert(ev_et);
	ev_lt = event_new(base, data->pair[0], EV_READ, read_cb, ev_lt);
	tt_assert(ev_lt);

	/* Add edge-triggered, then level-triggered.  Get an error. */
	tt_int_op(0, ==, event_add(ev_et, NULL));
	tt_int_op(-1, ==, event_add(ev_lt, NULL));
	tt_int_op(EV_READ, ==, event_pending(ev_et, EV_READ, NULL));
	tt_int_op(0, ==, event_pending(ev_lt, EV_READ, NULL));

	tt_int_op(0, ==, event_del(ev_et));
	/* Add level-triggered, then edge-triggered.  Get an error. */
	tt_int_op(0, ==, event_add(ev_lt, NULL));
	tt_int_op(-1, ==, event_add(ev_et, NULL));
	tt_int_op(EV_READ, ==, event_pending(ev_lt, EV_READ, NULL));
	tt_int_op(0, ==, event_pending(ev_et, EV_READ, NULL));

end:
	if (ev_et)
		event_free(ev_et);
	if (ev_lt)
		event_free(ev_lt);
	if (base)
		event_base_free(base);
}

struct testcase_t edgetriggered_testcases[] = {
	{ "et", test_edgetriggered, TT_FORK, NULL, NULL },
	{ "et_mix_error", test_edgetriggered_mix_error,
	  TT_FORK|TT_NEED_SOCKETPAIR|TT_NO_LOGS, &basic_setup, NULL },
	END_OF_TESTCASES
};
