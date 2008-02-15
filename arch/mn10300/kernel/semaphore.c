/* MN10300 Semaphore implementation
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/sched.h>
#include <linux/module.h>
#include <asm/semaphore.h>

struct sem_waiter {
	struct list_head	list;
	struct task_struct	*task;
};

#if SEMAPHORE_DEBUG
void semtrace(struct semaphore *sem, const char *str)
{
	if (sem->debug)
		printk(KERN_DEBUG "[%d] %s({%d,%d})\n",
		       current->pid,
		       str,
		       atomic_read(&sem->count),
		       list_empty(&sem->wait_list) ? 0 : 1);
}
#else
#define semtrace(SEM, STR) do { } while (0)
#endif

/*
 * wait for a token to be granted from a semaphore
 * - entered with lock held and interrupts disabled
 */
void __down(struct semaphore *sem, unsigned long flags)
{
	struct task_struct *tsk = current;
	struct sem_waiter waiter;

	semtrace(sem, "Entering __down");

	/* set up my own style of waitqueue */
	waiter.task = tsk;
	get_task_struct(tsk);

	list_add_tail(&waiter.list, &sem->wait_list);

	/* we don't need to touch the semaphore struct anymore */
	spin_unlock_irqrestore(&sem->wait_lock, flags);

	/* wait to be given the semaphore */
	set_task_state(tsk, TASK_UNINTERRUPTIBLE);

	for (;;) {
		if (!waiter.task)
			break;
		schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	}

	tsk->state = TASK_RUNNING;
	semtrace(sem, "Leaving __down");
}
EXPORT_SYMBOL(__down);

/*
 * interruptibly wait for a token to be granted from a semaphore
 * - entered with lock held and interrupts disabled
 */
int __down_interruptible(struct semaphore *sem, unsigned long flags)
{
	struct task_struct *tsk = current;
	struct sem_waiter waiter;
	int ret;

	semtrace(sem, "Entering __down_interruptible");

	/* set up my own style of waitqueue */
	waiter.task = tsk;
	get_task_struct(tsk);

	list_add_tail(&waiter.list, &sem->wait_list);

	/* we don't need to touch the semaphore struct anymore */
	set_task_state(tsk, TASK_INTERRUPTIBLE);

	spin_unlock_irqrestore(&sem->wait_lock, flags);

	/* wait to be given the semaphore */
	ret = 0;
	for (;;) {
		if (!waiter.task)
			break;
		if (unlikely(signal_pending(current)))
			goto interrupted;
		schedule();
		set_task_state(tsk, TASK_INTERRUPTIBLE);
	}

 out:
	tsk->state = TASK_RUNNING;
	semtrace(sem, "Leaving __down_interruptible");
	return ret;

 interrupted:
	spin_lock_irqsave(&sem->wait_lock, flags);
	list_del(&waiter.list);
	spin_unlock_irqrestore(&sem->wait_lock, flags);

	ret = 0;
	if (!waiter.task) {
		put_task_struct(current);
		ret = -EINTR;
	}
	goto out;
}
EXPORT_SYMBOL(__down_interruptible);

/*
 * release a single token back to a semaphore
 * - entered with lock held and interrupts disabled
 */
void __up(struct semaphore *sem)
{
	struct task_struct *tsk;
	struct sem_waiter *waiter;

	semtrace(sem, "Entering __up");

	/* grant the token to the process at the front of the queue */
	waiter = list_entry(sem->wait_list.next, struct sem_waiter, list);

	/* We must be careful not to touch 'waiter' after we set ->task = NULL.
	 * It is an allocated on the waiter's stack and may become invalid at
	 * any time after that point (due to a wakeup from another source).
	 */
	list_del_init(&waiter->list);
	tsk = waiter->task;
	smp_mb();
	waiter->task = NULL;
	wake_up_process(tsk);
	put_task_struct(tsk);

	semtrace(sem, "Leaving __up");
}
EXPORT_SYMBOL(__up);
