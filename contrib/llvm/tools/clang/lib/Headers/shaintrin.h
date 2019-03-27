/*===---- shaintrin.h - SHA intrinsics -------------------------------------===
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
#error "Never use <shaintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __SHAINTRIN_H
#define __SHAINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("sha"), __min_vector_width__(128)))

#define _mm_sha1rnds4_epu32(V1, V2, M) \
  __builtin_ia32_sha1rnds4((__v4si)(__m128i)(V1), (__v4si)(__m128i)(V2), (M))

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha1nexte_epu32(__m128i __X, __m128i __Y)
{
  return (__m128i)__builtin_ia32_sha1nexte((__v4si)__X, (__v4si)__Y);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha1msg1_epu32(__m128i __X, __m128i __Y)
{
  return (__m128i)__builtin_ia32_sha1msg1((__v4si)__X, (__v4si)__Y);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha1msg2_epu32(__m128i __X, __m128i __Y)
{
  return (__m128i)__builtin_ia32_sha1msg2((__v4si)__X, (__v4si)__Y);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha256rnds2_epu32(__m128i __X, __m128i __Y, __m128i __Z)
{
  return (__m128i)__builtin_ia32_sha256rnds2((__v4si)__X, (__v4si)__Y, (__v4si)__Z);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha256msg1_epu32(__m128i __X, __m128i __Y)
{
  return (__m128i)__builtin_ia32_sha256msg1((__v4si)__X, (__v4si)__Y);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha256msg2_epu32(__m128i __X, __m128i __Y)
{
  return (__m128i)__builtin_ia32_sha256msg2((__v4si)__X, (__v4si)__Y);
}

#undef __DEFAULT_FN_ATTRS

#endif /* __SHAINTRIN_H */
