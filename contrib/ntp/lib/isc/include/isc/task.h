/*
 * Copyright (C) 2004-2007, 2009-2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1998-2001, 2003  Internet Software Consortium.
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

#ifndef ISC_TASK_H
#define ISC_TASK_H 1

/*****
 ***** Module Info
 *****/

/*! \file isc/task.h
 * \brief The task system provides a lightweight execution context, which is
 * basically an event queue.

 * When a task's event queue is non-empty, the
 * task is runnable.  A small work crew of threads, typically one per CPU,
 * execute runnable tasks by dispatching the events on the tasks' event
 * queues.  Context switching between tasks is fast.
 *
 * \li MP:
 *	The module ensures appropriate synchronization of data structures it
 *	creates and manipulates.
 *	The caller must ensure that isc_taskmgr_destroy() is called only
 *	once for a given manager.
 *
 * \li Reliability:
 *	No anticipated impact.
 *
 * \li Resources:
 *	TBS
 *
 * \li Security:
 *	No anticipated impact.
 *
 * \li Standards:
 *	None.
 *
 * \section purge Purging and Unsending
 *
 * Events which have been queued for a task but not delivered may be removed
 * from the task's event queue by purging or unsending.
 *
 * With both types, the caller specifies a matching pattern that selects
 * events based upon their sender, type, and tag.
 *
 * Purging calls isc_event_free() on the matching events.
 *
 * Unsending returns a list of events that matched the pattern.
 * The caller is then responsible for them.
 *
 * Consumers of events should purge, not unsend.
 *
 * Producers of events often want to remove events when the caller indicates
 * it is no longer interested in the object, e.g. by canceling a timer.
 * Sometimes this can be done by purging, but for some event types, the
 * calls to isc_event_free() cause deadlock because the event free routine
 * wants to acquire a lock the caller is already holding.  Unsending instead
 * of purging solves this problem.  As a general rule, producers should only
 * unsend events which they have sent.
 */


/***
 *** Imports.
 ***/

#include <isc/eventclass.h>
#include <isc/lang.h>
#include <isc/stdtime.h>
#include <isc/types.h>
#include <isc/xml.h>

#define ISC_TASKEVENT_FIRSTEVENT	(ISC_EVENTCLASS_TASK + 0)
#define ISC_TASKEVENT_SHUTDOWN		(ISC_EVENTCLASS_TASK + 1)
#define ISC_TASKEVENT_TEST		(ISC_EVENTCLASS_TASK + 1)
#define ISC_TASKEVENT_LASTEVENT		(ISC_EVENTCLASS_TASK + 65535)

/*****
 ***** Tasks.
 *****/

ISC_LANG_BEGINDECLS

/***
 *** Types
 ***/

typedef enum {
		isc_taskmgrmode_normal = 0,
		isc_taskmgrmode_privileged
} isc_taskmgrmode_t;

/*% Task and task manager methods */
typedef struct isc_taskmgrmethods {
	void		(*destroy)(isc_taskmgr_t **managerp);
	void		(*setmode)(isc_taskmgr_t *manager,
				   isc_taskmgrmode_t mode);
	isc_taskmgrmode_t (*mode)(isc_taskmgr_t *manager);
	isc_result_t	(*taskcreate)(isc_taskmgr_t *manager,
				      unsigned int quantum,
				      isc_task_t **taskp);
} isc_taskmgrmethods_t;

