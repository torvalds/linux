// SPDX-License-Identifier: GPL-2.0

#include <linux/linkage.h>
#include <asm/assembler.h>

	.irp		b, 0, 1, 2, 3, 4, 5, 6, 7, 8
	.set		.Lv\b\().4s, \b
	.endr

	.macro		sm4e, rd, rn
	.inst		0xcec08400 | .L\rd | (.L\rn << 5)
	.endm

	/*
	 * void sm4_ce_do_crypt(const u32 *rk, u32 *out, const u32 *in);
	 */
	.text
ENTRY(sm4_ce_do_crypt)
	ld1		{v8.4s}, [x2]
	ld1		{v0.4s-v3.4s}, [x0], #64
CPU_LE(	rev32		v8.16b, v8.16b		)
	ld1		{v4.4s-v7.4s}, [x0]
	sm4e		v8.4s, v0.4s
	sm4e		v8.4s, v1.4s
	sm4e		v8.4s, v2.4s
	sm4e		v8.4s, v3.4s
	sm4e		v8.4s, v4.4s
	sm4e		v8.4s, v5.4s
	sm4e		v8.4s, v6.4s
	sm4e		v8.4s, v7.4s
	rev64		v8.4s, v8.4s
	ext		v8.16b, v8.16b, v8.16b, #8
CPU_LE(	rev32		v8.16b, v8.16b		)
	st1		{v8.4s}, [x1]
	ret
ENDPROC(sm4_ce_do_crypt)
