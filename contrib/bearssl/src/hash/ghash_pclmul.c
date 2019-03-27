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

#define BR_ENABLE_INTRINSICS   1
#include "inner.h"

/*
 * This is the GHASH implementation that leverages the pclmulqdq opcode
 * (from the AES-NI instructions).
 */

#if BR_AES_X86NI

/*
 * Test CPU support for PCLMULQDQ.
 */
static inline int
pclmul_supported(void)
{
	/*
	 * Bit mask for features in ECX:
	 *    1   PCLMULQDQ support
	 */
	return br_cpuid(0, 0, 0x00000002, 0);
}

/* see bearssl_hash.h */
br_ghash
br_ghash_pclmul_get(void)
{
	return pclmul_supported() ? &br_ghash_pclmul : 0;
}

BR_TARGETS_X86_UP

/*
 * GHASH is defined over elements of GF(2^128) with "full little-endian"
 * representation: leftmost byte is least significant, and, within each
 * byte, leftmost _bit_ is least significant. The natural ordering in
 * x86 is "mixed little-endian": bytes are ordered from least to most
 * significant, but bits within a byte are in most-to-least significant
 * order. Going to full little-endian representation would require
 * reversing bits within each byte, which is doable but expensive.
 *
 * Instead, we go to full big-endian representation, by swapping bytes
 * around, which is done with a single _mm_shuffle_epi8() opcode (it
 * comes with SSSE3; all CPU that offer pclmulqdq also have SSSE3). We
 * can use a full big-endian representation because in a carryless
 * multiplication, we have a nice bit reversal property:
 *
 *    rev_128(x) * rev_128(y) = rev_255(x * y)
 *
 * So by using full big-endian, we still get the right result, except
 * that it is right-shifted by 1 bit. The left-shift is relatively
 * inexpensive, and it can be mutualised.
 *
 *
 * Since SSE2 opcodes do not have facilities for shitfting full 128-bit
 * values with bit precision, we have to break down values into 64-bit
 * chunks. We number chunks from 0 to 3 in left to right order.
 */

/*
 * Byte-swap a complete 128-bit value. This normally uses
 * _mm_shuffle_epi8(), which gets translated to pshufb (an SSSE3 opcode).
 * However, this crashes old Clang versions, so, for Clang before 3.8,
 * we use an alternate (and less efficient) version.
 */
#if BR_CLANG && !BR_CLANG_3_8
#define BYTESWAP_DECL
#define BYTESWAP_PREP   (void)0
#define BYTESWAP(x)   do { \
		__m128i byteswap1, byteswap2; \
		byteswap1 = (x); \
		byteswap2 = _mm_srli_epi16(byteswap1, 8); \
		byteswap1 = _mm_slli_epi16(byteswap1, 8); \
		byteswap1 = _mm_or_si128(byteswap1, byteswap2); \
		byteswap1 = _mm_shufflelo_epi16(byteswap1, 0x1B); \
		byteswap1 = _mm_shufflehi_epi16(byteswap1, 0x1B); \
		(x) = _mm_shuffle_epi32(byteswap1, 0x4E); \
	} while (0)
#else
#define BYTESWAP_DECL   __m128i byteswap_index;
#define BYTESWAP_PREP   do { \
		byteswap_index = _mm_set_epi8( \
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15); \
	} while (0)
#define BYTESWAP(x)   do { \
		(x) = _mm_shuffle_epi8((x), byteswap_index); \
	} while (0)
#endif

/*
 * Call pclmulqdq. Clang appears to have trouble with the intrinsic, so,
 * for that compiler, we use inline assembly. Inline assembly is
 * potentially a bit slower because the compiler does not understand
 * what the opcode does, and thus cannot optimize instruction
 * scheduling.
 *
 * We use a target of "sse2" only, so that Clang may still handle the
 * '__m128i' type and allocate SSE2 registers.
 */
#if BR_CLANG
BR_TARGET("sse2")
static inline __m128i
pclmulqdq00(__m128i x, __m128i y)
{
	__asm__ ("pclmulqdq $0x00, %1, %0" : "+x" (x) : "x" (y));
	return x;
}
BR_TARGET("sse2")
static inline __m128i
pclmulqdq11(__m128i x, __m128i y)
{
	__asm__ ("pclmulqdq $0x11, %1, %0" : "+x" (x) : "x" (y));
	return x;
}
#else
#define pclmulqdq00(x, y)   _mm_clmulepi64_si128(x, y, 0x00)
#define pclmulqdq11(x, y)   _mm_clmulepi64_si128(x, y, 0x11)
#endif

