/*
 * Copyright (c) 2003-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
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

/* The old tests here need assertions to work. */
#undef NDEBUG

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include "event2/event-config.h"

#include <sys/types.h>
#include <sys/stat.h>
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/queue.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#endif
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#ifdef EVENT__HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#include "event2/event-config.h"
#include "event2/event.h"
#include "event2/event_struct.h"
#include "event2/event_compat.h"
#include "event2/tag.h"
#include "event2/buffer.h"
#include "event2/bufferevent.h"
#include "event2/bufferevent_compat.h"
#include "event2/bufferevent_struct.h"
#include "event2/listener.h"
#include "event2/util.h"

#include "bufferevent-internal.h"
#include "evthread-internal.h"
#include "util-internal.h"
#ifdef _WIN32
#include "iocp-internal.h"
#endif

#include "regress.h"
#include "regress_testutils.h"

/*
 * simple bufferevent test
 */

static void
readcb(struct bufferevent *bev, void *arg)
{
	if (evbuffer_get_length(bev->input) == 8333) {
		struct evbuffer *evbuf = evbuffer_new();
		assert(evbuf != NULL);

		/* gratuitous test of bufferevent_read_buffer */
		bufferevent_read_buffer(bev, evbuf);

		bufferevent_disable(bev, EV_READ);

		if (evbuffer_get_length(evbuf) == 8333) {
			test_ok++;
		}

		evbuffer_free(evbuf);
	}
}

static void
writecb(struct bufferevent *bev, void *arg)
{
	if (evbuffer_get_length(bev->output) == 0) {
		test_ok++;
	}
}

static void
errorcb(struct bufferevent *bev, short what, void *arg)
{
	test_ok = -2;
}

static void
test_bufferevent_impl(int use_pair, int flush)
{
	struct bufferevent *bev1 = NULL, *bev2 = NULL;
	char buffer[8333];
	int i;
	int expected = 2;

	if (use_pair) {
		struct bufferevent *pair[2];
		tt_assert(0 == bufferevent_pair_new(NULL, 0, pair));
		bev1 = pair[0];
		bev2 = pair[1];
		bufferevent_setcb(bev1, readcb, writecb, errorcb, bev1);
		bufferevent_setcb(bev2, readcb, writecb, errorcb, NULL);
		tt_int_op(bufferevent_getfd(bev1), ==, -1);
		tt_ptr_op(bufferevent_get_underlying(bev1), ==, NULL);
		tt_ptr_op(bufferevent_pair_get_partner(bev1), ==, bev2);
		tt_ptr_op(bufferevent_pair_get_partner(bev2), ==, bev1);
	} else {
		bev1 = bufferevent_new(pair[0], readcb, writecb, errorcb, NULL);
		bev2 = bufferevent_new(pair[1], readcb, writecb, errorcb, NULL);
		tt_int_op(bufferevent_getfd(bev1), ==, pair[0]);
		tt_ptr_op(bufferevent_get_underlying(bev1), ==, NULL);
		tt_ptr_op(bufferevent_pair_get_partner(bev1), ==, NULL);
		tt_ptr_op(bufferevent_pair_get_partner(bev2), ==, NULL);
	}

	{
		/* Test getcb. */
		bufferevent_data_cb r, w;
		bufferevent_event_cb e;
		void *a;
		bufferevent_getcb(bev1, &r, &w, &e, &a);
		tt_ptr_op(r, ==, readcb);
		tt_ptr_op(w, ==, writecb);
		tt_ptr_op(e, ==, errorcb);
		tt_ptr_op(a, ==, use_pair ? bev1 : NULL);
	}

	bufferevent_disable(bev1, EV_READ);
	bufferevent_enable(bev2, EV_READ);

	tt_int_op(bufferevent_get_enabled(bev1), ==, EV_WRITE);
	tt_int_op(bufferevent_get_enabled(bev2), ==, EV_WRITE|EV_READ);

	for (i = 0; i < (int)sizeof(buffer); i++)
		buffer[i] = i;

	bufferevent_write(bev1, buffer, sizeof(buffer));
	if (flush >= 0) {
		tt_int_op(bufferevent_flush(bev1, EV_WRITE, flush), >=, 0);
	}

	event_dispatch();

	bufferevent_free(bev2);
	tt_ptr_op(bufferevent_pair_get_partner(bev1), ==, NULL);
	bufferevent_free(bev1);

	/** Only pair call errorcb for BEV_FINISHED */
	if (use_pair && flush == BEV_FINISHED) {
		expected = -1;
	}
	if (test_ok != expected)
		test_ok = 0;
end:
	;
}

static void test_bufferevent(void) { test_bufferevent_impl(0, -1); }
static void test_bufferevent_pair(void) { test_bufferevent_impl(1, -1); }

static void test_bufferevent_flush_normal(void) { test_bufferevent_impl(0, BEV_NORMAL); }
static void test_bufferevent_flush_flush(void) { test_bufferevent_impl(0, BEV_FLUSH); }
static void test_bufferevent_flush_finished(void) { test_bufferevent_impl(0, BEV_FINISHED); }

static void test_bufferevent_pair_flush_normal(void) { test_bufferevent_impl(1, BEV_NORMAL); }
static void test_bufferevent_pair_flush_flush(void) { test_bufferevent_impl(1, BEV_FLUSH); }
static void test_bufferevent_pair_flush_finished(void) { test_bufferevent_impl(1, BEV_FINISHED); }

#if defined(EVTHREAD_USE_PTHREADS_IMPLEMENTED)
/**
 * Trace lock/unlock/alloc/free for locks.
 * (More heavier then evthread_debug*)
 */
typedef struct
{
	void *lock;
	enum {
		ALLOC, FREE,
	} status;
	size_t locked /** allow recursive locking */;
} lock_wrapper;
struct lock_unlock_base
{
	/* Original callbacks */
	struct evthread_lock_callbacks cbs;
	/* Map of locks */
	lock_wrapper *locks;
	size_t nr_locks;
} lu_base = {
	.locks = NULL,
};

static lock_wrapper *lu_find(void *lock_)
{
	size_t i;
	for (i = 0; i < lu_base.nr_locks; ++i) {
		lock_wrapper *lock = &lu_base.locks[i];
		if (lock->lock == lock_)
			return lock;
	}
	return NULL;
}

