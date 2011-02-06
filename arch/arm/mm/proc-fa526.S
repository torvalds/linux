/*
 *  linux/arch/arm/mm/proc-fa526.S: MMU functions for FA526
 *
 *  Written by : Luke Lee
 *  Copyright (C) 2005 Faraday Corp.
 *  Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *
 * These are the low level assembler for performing cache and TLB
 * functions on the fa526.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/hwcap.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/system.h>

#include "proc-macros.S"

#define CACHE_DLINESIZE	16

	.text
/*
 * cpu_fa526_proc_init()
 */
ENTRY(cpu_fa526_proc_init)
	mov	pc, lr

/*
 * cpu_fa526_proc_fin()
 */
ENTRY(cpu_fa526_proc_fin)
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1000			@ ...i............
	bic	r0, r0, #0x000e			@ ............wca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	nop
	nop
	mov	pc, lr

/*
 * cpu_fa526_reset(loc)
 *
 * Perform a soft reset of the system.  Put the CPU into the
 * same state as it would be if it had been reset, and branch
 * to what would be the reset vector.
 *
 * loc: location to jump to for soft reset
 */
	.align	4
ENTRY(cpu_fa526_reset)
/* TODO: Use CP8 if possible... */
	mov	ip, #0
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I,D caches
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
#ifdef CONFIG_MMU
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
#endif
	mrc	p15, 0, ip, c1, c0, 0		@ ctrl register
	bic	ip, ip, #0x000f			@ ............wcam
	bic	ip, ip, #0x1100			@ ...i...s........
	bic	ip, ip, #0x0800			@ BTB off
	mcr	p15, 0, ip, c1, c0, 0		@ ctrl register
	nop
	nop
	mov	pc, r0

/*
 * cpu_fa526_do_idle()
 */
	.align	4
ENTRY(cpu_fa526_do_idle)
	mcr	p15, 0, r0, c7, c0, 4		@ Wait for interrupt
	mov	pc, lr


ENTRY(cpu_fa526_dcache_clean_area)
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	subs	r1, r1, #CACHE_DLINESIZE
	bhi	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/* =============================== PageTable ============================== */

/*
 * cpu_fa526_switch_mm(pgd)
 *
 * Set the translation base pointer to be as described by pgd.
 *
 * pgd: new page tables
 */
	.align	4
ENTRY(cpu_fa526_switch_mm)
#ifdef CONFIG_MMU
	mov	ip, #0
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	mcr	p15, 0, ip, c7, c6, 0		@ invalidate D cache
#else
	mcr	p15, 0, ip, c7, c14, 0		@ clean and invalidate whole D cache
#endif
	mcr	p15, 0, ip, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, ip, c7, c5, 6		@ invalidate BTB since mm changed
	mcr	p15, 0, ip, c7, c10, 4		@ data write barrier
	mcr	p15, 0, ip, c7, c5, 4		@ prefetch flush
	mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate UTLB
#endif
	mov	pc, lr

/*
 * cpu_fa526_set_pte_ext(ptep, pte, ext)
 *
 * Set a PTE and flush it out
 */
	.align	4
ENTRY(cpu_fa526_set_pte_ext)
#ifdef CONFIG_MMU
	armv3_set_pte_ext
	mov	r0, r0
	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mov	r0, #0
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
#endif
	mov	pc, lr

	__CPUINIT

	.type	__fa526_setup, #function
__fa526_setup:
	/* On return of this routine, r0 must carry correct flags for CFG register */
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7		@ invalidate I,D caches on v4
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer on v4
#ifdef CONFIG_MMU
	mcr	p15, 0, r0, c8, c7		@ invalidate I,D TLBs on v4
#endif
	mcr	p15, 0, r0, c7, c5, 5		@ invalidate IScratchpad RAM

	mov	r0, #1
	mcr	p15, 0, r0, c1, c1, 0		@ turn-on ECR

	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 6		@ invalidate BTB All
	mcr	p15, 0, r0, c7, c10, 4		@ data write barrier
	mcr	p15, 0, r0, c7, c5, 4		@ prefetch flush

	mov	r0, #0x1f			@ Domains 0, 1 = manager, 2 = client
	mcr	p15, 0, r0, c3, c0		@ load domain access register

	mrc	p15, 0, r0, c1, c0		@ get control register v4
	ldr	r5, fa526_cr1_clear
	bic	r0, r0, r5
	ldr	r5, fa526_cr1_set
	orr	r0, r0, r5
	mov	pc, lr
	.size	__fa526_setup, . - __fa526_setup

	/*
	 * .RVI ZFRS BLDP WCAM
	 * ..11 1001 .111 1101
	 *
	 */
	.type	fa526_cr1_clear, #object
	.type	fa526_cr1_set, #object
fa526_cr1_clear:
	.word	0x3f3f
fa526_cr1_set:
	.word	0x397D

	__INITDATA

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
	.type	fa526_processor_functions, #object
fa526_processor_functions:
	.word	v4_early_abort
	.word	legacy_pabort
	.word	cpu_fa526_proc_init
	.word	cpu_fa526_proc_fin
	.word	cpu_fa526_reset
	.word   cpu_fa526_do_idle
	.word	cpu_fa526_dcache_clean_area
	.word	cpu_fa526_switch_mm
	.word	cpu_fa526_set_pte_ext
	.word	0
	.word	0
	.word	0
	.size	fa526_processor_functions, . - fa526_processor_functions

	.section ".rodata"

	.type	cpu_arch_name, #object
cpu_arch_name:
	.asciz	"armv4"
	.size	cpu_arch_name, . - cpu_arch_name

	.type	cpu_elf_name, #object
cpu_elf_name:
	.asciz	"v4"
	.size	cpu_elf_name, . - cpu_elf_name

	.type	cpu_fa526_name, #object
cpu_fa526_name:
	.asciz	"FA526"
	.size	cpu_fa526_name, . - cpu_fa526_name

	.align

	.section ".proc.info.init", #alloc, #execinstr

	.type	__fa526_proc_info,#object
__fa526_proc_info:
	.long	0x66015261
	.long	0xff01fff1
	.long   PMD_TYPE_SECT | \
		PMD_SECT_BUFFERABLE | \
		PMD_SECT_CACHEABLE | \
		PMD_BIT4 | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	.long   PMD_TYPE_SECT | \
		PMD_BIT4 | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	b	__fa526_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF
	.long	cpu_fa526_name
	.long	fa526_processor_functions
	.long	fa_tlb_fns
	.long	fa_user_fns
	.long	fa_cache_fns
	.size	__fa526_proc_info, . - __fa526_proc_info
