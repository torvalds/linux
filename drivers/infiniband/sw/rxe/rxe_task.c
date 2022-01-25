// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/hardirq.h>

#include "rxe_task.h"

int __rxe_do_task(struct rxe_task *task)

{
	int ret;

	while ((ret = task->func(task->arg)) == 0)
		;

	task->ret = ret;

	return ret;
}

/*
 * this locking is due to a potential race where
 * a second caller finds the task already running
 * but looks just after the last call to func
 */
void rxe_do_task(struct tasklet_struct *t)
{
	int cont;
	int ret;
	struct rxe_task *task = from_tasklet(task, t, tasklet);

	spin_lock_bh(&task->state_lock);
	switch (task->state) {
	case TASK_STATE_START:
		task->state = TASK_STATE_BUSY;
		spin_unlock_bh(&task->state_lock);
		break;

	case TASK_STATE_BUSY:
		task->state = TASK_STATE_ARMED;
		fallthrough;
	case TASK_STATE_ARMED:
		spin_unlock_bh(&task->state_lock);
		return;

	default:
		spin_unlock_bh(&task->state_lock);
		pr_warn("%s failed with bad state %d\n", __func__, task->state);
		return;
	}

	do {
		cont = 0;
		ret = task->func(task->arg);

		spin_lock_bh(&task->state_lock);
		switch (task->state) {
		case TASK_STATE_BUSY:
			if (ret)
				task->state = TASK_STATE_START;
			else
				cont = 1;
			break;

		/* soneone tried to run the task since the last time we called
		 * func, so we will call one more time regardless of the
		 * return value
		 */
		case TASK_STATE_ARMED:
			task->state = TASK_STATE_BUSY;
			cont = 1;
			break;

		default:
			pr_warn("%s failed with bad state %d\n", __func__,
				task->state);
		}
		spin_unlock_bh(&task->state_lock);
	} while (cont);

	task->ret = ret;
}

int rxe_init_task(void *obj, struct rxe_task *task,
		  void *arg, int (*func)(void *), char *name)
{
	task->obj	= obj;
	task->arg	= arg;
	task->func	= func;
	snprintf(task->name, sizeof(task->name), "%s", name);
	task->destroyed	= false;

	tasklet_setup(&task->tasklet, rxe_do_task);

	task->state = TASK_STATE_START;
	spin_lock_init(&task->state_lock);

	return 0;
}

void rxe_cleanup_task(struct rxe_task *task)
{
	bool idle;

	/*
	 * Mark the task, then wait for it to finish. It might be
	 * running in a non-tasklet (direct call) context.
	 */
	task->destroyed = true;

	do {
		spin_lock_bh(&task->state_lock);
		idle = (task->state == TASK_STATE_START);
		spin_unlock_bh(&task->state_lock);
	} while (!idle);

	tasklet_kill(&task->tasklet);
}

void rxe_run_task(struct rxe_task *task, int sched)
{
	if (task->destroyed)
		return;

	if (sched)
		tasklet_schedule(&task->tasklet);
	else
		rxe_do_task(&task->tasklet);
}

void rxe_disable_task(struct rxe_task *task)
{
	tasklet_disable(&task->tasklet);
}

void rxe_enable_task(struct rxe_task *task)
{
	tasklet_enable(&task->tasklet);
}
