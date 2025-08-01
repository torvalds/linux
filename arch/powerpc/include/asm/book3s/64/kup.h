/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_KUP_H
#define _ASM_POWERPC_BOOK3S_64_KUP_H

#include <linux/const.h>
#include <asm/reg.h>

#define AMR_KUAP_BLOCK_READ	UL(0x5455555555555555)
#define AMR_KUAP_BLOCK_WRITE	UL(0xa8aaaaaaaaaaaaaa)
#define AMR_KUEP_BLOCKED	UL(0x5455555555555555)
#define AMR_KUAP_BLOCKED	(AMR_KUAP_BLOCK_READ | AMR_KUAP_BLOCK_WRITE)

#ifdef __ASSEMBLER__

.macro kuap_user_restore gpr1, gpr2
#if defined(CONFIG_PPC_PKEY)
	BEGIN_MMU_FTR_SECTION_NESTED(67)
	b	100f  // skip_restore_amr
	END_MMU_FTR_SECTION_NESTED_IFCLR(MMU_FTR_PKEY, 67)
	/*
	 * AMR and IAMR are going to be different when
	 * returning to userspace.
	 */
	ld	\gpr1, STACK_REGS_AMR(r1)

	/*
	 * If kuap feature is not enabled, do the mtspr
	 * only if AMR value is different.
	 */
	BEGIN_MMU_FTR_SECTION_NESTED(68)
	mfspr	\gpr2, SPRN_AMR
	cmpd	\gpr1, \gpr2
	beq	99f
	END_MMU_FTR_SECTION_NESTED_IFCLR(MMU_FTR_KUAP, 68)

	isync
	mtspr	SPRN_AMR, \gpr1
99:
	/*
	 * Restore IAMR only when returning to userspace
	 */
	ld	\gpr1, STACK_REGS_IAMR(r1)

	/*
	 * If kuep feature is not enabled, do the mtspr
	 * only if IAMR value is different.
	 */
	BEGIN_MMU_FTR_SECTION_NESTED(69)
	mfspr	\gpr2, SPRN_IAMR
	cmpd	\gpr1, \gpr2
	beq	100f
	END_MMU_FTR_SECTION_NESTED_IFCLR(MMU_FTR_BOOK3S_KUEP, 69)

	isync
	mtspr	SPRN_IAMR, \gpr1

100: //skip_restore_amr
	/* No isync required, see kuap_user_restore() */
#endif
.endm

.macro kuap_kernel_restore gpr1, gpr2
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
	END_MMU_FTR_SECTION_NESTED_IFSET(MMU_FTR_KUAP, 67)
#endif
.endm

#ifdef CONFIG_PPC_KUAP
.macro kuap_check_amr gpr1, gpr2
#ifdef CONFIG_PPC_KUAP_DEBUG
	BEGIN_MMU_FTR_SECTION_NESTED(67)
	mfspr	\gpr1, SPRN_AMR
	/* Prevent access to userspace using any key values */
	LOAD_REG_IMMEDIATE(\gpr2, AMR_KUAP_BLOCKED)
999:	tdne	\gpr1, \gpr2
	EMIT_WARN_ENTRY 999b, __FILE__, __LINE__, (BUGFLAG_WARNING | BUGFLAG_ONCE)
	END_MMU_FTR_SECTION_NESTED_IFSET(MMU_FTR_KUAP, 67)
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
	END_MMU_FTR_SECTION_NESTED_IFCLR(MMU_FTR_PKEY | MMU_FTR_KUAP, 68)

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
	END_MMU_FTR_SECTION_NESTED_IFSET(MMU_FTR_KUAP, 69)

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

#else /* !__ASSEMBLER__ */

#include <linux/jump_label.h>
#include <linux/sched.h>

DECLARE_STATIC_KEY_FALSE(uaccess_flush_key);

#ifdef CONFIG_PPC_PKEY

extern u64 __ro_after_init default_uamor;
extern u64 __ro_after_init default_amr;
extern u64 __ro_after_init default_iamr;

#include <asm/mmu.h>
#include <asm/ptrace.h>

/* usage of kthread_use_mm() should inherit the
 * AMR value of the operating address space. But, the AMR value is
 * thread-specific and we inherit the address space and not thread
 * access restrictions. Because of this ignore AMR value when accessing
 * userspace via kernel thread.
 */
static __always_inline u64 current_thread_amr(void)
{
	if (current->thread.regs)
		return current->thread.regs->amr;
	return default_amr;
}

static __always_inline u64 current_thread_iamr(void)
{
	if (current->thread.regs)
		return current->thread.regs->iamr;
	return default_iamr;
}
#endif /* CONFIG_PPC_PKEY */

#ifdef CONFIG_PPC_KUAP

static __always_inline void kuap_user_restore(struct pt_regs *regs)
{
	bool restore_amr = false, restore_iamr = false;
	unsigned long amr, iamr;

	if (!mmu_has_feature(MMU_FTR_PKEY))
		return;

	if (!mmu_has_feature(MMU_FTR_KUAP)) {
		amr = mfspr(SPRN_AMR);
		if (amr != regs->amr)
			restore_amr = true;
	} else {
		restore_amr = true;
	}

	if (!mmu_has_feature(MMU_FTR_BOOK3S_KUEP)) {
		iamr = mfspr(SPRN_IAMR);
		if (iamr != regs->iamr)
			restore_iamr = true;
	} else {
		restore_iamr = true;
	}


	if (restore_amr || restore_iamr) {
		isync();
		if (restore_amr)
			mtspr(SPRN_AMR, regs->amr);
		if (restore_iamr)
			mtspr(SPRN_IAMR, regs->iamr);
	}
	/*
	 * No isync required here because we are about to rfi
	 * back to previous context before any user accesses
	 * would be made, which is a CSI.
	 */
}

