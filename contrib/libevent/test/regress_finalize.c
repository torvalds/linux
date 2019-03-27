/*
 * Copyright (c) 2013 Niels Provos and Nick Mathewson
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

#include "event2/event-config.h"
#include "evconfig-private.h"
#include "tinytest.h"
#include "tinytest_macros.h"
#include <stdlib.h>

#include "event2/event.h"
#include "event2/util.h"
#include "event-internal.h"
#include "defer-internal.h"

#include "regress.h"
#include "regress_thread.h"

static void
timer_callback(evutil_socket_t fd, short what, void *arg)
{
	int *int_arg = arg;
	*int_arg += 1;
	(void)fd;
	(void)what;
}
static void
simple_callback(struct event_callback *evcb, void *arg)
{
	int *int_arg = arg;
        *int_arg += 1;
	(void)evcb;
}
static void
event_finalize_callback_1(struct event *ev, void *arg)
{
	int *int_arg = arg;
        *int_arg += 100;
	(void)ev;
}
static void
callback_finalize_callback_1(struct event_callback *evcb, void *arg)
{
	int *int_arg = arg;
        *int_arg += 100;
	(void)evcb;
}


static void
test_fin_cb_invoked(void *arg)
{
	struct basic_test_data *data = arg;
	struct event_base *base = data->base;

	struct event *ev;
	struct event ev2;
	struct event_callback evcb;
	int cb_called = 0;
	int ev_called = 0;

	const struct timeval ten_sec = {10,0};

	event_deferred_cb_init_(&evcb, 0, simple_callback, &cb_called);
	ev = evtimer_new(base, timer_callback, &ev_called);
	/* Just finalize them; don't bother adding. */
	event_free_finalize(0, ev, event_finalize_callback_1);
	event_callback_finalize_(base, 0, &evcb, callback_finalize_callback_1);

	event_base_dispatch(base);

	tt_int_op(cb_called, ==, 100);
	tt_int_op(ev_called, ==, 100);

	ev_called = cb_called = 0;
	event_base_assert_ok_(base);

	/* Now try it when they're active. (actually, don't finalize: make
	 * sure activation can happen! */
	ev = evtimer_new(base, timer_callback, &ev_called);
	event_deferred_cb_init_(&evcb, 0, simple_callback, &cb_called);

	event_active(ev, EV_TIMEOUT, 1);
	event_callback_activate_(base, &evcb);

	event_base_dispatch(base);
	tt_int_op(cb_called, ==, 1);
	tt_int_op(ev_called, ==, 1);

	ev_called = cb_called = 0;
	event_base_assert_ok_(base);

	/* Great, it worked. Now activate and finalize and make sure only
	 * finalizing happens. */
	event_active(ev, EV_TIMEOUT, 1);
	event_callback_activate_(base, &evcb);
	event_free_finalize(0, ev, event_finalize_callback_1);
	event_callback_finalize_(base, 0, &evcb, callback_finalize_callback_1);

	event_base_dispatch(base);
	tt_int_op(cb_called, ==, 100);
	tt_int_op(ev_called, ==, 100);

	ev_called = 0;

	event_base_assert_ok_(base);

	/* Okay, now add but don't have it become active, and make sure *that*
	 * works. */
	ev = evtimer_new(base, timer_callback, &ev_called);
	event_add(ev, &ten_sec);
	event_free_finalize(0, ev, event_finalize_callback_1);

	event_base_dispatch(base);
	tt_int_op(ev_called, ==, 100);

	ev_called = 0;
	event_base_assert_ok_(base);

	/* Now try adding and deleting after finalizing. */
	ev = evtimer_new(base, timer_callback, &ev_called);
	evtimer_assign(&ev2, base, timer_callback, &ev_called);
	event_add(ev, &ten_sec);
	event_free_finalize(0, ev, event_finalize_callback_1);
	event_finalize(0, &ev2, event_finalize_callback_1);

	event_add(&ev2, &ten_sec);
	event_del(ev);
	event_active(&ev2, EV_TIMEOUT, 1);

	event_base_dispatch(base);
	tt_int_op(ev_called, ==, 200);

	event_base_assert_ok_(base);

end:
	;
}

#ifndef EVENT__DISABLE_MM_REPLACEMENT
static void *
tfff_malloc(size_t n)
{
	return malloc(n);
}
static void *tfff_p1=NULL, *tfff_p2=NULL;
static int tfff_p1_freed=0, tfff_p2_freed=0;
static void
tfff_free(void *p)
{
	if (! p)
		return;
	if (p == tfff_p1)
		++tfff_p1_freed;
	if (p == tfff_p2)
		++tfff_p2_freed;
	free(p);
}
static void *
tfff_realloc(void *p, size_t sz)
{
	return realloc(p,sz);
}
#endif