static void *trace_lock_alloc(unsigned locktype)
{
	void *lock;
	++lu_base.nr_locks;
	lu_base.locks = realloc(lu_base.locks,
		sizeof(lock_wrapper) * lu_base.nr_locks);
	lock = lu_base.cbs.alloc(locktype);
	lu_base.locks[lu_base.nr_locks - 1] = (lock_wrapper){ lock, ALLOC, 0 };
	return lock;
}
static void trace_lock_free(void *lock_, unsigned locktype)
{
	lock_wrapper *lock = lu_find(lock_);
	if (!lock || lock->status == FREE || lock->locked) {
		TT_FAIL(("lock: free error"));
	} else {
		lock->status = FREE;
		lu_base.cbs.free(lock_, locktype);
	}
}
static int trace_lock_lock(unsigned mode, void *lock_)
{
	lock_wrapper *lock = lu_find(lock_);
	if (!lock || lock->status == FREE) {
		TT_FAIL(("lock: lock error"));
		return -1;
	} else {
		++lock->locked;
		return lu_base.cbs.lock(mode, lock_);
	}
}
static int trace_lock_unlock(unsigned mode, void *lock_)
{
	lock_wrapper *lock = lu_find(lock_);
	if (!lock || lock->status == FREE || !lock->locked) {
		TT_FAIL(("lock: unlock error"));
		return -1;
	} else {
		--lock->locked;
		return lu_base.cbs.unlock(mode, lock_);
	}
}
static void lock_unlock_free_thread_cbs(void)
{
	event_base_free(NULL);

	if (libevent_tests_running_in_debug_mode)
		libevent_global_shutdown();

	/** drop immutable flag */
	evthread_set_lock_callbacks(NULL);
	/** avoid calling of event_global_setup_locks_() for new cbs */
	libevent_global_shutdown();
	/** drop immutable flag for non-debug ops (since called after shutdown) */
	evthread_set_lock_callbacks(NULL);
}

static int use_lock_unlock_profiler(void)
{
	struct evthread_lock_callbacks cbs = {
		EVTHREAD_LOCK_API_VERSION,
		EVTHREAD_LOCKTYPE_RECURSIVE,
		trace_lock_alloc,
		trace_lock_free,
		trace_lock_lock,
		trace_lock_unlock,
	};
	memcpy(&lu_base.cbs, evthread_get_lock_callbacks(),
		sizeof(lu_base.cbs));
	{
		lock_unlock_free_thread_cbs();

		evthread_set_lock_callbacks(&cbs);
		/** re-create debug locks correctly */
		evthread_enable_lock_debugging();

		event_init();
	}
	return 0;
}
static void free_lock_unlock_profiler(struct basic_test_data *data)
{
	/** fix "held_by" for kqueue */
	evthread_set_lock_callbacks(NULL);

	lock_unlock_free_thread_cbs();
	free(lu_base.locks);
	data->base = NULL;
}

static void test_bufferevent_pair_release_lock(void *arg)
{
	struct basic_test_data *data = arg;
	use_lock_unlock_profiler();
	{
		struct bufferevent *pair[2];
		if (!bufferevent_pair_new(NULL, BEV_OPT_THREADSAFE, pair)) {
			bufferevent_free(pair[0]);
			bufferevent_free(pair[1]);
		} else
			tt_abort_perror("bufferevent_pair_new");
	}
	free_lock_unlock_profiler(data);
end:
	;
}
#endif

/*
 * test watermarks and bufferevent
 */

static void
wm_readcb(struct bufferevent *bev, void *arg)
{
	struct evbuffer *evbuf = evbuffer_new();
	int len = (int)evbuffer_get_length(bev->input);
	static int nread;

	assert(len >= 10 && len <= 20);

	assert(evbuf != NULL);

	/* gratuitous test of bufferevent_read_buffer */
	bufferevent_read_buffer(bev, evbuf);

	nread += len;
	if (nread == 65000) {
		bufferevent_disable(bev, EV_READ);
		test_ok++;
	}

	evbuffer_free(evbuf);
}

static void
wm_writecb(struct bufferevent *bev, void *arg)
{
	assert(evbuffer_get_length(bev->output) <= 100);
	if (evbuffer_get_length(bev->output) == 0) {
		evbuffer_drain(bev->output, evbuffer_get_length(bev->output));
		test_ok++;
	}
}

static void
wm_errorcb(struct bufferevent *bev, short what, void *arg)
{
	test_ok = -2;
}

static void
test_bufferevent_watermarks_impl(int use_pair)
{
	struct bufferevent *bev1 = NULL, *bev2 = NULL;
	char buffer[65000];
	size_t low, high;
	int i;
	test_ok = 0;

	if (use_pair) {
		struct bufferevent *pair[2];
		tt_assert(0 == bufferevent_pair_new(NULL, 0, pair));
		bev1 = pair[0];
		bev2 = pair[1];
		bufferevent_setcb(bev1, NULL, wm_writecb, errorcb, NULL);
		bufferevent_setcb(bev2, wm_readcb, NULL, errorcb, NULL);
	} else {
		bev1 = bufferevent_new(pair[0], NULL, wm_writecb, wm_errorcb, NULL);
		bev2 = bufferevent_new(pair[1], wm_readcb, NULL, wm_errorcb, NULL);
	}
	tt_assert(bev1);
	tt_assert(bev2);
	bufferevent_disable(bev1, EV_READ);
	bufferevent_enable(bev2, EV_READ);

	/* By default, low watermarks are set to 0 */
	bufferevent_getwatermark(bev1, EV_READ, &low, NULL);
	tt_int_op(low, ==, 0);
	bufferevent_getwatermark(bev2, EV_WRITE, &low, NULL);
	tt_int_op(low, ==, 0);

	for (i = 0; i < (int)sizeof(buffer); i++)
		buffer[i] = (char)i;

	/* limit the reading on the receiving bufferevent */
	bufferevent_setwatermark(bev2, EV_READ, 10, 20);

	bufferevent_getwatermark(bev2, EV_READ, &low, &high);
	tt_int_op(low, ==, 10);
	tt_int_op(high, ==, 20);

	/* Tell the sending bufferevent not to notify us till it's down to
	   100 bytes. */
	bufferevent_setwatermark(bev1, EV_WRITE, 100, 2000);

	bufferevent_getwatermark(bev1, EV_WRITE, &low, &high);
	tt_int_op(low, ==, 100);
	tt_int_op(high, ==, 2000);

	{
	int r = bufferevent_getwatermark(bev1, EV_WRITE | EV_READ, &low, &high);
	tt_int_op(r, !=, 0);
	}

	bufferevent_write(bev1, buffer, sizeof(buffer));

	event_dispatch();

	tt_int_op(test_ok, ==, 2);

	/* The write callback drained all the data from outbuf, so we
	 * should have removed the write event... */
	tt_assert(!event_pending(&bev2->ev_write, EV_WRITE, NULL));

end:
	if (bev1)
		bufferevent_free(bev1);
	if (bev2)
		bufferevent_free(bev2);
}

