/*===---- smmintrin.h - SSE4 intrinsics ------------------------------------===
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

#ifndef __SMMINTRIN_H
#define __SMMINTRIN_H

#include <tmmintrin.h>

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("sse4.1"), __min_vector_width__(128)))

/* SSE4 Rounding macros. */
#define _MM_FROUND_TO_NEAREST_INT    0x00
#define _MM_FROUND_TO_NEG_INF        0x01
#define _MM_FROUND_TO_POS_INF        0x02
#define _MM_FROUND_TO_ZERO           0x03
#define _MM_FROUND_CUR_DIRECTION     0x04

#define _MM_FROUND_RAISE_EXC         0x00
#define _MM_FROUND_NO_EXC            0x08

#define _MM_FROUND_NINT      (_MM_FROUND_RAISE_EXC | _MM_FROUND_TO_NEAREST_INT)
#define _MM_FROUND_FLOOR     (_MM_FROUND_RAISE_EXC | _MM_FROUND_TO_NEG_INF)
#define _MM_FROUND_CEIL      (_MM_FROUND_RAISE_EXC | _MM_FROUND_TO_POS_INF)
#define _MM_FROUND_TRUNC     (_MM_FROUND_RAISE_EXC | _MM_FROUND_TO_ZERO)
#define _MM_FROUND_RINT      (_MM_FROUND_RAISE_EXC | _MM_FROUND_CUR_DIRECTION)
#define _MM_FROUND_NEARBYINT (_MM_FROUND_NO_EXC | _MM_FROUND_CUR_DIRECTION)

/// Rounds up each element of the 128-bit vector of [4 x float] to an
///    integer and returns the rounded values in a 128-bit vector of
///    [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm_ceil_ps(__m128 X);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDPS / ROUNDPS </c> instruction.
///
/// \param X
///    A 128-bit vector of [4 x float] values to be rounded up.
/// \returns A 128-bit vector of [4 x float] containing the rounded values.
#define _mm_ceil_ps(X)       _mm_round_ps((X), _MM_FROUND_CEIL)

/// Rounds up each element of the 128-bit vector of [2 x double] to an
///    integer and returns the rounded values in a 128-bit vector of
///    [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm_ceil_pd(__m128d X);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDPD / ROUNDPD </c> instruction.
///
/// \param X
///    A 128-bit vector of [2 x double] values to be rounded up.
/// \returns A 128-bit vector of [2 x double] containing the rounded values.
#define _mm_ceil_pd(X)       _mm_round_pd((X), _MM_FROUND_CEIL)

/// Copies three upper elements of the first 128-bit vector operand to
///    the corresponding three upper elements of the 128-bit result vector of
///    [4 x float]. Rounds up the lowest element of the second 128-bit vector
///    operand to an integer and copies it to the lowest element of the 128-bit
///    result vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm_ceil_ss(__m128 X, __m128 Y);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDSS / ROUNDSS </c> instruction.
///
/// \param X
///    A 128-bit vector of [4 x float]. The values stored in bits [127:32] are
///    copied to the corresponding bits of the result.
/// \param Y
///    A 128-bit vector of [4 x float]. The value stored in bits [31:0] is
///    rounded up to the nearest integer and copied to the corresponding bits
///    of the result.
/// \returns A 128-bit vector of [4 x float] containing the copied and rounded
///    values.
#define _mm_ceil_ss(X, Y)    _mm_round_ss((X), (Y), _MM_FROUND_CEIL)

/// Copies the upper element of the first 128-bit vector operand to the
///    corresponding upper element of the 128-bit result vector of [2 x double].
///    Rounds up the lower element of the second 128-bit vector operand to an
///    integer and copies it to the lower element of the 128-bit result vector
///    of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm_ceil_sd(__m128d X, __m128d Y);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDSD / ROUNDSD </c> instruction.
///
/// \param X
///    A 128-bit vector of [2 x double]. The value stored in bits [127:64] is
///    copied to the corresponding bits of the result.
/// \param Y
///    A 128-bit vector of [2 x double]. The value stored in bits [63:0] is
///    rounded up to the nearest integer and copied to the corresponding bits
///    of the result.
/// \returns A 128-bit vector of [2 x double] containing the copied and rounded
///    values.
#define _mm_ceil_sd(X, Y)    _mm_round_sd((X), (Y), _MM_FROUND_CEIL)

/// Rounds down each element of the 128-bit vector of [4 x float] to an
///    an integer and returns the rounded values in a 128-bit vector of
///    [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm_floor_ps(__m128 X);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDPS / ROUNDPS </c> instruction.
///
/// \param X
///    A 128-bit vector of [4 x float] values to be rounded down.
/// \returns A 128-bit vector of [4 x float] containing the rounded values.
#define _mm_floor_ps(X)      _mm_round_ps((X), _MM_FROUND_FLOOR)

/// Rounds down each element of the 128-bit vector of [2 x double] to an
///    integer and returns the rounded values in a 128-bit vector of
///    [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm_floor_pd(__m128d X);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDPD / ROUNDPD </c> instruction.
///
/// \param X
///    A 128-bit vector of [2 x double].
/// \returns A 128-bit vector of [2 x double] containing the rounded values.
#define _mm_floor_pd(X)      _mm_round_pd((X), _MM_FROUND_FLOOR)

/// Copies three upper elements of the first 128-bit vector operand to
///    the corresponding three upper elements of the 128-bit result vector of
///    [4 x float]. Rounds down the lowest element of the second 128-bit vector
///    operand to an integer and copies it to the lowest element of the 128-bit
///    result vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm_floor_ss(__m128 X, __m128 Y);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDSS / ROUNDSS </c> instruction.
///
/// \param X
///    A 128-bit vector of [4 x float]. The values stored in bits [127:32] are
///    copied to the corresponding bits of the result.
/// \param Y
///    A 128-bit vector of [4 x float]. The value stored in bits [31:0] is
///    rounded down to the nearest integer and copied to the corresponding bits
///    of the result.
/// \returns A 128-bit vector of [4 x float] containing the copied and rounded
///    values.
#define _mm_floor_ss(X, Y)   _mm_round_ss((X), (Y), _MM_FROUND_FLOOR)

/// Copies the upper element of the first 128-bit vector operand to the
///    corresponding upper element of the 128-bit result vector of [2 x double].
///    Rounds down the lower element of the second 128-bit vector operand to an
///    integer and copies it to the lower element of the 128-bit result vector
///    of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm_floor_sd(__m128d X, __m128d Y);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDSD / ROUNDSD </c> instruction.
///
/// \param X
///    A 128-bit vector of [2 x double]. The value stored in bits [127:64] is
///    copied to the corresponding bits of the result.
/// \param Y
///    A 128-bit vector of [2 x double]. The value stored in bits [63:0] is
///    rounded down to the nearest integer and copied to the corresponding bits
///    of the result.
/// \returns A 128-bit vector of [2 x double] containing the copied and rounded
///    values.
#define _mm_floor_sd(X, Y)   _mm_round_sd((X), (Y), _MM_FROUND_FLOOR)

/// Rounds each element of the 128-bit vector of [4 x float] to an
///    integer value according to the rounding control specified by the second
///    argument and returns the rounded values in a 128-bit vector of
///    [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm_round_ps(__m128 X, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDPS / ROUNDPS </c> instruction.
///
/// \param X
///    A 128-bit vector of [4 x float].
/// \param M
///    An integer value that specifies the rounding operation. \n
///    Bits [7:4] are reserved. \n
///    Bit [3] is a precision exception value: \n
///      0: A normal PE exception is used \n
///      1: The PE field is not updated \n
///    Bit [2] is the rounding control source: \n
///      0: Use bits [1:0] of \a M \n
///      1: Use the current MXCSR setting \n
///    Bits [1:0] contain the rounding control definition: \n
///      00: Nearest \n
///      01: Downward (toward negative infinity) \n
///      10: Upward (toward positive infinity) \n
///      11: Truncated
/// \returns A 128-bit vector of [4 x float] containing the rounded values.
#define _mm_round_ps(X, M) \
  (__m128)__builtin_ia32_roundps((__v4sf)(__m128)(X), (M))

/// Copies three upper elements of the first 128-bit vector operand to
///    the corresponding three upper elements of the 128-bit result vector of
///    [4 x float]. Rounds the lowest element of the second 128-bit vector
///    operand to an integer value according to the rounding control specified
///    by the third argument and copies it to the lowest element of the 128-bit
///    result vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm_round_ss(__m128 X, __m128 Y, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDSS / ROUNDSS </c> instruction.
///
/// \param X
///    A 128-bit vector of [4 x float]. The values stored in bits [127:32] are
///    copied to the corresponding bits of the result.
/// \param Y
///    A 128-bit vector of [4 x float]. The value stored in bits [31:0] is
///    rounded to the nearest integer using the specified rounding control and
///    copied to the corresponding bits of the result.
/// \param M
///    An integer value that specifies the rounding operation. \n
///    Bits [7:4] are reserved. \n
///    Bit [3] is a precision exception value: \n
///      0: A normal PE exception is used \n
///      1: The PE field is not updated \n
///    Bit [2] is the rounding control source: \n
///      0: Use bits [1:0] of \a M \n
///      1: Use the current MXCSR setting \n
///    Bits [1:0] contain the rounding control definition: \n
///      00: Nearest \n
///      01: Downward (toward negative infinity) \n
///      10: Upward (toward positive infinity) \n
///      11: Truncated
/// \returns A 128-bit vector of [4 x float] containing the copied and rounded
///    values.
#define _mm_round_ss(X, Y, M) \
  (__m128)__builtin_ia32_roundss((__v4sf)(__m128)(X), \
                                 (__v4sf)(__m128)(Y), (M))

/// Rounds each element of the 128-bit vector of [2 x double] to an
///    integer value according to the rounding control specified by the second
///    argument and returns the rounded values in a 128-bit vector of
///    [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm_round_pd(__m128d X, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDPD / ROUNDPD </c> instruction.
///
/// \param X
///    A 128-bit vector of [2 x double].
/// \param M
///    An integer value that specifies the rounding operation. \n
///    Bits [7:4] are reserved. \n
///    Bit [3] is a precision exception value: \n
///      0: A normal PE exception is used \n
///      1: The PE field is not updated \n
///    Bit [2] is the rounding control source: \n
///      0: Use bits [1:0] of \a M \n
///      1: Use the current MXCSR setting \n
///    Bits [1:0] contain the rounding control definition: \n
///      00: Nearest \n
///      01: Downward (toward negative infinity) \n
///      10: Upward (toward positive infinity) \n
///      11: Truncated
/// \returns A 128-bit vector of [2 x double] containing the rounded values.
#define _mm_round_pd(X, M) \
  (__m128d)__builtin_ia32_roundpd((__v2df)(__m128d)(X), (M))

