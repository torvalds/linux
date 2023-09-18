/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_32_KUP_H
#define _ASM_POWERPC_BOOK3S_32_KUP_H

#include <asm/bug.h>
#include <asm/book3s/32/mmu-hash.h>
#include <asm/mmu.h>
#include <asm/synch.h>

#ifndef __ASSEMBLY__

#ifdef CONFIG_PPC_KUAP

#include <linux/sched.h>

#define KUAP_NONE	(~0UL)

static __always_inline void kuap_lock_one(unsigned long addr)
{
	mtsr(mfsr(addr) | SR_KS, addr);
	isync();	/* Context sync required after mtsr() */
}

static __always_inline void kuap_unlock_one(unsigned long addr)
{
	mtsr(mfsr(addr) & ~SR_KS, addr);
	isync();	/* Context sync required after mtsr() */
}

static __always_inline void uaccess_begin_32s(unsigned long addr)
{
	unsigned long tmp;

	asm volatile(ASM_MMU_FTR_IFSET(
		"mfsrin %0, %1;"
		"rlwinm %0, %0, 0, %2;"
		"mtsrin %0, %1;"
		"isync", "", %3)
		: "=&r"(tmp)
		: "r"(addr), "i"(~SR_KS), "i"(MMU_FTR_KUAP)
		: "memory");
}

static __always_inline void uaccess_end_32s(unsigned long addr)
{
	unsigned long tmp;

	asm volatile(ASM_MMU_FTR_IFSET(
		"mfsrin %0, %1;"
		"oris %0, %0, %2;"
		"mtsrin %0, %1;"
		"isync", "", %3)
		: "=&r"(tmp)
		: "r"(addr), "i"(SR_KS >> 16), "i"(MMU_FTR_KUAP)
		: "memory");
}

static __always_inline void __kuap_save_and_lock(struct pt_regs *regs)
{
	unsigned long kuap = current->thread.kuap;

	regs->kuap = kuap;
	if (unlikely(kuap == KUAP_NONE))
		return;

	current->thread.kuap = KUAP_NONE;
	kuap_lock_one(kuap);
}
#define __kuap_save_and_lock __kuap_save_and_lock

static __always_inline void kuap_user_restore(struct pt_regs *regs)
{
}

static __always_inline void __kuap_kernel_restore(struct pt_regs *regs, unsigned long kuap)
{
	if (unlikely(kuap != KUAP_NONE)) {
		current->thread.kuap = KUAP_NONE;
		kuap_lock_one(kuap);
	}

	if (likely(regs->kuap == KUAP_NONE))
		return;

	current->thread.kuap = regs->kuap;

	kuap_unlock_one(regs->kuap);
}

static __always_inline unsigned long __kuap_get_and_assert_locked(void)
{
	unsigned long kuap = current->thread.kuap;

	WARN_ON_ONCE(IS_ENABLED(CONFIG_PPC_KUAP_DEBUG) && kuap != KUAP_NONE);

	return kuap;
}
#define __kuap_get_and_assert_locked __kuap_get_and_assert_locked

static __always_inline void allow_user_access(void __user *to, const void __user *from,
					      u32 size, unsigned long dir)
{
	BUILD_BUG_ON(!__builtin_constant_p(dir));

	if (!(dir & KUAP_WRITE))
		return;

	current->thread.kuap = (__force u32)to;
	uaccess_begin_32s((__force u32)to);
}

static __always_inline void prevent_user_access(unsigned long dir)
{
	u32 kuap = current->thread.kuap;

	BUILD_BUG_ON(!__builtin_constant_p(dir));

	if (!(dir & KUAP_WRITE))
		return;

	current->thread.kuap = KUAP_NONE;
	uaccess_end_32s(kuap);
}

static __always_inline unsigned long prevent_user_access_return(void)
{
	unsigned long flags = current->thread.kuap;

	if (flags != KUAP_NONE) {
		current->thread.kuap = KUAP_NONE;
		uaccess_end_32s(flags);
	}

	return flags;
}

static __always_inline void restore_user_access(unsigned long flags)
{
	if (flags != KUAP_NONE) {
		current->thread.kuap = flags;
		uaccess_begin_32s(flags);
	}
}

static __always_inline bool
__bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	unsigned long kuap = regs->kuap;

	if (!is_write)
		return false;
	if (kuap == KUAP_NONE)
		return true;

	/*
	 * If faulting address doesn't match unlocked segment, change segment.
	 * In case of unaligned store crossing two segments, emulate store.
	 */
	if ((kuap ^ address) & 0xf0000000) {
		if (!(kuap & 0x0fffffff) && address > kuap - 4 && fix_alignment(regs)) {
			regs_add_return_ip(regs, 4);
			emulate_single_step(regs);
		} else {
			regs->kuap = address;
		}
	}

	return false;
}

#endif /* CONFIG_PPC_KUAP */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_BOOK3S_32_KUP_H */
