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
 * This code contains the AES key schedule implementation using the
 * AES-NI opcodes.
 */

#if BR_AES_X86NI

/* see inner.h */
int
br_aes_x86ni_supported(void)
{
	/*
	 * Bit mask for features in ECX:
	 *   19   SSE4.1 (used for _mm_insert_epi32(), for AES-CTR)
	 *   25   AES-NI
	 */
	return br_cpuid(0, 0, 0x02080000, 0);
}

BR_TARGETS_X86_UP

BR_TARGET("sse2,aes")
static inline __m128i
expand_step128(__m128i k, __m128i k2)
{
	k = _mm_xor_si128(k, _mm_slli_si128(k, 4));
	k = _mm_xor_si128(k, _mm_slli_si128(k, 4));
	k = _mm_xor_si128(k, _mm_slli_si128(k, 4));
	k2 = _mm_shuffle_epi32(k2, 0xFF);
	return _mm_xor_si128(k, k2);
}

BR_TARGET("sse2,aes")
static inline void
expand_step192(__m128i *t1, __m128i *t2, __m128i *t3)
{
	__m128i t4;

	*t2 = _mm_shuffle_epi32(*t2, 0x55);
	t4 = _mm_slli_si128(*t1, 0x4);
	*t1 = _mm_xor_si128(*t1, t4);
	t4 = _mm_slli_si128(t4, 0x4);
	*t1 = _mm_xor_si128(*t1, t4);
	t4 = _mm_slli_si128(t4, 0x4);
	*t1 = _mm_xor_si128(*t1, t4);
	*t1 = _mm_xor_si128(*t1, *t2);
	*t2 = _mm_shuffle_epi32(*t1, 0xFF);
	t4 = _mm_slli_si128(*t3, 0x4);
	*t3 = _mm_xor_si128(*t3, t4);
	*t3 = _mm_xor_si128(*t3, *t2);
}

BR_TARGET("sse2,aes")
static inline void
expand_step256_1(__m128i *t1, __m128i *t2)
{
	__m128i t4;

	*t2 = _mm_shuffle_epi32(*t2, 0xFF);
	t4 = _mm_slli_si128(*t1, 0x4);
	*t1 = _mm_xor_si128(*t1, t4);
	t4 = _mm_slli_si128(t4, 0x4);
	*t1 = _mm_xor_si128(*t1, t4);
	t4 = _mm_slli_si128(t4, 0x4);
	*t1 = _mm_xor_si128(*t1, t4);
	*t1 = _mm_xor_si128(*t1, *t2);
}

BR_TARGET("sse2,aes")
static inline void
expand_step256_2(__m128i *t1, __m128i *t3)
{
	__m128i t2, t4;

	t4 = _mm_aeskeygenassist_si128(*t1, 0x0);
	t2 = _mm_shuffle_epi32(t4, 0xAA);
	t4 = _mm_slli_si128(*t3, 0x4);
	*t3 = _mm_xor_si128(*t3, t4);
	t4 = _mm_slli_si128(t4, 0x4);
	*t3 = _mm_xor_si128(*t3, t4);
	t4 = _mm_slli_si128(t4, 0x4);
	*t3 = _mm_xor_si128(*t3, t4);
	*t3 = _mm_xor_si128(*t3, t2);
}

/*
 * Perform key schedule for AES, encryption direction. Subkeys are written
 * in sk[], and the number of rounds is returned. Key length MUST be 16,
 * 24 or 32 bytes.
 */
BR_TARGET("sse2,aes")
static unsigned
x86ni_keysched(__m128i *sk, const void *key, size_t len)
{
	const unsigned char *kb;

#define KEXP128(k, i, rcon)   do { \
		k = expand_step128(k, _mm_aeskeygenassist_si128(k, rcon)); \
		sk[i] = k; \
	} while (0)