/// Copies the upper element of the first 128-bit vector operand to the
///    corresponding upper element of the 128-bit result vector of [2 x double].
///    Rounds the lower element of the second 128-bit vector operand to an
///    integer value according to the rounding control specified by the third
///    argument and copies it to the lower element of the 128-bit result vector
///    of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm_round_sd(__m128d X, __m128d Y, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDSD / ROUNDSD </c> instruction.
///
/// \param X
///    A 128-bit vector of [2 x double]. The value stored in bits [127:64] is
///    copied to the corresponding bits of the result.
/// \param Y
///    A 128-bit vector of [2 x double]. The value stored in bits [63:0] is
///    rounded to the nearest integer using the specified rounding control and
///    copied to the corresponding bits of the result.
/// \param M
///    An integer value that specifies the rounding operation. \n
///    Bits [7:4] are reserved. \n
///    Bit [3] is a precision exception value: \n
///      0: A normal PE exception is used \n
///      1: The PE field is not updated \n
///    Bit [2] is the rounding control source: \n
///      0: Use bits [1:0] of \a M \n
///      1: Use the current MXCSR setting \n
///    Bits [1:0] contain the rounding control definition: \n
///      00: Nearest \n
///      01: Downward (toward negative infinity) \n
///      10: Upward (toward positive infinity) \n
///      11: Truncated
/// \returns A 128-bit vector of [2 x double] containing the copied and rounded
///    values.
#define _mm_round_sd(X, Y, M) \
  (__m128d)__builtin_ia32_roundsd((__v2df)(__m128d)(X), \
                                  (__v2df)(__m128d)(Y), (M))

/* SSE4 Packed Blending Intrinsics.  */
/// Returns a 128-bit vector of [2 x double] where the values are
///    selected from either the first or second operand as specified by the
///    third operand, the control mask.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm_blend_pd(__m128d V1, __m128d V2, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VBLENDPD / BLENDPD </c> instruction.
///
/// \param V1
///    A 128-bit vector of [2 x double].
/// \param V2
///    A 128-bit vector of [2 x double].
/// \param M
///    An immediate integer operand, with mask bits [1:0] specifying how the
///    values are to be copied. The position of the mask bit corresponds to the
///    index of a copied value. When a mask bit is 0, the corresponding 64-bit
///    element in operand \a V1 is copied to the same position in the result.
///    When a mask bit is 1, the corresponding 64-bit element in operand \a V2
///    is copied to the same position in the result.
/// \returns A 128-bit vector of [2 x double] containing the copied values.
#define _mm_blend_pd(V1, V2, M) \
  (__m128d) __builtin_ia32_blendpd ((__v2df)(__m128d)(V1), \
                                    (__v2df)(__m128d)(V2), (int)(M))

/// Returns a 128-bit vector of [4 x float] where the values are selected
///    from either the first or second operand as specified by the third
///    operand, the control mask.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm_blend_ps(__m128 V1, __m128 V2, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VBLENDPS / BLENDPS </c> instruction.
///
/// \param V1
///    A 128-bit vector of [4 x float].
/// \param V2
///    A 128-bit vector of [4 x float].
/// \param M
///    An immediate integer operand, with mask bits [3:0] specifying how the
///    values are to be copied. The position of the mask bit corresponds to the
///    index of a copied value. When a mask bit is 0, the corresponding 32-bit
///    element in operand \a V1 is copied to the same position in the result.
///    When a mask bit is 1, the corresponding 32-bit element in operand \a V2
///    is copied to the same position in the result.
/// \returns A 128-bit vector of [4 x float] containing the copied values.
#define _mm_blend_ps(V1, V2, M) \
  (__m128) __builtin_ia32_blendps ((__v4sf)(__m128)(V1), \
                                   (__v4sf)(__m128)(V2), (int)(M))

/// Returns a 128-bit vector of [2 x double] where the values are
///    selected from either the first or second operand as specified by the
///    third operand, the control mask.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VBLENDVPD / BLENDVPD </c> instruction.
///
/// \param __V1
///    A 128-bit vector of [2 x double].
/// \param __V2
///    A 128-bit vector of [2 x double].
/// \param __M
///    A 128-bit vector operand, with mask bits 127 and 63 specifying how the
///    values are to be copied. The position of the mask bit corresponds to the
///    most significant bit of a copied value. When a mask bit is 0, the
///    corresponding 64-bit element in operand \a __V1 is copied to the same
///    position in the result. When a mask bit is 1, the corresponding 64-bit
///    element in operand \a __V2 is copied to the same position in the result.
/// \returns A 128-bit vector of [2 x double] containing the copied values.
static __inline__ __m128d __DEFAULT_FN_ATTRS
_mm_blendv_pd (__m128d __V1, __m128d __V2, __m128d __M)
{
  return (__m128d) __builtin_ia32_blendvpd ((__v2df)__V1, (__v2df)__V2,
                                            (__v2df)__M);
}

/// Returns a 128-bit vector of [4 x float] where the values are
///    selected from either the first or second operand as specified by the
///    third operand, the control mask.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VBLENDVPS / BLENDVPS </c> instruction.
///
/// \param __V1
///    A 128-bit vector of [4 x float].
/// \param __V2
///    A 128-bit vector of [4 x float].
/// \param __M
///    A 128-bit vector operand, with mask bits 127, 95, 63, and 31 specifying
///    how the values are to be copied. The position of the mask bit corresponds
///    to the most significant bit of a copied value. When a mask bit is 0, the
///    corresponding 32-bit element in operand \a __V1 is copied to the same
///    position in the result. When a mask bit is 1, the corresponding 32-bit
///    element in operand \a __V2 is copied to the same position in the result.
/// \returns A 128-bit vector of [4 x float] containing the copied values.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_blendv_ps (__m128 __V1, __m128 __V2, __m128 __M)
{
  return (__m128) __builtin_ia32_blendvps ((__v4sf)__V1, (__v4sf)__V2,
                                           (__v4sf)__M);
}

/// Returns a 128-bit vector of [16 x i8] where the values are selected
///    from either of the first or second operand as specified by the third
///    operand, the control mask.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPBLENDVB / PBLENDVB </c> instruction.
///
/// \param __V1
///    A 128-bit vector of [16 x i8].
/// \param __V2
///    A 128-bit vector of [16 x i8].
/// \param __M
///    A 128-bit vector operand, with mask bits 127, 119, 111...7 specifying
///    how the values are to be copied. The position of the mask bit corresponds
///    to the most significant bit of a copied value. When a mask bit is 0, the
///    corresponding 8-bit element in operand \a __V1 is copied to the same
///    position in the result. When a mask bit is 1, the corresponding 8-bit
///    element in operand \a __V2 is copied to the same position in the result.
/// \returns A 128-bit vector of [16 x i8] containing the copied values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_blendv_epi8 (__m128i __V1, __m128i __V2, __m128i __M)
{
  return (__m128i) __builtin_ia32_pblendvb128 ((__v16qi)__V1, (__v16qi)__V2,
                                               (__v16qi)__M);
}

/// Returns a 128-bit vector of [8 x i16] where the values are selected
///    from either of the first or second operand as specified by the third
///    operand, the control mask.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_blend_epi16(__m128i V1, __m128i V2, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPBLENDW / PBLENDW </c> instruction.
///
/// \param V1
///    A 128-bit vector of [8 x i16].
/// \param V2
///    A 128-bit vector of [8 x i16].
/// \param M
///    An immediate integer operand, with mask bits [7:0] specifying how the
///    values are to be copied. The position of the mask bit corresponds to the
///    index of a copied value. When a mask bit is 0, the corresponding 16-bit
///    element in operand \a V1 is copied to the same position in the result.
///    When a mask bit is 1, the corresponding 16-bit element in operand \a V2
///    is copied to the same position in the result.
/// \returns A 128-bit vector of [8 x i16] containing the copied values.
#define _mm_blend_epi16(V1, V2, M) \
  (__m128i) __builtin_ia32_pblendw128 ((__v8hi)(__m128i)(V1), \
                                       (__v8hi)(__m128i)(V2), (int)(M))

/* SSE4 Dword Multiply Instructions.  */
/// Multiples corresponding elements of two 128-bit vectors of [4 x i32]
///    and returns the lower 32 bits of the each product in a 128-bit vector of
///    [4 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMULLD / PMULLD </c> instruction.
///
/// \param __V1
///    A 128-bit integer vector.
/// \param __V2
///    A 128-bit integer vector.
/// \returns A 128-bit integer vector containing the products of both operands.
static __inline__  __m128i __DEFAULT_FN_ATTRS
_mm_mullo_epi32 (__m128i __V1, __m128i __V2)
{
  return (__m128i) ((__v4su)__V1 * (__v4su)__V2);
}

/// Multiplies corresponding even-indexed elements of two 128-bit
///    vectors of [4 x i32] and returns a 128-bit vector of [2 x i64]
///    containing the products.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMULDQ / PMULDQ </c> instruction.
///
/// \param __V1
///    A 128-bit vector of [4 x i32].
/// \param __V2
///    A 128-bit vector of [4 x i32].
/// \returns A 128-bit vector of [2 x i64] containing the products of both
///    operands.
static __inline__  __m128i __DEFAULT_FN_ATTRS
_mm_mul_epi32 (__m128i __V1, __m128i __V2)
{
  return (__m128i) __builtin_ia32_pmuldq128 ((__v4si)__V1, (__v4si)__V2);
}

/* SSE4 Floating Point Dot Product Instructions.  */
/// Computes the dot product of the two 128-bit vectors of [4 x float]
///    and returns it in the elements of the 128-bit result vector of
///    [4 x float].
///
///    The immediate integer operand controls which input elements
///    will contribute to the dot product, and where the final results are
///    returned.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm_dp_ps(__m128 X, __m128 Y, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VDPPS / DPPS </c> instruction.
///
/// \param X
///    A 128-bit vector of [4 x float].
/// \param Y
///    A 128-bit vector of [4 x float].
/// \param M
///    An immediate integer operand. Mask bits [7:4] determine which elements
///    of the input vectors are used, with bit [4] corresponding to the lowest
///    element and bit [7] corresponding to the highest element of each [4 x
///    float] vector. If a bit is set, the corresponding elements from the two
///    input vectors are used as an input for dot product; otherwise that input
///    is treated as zero. Bits [3:0] determine which elements of the result
///    will receive a copy of the final dot product, with bit [0] corresponding
///    to the lowest element and bit [3] corresponding to the highest element of
///    each [4 x float] subvector. If a bit is set, the dot product is returned
///    in the corresponding element; otherwise that element is set to zero.
/// \returns A 128-bit vector of [4 x float] containing the dot product.
#define _mm_dp_ps(X, Y, M) \
  (__m128) __builtin_ia32_dpps((__v4sf)(__m128)(X), \
                               (__v4sf)(__m128)(Y), (M))

/// Computes the dot product of the two 128-bit vectors of [2 x double]
///    and returns it in the elements of the 128-bit result vector of
///    [2 x double].
///
///    The immediate integer operand controls which input
///    elements will contribute to the dot product, and where the final results
///    are returned.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm_dp_pd(__m128d X, __m128d Y, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VDPPD / DPPD </c> instruction.
///
/// \param X
///    A 128-bit vector of [2 x double].
/// \param Y
///    A 128-bit vector of [2 x double].
/// \param M
///    An immediate integer operand. Mask bits [5:4] determine which elements
///    of the input vectors are used, with bit [4] corresponding to the lowest
///    element and bit [5] corresponding to the highest element of each of [2 x
///    double] vector. If a bit is set, the corresponding elements from the two
///    input vectors are used as an input for dot product; otherwise that input
///    is treated as zero. Bits [1:0] determine which elements of the result
///    will receive a copy of the final dot product, with bit [0] corresponding
///    to the lowest element and bit [1] corresponding to the highest element of
///    each [2 x double] vector. If a bit is set, the dot product is returned in
///    the corresponding element; otherwise that element is set to zero.
#define _mm_dp_pd(X, Y, M) \
  (__m128d) __builtin_ia32_dppd((__v2df)(__m128d)(X), \
                                (__v2df)(__m128d)(Y), (M))

