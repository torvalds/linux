/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_KUP_RADIX_H
#define _ASM_POWERPC_BOOK3S_64_KUP_RADIX_H

#include <linux/const.h>
#include <asm/reg.h>

#define AMR_KUAP_BLOCK_READ	UL(0x4000000000000000)
#define AMR_KUAP_BLOCK_WRITE	UL(0x8000000000000000)
#define AMR_KUAP_BLOCKED	(AMR_KUAP_BLOCK_READ | AMR_KUAP_BLOCK_WRITE)
#define AMR_KUAP_SHIFT		62

#ifdef __ASSEMBLY__

.macro kuap_restore_amr	gpr1, gpr2
#ifdef CONFIG_PPC_KUAP
	BEGIN_MMU_FTR_SECTION_NESTED(67)
	mfspr	\gpr1, SPRN_AMR
	ld	\gpr2, STACK_REGS_KUAP(r1)
	cmpd	\gpr1, \gpr2
	beq	998f
	isync
	mtspr	SPRN_AMR, \gpr2
	/* No isync required, see kuap_restore_amr() */
998:
	END_MMU_FTR_SECTION_NESTED_IFSET(MMU_FTR_RADIX_KUAP, 67)
#endif
.endm

#ifdef CONFIG_PPC_KUAP
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
#endif

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

#include <linux/jump_label.h>

DECLARE_STATIC_KEY_FALSE(uaccess_flush_key);

#ifdef CONFIG_PPC_KUAP

#include <asm/mmu.h>
#include <asm/ptrace.h>

static inline void kuap_restore_amr(struct pt_regs *regs, unsigned long amr)
{
	if (mmu_has_feature(MMU_FTR_RADIX_KUAP) && unlikely(regs->kuap != amr)) {
		isync();
		mtspr(SPRN_AMR, regs->kuap);
		/*
		 * No isync required here because we are about to RFI back to
		 * previous context before any user accesses would be made,
		 * which is a CSI.
		 */
	}
}

static inline unsigned long kuap_get_and_check_amr(void)
{
	if (mmu_has_feature(MMU_FTR_RADIX_KUAP)) {
		unsigned long amr = mfspr(SPRN_AMR);
		if (IS_ENABLED(CONFIG_PPC_KUAP_DEBUG)) /* kuap_check_amr() */
			WARN_ON_ONCE(amr != AMR_KUAP_BLOCKED);
		return amr;
	}
	return 0;
}

static inline void kuap_check_amr(void)
{
	if (IS_ENABLED(CONFIG_PPC_KUAP_DEBUG) && mmu_has_feature(MMU_FTR_RADIX_KUAP))
		WARN_ON_ONCE(mfspr(SPRN_AMR) != AMR_KUAP_BLOCKED);
}

/*
 * We support individually allowing read or write, but we don't support nesting
 * because that would require an expensive read/modify write of the AMR.
 */

static inline unsigned long get_kuap(void)
{
	/*
	 * We return AMR_KUAP_BLOCKED when we don't support KUAP because
	 * prevent_user_access_return needs to return AMR_KUAP_BLOCKED to
	 * cause restore_user_access to do a flush.
	 *
	 * This has no effect in terms of actually blocking things on hash,
	 * so it doesn't break anything.
	 */
	if (!early_mmu_has_feature(MMU_FTR_RADIX_KUAP))
		return AMR_KUAP_BLOCKED;

	return mfspr(SPRN_AMR);
}

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

static inline bool
bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	return WARN(mmu_has_feature(MMU_FTR_RADIX_KUAP) &&
		    (regs->kuap & (is_write ? AMR_KUAP_BLOCK_WRITE : AMR_KUAP_BLOCK_READ)),
		    "Bug: %s fault blocked by AMR!", is_write ? "Write" : "Read");
}
#else /* CONFIG_PPC_KUAP */
static inline void kuap_restore_amr(struct pt_regs *regs, unsigned long amr) { }

static inline unsigned long kuap_get_and_check_amr(void)
{
	return 0UL;
}

static inline unsigned long get_kuap(void)
{
	return AMR_KUAP_BLOCKED;
}

static inline void set_kuap(unsigned long value) { }
#endif /* !CONFIG_PPC_KUAP */

static __always_inline void allow_user_access(void __user *to, const void __user *from,
					      unsigned long size, unsigned long dir)
{
	// This is written so we can resolve to a single case at build time
	BUILD_BUG_ON(!__builtin_constant_p(dir));
	if (dir == KUAP_READ)
		set_kuap(AMR_KUAP_BLOCK_WRITE);
	else if (dir == KUAP_WRITE)
		set_kuap(AMR_KUAP_BLOCK_READ);
	else if (dir == KUAP_READ_WRITE)
		set_kuap(0);
	else
		BUILD_BUG();
}

static inline void prevent_user_access(void __user *to, const void __user *from,
				       unsigned long size, unsigned long dir)
{
	set_kuap(AMR_KUAP_BLOCKED);
	if (static_branch_unlikely(&uaccess_flush_key))
		do_uaccess_flush();
}

static inline unsigned long prevent_user_access_return(void)
{
	unsigned long flags = get_kuap();

	set_kuap(AMR_KUAP_BLOCKED);
	if (static_branch_unlikely(&uaccess_flush_key))
		do_uaccess_flush();

	return flags;
}

static inline void restore_user_access(unsigned long flags)
{
	set_kuap(flags);
	if (static_branch_unlikely(&uaccess_flush_key) && flags == AMR_KUAP_BLOCKED)
		do_uaccess_flush();
}
#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_BOOK3S_64_KUP_RADIX_H */
