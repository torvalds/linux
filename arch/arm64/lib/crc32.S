/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Accelerated CRC32(C) using AArch64 CRC instructions
 *
 * Copyright (C) 2016 - 2018 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <asm/alternative.h>
#include <asm/assembler.h>

	.cpu		generic+crc

	.macro		__crc32, c
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
CPU_BE(	rev		x3, x3		)
CPU_BE(	rev		x4, x4		)
CPU_BE(	rev		x5, x5		)
CPU_BE(	rev		x6, x6		)

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
CPU_BE(	rev		x3, x3		)
CPU_BE(	rev		x4, x4		)
CPU_BE(	rev		x5, x5		)
CPU_BE(	rev		x6, x6		)
	crc32\c\()x	w0, w0, x3
	crc32\c\()x	w0, w0, x4
	crc32\c\()x	w0, w0, x5
	crc32\c\()x	w0, w0, x6
	cbnz		x2, 32b
0:	ret

8:	tbz		x2, #3, 4f
	ldr		x3, [x1], #8
CPU_BE(	rev		x3, x3		)
	crc32\c\()x	w0, w0, x3
4:	tbz		x2, #2, 2f
	ldr		w3, [x1], #4
CPU_BE(	rev		w3, w3		)
	crc32\c\()w	w0, w0, w3
2:	tbz		x2, #1, 1f
	ldrh		w3, [x1], #2
CPU_BE(	rev16		w3, w3		)
	crc32\c\()h	w0, w0, w3
1:	tbz		x2, #0, 0f
	ldrb		w3, [x1]
	crc32\c\()b	w0, w0, w3
0:	ret
	.endm

	.align		5
ENTRY(crc32_le)
alternative_if_not ARM64_HAS_CRC32
	b		crc32_le_base
alternative_else_nop_endif
	__crc32
ENDPROC(crc32_le)

	.align		5
ENTRY(__crc32c_le)
alternative_if_not ARM64_HAS_CRC32
	b		__crc32c_le_base
alternative_else_nop_endif
	__crc32		c
ENDPROC(__crc32c_le)
