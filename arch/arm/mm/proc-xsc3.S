/*
 * linux/arch/arm/mm/proc-xsc3.S
 *
 * Original Author: Matthew Gilbert
 * Current Maintainer: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2004 (C) Intel Corp.
 * Copyright 2005 (c) MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MMU functions for the Intel XScale3 Core (XSC3).  The XSC3 core is an
 * extension to Intel's original XScale core that adds the following
 * features:
 *
 * - ARMv6 Supersections
 * - Low Locality Reference pages (replaces mini-cache)
 * - 36-bit addressing
 * - L2 cache
 * - Cache-coherency if chipset supports it
 *
 * Based on orignal XScale code by Nicolas Pitre
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/procinfo.h>
#include <asm/hardware.h>
#include <asm/pgtable.h>
#include <asm/pgtable-hwdef.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include "proc-macros.S"

/*
 * This is the maximum size of an area which will be flushed.  If the
 * area is larger than this, then we flush the whole cache.
 */
#define MAX_AREA_SIZE	32768

/*
 * The cache line size of the I and D cache.
 */
#define CACHELINESIZE	32

/*
 * The size of the data cache.
 */
#define CACHESIZE	32768

/*
 * Run with L2 enabled.
 */
#define L2_CACHE_ENABLE	1

/*
 * Enable the Branch Target Buffer (can cause crashes, see erratum #42.)
 */
#define BTB_ENABLE	0

/*
 * This macro is used to wait for a CP15 write and is needed
 * when we have to ensure that the last operation to the co-pro
 * was completed before continuing with operation.
 */
	.macro	cpwait_ret, lr, rd
	mrc	p15, 0, \rd, c2, c0, 0		@ arbitrary read of cp15
	sub	pc, \lr, \rd, LSR #32		@ wait for completion and
						@ flush instruction pipeline
	.endm

/*
 * This macro cleans & invalidates the entire xsc3 dcache by set & way.
 */

 	.macro  clean_d_cache rd, rs
	mov	\rd, #0x1f00
	orr	\rd, \rd, #0x00e0
1:	mcr	p15, 0, \rd, c7, c14, 2		@ clean/inv set/way
	adds	\rd, \rd, #0x40000000
	bcc	1b
	subs	\rd, \rd, #0x20
	bpl	1b
	.endm

	.text

/*
 * cpu_xsc3_proc_init()
 *
 * Nothing too exciting at the moment
 */
ENTRY(cpu_xsc3_proc_init)
	mov	pc, lr

/*
 * cpu_xsc3_proc_fin()
 */
