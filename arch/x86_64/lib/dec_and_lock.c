/*
 * x86 version of "atomic_dec_and_lock()" using
 * the atomic "cmpxchg" instruction.
 *
 * (For CPU's lacking cmpxchg, we use the slow
 * generic version, and this one never even gets
 * compiled).
 */

#include <linux/spinlock.h>
#include <asm/atomic.h>

int _atomic_dec_and_lock(atomic_t *atomic, spinlock_t *lock)
{
	int counter;
	int newcount;

repeat:
	counter = atomic_read(atomic);
	newcount = counter-1;

	if (!newcount)
		goto slow_path;

	asm volatile("lock; cmpxchgl %1,%2"
		:"=a" (newcount)
		:"r" (newcount), "m" (atomic->counter), "0" (counter));

	/* If the above failed, "eax" will have changed */
	if (newcount != counter)
		goto repeat;
	return 0;

slow_path:
	spin_lock(lock);
	if (atomic_dec_and_test(atomic))
		return 1;
	spin_unlock(lock);
	return 0;
}
