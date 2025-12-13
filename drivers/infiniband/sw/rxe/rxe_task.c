// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include "rxe.h"

static struct workqueue_struct *rxe_wq;

int rxe_alloc_wq(void)
{
	rxe_wq = alloc_workqueue("rxe_wq", WQ_UNBOUND, WQ_MAX_ACTIVE);
	if (!rxe_wq)
		return -ENOMEM;

	return 0;
}

void rxe_destroy_wq(void)
{
	destroy_workqueue(rxe_wq);
}

/* Check if task is idle i.e. not running, not scheduled in
 * work queue and not draining. If so move to busy to
 * reserve a slot in do_task() by setting to busy and taking
 * a qp reference to cover the gap from now until the task finishes.
 * state will move out of busy if task returns a non zero value
 * in do_task(). If state is already busy it is raised to armed
 * to indicate to do_task that additional pass should be made
 * over the task.
 * Context: caller should hold task->lock.
 * Returns: true if state transitioned from idle to busy else false.
 */
static bool __reserve_if_idle(struct rxe_task *task)
{
	WARN_ON(rxe_read(task->qp) <= 0);

	if (task->state == TASK_STATE_IDLE) {
		rxe_get(task->qp);
		task->state = TASK_STATE_BUSY;
		task->num_sched++;
		return true;
	}

	if (task->state == TASK_STATE_BUSY)
		task->state = TASK_STATE_ARMED;

	return false;
}

/* check if task is idle or drained and not currently
 * scheduled in the work queue. This routine is
 * called by rxe_cleanup_task or rxe_disable_task to
 * see if the queue is empty.
 * Context: caller should hold task->lock.
 * Returns true if done else false.
 */
static bool __is_done(struct rxe_task *task)
{
	if (work_pending(&task->work))
		return false;

	if (task->state == TASK_STATE_IDLE ||
	    task->state == TASK_STATE_DRAINED) {
		return true;
	}

	return false;
}

/* a locked version of __is_done */
static bool is_done(struct rxe_task *task)
{
	unsigned long flags;
	int done;

	spin_lock_irqsave(&task->lock, flags);
	done = __is_done(task);
	spin_unlock_irqrestore(&task->lock, flags);

	return done;
}

/* do_task is a wrapper for the three tasks (requester,
 * completer, responder) and calls them in a loop until
 * they return a non-zero value. It is called indirectly
 * when rxe_sched_task schedules the task. They must
 * call __reserve_if_idle to move the task to busy before
 * calling or scheduling. The task can also be moved to
 * drained or invalid by calls to rxe_cleanup_task or
 * rxe_disable_task. In that case tasks which get here
 * are not executed but just flushed. The tasks are
 * designed to look to see if there is work to do and
 * then do part of it before returning here with a return
 * value of zero until all the work has been consumed then
 * it returns a non-zero value.
 * The number of times the task can be run is limited by
 * max iterations so one task cannot hold the cpu forever.
 * If the limit is hit and work remains the task is rescheduled.
 */