static __always_inline void __kuap_kernel_restore(struct pt_regs *regs, unsigned long amr)
{
	if (likely(regs->amr == amr))
		return;

	isync();
	mtspr(SPRN_AMR, regs->amr);
	/*
	 * No isync required here because we are about to rfi
	 * back to previous context before any user accesses
	 * would be made, which is a CSI.
	 *
	 * No need to restore IAMR when returning to kernel space.
	 */
}

static __always_inline unsigned long __kuap_get_and_assert_locked(void)
{
	unsigned long amr = mfspr(SPRN_AMR);

	if (IS_ENABLED(CONFIG_PPC_KUAP_DEBUG)) /* kuap_check_amr() */
		WARN_ON_ONCE(amr != AMR_KUAP_BLOCKED);
	return amr;
}
#define __kuap_get_and_assert_locked __kuap_get_and_assert_locked

/* __kuap_lock() not required, book3s/64 does that in ASM */

/*
 * We support individually allowing read or write, but we don't support nesting
 * because that would require an expensive read/modify write of the AMR.
 */

static __always_inline unsigned long get_kuap(void)
{
	/*
	 * We return AMR_KUAP_BLOCKED when we don't support KUAP because
	 * prevent_user_access_return needs to return AMR_KUAP_BLOCKED to
	 * cause restore_user_access to do a flush.
	 *
	 * This has no effect in terms of actually blocking things on hash,
	 * so it doesn't break anything.
	 */
	if (!mmu_has_feature(MMU_FTR_KUAP))
		return AMR_KUAP_BLOCKED;

	return mfspr(SPRN_AMR);
}

static __always_inline void set_kuap(unsigned long value)
{
	if (!mmu_has_feature(MMU_FTR_KUAP))
		return;

	/*
	 * ISA v3.0B says we need a CSI (Context Synchronising Instruction) both
	 * before and after the move to AMR. See table 6 on page 1134.
	 */
	isync();
	mtspr(SPRN_AMR, value);
	isync();
}

static __always_inline bool
__bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	/*
	 * For radix this will be a storage protection fault (DSISR_PROTFAULT).
	 * For hash this will be a key fault (DSISR_KEYFAULT)
	 */
	/*
	 * We do have exception table entry, but accessing the
	 * userspace results in fault.  This could be because we
	 * didn't unlock the AMR or access is denied by userspace
	 * using a key value that blocks access. We are only interested
	 * in catching the use case of accessing without unlocking
	 * the AMR. Hence check for BLOCK_WRITE/READ against AMR.
	 */
	if (is_write) {
		return (regs->amr & AMR_KUAP_BLOCK_WRITE) == AMR_KUAP_BLOCK_WRITE;
	}
	return (regs->amr & AMR_KUAP_BLOCK_READ) == AMR_KUAP_BLOCK_READ;
}

static __always_inline void allow_user_access(void __user *to, const void __user *from,
					      unsigned long size, unsigned long dir)
{
	unsigned long thread_amr = 0;

	// This is written so we can resolve to a single case at build time
	BUILD_BUG_ON(!__builtin_constant_p(dir));

	if (mmu_has_feature(MMU_FTR_PKEY))
		thread_amr = current_thread_amr();

	if (dir == KUAP_READ)
		set_kuap(thread_amr | AMR_KUAP_BLOCK_WRITE);
	else if (dir == KUAP_WRITE)
		set_kuap(thread_amr | AMR_KUAP_BLOCK_READ);
	else if (dir == KUAP_READ_WRITE)
		set_kuap(thread_amr);
	else
		BUILD_BUG();
}

#else /* CONFIG_PPC_KUAP */

static __always_inline unsigned long get_kuap(void)
{
	return AMR_KUAP_BLOCKED;
}

static __always_inline void set_kuap(unsigned long value) { }

static __always_inline void allow_user_access(void __user *to, const void __user *from,
					      unsigned long size, unsigned long dir)
{ }

#endif /* !CONFIG_PPC_KUAP */

static __always_inline void prevent_user_access(unsigned long dir)
{
	set_kuap(AMR_KUAP_BLOCKED);
	if (static_branch_unlikely(&uaccess_flush_key))
		do_uaccess_flush();
}

static __always_inline unsigned long prevent_user_access_return(void)
{
	unsigned long flags = get_kuap();

	set_kuap(AMR_KUAP_BLOCKED);
	if (static_branch_unlikely(&uaccess_flush_key))
		do_uaccess_flush();

	return flags;
}

static __always_inline void restore_user_access(unsigned long flags)
{
	set_kuap(flags);
	if (static_branch_unlikely(&uaccess_flush_key) && flags == AMR_KUAP_BLOCKED)
		do_uaccess_flush();
}
#endif /* __ASSEMBLER__ */

#endif /* _ASM_POWERPC_BOOK3S_64_KUP_H */
