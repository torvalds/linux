/*
 * Copyright (C) 2003 Jerome Marchand, Bull S.A.
 *	Cleaned up by David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * This file is released under the GPLv2, or at your option any later version.
 *
 * ia64 version of "atomic_dec_and_lock()" using the atomic "cmpxchg" instruction.  This
 * code is an adaptation of the x86 version of "atomic_dec_and_lock()".
 */

#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

/*
 * Decrement REFCOUNT and if the count reaches zero, acquire the spinlock.  Both of these
 * operations have to be done atomically, so that the count doesn't drop to zero without
 * acquiring the spinlock first.
 */
int
_atomic_dec_and_lock (atomic_t *refcount, spinlock_t *lock)
{
	int old, new;

	do {
		old = atomic_read(refcount);
		new = old - 1;

		if (unlikely (old == 1)) {
			/* oops, we may be decrementing to zero, do it the slow way... */
			spin_lock(lock);
			if (atomic_dec_and_test(refcount))
				return 1;
			spin_unlock(lock);
			return 0;
		}
	} while (cmpxchg(&refcount->counter, old, new) != old);
	return 0;
}

EXPORT_SYMBOL(_atomic_dec_and_lock);
