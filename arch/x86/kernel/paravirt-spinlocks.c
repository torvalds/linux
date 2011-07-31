/*
 * Split spinlock implementation out into its own file, so it can be
 * compiled in a FTRACE-compatible way.
 */
#include <linux/spinlock.h>
#include <linux/module.h>

#include <asm/paravirt.h>

static inline void
default_spin_lock_flags(arch_spinlock_t *lock, unsigned long flags)
{
	arch_spin_lock(lock);
}

struct pv_lock_ops pv_lock_ops = {
#ifdef CONFIG_SMP
	.spin_is_locked = __ticket_spin_is_locked,
	.spin_is_contended = __ticket_spin_is_contended,

	.spin_lock = __ticket_spin_lock,
	.spin_lock_flags = default_spin_lock_flags,
	.spin_trylock = __ticket_spin_trylock,
	.spin_unlock = __ticket_spin_unlock,
#endif
};
EXPORT_SYMBOL(pv_lock_ops);

