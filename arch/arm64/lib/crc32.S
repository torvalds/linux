/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Accelerated CRC32(C) using AArch64 CRC instructions
 *
 * Copyright (C) 2016 - 2018 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <asm/alternative.h>
#include <asm/assembler.h>

	.arch		armv8-a+crc

	.macro		byteorder, reg, be
	.if		\be
CPU_LE( rev		\reg, \reg	)
	.else
CPU_BE( rev		\reg, \reg	)
	.endif
	.endm

	.macro		byteorder16, reg, be
	.if		\be
CPU_LE( rev16		\reg, \reg	)
	.else
CPU_BE( rev16		\reg, \reg	)
	.endif
	.endm

	.macro		bitorder, reg, be
	.if		\be
	rbit		\reg, \reg
	.endif
	.endm

	.macro		bitorder16, reg, be
	.if		\be
	rbit		\reg, \reg
	lsr		\reg, \reg, #16
	.endif
	.endm

	.macro		bitorder8, reg, be
	.if		\be
	rbit		\reg, \reg
	lsr		\reg, \reg, #24
	.endif
	.endm

	.macro		__crc32, c, be=0
	bitorder	w0, \be
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
	byteorder	x3, \be
	byteorder	x4, \be
	byteorder	x5, \be
	byteorder	x6, \be
	bitorder	x3, \be
	bitorder	x4, \be
	bitorder	x5, \be
	bitorder	x6, \be

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
	byteorder	x3, \be
	byteorder	x4, \be
	byteorder	x5, \be
	byteorder	x6, \be
	bitorder	x3, \be
	bitorder	x4, \be
	bitorder	x5, \be
	bitorder	x6, \be
	crc32\c\()x	w0, w0, x3
	crc32\c\()x	w0, w0, x4
	crc32\c\()x	w0, w0, x5
	crc32\c\()x	w0, w0, x6
	cbnz		x2, 32b
0:	bitorder	w0, \be
	ret

8:	tbz		x2, #3, 4f
	ldr		x3, [x1], #8
	byteorder	x3, \be
	bitorder	x3, \be
	crc32\c\()x	w0, w0, x3
4:	tbz		x2, #2, 2f
	ldr		w3, [x1], #4
	byteorder	w3, \be
	bitorder	w3, \be
	crc32\c\()w	w0, w0, w3
2:	tbz		x2, #1, 1f
	ldrh		w3, [x1], #2
	byteorder16	w3, \be
	bitorder16	w3, \be
	crc32\c\()h	w0, w0, w3
1:	tbz		x2, #0, 0f
	ldrb		w3, [x1]
	bitorder8	w3, \be
	crc32\c\()b	w0, w0, w3
0:	bitorder	w0, \be
	ret
	.endm

	.align		5
SYM_FUNC_START(crc32_le)
alternative_if_not ARM64_HAS_CRC32
	b		crc32_le_base
alternative_else_nop_endif
	__crc32
SYM_FUNC_END(crc32_le)

	.align		5
SYM_FUNC_START(__crc32c_le)
alternative_if_not ARM64_HAS_CRC32
	b		__crc32c_le_base
alternative_else_nop_endif
	__crc32		c
SYM_FUNC_END(__crc32c_le)

	.align		5
SYM_FUNC_START(crc32_be)
alternative_if_not ARM64_HAS_CRC32
	b		crc32_be_base
alternative_else_nop_endif
	__crc32		be=1
SYM_FUNC_END(crc32_be)
