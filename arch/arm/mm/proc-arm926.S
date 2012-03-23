/*
 *  linux/arch/arm/mm/proc-arm926.S: MMU functions for ARM926EJ-S
 *
 *  Copyright (C) 1999-2001 ARM Limited
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
 * functions on the arm926.
 *
 *  CONFIG_CPU_ARM926_CPU_IDLE -> nohlt
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
 * This is the maximum size of an area which will be invalidated
 * using the single invalidate entry instructions.  Anything larger
 * than this, and we go for the whole cache.
 *
 * This value should be chosen such that we choose the cheapest
 * alternative.
 */
#define CACHE_DLIMIT	16384

/*
 * the cache line size of the I and D cache
 */
#define CACHE_DLINESIZE	32

	.text
/*
 * cpu_arm926_proc_init()
 */
ENTRY(cpu_arm926_proc_init)
	mov	pc, lr

/*
 * cpu_arm926_proc_fin()
 */
ENTRY(cpu_arm926_proc_fin)
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1000			@ ...i............
	bic	r0, r0, #0x000e			@ ............wca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	mov	pc, lr

/*
 * cpu_arm926_reset(loc)
 *
 * Perform a soft reset of the system.  Put the CPU into the
 * same state as it would be if it had been reset, and branch
 * to what would be the reset vector.
 *
 * loc: location to jump to for soft reset
 */
	.align	5
	.pushsection	.idmap.text, "ax"
ENTRY(cpu_arm926_reset)
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
ENDPROC(cpu_arm926_reset)
	.popsection

/*
 * cpu_arm926_do_idle()
 *
 * Called with IRQs disabled
 */
	.align	10
ENTRY(cpu_arm926_do_idle)
	mov	r0, #0
	mrc	p15, 0, r1, c1, c0, 0		@ Read control register
	mcr	p15, 0, r0, c7, c10, 4		@ Drain write buffer
	bic	r2, r1, #1 << 12
	mrs	r3, cpsr			@ Disable FIQs while Icache
	orr	ip, r3, #PSR_F_BIT		@ is disabled
	msr	cpsr_c, ip
	mcr	p15, 0, r2, c1, c0, 0		@ Disable I cache
	mcr	p15, 0, r0, c7, c0, 4		@ Wait for interrupt
	mcr	p15, 0, r1, c1, c0, 0		@ Restore ICache enable
	msr	cpsr_c, r3			@ Restore FIQ state
	mov	pc, lr

/*
 *	flush_icache_all()
 *
 *	Unconditionally clean and invalidate the entire icache.
 */
ENTRY(arm926_flush_icache_all)
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	mov	pc, lr
ENDPROC(arm926_flush_icache_all)

/*
 *	flush_user_cache_all()
 *
 *	Clean and invalidate all cache entries in a particular
 *	address space.
 */
ENTRY(arm926_flush_user_cache_all)
	/* FALLTHROUGH */

/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
ENTRY(arm926_flush_kern_cache_all)
	mov	r2, #VM_EXEC
	mov	ip, #0
__flush_whole_cache:
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	mcr	p15, 0, ip, c7, c6, 0		@ invalidate D cache
#else
1:	mrc	p15, 0, r15, c7, c14, 3 	@ test,clean,invalidate
	bne	1b
#endif
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c5, 0		@ invalidate I cache
	mcrne	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	flush_user_cache_range(start, end, flags)
 *
 *	Clean and invalidate a range of cache entries in the
 *	specified address range.
 *
 *	- start	- start address (inclusive)
 *	- end	- end address (exclusive)
 *	- flags	- vm_flags describing address space
 */
ENTRY(arm926_flush_user_cache_range)
	mov	ip, #0
	sub	r3, r1, r0			@ calculate total size
	cmp	r3, #CACHE_DLIMIT
	bgt	__flush_whole_cache
1:	tst	r2, #VM_EXEC
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	mcrne	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	mcrne	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
#else
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D entry
	mcrne	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D entry
	mcrne	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
#endif
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
ENTRY(arm926_coherent_kern_range)
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
ENTRY(arm926_coherent_user_range)
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
ENTRY(arm926_flush_kern_dcache_area)
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
arm926_dma_inv_range:
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	tst	r0, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r0, c7, c10, 1		@ clean D entry
	tst	r1, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r1, c7, c10, 1		@ clean D entry
#endif
	bic	r0, r0, #CACHE_DLINESIZE - 1
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
arm926_dma_clean_range:
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
#endif
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
ENTRY(arm926_dma_flush_range)
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
#else
	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
#endif
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
ENTRY(arm926_dma_map_area)
	add	r1, r1, r0
	cmp	r2, #DMA_TO_DEVICE
	beq	arm926_dma_clean_range
	bcs	arm926_dma_inv_range
	b	arm926_dma_flush_range
ENDPROC(arm926_dma_map_area)

