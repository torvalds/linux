/*
 *  linux/arch/arm/mm/proc-arm6,7.S
 *
 *  Copyright (C) 1997-2000 Russell King
 *  hacked for non-paged-MM by Hyok S. Choi, 2003.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  These are the low level assembler for performing cache and TLB
 *  functions on the ARM610 & ARM710.
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

ENTRY(cpu_arm6_dcache_clean_area)
ENTRY(cpu_arm7_dcache_clean_area)
		mov	pc, lr

/*
 * Function: arm6_7_data_abort ()
 *
 * Params  : r2 = address of aborted instruction
 *	   : sp = pointer to registers
 *
 * Purpose : obtain information about current aborted instruction
 *
 * Returns : r0 = address of abort
 *	   : r1 = FSR
 */

ENTRY(cpu_arm7_data_abort)
	mrc	p15, 0, r1, c5, c0, 0		@ get FSR
	mrc	p15, 0, r0, c6, c0, 0		@ get FAR
	ldr	r8, [r2]			@ read arm instruction
	tst	r8, #1 << 20			@ L = 0 -> write?
	orreq	r1, r1, #1 << 11		@ yes.
	and	r7, r8, #15 << 24
	add	pc, pc, r7, lsr #22		@ Now branch to the relevant processing routine
	nop

