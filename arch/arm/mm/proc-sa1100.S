/*
 *  linux/arch/arm/mm/proc-sa1100.S
 *
 *  Copyright (C) 1997-2002 Russell King
 *  hacked for non-paged-MM by Hyok S. Choi, 2003.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  MMU functions for SA110
 *
 *  These are the low level assembler for performing cache and TLB
 *  functions on the StrongARM-1100 and StrongARM-1110.
 *
 *  Note that SA1100 and SA1110 share everything but their name and CPU ID.
 *
 *  12-jun-2000, Erik Mouw (J.A.K.Mouw@its.tudelft.nl):
 *    Flush the read buffer at context switches
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/elf.h>
#include <mach/hardware.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>

#include "proc-macros.S"

/*
 * the cache line size of the I and D cache
 */
#define DCACHELINESIZE	32

	__INIT

/*
 * cpu_sa1100_proc_init()
 */
ENTRY(cpu_sa1100_proc_init)
	mov	r0, #0
	mcr	p15, 0, r0, c15, c1, 2		@ Enable clock switching
	mcr	p15, 0, r0, c9, c0, 5		@ Allow read-buffer operations from userland
	mov	pc, lr

	.previous

/*
 * cpu_sa1100_proc_fin()
 *
 * Prepare the CPU for reset:
 *  - Disable interrupts
 *  - Clean and turn off caches.
 */
ENTRY(cpu_sa1100_proc_fin)
	stmfd	sp!, {lr}
	mov	ip, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
	msr	cpsr_c, ip
	bl	v4wb_flush_kern_cache_all
	mcr	p15, 0, ip, c15, c2, 2		@ Disable clock switching
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1000			@ ...i............
	bic	r0, r0, #0x000e			@ ............wca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	ldmfd	sp!, {pc}

/*
 * cpu_sa1100_reset(loc)
 *
 * Perform a soft reset of the system.  Put the CPU into the
 * same state as it would be if it had been reset, and branch
 * to what would be the reset vector.
 *
 * loc: location to jump to for soft reset
 */
	.align	5
ENTRY(cpu_sa1100_reset)
	mov	ip, #0
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I,D caches
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
#ifdef CONFIG_MMU
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
#endif
	mrc	p15, 0, ip, c1, c0, 0		@ ctrl register
	bic	ip, ip, #0x000f			@ ............wcam
	bic	ip, ip, #0x1100			@ ...i...s........
	mcr	p15, 0, ip, c1, c0, 0		@ ctrl register
	mov	pc, r0

/*
 * cpu_sa1100_do_idle(type)
 *
 * Cause the processor to idle
 *
 * type: call type:
 *   0 = slow idle
 *   1 = fast idle
 *   2 = switch to slow processor clock
 *   3 = switch to fast processor clock
 */
	.align	5
