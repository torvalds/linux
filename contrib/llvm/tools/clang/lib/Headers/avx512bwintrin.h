/*===------------- avx512bwintrin.h - AVX512BW intrinsics ------------------===
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
#error "Never use <avx512bwintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512BWINTRIN_H
#define __AVX512BWINTRIN_H

typedef unsigned int __mmask32;
typedef unsigned long long __mmask64;

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS512 __attribute__((__always_inline__, __nodebug__, __target__("avx512bw"), __min_vector_width__(512)))
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("avx512bw")))

static __inline __mmask32 __DEFAULT_FN_ATTRS
_knot_mask32(__mmask32 __M)
{
  return __builtin_ia32_knotsi(__M);
}

static __inline __mmask64 __DEFAULT_FN_ATTRS
_knot_mask64(__mmask64 __M)
{
  return __builtin_ia32_knotdi(__M);
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS
_kand_mask32(__mmask32 __A, __mmask32 __B)
{
  return (__mmask32)__builtin_ia32_kandsi((__mmask32)__A, (__mmask32)__B);
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS
_kand_mask64(__mmask64 __A, __mmask64 __B)
{
  return (__mmask64)__builtin_ia32_kanddi((__mmask64)__A, (__mmask64)__B);
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS
_kandn_mask32(__mmask32 __A, __mmask32 __B)
{
  return (__mmask32)__builtin_ia32_kandnsi((__mmask32)__A, (__mmask32)__B);
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS
_kandn_mask64(__mmask64 __A, __mmask64 __B)
{
  return (__mmask64)__builtin_ia32_kandndi((__mmask64)__A, (__mmask64)__B);
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS
_kor_mask32(__mmask32 __A, __mmask32 __B)
{
  return (__mmask32)__builtin_ia32_korsi((__mmask32)__A, (__mmask32)__B);
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS
_kor_mask64(__mmask64 __A, __mmask64 __B)
{
  return (__mmask64)__builtin_ia32_kordi((__mmask64)__A, (__mmask64)__B);
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS
_kxnor_mask32(__mmask32 __A, __mmask32 __B)
{
  return (__mmask32)__builtin_ia32_kxnorsi((__mmask32)__A, (__mmask32)__B);
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS
_kxnor_mask64(__mmask64 __A, __mmask64 __B)
{
  return (__mmask64)__builtin_ia32_kxnordi((__mmask64)__A, (__mmask64)__B);
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS
_kxor_mask32(__mmask32 __A, __mmask32 __B)
{
  return (__mmask32)__builtin_ia32_kxorsi((__mmask32)__A, (__mmask32)__B);
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS
_kxor_mask64(__mmask64 __A, __mmask64 __B)
{
  return (__mmask64)__builtin_ia32_kxordi((__mmask64)__A, (__mmask64)__B);
}

static __inline__ unsigned char __DEFAULT_FN_ATTRS
_kortestc_mask32_u8(__mmask32 __A, __mmask32 __B)
{
  return (unsigned char)__builtin_ia32_kortestcsi(__A, __B);
}

static __inline__ unsigned char __DEFAULT_FN_ATTRS
_kortestz_mask32_u8(__mmask32 __A, __mmask32 __B)
{
  return (unsigned char)__builtin_ia32_kortestzsi(__A, __B);
}

static __inline__ unsigned char __DEFAULT_FN_ATTRS
_kortest_mask32_u8(__mmask32 __A, __mmask32 __B, unsigned char *__C) {
  *__C = (unsigned char)__builtin_ia32_kortestcsi(__A, __B);
  return (unsigned char)__builtin_ia32_kortestzsi(__A, __B);
}

static __inline__ unsigned char __DEFAULT_FN_ATTRS
_kortestc_mask64_u8(__mmask64 __A, __mmask64 __B)
{
  return (unsigned char)__builtin_ia32_kortestcdi(__A, __B);
}

static __inline__ unsigned char __DEFAULT_FN_ATTRS
_kortestz_mask64_u8(__mmask64 __A, __mmask64 __B)
{
  return (unsigned char)__builtin_ia32_kortestzdi(__A, __B);
}

static __inline__ unsigned char __DEFAULT_FN_ATTRS
_kortest_mask64_u8(__mmask64 __A, __mmask64 __B, unsigned char *__C) {
  *__C = (unsigned char)__builtin_ia32_kortestcdi(__A, __B);
  return (unsigned char)__builtin_ia32_kortestzdi(__A, __B);
}

static __inline__ unsigned char __DEFAULT_FN_ATTRS
_ktestc_mask32_u8(__mmask32 __A, __mmask32 __B)
{
  return (unsigned char)__builtin_ia32_ktestcsi(__A, __B);
}

static __inline__ unsigned char __DEFAULT_FN_ATTRS
_ktestz_mask32_u8(__mmask32 __A, __mmask32 __B)
{
  return (unsigned char)__builtin_ia32_ktestzsi(__A, __B);
}

static __inline__ unsigned char __DEFAULT_FN_ATTRS
_ktest_mask32_u8(__mmask32 __A, __mmask32 __B, unsigned char *__C) {
  *__C = (unsigned char)__builtin_ia32_ktestcsi(__A, __B);
  return (unsigned char)__builtin_ia32_ktestzsi(__A, __B);
}

static __inline__ unsigned char __DEFAULT_FN_ATTRS
_ktestc_mask64_u8(__mmask64 __A, __mmask64 __B)
{
  return (unsigned char)__builtin_ia32_ktestcdi(__A, __B);
}

static __inline__ unsigned char __DEFAULT_FN_ATTRS
_ktestz_mask64_u8(__mmask64 __A, __mmask64 __B)
{
  return (unsigned char)__builtin_ia32_ktestzdi(__A, __B);
}

static __inline__ unsigned char __DEFAULT_FN_ATTRS
_ktest_mask64_u8(__mmask64 __A, __mmask64 __B, unsigned char *__C) {
  *__C = (unsigned char)__builtin_ia32_ktestcdi(__A, __B);
  return (unsigned char)__builtin_ia32_ktestzdi(__A, __B);
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS
_kadd_mask32(__mmask32 __A, __mmask32 __B)
{
  return (__mmask32)__builtin_ia32_kaddsi((__mmask32)__A, (__mmask32)__B);
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS
_kadd_mask64(__mmask64 __A, __mmask64 __B)
{
  return (__mmask64)__builtin_ia32_kadddi((__mmask64)__A, (__mmask64)__B);
}

#define _kshiftli_mask32(A, I) \
  (__mmask32)__builtin_ia32_kshiftlisi((__mmask32)(A), (unsigned int)(I))

#define _kshiftri_mask32(A, I) \
  (__mmask32)__builtin_ia32_kshiftrisi((__mmask32)(A), (unsigned int)(I))

#define _kshiftli_mask64(A, I) \
  (__mmask64)__builtin_ia32_kshiftlidi((__mmask64)(A), (unsigned int)(I))

#define _kshiftri_mask64(A, I) \
  (__mmask64)__builtin_ia32_kshiftridi((__mmask64)(A), (unsigned int)(I))

static __inline__ unsigned int __DEFAULT_FN_ATTRS
_cvtmask32_u32(__mmask32 __A) {
  return (unsigned int)__builtin_ia32_kmovd((__mmask32)__A);
}

static __inline__ unsigned long long __DEFAULT_FN_ATTRS
_cvtmask64_u64(__mmask64 __A) {
  return (unsigned long long)__builtin_ia32_kmovq((__mmask64)__A);
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS
_cvtu32_mask32(unsigned int __A) {
  return (__mmask32)__builtin_ia32_kmovd((__mmask32)__A);
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS
_cvtu64_mask64(unsigned long long __A) {
  return (__mmask64)__builtin_ia32_kmovq((__mmask64)__A);
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS
_load_mask32(__mmask32 *__A) {
  return (__mmask32)__builtin_ia32_kmovd(*(__mmask32 *)__A);
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS
_load_mask64(__mmask64 *__A) {
  return (__mmask64)__builtin_ia32_kmovq(*(__mmask64 *)__A);
}

static __inline__ void __DEFAULT_FN_ATTRS
_store_mask32(__mmask32 *__A, __mmask32 __B) {
  *(__mmask32 *)__A = __builtin_ia32_kmovd((__mmask32)__B);
}

static __inline__ void __DEFAULT_FN_ATTRS
_store_mask64(__mmask64 *__A, __mmask64 __B) {
  *(__mmask64 *)__A = __builtin_ia32_kmovq((__mmask64)__B);
}

/* Integer compare */

