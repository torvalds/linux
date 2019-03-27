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
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "event2/event.h"
#include "event2/buffer.h"
#include "event2/buffer_compat.h"
#include "event2/util.h"

#include "defer-internal.h"
#include "evbuffer-internal.h"
#include "log-internal.h"

#include "regress.h"

/* Validates that an evbuffer is good. Returns false if it isn't, true if it
 * is*/
static int
evbuffer_validate_(struct evbuffer *buf)
{
	struct evbuffer_chain *chain;
	size_t sum = 0;
	int found_last_with_datap = 0;

	if (buf->first == NULL) {
		tt_assert(buf->last == NULL);
		tt_assert(buf->total_len == 0);
	}

	chain = buf->first;

	tt_assert(buf->last_with_datap);
	if (buf->last_with_datap == &buf->first)
		found_last_with_datap = 1;

	while (chain != NULL) {
		if (&chain->next == buf->last_with_datap)
			found_last_with_datap = 1;
		sum += chain->off;
		if (chain->next == NULL) {
			tt_assert(buf->last == chain);
		}
		tt_assert(chain->buffer_len >= chain->misalign + chain->off);
		chain = chain->next;
	}

	if (buf->first)
		tt_assert(*buf->last_with_datap);

	if (*buf->last_with_datap) {
		chain = *buf->last_with_datap;
		if (chain->off == 0 || buf->total_len == 0) {
			tt_assert(chain->off == 0)
			tt_assert(chain == buf->first);
			tt_assert(buf->total_len == 0);
		}
		chain = chain->next;
		while (chain != NULL) {
			tt_assert(chain->off == 0);
			chain = chain->next;
		}
	} else {
		tt_assert(buf->last_with_datap == &buf->first);
	}
	tt_assert(found_last_with_datap);

	tt_assert(sum == buf->total_len);
	return 1;
 end:
	return 0;
}

static void
evbuffer_get_waste(struct evbuffer *buf, size_t *allocatedp, size_t *wastedp, size_t *usedp)
{
	struct evbuffer_chain *chain;
	size_t a, w, u;
	int n = 0;
	u = a = w = 0;

	chain = buf->first;
	/* skip empty at start */
	while (chain && chain->off==0) {
		++n;
		a += chain->buffer_len;
		chain = chain->next;
	}
	/* first nonempty chain: stuff at the end only is wasted. */
	if (chain) {
		++n;
		a += chain->buffer_len;
		u += chain->off;
		if (chain->next && chain->next->off)
			w += (size_t)(chain->buffer_len - (chain->misalign + chain->off));
		chain = chain->next;
	}
	/* subsequent nonempty chains */
	while (chain && chain->off) {
		++n;
		a += chain->buffer_len;
		w += (size_t)chain->misalign;
		u += chain->off;
		if (chain->next && chain->next->off)
			w += (size_t) (chain->buffer_len - (chain->misalign + chain->off));
		chain = chain->next;
	}
	/* subsequent empty chains */
	while (chain) {
		++n;
		a += chain->buffer_len;
	}
	*allocatedp = a;
	*wastedp = w;
	*usedp = u;
}

#define evbuffer_validate(buf)			\
	TT_STMT_BEGIN if (!evbuffer_validate_(buf)) TT_DIE(("Buffer format invalid")); TT_STMT_END

static void
test_evbuffer(void *ptr)
{
	static char buffer[512], *tmp;
	struct evbuffer *evb = evbuffer_new();
	struct evbuffer *evb_two = evbuffer_new();
	size_t sz_tmp;
	int i;

	evbuffer_validate(evb);
	evbuffer_add_printf(evb, "%s/%d", "hello", 1);
	evbuffer_validate(evb);

	tt_assert(evbuffer_get_length(evb) == 7);
	tt_assert(!memcmp((char*)EVBUFFER_DATA(evb), "hello/1", 1));

	evbuffer_add_buffer(evb, evb_two);
	evbuffer_validate(evb);

	evbuffer_drain(evb, strlen("hello/"));
	evbuffer_validate(evb);
	tt_assert(evbuffer_get_length(evb) == 1);
	tt_assert(!memcmp((char*)EVBUFFER_DATA(evb), "1", 1));

	evbuffer_add_printf(evb_two, "%s", "/hello");
	evbuffer_validate(evb);
	evbuffer_add_buffer(evb, evb_two);
	evbuffer_validate(evb);

	tt_assert(evbuffer_get_length(evb_two) == 0);
	tt_assert(evbuffer_get_length(evb) == 7);
	tt_assert(!memcmp((char*)EVBUFFER_DATA(evb), "1/hello", 7) != 0);

	memset(buffer, 0, sizeof(buffer));
	evbuffer_add(evb, buffer, sizeof(buffer));
	evbuffer_validate(evb);
	tt_assert(evbuffer_get_length(evb) == 7 + 512);

	tmp = (char *)evbuffer_pullup(evb, 7 + 512);
	tt_assert(tmp);
	tt_assert(!strncmp(tmp, "1/hello", 7));
	tt_assert(!memcmp(tmp + 7, buffer, sizeof(buffer)));
	evbuffer_validate(evb);

	evbuffer_prepend(evb, "something", 9);
	evbuffer_validate(evb);
	evbuffer_prepend(evb, "else", 4);
	evbuffer_validate(evb);

	tmp = (char *)evbuffer_pullup(evb, 4 + 9 + 7);
	tt_assert(!strncmp(tmp, "elsesomething1/hello", 4 + 9 + 7));
	evbuffer_validate(evb);

	evbuffer_drain(evb, -1);
	evbuffer_validate(evb);
	evbuffer_drain(evb_two, -1);
	evbuffer_validate(evb);

	for (i = 0; i < 3; ++i) {
		evbuffer_add(evb_two, buffer, sizeof(buffer));
		evbuffer_validate(evb_two);
		evbuffer_add_buffer(evb, evb_two);
		evbuffer_validate(evb);
		evbuffer_validate(evb_two);
	}

	tt_assert(evbuffer_get_length(evb_two) == 0);
	tt_assert(evbuffer_get_length(evb) == i * sizeof(buffer));

	/* test remove buffer */
	sz_tmp = (size_t)(sizeof(buffer)*2.5);
	evbuffer_remove_buffer(evb, evb_two, sz_tmp);
	tt_assert(evbuffer_get_length(evb_two) == sz_tmp);
	tt_assert(evbuffer_get_length(evb) == sizeof(buffer) / 2);
	evbuffer_validate(evb);

	if (memcmp(evbuffer_pullup(
			   evb, -1), buffer, sizeof(buffer) / 2) != 0 ||
	    memcmp(evbuffer_pullup(
			   evb_two, -1), buffer, sizeof(buffer)) != 0)
		tt_abort_msg("Pullup did not preserve content");

	evbuffer_validate(evb);


	/* testing one-vector reserve and commit */
	{
		struct evbuffer_iovec v[1];
		char *buf;
		int i, j, r;

		for (i = 0; i < 3; ++i) {
			r = evbuffer_reserve_space(evb, 10000, v, 1);
			tt_int_op(r, ==, 1);
			tt_assert(v[0].iov_len >= 10000);
			tt_assert(v[0].iov_base != NULL);

			evbuffer_validate(evb);
			buf = v[0].iov_base;
			for (j = 0; j < 10000; ++j) {
				buf[j] = j;
			}
			evbuffer_validate(evb);

			tt_int_op(evbuffer_commit_space(evb, v, 1), ==, 0);
			evbuffer_validate(evb);

			tt_assert(evbuffer_get_length(evb) >= 10000);

			evbuffer_drain(evb, j * 5000);
			evbuffer_validate(evb);
		}
	}

 end:
	evbuffer_free(evb);
	evbuffer_free(evb_two);
}

static void
no_cleanup(const void *data, size_t datalen, void *extra)
{
}

static void
test_evbuffer_remove_buffer_with_empty(void *ptr)
{
    struct evbuffer *src = evbuffer_new();
    struct evbuffer *dst = evbuffer_new();
    char buf[2];

    evbuffer_validate(src);
    evbuffer_validate(dst);

    /* setup the buffers */
    /* we need more data in src than we will move later */
    evbuffer_add_reference(src, buf, sizeof(buf), no_cleanup, NULL);
    evbuffer_add_reference(src, buf, sizeof(buf), no_cleanup, NULL);
    /* we need one buffer in dst and one empty buffer at the end */
    evbuffer_add(dst, buf, sizeof(buf));
    evbuffer_add_reference(dst, buf, 0, no_cleanup, NULL);

    evbuffer_validate(src);
    evbuffer_validate(dst);

    /* move three bytes over */
    evbuffer_remove_buffer(src, dst, 3);

    evbuffer_validate(src);
    evbuffer_validate(dst);

end:
    evbuffer_free(src);
    evbuffer_free(dst);
}

