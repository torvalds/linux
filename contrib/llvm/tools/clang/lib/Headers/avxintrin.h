/*===---- avxintrin.h - AVX intrinsics -------------------------------------===
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
#error "Never use <avxintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVXINTRIN_H
#define __AVXINTRIN_H

typedef double __v4df __attribute__ ((__vector_size__ (32)));
typedef float __v8sf __attribute__ ((__vector_size__ (32)));
typedef long long __v4di __attribute__ ((__vector_size__ (32)));
typedef int __v8si __attribute__ ((__vector_size__ (32)));
typedef short __v16hi __attribute__ ((__vector_size__ (32)));
typedef char __v32qi __attribute__ ((__vector_size__ (32)));

/* Unsigned types */
typedef unsigned long long __v4du __attribute__ ((__vector_size__ (32)));
typedef unsigned int __v8su __attribute__ ((__vector_size__ (32)));
typedef unsigned short __v16hu __attribute__ ((__vector_size__ (32)));
typedef unsigned char __v32qu __attribute__ ((__vector_size__ (32)));

/* We need an explicitly signed variant for char. Note that this shouldn't
 * appear in the interface though. */
typedef signed char __v32qs __attribute__((__vector_size__(32)));

typedef float __m256 __attribute__ ((__vector_size__ (32)));
typedef double __m256d __attribute__((__vector_size__(32)));
typedef long long __m256i __attribute__((__vector_size__(32)));

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("avx"), __min_vector_width__(256)))
#define __DEFAULT_FN_ATTRS128 __attribute__((__always_inline__, __nodebug__, __target__("avx"), __min_vector_width__(128)))

/* Arithmetic */
/// Adds two 256-bit vectors of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VADDPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double] containing one of the source operands.
/// \param __b
///    A 256-bit vector of [4 x double] containing one of the source operands.
/// \returns A 256-bit vector of [4 x double] containing the sums of both
///    operands.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_add_pd(__m256d __a, __m256d __b)
{
  return (__m256d)((__v4df)__a+(__v4df)__b);
}

/// Adds two 256-bit vectors of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VADDPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float] containing one of the source operands.
/// \param __b
///    A 256-bit vector of [8 x float] containing one of the source operands.
/// \returns A 256-bit vector of [8 x float] containing the sums of both
///    operands.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_add_ps(__m256 __a, __m256 __b)
{
  return (__m256)((__v8sf)__a+(__v8sf)__b);
}

/// Subtracts two 256-bit vectors of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VSUBPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double] containing the minuend.
/// \param __b
///    A 256-bit vector of [4 x double] containing the subtrahend.
/// \returns A 256-bit vector of [4 x double] containing the differences between
///    both operands.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_sub_pd(__m256d __a, __m256d __b)
{
  return (__m256d)((__v4df)__a-(__v4df)__b);
}

/// Subtracts two 256-bit vectors of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VSUBPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float] containing the minuend.
/// \param __b
///    A 256-bit vector of [8 x float] containing the subtrahend.
/// \returns A 256-bit vector of [8 x float] containing the differences between
///    both operands.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_sub_ps(__m256 __a, __m256 __b)
{
  return (__m256)((__v8sf)__a-(__v8sf)__b);
}

/// Adds the even-indexed values and subtracts the odd-indexed values of
///    two 256-bit vectors of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VADDSUBPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double] containing the left source operand.
/// \param __b
///    A 256-bit vector of [4 x double] containing the right source operand.
/// \returns A 256-bit vector of [4 x double] containing the alternating sums
///    and differences between both operands.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_addsub_pd(__m256d __a, __m256d __b)
{
  return (__m256d)__builtin_ia32_addsubpd256((__v4df)__a, (__v4df)__b);
}

/// Adds the even-indexed values and subtracts the odd-indexed values of
///    two 256-bit vectors of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VADDSUBPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float] containing the left source operand.
/// \param __b
///    A 256-bit vector of [8 x float] containing the right source operand.
/// \returns A 256-bit vector of [8 x float] containing the alternating sums and
///    differences between both operands.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_addsub_ps(__m256 __a, __m256 __b)
{
  return (__m256)__builtin_ia32_addsubps256((__v8sf)__a, (__v8sf)__b);
}

/// Divides two 256-bit vectors of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VDIVPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double] containing the dividend.
/// \param __b
///    A 256-bit vector of [4 x double] containing the divisor.
/// \returns A 256-bit vector of [4 x double] containing the quotients of both
///    operands.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_div_pd(__m256d __a, __m256d __b)
{
  return (__m256d)((__v4df)__a/(__v4df)__b);
}

/// Divides two 256-bit vectors of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VDIVPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float] containing the dividend.
/// \param __b
///    A 256-bit vector of [8 x float] containing the divisor.
/// \returns A 256-bit vector of [8 x float] containing the quotients of both
///    operands.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_div_ps(__m256 __a, __m256 __b)
{
  return (__m256)((__v8sf)__a/(__v8sf)__b);
}

/// Compares two 256-bit vectors of [4 x double] and returns the greater
///    of each pair of values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMAXPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double] containing one of the operands.
/// \param __b
///    A 256-bit vector of [4 x double] containing one of the operands.
/// \returns A 256-bit vector of [4 x double] containing the maximum values
///    between both operands.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_max_pd(__m256d __a, __m256d __b)
{
  return (__m256d)__builtin_ia32_maxpd256((__v4df)__a, (__v4df)__b);
}

/// Compares two 256-bit vectors of [8 x float] and returns the greater
///    of each pair of values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMAXPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float] containing one of the operands.
/// \param __b
///    A 256-bit vector of [8 x float] containing one of the operands.
/// \returns A 256-bit vector of [8 x float] containing the maximum values
///    between both operands.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_max_ps(__m256 __a, __m256 __b)
{
  return (__m256)__builtin_ia32_maxps256((__v8sf)__a, (__v8sf)__b);
}

/// Compares two 256-bit vectors of [4 x double] and returns the lesser
///    of each pair of values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMINPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double] containing one of the operands.
/// \param __b
///    A 256-bit vector of [4 x double] containing one of the operands.
/// \returns A 256-bit vector of [4 x double] containing the minimum values
///    between both operands.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_min_pd(__m256d __a, __m256d __b)
{
  return (__m256d)__builtin_ia32_minpd256((__v4df)__a, (__v4df)__b);
}

/// Compares two 256-bit vectors of [8 x float] and returns the lesser
///    of each pair of values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMINPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float] containing one of the operands.
/// \param __b
///    A 256-bit vector of [8 x float] containing one of the operands.
/// \returns A 256-bit vector of [8 x float] containing the minimum values
///    between both operands.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_min_ps(__m256 __a, __m256 __b)
{
  return (__m256)__builtin_ia32_minps256((__v8sf)__a, (__v8sf)__b);
}

/// Multiplies two 256-bit vectors of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMULPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double] containing one of the operands.
/// \param __b
///    A 256-bit vector of [4 x double] containing one of the operands.
/// \returns A 256-bit vector of [4 x double] containing the products of both
///    operands.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_mul_pd(__m256d __a, __m256d __b)
{
  return (__m256d)((__v4df)__a * (__v4df)__b);
}

/// Multiplies two 256-bit vectors of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMULPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float] containing one of the operands.
/// \param __b
///    A 256-bit vector of [8 x float] containing one of the operands.
/// \returns A 256-bit vector of [8 x float] containing the products of both
///    operands.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_mul_ps(__m256 __a, __m256 __b)
{
  return (__m256)((__v8sf)__a * (__v8sf)__b);
}

/// Calculates the square roots of the values in a 256-bit vector of
///    [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VSQRTPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double].
/// \returns A 256-bit vector of [4 x double] containing the square roots of the
///    values in the operand.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_sqrt_pd(__m256d __a)
{
  return (__m256d)__builtin_ia32_sqrtpd256((__v4df)__a);
}

/// Calculates the square roots of the values in a 256-bit vector of
///    [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VSQRTPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float].
/// \returns A 256-bit vector of [8 x float] containing the square roots of the
///    values in the operand.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_sqrt_ps(__m256 __a)
{
  return (__m256)__builtin_ia32_sqrtps256((__v8sf)__a);
}

/// Calculates the reciprocal square roots of the values in a 256-bit
///    vector of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VRSQRTPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float].
/// \returns A 256-bit vector of [8 x float] containing the reciprocal square
///    roots of the values in the operand.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_rsqrt_ps(__m256 __a)
{
  return (__m256)__builtin_ia32_rsqrtps256((__v8sf)__a);
}

/// Calculates the reciprocals of the values in a 256-bit vector of
///    [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VRCPPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float].
/// \returns A 256-bit vector of [8 x float] containing the reciprocals of the
///    values in the operand.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_rcp_ps(__m256 __a)
{
  return (__m256)__builtin_ia32_rcpps256((__v8sf)__a);
}

/// Rounds the values in a 256-bit vector of [4 x double] as specified
///    by the byte operand. The source values are rounded to integer values and
///    returned as 64-bit double-precision floating-point values.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256d _mm256_round_pd(__m256d V, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDPD </c> instruction.
///
/// \param V
///    A 256-bit vector of [4 x double].
/// \param M
///    An integer value that specifies the rounding operation. \n
///    Bits [7:4] are reserved. \n
///    Bit [3] is a precision exception value: \n
///      0: A normal PE exception is used. \n
///      1: The PE field is not updated. \n
///    Bit [2] is the rounding control source: \n
///      0: Use bits [1:0] of \a M. \n
///      1: Use the current MXCSR setting. \n
///    Bits [1:0] contain the rounding control definition: \n
///      00: Nearest. \n
///      01: Downward (toward negative infinity). \n
///      10: Upward (toward positive infinity). \n
///      11: Truncated.
/// \returns A 256-bit vector of [4 x double] containing the rounded values.
#define _mm256_round_pd(V, M) \
    (__m256d)__builtin_ia32_roundpd256((__v4df)(__m256d)(V), (M))

/// Rounds the values stored in a 256-bit vector of [8 x float] as
///    specified by the byte operand. The source values are rounded to integer
///    values and returned as floating-point values.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256 _mm256_round_ps(__m256 V, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDPS </c> instruction.
///
/// \param V
///    A 256-bit vector of [8 x float].
/// \param M
///    An integer value that specifies the rounding operation. \n
///    Bits [7:4] are reserved. \n
///    Bit [3] is a precision exception value: \n
///      0: A normal PE exception is used. \n
///      1: The PE field is not updated. \n
///    Bit [2] is the rounding control source: \n
///      0: Use bits [1:0] of \a M. \n
///      1: Use the current MXCSR setting. \n
///    Bits [1:0] contain the rounding control definition: \n
///      00: Nearest. \n
///      01: Downward (toward negative infinity). \n
///      10: Upward (toward positive infinity). \n
///      11: Truncated.
/// \returns A 256-bit vector of [8 x float] containing the rounded values.
#define _mm256_round_ps(V, M) \
  (__m256)__builtin_ia32_roundps256((__v8sf)(__m256)(V), (M))

/// Rounds up the values stored in a 256-bit vector of [4 x double]. The
///    source values are rounded up to integer values and returned as 64-bit
///    double-precision floating-point values.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256d _mm256_ceil_pd(__m256d V);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDPD </c> instruction.
///
/// \param V
///    A 256-bit vector of [4 x double].
/// \returns A 256-bit vector of [4 x double] containing the rounded up values.
#define _mm256_ceil_pd(V)  _mm256_round_pd((V), _MM_FROUND_CEIL)

/// Rounds down the values stored in a 256-bit vector of [4 x double].
///    The source values are rounded down to integer values and returned as
///    64-bit double-precision floating-point values.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256d _mm256_floor_pd(__m256d V);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDPD </c> instruction.
///
/// \param V
///    A 256-bit vector of [4 x double].
/// \returns A 256-bit vector of [4 x double] containing the rounded down
///    values.
#define _mm256_floor_pd(V) _mm256_round_pd((V), _MM_FROUND_FLOOR)

/// Rounds up the values stored in a 256-bit vector of [8 x float]. The
///    source values are rounded up to integer values and returned as
///    floating-point values.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256 _mm256_ceil_ps(__m256 V);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDPS </c> instruction.
///
/// \param V
///    A 256-bit vector of [8 x float].
/// \returns A 256-bit vector of [8 x float] containing the rounded up values.
#define _mm256_ceil_ps(V)  _mm256_round_ps((V), _MM_FROUND_CEIL)

/// Rounds down the values stored in a 256-bit vector of [8 x float]. The
///    source values are rounded down to integer values and returned as
///    floating-point values.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256 _mm256_floor_ps(__m256 V);
/// \endcode
///
/// This intrinsic corresponds to the <c> VROUNDPS </c> instruction.
///
/// \param V
///    A 256-bit vector of [8 x float].
/// \returns A 256-bit vector of [8 x float] containing the rounded down values.
#define _mm256_floor_ps(V) _mm256_round_ps((V), _MM_FROUND_FLOOR)

/* Logical */
/// Performs a bitwise AND of two 256-bit vectors of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VANDPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double] containing one of the source operands.
/// \param __b
///    A 256-bit vector of [4 x double] containing one of the source operands.
/// \returns A 256-bit vector of [4 x double] containing the bitwise AND of the
///    values between both operands.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_and_pd(__m256d __a, __m256d __b)
{
  return (__m256d)((__v4du)__a & (__v4du)__b);
}

/// Performs a bitwise AND of two 256-bit vectors of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VANDPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float] containing one of the source operands.
/// \param __b
///    A 256-bit vector of [8 x float] containing one of the source operands.
/// \returns A 256-bit vector of [8 x float] containing the bitwise AND of the
///    values between both operands.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_and_ps(__m256 __a, __m256 __b)
{
  return (__m256)((__v8su)__a & (__v8su)__b);
}

/// Performs a bitwise AND of two 256-bit vectors of [4 x double], using
///    the one's complement of the values contained in the first source operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VANDNPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double] containing the left source operand. The
///    one's complement of this value is used in the bitwise AND.
/// \param __b
///    A 256-bit vector of [4 x double] containing the right source operand.
/// \returns A 256-bit vector of [4 x double] containing the bitwise AND of the
///    values of the second operand and the one's complement of the first
///    operand.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_andnot_pd(__m256d __a, __m256d __b)
{
  return (__m256d)(~(__v4du)__a & (__v4du)__b);
}

/// Performs a bitwise AND of two 256-bit vectors of [8 x float], using
///    the one's complement of the values contained in the first source operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VANDNPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float] containing the left source operand. The
///    one's complement of this value is used in the bitwise AND.
/// \param __b
///    A 256-bit vector of [8 x float] containing the right source operand.
/// \returns A 256-bit vector of [8 x float] containing the bitwise AND of the
///    values of the second operand and the one's complement of the first
///    operand.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_andnot_ps(__m256 __a, __m256 __b)
{
  return (__m256)(~(__v8su)__a & (__v8su)__b);
}

/// Performs a bitwise OR of two 256-bit vectors of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VORPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double] containing one of the source operands.
/// \param __b
///    A 256-bit vector of [4 x double] containing one of the source operands.
/// \returns A 256-bit vector of [4 x double] containing the bitwise OR of the
///    values between both operands.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_or_pd(__m256d __a, __m256d __b)
{
  return (__m256d)((__v4du)__a | (__v4du)__b);
}

/// Performs a bitwise OR of two 256-bit vectors of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VORPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float] containing one of the source operands.
/// \param __b
///    A 256-bit vector of [8 x float] containing one of the source operands.
/// \returns A 256-bit vector of [8 x float] containing the bitwise OR of the
///    values between both operands.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_or_ps(__m256 __a, __m256 __b)
{
  return (__m256)((__v8su)__a | (__v8su)__b);
}

/// Performs a bitwise XOR of two 256-bit vectors of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VXORPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double] containing one of the source operands.
/// \param __b
///    A 256-bit vector of [4 x double] containing one of the source operands.
/// \returns A 256-bit vector of [4 x double] containing the bitwise XOR of the
///    values between both operands.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_xor_pd(__m256d __a, __m256d __b)
{
  return (__m256d)((__v4du)__a ^ (__v4du)__b);
}

/// Performs a bitwise XOR of two 256-bit vectors of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VXORPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float] containing one of the source operands.
/// \param __b
///    A 256-bit vector of [8 x float] containing one of the source operands.
/// \returns A 256-bit vector of [8 x float] containing the bitwise XOR of the
///    values between both operands.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_xor_ps(__m256 __a, __m256 __b)
{
  return (__m256)((__v8su)__a ^ (__v8su)__b);
}

/* Horizontal arithmetic */
/// Horizontally adds the adjacent pairs of values contained in two
///    256-bit vectors of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VHADDPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double] containing one of the source operands.
///    The horizontal sums of the values are returned in the even-indexed
///    elements of a vector of [4 x double].
/// \param __b
///    A 256-bit vector of [4 x double] containing one of the source operands.
///    The horizontal sums of the values are returned in the odd-indexed
///    elements of a vector of [4 x double].
/// \returns A 256-bit vector of [4 x double] containing the horizontal sums of
///    both operands.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_hadd_pd(__m256d __a, __m256d __b)
{
  return (__m256d)__builtin_ia32_haddpd256((__v4df)__a, (__v4df)__b);
}

/// Horizontally adds the adjacent pairs of values contained in two
///    256-bit vectors of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VHADDPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float] containing one of the source operands.
///    The horizontal sums of the values are returned in the elements with
///    index 0, 1, 4, 5 of a vector of [8 x float].
/// \param __b
///    A 256-bit vector of [8 x float] containing one of the source operands.
///    The horizontal sums of the values are returned in the elements with
///    index 2, 3, 6, 7 of a vector of [8 x float].
/// \returns A 256-bit vector of [8 x float] containing the horizontal sums of
///    both operands.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_hadd_ps(__m256 __a, __m256 __b)
{
  return (__m256)__builtin_ia32_haddps256((__v8sf)__a, (__v8sf)__b);
}

/// Horizontally subtracts the adjacent pairs of values contained in two
///    256-bit vectors of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VHSUBPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double] containing one of the source operands.
///    The horizontal differences between the values are returned in the
///    even-indexed elements of a vector of [4 x double].
/// \param __b
///    A 256-bit vector of [4 x double] containing one of the source operands.
///    The horizontal differences between the values are returned in the
///    odd-indexed elements of a vector of [4 x double].
/// \returns A 256-bit vector of [4 x double] containing the horizontal
///    differences of both operands.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_hsub_pd(__m256d __a, __m256d __b)
{
  return (__m256d)__builtin_ia32_hsubpd256((__v4df)__a, (__v4df)__b);
}

/// Horizontally subtracts the adjacent pairs of values contained in two
///    256-bit vectors of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VHSUBPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float] containing one of the source operands.
///    The horizontal differences between the values are returned in the
///    elements with index 0, 1, 4, 5 of a vector of [8 x float].
/// \param __b
///    A 256-bit vector of [8 x float] containing one of the source operands.
///    The horizontal differences between the values are returned in the
///    elements with index 2, 3, 6, 7 of a vector of [8 x float].
/// \returns A 256-bit vector of [8 x float] containing the horizontal
///    differences of both operands.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_hsub_ps(__m256 __a, __m256 __b)
{
  return (__m256)__builtin_ia32_hsubps256((__v8sf)__a, (__v8sf)__b);
}

/* Vector permutations */
/// Copies the values in a 128-bit vector of [2 x double] as specified
///    by the 128-bit integer vector operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPERMILPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __c
///    A 128-bit integer vector operand specifying how the values are to be
///    copied. \n
///    Bit [1]: \n
///      0: Bits [63:0] of the source are copied to bits [63:0] of the returned
///         vector. \n
///      1: Bits [127:64] of the source are copied to bits [63:0] of the
///         returned vector. \n
///    Bit [65]: \n
///      0: Bits [63:0] of the source are copied to bits [127:64] of the
///         returned vector. \n
///      1: Bits [127:64] of the source are copied to bits [127:64] of the
///         returned vector.
/// \returns A 128-bit vector of [2 x double] containing the copied values.
static __inline __m128d __DEFAULT_FN_ATTRS128
_mm_permutevar_pd(__m128d __a, __m128i __c)
{
  return (__m128d)__builtin_ia32_vpermilvarpd((__v2df)__a, (__v2di)__c);
}

/// Copies the values in a 256-bit vector of [4 x double] as specified
///    by the 256-bit integer vector operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPERMILPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double].
/// \param __c
///    A 256-bit integer vector operand specifying how the values are to be
///    copied. \n
///    Bit [1]: \n
///      0: Bits [63:0] of the source are copied to bits [63:0] of the returned
///         vector. \n
///      1: Bits [127:64] of the source are copied to bits [63:0] of the
///         returned vector. \n
///    Bit [65]: \n
///      0: Bits [63:0] of the source are copied to bits [127:64] of the
///         returned vector. \n
///      1: Bits [127:64] of the source are copied to bits [127:64] of the
///         returned vector. \n
///    Bit [129]: \n
///      0: Bits [191:128] of the source are copied to bits [191:128] of the
///         returned vector. \n
///      1: Bits [255:192] of the source are copied to bits [191:128] of the
///         returned vector. \n
///    Bit [193]: \n
///      0: Bits [191:128] of the source are copied to bits [255:192] of the
///         returned vector. \n
///      1: Bits [255:192] of the source are copied to bits [255:192] of the
///    returned vector.
/// \returns A 256-bit vector of [4 x double] containing the copied values.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_permutevar_pd(__m256d __a, __m256i __c)
{
  return (__m256d)__builtin_ia32_vpermilvarpd256((__v4df)__a, (__v4di)__c);
}