/* SSE4 Streaming Load Hint Instruction.  */
/// Loads integer values from a 128-bit aligned memory location to a
///    128-bit integer vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVNTDQA / MOVNTDQA </c> instruction.
///
/// \param __V
///    A pointer to a 128-bit aligned memory location that contains the integer
///    values.
/// \returns A 128-bit integer vector containing the data stored at the
///    specified memory location.
static __inline__  __m128i __DEFAULT_FN_ATTRS
_mm_stream_load_si128 (__m128i const *__V)
{
  return (__m128i) __builtin_nontemporal_load ((const __v2di *) __V);
}

/* SSE4 Packed Integer Min/Max Instructions.  */
/// Compares the corresponding elements of two 128-bit vectors of
///    [16 x i8] and returns a 128-bit vector of [16 x i8] containing the lesser
///    of the two values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMINSB / PMINSB </c> instruction.
///
/// \param __V1
///    A 128-bit vector of [16 x i8].
/// \param __V2
///    A 128-bit vector of [16 x i8]
/// \returns A 128-bit vector of [16 x i8] containing the lesser values.
static __inline__  __m128i __DEFAULT_FN_ATTRS
_mm_min_epi8 (__m128i __V1, __m128i __V2)
{
  return (__m128i) __builtin_ia32_pminsb128 ((__v16qi) __V1, (__v16qi) __V2);
}

/// Compares the corresponding elements of two 128-bit vectors of
///    [16 x i8] and returns a 128-bit vector of [16 x i8] containing the
///    greater value of the two.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMAXSB / PMAXSB </c> instruction.
///
/// \param __V1
///    A 128-bit vector of [16 x i8].
/// \param __V2
///    A 128-bit vector of [16 x i8].
/// \returns A 128-bit vector of [16 x i8] containing the greater values.
static __inline__  __m128i __DEFAULT_FN_ATTRS
_mm_max_epi8 (__m128i __V1, __m128i __V2)
{
  return (__m128i) __builtin_ia32_pmaxsb128 ((__v16qi) __V1, (__v16qi) __V2);
}

/// Compares the corresponding elements of two 128-bit vectors of
///    [8 x u16] and returns a 128-bit vector of [8 x u16] containing the lesser
///    value of the two.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMINUW / PMINUW </c> instruction.
///
/// \param __V1
///    A 128-bit vector of [8 x u16].
/// \param __V2
///    A 128-bit vector of [8 x u16].
/// \returns A 128-bit vector of [8 x u16] containing the lesser values.
static __inline__  __m128i __DEFAULT_FN_ATTRS
_mm_min_epu16 (__m128i __V1, __m128i __V2)
{
  return (__m128i) __builtin_ia32_pminuw128 ((__v8hi) __V1, (__v8hi) __V2);
}

/// Compares the corresponding elements of two 128-bit vectors of
///    [8 x u16] and returns a 128-bit vector of [8 x u16] containing the
///    greater value of the two.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMAXUW / PMAXUW </c> instruction.
///
/// \param __V1
///    A 128-bit vector of [8 x u16].
/// \param __V2
///    A 128-bit vector of [8 x u16].
/// \returns A 128-bit vector of [8 x u16] containing the greater values.
static __inline__  __m128i __DEFAULT_FN_ATTRS
_mm_max_epu16 (__m128i __V1, __m128i __V2)
{
  return (__m128i) __builtin_ia32_pmaxuw128 ((__v8hi) __V1, (__v8hi) __V2);
}

/// Compares the corresponding elements of two 128-bit vectors of
///    [4 x i32] and returns a 128-bit vector of [4 x i32] containing the lesser
///    value of the two.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMINSD / PMINSD </c> instruction.
///
/// \param __V1
///    A 128-bit vector of [4 x i32].
/// \param __V2
///    A 128-bit vector of [4 x i32].
/// \returns A 128-bit vector of [4 x i32] containing the lesser values.
static __inline__  __m128i __DEFAULT_FN_ATTRS
_mm_min_epi32 (__m128i __V1, __m128i __V2)
{
  return (__m128i) __builtin_ia32_pminsd128 ((__v4si) __V1, (__v4si) __V2);
}

/// Compares the corresponding elements of two 128-bit vectors of
///    [4 x i32] and returns a 128-bit vector of [4 x i32] containing the
///    greater value of the two.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMAXSD / PMAXSD </c> instruction.
///
/// \param __V1
///    A 128-bit vector of [4 x i32].
/// \param __V2
///    A 128-bit vector of [4 x i32].
/// \returns A 128-bit vector of [4 x i32] containing the greater values.
static __inline__  __m128i __DEFAULT_FN_ATTRS
_mm_max_epi32 (__m128i __V1, __m128i __V2)
{
  return (__m128i) __builtin_ia32_pmaxsd128 ((__v4si) __V1, (__v4si) __V2);
}

/// Compares the corresponding elements of two 128-bit vectors of
///    [4 x u32] and returns a 128-bit vector of [4 x u32] containing the lesser
///    value of the two.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMINUD / PMINUD </c>  instruction.
///
/// \param __V1
///    A 128-bit vector of [4 x u32].
/// \param __V2
///    A 128-bit vector of [4 x u32].
/// \returns A 128-bit vector of [4 x u32] containing the lesser values.
static __inline__  __m128i __DEFAULT_FN_ATTRS
_mm_min_epu32 (__m128i __V1, __m128i __V2)
{
  return (__m128i) __builtin_ia32_pminud128((__v4si) __V1, (__v4si) __V2);
}

/// Compares the corresponding elements of two 128-bit vectors of
///    [4 x u32] and returns a 128-bit vector of [4 x u32] containing the
///    greater value of the two.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMAXUD / PMAXUD </c> instruction.
///
/// \param __V1
///    A 128-bit vector of [4 x u32].
/// \param __V2
///    A 128-bit vector of [4 x u32].
/// \returns A 128-bit vector of [4 x u32] containing the greater values.
static __inline__  __m128i __DEFAULT_FN_ATTRS
_mm_max_epu32 (__m128i __V1, __m128i __V2)
{
  return (__m128i) __builtin_ia32_pmaxud128((__v4si) __V1, (__v4si) __V2);
}

/* SSE4 Insertion and Extraction from XMM Register Instructions.  */
/// Takes the first argument \a X and inserts an element from the second
///    argument \a Y as selected by the third argument \a N. That result then
///    has elements zeroed out also as selected by the third argument \a N. The
///    resulting 128-bit vector of [4 x float] is then returned.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm_insert_ps(__m128 X, __m128 Y, const int N);
/// \endcode
///
/// This intrinsic corresponds to the <c> VINSERTPS </c> instruction.
///
/// \param X
///    A 128-bit vector source operand of [4 x float]. With the exception of
///    those bits in the result copied from parameter \a Y and zeroed by bits
///    [3:0] of \a N, all bits from this parameter are copied to the result.
/// \param Y
///    A 128-bit vector source operand of [4 x float]. One single-precision
///    floating-point element from this source, as determined by the immediate
///    parameter, is copied to the result.
/// \param N
///    Specifies which bits from operand \a Y will be copied, which bits in the
///    result they will be be copied to, and which bits in the result will be
///    cleared. The following assignments are made: \n
///    Bits [7:6] specify the bits to copy from operand \a Y: \n
///      00: Selects bits [31:0] from operand \a Y. \n
///      01: Selects bits [63:32] from operand \a Y. \n
///      10: Selects bits [95:64] from operand \a Y. \n
///      11: Selects bits [127:96] from operand \a Y. \n
///    Bits [5:4] specify the bits in the result to which the selected bits
///    from operand \a Y are copied: \n
///      00: Copies the selected bits from \a Y to result bits [31:0]. \n
///      01: Copies the selected bits from \a Y to result bits [63:32]. \n
///      10: Copies the selected bits from \a Y to result bits [95:64]. \n
///      11: Copies the selected bits from \a Y to result bits [127:96]. \n
///    Bits[3:0]: If any of these bits are set, the corresponding result
///    element is cleared.
/// \returns A 128-bit vector of [4 x float] containing the copied
///    single-precision floating point elements from the operands.
#define _mm_insert_ps(X, Y, N) __builtin_ia32_insertps128((X), (Y), (N))

/// Extracts a 32-bit integer from a 128-bit vector of [4 x float] and
///    returns it, using the immediate value parameter \a N as a selector.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_extract_ps(__m128 X, const int N);
/// \endcode
///
/// This intrinsic corresponds to the <c> VEXTRACTPS / EXTRACTPS </c>
/// instruction.
///
/// \param X
///    A 128-bit vector of [4 x float].
/// \param N
///    An immediate value. Bits [1:0] determines which bits from the argument
///    \a X are extracted and returned: \n
///    00: Bits [31:0] of parameter \a X are returned. \n
///    01: Bits [63:32] of parameter \a X are returned. \n
///    10: Bits [95:64] of parameter \a X are returned. \n
///    11: Bits [127:96] of parameter \a X are returned.
/// \returns A 32-bit integer containing the extracted 32 bits of float data.
#define _mm_extract_ps(X, N) (__extension__                      \
  ({ union { int __i; float __f; } __t;  \
     __t.__f = __builtin_ia32_vec_ext_v4sf((__v4sf)(__m128)(X), (int)(N)); \
     __t.__i;}))

/* Miscellaneous insert and extract macros.  */
/* Extract a single-precision float from X at index N into D.  */
#define _MM_EXTRACT_FLOAT(D, X, N) \
  { (D) = __builtin_ia32_vec_ext_v4sf((__v4sf)(__m128)(X), (int)(N)); }

/* Or together 2 sets of indexes (X and Y) with the zeroing bits (Z) to create
   an index suitable for _mm_insert_ps.  */
#define _MM_MK_INSERTPS_NDX(X, Y, Z) (((X) << 6) | ((Y) << 4) | (Z))

/* Extract a float from X at index N into the first index of the return.  */
#define _MM_PICK_OUT_PS(X, N) _mm_insert_ps (_mm_setzero_ps(), (X),   \
                                             _MM_MK_INSERTPS_NDX((N), 0, 0x0e))

