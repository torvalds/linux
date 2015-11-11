/*
 *  linux/arch/arm/mm/proc-xscale.S
 *
 *  Author:	Nicolas Pitre
 *  Created:	November 2000
 *  Copyright:	(C) 2000, 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MMU functions for the Intel XScale CPUs
 *
 * 2001 Aug 21:
 *	some contributions by Brett Gaines <brett.w.gaines@intel.com>
 *	Copyright 2001 by Intel Corp.
 *
 * 2001 Sep 08:
 *	Completely revisited, many important fixes
 *	Nicolas Pitre <nico@fluxnic.net>
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/hwcap.h>
#include <asm/pgtable.h>
#include <asm/pgtable-hwdef.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include "proc-macros.S"

/*
 * This is the maximum size of an area which will be flushed.  If the area
 * is larger than this, then we flush the whole cache
 */
#define MAX_AREA_SIZE	32768

/*
 * the cache line size of the I and D cache
 */
#define CACHELINESIZE	32

/*
 * the size of the data cache
 */
#define CACHESIZE	32768

/*
 * Virtual address used to allocate the cache when flushed
 *
 * This must be an address range which is _never_ used.  It should
 * apparently have a mapping in the corresponding page table for
 * compatibility with future CPUs that _could_ require it.  For instance we
 * don't care.
 *
 * This must be aligned on a 2*CACHESIZE boundary.  The code selects one of
 * the 2 areas in alternance each time the clean_d_cache macro is used.
 * Without this the XScale core exhibits cache eviction problems and no one
 * knows why.
 *
 * Reminder: the vector table is located at 0xffff0000-0xffff0fff.
 */
#define CLEAN_ADDR	0xfffe0000

/*
 * This macro is used to wait for a CP15 write and is needed
 * when we have to ensure that the last operation to the co-pro
 * was completed before continuing with operation.
 */
	.macro	cpwait, rd
	mrc	p15, 0, \rd, c2, c0, 0		@ arbitrary read of cp15
	mov	\rd, \rd			@ wait for completion
	sub 	pc, pc, #4			@ flush instruction pipeline
	.endm

	.macro	cpwait_ret, lr, rd
	mrc	p15, 0, \rd, c2, c0, 0		@ arbitrary read of cp15
	sub	pc, \lr, \rd, LSR #32		@ wait for completion and
						@ flush instruction pipeline
	.endm

/*
 * This macro cleans the entire dcache using line allocate.
 * The main loop has been unrolled to reduce loop overhead.
 * rd and rs are two scratch registers.
 */
	.macro  clean_d_cache, rd, rs
	ldr	\rs, =clean_addr
	ldr	\rd, [\rs]
	eor	\rd, \rd, #CACHESIZE
	str	\rd, [\rs]
	add	\rs, \rd, #CACHESIZE
1:	mcr	p15, 0, \rd, c7, c2, 5		@ allocate D cache line
	add	\rd, \rd, #CACHELINESIZE
	mcr	p15, 0, \rd, c7, c2, 5		@ allocate D cache line
	add	\rd, \rd, #CACHELINESIZE
	mcr	p15, 0, \rd, c7, c2, 5		@ allocate D cache line
	add	\rd, \rd, #CACHELINESIZE
	mcr	p15, 0, \rd, c7, c2, 5		@ allocate D cache line
	add	\rd, \rd, #CACHELINESIZE
	teq	\rd, \rs
	bne	1b
	.endm

	.data
clean_addr:	.word	CLEAN_ADDR

	.text

/*
 * cpu_xscale_proc_init()
 *
 * Nothing too exciting at the moment
 */
ENTRY(cpu_xscale_proc_init)
	@ enable write buffer coalescing. Some bootloader disable it
	mrc	p15, 0, r1, c1, c0, 1
	bic	r1, r1, #1
	mcr	p15, 0, r1, c1, c0, 1
	ret	lr

/*
 * cpu_xscale_proc_fin()
 */
ENTRY(cpu_xscale_proc_fin)
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1800			@ ...IZ...........
	bic	r0, r0, #0x0006			@ .............CA.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	ret	lr

/*
 * cpu_xscale_reset(loc)
 *
 * Perform a soft reset of the system.  Put the CPU into the
 * same state as it would be if it had been reset, and branch
 * to what would be the reset vector.
 *
 * loc: location to jump to for soft reset
 *
 * Beware PXA270 erratum E7.
 */
	.align	5
	.pushsection	.idmap.text, "ax"
