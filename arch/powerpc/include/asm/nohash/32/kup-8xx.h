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

static inline void kuap_save_and_lock(struct pt_regs *regs)
{
	if (kuap_is_disabled())
		return;

	regs->kuap = mfspr(SPRN_MD_AP);
	mtspr(SPRN_MD_AP, MD_APG_KUAP);
}

static inline void kuap_user_restore(struct pt_regs *regs)
{
}

static inline void kuap_kernel_restore(struct pt_regs *regs, unsigned long kuap)
{
	if (kuap_is_disabled())
		return;

	mtspr(SPRN_MD_AP, regs->kuap);
}

static inline unsigned long kuap_get_and_assert_locked(void)
{
	unsigned long kuap;

	if (kuap_is_disabled())
		return MD_APG_INIT;

	kuap = mfspr(SPRN_MD_AP);

	if (IS_ENABLED(CONFIG_PPC_KUAP_DEBUG))
		WARN_ON_ONCE(kuap >> 16 != MD_APG_KUAP >> 16);

	return kuap;
}

static inline void kuap_assert_locked(void)
{
	if (IS_ENABLED(CONFIG_PPC_KUAP_DEBUG) && !kuap_is_disabled())
		kuap_get_and_assert_locked();
}

static inline void allow_user_access(void __user *to, const void __user *from,
				     unsigned long size, unsigned long dir)
{
	if (kuap_is_disabled())
		return;

	mtspr(SPRN_MD_AP, MD_APG_INIT);
}

static inline void prevent_user_access(unsigned long dir)
{
	if (kuap_is_disabled())
		return;

	mtspr(SPRN_MD_AP, MD_APG_KUAP);
}

static inline unsigned long prevent_user_access_return(void)
{
	unsigned long flags;

	if (kuap_is_disabled())
		return MD_APG_INIT;

	flags = mfspr(SPRN_MD_AP);

	mtspr(SPRN_MD_AP, MD_APG_KUAP);

	return flags;
}

static inline void restore_user_access(unsigned long flags)
{
	if (kuap_is_disabled())
		return;

	mtspr(SPRN_MD_AP, flags);
}

static inline bool
bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	if (kuap_is_disabled())
		return false;

	return !((regs->kuap ^ MD_APG_KUAP) & 0xff000000);
}

#endif /* !__ASSEMBLY__ */

#endif /* CONFIG_PPC_KUAP */

#endif /* _ASM_POWERPC_KUP_8XX_H_ */