/* Insert int into packed integer array at index.  */
/// Constructs a 128-bit vector of [16 x i8] by first making a copy of
///    the 128-bit integer vector parameter, and then inserting the lower 8 bits
///    of an integer parameter \a I into an offset specified by the immediate
///    value parameter \a N.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_insert_epi8(__m128i X, int I, const int N);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPINSRB / PINSRB </c> instruction.
///
/// \param X
///    A 128-bit integer vector of [16 x i8]. This vector is copied to the
///    result and then one of the sixteen elements in the result vector is
///    replaced by the lower 8 bits of \a I.
/// \param I
///    An integer. The lower 8 bits of this operand are written to the result
///    beginning at the offset specified by \a N.
/// \param N
///    An immediate value. Bits [3:0] specify the bit offset in the result at
///    which the lower 8 bits of \a I are written. \n
///    0000: Bits [7:0] of the result are used for insertion. \n
///    0001: Bits [15:8] of the result are used for insertion. \n
///    0010: Bits [23:16] of the result are used for insertion. \n
///    0011: Bits [31:24] of the result are used for insertion. \n
///    0100: Bits [39:32] of the result are used for insertion. \n
///    0101: Bits [47:40] of the result are used for insertion. \n
///    0110: Bits [55:48] of the result are used for insertion. \n
///    0111: Bits [63:56] of the result are used for insertion. \n
///    1000: Bits [71:64] of the result are used for insertion. \n
///    1001: Bits [79:72] of the result are used for insertion. \n
///    1010: Bits [87:80] of the result are used for insertion. \n
///    1011: Bits [95:88] of the result are used for insertion. \n
///    1100: Bits [103:96] of the result are used for insertion. \n
///    1101: Bits [111:104] of the result are used for insertion. \n
///    1110: Bits [119:112] of the result are used for insertion. \n
///    1111: Bits [127:120] of the result are used for insertion.
/// \returns A 128-bit integer vector containing the constructed values.
#define _mm_insert_epi8(X, I, N) \
  (__m128i)__builtin_ia32_vec_set_v16qi((__v16qi)(__m128i)(X), \
                                        (int)(I), (int)(N))

/// Constructs a 128-bit vector of [4 x i32] by first making a copy of
///    the 128-bit integer vector parameter, and then inserting the 32-bit
///    integer parameter \a I at the offset specified by the immediate value
///    parameter \a N.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_insert_epi32(__m128i X, int I, const int N);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPINSRD / PINSRD </c> instruction.
///
/// \param X
///    A 128-bit integer vector of [4 x i32]. This vector is copied to the
///    result and then one of the four elements in the result vector is
///    replaced by \a I.
/// \param I
///    A 32-bit integer that is written to the result beginning at the offset
///    specified by \a N.
/// \param N
///    An immediate value. Bits [1:0] specify the bit offset in the result at
///    which the integer \a I is written. \n
///    00: Bits [31:0] of the result are used for insertion. \n
///    01: Bits [63:32] of the result are used for insertion. \n
///    10: Bits [95:64] of the result are used for insertion. \n
///    11: Bits [127:96] of the result are used for insertion.
/// \returns A 128-bit integer vector containing the constructed values.
#define _mm_insert_epi32(X, I, N) \
  (__m128i)__builtin_ia32_vec_set_v4si((__v4si)(__m128i)(X), \
                                       (int)(I), (int)(N))

#ifdef __x86_64__
/// Constructs a 128-bit vector of [2 x i64] by first making a copy of
///    the 128-bit integer vector parameter, and then inserting the 64-bit
///    integer parameter \a I, using the immediate value parameter \a N as an
///    insertion location selector.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_insert_epi64(__m128i X, long long I, const int N);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPINSRQ / PINSRQ </c> instruction.
///
/// \param X
///    A 128-bit integer vector of [2 x i64]. This vector is copied to the
///    result and then one of the two elements in the result vector is replaced
///    by \a I.
/// \param I
///    A 64-bit integer that is written to the result beginning at the offset
///    specified by \a N.
/// \param N
///    An immediate value. Bit [0] specifies the bit offset in the result at
///    which the integer \a I is written. \n
///    0: Bits [63:0] of the result are used for insertion. \n
///    1: Bits [127:64] of the result are used for insertion. \n
/// \returns A 128-bit integer vector containing the constructed values.
#define _mm_insert_epi64(X, I, N) \
  (__m128i)__builtin_ia32_vec_set_v2di((__v2di)(__m128i)(X), \
                                       (long long)(I), (int)(N))
#endif /* __x86_64__ */

/* Extract int from packed integer array at index.  This returns the element
 * as a zero extended value, so it is unsigned.
 */
/// Extracts an 8-bit element from the 128-bit integer vector of
///    [16 x i8], using the immediate value parameter \a N as a selector.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_extract_epi8(__m128i X, const int N);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPEXTRB / PEXTRB </c> instruction.
///
/// \param X
///    A 128-bit integer vector.
/// \param N
///    An immediate value. Bits [3:0] specify which 8-bit vector element from
///    the argument \a X to extract and copy to the result. \n
///    0000: Bits [7:0] of parameter \a X are extracted. \n
///    0001: Bits [15:8] of the parameter \a X are extracted. \n
///    0010: Bits [23:16] of the parameter \a X are extracted. \n
///    0011: Bits [31:24] of the parameter \a X are extracted. \n
///    0100: Bits [39:32] of the parameter \a X are extracted. \n
///    0101: Bits [47:40] of the parameter \a X are extracted. \n
///    0110: Bits [55:48] of the parameter \a X are extracted. \n
///    0111: Bits [63:56] of the parameter \a X are extracted. \n
///    1000: Bits [71:64] of the parameter \a X are extracted. \n
///    1001: Bits [79:72] of the parameter \a X are extracted. \n
///    1010: Bits [87:80] of the parameter \a X are extracted. \n
///    1011: Bits [95:88] of the parameter \a X are extracted. \n
///    1100: Bits [103:96] of the parameter \a X are extracted. \n
///    1101: Bits [111:104] of the parameter \a X are extracted. \n
///    1110: Bits [119:112] of the parameter \a X are extracted. \n
///    1111: Bits [127:120] of the parameter \a X are extracted.
/// \returns  An unsigned integer, whose lower 8 bits are selected from the
///    128-bit integer vector parameter and the remaining bits are assigned
///    zeros.
#define _mm_extract_epi8(X, N) \
  (int)(unsigned char)__builtin_ia32_vec_ext_v16qi((__v16qi)(__m128i)(X), \
                                                   (int)(N))

/// Extracts a 32-bit element from the 128-bit integer vector of
///    [4 x i32], using the immediate value parameter \a N as a selector.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_extract_epi32(__m128i X, const int N);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPEXTRD / PEXTRD </c> instruction.
///
/// \param X
///    A 128-bit integer vector.
/// \param N
///    An immediate value. Bits [1:0] specify which 32-bit vector element from
///    the argument \a X to extract and copy to the result. \n
///    00: Bits [31:0] of the parameter \a X are extracted. \n
///    01: Bits [63:32] of the parameter \a X are extracted. \n
///    10: Bits [95:64] of the parameter \a X are extracted. \n
///    11: Bits [127:96] of the parameter \a X are exracted.
/// \returns  An integer, whose lower 32 bits are selected from the 128-bit
///    integer vector parameter and the remaining bits are assigned zeros.
#define _mm_extract_epi32(X, N) \
  (int)__builtin_ia32_vec_ext_v4si((__v4si)(__m128i)(X), (int)(N))

#ifdef __x86_64__
/// Extracts a 64-bit element from the 128-bit integer vector of
///    [2 x i64], using the immediate value parameter \a N as a selector.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// long long _mm_extract_epi64(__m128i X, const int N);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPEXTRQ / PEXTRQ </c> instruction.
///
/// \param X
///    A 128-bit integer vector.
/// \param N
///    An immediate value. Bit [0] specifies which 64-bit vector element from
///    the argument \a X to return. \n
///    0: Bits [63:0] are returned. \n
///    1: Bits [127:64] are returned. \n
/// \returns  A 64-bit integer.
#define _mm_extract_epi64(X, N) \
  (long long)__builtin_ia32_vec_ext_v2di((__v2di)(__m128i)(X), (int)(N))
#endif /* __x86_64 */

/* SSE4 128-bit Packed Integer Comparisons.  */
/// Tests whether the specified bits in a 128-bit integer vector are all
///    zeros.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPTEST / PTEST </c> instruction.
///
/// \param __M
///    A 128-bit integer vector containing the bits to be tested.
/// \param __V
///    A 128-bit integer vector selecting which bits to test in operand \a __M.
/// \returns TRUE if the specified bits are all zeros; FALSE otherwise.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_testz_si128(__m128i __M, __m128i __V)
{
  return __builtin_ia32_ptestz128((__v2di)__M, (__v2di)__V);
}

/// Tests whether the specified bits in a 128-bit integer vector are all
///    ones.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPTEST / PTEST </c> instruction.
///
/// \param __M
///    A 128-bit integer vector containing the bits to be tested.
/// \param __V
///    A 128-bit integer vector selecting which bits to test in operand \a __M.
/// \returns TRUE if the specified bits are all ones; FALSE otherwise.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_testc_si128(__m128i __M, __m128i __V)
{
  return __builtin_ia32_ptestc128((__v2di)__M, (__v2di)__V);
}

/// Tests whether the specified bits in a 128-bit integer vector are
///    neither all zeros nor all ones.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPTEST / PTEST </c> instruction.
///
/// \param __M
///    A 128-bit integer vector containing the bits to be tested.
/// \param __V
///    A 128-bit integer vector selecting which bits to test in operand \a __M.
/// \returns TRUE if the specified bits are neither all zeros nor all ones;
///    FALSE otherwise.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_testnzc_si128(__m128i __M, __m128i __V)
{
  return __builtin_ia32_ptestnzc128((__v2di)__M, (__v2di)__V);
}

/// Tests whether the specified bits in a 128-bit integer vector are all
///    ones.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_test_all_ones(__m128i V);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPTEST / PTEST </c> instruction.
///
/// \param V
///    A 128-bit integer vector containing the bits to be tested.
/// \returns TRUE if the bits specified in the operand are all set to 1; FALSE
///    otherwise.
#define _mm_test_all_ones(V) _mm_testc_si128((V), _mm_cmpeq_epi32((V), (V)))

/// Tests whether the specified bits in a 128-bit integer vector are
///    neither all zeros nor all ones.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_test_mix_ones_zeros(__m128i M, __m128i V);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPTEST / PTEST </c> instruction.
///
/// \param M
///    A 128-bit integer vector containing the bits to be tested.
/// \param V
///    A 128-bit integer vector selecting which bits to test in operand \a M.
/// \returns TRUE if the specified bits are neither all zeros nor all ones;
///    FALSE otherwise.
#define _mm_test_mix_ones_zeros(M, V) _mm_testnzc_si128((M), (V))

/// Tests whether the specified bits in a 128-bit integer vector are all
///    zeros.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_test_all_zeros(__m128i M, __m128i V);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPTEST / PTEST </c> instruction.
///
/// \param M
///    A 128-bit integer vector containing the bits to be tested.
/// \param V
///    A 128-bit integer vector selecting which bits to test in operand \a M.
/// \returns TRUE if the specified bits are all zeros; FALSE otherwise.
#define _mm_test_all_zeros(M, V) _mm_testz_si128 ((M), (V))

/* SSE4 64-bit Packed Integer Comparisons.  */
/// Compares each of the corresponding 64-bit values of the 128-bit
///    integer vectors for equality.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPCMPEQQ / PCMPEQQ </c> instruction.
///
/// \param __V1
///    A 128-bit integer vector.
/// \param __V2
///    A 128-bit integer vector.
/// \returns A 128-bit integer vector containing the comparison results.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_cmpeq_epi64(__m128i __V1, __m128i __V2)
{
  return (__m128i)((__v2di)__V1 == (__v2di)__V2);
}

