/*
 *  linux/arch/arm/mm/proc-arm920.S: MMU functions for ARM920
 *
 *  Copyright (C) 1999,2000 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *  hacked for non-paged-MM by Hyok S. Choi, 2003.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * These are the low level assembler for performing cache and TLB
 * functions on the arm920.
 *
 *  CONFIG_CPU_ARM920_CPU_IDLE -> nohlt
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/hwcap.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include "proc-macros.S"

/*
 * The size of one data cache line.
 */
#define CACHE_DLINESIZE	32

/*
 * The number of data cache segments.
 */
#define CACHE_DSEGMENTS	8

/*
 * The number of lines in a cache segment.
 */
#define CACHE_DENTRIES	64

/*
 * This is the size at which it becomes more efficient to
 * clean the whole cache, rather than using the individual
 * cache line maintenance instructions.
 */
#define CACHE_DLIMIT	65536


	.text
/*
 * cpu_arm920_proc_init()
 */
ENTRY(cpu_arm920_proc_init)
	mov	pc, lr

/*
 * cpu_arm920_proc_fin()
 */
ENTRY(cpu_arm920_proc_fin)
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1000			@ ...i............
	bic	r0, r0, #0x000e			@ ............wca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	mov	pc, lr

/*
 * cpu_arm920_reset(loc)
 *
 * Perform a soft reset of the system.  Put the CPU into the
 * same state as it would be if it had been reset, and branch
 * to what would be the reset vector.
 *
 * loc: location to jump to for soft reset
 */
	.align	5
ENTRY(cpu_arm920_reset)
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
 * cpu_arm920_do_idle()
 */
	.align	5
ENTRY(cpu_arm920_do_idle)
	mcr	p15, 0, r0, c7, c0, 4		@ Wait for interrupt
	mov	pc, lr


#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH

/*
 *	flush_icache_all()
 *
 *	Unconditionally clean and invalidate the entire icache.
 */
ENTRY(arm920_flush_icache_all)
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	mov	pc, lr
ENDPROC(arm920_flush_icache_all)

/*
 *	flush_user_cache_all()
 *
 *	Invalidate all cache entries in a particular address
 *	space.
 */
ENTRY(arm920_flush_user_cache_all)
	/* FALLTHROUGH */

/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
ENTRY(arm920_flush_kern_cache_all)
	mov	r2, #VM_EXEC
	mov	ip, #0
__flush_whole_cache:
	mov	r1, #(CACHE_DSEGMENTS - 1) << 5	@ 8 segments
1:	orr	r3, r1, #(CACHE_DENTRIES - 1) << 26 @ 64 entries
2:	mcr	p15, 0, r3, c7, c14, 2		@ clean+invalidate D index
	subs	r3, r3, #1 << 26
	bcs	2b				@ entries 63 to 0
	subs	r1, r1, #1 << 5
	bcs	1b				@ segments 7 to 0
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c5, 0		@ invalidate I cache
	mcrne	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	flush_user_cache_range(start, end, flags)
 *
 *	Invalidate a range of cache entries in the specified
 *	address space.
 *
 *	- start	- start address (inclusive)
 *	- end	- end address (exclusive)
 *	- flags	- vm_flags for address space
 */
ENTRY(arm920_flush_user_cache_range)
	mov	ip, #0
	sub	r3, r1, r0			@ calculate total size
	cmp	r3, #CACHE_DLIMIT
	bhs	__flush_whole_cache

1:	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
	tst	r2, #VM_EXEC
	mcrne	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	coherent_kern_range(start, end)
 *
 *	Ensure coherency between the Icache and the Dcache in the
 *	region described by start, end.  If you have non-snooping
 *	Harvard caches, you need to implement this function.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 */
ENTRY(arm920_coherent_kern_range)
	/* FALLTHROUGH */

/*
 *	coherent_user_range(start, end)
 *
 *	Ensure coherency between the Icache and the Dcache in the
 *	region described by start, end.  If you have non-snooping
 *	Harvard caches, you need to implement this function.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 */
ENTRY(arm920_coherent_user_range)
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	flush_kern_dcache_area(void *addr, size_t size)
 *
 *	Ensure no D cache aliasing occurs, either with itself or
 *	the I cache
 *
 *	- addr	- kernel address
 *	- size	- region size
 */
ENTRY(arm920_flush_kern_dcache_area)
	add	r1, r0, r1
1:	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_inv_range(start, end)
 *
 *	Invalidate (discard) the specified virtual address range.
 *	May not write back any entries.  If 'start' or 'end'
 *	are not cache line aligned, those lines must be written
 *	back.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 *
 * (same as v4wb)
 */
arm920_dma_inv_range:
	tst	r0, #CACHE_DLINESIZE - 1
	bic	r0, r0, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r0, c7, c10, 1		@ clean D entry
	tst	r1, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r1, c7, c10, 1		@ clean D entry
1:	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_clean_range(start, end)
 *
 *	Clean the specified virtual address range.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 *
 * (same as v4wb)
 */
arm920_dma_clean_range:
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_flush_range(start, end)
 *
 *	Clean and invalidate the specified virtual address range.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 */
ENTRY(arm920_dma_flush_range)
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(arm920_dma_map_area)
	add	r1, r1, r0
	cmp	r2, #DMA_TO_DEVICE
	beq	arm920_dma_clean_range
	bcs	arm920_dma_inv_range
	b	arm920_dma_flush_range
