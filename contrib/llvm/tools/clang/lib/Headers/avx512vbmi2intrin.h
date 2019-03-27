/*===------------- avx512vbmi2intrin.h - VBMI2 intrinsics ------------------===
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
#error "Never use <avx512vbmi2intrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512VBMI2INTRIN_H
#define __AVX512VBMI2INTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("avx512vbmi2"), __min_vector_width__(512)))


static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_compress_epi16(__m512i __S, __mmask32 __U, __m512i __D)
{
  return (__m512i) __builtin_ia32_compresshi512_mask ((__v32hi) __D,
              (__v32hi) __S,
              __U);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_compress_epi16(__mmask32 __U, __m512i __D)
{
  return (__m512i) __builtin_ia32_compresshi512_mask ((__v32hi) __D,
              (__v32hi) _mm512_setzero_si512(),
              __U);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_compress_epi8(__m512i __S, __mmask64 __U, __m512i __D)
{
  return (__m512i) __builtin_ia32_compressqi512_mask ((__v64qi) __D,
              (__v64qi) __S,
              __U);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_compress_epi8(__mmask64 __U, __m512i __D)
{
  return (__m512i) __builtin_ia32_compressqi512_mask ((__v64qi) __D,
              (__v64qi) _mm512_setzero_si512(),
              __U);
}

static __inline__ void __DEFAULT_FN_ATTRS
_mm512_mask_compressstoreu_epi16(void *__P, __mmask32 __U, __m512i __D)
{
  __builtin_ia32_compressstorehi512_mask ((__v32hi *) __P, (__v32hi) __D,
              __U);
}

static __inline__ void __DEFAULT_FN_ATTRS
_mm512_mask_compressstoreu_epi8(void *__P, __mmask64 __U, __m512i __D)
{
  __builtin_ia32_compressstoreqi512_mask ((__v64qi *) __P, (__v64qi) __D,
              __U);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_expand_epi16(__m512i __S, __mmask32 __U, __m512i __D)
{
  return (__m512i) __builtin_ia32_expandhi512_mask ((__v32hi) __D,
              (__v32hi) __S,
              __U);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_expand_epi16(__mmask32 __U, __m512i __D)
{
  return (__m512i) __builtin_ia32_expandhi512_mask ((__v32hi) __D,
              (__v32hi) _mm512_setzero_si512(),
              __U);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_expand_epi8(__m512i __S, __mmask64 __U, __m512i __D)
{
  return (__m512i) __builtin_ia32_expandqi512_mask ((__v64qi) __D,
              (__v64qi) __S,
              __U);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_expand_epi8(__mmask64 __U, __m512i __D)
{
  return (__m512i) __builtin_ia32_expandqi512_mask ((__v64qi) __D,
              (__v64qi) _mm512_setzero_si512(),
              __U);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_expandloadu_epi16(__m512i __S, __mmask32 __U, void const *__P)
{
  return (__m512i) __builtin_ia32_expandloadhi512_mask ((const __v32hi *)__P,
              (__v32hi) __S,
              __U);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_expandloadu_epi16(__mmask32 __U, void const *__P)
{
  return (__m512i) __builtin_ia32_expandloadhi512_mask ((const __v32hi *)__P,
              (__v32hi) _mm512_setzero_si512(),
              __U);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_expandloadu_epi8(__m512i __S, __mmask64 __U, void const *__P)
{
  return (__m512i) __builtin_ia32_expandloadqi512_mask ((const __v64qi *)__P,
              (__v64qi) __S,
              __U);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_expandloadu_epi8(__mmask64 __U, void const *__P)
{
  return (__m512i) __builtin_ia32_expandloadqi512_mask ((const __v64qi *)__P,
              (__v64qi) _mm512_setzero_si512(),
              __U);
}

#define _mm512_shldi_epi64(A, B, I) \
  (__m512i)__builtin_ia32_vpshldq512((__v8di)(__m512i)(A), \
                                     (__v8di)(__m512i)(B), (int)(I))

#define _mm512_mask_shldi_epi64(S, U, A, B, I) \
  (__m512i)__builtin_ia32_selectq_512((__mmask8)(U), \
                                    (__v8di)_mm512_shldi_epi64((A), (B), (I)), \
                                    (__v8di)(__m512i)(S))

#define _mm512_maskz_shldi_epi64(U, A, B, I) \
  (__m512i)__builtin_ia32_selectq_512((__mmask8)(U), \
                                    (__v8di)_mm512_shldi_epi64((A), (B), (I)), \
                                    (__v8di)_mm512_setzero_si512())

#define _mm512_shldi_epi32(A, B, I) \
  (__m512i)__builtin_ia32_vpshldd512((__v16si)(__m512i)(A), \
                                     (__v16si)(__m512i)(B), (int)(I))

#define _mm512_mask_shldi_epi32(S, U, A, B, I) \
  (__m512i)__builtin_ia32_selectd_512((__mmask16)(U), \
                                   (__v16si)_mm512_shldi_epi32((A), (B), (I)), \
                                   (__v16si)(__m512i)(S))

#define _mm512_maskz_shldi_epi32(U, A, B, I) \
  (__m512i)__builtin_ia32_selectd_512((__mmask16)(U), \
                                   (__v16si)_mm512_shldi_epi32((A), (B), (I)), \
                                   (__v16si)_mm512_setzero_si512())

#define _mm512_shldi_epi16(A, B, I) \
  (__m512i)__builtin_ia32_vpshldw512((__v32hi)(__m512i)(A), \
                                     (__v32hi)(__m512i)(B), (int)(I))

#define _mm512_mask_shldi_epi16(S, U, A, B, I) \
  (__m512i)__builtin_ia32_selectw_512((__mmask32)(U), \
                                   (__v32hi)_mm512_shldi_epi16((A), (B), (I)), \
                                   (__v32hi)(__m512i)(S))

#define _mm512_maskz_shldi_epi16(U, A, B, I) \
  (__m512i)__builtin_ia32_selectw_512((__mmask32)(U), \
                                   (__v32hi)_mm512_shldi_epi16((A), (B), (I)), \
                                   (__v32hi)_mm512_setzero_si512())

#define _mm512_shrdi_epi64(A, B, I) \
  (__m512i)__builtin_ia32_vpshrdq512((__v8di)(__m512i)(A), \
                                     (__v8di)(__m512i)(B), (int)(I))

#define _mm512_mask_shrdi_epi64(S, U, A, B, I) \
  (__m512i)__builtin_ia32_selectq_512((__mmask8)(U), \
                                    (__v8di)_mm512_shrdi_epi64((A), (B), (I)), \
                                    (__v8di)(__m512i)(S))

#define _mm512_maskz_shrdi_epi64(U, A, B, I) \
  (__m512i)__builtin_ia32_selectq_512((__mmask8)(U), \
                                    (__v8di)_mm512_shrdi_epi64((A), (B), (I)), \
                                    (__v8di)_mm512_setzero_si512())

#define _mm512_shrdi_epi32(A, B, I) \
  (__m512i)__builtin_ia32_vpshrdd512((__v16si)(__m512i)(A), \
                                     (__v16si)(__m512i)(B), (int)(I))

#define _mm512_mask_shrdi_epi32(S, U, A, B, I) \
  (__m512i)__builtin_ia32_selectd_512((__mmask16)(U), \
                                   (__v16si)_mm512_shrdi_epi32((A), (B), (I)), \
                                   (__v16si)(__m512i)(S))

#define _mm512_maskz_shrdi_epi32(U, A, B, I) \
  (__m512i)__builtin_ia32_selectd_512((__mmask16)(U), \
                                   (__v16si)_mm512_shrdi_epi32((A), (B), (I)), \
                                   (__v16si)_mm512_setzero_si512())

#define _mm512_shrdi_epi16(A, B, I) \
  (__m512i)__builtin_ia32_vpshrdw512((__v32hi)(__m512i)(A), \
                                     (__v32hi)(__m512i)(B), (int)(I))

#define _mm512_mask_shrdi_epi16(S, U, A, B, I) \
  (__m512i)__builtin_ia32_selectw_512((__mmask32)(U), \
                                   (__v32hi)_mm512_shrdi_epi16((A), (B), (I)), \
                                   (__v32hi)(__m512i)(S))

#define _mm512_maskz_shrdi_epi16(U, A, B, I) \
  (__m512i)__builtin_ia32_selectw_512((__mmask32)(U), \
                                   (__v32hi)_mm512_shrdi_epi16((A), (B), (I)), \
                                   (__v32hi)_mm512_setzero_si512())

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_shldv_epi64(__m512i __A, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_vpshldvq512((__v8di)__A, (__v8di)__B,
                                             (__v8di)__C);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_shldv_epi64(__m512i __A, __mmask8 __U, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_selectq_512(__U,
                                      (__v8di)_mm512_shldv_epi64(__A, __B, __C),
                                      (__v8di)__A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_shldv_epi64(__mmask8 __U, __m512i __A, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_selectq_512(__U,
                                      (__v8di)_mm512_shldv_epi64(__A, __B, __C),
                                      (__v8di)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_shldv_epi32(__m512i __A, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_vpshldvd512((__v16si)__A, (__v16si)__B,
                                             (__v16si)__C);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_shldv_epi32(__m512i __A, __mmask16 __U, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_selectd_512(__U,
                                     (__v16si)_mm512_shldv_epi32(__A, __B, __C),
                                     (__v16si)__A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_shldv_epi32(__mmask16 __U, __m512i __A, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_selectd_512(__U,
                                     (__v16si)_mm512_shldv_epi32(__A, __B, __C),
                                     (__v16si)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_shldv_epi16(__m512i __A, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_vpshldvw512((__v32hi)__A, (__v32hi)__B,
                                             (__v32hi)__C);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_shldv_epi16(__m512i __A, __mmask32 __U, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_selectw_512(__U,
                                     (__v32hi)_mm512_shldv_epi16(__A, __B, __C),
                                     (__v32hi)__A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_shldv_epi16(__mmask32 __U, __m512i __A, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_selectw_512(__U,
                                     (__v32hi)_mm512_shldv_epi16(__A, __B, __C),
                                     (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_shrdv_epi64(__m512i __A, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_vpshrdvq512((__v8di)__A, (__v8di)__B,
                                             (__v8di)__C);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_shrdv_epi64(__m512i __A, __mmask8 __U, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_selectq_512(__U,
                                      (__v8di)_mm512_shrdv_epi64(__A, __B, __C),
                                      (__v8di)__A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_shrdv_epi64(__mmask8 __U, __m512i __A, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_selectq_512(__U,
                                      (__v8di)_mm512_shrdv_epi64(__A, __B, __C),
                                      (__v8di)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_shrdv_epi32(__m512i __A, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_vpshrdvd512((__v16si)__A, (__v16si)__B,
                                             (__v16si)__C);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_shrdv_epi32(__m512i __A, __mmask16 __U, __m512i __B, __m512i __C)
{
  return (__m512i) __builtin_ia32_selectd_512(__U,
                                     (__v16si)_mm512_shrdv_epi32(__A, __B, __C),
                                     (__v16si)__A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_shrdv_epi32(__mmask16 __U, __m512i __A, __m512i __B, __m512i __C)
{
  return (__m512i) __builtin_ia32_selectd_512(__U,
                                     (__v16si)_mm512_shrdv_epi32(__A, __B, __C),
                                     (__v16si)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_shrdv_epi16(__m512i __A, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_vpshrdvw512((__v32hi)__A, (__v32hi)__B,
                                             (__v32hi)__C);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_shrdv_epi16(__m512i __A, __mmask32 __U, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_selectw_512(__U,
                                     (__v32hi)_mm512_shrdv_epi16(__A, __B, __C),
                                     (__v32hi)__A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_shrdv_epi16(__mmask32 __U, __m512i __A, __m512i __B, __m512i __C)
{
  return (__m512i)__builtin_ia32_selectw_512(__U,
                                     (__v32hi)_mm512_shrdv_epi16(__A, __B, __C),
                                     (__v32hi)_mm512_setzero_si512());
}


#undef __DEFAULT_FN_ATTRS

#endif

