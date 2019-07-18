// SPDX-License-Identifier: GPL-2.0
/*
 * Ldisc rw semaphore
 *
 * The ldisc semaphore is semantically a rw_semaphore but which enforces
 * an alternate policy, namely:
 *   1) Supports lock wait timeouts
 *   2) Write waiter has priority
 *   3) Downgrading is not supported
 *
 * Implementation notes:
 *   1) Upper half of semaphore count is a wait count (differs from rwsem
 *	in that rwsem normalizes the upper half to the wait bias)
 *   2) Lacks overflow checking
 *
 * The generic counting was copied and modified from include/asm-generic/rwsem.h
 * by Paul Mackerras <paulus@samba.org>.
 *
 * The scheduling policy was copied and modified from lib/rwsem.c
 * Written by David Howells (dhowells@redhat.com).
 *
 * This implementation incorporates the write lock stealing work of
 * Michel Lespinasse <walken@google.com>.
 *
 * Copyright (C) 2013 Peter Hurley <peter@hurleysoftware.com>
 */

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>


#if BITS_PER_LONG == 64
# define LDSEM_ACTIVE_MASK	0xffffffffL
#else
# define LDSEM_ACTIVE_MASK	0x0000ffffL
#endif

#define LDSEM_UNLOCKED		0L
#define LDSEM_ACTIVE_BIAS	1L
#define LDSEM_WAIT_BIAS		(-LDSEM_ACTIVE_MASK-1)
#define LDSEM_READ_BIAS		LDSEM_ACTIVE_BIAS
#define LDSEM_WRITE_BIAS	(LDSEM_WAIT_BIAS + LDSEM_ACTIVE_BIAS)

struct ldsem_waiter {
	struct list_head list;
	struct task_struct *task;
};

/*
 * Initialize an ldsem:
 */
void __init_ldsem(struct ld_semaphore *sem, const char *name,
		  struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/*
	 * Make sure we are not reinitializing a held semaphore:
	 */
	debug_check_no_locks_freed((void *)sem, sizeof(*sem));
	lockdep_init_map(&sem->dep_map, name, key, 0);
#endif
	atomic_long_set(&sem->count, LDSEM_UNLOCKED);
	sem->wait_readers = 0;
	raw_spin_lock_init(&sem->wait_lock);
	INIT_LIST_HEAD(&sem->read_wait);
	INIT_LIST_HEAD(&sem->write_wait);
}

static void __ldsem_wake_readers(struct ld_semaphore *sem)
{
	struct ldsem_waiter *waiter, *next;
	struct task_struct *tsk;
	long adjust, count;

	/*
	 * Try to grant read locks to all readers on the read wait list.
	 * Note the 'active part' of the count is incremented by
	 * the number of readers before waking any processes up.
	 */
	adjust = sem->wait_readers * (LDSEM_ACTIVE_BIAS - LDSEM_WAIT_BIAS);
	count = atomic_long_add_return(adjust, &sem->count);
	do {
		if (count > 0)
			break;
		if (atomic_long_try_cmpxchg(&sem->count, &count, count - adjust))
			return;
	} while (1);

	list_for_each_entry_safe(waiter, next, &sem->read_wait, list) {
		tsk = waiter->task;
		smp_store_release(&waiter->task, NULL);
		wake_up_process(tsk);
		put_task_struct(tsk);
	}
	INIT_LIST_HEAD(&sem->read_wait);
	sem->wait_readers = 0;
}

static inline int writer_trylock(struct ld_semaphore *sem)
{
	/*
	 * Only wake this writer if the active part of the count can be
	 * transitioned from 0 -> 1
	 */
	long count = atomic_long_add_return(LDSEM_ACTIVE_BIAS, &sem->count);
	do {
		if ((count & LDSEM_ACTIVE_MASK) == LDSEM_ACTIVE_BIAS)
			return 1;
		if (atomic_long_try_cmpxchg(&sem->count, &count, count - LDSEM_ACTIVE_BIAS))
			return 0;
	} while (1);
}

static void __ldsem_wake_writer(struct ld_semaphore *sem)
{
	struct ldsem_waiter *waiter;

	waiter = list_entry(sem->write_wait.next, struct ldsem_waiter, list);
	wake_up_process(waiter->task);
}

/*
 * handle the lock release when processes blocked on it that can now run
 * - if we come here from up_xxxx(), then:
 *   - the 'active part' of count (&0x0000ffff) reached 0 (but may have changed)
 *   - the 'waiting part' of count (&0xffff0000) is -ve (and will still be so)
 * - the spinlock must be held by the caller
 * - woken process blocks are discarded from the list after having task zeroed
 */