/// Copies the values stored in a 128-bit vector of [4 x float] as
///    specified by the 128-bit integer vector operand.
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPERMILPS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __c
///    A 128-bit integer vector operand specifying how the values are to be
///    copied. \n
///    Bits [1:0]: \n
///      00: Bits [31:0] of the source are copied to bits [31:0] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [31:0] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [31:0] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [31:0] of the
///          returned vector. \n
///    Bits [33:32]: \n
///      00: Bits [31:0] of the source are copied to bits [63:32] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [63:32] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [63:32] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [63:32] of the
///          returned vector. \n
///    Bits [65:64]: \n
///      00: Bits [31:0] of the source are copied to bits [95:64] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [95:64] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [95:64] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [95:64] of the
///          returned vector. \n
///    Bits [97:96]: \n
///      00: Bits [31:0] of the source are copied to bits [127:96] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [127:96] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [127:96] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [127:96] of the
///          returned vector.
/// \returns A 128-bit vector of [4 x float] containing the copied values.
static __inline __m128 __DEFAULT_FN_ATTRS128
_mm_permutevar_ps(__m128 __a, __m128i __c)
{
  return (__m128)__builtin_ia32_vpermilvarps((__v4sf)__a, (__v4si)__c);
}

/// Copies the values stored in a 256-bit vector of [8 x float] as
///    specified by the 256-bit integer vector operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPERMILPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float].
/// \param __c
///    A 256-bit integer vector operand specifying how the values are to be
///    copied. \n
///    Bits [1:0]: \n
///      00: Bits [31:0] of the source are copied to bits [31:0] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [31:0] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [31:0] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [31:0] of the
///          returned vector. \n
///    Bits [33:32]: \n
///      00: Bits [31:0] of the source are copied to bits [63:32] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [63:32] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [63:32] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [63:32] of the
///          returned vector. \n
///    Bits [65:64]: \n
///      00: Bits [31:0] of the source are copied to bits [95:64] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [95:64] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [95:64] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [95:64] of the
///          returned vector. \n
///    Bits [97:96]: \n
///      00: Bits [31:0] of the source are copied to bits [127:96] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [127:96] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [127:96] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [127:96] of the
///          returned vector. \n
///    Bits [129:128]: \n
///      00: Bits [159:128] of the source are copied to bits [159:128] of the
///          returned vector. \n
///      01: Bits [191:160] of the source are copied to bits [159:128] of the
///          returned vector. \n
///      10: Bits [223:192] of the source are copied to bits [159:128] of the
///          returned vector. \n
///      11: Bits [255:224] of the source are copied to bits [159:128] of the
///          returned vector. \n
///    Bits [161:160]: \n
///      00: Bits [159:128] of the source are copied to bits [191:160] of the
///          returned vector. \n
///      01: Bits [191:160] of the source are copied to bits [191:160] of the
///          returned vector. \n
///      10: Bits [223:192] of the source are copied to bits [191:160] of the
///          returned vector. \n
///      11: Bits [255:224] of the source are copied to bits [191:160] of the
///          returned vector. \n
///    Bits [193:192]: \n
///      00: Bits [159:128] of the source are copied to bits [223:192] of the
///          returned vector. \n
///      01: Bits [191:160] of the source are copied to bits [223:192] of the
///          returned vector. \n
///      10: Bits [223:192] of the source are copied to bits [223:192] of the
///          returned vector. \n
///      11: Bits [255:224] of the source are copied to bits [223:192] of the
///          returned vector. \n
///    Bits [225:224]: \n
///      00: Bits [159:128] of the source are copied to bits [255:224] of the
///          returned vector. \n
///      01: Bits [191:160] of the source are copied to bits [255:224] of the
///          returned vector. \n
///      10: Bits [223:192] of the source are copied to bits [255:224] of the
///          returned vector. \n
///      11: Bits [255:224] of the source are copied to bits [255:224] of the
///          returned vector.
/// \returns A 256-bit vector of [8 x float] containing the copied values.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_permutevar_ps(__m256 __a, __m256i __c)
{
  return (__m256)__builtin_ia32_vpermilvarps256((__v8sf)__a, (__v8si)__c);
}

/// Copies the values in a 128-bit vector of [2 x double] as specified
///    by the immediate integer operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm_permute_pd(__m128d A, const int C);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPERMILPD </c> instruction.
///
/// \param A
///    A 128-bit vector of [2 x double].
/// \param C
///    An immediate integer operand specifying how the values are to be
///    copied. \n
///    Bit [0]: \n
///      0: Bits [63:0] of the source are copied to bits [63:0] of the returned
///         vector. \n
///      1: Bits [127:64] of the source are copied to bits [63:0] of the
///         returned vector. \n
///    Bit [1]: \n
///      0: Bits [63:0] of the source are copied to bits [127:64] of the
///         returned vector. \n
///      1: Bits [127:64] of the source are copied to bits [127:64] of the
///         returned vector.
/// \returns A 128-bit vector of [2 x double] containing the copied values.
#define _mm_permute_pd(A, C) \
  (__m128d)__builtin_ia32_vpermilpd((__v2df)(__m128d)(A), (int)(C))

/// Copies the values in a 256-bit vector of [4 x double] as specified by
///    the immediate integer operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256d _mm256_permute_pd(__m256d A, const int C);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPERMILPD </c> instruction.
///
/// \param A
///    A 256-bit vector of [4 x double].
/// \param C
///    An immediate integer operand specifying how the values are to be
///    copied. \n
///    Bit [0]: \n
///      0: Bits [63:0] of the source are copied to bits [63:0] of the returned
///         vector. \n
///      1: Bits [127:64] of the source are copied to bits [63:0] of the
///         returned vector. \n
///    Bit [1]: \n
///      0: Bits [63:0] of the source are copied to bits [127:64] of the
///         returned vector. \n
///      1: Bits [127:64] of the source are copied to bits [127:64] of the
///         returned vector. \n
///    Bit [2]: \n
///      0: Bits [191:128] of the source are copied to bits [191:128] of the
///         returned vector. \n
///      1: Bits [255:192] of the source are copied to bits [191:128] of the
///         returned vector. \n
///    Bit [3]: \n
///      0: Bits [191:128] of the source are copied to bits [255:192] of the
///         returned vector. \n
///      1: Bits [255:192] of the source are copied to bits [255:192] of the
///         returned vector.
/// \returns A 256-bit vector of [4 x double] containing the copied values.
#define _mm256_permute_pd(A, C) \
  (__m256d)__builtin_ia32_vpermilpd256((__v4df)(__m256d)(A), (int)(C))

/// Copies the values in a 128-bit vector of [4 x float] as specified by
///    the immediate integer operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm_permute_ps(__m128 A, const int C);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPERMILPS </c> instruction.
///
/// \param A
///    A 128-bit vector of [4 x float].
/// \param C
///    An immediate integer operand specifying how the values are to be
///    copied. \n
///    Bits [1:0]: \n
///      00: Bits [31:0] of the source are copied to bits [31:0] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [31:0] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [31:0] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [31:0] of the
///          returned vector. \n
///    Bits [3:2]: \n
///      00: Bits [31:0] of the source are copied to bits [63:32] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [63:32] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [63:32] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [63:32] of the
///          returned vector. \n
///    Bits [5:4]: \n
///      00: Bits [31:0] of the source are copied to bits [95:64] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [95:64] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [95:64] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [95:64] of the
///          returned vector. \n
///    Bits [7:6]: \n
///      00: Bits [31:0] of the source are copied to bits [127:96] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [127:96] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [127:96] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [127:96] of the
///          returned vector.
/// \returns A 128-bit vector of [4 x float] containing the copied values.
#define _mm_permute_ps(A, C) \
  (__m128)__builtin_ia32_vpermilps((__v4sf)(__m128)(A), (int)(C))

/// Copies the values in a 256-bit vector of [8 x float] as specified by
///    the immediate integer operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256 _mm256_permute_ps(__m256 A, const int C);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPERMILPS </c> instruction.
///
/// \param A
///    A 256-bit vector of [8 x float].
/// \param C
///    An immediate integer operand specifying how the values are to be
///    copied. \n
///    Bits [1:0]: \n
///      00: Bits [31:0] of the source are copied to bits [31:0] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [31:0] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [31:0] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [31:0] of the
///          returned vector. \n
///    Bits [3:2]: \n
///      00: Bits [31:0] of the source are copied to bits [63:32] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [63:32] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [63:32] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [63:32] of the
///          returned vector. \n
///    Bits [5:4]: \n
///      00: Bits [31:0] of the source are copied to bits [95:64] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [95:64] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [95:64] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [95:64] of the
///          returned vector. \n
///    Bits [7:6]: \n
///      00: Bits [31:0] of the source are copied to bits [127:96] of the
///          returned vector. \n
///      01: Bits [63:32] of the source are copied to bits [127:96] of the
///          returned vector. \n
///      10: Bits [95:64] of the source are copied to bits [127:96] of the
///          returned vector. \n
///      11: Bits [127:96] of the source are copied to bits [127:96] of the
///          returned vector. \n
///    Bits [1:0]: \n
///      00: Bits [159:128] of the source are copied to bits [159:128] of the
///          returned vector. \n
///      01: Bits [191:160] of the source are copied to bits [159:128] of the
///          returned vector. \n
///      10: Bits [223:192] of the source are copied to bits [159:128] of the
///          returned vector. \n
///      11: Bits [255:224] of the source are copied to bits [159:128] of the
///          returned vector. \n
///    Bits [3:2]: \n
///      00: Bits [159:128] of the source are copied to bits [191:160] of the
///          returned vector. \n
///      01: Bits [191:160] of the source are copied to bits [191:160] of the
///          returned vector. \n
///      10: Bits [223:192] of the source are copied to bits [191:160] of the
///          returned vector. \n
///      11: Bits [255:224] of the source are copied to bits [191:160] of the
///          returned vector. \n
///    Bits [5:4]: \n
///      00: Bits [159:128] of the source are copied to bits [223:192] of the
///          returned vector. \n
///      01: Bits [191:160] of the source are copied to bits [223:192] of the
///          returned vector. \n
///      10: Bits [223:192] of the source are copied to bits [223:192] of the
///          returned vector. \n
///      11: Bits [255:224] of the source are copied to bits [223:192] of the
///          returned vector. \n
///    Bits [7:6]: \n
///      00: Bits [159:128] of the source are copied to bits [255:224] of the
///          returned vector. \n
///      01: Bits [191:160] of the source are copied to bits [255:224] of the
///          returned vector. \n
///      10: Bits [223:192] of the source are copied to bits [255:224] of the
///          returned vector. \n
///      11: Bits [255:224] of the source are copied to bits [255:224] of the
///          returned vector.
/// \returns A 256-bit vector of [8 x float] containing the copied values.
#define _mm256_permute_ps(A, C) \
  (__m256)__builtin_ia32_vpermilps256((__v8sf)(__m256)(A), (int)(C))

/// Permutes 128-bit data values stored in two 256-bit vectors of
///    [4 x double], as specified by the immediate integer operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256d _mm256_permute2f128_pd(__m256d V1, __m256d V2, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPERM2F128 </c> instruction.
///
/// \param V1
///    A 256-bit vector of [4 x double].
/// \param V2
///    A 256-bit vector of [4 x double.
/// \param M
///    An immediate integer operand specifying how the values are to be
///    permuted. \n
///    Bits [1:0]: \n
///      00: Bits [127:0] of operand \a V1 are copied to bits [127:0] of the
///          destination. \n
///      01: Bits [255:128] of operand \a V1 are copied to bits [127:0] of the
///          destination. \n
///      10: Bits [127:0] of operand \a V2 are copied to bits [127:0] of the
///          destination. \n
///      11: Bits [255:128] of operand \a V2 are copied to bits [127:0] of the
///          destination. \n
///    Bits [5:4]: \n
///      00: Bits [127:0] of operand \a V1 are copied to bits [255:128] of the
///          destination. \n
///      01: Bits [255:128] of operand \a V1 are copied to bits [255:128] of the
///          destination. \n
///      10: Bits [127:0] of operand \a V2 are copied to bits [255:128] of the
///          destination. \n
///      11: Bits [255:128] of operand \a V2 are copied to bits [255:128] of the
///          destination.
/// \returns A 256-bit vector of [4 x double] containing the copied values.
#define _mm256_permute2f128_pd(V1, V2, M) \
  (__m256d)__builtin_ia32_vperm2f128_pd256((__v4df)(__m256d)(V1), \
                                           (__v4df)(__m256d)(V2), (int)(M))

/// Permutes 128-bit data values stored in two 256-bit vectors of
///    [8 x float], as specified by the immediate integer operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256 _mm256_permute2f128_ps(__m256 V1, __m256 V2, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPERM2F128 </c> instruction.
///
/// \param V1
///    A 256-bit vector of [8 x float].
/// \param V2
///    A 256-bit vector of [8 x float].
/// \param M
///    An immediate integer operand specifying how the values are to be
///    permuted. \n
///    Bits [1:0]: \n
///    00: Bits [127:0] of operand \a V1 are copied to bits [127:0] of the
///    destination. \n
///    01: Bits [255:128] of operand \a V1 are copied to bits [127:0] of the
///    destination. \n
///    10: Bits [127:0] of operand \a V2 are copied to bits [127:0] of the
///    destination. \n
///    11: Bits [255:128] of operand \a V2 are copied to bits [127:0] of the
///    destination. \n
///    Bits [5:4]: \n
///    00: Bits [127:0] of operand \a V1 are copied to bits [255:128] of the
///    destination. \n
///    01: Bits [255:128] of operand \a V1 are copied to bits [255:128] of the
///    destination. \n
///    10: Bits [127:0] of operand \a V2 are copied to bits [255:128] of the
///    destination. \n
///    11: Bits [255:128] of operand \a V2 are copied to bits [255:128] of the
///    destination.
/// \returns A 256-bit vector of [8 x float] containing the copied values.
#define _mm256_permute2f128_ps(V1, V2, M) \
  (__m256)__builtin_ia32_vperm2f128_ps256((__v8sf)(__m256)(V1), \
                                          (__v8sf)(__m256)(V2), (int)(M))

/// Permutes 128-bit data values stored in two 256-bit integer vectors,
///    as specified by the immediate integer operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256i _mm256_permute2f128_si256(__m256i V1, __m256i V2, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPERM2F128 </c> instruction.
///
/// \param V1
///    A 256-bit integer vector.
/// \param V2
///    A 256-bit integer vector.
/// \param M
///    An immediate integer operand specifying how the values are to be copied.
///    Bits [1:0]: \n
///    00: Bits [127:0] of operand \a V1 are copied to bits [127:0] of the
///    destination. \n
///    01: Bits [255:128] of operand \a V1 are copied to bits [127:0] of the
///    destination. \n
///    10: Bits [127:0] of operand \a V2 are copied to bits [127:0] of the
///    destination. \n
///    11: Bits [255:128] of operand \a V2 are copied to bits [127:0] of the
///    destination. \n
///    Bits [5:4]: \n
///    00: Bits [127:0] of operand \a V1 are copied to bits [255:128] of the
///    destination. \n
///    01: Bits [255:128] of operand \a V1 are copied to bits [255:128] of the
///    destination. \n
///    10: Bits [127:0] of operand \a V2 are copied to bits [255:128] of the
///    destination. \n
///    11: Bits [255:128] of operand \a V2 are copied to bits [255:128] of the
///    destination.
/// \returns A 256-bit integer vector containing the copied values.
#define _mm256_permute2f128_si256(V1, V2, M) \
  (__m256i)__builtin_ia32_vperm2f128_si256((__v8si)(__m256i)(V1), \
                                           (__v8si)(__m256i)(V2), (int)(M))

/* Vector Blend */
/// Merges 64-bit double-precision data values stored in either of the
///    two 256-bit vectors of [4 x double], as specified by the immediate
///    integer operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256d _mm256_blend_pd(__m256d V1, __m256d V2, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VBLENDPD </c> instruction.
///
/// \param V1
///    A 256-bit vector of [4 x double].
/// \param V2
///    A 256-bit vector of [4 x double].
/// \param M
///    An immediate integer operand, with mask bits [3:0] specifying how the
///    values are to be copied. The position of the mask bit corresponds to the
///    index of a copied value. When a mask bit is 0, the corresponding 64-bit
///    element in operand \a V1 is copied to the same position in the
///    destination. When a mask bit is 1, the corresponding 64-bit element in
///    operand \a V2 is copied to the same position in the destination.
/// \returns A 256-bit vector of [4 x double] containing the copied values.
#define _mm256_blend_pd(V1, V2, M) \
  (__m256d)__builtin_ia32_blendpd256((__v4df)(__m256d)(V1), \
                                     (__v4df)(__m256d)(V2), (int)(M))

/// Merges 32-bit single-precision data values stored in either of the
///    two 256-bit vectors of [8 x float], as specified by the immediate
///    integer operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256 _mm256_blend_ps(__m256 V1, __m256 V2, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VBLENDPS </c> instruction.
///
/// \param V1
///    A 256-bit vector of [8 x float].
/// \param V2
///    A 256-bit vector of [8 x float].
/// \param M
///    An immediate integer operand, with mask bits [7:0] specifying how the
///    values are to be copied. The position of the mask bit corresponds to the
///    index of a copied value. When a mask bit is 0, the corresponding 32-bit
///    element in operand \a V1 is copied to the same position in the
///    destination. When a mask bit is 1, the corresponding 32-bit element in
///    operand \a V2 is copied to the same position in the destination.
/// \returns A 256-bit vector of [8 x float] containing the copied values.
#define _mm256_blend_ps(V1, V2, M) \
  (__m256)__builtin_ia32_blendps256((__v8sf)(__m256)(V1), \
                                    (__v8sf)(__m256)(V2), (int)(M))

/// Merges 64-bit double-precision data values stored in either of the
///    two 256-bit vectors of [4 x double], as specified by the 256-bit vector
///    operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VBLENDVPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double].
/// \param __b
///    A 256-bit vector of [4 x double].
/// \param __c
///    A 256-bit vector operand, with mask bits 255, 191, 127, and 63 specifying
///    how the values are to be copied. The position of the mask bit corresponds
///    to the most significant bit of a copied value. When a mask bit is 0, the
///    corresponding 64-bit element in operand \a __a is copied to the same
///    position in the destination. When a mask bit is 1, the corresponding
///    64-bit element in operand \a __b is copied to the same position in the
///    destination.
/// \returns A 256-bit vector of [4 x double] containing the copied values.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_blendv_pd(__m256d __a, __m256d __b, __m256d __c)
{
  return (__m256d)__builtin_ia32_blendvpd256(
    (__v4df)__a, (__v4df)__b, (__v4df)__c);
}

/// Merges 32-bit single-precision data values stored in either of the
///    two 256-bit vectors of [8 x float], as specified by the 256-bit vector
///    operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VBLENDVPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float].
/// \param __b
///    A 256-bit vector of [8 x float].
/// \param __c
///    A 256-bit vector operand, with mask bits 255, 223, 191, 159, 127, 95, 63,
///    and 31 specifying how the values are to be copied. The position of the
///    mask bit corresponds to the most significant bit of a copied value. When
///    a mask bit is 0, the corresponding 32-bit element in operand \a __a is
///    copied to the same position in the destination. When a mask bit is 1, the
///    corresponding 32-bit element in operand \a __b is copied to the same
///    position in the destination.
/// \returns A 256-bit vector of [8 x float] containing the copied values.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_blendv_ps(__m256 __a, __m256 __b, __m256 __c)
{
  return (__m256)__builtin_ia32_blendvps256(
    (__v8sf)__a, (__v8sf)__b, (__v8sf)__c);
}

