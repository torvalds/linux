/*
 *  linux/arch/arm26/mm/proc-arm2,3.S
 *
 *  Copyright (C) 1997-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  MMU functions for ARM2,3
 *
 *  These are the low level assembler for performing cache
 *  and memory functions on ARM2, ARM250 and ARM3 processors.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/asm_offsets.h>
#include <asm/procinfo.h>
#include <asm/ptrace.h>

/*
 * MEMC workhorse code.  It's both a horse which things it's a pig.
 */
/*
 * Function: cpu_memc_update_entry(pgd_t *pgd, unsigned long phys_pte, unsigned long addr)
 * Params  : pgd	Page tables/MEMC mapping
 *         : phys_pte	physical address, or PTE
 *         : addr	virtual address
 */
ENTRY(cpu_memc_update_entry)
		tst	r1, #PAGE_PRESENT		@ is the page present
		orreq	r1, r1, #PAGE_OLD | PAGE_CLEAN
		moveq	r2, #0x01f00000
		mov	r3, r1, lsr #13			@ convert to physical page nr
		and	r3, r3, #0x3fc
		adr	ip, memc_phys_table_32
		ldr	r3, [ip, r3]
		tst	r1, #PAGE_OLD | PAGE_NOT_USER
		biceq	r3, r3, #0x200
		tsteq	r1, #PAGE_READONLY | PAGE_CLEAN
		biceq	r3, r3, #0x300
		mov	r2, r2, lsr #15			@ virtual -> nr
		orr	r3, r3, r2, lsl #15
		and	r2, r2, #0x300
		orr	r3, r3, r2, lsl #2
		and	r2, r3, #255
		sub	r0, r0, #256 * 4
		str	r3, [r0, r2, lsl #2]
		strb	r3, [r3]
		movs	pc, lr
/*
 * Params  : r0 = preserved
 *         : r1 = memc table base (preserved)
 *         : r2 = page table entry
 *         : r3 = preserved
 *         : r4 = unused
 *         : r5 = memc physical address translation table
 *         : ip = virtual address (preserved)
 */
update_pte:
		mov	r4, r2, lsr #13
		and	r4, r4, #0x3fc
		ldr	r4, [r5, r4]			@ covert to MEMC page

		tst	r2, #PAGE_OLD | PAGE_NOT_USER	@ check for MEMC read
		biceq	r4, r4, #0x200
		tsteq	r2, #PAGE_READONLY | PAGE_CLEAN	@ check for MEMC write
		biceq	r4, r4, #0x300

		orr	r4, r4, ip
		and	r2, ip, #0x01800000
		orr	r4, r4, r2, lsr #13

		and	r2, r4, #255
		str	r4, [r1, r2, lsl #2]
		movs	pc, lr

/*
 * Params  : r0 = preserved
 *         : r1 = memc table base (preserved)
 *         : r2 = page table base
 *         : r3 = preserved
 *         : r4 = unused
 *         : r5 = memc physical address translation table
 *         : ip = virtual address (updated)
 */
update_pte_table:
		stmfd	sp!, {r0, lr}
		bic	r0, r2, #3
1:		ldr	r2, [r0], #4			@ get entry
		tst	r2, #PAGE_PRESENT		@ page present
		blne	update_pte			@ process pte
		add	ip, ip, #32768			@ increment virt addr
		ldr	r2, [r0], #4			@ get entry
		tst	r2, #PAGE_PRESENT		@ page present
		blne	update_pte			@ process pte
		add	ip, ip, #32768			@ increment virt addr
		ldr	r2, [r0], #4			@ get entry
		tst	r2, #PAGE_PRESENT		@ page present
		blne	update_pte			@ process pte
		add	ip, ip, #32768			@ increment virt addr
		ldr	r2, [r0], #4			@ get entry
		tst	r2, #PAGE_PRESENT		@ page present
		blne	update_pte			@ process pte
		add	ip, ip, #32768			@ increment virt addr
		tst	ip, #32768 * 31			@ finished?
		bne	1b
		ldmfd	sp!, {r0, pc}^

/*
 * Function: cpu_memc_update_all(pgd_t *pgd)
 * Params  : pgd	Page tables/MEMC mapping
 * Notes   : this is optimised for 32k pages
 */
ENTRY(cpu_memc_update_all)
		stmfd	sp!, {r4, r5, lr}
		bl	clear_tables
		sub	r1, r0, #256 * 4		@ start of MEMC tables
		adr	r5, memc_phys_table_32		@ Convert to logical page number
		mov	ip, #0				@ virtual address
1:		ldmia	r0!, {r2, r3}                   @ load two pgd entries
		tst	r2, #PAGE_PRESENT               @ is pgd entry present?
		addeq	ip, ip, #1048576        @FIXME - PAGE_PRESENT is for PTEs technically...
		blne	update_pte_table
		mov	r2, r3
		tst	r2, #PAGE_PRESENT		@ is pgd entry present?
		addeq	ip, ip, #1048576
		blne	update_pte_table
		teq	ip, #32 * 1048576
		bne	1b
		ldmfd	sp!, {r4, r5, pc}^

/*
 * Build the table to map from physical page number to memc page number
 */
		.type	memc_phys_table_32, #object
memc_phys_table_32:
		.irp	b7, 0x00, 0x80
		.irp	b6, 0x00, 0x02
		.irp	b5, 0x00, 0x04
		.irp	b4, 0x00, 0x01

		.irp	b3, 0x00, 0x40
		.irp	b2, 0x00, 0x20
		.irp	b1, 0x00, 0x10
		.irp	b0, 0x00, 0x08
		.long	0x03800300 + \b7 + \b6 + \b5 + \b4 + \b3 + \b2 + \b1 + \b0
		.endr
		.endr
		.endr
		.endr

		.endr
		.endr
		.endr
		.endr
		.size	memc_phys_table_32, . - memc_phys_table_32

/*
 * helper for cpu_memc_update_all, this clears out all
 * mappings, setting them close to the top of memory,
 * and inaccessible (0x01f00000).
 * Params  : r0 = page table pointer
 */
clear_tables:	ldr	r1, _arm3_set_pgd - 4
		ldr	r2, [r1]
		sub	r1, r0, #256 * 4		@ start of MEMC tables
		add	r2, r1, r2, lsl #2		@ end of tables
		mov	r3, #0x03f00000			@ Default mapping (null mapping)
		orr	r3, r3, #0x00000f00
		orr	r4, r3, #1
		orr	r5, r3, #2
		orr	ip, r3, #3
1:		stmia	r1!, {r3, r4, r5, ip}
		add	r3, r3, #4
		add	r4, r4, #4
		add	r5, r5, #4
		add	ip, ip, #4
		stmia	r1!, {r3, r4, r5, ip}
		add	r3, r3, #4
		add	r4, r4, #4
		add	r5, r5, #4
		add	ip, ip, #4
		teq	r1, r2
		bne	1b
		mov	pc, lr

/*
 * Function: *_set_pgd(pgd_t *pgd)
 * Params  : pgd	New page tables/MEMC mapping
 * Purpose : update MEMC hardware with new mapping
 */
		.word	page_nr   @ extern - declared in mm-memc.c
_arm3_set_pgd:	mcr	p15, 0, r1, c1, c0, 0		@ flush cache
_arm2_set_pgd:	stmfd	sp!, {lr}
		ldr	r1, _arm3_set_pgd - 4
		ldr	r2, [r1]
		sub	r0, r0, #256 * 4		@ start of MEMC tables
		add	r1, r0, r2, lsl #2		@ end of tables
1:		ldmia	r0!, {r2, r3, ip, lr}
		strb	r2, [r2]
		strb	r3, [r3]
		strb	ip, [ip]
		strb	lr, [lr]
		ldmia	r0!, {r2, r3, ip, lr}
		strb	r2, [r2]
		strb	r3, [r3]
		strb	ip, [ip]
		strb	lr, [lr]
		teq	r0, r1
		bne	1b
		ldmfd	sp!, {pc}^

/*
 * Function: *_proc_init (void)
 * Purpose : Initialise the cache control registers
 */
_arm3_proc_init:
		mov	r0, #0x001f0000
		orr	r0, r0, #0x0000ff00
		orr	r0, r0, #0x000000ff
		mcr	p15, 0, r0, c3, c0		@ ARM3 Cacheable
		mcr     p15, 0, r0, c4, c0		@ ARM3 Updateable
		mov	r0, #0
		mcr     p15, 0, r0, c5, c0		@ ARM3 Disruptive
		mcr     p15, 0, r0, c1, c0		@ ARM3 Flush
		mov	r0, #3
		mcr     p15, 0, r0, c2, c0		@ ARM3 Control
_arm2_proc_init:
		movs	pc, lr

/*
 * Function: *_proc_fin (void)
 * Purpose : Finalise processor (disable caches)
 */
_arm3_proc_fin:	mov	r0, #2
		mcr	p15, 0, r0, c2, c0
_arm2_proc_fin:	orrs	pc, lr, #PSR_I_BIT|PSR_F_BIT

/*
 * Function: *_xchg_1 (int new, volatile void *ptr)
 * Params  : new	New value to store at...
 *	   : ptr	pointer to byte-wide location
 * Purpose : Performs an exchange operation
 * Returns : Original byte data at 'ptr'
 */
_arm2_xchg_1:	mov	r2, pc
		orr	r2, r2, #PSR_I_BIT
		teqp	r2, #0
		ldrb	r2, [r1]
		strb	r0, [r1]
		mov	r0, r2
		movs	pc, lr

_arm3_xchg_1:	swpb	r0, r0, [r1]
		movs	pc, lr

/*
 * Function: *_xchg_4 (int new, volatile void *ptr)
 * Params  : new	New value to store at...
 *	   : ptr	pointer to word-wide location
 * Purpose : Performs an exchange operation
 * Returns : Original word data at 'ptr'
 */
_arm2_xchg_4:	mov	r2, pc
		orr	r2, r2, #PSR_I_BIT
		teqp	r2, #0
		ldr	r2, [r1]
		str	r0, [r1]
		mov	r0, r2
		movs	pc, lr

_arm3_xchg_4:	swp	r0, r0, [r1]
		movs	pc, lr

_arm2_3_check_bugs:
		bics	pc, lr, #PSR_F_BIT		@ Clear FIQ disable bit

armvlsi_name:	.asciz	"ARM/VLSI"
_arm2_name:	.asciz	"ARM 2"
_arm250_name:	.asciz	"ARM 250"
_arm3_name:	.asciz	"ARM 3"

		.section ".init.text", #alloc, #execinstr
/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
		.globl	arm2_processor_functions
arm2_processor_functions:
		.word	_arm2_3_check_bugs
		.word	_arm2_proc_init
		.word	_arm2_proc_fin
		.word	_arm2_set_pgd
		.word	_arm2_xchg_1
		.word	_arm2_xchg_4

cpu_arm2_info:
		.long	armvlsi_name
		.long	_arm2_name

		.globl	arm250_processor_functions
arm250_processor_functions:
		.word	_arm2_3_check_bugs
		.word	_arm2_proc_init
		.word	_arm2_proc_fin
		.word	_arm2_set_pgd
		.word	_arm3_xchg_1
		.word	_arm3_xchg_4

cpu_arm250_info:
		.long	armvlsi_name
		.long	_arm250_name

		.globl	arm3_processor_functions
arm3_processor_functions:
		.word	_arm2_3_check_bugs
		.word	_arm3_proc_init
		.word	_arm3_proc_fin
		.word	_arm3_set_pgd
		.word	_arm3_xchg_1
		.word	_arm3_xchg_4

cpu_arm3_info:
		.long	armvlsi_name
		.long	_arm3_name

arm2_arch_name:	.asciz	"armv1"
arm3_arch_name:	.asciz	"armv2"
arm2_elf_name:	.asciz	"v1"
arm3_elf_name:	.asciz	"v2"
		.align

		.section ".proc.info", #alloc, #execinstr

		.long	0x41560200
		.long	0xfffffff0
		.long	arm2_arch_name
		.long	arm2_elf_name
		.long   0
		.long	cpu_arm2_info
		.long	arm2_processor_functions

		.long	0x41560250
		.long	0xfffffff0
		.long	arm3_arch_name
		.long	arm3_elf_name
		.long   0
		.long	cpu_arm250_info
		.long	arm250_processor_functions

		.long	0x41560300
		.long	0xfffffff0
		.long	arm3_arch_name
		.long	arm3_elf_name
		.long   0
		.long	cpu_arm3_info
		.long	arm3_processor_functions