ENTRY(cpu_xscale_reset)
	mov	r1, #PSR_F_BIT|PSR_I_BIT|SVC_MODE
	msr	cpsr_c, r1			@ reset CPSR
	mcr	p15, 0, r1, c10, c4, 1		@ unlock I-TLB
	mcr	p15, 0, r1, c8, c5, 0		@ invalidate I-TLB
	mrc	p15, 0, r1, c1, c0, 0		@ ctrl register
	bic	r1, r1, #0x0086			@ ........B....CA.
	bic	r1, r1, #0x3900			@ ..VIZ..S........
	sub	pc, pc, #4			@ flush pipeline
	@ *** cache line aligned ***
	mcr	p15, 0, r1, c1, c0, 0		@ ctrl register
	bic	r1, r1, #0x0001			@ ...............M
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I,D caches & BTB
	mcr	p15, 0, r1, c1, c0, 0		@ ctrl register
	@ CAUTION: MMU turned off from this point. We count on the pipeline
	@ already containing those two last instructions to survive.
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
	ret	r0
ENDPROC(cpu_xscale_reset)
	.popsection

/*
 * cpu_xscale_do_idle()
 *
 * Cause the processor to idle
 *
 * For now we do nothing but go to idle mode for every case
 *
 * XScale supports clock switching, but using idle mode support
 * allows external hardware to react to system state changes.
 */
	.align	5

ENTRY(cpu_xscale_do_idle)
	mov	r0, #1
	mcr	p14, 0, r0, c7, c0, 0		@ Go to IDLE
	ret	lr

/* ================================= CACHE ================================ */

/*
 *	flush_icache_all()
 *
 *	Unconditionally clean and invalidate the entire icache.
 */
ENTRY(xscale_flush_icache_all)
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	ret	lr
ENDPROC(xscale_flush_icache_all)

/*
 *	flush_user_cache_all()
 *
 *	Invalidate all cache entries in a particular address
 *	space.
 */
ENTRY(xscale_flush_user_cache_all)
	/* FALLTHROUGH */

/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
ENTRY(xscale_flush_kern_cache_all)
	mov	r2, #VM_EXEC
	mov	ip, #0
__flush_whole_cache:
	clean_d_cache r0, r1
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c5, 0		@ Invalidate I cache & BTB
	mcrne	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	ret	lr

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
ENTRY(xscale_flush_user_cache_range)
	mov	ip, #0
	sub	r3, r1, r0			@ calculate total size
	cmp	r3, #MAX_AREA_SIZE
	bhs	__flush_whole_cache

1:	tst	r2, #VM_EXEC
	mcrne	p15, 0, r0, c7, c5, 1		@ Invalidate I cache line
	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	mcr	p15, 0, r0, c7, c6, 1		@ Invalidate D cache line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c5, 6		@ Invalidate BTB
	mcrne	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	ret	lr

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
ENTRY(xscale_coherent_kern_range)
	bic	r0, r0, #CACHELINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ Invalidate I cache & BTB
	mcr	p15, 0, r0, c7, c10, 4		@ Drain Write (& Fill) Buffer
	ret	lr

/*
 *	coherent_user_range(start, end)
 *
 *	Ensure coherency between the Icache and the Dcache in the
 *	region described by start.  If you have non-snooping
 *	Harvard caches, you need to implement this function.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(xscale_coherent_user_range)
	bic	r0, r0, #CACHELINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c5, 1		@ Invalidate I cache entry
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 6		@ Invalidate BTB
	mcr	p15, 0, r0, c7, c10, 4		@ Drain Write (& Fill) Buffer
	ret	lr

/*
 *	flush_kern_dcache_area(void *addr, size_t size)
 *
 *	Ensure no D cache aliasing occurs, either with itself or
 *	the I cache
 *
 *	- addr	- kernel address
 *	- size	- region size
 */