/* SSE4 Packed Integer Sign-Extension.  */
/// Sign-extends each of the lower eight 8-bit integer elements of a
///    128-bit vector of [16 x i8] to 16-bit values and returns them in a
///    128-bit vector of [8 x i16]. The upper eight elements of the input vector
///    are unused.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMOVSXBW / PMOVSXBW </c> instruction.
///
/// \param __V
///    A 128-bit vector of [16 x i8]. The lower eight 8-bit elements are sign-
///    extended to 16-bit values.
/// \returns A 128-bit vector of [8 x i16] containing the sign-extended values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_cvtepi8_epi16(__m128i __V)
{
  /* This function always performs a signed extension, but __v16qi is a char
     which may be signed or unsigned, so use __v16qs. */
  return (__m128i)__builtin_convertvector(__builtin_shufflevector((__v16qs)__V, (__v16qs)__V, 0, 1, 2, 3, 4, 5, 6, 7), __v8hi);
}

/// Sign-extends each of the lower four 8-bit integer elements of a
///    128-bit vector of [16 x i8] to 32-bit values and returns them in a
///    128-bit vector of [4 x i32]. The upper twelve elements of the input
///    vector are unused.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMOVSXBD / PMOVSXBD </c> instruction.
///
/// \param __V
///    A 128-bit vector of [16 x i8]. The lower four 8-bit elements are
///    sign-extended to 32-bit values.
/// \returns A 128-bit vector of [4 x i32] containing the sign-extended values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_cvtepi8_epi32(__m128i __V)
{
  /* This function always performs a signed extension, but __v16qi is a char
     which may be signed or unsigned, so use __v16qs. */
  return (__m128i)__builtin_convertvector(__builtin_shufflevector((__v16qs)__V, (__v16qs)__V, 0, 1, 2, 3), __v4si);
}

/// Sign-extends each of the lower two 8-bit integer elements of a
///    128-bit integer vector of [16 x i8] to 64-bit values and returns them in
///    a 128-bit vector of [2 x i64]. The upper fourteen elements of the input
///    vector are unused.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMOVSXBQ / PMOVSXBQ </c> instruction.
///
/// \param __V
///    A 128-bit vector of [16 x i8]. The lower two 8-bit elements are
///    sign-extended to 64-bit values.
/// \returns A 128-bit vector of [2 x i64] containing the sign-extended values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_cvtepi8_epi64(__m128i __V)
{
  /* This function always performs a signed extension, but __v16qi is a char
     which may be signed or unsigned, so use __v16qs. */
  return (__m128i)__builtin_convertvector(__builtin_shufflevector((__v16qs)__V, (__v16qs)__V, 0, 1), __v2di);
}

/// Sign-extends each of the lower four 16-bit integer elements of a
///    128-bit integer vector of [8 x i16] to 32-bit values and returns them in
///    a 128-bit vector of [4 x i32]. The upper four elements of the input
///    vector are unused.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMOVSXWD / PMOVSXWD </c> instruction.
///
/// \param __V
///    A 128-bit vector of [8 x i16]. The lower four 16-bit elements are
///    sign-extended to 32-bit values.
/// \returns A 128-bit vector of [4 x i32] containing the sign-extended values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_cvtepi16_epi32(__m128i __V)
{
  return (__m128i)__builtin_convertvector(__builtin_shufflevector((__v8hi)__V, (__v8hi)__V, 0, 1, 2, 3), __v4si);
}

/// Sign-extends each of the lower two 16-bit integer elements of a
///    128-bit integer vector of [8 x i16] to 64-bit values and returns them in
///    a 128-bit vector of [2 x i64]. The upper six elements of the input
///    vector are unused.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMOVSXWQ / PMOVSXWQ </c> instruction.
///
/// \param __V
///    A 128-bit vector of [8 x i16]. The lower two 16-bit elements are
///     sign-extended to 64-bit values.
/// \returns A 128-bit vector of [2 x i64] containing the sign-extended values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_cvtepi16_epi64(__m128i __V)
{
  return (__m128i)__builtin_convertvector(__builtin_shufflevector((__v8hi)__V, (__v8hi)__V, 0, 1), __v2di);
}

/// Sign-extends each of the lower two 32-bit integer elements of a
///    128-bit integer vector of [4 x i32] to 64-bit values and returns them in
///    a 128-bit vector of [2 x i64]. The upper two elements of the input vector
///    are unused.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMOVSXDQ / PMOVSXDQ </c> instruction.
///
/// \param __V
///    A 128-bit vector of [4 x i32]. The lower two 32-bit elements are
///    sign-extended to 64-bit values.
/// \returns A 128-bit vector of [2 x i64] containing the sign-extended values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_cvtepi32_epi64(__m128i __V)
{
  return (__m128i)__builtin_convertvector(__builtin_shufflevector((__v4si)__V, (__v4si)__V, 0, 1), __v2di);
}

/* SSE4 Packed Integer Zero-Extension.  */
/// Zero-extends each of the lower eight 8-bit integer elements of a
///    128-bit vector of [16 x i8] to 16-bit values and returns them in a
///    128-bit vector of [8 x i16]. The upper eight elements of the input vector
///    are unused.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMOVZXBW / PMOVZXBW </c> instruction.
///
/// \param __V
///    A 128-bit vector of [16 x i8]. The lower eight 8-bit elements are
///    zero-extended to 16-bit values.
/// \returns A 128-bit vector of [8 x i16] containing the zero-extended values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_cvtepu8_epi16(__m128i __V)
{
  return (__m128i)__builtin_convertvector(__builtin_shufflevector((__v16qu)__V, (__v16qu)__V, 0, 1, 2, 3, 4, 5, 6, 7), __v8hi);
}

/// Zero-extends each of the lower four 8-bit integer elements of a
///    128-bit vector of [16 x i8] to 32-bit values and returns them in a
///    128-bit vector of [4 x i32]. The upper twelve elements of the input
///    vector are unused.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMOVZXBD / PMOVZXBD </c> instruction.
///
/// \param __V
///    A 128-bit vector of [16 x i8]. The lower four 8-bit elements are
///    zero-extended to 32-bit values.
/// \returns A 128-bit vector of [4 x i32] containing the zero-extended values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_cvtepu8_epi32(__m128i __V)
{
  return (__m128i)__builtin_convertvector(__builtin_shufflevector((__v16qu)__V, (__v16qu)__V, 0, 1, 2, 3), __v4si);
}

/// Zero-extends each of the lower two 8-bit integer elements of a
///    128-bit integer vector of [16 x i8] to 64-bit values and returns them in
///    a 128-bit vector of [2 x i64]. The upper fourteen elements of the input
///    vector are unused.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMOVZXBQ / PMOVZXBQ </c> instruction.
///
/// \param __V
///    A 128-bit vector of [16 x i8]. The lower two 8-bit elements are
///    zero-extended to 64-bit values.
/// \returns A 128-bit vector of [2 x i64] containing the zero-extended values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_cvtepu8_epi64(__m128i __V)
{
  return (__m128i)__builtin_convertvector(__builtin_shufflevector((__v16qu)__V, (__v16qu)__V, 0, 1), __v2di);
}

/// Zero-extends each of the lower four 16-bit integer elements of a
///    128-bit integer vector of [8 x i16] to 32-bit values and returns them in
///    a 128-bit vector of [4 x i32]. The upper four elements of the input
///    vector are unused.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMOVZXWD / PMOVZXWD </c> instruction.
///
/// \param __V
///    A 128-bit vector of [8 x i16]. The lower four 16-bit elements are
///    zero-extended to 32-bit values.
/// \returns A 128-bit vector of [4 x i32] containing the zero-extended values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_cvtepu16_epi32(__m128i __V)
{
  return (__m128i)__builtin_convertvector(__builtin_shufflevector((__v8hu)__V, (__v8hu)__V, 0, 1, 2, 3), __v4si);
}

/// Zero-extends each of the lower two 16-bit integer elements of a
///    128-bit integer vector of [8 x i16] to 64-bit values and returns them in
///    a 128-bit vector of [2 x i64]. The upper six elements of the input vector
///    are unused.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMOVZXWQ / PMOVZXWQ </c> instruction.
///
/// \param __V
///    A 128-bit vector of [8 x i16]. The lower two 16-bit elements are
///    zero-extended to 64-bit values.
/// \returns A 128-bit vector of [2 x i64] containing the zero-extended values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_cvtepu16_epi64(__m128i __V)
{
  return (__m128i)__builtin_convertvector(__builtin_shufflevector((__v8hu)__V, (__v8hu)__V, 0, 1), __v2di);
}

/// Zero-extends each of the lower two 32-bit integer elements of a
///    128-bit integer vector of [4 x i32] to 64-bit values and returns them in
///    a 128-bit vector of [2 x i64]. The upper two elements of the input vector
///    are unused.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPMOVZXDQ / PMOVZXDQ </c> instruction.
///
/// \param __V
///    A 128-bit vector of [4 x i32]. The lower two 32-bit elements are
///    zero-extended to 64-bit values.
/// \returns A 128-bit vector of [2 x i64] containing the zero-extended values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_cvtepu32_epi64(__m128i __V)
{
  return (__m128i)__builtin_convertvector(__builtin_shufflevector((__v4su)__V, (__v4su)__V, 0, 1), __v2di);
}

/* SSE4 Pack with Unsigned Saturation.  */
/// Converts 32-bit signed integers from both 128-bit integer vector
///    operands into 16-bit unsigned integers, and returns the packed result.
///    Values greater than 0xFFFF are saturated to 0xFFFF. Values less than
///    0x0000 are saturated to 0x0000.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPACKUSDW / PACKUSDW </c> instruction.
///
/// \param __V1
///    A 128-bit vector of [4 x i32]. Each 32-bit element is treated as a
///    signed integer and is converted to a 16-bit unsigned integer with
///    saturation. Values greater than 0xFFFF are saturated to 0xFFFF. Values
///    less than 0x0000 are saturated to 0x0000. The converted [4 x i16] values
///    are written to the lower 64 bits of the result.
/// \param __V2
///    A 128-bit vector of [4 x i32]. Each 32-bit element is treated as a
///    signed integer and is converted to a 16-bit unsigned integer with
///    saturation. Values greater than 0xFFFF are saturated to 0xFFFF. Values
///    less than 0x0000 are saturated to 0x0000. The converted [4 x i16] values
///    are written to the higher 64 bits of the result.
/// \returns A 128-bit vector of [8 x i16] containing the converted values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_packus_epi32(__m128i __V1, __m128i __V2)
{
  return (__m128i) __builtin_ia32_packusdw128((__v4si)__V1, (__v4si)__V2);
}

/* SSE4 Multiple Packed Sums of Absolute Difference.  */
/// Subtracts 8-bit unsigned integer values and computes the absolute
///    values of the differences to the corresponding bits in the destination.
///    Then sums of the absolute differences are returned according to the bit
///    fields in the immediate operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_mpsadbw_epu8(__m128i X, __m128i Y, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VMPSADBW / MPSADBW </c> instruction.
///
/// \param X
///    A 128-bit vector of [16 x i8].
/// \param Y
///    A 128-bit vector of [16 x i8].
/// \param M
///    An 8-bit immediate operand specifying how the absolute differences are to
///    be calculated, according to the following algorithm:
///    \code
///    // M2 represents bit 2 of the immediate operand
///    // M10 represents bits [1:0] of the immediate operand
///    i = M2 * 4;
///    j = M10 * 4;
///    for (k = 0; k < 8; k = k + 1) {
///      d0 = abs(X[i + k + 0] - Y[j + 0]);
///      d1 = abs(X[i + k + 1] - Y[j + 1]);
///      d2 = abs(X[i + k + 2] - Y[j + 2]);
///      d3 = abs(X[i + k + 3] - Y[j + 3]);
///      r[k] = d0 + d1 + d2 + d3;
///    }
///    \endcode
/// \returns A 128-bit integer vector containing the sums of the sets of
///    absolute differences between both operands.
#define _mm_mpsadbw_epu8(X, Y, M) \
  (__m128i) __builtin_ia32_mpsadbw128((__v16qi)(__m128i)(X), \
                                      (__v16qi)(__m128i)(Y), (M))

