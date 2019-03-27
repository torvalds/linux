/*===---- avx512vlbitalgintrin.h - BITALG intrinsics -----------------------===
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __IMMINTRIN_H
#error "Never use <avx512vlbitalgintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512VLBITALGINTRIN_H
#define __AVX512VLBITALGINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS128 __attribute__((__always_inline__, __nodebug__, __target__("avx512vl,avx512bitalg"), __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS256 __attribute__((__always_inline__, __nodebug__, __target__("avx512vl,avx512bitalg"), __min_vector_width__(256)))

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_popcnt_epi16(__m256i __A)
{
  return (__m256i) __builtin_ia32_vpopcntw_256((__v16hi) __A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_popcnt_epi16(__m256i __A, __mmask16 __U, __m256i __B)
{
  return (__m256i) __builtin_ia32_selectw_256((__mmask16) __U,
              (__v16hi) _mm256_popcnt_epi16(__B),
              (__v16hi) __A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_popcnt_epi16(__mmask16 __U, __m256i __B)
{
  return _mm256_mask_popcnt_epi16((__m256i) _mm256_setzero_si256(),
              __U,
              __B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_popcnt_epi16(__m128i __A)
{
  return (__m128i) __builtin_ia32_vpopcntw_128((__v8hi) __A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_popcnt_epi16(__m128i __A, __mmask8 __U, __m128i __B)
{
  return (__m128i) __builtin_ia32_selectw_128((__mmask8) __U,
              (__v8hi) _mm_popcnt_epi16(__B),
              (__v8hi) __A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_popcnt_epi16(__mmask8 __U, __m128i __B)
{
  return _mm_mask_popcnt_epi16((__m128i) _mm_setzero_si128(),
              __U,
              __B);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_popcnt_epi8(__m256i __A)
{
  return (__m256i) __builtin_ia32_vpopcntb_256((__v32qi) __A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_popcnt_epi8(__m256i __A, __mmask32 __U, __m256i __B)
{
  return (__m256i) __builtin_ia32_selectb_256((__mmask32) __U,
              (__v32qi) _mm256_popcnt_epi8(__B),
              (__v32qi) __A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_popcnt_epi8(__mmask32 __U, __m256i __B)
{
  return _mm256_mask_popcnt_epi8((__m256i) _mm256_setzero_si256(),
              __U,
              __B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_popcnt_epi8(__m128i __A)
{
  return (__m128i) __builtin_ia32_vpopcntb_128((__v16qi) __A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_popcnt_epi8(__m128i __A, __mmask16 __U, __m128i __B)
{
  return (__m128i) __builtin_ia32_selectb_128((__mmask16) __U,
              (__v16qi) _mm_popcnt_epi8(__B),
              (__v16qi) __A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_popcnt_epi8(__mmask16 __U, __m128i __B)
{
  return _mm_mask_popcnt_epi8((__m128i) _mm_setzero_si128(),
              __U,
              __B);
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS256
_mm256_mask_bitshuffle_epi64_mask(__mmask32 __U, __m256i __A, __m256i __B)
{
  return (__mmask32) __builtin_ia32_vpshufbitqmb256_mask((__v32qi) __A,
              (__v32qi) __B,
              __U);
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS256
_mm256_bitshuffle_epi64_mask(__m256i __A, __m256i __B)
{
  return _mm256_mask_bitshuffle_epi64_mask((__mmask32) -1,
              __A,
              __B);
}

static __inline__ __mmask16 __DEFAULT_FN_ATTRS128
_mm_mask_bitshuffle_epi64_mask(__mmask16 __U, __m128i __A, __m128i __B)
{
  return (__mmask16) __builtin_ia32_vpshufbitqmb128_mask((__v16qi) __A,
              (__v16qi) __B,
              __U);
}

static __inline__ __mmask16 __DEFAULT_FN_ATTRS128
_mm_bitshuffle_epi64_mask(__m128i __A, __m128i __B)
{
  return _mm_mask_bitshuffle_epi64_mask((__mmask16) -1,
              __A,
              __B);
}


#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256

#endif