static void
test_evbuffer_reserve2(void *ptr)
{
	/* Test the two-vector cases of reserve/commit. */
	struct evbuffer *buf = evbuffer_new();
	int n, i;
	struct evbuffer_iovec v[2];
	size_t remaining;
	char *cp, *cp2;

	/* First chunk will necessarily be one chunk. Use 512 bytes of it.*/
	n = evbuffer_reserve_space(buf, 1024, v, 2);
	tt_int_op(n, ==, 1);
	tt_int_op(evbuffer_get_length(buf), ==, 0);
	tt_assert(v[0].iov_base != NULL);
	tt_int_op(v[0].iov_len, >=, 1024);
	memset(v[0].iov_base, 'X', 512);
	cp = v[0].iov_base;
	remaining = v[0].iov_len - 512;
	v[0].iov_len = 512;
	evbuffer_validate(buf);
	tt_int_op(0, ==, evbuffer_commit_space(buf, v, 1));
	tt_int_op(evbuffer_get_length(buf), ==, 512);
	evbuffer_validate(buf);

	/* Ask for another same-chunk request, in an existing chunk. Use 8
	 * bytes of it. */
	n = evbuffer_reserve_space(buf, 32, v, 2);
	tt_int_op(n, ==, 1);
	tt_assert(cp + 512 == v[0].iov_base);
	tt_int_op(remaining, ==, v[0].iov_len);
	memset(v[0].iov_base, 'Y', 8);
	v[0].iov_len = 8;
	tt_int_op(0, ==, evbuffer_commit_space(buf, v, 1));
	tt_int_op(evbuffer_get_length(buf), ==, 520);
	remaining -= 8;
	evbuffer_validate(buf);

	/* Now ask for a request that will be split. Use only one byte of it,
	   though. */
	n = evbuffer_reserve_space(buf, remaining+64, v, 2);
	tt_int_op(n, ==, 2);
	tt_assert(cp + 520 == v[0].iov_base);
	tt_int_op(remaining, ==, v[0].iov_len);
	tt_assert(v[1].iov_base);
	tt_assert(v[1].iov_len >= 64);
	cp2 = v[1].iov_base;
	memset(v[0].iov_base, 'Z', 1);
	v[0].iov_len = 1;
	tt_int_op(0, ==, evbuffer_commit_space(buf, v, 1));
	tt_int_op(evbuffer_get_length(buf), ==, 521);
	remaining -= 1;
	evbuffer_validate(buf);

	/* Now ask for a request that will be split. Use some of the first
	 * part and some of the second. */
	n = evbuffer_reserve_space(buf, remaining+64, v, 2);
	evbuffer_validate(buf);
	tt_int_op(n, ==, 2);
	tt_assert(cp + 521 == v[0].iov_base);
	tt_int_op(remaining, ==, v[0].iov_len);
	tt_assert(v[1].iov_base == cp2);
	tt_assert(v[1].iov_len >= 64);
	memset(v[0].iov_base, 'W', 400);
	v[0].iov_len = 400;
	memset(v[1].iov_base, 'x', 60);
	v[1].iov_len = 60;
	tt_int_op(0, ==, evbuffer_commit_space(buf, v, 2));
	tt_int_op(evbuffer_get_length(buf), ==, 981);
	evbuffer_validate(buf);

	/* Now peek to make sure stuff got made how we like. */
	memset(v,0,sizeof(v));
	n = evbuffer_peek(buf, -1, NULL, v, 2);
	tt_int_op(n, ==, 2);
	tt_int_op(v[0].iov_len, ==, 921);
	tt_int_op(v[1].iov_len, ==, 60);

	cp = v[0].iov_base;
	for (i=0; i<512; ++i)
		tt_int_op(cp[i], ==, 'X');
	for (i=512; i<520; ++i)
		tt_int_op(cp[i], ==, 'Y');
	for (i=520; i<521; ++i)
		tt_int_op(cp[i], ==, 'Z');
	for (i=521; i<921; ++i)
		tt_int_op(cp[i], ==, 'W');

	cp = v[1].iov_base;
	for (i=0; i<60; ++i)
		tt_int_op(cp[i], ==, 'x');

end:
	evbuffer_free(buf);
}

static void
test_evbuffer_reserve_many(void *ptr)
{
	/* This is a glass-box test to handle expanding a buffer with more
	 * chunks and reallocating chunks as needed */
	struct evbuffer *buf = evbuffer_new();
	struct evbuffer_iovec v[8];
	int n;
	size_t sz;
	int add_data = ptr && !strcmp(ptr, "add");
	int fill_first = ptr && !strcmp(ptr, "fill");
	char *cp1, *cp2;

	/* When reserving the the first chunk, we just allocate it */
	n = evbuffer_reserve_space(buf, 128, v, 2);
	evbuffer_validate(buf);
	tt_int_op(n, ==, 1);
	tt_assert(v[0].iov_len >= 128);
	sz = v[0].iov_len;
	cp1 = v[0].iov_base;
	if (add_data) {
		*(char*)v[0].iov_base = 'X';
		v[0].iov_len = 1;
		n = evbuffer_commit_space(buf, v, 1);
		tt_int_op(n, ==, 0);
	} else if (fill_first) {
		memset(v[0].iov_base, 'X', v[0].iov_len);
		n = evbuffer_commit_space(buf, v, 1);
		tt_int_op(n, ==, 0);
		n = evbuffer_reserve_space(buf, 128, v, 2);
		tt_int_op(n, ==, 1);
		sz = v[0].iov_len;
		tt_assert(v[0].iov_base != cp1);
		cp1 = v[0].iov_base;
	}

	/* Make another chunk get added. */
	n = evbuffer_reserve_space(buf, sz+128, v, 2);
	evbuffer_validate(buf);
	tt_int_op(n, ==, 2);
	sz = v[0].iov_len + v[1].iov_len;
	tt_int_op(sz, >=, v[0].iov_len+128);
	if (add_data) {
		tt_assert(v[0].iov_base == cp1 + 1);
	} else {
		tt_assert(v[0].iov_base == cp1);
	}
	cp1 = v[0].iov_base;
	cp2 = v[1].iov_base;

	/* And a third chunk. */
	n = evbuffer_reserve_space(buf, sz+128, v, 3);
	evbuffer_validate(buf);
	tt_int_op(n, ==, 3);
	tt_assert(cp1 == v[0].iov_base);
	tt_assert(cp2 == v[1].iov_base);
	sz = v[0].iov_len + v[1].iov_len + v[2].iov_len;

	/* Now force a reallocation by asking for more space in only 2
	 * buffers. */
	n = evbuffer_reserve_space(buf, sz+128, v, 2);
	evbuffer_validate(buf);
	if (add_data) {
		tt_int_op(n, ==, 2);
		tt_assert(cp1 == v[0].iov_base);
	} else {
		tt_int_op(n, ==, 1);
	}

end:
	evbuffer_free(buf);
}

static void
test_evbuffer_expand(void *ptr)
{
	char data[4096];
	struct evbuffer *buf;
	size_t a,w,u;
	void *buffer;

	memset(data, 'X', sizeof(data));

	/* Make sure that expand() works on an empty buffer */
	buf = evbuffer_new();
	tt_int_op(evbuffer_expand(buf, 20000), ==, 0);
	evbuffer_validate(buf);
	a=w=u=0;
	evbuffer_get_waste(buf, &a,&w,&u);
	tt_assert(w == 0);
	tt_assert(u == 0);
	tt_assert(a >= 20000);
	tt_assert(buf->first);
	tt_assert(buf->first == buf->last);
	tt_assert(buf->first->off == 0);
	tt_assert(buf->first->buffer_len >= 20000);

	/* Make sure that expand() works as a no-op when there's enough
	 * contiguous space already. */
	buffer = buf->first->buffer;
	evbuffer_add(buf, data, 1024);
	tt_int_op(evbuffer_expand(buf, 1024), ==, 0);
	tt_assert(buf->first->buffer == buffer);
	evbuffer_validate(buf);
	evbuffer_free(buf);

	/* Make sure that expand() can work by moving misaligned data
	 * when it makes sense to do so. */
	buf = evbuffer_new();
	evbuffer_add(buf, data, 400);
	{
		int n = (int)(buf->first->buffer_len - buf->first->off - 1);
		tt_assert(n < (int)sizeof(data));
		evbuffer_add(buf, data, n);
	}
	tt_assert(buf->first == buf->last);
	tt_assert(buf->first->off == buf->first->buffer_len - 1);
	evbuffer_drain(buf, buf->first->off - 1);
	tt_assert(1 == evbuffer_get_length(buf));
	tt_assert(buf->first->misalign > 0);
	tt_assert(buf->first->off == 1);
	buffer = buf->first->buffer;
	tt_assert(evbuffer_expand(buf, 40) == 0);
	tt_assert(buf->first == buf->last);
	tt_assert(buf->first->off == 1);
	tt_assert(buf->first->buffer == buffer);
	tt_assert(buf->first->misalign == 0);
	evbuffer_validate(buf);
	evbuffer_free(buf);

	/* add, expand, pull-up: This used to crash libevent. */
	buf = evbuffer_new();

	evbuffer_add(buf, data, sizeof(data));
	evbuffer_add(buf, data, sizeof(data));
	evbuffer_add(buf, data, sizeof(data));

	evbuffer_validate(buf);
	evbuffer_expand(buf, 1024);
	evbuffer_validate(buf);
	evbuffer_pullup(buf, -1);
	evbuffer_validate(buf);

end:
	evbuffer_free(buf);
}


static int reference_cb_called;
static void
reference_cb(const void *data, size_t len, void *extra)
{
	tt_str_op(data, ==, "this is what we add as read-only memory.");
	tt_int_op(len, ==, strlen(data));
	tt_want(extra == (void *)0xdeadaffe);
	++reference_cb_called;
end:
	;
}

static void
test_evbuffer_reference(void *ptr)
{
	struct evbuffer *src = evbuffer_new();
	struct evbuffer *dst = evbuffer_new();
	struct evbuffer_iovec v[1];
	const char *data = "this is what we add as read-only memory.";
	reference_cb_called = 0;

	tt_assert(evbuffer_add_reference(src, data, strlen(data),
		 reference_cb, (void *)0xdeadaffe) != -1);

	evbuffer_reserve_space(dst, strlen(data), v, 1);
	tt_assert(evbuffer_remove(src, v[0].iov_base, 10) != -1);

	evbuffer_validate(src);
	evbuffer_validate(dst);

	/* make sure that we don't write data at the beginning */
	evbuffer_prepend(src, "aaaaa", 5);
	evbuffer_validate(src);
	evbuffer_drain(src, 5);

	tt_assert(evbuffer_remove(src, ((char*)(v[0].iov_base)) + 10,
		strlen(data) - 10) != -1);

	v[0].iov_len = strlen(data);

	evbuffer_commit_space(dst, v, 1);
	evbuffer_validate(src);
	evbuffer_validate(dst);

	tt_int_op(reference_cb_called, ==, 1);

	tt_assert(!memcmp(evbuffer_pullup(dst, strlen(data)),
			  data, strlen(data)));
	evbuffer_validate(dst);

 end:
	evbuffer_free(dst);
	evbuffer_free(src);
}

static struct event_base *addfile_test_event_base = NULL;
static int addfile_test_done_writing = 0;
static int addfile_test_total_written = 0;
static int addfile_test_total_read = 0;

static void
addfile_test_writecb(evutil_socket_t fd, short what, void *arg)
{
	struct evbuffer *b = arg;
	int r;
	evbuffer_validate(b);
	while (evbuffer_get_length(b)) {
		r = evbuffer_write(b, fd);
		if (r > 0) {
			addfile_test_total_written += r;
			TT_BLATHER(("Wrote %d/%d bytes", r, addfile_test_total_written));
		} else {
			int e = evutil_socket_geterror(fd);
			if (EVUTIL_ERR_RW_RETRIABLE(e))
				return;
			tt_fail_perror("write");
			event_base_loopexit(addfile_test_event_base,NULL);
		}
		evbuffer_validate(b);
	}
	addfile_test_done_writing = 1;
	return;
end:
	event_base_loopexit(addfile_test_event_base,NULL);
}

static void
addfile_test_readcb(evutil_socket_t fd, short what, void *arg)
{
	struct evbuffer *b = arg;
	int e, r = 0;
	do {
		r = evbuffer_read(b, fd, 1024);
		if (r > 0) {
			addfile_test_total_read += r;
			TT_BLATHER(("Read %d/%d bytes", r, addfile_test_total_read));
		}
	} while (r > 0);
	if (r < 0) {
		e = evutil_socket_geterror(fd);
		if (! EVUTIL_ERR_RW_RETRIABLE(e)) {
			tt_fail_perror("read");
			event_base_loopexit(addfile_test_event_base,NULL);
		}
	}
	if (addfile_test_done_writing &&
	    addfile_test_total_read >= addfile_test_total_written) {
		event_base_loopexit(addfile_test_event_base,NULL);
	}
}

