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

#include <stdlib.h>
#include <string.h>
#include "event2/event.h"
#include "event2/thread.h"
#include "event2/buffer.h"
#include "event2/buffer_compat.h"
#include "event2/bufferevent.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include "regress.h"
#include "tinytest.h"
#include "tinytest_macros.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#undef WIN32_LEAN_AND_MEAN

#include "iocp-internal.h"
#include "evbuffer-internal.h"
#include "evthread-internal.h"

/* FIXME remove these ones */
#include <sys/queue.h>
#include "event2/event_struct.h"
#include "event-internal.h"

#define MAX_CALLS 16

static void *count_lock = NULL, *count_cond = NULL;
static int count = 0;

static void
count_init(void)
{
	EVTHREAD_ALLOC_LOCK(count_lock, 0);
	EVTHREAD_ALLOC_COND(count_cond);

	tt_assert(count_lock);
	tt_assert(count_cond);

end:
	;
}

static void
count_free(void)
{
	EVTHREAD_FREE_LOCK(count_lock, 0);
	EVTHREAD_FREE_COND(count_cond);
}

static void
count_incr(void)
{
	EVLOCK_LOCK(count_lock, 0);
	count++;
	EVTHREAD_COND_BROADCAST(count_cond);
	EVLOCK_UNLOCK(count_lock, 0);
}

static int
count_wait_for(int i, int ms)
{
	struct timeval tv;
	DWORD elapsed;
	int rv = -1;

	EVLOCK_LOCK(count_lock, 0);
	while (ms > 0 && count != i) {
		tv.tv_sec = 0;
		tv.tv_usec = ms * 1000;
		elapsed = GetTickCount();
		EVTHREAD_COND_WAIT_TIMED(count_cond, count_lock, &tv);
		elapsed = GetTickCount() - elapsed;
		ms -= elapsed;
	}
	if (count == i)
		rv = 0;
	EVLOCK_UNLOCK(count_lock, 0);

	return rv;
}

struct dummy_overlapped {
	struct event_overlapped eo;
	void *lock;
	int call_count;
	uintptr_t keys[MAX_CALLS];
	ev_ssize_t sizes[MAX_CALLS];
};

static void
dummy_cb(struct event_overlapped *o, uintptr_t key, ev_ssize_t n, int ok)
{
	struct dummy_overlapped *d_o =
	    EVUTIL_UPCAST(o, struct dummy_overlapped, eo);

	EVLOCK_LOCK(d_o->lock, 0);
	if (d_o->call_count < MAX_CALLS) {
		d_o->keys[d_o->call_count] = key;
		d_o->sizes[d_o->call_count] = n;
	}
	d_o->call_count++;
	EVLOCK_UNLOCK(d_o->lock, 0);

	count_incr();
}

static int
pair_is_in(struct dummy_overlapped *o, uintptr_t key, ev_ssize_t n)
{
	int i;
	int result = 0;
	EVLOCK_LOCK(o->lock, 0);
	for (i=0; i < o->call_count; ++i) {
		if (o->keys[i] == key && o->sizes[i] == n) {
			result = 1;
			break;
		}
	}
	EVLOCK_UNLOCK(o->lock, 0);
	return result;
}

static void
test_iocp_port(void *ptr)
{
	struct event_iocp_port *port = NULL;
	struct dummy_overlapped o1, o2;

	memset(&o1, 0, sizeof(o1));
	memset(&o2, 0, sizeof(o2));

	count_init();
	EVTHREAD_ALLOC_LOCK(o1.lock, EVTHREAD_LOCKTYPE_RECURSIVE);
	EVTHREAD_ALLOC_LOCK(o2.lock, EVTHREAD_LOCKTYPE_RECURSIVE);

	tt_assert(o1.lock);
	tt_assert(o2.lock);

	event_overlapped_init_(&o1.eo, dummy_cb);
	event_overlapped_init_(&o2.eo, dummy_cb);

	port = event_iocp_port_launch_(0);
	tt_assert(port);

	tt_assert(!event_iocp_activate_overlapped_(port, &o1.eo, 10, 100));
	tt_assert(!event_iocp_activate_overlapped_(port, &o2.eo, 20, 200));

	tt_assert(!event_iocp_activate_overlapped_(port, &o1.eo, 11, 101));
	tt_assert(!event_iocp_activate_overlapped_(port, &o2.eo, 21, 201));

	tt_assert(!event_iocp_activate_overlapped_(port, &o1.eo, 12, 102));
	tt_assert(!event_iocp_activate_overlapped_(port, &o2.eo, 22, 202));

	tt_assert(!event_iocp_activate_overlapped_(port, &o1.eo, 13, 103));
	tt_assert(!event_iocp_activate_overlapped_(port, &o2.eo, 23, 203));

	tt_int_op(count_wait_for(8, 2000), ==, 0);

	tt_want(!event_iocp_shutdown_(port, 2000));

	tt_int_op(o1.call_count, ==, 4);
	tt_int_op(o2.call_count, ==, 4);

	tt_want(pair_is_in(&o1, 10, 100));
	tt_want(pair_is_in(&o1, 11, 101));
	tt_want(pair_is_in(&o1, 12, 102));
	tt_want(pair_is_in(&o1, 13, 103));

	tt_want(pair_is_in(&o2, 20, 200));
	tt_want(pair_is_in(&o2, 21, 201));
	tt_want(pair_is_in(&o2, 22, 202));
	tt_want(pair_is_in(&o2, 23, 203));

end:
	EVTHREAD_FREE_LOCK(o1.lock, EVTHREAD_LOCKTYPE_RECURSIVE);
	EVTHREAD_FREE_LOCK(o2.lock, EVTHREAD_LOCKTYPE_RECURSIVE);
	count_free();
}

