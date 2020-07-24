/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_QSPINLOCK_H
#define _ASM_POWERPC_QSPINLOCK_H

#include <asm-generic/qspinlock_types.h>

#define _Q_PENDING_LOOPS	(1 << 9) /* not tuned */

#define smp_mb__after_spinlock()   smp_mb()

static __always_inline int queued_spin_is_locked(struct qspinlock *lock)
{
	/*
	 * This barrier was added to simple spinlocks by commit 51d7d5205d338,
	 * but it should now be possible to remove it, asm arm64 has done with
	 * commit c6f5d02b6a0f.
	 */
	smp_mb();
	return atomic_read(&lock->val);
}
#define queued_spin_is_locked queued_spin_is_locked

#include <asm-generic/qspinlock.h>

#endif /* _ASM_POWERPC_QSPINLOCK_H */
