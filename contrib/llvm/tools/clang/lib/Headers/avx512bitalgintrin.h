/*===------------- avx512bitalgintrin.h - BITALG intrinsics ------------------===
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
#error "Never use <avx512bitalgintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512BITALGINTRIN_H
#define __AVX512BITALGINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("avx512bitalg"), __min_vector_width__(512)))

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_popcnt_epi16(__m512i __A)
{
  return (__m512i) __builtin_ia32_vpopcntw_512((__v32hi) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_popcnt_epi16(__m512i __A, __mmask32 __U, __m512i __B)
{
  return (__m512i) __builtin_ia32_selectw_512((__mmask32) __U,
              (__v32hi) _mm512_popcnt_epi16(__B),
              (__v32hi) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_popcnt_epi16(__mmask32 __U, __m512i __B)
{
  return _mm512_mask_popcnt_epi16((__m512i) _mm512_setzero_si512(),
              __U,
              __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_popcnt_epi8(__m512i __A)
{
  return (__m512i) __builtin_ia32_vpopcntb_512((__v64qi) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_popcnt_epi8(__m512i __A, __mmask64 __U, __m512i __B)
{
  return (__m512i) __builtin_ia32_selectb_512((__mmask64) __U,
              (__v64qi) _mm512_popcnt_epi8(__B),
              (__v64qi) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_popcnt_epi8(__mmask64 __U, __m512i __B)
{
  return _mm512_mask_popcnt_epi8((__m512i) _mm512_setzero_si512(),
              __U,
              __B);
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS
_mm512_mask_bitshuffle_epi64_mask(__mmask64 __U, __m512i __A, __m512i __B)
{
  return (__mmask64) __builtin_ia32_vpshufbitqmb512_mask((__v64qi) __A,
              (__v64qi) __B,
              __U);
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS
_mm512_bitshuffle_epi64_mask(__m512i __A, __m512i __B)
{
  return _mm512_mask_bitshuffle_epi64_mask((__mmask64) -1,
              __A,
              __B);
}


#undef __DEFAULT_FN_ATTRS

#endif