static void
test_bufferevent_watermarks(void)
{
	test_bufferevent_watermarks_impl(0);
}

static void
test_bufferevent_pair_watermarks(void)
{
	test_bufferevent_watermarks_impl(1);
}

/*
 * Test bufferevent filters
 */

/* strip an 'x' from each byte */

static enum bufferevent_filter_result
bufferevent_input_filter(struct evbuffer *src, struct evbuffer *dst,
    ev_ssize_t lim, enum bufferevent_flush_mode state, void *ctx)
{
	const unsigned char *buffer;
	unsigned i;

	buffer = evbuffer_pullup(src, evbuffer_get_length(src));
	for (i = 0; i < evbuffer_get_length(src); i += 2) {
		if (buffer[i] == '-')
			continue;

		assert(buffer[i] == 'x');
		evbuffer_add(dst, buffer + i + 1, 1);
	}

	evbuffer_drain(src, i);
	return (BEV_OK);
}

/* add an 'x' before each byte */

static enum bufferevent_filter_result
bufferevent_output_filter(struct evbuffer *src, struct evbuffer *dst,
    ev_ssize_t lim, enum bufferevent_flush_mode state, void *ctx)
{
	const unsigned char *buffer;
	unsigned i;
	struct bufferevent **bevp = ctx;

	++test_ok;

	if (test_ok == 1) {
		buffer = evbuffer_pullup(src, evbuffer_get_length(src));
		for (i = 0; i < evbuffer_get_length(src); ++i) {
			evbuffer_add(dst, "x", 1);
			evbuffer_add(dst, buffer + i, 1);
		}
		evbuffer_drain(src, evbuffer_get_length(src));
	} else {
		return BEV_ERROR;
	}

	if (bevp && test_ok == 1) {
		int prev = ++test_ok;
		bufferevent_write(*bevp, "-", 1);
		/* check that during this bufferevent_write()
		 * bufferevent_output_filter() will not be called again */
		assert(test_ok == prev);
		--test_ok;
	}

	return (BEV_OK);
}

static void
test_bufferevent_filters_impl(int use_pair, int disable)
{
	struct bufferevent *bev1 = NULL, *bev2 = NULL;
	struct bufferevent *bev1_base = NULL, *bev2_base = NULL;
	char buffer[8333];
	int i;

	test_ok = 0;

	if (use_pair) {
		struct bufferevent *pair[2];
		tt_assert(0 == bufferevent_pair_new(NULL, 0, pair));
		bev1 = pair[0];
		bev2 = pair[1];
	} else {
		bev1 = bufferevent_socket_new(NULL, pair[0], 0);
		bev2 = bufferevent_socket_new(NULL, pair[1], 0);
	}
	bev1_base = bev1;
	bev2_base = bev2;

	for (i = 0; i < (int)sizeof(buffer); i++)
		buffer[i] = i;

	bev1 = bufferevent_filter_new(bev1, NULL, bufferevent_output_filter,
				      BEV_OPT_CLOSE_ON_FREE, NULL,
					  disable ? &bev1 : NULL);

	bev2 = bufferevent_filter_new(bev2, bufferevent_input_filter,
				      NULL, BEV_OPT_CLOSE_ON_FREE, NULL, NULL);
	bufferevent_setcb(bev1, NULL, writecb, errorcb, NULL);
	bufferevent_setcb(bev2, readcb, NULL, errorcb, NULL);

	tt_ptr_op(bufferevent_get_underlying(bev1), ==, bev1_base);
	tt_ptr_op(bufferevent_get_underlying(bev2), ==, bev2_base);
	tt_int_op(bufferevent_getfd(bev1), ==, -1);
	tt_int_op(bufferevent_getfd(bev2), ==, -1);

	bufferevent_disable(bev1, EV_READ);
	bufferevent_enable(bev2, EV_READ);
	/* insert some filters */
	bufferevent_write(bev1, buffer, sizeof(buffer));

	event_dispatch();

	if (test_ok != 3 + !!disable)
		test_ok = 0;

end:
	if (bev1)
		bufferevent_free(bev1);
	if (bev2)
		bufferevent_free(bev2);

}

static void test_bufferevent_filters(void)
{ test_bufferevent_filters_impl(0, 0); }
static void test_bufferevent_pair_filters(void)
{ test_bufferevent_filters_impl(1, 0); }
static void test_bufferevent_filters_disable(void)
{ test_bufferevent_filters_impl(0, 1); }
static void test_bufferevent_pair_filters_disable(void)
{ test_bufferevent_filters_impl(1, 1); }


static void
sender_writecb(struct bufferevent *bev, void *ctx)
{
	if (evbuffer_get_length(bufferevent_get_output(bev)) == 0) {
		bufferevent_disable(bev,EV_READ|EV_WRITE);
		TT_BLATHER(("Flushed %d: freeing it.", (int)bufferevent_getfd(bev)));
		bufferevent_free(bev);
	}
}

static void
sender_errorcb(struct bufferevent *bev, short what, void *ctx)
{
	TT_FAIL(("Got sender error %d",(int)what));
}

static int bufferevent_connect_test_flags = 0;
static int bufferevent_trigger_test_flags = 0;
static int n_strings_read = 0;
static int n_reads_invoked = 0;
static int n_events_invoked = 0;

#define TEST_STR "Now is the time for all good events to signal for " \
	"the good of their protocol"
static void
listen_cb(struct evconnlistener *listener, evutil_socket_t fd,
    struct sockaddr *sa, int socklen, void *arg)
{
	struct event_base *base = arg;
	struct bufferevent *bev;
	const char s[] = TEST_STR;
	TT_BLATHER(("Got a request on socket %d", (int)fd ));
	bev = bufferevent_socket_new(base, fd, bufferevent_connect_test_flags);
	tt_assert(bev);
	bufferevent_setcb(bev, NULL, sender_writecb, sender_errorcb, NULL);
	bufferevent_write(bev, s, sizeof(s));
end:
	;
}