/* 0 */	b	.data_unknown
/* 1 */	mov	pc, lr				@ swp
/* 2 */	b	.data_unknown
/* 3 */	b	.data_unknown
/* 4 */	b	.data_arm_lateldrpostconst	@ ldr	rd, [rn], #m
/* 5 */	b	.data_arm_lateldrpreconst	@ ldr	rd, [rn, #m]
/* 6 */	b	.data_arm_lateldrpostreg	@ ldr	rd, [rn], rm
/* 7 */	b	.data_arm_lateldrprereg		@ ldr	rd, [rn, rm]
/* 8 */	b	.data_arm_ldmstm		@ ldm*a	rn, <rlist>
/* 9 */	b	.data_arm_ldmstm		@ ldm*b	rn, <rlist>
/* a */	b	.data_unknown
/* b */	b	.data_unknown
/* c */	mov	pc, lr				@ ldc	rd, [rn], #m	@ Same as ldr	rd, [rn], #m
/* d */	mov	pc, lr				@ ldc	rd, [rn, #m]
/* e */	b	.data_unknown
/* f */
.data_unknown:	@ Part of jumptable
	mov	r0, r2
	mov	r1, r8
	mov	r2, sp
	bl	baddataabort
	b	ret_from_exception

ENTRY(cpu_arm6_data_abort)
	mrc	p15, 0, r1, c5, c0, 0		@ get FSR
	mrc	p15, 0, r0, c6, c0, 0		@ get FAR
	ldr	r8, [r2]			@ read arm instruction
	tst	r8, #1 << 20			@ L = 0 -> write?
	orreq	r1, r1, #1 << 11		@ yes.
	and	r7, r8, #14 << 24
	teq	r7, #8 << 24			@ was it ldm/stm
	movne	pc, lr

.data_arm_ldmstm:
	tst	r8, #1 << 21			@ check writeback bit
	moveq	pc, lr				@ no writeback -> no fixup
	mov	r7, #0x11
	orr	r7, r7, #0x1100
	and	r6, r8, r7
	and	r2, r8, r7, lsl #1
	add	r6, r6, r2, lsr #1
	and	r2, r8, r7, lsl #2
	add	r6, r6, r2, lsr #2
	and	r2, r8, r7, lsl #3
	add	r6, r6, r2, lsr #3
	add	r6, r6, r6, lsr #8
	add	r6, r6, r6, lsr #4
	and	r6, r6, #15			@ r6 = no. of registers to transfer.
	and	r5, r8, #15 << 16		@ Extract 'n' from instruction
	ldr	r7, [sp, r5, lsr #14]		@ Get register 'Rn'
	tst	r8, #1 << 23			@ Check U bit
	subne	r7, r7, r6, lsl #2		@ Undo increment
	addeq	r7, r7, r6, lsl #2		@ Undo decrement
	str	r7, [sp, r5, lsr #14]		@ Put register 'Rn'
	mov	pc, lr

.data_arm_apply_r6_and_rn:
	and	r5, r8, #15 << 16		@ Extract 'n' from instruction
	ldr	r7, [sp, r5, lsr #14]		@ Get register 'Rn'
	tst	r8, #1 << 23			@ Check U bit
	subne	r7, r7, r6			@ Undo incrmenet
	addeq	r7, r7, r6			@ Undo decrement
	str	r7, [sp, r5, lsr #14]		@ Put register 'Rn'
	mov	pc, lr

.data_arm_lateldrpreconst:
	tst	r8, #1 << 21			@ check writeback bit
	moveq	pc, lr				@ no writeback -> no fixup
.data_arm_lateldrpostconst:
	movs	r2, r8, lsl #20			@ Get offset
	moveq	pc, lr				@ zero -> no fixup
	and	r5, r8, #15 << 16		@ Extract 'n' from instruction
	ldr	r7, [sp, r5, lsr #14]		@ Get register 'Rn'
	tst	r8, #1 << 23			@ Check U bit
	subne	r7, r7, r2, lsr #20		@ Undo increment
	addeq	r7, r7, r2, lsr #20		@ Undo decrement
	str	r7, [sp, r5, lsr #14]		@ Put register 'Rn'
	mov	pc, lr

.data_arm_lateldrprereg:
	tst	r8, #1 << 21			@ check writeback bit
	moveq	pc, lr				@ no writeback -> no fixup
.data_arm_lateldrpostreg:
	and	r7, r8, #15			@ Extract 'm' from instruction
	ldr	r6, [sp, r7, lsl #2]		@ Get register 'Rm'
	mov	r5, r8, lsr #7			@ get shift count
	ands	r5, r5, #31
	and	r7, r8, #0x70			@ get shift type
	orreq	r7, r7, #8			@ shift count = 0
	add	pc, pc, r7
	nop

	mov	r6, r6, lsl r5			@ 0: LSL #!0
	b	.data_arm_apply_r6_and_rn
	b	.data_arm_apply_r6_and_rn	@ 1: LSL #0
	nop
	b	.data_unknown			@ 2: MUL?
	nop
	b	.data_unknown			@ 3: MUL?
	nop
	mov	r6, r6, lsr r5			@ 4: LSR #!0
	b	.data_arm_apply_r6_and_rn
	mov	r6, r6, lsr #32			@ 5: LSR #32
	b	.data_arm_apply_r6_and_rn
	b	.data_unknown			@ 6: MUL?
	nop
	b	.data_unknown			@ 7: MUL?
	nop
	mov	r6, r6, asr r5			@ 8: ASR #!0
	b	.data_arm_apply_r6_and_rn
	mov	r6, r6, asr #32			@ 9: ASR #32
	b	.data_arm_apply_r6_and_rn
	b	.data_unknown			@ A: MUL?
	nop
	b	.data_unknown			@ B: MUL?
	nop
	mov	r6, r6, ror r5			@ C: ROR #!0
	b	.data_arm_apply_r6_and_rn
	mov	r6, r6, rrx			@ D: RRX
	b	.data_arm_apply_r6_and_rn
	b	.data_unknown			@ E: MUL?
	nop
	b	.data_unknown			@ F: MUL?

/*
 * Function: arm6_7_proc_init (void)
 *	   : arm6_7_proc_fin (void)
 *
 * Notes   : This processor does not require these
 */
ENTRY(cpu_arm6_proc_init)
ENTRY(cpu_arm7_proc_init)
		mov	pc, lr

ENTRY(cpu_arm6_proc_fin)
ENTRY(cpu_arm7_proc_fin)
		mov	r0, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
		msr	cpsr_c, r0
		mov	r0, #0x31			@ ....S..DP...M
		mcr	p15, 0, r0, c1, c0, 0		@ disable caches
		mov	pc, lr

ENTRY(cpu_arm6_do_idle)
ENTRY(cpu_arm7_do_idle)
		mov	pc, lr

/*
 * Function: arm6_7_switch_mm(unsigned long pgd_phys)
 * Params  : pgd_phys	Physical address of page table
 * Purpose : Perform a task switch, saving the old processes state, and restoring
 *	     the new.
 */
ENTRY(cpu_arm6_switch_mm)
ENTRY(cpu_arm7_switch_mm)
#ifdef CONFIG_MMU
		mov	r1, #0
		mcr	p15, 0, r1, c7, c0, 0		@ flush cache
		mcr	p15, 0, r0, c2, c0, 0		@ update page table ptr
		mcr	p15, 0, r1, c5, c0, 0		@ flush TLBs
#endif
		mov	pc, lr

/*
 * Function: arm6_7_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext)
 * Params  : r0 = Address to set
 *	   : r1 = value to set
 * Purpose : Set a PTE and flush it out of any WB cache
 */
	.align	5
ENTRY(cpu_arm6_set_pte_ext)
ENTRY(cpu_arm7_set_pte_ext)
#ifdef CONFIG_MMU
	armv3_set_pte_ext wc_disable=0
#endif /* CONFIG_MMU */
	mov	pc, lr

/*
 * Function: _arm6_7_reset
 * Params  : r0 = address to jump to
 * Notes   : This sets up everything for a reset
 */
ENTRY(cpu_arm6_reset)
ENTRY(cpu_arm7_reset)
		mov	r1, #0
		mcr	p15, 0, r1, c7, c0, 0		@ flush cache
#ifdef CONFIG_MMU
		mcr	p15, 0, r1, c5, c0, 0		@ flush TLB
#endif
		mov	r1, #0x30
		mcr	p15, 0, r1, c1, c0, 0		@ turn off MMU etc
		mov	pc, r0

		__INIT

		.type	__arm6_setup, #function
__arm6_setup:	mov	r0, #0
		mcr	p15, 0, r0, c7, c0		@ flush caches on v3
#ifdef CONFIG_MMU
		mcr	p15, 0, r0, c5, c0		@ flush TLBs on v3
		mov	r0, #0x3d			@ . ..RS BLDP WCAM
		orr	r0, r0, #0x100			@ . ..01 0011 1101
#else
		mov	r0, #0x3c			@ . ..RS BLDP WCA.
#endif
		mov	pc, lr
		.size	__arm6_setup, . - __arm6_setup

		.type	__arm7_setup, #function
__arm7_setup:	mov	r0, #0
		mcr	p15, 0, r0, c7, c0		@ flush caches on v3
#ifdef CONFIG_MMU
		mcr	p15, 0, r0, c5, c0		@ flush TLBs on v3
		mcr	p15, 0, r0, c3, c0		@ load domain access register
		mov	r0, #0x7d			@ . ..RS BLDP WCAM
		orr	r0, r0, #0x100			@ . ..01 0111 1101
#else
		mov	r0, #0x7c			@ . ..RS BLDP WCA.
#endif
		mov	pc, lr
		.size	__arm7_setup, . - __arm7_setup

		__INITDATA

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
		.type	arm6_processor_functions, #object
ENTRY(arm6_processor_functions)
		.word	cpu_arm6_data_abort
		.word	legacy_pabort
		.word	cpu_arm6_proc_init
		.word	cpu_arm6_proc_fin
		.word	cpu_arm6_reset
		.word	cpu_arm6_do_idle
		.word	cpu_arm6_dcache_clean_area
		.word	cpu_arm6_switch_mm
		.word	cpu_arm6_set_pte_ext
		.size	arm6_processor_functions, . - arm6_processor_functions

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
		.type	arm7_processor_functions, #object
ENTRY(arm7_processor_functions)
		.word	cpu_arm7_data_abort
		.word	legacy_pabort
		.word	cpu_arm7_proc_init
		.word	cpu_arm7_proc_fin
		.word	cpu_arm7_reset
		.word	cpu_arm7_do_idle
		.word	cpu_arm7_dcache_clean_area
		.word	cpu_arm7_switch_mm
		.word	cpu_arm7_set_pte_ext
		.size	arm7_processor_functions, . - arm7_processor_functions

		.section ".rodata"

		.type	cpu_arch_name, #object
cpu_arch_name:	.asciz	"armv3"
		.size	cpu_arch_name, . - cpu_arch_name

		.type	cpu_elf_name, #object
cpu_elf_name:	.asciz	"v3"
		.size	cpu_elf_name, . - cpu_elf_name

		.type	cpu_arm6_name, #object
cpu_arm6_name:	.asciz	"ARM6"
		.size	cpu_arm6_name, . - cpu_arm6_name

		.type	cpu_arm610_name, #object
cpu_arm610_name:
		.asciz	"ARM610"
		.size	cpu_arm610_name, . - cpu_arm610_name

		.type	cpu_arm7_name, #object
cpu_arm7_name:	.asciz	"ARM7"
		.size	cpu_arm7_name, . - cpu_arm7_name

		.type	cpu_arm710_name, #object
cpu_arm710_name:
		.asciz	"ARM710"
		.size	cpu_arm710_name, . - cpu_arm710_name

		.align

		.section ".proc.info.init", #alloc, #execinstr

		.type	__arm6_proc_info, #object
__arm6_proc_info:
		.long	0x41560600
		.long	0xfffffff0
		.long	0x00000c1e
		.long   PMD_TYPE_SECT | \
			PMD_BIT4 | \
			PMD_SECT_AP_WRITE | \
			PMD_SECT_AP_READ
		b	__arm6_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_26BIT
		.long	cpu_arm6_name
		.long	arm6_processor_functions
		.long	v3_tlb_fns
		.long	v3_user_fns
		.long	v3_cache_fns
		.size	__arm6_proc_info, . - __arm6_proc_info

		.type	__arm610_proc_info, #object
__arm610_proc_info:
		.long	0x41560610
		.long	0xfffffff0
		.long	0x00000c1e
		.long   PMD_TYPE_SECT | \
			PMD_BIT4 | \
			PMD_SECT_AP_WRITE | \
			PMD_SECT_AP_READ
		b	__arm6_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_26BIT
		.long	cpu_arm610_name
		.long	arm6_processor_functions
		.long	v3_tlb_fns
		.long	v3_user_fns
		.long	v3_cache_fns
		.size	__arm610_proc_info, . - __arm610_proc_info

		.type	__arm7_proc_info, #object
__arm7_proc_info:
		.long	0x41007000
		.long	0xffffff00
		.long	0x00000c1e
		.long   PMD_TYPE_SECT | \
			PMD_BIT4 | \
			PMD_SECT_AP_WRITE | \
			PMD_SECT_AP_READ
		b	__arm7_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_26BIT
		.long	cpu_arm7_name
		.long	arm7_processor_functions
		.long	v3_tlb_fns
		.long	v3_user_fns
		.long	v3_cache_fns
		.size	__arm7_proc_info, . - __arm7_proc_info

		.type	__arm710_proc_info, #object
__arm710_proc_info:
		.long	0x41007100
		.long	0xfff8ff00
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
		b	__arm7_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_26BIT
		.long	cpu_arm710_name
		.long	arm7_processor_functions
		.long	v3_tlb_fns
		.long	v3_user_fns
		.long	v3_cache_fns
		.size	__arm710_proc_info, . - __arm710_proc_info
