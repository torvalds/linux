/*
 * Split spinlock implementation out into its own file, so it can be
 * compiled in a FTRACE-compatible way.
 */
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/jump_label.h>

#include <asm/paravirt.h>

__visible void __native_queued_spin_unlock(struct qspinlock *lock)
{
	native_queued_spin_unlock(lock);
}
PV_CALLEE_SAVE_REGS_THUNK(__native_queued_spin_unlock);

bool pv_is_native_spin_unlock(void)
{
	return pv_lock_ops.queued_spin_unlock.func ==
		__raw_callee_save___native_queued_spin_unlock;
}

__visible bool __native_vcpu_is_preempted(long cpu)
{
	return false;
}
PV_CALLEE_SAVE_REGS_THUNK(__native_vcpu_is_preempted);

bool pv_is_native_vcpu_is_preempted(void)
{
	return pv_lock_ops.vcpu_is_preempted.func ==
		__raw_callee_save___native_vcpu_is_preempted;
}

struct pv_lock_ops pv_lock_ops = {
#ifdef CONFIG_SMP
	.queued_spin_lock_slowpath = native_queued_spin_lock_slowpath,
	.queued_spin_unlock = PV_CALLEE_SAVE(__native_queued_spin_unlock),
	.wait = paravirt_nop,
	.kick = paravirt_nop,
	.vcpu_is_preempted = PV_CALLEE_SAVE(__native_vcpu_is_preempted),
#endif /* SMP */
};
EXPORT_SYMBOL(pv_lock_ops);
