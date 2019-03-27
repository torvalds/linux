/*===---- bmi2intrin.h - BMI2 intrinsics -----------------------------------===
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
#error "Never use <bmi2intrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __BMI2INTRIN_H
#define __BMI2INTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("bmi2")))

static __inline__ unsigned int __DEFAULT_FN_ATTRS
_bzhi_u32(unsigned int __X, unsigned int __Y)
{
  return __builtin_ia32_bzhi_si(__X, __Y);
}

static __inline__ unsigned int __DEFAULT_FN_ATTRS
_pdep_u32(unsigned int __X, unsigned int __Y)
{
  return __builtin_ia32_pdep_si(__X, __Y);
}

static __inline__ unsigned int __DEFAULT_FN_ATTRS
_pext_u32(unsigned int __X, unsigned int __Y)
{
  return __builtin_ia32_pext_si(__X, __Y);
}

#ifdef  __x86_64__

static __inline__ unsigned long long __DEFAULT_FN_ATTRS
_bzhi_u64(unsigned long long __X, unsigned long long __Y)
{
  return __builtin_ia32_bzhi_di(__X, __Y);
}

static __inline__ unsigned long long __DEFAULT_FN_ATTRS
_pdep_u64(unsigned long long __X, unsigned long long __Y)
{
  return __builtin_ia32_pdep_di(__X, __Y);
}

static __inline__ unsigned long long __DEFAULT_FN_ATTRS
_pext_u64(unsigned long long __X, unsigned long long __Y)
{
  return __builtin_ia32_pext_di(__X, __Y);
}

static __inline__ unsigned long long __DEFAULT_FN_ATTRS
_mulx_u64 (unsigned long long __X, unsigned long long __Y,
	   unsigned long long *__P)
{
  unsigned __int128 __res = (unsigned __int128) __X * __Y;
  *__P = (unsigned long long) (__res >> 64);
  return (unsigned long long) __res;
}

#else /* !__x86_64__ */

static __inline__ unsigned int __DEFAULT_FN_ATTRS
_mulx_u32 (unsigned int __X, unsigned int __Y, unsigned int *__P)
{
  unsigned long long __res = (unsigned long long) __X * __Y;
  *__P = (unsigned int) (__res >> 32);
  return (unsigned int) __res;
}

#endif /* !__x86_64__  */

#undef __DEFAULT_FN_ATTRS

#endif /* __BMI2INTRIN_H */
