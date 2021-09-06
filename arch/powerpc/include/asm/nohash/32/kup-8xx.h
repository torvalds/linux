/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_KUP_8XX_H_
#define _ASM_POWERPC_KUP_8XX_H_

#include <asm/bug.h>
#include <asm/mmu.h>

#ifdef CONFIG_PPC_KUAP

#ifndef __ASSEMBLY__

#include <asm/reg.h>

static inline void kuap_save_and_lock(struct pt_regs *regs)
{
	regs->kuap = mfspr(SPRN_MD_AP);
	mtspr(SPRN_MD_AP, MD_APG_KUAP);
}

static inline void kuap_user_restore(struct pt_regs *regs)
{
}

static inline void kuap_kernel_restore(struct pt_regs *regs, unsigned long kuap)
{
	mtspr(SPRN_MD_AP, regs->kuap);
}

static inline unsigned long kuap_get_and_assert_locked(void)
{
	unsigned long kuap = mfspr(SPRN_MD_AP);

	if (IS_ENABLED(CONFIG_PPC_KUAP_DEBUG))
		WARN_ON_ONCE(kuap >> 16 != MD_APG_KUAP >> 16);

	return kuap;
}

static inline void kuap_assert_locked(void)
{
	if (IS_ENABLED(CONFIG_PPC_KUAP_DEBUG))
		kuap_get_and_assert_locked();
}

static inline void allow_user_access(void __user *to, const void __user *from,
				     unsigned long size, unsigned long dir)
{
	mtspr(SPRN_MD_AP, MD_APG_INIT);
}

static inline void prevent_user_access(void __user *to, const void __user *from,
				       unsigned long size, unsigned long dir)
{
	mtspr(SPRN_MD_AP, MD_APG_KUAP);
}

static inline unsigned long prevent_user_access_return(void)
{
	unsigned long flags = mfspr(SPRN_MD_AP);

	mtspr(SPRN_MD_AP, MD_APG_KUAP);

	return flags;
}

static inline void restore_user_access(unsigned long flags)
{
	mtspr(SPRN_MD_AP, flags);
}

static inline bool
bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	return !((regs->kuap ^ MD_APG_KUAP) & 0xff000000);
}

#endif /* !__ASSEMBLY__ */

#endif /* CONFIG_PPC_KUAP */

#endif /* _ASM_POWERPC_KUP_8XX_H_ */
