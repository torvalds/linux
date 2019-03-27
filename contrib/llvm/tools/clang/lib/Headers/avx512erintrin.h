/*===---- avx512erintrin.h - AVX512ER intrinsics ---------------------------===
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
#error "Never use <avx512erintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512ERINTRIN_H
#define __AVX512ERINTRIN_H

/* exp2a23 */
#define _mm512_exp2a23_round_pd(A, R) \
  (__m512d)__builtin_ia32_exp2pd_mask((__v8df)(__m512d)(A), \
                                      (__v8df)_mm512_setzero_pd(), \
                                      (__mmask8)-1, (int)(R))

#define _mm512_mask_exp2a23_round_pd(S, M, A, R) \
  (__m512d)__builtin_ia32_exp2pd_mask((__v8df)(__m512d)(A), \
                                      (__v8df)(__m512d)(S), (__mmask8)(M), \
                                      (int)(R))

#define _mm512_maskz_exp2a23_round_pd(M, A, R) \
  (__m512d)__builtin_ia32_exp2pd_mask((__v8df)(__m512d)(A), \
                                      (__v8df)_mm512_setzero_pd(), \
                                      (__mmask8)(M), (int)(R))

#define _mm512_exp2a23_pd(A) \
  _mm512_exp2a23_round_pd((A), _MM_FROUND_CUR_DIRECTION)

#define _mm512_mask_exp2a23_pd(S, M, A) \
  _mm512_mask_exp2a23_round_pd((S), (M), (A), _MM_FROUND_CUR_DIRECTION)

#define _mm512_maskz_exp2a23_pd(M, A) \
  _mm512_maskz_exp2a23_round_pd((M), (A), _MM_FROUND_CUR_DIRECTION)

#define _mm512_exp2a23_round_ps(A, R) \
  (__m512)__builtin_ia32_exp2ps_mask((__v16sf)(__m512)(A), \
                                     (__v16sf)_mm512_setzero_ps(), \
                                     (__mmask16)-1, (int)(R))

#define _mm512_mask_exp2a23_round_ps(S, M, A, R) \
  (__m512)__builtin_ia32_exp2ps_mask((__v16sf)(__m512)(A), \
                                     (__v16sf)(__m512)(S), (__mmask16)(M), \
                                     (int)(R))

#define _mm512_maskz_exp2a23_round_ps(M, A, R) \
  (__m512)__builtin_ia32_exp2ps_mask((__v16sf)(__m512)(A), \
                                     (__v16sf)_mm512_setzero_ps(), \
                                     (__mmask16)(M), (int)(R))

#define _mm512_exp2a23_ps(A) \
  _mm512_exp2a23_round_ps((A), _MM_FROUND_CUR_DIRECTION)

#define _mm512_mask_exp2a23_ps(S, M, A) \
  _mm512_mask_exp2a23_round_ps((S), (M), (A), _MM_FROUND_CUR_DIRECTION)

#define _mm512_maskz_exp2a23_ps(M, A) \
  _mm512_maskz_exp2a23_round_ps((M), (A), _MM_FROUND_CUR_DIRECTION)

/* rsqrt28 */
#define _mm512_rsqrt28_round_pd(A, R) \
  (__m512d)__builtin_ia32_rsqrt28pd_mask((__v8df)(__m512d)(A), \
                                         (__v8df)_mm512_setzero_pd(), \
                                         (__mmask8)-1, (int)(R))

#define _mm512_mask_rsqrt28_round_pd(S, M, A, R) \
  (__m512d)__builtin_ia32_rsqrt28pd_mask((__v8df)(__m512d)(A), \
                                         (__v8df)(__m512d)(S), (__mmask8)(M), \
                                         (int)(R))

#define _mm512_maskz_rsqrt28_round_pd(M, A, R) \
  (__m512d)__builtin_ia32_rsqrt28pd_mask((__v8df)(__m512d)(A), \
                                         (__v8df)_mm512_setzero_pd(), \
                                         (__mmask8)(M), (int)(R))

#define _mm512_rsqrt28_pd(A) \
  _mm512_rsqrt28_round_pd((A), _MM_FROUND_CUR_DIRECTION)

#define _mm512_mask_rsqrt28_pd(S, M, A) \
  _mm512_mask_rsqrt28_round_pd((S), (M), (A), _MM_FROUND_CUR_DIRECTION)

#define _mm512_maskz_rsqrt28_pd(M, A) \
  _mm512_maskz_rsqrt28_round_pd((M), (A), _MM_FROUND_CUR_DIRECTION)

#define _mm512_rsqrt28_round_ps(A, R) \
  (__m512)__builtin_ia32_rsqrt28ps_mask((__v16sf)(__m512)(A), \
                                        (__v16sf)_mm512_setzero_ps(), \
                                        (__mmask16)-1, (int)(R))

#define _mm512_mask_rsqrt28_round_ps(S, M, A, R) \
  (__m512)__builtin_ia32_rsqrt28ps_mask((__v16sf)(__m512)(A), \
                                        (__v16sf)(__m512)(S), (__mmask16)(M), \
                                        (int)(R))

