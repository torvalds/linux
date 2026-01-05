// SPDX-License-Identifier: GPL-2.0
/*
 * Split spinlock implementation out into its own file, so it can be
 * compiled in a FTRACE-compatible way.
 */
#include <linux/static_call.h>
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/jump_label.h>

DEFINE_STATIC_KEY_FALSE(virt_spin_lock_key);

#ifdef CONFIG_SMP
void __init native_pv_lock_init(void)
{
	if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		static_branch_enable(&virt_spin_lock_key);
}
#endif

#ifdef CONFIG_PARAVIRT_SPINLOCKS
__visible void __native_queued_spin_unlock(struct qspinlock *lock)
{
	native_queued_spin_unlock(lock);
}
PV_CALLEE_SAVE_REGS_THUNK(__native_queued_spin_unlock);

bool pv_is_native_spin_unlock(void)
{
	return pv_ops_lock.queued_spin_unlock.func ==
		__raw_callee_save___native_queued_spin_unlock;
}

__visible bool __native_vcpu_is_preempted(long cpu)
{
	return false;
}
PV_CALLEE_SAVE_REGS_THUNK(__native_vcpu_is_preempted);

bool pv_is_native_vcpu_is_preempted(void)
{
	return pv_ops_lock.vcpu_is_preempted.func ==
		__raw_callee_save___native_vcpu_is_preempted;
}

void __init paravirt_set_cap(void)
{
	if (!pv_is_native_spin_unlock())
		setup_force_cpu_cap(X86_FEATURE_PVUNLOCK);

	if (!pv_is_native_vcpu_is_preempted())
		setup_force_cpu_cap(X86_FEATURE_VCPUPREEMPT);
}

struct pv_lock_ops pv_ops_lock = {
	.queued_spin_lock_slowpath	= native_queued_spin_lock_slowpath,
	.queued_spin_unlock		= PV_CALLEE_SAVE(__native_queued_spin_unlock),
	.wait				= paravirt_nop,
	.kick				= paravirt_nop,
	.vcpu_is_preempted		= PV_CALLEE_SAVE(__native_vcpu_is_preempted),
};
EXPORT_SYMBOL(pv_ops_lock);
#endif
