/*
 *  linux/arch/arm/mm/proc-feroceon.S: MMU functions for Feroceon
 *
 *  Heavily based on proc-arm926.S
 *  Maintainer: Assaf Hoffman <hoffman@marvell.com>
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

	.bss
	.align 3
__cache_params_loc:
	.space	8

	.text
__cache_params:
	.word	__cache_params_loc

/*
 * cpu_feroceon_proc_init()
 */
ENTRY(cpu_feroceon_proc_init)
	mrc	p15, 0, r0, c0, c0, 1		@ read cache type register
	ldr	r1, __cache_params
	mov	r2, #(16 << 5)
	tst	r0, #(1 << 16)			@ get way
	mov	r0, r0, lsr #18			@ get cache size order
	movne	r3, #((4 - 1) << 30)		@ 4-way
	and	r0, r0, #0xf
	moveq	r3, #0				@ 1-way
	mov	r2, r2, lsl r0			@ actual cache size
	movne	r2, r2, lsr #2			@ turned into # of sets
	sub	r2, r2, #(1 << 5)
	stmia	r1, {r2, r3}
	ret	lr

/*
 * cpu_feroceon_proc_fin()
 */
ENTRY(cpu_feroceon_proc_fin)
#if defined(CONFIG_CACHE_FEROCEON_L2) && \
	!defined(CONFIG_CACHE_FEROCEON_L2_WRITETHROUGH)
	mov	r0, #0
	mcr	p15, 1, r0, c15, c9, 0		@ clean L2
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
#endif

	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1000			@ ...i............
	bic	r0, r0, #0x000e			@ ............wca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	ret	lr

/*
 * cpu_feroceon_reset(loc)
 *
 * Perform a soft reset of the system.  Put the CPU into the
 * same state as it would be if it had been reset, and branch
 * to what would be the reset vector.
 *
 * loc: location to jump to for soft reset
 */
	.align	5
	.pushsection	.idmap.text, "ax"
ENTRY(cpu_feroceon_reset)
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
	ret	r0
ENDPROC(cpu_feroceon_reset)
	.popsection

/*
 * cpu_feroceon_do_idle()
 *
 * Called with IRQs disabled
 */
	.align	5
ENTRY(cpu_feroceon_do_idle)
	mov	r0, #0
	mcr	p15, 0, r0, c7, c10, 4		@ Drain write buffer
	mcr	p15, 0, r0, c7, c0, 4		@ Wait for interrupt
	ret	lr

/*
 *	flush_icache_all()
 *
 *	Unconditionally clean and invalidate the entire icache.
 */
ENTRY(feroceon_flush_icache_all)
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	ret	lr
ENDPROC(feroceon_flush_icache_all)

/*
 *	flush_user_cache_all()
 *
 *	Clean and invalidate all cache entries in a particular
 *	address space.
 */
	.align	5
ENTRY(feroceon_flush_user_cache_all)
	/* FALLTHROUGH */

/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
ENTRY(feroceon_flush_kern_cache_all)
	mov	r2, #VM_EXEC

__flush_whole_cache:
	ldr	r1, __cache_params
	ldmia	r1, {r1, r3}
1:	orr	ip, r1, r3
2:	mcr	p15, 0, ip, c7, c14, 2		@ clean + invalidate D set/way
	subs	ip, ip, #(1 << 30)		@ next way
	bcs	2b
	subs	r1, r1, #(1 << 5)		@ next set
	bcs	1b

	tst	r2, #VM_EXEC
	mov	ip, #0
	mcrne	p15, 0, ip, c7, c5, 0		@ invalidate I cache
	mcrne	p15, 0, ip, c7, c10, 4		@ drain WB
	ret	lr

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
	.align	5
ENTRY(feroceon_flush_user_cache_range)
	sub	r3, r1, r0			@ calculate total size
	cmp	r3, #CACHE_DLIMIT
	bgt	__flush_whole_cache
