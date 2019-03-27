/*===---- adxintrin.h - ADX intrinsics -------------------------------------===
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

#ifndef __IMMINTRIN_H
#error "Never use <adxintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __ADXINTRIN_H
#define __ADXINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__))

/* Intrinsics that are available only if __ADX__ defined */
static __inline unsigned char __attribute__((__always_inline__, __nodebug__, __target__("adx")))
_addcarryx_u32(unsigned char __cf, unsigned int __x, unsigned int __y,
               unsigned int *__p)
{
  return __builtin_ia32_addcarryx_u32(__cf, __x, __y, __p);
}

#ifdef __x86_64__
static __inline unsigned char __attribute__((__always_inline__, __nodebug__, __target__("adx")))
_addcarryx_u64(unsigned char __cf, unsigned long long __x,
               unsigned long long __y, unsigned long long  *__p)
{
  return __builtin_ia32_addcarryx_u64(__cf, __x, __y, __p);
}
#endif

/* Intrinsics that are also available if __ADX__ undefined */
static __inline unsigned char __DEFAULT_FN_ATTRS
_addcarry_u32(unsigned char __cf, unsigned int __x, unsigned int __y,
              unsigned int *__p)
{
  return __builtin_ia32_addcarryx_u32(__cf, __x, __y, __p);
}

#ifdef __x86_64__
static __inline unsigned char __DEFAULT_FN_ATTRS
_addcarry_u64(unsigned char __cf, unsigned long long __x,
              unsigned long long __y, unsigned long long  *__p)
{
  return __builtin_ia32_addcarryx_u64(__cf, __x, __y, __p);
}
#endif

static __inline unsigned char __DEFAULT_FN_ATTRS
_subborrow_u32(unsigned char __cf, unsigned int __x, unsigned int __y,
              unsigned int *__p)
{
  return __builtin_ia32_subborrow_u32(__cf, __x, __y, __p);
}

#ifdef __x86_64__
static __inline unsigned char __DEFAULT_FN_ATTRS
_subborrow_u64(unsigned char __cf, unsigned long long __x,
               unsigned long long __y, unsigned long long  *__p)
{
  return __builtin_ia32_subborrow_u64(__cf, __x, __y, __p);
}
#endif

#undef __DEFAULT_FN_ATTRS

#endif /* __ADXINTRIN_H */
