/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/mm/proc-v7.S
 *
 *  Copyright (C) 2001 Deep Blue Solutions Ltd.
 *
 *  This is the "shell" of the ARMv7 processor support.
 */
#include <linux/arm-smccc.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/pgtable.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/hwcap.h>
#include <asm/pgtable-hwdef.h>
#include <asm/memory.h>

#include "proc-macros.S"

#ifdef CONFIG_ARM_LPAE
#include "proc-v7-3level.S"
#else
#include "proc-v7-2level.S"
#endif

ENTRY(cpu_v7_proc_init)
	ret	lr
ENDPROC(cpu_v7_proc_init)

ENTRY(cpu_v7_proc_fin)
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1000			@ ...i............
	bic	r0, r0, #0x0006			@ .............ca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	ret	lr
ENDPROC(cpu_v7_proc_fin)

/*
 *	cpu_v7_reset(loc, hyp)
 *
 *	Perform a soft reset of the system.  Put the CPU into the
 *	same state as it would be if it had been reset, and branch
 *	to what would be the reset vector.
 *
 *	- loc   - location to jump to for soft reset
 *	- hyp   - indicate if restart occurs in HYP mode
 *
 *	This code must be executed using a flat identity mapping with
 *      caches disabled.
 */
	.align	5
	.pushsection	.idmap.text, "ax"
