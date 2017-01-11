/*
 * Accelerated poly_hash implementation with ARMv8 PMULL instructions.
 *
 * Based on ghash-ce-core.S.
 *
 * Copyright (C) 2014 Linaro Ltd. <ard.biesheuvel@linaro.org>
 * Copyright (C) 2017 Google, Inc. <ebiggers@google.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	KEY	.req	v0
	KEY2	.req	v1
	T1	.req	v2
	T2	.req	v3
	GSTAR	.req	v4
	XL	.req	v5
	XM	.req	v6
	XH	.req	v7

	.text
	.arch		armv8-a+crypto

	/* 16-byte aligned (2**4 = 16); not required, but might as well */
	.align		4
.Lgstar:
	.quad		0x87, 0x87

/*
 * void pmull_poly_hash_update(le128 *digest, const le128 *key,
 *			       const u8 *src, unsigned int blocks,
 *			       unsigned int partial);
 */
ENTRY(pmull_poly_hash_update)

	/* Load digest into XL */
	ld1		{XL.16b}, [x0]

	/* Load key into KEY */
	ld1		{KEY.16b}, [x1]

	/* Load g*(x) = g(x) + x^128 = x^7 + x^2 + x + 1 into both halves of
	 * GSTAR */
	adr		x1, .Lgstar
	ld1		{GSTAR.2d}, [x1]

	/* Set KEY2 to (KEY[1]+KEY[0]):(KEY[1]+KEY[0]).  This is needed for
	 * Karatsuba multiplication. */
	ext		KEY2.16b, KEY.16b, KEY.16b, #8
	eor		KEY2.16b, KEY2.16b, KEY.16b

	/* If 'partial' is nonzero, then we're finishing a pending block and
	 * should go right to the multiplication. */
	cbnz		w4, 1f

0:
	/* Add the next block from 'src' to the digest */
	ld1		{T1.16b}, [x2], #16
	eor		XL.16b, XL.16b, T1.16b
	sub		w3, w3, #1

1:
	/*
	 * Multiply the current 128-bit digest (a1:a0, in XL) by the 128-bit key
	 * (b1:b0, in KEY) using Karatsuba multiplication.
	 */

	/* T1 = (a1+a0):(a1+a0) */
	ext		T1.16b, XL.16b, XL.16b, #8
	eor		T1.16b, T1.16b, XL.16b

	/* XH = a1 * b1 */
	pmull2		XH.1q, XL.2d, KEY.2d

	/* XL = a0 * b0 */
	pmull		XL.1q, XL.1d, KEY.1d

	/* XM = (a1+a0) * (b1+b0) */
	pmull		XM.1q, T1.1d, KEY2.1d

	/* XM += (XH[0]:XL[1]) + XL + XH */
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		T2.16b, XL.16b, XH.16b
	eor		XM.16b, XM.16b, T1.16b
	eor		XM.16b, XM.16b, T2.16b

	/*
	 * Now the 256-bit product is in XH[1]:XM:XL[0].  It represents a
	 * polynomial over GF(2) with degree as large as 255.  We need to
	 * compute its remainder modulo g(x) = x^128+x^7+x^2+x+1.  For this it
	 * is sufficient to compute the remainder of the high half 'c(x)x^128'
	 * add it to the low half.  To reduce the high half we use the Barrett
	 * reduction method.  The basic idea is that we can express the
	 * remainder p(x) as g(x)q(x) mod x^128, where q(x) = (c(x)x^128)/g(x).
	 * As detailed in [1], to avoid having to divide by g(x) at runtime the
	 * following equivalent expression can be derived:
	 *
	 *	p(x) = [ g*(x)((c(x)q+(x))/x^128) ] mod x^128
	 *
	 * where g*(x) = x^128+g(x) = x^7+x^2+x+1, and q+(x) = x^256/g(x) = g(x)
	 * in this case.  This is also equivalent to:
	 *
	 *	p(x) = [ g*(x)((c(x)(x^128 + g*(x)))/x^128) ] mod x^128
	 *	     = [ g*(x)(c(x) + (c(x)g*(x))/x^128) ] mod x^128
	 *
	 * Since deg g*(x) < 64:
	 *
	 *	p(x) = [ g*(x)(c(x) + ((c(x)/x^64)g*(x))/x^64) ] mod x^128
	 *	     = [ g*(x)((c(x)/x^64)x^64 + (c(x) mod x^64) +
	 *				((c(x)/x^64)g*(x))/x^64) ] mod x^128
	 *
	 * Letting t(x) = g*(x)(c(x)/x^64):
	 *
	 *	p(x) = [ t(x)x^64 + g*(x)((c(x) mod x^64) + t(x)/x^64) ] mod x^128
	 *
	 * Therefore, to do the reduction we only need to issue two 64-bit =>
	 * 128-bit carryless multiplications: g*(x) times c(x)/x^64, and g*(x)
	 * times ((c(x) mod x^64) + t(x)/x^64).  (Multiplication by x^64 doesn't
	 * count since it is simply a shift or move.)
	 *
	 * An alternate reduction method, also based on Barrett reduction and
	 * described in [1], uses only shifts and XORs --- no multiplications.
	 * However, the method with multiplications requires fewer instructions
	 * and is faster on processors with fast carryless multiplication.
	 *
	 * [1] "Intel Carry-Less Multiplication Instruction and its Usage for
	 * Computing the GCM Mode",
	 * https://software.intel.com/sites/default/files/managed/72/cc/clmul-wp-rev-2.02-2014-04-20.pdf
	 */

	/* 256-bit product is XH[1]:XM:XL[0], so c(x) is XH[1]:XM[1] */

	/* T1 = t(x) = g*(x)(c(x)/x^64) */
	pmull2		T1.1q, GSTAR.2d, XH.2d

	/* T2 = g*(x)((c(x) mod x^64) + t(x)/x^64) */
	eor		T2.16b, XM.16b, T1.16b
	pmull2		T2.1q, GSTAR.2d, T2.2d

	/* Make XL[0] be the low half of the 128-bit result by adding the low 64
	 * bits of the T2 term to what was already there.  The 't(x)x^64' term
	 * makes no difference, so skip it. */
	eor		XL.16b, XL.16b, T2.16b

	/* Make XL[1] be the high half of the 128-bit result by adding the high
	 * 64 bits of the 't(x)x^64' and T2 terms to what was already in XM[0],
	 * then moving XM[0] to XL[1]. */
	eor		XM.16b, XM.16b, T1.16b
	ext		T2.16b, T2.16b, T2.16b, #8
	eor		XM.16b, XM.16b, T2.16b
	mov		XL.d[1], XM.d[0]

	/* If more blocks remain, then loop back to process the next block;
	 * else, store the digest and return. */
	cbnz		w3, 0b
	st1		{XL.16b}, [x0]
	ret
ENDPROC(pmull_poly_hash_update)
