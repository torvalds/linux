/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Arm Ltd.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * Find a character in an area of memory.
 *
 * Parameters:
 *	x0 - buf
 *	x1 - c
 *	x2 - n
 * Returns:
 *	x0 - address of first occurrence of 'c' or 0
 */

#define L(label) .L ## label

#define REP8_01 0x0101010101010101
#define REP8_7f 0x7f7f7f7f7f7f7f7f

#define srcin		x0
#define chrin		w1
#define cntin		x2

#define result		x0

#define wordcnt		x3
#define rep01		x4
#define repchr		x5
#define cur_word	x6
#define cur_byte	w6
#define tmp		x7
#define tmp2		x8

	.p2align 4
	nop
SYM_FUNC_START_WEAK_PI(memchr)
	and	chrin, chrin, #0xff
	lsr	wordcnt, cntin, #3
	cbz	wordcnt, L(byte_loop)
	mov	rep01, #REP8_01
	mul	repchr, x1, rep01
	and	cntin, cntin, #7
L(word_loop):
	ldr	cur_word, [srcin], #8
	sub	wordcnt, wordcnt, #1
	eor	cur_word, cur_word, repchr
	sub	tmp, cur_word, rep01
	orr	tmp2, cur_word, #REP8_7f
	bics	tmp, tmp, tmp2
	b.ne	L(found_word)
	cbnz	wordcnt, L(word_loop)
L(byte_loop):
	cbz	cntin, L(not_found)
	ldrb	cur_byte, [srcin], #1
	sub	cntin, cntin, #1
	cmp	cur_byte, chrin
	b.ne	L(byte_loop)
	sub	srcin, srcin, #1
	ret
L(found_word):
CPU_LE(	rev	tmp, tmp)
	clz	tmp, tmp
	sub	tmp, tmp, #64
	add	result, srcin, tmp, asr #3
	ret
L(not_found):
	mov	result, #0
	ret
SYM_FUNC_END_PI(memchr)
EXPORT_SYMBOL_NOKASAN(memchr)