ENTRY(cpu_sa1100_do_idle)
	mov	r0, r0				@ 4 nop padding
	mov	r0, r0
	mov	r0, r0
	mov	r0, r0				@ 4 nop padding
	mov	r0, r0
	mov	r0, r0
	mov	r0, #0
	ldr	r1, =UNCACHEABLE_ADDR		@ ptr to uncacheable address
	@ --- aligned to a cache line
	mcr	p15, 0, r0, c15, c2, 2		@ disable clock switching
	ldr	r1, [r1, #0]			@ force switch to MCLK
	mcr	p15, 0, r0, c15, c8, 2		@ wait for interrupt
	mov	r0, r0				@ safety
	mcr	p15, 0, r0, c15, c1, 2		@ enable clock switching
	mov	pc, lr

/* ================================= CACHE ================================ */

/*
 * cpu_sa1100_dcache_clean_area(addr,sz)
 *
 * Clean the specified entry of any caches such that the MMU
 * translation fetches will obtain correct data.
 *
 * addr: cache-unaligned virtual address
 */
	.align	5
ENTRY(cpu_sa1100_dcache_clean_area)
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #DCACHELINESIZE
	subs	r1, r1, #DCACHELINESIZE
	bhi	1b
	mov	pc, lr

/* =============================== PageTable ============================== */

/*
 * cpu_sa1100_switch_mm(pgd)
 *
 * Set the translation base pointer to be as described by pgd.
 *
 * pgd: new page tables
 */
	.align	5
ENTRY(cpu_sa1100_switch_mm)
#ifdef CONFIG_MMU
	str	lr, [sp, #-4]!
	bl	v4wb_flush_kern_cache_all	@ clears IP
	mcr	p15, 0, ip, c9, c0, 0		@ invalidate RB
	mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
	ldr	pc, [sp], #4
#else
	mov	pc, lr
#endif

/*
 * cpu_sa1100_set_pte_ext(ptep, pte, ext)
 *
 * Set a PTE and flush it out
 */
	.align	5
ENTRY(cpu_sa1100_set_pte_ext)
#ifdef CONFIG_MMU
	armv3_set_pte_ext wc_disable=0
	mov	r0, r0
	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
#endif
	mov	pc, lr

	__INIT

	.type	__sa1100_setup, #function
__sa1100_setup:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7		@ invalidate I,D caches on v4
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer on v4
#ifdef CONFIG_MMU
	mcr	p15, 0, r0, c8, c7		@ invalidate I,D TLBs on v4
#endif
	adr	r5, sa1100_crval
	ldmia	r5, {r5, r6}
	mrc	p15, 0, r0, c1, c0		@ get control register v4
	bic	r0, r0, r5
	orr	r0, r0, r6
	mov	pc, lr
	.size	__sa1100_setup, . - __sa1100_setup

	/*
	 *  R
	 * .RVI ZFRS BLDP WCAM
	 * ..11 0001 ..11 1101
	 * 
	 */
	.type	sa1100_crval, #object
sa1100_crval:
	crval	clear=0x00003f3f, mmuset=0x0000313d, ucset=0x00001130

	__INITDATA

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */

/*
 * SA1100 and SA1110 share the same function calls
 */
	.type	sa1100_processor_functions, #object
ENTRY(sa1100_processor_functions)
	.word	v4_early_abort
	.word	pabort_noifar
	.word	cpu_sa1100_proc_init
	.word	cpu_sa1100_proc_fin
	.word	cpu_sa1100_reset
	.word	cpu_sa1100_do_idle
	.word	cpu_sa1100_dcache_clean_area
	.word	cpu_sa1100_switch_mm
	.word	cpu_sa1100_set_pte_ext
	.size	sa1100_processor_functions, . - sa1100_processor_functions

	.section ".rodata"

	.type	cpu_arch_name, #object
cpu_arch_name:
	.asciz	"armv4"
	.size	cpu_arch_name, . - cpu_arch_name

	.type	cpu_elf_name, #object
cpu_elf_name:
	.asciz	"v4"
	.size	cpu_elf_name, . - cpu_elf_name

	.type	cpu_sa1100_name, #object
cpu_sa1100_name:
	.asciz	"StrongARM-1100"
	.size	cpu_sa1100_name, . - cpu_sa1100_name

	.type	cpu_sa1110_name, #object
cpu_sa1110_name:
	.asciz	"StrongARM-1110"
	.size	cpu_sa1110_name, . - cpu_sa1110_name

	.align

	.section ".proc.info.init", #alloc, #execinstr

	.type	__sa1100_proc_info,#object
__sa1100_proc_info:
	.long	0x4401a110
	.long	0xfffffff0
	.long   PMD_TYPE_SECT | \
		PMD_SECT_BUFFERABLE | \
		PMD_SECT_CACHEABLE | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	.long   PMD_TYPE_SECT | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	b	__sa1100_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF | HWCAP_26BIT | HWCAP_FAST_MULT
	.long	cpu_sa1100_name
	.long	sa1100_processor_functions
	.long	v4wb_tlb_fns
	.long	v4_mc_user_fns
	.long	v4wb_cache_fns
	.size	__sa1100_proc_info, . - __sa1100_proc_info

	.type	__sa1110_proc_info,#object
__sa1110_proc_info:
	.long	0x6901b110
	.long	0xfffffff0
	.long   PMD_TYPE_SECT | \
		PMD_SECT_BUFFERABLE | \
		PMD_SECT_CACHEABLE | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	.long   PMD_TYPE_SECT | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	b	__sa1100_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF | HWCAP_26BIT | HWCAP_FAST_MULT
	.long	cpu_sa1110_name
	.long	sa1100_processor_functions
	.long	v4wb_tlb_fns
	.long	v4_mc_user_fns
	.long	v4wb_cache_fns
	.size	__sa1110_proc_info, . - __sa1110_proc_info
