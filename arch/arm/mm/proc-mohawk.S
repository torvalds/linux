/*
 *  linux/arch/arm/mm/proc-mohawk.S: MMU functions for Marvell PJ1 core
 *
 *  PJ1 (codename Mohawk) is a hybrid of the xscale3 and Marvell's own core.
 *
 *  Heavily based on proc-arm926.S and proc-xsc3.S
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
 * This is the maximum size of an area which will be flushed.  If the
 * area is larger than this, then we flush the whole cache.
 */
#define CACHE_DLIMIT	32768

/*
 * The cache line size of the L1 D cache.
 */
#define CACHE_DLINESIZE	32

/*
 * cpu_mohawk_proc_init()
 */
ENTRY(cpu_mohawk_proc_init)
	mov	pc, lr

/*
 * cpu_mohawk_proc_fin()
 */
ENTRY(cpu_mohawk_proc_fin)
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1800			@ ...iz...........
	bic	r0, r0, #0x0006			@ .............ca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	mov	pc, lr

/*
 * cpu_mohawk_reset(loc)
 *
 * Perform a soft reset of the system.  Put the CPU into the
 * same state as it would be if it had been reset, and branch
 * to what would be the reset vector.
 *
 * loc: location to jump to for soft reset
 *
 * (same as arm926)
 */
	.align	5
	.pushsection	.idmap.text, "ax"
ENTRY(cpu_mohawk_reset)
	mov	ip, #0
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I,D caches
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
	mrc	p15, 0, ip, c1, c0, 0		@ ctrl register
	bic	ip, ip, #0x0007			@ .............cam
	bic	ip, ip, #0x1100			@ ...i...s........
	mcr	p15, 0, ip, c1, c0, 0		@ ctrl register
	mov	pc, r0
ENDPROC(cpu_mohawk_reset)
	.popsection

/*
 * cpu_mohawk_do_idle()
 *
 * Called with IRQs disabled
 */
	.align	5
ENTRY(cpu_mohawk_do_idle)
	mov	r0, #0
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer
	mcr	p15, 0, r0, c7, c0, 4		@ wait for interrupt
	mov	pc, lr

/*
 *	flush_icache_all()
 *
 *	Unconditionally clean and invalidate the entire icache.
 */
ENTRY(mohawk_flush_icache_all)
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	mov	pc, lr
ENDPROC(mohawk_flush_icache_all)

/*
 *	flush_user_cache_all()
 *
 *	Clean and invalidate all cache entries in a particular
 *	address space.
 */
ENTRY(mohawk_flush_user_cache_all)
	/* FALLTHROUGH */

/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
ENTRY(mohawk_flush_kern_cache_all)
	mov	r2, #VM_EXEC
	mov	ip, #0
__flush_whole_cache:
	mcr	p15, 0, ip, c7, c14, 0		@ clean & invalidate all D cache
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c5, 0		@ invalidate I cache
	mcrne	p15, 0, ip, c7, c10, 0		@ drain write buffer
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
 *
 * (same as arm926)
 */
ENTRY(mohawk_flush_user_cache_range)
	mov	ip, #0
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
ENTRY(mohawk_coherent_kern_range)
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
 *
 * (same as arm926)
 */
ENTRY(mohawk_coherent_user_range)
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	r0, #0
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
ENTRY(mohawk_flush_kern_dcache_area)
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
mohawk_dma_inv_range:
	tst	r0, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r0, c7, c10, 1		@ clean D entry
	tst	r1, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r1, c7, c10, 1		@ clean D entry
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
mohawk_dma_clean_range:
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
ENTRY(mohawk_dma_flush_range)
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:
	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
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
ENTRY(mohawk_dma_map_area)
	add	r1, r1, r0
	cmp	r2, #DMA_TO_DEVICE
	beq	mohawk_dma_clean_range
	bcs	mohawk_dma_inv_range
	b	mohawk_dma_flush_range
ENDPROC(mohawk_dma_map_area)

/*
 *	dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(mohawk_dma_unmap_area)
	mov	pc, lr
ENDPROC(mohawk_dma_unmap_area)

	.globl	mohawk_flush_kern_cache_louis
	.equ	mohawk_flush_kern_cache_louis, mohawk_flush_kern_cache_all

	@ define struct cpu_cache_fns (see <asm/cacheflush.h> and proc-macros.S)
	define_cache_functions mohawk

ENTRY(cpu_mohawk_dcache_clean_area)
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	subs	r1, r1, #CACHE_DLINESIZE
	bhi	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 * cpu_mohawk_switch_mm(pgd)
 *
 * Set the translation base pointer to be as described by pgd.
 *
 * pgd: new page tables
 */
	.align	5
ENTRY(cpu_mohawk_switch_mm)
	mov	ip, #0
	mcr	p15, 0, ip, c7, c14, 0		@ clean & invalidate all D cache
	mcr	p15, 0, ip, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	orr	r0, r0, #0x18			@ cache the page table in L2
	mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
	mov	pc, lr

/*
 * cpu_mohawk_set_pte_ext(ptep, pte, ext)
 *
 * Set a PTE and flush it out
 */
	.align	5
ENTRY(cpu_mohawk_set_pte_ext)
	armv3_set_pte_ext
	mov	r0, r0
	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

.globl	cpu_mohawk_suspend_size
.equ	cpu_mohawk_suspend_size, 4 * 6
#ifdef CONFIG_PM_SLEEP
ENTRY(cpu_mohawk_do_suspend)
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
ENDPROC(cpu_mohawk_do_suspend)

ENTRY(cpu_mohawk_do_resume)
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
ENDPROC(cpu_mohawk_do_resume)
#endif

	__CPUINIT

	.type	__mohawk_setup, #function
__mohawk_setup:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7		@ invalidate I,D caches
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer
	mcr	p15, 0, r0, c8, c7		@ invalidate I,D TLBs
	orr	r4, r4, #0x18			@ cache the page table in L2
	mcr	p15, 0, r4, c2, c0, 0		@ load page table pointer

	mov	r0, #0				@ don't allow CP access
	mcr	p15, 0, r0, c15, c1, 0		@ write CP access register

	adr	r5, mohawk_crval
	ldmia	r5, {r5, r6}
	mrc	p15, 0, r0, c1, c0		@ get control register
	bic	r0, r0, r5
	orr	r0, r0, r6
	mov	pc, lr

	.size	__mohawk_setup, . - __mohawk_setup

	/*
	 *  R
	 * .RVI ZFRS BLDP WCAM
	 * .011 1001 ..00 0101
	 *
	 */
	.type	mohawk_crval, #object
mohawk_crval:
	crval	clear=0x00007f3f, mmuset=0x00003905, ucset=0x00001134

	__INITDATA

	@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
	define_processor_functions mohawk, dabort=v5t_early_abort, pabort=legacy_pabort

	.section ".rodata"

	string	cpu_arch_name, "armv5te"
	string	cpu_elf_name, "v5"
	string	cpu_mohawk_name, "Marvell 88SV331x"

	.align

	.section ".proc.info.init", #alloc, #execinstr

	.type	__88sv331x_proc_info,#object
__88sv331x_proc_info:
	.long	0x56158000			@ Marvell 88SV331x (MOHAWK)
	.long	0xfffff000
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
	b	__mohawk_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_EDSP
	.long	cpu_mohawk_name
	.long	mohawk_processor_functions
	.long	v4wbi_tlb_fns
	.long	v4wb_user_fns
	.long	mohawk_cache_fns
	.size	__88sv331x_proc_info, . - __88sv331x_proc_info