/// Finds the minimum unsigned 16-bit element in the input 128-bit
///    vector of [8 x u16] and returns it and along with its index.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPHMINPOSUW / PHMINPOSUW </c>
/// instruction.
///
/// \param __V
///    A 128-bit vector of [8 x u16].
/// \returns A 128-bit value where bits [15:0] contain the minimum value found
///    in parameter \a __V, bits [18:16] contain the index of the minimum value
///    and the remaining bits are set to 0.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_minpos_epu16(__m128i __V)
{
  return (__m128i) __builtin_ia32_phminposuw128((__v8hi)__V);
}

/* Handle the sse4.2 definitions here. */

/* These definitions are normally in nmmintrin.h, but gcc puts them in here
   so we'll do the same.  */

#undef __DEFAULT_FN_ATTRS
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("sse4.2")))

/* These specify the type of data that we're comparing.  */
#define _SIDD_UBYTE_OPS                 0x00
#define _SIDD_UWORD_OPS                 0x01
#define _SIDD_SBYTE_OPS                 0x02
#define _SIDD_SWORD_OPS                 0x03

/* These specify the type of comparison operation.  */
#define _SIDD_CMP_EQUAL_ANY             0x00
#define _SIDD_CMP_RANGES                0x04
#define _SIDD_CMP_EQUAL_EACH            0x08
#define _SIDD_CMP_EQUAL_ORDERED         0x0c

/* These macros specify the polarity of the operation.  */
#define _SIDD_POSITIVE_POLARITY         0x00
#define _SIDD_NEGATIVE_POLARITY         0x10
#define _SIDD_MASKED_POSITIVE_POLARITY  0x20
#define _SIDD_MASKED_NEGATIVE_POLARITY  0x30

/* These macros are used in _mm_cmpXstri() to specify the return.  */
#define _SIDD_LEAST_SIGNIFICANT         0x00
#define _SIDD_MOST_SIGNIFICANT          0x40

/* These macros are used in _mm_cmpXstri() to specify the return.  */
#define _SIDD_BIT_MASK                  0x00
#define _SIDD_UNIT_MASK                 0x40

/* SSE4.2 Packed Comparison Intrinsics.  */
/// Uses the immediate operand \a M to perform a comparison of string
///    data with implicitly defined lengths that is contained in source operands
///    \a A and \a B. Returns a 128-bit integer vector representing the result
///    mask of the comparison.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_cmpistrm(__m128i A, __m128i B, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCMPISTRM / PCMPISTRM </c>
/// instruction.
///
/// \param A
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param B
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param M
///    An 8-bit immediate operand specifying whether the characters are bytes or
///    words, the type of comparison to perform, and the format of the return
///    value. \n
///    Bits [1:0]: Determine source data format. \n
///      00: 16 unsigned bytes \n
///      01: 8 unsigned words \n
///      10: 16 signed bytes \n
///      11: 8 signed words \n
///    Bits [3:2]: Determine comparison type and aggregation method. \n
///      00: Subset: Each character in \a B is compared for equality with all
///          the characters in \a A. \n
///      01: Ranges: Each character in \a B is compared to \a A. The comparison
///          basis is greater than or equal for even-indexed elements in \a A,
///          and less than or equal for odd-indexed elements in \a A. \n
///      10: Match: Compare each pair of corresponding characters in \a A and
///          \a B for equality. \n
///      11: Substring: Search \a B for substring matches of \a A. \n
///    Bits [5:4]: Determine whether to perform a one's complement on the bit
///                mask of the comparison results. \n
///      00: No effect. \n
///      01: Negate the bit mask. \n
///      10: No effect. \n
///      11: Negate the bit mask only for bits with an index less than or equal
///          to the size of \a A or \a B. \n
///    Bit [6]: Determines whether the result is zero-extended or expanded to 16
///             bytes. \n
///      0: The result is zero-extended to 16 bytes. \n
///      1: The result is expanded to 16 bytes (this expansion is performed by
///         repeating each bit 8 or 16 times).
/// \returns Returns a 128-bit integer vector representing the result mask of
///    the comparison.
#define _mm_cmpistrm(A, B, M) \
  (__m128i)__builtin_ia32_pcmpistrm128((__v16qi)(__m128i)(A), \
                                       (__v16qi)(__m128i)(B), (int)(M))

/// Uses the immediate operand \a M to perform a comparison of string
///    data with implicitly defined lengths that is contained in source operands
///    \a A and \a B. Returns an integer representing the result index of the
///    comparison.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_cmpistri(__m128i A, __m128i B, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCMPISTRI / PCMPISTRI </c>
/// instruction.
///
/// \param A
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param B
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param M
///    An 8-bit immediate operand specifying whether the characters are bytes or
///    words, the type of comparison to perform, and the format of the return
///    value. \n
///    Bits [1:0]: Determine source data format. \n
///      00: 16 unsigned bytes \n
///      01: 8 unsigned words \n
///      10: 16 signed bytes \n
///      11: 8 signed words \n
///    Bits [3:2]: Determine comparison type and aggregation method. \n
///      00: Subset: Each character in \a B is compared for equality with all
///          the characters in \a A. \n
///      01: Ranges: Each character in \a B is compared to \a A. The comparison
///          basis is greater than or equal for even-indexed elements in \a A,
///          and less than or equal for odd-indexed elements in \a A. \n
///      10: Match: Compare each pair of corresponding characters in \a A and
///          \a B for equality. \n
///      11: Substring: Search B for substring matches of \a A. \n
///    Bits [5:4]: Determine whether to perform a one's complement on the bit
///                mask of the comparison results. \n
///      00: No effect. \n
///      01: Negate the bit mask. \n
///      10: No effect. \n
///      11: Negate the bit mask only for bits with an index less than or equal
///          to the size of \a A or \a B. \n
///    Bit [6]: Determines whether the index of the lowest set bit or the
///             highest set bit is returned. \n
///      0: The index of the least significant set bit. \n
///      1: The index of the most significant set bit. \n
/// \returns Returns an integer representing the result index of the comparison.
#define _mm_cmpistri(A, B, M) \
  (int)__builtin_ia32_pcmpistri128((__v16qi)(__m128i)(A), \
                                   (__v16qi)(__m128i)(B), (int)(M))

/// Uses the immediate operand \a M to perform a comparison of string
///    data with explicitly defined lengths that is contained in source operands
///    \a A and \a B. Returns a 128-bit integer vector representing the result
///    mask of the comparison.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_cmpestrm(__m128i A, int LA, __m128i B, int LB, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCMPESTRM / PCMPESTRM </c>
/// instruction.
///
/// \param A
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param LA
///    An integer that specifies the length of the string in \a A.
/// \param B
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param LB
///    An integer that specifies the length of the string in \a B.
/// \param M
///    An 8-bit immediate operand specifying whether the characters are bytes or
///    words, the type of comparison to perform, and the format of the return
///    value. \n
///    Bits [1:0]: Determine source data format. \n
///      00: 16 unsigned bytes \n
///      01: 8 unsigned words \n
///      10: 16 signed bytes \n
///      11: 8 signed words \n
///    Bits [3:2]: Determine comparison type and aggregation method. \n
///      00: Subset: Each character in \a B is compared for equality with all
///          the characters in \a A. \n
///      01: Ranges: Each character in \a B is compared to \a A. The comparison
///          basis is greater than or equal for even-indexed elements in \a A,
///          and less than or equal for odd-indexed elements in \a A. \n
///      10: Match: Compare each pair of corresponding characters in \a A and
///          \a B for equality. \n
///      11: Substring: Search \a B for substring matches of \a A. \n
///    Bits [5:4]: Determine whether to perform a one's complement on the bit
///                mask of the comparison results. \n
///      00: No effect. \n
///      01: Negate the bit mask. \n
///      10: No effect. \n
///      11: Negate the bit mask only for bits with an index less than or equal
///          to the size of \a A or \a B. \n
///    Bit [6]: Determines whether the result is zero-extended or expanded to 16
///             bytes. \n
///      0: The result is zero-extended to 16 bytes. \n
///      1: The result is expanded to 16 bytes (this expansion is performed by
///         repeating each bit 8 or 16 times). \n
/// \returns Returns a 128-bit integer vector representing the result mask of
///    the comparison.
#define _mm_cmpestrm(A, LA, B, LB, M) \
  (__m128i)__builtin_ia32_pcmpestrm128((__v16qi)(__m128i)(A), (int)(LA), \
                                       (__v16qi)(__m128i)(B), (int)(LB), \
                                       (int)(M))

/// Uses the immediate operand \a M to perform a comparison of string
///    data with explicitly defined lengths that is contained in source operands
///    \a A and \a B. Returns an integer representing the result index of the
///    comparison.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_cmpestri(__m128i A, int LA, __m128i B, int LB, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCMPESTRI / PCMPESTRI </c>
/// instruction.
///
/// \param A
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param LA
///    An integer that specifies the length of the string in \a A.
/// \param B
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param LB
///    An integer that specifies the length of the string in \a B.
/// \param M
///    An 8-bit immediate operand specifying whether the characters are bytes or
///    words, the type of comparison to perform, and the format of the return
///    value. \n
///    Bits [1:0]: Determine source data format. \n
///      00: 16 unsigned bytes \n
///      01: 8 unsigned words \n
///      10: 16 signed bytes \n
///      11: 8 signed words \n
///    Bits [3:2]: Determine comparison type and aggregation method. \n
///      00: Subset: Each character in \a B is compared for equality with all
///          the characters in \a A. \n
///      01: Ranges: Each character in \a B is compared to \a A. The comparison
///          basis is greater than or equal for even-indexed elements in \a A,
///          and less than or equal for odd-indexed elements in \a A. \n
///      10: Match: Compare each pair of corresponding characters in \a A and
///          \a B for equality. \n
///      11: Substring: Search B for substring matches of \a A. \n
///    Bits [5:4]: Determine whether to perform a one's complement on the bit
///                mask of the comparison results. \n
///      00: No effect. \n
///      01: Negate the bit mask. \n
///      10: No effect. \n
///      11: Negate the bit mask only for bits with an index less than or equal
///          to the size of \a A or \a B. \n
///    Bit [6]: Determines whether the index of the lowest set bit or the
///             highest set bit is returned. \n
///      0: The index of the least significant set bit. \n
///      1: The index of the most significant set bit. \n
/// \returns Returns an integer representing the result index of the comparison.
#define _mm_cmpestri(A, LA, B, LB, M) \
  (int)__builtin_ia32_pcmpestri128((__v16qi)(__m128i)(A), (int)(LA), \
                                   (__v16qi)(__m128i)(B), (int)(LB), \
                                   (int)(M))

