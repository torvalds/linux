/*
 *  linux/arch/arm26/kernel/head.S
 *
 *  Copyright (C) 1994-2000 Russell King
 *  Copyright (C) 2003 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  26-bit kernel startup code
 */
#include <linux/linkage.h>
#include <asm/mach-types.h>

		.globl	swapper_pg_dir
		.equ	swapper_pg_dir,	0x0207d000

/*
 * Entry point.
 */
		.section ".init.text",#alloc,#execinstr
ENTRY(stext)

__entry:
		cmp	pc, #0x02000000
		ldrlt	pc, LC0			@ if 0x01800000, call at 0x02080000
		teq	r0, #0                  @ Check for old calling method
		blne	oldparams		@ Move page if old

		adr	r0, LC0
		ldmib	r0, {r2-r5, sp}		@ Setup stack (and fetch other values)

		mov	r0, #0			@ Clear BSS
1:		cmp	r2, r3
		strcc	r0, [r2], #4
		bcc	1b

		bl	detect_proc_type
		str	r0, [r4]
		bl	detect_arch_type
		str	r0, [r5]

#ifdef CONFIG_XIP_KERNEL
               ldr     r3, ETEXT                       @ data section copy
               ldr     r4, SDATA
               ldr     r5, EDATA
1:
               ldr     r6, [r3], #4
               str     r6, [r4], #4
               cmp     r4, r5
               blt     1b
#endif
		mov	fp, #0
		b	start_kernel

LC0:		.word	_stext
		.word	__bss_start		@ r2
		.word	_end			@ r3
		.word	processor_id		@ r4
		.word	__machine_arch_type	@ r5
		.word	init_thread_union+8192	@ sp
#ifdef CONFIG_XIP_KERNEL
ETEXT:		.word   _endtext
SDATA:		.word   _sdata
EDATA:		.word   __bss_start
#endif

arm2_id:	.long	0x41560200  @ ARM2 and 250 dont have a CPUID
arm250_id:	.long	0x41560250  @ So we create some after probing for them
		.align

oldparams:	mov     r4, #0x02000000
		add	r3, r4, #0x00080000
		add	r4, r4, #0x0007c000
1:		ldmia	r0!, {r5 - r12}
		stmia	r4!, {r5 - r12}
		cmp	r4, r3
		blt	1b
		mov	pc, lr

/*
 * We need some way to automatically detect the difference between
 * these two machines.  Unfortunately, it is not possible to detect
 * the presence of the SuperIO chip, because that will hang the old
 * Archimedes machines solid.
 */
/* DAG: Outdated, these have been combined !!!!!!! */
detect_arch_type:
#if defined(CONFIG_ARCH_ARC)
		mov	r0, #MACH_TYPE_ARCHIMEDES
#elif defined(CONFIG_ARCH_A5K)
		mov	r0, #MACH_TYPE_A5K
#endif
		mov	pc, lr

detect_proc_type:
		mov	ip, lr
		mov	r2, #0xea000000		@ Point undef instr to continuation
		adr	r0, continue - 12
		orr	r0, r2, r0, lsr #2
		mov	r1, #0
		str	r0, [r1, #4]
		ldr	r0, arm2_id
		swp	r2, r2, [r1]		@ check for swp (ARM2 cant)
		ldr	r0, arm250_id
		mrc	15, 0, r3, c0, c0	@ check for CP#15 (ARM250 cant)
		mov	r0, r3
continue:	mov	r2, #0xeb000000		@ Make undef vector loop
		sub	r2, r2, #2
		str	r2, [r1, #4]
		mov	pc, ip
