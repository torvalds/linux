/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_32_KUP_H
#define _ASM_POWERPC_BOOK3S_32_KUP_H

#include <asm/bug.h>
#include <asm/book3s/32/mmu-hash.h>
#include <asm/mmu.h>
#include <asm/synch.h>

#ifndef __ASSEMBLY__

#include <linux/jump_label.h>

extern struct static_key_false disable_kuap_key;

static __always_inline bool kuep_is_disabled(void)
{
	return !IS_ENABLED(CONFIG_PPC_KUEP);
}

#ifdef CONFIG_PPC_KUAP

#include <linux/sched.h>

#define KUAP_NONE	(~0UL)
#define KUAP_ALL	(~1UL)

static __always_inline bool kuap_is_disabled(void)
{
	return static_branch_unlikely(&disable_kuap_key);
}

static inline void kuap_lock_one(unsigned long addr)
{
	mtsr(mfsr(addr) | SR_KS, addr);
	isync();	/* Context sync required after mtsr() */
}

static inline void kuap_unlock_one(unsigned long addr)
{
	mtsr(mfsr(addr) & ~SR_KS, addr);
	isync();	/* Context sync required after mtsr() */
}

static inline void kuap_lock_all(void)
{
	update_user_segments(mfsr(0) | SR_KS);
	isync();	/* Context sync required after mtsr() */
}

static inline void kuap_unlock_all(void)
{
	update_user_segments(mfsr(0) & ~SR_KS);
	isync();	/* Context sync required after mtsr() */
}

void kuap_lock_all_ool(void);
void kuap_unlock_all_ool(void);

static inline void kuap_lock_addr(unsigned long addr, bool ool)
{
	if (likely(addr != KUAP_ALL))
		kuap_lock_one(addr);
	else if (!ool)
		kuap_lock_all();
	else
		kuap_lock_all_ool();
}

static inline void kuap_unlock(unsigned long addr, bool ool)
{
	if (likely(addr != KUAP_ALL))
		kuap_unlock_one(addr);
	else if (!ool)
		kuap_unlock_all();
	else
		kuap_unlock_all_ool();
}

static inline void __kuap_lock(void)
{
}

static inline void __kuap_save_and_lock(struct pt_regs *regs)
{
	unsigned long kuap = current->thread.kuap;

	regs->kuap = kuap;
	if (unlikely(kuap == KUAP_NONE))
		return;

	current->thread.kuap = KUAP_NONE;
	kuap_lock_addr(kuap, false);
}

static inline void kuap_user_restore(struct pt_regs *regs)
{
}

static inline void __kuap_kernel_restore(struct pt_regs *regs, unsigned long kuap)
{
	if (unlikely(kuap != KUAP_NONE)) {
		current->thread.kuap = KUAP_NONE;
		kuap_lock_addr(kuap, false);
	}

	if (likely(regs->kuap == KUAP_NONE))
		return;

	current->thread.kuap = regs->kuap;

	kuap_unlock(regs->kuap, false);
}

static inline unsigned long __kuap_get_and_assert_locked(void)
{
	unsigned long kuap = current->thread.kuap;

	WARN_ON_ONCE(IS_ENABLED(CONFIG_PPC_KUAP_DEBUG) && kuap != KUAP_NONE);

	return kuap;
}

static __always_inline void __allow_user_access(void __user *to, const void __user *from,
						u32 size, unsigned long dir)
{
	BUILD_BUG_ON(!__builtin_constant_p(dir));

	if (!(dir & KUAP_WRITE))
		return;

	current->thread.kuap = (__force u32)to;
	kuap_unlock_one((__force u32)to);
}

static __always_inline void __prevent_user_access(unsigned long dir)
{
	u32 kuap = current->thread.kuap;

	BUILD_BUG_ON(!__builtin_constant_p(dir));

	if (!(dir & KUAP_WRITE))
		return;

	current->thread.kuap = KUAP_NONE;
	kuap_lock_addr(kuap, true);
}

static inline unsigned long __prevent_user_access_return(void)
{
	unsigned long flags = current->thread.kuap;

	if (flags != KUAP_NONE) {
		current->thread.kuap = KUAP_NONE;
		kuap_lock_addr(flags, true);
	}

	return flags;
}

static inline void __restore_user_access(unsigned long flags)
{
	if (flags != KUAP_NONE) {
		current->thread.kuap = flags;
		kuap_unlock(flags, true);
	}
}

static inline bool
__bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	unsigned long kuap = regs->kuap;

	if (!is_write || kuap == KUAP_ALL)
		return false;
	if (kuap == KUAP_NONE)
		return true;

	/* If faulting address doesn't match unlocked segment, unlock all */
	if ((kuap ^ address) & 0xf0000000)
		regs->kuap = KUAP_ALL;

	return false;
}

#endif /* CONFIG_PPC_KUAP */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_BOOK3S_32_KUP_H */