typedef struct isc_taskmethods {
	void (*attach)(isc_task_t *source, isc_task_t **targetp);
	void (*detach)(isc_task_t **taskp);
	void (*destroy)(isc_task_t **taskp);
	void (*send)(isc_task_t *task, isc_event_t **eventp);
	void (*sendanddetach)(isc_task_t **taskp, isc_event_t **eventp);
	unsigned int (*unsend)(isc_task_t *task, void *sender, isc_eventtype_t type,
			       void *tag, isc_eventlist_t *events);
	isc_result_t (*onshutdown)(isc_task_t *task, isc_taskaction_t action,
				   const void *arg);
	void (*shutdown)(isc_task_t *task);
	void (*setname)(isc_task_t *task, const char *name, void *tag);
	unsigned int (*purgeevents)(isc_task_t *task, void *sender,
				    isc_eventtype_t type, void *tag);
	unsigned int (*purgerange)(isc_task_t *task, void *sender,
				   isc_eventtype_t first, isc_eventtype_t last,
				   void *tag);
	isc_result_t (*beginexclusive)(isc_task_t *task);
	void (*endexclusive)(isc_task_t *task);
    void (*setprivilege)(isc_task_t *task, isc_boolean_t priv);
    isc_boolean_t (*privilege)(isc_task_t *task);
} isc_taskmethods_t;

/*%
 * This structure is actually just the common prefix of a task manager
 * object implementation's version of an isc_taskmgr_t.
 * \brief
 * Direct use of this structure by clients is forbidden.  task implementations
 * may change the structure.  'magic' must be ISCAPI_TASKMGR_MAGIC for any
 * of the isc_task_ routines to work.  task implementations must maintain
 * all task invariants.
 */
struct isc_taskmgr {
	unsigned int		impmagic;
	unsigned int		magic;
	isc_taskmgrmethods_t	*methods;
};

#define ISCAPI_TASKMGR_MAGIC	ISC_MAGIC('A','t','m','g')
#define ISCAPI_TASKMGR_VALID(m)	((m) != NULL && \
				 (m)->magic == ISCAPI_TASKMGR_MAGIC)

/*%
 * This is the common prefix of a task object.  The same note as
 * that for the taskmgr structure applies.
 */
struct isc_task {
	unsigned int		impmagic;
	unsigned int		magic;
	isc_taskmethods_t	*methods;
};

#define ISCAPI_TASK_MAGIC	ISC_MAGIC('A','t','s','t')
#define ISCAPI_TASK_VALID(s)	((s) != NULL && \
				 (s)->magic == ISCAPI_TASK_MAGIC)

isc_result_t
isc_task_create(isc_taskmgr_t *manager, unsigned int quantum,
		isc_task_t **taskp);
/*%<
 * Create a task.
 *
 * Notes:
 *
 *\li	If 'quantum' is non-zero, then only that many events can be dispatched
 *	before the task must yield to other tasks waiting to execute.  If
 *	quantum is zero, then the default quantum of the task manager will
 *	be used.
 *
 *\li	The 'quantum' option may be removed from isc_task_create() in the
 *	future.  If this happens, isc_task_getquantum() and
 *	isc_task_setquantum() will be provided.
 *
 * Requires:
 *
 *\li	'manager' is a valid task manager.
 *
 *\li	taskp != NULL && *taskp == NULL
 *
 * Ensures:
 *
 *\li	On success, '*taskp' is bound to the new task.
 *
 * Returns:
 *
 *\li   #ISC_R_SUCCESS
 *\li	#ISC_R_NOMEMORY
 *\li	#ISC_R_UNEXPECTED
 *\li	#ISC_R_SHUTTINGDOWN
 */

void
isc_task_attach(isc_task_t *source, isc_task_t **targetp);
/*%<
 * Attach *targetp to source.
 *
 * Requires:
 *
 *\li	'source' is a valid task.
 *
 *\li	'targetp' points to a NULL isc_task_t *.
 *
 * Ensures:
 *
 *\li	*targetp is attached to source.
 */

void
isc_task_detach(isc_task_t **taskp);
/*%<
 * Detach *taskp from its task.
 *
 * Requires:
 *
 *\li	'*taskp' is a valid task.
 *
 * Ensures:
 *
 *\li	*taskp is NULL.
 *
 *\li	If '*taskp' is the last reference to the task, the task is idle (has
 *	an empty event queue), and has not been shutdown, the task will be
 *	shutdown.
 *
 *\li	If '*taskp' is the last reference to the task and
 *	the task has been shutdown,
 *		all resources used by the task will be freed.
 */

