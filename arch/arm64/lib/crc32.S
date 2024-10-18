/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Accelerated CRC32(C) using AArch64 CRC instructions
 *
 * Copyright (C) 2016 - 2018 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.arch		armv8-a+crc

	.macro		bitle, reg
	.endm

	.macro		bitbe, reg
	rbit		\reg, \reg
	.endm

	.macro		bytele, reg
	.endm

	.macro		bytebe, reg
	rbit		\reg, \reg
	lsr		\reg, \reg, #24
	.endm

	.macro		hwordle, reg
CPU_BE(	rev16		\reg, \reg	)
	.endm

	.macro		hwordbe, reg
CPU_LE(	rev		\reg, \reg	)
	rbit		\reg, \reg
CPU_BE(	lsr		\reg, \reg, #16	)
	.endm

	.macro		le, regs:vararg
	.irp		r, \regs
CPU_BE(	rev		\r, \r		)
	.endr
	.endm

	.macro		be, regs:vararg
	.irp		r, \regs
CPU_LE(	rev		\r, \r		)
	.endr
	.irp		r, \regs
	rbit		\r, \r
	.endr
	.endm

	.macro		__crc32, c, order=le
	bit\order	w0
	cmp		x2, #16
	b.lt		8f			// less than 16 bytes

	and		x7, x2, #0x1f
	and		x2, x2, #~0x1f
	cbz		x7, 32f			// multiple of 32 bytes

	and		x8, x7, #0xf
	ldp		x3, x4, [x1]
	add		x8, x8, x1
	add		x1, x1, x7
	ldp		x5, x6, [x8]
	\order		x3, x4, x5, x6

	tst		x7, #8
	crc32\c\()x	w8, w0, x3
	csel		x3, x3, x4, eq
	csel		w0, w0, w8, eq
	tst		x7, #4
	lsr		x4, x3, #32
	crc32\c\()w	w8, w0, w3
	csel		x3, x3, x4, eq
	csel		w0, w0, w8, eq
	tst		x7, #2
	lsr		w4, w3, #16
	crc32\c\()h	w8, w0, w3
	csel		w3, w3, w4, eq
	csel		w0, w0, w8, eq
	tst		x7, #1
	crc32\c\()b	w8, w0, w3
	csel		w0, w0, w8, eq
	tst		x7, #16
	crc32\c\()x	w8, w0, x5
	crc32\c\()x	w8, w8, x6
	csel		w0, w0, w8, eq
	cbz		x2, 0f

32:	ldp		x3, x4, [x1], #32
	sub		x2, x2, #32
	ldp		x5, x6, [x1, #-16]
	\order		x3, x4, x5, x6
	crc32\c\()x	w0, w0, x3
	crc32\c\()x	w0, w0, x4
	crc32\c\()x	w0, w0, x5
	crc32\c\()x	w0, w0, x6
	cbnz		x2, 32b
0:	bit\order	w0
	ret

8:	tbz		x2, #3, 4f
	ldr		x3, [x1], #8
	\order		x3
	crc32\c\()x	w0, w0, x3
4:	tbz		x2, #2, 2f
	ldr		w3, [x1], #4
	\order		w3
	crc32\c\()w	w0, w0, w3
2:	tbz		x2, #1, 1f
	ldrh		w3, [x1], #2
	hword\order	w3
	crc32\c\()h	w0, w0, w3
1:	tbz		x2, #0, 0f
	ldrb		w3, [x1]
	byte\order	w3
	crc32\c\()b	w0, w0, w3
0:	bit\order	w0
	ret
	.endm

	.align		5
SYM_FUNC_START(crc32_le_arm64)
	__crc32
SYM_FUNC_END(crc32_le_arm64)

	.align		5
SYM_FUNC_START(crc32c_le_arm64)
	__crc32		c
SYM_FUNC_END(crc32c_le_arm64)

	.align		5
SYM_FUNC_START(crc32_be_arm64)
	__crc32		order=be
SYM_FUNC_END(crc32_be_arm64)