/*
 * From a 128-bit value kw, compute kx as the XOR of the two 64-bit
 * halves of kw (into the right half of kx; left half is unspecified).
 */
#define BK(kw, kx)   do { \
		kx = _mm_xor_si128(kw, _mm_shuffle_epi32(kw, 0x0E)); \
	} while (0)

/*
 * Combine two 64-bit values (k0:k1) into a 128-bit (kw) value and
 * the XOR of the two values (kx).
 */
#define PBK(k0, k1, kw, kx)   do { \
		kw = _mm_unpacklo_epi64(k1, k0); \
		kx = _mm_xor_si128(k0, k1); \
	} while (0)

/*
 * Left-shift by 1 bit a 256-bit value (in four 64-bit words).
 */
#define SL_256(x0, x1, x2, x3)   do { \
		x0 = _mm_or_si128( \
			_mm_slli_epi64(x0, 1), \
			_mm_srli_epi64(x1, 63)); \
		x1 = _mm_or_si128( \
			_mm_slli_epi64(x1, 1), \
			_mm_srli_epi64(x2, 63)); \
		x2 = _mm_or_si128( \
			_mm_slli_epi64(x2, 1), \
			_mm_srli_epi64(x3, 63)); \
		x3 = _mm_slli_epi64(x3, 1); \
	} while (0)

/*
 * Perform reduction in GF(2^128). The 256-bit value is in x0..x3;
 * result is written in x0..x1.
 */
#define REDUCE_F128(x0, x1, x2, x3)   do { \
		x1 = _mm_xor_si128( \
			x1, \
			_mm_xor_si128( \
				_mm_xor_si128( \
					x3, \
					_mm_srli_epi64(x3, 1)), \
				_mm_xor_si128( \
					_mm_srli_epi64(x3, 2), \
					_mm_srli_epi64(x3, 7)))); \
		x2 = _mm_xor_si128( \
			_mm_xor_si128( \
				x2, \
				_mm_slli_epi64(x3, 63)), \
			_mm_xor_si128( \
				_mm_slli_epi64(x3, 62), \
				_mm_slli_epi64(x3, 57))); \
		x0 = _mm_xor_si128( \
			x0, \
			_mm_xor_si128( \
				_mm_xor_si128( \
					x2, \
					_mm_srli_epi64(x2, 1)), \
				_mm_xor_si128( \
					_mm_srli_epi64(x2, 2), \
					_mm_srli_epi64(x2, 7)))); \
		x1 = _mm_xor_si128( \
			_mm_xor_si128( \
				x1, \
				_mm_slli_epi64(x2, 63)), \
			_mm_xor_si128( \
				_mm_slli_epi64(x2, 62), \
				_mm_slli_epi64(x2, 57))); \
	} while (0)

/*
 * Square value kw into (dw,dx).
 */
#define SQUARE_F128(kw, dw, dx)   do { \
		__m128i z0, z1, z2, z3; \
		z1 = pclmulqdq11(kw, kw); \
		z3 = pclmulqdq00(kw, kw); \
		z0 = _mm_shuffle_epi32(z1, 0x0E); \
		z2 = _mm_shuffle_epi32(z3, 0x0E); \
		SL_256(z0, z1, z2, z3); \
		REDUCE_F128(z0, z1, z2, z3); \
		PBK(z0, z1, dw, dx); \
	} while (0)