ENTRY(cpu_xsc3_proc_fin)
	str	lr, [sp, #-4]!
	mov	r0, #PSR_F_BIT|PSR_I_BIT|SVC_MODE
	msr	cpsr_c, r0
	bl	xsc3_flush_kern_cache_all	@ clean caches
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1800			@ ...IZ...........
	bic	r0, r0, #0x0006			@ .............CA.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	ldr	pc, [sp], #4

/*
 * cpu_xsc3_reset(loc)
 *
 * Perform a soft reset of the system.  Put the CPU into the
 * same state as it would be if it had been reset, and branch
 * to what would be the reset vector.
 *
 * loc: location to jump to for soft reset
 */
	.align	5
ENTRY(cpu_xsc3_reset)
	mov	r1, #PSR_F_BIT|PSR_I_BIT|SVC_MODE
	msr	cpsr_c, r1			@ reset CPSR
	mrc	p15, 0, r1, c1, c0, 0		@ ctrl register
	bic	r1, r1, #0x0086			@ ........B....CA.
	bic	r1, r1, #0x3900			@ ..VIZ..S........
	mcr	p15, 0, r1, c1, c0, 0		@ ctrl register
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I,D caches & BTB
	bic	r1, r1, #0x0001			@ ...............M
	mcr	p15, 0, r1, c1, c0, 0		@ ctrl register
	@ CAUTION: MMU turned off from this point.  We count on the pipeline
	@ already containing those two last instructions to survive.
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
	mov	pc, r0

/*
 * cpu_xsc3_do_idle()
 *
 * Cause the processor to idle
 *
 * For now we do nothing but go to idle mode for every case
 *
 * XScale supports clock switching, but using idle mode support
 * allows external hardware to react to system state changes.

 MMG: Come back to this one.
 */
	.align	5

ENTRY(cpu_xsc3_do_idle)
	mov	r0, #1
	mcr	p14, 0, r0, c7, c0, 0		@ Go to IDLE
	mov	pc, lr

/* ================================= CACHE ================================ */

/*
 *	flush_user_cache_all()
 *
 *	Invalidate all cache entries in a particular address
 *	space.
 */
ENTRY(xsc3_flush_user_cache_all)
	/* FALLTHROUGH */

/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
ENTRY(xsc3_flush_kern_cache_all)
	mov	r2, #VM_EXEC
	mov	ip, #0
__flush_whole_cache:
	clean_d_cache r0, r1
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c5, 0		@ Invalidate I cache & BTB
	mcrne	p15, 0, ip, c7, c10, 4		@ Drain Write Buffer
	mcrne	p15, 0, ip, c7, c5, 4		@ Prefetch Flush
	mov	pc, lr

/*
 *	flush_user_cache_range(start, end, vm_flags)
 *
 *	Invalidate a range of cache entries in the specified
 *	address space.
 *
 *	- start - start address (may not be aligned)
 *	- end	- end address (exclusive, may not be aligned)
 *	- vma	- vma_area_struct describing address space
 */
	.align	5
ENTRY(xsc3_flush_user_cache_range)
	mov	ip, #0
	sub	r3, r1, r0			@ calculate total size
	cmp	r3, #MAX_AREA_SIZE
	bhs	__flush_whole_cache

1:	tst	r2, #VM_EXEC
	mcrne	p15, 0, r0, c7, c5, 1		@ Invalidate I cache line
	mcr	p15, 0, r0, c7, c14, 1		@ Clean/invalidate D cache line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c5, 6		@ Invalidate BTB
	mcrne	p15, 0, ip, c7, c10, 4		@ Drain Write Buffer
	mcrne	p15, 0, ip, c7, c5, 4		@ Prefetch Flush
	mov	pc, lr

/*
 *	coherent_kern_range(start, end)
 *
 *	Ensure coherency between the Icache and the Dcache in the
 *	region described by start.  If you have non-snooping
 *	Harvard caches, you need to implement this function.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 *
 *	Note: single I-cache line invalidation isn't used here since
 *	it also trashes the mini I-cache used by JTAG debuggers.
 */
ENTRY(xsc3_coherent_kern_range)
/* FALLTHROUGH */
ENTRY(xsc3_coherent_user_range)
	bic	r0, r0, #CACHELINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ Invalidate I cache & BTB
	mcr	p15, 0, r0, c7, c10, 4		@ Drain Write Buffer
	mcr	p15, 0, r0, c7, c5, 4		@ Prefetch Flush
	mov	pc, lr

/*
 *	flush_kern_dcache_page(void *page)
 *
 *	Ensure no D cache aliasing occurs, either with itself or
 *	the I cache
 *
 *	- addr	- page aligned address
 */
ENTRY(xsc3_flush_kern_dcache_page)
	add	r1, r0, #PAGE_SZ
1:	mcr	p15, 0, r0, c7, c14, 1		@ Clean/Invalidate D Cache line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ Invalidate I cache & BTB
	mcr	p15, 0, r0, c7, c10, 4		@ Drain Write Buffer
	mcr	p15, 0, r0, c7, c5, 4		@ Prefetch Flush
	mov	pc, lr

/*
 *	dma_inv_range(start, end)
 *
 *	Invalidate (discard) the specified virtual address range.
 *	May not write back any entries.  If 'start' or 'end'
 *	are not cache line aligned, those lines must be written
 *	back.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(xsc3_dma_inv_range)
	tst	r0, #CACHELINESIZE - 1
	bic	r0, r0, #CACHELINESIZE - 1
	mcrne	p15, 0, r0, c7, c10, 1		@ clean L1 D entry
	mcrne	p15, 1, r0, c7, c11, 1		@ clean L2 D entry
	tst	r1, #CACHELINESIZE - 1
	mcrne	p15, 0, r1, c7, c10, 1		@ clean L1 D entry
	mcrne	p15, 1, r1, c7, c11, 1		@ clean L2 D entry
1:	mcr	p15, 0, r0, c7, c6, 1		@ invalidate L1 D entry
	mcr	p15, 1, r0, c7, c7, 1		@ Invalidate L2 D cache line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ Drain Write Buffer
	mov	pc, lr

/*
 *	dma_clean_range(start, end)
 *
 *	Clean the specified virtual address range.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(xsc3_dma_clean_range)
	bic	r0, r0, #CACHELINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean L1 D entry
	mcr	p15, 1, r0, c7, c11, 1		@ clean L2 D entry
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ Drain Write Buffer
	mov	pc, lr

/*
 *	dma_flush_range(start, end)
 *
 *	Clean and invalidate the specified virtual address range.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(xsc3_dma_flush_range)
	bic	r0, r0, #CACHELINESIZE - 1
1:	mcr	p15, 0, r0, c7, c14, 1	@ Clean/invalidate L1 D cache line
	mcr	p15, 1, r0, c7, c11, 1	@ Clean L2 D cache line
	mcr	p15, 1, r0, c7, c7, 1	@ Invalidate L2 D cache line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ Drain Write Buffer
	mov	pc, lr

ENTRY(xsc3_cache_fns)
	.long	xsc3_flush_kern_cache_all
	.long	xsc3_flush_user_cache_all
	.long	xsc3_flush_user_cache_range
	.long	xsc3_coherent_kern_range
	.long	xsc3_coherent_user_range
	.long	xsc3_flush_kern_dcache_page
	.long	xsc3_dma_inv_range
	.long	xsc3_dma_clean_range
	.long	xsc3_dma_flush_range

ENTRY(cpu_xsc3_dcache_clean_area)
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHELINESIZE
	subs	r1, r1, #CACHELINESIZE
	bhi	1b
	mov	pc, lr

/* =============================== PageTable ============================== */

/*
 * cpu_xsc3_switch_mm(pgd)
 *
 * Set the translation base pointer to be as described by pgd.
 *
 * pgd: new page tables
 */
	.align	5
ENTRY(cpu_xsc3_switch_mm)
	clean_d_cache r1, r2
	mcr	p15, 0, ip, c7, c5, 0		@ Invalidate I cache & BTB
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write Buffer
	mcr	p15, 0, ip, c7, c5, 4		@ Prefetch Flush
#ifdef L2_CACHE_ENABLE
	orr	r0, r0, #0x18			@ cache the page table in L2
#endif
	mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
	cpwait_ret lr, ip

/*
 * cpu_xsc3_set_pte(ptep, pte)
 *
 * Set a PTE and flush it out
 *
 */
	.align	5
ENTRY(cpu_xsc3_set_pte)
	str	r1, [r0], #-2048		@ linux version

	bic	r2, r1, #0xdf0			@ Keep C, B, coherency bits
	orr	r2, r2, #PTE_TYPE_EXT		@ extended page

	eor	r3, r1, #L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_WRITE | L_PTE_DIRTY

	tst	r3, #L_PTE_USER			@ User?
	orrne	r2, r2, #PTE_EXT_AP_URO_SRW	@ yes -> user r/o, system r/w

	tst	r3, #L_PTE_WRITE | L_PTE_DIRTY	@ Write and Dirty?
	orreq	r2, r2, #PTE_EXT_AP_UNO_SRW	@ yes -> user n/a, system r/w
						@ combined with user -> user r/w

#if L2_CACHE_ENABLE
	@ If its cacheable it needs to be in L2 also.
	eor	ip, r1, #L_PTE_CACHEABLE
	tst	ip, #L_PTE_CACHEABLE
	orreq	r2, r2, #PTE_EXT_TEX(0x5)
#endif

	tst	r3, #L_PTE_PRESENT | L_PTE_YOUNG	@ Present and Young?
	movne	r2, #0				@ no -> fault

	str	r2, [r0]			@ hardware version
	mov	ip, #0
	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line mcr
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write Buffer
	mov	pc, lr

	.ltorg

	.align

	__INIT

	.type	__xsc3_setup, #function
__xsc3_setup:
	mov	r0, #PSR_F_BIT|PSR_I_BIT|SVC_MODE
	msr	cpsr_c, r0
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I, D caches & BTB
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write Buffer
	mcr	p15, 0, ip, c7, c5, 4		@ Prefetch Flush
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I, D TLBs
#if L2_CACHE_ENABLE
	orr	r4, r4, #0x18			@ cache the page table in L2
#endif
	mcr	p15, 0, r4, c2, c0, 0		@ load page table pointer
	mov	r0, #1				@ Allow access to CP0 and CP13
	orr	r0, r0, #1 << 13		@ Its undefined whether this
	mcr	p15, 0, r0, c15, c1, 0		@ affects USR or SVC modes
	mrc	p15, 0, r0, c1, c0, 1		@ get auxiliary control reg
	and	r0, r0, #2			@ preserve bit P bit setting
#if L2_CACHE_ENABLE
	orr	r0, r0, #(1 << 10)		@ enable L2 for LLR cache
#endif
	mcr	p15, 0, r0, c1, c0, 1		@ set auxiliary control reg
	mrc	p15, 0, r0, c1, c0, 0		@ get control register
	bic	r0, r0, #0x0200			@ .... ..R. .... ....
	bic	r0, r0, #0x0002			@ .... .... .... ..A.
	orr	r0, r0, #0x0005			@ .... .... .... .C.M
#if BTB_ENABLE
	orr	r0, r0, #0x3900			@ ..VI Z..S .... ....
#else
	orr	r0, r0, #0x3100			@ ..VI ...S .... ....
#endif
#if L2_CACHE_ENABLE
	orr 	r0, r0, #0x4000000		@ L2 enable
#endif
	mov	pc, lr

	.size	__xsc3_setup, . - __xsc3_setup

	__INITDATA

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */

	.type	xsc3_processor_functions, #object
ENTRY(xsc3_processor_functions)
	.word	v5t_early_abort
	.word	cpu_xsc3_proc_init
	.word	cpu_xsc3_proc_fin
	.word	cpu_xsc3_reset
	.word	cpu_xsc3_do_idle
	.word	cpu_xsc3_dcache_clean_area
	.word	cpu_xsc3_switch_mm
	.word	cpu_xsc3_set_pte
	.size	xsc3_processor_functions, . - xsc3_processor_functions

	.section ".rodata"

	.type	cpu_arch_name, #object
cpu_arch_name:
	.asciz	"armv5te"
	.size	cpu_arch_name, . - cpu_arch_name

	.type	cpu_elf_name, #object
cpu_elf_name:
	.asciz	"v5"
	.size	cpu_elf_name, . - cpu_elf_name

	.type	cpu_xsc3_name, #object
cpu_xsc3_name:
	.asciz	"XScale-Core3"
	.size	cpu_xsc3_name, . - cpu_xsc3_name

	.align

	.section ".proc.info.init", #alloc, #execinstr

	.type	__xsc3_proc_info,#object
__xsc3_proc_info:
	.long	0x69056000
	.long	0xffffe000
	.long	0x00000c0e
	b	__xsc3_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_EDSP
	.long	cpu_xsc3_name
	.long	xsc3_processor_functions
	.long	v4wbi_tlb_fns
	.long	xsc3_mc_user_fns
	.long	xsc3_cache_fns
	.size	__xsc3_proc_info, . - __xsc3_proc_info
