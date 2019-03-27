/*===---- avx512vlcdintrin.h - AVX512VL and AVX512CD intrinsics ------------===
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
#error "Never use <avx512vlcdintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512VLCDINTRIN_H
#define __AVX512VLCDINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS128 __attribute__((__always_inline__, __nodebug__, __target__("avx512vl,avx512cd"), __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS256 __attribute__((__always_inline__, __nodebug__, __target__("avx512vl,avx512cd"), __min_vector_width__(256)))


static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_broadcastmb_epi64 (__mmask8 __A)
{
  return (__m128i) _mm_set1_epi64x((long long) __A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_broadcastmb_epi64 (__mmask8 __A)
{
  return (__m256i) _mm256_set1_epi64x((long long)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_broadcastmw_epi32 (__mmask16 __A)
{
  return (__m128i) _mm_set1_epi32((int)__A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_broadcastmw_epi32 (__mmask16 __A)
{
  return (__m256i) _mm256_set1_epi32((int)__A);
}


static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_conflict_epi64 (__m128i __A)
{
  return (__m128i) __builtin_ia32_vpconflictdi_128_mask ((__v2di) __A,
               (__v2di) _mm_undefined_si128 (),
               (__mmask8) -1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_conflict_epi64 (__m128i __W, __mmask8 __U, __m128i __A)
{
  return (__m128i) __builtin_ia32_vpconflictdi_128_mask ((__v2di) __A,
               (__v2di) __W,
               (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_conflict_epi64 (__mmask8 __U, __m128i __A)
{
  return (__m128i) __builtin_ia32_vpconflictdi_128_mask ((__v2di) __A,
               (__v2di)
               _mm_setzero_si128 (),
               (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_conflict_epi64 (__m256i __A)
{
  return (__m256i) __builtin_ia32_vpconflictdi_256_mask ((__v4di) __A,
               (__v4di)  _mm256_undefined_si256 (),
               (__mmask8) -1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_conflict_epi64 (__m256i __W, __mmask8 __U, __m256i __A)
{
  return (__m256i) __builtin_ia32_vpconflictdi_256_mask ((__v4di) __A,
               (__v4di) __W,
               (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_conflict_epi64 (__mmask8 __U, __m256i __A)
{
  return (__m256i) __builtin_ia32_vpconflictdi_256_mask ((__v4di) __A,
               (__v4di) _mm256_setzero_si256 (),
               (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_conflict_epi32 (__m128i __A)
{
  return (__m128i) __builtin_ia32_vpconflictsi_128_mask ((__v4si) __A,
               (__v4si) _mm_undefined_si128 (),
               (__mmask8) -1);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_conflict_epi32 (__m128i __W, __mmask8 __U, __m128i __A)
{
  return (__m128i) __builtin_ia32_vpconflictsi_128_mask ((__v4si) __A,
               (__v4si) __W,
               (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_conflict_epi32 (__mmask8 __U, __m128i __A)
{
  return (__m128i) __builtin_ia32_vpconflictsi_128_mask ((__v4si) __A,
               (__v4si) _mm_setzero_si128 (),
               (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_conflict_epi32 (__m256i __A)
{
  return (__m256i) __builtin_ia32_vpconflictsi_256_mask ((__v8si) __A,
               (__v8si) _mm256_undefined_si256 (),
               (__mmask8) -1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_conflict_epi32 (__m256i __W, __mmask8 __U, __m256i __A)
{
  return (__m256i) __builtin_ia32_vpconflictsi_256_mask ((__v8si) __A,
               (__v8si) __W,
               (__mmask8) __U);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_conflict_epi32 (__mmask8 __U, __m256i __A)
{
  return (__m256i) __builtin_ia32_vpconflictsi_256_mask ((__v8si) __A,
               (__v8si)
               _mm256_setzero_si256 (),
               (__mmask8) __U);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_lzcnt_epi32 (__m128i __A)
{
  return (__m128i) __builtin_ia32_vplzcntd_128 ((__v4si) __A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_lzcnt_epi32 (__m128i __W, __mmask8 __U, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectd_128((__mmask8)__U,
                                             (__v4si)_mm_lzcnt_epi32(__A),
                                             (__v4si)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_lzcnt_epi32 (__mmask8 __U, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectd_128((__mmask8)__U,
                                             (__v4si)_mm_lzcnt_epi32(__A),
                                             (__v4si)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_lzcnt_epi32 (__m256i __A)
{
  return (__m256i) __builtin_ia32_vplzcntd_256 ((__v8si) __A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_lzcnt_epi32 (__m256i __W, __mmask8 __U, __m256i __A)
{
  return (__m256i)__builtin_ia32_selectd_256((__mmask8)__U,
                                             (__v8si)_mm256_lzcnt_epi32(__A),
                                             (__v8si)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_lzcnt_epi32 (__mmask8 __U, __m256i __A)
{
  return (__m256i)__builtin_ia32_selectd_256((__mmask8)__U,
                                             (__v8si)_mm256_lzcnt_epi32(__A),
                                             (__v8si)_mm256_setzero_si256());
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_lzcnt_epi64 (__m128i __A)
{
  return (__m128i) __builtin_ia32_vplzcntq_128 ((__v2di) __A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_mask_lzcnt_epi64 (__m128i __W, __mmask8 __U, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectq_128((__mmask8)__U,
                                             (__v2di)_mm_lzcnt_epi64(__A),
                                             (__v2di)__W);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS128
_mm_maskz_lzcnt_epi64 (__mmask8 __U, __m128i __A)
{
  return (__m128i)__builtin_ia32_selectq_128((__mmask8)__U,
                                             (__v2di)_mm_lzcnt_epi64(__A),
                                             (__v2di)_mm_setzero_si128());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_lzcnt_epi64 (__m256i __A)
{
  return (__m256i) __builtin_ia32_vplzcntq_256 ((__v4di) __A);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_mask_lzcnt_epi64 (__m256i __W, __mmask8 __U, __m256i __A)
{
  return (__m256i)__builtin_ia32_selectq_256((__mmask8)__U,
                                             (__v4di)_mm256_lzcnt_epi64(__A),
                                             (__v4di)__W);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_maskz_lzcnt_epi64 (__mmask8 __U, __m256i __A)
{
  return (__m256i)__builtin_ia32_selectq_256((__mmask8)__U,
                                             (__v4di)_mm256_lzcnt_epi64(__A),
                                             (__v4di)_mm256_setzero_si256());
}

#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256

#endif /* __AVX512VLCDINTRIN_H */