/* see bearssl_hash.h */
BR_TARGET("ssse3,pclmul")
void
br_ghash_pclmul(void *y, const void *h, const void *data, size_t len)
{
	const unsigned char *buf1, *buf2;
	unsigned char tmp[64];
	size_t num4, num1;
	__m128i yw, h1w, h1x;
	BYTESWAP_DECL

	/*
	 * We split data into two chunks. First chunk starts at buf1
	 * and contains num4 blocks of 64-byte values. Second chunk
	 * starts at buf2 and contains num1 blocks of 16-byte values.
	 * We want the first chunk to be as large as possible.
	 */
	buf1 = data;
	num4 = len >> 6;
	len &= 63;
	buf2 = buf1 + (num4 << 6);
	num1 = (len + 15) >> 4;
	if ((len & 15) != 0) {
		memcpy(tmp, buf2, len);
		memset(tmp + len, 0, (num1 << 4) - len);
		buf2 = tmp;
	}

	/*
	 * Preparatory step for endian conversions.
	 */
	BYTESWAP_PREP;

	/*
	 * Load y and h.
	 */
	yw = _mm_loadu_si128(y);
	h1w = _mm_loadu_si128(h);
	BYTESWAP(yw);
	BYTESWAP(h1w);
	BK(h1w, h1x);

	if (num4 > 0) {
		__m128i h2w, h2x, h3w, h3x, h4w, h4x;
		__m128i t0, t1, t2, t3;

		/*
		 * Compute h2 = h^2.
		 */
		SQUARE_F128(h1w, h2w, h2x);

		/*
		 * Compute h3 = h^3 = h*(h^2).
		 */
		t1 = pclmulqdq11(h1w, h2w);
		t3 = pclmulqdq00(h1w, h2w);
		t2 = _mm_xor_si128(pclmulqdq00(h1x, h2x),
			_mm_xor_si128(t1, t3));
		t0 = _mm_shuffle_epi32(t1, 0x0E);
		t1 = _mm_xor_si128(t1, _mm_shuffle_epi32(t2, 0x0E));
		t2 = _mm_xor_si128(t2, _mm_shuffle_epi32(t3, 0x0E));
		SL_256(t0, t1, t2, t3);
		REDUCE_F128(t0, t1, t2, t3);
		PBK(t0, t1, h3w, h3x);

		/*
		 * Compute h4 = h^4 = (h^2)^2.
		 */
		SQUARE_F128(h2w, h4w, h4x);

		while (num4 -- > 0) {
			__m128i aw0, aw1, aw2, aw3;
			__m128i ax0, ax1, ax2, ax3;

			aw0 = _mm_loadu_si128((void *)(buf1 +  0));
			aw1 = _mm_loadu_si128((void *)(buf1 + 16));
			aw2 = _mm_loadu_si128((void *)(buf1 + 32));
			aw3 = _mm_loadu_si128((void *)(buf1 + 48));
			BYTESWAP(aw0);
			BYTESWAP(aw1);
			BYTESWAP(aw2);
			BYTESWAP(aw3);
			buf1 += 64;

			aw0 = _mm_xor_si128(aw0, yw);
			BK(aw1, ax1);
			BK(aw2, ax2);
			BK(aw3, ax3);
			BK(aw0, ax0);

			t1 = _mm_xor_si128(
				_mm_xor_si128(
					pclmulqdq11(aw0, h4w),
					pclmulqdq11(aw1, h3w)),
				_mm_xor_si128(
					pclmulqdq11(aw2, h2w),
					pclmulqdq11(aw3, h1w)));
			t3 = _mm_xor_si128(
				_mm_xor_si128(
					pclmulqdq00(aw0, h4w),
					pclmulqdq00(aw1, h3w)),
				_mm_xor_si128(
					pclmulqdq00(aw2, h2w),
					pclmulqdq00(aw3, h1w)));
			t2 = _mm_xor_si128(
				_mm_xor_si128(
					pclmulqdq00(ax0, h4x),
					pclmulqdq00(ax1, h3x)),
				_mm_xor_si128(
					pclmulqdq00(ax2, h2x),
					pclmulqdq00(ax3, h1x)));
			t2 = _mm_xor_si128(t2, _mm_xor_si128(t1, t3));
			t0 = _mm_shuffle_epi32(t1, 0x0E);
			t1 = _mm_xor_si128(t1, _mm_shuffle_epi32(t2, 0x0E));
			t2 = _mm_xor_si128(t2, _mm_shuffle_epi32(t3, 0x0E));
			SL_256(t0, t1, t2, t3);
			REDUCE_F128(t0, t1, t2, t3);
			yw = _mm_unpacklo_epi64(t1, t0);
		}
	}

	while (num1 -- > 0) {
		__m128i aw, ax;
		__m128i t0, t1, t2, t3;

		aw = _mm_loadu_si128((void *)buf2);
		BYTESWAP(aw);
		buf2 += 16;

		aw = _mm_xor_si128(aw, yw);
		BK(aw, ax);

		t1 = pclmulqdq11(aw, h1w);
		t3 = pclmulqdq00(aw, h1w);
		t2 = pclmulqdq00(ax, h1x);
		t2 = _mm_xor_si128(t2, _mm_xor_si128(t1, t3));
		t0 = _mm_shuffle_epi32(t1, 0x0E);
		t1 = _mm_xor_si128(t1, _mm_shuffle_epi32(t2, 0x0E));
		t2 = _mm_xor_si128(t2, _mm_shuffle_epi32(t3, 0x0E));
		SL_256(t0, t1, t2, t3);
		REDUCE_F128(t0, t1, t2, t3);
		yw = _mm_unpacklo_epi64(t1, t0);
	}

	BYTESWAP(yw);
	_mm_storeu_si128(y, yw);
}

BR_TARGETS_X86_DOWN

#else

/* see bearssl_hash.h */
br_ghash
br_ghash_pclmul_get(void)
{
	return 0;
}

#endif