void
isc_task_send(isc_task_t *task, isc_event_t **eventp);
/*%<
 * Send '*event' to 'task'.
 *
 * Requires:
 *
 *\li	'task' is a valid task.
 *\li	eventp != NULL && *eventp != NULL.
 *
 * Ensures:
 *
 *\li	*eventp == NULL.
 */

void
isc_task_sendanddetach(isc_task_t **taskp, isc_event_t **eventp);
/*%<
 * Send '*event' to '*taskp' and then detach '*taskp' from its
 * task.
 *
 * Requires:
 *
 *\li	'*taskp' is a valid task.
 *\li	eventp != NULL && *eventp != NULL.
 *
 * Ensures:
 *
 *\li	*eventp == NULL.
 *
 *\li	*taskp == NULL.
 *
 *\li	If '*taskp' is the last reference to the task, the task is
 *	idle (has an empty event queue), and has not been shutdown,
 *	the task will be shutdown.
 *
 *\li	If '*taskp' is the last reference to the task and
 *	the task has been shutdown,
 *		all resources used by the task will be freed.
 */


unsigned int
isc_task_purgerange(isc_task_t *task, void *sender, isc_eventtype_t first,
		    isc_eventtype_t last, void *tag);
/*%<
 * Purge events from a task's event queue.
 *
 * Requires:
 *
 *\li	'task' is a valid task.
 *
 *\li	last >= first
 *
 * Ensures:
 *
 *\li	Events in the event queue of 'task' whose sender is 'sender', whose
 *	type is >= first and <= last, and whose tag is 'tag' will be purged,
 *	unless they are marked as unpurgable.
 *
 *\li	A sender of NULL will match any sender.  A NULL tag matches any
 *	tag.
 *
 * Returns:
 *
 *\li	The number of events purged.
 */

unsigned int
isc_task_purge(isc_task_t *task, void *sender, isc_eventtype_t type,
	       void *tag);
/*%<
 * Purge events from a task's event queue.
 *
 * Notes:
 *
 *\li	This function is equivalent to
 *
 *\code
 *		isc_task_purgerange(task, sender, type, type, tag);
 *\endcode
 *
 * Requires:
 *
 *\li	'task' is a valid task.
 *
 * Ensures:
 *
 *\li	Events in the event queue of 'task' whose sender is 'sender', whose
 *	type is 'type', and whose tag is 'tag' will be purged, unless they
 *	are marked as unpurgable.
 *
 *\li	A sender of NULL will match any sender.  A NULL tag matches any
 *	tag.
 *
 * Returns:
 *
 *\li	The number of events purged.
 */

isc_boolean_t
isc_task_purgeevent(isc_task_t *task, isc_event_t *event);
/*%<
 * Purge 'event' from a task's event queue.
 *
 * XXXRTH:  WARNING:  This method may be removed before beta.
 *
 * Notes:
 *
 *\li	If 'event' is on the task's event queue, it will be purged,
 * 	unless it is marked as unpurgeable.  'event' does not have to be
 *	on the task's event queue; in fact, it can even be an invalid
 *	pointer.  Purging only occurs if the event is actually on the task's
 *	event queue.
 *
 * \li	Purging never changes the state of the task.
 *
 * Requires:
 *
 *\li	'task' is a valid task.
 *
 * Ensures:
 *
 *\li	'event' is not in the event queue for 'task'.
 *
 * Returns:
 *
 *\li	#ISC_TRUE			The event was purged.
 *\li	#ISC_FALSE			The event was not in the event queue,
 *					or was marked unpurgeable.
 */

unsigned int
isc_task_unsendrange(isc_task_t *task, void *sender, isc_eventtype_t first,
		     isc_eventtype_t last, void *tag, isc_eventlist_t *events);
