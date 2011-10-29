/*
 *  linux/arch/arm/mm/proc-arm1020.S: MMU functions for ARM1020
 *
 *  Copyright (C) 2000 ARM Limited
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
 * functions on the arm1020.
 *
 *  CONFIG_CPU_ARM1020_CPU_IDLE -> nohlt
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/hwcap.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>

#include "proc-macros.S"

/*
 * This is the maximum size of an area which will be invalidated
 * using the single invalidate entry instructions.  Anything larger
 * than this, and we go for the whole cache.
 *
 * This value should be chosen such that we choose the cheapest
 * alternative.
 */
#define MAX_AREA_SIZE	32768

/*
 * The size of one data cache line.
 */
#define CACHE_DLINESIZE	32

/*
 * The number of data cache segments.
 */
#define CACHE_DSEGMENTS	16

/*
 * The number of lines in a cache segment.
 */
#define CACHE_DENTRIES	64

/*
 * This is the size at which it becomes more efficient to
 * clean the whole cache, rather than using the individual
 * cache line maintenance instructions.
 */
#define CACHE_DLIMIT	32768

	.text
/*
 * cpu_arm1020_proc_init()
 */
ENTRY(cpu_arm1020_proc_init)
	mov	pc, lr

/*
 * cpu_arm1020_proc_fin()
 */
ENTRY(cpu_arm1020_proc_fin)
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1000 		@ ...i............
	bic	r0, r0, #0x000e 		@ ............wca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	mov	pc, lr

/*
 * cpu_arm1020_reset(loc)
 *
 * Perform a soft reset of the system.	Put the CPU into the
 * same state as it would be if it had been reset, and branch
 * to what would be the reset vector.
 *
 * loc: location to jump to for soft reset
 */
	.align	5
ENTRY(cpu_arm1020_reset)
	mov	ip, #0
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I,D caches
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
#ifdef CONFIG_MMU
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
#endif
	mrc	p15, 0, ip, c1, c0, 0		@ ctrl register
	bic	ip, ip, #0x000f 		@ ............wcam
	bic	ip, ip, #0x1100 		@ ...i...s........
	mcr	p15, 0, ip, c1, c0, 0		@ ctrl register
	mov	pc, r0

/*
 * cpu_arm1020_do_idle()
 */
	.align	5
ENTRY(cpu_arm1020_do_idle)
	mcr	p15, 0, r0, c7, c0, 4		@ Wait for interrupt
	mov	pc, lr

/* ================================= CACHE ================================ */

	.align	5

/*
 *	flush_icache_all()
 *
 *	Unconditionally clean and invalidate the entire icache.
 */
ENTRY(arm1020_flush_icache_all)
#ifndef CONFIG_CPU_ICACHE_DISABLE
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
#endif
	mov	pc, lr
ENDPROC(arm1020_flush_icache_all)

/*
 *	flush_user_cache_all()
 *
 *	Invalidate all cache entries in a particular address
 *	space.
 */
ENTRY(arm1020_flush_user_cache_all)
	/* FALLTHROUGH */
/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
ENTRY(arm1020_flush_kern_cache_all)
	mov	r2, #VM_EXEC
	mov	ip, #0
__flush_whole_cache:
#ifndef CONFIG_CPU_DCACHE_DISABLE
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	r1, #(CACHE_DSEGMENTS - 1) << 5	@ 16 segments
1:	orr	r3, r1, #(CACHE_DENTRIES - 1) << 26 @ 64 entries
2:	mcr	p15, 0, r3, c7, c14, 2		@ clean+invalidate D index
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	subs	r3, r3, #1 << 26
	bcs	2b				@ entries 63 to 0
	subs	r1, r1, #1 << 5
	bcs	1b				@ segments 15 to 0
#endif
	tst	r2, #VM_EXEC
#ifndef CONFIG_CPU_ICACHE_DISABLE
	mcrne	p15, 0, ip, c7, c5, 0		@ invalidate I cache
#endif
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
 *	- flags	- vm_flags for this space
 */
ENTRY(arm1020_flush_user_cache_range)
	mov	ip, #0
	sub	r3, r1, r0			@ calculate total size
	cmp	r3, #CACHE_DLIMIT
	bhs	__flush_whole_cache

