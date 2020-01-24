/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_KUP_RADIX_H
#define _ASM_POWERPC_BOOK3S_64_KUP_RADIX_H

#include <linux/const.h>

#define AMR_KUAP_BLOCK_READ	UL(0x4000000000000000)
#define AMR_KUAP_BLOCK_WRITE	UL(0x8000000000000000)
#define AMR_KUAP_BLOCKED	(AMR_KUAP_BLOCK_READ | AMR_KUAP_BLOCK_WRITE)
#define AMR_KUAP_SHIFT		62

#ifdef __ASSEMBLY__

.macro kuap_restore_amr	gpr
#ifdef CONFIG_PPC_KUAP
	BEGIN_MMU_FTR_SECTION_NESTED(67)
	ld	\gpr, STACK_REGS_KUAP(r1)
	mtspr	SPRN_AMR, \gpr
	END_MMU_FTR_SECTION_NESTED_IFSET(MMU_FTR_RADIX_KUAP, 67)
#endif
.endm

.macro kuap_check_amr gpr1, gpr2
#ifdef CONFIG_PPC_KUAP_DEBUG
	BEGIN_MMU_FTR_SECTION_NESTED(67)
	mfspr	\gpr1, SPRN_AMR
	li	\gpr2, (AMR_KUAP_BLOCKED >> AMR_KUAP_SHIFT)
	sldi	\gpr2, \gpr2, AMR_KUAP_SHIFT
999:	tdne	\gpr1, \gpr2
	EMIT_BUG_ENTRY 999b, __FILE__, __LINE__, (BUGFLAG_WARNING | BUGFLAG_ONCE)
	END_MMU_FTR_SECTION_NESTED_IFSET(MMU_FTR_RADIX_KUAP, 67)
#endif
.endm

.macro kuap_save_amr_and_lock gpr1, gpr2, use_cr, msr_pr_cr
#ifdef CONFIG_PPC_KUAP
	BEGIN_MMU_FTR_SECTION_NESTED(67)
	.ifnb \msr_pr_cr
	bne	\msr_pr_cr, 99f
	.endif
	mfspr	\gpr1, SPRN_AMR
	std	\gpr1, STACK_REGS_KUAP(r1)
	li	\gpr2, (AMR_KUAP_BLOCKED >> AMR_KUAP_SHIFT)
	sldi	\gpr2, \gpr2, AMR_KUAP_SHIFT
	cmpd	\use_cr, \gpr1, \gpr2
	beq	\use_cr, 99f
	// We don't isync here because we very recently entered via rfid
	mtspr	SPRN_AMR, \gpr2
	isync
99:
	END_MMU_FTR_SECTION_NESTED_IFSET(MMU_FTR_RADIX_KUAP, 67)
#endif
.endm

#else /* !__ASSEMBLY__ */

#ifdef CONFIG_PPC_KUAP

#include <asm/reg.h>

/*
 * We support individually allowing read or write, but we don't support nesting
 * because that would require an expensive read/modify write of the AMR.
 */

static inline void set_kuap(unsigned long value)
{
	if (!early_mmu_has_feature(MMU_FTR_RADIX_KUAP))
		return;

	/*
	 * ISA v3.0B says we need a CSI (Context Synchronising Instruction) both
	 * before and after the move to AMR. See table 6 on page 1134.
	 */
	isync();
	mtspr(SPRN_AMR, value);
	isync();
}

static __always_inline void allow_user_access(void __user *to, const void __user *from,
					      unsigned long size, unsigned long dir)
{
	// This is written so we can resolve to a single case at build time
	BUILD_BUG_ON(!__builtin_constant_p(dir));
	if (dir == KUAP_READ)
		set_kuap(AMR_KUAP_BLOCK_WRITE);
	else if (dir == KUAP_WRITE)
		set_kuap(AMR_KUAP_BLOCK_READ);
	else
		set_kuap(0);
}

static inline void prevent_user_access(void __user *to, const void __user *from,
				       unsigned long size, unsigned long dir)
{
	set_kuap(AMR_KUAP_BLOCKED);
}

static inline bool
bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	return WARN(mmu_has_feature(MMU_FTR_RADIX_KUAP) &&
		    (regs->kuap & (is_write ? AMR_KUAP_BLOCK_WRITE : AMR_KUAP_BLOCK_READ)),
		    "Bug: %s fault blocked by AMR!", is_write ? "Write" : "Read");
}
#endif /* CONFIG_PPC_KUAP */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_BOOK3S_64_KUP_RADIX_H */
