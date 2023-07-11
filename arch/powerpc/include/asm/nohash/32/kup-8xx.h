/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_KUP_8XX_H_
#define _ASM_POWERPC_KUP_8XX_H_

#include <asm/bug.h>
#include <asm/mmu.h>

#ifdef CONFIG_PPC_KUAP

#ifndef __ASSEMBLY__

#include <asm/reg.h>

static inline void __kuap_save_and_lock(struct pt_regs *regs)
{
	regs->kuap = mfspr(SPRN_MD_AP);
	mtspr(SPRN_MD_AP, MD_APG_KUAP);
}
#define __kuap_save_and_lock __kuap_save_and_lock

static inline void kuap_user_restore(struct pt_regs *regs)
{
}

static inline void __kuap_kernel_restore(struct pt_regs *regs, unsigned long kuap)
{
	mtspr(SPRN_MD_AP, regs->kuap);
}

#ifdef CONFIG_PPC_KUAP_DEBUG
static inline unsigned long __kuap_get_and_assert_locked(void)
{
	WARN_ON_ONCE(mfspr(SPRN_MD_AP) >> 16 != MD_APG_KUAP >> 16);

	return 0;
}
#define __kuap_get_and_assert_locked __kuap_get_and_assert_locked
#endif

static inline void __allow_user_access(void __user *to, const void __user *from,
				       unsigned long size, unsigned long dir)
{
	mtspr(SPRN_MD_AP, MD_APG_INIT);
}

static inline void __prevent_user_access(unsigned long dir)
{
	mtspr(SPRN_MD_AP, MD_APG_KUAP);
}

static inline unsigned long __prevent_user_access_return(void)
{
	unsigned long flags;

	flags = mfspr(SPRN_MD_AP);

	mtspr(SPRN_MD_AP, MD_APG_KUAP);

	return flags;
}

static inline void __restore_user_access(unsigned long flags)
{
	mtspr(SPRN_MD_AP, flags);
}

static inline bool
__bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	return !((regs->kuap ^ MD_APG_KUAP) & 0xff000000);
}

#endif /* !__ASSEMBLY__ */

#endif /* CONFIG_PPC_KUAP */

#endif /* _ASM_POWERPC_KUP_8XX_H_ */
