/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_KUP_H
#define _ASM_POWERPC_BOOK3S_64_KUP_H

#include <linux/const.h>
#include <asm/reg.h>

#define AMR_KUAP_BLOCK_READ	UL(0x4000000000000000)
#define AMR_KUAP_BLOCK_WRITE	UL(0x8000000000000000)
#define AMR_KUEP_BLOCKED	(1UL << 62)
#define AMR_KUAP_BLOCKED	(AMR_KUAP_BLOCK_READ | AMR_KUAP_BLOCK_WRITE)
#define AMR_KUAP_SHIFT		62

#ifdef __ASSEMBLY__

.macro kuap_user_restore gpr1
#if defined(CONFIG_PPC_PKEY)
	BEGIN_MMU_FTR_SECTION_NESTED(67)
	/*
	 * AMR and IAMR are going to be different when
	 * returning to userspace.
	 */
	ld	\gpr1, STACK_REGS_AMR(r1)
	isync
	mtspr	SPRN_AMR, \gpr1
	/*
	 * Restore IAMR only when returning to userspace
	 */
	ld	\gpr1, STACK_REGS_IAMR(r1)
	mtspr	SPRN_IAMR, \gpr1

	/* No isync required, see kuap_user_restore() */
	END_MMU_FTR_SECTION_NESTED_IFSET(MMU_FTR_PKEY, 67)
#endif
.endm

.macro kuap_kernel_restore	gpr1, gpr2
#if defined(CONFIG_PPC_PKEY)

	BEGIN_MMU_FTR_SECTION_NESTED(67)
	/*
	 * AMR is going to be mostly the same since we are
	 * returning to the kernel. Compare and do a mtspr.
	 */
	ld	\gpr2, STACK_REGS_AMR(r1)
	mfspr	\gpr1, SPRN_AMR
	cmpd	\gpr1, \gpr2
	beq	100f
	isync
	mtspr	SPRN_AMR, \gpr2
	/*
	 * No isync required, see kuap_restore_amr()
	 * No need to restore IAMR when returning to kernel space.
	 */
100:
	END_MMU_FTR_SECTION_NESTED_IFSET(MMU_FTR_BOOK3S_KUAP, 67)
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
	END_MMU_FTR_SECTION_NESTED_IFSET(MMU_FTR_BOOK3S_KUAP, 67)
#endif
.endm
#endif

/*
 *	if (pkey) {
 *
 *		save AMR -> stack;
 *		if (kuap) {
 *			if (AMR != BLOCKED)
 *				KUAP_BLOCKED -> AMR;
 *		}
 *		if (from_user) {
 *			save IAMR -> stack;
 *			if (kuep) {
 *				KUEP_BLOCKED ->IAMR
 *			}
 *		}
 *		return;
 *	}
 *
 *	if (kuap) {
 *		if (from_kernel) {
 *			save AMR -> stack;
 *			if (AMR != BLOCKED)
 *				KUAP_BLOCKED -> AMR;
 *		}
 *
 *	}
 */
.macro kuap_save_amr_and_lock gpr1, gpr2, use_cr, msr_pr_cr
#if defined(CONFIG_PPC_PKEY)

	/*
	 * if both pkey and kuap is disabled, nothing to do
	 */
	BEGIN_MMU_FTR_SECTION_NESTED(68)
	b	100f  // skip_save_amr
	END_MMU_FTR_SECTION_NESTED_IFCLR(MMU_FTR_PKEY | MMU_FTR_BOOK3S_KUAP, 68)

	/*
	 * if pkey is disabled and we are entering from userspace
	 * don't do anything.
	 */
	BEGIN_MMU_FTR_SECTION_NESTED(67)
	.ifnb \msr_pr_cr
	/*
	 * Without pkey we are not changing AMR outside the kernel
	 * hence skip this completely.
	 */
	bne	\msr_pr_cr, 100f  // from userspace
	.endif
        END_MMU_FTR_SECTION_NESTED_IFCLR(MMU_FTR_PKEY, 67)

	/*
	 * pkey is enabled or pkey is disabled but entering from kernel
	 */
	mfspr	\gpr1, SPRN_AMR
	std	\gpr1, STACK_REGS_AMR(r1)

	/*
	 * update kernel AMR with AMR_KUAP_BLOCKED only
	 * if KUAP feature is enabled
	 */
	BEGIN_MMU_FTR_SECTION_NESTED(69)
	LOAD_REG_IMMEDIATE(\gpr2, AMR_KUAP_BLOCKED)
	cmpd	\use_cr, \gpr1, \gpr2
	beq	\use_cr, 102f
	/*
	 * We don't isync here because we very recently entered via an interrupt
	 */
	mtspr	SPRN_AMR, \gpr2
	isync
