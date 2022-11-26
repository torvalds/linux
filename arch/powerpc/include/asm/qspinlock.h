/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_QSPINLOCK_H
#define _ASM_POWERPC_QSPINLOCK_H

#include <linux/compiler.h>
#include <asm/qspinlock_types.h>
#include <asm/paravirt.h>

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

static __always_inline int queued_spin_trylock(struct qspinlock *lock)
{
	u32 prev;

	asm volatile(
"1:	lwarx	%0,0,%1,%3	# queued_spin_trylock			\n"
"	cmpwi	0,%0,0							\n"
"	bne-	2f							\n"
"	stwcx.	%2,0,%1							\n"
"	bne-	1b							\n"
"\t"	PPC_ACQUIRE_BARRIER "						\n"
"2:									\n"
	: "=&r" (prev)
	: "r" (&lock->val), "r" (_Q_LOCKED_VAL),
	  "i" (IS_ENABLED(CONFIG_PPC64))
	: "cr0", "memory");

	return likely(prev == 0);
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