static void
test_evbuffer_add_file(void *ptr)
{
	struct basic_test_data *testdata = ptr;
	const char *impl = testdata->setup_data;
	struct evbuffer *src = evbuffer_new(), *dest = evbuffer_new();
	char *tmpfilename = NULL;
	char *data = NULL;
	const char *expect_data;
	size_t datalen, expect_len;
	const char *compare;
	int fd = -1;
	int want_ismapping = -1, want_cansendfile = -1;
	unsigned flags = 0;
	int use_segment = 1, use_bigfile = 0, map_from_offset = 0,
	    view_from_offset = 0;
	struct evbuffer_file_segment *seg = NULL;
	ev_off_t starting_offset = 0, mapping_len = -1;
	ev_off_t segment_offset = 0, segment_len = -1;
	struct event *rev=NULL, *wev=NULL;
	struct event_base *base = testdata->base;
	evutil_socket_t pair[2] = {-1, -1};
	struct evutil_weakrand_state seed = { 123456789U };

	/* This test is highly parameterized based on substrings of its
	 * argument.  The strings are: */
	tt_assert(impl);
	if (strstr(impl, "nosegment")) {
		/* If nosegment is set, use the older evbuffer_add_file
		 * interface */
		use_segment = 0;
	}
	if (strstr(impl, "bigfile")) {
		/* If bigfile is set, use a 512K file.  Else use a smaller
		 * one. */
		use_bigfile = 1;
	}
	if (strstr(impl, "map_offset")) {
		/* If map_offset is set, we build the file segment starting
		 * from a point other than byte 0 and ending somewhere other
		 * than the last byte.  Otherwise we map the whole thing */
		map_from_offset = 1;
	}
	if (strstr(impl, "offset_in_segment")) {
		/* If offset_in_segment is set, we add a subsection of the
		 * file semgment starting from a point other than byte 0 of
		 * the segment. */
		view_from_offset = 1;
	}
	if (strstr(impl, "sendfile")) {
		/* If sendfile is set, we try to use a sendfile/splice style
		 * backend. */
		flags = EVBUF_FS_DISABLE_MMAP;
		want_cansendfile = 1;
		want_ismapping = 0;
	} else if (strstr(impl, "mmap")) {
		/* If sendfile is set, we try to use a mmap/CreateFileMapping
		 * style backend. */
		flags = EVBUF_FS_DISABLE_SENDFILE;
		want_ismapping = 1;
		want_cansendfile = 0;
	} else if (strstr(impl, "linear")) {
		/* If linear is set, we try to use a read-the-whole-thing
		 * backend. */
		flags = EVBUF_FS_DISABLE_SENDFILE|EVBUF_FS_DISABLE_MMAP;
		want_ismapping = 0;
		want_cansendfile = 0;
	} else if (strstr(impl, "default")) {
		/* The caller doesn't care which backend we use. */
		;
	} else {
		/* The caller must choose a backend. */
		TT_DIE(("Didn't recognize the implementation"));
	}

	if (use_bigfile) {
		unsigned int i;
		datalen = 1024*512;
		data = malloc(1024*512);
		tt_assert(data);
		for (i = 0; i < datalen; ++i)
			data[i] = (char)evutil_weakrand_(&seed);
	} else {
		data = strdup("here is a relatively small string.");
		tt_assert(data);
		datalen = strlen(data);
	}

	fd = regress_make_tmpfile(data, datalen, &tmpfilename);

	if (map_from_offset) {
		starting_offset = datalen/4 + 1;
		mapping_len = datalen / 2 - 1;
		expect_data = data + starting_offset;
		expect_len = mapping_len;
	} else {
		expect_data = data;
		expect_len = datalen;
	}
	if (view_from_offset) {
		tt_assert(use_segment); /* Can't do this with add_file*/
		segment_offset = expect_len / 3;
		segment_len = expect_len / 2;
		expect_data = expect_data + segment_offset;
		expect_len = segment_len;
	}

	if (use_segment) {
		seg = evbuffer_file_segment_new(fd, starting_offset,
		    mapping_len, flags);
		tt_assert(seg);
		if (want_ismapping >= 0) {
			if (seg->is_mapping != (unsigned)want_ismapping)
				tt_skip();
		}
		if (want_cansendfile >= 0) {
			if (seg->can_sendfile != (unsigned)want_cansendfile)
				tt_skip();
		}
	}

	/* Say that it drains to a fd so that we can use sendfile. */
	evbuffer_set_flags(src, EVBUFFER_FLAG_DRAINS_TO_FD);

#if defined(EVENT__HAVE_SENDFILE) && defined(__sun__) && defined(__svr4__)
	/* We need to use a pair of AF_INET sockets, since Solaris
	   doesn't support sendfile() over AF_UNIX. */
	if (evutil_ersatz_socketpair_(AF_INET, SOCK_STREAM, 0, pair) == -1)
		tt_abort_msg("ersatz_socketpair failed");
#else
	if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == -1)
		tt_abort_msg("socketpair failed");
#endif
	evutil_make_socket_nonblocking(pair[0]);
	evutil_make_socket_nonblocking(pair[1]);

	tt_assert(fd != -1);

	if (use_segment) {
		tt_assert(evbuffer_add_file_segment(src, seg,
			segment_offset, segment_len)!=-1);
	} else {
		tt_assert(evbuffer_add_file(src, fd, starting_offset,
			mapping_len) != -1);
	}

	evbuffer_validate(src);

	addfile_test_event_base = base;
	wev = event_new(base, pair[0], EV_WRITE|EV_PERSIST,
	    addfile_test_writecb, src);
	rev = event_new(base, pair[1], EV_READ|EV_PERSIST,
	    addfile_test_readcb, dest);

	event_add(wev, NULL);
	event_add(rev, NULL);
	event_base_dispatch(base);

	evbuffer_validate(src);
	evbuffer_validate(dest);

	tt_assert(addfile_test_done_writing);
	tt_int_op(addfile_test_total_written, ==, expect_len);
	tt_int_op(addfile_test_total_read, ==, expect_len);

	compare = (char *)evbuffer_pullup(dest, expect_len);
	tt_assert(compare != NULL);
	if (memcmp(compare, expect_data, expect_len)) {
		tt_abort_msg("Data from add_file differs.");
	}

	evbuffer_validate(dest);
 end:
	if (data)
		free(data);
	if (seg)
		evbuffer_file_segment_free(seg);
	if (src)
		evbuffer_free(src);
	if (dest)
		evbuffer_free(dest);
	if (pair[0] >= 0)
		evutil_closesocket(pair[0]);
	if (pair[1] >= 0)
		evutil_closesocket(pair[1]);
	if (wev)
		event_free(wev);
	if (rev)
		event_free(rev);
	if (tmpfilename) {
		unlink(tmpfilename);
		free(tmpfilename);
	}
}

static int file_segment_cleanup_cb_called_count = 0;
static struct evbuffer_file_segment const* file_segment_cleanup_cb_called_with = NULL;
static int file_segment_cleanup_cb_called_with_flags = 0;
static void* file_segment_cleanup_cb_called_with_arg = NULL;
static void
file_segment_cleanup_cp(struct evbuffer_file_segment const* seg, int flags, void* arg)
{
	++file_segment_cleanup_cb_called_count;
	file_segment_cleanup_cb_called_with = seg;
	file_segment_cleanup_cb_called_with_flags = flags;
	file_segment_cleanup_cb_called_with_arg = arg;
}

static void
test_evbuffer_file_segment_add_cleanup_cb(void* ptr)
{
	char *tmpfilename = NULL;
	int fd = -1;
	struct evbuffer *evb = NULL;
	struct evbuffer_file_segment *seg = NULL, *segptr;
	char const* arg = "token";

	fd = regress_make_tmpfile("file_segment_test_file", 22, &tmpfilename);
	tt_int_op(fd, >=, 0);

	evb = evbuffer_new();
	tt_assert(evb);

	segptr = seg = evbuffer_file_segment_new(fd, 0, -1, 0);
	tt_assert(seg);

	evbuffer_file_segment_add_cleanup_cb(
	  seg, &file_segment_cleanup_cp, (void*)arg);

	tt_assert(fd != -1);

	tt_assert(evbuffer_add_file_segment(evb, seg, 0, -1)!=-1);

	evbuffer_validate(evb);

	tt_int_op(file_segment_cleanup_cb_called_count, ==, 0);
	evbuffer_file_segment_free(seg);
	seg = NULL; /* Prevent double-free. */

	tt_int_op(file_segment_cleanup_cb_called_count, ==, 0);
	evbuffer_free(evb);
	evb = NULL; /* pevent double-free */

	tt_int_op(file_segment_cleanup_cb_called_count, ==, 1);
	tt_assert(file_segment_cleanup_cb_called_with == segptr);
	tt_assert(file_segment_cleanup_cb_called_with_flags == 0);
	tt_assert(file_segment_cleanup_cb_called_with_arg == (void*)arg);

end:
	if (evb)
		evbuffer_free(evb);
	if (seg)
		evbuffer_file_segment_free(seg);
	if (tmpfilename) {
		unlink(tmpfilename);
		free(tmpfilename);
	}
}

#ifndef EVENT__DISABLE_MM_REPLACEMENT
static void *
failing_malloc(size_t how_much)
{
	errno = ENOMEM;
	return NULL;
}
#endif

