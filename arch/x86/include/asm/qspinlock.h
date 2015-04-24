#ifndef _ASM_X86_QSPINLOCK_H
#define _ASM_X86_QSPINLOCK_H

#include <asm/cpufeature.h>
#include <asm-generic/qspinlock_types.h>

#define	queued_spin_unlock queued_spin_unlock
/**
 * queued_spin_unlock - release a queued spinlock
 * @lock : Pointer to queued spinlock structure
 *
 * A smp_store_release() on the least-significant byte.
 */
static inline void queued_spin_unlock(struct qspinlock *lock)
{
	smp_store_release((u8 *)lock, 0);
}

#define virt_queued_spin_lock virt_queued_spin_lock

static inline bool virt_queued_spin_lock(struct qspinlock *lock)
{
	if (!static_cpu_has(X86_FEATURE_HYPERVISOR))
		return false;

	while (atomic_cmpxchg(&lock->val, 0, _Q_LOCKED_VAL) != 0)
		cpu_relax();

	return true;
}

#include <asm-generic/qspinlock.h>

#endif /* _ASM_X86_QSPINLOCK_H */
