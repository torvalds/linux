/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#ifndef RXE_TASK_H
#define RXE_TASK_H

enum {
	TASK_STATE_IDLE		= 0,
	TASK_STATE_BUSY		= 1,
	TASK_STATE_ARMED	= 2,
	TASK_STATE_DRAINING	= 3,
	TASK_STATE_DRAINED	= 4,
	TASK_STATE_INVALID	= 5,
};

/*
 * data structure to describe a 'task' which is a short
 * function that returns 0 as long as it needs to be
 * called again.
 */
struct rxe_task {
	struct work_struct	work;
	int			state;
	spinlock_t		lock;
	struct rxe_qp		*qp;
	int			(*func)(struct rxe_qp *qp);
	int			ret;
	long			num_sched;
	long			num_done;
};

int rxe_alloc_wq(void);

void rxe_destroy_wq(void);

/*
 * init rxe_task structure
 *	qp  => parameter to pass to func
 *	func => function to call until it returns != 0
 */
int rxe_init_task(struct rxe_task *task, struct rxe_qp *qp,
		  int (*func)(struct rxe_qp *));

/* cleanup task */
void rxe_cleanup_task(struct rxe_task *task);

void rxe_sched_task(struct rxe_task *task);

/* keep a task from scheduling */
void rxe_disable_task(struct rxe_task *task);

/* allow task to run */
void rxe_enable_task(struct rxe_task *task);

#endif /* RXE_TASK_H */