static void
test_evbuffer_readln(void *ptr)
{
	struct evbuffer *evb = evbuffer_new();
	struct evbuffer *evb_tmp = evbuffer_new();
	const char *s;
	char *cp = NULL;
	size_t sz;

#define tt_line_eq(content)						\
	TT_STMT_BEGIN							\
	if (!cp || sz != strlen(content) || strcmp(cp, content)) {	\
		TT_DIE(("Wanted %s; got %s [%d]", content, cp, (int)sz)); \
	}								\
	TT_STMT_END

	/* Test EOL_ANY. */
	s = "complex silly newline\r\n\n\r\n\n\rmore\0\n";
	evbuffer_add(evb, s, strlen(s)+2);
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_ANY);
	tt_line_eq("complex silly newline");
	free(cp);
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_ANY);
	if (!cp || sz != 5 || memcmp(cp, "more\0\0", 6))
		tt_abort_msg("Not as expected");
	tt_uint_op(evbuffer_get_length(evb), ==, 0);
	evbuffer_validate(evb);
	s = "\nno newline";
	evbuffer_add(evb, s, strlen(s));
	free(cp);
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_ANY);
	tt_line_eq("");
	free(cp);
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_ANY);
	tt_assert(!cp);
	evbuffer_validate(evb);
	evbuffer_drain(evb, evbuffer_get_length(evb));
	tt_assert(evbuffer_get_length(evb) == 0);
	evbuffer_validate(evb);

	/* Test EOL_CRLF */
	s = "Line with\rin the middle\nLine with good crlf\r\n\nfinal\n";
	evbuffer_add(evb, s, strlen(s));
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF);
	tt_line_eq("Line with\rin the middle");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF);
	tt_line_eq("Line with good crlf");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF);
	tt_line_eq("");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF);
	tt_line_eq("final");
	s = "x";
	evbuffer_validate(evb);
	evbuffer_add(evb, s, 1);
	evbuffer_validate(evb);
	free(cp);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF);
	tt_assert(!cp);
	evbuffer_validate(evb);

	/* Test CRLF_STRICT */
	s = " and a bad crlf\nand a good one\r\n\r\nMore\r";
	evbuffer_add(evb, s, strlen(s));
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_line_eq("x and a bad crlf\nand a good one");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_line_eq("");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_assert(!cp);
	evbuffer_validate(evb);
	evbuffer_add(evb, "\n", 1);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_line_eq("More");
	free(cp);
	tt_assert(evbuffer_get_length(evb) == 0);
	evbuffer_validate(evb);

	s = "An internal CR\r is not an eol\r\nNor is a lack of one";
	evbuffer_add(evb, s, strlen(s));
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_line_eq("An internal CR\r is not an eol");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_assert(!cp);
	evbuffer_validate(evb);

	evbuffer_add(evb, "\r\n", 2);
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_line_eq("Nor is a lack of one");
	free(cp);
	tt_assert(evbuffer_get_length(evb) == 0);
	evbuffer_validate(evb);

	/* Test LF */
	s = "An\rand a nl\n\nText";
	evbuffer_add(evb, s, strlen(s));
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_LF);
	tt_line_eq("An\rand a nl");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_LF);
	tt_line_eq("");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_LF);
	tt_assert(!cp);
	free(cp);
	evbuffer_add(evb, "\n", 1);
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_LF);
	tt_line_eq("Text");
	free(cp);
	evbuffer_validate(evb);

	/* Test NUL */
	tt_int_op(evbuffer_get_length(evb), ==, 0);
	{
		char x[] =
		    "NUL\n\0\0"
		    "The all-zeros character which may serve\0"
		    "to accomplish time fill\0and media fill";
		/* Add all but the final NUL of x. */
		evbuffer_add(evb, x, sizeof(x)-1);
	}
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_NUL);
	tt_line_eq("NUL\n");
	free(cp);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_NUL);
	tt_line_eq("");
	free(cp);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_NUL);
	tt_line_eq("The all-zeros character which may serve");
	free(cp);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_NUL);
	tt_line_eq("to accomplish time fill");
	free(cp);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_NUL);
	tt_ptr_op(cp, ==, NULL);
	evbuffer_drain(evb, -1);

	/* Test CRLF_STRICT - across boundaries*/
	s = " and a bad crlf\nand a good one\r";
	evbuffer_add(evb_tmp, s, strlen(s));
	evbuffer_validate(evb);
	evbuffer_add_buffer(evb, evb_tmp);
	evbuffer_validate(evb);
	s = "\n\r";
	evbuffer_add(evb_tmp, s, strlen(s));
	evbuffer_validate(evb);
	evbuffer_add_buffer(evb, evb_tmp);
	evbuffer_validate(evb);
	s = "\nMore\r";
	evbuffer_add(evb_tmp, s, strlen(s));
	evbuffer_validate(evb);
	evbuffer_add_buffer(evb, evb_tmp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_line_eq(" and a bad crlf\nand a good one");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_line_eq("");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_assert(!cp);
	free(cp);
	evbuffer_validate(evb);
	evbuffer_add(evb, "\n", 1);
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_line_eq("More");
	free(cp); cp = NULL;
	evbuffer_validate(evb);
	tt_assert(evbuffer_get_length(evb) == 0);

	/* Test memory problem*/
	s = "one line\ntwo line\nblue line";
	evbuffer_add(evb_tmp, s, strlen(s));
	evbuffer_validate(evb);
	evbuffer_add_buffer(evb, evb_tmp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_LF);
	tt_line_eq("one line");
	free(cp); cp = NULL;
	evbuffer_validate(evb);

	/* the next call to readline should fail */
#ifndef EVENT__DISABLE_MM_REPLACEMENT
	event_set_mem_functions(failing_malloc, realloc, free);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_LF);
	tt_assert(cp == NULL);
	evbuffer_validate(evb);

	/* now we should get the next line back */
	event_set_mem_functions(malloc, realloc, free);
#endif
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_LF);
	tt_line_eq("two line");
	free(cp); cp = NULL;
	evbuffer_validate(evb);

 end:
	evbuffer_free(evb);
	evbuffer_free(evb_tmp);
	if (cp) free(cp);
}

static void
test_evbuffer_search_eol(void *ptr)
{
	struct evbuffer *buf = evbuffer_new();
	struct evbuffer_ptr ptr1, ptr2;
	const char *s;
	size_t eol_len;

	s = "string! \r\n\r\nx\n";
	evbuffer_add(buf, s, strlen(s));
	eol_len = -1;
	ptr1 = evbuffer_search_eol(buf, NULL, &eol_len, EVBUFFER_EOL_CRLF);
	tt_int_op(ptr1.pos, ==, 8);
	tt_int_op(eol_len, ==, 2);

	eol_len = -1;
	ptr2 = evbuffer_search_eol(buf, &ptr1, &eol_len, EVBUFFER_EOL_CRLF);
	tt_int_op(ptr2.pos, ==, 8);
	tt_int_op(eol_len, ==, 2);

	evbuffer_ptr_set(buf, &ptr1, 1, EVBUFFER_PTR_ADD);
	eol_len = -1;
	ptr2 = evbuffer_search_eol(buf, &ptr1, &eol_len, EVBUFFER_EOL_CRLF);
	tt_int_op(ptr2.pos, ==, 9);
	tt_int_op(eol_len, ==, 1);

	eol_len = -1;
	ptr2 = evbuffer_search_eol(buf, &ptr1, &eol_len, EVBUFFER_EOL_CRLF_STRICT);
	tt_int_op(ptr2.pos, ==, 10);
	tt_int_op(eol_len, ==, 2);

	eol_len = -1;
	ptr1 = evbuffer_search_eol(buf, NULL, &eol_len, EVBUFFER_EOL_LF);
	tt_int_op(ptr1.pos, ==, 9);
	tt_int_op(eol_len, ==, 1);

	eol_len = -1;
	ptr2 = evbuffer_search_eol(buf, &ptr1, &eol_len, EVBUFFER_EOL_LF);
	tt_int_op(ptr2.pos, ==, 9);
	tt_int_op(eol_len, ==, 1);

	evbuffer_ptr_set(buf, &ptr1, 1, EVBUFFER_PTR_ADD);
	eol_len = -1;
	ptr2 = evbuffer_search_eol(buf, &ptr1, &eol_len, EVBUFFER_EOL_LF);
	tt_int_op(ptr2.pos, ==, 11);
	tt_int_op(eol_len, ==, 1);

	tt_assert(evbuffer_ptr_set(buf, &ptr1, evbuffer_get_length(buf), EVBUFFER_PTR_SET) == 0);
	eol_len = -1;
	ptr2 = evbuffer_search_eol(buf, &ptr1, &eol_len, EVBUFFER_EOL_LF);
	tt_int_op(ptr2.pos, ==, -1);
	tt_int_op(eol_len, ==, 0);

end:
	evbuffer_free(buf);
}

static void
test_evbuffer_iterative(void *ptr)
{
	struct evbuffer *buf = evbuffer_new();
	const char *abc = "abcdefghijklmnopqrstvuwxyzabcdefghijklmnopqrstvuwxyzabcdefghijklmnopqrstvuwxyzabcdefghijklmnopqrstvuwxyz";
	unsigned i, j, sum, n;

	sum = 0;
	n = 0;
	for (i = 0; i < 1000; ++i) {
		for (j = 1; j < strlen(abc); ++j) {
			char format[32];
			evutil_snprintf(format, sizeof(format), "%%%u.%us", j, j);
			evbuffer_add_printf(buf, format, abc);

			/* Only check for rep violations every so often.
			   Walking over the whole list of chains can get
			   pretty expensive as it gets long.
			 */
			if ((n % 337) == 0)
				evbuffer_validate(buf);

			sum += j;
			n++;
		}
	}
	evbuffer_validate(buf);

	tt_uint_op(sum, ==, evbuffer_get_length(buf));

	{
		size_t a,w,u;
		a=w=u=0;
		evbuffer_get_waste(buf, &a, &w, &u);
		if (0)
			printf("Allocated: %u.\nWasted: %u.\nUsed: %u.",
			    (unsigned)a, (unsigned)w, (unsigned)u);
		tt_assert( ((double)w)/a < .125);
	}
 end:
	evbuffer_free(buf);

}

static void
test_evbuffer_find(void *ptr)
{
	u_char* p;
	const char* test1 = "1234567890\r\n";
	const char* test2 = "1234567890\r";
#define EVBUFFER_INITIAL_LENGTH 256
	char test3[EVBUFFER_INITIAL_LENGTH];
	unsigned int i;
	struct evbuffer * buf = evbuffer_new();

	tt_assert(buf);

	/* make sure evbuffer_find doesn't match past the end of the buffer */
	evbuffer_add(buf, (u_char*)test1, strlen(test1));
	evbuffer_validate(buf);
	evbuffer_drain(buf, strlen(test1));
	evbuffer_validate(buf);
	evbuffer_add(buf, (u_char*)test2, strlen(test2));
	evbuffer_validate(buf);
	p = evbuffer_find(buf, (u_char*)"\r\n", 2);
	tt_want(p == NULL);

	/*
	 * drain the buffer and do another find; in r309 this would
	 * read past the allocated buffer causing a valgrind error.
	 */
	evbuffer_drain(buf, strlen(test2));
	evbuffer_validate(buf);
	for (i = 0; i < EVBUFFER_INITIAL_LENGTH; ++i)
		test3[i] = 'a';
	test3[EVBUFFER_INITIAL_LENGTH - 1] = 'x';
	evbuffer_add(buf, (u_char *)test3, EVBUFFER_INITIAL_LENGTH);
	evbuffer_validate(buf);
	p = evbuffer_find(buf, (u_char *)"xy", 2);
	tt_want(p == NULL);

	/* simple test for match at end of allocated buffer */
	p = evbuffer_find(buf, (u_char *)"ax", 2);
	tt_assert(p != NULL);
	tt_want(strncmp((char*)p, "ax", 2) == 0);

end:
	if (buf)
		evbuffer_free(buf);
}