/* Vector Dot Product */
/// Computes two dot products in parallel, using the lower and upper
///    halves of two [8 x float] vectors as input to the two computations, and
///    returning the two dot products in the lower and upper halves of the
///    [8 x float] result.
///
///    The immediate integer operand controls which input elements will
///    contribute to the dot product, and where the final results are returned.
///    In general, for each dot product, the four corresponding elements of the
///    input vectors are multiplied; the first two and second two products are
///    summed, then the two sums are added to form the final result.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256 _mm256_dp_ps(__m256 V1, __m256 V2, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VDPPS </c> instruction.
///
/// \param V1
///    A vector of [8 x float] values, treated as two [4 x float] vectors.
/// \param V2
///    A vector of [8 x float] values, treated as two [4 x float] vectors.
/// \param M
///    An immediate integer argument. Bits [7:4] determine which elements of
///    the input vectors are used, with bit [4] corresponding to the lowest
///    element and bit [7] corresponding to the highest element of each [4 x
///    float] subvector. If a bit is set, the corresponding elements from the
///    two input vectors are used as an input for dot product; otherwise that
///    input is treated as zero. Bits [3:0] determine which elements of the
///    result will receive a copy of the final dot product, with bit [0]
///    corresponding to the lowest element and bit [3] corresponding to the
///    highest element of each [4 x float] subvector. If a bit is set, the dot
///    product is returned in the corresponding element; otherwise that element
///    is set to zero. The bitmask is applied in the same way to each of the
///    two parallel dot product computations.
/// \returns A 256-bit vector of [8 x float] containing the two dot products.
#define _mm256_dp_ps(V1, V2, M) \
  (__m256)__builtin_ia32_dpps256((__v8sf)(__m256)(V1), \
                                 (__v8sf)(__m256)(V2), (M))

/* Vector shuffle */
/// Selects 8 float values from the 256-bit operands of [8 x float], as
///    specified by the immediate value operand.
///
///    The four selected elements in each operand are copied to the destination
///    according to the bits specified in the immediate operand. The selected
///    elements from the first 256-bit operand are copied to bits [63:0] and
///    bits [191:128] of the destination, and the selected elements from the
///    second 256-bit operand are copied to bits [127:64] and bits [255:192] of
///    the destination. For example, if bits [7:0] of the immediate operand
///    contain a value of 0xFF, the 256-bit destination vector would contain the
///    following values: b[7], b[7], a[7], a[7], b[3], b[3], a[3], a[3].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256 _mm256_shuffle_ps(__m256 a, __m256 b, const int mask);
/// \endcode
///
/// This intrinsic corresponds to the <c> VSHUFPS </c> instruction.
///
/// \param a
///    A 256-bit vector of [8 x float]. The four selected elements in this
///    operand are copied to bits [63:0] and bits [191:128] in the destination,
///    according to the bits specified in the immediate operand.
/// \param b
///    A 256-bit vector of [8 x float]. The four selected elements in this
///    operand are copied to bits [127:64] and bits [255:192] in the
///    destination, according to the bits specified in the immediate operand.
/// \param mask
///    An immediate value containing an 8-bit value specifying which elements to
///    copy from \a a and \a b \n.
///    Bits [3:0] specify the values copied from operand \a a. \n
///    Bits [7:4] specify the values copied from operand \a b. \n
///    The destinations within the 256-bit destination are assigned values as
///    follows, according to the bit value assignments described below: \n
///    Bits [1:0] are used to assign values to bits [31:0] and [159:128] in the
///    destination. \n
///    Bits [3:2] are used to assign values to bits [63:32] and [191:160] in the
///    destination. \n
///    Bits [5:4] are used to assign values to bits [95:64] and [223:192] in the
///    destination. \n
///    Bits [7:6] are used to assign values to bits [127:96] and [255:224] in
///    the destination. \n
///    Bit value assignments: \n
///    00: Bits [31:0] and [159:128] are copied from the selected operand. \n
///    01: Bits [63:32] and [191:160] are copied from the selected operand. \n
///    10: Bits [95:64] and [223:192] are copied from the selected operand. \n
///    11: Bits [127:96] and [255:224] are copied from the selected operand.
/// \returns A 256-bit vector of [8 x float] containing the shuffled values.
#define _mm256_shuffle_ps(a, b, mask) \
  (__m256)__builtin_ia32_shufps256((__v8sf)(__m256)(a), \
                                   (__v8sf)(__m256)(b), (int)(mask))

/// Selects four double-precision values from the 256-bit operands of
///    [4 x double], as specified by the immediate value operand.
///
///    The selected elements from the first 256-bit operand are copied to bits
///    [63:0] and bits [191:128] in the destination, and the selected elements
///    from the second 256-bit operand are copied to bits [127:64] and bits
///    [255:192] in the destination. For example, if bits [3:0] of the immediate
///    operand contain a value of 0xF, the 256-bit destination vector would
///    contain the following values: b[3], a[3], b[1], a[1].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256d _mm256_shuffle_pd(__m256d a, __m256d b, const int mask);
/// \endcode
///
/// This intrinsic corresponds to the <c> VSHUFPD </c> instruction.
///
/// \param a
///    A 256-bit vector of [4 x double].
/// \param b
///    A 256-bit vector of [4 x double].
/// \param mask
///    An immediate value containing 8-bit values specifying which elements to
///    copy from \a a and \a b: \n
///    Bit [0]=0: Bits [63:0] are copied from \a a to bits [63:0] of the
///    destination. \n
///    Bit [0]=1: Bits [127:64] are copied from \a a to bits [63:0] of the
///    destination. \n
///    Bit [1]=0: Bits [63:0] are copied from \a b to bits [127:64] of the
///    destination. \n
///    Bit [1]=1: Bits [127:64] are copied from \a b to bits [127:64] of the
///    destination. \n
///    Bit [2]=0: Bits [191:128] are copied from \a a to bits [191:128] of the
///    destination. \n
///    Bit [2]=1: Bits [255:192] are copied from \a a to bits [191:128] of the
///    destination. \n
///    Bit [3]=0: Bits [191:128] are copied from \a b to bits [255:192] of the
///    destination. \n
///    Bit [3]=1: Bits [255:192] are copied from \a b to bits [255:192] of the
///    destination.
/// \returns A 256-bit vector of [4 x double] containing the shuffled values.
#define _mm256_shuffle_pd(a, b, mask) \
  (__m256d)__builtin_ia32_shufpd256((__v4df)(__m256d)(a), \
                                    (__v4df)(__m256d)(b), (int)(mask))

/* Compare */
#define _CMP_EQ_OQ    0x00 /* Equal (ordered, non-signaling)  */
#define _CMP_LT_OS    0x01 /* Less-than (ordered, signaling)  */
#define _CMP_LE_OS    0x02 /* Less-than-or-equal (ordered, signaling)  */
#define _CMP_UNORD_Q  0x03 /* Unordered (non-signaling)  */
#define _CMP_NEQ_UQ   0x04 /* Not-equal (unordered, non-signaling)  */
#define _CMP_NLT_US   0x05 /* Not-less-than (unordered, signaling)  */
#define _CMP_NLE_US   0x06 /* Not-less-than-or-equal (unordered, signaling)  */
#define _CMP_ORD_Q    0x07 /* Ordered (non-signaling)   */
#define _CMP_EQ_UQ    0x08 /* Equal (unordered, non-signaling)  */
#define _CMP_NGE_US   0x09 /* Not-greater-than-or-equal (unordered, signaling)  */
#define _CMP_NGT_US   0x0a /* Not-greater-than (unordered, signaling)  */
#define _CMP_FALSE_OQ 0x0b /* False (ordered, non-signaling)  */
#define _CMP_NEQ_OQ   0x0c /* Not-equal (ordered, non-signaling)  */
#define _CMP_GE_OS    0x0d /* Greater-than-or-equal (ordered, signaling)  */
#define _CMP_GT_OS    0x0e /* Greater-than (ordered, signaling)  */
#define _CMP_TRUE_UQ  0x0f /* True (unordered, non-signaling)  */
#define _CMP_EQ_OS    0x10 /* Equal (ordered, signaling)  */
#define _CMP_LT_OQ    0x11 /* Less-than (ordered, non-signaling)  */
#define _CMP_LE_OQ    0x12 /* Less-than-or-equal (ordered, non-signaling)  */
#define _CMP_UNORD_S  0x13 /* Unordered (signaling)  */
#define _CMP_NEQ_US   0x14 /* Not-equal (unordered, signaling)  */
#define _CMP_NLT_UQ   0x15 /* Not-less-than (unordered, non-signaling)  */
#define _CMP_NLE_UQ   0x16 /* Not-less-than-or-equal (unordered, non-signaling)  */
#define _CMP_ORD_S    0x17 /* Ordered (signaling)  */
#define _CMP_EQ_US    0x18 /* Equal (unordered, signaling)  */
#define _CMP_NGE_UQ   0x19 /* Not-greater-than-or-equal (unordered, non-signaling)  */
#define _CMP_NGT_UQ   0x1a /* Not-greater-than (unordered, non-signaling)  */
#define _CMP_FALSE_OS 0x1b /* False (ordered, signaling)  */
#define _CMP_NEQ_OS   0x1c /* Not-equal (ordered, signaling)  */
#define _CMP_GE_OQ    0x1d /* Greater-than-or-equal (ordered, non-signaling)  */
#define _CMP_GT_OQ    0x1e /* Greater-than (ordered, non-signaling)  */
#define _CMP_TRUE_US  0x1f /* True (unordered, signaling)  */

/// Compares each of the corresponding double-precision values of two
///    128-bit vectors of [2 x double], using the operation specified by the
///    immediate integer operand.
///
///    Returns a [2 x double] vector consisting of two doubles corresponding to
///    the two comparison results: zero if the comparison is false, and all 1's
///    if the comparison is true.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm_cmp_pd(__m128d a, __m128d b, const int c);
/// \endcode
///
/// This intrinsic corresponds to the <c> VCMPPD </c> instruction.
///
/// \param a
///    A 128-bit vector of [2 x double].
/// \param b
///    A 128-bit vector of [2 x double].
/// \param c
///    An immediate integer operand, with bits [4:0] specifying which comparison
///    operation to use: \n
///    0x00: Equal (ordered, non-signaling) \n
///    0x01: Less-than (ordered, signaling) \n
///    0x02: Less-than-or-equal (ordered, signaling) \n
///    0x03: Unordered (non-signaling) \n
///    0x04: Not-equal (unordered, non-signaling) \n
///    0x05: Not-less-than (unordered, signaling) \n
///    0x06: Not-less-than-or-equal (unordered, signaling) \n
///    0x07: Ordered (non-signaling) \n
///    0x08: Equal (unordered, non-signaling) \n
///    0x09: Not-greater-than-or-equal (unordered, signaling) \n
///    0x0A: Not-greater-than (unordered, signaling) \n
///    0x0B: False (ordered, non-signaling) \n
///    0x0C: Not-equal (ordered, non-signaling) \n
///    0x0D: Greater-than-or-equal (ordered, signaling) \n
///    0x0E: Greater-than (ordered, signaling) \n
///    0x0F: True (unordered, non-signaling) \n
///    0x10: Equal (ordered, signaling) \n
///    0x11: Less-than (ordered, non-signaling) \n
///    0x12: Less-than-or-equal (ordered, non-signaling) \n
///    0x13: Unordered (signaling) \n
///    0x14: Not-equal (unordered, signaling) \n
///    0x15: Not-less-than (unordered, non-signaling) \n
///    0x16: Not-less-than-or-equal (unordered, non-signaling) \n
///    0x17: Ordered (signaling) \n
///    0x18: Equal (unordered, signaling) \n
///    0x19: Not-greater-than-or-equal (unordered, non-signaling) \n
///    0x1A: Not-greater-than (unordered, non-signaling) \n
///    0x1B: False (ordered, signaling) \n
///    0x1C: Not-equal (ordered, signaling) \n
///    0x1D: Greater-than-or-equal (ordered, non-signaling) \n
///    0x1E: Greater-than (ordered, non-signaling) \n
///    0x1F: True (unordered, signaling)
/// \returns A 128-bit vector of [2 x double] containing the comparison results.
#define _mm_cmp_pd(a, b, c) \
  (__m128d)__builtin_ia32_cmppd((__v2df)(__m128d)(a), \
                                (__v2df)(__m128d)(b), (c))

/// Compares each of the corresponding values of two 128-bit vectors of
///    [4 x float], using the operation specified by the immediate integer
///    operand.
///
///    Returns a [4 x float] vector consisting of four floats corresponding to
///    the four comparison results: zero if the comparison is false, and all 1's
///    if the comparison is true.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm_cmp_ps(__m128 a, __m128 b, const int c);
/// \endcode
///
/// This intrinsic corresponds to the <c> VCMPPS </c> instruction.
///
/// \param a
///    A 128-bit vector of [4 x float].
/// \param b
///    A 128-bit vector of [4 x float].
/// \param c
///    An immediate integer operand, with bits [4:0] specifying which comparison
///    operation to use: \n
///    0x00: Equal (ordered, non-signaling) \n
///    0x01: Less-than (ordered, signaling) \n
///    0x02: Less-than-or-equal (ordered, signaling) \n
///    0x03: Unordered (non-signaling) \n
///    0x04: Not-equal (unordered, non-signaling) \n
///    0x05: Not-less-than (unordered, signaling) \n
///    0x06: Not-less-than-or-equal (unordered, signaling) \n
///    0x07: Ordered (non-signaling) \n
///    0x08: Equal (unordered, non-signaling) \n
///    0x09: Not-greater-than-or-equal (unordered, signaling) \n
///    0x0A: Not-greater-than (unordered, signaling) \n
///    0x0B: False (ordered, non-signaling) \n
///    0x0C: Not-equal (ordered, non-signaling) \n
///    0x0D: Greater-than-or-equal (ordered, signaling) \n
///    0x0E: Greater-than (ordered, signaling) \n
///    0x0F: True (unordered, non-signaling) \n
///    0x10: Equal (ordered, signaling) \n
///    0x11: Less-than (ordered, non-signaling) \n
///    0x12: Less-than-or-equal (ordered, non-signaling) \n
///    0x13: Unordered (signaling) \n
///    0x14: Not-equal (unordered, signaling) \n
///    0x15: Not-less-than (unordered, non-signaling) \n
///    0x16: Not-less-than-or-equal (unordered, non-signaling) \n
///    0x17: Ordered (signaling) \n
///    0x18: Equal (unordered, signaling) \n
///    0x19: Not-greater-than-or-equal (unordered, non-signaling) \n
///    0x1A: Not-greater-than (unordered, non-signaling) \n
///    0x1B: False (ordered, signaling) \n
///    0x1C: Not-equal (ordered, signaling) \n
///    0x1D: Greater-than-or-equal (ordered, non-signaling) \n
///    0x1E: Greater-than (ordered, non-signaling) \n
///    0x1F: True (unordered, signaling)
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
#define _mm_cmp_ps(a, b, c) \
  (__m128)__builtin_ia32_cmpps((__v4sf)(__m128)(a), \
                               (__v4sf)(__m128)(b), (c))

/// Compares each of the corresponding double-precision values of two
///    256-bit vectors of [4 x double], using the operation specified by the
///    immediate integer operand.
///
///    Returns a [4 x double] vector consisting of four doubles corresponding to
///    the four comparison results: zero if the comparison is false, and all 1's
///    if the comparison is true.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256d _mm256_cmp_pd(__m256d a, __m256d b, const int c);
/// \endcode
///
/// This intrinsic corresponds to the <c> VCMPPD </c> instruction.
///
/// \param a
///    A 256-bit vector of [4 x double].
/// \param b
///    A 256-bit vector of [4 x double].
/// \param c
///    An immediate integer operand, with bits [4:0] specifying which comparison
///    operation to use: \n
///    0x00: Equal (ordered, non-signaling) \n
///    0x01: Less-than (ordered, signaling) \n
///    0x02: Less-than-or-equal (ordered, signaling) \n
///    0x03: Unordered (non-signaling) \n
///    0x04: Not-equal (unordered, non-signaling) \n
///    0x05: Not-less-than (unordered, signaling) \n
///    0x06: Not-less-than-or-equal (unordered, signaling) \n
///    0x07: Ordered (non-signaling) \n
///    0x08: Equal (unordered, non-signaling) \n
///    0x09: Not-greater-than-or-equal (unordered, signaling) \n
///    0x0A: Not-greater-than (unordered, signaling) \n
///    0x0B: False (ordered, non-signaling) \n
///    0x0C: Not-equal (ordered, non-signaling) \n
///    0x0D: Greater-than-or-equal (ordered, signaling) \n
///    0x0E: Greater-than (ordered, signaling) \n
///    0x0F: True (unordered, non-signaling) \n
///    0x10: Equal (ordered, signaling) \n
///    0x11: Less-than (ordered, non-signaling) \n
///    0x12: Less-than-or-equal (ordered, non-signaling) \n
///    0x13: Unordered (signaling) \n
///    0x14: Not-equal (unordered, signaling) \n
///    0x15: Not-less-than (unordered, non-signaling) \n
///    0x16: Not-less-than-or-equal (unordered, non-signaling) \n
///    0x17: Ordered (signaling) \n
///    0x18: Equal (unordered, signaling) \n
///    0x19: Not-greater-than-or-equal (unordered, non-signaling) \n
///    0x1A: Not-greater-than (unordered, non-signaling) \n
///    0x1B: False (ordered, signaling) \n
///    0x1C: Not-equal (ordered, signaling) \n
///    0x1D: Greater-than-or-equal (ordered, non-signaling) \n
///    0x1E: Greater-than (ordered, non-signaling) \n
///    0x1F: True (unordered, signaling)
/// \returns A 256-bit vector of [4 x double] containing the comparison results.
#define _mm256_cmp_pd(a, b, c) \
  (__m256d)__builtin_ia32_cmppd256((__v4df)(__m256d)(a), \
                                   (__v4df)(__m256d)(b), (c))

/// Compares each of the corresponding values of two 256-bit vectors of
///    [8 x float], using the operation specified by the immediate integer
///    operand.
///
///    Returns a [8 x float] vector consisting of eight floats corresponding to
///    the eight comparison results: zero if the comparison is false, and all
///    1's if the comparison is true.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256 _mm256_cmp_ps(__m256 a, __m256 b, const int c);
/// \endcode
///
/// This intrinsic corresponds to the <c> VCMPPS </c> instruction.
///
/// \param a
///    A 256-bit vector of [8 x float].
/// \param b
///    A 256-bit vector of [8 x float].
/// \param c
///    An immediate integer operand, with bits [4:0] specifying which comparison
///    operation to use: \n
///    0x00: Equal (ordered, non-signaling) \n
///    0x01: Less-than (ordered, signaling) \n
///    0x02: Less-than-or-equal (ordered, signaling) \n
///    0x03: Unordered (non-signaling) \n
///    0x04: Not-equal (unordered, non-signaling) \n
///    0x05: Not-less-than (unordered, signaling) \n
///    0x06: Not-less-than-or-equal (unordered, signaling) \n
///    0x07: Ordered (non-signaling) \n
///    0x08: Equal (unordered, non-signaling) \n
///    0x09: Not-greater-than-or-equal (unordered, signaling) \n
///    0x0A: Not-greater-than (unordered, signaling) \n
///    0x0B: False (ordered, non-signaling) \n
///    0x0C: Not-equal (ordered, non-signaling) \n
///    0x0D: Greater-than-or-equal (ordered, signaling) \n
///    0x0E: Greater-than (ordered, signaling) \n
///    0x0F: True (unordered, non-signaling) \n
///    0x10: Equal (ordered, signaling) \n
///    0x11: Less-than (ordered, non-signaling) \n
///    0x12: Less-than-or-equal (ordered, non-signaling) \n
///    0x13: Unordered (signaling) \n
///    0x14: Not-equal (unordered, signaling) \n
///    0x15: Not-less-than (unordered, non-signaling) \n
///    0x16: Not-less-than-or-equal (unordered, non-signaling) \n
///    0x17: Ordered (signaling) \n
///    0x18: Equal (unordered, signaling) \n
///    0x19: Not-greater-than-or-equal (unordered, non-signaling) \n
///    0x1A: Not-greater-than (unordered, non-signaling) \n
///    0x1B: False (ordered, signaling) \n
///    0x1C: Not-equal (ordered, signaling) \n
///    0x1D: Greater-than-or-equal (ordered, non-signaling) \n
///    0x1E: Greater-than (ordered, non-signaling) \n
///    0x1F: True (unordered, signaling)
/// \returns A 256-bit vector of [8 x float] containing the comparison results.
#define _mm256_cmp_ps(a, b, c) \
  (__m256)__builtin_ia32_cmpps256((__v8sf)(__m256)(a), \
                                  (__v8sf)(__m256)(b), (c))

