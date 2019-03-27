/*
 * Copyright (C) 2009-2012  Internet Systems Consortium, Inc. ("ISC")
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

#include <config.h>

#include <unistd.h>

#include <isc/app.h>
#include <isc/magic.h>
#include <isc/mutex.h>
#include <isc/once.h>
#include <isc/task.h>
#include <isc/util.h>

static isc_mutex_t createlock;
static isc_once_t once = ISC_ONCE_INIT;
static isc_taskmgrcreatefunc_t taskmgr_createfunc = NULL;

static void
initialize(void) {
	RUNTIME_CHECK(isc_mutex_init(&createlock) == ISC_R_SUCCESS);
}

isc_result_t
isc_task_register(isc_taskmgrcreatefunc_t createfunc) {
	isc_result_t result = ISC_R_SUCCESS;

	RUNTIME_CHECK(isc_once_do(&once, initialize) == ISC_R_SUCCESS);

	LOCK(&createlock);
	if (taskmgr_createfunc == NULL)
		taskmgr_createfunc = createfunc;
	else
		result = ISC_R_EXISTS;
	UNLOCK(&createlock);

	return (result);
}

isc_result_t
isc_taskmgr_createinctx(isc_mem_t *mctx, isc_appctx_t *actx,
			unsigned int workers, unsigned int default_quantum,
			isc_taskmgr_t **managerp)
{
	isc_result_t result;

	LOCK(&createlock);

	REQUIRE(taskmgr_createfunc != NULL);
	result = (*taskmgr_createfunc)(mctx, workers, default_quantum,
				       managerp);

	UNLOCK(&createlock);

	if (result == ISC_R_SUCCESS)
		isc_appctx_settaskmgr(actx, *managerp);

	return (result);
}

isc_result_t
isc_taskmgr_create(isc_mem_t *mctx, unsigned int workers,
		   unsigned int default_quantum, isc_taskmgr_t **managerp)
{
	isc_result_t result;

	LOCK(&createlock);

	REQUIRE(taskmgr_createfunc != NULL);
	result = (*taskmgr_createfunc)(mctx, workers, default_quantum,
				       managerp);

	UNLOCK(&createlock);

	return (result);
}

void
isc_taskmgr_destroy(isc_taskmgr_t **managerp) {
	REQUIRE(managerp != NULL && ISCAPI_TASKMGR_VALID(*managerp));

	(*managerp)->methods->destroy(managerp);

	ENSURE(*managerp == NULL);
}

void
isc_taskmgr_setmode(isc_taskmgr_t *manager, isc_taskmgrmode_t mode) {
	REQUIRE(ISCAPI_TASKMGR_VALID(manager));

	manager->methods->setmode(manager, mode);
}

isc_taskmgrmode_t
isc_taskmgr_mode(isc_taskmgr_t *manager) {
	REQUIRE(ISCAPI_TASKMGR_VALID(manager));

	return (manager->methods->mode(manager));
}

isc_result_t
isc_task_create(isc_taskmgr_t *manager, unsigned int quantum,
		isc_task_t **taskp)
{
	REQUIRE(ISCAPI_TASKMGR_VALID(manager));
	REQUIRE(taskp != NULL && *taskp == NULL);

	return (manager->methods->taskcreate(manager, quantum, taskp));
}

void
isc_task_attach(isc_task_t *source, isc_task_t **targetp) {
	REQUIRE(ISCAPI_TASK_VALID(source));
	REQUIRE(targetp != NULL && *targetp == NULL);

	source->methods->attach(source, targetp);

	ENSURE(*targetp == source);
}

void
isc_task_detach(isc_task_t **taskp) {
	REQUIRE(taskp != NULL && ISCAPI_TASK_VALID(*taskp));

	(*taskp)->methods->detach(taskp);

	ENSURE(*taskp == NULL);
}

void
isc_task_send(isc_task_t *task, isc_event_t **eventp) {
	REQUIRE(ISCAPI_TASK_VALID(task));
	REQUIRE(eventp != NULL && *eventp != NULL);

	task->methods->send(task, eventp);

	ENSURE(*eventp == NULL);
}

void
isc_task_sendanddetach(isc_task_t **taskp, isc_event_t **eventp) {
	REQUIRE(taskp != NULL && ISCAPI_TASK_VALID(*taskp));
	REQUIRE(eventp != NULL && *eventp != NULL);

	(*taskp)->methods->sendanddetach(taskp, eventp);

	ENSURE(*taskp == NULL && *eventp == NULL);
}

unsigned int
isc_task_unsend(isc_task_t *task, void *sender, isc_eventtype_t type,
		void *tag, isc_eventlist_t *events)
{
	REQUIRE(ISCAPI_TASK_VALID(task));

	return (task->methods->unsend(task, sender, type, tag, events));
}

isc_result_t
isc_task_onshutdown(isc_task_t *task, isc_taskaction_t action, const void *arg)
{
	REQUIRE(ISCAPI_TASK_VALID(task));

	return (task->methods->onshutdown(task, action, arg));
}

void
isc_task_shutdown(isc_task_t *task) {
	REQUIRE(ISCAPI_TASK_VALID(task));

	task->methods->shutdown(task);
}

void
isc_task_setname(isc_task_t *task, const char *name, void *tag) {
	REQUIRE(ISCAPI_TASK_VALID(task));

	task->methods->setname(task, name, tag);
}

unsigned int
isc_task_purge(isc_task_t *task, void *sender, isc_eventtype_t type, void *tag)
{
	REQUIRE(ISCAPI_TASK_VALID(task));

	return (task->methods->purgeevents(task, sender, type, tag));
}

isc_result_t
isc_task_beginexclusive(isc_task_t *task) {
	REQUIRE(ISCAPI_TASK_VALID(task));

	return (task->methods->beginexclusive(task));
}

void
isc_task_endexclusive(isc_task_t *task) {
	REQUIRE(ISCAPI_TASK_VALID(task));

	task->methods->endexclusive(task);
}

void
isc_task_setprivilege(isc_task_t *task, isc_boolean_t priv) {
	REQUIRE(ISCAPI_TASK_VALID(task));

	task->methods->setprivilege(task, priv);
}

isc_boolean_t
isc_task_privilege(isc_task_t *task) {
	REQUIRE(ISCAPI_TASK_VALID(task));

	return (task->methods->privilege(task));
}


/*%
 * This is necessary for libisc's internal timer implementation.  Other
 * implementation might skip implementing this.
 */
unsigned int
isc_task_purgerange(isc_task_t *task, void *sender, isc_eventtype_t first,
		    isc_eventtype_t last, void *tag)
{
	REQUIRE(ISCAPI_TASK_VALID(task));

	return (task->methods->purgerange(task, sender, first, last, tag));
}
