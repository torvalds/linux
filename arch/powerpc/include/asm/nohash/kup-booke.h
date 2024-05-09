/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_KUP_BOOKE_H_
#define _ASM_POWERPC_KUP_BOOKE_H_

#include <asm/bug.h>
#include <asm/mmu.h>

#ifdef CONFIG_PPC_KUAP

#ifdef __ASSEMBLY__

.macro kuap_check_amr	gpr1, gpr2
.endm

#else

#include <linux/sched.h>

#include <asm/reg.h>

static __always_inline void __kuap_lock(void)
{
	mtspr(SPRN_PID, 0);
	isync();
}
#define __kuap_lock __kuap_lock

static __always_inline void __kuap_save_and_lock(struct pt_regs *regs)
{
	regs->kuap = mfspr(SPRN_PID);
	mtspr(SPRN_PID, 0);
	isync();
}
#define __kuap_save_and_lock __kuap_save_and_lock

static __always_inline void kuap_user_restore(struct pt_regs *regs)
{
	if (kuap_is_disabled())
		return;

	mtspr(SPRN_PID, current->thread.pid);

	/* Context synchronisation is performed by rfi */
}

static __always_inline void __kuap_kernel_restore(struct pt_regs *regs, unsigned long kuap)
{
	if (regs->kuap)
		mtspr(SPRN_PID, current->thread.pid);

	/* Context synchronisation is performed by rfi */
}

#ifdef CONFIG_PPC_KUAP_DEBUG
static __always_inline unsigned long __kuap_get_and_assert_locked(void)
{
	WARN_ON_ONCE(mfspr(SPRN_PID));

	return 0;
}
#define __kuap_get_and_assert_locked __kuap_get_and_assert_locked
#endif

static __always_inline void uaccess_begin_booke(unsigned long val)
{
	asm(ASM_MMU_FTR_IFSET("mtspr %0, %1; isync", "", %2) : :
	    "i"(SPRN_PID), "r"(val), "i"(MMU_FTR_KUAP) : "memory");
}

static __always_inline void uaccess_end_booke(void)
{
	asm(ASM_MMU_FTR_IFSET("mtspr %0, %1; isync", "", %2) : :
	    "i"(SPRN_PID), "r"(0), "i"(MMU_FTR_KUAP) : "memory");
}

static __always_inline void allow_user_access(void __user *to, const void __user *from,
					      unsigned long size, unsigned long dir)
{
	uaccess_begin_booke(current->thread.pid);
}

static __always_inline void prevent_user_access(unsigned long dir)
{
	uaccess_end_booke();
}

static __always_inline unsigned long prevent_user_access_return(void)
{
	unsigned long flags = mfspr(SPRN_PID);

	uaccess_end_booke();

	return flags;
}

static __always_inline void restore_user_access(unsigned long flags)
{
	if (flags)
		uaccess_begin_booke(current->thread.pid);
}

static __always_inline bool
__bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	return !regs->kuap;
}

#endif /* !__ASSEMBLY__ */

#endif /* CONFIG_PPC_KUAP */

#endif /* _ASM_POWERPC_KUP_BOOKE_H_ */