static void
test_evbuffer_ptr_set(void *ptr)
{
	struct evbuffer *buf = evbuffer_new();
	struct evbuffer_ptr pos;
	struct evbuffer_iovec v[1];

	tt_assert(buf);

	tt_int_op(evbuffer_get_length(buf), ==, 0);

	tt_assert(evbuffer_ptr_set(buf, &pos, 0, EVBUFFER_PTR_SET) == 0);
	tt_assert(pos.pos == 0);
	tt_assert(evbuffer_ptr_set(buf, &pos, 1, EVBUFFER_PTR_ADD) == -1);
	tt_assert(pos.pos == -1);
	tt_assert(evbuffer_ptr_set(buf, &pos, 1, EVBUFFER_PTR_SET) == -1);
	tt_assert(pos.pos == -1);

	/* create some chains */
	evbuffer_reserve_space(buf, 5000, v, 1);
	v[0].iov_len = 5000;
	memset(v[0].iov_base, 1, v[0].iov_len);
	evbuffer_commit_space(buf, v, 1);
	evbuffer_validate(buf);

	evbuffer_reserve_space(buf, 4000, v, 1);
	v[0].iov_len = 4000;
	memset(v[0].iov_base, 2, v[0].iov_len);
	evbuffer_commit_space(buf, v, 1);

	evbuffer_reserve_space(buf, 3000, v, 1);
	v[0].iov_len = 3000;
	memset(v[0].iov_base, 3, v[0].iov_len);
	evbuffer_commit_space(buf, v, 1);
	evbuffer_validate(buf);

	tt_int_op(evbuffer_get_length(buf), ==, 12000);

	tt_assert(evbuffer_ptr_set(buf, &pos, 13000, EVBUFFER_PTR_SET) == -1);
	tt_assert(pos.pos == -1);
	tt_assert(evbuffer_ptr_set(buf, &pos, 0, EVBUFFER_PTR_SET) == 0);
	tt_assert(pos.pos == 0);
	tt_assert(evbuffer_ptr_set(buf, &pos, 13000, EVBUFFER_PTR_ADD) == -1);

	tt_assert(evbuffer_ptr_set(buf, &pos, 0, EVBUFFER_PTR_SET) == 0);
	tt_assert(pos.pos == 0);
	tt_assert(evbuffer_ptr_set(buf, &pos, 10000, EVBUFFER_PTR_ADD) == 0);
	tt_assert(pos.pos == 10000);
	tt_assert(evbuffer_ptr_set(buf, &pos, 1000, EVBUFFER_PTR_ADD) == 0);
	tt_assert(pos.pos == 11000);
	tt_assert(evbuffer_ptr_set(buf, &pos, 1000, EVBUFFER_PTR_ADD) == 0);
	tt_assert(pos.pos == 12000);
	tt_assert(evbuffer_ptr_set(buf, &pos, 1000, EVBUFFER_PTR_ADD) == -1);
	tt_assert(pos.pos == -1);

end:
	if (buf)
		evbuffer_free(buf);
}

static void
test_evbuffer_search(void *ptr)
{
	struct evbuffer *buf = evbuffer_new();
	struct evbuffer *tmp = evbuffer_new();
	struct evbuffer_ptr pos, end;

	tt_assert(buf);
	tt_assert(tmp);

	pos = evbuffer_search(buf, "x", 1, NULL);
	tt_int_op(pos.pos, ==, -1);
	tt_assert(evbuffer_ptr_set(buf, &pos, 0, EVBUFFER_PTR_SET) == 0);
	pos = evbuffer_search(buf, "x", 1, &pos);
	tt_int_op(pos.pos, ==, -1);
	tt_assert(evbuffer_ptr_set(buf, &pos, 0, EVBUFFER_PTR_SET) == 0);
	pos = evbuffer_search_range(buf, "x", 1, &pos, &pos);
	tt_int_op(pos.pos, ==, -1);
	tt_assert(evbuffer_ptr_set(buf, &pos, 0, EVBUFFER_PTR_SET) == 0);
	pos = evbuffer_search_range(buf, "x", 1, &pos, NULL);
	tt_int_op(pos.pos, ==, -1);

	/* set up our chains */
	evbuffer_add_printf(tmp, "hello");  /* 5 chars */
	evbuffer_add_buffer(buf, tmp);
	evbuffer_add_printf(tmp, "foo");    /* 3 chars */
	evbuffer_add_buffer(buf, tmp);
	evbuffer_add_printf(tmp, "cat");    /* 3 chars */
	evbuffer_add_buffer(buf, tmp);
	evbuffer_add_printf(tmp, "attack");
	evbuffer_add_buffer(buf, tmp);

	pos = evbuffer_search(buf, "attack", 6, NULL);
	tt_int_op(pos.pos, ==, 11);
	pos = evbuffer_search(buf, "attacker", 8, NULL);
	tt_int_op(pos.pos, ==, -1);

	/* test continuing search */
	pos = evbuffer_search(buf, "oc", 2, NULL);
	tt_int_op(pos.pos, ==, 7);
	pos = evbuffer_search(buf, "cat", 3, &pos);
	tt_int_op(pos.pos, ==, 8);
	pos = evbuffer_search(buf, "tacking", 7, &pos);
	tt_int_op(pos.pos, ==, -1);

	evbuffer_ptr_set(buf, &pos, 5, EVBUFFER_PTR_SET);
	pos = evbuffer_search(buf, "foo", 3, &pos);
	tt_int_op(pos.pos, ==, 5);

	evbuffer_ptr_set(buf, &pos, 2, EVBUFFER_PTR_ADD);
	pos = evbuffer_search(buf, "tat", 3, &pos);
	tt_int_op(pos.pos, ==, 10);

	/* test bounded search. */
	/* Set "end" to the first t in "attack". */
	evbuffer_ptr_set(buf, &end, 12, EVBUFFER_PTR_SET);
	pos = evbuffer_search_range(buf, "foo", 3, NULL, &end);
	tt_int_op(pos.pos, ==, 5);
	pos = evbuffer_search_range(buf, "foocata", 7, NULL, &end);
	tt_int_op(pos.pos, ==, 5);
	pos = evbuffer_search_range(buf, "foocatat", 8, NULL, &end);
	tt_int_op(pos.pos, ==, -1);
	pos = evbuffer_search_range(buf, "ack", 3, NULL, &end);
	tt_int_op(pos.pos, ==, -1);

	/* Set "end" after the last byte in the buffer. */
	tt_assert(evbuffer_ptr_set(buf, &end, 17, EVBUFFER_PTR_SET) == 0);

	pos = evbuffer_search_range(buf, "attack", 6, NULL, &end);
	tt_int_op(pos.pos, ==, 11);
	tt_assert(evbuffer_ptr_set(buf, &pos, 11, EVBUFFER_PTR_SET) == 0);
	pos = evbuffer_search_range(buf, "attack", 6, &pos, &end);
	tt_int_op(pos.pos, ==, 11);
	tt_assert(evbuffer_ptr_set(buf, &pos, 17, EVBUFFER_PTR_SET) == 0);
	pos = evbuffer_search_range(buf, "attack", 6, &pos, &end);
	tt_int_op(pos.pos, ==, -1);
	tt_assert(evbuffer_ptr_set(buf, &pos, 17, EVBUFFER_PTR_SET) == 0);
	pos = evbuffer_search_range(buf, "attack", 6, &pos, NULL);
	tt_int_op(pos.pos, ==, -1);

end:
	if (buf)
		evbuffer_free(buf);
	if (tmp)
		evbuffer_free(tmp);
}

static void
log_change_callback(struct evbuffer *buffer,
    const struct evbuffer_cb_info *cbinfo,
    void *arg)
{

	size_t old_len = cbinfo->orig_size;
	size_t new_len = old_len + cbinfo->n_added - cbinfo->n_deleted;
	struct evbuffer *out = arg;
	evbuffer_add_printf(out, "%lu->%lu; ", (unsigned long)old_len,
			    (unsigned long)new_len);
}
static void
self_draining_callback(struct evbuffer *evbuffer, size_t old_len,
		size_t new_len, void *arg)
{
	if (new_len > old_len)
		evbuffer_drain(evbuffer, new_len);
}

static void
test_evbuffer_callbacks(void *ptr)
{
	struct evbuffer *buf = evbuffer_new();
	struct evbuffer *buf_out1 = evbuffer_new();
	struct evbuffer *buf_out2 = evbuffer_new();
	struct evbuffer_cb_entry *cb1, *cb2;

	tt_assert(buf);
	tt_assert(buf_out1);
	tt_assert(buf_out2);

	cb1 = evbuffer_add_cb(buf, log_change_callback, buf_out1);
	cb2 = evbuffer_add_cb(buf, log_change_callback, buf_out2);

	/* Let's run through adding and deleting some stuff from the buffer
	 * and turning the callbacks on and off and removing them.  The callback
	 * adds a summary of length changes to buf_out1/buf_out2 when called. */
	/* size: 0-> 36. */
	evbuffer_add_printf(buf, "The %d magic words are spotty pudding", 2);
	evbuffer_validate(buf);
	evbuffer_cb_clear_flags(buf, cb2, EVBUFFER_CB_ENABLED);
	evbuffer_drain(buf, 10); /*36->26*/
	evbuffer_validate(buf);
	evbuffer_prepend(buf, "Hello", 5);/*26->31*/
	evbuffer_cb_set_flags(buf, cb2, EVBUFFER_CB_ENABLED);
	evbuffer_add_reference(buf, "Goodbye", 7, NULL, NULL); /*31->38*/
	evbuffer_remove_cb_entry(buf, cb1);
	evbuffer_validate(buf);
	evbuffer_drain(buf, evbuffer_get_length(buf)); /*38->0*/;
	tt_assert(-1 == evbuffer_remove_cb(buf, log_change_callback, NULL));
	evbuffer_add(buf, "X", 1); /* 0->1 */
	tt_assert(!evbuffer_remove_cb(buf, log_change_callback, buf_out2));
	evbuffer_validate(buf);

	tt_str_op((const char *) evbuffer_pullup(buf_out1, -1), ==,
		  "0->36; 36->26; 26->31; 31->38; ");
	tt_str_op((const char *) evbuffer_pullup(buf_out2, -1), ==,
		  "0->36; 31->38; 38->0; 0->1; ");
	evbuffer_drain(buf_out1, evbuffer_get_length(buf_out1));
	evbuffer_drain(buf_out2, evbuffer_get_length(buf_out2));
	/* Let's test the obsolete buffer_setcb function too. */
	cb1 = evbuffer_add_cb(buf, log_change_callback, buf_out1);
	tt_assert(cb1 != NULL);
	cb2 = evbuffer_add_cb(buf, log_change_callback, buf_out2);
	tt_assert(cb2 != NULL);
	evbuffer_setcb(buf, self_draining_callback, NULL);
	evbuffer_add_printf(buf, "This should get drained right away.");
	tt_uint_op(evbuffer_get_length(buf), ==, 0);
	tt_uint_op(evbuffer_get_length(buf_out1), ==, 0);
	tt_uint_op(evbuffer_get_length(buf_out2), ==, 0);
	evbuffer_setcb(buf, NULL, NULL);
	evbuffer_add_printf(buf, "This will not.");
	tt_str_op((const char *) evbuffer_pullup(buf, -1), ==, "This will not.");
	evbuffer_validate(buf);
	evbuffer_drain(buf, evbuffer_get_length(buf));
	evbuffer_validate(buf);
#if 0
	/* Now let's try a suspended callback. */
	cb1 = evbuffer_add_cb(buf, log_change_callback, buf_out1);
	cb2 = evbuffer_add_cb(buf, log_change_callback, buf_out2);
	evbuffer_cb_suspend(buf,cb2);
	evbuffer_prepend(buf,"Hello world",11); /*0->11*/
	evbuffer_validate(buf);
	evbuffer_cb_suspend(buf,cb1);
	evbuffer_add(buf,"more",4); /* 11->15 */
	evbuffer_cb_unsuspend(buf,cb2);
	evbuffer_drain(buf, 4); /* 15->11 */
	evbuffer_cb_unsuspend(buf,cb1);
	evbuffer_drain(buf, evbuffer_get_length(buf)); /* 11->0 */

	tt_str_op(evbuffer_pullup(buf_out1, -1), ==,
		  "0->11; 11->11; 11->0; ");
	tt_str_op(evbuffer_pullup(buf_out2, -1), ==,
		  "0->15; 15->11; 11->0; ");
#endif

 end:
	if (buf)
		evbuffer_free(buf);
	if (buf_out1)
		evbuffer_free(buf_out1);
	if (buf_out2)
		evbuffer_free(buf_out2);
}

