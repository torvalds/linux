/*
 * MIPS-specific semaphore code.
 *
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 * Copyright (C) 2004 Ralf Baechle <ralf@linux-mips.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * April 2001 - Reworked by Paul Mackerras <paulus@samba.org>
 * to eliminate the SMP races in the old version between the updates
 * of `count' and `waking'.  Now we use negative `count' values to
 * indicate that some process(es) are waiting for the semaphore.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <asm/atomic.h>
#include <asm/cpu-features.h>
#include <asm/errno.h>
#include <asm/semaphore.h>
#include <asm/war.h>
/*
 * Atomically update sem->count.
 * This does the equivalent of the following:
 *
 *	old_count = sem->count;
 *	tmp = MAX(old_count, 0) + incr;
 *	sem->count = tmp;
 *	return old_count;
 *
 * On machines without lld/scd we need a spinlock to make the manipulation of
 * sem->count and sem->waking atomic.  Scalability isn't an issue because
 * this lock is used on UP only so it's just an empty variable.
 */
static inline int __sem_update_count(struct semaphore *sem, int incr)
{
	int old_count, tmp;

	if (cpu_has_llsc && R10000_LLSC_WAR) {
		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	ll	%0, %2		# __sem_update_count	\n"
		"	sra	%1, %0, 31				\n"
		"	not	%1					\n"
		"	and	%1, %0, %1				\n"
		"	addu	%1, %1, %3				\n"
		"	sc	%1, %2					\n"
		"	beqzl	%1, 1b					\n"
		"	.set	mips0					\n"
		: "=&r" (old_count), "=&r" (tmp), "=m" (sem->count)
		: "r" (incr), "m" (sem->count));
	} else if (cpu_has_llsc) {
		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	ll	%0, %2		# __sem_update_count	\n"
		"	sra	%1, %0, 31				\n"
		"	not	%1					\n"
		"	and	%1, %0, %1				\n"
		"	addu	%1, %1, %3				\n"
		"	sc	%1, %2					\n"
		"	beqz	%1, 1b					\n"
		"	.set	mips0					\n"
		: "=&r" (old_count), "=&r" (tmp), "=m" (sem->count)
		: "r" (incr), "m" (sem->count));
	} else {
		static DEFINE_SPINLOCK(semaphore_lock);
		unsigned long flags;

		spin_lock_irqsave(&semaphore_lock, flags);
		old_count = atomic_read(&sem->count);
		tmp = max_t(int, old_count, 0) + incr;
		atomic_set(&sem->count, tmp);
		spin_unlock_irqrestore(&semaphore_lock, flags);
	}

	return old_count;
}

void __up(struct semaphore *sem)
{
	/*
	 * Note that we incremented count in up() before we came here,
	 * but that was ineffective since the result was <= 0, and
	 * any negative value of count is equivalent to 0.
	 * This ends up setting count to 1, unless count is now > 0
	 * (i.e. because some other cpu has called up() in the meantime),
	 * in which case we just increment count.
	 */
	__sem_update_count(sem, 1);
	wake_up(&sem->wait);
}

EXPORT_SYMBOL(__up);

/*
 * Note that when we come in to __down or __down_interruptible,
 * we have already decremented count, but that decrement was
 * ineffective since the result was < 0, and any negative value
 * of count is equivalent to 0.
 * Thus it is only when we decrement count from some value > 0
 * that we have actually got the semaphore.
 */
void __sched __down(struct semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	__set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	add_wait_queue_exclusive(&sem->wait, &wait);

	/*
	 * Try to get the semaphore.  If the count is > 0, then we've
	 * got the semaphore; we decrement count and exit the loop.
	 * If the count is 0 or negative, we set it to -1, indicating
	 * that we are asleep, and then sleep.
	 */
	while (__sem_update_count(sem, -1) <= 0) {
		schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	}
	remove_wait_queue(&sem->wait, &wait);
	__set_task_state(tsk, TASK_RUNNING);

	/*
	 * If there are any more sleepers, wake one of them up so
	 * that it can either get the semaphore, or set count to -1
	 * indicating that there are still processes sleeping.
	 */
	wake_up(&sem->wait);
}

EXPORT_SYMBOL(__down);

int __sched __down_interruptible(struct semaphore * sem)
{
	int retval = 0;
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	__set_task_state(tsk, TASK_INTERRUPTIBLE);
	add_wait_queue_exclusive(&sem->wait, &wait);

	while (__sem_update_count(sem, -1) <= 0) {
		if (signal_pending(current)) {
			/*
			 * A signal is pending - give up trying.
			 * Set sem->count to 0 if it is negative,
			 * since we are no longer sleeping.
			 */
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

EXPORT_SYMBOL(__down_interruptible);
