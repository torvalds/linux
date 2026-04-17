/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LOONGARCH_QSPINLOCK_H
#define _ASM_LOONGARCH_QSPINLOCK_H

#include <asm/kvm_para.h>
#include <linux/jump_label.h>

#ifdef CONFIG_PARAVIRT
DECLARE_STATIC_KEY_FALSE(virt_preempt_key);
DECLARE_STATIC_KEY_FALSE(virt_spin_lock_key);
DECLARE_PER_CPU(struct kvm_steal_time, steal_time);

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

/*
 * Macro is better than inline function here
 * With macro, parameter cpu is parsed only when it is used.
 * With inline function, parameter cpu is parsed even though it is not used.
 * This may cause cache line thrashing across NUMA nodes.
 */
#define vcpu_is_preempted(cpu)							\
({										\
	bool __val;								\
										\
	if (!static_branch_unlikely(&virt_preempt_key))				\
		__val = false;							\
	else {									\
		struct kvm_steal_time *src;					\
		src = &per_cpu(steal_time, cpu);				\
		__val = !!(READ_ONCE(src->preempted) & KVM_VCPU_PREEMPTED);	\
	}									\
	__val;									\
})

#endif /* CONFIG_PARAVIRT */

#include <asm-generic/qspinlock.h>

#endif // _ASM_LOONGARCH_QSPINLOCK_H
