/*===------------ ptwriteintrin.h - PTWRITE intrinsic --------------------===
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
#error "Never use <ptwriteintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __PTWRITEINTRIN_H
#define __PTWRITEINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS \
  __attribute__((__always_inline__, __nodebug__,  __target__("ptwrite")))

static __inline__ void __DEFAULT_FN_ATTRS
_ptwrite32(unsigned int __value) {
  __builtin_ia32_ptwrite32(__value);
}

#ifdef __x86_64__

static __inline__ void __DEFAULT_FN_ATTRS
_ptwrite64(unsigned long long __value) {
  __builtin_ia32_ptwrite64(__value);
}

#endif /* __x86_64__ */

#undef __DEFAULT_FN_ATTRS

#endif /* __PTWRITEINTRIN_H */