static struct evbuffer *rbuf = NULL, *wbuf = NULL;

static void
read_complete(struct event_overlapped *eo, uintptr_t key,
    ev_ssize_t nbytes, int ok)
{
	tt_assert(ok);
	evbuffer_commit_read_(rbuf, nbytes);
	count_incr();
end:
	;
}

static void
write_complete(struct event_overlapped *eo, uintptr_t key,
    ev_ssize_t nbytes, int ok)
{
	tt_assert(ok);
	evbuffer_commit_write_(wbuf, nbytes);
	count_incr();
end:
	;
}

static void
test_iocp_evbuffer(void *ptr)
{
	struct event_overlapped rol, wol;
	struct basic_test_data *data = ptr;
	struct event_iocp_port *port = NULL;
	struct evbuffer *buf=NULL;
	struct evbuffer_chain *chain;
	char junk[1024];
	int i;

	count_init();
	event_overlapped_init_(&rol, read_complete);
	event_overlapped_init_(&wol, write_complete);

	for (i = 0; i < (int)sizeof(junk); ++i)
		junk[i] = (char)(i);

	rbuf = evbuffer_overlapped_new_(data->pair[0]);
	wbuf = evbuffer_overlapped_new_(data->pair[1]);
	evbuffer_enable_locking(rbuf, NULL);
	evbuffer_enable_locking(wbuf, NULL);

	port = event_iocp_port_launch_(0);
	tt_assert(port);
	tt_assert(rbuf);
	tt_assert(wbuf);

	tt_assert(!event_iocp_port_associate_(port, data->pair[0], 100));
	tt_assert(!event_iocp_port_associate_(port, data->pair[1], 100));

	for (i=0;i<10;++i)
		evbuffer_add(wbuf, junk, sizeof(junk));

	buf = evbuffer_new();
	tt_assert(buf != NULL);
	evbuffer_add(rbuf, junk, sizeof(junk));
	tt_assert(!evbuffer_launch_read_(rbuf, 2048, &rol));
	evbuffer_add_buffer(buf, rbuf);
	tt_int_op(evbuffer_get_length(buf), ==, sizeof(junk));
	for (chain = buf->first; chain; chain = chain->next)
		tt_int_op(chain->flags & EVBUFFER_MEM_PINNED_ANY, ==, 0);
	tt_assert(!evbuffer_get_length(rbuf));
	tt_assert(!evbuffer_launch_write_(wbuf, 512, &wol));

	tt_int_op(count_wait_for(2, 2000), ==, 0);

	tt_int_op(evbuffer_get_length(rbuf),==,512);

	/* FIXME Actually test some stuff here. */

	tt_want(!event_iocp_shutdown_(port, 2000));
end:
	count_free();
	evbuffer_free(rbuf);
	evbuffer_free(wbuf);
	if (buf) evbuffer_free(buf);
}

static int got_readcb = 0;

static void
async_readcb(struct bufferevent *bev, void *arg)
{
	/* Disabling read should cause the loop to quit */
	bufferevent_disable(bev, EV_READ);
	got_readcb++;
}

static void
test_iocp_bufferevent_async(void *ptr)
{
	struct basic_test_data *data = ptr;
	struct event_iocp_port *port = NULL;
	struct bufferevent *bea1=NULL, *bea2=NULL;
	char buf[128];
	size_t n;

	event_base_start_iocp_(data->base, 0);
	port = event_base_get_iocp_(data->base);
	tt_assert(port);

	bea1 = bufferevent_async_new_(data->base, data->pair[0],
	    BEV_OPT_DEFER_CALLBACKS);
	bea2 = bufferevent_async_new_(data->base, data->pair[1],
	    BEV_OPT_DEFER_CALLBACKS);
	tt_assert(bea1);
	tt_assert(bea2);

	bufferevent_setcb(bea2, async_readcb, NULL, NULL, NULL);
	bufferevent_enable(bea1, EV_WRITE);
	bufferevent_enable(bea2, EV_READ);

	bufferevent_write(bea1, "Hello world", strlen("Hello world")+1);

	event_base_dispatch(data->base);

	tt_int_op(got_readcb, ==, 1);
	n = bufferevent_read(bea2, buf, sizeof(buf)-1);
	buf[n]='\0';
	tt_str_op(buf, ==, "Hello world");

end:
	bufferevent_free(bea1);
	bufferevent_free(bea2);
}


struct testcase_t iocp_testcases[] = {
	{ "port", test_iocp_port, TT_FORK|TT_NEED_THREADS, &basic_setup, NULL },
	{ "evbuffer", test_iocp_evbuffer,
	  TT_FORK|TT_NEED_SOCKETPAIR|TT_NEED_THREADS,
	  &basic_setup, NULL },
	{ "bufferevent_async", test_iocp_bufferevent_async,
	  TT_FORK|TT_NEED_SOCKETPAIR|TT_NEED_THREADS|TT_NEED_BASE,
	  &basic_setup, NULL },
	END_OF_TESTCASES
};
