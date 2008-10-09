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
 *  r5  = faulted PC+4
 *  r9  = successful return
 *  r10 = thread_info structure
 *  lr  = failure return
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>
#include <asm/vfpmacros.h>

ENTRY(do_vfp)
	enable_irq
 	ldr	r4, .LCvfp
	ldr	r11, [r10, #TI_CPU]	@ CPU number
	add	r10, r10, #TI_VFPSTATE	@ r10 = workspace
	ldr	pc, [r4]		@ call VFP entry point
ENDPROC(do_vfp)

ENTRY(vfp_null_entry)
	mov	pc, lr
ENDPROC(vfp_null_entry)

.LCvfp:
	.word	vfp_vector

@ This code is called if the VFP does not exist. It needs to flag the
@ failure to the VFP initialisation code.

	__INIT
ENTRY(vfp_testing_entry)
	ldr	r0, VFP_arch_address
	str	r5, [r0]		@ known non-zero value
	mov	pc, r9			@ we have handled the fault
ENDPROC(vfp_testing_entry)

VFP_arch_address:
	.word	VFP_arch

	__FINIT
