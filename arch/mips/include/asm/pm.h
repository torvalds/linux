/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2014 Imagination Technologies Ltd
 *
 * PM helper macros for CPU power off (e.g. Suspend-to-RAM).
 */

#ifndef __ASM_PM_H
#define __ASM_PM_H

#ifdef __ASSEMBLY__

#include <asm/asm-offsets.h>
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>

/* Save CPU state to stack for suspend to RAM */
.macro SUSPEND_SAVE_REGS
	PTR_SUBU	sp, PT_SIZE
	/* Call preserved GPRs */
	LONG_S	$16, PT_R16(sp)
	LONG_S	$17, PT_R17(sp)
	LONG_S	$18, PT_R18(sp)
	LONG_S	$19, PT_R19(sp)
	LONG_S	$20, PT_R20(sp)
	LONG_S	$21, PT_R21(sp)
	LONG_S	$22, PT_R22(sp)
	LONG_S	$23, PT_R23(sp)
	LONG_S	$28, PT_R28(sp)
	LONG_S	$30, PT_R30(sp)
	LONG_S	$31, PT_R31(sp)
	/* A couple of CP0 registers with space in pt_regs */
	mfc0	k0, CP0_STATUS
	LONG_S	k0, PT_STATUS(sp)
.endm

/* Restore CPU state from stack after resume from RAM */
.macro RESUME_RESTORE_REGS_RETURN
	.set	push
	.set	noreorder
	/* A couple of CP0 registers with space in pt_regs */
	LONG_L	k0, PT_STATUS(sp)
	mtc0	k0, CP0_STATUS
	/* Call preserved GPRs */
	LONG_L	$16, PT_R16(sp)
	LONG_L	$17, PT_R17(sp)
	LONG_L	$18, PT_R18(sp)
	LONG_L	$19, PT_R19(sp)
	LONG_L	$20, PT_R20(sp)
	LONG_L	$21, PT_R21(sp)
	LONG_L	$22, PT_R22(sp)
	LONG_L	$23, PT_R23(sp)
	LONG_L	$28, PT_R28(sp)
	LONG_L	$30, PT_R30(sp)
	LONG_L	$31, PT_R31(sp)
	/* Pop and return */
	jr	ra
	 PTR_ADDIU	sp, PT_SIZE
	.set	pop
.endm

/* Get address of static suspend state into t1 */
.macro LA_STATIC_SUSPEND
	PTR_LA	t1, mips_static_suspend_state
.endm

/* Save important CPU state for early restoration to global data */
.macro SUSPEND_SAVE_STATIC
#ifdef CONFIG_EVA
	/*
	 * Segment configuration is saved in global data where it can be easily
	 * reloaded without depending on the segment configuration.
	 */
	mfc0	k0, CP0_SEGCTL0
	LONG_S	k0, SSS_SEGCTL0(t1)
	mfc0	k0, CP0_SEGCTL1
	LONG_S	k0, SSS_SEGCTL1(t1)
	mfc0	k0, CP0_SEGCTL2
	LONG_S	k0, SSS_SEGCTL2(t1)
#endif
	/* save stack pointer (pointing to GPRs) */
	LONG_S	sp, SSS_SP(t1)
.endm

/* Restore important CPU state early from global data */
.macro RESUME_RESTORE_STATIC
#ifdef CONFIG_EVA
	/*
	 * Segment configuration must be restored prior to any access to
	 * allocated memory, as it may reside outside of the legacy kernel
	 * segments.
	 */
	LONG_L	k0, SSS_SEGCTL0(t1)
	mtc0	k0, CP0_SEGCTL0
	LONG_L	k0, SSS_SEGCTL1(t1)
	mtc0	k0, CP0_SEGCTL1
	LONG_L	k0, SSS_SEGCTL2(t1)
	mtc0	k0, CP0_SEGCTL2
	tlbw_use_hazard
#endif
	/* restore stack pointer (pointing to GPRs) */
	LONG_L	sp, SSS_SP(t1)
.endm

/* flush caches to make sure context has reached memory */
.macro SUSPEND_CACHE_FLUSH
	.extern	__flush_cache_all
	.set	push
	.set	noreorder
	PTR_LA	t1, __flush_cache_all
	LONG_L	t0, 0(t1)
	jalr	t0
	 nop
	.set	pop
 .endm

/* Save suspend state and flush data caches to RAM */
.macro SUSPEND_SAVE
	SUSPEND_SAVE_REGS
	LA_STATIC_SUSPEND
	SUSPEND_SAVE_STATIC
	SUSPEND_CACHE_FLUSH
.endm

/* Restore saved state after resume from RAM and return */
.macro RESUME_RESTORE_RETURN
	LA_STATIC_SUSPEND
	RESUME_RESTORE_STATIC
	RESUME_RESTORE_REGS_RETURN
.endm

#else /* __ASSEMBLY__ */

/**
 * struct mips_static_suspend_state - Core saved CPU state across S2R.
 * @segctl:	CP0 Segment control registers.
 * @sp:		Stack frame where GP register context is saved.
 *
 * This structure contains minimal CPU state that must be saved in static kernel
 * data in order to be able to restore the rest of the state. This includes
 * segmentation configuration in the case of EVA being enabled, as they must be
 * restored prior to any kmalloc'd memory being referenced (even the stack
 * pointer).
 */
struct mips_static_suspend_state {
#ifdef CONFIG_EVA
	unsigned long segctl[3];
#endif
	unsigned long sp;
};

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_PM_HELPERS_H */