/*%<
 * Remove events from a task's event queue.
 *
 * Requires:
 *
 *\li	'task' is a valid task.
 *
 *\li	last >= first.
 *
 *\li	*events is a valid list.
 *
 * Ensures:
 *
 *\li	Events in the event queue of 'task' whose sender is 'sender', whose
 *	type is >= first and <= last, and whose tag is 'tag' will be dequeued
 *	and appended to *events.
 *
 *\li	A sender of NULL will match any sender.  A NULL tag matches any
 *	tag.
 *
 * Returns:
 *
 *\li	The number of events unsent.
 */

unsigned int
isc_task_unsend(isc_task_t *task, void *sender, isc_eventtype_t type,
		void *tag, isc_eventlist_t *events);
/*%<
 * Remove events from a task's event queue.
 *
 * Notes:
 *
 *\li	This function is equivalent to
 *
 *\code
 *		isc_task_unsendrange(task, sender, type, type, tag, events);
 *\endcode
 *
 * Requires:
 *
 *\li	'task' is a valid task.
 *
 *\li	*events is a valid list.
 *
 * Ensures:
 *
 *\li	Events in the event queue of 'task' whose sender is 'sender', whose
 *	type is 'type', and whose tag is 'tag' will be dequeued and appended
 *	to *events.
 *
 * Returns:
 *
 *\li	The number of events unsent.
 */

isc_result_t
isc_task_onshutdown(isc_task_t *task, isc_taskaction_t action,
		    const void *arg);
/*%<
 * Send a shutdown event with action 'action' and argument 'arg' when
 * 'task' is shutdown.
 *
 * Notes:
 *
 *\li	Shutdown events are posted in LIFO order.
 *
 * Requires:
 *
 *\li	'task' is a valid task.
 *
 *\li	'action' is a valid task action.
 *
 * Ensures:
 *
 *\li	When the task is shutdown, shutdown events requested with
 *	isc_task_onshutdown() will be appended to the task's event queue.
 *

 * Returns:
 *
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOMEMORY
 *\li	#ISC_R_TASKSHUTTINGDOWN			Task is shutting down.
 */

void
isc_task_shutdown(isc_task_t *task);
/*%<
 * Shutdown 'task'.
 *
 * Notes:
 *
 *\li	Shutting down a task causes any shutdown events requested with
 *	isc_task_onshutdown() to be posted (in LIFO order).  The task
 *	moves into a "shutting down" mode which prevents further calls
 *	to isc_task_onshutdown().
 *
 *\li	Trying to shutdown a task that has already been shutdown has no
 *	effect.
 *
 * Requires:
 *
 *\li	'task' is a valid task.
 *
 * Ensures:
 *
 *\li	Any shutdown events requested with isc_task_onshutdown() have been
 *	posted (in LIFO order).
 */

void
isc_task_destroy(isc_task_t **taskp);
/*%<
 * Destroy '*taskp'.
 *
 * Notes:
 *
 *\li	This call is equivalent to:
 *
 *\code
 *		isc_task_shutdown(*taskp);
 *		isc_task_detach(taskp);
 *\endcode
 *
 * Requires:
 *
 *	'*taskp' is a valid task.
 *
 * Ensures:
 *
 *\li	Any shutdown events requested with isc_task_onshutdown() have been
 *	posted (in LIFO order).
 *
 *\li	*taskp == NULL
 *
 *\li	If '*taskp' is the last reference to the task,
 *		all resources used by the task will be freed.
 */

void
isc_task_setname(isc_task_t *task, const char *name, void *tag);
/*%<
 * Name 'task'.
 *
 * Notes:
 *
 *\li	Only the first 15 characters of 'name' will be copied.
 *
 *\li	Naming a task is currently only useful for debugging purposes.
 *
 * Requires:
 *
 *\li	'task' is a valid task.
 */

