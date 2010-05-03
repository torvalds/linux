/*
 *  linux/arch/arm/lib/csumpartialcopyuser.S
 *
 *  Copyright (C) 1995-1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 27/03/03 Ian Molton Clean up CONFIG_CPU
 *
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/errno.h>
#include <asm/asm-offsets.h>

		.text

		.macro	save_regs
		stmfd	sp!, {r1, r2, r4 - r8, lr}
		.endm

		.macro	load_regs
		ldmfd	sp!, {r1, r2, r4 - r8, pc}
		.endm

		.macro	load1b,	reg1
		ldrusr	\reg1, r0, 1
		.endm

		.macro	load2b, reg1, reg2
		ldrusr	\reg1, r0, 1
		ldrusr	\reg2, r0, 1
		.endm

		.macro	load1l, reg1
		ldrusr	\reg1, r0, 4
		.endm

		.macro	load2l, reg1, reg2
		ldrusr	\reg1, r0, 4
		ldrusr	\reg2, r0, 4
		.endm

		.macro	load4l, reg1, reg2, reg3, reg4
		ldrusr	\reg1, r0, 4
		ldrusr	\reg2, r0, 4
		ldrusr	\reg3, r0, 4
		ldrusr	\reg4, r0, 4
		.endm

/*
 * unsigned int
 * csum_partial_copy_from_user(const char *src, char *dst, int len, int sum, int *err_ptr)
 *  r0 = src, r1 = dst, r2 = len, r3 = sum, [sp] = *err_ptr
 *  Returns : r0 = checksum, [[sp, #0], #0] = 0 or -EFAULT
 */

#define FN_ENTRY	ENTRY(csum_partial_copy_from_user)
#define FN_EXIT		ENDPROC(csum_partial_copy_from_user)

#include "csumpartialcopygeneric.S"

/*
 * FIXME: minor buglet here
 * We don't return the checksum for the data present in the buffer.  To do
 * so properly, we would have to add in whatever registers were loaded before
 * the fault, which, with the current asm above is not predictable.
 */
		.pushsection .fixup,"ax"
		.align	4
9001:		mov	r4, #-EFAULT
		ldr	r5, [fp, #4]		@ *err_ptr
		str	r4, [r5]
		ldmia	sp, {r1, r2}		@ retrieve dst, len
		add	r2, r2, r1
		mov	r0, #0			@ zero the buffer
9002:		teq	r2, r1
		strneb	r0, [r1], #1
		bne	9002b
		load_regs
		.popsection
