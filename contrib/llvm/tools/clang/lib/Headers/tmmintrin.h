/*===---- tmmintrin.h - SSSE3 intrinsics -----------------------------------===
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

#ifndef __TMMINTRIN_H
#define __TMMINTRIN_H

#include <pmmintrin.h>

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("ssse3"), __min_vector_width__(64)))
#define __DEFAULT_FN_ATTRS_MMX __attribute__((__always_inline__, __nodebug__, __target__("mmx,ssse3"), __min_vector_width__(64)))

/// Computes the absolute value of each of the packed 8-bit signed
///    integers in the source operand and stores the 8-bit unsigned integer
///    results in the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PABSB instruction.
///
/// \param __a
///    A 64-bit vector of [8 x i8].
/// \returns A 64-bit integer vector containing the absolute values of the
///    elements in the operand.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_abs_pi8(__m64 __a)
{
    return (__m64)__builtin_ia32_pabsb((__v8qi)__a);
}

/// Computes the absolute value of each of the packed 8-bit signed
///    integers in the source operand and stores the 8-bit unsigned integer
///    results in the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VPABSB instruction.
///
/// \param __a
///    A 128-bit vector of [16 x i8].
/// \returns A 128-bit integer vector containing the absolute values of the
///    elements in the operand.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_abs_epi8(__m128i __a)
{
    return (__m128i)__builtin_ia32_pabsb128((__v16qi)__a);
}

/// Computes the absolute value of each of the packed 16-bit signed
///    integers in the source operand and stores the 16-bit unsigned integer
///    results in the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PABSW instruction.
///
/// \param __a
///    A 64-bit vector of [4 x i16].
/// \returns A 64-bit integer vector containing the absolute values of the
///    elements in the operand.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_abs_pi16(__m64 __a)
{
    return (__m64)__builtin_ia32_pabsw((__v4hi)__a);
}

/// Computes the absolute value of each of the packed 16-bit signed
///    integers in the source operand and stores the 16-bit unsigned integer
///    results in the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VPABSW instruction.
///
/// \param __a
///    A 128-bit vector of [8 x i16].
/// \returns A 128-bit integer vector containing the absolute values of the
///    elements in the operand.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_abs_epi16(__m128i __a)
{
    return (__m128i)__builtin_ia32_pabsw128((__v8hi)__a);
}

/// Computes the absolute value of each of the packed 32-bit signed
///    integers in the source operand and stores the 32-bit unsigned integer
///    results in the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PABSD instruction.
///
/// \param __a
///    A 64-bit vector of [2 x i32].
/// \returns A 64-bit integer vector containing the absolute values of the
///    elements in the operand.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_abs_pi32(__m64 __a)
{
    return (__m64)__builtin_ia32_pabsd((__v2si)__a);
}

/// Computes the absolute value of each of the packed 32-bit signed
///    integers in the source operand and stores the 32-bit unsigned integer
///    results in the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VPABSD instruction.
///
/// \param __a
///    A 128-bit vector of [4 x i32].
/// \returns A 128-bit integer vector containing the absolute values of the
///    elements in the operand.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_abs_epi32(__m128i __a)
{
    return (__m128i)__builtin_ia32_pabsd128((__v4si)__a);
}

/// Concatenates the two 128-bit integer vector operands, and
///    right-shifts the result by the number of bytes specified in the immediate
///    operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_alignr_epi8(__m128i a, __m128i b, const int n);
/// \endcode
///
/// This intrinsic corresponds to the \c PALIGNR instruction.
///
/// \param a
///    A 128-bit vector of [16 x i8] containing one of the source operands.
/// \param b
///    A 128-bit vector of [16 x i8] containing one of the source operands.
/// \param n
///    An immediate operand specifying how many bytes to right-shift the result.
/// \returns A 128-bit integer vector containing the concatenated right-shifted
///    value.
#define _mm_alignr_epi8(a, b, n) \
  (__m128i)__builtin_ia32_palignr128((__v16qi)(__m128i)(a), \
                                     (__v16qi)(__m128i)(b), (n))

/// Concatenates the two 64-bit integer vector operands, and right-shifts
///    the result by the number of bytes specified in the immediate operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m64 _mm_alignr_pi8(__m64 a, __m64 b, const int n);
/// \endcode
///
/// This intrinsic corresponds to the \c PALIGNR instruction.
///
/// \param a
///    A 64-bit vector of [8 x i8] containing one of the source operands.
/// \param b
///    A 64-bit vector of [8 x i8] containing one of the source operands.
/// \param n
///    An immediate operand specifying how many bytes to right-shift the result.
/// \returns A 64-bit integer vector containing the concatenated right-shifted
///    value.
#define _mm_alignr_pi8(a, b, n) \
  (__m64)__builtin_ia32_palignr((__v8qi)(__m64)(a), (__v8qi)(__m64)(b), (n))

/// Horizontally adds the adjacent pairs of values contained in 2 packed
///    128-bit vectors of [8 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VPHADDW instruction.
///
/// \param __a
///    A 128-bit vector of [8 x i16] containing one of the source operands. The
///    horizontal sums of the values are stored in the lower bits of the
///    destination.
/// \param __b
///    A 128-bit vector of [8 x i16] containing one of the source operands. The
///    horizontal sums of the values are stored in the upper bits of the
///    destination.
/// \returns A 128-bit vector of [8 x i16] containing the horizontal sums of
///    both operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_hadd_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_phaddw128((__v8hi)__a, (__v8hi)__b);
}

/// Horizontally adds the adjacent pairs of values contained in 2 packed
///    128-bit vectors of [4 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VPHADDD instruction.
///
/// \param __a
///    A 128-bit vector of [4 x i32] containing one of the source operands. The
///    horizontal sums of the values are stored in the lower bits of the
///    destination.
/// \param __b
///    A 128-bit vector of [4 x i32] containing one of the source operands. The
///    horizontal sums of the values are stored in the upper bits of the
///    destination.
/// \returns A 128-bit vector of [4 x i32] containing the horizontal sums of
///    both operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_hadd_epi32(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_phaddd128((__v4si)__a, (__v4si)__b);
}

/// Horizontally adds the adjacent pairs of values contained in 2 packed
///    64-bit vectors of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PHADDW instruction.
///
/// \param __a
///    A 64-bit vector of [4 x i16] containing one of the source operands. The
///    horizontal sums of the values are stored in the lower bits of the
///    destination.
/// \param __b
///    A 64-bit vector of [4 x i16] containing one of the source operands. The
///    horizontal sums of the values are stored in the upper bits of the
///    destination.
/// \returns A 64-bit vector of [4 x i16] containing the horizontal sums of both
///    operands.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_hadd_pi16(__m64 __a, __m64 __b)
{
    return (__m64)__builtin_ia32_phaddw((__v4hi)__a, (__v4hi)__b);
}

/// Horizontally adds the adjacent pairs of values contained in 2 packed
///    64-bit vectors of [2 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PHADDD instruction.
///
/// \param __a
///    A 64-bit vector of [2 x i32] containing one of the source operands. The
///    horizontal sums of the values are stored in the lower bits of the
///    destination.
/// \param __b
///    A 64-bit vector of [2 x i32] containing one of the source operands. The
///    horizontal sums of the values are stored in the upper bits of the
///    destination.
/// \returns A 64-bit vector of [2 x i32] containing the horizontal sums of both
///    operands.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_hadd_pi32(__m64 __a, __m64 __b)
{
    return (__m64)__builtin_ia32_phaddd((__v2si)__a, (__v2si)__b);
}

/// Horizontally adds the adjacent pairs of values contained in 2 packed
///    128-bit vectors of [8 x i16]. Positive sums greater than 0x7FFF are
///    saturated to 0x7FFF. Negative sums less than 0x8000 are saturated to
///    0x8000.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VPHADDSW instruction.
///
/// \param __a
///    A 128-bit vector of [8 x i16] containing one of the source operands. The
///    horizontal sums of the values are stored in the lower bits of the
///    destination.
/// \param __b
///    A 128-bit vector of [8 x i16] containing one of the source operands. The
///    horizontal sums of the values are stored in the upper bits of the
///    destination.
/// \returns A 128-bit vector of [8 x i16] containing the horizontal saturated
///    sums of both operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_hadds_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_phaddsw128((__v8hi)__a, (__v8hi)__b);
}

/// Horizontally adds the adjacent pairs of values contained in 2 packed
///    64-bit vectors of [4 x i16]. Positive sums greater than 0x7FFF are
///    saturated to 0x7FFF. Negative sums less than 0x8000 are saturated to
///    0x8000.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PHADDSW instruction.
///
/// \param __a
///    A 64-bit vector of [4 x i16] containing one of the source operands. The
///    horizontal sums of the values are stored in the lower bits of the
///    destination.
/// \param __b
///    A 64-bit vector of [4 x i16] containing one of the source operands. The
///    horizontal sums of the values are stored in the upper bits of the
///    destination.
/// \returns A 64-bit vector of [4 x i16] containing the horizontal saturated
///    sums of both operands.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_hadds_pi16(__m64 __a, __m64 __b)
{
    return (__m64)__builtin_ia32_phaddsw((__v4hi)__a, (__v4hi)__b);
}

/// Horizontally subtracts the adjacent pairs of values contained in 2
///    packed 128-bit vectors of [8 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VPHSUBW instruction.
///
/// \param __a
///    A 128-bit vector of [8 x i16] containing one of the source operands. The
///    horizontal differences between the values are stored in the lower bits of
///    the destination.
/// \param __b
///    A 128-bit vector of [8 x i16] containing one of the source operands. The
///    horizontal differences between the values are stored in the upper bits of
///    the destination.
/// \returns A 128-bit vector of [8 x i16] containing the horizontal differences
///    of both operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_hsub_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_phsubw128((__v8hi)__a, (__v8hi)__b);
}

/// Horizontally subtracts the adjacent pairs of values contained in 2
///    packed 128-bit vectors of [4 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VPHSUBD instruction.
///
/// \param __a
///    A 128-bit vector of [4 x i32] containing one of the source operands. The
///    horizontal differences between the values are stored in the lower bits of
///    the destination.
/// \param __b
///    A 128-bit vector of [4 x i32] containing one of the source operands. The
///    horizontal differences between the values are stored in the upper bits of
///    the destination.
/// \returns A 128-bit vector of [4 x i32] containing the horizontal differences
///    of both operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_hsub_epi32(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_phsubd128((__v4si)__a, (__v4si)__b);
}

/// Horizontally subtracts the adjacent pairs of values contained in 2
///    packed 64-bit vectors of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PHSUBW instruction.
///
/// \param __a
///    A 64-bit vector of [4 x i16] containing one of the source operands. The
///    horizontal differences between the values are stored in the lower bits of
///    the destination.
/// \param __b
///    A 64-bit vector of [4 x i16] containing one of the source operands. The
///    horizontal differences between the values are stored in the upper bits of
///    the destination.
/// \returns A 64-bit vector of [4 x i16] containing the horizontal differences
///    of both operands.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_hsub_pi16(__m64 __a, __m64 __b)
{
    return (__m64)__builtin_ia32_phsubw((__v4hi)__a, (__v4hi)__b);
}

/// Horizontally subtracts the adjacent pairs of values contained in 2
///    packed 64-bit vectors of [2 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PHSUBD instruction.
///
/// \param __a
///    A 64-bit vector of [2 x i32] containing one of the source operands. The
///    horizontal differences between the values are stored in the lower bits of
///    the destination.
/// \param __b
///    A 64-bit vector of [2 x i32] containing one of the source operands. The
///    horizontal differences between the values are stored in the upper bits of
///    the destination.
/// \returns A 64-bit vector of [2 x i32] containing the horizontal differences
///    of both operands.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_hsub_pi32(__m64 __a, __m64 __b)
{
    return (__m64)__builtin_ia32_phsubd((__v2si)__a, (__v2si)__b);
}

/// Horizontally subtracts the adjacent pairs of values contained in 2
///    packed 128-bit vectors of [8 x i16]. Positive differences greater than
///    0x7FFF are saturated to 0x7FFF. Negative differences less than 0x8000 are
///    saturated to 0x8000.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VPHSUBSW instruction.
///
/// \param __a
///    A 128-bit vector of [8 x i16] containing one of the source operands. The
///    horizontal differences between the values are stored in the lower bits of
///    the destination.
/// \param __b
///    A 128-bit vector of [8 x i16] containing one of the source operands. The
///    horizontal differences between the values are stored in the upper bits of
///    the destination.
/// \returns A 128-bit vector of [8 x i16] containing the horizontal saturated
///    differences of both operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_hsubs_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_phsubsw128((__v8hi)__a, (__v8hi)__b);
}

/// Horizontally subtracts the adjacent pairs of values contained in 2
///    packed 64-bit vectors of [4 x i16]. Positive differences greater than
///    0x7FFF are saturated to 0x7FFF. Negative differences less than 0x8000 are
///    saturated to 0x8000.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PHSUBSW instruction.
///
/// \param __a
///    A 64-bit vector of [4 x i16] containing one of the source operands. The
///    horizontal differences between the values are stored in the lower bits of
///    the destination.
/// \param __b
///    A 64-bit vector of [4 x i16] containing one of the source operands. The
///    horizontal differences between the values are stored in the upper bits of
///    the destination.
/// \returns A 64-bit vector of [4 x i16] containing the horizontal saturated
///    differences of both operands.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_hsubs_pi16(__m64 __a, __m64 __b)
{
    return (__m64)__builtin_ia32_phsubsw((__v4hi)__a, (__v4hi)__b);
}

/// Multiplies corresponding pairs of packed 8-bit unsigned integer
///    values contained in the first source operand and packed 8-bit signed
///    integer values contained in the second source operand, adds pairs of
///    contiguous products with signed saturation, and writes the 16-bit sums to
///    the corresponding bits in the destination.
///
///    For example, bits [7:0] of both operands are multiplied, bits [15:8] of
///    both operands are multiplied, and the sum of both results is written to
///    bits [15:0] of the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VPMADDUBSW instruction.
///
/// \param __a
///    A 128-bit integer vector containing the first source operand.
/// \param __b
///    A 128-bit integer vector containing the second source operand.
/// \returns A 128-bit integer vector containing the sums of products of both
///    operands: \n
///    \a R0 := (\a __a0 * \a __b0) + (\a __a1 * \a __b1) \n
///    \a R1 := (\a __a2 * \a __b2) + (\a __a3 * \a __b3) \n
///    \a R2 := (\a __a4 * \a __b4) + (\a __a5 * \a __b5) \n
///    \a R3 := (\a __a6 * \a __b6) + (\a __a7 * \a __b7) \n
///    \a R4 := (\a __a8 * \a __b8) + (\a __a9 * \a __b9) \n
///    \a R5 := (\a __a10 * \a __b10) + (\a __a11 * \a __b11) \n
///    \a R6 := (\a __a12 * \a __b12) + (\a __a13 * \a __b13) \n
///    \a R7 := (\a __a14 * \a __b14) + (\a __a15 * \a __b15)
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_maddubs_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_pmaddubsw128((__v16qi)__a, (__v16qi)__b);
}

/// Multiplies corresponding pairs of packed 8-bit unsigned integer
///    values contained in the first source operand and packed 8-bit signed
///    integer values contained in the second source operand, adds pairs of
///    contiguous products with signed saturation, and writes the 16-bit sums to
///    the corresponding bits in the destination.
///
///    For example, bits [7:0] of both operands are multiplied, bits [15:8] of
///    both operands are multiplied, and the sum of both results is written to
///    bits [15:0] of the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PMADDUBSW instruction.
///
/// \param __a
///    A 64-bit integer vector containing the first source operand.
/// \param __b
///    A 64-bit integer vector containing the second source operand.
/// \returns A 64-bit integer vector containing the sums of products of both
///    operands: \n
///    \a R0 := (\a __a0 * \a __b0) + (\a __a1 * \a __b1) \n
///    \a R1 := (\a __a2 * \a __b2) + (\a __a3 * \a __b3) \n
///    \a R2 := (\a __a4 * \a __b4) + (\a __a5 * \a __b5) \n
///    \a R3 := (\a __a6 * \a __b6) + (\a __a7 * \a __b7)
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_maddubs_pi16(__m64 __a, __m64 __b)
{
    return (__m64)__builtin_ia32_pmaddubsw((__v8qi)__a, (__v8qi)__b);
}

/// Multiplies packed 16-bit signed integer values, truncates the 32-bit
///    products to the 18 most significant bits by right-shifting, rounds the
///    truncated value by adding 1, and writes bits [16:1] to the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VPMULHRSW instruction.
///
/// \param __a
///    A 128-bit vector of [8 x i16] containing one of the source operands.
/// \param __b
///    A 128-bit vector of [8 x i16] containing one of the source operands.
/// \returns A 128-bit vector of [8 x i16] containing the rounded and scaled
///    products of both operands.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_mulhrs_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_pmulhrsw128((__v8hi)__a, (__v8hi)__b);
}

/// Multiplies packed 16-bit signed integer values, truncates the 32-bit
///    products to the 18 most significant bits by right-shifting, rounds the
///    truncated value by adding 1, and writes bits [16:1] to the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PMULHRSW instruction.
///
/// \param __a
///    A 64-bit vector of [4 x i16] containing one of the source operands.
/// \param __b
///    A 64-bit vector of [4 x i16] containing one of the source operands.
/// \returns A 64-bit vector of [4 x i16] containing the rounded and scaled
///    products of both operands.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_mulhrs_pi16(__m64 __a, __m64 __b)
{
    return (__m64)__builtin_ia32_pmulhrsw((__v4hi)__a, (__v4hi)__b);
}

/// Copies the 8-bit integers from a 128-bit integer vector to the
///    destination or clears 8-bit values in the destination, as specified by
///    the second source operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VPSHUFB instruction.
///
/// \param __a
///    A 128-bit integer vector containing the values to be copied.
/// \param __b
///    A 128-bit integer vector containing control bytes corresponding to
///    positions in the destination:
///    Bit 7: \n
///    1: Clear the corresponding byte in the destination. \n
///    0: Copy the selected source byte to the corresponding byte in the
///    destination. \n
///    Bits [6:4] Reserved.  \n
///    Bits [3:0] select the source byte to be copied.
/// \returns A 128-bit integer vector containing the copied or cleared values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_shuffle_epi8(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_pshufb128((__v16qi)__a, (__v16qi)__b);
}

/// Copies the 8-bit integers from a 64-bit integer vector to the
///    destination or clears 8-bit values in the destination, as specified by
///    the second source operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PSHUFB instruction.
///
/// \param __a
///    A 64-bit integer vector containing the values to be copied.
/// \param __b
///    A 64-bit integer vector containing control bytes corresponding to
///    positions in the destination:
///    Bit 7: \n
///    1: Clear the corresponding byte in the destination. \n
///    0: Copy the selected source byte to the corresponding byte in the
///    destination. \n
///    Bits [3:0] select the source byte to be copied.
/// \returns A 64-bit integer vector containing the copied or cleared values.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_shuffle_pi8(__m64 __a, __m64 __b)
{
    return (__m64)__builtin_ia32_pshufb((__v8qi)__a, (__v8qi)__b);
}

/// For each 8-bit integer in the first source operand, perform one of
///    the following actions as specified by the second source operand.
///
///    If the byte in the second source is negative, calculate the two's
///    complement of the corresponding byte in the first source, and write that
///    value to the destination. If the byte in the second source is positive,
///    copy the corresponding byte from the first source to the destination. If
///    the byte in the second source is zero, clear the corresponding byte in
///    the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VPSIGNB instruction.
///
/// \param __a
///    A 128-bit integer vector containing the values to be copied.
/// \param __b
///    A 128-bit integer vector containing control bytes corresponding to
///    positions in the destination.
/// \returns A 128-bit integer vector containing the resultant values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sign_epi8(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_psignb128((__v16qi)__a, (__v16qi)__b);
}

/// For each 16-bit integer in the first source operand, perform one of
///    the following actions as specified by the second source operand.
///
///    If the word in the second source is negative, calculate the two's
///    complement of the corresponding word in the first source, and write that
///    value to the destination. If the word in the second source is positive,
///    copy the corresponding word from the first source to the destination. If
///    the word in the second source is zero, clear the corresponding word in
///    the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VPSIGNW instruction.
///
/// \param __a
///    A 128-bit integer vector containing the values to be copied.
/// \param __b
///    A 128-bit integer vector containing control words corresponding to
///    positions in the destination.
/// \returns A 128-bit integer vector containing the resultant values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sign_epi16(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_psignw128((__v8hi)__a, (__v8hi)__b);
}

/// For each 32-bit integer in the first source operand, perform one of
///    the following actions as specified by the second source operand.
///
///    If the doubleword in the second source is negative, calculate the two's
///    complement of the corresponding word in the first source, and write that
///    value to the destination. If the doubleword in the second source is
///    positive, copy the corresponding word from the first source to the
///    destination. If the doubleword in the second source is zero, clear the
///    corresponding word in the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VPSIGND instruction.
///
/// \param __a
///    A 128-bit integer vector containing the values to be copied.
/// \param __b
///    A 128-bit integer vector containing control doublewords corresponding to
///    positions in the destination.
/// \returns A 128-bit integer vector containing the resultant values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sign_epi32(__m128i __a, __m128i __b)
{
    return (__m128i)__builtin_ia32_psignd128((__v4si)__a, (__v4si)__b);
}

/// For each 8-bit integer in the first source operand, perform one of
///    the following actions as specified by the second source operand.
///
///    If the byte in the second source is negative, calculate the two's
///    complement of the corresponding byte in the first source, and write that
///    value to the destination. If the byte in the second source is positive,
///    copy the corresponding byte from the first source to the destination. If
///    the byte in the second source is zero, clear the corresponding byte in
///    the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PSIGNB instruction.
///
/// \param __a
///    A 64-bit integer vector containing the values to be copied.
/// \param __b
///    A 64-bit integer vector containing control bytes corresponding to
///    positions in the destination.
/// \returns A 64-bit integer vector containing the resultant values.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_sign_pi8(__m64 __a, __m64 __b)
{
    return (__m64)__builtin_ia32_psignb((__v8qi)__a, (__v8qi)__b);
}

/// For each 16-bit integer in the first source operand, perform one of
///    the following actions as specified by the second source operand.
///
///    If the word in the second source is negative, calculate the two's
///    complement of the corresponding word in the first source, and write that
///    value to the destination. If the word in the second source is positive,
///    copy the corresponding word from the first source to the destination. If
///    the word in the second source is zero, clear the corresponding word in
///    the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PSIGNW instruction.
///
/// \param __a
///    A 64-bit integer vector containing the values to be copied.
/// \param __b
///    A 64-bit integer vector containing control words corresponding to
///    positions in the destination.
/// \returns A 64-bit integer vector containing the resultant values.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_sign_pi16(__m64 __a, __m64 __b)
{
    return (__m64)__builtin_ia32_psignw((__v4hi)__a, (__v4hi)__b);
}

/// For each 32-bit integer in the first source operand, perform one of
///    the following actions as specified by the second source operand.
///
///    If the doubleword in the second source is negative, calculate the two's
///    complement of the corresponding doubleword in the first source, and
///    write that value to the destination. If the doubleword in the second
///    source is positive, copy the corresponding doubleword from the first
///    source to the destination. If the doubleword in the second source is
///    zero, clear the corresponding doubleword in the destination.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PSIGND instruction.
///
/// \param __a
///    A 64-bit integer vector containing the values to be copied.
/// \param __b
///    A 64-bit integer vector containing two control doublewords corresponding
///    to positions in the destination.
/// \returns A 64-bit integer vector containing the resultant values.
static __inline__ __m64 __DEFAULT_FN_ATTRS_MMX
_mm_sign_pi32(__m64 __a, __m64 __b)
{
    return (__m64)__builtin_ia32_psignd((__v2si)__a, (__v2si)__b);
}

#undef __DEFAULT_FN_ATTRS
#undef __DEFAULT_FN_ATTRS_MMX

#endif /* __TMMINTRIN_H */
