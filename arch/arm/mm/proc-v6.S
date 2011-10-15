/*
 *  linux/arch/arm/mm/proc-v6.S
 *
 *  Copyright (C) 2001 Deep Blue Solutions Ltd.
 *  Modified by Catalin Marinas for noMMU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This is the "shell" of the ARMv6 processor support.
 */
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/hwcap.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>

#include "proc-macros.S"

#define D_CACHE_LINE_SIZE	32

#define TTB_C		(1 << 0)
#define TTB_S		(1 << 1)
#define TTB_IMP		(1 << 2)
#define TTB_RGN_NC	(0 << 3)
#define TTB_RGN_WBWA	(1 << 3)
#define TTB_RGN_WT	(2 << 3)
#define TTB_RGN_WB	(3 << 3)

#define TTB_FLAGS_UP	TTB_RGN_WBWA
#define PMD_FLAGS_UP	PMD_SECT_WB
#define TTB_FLAGS_SMP	TTB_RGN_WBWA|TTB_S
#define PMD_FLAGS_SMP	PMD_SECT_WBWA|PMD_SECT_S

ENTRY(cpu_v6_proc_init)
	mov	pc, lr

ENTRY(cpu_v6_proc_fin)
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1000			@ ...i............
	bic	r0, r0, #0x0006			@ .............ca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	mov	pc, lr

/*
 *	cpu_v6_reset(loc)
 *
 *	Perform a soft reset of the system.  Put the CPU into the
 *	same state as it would be if it had been reset, and branch
 *	to what would be the reset vector.
 *
 *	- loc   - location to jump to for soft reset
 */
	.align	5
ENTRY(cpu_v6_reset)
	mrc	p15, 0, r1, c1, c0, 0		@ ctrl register
	bic	r1, r1, #0x1			@ ...............m
	mcr	p15, 0, r1, c1, c0, 0		@ disable MMU
	mov	r1, #0
	mcr	p15, 0, r1, c7, c5, 4		@ ISB
	mov	pc, r0

/*
 *	cpu_v6_do_idle()
 *
 *	Idle the processor (eg, wait for interrupt).
 *
 *	IRQs are already disabled.
 */
ENTRY(cpu_v6_do_idle)
	mov	r1, #0
	mcr	p15, 0, r1, c7, c10, 4		@ DWB - WFI may enter a low-power mode
	mcr	p15, 0, r1, c7, c0, 4		@ wait for interrupt
	mov	pc, lr

ENTRY(cpu_v6_dcache_clean_area)
#ifndef TLB_CAN_READ_FROM_L1_CACHE
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #D_CACHE_LINE_SIZE
	subs	r1, r1, #D_CACHE_LINE_SIZE
	bhi	1b
#endif
	mov	pc, lr

/*
 *	cpu_arm926_switch_mm(pgd_phys, tsk)
 *
 *	Set the translation table base pointer to be pgd_phys
 *
 *	- pgd_phys - physical address of new TTB
 *
 *	It is assumed that:
 *	- we are not using split page tables
 */
