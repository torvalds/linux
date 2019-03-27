/*===------------------ vaesintrin.h - VAES intrinsics ---------------------===
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
#error "Never use <vaesintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __VAESINTRIN_H
#define __VAESINTRIN_H

/* Default attributes for YMM forms. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("vaes"), __min_vector_width__(256)))

/* Default attributes for ZMM forms. */
#define __DEFAULT_FN_ATTRS_F __attribute__((__always_inline__, __nodebug__, __target__("avx512f,vaes"), __min_vector_width__(512)))


static __inline__ __m256i __DEFAULT_FN_ATTRS
 _mm256_aesenc_epi128(__m256i __A, __m256i __B)
{
  return (__m256i) __builtin_ia32_aesenc256((__v4di) __A,
              (__v4di) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS_F
 _mm512_aesenc_epi128(__m512i __A, __m512i __B)
{
  return (__m512i) __builtin_ia32_aesenc512((__v8di) __A,
              (__v8di) __B);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS
 _mm256_aesdec_epi128(__m256i __A, __m256i __B)
{
  return (__m256i) __builtin_ia32_aesdec256((__v4di) __A,
              (__v4di) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS_F
 _mm512_aesdec_epi128(__m512i __A, __m512i __B)
{
  return (__m512i) __builtin_ia32_aesdec512((__v8di) __A,
              (__v8di) __B);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS
 _mm256_aesenclast_epi128(__m256i __A, __m256i __B)
{
  return (__m256i) __builtin_ia32_aesenclast256((__v4di) __A,
              (__v4di) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS_F
 _mm512_aesenclast_epi128(__m512i __A, __m512i __B)
{
  return (__m512i) __builtin_ia32_aesenclast512((__v8di) __A,
              (__v8di) __B);
}

static __inline__ __m256i __DEFAULT_FN_ATTRS
 _mm256_aesdeclast_epi128(__m256i __A, __m256i __B)
{
  return (__m256i) __builtin_ia32_aesdeclast256((__v4di) __A,
              (__v4di) __B);
}

static __inline__ __m512i __DEFAULT_FN_ATTRS_F
 _mm512_aesdeclast_epi128(__m512i __A, __m512i __B)
{
  return (__m512i) __builtin_ia32_aesdeclast512((__v8di) __A,
              (__v8di) __B);
}


#undef __DEFAULT_FN_ATTRS
#undef __DEFAULT_FN_ATTRS_F

#endif