ENTRY(cpu_v7_reset)
	mrc	p15, 0, r2, c1, c0, 0		@ ctrl register
	bic	r2, r2, #0x1			@ ...............m
 THUMB(	bic	r2, r2, #1 << 30 )		@ SCTLR.TE (Thumb exceptions)
	mcr	p15, 0, r2, c1, c0, 0		@ disable MMU
	isb
#ifdef CONFIG_ARM_VIRT_EXT
	teq	r1, #0
	bne	__hyp_soft_restart
#endif
	bx	r0
ENDPROC(cpu_v7_reset)
	.popsection

/*
 *	cpu_v7_do_idle()
 *
 *	Idle the processor (eg, wait for interrupt).
 *
 *	IRQs are already disabled.
 */
ENTRY(cpu_v7_do_idle)
	dsb					@ WFI may enter a low-power mode
	wfi
	ret	lr
ENDPROC(cpu_v7_do_idle)

ENTRY(cpu_v7_dcache_clean_area)
	ALT_SMP(W(nop))			@ MP extensions imply L1 PTW
	ALT_UP_B(1f)
	ret	lr
1:	dcache_line_size r2, r3
2:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, r2
	subs	r1, r1, r2
	bhi	2b
	dsb	ishst
	ret	lr
ENDPROC(cpu_v7_dcache_clean_area)

#ifdef CONFIG_ARM_PSCI
	.arch_extension sec
ENTRY(cpu_v7_smc_switch_mm)
	stmfd	sp!, {r0 - r3}
	movw	r0, #:lower16:ARM_SMCCC_ARCH_WORKAROUND_1
	movt	r0, #:upper16:ARM_SMCCC_ARCH_WORKAROUND_1
	smc	#0
	ldmfd	sp!, {r0 - r3}
	b	cpu_v7_switch_mm
ENDPROC(cpu_v7_smc_switch_mm)
	.arch_extension virt
ENTRY(cpu_v7_hvc_switch_mm)
	stmfd	sp!, {r0 - r3}
	movw	r0, #:lower16:ARM_SMCCC_ARCH_WORKAROUND_1
	movt	r0, #:upper16:ARM_SMCCC_ARCH_WORKAROUND_1
	hvc	#0
	ldmfd	sp!, {r0 - r3}
	b	cpu_v7_switch_mm
ENDPROC(cpu_v7_hvc_switch_mm)
#endif
ENTRY(cpu_v7_iciallu_switch_mm)
	mov	r3, #0
	mcr	p15, 0, r3, c7, c5, 0		@ ICIALLU
	b	cpu_v7_switch_mm
ENDPROC(cpu_v7_iciallu_switch_mm)
ENTRY(cpu_v7_bpiall_switch_mm)
	mov	r3, #0
	mcr	p15, 0, r3, c7, c5, 6		@ flush BTAC/BTB
	b	cpu_v7_switch_mm
ENDPROC(cpu_v7_bpiall_switch_mm)

	string	cpu_v7_name, "ARMv7 Processor"
	.align

/* Suspend/resume support: derived from arch/arm/mach-s5pv210/sleep.S */
.globl	cpu_v7_suspend_size
.equ	cpu_v7_suspend_size, 4 * 9
#ifdef CONFIG_ARM_CPU_SUSPEND
ENTRY(cpu_v7_do_suspend)
	stmfd	sp!, {r4 - r11, lr}
	mrc	p15, 0, r4, c13, c0, 0	@ FCSE/PID
	mrc	p15, 0, r5, c13, c0, 3	@ User r/o thread ID
	stmia	r0!, {r4 - r5}
#ifdef CONFIG_MMU
	mrc	p15, 0, r6, c3, c0, 0	@ Domain ID
#ifdef CONFIG_ARM_LPAE
	mrrc	p15, 1, r5, r7, c2	@ TTB 1
#else
	mrc	p15, 0, r7, c2, c0, 1	@ TTB 1
#endif
	mrc	p15, 0, r11, c2, c0, 2	@ TTB control register
#endif
	mrc	p15, 0, r8, c1, c0, 0	@ Control register
	mrc	p15, 0, r9, c1, c0, 1	@ Auxiliary control register
	mrc	p15, 0, r10, c1, c0, 2	@ Co-processor access control
	stmia	r0, {r5 - r11}
	ldmfd	sp!, {r4 - r11, pc}
ENDPROC(cpu_v7_do_suspend)

ENTRY(cpu_v7_do_resume)
	mov	ip, #0
	mcr	p15, 0, ip, c7, c5, 0	@ invalidate I cache
	mcr	p15, 0, ip, c13, c0, 1	@ set reserved context ID
	ldmia	r0!, {r4 - r5}
	mcr	p15, 0, r4, c13, c0, 0	@ FCSE/PID
	mcr	p15, 0, r5, c13, c0, 3	@ User r/o thread ID
	ldmia	r0, {r5 - r11}
#ifdef CONFIG_MMU
	mcr	p15, 0, ip, c8, c7, 0	@ invalidate TLBs
	mcr	p15, 0, r6, c3, c0, 0	@ Domain ID
#ifdef CONFIG_ARM_LPAE
	mcrr	p15, 0, r1, ip, c2	@ TTB 0
	mcrr	p15, 1, r5, r7, c2	@ TTB 1
#else
	ALT_SMP(orr	r1, r1, #TTB_FLAGS_SMP)
	ALT_UP(orr	r1, r1, #TTB_FLAGS_UP)
	mcr	p15, 0, r1, c2, c0, 0	@ TTB 0
	mcr	p15, 0, r7, c2, c0, 1	@ TTB 1
#endif
	mcr	p15, 0, r11, c2, c0, 2	@ TTB control register
	ldr	r4, =PRRR		@ PRRR
	ldr	r5, =NMRR		@ NMRR
	mcr	p15, 0, r4, c10, c2, 0	@ write PRRR
	mcr	p15, 0, r5, c10, c2, 1	@ write NMRR
#endif	/* CONFIG_MMU */
	mrc	p15, 0, r4, c1, c0, 1	@ Read Auxiliary control register
	teq	r4, r9			@ Is it already set?
	mcrne	p15, 0, r9, c1, c0, 1	@ No, so write it
	mcr	p15, 0, r10, c1, c0, 2	@ Co-processor access control
	isb
	dsb
	mov	r0, r8			@ control register
	b	cpu_resume_mmu
ENDPROC(cpu_v7_do_resume)
#endif

.globl	cpu_ca9mp_suspend_size
.equ	cpu_ca9mp_suspend_size, cpu_v7_suspend_size + 4 * 2
#ifdef CONFIG_ARM_CPU_SUSPEND
ENTRY(cpu_ca9mp_do_suspend)
	stmfd	sp!, {r4 - r5}
	mrc	p15, 0, r4, c15, c0, 1		@ Diagnostic register
	mrc	p15, 0, r5, c15, c0, 0		@ Power register
	stmia	r0!, {r4 - r5}
	ldmfd	sp!, {r4 - r5}
	b	cpu_v7_do_suspend
ENDPROC(cpu_ca9mp_do_suspend)

ENTRY(cpu_ca9mp_do_resume)
	ldmia	r0!, {r4 - r5}
	mrc	p15, 0, r10, c15, c0, 1		@ Read Diagnostic register
	teq	r4, r10				@ Already restored?
	mcrne	p15, 0, r4, c15, c0, 1		@ No, so restore it
	mrc	p15, 0, r10, c15, c0, 0		@ Read Power register
	teq	r5, r10				@ Already restored?
	mcrne	p15, 0, r5, c15, c0, 0		@ No, so restore it
	b	cpu_v7_do_resume
ENDPROC(cpu_ca9mp_do_resume)
#endif

#ifdef CONFIG_CPU_PJ4B
	globl_equ	cpu_pj4b_switch_mm,     cpu_v7_switch_mm
	globl_equ	cpu_pj4b_set_pte_ext,	cpu_v7_set_pte_ext
	globl_equ	cpu_pj4b_proc_init,	cpu_v7_proc_init
	globl_equ	cpu_pj4b_proc_fin, 	cpu_v7_proc_fin
	globl_equ	cpu_pj4b_reset,	   	cpu_v7_reset
#ifdef CONFIG_PJ4B_ERRATA_4742
ENTRY(cpu_pj4b_do_idle)
	dsb					@ WFI may enter a low-power mode
	wfi
	dsb					@barrier
	ret	lr
ENDPROC(cpu_pj4b_do_idle)
#else
	globl_equ	cpu_pj4b_do_idle,  	cpu_v7_do_idle
#endif
	globl_equ	cpu_pj4b_dcache_clean_area,	cpu_v7_dcache_clean_area
#ifdef CONFIG_ARM_CPU_SUSPEND
ENTRY(cpu_pj4b_do_suspend)
	stmfd	sp!, {r6 - r10}
	mrc	p15, 1, r6, c15, c1, 0  @ save CP15 - extra features
	mrc	p15, 1, r7, c15, c2, 0	@ save CP15 - Aux Func Modes Ctrl 0
	mrc	p15, 1, r8, c15, c1, 2	@ save CP15 - Aux Debug Modes Ctrl 2
	mrc	p15, 1, r9, c15, c1, 1  @ save CP15 - Aux Debug Modes Ctrl 1
	mrc	p15, 0, r10, c9, c14, 0  @ save CP15 - PMC
	stmia	r0!, {r6 - r10}
	ldmfd	sp!, {r6 - r10}
	b cpu_v7_do_suspend
ENDPROC(cpu_pj4b_do_suspend)

ENTRY(cpu_pj4b_do_resume)
	ldmia	r0!, {r6 - r10}
	mcr	p15, 1, r6, c15, c1, 0  @ restore CP15 - extra features
	mcr	p15, 1, r7, c15, c2, 0	@ restore CP15 - Aux Func Modes Ctrl 0
	mcr	p15, 1, r8, c15, c1, 2	@ restore CP15 - Aux Debug Modes Ctrl 2
	mcr	p15, 1, r9, c15, c1, 1  @ restore CP15 - Aux Debug Modes Ctrl 1
	mcr	p15, 0, r10, c9, c14, 0  @ restore CP15 - PMC
	b cpu_v7_do_resume
ENDPROC(cpu_pj4b_do_resume)
#endif
.globl	cpu_pj4b_suspend_size
.equ	cpu_pj4b_suspend_size, cpu_v7_suspend_size + 4 * 5

#endif

/*
 *	__v7_setup
 *
 *	Initialise TLB, Caches, and MMU state ready to switch the MMU
 *	on.  Return in r0 the new CP15 C1 control register setting.
 *
 *	r1, r2, r4, r5, r9, r13 must be preserved - r13 is not a stack
 *	r4: TTBR0 (low word)
 *	r5: TTBR0 (high word if LPAE)
 *	r8: TTBR1
 *	r9: Main ID register
 *
 *	This should be able to cover all ARMv7 cores.
 *
 *	It is assumed that:
 *	- cache type register is implemented
 */
__v7_ca5mp_setup:
__v7_ca9mp_setup:
__v7_cr7mp_setup:
__v7_cr8mp_setup:
	mov	r10, #(1 << 0)			@ Cache/TLB ops broadcasting
	b	1f
__v7_ca7mp_setup:
__v7_ca12mp_setup:
__v7_ca15mp_setup:
__v7_b15mp_setup:
__v7_ca17mp_setup:
	mov	r10, #0
1:	adr	r0, __v7_setup_stack_ptr
	ldr	r12, [r0]
	add	r12, r12, r0			@ the local stack
	stmia	r12, {r1-r6, lr}		@ v7_invalidate_l1 touches r0-r6
	bl      v7_invalidate_l1
	ldmia	r12, {r1-r6, lr}
#ifdef CONFIG_SMP
	orr	r10, r10, #(1 << 6)		@ Enable SMP/nAMP mode
	ALT_SMP(mrc	p15, 0, r0, c1, c0, 1)
	ALT_UP(mov	r0, r10)		@ fake it for UP
	orr	r10, r10, r0			@ Set required bits
	teq	r10, r0				@ Were they already set?
	mcrne	p15, 0, r10, c1, c0, 1		@ No, update register
#endif
	b	__v7_setup_cont

/*
 * Errata:
 *  r0, r10 available for use
 *  r1, r2, r4, r5, r9, r13: must be preserved
 *  r3: contains MIDR rX number in bits 23-20
 *  r6: contains MIDR rXpY as 8-bit XY number
 *  r9: MIDR
 */
__ca8_errata:
#if defined(CONFIG_ARM_ERRATA_430973) && !defined(CONFIG_ARCH_MULTIPLATFORM)
	teq	r3, #0x00100000			@ only present in r1p*
	mrceq	p15, 0, r0, c1, c0, 1		@ read aux control register
	orreq	r0, r0, #(1 << 6)		@ set IBE to 1
	mcreq	p15, 0, r0, c1, c0, 1		@ write aux control register
#endif
#ifdef CONFIG_ARM_ERRATA_458693
	teq	r6, #0x20			@ only present in r2p0
	mrceq	p15, 0, r0, c1, c0, 1		@ read aux control register
	orreq	r0, r0, #(1 << 5)		@ set L1NEON to 1
	orreq	r0, r0, #(1 << 9)		@ set PLDNOP to 1
	mcreq	p15, 0, r0, c1, c0, 1		@ write aux control register
#endif
#ifdef CONFIG_ARM_ERRATA_460075
	teq	r6, #0x20			@ only present in r2p0
	mrceq	p15, 1, r0, c9, c0, 2		@ read L2 cache aux ctrl register
	tsteq	r0, #1 << 22
	orreq	r0, r0, #(1 << 22)		@ set the Write Allocate disable bit
	mcreq	p15, 1, r0, c9, c0, 2		@ write the L2 cache aux ctrl register
#endif
	b	__errata_finish

__ca9_errata:
#ifdef CONFIG_ARM_ERRATA_742230
	cmp	r6, #0x22			@ only present up to r2p2
	mrcle	p15, 0, r0, c15, c0, 1		@ read diagnostic register
	orrle	r0, r0, #1 << 4			@ set bit #4
	mcrle	p15, 0, r0, c15, c0, 1		@ write diagnostic register
#endif
#ifdef CONFIG_ARM_ERRATA_742231
	teq	r6, #0x20			@ present in r2p0
	teqne	r6, #0x21			@ present in r2p1
	teqne	r6, #0x22			@ present in r2p2
	mrceq	p15, 0, r0, c15, c0, 1		@ read diagnostic register
	orreq	r0, r0, #1 << 12		@ set bit #12
	orreq	r0, r0, #1 << 22		@ set bit #22
	mcreq	p15, 0, r0, c15, c0, 1		@ write diagnostic register
#endif
#ifdef CONFIG_ARM_ERRATA_743622
	teq	r3, #0x00200000			@ only present in r2p*
	mrceq	p15, 0, r0, c15, c0, 1		@ read diagnostic register
	orreq	r0, r0, #1 << 6			@ set bit #6
	mcreq	p15, 0, r0, c15, c0, 1		@ write diagnostic register
#endif
#if defined(CONFIG_ARM_ERRATA_751472) && defined(CONFIG_SMP)
	ALT_SMP(cmp r6, #0x30)			@ present prior to r3p0
	ALT_UP_B(1f)
	mrclt	p15, 0, r0, c15, c0, 1		@ read diagnostic register
	orrlt	r0, r0, #1 << 11		@ set bit #11
	mcrlt	p15, 0, r0, c15, c0, 1		@ write diagnostic register
1:
#endif
	b	__errata_finish

__ca15_errata:
#ifdef CONFIG_ARM_ERRATA_773022
	cmp	r6, #0x4			@ only present up to r0p4
	mrcle	p15, 0, r0, c1, c0, 1		@ read aux control register
	orrle	r0, r0, #1 << 1			@ disable loop buffer
	mcrle	p15, 0, r0, c1, c0, 1		@ write aux control register
#endif
	b	__errata_finish

__ca12_errata:
#ifdef CONFIG_ARM_ERRATA_818325_852422
	mrc	p15, 0, r10, c15, c0, 1		@ read diagnostic register
	orr	r10, r10, #1 << 12		@ set bit #12
	mcr	p15, 0, r10, c15, c0, 1		@ write diagnostic register
#endif
#ifdef CONFIG_ARM_ERRATA_821420
	mrc	p15, 0, r10, c15, c0, 2		@ read internal feature reg
	orr	r10, r10, #1 << 1		@ set bit #1
	mcr	p15, 0, r10, c15, c0, 2		@ write internal feature reg
#endif
#ifdef CONFIG_ARM_ERRATA_825619
	mrc	p15, 0, r10, c15, c0, 1		@ read diagnostic register
	orr	r10, r10, #1 << 24		@ set bit #24
	mcr	p15, 0, r10, c15, c0, 1		@ write diagnostic register
#endif
#ifdef CONFIG_ARM_ERRATA_857271
	mrc	p15, 0, r10, c15, c0, 1		@ read diagnostic register
	orr	r10, r10, #3 << 10		@ set bits #10 and #11
	mcr	p15, 0, r10, c15, c0, 1		@ write diagnostic register
#endif
	b	__errata_finish

__ca17_errata:
#ifdef CONFIG_ARM_ERRATA_852421
	cmp	r6, #0x12			@ only present up to r1p2
	mrcle	p15, 0, r10, c15, c0, 1		@ read diagnostic register
	orrle	r10, r10, #1 << 24		@ set bit #24
	mcrle	p15, 0, r10, c15, c0, 1		@ write diagnostic register
#endif
#ifdef CONFIG_ARM_ERRATA_852423
	cmp	r6, #0x12			@ only present up to r1p2
	mrcle	p15, 0, r10, c15, c0, 1		@ read diagnostic register
	orrle	r10, r10, #1 << 12		@ set bit #12
	mcrle	p15, 0, r10, c15, c0, 1		@ write diagnostic register
#endif
#ifdef CONFIG_ARM_ERRATA_857272
	mrc	p15, 0, r10, c15, c0, 1		@ read diagnostic register
	orr	r10, r10, #3 << 10		@ set bits #10 and #11
	mcr	p15, 0, r10, c15, c0, 1		@ write diagnostic register
#endif
	b	__errata_finish

__v7_pj4b_setup:
#ifdef CONFIG_CPU_PJ4B

/* Auxiliary Debug Modes Control 1 Register */
#define PJ4B_STATIC_BP (1 << 2) /* Enable Static BP */
#define PJ4B_INTER_PARITY (1 << 8) /* Disable Internal Parity Handling */
#define PJ4B_CLEAN_LINE (1 << 16) /* Disable data transfer for clean line */

/* Auxiliary Debug Modes Control 2 Register */
#define PJ4B_FAST_LDR (1 << 23) /* Disable fast LDR */
#define PJ4B_SNOOP_DATA (1 << 25) /* Do not interleave write and snoop data */
#define PJ4B_CWF (1 << 27) /* Disable Critical Word First feature */
#define PJ4B_OUTSDNG_NC (1 << 29) /* Disable outstanding non cacheable rqst */
#define PJ4B_L1_REP_RR (1 << 30) /* L1 replacement - Strict round robin */
#define PJ4B_AUX_DBG_CTRL2 (PJ4B_SNOOP_DATA | PJ4B_CWF |\
			    PJ4B_OUTSDNG_NC | PJ4B_L1_REP_RR)

/* Auxiliary Functional Modes Control Register 0 */
#define PJ4B_SMP_CFB (1 << 1) /* Set SMP mode. Join the coherency fabric */
#define PJ4B_L1_PAR_CHK (1 << 2) /* Support L1 parity checking */
#define PJ4B_BROADCAST_CACHE (1 << 8) /* Broadcast Cache and TLB maintenance */

/* Auxiliary Debug Modes Control 0 Register */
#define PJ4B_WFI_WFE (1 << 22) /* WFI/WFE - serve the DVM and back to idle */

	/* Auxiliary Debug Modes Control 1 Register */
	mrc	p15, 1,	r0, c15, c1, 1
	orr     r0, r0, #PJ4B_CLEAN_LINE
	orr     r0, r0, #PJ4B_INTER_PARITY
	bic	r0, r0, #PJ4B_STATIC_BP
	mcr	p15, 1,	r0, c15, c1, 1

	/* Auxiliary Debug Modes Control 2 Register */
	mrc	p15, 1,	r0, c15, c1, 2
	bic	r0, r0, #PJ4B_FAST_LDR
	orr	r0, r0, #PJ4B_AUX_DBG_CTRL2
	mcr	p15, 1,	r0, c15, c1, 2

	/* Auxiliary Functional Modes Control Register 0 */
	mrc	p15, 1,	r0, c15, c2, 0
#ifdef CONFIG_SMP
	orr	r0, r0, #PJ4B_SMP_CFB
#endif
	orr	r0, r0, #PJ4B_L1_PAR_CHK
	orr	r0, r0, #PJ4B_BROADCAST_CACHE
	mcr	p15, 1,	r0, c15, c2, 0

	/* Auxiliary Debug Modes Control 0 Register */
	mrc	p15, 1,	r0, c15, c1, 0
	orr	r0, r0, #PJ4B_WFI_WFE
	mcr	p15, 1,	r0, c15, c1, 0

#endif /* CONFIG_CPU_PJ4B */

__v7_setup:
	adr	r0, __v7_setup_stack_ptr
	ldr	r12, [r0]
	add	r12, r12, r0			@ the local stack
	stmia	r12, {r1-r6, lr}		@ v7_invalidate_l1 touches r0-r6
	bl      v7_invalidate_l1
	ldmia	r12, {r1-r6, lr}

__v7_setup_cont:
	and	r0, r9, #0xff000000		@ ARM?
	teq	r0, #0x41000000
	bne	__errata_finish
	and	r3, r9, #0x00f00000		@ variant
	and	r6, r9, #0x0000000f		@ revision
	orr	r6, r6, r3, lsr #20-4		@ combine variant and revision
	ubfx	r0, r9, #4, #12			@ primary part number

	/* Cortex-A8 Errata */
	ldr	r10, =0x00000c08		@ Cortex-A8 primary part number
	teq	r0, r10
	beq	__ca8_errata

	/* Cortex-A9 Errata */
	ldr	r10, =0x00000c09		@ Cortex-A9 primary part number
	teq	r0, r10
	beq	__ca9_errata

	/* Cortex-A12 Errata */
	ldr	r10, =0x00000c0d		@ Cortex-A12 primary part number
	teq	r0, r10
	beq	__ca12_errata

	/* Cortex-A17 Errata */
	ldr	r10, =0x00000c0e		@ Cortex-A17 primary part number
	teq	r0, r10
	beq	__ca17_errata

	/* Cortex-A15 Errata */
	ldr	r10, =0x00000c0f		@ Cortex-A15 primary part number
	teq	r0, r10
	beq	__ca15_errata

__errata_finish:
	mov	r10, #0
	mcr	p15, 0, r10, c7, c5, 0		@ I+BTB cache invalidate
#ifdef CONFIG_MMU
	mcr	p15, 0, r10, c8, c7, 0		@ invalidate I + D TLBs
	v7_ttb_setup r10, r4, r5, r8, r3	@ TTBCR, TTBRx setup
	ldr	r3, =PRRR			@ PRRR
	ldr	r6, =NMRR			@ NMRR
	mcr	p15, 0, r3, c10, c2, 0		@ write PRRR
	mcr	p15, 0, r6, c10, c2, 1		@ write NMRR
#endif
	dsb					@ Complete invalidations
#ifndef CONFIG_ARM_THUMBEE
	mrc	p15, 0, r0, c0, c1, 0		@ read ID_PFR0 for ThumbEE
	and	r0, r0, #(0xf << 12)		@ ThumbEE enabled field
	teq	r0, #(1 << 12)			@ check if ThumbEE is present
	bne	1f
	mov	r3, #0
	mcr	p14, 6, r3, c1, c0, 0		@ Initialize TEEHBR to 0
	mrc	p14, 6, r0, c0, c0, 0		@ load TEECR
	orr	r0, r0, #1			@ set the 1st bit in order to
	mcr	p14, 6, r0, c0, c0, 0		@ stop userspace TEEHBR access
1:
#endif
	adr	r3, v7_crval
	ldmia	r3, {r3, r6}
 ARM_BE8(orr	r6, r6, #1 << 25)		@ big-endian page tables
#ifdef CONFIG_SWP_EMULATE
	orr     r3, r3, #(1 << 10)              @ set SW bit in "clear"
	bic     r6, r6, #(1 << 10)              @ clear it in "mmuset"
#endif
   	mrc	p15, 0, r0, c1, c0, 0		@ read control register
	bic	r0, r0, r3			@ clear bits them
	orr	r0, r0, r6			@ set them
 THUMB(	orr	r0, r0, #1 << 30	)	@ Thumb exceptions
	ret	lr				@ return to head.S:__ret

	.align	2
__v7_setup_stack_ptr:
	.word	PHYS_RELATIVE(__v7_setup_stack, .)
ENDPROC(__v7_setup)

	.bss
	.align	2
__v7_setup_stack:
	.space	4 * 7				@ 7 registers

	__INITDATA

	.weak cpu_v7_bugs_init

	@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
	define_processor_functions v7, dabort=v7_early_abort, pabort=v7_pabort, suspend=1, bugs=cpu_v7_bugs_init

#ifdef CONFIG_HARDEN_BRANCH_PREDICTOR
	@ generic v7 bpiall on context switch
	globl_equ	cpu_v7_bpiall_proc_init,	cpu_v7_proc_init
	globl_equ	cpu_v7_bpiall_proc_fin,		cpu_v7_proc_fin
	globl_equ	cpu_v7_bpiall_reset,		cpu_v7_reset
	globl_equ	cpu_v7_bpiall_do_idle,		cpu_v7_do_idle
	globl_equ	cpu_v7_bpiall_dcache_clean_area, cpu_v7_dcache_clean_area
	globl_equ	cpu_v7_bpiall_set_pte_ext,	cpu_v7_set_pte_ext
	globl_equ	cpu_v7_bpiall_suspend_size,	cpu_v7_suspend_size
#ifdef CONFIG_ARM_CPU_SUSPEND
	globl_equ	cpu_v7_bpiall_do_suspend,	cpu_v7_do_suspend
	globl_equ	cpu_v7_bpiall_do_resume,	cpu_v7_do_resume
#endif
	define_processor_functions v7_bpiall, dabort=v7_early_abort, pabort=v7_pabort, suspend=1, bugs=cpu_v7_bugs_init

#define HARDENED_BPIALL_PROCESSOR_FUNCTIONS v7_bpiall_processor_functions
#else
#define HARDENED_BPIALL_PROCESSOR_FUNCTIONS v7_processor_functions
#endif

#ifndef CONFIG_ARM_LPAE
	@ Cortex-A8 - always needs bpiall switch_mm implementation
	globl_equ	cpu_ca8_proc_init,	cpu_v7_proc_init
	globl_equ	cpu_ca8_proc_fin,	cpu_v7_proc_fin
	globl_equ	cpu_ca8_reset,		cpu_v7_reset
	globl_equ	cpu_ca8_do_idle,	cpu_v7_do_idle
	globl_equ	cpu_ca8_dcache_clean_area, cpu_v7_dcache_clean_area
	globl_equ	cpu_ca8_set_pte_ext,	cpu_v7_set_pte_ext
	globl_equ	cpu_ca8_switch_mm,	cpu_v7_bpiall_switch_mm
	globl_equ	cpu_ca8_suspend_size,	cpu_v7_suspend_size
#ifdef CONFIG_ARM_CPU_SUSPEND
	globl_equ	cpu_ca8_do_suspend,	cpu_v7_do_suspend
	globl_equ	cpu_ca8_do_resume,	cpu_v7_do_resume
#endif
	define_processor_functions ca8, dabort=v7_early_abort, pabort=v7_pabort, suspend=1, bugs=cpu_v7_ca8_ibe

	@ Cortex-A9 - needs more registers preserved across suspend/resume
	@ and bpiall switch_mm for hardening
	globl_equ	cpu_ca9mp_proc_init,	cpu_v7_proc_init
	globl_equ	cpu_ca9mp_proc_fin,	cpu_v7_proc_fin
	globl_equ	cpu_ca9mp_reset,	cpu_v7_reset
	globl_equ	cpu_ca9mp_do_idle,	cpu_v7_do_idle
	globl_equ	cpu_ca9mp_dcache_clean_area, cpu_v7_dcache_clean_area
#ifdef CONFIG_HARDEN_BRANCH_PREDICTOR
	globl_equ	cpu_ca9mp_switch_mm,	cpu_v7_bpiall_switch_mm
#else
	globl_equ	cpu_ca9mp_switch_mm,	cpu_v7_switch_mm
#endif
	globl_equ	cpu_ca9mp_set_pte_ext,	cpu_v7_set_pte_ext
	define_processor_functions ca9mp, dabort=v7_early_abort, pabort=v7_pabort, suspend=1, bugs=cpu_v7_bugs_init
#endif

	@ Cortex-A15 - needs iciallu switch_mm for hardening
	globl_equ	cpu_ca15_proc_init,	cpu_v7_proc_init
	globl_equ	cpu_ca15_proc_fin,	cpu_v7_proc_fin
	globl_equ	cpu_ca15_reset,		cpu_v7_reset
	globl_equ	cpu_ca15_do_idle,	cpu_v7_do_idle
	globl_equ	cpu_ca15_dcache_clean_area, cpu_v7_dcache_clean_area
#ifdef CONFIG_HARDEN_BRANCH_PREDICTOR
	globl_equ	cpu_ca15_switch_mm,	cpu_v7_iciallu_switch_mm
#else
	globl_equ	cpu_ca15_switch_mm,	cpu_v7_switch_mm
#endif
	globl_equ	cpu_ca15_set_pte_ext,	cpu_v7_set_pte_ext
	globl_equ	cpu_ca15_suspend_size,	cpu_v7_suspend_size
	globl_equ	cpu_ca15_do_suspend,	cpu_v7_do_suspend
	globl_equ	cpu_ca15_do_resume,	cpu_v7_do_resume
	define_processor_functions ca15, dabort=v7_early_abort, pabort=v7_pabort, suspend=1, bugs=cpu_v7_ca15_ibe
#ifdef CONFIG_CPU_PJ4B
	define_processor_functions pj4b, dabort=v7_early_abort, pabort=v7_pabort, suspend=1
#endif

	.section ".rodata"

	string	cpu_arch_name, "armv7"
	string	cpu_elf_name, "v7"
	.align

	.section ".proc.info.init", "a"

	/*
	 * Standard v7 proc info content
	 */
.macro __v7_proc name, initfunc, mm_mmuflags = 0, io_mmuflags = 0, hwcaps = 0, proc_fns = v7_processor_functions, cache_fns = v7_cache_fns
	ALT_SMP(.long	PMD_TYPE_SECT | PMD_SECT_AP_WRITE | PMD_SECT_AP_READ | \
			PMD_SECT_AF | PMD_FLAGS_SMP | \mm_mmuflags)
	ALT_UP(.long	PMD_TYPE_SECT | PMD_SECT_AP_WRITE | PMD_SECT_AP_READ | \
			PMD_SECT_AF | PMD_FLAGS_UP | \mm_mmuflags)
	.long	PMD_TYPE_SECT | PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ | PMD_SECT_AF | \io_mmuflags
	initfn	\initfunc, \name
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF | HWCAP_THUMB | HWCAP_FAST_MULT | \
		HWCAP_EDSP | HWCAP_TLS | \hwcaps
	.long	cpu_v7_name
	.long	\proc_fns
	.long	v7wbi_tlb_fns
	.long	v6_user_fns
	.long	\cache_fns
.endm

#ifndef CONFIG_ARM_LPAE
	/*
	 * ARM Ltd. Cortex A5 processor.
	 */
	.type   __v7_ca5mp_proc_info, #object
__v7_ca5mp_proc_info:
	.long	0x410fc050
	.long	0xff0ffff0
	__v7_proc __v7_ca5mp_proc_info, __v7_ca5mp_setup
	.size	__v7_ca5mp_proc_info, . - __v7_ca5mp_proc_info

	/*
	 * ARM Ltd. Cortex A9 processor.
	 */
	.type   __v7_ca9mp_proc_info, #object
__v7_ca9mp_proc_info:
	.long	0x410fc090
	.long	0xff0ffff0
	__v7_proc __v7_ca9mp_proc_info, __v7_ca9mp_setup, proc_fns = ca9mp_processor_functions
	.size	__v7_ca9mp_proc_info, . - __v7_ca9mp_proc_info

	/*
	 * ARM Ltd. Cortex A8 processor.
	 */
	.type	__v7_ca8_proc_info, #object
__v7_ca8_proc_info:
	.long	0x410fc080
	.long	0xff0ffff0
	__v7_proc __v7_ca8_proc_info, __v7_setup, proc_fns = ca8_processor_functions
	.size	__v7_ca8_proc_info, . - __v7_ca8_proc_info

#endif	/* CONFIG_ARM_LPAE */

	/*
	 * Marvell PJ4B processor.
	 */
#ifdef CONFIG_CPU_PJ4B
	.type   __v7_pj4b_proc_info, #object
__v7_pj4b_proc_info:
	.long	0x560f5800
	.long	0xff0fff00
	__v7_proc __v7_pj4b_proc_info, __v7_pj4b_setup, proc_fns = pj4b_processor_functions
	.size	__v7_pj4b_proc_info, . - __v7_pj4b_proc_info
#endif

	/*
	 * ARM Ltd. Cortex R7 processor.
	 */
	.type	__v7_cr7mp_proc_info, #object
__v7_cr7mp_proc_info:
	.long	0x410fc170
	.long	0xff0ffff0
	__v7_proc __v7_cr7mp_proc_info, __v7_cr7mp_setup
	.size	__v7_cr7mp_proc_info, . - __v7_cr7mp_proc_info

	/*
	 * ARM Ltd. Cortex R8 processor.
	 */
	.type	__v7_cr8mp_proc_info, #object
__v7_cr8mp_proc_info:
	.long	0x410fc180
	.long	0xff0ffff0
	__v7_proc __v7_cr8mp_proc_info, __v7_cr8mp_setup
	.size	__v7_cr8mp_proc_info, . - __v7_cr8mp_proc_info

	/*
	 * ARM Ltd. Cortex A7 processor.
	 */
	.type	__v7_ca7mp_proc_info, #object
__v7_ca7mp_proc_info:
	.long	0x410fc070
	.long	0xff0ffff0
	__v7_proc __v7_ca7mp_proc_info, __v7_ca7mp_setup
	.size	__v7_ca7mp_proc_info, . - __v7_ca7mp_proc_info

	/*
	 * ARM Ltd. Cortex A12 processor.
	 */
	.type	__v7_ca12mp_proc_info, #object
__v7_ca12mp_proc_info:
	.long	0x410fc0d0
	.long	0xff0ffff0
	__v7_proc __v7_ca12mp_proc_info, __v7_ca12mp_setup, proc_fns = HARDENED_BPIALL_PROCESSOR_FUNCTIONS
	.size	__v7_ca12mp_proc_info, . - __v7_ca12mp_proc_info

	/*
	 * ARM Ltd. Cortex A15 processor.
	 */
	.type	__v7_ca15mp_proc_info, #object
__v7_ca15mp_proc_info:
	.long	0x410fc0f0
	.long	0xff0ffff0
	__v7_proc __v7_ca15mp_proc_info, __v7_ca15mp_setup, proc_fns = ca15_processor_functions
	.size	__v7_ca15mp_proc_info, . - __v7_ca15mp_proc_info

	/*
	 * Broadcom Corporation Brahma-B15 processor.
	 */
	.type	__v7_b15mp_proc_info, #object
__v7_b15mp_proc_info:
	.long	0x420f00f0
	.long	0xff0ffff0
	__v7_proc __v7_b15mp_proc_info, __v7_b15mp_setup, proc_fns = ca15_processor_functions, cache_fns = b15_cache_fns
	.size	__v7_b15mp_proc_info, . - __v7_b15mp_proc_info

	/*
	 * ARM Ltd. Cortex A17 processor.
	 */
	.type	__v7_ca17mp_proc_info, #object
__v7_ca17mp_proc_info:
	.long	0x410fc0e0
	.long	0xff0ffff0
	__v7_proc __v7_ca17mp_proc_info, __v7_ca17mp_setup, proc_fns = HARDENED_BPIALL_PROCESSOR_FUNCTIONS
	.size	__v7_ca17mp_proc_info, . - __v7_ca17mp_proc_info

	/* ARM Ltd. Cortex A73 processor */
	.type	__v7_ca73_proc_info, #object
__v7_ca73_proc_info:
	.long	0x410fd090
	.long	0xff0ffff0
	__v7_proc __v7_ca73_proc_info, __v7_setup, proc_fns = HARDENED_BPIALL_PROCESSOR_FUNCTIONS
	.size	__v7_ca73_proc_info, . - __v7_ca73_proc_info

	/* ARM Ltd. Cortex A75 processor */
	.type	__v7_ca75_proc_info, #object
__v7_ca75_proc_info:
	.long	0x410fd0a0
	.long	0xff0ffff0
	__v7_proc __v7_ca75_proc_info, __v7_setup, proc_fns = HARDENED_BPIALL_PROCESSOR_FUNCTIONS
	.size	__v7_ca75_proc_info, . - __v7_ca75_proc_info

	/*
	 * Qualcomm Inc. Krait processors.
	 */
	.type	__krait_proc_info, #object
__krait_proc_info:
	.long	0x510f0400		@ Required ID value
	.long	0xff0ffc00		@ Mask for ID
	/*
	 * Some Krait processors don't indicate support for SDIV and UDIV
	 * instructions in the ARM instruction set, even though they actually
	 * do support them. They also don't indicate support for fused multiply
	 * instructions even though they actually do support them.
	 */
	__v7_proc __krait_proc_info, __v7_setup, hwcaps = HWCAP_IDIV | HWCAP_VFPv4
	.size	__krait_proc_info, . - __krait_proc_info

	/*
	 * Match any ARMv7 processor core.
	 */
	.type	__v7_proc_info, #object
__v7_proc_info:
	.long	0x000f0000		@ Required ID value
	.long	0x000f0000		@ Mask for ID
	__v7_proc __v7_proc_info, __v7_setup
	.size	__v7_proc_info, . - __v7_proc_info
