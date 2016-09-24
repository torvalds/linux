/*
 * Split spinlock implementation out into its own file, so it can be
 * compiled in a FTRACE-compatible way.
 */
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/jump_label.h>

#include <asm/paravirt.h>

#ifdef CONFIG_QUEUED_SPINLOCKS
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
#endif

struct pv_lock_ops pv_lock_ops = {
#ifdef CONFIG_SMP
#ifdef CONFIG_QUEUED_SPINLOCKS
	.queued_spin_lock_slowpath = native_queued_spin_lock_slowpath,
	.queued_spin_unlock = PV_CALLEE_SAVE(__native_queued_spin_unlock),
	.wait = paravirt_nop,
	.kick = paravirt_nop,
#else /* !CONFIG_QUEUED_SPINLOCKS */
	.lock_spinning = __PV_IS_CALLEE_SAVE(paravirt_nop),
	.unlock_kick = paravirt_nop,
#endif /* !CONFIG_QUEUED_SPINLOCKS */
#endif /* SMP */
};
EXPORT_SYMBOL(pv_lock_ops);

struct static_key paravirt_ticketlocks_enabled = STATIC_KEY_INIT_FALSE;
EXPORT_SYMBOL(paravirt_ticketlocks_enabled);