const char *
isc_task_getname(isc_task_t *task);
/*%<
 * Get the name of 'task', as previously set using isc_task_setname().
 *
 * Notes:
 *\li	This function is for debugging purposes only.
 *
 * Requires:
 *\li	'task' is a valid task.
 *
 * Returns:
 *\li	A non-NULL pointer to a null-terminated string.
 * 	If the task has not been named, the string is
 * 	empty.
 *
 */

void *
isc_task_gettag(isc_task_t *task);
/*%<
 * Get the tag value for  'task', as previously set using isc_task_settag().
 *
 * Notes:
 *\li	This function is for debugging purposes only.
 *
 * Requires:
 *\li	'task' is a valid task.
 */

isc_result_t
isc_task_beginexclusive(isc_task_t *task);
/*%<
 * Request exclusive access for 'task', which must be the calling
 * task.  Waits for any other concurrently executing tasks to finish their
 * current event, and prevents any new events from executing in any of the
 * tasks sharing a task manager with 'task'.
 *
 * The exclusive access must be relinquished by calling
 * isc_task_endexclusive() before returning from the current event handler.
 *
 * Requires:
 *\li	'task' is the calling task.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		The current task now has exclusive access.
 *\li	#ISC_R_LOCKBUSY		Another task has already requested exclusive
 *				access.
 */

void
isc_task_endexclusive(isc_task_t *task);
/*%<
 * Relinquish the exclusive access obtained by isc_task_beginexclusive(),
 * allowing other tasks to execute.
 *
 * Requires:
 *\li	'task' is the calling task, and has obtained
 *		exclusive access by calling isc_task_spl().
 */

void
isc_task_getcurrenttime(isc_task_t *task, isc_stdtime_t *t);
/*%<
 * Provide the most recent timestamp on the task.  The timestamp is considered
 * as the "current time" in the second-order granularity.
 *
 * Requires:
 *\li	'task' is a valid task.
 *\li	't' is a valid non NULL pointer.
 *
 * Ensures:
 *\li	'*t' has the "current time".
 */

isc_boolean_t
isc_task_exiting(isc_task_t *t);
/*%<
 * Returns ISC_TRUE if the task is in the process of shutting down,
 * ISC_FALSE otherwise.
 *
 * Requires:
 *\li	'task' is a valid task.
 */

void
isc_task_setprivilege(isc_task_t *task, isc_boolean_t priv);
/*%<
 * Set or unset the task's "privileged" flag depending on the value of
 * 'priv'.
 *
 * Under normal circumstances this flag has no effect on the task behavior,
 * but when the task manager has been set to privileged exeuction mode via
 * isc_taskmgr_setmode(), only tasks with the flag set will be executed,
 * and all other tasks will wait until they're done.  Once all privileged
 * tasks have finished executing, the task manager will automatically
 * return to normal execution mode and nonprivileged task can resume.
 *
 * Requires:
 *\li	'task' is a valid task.
 */

isc_boolean_t
isc_task_privilege(isc_task_t *task);
/*%<
 * Returns the current value of the task's privilege flag.
 *
 * Requires:
 *\li	'task' is a valid task.
 */

/*****
 ***** Task Manager.
 *****/

isc_result_t
isc_taskmgr_createinctx(isc_mem_t *mctx, isc_appctx_t *actx,
			unsigned int workers, unsigned int default_quantum,
			isc_taskmgr_t **managerp);
isc_result_t
isc_taskmgr_create(isc_mem_t *mctx, unsigned int workers,
		   unsigned int default_quantum, isc_taskmgr_t **managerp);