1:	tst	r2, #VM_EXEC
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D entry
	mcrne	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
	mcr	p15, 0, r0, c7, c14, 1		@ clean and invalidate D entry
	mcrne	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	tst	r2, #VM_EXEC
	mov	ip, #0
	mcrne	p15, 0, ip, c7, c10, 4		@ drain WB
	ret	lr

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
	.align	5
ENTRY(feroceon_coherent_kern_range)
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
ENTRY(feroceon_coherent_user_range)
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	r0, #0
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
	.align	5
ENTRY(feroceon_flush_kern_dcache_area)
	add	r1, r0, r1
1:	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	ret	lr

	.align	5
ENTRY(feroceon_range_flush_kern_dcache_area)
	mrs	r2, cpsr
	add	r1, r0, #PAGE_SZ - CACHE_DLINESIZE	@ top addr is inclusive
	orr	r3, r2, #PSR_I_BIT
	msr	cpsr_c, r3			@ disable interrupts
	mcr	p15, 5, r0, c15, c15, 0		@ D clean/inv range start
	mcr	p15, 5, r1, c15, c15, 1		@ D clean/inv range top
	msr	cpsr_c, r2			@ restore interrupts
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	ret	lr

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
	.align	5
feroceon_dma_inv_range:
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
	ret	lr

	.align	5
feroceon_range_dma_inv_range:
	mrs	r2, cpsr
	tst	r0, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r0, c7, c10, 1		@ clean D entry
	tst	r1, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r1, c7, c10, 1		@ clean D entry
	cmp	r1, r0
	subne	r1, r1, #1			@ top address is inclusive
	orr	r3, r2, #PSR_I_BIT
	msr	cpsr_c, r3			@ disable interrupts
	mcr	p15, 5, r0, c15, c14, 0		@ D inv range start
	mcr	p15, 5, r1, c15, c14, 1		@ D inv range top
	msr	cpsr_c, r2			@ restore interrupts
	ret	lr

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
	.align	5
feroceon_dma_clean_range:
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	ret	lr

	.align	5
feroceon_range_dma_clean_range:
	mrs	r2, cpsr
	cmp	r1, r0
	subne	r1, r1, #1			@ top address is inclusive
	orr	r3, r2, #PSR_I_BIT
	msr	cpsr_c, r3			@ disable interrupts
	mcr	p15, 5, r0, c15, c13, 0		@ D clean range start
	mcr	p15, 5, r1, c15, c13, 1		@ D clean range top
	msr	cpsr_c, r2			@ restore interrupts
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	ret	lr

/*
 *	dma_flush_range(start, end)
 *
 *	Clean and invalidate the specified virtual address range.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 */
	.align	5
ENTRY(feroceon_dma_flush_range)
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	ret	lr

	.align	5
ENTRY(feroceon_range_dma_flush_range)
	mrs	r2, cpsr
	cmp	r1, r0
	subne	r1, r1, #1			@ top address is inclusive
	orr	r3, r2, #PSR_I_BIT
	msr	cpsr_c, r3			@ disable interrupts
	mcr	p15, 5, r0, c15, c15, 0		@ D clean/inv range start
	mcr	p15, 5, r1, c15, c15, 1		@ D clean/inv range top
	msr	cpsr_c, r2			@ restore interrupts
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	ret	lr