static int ref_done_cb_called_count = 0;
static void *ref_done_cb_called_with = NULL;
static const void *ref_done_cb_called_with_data = NULL;
static size_t ref_done_cb_called_with_len = 0;
static void ref_done_cb(const void *data, size_t len, void *info)
{
	++ref_done_cb_called_count;
	ref_done_cb_called_with = info;
	ref_done_cb_called_with_data = data;
	ref_done_cb_called_with_len = len;
}

static void
test_evbuffer_add_reference(void *ptr)
{
	const char chunk1[] = "If you have found the answer to such a problem";
	const char chunk2[] = "you ought to write it up for publication";
			  /* -- Knuth's "Notes on the Exercises" from TAOCP */
	char tmp[16];
	size_t len1 = strlen(chunk1), len2=strlen(chunk2);

	struct evbuffer *buf1 = NULL, *buf2 = NULL;

	buf1 = evbuffer_new();
	tt_assert(buf1);

	evbuffer_add_reference(buf1, chunk1, len1, ref_done_cb, (void*)111);
	evbuffer_add(buf1, ", ", 2);
	evbuffer_add_reference(buf1, chunk2, len2, ref_done_cb, (void*)222);
	tt_int_op(evbuffer_get_length(buf1), ==, len1+len2+2);

	/* Make sure we can drain a little from a reference. */
	tt_int_op(evbuffer_remove(buf1, tmp, 6), ==, 6);
	tt_int_op(memcmp(tmp, "If you", 6), ==, 0);
	tt_int_op(evbuffer_remove(buf1, tmp, 5), ==, 5);
	tt_int_op(memcmp(tmp, " have", 5), ==, 0);

	/* Make sure that prepending does not meddle with immutable data */
	tt_int_op(evbuffer_prepend(buf1, "I have ", 7), ==, 0);
	tt_int_op(memcmp(chunk1, "If you", 6), ==, 0);
	evbuffer_validate(buf1);

	/* Make sure that when the chunk is over, the callback is invoked. */
	evbuffer_drain(buf1, 7); /* Remove prepended stuff. */
	evbuffer_drain(buf1, len1-11-1); /* remove all but one byte of chunk1 */
	tt_int_op(ref_done_cb_called_count, ==, 0);
	evbuffer_remove(buf1, tmp, 1);
	tt_int_op(tmp[0], ==, 'm');
	tt_assert(ref_done_cb_called_with == (void*)111);
	tt_assert(ref_done_cb_called_with_data == chunk1);
	tt_assert(ref_done_cb_called_with_len == len1);
	tt_int_op(ref_done_cb_called_count, ==, 1);
	evbuffer_validate(buf1);

	/* Drain some of the remaining chunk, then add it to another buffer */
	evbuffer_drain(buf1, 6); /* Remove the ", you ". */
	buf2 = evbuffer_new();
	tt_assert(buf2);
	tt_int_op(ref_done_cb_called_count, ==, 1);
	evbuffer_add(buf2, "I ", 2);

	evbuffer_add_buffer(buf2, buf1);
	tt_int_op(ref_done_cb_called_count, ==, 1);
	evbuffer_remove(buf2, tmp, 16);
	tt_int_op(memcmp("I ought to write", tmp, 16), ==, 0);
	evbuffer_drain(buf2, evbuffer_get_length(buf2));
	tt_int_op(ref_done_cb_called_count, ==, 2);
	tt_assert(ref_done_cb_called_with == (void*)222);
	evbuffer_validate(buf2);

	/* Now add more stuff to buf1 and make sure that it gets removed on
	 * free. */
	evbuffer_add(buf1, "You shake and shake the ", 24);
	evbuffer_add_reference(buf1, "ketchup bottle", 14, ref_done_cb,
	    (void*)3333);
	evbuffer_add(buf1, ". Nothing comes and then a lot'll.", 35);
	evbuffer_free(buf1);
	buf1 = NULL;
	tt_int_op(ref_done_cb_called_count, ==, 3);
	tt_assert(ref_done_cb_called_with == (void*)3333);

end:
	if (buf1)
		evbuffer_free(buf1);
	if (buf2)
		evbuffer_free(buf2);
}

static void
test_evbuffer_multicast(void *ptr)
{
	const char chunk1[] = "If you have found the answer to such a problem";
	const char chunk2[] = "you ought to write it up for publication";
			  /* -- Knuth's "Notes on the Exercises" from TAOCP */
	char tmp[16];
	size_t len1 = strlen(chunk1), len2=strlen(chunk2);

	struct evbuffer *buf1 = NULL, *buf2 = NULL;

	buf1 = evbuffer_new();
	tt_assert(buf1);

	evbuffer_add(buf1, chunk1, len1);
	evbuffer_add(buf1, ", ", 2);
	evbuffer_add(buf1, chunk2, len2);
	tt_int_op(evbuffer_get_length(buf1), ==, len1+len2+2);

	buf2 = evbuffer_new();
	tt_assert(buf2);

	tt_int_op(evbuffer_add_buffer_reference(buf2, buf1), ==, 0);
	/* nested references are not allowed */
	tt_int_op(evbuffer_add_buffer_reference(buf2, buf2), ==, -1);
	tt_int_op(evbuffer_add_buffer_reference(buf1, buf2), ==, -1);

	/* both buffers contain the same amount of data */
	tt_int_op(evbuffer_get_length(buf1), ==, evbuffer_get_length(buf1));

	/* Make sure we can drain a little from the first buffer. */
	tt_int_op(evbuffer_remove(buf1, tmp, 6), ==, 6);
	tt_int_op(memcmp(tmp, "If you", 6), ==, 0);
	tt_int_op(evbuffer_remove(buf1, tmp, 5), ==, 5);
	tt_int_op(memcmp(tmp, " have", 5), ==, 0);

	/* Make sure that prepending does not meddle with immutable data */
	tt_int_op(evbuffer_prepend(buf1, "I have ", 7), ==, 0);
	tt_int_op(memcmp(chunk1, "If you", 6), ==, 0);
	evbuffer_validate(buf1);

	/* Make sure we can drain a little from the second buffer. */
	tt_int_op(evbuffer_remove(buf2, tmp, 6), ==, 6);
	tt_int_op(memcmp(tmp, "If you", 6), ==, 0);
	tt_int_op(evbuffer_remove(buf2, tmp, 5), ==, 5);
	tt_int_op(memcmp(tmp, " have", 5), ==, 0);

	/* Make sure that prepending does not meddle with immutable data */
	tt_int_op(evbuffer_prepend(buf2, "I have ", 7), ==, 0);
	tt_int_op(memcmp(chunk1, "If you", 6), ==, 0);
	evbuffer_validate(buf2);

	/* Make sure the data can be read from the second buffer when the first is freed */
	evbuffer_free(buf1);
	buf1 = NULL;

	tt_int_op(evbuffer_remove(buf2, tmp, 6), ==, 6);
	tt_int_op(memcmp(tmp, "I have", 6), ==, 0);

	tt_int_op(evbuffer_remove(buf2, tmp, 6), ==, 6);
	tt_int_op(memcmp(tmp, "  foun", 6), ==, 0);

end:
	if (buf1)
		evbuffer_free(buf1);
	if (buf2)
		evbuffer_free(buf2);
}

static void
test_evbuffer_multicast_drain(void *ptr)
{
	const char chunk1[] = "If you have found the answer to such a problem";
	const char chunk2[] = "you ought to write it up for publication";
			  /* -- Knuth's "Notes on the Exercises" from TAOCP */
	size_t len1 = strlen(chunk1), len2=strlen(chunk2);

	struct evbuffer *buf1 = NULL, *buf2 = NULL;

	buf1 = evbuffer_new();
	tt_assert(buf1);

	evbuffer_add(buf1, chunk1, len1);
	evbuffer_add(buf1, ", ", 2);
	evbuffer_add(buf1, chunk2, len2);
	tt_int_op(evbuffer_get_length(buf1), ==, len1+len2+2);

	buf2 = evbuffer_new();
	tt_assert(buf2);

	tt_int_op(evbuffer_add_buffer_reference(buf2, buf1), ==, 0);
	tt_int_op(evbuffer_get_length(buf2), ==, len1+len2+2);
	tt_int_op(evbuffer_drain(buf1, evbuffer_get_length(buf1)), ==, 0);
	tt_int_op(evbuffer_get_length(buf2), ==, len1+len2+2);
	tt_int_op(evbuffer_drain(buf2, evbuffer_get_length(buf2)), ==, 0);
	evbuffer_validate(buf1);
	evbuffer_validate(buf2);

end:
	if (buf1)
		evbuffer_free(buf1);
	if (buf2)
		evbuffer_free(buf2);
}