/// Compares each of the corresponding scalar double-precision values of
///    two 128-bit vectors of [2 x double], using the operation specified by the
///    immediate integer operand.
///
///    If the result is true, all 64 bits of the destination vector are set;
///    otherwise they are cleared.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm_cmp_sd(__m128d a, __m128d b, const int c);
/// \endcode
///
/// This intrinsic corresponds to the <c> VCMPSD </c> instruction.
///
/// \param a
///    A 128-bit vector of [2 x double].
/// \param b
///    A 128-bit vector of [2 x double].
/// \param c
///    An immediate integer operand, with bits [4:0] specifying which comparison
///    operation to use: \n
///    0x00: Equal (ordered, non-signaling) \n
///    0x01: Less-than (ordered, signaling) \n
///    0x02: Less-than-or-equal (ordered, signaling) \n
///    0x03: Unordered (non-signaling) \n
///    0x04: Not-equal (unordered, non-signaling) \n
///    0x05: Not-less-than (unordered, signaling) \n
///    0x06: Not-less-than-or-equal (unordered, signaling) \n
///    0x07: Ordered (non-signaling) \n
///    0x08: Equal (unordered, non-signaling) \n
///    0x09: Not-greater-than-or-equal (unordered, signaling) \n
///    0x0A: Not-greater-than (unordered, signaling) \n
///    0x0B: False (ordered, non-signaling) \n
///    0x0C: Not-equal (ordered, non-signaling) \n
///    0x0D: Greater-than-or-equal (ordered, signaling) \n
///    0x0E: Greater-than (ordered, signaling) \n
///    0x0F: True (unordered, non-signaling) \n
///    0x10: Equal (ordered, signaling) \n
///    0x11: Less-than (ordered, non-signaling) \n
///    0x12: Less-than-or-equal (ordered, non-signaling) \n
///    0x13: Unordered (signaling) \n
///    0x14: Not-equal (unordered, signaling) \n
///    0x15: Not-less-than (unordered, non-signaling) \n
///    0x16: Not-less-than-or-equal (unordered, non-signaling) \n
///    0x17: Ordered (signaling) \n
///    0x18: Equal (unordered, signaling) \n
///    0x19: Not-greater-than-or-equal (unordered, non-signaling) \n
///    0x1A: Not-greater-than (unordered, non-signaling) \n
///    0x1B: False (ordered, signaling) \n
///    0x1C: Not-equal (ordered, signaling) \n
///    0x1D: Greater-than-or-equal (ordered, non-signaling) \n
///    0x1E: Greater-than (ordered, non-signaling) \n
///    0x1F: True (unordered, signaling)
/// \returns A 128-bit vector of [2 x double] containing the comparison results.
#define _mm_cmp_sd(a, b, c) \
  (__m128d)__builtin_ia32_cmpsd((__v2df)(__m128d)(a), \
                                (__v2df)(__m128d)(b), (c))

/// Compares each of the corresponding scalar values of two 128-bit
///    vectors of [4 x float], using the operation specified by the immediate
///    integer operand.
///
///    If the result is true, all 32 bits of the destination vector are set;
///    otherwise they are cleared.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm_cmp_ss(__m128 a, __m128 b, const int c);
/// \endcode
///
/// This intrinsic corresponds to the <c> VCMPSS </c> instruction.
///
/// \param a
///    A 128-bit vector of [4 x float].
/// \param b
///    A 128-bit vector of [4 x float].
/// \param c
///    An immediate integer operand, with bits [4:0] specifying which comparison
///    operation to use: \n
///    0x00: Equal (ordered, non-signaling) \n
///    0x01: Less-than (ordered, signaling) \n
///    0x02: Less-than-or-equal (ordered, signaling) \n
///    0x03: Unordered (non-signaling) \n
///    0x04: Not-equal (unordered, non-signaling) \n
///    0x05: Not-less-than (unordered, signaling) \n
///    0x06: Not-less-than-or-equal (unordered, signaling) \n
///    0x07: Ordered (non-signaling) \n
///    0x08: Equal (unordered, non-signaling) \n
///    0x09: Not-greater-than-or-equal (unordered, signaling) \n
///    0x0A: Not-greater-than (unordered, signaling) \n
///    0x0B: False (ordered, non-signaling) \n
///    0x0C: Not-equal (ordered, non-signaling) \n
///    0x0D: Greater-than-or-equal (ordered, signaling) \n
///    0x0E: Greater-than (ordered, signaling) \n
///    0x0F: True (unordered, non-signaling) \n
///    0x10: Equal (ordered, signaling) \n
///    0x11: Less-than (ordered, non-signaling) \n
///    0x12: Less-than-or-equal (ordered, non-signaling) \n
///    0x13: Unordered (signaling) \n
///    0x14: Not-equal (unordered, signaling) \n
///    0x15: Not-less-than (unordered, non-signaling) \n
///    0x16: Not-less-than-or-equal (unordered, non-signaling) \n
///    0x17: Ordered (signaling) \n
///    0x18: Equal (unordered, signaling) \n
///    0x19: Not-greater-than-or-equal (unordered, non-signaling) \n
///    0x1A: Not-greater-than (unordered, non-signaling) \n
///    0x1B: False (ordered, signaling) \n
///    0x1C: Not-equal (ordered, signaling) \n
///    0x1D: Greater-than-or-equal (ordered, non-signaling) \n
///    0x1E: Greater-than (ordered, non-signaling) \n
///    0x1F: True (unordered, signaling)
/// \returns A 128-bit vector of [4 x float] containing the comparison results.
#define _mm_cmp_ss(a, b, c) \
  (__m128)__builtin_ia32_cmpss((__v4sf)(__m128)(a), \
                               (__v4sf)(__m128)(b), (c))

/// Takes a [8 x i32] vector and returns the vector element value
///    indexed by the immediate constant operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VEXTRACTF128+COMPOSITE </c>
///   instruction.
///
/// \param __a
///    A 256-bit vector of [8 x i32].
/// \param __imm
///    An immediate integer operand with bits [2:0] determining which vector
///    element is extracted and returned.
/// \returns A 32-bit integer containing the extracted 32 bits of extended
///    packed data.
#define _mm256_extract_epi32(X, N) \
  (int)__builtin_ia32_vec_ext_v8si((__v8si)(__m256i)(X), (int)(N))

/// Takes a [16 x i16] vector and returns the vector element value
///    indexed by the immediate constant operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VEXTRACTF128+COMPOSITE </c>
///   instruction.
///
/// \param __a
///    A 256-bit integer vector of [16 x i16].
/// \param __imm
///    An immediate integer operand with bits [3:0] determining which vector
///    element is extracted and returned.
/// \returns A 32-bit integer containing the extracted 16 bits of zero extended
///    packed data.
#define _mm256_extract_epi16(X, N) \
  (int)(unsigned short)__builtin_ia32_vec_ext_v16hi((__v16hi)(__m256i)(X), \
                                                    (int)(N))

/// Takes a [32 x i8] vector and returns the vector element value
///    indexed by the immediate constant operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VEXTRACTF128+COMPOSITE </c>
///   instruction.
///
/// \param __a
///    A 256-bit integer vector of [32 x i8].
/// \param __imm
///    An immediate integer operand with bits [4:0] determining which vector
///    element is extracted and returned.
/// \returns A 32-bit integer containing the extracted 8 bits of zero extended
///    packed data.
#define _mm256_extract_epi8(X, N) \
  (int)(unsigned char)__builtin_ia32_vec_ext_v32qi((__v32qi)(__m256i)(X), \
                                                   (int)(N))

#ifdef __x86_64__
/// Takes a [4 x i64] vector and returns the vector element value
///    indexed by the immediate constant operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VEXTRACTF128+COMPOSITE </c>
///   instruction.
///
/// \param __a
///    A 256-bit integer vector of [4 x i64].
/// \param __imm
///    An immediate integer operand with bits [1:0] determining which vector
///    element is extracted and returned.
/// \returns A 64-bit integer containing the extracted 64 bits of extended
///    packed data.
#define _mm256_extract_epi64(X, N) \
  (long long)__builtin_ia32_vec_ext_v4di((__v4di)(__m256i)(X), (int)(N))
#endif

/// Takes a [8 x i32] vector and replaces the vector element value
///    indexed by the immediate constant operand by a new value. Returns the
///    modified vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VINSERTF128+COMPOSITE </c>
///   instruction.
///
/// \param __a
///    A vector of [8 x i32] to be used by the insert operation.
/// \param __b
///    An integer value. The replacement value for the insert operation.
/// \param __imm
///    An immediate integer specifying the index of the vector element to be
///    replaced.
/// \returns A copy of vector \a __a, after replacing its element indexed by
///    \a __imm with \a __b.
#define _mm256_insert_epi32(X, I, N) \
  (__m256i)__builtin_ia32_vec_set_v8si((__v8si)(__m256i)(X), \
                                       (int)(I), (int)(N))


/// Takes a [16 x i16] vector and replaces the vector element value
///    indexed by the immediate constant operand with a new value. Returns the
///    modified vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VINSERTF128+COMPOSITE </c>
///   instruction.
///
/// \param __a
///    A vector of [16 x i16] to be used by the insert operation.
/// \param __b
///    An i16 integer value. The replacement value for the insert operation.
/// \param __imm
///    An immediate integer specifying the index of the vector element to be
///    replaced.
/// \returns A copy of vector \a __a, after replacing its element indexed by
///    \a __imm with \a __b.
#define _mm256_insert_epi16(X, I, N) \
  (__m256i)__builtin_ia32_vec_set_v16hi((__v16hi)(__m256i)(X), \
                                        (int)(I), (int)(N))

/// Takes a [32 x i8] vector and replaces the vector element value
///    indexed by the immediate constant operand with a new value. Returns the
///    modified vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VINSERTF128+COMPOSITE </c>
///   instruction.
///
/// \param __a
///    A vector of [32 x i8] to be used by the insert operation.
/// \param __b
///    An i8 integer value. The replacement value for the insert operation.
/// \param __imm
///    An immediate integer specifying the index of the vector element to be
///    replaced.
/// \returns A copy of vector \a __a, after replacing its element indexed by
///    \a __imm with \a __b.
#define _mm256_insert_epi8(X, I, N) \
  (__m256i)__builtin_ia32_vec_set_v32qi((__v32qi)(__m256i)(X), \
                                        (int)(I), (int)(N))

#ifdef __x86_64__
/// Takes a [4 x i64] vector and replaces the vector element value
///    indexed by the immediate constant operand with a new value. Returns the
///    modified vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VINSERTF128+COMPOSITE </c>
///   instruction.
///
/// \param __a
///    A vector of [4 x i64] to be used by the insert operation.
/// \param __b
///    A 64-bit integer value. The replacement value for the insert operation.
/// \param __imm
///    An immediate integer specifying the index of the vector element to be
///    replaced.
/// \returns A copy of vector \a __a, after replacing its element indexed by
///     \a __imm with \a __b.
#define _mm256_insert_epi64(X, I, N) \
  (__m256i)__builtin_ia32_vec_set_v4di((__v4di)(__m256i)(X), \
                                       (long long)(I), (int)(N))
#endif

/* Conversion */
/// Converts a vector of [4 x i32] into a vector of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTDQ2PD </c> instruction.
///
/// \param __a
///    A 128-bit integer vector of [4 x i32].
/// \returns A 256-bit vector of [4 x double] containing the converted values.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_cvtepi32_pd(__m128i __a)
{
  return (__m256d)__builtin_convertvector((__v4si)__a, __v4df);
}

/// Converts a vector of [8 x i32] into a vector of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTDQ2PS </c> instruction.
///
/// \param __a
///    A 256-bit integer vector.
/// \returns A 256-bit vector of [8 x float] containing the converted values.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_cvtepi32_ps(__m256i __a)
{
  return (__m256)__builtin_convertvector((__v8si)__a, __v8sf);
}

/// Converts a 256-bit vector of [4 x double] into a 128-bit vector of
///    [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTPD2PS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double].
/// \returns A 128-bit vector of [4 x float] containing the converted values.
static __inline __m128 __DEFAULT_FN_ATTRS
_mm256_cvtpd_ps(__m256d __a)
{
  return (__m128)__builtin_ia32_cvtpd2ps256((__v4df) __a);
}

/// Converts a vector of [8 x float] into a vector of [8 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTPS2DQ </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float].
/// \returns A 256-bit integer vector containing the converted values.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_cvtps_epi32(__m256 __a)
{
  return (__m256i)__builtin_ia32_cvtps2dq256((__v8sf) __a);
}

/// Converts a 128-bit vector of [4 x float] into a 256-bit vector of [4
///    x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTPS2PD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \returns A 256-bit vector of [4 x double] containing the converted values.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_cvtps_pd(__m128 __a)
{
  return (__m256d)__builtin_convertvector((__v4sf)__a, __v4df);
}

/// Converts a 256-bit vector of [4 x double] into a 128-bit vector of [4
///    x i32], truncating the result by rounding towards zero when it is
///    inexact.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTTPD2DQ </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double].
/// \returns A 128-bit integer vector containing the converted values.
static __inline __m128i __DEFAULT_FN_ATTRS
_mm256_cvttpd_epi32(__m256d __a)
{
  return (__m128i)__builtin_ia32_cvttpd2dq256((__v4df) __a);
}

/// Converts a 256-bit vector of [4 x double] into a 128-bit vector of [4
///    x i32]. When a conversion is inexact, the value returned is rounded
///    according to the rounding control bits in the MXCSR register.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTPD2DQ </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double].
/// \returns A 128-bit integer vector containing the converted values.
static __inline __m128i __DEFAULT_FN_ATTRS
_mm256_cvtpd_epi32(__m256d __a)
{
  return (__m128i)__builtin_ia32_cvtpd2dq256((__v4df) __a);
}

/// Converts a vector of [8 x float] into a vector of [8 x i32],
///    truncating the result by rounding towards zero when it is inexact.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VCVTTPS2DQ </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float].
/// \returns A 256-bit integer vector containing the converted values.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_cvttps_epi32(__m256 __a)
{
  return (__m256i)__builtin_ia32_cvttps2dq256((__v8sf) __a);
}

/// Returns the first element of the input vector of [4 x double].
///
/// \headerfile <avxintrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double].
/// \returns A 64 bit double containing the first element of the input vector.
static __inline double __DEFAULT_FN_ATTRS
_mm256_cvtsd_f64(__m256d __a)
{
 return __a[0];
}

/// Returns the first element of the input vector of [8 x i32].
///
/// \headerfile <avxintrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __a
///    A 256-bit vector of [8 x i32].
/// \returns A 32 bit integer containing the first element of the input vector.
static __inline int __DEFAULT_FN_ATTRS
_mm256_cvtsi256_si32(__m256i __a)
{
 __v8si __b = (__v8si)__a;
 return __b[0];
}

/// Returns the first element of the input vector of [8 x float].
///
/// \headerfile <avxintrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float].
/// \returns A 32 bit float containing the first element of the input vector.
static __inline float __DEFAULT_FN_ATTRS
_mm256_cvtss_f32(__m256 __a)
{
 return __a[0];
}

/* Vector replicate */
/// Moves and duplicates odd-indexed values from a 256-bit vector of
///    [8 x float] to float values in a 256-bit vector of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVSHDUP </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float]. \n
///    Bits [255:224] of \a __a are written to bits [255:224] and [223:192] of
///    the return value. \n
///    Bits [191:160] of \a __a are written to bits [191:160] and [159:128] of
///    the return value. \n
///    Bits [127:96] of \a __a are written to bits [127:96] and [95:64] of the
///    return value. \n
///    Bits [63:32] of \a __a are written to bits [63:32] and [31:0] of the
///    return value.
/// \returns A 256-bit vector of [8 x float] containing the moved and duplicated
///    values.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_movehdup_ps(__m256 __a)
{
  return __builtin_shufflevector((__v8sf)__a, (__v8sf)__a, 1, 1, 3, 3, 5, 5, 7, 7);
}

/// Moves and duplicates even-indexed values from a 256-bit vector of
///    [8 x float] to float values in a 256-bit vector of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVSLDUP </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float]. \n
///    Bits [223:192] of \a __a are written to bits [255:224] and [223:192] of
///    the return value. \n
///    Bits [159:128] of \a __a are written to bits [191:160] and [159:128] of
///    the return value. \n
///    Bits [95:64] of \a __a are written to bits [127:96] and [95:64] of the
///    return value. \n
///    Bits [31:0] of \a __a are written to bits [63:32] and [31:0] of the
///    return value.
/// \returns A 256-bit vector of [8 x float] containing the moved and duplicated
///    values.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_moveldup_ps(__m256 __a)
{
  return __builtin_shufflevector((__v8sf)__a, (__v8sf)__a, 0, 0, 2, 2, 4, 4, 6, 6);
}

/// Moves and duplicates double-precision floating point values from a
///    256-bit vector of [4 x double] to double-precision values in a 256-bit
///    vector of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVDDUP </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double]. \n
///    Bits [63:0] of \a __a are written to bits [127:64] and [63:0] of the
///    return value. \n
///    Bits [191:128] of \a __a are written to bits [255:192] and [191:128] of
///    the return value.
/// \returns A 256-bit vector of [4 x double] containing the moved and
///    duplicated values.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_movedup_pd(__m256d __a)
{
  return __builtin_shufflevector((__v4df)__a, (__v4df)__a, 0, 0, 2, 2);
}

/* Unpack and Interleave */
/// Unpacks the odd-indexed vector elements from two 256-bit vectors of
///    [4 x double] and interleaves them into a 256-bit vector of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUNPCKHPD </c> instruction.
///
/// \param __a
///    A 256-bit floating-point vector of [4 x double]. \n
///    Bits [127:64] are written to bits [63:0] of the return value. \n
///    Bits [255:192] are written to bits [191:128] of the return value. \n
/// \param __b
///    A 256-bit floating-point vector of [4 x double]. \n
///    Bits [127:64] are written to bits [127:64] of the return value. \n
///    Bits [255:192] are written to bits [255:192] of the return value. \n
/// \returns A 256-bit vector of [4 x double] containing the interleaved values.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_unpackhi_pd(__m256d __a, __m256d __b)
{
  return __builtin_shufflevector((__v4df)__a, (__v4df)__b, 1, 5, 1+2, 5+2);
}

/// Unpacks the even-indexed vector elements from two 256-bit vectors of
///    [4 x double] and interleaves them into a 256-bit vector of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUNPCKLPD </c> instruction.
///
/// \param __a
///    A 256-bit floating-point vector of [4 x double]. \n
///    Bits [63:0] are written to bits [63:0] of the return value. \n
///    Bits [191:128] are written to bits [191:128] of the return value.
/// \param __b
///    A 256-bit floating-point vector of [4 x double]. \n
///    Bits [63:0] are written to bits [127:64] of the return value. \n
///    Bits [191:128] are written to bits [255:192] of the return value. \n
/// \returns A 256-bit vector of [4 x double] containing the interleaved values.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_unpacklo_pd(__m256d __a, __m256d __b)
{
  return __builtin_shufflevector((__v4df)__a, (__v4df)__b, 0, 4, 0+2, 4+2);
}

/// Unpacks the 32-bit vector elements 2, 3, 6 and 7 from each of the
///    two 256-bit vectors of [8 x float] and interleaves them into a 256-bit
///    vector of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUNPCKHPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float]. \n
///    Bits [95:64] are written to bits [31:0] of the return value. \n
///    Bits [127:96] are written to bits [95:64] of the return value. \n
///    Bits [223:192] are written to bits [159:128] of the return value. \n
///    Bits [255:224] are written to bits [223:192] of the return value.
/// \param __b
///    A 256-bit vector of [8 x float]. \n
///    Bits [95:64] are written to bits [63:32] of the return value. \n
///    Bits [127:96] are written to bits [127:96] of the return value. \n
///    Bits [223:192] are written to bits [191:160] of the return value. \n
///    Bits [255:224] are written to bits [255:224] of the return value.
/// \returns A 256-bit vector of [8 x float] containing the interleaved values.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_unpackhi_ps(__m256 __a, __m256 __b)
{
  return __builtin_shufflevector((__v8sf)__a, (__v8sf)__b, 2, 10, 2+1, 10+1, 6, 14, 6+1, 14+1);
}

/// Unpacks the 32-bit vector elements 0, 1, 4 and 5 from each of the
///    two 256-bit vectors of [8 x float] and interleaves them into a 256-bit
///    vector of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUNPCKLPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float]. \n
///    Bits [31:0] are written to bits [31:0] of the return value. \n
///    Bits [63:32] are written to bits [95:64] of the return value. \n
///    Bits [159:128] are written to bits [159:128] of the return value. \n
///    Bits [191:160] are written to bits [223:192] of the return value.
/// \param __b
///    A 256-bit vector of [8 x float]. \n
///    Bits [31:0] are written to bits [63:32] of the return value. \n
///    Bits [63:32] are written to bits [127:96] of the return value. \n
///    Bits [159:128] are written to bits [191:160] of the return value. \n
///    Bits [191:160] are written to bits [255:224] of the return value.
/// \returns A 256-bit vector of [8 x float] containing the interleaved values.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_unpacklo_ps(__m256 __a, __m256 __b)
{
  return __builtin_shufflevector((__v8sf)__a, (__v8sf)__b, 0, 8, 0+1, 8+1, 4, 12, 4+1, 12+1);
}

/* Bit Test */
/// Given two 128-bit floating-point vectors of [2 x double], perform an
///    element-by-element comparison of the double-precision element in the
///    first source vector and the corresponding element in the second source
///    vector.
///
///    The EFLAGS register is updated as follows: \n
///    If there is at least one pair of double-precision elements where the
///    sign-bits of both elements are 1, the ZF flag is set to 0. Otherwise the
///    ZF flag is set to 1. \n
///    If there is at least one pair of double-precision elements where the
///    sign-bit of the first element is 0 and the sign-bit of the second element
///    is 1, the CF flag is set to 0. Otherwise the CF flag is set to 1. \n
///    This intrinsic returns the value of the ZF flag.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VTESTPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __b
///    A 128-bit vector of [2 x double].
/// \returns the ZF flag in the EFLAGS register.
static __inline int __DEFAULT_FN_ATTRS128
_mm_testz_pd(__m128d __a, __m128d __b)
{
  return __builtin_ia32_vtestzpd((__v2df)__a, (__v2df)__b);
}