/* SSE4.2 Packed Comparison Intrinsics and EFlag Reading.  */
/// Uses the immediate operand \a M to perform a comparison of string
///    data with implicitly defined lengths that is contained in source operands
///    \a A and \a B. Returns 1 if the bit mask is zero and the length of the
///    string in \a B is the maximum, otherwise, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_cmpistra(__m128i A, __m128i B, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCMPISTRI / PCMPISTRI </c>
/// instruction.
///
/// \param A
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param B
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param M
///    An 8-bit immediate operand specifying whether the characters are bytes or
///    words and the type of comparison to perform. \n
///    Bits [1:0]: Determine source data format. \n
///      00: 16 unsigned bytes \n
///      01: 8 unsigned words \n
///      10: 16 signed bytes \n
///      11: 8 signed words \n
///    Bits [3:2]: Determine comparison type and aggregation method. \n
///      00: Subset: Each character in \a B is compared for equality with all
///          the characters in \a A. \n
///      01: Ranges: Each character in \a B is compared to \a A. The comparison
///          basis is greater than or equal for even-indexed elements in \a A,
///          and less than or equal for odd-indexed elements in \a A. \n
///      10: Match: Compare each pair of corresponding characters in \a A and
///          \a B for equality. \n
///      11: Substring: Search \a B for substring matches of \a A. \n
///    Bits [5:4]: Determine whether to perform a one's complement on the bit
///                mask of the comparison results. \n
///      00: No effect. \n
///      01: Negate the bit mask. \n
///      10: No effect. \n
///      11: Negate the bit mask only for bits with an index less than or equal
///          to the size of \a A or \a B. \n
/// \returns Returns 1 if the bit mask is zero and the length of the string in
///    \a B is the maximum; otherwise, returns 0.
#define _mm_cmpistra(A, B, M) \
  (int)__builtin_ia32_pcmpistria128((__v16qi)(__m128i)(A), \
                                    (__v16qi)(__m128i)(B), (int)(M))

/// Uses the immediate operand \a M to perform a comparison of string
///    data with implicitly defined lengths that is contained in source operands
///    \a A and \a B. Returns 1 if the bit mask is non-zero, otherwise, returns
///    0.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_cmpistrc(__m128i A, __m128i B, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCMPISTRI / PCMPISTRI </c>
/// instruction.
///
/// \param A
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param B
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param M
///    An 8-bit immediate operand specifying whether the characters are bytes or
///    words and the type of comparison to perform. \n
///    Bits [1:0]: Determine source data format. \n
///      00: 16 unsigned bytes \n
///      01: 8 unsigned words \n
///      10: 16 signed bytes \n
///      11: 8 signed words \n
///    Bits [3:2]: Determine comparison type and aggregation method. \n
///      00: Subset: Each character in \a B is compared for equality with all
///          the characters in \a A. \n
///      01: Ranges: Each character in \a B is compared to \a A. The comparison
///          basis is greater than or equal for even-indexed elements in \a A,
///          and less than or equal for odd-indexed elements in \a A. \n
///      10: Match: Compare each pair of corresponding characters in \a A and
///          \a B for equality. \n
///      11: Substring: Search B for substring matches of \a A. \n
///    Bits [5:4]: Determine whether to perform a one's complement on the bit
///                mask of the comparison results. \n
///      00: No effect. \n
///      01: Negate the bit mask. \n
///      10: No effect. \n
///      11: Negate the bit mask only for bits with an index less than or equal
///          to the size of \a A or \a B.
/// \returns Returns 1 if the bit mask is non-zero, otherwise, returns 0.
#define _mm_cmpistrc(A, B, M) \
  (int)__builtin_ia32_pcmpistric128((__v16qi)(__m128i)(A), \
                                    (__v16qi)(__m128i)(B), (int)(M))

/// Uses the immediate operand \a M to perform a comparison of string
///    data with implicitly defined lengths that is contained in source operands
///    \a A and \a B. Returns bit 0 of the resulting bit mask.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_cmpistro(__m128i A, __m128i B, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCMPISTRI / PCMPISTRI </c>
/// instruction.
///
/// \param A
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param B
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param M
///    An 8-bit immediate operand specifying whether the characters are bytes or
///    words and the type of comparison to perform. \n
///    Bits [1:0]: Determine source data format. \n
///      00: 16 unsigned bytes \n
///      01: 8 unsigned words \n
///      10: 16 signed bytes \n
///      11: 8 signed words \n
///    Bits [3:2]: Determine comparison type and aggregation method. \n
///      00: Subset: Each character in \a B is compared for equality with all
///          the characters in \a A. \n
///      01: Ranges: Each character in \a B is compared to \a A. The comparison
///          basis is greater than or equal for even-indexed elements in \a A,
///          and less than or equal for odd-indexed elements in \a A. \n
///      10: Match: Compare each pair of corresponding characters in \a A and
///          \a B for equality. \n
///      11: Substring: Search B for substring matches of \a A. \n
///    Bits [5:4]: Determine whether to perform a one's complement on the bit
///                mask of the comparison results. \n
///      00: No effect. \n
///      01: Negate the bit mask. \n
///      10: No effect. \n
///      11: Negate the bit mask only for bits with an index less than or equal
///          to the size of \a A or \a B. \n
/// \returns Returns bit 0 of the resulting bit mask.
#define _mm_cmpistro(A, B, M) \
  (int)__builtin_ia32_pcmpistrio128((__v16qi)(__m128i)(A), \
                                    (__v16qi)(__m128i)(B), (int)(M))

/// Uses the immediate operand \a M to perform a comparison of string
///    data with implicitly defined lengths that is contained in source operands
///    \a A and \a B. Returns 1 if the length of the string in \a A is less than
///    the maximum, otherwise, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_cmpistrs(__m128i A, __m128i B, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCMPISTRI / PCMPISTRI </c>
/// instruction.
///
/// \param A
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param B
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param M
///    An 8-bit immediate operand specifying whether the characters are bytes or
///    words and the type of comparison to perform. \n
///    Bits [1:0]: Determine source data format. \n
///      00: 16 unsigned bytes \n
///      01: 8 unsigned words \n
///      10: 16 signed bytes \n
///      11: 8 signed words \n
///    Bits [3:2]: Determine comparison type and aggregation method. \n
///      00: Subset: Each character in \a B is compared for equality with all
///          the characters in \a A. \n
///      01: Ranges: Each character in \a B is compared to \a A. The comparison
///          basis is greater than or equal for even-indexed elements in \a A,
///          and less than or equal for odd-indexed elements in \a A. \n
///      10: Match: Compare each pair of corresponding characters in \a A and
///          \a B for equality. \n
///      11: Substring: Search \a B for substring matches of \a A. \n
///    Bits [5:4]: Determine whether to perform a one's complement on the bit
///                mask of the comparison results. \n
///      00: No effect. \n
///      01: Negate the bit mask. \n
///      10: No effect. \n
///      11: Negate the bit mask only for bits with an index less than or equal
///          to the size of \a A or \a B. \n
/// \returns Returns 1 if the length of the string in \a A is less than the
///    maximum, otherwise, returns 0.
#define _mm_cmpistrs(A, B, M) \
  (int)__builtin_ia32_pcmpistris128((__v16qi)(__m128i)(A), \
                                    (__v16qi)(__m128i)(B), (int)(M))

/// Uses the immediate operand \a M to perform a comparison of string
///    data with implicitly defined lengths that is contained in source operands
///    \a A and \a B. Returns 1 if the length of the string in \a B is less than
///    the maximum, otherwise, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_cmpistrz(__m128i A, __m128i B, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCMPISTRI / PCMPISTRI </c>
/// instruction.
///
/// \param A
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param B
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param M
///    An 8-bit immediate operand specifying whether the characters are bytes or
///    words and the type of comparison to perform. \n
///    Bits [1:0]: Determine source data format. \n
///      00: 16 unsigned bytes \n
///      01: 8 unsigned words \n
///      10: 16 signed bytes \n
///      11: 8 signed words \n
///    Bits [3:2]: Determine comparison type and aggregation method. \n
///      00: Subset: Each character in \a B is compared for equality with all
///          the characters in \a A. \n
///      01: Ranges: Each character in \a B is compared to \a A. The comparison
///          basis is greater than or equal for even-indexed elements in \a A,
///          and less than or equal for odd-indexed elements in \a A. \n
///      10: Match: Compare each pair of corresponding characters in \a A and
///          \a B for equality. \n
///      11: Substring: Search \a B for substring matches of \a A. \n
///    Bits [5:4]: Determine whether to perform a one's complement on the bit
///                mask of the comparison results. \n
///      00: No effect. \n
///      01: Negate the bit mask. \n
///      10: No effect. \n
///      11: Negate the bit mask only for bits with an index less than or equal
///          to the size of \a A or \a B.
/// \returns Returns 1 if the length of the string in \a B is less than the
///    maximum, otherwise, returns 0.
#define _mm_cmpistrz(A, B, M) \
  (int)__builtin_ia32_pcmpistriz128((__v16qi)(__m128i)(A), \
                                    (__v16qi)(__m128i)(B), (int)(M))

/// Uses the immediate operand \a M to perform a comparison of string
///    data with explicitly defined lengths that is contained in source operands
///    \a A and \a B. Returns 1 if the bit mask is zero and the length of the
///    string in \a B is the maximum, otherwise, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_cmpestra(__m128i A, int LA, __m128i B, int LB, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCMPESTRI / PCMPESTRI </c>
/// instruction.
///
/// \param A
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param LA
///    An integer that specifies the length of the string in \a A.
/// \param B
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param LB
///    An integer that specifies the length of the string in \a B.
/// \param M
///    An 8-bit immediate operand specifying whether the characters are bytes or
///    words and the type of comparison to perform. \n
///    Bits [1:0]: Determine source data format. \n
///      00: 16 unsigned bytes \n
///      01: 8 unsigned words \n
///      10: 16 signed bytes \n
///      11: 8 signed words \n
///    Bits [3:2]: Determine comparison type and aggregation method. \n
///      00: Subset: Each character in \a B is compared for equality with all
///          the characters in \a A. \n
///      01: Ranges: Each character in \a B is compared to \a A. The comparison
///          basis is greater than or equal for even-indexed elements in \a A,
///          and less than or equal for odd-indexed elements in \a A. \n
///      10: Match: Compare each pair of corresponding characters in \a A and
///          \a B for equality. \n
///      11: Substring: Search \a B for substring matches of \a A. \n
///    Bits [5:4]: Determine whether to perform a one's complement on the bit
///                mask of the comparison results. \n
///      00: No effect. \n
///      01: Negate the bit mask. \n
///      10: No effect. \n
///      11: Negate the bit mask only for bits with an index less than or equal
///          to the size of \a A or \a B.
/// \returns Returns 1 if the bit mask is zero and the length of the string in
///    \a B is the maximum, otherwise, returns 0.
#define _mm_cmpestra(A, LA, B, LB, M) \
  (int)__builtin_ia32_pcmpestria128((__v16qi)(__m128i)(A), (int)(LA), \
                                    (__v16qi)(__m128i)(B), (int)(LB), \
                                    (int)(M))