102:
	END_MMU_FTR_SECTION_NESTED_IFSET(MMU_FTR_BOOK3S_KUAP, 69)

	/*
	 * if entering from kernel we don't need save IAMR
	 */
	.ifnb \msr_pr_cr
	beq	\msr_pr_cr, 100f // from kernel space
	mfspr	\gpr1, SPRN_IAMR
	std	\gpr1, STACK_REGS_IAMR(r1)

	/*
	 * update kernel IAMR with AMR_KUEP_BLOCKED only
	 * if KUEP feature is enabled
	 */
	BEGIN_MMU_FTR_SECTION_NESTED(70)
	LOAD_REG_IMMEDIATE(\gpr2, AMR_KUEP_BLOCKED)
	mtspr	SPRN_IAMR, \gpr2
	isync
	END_MMU_FTR_SECTION_NESTED_IFSET(MMU_FTR_BOOK3S_KUEP, 70)
	.endif

100: // skip_save_amr
#endif
.endm

#else /* !__ASSEMBLY__ */

#include <linux/jump_label.h>

DECLARE_STATIC_KEY_FALSE(uaccess_flush_key);

#ifdef CONFIG_PPC_PKEY

#include <asm/mmu.h>
#include <asm/ptrace.h>

/*
 * For kernel thread that doesn't have thread.regs return
 * default AMR/IAMR values.
 */
static inline u64 current_thread_amr(void)
{
	if (current->thread.regs)
		return current->thread.regs->amr;
	return AMR_KUAP_BLOCKED;
}

static inline u64 current_thread_iamr(void)
{
	if (current->thread.regs)
		return current->thread.regs->iamr;
	return AMR_KUEP_BLOCKED;
}
#endif /* CONFIG_PPC_PKEY */

#ifdef CONFIG_PPC_KUAP

static inline void kuap_user_restore(struct pt_regs *regs)
{
	if (!mmu_has_feature(MMU_FTR_PKEY))
		return;

	isync();
	mtspr(SPRN_AMR, regs->amr);
	mtspr(SPRN_IAMR, regs->iamr);
	/*
	 * No isync required here because we are about to rfi
	 * back to previous context before any user accesses
	 * would be made, which is a CSI.
	 */
}
static inline void kuap_kernel_restore(struct pt_regs *regs,
					   unsigned long amr)
{
	if (mmu_has_feature(MMU_FTR_BOOK3S_KUAP)) {
		if (unlikely(regs->amr != amr)) {
			isync();
			mtspr(SPRN_AMR, regs->amr);
			/*
			 * No isync required here because we are about to rfi
			 * back to previous context before any user accesses
			 * would be made, which is a CSI.
			 */
		}
	}
	/*
	 * No need to restore IAMR when returning to kernel space.
	 */
}

static inline unsigned long kuap_get_and_check_amr(void)
{
	if (mmu_has_feature(MMU_FTR_BOOK3S_KUAP)) {
		unsigned long amr = mfspr(SPRN_AMR);
		if (IS_ENABLED(CONFIG_PPC_KUAP_DEBUG)) /* kuap_check_amr() */
			WARN_ON_ONCE(amr != AMR_KUAP_BLOCKED);
		return amr;
	}
	return 0;
}

#else /* CONFIG_PPC_PKEY */

static inline void kuap_user_restore(struct pt_regs *regs)
{
}

static inline void kuap_kernel_restore(struct pt_regs *regs, unsigned long amr)
{
}

static inline unsigned long kuap_get_and_check_amr(void)
{
	return 0;
}

#endif /* CONFIG_PPC_PKEY */


#ifdef CONFIG_PPC_KUAP

static inline void kuap_check_amr(void)
{
	if (IS_ENABLED(CONFIG_PPC_KUAP_DEBUG) && mmu_has_feature(MMU_FTR_BOOK3S_KUAP))
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
	if (!early_mmu_has_feature(MMU_FTR_BOOK3S_KUAP))
		return AMR_KUAP_BLOCKED;

	return mfspr(SPRN_AMR);
}

static inline void set_kuap(unsigned long value)
{
	if (!early_mmu_has_feature(MMU_FTR_BOOK3S_KUAP))
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
	return WARN(mmu_has_feature(MMU_FTR_BOOK3S_KUAP) &&
		    (regs->kuap & (is_write ? AMR_KUAP_BLOCK_WRITE : AMR_KUAP_BLOCK_READ)),
		    "Bug: %s fault blocked by AMR!", is_write ? "Write" : "Read");
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
	else if (dir == KUAP_READ_WRITE)
		set_kuap(0);
	else
		BUILD_BUG();
}

#else /* CONFIG_PPC_KUAP */

static inline unsigned long get_kuap(void)
{
	return AMR_KUAP_BLOCKED;
}

static inline void set_kuap(unsigned long value) { }

static __always_inline void allow_user_access(void __user *to, const void __user *from,
					      unsigned long size, unsigned long dir)
{ }

#endif /* !CONFIG_PPC_KUAP */

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

#endif /* _ASM_POWERPC_BOOK3S_64_KUP_H */
