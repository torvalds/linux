/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013-2022, Arm Limited.
 *
 * Adapted from the original at:
 * https://github.com/ARM-software/optimized-routines/blob/189dfefe37d54c5b/string/aarch64/strncmp.S
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

/* Assumptions:
 *
 * ARMv8-a, AArch64.
 * MTE compatible.
 */

#define L(label) .L ## label

#define REP8_01 0x0101010101010101
#define REP8_7f 0x7f7f7f7f7f7f7f7f

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
#define mask		x13
#define endloop		x14
#define count		mask
#define offset		pos
#define neg_offset	x15

/* Define endian dependent shift operations.
   On big-endian early bytes are at MSB and on little-endian LSB.
   LS_FW means shifting towards early bytes.
   LS_BK means shifting towards later bytes.
   */
#ifdef __AARCH64EB__
#define LS_FW lsl
#define LS_BK lsr
#else
#define LS_FW lsr
#define LS_BK lsl
#endif

SYM_FUNC_START_WEAK_PI(strncmp)
	cbz	limit, L(ret0)
	eor	tmp1, src1, src2
	mov	zeroones, #REP8_01
	tst	tmp1, #7
	and	count, src1, #7
	b.ne	L(misaligned8)
	cbnz	count, L(mutual_align)

	/* NUL detection works on the principle that (X - 1) & (~X) & 0x80
	   (=> (X - 1) & ~(X | 0x7f)) is non-zero iff a byte is zero, and
	   can be done in parallel across the entire word.  */
	.p2align 4
L(loop_aligned):
	ldr	data1, [src1], #8
	ldr	data2, [src2], #8
L(start_realigned):
	subs	limit, limit, #8
	sub	tmp1, data1, zeroones
	orr	tmp2, data1, #REP8_7f
	eor	diff, data1, data2	/* Non-zero if differences found.  */
	csinv	endloop, diff, xzr, hi	/* Last Dword or differences.  */
	bics	has_nul, tmp1, tmp2	/* Non-zero if NUL terminator.  */
	ccmp	endloop, #0, #0, eq
	b.eq	L(loop_aligned)
	/* End of main loop */

L(full_check):
#ifndef __AARCH64EB__
	orr	syndrome, diff, has_nul
	add	limit, limit, 8	/* Rewind limit to before last subs. */
L(syndrome_check):
	/* Limit was reached. Check if the NUL byte or the difference
	   is before the limit. */
	rev	syndrome, syndrome
	rev	data1, data1
	clz	pos, syndrome
	rev	data2, data2
	lsl	data1, data1, pos
	cmp	limit, pos, lsr #3
	lsl	data2, data2, pos
	/* But we need to zero-extend (char is unsigned) the value and then
	   perform a signed 32-bit subtraction.  */
	lsr	data1, data1, #56
	sub	result, data1, data2, lsr #56
	csel result, result, xzr, hi
	ret
#else
	/* Not reached the limit, must have found the end or a diff.  */
	tbz	limit, #63, L(not_limit)
	add	tmp1, limit, 8
	cbz	limit, L(not_limit)

	lsl	limit, tmp1, #3	/* Bits -> bytes.  */
	mov	mask, #~0
	lsr	mask, mask, limit
	bic	data1, data1, mask
	bic	data2, data2, mask

	/* Make sure that the NUL byte is marked in the syndrome.  */
	orr	has_nul, has_nul, mask

L(not_limit):
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
	/* The most-significant-non-zero bit of the syndrome marks either the
	   first bit that is different, or the top bit of the first zero byte.
	   Shifting left now will bring the critical information into the
	   top bits.  */
L(end_quick):
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
	LS_FW	tmp2, tmp2, tmp3	/* Shift (count & 63).  */
	/* Adjust the limit and ensure it doesn't overflow.  */
	adds	limit, limit, count
	csinv	limit, limit, xzr, lo
	orr	data1, data1, tmp2
	orr	data2, data2, tmp2
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
	cbz	count, L(src1_aligned)

	neg	count, count
	and	count, count, #7
	sub	limit, limit, count