/* Some cases that we didn't get in test_evbuffer() above, for more coverage. */
static void
test_evbuffer_prepend(void *ptr)
{
	struct evbuffer *buf1 = NULL, *buf2 = NULL;
	char tmp[128];
	int n;

	buf1 = evbuffer_new();
	tt_assert(buf1);

	/* Case 0: The evbuffer is entirely empty. */
	evbuffer_prepend(buf1, "This string has 29 characters", 29);
	evbuffer_validate(buf1);

	/* Case 1: Prepend goes entirely in new chunk. */
	evbuffer_prepend(buf1, "Short.", 6);
	evbuffer_validate(buf1);

	/* Case 2: prepend goes entirely in first chunk. */
	evbuffer_drain(buf1, 6+11);
	evbuffer_prepend(buf1, "it", 2);
	evbuffer_validate(buf1);
	tt_assert(!memcmp(buf1->first->buffer+buf1->first->misalign,
		"it has", 6));

	/* Case 3: prepend is split over multiple chunks. */
	evbuffer_prepend(buf1, "It is no longer true to say ", 28);
	evbuffer_validate(buf1);
	n = evbuffer_remove(buf1, tmp, sizeof(tmp)-1);
	tt_int_op(n, >=, 0);
	tmp[n]='\0';
	tt_str_op(tmp,==,"It is no longer true to say it has 29 characters");

	buf2 = evbuffer_new();
	tt_assert(buf2);

	/* Case 4: prepend a buffer to an empty buffer. */
	n = 999;
	evbuffer_add_printf(buf1, "Here is string %d. ", n++);
	evbuffer_prepend_buffer(buf2, buf1);
	evbuffer_validate(buf2);

	/* Case 5: prepend a buffer to a nonempty buffer. */
	evbuffer_add_printf(buf1, "Here is string %d. ", n++);
	evbuffer_prepend_buffer(buf2, buf1);
	evbuffer_validate(buf2);
	evbuffer_validate(buf1);
	n = evbuffer_remove(buf2, tmp, sizeof(tmp)-1);
	tt_int_op(n, >=, 0);
	tmp[n]='\0';
	tt_str_op(tmp,==,"Here is string 1000. Here is string 999. ");

end:
	if (buf1)
		evbuffer_free(buf1);
	if (buf2)
		evbuffer_free(buf2);

}

static void
test_evbuffer_peek_first_gt(void *info)
{
	struct evbuffer *buf = NULL, *tmp_buf = NULL;
	struct evbuffer_ptr ptr;
	struct evbuffer_iovec v[2];

	buf = evbuffer_new();
	tmp_buf = evbuffer_new();
	evbuffer_add_printf(tmp_buf, "Contents of chunk 100\n");
	evbuffer_add_buffer(buf, tmp_buf);
	evbuffer_add_printf(tmp_buf, "Contents of chunk 1\n");
	evbuffer_add_buffer(buf, tmp_buf);

	evbuffer_ptr_set(buf, &ptr, 0, EVBUFFER_PTR_SET);

	/** The only case that matters*/
	tt_int_op(evbuffer_peek(buf, -1, &ptr, NULL, 0), ==, 2);
	/** Just in case */
	tt_int_op(evbuffer_peek(buf, -1, &ptr, v, 2), ==, 2);

	evbuffer_ptr_set(buf, &ptr, 20, EVBUFFER_PTR_ADD);
	tt_int_op(evbuffer_peek(buf, -1, &ptr, NULL, 0), ==, 2);
	tt_int_op(evbuffer_peek(buf, -1, &ptr, v, 2), ==, 2);
	tt_int_op(evbuffer_peek(buf, 2, &ptr, NULL, 0), ==, 1);
	tt_int_op(evbuffer_peek(buf, 2, &ptr, v, 2), ==, 1);
	tt_int_op(evbuffer_peek(buf, 3, &ptr, NULL, 0), ==, 2);
	tt_int_op(evbuffer_peek(buf, 3, &ptr, v, 2), ==, 2);

end:
	if (buf)
		evbuffer_free(buf);
	if (tmp_buf)
		evbuffer_free(tmp_buf);
}

static void
test_evbuffer_peek(void *info)
{
	struct evbuffer *buf = NULL, *tmp_buf = NULL;
	int i;
	struct evbuffer_iovec v[20];
	struct evbuffer_ptr ptr;

#define tt_iov_eq(v, s)						\
	tt_int_op((v)->iov_len, ==, strlen(s));			\
	tt_assert(!memcmp((v)->iov_base, (s), strlen(s)))

	/* Let's make a very fragmented buffer. */
	buf = evbuffer_new();
	tmp_buf = evbuffer_new();
	for (i = 0; i < 16; ++i) {
		evbuffer_add_printf(tmp_buf, "Contents of chunk [%d]\n", i);
		evbuffer_add_buffer(buf, tmp_buf);
	}

	/* How many chunks do we need for everything? */
	i = evbuffer_peek(buf, -1, NULL, NULL, 0);
	tt_int_op(i, ==, 16);

	/* Simple peek: get everything. */
	i = evbuffer_peek(buf, -1, NULL, v, 20);
	tt_int_op(i, ==, 16); /* we used only 16 chunks. */
	tt_iov_eq(&v[0], "Contents of chunk [0]\n");
	tt_iov_eq(&v[3], "Contents of chunk [3]\n");
	tt_iov_eq(&v[12], "Contents of chunk [12]\n");
	tt_iov_eq(&v[15], "Contents of chunk [15]\n");

	/* Just get one chunk worth. */
	memset(v, 0, sizeof(v));
	i = evbuffer_peek(buf, -1, NULL, v, 1);
	tt_int_op(i, ==, 1);
	tt_iov_eq(&v[0], "Contents of chunk [0]\n");
	tt_assert(v[1].iov_base == NULL);

	/* Suppose we want at least the first 40 bytes. */
	memset(v, 0, sizeof(v));
	i = evbuffer_peek(buf, 40, NULL, v, 16);
	tt_int_op(i, ==, 2);
	tt_iov_eq(&v[0], "Contents of chunk [0]\n");
	tt_iov_eq(&v[1], "Contents of chunk [1]\n");
	tt_assert(v[2].iov_base == NULL);

	/* How many chunks do we need for 100 bytes? */
	memset(v, 0, sizeof(v));
	i = evbuffer_peek(buf, 100, NULL, NULL, 0);
	tt_int_op(i, ==, 5);
	tt_assert(v[0].iov_base == NULL);

	/* Now we ask for more bytes than we provide chunks for */
	memset(v, 0, sizeof(v));
	i = evbuffer_peek(buf, 60, NULL, v, 1);
	tt_int_op(i, ==, 3);
	tt_iov_eq(&v[0], "Contents of chunk [0]\n");
	tt_assert(v[1].iov_base == NULL);

	/* Now we ask for more bytes than the buffer has. */
	memset(v, 0, sizeof(v));
	i = evbuffer_peek(buf, 65536, NULL, v, 20);
	tt_int_op(i, ==, 16); /* we used only 16 chunks. */
	tt_iov_eq(&v[0], "Contents of chunk [0]\n");
	tt_iov_eq(&v[3], "Contents of chunk [3]\n");
	tt_iov_eq(&v[12], "Contents of chunk [12]\n");
	tt_iov_eq(&v[15], "Contents of chunk [15]\n");
	tt_assert(v[16].iov_base == NULL);

	/* What happens if we try an empty buffer? */
	memset(v, 0, sizeof(v));
	i = evbuffer_peek(tmp_buf, -1, NULL, v, 20);
	tt_int_op(i, ==, 0);
	tt_assert(v[0].iov_base == NULL);
	memset(v, 0, sizeof(v));
	i = evbuffer_peek(tmp_buf, 50, NULL, v, 20);
	tt_int_op(i, ==, 0);
	tt_assert(v[0].iov_base == NULL);

	/* Okay, now time to have fun with pointers. */
	memset(v, 0, sizeof(v));
	evbuffer_ptr_set(buf, &ptr, 30, EVBUFFER_PTR_SET);
	i = evbuffer_peek(buf, 50, &ptr, v, 20);
	tt_int_op(i, ==, 3);
	tt_iov_eq(&v[0], " of chunk [1]\n");
	tt_iov_eq(&v[1], "Contents of chunk [2]\n");
	tt_iov_eq(&v[2], "Contents of chunk [3]\n"); /*more than we asked for*/

	/* advance to the start of another chain. */
	memset(v, 0, sizeof(v));
	evbuffer_ptr_set(buf, &ptr, 14, EVBUFFER_PTR_ADD);
	i = evbuffer_peek(buf, 44, &ptr, v, 20);
	tt_int_op(i, ==, 2);
	tt_iov_eq(&v[0], "Contents of chunk [2]\n");
	tt_iov_eq(&v[1], "Contents of chunk [3]\n"); /*more than we asked for*/

	/* peek at the end of the buffer */
	memset(v, 0, sizeof(v));
	tt_assert(evbuffer_ptr_set(buf, &ptr, evbuffer_get_length(buf), EVBUFFER_PTR_SET) == 0);
	i = evbuffer_peek(buf, 44, &ptr, v, 20);
	tt_int_op(i, ==, 0);
	tt_assert(v[0].iov_base == NULL);

end:
	if (buf)
		evbuffer_free(buf);
	if (tmp_buf)
		evbuffer_free(tmp_buf);
}

/* Check whether evbuffer freezing works right.  This is called twice,
   once with the argument "start" and once with the argument "end".
   When we test "start", we freeze the start of an evbuffer and make sure
   that modifying the start of the buffer doesn't work.  When we test
   "end", we freeze the end of an evbuffer and make sure that modifying
   the end of the buffer doesn't work.
 */
