/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_32_KUP_H
#define _ASM_POWERPC_BOOK3S_32_KUP_H

#include <asm/bug.h>
#include <asm/book3s/32/mmu-hash.h>

#ifndef __ASSEMBLY__

#ifdef CONFIG_PPC_KUAP

#include <linux/sched.h>

static inline void kuap_update_sr(u32 sr, u32 addr, u32 end)
{
	addr &= 0xf0000000;	/* align addr to start of segment */
	barrier();	/* make sure thread.kuap is updated before playing with SRs */
	while (addr < end) {
		mtsr(sr, addr);
		sr += 0x111;		/* next VSID */
		sr &= 0xf0ffffff;	/* clear VSID overflow */
		addr += 0x10000000;	/* address of next segment */
	}
	isync();	/* Context sync required after mtsr() */
}

static inline void kuap_save_and_lock(struct pt_regs *regs)
{
	unsigned long kuap = current->thread.kuap;
	u32 addr = kuap & 0xf0000000;
	u32 end = kuap << 28;

	regs->kuap = kuap;
	if (unlikely(!kuap))
		return;

	current->thread.kuap = 0;
	kuap_update_sr(mfsr(addr) | SR_KS, addr, end);	/* Set Ks */
}

static inline void kuap_user_restore(struct pt_regs *regs)
{
}

static inline void kuap_kernel_restore(struct pt_regs *regs, unsigned long kuap)
{
	u32 addr = regs->kuap & 0xf0000000;
	u32 end = regs->kuap << 28;

	current->thread.kuap = regs->kuap;

	if (unlikely(regs->kuap == kuap))
		return;

	kuap_update_sr(mfsr(addr) & ~SR_KS, addr, end);	/* Clear Ks */
}

static inline unsigned long kuap_get_and_assert_locked(void)
{
	unsigned long kuap = current->thread.kuap;

	WARN_ON_ONCE(IS_ENABLED(CONFIG_PPC_KUAP_DEBUG) && kuap != 0);

	return kuap;
}

static inline void kuap_assert_locked(void)
{
	kuap_get_and_assert_locked();
}

static __always_inline void allow_user_access(void __user *to, const void __user *from,
					      u32 size, unsigned long dir)
{
	u32 addr, end;

	BUILD_BUG_ON(!__builtin_constant_p(dir));
	BUILD_BUG_ON(dir & ~KUAP_READ_WRITE);

	if (!(dir & KUAP_WRITE))
		return;

	addr = (__force u32)to;

	if (unlikely(addr >= TASK_SIZE || !size))
		return;

	end = min(addr + size, TASK_SIZE);

	current->thread.kuap = (addr & 0xf0000000) | ((((end - 1) >> 28) + 1) & 0xf);
	kuap_update_sr(mfsr(addr) & ~SR_KS, addr, end);	/* Clear Ks */
}

static __always_inline void prevent_user_access(void __user *to, const void __user *from,
						u32 size, unsigned long dir)
{
	u32 addr, end;

	BUILD_BUG_ON(!__builtin_constant_p(dir));

	if (dir & KUAP_CURRENT_WRITE) {
		u32 kuap = current->thread.kuap;

		if (unlikely(!kuap))
			return;

		addr = kuap & 0xf0000000;
		end = kuap << 28;
	} else if (dir & KUAP_WRITE) {
		addr = (__force u32)to;
		end = min(addr + size, TASK_SIZE);

		if (unlikely(addr >= TASK_SIZE || !size))
			return;
	} else {
		return;
	}

	current->thread.kuap = 0;
	kuap_update_sr(mfsr(addr) | SR_KS, addr, end);	/* set Ks */
}

static inline unsigned long prevent_user_access_return(void)
{
	unsigned long flags = current->thread.kuap;
	unsigned long addr = flags & 0xf0000000;
	unsigned long end = flags << 28;
	void __user *to = (__force void __user *)addr;

	if (flags)
		prevent_user_access(to, to, end - addr, KUAP_READ_WRITE);

	return flags;
}

static inline void restore_user_access(unsigned long flags)
{
	unsigned long addr = flags & 0xf0000000;
	unsigned long end = flags << 28;
	void __user *to = (__force void __user *)addr;

	if (flags)
		allow_user_access(to, to, end - addr, KUAP_READ_WRITE);
}

static inline bool
bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	unsigned long begin = regs->kuap & 0xf0000000;
	unsigned long end = regs->kuap << 28;

	return is_write && (address < begin || address >= end);
}

#endif /* CONFIG_PPC_KUAP */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_BOOK3S_32_KUP_H */