#define _mm512_cmp_epi8_mask(a, b, p) \
  (__mmask64)__builtin_ia32_cmpb512_mask((__v64qi)(__m512i)(a), \
                                         (__v64qi)(__m512i)(b), (int)(p), \
                                         (__mmask64)-1)

#define _mm512_mask_cmp_epi8_mask(m, a, b, p) \
  (__mmask64)__builtin_ia32_cmpb512_mask((__v64qi)(__m512i)(a), \
                                         (__v64qi)(__m512i)(b), (int)(p), \
                                         (__mmask64)(m))

#define _mm512_cmp_epu8_mask(a, b, p) \
  (__mmask64)__builtin_ia32_ucmpb512_mask((__v64qi)(__m512i)(a), \
                                          (__v64qi)(__m512i)(b), (int)(p), \
                                          (__mmask64)-1)

#define _mm512_mask_cmp_epu8_mask(m, a, b, p) \
  (__mmask64)__builtin_ia32_ucmpb512_mask((__v64qi)(__m512i)(a), \
                                          (__v64qi)(__m512i)(b), (int)(p), \
                                          (__mmask64)(m))

#define _mm512_cmp_epi16_mask(a, b, p) \
  (__mmask32)__builtin_ia32_cmpw512_mask((__v32hi)(__m512i)(a), \
                                         (__v32hi)(__m512i)(b), (int)(p), \
                                         (__mmask32)-1)

#define _mm512_mask_cmp_epi16_mask(m, a, b, p) \
  (__mmask32)__builtin_ia32_cmpw512_mask((__v32hi)(__m512i)(a), \
                                         (__v32hi)(__m512i)(b), (int)(p), \
                                         (__mmask32)(m))

#define _mm512_cmp_epu16_mask(a, b, p) \
  (__mmask32)__builtin_ia32_ucmpw512_mask((__v32hi)(__m512i)(a), \
                                          (__v32hi)(__m512i)(b), (int)(p), \
                                          (__mmask32)-1)

#define _mm512_mask_cmp_epu16_mask(m, a, b, p) \
  (__mmask32)__builtin_ia32_ucmpw512_mask((__v32hi)(__m512i)(a), \
                                          (__v32hi)(__m512i)(b), (int)(p), \
                                          (__mmask32)(m))

#define _mm512_cmpeq_epi8_mask(A, B) \
    _mm512_cmp_epi8_mask((A), (B), _MM_CMPINT_EQ)
#define _mm512_mask_cmpeq_epi8_mask(k, A, B) \
    _mm512_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_EQ)
#define _mm512_cmpge_epi8_mask(A, B) \
    _mm512_cmp_epi8_mask((A), (B), _MM_CMPINT_GE)
#define _mm512_mask_cmpge_epi8_mask(k, A, B) \
    _mm512_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_GE)
#define _mm512_cmpgt_epi8_mask(A, B) \
    _mm512_cmp_epi8_mask((A), (B), _MM_CMPINT_GT)
#define _mm512_mask_cmpgt_epi8_mask(k, A, B) \
    _mm512_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_GT)
#define _mm512_cmple_epi8_mask(A, B) \
    _mm512_cmp_epi8_mask((A), (B), _MM_CMPINT_LE)
#define _mm512_mask_cmple_epi8_mask(k, A, B) \
    _mm512_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_LE)
#define _mm512_cmplt_epi8_mask(A, B) \
    _mm512_cmp_epi8_mask((A), (B), _MM_CMPINT_LT)
#define _mm512_mask_cmplt_epi8_mask(k, A, B) \
    _mm512_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_LT)
#define _mm512_cmpneq_epi8_mask(A, B) \
    _mm512_cmp_epi8_mask((A), (B), _MM_CMPINT_NE)
#define _mm512_mask_cmpneq_epi8_mask(k, A, B) \
    _mm512_mask_cmp_epi8_mask((k), (A), (B), _MM_CMPINT_NE)

#define _mm512_cmpeq_epu8_mask(A, B) \
    _mm512_cmp_epu8_mask((A), (B), _MM_CMPINT_EQ)
#define _mm512_mask_cmpeq_epu8_mask(k, A, B) \
    _mm512_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_EQ)
#define _mm512_cmpge_epu8_mask(A, B) \
    _mm512_cmp_epu8_mask((A), (B), _MM_CMPINT_GE)
#define _mm512_mask_cmpge_epu8_mask(k, A, B) \
    _mm512_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_GE)
#define _mm512_cmpgt_epu8_mask(A, B) \
    _mm512_cmp_epu8_mask((A), (B), _MM_CMPINT_GT)
#define _mm512_mask_cmpgt_epu8_mask(k, A, B) \
    _mm512_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_GT)
#define _mm512_cmple_epu8_mask(A, B) \
    _mm512_cmp_epu8_mask((A), (B), _MM_CMPINT_LE)
#define _mm512_mask_cmple_epu8_mask(k, A, B) \
    _mm512_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_LE)
#define _mm512_cmplt_epu8_mask(A, B) \
    _mm512_cmp_epu8_mask((A), (B), _MM_CMPINT_LT)
#define _mm512_mask_cmplt_epu8_mask(k, A, B) \
    _mm512_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_LT)
#define _mm512_cmpneq_epu8_mask(A, B) \
    _mm512_cmp_epu8_mask((A), (B), _MM_CMPINT_NE)
#define _mm512_mask_cmpneq_epu8_mask(k, A, B) \
    _mm512_mask_cmp_epu8_mask((k), (A), (B), _MM_CMPINT_NE)

#define _mm512_cmpeq_epi16_mask(A, B) \
    _mm512_cmp_epi16_mask((A), (B), _MM_CMPINT_EQ)
#define _mm512_mask_cmpeq_epi16_mask(k, A, B) \
    _mm512_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_EQ)
#define _mm512_cmpge_epi16_mask(A, B) \
    _mm512_cmp_epi16_mask((A), (B), _MM_CMPINT_GE)
#define _mm512_mask_cmpge_epi16_mask(k, A, B) \
    _mm512_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_GE)
