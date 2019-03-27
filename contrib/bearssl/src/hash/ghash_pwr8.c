/*
 * Copyright (c) 2017 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define BR_POWER_ASM_MACROS   1
#include "inner.h"

/*
 * This is the GHASH implementation that leverages the POWER8 opcodes.
 */

#if BR_POWER8

/*
 * Some symbolic names for registers.
 *   HB0 = 16 bytes of value 0
 *   HB1 = 16 bytes of value 1
 *   HB2 = 16 bytes of value 2
 *   HB6 = 16 bytes of value 6
 *   HB7 = 16 bytes of value 7
 *   TT0, TT1 and TT2 are temporaries
 *
 * BSW holds the pattern for byteswapping 32-bit words; this is set only
 * on little-endian systems. XBSW is the same register with the +32 offset
 * for access with the VSX opcodes.
 */
#define HB0     0
#define HB1     1
#define HB2     2
#define HB6     3
#define HB7     4
#define TT0     5
#define TT1     6
#define TT2     7

#define BSW     8
#define XBSW   40

/*
 * Macro to initialise the constants.
 */
#define INIT \
		vxor(HB0, HB0, HB0) \
		vspltisb(HB1, 1) \
		vspltisb(HB2, 2) \
		vspltisb(HB6, 6) \
		vspltisb(HB7, 7) \
		INIT_BSW

/*
 * Fix endianness of a value after reading it or before writing it, if
 * necessary.
 */
#if BR_POWER8_LE
#define INIT_BSW         lxvw4x(XBSW, 0, %[idx2be])
#define FIX_ENDIAN(xx)   vperm(xx, xx, xx, BSW)
#else
#define INIT_BSW
#define FIX_ENDIAN(xx)
#endif

/*
 * Left-shift x0:x1 by one bit to the left. This is a corrective action
 * needed because GHASH is defined in full little-endian specification,
 * while the opcodes use full big-endian convention, so the 255-bit product
 * ends up one bit to the right.
 */
#define SL_256(x0, x1) \
		vsldoi(TT0, HB0, x1, 1) \
		vsl(x0, x0, HB1) \
		vsr(TT0, TT0, HB7) \
		vsl(x1, x1, HB1) \
		vxor(x0, x0, TT0)

/*
 * Reduce x0:x1 in GF(2^128), result in xd (register xd may be the same as
 * x0 or x1, or a different register). x0 and x1 are modified.
 */
#define REDUCE_F128(xd, x0, x1) \
		vxor(x0, x0, x1) \
		vsr(TT0, x1, HB1) \
		vsr(TT1, x1, HB2) \
		vsr(TT2, x1, HB7) \
		vxor(x0, x0, TT0) \
		vxor(TT1, TT1, TT2) \
		vxor(x0, x0, TT1) \
		vsldoi(x1, x1, HB0, 15) \
		vsl(TT1, x1, HB6) \
		vsl(TT2, x1, HB1) \
		vxor(x1, TT1, TT2) \
		vsr(TT0, x1, HB1) \
		vsr(TT1, x1, HB2) \
		vsr(TT2, x1, HB7) \
		vxor(x0, x0, x1) \
		vxor(x0, x0, TT0) \
		vxor(TT1, TT1, TT2) \
		vxor(xd, x0, TT1)

