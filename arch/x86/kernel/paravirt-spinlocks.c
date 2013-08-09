/*
 * Split spinlock implementation out into its own file, so it can be
 * compiled in a FTRACE-compatible way.
 */
#include <linux/spinlock.h>
#include <linux/module.h>

#include <asm/paravirt.h>

struct pv_lock_ops pv_lock_ops = {
#ifdef CONFIG_SMP
	.lock_spinning = __PV_IS_CALLEE_SAVE(paravirt_nop),
	.unlock_kick = paravirt_nop,
#endif
};
EXPORT_SYMBOL(pv_lock_ops);