#define _mm512_maskz_rsqrt28_round_ps(M, A, R) \
  (__m512)__builtin_ia32_rsqrt28ps_mask((__v16sf)(__m512)(A), \
                                        (__v16sf)_mm512_setzero_ps(), \
                                        (__mmask16)(M), (int)(R))

#define _mm512_rsqrt28_ps(A) \
  _mm512_rsqrt28_round_ps((A), _MM_FROUND_CUR_DIRECTION)

#define _mm512_mask_rsqrt28_ps(S, M, A) \
  _mm512_mask_rsqrt28_round_ps((S), (M), A, _MM_FROUND_CUR_DIRECTION)

#define _mm512_maskz_rsqrt28_ps(M, A) \
  _mm512_maskz_rsqrt28_round_ps((M), (A), _MM_FROUND_CUR_DIRECTION)

#define _mm_rsqrt28_round_ss(A, B, R) \
  (__m128)__builtin_ia32_rsqrt28ss_round_mask((__v4sf)(__m128)(A), \
                                              (__v4sf)(__m128)(B), \
                                              (__v4sf)_mm_setzero_ps(), \
                                              (__mmask8)-1, (int)(R))

#define _mm_mask_rsqrt28_round_ss(S, M, A, B, R) \
  (__m128)__builtin_ia32_rsqrt28ss_round_mask((__v4sf)(__m128)(A), \
                                              (__v4sf)(__m128)(B), \
                                              (__v4sf)(__m128)(S), \
                                              (__mmask8)(M), (int)(R))

#define _mm_maskz_rsqrt28_round_ss(M, A, B, R) \
  (__m128)__builtin_ia32_rsqrt28ss_round_mask((__v4sf)(__m128)(A), \
                                              (__v4sf)(__m128)(B), \
                                              (__v4sf)_mm_setzero_ps(), \
                                              (__mmask8)(M), (int)(R))

#define _mm_rsqrt28_ss(A, B) \
  _mm_rsqrt28_round_ss((A), (B), _MM_FROUND_CUR_DIRECTION)

#define _mm_mask_rsqrt28_ss(S, M, A, B) \
  _mm_mask_rsqrt28_round_ss((S), (M), (A), (B), _MM_FROUND_CUR_DIRECTION)

#define _mm_maskz_rsqrt28_ss(M, A, B) \
  _mm_maskz_rsqrt28_round_ss((M), (A), (B), _MM_FROUND_CUR_DIRECTION)

#define _mm_rsqrt28_round_sd(A, B, R) \
  (__m128d)__builtin_ia32_rsqrt28sd_round_mask((__v2df)(__m128d)(A), \
                                               (__v2df)(__m128d)(B), \
                                               (__v2df)_mm_setzero_pd(), \
                                               (__mmask8)-1, (int)(R))

#define _mm_mask_rsqrt28_round_sd(S, M, A, B, R) \
  (__m128d)__builtin_ia32_rsqrt28sd_round_mask((__v2df)(__m128d)(A), \
                                               (__v2df)(__m128d)(B), \
                                               (__v2df)(__m128d)(S), \
                                               (__mmask8)(M), (int)(R))

#define _mm_maskz_rsqrt28_round_sd(M, A, B, R) \
  (__m128d)__builtin_ia32_rsqrt28sd_round_mask((__v2df)(__m128d)(A), \
                                               (__v2df)(__m128d)(B), \
                                               (__v2df)_mm_setzero_pd(), \
                                               (__mmask8)(M), (int)(R))

#define _mm_rsqrt28_sd(A, B) \
  _mm_rsqrt28_round_sd((A), (B), _MM_FROUND_CUR_DIRECTION)

#define _mm_mask_rsqrt28_sd(S, M, A, B) \
  _mm_mask_rsqrt28_round_sd((S), (M), (A), (B), _MM_FROUND_CUR_DIRECTION)

#define _mm_maskz_rsqrt28_sd(M, A, B) \
  _mm_maskz_rsqrt28_round_sd((M), (A), (B), _MM_FROUND_CUR_DIRECTION)

/* rcp28 */
#define _mm512_rcp28_round_pd(A, R) \
  (__m512d)__builtin_ia32_rcp28pd_mask((__v8df)(__m512d)(A), \
                                       (__v8df)_mm512_setzero_pd(), \
                                       (__mmask8)-1, (int)(R))

#define _mm512_mask_rcp28_round_pd(S, M, A, R) \
  (__m512d)__builtin_ia32_rcp28pd_mask((__v8df)(__m512d)(A), \
                                       (__v8df)(__m512d)(S), (__mmask8)(M), \
                                       (int)(R))

#define _mm512_maskz_rcp28_round_pd(M, A, R) \
  (__m512d)__builtin_ia32_rcp28pd_mask((__v8df)(__m512d)(A), \
                                       (__v8df)_mm512_setzero_pd(), \
                                       (__mmask8)(M), (int)(R))

