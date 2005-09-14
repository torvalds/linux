#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <asm/system.h>

/*
 * This is an implementation of the notion of "decrement a
 * reference count, and return locked if it decremented to zero".
 *
 * This implementation can be used on any architecture that
 * has a cmpxchg, and where atomic->value is an int holding
 * the value of the atomic (i.e. the high bits aren't used
 * for a lock or anything like that).
 */
int _atomic_dec_and_lock(atomic_t *atomic, spinlock_t *lock)
{
	int counter;
	int newcount;

	for (;;) {
		counter = atomic_read(atomic);
		newcount = counter - 1;
		if (!newcount)
			break;		/* do it the slow way */

		newcount = cmpxchg(&atomic->counter, counter, newcount);
		if (newcount == counter)
			return 0;
	}

	spin_lock(lock);
	if (atomic_dec_and_test(atomic))
		return 1;
	spin_unlock(lock);
	return 0;
}

EXPORT_SYMBOL(_atomic_dec_and_lock);
