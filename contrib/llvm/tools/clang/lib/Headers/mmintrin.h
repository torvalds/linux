/*===---- mmintrin.h - MMX intrinsics --------------------------------------===
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

#ifndef __MMINTRIN_H
#define __MMINTRIN_H

typedef long long __m64 __attribute__((__vector_size__(8)));

typedef long long __v1di __attribute__((__vector_size__(8)));
typedef int __v2si __attribute__((__vector_size__(8)));
typedef short __v4hi __attribute__((__vector_size__(8)));
typedef char __v8qi __attribute__((__vector_size__(8)));

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("mmx"), __min_vector_width__(64)))

/// Clears the MMX state by setting the state of the x87 stack registers
///    to empty.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> EMMS </c> instruction.
///
static __inline__ void  __attribute__((__always_inline__, __nodebug__, __target__("mmx")))
_mm_empty(void)
{
    __builtin_ia32_emms();
}

/// Constructs a 64-bit integer vector, setting the lower 32 bits to the
///    value of the 32-bit integer parameter and setting the upper 32 bits to 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> MOVD </c> instruction.
///
/// \param __i
///    A 32-bit integer value.
/// \returns A 64-bit integer vector. The lower 32 bits contain the value of the
///    parameter. The upper 32 bits are set to 0.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_cvtsi32_si64(int __i)
{
    return (__m64)__builtin_ia32_vec_init_v2si(__i, 0);
}

/// Returns the lower 32 bits of a 64-bit integer vector as a 32-bit
///    signed integer.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> MOVD </c> instruction.
///
/// \param __m
///    A 64-bit integer vector.
/// \returns A 32-bit signed integer value containing the lower 32 bits of the
///    parameter.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_cvtsi64_si32(__m64 __m)
{
    return __builtin_ia32_vec_ext_v2si((__v2si)__m, 0);
}

/// Casts a 64-bit signed integer value into a 64-bit integer vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> MOVQ </c> instruction.
///
/// \param __i
///    A 64-bit signed integer.
/// \returns A 64-bit integer vector containing the same bitwise pattern as the
///    parameter.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_cvtsi64_m64(long long __i)
{
    return (__m64)__i;
}

/// Casts a 64-bit integer vector into a 64-bit signed integer value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> MOVQ </c> instruction.
///
/// \param __m
///    A 64-bit integer vector.
/// \returns A 64-bit signed integer containing the same bitwise pattern as the
///    parameter.
static __inline__ long long __DEFAULT_FN_ATTRS
_mm_cvtm64_si64(__m64 __m)
{
    return (long long)__m;
}

/// Converts 16-bit signed integers from both 64-bit integer vector
///    parameters of [4 x i16] into 8-bit signed integer values, and constructs
///    a 64-bit integer vector of [8 x i8] as the result. Positive values
///    greater than 0x7F are saturated to 0x7F. Negative values less than 0x80
///    are saturated to 0x80.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PACKSSWB </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [4 x i16]. Each 16-bit element is treated as a
///    16-bit signed integer and is converted to an 8-bit signed integer with
///    saturation. Positive values greater than 0x7F are saturated to 0x7F.
///    Negative values less than 0x80 are saturated to 0x80. The converted
///    [4 x i8] values are written to the lower 32 bits of the result.
/// \param __m2
///    A 64-bit integer vector of [4 x i16]. Each 16-bit element is treated as a
///    16-bit signed integer and is converted to an 8-bit signed integer with
///    saturation. Positive values greater than 0x7F are saturated to 0x7F.
///    Negative values less than 0x80 are saturated to 0x80. The converted
///    [4 x i8] values are written to the upper 32 bits of the result.
/// \returns A 64-bit integer vector of [8 x i8] containing the converted
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_packs_pi16(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_packsswb((__v4hi)__m1, (__v4hi)__m2);
}

/// Converts 32-bit signed integers from both 64-bit integer vector
///    parameters of [2 x i32] into 16-bit signed integer values, and constructs
///    a 64-bit integer vector of [4 x i16] as the result. Positive values
///    greater than 0x7FFF are saturated to 0x7FFF. Negative values less than
///    0x8000 are saturated to 0x8000.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PACKSSDW </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [2 x i32]. Each 32-bit element is treated as a
///    32-bit signed integer and is converted to a 16-bit signed integer with
///    saturation. Positive values greater than 0x7FFF are saturated to 0x7FFF.
///    Negative values less than 0x8000 are saturated to 0x8000. The converted
///    [2 x i16] values are written to the lower 32 bits of the result.
/// \param __m2
///    A 64-bit integer vector of [2 x i32]. Each 32-bit element is treated as a
///    32-bit signed integer and is converted to a 16-bit signed integer with
///    saturation. Positive values greater than 0x7FFF are saturated to 0x7FFF.
///    Negative values less than 0x8000 are saturated to 0x8000. The converted
///    [2 x i16] values are written to the upper 32 bits of the result.
/// \returns A 64-bit integer vector of [4 x i16] containing the converted
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_packs_pi32(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_packssdw((__v2si)__m1, (__v2si)__m2);
}

/// Converts 16-bit signed integers from both 64-bit integer vector
///    parameters of [4 x i16] into 8-bit unsigned integer values, and
///    constructs a 64-bit integer vector of [8 x i8] as the result. Values
///    greater than 0xFF are saturated to 0xFF. Values less than 0 are saturated
///    to 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PACKUSWB </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [4 x i16]. Each 16-bit element is treated as a
///    16-bit signed integer and is converted to an 8-bit unsigned integer with
///    saturation. Values greater than 0xFF are saturated to 0xFF. Values less
///    than 0 are saturated to 0. The converted [4 x i8] values are written to
///    the lower 32 bits of the result.
/// \param __m2
///    A 64-bit integer vector of [4 x i16]. Each 16-bit element is treated as a
///    16-bit signed integer and is converted to an 8-bit unsigned integer with
///    saturation. Values greater than 0xFF are saturated to 0xFF. Values less
///    than 0 are saturated to 0. The converted [4 x i8] values are written to
///    the upper 32 bits of the result.
/// \returns A 64-bit integer vector of [8 x i8] containing the converted
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_packs_pu16(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_packuswb((__v4hi)__m1, (__v4hi)__m2);
}

/// Unpacks the upper 32 bits from two 64-bit integer vectors of [8 x i8]
///    and interleaves them into a 64-bit integer vector of [8 x i8].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PUNPCKHBW </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [8 x i8]. \n
///    Bits [39:32] are written to bits [7:0] of the result. \n
///    Bits [47:40] are written to bits [23:16] of the result. \n
///    Bits [55:48] are written to bits [39:32] of the result. \n
///    Bits [63:56] are written to bits [55:48] of the result.
/// \param __m2
///    A 64-bit integer vector of [8 x i8].
///    Bits [39:32] are written to bits [15:8] of the result. \n
///    Bits [47:40] are written to bits [31:24] of the result. \n
///    Bits [55:48] are written to bits [47:40] of the result. \n
///    Bits [63:56] are written to bits [63:56] of the result.
/// \returns A 64-bit integer vector of [8 x i8] containing the interleaved
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_unpackhi_pi8(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_punpckhbw((__v8qi)__m1, (__v8qi)__m2);
}

/// Unpacks the upper 32 bits from two 64-bit integer vectors of
///    [4 x i16] and interleaves them into a 64-bit integer vector of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PUNPCKHWD </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [4 x i16].
///    Bits [47:32] are written to bits [15:0] of the result. \n
///    Bits [63:48] are written to bits [47:32] of the result.
/// \param __m2
///    A 64-bit integer vector of [4 x i16].
///    Bits [47:32] are written to bits [31:16] of the result. \n
///    Bits [63:48] are written to bits [63:48] of the result.
/// \returns A 64-bit integer vector of [4 x i16] containing the interleaved
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_unpackhi_pi16(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_punpckhwd((__v4hi)__m1, (__v4hi)__m2);
}

/// Unpacks the upper 32 bits from two 64-bit integer vectors of
///    [2 x i32] and interleaves them into a 64-bit integer vector of [2 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PUNPCKHDQ </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [2 x i32]. The upper 32 bits are written to
///    the lower 32 bits of the result.
/// \param __m2
///    A 64-bit integer vector of [2 x i32]. The upper 32 bits are written to
///    the upper 32 bits of the result.
/// \returns A 64-bit integer vector of [2 x i32] containing the interleaved
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_unpackhi_pi32(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_punpckhdq((__v2si)__m1, (__v2si)__m2);
}

/// Unpacks the lower 32 bits from two 64-bit integer vectors of [8 x i8]
///    and interleaves them into a 64-bit integer vector of [8 x i8].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PUNPCKLBW </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [8 x i8].
///    Bits [7:0] are written to bits [7:0] of the result. \n
///    Bits [15:8] are written to bits [23:16] of the result. \n
///    Bits [23:16] are written to bits [39:32] of the result. \n
///    Bits [31:24] are written to bits [55:48] of the result.
/// \param __m2
///    A 64-bit integer vector of [8 x i8].
///    Bits [7:0] are written to bits [15:8] of the result. \n
///    Bits [15:8] are written to bits [31:24] of the result. \n
///    Bits [23:16] are written to bits [47:40] of the result. \n
///    Bits [31:24] are written to bits [63:56] of the result.
/// \returns A 64-bit integer vector of [8 x i8] containing the interleaved
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_unpacklo_pi8(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_punpcklbw((__v8qi)__m1, (__v8qi)__m2);
}

/// Unpacks the lower 32 bits from two 64-bit integer vectors of
///    [4 x i16] and interleaves them into a 64-bit integer vector of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PUNPCKLWD </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [4 x i16].
///    Bits [15:0] are written to bits [15:0] of the result. \n
///    Bits [31:16] are written to bits [47:32] of the result.
/// \param __m2
///    A 64-bit integer vector of [4 x i16].
///    Bits [15:0] are written to bits [31:16] of the result. \n
///    Bits [31:16] are written to bits [63:48] of the result.
/// \returns A 64-bit integer vector of [4 x i16] containing the interleaved
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_unpacklo_pi16(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_punpcklwd((__v4hi)__m1, (__v4hi)__m2);
}

/// Unpacks the lower 32 bits from two 64-bit integer vectors of
///    [2 x i32] and interleaves them into a 64-bit integer vector of [2 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PUNPCKLDQ </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [2 x i32]. The lower 32 bits are written to
///    the lower 32 bits of the result.
/// \param __m2
///    A 64-bit integer vector of [2 x i32]. The lower 32 bits are written to
///    the upper 32 bits of the result.
/// \returns A 64-bit integer vector of [2 x i32] containing the interleaved
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_unpacklo_pi32(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_punpckldq((__v2si)__m1, (__v2si)__m2);
}

/// Adds each 8-bit integer element of the first 64-bit integer vector
///    of [8 x i8] to the corresponding 8-bit integer element of the second
///    64-bit integer vector of [8 x i8]. The lower 8 bits of the results are
///    packed into a 64-bit integer vector of [8 x i8].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PADDB </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [8 x i8].
/// \param __m2
///    A 64-bit integer vector of [8 x i8].
/// \returns A 64-bit integer vector of [8 x i8] containing the sums of both
///    parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_add_pi8(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_paddb((__v8qi)__m1, (__v8qi)__m2);
}

/// Adds each 16-bit integer element of the first 64-bit integer vector
///    of [4 x i16] to the corresponding 16-bit integer element of the second
///    64-bit integer vector of [4 x i16]. The lower 16 bits of the results are
///    packed into a 64-bit integer vector of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PADDW </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [4 x i16].
/// \param __m2
///    A 64-bit integer vector of [4 x i16].
/// \returns A 64-bit integer vector of [4 x i16] containing the sums of both
///    parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_add_pi16(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_paddw((__v4hi)__m1, (__v4hi)__m2);
}

/// Adds each 32-bit integer element of the first 64-bit integer vector
///    of [2 x i32] to the corresponding 32-bit integer element of the second
///    64-bit integer vector of [2 x i32]. The lower 32 bits of the results are
///    packed into a 64-bit integer vector of [2 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PADDD </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [2 x i32].
/// \param __m2
///    A 64-bit integer vector of [2 x i32].
/// \returns A 64-bit integer vector of [2 x i32] containing the sums of both
///    parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_add_pi32(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_paddd((__v2si)__m1, (__v2si)__m2);
}

/// Adds each 8-bit signed integer element of the first 64-bit integer
///    vector of [8 x i8] to the corresponding 8-bit signed integer element of
///    the second 64-bit integer vector of [8 x i8]. Positive sums greater than
///    0x7F are saturated to 0x7F. Negative sums less than 0x80 are saturated to
///    0x80. The results are packed into a 64-bit integer vector of [8 x i8].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PADDSB </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [8 x i8].
/// \param __m2
///    A 64-bit integer vector of [8 x i8].
/// \returns A 64-bit integer vector of [8 x i8] containing the saturated sums
///    of both parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_adds_pi8(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_paddsb((__v8qi)__m1, (__v8qi)__m2);
}

/// Adds each 16-bit signed integer element of the first 64-bit integer
///    vector of [4 x i16] to the corresponding 16-bit signed integer element of
///    the second 64-bit integer vector of [4 x i16]. Positive sums greater than
///    0x7FFF are saturated to 0x7FFF. Negative sums less than 0x8000 are
///    saturated to 0x8000. The results are packed into a 64-bit integer vector
///    of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PADDSW </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [4 x i16].
/// \param __m2
///    A 64-bit integer vector of [4 x i16].
/// \returns A 64-bit integer vector of [4 x i16] containing the saturated sums
///    of both parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_adds_pi16(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_paddsw((__v4hi)__m1, (__v4hi)__m2);
}

/// Adds each 8-bit unsigned integer element of the first 64-bit integer
///    vector of [8 x i8] to the corresponding 8-bit unsigned integer element of
///    the second 64-bit integer vector of [8 x i8]. Sums greater than 0xFF are
///    saturated to 0xFF. The results are packed into a 64-bit integer vector of
///    [8 x i8].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PADDUSB </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [8 x i8].
/// \param __m2
///    A 64-bit integer vector of [8 x i8].
/// \returns A 64-bit integer vector of [8 x i8] containing the saturated
///    unsigned sums of both parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_adds_pu8(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_paddusb((__v8qi)__m1, (__v8qi)__m2);
}

/// Adds each 16-bit unsigned integer element of the first 64-bit integer
///    vector of [4 x i16] to the corresponding 16-bit unsigned integer element
///    of the second 64-bit integer vector of [4 x i16]. Sums greater than
///    0xFFFF are saturated to 0xFFFF. The results are packed into a 64-bit
///    integer vector of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PADDUSW </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [4 x i16].
/// \param __m2
///    A 64-bit integer vector of [4 x i16].
/// \returns A 64-bit integer vector of [4 x i16] containing the saturated
///    unsigned sums of both parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_adds_pu16(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_paddusw((__v4hi)__m1, (__v4hi)__m2);
}

/// Subtracts each 8-bit integer element of the second 64-bit integer
///    vector of [8 x i8] from the corresponding 8-bit integer element of the
///    first 64-bit integer vector of [8 x i8]. The lower 8 bits of the results
///    are packed into a 64-bit integer vector of [8 x i8].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSUBB </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [8 x i8] containing the minuends.
/// \param __m2
///    A 64-bit integer vector of [8 x i8] containing the subtrahends.
/// \returns A 64-bit integer vector of [8 x i8] containing the differences of
///    both parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_sub_pi8(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_psubb((__v8qi)__m1, (__v8qi)__m2);
}

/// Subtracts each 16-bit integer element of the second 64-bit integer
///    vector of [4 x i16] from the corresponding 16-bit integer element of the
///    first 64-bit integer vector of [4 x i16]. The lower 16 bits of the
///    results are packed into a 64-bit integer vector of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSUBW </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [4 x i16] containing the minuends.
/// \param __m2
///    A 64-bit integer vector of [4 x i16] containing the subtrahends.
/// \returns A 64-bit integer vector of [4 x i16] containing the differences of
///    both parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_sub_pi16(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_psubw((__v4hi)__m1, (__v4hi)__m2);
}

/// Subtracts each 32-bit integer element of the second 64-bit integer
///    vector of [2 x i32] from the corresponding 32-bit integer element of the
///    first 64-bit integer vector of [2 x i32]. The lower 32 bits of the
///    results are packed into a 64-bit integer vector of [2 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSUBD </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [2 x i32] containing the minuends.
/// \param __m2
///    A 64-bit integer vector of [2 x i32] containing the subtrahends.
/// \returns A 64-bit integer vector of [2 x i32] containing the differences of
///    both parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_sub_pi32(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_psubd((__v2si)__m1, (__v2si)__m2);
}

/// Subtracts each 8-bit signed integer element of the second 64-bit
///    integer vector of [8 x i8] from the corresponding 8-bit signed integer
///    element of the first 64-bit integer vector of [8 x i8]. Positive results
///    greater than 0x7F are saturated to 0x7F. Negative results less than 0x80
///    are saturated to 0x80. The results are packed into a 64-bit integer
///    vector of [8 x i8].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSUBSB </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [8 x i8] containing the minuends.
/// \param __m2
///    A 64-bit integer vector of [8 x i8] containing the subtrahends.
/// \returns A 64-bit integer vector of [8 x i8] containing the saturated
///    differences of both parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_subs_pi8(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_psubsb((__v8qi)__m1, (__v8qi)__m2);
}

/// Subtracts each 16-bit signed integer element of the second 64-bit
///    integer vector of [4 x i16] from the corresponding 16-bit signed integer
///    element of the first 64-bit integer vector of [4 x i16]. Positive results
///    greater than 0x7FFF are saturated to 0x7FFF. Negative results less than
///    0x8000 are saturated to 0x8000. The results are packed into a 64-bit
///    integer vector of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSUBSW </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [4 x i16] containing the minuends.
/// \param __m2
///    A 64-bit integer vector of [4 x i16] containing the subtrahends.
/// \returns A 64-bit integer vector of [4 x i16] containing the saturated
///    differences of both parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_subs_pi16(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_psubsw((__v4hi)__m1, (__v4hi)__m2);
}

/// Subtracts each 8-bit unsigned integer element of the second 64-bit
///    integer vector of [8 x i8] from the corresponding 8-bit unsigned integer
///    element of the first 64-bit integer vector of [8 x i8].
///
///    If an element of the first vector is less than the corresponding element
///    of the second vector, the result is saturated to 0. The results are
///    packed into a 64-bit integer vector of [8 x i8].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSUBUSB </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [8 x i8] containing the minuends.
/// \param __m2
///    A 64-bit integer vector of [8 x i8] containing the subtrahends.
/// \returns A 64-bit integer vector of [8 x i8] containing the saturated
///    differences of both parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_subs_pu8(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_psubusb((__v8qi)__m1, (__v8qi)__m2);
}

/// Subtracts each 16-bit unsigned integer element of the second 64-bit
///    integer vector of [4 x i16] from the corresponding 16-bit unsigned
///    integer element of the first 64-bit integer vector of [4 x i16].
///
///    If an element of the first vector is less than the corresponding element
///    of the second vector, the result is saturated to 0. The results are
///    packed into a 64-bit integer vector of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSUBUSW </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [4 x i16] containing the minuends.
/// \param __m2
///    A 64-bit integer vector of [4 x i16] containing the subtrahends.
/// \returns A 64-bit integer vector of [4 x i16] containing the saturated
///    differences of both parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_subs_pu16(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_psubusw((__v4hi)__m1, (__v4hi)__m2);
}

/// Multiplies each 16-bit signed integer element of the first 64-bit
///    integer vector of [4 x i16] by the corresponding 16-bit signed integer
///    element of the second 64-bit integer vector of [4 x i16] and get four
///    32-bit products. Adds adjacent pairs of products to get two 32-bit sums.
///    The lower 32 bits of these two sums are packed into a 64-bit integer
///    vector of [2 x i32].
///
///    For example, bits [15:0] of both parameters are multiplied, bits [31:16]
///    of both parameters are multiplied, and the sum of both results is written
///    to bits [31:0] of the result.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PMADDWD </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [4 x i16].
/// \param __m2
///    A 64-bit integer vector of [4 x i16].
/// \returns A 64-bit integer vector of [2 x i32] containing the sums of
///    products of both parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_madd_pi16(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_pmaddwd((__v4hi)__m1, (__v4hi)__m2);
}

/// Multiplies each 16-bit signed integer element of the first 64-bit
///    integer vector of [4 x i16] by the corresponding 16-bit signed integer
///    element of the second 64-bit integer vector of [4 x i16]. Packs the upper
///    16 bits of the 32-bit products into a 64-bit integer vector of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PMULHW </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [4 x i16].
/// \param __m2
///    A 64-bit integer vector of [4 x i16].
/// \returns A 64-bit integer vector of [4 x i16] containing the upper 16 bits
///    of the products of both parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_mulhi_pi16(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_pmulhw((__v4hi)__m1, (__v4hi)__m2);
}

/// Multiplies each 16-bit signed integer element of the first 64-bit
///    integer vector of [4 x i16] by the corresponding 16-bit signed integer
///    element of the second 64-bit integer vector of [4 x i16]. Packs the lower
///    16 bits of the 32-bit products into a 64-bit integer vector of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PMULLW </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [4 x i16].
/// \param __m2
///    A 64-bit integer vector of [4 x i16].
/// \returns A 64-bit integer vector of [4 x i16] containing the lower 16 bits
///    of the products of both parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_mullo_pi16(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_pmullw((__v4hi)__m1, (__v4hi)__m2);
}

/// Left-shifts each 16-bit signed integer element of the first
///    parameter, which is a 64-bit integer vector of [4 x i16], by the number
///    of bits specified by the second parameter, which is a 64-bit integer. The
///    lower 16 bits of the results are packed into a 64-bit integer vector of
///    [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSLLW </c> instruction.
///
/// \param __m
///    A 64-bit integer vector of [4 x i16].
/// \param __count
///    A 64-bit integer vector interpreted as a single 64-bit integer.
/// \returns A 64-bit integer vector of [4 x i16] containing the left-shifted
///    values. If \a __count is greater or equal to 16, the result is set to all
///    0.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_sll_pi16(__m64 __m, __m64 __count)
{
    return (__m64)__builtin_ia32_psllw((__v4hi)__m, __count);
}

/// Left-shifts each 16-bit signed integer element of a 64-bit integer
///    vector of [4 x i16] by the number of bits specified by a 32-bit integer.
///    The lower 16 bits of the results are packed into a 64-bit integer vector
///    of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSLLW </c> instruction.
///
/// \param __m
///    A 64-bit integer vector of [4 x i16].
/// \param __count
///    A 32-bit integer value.
/// \returns A 64-bit integer vector of [4 x i16] containing the left-shifted
///    values. If \a __count is greater or equal to 16, the result is set to all
///    0.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_slli_pi16(__m64 __m, int __count)
{
    return (__m64)__builtin_ia32_psllwi((__v4hi)__m, __count);
}

/// Left-shifts each 32-bit signed integer element of the first
///    parameter, which is a 64-bit integer vector of [2 x i32], by the number
///    of bits specified by the second parameter, which is a 64-bit integer. The
///    lower 32 bits of the results are packed into a 64-bit integer vector of
///    [2 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSLLD </c> instruction.
///
/// \param __m
///    A 64-bit integer vector of [2 x i32].
/// \param __count
///    A 64-bit integer vector interpreted as a single 64-bit integer.
/// \returns A 64-bit integer vector of [2 x i32] containing the left-shifted
///    values. If \a __count is greater or equal to 32, the result is set to all
///    0.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_sll_pi32(__m64 __m, __m64 __count)
{
    return (__m64)__builtin_ia32_pslld((__v2si)__m, __count);
}

/// Left-shifts each 32-bit signed integer element of a 64-bit integer
///    vector of [2 x i32] by the number of bits specified by a 32-bit integer.
///    The lower 32 bits of the results are packed into a 64-bit integer vector
///    of [2 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSLLD </c> instruction.
///
/// \param __m
///    A 64-bit integer vector of [2 x i32].
/// \param __count
///    A 32-bit integer value.
/// \returns A 64-bit integer vector of [2 x i32] containing the left-shifted
///    values. If \a __count is greater or equal to 32, the result is set to all
///    0.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_slli_pi32(__m64 __m, int __count)
{
    return (__m64)__builtin_ia32_pslldi((__v2si)__m, __count);
}

/// Left-shifts the first 64-bit integer parameter by the number of bits
///    specified by the second 64-bit integer parameter. The lower 64 bits of
///    result are returned.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSLLQ </c> instruction.
///
/// \param __m
///    A 64-bit integer vector interpreted as a single 64-bit integer.
/// \param __count
///    A 64-bit integer vector interpreted as a single 64-bit integer.
/// \returns A 64-bit integer vector containing the left-shifted value. If
///     \a __count is greater or equal to 64, the result is set to 0.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_sll_si64(__m64 __m, __m64 __count)
{
    return (__m64)__builtin_ia32_psllq((__v1di)__m, __count);
}

/// Left-shifts the first parameter, which is a 64-bit integer, by the
///    number of bits specified by the second parameter, which is a 32-bit
///    integer. The lower 64 bits of result are returned.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSLLQ </c> instruction.
///
/// \param __m
///    A 64-bit integer vector interpreted as a single 64-bit integer.
/// \param __count
///    A 32-bit integer value.
/// \returns A 64-bit integer vector containing the left-shifted value. If
///     \a __count is greater or equal to 64, the result is set to 0.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_slli_si64(__m64 __m, int __count)
{
    return (__m64)__builtin_ia32_psllqi((__v1di)__m, __count);
}

/// Right-shifts each 16-bit integer element of the first parameter,
///    which is a 64-bit integer vector of [4 x i16], by the number of bits
///    specified by the second parameter, which is a 64-bit integer.
///
///    High-order bits are filled with the sign bit of the initial value of each
///    16-bit element. The 16-bit results are packed into a 64-bit integer
///    vector of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSRAW </c> instruction.
///
/// \param __m
///    A 64-bit integer vector of [4 x i16].
/// \param __count
///    A 64-bit integer vector interpreted as a single 64-bit integer.
/// \returns A 64-bit integer vector of [4 x i16] containing the right-shifted
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_sra_pi16(__m64 __m, __m64 __count)
{
    return (__m64)__builtin_ia32_psraw((__v4hi)__m, __count);
}

/// Right-shifts each 16-bit integer element of a 64-bit integer vector
///    of [4 x i16] by the number of bits specified by a 32-bit integer.
///
///    High-order bits are filled with the sign bit of the initial value of each
///    16-bit element. The 16-bit results are packed into a 64-bit integer
///    vector of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSRAW </c> instruction.
///
/// \param __m
///    A 64-bit integer vector of [4 x i16].
/// \param __count
///    A 32-bit integer value.
/// \returns A 64-bit integer vector of [4 x i16] containing the right-shifted
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_srai_pi16(__m64 __m, int __count)
{
    return (__m64)__builtin_ia32_psrawi((__v4hi)__m, __count);
}

/// Right-shifts each 32-bit integer element of the first parameter,
///    which is a 64-bit integer vector of [2 x i32], by the number of bits
///    specified by the second parameter, which is a 64-bit integer.
///
///    High-order bits are filled with the sign bit of the initial value of each
///    32-bit element. The 32-bit results are packed into a 64-bit integer
///    vector of [2 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSRAD </c> instruction.
///
/// \param __m
///    A 64-bit integer vector of [2 x i32].
/// \param __count
///    A 64-bit integer vector interpreted as a single 64-bit integer.
/// \returns A 64-bit integer vector of [2 x i32] containing the right-shifted
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_sra_pi32(__m64 __m, __m64 __count)
{
    return (__m64)__builtin_ia32_psrad((__v2si)__m, __count);
}

/// Right-shifts each 32-bit integer element of a 64-bit integer vector
///    of [2 x i32] by the number of bits specified by a 32-bit integer.
///
///    High-order bits are filled with the sign bit of the initial value of each
///    32-bit element. The 32-bit results are packed into a 64-bit integer
///    vector of [2 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSRAD </c> instruction.
///
/// \param __m
///    A 64-bit integer vector of [2 x i32].
/// \param __count
///    A 32-bit integer value.
/// \returns A 64-bit integer vector of [2 x i32] containing the right-shifted
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_srai_pi32(__m64 __m, int __count)
{
    return (__m64)__builtin_ia32_psradi((__v2si)__m, __count);
}

/// Right-shifts each 16-bit integer element of the first parameter,
///    which is a 64-bit integer vector of [4 x i16], by the number of bits
///    specified by the second parameter, which is a 64-bit integer.
///
///    High-order bits are cleared. The 16-bit results are packed into a 64-bit
///    integer vector of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSRLW </c> instruction.
///
/// \param __m
///    A 64-bit integer vector of [4 x i16].
/// \param __count
///    A 64-bit integer vector interpreted as a single 64-bit integer.
/// \returns A 64-bit integer vector of [4 x i16] containing the right-shifted
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_srl_pi16(__m64 __m, __m64 __count)
{
    return (__m64)__builtin_ia32_psrlw((__v4hi)__m, __count);
}

/// Right-shifts each 16-bit integer element of a 64-bit integer vector
///    of [4 x i16] by the number of bits specified by a 32-bit integer.
///
///    High-order bits are cleared. The 16-bit results are packed into a 64-bit
///    integer vector of [4 x i16].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSRLW </c> instruction.
///
/// \param __m
///    A 64-bit integer vector of [4 x i16].
/// \param __count
///    A 32-bit integer value.
/// \returns A 64-bit integer vector of [4 x i16] containing the right-shifted
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_srli_pi16(__m64 __m, int __count)
{
    return (__m64)__builtin_ia32_psrlwi((__v4hi)__m, __count);
}

/// Right-shifts each 32-bit integer element of the first parameter,
///    which is a 64-bit integer vector of [2 x i32], by the number of bits
///    specified by the second parameter, which is a 64-bit integer.
///
///    High-order bits are cleared. The 32-bit results are packed into a 64-bit
///    integer vector of [2 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSRLD </c> instruction.
///
/// \param __m
///    A 64-bit integer vector of [2 x i32].
/// \param __count
///    A 64-bit integer vector interpreted as a single 64-bit integer.
/// \returns A 64-bit integer vector of [2 x i32] containing the right-shifted
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_srl_pi32(__m64 __m, __m64 __count)
{
    return (__m64)__builtin_ia32_psrld((__v2si)__m, __count);
}

/// Right-shifts each 32-bit integer element of a 64-bit integer vector
///    of [2 x i32] by the number of bits specified by a 32-bit integer.
///
///    High-order bits are cleared. The 32-bit results are packed into a 64-bit
///    integer vector of [2 x i32].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSRLD </c> instruction.
///
/// \param __m
///    A 64-bit integer vector of [2 x i32].
/// \param __count
///    A 32-bit integer value.
/// \returns A 64-bit integer vector of [2 x i32] containing the right-shifted
///    values.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_srli_pi32(__m64 __m, int __count)
{
    return (__m64)__builtin_ia32_psrldi((__v2si)__m, __count);
}

/// Right-shifts the first 64-bit integer parameter by the number of bits
///    specified by the second 64-bit integer parameter.
///
///    High-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSRLQ </c> instruction.
///
/// \param __m
///    A 64-bit integer vector interpreted as a single 64-bit integer.
/// \param __count
///    A 64-bit integer vector interpreted as a single 64-bit integer.
/// \returns A 64-bit integer vector containing the right-shifted value.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_srl_si64(__m64 __m, __m64 __count)
{
    return (__m64)__builtin_ia32_psrlq((__v1di)__m, __count);
}

/// Right-shifts the first parameter, which is a 64-bit integer, by the
///    number of bits specified by the second parameter, which is a 32-bit
///    integer.
///
///    High-order bits are cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PSRLQ </c> instruction.
///
/// \param __m
///    A 64-bit integer vector interpreted as a single 64-bit integer.
/// \param __count
///    A 32-bit integer value.
/// \returns A 64-bit integer vector containing the right-shifted value.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_srli_si64(__m64 __m, int __count)
{
    return (__m64)__builtin_ia32_psrlqi((__v1di)__m, __count);
}

/// Performs a bitwise AND of two 64-bit integer vectors.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PAND </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector.
/// \param __m2
///    A 64-bit integer vector.
/// \returns A 64-bit integer vector containing the bitwise AND of both
///    parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_and_si64(__m64 __m1, __m64 __m2)
{
    return __builtin_ia32_pand((__v1di)__m1, (__v1di)__m2);
}

/// Performs a bitwise NOT of the first 64-bit integer vector, and then
///    performs a bitwise AND of the intermediate result and the second 64-bit
///    integer vector.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PANDN </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector. The one's complement of this parameter is used
///    in the bitwise AND.
/// \param __m2
///    A 64-bit integer vector.
/// \returns A 64-bit integer vector containing the bitwise AND of the second
///    parameter and the one's complement of the first parameter.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_andnot_si64(__m64 __m1, __m64 __m2)
{
    return __builtin_ia32_pandn((__v1di)__m1, (__v1di)__m2);
}

/// Performs a bitwise OR of two 64-bit integer vectors.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> POR </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector.
/// \param __m2
///    A 64-bit integer vector.
/// \returns A 64-bit integer vector containing the bitwise OR of both
///    parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_or_si64(__m64 __m1, __m64 __m2)
{
    return __builtin_ia32_por((__v1di)__m1, (__v1di)__m2);
}

/// Performs a bitwise exclusive OR of two 64-bit integer vectors.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PXOR </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector.
/// \param __m2
///    A 64-bit integer vector.
/// \returns A 64-bit integer vector containing the bitwise exclusive OR of both
///    parameters.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_xor_si64(__m64 __m1, __m64 __m2)
{
    return __builtin_ia32_pxor((__v1di)__m1, (__v1di)__m2);
}

/// Compares the 8-bit integer elements of two 64-bit integer vectors of
///    [8 x i8] to determine if the element of the first vector is equal to the
///    corresponding element of the second vector.
///
///    The comparison yields 0 for false, 0xFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PCMPEQB </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [8 x i8].
/// \param __m2
///    A 64-bit integer vector of [8 x i8].
/// \returns A 64-bit integer vector of [8 x i8] containing the comparison
///    results.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_cmpeq_pi8(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_pcmpeqb((__v8qi)__m1, (__v8qi)__m2);
}

/// Compares the 16-bit integer elements of two 64-bit integer vectors of
///    [4 x i16] to determine if the element of the first vector is equal to the
///    corresponding element of the second vector.
///
///    The comparison yields 0 for false, 0xFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PCMPEQW </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [4 x i16].
/// \param __m2
///    A 64-bit integer vector of [4 x i16].
/// \returns A 64-bit integer vector of [4 x i16] containing the comparison
///    results.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_cmpeq_pi16(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_pcmpeqw((__v4hi)__m1, (__v4hi)__m2);
}

/// Compares the 32-bit integer elements of two 64-bit integer vectors of
///    [2 x i32] to determine if the element of the first vector is equal to the
///    corresponding element of the second vector.
///
///    The comparison yields 0 for false, 0xFFFFFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PCMPEQD </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [2 x i32].
/// \param __m2
///    A 64-bit integer vector of [2 x i32].
/// \returns A 64-bit integer vector of [2 x i32] containing the comparison
///    results.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_cmpeq_pi32(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_pcmpeqd((__v2si)__m1, (__v2si)__m2);
}

/// Compares the 8-bit integer elements of two 64-bit integer vectors of
///    [8 x i8] to determine if the element of the first vector is greater than
///    the corresponding element of the second vector.
///
///    The comparison yields 0 for false, 0xFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PCMPGTB </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [8 x i8].
/// \param __m2
///    A 64-bit integer vector of [8 x i8].
/// \returns A 64-bit integer vector of [8 x i8] containing the comparison
///    results.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_cmpgt_pi8(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_pcmpgtb((__v8qi)__m1, (__v8qi)__m2);
}

/// Compares the 16-bit integer elements of two 64-bit integer vectors of
///    [4 x i16] to determine if the element of the first vector is greater than
///    the corresponding element of the second vector.
///
///    The comparison yields 0 for false, 0xFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PCMPGTW </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [4 x i16].
/// \param __m2
///    A 64-bit integer vector of [4 x i16].
/// \returns A 64-bit integer vector of [4 x i16] containing the comparison
///    results.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_cmpgt_pi16(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_pcmpgtw((__v4hi)__m1, (__v4hi)__m2);
}

/// Compares the 32-bit integer elements of two 64-bit integer vectors of
///    [2 x i32] to determine if the element of the first vector is greater than
///    the corresponding element of the second vector.
///
///    The comparison yields 0 for false, 0xFFFFFFFF for true.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PCMPGTD </c> instruction.
///
/// \param __m1
///    A 64-bit integer vector of [2 x i32].
/// \param __m2
///    A 64-bit integer vector of [2 x i32].
/// \returns A 64-bit integer vector of [2 x i32] containing the comparison
///    results.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_cmpgt_pi32(__m64 __m1, __m64 __m2)
{
    return (__m64)__builtin_ia32_pcmpgtd((__v2si)__m1, (__v2si)__m2);
}

/// Constructs a 64-bit integer vector initialized to zero.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> PXOR </c> instruction.
///
/// \returns An initialized 64-bit integer vector with all elements set to zero.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_setzero_si64(void)
{
    return __extension__ (__m64){ 0LL };
}

/// Constructs a 64-bit integer vector initialized with the specified
///    32-bit integer values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __i1
///    A 32-bit integer value used to initialize the upper 32 bits of the
///    result.
/// \param __i0
///    A 32-bit integer value used to initialize the lower 32 bits of the
///    result.
/// \returns An initialized 64-bit integer vector.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_set_pi32(int __i1, int __i0)
{
    return (__m64)__builtin_ia32_vec_init_v2si(__i0, __i1);
}

/// Constructs a 64-bit integer vector initialized with the specified
///    16-bit integer values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __s3
///    A 16-bit integer value used to initialize bits [63:48] of the result.
/// \param __s2
///    A 16-bit integer value used to initialize bits [47:32] of the result.
/// \param __s1
///    A 16-bit integer value used to initialize bits [31:16] of the result.
/// \param __s0
///    A 16-bit integer value used to initialize bits [15:0] of the result.
/// \returns An initialized 64-bit integer vector.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_set_pi16(short __s3, short __s2, short __s1, short __s0)
{
    return (__m64)__builtin_ia32_vec_init_v4hi(__s0, __s1, __s2, __s3);
}

/// Constructs a 64-bit integer vector initialized with the specified
///    8-bit integer values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __b7
///    An 8-bit integer value used to initialize bits [63:56] of the result.
/// \param __b6
///    An 8-bit integer value used to initialize bits [55:48] of the result.
/// \param __b5
///    An 8-bit integer value used to initialize bits [47:40] of the result.
/// \param __b4
///    An 8-bit integer value used to initialize bits [39:32] of the result.
/// \param __b3
///    An 8-bit integer value used to initialize bits [31:24] of the result.
/// \param __b2
///    An 8-bit integer value used to initialize bits [23:16] of the result.
/// \param __b1
///    An 8-bit integer value used to initialize bits [15:8] of the result.
/// \param __b0
///    An 8-bit integer value used to initialize bits [7:0] of the result.
/// \returns An initialized 64-bit integer vector.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_set_pi8(char __b7, char __b6, char __b5, char __b4, char __b3, char __b2,
            char __b1, char __b0)
{
    return (__m64)__builtin_ia32_vec_init_v8qi(__b0, __b1, __b2, __b3,
                                               __b4, __b5, __b6, __b7);
}

/// Constructs a 64-bit integer vector of [2 x i32], with each of the
///    32-bit integer vector elements set to the specified 32-bit integer
///    value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __i
///    A 32-bit integer value used to initialize each vector element of the
///    result.
/// \returns An initialized 64-bit integer vector of [2 x i32].
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_set1_pi32(int __i)
{
    return _mm_set_pi32(__i, __i);
}

/// Constructs a 64-bit integer vector of [4 x i16], with each of the
///    16-bit integer vector elements set to the specified 16-bit integer
///    value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __w
///    A 16-bit integer value used to initialize each vector element of the
///    result.
/// \returns An initialized 64-bit integer vector of [4 x i16].
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_set1_pi16(short __w)
{
    return _mm_set_pi16(__w, __w, __w, __w);
}

/// Constructs a 64-bit integer vector of [8 x i8], with each of the
///    8-bit integer vector elements set to the specified 8-bit integer value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __b
///    An 8-bit integer value used to initialize each vector element of the
///    result.
/// \returns An initialized 64-bit integer vector of [8 x i8].
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_set1_pi8(char __b)
{
    return _mm_set_pi8(__b, __b, __b, __b, __b, __b, __b, __b);
}

/// Constructs a 64-bit integer vector, initialized in reverse order with
///    the specified 32-bit integer values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __i0
///    A 32-bit integer value used to initialize the lower 32 bits of the
///    result.
/// \param __i1
///    A 32-bit integer value used to initialize the upper 32 bits of the
///    result.
/// \returns An initialized 64-bit integer vector.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_setr_pi32(int __i0, int __i1)
{
    return _mm_set_pi32(__i1, __i0);
}

/// Constructs a 64-bit integer vector, initialized in reverse order with
///    the specified 16-bit integer values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __w0
///    A 16-bit integer value used to initialize bits [15:0] of the result.
/// \param __w1
///    A 16-bit integer value used to initialize bits [31:16] of the result.
/// \param __w2
///    A 16-bit integer value used to initialize bits [47:32] of the result.
/// \param __w3
///    A 16-bit integer value used to initialize bits [63:48] of the result.
/// \returns An initialized 64-bit integer vector.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_setr_pi16(short __w0, short __w1, short __w2, short __w3)
{
    return _mm_set_pi16(__w3, __w2, __w1, __w0);
}

/// Constructs a 64-bit integer vector, initialized in reverse order with
///    the specified 8-bit integer values.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic is a utility function and does not correspond to a specific
///    instruction.
///
/// \param __b0
///    An 8-bit integer value used to initialize bits [7:0] of the result.
/// \param __b1
///    An 8-bit integer value used to initialize bits [15:8] of the result.
/// \param __b2
///    An 8-bit integer value used to initialize bits [23:16] of the result.
/// \param __b3
///    An 8-bit integer value used to initialize bits [31:24] of the result.
/// \param __b4
///    An 8-bit integer value used to initialize bits [39:32] of the result.
/// \param __b5
///    An 8-bit integer value used to initialize bits [47:40] of the result.
/// \param __b6
///    An 8-bit integer value used to initialize bits [55:48] of the result.
/// \param __b7
///    An 8-bit integer value used to initialize bits [63:56] of the result.
/// \returns An initialized 64-bit integer vector.
static __inline__ __m64 __DEFAULT_FN_ATTRS
_mm_setr_pi8(char __b0, char __b1, char __b2, char __b3, char __b4, char __b5,
             char __b6, char __b7)
{
    return _mm_set_pi8(__b7, __b6, __b5, __b4, __b3, __b2, __b1, __b0);
}

#undef __DEFAULT_FN_ATTRS

/* Aliases for compatibility. */
#define _m_empty _mm_empty
#define _m_from_int _mm_cvtsi32_si64
#define _m_from_int64 _mm_cvtsi64_m64
#define _m_to_int _mm_cvtsi64_si32
#define _m_to_int64 _mm_cvtm64_si64
#define _m_packsswb _mm_packs_pi16
#define _m_packssdw _mm_packs_pi32
#define _m_packuswb _mm_packs_pu16
#define _m_punpckhbw _mm_unpackhi_pi8
#define _m_punpckhwd _mm_unpackhi_pi16
#define _m_punpckhdq _mm_unpackhi_pi32
#define _m_punpcklbw _mm_unpacklo_pi8
#define _m_punpcklwd _mm_unpacklo_pi16
#define _m_punpckldq _mm_unpacklo_pi32
#define _m_paddb _mm_add_pi8
#define _m_paddw _mm_add_pi16
#define _m_paddd _mm_add_pi32
#define _m_paddsb _mm_adds_pi8
#define _m_paddsw _mm_adds_pi16
#define _m_paddusb _mm_adds_pu8
#define _m_paddusw _mm_adds_pu16
#define _m_psubb _mm_sub_pi8
#define _m_psubw _mm_sub_pi16
#define _m_psubd _mm_sub_pi32
#define _m_psubsb _mm_subs_pi8
#define _m_psubsw _mm_subs_pi16
#define _m_psubusb _mm_subs_pu8
#define _m_psubusw _mm_subs_pu16
#define _m_pmaddwd _mm_madd_pi16
#define _m_pmulhw _mm_mulhi_pi16
#define _m_pmullw _mm_mullo_pi16
#define _m_psllw _mm_sll_pi16
#define _m_psllwi _mm_slli_pi16
#define _m_pslld _mm_sll_pi32
#define _m_pslldi _mm_slli_pi32
#define _m_psllq _mm_sll_si64
#define _m_psllqi _mm_slli_si64
#define _m_psraw _mm_sra_pi16
#define _m_psrawi _mm_srai_pi16
#define _m_psrad _mm_sra_pi32
#define _m_psradi _mm_srai_pi32
#define _m_psrlw _mm_srl_pi16
#define _m_psrlwi _mm_srli_pi16
#define _m_psrld _mm_srl_pi32
#define _m_psrldi _mm_srli_pi32
#define _m_psrlq _mm_srl_si64
#define _m_psrlqi _mm_srli_si64
#define _m_pand _mm_and_si64
#define _m_pandn _mm_andnot_si64
#define _m_por _mm_or_si64
#define _m_pxor _mm_xor_si64
#define _m_pcmpeqb _mm_cmpeq_pi8
#define _m_pcmpeqw _mm_cmpeq_pi16
#define _m_pcmpeqd _mm_cmpeq_pi32
#define _m_pcmpgtb _mm_cmpgt_pi8
#define _m_pcmpgtw _mm_cmpgt_pi16
#define _m_pcmpgtd _mm_cmpgt_pi32

#endif /* __MMINTRIN_H */

