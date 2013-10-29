/*
 * linux/arch/arm/mm/proc-xsc3.S
 *
 * Original Author: Matthew Gilbert
 * Current Maintainer: Lennert Buytenhek <buytenh@wantstofly.org>
 *
 * Copyright 2004 (C) Intel Corp.
 * Copyright 2005 (C) MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MMU functions for the Intel XScale3 Core (XSC3).  The XSC3 core is
 * an extension to Intel's original XScale core that adds the following
 * features:
 *
 * - ARMv6 Supersections
 * - Low Locality Reference pages (replaces mini-cache)
 * - 36-bit addressing
 * - L2 cache
 * - Cache coherency if chipset supports it
 *
 * Based on original XScale code by Nicolas Pitre.
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
 * This is the maximum size of an area which will be flushed.  If the
 * area is larger than this, then we flush the whole cache.
 */
#define MAX_AREA_SIZE	32768

/*
 * The cache line size of the L1 I, L1 D and unified L2 cache.
 */
#define CACHELINESIZE	32

/*
 * The size of the L1 D cache.
 */
#define CACHESIZE	32768

/*
 * This macro is used to wait for a CP15 write and is needed when we
 * have to ensure that the last operation to the coprocessor was
 * completed before continuing with operation.
 */
	.macro	cpwait_ret, lr, rd
	mrc	p15, 0, \rd, c2, c0, 0		@ arbitrary read of cp15
	sub	pc, \lr, \rd, LSR #32		@ wait for completion and
						@ flush instruction pipeline
	.endm

/*
 * This macro cleans and invalidates the entire L1 D cache.
 */

 	.macro  clean_d_cache rd, rs
	mov	\rd, #0x1f00
	orr	\rd, \rd, #0x00e0
1:	mcr	p15, 0, \rd, c7, c14, 2		@ clean/invalidate L1 D line
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
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1800			@ ...IZ...........
	bic	r0, r0, #0x0006			@ .............CA.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	mov	pc, lr

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
	.pushsection	.idmap.text, "ax"
ENTRY(cpu_xsc3_reset)
	mov	r1, #PSR_F_BIT|PSR_I_BIT|SVC_MODE
	msr	cpsr_c, r1			@ reset CPSR
	mrc	p15, 0, r1, c1, c0, 0		@ ctrl register
	bic	r1, r1, #0x3900			@ ..VIZ..S........
	bic	r1, r1, #0x0086			@ ........B....CA.
	mcr	p15, 0, r1, c1, c0, 0		@ ctrl register
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate L1 caches and BTB
	bic	r1, r1, #0x0001			@ ...............M
	mcr	p15, 0, r1, c1, c0, 0		@ ctrl register
	@ CAUTION: MMU turned off from this point.  We count on the pipeline
	@ already containing those two last instructions to survive.
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I and D TLBs
	mov	pc, r0
ENDPROC(cpu_xsc3_reset)
	.popsection

/*
 * cpu_xsc3_do_idle()
 *
 * Cause the processor to idle
 *
 * For now we do nothing but go to idle mode for every case
 *
 * XScale supports clock switching, but using idle mode support
 * allows external hardware to react to system state changes.
 */
	.align	5

ENTRY(cpu_xsc3_do_idle)
	mov	r0, #1
	mcr	p14, 0, r0, c7, c0, 0		@ go to idle
	mov	pc, lr

/* ================================= CACHE ================================ */

/*
 *	flush_icache_all()
 *
 *	Unconditionally clean and invalidate the entire icache.
 */
ENTRY(xsc3_flush_icache_all)
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	mov	pc, lr
ENDPROC(xsc3_flush_icache_all)

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
	mcrne	p15, 0, ip, c7, c5, 0		@ invalidate L1 I cache and BTB
	mcrne	p15, 0, ip, c7, c10, 4		@ data write barrier
	mcrne	p15, 0, ip, c7, c5, 4		@ prefetch flush
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
	mcrne	p15, 0, r0, c7, c5, 1		@ invalidate L1 I line
	mcr	p15, 0, r0, c7, c14, 1		@ clean/invalidate L1 D line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c5, 6		@ invalidate BTB
	mcrne	p15, 0, ip, c7, c10, 4		@ data write barrier
	mcrne	p15, 0, ip, c7, c5, 4		@ prefetch flush
	mov	pc, lr

/*
 *	coherent_kern_range(start, end)
 *
 *	Ensure coherency between the I cache and the D cache in the
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
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean L1 D line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate L1 I cache and BTB
	mcr	p15, 0, r0, c7, c10, 4		@ data write barrier
	mcr	p15, 0, r0, c7, c5, 4		@ prefetch flush
	mov	pc, lr

/*
 *	flush_kern_dcache_area(void *addr, size_t size)
 *
 *	Ensure no D cache aliasing occurs, either with itself or
 *	the I cache.
 *
 *	- addr	- kernel address
 *	- size	- region size
 */
