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

#define SPINLOCK_LOCKVAL (S390_lowcore.spinlock_lockval)

extern int spin_retry;

static inline int
_raw_compare_and_swap(unsigned int *lock, unsigned int old, unsigned int new)
{
	unsigned int old_expected = old;

	asm volatile(
		"	cs	%0,%3,%1"
		: "=d" (old), "=Q" (*lock)
		: "0" (old), "d" (new), "Q" (*lock)
		: "cc", "memory" );
	return old == old_expected;
}

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 *
 * (the type definitions are in asm/spinlock_types.h)
 */

void arch_spin_lock_wait(arch_spinlock_t *);
int arch_spin_trylock_retry(arch_spinlock_t *);
void arch_spin_relax(arch_spinlock_t *);
void arch_spin_lock_wait_flags(arch_spinlock_t *, unsigned long flags);

static inline u32 arch_spin_lockval(int cpu)
{
	return ~cpu;
}

static inline int arch_spin_value_unlocked(arch_spinlock_t lock)
{
	return lock.lock == 0;
}

static inline int arch_spin_is_locked(arch_spinlock_t *lp)
{
	return ACCESS_ONCE(lp->lock) != 0;
}

static inline int arch_spin_trylock_once(arch_spinlock_t *lp)
{
	barrier();
	return likely(arch_spin_value_unlocked(*lp) &&
		      _raw_compare_and_swap(&lp->lock, 0, SPINLOCK_LOCKVAL));
}

static inline int arch_spin_tryrelease_once(arch_spinlock_t *lp)
{
	return _raw_compare_and_swap(&lp->lock, SPINLOCK_LOCKVAL, 0);
}

static inline void arch_spin_lock(arch_spinlock_t *lp)
{
	if (!arch_spin_trylock_once(lp))
		arch_spin_lock_wait(lp);
}

static inline void arch_spin_lock_flags(arch_spinlock_t *lp,
					unsigned long flags)
{
	if (!arch_spin_trylock_once(lp))
		arch_spin_lock_wait_flags(lp, flags);
}

static inline int arch_spin_trylock(arch_spinlock_t *lp)
{
	if (!arch_spin_trylock_once(lp))
		return arch_spin_trylock_retry(lp);
	return 1;
}

static inline void arch_spin_unlock(arch_spinlock_t *lp)
{
	arch_spin_tryrelease_once(lp);
}

static inline void arch_spin_unlock_wait(arch_spinlock_t *lock)
{
	while (arch_spin_is_locked(lock))
		arch_spin_relax(lock);
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

static inline int arch_read_trylock_once(arch_rwlock_t *rw)
{
	unsigned int old = ACCESS_ONCE(rw->lock);
	return likely((int) old >= 0 &&
		      _raw_compare_and_swap(&rw->lock, old, old + 1));
}

static inline int arch_write_trylock_once(arch_rwlock_t *rw)
{
	unsigned int old = ACCESS_ONCE(rw->lock);
	return likely(old == 0 &&
		      _raw_compare_and_swap(&rw->lock, 0, 0x80000000));
}

static inline void arch_read_lock(arch_rwlock_t *rw)
{
	if (!arch_read_trylock_once(rw))
		_raw_read_lock_wait(rw);
}

static inline void arch_read_lock_flags(arch_rwlock_t *rw, unsigned long flags)
{
	if (!arch_read_trylock_once(rw))
		_raw_read_lock_wait_flags(rw, flags);
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	unsigned int old;

	do {
		old = ACCESS_ONCE(rw->lock);
	} while (!_raw_compare_and_swap(&rw->lock, old, old - 1));
}

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	if (!arch_write_trylock_once(rw))
		_raw_write_lock_wait(rw);
}

static inline void arch_write_lock_flags(arch_rwlock_t *rw, unsigned long flags)
{
	if (!arch_write_trylock_once(rw))
		_raw_write_lock_wait_flags(rw, flags);
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	_raw_compare_and_swap(&rw->lock, 0x80000000, 0);
}

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	if (!arch_read_trylock_once(rw))
		return _raw_read_trylock_retry(rw);
	return 1;
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	if (!arch_write_trylock_once(rw))
		return _raw_write_trylock_retry(rw);
	return 1;
}

#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif /* __ASM_SPINLOCK_H */
