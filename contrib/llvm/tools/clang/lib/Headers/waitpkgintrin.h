/*===----------------------- waitpkgintrin.h - WAITPKG --------------------===
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
#error "Never use <waitpkgintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __WAITPKGINTRIN_H
#define __WAITPKGINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS \
  __attribute__((__always_inline__, __nodebug__,  __target__("waitpkg")))

static __inline__ void __DEFAULT_FN_ATTRS
_umonitor (void * __address)
{
  __builtin_ia32_umonitor (__address);
}

static __inline__ unsigned char __DEFAULT_FN_ATTRS
_umwait (unsigned int __control, unsigned long long __counter)
{
  return __builtin_ia32_umwait (__control,
    (unsigned int)(__counter >> 32), (unsigned int)__counter);
}

static __inline__ unsigned char __DEFAULT_FN_ATTRS
_tpause (unsigned int __control, unsigned long long __counter)
{
  return __builtin_ia32_tpause (__control,
    (unsigned int)(__counter >> 32), (unsigned int)__counter);
}

#undef __DEFAULT_FN_ATTRS

#endif /* __WAITPKGINTRIN_H */
