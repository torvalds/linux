/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/spinlock.h>
#include <linux/module.h>
#include <asm/processor.h>

#include "spinlock_common.h"

/*
 * Read the spinlock value without allocating in our cache and without
 * causing an invalidation to another cpu with a copy of the cacheline.
 * This is important when we are spinning waiting for the lock.
 */
static inline u32 arch_spin_read_noalloc(void *lock)
{
	return atomic_cmpxchg((atomic_t *)lock, -1, -1);
}

/*
 * Wait until the high bits (current) match my ticket.
 * If we notice the overflow bit set on entry, we clear it.
 */
void arch_spin_lock_slow(arch_spinlock_t *lock, u32 my_ticket)
{
	if (unlikely(my_ticket & __ARCH_SPIN_NEXT_OVERFLOW)) {
		__insn_fetchand4(&lock->lock, ~__ARCH_SPIN_NEXT_OVERFLOW);
		my_ticket &= ~__ARCH_SPIN_NEXT_OVERFLOW;
	}

	for (;;) {
		u32 val = arch_spin_read_noalloc(lock);
		u32 delta = my_ticket - arch_spin_current(val);
		if (delta == 0)
			return;
		relax((128 / CYCLES_PER_RELAX_LOOP) * delta);
	}
}
EXPORT_SYMBOL(arch_spin_lock_slow);

/*
 * Check the lock to see if it is plausible, and try to get it with cmpxchg().
 */
int arch_spin_trylock(arch_spinlock_t *lock)
{
	u32 val = arch_spin_read_noalloc(lock);
	if (unlikely(arch_spin_current(val) != arch_spin_next(val)))
		return 0;
	return cmpxchg(&lock->lock, val, (val + 1) & ~__ARCH_SPIN_NEXT_OVERFLOW)
		== val;
}
EXPORT_SYMBOL(arch_spin_trylock);

void arch_spin_unlock_wait(arch_spinlock_t *lock)
{
	u32 iterations = 0;
	u32 val = READ_ONCE(lock->lock);
	u32 curr = arch_spin_current(val);

	/* Return immediately if unlocked. */
	if (arch_spin_next(val) == curr)
		return;

	/* Wait until the current locker has released the lock. */
	do {
		delay_backoff(iterations++);
	} while (arch_spin_current(READ_ONCE(lock->lock)) == curr);
}
EXPORT_SYMBOL(arch_spin_unlock_wait);

/*
 * If the read lock fails due to a writer, we retry periodically
 * until the value is positive and we write our incremented reader count.
 */
void __read_lock_failed(arch_rwlock_t *rw)
{
	u32 val;
	int iterations = 0;
	do {
		delay_backoff(iterations++);
		val = __insn_fetchaddgez4(&rw->lock, 1);
	} while (unlikely(arch_write_val_locked(val)));
}
EXPORT_SYMBOL(__read_lock_failed);

/*
 * If we failed because there were readers, clear the "writer" bit
 * so we don't block additional readers.  Otherwise, there was another
 * writer anyway, so our "fetchor" made no difference.  Then wait,
 * issuing periodic fetchor instructions, till we get the lock.
 */
void __write_lock_failed(arch_rwlock_t *rw, u32 val)
{
	int iterations = 0;
	do {
		if (!arch_write_val_locked(val))
			val = __insn_fetchand4(&rw->lock, ~__WRITE_LOCK_BIT);
		delay_backoff(iterations++);
		val = __insn_fetchor4(&rw->lock, __WRITE_LOCK_BIT);
	} while (val != 0);
}
EXPORT_SYMBOL(__write_lock_failed);