ENDPROC(arm920_dma_map_area)

/*
 *	dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(arm920_dma_unmap_area)
	mov	pc, lr
ENDPROC(arm920_dma_unmap_area)

	@ define struct cpu_cache_fns (see <asm/cacheflush.h> and proc-macros.S)
	define_cache_functions arm920
#endif


ENTRY(cpu_arm920_dcache_clean_area)
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	subs	r1, r1, #CACHE_DLINESIZE
	bhi	1b
	mov	pc, lr

/* =============================== PageTable ============================== */

/*
 * cpu_arm920_switch_mm(pgd)
 *
 * Set the translation base pointer to be as described by pgd.
 *
 * pgd: new page tables
 */
	.align	5
ENTRY(cpu_arm920_switch_mm)
#ifdef CONFIG_MMU
	mov	ip, #0
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	mcr	p15, 0, ip, c7, c6, 0		@ invalidate D cache
#else
@ && 'Clean & Invalidate whole DCache'
@ && Re-written to use Index Ops.
@ && Uses registers r1, r3 and ip

	mov	r1, #(CACHE_DSEGMENTS - 1) << 5	@ 8 segments
1:	orr	r3, r1, #(CACHE_DENTRIES - 1) << 26 @ 64 entries
2:	mcr	p15, 0, r3, c7, c14, 2		@ clean & invalidate D index
	subs	r3, r3, #1 << 26
	bcs	2b				@ entries 63 to 0
	subs	r1, r1, #1 << 5
	bcs	1b				@ segments 7 to 0
#endif
	mcr	p15, 0, ip, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
#endif
	mov	pc, lr

/*
 * cpu_arm920_set_pte(ptep, pte, ext)
 *
 * Set a PTE and flush it out
 */
	.align	5
ENTRY(cpu_arm920_set_pte_ext)
#ifdef CONFIG_MMU
	armv3_set_pte_ext
	mov	r0, r0
	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
#endif
	mov	pc, lr

/* Suspend/resume support: taken from arch/arm/plat-s3c24xx/sleep.S */
.globl	cpu_arm920_suspend_size
.equ	cpu_arm920_suspend_size, 4 * 4
#ifdef CONFIG_PM_SLEEP
ENTRY(cpu_arm920_do_suspend)
	stmfd	sp!, {r4 - r7, lr}
	mrc	p15, 0, r4, c13, c0, 0	@ PID
	mrc	p15, 0, r5, c3, c0, 0	@ Domain ID
	mrc	p15, 0, r6, c2, c0, 0	@ TTB address
	mrc	p15, 0, r7, c1, c0, 0	@ Control register
	stmia	r0, {r4 - r7}
	ldmfd	sp!, {r4 - r7, pc}
ENDPROC(cpu_arm920_do_suspend)

ENTRY(cpu_arm920_do_resume)
	mov	ip, #0
	mcr	p15, 0, ip, c8, c7, 0	@ invalidate I+D TLBs
	mcr	p15, 0, ip, c7, c7, 0	@ invalidate I+D caches
	ldmia	r0, {r4 - r7}
	mcr	p15, 0, r4, c13, c0, 0	@ PID
	mcr	p15, 0, r5, c3, c0, 0	@ Domain ID
	mcr	p15, 0, r6, c2, c0, 0	@ TTB address
	mov	r0, r7			@ control register
	mov	r2, r6, lsr #14		@ get TTB0 base
	mov	r2, r2, lsl #14
	ldr	r3, =PMD_TYPE_SECT | PMD_SECT_BUFFERABLE | \
		     PMD_SECT_CACHEABLE | PMD_BIT4 | PMD_SECT_AP_WRITE
	b	cpu_resume_mmu
ENDPROC(cpu_arm920_do_resume)
#endif

	__CPUINIT

	.type	__arm920_setup, #function
__arm920_setup:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7		@ invalidate I,D caches on v4
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer on v4
#ifdef CONFIG_MMU
	mcr	p15, 0, r0, c8, c7		@ invalidate I,D TLBs on v4
#endif
	adr	r5, arm920_crval
	ldmia	r5, {r5, r6}
	mrc	p15, 0, r0, c1, c0		@ get control register v4
	bic	r0, r0, r5
	orr	r0, r0, r6
	mov	pc, lr
	.size	__arm920_setup, . - __arm920_setup

	/*
	 *  R
	 * .RVI ZFRS BLDP WCAM
	 * ..11 0001 ..11 0101
	 * 
	 */
	.type	arm920_crval, #object
arm920_crval:
	crval	clear=0x00003f3f, mmuset=0x00003135, ucset=0x00001130

	__INITDATA
	@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
	define_processor_functions arm920, dabort=v4t_early_abort, pabort=legacy_pabort, suspend=1

	.section ".rodata"

	string	cpu_arch_name, "armv4t"
	string	cpu_elf_name, "v4"
	string	cpu_arm920_name, "ARM920T"

	.align

	.section ".proc.info.init", #alloc, #execinstr

	.type	__arm920_proc_info,#object
__arm920_proc_info:
	.long	0x41009200
	.long	0xff00fff0
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
	b	__arm920_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF | HWCAP_THUMB
	.long	cpu_arm920_name
	.long	arm920_processor_functions
	.long	v4wbi_tlb_fns
	.long	v4wb_user_fns
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	.long	arm920_cache_fns
#else
	.long	v4wt_cache_fns
#endif
	.size	__arm920_proc_info, . - __arm920_proc_info
