/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SPINLOCK_LOCK1_H
#define __ASM_SPINLOCK_LOCK1_H

#include <asm/bug.h>
#include <asm/global_lock.h>

static inline int arch_spin_is_locked(arch_spinlock_t *lock)
{
	int ret;

	barrier();
	ret = lock->lock;
	WARN_ON(ret != 0 && ret != 1);
	return ret;
}

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	unsigned int we_won = 0;
	unsigned long flags;

again:
	__global_lock1(flags);
	if (lock->lock == 0) {
		fence();
		lock->lock = 1;
		we_won = 1;
	}
	__global_unlock1(flags);
	if (we_won == 0)
		goto again;
	WARN_ON(lock->lock != 1);
}

/* Returns 0 if failed to acquire lock */
static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	unsigned long flags;
	unsigned int ret;

	__global_lock1(flags);
	ret = lock->lock;
	if (ret == 0) {
		fence();
		lock->lock = 1;
	}
	__global_unlock1(flags);
	return (ret == 0);
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	barrier();
	WARN_ON(!lock->lock);
	lock->lock = 0;
}

/*
 * RWLOCKS
 *
 *
 * Write locks are easy - we just set bit 31.  When unlocking, we can
 * just write zero since the lock is exclusively held.
 */

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	unsigned long flags;
	unsigned int we_won = 0;

again:
	__global_lock1(flags);
	if (rw->lock == 0) {
		fence();
		rw->lock = 0x80000000;
		we_won = 1;
	}
	__global_unlock1(flags);
	if (we_won == 0)
		goto again;
	WARN_ON(rw->lock != 0x80000000);
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	unsigned long flags;
	unsigned int ret;

	__global_lock1(flags);
	ret = rw->lock;
	if (ret == 0) {
		fence();
		rw->lock = 0x80000000;
	}
	__global_unlock1(flags);

	return (ret == 0);
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	barrier();
	WARN_ON(rw->lock != 0x80000000);
	rw->lock = 0;
}

/*
 * Read locks are a bit more hairy:
 *  - Exclusively load the lock value.
 *  - Increment it.
 *  - Store new lock value if positive, and we still own this location.
 *    If the value is negative, we've already failed.
 *  - If we failed to store the value, we want a negative result.
 *  - If we failed, try again.
 * Unlocking is similarly hairy.  We may have multiple read locks
 * currently active.  However, we know we won't have any write
 * locks.
 */
static inline void arch_read_lock(arch_rwlock_t *rw)
{
	unsigned long flags;
	unsigned int we_won = 0, ret;

again:
	__global_lock1(flags);
	ret = rw->lock;
	if (ret < 0x80000000) {
		fence();
		rw->lock = ret + 1;
		we_won = 1;
	}
	__global_unlock1(flags);
	if (!we_won)
		goto again;
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	unsigned long flags;
	unsigned int ret;

	__global_lock1(flags);
	fence();
	ret = rw->lock--;
	__global_unlock1(flags);
	WARN_ON(ret == 0);
}

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	unsigned long flags;
	unsigned int ret;

	__global_lock1(flags);
	ret = rw->lock;
	if (ret < 0x80000000) {
		fence();
		rw->lock = ret + 1;
	}
	__global_unlock1(flags);
	return (ret < 0x80000000);
}

#endif /* __ASM_SPINLOCK_LOCK1_H */
