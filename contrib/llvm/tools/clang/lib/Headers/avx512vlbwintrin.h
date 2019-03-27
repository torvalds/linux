/*===---- avx512vlbwintrin.h - AVX512VL and AVX512BW intrinsics ------------===
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
#error "Never use <avx512vlbwintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512VLBWINTRIN_H
#define __AVX512VLBWINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS128 __attribute__((__always_inline__, __nodebug__, __target__("avx512vl,avx512bw"), __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS256 __attribute__((__always_inline__, __nodebug__, __target__("avx512vl,avx512bw"), __min_vector_width__(256)))

/* Integer compare */

#define _mm_cmp_epi8_mask(a, b, p) \
  (__mmask16)__builtin_ia32_cmpb128_mask((__v16qi)(__m128i)(a), \
                                         (__v16qi)(__m128i)(b), (int)(p), \
                                         (__mmask16)-1)

#define _mm_mask_cmp_epi8_mask(m, a, b, p) \
  (__mmask16)__builtin_ia32_cmpb128_mask((__v16qi)(__m128i)(a), \
                                         (__v16qi)(__m128i)(b), (int)(p), \
                                         (__mmask16)(m))

#define _mm_cmp_epu8_mask(a, b, p) \
  (__mmask16)__builtin_ia32_ucmpb128_mask((__v16qi)(__m128i)(a), \
                                          (__v16qi)(__m128i)(b), (int)(p), \
                                          (__mmask16)-1)

#define _mm_mask_cmp_epu8_mask(m, a, b, p) \
  (__mmask16)__builtin_ia32_ucmpb128_mask((__v16qi)(__m128i)(a), \
                                          (__v16qi)(__m128i)(b), (int)(p), \
                                          (__mmask16)(m))

#define _mm256_cmp_epi8_mask(a, b, p) \
  (__mmask32)__builtin_ia32_cmpb256_mask((__v32qi)(__m256i)(a), \
                                         (__v32qi)(__m256i)(b), (int)(p), \
                                         (__mmask32)-1)

#define _mm256_mask_cmp_epi8_mask(m, a, b, p) \
  (__mmask32)__builtin_ia32_cmpb256_mask((__v32qi)(__m256i)(a), \
                                         (__v32qi)(__m256i)(b), (int)(p), \
                                         (__mmask32)(m))

#define _mm256_cmp_epu8_mask(a, b, p) \
  (__mmask32)__builtin_ia32_ucmpb256_mask((__v32qi)(__m256i)(a), \
                                          (__v32qi)(__m256i)(b), (int)(p), \
                                          (__mmask32)-1)

#define _mm256_mask_cmp_epu8_mask(m, a, b, p) \
  (__mmask32)__builtin_ia32_ucmpb256_mask((__v32qi)(__m256i)(a), \
                                          (__v32qi)(__m256i)(b), (int)(p), \
                                          (__mmask32)(m))

#define _mm_cmp_epi16_mask(a, b, p) \
  (__mmask8)__builtin_ia32_cmpw128_mask((__v8hi)(__m128i)(a), \
                                        (__v8hi)(__m128i)(b), (int)(p), \
                                        (__mmask8)-1)

#define _mm_mask_cmp_epi16_mask(m, a, b, p) \
  (__mmask8)__builtin_ia32_cmpw128_mask((__v8hi)(__m128i)(a), \
                                        (__v8hi)(__m128i)(b), (int)(p), \
                                        (__mmask8)(m))

#define _mm_cmp_epu16_mask(a, b, p) \
  (__mmask8)__builtin_ia32_ucmpw128_mask((__v8hi)(__m128i)(a), \
                                         (__v8hi)(__m128i)(b), (int)(p), \
                                         (__mmask8)-1)

#define _mm_mask_cmp_epu16_mask(m, a, b, p) \
  (__mmask8)__builtin_ia32_ucmpw128_mask((__v8hi)(__m128i)(a), \
                                         (__v8hi)(__m128i)(b), (int)(p), \
                                         (__mmask8)(m))

#define _mm256_cmp_epi16_mask(a, b, p) \
  (__mmask16)__builtin_ia32_cmpw256_mask((__v16hi)(__m256i)(a), \
                                         (__v16hi)(__m256i)(b), (int)(p), \
                                         (__mmask16)-1)

#define _mm256_mask_cmp_epi16_mask(m, a, b, p) \
  (__mmask16)__builtin_ia32_cmpw256_mask((__v16hi)(__m256i)(a), \
                                         (__v16hi)(__m256i)(b), (int)(p), \
                                         (__mmask16)(m))

#define _mm256_cmp_epu16_mask(a, b, p) \
  (__mmask16)__builtin_ia32_ucmpw256_mask((__v16hi)(__m256i)(a), \
                                          (__v16hi)(__m256i)(b), (int)(p), \
                                          (__mmask16)-1)

#define _mm256_mask_cmp_epu16_mask(m, a, b, p) \
  (__mmask16)__builtin_ia32_ucmpw256_mask((__v16hi)(__m256i)(a), \
                                          (__v16hi)(__m256i)(b), (int)(p), \
                                          (__mmask16)(m))

#define _mm_cmpeq_epi8_mask(A, B) \
    _mm_cmp_epi8_mask((A), (B), _MM_CMPINT_EQ)
#define _mm_mask_cmpeq_epi8_mask(k, A, B) \
    _mm_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_EQ)
#define _mm_cmpge_epi8_mask(A, B) \
    _mm_cmp_epi8_mask((A), (B), _MM_CMPINT_GE)
#define _mm_mask_cmpge_epi8_mask(k, A, B) \
    _mm_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_GE)
#define _mm_cmpgt_epi8_mask(A, B) \
    _mm_cmp_epi8_mask((A), (B), _MM_CMPINT_GT)
#define _mm_mask_cmpgt_epi8_mask(k, A, B) \
    _mm_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_GT)
#define _mm_cmple_epi8_mask(A, B) \
    _mm_cmp_epi8_mask((A), (B), _MM_CMPINT_LE)
#define _mm_mask_cmple_epi8_mask(k, A, B) \
    _mm_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_LE)
#define _mm_cmplt_epi8_mask(A, B) \
    _mm_cmp_epi8_mask((A), (B), _MM_CMPINT_LT)
#define _mm_mask_cmplt_epi8_mask(k, A, B) \
    _mm_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_LT)
#define _mm_cmpneq_epi8_mask(A, B) \
    _mm_cmp_epi8_mask((A), (B), _MM_CMPINT_NE)
#define _mm_mask_cmpneq_epi8_mask(k, A, B) \
    _mm_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_NE)

#define _mm256_cmpeq_epi8_mask(A, B) \
    _mm256_cmp_epi8_mask((A), (B), _MM_CMPINT_EQ)
#define _mm256_mask_cmpeq_epi8_mask(k, A, B) \
    _mm256_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_EQ)
#define _mm256_cmpge_epi8_mask(A, B) \
    _mm256_cmp_epi8_mask((A), (B), _MM_CMPINT_GE)
#define _mm256_mask_cmpge_epi8_mask(k, A, B) \
    _mm256_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_GE)
#define _mm256_cmpgt_epi8_mask(A, B) \
    _mm256_cmp_epi8_mask((A), (B), _MM_CMPINT_GT)
#define _mm256_mask_cmpgt_epi8_mask(k, A, B) \
    _mm256_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_GT)
#define _mm256_cmple_epi8_mask(A, B) \
    _mm256_cmp_epi8_mask((A), (B), _MM_CMPINT_LE)
#define _mm256_mask_cmple_epi8_mask(k, A, B) \
    _mm256_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_LE)
#define _mm256_cmplt_epi8_mask(A, B) \
    _mm256_cmp_epi8_mask((A), (B), _MM_CMPINT_LT)
#define _mm256_mask_cmplt_epi8_mask(k, A, B) \
    _mm256_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_LT)
#define _mm256_cmpneq_epi8_mask(A, B) \
    _mm256_cmp_epi8_mask((A), (B), _MM_CMPINT_NE)
#define _mm256_mask_cmpneq_epi8_mask(k, A, B) \
    _mm256_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_NE)

#define _mm_cmpeq_epu8_mask(A, B) \
    _mm_cmp_epu8_mask((A), (B), _MM_CMPINT_EQ)
#define _mm_mask_cmpeq_epu8_mask(k, A, B) \
    _mm_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_EQ)
#define _mm_cmpge_epu8_mask(A, B) \
    _mm_cmp_epu8_mask((A), (B), _MM_CMPINT_GE)
#define _mm_mask_cmpge_epu8_mask(k, A, B) \
    _mm_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_GE)
#define _mm_cmpgt_epu8_mask(A, B) \
    _mm_cmp_epu8_mask((A), (B), _MM_CMPINT_GT)
#define _mm_mask_cmpgt_epu8_mask(k, A, B) \
    _mm_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_GT)
#define _mm_cmple_epu8_mask(A, B) \
    _mm_cmp_epu8_mask((A), (B), _MM_CMPINT_LE)
#define _mm_mask_cmple_epu8_mask(k, A, B) \
    _mm_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_LE)
#define _mm_cmplt_epu8_mask(A, B) \
    _mm_cmp_epu8_mask((A), (B), _MM_CMPINT_LT)
