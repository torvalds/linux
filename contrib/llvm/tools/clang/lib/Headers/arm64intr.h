/*===---- arm64intr.h - ARM64 Windows intrinsics -------------------------------===
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

/* Only include this if we're compiling for the windows platform. */
#ifndef _MSC_VER
#include_next <arm64intr.h>
#else

#ifndef __ARM64INTR_H
#define __ARM64INTR_H

typedef enum
{
  _ARM64_BARRIER_SY    = 0xF,
  _ARM64_BARRIER_ST    = 0xE,
  _ARM64_BARRIER_LD    = 0xD,
  _ARM64_BARRIER_ISH   = 0xB,
  _ARM64_BARRIER_ISHST = 0xA,
  _ARM64_BARRIER_ISHLD = 0x9,
  _ARM64_BARRIER_NSH   = 0x7,
  _ARM64_BARRIER_NSHST = 0x6,
  _ARM64_BARRIER_NSHLD = 0x5,
  _ARM64_BARRIER_OSH   = 0x3,
  _ARM64_BARRIER_OSHST = 0x2,
  _ARM64_BARRIER_OSHLD = 0x1
} _ARM64INTR_BARRIER_TYPE;

#endif /* __ARM64INTR_H */
#endif /* _MSC_VER */
