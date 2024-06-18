/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sm3-ce-core.S - SM3 secure hash using ARMv8.2 Crypto Extensions
 *
 * Copyright (C) 2018 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <linux/cfi_types.h>
#include <asm/assembler.h>

	.irp		b, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
	.set		.Lv\b\().4s, \b
	.endr

	.macro		sm3partw1, rd, rn, rm
	.inst		0xce60c000 | .L\rd | (.L\rn << 5) | (.L\rm << 16)
	.endm

	.macro		sm3partw2, rd, rn, rm
	.inst		0xce60c400 | .L\rd | (.L\rn << 5) | (.L\rm << 16)
	.endm

	.macro		sm3ss1, rd, rn, rm, ra
	.inst		0xce400000 | .L\rd | (.L\rn << 5) | (.L\ra << 10) | (.L\rm << 16)
	.endm

	.macro		sm3tt1a, rd, rn, rm, imm2
	.inst		0xce408000 | .L\rd | (.L\rn << 5) | ((\imm2) << 12) | (.L\rm << 16)
	.endm

	.macro		sm3tt1b, rd, rn, rm, imm2
	.inst		0xce408400 | .L\rd | (.L\rn << 5) | ((\imm2) << 12) | (.L\rm << 16)
	.endm

	.macro		sm3tt2a, rd, rn, rm, imm2
	.inst		0xce408800 | .L\rd | (.L\rn << 5) | ((\imm2) << 12) | (.L\rm << 16)
	.endm

	.macro		sm3tt2b, rd, rn, rm, imm2
	.inst		0xce408c00 | .L\rd | (.L\rn << 5) | ((\imm2) << 12) | (.L\rm << 16)
	.endm

	.macro		round, ab, s0, t0, t1, i
	sm3ss1		v5.4s, v8.4s, \t0\().4s, v9.4s
	shl		\t1\().4s, \t0\().4s, #1
	sri		\t1\().4s, \t0\().4s, #31
	sm3tt1\ab	v8.4s, v5.4s, v10.4s, \i
	sm3tt2\ab	v9.4s, v5.4s, \s0\().4s, \i
	.endm

	.macro		qround, ab, s0, s1, s2, s3, s4
	.ifnb		\s4
	ext		\s4\().16b, \s1\().16b, \s2\().16b, #12
	ext		v6.16b, \s0\().16b, \s1\().16b, #12
	ext		v7.16b, \s2\().16b, \s3\().16b, #8
	sm3partw1	\s4\().4s, \s0\().4s, \s3\().4s
	.endif

	eor		v10.16b, \s0\().16b, \s1\().16b

	round		\ab, \s0, v11, v12, 0
	round		\ab, \s0, v12, v11, 1
	round		\ab, \s0, v11, v12, 2
	round		\ab, \s0, v12, v11, 3

	.ifnb		\s4
	sm3partw2	\s4\().4s, v7.4s, v6.4s
	.endif
	.endm

	/*
	 * void sm3_ce_transform(struct sm3_state *sst, u8 const *src,
	 *                       int blocks)
	 */
	.text
SYM_TYPED_FUNC_START(sm3_ce_transform)
	/* load state */
	ld1		{v8.4s-v9.4s}, [x0]
	rev64		v8.4s, v8.4s
	rev64		v9.4s, v9.4s
	ext		v8.16b, v8.16b, v8.16b, #8
	ext		v9.16b, v9.16b, v9.16b, #8

	adr_l		x8, .Lt
	ldp		s13, s14, [x8]

	/* load input */
0:	ld1		{v0.16b-v3.16b}, [x1], #64
	sub		w2, w2, #1

	mov		v15.16b, v8.16b
	mov		v16.16b, v9.16b

CPU_LE(	rev32		v0.16b, v0.16b		)
CPU_LE(	rev32		v1.16b, v1.16b		)
CPU_LE(	rev32		v2.16b, v2.16b		)
CPU_LE(	rev32		v3.16b, v3.16b		)

	ext		v11.16b, v13.16b, v13.16b, #4

	qround		a, v0, v1, v2, v3, v4
	qround		a, v1, v2, v3, v4, v0
	qround		a, v2, v3, v4, v0, v1
	qround		a, v3, v4, v0, v1, v2

	ext		v11.16b, v14.16b, v14.16b, #4

	qround		b, v4, v0, v1, v2, v3
	qround		b, v0, v1, v2, v3, v4
	qround		b, v1, v2, v3, v4, v0
	qround		b, v2, v3, v4, v0, v1
	qround		b, v3, v4, v0, v1, v2
	qround		b, v4, v0, v1, v2, v3
	qround		b, v0, v1, v2, v3, v4
	qround		b, v1, v2, v3, v4, v0
	qround		b, v2, v3, v4, v0, v1
	qround		b, v3, v4
	qround		b, v4, v0
	qround		b, v0, v1

	eor		v8.16b, v8.16b, v15.16b
	eor		v9.16b, v9.16b, v16.16b

	/* handled all input blocks? */
	cbnz		w2, 0b

	/* save state */
	rev64		v8.4s, v8.4s
	rev64		v9.4s, v9.4s
	ext		v8.16b, v8.16b, v8.16b, #8
	ext		v9.16b, v9.16b, v9.16b, #8
	st1		{v8.4s-v9.4s}, [x0]
	ret
SYM_FUNC_END(sm3_ce_transform)

	.section	".rodata", "a"
	.align		3
.Lt:	.word		0x79cc4519, 0x9d8a7a87
