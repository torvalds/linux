/*===---- mm3dnow.h - 3DNow! intrinsics ------------------------------------===
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

#ifndef _MM3DNOW_H_INCLUDED
#define _MM3DNOW_H_INCLUDED

#include <mmintrin.h>
#include <prfchwintrin.h>

typedef float __v2sf __attribute__((__vector_size__(8)));

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("3dnow"), __min_vector_width__(64)))

static __inline__ void __attribute__((__always_inline__, __nodebug__, __target__("3dnow")))
_m_femms(void) {
  __builtin_ia32_femms();
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pavgusb(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pavgusb((__v8qi)__m1, (__v8qi)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pf2id(__m64 __m) {
  return (__m64)__builtin_ia32_pf2id((__v2sf)__m);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfacc(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pfacc((__v2sf)__m1, (__v2sf)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfadd(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pfadd((__v2sf)__m1, (__v2sf)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfcmpeq(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pfcmpeq((__v2sf)__m1, (__v2sf)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfcmpge(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pfcmpge((__v2sf)__m1, (__v2sf)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfcmpgt(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pfcmpgt((__v2sf)__m1, (__v2sf)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfmax(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pfmax((__v2sf)__m1, (__v2sf)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfmin(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pfmin((__v2sf)__m1, (__v2sf)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfmul(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pfmul((__v2sf)__m1, (__v2sf)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfrcp(__m64 __m) {
  return (__m64)__builtin_ia32_pfrcp((__v2sf)__m);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfrcpit1(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pfrcpit1((__v2sf)__m1, (__v2sf)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfrcpit2(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pfrcpit2((__v2sf)__m1, (__v2sf)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfrsqrt(__m64 __m) {
  return (__m64)__builtin_ia32_pfrsqrt((__v2sf)__m);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfrsqrtit1(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pfrsqit1((__v2sf)__m1, (__v2sf)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfsub(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pfsub((__v2sf)__m1, (__v2sf)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfsubr(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pfsubr((__v2sf)__m1, (__v2sf)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pi2fd(__m64 __m) {
  return (__m64)__builtin_ia32_pi2fd((__v2si)__m);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pmulhrw(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pmulhrw((__v4hi)__m1, (__v4hi)__m2);
}

/* Handle the 3dnowa instructions here. */
#undef __DEFAULT_FN_ATTRS
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("3dnowa"), __min_vector_width__(64)))

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pf2iw(__m64 __m) {
  return (__m64)__builtin_ia32_pf2iw((__v2sf)__m);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfnacc(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pfnacc((__v2sf)__m1, (__v2sf)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pfpnacc(__m64 __m1, __m64 __m2) {
  return (__m64)__builtin_ia32_pfpnacc((__v2sf)__m1, (__v2sf)__m2);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pi2fw(__m64 __m) {
  return (__m64)__builtin_ia32_pi2fw((__v2si)__m);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pswapdsf(__m64 __m) {
  return (__m64)__builtin_ia32_pswapdsf((__v2sf)__m);
}

static __inline__ __m64 __DEFAULT_FN_ATTRS
_m_pswapdsi(__m64 __m) {
  return (__m64)__builtin_ia32_pswapdsi((__v2si)__m);
}

#undef __DEFAULT_FN_ATTRS

#endif