static void __ldsem_wake(struct ld_semaphore *sem)
{
	if (!list_empty(&sem->write_wait))
		__ldsem_wake_writer(sem);
	else if (!list_empty(&sem->read_wait))
		__ldsem_wake_readers(sem);
}

static void ldsem_wake(struct ld_semaphore *sem)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&sem->wait_lock, flags);
	__ldsem_wake(sem);
	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);
}

/*
 * wait for the read lock to be granted
 */
static struct ld_semaphore __sched *
down_read_failed(struct ld_semaphore *sem, long count, long timeout)
{
	struct ldsem_waiter waiter;
	long adjust = -LDSEM_ACTIVE_BIAS + LDSEM_WAIT_BIAS;

	/* set up my own style of waitqueue */
	raw_spin_lock_irq(&sem->wait_lock);

	/*
	 * Try to reverse the lock attempt but if the count has changed
	 * so that reversing fails, check if there are are no waiters,
	 * and early-out if not
	 */
	do {
		if (atomic_long_try_cmpxchg(&sem->count, &count, count + adjust)) {
			count += adjust;
			break;
		}
		if (count > 0) {
			raw_spin_unlock_irq(&sem->wait_lock);
			return sem;
		}
	} while (1);

	list_add_tail(&waiter.list, &sem->read_wait);
	sem->wait_readers++;

	waiter.task = current;
	get_task_struct(current);

	/* if there are no active locks, wake the new lock owner(s) */
	if ((count & LDSEM_ACTIVE_MASK) == 0)
		__ldsem_wake(sem);

	raw_spin_unlock_irq(&sem->wait_lock);

	/* wait to be given the lock */
	for (;;) {
		set_current_state(TASK_UNINTERRUPTIBLE);

		if (!smp_load_acquire(&waiter.task))
			break;
		if (!timeout)
			break;
		timeout = schedule_timeout(timeout);
	}

	__set_current_state(TASK_RUNNING);

	if (!timeout) {
		/*
		 * Lock timed out but check if this task was just
		 * granted lock ownership - if so, pretend there
		 * was no timeout; otherwise, cleanup lock wait.
		 */
		raw_spin_lock_irq(&sem->wait_lock);
		if (waiter.task) {
			atomic_long_add_return(-LDSEM_WAIT_BIAS, &sem->count);
			sem->wait_readers--;
			list_del(&waiter.list);
			raw_spin_unlock_irq(&sem->wait_lock);
			put_task_struct(waiter.task);
			return NULL;
		}
		raw_spin_unlock_irq(&sem->wait_lock);
	}

	return sem;
}

/*
 * wait for the write lock to be granted
 */
static struct ld_semaphore __sched *
down_write_failed(struct ld_semaphore *sem, long count, long timeout)
{
	struct ldsem_waiter waiter;
	long adjust = -LDSEM_ACTIVE_BIAS;
	int locked = 0;

	/* set up my own style of waitqueue */
	raw_spin_lock_irq(&sem->wait_lock);

	/*
	 * Try to reverse the lock attempt but if the count has changed
	 * so that reversing fails, check if the lock is now owned,
	 * and early-out if so.
	 */
	do {
		if (atomic_long_try_cmpxchg(&sem->count, &count, count + adjust))
			break;
		if ((count & LDSEM_ACTIVE_MASK) == LDSEM_ACTIVE_BIAS) {
			raw_spin_unlock_irq(&sem->wait_lock);
			return sem;
		}
	} while (1);

	list_add_tail(&waiter.list, &sem->write_wait);

	waiter.task = current;

	set_current_state(TASK_UNINTERRUPTIBLE);
	for (;;) {
		if (!timeout)
			break;
		raw_spin_unlock_irq(&sem->wait_lock);
		timeout = schedule_timeout(timeout);
		raw_spin_lock_irq(&sem->wait_lock);
		set_current_state(TASK_UNINTERRUPTIBLE);
		locked = writer_trylock(sem);
		if (locked)
			break;
	}

	if (!locked)
		atomic_long_add_return(-LDSEM_WAIT_BIAS, &sem->count);
	list_del(&waiter.list);

	/*
	 * In case of timeout, wake up every reader who gave the right of way
	 * to writer. Prevent separation readers into two groups:
	 * one that helds semaphore and another that sleeps.
	 * (in case of no contention with a writer)
	 */
	if (!locked && list_empty(&sem->write_wait))
		__ldsem_wake_readers(sem);

	raw_spin_unlock_irq(&sem->wait_lock);

	__set_current_state(TASK_RUNNING);

	/* lock wait may have timed out */
	if (!locked)
		return NULL;
	return sem;
}