ENTRY(cpu_v6_switch_mm)
#ifdef CONFIG_MMU
	mov	r2, #0
	ldr	r1, [r1, #MM_CONTEXT_ID]	@ get mm->context.id
	ALT_SMP(orr	r0, r0, #TTB_FLAGS_SMP)
	ALT_UP(orr	r0, r0, #TTB_FLAGS_UP)
	mcr	p15, 0, r2, c7, c5, 6		@ flush BTAC/BTB
	mcr	p15, 0, r2, c7, c10, 4		@ drain write buffer
	mcr	p15, 0, r0, c2, c0, 0		@ set TTB 0
	mcr	p15, 0, r1, c13, c0, 1		@ set context ID
#endif
	mov	pc, lr

/*
 *	cpu_v6_set_pte_ext(ptep, pte, ext)
 *
 *	Set a level 2 translation table entry.
 *
 *	- ptep  - pointer to level 2 translation table entry
 *		  (hardware version is stored at -1024 bytes)
 *	- pte   - PTE value to store
 *	- ext	- value for extended PTE bits
 */
	armv6_mt_table cpu_v6

ENTRY(cpu_v6_set_pte_ext)
#ifdef CONFIG_MMU
	armv6_set_pte_ext cpu_v6
#endif
	mov	pc, lr

/* Suspend/resume support: taken from arch/arm/mach-s3c64xx/sleep.S */
.globl	cpu_v6_suspend_size
.equ	cpu_v6_suspend_size, 4 * 8
#ifdef CONFIG_PM_SLEEP
ENTRY(cpu_v6_do_suspend)
	stmfd	sp!, {r4 - r11, lr}
	mrc	p15, 0, r4, c13, c0, 0	@ FCSE/PID
	mrc	p15, 0, r5, c13, c0, 1	@ Context ID
	mrc	p15, 0, r6, c3, c0, 0	@ Domain ID
	mrc	p15, 0, r7, c2, c0, 0	@ Translation table base 0
	mrc	p15, 0, r8, c2, c0, 1	@ Translation table base 1
	mrc	p15, 0, r9, c1, c0, 1	@ auxiliary control register
	mrc	p15, 0, r10, c1, c0, 2	@ co-processor access control
	mrc	p15, 0, r11, c1, c0, 0	@ control register
	stmia	r0, {r4 - r11}
	ldmfd	sp!, {r4- r11, pc}
ENDPROC(cpu_v6_do_suspend)

ENTRY(cpu_v6_do_resume)
	mov	ip, #0
	mcr	p15, 0, ip, c7, c14, 0	@ clean+invalidate D cache
	mcr	p15, 0, ip, c7, c5, 0	@ invalidate I cache
	mcr	p15, 0, ip, c7, c15, 0	@ clean+invalidate cache
	mcr	p15, 0, ip, c7, c10, 4	@ drain write buffer
	ldmia	r0, {r4 - r11}
	mcr	p15, 0, r4, c13, c0, 0	@ FCSE/PID
	mcr	p15, 0, r5, c13, c0, 1	@ Context ID
	mcr	p15, 0, r6, c3, c0, 0	@ Domain ID
	mcr	p15, 0, r7, c2, c0, 0	@ Translation table base 0
	mcr	p15, 0, r8, c2, c0, 1	@ Translation table base 1
	mcr	p15, 0, r9, c1, c0, 1	@ auxiliary control register
	mcr	p15, 0, r10, c1, c0, 2	@ co-processor access control
	mcr	p15, 0, ip, c2, c0, 2	@ TTB control register
	mcr	p15, 0, ip, c7, c5, 4	@ ISB
	mov	r0, r11			@ control register
	mov	r2, r7, lsr #14		@ get TTB0 base
	mov	r2, r2, lsl #14
	ldr	r3, cpu_resume_l1_flags
	b	cpu_resume_mmu
ENDPROC(cpu_v6_do_resume)
cpu_resume_l1_flags:
	ALT_SMP(.long PMD_TYPE_SECT | PMD_SECT_AP_WRITE | PMD_FLAGS_SMP)
	ALT_UP(.long  PMD_TYPE_SECT | PMD_SECT_AP_WRITE | PMD_FLAGS_UP)
#endif

	string	cpu_v6_name, "ARMv6-compatible processor"

	.align

	__CPUINIT

/*
 *	__v6_setup
 *
 *	Initialise TLB, Caches, and MMU state ready to switch the MMU
 *	on.  Return in r0 the new CP15 C1 control register setting.
 *
 *	We automatically detect if we have a Harvard cache, and use the
 *	Harvard cache control instructions insead of the unified cache
 *	control instructions.
 *
 *	This should be able to cover all ARMv6 cores.
 *
 *	It is assumed that:
 *	- cache type register is implemented
 */
__v6_setup:
#ifdef CONFIG_SMP
	ALT_SMP(mrc	p15, 0, r0, c1, c0, 1)	@ Enable SMP/nAMP mode
	ALT_UP(nop)
	orr	r0, r0, #0x20
	ALT_SMP(mcr	p15, 0, r0, c1, c0, 1)
	ALT_UP(nop)
#endif

	mov	r0, #0
	mcr	p15, 0, r0, c7, c14, 0		@ clean+invalidate D cache
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, r0, c7, c15, 0		@ clean+invalidate cache
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer
#ifdef CONFIG_MMU
	mcr	p15, 0, r0, c8, c7, 0		@ invalidate I + D TLBs
	mcr	p15, 0, r0, c2, c0, 2		@ TTB control register
	ALT_SMP(orr	r4, r4, #TTB_FLAGS_SMP)
	ALT_UP(orr	r4, r4, #TTB_FLAGS_UP)
	ALT_SMP(orr	r8, r8, #TTB_FLAGS_SMP)
	ALT_UP(orr	r8, r8, #TTB_FLAGS_UP)
	mcr	p15, 0, r8, c2, c0, 1		@ load TTB1
#endif /* CONFIG_MMU */
	adr	r5, v6_crval
	ldmia	r5, {r5, r6}
#ifdef CONFIG_CPU_ENDIAN_BE8
	orr	r6, r6, #1 << 25		@ big-endian page tables
#endif
	mrc	p15, 0, r0, c1, c0, 0		@ read control register
	bic	r0, r0, r5			@ clear bits them
	orr	r0, r0, r6			@ set them
#ifdef CONFIG_ARM_ERRATA_364296
	/*
	 * Workaround for the 364296 ARM1136 r0p2 erratum (possible cache data
	 * corruption with hit-under-miss enabled). The conditional code below
	 * (setting the undocumented bit 31 in the auxiliary control register
	 * and the FI bit in the control register) disables hit-under-miss
	 * without putting the processor into full low interrupt latency mode.
	 */
	ldr	r6, =0x4107b362			@ id for ARM1136 r0p2
	mrc	p15, 0, r5, c0, c0, 0		@ get processor id
	teq	r5, r6				@ check for the faulty core
	mrceq	p15, 0, r5, c1, c0, 1		@ load aux control reg
	orreq	r5, r5, #(1 << 31)		@ set the undocumented bit 31
	mcreq	p15, 0, r5, c1, c0, 1		@ write aux control reg
	orreq	r0, r0, #(1 << 21)		@ low interrupt latency configuration
#endif
	mov	pc, lr				@ return to head.S:__ret

	/*
	 *         V X F   I D LR
	 * .... ...E PUI. .T.T 4RVI ZFRS BLDP WCAM
	 * rrrr rrrx xxx0 0101 xxxx xxxx x111 xxxx < forced
	 *         0 110       0011 1.00 .111 1101 < we want
	 */
	.type	v6_crval, #object
v6_crval:
	crval	clear=0x01e0fb7f, mmuset=0x00c0387d, ucset=0x00c0187c

	__INITDATA

	@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
	define_processor_functions v6, dabort=v6_early_abort, pabort=v6_pabort, suspend=1

	.section ".rodata"

	string	cpu_arch_name, "armv6"
	string	cpu_elf_name, "v6"
	.align

	.section ".proc.info.init", #alloc, #execinstr

	/*
	 * Match any ARMv6 processor core.
	 */
	.type	__v6_proc_info, #object
__v6_proc_info:
	.long	0x0007b000
	.long	0x0007f000
	ALT_SMP(.long \
		PMD_TYPE_SECT | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ | \
		PMD_FLAGS_SMP)
	ALT_UP(.long \
		PMD_TYPE_SECT | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ | \
		PMD_FLAGS_UP)
	.long   PMD_TYPE_SECT | \
		PMD_SECT_XN | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	b	__v6_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	/* See also feat_v6_fixup() for HWCAP_TLS */
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_EDSP|HWCAP_JAVA|HWCAP_TLS
	.long	cpu_v6_name
	.long	v6_processor_functions
	.long	v6wbi_tlb_fns
	.long	v6_user_fns
	.long	v6_cache_fns
	.size	__v6_proc_info, . - __v6_proc_info
