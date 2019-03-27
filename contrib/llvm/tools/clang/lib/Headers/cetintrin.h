/*===---- cetintrin.h - CET intrinsic --------------------------------------===
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
#error "Never use <cetintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __CETINTRIN_H
#define __CETINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS                                                     \
  __attribute__((__always_inline__, __nodebug__, __target__("shstk")))

static __inline__ void __DEFAULT_FN_ATTRS _incsspd(int __a) {
  __builtin_ia32_incsspd(__a);
}

#ifdef __x86_64__
static __inline__ void __DEFAULT_FN_ATTRS _incsspq(unsigned long long __a) {
  __builtin_ia32_incsspq(__a);
}
#endif /* __x86_64__ */

#ifdef __x86_64__
static __inline__ void __DEFAULT_FN_ATTRS _inc_ssp(unsigned int __a) {
  __builtin_ia32_incsspq(__a);
}
#else /* __x86_64__ */
static __inline__ void __DEFAULT_FN_ATTRS _inc_ssp(unsigned int __a) {
  __builtin_ia32_incsspd((int)__a);
}
#endif /* __x86_64__ */

static __inline__ unsigned int __DEFAULT_FN_ATTRS _rdsspd(unsigned int __a) {
  return __builtin_ia32_rdsspd(__a);
}

#ifdef __x86_64__
static __inline__ unsigned long long __DEFAULT_FN_ATTRS _rdsspq(unsigned long long __a) {
  return __builtin_ia32_rdsspq(__a);
}
#endif /* __x86_64__ */

#ifdef __x86_64__
static __inline__ unsigned long long __DEFAULT_FN_ATTRS _get_ssp(void) {
  return __builtin_ia32_rdsspq(0);
}
#else /* __x86_64__ */
static __inline__ unsigned int __DEFAULT_FN_ATTRS _get_ssp(void) {
  return __builtin_ia32_rdsspd(0);
}
#endif /* __x86_64__ */

static __inline__ void __DEFAULT_FN_ATTRS _saveprevssp() {
  __builtin_ia32_saveprevssp();
}

static __inline__ void __DEFAULT_FN_ATTRS _rstorssp(void * __p) {
  __builtin_ia32_rstorssp(__p);
}

static __inline__ void __DEFAULT_FN_ATTRS _wrssd(unsigned int __a, void * __p) {
  __builtin_ia32_wrssd(__a, __p);
}

#ifdef __x86_64__
static __inline__ void __DEFAULT_FN_ATTRS _wrssq(unsigned long long __a, void * __p) {
  __builtin_ia32_wrssq(__a, __p);
}
#endif /* __x86_64__ */

static __inline__ void __DEFAULT_FN_ATTRS _wrussd(unsigned int __a, void * __p) {
  __builtin_ia32_wrussd(__a, __p);
}

#ifdef __x86_64__
static __inline__ void __DEFAULT_FN_ATTRS _wrussq(unsigned long long __a, void * __p) {
  __builtin_ia32_wrussq(__a, __p);
}
#endif /* __x86_64__ */

static __inline__ void __DEFAULT_FN_ATTRS _setssbsy() {
  __builtin_ia32_setssbsy();
}

static __inline__ void __DEFAULT_FN_ATTRS _clrssbsy(void * __p) {
  __builtin_ia32_clrssbsy(__p);
}

#undef __DEFAULT_FN_ATTRS

#endif /* __CETINTRIN_H */