/// Given two 128-bit floating-point vectors of [2 x double], perform an
///    element-by-element comparison of the double-precision element in the
///    first source vector and the corresponding element in the second source
///    vector.
///
///    The EFLAGS register is updated as follows: \n
///    If there is at least one pair of double-precision elements where the
///    sign-bits of both elements are 1, the ZF flag is set to 0. Otherwise the
///    ZF flag is set to 1. \n
///    If there is at least one pair of double-precision elements where the
///    sign-bit of the first element is 0 and the sign-bit of the second element
///    is 1, the CF flag is set to 0. Otherwise the CF flag is set to 1. \n
///    This intrinsic returns the value of the CF flag.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VTESTPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __b
///    A 128-bit vector of [2 x double].
/// \returns the CF flag in the EFLAGS register.
static __inline int __DEFAULT_FN_ATTRS128
_mm_testc_pd(__m128d __a, __m128d __b)
{
  return __builtin_ia32_vtestcpd((__v2df)__a, (__v2df)__b);
}

/// Given two 128-bit floating-point vectors of [2 x double], perform an
///    element-by-element comparison of the double-precision element in the
///    first source vector and the corresponding element in the second source
///    vector.
///
///    The EFLAGS register is updated as follows: \n
///    If there is at least one pair of double-precision elements where the
///    sign-bits of both elements are 1, the ZF flag is set to 0. Otherwise the
///    ZF flag is set to 1. \n
///    If there is at least one pair of double-precision elements where the
///    sign-bit of the first element is 0 and the sign-bit of the second element
///    is 1, the CF flag is set to 0. Otherwise the CF flag is set to 1. \n
///    This intrinsic returns 1 if both the ZF and CF flags are set to 0,
///    otherwise it returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VTESTPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \param __b
///    A 128-bit vector of [2 x double].
/// \returns 1 if both the ZF and CF flags are set to 0, otherwise returns 0.
static __inline int __DEFAULT_FN_ATTRS128
_mm_testnzc_pd(__m128d __a, __m128d __b)
{
  return __builtin_ia32_vtestnzcpd((__v2df)__a, (__v2df)__b);
}

/// Given two 128-bit floating-point vectors of [4 x float], perform an
///    element-by-element comparison of the single-precision element in the
///    first source vector and the corresponding element in the second source
///    vector.
///
///    The EFLAGS register is updated as follows: \n
///    If there is at least one pair of single-precision elements where the
///    sign-bits of both elements are 1, the ZF flag is set to 0. Otherwise the
///    ZF flag is set to 1. \n
///    If there is at least one pair of single-precision elements where the
///    sign-bit of the first element is 0 and the sign-bit of the second element
///    is 1, the CF flag is set to 0. Otherwise the CF flag is set to 1. \n
///    This intrinsic returns the value of the ZF flag.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VTESTPS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 128-bit vector of [4 x float].
/// \returns the ZF flag.
static __inline int __DEFAULT_FN_ATTRS128
_mm_testz_ps(__m128 __a, __m128 __b)
{
  return __builtin_ia32_vtestzps((__v4sf)__a, (__v4sf)__b);
}

/// Given two 128-bit floating-point vectors of [4 x float], perform an
///    element-by-element comparison of the single-precision element in the
///    first source vector and the corresponding element in the second source
///    vector.
///
///    The EFLAGS register is updated as follows: \n
///    If there is at least one pair of single-precision elements where the
///    sign-bits of both elements are 1, the ZF flag is set to 0. Otherwise the
///    ZF flag is set to 1. \n
///    If there is at least one pair of single-precision elements where the
///    sign-bit of the first element is 0 and the sign-bit of the second element
///    is 1, the CF flag is set to 0. Otherwise the CF flag is set to 1. \n
///    This intrinsic returns the value of the CF flag.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VTESTPS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 128-bit vector of [4 x float].
/// \returns the CF flag.
static __inline int __DEFAULT_FN_ATTRS128
_mm_testc_ps(__m128 __a, __m128 __b)
{
  return __builtin_ia32_vtestcps((__v4sf)__a, (__v4sf)__b);
}

/// Given two 128-bit floating-point vectors of [4 x float], perform an
///    element-by-element comparison of the single-precision element in the
///    first source vector and the corresponding element in the second source
///    vector.
///
///    The EFLAGS register is updated as follows: \n
///    If there is at least one pair of single-precision elements where the
///    sign-bits of both elements are 1, the ZF flag is set to 0. Otherwise the
///    ZF flag is set to 1. \n
///    If there is at least one pair of single-precision elements where the
///    sign-bit of the first element is 0 and the sign-bit of the second element
///    is 1, the CF flag is set to 0. Otherwise the CF flag is set to 1. \n
///    This intrinsic returns 1 if both the ZF and CF flags are set to 0,
///    otherwise it returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VTESTPS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \param __b
///    A 128-bit vector of [4 x float].
/// \returns 1 if both the ZF and CF flags are set to 0, otherwise returns 0.
static __inline int __DEFAULT_FN_ATTRS128
_mm_testnzc_ps(__m128 __a, __m128 __b)
{
  return __builtin_ia32_vtestnzcps((__v4sf)__a, (__v4sf)__b);
}

/// Given two 256-bit floating-point vectors of [4 x double], perform an
///    element-by-element comparison of the double-precision elements in the
///    first source vector and the corresponding elements in the second source
///    vector.
///
///    The EFLAGS register is updated as follows: \n
///    If there is at least one pair of double-precision elements where the
///    sign-bits of both elements are 1, the ZF flag is set to 0. Otherwise the
///    ZF flag is set to 1. \n
///    If there is at least one pair of double-precision elements where the
///    sign-bit of the first element is 0 and the sign-bit of the second element
///    is 1, the CF flag is set to 0. Otherwise the CF flag is set to 1. \n
///    This intrinsic returns the value of the ZF flag.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VTESTPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double].
/// \param __b
///    A 256-bit vector of [4 x double].
/// \returns the ZF flag.
static __inline int __DEFAULT_FN_ATTRS
_mm256_testz_pd(__m256d __a, __m256d __b)
{
  return __builtin_ia32_vtestzpd256((__v4df)__a, (__v4df)__b);
}

/// Given two 256-bit floating-point vectors of [4 x double], perform an
///    element-by-element comparison of the double-precision elements in the
///    first source vector and the corresponding elements in the second source
///    vector.
///
///    The EFLAGS register is updated as follows: \n
///    If there is at least one pair of double-precision elements where the
///    sign-bits of both elements are 1, the ZF flag is set to 0. Otherwise the
///    ZF flag is set to 1. \n
///    If there is at least one pair of double-precision elements where the
///    sign-bit of the first element is 0 and the sign-bit of the second element
///    is 1, the CF flag is set to 0. Otherwise the CF flag is set to 1. \n
///    This intrinsic returns the value of the CF flag.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VTESTPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double].
/// \param __b
///    A 256-bit vector of [4 x double].
/// \returns the CF flag.
static __inline int __DEFAULT_FN_ATTRS
_mm256_testc_pd(__m256d __a, __m256d __b)
{
  return __builtin_ia32_vtestcpd256((__v4df)__a, (__v4df)__b);
}

/// Given two 256-bit floating-point vectors of [4 x double], perform an
///    element-by-element comparison of the double-precision elements in the
///    first source vector and the corresponding elements in the second source
///    vector.
///
///    The EFLAGS register is updated as follows: \n
///    If there is at least one pair of double-precision elements where the
///    sign-bits of both elements are 1, the ZF flag is set to 0. Otherwise the
///    ZF flag is set to 1. \n
///    If there is at least one pair of double-precision elements where the
///    sign-bit of the first element is 0 and the sign-bit of the second element
///    is 1, the CF flag is set to 0. Otherwise the CF flag is set to 1. \n
///    This intrinsic returns 1 if both the ZF and CF flags are set to 0,
///    otherwise it returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VTESTPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double].
/// \param __b
///    A 256-bit vector of [4 x double].
/// \returns 1 if both the ZF and CF flags are set to 0, otherwise returns 0.
static __inline int __DEFAULT_FN_ATTRS
_mm256_testnzc_pd(__m256d __a, __m256d __b)
{
  return __builtin_ia32_vtestnzcpd256((__v4df)__a, (__v4df)__b);
}

/// Given two 256-bit floating-point vectors of [8 x float], perform an
///    element-by-element comparison of the single-precision element in the
///    first source vector and the corresponding element in the second source
///    vector.
///
///    The EFLAGS register is updated as follows: \n
///    If there is at least one pair of single-precision elements where the
///    sign-bits of both elements are 1, the ZF flag is set to 0. Otherwise the
///    ZF flag is set to 1. \n
///    If there is at least one pair of single-precision elements where the
///    sign-bit of the first element is 0 and the sign-bit of the second element
///    is 1, the CF flag is set to 0. Otherwise the CF flag is set to 1. \n
///    This intrinsic returns the value of the ZF flag.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VTESTPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float].
/// \param __b
///    A 256-bit vector of [8 x float].
/// \returns the ZF flag.
static __inline int __DEFAULT_FN_ATTRS
_mm256_testz_ps(__m256 __a, __m256 __b)
{
  return __builtin_ia32_vtestzps256((__v8sf)__a, (__v8sf)__b);
}

/// Given two 256-bit floating-point vectors of [8 x float], perform an
///    element-by-element comparison of the single-precision element in the
///    first source vector and the corresponding element in the second source
///    vector.
///
///    The EFLAGS register is updated as follows: \n
///    If there is at least one pair of single-precision elements where the
///    sign-bits of both elements are 1, the ZF flag is set to 0. Otherwise the
///    ZF flag is set to 1. \n
///    If there is at least one pair of single-precision elements where the
///    sign-bit of the first element is 0 and the sign-bit of the second element
///    is 1, the CF flag is set to 0. Otherwise the CF flag is set to 1. \n
///    This intrinsic returns the value of the CF flag.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VTESTPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float].
/// \param __b
///    A 256-bit vector of [8 x float].
/// \returns the CF flag.
static __inline int __DEFAULT_FN_ATTRS
_mm256_testc_ps(__m256 __a, __m256 __b)
{
  return __builtin_ia32_vtestcps256((__v8sf)__a, (__v8sf)__b);
}

/// Given two 256-bit floating-point vectors of [8 x float], perform an
///    element-by-element comparison of the single-precision elements in the
///    first source vector and the corresponding elements in the second source
///    vector.
///
///    The EFLAGS register is updated as follows: \n
///    If there is at least one pair of single-precision elements where the
///    sign-bits of both elements are 1, the ZF flag is set to 0. Otherwise the
///    ZF flag is set to 1. \n
///    If there is at least one pair of single-precision elements where the
///    sign-bit of the first element is 0 and the sign-bit of the second element
///    is 1, the CF flag is set to 0. Otherwise the CF flag is set to 1. \n
///    This intrinsic returns 1 if both the ZF and CF flags are set to 0,
///    otherwise it returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VTESTPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float].
/// \param __b
///    A 256-bit vector of [8 x float].
/// \returns 1 if both the ZF and CF flags are set to 0, otherwise returns 0.
static __inline int __DEFAULT_FN_ATTRS
_mm256_testnzc_ps(__m256 __a, __m256 __b)
{
  return __builtin_ia32_vtestnzcps256((__v8sf)__a, (__v8sf)__b);
}

/// Given two 256-bit integer vectors, perform a bit-by-bit comparison
///    of the two source vectors.
///
///    The EFLAGS register is updated as follows: \n
///    If there is at least one pair of bits where both bits are 1, the ZF flag
///    is set to 0. Otherwise the ZF flag is set to 1. \n
///    If there is at least one pair of bits where the bit from the first source
///    vector is 0 and the bit from the second source vector is 1, the CF flag
///    is set to 0. Otherwise the CF flag is set to 1. \n
///    This intrinsic returns the value of the ZF flag.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPTEST </c> instruction.
///
/// \param __a
///    A 256-bit integer vector.
/// \param __b
///    A 256-bit integer vector.
/// \returns the ZF flag.
static __inline int __DEFAULT_FN_ATTRS
_mm256_testz_si256(__m256i __a, __m256i __b)
{
  return __builtin_ia32_ptestz256((__v4di)__a, (__v4di)__b);
}

/// Given two 256-bit integer vectors, perform a bit-by-bit comparison
///    of the two source vectors.
///
///    The EFLAGS register is updated as follows: \n
///    If there is at least one pair of bits where both bits are 1, the ZF flag
///    is set to 0. Otherwise the ZF flag is set to 1. \n
///    If there is at least one pair of bits where the bit from the first source
///    vector is 0 and the bit from the second source vector is 1, the CF flag
///    is set to 0. Otherwise the CF flag is set to 1. \n
///    This intrinsic returns the value of the CF flag.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPTEST </c> instruction.
///
/// \param __a
///    A 256-bit integer vector.
/// \param __b
///    A 256-bit integer vector.
/// \returns the CF flag.
static __inline int __DEFAULT_FN_ATTRS
_mm256_testc_si256(__m256i __a, __m256i __b)
{
  return __builtin_ia32_ptestc256((__v4di)__a, (__v4di)__b);
}

/// Given two 256-bit integer vectors, perform a bit-by-bit comparison
///    of the two source vectors.
///
///    The EFLAGS register is updated as follows: \n
///    If there is at least one pair of bits where both bits are 1, the ZF flag
///    is set to 0. Otherwise the ZF flag is set to 1. \n
///    If there is at least one pair of bits where the bit from the first source
///    vector is 0 and the bit from the second source vector is 1, the CF flag
///    is set to 0. Otherwise the CF flag is set to 1. \n
///    This intrinsic returns 1 if both the ZF and CF flags are set to 0,
///    otherwise it returns 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPTEST </c> instruction.
///
/// \param __a
///    A 256-bit integer vector.
/// \param __b
///    A 256-bit integer vector.
/// \returns 1 if both the ZF and CF flags are set to 0, otherwise returns 0.
static __inline int __DEFAULT_FN_ATTRS
_mm256_testnzc_si256(__m256i __a, __m256i __b)
{
  return __builtin_ia32_ptestnzc256((__v4di)__a, (__v4di)__b);
}

/* Vector extract sign mask */
/// Extracts the sign bits of double-precision floating point elements
///    in a 256-bit vector of [4 x double] and writes them to the lower order
///    bits of the return value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVMSKPD </c> instruction.
///
/// \param __a
///    A 256-bit vector of [4 x double] containing the double-precision
///    floating point values with sign bits to be extracted.
/// \returns The sign bits from the operand, written to bits [3:0].
static __inline int __DEFAULT_FN_ATTRS
_mm256_movemask_pd(__m256d __a)
{
  return __builtin_ia32_movmskpd256((__v4df)__a);
}

/// Extracts the sign bits of single-precision floating point elements
///    in a 256-bit vector of [8 x float] and writes them to the lower order
///    bits of the return value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVMSKPS </c> instruction.
///
/// \param __a
///    A 256-bit vector of [8 x float] containing the single-precision floating
///    point values with sign bits to be extracted.
/// \returns The sign bits from the operand, written to bits [7:0].
static __inline int __DEFAULT_FN_ATTRS
_mm256_movemask_ps(__m256 __a)
{
  return __builtin_ia32_movmskps256((__v8sf)__a);
}

/* Vector __zero */
/// Zeroes the contents of all XMM or YMM registers.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VZEROALL </c> instruction.
static __inline void __attribute__((__always_inline__, __nodebug__, __target__("avx")))
_mm256_zeroall(void)
{
  __builtin_ia32_vzeroall();
}

/// Zeroes the upper 128 bits (bits 255:128) of all YMM registers.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VZEROUPPER </c> instruction.
static __inline void __attribute__((__always_inline__, __nodebug__, __target__("avx")))
_mm256_zeroupper(void)
{
  __builtin_ia32_vzeroupper();
}

/* Vector load with broadcast */
/// Loads a scalar single-precision floating point value from the
///    specified address pointed to by \a __a and broadcasts it to the elements
///    of a [4 x float] vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VBROADCASTSS </c> instruction.
///
/// \param __a
///    The single-precision floating point value to be broadcast.
/// \returns A 128-bit vector of [4 x float] whose 32-bit elements are set
///    equal to the broadcast value.
static __inline __m128 __DEFAULT_FN_ATTRS128
_mm_broadcast_ss(float const *__a)
{
  float __f = *__a;
  return __extension__ (__m128)(__v4sf){ __f, __f, __f, __f };
}

/// Loads a scalar double-precision floating point value from the
///    specified address pointed to by \a __a and broadcasts it to the elements
///    of a [4 x double] vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VBROADCASTSD </c> instruction.
///
/// \param __a
///    The double-precision floating point value to be broadcast.
/// \returns A 256-bit vector of [4 x double] whose 64-bit elements are set
///    equal to the broadcast value.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_broadcast_sd(double const *__a)
{
  double __d = *__a;
  return __extension__ (__m256d)(__v4df){ __d, __d, __d, __d };
}

/// Loads a scalar single-precision floating point value from the
///    specified address pointed to by \a __a and broadcasts it to the elements
///    of a [8 x float] vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VBROADCASTSS </c> instruction.
///
/// \param __a
///    The single-precision floating point value to be broadcast.
/// \returns A 256-bit vector of [8 x float] whose 32-bit elements are set
///    equal to the broadcast value.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_broadcast_ss(float const *__a)
{
  float __f = *__a;
  return __extension__ (__m256)(__v8sf){ __f, __f, __f, __f, __f, __f, __f, __f };
}

/// Loads the data from a 128-bit vector of [2 x double] from the
///    specified address pointed to by \a __a and broadcasts it to 128-bit
///    elements in a 256-bit vector of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VBROADCASTF128 </c> instruction.
///
/// \param __a
///    The 128-bit vector of [2 x double] to be broadcast.
/// \returns A 256-bit vector of [4 x double] whose 128-bit elements are set
///    equal to the broadcast value.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_broadcast_pd(__m128d const *__a)
{
  __m128d __b = _mm_loadu_pd((const double *)__a);
  return (__m256d)__builtin_shufflevector((__v2df)__b, (__v2df)__b,
                                          0, 1, 0, 1);
}

/// Loads the data from a 128-bit vector of [4 x float] from the
///    specified address pointed to by \a __a and broadcasts it to 128-bit
///    elements in a 256-bit vector of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VBROADCASTF128 </c> instruction.
///
/// \param __a
///    The 128-bit vector of [4 x float] to be broadcast.
/// \returns A 256-bit vector of [8 x float] whose 128-bit elements are set
///    equal to the broadcast value.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_broadcast_ps(__m128 const *__a)
{
  __m128 __b = _mm_loadu_ps((const float *)__a);
  return (__m256)__builtin_shufflevector((__v4sf)__b, (__v4sf)__b,
                                         0, 1, 2, 3, 0, 1, 2, 3);
}

/* SIMD load ops */
/// Loads 4 double-precision floating point values from a 32-byte aligned
///    memory location pointed to by \a __p into a vector of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVAPD </c> instruction.
///
/// \param __p
///    A 32-byte aligned pointer to a memory location containing
///    double-precision floating point values.
/// \returns A 256-bit vector of [4 x double] containing the moved values.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_load_pd(double const *__p)
{
  return *(__m256d *)__p;
}

/// Loads 8 single-precision floating point values from a 32-byte aligned
///    memory location pointed to by \a __p into a vector of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVAPS </c> instruction.
///
/// \param __p
///    A 32-byte aligned pointer to a memory location containing float values.
/// \returns A 256-bit vector of [8 x float] containing the moved values.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_load_ps(float const *__p)
{
  return *(__m256 *)__p;
}

/// Loads 4 double-precision floating point values from an unaligned
///    memory location pointed to by \a __p into a vector of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVUPD </c> instruction.
///
/// \param __p
///    A pointer to a memory location containing double-precision floating
///    point values.
/// \returns A 256-bit vector of [4 x double] containing the moved values.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_loadu_pd(double const *__p)
{
  struct __loadu_pd {
    __m256d __v;
  } __attribute__((__packed__, __may_alias__));
  return ((struct __loadu_pd*)__p)->__v;
}

/// Loads 8 single-precision floating point values from an unaligned
///    memory location pointed to by \a __p into a vector of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVUPS </c> instruction.
///
/// \param __p
///    A pointer to a memory location containing single-precision floating
///    point values.
/// \returns A 256-bit vector of [8 x float] containing the moved values.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_loadu_ps(float const *__p)
{
  struct __loadu_ps {
    __m256 __v;
  } __attribute__((__packed__, __may_alias__));
  return ((struct __loadu_ps*)__p)->__v;
}

