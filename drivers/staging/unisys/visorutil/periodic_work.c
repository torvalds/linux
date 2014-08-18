/* periodic_work.c
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/*
 *  Helper functions to schedule periodic work in Linux kernel mode.
 */

#include "uniklog.h"
#include "timskmod.h"
#include "periodic_work.h"

#define MYDRVNAME "periodic_work"



struct PERIODIC_WORK_Tag {
	rwlock_t lock;
	struct delayed_work work;
	void (*workfunc)(void *);
	void *workfuncarg;
	BOOL is_scheduled;
	BOOL want_to_stop;
	ulong jiffy_interval;
	struct workqueue_struct *workqueue;
	const char *devnam;
};



static void periodic_work_func(struct work_struct *work)
{
	PERIODIC_WORK *periodic_work =
		container_of(work, struct PERIODIC_WORK_Tag, work.work);
	(*periodic_work->workfunc)(periodic_work->workfuncarg);
}



PERIODIC_WORK *visor_periodic_work_create(ulong jiffy_interval,
					  struct workqueue_struct *workqueue,
					  void (*workfunc)(void *),
					  void *workfuncarg,
					  const char *devnam)
{
	PERIODIC_WORK *periodic_work = kzalloc(sizeof(PERIODIC_WORK),
					       GFP_KERNEL | __GFP_NORETRY);
	if (periodic_work == NULL) {
		ERRDRV("periodic_work allocation failed ");
		return NULL;
	}
	rwlock_init(&periodic_work->lock);
	periodic_work->jiffy_interval = jiffy_interval;
	periodic_work->workqueue = workqueue;
	periodic_work->workfunc = workfunc;
	periodic_work->workfuncarg = workfuncarg;
	periodic_work->devnam = devnam;
	return periodic_work;
}
EXPORT_SYMBOL_GPL(visor_periodic_work_create);



void visor_periodic_work_destroy(PERIODIC_WORK *periodic_work)
{
	if (periodic_work == NULL)
		return;
	kfree(periodic_work);
}
EXPORT_SYMBOL_GPL(visor_periodic_work_destroy);



/** Call this from your periodic work worker function to schedule the next
 *  call.
 *  If this function returns FALSE, there was a failure and the
 *  periodic work is no longer scheduled
 */
BOOL visor_periodic_work_nextperiod(PERIODIC_WORK *periodic_work)
{
	BOOL rc = FALSE;

	write_lock(&periodic_work->lock);
	if (periodic_work->want_to_stop) {
		periodic_work->is_scheduled = FALSE;
		periodic_work->want_to_stop = FALSE;
		rc = TRUE;  /* yes, TRUE; see visor_periodic_work_stop() */
		goto Away;
	} else if (queue_delayed_work(periodic_work->workqueue,
				      &periodic_work->work,
				      periodic_work->jiffy_interval) < 0) {
		ERRDEV(periodic_work->devnam, "queue_delayed_work failed!");
		periodic_work->is_scheduled = FALSE;
		rc = FALSE;
		goto Away;
	}
	rc = TRUE;
Away:
	write_unlock(&periodic_work->lock);
	return rc;
}
EXPORT_SYMBOL_GPL(visor_periodic_work_nextperiod);



/** This function returns TRUE iff new periodic work was actually started.
 *  If this function returns FALSE, then no work was started
 *  (either because it was already started, or because of a failure).
 */
BOOL visor_periodic_work_start(PERIODIC_WORK *periodic_work)
{
	BOOL rc = FALSE;

	write_lock(&periodic_work->lock);
	if (periodic_work->is_scheduled) {
		rc = FALSE;
		goto Away;
	}
	if (periodic_work->want_to_stop) {
		ERRDEV(periodic_work->devnam,
		       "dev_start_periodic_work failed!");
		rc = FALSE;
		goto Away;
	}
	INIT_DELAYED_WORK(&periodic_work->work, &periodic_work_func);
	if (queue_delayed_work(periodic_work->workqueue,
			       &periodic_work->work,
			       periodic_work->jiffy_interval) < 0) {
		ERRDEV(periodic_work->devnam,
		       "%s queue_delayed_work failed!", __func__);
		rc = FALSE;
		goto Away;
	}
	periodic_work->is_scheduled = TRUE;
	rc = TRUE;
Away:
	write_unlock(&periodic_work->lock);
	return rc;

}
EXPORT_SYMBOL_GPL(visor_periodic_work_start);




/** This function returns TRUE iff your call actually stopped the periodic
 *  work.
 *
 *  -- PAY ATTENTION... this is important --
 *
 *  NO NO #1
 *
 *     Do NOT call this function from some function that is running on the
 *     same workqueue as the work you are trying to stop might be running
 *     on!  If you violate this rule, visor_periodic_work_stop() MIGHT work,
 *     but it also MIGHT get hung up in an infinite loop saying
 *     "waiting for delayed work...".  This will happen if the delayed work
 *     you are trying to cancel has been put in the workqueue list, but can't
 *     run yet because we are running that same workqueue thread right now.
 *
 *     Bottom line: If you need to call visor_periodic_work_stop() from a
 *     workitem, be sure the workitem is on a DIFFERENT workqueue than the
 *     workitem that you are trying to cancel.
 *
 *     If I could figure out some way to check for this "no no" condition in
 *     the code, I would.  It would have saved me the trouble of writing this
 *     long comment.  And also, don't think this is some "theoretical" race
 *     condition.  It is REAL, as I have spent the day chasing it.
 *
 *  NO NO #2
 *
 *     Take close note of the locks that you own when you call this function.
 *     You must NOT own any locks that are needed by the periodic work
 *     function that is currently installed.  If you DO, a deadlock may result,
 *     because stopping the periodic work often involves waiting for the last
 *     iteration of the periodic work function to complete.  Again, if you hit
 *     this deadlock, you will get hung up in an infinite loop saying
 *     "waiting for delayed work...".
 */
BOOL visor_periodic_work_stop(PERIODIC_WORK *periodic_work)
{
	BOOL stopped_something = FALSE;

	write_lock(&periodic_work->lock);
	stopped_something = periodic_work->is_scheduled &&
		(!periodic_work->want_to_stop);
	while (periodic_work->is_scheduled) {
		periodic_work->want_to_stop = TRUE;
		if (cancel_delayed_work(&periodic_work->work)) {
			/* We get here if the delayed work was pending as
			 * delayed work, but was NOT run.
			 */
			ASSERT(periodic_work->is_scheduled);
			periodic_work->is_scheduled = FALSE;
		} else {
			/* If we get here, either the delayed work:
			 * - was run, OR,
			 * - is running RIGHT NOW on another processor, OR,
			 * - wasn't even scheduled (there is a miniscule
			 *   timing window where this could be the case)
			 * flush_workqueue() would make sure it is finished
			 * executing, but that still isn't very useful, which
			 * explains the loop...
			 */
		}
		if (periodic_work->is_scheduled) {
			write_unlock(&periodic_work->lock);
			WARNDEV(periodic_work->devnam,
				"waiting for delayed work...");
			/* We rely on the delayed work function running here,
			 * and eventually calling
			 * visor_periodic_work_nextperiod(),
			 * which will see that want_to_stop is set, and
			 * subsequently clear is_scheduled.
			 */
			SLEEPJIFFIES(10);
			write_lock(&periodic_work->lock);
		} else
			periodic_work->want_to_stop = FALSE;
	}
	write_unlock(&periodic_work->lock);
	return stopped_something;
}
EXPORT_SYMBOL_GPL(visor_periodic_work_stop);
