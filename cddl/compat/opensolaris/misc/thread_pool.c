/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include "thread_pool_impl.h"

typedef void (*_Voidfp)(void*); /* pointer to extern "C" function */

static void
delete_pool(tpool_t *tpool)
{
	tpool_job_t *job;

	/*
	 * There should be no pending jobs, but just in case...
	 */
	for (job = tpool->tp_head; job != NULL; job = tpool->tp_head) {
		tpool->tp_head = job->tpj_next;
		free(job);
	}
	(void) pthread_attr_destroy(&tpool->tp_attr);
	free(tpool);
}

/*
 * Worker thread is terminating.
 */
static void
worker_cleanup(void *arg)
{
	tpool_t *tpool = arg;

	if (--tpool->tp_current == 0 &&
	    (tpool->tp_flags & (TP_DESTROY | TP_ABANDON))) {
		if (tpool->tp_flags & TP_ABANDON) {
			pthread_mutex_unlock(&tpool->tp_mutex);
			delete_pool(tpool);
			return;
		}
		if (tpool->tp_flags & TP_DESTROY)
			(void) pthread_cond_broadcast(&tpool->tp_busycv);
	}
	pthread_mutex_unlock(&tpool->tp_mutex);
}

static void
notify_waiters(tpool_t *tpool)
{
	if (tpool->tp_head == NULL && tpool->tp_active == NULL) {
		tpool->tp_flags &= ~TP_WAIT;
		(void) pthread_cond_broadcast(&tpool->tp_waitcv);
	}
}

/*
 * Called by a worker thread on return from a tpool_dispatch()d job.
 */
static void
job_cleanup(void *arg)
{
	tpool_t *tpool = arg;
	pthread_t my_tid = pthread_self();
	tpool_active_t *activep;
	tpool_active_t **activepp;

	pthread_mutex_lock(&tpool->tp_mutex);
	/* CSTYLED */
	for (activepp = &tpool->tp_active;; activepp = &activep->tpa_next) {
		activep = *activepp;
		if (activep->tpa_tid == my_tid) {
			*activepp = activep->tpa_next;
			break;
		}
	}
	if (tpool->tp_flags & TP_WAIT)
		notify_waiters(tpool);
}

static void *
tpool_worker(void *arg)
{
	tpool_t *tpool = (tpool_t *)arg;
	int elapsed;
	tpool_job_t *job;
	void (*func)(void *);
	tpool_active_t active;
	sigset_t maskset;

	pthread_mutex_lock(&tpool->tp_mutex);
	pthread_cleanup_push(worker_cleanup, tpool);

	/*
	 * This is the worker's main loop.
	 * It will only be left if a timeout or an error has occured.
	 */
	active.tpa_tid = pthread_self();
	for (;;) {
		elapsed = 0;
		tpool->tp_idle++;
		if (tpool->tp_flags & TP_WAIT)
			notify_waiters(tpool);
		while ((tpool->tp_head == NULL ||
		    (tpool->tp_flags & TP_SUSPEND)) &&
		    !(tpool->tp_flags & (TP_DESTROY | TP_ABANDON))) {
			if (tpool->tp_current <= tpool->tp_minimum ||
			    tpool->tp_linger == 0) {
				(void) pthread_cond_wait(&tpool->tp_workcv,
				    &tpool->tp_mutex);
			} else {
				struct timespec timeout;

				clock_gettime(CLOCK_MONOTONIC, &timeout);
				timeout.tv_sec += tpool->tp_linger;
				if (pthread_cond_timedwait(&tpool->tp_workcv,
				    &tpool->tp_mutex, &timeout) != 0) {
					elapsed = 1;
					break;
				}
			}
		}
		tpool->tp_idle--;
		if (tpool->tp_flags & TP_DESTROY)
			break;
		if (tpool->tp_flags & TP_ABANDON) {
			/* can't abandon a suspended pool */
			if (tpool->tp_flags & TP_SUSPEND) {
				tpool->tp_flags &= ~TP_SUSPEND;
				(void) pthread_cond_broadcast(&tpool->tp_workcv);
			}
			if (tpool->tp_head == NULL)
				break;
		}
		if ((job = tpool->tp_head) != NULL &&
		    !(tpool->tp_flags & TP_SUSPEND)) {
			elapsed = 0;
			func = job->tpj_func;
			arg = job->tpj_arg;
			tpool->tp_head = job->tpj_next;
			if (job == tpool->tp_tail)
				tpool->tp_tail = NULL;
			tpool->tp_njobs--;
			active.tpa_next = tpool->tp_active;
			tpool->tp_active = &active;
			pthread_mutex_unlock(&tpool->tp_mutex);
			pthread_cleanup_push(job_cleanup, tpool);
			free(job);
			/*
			 * Call the specified function.
			 */
			func(arg);
			/*
			 * We don't know what this thread has been doing,
			 * so we reset its signal mask and cancellation
			 * state back to the initial values.
			 */
			sigfillset(&maskset);
			(void) pthread_sigmask(SIG_SETMASK, &maskset, NULL);
			(void) pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED,
			    NULL);
			(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,
			    NULL);
			pthread_cleanup_pop(1);
		}
		if (elapsed && tpool->tp_current > tpool->tp_minimum) {
			/*
			 * We timed out and there is no work to be done
			 * and the number of workers exceeds the minimum.
			 * Exit now to reduce the size of the pool.
			 */
			break;
		}
	}
	pthread_cleanup_pop(1);
	return (arg);
}

