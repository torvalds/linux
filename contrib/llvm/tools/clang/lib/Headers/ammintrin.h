/*===---- ammintrin.h - SSE4a intrinsics -----------------------------------===
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

#ifndef __AMMINTRIN_H
#define __AMMINTRIN_H

#include <pmmintrin.h>

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("sse4a"), __min_vector_width__(128)))

/// Extracts the specified bits from the lower 64 bits of the 128-bit
///    integer vector operand at the index \a idx and of the length \a len.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_extracti_si64(__m128i x, const int len, const int idx);
/// \endcode
///
/// This intrinsic corresponds to the <c> EXTRQ </c> instruction.
///
/// \param x
///    The value from which bits are extracted.
/// \param len
///    Bits [5:0] specify the length; the other bits are ignored. If bits [5:0]
///    are zero, the length is interpreted as 64.
/// \param idx
///    Bits [5:0] specify the index of the least significant bit; the other
///    bits are ignored. If the sum of the index and length is greater than 64,
///    the result is undefined. If the length and index are both zero, bits
///    [63:0] of parameter \a x are extracted. If the length is zero but the
///    index is non-zero, the result is undefined.
/// \returns A 128-bit integer vector whose lower 64 bits contain the bits
///    extracted from the source operand.
#define _mm_extracti_si64(x, len, idx) \
  ((__m128i)__builtin_ia32_extrqi((__v2di)(__m128i)(x), \
                                  (char)(len), (char)(idx)))

/// Extracts the specified bits from the lower 64 bits of the 128-bit
///    integer vector operand at the index and of the length specified by
///    \a __y.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> EXTRQ </c> instruction.
///
/// \param __x
///    The value from which bits are extracted.
/// \param __y
///    Specifies the index of the least significant bit at [13:8] and the
///    length at [5:0]; all other bits are ignored. If bits [5:0] are zero, the
///    length is interpreted as 64. If the sum of the index and length is
///    greater than 64, the result is undefined. If the length and index are
///    both zero, bits [63:0] of parameter \a __x are extracted. If the length
///    is zero but the index is non-zero, the result is undefined.
/// \returns A 128-bit vector whose lower 64 bits contain the bits extracted
///    from the source operand.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_extract_si64(__m128i __x, __m128i __y)
{
  return (__m128i)__builtin_ia32_extrq((__v2di)__x, (__v16qi)__y);
}

/// Inserts bits of a specified length from the source integer vector
///    \a y into the lower 64 bits of the destination integer vector \a x at
///    the index \a idx and of the length \a len.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_inserti_si64(__m128i x, __m128i y, const int len,
/// const int idx);
/// \endcode
///
/// This intrinsic corresponds to the <c> INSERTQ </c> instruction.
///
/// \param x
///    The destination operand where bits will be inserted. The inserted bits
///    are defined by the length \a len and by the index \a idx specifying the
///    least significant bit.
/// \param y
///    The source operand containing the bits to be extracted. The extracted
///    bits are the least significant bits of operand \a y of length \a len.
/// \param len
///    Bits [5:0] specify the length; the other bits are ignored. If bits [5:0]
///    are zero, the length is interpreted as 64.
/// \param idx
///    Bits [5:0] specify the index of the least significant bit; the other
///    bits are ignored. If the sum of the index and length is greater than 64,
///    the result is undefined. If the length and index are both zero, bits
///    [63:0] of parameter \a y are inserted into parameter \a x. If the length
///    is zero but the index is non-zero, the result is undefined.
/// \returns A 128-bit integer vector containing the original lower 64-bits of
///    destination operand \a x with the specified bitfields replaced by the
///    lower bits of source operand \a y. The upper 64 bits of the return value
///    are undefined.
#define _mm_inserti_si64(x, y, len, idx) \
  ((__m128i)__builtin_ia32_insertqi((__v2di)(__m128i)(x), \
                                    (__v2di)(__m128i)(y), \
                                    (char)(len), (char)(idx)))

/// Inserts bits of a specified length from the source integer vector
///    \a __y into the lower 64 bits of the destination integer vector \a __x
///    at the index and of the length specified by \a __y.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> INSERTQ </c> instruction.
///
/// \param __x
///    The destination operand where bits will be inserted. The inserted bits
///    are defined by the length and by the index of the least significant bit
///    specified by operand \a __y.
/// \param __y
///    The source operand containing the bits to be extracted. The extracted
///    bits are the least significant bits of operand \a __y with length
///    specified by bits [69:64]. These are inserted into the destination at the
///    index specified by bits [77:72]; all other bits are ignored. If bits
///    [69:64] are zero, the length is interpreted as 64. If the sum of the
///    index and length is greater than 64, the result is undefined. If the
///    length and index are both zero, bits [63:0] of parameter \a __y are
///    inserted into parameter \a __x. If the length is zero but the index is
///    non-zero, the result is undefined.
/// \returns A 128-bit integer vector containing the original lower 64-bits of
///    destination operand \a __x with the specified bitfields replaced by the
///    lower bits of source operand \a __y. The upper 64 bits of the return
///    value are undefined.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_insert_si64(__m128i __x, __m128i __y)
{
  return (__m128i)__builtin_ia32_insertq((__v2di)__x, (__v2di)__y);
}

/// Stores a 64-bit double-precision value in a 64-bit memory location.
///    To minimize caching, the data is flagged as non-temporal (unlikely to be
///    used again soon).
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> MOVNTSD </c> instruction.
///
/// \param __p
///    The 64-bit memory location used to store the register value.
/// \param __a
///    The 64-bit double-precision floating-point register value to be stored.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_stream_sd(double *__p, __m128d __a)
{
  __builtin_ia32_movntsd(__p, (__v2df)__a);
}

/// Stores a 32-bit single-precision floating-point value in a 32-bit
///    memory location. To minimize caching, the data is flagged as
///    non-temporal (unlikely to be used again soon).
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> MOVNTSS </c> instruction.
///
/// \param __p
///    The 32-bit memory location used to store the register value.
/// \param __a
///    The 32-bit single-precision floating-point register value to be stored.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_stream_ss(float *__p, __m128 __a)
{
  __builtin_ia32_movntss(__p, (__v4sf)__a);
}

#undef __DEFAULT_FN_ATTRS

#endif /* __AMMINTRIN_H */