#define _mm_mask_cmplt_epu8_mask(k, A, B) \
    _mm_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_LT)
#define _mm_cmpneq_epu8_mask(A, B) \
    _mm_cmp_epu8_mask((A), (B), _MM_CMPINT_NE)
#define _mm_mask_cmpneq_epu8_mask(k, A, B) \
    _mm_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_NE)

#define _mm256_cmpeq_epu8_mask(A, B) \
    _mm256_cmp_epu8_mask((A), (B), _MM_CMPINT_EQ)
#define _mm256_mask_cmpeq_epu8_mask(k, A, B) \
    _mm256_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_EQ)
#define _mm256_cmpge_epu8_mask(A, B) \
    _mm256_cmp_epu8_mask((A), (B), _MM_CMPINT_GE)
#define _mm256_mask_cmpge_epu8_mask(k, A, B) \
    _mm256_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_GE)
#define _mm256_cmpgt_epu8_mask(A, B) \
    _mm256_cmp_epu8_mask((A), (B), _MM_CMPINT_GT)
#define _mm256_mask_cmpgt_epu8_mask(k, A, B) \
    _mm256_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_GT)
#define _mm256_cmple_epu8_mask(A, B) \
    _mm256_cmp_epu8_mask((A), (B), _MM_CMPINT_LE)
#define _mm256_mask_cmple_epu8_mask(k, A, B) \
    _mm256_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_LE)
#define _mm256_cmplt_epu8_mask(A, B) \
    _mm256_cmp_epu8_mask((A), (B), _MM_CMPINT_LT)
#define _mm256_mask_cmplt_epu8_mask(k, A, B) \
    _mm256_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_LT)
#define _mm256_cmpneq_epu8_mask(A, B) \
    _mm256_cmp_epu8_mask((A), (B), _MM_CMPINT_NE)
#define _mm256_mask_cmpneq_epu8_mask(k, A, B) \
    _mm256_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_NE)

#define _mm_cmpeq_epi16_mask(A, B) \
    _mm_cmp_epi16_mask((A), (B), _MM_CMPINT_EQ)
#define _mm_mask_cmpeq_epi16_mask(k, A, B) \
    _mm_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_EQ)
#define _mm_cmpge_epi16_mask(A, B) \
    _mm_cmp_epi16_mask((A), (B), _MM_CMPINT_GE)
#define _mm_mask_cmpge_epi16_mask(k, A, B) \
    _mm_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_GE)
#define _mm_cmpgt_epi16_mask(A, B) \
    _mm_cmp_epi16_mask((A), (B), _MM_CMPINT_GT)
#define _mm_mask_cmpgt_epi16_mask(k, A, B) \
    _mm_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_GT)
#define _mm_cmple_epi16_mask(A, B) \
    _mm_cmp_epi16_mask((A), (B), _MM_CMPINT_LE)
#define _mm_mask_cmple_epi16_mask(k, A, B) \
    _mm_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_LE)
#define _mm_cmplt_epi16_mask(A, B) \
    _mm_cmp_epi16_mask((A), (B), _MM_CMPINT_LT)
#define _mm_mask_cmplt_epi16_mask(k, A, B) \
    _mm_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_LT)
#define _mm_cmpneq_epi16_mask(A, B) \
    _mm_cmp_epi16_mask((A), (B), _MM_CMPINT_NE)
#define _mm_mask_cmpneq_epi16_mask(k, A, B) \
    _mm_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_NE)

#define _mm256_cmpeq_epi16_mask(A, B) \
    _mm256_cmp_epi16_mask((A), (B), _MM_CMPINT_EQ)
#define _mm256_mask_cmpeq_epi16_mask(k, A, B) \
    _mm256_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_EQ)
#define _mm256_cmpge_epi16_mask(A, B) \
    _mm256_cmp_epi16_mask((A), (B), _MM_CMPINT_GE)
#define _mm256_mask_cmpge_epi16_mask(k, A, B) \
    _mm256_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_GE)
#define _mm256_cmpgt_epi16_mask(A, B) \
    _mm256_cmp_epi16_mask((A), (B), _MM_CMPINT_GT)
#define _mm256_mask_cmpgt_epi16_mask(k, A, B) \
    _mm256_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_GT)
#define _mm256_cmple_epi16_mask(A, B) \
    _mm256_cmp_epi16_mask((A), (B), _MM_CMPINT_LE)
#define _mm256_mask_cmple_epi16_mask(k, A, B) \
    _mm256_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_LE)
#define _mm256_cmplt_epi16_mask(A, B) \
    _mm256_cmp_epi16_mask((A), (B), _MM_CMPINT_LT)
#define _mm256_mask_cmplt_epi16_mask(k, A, B) \
    _mm256_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_LT)
#define _mm256_cmpneq_epi16_mask(A, B) \
    _mm256_cmp_epi16_mask((A), (B), _MM_CMPINT_NE)
#define _mm256_mask_cmpneq_epi16_mask(k, A, B) \
    _mm256_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_NE)

#define _mm_cmpeq_epu16_mask(A, B) \
    _mm_cmp_epu16_mask((A), (B), _MM_CMPINT_EQ)
#define _mm_mask_cmpeq_epu16_mask(k, A, B) \
    _mm_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_EQ)
#define _mm_cmpge_epu16_mask(A, B) \
    _mm_cmp_epu16_mask((A), (B), _MM_CMPINT_GE)
#define _mm_mask_cmpge_epu16_mask(k, A, B) \
    _mm_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_GE)
#define _mm_cmpgt_epu16_mask(A, B) \
    _mm_cmp_epu16_mask((A), (B), _MM_CMPINT_GT)
#define _mm_mask_cmpgt_epu16_mask(k, A, B) \
    _mm_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_GT)
#define _mm_cmple_epu16_mask(A, B) \
    _mm_cmp_epu16_mask((A), (B), _MM_CMPINT_LE)
#define _mm_mask_cmple_epu16_mask(k, A, B) \
    _mm_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_LE)
#define _mm_cmplt_epu16_mask(A, B) \
    _mm_cmp_epu16_mask((A), (B), _MM_CMPINT_LT)
#define _mm_mask_cmplt_epu16_mask(k, A, B) \
    _mm_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_LT)
#define _mm_cmpneq_epu16_mask(A, B) \
    _mm_cmp_epu16_mask((A), (B), _MM_CMPINT_NE)
#define _mm_mask_cmpneq_epu16_mask(k, A, B) \
    _mm_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_NE)

#define _mm256_cmpeq_epu16_mask(A, B) \
    _mm256_cmp_epu16_mask((A), (B), _MM_CMPINT_EQ)
#define _mm256_mask_cmpeq_epu16_mask(k, A, B) \
    _mm256_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_EQ)
#define _mm256_cmpge_epu16_mask(A, B) \
    _mm256_cmp_epu16_mask((A), (B), _MM_CMPINT_GE)
#define _mm256_mask_cmpge_epu16_mask(k, A, B) \
    _mm256_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_GE)
#define _mm256_cmpgt_epu16_mask(A, B) \
    _mm256_cmp_epu16_mask((A), (B), _MM_CMPINT_GT)
#define _mm256_mask_cmpgt_epu16_mask(k, A, B) \
    _mm256_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_GT)
#define _mm256_cmple_epu16_mask(A, B) \
    _mm256_cmp_epu16_mask((A), (B), _MM_CMPINT_LE)
#define _mm256_mask_cmple_epu16_mask(k, A, B) \
    _mm256_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_LE)
#define _mm256_cmplt_epu16_mask(A, B) \
    _mm256_cmp_epu16_mask((A), (B), _MM_CMPINT_LT)
#define _mm256_mask_cmplt_epu16_mask(k, A, B) \
    _mm256_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_LT)
#define _mm256_cmpneq_epu16_mask(A, B) \
    _mm256_cmp_epu16_mask((A), (B), _MM_CMPINT_NE)
