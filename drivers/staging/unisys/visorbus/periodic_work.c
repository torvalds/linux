/* periodic_work.c
 *
 * Copyright (C) 2010 - 2015 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
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
#include <linux/sched.h>

#include "periodic_work.h"

#define MYDRVNAME "periodic_work"

struct periodic_work {
	rwlock_t lock;
	struct delayed_work work;
	void (*workfunc)(void *);
	void *workfuncarg;
	bool is_scheduled;
	bool want_to_stop;
	ulong jiffy_interval;
	struct workqueue_struct *workqueue;
	const char *devnam;
};

static void periodic_work_func(struct work_struct *work)
{
	struct periodic_work *pw;

	pw = container_of(work, struct periodic_work, work.work);
	(*pw->workfunc)(pw->workfuncarg);
}

struct periodic_work *visor_periodic_work_create(ulong jiffy_interval,
					struct workqueue_struct *workqueue,
					void (*workfunc)(void *),
					void *workfuncarg,
					const char *devnam)
{
	struct periodic_work *pw;

	pw = kzalloc(sizeof(*pw), GFP_KERNEL | __GFP_NORETRY);
	if (!pw)
		return NULL;

	rwlock_init(&pw->lock);
	pw->jiffy_interval = jiffy_interval;
	pw->workqueue = workqueue;
	pw->workfunc = workfunc;
	pw->workfuncarg = workfuncarg;
	pw->devnam = devnam;
	return pw;
}
EXPORT_SYMBOL_GPL(visor_periodic_work_create);

void visor_periodic_work_destroy(struct periodic_work *pw)
{
	kfree(pw);
}
EXPORT_SYMBOL_GPL(visor_periodic_work_destroy);

/** Call this from your periodic work worker function to schedule the next
 *  call.
 *  If this function returns false, there was a failure and the
 *  periodic work is no longer scheduled
 */
bool visor_periodic_work_nextperiod(struct periodic_work *pw)
{
	bool rc = false;

	write_lock(&pw->lock);
	if (pw->want_to_stop) {
		pw->is_scheduled = false;
		pw->want_to_stop = false;
		rc = true;  /* yes, true; see visor_periodic_work_stop() */
		goto unlock;
	} else if (queue_delayed_work(pw->workqueue, &pw->work,
				      pw->jiffy_interval) < 0) {
		pw->is_scheduled = false;
		rc = false;
		goto unlock;
	}
	rc = true;
unlock:
	write_unlock(&pw->lock);
	return rc;
}
EXPORT_SYMBOL_GPL(visor_periodic_work_nextperiod);

/** This function returns true iff new periodic work was actually started.
 *  If this function returns false, then no work was started
 *  (either because it was already started, or because of a failure).
 */
bool visor_periodic_work_start(struct periodic_work *pw)
{
	bool rc = false;

	write_lock(&pw->lock);
	if (pw->is_scheduled) {
		rc = false;
		goto unlock;
	}
	if (pw->want_to_stop) {
		rc = false;
		goto unlock;
	}
	INIT_DELAYED_WORK(&pw->work, &periodic_work_func);
	if (queue_delayed_work(pw->workqueue, &pw->work,
			       pw->jiffy_interval) < 0) {
		rc = false;
		goto unlock;
	}
	pw->is_scheduled = true;
	rc = true;
unlock:
	write_unlock(&pw->lock);
	return rc;
}
EXPORT_SYMBOL_GPL(visor_periodic_work_start);

/** This function returns true iff your call actually stopped the periodic
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
bool visor_periodic_work_stop(struct periodic_work *pw)
{
	bool stopped_something = false;

	write_lock(&pw->lock);
	stopped_something = pw->is_scheduled && (!pw->want_to_stop);
	while (pw->is_scheduled) {
		pw->want_to_stop = true;
		if (cancel_delayed_work(&pw->work)) {
			/* We get here if the delayed work was pending as
			 * delayed work, but was NOT run.
			 */
			WARN_ON(!pw->is_scheduled);
			pw->is_scheduled = false;
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
		if (pw->is_scheduled) {
			write_unlock(&pw->lock);
			schedule_timeout_interruptible(msecs_to_jiffies(10));
			write_lock(&pw->lock);
		} else {
			pw->want_to_stop = false;
		}
	}
	write_unlock(&pw->lock);
	return stopped_something;
}
EXPORT_SYMBOL_GPL(visor_periodic_work_stop);