/*
 * Create a worker thread, with all signals blocked.
 */
static int
create_worker(tpool_t *tpool)
{
	sigset_t maskset, oset;
	pthread_t thread;
	int error;

	sigfillset(&maskset);
	(void) pthread_sigmask(SIG_SETMASK, &maskset, &oset);
	error = pthread_create(&thread, &tpool->tp_attr, tpool_worker, tpool);
	(void) pthread_sigmask(SIG_SETMASK, &oset, NULL);
	return (error);
}

tpool_t	*
tpool_create(uint_t min_threads, uint_t max_threads, uint_t linger,
	pthread_attr_t *attr)
{
	tpool_t	*tpool;
	int error;

	if (min_threads > max_threads || max_threads < 1) {
		errno = EINVAL;
		return (NULL);
	}

	tpool = calloc(1, sizeof (*tpool));
	if (tpool == NULL) {
		errno = ENOMEM;
		return (NULL);
	}
	(void) pthread_mutex_init(&tpool->tp_mutex, NULL);
	(void) pthread_cond_init(&tpool->tp_busycv, NULL);
	(void) pthread_cond_init(&tpool->tp_workcv, NULL);
	(void) pthread_cond_init(&tpool->tp_waitcv, NULL);
	tpool->tp_minimum = min_threads;
	tpool->tp_maximum = max_threads;
	tpool->tp_linger = linger;

	/* make all pool threads be detached daemon threads */
	(void) pthread_attr_init(&tpool->tp_attr);
	(void) pthread_attr_setdetachstate(&tpool->tp_attr,
	    PTHREAD_CREATE_DETACHED);

	return (tpool);
}

/*
 * Dispatch a work request to the thread pool.
 * If there are idle workers, awaken one.
 * Else, if the maximum number of workers has
 * not been reached, spawn a new worker thread.
 * Else just return with the job added to the queue.
 */
int
tpool_dispatch(tpool_t *tpool, void (*func)(void *), void *arg)
{
	tpool_job_t *job;

	if ((job = calloc(1, sizeof (*job))) == NULL)
		return (-1);
	job->tpj_next = NULL;
	job->tpj_func = func;
	job->tpj_arg = arg;

	pthread_mutex_lock(&tpool->tp_mutex);

	if (tpool->tp_head == NULL)
		tpool->tp_head = job;
	else
		tpool->tp_tail->tpj_next = job;
	tpool->tp_tail = job;
	tpool->tp_njobs++;

	if (!(tpool->tp_flags & TP_SUSPEND)) {
		if (tpool->tp_idle > 0)
			(void) pthread_cond_signal(&tpool->tp_workcv);
		else if (tpool->tp_current < tpool->tp_maximum &&
		    create_worker(tpool) == 0)
			tpool->tp_current++;
	}

	pthread_mutex_unlock(&tpool->tp_mutex);
	return (0);
}

/*
 * Assumes: by the time tpool_destroy() is called no one will use this
 * thread pool in any way and no one will try to dispatch entries to it.
 * Calling tpool_destroy() from a job in the pool will cause deadlock.
 */
