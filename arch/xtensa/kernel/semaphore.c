/*
 * arch/xtensa/kernel/semaphore.c
 *
 * Generic semaphore code. Buyer beware. Do your own specific changes
 * in <asm/semaphore-helper.h>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 *
 * Joe Taylor	<joe@tensilica.com, joetylr@yahoo.com>
 * Chris Zankel	<chris@zankel.net>
 * Marc Gauthier<marc@tensilica.com, marc@alumni.uwaterloo.ca>
 * Kevin Chea
 */

#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/init.h>
#include <asm/semaphore.h>
#include <asm/errno.h>

/*
 * These two _must_ execute atomically wrt each other.
 */

static __inline__ void wake_one_more(struct semaphore * sem)
{
	atomic_inc((atomic_t *)&sem->sleepers);
}

static __inline__ int waking_non_zero(struct semaphore *sem)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	if (sem->sleepers > 0) {
		sem->sleepers--;
		ret = 1;
	}
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

/*
 * waking_non_zero_interruptible:
 *	1	got the lock
 *	0	go to sleep
 *	-EINTR	interrupted
 *
 * We must undo the sem->count down_interruptible() increment while we are
 * protected by the spinlock in order to make atomic this atomic_inc() with the
 * atomic_read() in wake_one_more(), otherwise we can race. -arca
 */

static __inline__ int waking_non_zero_interruptible(struct semaphore *sem,
						struct task_struct *tsk)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	if (sem->sleepers > 0) {
		sem->sleepers--;
		ret = 1;
	} else if (signal_pending(tsk)) {
		atomic_inc(&sem->count);
		ret = -EINTR;
	}
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

/*
 * waking_non_zero_trylock:
 *	1	failed to lock
 *	0	got the lock
 *
 * We must undo the sem->count down_trylock() increment while we are
 * protected by the spinlock in order to make atomic this atomic_inc() with the
 * atomic_read() in wake_one_more(), otherwise we can race. -arca
 */

static __inline__ int waking_non_zero_trylock(struct semaphore *sem)
{
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	if (sem->sleepers <= 0)
		atomic_inc(&sem->count);
	else {
		sem->sleepers--;
		ret = 0;
	}
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

DEFINE_SPINLOCK(semaphore_wake_lock);

/*
 * Semaphores are implemented using a two-way counter:
 * The "count" variable is decremented for each process
 * that tries to sleep, while the "waking" variable is
 * incremented when the "up()" code goes to wake up waiting
 * processes.
 *
 * Notably, the inline "up()" and "down()" functions can
 * efficiently test if they need to do any extra work (up
 * needs to do something only if count was negative before
 * the increment operation.
 *
 * waking_non_zero() (from asm/semaphore.h) must execute
 * atomically.
 *
 * When __up() is called, the count was negative before
 * incrementing it, and we need to wake up somebody.
 *
 * This routine adds one to the count of processes that need to
 * wake up and exit.  ALL waiting processes actually wake up but
 * only the one that gets to the "waking" field first will gate
 * through and acquire the semaphore.  The others will go back
 * to sleep.
 *
 * Note that these functions are only called when there is
 * contention on the lock, and as such all this is the
 * "non-critical" part of the whole semaphore business. The
 * critical part is the inline stuff in <asm/semaphore.h>
 * where we want to avoid any extra jumps and calls.
 */

void __up(struct semaphore *sem)
{
	wake_one_more(sem);
	wake_up(&sem->wait);
}

/*
 * Perform the "down" function.  Return zero for semaphore acquired,
 * return negative for signalled out of the function.
 *
 * If called from __down, the return is ignored and the wait loop is
 * not interruptible.  This means that a task waiting on a semaphore
 * using "down()" cannot be killed until someone does an "up()" on
 * the semaphore.
 *
 * If called from __down_interruptible, the return value gets checked
 * upon return.  If the return value is negative then the task continues
 * with the negative value in the return register (it can be tested by
 * the caller).
 *
 * Either form may be used in conjunction with "up()".
 *
 */

#define DOWN_VAR				\
	struct task_struct *tsk = current;	\
	wait_queue_t wait;			\
	init_waitqueue_entry(&wait, tsk);

#define DOWN_HEAD(task_state)						\
									\
									\
	tsk->state = (task_state);					\
	add_wait_queue(&sem->wait, &wait);				\
									\
	/*								\
	 * Ok, we're set up.  sem->count is known to be less than zero	\
	 * so we must wait.						\
	 *								\
	 * We can let go the lock for purposes of waiting.		\
	 * We re-acquire it after awaking so as to protect		\
	 * all semaphore operations.					\
	 *								\
	 * If "up()" is called before we call waking_non_zero() then	\
	 * we will catch it right away.  If it is called later then	\
	 * we will have to go through a wakeup cycle to catch it.	\
	 *								\
	 * Multiple waiters contend for the semaphore lock to see	\
	 * who gets to gate through and who has to wait some more.	\
	 */								\
	for (;;) {

#define DOWN_TAIL(task_state)			\
		tsk->state = (task_state);	\
	}					\
	tsk->state = TASK_RUNNING;		\
	remove_wait_queue(&sem->wait, &wait);

void __sched __down(struct semaphore * sem)
{
	DOWN_VAR
	DOWN_HEAD(TASK_UNINTERRUPTIBLE)
	if (waking_non_zero(sem))
		break;
	schedule();
	DOWN_TAIL(TASK_UNINTERRUPTIBLE)
}

int __sched __down_interruptible(struct semaphore * sem)
{
	int ret = 0;
	DOWN_VAR
	DOWN_HEAD(TASK_INTERRUPTIBLE)

	ret = waking_non_zero_interruptible(sem, tsk);
	if (ret)
	{
		if (ret == 1)
			/* ret != 0 only if we get interrupted -arca */
			ret = 0;
		break;
	}
	schedule();
	DOWN_TAIL(TASK_INTERRUPTIBLE)
	return ret;
}

int __down_trylock(struct semaphore * sem)
{
	return waking_non_zero_trylock(sem);
}
