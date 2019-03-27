/*===---- pconfigintrin.h - X86 platform configuration ---------------------===
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
#error "Never use <pconfigintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __PCONFIGINTRIN_H
#define __PCONFIGINTRIN_H

#define __PCONFIG_KEY_PROGRAM 0x00000001

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS \
  __attribute__((__always_inline__, __nodebug__,  __target__("pconfig")))

static __inline unsigned int __DEFAULT_FN_ATTRS
_pconfig_u32(unsigned int __leaf, __SIZE_TYPE__ __d[])
{
  unsigned int __result;
  __asm__ ("pconfig"
           : "=a" (__result), "=b" (__d[0]), "=c" (__d[1]), "=d" (__d[2])
           : "a" (__leaf), "b" (__d[0]), "c" (__d[1]), "d" (__d[2])
           : "cc");
  return __result;
}

#undef __DEFAULT_FN_ATTRS

#endif