#define _mm256_mask_cmpneq_epu16_mask(k, A, B) \
    _mm256_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_NE)

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_add_epi8(__m256i __W, __mmask32 __U, __m256i __A, __m256i __B){
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                             (__v32qi)_mm256_add_epi8(__A, __B),
                                             (__v32qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_add_epi8(__mmask32 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                             (__v32qi)_mm256_add_epi8(__A, __B),
                                             (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_add_epi16(__m256i __W, __mmask16 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                             (__v16hi)_mm256_add_epi16(__A, __B),
                                             (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_add_epi16(__mmask16 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                             (__v16hi)_mm256_add_epi16(__A, __B),
                                             (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_sub_epi8(__m256i __W, __mmask32 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                             (__v32qi)_mm256_sub_epi8(__A, __B),
                                             (__v32qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_sub_epi8(__mmask32 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                             (__v32qi)_mm256_sub_epi8(__A, __B),
                                             (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_sub_epi16(__m256i __W, __mmask16 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                             (__v16hi)_mm256_sub_epi16(__A, __B),
                                             (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_sub_epi16(__mmask16 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                             (__v16hi)_mm256_sub_epi16(__A, __B),
                                             (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_add_epi8(__m128i __W, __mmask16 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_add_epi8(__A, __B),
                                             (__v16qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_add_epi8(__mmask16 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_add_epi8(__A, __B),
                                             (__v16qi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_add_epi16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_add_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_add_epi16(__mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_add_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_sub_epi8(__m128i __W, __mmask16 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_sub_epi8(__A, __B),
                                             (__v16qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_sub_epi8(__mmask16 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_sub_epi8(__A, __B),
                                             (__v16qi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_sub_epi16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_sub_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_sub_epi16(__mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_sub_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_mullo_epi16(__m256i __W, __mmask16 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                             (__v16hi)_mm256_mullo_epi16(__A, __B),
                                             (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_mullo_epi16(__mmask16 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                             (__v16hi)_mm256_mullo_epi16(__A, __B),
                                             (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_mullo_epi16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_mullo_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_mullo_epi16(__mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_mullo_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_blend_epi8 (__mmask16 __U, __m128i __A, __m128i __W)
{
  return (__m128i) __builtin_ia32_selectb_128 ((__mmask16) __U,
              (__v16qi) __W,
              (__v16qi) __A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_blend_epi8 (__mmask32 __U, __m256i __A, __m256i __W)
{
  return (__m256i) __builtin_ia32_selectb_256 ((__mmask32) __U,
               (__v32qi) __W,
               (__v32qi) __A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_blend_epi16 (__mmask8 __U, __m128i __A, __m128i __W)
{
  return (__m128i) __builtin_ia32_selectw_128 ((__mmask8) __U,
               (__v8hi) __W,
               (__v8hi) __A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_blend_epi16 (__mmask16 __U, __m256i __A, __m256i __W)
{
  return (__m256i) __builtin_ia32_selectw_256 ((__mmask16) __U,
               (__v16hi) __W,
               (__v16hi) __A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_abs_epi8(__m128i __W, __mmask16 __U, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_abs_epi8(__A),
                                             (__v16qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_abs_epi8(__mmask16 __U, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_abs_epi8(__A),
                                             (__v16qi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_abs_epi8(__m256i __W, __mmask32 __U, __m256i __A)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                             (__v32qi)_mm256_abs_epi8(__A),
                                             (__v32qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_abs_epi8 (__mmask32 __U, __m256i __A)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                             (__v32qi)_mm256_abs_epi8(__A),
                                             (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_abs_epi16(__m128i __W, __mmask8 __U, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_abs_epi16(__A),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_abs_epi16(__mmask8 __U, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_abs_epi16(__A),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_abs_epi16(__m256i __W, __mmask16 __U, __m256i __A)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                             (__v16hi)_mm256_abs_epi16(__A),
                                             (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_abs_epi16(__mmask16 __U, __m256i __A)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                             (__v16hi)_mm256_abs_epi16(__A),
                                             (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_packs_epi32(__mmask8 __M, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__M,
                                             (__v8hi)_mm_packs_epi32(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_packs_epi32(__m128i __W, __mmask8 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__M,
                                             (__v8hi)_mm_packs_epi32(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_packs_epi32(__mmask16 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__M,
                                          (__v16hi)_mm256_packs_epi32(__A, __B),
                                          (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_packs_epi32(__m256i __W, __mmask16 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__M,
                                          (__v16hi)_mm256_packs_epi32(__A, __B),
                                          (__v16hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_packs_epi16(__mmask16 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__M,
                                             (__v16qi)_mm_packs_epi16(__A, __B),
                                             (__v16qi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_packs_epi16(__m128i __W, __mmask16 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__M,
                                             (__v16qi)_mm_packs_epi16(__A, __B),
                                             (__v16qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_packs_epi16(__mmask32 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__M,
                                          (__v32qi)_mm256_packs_epi16(__A, __B),
                                          (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_packs_epi16(__m256i __W, __mmask32 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__M,
                                          (__v32qi)_mm256_packs_epi16(__A, __B),
                                          (__v32qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_packus_epi32(__mmask8 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__M,
                                             (__v8hi)_mm_packus_epi32(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_packus_epi32(__m128i __W, __mmask8 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__M,
                                             (__v8hi)_mm_packus_epi32(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_packus_epi32(__mmask16 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__M,
                                         (__v16hi)_mm256_packus_epi32(__A, __B),
                                         (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_packus_epi32(__m256i __W, __mmask16 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__M,
                                         (__v16hi)_mm256_packus_epi32(__A, __B),
                                         (__v16hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_packus_epi16(__mmask16 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__M,
                                            (__v16qi)_mm_packus_epi16(__A, __B),
                                            (__v16qi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_packus_epi16(__m128i __W, __mmask16 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__M,
                                            (__v16qi)_mm_packus_epi16(__A, __B),
                                            (__v16qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_packus_epi16(__mmask32 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__M,
                                         (__v32qi)_mm256_packus_epi16(__A, __B),
                                         (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_packus_epi16(__m256i __W, __mmask32 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__M,
                                         (__v32qi)_mm256_packus_epi16(__A, __B),
                                         (__v32qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_adds_epi8(__m128i __W, __mmask16 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_adds_epi8(__A, __B),
                                             (__v16qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_adds_epi8(__mmask16 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_adds_epi8(__A, __B),
                                             (__v16qi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_adds_epi8(__m256i __W, __mmask32 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                            (__v32qi)_mm256_adds_epi8(__A, __B),
                                            (__v32qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_adds_epi8(__mmask32 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                            (__v32qi)_mm256_adds_epi8(__A, __B),
                                            (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_adds_epi16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_adds_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_adds_epi16(__mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_adds_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_adds_epi16(__m256i __W, __mmask16 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                           (__v16hi)_mm256_adds_epi16(__A, __B),
                                           (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_adds_epi16(__mmask16 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                           (__v16hi)_mm256_adds_epi16(__A, __B),
                                           (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_adds_epu8(__m128i __W, __mmask16 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_adds_epu8(__A, __B),
                                             (__v16qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_adds_epu8(__mmask16 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_adds_epu8(__A, __B),
                                             (__v16qi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_adds_epu8(__m256i __W, __mmask32 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                            (__v32qi)_mm256_adds_epu8(__A, __B),
                                            (__v32qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_adds_epu8(__mmask32 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                            (__v32qi)_mm256_adds_epu8(__A, __B),
                                            (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_adds_epu16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_adds_epu16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_adds_epu16(__mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_adds_epu16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_adds_epu16(__m256i __W, __mmask16 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                           (__v16hi)_mm256_adds_epu16(__A, __B),
                                           (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_adds_epu16(__mmask16 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                           (__v16hi)_mm256_adds_epu16(__A, __B),
                                           (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_avg_epu8(__m128i __W, __mmask16 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_avg_epu8(__A, __B),
                                             (__v16qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_avg_epu8(__mmask16 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_avg_epu8(__A, __B),
                                             (__v16qi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_avg_epu8(__m256i __W, __mmask32 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                             (__v32qi)_mm256_avg_epu8(__A, __B),
                                             (__v32qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_avg_epu8(__mmask32 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                             (__v32qi)_mm256_avg_epu8(__A, __B),
                                             (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_avg_epu16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_avg_epu16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_avg_epu16(__mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_avg_epu16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_avg_epu16(__m256i __W, __mmask16 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                            (__v16hi)_mm256_avg_epu16(__A, __B),
                                            (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_avg_epu16(__mmask16 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                            (__v16hi)_mm256_avg_epu16(__A, __B),
                                            (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_max_epi8(__mmask16 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__M,
                                             (__v16qi)_mm_max_epi8(__A, __B),
                                             (__v16qi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_max_epi8(__m128i __W, __mmask16 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__M,
                                             (__v16qi)_mm_max_epi8(__A, __B),
                                             (__v16qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_max_epi8(__mmask32 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__M,
                                             (__v32qi)_mm256_max_epi8(__A, __B),
                                             (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_max_epi8(__m256i __W, __mmask32 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__M,
                                             (__v32qi)_mm256_max_epi8(__A, __B),
                                             (__v32qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_max_epi16(__mmask8 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__M,
                                             (__v8hi)_mm_max_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_max_epi16(__m128i __W, __mmask8 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__M,
                                             (__v8hi)_mm_max_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_max_epi16(__mmask16 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__M,
                                            (__v16hi)_mm256_max_epi16(__A, __B),
                                            (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_max_epi16(__m256i __W, __mmask16 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__M,
                                            (__v16hi)_mm256_max_epi16(__A, __B),
                                            (__v16hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_max_epu8(__mmask16 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__M,
                                             (__v16qi)_mm_max_epu8(__A, __B),
                                             (__v16qi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_max_epu8(__m128i __W, __mmask16 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__M,
                                             (__v16qi)_mm_max_epu8(__A, __B),
                                             (__v16qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_max_epu8 (__mmask32 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__M,
                                             (__v32qi)_mm256_max_epu8(__A, __B),
                                             (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_max_epu8(__m256i __W, __mmask32 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__M,
                                             (__v32qi)_mm256_max_epu8(__A, __B),
                                             (__v32qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_max_epu16(__mmask8 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__M,
                                             (__v8hi)_mm_max_epu16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_max_epu16(__m128i __W, __mmask8 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__M,
                                             (__v8hi)_mm_max_epu16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_max_epu16(__mmask16 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__M,
                                            (__v16hi)_mm256_max_epu16(__A, __B),
                                            (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_max_epu16(__m256i __W, __mmask16 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__M,
                                            (__v16hi)_mm256_max_epu16(__A, __B),
                                            (__v16hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_min_epi8(__mmask16 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__M,
                                             (__v16qi)_mm_min_epi8(__A, __B),
                                             (__v16qi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_min_epi8(__m128i __W, __mmask16 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__M,
                                             (__v16qi)_mm_min_epi8(__A, __B),
                                             (__v16qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_min_epi8(__mmask32 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__M,
                                             (__v32qi)_mm256_min_epi8(__A, __B),
                                             (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_min_epi8(__m256i __W, __mmask32 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__M,
                                             (__v32qi)_mm256_min_epi8(__A, __B),
                                             (__v32qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_min_epi16(__mmask8 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__M,
                                             (__v8hi)_mm_min_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_min_epi16(__m128i __W, __mmask8 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__M,
                                             (__v8hi)_mm_min_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_min_epi16(__mmask16 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__M,
                                            (__v16hi)_mm256_min_epi16(__A, __B),
                                            (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_min_epi16(__m256i __W, __mmask16 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__M,
                                            (__v16hi)_mm256_min_epi16(__A, __B),
                                            (__v16hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_min_epu8(__mmask16 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__M,
                                             (__v16qi)_mm_min_epu8(__A, __B),
                                             (__v16qi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_min_epu8(__m128i __W, __mmask16 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__M,
                                             (__v16qi)_mm_min_epu8(__A, __B),
                                             (__v16qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_min_epu8 (__mmask32 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__M,
                                             (__v32qi)_mm256_min_epu8(__A, __B),
                                             (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_min_epu8(__m256i __W, __mmask32 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__M,
                                             (__v32qi)_mm256_min_epu8(__A, __B),
                                             (__v32qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_min_epu16(__mmask8 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__M,
                                             (__v8hi)_mm_min_epu16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_min_epu16(__m128i __W, __mmask8 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__M,
                                             (__v8hi)_mm_min_epu16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_min_epu16(__mmask16 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__M,
                                            (__v16hi)_mm256_min_epu16(__A, __B),
                                            (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_min_epu16(__m256i __W, __mmask16 __M, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__M,
                                            (__v16hi)_mm256_min_epu16(__A, __B),
                                            (__v16hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_shuffle_epi8(__m128i __W, __mmask16 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                            (__v16qi)_mm_shuffle_epi8(__A, __B),
                                            (__v16qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_shuffle_epi8(__mmask16 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                            (__v16qi)_mm_shuffle_epi8(__A, __B),
                                            (__v16qi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_shuffle_epi8(__m256i __W, __mmask32 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                         (__v32qi)_mm256_shuffle_epi8(__A, __B),
                                         (__v32qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_shuffle_epi8(__mmask32 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                         (__v32qi)_mm256_shuffle_epi8(__A, __B),
                                         (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_subs_epi8(__m128i __W, __mmask16 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_subs_epi8(__A, __B),
                                             (__v16qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_subs_epi8(__mmask16 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_subs_epi8(__A, __B),
                                             (__v16qi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_subs_epi8(__m256i __W, __mmask32 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                            (__v32qi)_mm256_subs_epi8(__A, __B),
                                            (__v32qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_subs_epi8(__mmask32 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                            (__v32qi)_mm256_subs_epi8(__A, __B),
                                            (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_subs_epi16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_subs_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_subs_epi16(__mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_subs_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_subs_epi16(__m256i __W, __mmask16 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                           (__v16hi)_mm256_subs_epi16(__A, __B),
                                           (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_subs_epi16(__mmask16 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                           (__v16hi)_mm256_subs_epi16(__A, __B),
                                           (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_subs_epu8(__m128i __W, __mmask16 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_subs_epu8(__A, __B),
                                             (__v16qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_subs_epu8(__mmask16 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                             (__v16qi)_mm_subs_epu8(__A, __B),
                                             (__v16qi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_subs_epu8(__m256i __W, __mmask32 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                            (__v32qi)_mm256_subs_epu8(__A, __B),
                                            (__v32qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_subs_epu8(__mmask32 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                            (__v32qi)_mm256_subs_epu8(__A, __B),
                                            (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_subs_epu16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_subs_epu16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_subs_epu16(__mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_subs_epu16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_subs_epu16(__m256i __W, __mmask16 __U, __m256i __A,
      __m256i __B) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                           (__v16hi)_mm256_subs_epu16(__A, __B),
                                           (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_subs_epu16(__mmask16 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                           (__v16hi)_mm256_subs_epu16(__A, __B),
                                           (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_permutex2var_epi16(__m128i __A, __m128i __I, __m128i __B)
{
  return (__m128i)__builtin_ia32_vpermi2varhi128((__v8hi)__A, (__v8hi)__I,
                                                 (__v8hi) __B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_permutex2var_epi16(__m128i __A, __mmask8 __U, __m128i __I,
                            __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128(__U,
                                  (__v8hi)_mm_permutex2var_epi16(__A, __I, __B),
                                  (__v8hi)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask2_permutex2var_epi16(__m128i __A, __m128i __I, __mmask8 __U,
                             __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128(__U,
                                  (__v8hi)_mm_permutex2var_epi16(__A, __I, __B),
                                  (__v8hi)__I);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_permutex2var_epi16 (__mmask8 __U, __m128i __A, __m128i __I,
            __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128(__U,
                                  (__v8hi)_mm_permutex2var_epi16(__A, __I, __B),
                                  (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_permutex2var_epi16(__m256i __A, __m256i __I, __m256i __B)
{
  return (__m256i)__builtin_ia32_vpermi2varhi256((__v16hi)__A, (__v16hi)__I,
                                                 (__v16hi)__B);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_permutex2var_epi16(__m256i __A, __mmask16 __U, __m256i __I,
                               __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256(__U,
                              (__v16hi)_mm256_permutex2var_epi16(__A, __I, __B),
                              (__v16hi)__A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask2_permutex2var_epi16(__m256i __A, __m256i __I, __mmask16 __U,
                                __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256(__U,
                              (__v16hi)_mm256_permutex2var_epi16(__A, __I, __B),
                              (__v16hi)__I);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_permutex2var_epi16 (__mmask16 __U, __m256i __A, __m256i __I,
                                 __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256(__U,
                              (__v16hi)_mm256_permutex2var_epi16(__A, __I, __B),
                              (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_maddubs_epi16(__m128i __W, __mmask8 __U, __m128i __X, __m128i __Y) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                            (__v8hi)_mm_maddubs_epi16(__X, __Y),
                                            (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_maddubs_epi16(__mmask8 __U, __m128i __X, __m128i __Y) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                            (__v8hi)_mm_maddubs_epi16(__X, __Y),
                                            (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_maddubs_epi16(__m256i __W, __mmask16 __U, __m256i __X,
                          __m256i __Y) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                        (__v16hi)_mm256_maddubs_epi16(__X, __Y),
                                        (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_maddubs_epi16(__mmask16 __U, __m256i __X, __m256i __Y) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                        (__v16hi)_mm256_maddubs_epi16(__X, __Y),
                                        (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_madd_epi16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectd_128((__mmask8)__U,
                                             (__v4si)_mm_madd_epi16(__A, __B),
                                             (__v4si)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_madd_epi16(__mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectd_128((__mmask8)__U,
                                             (__v4si)_mm_madd_epi16(__A, __B),
                                             (__v4si)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_madd_epi16(__m256i __W, __mmask8 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectd_256((__mmask8)__U,
                                            (__v8si)_mm256_madd_epi16(__A, __B),
                                            (__v8si)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_madd_epi16(__mmask8 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectd_256((__mmask8)__U,
                                            (__v8si)_mm256_madd_epi16(__A, __B),
                                            (__v8si)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_cvtsepi16_epi8 (__m128i __A) {
  return (__m128i) __builtin_ia32_pmovswb128_mask ((__v8hi) __A,
               (__v16qi) _mm_setzero_si128(),
               (__mmask8) -1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvtsepi16_epi8 (__m128i __O, __mmask8 __M, __m128i __A) {
  return (__m128i) __builtin_ia32_pmovswb128_mask ((__v8hi) __A,
               (__v16qi) __O,
                __M);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvtsepi16_epi8 (__mmask8 __M, __m128i __A) {
  return (__m128i) __builtin_ia32_pmovswb128_mask ((__v8hi) __A,
               (__v16qi) _mm_setzero_si128(),
               __M);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS256
_mm256_cvtsepi16_epi8 (__m256i __A) {
  return (__m128i) __builtin_ia32_pmovswb256_mask ((__v16hi) __A,
               (__v16qi) _mm_setzero_si128(),
               (__mmask16) -1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS256
_mm256_mask_cvtsepi16_epi8 (__m128i __O, __mmask16 __M, __m256i __A) {
  return (__m128i) __builtin_ia32_pmovswb256_mask ((__v16hi) __A,
               (__v16qi) __O,
                __M);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtsepi16_epi8 (__mmask16 __M, __m256i __A) {
  return (__m128i) __builtin_ia32_pmovswb256_mask ((__v16hi) __A,
               (__v16qi) _mm_setzero_si128(),
               __M);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_cvtusepi16_epi8 (__m128i __A) {
  return (__m128i) __builtin_ia32_pmovuswb128_mask ((__v8hi) __A,
                (__v16qi) _mm_setzero_si128(),
                (__mmask8) -1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvtusepi16_epi8 (__m128i __O, __mmask8 __M, __m128i __A) {
  return (__m128i) __builtin_ia32_pmovuswb128_mask ((__v8hi) __A,
                (__v16qi) __O,
                __M);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvtusepi16_epi8 (__mmask8 __M, __m128i __A) {
  return (__m128i) __builtin_ia32_pmovuswb128_mask ((__v8hi) __A,
                (__v16qi) _mm_setzero_si128(),
                __M);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS256
_mm256_cvtusepi16_epi8 (__m256i __A) {
  return (__m128i) __builtin_ia32_pmovuswb256_mask ((__v16hi) __A,
                (__v16qi) _mm_setzero_si128(),
                (__mmask16) -1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS256
_mm256_mask_cvtusepi16_epi8 (__m128i __O, __mmask16 __M, __m256i __A) {
  return (__m128i) __builtin_ia32_pmovuswb256_mask ((__v16hi) __A,
                (__v16qi) __O,
                __M);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtusepi16_epi8 (__mmask16 __M, __m256i __A) {
  return (__m128i) __builtin_ia32_pmovuswb256_mask ((__v16hi) __A,
                (__v16qi) _mm_setzero_si128(),
                __M);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_cvtepi16_epi8 (__m128i __A) {
  return (__m128i)__builtin_shufflevector(
      __builtin_convertvector((__v8hi)__A, __v8qi),
      (__v8qi){0, 0, 0, 0, 0, 0, 0, 0}, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
      12, 13, 14, 15);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvtepi16_epi8 (__m128i __O, __mmask8 __M, __m128i __A) {
  return (__m128i) __builtin_ia32_pmovwb128_mask ((__v8hi) __A,
               (__v16qi) __O,
               __M);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvtepi16_epi8 (__mmask8 __M, __m128i __A) {
  return (__m128i) __builtin_ia32_pmovwb128_mask ((__v8hi) __A,
               (__v16qi) _mm_setzero_si128(),
               __M);
}

static __inline__ void __DEFAULT_FN_ATTRS128
_mm_mask_cvtepi16_storeu_epi8 (void * __P, __mmask8 __M, __m128i __A)
{
  __builtin_ia32_pmovwb128mem_mask ((__v16qi *) __P, (__v8hi) __A, __M);
}


static __inline__ void __DEFAULT_FN_ATTRS128
_mm_mask_cvtsepi16_storeu_epi8 (void * __P, __mmask8 __M, __m128i __A)
{
  __builtin_ia32_pmovswb128mem_mask ((__v16qi *) __P, (__v8hi) __A, __M);
}

static __inline__ void __DEFAULT_FN_ATTRS128
_mm_mask_cvtusepi16_storeu_epi8 (void * __P, __mmask8 __M, __m128i __A)
{
  __builtin_ia32_pmovuswb128mem_mask ((__v16qi *) __P, (__v8hi) __A, __M);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS256
_mm256_cvtepi16_epi8 (__m256i __A) {
  return (__m128i)__builtin_convertvector((__v16hi) __A, __v16qi);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS256
_mm256_mask_cvtepi16_epi8 (__m128i __O, __mmask16 __M, __m256i __A) {
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__M,
                                             (__v16qi)_mm256_cvtepi16_epi8(__A),
                                             (__v16qi)__O);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtepi16_epi8 (__mmask16 __M, __m256i __A) {
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__M,
                                             (__v16qi)_mm256_cvtepi16_epi8(__A),
                                             (__v16qi)_mm_setzero_si128());
}

static __inline__ void __DEFAULT_FN_ATTRS256
_mm256_mask_cvtepi16_storeu_epi8 (void * __P, __mmask16 __M, __m256i __A)
{
  __builtin_ia32_pmovwb256mem_mask ((__v16qi *) __P, (__v16hi) __A, __M);
}

static __inline__ void __DEFAULT_FN_ATTRS256
_mm256_mask_cvtsepi16_storeu_epi8 (void * __P, __mmask16 __M, __m256i __A)
{
  __builtin_ia32_pmovswb256mem_mask ((__v16qi *) __P, (__v16hi) __A, __M);
}

static __inline__ void __DEFAULT_FN_ATTRS256
_mm256_mask_cvtusepi16_storeu_epi8 (void * __P, __mmask16 __M, __m256i __A)
{
  __builtin_ia32_pmovuswb256mem_mask ((__v16qi*) __P, (__v16hi) __A, __M);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_mulhrs_epi16(__m128i __W, __mmask8 __U, __m128i __X, __m128i __Y) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_mulhrs_epi16(__X, __Y),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_mulhrs_epi16(__mmask8 __U, __m128i __X, __m128i __Y) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_mulhrs_epi16(__X, __Y),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_mulhrs_epi16(__m256i __W, __mmask16 __U, __m256i __X, __m256i __Y) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                         (__v16hi)_mm256_mulhrs_epi16(__X, __Y),
                                         (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_mulhrs_epi16(__mmask16 __U, __m256i __X, __m256i __Y) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                         (__v16hi)_mm256_mulhrs_epi16(__X, __Y),
                                         (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_mulhi_epu16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_mulhi_epu16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_mulhi_epu16(__mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_mulhi_epu16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_mulhi_epu16(__m256i __W, __mmask16 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                          (__v16hi)_mm256_mulhi_epu16(__A, __B),
                                          (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_mulhi_epu16(__mmask16 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                          (__v16hi)_mm256_mulhi_epu16(__A, __B),
                                          (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_mulhi_epi16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_mulhi_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_mulhi_epi16(__mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_mulhi_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_mulhi_epi16(__m256i __W, __mmask16 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                          (__v16hi)_mm256_mulhi_epi16(__A, __B),
                                          (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_mulhi_epi16(__mmask16 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                          (__v16hi)_mm256_mulhi_epi16(__A, __B),
                                          (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_unpackhi_epi8(__m128i __W, __mmask16 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                           (__v16qi)_mm_unpackhi_epi8(__A, __B),
                                           (__v16qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_unpackhi_epi8(__mmask16 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                           (__v16qi)_mm_unpackhi_epi8(__A, __B),
                                           (__v16qi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_unpackhi_epi8(__m256i __W, __mmask32 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                        (__v32qi)_mm256_unpackhi_epi8(__A, __B),
                                        (__v32qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_unpackhi_epi8(__mmask32 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                        (__v32qi)_mm256_unpackhi_epi8(__A, __B),
                                        (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_unpackhi_epi16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                           (__v8hi)_mm_unpackhi_epi16(__A, __B),
                                           (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_unpackhi_epi16(__mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                           (__v8hi)_mm_unpackhi_epi16(__A, __B),
                                           (__v8hi) _mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_unpackhi_epi16(__m256i __W, __mmask16 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                       (__v16hi)_mm256_unpackhi_epi16(__A, __B),
                                       (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_unpackhi_epi16(__mmask16 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                       (__v16hi)_mm256_unpackhi_epi16(__A, __B),
                                       (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_unpacklo_epi8(__m128i __W, __mmask16 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                           (__v16qi)_mm_unpacklo_epi8(__A, __B),
                                           (__v16qi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_unpacklo_epi8(__mmask16 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectb_128((__mmask16)__U,
                                           (__v16qi)_mm_unpacklo_epi8(__A, __B),
                                           (__v16qi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_unpacklo_epi8(__m256i __W, __mmask32 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                        (__v32qi)_mm256_unpacklo_epi8(__A, __B),
                                        (__v32qi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_unpacklo_epi8(__mmask32 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectb_256((__mmask32)__U,
                                        (__v32qi)_mm256_unpacklo_epi8(__A, __B),
                                        (__v32qi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_unpacklo_epi16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                           (__v8hi)_mm_unpacklo_epi16(__A, __B),
                                           (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_unpacklo_epi16(__mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                           (__v8hi)_mm_unpacklo_epi16(__A, __B),
                                           (__v8hi) _mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_unpacklo_epi16(__m256i __W, __mmask16 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                       (__v16hi)_mm256_unpacklo_epi16(__A, __B),
                                       (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_unpacklo_epi16(__mmask16 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                       (__v16hi)_mm256_unpacklo_epi16(__A, __B),
                                       (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvtepi8_epi16(__m128i __W, __mmask8 __U, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_cvtepi8_epi16(__A),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvtepi8_epi16(__mmask8 __U, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_cvtepi8_epi16(__A),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvtepi8_epi16(__m256i __W, __mmask16 __U, __m128i __A)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                             (__v16hi)_mm256_cvtepi8_epi16(__A),
                                             (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtepi8_epi16(__mmask16 __U, __m128i __A)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                             (__v16hi)_mm256_cvtepi8_epi16(__A),
                                             (__v16hi)_mm256_setzero_si256());
}


static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvtepu8_epi16(__m128i __W, __mmask8 __U, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_cvtepu8_epi16(__A),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvtepu8_epi16(__mmask8 __U, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_cvtepu8_epi16(__A),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvtepu8_epi16(__m256i __W, __mmask16 __U, __m128i __A)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                             (__v16hi)_mm256_cvtepu8_epi16(__A),
                                             (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtepu8_epi16 (__mmask16 __U, __m128i __A)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                             (__v16hi)_mm256_cvtepu8_epi16(__A),
                                             (__v16hi)_mm256_setzero_si256());
}


#define _mm_mask_shufflehi_epi16(W, U, A, imm) \
  (__m128i)__builtin_ia32_selectw_128((__mmask8)(U), \
                                      (__v8hi)_mm_shufflehi_epi16((A), (imm)), \
                                      (__v8hi)(__m128i)(W))

#define _mm_maskz_shufflehi_epi16(U, A, imm) \
  (__m128i)__builtin_ia32_selectw_128((__mmask8)(U), \
                                      (__v8hi)_mm_shufflehi_epi16((A), (imm)), \
                                      (__v8hi)_mm_setzero_si128())

#define _mm256_mask_shufflehi_epi16(W, U, A, imm) \
  (__m256i)__builtin_ia32_selectw_256((__mmask16)(U), \
                                      (__v16hi)_mm256_shufflehi_epi16((A), (imm)), \
                                      (__v16hi)(__m256i)(W))

#define _mm256_maskz_shufflehi_epi16(U, A, imm) \
  (__m256i)__builtin_ia32_selectw_256((__mmask16)(U), \
                                      (__v16hi)_mm256_shufflehi_epi16((A), (imm)), \
                                      (__v16hi)_mm256_setzero_si256())

#define _mm_mask_shufflelo_epi16(W, U, A, imm) \
  (__m128i)__builtin_ia32_selectw_128((__mmask8)(U), \
                                      (__v8hi)_mm_shufflelo_epi16((A), (imm)), \
                                      (__v8hi)(__m128i)(W))

#define _mm_maskz_shufflelo_epi16(U, A, imm) \
  (__m128i)__builtin_ia32_selectw_128((__mmask8)(U), \
                                      (__v8hi)_mm_shufflelo_epi16((A), (imm)), \
                                      (__v8hi)_mm_setzero_si128())

#define _mm256_mask_shufflelo_epi16(W, U, A, imm) \
  (__m256i)__builtin_ia32_selectw_256((__mmask16)(U), \
                                      (__v16hi)_mm256_shufflelo_epi16((A), \
                                                                      (imm)), \
                                      (__v16hi)(__m256i)(W))

#define _mm256_maskz_shufflelo_epi16(U, A, imm) \
  (__m256i)__builtin_ia32_selectw_256((__mmask16)(U), \
                                      (__v16hi)_mm256_shufflelo_epi16((A), \
                                                                      (imm)), \
                                      (__v16hi)_mm256_setzero_si256())

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_sllv_epi16(__m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_psllv16hi((__v16hi)__A, (__v16hi)__B);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_sllv_epi16(__m256i __W, __mmask16 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                           (__v16hi)_mm256_sllv_epi16(__A, __B),
                                           (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_sllv_epi16(__mmask16 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                           (__v16hi)_mm256_sllv_epi16(__A, __B),
                                           (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_sllv_epi16(__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psllv8hi((__v8hi)__A, (__v8hi)__B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_sllv_epi16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_sllv_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_sllv_epi16(__mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_sllv_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_sll_epi16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_sll_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_sll_epi16 (__mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_sll_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_sll_epi16(__m256i __W, __mmask16 __U, __m256i __A, __m128i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                          (__v16hi)_mm256_sll_epi16(__A, __B),
                                          (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_sll_epi16(__mmask16 __U, __m256i __A, __m128i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                          (__v16hi)_mm256_sll_epi16(__A, __B),
                                          (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_slli_epi16(__m128i __W, __mmask8 __U, __m128i __A, int __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_slli_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_slli_epi16 (__mmask8 __U, __m128i __A, int __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_slli_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_slli_epi16(__m256i __W, __mmask16 __U, __m256i __A, int __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                         (__v16hi)_mm256_slli_epi16(__A, __B),
                                         (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_slli_epi16(__mmask16 __U, __m256i __A, int __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                         (__v16hi)_mm256_slli_epi16(__A, __B),
                                         (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_srlv_epi16(__m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_psrlv16hi((__v16hi)__A, (__v16hi)__B);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_srlv_epi16(__m256i __W, __mmask16 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                           (__v16hi)_mm256_srlv_epi16(__A, __B),
                                           (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_srlv_epi16(__mmask16 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                           (__v16hi)_mm256_srlv_epi16(__A, __B),
                                           (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_srlv_epi16(__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psrlv8hi((__v8hi)__A, (__v8hi)__B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_srlv_epi16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_srlv_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_srlv_epi16(__mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_srlv_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_srav_epi16(__m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_psrav16hi((__v16hi)__A, (__v16hi)__B);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_srav_epi16(__m256i __W, __mmask16 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                           (__v16hi)_mm256_srav_epi16(__A, __B),
                                           (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_srav_epi16(__mmask16 __U, __m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                           (__v16hi)_mm256_srav_epi16(__A, __B),
                                           (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_srav_epi16(__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_psrav8hi((__v8hi)__A, (__v8hi)__B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_srav_epi16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_srav_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_srav_epi16(__mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_srav_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_sra_epi16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_sra_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_sra_epi16(__mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_sra_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_sra_epi16(__m256i __W, __mmask16 __U, __m256i __A, __m128i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                          (__v16hi)_mm256_sra_epi16(__A, __B),
                                          (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_sra_epi16(__mmask16 __U, __m256i __A, __m128i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                          (__v16hi)_mm256_sra_epi16(__A, __B),
                                          (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_srai_epi16(__m128i __W, __mmask8 __U, __m128i __A, int __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_srai_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_srai_epi16(__mmask8 __U, __m128i __A, int __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_srai_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_srai_epi16(__m256i __W, __mmask16 __U, __m256i __A, int __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                         (__v16hi)_mm256_srai_epi16(__A, __B),
                                         (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_srai_epi16(__mmask16 __U, __m256i __A, int __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                         (__v16hi)_mm256_srai_epi16(__A, __B),
                                         (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_srl_epi16(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_srl_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_srl_epi16 (__mmask8 __U, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_srl_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_srl_epi16(__m256i __W, __mmask16 __U, __m256i __A, __m128i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                          (__v16hi)_mm256_srl_epi16(__A, __B),
                                          (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_srl_epi16(__mmask16 __U, __m256i __A, __m128i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                          (__v16hi)_mm256_srl_epi16(__A, __B),
                                          (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_srli_epi16(__m128i __W, __mmask8 __U, __m128i __A, int __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_srli_epi16(__A, __B),
                                             (__v8hi)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_srli_epi16 (__mmask8 __U, __m128i __A, int __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__U,
                                             (__v8hi)_mm_srli_epi16(__A, __B),
                                             (__v8hi)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_srli_epi16(__m256i __W, __mmask16 __U, __m256i __A, int __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                         (__v16hi)_mm256_srli_epi16(__A, __B),
                                         (__v16hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_srli_epi16(__mmask16 __U, __m256i __A, int __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__U,
                                         (__v16hi)_mm256_srli_epi16(__A, __B),
                                         (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_mov_epi16 (__m128i __W, __mmask8 __U, __m128i __A)
{
  return (__m128i) __builtin_ia32_selectw_128 ((__mmask8) __U,
                (__v8hi) __A,
                (__v8hi) __W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_mov_epi16 (__mmask8 __U, __m128i __A)
{
  return (__m128i) __builtin_ia32_selectw_128 ((__mmask8) __U,
                (__v8hi) __A,
                (__v8hi) _mm_setzero_si128 ());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_mov_epi16 (__m256i __W, __mmask16 __U, __m256i __A)
{
  return (__m256i) __builtin_ia32_selectw_256 ((__mmask16) __U,
                (__v16hi) __A,
                (__v16hi) __W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_mov_epi16 (__mmask16 __U, __m256i __A)
{
  return (__m256i) __builtin_ia32_selectw_256 ((__mmask16) __U,
                (__v16hi) __A,
                (__v16hi) _mm256_setzero_si256 ());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_mov_epi8 (__m128i __W, __mmask16 __U, __m128i __A)
{
  return (__m128i) __builtin_ia32_selectb_128 ((__mmask16) __U,
                (__v16qi) __A,
                (__v16qi) __W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_mov_epi8 (__mmask16 __U, __m128i __A)
{
  return (__m128i) __builtin_ia32_selectb_128 ((__mmask16) __U,
                (__v16qi) __A,
                (__v16qi) _mm_setzero_si128 ());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_mov_epi8 (__m256i __W, __mmask32 __U, __m256i __A)
{
  return (__m256i) __builtin_ia32_selectb_256 ((__mmask32) __U,
                (__v32qi) __A,
                (__v32qi) __W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_mov_epi8 (__mmask32 __U, __m256i __A)
{
  return (__m256i) __builtin_ia32_selectb_256 ((__mmask32) __U,
                (__v32qi) __A,
                (__v32qi) _mm256_setzero_si256 ());
}


static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_set1_epi8 (__m128i __O, __mmask16 __M, char __A)
{
  return (__m128i) __builtin_ia32_selectb_128(__M,
                                              (__v16qi) _mm_set1_epi8(__A),
                                              (__v16qi) __O);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_set1_epi8 (__mmask16 __M, char __A)
{
 return (__m128i) __builtin_ia32_selectb_128(__M,
                                             (__v16qi) _mm_set1_epi8(__A),
                                             (__v16qi) _mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_set1_epi8 (__m256i __O, __mmask32 __M, char __A)
{
  return (__m256i) __builtin_ia32_selectb_256(__M,
                                              (__v32qi) _mm256_set1_epi8(__A),
                                              (__v32qi) __O);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_set1_epi8 (__mmask32 __M, char __A)
{
  return (__m256i) __builtin_ia32_selectb_256(__M,
                                              (__v32qi) _mm256_set1_epi8(__A),
                                              (__v32qi) _mm256_setzero_si256());
}

static __inline __m128i __DEFAULT_FN_ATTRS128
_mm_loadu_epi16 (void const *__P)
{
  struct __loadu_epi16 {
    __m128i __v;
  } __attribute__((__packed__, __may_alias__));
  return ((struct __loadu_epi16*)__P)->__v;
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_loadu_epi16 (__m128i __W, __mmask8 __U, void const *__P)
{
  return (__m128i) __builtin_ia32_loaddquhi128_mask ((__v8hi *) __P,
                 (__v8hi) __W,
                 (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_loadu_epi16 (__mmask8 __U, void const *__P)
{
  return (__m128i) __builtin_ia32_loaddquhi128_mask ((__v8hi *) __P,
                 (__v8hi)
                 _mm_setzero_si128 (),
                 (__mmask8) __U);
}

static __inline __m256i __DEFAULT_FN_ATTRS256
_mm256_loadu_epi16 (void const *__P)
{
  struct __loadu_epi16 {
    __m256i __v;
  } __attribute__((__packed__, __may_alias__));
  return ((struct __loadu_epi16*)__P)->__v;
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_loadu_epi16 (__m256i __W, __mmask16 __U, void const *__P)
{
  return (__m256i) __builtin_ia32_loaddquhi256_mask ((__v16hi *) __P,
                 (__v16hi) __W,
                 (__mmask16) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_loadu_epi16 (__mmask16 __U, void const *__P)
{
  return (__m256i) __builtin_ia32_loaddquhi256_mask ((__v16hi *) __P,
                 (__v16hi)
                 _mm256_setzero_si256 (),
                 (__mmask16) __U);
}

static __inline __m128i __DEFAULT_FN_ATTRS128
_mm_loadu_epi8 (void const *__P)
{
  struct __loadu_epi8 {
    __m128i __v;
  } __attribute__((__packed__, __may_alias__));
  return ((struct __loadu_epi8*)__P)->__v;
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_loadu_epi8 (__m128i __W, __mmask16 __U, void const *__P)
{
  return (__m128i) __builtin_ia32_loaddquqi128_mask ((__v16qi *) __P,
                 (__v16qi) __W,
                 (__mmask16) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_loadu_epi8 (__mmask16 __U, void const *__P)
{
  return (__m128i) __builtin_ia32_loaddquqi128_mask ((__v16qi *) __P,
                 (__v16qi)
                 _mm_setzero_si128 (),
                 (__mmask16) __U);
}

static __inline __m256i __DEFAULT_FN_ATTRS256
_mm256_loadu_epi8 (void const *__P)
{
  struct __loadu_epi8 {
    __m256i __v;
  } __attribute__((__packed__, __may_alias__));
  return ((struct __loadu_epi8*)__P)->__v;
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_loadu_epi8 (__m256i __W, __mmask32 __U, void const *__P)
{
  return (__m256i) __builtin_ia32_loaddquqi256_mask ((__v32qi *) __P,
                 (__v32qi) __W,
                 (__mmask32) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_loadu_epi8 (__mmask32 __U, void const *__P)
{
  return (__m256i) __builtin_ia32_loaddquqi256_mask ((__v32qi *) __P,
                 (__v32qi)
                 _mm256_setzero_si256 (),
                 (__mmask32) __U);
}

static __inline void __DEFAULT_FN_ATTRS128
_mm_storeu_epi16 (void *__P, __m128i __A)
{
  struct __storeu_epi16 {
    __m128i __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_epi16*)__P)->__v = __A;
}

static __inline__ void __DEFAULT_FN_ATTRS128
_mm_mask_storeu_epi16 (void *__P, __mmask8 __U, __m128i __A)
{
  __builtin_ia32_storedquhi128_mask ((__v8hi *) __P,
             (__v8hi) __A,
             (__mmask8) __U);
}

static __inline void __DEFAULT_FN_ATTRS256
_mm256_storeu_epi16 (void *__P, __m256i __A)
{
  struct __storeu_epi16 {
    __m256i __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_epi16*)__P)->__v = __A;
}

static __inline__ void __DEFAULT_FN_ATTRS256
_mm256_mask_storeu_epi16 (void *__P, __mmask16 __U, __m256i __A)
{
  __builtin_ia32_storedquhi256_mask ((__v16hi *) __P,
             (__v16hi) __A,
             (__mmask16) __U);
}

static __inline void __DEFAULT_FN_ATTRS128
_mm_storeu_epi8 (void *__P, __m128i __A)
{
  struct __storeu_epi8 {
    __m128i __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_epi8*)__P)->__v = __A;
}

static __inline__ void __DEFAULT_FN_ATTRS128
_mm_mask_storeu_epi8 (void *__P, __mmask16 __U, __m128i __A)
{
  __builtin_ia32_storedquqi128_mask ((__v16qi *) __P,
             (__v16qi) __A,
             (__mmask16) __U);
}

static __inline void __DEFAULT_FN_ATTRS256
_mm256_storeu_epi8 (void *__P, __m256i __A)
{
  struct __storeu_epi8 {
    __m256i __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_epi8*)__P)->__v = __A;
}

static __inline__ void __DEFAULT_FN_ATTRS256
_mm256_mask_storeu_epi8 (void *__P, __mmask32 __U, __m256i __A)
{
  __builtin_ia32_storedquqi256_mask ((__v32qi *) __P,
             (__v32qi) __A,
             (__mmask32) __U);
}

static __inline__ __mmask16 __DEFAULT_FN_ATTRS128
_mm_test_epi8_mask (__m128i __A, __m128i __B)
{
  return _mm_cmpneq_epi8_mask (_mm_and_si128(__A, __B), _mm_setzero_si128());
}

static __inline__ __mmask16 __DEFAULT_FN_ATTRS128
_mm_mask_test_epi8_mask (__mmask16 __U, __m128i __A, __m128i __B)
{
  return _mm_mask_cmpneq_epi8_mask (__U, _mm_and_si128 (__A, __B),
                                    _mm_setzero_si128());
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS256
_mm256_test_epi8_mask (__m256i __A, __m256i __B)
{
  return _mm256_cmpneq_epi8_mask (_mm256_and_si256(__A, __B),
                                  _mm256_setzero_si256());
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS256
_mm256_mask_test_epi8_mask (__mmask32 __U, __m256i __A, __m256i __B)
{
  return _mm256_mask_cmpneq_epi8_mask (__U, _mm256_and_si256(__A, __B),
                                       _mm256_setzero_si256());
}

static __inline__ __mmask8 __DEFAULT_FN_ATTRS128
_mm_test_epi16_mask (__m128i __A, __m128i __B)
{
  return _mm_cmpneq_epi16_mask (_mm_and_si128 (__A, __B), _mm_setzero_si128());
}

static __inline__ __mmask8 __DEFAULT_FN_ATTRS128
_mm_mask_test_epi16_mask (__mmask8 __U, __m128i __A, __m128i __B)
{
  return _mm_mask_cmpneq_epi16_mask (__U, _mm_and_si128 (__A, __B),
                                     _mm_setzero_si128());
}

static __inline__ __mmask16 __DEFAULT_FN_ATTRS256
_mm256_test_epi16_mask (__m256i __A, __m256i __B)
{
  return _mm256_cmpneq_epi16_mask (_mm256_and_si256 (__A, __B),
                                   _mm256_setzero_si256 ());
}

static __inline__ __mmask16 __DEFAULT_FN_ATTRS256
_mm256_mask_test_epi16_mask (__mmask16 __U, __m256i __A, __m256i __B)
{
  return _mm256_mask_cmpneq_epi16_mask (__U, _mm256_and_si256(__A, __B),
                                        _mm256_setzero_si256());
}

static __inline__ __mmask16 __DEFAULT_FN_ATTRS128
_mm_testn_epi8_mask (__m128i __A, __m128i __B)
{
  return _mm_cmpeq_epi8_mask (_mm_and_si128 (__A, __B), _mm_setzero_si128());
}

static __inline__ __mmask16 __DEFAULT_FN_ATTRS128
_mm_mask_testn_epi8_mask (__mmask16 __U, __m128i __A, __m128i __B)
{
  return _mm_mask_cmpeq_epi8_mask (__U, _mm_and_si128 (__A, __B),
                                  _mm_setzero_si128());
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS256
_mm256_testn_epi8_mask (__m256i __A, __m256i __B)
{
  return _mm256_cmpeq_epi8_mask (_mm256_and_si256 (__A, __B),
                                 _mm256_setzero_si256());
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS256
_mm256_mask_testn_epi8_mask (__mmask32 __U, __m256i __A, __m256i __B)
{
  return _mm256_mask_cmpeq_epi8_mask (__U, _mm256_and_si256 (__A, __B),
                                      _mm256_setzero_si256());
}

static __inline__ __mmask8 __DEFAULT_FN_ATTRS128
_mm_testn_epi16_mask (__m128i __A, __m128i __B)
{
  return _mm_cmpeq_epi16_mask (_mm_and_si128 (__A, __B), _mm_setzero_si128());
}

static __inline__ __mmask8 __DEFAULT_FN_ATTRS128
_mm_mask_testn_epi16_mask (__mmask8 __U, __m128i __A, __m128i __B)
{
  return _mm_mask_cmpeq_epi16_mask (__U, _mm_and_si128(__A, __B), _mm_setzero_si128());
}

static __inline__ __mmask16 __DEFAULT_FN_ATTRS256
_mm256_testn_epi16_mask (__m256i __A, __m256i __B)
{
  return _mm256_cmpeq_epi16_mask (_mm256_and_si256(__A, __B),
                                  _mm256_setzero_si256());
}

static __inline__ __mmask16 __DEFAULT_FN_ATTRS256
_mm256_mask_testn_epi16_mask (__mmask16 __U, __m256i __A, __m256i __B)
{
  return _mm256_mask_cmpeq_epi16_mask (__U, _mm256_and_si256 (__A, __B),
                                       _mm256_setzero_si256());
}

static __inline__ __mmask16 __DEFAULT_FN_ATTRS128
_mm_movepi8_mask (__m128i __A)
{
  return (__mmask16) __builtin_ia32_cvtb2mask128 ((__v16qi) __A);
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS256
_mm256_movepi8_mask (__m256i __A)
{
  return (__mmask32) __builtin_ia32_cvtb2mask256 ((__v32qi) __A);
}

static __inline__ __mmask8 __DEFAULT_FN_ATTRS128
_mm_movepi16_mask (__m128i __A)
{
  return (__mmask8) __builtin_ia32_cvtw2mask128 ((__v8hi) __A);
}

static __inline__ __mmask16 __DEFAULT_FN_ATTRS256
_mm256_movepi16_mask (__m256i __A)
{
  return (__mmask16) __builtin_ia32_cvtw2mask256 ((__v16hi) __A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_movm_epi8 (__mmask16 __A)
{
  return (__m128i) __builtin_ia32_cvtmask2b128 (__A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_movm_epi8 (__mmask32 __A)
{
  return (__m256i) __builtin_ia32_cvtmask2b256 (__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_movm_epi16 (__mmask8 __A)
{
  return (__m128i) __builtin_ia32_cvtmask2w128 (__A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_movm_epi16 (__mmask16 __A)
{
  return (__m256i) __builtin_ia32_cvtmask2w256 (__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_broadcastb_epi8 (__m128i __O, __mmask16 __M, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectb_128(__M,
                                             (__v16qi) _mm_broadcastb_epi8(__A),
                                             (__v16qi) __O);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_broadcastb_epi8 (__mmask16 __M, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectb_128(__M,
                                             (__v16qi) _mm_broadcastb_epi8(__A),
                                             (__v16qi) _mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_broadcastb_epi8 (__m256i __O, __mmask32 __M, __m128i __A)
{
  return (__m256i)__builtin_ia32_selectb_256(__M,
                                             (__v32qi) _mm256_broadcastb_epi8(__A),
                                             (__v32qi) __O);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_broadcastb_epi8 (__mmask32 __M, __m128i __A)
{
  return (__m256i)__builtin_ia32_selectb_256(__M,
                                             (__v32qi) _mm256_broadcastb_epi8(__A),
                                             (__v32qi) _mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_broadcastw_epi16 (__m128i __O, __mmask8 __M, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectw_128(__M,
                                             (__v8hi) _mm_broadcastw_epi16(__A),
                                             (__v8hi) __O);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_broadcastw_epi16 (__mmask8 __M, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectw_128(__M,
                                             (__v8hi) _mm_broadcastw_epi16(__A),
                                             (__v8hi) _mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_broadcastw_epi16 (__m256i __O, __mmask16 __M, __m128i __A)
{
  return (__m256i)__builtin_ia32_selectw_256(__M,
                                             (__v16hi) _mm256_broadcastw_epi16(__A),
                                             (__v16hi) __O);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_broadcastw_epi16 (__mmask16 __M, __m128i __A)
{
  return (__m256i)__builtin_ia32_selectw_256(__M,
                                             (__v16hi) _mm256_broadcastw_epi16(__A),
                                             (__v16hi) _mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_set1_epi16 (__m256i __O, __mmask16 __M, short __A)
{
  return (__m256i) __builtin_ia32_selectw_256 (__M,
                                               (__v16hi) _mm256_set1_epi16(__A),
                                               (__v16hi) __O);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_set1_epi16 (__mmask16 __M, short __A)
{
  return (__m256i) __builtin_ia32_selectw_256(__M,
                                              (__v16hi)_mm256_set1_epi16(__A),
                                              (__v16hi) _mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_set1_epi16 (__m128i __O, __mmask8 __M, short __A)
{
  return (__m128i) __builtin_ia32_selectw_128(__M,
                                              (__v8hi) _mm_set1_epi16(__A),
                                              (__v8hi) __O);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_set1_epi16 (__mmask8 __M, short __A)
{
  return (__m128i) __builtin_ia32_selectw_128(__M,
                                              (__v8hi) _mm_set1_epi16(__A),
                                              (__v8hi) _mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_permutexvar_epi16 (__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_permvarhi128((__v8hi) __B, (__v8hi) __A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_permutexvar_epi16 (__mmask8 __M, __m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__M,
                                        (__v8hi)_mm_permutexvar_epi16(__A, __B),
                                        (__v8hi) _mm_setzero_si128());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_permutexvar_epi16 (__m128i __W, __mmask8 __M, __m128i __A,
          __m128i __B)
{
  return (__m128i)__builtin_ia32_selectw_128((__mmask8)__M,
                                        (__v8hi)_mm_permutexvar_epi16(__A, __B),
                                        (__v8hi)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_permutexvar_epi16 (__m256i __A, __m256i __B)
{
  return (__m256i)__builtin_ia32_permvarhi256((__v16hi) __B, (__v16hi) __A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_permutexvar_epi16 (__mmask16 __M, __m256i __A,
        __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__M,
                                    (__v16hi)_mm256_permutexvar_epi16(__A, __B),
                                    (__v16hi)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_permutexvar_epi16 (__m256i __W, __mmask16 __M, __m256i __A,
             __m256i __B)
{
  return (__m256i)__builtin_ia32_selectw_256((__mmask16)__M,
                                    (__v16hi)_mm256_permutexvar_epi16(__A, __B),
                                    (__v16hi)__W);
}

#define _mm_mask_alignr_epi8(W, U, A, B, N) \
  (__m128i)__builtin_ia32_selectb_128((__mmask16)(U), \
                                 (__v16qi)_mm_alignr_epi8((A), (B), (int)(N)), \
                                 (__v16qi)(__m128i)(W))

#define _mm_maskz_alignr_epi8(U, A, B, N) \
  (__m128i)__builtin_ia32_selectb_128((__mmask16)(U), \
                                 (__v16qi)_mm_alignr_epi8((A), (B), (int)(N)), \
                                 (__v16qi)_mm_setzero_si128())

#define _mm256_mask_alignr_epi8(W, U, A, B, N) \
  (__m256i)__builtin_ia32_selectb_256((__mmask32)(U), \
                              (__v32qi)_mm256_alignr_epi8((A), (B), (int)(N)), \
                              (__v32qi)(__m256i)(W))

#define _mm256_maskz_alignr_epi8(U, A, B, N) \
  (__m256i)__builtin_ia32_selectb_256((__mmask32)(U), \
                              (__v32qi)_mm256_alignr_epi8((A), (B), (int)(N)), \
                              (__v32qi)_mm256_setzero_si256())

#define _mm_dbsad_epu8(A, B, imm) \
  (__m128i)__builtin_ia32_dbpsadbw128((__v16qi)(__m128i)(A), \
                                      (__v16qi)(__m128i)(B), (int)(imm))

#define _mm_mask_dbsad_epu8(W, U, A, B, imm) \
  (__m128i)__builtin_ia32_selectw_128((__mmask8)(U), \
                                      (__v8hi)_mm_dbsad_epu8((A), (B), (imm)), \
                                      (__v8hi)(__m128i)(W))

#define _mm_maskz_dbsad_epu8(U, A, B, imm) \
  (__m128i)__builtin_ia32_selectw_128((__mmask8)(U), \
                                      (__v8hi)_mm_dbsad_epu8((A), (B), (imm)), \
                                      (__v8hi)_mm_setzero_si128())

#define _mm256_dbsad_epu8(A, B, imm) \
  (__m256i)__builtin_ia32_dbpsadbw256((__v32qi)(__m256i)(A), \
                                      (__v32qi)(__m256i)(B), (int)(imm))

#define _mm256_mask_dbsad_epu8(W, U, A, B, imm) \
  (__m256i)__builtin_ia32_selectw_256((__mmask16)(U), \
                                  (__v16hi)_mm256_dbsad_epu8((A), (B), (imm)), \
                                  (__v16hi)(__m256i)(W))

#define _mm256_maskz_dbsad_epu8(U, A, B, imm) \
  (__m256i)__builtin_ia32_selectw_256((__mmask16)(U), \
                                  (__v16hi)_mm256_dbsad_epu8((A), (B), (imm)), \
                                  (__v16hi)_mm256_setzero_si256())

#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256

#endif /* __AVX512VLBWINTRIN_H */
