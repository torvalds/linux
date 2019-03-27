/*
 * Copyright (C) 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id$ */

/*! \file */

#include <config.h>

#include <atf-c.h>

#include <unistd.h>

#include <isc/task.h>
#include <isc/util.h>

#include "../task_p.h"
#include "isctest.h"

/*
 * Helper functions
 */

/* task event handler, sets a boolean to true */
int counter = 0;
isc_mutex_t set_lock;

static void
set(isc_task_t *task, isc_event_t *event) {
	int *value = (int *) event->ev_arg;

	UNUSED(task);

	isc_event_free(&event);
	LOCK(&set_lock);
	*value = counter++;
	UNLOCK(&set_lock);
}

static void
set_and_drop(isc_task_t *task, isc_event_t *event) {
	int *value = (int *) event->ev_arg;

	UNUSED(task);

	isc_event_free(&event);
	LOCK(&set_lock);
	*value = (int) isc_taskmgr_mode(taskmgr);
	counter++;
	UNLOCK(&set_lock);
	isc_taskmgr_setmode(taskmgr, isc_taskmgrmode_normal);
}

/*
 * Individual unit tests
 */

/* Create a task */
ATF_TC(create_task);
ATF_TC_HEAD(create_task, tc) {
	atf_tc_set_md_var(tc, "descr", "create and destroy a task");
}
ATF_TC_BODY(create_task, tc) {
	isc_result_t result;
	isc_task_t *task = NULL;

	UNUSED(tc);

	result = isc_test_begin(NULL, ISC_TRUE);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	result = isc_task_create(taskmgr, 0, &task);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	isc_task_destroy(&task);
	ATF_REQUIRE_EQ(task, NULL);

	isc_test_end();
}

/* Process events */
ATF_TC(all_events);
ATF_TC_HEAD(all_events, tc) {
	atf_tc_set_md_var(tc, "descr", "process task events");
}
ATF_TC_BODY(all_events, tc) {
	isc_result_t result;
	isc_task_t *task = NULL;
	isc_event_t *event;
	int a = 0, b = 0;
	int i = 0;

	UNUSED(tc);

	counter = 1;

	result = isc_mutex_init(&set_lock);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	result = isc_test_begin(NULL, ISC_TRUE);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	result = isc_task_create(taskmgr, 0, &task);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	/* First event */
	event = isc_event_allocate(mctx, task, ISC_TASKEVENT_TEST,
				   set, &a, sizeof (isc_event_t));
	ATF_REQUIRE(event != NULL);

	ATF_CHECK_EQ(a, 0);
	isc_task_send(task, &event);

	event = isc_event_allocate(mctx, task, ISC_TASKEVENT_TEST,
				   set, &b, sizeof (isc_event_t));
	ATF_REQUIRE(event != NULL);

	ATF_CHECK_EQ(b, 0);
	isc_task_send(task, &event);

	while ((a == 0 || b == 0) && i++ < 5000) {
#ifndef ISC_PLATFORM_USETHREADS
		while (isc__taskmgr_ready(taskmgr))
			isc__taskmgr_dispatch(taskmgr);
#endif
		isc_test_nap(1000);
	}

	ATF_CHECK(a != 0);
	ATF_CHECK(b != 0);

	isc_task_destroy(&task);
	ATF_REQUIRE_EQ(task, NULL);

	isc_test_end();
}

