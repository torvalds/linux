/*
 *  linux/arch/arm/mm/proc-arm720.S: MMU functions for ARM720
 *
 *  Copyright (C) 2000 Steve Hill (sjhill@cotw.com)
 *                     Rob Scott (rscott@mtrob.fdns.net)
 *  Copyright (C) 2000 ARM Limited, Deep Blue Solutions Ltd.
 *  hacked for non-paged-MM by Hyok S. Choi, 2004.
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
 * functions on the ARM720T.  The ARM720T has a writethrough IDC
 * cache, so we don't need to clean it.
 *
 *  Changelog:
 *   05-09-2000 SJH	Created by moving 720 specific functions
 *			out of 'proc-arm6,7.S' per RMK discussion
 *   07-25-2000 SJH	Added idle function.
 *   08-25-2000	DBS	Updated for integration of ARM Ltd version.
 *   04-20-2004 HSC	modified for non-paged memory management mode.
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
 * Function: arm720_proc_init (void)
 *	   : arm720_proc_fin (void)
 *
 * Notes   : This processor does not require these
 */
ENTRY(cpu_arm720_dcache_clean_area)
ENTRY(cpu_arm720_proc_init)
		mov	pc, lr

ENTRY(cpu_arm720_proc_fin)
		mrc	p15, 0, r0, c1, c0, 0
		bic	r0, r0, #0x1000			@ ...i............
		bic	r0, r0, #0x000e			@ ............wca.
		mcr	p15, 0, r0, c1, c0, 0		@ disable caches
		mov	pc, lr

/*
 * Function: arm720_proc_do_idle(void)
 * Params  : r0 = unused
 * Purpose : put the processor in proper idle mode
 */
ENTRY(cpu_arm720_do_idle)
		mov	pc, lr

/*
 * Function: arm720_switch_mm(unsigned long pgd_phys)
 * Params  : pgd_phys	Physical address of page table
 * Purpose : Perform a task switch, saving the old process' state and restoring
 *	     the new.
 */
ENTRY(cpu_arm720_switch_mm)
#ifdef CONFIG_MMU
		mov	r1, #0
		mcr	p15, 0, r1, c7, c7, 0		@ invalidate cache
		mcr	p15, 0, r0, c2, c0, 0		@ update page table ptr
		mcr	p15, 0, r1, c8, c7, 0		@ flush TLB (v4)
#endif
		mov	pc, lr

/*
 * Function: arm720_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext)
 * Params  : r0 = Address to set
 *	   : r1 = value to set
 * Purpose : Set a PTE and flush it out of any WB cache
 */
	.align	5
ENTRY(cpu_arm720_set_pte_ext)
#ifdef CONFIG_MMU
	armv3_set_pte_ext wc_disable=0
#endif
	mov	pc, lr

/*
 * Function: arm720_reset
 * Params  : r0 = address to jump to
 * Notes   : This sets up everything for a reset
 */
ENTRY(cpu_arm720_reset)
		mov	ip, #0
		mcr	p15, 0, ip, c7, c7, 0		@ invalidate cache
#ifdef CONFIG_MMU
		mcr	p15, 0, ip, c8, c7, 0		@ flush TLB (v4)
#endif
		mrc	p15, 0, ip, c1, c0, 0		@ get ctrl register
		bic	ip, ip, #0x000f			@ ............wcam
		bic	ip, ip, #0x2100			@ ..v....s........
		mcr	p15, 0, ip, c1, c0, 0		@ ctrl register
		mov	pc, r0

	__CPUINIT

	.type	__arm710_setup, #function
__arm710_setup:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7, 0		@ invalidate caches
#ifdef CONFIG_MMU
	mcr	p15, 0, r0, c8, c7, 0		@ flush TLB (v4)
#endif
	mrc	p15, 0, r0, c1, c0		@ get control register
	ldr	r5, arm710_cr1_clear
	bic	r0, r0, r5
	ldr	r5, arm710_cr1_set
	orr	r0, r0, r5
	mov	pc, lr				@ __ret (head.S)
	.size	__arm710_setup, . - __arm710_setup

	/*
	 *  R
	 * .RVI ZFRS BLDP WCAM
	 * .... 0001 ..11 1101
	 * 
	 */
	.type	arm710_cr1_clear, #object
	.type	arm710_cr1_set, #object
arm710_cr1_clear:
	.word	0x0f3f
arm710_cr1_set:
	.word	0x013d

	.type	__arm720_setup, #function
__arm720_setup:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7, 0		@ invalidate caches
#ifdef CONFIG_MMU
	mcr	p15, 0, r0, c8, c7, 0		@ flush TLB (v4)
#endif
	adr	r5, arm720_crval
	ldmia	r5, {r5, r6}
	mrc	p15, 0, r0, c1, c0		@ get control register
	bic	r0, r0, r5
	orr	r0, r0, r6
	mov	pc, lr				@ __ret (head.S)
	.size	__arm720_setup, . - __arm720_setup

	/*
	 *  R
	 * .RVI ZFRS BLDP WCAM
	 * ..1. 1001 ..11 1101
	 * 
	 */
	.type	arm720_crval, #object
arm720_crval:
	crval	clear=0x00002f3f, mmuset=0x0000213d, ucset=0x00000130

		__INITDATA
	@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
	define_processor_functions arm720, dabort=v4t_late_abort, pabort=legacy_pabort

		.section ".rodata"

	string	cpu_arch_name, "armv4t"
	string	cpu_elf_name, "v4"
	string	cpu_arm710_name, "ARM710T"
	string	cpu_arm720_name, "ARM720T"

		.align

/*
 * See <asm/procinfo.h> for a definition of this structure.
 */
	
		.section ".proc.info.init", #alloc, #execinstr

.macro arm720_proc_info name:req, cpu_val:req, cpu_mask:req, cpu_name:req, cpu_flush:req
		.type	__\name\()_proc_info,#object
__\name\()_proc_info:
		.long	\cpu_val
		.long	\cpu_mask
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
		b	\cpu_flush				@ cpu_flush
		.long	cpu_arch_name				@ arch_name
		.long	cpu_elf_name				@ elf_name
		.long	HWCAP_SWP | HWCAP_HALF | HWCAP_THUMB	@ elf_hwcap
		.long	\cpu_name
		.long	arm720_processor_functions
		.long	v4_tlb_fns
		.long	v4wt_user_fns
		.long	v4_cache_fns
		.size	__\name\()_proc_info, . - __\name\()_proc_info
.endm

	arm720_proc_info arm710, 0x41807100, 0xffffff00, cpu_arm710_name, __arm710_setup
	arm720_proc_info arm720, 0x41807200, 0xffffff00, cpu_arm720_name, __arm720_setup
