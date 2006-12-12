/* kafstimod.c: AFS timeout daemon
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/freezer.h>
#include "cell.h"
#include "volume.h"
#include "kafstimod.h"
#include <asm/errno.h>
#include "internal.h"

static DECLARE_COMPLETION(kafstimod_alive);
static DECLARE_COMPLETION(kafstimod_dead);
static DECLARE_WAIT_QUEUE_HEAD(kafstimod_sleepq);
static int kafstimod_die;

static LIST_HEAD(kafstimod_list);
static DEFINE_SPINLOCK(kafstimod_lock);

static int kafstimod(void *arg);

/*****************************************************************************/
/*
 * start the timeout daemon
 */
int afs_kafstimod_start(void)
{
	int ret;

	ret = kernel_thread(kafstimod, NULL, 0);
	if (ret < 0)
		return ret;

	wait_for_completion(&kafstimod_alive);

	return ret;
} /* end afs_kafstimod_start() */

/*****************************************************************************/
/*
 * stop the timeout daemon
 */
void afs_kafstimod_stop(void)
{
	/* get rid of my daemon */
	kafstimod_die = 1;
	wake_up(&kafstimod_sleepq);
	wait_for_completion(&kafstimod_dead);

} /* end afs_kafstimod_stop() */

/*****************************************************************************/
/*
 * timeout processing daemon
 */
static int kafstimod(void *arg)
{
	struct afs_timer *timer;

	DECLARE_WAITQUEUE(myself, current);

	printk("kAFS: Started kafstimod %d\n", current->pid);

	daemonize("kafstimod");

	complete(&kafstimod_alive);

	/* loop around looking for things to attend to */
 loop:
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&kafstimod_sleepq, &myself);

	for (;;) {
		unsigned long jif;
		signed long timeout;

		/* deal with the server being asked to die */
		if (kafstimod_die) {
			remove_wait_queue(&kafstimod_sleepq, &myself);
			_leave("");
			complete_and_exit(&kafstimod_dead, 0);
		}

		try_to_freeze();

		/* discard pending signals */
		afs_discard_my_signals();

		/* work out the time to elapse before the next event */
		spin_lock(&kafstimod_lock);
		if (list_empty(&kafstimod_list)) {
			timeout = MAX_SCHEDULE_TIMEOUT;
		}
		else {
			timer = list_entry(kafstimod_list.next,
					   struct afs_timer, link);
			timeout = timer->timo_jif;
			jif = jiffies;

			if (time_before_eq((unsigned long) timeout, jif))
				goto immediate;

			else {
				timeout = (long) timeout - (long) jiffies;
			}
		}
		spin_unlock(&kafstimod_lock);

		schedule_timeout(timeout);

		set_current_state(TASK_INTERRUPTIBLE);
	}

	/* the thing on the front of the queue needs processing
	 * - we come here with the lock held and timer pointing to the expired
	 *   entry
	 */
 immediate:
	remove_wait_queue(&kafstimod_sleepq, &myself);
	set_current_state(TASK_RUNNING);

	_debug("@@@ Begin Timeout of %p", timer);

	/* dequeue the timer */
	list_del_init(&timer->link);
	spin_unlock(&kafstimod_lock);

	/* call the timeout function */
	timer->ops->timed_out(timer);

	_debug("@@@ End Timeout");
	goto loop;

} /* end kafstimod() */

/*****************************************************************************/
/*
 * (re-)queue a timer
 */
void afs_kafstimod_add_timer(struct afs_timer *timer, unsigned long timeout)
{
	struct afs_timer *ptimer;
	struct list_head *_p;

	_enter("%p,%lu", timer, timeout);

	spin_lock(&kafstimod_lock);

	list_del(&timer->link);

	/* the timer was deferred or reset - put it back in the queue at the
	 * right place */
	timer->timo_jif = jiffies + timeout;

	list_for_each(_p, &kafstimod_list) {
		ptimer = list_entry(_p, struct afs_timer, link);
		if (time_before(timer->timo_jif, ptimer->timo_jif))
			break;
	}

	list_add_tail(&timer->link, _p); /* insert before stopping point */

	spin_unlock(&kafstimod_lock);

	wake_up(&kafstimod_sleepq);

	_leave("");
} /* end afs_kafstimod_add_timer() */

/*****************************************************************************/
/*
 * dequeue a timer
 * - returns 0 if the timer was deleted or -ENOENT if it wasn't queued
 */
int afs_kafstimod_del_timer(struct afs_timer *timer)
{
	int ret = 0;

	_enter("%p", timer);

	spin_lock(&kafstimod_lock);

	if (list_empty(&timer->link))
		ret = -ENOENT;
	else
		list_del_init(&timer->link);

	spin_unlock(&kafstimod_lock);

	wake_up(&kafstimod_sleepq);

	_leave(" = %d", ret);
	return ret;
} /* end afs_kafstimod_del_timer() */