static int __ldsem_down_read_nested(struct ld_semaphore *sem,
					   int subclass, long timeout)
{
	long count;

	rwsem_acquire_read(&sem->dep_map, subclass, 0, _RET_IP_);

	count = atomic_long_add_return(LDSEM_READ_BIAS, &sem->count);
	if (count <= 0) {
		lock_contended(&sem->dep_map, _RET_IP_);
		if (!down_read_failed(sem, count, timeout)) {
			rwsem_release(&sem->dep_map, 1, _RET_IP_);
			return 0;
		}
	}
	lock_acquired(&sem->dep_map, _RET_IP_);
	return 1;
}

static int __ldsem_down_write_nested(struct ld_semaphore *sem,
					    int subclass, long timeout)
{
	long count;

	rwsem_acquire(&sem->dep_map, subclass, 0, _RET_IP_);

	count = atomic_long_add_return(LDSEM_WRITE_BIAS, &sem->count);
	if ((count & LDSEM_ACTIVE_MASK) != LDSEM_ACTIVE_BIAS) {
		lock_contended(&sem->dep_map, _RET_IP_);
		if (!down_write_failed(sem, count, timeout)) {
			rwsem_release(&sem->dep_map, 1, _RET_IP_);
			return 0;
		}
	}
	lock_acquired(&sem->dep_map, _RET_IP_);
	return 1;
}


/*
 * lock for reading -- returns 1 if successful, 0 if timed out
 */
int __sched ldsem_down_read(struct ld_semaphore *sem, long timeout)
{
	might_sleep();
	return __ldsem_down_read_nested(sem, 0, timeout);
}

/*
 * trylock for reading -- returns 1 if successful, 0 if contention
 */
int ldsem_down_read_trylock(struct ld_semaphore *sem)
{
	long count = atomic_long_read(&sem->count);

	while (count >= 0) {
		if (atomic_long_try_cmpxchg(&sem->count, &count, count + LDSEM_READ_BIAS)) {
			rwsem_acquire_read(&sem->dep_map, 0, 1, _RET_IP_);
			lock_acquired(&sem->dep_map, _RET_IP_);
			return 1;
		}
	}
	return 0;
}

/*
 * lock for writing -- returns 1 if successful, 0 if timed out
 */
int __sched ldsem_down_write(struct ld_semaphore *sem, long timeout)
{
	might_sleep();
	return __ldsem_down_write_nested(sem, 0, timeout);
}

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
int ldsem_down_write_trylock(struct ld_semaphore *sem)
{
	long count = atomic_long_read(&sem->count);

	while ((count & LDSEM_ACTIVE_MASK) == 0) {
		if (atomic_long_try_cmpxchg(&sem->count, &count, count + LDSEM_WRITE_BIAS)) {
			rwsem_acquire(&sem->dep_map, 0, 1, _RET_IP_);
			lock_acquired(&sem->dep_map, _RET_IP_);
			return 1;
		}
	}
	return 0;
}

/*
 * release a read lock
 */
void ldsem_up_read(struct ld_semaphore *sem)
{
	long count;

	rwsem_release(&sem->dep_map, 1, _RET_IP_);

	count = atomic_long_add_return(-LDSEM_READ_BIAS, &sem->count);
	if (count < 0 && (count & LDSEM_ACTIVE_MASK) == 0)
		ldsem_wake(sem);
}

/*
 * release a write lock
 */
void ldsem_up_write(struct ld_semaphore *sem)
{
	long count;

	rwsem_release(&sem->dep_map, 1, _RET_IP_);

	count = atomic_long_add_return(-LDSEM_WRITE_BIAS, &sem->count);
	if (count < 0)
		ldsem_wake(sem);
}


#ifdef CONFIG_DEBUG_LOCK_ALLOC

int ldsem_down_read_nested(struct ld_semaphore *sem, int subclass, long timeout)
{
	might_sleep();
	return __ldsem_down_read_nested(sem, subclass, timeout);
}

int ldsem_down_write_nested(struct ld_semaphore *sem, int subclass,
			    long timeout)
{
	might_sleep();
	return __ldsem_down_write_nested(sem, subclass, timeout);
}

#endif
