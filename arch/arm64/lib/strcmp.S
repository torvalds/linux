/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 ARM Ltd.
 * Copyright (C) 2013 Linaro.
 *
 * This code is based on glibc cortex strings work originally authored by Linaro
 * be found @
 *
 * http://bazaar.launchpad.net/~linaro-toolchain-dev/cortex-strings/trunk/
 * files/head:/src/aarch64/
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * compare two strings
 *
 * Parameters:
 *	x0 - const string 1 pointer
 *    x1 - const string 2 pointer
 * Returns:
 * x0 - an integer less than, equal to, or greater than zero
 * if  s1  is  found, respectively, to be less than, to match,
 * or be greater than s2.
 */

#define REP8_01 0x0101010101010101
#define REP8_7f 0x7f7f7f7f7f7f7f7f
#define REP8_80 0x8080808080808080

/* Parameters and result.  */
src1		.req	x0
src2		.req	x1
result		.req	x0

/* Internal variables.  */
data1		.req	x2
data1w		.req	w2
data2		.req	x3
data2w		.req	w3
has_nul		.req	x4
diff		.req	x5
syndrome	.req	x6
tmp1		.req	x7
tmp2		.req	x8
tmp3		.req	x9
zeroones	.req	x10
pos		.req	x11

SYM_FUNC_START_WEAK_PI(strcmp)
	eor	tmp1, src1, src2
	mov	zeroones, #REP8_01
	tst	tmp1, #7
	b.ne	.Lmisaligned8
	ands	tmp1, src1, #7
	b.ne	.Lmutual_align

	/*
	* NUL detection works on the principle that (X - 1) & (~X) & 0x80
	* (=> (X - 1) & ~(X | 0x7f)) is non-zero iff a byte is zero, and
	* can be done in parallel across the entire word.
	*/
.Lloop_aligned:
	ldr	data1, [src1], #8
	ldr	data2, [src2], #8
.Lstart_realigned:
	sub	tmp1, data1, zeroones
	orr	tmp2, data1, #REP8_7f
	eor	diff, data1, data2	/* Non-zero if differences found.  */
	bic	has_nul, tmp1, tmp2	/* Non-zero if NUL terminator.  */
	orr	syndrome, diff, has_nul
	cbz	syndrome, .Lloop_aligned
	b	.Lcal_cmpresult

.Lmutual_align:
	/*
	* Sources are mutually aligned, but are not currently at an
	* alignment boundary.  Round down the addresses and then mask off
	* the bytes that preceed the start point.
	*/
	bic	src1, src1, #7
	bic	src2, src2, #7
	lsl	tmp1, tmp1, #3		/* Bytes beyond alignment -> bits.  */
	ldr	data1, [src1], #8
	neg	tmp1, tmp1		/* Bits to alignment -64.  */
	ldr	data2, [src2], #8
	mov	tmp2, #~0
	/* Big-endian.  Early bytes are at MSB.  */
CPU_BE( lsl	tmp2, tmp2, tmp1 )	/* Shift (tmp1 & 63).  */
	/* Little-endian.  Early bytes are at LSB.  */
CPU_LE( lsr	tmp2, tmp2, tmp1 )	/* Shift (tmp1 & 63).  */

	orr	data1, data1, tmp2
	orr	data2, data2, tmp2
	b	.Lstart_realigned

.Lmisaligned8:
	/*
	* Get the align offset length to compare per byte first.
	* After this process, one string's address will be aligned.
	*/
	and	tmp1, src1, #7
	neg	tmp1, tmp1
	add	tmp1, tmp1, #8
	and	tmp2, src2, #7
	neg	tmp2, tmp2
	add	tmp2, tmp2, #8
	subs	tmp3, tmp1, tmp2
	csel	pos, tmp1, tmp2, hi /*Choose the maximum. */
.Ltinycmp:
	ldrb	data1w, [src1], #1
	ldrb	data2w, [src2], #1
	subs	pos, pos, #1
	ccmp	data1w, #1, #0, ne  /* NZCV = 0b0000.  */
	ccmp	data1w, data2w, #0, cs  /* NZCV = 0b0000.  */
	b.eq	.Ltinycmp
	cbnz	pos, 1f /*find the null or unequal...*/
	cmp	data1w, #1
	ccmp	data1w, data2w, #0, cs
	b.eq	.Lstart_align /*the last bytes are equal....*/