static int
fake_listener_create(struct sockaddr_in *localhost)
{
	struct sockaddr *sa = (struct sockaddr *)localhost;
	evutil_socket_t fd = -1;
	ev_socklen_t slen = sizeof(*localhost);

	memset(localhost, 0, sizeof(*localhost));
	localhost->sin_port = 0; /* have the kernel pick a port */
	localhost->sin_addr.s_addr = htonl(0x7f000001L);
	localhost->sin_family = AF_INET;

	/* bind, but don't listen or accept. should trigger
	   "Connection refused" reliably on most platforms. */
	fd = socket(localhost->sin_family, SOCK_STREAM, 0);
	tt_assert(fd >= 0);
	tt_assert(bind(fd, sa, slen) == 0);
	tt_assert(getsockname(fd, sa, &slen) == 0);

	return fd;

end:
	return -1;
}

static void
reader_eventcb(struct bufferevent *bev, short what, void *ctx)
{
	struct event_base *base = ctx;
	if (what & BEV_EVENT_ERROR) {
		perror("foobar");
		TT_FAIL(("got connector error %d", (int)what));
		return;
	}
	if (what & BEV_EVENT_CONNECTED) {
		TT_BLATHER(("connected on %d", (int)bufferevent_getfd(bev)));
		bufferevent_enable(bev, EV_READ);
	}
	if (what & BEV_EVENT_EOF) {
		char buf[512];
		size_t n;
		n = bufferevent_read(bev, buf, sizeof(buf)-1);
		tt_int_op(n, >=, 0);
		buf[n] = '\0';
		tt_str_op(buf, ==, TEST_STR);
		if (++n_strings_read == 2)
			event_base_loopexit(base, NULL);
		TT_BLATHER(("EOF on %d: %d strings read.",
			(int)bufferevent_getfd(bev), n_strings_read));
	}
end:
	;
}

static void
reader_eventcb_simple(struct bufferevent *bev, short what, void *ctx)
{
	TT_BLATHER(("Read eventcb simple invoked on %d.",
		(int)bufferevent_getfd(bev)));
	n_events_invoked++;
}

static void
reader_readcb(struct bufferevent *bev, void *ctx)
{
	TT_BLATHER(("Read invoked on %d.", (int)bufferevent_getfd(bev)));
	n_reads_invoked++;
}

static void
test_bufferevent_connect(void *arg)
{
	struct basic_test_data *data = arg;
	struct evconnlistener *lev=NULL;
	struct bufferevent *bev1=NULL, *bev2=NULL;
	struct sockaddr_in localhost;
	struct sockaddr_storage ss;
	struct sockaddr *sa;
	ev_socklen_t slen;

	int be_flags=BEV_OPT_CLOSE_ON_FREE;

	if (strstr((char*)data->setup_data, "defer")) {
		be_flags |= BEV_OPT_DEFER_CALLBACKS;
	}
	if (strstr((char*)data->setup_data, "unlocked")) {
		be_flags |= BEV_OPT_UNLOCK_CALLBACKS;
	}
	if (strstr((char*)data->setup_data, "lock")) {
		be_flags |= BEV_OPT_THREADSAFE;
	}
	bufferevent_connect_test_flags = be_flags;
#ifdef _WIN32
	if (!strcmp((char*)data->setup_data, "unset_connectex")) {
		struct win32_extension_fns *ext =
		    (struct win32_extension_fns *)
		    event_get_win32_extension_fns_();
		ext->ConnectEx = NULL;
	}
#endif

	memset(&localhost, 0, sizeof(localhost));

	localhost.sin_port = 0; /* pick-a-port */
	localhost.sin_addr.s_addr = htonl(0x7f000001L);
	localhost.sin_family = AF_INET;
	sa = (struct sockaddr *)&localhost;
	lev = evconnlistener_new_bind(data->base, listen_cb, data->base,
	    LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE,
	    16, sa, sizeof(localhost));
	tt_assert(lev);

	sa = (struct sockaddr *)&ss;
	slen = sizeof(ss);
	if (regress_get_listener_addr(lev, sa, &slen) < 0) {
		tt_abort_perror("getsockname");
	}

	tt_assert(!evconnlistener_enable(lev));
	bev1 = bufferevent_socket_new(data->base, -1, be_flags);
	bev2 = bufferevent_socket_new(data->base, -1, be_flags);
	tt_assert(bev1);
	tt_assert(bev2);
	bufferevent_setcb(bev1, reader_readcb,NULL, reader_eventcb, data->base);
	bufferevent_setcb(bev2, reader_readcb,NULL, reader_eventcb, data->base);

	bufferevent_enable(bev1, EV_READ);
	bufferevent_enable(bev2, EV_READ);

	tt_want(!bufferevent_socket_connect(bev1, sa, sizeof(localhost)));
	tt_want(!bufferevent_socket_connect(bev2, sa, sizeof(localhost)));

	event_base_dispatch(data->base);

	tt_int_op(n_strings_read, ==, 2);
	tt_int_op(n_reads_invoked, >=, 2);
end:
	if (lev)
		evconnlistener_free(lev);

	if (bev1)
		bufferevent_free(bev1);

	if (bev2)
		bufferevent_free(bev2);
}

static void
test_bufferevent_connect_fail_eventcb(void *arg)
{
	struct basic_test_data *data = arg;
	int flags = BEV_OPT_CLOSE_ON_FREE | (long)data->setup_data;
	struct bufferevent *bev = NULL;
	struct evconnlistener *lev = NULL;
	struct sockaddr_in localhost;
	ev_socklen_t slen = sizeof(localhost);
	evutil_socket_t fake_listener = -1;

	fake_listener = fake_listener_create(&localhost);

	tt_int_op(n_events_invoked, ==, 0);

	bev = bufferevent_socket_new(data->base, -1, flags);
	tt_assert(bev);
	bufferevent_setcb(bev, reader_readcb, reader_readcb,
		reader_eventcb_simple, data->base);
	bufferevent_enable(bev, EV_READ|EV_WRITE);
	tt_int_op(n_events_invoked, ==, 0);
	tt_int_op(n_reads_invoked, ==, 0);
	/** @see also test_bufferevent_connect_fail() */
	bufferevent_socket_connect(bev, (struct sockaddr *)&localhost, slen);
	tt_int_op(n_events_invoked, ==, 0);
	tt_int_op(n_reads_invoked, ==, 0);
	event_base_dispatch(data->base);
	tt_int_op(n_events_invoked, ==, 1);
	tt_int_op(n_reads_invoked, ==, 0);

end:
	if (lev)
		evconnlistener_free(lev);
	if (bev)
		bufferevent_free(bev);
	if (fake_listener >= 0)
		evutil_closesocket(fake_listener);
}

