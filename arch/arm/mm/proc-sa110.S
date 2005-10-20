/*
 *  linux/arch/arm/mm/proc-sa110.S
 *
 *  Copyright (C) 1997-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  MMU functions for SA110
 *
 *  These are the low level assembler for performing cache and TLB
 *  functions on the StrongARM-110.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/procinfo.h>
#include <asm/hardware.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>

/*
 * the cache line size of the I and D cache
 */
#define DCACHELINESIZE	32
#define FLUSH_OFFSET	32768

	.macro flush_110_dcache	rd, ra, re
	ldr	\rd, =flush_base
	ldr	\ra, [\rd]
	eor	\ra, \ra, #FLUSH_OFFSET
	str	\ra, [\rd]
	add	\re, \ra, #16384		@ only necessary for 16k
1001:	ldr	\rd, [\ra], #DCACHELINESIZE
	teq	\re, \ra
	bne	1001b
	.endm

	.data
flush_base:
	.long	FLUSH_BASE
	.text

/*
 * cpu_sa110_proc_init()
 */
ENTRY(cpu_sa110_proc_init)
	mov	r0, #0
	mcr	p15, 0, r0, c15, c1, 2		@ Enable clock switching
	mov	pc, lr

/*
 * cpu_sa110_proc_fin()
 */
ENTRY(cpu_sa110_proc_fin)
	stmfd	sp!, {lr}
	mov	ip, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
	msr	cpsr_c, ip
	bl	v4wb_flush_kern_cache_all	@ clean caches
1:	mov	r0, #0
	mcr	p15, 0, r0, c15, c2, 2		@ Disable clock switching
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1000			@ ...i............
	bic	r0, r0, #0x000e			@ ............wca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	ldmfd	sp!, {pc}

/*
 * cpu_sa110_reset(loc)
 *
 * Perform a soft reset of the system.  Put the CPU into the
 * same state as it would be if it had been reset, and branch
 * to what would be the reset vector.
 *
 * loc: location to jump to for soft reset
 */
	.align	5
ENTRY(cpu_sa110_reset)
	mov	ip, #0
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I,D caches
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
	mrc	p15, 0, ip, c1, c0, 0		@ ctrl register
	bic	ip, ip, #0x000f			@ ............wcam
	bic	ip, ip, #0x1100			@ ...i...s........
	mcr	p15, 0, ip, c1, c0, 0		@ ctrl register
	mov	pc, r0

/*
 * cpu_sa110_do_idle(type)
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

ENTRY(cpu_sa110_do_idle)
	mcr	p15, 0, ip, c15, c2, 2		@ disable clock switching
	ldr	r1, =UNCACHEABLE_ADDR		@ load from uncacheable loc
	ldr	r1, [r1, #0]			@ force switch to MCLK
	mov	r0, r0				@ safety
	mov	r0, r0				@ safety
	mov	r0, r0				@ safety
	mcr	p15, 0, r0, c15, c8, 2		@ Wait for interrupt, cache aligned
	mov	r0, r0				@ safety
	mov	r0, r0				@ safety
	mov	r0, r0				@ safety
	mcr	p15, 0, r0, c15, c1, 2		@ enable clock switching
	mov	pc, lr

/* ================================= CACHE ================================ */

/*
 * cpu_sa110_dcache_clean_area(addr,sz)
 *
 * Clean the specified entry of any caches such that the MMU
 * translation fetches will obtain correct data.
 *
 * addr: cache-unaligned virtual address
 */
	.align	5
ENTRY(cpu_sa110_dcache_clean_area)
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #DCACHELINESIZE
	subs	r1, r1, #DCACHELINESIZE
	bhi	1b
	mov	pc, lr

/* =============================== PageTable ============================== */

/*
 * cpu_sa110_switch_mm(pgd)
 *
 * Set the translation base pointer to be as described by pgd.
 *
 * pgd: new page tables
 */
	.align	5