/*
 *	dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(feroceon_dma_map_area)
	add	r1, r1, r0
	cmp	r2, #DMA_TO_DEVICE
	beq	feroceon_dma_clean_range
	bcs	feroceon_dma_inv_range
	b	feroceon_dma_flush_range
ENDPROC(feroceon_dma_map_area)

/*
 *	dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(feroceon_range_dma_map_area)
	add	r1, r1, r0
	cmp	r2, #DMA_TO_DEVICE
	beq	feroceon_range_dma_clean_range
	bcs	feroceon_range_dma_inv_range
	b	feroceon_range_dma_flush_range
ENDPROC(feroceon_range_dma_map_area)

/*
 *	dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(feroceon_dma_unmap_area)
	ret	lr
ENDPROC(feroceon_dma_unmap_area)

	.globl	feroceon_flush_kern_cache_louis
	.equ	feroceon_flush_kern_cache_louis, feroceon_flush_kern_cache_all

	@ define struct cpu_cache_fns (see <asm/cacheflush.h> and proc-macros.S)
	define_cache_functions feroceon

.macro range_alias basename
	.globl feroceon_range_\basename
	.type feroceon_range_\basename , %function
	.equ feroceon_range_\basename , feroceon_\basename
.endm

/*
 * Most of the cache functions are unchanged for this case.
 * Export suitable alias symbols for the unchanged functions:
 */
	range_alias flush_icache_all
	range_alias flush_user_cache_all
	range_alias flush_kern_cache_all
	range_alias flush_kern_cache_louis
	range_alias flush_user_cache_range
	range_alias coherent_kern_range
	range_alias coherent_user_range
	range_alias dma_unmap_area

	define_cache_functions feroceon_range

	.align	5
ENTRY(cpu_feroceon_dcache_clean_area)
#if defined(CONFIG_CACHE_FEROCEON_L2) && \
	!defined(CONFIG_CACHE_FEROCEON_L2_WRITETHROUGH)
	mov	r2, r0
	mov	r3, r1
#endif
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	subs	r1, r1, #CACHE_DLINESIZE
	bhi	1b
#if defined(CONFIG_CACHE_FEROCEON_L2) && \
	!defined(CONFIG_CACHE_FEROCEON_L2_WRITETHROUGH)
1:	mcr	p15, 1, r2, c15, c9, 1		@ clean L2 entry
	add	r2, r2, #CACHE_DLINESIZE
	subs	r3, r3, #CACHE_DLINESIZE
	bhi	1b
#endif
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	ret	lr

/* =============================== PageTable ============================== */

/*
 * cpu_feroceon_switch_mm(pgd)
 *
 * Set the translation base pointer to be as described by pgd.
 *
 * pgd: new page tables
 */
	.align	5
ENTRY(cpu_feroceon_switch_mm)
#ifdef CONFIG_MMU
	/*
	 * Note: we wish to call __flush_whole_cache but we need to preserve
	 * lr to do so.  The only way without touching main memory is to
	 * use r2 which is normally used to test the VM_EXEC flag, and
	 * compensate locally for the skipped ops if it is not set.
	 */
	mov	r2, lr				@ abuse r2 to preserve lr
	bl	__flush_whole_cache
	@ if r2 contains the VM_EXEC bit then the next 2 ops are done already
	tst	r2, #VM_EXEC
	mcreq	p15, 0, ip, c7, c5, 0		@ invalidate I cache
	mcreq	p15, 0, ip, c7, c10, 4		@ drain WB

	mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
	ret	r2
#else
	ret	lr
#endif

/*
 * cpu_feroceon_set_pte_ext(ptep, pte, ext)
 *
 * Set a PTE and flush it out
 */
	.align	5
ENTRY(cpu_feroceon_set_pte_ext)
#ifdef CONFIG_MMU
	armv3_set_pte_ext wc_disable=0
	mov	r0, r0
	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
#if defined(CONFIG_CACHE_FEROCEON_L2) && \
	!defined(CONFIG_CACHE_FEROCEON_L2_WRITETHROUGH)
	mcr	p15, 1, r0, c15, c9, 1		@ clean L2 entry
#endif
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
#endif
	ret	lr

/* Suspend/resume support: taken from arch/arm/mm/proc-arm926.S */
.globl	cpu_feroceon_suspend_size
.equ	cpu_feroceon_suspend_size, 4 * 3
#ifdef CONFIG_ARM_CPU_SUSPEND
ENTRY(cpu_feroceon_do_suspend)
	stmfd	sp!, {r4 - r6, lr}
	mrc	p15, 0, r4, c13, c0, 0	@ PID
	mrc	p15, 0, r5, c3, c0, 0	@ Domain ID
	mrc	p15, 0, r6, c1, c0, 0	@ Control register
	stmia	r0, {r4 - r6}
	ldmfd	sp!, {r4 - r6, pc}
