/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_QSPINLOCK_H
#define _ASM_POWERPC_QSPINLOCK_H

#include <linux/compiler.h>
#include <asm/qspinlock_types.h>
#include <asm/paravirt.h>

#ifdef CONFIG_PPC64
/*
 * Use the EH=1 hint for accesses that result in the lock being acquired.
 * The hardware is supposed to optimise this pattern by holding the lock
 * cacheline longer, and releasing when a store to the same memory (the
 * unlock) is performed.
 */
#define _Q_SPIN_EH_HINT 1
#else
#define _Q_SPIN_EH_HINT 0
#endif

/*
 * The trylock itself may steal. This makes trylocks slightly stronger, and
 * makes locks slightly more efficient when stealing.
 *
 * This is compile-time, so if true then there may always be stealers, so the
 * nosteal paths become unused.
 */
#define _Q_SPIN_TRY_LOCK_STEAL 1

/*
 * Put a speculation barrier after testing the lock/node and finding it
 * busy. Try to prevent pointless speculation in slow paths.
 *
 * Slows down the lockstorm microbenchmark with no stealing, where locking
 * is purely FIFO through the queue. May have more benefit in real workload
 * where speculating into the wrong place could have a greater cost.
 */
#define _Q_SPIN_SPEC_BARRIER 0

#ifdef CONFIG_PPC64
/*
 * Execute a miso instruction after passing the MCS lock ownership to the
 * queue head. Miso is intended to make stores visible to other CPUs sooner.
 *
 * This seems to make the lockstorm microbenchmark nospin test go slightly
 * faster on POWER10, but disable for now.
 */
#define _Q_SPIN_MISO 0
#else
#define _Q_SPIN_MISO 0
#endif

#ifdef CONFIG_PPC64
/*
 * This executes miso after an unlock of the lock word, having ownership
 * pass to the next CPU sooner. This will slow the uncontended path to some
 * degree. Not evidence it helps yet.
 */
#define _Q_SPIN_MISO_UNLOCK 0
#else
#define _Q_SPIN_MISO_UNLOCK 0
#endif

/*
 * Seems to slow down lockstorm microbenchmark, suspect queue node just
 * has to become shared again right afterwards when its waiter spins on
 * the lock field.
 */
#define _Q_SPIN_PREFETCH_NEXT 0

static __always_inline int queued_spin_is_locked(struct qspinlock *lock)
{
	return READ_ONCE(lock->val);
}

static __always_inline int queued_spin_value_unlocked(struct qspinlock lock)
{
	return !lock.val;
}

static __always_inline int queued_spin_is_contended(struct qspinlock *lock)
{
	return !!(READ_ONCE(lock->val) & _Q_TAIL_CPU_MASK);
}

static __always_inline u32 queued_spin_encode_locked_val(void)
{
	/* XXX: make this use lock value in paca like simple spinlocks? */
	return _Q_LOCKED_VAL | (smp_processor_id() << _Q_OWNER_CPU_OFFSET);
}

static __always_inline int __queued_spin_trylock_nosteal(struct qspinlock *lock)
{
	u32 new = queued_spin_encode_locked_val();
	u32 prev;

	/* Trylock succeeds only when unlocked and no queued nodes */
	asm volatile(
"1:	lwarx	%0,0,%1,%3	# __queued_spin_trylock_nosteal		\n"
"	cmpwi	0,%0,0							\n"
"	bne-	2f							\n"
"	stwcx.	%2,0,%1							\n"
"	bne-	1b							\n"
"\t"	PPC_ACQUIRE_BARRIER "						\n"
"2:									\n"
	: "=&r" (prev)
	: "r" (&lock->val), "r" (new),
	  "i" (_Q_SPIN_EH_HINT)
	: "cr0", "memory");

	return likely(prev == 0);
}

static __always_inline int __queued_spin_trylock_steal(struct qspinlock *lock)
{
	u32 new = queued_spin_encode_locked_val();
	u32 prev, tmp;

	/* Trylock may get ahead of queued nodes if it finds unlocked */
	asm volatile(
"1:	lwarx	%0,0,%2,%5	# __queued_spin_trylock_steal		\n"
"	andc.	%1,%0,%4						\n"
"	bne-	2f							\n"
"	and	%1,%0,%4						\n"
"	or	%1,%1,%3						\n"
"	stwcx.	%1,0,%2							\n"
"	bne-	1b							\n"
"\t"	PPC_ACQUIRE_BARRIER "						\n"
"2:									\n"
	: "=&r" (prev), "=&r" (tmp)
	: "r" (&lock->val), "r" (new), "r" (_Q_TAIL_CPU_MASK),
	  "i" (_Q_SPIN_EH_HINT)
	: "cr0", "memory");

	return likely(!(prev & ~_Q_TAIL_CPU_MASK));
}

static __always_inline int queued_spin_trylock(struct qspinlock *lock)
{
	if (!_Q_SPIN_TRY_LOCK_STEAL)
		return __queued_spin_trylock_nosteal(lock);
	else
		return __queued_spin_trylock_steal(lock);
}

void queued_spin_lock_slowpath(struct qspinlock *lock);

static __always_inline void queued_spin_lock(struct qspinlock *lock)
{
	if (!queued_spin_trylock(lock))
		queued_spin_lock_slowpath(lock);
}

static inline void queued_spin_unlock(struct qspinlock *lock)
{
	smp_store_release(&lock->locked, 0);
	if (_Q_SPIN_MISO_UNLOCK)
		asm volatile("miso" ::: "memory");
}

#define arch_spin_is_locked(l)		queued_spin_is_locked(l)
#define arch_spin_is_contended(l)	queued_spin_is_contended(l)
#define arch_spin_value_unlocked(l)	queued_spin_value_unlocked(l)
#define arch_spin_lock(l)		queued_spin_lock(l)
#define arch_spin_trylock(l)		queued_spin_trylock(l)
#define arch_spin_unlock(l)		queued_spin_unlock(l)

#ifdef CONFIG_PARAVIRT_SPINLOCKS
void pv_spinlocks_init(void);
#else
static inline void pv_spinlocks_init(void) { }
#endif

#endif /* _ASM_POWERPC_QSPINLOCK_H */
