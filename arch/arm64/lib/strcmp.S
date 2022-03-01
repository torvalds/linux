/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2022, Arm Limited.
 *
 * Adapted from the original at:
 * https://github.com/ARM-software/optimized-routines/blob/189dfefe37d54c5b/string/aarch64/strcmp.S
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

#define src1		x0
#define src2		x1
#define result		x0

#define data1		x2
#define data1w		w2
#define data2		x3
#define data2w		w3
#define has_nul		x4
#define diff		x5
#define off1		x5
#define syndrome	x6
#define tmp		x6
#define data3		x7
#define zeroones	x8
#define shift		x9
#define off2		x10

/* On big-endian early bytes are at MSB and on little-endian LSB.
   LS_FW means shifting towards early bytes.  */
#ifdef __AARCH64EB__
# define LS_FW lsl
#else
# define LS_FW lsr
#endif

/* NUL detection works on the principle that (X - 1) & (~X) & 0x80
   (=> (X - 1) & ~(X | 0x7f)) is non-zero iff a byte is zero, and
   can be done in parallel across the entire word.
   Since carry propagation makes 0x1 bytes before a NUL byte appear
   NUL too in big-endian, byte-reverse the data before the NUL check.  */


SYM_FUNC_START_WEAK_PI(strcmp)
	sub	off2, src2, src1
	mov	zeroones, REP8_01
	and	tmp, src1, 7
	tst	off2, 7
	b.ne	L(misaligned8)
	cbnz	tmp, L(mutual_align)

	.p2align 4

L(loop_aligned):
	ldr	data2, [src1, off2]
	ldr	data1, [src1], 8
L(start_realigned):
#ifdef __AARCH64EB__
	rev	tmp, data1
	sub	has_nul, tmp, zeroones
	orr	tmp, tmp, REP8_7f
#else
	sub	has_nul, data1, zeroones
	orr	tmp, data1, REP8_7f
#endif
	bics	has_nul, has_nul, tmp	/* Non-zero if NUL terminator.  */
	ccmp	data1, data2, 0, eq
	b.eq	L(loop_aligned)
#ifdef __AARCH64EB__
	rev	has_nul, has_nul
#endif
	eor	diff, data1, data2
	orr	syndrome, diff, has_nul
L(end):
#ifndef __AARCH64EB__
	rev	syndrome, syndrome
	rev	data1, data1
	rev	data2, data2
#endif
	clz	shift, syndrome
	/* The most-significant-non-zero bit of the syndrome marks either the
	   first bit that is different, or the top bit of the first zero byte.
	   Shifting left now will bring the critical information into the
	   top bits.  */
	lsl	data1, data1, shift
	lsl	data2, data2, shift
	/* But we need to zero-extend (char is unsigned) the value and then
	   perform a signed 32-bit subtraction.  */
	lsr	data1, data1, 56
	sub	result, data1, data2, lsr 56
	ret

	.p2align 4

L(mutual_align):
	/* Sources are mutually aligned, but are not currently at an
	   alignment boundary.  Round down the addresses and then mask off
	   the bytes that precede the start point.  */
	bic	src1, src1, 7
	ldr	data2, [src1, off2]
	ldr	data1, [src1], 8
	neg	shift, src2, lsl 3	/* Bits to alignment -64.  */
	mov	tmp, -1
	LS_FW	tmp, tmp, shift
	orr	data1, data1, tmp
	orr	data2, data2, tmp
	b	L(start_realigned)

L(misaligned8):
	/* Align SRC1 to 8 bytes and then compare 8 bytes at a time, always
	   checking to make sure that we don't access beyond the end of SRC2.  */
	cbz	tmp, L(src1_aligned)
L(do_misaligned):
	ldrb	data1w, [src1], 1
	ldrb	data2w, [src2], 1
	cmp	data1w, 0
	ccmp	data1w, data2w, 0, ne	/* NZCV = 0b0000.  */
	b.ne	L(done)
	tst	src1, 7
	b.ne	L(do_misaligned)

L(src1_aligned):
	neg	shift, src2, lsl 3
	bic	src2, src2, 7
	ldr	data3, [src2], 8
#ifdef __AARCH64EB__
	rev	data3, data3
#endif
	lsr	tmp, zeroones, shift
	orr	data3, data3, tmp
	sub	has_nul, data3, zeroones
	orr	tmp, data3, REP8_7f
	bics	has_nul, has_nul, tmp
	b.ne	L(tail)

	sub	off1, src2, src1

	.p2align 4

L(loop_unaligned):
	ldr	data3, [src1, off1]
	ldr	data2, [src1, off2]
#ifdef __AARCH64EB__
	rev	data3, data3
#endif
	sub	has_nul, data3, zeroones
	orr	tmp, data3, REP8_7f
	ldr	data1, [src1], 8
	bics	has_nul, has_nul, tmp
	ccmp	data1, data2, 0, eq
	b.eq	L(loop_unaligned)

	lsl	tmp, has_nul, shift
#ifdef __AARCH64EB__
	rev	tmp, tmp
#endif
	eor	diff, data1, data2
	orr	syndrome, diff, tmp
	cbnz	syndrome, L(end)
L(tail):
	ldr	data1, [src1]
	neg	shift, shift
	lsr	data2, data3, shift
	lsr	has_nul, has_nul, shift
#ifdef __AARCH64EB__
	rev     data2, data2
	rev	has_nul, has_nul
#endif
	eor	diff, data1, data2
	orr	syndrome, diff, has_nul
	b	L(end)

L(done):
	sub	result, data1, data2
	ret

SYM_FUNC_END_PI(strcmp)
EXPORT_SYMBOL_NOKASAN(strcmp)