void
tpool_destroy(tpool_t *tpool)
{
	tpool_active_t *activep;

	pthread_mutex_lock(&tpool->tp_mutex);
	pthread_cleanup_push((_Voidfp)pthread_mutex_unlock, &tpool->tp_mutex);

	/* mark the pool as being destroyed; wakeup idle workers */
	tpool->tp_flags |= TP_DESTROY;
	tpool->tp_flags &= ~TP_SUSPEND;
	(void) pthread_cond_broadcast(&tpool->tp_workcv);

	/* cancel all active workers */
	for (activep = tpool->tp_active; activep; activep = activep->tpa_next)
		(void) pthread_cancel(activep->tpa_tid);

	/* wait for all active workers to finish */
	while (tpool->tp_active != NULL) {
		tpool->tp_flags |= TP_WAIT;
		(void) pthread_cond_wait(&tpool->tp_waitcv, &tpool->tp_mutex);
	}

	/* the last worker to terminate will wake us up */
	while (tpool->tp_current != 0)
		(void) pthread_cond_wait(&tpool->tp_busycv, &tpool->tp_mutex);

	pthread_cleanup_pop(1);	/* pthread_mutex_unlock(&tpool->tp_mutex); */
	delete_pool(tpool);
}

/*
 * Like tpool_destroy(), but don't cancel workers or wait for them to finish.
 * The last worker to terminate will delete the pool.
 */
void
tpool_abandon(tpool_t *tpool)
{

	pthread_mutex_lock(&tpool->tp_mutex);
	if (tpool->tp_current == 0) {
		/* no workers, just delete the pool */
		pthread_mutex_unlock(&tpool->tp_mutex);
		delete_pool(tpool);
	} else {
		/* wake up all workers, last one will delete the pool */
		tpool->tp_flags |= TP_ABANDON;
		tpool->tp_flags &= ~TP_SUSPEND;
		(void) pthread_cond_broadcast(&tpool->tp_workcv);
		pthread_mutex_unlock(&tpool->tp_mutex);
	}
}

/*
 * Wait for all jobs to complete.
 * Calling tpool_wait() from a job in the pool will cause deadlock.
 */
void
tpool_wait(tpool_t *tpool)
{

	pthread_mutex_lock(&tpool->tp_mutex);
	pthread_cleanup_push((_Voidfp)pthread_mutex_unlock, &tpool->tp_mutex);
	while (tpool->tp_head != NULL || tpool->tp_active != NULL) {
		tpool->tp_flags |= TP_WAIT;
		(void) pthread_cond_wait(&tpool->tp_waitcv, &tpool->tp_mutex);
	}
	pthread_cleanup_pop(1);	/* pthread_mutex_unlock(&tpool->tp_mutex); */
}

void
tpool_suspend(tpool_t *tpool)
{

	pthread_mutex_lock(&tpool->tp_mutex);
	tpool->tp_flags |= TP_SUSPEND;
	pthread_mutex_unlock(&tpool->tp_mutex);
}

int
tpool_suspended(tpool_t *tpool)
{
	int suspended;

	pthread_mutex_lock(&tpool->tp_mutex);
	suspended = (tpool->tp_flags & TP_SUSPEND) != 0;
	pthread_mutex_unlock(&tpool->tp_mutex);

	return (suspended);
}

void
tpool_resume(tpool_t *tpool)
{
	int excess;

	pthread_mutex_lock(&tpool->tp_mutex);
	if (!(tpool->tp_flags & TP_SUSPEND)) {
		pthread_mutex_unlock(&tpool->tp_mutex);
		return;
	}
	tpool->tp_flags &= ~TP_SUSPEND;
	(void) pthread_cond_broadcast(&tpool->tp_workcv);
	excess = tpool->tp_njobs - tpool->tp_idle;
	while (excess-- > 0 && tpool->tp_current < tpool->tp_maximum) {
		if (create_worker(tpool) != 0)
			break;		/* pthread_create() failed */
		tpool->tp_current++;
	}
	pthread_mutex_unlock(&tpool->tp_mutex);
}

int
tpool_member(tpool_t *tpool)
{
	pthread_t my_tid = pthread_self();
	tpool_active_t *activep;

	pthread_mutex_lock(&tpool->tp_mutex);
	for (activep = tpool->tp_active; activep; activep = activep->tpa_next) {
		if (activep->tpa_tid == my_tid) {
			pthread_mutex_unlock(&tpool->tp_mutex);
			return (1);
		}
	}
	pthread_mutex_unlock(&tpool->tp_mutex);
	return (0);
}
