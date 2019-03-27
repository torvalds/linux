/* Copyright (c) 2012, Linaro Limited
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

/*
 * Copyright (c) 2015 ARM Ltd
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the company may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ARM LTD ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ARM LTD BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Assumptions:
 *
 * ARMv8-a, AArch64, unaligned accesses
 *
 */


#define dstin	x0
#define val	x1
#define valw	w1
#define count	x2
#define dst	x3
#define dstend	x4
#define tmp1	x5
#define tmp1w	w5
#define tmp2	x6
#define tmp2w	w6
#define zva_len x7
#define zva_lenw w7

#define L(l) .L ## l

	.macro def_fn f p2align=0
	.text
	.p2align \p2align
	.global \f
	.type \f, %function
\f:
	.endm

def_fn memset p2align=6

	dup	v0.16B, valw
	add	dstend, dstin, count

	cmp	count, 96
	b.hi	L(set_long)
	cmp	count, 16
	b.hs	L(set_medium)
	mov	val, v0.D[0]

	/* Set 0..15 bytes.  */
	tbz	count, 3, 1f
	str	val, [dstin]
	str	val, [dstend, -8]
	ret
	nop
1:	tbz	count, 2, 2f
	str	valw, [dstin]
	str	valw, [dstend, -4]
	ret
2:	cbz	count, 3f
	strb	valw, [dstin]
	tbz	count, 1, 3f
	strh	valw, [dstend, -2]
3:	ret

	/* Set 17..96 bytes.  */
L(set_medium):
	str	q0, [dstin]
	tbnz	count, 6, L(set96)
	str	q0, [dstend, -16]
	tbz	count, 5, 1f
	str	q0, [dstin, 16]
	str	q0, [dstend, -32]
1:	ret

	.p2align 4
	/* Set 64..96 bytes.  Write 64 bytes from the start and
	   32 bytes from the end.  */
L(set96):
	str	q0, [dstin, 16]
	stp	q0, q0, [dstin, 32]
	stp	q0, q0, [dstend, -32]
	ret

	.p2align 3
	nop
L(set_long):
	and	valw, valw, 255
	bic	dst, dstin, 15
	str	q0, [dstin]
	cmp	count, 256
	ccmp	valw, 0, 0, cs
	b.eq	L(try_zva)
L(no_zva):
	sub	count, dstend, dst	/* Count is 16 too large.  */
	add	dst, dst, 16
	sub	count, count, 64 + 16	/* Adjust count and bias for loop.  */
1:	stp	q0, q0, [dst], 64
	stp	q0, q0, [dst, -32]
L(tail64):
	subs	count, count, 64
	b.hi	1b
2:	stp	q0, q0, [dstend, -64]
	stp	q0, q0, [dstend, -32]
	ret

	.p2align 3
L(try_zva):
	mrs	tmp1, dczid_el0
	tbnz	tmp1w, 4, L(no_zva)
	and	tmp1w, tmp1w, 15
	cmp	tmp1w, 4	/* ZVA size is 64 bytes.  */
	b.ne	 L(zva_128)

	/* Write the first and last 64 byte aligned block using stp rather
	   than using DC ZVA.  This is faster on some cores.
	 */
L(zva_64):
	str	q0, [dst, 16]
	stp	q0, q0, [dst, 32]
	bic	dst, dst, 63
	stp	q0, q0, [dst, 64]
	stp	q0, q0, [dst, 96]
	sub	count, dstend, dst	/* Count is now 128 too large.	*/
	sub	count, count, 128+64+64	/* Adjust count and bias for loop.  */
	add	dst, dst, 128
	nop
1:	dc	zva, dst
	add	dst, dst, 64
	subs	count, count, 64
	b.hi	1b
	stp	q0, q0, [dst, 0]
	stp	q0, q0, [dst, 32]
	stp	q0, q0, [dstend, -64]
	stp	q0, q0, [dstend, -32]
	ret

	.p2align 3
L(zva_128):
	cmp	tmp1w, 5	/* ZVA size is 128 bytes.  */
	b.ne	L(zva_other)

	str	q0, [dst, 16]
	stp	q0, q0, [dst, 32]
	stp	q0, q0, [dst, 64]
	stp	q0, q0, [dst, 96]
	bic	dst, dst, 127
	sub	count, dstend, dst	/* Count is now 128 too large.	*/
	sub	count, count, 128+128	/* Adjust count and bias for loop.  */
	add	dst, dst, 128
1:	dc	zva, dst
	add	dst, dst, 128
	subs	count, count, 128
	b.hi	1b
	stp	q0, q0, [dstend, -128]
	stp	q0, q0, [dstend, -96]
	stp	q0, q0, [dstend, -64]
	stp	q0, q0, [dstend, -32]
	ret

L(zva_other):
	mov	tmp2w, 4
	lsl	zva_lenw, tmp2w, tmp1w
	add	tmp1, zva_len, 64	/* Max alignment bytes written.	 */
	cmp	count, tmp1
	blo	L(no_zva)

	sub	tmp2, zva_len, 1
	add	tmp1, dst, zva_len
	add	dst, dst, 16
	subs	count, tmp1, dst	/* Actual alignment bytes to write.  */
	bic	tmp1, tmp1, tmp2	/* Aligned dc zva start address.  */
	beq	2f
1:	stp	q0, q0, [dst], 64
	stp	q0, q0, [dst, -32]
	subs	count, count, 64
	b.hi	1b
2:	mov	dst, tmp1
	sub	count, dstend, tmp1	/* Remaining bytes to write.  */
	subs	count, count, zva_len
	b.lo	4f
3:	dc	zva, dst
	add	dst, dst, zva_len
	subs	count, count, zva_len
	b.hs	3b
4:	add	count, count, zva_len
	b	L(tail64)

	.size	memset, . - memset
