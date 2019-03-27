/*===------------- avx512vlvbmi2intrin.h - VBMI2 intrinsics -----------------===
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
#error "Never use <avx512vlvbmi2intrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512VLVBMI2INTRIN_H
#define __AVX512VLVBMI2INTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS128 __attribute__((__always_inline__, __nodebug__, __target__("avx512vl,avx512vbmi2"), __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS256 __attribute__((__always_inline__, __nodebug__, __target__("avx512vl,avx512vbmi2"), __min_vector_width__(256)))

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_compress_epi16(__m128i __S, __mmask8 __U, __m128i __D)
{
  return (__m128i) __builtin_ia32_compresshi128_mask ((__v8hi) __D,
              (__v8hi) __S,
              __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_compress_epi16(__mmask8 __U, __m128i __D)
{
  return (__m128i) __builtin_ia32_compresshi128_mask ((__v8hi) __D,
              (__v8hi) _mm_setzero_si128(),
              __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_compress_epi8(__m128i __S, __mmask16 __U, __m128i __D)
{
  return (__m128i) __builtin_ia32_compressqi128_mask ((__v16qi) __D,
              (__v16qi) __S,
              __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_compress_epi8(__mmask16 __U, __m128i __D)
{
  return (__m128i) __builtin_ia32_compressqi128_mask ((__v16qi) __D,
              (__v16qi) _mm_setzero_si128(),
              __U);
}

static __inline__ void __DEFAULT_FN_ATTRS128
_mm_mask_compressstoreu_epi16(void *__P, __mmask8 __U, __m128i __D)
{
  __builtin_ia32_compressstorehi128_mask ((__v8hi *) __P, (__v8hi) __D,
              __U);
}

static __inline__ void __DEFAULT_FN_ATTRS128
_mm_mask_compressstoreu_epi8(void *__P, __mmask16 __U, __m128i __D)
{
  __builtin_ia32_compressstoreqi128_mask ((__v16qi *) __P, (__v16qi) __D,
              __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_expand_epi16(__m128i __S, __mmask8 __U, __m128i __D)
{
  return (__m128i) __builtin_ia32_expandhi128_mask ((__v8hi) __D,
              (__v8hi) __S,
              __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_expand_epi16(__mmask8 __U, __m128i __D)
{
  return (__m128i) __builtin_ia32_expandhi128_mask ((__v8hi) __D,
              (__v8hi) _mm_setzero_si128(),
              __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_expand_epi8(__m128i __S, __mmask16 __U, __m128i __D)
{
  return (__m128i) __builtin_ia32_expandqi128_mask ((__v16qi) __D,
              (__v16qi) __S,
              __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_expand_epi8(__mmask16 __U, __m128i __D)
{
  return (__m128i) __builtin_ia32_expandqi128_mask ((__v16qi) __D,
              (__v16qi) _mm_setzero_si128(),
              __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_expandloadu_epi16(__m128i __S, __mmask8 __U, void const *__P)
{
  return (__m128i) __builtin_ia32_expandloadhi128_mask ((const __v8hi *)__P,
              (__v8hi) __S,
              __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_expandloadu_epi16(__mmask8 __U, void const *__P)
{
  return (__m128i) __builtin_ia32_expandloadhi128_mask ((const __v8hi *)__P,
              (__v8hi) _mm_setzero_si128(),
              __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_expandloadu_epi8(__m128i __S, __mmask16 __U, void const *__P)
{
  return (__m128i) __builtin_ia32_expandloadqi128_mask ((const __v16qi *)__P,
              (__v16qi) __S,
              __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_expandloadu_epi8(__mmask16 __U, void const *__P)
{
  return (__m128i) __builtin_ia32_expandloadqi128_mask ((const __v16qi *)__P,
              (__v16qi) _mm_setzero_si128(),
              __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_compress_epi16(__m256i __S, __mmask16 __U, __m256i __D)
{
  return (__m256i) __builtin_ia32_compresshi256_mask ((__v16hi) __D,
              (__v16hi) __S,
              __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_compress_epi16(__mmask16 __U, __m256i __D)
{
  return (__m256i) __builtin_ia32_compresshi256_mask ((__v16hi) __D,
              (__v16hi) _mm256_setzero_si256(),
              __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_compress_epi8(__m256i __S, __mmask32 __U, __m256i __D)
{
  return (__m256i) __builtin_ia32_compressqi256_mask ((__v32qi) __D,
              (__v32qi) __S,
              __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_compress_epi8(__mmask32 __U, __m256i __D)
{
  return (__m256i) __builtin_ia32_compressqi256_mask ((__v32qi) __D,
              (__v32qi) _mm256_setzero_si256(),
              __U);
}

static __inline__ void __DEFAULT_FN_ATTRS256
_mm256_mask_compressstoreu_epi16(void *__P, __mmask16 __U, __m256i __D)
{
  __builtin_ia32_compressstorehi256_mask ((__v16hi *) __P, (__v16hi) __D,
              __U);
}

static __inline__ void __DEFAULT_FN_ATTRS256
_mm256_mask_compressstoreu_epi8(void *__P, __mmask32 __U, __m256i __D)
{
  __builtin_ia32_compressstoreqi256_mask ((__v32qi *) __P, (__v32qi) __D,
              __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_expand_epi16(__m256i __S, __mmask16 __U, __m256i __D)
{
  return (__m256i) __builtin_ia32_expandhi256_mask ((__v16hi) __D,
              (__v16hi) __S,
              __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_expand_epi16(__mmask16 __U, __m256i __D)
{
  return (__m256i) __builtin_ia32_expandhi256_mask ((__v16hi) __D,
              (__v16hi) _mm256_setzero_si256(),
              __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_expand_epi8(__m256i __S, __mmask32 __U, __m256i __D)
{
  return (__m256i) __builtin_ia32_expandqi256_mask ((__v32qi) __D,
              (__v32qi) __S,
              __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_expand_epi8(__mmask32 __U, __m256i __D)
{
  return (__m256i) __builtin_ia32_expandqi256_mask ((__v32qi) __D,
              (__v32qi) _mm256_setzero_si256(),
              __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_expandloadu_epi16(__m256i __S, __mmask16 __U, void const *__P)
{
  return (__m256i) __builtin_ia32_expandloadhi256_mask ((const __v16hi *)__P,
              (__v16hi) __S,
              __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_expandloadu_epi16(__mmask16 __U, void const *__P)
{
  return (__m256i) __builtin_ia32_expandloadhi256_mask ((const __v16hi *)__P,
              (__v16hi) _mm256_setzero_si256(),
              __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_expandloadu_epi8(__m256i __S, __mmask32 __U, void const *__P)
{
  return (__m256i) __builtin_ia32_expandloadqi256_mask ((const __v32qi *)__P,
              (__v32qi) __S,
              __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_expandloadu_epi8(__mmask32 __U, void const *__P)
{
  return (__m256i) __builtin_ia32_expandloadqi256_mask ((const __v32qi *)__P,
              (__v32qi) _mm256_setzero_si256(),
              __U);
}

#define _mm256_shldi_epi64(A, B, I) \
  (__m256i)__builtin_ia32_vpshldq256((__v4di)(__m256i)(A), \
                                     (__v4di)(__m256i)(B), (int)(I))

#define _mm256_mask_shldi_epi64(S, U, A, B, I) \
  (__m256i)__builtin_ia32_selectq_256((__mmask8)(U), \
                                    (__v4di)_mm256_shldi_epi64((A), (B), (I)), \
                                    (__v4di)(__m256i)(S))

#define _mm256_maskz_shldi_epi64(U, A, B, I) \
  (__m256i)__builtin_ia32_selectq_256((__mmask8)(U), \
                                    (__v4di)_mm256_shldi_epi64((A), (B), (I)), \
                                    (__v4di)_mm256_setzero_si256())

#define _mm_shldi_epi64(A, B, I) \
  (__m128i)__builtin_ia32_vpshldq128((__v2di)(__m128i)(A), \
                                     (__v2di)(__m128i)(B), (int)(I))

#define _mm_mask_shldi_epi64(S, U, A, B, I) \
  (__m128i)__builtin_ia32_selectq_128((__mmask8)(U), \
                                      (__v2di)_mm_shldi_epi64((A), (B), (I)), \
                                      (__v2di)(__m128i)(S))

#define _mm_maskz_shldi_epi64(U, A, B, I) \
  (__m128i)__builtin_ia32_selectq_128((__mmask8)(U), \
                                      (__v2di)_mm_shldi_epi64((A), (B), (I)), \
                                      (__v2di)_mm_setzero_si128())

#define _mm256_shldi_epi32(A, B, I) \
  (__m256i)__builtin_ia32_vpshldd256((__v8si)(__m256i)(A), \
                                     (__v8si)(__m256i)(B), (int)(I))

#define _mm256_mask_shldi_epi32(S, U, A, B, I) \
  (__m256i)__builtin_ia32_selectd_256((__mmask8)(U), \
                                    (__v8si)_mm256_shldi_epi32((A), (B), (I)), \
                                    (__v8si)(__m256i)(S))

#define _mm256_maskz_shldi_epi32(U, A, B, I) \
  (__m256i)__builtin_ia32_selectd_256((__mmask8)(U), \
                                    (__v8si)_mm256_shldi_epi32((A), (B), (I)), \
                                    (__v8si)_mm256_setzero_si256())

#define _mm_shldi_epi32(A, B, I) \
  (__m128i)__builtin_ia32_vpshldd128((__v4si)(__m128i)(A), \
                                     (__v4si)(__m128i)(B), (int)(I))

#define _mm_mask_shldi_epi32(S, U, A, B, I) \
  (__m128i)__builtin_ia32_selectd_128((__mmask8)(U), \
                                      (__v4si)_mm_shldi_epi32((A), (B), (I)), \
                                      (__v4si)(__m128i)(S))

#define _mm_maskz_shldi_epi32(U, A, B, I) \
  (__m128i)__builtin_ia32_selectd_128((__mmask8)(U), \
                                      (__v4si)_mm_shldi_epi32((A), (B), (I)), \
                                      (__v4si)_mm_setzero_si128())

#define _mm256_shldi_epi16(A, B, I) \
  (__m256i)__builtin_ia32_vpshldw256((__v16hi)(__m256i)(A), \
                                     (__v16hi)(__m256i)(B), (int)(I))

#define _mm256_mask_shldi_epi16(S, U, A, B, I) \
  (__m256i)__builtin_ia32_selectw_256((__mmask16)(U), \
                                   (__v16hi)_mm256_shldi_epi16((A), (B), (I)), \
                                   (__v16hi)(__m256i)(S))

#define _mm256_maskz_shldi_epi16(U, A, B, I) \
  (__m256i)__builtin_ia32_selectw_256((__mmask16)(U), \
                                   (__v16hi)_mm256_shldi_epi16((A), (B), (I)), \
                                   (__v16hi)_mm256_setzero_si256())

#define _mm_shldi_epi16(A, B, I) \
  (__m128i)__builtin_ia32_vpshldw128((__v8hi)(__m128i)(A), \
                                     (__v8hi)(__m128i)(B), (int)(I))

#define _mm_mask_shldi_epi16(S, U, A, B, I) \
  (__m128i)__builtin_ia32_selectw_128((__mmask8)(U), \
                                      (__v8hi)_mm_shldi_epi16((A), (B), (I)), \
                                      (__v8hi)(__m128i)(S))

#define _mm_maskz_shldi_epi16(U, A, B, I) \
  (__m128i)__builtin_ia32_selectw_128((__mmask8)(U), \
                                      (__v8hi)_mm_shldi_epi16((A), (B), (I)), \
                                      (__v8hi)_mm_setzero_si128())

#define _mm256_shrdi_epi64(A, B, I) \
  (__m256i)__builtin_ia32_vpshrdq256((__v4di)(__m256i)(A), \
                                     (__v4di)(__m256i)(B), (int)(I))

#define _mm256_mask_shrdi_epi64(S, U, A, B, I) \
  (__m256i)__builtin_ia32_selectq_256((__mmask8)(U), \
                                    (__v4di)_mm256_shrdi_epi64((A), (B), (I)), \
                                    (__v4di)(__m256i)(S))

#define _mm256_maskz_shrdi_epi64(U, A, B, I) \
  (__m256i)__builtin_ia32_selectq_256((__mmask8)(U), \
                                    (__v4di)_mm256_shrdi_epi64((A), (B), (I)), \
                                    (__v4di)_mm256_setzero_si256())

#define _mm_shrdi_epi64(A, B, I) \
  (__m128i)__builtin_ia32_vpshrdq128((__v2di)(__m128i)(A), \
                                     (__v2di)(__m128i)(B), (int)(I))

#define _mm_mask_shrdi_epi64(S, U, A, B, I) \
  (__m128i)__builtin_ia32_selectq_128((__mmask8)(U), \
                                      (__v2di)_mm_shrdi_epi64((A), (B), (I)), \
                                      (__v2di)(__m128i)(S))

#define _mm_maskz_shrdi_epi64(U, A, B, I) \
  (__m128i)__builtin_ia32_selectq_128((__mmask8)(U), \
                                      (__v2di)_mm_shrdi_epi64((A), (B), (I)), \
                                      (__v2di)_mm_setzero_si128())

#define _mm256_shrdi_epi32(A, B, I) \
  (__m256i)__builtin_ia32_vpshrdd256((__v8si)(__m256i)(A), \
                                     (__v8si)(__m256i)(B), (int)(I))

#define _mm256_mask_shrdi_epi32(S, U, A, B, I) \
  (__m256i)__builtin_ia32_selectd_256((__mmask8)(U), \
                                    (__v8si)_mm256_shrdi_epi32((A), (B), (I)), \
                                    (__v8si)(__m256i)(S))

#define _mm256_maskz_shrdi_epi32(U, A, B, I) \
  (__m256i)__builtin_ia32_selectd_256((__mmask8)(U), \
                                    (__v8si)_mm256_shrdi_epi32((A), (B), (I)), \
                                    (__v8si)_mm256_setzero_si256())

#define _mm_shrdi_epi32(A, B, I) \
  (__m128i)__builtin_ia32_vpshrdd128((__v4si)(__m128i)(A), \
                                     (__v4si)(__m128i)(B), (int)(I))

#define _mm_mask_shrdi_epi32(S, U, A, B, I) \
  (__m128i)__builtin_ia32_selectd_128((__mmask8)(U), \
                                      (__v4si)_mm_shrdi_epi32((A), (B), (I)), \
                                      (__v4si)(__m128i)(S))

#define _mm_maskz_shrdi_epi32(U, A, B, I) \
  (__m128i)__builtin_ia32_selectd_128((__mmask8)(U), \
                                      (__v4si)_mm_shrdi_epi32((A), (B), (I)), \
                                      (__v4si)_mm_setzero_si128())

#define _mm256_shrdi_epi16(A, B, I) \
  (__m256i)__builtin_ia32_vpshrdw256((__v16hi)(__m256i)(A), \
                                     (__v16hi)(__m256i)(B), (int)(I))

#define _mm256_mask_shrdi_epi16(S, U, A, B, I) \
  (__m256i)__builtin_ia32_selectw_256((__mmask16)(U), \
                                   (__v16hi)_mm256_shrdi_epi16((A), (B), (I)), \
                                   (__v16hi)(__m256i)(S))

#define _mm256_maskz_shrdi_epi16(U, A, B, I) \
  (__m256i)__builtin_ia32_selectw_256((__mmask16)(U), \
                                   (__v16hi)_mm256_shrdi_epi16((A), (B), (I)), \
                                   (__v16hi)_mm256_setzero_si256())

#define _mm_shrdi_epi16(A, B, I) \
  (__m128i)__builtin_ia32_vpshrdw128((__v8hi)(__m128i)(A), \
                                     (__v8hi)(__m128i)(B), (int)(I))

#define _mm_mask_shrdi_epi16(S, U, A, B, I) \
  (__m128i)__builtin_ia32_selectw_128((__mmask8)(U), \
                                      (__v8hi)_mm_shrdi_epi16((A), (B), (I)), \
                                      (__v8hi)(__m128i)(S))

#define _mm_maskz_shrdi_epi16(U, A, B, I) \
  (__m128i)__builtin_ia32_selectw_128((__mmask8)(U), \
                                      (__v8hi)_mm_shrdi_epi16((A), (B), (I)), \
                                      (__v8hi)_mm_setzero_si128())

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_shldv_epi64(__m256i __A, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_vpshldvq256((__v4di)__A, (__v4di)__B,
                                             (__v4di)__C);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_shldv_epi64(__m256i __A, __mmask8 __U, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_selectq_256(__U,
                                      (__v4di)_mm256_shldv_epi64(__A, __B, __C),
                                      (__v4di)__A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_shldv_epi64(__mmask8 __U, __m256i __A, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_selectq_256(__U,
                                      (__v4di)_mm256_shldv_epi64(__A, __B, __C),
                                      (__v4di)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_shldv_epi64(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpshldvq128((__v2di)__A, (__v2di)__B,
                                             (__v2di)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_shldv_epi64(__m128i __A, __mmask8 __U, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_selectq_128(__U,
                                         (__v2di)_mm_shldv_epi64(__A, __B, __C),
                                         (__v2di)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_shldv_epi64(__mmask8 __U, __m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_selectq_128(__U,
                                         (__v2di)_mm_shldv_epi64(__A, __B, __C),
                                         (__v2di)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_shldv_epi32(__m256i __A, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_vpshldvd256((__v8si)__A, (__v8si)__B,
                                             (__v8si)__C);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_shldv_epi32(__m256i __A, __mmask8 __U, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_selectd_256(__U,
                                      (__v8si)_mm256_shldv_epi32(__A, __B, __C),
                                      (__v8si)__A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_shldv_epi32(__mmask8 __U, __m256i __A, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_selectd_256(__U,
                                      (__v8si)_mm256_shldv_epi32(__A, __B, __C),
                                      (__v8si)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_shldv_epi32(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpshldvd128((__v4si)__A, (__v4si)__B,
                                             (__v4si)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_shldv_epi32(__m128i __A, __mmask8 __U, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_selectd_128(__U,
                                         (__v4si)_mm_shldv_epi32(__A, __B, __C),
                                         (__v4si)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_shldv_epi32(__mmask8 __U, __m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_selectd_128(__U,
                                         (__v4si)_mm_shldv_epi32(__A, __B, __C),
                                         (__v4si)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_shldv_epi16(__m256i __A, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_vpshldvw256((__v16hi)__A, (__v16hi)__B,
                                             (__v16hi)__C);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_shldv_epi16(__m256i __A, __mmask16 __U, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_selectw_256(__U,
                                      (__v16hi)_mm256_shldv_epi16(__A, __B, __C),
                                      (__v16hi)__A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_shldv_epi16(__mmask16 __U, __m256i __A, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_selectw_256(__U,
                                      (__v16hi)_mm256_shldv_epi16(__A, __B, __C),
                                      (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_shldv_epi16(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpshldvw128((__v8hi)__A, (__v8hi)__B,
                                             (__v8hi)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_shldv_epi16(__m128i __A, __mmask8 __U, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_selectw_128(__U,
                                         (__v8hi)_mm_shldv_epi16(__A, __B, __C),
                                         (__v8hi)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_shldv_epi16(__mmask8 __U, __m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_selectw_128(__U,
                                         (__v8hi)_mm_shldv_epi16(__A, __B, __C),
                                         (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_shrdv_epi64(__m256i __A, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_vpshrdvq256((__v4di)__A, (__v4di)__B,
                                             (__v4di)__C);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_shrdv_epi64(__m256i __A, __mmask8 __U, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_selectq_256(__U,
                                      (__v4di)_mm256_shrdv_epi64(__A, __B, __C),
                                      (__v4di)__A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_shrdv_epi64(__mmask8 __U, __m256i __A, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_selectq_256(__U,
                                      (__v4di)_mm256_shrdv_epi64(__A, __B, __C),
                                      (__v4di)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_shrdv_epi64(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpshrdvq128((__v2di)__A, (__v2di)__B,
                                             (__v2di)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_shrdv_epi64(__m128i __A, __mmask8 __U, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_selectq_128(__U,
                                         (__v2di)_mm_shrdv_epi64(__A, __B, __C),
                                         (__v2di)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_shrdv_epi64(__mmask8 __U, __m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_selectq_128(__U,
                                         (__v2di)_mm_shrdv_epi64(__A, __B, __C),
                                         (__v2di)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_shrdv_epi32(__m256i __A, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_vpshrdvd256((__v8si)__A, (__v8si)__B,
                                             (__v8si)__C);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_shrdv_epi32(__m256i __A, __mmask8 __U, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_selectd_256(__U,
                                      (__v8si)_mm256_shrdv_epi32(__A, __B, __C),
                                      (__v8si)__A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_shrdv_epi32(__mmask8 __U, __m256i __A, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_selectd_256(__U,
                                      (__v8si)_mm256_shrdv_epi32(__A, __B, __C),
                                      (__v8si)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_shrdv_epi32(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpshrdvd128((__v4si)__A, (__v4si)__B,
                                             (__v4si)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_shrdv_epi32(__m128i __A, __mmask8 __U, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_selectd_128(__U,
                                         (__v4si)_mm_shrdv_epi32(__A, __B, __C),
                                         (__v4si)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_shrdv_epi32(__mmask8 __U, __m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_selectd_128(__U,
                                         (__v4si)_mm_shrdv_epi32(__A, __B, __C),
                                         (__v4si)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_shrdv_epi16(__m256i __A, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_vpshrdvw256((__v16hi)__A, (__v16hi)__B,
                                             (__v16hi)__C);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_shrdv_epi16(__m256i __A, __mmask16 __U, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_selectw_256(__U,
                                     (__v16hi)_mm256_shrdv_epi16(__A, __B, __C),
                                     (__v16hi)__A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_shrdv_epi16(__mmask16 __U, __m256i __A, __m256i __B, __m256i __C)
{
  return (__m256i)__builtin_ia32_selectw_256(__U,
                                     (__v16hi)_mm256_shrdv_epi16(__A, __B, __C),
                                     (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_shrdv_epi16(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpshrdvw128((__v8hi)__A, (__v8hi)__B,
                                             (__v8hi)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_shrdv_epi16(__m128i __A, __mmask8 __U, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_selectw_128(__U,
                                         (__v8hi)_mm_shrdv_epi16(__A, __B, __C),
                                         (__v8hi)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_shrdv_epi16(__mmask8 __U, __m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_selectw_128(__U,
                                         (__v8hi)_mm_shrdv_epi16(__A, __B, __C),
                                         (__v8hi)_mm_setzero_si128());
}


#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256

#endif