static void
test_evbuffer_freeze(void *ptr)
{
	struct evbuffer *buf = NULL, *tmp_buf=NULL;
	const char string[] = /* Year's End, Richard Wilbur */
	    "I've known the wind by water banks to shake\n"
	    "The late leaves down, which frozen where they fell\n"
	    "And held in ice as dancers in a spell\n"
	    "Fluttered all winter long into a lake...";
	const int start = !strcmp(ptr, "start");
	char *cp;
	char charbuf[128];
	int r;
	size_t orig_length;
	struct evbuffer_iovec v[1];

	if (!start)
		tt_str_op(ptr, ==, "end");

	buf = evbuffer_new();
	tmp_buf = evbuffer_new();
	tt_assert(tmp_buf);

	evbuffer_add(buf, string, strlen(string));
	evbuffer_freeze(buf, start); /* Freeze the start or the end.*/

#define FREEZE_EQ(a, startcase, endcase)		\
	do {						\
	    if (start) {				\
		    tt_int_op((a), ==, (startcase));	\
	    } else {					\
		    tt_int_op((a), ==, (endcase));	\
	    }						\
	} while (0)


	orig_length = evbuffer_get_length(buf);

	/* These functions all manipulate the end of buf. */
	r = evbuffer_add(buf, "abc", 0);
	FREEZE_EQ(r, 0, -1);
	r = evbuffer_reserve_space(buf, 10, v, 1);
	FREEZE_EQ(r, 1, -1);
	if (r == 1) {
		memset(v[0].iov_base, 'X', 10);
		v[0].iov_len = 10;
	}
	r = evbuffer_commit_space(buf, v, 1);
	FREEZE_EQ(r, 0, -1);
	r = evbuffer_add_reference(buf, string, 5, NULL, NULL);
	FREEZE_EQ(r, 0, -1);
	r = evbuffer_add_printf(buf, "Hello %s", "world");
	FREEZE_EQ(r, 11, -1);
	/* TODO: test add_buffer, add_file, read */

	if (!start)
		tt_int_op(orig_length, ==, evbuffer_get_length(buf));

	orig_length = evbuffer_get_length(buf);

	/* These functions all manipulate the start of buf. */
	r = evbuffer_remove(buf, charbuf, 1);
	FREEZE_EQ(r, -1, 1);
	r = evbuffer_drain(buf, 3);
	FREEZE_EQ(r, -1, 0);
	r = evbuffer_prepend(buf, "dummy", 5);
	FREEZE_EQ(r, -1, 0);
	cp = evbuffer_readln(buf, NULL, EVBUFFER_EOL_LF);
	FREEZE_EQ(cp==NULL, 1, 0);
	if (cp)
		free(cp);
	/* TODO: Test remove_buffer, add_buffer, write, prepend_buffer */

	if (start)
		tt_int_op(orig_length, ==, evbuffer_get_length(buf));

end:
	if (buf)
		evbuffer_free(buf);

	if (tmp_buf)
		evbuffer_free(tmp_buf);
}

static void
test_evbuffer_add_iovec(void * ptr)
{
	struct evbuffer * buf = NULL;
	struct evbuffer_iovec vec[4];
	const char * data[] = {
		"Guilt resembles a sword with two edges.",
		"On the one hand, it cuts for Justice, imposing practical morality upon those who fear it.",
		"Conscience does not always adhere to rational judgment.",
		"Guilt is always a self-imposed burden, but it is not always rightly imposed."
		/* -- R.A. Salvatore, _Sojurn_ */
	};
	size_t expected_length = 0;
	size_t returned_length = 0;
	int i;

	buf = evbuffer_new();

	tt_assert(buf);

	for (i = 0; i < 4; i++) {
		vec[i].iov_len  = strlen(data[i]);
		vec[i].iov_base = (char*) data[i];
		expected_length += vec[i].iov_len;
	}

	returned_length = evbuffer_add_iovec(buf, vec, 4);

	tt_int_op(returned_length, ==, evbuffer_get_length(buf));
	tt_int_op(evbuffer_get_length(buf), ==, expected_length);

	for (i = 0; i < 4; i++) {
		char charbuf[1024];

		memset(charbuf, 0, 1024);
		evbuffer_remove(buf, charbuf, strlen(data[i]));
		tt_assert(strcmp(charbuf, data[i]) == 0);
	}

	tt_assert(evbuffer_get_length(buf) == 0);
end:
	if (buf) {
		evbuffer_free(buf);
	}
}

static void
test_evbuffer_copyout(void *dummy)
{
	const char string[] =
	    "Still they skirmish to and fro, men my messmates on the snow "
	    "When we headed off the aurochs turn for turn; "
	    "When the rich Allobrogenses never kept amanuenses, "
	    "And our only plots were piled in lakes at Berne.";
	/* -- Kipling, "In The Neolithic Age" */
	char tmp[1024];
	struct evbuffer_ptr ptr;
	struct evbuffer *buf;

	(void)dummy;

	buf = evbuffer_new();
	tt_assert(buf);

	tt_int_op(strlen(string), ==, 206);

	/* Ensure separate chains */
	evbuffer_add_reference(buf, string, 80, no_cleanup, NULL);
	evbuffer_add_reference(buf, string+80, 80, no_cleanup, NULL);
	evbuffer_add(buf, string+160, strlen(string)-160);

	tt_int_op(206, ==, evbuffer_get_length(buf));

	/* First, let's test plain old copyout. */

	/* Copy a little from the beginning. */
	tt_int_op(10, ==, evbuffer_copyout(buf, tmp, 10));
	tt_int_op(0, ==, memcmp(tmp, "Still they", 10));

	/* Now copy more than a little from the beginning */
	memset(tmp, 0, sizeof(tmp));
	tt_int_op(100, ==, evbuffer_copyout(buf, tmp, 100));
	tt_int_op(0, ==, memcmp(tmp, string, 100));

	/* Copy too much; ensure truncation. */
	memset(tmp, 0, sizeof(tmp));
	tt_int_op(206, ==, evbuffer_copyout(buf, tmp, 230));
	tt_int_op(0, ==, memcmp(tmp, string, 206));

	/* That was supposed to be nondestructive, btw */
	tt_int_op(206, ==, evbuffer_get_length(buf));

	/* Now it's time to test copyout_from!  First, let's start in the
	 * first chain. */
	evbuffer_ptr_set(buf, &ptr, 15, EVBUFFER_PTR_SET);
	memset(tmp, 0, sizeof(tmp));
	tt_int_op(10, ==, evbuffer_copyout_from(buf, &ptr, tmp, 10));
	tt_int_op(0, ==, memcmp(tmp, "mish to an", 10));

	/* Right up to the end of the first chain */
	memset(tmp, 0, sizeof(tmp));
	tt_int_op(65, ==, evbuffer_copyout_from(buf, &ptr, tmp, 65));
	tt_int_op(0, ==, memcmp(tmp, string+15, 65));

	/* Span into the second chain */
	memset(tmp, 0, sizeof(tmp));
	tt_int_op(90, ==, evbuffer_copyout_from(buf, &ptr, tmp, 90));
	tt_int_op(0, ==, memcmp(tmp, string+15, 90));

	/* Span into the third chain */
	memset(tmp, 0, sizeof(tmp));
	tt_int_op(160, ==, evbuffer_copyout_from(buf, &ptr, tmp, 160));
	tt_int_op(0, ==, memcmp(tmp, string+15, 160));

	/* Overrun */
	memset(tmp, 0, sizeof(tmp));
	tt_int_op(206-15, ==, evbuffer_copyout_from(buf, &ptr, tmp, 999));
	tt_int_op(0, ==, memcmp(tmp, string+15, 206-15));

	/* That was supposed to be nondestructive, too */
	tt_int_op(206, ==, evbuffer_get_length(buf));

end:
	if (buf)
		evbuffer_free(buf);
}

static void *
setup_passthrough(const struct testcase_t *testcase)
{
	return testcase->setup_data;
}
static int
cleanup_passthrough(const struct testcase_t *testcase, void *ptr)
{
	(void) ptr;
	return 1;
}

static const struct testcase_setup_t nil_setup = {
	setup_passthrough,
	cleanup_passthrough
};

struct testcase_t evbuffer_testcases[] = {
	{ "evbuffer", test_evbuffer, 0, NULL, NULL },
	{ "remove_buffer_with_empty", test_evbuffer_remove_buffer_with_empty, 0, NULL, NULL },
	{ "reserve2", test_evbuffer_reserve2, 0, NULL, NULL },
	{ "reserve_many", test_evbuffer_reserve_many, 0, NULL, NULL },
	{ "reserve_many2", test_evbuffer_reserve_many, 0, &nil_setup, (void*)"add" },
	{ "reserve_many3", test_evbuffer_reserve_many, 0, &nil_setup, (void*)"fill" },
	{ "expand", test_evbuffer_expand, 0, NULL, NULL },
	{ "reference", test_evbuffer_reference, 0, NULL, NULL },
	{ "iterative", test_evbuffer_iterative, 0, NULL, NULL },
	{ "readln", test_evbuffer_readln, TT_NO_LOGS, &basic_setup, NULL },
	{ "search_eol", test_evbuffer_search_eol, 0, NULL, NULL },
	{ "find", test_evbuffer_find, 0, NULL, NULL },
	{ "ptr_set", test_evbuffer_ptr_set, 0, NULL, NULL },
	{ "search", test_evbuffer_search, 0, NULL, NULL },
	{ "callbacks", test_evbuffer_callbacks, 0, NULL, NULL },
	{ "add_reference", test_evbuffer_add_reference, 0, NULL, NULL },
	{ "multicast", test_evbuffer_multicast, 0, NULL, NULL },
	{ "multicast_drain", test_evbuffer_multicast_drain, 0, NULL, NULL },
	{ "prepend", test_evbuffer_prepend, TT_FORK, NULL, NULL },
	{ "peek", test_evbuffer_peek, 0, NULL, NULL },
	{ "peek_first_gt", test_evbuffer_peek_first_gt, 0, NULL, NULL },
	{ "freeze_start", test_evbuffer_freeze, 0, &nil_setup, (void*)"start" },
	{ "freeze_end", test_evbuffer_freeze, 0, &nil_setup, (void*)"end" },
	{ "add_iovec", test_evbuffer_add_iovec, 0, NULL, NULL},
	{ "copyout", test_evbuffer_copyout, 0, NULL, NULL},
	{ "file_segment_add_cleanup_cb", test_evbuffer_file_segment_add_cleanup_cb, 0, NULL, NULL },

#define ADDFILE_TEST(name, parameters)					\
	{ name, test_evbuffer_add_file, TT_FORK|TT_NEED_BASE,		\
	  &basic_setup, (void*)(parameters) }

#define ADDFILE_TEST_GROUP(name, parameters)			\
	ADDFILE_TEST(name "_sendfile", "sendfile " parameters), \
	ADDFILE_TEST(name "_mmap", "mmap " parameters),		\
	ADDFILE_TEST(name "_linear", "linear " parameters)

	ADDFILE_TEST_GROUP("add_file", ""),
	ADDFILE_TEST("add_file_nosegment", "default nosegment"),

	ADDFILE_TEST_GROUP("add_big_file", "bigfile"),
	ADDFILE_TEST("add_big_file_nosegment", "default nosegment bigfile"),

	ADDFILE_TEST_GROUP("add_file_offset", "bigfile map_offset"),
	ADDFILE_TEST("add_file_offset_nosegment",
	    "default nosegment bigfile map_offset"),

	ADDFILE_TEST_GROUP("add_file_offset2", "bigfile offset_in_segment"),

	ADDFILE_TEST_GROUP("add_file_offset3",
	    "bigfile offset_in_segment map_offset"),

	END_OF_TESTCASES
};