ENDPROC(cpu_feroceon_do_suspend)

ENTRY(cpu_feroceon_do_resume)
	mov	ip, #0
	mcr	p15, 0, ip, c8, c7, 0	@ invalidate I+D TLBs
	mcr	p15, 0, ip, c7, c7, 0	@ invalidate I+D caches
	ldmia	r0, {r4 - r6}
	mcr	p15, 0, r4, c13, c0, 0	@ PID
	mcr	p15, 0, r5, c3, c0, 0	@ Domain ID
	mcr	p15, 0, r1, c2, c0, 0	@ TTB address
	mov	r0, r6			@ control register
	b	cpu_resume_mmu
ENDPROC(cpu_feroceon_do_resume)
#endif

	.type	__feroceon_setup, #function
__feroceon_setup:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7		@ invalidate I,D caches on v4
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer on v4
#ifdef CONFIG_MMU
	mcr	p15, 0, r0, c8, c7		@ invalidate I,D TLBs on v4
#endif

	adr	r5, feroceon_crval
	ldmia	r5, {r5, r6}
	mrc	p15, 0, r0, c1, c0		@ get control register v4
	bic	r0, r0, r5
	orr	r0, r0, r6
	ret	lr
	.size	__feroceon_setup, . - __feroceon_setup

	/*
	 *      B
	 *  R   P
	 * .RVI UFRS BLDP WCAM
	 * .011 .001 ..11 0101
	 *
	 */
	.type	feroceon_crval, #object
feroceon_crval:
	crval	clear=0x0000773f, mmuset=0x00003135, ucset=0x00001134

	__INITDATA

	@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
	define_processor_functions feroceon, dabort=v5t_early_abort, pabort=legacy_pabort

	.section ".rodata"

	string	cpu_arch_name, "armv5te"
	string	cpu_elf_name, "v5"
	string	cpu_feroceon_name, "Feroceon"
	string	cpu_88fr531_name, "Feroceon 88FR531-vd"
	string	cpu_88fr571_name, "Feroceon 88FR571-vd"
	string	cpu_88fr131_name, "Feroceon 88FR131"

	.align

	.section ".proc.info.init", #alloc, #execinstr

.macro feroceon_proc_info name:req, cpu_val:req, cpu_mask:req, cpu_name:req, cache:req
	.type	__\name\()_proc_info,#object
__\name\()_proc_info:
	.long	\cpu_val
	.long	\cpu_mask
	.long	PMD_TYPE_SECT | \
		PMD_SECT_BUFFERABLE | \
		PMD_SECT_CACHEABLE | \
		PMD_BIT4 | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	.long	PMD_TYPE_SECT | \
		PMD_BIT4 | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	b	__feroceon_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_EDSP
	.long	\cpu_name
	.long	feroceon_processor_functions
	.long	v4wbi_tlb_fns
	.long	feroceon_user_fns
	.long	\cache
	 .size	__\name\()_proc_info, . - __\name\()_proc_info
.endm

#ifdef CONFIG_CPU_FEROCEON_OLD_ID
	feroceon_proc_info feroceon_old_id, 0x41009260, 0xff00fff0, \
		cpu_name=cpu_feroceon_name, cache=feroceon_cache_fns
#endif

	feroceon_proc_info 88fr531, 0x56055310, 0xfffffff0, cpu_88fr531_name, \
		cache=feroceon_cache_fns
	feroceon_proc_info 88fr571, 0x56155710, 0xfffffff0, cpu_88fr571_name, \
		cache=feroceon_range_cache_fns
	feroceon_proc_info 88fr131, 0x56251310, 0xfffffff0, cpu_88fr131_name, \
		cache=feroceon_range_cache_fns