#ifndef CONFIG_CPU_DCACHE_DISABLE
	mcr	p15, 0, ip, c7, c10, 4
1:	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
#endif
	tst	r2, #VM_EXEC
#ifndef CONFIG_CPU_ICACHE_DISABLE
	mcrne	p15, 0, ip, c7, c5, 0		@ invalidate I cache
#endif
	mcrne	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	coherent_kern_range(start, end)
 *
 *	Ensure coherency between the Icache and the Dcache in the
 *	region described by start.  If you have non-snooping
 *	Harvard caches, you need to implement this function.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 */
ENTRY(arm1020_coherent_kern_range)
	/* FALLTRHOUGH */

/*
 *	coherent_user_range(start, end)
 *
 *	Ensure coherency between the Icache and the Dcache in the
 *	region described by start.  If you have non-snooping
 *	Harvard caches, you need to implement this function.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 */
ENTRY(arm1020_coherent_user_range)
	mov	ip, #0
	bic	r0, r0, #CACHE_DLINESIZE - 1
	mcr	p15, 0, ip, c7, c10, 4
1:
#ifndef CONFIG_CPU_DCACHE_DISABLE
	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
#endif
#ifndef CONFIG_CPU_ICACHE_DISABLE
	mcr	p15, 0, r0, c7, c5, 1		@ invalidate I entry
#endif
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
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
ENTRY(arm1020_flush_kern_dcache_area)
	mov	ip, #0
#ifndef CONFIG_CPU_DCACHE_DISABLE
	add	r1, r0, r1
1:	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
#endif
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
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
arm1020_dma_inv_range:
	mov	ip, #0
#ifndef CONFIG_CPU_DCACHE_DISABLE
	tst	r0, #CACHE_DLINESIZE - 1
	bic	r0, r0, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, ip, c7, c10, 4
	mcrne	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcrne	p15, 0, ip, c7, c10, 4		@ drain WB
	tst	r1, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, ip, c7, c10, 4
	mcrne	p15, 0, r1, c7, c10, 1		@ clean D entry
	mcrne	p15, 0, ip, c7, c10, 4		@ drain WB
1:	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
#endif
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
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
arm1020_dma_clean_range:
	mov	ip, #0
#ifndef CONFIG_CPU_DCACHE_DISABLE
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
#endif
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_flush_range(start, end)
 *
 *	Clean and invalidate the specified virtual address range.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 */
ENTRY(arm1020_dma_flush_range)
	mov	ip, #0
#ifndef CONFIG_CPU_DCACHE_DISABLE
	bic	r0, r0, #CACHE_DLINESIZE - 1
	mcr	p15, 0, ip, c7, c10, 4
1:	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
#endif
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(arm1020_dma_map_area)
	add	r1, r1, r0
	cmp	r2, #DMA_TO_DEVICE
	beq	arm1020_dma_clean_range
	bcs	arm1020_dma_inv_range
	b	arm1020_dma_flush_range
ENDPROC(arm1020_dma_map_area)

/*
 *	dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(arm1020_dma_unmap_area)
	mov	pc, lr
ENDPROC(arm1020_dma_unmap_area)

	@ define struct cpu_cache_fns (see <asm/cacheflush.h> and proc-macros.S)
	define_cache_functions arm1020

	.align	5
ENTRY(cpu_arm1020_dcache_clean_area)
#ifndef CONFIG_CPU_DCACHE_DISABLE
	mov	ip, #0
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	add	r0, r0, #CACHE_DLINESIZE
	subs	r1, r1, #CACHE_DLINESIZE
	bhi	1b
#endif
	mov	pc, lr

/* =============================== PageTable ============================== */

/*
 * cpu_arm1020_switch_mm(pgd)
 *
 * Set the translation base pointer to be as described by pgd.
 *
 * pgd: new page tables
 */
	.align	5
ENTRY(cpu_arm1020_switch_mm)
#ifdef CONFIG_MMU
#ifndef CONFIG_CPU_DCACHE_DISABLE
	mcr	p15, 0, r3, c7, c10, 4
	mov	r1, #0xF			@ 16 segments
