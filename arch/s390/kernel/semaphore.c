/*
 *  linux/arch/s390/kernel/semaphore.c
 *
 *  S390 version
 *    Copyright (C) 1998-2000 IBM Corporation
 *    Author(s): Martin Schwidefsky
 *
 *  Derived from "linux/arch/i386/kernel/semaphore.c
 *    Copyright (C) 1999, Linus Torvalds
 *
 */
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <asm/semaphore.h>

/*
 * Atomically update sem->count. Equivalent to:
 *   old_val = sem->count.counter;
 *   new_val = ((old_val >= 0) ? old_val : 0) + incr;
 *   sem->count.counter = new_val;
 *   return old_val;
 */
static inline int __sem_update_count(struct semaphore *sem, int incr)
{
	int old_val, new_val;

	asm volatile(
		"	l	%0,0(%3)\n"
		"0:	ltr	%1,%0\n"
		"	jhe	1f\n"
		"	lhi	%1,0\n"
		"1:	ar	%1,%4\n"
		"	cs	%0,%1,0(%3)\n"
		"	jl	0b\n"
		: "=&d" (old_val), "=&d" (new_val), "=m" (sem->count)
		: "a" (&sem->count), "d" (incr), "m" (sem->count)
		: "cc");
	return old_val;
}

/*
 * The inline function up() incremented count but the result
 * was <= 0. This indicates that some process is waiting on
 * the semaphore. The semaphore is free and we'll wake the
 * first sleeping process, so we set count to 1 unless some
 * other cpu has called up in the meantime in which case
 * we just increment count by 1.
 */
void __up(struct semaphore *sem)
{
	__sem_update_count(sem, 1);
	wake_up(&sem->wait);
}

/*
 * The inline function down() decremented count and the result
 * was < 0. The wait loop will atomically test and update the
 * semaphore counter following the rules:
 *   count > 0: decrement count, wake up queue and exit.
 *   count <= 0: set count to -1, go to sleep.
 */
void __sched __down(struct semaphore * sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	__set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	add_wait_queue_exclusive(&sem->wait, &wait);
	while (__sem_update_count(sem, -1) <= 0) {
		schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	}
	remove_wait_queue(&sem->wait, &wait);
	__set_task_state(tsk, TASK_RUNNING);
	wake_up(&sem->wait);
}

/*
 * Same as __down() with an additional test for signals.
 * If a signal is pending the count is updated as follows:
 *   count > 0: wake up queue and exit.
 *   count <= 0: set count to 0, wake up queue and exit.
 */
int __sched __down_interruptible(struct semaphore * sem)
{
	int retval = 0;
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	__set_task_state(tsk, TASK_INTERRUPTIBLE);
	add_wait_queue_exclusive(&sem->wait, &wait);
	while (__sem_update_count(sem, -1) <= 0) {
		if (signal_pending(current)) {
			__sem_update_count(sem, 0);
			retval = -EINTR;
			break;
		}
		schedule();
		set_task_state(tsk, TASK_INTERRUPTIBLE);
	}
	remove_wait_queue(&sem->wait, &wait);
	__set_task_state(tsk, TASK_RUNNING);
	wake_up(&sem->wait);
	return retval;
}