/* see bearssl_hash.h */
void
br_ghash_pwr8(void *y, const void *h, const void *data, size_t len)
{
	const unsigned char *buf1, *buf2;
	size_t num4, num1;
	unsigned char tmp[64];
	long cc0, cc1, cc2, cc3;

#if BR_POWER8_LE
	static const uint32_t idx2be[] = {
		0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C
	};
#endif

	buf1 = data;

	/*
	 * Assembly code requires data into two chunks; first chunk
	 * must contain a number of blocks which is a multiple of 4.
	 * Since the processing for the first chunk is faster, we want
	 * to make it as big as possible.
	 *
	 * For the remainder, there are two possibilities:
	 *  -- if the remainder size is a multiple of 16, then use it
	 *     in place;
	 *  -- otherwise, copy it to the tmp[] array and pad it with
	 *     zeros.
	 */
	num4 = len >> 6;
	buf2 = buf1 + (num4 << 6);
	len &= 63;
	num1 = (len + 15) >> 4;
	if ((len & 15) != 0) {
		memcpy(tmp, buf2, len);
		memset(tmp + len, 0, (num1 << 4) - len);
		buf2 = tmp;
	}

	cc0 =  0;
	cc1 = 16;
	cc2 = 32;
	cc3 = 48;
	asm volatile (
		INIT

		/*
		 * Load current h (denoted hereafter h1) in v9.
		 */
		lxvw4x(41, 0, %[h])
		FIX_ENDIAN(9)

		/*
		 * Load current y into v28.
		 */
		lxvw4x(60, 0, %[y])
		FIX_ENDIAN(28)

		/*
		 * Split h1 into three registers:
		 *   v17 = h1_1:h1_0
		 *   v18 =    0:h1_0
		 *   v19 = h1_1:0
		 */
		xxpermdi(49, 41, 41, 2)
		vsldoi(18, HB0, 9, 8)
		vsldoi(19, 9, HB0, 8)

		/*
		 * If num4 is 0, skip directly to the second chunk.
		 */
		cmpldi(%[num4], 0)
		beq(chunk1)

		/*
		 * Compute h2 = h*h in v10.
		 */
		vpmsumd(10, 18, 18)
		vpmsumd(11, 19, 19)
		SL_256(10, 11)
		REDUCE_F128(10, 10, 11)

		/*
		 * Compute h3 = h*h*h in v11.
		 * We first split h2 into:
		 *   v10 = h2_0:h2_1
		 *   v11 =    0:h2_0
		 *   v12 = h2_1:0
		 * Then we do the product with h1, and reduce into v11.
		 */
		vsldoi(11, HB0, 10, 8)
		vsldoi(12, 10, HB0, 8)
		vpmsumd(13, 10, 17)
		vpmsumd(11, 11, 18)
		vpmsumd(12, 12, 19)
		vsldoi(14, HB0, 13, 8)
		vsldoi(15, 13, HB0, 8)
		vxor(11, 11, 14)
		vxor(12, 12, 15)
		SL_256(11, 12)
		REDUCE_F128(11, 11, 12)

		/*
		 * Compute h4 = h*h*h*h in v12. This is done by squaring h2.
		 */
		vsldoi(12, HB0, 10, 8)
		vsldoi(13, 10, HB0, 8)
		vpmsumd(12, 12, 12)
		vpmsumd(13, 13, 13)
		SL_256(12, 13)
		REDUCE_F128(12, 12, 13)

		/*
		 * Repack h1, h2, h3 and h4:
		 *   v13 = h4_0:h3_0
		 *   v14 = h4_1:h3_1
		 *   v15 = h2_0:h1_0
		 *   v16 = h2_1:h1_1
		 */
		xxpermdi(45, 44, 43, 0)
		xxpermdi(46, 44, 43, 3)
		xxpermdi(47, 42, 41, 0)
		xxpermdi(48, 42, 41, 3)

		/*
		 * Loop for each group of four blocks.
		 */
		mtctr(%[num4])
	label(loop4)
		/*
		 * Read the four next blocks.
		 *   v20 = y + a0 = b0
		 *   v21 = a1     = b1
		 *   v22 = a2     = b2
		 *   v23 = a3     = b3
		 */
		lxvw4x(52, %[cc0], %[buf1])
		lxvw4x(53, %[cc1], %[buf1])
		lxvw4x(54, %[cc2], %[buf1])
		lxvw4x(55, %[cc3], %[buf1])
		FIX_ENDIAN(20)
		FIX_ENDIAN(21)
		FIX_ENDIAN(22)
		FIX_ENDIAN(23)
		addi(%[buf1], %[buf1], 64)
		vxor(20, 20, 28)

		/*
		 * Repack the blocks into v9, v10, v11 and v12.
		 *   v9  = b0_0:b1_0
		 *   v10 = b0_1:b1_1
		 *   v11 = b2_0:b3_0
		 *   v12 = b2_1:b3_1
		 */
		xxpermdi(41, 52, 53, 0)
		xxpermdi(42, 52, 53, 3)
		xxpermdi(43, 54, 55, 0)
		xxpermdi(44, 54, 55, 3)

		/*
		 * Compute the products.
		 *   v20 = b0_0*h4_0 + b1_0*h3_0
		 *   v21 = b0_1*h4_0 + b1_1*h3_0
		 *   v22 = b0_0*h4_1 + b1_0*h3_1
		 *   v23 = b0_1*h4_1 + b1_1*h3_1
		 *   v24 = b2_0*h2_0 + b3_0*h1_0
		 *   v25 = b2_1*h2_0 + b3_1*h1_0
		 *   v26 = b2_0*h2_1 + b3_0*h1_1
		 *   v27 = b2_1*h2_1 + b3_1*h1_1
		 */
		vpmsumd(20, 13,  9)
		vpmsumd(21, 13, 10)
		vpmsumd(22, 14,  9)
		vpmsumd(23, 14, 10)
		vpmsumd(24, 15, 11)
		vpmsumd(25, 15, 12)
		vpmsumd(26, 16, 11)
		vpmsumd(27, 16, 12)

		/*
		 * Sum products into a single 256-bit result in v11:v12.
		 */
		vxor(11, 20, 24)
		vxor(12, 23, 27)
		vxor( 9, 21, 22)
		vxor(10, 25, 26)
		vxor(20,  9, 10)
		vsldoi( 9, HB0, 20, 8)
		vsldoi(10, 20, HB0, 8)
		vxor(11, 11, 9)
		vxor(12, 12, 10)

		/*
		 * Fix and reduce in GF(2^128); this is the new y (in v28).
		 */
		SL_256(11, 12)
		REDUCE_F128(28, 11, 12)

		/*
		 * Loop for next group of four blocks.
		 */
		bdnz(loop4)

		/*
		 * Process second chunk, one block at a time.
		 */
	label(chunk1)
		cmpldi(%[num1], 0)
		beq(done)

		mtctr(%[num1])
	label(loop1)
		/*
		 * Load next data block and XOR it into y.
		 */
		lxvw4x(41, 0, %[buf2])
#if BR_POWER8_LE
		FIX_ENDIAN(9)
#endif
		addi(%[buf2], %[buf2], 16)
		vxor(9, 28, 9)

		/*
		 * Split y into doublewords:
		 *   v9  = y_0:y_1
		 *   v10 =   0:y_0
		 *   v11 = y_1:0
		 */
		vsldoi(10, HB0, 9, 8)
		vsldoi(11, 9, HB0, 8)

		/*
		 * Compute products with h:
		 *   v12 = y_0 * h_0
		 *   v13 = y_1 * h_1
		 *   v14 = y_1 * h_0 + y_0 * h_1
		 */
		vpmsumd(14,  9, 17)
		vpmsumd(12, 10, 18)
		vpmsumd(13, 11, 19)

		/*
		 * Propagate v14 into v12:v13 to finalise product.
		 */
		vsldoi(10, HB0, 14, 8)
		vsldoi(11, 14, HB0, 8)
		vxor(12, 12, 10)
		vxor(13, 13, 11)

		/*
		 * Fix result and reduce into v28 (next value for y).
		 */
		SL_256(12, 13)
		REDUCE_F128(28, 12, 13)
		bdnz(loop1)

	label(done)
		/*
		 * Write back the new y.
		 */
		FIX_ENDIAN(28)
		stxvw4x(60, 0, %[y])

: [buf1] "+b" (buf1), [buf2] "+b" (buf2)
: [y] "b" (y), [h] "b" (h), [num4] "b" (num4), [num1] "b" (num1),
  [cc0] "b" (cc0), [cc1] "b" (cc1), [cc2] "b" (cc2), [cc3] "b" (cc3)
#if BR_POWER8_LE
	, [idx2be] "b" (idx2be)
#endif
: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9",
  "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19",
  "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29",
  "ctr", "memory"
	);
}

/* see bearssl_hash.h */
br_ghash
br_ghash_pwr8_get(void)
{
	return &br_ghash_pwr8;
}

#else

/* see bearssl_hash.h */
br_ghash
br_ghash_pwr8_get(void)
{
	return 0;
}

#endif
