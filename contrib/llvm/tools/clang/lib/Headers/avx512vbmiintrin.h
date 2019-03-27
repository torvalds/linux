/*===------------- avx512vbmiintrin.h - VBMI intrinsics ------------------===
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
#error "Never use <avx512vbmiintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __VBMIINTRIN_H
#define __VBMIINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("avx512vbmi"), __min_vector_width__(512)))


static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_permutex2var_epi8(__m512i __A, __m512i __I, __m512i __B)
{
  return (__m512i)__builtin_ia32_vpermi2varqi512((__v64qi)__A, (__v64qi)__I,
                                                 (__v64qi) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_permutex2var_epi8(__m512i __A, __mmask64 __U, __m512i __I,
                              __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512(__U,
                               (__v64qi)_mm512_permutex2var_epi8(__A, __I, __B),
                               (__v64qi)__A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask2_permutex2var_epi8(__m512i __A, __m512i __I, __mmask64 __U,
                               __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512(__U,
                               (__v64qi)_mm512_permutex2var_epi8(__A, __I, __B),
                               (__v64qi)__I);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_permutex2var_epi8(__mmask64 __U, __m512i __A, __m512i __I,
                               __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512(__U,
                               (__v64qi)_mm512_permutex2var_epi8(__A, __I, __B),
                               (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_permutexvar_epi8 (__m512i __A, __m512i __B)
{
  return (__m512i)__builtin_ia32_permvarqi512((__v64qi) __B, (__v64qi) __A);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_permutexvar_epi8 (__mmask64 __M, __m512i __A,
        __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                     (__v64qi)_mm512_permutexvar_epi8(__A, __B),
                                     (__v64qi)_mm512_setzero_si512());
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_permutexvar_epi8 (__m512i __W, __mmask64 __M, __m512i __A,
             __m512i __B)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                     (__v64qi)_mm512_permutexvar_epi8(__A, __B),
                                     (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_multishift_epi64_epi8(__m512i __X, __m512i __Y)
{
  return (__m512i)__builtin_ia32_vpmultishiftqb512((__v64qi)__X, (__v64qi) __Y);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_mask_multishift_epi64_epi8(__m512i __W, __mmask64 __M, __m512i __X,
                                  __m512i __Y)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                (__v64qi)_mm512_multishift_epi64_epi8(__X, __Y),
                                (__v64qi)__W);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS
_mm512_maskz_multishift_epi64_epi8(__mmask64 __M, __m512i __X, __m512i __Y)
{
  return (__m512i)__builtin_ia32_selectb_512((__mmask64)__M,
                                (__v64qi)_mm512_multishift_epi64_epi8(__X, __Y),
                                (__v64qi)_mm512_setzero_si512());
}


#undef __DEFAULT_FN_ATTRS

#endif
