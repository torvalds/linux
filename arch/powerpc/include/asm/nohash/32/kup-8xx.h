/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_KUP_8XX_H_
#define _ASM_POWERPC_KUP_8XX_H_

#include <asm/bug.h>
#include <asm/mmu.h>

#ifdef CONFIG_PPC_KUAP

#ifdef __ASSEMBLY__

.macro kuap_save_and_lock	sp, thread, gpr1, gpr2, gpr3
	lis	\gpr2, MD_APG_KUAP@h	/* only APG0 and APG1 are used */
	mfspr	\gpr1, SPRN_MD_AP
	mtspr	SPRN_MD_AP, \gpr2
	stw	\gpr1, STACK_REGS_KUAP(\sp)
.endm

.macro kuap_restore	sp, current, gpr1, gpr2, gpr3
	lwz	\gpr1, STACK_REGS_KUAP(\sp)
	mtspr	SPRN_MD_AP, \gpr1
.endm

.macro kuap_check	current, gpr
#ifdef CONFIG_PPC_KUAP_DEBUG
	mfspr	\gpr, SPRN_MD_AP
	rlwinm	\gpr, \gpr, 16, 0xffff
999:	twnei	\gpr, MD_APG_KUAP@h
	EMIT_BUG_ENTRY 999b, __FILE__, __LINE__, (BUGFLAG_WARNING | BUGFLAG_ONCE)
#endif
.endm

#else /* !__ASSEMBLY__ */

#include <asm/reg.h>

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
	return WARN(!((regs->kuap ^ MD_APG_KUAP) & 0xff000000),
		    "Bug: fault blocked by AP register !");
}

#endif /* !__ASSEMBLY__ */

#endif /* CONFIG_PPC_KUAP */

#endif /* _ASM_POWERPC_KUP_8XX_H_ */
