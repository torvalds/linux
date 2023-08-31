/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_KUP_8XX_H_
#define _ASM_POWERPC_KUP_8XX_H_

#include <asm/bug.h>
#include <asm/mmu.h>

#ifdef CONFIG_PPC_KUAP

#ifndef __ASSEMBLY__

#include <asm/reg.h>

static __always_inline void __kuap_save_and_lock(struct pt_regs *regs)
{
	regs->kuap = mfspr(SPRN_MD_AP);
	mtspr(SPRN_MD_AP, MD_APG_KUAP);
}
#define __kuap_save_and_lock __kuap_save_and_lock

static __always_inline void kuap_user_restore(struct pt_regs *regs)
{
}

static __always_inline void __kuap_kernel_restore(struct pt_regs *regs, unsigned long kuap)
{
	mtspr(SPRN_MD_AP, regs->kuap);
}

#ifdef CONFIG_PPC_KUAP_DEBUG
static __always_inline unsigned long __kuap_get_and_assert_locked(void)
{
	WARN_ON_ONCE(mfspr(SPRN_MD_AP) >> 16 != MD_APG_KUAP >> 16);

	return 0;
}
#define __kuap_get_and_assert_locked __kuap_get_and_assert_locked
#endif

static __always_inline void uaccess_begin_8xx(unsigned long val)
{
	asm(ASM_MMU_FTR_IFSET("mtspr %0, %1", "", %2) : :
	    "i"(SPRN_MD_AP), "r"(val), "i"(MMU_FTR_KUAP) : "memory");
}

static __always_inline void uaccess_end_8xx(void)
{
	asm(ASM_MMU_FTR_IFSET("mtspr %0, %1", "", %2) : :
	    "i"(SPRN_MD_AP), "r"(MD_APG_KUAP), "i"(MMU_FTR_KUAP) : "memory");
}

static __always_inline void allow_user_access(void __user *to, const void __user *from,
					      unsigned long size, unsigned long dir)
{
	uaccess_begin_8xx(MD_APG_INIT);
}

static __always_inline void prevent_user_access(unsigned long dir)
{
	uaccess_end_8xx();
}

static __always_inline unsigned long prevent_user_access_return(void)
{
	unsigned long flags;

	flags = mfspr(SPRN_MD_AP);

	uaccess_end_8xx();

	return flags;
}

static __always_inline void restore_user_access(unsigned long flags)
{
	uaccess_begin_8xx(flags);
}

static __always_inline bool
__bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	return !((regs->kuap ^ MD_APG_KUAP) & 0xff000000);
}

#endif /* !__ASSEMBLY__ */

#endif /* CONFIG_PPC_KUAP */

#endif /* _ASM_POWERPC_KUP_8XX_H_ */