ENTRY(xscale_flush_kern_dcache_area)
	add	r1, r0, r1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ Invalidate I cache & BTB
	mcr	p15, 0, r0, c7, c10, 4		@ Drain Write (& Fill) Buffer
	ret	lr

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
xscale_dma_inv_range:
	tst	r0, #CACHELINESIZE - 1
	bic	r0, r0, #CACHELINESIZE - 1
	mcrne	p15, 0, r0, c7, c10, 1		@ clean D entry
	tst	r1, #CACHELINESIZE - 1
	mcrne	p15, 0, r1, c7, c10, 1		@ clean D entry
1:	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ Drain Write (& Fill) Buffer
	ret	lr

/*
 *	dma_clean_range(start, end)
 *
 *	Clean the specified virtual address range.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
xscale_dma_clean_range:
	bic	r0, r0, #CACHELINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ Drain Write (& Fill) Buffer
	ret	lr

/*
 *	dma_flush_range(start, end)
 *
 *	Clean and invalidate the specified virtual address range.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(xscale_dma_flush_range)
	bic	r0, r0, #CACHELINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ Drain Write (& Fill) Buffer
	ret	lr

/*
 *	dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(xscale_dma_map_area)
	add	r1, r1, r0
	cmp	r2, #DMA_TO_DEVICE
	beq	xscale_dma_clean_range
	bcs	xscale_dma_inv_range
	b	xscale_dma_flush_range
ENDPROC(xscale_dma_map_area)

/*
 *	dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(xscale_80200_A0_A1_dma_map_area)
	add	r1, r1, r0
	teq	r2, #DMA_TO_DEVICE
	beq	xscale_dma_clean_range
	b	xscale_dma_flush_range
ENDPROC(xscale_80200_A0_A1_dma_map_area)

/*
 *	dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(xscale_dma_unmap_area)
	ret	lr
ENDPROC(xscale_dma_unmap_area)

	.globl	xscale_flush_kern_cache_louis
	.equ	xscale_flush_kern_cache_louis, xscale_flush_kern_cache_all

	@ define struct cpu_cache_fns (see <asm/cacheflush.h> and proc-macros.S)
	define_cache_functions xscale

/*
 * On stepping A0/A1 of the 80200, invalidating D-cache by line doesn't
 * clear the dirty bits, which means that if we invalidate a dirty line,
 * the dirty data can still be written back to external memory later on.
 *
 * The recommended workaround is to always do a clean D-cache line before
 * doing an invalidate D-cache line, so on the affected processors,
 * dma_inv_range() is implemented as dma_flush_range().
 *
 * See erratum #25 of "Intel 80200 Processor Specification Update",
 * revision January 22, 2003, available at:
 *     http://www.intel.com/design/iio/specupdt/273415.htm
 */
.macro a0_alias basename
	.globl xscale_80200_A0_A1_\basename
	.type xscale_80200_A0_A1_\basename , %function
	.equ xscale_80200_A0_A1_\basename , xscale_\basename
.endm

/*
 * Most of the cache functions are unchanged for these processor revisions.
 * Export suitable alias symbols for the unchanged functions:
 */
	a0_alias flush_icache_all
	a0_alias flush_user_cache_all
	a0_alias flush_kern_cache_all
	a0_alias flush_kern_cache_louis
	a0_alias flush_user_cache_range
	a0_alias coherent_kern_range
	a0_alias coherent_user_range
	a0_alias flush_kern_dcache_area
	a0_alias dma_flush_range
	a0_alias dma_unmap_area

	@ define struct cpu_cache_fns (see <asm/cacheflush.h> and proc-macros.S)
	define_cache_functions xscale_80200_A0_A1

ENTRY(cpu_xscale_dcache_clean_area)
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHELINESIZE
	subs	r1, r1, #CACHELINESIZE
	bhi	1b
	ret	lr

/* =============================== PageTable ============================== */

/*
 * cpu_xscale_switch_mm(pgd)
 *
 * Set the translation base pointer to be as described by pgd.
 *
 * pgd: new page tables
 */
	.align	5
ENTRY(cpu_xscale_switch_mm)
	clean_d_cache r1, r2
	mcr	p15, 0, ip, c7, c5, 0		@ Invalidate I cache & BTB
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
	cpwait_ret lr, ip

/*
 * cpu_xscale_set_pte_ext(ptep, pte, ext)
 *
 * Set a PTE and flush it out
 *
 * Errata 40: must set memory to write-through for user read-only pages.
 */
