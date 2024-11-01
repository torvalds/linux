/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_KUP_BOOKE_H_
#define _ASM_POWERPC_KUP_BOOKE_H_

#include <asm/bug.h>

#ifdef CONFIG_PPC_KUAP

#ifdef __ASSEMBLY__

.macro kuap_check_amr	gpr1, gpr2
.endm

#else

#include <linux/jump_label.h>
#include <linux/sched.h>

#include <asm/reg.h>

extern struct static_key_false disable_kuap_key;

static __always_inline bool kuap_is_disabled(void)
{
	return static_branch_unlikely(&disable_kuap_key);
}

static inline void __kuap_lock(void)
{
	mtspr(SPRN_PID, 0);
	isync();
}

static inline void __kuap_save_and_lock(struct pt_regs *regs)
{
	regs->kuap = mfspr(SPRN_PID);
	mtspr(SPRN_PID, 0);
	isync();
}

static inline void kuap_user_restore(struct pt_regs *regs)
{
	if (kuap_is_disabled())
		return;

	mtspr(SPRN_PID, current->thread.pid);

	/* Context synchronisation is performed by rfi */
}

static inline void __kuap_kernel_restore(struct pt_regs *regs, unsigned long kuap)
{
	if (regs->kuap)
		mtspr(SPRN_PID, current->thread.pid);

	/* Context synchronisation is performed by rfi */
}

static inline unsigned long __kuap_get_and_assert_locked(void)
{
	unsigned long kuap = mfspr(SPRN_PID);

	if (IS_ENABLED(CONFIG_PPC_KUAP_DEBUG))
		WARN_ON_ONCE(kuap);

	return kuap;
}

static inline void __allow_user_access(void __user *to, const void __user *from,
				       unsigned long size, unsigned long dir)
{
	mtspr(SPRN_PID, current->thread.pid);
	isync();
}

static inline void __prevent_user_access(unsigned long dir)
{
	mtspr(SPRN_PID, 0);
	isync();
}

static inline unsigned long __prevent_user_access_return(void)
{
	unsigned long flags = mfspr(SPRN_PID);

	mtspr(SPRN_PID, 0);
	isync();

	return flags;
}

static inline void __restore_user_access(unsigned long flags)
{
	if (flags) {
		mtspr(SPRN_PID, current->thread.pid);
		isync();
	}
}

static inline bool
__bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	return !regs->kuap;
}

#endif /* !__ASSEMBLY__ */

#endif /* CONFIG_PPC_KUAP */

#endif /* _ASM_POWERPC_KUP_BOOKE_H_ */