/*%<
 * Create a new task manager.  isc_taskmgr_createinctx() also associates
 * the new manager with the specified application context.
 *
 * Notes:
 *
 *\li	'workers' in the number of worker threads to create.  In general,
 *	the value should be close to the number of processors in the system.
 *	The 'workers' value is advisory only.  An attempt will be made to
 *	create 'workers' threads, but if at least one thread creation
 *	succeeds, isc_taskmgr_create() may return ISC_R_SUCCESS.
 *
 *\li	If 'default_quantum' is non-zero, then it will be used as the default
 *	quantum value when tasks are created.  If zero, then an implementation
 *	defined default quantum will be used.
 *
 * Requires:
 *
 *\li      'mctx' is a valid memory context.
 *
 *\li	workers > 0
 *
 *\li	managerp != NULL && *managerp == NULL
 *
 *\li	'actx' is a valid application context (for createinctx()).
 *
 * Ensures:
 *
 *\li	On success, '*managerp' will be attached to the newly created task
 *	manager.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOMEMORY
 *\li	#ISC_R_NOTHREADS		No threads could be created.
 *\li	#ISC_R_UNEXPECTED		An unexpected error occurred.
 *\li	#ISC_R_SHUTTINGDOWN      	The non-threaded, shared, task
 *					manager shutting down.
 */

void
isc_taskmgr_setmode(isc_taskmgr_t *manager, isc_taskmgrmode_t mode);

isc_taskmgrmode_t
isc_taskmgr_mode(isc_taskmgr_t *manager);
/*%<
 * Set/get the current operating mode of the task manager.  Valid modes are:
 *
 *\li  isc_taskmgrmode_normal
 *\li  isc_taskmgrmode_privileged
 *
 * In privileged execution mode, only tasks that have had the "privilege"
 * flag set via isc_task_setprivilege() can be executed.  When all such
 * tasks are complete, the manager automatically returns to normal mode
 * and proceeds with running non-privileged ready tasks.  This means it is
 * necessary to have at least one privileged task waiting on the ready
 * queue *before* setting the manager into privileged execution mode,
 * which in turn means the task which calls this function should be in
 * task-exclusive mode when it does so.
 *
 * Requires:
 *
 *\li      'manager' is a valid task manager.
 */

void
isc_taskmgr_destroy(isc_taskmgr_t **managerp);
/*%<
 * Destroy '*managerp'.
 *
 * Notes:
 *
 *\li	Calling isc_taskmgr_destroy() will shutdown all tasks managed by
 *	*managerp that haven't already been shutdown.  The call will block
 *	until all tasks have entered the done state.
 *
 *\li	isc_taskmgr_destroy() must not be called by a task event action,
 *	because it would block forever waiting for the event action to
 *	complete.  An event action that wants to cause task manager shutdown
 *	should request some non-event action thread of execution to do the
 *	shutdown, e.g. by signaling a condition variable or using
 *	isc_app_shutdown().
 *
 *\li	Task manager references are not reference counted, so the caller
 *	must ensure that no attempt will be made to use the manager after
 *	isc_taskmgr_destroy() returns.
 *
 * Requires:
 *
 *\li	'*managerp' is a valid task manager.
 *
 *\li	isc_taskmgr_destroy() has not be called previously on '*managerp'.
 *
 * Ensures:
 *
 *\li	All resources used by the task manager, and any tasks it managed,
 *	have been freed.
 */

#ifdef HAVE_LIBXML2

void
isc_taskmgr_renderxml(isc_taskmgr_t *mgr, xmlTextWriterPtr writer);

#endif

/*%<
 * See isc_taskmgr_create() above.
 */
typedef isc_result_t
(*isc_taskmgrcreatefunc_t)(isc_mem_t *mctx, unsigned int workers,
			   unsigned int default_quantum,
			   isc_taskmgr_t **managerp);

isc_result_t
isc_task_register(isc_taskmgrcreatefunc_t createfunc);
/*%<
 * Register a new task management implementation and add it to the list of
 * supported implementations.  This function must be called when a different
 * event library is used than the one contained in the ISC library.
 */

isc_result_t
isc__task_register(void);
/*%<
 * A short cut function that specifies the task management module in the ISC
 * library for isc_task_register().  An application that uses the ISC library
 * usually do not have to care about this function: it would call
 * isc_lib_register(), which internally calls this function.
 */

ISC_LANG_ENDDECLS

#endif /* ISC_TASK_H */
