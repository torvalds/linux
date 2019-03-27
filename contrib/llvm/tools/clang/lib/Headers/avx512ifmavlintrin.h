/*===------------- avx512ifmavlintrin.h - IFMA intrinsics ------------------===
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
#error "Never use <avx512ifmavlintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __IFMAVLINTRIN_H
#define __IFMAVLINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS128 __attribute__((__always_inline__, __nodebug__, __target__("avx512ifma,avx512vl"), __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS256 __attribute__((__always_inline__, __nodebug__, __target__("avx512ifma,avx512vl"), __min_vector_width__(256)))



static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_madd52hi_epu64 (__m128i __X, __m128i __Y, __m128i __Z)
{
  return (__m128i)__builtin_ia32_vpmadd52huq128((__v2di) __X, (__v2di) __Y,
                                                (__v2di) __Z);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_madd52hi_epu64 (__m128i __W, __mmask8 __M, __m128i __X, __m128i __Y)
{
  return (__m128i)__builtin_ia32_selectq_128(__M,
                                      (__v2di)_mm_madd52hi_epu64(__W, __X, __Y),
                                      (__v2di)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_madd52hi_epu64 (__mmask8 __M, __m128i __X, __m128i __Y, __m128i __Z)
{
  return (__m128i)__builtin_ia32_selectq_128(__M,
                                      (__v2di)_mm_madd52hi_epu64(__X, __Y, __Z),
                                      (__v2di)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_madd52hi_epu64 (__m256i __X, __m256i __Y, __m256i __Z)
{
  return (__m256i)__builtin_ia32_vpmadd52huq256((__v4di)__X, (__v4di)__Y,
                                                (__v4di)__Z);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_madd52hi_epu64 (__m256i __W, __mmask8 __M, __m256i __X, __m256i __Y)
{
  return (__m256i)__builtin_ia32_selectq_256(__M,
                                   (__v4di)_mm256_madd52hi_epu64(__W, __X, __Y),
                                   (__v4di)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_madd52hi_epu64 (__mmask8 __M, __m256i __X, __m256i __Y, __m256i __Z)
{
  return (__m256i)__builtin_ia32_selectq_256(__M,
                                   (__v4di)_mm256_madd52hi_epu64(__X, __Y, __Z),
                                   (__v4di)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_madd52lo_epu64 (__m128i __X, __m128i __Y, __m128i __Z)
{
  return (__m128i)__builtin_ia32_vpmadd52luq128((__v2di)__X, (__v2di)__Y,
                                                (__v2di)__Z);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_madd52lo_epu64 (__m128i __W, __mmask8 __M, __m128i __X, __m128i __Y)
{
  return (__m128i)__builtin_ia32_selectq_128(__M,
                                      (__v2di)_mm_madd52lo_epu64(__W, __X, __Y),
                                      (__v2di)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_madd52lo_epu64 (__mmask8 __M, __m128i __X, __m128i __Y, __m128i __Z)
{
  return (__m128i)__builtin_ia32_selectq_128(__M,
                                      (__v2di)_mm_madd52lo_epu64(__X, __Y, __Z),
                                      (__v2di)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_madd52lo_epu64 (__m256i __X, __m256i __Y, __m256i __Z)
{
  return (__m256i)__builtin_ia32_vpmadd52luq256((__v4di)__X, (__v4di)__Y,
                                                (__v4di)__Z);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_madd52lo_epu64 (__m256i __W, __mmask8 __M, __m256i __X, __m256i __Y)
{
  return (__m256i)__builtin_ia32_selectq_256(__M,
                                   (__v4di)_mm256_madd52lo_epu64(__W, __X, __Y),
                                   (__v4di)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_madd52lo_epu64 (__mmask8 __M, __m256i __X, __m256i __Y, __m256i __Z)
{
  return (__m256i)__builtin_ia32_selectq_256(__M,
                                   (__v4di)_mm256_madd52lo_epu64(__X, __Y, __Z),
                                   (__v4di)_mm256_setzero_si256());
}


#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256

#endif
