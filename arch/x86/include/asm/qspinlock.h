#ifndef _ASM_X86_QSPINLOCK_H
#define _ASM_X86_QSPINLOCK_H

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

#include <asm-generic/qspinlock.h>

#endif /* _ASM_X86_QSPINLOCK_H */