L(page_end_loop):
	ldrb	data1w, [src1], #1
	ldrb	data2w, [src2], #1
	cmp	data1w, #1
	ccmp	data1w, data2w, #0, cs	/* NZCV = 0b0000.  */
	b.ne	L(done)
	subs	count, count, #1
	b.hi	L(page_end_loop)

	/* The following diagram explains the comparison of misaligned strings.
	   The bytes are shown in natural order. For little-endian, it is
	   reversed in the registers. The "x" bytes are before the string.
	   The "|" separates data that is loaded at one time.
	   src1     | a a a a a a a a | b b b c c c c c | . . .
	   src2     | x x x x x a a a   a a a a a b b b | c c c c c . . .

	   After shifting in each step, the data looks like this:
	                STEP_A              STEP_B              STEP_C
	   data1    a a a a a a a a     b b b c c c c c     b b b c c c c c
	   data2    a a a a a a a a     b b b 0 0 0 0 0     0 0 0 c c c c c

	   The bytes with "0" are eliminated from the syndrome via mask.

	   Align SRC2 down to 16 bytes. This way we can read 16 bytes at a
	   time from SRC2. The comparison happens in 3 steps. After each step
	   the loop can exit, or read from SRC1 or SRC2. */
L(src1_aligned):
	/* Calculate offset from 8 byte alignment to string start in bits. No
	   need to mask offset since shifts are ignoring upper bits. */
	lsl	offset, src2, #3
	bic	src2, src2, #0xf
	mov	mask, -1
	neg	neg_offset, offset
	ldr	data1, [src1], #8
	ldp	tmp1, tmp2, [src2], #16
	LS_BK	mask, mask, neg_offset
	and	neg_offset, neg_offset, #63	/* Need actual value for cmp later. */
	/* Skip the first compare if data in tmp1 is irrelevant. */
	tbnz	offset, 6, L(misaligned_mid_loop)

L(loop_misaligned):
	/* STEP_A: Compare full 8 bytes when there is enough data from SRC2.*/
	LS_FW	data2, tmp1, offset
	LS_BK	tmp1, tmp2, neg_offset
	subs	limit, limit, #8
	orr	data2, data2, tmp1	/* 8 bytes from SRC2 combined from two regs.*/
	sub	has_nul, data1, zeroones
	eor	diff, data1, data2	/* Non-zero if differences found.  */
	orr	tmp3, data1, #REP8_7f
	csinv	endloop, diff, xzr, hi	/* If limit, set to all ones. */
	bic	has_nul, has_nul, tmp3	/* Non-zero if NUL byte found in SRC1. */
	orr	tmp3, endloop, has_nul
	cbnz	tmp3, L(full_check)

	ldr	data1, [src1], #8
L(misaligned_mid_loop):
	/* STEP_B: Compare first part of data1 to second part of tmp2. */
	LS_FW	data2, tmp2, offset
#ifdef __AARCH64EB__
	/* For big-endian we do a byte reverse to avoid carry-propagation
	problem described above. This way we can reuse the has_nul in the
	next step and also use syndrome value trick at the end. */
	rev	tmp3, data1
	#define data1_fixed tmp3
#else
	#define data1_fixed data1
#endif
	sub	has_nul, data1_fixed, zeroones
	orr	tmp3, data1_fixed, #REP8_7f
	eor	diff, data2, data1	/* Non-zero if differences found.  */
	bic	has_nul, has_nul, tmp3	/* Non-zero if NUL terminator.  */
#ifdef __AARCH64EB__
	rev	has_nul, has_nul
#endif
	cmp	limit, neg_offset, lsr #3
	orr	syndrome, diff, has_nul
	bic	syndrome, syndrome, mask	/* Ignore later bytes. */
	csinv	tmp3, syndrome, xzr, hi	/* If limit, set to all ones. */
	cbnz	tmp3, L(syndrome_check)

	/* STEP_C: Compare second part of data1 to first part of tmp1. */
	ldp	tmp1, tmp2, [src2], #16
	cmp	limit, #8
	LS_BK	data2, tmp1, neg_offset
	eor	diff, data2, data1	/* Non-zero if differences found.  */
	orr	syndrome, diff, has_nul
	and	syndrome, syndrome, mask	/* Ignore earlier bytes. */
	csinv	tmp3, syndrome, xzr, hi	/* If limit, set to all ones. */
	cbnz	tmp3, L(syndrome_check)

	ldr	data1, [src1], #8
	sub	limit, limit, #8
	b	L(loop_misaligned)

#ifdef	__AARCH64EB__
L(syndrome_check):
	clz	pos, syndrome
	cmp	pos, limit, lsl #3
	b.lo	L(end_quick)
#endif

L(ret0):
	mov	result, #0
	ret
SYM_FUNC_END_PI(strncmp)
EXPORT_SYMBOL_NOKASAN(strncmp)
