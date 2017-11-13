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
#include <asm/alternative.h>

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

void arch_spin_relax(arch_spinlock_t *lock);

void arch_spin_lock_wait(arch_spinlock_t *);
int arch_spin_trylock_retry(arch_spinlock_t *);
void arch_spin_lock_setup(int cpu);

static inline u32 arch_spin_lockval(int cpu)
{
	return cpu + 1;
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
	return likely(__atomic_cmpxchg_bool(&lp->lock, 0, SPINLOCK_LOCKVAL));
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
		arch_spin_lock_wait(lp);
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
		ALTERNATIVE("", ".long 0xb2fa0070", 49)	/* NIAI 7 */
		"	sth	%1,%0\n"
		: "=Q" (((unsigned short *) &lp->lock)[1])
		: "d" (0) : "cc", "memory");
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
#define arch_read_can_lock(x) (((x)->cnts & 0xffff0000) == 0)

/**
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define arch_write_can_lock(x) ((x)->cnts == 0)

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)
#define arch_read_relax(rw) barrier()
#define arch_write_relax(rw) barrier()

void arch_read_lock_wait(arch_rwlock_t *lp);
void arch_write_lock_wait(arch_rwlock_t *lp);

static inline void arch_read_lock(arch_rwlock_t *rw)
{
	int old;

	old = __atomic_add(1, &rw->cnts);
	if (old & 0xffff0000)
		arch_read_lock_wait(rw);
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	__atomic_add_const_barrier(-1, &rw->cnts);
}

static inline void arch_write_lock(arch_rwlock_t *rw)
{
	if (!__atomic_cmpxchg_bool(&rw->cnts, 0, 0x30000))
		arch_write_lock_wait(rw);
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	__atomic_add_barrier(-0x30000, &rw->cnts);
}


static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	int old;

	old = READ_ONCE(rw->cnts);
	return (!(old & 0xffff0000) &&
		__atomic_cmpxchg_bool(&rw->cnts, old, old + 1));
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	int old;

	old = READ_ONCE(rw->cnts);
	return !old && __atomic_cmpxchg_bool(&rw->cnts, 0, 0x30000);
}

#endif /* __ASM_SPINLOCK_H */
