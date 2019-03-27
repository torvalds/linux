/* Copyright (c) 2013-2015, Linaro Limited
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
 * ARMv8-a, AArch64, unaligned accesses, min page size 4k.
 */

/* To test the page crossing code path more thoroughly, compile with
   -DTEST_PAGE_CROSS - this will force all calls through the slower
   entry path.  This option is not intended for production use.	 */

/* Arguments and results.  */
#define srcin		x0
#define len		x0

/* Locals and temporaries.  */
#define src		x1
#define data1		x2
#define data2		x3
#define has_nul1	x4
#define has_nul2	x5
#define tmp1		x4
#define tmp2		x5
#define tmp3		x6
#define tmp4		x7
#define zeroones	x8

#define L(l) .L ## l

	.macro def_fn f p2align=0
	.text
	.p2align \p2align
	.global \f
	.type \f, %function
\f:
	.endm

	/* NUL detection works on the principle that (X - 1) & (~X) & 0x80
	   (=> (X - 1) & ~(X | 0x7f)) is non-zero iff a byte is zero, and
	   can be done in parallel across the entire word. A faster check
	   (X - 1) & 0x80 is zero for non-NUL ASCII characters, but gives
	   false hits for characters 129..255.	*/

#define REP8_01 0x0101010101010101
#define REP8_7f 0x7f7f7f7f7f7f7f7f
#define REP8_80 0x8080808080808080

#ifdef TEST_PAGE_CROSS
# define MIN_PAGE_SIZE 15
#else
# define MIN_PAGE_SIZE 4096
#endif

	/* Since strings are short on average, we check the first 16 bytes
	   of the string for a NUL character.  In order to do an unaligned ldp
	   safely we have to do a page cross check first.  If there is a NUL
	   byte we calculate the length from the 2 8-byte words using
	   conditional select to reduce branch mispredictions (it is unlikely
	   strlen will be repeatedly called on strings with the same length).

	   If the string is longer than 16 bytes, we align src so don't need
	   further page cross checks, and process 32 bytes per iteration
	   using the fast NUL check.  If we encounter non-ASCII characters,
	   fallback to a second loop using the full NUL check.

	   If the page cross check fails, we read 16 bytes from an aligned
	   address, remove any characters before the string, and continue
	   in the main loop using aligned loads.  Since strings crossing a
	   page in the first 16 bytes are rare (probability of
	   16/MIN_PAGE_SIZE ~= 0.4%), this case does not need to be optimized.

	   AArch64 systems have a minimum page size of 4k.  We don't bother
	   checking for larger page sizes - the cost of setting up the correct
	   page size is just not worth the extra gain from a small reduction in
	   the cases taking the slow path.  Note that we only care about
	   whether the first fetch, which may be misaligned, crosses a page
	   boundary.  */

def_fn strlen p2align=6
	and	tmp1, srcin, MIN_PAGE_SIZE - 1
	mov	zeroones, REP8_01
	cmp	tmp1, MIN_PAGE_SIZE - 16
	b.gt	L(page_cross)
	ldp	data1, data2, [srcin]
#ifdef __AARCH64EB__
	/* For big-endian, carry propagation (if the final byte in the
	   string is 0x01) means we cannot use has_nul1/2 directly.
	   Since we expect strings to be small and early-exit,
	   byte-swap the data now so has_null1/2 will be correct.  */
	rev	data1, data1
	rev	data2, data2
#endif
	sub	tmp1, data1, zeroones
	orr	tmp2, data1, REP8_7f
	sub	tmp3, data2, zeroones
	orr	tmp4, data2, REP8_7f
	bics	has_nul1, tmp1, tmp2
	bic	has_nul2, tmp3, tmp4
	ccmp	has_nul2, 0, 0, eq
	beq	L(main_loop_entry)

	/* Enter with C = has_nul1 == 0.  */
	csel	has_nul1, has_nul1, has_nul2, cc
	mov	len, 8
	rev	has_nul1, has_nul1
	clz	tmp1, has_nul1
	csel	len, xzr, len, cc
	add	len, len, tmp1, lsr 3
	ret

	/* The inner loop processes 32 bytes per iteration and uses the fast
	   NUL check.  If we encounter non-ASCII characters, use a second
	   loop with the accurate NUL check.  */
	.p2align 4