#define _mm512_rcp28_pd(A) \
  _mm512_rcp28_round_pd((A), _MM_FROUND_CUR_DIRECTION)

#define _mm512_mask_rcp28_pd(S, M, A) \
  _mm512_mask_rcp28_round_pd((S), (M), (A), _MM_FROUND_CUR_DIRECTION)

#define _mm512_maskz_rcp28_pd(M, A) \
  _mm512_maskz_rcp28_round_pd((M), (A), _MM_FROUND_CUR_DIRECTION)

#define _mm512_rcp28_round_ps(A, R) \
  (__m512)__builtin_ia32_rcp28ps_mask((__v16sf)(__m512)(A), \
                                      (__v16sf)_mm512_setzero_ps(), \
                                      (__mmask16)-1, (int)(R))

#define _mm512_mask_rcp28_round_ps(S, M, A, R) \
  (__m512)__builtin_ia32_rcp28ps_mask((__v16sf)(__m512)(A), \
                                      (__v16sf)(__m512)(S), (__mmask16)(M), \
                                      (int)(R))

#define _mm512_maskz_rcp28_round_ps(M, A, R) \
  (__m512)__builtin_ia32_rcp28ps_mask((__v16sf)(__m512)(A), \
                                      (__v16sf)_mm512_setzero_ps(), \
                                      (__mmask16)(M), (int)(R))

#define _mm512_rcp28_ps(A) \
  _mm512_rcp28_round_ps((A), _MM_FROUND_CUR_DIRECTION)

#define _mm512_mask_rcp28_ps(S, M, A) \
  _mm512_mask_rcp28_round_ps((S), (M), (A), _MM_FROUND_CUR_DIRECTION)

#define _mm512_maskz_rcp28_ps(M, A) \
  _mm512_maskz_rcp28_round_ps((M), (A), _MM_FROUND_CUR_DIRECTION)

#define _mm_rcp28_round_ss(A, B, R) \
  (__m128)__builtin_ia32_rcp28ss_round_mask((__v4sf)(__m128)(A), \
                                            (__v4sf)(__m128)(B), \
                                            (__v4sf)_mm_setzero_ps(), \
                                            (__mmask8)-1, (int)(R))

#define _mm_mask_rcp28_round_ss(S, M, A, B, R) \
  (__m128)__builtin_ia32_rcp28ss_round_mask((__v4sf)(__m128)(A), \
                                            (__v4sf)(__m128)(B), \
                                            (__v4sf)(__m128)(S), \
                                            (__mmask8)(M), (int)(R))

#define _mm_maskz_rcp28_round_ss(M, A, B, R) \
  (__m128)__builtin_ia32_rcp28ss_round_mask((__v4sf)(__m128)(A), \
                                            (__v4sf)(__m128)(B), \
                                            (__v4sf)_mm_setzero_ps(), \
                                            (__mmask8)(M), (int)(R))

#define _mm_rcp28_ss(A, B) \
  _mm_rcp28_round_ss((A), (B), _MM_FROUND_CUR_DIRECTION)

#define _mm_mask_rcp28_ss(S, M, A, B) \
  _mm_mask_rcp28_round_ss((S), (M), (A), (B), _MM_FROUND_CUR_DIRECTION)

#define _mm_maskz_rcp28_ss(M, A, B) \
  _mm_maskz_rcp28_round_ss((M), (A), (B), _MM_FROUND_CUR_DIRECTION)

#define _mm_rcp28_round_sd(A, B, R) \
  (__m128d)__builtin_ia32_rcp28sd_round_mask((__v2df)(__m128d)(A), \
                                             (__v2df)(__m128d)(B), \
                                             (__v2df)_mm_setzero_pd(), \
                                             (__mmask8)-1, (int)(R))

#define _mm_mask_rcp28_round_sd(S, M, A, B, R) \
  (__m128d)__builtin_ia32_rcp28sd_round_mask((__v2df)(__m128d)(A), \
                                             (__v2df)(__m128d)(B), \
                                             (__v2df)(__m128d)(S), \
                                             (__mmask8)(M), (int)(R))

#define _mm_maskz_rcp28_round_sd(M, A, B, R) \
  (__m128d)__builtin_ia32_rcp28sd_round_mask((__v2df)(__m128d)(A), \
                                             (__v2df)(__m128d)(B), \
                                             (__v2df)_mm_setzero_pd(), \
                                             (__mmask8)(M), (int)(R))

#define _mm_rcp28_sd(A, B) \
  _mm_rcp28_round_sd((A), (B), _MM_FROUND_CUR_DIRECTION)

#define _mm_mask_rcp28_sd(S, M, A, B) \
  _mm_mask_rcp28_round_sd((S), (M), (A), (B), _MM_FROUND_CUR_DIRECTION)

#define _mm_maskz_rcp28_sd(M, A, B) \
  _mm_maskz_rcp28_round_sd((M), (A), (B), _MM_FROUND_CUR_DIRECTION)

#endif /* __AVX512ERINTRIN_H */