/// Loads 256 bits of integer data from a 32-byte aligned memory
///    location pointed to by \a __p into elements of a 256-bit integer vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVDQA </c> instruction.
///
/// \param __p
///    A 32-byte aligned pointer to a 256-bit integer vector containing integer
///    values.
/// \returns A 256-bit integer vector containing the moved values.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_load_si256(__m256i const *__p)
{
  return *__p;
}

/// Loads 256 bits of integer data from an unaligned memory location
///    pointed to by \a __p into a 256-bit integer vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVDQU </c> instruction.
///
/// \param __p
///    A pointer to a 256-bit integer vector containing integer values.
/// \returns A 256-bit integer vector containing the moved values.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_loadu_si256(__m256i const *__p)
{
  struct __loadu_si256 {
    __m256i __v;
  } __attribute__((__packed__, __may_alias__));
  return ((struct __loadu_si256*)__p)->__v;
}

/// Loads 256 bits of integer data from an unaligned memory location
///    pointed to by \a __p into a 256-bit integer vector. This intrinsic may
///    perform better than \c _mm256_loadu_si256 when the data crosses a cache
///    line boundary.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VLDDQU </c> instruction.
///
/// \param __p
///    A pointer to a 256-bit integer vector containing integer values.
/// \returns A 256-bit integer vector containing the moved values.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_lddqu_si256(__m256i const *__p)
{
  return (__m256i)__builtin_ia32_lddqu256((char const *)__p);
}

/* SIMD store ops */
/// Stores double-precision floating point values from a 256-bit vector
///    of [4 x double] to a 32-byte aligned memory location pointed to by
///    \a __p.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVAPD </c> instruction.
///
/// \param __p
///    A 32-byte aligned pointer to a memory location that will receive the
///    double-precision floaing point values.
/// \param __a
///    A 256-bit vector of [4 x double] containing the values to be moved.
static __inline void __DEFAULT_FN_ATTRS
_mm256_store_pd(double *__p, __m256d __a)
{
  *(__m256d *)__p = __a;
}

/// Stores single-precision floating point values from a 256-bit vector
///    of [8 x float] to a 32-byte aligned memory location pointed to by \a __p.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVAPS </c> instruction.
///
/// \param __p
///    A 32-byte aligned pointer to a memory location that will receive the
///    float values.
/// \param __a
///    A 256-bit vector of [8 x float] containing the values to be moved.
static __inline void __DEFAULT_FN_ATTRS
_mm256_store_ps(float *__p, __m256 __a)
{
  *(__m256 *)__p = __a;
}

/// Stores double-precision floating point values from a 256-bit vector
///    of [4 x double] to an unaligned memory location pointed to by \a __p.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVUPD </c> instruction.
///
/// \param __p
///    A pointer to a memory location that will receive the double-precision
///    floating point values.
/// \param __a
///    A 256-bit vector of [4 x double] containing the values to be moved.
static __inline void __DEFAULT_FN_ATTRS
_mm256_storeu_pd(double *__p, __m256d __a)
{
  struct __storeu_pd {
    __m256d __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_pd*)__p)->__v = __a;
}

/// Stores single-precision floating point values from a 256-bit vector
///    of [8 x float] to an unaligned memory location pointed to by \a __p.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVUPS </c> instruction.
///
/// \param __p
///    A pointer to a memory location that will receive the float values.
/// \param __a
///    A 256-bit vector of [8 x float] containing the values to be moved.
static __inline void __DEFAULT_FN_ATTRS
_mm256_storeu_ps(float *__p, __m256 __a)
{
  struct __storeu_ps {
    __m256 __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_ps*)__p)->__v = __a;
}

/// Stores integer values from a 256-bit integer vector to a 32-byte
///    aligned memory location pointed to by \a __p.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVDQA </c> instruction.
///
/// \param __p
///    A 32-byte aligned pointer to a memory location that will receive the
///    integer values.
/// \param __a
///    A 256-bit integer vector containing the values to be moved.
static __inline void __DEFAULT_FN_ATTRS
_mm256_store_si256(__m256i *__p, __m256i __a)
{
  *__p = __a;
}

/// Stores integer values from a 256-bit integer vector to an unaligned
///    memory location pointed to by \a __p.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVDQU </c> instruction.
///
/// \param __p
///    A pointer to a memory location that will receive the integer values.
/// \param __a
///    A 256-bit integer vector containing the values to be moved.
static __inline void __DEFAULT_FN_ATTRS
_mm256_storeu_si256(__m256i *__p, __m256i __a)
{
  struct __storeu_si256 {
    __m256i __v;
  } __attribute__((__packed__, __may_alias__));
  ((struct __storeu_si256*)__p)->__v = __a;
}

/* Conditional load ops */
/// Conditionally loads double-precision floating point elements from a
///    memory location pointed to by \a __p into a 128-bit vector of
///    [2 x double], depending on the mask bits associated with each data
///    element.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMASKMOVPD </c> instruction.
///
/// \param __p
///    A pointer to a memory location that contains the double-precision
///    floating point values.
/// \param __m
///    A 128-bit integer vector containing the mask. The most significant bit of
///    each data element represents the mask bits. If a mask bit is zero, the
///    corresponding value in the memory location is not loaded and the
///    corresponding field in the return value is set to zero.
/// \returns A 128-bit vector of [2 x double] containing the loaded values.
static __inline __m128d __DEFAULT_FN_ATTRS128
_mm_maskload_pd(double const *__p, __m128i __m)
{
  return (__m128d)__builtin_ia32_maskloadpd((const __v2df *)__p, (__v2di)__m);
}

/// Conditionally loads double-precision floating point elements from a
///    memory location pointed to by \a __p into a 256-bit vector of
///    [4 x double], depending on the mask bits associated with each data
///    element.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMASKMOVPD </c> instruction.
///
/// \param __p
///    A pointer to a memory location that contains the double-precision
///    floating point values.
/// \param __m
///    A 256-bit integer vector of [4 x quadword] containing the mask. The most
///    significant bit of each quadword element represents the mask bits. If a
///    mask bit is zero, the corresponding value in the memory location is not
///    loaded and the corresponding field in the return value is set to zero.
/// \returns A 256-bit vector of [4 x double] containing the loaded values.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_maskload_pd(double const *__p, __m256i __m)
{
  return (__m256d)__builtin_ia32_maskloadpd256((const __v4df *)__p,
                                               (__v4di)__m);
}

/// Conditionally loads single-precision floating point elements from a
///    memory location pointed to by \a __p into a 128-bit vector of
///    [4 x float], depending on the mask bits associated with each data
///    element.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMASKMOVPS </c> instruction.
///
/// \param __p
///    A pointer to a memory location that contains the single-precision
///    floating point values.
/// \param __m
///    A 128-bit integer vector containing the mask. The most significant bit of
///    each data element represents the mask bits. If a mask bit is zero, the
///    corresponding value in the memory location is not loaded and the
///    corresponding field in the return value is set to zero.
/// \returns A 128-bit vector of [4 x float] containing the loaded values.
static __inline __m128 __DEFAULT_FN_ATTRS128
_mm_maskload_ps(float const *__p, __m128i __m)
{
  return (__m128)__builtin_ia32_maskloadps((const __v4sf *)__p, (__v4si)__m);
}

/// Conditionally loads single-precision floating point elements from a
///    memory location pointed to by \a __p into a 256-bit vector of
///    [8 x float], depending on the mask bits associated with each data
///    element.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMASKMOVPS </c> instruction.
///
/// \param __p
///    A pointer to a memory location that contains the single-precision
///    floating point values.
/// \param __m
///    A 256-bit integer vector of [8 x dword] containing the mask. The most
///    significant bit of each dword element represents the mask bits. If a mask
///    bit is zero, the corresponding value in the memory location is not loaded
///    and the corresponding field in the return value is set to zero.
/// \returns A 256-bit vector of [8 x float] containing the loaded values.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_maskload_ps(float const *__p, __m256i __m)
{
  return (__m256)__builtin_ia32_maskloadps256((const __v8sf *)__p, (__v8si)__m);
}

/* Conditional store ops */
/// Moves single-precision floating point values from a 256-bit vector
///    of [8 x float] to a memory location pointed to by \a __p, according to
///    the specified mask.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMASKMOVPS </c> instruction.
///
/// \param __p
///    A pointer to a memory location that will receive the float values.
/// \param __m
///    A 256-bit integer vector of [8 x dword] containing the mask. The most
///    significant bit of each dword element in the mask vector represents the
///    mask bits. If a mask bit is zero, the corresponding value from vector
///    \a __a is not stored and the corresponding field in the memory location
///    pointed to by \a __p is not changed.
/// \param __a
///    A 256-bit vector of [8 x float] containing the values to be stored.
static __inline void __DEFAULT_FN_ATTRS
_mm256_maskstore_ps(float *__p, __m256i __m, __m256 __a)
{
  __builtin_ia32_maskstoreps256((__v8sf *)__p, (__v8si)__m, (__v8sf)__a);
}

/// Moves double-precision values from a 128-bit vector of [2 x double]
///    to a memory location pointed to by \a __p, according to the specified
///    mask.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMASKMOVPD </c> instruction.
///
/// \param __p
///    A pointer to a memory location that will receive the float values.
/// \param __m
///    A 128-bit integer vector containing the mask. The most significant bit of
///    each field in the mask vector represents the mask bits. If a mask bit is
///    zero, the corresponding value from vector \a __a is not stored and the
///    corresponding field in the memory location pointed to by \a __p is not
///    changed.
/// \param __a
///    A 128-bit vector of [2 x double] containing the values to be stored.
static __inline void __DEFAULT_FN_ATTRS128
_mm_maskstore_pd(double *__p, __m128i __m, __m128d __a)
{
  __builtin_ia32_maskstorepd((__v2df *)__p, (__v2di)__m, (__v2df)__a);
}

/// Moves double-precision values from a 256-bit vector of [4 x double]
///    to a memory location pointed to by \a __p, according to the specified
///    mask.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMASKMOVPD </c> instruction.
///
/// \param __p
///    A pointer to a memory location that will receive the float values.
/// \param __m
///    A 256-bit integer vector of [4 x quadword] containing the mask. The most
///    significant bit of each quadword element in the mask vector represents
///    the mask bits. If a mask bit is zero, the corresponding value from vector
///    __a is not stored and the corresponding field in the memory location
///    pointed to by \a __p is not changed.
/// \param __a
///    A 256-bit vector of [4 x double] containing the values to be stored.
static __inline void __DEFAULT_FN_ATTRS
_mm256_maskstore_pd(double *__p, __m256i __m, __m256d __a)
{
  __builtin_ia32_maskstorepd256((__v4df *)__p, (__v4di)__m, (__v4df)__a);
}

/// Moves single-precision floating point values from a 128-bit vector
///    of [4 x float] to a memory location pointed to by \a __p, according to
///    the specified mask.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMASKMOVPS </c> instruction.
///
/// \param __p
///    A pointer to a memory location that will receive the float values.
/// \param __m
///    A 128-bit integer vector containing the mask. The most significant bit of
///    each field in the mask vector represents the mask bits. If a mask bit is
///    zero, the corresponding value from vector __a is not stored and the
///    corresponding field in the memory location pointed to by \a __p is not
///    changed.
/// \param __a
///    A 128-bit vector of [4 x float] containing the values to be stored.
static __inline void __DEFAULT_FN_ATTRS128
_mm_maskstore_ps(float *__p, __m128i __m, __m128 __a)
{
  __builtin_ia32_maskstoreps((__v4sf *)__p, (__v4si)__m, (__v4sf)__a);
}

/* Cacheability support ops */
/// Moves integer data from a 256-bit integer vector to a 32-byte
///    aligned memory location. To minimize caching, the data is flagged as
///    non-temporal (unlikely to be used again soon).
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVNTDQ </c> instruction.
///
/// \param __a
///    A pointer to a 32-byte aligned memory location that will receive the
///    integer values.
/// \param __b
///    A 256-bit integer vector containing the values to be moved.
static __inline void __DEFAULT_FN_ATTRS
_mm256_stream_si256(__m256i *__a, __m256i __b)
{
  typedef __v4di __v4di_aligned __attribute__((aligned(32)));
  __builtin_nontemporal_store((__v4di_aligned)__b, (__v4di_aligned*)__a);
}

/// Moves double-precision values from a 256-bit vector of [4 x double]
///    to a 32-byte aligned memory location. To minimize caching, the data is
///    flagged as non-temporal (unlikely to be used again soon).
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVNTPD </c> instruction.
///
/// \param __a
///    A pointer to a 32-byte aligned memory location that will receive the
///    double-precision floating-point values.
/// \param __b
///    A 256-bit vector of [4 x double] containing the values to be moved.
static __inline void __DEFAULT_FN_ATTRS
_mm256_stream_pd(double *__a, __m256d __b)
{
  typedef __v4df __v4df_aligned __attribute__((aligned(32)));
  __builtin_nontemporal_store((__v4df_aligned)__b, (__v4df_aligned*)__a);
}

/// Moves single-precision floating point values from a 256-bit vector
///    of [8 x float] to a 32-byte aligned memory location. To minimize
///    caching, the data is flagged as non-temporal (unlikely to be used again
///    soon).
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVNTPS </c> instruction.
///
/// \param __p
///    A pointer to a 32-byte aligned memory location that will receive the
///    single-precision floating point values.
/// \param __a
///    A 256-bit vector of [8 x float] containing the values to be moved.
static __inline void __DEFAULT_FN_ATTRS
_mm256_stream_ps(float *__p, __m256 __a)
{
  typedef __v8sf __v8sf_aligned __attribute__((aligned(32)));
  __builtin_nontemporal_store((__v8sf_aligned)__a, (__v8sf_aligned*)__p);
}

/* Create vectors */
/// Create a 256-bit vector of [4 x double] with undefined values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \returns A 256-bit vector of [4 x double] containing undefined values.
static __inline__ __m256d __DEFAULT_FN_ATTRS
_mm256_undefined_pd(void)
{
  return (__m256d)__builtin_ia32_undef256();
}

/// Create a 256-bit vector of [8 x float] with undefined values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \returns A 256-bit vector of [8 x float] containing undefined values.
static __inline__ __m256 __DEFAULT_FN_ATTRS
_mm256_undefined_ps(void)
{
  return (__m256)__builtin_ia32_undef256();
}

/// Create a 256-bit integer vector with undefined values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \returns A 256-bit integer vector containing undefined values.
static __inline__ __m256i __DEFAULT_FN_ATTRS
_mm256_undefined_si256(void)
{
  return (__m256i)__builtin_ia32_undef256();
}

/// Constructs a 256-bit floating-point vector of [4 x double]
///    initialized with the specified double-precision floating-point values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUNPCKLPD+VINSERTF128 </c>
///   instruction.
///
/// \param __a
///    A double-precision floating-point value used to initialize bits [255:192]
///    of the result.
/// \param __b
///    A double-precision floating-point value used to initialize bits [191:128]
///    of the result.
/// \param __c
///    A double-precision floating-point value used to initialize bits [127:64]
///    of the result.
/// \param __d
///    A double-precision floating-point value used to initialize bits [63:0]
///    of the result.
/// \returns An initialized 256-bit floating-point vector of [4 x double].
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_set_pd(double __a, double __b, double __c, double __d)
{
  return __extension__ (__m256d){ __d, __c, __b, __a };
}

/// Constructs a 256-bit floating-point vector of [8 x float] initialized
///    with the specified single-precision floating-point values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///   instruction.
///
/// \param __a
///    A single-precision floating-point value used to initialize bits [255:224]
///    of the result.
/// \param __b
///    A single-precision floating-point value used to initialize bits [223:192]
///    of the result.
/// \param __c
///    A single-precision floating-point value used to initialize bits [191:160]
///    of the result.
/// \param __d
///    A single-precision floating-point value used to initialize bits [159:128]
///    of the result.
/// \param __e
///    A single-precision floating-point value used to initialize bits [127:96]
///    of the result.
/// \param __f
///    A single-precision floating-point value used to initialize bits [95:64]
///    of the result.
/// \param __g
///    A single-precision floating-point value used to initialize bits [63:32]
///    of the result.
/// \param __h
///    A single-precision floating-point value used to initialize bits [31:0]
///    of the result.
/// \returns An initialized 256-bit floating-point vector of [8 x float].
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_set_ps(float __a, float __b, float __c, float __d,
              float __e, float __f, float __g, float __h)
{
  return __extension__ (__m256){ __h, __g, __f, __e, __d, __c, __b, __a };
}

/// Constructs a 256-bit integer vector initialized with the specified
///    32-bit integral values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///   instruction.
///
/// \param __i0
///    A 32-bit integral value used to initialize bits [255:224] of the result.
/// \param __i1
///    A 32-bit integral value used to initialize bits [223:192] of the result.
/// \param __i2
///    A 32-bit integral value used to initialize bits [191:160] of the result.
/// \param __i3
///    A 32-bit integral value used to initialize bits [159:128] of the result.
/// \param __i4
///    A 32-bit integral value used to initialize bits [127:96] of the result.
/// \param __i5
///    A 32-bit integral value used to initialize bits [95:64] of the result.
/// \param __i6
///    A 32-bit integral value used to initialize bits [63:32] of the result.
/// \param __i7
///    A 32-bit integral value used to initialize bits [31:0] of the result.
/// \returns An initialized 256-bit integer vector.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_set_epi32(int __i0, int __i1, int __i2, int __i3,
                 int __i4, int __i5, int __i6, int __i7)
{
  return __extension__ (__m256i)(__v8si){ __i7, __i6, __i5, __i4, __i3, __i2, __i1, __i0 };
}

/// Constructs a 256-bit integer vector initialized with the specified
///    16-bit integral values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///   instruction.
///
/// \param __w15
///    A 16-bit integral value used to initialize bits [255:240] of the result.
/// \param __w14
///    A 16-bit integral value used to initialize bits [239:224] of the result.
/// \param __w13
///    A 16-bit integral value used to initialize bits [223:208] of the result.
/// \param __w12
///    A 16-bit integral value used to initialize bits [207:192] of the result.
/// \param __w11
///    A 16-bit integral value used to initialize bits [191:176] of the result.
/// \param __w10
///    A 16-bit integral value used to initialize bits [175:160] of the result.
/// \param __w09
///    A 16-bit integral value used to initialize bits [159:144] of the result.
/// \param __w08
///    A 16-bit integral value used to initialize bits [143:128] of the result.
/// \param __w07
///    A 16-bit integral value used to initialize bits [127:112] of the result.
/// \param __w06
///    A 16-bit integral value used to initialize bits [111:96] of the result.
/// \param __w05
///    A 16-bit integral value used to initialize bits [95:80] of the result.
/// \param __w04
///    A 16-bit integral value used to initialize bits [79:64] of the result.
/// \param __w03
///    A 16-bit integral value used to initialize bits [63:48] of the result.
/// \param __w02
///    A 16-bit integral value used to initialize bits [47:32] of the result.
/// \param __w01
///    A 16-bit integral value used to initialize bits [31:16] of the result.
/// \param __w00
///    A 16-bit integral value used to initialize bits [15:0] of the result.
/// \returns An initialized 256-bit integer vector.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_set_epi16(short __w15, short __w14, short __w13, short __w12,
                 short __w11, short __w10, short __w09, short __w08,
                 short __w07, short __w06, short __w05, short __w04,
                 short __w03, short __w02, short __w01, short __w00)
{
  return __extension__ (__m256i)(__v16hi){ __w00, __w01, __w02, __w03, __w04, __w05, __w06,
    __w07, __w08, __w09, __w10, __w11, __w12, __w13, __w14, __w15 };
}

