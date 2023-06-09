// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include "rxe.h"

/* Check if task is idle i.e. not running, not scheduled in
 * tasklet queue and not draining. If so move to busy to
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

	if (task->tasklet.state & BIT(TASKLET_STATE_SCHED))
		return false;

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
 * scheduled in the tasklet queue. This routine is
 * called by rxe_cleanup_task or rxe_disable_task to
 * see if the queue is empty.
 * Context: caller should hold task->lock.
 * Returns true if done else false.
 */
static bool __is_done(struct rxe_task *task)
{
	if (task->tasklet.state & BIT(TASKLET_STATE_SCHED))
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
 * they return a non-zero value. It is called either
 * directly by rxe_run_task or indirectly if rxe_sched_task
 * schedules the task. They must call __reserve_if_idle to
 * move the task to busy before calling or scheduling.
 * The task can also be moved to drained or invalid
 * by calls to rxe-cleanup_task or rxe_disable_task.
 * In that case tasks which get here are not executed but
 * just flushed. The tasks are designed to look to see if
 * there is work to do and do part of it before returning
 * here with a return value of zero until all the work
 * has been consumed then it retuens a non-zero value.
 * The number of times the task can be run is limited by
 * max iterations so one task cannot hold the cpu forever.
 */
static void do_task(struct tasklet_struct *t)
{
	int cont;
	int ret;
	struct rxe_task *task = from_tasklet(task, t, tasklet);
	unsigned int iterations;
	unsigned long flags;
	int resched = 0;

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
		switch (task->state) {
		case TASK_STATE_BUSY:
			if (ret) {
				task->state = TASK_STATE_IDLE;
			} else {
				/* This can happen if the client
				 * can add work faster than the
				 * tasklet can finish it.
				 * Reschedule the tasklet and exit
				 * the loop to give up the cpu
				 */
				task->state = TASK_STATE_IDLE;
				resched = 1;
			}
			break;

		/* someone tried to run the task since the last time we called
		 * func, so we will call one more time regardless of the
		 * return value
		 */
		case TASK_STATE_ARMED:
			task->state = TASK_STATE_BUSY;
			cont = 1;
			break;

		case TASK_STATE_DRAINING:
			if (ret)
				task->state = TASK_STATE_DRAINED;
			else
				cont = 1;
			break;

		default:
			WARN_ON(1);
			rxe_info_qp(task->qp, "unexpected task state = %d", task->state);
		}

		if (!cont) {
			task->num_done++;
			if (WARN_ON(task->num_done != task->num_sched))
				rxe_err_qp(task->qp, "%ld tasks scheduled, %ld tasks done",
					   task->num_sched, task->num_done);
		}
		spin_unlock_irqrestore(&task->lock, flags);
	} while (cont);

	task->ret = ret;

	if (resched)
		rxe_sched_task(task);

	rxe_put(task->qp);
}

int rxe_init_task(struct rxe_task *task, struct rxe_qp *qp,
		  int (*func)(struct rxe_qp *))
{
	WARN_ON(rxe_read(qp) <= 0);

	task->qp = qp;
	task->func = func;

	tasklet_setup(&task->tasklet, do_task);

	task->state = TASK_STATE_IDLE;
	spin_lock_init(&task->lock);

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

	tasklet_kill(&task->tasklet);

	spin_lock_irqsave(&task->lock, flags);
	task->state = TASK_STATE_INVALID;
	spin_unlock_irqrestore(&task->lock, flags);
}

/* run the task inline if it is currently idle
 * cannot call do_task holding the lock
 */
void rxe_run_task(struct rxe_task *task)
{
	unsigned long flags;
	int run;

	WARN_ON(rxe_read(task->qp) <= 0);

	spin_lock_irqsave(&task->lock, flags);
	run = __reserve_if_idle(task);
	spin_unlock_irqrestore(&task->lock, flags);

	if (run)
		do_task(&task->tasklet);
}

/* schedule the task to run later as a tasklet.
 * the tasklet)schedule call can be called holding
 * the lock.
 */
void rxe_sched_task(struct rxe_task *task)
{
	unsigned long flags;

	WARN_ON(rxe_read(task->qp) <= 0);

	spin_lock_irqsave(&task->lock, flags);
	if (__reserve_if_idle(task))
		tasklet_schedule(&task->tasklet);
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

	tasklet_disable(&task->tasklet);
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
	tasklet_enable(&task->tasklet);
	spin_unlock_irqrestore(&task->lock, flags);
}
