/*===---- lzcntintrin.h - LZCNT intrinsics ---------------------------------===
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

#if !defined __X86INTRIN_H && !defined __IMMINTRIN_H
#error "Never use <lzcntintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __LZCNTINTRIN_H
#define __LZCNTINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("lzcnt")))

#ifndef _MSC_VER
/// Counts the number of leading zero bits in the operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c LZCNT instruction.
///
/// \param __X
///    An unsigned 16-bit integer whose leading zeros are to be counted.
/// \returns An unsigned 16-bit integer containing the number of leading zero
///    bits in the operand.
#define __lzcnt16(X) __builtin_ia32_lzcnt_u16((unsigned short)(X))
#endif // _MSC_VER

/// Counts the number of leading zero bits in the operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c LZCNT instruction.
///
/// \param __X
///    An unsigned 32-bit integer whose leading zeros are to be counted.
/// \returns An unsigned 32-bit integer containing the number of leading zero
///    bits in the operand.
/// \see _lzcnt_u32
static __inline__ unsigned int __DEFAULT_FN_ATTRS
__lzcnt32(unsigned int __X)
{
  return __builtin_ia32_lzcnt_u32(__X);
}

/// Counts the number of leading zero bits in the operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c LZCNT instruction.
///
/// \param __X
///    An unsigned 32-bit integer whose leading zeros are to be counted.
/// \returns An unsigned 32-bit integer containing the number of leading zero
///    bits in the operand.
/// \see __lzcnt32
static __inline__ unsigned int __DEFAULT_FN_ATTRS
_lzcnt_u32(unsigned int __X)
{
  return __builtin_ia32_lzcnt_u32(__X);
}

#ifdef __x86_64__
#ifndef _MSC_VER
/// Counts the number of leading zero bits in the operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c LZCNT instruction.
///
/// \param __X
///    An unsigned 64-bit integer whose leading zeros are to be counted.
/// \returns An unsigned 64-bit integer containing the number of leading zero
///    bits in the operand.
/// \see _lzcnt_u64
#define __lzcnt64(X) __builtin_ia32_lzcnt_u64((unsigned long long)(X))
#endif // _MSC_VER

/// Counts the number of leading zero bits in the operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c LZCNT instruction.
///
/// \param __X
///    An unsigned 64-bit integer whose leading zeros are to be counted.
/// \returns An unsigned 64-bit integer containing the number of leading zero
///    bits in the operand.
/// \see __lzcnt64
static __inline__ unsigned long long __DEFAULT_FN_ATTRS
_lzcnt_u64(unsigned long long __X)
{
  return __builtin_ia32_lzcnt_u64(__X);
}
#endif

#undef __DEFAULT_FN_ATTRS

#endif /* __LZCNTINTRIN_H */
