/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013-2021, Arm Limited.
 *
 * Adapted from the original at:
 * https://github.com/ARM-software/optimized-routines/blob/e823e3abf5f89ecb/string/aarch64/strncmp.S
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

/* Assumptions:
 *
 * ARMv8-a, AArch64
 */

#define L(label) .L ## label

#define REP8_01 0x0101010101010101
#define REP8_7f 0x7f7f7f7f7f7f7f7f
#define REP8_80 0x8080808080808080

/* Parameters and result.  */
#define src1		x0
#define src2		x1
#define limit		x2
#define result		x0

/* Internal variables.  */
#define data1		x3
#define data1w		w3
#define data2		x4
#define data2w		w4
#define has_nul		x5
#define diff		x6
#define syndrome	x7
#define tmp1		x8
#define tmp2		x9
#define tmp3		x10
#define zeroones	x11
#define pos		x12
#define limit_wd	x13
#define mask		x14
#define endloop		x15
#define count		mask

SYM_FUNC_START_WEAK_PI(strncmp)
	cbz	limit, L(ret0)
	eor	tmp1, src1, src2
	mov	zeroones, #REP8_01
	tst	tmp1, #7
	and	count, src1, #7
	b.ne	L(misaligned8)
	cbnz	count, L(mutual_align)
	/* Calculate the number of full and partial words -1.  */
	sub	limit_wd, limit, #1	/* limit != 0, so no underflow.  */
	lsr	limit_wd, limit_wd, #3	/* Convert to Dwords.  */

	/* NUL detection works on the principle that (X - 1) & (~X) & 0x80
	   (=> (X - 1) & ~(X | 0x7f)) is non-zero iff a byte is zero, and
	   can be done in parallel across the entire word.  */
	.p2align 4
L(loop_aligned):
	ldr	data1, [src1], #8
	ldr	data2, [src2], #8
L(start_realigned):
	subs	limit_wd, limit_wd, #1
	sub	tmp1, data1, zeroones
	orr	tmp2, data1, #REP8_7f
	eor	diff, data1, data2	/* Non-zero if differences found.  */
	csinv	endloop, diff, xzr, pl	/* Last Dword or differences.  */
	bics	has_nul, tmp1, tmp2	/* Non-zero if NUL terminator.  */
	ccmp	endloop, #0, #0, eq
	b.eq	L(loop_aligned)
	/* End of main loop */

	/* Not reached the limit, must have found the end or a diff.  */
	tbz	limit_wd, #63, L(not_limit)

	/* Limit % 8 == 0 => all bytes significant.  */
	ands	limit, limit, #7
	b.eq	L(not_limit)

	lsl	limit, limit, #3	/* Bits -> bytes.  */
	mov	mask, #~0
#ifdef __AARCH64EB__
	lsr	mask, mask, limit
#else
	lsl	mask, mask, limit
#endif
	bic	data1, data1, mask
	bic	data2, data2, mask

	/* Make sure that the NUL byte is marked in the syndrome.  */
	orr	has_nul, has_nul, mask

L(not_limit):
	orr	syndrome, diff, has_nul

#ifndef	__AARCH64EB__
	rev	syndrome, syndrome
	rev	data1, data1
	/* The MS-non-zero bit of the syndrome marks either the first bit
	   that is different, or the top bit of the first zero byte.
	   Shifting left now will bring the critical information into the
	   top bits.  */
	clz	pos, syndrome
	rev	data2, data2
	lsl	data1, data1, pos
	lsl	data2, data2, pos
	/* But we need to zero-extend (char is unsigned) the value and then
	   perform a signed 32-bit subtraction.  */
	lsr	data1, data1, #56
	sub	result, data1, data2, lsr #56
	ret
#else
	/* For big-endian we cannot use the trick with the syndrome value
	   as carry-propagation can corrupt the upper bits if the trailing
	   bytes in the string contain 0x01.  */
	/* However, if there is no NUL byte in the dword, we can generate
	   the result directly.  We can't just subtract the bytes as the
	   MSB might be significant.  */
	cbnz	has_nul, 1f
	cmp	data1, data2
	cset	result, ne
	cneg	result, result, lo
	ret
1:
	/* Re-compute the NUL-byte detection, using a byte-reversed value.  */
	rev	tmp3, data1
	sub	tmp1, tmp3, zeroones
	orr	tmp2, tmp3, #REP8_7f
	bic	has_nul, tmp1, tmp2
	rev	has_nul, has_nul
	orr	syndrome, diff, has_nul
	clz	pos, syndrome
	/* The MS-non-zero bit of the syndrome marks either the first bit
	   that is different, or the top bit of the first zero byte.
	   Shifting left now will bring the critical information into the
	   top bits.  */
	lsl	data1, data1, pos
	lsl	data2, data2, pos
	/* But we need to zero-extend (char is unsigned) the value and then
	   perform a signed 32-bit subtraction.  */
	lsr	data1, data1, #56
	sub	result, data1, data2, lsr #56
	ret