#define _mm512_cmpgt_epi16_mask(A, B) \
    _mm512_cmp_epi16_mask((A), (B), _MM_CMPINT_GT)
#define _mm512_mask_cmpgt_epi16_mask(k, A, B) \
    _mm512_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_GT)
#define _mm512_cmple_epi16_mask(A, B) \
    _mm512_cmp_epi16_mask((A), (B), _MM_CMPINT_LE)
#define _mm512_mask_cmple_epi16_mask(k, A, B) \
    _mm512_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_LE)
#define _mm512_cmplt_epi16_mask(A, B) \
    _mm512_cmp_epi16_mask((A), (B), _MM_CMPINT_LT)
#define _mm512_mask_cmplt_epi16_mask(k, A, B) \
    _mm512_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_LT)
#define _mm512_cmpneq_epi16_mask(A, B) \
    _mm512_cmp_epi16_mask((A), (B), _MM_CMPINT_NE)
#define _mm512_mask_cmpneq_epi16_mask(k, A, B) \
    _mm512_mask_cmp_epi16_mask((k), (A), (B), _MM_CMPINT_NE)

#define _mm512_cmpeq_epu16_mask(A, B) \
    _mm512_cmp_epu16_mask((A), (B), _MM_CMPINT_EQ)
#define _mm512_mask_cmpeq_epu16_mask(k, A, B) \
    _mm512_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_EQ)
#define _mm512_cmpge_epu16_mask(A, B) \
    _mm512_cmp_epu16_mask((A), (B), _MM_CMPINT_GE)
#define _mm512_mask_cmpge_epu16_mask(k, A, B) \
    _mm512_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_GE)
#define _mm512_cmpgt_epu16_mask(A, B) \
    _mm512_cmp_epu16_mask((A), (B), _MM_CMPINT_GT)
#define _mm512_mask_cmpgt_epu16_mask(k, A, B) \
    _mm512_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_GT)
#define _mm512_cmple_epu16_mask(A, B) \
    _mm512_cmp_epu16_mask((A), (B), _MM_CMPINT_LE)
#define _mm512_mask_cmple_epu16_mask(k, A, B) \
    _mm512_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_LE)
#define _mm512_cmplt_epu16_mask(A, B) \
    _mm512_cmp_epu16_mask((A), (B), _MM_CMPINT_LT)
#define _mm512_mask_cmplt_epu16_mask(k, A, B) \
    _mm512_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_LT)
#define _mm512_cmpneq_epu16_mask(A, B) \
    _mm512_cmp_epu16_mask((A), (B), _MM_CMPINT_NE)
