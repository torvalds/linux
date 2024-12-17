/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LOONGARCH_QSPINLOCK_H
#define _ASM_LOONGARCH_QSPINLOCK_H

#include <linux/jump_label.h>

#ifdef CONFIG_PARAVIRT

DECLARE_STATIC_KEY_FALSE(virt_spin_lock_key);

#define virt_spin_lock virt_spin_lock

static inline bool virt_spin_lock(struct qspinlock *lock)
{
	int val;

	if (!static_branch_unlikely(&virt_spin_lock_key))
		return false;

	/*
	 * On hypervisors without PARAVIRT_SPINLOCKS support we fall
	 * back to a Test-and-Set spinlock, because fair locks have
	 * horrible lock 'holder' preemption issues.
	 */

__retry:
	val = atomic_read(&lock->val);

	if (val || !atomic_try_cmpxchg(&lock->val, &val, _Q_LOCKED_VAL)) {
		cpu_relax();
		goto __retry;
	}

	return true;
}

#endif /* CONFIG_PARAVIRT */

#include <asm-generic/qspinlock.h>

#endif // _ASM_LOONGARCH_QSPINLOCK_H