#define KEXP192(i, rcon1, rcon2)   do { \
		sk[(i) + 0] = t1; \
		sk[(i) + 1] = t3; \
		t2 = _mm_aeskeygenassist_si128(t3, rcon1); \
		expand_step192(&t1, &t2, &t3); \
		sk[(i) + 1] = _mm_castpd_si128(_mm_shuffle_pd( \
			_mm_castsi128_pd(sk[(i) + 1]), \
			_mm_castsi128_pd(t1), 0)); \
		sk[(i) + 2] = _mm_castpd_si128(_mm_shuffle_pd( \
			_mm_castsi128_pd(t1), \
			_mm_castsi128_pd(t3), 1)); \
		t2 = _mm_aeskeygenassist_si128(t3, rcon2); \
		expand_step192(&t1, &t2, &t3); \
	} while (0)

#define KEXP256(i, rcon)   do { \
		sk[(i) + 0] = t3; \
		t2 = _mm_aeskeygenassist_si128(t3, rcon); \
		expand_step256_1(&t1, &t2); \
		sk[(i) + 1] = t1; \
		expand_step256_2(&t1, &t3); \
	} while (0)

	kb = key;
	switch (len) {
		__m128i t1, t2, t3;

	case 16:
		t1 = _mm_loadu_si128((const void *)kb);
		sk[0] = t1;
		KEXP128(t1,  1, 0x01);
		KEXP128(t1,  2, 0x02);
		KEXP128(t1,  3, 0x04);
		KEXP128(t1,  4, 0x08);
		KEXP128(t1,  5, 0x10);
		KEXP128(t1,  6, 0x20);
		KEXP128(t1,  7, 0x40);
		KEXP128(t1,  8, 0x80);
		KEXP128(t1,  9, 0x1B);
		KEXP128(t1, 10, 0x36);
		return 10;

	case 24:
		t1 = _mm_loadu_si128((const void *)kb);
		t3 = _mm_loadu_si128((const void *)(kb + 8));
		t3 = _mm_shuffle_epi32(t3, 0x4E);
		KEXP192(0, 0x01, 0x02);
		KEXP192(3, 0x04, 0x08);
		KEXP192(6, 0x10, 0x20);
		KEXP192(9, 0x40, 0x80);
		sk[12] = t1;
		return 12;

	case 32:
		t1 = _mm_loadu_si128((const void *)kb);
		t3 = _mm_loadu_si128((const void *)(kb + 16));
		sk[0] = t1;
		KEXP256( 1, 0x01);
		KEXP256( 3, 0x02);
		KEXP256( 5, 0x04);
		KEXP256( 7, 0x08);
		KEXP256( 9, 0x10);
		KEXP256(11, 0x20);
		sk[13] = t3;
		t2 = _mm_aeskeygenassist_si128(t3, 0x40);
		expand_step256_1(&t1, &t2);
		sk[14] = t1;
		return 14;

	default:
		return 0;
	}

#undef KEXP128
#undef KEXP192
#undef KEXP256
}

/* see inner.h */
BR_TARGET("sse2,aes")
unsigned
br_aes_x86ni_keysched_enc(unsigned char *skni, const void *key, size_t len)
{
	__m128i sk[15];
	unsigned num_rounds;

	num_rounds = x86ni_keysched(sk, key, len);
	memcpy(skni, sk, (num_rounds + 1) << 4);
	return num_rounds;
}

/* see inner.h */
BR_TARGET("sse2,aes")
unsigned
br_aes_x86ni_keysched_dec(unsigned char *skni, const void *key, size_t len)
{
	__m128i sk[15];
	unsigned u, num_rounds;

	num_rounds = x86ni_keysched(sk, key, len);
	_mm_storeu_si128((void *)skni, sk[num_rounds]);
	for (u = 1; u < num_rounds; u ++) {
		_mm_storeu_si128((void *)(skni + (u << 4)),
			_mm_aesimc_si128(sk[num_rounds - u]));
	}
	_mm_storeu_si128((void *)(skni + (num_rounds << 4)), sk[0]);
	return num_rounds;
}

BR_TARGETS_X86_DOWN

#endif
