/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/mm/proc-sa1100.S
 *
 *  Copyright (C) 1997-2002 Russell King
 *  hacked for non-paged-MM by Hyok S. Choi, 2003.
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
#include <linux/pgtable.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/hwcap.h>
#include <mach/hardware.h>
#include <asm/pgtable-hwdef.h>

#include "proc-macros.S"

/*
 * the cache line size of the I and D cache
 */
#define DCACHELINESIZE	32

	.section .text

/*
 * cpu_sa1100_proc_init()
 */
ENTRY(cpu_sa1100_proc_init)
	mov	r0, #0
	mcr	p15, 0, r0, c15, c1, 2		@ Enable clock switching
	mcr	p15, 0, r0, c9, c0, 5		@ Allow read-buffer operations from userland
	ret	lr

/*
 * cpu_sa1100_proc_fin()
 *
 * Prepare the CPU for reset:
 *  - Disable interrupts
 *  - Clean and turn off caches.
 */
ENTRY(cpu_sa1100_proc_fin)
	mcr	p15, 0, ip, c15, c2, 2		@ Disable clock switching
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1000			@ ...i............
	bic	r0, r0, #0x000e			@ ............wca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	ret	lr

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
	.pushsection	.idmap.text, "ax"
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
	ret	r0
ENDPROC(cpu_sa1100_reset)
	.popsection

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
	ret	lr

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
	ret	lr

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
	ret	lr
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
	ret	lr

.globl	cpu_sa1100_suspend_size
.equ	cpu_sa1100_suspend_size, 4 * 3
#ifdef CONFIG_ARM_CPU_SUSPEND
ENTRY(cpu_sa1100_do_suspend)
	stmfd	sp!, {r4 - r6, lr}
	mrc	p15, 0, r4, c3, c0, 0		@ domain ID
	mrc	p15, 0, r5, c13, c0, 0		@ PID
	mrc	p15, 0, r6, c1, c0, 0		@ control reg
	stmia	r0, {r4 - r6}			@ store cp regs
	ldmfd	sp!, {r4 - r6, pc}
ENDPROC(cpu_sa1100_do_suspend)

ENTRY(cpu_sa1100_do_resume)
	ldmia	r0, {r4 - r6}			@ load cp regs
	mov	ip, #0
	mcr	p15, 0, ip, c8, c7, 0		@ flush I+D TLBs
	mcr	p15, 0, ip, c7, c7, 0		@ flush I&D cache
	mcr	p15, 0, ip, c9, c0, 0		@ invalidate RB
	mcr	p15, 0, ip, c9, c0, 5		@ allow user space to use RB

	mcr	p15, 0, r4, c3, c0, 0		@ domain ID
	mcr	p15, 0, r1, c2, c0, 0		@ translation table base addr
	mcr	p15, 0, r5, c13, c0, 0		@ PID
	mov	r0, r6				@ control register
	b	cpu_resume_mmu
ENDPROC(cpu_sa1100_do_resume)
#endif

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
	ret	lr
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
 * SA1100 and SA1110 share the same function calls
 */

	@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
	define_processor_functions sa1100, dabort=v4_early_abort, pabort=legacy_pabort, suspend=1

	.section ".rodata"

	string	cpu_arch_name, "armv4"
	string	cpu_elf_name, "v4"
	string	cpu_sa1100_name, "StrongARM-1100"
	string	cpu_sa1110_name, "StrongARM-1110"

	.align

	.section ".proc.info.init", "a"

.macro sa1100_proc_info name:req, cpu_val:req, cpu_mask:req, cpu_name:req
	.type	__\name\()_proc_info,#object
__\name\()_proc_info:
	.long	\cpu_val
	.long	\cpu_mask
	.long   PMD_TYPE_SECT | \
		PMD_SECT_BUFFERABLE | \
		PMD_SECT_CACHEABLE | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	.long   PMD_TYPE_SECT | \
		PMD_SECT_AP_WRITE | \
		PMD_SECT_AP_READ
	initfn	__sa1100_setup, __\name\()_proc_info
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF | HWCAP_26BIT | HWCAP_FAST_MULT
	.long	\cpu_name
	.long	sa1100_processor_functions
	.long	v4wb_tlb_fns
	.long	v4_mc_user_fns
	.long	v4wb_cache_fns
	.size	__\name\()_proc_info, . - __\name\()_proc_info
.endm

	sa1100_proc_info sa1100, 0x4401a110, 0xfffffff0, cpu_sa1100_name
	sa1100_proc_info sa1110, 0x6901b110, 0xfffffff0, cpu_sa1110_name
