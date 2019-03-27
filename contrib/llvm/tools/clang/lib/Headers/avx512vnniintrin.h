/*===------------- avx512vnniintrin.h - VNNI intrinsics ------------------===
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
#error "Never use <avx512vnniintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512VNNIINTRIN_H
#define __AVX512VNNIINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("avx512vnni"), __min_vector_width__(512)))


static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_dpbusd_epi32(__m512i __S, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_vpdpbusd512((__v16si)__S, (__v16si)__A,
                                             (__v16si)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_dpbusd_epi32(__m512i __S, __mmask16 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectd_512(__U,
                                    (__v16si)_mm512_dpbusd_epi32(__S, __A, __B),
                                    (__v16si)__S);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_dpbusd_epi32(__mmask16 __U, __m512i __S, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectd_512(__U,
                                    (__v16si)_mm512_dpbusd_epi32(__S, __A, __B),
                                    (__v16si)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_dpbusds_epi32(__m512i __S, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_vpdpbusds512((__v16si)__S, (__v16si)__A,
                                              (__v16si)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_dpbusds_epi32(__m512i __S, __mmask16 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectd_512(__U,
                                   (__v16si)_mm512_dpbusds_epi32(__S, __A, __B),
                                   (__v16si)__S);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_dpbusds_epi32(__mmask16 __U, __m512i __S, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectd_512(__U,
                                   (__v16si)_mm512_dpbusds_epi32(__S, __A, __B),
                                   (__v16si)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_dpwssd_epi32(__m512i __S, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_vpdpwssd512((__v16si)__S, (__v16si)__A,
                                             (__v16si)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_dpwssd_epi32(__m512i __S, __mmask16 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectd_512(__U,
                                    (__v16si)_mm512_dpwssd_epi32(__S, __A, __B),
                                    (__v16si)__S);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_dpwssd_epi32(__mmask16 __U, __m512i __S, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectd_512(__U,
                                    (__v16si)_mm512_dpwssd_epi32(__S, __A, __B),
                                    (__v16si)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_dpwssds_epi32(__m512i __S, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_vpdpwssds512((__v16si)__S, (__v16si)__A,
                                              (__v16si)__B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_dpwssds_epi32(__m512i __S, __mmask16 __U, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectd_512(__U,
                                   (__v16si)_mm512_dpwssds_epi32(__S, __A, __B),
                                   (__v16si)__S);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_dpwssds_epi32(__mmask16 __U, __m512i __S, __m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_selectd_512(__U,
                                   (__v16si)_mm512_dpwssds_epi32(__S, __A, __B),
                                   (__v16si)_mm512_setzero_si512());
}

#undef __DEFAULT_FN_ATTRS

#endif
