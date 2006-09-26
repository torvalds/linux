/*
 * AVR32 sempahore implementation.
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * Based on linux/arch/i386/kernel/semaphore.c
 *  Copyright (C) 1999 Linus Torvalds
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/module.h>

#include <asm/semaphore.h>
#include <asm/atomic.h>

/*
 * Semaphores are implemented using a two-way counter:
 * The "count" variable is decremented for each process
 * that tries to acquire the semaphore, while the "sleeping"
 * variable is a count of such acquires.
 *
 * Notably, the inline "up()" and "down()" functions can
 * efficiently test if they need to do any extra work (up
 * needs to do something only if count was negative before
 * the increment operation.
 *
 * "sleeping" and the contention routine ordering is protected
 * by the spinlock in the semaphore's waitqueue head.
 *
 * Note that these functions are only called when there is
 * contention on the lock, and as such all this is the
 * "non-critical" part of the whole semaphore business. The
 * critical part is the inline stuff in <asm/semaphore.h>
 * where we want to avoid any extra jumps and calls.
 */

/*
 * Logic:
 *  - only on a boundary condition do we need to care. When we go
 *    from a negative count to a non-negative, we wake people up.
 *  - when we go from a non-negative count to a negative do we
 *    (a) synchronize with the "sleeper" count and (b) make sure
 *    that we're on the wakeup list before we synchronize so that
 *    we cannot lose wakeup events.
 */

void __up(struct semaphore *sem)
{
	wake_up(&sem->wait);
}
EXPORT_SYMBOL(__up);

void __sched __down(struct semaphore *sem)
{
	struct task_struct *tsk = current;
        DECLARE_WAITQUEUE(wait, tsk);
        unsigned long flags;

        tsk->state = TASK_UNINTERRUPTIBLE;
        spin_lock_irqsave(&sem->wait.lock, flags);
        add_wait_queue_exclusive_locked(&sem->wait, &wait);

        sem->sleepers++;
        for (;;) {
                int sleepers = sem->sleepers;

                /*
                 * Add "everybody else" into it. They aren't
                 * playing, because we own the spinlock in
                 * the wait_queue_head.
                 */
                if (atomic_add_return(sleepers - 1, &sem->count) >= 0) {
                        sem->sleepers = 0;
                        break;
                }
                sem->sleepers = 1;      /* us - see -1 above */
                spin_unlock_irqrestore(&sem->wait.lock, flags);

                schedule();

                spin_lock_irqsave(&sem->wait.lock, flags);
                tsk->state = TASK_UNINTERRUPTIBLE;
        }
        remove_wait_queue_locked(&sem->wait, &wait);
        wake_up_locked(&sem->wait);
        spin_unlock_irqrestore(&sem->wait.lock, flags);
        tsk->state = TASK_RUNNING;
}
EXPORT_SYMBOL(__down);

int __sched __down_interruptible(struct semaphore *sem)
{
	int retval = 0;
	struct task_struct *tsk = current;
        DECLARE_WAITQUEUE(wait, tsk);
        unsigned long flags;

        tsk->state = TASK_INTERRUPTIBLE;
        spin_lock_irqsave(&sem->wait.lock, flags);
        add_wait_queue_exclusive_locked(&sem->wait, &wait);

        sem->sleepers++;
        for (;;) {
                int sleepers = sem->sleepers;

		/*
		 * With signals pending, this turns into the trylock
		 * failure case - we won't be sleeping, and we can't
		 * get the lock as it has contention. Just correct the
		 * count and exit.
		 */
		if (signal_pending(current)) {
			retval = -EINTR;
			sem->sleepers = 0;
			atomic_add(sleepers, &sem->count);
			break;
		}

                /*
                 * Add "everybody else" into it. They aren't
                 * playing, because we own the spinlock in
                 * the wait_queue_head.
                 */
                if (atomic_add_return(sleepers - 1, &sem->count) >= 0) {
                        sem->sleepers = 0;
                        break;
                }
                sem->sleepers = 1;      /* us - see -1 above */
                spin_unlock_irqrestore(&sem->wait.lock, flags);

                schedule();

                spin_lock_irqsave(&sem->wait.lock, flags);
                tsk->state = TASK_INTERRUPTIBLE;
        }
        remove_wait_queue_locked(&sem->wait, &wait);
        wake_up_locked(&sem->wait);
        spin_unlock_irqrestore(&sem->wait.lock, flags);

        tsk->state = TASK_RUNNING;
	return retval;
}
EXPORT_SYMBOL(__down_interruptible);