/* Privileged events */
ATF_TC(privileged_events);
ATF_TC_HEAD(privileged_events, tc) {
	atf_tc_set_md_var(tc, "descr", "process privileged events");
}
ATF_TC_BODY(privileged_events, tc) {
	isc_result_t result;
	isc_task_t *task1 = NULL, *task2 = NULL;
	isc_event_t *event;
	int a = 0, b = 0, c = 0, d = 0, e = 0;
	int i = 0;

	UNUSED(tc);

	counter = 1;
	result = isc_mutex_init(&set_lock);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	result = isc_test_begin(NULL, ISC_TRUE);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

#ifdef ISC_PLATFORM_USETHREADS
	/*
	 * Pause the task manager so we can fill up the work queue
	 * without things happening while we do it.
	 */
	isc__taskmgr_pause(taskmgr);
#endif

	result = isc_task_create(taskmgr, 0, &task1);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);
	isc_task_setname(task1, "privileged", NULL);
	ATF_CHECK(!isc_task_privilege(task1));
	isc_task_setprivilege(task1, ISC_TRUE);
	ATF_CHECK(isc_task_privilege(task1));

	result = isc_task_create(taskmgr, 0, &task2);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);
	isc_task_setname(task2, "normal", NULL);
	ATF_CHECK(!isc_task_privilege(task2));

	/* First event: privileged */
	event = isc_event_allocate(mctx, task1, ISC_TASKEVENT_TEST,
				   set, &a, sizeof (isc_event_t));
	ATF_REQUIRE(event != NULL);

	ATF_CHECK_EQ(a, 0);
	isc_task_send(task1, &event);

	/* Second event: not privileged */
	event = isc_event_allocate(mctx, task2, ISC_TASKEVENT_TEST,
				   set, &b, sizeof (isc_event_t));
	ATF_REQUIRE(event != NULL);

	ATF_CHECK_EQ(b, 0);
	isc_task_send(task2, &event);

	/* Third event: privileged */
	event = isc_event_allocate(mctx, task1, ISC_TASKEVENT_TEST,
				   set, &c, sizeof (isc_event_t));
	ATF_REQUIRE(event != NULL);

	ATF_CHECK_EQ(c, 0);
	isc_task_send(task1, &event);

	/* Fourth event: privileged */
	event = isc_event_allocate(mctx, task1, ISC_TASKEVENT_TEST,
				   set, &d, sizeof (isc_event_t));
	ATF_REQUIRE(event != NULL);

	ATF_CHECK_EQ(d, 0);
	isc_task_send(task1, &event);

	/* Fifth event: not privileged */
	event = isc_event_allocate(mctx, task2, ISC_TASKEVENT_TEST,
				   set, &e, sizeof (isc_event_t));
	ATF_REQUIRE(event != NULL);

	ATF_CHECK_EQ(e, 0);
	isc_task_send(task2, &event);

	ATF_CHECK_EQ(isc_taskmgr_mode(taskmgr), isc_taskmgrmode_normal);
	isc_taskmgr_setmode(taskmgr, isc_taskmgrmode_privileged);
	ATF_CHECK_EQ(isc_taskmgr_mode(taskmgr), isc_taskmgrmode_privileged);

#ifdef ISC_PLATFORM_USETHREADS
	isc__taskmgr_resume(taskmgr);
#endif

	/* We're waiting for *all* variables to be set */
	while ((a == 0 || b == 0 || c == 0 || d == 0 || e == 0) && i++ < 5000) {
#ifndef ISC_PLATFORM_USETHREADS
		while (isc__taskmgr_ready(taskmgr))
			isc__taskmgr_dispatch(taskmgr);
#endif
		isc_test_nap(1000);
	}

	/*
	 * We can't guarantee what order the events fire, but
	 * we do know the privileged tasks that set a, c, and d
	 * would have fired first.
	 */
	ATF_CHECK(a <= 3);
	ATF_CHECK(c <= 3);
	ATF_CHECK(d <= 3);

	/* ...and the non-privileged tasks that set b and e, last */
	ATF_CHECK(b >= 4);
	ATF_CHECK(e >= 4);

	ATF_CHECK_EQ(counter, 6);

	isc_task_setprivilege(task1, ISC_FALSE);
	ATF_CHECK(!isc_task_privilege(task1));

	ATF_CHECK_EQ(isc_taskmgr_mode(taskmgr), isc_taskmgrmode_normal);

	isc_task_destroy(&task1);
	ATF_REQUIRE_EQ(task1, NULL);
	isc_task_destroy(&task2);
	ATF_REQUIRE_EQ(task2, NULL);

	isc_test_end();
}

/*
 * Edge case: this tests that the task manager behaves as expected when
 * we explicitly set it into normal mode *while* running privileged.
 */