static void
want_fail_eventcb(struct bufferevent *bev, short what, void *ctx)
{
	struct event_base *base = ctx;
	const char *err;
	evutil_socket_t s;

	if (what & BEV_EVENT_ERROR) {
		s = bufferevent_getfd(bev);
		err = evutil_socket_error_to_string(evutil_socket_geterror(s));
		TT_BLATHER(("connection failure on "EV_SOCK_FMT": %s",
			EV_SOCK_ARG(s), err));
		test_ok = 1;
	} else {
		TT_FAIL(("didn't fail? what %hd", what));
	}

	event_base_loopexit(base, NULL);
}

static void
close_socket_cb(evutil_socket_t fd, short what, void *arg)
{
	evutil_socket_t *fdp = arg;
	if (*fdp >= 0) {
		evutil_closesocket(*fdp);
		*fdp = -1;
	}
}

static void
test_bufferevent_connect_fail(void *arg)
{
	struct basic_test_data *data = (struct basic_test_data *)arg;
	struct bufferevent *bev=NULL;
	struct event close_listener_event;
	int close_listener_event_added = 0;
	struct timeval one_second = { 1, 0 };
	struct sockaddr_in localhost;
	ev_socklen_t slen = sizeof(localhost);
	evutil_socket_t fake_listener = -1;
	int r;

	test_ok = 0;

	fake_listener = fake_listener_create(&localhost);
	bev = bufferevent_socket_new(data->base, -1,
		BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
	tt_assert(bev);
	bufferevent_setcb(bev, NULL, NULL, want_fail_eventcb, data->base);

	r = bufferevent_socket_connect(bev, (struct sockaddr *)&localhost, slen);
	/* XXXX we'd like to test the '0' case everywhere, but FreeBSD tells
	 * detects the error immediately, which is not really wrong of it. */
	tt_want(r == 0 || r == -1);

	/* Close the listener socket after a second. This should trigger
	   "connection refused" on some other platforms, including OSX. */
	evtimer_assign(&close_listener_event, data->base, close_socket_cb,
	    &fake_listener);
	event_add(&close_listener_event, &one_second);
	close_listener_event_added = 1;

	event_base_dispatch(data->base);

	tt_int_op(test_ok, ==, 1);

end:
	if (fake_listener >= 0)
		evutil_closesocket(fake_listener);

	if (bev)
		bufferevent_free(bev);

	if (close_listener_event_added)
		event_del(&close_listener_event);
}

struct timeout_cb_result {
	struct timeval read_timeout_at;
	struct timeval write_timeout_at;
	struct timeval last_wrote_at;
	int n_read_timeouts;
	int n_write_timeouts;
	int total_calls;
};

static void
bev_timeout_write_cb(struct bufferevent *bev, void *arg)
{
	struct timeout_cb_result *res = arg;
	evutil_gettimeofday(&res->last_wrote_at, NULL);
}

static void
bev_timeout_event_cb(struct bufferevent *bev, short what, void *arg)
{
	struct timeout_cb_result *res = arg;
	++res->total_calls;

	if ((what & (BEV_EVENT_READING|BEV_EVENT_TIMEOUT))
	    == (BEV_EVENT_READING|BEV_EVENT_TIMEOUT)) {
		evutil_gettimeofday(&res->read_timeout_at, NULL);
		++res->n_read_timeouts;
	}
	if ((what & (BEV_EVENT_WRITING|BEV_EVENT_TIMEOUT))
	    == (BEV_EVENT_WRITING|BEV_EVENT_TIMEOUT)) {
		evutil_gettimeofday(&res->write_timeout_at, NULL);
		++res->n_write_timeouts;
	}
}

static void
test_bufferevent_timeouts(void *arg)
{
	/* "arg" is a string containing "pair" and/or "filter". */
	struct bufferevent *bev1 = NULL, *bev2 = NULL;
	struct basic_test_data *data = arg;
	int use_pair = 0, use_filter = 0;
	struct timeval tv_w, tv_r, started_at;
	struct timeout_cb_result res1, res2;
	char buf[1024];

	memset(&res1, 0, sizeof(res1));
	memset(&res2, 0, sizeof(res2));

	if (strstr((char*)data->setup_data, "pair"))
		use_pair = 1;
	if (strstr((char*)data->setup_data, "filter"))
		use_filter = 1;

	if (use_pair) {
		struct bufferevent *p[2];
		tt_int_op(0, ==, bufferevent_pair_new(data->base, 0, p));
		bev1 = p[0];
		bev2 = p[1];
	} else {
		bev1 = bufferevent_socket_new(data->base, data->pair[0], 0);
		bev2 = bufferevent_socket_new(data->base, data->pair[1], 0);
	}

	tt_assert(bev1);
	tt_assert(bev2);

	if (use_filter) {
		struct bufferevent *bevf1, *bevf2;
		bevf1 = bufferevent_filter_new(bev1, NULL, NULL,
		    BEV_OPT_CLOSE_ON_FREE, NULL, NULL);
		bevf2 = bufferevent_filter_new(bev2, NULL, NULL,
		    BEV_OPT_CLOSE_ON_FREE, NULL, NULL);
		tt_assert(bevf1);
		tt_assert(bevf2);
		bev1 = bevf1;
		bev2 = bevf2;
	}

	/* Do this nice and early. */
	bufferevent_disable(bev2, EV_READ);

	/* bev1 will try to write and read.  Both will time out. */
	evutil_gettimeofday(&started_at, NULL);
	tv_w.tv_sec = tv_r.tv_sec = 0;
	tv_w.tv_usec = 100*1000;
	tv_r.tv_usec = 150*1000;
	bufferevent_setcb(bev1, NULL, bev_timeout_write_cb,
	    bev_timeout_event_cb, &res1);
	bufferevent_setwatermark(bev1, EV_WRITE, 1024*1024+10, 0);
	bufferevent_set_timeouts(bev1, &tv_r, &tv_w);
	if (use_pair) {
		/* For a pair, the fact that the other side isn't reading
		 * makes the writer stall */
		bufferevent_write(bev1, "ABCDEFG", 7);
	} else {
		/* For a real socket, the kernel's TCP buffers can eat a
		 * fair number of bytes; make sure that at some point we
		 * have some bytes that will stall. */
		struct evbuffer *output = bufferevent_get_output(bev1);
		int i;
		memset(buf, 0xbb, sizeof(buf));
		for (i=0;i<1024;++i) {
			evbuffer_add_reference(output, buf, sizeof(buf),
			    NULL, NULL);
		}
	}
	bufferevent_enable(bev1, EV_READ|EV_WRITE);

	/* bev2 has nothing to say, and isn't listening. */
	bufferevent_setcb(bev2, NULL,  bev_timeout_write_cb,
	    bev_timeout_event_cb, &res2);
	tv_w.tv_sec = tv_r.tv_sec = 0;
	tv_w.tv_usec = 200*1000;
	tv_r.tv_usec = 100*1000;
	bufferevent_set_timeouts(bev2, &tv_r, &tv_w);
	bufferevent_enable(bev2, EV_WRITE);

	tv_r.tv_sec = 0;
	tv_r.tv_usec = 350000;

	event_base_loopexit(data->base, &tv_r);
	event_base_dispatch(data->base);

	/* XXXX Test that actually reading or writing a little resets the
	 * timeouts. */

	/* Each buf1 timeout happens, and happens only once. */
	tt_want(res1.n_read_timeouts);
	tt_want(res1.n_write_timeouts);
	tt_want(res1.n_read_timeouts == 1);
	tt_want(res1.n_write_timeouts == 1);

	test_timeval_diff_eq(&started_at, &res1.read_timeout_at, 150);
	test_timeval_diff_eq(&started_at, &res1.write_timeout_at, 100);

end:
	if (bev1)
		bufferevent_free(bev1);
	if (bev2)
		bufferevent_free(bev2);
}

static void
trigger_failure_cb(evutil_socket_t fd, short what, void *ctx)
{
	TT_FAIL(("The triggered callback did not fire or the machine is really slow (try increasing timeout)."));
}

static void
trigger_eventcb(struct bufferevent *bev, short what, void *ctx)
{
	struct event_base *base = ctx;
	if (what == ~0) {
		TT_BLATHER(("Event successfully triggered."));
		event_base_loopexit(base, NULL);
		return;
	}
	reader_eventcb(bev, what, ctx);
}

static void
trigger_readcb_triggered(struct bufferevent *bev, void *ctx)
{
	TT_BLATHER(("Read successfully triggered."));
	n_reads_invoked++;
	bufferevent_trigger_event(bev, ~0, bufferevent_trigger_test_flags);
}

static void
trigger_readcb(struct bufferevent *bev, void *ctx)
{
	struct timeval timeout = { 30, 0 };
	struct event_base *base = ctx;
	size_t low, high, len;
	int expected_reads;

	TT_BLATHER(("Read invoked on %d.", (int)bufferevent_getfd(bev)));
	expected_reads = ++n_reads_invoked;

	bufferevent_setcb(bev, trigger_readcb_triggered, NULL, trigger_eventcb, ctx);

	bufferevent_getwatermark(bev, EV_READ, &low, &high);
	len = evbuffer_get_length(bufferevent_get_input(bev));

	bufferevent_setwatermark(bev, EV_READ, len + 1, 0);
	bufferevent_trigger(bev, EV_READ, bufferevent_trigger_test_flags);
	/* no callback expected */
	tt_int_op(n_reads_invoked, ==, expected_reads);

	if ((bufferevent_trigger_test_flags & BEV_TRIG_DEFER_CALLBACKS) ||
	    (bufferevent_connect_test_flags & BEV_OPT_DEFER_CALLBACKS)) {
		/* will be deferred */
	} else {
		expected_reads++;
	}

	event_base_once(base, -1, EV_TIMEOUT, trigger_failure_cb, NULL, &timeout);

	bufferevent_trigger(bev, EV_READ,
	    bufferevent_trigger_test_flags | BEV_TRIG_IGNORE_WATERMARKS);
	tt_int_op(n_reads_invoked, ==, expected_reads);

	bufferevent_setwatermark(bev, EV_READ, low, high);
end:
	;
}

static void
test_bufferevent_trigger(void *arg)
{
	struct basic_test_data *data = arg;
	struct evconnlistener *lev=NULL;
	struct bufferevent *bev=NULL;
	struct sockaddr_in localhost;
	struct sockaddr_storage ss;
	struct sockaddr *sa;
	ev_socklen_t slen;

	int be_flags=BEV_OPT_CLOSE_ON_FREE;
	int trig_flags=0;

	if (strstr((char*)data->setup_data, "defer")) {
		be_flags |= BEV_OPT_DEFER_CALLBACKS;
	}
	bufferevent_connect_test_flags = be_flags;

	if (strstr((char*)data->setup_data, "postpone")) {
		trig_flags |= BEV_TRIG_DEFER_CALLBACKS;
	}
	bufferevent_trigger_test_flags = trig_flags;

	memset(&localhost, 0, sizeof(localhost));

	localhost.sin_port = 0; /* pick-a-port */
	localhost.sin_addr.s_addr = htonl(0x7f000001L);
	localhost.sin_family = AF_INET;
	sa = (struct sockaddr *)&localhost;
	lev = evconnlistener_new_bind(data->base, listen_cb, data->base,
	    LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE,
	    16, sa, sizeof(localhost));
	tt_assert(lev);

	sa = (struct sockaddr *)&ss;
	slen = sizeof(ss);
	if (regress_get_listener_addr(lev, sa, &slen) < 0) {
		tt_abort_perror("getsockname");
	}

	tt_assert(!evconnlistener_enable(lev));
	bev = bufferevent_socket_new(data->base, -1, be_flags);
	tt_assert(bev);
	bufferevent_setcb(bev, trigger_readcb, NULL, trigger_eventcb, data->base);

	bufferevent_enable(bev, EV_READ);

	tt_want(!bufferevent_socket_connect(bev, sa, sizeof(localhost)));

	event_base_dispatch(data->base);

	tt_int_op(n_reads_invoked, ==, 2);
end:
	if (lev)
		evconnlistener_free(lev);

	if (bev)
		bufferevent_free(bev);
}

static void
test_bufferevent_socket_filter_inactive(void *arg)
{
	struct basic_test_data *data = arg;
	struct bufferevent *bev = NULL, *bevf = NULL;

	bev = bufferevent_socket_new(data->base, -1, 0);
	tt_assert(bev);
	bevf = bufferevent_filter_new(bev, NULL, NULL, 0, NULL, NULL);
	tt_assert(bevf);

end:
	if (bevf)
		bufferevent_free(bevf);
	if (bev)
		bufferevent_free(bev);
}

static void
pair_flush_eventcb(struct bufferevent *bev, short what, void *ctx)
{
	int *callback_what = ctx;
	*callback_what = what;
}

static void
test_bufferevent_pair_flush(void *arg)
{
	struct basic_test_data *data = arg;
	struct bufferevent *pair[2];
	struct bufferevent *bev1 = NULL;
	struct bufferevent *bev2 = NULL;
	int callback_what = 0;

	tt_assert(0 == bufferevent_pair_new(data->base, 0, pair));
	bev1 = pair[0];
	bev2 = pair[1];
	tt_assert(0 == bufferevent_enable(bev1, EV_WRITE));
	tt_assert(0 == bufferevent_enable(bev2, EV_READ));

	bufferevent_setcb(bev2, NULL, NULL, pair_flush_eventcb, &callback_what);

	bufferevent_flush(bev1, EV_WRITE, BEV_FINISHED);

	event_base_loop(data->base, EVLOOP_ONCE);

	tt_assert(callback_what == (BEV_EVENT_READING | BEV_EVENT_EOF));

end:
	if (bev1)
		bufferevent_free(bev1);
	if (bev2)
		bufferevent_free(bev2);
}

struct bufferevent_filter_data_stuck {
	size_t header_size;
	size_t total_read;
};

static void
bufferevent_filter_data_stuck_readcb(struct bufferevent *bev, void *arg)
{
	struct bufferevent_filter_data_stuck *filter_data = arg;
	struct evbuffer *input = bufferevent_get_input(bev);
	size_t read_size = evbuffer_get_length(input);
	evbuffer_drain(input, read_size);
	filter_data->total_read += read_size;
}

/**
 * This filter prepends header once before forwarding data.
 */
static enum bufferevent_filter_result
bufferevent_filter_data_stuck_inputcb(
    struct evbuffer *src, struct evbuffer *dst, ev_ssize_t dst_limit,
    enum bufferevent_flush_mode mode, void *ctx)
{
	struct bufferevent_filter_data_stuck *filter_data = ctx;
	static int header_inserted = 0;
	size_t payload_size;
	size_t header_size = 0;

	if (!header_inserted) {
		char *header = calloc(filter_data->header_size, 1);
		evbuffer_add(dst, header, filter_data->header_size);
		free(header);
		header_size = filter_data->header_size;
		header_inserted = 1;
	}

	payload_size = evbuffer_get_length(src);
	if (payload_size > dst_limit - header_size) {
		payload_size = dst_limit - header_size;
	}

	tt_int_op(payload_size, ==, evbuffer_remove_buffer(src, dst, payload_size));

end:
	return BEV_OK;
}

static void
test_bufferevent_filter_data_stuck(void *arg)
{
	const size_t read_high_wm = 4096;
	struct bufferevent_filter_data_stuck filter_data;
	struct basic_test_data *data = arg;
	struct bufferevent *pair[2];
	struct bufferevent *filter = NULL;

	int options = BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS;

	char payload[4096];
	int payload_size = sizeof(payload);

	memset(&filter_data, 0, sizeof(filter_data));
	filter_data.header_size = 20;

	tt_assert(bufferevent_pair_new(data->base, options, pair) == 0);

	bufferevent_setwatermark(pair[0], EV_READ, 0, read_high_wm);
	bufferevent_setwatermark(pair[1], EV_READ, 0, read_high_wm);

	tt_assert(
		filter =
		 bufferevent_filter_new(pair[1],
		 bufferevent_filter_data_stuck_inputcb,
		 NULL,
		 options,
		 NULL,
		 &filter_data));

	bufferevent_setcb(filter,
		bufferevent_filter_data_stuck_readcb,
		NULL,
		NULL,
		&filter_data);

	tt_assert(bufferevent_enable(filter, EV_READ|EV_WRITE) == 0);

	bufferevent_setwatermark(filter, EV_READ, 0, read_high_wm);

	tt_assert(bufferevent_write(pair[0], payload, sizeof(payload)) == 0);

	event_base_dispatch(data->base);

	tt_int_op(filter_data.total_read, ==, payload_size + filter_data.header_size);
end:
	if (pair[0])
		bufferevent_free(pair[0]);
	if (filter)
		bufferevent_free(filter);
}

struct testcase_t bufferevent_testcases[] = {

	LEGACY(bufferevent, TT_ISOLATED),
	LEGACY(bufferevent_pair, TT_ISOLATED),
	LEGACY(bufferevent_flush_normal, TT_ISOLATED),
	LEGACY(bufferevent_flush_flush, TT_ISOLATED),
	LEGACY(bufferevent_flush_finished, TT_ISOLATED),
	LEGACY(bufferevent_pair_flush_normal, TT_ISOLATED),
	LEGACY(bufferevent_pair_flush_flush, TT_ISOLATED),
	LEGACY(bufferevent_pair_flush_finished, TT_ISOLATED),
#if defined(EVTHREAD_USE_PTHREADS_IMPLEMENTED)
	{ "bufferevent_pair_release_lock", test_bufferevent_pair_release_lock,
	  TT_FORK|TT_ISOLATED|TT_NEED_THREADS|TT_NEED_BASE|TT_LEGACY,
	  &basic_setup, NULL },
#endif
	LEGACY(bufferevent_watermarks, TT_ISOLATED),
	LEGACY(bufferevent_pair_watermarks, TT_ISOLATED),
	LEGACY(bufferevent_filters, TT_ISOLATED),
	LEGACY(bufferevent_pair_filters, TT_ISOLATED),
	LEGACY(bufferevent_filters_disable, TT_ISOLATED),
	LEGACY(bufferevent_pair_filters_disable, TT_ISOLATED),
	{ "bufferevent_connect", test_bufferevent_connect, TT_FORK|TT_NEED_BASE,
	  &basic_setup, (void*)"" },
	{ "bufferevent_connect_defer", test_bufferevent_connect,
	  TT_FORK|TT_NEED_BASE, &basic_setup, (void*)"defer" },
	{ "bufferevent_connect_lock", test_bufferevent_connect,
	  TT_FORK|TT_NEED_BASE|TT_NEED_THREADS, &basic_setup, (void*)"lock" },
	{ "bufferevent_connect_lock_defer", test_bufferevent_connect,
	  TT_FORK|TT_NEED_BASE|TT_NEED_THREADS, &basic_setup,
	  (void*)"defer lock" },
	{ "bufferevent_connect_unlocked_cbs", test_bufferevent_connect,
	  TT_FORK|TT_NEED_BASE|TT_NEED_THREADS, &basic_setup,
	  (void*)"lock defer unlocked" },
	{ "bufferevent_connect_fail", test_bufferevent_connect_fail,
	  TT_FORK|TT_NEED_BASE, &basic_setup, NULL },
	{ "bufferevent_timeout", test_bufferevent_timeouts,
	  TT_FORK|TT_NEED_BASE|TT_NEED_SOCKETPAIR, &basic_setup, (void*)"" },
	{ "bufferevent_timeout_pair", test_bufferevent_timeouts,
	  TT_FORK|TT_NEED_BASE, &basic_setup, (void*)"pair" },
	{ "bufferevent_timeout_filter", test_bufferevent_timeouts,
	  TT_FORK|TT_NEED_BASE, &basic_setup, (void*)"filter" },
	{ "bufferevent_timeout_filter_pair", test_bufferevent_timeouts,
	  TT_FORK|TT_NEED_BASE, &basic_setup, (void*)"filter pair" },
	{ "bufferevent_trigger", test_bufferevent_trigger, TT_FORK|TT_NEED_BASE,
	  &basic_setup, (void*)"" },
	{ "bufferevent_trigger_defer", test_bufferevent_trigger,
	  TT_FORK|TT_NEED_BASE, &basic_setup, (void*)"defer" },
	{ "bufferevent_trigger_postpone", test_bufferevent_trigger,
	  TT_FORK|TT_NEED_BASE|TT_NEED_THREADS, &basic_setup,
	  (void*)"postpone" },
	{ "bufferevent_trigger_defer_postpone", test_bufferevent_trigger,
	  TT_FORK|TT_NEED_BASE|TT_NEED_THREADS, &basic_setup,
	  (void*)"defer postpone" },
#ifdef EVENT__HAVE_LIBZ
	LEGACY(bufferevent_zlib, TT_ISOLATED),
#else
	{ "bufferevent_zlib", NULL, TT_SKIP, NULL, NULL },
#endif

	{ "bufferevent_connect_fail_eventcb_defer",
	  test_bufferevent_connect_fail_eventcb,
	  TT_FORK|TT_NEED_BASE, &basic_setup, (void*)BEV_OPT_DEFER_CALLBACKS },
	{ "bufferevent_connect_fail_eventcb",
	  test_bufferevent_connect_fail_eventcb,
	  TT_FORK|TT_NEED_BASE, &basic_setup, NULL },

	{ "bufferevent_socket_filter_inactive",
	  test_bufferevent_socket_filter_inactive,
	  TT_FORK|TT_NEED_BASE, &basic_setup, NULL },
	{ "bufferevent_pair_flush",
	  test_bufferevent_pair_flush,
	  TT_FORK|TT_NEED_BASE, &basic_setup, NULL },
	{ "bufferevent_filter_data_stuck",
	  test_bufferevent_filter_data_stuck,
	  TT_FORK|TT_NEED_BASE, &basic_setup, NULL },

	END_OF_TESTCASES,
};

struct testcase_t bufferevent_iocp_testcases[] = {

	LEGACY(bufferevent, TT_ISOLATED|TT_ENABLE_IOCP),
	LEGACY(bufferevent_flush_normal, TT_ISOLATED),
	LEGACY(bufferevent_flush_flush, TT_ISOLATED),
	LEGACY(bufferevent_flush_finished, TT_ISOLATED),
	LEGACY(bufferevent_watermarks, TT_ISOLATED|TT_ENABLE_IOCP),
	LEGACY(bufferevent_filters, TT_ISOLATED|TT_ENABLE_IOCP),
	LEGACY(bufferevent_filters_disable, TT_ISOLATED|TT_ENABLE_IOCP),
	{ "bufferevent_connect", test_bufferevent_connect,
	  TT_FORK|TT_NEED_BASE|TT_ENABLE_IOCP, &basic_setup, (void*)"" },
	{ "bufferevent_connect_defer", test_bufferevent_connect,
	  TT_FORK|TT_NEED_BASE|TT_ENABLE_IOCP, &basic_setup, (void*)"defer" },
	{ "bufferevent_connect_lock", test_bufferevent_connect,
	  TT_FORK|TT_NEED_BASE|TT_NEED_THREADS|TT_ENABLE_IOCP, &basic_setup,
	  (void*)"lock" },
	{ "bufferevent_connect_lock_defer", test_bufferevent_connect,
	  TT_FORK|TT_NEED_BASE|TT_NEED_THREADS|TT_ENABLE_IOCP, &basic_setup,
	  (void*)"defer lock" },
	{ "bufferevent_connect_fail", test_bufferevent_connect_fail,
	  TT_FORK|TT_NEED_BASE|TT_ENABLE_IOCP, &basic_setup, NULL },
	{ "bufferevent_connect_nonblocking", test_bufferevent_connect,
	  TT_FORK|TT_NEED_BASE|TT_ENABLE_IOCP, &basic_setup,
	  (void*)"unset_connectex" },

	{ "bufferevent_connect_fail_eventcb_defer",
	  test_bufferevent_connect_fail_eventcb,
	  TT_FORK|TT_NEED_BASE|TT_ENABLE_IOCP, &basic_setup,
	  (void*)BEV_OPT_DEFER_CALLBACKS },
	{ "bufferevent_connect_fail_eventcb",
	  test_bufferevent_connect_fail_eventcb,
	  TT_FORK|TT_NEED_BASE|TT_ENABLE_IOCP, &basic_setup, NULL },

	END_OF_TESTCASES,
};
