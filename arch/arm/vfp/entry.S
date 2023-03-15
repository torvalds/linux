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
	mov	r1, r0				@ pass trigger opcode via R1
	mov	r0, sp				@ pass struct pt_regs via R0
	bl	vfp_support_entry		@ dispatch the VFP exception
	cmp	r0, #0				@ handled successfully?
	reteq	r9				@ then use R9 as return address
	ret	lr				@ pass to undef handler
ENDPROC(do_vfp)