static void do_task(struct rxe_task *task)
{
	unsigned int iterations;
	unsigned long flags;
	int resched = 0;
	int cont;
	int ret;

	WARN_ON(rxe_read(task->qp) <= 0);

	spin_lock_irqsave(&task->lock, flags);
	if (task->state >= TASK_STATE_DRAINED) {
		rxe_put(task->qp);
		task->num_done++;
		spin_unlock_irqrestore(&task->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&task->lock, flags);

	do {
		iterations = RXE_MAX_ITERATIONS;
		cont = 0;

		do {
			ret = task->func(task->qp);
		} while (ret == 0 && iterations-- > 0);

		spin_lock_irqsave(&task->lock, flags);
		/* we're not done yet but we ran out of iterations.
		 * yield the cpu and reschedule the task
		 */
		if (!ret) {
			if (task->state != TASK_STATE_DRAINING) {
				task->state = TASK_STATE_IDLE;
				resched = 1;
			} else {
				cont = 1;
			}
			goto exit;
		}

		switch (task->state) {
		case TASK_STATE_BUSY:
			task->state = TASK_STATE_IDLE;
			break;

		/* someone tried to schedule the task while we
		 * were running, keep going
		 */
		case TASK_STATE_ARMED:
			task->state = TASK_STATE_BUSY;
			cont = 1;
			break;

		case TASK_STATE_DRAINING:
			task->state = TASK_STATE_DRAINED;
			break;

		default:
			WARN_ON(1);
			rxe_dbg_qp(task->qp, "unexpected task state = %d\n",
				   task->state);
			task->state = TASK_STATE_IDLE;
		}

exit:
		if (!cont) {
			task->num_done++;
			if (WARN_ON(task->num_done != task->num_sched))
				rxe_dbg_qp(
					task->qp,
					"%ld tasks scheduled, %ld tasks done\n",
					task->num_sched, task->num_done);
		}
		spin_unlock_irqrestore(&task->lock, flags);
	} while (cont);

	task->ret = ret;

	if (resched)
		rxe_sched_task(task);

	rxe_put(task->qp);
}

/* wrapper around do_task to fix argument for work queue */
static void do_work(struct work_struct *work)
{
	do_task(container_of(work, struct rxe_task, work));
}

int rxe_init_task(struct rxe_task *task, struct rxe_qp *qp,
		  int (*func)(struct rxe_qp *))
{
	WARN_ON(rxe_read(qp) <= 0);

	task->qp = qp;
	task->func = func;
	task->state = TASK_STATE_IDLE;
	spin_lock_init(&task->lock);
	INIT_WORK(&task->work, do_work);

	return 0;
}

/* rxe_cleanup_task is only called from rxe_do_qp_cleanup in
 * process context. The qp is already completed with no
 * remaining references. Once the queue is drained the
 * task is moved to invalid and returns. The qp cleanup
 * code then calls the task functions directly without
 * using the task struct to drain any late arriving packets
 * or work requests.
 */
void rxe_cleanup_task(struct rxe_task *task)
{
	unsigned long flags;

	spin_lock_irqsave(&task->lock, flags);
	if (!__is_done(task) && task->state < TASK_STATE_DRAINED) {
		task->state = TASK_STATE_DRAINING;
	} else {
		task->state = TASK_STATE_INVALID;
		spin_unlock_irqrestore(&task->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&task->lock, flags);

	/* now the task cannot be scheduled or run just wait
	 * for the previously scheduled tasks to finish.
	 */
	while (!is_done(task))
		cond_resched();

	spin_lock_irqsave(&task->lock, flags);
	task->state = TASK_STATE_INVALID;
	spin_unlock_irqrestore(&task->lock, flags);
}

/* schedule the task to run later as a work queue entry.
 * the queue_work call can be called holding
 * the lock.
 */
void rxe_sched_task(struct rxe_task *task)
{
	unsigned long flags;

	WARN_ON(rxe_read(task->qp) <= 0);

	spin_lock_irqsave(&task->lock, flags);
	if (__reserve_if_idle(task))
		queue_work(rxe_wq, &task->work);
	spin_unlock_irqrestore(&task->lock, flags);
}

/* rxe_disable/enable_task are only called from
 * rxe_modify_qp in process context. Task is moved
 * to the drained state by do_task.
 */
void rxe_disable_task(struct rxe_task *task)
{
	unsigned long flags;

	WARN_ON(rxe_read(task->qp) <= 0);

	spin_lock_irqsave(&task->lock, flags);
	if (!__is_done(task) && task->state < TASK_STATE_DRAINED) {
		task->state = TASK_STATE_DRAINING;
	} else {
		task->state = TASK_STATE_DRAINED;
		spin_unlock_irqrestore(&task->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&task->lock, flags);

	while (!is_done(task))
		cond_resched();

	spin_lock_irqsave(&task->lock, flags);
	task->state = TASK_STATE_DRAINED;
	spin_unlock_irqrestore(&task->lock, flags);
}

void rxe_enable_task(struct rxe_task *task)
{
	unsigned long flags;

	WARN_ON(rxe_read(task->qp) <= 0);

	spin_lock_irqsave(&task->lock, flags);
	if (task->state == TASK_STATE_INVALID) {
		spin_unlock_irqrestore(&task->lock, flags);
		return;
	}

	task->state = TASK_STATE_IDLE;
	spin_unlock_irqrestore(&task->lock, flags);
}
