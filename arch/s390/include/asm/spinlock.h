/* SPDX-License-Identifier: GPL-2.0 */
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
#include <asm/atomic_ops.h>
#include <asm/barrier.h>
#include <asm/processor.h>

#define SPINLOCK_LOCKVAL (S390_lowcore.spinlock_lockval)

extern int spin_retry;

#ifndef CONFIG_SMP
static inline bool arch_vcpu_is_preempted(int cpu) { return false; }
#else
bool arch_vcpu_is_preempted(int cpu);
#endif

#define vcpu_is_preempted arch_vcpu_is_preempted

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 *
 * (the type definitions are in asm/spinlock_types.h)
 */

void arch_lock_relax(int cpu);

void arch_spin_lock_wait(arch_spinlock_t *);
int arch_spin_trylock_retry(arch_spinlock_t *);
void arch_spin_lock_wait_flags(arch_spinlock_t *, unsigned long flags);

static inline void arch_spin_relax(arch_spinlock_t *lock)
{
	arch_lock_relax(lock->lock);
}

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
	return READ_ONCE(lp->lock) != 0;
}

static inline int arch_spin_trylock_once(arch_spinlock_t *lp)
{
	barrier();
	return likely(arch_spin_value_unlocked(*lp) &&
		      __atomic_cmpxchg_bool(&lp->lock, 0, SPINLOCK_LOCKVAL));
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
	typecheck(int, lp->lock);
	asm volatile(
#ifdef CONFIG_HAVE_MARCH_ZEC12_FEATURES
		"	.long	0xb2fa0070\n"	/* NIAI 7 */
#endif
		"	st	%1,%0\n"
		: "=Q" (lp->lock) : "d" (0) : "cc", "memory");
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

extern int _raw_read_trylock_retry(arch_rwlock_t *lp);
extern int _raw_write_trylock_retry(arch_rwlock_t *lp);

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

static inline int arch_read_trylock_once(arch_rwlock_t *rw)
{
	int old = ACCESS_ONCE(rw->lock);
	return likely(old >= 0 &&
		      __atomic_cmpxchg_bool(&rw->lock, old, old + 1));
}

static inline int arch_write_trylock_once(arch_rwlock_t *rw)
{
	int old = ACCESS_ONCE(rw->lock);
	return likely(old == 0 &&
		      __atomic_cmpxchg_bool(&rw->lock, 0, 0x80000000));
}

#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES

#define __RAW_OP_OR	"lao"
#define __RAW_OP_AND	"lan"
#define __RAW_OP_ADD	"laa"

#define __RAW_LOCK(ptr, op_val, op_string)		\
({							\
	int old_val;					\
							\
	typecheck(int *, ptr);				\
	asm volatile(					\
		op_string "	%0,%2,%1\n"		\
		"bcr	14,0\n"				\
		: "=d" (old_val), "+Q" (*ptr)		\
		: "d" (op_val)				\
		: "cc", "memory");			\
	old_val;					\
})

#define __RAW_UNLOCK(ptr, op_val, op_string)		\
({							\
	int old_val;					\
							\
	typecheck(int *, ptr);				\
	asm volatile(					\
		op_string "	%0,%2,%1\n"		\
		: "=d" (old_val), "+Q" (*ptr)		\
		: "d" (op_val)				\
		: "cc", "memory");			\
	old_val;					\
})

extern void _raw_read_lock_wait(arch_rwlock_t *lp);
extern void _raw_write_lock_wait(arch_rwlock_t *lp, int prev);

static inline void arch_read_lock(arch_rwlock_t *rw)
{
	int old;

	old = __RAW_LOCK(&rw->lock, 1, __RAW_OP_ADD);
	if (old < 0)
		_raw_read_lock_wait(rw);
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	__RAW_UNLOCK(&rw->lock, -1, __RAW_OP_ADD);
}

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	int old;

	old = __RAW_LOCK(&rw->lock, 0x80000000, __RAW_OP_OR);
	if (old != 0)
		_raw_write_lock_wait(rw, old);
	rw->owner = SPINLOCK_LOCKVAL;
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	rw->owner = 0;
	__RAW_UNLOCK(&rw->lock, 0x7fffffff, __RAW_OP_AND);
}

#else /* CONFIG_HAVE_MARCH_Z196_FEATURES */

extern void _raw_read_lock_wait(arch_rwlock_t *lp);
extern void _raw_write_lock_wait(arch_rwlock_t *lp);

static inline void arch_read_lock(arch_rwlock_t *rw)
{
	if (!arch_read_trylock_once(rw))
		_raw_read_lock_wait(rw);
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	int old;

	do {
		old = ACCESS_ONCE(rw->lock);
	} while (!__atomic_cmpxchg_bool(&rw->lock, old, old - 1));
}

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	if (!arch_write_trylock_once(rw))
		_raw_write_lock_wait(rw);
	rw->owner = SPINLOCK_LOCKVAL;
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	typecheck(int, rw->lock);

	rw->owner = 0;
	asm volatile(
		"st	%1,%0\n"
		: "+Q" (rw->lock)
		: "d" (0)
		: "cc", "memory");
}

#endif /* CONFIG_HAVE_MARCH_Z196_FEATURES */

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	if (!arch_read_trylock_once(rw))
		return _raw_read_trylock_retry(rw);
	return 1;
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	if (!arch_write_trylock_once(rw) && !_raw_write_trylock_retry(rw))
		return 0;
	rw->owner = SPINLOCK_LOCKVAL;
	return 1;
}

static inline void arch_read_relax(arch_rwlock_t *rw)
{
	arch_lock_relax(rw->owner);
}

static inline void arch_write_relax(arch_rwlock_t *rw)
{
	arch_lock_relax(rw->owner);
}

#endif /* __ASM_SPINLOCK_H */
