/*
 * Semaphore implementation Copyright (c) 2001 Matthew Wilcox, Hewlett-Packard
 */

#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/init.h>

/*
 * Semaphores are complex as we wish to avoid using two variables.
 * `count' has multiple roles, depending on its value.  If it is positive
 * or zero, there are no waiters.  The functions here will never be
 * called; see <asm/semaphore.h>
 *
 * When count is -1 it indicates there is at least one task waiting
 * for the semaphore.
 *
 * When count is less than that, there are '- count - 1' wakeups
 * pending.  ie if it has value -3, there are 2 wakeups pending.
 *
 * Note that these functions are only called when there is contention
 * on the lock, and as such all this is the "non-critical" part of the
 * whole semaphore business. The critical part is the inline stuff in
 * <asm/semaphore.h> where we want to avoid any extra jumps and calls.
 */
void __up(struct semaphore *sem)
{
	sem->count--;
	wake_up(&sem->wait);
}

#define wakers(count) (-1 - count)

#define DOWN_HEAD							\
	int ret = 0;							\
	DECLARE_WAITQUEUE(wait, current);				\
									\
	/* Note that someone is waiting */				\
	if (sem->count == 0)						\
		sem->count = -1;					\
									\
	/* protected by the sentry still -- use unlocked version */	\
	wait.flags = WQ_FLAG_EXCLUSIVE;					\
	__add_wait_queue_tail(&sem->wait, &wait);			\
 lost_race:								\
	spin_unlock_irq(&sem->sentry);					\

#define DOWN_TAIL							\
	spin_lock_irq(&sem->sentry);					\
	if (wakers(sem->count) == 0 && ret == 0)			\
		goto lost_race;	/* Someone stole our wakeup */		\
	__remove_wait_queue(&sem->wait, &wait);				\
	current->state = TASK_RUNNING;					\
	if (!waitqueue_active(&sem->wait) && (sem->count < 0))		\
		sem->count = wakers(sem->count);

#define UPDATE_COUNT							\
	sem->count += (sem->count < 0) ? 1 : - 1;
	

void __sched __down(struct semaphore * sem)
{
	DOWN_HEAD

	for(;;) {
		set_task_state(current, TASK_UNINTERRUPTIBLE);
		/* we can _read_ this without the sentry */
		if (sem->count != -1)
			break;
 		schedule();
 	}

	DOWN_TAIL
	UPDATE_COUNT
}

int __sched __down_interruptible(struct semaphore * sem)
{
	DOWN_HEAD

	for(;;) {
		set_task_state(current, TASK_INTERRUPTIBLE);
		/* we can _read_ this without the sentry */
		if (sem->count != -1)
			break;

		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}
		schedule();
	}

	DOWN_TAIL

	if (!ret) {
		UPDATE_COUNT
	}

	return ret;
}
