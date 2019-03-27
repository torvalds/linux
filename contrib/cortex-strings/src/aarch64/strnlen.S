/* strnlen - calculate the length of a string with limit.

   Copyright (c) 2013, Linaro Limited
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:
       * Redistributions of source code must retain the above copyright
         notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above copyright
         notice, this list of conditions and the following disclaimer in the
         documentation and/or other materials provided with the distribution.
       * Neither the name of the Linaro nor the
         names of its contributors may be used to endorse or promote products
         derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

/* Assumptions:
 *
 * ARMv8-a, AArch64
 */

/* Arguments and results.  */
#define srcin		x0
#define len		x0
#define limit		x1

/* Locals and temporaries.  */
#define src		x2
#define data1		x3
#define data2		x4
#define data2a		x5
#define has_nul1	x6
#define has_nul2	x7
#define tmp1		x8
#define tmp2		x9
#define tmp3		x10
#define tmp4		x11
#define zeroones	x12
#define pos		x13
#define limit_wd	x14

	.macro def_fn f p2align=0
	.text
	.p2align \p2align
	.global \f
	.type \f, %function
\f:
	.endm

#define REP8_01 0x0101010101010101
#define REP8_7f 0x7f7f7f7f7f7f7f7f
#define REP8_80 0x8080808080808080

	.text
	.p2align	6
.Lstart:
	/* Pre-pad to ensure critical loop begins an icache line.  */
	.rep 7
	nop
	.endr
	/* Put this code here to avoid wasting more space with pre-padding.  */
.Lhit_limit:
	mov	len, limit
	ret

def_fn strnlen
	cbz	limit, .Lhit_limit
	mov	zeroones, #REP8_01
	bic	src, srcin, #15
	ands	tmp1, srcin, #15
	b.ne	.Lmisaligned
	/* Calculate the number of full and partial words -1.  */
	sub	limit_wd, limit, #1	/* Limit != 0, so no underflow.  */
	lsr	limit_wd, limit_wd, #4	/* Convert to Qwords.  */

	/* NUL detection works on the principle that (X - 1) & (~X) & 0x80
	   (=> (X - 1) & ~(X | 0x7f)) is non-zero iff a byte is zero, and
	   can be done in parallel across the entire word.  */
	/* The inner loop deals with two Dwords at a time.  This has a
	   slightly higher start-up cost, but we should win quite quickly,
	   especially on cores with a high number of issue slots per
	   cycle, as we get much better parallelism out of the operations.  */

	/* Start of critial section -- keep to one 64Byte cache line.  */
.Lloop:
	ldp	data1, data2, [src], #16
.Lrealigned:
	sub	tmp1, data1, zeroones
	orr	tmp2, data1, #REP8_7f
	sub	tmp3, data2, zeroones
	orr	tmp4, data2, #REP8_7f
	bic	has_nul1, tmp1, tmp2
	bic	has_nul2, tmp3, tmp4
	subs	limit_wd, limit_wd, #1
	orr	tmp1, has_nul1, has_nul2
	ccmp	tmp1, #0, #0, pl	/* NZCV = 0000  */
	b.eq	.Lloop
	/* End of critical section -- keep to one 64Byte cache line.  */

	orr	tmp1, has_nul1, has_nul2
	cbz	tmp1, .Lhit_limit	/* No null in final Qword.  */

	/* We know there's a null in the final Qword.  The easiest thing
	   to do now is work out the length of the string and return
	   MIN (len, limit).  */

	sub	len, src, srcin
	cbz	has_nul1, .Lnul_in_data2
#ifdef __AARCH64EB__
	mov	data2, data1
#endif
	sub	len, len, #8
	mov	has_nul2, has_nul1
.Lnul_in_data2:
#ifdef __AARCH64EB__
	/* For big-endian, carry propagation (if the final byte in the
	   string is 0x01) means we cannot use has_nul directly.  The
	   easiest way to get the correct byte is to byte-swap the data
	   and calculate the syndrome a second time.  */
	rev	data2, data2
	sub	tmp1, data2, zeroones
	orr	tmp2, data2, #REP8_7f
	bic	has_nul2, tmp1, tmp2
#endif
	sub	len, len, #8
	rev	has_nul2, has_nul2
	clz	pos, has_nul2
	add	len, len, pos, lsr #3		/* Bits to bytes.  */
	cmp	len, limit
	csel	len, len, limit, ls		/* Return the lower value.  */
	ret

.Lmisaligned:
	/* Deal with a partial first word.
	   We're doing two things in parallel here;
	   1) Calculate the number of words (but avoiding overflow if
	      limit is near ULONG_MAX) - to do this we need to work out
	      limit + tmp1 - 1 as a 65-bit value before shifting it;
	   2) Load and mask the initial data words - we force the bytes
	      before the ones we are interested in to 0xff - this ensures
	      early bytes will not hit any zero detection.  */
	sub	limit_wd, limit, #1
	neg	tmp4, tmp1
	cmp	tmp1, #8

	and	tmp3, limit_wd, #15
	lsr	limit_wd, limit_wd, #4
	mov	tmp2, #~0

	ldp	data1, data2, [src], #16
	lsl	tmp4, tmp4, #3		/* Bytes beyond alignment -> bits.  */
	add	tmp3, tmp3, tmp1

#ifdef __AARCH64EB__
	/* Big-endian.  Early bytes are at MSB.  */
	lsl	tmp2, tmp2, tmp4	/* Shift (tmp1 & 63).  */
#else
	/* Little-endian.  Early bytes are at LSB.  */
	lsr	tmp2, tmp2, tmp4	/* Shift (tmp1 & 63).  */
#endif
	add	limit_wd, limit_wd, tmp3, lsr #4

	orr	data1, data1, tmp2
	orr	data2a, data2, tmp2

	csinv	data1, data1, xzr, le
	csel	data2, data2, data2a, le
	b	.Lrealigned
	.size	strnlen, . - .Lstart	/* Include pre-padding in size.  */