#endif

L(mutual_align):
	/* Sources are mutually aligned, but are not currently at an
	   alignment boundary.  Round down the addresses and then mask off
	   the bytes that precede the start point.
	   We also need to adjust the limit calculations, but without
	   overflowing if the limit is near ULONG_MAX.  */
	bic	src1, src1, #7
	bic	src2, src2, #7
	ldr	data1, [src1], #8
	neg	tmp3, count, lsl #3	/* 64 - bits(bytes beyond align). */
	ldr	data2, [src2], #8
	mov	tmp2, #~0
	sub	limit_wd, limit, #1	/* limit != 0, so no underflow.  */
#ifdef __AARCH64EB__
	/* Big-endian.  Early bytes are at MSB.  */
	lsl	tmp2, tmp2, tmp3	/* Shift (count & 63).  */
#else
	/* Little-endian.  Early bytes are at LSB.  */
	lsr	tmp2, tmp2, tmp3	/* Shift (count & 63).  */
#endif
	and	tmp3, limit_wd, #7
	lsr	limit_wd, limit_wd, #3
	/* Adjust the limit. Only low 3 bits used, so overflow irrelevant.  */
	add	limit, limit, count
	add	tmp3, tmp3, count
	orr	data1, data1, tmp2
	orr	data2, data2, tmp2
	add	limit_wd, limit_wd, tmp3, lsr #3
	b	L(start_realigned)

	.p2align 4
	/* Don't bother with dwords for up to 16 bytes.  */
L(misaligned8):
	cmp	limit, #16
	b.hs	L(try_misaligned_words)

L(byte_loop):
	/* Perhaps we can do better than this.  */
	ldrb	data1w, [src1], #1
	ldrb	data2w, [src2], #1
	subs	limit, limit, #1
	ccmp	data1w, #1, #0, hi	/* NZCV = 0b0000.  */
	ccmp	data1w, data2w, #0, cs	/* NZCV = 0b0000.  */
	b.eq	L(byte_loop)
L(done):
	sub	result, data1, data2
	ret
	/* Align the SRC1 to a dword by doing a bytewise compare and then do
	   the dword loop.  */
L(try_misaligned_words):
	lsr	limit_wd, limit, #3
	cbz	count, L(do_misaligned)

	neg	count, count
	and	count, count, #7
	sub	limit, limit, count
	lsr	limit_wd, limit, #3

L(page_end_loop):
	ldrb	data1w, [src1], #1
	ldrb	data2w, [src2], #1
	cmp	data1w, #1
	ccmp	data1w, data2w, #0, cs	/* NZCV = 0b0000.  */
	b.ne	L(done)
	subs	count, count, #1
	b.hi	L(page_end_loop)

L(do_misaligned):
	/* Prepare ourselves for the next page crossing.  Unlike the aligned
	   loop, we fetch 1 less dword because we risk crossing bounds on
	   SRC2.  */
	mov	count, #8
	subs	limit_wd, limit_wd, #1
	b.lo	L(done_loop)
L(loop_misaligned):
	and	tmp2, src2, #0xff8
	eor	tmp2, tmp2, #0xff8
	cbz	tmp2, L(page_end_loop)

	ldr	data1, [src1], #8
	ldr	data2, [src2], #8
	sub	tmp1, data1, zeroones
	orr	tmp2, data1, #REP8_7f
	eor	diff, data1, data2	/* Non-zero if differences found.  */
	bics	has_nul, tmp1, tmp2	/* Non-zero if NUL terminator.  */
	ccmp	diff, #0, #0, eq
	b.ne	L(not_limit)
	subs	limit_wd, limit_wd, #1
	b.pl	L(loop_misaligned)

L(done_loop):
	/* We found a difference or a NULL before the limit was reached.  */
	and	limit, limit, #7
	cbz	limit, L(not_limit)
	/* Read the last word.  */
	sub	src1, src1, 8
	sub	src2, src2, 8
	ldr	data1, [src1, limit]
	ldr	data2, [src2, limit]
	sub	tmp1, data1, zeroones
	orr	tmp2, data1, #REP8_7f
	eor	diff, data1, data2	/* Non-zero if differences found.  */
	bics	has_nul, tmp1, tmp2	/* Non-zero if NUL terminator.  */
	ccmp	diff, #0, #0, eq
	b.ne	L(not_limit)

L(ret0):
	mov	result, #0
	ret

SYM_FUNC_END_PI(strncmp)
EXPORT_SYMBOL_NOKASAN(strncmp)
