/* kafsasyncd.c: AFS asynchronous operation daemon
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 *
 * The AFS async daemon is used to the following:
 * - probe "dead" servers to see whether they've come back to life yet.
 * - probe "live" servers that we haven't talked to for a while to see if they are better
 *   candidates for serving than what we're currently using
 * - poll volume location servers to keep up to date volume location lists
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include "cell.h"
#include "server.h"
#include "volume.h"
#include "kafsasyncd.h"
#include "kafstimod.h"
#include <rxrpc/call.h>
#include <asm/errno.h>
#include "internal.h"

static DECLARE_COMPLETION(kafsasyncd_alive);
static DECLARE_COMPLETION(kafsasyncd_dead);
static DECLARE_WAIT_QUEUE_HEAD(kafsasyncd_sleepq);
static struct task_struct *kafsasyncd_task;
static int kafsasyncd_die;

static int kafsasyncd(void *arg);

static LIST_HEAD(kafsasyncd_async_attnq);
static LIST_HEAD(kafsasyncd_async_busyq);
static DEFINE_SPINLOCK(kafsasyncd_async_lock);

static void kafsasyncd_null_call_attn_func(struct rxrpc_call *call)
{
}

static void kafsasyncd_null_call_error_func(struct rxrpc_call *call)
{
}

/*****************************************************************************/
/*
 * start the async daemon
 */
int afs_kafsasyncd_start(void)
{
	int ret;

	ret = kernel_thread(kafsasyncd, NULL, 0);
	if (ret < 0)
		return ret;

	wait_for_completion(&kafsasyncd_alive);

	return ret;
} /* end afs_kafsasyncd_start() */

/*****************************************************************************/
/*
 * stop the async daemon
 */
void afs_kafsasyncd_stop(void)
{
	/* get rid of my daemon */
	kafsasyncd_die = 1;
	wake_up(&kafsasyncd_sleepq);
	wait_for_completion(&kafsasyncd_dead);

} /* end afs_kafsasyncd_stop() */

/*****************************************************************************/
/*
 * probing daemon
 */
static int kafsasyncd(void *arg)
{
	struct afs_async_op *op;
	int die;

	DECLARE_WAITQUEUE(myself, current);

	kafsasyncd_task = current;

	printk("kAFS: Started kafsasyncd %d\n", current->pid);

	daemonize("kafsasyncd");

	complete(&kafsasyncd_alive);

	/* loop around looking for things to attend to */
	do {
		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&kafsasyncd_sleepq, &myself);

		for (;;) {
			if (!list_empty(&kafsasyncd_async_attnq) ||
			    signal_pending(current) ||
			    kafsasyncd_die)
				break;

			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
		}

		remove_wait_queue(&kafsasyncd_sleepq, &myself);
		set_current_state(TASK_RUNNING);

		try_to_freeze();

		/* discard pending signals */
		afs_discard_my_signals();

		die = kafsasyncd_die;

		/* deal with the next asynchronous operation requiring
		 * attention */
		if (!list_empty(&kafsasyncd_async_attnq)) {
			struct afs_async_op *op;

			_debug("@@@ Begin Asynchronous Operation");

			op = NULL;
			spin_lock(&kafsasyncd_async_lock);

			if (!list_empty(&kafsasyncd_async_attnq)) {
				op = list_entry(kafsasyncd_async_attnq.next,
						struct afs_async_op, link);
				list_del(&op->link);
				list_add_tail(&op->link,
					      &kafsasyncd_async_busyq);
			}

			spin_unlock(&kafsasyncd_async_lock);

			_debug("@@@ Operation %p {%p}\n",
			       op, op ? op->ops : NULL);

			if (op)
				op->ops->attend(op);

			_debug("@@@ End Asynchronous Operation");
		}

	} while(!die);

	/* need to kill all outstanding asynchronous operations before
	 * exiting */
	kafsasyncd_task = NULL;
	spin_lock(&kafsasyncd_async_lock);

	/* fold the busy and attention queues together */
	list_splice_init(&kafsasyncd_async_busyq,
			 &kafsasyncd_async_attnq);

	/* dequeue kafsasyncd from all their wait queues */
	list_for_each_entry(op, &kafsasyncd_async_attnq, link) {
		op->call->app_attn_func = kafsasyncd_null_call_attn_func;
		op->call->app_error_func = kafsasyncd_null_call_error_func;
		remove_wait_queue(&op->call->waitq, &op->waiter);
	}

	spin_unlock(&kafsasyncd_async_lock);

	/* abort all the operations */
	while (!list_empty(&kafsasyncd_async_attnq)) {
		op = list_entry(kafsasyncd_async_attnq.next, struct afs_async_op, link);
		list_del_init(&op->link);

		rxrpc_call_abort(op->call, -EIO);
		rxrpc_put_call(op->call);
		op->call = NULL;

		op->ops->discard(op);
	}

	/* and that's all */
	_leave("");
	complete_and_exit(&kafsasyncd_dead, 0);

} /* end kafsasyncd() */

/*****************************************************************************/
/*
 * begin an operation
 * - place operation on busy queue
 */
void afs_kafsasyncd_begin_op(struct afs_async_op *op)
{
	_enter("");

	spin_lock(&kafsasyncd_async_lock);

	init_waitqueue_entry(&op->waiter, kafsasyncd_task);
	add_wait_queue(&op->call->waitq, &op->waiter);

	list_del(&op->link);
	list_add_tail(&op->link, &kafsasyncd_async_busyq);

	spin_unlock(&kafsasyncd_async_lock);

	_leave("");
} /* end afs_kafsasyncd_begin_op() */

/*****************************************************************************/
/*
 * request attention for an operation
 * - move to attention queue
 */
void afs_kafsasyncd_attend_op(struct afs_async_op *op)
{
	_enter("");

	spin_lock(&kafsasyncd_async_lock);

	list_del(&op->link);
	list_add_tail(&op->link, &kafsasyncd_async_attnq);

	spin_unlock(&kafsasyncd_async_lock);

	wake_up(&kafsasyncd_sleepq);

	_leave("");
} /* end afs_kafsasyncd_attend_op() */

/*****************************************************************************/
/*
 * terminate an operation
 * - remove from either queue
 */
void afs_kafsasyncd_terminate_op(struct afs_async_op *op)
{
	_enter("");

	spin_lock(&kafsasyncd_async_lock);

	if (!list_empty(&op->link)) {
		list_del_init(&op->link);
		remove_wait_queue(&op->call->waitq, &op->waiter);
	}

	spin_unlock(&kafsasyncd_async_lock);

	wake_up(&kafsasyncd_sleepq);

	_leave("");
} /* end afs_kafsasyncd_terminate_op() */
