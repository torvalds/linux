/*===---- popcntintrin.h - POPCNT intrinsics -------------------------------===
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

#ifndef __POPCNTINTRIN_H
#define __POPCNTINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("popcnt")))

/// Counts the number of bits in the source operand having a value of 1.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> POPCNT </c> instruction.
///
/// \param __A
///    An unsigned 32-bit integer operand.
/// \returns A 32-bit integer containing the number of bits with value 1 in the
///    source operand.
static __inline__ int __DEFAULT_FN_ATTRS
_mm_popcnt_u32(unsigned int __A)
{
  return __builtin_popcount(__A);
}

/// Counts the number of bits in the source operand having a value of 1.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> POPCNT </c> instruction.
///
/// \param __A
///    A signed 32-bit integer operand.
/// \returns A 32-bit integer containing the number of bits with value 1 in the
///    source operand.
static __inline__ int __DEFAULT_FN_ATTRS
_popcnt32(int __A)
{
  return __builtin_popcount(__A);
}

#ifdef __x86_64__
/// Counts the number of bits in the source operand having a value of 1.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> POPCNT </c> instruction.
///
/// \param __A
///    An unsigned 64-bit integer operand.
/// \returns A 64-bit integer containing the number of bits with value 1 in the
///    source operand.
static __inline__ long long __DEFAULT_FN_ATTRS
_mm_popcnt_u64(unsigned long long __A)
{
  return __builtin_popcountll(__A);
}

/// Counts the number of bits in the source operand having a value of 1.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> POPCNT </c> instruction.
///
/// \param __A
///    A signed 64-bit integer operand.
/// \returns A 64-bit integer containing the number of bits with value 1 in the
///    source operand.
static __inline__ long long __DEFAULT_FN_ATTRS
_popcnt64(long long __A)
{
  return __builtin_popcountll(__A);
}
#endif /* __x86_64__ */

#undef __DEFAULT_FN_ATTRS

#endif /* __POPCNTINTRIN_H */
