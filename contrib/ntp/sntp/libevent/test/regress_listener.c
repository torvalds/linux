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
#include "util-internal.h"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include <sys/types.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
# ifdef _XOPEN_SOURCE_EXTENDED
#  include <arpa/inet.h>
# endif
#include <unistd.h>
#endif

#include <string.h>

#include "event2/listener.h"
#include "event2/event.h"
#include "event2/util.h"

#include "regress.h"
#include "tinytest.h"
#include "tinytest_macros.h"

static void
acceptcb(struct evconnlistener *listener, evutil_socket_t fd,
    struct sockaddr *addr, int socklen, void *arg)
{
	int *ptr = arg;
	--*ptr;
	TT_BLATHER(("Got one for %p", ptr));
	evutil_closesocket(fd);

	if (! *ptr)
		evconnlistener_disable(listener);
}

static void
regress_pick_a_port(void *arg)
{
	struct basic_test_data *data = arg;
	struct event_base *base = data->base;
	struct evconnlistener *listener1 = NULL, *listener2 = NULL;
	struct sockaddr_in sin;
	int count1 = 2, count2 = 1;
	struct sockaddr_storage ss1, ss2;
	struct sockaddr_in *sin1, *sin2;
	ev_socklen_t slen1 = sizeof(ss1), slen2 = sizeof(ss2);
	unsigned int flags =
	    LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_EXEC;

	evutil_socket_t fd1 = -1, fd2 = -1, fd3 = -1;

	if (data->setup_data && strstr((char*)data->setup_data, "ts")) {
		flags |= LEV_OPT_THREADSAFE;
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(0x7f000001); /* 127.0.0.1 */
	sin.sin_port = 0; /* "You pick!" */

	listener1 = evconnlistener_new_bind(base, acceptcb, &count1,
	    flags, -1, (struct sockaddr *)&sin, sizeof(sin));
	tt_assert(listener1);
	listener2 = evconnlistener_new_bind(base, acceptcb, &count2,
	    flags, -1, (struct sockaddr *)&sin, sizeof(sin));
	tt_assert(listener2);

	tt_int_op(evconnlistener_get_fd(listener1), >=, 0);
	tt_int_op(evconnlistener_get_fd(listener2), >=, 0);
	tt_assert(getsockname(evconnlistener_get_fd(listener1),
		(struct sockaddr*)&ss1, &slen1) == 0);
	tt_assert(getsockname(evconnlistener_get_fd(listener2),
		(struct sockaddr*)&ss2, &slen2) == 0);
	tt_int_op(ss1.ss_family, ==, AF_INET);
	tt_int_op(ss2.ss_family, ==, AF_INET);

	sin1 = (struct sockaddr_in*)&ss1;
	sin2 = (struct sockaddr_in*)&ss2;
	tt_int_op(ntohl(sin1->sin_addr.s_addr), ==, 0x7f000001);
	tt_int_op(ntohl(sin2->sin_addr.s_addr), ==, 0x7f000001);
	tt_int_op(sin1->sin_port, !=, sin2->sin_port);

	tt_ptr_op(evconnlistener_get_base(listener1), ==, base);
	tt_ptr_op(evconnlistener_get_base(listener2), ==, base);

	fd1 = fd2 = fd3 = -1;
	evutil_socket_connect_(&fd1, (struct sockaddr*)&ss1, slen1);
	evutil_socket_connect_(&fd2, (struct sockaddr*)&ss1, slen1);
	evutil_socket_connect_(&fd3, (struct sockaddr*)&ss2, slen2);

#ifdef _WIN32
	Sleep(100); /* XXXX this is a stupid stopgap. */
#endif
	event_base_dispatch(base);

	tt_int_op(count1, ==, 0);
	tt_int_op(count2, ==, 0);

end:
	if (fd1>=0)
		EVUTIL_CLOSESOCKET(fd1);
	if (fd2>=0)
		EVUTIL_CLOSESOCKET(fd2);
	if (fd3>=0)
		EVUTIL_CLOSESOCKET(fd3);
	if (listener1)
		evconnlistener_free(listener1);
	if (listener2)
		evconnlistener_free(listener2);
}

static void
errorcb(struct evconnlistener *lis, void *data_)
{
	int *data = data_;
	*data = 1000;
	evconnlistener_disable(lis);
}

static void
regress_listener_error(void *arg)
{
	struct basic_test_data *data = arg;
	struct event_base *base = data->base;
	struct evconnlistener *listener = NULL;
	int count = 1;
	unsigned int flags = LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE;

	if (data->setup_data && strstr((char*)data->setup_data, "ts")) {
		flags |= LEV_OPT_THREADSAFE;
	}

	/* send, so that pair[0] will look 'readable'*/
	tt_int_op(send(data->pair[1], "hello", 5, 0), >, 0);

	/* Start a listener with a bogus socket. */
	listener = evconnlistener_new(base, acceptcb, &count,
	    flags, 0,
	    data->pair[0]);
	tt_assert(listener);

	evconnlistener_set_error_cb(listener, errorcb);

	tt_assert(listener);

	event_base_dispatch(base);
	tt_int_op(count,==,1000); /* set by error cb */

end:
	if (listener)
		evconnlistener_free(listener);
}

struct testcase_t listener_testcases[] = {

	{ "randport", regress_pick_a_port, TT_FORK|TT_NEED_BASE,
	  &basic_setup, NULL},

	{ "randport_ts", regress_pick_a_port, TT_FORK|TT_NEED_BASE,
	  &basic_setup, (char*)"ts"},

	{ "error", regress_listener_error,
	  TT_FORK|TT_NEED_BASE|TT_NEED_SOCKETPAIR,
	  &basic_setup, NULL},

	{ "error_ts", regress_listener_error,
	  TT_FORK|TT_NEED_BASE|TT_NEED_SOCKETPAIR,
	  &basic_setup, (char*)"ts"},

	END_OF_TESTCASES,
};

struct testcase_t listener_iocp_testcases[] = {
	{ "randport", regress_pick_a_port,
	  TT_FORK|TT_NEED_BASE|TT_ENABLE_IOCP,
	  &basic_setup, NULL},

	{ "error", regress_listener_error,
	  TT_FORK|TT_NEED_BASE|TT_NEED_SOCKETPAIR|TT_ENABLE_IOCP,
	  &basic_setup, NULL},

	END_OF_TESTCASES,
};
