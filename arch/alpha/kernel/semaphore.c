/*
 * Alpha semaphore implementation.
 *
 * (C) Copyright 1996 Linus Torvalds
 * (C) Copyright 1999, 2000 Richard Henderson
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/init.h>

/*
 * This is basically the PPC semaphore scheme ported to use
 * the Alpha ll/sc sequences, so see the PPC code for
 * credits.
 */

/*
 * Atomically update sem->count.
 * This does the equivalent of the following:
 *
 *	old_count = sem->count;
 *	tmp = MAX(old_count, 0) + incr;
 *	sem->count = tmp;
 *	return old_count;
 */
static inline int __sem_update_count(struct semaphore *sem, int incr)
{
	long old_count, tmp = 0;

	__asm__ __volatile__(
	"1:	ldl_l	%0,%2\n"
	"	cmovgt	%0,%0,%1\n"
	"	addl	%1,%3,%1\n"
	"	stl_c	%1,%2\n"
	"	beq	%1,2f\n"
	"	mb\n"
	".subsection 2\n"
	"2:	br	1b\n"
	".previous"
	: "=&r" (old_count), "=&r" (tmp), "=m" (sem->count)
	: "Ir" (incr), "1" (tmp), "m" (sem->count));

	return old_count;
}

/*
 * Perform the "down" function.  Return zero for semaphore acquired,
 * return negative for signalled out of the function.
 *
 * If called from down, the return is ignored and the wait loop is
 * not interruptible.  This means that a task waiting on a semaphore
 * using "down()" cannot be killed until someone does an "up()" on
 * the semaphore.
 *
 * If called from down_interruptible, the return value gets checked
 * upon return.  If the return value is negative then the task continues
 * with the negative value in the return register (it can be tested by
 * the caller).
 *
 * Either form may be used in conjunction with "up()".
 */

void __sched
__down_failed(struct semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

#ifdef CONFIG_DEBUG_SEMAPHORE
	printk("%s(%d): down failed(%p)\n",
	       tsk->comm, task_pid_nr(tsk), sem);
#endif

	tsk->state = TASK_UNINTERRUPTIBLE;
	wmb();
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
	tsk->state = TASK_RUNNING;

	/*
	 * If there are any more sleepers, wake one of them up so
	 * that it can either get the semaphore, or set count to -1
	 * indicating that there are still processes sleeping.
	 */
	wake_up(&sem->wait);

#ifdef CONFIG_DEBUG_SEMAPHORE
	printk("%s(%d): down acquired(%p)\n",
	       tsk->comm, task_pid_nr(tsk), sem);
#endif
}

int __sched
__down_failed_interruptible(struct semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);
	long ret = 0;

#ifdef CONFIG_DEBUG_SEMAPHORE
	printk("%s(%d): down failed(%p)\n",
	       tsk->comm, task_pid_nr(tsk), sem);
#endif

	tsk->state = TASK_INTERRUPTIBLE;
	wmb();
	add_wait_queue_exclusive(&sem->wait, &wait);

	while (__sem_update_count(sem, -1) <= 0) {
		if (signal_pending(current)) {
			/*
			 * A signal is pending - give up trying.
			 * Set sem->count to 0 if it is negative,
			 * since we are no longer sleeping.
			 */
			__sem_update_count(sem, 0);
			ret = -EINTR;
			break;
		}
		schedule();
		set_task_state(tsk, TASK_INTERRUPTIBLE);
	}

	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;
	wake_up(&sem->wait);

#ifdef CONFIG_DEBUG_SEMAPHORE
	printk("%s(%d): down %s(%p)\n",
	       current->comm, task_pid_nr(current),
	       (ret < 0 ? "interrupted" : "acquired"), sem);
#endif
	return ret;
}

void
__up_wakeup(struct semaphore *sem)
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

void __sched
down(struct semaphore *sem)
{
#ifdef WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
#ifdef CONFIG_DEBUG_SEMAPHORE
	printk("%s(%d): down(%p) <count=%d> from %p\n",
	       current->comm, task_pid_nr(current), sem,
	       atomic_read(&sem->count), __builtin_return_address(0));
#endif
	__down(sem);
}

int __sched
down_interruptible(struct semaphore *sem)
{
#ifdef WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
#ifdef CONFIG_DEBUG_SEMAPHORE
	printk("%s(%d): down(%p) <count=%d> from %p\n",
	       current->comm, task_pid_nr(current), sem,
	       atomic_read(&sem->count), __builtin_return_address(0));
#endif
	return __down_interruptible(sem);
}

int
down_trylock(struct semaphore *sem)
{
	int ret;

#ifdef WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	ret = __down_trylock(sem);

#ifdef CONFIG_DEBUG_SEMAPHORE
	printk("%s(%d): down_trylock %s from %p\n",
	       current->comm, task_pid_nr(current),
	       ret ? "failed" : "acquired",
	       __builtin_return_address(0));
#endif

	return ret;
}

void
up(struct semaphore *sem)
{
#ifdef WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
#ifdef CONFIG_DEBUG_SEMAPHORE
	printk("%s(%d): up(%p) <count=%d> from %p\n",
	       current->comm, task_pid_nr(current), sem,
	       atomic_read(&sem->count), __builtin_return_address(0));
#endif
	__up(sem);
}
