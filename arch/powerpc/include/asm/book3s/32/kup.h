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
extern struct static_key_false disable_kuep_key;

static __always_inline bool kuap_is_disabled(void)
{
	return !IS_ENABLED(CONFIG_PPC_KUAP) || static_branch_unlikely(&disable_kuap_key);
}

static __always_inline bool kuep_is_disabled(void)
{
	return !IS_ENABLED(CONFIG_PPC_KUEP) || static_branch_unlikely(&disable_kuep_key);
}

static inline void kuep_lock(void)
{
	if (kuep_is_disabled())
		return;

	update_user_segments(mfsr(0) | SR_NX);
	/*
	 * This isync() shouldn't be necessary as the kernel is not excepted to
	 * run any instruction in userspace soon after the update of segments,
	 * but hash based cores (at least G3) seem to exhibit a random
	 * behaviour when the 'isync' is not there. 603 cores don't have this
	 * behaviour so don't do the 'isync' as it saves several CPU cycles.
	 */
	if (mmu_has_feature(MMU_FTR_HPTE_TABLE))
		isync();	/* Context sync required after mtsr() */
}

static inline void kuep_unlock(void)
{
	if (kuep_is_disabled())
		return;

	update_user_segments(mfsr(0) & ~SR_NX);
	/*
	 * This isync() shouldn't be necessary as a 'rfi' will soon be executed
	 * to return to userspace, but hash based cores (at least G3) seem to
	 * exhibit a random behaviour when the 'isync' is not there. 603 cores
	 * don't have this behaviour so don't do the 'isync' as it saves several
	 * CPU cycles.
	 */
	if (mmu_has_feature(MMU_FTR_HPTE_TABLE))
		isync();	/* Context sync required after mtsr() */
}

#ifdef CONFIG_PPC_KUAP

#include <linux/sched.h>

#define KUAP_NONE	(~0UL)
#define KUAP_ALL	(~1UL)

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

static inline void kuap_lock(unsigned long addr, bool ool)
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

static inline void kuap_save_and_lock(struct pt_regs *regs)
{
	unsigned long kuap = current->thread.kuap;

	if (kuap_is_disabled())
		return;

	regs->kuap = kuap;
	if (unlikely(kuap == KUAP_NONE))
		return;

	current->thread.kuap = KUAP_NONE;
	kuap_lock(kuap, false);
}

static inline void kuap_user_restore(struct pt_regs *regs)
{
}

static inline void kuap_kernel_restore(struct pt_regs *regs, unsigned long kuap)
{
	if (kuap_is_disabled())
		return;

	if (unlikely(kuap != KUAP_NONE)) {
		current->thread.kuap = KUAP_NONE;
		kuap_lock(kuap, false);
	}

	if (likely(regs->kuap == KUAP_NONE))
		return;

	current->thread.kuap = regs->kuap;

	kuap_unlock(regs->kuap, false);
}

static inline unsigned long kuap_get_and_assert_locked(void)
{
	unsigned long kuap = current->thread.kuap;

	if (kuap_is_disabled())
		return KUAP_NONE;

	WARN_ON_ONCE(IS_ENABLED(CONFIG_PPC_KUAP_DEBUG) && kuap != KUAP_NONE);

	return kuap;
}

static inline void kuap_assert_locked(void)
{
	kuap_get_and_assert_locked();
}

static __always_inline void allow_user_access(void __user *to, const void __user *from,
					      u32 size, unsigned long dir)
{
	if (kuap_is_disabled())
		return;

	BUILD_BUG_ON(!__builtin_constant_p(dir));

	if (!(dir & KUAP_WRITE))
		return;

	current->thread.kuap = (__force u32)to;
	kuap_unlock_one((__force u32)to);
}

static __always_inline void prevent_user_access(unsigned long dir)
{
	u32 kuap = current->thread.kuap;

	if (kuap_is_disabled())
		return;

	BUILD_BUG_ON(!__builtin_constant_p(dir));

	if (!(dir & KUAP_WRITE))
		return;

	current->thread.kuap = KUAP_NONE;
	kuap_lock(kuap, true);
}

static inline unsigned long prevent_user_access_return(void)
{
	unsigned long flags = current->thread.kuap;

	if (kuap_is_disabled())
		return KUAP_NONE;

	if (flags != KUAP_NONE) {
		current->thread.kuap = KUAP_NONE;
		kuap_lock(flags, true);
	}

	return flags;
}

static inline void restore_user_access(unsigned long flags)
{
	if (kuap_is_disabled())
		return;

	if (flags != KUAP_NONE) {
		current->thread.kuap = flags;
		kuap_unlock(flags, true);
	}
}

static inline bool
bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	unsigned long kuap = regs->kuap;

	if (kuap_is_disabled())
		return false;

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