cpu_xscale_mt_table:
	.long	0x00						@ L_PTE_MT_UNCACHED
	.long	PTE_BUFFERABLE					@ L_PTE_MT_BUFFERABLE
	.long	PTE_CACHEABLE					@ L_PTE_MT_WRITETHROUGH
	.long	PTE_CACHEABLE | PTE_BUFFERABLE			@ L_PTE_MT_WRITEBACK
	.long	PTE_EXT_TEX(1) | PTE_BUFFERABLE			@ L_PTE_MT_DEV_SHARED
	.long	0x00						@ unused
	.long	PTE_EXT_TEX(1) | PTE_CACHEABLE			@ L_PTE_MT_MINICACHE
	.long	PTE_EXT_TEX(1) | PTE_CACHEABLE | PTE_BUFFERABLE	@ L_PTE_MT_WRITEALLOC
	.long	0x00						@ unused
	.long	PTE_BUFFERABLE					@ L_PTE_MT_DEV_WC
	.long	0x00						@ unused
	.long	PTE_CACHEABLE | PTE_BUFFERABLE			@ L_PTE_MT_DEV_CACHED
	.long	0x00						@ L_PTE_MT_DEV_NONSHARED
	.long	0x00						@ unused
	.long	0x00						@ unused
	.long	0x00						@ unused

	.align	5
ENTRY(cpu_xscale_set_pte_ext)
	xscale_set_pte_ext_prologue

	@
	@ Erratum 40: must set memory to write-through for user read-only pages
	@
	and	ip, r1, #(L_PTE_MT_MASK | L_PTE_USER | L_PTE_RDONLY) & ~(4 << 2)
	teq	ip, #L_PTE_MT_WRITEBACK | L_PTE_USER | L_PTE_RDONLY

	moveq	r1, #L_PTE_MT_WRITETHROUGH
	and	r1, r1, #L_PTE_MT_MASK
	adr	ip, cpu_xscale_mt_table
	ldr	ip, [ip, r1]
	bic	r2, r2, #0x0c
	orr	r2, r2, ip

	xscale_set_pte_ext_epilogue
	ret	lr

	.ltorg
	.align

.globl	cpu_xscale_suspend_size
.equ	cpu_xscale_suspend_size, 4 * 6
#ifdef CONFIG_ARM_CPU_SUSPEND
ENTRY(cpu_xscale_do_suspend)
	stmfd	sp!, {r4 - r9, lr}
	mrc	p14, 0, r4, c6, c0, 0	@ clock configuration, for turbo mode
	mrc	p15, 0, r5, c15, c1, 0	@ CP access reg
	mrc	p15, 0, r6, c13, c0, 0	@ PID
	mrc	p15, 0, r7, c3, c0, 0	@ domain ID
	mrc	p15, 0, r8, c1, c0, 1	@ auxiliary control reg
	mrc	p15, 0, r9, c1, c0, 0	@ control reg
	bic	r4, r4, #2		@ clear frequency change bit
	stmia	r0, {r4 - r9}		@ store cp regs
	ldmfd	sp!, {r4 - r9, pc}
ENDPROC(cpu_xscale_do_suspend)

ENTRY(cpu_xscale_do_resume)
	ldmia	r0, {r4 - r9}		@ load cp regs
	mov	ip, #0
	mcr	p15, 0, ip, c8, c7, 0	@ invalidate I & D TLBs
	mcr	p15, 0, ip, c7, c7, 0	@ invalidate I & D caches, BTB
	mcr	p14, 0, r4, c6, c0, 0	@ clock configuration, turbo mode.
	mcr	p15, 0, r5, c15, c1, 0	@ CP access reg
	mcr	p15, 0, r6, c13, c0, 0	@ PID
	mcr	p15, 0, r7, c3, c0, 0	@ domain ID
	mcr	p15, 0, r1, c2, c0, 0	@ translation table base addr
	mcr	p15, 0, r8, c1, c0, 1	@ auxiliary control reg
	mov	r0, r9			@ control register
	b	cpu_resume_mmu
ENDPROC(cpu_xscale_do_resume)
#endif

	.type	__xscale_setup, #function
