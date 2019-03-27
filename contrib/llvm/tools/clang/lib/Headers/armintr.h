/*===---- armintr.h - ARM Windows intrinsics -------------------------------===
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
#include_next <armintr.h>
#else

#ifndef __ARMINTR_H
#define __ARMINTR_H

typedef enum
{
  _ARM_BARRIER_SY    = 0xF,
  _ARM_BARRIER_ST    = 0xE,
  _ARM_BARRIER_ISH   = 0xB,
  _ARM_BARRIER_ISHST = 0xA,
  _ARM_BARRIER_NSH   = 0x7,
  _ARM_BARRIER_NSHST = 0x6,
  _ARM_BARRIER_OSH   = 0x3,
  _ARM_BARRIER_OSHST = 0x2
} _ARMINTR_BARRIER_TYPE;

#endif /* __ARMINTR_H */
#endif /* _MSC_VER */