/// Constructs a 256-bit integer vector initialized with the specified
///    8-bit integral values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///   instruction.
///
/// \param __b31
///    An 8-bit integral value used to initialize bits [255:248] of the result.
/// \param __b30
///    An 8-bit integral value used to initialize bits [247:240] of the result.
/// \param __b29
///    An 8-bit integral value used to initialize bits [239:232] of the result.
/// \param __b28
///    An 8-bit integral value used to initialize bits [231:224] of the result.
/// \param __b27
///    An 8-bit integral value used to initialize bits [223:216] of the result.
/// \param __b26
///    An 8-bit integral value used to initialize bits [215:208] of the result.
/// \param __b25
///    An 8-bit integral value used to initialize bits [207:200] of the result.
/// \param __b24
///    An 8-bit integral value used to initialize bits [199:192] of the result.
/// \param __b23
///    An 8-bit integral value used to initialize bits [191:184] of the result.
/// \param __b22
///    An 8-bit integral value used to initialize bits [183:176] of the result.
/// \param __b21
///    An 8-bit integral value used to initialize bits [175:168] of the result.
/// \param __b20
///    An 8-bit integral value used to initialize bits [167:160] of the result.
/// \param __b19
///    An 8-bit integral value used to initialize bits [159:152] of the result.
/// \param __b18
///    An 8-bit integral value used to initialize bits [151:144] of the result.
/// \param __b17
///    An 8-bit integral value used to initialize bits [143:136] of the result.
/// \param __b16
///    An 8-bit integral value used to initialize bits [135:128] of the result.
/// \param __b15
///    An 8-bit integral value used to initialize bits [127:120] of the result.
/// \param __b14
///    An 8-bit integral value used to initialize bits [119:112] of the result.
/// \param __b13
///    An 8-bit integral value used to initialize bits [111:104] of the result.
/// \param __b12
///    An 8-bit integral value used to initialize bits [103:96] of the result.
/// \param __b11
///    An 8-bit integral value used to initialize bits [95:88] of the result.
/// \param __b10
///    An 8-bit integral value used to initialize bits [87:80] of the result.
/// \param __b09
///    An 8-bit integral value used to initialize bits [79:72] of the result.
/// \param __b08
///    An 8-bit integral value used to initialize bits [71:64] of the result.
/// \param __b07
///    An 8-bit integral value used to initialize bits [63:56] of the result.
/// \param __b06
///    An 8-bit integral value used to initialize bits [55:48] of the result.
/// \param __b05
///    An 8-bit integral value used to initialize bits [47:40] of the result.
/// \param __b04
///    An 8-bit integral value used to initialize bits [39:32] of the result.
/// \param __b03
///    An 8-bit integral value used to initialize bits [31:24] of the result.
/// \param __b02
///    An 8-bit integral value used to initialize bits [23:16] of the result.
/// \param __b01
///    An 8-bit integral value used to initialize bits [15:8] of the result.
/// \param __b00
///    An 8-bit integral value used to initialize bits [7:0] of the result.
/// \returns An initialized 256-bit integer vector.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_set_epi8(char __b31, char __b30, char __b29, char __b28,
                char __b27, char __b26, char __b25, char __b24,
                char __b23, char __b22, char __b21, char __b20,
                char __b19, char __b18, char __b17, char __b16,
                char __b15, char __b14, char __b13, char __b12,
                char __b11, char __b10, char __b09, char __b08,
                char __b07, char __b06, char __b05, char __b04,
                char __b03, char __b02, char __b01, char __b00)
{
  return __extension__ (__m256i)(__v32qi){
    __b00, __b01, __b02, __b03, __b04, __b05, __b06, __b07,
    __b08, __b09, __b10, __b11, __b12, __b13, __b14, __b15,
    __b16, __b17, __b18, __b19, __b20, __b21, __b22, __b23,
    __b24, __b25, __b26, __b27, __b28, __b29, __b30, __b31
  };
}

/// Constructs a 256-bit integer vector initialized with the specified
///    64-bit integral values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPUNPCKLQDQ+VINSERTF128 </c>
///   instruction.
///
/// \param __a
///    A 64-bit integral value used to initialize bits [255:192] of the result.
/// \param __b
///    A 64-bit integral value used to initialize bits [191:128] of the result.
/// \param __c
///    A 64-bit integral value used to initialize bits [127:64] of the result.
/// \param __d
///    A 64-bit integral value used to initialize bits [63:0] of the result.
/// \returns An initialized 256-bit integer vector.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_set_epi64x(long long __a, long long __b, long long __c, long long __d)
{
  return __extension__ (__m256i)(__v4di){ __d, __c, __b, __a };
}

/* Create vectors with elements in reverse order */
/// Constructs a 256-bit floating-point vector of [4 x double],
///    initialized in reverse order with the specified double-precision
///    floating-point values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VUNPCKLPD+VINSERTF128 </c>
///   instruction.
///
/// \param __a
///    A double-precision floating-point value used to initialize bits [63:0]
///    of the result.
/// \param __b
///    A double-precision floating-point value used to initialize bits [127:64]
///    of the result.
/// \param __c
///    A double-precision floating-point value used to initialize bits [191:128]
///    of the result.
/// \param __d
///    A double-precision floating-point value used to initialize bits [255:192]
///    of the result.
/// \returns An initialized 256-bit floating-point vector of [4 x double].
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_setr_pd(double __a, double __b, double __c, double __d)
{
  return _mm256_set_pd(__d, __c, __b, __a);
}

/// Constructs a 256-bit floating-point vector of [8 x float],
///    initialized in reverse order with the specified single-precision
///    float-point values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///   instruction.
///
/// \param __a
///    A single-precision floating-point value used to initialize bits [31:0]
///    of the result.
/// \param __b
///    A single-precision floating-point value used to initialize bits [63:32]
///    of the result.
/// \param __c
///    A single-precision floating-point value used to initialize bits [95:64]
///    of the result.
/// \param __d
///    A single-precision floating-point value used to initialize bits [127:96]
///    of the result.
/// \param __e
///    A single-precision floating-point value used to initialize bits [159:128]
///    of the result.
/// \param __f
///    A single-precision floating-point value used to initialize bits [191:160]
///    of the result.
/// \param __g
///    A single-precision floating-point value used to initialize bits [223:192]
///    of the result.
/// \param __h
///    A single-precision floating-point value used to initialize bits [255:224]
///    of the result.
/// \returns An initialized 256-bit floating-point vector of [8 x float].
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_setr_ps(float __a, float __b, float __c, float __d,
               float __e, float __f, float __g, float __h)
{
  return _mm256_set_ps(__h, __g, __f, __e, __d, __c, __b, __a);
}

/// Constructs a 256-bit integer vector, initialized in reverse order
///    with the specified 32-bit integral values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///   instruction.
///
/// \param __i0
///    A 32-bit integral value used to initialize bits [31:0] of the result.
/// \param __i1
///    A 32-bit integral value used to initialize bits [63:32] of the result.
/// \param __i2
///    A 32-bit integral value used to initialize bits [95:64] of the result.
/// \param __i3
///    A 32-bit integral value used to initialize bits [127:96] of the result.
/// \param __i4
///    A 32-bit integral value used to initialize bits [159:128] of the result.
/// \param __i5
///    A 32-bit integral value used to initialize bits [191:160] of the result.
/// \param __i6
///    A 32-bit integral value used to initialize bits [223:192] of the result.
/// \param __i7
///    A 32-bit integral value used to initialize bits [255:224] of the result.
/// \returns An initialized 256-bit integer vector.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_setr_epi32(int __i0, int __i1, int __i2, int __i3,
                  int __i4, int __i5, int __i6, int __i7)
{
  return _mm256_set_epi32(__i7, __i6, __i5, __i4, __i3, __i2, __i1, __i0);
}

/// Constructs a 256-bit integer vector, initialized in reverse order
///    with the specified 16-bit integral values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///   instruction.
///
/// \param __w15
///    A 16-bit integral value used to initialize bits [15:0] of the result.
/// \param __w14
///    A 16-bit integral value used to initialize bits [31:16] of the result.
/// \param __w13
///    A 16-bit integral value used to initialize bits [47:32] of the result.
/// \param __w12
///    A 16-bit integral value used to initialize bits [63:48] of the result.
/// \param __w11
///    A 16-bit integral value used to initialize bits [79:64] of the result.
/// \param __w10
///    A 16-bit integral value used to initialize bits [95:80] of the result.
/// \param __w09
///    A 16-bit integral value used to initialize bits [111:96] of the result.
/// \param __w08
///    A 16-bit integral value used to initialize bits [127:112] of the result.
/// \param __w07
///    A 16-bit integral value used to initialize bits [143:128] of the result.
/// \param __w06
///    A 16-bit integral value used to initialize bits [159:144] of the result.
/// \param __w05
///    A 16-bit integral value used to initialize bits [175:160] of the result.
/// \param __w04
///    A 16-bit integral value used to initialize bits [191:176] of the result.
/// \param __w03
///    A 16-bit integral value used to initialize bits [207:192] of the result.
/// \param __w02
///    A 16-bit integral value used to initialize bits [223:208] of the result.
/// \param __w01
///    A 16-bit integral value used to initialize bits [239:224] of the result.
/// \param __w00
///    A 16-bit integral value used to initialize bits [255:240] of the result.
/// \returns An initialized 256-bit integer vector.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_setr_epi16(short __w15, short __w14, short __w13, short __w12,
       short __w11, short __w10, short __w09, short __w08,
       short __w07, short __w06, short __w05, short __w04,
       short __w03, short __w02, short __w01, short __w00)
{
  return _mm256_set_epi16(__w00, __w01, __w02, __w03,
                          __w04, __w05, __w06, __w07,
                          __w08, __w09, __w10, __w11,
                          __w12, __w13, __w14, __w15);
}

/// Constructs a 256-bit integer vector, initialized in reverse order
///    with the specified 8-bit integral values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///   instruction.
///
/// \param __b31
///    An 8-bit integral value used to initialize bits [7:0] of the result.
/// \param __b30
///    An 8-bit integral value used to initialize bits [15:8] of the result.
/// \param __b29
///    An 8-bit integral value used to initialize bits [23:16] of the result.
/// \param __b28
///    An 8-bit integral value used to initialize bits [31:24] of the result.
/// \param __b27
///    An 8-bit integral value used to initialize bits [39:32] of the result.
/// \param __b26
///    An 8-bit integral value used to initialize bits [47:40] of the result.
/// \param __b25
///    An 8-bit integral value used to initialize bits [55:48] of the result.
/// \param __b24
///    An 8-bit integral value used to initialize bits [63:56] of the result.
/// \param __b23
///    An 8-bit integral value used to initialize bits [71:64] of the result.
/// \param __b22
///    An 8-bit integral value used to initialize bits [79:72] of the result.
/// \param __b21
///    An 8-bit integral value used to initialize bits [87:80] of the result.
/// \param __b20
///    An 8-bit integral value used to initialize bits [95:88] of the result.
/// \param __b19
///    An 8-bit integral value used to initialize bits [103:96] of the result.
/// \param __b18
///    An 8-bit integral value used to initialize bits [111:104] of the result.
/// \param __b17
///    An 8-bit integral value used to initialize bits [119:112] of the result.
/// \param __b16
///    An 8-bit integral value used to initialize bits [127:120] of the result.
/// \param __b15
///    An 8-bit integral value used to initialize bits [135:128] of the result.
/// \param __b14
///    An 8-bit integral value used to initialize bits [143:136] of the result.
/// \param __b13
///    An 8-bit integral value used to initialize bits [151:144] of the result.
/// \param __b12
///    An 8-bit integral value used to initialize bits [159:152] of the result.
/// \param __b11
///    An 8-bit integral value used to initialize bits [167:160] of the result.
/// \param __b10
///    An 8-bit integral value used to initialize bits [175:168] of the result.
/// \param __b09
///    An 8-bit integral value used to initialize bits [183:176] of the result.
/// \param __b08
///    An 8-bit integral value used to initialize bits [191:184] of the result.
/// \param __b07
///    An 8-bit integral value used to initialize bits [199:192] of the result.
/// \param __b06
///    An 8-bit integral value used to initialize bits [207:200] of the result.
/// \param __b05
///    An 8-bit integral value used to initialize bits [215:208] of the result.
/// \param __b04
///    An 8-bit integral value used to initialize bits [223:216] of the result.
/// \param __b03
///    An 8-bit integral value used to initialize bits [231:224] of the result.
/// \param __b02
///    An 8-bit integral value used to initialize bits [239:232] of the result.
/// \param __b01
///    An 8-bit integral value used to initialize bits [247:240] of the result.
/// \param __b00
///    An 8-bit integral value used to initialize bits [255:248] of the result.
/// \returns An initialized 256-bit integer vector.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_setr_epi8(char __b31, char __b30, char __b29, char __b28,
                 char __b27, char __b26, char __b25, char __b24,
                 char __b23, char __b22, char __b21, char __b20,
                 char __b19, char __b18, char __b17, char __b16,
                 char __b15, char __b14, char __b13, char __b12,
                 char __b11, char __b10, char __b09, char __b08,
                 char __b07, char __b06, char __b05, char __b04,
                 char __b03, char __b02, char __b01, char __b00)
{
  return _mm256_set_epi8(__b00, __b01, __b02, __b03, __b04, __b05, __b06, __b07,
                         __b08, __b09, __b10, __b11, __b12, __b13, __b14, __b15,
                         __b16, __b17, __b18, __b19, __b20, __b21, __b22, __b23,
                         __b24, __b25, __b26, __b27, __b28, __b29, __b30, __b31);
}

/// Constructs a 256-bit integer vector, initialized in reverse order
///    with the specified 64-bit integral values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPUNPCKLQDQ+VINSERTF128 </c>
///   instruction.
///
/// \param __a
///    A 64-bit integral value used to initialize bits [63:0] of the result.
/// \param __b
///    A 64-bit integral value used to initialize bits [127:64] of the result.
/// \param __c
///    A 64-bit integral value used to initialize bits [191:128] of the result.
/// \param __d
///    A 64-bit integral value used to initialize bits [255:192] of the result.
/// \returns An initialized 256-bit integer vector.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_setr_epi64x(long long __a, long long __b, long long __c, long long __d)
{
  return _mm256_set_epi64x(__d, __c, __b, __a);
}

/* Create vectors with repeated elements */
/// Constructs a 256-bit floating-point vector of [4 x double], with each
///    of the four double-precision floating-point vector elements set to the
///    specified double-precision floating-point value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVDDUP+VINSERTF128 </c> instruction.
///
/// \param __w
///    A double-precision floating-point value used to initialize each vector
///    element of the result.
/// \returns An initialized 256-bit floating-point vector of [4 x double].
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_set1_pd(double __w)
{
  return _mm256_set_pd(__w, __w, __w, __w);
}

/// Constructs a 256-bit floating-point vector of [8 x float], with each
///    of the eight single-precision floating-point vector elements set to the
///    specified single-precision floating-point value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPERMILPS+VINSERTF128 </c>
///   instruction.
///
/// \param __w
///    A single-precision floating-point value used to initialize each vector
///    element of the result.
/// \returns An initialized 256-bit floating-point vector of [8 x float].
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_set1_ps(float __w)
{
  return _mm256_set_ps(__w, __w, __w, __w, __w, __w, __w, __w);
}

/// Constructs a 256-bit integer vector of [8 x i32], with each of the
///    32-bit integral vector elements set to the specified 32-bit integral
///    value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPERMILPS+VINSERTF128 </c>
///   instruction.
///
/// \param __i
///    A 32-bit integral value used to initialize each vector element of the
///    result.
/// \returns An initialized 256-bit integer vector of [8 x i32].
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_set1_epi32(int __i)
{
  return _mm256_set_epi32(__i, __i, __i, __i, __i, __i, __i, __i);
}

/// Constructs a 256-bit integer vector of [16 x i16], with each of the
///    16-bit integral vector elements set to the specified 16-bit integral
///    value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSHUFB+VINSERTF128 </c> instruction.
///
/// \param __w
///    A 16-bit integral value used to initialize each vector element of the
///    result.
/// \returns An initialized 256-bit integer vector of [16 x i16].
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_set1_epi16(short __w)
{
  return _mm256_set_epi16(__w, __w, __w, __w, __w, __w, __w, __w,
                          __w, __w, __w, __w, __w, __w, __w, __w);
}

/// Constructs a 256-bit integer vector of [32 x i8], with each of the
///    8-bit integral vector elements set to the specified 8-bit integral value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VPSHUFB+VINSERTF128 </c> instruction.
///
/// \param __b
///    An 8-bit integral value used to initialize each vector element of the
///    result.
/// \returns An initialized 256-bit integer vector of [32 x i8].
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_set1_epi8(char __b)
{
  return _mm256_set_epi8(__b, __b, __b, __b, __b, __b, __b, __b,
                         __b, __b, __b, __b, __b, __b, __b, __b,
                         __b, __b, __b, __b, __b, __b, __b, __b,
                         __b, __b, __b, __b, __b, __b, __b, __b);
}

/// Constructs a 256-bit integer vector of [4 x i64], with each of the
///    64-bit integral vector elements set to the specified 64-bit integral
///    value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVDDUP+VINSERTF128 </c> instruction.
///
/// \param __q
///    A 64-bit integral value used to initialize each vector element of the
///    result.
/// \returns An initialized 256-bit integer vector of [4 x i64].
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_set1_epi64x(long long __q)
{
  return _mm256_set_epi64x(__q, __q, __q, __q);
}

/* Create __zeroed vectors */
/// Constructs a 256-bit floating-point vector of [4 x double] with all
///    vector elements initialized to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VXORPS </c> instruction.
///
/// \returns A 256-bit vector of [4 x double] with all elements set to zero.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_setzero_pd(void)
{
  return __extension__ (__m256d){ 0, 0, 0, 0 };
}

/// Constructs a 256-bit floating-point vector of [8 x float] with all
///    vector elements initialized to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VXORPS </c> instruction.
///
/// \returns A 256-bit vector of [8 x float] with all elements set to zero.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_setzero_ps(void)
{
  return __extension__ (__m256){ 0, 0, 0, 0, 0, 0, 0, 0 };
}

/// Constructs a 256-bit integer vector initialized to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VXORPS </c> instruction.
///
/// \returns A 256-bit integer vector initialized to zero.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_setzero_si256(void)
{
  return __extension__ (__m256i)(__v4di){ 0, 0, 0, 0 };
}

/* Cast between vector types */
/// Casts a 256-bit floating-point vector of [4 x double] into a 256-bit
///    floating-point vector of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 256-bit floating-point vector of [4 x double].
/// \returns A 256-bit floating-point vector of [8 x float] containing the same
///    bitwise pattern as the parameter.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_castpd_ps(__m256d __a)
{
  return (__m256)__a;
}

/// Casts a 256-bit floating-point vector of [4 x double] into a 256-bit
///    integer vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 256-bit floating-point vector of [4 x double].
/// \returns A 256-bit integer vector containing the same bitwise pattern as the
///    parameter.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_castpd_si256(__m256d __a)
{
  return (__m256i)__a;
}

/// Casts a 256-bit floating-point vector of [8 x float] into a 256-bit
///    floating-point vector of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 256-bit floating-point vector of [8 x float].
/// \returns A 256-bit floating-point vector of [4 x double] containing the same
///    bitwise pattern as the parameter.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_castps_pd(__m256 __a)
{
  return (__m256d)__a;
}

/// Casts a 256-bit floating-point vector of [8 x float] into a 256-bit
///    integer vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 256-bit floating-point vector of [8 x float].
/// \returns A 256-bit integer vector containing the same bitwise pattern as the
///    parameter.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_castps_si256(__m256 __a)
{
  return (__m256i)__a;
}

/// Casts a 256-bit integer vector into a 256-bit floating-point vector
///    of [8 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 256-bit integer vector.
/// \returns A 256-bit floating-point vector of [8 x float] containing the same
///    bitwise pattern as the parameter.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_castsi256_ps(__m256i __a)
{
  return (__m256)__a;
}

/// Casts a 256-bit integer vector into a 256-bit floating-point vector
///    of [4 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 256-bit integer vector.
/// \returns A 256-bit floating-point vector of [4 x double] containing the same
///    bitwise pattern as the parameter.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_castsi256_pd(__m256i __a)
{
  return (__m256d)__a;
}

/// Returns the lower 128 bits of a 256-bit floating-point vector of
///    [4 x double] as a 128-bit floating-point vector of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 256-bit floating-point vector of [4 x double].
/// \returns A 128-bit floating-point vector of [2 x double] containing the
///    lower 128 bits of the parameter.
static __inline __m128d __DEFAULT_FN_ATTRS
_mm256_castpd256_pd128(__m256d __a)
{
  return __builtin_shufflevector((__v4df)__a, (__v4df)__a, 0, 1);
}

/// Returns the lower 128 bits of a 256-bit floating-point vector of
///    [8 x float] as a 128-bit floating-point vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 256-bit floating-point vector of [8 x float].
/// \returns A 128-bit floating-point vector of [4 x float] containing the
///    lower 128 bits of the parameter.
static __inline __m128 __DEFAULT_FN_ATTRS
_mm256_castps256_ps128(__m256 __a)
{
  return __builtin_shufflevector((__v8sf)__a, (__v8sf)__a, 0, 1, 2, 3);
}

/// Truncates a 256-bit integer vector into a 128-bit integer vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 256-bit integer vector.
/// \returns A 128-bit integer vector containing the lower 128 bits of the
///    parameter.
static __inline __m128i __DEFAULT_FN_ATTRS
_mm256_castsi256_si128(__m256i __a)
{
  return __builtin_shufflevector((__v4di)__a, (__v4di)__a, 0, 1);
}

/// Constructs a 256-bit floating-point vector of [4 x double] from a
///    128-bit floating-point vector of [2 x double].
///
///    The lower 128 bits contain the value of the source vector. The contents
///    of the upper 128 bits are undefined.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \returns A 256-bit floating-point vector of [4 x double]. The lower 128 bits
///    contain the value of the parameter. The contents of the upper 128 bits
///    are undefined.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_castpd128_pd256(__m128d __a)
{
  return __builtin_shufflevector((__v2df)__a, (__v2df)__a, 0, 1, -1, -1);
}

