/*
 * Copyright (C) 2004-2007, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2001  Internet Software Consortium.
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

#ifndef ISC_TASKPOOL_H
#define ISC_TASKPOOL_H 1

/*****
 ***** Module Info
 *****/

/*! \file isc/taskpool.h
 * \brief A task pool is a mechanism for sharing a small number of tasks
 * among a large number of objects such that each object is
 * assigned a unique task, but each task may be shared by several
 * objects.
 *
 * Task pools are used to let objects that can exist in large
 * numbers (e.g., zones) use tasks for synchronization without
 * the memory overhead and unfair scheduling competition that
 * could result from creating a separate task for each object.
 */


/***
 *** Imports.
 ***/

#include <isc/lang.h>
#include <isc/task.h>

ISC_LANG_BEGINDECLS

/*****
 ***** Types.
 *****/

typedef struct isc_taskpool isc_taskpool_t;

/*****
 ***** Functions.
 *****/

isc_result_t
isc_taskpool_create(isc_taskmgr_t *tmgr, isc_mem_t *mctx,
		    unsigned int ntasks, unsigned int quantum,
		    isc_taskpool_t **poolp);
/*%<
 * Create a task pool of "ntasks" tasks, each with quantum
 * "quantum".
 *
 * Requires:
 *
 *\li	'tmgr' is a valid task manager.
 *
 *\li	'mctx' is a valid memory context.
 *
 *\li	poolp != NULL && *poolp == NULL
 *
 * Ensures:
 *
 *\li	On success, '*taskp' points to the new task pool.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOMEMORY
 *\li	#ISC_R_UNEXPECTED
 */

void
isc_taskpool_gettask(isc_taskpool_t *pool, isc_task_t **targetp);
/*%<
 * Attach to a task from the pool.  Currently the next task is chosen
 * from the pool at random.  (This may be changed in the future to
 * something that guaratees balance.)
 */

int
isc_taskpool_size(isc_taskpool_t *pool);
/*%<
 * Returns the number of tasks in the task pool 'pool'.
 */

isc_result_t
isc_taskpool_expand(isc_taskpool_t **sourcep, unsigned int size,
					isc_taskpool_t **targetp);

/*%<
 * If 'size' is larger than the number of tasks in the pool pointed to by
 * 'sourcep', then a new taskpool of size 'size' is allocated, the existing
 * tasks from are moved into it, additional tasks are created to bring the
 * total number up to 'size', and the resulting pool is attached to
 * 'targetp'.
 *
 * If 'size' is less than or equal to the tasks in pool 'source', then
 * 'sourcep' is attached to 'targetp' without any other action being taken.
 *
 * In either case, 'sourcep' is detached.
 *
 * Requires:
 *
 * \li	'sourcep' is not NULL and '*source' is not NULL
 * \li	'targetp' is not NULL and '*source' is NULL
 *
 * Ensures:
 *
 * \li	On success, '*targetp' points to a valid task pool.
 * \li	On success, '*sourcep' points to NULL.
 *
 * Returns:
 *
 * \li	#ISC_R_SUCCESS
 * \li	#ISC_R_NOMEMORY
 */

void
isc_taskpool_destroy(isc_taskpool_t **poolp);
/*%<
 * Destroy a task pool.  The tasks in the pool are detached but not
 * shut down.
 *
 * Requires:
 * \li	'*poolp' is a valid task pool.
 */

void
isc_taskpool_setprivilege(isc_taskpool_t *pool, isc_boolean_t priv);
/*%<
 * Set the privilege flag on all tasks in 'pool' to 'priv'.  If 'priv' is
 * true, then when the task manager is set into privileged mode, only
 * tasks wihin this pool will be able to execute.  (Note:  It is important
 * to turn the pool tasks' privilege back off before the last task finishes
 * executing.)
 *
 * Requires:
 * \li	'pool' is a valid task pool.
 */

ISC_LANG_ENDDECLS

#endif /* ISC_TASKPOOL_H */
