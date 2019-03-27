/*===---- avx512vldqintrin.h - AVX512VL and AVX512DQ intrinsics ------------===
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
#error "Never use <avx512vldqintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512VLDQINTRIN_H
#define __AVX512VLDQINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS128 __attribute__((__always_inline__, __nodebug__, __target__("avx512vl,avx512dq"), __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS256 __attribute__((__always_inline__, __nodebug__, __target__("avx512vl,avx512dq"), __min_vector_width__(256)))

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mullo_epi64 (__m256i __A, __m256i __B) {
  return (__m256i) ((__v4du) __A * (__v4du) __B);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_mullo_epi64(__m256i __W, __mmask8 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectq_256((__mmask8)__U,
                                             (__v4di)_mm256_mullo_epi64(__A, __B),
                                             (__v4di)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_mullo_epi64(__mmask8 __U, __m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_selectq_256((__mmask8)__U,
                                             (__v4di)_mm256_mullo_epi64(__A, __B),
                                             (__v4di)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mullo_epi64 (__m128i __A, __m128i __B) {
  return (__m128i) ((__v2du) __A * (__v2du) __B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_mullo_epi64(__m128i __W, __mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectq_128((__mmask8)__U,
                                             (__v2di)_mm_mullo_epi64(__A, __B),
                                             (__v2di)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_mullo_epi64(__mmask8 __U, __m128i __A, __m128i __B) {
  return (__m128i)__builtin_ia32_selectq_128((__mmask8)__U,
                                             (__v2di)_mm_mullo_epi64(__A, __B),
                                             (__v2di)_mm_setzero_si128());
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_mask_andnot_pd(__m256d __W, __mmask8 __U, __m256d __A, __m256d __B) {
  return (__m256d)__builtin_ia32_selectpd_256((__mmask8)__U,
                                              (__v4df)_mm256_andnot_pd(__A, __B),
                                              (__v4df)__W);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_maskz_andnot_pd(__mmask8 __U, __m256d __A, __m256d __B) {
  return (__m256d)__builtin_ia32_selectpd_256((__mmask8)__U,
                                              (__v4df)_mm256_andnot_pd(__A, __B),
                                              (__v4df)_mm256_setzero_pd());
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_mask_andnot_pd(__m128d __W, __mmask8 __U, __m128d __A, __m128d __B) {
  return (__m128d)__builtin_ia32_selectpd_128((__mmask8)__U,
                                              (__v2df)_mm_andnot_pd(__A, __B),
                                              (__v2df)__W);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_maskz_andnot_pd(__mmask8 __U, __m128d __A, __m128d __B) {
  return (__m128d)__builtin_ia32_selectpd_128((__mmask8)__U,
                                              (__v2df)_mm_andnot_pd(__A, __B),
                                              (__v2df)_mm_setzero_pd());
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_mask_andnot_ps(__m256 __W, __mmask8 __U, __m256 __A, __m256 __B) {
  return (__m256)__builtin_ia32_selectps_256((__mmask8)__U,
                                             (__v8sf)_mm256_andnot_ps(__A, __B),
                                             (__v8sf)__W);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_maskz_andnot_ps(__mmask8 __U, __m256 __A, __m256 __B) {
  return (__m256)__builtin_ia32_selectps_256((__mmask8)__U,
                                             (__v8sf)_mm256_andnot_ps(__A, __B),
                                             (__v8sf)_mm256_setzero_ps());
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_mask_andnot_ps(__m128 __W, __mmask8 __U, __m128 __A, __m128 __B) {
  return (__m128)__builtin_ia32_selectps_128((__mmask8)__U,
                                             (__v4sf)_mm_andnot_ps(__A, __B),
                                             (__v4sf)__W);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_maskz_andnot_ps(__mmask8 __U, __m128 __A, __m128 __B) {
  return (__m128)__builtin_ia32_selectps_128((__mmask8)__U,
                                             (__v4sf)_mm_andnot_ps(__A, __B),
                                             (__v4sf)_mm_setzero_ps());
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_mask_and_pd(__m256d __W, __mmask8 __U, __m256d __A, __m256d __B) {
  return (__m256d)__builtin_ia32_selectpd_256((__mmask8)__U,
                                              (__v4df)_mm256_and_pd(__A, __B),
                                              (__v4df)__W);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_maskz_and_pd(__mmask8 __U, __m256d __A, __m256d __B) {
  return (__m256d)__builtin_ia32_selectpd_256((__mmask8)__U,
                                              (__v4df)_mm256_and_pd(__A, __B),
                                              (__v4df)_mm256_setzero_pd());
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_mask_and_pd(__m128d __W, __mmask8 __U, __m128d __A, __m128d __B) {
  return (__m128d)__builtin_ia32_selectpd_128((__mmask8)__U,
                                              (__v2df)_mm_and_pd(__A, __B),
                                              (__v2df)__W);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_maskz_and_pd(__mmask8 __U, __m128d __A, __m128d __B) {
  return (__m128d)__builtin_ia32_selectpd_128((__mmask8)__U,
                                              (__v2df)_mm_and_pd(__A, __B),
                                              (__v2df)_mm_setzero_pd());
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_mask_and_ps(__m256 __W, __mmask8 __U, __m256 __A, __m256 __B) {
  return (__m256)__builtin_ia32_selectps_256((__mmask8)__U,
                                             (__v8sf)_mm256_and_ps(__A, __B),
                                             (__v8sf)__W);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_maskz_and_ps(__mmask8 __U, __m256 __A, __m256 __B) {
  return (__m256)__builtin_ia32_selectps_256((__mmask8)__U,
                                             (__v8sf)_mm256_and_ps(__A, __B),
                                             (__v8sf)_mm256_setzero_ps());
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_mask_and_ps(__m128 __W, __mmask8 __U, __m128 __A, __m128 __B) {
  return (__m128)__builtin_ia32_selectps_128((__mmask8)__U,
                                             (__v4sf)_mm_and_ps(__A, __B),
                                             (__v4sf)__W);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_maskz_and_ps(__mmask8 __U, __m128 __A, __m128 __B) {
  return (__m128)__builtin_ia32_selectps_128((__mmask8)__U,
                                             (__v4sf)_mm_and_ps(__A, __B),
                                             (__v4sf)_mm_setzero_ps());
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_mask_xor_pd(__m256d __W, __mmask8 __U, __m256d __A, __m256d __B) {
  return (__m256d)__builtin_ia32_selectpd_256((__mmask8)__U,
                                              (__v4df)_mm256_xor_pd(__A, __B),
                                              (__v4df)__W);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_maskz_xor_pd(__mmask8 __U, __m256d __A, __m256d __B) {
  return (__m256d)__builtin_ia32_selectpd_256((__mmask8)__U,
                                              (__v4df)_mm256_xor_pd(__A, __B),
                                              (__v4df)_mm256_setzero_pd());
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_mask_xor_pd(__m128d __W, __mmask8 __U, __m128d __A, __m128d __B) {
  return (__m128d)__builtin_ia32_selectpd_128((__mmask8)__U,
                                              (__v2df)_mm_xor_pd(__A, __B),
                                              (__v2df)__W);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_maskz_xor_pd (__mmask8 __U, __m128d __A, __m128d __B) {
  return (__m128d)__builtin_ia32_selectpd_128((__mmask8)__U,
                                              (__v2df)_mm_xor_pd(__A, __B),
                                              (__v2df)_mm_setzero_pd());
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_mask_xor_ps(__m256 __W, __mmask8 __U, __m256 __A, __m256 __B) {
  return (__m256)__builtin_ia32_selectps_256((__mmask8)__U,
                                             (__v8sf)_mm256_xor_ps(__A, __B),
                                             (__v8sf)__W);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_maskz_xor_ps(__mmask8 __U, __m256 __A, __m256 __B) {
  return (__m256)__builtin_ia32_selectps_256((__mmask8)__U,
                                             (__v8sf)_mm256_xor_ps(__A, __B),
                                             (__v8sf)_mm256_setzero_ps());
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_mask_xor_ps(__m128 __W, __mmask8 __U, __m128 __A, __m128 __B) {
  return (__m128)__builtin_ia32_selectps_128((__mmask8)__U,
                                             (__v4sf)_mm_xor_ps(__A, __B),
                                             (__v4sf)__W);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_maskz_xor_ps(__mmask8 __U, __m128 __A, __m128 __B) {
  return (__m128)__builtin_ia32_selectps_128((__mmask8)__U,
                                             (__v4sf)_mm_xor_ps(__A, __B),
                                             (__v4sf)_mm_setzero_ps());
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_mask_or_pd(__m256d __W, __mmask8 __U, __m256d __A, __m256d __B) {
  return (__m256d)__builtin_ia32_selectpd_256((__mmask8)__U,
                                              (__v4df)_mm256_or_pd(__A, __B),
                                              (__v4df)__W);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_maskz_or_pd(__mmask8 __U, __m256d __A, __m256d __B) {
  return (__m256d)__builtin_ia32_selectpd_256((__mmask8)__U,
                                              (__v4df)_mm256_or_pd(__A, __B),
                                              (__v4df)_mm256_setzero_pd());
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_mask_or_pd(__m128d __W, __mmask8 __U, __m128d __A, __m128d __B) {
  return (__m128d)__builtin_ia32_selectpd_128((__mmask8)__U,
                                              (__v2df)_mm_or_pd(__A, __B),
                                              (__v2df)__W);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_maskz_or_pd(__mmask8 __U, __m128d __A, __m128d __B) {
  return (__m128d)__builtin_ia32_selectpd_128((__mmask8)__U,
                                              (__v2df)_mm_or_pd(__A, __B),
                                              (__v2df)_mm_setzero_pd());
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_mask_or_ps(__m256 __W, __mmask8 __U, __m256 __A, __m256 __B) {
  return (__m256)__builtin_ia32_selectps_256((__mmask8)__U,
                                             (__v8sf)_mm256_or_ps(__A, __B),
                                             (__v8sf)__W);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_maskz_or_ps(__mmask8 __U, __m256 __A, __m256 __B) {
  return (__m256)__builtin_ia32_selectps_256((__mmask8)__U,
                                             (__v8sf)_mm256_or_ps(__A, __B),
                                             (__v8sf)_mm256_setzero_ps());
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_mask_or_ps(__m128 __W, __mmask8 __U, __m128 __A, __m128 __B) {
  return (__m128)__builtin_ia32_selectps_128((__mmask8)__U,
                                             (__v4sf)_mm_or_ps(__A, __B),
                                             (__v4sf)__W);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_maskz_or_ps(__mmask8 __U, __m128 __A, __m128 __B) {
  return (__m128)__builtin_ia32_selectps_128((__mmask8)__U,
                                             (__v4sf)_mm_or_ps(__A, __B),
                                             (__v4sf)_mm_setzero_ps());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_cvtpd_epi64 (__m128d __A) {
  return (__m128i) __builtin_ia32_cvtpd2qq128_mask ((__v2df) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) -1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvtpd_epi64 (__m128i __W, __mmask8 __U, __m128d __A) {
  return (__m128i) __builtin_ia32_cvtpd2qq128_mask ((__v2df) __A,
                (__v2di) __W,
                (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvtpd_epi64 (__mmask8 __U, __m128d __A) {
  return (__m128i) __builtin_ia32_cvtpd2qq128_mask ((__v2df) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvtpd_epi64 (__m256d __A) {
  return (__m256i) __builtin_ia32_cvtpd2qq256_mask ((__v4df) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) -1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvtpd_epi64 (__m256i __W, __mmask8 __U, __m256d __A) {
  return (__m256i) __builtin_ia32_cvtpd2qq256_mask ((__v4df) __A,
                (__v4di) __W,
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtpd_epi64 (__mmask8 __U, __m256d __A) {
  return (__m256i) __builtin_ia32_cvtpd2qq256_mask ((__v4df) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_cvtpd_epu64 (__m128d __A) {
  return (__m128i) __builtin_ia32_cvtpd2uqq128_mask ((__v2df) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) -1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvtpd_epu64 (__m128i __W, __mmask8 __U, __m128d __A) {
  return (__m128i) __builtin_ia32_cvtpd2uqq128_mask ((__v2df) __A,
                (__v2di) __W,
                (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvtpd_epu64 (__mmask8 __U, __m128d __A) {
  return (__m128i) __builtin_ia32_cvtpd2uqq128_mask ((__v2df) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvtpd_epu64 (__m256d __A) {
  return (__m256i) __builtin_ia32_cvtpd2uqq256_mask ((__v4df) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) -1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvtpd_epu64 (__m256i __W, __mmask8 __U, __m256d __A) {
  return (__m256i) __builtin_ia32_cvtpd2uqq256_mask ((__v4df) __A,
                (__v4di) __W,
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtpd_epu64 (__mmask8 __U, __m256d __A) {
  return (__m256i) __builtin_ia32_cvtpd2uqq256_mask ((__v4df) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_cvtps_epi64 (__m128 __A) {
  return (__m128i) __builtin_ia32_cvtps2qq128_mask ((__v4sf) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) -1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvtps_epi64 (__m128i __W, __mmask8 __U, __m128 __A) {
  return (__m128i) __builtin_ia32_cvtps2qq128_mask ((__v4sf) __A,
                (__v2di) __W,
                (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvtps_epi64 (__mmask8 __U, __m128 __A) {
  return (__m128i) __builtin_ia32_cvtps2qq128_mask ((__v4sf) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvtps_epi64 (__m128 __A) {
  return (__m256i) __builtin_ia32_cvtps2qq256_mask ((__v4sf) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) -1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvtps_epi64 (__m256i __W, __mmask8 __U, __m128 __A) {
  return (__m256i) __builtin_ia32_cvtps2qq256_mask ((__v4sf) __A,
                (__v4di) __W,
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtps_epi64 (__mmask8 __U, __m128 __A) {
  return (__m256i) __builtin_ia32_cvtps2qq256_mask ((__v4sf) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_cvtps_epu64 (__m128 __A) {
  return (__m128i) __builtin_ia32_cvtps2uqq128_mask ((__v4sf) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) -1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvtps_epu64 (__m128i __W, __mmask8 __U, __m128 __A) {
  return (__m128i) __builtin_ia32_cvtps2uqq128_mask ((__v4sf) __A,
                (__v2di) __W,
                (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvtps_epu64 (__mmask8 __U, __m128 __A) {
  return (__m128i) __builtin_ia32_cvtps2uqq128_mask ((__v4sf) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvtps_epu64 (__m128 __A) {
  return (__m256i) __builtin_ia32_cvtps2uqq256_mask ((__v4sf) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) -1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvtps_epu64 (__m256i __W, __mmask8 __U, __m128 __A) {
  return (__m256i) __builtin_ia32_cvtps2uqq256_mask ((__v4sf) __A,
                (__v4di) __W,
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtps_epu64 (__mmask8 __U, __m128 __A) {
  return (__m256i) __builtin_ia32_cvtps2uqq256_mask ((__v4sf) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) __U);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_cvtepi64_pd (__m128i __A) {
  return (__m128d)__builtin_convertvector((__v2di)__A, __v2df);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_mask_cvtepi64_pd (__m128d __W, __mmask8 __U, __m128i __A) {
  return (__m128d)__builtin_ia32_selectpd_128((__mmask8)__U,
                                              (__v2df)_mm_cvtepi64_pd(__A),
                                              (__v2df)__W);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_maskz_cvtepi64_pd (__mmask8 __U, __m128i __A) {
  return (__m128d)__builtin_ia32_selectpd_128((__mmask8)__U,
                                              (__v2df)_mm_cvtepi64_pd(__A),
                                              (__v2df)_mm_setzero_pd());
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_cvtepi64_pd (__m256i __A) {
  return (__m256d)__builtin_convertvector((__v4di)__A, __v4df);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_mask_cvtepi64_pd (__m256d __W, __mmask8 __U, __m256i __A) {
  return (__m256d)__builtin_ia32_selectpd_256((__mmask8)__U,
                                              (__v4df)_mm256_cvtepi64_pd(__A),
                                              (__v4df)__W);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtepi64_pd (__mmask8 __U, __m256i __A) {
  return (__m256d)__builtin_ia32_selectpd_256((__mmask8)__U,
                                              (__v4df)_mm256_cvtepi64_pd(__A),
                                              (__v4df)_mm256_setzero_pd());
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_cvtepi64_ps (__m128i __A) {
  return (__m128) __builtin_ia32_cvtqq2ps128_mask ((__v2di) __A,
                (__v4sf) _mm_setzero_ps(),
                (__mmask8) -1);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_mask_cvtepi64_ps (__m128 __W, __mmask8 __U, __m128i __A) {
  return (__m128) __builtin_ia32_cvtqq2ps128_mask ((__v2di) __A,
                (__v4sf) __W,
                (__mmask8) __U);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_maskz_cvtepi64_ps (__mmask8 __U, __m128i __A) {
  return (__m128) __builtin_ia32_cvtqq2ps128_mask ((__v2di) __A,
                (__v4sf) _mm_setzero_ps(),
                (__mmask8) __U);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS256
_mm256_cvtepi64_ps (__m256i __A) {
  return (__m128) __builtin_ia32_cvtqq2ps256_mask ((__v4di) __A,
                (__v4sf) _mm_setzero_ps(),
                (__mmask8) -1);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS256
_mm256_mask_cvtepi64_ps (__m128 __W, __mmask8 __U, __m256i __A) {
  return (__m128) __builtin_ia32_cvtqq2ps256_mask ((__v4di) __A,
                (__v4sf) __W,
                (__mmask8) __U);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtepi64_ps (__mmask8 __U, __m256i __A) {
  return (__m128) __builtin_ia32_cvtqq2ps256_mask ((__v4di) __A,
                (__v4sf) _mm_setzero_ps(),
                (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_cvttpd_epi64 (__m128d __A) {
  return (__m128i) __builtin_ia32_cvttpd2qq128_mask ((__v2df) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) -1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvttpd_epi64 (__m128i __W, __mmask8 __U, __m128d __A) {
  return (__m128i) __builtin_ia32_cvttpd2qq128_mask ((__v2df) __A,
                (__v2di) __W,
                (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvttpd_epi64 (__mmask8 __U, __m128d __A) {
  return (__m128i) __builtin_ia32_cvttpd2qq128_mask ((__v2df) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvttpd_epi64 (__m256d __A) {
  return (__m256i) __builtin_ia32_cvttpd2qq256_mask ((__v4df) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) -1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvttpd_epi64 (__m256i __W, __mmask8 __U, __m256d __A) {
  return (__m256i) __builtin_ia32_cvttpd2qq256_mask ((__v4df) __A,
                (__v4di) __W,
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvttpd_epi64 (__mmask8 __U, __m256d __A) {
  return (__m256i) __builtin_ia32_cvttpd2qq256_mask ((__v4df) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_cvttpd_epu64 (__m128d __A) {
  return (__m128i) __builtin_ia32_cvttpd2uqq128_mask ((__v2df) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) -1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvttpd_epu64 (__m128i __W, __mmask8 __U, __m128d __A) {
  return (__m128i) __builtin_ia32_cvttpd2uqq128_mask ((__v2df) __A,
                (__v2di) __W,
                (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvttpd_epu64 (__mmask8 __U, __m128d __A) {
  return (__m128i) __builtin_ia32_cvttpd2uqq128_mask ((__v2df) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvttpd_epu64 (__m256d __A) {
  return (__m256i) __builtin_ia32_cvttpd2uqq256_mask ((__v4df) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) -1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvttpd_epu64 (__m256i __W, __mmask8 __U, __m256d __A) {
  return (__m256i) __builtin_ia32_cvttpd2uqq256_mask ((__v4df) __A,
                (__v4di) __W,
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvttpd_epu64 (__mmask8 __U, __m256d __A) {
  return (__m256i) __builtin_ia32_cvttpd2uqq256_mask ((__v4df) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_cvttps_epi64 (__m128 __A) {
  return (__m128i) __builtin_ia32_cvttps2qq128_mask ((__v4sf) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) -1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvttps_epi64 (__m128i __W, __mmask8 __U, __m128 __A) {
  return (__m128i) __builtin_ia32_cvttps2qq128_mask ((__v4sf) __A,
                (__v2di) __W,
                (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvttps_epi64 (__mmask8 __U, __m128 __A) {
  return (__m128i) __builtin_ia32_cvttps2qq128_mask ((__v4sf) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvttps_epi64 (__m128 __A) {
  return (__m256i) __builtin_ia32_cvttps2qq256_mask ((__v4sf) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) -1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvttps_epi64 (__m256i __W, __mmask8 __U, __m128 __A) {
  return (__m256i) __builtin_ia32_cvttps2qq256_mask ((__v4sf) __A,
                (__v4di) __W,
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvttps_epi64 (__mmask8 __U, __m128 __A) {
  return (__m256i) __builtin_ia32_cvttps2qq256_mask ((__v4sf) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_cvttps_epu64 (__m128 __A) {
  return (__m128i) __builtin_ia32_cvttps2uqq128_mask ((__v4sf) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) -1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_cvttps_epu64 (__m128i __W, __mmask8 __U, __m128 __A) {
  return (__m128i) __builtin_ia32_cvttps2uqq128_mask ((__v4sf) __A,
                (__v2di) __W,
                (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_cvttps_epu64 (__mmask8 __U, __m128 __A) {
  return (__m128i) __builtin_ia32_cvttps2uqq128_mask ((__v4sf) __A,
                (__v2di) _mm_setzero_si128(),
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cvttps_epu64 (__m128 __A) {
  return (__m256i) __builtin_ia32_cvttps2uqq256_mask ((__v4sf) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) -1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_cvttps_epu64 (__m256i __W, __mmask8 __U, __m128 __A) {
  return (__m256i) __builtin_ia32_cvttps2uqq256_mask ((__v4sf) __A,
                (__v4di) __W,
                (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_cvttps_epu64 (__mmask8 __U, __m128 __A) {
  return (__m256i) __builtin_ia32_cvttps2uqq256_mask ((__v4sf) __A,
                (__v4di) _mm256_setzero_si256(),
                (__mmask8) __U);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_cvtepu64_pd (__m128i __A) {
  return (__m128d)__builtin_convertvector((__v2du)__A, __v2df);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_mask_cvtepu64_pd (__m128d __W, __mmask8 __U, __m128i __A) {
  return (__m128d)__builtin_ia32_selectpd_128((__mmask8)__U,
                                              (__v2df)_mm_cvtepu64_pd(__A),
                                              (__v2df)__W);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_maskz_cvtepu64_pd (__mmask8 __U, __m128i __A) {
  return (__m128d)__builtin_ia32_selectpd_128((__mmask8)__U,
                                              (__v2df)_mm_cvtepu64_pd(__A),
                                              (__v2df)_mm_setzero_pd());
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_cvtepu64_pd (__m256i __A) {
  return (__m256d)__builtin_convertvector((__v4du)__A, __v4df);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_mask_cvtepu64_pd (__m256d __W, __mmask8 __U, __m256i __A) {
  return (__m256d)__builtin_ia32_selectpd_256((__mmask8)__U,
                                              (__v4df)_mm256_cvtepu64_pd(__A),
                                              (__v4df)__W);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtepu64_pd (__mmask8 __U, __m256i __A) {
  return (__m256d)__builtin_ia32_selectpd_256((__mmask8)__U,
                                              (__v4df)_mm256_cvtepu64_pd(__A),
                                              (__v4df)_mm256_setzero_pd());
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_cvtepu64_ps (__m128i __A) {
  return (__m128) __builtin_ia32_cvtuqq2ps128_mask ((__v2di) __A,
                (__v4sf) _mm_setzero_ps(),
                (__mmask8) -1);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_mask_cvtepu64_ps (__m128 __W, __mmask8 __U, __m128i __A) {
  return (__m128) __builtin_ia32_cvtuqq2ps128_mask ((__v2di) __A,
                (__v4sf) __W,
                (__mmask8) __U);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_maskz_cvtepu64_ps (__mmask8 __U, __m128i __A) {
  return (__m128) __builtin_ia32_cvtuqq2ps128_mask ((__v2di) __A,
                (__v4sf) _mm_setzero_ps(),
                (__mmask8) __U);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS256
_mm256_cvtepu64_ps (__m256i __A) {
  return (__m128) __builtin_ia32_cvtuqq2ps256_mask ((__v4di) __A,
                (__v4sf) _mm_setzero_ps(),
                (__mmask8) -1);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS256
_mm256_mask_cvtepu64_ps (__m128 __W, __mmask8 __U, __m256i __A) {
  return (__m128) __builtin_ia32_cvtuqq2ps256_mask ((__v4di) __A,
                (__v4sf) __W,
                (__mmask8) __U);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS256
_mm256_maskz_cvtepu64_ps (__mmask8 __U, __m256i __A) {
  return (__m128) __builtin_ia32_cvtuqq2ps256_mask ((__v4di) __A,
                (__v4sf) _mm_setzero_ps(),
                (__mmask8) __U);
}

#define _mm_range_pd(A, B, C) \
  (__m128d)__builtin_ia32_rangepd128_mask((__v2df)(__m128d)(A), \
                                          (__v2df)(__m128d)(B), (int)(C), \
                                          (__v2df)_mm_setzero_pd(), \
                                          (__mmask8)-1)

#define _mm_mask_range_pd(W, U, A, B, C) \
  (__m128d)__builtin_ia32_rangepd128_mask((__v2df)(__m128d)(A), \
                                          (__v2df)(__m128d)(B), (int)(C), \
                                          (__v2df)(__m128d)(W), \
                                          (__mmask8)(U))

#define _mm_maskz_range_pd(U, A, B, C) \
  (__m128d)__builtin_ia32_rangepd128_mask((__v2df)(__m128d)(A), \
                                          (__v2df)(__m128d)(B), (int)(C), \
                                          (__v2df)_mm_setzero_pd(), \
                                          (__mmask8)(U))

#define _mm256_range_pd(A, B, C) \
  (__m256d)__builtin_ia32_rangepd256_mask((__v4df)(__m256d)(A), \
                                          (__v4df)(__m256d)(B), (int)(C), \
                                          (__v4df)_mm256_setzero_pd(), \
                                          (__mmask8)-1)

#define _mm256_mask_range_pd(W, U, A, B, C) \
  (__m256d)__builtin_ia32_rangepd256_mask((__v4df)(__m256d)(A), \
                                          (__v4df)(__m256d)(B), (int)(C), \
                                          (__v4df)(__m256d)(W), \
                                          (__mmask8)(U))

#define _mm256_maskz_range_pd(U, A, B, C) \
  (__m256d)__builtin_ia32_rangepd256_mask((__v4df)(__m256d)(A), \
                                          (__v4df)(__m256d)(B), (int)(C), \
                                          (__v4df)_mm256_setzero_pd(), \
                                          (__mmask8)(U))

#define _mm_range_ps(A, B, C) \
  (__m128)__builtin_ia32_rangeps128_mask((__v4sf)(__m128)(A), \
                                         (__v4sf)(__m128)(B), (int)(C), \
                                         (__v4sf)_mm_setzero_ps(), \
                                         (__mmask8)-1)

#define _mm_mask_range_ps(W, U, A, B, C) \
  (__m128)__builtin_ia32_rangeps128_mask((__v4sf)(__m128)(A), \
                                         (__v4sf)(__m128)(B), (int)(C), \
                                         (__v4sf)(__m128)(W), (__mmask8)(U))

#define _mm_maskz_range_ps(U, A, B, C) \
  (__m128)__builtin_ia32_rangeps128_mask((__v4sf)(__m128)(A), \
                                         (__v4sf)(__m128)(B), (int)(C), \
                                         (__v4sf)_mm_setzero_ps(), \
                                         (__mmask8)(U))

#define _mm256_range_ps(A, B, C) \
  (__m256)__builtin_ia32_rangeps256_mask((__v8sf)(__m256)(A), \
                                         (__v8sf)(__m256)(B), (int)(C), \
                                         (__v8sf)_mm256_setzero_ps(), \
                                         (__mmask8)-1)

#define _mm256_mask_range_ps(W, U, A, B, C) \
  (__m256)__builtin_ia32_rangeps256_mask((__v8sf)(__m256)(A), \
                                         (__v8sf)(__m256)(B), (int)(C), \
                                         (__v8sf)(__m256)(W), (__mmask8)(U))

#define _mm256_maskz_range_ps(U, A, B, C) \
  (__m256)__builtin_ia32_rangeps256_mask((__v8sf)(__m256)(A), \
                                         (__v8sf)(__m256)(B), (int)(C), \
                                         (__v8sf)_mm256_setzero_ps(), \
                                         (__mmask8)(U))

#define _mm_reduce_pd(A, B) \
  (__m128d)__builtin_ia32_reducepd128_mask((__v2df)(__m128d)(A), (int)(B), \
                                           (__v2df)_mm_setzero_pd(), \
                                           (__mmask8)-1)

#define _mm_mask_reduce_pd(W, U, A, B) \
  (__m128d)__builtin_ia32_reducepd128_mask((__v2df)(__m128d)(A), (int)(B), \
                                           (__v2df)(__m128d)(W), \
                                           (__mmask8)(U))

#define _mm_maskz_reduce_pd(U, A, B) \
  (__m128d)__builtin_ia32_reducepd128_mask((__v2df)(__m128d)(A), (int)(B), \
                                           (__v2df)_mm_setzero_pd(), \
                                           (__mmask8)(U))

#define _mm256_reduce_pd(A, B) \
  (__m256d)__builtin_ia32_reducepd256_mask((__v4df)(__m256d)(A), (int)(B), \
                                           (__v4df)_mm256_setzero_pd(), \
                                           (__mmask8)-1)

#define _mm256_mask_reduce_pd(W, U, A, B) \
  (__m256d)__builtin_ia32_reducepd256_mask((__v4df)(__m256d)(A), (int)(B), \
                                           (__v4df)(__m256d)(W), \
                                           (__mmask8)(U))

#define _mm256_maskz_reduce_pd(U, A, B) \
  (__m256d)__builtin_ia32_reducepd256_mask((__v4df)(__m256d)(A), (int)(B), \
                                           (__v4df)_mm256_setzero_pd(), \
                                           (__mmask8)(U))

#define _mm_reduce_ps(A, B) \
  (__m128)__builtin_ia32_reduceps128_mask((__v4sf)(__m128)(A), (int)(B), \
                                          (__v4sf)_mm_setzero_ps(), \
                                          (__mmask8)-1)

#define _mm_mask_reduce_ps(W, U, A, B) \
  (__m128)__builtin_ia32_reduceps128_mask((__v4sf)(__m128)(A), (int)(B), \
                                          (__v4sf)(__m128)(W), \
                                          (__mmask8)(U))

#define _mm_maskz_reduce_ps(U, A, B) \
  (__m128)__builtin_ia32_reduceps128_mask((__v4sf)(__m128)(A), (int)(B), \
                                          (__v4sf)_mm_setzero_ps(), \
                                          (__mmask8)(U))

#define _mm256_reduce_ps(A, B) \
  (__m256)__builtin_ia32_reduceps256_mask((__v8sf)(__m256)(A), (int)(B), \
                                          (__v8sf)_mm256_setzero_ps(), \
                                          (__mmask8)-1)

#define _mm256_mask_reduce_ps(W, U, A, B) \
  (__m256)__builtin_ia32_reduceps256_mask((__v8sf)(__m256)(A), (int)(B), \
                                          (__v8sf)(__m256)(W), \
                                          (__mmask8)(U))

#define _mm256_maskz_reduce_ps(U, A, B) \
  (__m256)__builtin_ia32_reduceps256_mask((__v8sf)(__m256)(A), (int)(B), \
                                          (__v8sf)_mm256_setzero_ps(), \
                                          (__mmask8)(U))

static __inline__ __mmask8 __DEFAULT_FN_ATTRS128
_mm_movepi32_mask (__m128i __A)
{
  return (__mmask8) __builtin_ia32_cvtd2mask128 ((__v4si) __A);
}

static __inline__ __mmask8 __DEFAULT_FN_ATTRS256
_mm256_movepi32_mask (__m256i __A)
{
  return (__mmask8) __builtin_ia32_cvtd2mask256 ((__v8si) __A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_movm_epi32 (__mmask8 __A)
{
  return (__m128i) __builtin_ia32_cvtmask2d128 (__A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_movm_epi32 (__mmask8 __A)
{
  return (__m256i) __builtin_ia32_cvtmask2d256 (__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_movm_epi64 (__mmask8 __A)
{
  return (__m128i) __builtin_ia32_cvtmask2q128 (__A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_movm_epi64 (__mmask8 __A)
{
  return (__m256i) __builtin_ia32_cvtmask2q256 (__A);
}

static __inline__ __mmask8 __DEFAULT_FN_ATTRS128
_mm_movepi64_mask (__m128i __A)
{
  return (__mmask8) __builtin_ia32_cvtq2mask128 ((__v2di) __A);
}

static __inline__ __mmask8 __DEFAULT_FN_ATTRS256
_mm256_movepi64_mask (__m256i __A)
{
  return (__mmask8) __builtin_ia32_cvtq2mask256 ((__v4di) __A);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_broadcast_f32x2 (__m128 __A)
{
  return (__m256)__builtin_shufflevector((__v4sf)__A, (__v4sf)__A,
                                         0, 1, 0, 1, 0, 1, 0, 1);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_mask_broadcast_f32x2 (__m256 __O, __mmask8 __M, __m128 __A)
{
  return (__m256)__builtin_ia32_selectps_256((__mmask8)__M,
                                             (__v8sf)_mm256_broadcast_f32x2(__A),
                                             (__v8sf)__O);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_maskz_broadcast_f32x2 (__mmask8 __M, __m128 __A)
{
  return (__m256)__builtin_ia32_selectps_256((__mmask8)__M,
                                             (__v8sf)_mm256_broadcast_f32x2(__A),
                                             (__v8sf)_mm256_setzero_ps());
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_broadcast_f64x2(__m128d __A)
{
  return (__m256d)__builtin_shufflevector((__v2df)__A, (__v2df)__A,
                                          0, 1, 0, 1);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_mask_broadcast_f64x2(__m256d __O, __mmask8 __M, __m128d __A)
{
  return (__m256d)__builtin_ia32_selectpd_256((__mmask8)__M,
                                            (__v4df)_mm256_broadcast_f64x2(__A),
                                            (__v4df)__O);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_maskz_broadcast_f64x2 (__mmask8 __M, __m128d __A)
{
  return (__m256d)__builtin_ia32_selectpd_256((__mmask8)__M,
                                            (__v4df)_mm256_broadcast_f64x2(__A),
                                            (__v4df)_mm256_setzero_pd());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_broadcast_i32x2 (__m128i __A)
{
  return (__m128i)__builtin_shufflevector((__v4si)__A, (__v4si)__A,
                                          0, 1, 0, 1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_broadcast_i32x2 (__m128i __O, __mmask8 __M, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectd_128((__mmask8)__M,
                                             (__v4si)_mm_broadcast_i32x2(__A),
                                             (__v4si)__O);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_broadcast_i32x2 (__mmask8 __M, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectd_128((__mmask8)__M,
                                             (__v4si)_mm_broadcast_i32x2(__A),
                                             (__v4si)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_broadcast_i32x2 (__m128i __A)
{
  return (__m256i)__builtin_shufflevector((__v4si)__A, (__v4si)__A,
                                          0, 1, 0, 1, 0, 1, 0, 1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_broadcast_i32x2 (__m256i __O, __mmask8 __M, __m128i __A)
{
  return (__m256i)__builtin_ia32_selectd_256((__mmask8)__M,
                                             (__v8si)_mm256_broadcast_i32x2(__A),
                                             (__v8si)__O);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_broadcast_i32x2 (__mmask8 __M, __m128i __A)
{
  return (__m256i)__builtin_ia32_selectd_256((__mmask8)__M,
                                             (__v8si)_mm256_broadcast_i32x2(__A),
                                             (__v8si)_mm256_setzero_si256());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_broadcast_i64x2(__m128i __A)
{
  return (__m256i)__builtin_shufflevector((__v2di)__A, (__v2di)__A,
                                          0, 1, 0, 1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_broadcast_i64x2(__m256i __O, __mmask8 __M, __m128i __A)
{
  return (__m256i)__builtin_ia32_selectq_256((__mmask8)__M,
                                            (__v4di)_mm256_broadcast_i64x2(__A),
                                            (__v4di)__O);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_broadcast_i64x2 (__mmask8 __M, __m128i __A)
{
  return (__m256i)__builtin_ia32_selectq_256((__mmask8)__M,
                                            (__v4di)_mm256_broadcast_i64x2(__A),
                                            (__v4di)_mm256_setzero_si256());
}

#define _mm256_extractf64x2_pd(A, imm) \
  (__m128d)__builtin_ia32_extractf64x2_256_mask((__v4df)(__m256d)(A), \
                                                (int)(imm), \
                                                (__v2df)_mm_undefined_pd(), \
                                                (__mmask8)-1)

#define _mm256_mask_extractf64x2_pd(W, U, A, imm) \
  (__m128d)__builtin_ia32_extractf64x2_256_mask((__v4df)(__m256d)(A), \
                                                (int)(imm), \
                                                (__v2df)(__m128d)(W), \
                                                (__mmask8)(U))

#define _mm256_maskz_extractf64x2_pd(U, A, imm) \
  (__m128d)__builtin_ia32_extractf64x2_256_mask((__v4df)(__m256d)(A), \
                                                (int)(imm), \
                                                (__v2df)_mm_setzero_pd(), \
                                                (__mmask8)(U))

#define _mm256_extracti64x2_epi64(A, imm) \
  (__m128i)__builtin_ia32_extracti64x2_256_mask((__v4di)(__m256i)(A), \
                                                (int)(imm), \
                                                (__v2di)_mm_undefined_si128(), \
                                                (__mmask8)-1)

#define _mm256_mask_extracti64x2_epi64(W, U, A, imm) \
  (__m128i)__builtin_ia32_extracti64x2_256_mask((__v4di)(__m256i)(A), \
                                                (int)(imm), \
                                                (__v2di)(__m128i)(W), \
                                                (__mmask8)(U))

#define _mm256_maskz_extracti64x2_epi64(U, A, imm) \
  (__m128i)__builtin_ia32_extracti64x2_256_mask((__v4di)(__m256i)(A), \
                                                (int)(imm), \
                                                (__v2di)_mm_setzero_si128(), \
                                                (__mmask8)(U))

#define _mm256_insertf64x2(A, B, imm) \
  (__m256d)__builtin_ia32_insertf64x2_256((__v4df)(__m256d)(A), \
                                          (__v2df)(__m128d)(B), (int)(imm))

#define _mm256_mask_insertf64x2(W, U, A, B, imm) \
  (__m256d)__builtin_ia32_selectpd_256((__mmask8)(U), \
                                  (__v4df)_mm256_insertf64x2((A), (B), (imm)), \
                                  (__v4df)(__m256d)(W))

#define _mm256_maskz_insertf64x2(U, A, B, imm) \
  (__m256d)__builtin_ia32_selectpd_256((__mmask8)(U), \
                                  (__v4df)_mm256_insertf64x2((A), (B), (imm)), \
                                  (__v4df)_mm256_setzero_pd())

#define _mm256_inserti64x2(A, B, imm) \
  (__m256i)__builtin_ia32_inserti64x2_256((__v4di)(__m256i)(A), \
                                          (__v2di)(__m128i)(B), (int)(imm))

#define _mm256_mask_inserti64x2(W, U, A, B, imm) \
  (__m256i)__builtin_ia32_selectq_256((__mmask8)(U), \
                                  (__v4di)_mm256_inserti64x2((A), (B), (imm)), \
                                  (__v4di)(__m256i)(W))

#define _mm256_maskz_inserti64x2(U, A, B, imm) \
  (__m256i)__builtin_ia32_selectq_256((__mmask8)(U), \
                                  (__v4di)_mm256_inserti64x2((A), (B), (imm)), \
                                  (__v4di)_mm256_setzero_si256())

#define _mm_mask_fpclass_pd_mask(U, A, imm) \
  (__mmask8)__builtin_ia32_fpclasspd128_mask((__v2df)(__m128d)(A), (int)(imm), \
                                             (__mmask8)(U))

#define _mm_fpclass_pd_mask(A, imm) \
  (__mmask8)__builtin_ia32_fpclasspd128_mask((__v2df)(__m128d)(A), (int)(imm), \
                                             (__mmask8)-1)

#define _mm256_mask_fpclass_pd_mask(U, A, imm) \
  (__mmask8)__builtin_ia32_fpclasspd256_mask((__v4df)(__m256d)(A), (int)(imm), \
                                             (__mmask8)(U))

#define _mm256_fpclass_pd_mask(A, imm) \
  (__mmask8)__builtin_ia32_fpclasspd256_mask((__v4df)(__m256d)(A), (int)(imm), \
                                             (__mmask8)-1)

#define _mm_mask_fpclass_ps_mask(U, A, imm) \
  (__mmask8)__builtin_ia32_fpclassps128_mask((__v4sf)(__m128)(A), (int)(imm), \
                                             (__mmask8)(U))

#define _mm_fpclass_ps_mask(A, imm) \
  (__mmask8)__builtin_ia32_fpclassps128_mask((__v4sf)(__m128)(A), (int)(imm), \
                                             (__mmask8)-1)

#define _mm256_mask_fpclass_ps_mask(U, A, imm) \
  (__mmask8)__builtin_ia32_fpclassps256_mask((__v8sf)(__m256)(A), (int)(imm), \
                                             (__mmask8)(U))

#define _mm256_fpclass_ps_mask(A, imm) \
  (__mmask8)__builtin_ia32_fpclassps256_mask((__v8sf)(__m256)(A), (int)(imm), \
                                             (__mmask8)-1)

#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256

#endif