#define _mm512_mask_cmpneq_epu16_mask(k, A, B) \
    _mm512_mask_cmp_epu16_mask((k), (A), (B), _MM_CMPINT_NE)

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_add_epi8 (__m512i __A, __m512i __B) {
  return (__m512i) ((__v64qu) __A + (__v64qu) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_add_epi8(__m512i __W, __mmask64 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                             (__v64qi)_mm512_add_epi8(__A, __B),
                                             (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_add_epi8(__mmask64 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                             (__v64qi)_mm512_add_epi8(__A, __B),
                                             (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_sub_epi8 (__m512i __A, __m512i __B) {
  return (__m512i) ((__v64qu) __A - (__v64qu) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_sub_epi8(__m512i __W, __mmask64 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                             (__v64qi)_mm512_sub_epi8(__A, __B),
                                             (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_sub_epi8(__mmask64 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                             (__v64qi)_mm512_sub_epi8(__A, __B),
                                             (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_add_epi16 (__m512i __A, __m512i __B) {
  return (__m512i) ((__v32hu) __A + (__v32hu) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_add_epi16(__m512i __W, __mmask32 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                             (__v32hi)_mm512_add_epi16(__A, __B),
                                             (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_add_epi16(__mmask32 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                             (__v32hi)_mm512_add_epi16(__A, __B),
                                             (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_sub_epi16 (__m512i __A, __m512i __B) {
  return (__m512i) ((__v32hu) __A - (__v32hu) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_sub_epi16(__m512i __W, __mmask32 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                             (__v32hi)_mm512_sub_epi16(__A, __B),
                                             (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_sub_epi16(__mmask32 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                             (__v32hi)_mm512_sub_epi16(__A, __B),
                                             (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mullo_epi16 (__m512i __A, __m512i __B) {
  return (__m512i) ((__v32hu) __A * (__v32hu) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_mullo_epi16(__m512i __W, __mmask32 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                             (__v32hi)_mm512_mullo_epi16(__A, __B),
                                             (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_mullo_epi16(__mmask32 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                             (__v32hi)_mm512_mullo_epi16(__A, __B),
                                             (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_blend_epi8 (__mmask64 __U, __m512i __A, __m512i __W)
{
  return (__m512i) __builtin_ia32_selectb_512 ((__mmask64) __U,
              (__v64qi) __W,
              (__v64qi) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_blend_epi16 (__mmask32 __U, __m512i __A, __m512i __W)
{
  return (__m512i) __builtin_ia32_selectw_512 ((__mmask32) __U,
              (__v32hi) __W,
              (__v32hi) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_abs_epi8 (__m512i __A)
{
  return (__m512i)__builtin_ia32_pabsb512((__v64qi)__A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_abs_epi8 (__m512i __W, __mmask64 __U, __m512i __A)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                             (__v64qi)_mm512_abs_epi8(__A),
                                             (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_abs_epi8 (__mmask64 __U, __m512i __A)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                             (__v64qi)_mm512_abs_epi8(__A),
                                             (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_abs_epi16 (__m512i __A)
{
  return (__m512i)__builtin_ia32_pabsw512((__v32hi)__A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_abs_epi16 (__m512i __W, __mmask32 __U, __m512i __A)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                             (__v32hi)_mm512_abs_epi16(__A),
                                             (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_abs_epi16 (__mmask32 __U, __m512i __A)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                             (__v32hi)_mm512_abs_epi16(__A),
                                             (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_packs_epi32(__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_packssdw512((__v16si)__A, (__v16si)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_packs_epi32(__mmask32 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__M,
                                       (__v32hi)_mm512_packs_epi32(__A, __B),
                                       (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_packs_epi32(__m512i __W, __mmask32 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__M,
                                       (__v32hi)_mm512_packs_epi32(__A, __B),
                                       (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_packs_epi16(__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_packsswb512((__v32hi)__A, (__v32hi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_packs_epi16(__m512i __W, __mmask64 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                        (__v64qi)_mm512_packs_epi16(__A, __B),
                                        (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_packs_epi16(__mmask64 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                        (__v64qi)_mm512_packs_epi16(__A, __B),
                                        (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_packus_epi32(__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_packusdw512((__v16si) __A, (__v16si) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_packus_epi32(__mmask32 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__M,
                                       (__v32hi)_mm512_packus_epi32(__A, __B),
                                       (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_packus_epi32(__m512i __W, __mmask32 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__M,
                                       (__v32hi)_mm512_packus_epi32(__A, __B),
                                       (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_packus_epi16(__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_packuswb512((__v32hi) __A, (__v32hi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_packus_epi16(__m512i __W, __mmask64 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                        (__v64qi)_mm512_packus_epi16(__A, __B),
                                        (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_packus_epi16(__mmask64 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                        (__v64qi)_mm512_packus_epi16(__A, __B),
                                        (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_adds_epi8 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_paddsb512((__v64qi)__A, (__v64qi)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_adds_epi8 (__m512i __W, __mmask64 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                        (__v64qi)_mm512_adds_epi8(__A, __B),
                                        (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_adds_epi8 (__mmask64 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                        (__v64qi)_mm512_adds_epi8(__A, __B),
                                        (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_adds_epi16 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_paddsw512((__v32hi)__A, (__v32hi)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_adds_epi16 (__m512i __W, __mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                        (__v32hi)_mm512_adds_epi16(__A, __B),
                                        (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_adds_epi16 (__mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                        (__v32hi)_mm512_adds_epi16(__A, __B),
                                        (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_adds_epu8 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_paddusb512((__v64qi) __A, (__v64qi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_adds_epu8 (__m512i __W, __mmask64 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                        (__v64qi)_mm512_adds_epu8(__A, __B),
                                        (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_adds_epu8 (__mmask64 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                        (__v64qi)_mm512_adds_epu8(__A, __B),
                                        (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_adds_epu16 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_paddusw512((__v32hi) __A, (__v32hi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_adds_epu16 (__m512i __W, __mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                        (__v32hi)_mm512_adds_epu16(__A, __B),
                                        (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_adds_epu16 (__mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                        (__v32hi)_mm512_adds_epu16(__A, __B),
                                        (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_avg_epu8 (__m512i __A, __m512i __B)
{
  typedef unsigned short __v64hu __attribute__((__vector_size__(128)));
  return (__m512i)__builtin_convertvector(
              ((__builtin_convertvector((__v64qu) __A, __v64hu) +
                __builtin_convertvector((__v64qu) __B, __v64hu)) + 1)
                >> 1, __v64qu);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_avg_epu8 (__m512i __W, __mmask64 __U, __m512i __A,
          __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
              (__v64qi)_mm512_avg_epu8(__A, __B),
              (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_avg_epu8 (__mmask64 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
              (__v64qi)_mm512_avg_epu8(__A, __B),
              (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_avg_epu16 (__m512i __A, __m512i __B)
{
  typedef unsigned int __v32su __attribute__((__vector_size__(128)));
  return (__m512i)__builtin_convertvector(
              ((__builtin_convertvector((__v32hu) __A, __v32su) +
                __builtin_convertvector((__v32hu) __B, __v32su)) + 1)
                >> 1, __v32hu);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_avg_epu16 (__m512i __W, __mmask32 __U, __m512i __A,
           __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
              (__v32hi)_mm512_avg_epu16(__A, __B),
              (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_avg_epu16 (__mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
              (__v32hi)_mm512_avg_epu16(__A, __B),
              (__v32hi) _mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_max_epi8 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_pmaxsb512((__v64qi) __A, (__v64qi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_max_epi8 (__mmask64 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                             (__v64qi)_mm512_max_epi8(__A, __B),
                                             (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_max_epi8 (__m512i __W, __mmask64 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                             (__v64qi)_mm512_max_epi8(__A, __B),
                                             (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_max_epi16 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_pmaxsw512((__v32hi) __A, (__v32hi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_max_epi16 (__mmask32 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__M,
                                            (__v32hi)_mm512_max_epi16(__A, __B),
                                            (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_max_epi16 (__m512i __W, __mmask32 __M, __m512i __A,
           __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__M,
                                            (__v32hi)_mm512_max_epi16(__A, __B),
                                            (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_max_epu8 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_pmaxub512((__v64qi)__A, (__v64qi)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_max_epu8 (__mmask64 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                             (__v64qi)_mm512_max_epu8(__A, __B),
                                             (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_max_epu8 (__m512i __W, __mmask64 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                             (__v64qi)_mm512_max_epu8(__A, __B),
                                             (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_max_epu16 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_pmaxuw512((__v32hi)__A, (__v32hi)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_max_epu16 (__mmask32 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__M,
                                            (__v32hi)_mm512_max_epu16(__A, __B),
                                            (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_max_epu16 (__m512i __W, __mmask32 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__M,
                                            (__v32hi)_mm512_max_epu16(__A, __B),
                                            (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_min_epi8 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_pminsb512((__v64qi) __A, (__v64qi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_min_epi8 (__mmask64 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                             (__v64qi)_mm512_min_epi8(__A, __B),
                                             (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_min_epi8 (__m512i __W, __mmask64 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                             (__v64qi)_mm512_min_epi8(__A, __B),
                                             (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_min_epi16 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_pminsw512((__v32hi) __A, (__v32hi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_min_epi16 (__mmask32 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__M,
                                            (__v32hi)_mm512_min_epi16(__A, __B),
                                            (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_min_epi16 (__m512i __W, __mmask32 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__M,
                                            (__v32hi)_mm512_min_epi16(__A, __B),
                                            (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_min_epu8 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_pminub512((__v64qi)__A, (__v64qi)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_min_epu8 (__mmask64 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                             (__v64qi)_mm512_min_epu8(__A, __B),
                                             (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_min_epu8 (__m512i __W, __mmask64 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                             (__v64qi)_mm512_min_epu8(__A, __B),
                                             (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_min_epu16 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_pminuw512((__v32hi)__A, (__v32hi)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_min_epu16 (__mmask32 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__M,
                                            (__v32hi)_mm512_min_epu16(__A, __B),
                                            (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_min_epu16 (__m512i __W, __mmask32 __M, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__M,
                                            (__v32hi)_mm512_min_epu16(__A, __B),
                                            (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_shuffle_epi8(__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_pshufb512((__v64qi)__A,(__v64qi)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_shuffle_epi8(__m512i __W, __mmask64 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                         (__v64qi)_mm512_shuffle_epi8(__A, __B),
                                         (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_shuffle_epi8(__mmask64 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                         (__v64qi)_mm512_shuffle_epi8(__A, __B),
                                         (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_subs_epi8 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_psubsb512((__v64qi)__A, (__v64qi)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_subs_epi8 (__m512i __W, __mmask64 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                        (__v64qi)_mm512_subs_epi8(__A, __B),
                                        (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_subs_epi8 (__mmask64 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                        (__v64qi)_mm512_subs_epi8(__A, __B),
                                        (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_subs_epi16 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_psubsw512((__v32hi)__A, (__v32hi)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_subs_epi16 (__m512i __W, __mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                        (__v32hi)_mm512_subs_epi16(__A, __B),
                                        (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_subs_epi16 (__mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                        (__v32hi)_mm512_subs_epi16(__A, __B),
                                        (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_subs_epu8 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_psubusb512((__v64qi) __A, (__v64qi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_subs_epu8 (__m512i __W, __mmask64 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                        (__v64qi)_mm512_subs_epu8(__A, __B),
                                        (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_subs_epu8 (__mmask64 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                        (__v64qi)_mm512_subs_epu8(__A, __B),
                                        (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_subs_epu16 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_psubusw512((__v32hi) __A, (__v32hi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_subs_epu16 (__m512i __W, __mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                        (__v32hi)_mm512_subs_epu16(__A, __B),
                                        (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_subs_epu16 (__mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                        (__v32hi)_mm512_subs_epu16(__A, __B),
                                        (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_permutex2var_epi16(__m512i __A, __m512i __I, __m512i __B)
{
  return (__m512i)__builtin_ia32_vpermi2varhi512((__v32hi)__A, (__v32hi)__I,
                                                 (__v32hi)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_permutex2var_epi16(__m512i __A, __mmask32 __U, __m512i __I,
                               __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512(__U,
                              (__v32hi)_mm512_permutex2var_epi16(__A, __I, __B),
                              (__v32hi)__A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask2_permutex2var_epi16(__m512i __A, __m512i __I, __mmask32 __U,
                                __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512(__U,
                              (__v32hi)_mm512_permutex2var_epi16(__A, __I, __B),
                              (__v32hi)__I);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_permutex2var_epi16(__mmask32 __U, __m512i __A, __m512i __I,
                                __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512(__U,
                              (__v32hi)_mm512_permutex2var_epi16(__A, __I, __B),
                              (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mulhrs_epi16(__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_pmulhrsw512((__v32hi)__A, (__v32hi)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_mulhrs_epi16(__m512i __W, __mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                         (__v32hi)_mm512_mulhrs_epi16(__A, __B),
                                         (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_mulhrs_epi16(__mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                         (__v32hi)_mm512_mulhrs_epi16(__A, __B),
                                         (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mulhi_epi16(__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_pmulhw512((__v32hi) __A, (__v32hi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_mulhi_epi16(__m512i __W, __mmask32 __U, __m512i __A,
       __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                          (__v32hi)_mm512_mulhi_epi16(__A, __B),
                                          (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_mulhi_epi16(__mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                          (__v32hi)_mm512_mulhi_epi16(__A, __B),
                                          (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mulhi_epu16(__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_pmulhuw512((__v32hi) __A, (__v32hi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_mulhi_epu16(__m512i __W, __mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                          (__v32hi)_mm512_mulhi_epu16(__A, __B),
                                          (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_mulhi_epu16 (__mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                          (__v32hi)_mm512_mulhi_epu16(__A, __B),
                                          (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maddubs_epi16(__m512i __X, __m512i __Y) {
  return (__m512i)__builtin_ia32_pmaddubsw512((__v64qi)__X, (__v64qi)__Y);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_maddubs_epi16(__m512i __W, __mmask32 __U, __m512i __X,
                          __m512i __Y) {
  return (__m512i)__builtin_ia32_selectw_512((__mmask32) __U,
                                        (__v32hi)_mm512_maddubs_epi16(__X, __Y),
                                        (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_maddubs_epi16(__mmask32 __U, __m512i __X, __m512i __Y) {
  return (__m512i)__builtin_ia32_selectw_512((__mmask32) __U,
                                        (__v32hi)_mm512_maddubs_epi16(__X, __Y),
                                        (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_madd_epi16(__m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_pmaddwd512((__v32hi)__A, (__v32hi)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_madd_epi16(__m512i __W, __mmask16 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectd_512((__mmask16)__U,
                                           (__v16si)_mm512_madd_epi16(__A, __B),
                                           (__v16si)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_madd_epi16(__mmask16 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectd_512((__mmask16)__U,
                                           (__v16si)_mm512_madd_epi16(__A, __B),
                                           (__v16si)_mm512_setzero_si512());
}

static __inline__ __m256i __DEFAULT_FN_ATTRS512
_mm512_cvtsepi16_epi8 (__m512i __A) {
  return (__m256i) __builtin_ia32_pmovswb512_mask ((__v32hi) __A,
               (__v32qi)_mm256_setzero_si256(),
               (__mmask32) -1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS512
_mm512_mask_cvtsepi16_epi8 (__m256i __O, __mmask32 __M, __m512i __A) {
  return (__m256i) __builtin_ia32_pmovswb512_mask ((__v32hi) __A,
               (__v32qi)__O,
               __M);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtsepi16_epi8 (__mmask32 __M, __m512i __A) {
  return (__m256i) __builtin_ia32_pmovswb512_mask ((__v32hi) __A,
               (__v32qi) _mm256_setzero_si256(),
               __M);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS512
_mm512_cvtusepi16_epi8 (__m512i __A) {
  return (__m256i) __builtin_ia32_pmovuswb512_mask ((__v32hi) __A,
                (__v32qi) _mm256_setzero_si256(),
                (__mmask32) -1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS512
_mm512_mask_cvtusepi16_epi8 (__m256i __O, __mmask32 __M, __m512i __A) {
  return (__m256i) __builtin_ia32_pmovuswb512_mask ((__v32hi) __A,
                (__v32qi) __O,
                __M);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtusepi16_epi8 (__mmask32 __M, __m512i __A) {
  return (__m256i) __builtin_ia32_pmovuswb512_mask ((__v32hi) __A,
                (__v32qi) _mm256_setzero_si256(),
                __M);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS512
_mm512_cvtepi16_epi8 (__m512i __A) {
  return (__m256i) __builtin_ia32_pmovwb512_mask ((__v32hi) __A,
              (__v32qi) _mm256_undefined_si256(),
              (__mmask32) -1);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS512
_mm512_mask_cvtepi16_epi8 (__m256i __O, __mmask32 __M, __m512i __A) {
  return (__m256i) __builtin_ia32_pmovwb512_mask ((__v32hi) __A,
              (__v32qi) __O,
              __M);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtepi16_epi8 (__mmask32 __M, __m512i __A) {
  return (__m256i) __builtin_ia32_pmovwb512_mask ((__v32hi) __A,
              (__v32qi) _mm256_setzero_si256(),
              __M);
}

static __inline__ void __DEFAULT_FN_ATTRS512
_mm512_mask_cvtepi16_storeu_epi8 (void * __P, __mmask32 __M, __m512i __A)
{
  __builtin_ia32_pmovwb512mem_mask ((__v32qi *) __P, (__v32hi) __A, __M);
}

static __inline__ void __DEFAULT_FN_ATTRS512
_mm512_mask_cvtsepi16_storeu_epi8 (void * __P, __mmask32 __M, __m512i __A)
{
  __builtin_ia32_pmovswb512mem_mask ((__v32qi *) __P, (__v32hi) __A, __M);
}

static __inline__ void __DEFAULT_FN_ATTRS512
_mm512_mask_cvtusepi16_storeu_epi8 (void * __P, __mmask32 __M, __m512i __A)
{
  __builtin_ia32_pmovuswb512mem_mask ((__v32qi *) __P, (__v32hi) __A, __M);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_unpackhi_epi8(__m512i __A, __m512i __B) {
  return (__m512i)__builtin_shufflevector((__v64qi)__A, (__v64qi)__B,
                                          8,  64+8,   9, 64+9,
                                          10, 64+10, 11, 64+11,
                                          12, 64+12, 13, 64+13,
                                          14, 64+14, 15, 64+15,
                                          24, 64+24, 25, 64+25,
                                          26, 64+26, 27, 64+27,
                                          28, 64+28, 29, 64+29,
                                          30, 64+30, 31, 64+31,
                                          40, 64+40, 41, 64+41,
                                          42, 64+42, 43, 64+43,
                                          44, 64+44, 45, 64+45,
                                          46, 64+46, 47, 64+47,
                                          56, 64+56, 57, 64+57,
                                          58, 64+58, 59, 64+59,
                                          60, 64+60, 61, 64+61,
                                          62, 64+62, 63, 64+63);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_unpackhi_epi8(__m512i __W, __mmask64 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                        (__v64qi)_mm512_unpackhi_epi8(__A, __B),
                                        (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_unpackhi_epi8(__mmask64 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                        (__v64qi)_mm512_unpackhi_epi8(__A, __B),
                                        (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_unpackhi_epi16(__m512i __A, __m512i __B) {
  return (__m512i)__builtin_shufflevector((__v32hi)__A, (__v32hi)__B,
                                          4,  32+4,   5, 32+5,
                                          6,  32+6,   7, 32+7,
                                          12, 32+12, 13, 32+13,
                                          14, 32+14, 15, 32+15,
                                          20, 32+20, 21, 32+21,
                                          22, 32+22, 23, 32+23,
                                          28, 32+28, 29, 32+29,
                                          30, 32+30, 31, 32+31);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_unpackhi_epi16(__m512i __W, __mmask32 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                       (__v32hi)_mm512_unpackhi_epi16(__A, __B),
                                       (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_unpackhi_epi16(__mmask32 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                       (__v32hi)_mm512_unpackhi_epi16(__A, __B),
                                       (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_unpacklo_epi8(__m512i __A, __m512i __B) {
  return (__m512i)__builtin_shufflevector((__v64qi)__A, (__v64qi)__B,
                                          0,  64+0,   1, 64+1,
                                          2,  64+2,   3, 64+3,
                                          4,  64+4,   5, 64+5,
                                          6,  64+6,   7, 64+7,
                                          16, 64+16, 17, 64+17,
                                          18, 64+18, 19, 64+19,
                                          20, 64+20, 21, 64+21,
                                          22, 64+22, 23, 64+23,
                                          32, 64+32, 33, 64+33,
                                          34, 64+34, 35, 64+35,
                                          36, 64+36, 37, 64+37,
                                          38, 64+38, 39, 64+39,
                                          48, 64+48, 49, 64+49,
                                          50, 64+50, 51, 64+51,
                                          52, 64+52, 53, 64+53,
                                          54, 64+54, 55, 64+55);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_unpacklo_epi8(__m512i __W, __mmask64 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                        (__v64qi)_mm512_unpacklo_epi8(__A, __B),
                                        (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_unpacklo_epi8(__mmask64 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__U,
                                        (__v64qi)_mm512_unpacklo_epi8(__A, __B),
                                        (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_unpacklo_epi16(__m512i __A, __m512i __B) {
  return (__m512i)__builtin_shufflevector((__v32hi)__A, (__v32hi)__B,
                                          0,  32+0,   1, 32+1,
                                          2,  32+2,   3, 32+3,
                                          8,  32+8,   9, 32+9,
                                          10, 32+10, 11, 32+11,
                                          16, 32+16, 17, 32+17,
                                          18, 32+18, 19, 32+19,
                                          24, 32+24, 25, 32+25,
                                          26, 32+26, 27, 32+27);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_unpacklo_epi16(__m512i __W, __mmask32 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                       (__v32hi)_mm512_unpacklo_epi16(__A, __B),
                                       (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_unpacklo_epi16(__mmask32 __U, __m512i __A, __m512i __B) {
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                       (__v32hi)_mm512_unpacklo_epi16(__A, __B),
                                       (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_cvtepi8_epi16(__m256i __A)
{
  /* This function always performs a signed extension, but __v32qi is a char
     which may be signed or unsigned, so use __v32qs. */
  return (__m512i)__builtin_convertvector((__v32qs)__A, __v32hi);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_cvtepi8_epi16(__m512i __W, __mmask32 __U, __m256i __A)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                             (__v32hi)_mm512_cvtepi8_epi16(__A),
                                             (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtepi8_epi16(__mmask32 __U, __m256i __A)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                             (__v32hi)_mm512_cvtepi8_epi16(__A),
                                             (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_cvtepu8_epi16(__m256i __A)
{
  return (__m512i)__builtin_convertvector((__v32qu)__A, __v32hi);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_cvtepu8_epi16(__m512i __W, __mmask32 __U, __m256i __A)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                             (__v32hi)_mm512_cvtepu8_epi16(__A),
                                             (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_cvtepu8_epi16(__mmask32 __U, __m256i __A)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                             (__v32hi)_mm512_cvtepu8_epi16(__A),
                                             (__v32hi)_mm512_setzero_si512());
}


#define _mm512_shufflehi_epi16(A, imm) \
  (__m512i)__builtin_ia32_pshufhw512((__v32hi)(__m512i)(A), (int)(imm))

#define _mm512_mask_shufflehi_epi16(W, U, A, imm) \
  (__m512i)__builtin_ia32_selectw_512((__mmask32)(U), \
                                      (__v32hi)_mm512_shufflehi_epi16((A), \
                                                                      (imm)), \
                                      (__v32hi)(__m512i)(W))

#define _mm512_maskz_shufflehi_epi16(U, A, imm) \
  (__m512i)__builtin_ia32_selectw_512((__mmask32)(U), \
                                      (__v32hi)_mm512_shufflehi_epi16((A), \
                                                                      (imm)), \
                                      (__v32hi)_mm512_setzero_si512())

#define _mm512_shufflelo_epi16(A, imm) \
  (__m512i)__builtin_ia32_pshuflw512((__v32hi)(__m512i)(A), (int)(imm))


#define _mm512_mask_shufflelo_epi16(W, U, A, imm) \
  (__m512i)__builtin_ia32_selectw_512((__mmask32)(U), \
                                      (__v32hi)_mm512_shufflelo_epi16((A), \
                                                                      (imm)), \
                                      (__v32hi)(__m512i)(W))


#define _mm512_maskz_shufflelo_epi16(U, A, imm) \
  (__m512i)__builtin_ia32_selectw_512((__mmask32)(U), \
                                      (__v32hi)_mm512_shufflelo_epi16((A), \
                                                                      (imm)), \
                                      (__v32hi)_mm512_setzero_si512())

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_sllv_epi16(__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_psllv32hi((__v32hi) __A, (__v32hi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_sllv_epi16 (__m512i __W, __mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                           (__v32hi)_mm512_sllv_epi16(__A, __B),
                                           (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_sllv_epi16(__mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                           (__v32hi)_mm512_sllv_epi16(__A, __B),
                                           (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_sll_epi16(__m512i __A, __m128i __B)
{
  return (__m512i)__builtin_ia32_psllw512((__v32hi) __A, (__v8hi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_sll_epi16(__m512i __W, __mmask32 __U, __m512i __A, __m128i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                          (__v32hi)_mm512_sll_epi16(__A, __B),
                                          (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_sll_epi16(__mmask32 __U, __m512i __A, __m128i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                          (__v32hi)_mm512_sll_epi16(__A, __B),
                                          (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_slli_epi16(__m512i __A, int __B)
{
  return (__m512i)__builtin_ia32_psllwi512((__v32hi)__A, __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_slli_epi16(__m512i __W, __mmask32 __U, __m512i __A, int __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                         (__v32hi)_mm512_slli_epi16(__A, __B),
                                         (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_slli_epi16(__mmask32 __U, __m512i __A, int __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                         (__v32hi)_mm512_slli_epi16(__A, __B),
                                         (__v32hi)_mm512_setzero_si512());
}

#define _mm512_bslli_epi128(a, imm) \
  (__m512i)__builtin_ia32_pslldqi512_byteshift((__v8di)(__m512i)(a), (int)(imm))

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_srlv_epi16(__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_psrlv32hi((__v32hi)__A, (__v32hi)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_srlv_epi16(__m512i __W, __mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                           (__v32hi)_mm512_srlv_epi16(__A, __B),
                                           (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_srlv_epi16(__mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                           (__v32hi)_mm512_srlv_epi16(__A, __B),
                                           (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_srav_epi16(__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_psrav32hi((__v32hi)__A, (__v32hi)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_srav_epi16(__m512i __W, __mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                           (__v32hi)_mm512_srav_epi16(__A, __B),
                                           (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_srav_epi16(__mmask32 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                           (__v32hi)_mm512_srav_epi16(__A, __B),
                                           (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_sra_epi16(__m512i __A, __m128i __B)
{
  return (__m512i)__builtin_ia32_psraw512((__v32hi) __A, (__v8hi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_sra_epi16(__m512i __W, __mmask32 __U, __m512i __A, __m128i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                          (__v32hi)_mm512_sra_epi16(__A, __B),
                                          (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_sra_epi16(__mmask32 __U, __m512i __A, __m128i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                          (__v32hi)_mm512_sra_epi16(__A, __B),
                                          (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_srai_epi16(__m512i __A, int __B)
{
  return (__m512i)__builtin_ia32_psrawi512((__v32hi)__A, __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_srai_epi16(__m512i __W, __mmask32 __U, __m512i __A, int __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                         (__v32hi)_mm512_srai_epi16(__A, __B),
                                         (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_srai_epi16(__mmask32 __U, __m512i __A, int __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                         (__v32hi)_mm512_srai_epi16(__A, __B),
                                         (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_srl_epi16(__m512i __A, __m128i __B)
{
  return (__m512i)__builtin_ia32_psrlw512((__v32hi) __A, (__v8hi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_srl_epi16(__m512i __W, __mmask32 __U, __m512i __A, __m128i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                          (__v32hi)_mm512_srl_epi16(__A, __B),
                                          (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_srl_epi16(__mmask32 __U, __m512i __A, __m128i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                          (__v32hi)_mm512_srl_epi16(__A, __B),
                                          (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_srli_epi16(__m512i __A, int __B)
{
  return (__m512i)__builtin_ia32_psrlwi512((__v32hi)__A, __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_srli_epi16(__m512i __W, __mmask32 __U, __m512i __A, int __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                         (__v32hi)_mm512_srli_epi16(__A, __B),
                                         (__v32hi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_srli_epi16(__mmask32 __U, __m512i __A, int __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__U,
                                         (__v32hi)_mm512_srli_epi16(__A, __B),
                                         (__v32hi)_mm512_setzero_si512());
}

#define _mm512_bsrli_epi128(a, imm) \
  (__m512i)__builtin_ia32_psrldqi512_byteshift((__v8di)(__m512i)(a), (int)(imm))

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_mov_epi16 (__m512i __W, __mmask32 __U, __m512i __A)
{
  return (__m512i) __builtin_ia32_selectw_512 ((__mmask32) __U,
                (__v32hi) __A,
                (__v32hi) __W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_mov_epi16 (__mmask32 __U, __m512i __A)
{
  return (__m512i) __builtin_ia32_selectw_512 ((__mmask32) __U,
                (__v32hi) __A,
                (__v32hi) _mm512_setzero_si512 ());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_mov_epi8 (__m512i __W, __mmask64 __U, __m512i __A)
{
  return (__m512i) __builtin_ia32_selectb_512 ((__mmask64) __U,
                (__v64qi) __A,
                (__v64qi) __W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_mov_epi8 (__mmask64 __U, __m512i __A)
{
  return (__m512i) __builtin_ia32_selectb_512 ((__mmask64) __U,
                (__v64qi) __A,
                (__v64qi) _mm512_setzero_si512 ());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_set1_epi8 (__m512i __O, __mmask64 __M, char __A)
{
  return (__m512i) __builtin_ia32_selectb_512(__M,
                                              (__v64qi)_mm512_set1_epi8(__A),
                                              (__v64qi) __O);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_set1_epi8 (__mmask64 __M, char __A)
{
  return (__m512i) __builtin_ia32_selectb_512(__M,
                                              (__v64qi) _mm512_set1_epi8(__A),
                                              (__v64qi) _mm512_setzero_si512());
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS512
_mm512_kunpackd (__mmask64 __A, __mmask64 __B)
{
  return (__mmask64) __builtin_ia32_kunpckdi ((__mmask64) __A,
                (__mmask64) __B);
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS512
_mm512_kunpackw (__mmask32 __A, __mmask32 __B)
{
  return (__mmask32) __builtin_ia32_kunpcksi ((__mmask32) __A,
                (__mmask32) __B);
}

static __inline __m512i __DEFAULT_FN_ATTRS512
_mm512_loadu_epi16 (void const *__P)
{
  struct __loadu_epi16 {
    __m512i __v;
  } __attribute__((__packed__, __may_alias__));
  return ((struct __loadu_epi16*)__P)->__v;
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_loadu_epi16 (__m512i __W, __mmask32 __U, void const *__P)
{
  return (__m512i) __builtin_ia32_loaddquhi512_mask ((__v32hi *) __P,
                 (__v32hi) __W,
                 (__mmask32) __U);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_loadu_epi16 (__mmask32 __U, void const *__P)
{
  return (__m512i) __builtin_ia32_loaddquhi512_mask ((__v32hi *) __P,
                 (__v32hi)
                 _mm512_setzero_si512 (),
                 (__mmask32) __U);
}

static __inline __m512i __DEFAULT_FN_ATTRS512
_mm512_loadu_epi8 (void const *__P)
{
  struct __loadu_epi8 {
    __m512i __v;
  } __attribute__((__packed__, __may_alias__));
  return ((struct __loadu_epi8*)__P)->__v;
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_loadu_epi8 (__m512i __W, __mmask64 __U, void const *__P)
{
  return (__m512i) __builtin_ia32_loaddquqi512_mask ((__v64qi *) __P,
                 (__v64qi) __W,
                 (__mmask64) __U);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_loadu_epi8 (__mmask64 __U, void const *__P)
{
  return (__m512i) __builtin_ia32_loaddquqi512_mask ((__v64qi *) __P,
                 (__v64qi)
                 _mm512_setzero_si512 (),
                 (__mmask64) __U);
}

static __inline void __DEFAULT_FN_ATTRS512
_mm512_storeu_epi16 (void *__P, __m512i __A)
{
  struct __storeu_epi16 {
    __m512i __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_epi16*)__P)->__v = __A;
}

static __inline__ void __DEFAULT_FN_ATTRS512
_mm512_mask_storeu_epi16 (void *__P, __mmask32 __U, __m512i __A)
{
  __builtin_ia32_storedquhi512_mask ((__v32hi *) __P,
             (__v32hi) __A,
             (__mmask32) __U);
}

static __inline void __DEFAULT_FN_ATTRS512
_mm512_storeu_epi8 (void *__P, __m512i __A)
{
  struct __storeu_epi8 {
    __m512i __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_epi8*)__P)->__v = __A;
}

static __inline__ void __DEFAULT_FN_ATTRS512
_mm512_mask_storeu_epi8 (void *__P, __mmask64 __U, __m512i __A)
{
  __builtin_ia32_storedquqi512_mask ((__v64qi *) __P,
             (__v64qi) __A,
             (__mmask64) __U);
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS512
_mm512_test_epi8_mask (__m512i __A, __m512i __B)
{
  return _mm512_cmpneq_epi8_mask (_mm512_and_epi32 (__A, __B),
                                  _mm512_setzero_si512());
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS512
_mm512_mask_test_epi8_mask (__mmask64 __U, __m512i __A, __m512i __B)
{
  return _mm512_mask_cmpneq_epi8_mask (__U, _mm512_and_epi32 (__A, __B),
                                       _mm512_setzero_si512());
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS512
_mm512_test_epi16_mask (__m512i __A, __m512i __B)
{
  return _mm512_cmpneq_epi16_mask (_mm512_and_epi32 (__A, __B),
                                   _mm512_setzero_si512());
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS512
_mm512_mask_test_epi16_mask (__mmask32 __U, __m512i __A, __m512i __B)
{
  return _mm512_mask_cmpneq_epi16_mask (__U, _mm512_and_epi32 (__A, __B),
                                        _mm512_setzero_si512());
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS512
_mm512_testn_epi8_mask (__m512i __A, __m512i __B)
{
  return _mm512_cmpeq_epi8_mask (_mm512_and_epi32 (__A, __B), _mm512_setzero_si512());
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS512
_mm512_mask_testn_epi8_mask (__mmask64 __U, __m512i __A, __m512i __B)
{
  return _mm512_mask_cmpeq_epi8_mask (__U, _mm512_and_epi32 (__A, __B),
                                      _mm512_setzero_si512());
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS512
_mm512_testn_epi16_mask (__m512i __A, __m512i __B)
{
  return _mm512_cmpeq_epi16_mask (_mm512_and_epi32 (__A, __B),
                                  _mm512_setzero_si512());
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS512
_mm512_mask_testn_epi16_mask (__mmask32 __U, __m512i __A, __m512i __B)
{
  return _mm512_mask_cmpeq_epi16_mask (__U, _mm512_and_epi32 (__A, __B),
                                       _mm512_setzero_si512());
}

static __inline__ __mmask64 __DEFAULT_FN_ATTRS512
_mm512_movepi8_mask (__m512i __A)
{
  return (__mmask64) __builtin_ia32_cvtb2mask512 ((__v64qi) __A);
}

static __inline__ __mmask32 __DEFAULT_FN_ATTRS512
_mm512_movepi16_mask (__m512i __A)
{
  return (__mmask32) __builtin_ia32_cvtw2mask512 ((__v32hi) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_movm_epi8 (__mmask64 __A)
{
  return (__m512i) __builtin_ia32_cvtmask2b512 (__A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_movm_epi16 (__mmask32 __A)
{
  return (__m512i) __builtin_ia32_cvtmask2w512 (__A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_broadcastb_epi8 (__m128i __A)
{
  return (__m512i)__builtin_shufflevector((__v16qi) __A, (__v16qi) __A,
                                          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_broadcastb_epi8 (__m512i __O, __mmask64 __M, __m128i __A)
{
  return (__m512i)__builtin_ia32_selectb_512(__M,
                                             (__v64qi) _mm512_broadcastb_epi8(__A),
                                             (__v64qi) __O);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_broadcastb_epi8 (__mmask64 __M, __m128i __A)
{
  return (__m512i)__builtin_ia32_selectb_512(__M,
                                             (__v64qi) _mm512_broadcastb_epi8(__A),
                                             (__v64qi) _mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_set1_epi16 (__m512i __O, __mmask32 __M, short __A)
{
  return (__m512i) __builtin_ia32_selectw_512(__M,
                                              (__v32hi) _mm512_set1_epi16(__A),
                                              (__v32hi) __O);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_set1_epi16 (__mmask32 __M, short __A)
{
  return (__m512i) __builtin_ia32_selectw_512(__M,
                                              (__v32hi) _mm512_set1_epi16(__A),
                                              (__v32hi) _mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_broadcastw_epi16 (__m128i __A)
{
  return (__m512i)__builtin_shufflevector((__v8hi) __A, (__v8hi) __A,
                                          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_broadcastw_epi16 (__m512i __O, __mmask32 __M, __m128i __A)
{
  return (__m512i)__builtin_ia32_selectw_512(__M,
                                             (__v32hi) _mm512_broadcastw_epi16(__A),
                                             (__v32hi) __O);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_broadcastw_epi16 (__mmask32 __M, __m128i __A)
{
  return (__m512i)__builtin_ia32_selectw_512(__M,
                                             (__v32hi) _mm512_broadcastw_epi16(__A),
                                             (__v32hi) _mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_permutexvar_epi16 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_permvarhi512((__v32hi)__B, (__v32hi)__A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_maskz_permutexvar_epi16 (__mmask32 __M, __m512i __A,
        __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__M,
                                    (__v32hi)_mm512_permutexvar_epi16(__A, __B),
                                    (__v32hi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_mask_permutexvar_epi16 (__m512i __W, __mmask32 __M, __m512i __A,
             __m512i __B)
{
  return (__m512i)__builtin_ia32_selectw_512((__mmask32)__M,
                                    (__v32hi)_mm512_permutexvar_epi16(__A, __B),
                                    (__v32hi)__W);
}

#define _mm512_alignr_epi8(A, B, N) \
  (__m512i)__builtin_ia32_palignr512((__v64qi)(__m512i)(A), \
                                     (__v64qi)(__m512i)(B), (int)(N))

#define _mm512_mask_alignr_epi8(W, U, A, B, N) \
  (__m512i)__builtin_ia32_selectb_512((__mmask64)(U), \
                             (__v64qi)_mm512_alignr_epi8((A), (B), (int)(N)), \
                             (__v64qi)(__m512i)(W))

#define _mm512_maskz_alignr_epi8(U, A, B, N) \
  (__m512i)__builtin_ia32_selectb_512((__mmask64)(U), \
                              (__v64qi)_mm512_alignr_epi8((A), (B), (int)(N)), \
                              (__v64qi)(__m512i)_mm512_setzero_si512())

#define _mm512_dbsad_epu8(A, B, imm) \
  (__m512i)__builtin_ia32_dbpsadbw512((__v64qi)(__m512i)(A), \
                                      (__v64qi)(__m512i)(B), (int)(imm))

#define _mm512_mask_dbsad_epu8(W, U, A, B, imm) \
  (__m512i)__builtin_ia32_selectw_512((__mmask32)(U), \
                                  (__v32hi)_mm512_dbsad_epu8((A), (B), (imm)), \
                                  (__v32hi)(__m512i)(W))

#define _mm512_maskz_dbsad_epu8(U, A, B, imm) \
  (__m512i)__builtin_ia32_selectw_512((__mmask32)(U), \
                                  (__v32hi)_mm512_dbsad_epu8((A), (B), (imm)), \
                                  (__v32hi)_mm512_setzero_si512())

static __inline__ __m512i __DEFAULT_FN_ATTRS512
_mm512_sad_epu8 (__m512i __A, __m512i __B)
{
 return (__m512i) __builtin_ia32_psadbw512 ((__v64qi) __A,
               (__v64qi) __B);
}

#undef __DEFAULT_FN_ATTRS512
#undef __DEFAULT_FN_ATTRS

#endif
