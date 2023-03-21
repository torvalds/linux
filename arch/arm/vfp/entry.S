/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/vfp/entry.S
 *
 *  Copyright (C) 2004 ARM Limited.
 *  Written by Deep Blue Solutions Limited.
 */
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/vfpmacros.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>

@ VFP entry point.
@
@  r0  = instruction opcode (32-bit ARM or two 16-bit Thumb)
@  r2  = PC value to resume execution after successful emulation
@  r9  = normal "successful" return address
@  r10 = this threads thread_info structure
@  lr  = unrecognised instruction return address
@  IRQs enabled.
@
ENTRY(do_vfp)
	mov	r1, r10
	mov	r3, r9
 	ldr	r4, .LCvfp
	ldr	pc, [r4]		@ call VFP entry point
ENDPROC(do_vfp)

ENTRY(vfp_null_entry)
	ret	lr
ENDPROC(vfp_null_entry)

	.align	2
.LCvfp:
	.word	vfp_vector
