/*
 *  linux/arch/arm/vfp/entry.S
 *
 *  Copyright (C) 2004 ARM Limited.
 *  Written by Deep Blue Solutions Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Basic entry code, called from the kernel's undefined instruction trap.
 *  r0  = faulted instruction
 *  r2  = faulted PC+4
 *  r9  = successful return
 *  r10 = thread_info structure
 *  lr  = failure return
 */
#include <asm/thread_info.h>
#include <asm/vfpmacros.h>
#include "../kernel/entry-header.S"

ENTRY(do_vfp)
#ifdef CONFIG_PREEMPT_COUNT
	ldr	r4, [r10, #TI_PREEMPT]	@ get preempt count
	add	r11, r4, #1		@ increment it
	str	r11, [r10, #TI_PREEMPT]
#endif
	enable_irq
 	ldr	r4, .LCvfp
	ldr	r11, [r10, #TI_CPU]	@ CPU number
	add	r10, r10, #TI_VFPSTATE	@ r10 = workspace
	ldr	pc, [r4]		@ call VFP entry point
ENDPROC(do_vfp)

ENTRY(vfp_null_entry)
#ifdef CONFIG_PREEMPT_COUNT
	get_thread_info	r10
	ldr	r4, [r10, #TI_PREEMPT]	@ get preempt count
	sub	r11, r4, #1		@ decrement it
	str	r11, [r10, #TI_PREEMPT]
#endif
	mov	pc, lr
ENDPROC(vfp_null_entry)

	.align	2
.LCvfp:
	.word	vfp_vector

@ This code is called if the VFP does not exist. It needs to flag the
@ failure to the VFP initialisation code.

	__INIT
ENTRY(vfp_testing_entry)
#ifdef CONFIG_PREEMPT_COUNT
	get_thread_info	r10
	ldr	r4, [r10, #TI_PREEMPT]	@ get preempt count
	sub	r11, r4, #1		@ decrement it
	str	r11, [r10, #TI_PREEMPT]
#endif
	ldr	r0, VFP_arch_address
	str	r5, [r0]		@ known non-zero value
	mov	pc, r9			@ we have handled the fault
ENDPROC(vfp_testing_entry)

	.align	2
VFP_arch_address:
	.word	VFP_arch

	__FINIT