/*
 *	dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(arm926_dma_unmap_area)
	mov	pc, lr
ENDPROC(arm926_dma_unmap_area)

	@ define struct cpu_cache_fns (see <asm/cacheflush.h> and proc-macros.S)
	define_cache_functions arm926

ENTRY(cpu_arm926_dcache_clean_area)
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	subs	r1, r1, #CACHE_DLINESIZE
	bhi	1b
#endif
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/* =============================== PageTable ============================== */

/*
 * cpu_arm926_switch_mm(pgd)
 *
 * Set the translation base pointer to be as described by pgd.
 *
 * pgd: new page tables
 */
	.align	5
ENTRY(cpu_arm926_switch_mm)
#ifdef CONFIG_MMU
	mov	ip, #0
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	mcr	p15, 0, ip, c7, c6, 0		@ invalidate D cache
#else
@ && 'Clean & Invalidate whole DCache'
1:	mrc	p15, 0, r15, c7, c14, 3 	@ test,clean,invalidate
	bne	1b
#endif
	mcr	p15, 0, ip, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
#endif
	mov	pc, lr

/*
 * cpu_arm926_set_pte_ext(ptep, pte, ext)
 *
 * Set a PTE and flush it out
 */
	.align	5
ENTRY(cpu_arm926_set_pte_ext)
#ifdef CONFIG_MMU
	armv3_set_pte_ext
	mov	r0, r0
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
#endif
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
#endif
	mov	pc, lr

/* Suspend/resume support: taken from arch/arm/plat-s3c24xx/sleep.S */
.globl	cpu_arm926_suspend_size
.equ	cpu_arm926_suspend_size, 4 * 3
#ifdef CONFIG_PM_SLEEP
ENTRY(cpu_arm926_do_suspend)
	stmfd	sp!, {r4 - r6, lr}
	mrc	p15, 0, r4, c13, c0, 0	@ PID
	mrc	p15, 0, r5, c3, c0, 0	@ Domain ID
	mrc	p15, 0, r6, c1, c0, 0	@ Control register
	stmia	r0, {r4 - r6}
	ldmfd	sp!, {r4 - r6, pc}
ENDPROC(cpu_arm926_do_suspend)

ENTRY(cpu_arm926_do_resume)
	mov	ip, #0
	mcr	p15, 0, ip, c8, c7, 0	@ invalidate I+D TLBs
	mcr	p15, 0, ip, c7, c7, 0	@ invalidate I+D caches
	ldmia	r0, {r4 - r6}
	mcr	p15, 0, r4, c13, c0, 0	@ PID
	mcr	p15, 0, r5, c3, c0, 0	@ Domain ID
	mcr	p15, 0, r1, c2, c0, 0	@ TTB address
	mov	r0, r6			@ control register
	b	cpu_resume_mmu
ENDPROC(cpu_arm926_do_resume)
#endif

	__CPUINIT

	.type	__arm926_setup, #function
__arm926_setup:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7		@ invalidate I,D caches on v4
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer on v4
#ifdef CONFIG_MMU
	mcr	p15, 0, r0, c8, c7		@ invalidate I,D TLBs on v4
#endif


#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	mov	r0, #4				@ disable write-back on caches explicitly
	mcr	p15, 7, r0, c15, c0, 0
#endif 

	adr	r5, arm926_crval
	ldmia	r5, {r5, r6}
	mrc	p15, 0, r0, c1, c0		@ get control register v4
	bic	r0, r0, r5
	orr	r0, r0, r6
#ifdef CONFIG_CPU_CACHE_ROUND_ROBIN
	orr	r0, r0, #0x4000			@ .1.. .... .... ....
#endif
	mov	pc, lr
	.size	__arm926_setup, . - __arm926_setup

	/*
	 *  R
	 * .RVI ZFRS BLDP WCAM
	 * .011 0001 ..11 0101
	 * 
	 */
	.type	arm926_crval, #object
arm926_crval:
	crval	clear=0x00007f3f, mmuset=0x00003135, ucset=0x00001134

	__INITDATA

	@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
	define_processor_functions arm926, dabort=v5tj_early_abort, pabort=legacy_pabort, suspend=1

	.section ".rodata"

	string	cpu_arch_name, "armv5tej"
	string	cpu_elf_name, "v5"
	string	cpu_arm926_name, "ARM926EJ-S"

	.align

	.section ".proc.info.init", #alloc, #execinstr

	.type	__arm926_proc_info,#object
__arm926_proc_info:
	.long	0x41069260			@ ARM926EJ-S (v5TEJ)
	.long	0xff0ffff0
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
	b	__arm926_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_EDSP|HWCAP_JAVA
	.long	cpu_arm926_name
	.long	arm926_processor_functions
	.long	v4wbi_tlb_fns
	.long	v4wb_user_fns
	.long	arm926_cache_fns
	.size	__arm926_proc_info, . - __arm926_proc_info