ATF_TC(privilege_drop);
ATF_TC_HEAD(privilege_drop, tc) {
	atf_tc_set_md_var(tc, "descr", "process privileged events");
}
ATF_TC_BODY(privilege_drop, tc) {
	isc_result_t result;
	isc_task_t *task1 = NULL, *task2 = NULL;
	isc_event_t *event;
	int a = -1, b = -1, c = -1, d = -1, e = -1;	/* non valid states */
	int i = 0;

	UNUSED(tc);

	counter = 1;
	result = isc_mutex_init(&set_lock);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	result = isc_test_begin(NULL, ISC_TRUE);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

#ifdef ISC_PLATFORM_USETHREADS
	/*
	 * Pause the task manager so we can fill up the work queue
	 * without things happening while we do it.
	 */
	isc__taskmgr_pause(taskmgr);
#endif

	result = isc_task_create(taskmgr, 0, &task1);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);
	isc_task_setname(task1, "privileged", NULL);
	ATF_CHECK(!isc_task_privilege(task1));
	isc_task_setprivilege(task1, ISC_TRUE);
	ATF_CHECK(isc_task_privilege(task1));

	result = isc_task_create(taskmgr, 0, &task2);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);
	isc_task_setname(task2, "normal", NULL);
	ATF_CHECK(!isc_task_privilege(task2));

	/* First event: privileged */
	event = isc_event_allocate(mctx, task1, ISC_TASKEVENT_TEST,
				   set_and_drop, &a, sizeof (isc_event_t));
	ATF_REQUIRE(event != NULL);

	ATF_CHECK_EQ(a, -1);
	isc_task_send(task1, &event);

	/* Second event: not privileged */
	event = isc_event_allocate(mctx, task2, ISC_TASKEVENT_TEST,
				   set_and_drop, &b, sizeof (isc_event_t));
	ATF_REQUIRE(event != NULL);

	ATF_CHECK_EQ(b, -1);
	isc_task_send(task2, &event);

	/* Third event: privileged */
	event = isc_event_allocate(mctx, task1, ISC_TASKEVENT_TEST,
				   set_and_drop, &c, sizeof (isc_event_t));
	ATF_REQUIRE(event != NULL);

	ATF_CHECK_EQ(c, -1);
	isc_task_send(task1, &event);

	/* Fourth event: privileged */
	event = isc_event_allocate(mctx, task1, ISC_TASKEVENT_TEST,
				   set_and_drop, &d, sizeof (isc_event_t));
	ATF_REQUIRE(event != NULL);

	ATF_CHECK_EQ(d, -1);
	isc_task_send(task1, &event);

	/* Fifth event: not privileged */
	event = isc_event_allocate(mctx, task2, ISC_TASKEVENT_TEST,
				   set_and_drop, &e, sizeof (isc_event_t));
	ATF_REQUIRE(event != NULL);

	ATF_CHECK_EQ(e, -1);
	isc_task_send(task2, &event);

	ATF_CHECK_EQ(isc_taskmgr_mode(taskmgr), isc_taskmgrmode_normal);
	isc_taskmgr_setmode(taskmgr, isc_taskmgrmode_privileged);
	ATF_CHECK_EQ(isc_taskmgr_mode(taskmgr), isc_taskmgrmode_privileged);

#ifdef ISC_PLATFORM_USETHREADS
	isc__taskmgr_resume(taskmgr);
#endif

	/* We're waiting for all variables to be set. */
	while ((a == -1 || b == -1 || c == -1 || d == -1 || e == -1) &&
	       i++ < 5000) {
#ifndef ISC_PLATFORM_USETHREADS
		while (isc__taskmgr_ready(taskmgr))
			isc__taskmgr_dispatch(taskmgr);
#endif
		isc_test_nap(1000);
	}

	/*
	 * We can't guarantee what order the events fire, but
	 * we do know *exactly one* of the privileged tasks will
	 * have run in privileged mode...
	 */
	ATF_CHECK(a == isc_taskmgrmode_privileged ||
		  c == isc_taskmgrmode_privileged ||
		  d == isc_taskmgrmode_privileged);
	ATF_CHECK(a + c + d == isc_taskmgrmode_privileged);

	/* ...and neither of the non-privileged tasks did... */
	ATF_CHECK(b == isc_taskmgrmode_normal || e == isc_taskmgrmode_normal);

	/* ...but all five of them did run. */
	ATF_CHECK_EQ(counter, 6);

	ATF_CHECK_EQ(isc_taskmgr_mode(taskmgr), isc_taskmgrmode_normal);

	isc_task_destroy(&task1);
	ATF_REQUIRE_EQ(task1, NULL);
	isc_task_destroy(&task2);
	ATF_REQUIRE_EQ(task2, NULL);

	isc_test_end();
}

/*
 * Main
 */
ATF_TP_ADD_TCS(tp) {
	ATF_TP_ADD_TC(tp, create_task);
	ATF_TP_ADD_TC(tp, all_events);
	ATF_TP_ADD_TC(tp, privileged_events);
	ATF_TP_ADD_TC(tp, privilege_drop);

	return (atf_no_error());
}