1:
	sub	result, data1, data2
	ret

.Lstart_align:
	ands	xzr, src1, #7
	b.eq	.Lrecal_offset
	/*process more leading bytes to make str1 aligned...*/
	add	src1, src1, tmp3
	add	src2, src2, tmp3
	/*load 8 bytes from aligned str1 and non-aligned str2..*/
	ldr	data1, [src1], #8
	ldr	data2, [src2], #8

	sub	tmp1, data1, zeroones
	orr	tmp2, data1, #REP8_7f
	bic	has_nul, tmp1, tmp2
	eor	diff, data1, data2 /* Non-zero if differences found.  */
	orr	syndrome, diff, has_nul
	cbnz	syndrome, .Lcal_cmpresult
	/*How far is the current str2 from the alignment boundary...*/
	and	tmp3, tmp3, #7
.Lrecal_offset:
	neg	pos, tmp3
.Lloopcmp_proc:
	/*
	* Divide the eight bytes into two parts. First,backwards the src2
	* to an alignment boundary,load eight bytes from the SRC2 alignment
	* boundary,then compare with the relative bytes from SRC1.
	* If all 8 bytes are equal,then start the second part's comparison.
	* Otherwise finish the comparison.
	* This special handle can garantee all the accesses are in the
	* thread/task space in avoid to overrange access.
	*/
	ldr	data1, [src1,pos]
	ldr	data2, [src2,pos]
	sub	tmp1, data1, zeroones
	orr	tmp2, data1, #REP8_7f
	bic	has_nul, tmp1, tmp2
	eor	diff, data1, data2  /* Non-zero if differences found.  */
	orr	syndrome, diff, has_nul
	cbnz	syndrome, .Lcal_cmpresult

	/*The second part process*/
	ldr	data1, [src1], #8
	ldr	data2, [src2], #8
	sub	tmp1, data1, zeroones
	orr	tmp2, data1, #REP8_7f
	bic	has_nul, tmp1, tmp2
	eor	diff, data1, data2  /* Non-zero if differences found.  */
	orr	syndrome, diff, has_nul
	cbz	syndrome, .Lloopcmp_proc

.Lcal_cmpresult:
	/*
	* reversed the byte-order as big-endian,then CLZ can find the most
	* significant zero bits.
	*/
CPU_LE( rev	syndrome, syndrome )
CPU_LE( rev	data1, data1 )
CPU_LE( rev	data2, data2 )

	/*
	* For big-endian we cannot use the trick with the syndrome value
	* as carry-propagation can corrupt the upper bits if the trailing
	* bytes in the string contain 0x01.
	* However, if there is no NUL byte in the dword, we can generate
	* the result directly.  We ca not just subtract the bytes as the
	* MSB might be significant.
	*/
CPU_BE( cbnz	has_nul, 1f )
CPU_BE( cmp	data1, data2 )
CPU_BE( cset	result, ne )
CPU_BE( cneg	result, result, lo )
CPU_BE( ret )
CPU_BE( 1: )
	/*Re-compute the NUL-byte detection, using a byte-reversed value. */
CPU_BE(	rev	tmp3, data1 )
CPU_BE(	sub	tmp1, tmp3, zeroones )
CPU_BE(	orr	tmp2, tmp3, #REP8_7f )
CPU_BE(	bic	has_nul, tmp1, tmp2 )
CPU_BE(	rev	has_nul, has_nul )
CPU_BE(	orr	syndrome, diff, has_nul )

	clz	pos, syndrome
	/*
	* The MS-non-zero bit of the syndrome marks either the first bit
	* that is different, or the top bit of the first zero byte.
	* Shifting left now will bring the critical information into the
	* top bits.
	*/
	lsl	data1, data1, pos
	lsl	data2, data2, pos
	/*
	* But we need to zero-extend (char is unsigned) the value and then
	* perform a signed 32-bit subtraction.
	*/
	lsr	data1, data1, #56
	sub	result, data1, data2, lsr #56
	ret
SYM_FUNC_END_PI(strcmp)
EXPORT_SYMBOL_NOKASAN(strcmp)