__xscale_setup:
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I, D caches & BTB
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I, D TLBs
	mov	r0, #1 << 6			@ cp6 for IOP3xx and Bulverde
	orr	r0, r0, #1 << 13		@ Its undefined whether this
	mcr	p15, 0, r0, c15, c1, 0		@ affects USR or SVC modes

	adr	r5, xscale_crval
	ldmia	r5, {r5, r6}
	mrc	p15, 0, r0, c1, c0, 0		@ get control register
	bic	r0, r0, r5
	orr	r0, r0, r6
	ret	lr
	.size	__xscale_setup, . - __xscale_setup

	/*
	 *  R
	 * .RVI ZFRS BLDP WCAM
	 * ..11 1.01 .... .101
	 * 
	 */
	.type	xscale_crval, #object
xscale_crval:
	crval	clear=0x00003b07, mmuset=0x00003905, ucset=0x00001900

	__INITDATA

	@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
	define_processor_functions xscale, dabort=v5t_early_abort, pabort=legacy_pabort, suspend=1

	.section ".rodata"

	string	cpu_arch_name, "armv5te"
	string	cpu_elf_name, "v5"

	string	cpu_80200_A0_A1_name, "XScale-80200 A0/A1"
	string	cpu_80200_name, "XScale-80200"
	string	cpu_80219_name, "XScale-80219"
	string	cpu_8032x_name, "XScale-IOP8032x Family"
	string	cpu_8033x_name, "XScale-IOP8033x Family"
	string	cpu_pxa250_name, "XScale-PXA250"
	string	cpu_pxa210_name, "XScale-PXA210"
	string	cpu_ixp42x_name, "XScale-IXP42x Family"
	string	cpu_ixp43x_name, "XScale-IXP43x Family"
	string	cpu_ixp46x_name, "XScale-IXP46x Family"
	string	cpu_ixp2400_name, "XScale-IXP2400"
	string	cpu_ixp2800_name, "XScale-IXP2800"
	string	cpu_pxa255_name, "XScale-PXA255"
	string	cpu_pxa270_name, "XScale-PXA270"

	.align

	.section ".proc.info.init", #alloc

.macro xscale_proc_info name:req, cpu_val:req, cpu_mask:req, cpu_name:req, cache
	.type	__\name\()_proc_info,#object
__\name\()_proc_info:
	.long	\cpu_val
	.long	\cpu_mask
	.long	PMD_TYPE_SECT | \
		PMD_SECT_BUFFERABLE | \
		PMD_SECT_CACHEABLE | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	.long	PMD_TYPE_SECT | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	initfn	__xscale_setup, __\name\()_proc_info
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_EDSP
	.long	\cpu_name
	.long	xscale_processor_functions
	.long	v4wbi_tlb_fns
	.long	xscale_mc_user_fns
	.ifb \cache
		.long	xscale_cache_fns
	.else
		.long	\cache
	.endif
	.size	__\name\()_proc_info, . - __\name\()_proc_info
.endm

	xscale_proc_info 80200_A0_A1, 0x69052000, 0xfffffffe, cpu_80200_name, \
		cache=xscale_80200_A0_A1_cache_fns
	xscale_proc_info 80200, 0x69052000, 0xfffffff0, cpu_80200_name
	xscale_proc_info 80219, 0x69052e20, 0xffffffe0, cpu_80219_name
	xscale_proc_info 8032x, 0x69052420, 0xfffff7e0, cpu_8032x_name
	xscale_proc_info 8033x, 0x69054010, 0xfffffd30, cpu_8033x_name
	xscale_proc_info pxa250, 0x69052100, 0xfffff7f0, cpu_pxa250_name
	xscale_proc_info pxa210, 0x69052120, 0xfffff3f0, cpu_pxa210_name
	xscale_proc_info ixp2400, 0x69054190, 0xfffffff0, cpu_ixp2400_name
	xscale_proc_info ixp2800, 0x690541a0, 0xfffffff0, cpu_ixp2800_name
	xscale_proc_info ixp42x, 0x690541c0, 0xffffffc0, cpu_ixp42x_name
	xscale_proc_info ixp43x, 0x69054040, 0xfffffff0, cpu_ixp43x_name
	xscale_proc_info ixp46x, 0x69054200, 0xffffff00, cpu_ixp46x_name
	xscale_proc_info pxa255, 0x69052d00, 0xfffffff0, cpu_pxa255_name
	xscale_proc_info pxa270, 0x69054110, 0xfffffff0, cpu_pxa270_name
