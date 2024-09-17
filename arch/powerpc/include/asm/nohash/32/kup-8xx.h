/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_KUP_8XX_H_
#define _ASM_POWERPC_KUP_8XX_H_

#include <asm/bug.h>
#include <asm/mmu.h>

#ifdef CONFIG_PPC_KUAP

#ifndef __ASSEMBLY__

#include <linux/jump_label.h>

#include <asm/reg.h>

extern struct static_key_false disable_kuap_key;

static __always_inline bool kuap_is_disabled(void)
{
	return static_branch_unlikely(&disable_kuap_key);
}

static inline void __kuap_lock(void)
{
}

static inline void __kuap_save_and_lock(struct pt_regs *regs)
{
	regs->kuap = mfspr(SPRN_MD_AP);
	mtspr(SPRN_MD_AP, MD_APG_KUAP);
}

static inline void kuap_user_restore(struct pt_regs *regs)
{
}

static inline void __kuap_kernel_restore(struct pt_regs *regs, unsigned long kuap)
{
	mtspr(SPRN_MD_AP, regs->kuap);
}

static inline unsigned long __kuap_get_and_assert_locked(void)
{
	unsigned long kuap;

	kuap = mfspr(SPRN_MD_AP);

	if (IS_ENABLED(CONFIG_PPC_KUAP_DEBUG))
		WARN_ON_ONCE(kuap >> 16 != MD_APG_KUAP >> 16);

	return kuap;
}

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