static void
test_fin_free_finalize(void *arg)
{
#ifdef EVENT__DISABLE_MM_REPLACEMENT
	tinytest_set_test_skipped_();
#else
	struct event_base *base = NULL;
	struct event *ev, *ev2;
	int ev_called = 0;
	int ev2_called = 0;

	(void)arg;

	event_set_mem_functions(tfff_malloc, tfff_realloc, tfff_free);

	base = event_base_new();
	tt_assert(base);

	ev = evtimer_new(base, timer_callback, &ev_called);
	ev2 = evtimer_new(base, timer_callback, &ev2_called);
	tfff_p1 = ev;
	tfff_p2 = ev2;
	event_free_finalize(0, ev, event_finalize_callback_1);
	event_finalize(0, ev2, event_finalize_callback_1);

	event_base_dispatch(base);

	tt_int_op(ev_called, ==, 100);
	tt_int_op(ev2_called, ==, 100);

	event_base_assert_ok_(base);
	tt_int_op(tfff_p1_freed, ==, 1);
	tt_int_op(tfff_p2_freed, ==, 0);

	event_free(ev2);

end:
	if (base)
		event_base_free(base);
#endif
}

/* For test_fin_within_cb */
struct event_and_count {
	struct event *ev;
	struct event *ev2;
	int count;
};
static void
event_finalize_callback_2(struct event *ev, void *arg)
{
	struct event_and_count *evc = arg;
	evc->count += 100;
	event_free(ev);
}
static void
timer_callback_2(evutil_socket_t fd, short what, void *arg)
{
	struct event_and_count *evc = arg;
	event_finalize(0, evc->ev, event_finalize_callback_2);
	event_finalize(0, evc->ev2, event_finalize_callback_2);
	++ evc->count;
	(void)fd;
	(void)what;
}

static void
test_fin_within_cb(void *arg)
{
	struct basic_test_data *data = arg;
	struct event_base *base = data->base;

	struct event_and_count evc1, evc2;
	evc1.count = evc2.count = 0;
	evc2.ev2 = evc1.ev = evtimer_new(base, timer_callback_2, &evc1);
	evc1.ev2 = evc2.ev = evtimer_new(base, timer_callback_2, &evc2);

	/* Activate both.  The first one will have its callback run, which
	 * will finalize both of them, preventing the second one's callback
	 * from running. */
	event_active(evc1.ev, EV_TIMEOUT, 1);
	event_active(evc2.ev, EV_TIMEOUT, 1);

	event_base_dispatch(base);
	tt_int_op(evc1.count, ==, 101);
	tt_int_op(evc2.count, ==, 100);

	event_base_assert_ok_(base);
	/* Now try with EV_PERSIST events. */
	evc1.count = evc2.count = 0;
	evc2.ev2 = evc1.ev = event_new(base, -1, EV_PERSIST, timer_callback_2, &evc1);
	evc1.ev2 = evc2.ev = event_new(base, -1, EV_PERSIST, timer_callback_2, &evc2);

	event_active(evc1.ev, EV_TIMEOUT, 1);
	event_active(evc2.ev, EV_TIMEOUT, 1);

	event_base_dispatch(base);
	tt_int_op(evc1.count, ==, 101);
	tt_int_op(evc2.count, ==, 100);

	event_base_assert_ok_(base);
end:
	;
}

#if 0
static void
timer_callback_3(evutil_socket_t *fd, short what, void *arg)
{
	(void)fd;
	(void)what;

}
static void
test_fin_many(void *arg)
{
	struct basic_test_data *data = arg;
	struct event_base *base = data->base;

	struct event *ev1, *ev2;
	struct event_callback evcb1, evcb2;
	int ev1_count = 0, ev2_count = 0;
	int evcb1_count = 0, evcb2_count = 0;
	struct event_callback *array[4];

	int n;

	/* First attempt: call finalize_many with no events running */
	ev1 = evtimer_new(base, timer_callback, &ev1_count);
	ev1 = evtimer_new(base, timer_callback, &ev2_count);
	event_deferred_cb_init_(&evcb1, 0, simple_callback, &evcb1_called);
	event_deferred_cb_init_(&evcb2, 0, simple_callback, &evcb2_called);
	array[0] = &ev1->ev_evcallback;
	array[1] = &ev2->ev_evcallback;
	array[2] = &evcb1;
	array[3] = &evcb2;

	

	n = event_callback_finalize_many(base, 4, array,
	    callback_finalize_callback_1);

}
#endif


#define TEST(name, flags)					\
	{ #name, test_fin_##name, (flags), &basic_setup, NULL }

struct testcase_t finalize_testcases[] = {

	TEST(cb_invoked, TT_FORK|TT_NEED_BASE),
	TEST(free_finalize, TT_FORK),
	TEST(within_cb, TT_FORK|TT_NEED_BASE),
//	TEST(many, TT_FORK|TT_NEED_BASE),


	END_OF_TESTCASES
};