ENTRY(cpu_sa110_switch_mm)
	flush_110_dcache	r3, ip, r1
	mov	r1, #0
	mcr	p15, 0, r1, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, r1, c7, c10, 4		@ drain WB
	mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
	mcr	p15, 0, r1, c8, c7, 0		@ invalidate I & D TLBs
	mov	pc, lr

/*
 * cpu_sa110_set_pte(ptep, pte)
 *
 * Set a PTE and flush it out
 */
	.align	5
ENTRY(cpu_sa110_set_pte)
	str	r1, [r0], #-2048		@ linux version

	eor	r1, r1, #L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_WRITE | L_PTE_DIRTY

	bic	r2, r1, #PTE_SMALL_AP_MASK
	bic	r2, r2, #PTE_TYPE_MASK
	orr	r2, r2, #PTE_TYPE_SMALL

	tst	r1, #L_PTE_USER			@ User?
	orrne	r2, r2, #PTE_SMALL_AP_URO_SRW

	tst	r1, #L_PTE_WRITE | L_PTE_DIRTY	@ Write and Dirty?
	orreq	r2, r2, #PTE_SMALL_AP_UNO_SRW

	tst	r1, #L_PTE_PRESENT | L_PTE_YOUNG	@ Present and Young?
	movne	r2, #0

	str	r2, [r0]			@ hardware version
	mov	r0, r0
	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

	__INIT

	.type	__sa110_setup, #function
__sa110_setup:
	mov	r10, #0
	mcr	p15, 0, r10, c7, c7		@ invalidate I,D caches on v4
	mcr	p15, 0, r10, c7, c10, 4		@ drain write buffer on v4
	mcr	p15, 0, r10, c8, c7		@ invalidate I,D TLBs on v4
	mrc	p15, 0, r0, c1, c0		@ get control register v4
	ldr	r5, sa110_cr1_clear
	bic	r0, r0, r5
	ldr	r5, sa110_cr1_set
	orr	r0, r0, r5
	mov	pc, lr
	.size	__sa110_setup, . - __sa110_setup

	/*
	 *  R
	 * .RVI ZFRS BLDP WCAM
	 * ..01 0001 ..11 1101
	 * 
	 */
	.type	sa110_cr1_clear, #object
	.type	sa110_cr1_set, #object
sa110_cr1_clear:
	.word	0x3f3f
sa110_cr1_set:
	.word	0x113d

	__INITDATA

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */

	.type	sa110_processor_functions, #object
ENTRY(sa110_processor_functions)
	.word	v4_early_abort
	.word	cpu_sa110_proc_init
	.word	cpu_sa110_proc_fin
	.word	cpu_sa110_reset
	.word	cpu_sa110_do_idle
	.word	cpu_sa110_dcache_clean_area
	.word	cpu_sa110_switch_mm
	.word	cpu_sa110_set_pte
	.size	sa110_processor_functions, . - sa110_processor_functions

	.section ".rodata"

	.type	cpu_arch_name, #object
cpu_arch_name:
	.asciz	"armv4"
	.size	cpu_arch_name, . - cpu_arch_name

	.type	cpu_elf_name, #object
cpu_elf_name:
	.asciz	"v4"
	.size	cpu_elf_name, . - cpu_elf_name

	.type	cpu_sa110_name, #object
cpu_sa110_name:
	.asciz	"StrongARM-110"
	.size	cpu_sa110_name, . - cpu_sa110_name

	.align

	.section ".proc.info.init", #alloc, #execinstr

	.type	__sa110_proc_info,#object
__sa110_proc_info:
	.long	0x4401a100
	.long	0xfffffff0
	.long   PMD_TYPE_SECT | \
		PMD_SECT_BUFFERABLE | \
		PMD_SECT_CACHEABLE | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	b	__sa110_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF | HWCAP_26BIT | HWCAP_FAST_MULT
	.long	cpu_sa110_name
	.long	sa110_processor_functions
	.long	v4wb_tlb_fns
	.long	v4wb_user_fns
	.long	v4wb_cache_fns
	.size	__sa110_proc_info, . - __sa110_proc_info
