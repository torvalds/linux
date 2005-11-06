/*
 *  linux/arch/arm26/lib/csumpartialcopyuser.S
 *
 *  Copyright (C) 1995-1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/errno.h>
#include <asm/asm-offsets.h>

		.text

		.macro	save_regs
		stmfd	sp!, {r1 - r2, r4 - r9, fp, ip, lr, pc}
		mov	r9, sp, lsr #13
		mov	r9, r9, lsl #13
		ldr	r9, [r9, #TSK_ADDR_LIMIT]
		mov	r9, r9, lsr #24
		.endm

		.macro	load_regs,flags
		ldm\flags	fp, {r1, r2, r4-r9, fp, sp, pc}^
		.endm

		.macro	load1b,	reg1
		tst	r9, #0x01
9999:		ldreqbt	\reg1, [r0], #1
		ldrneb	\reg1, [r0], #1
		.section __ex_table, "a"
		.align	3
		.long	9999b, 6001f
		.previous
		.endm

		.macro	load2b, reg1, reg2
		tst	r9, #0x01
9999:		ldreqbt	\reg1, [r0], #1
		ldrneb	\reg1, [r0], #1
9998:		ldreqbt	\reg2, [r0], #1
		ldrneb	\reg2, [r0], #1
		.section __ex_table, "a"
		.long	9999b, 6001f
		.long	9998b, 6001f
		.previous
		.endm

		.macro	load1l, reg1
		tst	r9, #0x01
9999:		ldreqt	\reg1, [r0], #4
		ldrne	\reg1, [r0], #4
		.section __ex_table, "a"
		.align	3
		.long	9999b, 6001f
		.previous
		.endm

		.macro	load2l, reg1, reg2
		tst	r9, #0x01
		ldmneia	r0!, {\reg1, \reg2}
9999:		ldreqt	\reg1, [r0], #4
9998:		ldreqt	\reg2, [r0], #4
		.section __ex_table, "a"
		.long	9999b, 6001f
		.long	9998b, 6001f
		.previous
		.endm

		.macro	load4l, reg1, reg2, reg3, reg4
		tst	r9, #0x01
		ldmneia	r0!, {\reg1, \reg2, \reg3, \reg4}
9999:		ldreqt	\reg1, [r0], #4
9998:		ldreqt	\reg2, [r0], #4
9997:		ldreqt	\reg3, [r0], #4
9996:		ldreqt	\reg4, [r0], #4
		.section __ex_table, "a"
		.long	9999b, 6001f
		.long	9998b, 6001f
		.long	9997b, 6001f
		.long	9996b, 6001f
		.previous
		.endm

/*
 * unsigned int
 * csum_partial_copy_from_user(const char *src, char *dst, int len, int sum, int *err_ptr)
 *  r0 = src, r1 = dst, r2 = len, r3 = sum, [sp] = *err_ptr
 *  Returns : r0 = checksum, [[sp, #0], #0] = 0 or -EFAULT
 */

#define FN_ENTRY	ENTRY(csum_partial_copy_from_user)

#include "csumpartialcopygeneric.S"

/*
 * FIXME: minor buglet here
 * We don't return the checksum for the data present in the buffer.  To do
 * so properly, we would have to add in whatever registers were loaded before
 * the fault, which, with the current asm above is not predictable.
 */
		.align	4
6001:		mov	r4, #-EFAULT
		ldr	r5, [fp, #4]		@ *err_ptr
		str	r4, [r5]
		ldmia	sp, {r1, r2}		@ retrieve dst, len
		add	r2, r2, r1
		mov	r0, #0			@ zero the buffer
6002:		teq	r2, r1
		strneb	r0, [r1], #1
		bne	6002b
		load_regs	ea
