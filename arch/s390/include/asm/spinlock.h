/*
 *  S390 version
 *    Copyright IBM Corp. 1999
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/spinlock.h"
 */

#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <linux/smp.h>

extern int spin_retry;

static inline int
_raw_compare_and_swap(volatile unsigned int *lock,
		      unsigned int old, unsigned int new)
{
	asm volatile(
		"	cs	%0,%3,%1"
		: "=d" (old), "=Q" (*lock)
		: "0" (old), "d" (new), "Q" (*lock)
		: "cc", "memory" );
	return old;
}

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 *
 * (the type definitions are in asm/spinlock_types.h)
 */

#define arch_spin_is_locked(x) ((x)->owner_cpu != 0)
#define arch_spin_unlock_wait(lock) \
	do { while (arch_spin_is_locked(lock)) \
		 arch_spin_relax(lock); } while (0)

extern void arch_spin_lock_wait(arch_spinlock_t *);
extern void arch_spin_lock_wait_flags(arch_spinlock_t *, unsigned long flags);
extern int arch_spin_trylock_retry(arch_spinlock_t *);
extern void arch_spin_relax(arch_spinlock_t *lock);

static inline int arch_spin_value_unlocked(arch_spinlock_t lock)
{
	return lock.owner_cpu == 0;
}

static inline void arch_spin_lock(arch_spinlock_t *lp)
{
	int old;

	old = _raw_compare_and_swap(&lp->owner_cpu, 0, ~smp_processor_id());
	if (likely(old == 0))
		return;
	arch_spin_lock_wait(lp);
}

static inline void arch_spin_lock_flags(arch_spinlock_t *lp,
					 unsigned long flags)
{
	int old;

	old = _raw_compare_and_swap(&lp->owner_cpu, 0, ~smp_processor_id());
	if (likely(old == 0))
		return;
	arch_spin_lock_wait_flags(lp, flags);
}

static inline int arch_spin_trylock(arch_spinlock_t *lp)
{
	int old;

	old = _raw_compare_and_swap(&lp->owner_cpu, 0, ~smp_processor_id());
	if (likely(old == 0))
		return 1;
	return arch_spin_trylock_retry(lp);
}

static inline void arch_spin_unlock(arch_spinlock_t *lp)
{
	_raw_compare_and_swap(&lp->owner_cpu, lp->owner_cpu, 0);
}
		
/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts
 * but no interrupt writers. For those circumstances we
 * can "mix" irq-safe locks - any writer needs to get a
 * irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 */

/**
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define arch_read_can_lock(x) ((int)(x)->lock >= 0)

/**
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define arch_write_can_lock(x) ((x)->lock == 0)

extern void _raw_read_lock_wait(arch_rwlock_t *lp);
extern void _raw_read_lock_wait_flags(arch_rwlock_t *lp, unsigned long flags);
extern int _raw_read_trylock_retry(arch_rwlock_t *lp);
extern void _raw_write_lock_wait(arch_rwlock_t *lp);
extern void _raw_write_lock_wait_flags(arch_rwlock_t *lp, unsigned long flags);
extern int _raw_write_trylock_retry(arch_rwlock_t *lp);

static inline void arch_read_lock(arch_rwlock_t *rw)
{
	unsigned int old;
	old = rw->lock & 0x7fffffffU;
	if (_raw_compare_and_swap(&rw->lock, old, old + 1) != old)
		_raw_read_lock_wait(rw);
}

static inline void arch_read_lock_flags(arch_rwlock_t *rw, unsigned long flags)
{
	unsigned int old;
	old = rw->lock & 0x7fffffffU;
	if (_raw_compare_and_swap(&rw->lock, old, old + 1) != old)
		_raw_read_lock_wait_flags(rw, flags);
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	unsigned int old, cmp;

	old = rw->lock;
	do {
		cmp = old;
		old = _raw_compare_and_swap(&rw->lock, old, old - 1);
	} while (cmp != old);
}

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	if (unlikely(_raw_compare_and_swap(&rw->lock, 0, 0x80000000) != 0))
		_raw_write_lock_wait(rw);
}

static inline void arch_write_lock_flags(arch_rwlock_t *rw, unsigned long flags)
{
	if (unlikely(_raw_compare_and_swap(&rw->lock, 0, 0x80000000) != 0))
		_raw_write_lock_wait_flags(rw, flags);
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	_raw_compare_and_swap(&rw->lock, 0x80000000, 0);
}

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	unsigned int old;
	old = rw->lock & 0x7fffffffU;
	if (likely(_raw_compare_and_swap(&rw->lock, old, old + 1) == old))
		return 1;
	return _raw_read_trylock_retry(rw);
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	if (likely(_raw_compare_and_swap(&rw->lock, 0, 0x80000000) == 0))
		return 1;
	return _raw_write_trylock_retry(rw);
}

#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif /* __ASM_SPINLOCK_H */