ENTRY(xsc3_flush_kern_dcache_area)
	add	r1, r0, r1
1:	mcr	p15, 0, r0, c7, c14, 1		@ clean/invalidate L1 D line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate L1 I cache and BTB
	mcr	p15, 0, r0, c7, c10, 4		@ data write barrier
	mcr	p15, 0, r0, c7, c5, 4		@ prefetch flush
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
xsc3_dma_inv_range:
	tst	r0, #CACHELINESIZE - 1
	bic	r0, r0, #CACHELINESIZE - 1
	mcrne	p15, 0, r0, c7, c10, 1		@ clean L1 D line
	tst	r1, #CACHELINESIZE - 1
	mcrne	p15, 0, r1, c7, c10, 1		@ clean L1 D line
1:	mcr	p15, 0, r0, c7, c6, 1		@ invalidate L1 D line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ data write barrier
	mov	pc, lr

/*
 *	dma_clean_range(start, end)
 *
 *	Clean the specified virtual address range.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
xsc3_dma_clean_range:
	bic	r0, r0, #CACHELINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean L1 D line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ data write barrier
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
1:	mcr	p15, 0, r0, c7, c14, 1		@ clean/invalidate L1 D line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ data write barrier
	mov	pc, lr

/*
 *	dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(xsc3_dma_map_area)
	add	r1, r1, r0
	cmp	r2, #DMA_TO_DEVICE
	beq	xsc3_dma_clean_range
	bcs	xsc3_dma_inv_range
	b	xsc3_dma_flush_range
ENDPROC(xsc3_dma_map_area)

/*
 *	dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(xsc3_dma_unmap_area)
	mov	pc, lr
ENDPROC(xsc3_dma_unmap_area)

	@ define struct cpu_cache_fns (see <asm/cacheflush.h> and proc-macros.S)
	define_cache_functions xsc3

ENTRY(cpu_xsc3_dcache_clean_area)
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean L1 D line
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
	mcr	p15, 0, ip, c7, c5, 0		@ invalidate L1 I cache and BTB
	mcr	p15, 0, ip, c7, c10, 4		@ data write barrier
	mcr	p15, 0, ip, c7, c5, 4		@ prefetch flush
	orr	r0, r0, #0x18			@ cache the page table in L2
	mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I and D TLBs
	cpwait_ret lr, ip

/*
 * cpu_xsc3_set_pte_ext(ptep, pte, ext)
 *
 * Set a PTE and flush it out
 */
cpu_xsc3_mt_table:
	.long	0x00						@ L_PTE_MT_UNCACHED
	.long	PTE_EXT_TEX(1)					@ L_PTE_MT_BUFFERABLE
	.long	PTE_EXT_TEX(5) | PTE_CACHEABLE			@ L_PTE_MT_WRITETHROUGH
	.long	PTE_CACHEABLE | PTE_BUFFERABLE			@ L_PTE_MT_WRITEBACK
	.long	PTE_EXT_TEX(1) | PTE_BUFFERABLE			@ L_PTE_MT_DEV_SHARED
	.long	0x00						@ unused
	.long	0x00						@ L_PTE_MT_MINICACHE (not present)
	.long	PTE_EXT_TEX(5) | PTE_CACHEABLE | PTE_BUFFERABLE	@ L_PTE_MT_WRITEALLOC (not present?)
	.long	0x00						@ unused
	.long	PTE_EXT_TEX(1)					@ L_PTE_MT_DEV_WC
	.long	0x00						@ unused
	.long	PTE_CACHEABLE | PTE_BUFFERABLE			@ L_PTE_MT_DEV_CACHED
	.long	PTE_EXT_TEX(2)					@ L_PTE_MT_DEV_NONSHARED
	.long	0x00						@ unused
	.long	0x00						@ unused
	.long	0x00						@ unused

	.align	5
ENTRY(cpu_xsc3_set_pte_ext)
	xscale_set_pte_ext_prologue

	tst	r1, #L_PTE_SHARED		@ shared?
	and	r1, r1, #L_PTE_MT_MASK
	adr	ip, cpu_xsc3_mt_table
	ldr	ip, [ip, r1]
	orrne	r2, r2, #PTE_EXT_COHERENT	@ interlock: mask in coherent bit
	bic	r2, r2, #0x0c			@ clear old C,B bits
	orr	r2, r2, ip

	xscale_set_pte_ext_epilogue
	mov	pc, lr

	.ltorg
	.align

