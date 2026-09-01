// SPDX-License-Identifier: GPL-2.0
/*
 * Split spinlock implementation out into its own file, so it can be
 * compiled in a FTRACE-compatible way.
 */
#include <linux/static_call.h>
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/jump_label.h>
#include <trace/events/lock.h>

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

DEFINE_STATIC_CALL(queued_spin_lock_slowpath, native_queued_spin_lock_slowpath);
EXPORT_STATIC_CALL_TRAMP(queued_spin_lock_slowpath);
DEFINE_STATIC_CALL(queued_spin_unlock, __raw_callee_save___native_queued_spin_unlock);
EXPORT_STATIC_CALL_TRAMP(queued_spin_unlock);

/*
 * Traced unlock variants, swapped in via static_call while the
 * contended_release tracepoint is enabled. Two of them, so each tail calls its
 * own base directly.
 */
__visible void native_queued_spin_unlock_traced(struct qspinlock *lock)
{
	if (queued_spin_is_contended(lock))
		trace_call__contended_release(lock);
	native_queued_spin_unlock(lock);
}
PV_CALLEE_SAVE_REGS_THUNK(native_queued_spin_unlock_traced);

__visible void pv_queued_spin_unlock_traced(struct qspinlock *lock)
{
	if (queued_spin_is_contended(lock))
		trace_call__contended_release(lock);
	__raw_callee_save___pv_queued_spin_unlock(lock);
}
PV_CALLEE_SAVE_REGS_THUNK(pv_queued_spin_unlock_traced);

bool pv_is_native_spin_unlock(void)
{
	void *unlock = static_call_query(queued_spin_unlock);

	return unlock == __raw_callee_save___native_queued_spin_unlock ||
	       unlock == __raw_callee_save_native_queued_spin_unlock_traced;
}

int arch_contended_release_trace_reg(void)
{
	void *cur = static_call_query(queued_spin_unlock);

	if (cur == __raw_callee_save___native_queued_spin_unlock)
		static_call_update(queued_spin_unlock,
				   __raw_callee_save_native_queued_spin_unlock_traced);
	else if (cur == __raw_callee_save___pv_queued_spin_unlock)
		static_call_update(queued_spin_unlock,
				   __raw_callee_save_pv_queued_spin_unlock_traced);
	return 0;
}

void arch_contended_release_trace_unreg(void)
{
	void *cur = static_call_query(queued_spin_unlock);

	if (cur == __raw_callee_save_native_queued_spin_unlock_traced)
		static_call_update(queued_spin_unlock,
				   __raw_callee_save___native_queued_spin_unlock);
	else if (cur == __raw_callee_save_pv_queued_spin_unlock_traced)
		static_call_update(queued_spin_unlock,
				   __raw_callee_save___pv_queued_spin_unlock);
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
	if (!pv_is_native_vcpu_is_preempted())
		setup_force_cpu_cap(X86_FEATURE_VCPUPREEMPT);
}

struct pv_lock_ops pv_ops_lock = {
	.wait				= paravirt_nop,
	.kick				= paravirt_nop,
	.vcpu_is_preempted		= PV_CALLEE_SAVE(__native_vcpu_is_preempted),
};
EXPORT_SYMBOL(pv_ops_lock);
#endif