/// Constructs a 256-bit floating-point vector of [8 x float] from a
///    128-bit floating-point vector of [4 x float].
///
///    The lower 128 bits contain the value of the source vector. The contents
///    of the upper 128 bits are undefined.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \returns A 256-bit floating-point vector of [8 x float]. The lower 128 bits
///    contain the value of the parameter. The contents of the upper 128 bits
///    are undefined.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_castps128_ps256(__m128 __a)
{
  return __builtin_shufflevector((__v4sf)__a, (__v4sf)__a, 0, 1, 2, 3, -1, -1, -1, -1);
}

/// Constructs a 256-bit integer vector from a 128-bit integer vector.
///
///    The lower 128 bits contain the value of the source vector. The contents
///    of the upper 128 bits are undefined.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit integer vector.
/// \returns A 256-bit integer vector. The lower 128 bits contain the value of
///    the parameter. The contents of the upper 128 bits are undefined.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_castsi128_si256(__m128i __a)
{
  return __builtin_shufflevector((__v2di)__a, (__v2di)__a, 0, 1, -1, -1);
}

/// Constructs a 256-bit floating-point vector of [4 x double] from a
///    128-bit floating-point vector of [2 x double]. The lower 128 bits
///    contain the value of the source vector. The upper 128 bits are set
///    to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double].
/// \returns A 256-bit floating-point vector of [4 x double]. The lower 128 bits
///    contain the value of the parameter. The upper 128 bits are set to zero.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_zextpd128_pd256(__m128d __a)
{
  return __builtin_shufflevector((__v2df)__a, (__v2df)_mm_setzero_pd(), 0, 1, 2, 3);
}

/// Constructs a 256-bit floating-point vector of [8 x float] from a
///    128-bit floating-point vector of [4 x float]. The lower 128 bits contain
///    the value of the source vector. The upper 128 bits are set to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float].
/// \returns A 256-bit floating-point vector of [8 x float]. The lower 128 bits
///    contain the value of the parameter. The upper 128 bits are set to zero.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_zextps128_ps256(__m128 __a)
{
  return __builtin_shufflevector((__v4sf)__a, (__v4sf)_mm_setzero_ps(), 0, 1, 2, 3, 4, 5, 6, 7);
}

/// Constructs a 256-bit integer vector from a 128-bit integer vector.
///    The lower 128 bits contain the value of the source vector. The upper
///    128 bits are set to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic has no corresponding instruction.
///
/// \param __a
///    A 128-bit integer vector.
/// \returns A 256-bit integer vector. The lower 128 bits contain the value of
///    the parameter. The upper 128 bits are set to zero.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_zextsi128_si256(__m128i __a)
{
  return __builtin_shufflevector((__v2di)__a, (__v2di)_mm_setzero_si128(), 0, 1, 2, 3);
}

/*
   Vector insert.
   We use macros rather than inlines because we only want to accept
   invocations where the immediate M is a constant expression.
*/
/// Constructs a new 256-bit vector of [8 x float] by first duplicating
///    a 256-bit vector of [8 x float] given in the first parameter, and then
///    replacing either the upper or the lower 128 bits with the contents of a
///    128-bit vector of [4 x float] in the second parameter.
///
///    The immediate integer parameter determines between the upper or the lower
///    128 bits.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256 _mm256_insertf128_ps(__m256 V1, __m128 V2, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VINSERTF128 </c> instruction.
///
/// \param V1
///    A 256-bit vector of [8 x float]. This vector is copied to the result
///    first, and then either the upper or the lower 128 bits of the result will
///    be replaced by the contents of \a V2.
/// \param V2
///    A 128-bit vector of [4 x float]. The contents of this parameter are
///    written to either the upper or the lower 128 bits of the result depending
///    on the value of parameter \a M.
/// \param M
///    An immediate integer. The least significant bit determines how the values
///    from the two parameters are interleaved: \n
///    If bit [0] of \a M is 0, \a V2 are copied to bits [127:0] of the result,
///    and bits [255:128] of \a V1 are copied to bits [255:128] of the
///    result. \n
///    If bit [0] of \a M is 1, \a V2 are copied to bits [255:128] of the
///    result, and bits [127:0] of \a V1 are copied to bits [127:0] of the
///    result.
/// \returns A 256-bit vector of [8 x float] containing the interleaved values.
#define _mm256_insertf128_ps(V1, V2, M) \
  (__m256)__builtin_ia32_vinsertf128_ps256((__v8sf)(__m256)(V1), \
                                           (__v4sf)(__m128)(V2), (int)(M))

/// Constructs a new 256-bit vector of [4 x double] by first duplicating
///    a 256-bit vector of [4 x double] given in the first parameter, and then
///    replacing either the upper or the lower 128 bits with the contents of a
///    128-bit vector of [2 x double] in the second parameter.
///
///    The immediate integer parameter determines between the upper or the lower
///    128 bits.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256d _mm256_insertf128_pd(__m256d V1, __m128d V2, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VINSERTF128 </c> instruction.
///
/// \param V1
///    A 256-bit vector of [4 x double]. This vector is copied to the result
///    first, and then either the upper or the lower 128 bits of the result will
///    be replaced by the contents of \a V2.
/// \param V2
///    A 128-bit vector of [2 x double]. The contents of this parameter are
///    written to either the upper or the lower 128 bits of the result depending
///    on the value of parameter \a M.
/// \param M
///    An immediate integer. The least significant bit determines how the values
///    from the two parameters are interleaved: \n
///    If bit [0] of \a M is 0, \a V2 are copied to bits [127:0] of the result,
///    and bits [255:128] of \a V1 are copied to bits [255:128] of the
///    result. \n
///    If bit [0] of \a M is 1, \a V2 are copied to bits [255:128] of the
///    result, and bits [127:0] of \a V1 are copied to bits [127:0] of the
///    result.
/// \returns A 256-bit vector of [4 x double] containing the interleaved values.
#define _mm256_insertf128_pd(V1, V2, M) \
  (__m256d)__builtin_ia32_vinsertf128_pd256((__v4df)(__m256d)(V1), \
                                            (__v2df)(__m128d)(V2), (int)(M))

/// Constructs a new 256-bit integer vector by first duplicating a
///    256-bit integer vector given in the first parameter, and then replacing
///    either the upper or the lower 128 bits with the contents of a 128-bit
///    integer vector in the second parameter.
///
///    The immediate integer parameter determines between the upper or the lower
///    128 bits.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m256i _mm256_insertf128_si256(__m256i V1, __m128i V2, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VINSERTF128 </c> instruction.
///
/// \param V1
///    A 256-bit integer vector. This vector is copied to the result first, and
///    then either the upper or the lower 128 bits of the result will be
///    replaced by the contents of \a V2.
/// \param V2
///    A 128-bit integer vector. The contents of this parameter are written to
///    either the upper or the lower 128 bits of the result depending on the
///     value of parameter \a M.
/// \param M
///    An immediate integer. The least significant bit determines how the values
///    from the two parameters are interleaved: \n
///    If bit [0] of \a M is 0, \a V2 are copied to bits [127:0] of the result,
///    and bits [255:128] of \a V1 are copied to bits [255:128] of the
///    result. \n
///    If bit [0] of \a M is 1, \a V2 are copied to bits [255:128] of the
///    result, and bits [127:0] of \a V1 are copied to bits [127:0] of the
///    result.
/// \returns A 256-bit integer vector containing the interleaved values.
#define _mm256_insertf128_si256(V1, V2, M) \
  (__m256i)__builtin_ia32_vinsertf128_si256((__v8si)(__m256i)(V1), \
                                            (__v4si)(__m128i)(V2), (int)(M))

/*
   Vector extract.
   We use macros rather than inlines because we only want to accept
   invocations where the immediate M is a constant expression.
*/
/// Extracts either the upper or the lower 128 bits from a 256-bit vector
///    of [8 x float], as determined by the immediate integer parameter, and
///    returns the extracted bits as a 128-bit vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128 _mm256_extractf128_ps(__m256 V, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VEXTRACTF128 </c> instruction.
///
/// \param V
///    A 256-bit vector of [8 x float].
/// \param M
///    An immediate integer. The least significant bit determines which bits are
///    extracted from the first parameter: \n
///    If bit [0] of \a M is 0, bits [127:0] of \a V are copied to the
///    result. \n
///    If bit [0] of \a M is 1, bits [255:128] of \a V are copied to the result.
/// \returns A 128-bit vector of [4 x float] containing the extracted bits.
#define _mm256_extractf128_ps(V, M) \
  (__m128)__builtin_ia32_vextractf128_ps256((__v8sf)(__m256)(V), (int)(M))

/// Extracts either the upper or the lower 128 bits from a 256-bit vector
///    of [4 x double], as determined by the immediate integer parameter, and
///    returns the extracted bits as a 128-bit vector of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm256_extractf128_pd(__m256d V, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VEXTRACTF128 </c> instruction.
///
/// \param V
///    A 256-bit vector of [4 x double].
/// \param M
///    An immediate integer. The least significant bit determines which bits are
///    extracted from the first parameter: \n
///    If bit [0] of \a M is 0, bits [127:0] of \a V are copied to the
///    result. \n
///    If bit [0] of \a M is 1, bits [255:128] of \a V are copied to the result.
/// \returns A 128-bit vector of [2 x double] containing the extracted bits.
#define _mm256_extractf128_pd(V, M) \
  (__m128d)__builtin_ia32_vextractf128_pd256((__v4df)(__m256d)(V), (int)(M))

/// Extracts either the upper or the lower 128 bits from a 256-bit
///    integer vector, as determined by the immediate integer parameter, and
///    returns the extracted bits as a 128-bit integer vector.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm256_extractf128_si256(__m256i V, const int M);
/// \endcode
///
/// This intrinsic corresponds to the <c> VEXTRACTF128 </c> instruction.
///
/// \param V
///    A 256-bit integer vector.
/// \param M
///    An immediate integer. The least significant bit determines which bits are
///    extracted from the first parameter:  \n
///    If bit [0] of \a M is 0, bits [127:0] of \a V are copied to the
///    result. \n
///    If bit [0] of \a M is 1, bits [255:128] of \a V are copied to the result.
/// \returns A 128-bit integer vector containing the extracted bits.
#define _mm256_extractf128_si256(V, M) \
  (__m128i)__builtin_ia32_vextractf128_si256((__v8si)(__m256i)(V), (int)(M))

/* SIMD load ops (unaligned) */
/// Loads two 128-bit floating-point vectors of [4 x float] from
///    unaligned memory locations and constructs a 256-bit floating-point vector
///    of [8 x float] by concatenating the two 128-bit vectors.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to load instructions followed by the
///   <c> VINSERTF128 </c> instruction.
///
/// \param __addr_hi
///    A pointer to a 128-bit memory location containing 4 consecutive
///    single-precision floating-point values. These values are to be copied to
///    bits[255:128] of the result. The address of the memory location does not
///    have to be aligned.
/// \param __addr_lo
///    A pointer to a 128-bit memory location containing 4 consecutive
///    single-precision floating-point values. These values are to be copied to
///    bits[127:0] of the result. The address of the memory location does not
///    have to be aligned.
/// \returns A 256-bit floating-point vector of [8 x float] containing the
///    concatenated result.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_loadu2_m128(float const *__addr_hi, float const *__addr_lo)
{
  __m256 __v256 = _mm256_castps128_ps256(_mm_loadu_ps(__addr_lo));
  return _mm256_insertf128_ps(__v256, _mm_loadu_ps(__addr_hi), 1);
}

/// Loads two 128-bit floating-point vectors of [2 x double] from
///    unaligned memory locations and constructs a 256-bit floating-point vector
///    of [4 x double] by concatenating the two 128-bit vectors.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to load instructions followed by the
///   <c> VINSERTF128 </c> instruction.
///
/// \param __addr_hi
///    A pointer to a 128-bit memory location containing two consecutive
///    double-precision floating-point values. These values are to be copied to
///    bits[255:128] of the result. The address of the memory location does not
///    have to be aligned.
/// \param __addr_lo
///    A pointer to a 128-bit memory location containing two consecutive
///    double-precision floating-point values. These values are to be copied to
///    bits[127:0] of the result. The address of the memory location does not
///    have to be aligned.
/// \returns A 256-bit floating-point vector of [4 x double] containing the
///    concatenated result.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_loadu2_m128d(double const *__addr_hi, double const *__addr_lo)
{
  __m256d __v256 = _mm256_castpd128_pd256(_mm_loadu_pd(__addr_lo));
  return _mm256_insertf128_pd(__v256, _mm_loadu_pd(__addr_hi), 1);
}

/// Loads two 128-bit integer vectors from unaligned memory locations and
///    constructs a 256-bit integer vector by concatenating the two 128-bit
///    vectors.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to load instructions followed by the
///   <c> VINSERTF128 </c> instruction.
///
/// \param __addr_hi
///    A pointer to a 128-bit memory location containing a 128-bit integer
///    vector. This vector is to be copied to bits[255:128] of the result. The
///    address of the memory location does not have to be aligned.
/// \param __addr_lo
///    A pointer to a 128-bit memory location containing a 128-bit integer
///    vector. This vector is to be copied to bits[127:0] of the result. The
///    address of the memory location does not have to be aligned.
/// \returns A 256-bit integer vector containing the concatenated result.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_loadu2_m128i(__m128i const *__addr_hi, __m128i const *__addr_lo)
{
  __m256i __v256 = _mm256_castsi128_si256(_mm_loadu_si128(__addr_lo));
  return _mm256_insertf128_si256(__v256, _mm_loadu_si128(__addr_hi), 1);
}

/* SIMD store ops (unaligned) */
/// Stores the upper and lower 128 bits of a 256-bit floating-point
///    vector of [8 x float] into two different unaligned memory locations.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VEXTRACTF128 </c> instruction and the
///   store instructions.
///
/// \param __addr_hi
///    A pointer to a 128-bit memory location. Bits[255:128] of \a __a are to be
///    copied to this memory location. The address of this memory location does
///    not have to be aligned.
/// \param __addr_lo
///    A pointer to a 128-bit memory location. Bits[127:0] of \a __a are to be
///    copied to this memory location. The address of this memory location does
///    not have to be aligned.
/// \param __a
///    A 256-bit floating-point vector of [8 x float].
static __inline void __DEFAULT_FN_ATTRS
_mm256_storeu2_m128(float *__addr_hi, float *__addr_lo, __m256 __a)
{
  __m128 __v128;

  __v128 = _mm256_castps256_ps128(__a);
  _mm_storeu_ps(__addr_lo, __v128);
  __v128 = _mm256_extractf128_ps(__a, 1);
  _mm_storeu_ps(__addr_hi, __v128);
}

/// Stores the upper and lower 128 bits of a 256-bit floating-point
///    vector of [4 x double] into two different unaligned memory locations.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VEXTRACTF128 </c> instruction and the
///   store instructions.
///
/// \param __addr_hi
///    A pointer to a 128-bit memory location. Bits[255:128] of \a __a are to be
///    copied to this memory location. The address of this memory location does
///    not have to be aligned.
/// \param __addr_lo
///    A pointer to a 128-bit memory location. Bits[127:0] of \a __a are to be
///    copied to this memory location. The address of this memory location does
///    not have to be aligned.
/// \param __a
///    A 256-bit floating-point vector of [4 x double].
static __inline void __DEFAULT_FN_ATTRS
_mm256_storeu2_m128d(double *__addr_hi, double *__addr_lo, __m256d __a)
{
  __m128d __v128;

  __v128 = _mm256_castpd256_pd128(__a);
  _mm_storeu_pd(__addr_lo, __v128);
  __v128 = _mm256_extractf128_pd(__a, 1);
  _mm_storeu_pd(__addr_hi, __v128);
}

/// Stores the upper and lower 128 bits of a 256-bit integer vector into
///    two different unaligned memory locations.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VEXTRACTF128 </c> instruction and the
///   store instructions.
///
/// \param __addr_hi
///    A pointer to a 128-bit memory location. Bits[255:128] of \a __a are to be
///    copied to this memory location. The address of this memory location does
///    not have to be aligned.
/// \param __addr_lo
///    A pointer to a 128-bit memory location. Bits[127:0] of \a __a are to be
///    copied to this memory location. The address of this memory location does
///    not have to be aligned.
/// \param __a
///    A 256-bit integer vector.
static __inline void __DEFAULT_FN_ATTRS
_mm256_storeu2_m128i(__m128i *__addr_hi, __m128i *__addr_lo, __m256i __a)
{
  __m128i __v128;

  __v128 = _mm256_castsi256_si128(__a);
  _mm_storeu_si128(__addr_lo, __v128);
  __v128 = _mm256_extractf128_si256(__a, 1);
  _mm_storeu_si128(__addr_hi, __v128);
}

/// Constructs a 256-bit floating-point vector of [8 x float] by
///    concatenating two 128-bit floating-point vectors of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VINSERTF128 </c> instruction.
///
/// \param __hi
///    A 128-bit floating-point vector of [4 x float] to be copied to the upper
///    128 bits of the result.
/// \param __lo
///    A 128-bit floating-point vector of [4 x float] to be copied to the lower
///    128 bits of the result.
/// \returns A 256-bit floating-point vector of [8 x float] containing the
///    concatenated result.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_set_m128 (__m128 __hi, __m128 __lo)
{
  return (__m256) __builtin_shufflevector((__v4sf)__lo, (__v4sf)__hi, 0, 1, 2, 3, 4, 5, 6, 7);
}

/// Constructs a 256-bit floating-point vector of [4 x double] by
///    concatenating two 128-bit floating-point vectors of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VINSERTF128 </c> instruction.
///
/// \param __hi
///    A 128-bit floating-point vector of [2 x double] to be copied to the upper
///    128 bits of the result.
/// \param __lo
///    A 128-bit floating-point vector of [2 x double] to be copied to the lower
///    128 bits of the result.
/// \returns A 256-bit floating-point vector of [4 x double] containing the
///    concatenated result.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_set_m128d (__m128d __hi, __m128d __lo)
{
  return (__m256d) __builtin_shufflevector((__v2df)__lo, (__v2df)__hi, 0, 1, 2, 3);
}

/// Constructs a 256-bit integer vector by concatenating two 128-bit
///    integer vectors.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VINSERTF128 </c> instruction.
///
/// \param __hi
///    A 128-bit integer vector to be copied to the upper 128 bits of the
///    result.
/// \param __lo
///    A 128-bit integer vector to be copied to the lower 128 bits of the
///    result.
/// \returns A 256-bit integer vector containing the concatenated result.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_set_m128i (__m128i __hi, __m128i __lo)
{
  return (__m256i) __builtin_shufflevector((__v2di)__lo, (__v2di)__hi, 0, 1, 2, 3);
}

/// Constructs a 256-bit floating-point vector of [8 x float] by
///    concatenating two 128-bit floating-point vectors of [4 x float]. This is
///    similar to _mm256_set_m128, but the order of the input parameters is
///    swapped.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VINSERTF128 </c> instruction.
///
/// \param __lo
///    A 128-bit floating-point vector of [4 x float] to be copied to the lower
///    128 bits of the result.
/// \param __hi
///    A 128-bit floating-point vector of [4 x float] to be copied to the upper
///    128 bits of the result.
/// \returns A 256-bit floating-point vector of [8 x float] containing the
///    concatenated result.
static __inline __m256 __DEFAULT_FN_ATTRS
_mm256_setr_m128 (__m128 __lo, __m128 __hi)
{
  return _mm256_set_m128(__hi, __lo);
}

/// Constructs a 256-bit floating-point vector of [4 x double] by
///    concatenating two 128-bit floating-point vectors of [2 x double]. This is
///    similar to _mm256_set_m128d, but the order of the input parameters is
///    swapped.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VINSERTF128 </c> instruction.
///
/// \param __lo
///    A 128-bit floating-point vector of [2 x double] to be copied to the lower
///    128 bits of the result.
/// \param __hi
///    A 128-bit floating-point vector of [2 x double] to be copied to the upper
///    128 bits of the result.
/// \returns A 256-bit floating-point vector of [4 x double] containing the
///    concatenated result.
static __inline __m256d __DEFAULT_FN_ATTRS
_mm256_setr_m128d (__m128d __lo, __m128d __hi)
{
  return (__m256d)_mm256_set_m128d(__hi, __lo);
}

/// Constructs a 256-bit integer vector by concatenating two 128-bit
///    integer vectors. This is similar to _mm256_set_m128i, but the order of
///    the input parameters is swapped.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VINSERTF128 </c> instruction.
///
/// \param __lo
///    A 128-bit integer vector to be copied to the lower 128 bits of the
///    result.
/// \param __hi
///    A 128-bit integer vector to be copied to the upper 128 bits of the
///    result.
/// \returns A 256-bit integer vector containing the concatenated result.
static __inline __m256i __DEFAULT_FN_ATTRS
_mm256_setr_m128i (__m128i __lo, __m128i __hi)
{
  return (__m256i)_mm256_set_m128i(__hi, __lo);
}

#undef __DEFAULT_FN_ATTRS
#undef __DEFAULT_FN_ATTRS128

#endif /* __AVXINTRIN_H */
