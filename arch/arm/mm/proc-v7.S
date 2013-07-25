/*
 *  linux/arch/arm/mm/proc-v7.S
 *
 *  Copyright (C) 2001 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This is the "shell" of the ARMv7 processor support.
 */
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/hwcap.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>

#include "proc-macros.S"

#ifdef CONFIG_ARM_LPAE
#include "proc-v7-3level.S"
#else
#include "proc-v7-2level.S"
#endif

ENTRY(cpu_v7_proc_init)
	mov	pc, lr
ENDPROC(cpu_v7_proc_init)

ENTRY(cpu_v7_proc_fin)
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1000			@ ...i............
	bic	r0, r0, #0x0006			@ .............ca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	mov	pc, lr
ENDPROC(cpu_v7_proc_fin)

/*
 *	cpu_v7_reset(loc)
 *
 *	Perform a soft reset of the system.  Put the CPU into the
 *	same state as it would be if it had been reset, and branch
 *	to what would be the reset vector.
 *
 *	- loc   - location to jump to for soft reset
 *
 *	This code must be executed using a flat identity mapping with
 *      caches disabled.
 */
	.align	5
	.pushsection	.idmap.text, "ax"
ENTRY(cpu_v7_reset)
	mrc	p15, 0, r1, c1, c0, 0		@ ctrl register
	bic	r1, r1, #0x1			@ ...............m
 THUMB(	bic	r1, r1, #1 << 30 )		@ SCTLR.TE (Thumb exceptions)
	mcr	p15, 0, r1, c1, c0, 0		@ disable MMU
	isb
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
	mov	pc, lr
ENDPROC(cpu_v7_do_idle)

ENTRY(cpu_v7_dcache_clean_area)
	ALT_SMP(mov	pc, lr)			@ MP extensions imply L1 PTW
	ALT_UP(W(nop))
	dcache_line_size r2, r3
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, r2
	subs	r1, r1, r2
	bhi	1b
	dsb
	mov	pc, lr
ENDPROC(cpu_v7_dcache_clean_area)

	string	cpu_v7_name, "ARMv7 Processor"
	.align

/* Suspend/resume support: derived from arch/arm/mach-s5pv210/sleep.S */
.globl	cpu_v7_suspend_size
.equ	cpu_v7_suspend_size, 4 * 8
#ifdef CONFIG_ARM_CPU_SUSPEND
ENTRY(cpu_v7_do_suspend)
	stmfd	sp!, {r4 - r10, lr}
	mrc	p15, 0, r4, c13, c0, 0	@ FCSE/PID
	mrc	p15, 0, r5, c13, c0, 3	@ User r/o thread ID
	stmia	r0!, {r4 - r5}
#ifdef CONFIG_MMU
	mrc	p15, 0, r6, c3, c0, 0	@ Domain ID
	mrc	p15, 0, r7, c2, c0, 1	@ TTB 1
	mrc	p15, 0, r11, c2, c0, 2	@ TTB control register
#endif
	mrc	p15, 0, r8, c1, c0, 0	@ Control register
	mrc	p15, 0, r9, c1, c0, 1	@ Auxiliary control register
	mrc	p15, 0, r10, c1, c0, 2	@ Co-processor access control
	stmia	r0, {r6 - r11}
	ldmfd	sp!, {r4 - r10, pc}
ENDPROC(cpu_v7_do_suspend)

ENTRY(cpu_v7_do_resume)
	mov	ip, #0
	mcr	p15, 0, ip, c7, c5, 0	@ invalidate I cache
	mcr	p15, 0, ip, c13, c0, 1	@ set reserved context ID
	ldmia	r0!, {r4 - r5}
	mcr	p15, 0, r4, c13, c0, 0	@ FCSE/PID
	mcr	p15, 0, r5, c13, c0, 3	@ User r/o thread ID
	ldmia	r0, {r6 - r11}
#ifdef CONFIG_MMU
	mcr	p15, 0, ip, c8, c7, 0	@ invalidate TLBs
	mcr	p15, 0, r6, c3, c0, 0	@ Domain ID
#ifndef CONFIG_ARM_LPAE
	ALT_SMP(orr	r1, r1, #TTB_FLAGS_SMP)
	ALT_UP(orr	r1, r1, #TTB_FLAGS_UP)
#endif
	mcr	p15, 0, r1, c2, c0, 0	@ TTB 0
	mcr	p15, 0, r7, c2, c0, 1	@ TTB 1
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
	mov	pc, lr
ENDPROC(cpu_pj4b_do_idle)
#else
	globl_equ	cpu_pj4b_do_idle,  	cpu_v7_do_idle
#endif
	globl_equ	cpu_pj4b_dcache_clean_area,	cpu_v7_dcache_clean_area
	globl_equ	cpu_pj4b_do_suspend,	cpu_v7_do_suspend
	globl_equ	cpu_pj4b_do_resume,	cpu_v7_do_resume
	globl_equ	cpu_pj4b_suspend_size,	cpu_v7_suspend_size

#endif

/*
 *	__v7_setup
 *
 *	Initialise TLB, Caches, and MMU state ready to switch the MMU
 *	on.  Return in r0 the new CP15 C1 control register setting.
 *
 *	This should be able to cover all ARMv7 cores.
 *
 *	It is assumed that:
 *	- cache type register is implemented
 */
__v7_ca5mp_setup:
__v7_ca9mp_setup:
__v7_cr7mp_setup:
	mov	r10, #(1 << 0)			@ Cache/TLB ops broadcasting
	b	1f
__v7_ca7mp_setup:
__v7_ca15mp_setup:
	mov	r10, #0
1:
#ifdef CONFIG_SMP
	ALT_SMP(mrc	p15, 0, r0, c1, c0, 1)
	ALT_UP(mov	r0, #(1 << 6))		@ fake it for UP
	tst	r0, #(1 << 6)			@ SMP/nAMP mode enabled?
	orreq	r0, r0, #(1 << 6)		@ Enable SMP/nAMP mode
	orreq	r0, r0, r10			@ Enable CPU-specific SMP bits
	mcreq	p15, 0, r0, c1, c0, 1
#endif
	b	__v7_setup

__v7_pj4b_setup:
#ifdef CONFIG_CPU_PJ4B

/* Auxiliary Debug Modes Control 1 Register */
#define PJ4B_STATIC_BP (1 << 2) /* Enable Static BP */
#define PJ4B_INTER_PARITY (1 << 8) /* Disable Internal Parity Handling */
#define PJ4B_BCK_OFF_STREX (1 << 5) /* Enable the back off of STREX instr */
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
	orr     r0, r0, #PJ4B_BCK_OFF_STREX
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
	adr	r12, __v7_setup_stack		@ the local stack
	stmia	r12, {r0-r5, r7, r9, r11, lr}
	bl      v7_flush_dcache_louis
	ldmia	r12, {r0-r5, r7, r9, r11, lr}

	mrc	p15, 0, r0, c0, c0, 0		@ read main ID register
	and	r10, r0, #0xff000000		@ ARM?
	teq	r10, #0x41000000
	bne	3f
	and	r5, r0, #0x00f00000		@ variant
	and	r6, r0, #0x0000000f		@ revision
	orr	r6, r6, r5, lsr #20-4		@ combine variant and revision
	ubfx	r0, r0, #4, #12			@ primary part number

	/* Cortex-A8 Errata */
	ldr	r10, =0x00000c08		@ Cortex-A8 primary part number
	teq	r0, r10
	bne	2f
#if defined(CONFIG_ARM_ERRATA_430973) && !defined(CONFIG_ARCH_MULTIPLATFORM)

	teq	r5, #0x00100000			@ only present in r1p*
	mrceq	p15, 0, r10, c1, c0, 1		@ read aux control register
	orreq	r10, r10, #(1 << 6)		@ set IBE to 1
	mcreq	p15, 0, r10, c1, c0, 1		@ write aux control register
#endif
#ifdef CONFIG_ARM_ERRATA_458693
	teq	r6, #0x20			@ only present in r2p0
	mrceq	p15, 0, r10, c1, c0, 1		@ read aux control register
	orreq	r10, r10, #(1 << 5)		@ set L1NEON to 1
	orreq	r10, r10, #(1 << 9)		@ set PLDNOP to 1
	mcreq	p15, 0, r10, c1, c0, 1		@ write aux control register
#endif
#ifdef CONFIG_ARM_ERRATA_460075
	teq	r6, #0x20			@ only present in r2p0
	mrceq	p15, 1, r10, c9, c0, 2		@ read L2 cache aux ctrl register
	tsteq	r10, #1 << 22
	orreq	r10, r10, #(1 << 22)		@ set the Write Allocate disable bit
	mcreq	p15, 1, r10, c9, c0, 2		@ write the L2 cache aux ctrl register
#endif
	b	3f

	/* Cortex-A9 Errata */
2:	ldr	r10, =0x00000c09		@ Cortex-A9 primary part number
	teq	r0, r10
	bne	3f
#ifdef CONFIG_ARM_ERRATA_742230
	cmp	r6, #0x22			@ only present up to r2p2
	mrcle	p15, 0, r10, c15, c0, 1		@ read diagnostic register
	orrle	r10, r10, #1 << 4		@ set bit #4
	mcrle	p15, 0, r10, c15, c0, 1		@ write diagnostic register
#endif
#ifdef CONFIG_ARM_ERRATA_742231
	teq	r6, #0x20			@ present in r2p0
	teqne	r6, #0x21			@ present in r2p1
	teqne	r6, #0x22			@ present in r2p2
	mrceq	p15, 0, r10, c15, c0, 1		@ read diagnostic register
	orreq	r10, r10, #1 << 12		@ set bit #12
	orreq	r10, r10, #1 << 22		@ set bit #22
	mcreq	p15, 0, r10, c15, c0, 1		@ write diagnostic register
#endif
#ifdef CONFIG_ARM_ERRATA_743622
	teq	r5, #0x00200000			@ only present in r2p*
	mrceq	p15, 0, r10, c15, c0, 1		@ read diagnostic register
	orreq	r10, r10, #1 << 6		@ set bit #6
	mcreq	p15, 0, r10, c15, c0, 1		@ write diagnostic register
#endif
#if defined(CONFIG_ARM_ERRATA_751472) && defined(CONFIG_SMP)
	ALT_SMP(cmp r6, #0x30)			@ present prior to r3p0
	ALT_UP_B(1f)
	mrclt	p15, 0, r10, c15, c0, 1		@ read diagnostic register
	orrlt	r10, r10, #1 << 11		@ set bit #11
	mcrlt	p15, 0, r10, c15, c0, 1		@ write diagnostic register
1:
#endif

3:	mov	r10, #0
	mcr	p15, 0, r10, c7, c5, 0		@ I+BTB cache invalidate
	dsb
#ifdef CONFIG_MMU
	mcr	p15, 0, r10, c8, c7, 0		@ invalidate I + D TLBs
	v7_ttb_setup r10, r4, r8, r5		@ TTBCR, TTBRx setup
	ldr	r5, =PRRR			@ PRRR
	ldr	r6, =NMRR			@ NMRR
	mcr	p15, 0, r5, c10, c2, 0		@ write PRRR
	mcr	p15, 0, r6, c10, c2, 1		@ write NMRR
#endif
#ifndef CONFIG_ARM_THUMBEE
	mrc	p15, 0, r0, c0, c1, 0		@ read ID_PFR0 for ThumbEE
	and	r0, r0, #(0xf << 12)		@ ThumbEE enabled field
	teq	r0, #(1 << 12)			@ check if ThumbEE is present
	bne	1f
	mov	r5, #0
	mcr	p14, 6, r5, c1, c0, 0		@ Initialize TEEHBR to 0
	mrc	p14, 6, r0, c0, c0, 0		@ load TEECR
	orr	r0, r0, #1			@ set the 1st bit in order to
	mcr	p14, 6, r0, c0, c0, 0		@ stop userspace TEEHBR access
1:
#endif
	adr	r5, v7_crval
	ldmia	r5, {r5, r6}
#ifdef CONFIG_CPU_ENDIAN_BE8
	orr	r6, r6, #1 << 25		@ big-endian page tables
#endif
#ifdef CONFIG_SWP_EMULATE
	orr     r5, r5, #(1 << 10)              @ set SW bit in "clear"
	bic     r6, r6, #(1 << 10)              @ clear it in "mmuset"
#endif
   	mrc	p15, 0, r0, c1, c0, 0		@ read control register
	bic	r0, r0, r5			@ clear bits them
	orr	r0, r0, r6			@ set them
 THUMB(	orr	r0, r0, #1 << 30	)	@ Thumb exceptions
	mov	pc, lr				@ return to head.S:__ret
ENDPROC(__v7_setup)

	.align	2
__v7_setup_stack:
	.space	4 * 11				@ 11 registers

	__INITDATA

	@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
	define_processor_functions v7, dabort=v7_early_abort, pabort=v7_pabort, suspend=1
#ifdef CONFIG_CPU_PJ4B
	define_processor_functions pj4b, dabort=v7_early_abort, pabort=v7_pabort, suspend=1
#endif

	.section ".rodata"

	string	cpu_arch_name, "armv7"
	string	cpu_elf_name, "v7"
	.align

	.section ".proc.info.init", #alloc, #execinstr

	/*
	 * Standard v7 proc info content
	 */
.macro __v7_proc initfunc, mm_mmuflags = 0, io_mmuflags = 0, hwcaps = 0, proc_fns = v7_processor_functions
	ALT_SMP(.long	PMD_TYPE_SECT | PMD_SECT_AP_WRITE | PMD_SECT_AP_READ | \
			PMD_SECT_AF | PMD_FLAGS_SMP | \mm_mmuflags)
	ALT_UP(.long	PMD_TYPE_SECT | PMD_SECT_AP_WRITE | PMD_SECT_AP_READ | \
			PMD_SECT_AF | PMD_FLAGS_UP | \mm_mmuflags)
	.long	PMD_TYPE_SECT | PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ | PMD_SECT_AF | \io_mmuflags
	W(b)	\initfunc
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF | HWCAP_THUMB | HWCAP_FAST_MULT | \
		HWCAP_EDSP | HWCAP_TLS | \hwcaps
	.long	cpu_v7_name
	.long	\proc_fns
	.long	v7wbi_tlb_fns
	.long	v6_user_fns
	.long	v7_cache_fns
.endm

#ifndef CONFIG_ARM_LPAE
	/*
	 * ARM Ltd. Cortex A5 processor.
	 */
	.type   __v7_ca5mp_proc_info, #object
__v7_ca5mp_proc_info:
	.long	0x410fc050
	.long	0xff0ffff0
	__v7_proc __v7_ca5mp_setup
	.size	__v7_ca5mp_proc_info, . - __v7_ca5mp_proc_info

	/*
	 * ARM Ltd. Cortex A9 processor.
	 */
	.type   __v7_ca9mp_proc_info, #object
__v7_ca9mp_proc_info:
	.long	0x410fc090
	.long	0xff0ffff0
	__v7_proc __v7_ca9mp_setup
	.size	__v7_ca9mp_proc_info, . - __v7_ca9mp_proc_info

#endif	/* CONFIG_ARM_LPAE */

	/*
	 * Marvell PJ4B processor.
	 */
#ifdef CONFIG_CPU_PJ4B
	.type   __v7_pj4b_proc_info, #object
__v7_pj4b_proc_info:
	.long	0x560f5800
	.long	0xff0fff00
	__v7_proc __v7_pj4b_setup, proc_fns = pj4b_processor_functions
	.size	__v7_pj4b_proc_info, . - __v7_pj4b_proc_info
#endif

	/*
	 * ARM Ltd. Cortex R7 processor.
	 */
	.type	__v7_cr7mp_proc_info, #object
__v7_cr7mp_proc_info:
	.long	0x410fc170
	.long	0xff0ffff0
	__v7_proc __v7_cr7mp_setup
	.size	__v7_cr7mp_proc_info, . - __v7_cr7mp_proc_info

	/*
	 * ARM Ltd. Cortex A7 processor.
	 */
	.type	__v7_ca7mp_proc_info, #object
__v7_ca7mp_proc_info:
	.long	0x410fc070
	.long	0xff0ffff0
	__v7_proc __v7_ca7mp_setup
	.size	__v7_ca7mp_proc_info, . - __v7_ca7mp_proc_info

	/*
	 * ARM Ltd. Cortex A15 processor.
	 */
	.type	__v7_ca15mp_proc_info, #object
__v7_ca15mp_proc_info:
	.long	0x410fc0f0
	.long	0xff0ffff0
	__v7_proc __v7_ca15mp_setup
	.size	__v7_ca15mp_proc_info, . - __v7_ca15mp_proc_info

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
	 * do support them.
	 */
	__v7_proc __v7_setup, hwcaps = HWCAP_IDIV
	.size	__krait_proc_info, . - __krait_proc_info

	/*
	 * Match any ARMv7 processor core.
	 */
	.type	__v7_proc_info, #object
__v7_proc_info:
	.long	0x000f0000		@ Required ID value
	.long	0x000f0000		@ Mask for ID
	__v7_proc __v7_setup
	.size	__v7_proc_info, . - __v7_proc_info
