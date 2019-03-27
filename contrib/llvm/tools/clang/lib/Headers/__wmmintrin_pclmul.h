/*===---- __wmmintrin_pclmul.h - PCMUL intrinsics ---------------------------===
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

#ifndef __WMMINTRIN_H
#error "Never use <__wmmintrin_pclmul.h> directly; include <wmmintrin.h> instead."
#endif

#ifndef __WMMINTRIN_PCLMUL_H
#define __WMMINTRIN_PCLMUL_H

/// Multiplies two 64-bit integer values, which are selected from source
///    operands using the immediate-value operand. The multiplication is a
///    carry-less multiplication, and the 128-bit integer product is stored in
///    the destination.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128i _mm_clmulepi64_si128(__m128i __X, __m128i __Y, const int __I);
/// \endcode
///
/// This intrinsic corresponds to the <c> VPCLMULQDQ </c> instruction.
///
/// \param __X
///    A 128-bit vector of [2 x i64] containing one of the source operands.
/// \param __Y
///    A 128-bit vector of [2 x i64] containing one of the source operands.
/// \param __I
///    An immediate value specifying which 64-bit values to select from the
///    operands. Bit 0 is used to select a value from operand \a __X, and bit
///    4 is used to select a value from operand \a __Y: \n
///    Bit[0]=0 indicates that bits[63:0] of operand \a __X are used. \n
///    Bit[0]=1 indicates that bits[127:64] of operand \a __X are used. \n
///    Bit[4]=0 indicates that bits[63:0] of operand \a __Y are used. \n
///    Bit[4]=1 indicates that bits[127:64] of operand \a __Y are used.
/// \returns The 128-bit integer vector containing the result of the carry-less
///    multiplication of the selected 64-bit values.
#define _mm_clmulepi64_si128(X, Y, I) \
  ((__m128i)__builtin_ia32_pclmulqdq128((__v2di)(__m128i)(X), \
                                        (__v2di)(__m128i)(Y), (char)(I)))

#endif /* __WMMINTRIN_PCLMUL_H */
