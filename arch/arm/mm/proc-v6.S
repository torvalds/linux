/*
 *  linux/arch/arm/mm/proc-v6.S
 *
 *  Copyright (C) 2001 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This is the "shell" of the ARMv6 processor support.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/constants.h>
#include <asm/procinfo.h>
#include <asm/pgtable.h>

#include "proc-macros.S"

#define D_CACHE_LINE_SIZE	32

	.macro	cpsie, flags
	.ifc \flags, f
	.long	0xf1080040
	.exitm
	.endif
	.ifc \flags, i
	.long	0xf1080080
	.exitm
	.endif
	.ifc \flags, if
	.long	0xf10800c0
	.exitm
	.endif
	.err
	.endm

	.macro	cpsid, flags
	.ifc \flags, f
	.long	0xf10c0040
	.exitm
	.endif
	.ifc \flags, i
	.long	0xf10c0080
	.exitm
	.endif
	.ifc \flags, if
	.long	0xf10c00c0
	.exitm
	.endif
	.err
	.endm

ENTRY(cpu_v6_proc_init)
	mov	pc, lr

ENTRY(cpu_v6_proc_fin)
	mov	pc, lr

/*
 *	cpu_v6_reset(loc)
 *
 *	Perform a soft reset of the system.  Put the CPU into the
 *	same state as it would be if it had been reset, and branch
 *	to what would be the reset vector.
 *
 *	- loc   - location to jump to for soft reset
 *
 *	It is assumed that:
 */
	.align	5
ENTRY(cpu_v6_reset)
	mov	pc, r0

/*
 *	cpu_v6_do_idle()
 *
 *	Idle the processor (eg, wait for interrupt).
 *
 *	IRQs are already disabled.
 */
ENTRY(cpu_v6_do_idle)
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
	mov	r2, #0
	ldr	r1, [r1, #MM_CONTEXT_ID]	@ get mm->context.id
	mcr     p15, 0, r2, c7, c5, 6           @ flush BTAC/BTB
	mcr	p15, 0, r2, c7, c10, 4		@ drain write buffer
	mcr	p15, 0, r0, c2, c0, 0		@ set TTB 0
	mcr	p15, 0, r1, c13, c0, 1		@ set context ID
	mov	pc, lr

#define nG	(1 << 11)
#define APX	(1 << 9)
#define AP1	(1 << 5)
#define AP0	(1 << 4)
#define XN	(1 << 0)

/*
 *	cpu_v6_set_pte(ptep, pte)
 *
 *	Set a level 2 translation table entry.
 *
 *	- ptep  - pointer to level 2 translation table entry
 *		  (hardware version is stored at -1024 bytes)
 *	- pte   - PTE value to store
 *
 *	Permissions:
 *	  YUWD  APX AP1 AP0	SVC	User
 *	  0xxx   0   0   0	no acc	no acc
 *	  100x   1   0   1	r/o	no acc
 *	  10x0   1   0   1	r/o	no acc
 *	  1011   0   0   1	r/w	no acc
 *	  110x   1   1   0	r/o	r/o
 *	  11x0   1   1   0	r/o	r/o
 *	  1111   0   1   1	r/w	r/w
 */
ENTRY(cpu_v6_set_pte)
	str	r1, [r0], #-2048		@ linux version

	bic	r2, r1, #0x00000ff0
	bic	r2, r2, #0x00000003
	orr	r2, r2, #AP0 | 2

	tst	r1, #L_PTE_WRITE
	tstne	r1, #L_PTE_DIRTY
	orreq	r2, r2, #APX

	tst	r1, #L_PTE_USER
	orrne	r2, r2, #AP1 | nG
	tstne	r2, #APX
	eorne	r2, r2, #AP0

	tst	r1, #L_PTE_YOUNG
	biceq	r2, r2, #APX | AP1 | AP0

@	tst	r1, #L_PTE_EXEC
@	orreq	r2, r2, #XN

	tst	r1, #L_PTE_PRESENT
	moveq	r2, #0

	str	r2, [r0]
	mcr	p15, 0, r0, c7, c10, 1 @ flush_pte
	mov	pc, lr




cpu_v6_name:
	.asciz	"Some Random V6 Processor"
	.align

	.section ".text.init", #alloc, #execinstr

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
	mov	r0, #0
	mcr	p15, 0, r0, c7, c14, 0		@ clean+invalidate D cache
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, r0, c7, c15, 0		@ clean+invalidate cache
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer
	mcr	p15, 0, r0, c8, c7, 0		@ invalidate I + D TLBs
	mcr	p15, 0, r0, c2, c0, 2		@ TTB control register
	mcr	p15, 0, r4, c2, c0, 1		@ load TTB1
#ifdef CONFIG_VFP
	mrc	p15, 0, r0, c1, c0, 2
	orr	r0, r0, #(3 << 20)
	mcr	p15, 0, r0, c1, c0, 2		@ Enable full access to VFP
#endif
	mrc	p15, 0, r0, c1, c0, 0		@ read control register
	ldr	r5, v6_cr1_clear		@ get mask for bits to clear
	bic	r0, r0, r5			@ clear bits them
	ldr	r5, v6_cr1_set			@ get mask for bits to set
	orr	r0, r0, r5			@ set them
	mov	pc, lr				@ return to head.S:__ret

	/*
	 *         V X F   I D LR
	 * .... ...E PUI. .T.T 4RVI ZFRS BLDP WCAM
	 * rrrr rrrx xxx0 0101 xxxx xxxx x111 xxxx < forced
	 *         0 110       0011 1.00 .111 1101 < we want
	 */
	.type	v6_cr1_clear, #object
	.type	v6_cr1_set, #object
v6_cr1_clear:
	.word	0x01e0fb7f
v6_cr1_set:
	.word	0x00c0387d

	.type	v6_processor_functions, #object
ENTRY(v6_processor_functions)
	.word	v6_early_abort
	.word	cpu_v6_proc_init
	.word	cpu_v6_proc_fin
	.word	cpu_v6_reset
	.word	cpu_v6_do_idle
	.word	cpu_v6_dcache_clean_area
	.word	cpu_v6_switch_mm
	.word	cpu_v6_set_pte
	.size	v6_processor_functions, . - v6_processor_functions

	.type	cpu_arch_name, #object
cpu_arch_name:
	.asciz	"armv6"
	.size	cpu_arch_name, . - cpu_arch_name

	.type	cpu_elf_name, #object
cpu_elf_name:
	.asciz	"v6"
	.size	cpu_elf_name, . - cpu_elf_name
	.align

	.section ".proc.info", #alloc, #execinstr

	/*
	 * Match any ARMv6 processor core.
	 */
	.type	__v6_proc_info, #object
__v6_proc_info:
	.long	0x0007b000
	.long	0x0007f000
	.long   PMD_TYPE_SECT | \
		PMD_SECT_BUFFERABLE | \
		PMD_SECT_CACHEABLE | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	b	__v6_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_VFP|HWCAP_EDSP|HWCAP_JAVA
	.long	cpu_v6_name
	.long	v6_processor_functions
	.long	v6wbi_tlb_fns
	.long	v6_user_fns
	.long	v6_cache_fns
	.size	__v6_proc_info, . - __v6_proc_info