1:	mov	r3, #0x3F			@ 64 entries
2:	mov	ip, r3, LSL #26 		@ shift up entry
	orr	ip, ip, r1, LSL #5		@ shift in/up index
	mcr	p15, 0, ip, c7, c14, 2		@ Clean & Inval DCache entry
	mov	ip, #0
	mcr	p15, 0, ip, c7, c10, 4
	subs	r3, r3, #1
	cmp	r3, #0
	bge	2b				@ entries 3F to 0
	subs	r1, r1, #1
	cmp	r1, #0
	bge	1b				@ segments 15 to 0

#endif
	mov	r1, #0
#ifndef CONFIG_CPU_ICACHE_DISABLE
	mcr	p15, 0, r1, c7, c5, 0		@ invalidate I cache
#endif
	mcr	p15, 0, r1, c7, c10, 4		@ drain WB
	mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
	mcr	p15, 0, r1, c8, c7, 0		@ invalidate I & D TLBs
#endif /* CONFIG_MMU */
	mov	pc, lr
        
/*
 * cpu_arm1020_set_pte(ptep, pte)
 *
 * Set a PTE and flush it out
 */
	.align	5
ENTRY(cpu_arm1020_set_pte_ext)
#ifdef CONFIG_MMU
	armv3_set_pte_ext
	mov	r0, r0
#ifndef CONFIG_CPU_DCACHE_DISABLE
	mcr	p15, 0, r0, c7, c10, 4
	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
#endif
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
#endif /* CONFIG_MMU */
	mov	pc, lr

	__CPUINIT

	.type	__arm1020_setup, #function
__arm1020_setup:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7		@ invalidate I,D caches on v4
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer on v4
#ifdef CONFIG_MMU
	mcr	p15, 0, r0, c8, c7		@ invalidate I,D TLBs on v4
#endif

	adr	r5, arm1020_crval
	ldmia	r5, {r5, r6}
	mrc	p15, 0, r0, c1, c0		@ get control register v4
	bic	r0, r0, r5
	orr	r0, r0, r6
#ifdef CONFIG_CPU_CACHE_ROUND_ROBIN
	orr	r0, r0, #0x4000 		@ .R.. .... .... ....
#endif
	mov	pc, lr
	.size	__arm1020_setup, . - __arm1020_setup

	/*
	 *  R
	 * .RVI ZFRS BLDP WCAM
	 * .011 1001 ..11 0101
	 */
	.type	arm1020_crval, #object
arm1020_crval:
	crval	clear=0x0000593f, mmuset=0x00003935, ucset=0x00001930

	__INITDATA
	@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
	define_processor_functions arm1020, dabort=v4t_early_abort, pabort=legacy_pabort


	.section ".rodata"

	string	cpu_arch_name, "armv5t"
	string	cpu_elf_name, "v5"

	.type	cpu_arm1020_name, #object
cpu_arm1020_name:
	.ascii	"ARM1020"
#ifndef CONFIG_CPU_ICACHE_DISABLE
	.ascii	"i"
#endif
#ifndef CONFIG_CPU_DCACHE_DISABLE
	.ascii	"d"
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	.ascii	"(wt)"
#else
	.ascii	"(wb)"
#endif
#endif
#ifndef CONFIG_CPU_BPREDICT_DISABLE
	.ascii	"B"
#endif
#ifdef CONFIG_CPU_CACHE_ROUND_ROBIN
	.ascii	"RR"
#endif
	.ascii	"\0"
	.size	cpu_arm1020_name, . - cpu_arm1020_name

	.align

	.section ".proc.info.init", #alloc, #execinstr

	.type	__arm1020_proc_info,#object
__arm1020_proc_info:
	.long	0x4104a200			@ ARM 1020T (Architecture v5T)
	.long	0xff0ffff0
	.long   PMD_TYPE_SECT | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	.long   PMD_TYPE_SECT | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	b	__arm1020_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF | HWCAP_THUMB
	.long	cpu_arm1020_name
	.long	arm1020_processor_functions
	.long	v4wbi_tlb_fns
	.long	v4wb_user_fns
	.long	arm1020_cache_fns
	.size	__arm1020_proc_info, . - __arm1020_proc_info
