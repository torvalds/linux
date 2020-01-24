/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_32_KUP_H
#define _ASM_POWERPC_BOOK3S_32_KUP_H

#include <asm/book3s/32/mmu-hash.h>

#ifdef __ASSEMBLY__

.macro kuep_update_sr	gpr1, gpr2		/* NEVER use r0 as gpr2 due to addis */
101:	mtsrin	\gpr1, \gpr2
	addi	\gpr1, \gpr1, 0x111		/* next VSID */
	rlwinm	\gpr1, \gpr1, 0, 0xf0ffffff	/* clear VSID overflow */
	addis	\gpr2, \gpr2, 0x1000		/* address of next segment */
	bdnz	101b
	isync
.endm

.macro kuep_lock	gpr1, gpr2
#ifdef CONFIG_PPC_KUEP
	li	\gpr1, NUM_USER_SEGMENTS
	li	\gpr2, 0
	mtctr	\gpr1
	mfsrin	\gpr1, \gpr2
	oris	\gpr1, \gpr1, SR_NX@h		/* set Nx */
	kuep_update_sr \gpr1, \gpr2
#endif
.endm

.macro kuep_unlock	gpr1, gpr2
#ifdef CONFIG_PPC_KUEP
	li	\gpr1, NUM_USER_SEGMENTS
	li	\gpr2, 0
	mtctr	\gpr1
	mfsrin	\gpr1, \gpr2
	rlwinm	\gpr1, \gpr1, 0, ~SR_NX		/* Clear Nx */
	kuep_update_sr \gpr1, \gpr2
#endif
.endm

#ifdef CONFIG_PPC_KUAP

.macro kuap_update_sr	gpr1, gpr2, gpr3	/* NEVER use r0 as gpr2 due to addis */
101:	mtsrin	\gpr1, \gpr2
	addi	\gpr1, \gpr1, 0x111		/* next VSID */
	rlwinm	\gpr1, \gpr1, 0, 0xf0ffffff	/* clear VSID overflow */
	addis	\gpr2, \gpr2, 0x1000		/* address of next segment */
	cmplw	\gpr2, \gpr3
	blt-	101b
	isync
.endm

.macro kuap_save_and_lock	sp, thread, gpr1, gpr2, gpr3
	lwz	\gpr2, KUAP(\thread)
	rlwinm.	\gpr3, \gpr2, 28, 0xf0000000
	stw	\gpr2, STACK_REGS_KUAP(\sp)
	beq+	102f
	li	\gpr1, 0
	stw	\gpr1, KUAP(\thread)
	mfsrin	\gpr1, \gpr2
	oris	\gpr1, \gpr1, SR_KS@h	/* set Ks */
	kuap_update_sr	\gpr1, \gpr2, \gpr3
102:
.endm

.macro kuap_restore	sp, current, gpr1, gpr2, gpr3
	lwz	\gpr2, STACK_REGS_KUAP(\sp)
	rlwinm.	\gpr3, \gpr2, 28, 0xf0000000
	stw	\gpr2, THREAD + KUAP(\current)
	beq+	102f
	mfsrin	\gpr1, \gpr2
	rlwinm	\gpr1, \gpr1, 0, ~SR_KS	/* Clear Ks */
	kuap_update_sr	\gpr1, \gpr2, \gpr3
102:
.endm

.macro kuap_check	current, gpr
#ifdef CONFIG_PPC_KUAP_DEBUG
	lwz	\gpr2, KUAP(thread)
999:	twnei	\gpr, 0
	EMIT_BUG_ENTRY 999b, __FILE__, __LINE__, (BUGFLAG_WARNING | BUGFLAG_ONCE)
#endif
.endm

#endif /* CONFIG_PPC_KUAP */

#else /* !__ASSEMBLY__ */

#ifdef CONFIG_PPC_KUAP

#include <linux/sched.h>

static inline void kuap_update_sr(u32 sr, u32 addr, u32 end)
{
	addr &= 0xf0000000;	/* align addr to start of segment */
	barrier();	/* make sure thread.kuap is updated before playing with SRs */
	while (addr < end) {
		mtsrin(sr, addr);
		sr += 0x111;		/* next VSID */
		sr &= 0xf0ffffff;	/* clear VSID overflow */
		addr += 0x10000000;	/* address of next segment */
	}
	isync();	/* Context sync required after mtsrin() */
}

static inline void allow_user_access(void __user *to, const void __user *from, u32 size)
{
	u32 addr, end;

	if (__builtin_constant_p(to) && to == NULL)
		return;

	addr = (__force u32)to;

	if (!addr || addr >= TASK_SIZE || !size)
		return;

	end = min(addr + size, TASK_SIZE);
	current->thread.kuap = (addr & 0xf0000000) | ((((end - 1) >> 28) + 1) & 0xf);
	kuap_update_sr(mfsrin(addr) & ~SR_KS, addr, end);	/* Clear Ks */
}

static inline void prevent_user_access(void __user *to, const void __user *from, u32 size)
{
	u32 addr = (__force u32)to;
	u32 end = min(addr + size, TASK_SIZE);

	if (!addr || addr >= TASK_SIZE || !size)
		return;

	current->thread.kuap = 0;
	kuap_update_sr(mfsrin(addr) | SR_KS, addr, end);	/* set Ks */
}

static inline bool
bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	unsigned long begin = regs->kuap & 0xf0000000;
	unsigned long end = regs->kuap << 28;

	if (!is_write)
		return false;

	return WARN(address < begin || address >= end,
		    "Bug: write fault blocked by segment registers !");
}

#endif /* CONFIG_PPC_KUAP */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_BOOK3S_32_KUP_H */
