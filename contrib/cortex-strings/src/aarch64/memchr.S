/*
 * memchr - find a character in a memory zone
 *
 * Copyright (c) 2014, ARM Limited
 * All rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the company nor the names of its contributors
 *       may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Assumptions:
 *
 * ARMv8-a, AArch64
 * Neon Available.
 */

/* Arguments and results.  */
#define srcin		x0
#define chrin		w1
#define cntin		x2

#define result		x0

#define src		x3
#define	tmp		x4
#define wtmp2		w5
#define synd		x6
#define soff		x9
#define cntrem		x10

#define vrepchr		v0
#define vdata1		v1
#define vdata2		v2
#define vhas_chr1	v3
#define vhas_chr2	v4
#define vrepmask	v5
#define vend		v6

/*
 * Core algorithm:
 *
 * For each 32-byte chunk we calculate a 64-bit syndrome value, with two bits
 * per byte. For each tuple, bit 0 is set if the relevant byte matched the
 * requested character and bit 1 is not used (faster than using a 32bit
 * syndrome). Since the bits in the syndrome reflect exactly the order in which
 * things occur in the original string, counting trailing zeros allows to
 * identify exactly which byte has matched.
 */

	.macro def_fn f p2align=0
	.text
	.p2align \p2align
	.global \f
	.type \f, %function
\f:
	.endm

def_fn memchr
	/* Do not dereference srcin if no bytes to compare.  */
	cbz	cntin, .Lzero_length
	/*
	 * Magic constant 0x40100401 allows us to identify which lane matches
	 * the requested byte.
	 */
	mov	wtmp2, #0x0401
	movk	wtmp2, #0x4010, lsl #16
	dup	vrepchr.16b, chrin
	/* Work with aligned 32-byte chunks */
	bic	src, srcin, #31
	dup	vrepmask.4s, wtmp2
	ands	soff, srcin, #31
	and	cntrem, cntin, #31
	b.eq	.Lloop

	/*
	 * Input string is not 32-byte aligned. We calculate the syndrome
	 * value for the aligned 32 bytes block containing the first bytes
	 * and mask the irrelevant part.
	 */

	ld1	{vdata1.16b, vdata2.16b}, [src], #32
	sub	tmp, soff, #32
	adds	cntin, cntin, tmp
	cmeq	vhas_chr1.16b, vdata1.16b, vrepchr.16b
	cmeq	vhas_chr2.16b, vdata2.16b, vrepchr.16b
	and	vhas_chr1.16b, vhas_chr1.16b, vrepmask.16b
	and	vhas_chr2.16b, vhas_chr2.16b, vrepmask.16b
	addp	vend.16b, vhas_chr1.16b, vhas_chr2.16b		/* 256->128 */
	addp	vend.16b, vend.16b, vend.16b			/* 128->64 */
	mov	synd, vend.d[0]
	/* Clear the soff*2 lower bits */
	lsl	tmp, soff, #1
	lsr	synd, synd, tmp
	lsl	synd, synd, tmp
	/* The first block can also be the last */
	b.ls	.Lmasklast
	/* Have we found something already? */
	cbnz	synd, .Ltail

.Lloop:
	ld1	{vdata1.16b, vdata2.16b}, [src], #32
	subs	cntin, cntin, #32
	cmeq	vhas_chr1.16b, vdata1.16b, vrepchr.16b
	cmeq	vhas_chr2.16b, vdata2.16b, vrepchr.16b
	/* If we're out of data we finish regardless of the result */
	b.ls	.Lend
	/* Use a fast check for the termination condition */
	orr	vend.16b, vhas_chr1.16b, vhas_chr2.16b
	addp	vend.2d, vend.2d, vend.2d
	mov	synd, vend.d[0]
	/* We're not out of data, loop if we haven't found the character */
	cbz	synd, .Lloop

.Lend:
	/* Termination condition found, let's calculate the syndrome value */
	and	vhas_chr1.16b, vhas_chr1.16b, vrepmask.16b
	and	vhas_chr2.16b, vhas_chr2.16b, vrepmask.16b
	addp	vend.16b, vhas_chr1.16b, vhas_chr2.16b		/* 256->128 */
	addp	vend.16b, vend.16b, vend.16b			/* 128->64 */
	mov	synd, vend.d[0]
	/* Only do the clear for the last possible block */
	b.hi	.Ltail

.Lmasklast:
	/* Clear the (32 - ((cntrem + soff) % 32)) * 2 upper bits */
	add	tmp, cntrem, soff
	and	tmp, tmp, #31
	sub	tmp, tmp, #32
	neg	tmp, tmp, lsl #1
	lsl	synd, synd, tmp
	lsr	synd, synd, tmp

.Ltail:
	/* Count the trailing zeros using bit reversing */
	rbit	synd, synd
	/* Compensate the last post-increment */
	sub	src, src, #32
	/* Check that we have found a character */
	cmp	synd, #0
	/* And count the leading zeros */
	clz	synd, synd
	/* Compute the potential result */
	add	result, src, synd, lsr #1
	/* Select result or NULL */
	csel	result, xzr, result, eq
	ret

.Lzero_length:
	mov	result, #0
	ret

	.size	memchr, . - memchr