.globl	cpu_xsc3_suspend_size
.equ	cpu_xsc3_suspend_size, 4 * 6
#ifdef CONFIG_ARM_CPU_SUSPEND
ENTRY(cpu_xsc3_do_suspend)
	stmfd	sp!, {r4 - r9, lr}
	mrc	p14, 0, r4, c6, c0, 0	@ clock configuration, for turbo mode
	mrc	p15, 0, r5, c15, c1, 0	@ CP access reg
	mrc	p15, 0, r6, c13, c0, 0	@ PID
	mrc 	p15, 0, r7, c3, c0, 0	@ domain ID
	mrc	p15, 0, r8, c1, c0, 1	@ auxiliary control reg
	mrc 	p15, 0, r9, c1, c0, 0	@ control reg
	bic	r4, r4, #2		@ clear frequency change bit
	stmia	r0, {r4 - r9}		@ store cp regs
	ldmia	sp!, {r4 - r9, pc}
ENDPROC(cpu_xsc3_do_suspend)

ENTRY(cpu_xsc3_do_resume)
	ldmia	r0, {r4 - r9}		@ load cp regs
	mov	ip, #0
	mcr	p15, 0, ip, c7, c7, 0	@ invalidate I & D caches, BTB
	mcr	p15, 0, ip, c7, c10, 4	@ drain write (&fill) buffer
	mcr	p15, 0, ip, c7, c5, 4	@ flush prefetch buffer
	mcr	p15, 0, ip, c8, c7, 0	@ invalidate I & D TLBs
	mcr	p14, 0, r4, c6, c0, 0	@ clock configuration, turbo mode.
	mcr	p15, 0, r5, c15, c1, 0	@ CP access reg
	mcr	p15, 0, r6, c13, c0, 0	@ PID
	mcr	p15, 0, r7, c3, c0, 0	@ domain ID
	orr	r1, r1, #0x18		@ cache the page table in L2
	mcr	p15, 0, r1, c2, c0, 0	@ translation table base addr
	mcr	p15, 0, r8, c1, c0, 1	@ auxiliary control reg
	mov	r0, r9			@ control register
	b	cpu_resume_mmu
ENDPROC(cpu_xsc3_do_resume)
#endif

	__CPUINIT

	.type	__xsc3_setup, #function
__xsc3_setup:
	mov	r0, #PSR_F_BIT|PSR_I_BIT|SVC_MODE
	msr	cpsr_c, r0
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate L1 caches and BTB
	mcr	p15, 0, ip, c7, c10, 4		@ data write barrier
	mcr	p15, 0, ip, c7, c5, 4		@ prefetch flush
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I and D TLBs
	orr	r4, r4, #0x18			@ cache the page table in L2
	mcr	p15, 0, r4, c2, c0, 0		@ load page table pointer

	mov	r0, #1 << 6			@ cp6 access for early sched_clock
	mcr	p15, 0, r0, c15, c1, 0		@ write CP access register

	mrc	p15, 0, r0, c1, c0, 1		@ get auxiliary control reg
	and	r0, r0, #2			@ preserve bit P bit setting
	orr	r0, r0, #(1 << 10)		@ enable L2 for LLR cache
	mcr	p15, 0, r0, c1, c0, 1		@ set auxiliary control reg

	adr	r5, xsc3_crval
	ldmia	r5, {r5, r6}

#ifdef CONFIG_CACHE_XSC3L2
	mrc	p15, 1, r0, c0, c0, 1		@ get L2 present information
	ands	r0, r0, #0xf8
	orrne	r6, r6, #(1 << 26)		@ enable L2 if present
#endif

	mrc	p15, 0, r0, c1, c0, 0		@ get control register
	bic	r0, r0, r5			@ ..V. ..R. .... ..A.
	orr	r0, r0, r6			@ ..VI Z..S .... .C.M (mmu)
						@ ...I Z..S .... .... (uc)
	mov	pc, lr

	.size	__xsc3_setup, . - __xsc3_setup

	.type	xsc3_crval, #object
xsc3_crval:
	crval	clear=0x04002202, mmuset=0x00003905, ucset=0x00001900

	__INITDATA

	@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
	define_processor_functions xsc3, dabort=v5t_early_abort, pabort=legacy_pabort, suspend=1

	.section ".rodata"

	string	cpu_arch_name, "armv5te"
	string	cpu_elf_name, "v5"
	string	cpu_xsc3_name, "XScale-V3 based processor"

	.align

	.section ".proc.info.init", #alloc, #execinstr

.macro xsc3_proc_info name:req, cpu_val:req, cpu_mask:req
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
	b	__xsc3_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_EDSP
	.long	cpu_xsc3_name
	.long	xsc3_processor_functions
	.long	v4wbi_tlb_fns
	.long	xsc3_mc_user_fns
	.long	xsc3_cache_fns
	.size	__\name\()_proc_info, . - __\name\()_proc_info
.endm

	xsc3_proc_info xsc3, 0x69056000, 0xffffe000

/* Note: PXA935 changed its implementor ID from Intel to Marvell */
	xsc3_proc_info xsc3_pxa935, 0x56056000, 0xffffe000