/// Uses the immediate operand \a M to perform a comparison of string
///    data with explicitly defined lengths that is contained in source operands
///    \a A and \a B. Returns 1 if the resulting mask is non-zero, otherwise,
///    returns 0.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_cmpestrc(__m128i A, int LA, __m128i B, int LB, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCMPESTRI / PCMPESTRI </c>
/// instruction.
///
/// \param A
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param LA
///    An integer that specifies the length of the string in \a A.
/// \param B
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param LB
///    An integer that specifies the length of the string in \a B.
/// \param M
///    An 8-bit immediate operand specifying whether the characters are bytes or
///    words and the type of comparison to perform. \n
///    Bits [1:0]: Determine source data format. \n
///      00: 16 unsigned bytes \n
///      01: 8 unsigned words \n
///      10: 16 signed bytes \n
///      11: 8 signed words \n
///    Bits [3:2]: Determine comparison type and aggregation method. \n
///      00: Subset: Each character in \a B is compared for equality with all
///          the characters in \a A. \n
///      01: Ranges: Each character in \a B is compared to \a A. The comparison
///          basis is greater than or equal for even-indexed elements in \a A,
///          and less than or equal for odd-indexed elements in \a A. \n
///      10: Match: Compare each pair of corresponding characters in \a A and
///          \a B for equality. \n
///      11: Substring: Search \a B for substring matches of \a A. \n
///    Bits [5:4]: Determine whether to perform a one's complement on the bit
///                mask of the comparison results. \n
///      00: No effect. \n
///      01: Negate the bit mask. \n
///      10: No effect. \n
///      11: Negate the bit mask only for bits with an index less than or equal
///          to the size of \a A or \a B. \n
/// \returns Returns 1 if the resulting mask is non-zero, otherwise, returns 0.
#define _mm_cmpestrc(A, LA, B, LB, M) \
  (int)__builtin_ia32_pcmpestric128((__v16qi)(__m128i)(A), (int)(LA), \
                                    (__v16qi)(__m128i)(B), (int)(LB), \
                                    (int)(M))

/// Uses the immediate operand \a M to perform a comparison of string
///    data with explicitly defined lengths that is contained in source operands
///    \a A and \a B. Returns bit 0 of the resulting bit mask.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_cmpestro(__m128i A, int LA, __m128i B, int LB, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCMPESTRI / PCMPESTRI </c>
/// instruction.
///
/// \param A
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param LA
///    An integer that specifies the length of the string in \a A.
/// \param B
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param LB
///    An integer that specifies the length of the string in \a B.
/// \param M
///    An 8-bit immediate operand specifying whether the characters are bytes or
///    words and the type of comparison to perform. \n
///    Bits [1:0]: Determine source data format. \n
///      00: 16 unsigned bytes \n
///      01: 8 unsigned words \n
///      10: 16 signed bytes \n
///      11: 8 signed words \n
///    Bits [3:2]: Determine comparison type and aggregation method. \n
///      00: Subset: Each character in \a B is compared for equality with all
///          the characters in \a A. \n
///      01: Ranges: Each character in \a B is compared to \a A. The comparison
///          basis is greater than or equal for even-indexed elements in \a A,
///          and less than or equal for odd-indexed elements in \a A. \n
///      10: Match: Compare each pair of corresponding characters in \a A and
///          \a B for equality. \n
///      11: Substring: Search \a B for substring matches of \a A. \n
///    Bits [5:4]: Determine whether to perform a one's complement on the bit
///                mask of the comparison results. \n
///      00: No effect. \n
///      01: Negate the bit mask. \n
///      10: No effect. \n
///      11: Negate the bit mask only for bits with an index less than or equal
///          to the size of \a A or \a B.
/// \returns Returns bit 0 of the resulting bit mask.
#define _mm_cmpestro(A, LA, B, LB, M) \
  (int)__builtin_ia32_pcmpestrio128((__v16qi)(__m128i)(A), (int)(LA), \
                                    (__v16qi)(__m128i)(B), (int)(LB), \
                                    (int)(M))

/// Uses the immediate operand \a M to perform a comparison of string
///    data with explicitly defined lengths that is contained in source operands
///    \a A and \a B. Returns 1 if the length of the string in \a A is less than
///    the maximum, otherwise, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_cmpestrs(__m128i A, int LA, __m128i B, int LB, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCMPESTRI / PCMPESTRI </c>
/// instruction.
///
/// \param A
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param LA
///    An integer that specifies the length of the string in \a A.
/// \param B
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param LB
///    An integer that specifies the length of the string in \a B.
/// \param M
///    An 8-bit immediate operand specifying whether the characters are bytes or
///    words and the type of comparison to perform. \n
///    Bits [1:0]: Determine source data format. \n
///      00: 16 unsigned bytes \n
///      01: 8 unsigned words \n
///      10: 16 signed bytes \n
///      11: 8 signed words \n
///    Bits [3:2]: Determine comparison type and aggregation method. \n
///      00: Subset: Each character in \a B is compared for equality with all
///          the characters in \a A. \n
///      01: Ranges: Each character in \a B is compared to \a A. The comparison
///          basis is greater than or equal for even-indexed elements in \a A,
///          and less than or equal for odd-indexed elements in \a A. \n
///      10: Match: Compare each pair of corresponding characters in \a A and
///          \a B for equality. \n
///      11: Substring: Search \a B for substring matches of \a A. \n
///    Bits [5:4]: Determine whether to perform a one's complement in the bit
///                mask of the comparison results. \n
///      00: No effect. \n
///      01: Negate the bit mask. \n
///      10: No effect. \n
///      11: Negate the bit mask only for bits with an index less than or equal
///          to the size of \a A or \a B. \n
/// \returns Returns 1 if the length of the string in \a A is less than the
///    maximum, otherwise, returns 0.
#define _mm_cmpestrs(A, LA, B, LB, M) \
  (int)__builtin_ia32_pcmpestris128((__v16qi)(__m128i)(A), (int)(LA), \
                                    (__v16qi)(__m128i)(B), (int)(LB), \
                                    (int)(M))

/// Uses the immediate operand \a M to perform a comparison of string
///    data with explicitly defined lengths that is contained in source operands
///    \a A and \a B. Returns 1 if the length of the string in \a B is less than
///    the maximum, otherwise, returns 0.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _mm_cmpestrz(__m128i A, int LA, __m128i B, int LB, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCMPESTRI </c> instruction.
///
/// \param A
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param LA
///    An integer that specifies the length of the string in \a A.
/// \param B
///    A 128-bit integer vector containing one of the source operands to be
///    compared.
/// \param LB
///    An integer that specifies the length of the string in \a B.
/// \param M
///    An 8-bit immediate operand specifying whether the characters are bytes or
///    words and the type of comparison to perform. \n
///    Bits [1:0]: Determine source data format. \n
///      00: 16 unsigned bytes  \n
///      01: 8 unsigned words \n
///      10: 16 signed bytes \n
///      11: 8 signed words \n
///    Bits [3:2]: Determine comparison type and aggregation method. \n
///      00: Subset: Each character in \a B is compared for equality with all
///          the characters in \a A. \n
///      01: Ranges: Each character in \a B is compared to \a A. The comparison
///          basis is greater than or equal for even-indexed elements in \a A,
///          and less than or equal for odd-indexed elements in \a A. \n
///      10: Match: Compare each pair of corresponding characters in \a A and
///          \a B for equality. \n
///      11: Substring: Search \a B for substring matches of \a A. \n
///    Bits [5:4]: Determine whether to perform a one's complement on the bit
///                mask of the comparison results. \n
///      00: No effect. \n
///      01: Negate the bit mask. \n
///      10: No effect. \n
///      11: Negate the bit mask only for bits with an index less than or equal
///          to the size of \a A or \a B.
/// \returns Returns 1 if the length of the string in \a B is less than the
///    maximum, otherwise, returns 0.
#define _mm_cmpestrz(A, LA, B, LB, M) \
  (int)__builtin_ia32_pcmpestriz128((__v16qi)(__m128i)(A), (int)(LA), \
                                    (__v16qi)(__m128i)(B), (int)(LB), \
                                    (int)(M))

/* SSE4.2 Compare Packed Data -- Greater Than.  */
/// Compares each of the corresponding 64-bit values of the 128-bit
///    integer vectors to determine if the values in the first operand are
///    greater than those in the second operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPCMPGTQ / PCMPGTQ </c> instruction.
///
/// \param __V1
///    A 128-bit integer vector.
/// \param __V2
///    A 128-bit integer vector.
/// \returns A 128-bit integer vector containing the comparison results.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_cmpgt_epi64(__m128i __V1, __m128i __V2)
{
  return (__m128i)((__v2di)__V1 > (__v2di)__V2);
}

/* SSE4.2 Accumulate CRC32.  */
/// Adds the unsigned integer operand to the CRC-32C checksum of the
///    unsigned char operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CRC32B </c> instruction.
///
/// \param __C
///    An unsigned integer operand to add to the CRC-32C checksum of operand
///    \a  __D.
/// \param __D
///    An unsigned 8-bit integer operand used to compute the CRC-32C checksum.
/// \returns The result of adding operand \a __C to the CRC-32C checksum of
///    operand \a __D.
static __inline__ unsigned int __DEFAULT_FN_ATTRS
_mm_crc32_u8(unsigned int __C, unsigned char __D)
{
  return __builtin_ia32_crc32qi(__C, __D);
}

/// Adds the unsigned integer operand to the CRC-32C checksum of the
///    unsigned short operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CRC32W </c> instruction.
///
/// \param __C
///    An unsigned integer operand to add to the CRC-32C checksum of operand
///    \a __D.
/// \param __D
///    An unsigned 16-bit integer operand used to compute the CRC-32C checksum.
/// \returns The result of adding operand \a __C to the CRC-32C checksum of
///    operand \a __D.
static __inline__ unsigned int __DEFAULT_FN_ATTRS
_mm_crc32_u16(unsigned int __C, unsigned short __D)
{
  return __builtin_ia32_crc32hi(__C, __D);
}

/// Adds the first unsigned integer operand to the CRC-32C checksum of
///    the second unsigned integer operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CRC32L </c> instruction.
///
/// \param __C
///    An unsigned integer operand to add to the CRC-32C checksum of operand
///    \a __D.
/// \param __D
///    An unsigned 32-bit integer operand used to compute the CRC-32C checksum.
/// \returns The result of adding operand \a __C to the CRC-32C checksum of
///    operand \a __D.
static __inline__ unsigned int __DEFAULT_FN_ATTRS
_mm_crc32_u32(unsigned int __C, unsigned int __D)
{
  return __builtin_ia32_crc32si(__C, __D);
}

#ifdef __x86_64__
/// Adds the unsigned integer operand to the CRC-32C checksum of the
///    unsigned 64-bit integer operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CRC32Q </c> instruction.
///
/// \param __C
///    An unsigned integer operand to add to the CRC-32C checksum of operand
///    \a __D.
/// \param __D
///    An unsigned 64-bit integer operand used to compute the CRC-32C checksum.
/// \returns The result of adding operand \a __C to the CRC-32C checksum of
///    operand \a __D.
static __inline__ unsigned long long __DEFAULT_FN_ATTRS
_mm_crc32_u64(unsigned long long __C, unsigned long long __D)
{
  return __builtin_ia32_crc32di(__C, __D);
}
#endif /* __x86_64__ */

#undef __DEFAULT_FN_ATTRS

#include <popcntintrin.h>

#endif /* __SMMINTRIN_H */
