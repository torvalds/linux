/*
 * Copyright (C) 2009  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: timer_api.c,v 1.4 2009/09/02 23:48:02 tbox Exp $ */

#include <config.h>

#include <unistd.h>

#include <isc/app.h>
#include <isc/magic.h>
#include <isc/mutex.h>
#include <isc/once.h>
#include <isc/timer.h>
#include <isc/util.h>

static isc_mutex_t createlock;
static isc_once_t once = ISC_ONCE_INIT;
static isc_timermgrcreatefunc_t timermgr_createfunc = NULL;

static void
initialize(void) {
	RUNTIME_CHECK(isc_mutex_init(&createlock) == ISC_R_SUCCESS);
}

isc_result_t
isc_timer_register(isc_timermgrcreatefunc_t createfunc) {
	isc_result_t result = ISC_R_SUCCESS;

	RUNTIME_CHECK(isc_once_do(&once, initialize) == ISC_R_SUCCESS);

	LOCK(&createlock);
	if (timermgr_createfunc == NULL)
		timermgr_createfunc = createfunc;
	else
		result = ISC_R_EXISTS;
	UNLOCK(&createlock);

	return (result);
}

isc_result_t
isc_timermgr_createinctx(isc_mem_t *mctx, isc_appctx_t *actx,
			 isc_timermgr_t **managerp)
{
	isc_result_t result;

	LOCK(&createlock);

	REQUIRE(timermgr_createfunc != NULL);
	result = (*timermgr_createfunc)(mctx, managerp);

	UNLOCK(&createlock);

	if (result == ISC_R_SUCCESS)
		isc_appctx_settimermgr(actx, *managerp);

	return (result);
}

isc_result_t
isc_timermgr_create(isc_mem_t *mctx, isc_timermgr_t **managerp) {
	isc_result_t result;

	LOCK(&createlock);

	REQUIRE(timermgr_createfunc != NULL);
	result = (*timermgr_createfunc)(mctx, managerp);

	UNLOCK(&createlock);

	return (result);
}

void
isc_timermgr_destroy(isc_timermgr_t **managerp) {
	REQUIRE(*managerp != NULL && ISCAPI_TIMERMGR_VALID(*managerp));

	(*managerp)->methods->destroy(managerp);

	ENSURE(*managerp == NULL);
}

isc_result_t
isc_timer_create(isc_timermgr_t *manager, isc_timertype_t type,
		 isc_time_t *expires, isc_interval_t *interval,
		 isc_task_t *task, isc_taskaction_t action, const void *arg,
		 isc_timer_t **timerp)
{
	REQUIRE(ISCAPI_TIMERMGR_VALID(manager));

	return (manager->methods->timercreate(manager, type, expires,
					      interval, task, action, arg,
					      timerp));
}

void
isc_timer_attach(isc_timer_t *timer, isc_timer_t **timerp) {
	REQUIRE(ISCAPI_TIMER_VALID(timer));
	REQUIRE(timerp != NULL && *timerp == NULL);

	timer->methods->attach(timer, timerp);

	ENSURE(*timerp == timer);
}

void
isc_timer_detach(isc_timer_t **timerp) {
	REQUIRE(timerp != NULL && ISCAPI_TIMER_VALID(*timerp));

	(*timerp)->methods->detach(timerp);

	ENSURE(*timerp == NULL);
}

isc_result_t
isc_timer_reset(isc_timer_t *timer, isc_timertype_t type,
		isc_time_t *expires, isc_interval_t *interval,
		isc_boolean_t purge)
{
	REQUIRE(ISCAPI_TIMER_VALID(timer));

	return (timer->methods->reset(timer, type, expires, interval, purge));
}

isc_result_t
isc_timer_touch(isc_timer_t *timer) {
	REQUIRE(ISCAPI_TIMER_VALID(timer));

	return (timer->methods->touch(timer));
}