L(main_loop_entry):
	bic	src, srcin, 15
	sub	src, src, 16
L(main_loop):
	ldp	data1, data2, [src, 32]!
.Lpage_cross_entry:
	sub	tmp1, data1, zeroones
	sub	tmp3, data2, zeroones
	orr	tmp2, tmp1, tmp3
	tst	tmp2, zeroones, lsl 7
	bne	1f
	ldp	data1, data2, [src, 16]
	sub	tmp1, data1, zeroones
	sub	tmp3, data2, zeroones
	orr	tmp2, tmp1, tmp3
	tst	tmp2, zeroones, lsl 7
	beq	L(main_loop)
	add	src, src, 16
1:
	/* The fast check failed, so do the slower, accurate NUL check.	 */
	orr	tmp2, data1, REP8_7f
	orr	tmp4, data2, REP8_7f
	bics	has_nul1, tmp1, tmp2
	bic	has_nul2, tmp3, tmp4
	ccmp	has_nul2, 0, 0, eq
	beq	L(nonascii_loop)

	/* Enter with C = has_nul1 == 0.  */
L(tail):
#ifdef __AARCH64EB__
	/* For big-endian, carry propagation (if the final byte in the
	   string is 0x01) means we cannot use has_nul1/2 directly.  The
	   easiest way to get the correct byte is to byte-swap the data
	   and calculate the syndrome a second time.  */
	csel	data1, data1, data2, cc
	rev	data1, data1
	sub	tmp1, data1, zeroones
	orr	tmp2, data1, REP8_7f
	bic	has_nul1, tmp1, tmp2
#else
	csel	has_nul1, has_nul1, has_nul2, cc
#endif
	sub	len, src, srcin
	rev	has_nul1, has_nul1
	add	tmp2, len, 8
	clz	tmp1, has_nul1
	csel	len, len, tmp2, cc
	add	len, len, tmp1, lsr 3
	ret

L(nonascii_loop):
	ldp	data1, data2, [src, 16]!
	sub	tmp1, data1, zeroones
	orr	tmp2, data1, REP8_7f
	sub	tmp3, data2, zeroones
	orr	tmp4, data2, REP8_7f
	bics	has_nul1, tmp1, tmp2
	bic	has_nul2, tmp3, tmp4
	ccmp	has_nul2, 0, 0, eq
	bne	L(tail)
	ldp	data1, data2, [src, 16]!
	sub	tmp1, data1, zeroones
	orr	tmp2, data1, REP8_7f
	sub	tmp3, data2, zeroones
	orr	tmp4, data2, REP8_7f
	bics	has_nul1, tmp1, tmp2
	bic	has_nul2, tmp3, tmp4
	ccmp	has_nul2, 0, 0, eq
	beq	L(nonascii_loop)
	b	L(tail)

	/* Load 16 bytes from [srcin & ~15] and force the bytes that precede
	   srcin to 0x7f, so we ignore any NUL bytes before the string.
	   Then continue in the aligned loop.  */
L(page_cross):
	bic	src, srcin, 15
	ldp	data1, data2, [src]
	lsl	tmp1, srcin, 3
	mov	tmp4, -1
#ifdef __AARCH64EB__
	/* Big-endian.	Early bytes are at MSB.	 */
	lsr	tmp1, tmp4, tmp1	/* Shift (tmp1 & 63).  */
#else
	/* Little-endian.  Early bytes are at LSB.  */
	lsl	tmp1, tmp4, tmp1	/* Shift (tmp1 & 63).  */
#endif
	orr	tmp1, tmp1, REP8_80
	orn	data1, data1, tmp1
	orn	tmp2, data2, tmp1
	tst	srcin, 8
	csel	data1, data1, tmp4, eq
	csel	data2, data2, tmp2, eq
	b	L(page_cross_entry)

	.size	strlen, . - strlen
