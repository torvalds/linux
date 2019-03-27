/*===---- altivec.h - Standard header for type generic math ---------------===*\
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
\*===----------------------------------------------------------------------===*/

#ifndef __ALTIVEC_H
#define __ALTIVEC_H

#ifndef __ALTIVEC__
#error "AltiVec support not enabled"
#endif

/* Constants for mapping CR6 bits to predicate result. */

#define __CR6_EQ 0
#define __CR6_EQ_REV 1
#define __CR6_LT 2
#define __CR6_LT_REV 3

/* Constants for vec_test_data_class */
#define __VEC_CLASS_FP_SUBNORMAL_N (1 << 0)
#define __VEC_CLASS_FP_SUBNORMAL_P (1 << 1)
#define __VEC_CLASS_FP_SUBNORMAL (__VEC_CLASS_FP_SUBNORMAL_P | \
                                  __VEC_CLASS_FP_SUBNORMAL_N)
#define __VEC_CLASS_FP_ZERO_N (1<<2)
#define __VEC_CLASS_FP_ZERO_P (1<<3)
#define __VEC_CLASS_FP_ZERO (__VEC_CLASS_FP_ZERO_P           | \
                             __VEC_CLASS_FP_ZERO_N)
#define __VEC_CLASS_FP_INFINITY_N (1<<4)
#define __VEC_CLASS_FP_INFINITY_P (1<<5)
#define __VEC_CLASS_FP_INFINITY (__VEC_CLASS_FP_INFINITY_P   | \
                                 __VEC_CLASS_FP_INFINITY_N)
#define __VEC_CLASS_FP_NAN (1<<6)
#define __VEC_CLASS_FP_NOT_NORMAL (__VEC_CLASS_FP_NAN        | \
                                   __VEC_CLASS_FP_SUBNORMAL  | \
                                   __VEC_CLASS_FP_ZERO       | \
                                   __VEC_CLASS_FP_INFINITY)

#define __ATTRS_o_ai __attribute__((__overloadable__, __always_inline__))

#ifdef __POWER9_VECTOR__
#include <stddef.h>
#endif

static __inline__ vector signed char __ATTRS_o_ai vec_perm(
    vector signed char __a, vector signed char __b, vector unsigned char __c);

static __inline__ vector unsigned char __ATTRS_o_ai
vec_perm(vector unsigned char __a, vector unsigned char __b,
         vector unsigned char __c);

static __inline__ vector bool char __ATTRS_o_ai
vec_perm(vector bool char __a, vector bool char __b, vector unsigned char __c);

static __inline__ vector short __ATTRS_o_ai vec_perm(vector signed short __a,
                                                     vector signed short __b,
                                                     vector unsigned char __c);

static __inline__ vector unsigned short __ATTRS_o_ai
vec_perm(vector unsigned short __a, vector unsigned short __b,
         vector unsigned char __c);

static __inline__ vector bool short __ATTRS_o_ai vec_perm(
    vector bool short __a, vector bool short __b, vector unsigned char __c);

static __inline__ vector pixel __ATTRS_o_ai vec_perm(vector pixel __a,
                                                     vector pixel __b,
                                                     vector unsigned char __c);

static __inline__ vector int __ATTRS_o_ai vec_perm(vector signed int __a,
                                                   vector signed int __b,
                                                   vector unsigned char __c);

static __inline__ vector unsigned int __ATTRS_o_ai vec_perm(
    vector unsigned int __a, vector unsigned int __b, vector unsigned char __c);

static __inline__ vector bool int __ATTRS_o_ai
vec_perm(vector bool int __a, vector bool int __b, vector unsigned char __c);

static __inline__ vector float __ATTRS_o_ai vec_perm(vector float __a,
                                                     vector float __b,
                                                     vector unsigned char __c);

#ifdef __VSX__
static __inline__ vector long long __ATTRS_o_ai
vec_perm(vector signed long long __a, vector signed long long __b,
         vector unsigned char __c);

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_perm(vector unsigned long long __a, vector unsigned long long __b,
         vector unsigned char __c);

static __inline__ vector bool long long __ATTRS_o_ai
vec_perm(vector bool long long __a, vector bool long long __b,
         vector unsigned char __c);

static __inline__ vector double __ATTRS_o_ai vec_perm(vector double __a,
                                                      vector double __b,
                                                      vector unsigned char __c);
#endif

static __inline__ vector unsigned char __ATTRS_o_ai
vec_xor(vector unsigned char __a, vector unsigned char __b);

/* vec_abs */

#define __builtin_altivec_abs_v16qi vec_abs
#define __builtin_altivec_abs_v8hi vec_abs
#define __builtin_altivec_abs_v4si vec_abs

static __inline__ vector signed char __ATTRS_o_ai
vec_abs(vector signed char __a) {
  return __builtin_altivec_vmaxsb(__a, -__a);
}

static __inline__ vector signed short __ATTRS_o_ai
vec_abs(vector signed short __a) {
  return __builtin_altivec_vmaxsh(__a, -__a);
}

static __inline__ vector signed int __ATTRS_o_ai
vec_abs(vector signed int __a) {
  return __builtin_altivec_vmaxsw(__a, -__a);
}

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
static __inline__ vector signed long long __ATTRS_o_ai
vec_abs(vector signed long long __a) {
  return __builtin_altivec_vmaxsd(__a, -__a);
}
#endif

static __inline__ vector float __ATTRS_o_ai vec_abs(vector float __a) {
#ifdef __VSX__
  return __builtin_vsx_xvabssp(__a);
#else
  vector unsigned int __res =
      (vector unsigned int)__a & (vector unsigned int)(0x7FFFFFFF);
  return (vector float)__res;
#endif
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_abs(vector double __a) {
  return __builtin_vsx_xvabsdp(__a);
}
#endif

/* vec_abss */
#define __builtin_altivec_abss_v16qi vec_abss
#define __builtin_altivec_abss_v8hi vec_abss
#define __builtin_altivec_abss_v4si vec_abss

static __inline__ vector signed char __ATTRS_o_ai
vec_abss(vector signed char __a) {
  return __builtin_altivec_vmaxsb(
      __a, __builtin_altivec_vsubsbs((vector signed char)(0), __a));
}

static __inline__ vector signed short __ATTRS_o_ai
vec_abss(vector signed short __a) {
  return __builtin_altivec_vmaxsh(
      __a, __builtin_altivec_vsubshs((vector signed short)(0), __a));
}

static __inline__ vector signed int __ATTRS_o_ai
vec_abss(vector signed int __a) {
  return __builtin_altivec_vmaxsw(
      __a, __builtin_altivec_vsubsws((vector signed int)(0), __a));
}

/* vec_absd */
#if defined(__POWER9_VECTOR__)

static __inline__ vector unsigned char __ATTRS_o_ai
vec_absd(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_altivec_vabsdub(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_absd(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_altivec_vabsduh(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_absd(vector unsigned int __a,  vector unsigned int __b) {
  return __builtin_altivec_vabsduw(__a, __b);
}

#endif /* End __POWER9_VECTOR__ */

/* vec_add */

static __inline__ vector signed char __ATTRS_o_ai
vec_add(vector signed char __a, vector signed char __b) {
  return __a + __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_add(vector bool char __a, vector signed char __b) {
  return (vector signed char)__a + __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_add(vector signed char __a, vector bool char __b) {
  return __a + (vector signed char)__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_add(vector unsigned char __a, vector unsigned char __b) {
  return __a + __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_add(vector bool char __a, vector unsigned char __b) {
  return (vector unsigned char)__a + __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_add(vector unsigned char __a, vector bool char __b) {
  return __a + (vector unsigned char)__b;
}

static __inline__ vector short __ATTRS_o_ai vec_add(vector short __a,
                                                    vector short __b) {
  return __a + __b;
}

static __inline__ vector short __ATTRS_o_ai vec_add(vector bool short __a,
                                                    vector short __b) {
  return (vector short)__a + __b;
}

static __inline__ vector short __ATTRS_o_ai vec_add(vector short __a,
                                                    vector bool short __b) {
  return __a + (vector short)__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_add(vector unsigned short __a, vector unsigned short __b) {
  return __a + __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_add(vector bool short __a, vector unsigned short __b) {
  return (vector unsigned short)__a + __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_add(vector unsigned short __a, vector bool short __b) {
  return __a + (vector unsigned short)__b;
}

static __inline__ vector int __ATTRS_o_ai vec_add(vector int __a,
                                                  vector int __b) {
  return __a + __b;
}

static __inline__ vector int __ATTRS_o_ai vec_add(vector bool int __a,
                                                  vector int __b) {
  return (vector int)__a + __b;
}

static __inline__ vector int __ATTRS_o_ai vec_add(vector int __a,
                                                  vector bool int __b) {
  return __a + (vector int)__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_add(vector unsigned int __a, vector unsigned int __b) {
  return __a + __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_add(vector bool int __a, vector unsigned int __b) {
  return (vector unsigned int)__a + __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_add(vector unsigned int __a, vector bool int __b) {
  return __a + (vector unsigned int)__b;
}

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
static __inline__ vector signed long long __ATTRS_o_ai
vec_add(vector signed long long __a, vector signed long long __b) {
  return __a + __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_add(vector unsigned long long __a, vector unsigned long long __b) {
  return __a + __b;
}

static __inline__ vector signed __int128 __ATTRS_o_ai
vec_add(vector signed __int128 __a, vector signed __int128 __b) {
  return __a + __b;
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_add(vector unsigned __int128 __a, vector unsigned __int128 __b) {
  return __a + __b;
}
#endif // defined(__POWER8_VECTOR__) && defined(__powerpc64__)

static __inline__ vector float __ATTRS_o_ai vec_add(vector float __a,
                                                    vector float __b) {
  return __a + __b;
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_add(vector double __a,
                                                     vector double __b) {
  return __a + __b;
}
#endif // __VSX__

/* vec_adde */

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
static __inline__ vector signed __int128 __ATTRS_o_ai
vec_adde(vector signed __int128 __a, vector signed __int128 __b,
         vector signed __int128 __c) {
  return __builtin_altivec_vaddeuqm(__a, __b, __c);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_adde(vector unsigned __int128 __a, vector unsigned __int128 __b,
         vector unsigned __int128 __c) {
  return __builtin_altivec_vaddeuqm(__a, __b, __c);
}
#endif

static __inline__ vector signed int __ATTRS_o_ai
vec_adde(vector signed int __a, vector signed int __b,
         vector signed int __c) {
  vector signed int __mask = {1, 1, 1, 1};
  vector signed int __carry = __c & __mask;
  return vec_add(vec_add(__a, __b), __carry);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_adde(vector unsigned int __a, vector unsigned int __b,
         vector unsigned int __c) {
  vector unsigned int __mask = {1, 1, 1, 1};
  vector unsigned int __carry = __c & __mask;
  return vec_add(vec_add(__a, __b), __carry);
}

/* vec_addec */

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
static __inline__ vector signed __int128 __ATTRS_o_ai
vec_addec(vector signed __int128 __a, vector signed __int128 __b,
          vector signed __int128 __c) {
  return __builtin_altivec_vaddecuq(__a, __b, __c);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_addec(vector unsigned __int128 __a, vector unsigned __int128 __b,
          vector unsigned __int128 __c) {
  return __builtin_altivec_vaddecuq(__a, __b, __c);
}

static __inline__ vector signed int __ATTRS_o_ai
vec_addec(vector signed int __a, vector signed int __b,
          vector signed int __c) {

  signed int __result[4];
  for (int i = 0; i < 4; i++) {
    unsigned int __tempa = (unsigned int) __a[i];
    unsigned int __tempb = (unsigned int) __b[i];
    unsigned int __tempc = (unsigned int) __c[i];
    __tempc = __tempc & 0x00000001;
    unsigned long long __longa = (unsigned long long) __tempa;
    unsigned long long __longb = (unsigned long long) __tempb;
    unsigned long long __longc = (unsigned long long) __tempc;
    unsigned long long __sum = __longa + __longb + __longc;
    unsigned long long __res = (__sum >> 32) & 0x01;
    unsigned long long __tempres = (unsigned int) __res;
    __result[i] = (signed int) __tempres;
  }

  vector signed int ret = { __result[0], __result[1], __result[2], __result[3] };
  return ret;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_addec(vector unsigned int __a, vector unsigned int __b,
          vector unsigned int __c) {

  unsigned int __result[4];
  for (int i = 0; i < 4; i++) {
    unsigned int __tempc = __c[i] & 1;
    unsigned long long __longa = (unsigned long long) __a[i];
    unsigned long long __longb = (unsigned long long) __b[i];
    unsigned long long __longc = (unsigned long long) __tempc;
    unsigned long long __sum = __longa + __longb + __longc;
    unsigned long long __res = (__sum >> 32) & 0x01;
    unsigned long long __tempres = (unsigned int) __res;
    __result[i] = (signed int) __tempres;
  }

  vector unsigned int ret = { __result[0], __result[1], __result[2], __result[3] };
  return ret;
}

#endif

/* vec_vaddubm */

#define __builtin_altivec_vaddubm vec_vaddubm

static __inline__ vector signed char __ATTRS_o_ai
vec_vaddubm(vector signed char __a, vector signed char __b) {
  return __a + __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vaddubm(vector bool char __a, vector signed char __b) {
  return (vector signed char)__a + __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vaddubm(vector signed char __a, vector bool char __b) {
  return __a + (vector signed char)__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vaddubm(vector unsigned char __a, vector unsigned char __b) {
  return __a + __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vaddubm(vector bool char __a, vector unsigned char __b) {
  return (vector unsigned char)__a + __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vaddubm(vector unsigned char __a, vector bool char __b) {
  return __a + (vector unsigned char)__b;
}

/* vec_vadduhm */

#define __builtin_altivec_vadduhm vec_vadduhm

static __inline__ vector short __ATTRS_o_ai vec_vadduhm(vector short __a,
                                                        vector short __b) {
  return __a + __b;
}

static __inline__ vector short __ATTRS_o_ai vec_vadduhm(vector bool short __a,
                                                        vector short __b) {
  return (vector short)__a + __b;
}

static __inline__ vector short __ATTRS_o_ai vec_vadduhm(vector short __a,
                                                        vector bool short __b) {
  return __a + (vector short)__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vadduhm(vector unsigned short __a, vector unsigned short __b) {
  return __a + __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vadduhm(vector bool short __a, vector unsigned short __b) {
  return (vector unsigned short)__a + __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vadduhm(vector unsigned short __a, vector bool short __b) {
  return __a + (vector unsigned short)__b;
}

/* vec_vadduwm */

#define __builtin_altivec_vadduwm vec_vadduwm

static __inline__ vector int __ATTRS_o_ai vec_vadduwm(vector int __a,
                                                      vector int __b) {
  return __a + __b;
}

static __inline__ vector int __ATTRS_o_ai vec_vadduwm(vector bool int __a,
                                                      vector int __b) {
  return (vector int)__a + __b;
}

static __inline__ vector int __ATTRS_o_ai vec_vadduwm(vector int __a,
                                                      vector bool int __b) {
  return __a + (vector int)__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vadduwm(vector unsigned int __a, vector unsigned int __b) {
  return __a + __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vadduwm(vector bool int __a, vector unsigned int __b) {
  return (vector unsigned int)__a + __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vadduwm(vector unsigned int __a, vector bool int __b) {
  return __a + (vector unsigned int)__b;
}

/* vec_vaddfp */

#define __builtin_altivec_vaddfp vec_vaddfp

static __inline__ vector float __attribute__((__always_inline__))
vec_vaddfp(vector float __a, vector float __b) {
  return __a + __b;
}

/* vec_addc */

static __inline__ vector signed int __ATTRS_o_ai
vec_addc(vector signed int __a, vector signed int __b) {
  return (vector signed int)__builtin_altivec_vaddcuw((vector unsigned int)__a,
                                                      (vector unsigned int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_addc(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_altivec_vaddcuw(__a, __b);
}

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
static __inline__ vector signed __int128 __ATTRS_o_ai
vec_addc(vector signed __int128 __a, vector signed __int128 __b) {
  return (vector signed __int128)__builtin_altivec_vaddcuq(
      (vector unsigned __int128)__a, (vector unsigned __int128)__b);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_addc(vector unsigned __int128 __a, vector unsigned __int128 __b) {
  return __builtin_altivec_vaddcuq(__a, __b);
}
#endif // defined(__POWER8_VECTOR__) && defined(__powerpc64__)

/* vec_vaddcuw */

static __inline__ vector unsigned int __attribute__((__always_inline__))
vec_vaddcuw(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_altivec_vaddcuw(__a, __b);
}

/* vec_adds */

static __inline__ vector signed char __ATTRS_o_ai
vec_adds(vector signed char __a, vector signed char __b) {
  return __builtin_altivec_vaddsbs(__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_adds(vector bool char __a, vector signed char __b) {
  return __builtin_altivec_vaddsbs((vector signed char)__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_adds(vector signed char __a, vector bool char __b) {
  return __builtin_altivec_vaddsbs(__a, (vector signed char)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_adds(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_altivec_vaddubs(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_adds(vector bool char __a, vector unsigned char __b) {
  return __builtin_altivec_vaddubs((vector unsigned char)__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_adds(vector unsigned char __a, vector bool char __b) {
  return __builtin_altivec_vaddubs(__a, (vector unsigned char)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_adds(vector short __a,
                                                     vector short __b) {
  return __builtin_altivec_vaddshs(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_adds(vector bool short __a,
                                                     vector short __b) {
  return __builtin_altivec_vaddshs((vector short)__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_adds(vector short __a,
                                                     vector bool short __b) {
  return __builtin_altivec_vaddshs(__a, (vector short)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_adds(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_altivec_vadduhs(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_adds(vector bool short __a, vector unsigned short __b) {
  return __builtin_altivec_vadduhs((vector unsigned short)__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_adds(vector unsigned short __a, vector bool short __b) {
  return __builtin_altivec_vadduhs(__a, (vector unsigned short)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_adds(vector int __a,
                                                   vector int __b) {
  return __builtin_altivec_vaddsws(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_adds(vector bool int __a,
                                                   vector int __b) {
  return __builtin_altivec_vaddsws((vector int)__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_adds(vector int __a,
                                                   vector bool int __b) {
  return __builtin_altivec_vaddsws(__a, (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_adds(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_altivec_vadduws(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_adds(vector bool int __a, vector unsigned int __b) {
  return __builtin_altivec_vadduws((vector unsigned int)__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_adds(vector unsigned int __a, vector bool int __b) {
  return __builtin_altivec_vadduws(__a, (vector unsigned int)__b);
}

/* vec_vaddsbs */

static __inline__ vector signed char __ATTRS_o_ai
vec_vaddsbs(vector signed char __a, vector signed char __b) {
  return __builtin_altivec_vaddsbs(__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vaddsbs(vector bool char __a, vector signed char __b) {
  return __builtin_altivec_vaddsbs((vector signed char)__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vaddsbs(vector signed char __a, vector bool char __b) {
  return __builtin_altivec_vaddsbs(__a, (vector signed char)__b);
}

/* vec_vaddubs */

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vaddubs(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_altivec_vaddubs(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vaddubs(vector bool char __a, vector unsigned char __b) {
  return __builtin_altivec_vaddubs((vector unsigned char)__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vaddubs(vector unsigned char __a, vector bool char __b) {
  return __builtin_altivec_vaddubs(__a, (vector unsigned char)__b);
}

/* vec_vaddshs */

static __inline__ vector short __ATTRS_o_ai vec_vaddshs(vector short __a,
                                                        vector short __b) {
  return __builtin_altivec_vaddshs(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_vaddshs(vector bool short __a,
                                                        vector short __b) {
  return __builtin_altivec_vaddshs((vector short)__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_vaddshs(vector short __a,
                                                        vector bool short __b) {
  return __builtin_altivec_vaddshs(__a, (vector short)__b);
}

/* vec_vadduhs */

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vadduhs(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_altivec_vadduhs(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vadduhs(vector bool short __a, vector unsigned short __b) {
  return __builtin_altivec_vadduhs((vector unsigned short)__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vadduhs(vector unsigned short __a, vector bool short __b) {
  return __builtin_altivec_vadduhs(__a, (vector unsigned short)__b);
}

/* vec_vaddsws */

static __inline__ vector int __ATTRS_o_ai vec_vaddsws(vector int __a,
                                                      vector int __b) {
  return __builtin_altivec_vaddsws(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_vaddsws(vector bool int __a,
                                                      vector int __b) {
  return __builtin_altivec_vaddsws((vector int)__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_vaddsws(vector int __a,
                                                      vector bool int __b) {
  return __builtin_altivec_vaddsws(__a, (vector int)__b);
}

/* vec_vadduws */

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vadduws(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_altivec_vadduws(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vadduws(vector bool int __a, vector unsigned int __b) {
  return __builtin_altivec_vadduws((vector unsigned int)__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vadduws(vector unsigned int __a, vector bool int __b) {
  return __builtin_altivec_vadduws(__a, (vector unsigned int)__b);
}

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
/* vec_vadduqm */

static __inline__ vector signed __int128 __ATTRS_o_ai
vec_vadduqm(vector signed __int128 __a, vector signed __int128 __b) {
  return __a + __b;
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_vadduqm(vector unsigned __int128 __a, vector unsigned __int128 __b) {
  return __a + __b;
}

/* vec_vaddeuqm */

static __inline__ vector signed __int128 __ATTRS_o_ai
vec_vaddeuqm(vector signed __int128 __a, vector signed __int128 __b,
             vector signed __int128 __c) {
  return __builtin_altivec_vaddeuqm(__a, __b, __c);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_vaddeuqm(vector unsigned __int128 __a, vector unsigned __int128 __b,
             vector unsigned __int128 __c) {
  return __builtin_altivec_vaddeuqm(__a, __b, __c);
}

/* vec_vaddcuq */

static __inline__ vector signed __int128 __ATTRS_o_ai
vec_vaddcuq(vector signed __int128 __a, vector signed __int128 __b) {
  return __builtin_altivec_vaddcuq(__a, __b);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_vaddcuq(vector unsigned __int128 __a, vector unsigned __int128 __b) {
  return __builtin_altivec_vaddcuq(__a, __b);
}

/* vec_vaddecuq */

static __inline__ vector signed __int128 __ATTRS_o_ai
vec_vaddecuq(vector signed __int128 __a, vector signed __int128 __b,
             vector signed __int128 __c) {
  return __builtin_altivec_vaddecuq(__a, __b, __c);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_vaddecuq(vector unsigned __int128 __a, vector unsigned __int128 __b,
             vector unsigned __int128 __c) {
  return __builtin_altivec_vaddecuq(__a, __b, __c);
}
#endif // defined(__POWER8_VECTOR__) && defined(__powerpc64__)

/* vec_and */

#define __builtin_altivec_vand vec_and

static __inline__ vector signed char __ATTRS_o_ai
vec_and(vector signed char __a, vector signed char __b) {
  return __a & __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_and(vector bool char __a, vector signed char __b) {
  return (vector signed char)__a & __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_and(vector signed char __a, vector bool char __b) {
  return __a & (vector signed char)__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_and(vector unsigned char __a, vector unsigned char __b) {
  return __a & __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_and(vector bool char __a, vector unsigned char __b) {
  return (vector unsigned char)__a & __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_and(vector unsigned char __a, vector bool char __b) {
  return __a & (vector unsigned char)__b;
}

static __inline__ vector bool char __ATTRS_o_ai vec_and(vector bool char __a,
                                                        vector bool char __b) {
  return __a & __b;
}

static __inline__ vector short __ATTRS_o_ai vec_and(vector short __a,
                                                    vector short __b) {
  return __a & __b;
}

static __inline__ vector short __ATTRS_o_ai vec_and(vector bool short __a,
                                                    vector short __b) {
  return (vector short)__a & __b;
}

static __inline__ vector short __ATTRS_o_ai vec_and(vector short __a,
                                                    vector bool short __b) {
  return __a & (vector short)__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_and(vector unsigned short __a, vector unsigned short __b) {
  return __a & __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_and(vector bool short __a, vector unsigned short __b) {
  return (vector unsigned short)__a & __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_and(vector unsigned short __a, vector bool short __b) {
  return __a & (vector unsigned short)__b;
}

static __inline__ vector bool short __ATTRS_o_ai
vec_and(vector bool short __a, vector bool short __b) {
  return __a & __b;
}

static __inline__ vector int __ATTRS_o_ai vec_and(vector int __a,
                                                  vector int __b) {
  return __a & __b;
}

static __inline__ vector int __ATTRS_o_ai vec_and(vector bool int __a,
                                                  vector int __b) {
  return (vector int)__a & __b;
}

static __inline__ vector int __ATTRS_o_ai vec_and(vector int __a,
                                                  vector bool int __b) {
  return __a & (vector int)__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_and(vector unsigned int __a, vector unsigned int __b) {
  return __a & __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_and(vector bool int __a, vector unsigned int __b) {
  return (vector unsigned int)__a & __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_and(vector unsigned int __a, vector bool int __b) {
  return __a & (vector unsigned int)__b;
}

static __inline__ vector bool int __ATTRS_o_ai vec_and(vector bool int __a,
                                                       vector bool int __b) {
  return __a & __b;
}

static __inline__ vector float __ATTRS_o_ai vec_and(vector float __a,
                                                    vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a & (vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_and(vector bool int __a,
                                                    vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a & (vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_and(vector float __a,
                                                    vector bool int __b) {
  vector unsigned int __res =
      (vector unsigned int)__a & (vector unsigned int)__b;
  return (vector float)__res;
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_and(vector bool long long __a,
                                                     vector double __b) {
  vector unsigned long long __res =
      (vector unsigned long long)__a & (vector unsigned long long)__b;
  return (vector double)__res;
}

static __inline__ vector double __ATTRS_o_ai
vec_and(vector double __a, vector bool long long __b) {
  vector unsigned long long __res =
      (vector unsigned long long)__a & (vector unsigned long long)__b;
  return (vector double)__res;
}

static __inline__ vector double __ATTRS_o_ai vec_and(vector double __a,
                                                     vector double __b) {
  vector unsigned long long __res =
      (vector unsigned long long)__a & (vector unsigned long long)__b;
  return (vector double)__res;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_and(vector signed long long __a, vector signed long long __b) {
  return __a & __b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_and(vector bool long long __a, vector signed long long __b) {
  return (vector signed long long)__a & __b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_and(vector signed long long __a, vector bool long long __b) {
  return __a & (vector signed long long)__b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_and(vector unsigned long long __a, vector unsigned long long __b) {
  return __a & __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_and(vector bool long long __a, vector unsigned long long __b) {
  return (vector unsigned long long)__a & __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_and(vector unsigned long long __a, vector bool long long __b) {
  return __a & (vector unsigned long long)__b;
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_and(vector bool long long __a, vector bool long long __b) {
  return __a & __b;
}
#endif

/* vec_vand */

static __inline__ vector signed char __ATTRS_o_ai
vec_vand(vector signed char __a, vector signed char __b) {
  return __a & __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vand(vector bool char __a, vector signed char __b) {
  return (vector signed char)__a & __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vand(vector signed char __a, vector bool char __b) {
  return __a & (vector signed char)__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vand(vector unsigned char __a, vector unsigned char __b) {
  return __a & __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vand(vector bool char __a, vector unsigned char __b) {
  return (vector unsigned char)__a & __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vand(vector unsigned char __a, vector bool char __b) {
  return __a & (vector unsigned char)__b;
}

static __inline__ vector bool char __ATTRS_o_ai vec_vand(vector bool char __a,
                                                         vector bool char __b) {
  return __a & __b;
}

static __inline__ vector short __ATTRS_o_ai vec_vand(vector short __a,
                                                     vector short __b) {
  return __a & __b;
}

static __inline__ vector short __ATTRS_o_ai vec_vand(vector bool short __a,
                                                     vector short __b) {
  return (vector short)__a & __b;
}

static __inline__ vector short __ATTRS_o_ai vec_vand(vector short __a,
                                                     vector bool short __b) {
  return __a & (vector short)__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vand(vector unsigned short __a, vector unsigned short __b) {
  return __a & __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vand(vector bool short __a, vector unsigned short __b) {
  return (vector unsigned short)__a & __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vand(vector unsigned short __a, vector bool short __b) {
  return __a & (vector unsigned short)__b;
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vand(vector bool short __a, vector bool short __b) {
  return __a & __b;
}

static __inline__ vector int __ATTRS_o_ai vec_vand(vector int __a,
                                                   vector int __b) {
  return __a & __b;
}

static __inline__ vector int __ATTRS_o_ai vec_vand(vector bool int __a,
                                                   vector int __b) {
  return (vector int)__a & __b;
}

static __inline__ vector int __ATTRS_o_ai vec_vand(vector int __a,
                                                   vector bool int __b) {
  return __a & (vector int)__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vand(vector unsigned int __a, vector unsigned int __b) {
  return __a & __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vand(vector bool int __a, vector unsigned int __b) {
  return (vector unsigned int)__a & __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vand(vector unsigned int __a, vector bool int __b) {
  return __a & (vector unsigned int)__b;
}

static __inline__ vector bool int __ATTRS_o_ai vec_vand(vector bool int __a,
                                                        vector bool int __b) {
  return __a & __b;
}

static __inline__ vector float __ATTRS_o_ai vec_vand(vector float __a,
                                                     vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a & (vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_vand(vector bool int __a,
                                                     vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a & (vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_vand(vector float __a,
                                                     vector bool int __b) {
  vector unsigned int __res =
      (vector unsigned int)__a & (vector unsigned int)__b;
  return (vector float)__res;
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_vand(vector signed long long __a, vector signed long long __b) {
  return __a & __b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_vand(vector bool long long __a, vector signed long long __b) {
  return (vector signed long long)__a & __b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_vand(vector signed long long __a, vector bool long long __b) {
  return __a & (vector signed long long)__b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_vand(vector unsigned long long __a, vector unsigned long long __b) {
  return __a & __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_vand(vector bool long long __a, vector unsigned long long __b) {
  return (vector unsigned long long)__a & __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_vand(vector unsigned long long __a, vector bool long long __b) {
  return __a & (vector unsigned long long)__b;
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_vand(vector bool long long __a, vector bool long long __b) {
  return __a & __b;
}
#endif

/* vec_andc */

#define __builtin_altivec_vandc vec_andc

static __inline__ vector signed char __ATTRS_o_ai
vec_andc(vector signed char __a, vector signed char __b) {
  return __a & ~__b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_andc(vector bool char __a, vector signed char __b) {
  return (vector signed char)__a & ~__b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_andc(vector signed char __a, vector bool char __b) {
  return __a & ~(vector signed char)__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_andc(vector unsigned char __a, vector unsigned char __b) {
  return __a & ~__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_andc(vector bool char __a, vector unsigned char __b) {
  return (vector unsigned char)__a & ~__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_andc(vector unsigned char __a, vector bool char __b) {
  return __a & ~(vector unsigned char)__b;
}

static __inline__ vector bool char __ATTRS_o_ai vec_andc(vector bool char __a,
                                                         vector bool char __b) {
  return __a & ~__b;
}

static __inline__ vector short __ATTRS_o_ai vec_andc(vector short __a,
                                                     vector short __b) {
  return __a & ~__b;
}

static __inline__ vector short __ATTRS_o_ai vec_andc(vector bool short __a,
                                                     vector short __b) {
  return (vector short)__a & ~__b;
}

static __inline__ vector short __ATTRS_o_ai vec_andc(vector short __a,
                                                     vector bool short __b) {
  return __a & ~(vector short)__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_andc(vector unsigned short __a, vector unsigned short __b) {
  return __a & ~__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_andc(vector bool short __a, vector unsigned short __b) {
  return (vector unsigned short)__a & ~__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_andc(vector unsigned short __a, vector bool short __b) {
  return __a & ~(vector unsigned short)__b;
}

static __inline__ vector bool short __ATTRS_o_ai
vec_andc(vector bool short __a, vector bool short __b) {
  return __a & ~__b;
}

static __inline__ vector int __ATTRS_o_ai vec_andc(vector int __a,
                                                   vector int __b) {
  return __a & ~__b;
}

static __inline__ vector int __ATTRS_o_ai vec_andc(vector bool int __a,
                                                   vector int __b) {
  return (vector int)__a & ~__b;
}

static __inline__ vector int __ATTRS_o_ai vec_andc(vector int __a,
                                                   vector bool int __b) {
  return __a & ~(vector int)__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_andc(vector unsigned int __a, vector unsigned int __b) {
  return __a & ~__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_andc(vector bool int __a, vector unsigned int __b) {
  return (vector unsigned int)__a & ~__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_andc(vector unsigned int __a, vector bool int __b) {
  return __a & ~(vector unsigned int)__b;
}

static __inline__ vector bool int __ATTRS_o_ai vec_andc(vector bool int __a,
                                                        vector bool int __b) {
  return __a & ~__b;
}

static __inline__ vector float __ATTRS_o_ai vec_andc(vector float __a,
                                                     vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a & ~(vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_andc(vector bool int __a,
                                                     vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a & ~(vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_andc(vector float __a,
                                                     vector bool int __b) {
  vector unsigned int __res =
      (vector unsigned int)__a & ~(vector unsigned int)__b;
  return (vector float)__res;
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_andc(vector bool long long __a,
                                                      vector double __b) {
  vector unsigned long long __res =
      (vector unsigned long long)__a & ~(vector unsigned long long)__b;
  return (vector double)__res;
}

static __inline__ vector double __ATTRS_o_ai
vec_andc(vector double __a, vector bool long long __b) {
  vector unsigned long long __res =
      (vector unsigned long long)__a & ~(vector unsigned long long)__b;
  return (vector double)__res;
}

static __inline__ vector double __ATTRS_o_ai vec_andc(vector double __a,
                                                      vector double __b) {
  vector unsigned long long __res =
      (vector unsigned long long)__a & ~(vector unsigned long long)__b;
  return (vector double)__res;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_andc(vector signed long long __a, vector signed long long __b) {
  return __a & ~__b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_andc(vector bool long long __a, vector signed long long __b) {
  return (vector signed long long)__a & ~__b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_andc(vector signed long long __a, vector bool long long __b) {
  return __a & ~(vector signed long long)__b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_andc(vector unsigned long long __a, vector unsigned long long __b) {
  return __a & ~__b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_andc(vector bool long long __a, vector unsigned long long __b) {
  return (vector unsigned long long)__a & ~__b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_andc(vector unsigned long long __a, vector bool long long __b) {
  return __a & ~(vector unsigned long long)__b;
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_andc(vector bool long long __a, vector bool long long __b) {
  return __a & ~__b;
}
#endif

/* vec_vandc */

static __inline__ vector signed char __ATTRS_o_ai
vec_vandc(vector signed char __a, vector signed char __b) {
  return __a & ~__b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vandc(vector bool char __a, vector signed char __b) {
  return (vector signed char)__a & ~__b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vandc(vector signed char __a, vector bool char __b) {
  return __a & ~(vector signed char)__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vandc(vector unsigned char __a, vector unsigned char __b) {
  return __a & ~__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vandc(vector bool char __a, vector unsigned char __b) {
  return (vector unsigned char)__a & ~__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vandc(vector unsigned char __a, vector bool char __b) {
  return __a & ~(vector unsigned char)__b;
}

static __inline__ vector bool char __ATTRS_o_ai
vec_vandc(vector bool char __a, vector bool char __b) {
  return __a & ~__b;
}

static __inline__ vector short __ATTRS_o_ai vec_vandc(vector short __a,
                                                      vector short __b) {
  return __a & ~__b;
}

static __inline__ vector short __ATTRS_o_ai vec_vandc(vector bool short __a,
                                                      vector short __b) {
  return (vector short)__a & ~__b;
}

static __inline__ vector short __ATTRS_o_ai vec_vandc(vector short __a,
                                                      vector bool short __b) {
  return __a & ~(vector short)__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vandc(vector unsigned short __a, vector unsigned short __b) {
  return __a & ~__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vandc(vector bool short __a, vector unsigned short __b) {
  return (vector unsigned short)__a & ~__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vandc(vector unsigned short __a, vector bool short __b) {
  return __a & ~(vector unsigned short)__b;
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vandc(vector bool short __a, vector bool short __b) {
  return __a & ~__b;
}

static __inline__ vector int __ATTRS_o_ai vec_vandc(vector int __a,
                                                    vector int __b) {
  return __a & ~__b;
}

static __inline__ vector int __ATTRS_o_ai vec_vandc(vector bool int __a,
                                                    vector int __b) {
  return (vector int)__a & ~__b;
}

static __inline__ vector int __ATTRS_o_ai vec_vandc(vector int __a,
                                                    vector bool int __b) {
  return __a & ~(vector int)__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vandc(vector unsigned int __a, vector unsigned int __b) {
  return __a & ~__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vandc(vector bool int __a, vector unsigned int __b) {
  return (vector unsigned int)__a & ~__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vandc(vector unsigned int __a, vector bool int __b) {
  return __a & ~(vector unsigned int)__b;
}

static __inline__ vector bool int __ATTRS_o_ai vec_vandc(vector bool int __a,
                                                         vector bool int __b) {
  return __a & ~__b;
}

static __inline__ vector float __ATTRS_o_ai vec_vandc(vector float __a,
                                                      vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a & ~(vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_vandc(vector bool int __a,
                                                      vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a & ~(vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_vandc(vector float __a,
                                                      vector bool int __b) {
  vector unsigned int __res =
      (vector unsigned int)__a & ~(vector unsigned int)__b;
  return (vector float)__res;
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_vandc(vector signed long long __a, vector signed long long __b) {
  return __a & ~__b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_vandc(vector bool long long __a, vector signed long long __b) {
  return (vector signed long long)__a & ~__b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_vandc(vector signed long long __a, vector bool long long __b) {
  return __a & ~(vector signed long long)__b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_vandc(vector unsigned long long __a, vector unsigned long long __b) {
  return __a & ~__b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_vandc(vector bool long long __a, vector unsigned long long __b) {
  return (vector unsigned long long)__a & ~__b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_vandc(vector unsigned long long __a, vector bool long long __b) {
  return __a & ~(vector unsigned long long)__b;
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_vandc(vector bool long long __a, vector bool long long __b) {
  return __a & ~__b;
}
#endif

/* vec_avg */

static __inline__ vector signed char __ATTRS_o_ai
vec_avg(vector signed char __a, vector signed char __b) {
  return __builtin_altivec_vavgsb(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_avg(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_altivec_vavgub(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_avg(vector short __a,
                                                    vector short __b) {
  return __builtin_altivec_vavgsh(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_avg(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_altivec_vavguh(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_avg(vector int __a,
                                                  vector int __b) {
  return __builtin_altivec_vavgsw(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_avg(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_altivec_vavguw(__a, __b);
}

/* vec_vavgsb */

static __inline__ vector signed char __attribute__((__always_inline__))
vec_vavgsb(vector signed char __a, vector signed char __b) {
  return __builtin_altivec_vavgsb(__a, __b);
}

/* vec_vavgub */

static __inline__ vector unsigned char __attribute__((__always_inline__))
vec_vavgub(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_altivec_vavgub(__a, __b);
}

/* vec_vavgsh */

static __inline__ vector short __attribute__((__always_inline__))
vec_vavgsh(vector short __a, vector short __b) {
  return __builtin_altivec_vavgsh(__a, __b);
}

/* vec_vavguh */

static __inline__ vector unsigned short __attribute__((__always_inline__))
vec_vavguh(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_altivec_vavguh(__a, __b);
}

/* vec_vavgsw */

static __inline__ vector int __attribute__((__always_inline__))
vec_vavgsw(vector int __a, vector int __b) {
  return __builtin_altivec_vavgsw(__a, __b);
}

/* vec_vavguw */

static __inline__ vector unsigned int __attribute__((__always_inline__))
vec_vavguw(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_altivec_vavguw(__a, __b);
}

/* vec_ceil */

static __inline__ vector float __ATTRS_o_ai vec_ceil(vector float __a) {
#ifdef __VSX__
  return __builtin_vsx_xvrspip(__a);
#else
  return __builtin_altivec_vrfip(__a);
#endif
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_ceil(vector double __a) {
  return __builtin_vsx_xvrdpip(__a);
}
#endif

/* vec_vrfip */

static __inline__ vector float __attribute__((__always_inline__))
vec_vrfip(vector float __a) {
  return __builtin_altivec_vrfip(__a);
}

/* vec_cmpb */

static __inline__ vector int __attribute__((__always_inline__))
vec_cmpb(vector float __a, vector float __b) {
  return __builtin_altivec_vcmpbfp(__a, __b);
}

/* vec_vcmpbfp */

static __inline__ vector int __attribute__((__always_inline__))
vec_vcmpbfp(vector float __a, vector float __b) {
  return __builtin_altivec_vcmpbfp(__a, __b);
}

/* vec_cmpeq */

static __inline__ vector bool char __ATTRS_o_ai
vec_cmpeq(vector signed char __a, vector signed char __b) {
  return (vector bool char)__builtin_altivec_vcmpequb((vector char)__a,
                                                      (vector char)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_cmpeq(vector unsigned char __a, vector unsigned char __b) {
  return (vector bool char)__builtin_altivec_vcmpequb((vector char)__a,
                                                      (vector char)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_cmpeq(vector bool char __a, vector bool char __b) {
  return (vector bool char)__builtin_altivec_vcmpequb((vector char)__a,
                                                      (vector char)__b);
}

static __inline__ vector bool short __ATTRS_o_ai vec_cmpeq(vector short __a,
                                                           vector short __b) {
  return (vector bool short)__builtin_altivec_vcmpequh(__a, __b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_cmpeq(vector unsigned short __a, vector unsigned short __b) {
  return (vector bool short)__builtin_altivec_vcmpequh((vector short)__a,
                                                       (vector short)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_cmpeq(vector bool short __a, vector bool short __b) {
  return (vector bool short)__builtin_altivec_vcmpequh((vector short)__a,
                                                       (vector short)__b);
}

static __inline__ vector bool int __ATTRS_o_ai vec_cmpeq(vector int __a,
                                                         vector int __b) {
  return (vector bool int)__builtin_altivec_vcmpequw(__a, __b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_cmpeq(vector unsigned int __a, vector unsigned int __b) {
  return (vector bool int)__builtin_altivec_vcmpequw((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai vec_cmpeq(vector bool int __a,
                                                         vector bool int __b) {
  return (vector bool int)__builtin_altivec_vcmpequw((vector int)__a,
                                                     (vector int)__b);
}

#ifdef __POWER8_VECTOR__
static __inline__ vector bool long long __ATTRS_o_ai
vec_cmpeq(vector signed long long __a, vector signed long long __b) {
  return (vector bool long long)__builtin_altivec_vcmpequd(__a, __b);
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_cmpeq(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector bool long long)__builtin_altivec_vcmpequd(
      (vector long long)__a, (vector long long)__b);
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_cmpeq(vector bool long long __a, vector bool long long __b) {
  return (vector bool long long)__builtin_altivec_vcmpequd(
      (vector long long)__a, (vector long long)__b);
}

#endif

static __inline__ vector bool int __ATTRS_o_ai vec_cmpeq(vector float __a,
                                                         vector float __b) {
#ifdef __VSX__
  return (vector bool int)__builtin_vsx_xvcmpeqsp(__a, __b);
#else
  return (vector bool int)__builtin_altivec_vcmpeqfp(__a, __b);
#endif
}

#ifdef __VSX__
static __inline__ vector bool long long __ATTRS_o_ai
vec_cmpeq(vector double __a, vector double __b) {
  return (vector bool long long)__builtin_vsx_xvcmpeqdp(__a, __b);
}
#endif

#ifdef __POWER9_VECTOR__
/* vec_cmpne */

static __inline__ vector bool char __ATTRS_o_ai
vec_cmpne(vector bool char __a, vector bool char __b) {
  return (vector bool char)__builtin_altivec_vcmpneb((vector char)__a,
                                                     (vector char)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_cmpne(vector signed char __a, vector signed char __b) {
  return (vector bool char)__builtin_altivec_vcmpneb((vector char)__a,
                                                     (vector char)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_cmpne(vector unsigned char __a, vector unsigned char __b) {
  return (vector bool char)__builtin_altivec_vcmpneb((vector char)__a,
                                                     (vector char)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_cmpne(vector bool short __a, vector bool short __b) {
  return (vector bool short)__builtin_altivec_vcmpneh((vector short)__a,
                                                      (vector short)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_cmpne(vector signed short __a, vector signed short __b) {
  return (vector bool short)__builtin_altivec_vcmpneh((vector short)__a,
                                                      (vector short)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_cmpne(vector unsigned short __a, vector unsigned short __b) {
  return (vector bool short)__builtin_altivec_vcmpneh((vector short)__a,
                                                      (vector short)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_cmpne(vector bool int __a, vector bool int __b) {
  return (vector bool int)__builtin_altivec_vcmpnew((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_cmpne(vector signed int __a, vector signed int __b) {
  return (vector bool int)__builtin_altivec_vcmpnew((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_cmpne(vector unsigned int __a, vector unsigned int __b) {
  return (vector bool int)__builtin_altivec_vcmpnew((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_cmpne(vector bool long long __a, vector bool long long __b) {
  return (vector bool long long)
    ~(__builtin_altivec_vcmpequd((vector long long)__a, (vector long long)__b));
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_cmpne(vector signed long long __a, vector signed long long __b) {
  return (vector bool long long)
    ~(__builtin_altivec_vcmpequd((vector long long)__a, (vector long long)__b));
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_cmpne(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector bool long long)
    ~(__builtin_altivec_vcmpequd((vector long long)__a, (vector long long)__b));
}

static __inline__ vector bool int __ATTRS_o_ai
vec_cmpne(vector float __a, vector float __b) {
  return (vector bool int)__builtin_altivec_vcmpnew((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_cmpne(vector double __a, vector double __b) {
  return (vector bool long long)
    ~(__builtin_altivec_vcmpequd((vector long long)__a, (vector long long)__b));
}

/* vec_cmpnez */

static __inline__ vector bool char __ATTRS_o_ai
vec_cmpnez(vector signed char __a, vector signed char __b) {
  return (vector bool char)__builtin_altivec_vcmpnezb((vector char)__a,
                                                      (vector char)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_cmpnez(vector unsigned char __a, vector unsigned char __b) {
  return (vector bool char)__builtin_altivec_vcmpnezb((vector char)__a,
                                                      (vector char)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_cmpnez(vector signed short __a, vector signed short __b) {
  return (vector bool short)__builtin_altivec_vcmpnezh((vector short)__a,
                                                       (vector short)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_cmpnez(vector unsigned short __a, vector unsigned short __b) {
  return (vector bool short)__builtin_altivec_vcmpnezh((vector short)__a,
                                                       (vector short)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_cmpnez(vector signed int __a, vector signed int __b) {
  return (vector bool int)__builtin_altivec_vcmpnezw((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_cmpnez(vector unsigned int __a, vector unsigned int __b) {
  return (vector bool int)__builtin_altivec_vcmpnezw((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ signed int __ATTRS_o_ai
vec_cntlz_lsbb(vector signed char __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vctzlsbb(__a);
#else
  return __builtin_altivec_vclzlsbb(__a);
#endif
}

static __inline__ signed int __ATTRS_o_ai
vec_cntlz_lsbb(vector unsigned char __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vctzlsbb(__a);
#else
  return __builtin_altivec_vclzlsbb(__a);
#endif
}

static __inline__ signed int __ATTRS_o_ai
vec_cnttz_lsbb(vector signed char __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vclzlsbb(__a);
#else
  return __builtin_altivec_vctzlsbb(__a);
#endif
}

static __inline__ signed int __ATTRS_o_ai
vec_cnttz_lsbb(vector unsigned char __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vclzlsbb(__a);
#else
  return __builtin_altivec_vctzlsbb(__a);
#endif
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_parity_lsbb(vector unsigned int __a) {
  return __builtin_altivec_vprtybw(__a);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_parity_lsbb(vector signed int __a) {
  return __builtin_altivec_vprtybw(__a);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_parity_lsbb(vector unsigned __int128 __a) {
  return __builtin_altivec_vprtybq(__a);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_parity_lsbb(vector signed __int128 __a) {
  return __builtin_altivec_vprtybq(__a);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_parity_lsbb(vector unsigned long long __a) {
  return __builtin_altivec_vprtybd(__a);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_parity_lsbb(vector signed long long __a) {
  return __builtin_altivec_vprtybd(__a);
}

#endif

/* vec_cmpgt */

static __inline__ vector bool char __ATTRS_o_ai
vec_cmpgt(vector signed char __a, vector signed char __b) {
  return (vector bool char)__builtin_altivec_vcmpgtsb(__a, __b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_cmpgt(vector unsigned char __a, vector unsigned char __b) {
  return (vector bool char)__builtin_altivec_vcmpgtub(__a, __b);
}

static __inline__ vector bool short __ATTRS_o_ai vec_cmpgt(vector short __a,
                                                           vector short __b) {
  return (vector bool short)__builtin_altivec_vcmpgtsh(__a, __b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_cmpgt(vector unsigned short __a, vector unsigned short __b) {
  return (vector bool short)__builtin_altivec_vcmpgtuh(__a, __b);
}

static __inline__ vector bool int __ATTRS_o_ai vec_cmpgt(vector int __a,
                                                         vector int __b) {
  return (vector bool int)__builtin_altivec_vcmpgtsw(__a, __b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_cmpgt(vector unsigned int __a, vector unsigned int __b) {
  return (vector bool int)__builtin_altivec_vcmpgtuw(__a, __b);
}

#ifdef __POWER8_VECTOR__
static __inline__ vector bool long long __ATTRS_o_ai
vec_cmpgt(vector signed long long __a, vector signed long long __b) {
  return (vector bool long long)__builtin_altivec_vcmpgtsd(__a, __b);
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_cmpgt(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector bool long long)__builtin_altivec_vcmpgtud(__a, __b);
}
#endif

static __inline__ vector bool int __ATTRS_o_ai vec_cmpgt(vector float __a,
                                                         vector float __b) {
#ifdef __VSX__
  return (vector bool int)__builtin_vsx_xvcmpgtsp(__a, __b);
#else
  return (vector bool int)__builtin_altivec_vcmpgtfp(__a, __b);
#endif
}

#ifdef __VSX__
static __inline__ vector bool long long __ATTRS_o_ai
vec_cmpgt(vector double __a, vector double __b) {
  return (vector bool long long)__builtin_vsx_xvcmpgtdp(__a, __b);
}
#endif

/* vec_cmpge */

static __inline__ vector bool char __ATTRS_o_ai
vec_cmpge(vector signed char __a, vector signed char __b) {
  return ~(vec_cmpgt(__b, __a));
}

static __inline__ vector bool char __ATTRS_o_ai
vec_cmpge(vector unsigned char __a, vector unsigned char __b) {
  return ~(vec_cmpgt(__b, __a));
}

static __inline__ vector bool short __ATTRS_o_ai
vec_cmpge(vector signed short __a, vector signed short __b) {
  return ~(vec_cmpgt(__b, __a));
}

static __inline__ vector bool short __ATTRS_o_ai
vec_cmpge(vector unsigned short __a, vector unsigned short __b) {
  return ~(vec_cmpgt(__b, __a));
}

static __inline__ vector bool int __ATTRS_o_ai
vec_cmpge(vector signed int __a, vector signed int __b) {
  return ~(vec_cmpgt(__b, __a));
}

static __inline__ vector bool int __ATTRS_o_ai
vec_cmpge(vector unsigned int __a, vector unsigned int __b) {
  return ~(vec_cmpgt(__b, __a));
}

static __inline__ vector bool int __ATTRS_o_ai vec_cmpge(vector float __a,
                                                         vector float __b) {
#ifdef __VSX__
  return (vector bool int)__builtin_vsx_xvcmpgesp(__a, __b);
#else
  return (vector bool int)__builtin_altivec_vcmpgefp(__a, __b);
#endif
}

#ifdef __VSX__
static __inline__ vector bool long long __ATTRS_o_ai
vec_cmpge(vector double __a, vector double __b) {
  return (vector bool long long)__builtin_vsx_xvcmpgedp(__a, __b);
}
#endif

#ifdef __POWER8_VECTOR__
static __inline__ vector bool long long __ATTRS_o_ai
vec_cmpge(vector signed long long __a, vector signed long long __b) {
  return ~(vec_cmpgt(__b, __a));
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_cmpge(vector unsigned long long __a, vector unsigned long long __b) {
  return ~(vec_cmpgt(__b, __a));
}
#endif

/* vec_vcmpgefp */

static __inline__ vector bool int __attribute__((__always_inline__))
vec_vcmpgefp(vector float __a, vector float __b) {
  return (vector bool int)__builtin_altivec_vcmpgefp(__a, __b);
}

/* vec_vcmpgtsb */

static __inline__ vector bool char __attribute__((__always_inline__))
vec_vcmpgtsb(vector signed char __a, vector signed char __b) {
  return (vector bool char)__builtin_altivec_vcmpgtsb(__a, __b);
}

/* vec_vcmpgtub */

static __inline__ vector bool char __attribute__((__always_inline__))
vec_vcmpgtub(vector unsigned char __a, vector unsigned char __b) {
  return (vector bool char)__builtin_altivec_vcmpgtub(__a, __b);
}

/* vec_vcmpgtsh */

static __inline__ vector bool short __attribute__((__always_inline__))
vec_vcmpgtsh(vector short __a, vector short __b) {
  return (vector bool short)__builtin_altivec_vcmpgtsh(__a, __b);
}

/* vec_vcmpgtuh */

static __inline__ vector bool short __attribute__((__always_inline__))
vec_vcmpgtuh(vector unsigned short __a, vector unsigned short __b) {
  return (vector bool short)__builtin_altivec_vcmpgtuh(__a, __b);
}

/* vec_vcmpgtsw */

static __inline__ vector bool int __attribute__((__always_inline__))
vec_vcmpgtsw(vector int __a, vector int __b) {
  return (vector bool int)__builtin_altivec_vcmpgtsw(__a, __b);
}

/* vec_vcmpgtuw */

static __inline__ vector bool int __attribute__((__always_inline__))
vec_vcmpgtuw(vector unsigned int __a, vector unsigned int __b) {
  return (vector bool int)__builtin_altivec_vcmpgtuw(__a, __b);
}

/* vec_vcmpgtfp */

static __inline__ vector bool int __attribute__((__always_inline__))
vec_vcmpgtfp(vector float __a, vector float __b) {
  return (vector bool int)__builtin_altivec_vcmpgtfp(__a, __b);
}

/* vec_cmple */

static __inline__ vector bool char __ATTRS_o_ai
vec_cmple(vector signed char __a, vector signed char __b) {
  return vec_cmpge(__b, __a);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_cmple(vector unsigned char __a, vector unsigned char __b) {
  return vec_cmpge(__b, __a);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_cmple(vector signed short __a, vector signed short __b) {
  return vec_cmpge(__b, __a);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_cmple(vector unsigned short __a, vector unsigned short __b) {
  return vec_cmpge(__b, __a);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_cmple(vector signed int __a, vector signed int __b) {
  return vec_cmpge(__b, __a);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_cmple(vector unsigned int __a, vector unsigned int __b) {
  return vec_cmpge(__b, __a);
}

static __inline__ vector bool int __ATTRS_o_ai vec_cmple(vector float __a,
                                                         vector float __b) {
  return vec_cmpge(__b, __a);
}

#ifdef __VSX__
static __inline__ vector bool long long __ATTRS_o_ai
vec_cmple(vector double __a, vector double __b) {
  return vec_cmpge(__b, __a);
}
#endif

#ifdef __POWER8_VECTOR__
static __inline__ vector bool long long __ATTRS_o_ai
vec_cmple(vector signed long long __a, vector signed long long __b) {
  return vec_cmpge(__b, __a);
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_cmple(vector unsigned long long __a, vector unsigned long long __b) {
  return vec_cmpge(__b, __a);
}
#endif

/* vec_cmplt */

static __inline__ vector bool char __ATTRS_o_ai
vec_cmplt(vector signed char __a, vector signed char __b) {
  return vec_cmpgt(__b, __a);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_cmplt(vector unsigned char __a, vector unsigned char __b) {
  return vec_cmpgt(__b, __a);
}

static __inline__ vector bool short __ATTRS_o_ai vec_cmplt(vector short __a,
                                                           vector short __b) {
  return vec_cmpgt(__b, __a);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_cmplt(vector unsigned short __a, vector unsigned short __b) {
  return vec_cmpgt(__b, __a);
}

static __inline__ vector bool int __ATTRS_o_ai vec_cmplt(vector int __a,
                                                         vector int __b) {
  return vec_cmpgt(__b, __a);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_cmplt(vector unsigned int __a, vector unsigned int __b) {
  return vec_cmpgt(__b, __a);
}

static __inline__ vector bool int __ATTRS_o_ai vec_cmplt(vector float __a,
                                                         vector float __b) {
  return vec_cmpgt(__b, __a);
}

#ifdef __VSX__
static __inline__ vector bool long long __ATTRS_o_ai
vec_cmplt(vector double __a, vector double __b) {
  return vec_cmpgt(__b, __a);
}
#endif

#ifdef __POWER8_VECTOR__
static __inline__ vector bool long long __ATTRS_o_ai
vec_cmplt(vector signed long long __a, vector signed long long __b) {
  return vec_cmpgt(__b, __a);
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_cmplt(vector unsigned long long __a, vector unsigned long long __b) {
  return vec_cmpgt(__b, __a);
}

/* vec_popcnt */

static __inline__ vector signed char __ATTRS_o_ai
vec_popcnt(vector signed char __a) {
  return __builtin_altivec_vpopcntb(__a);
}
static __inline__ vector unsigned char __ATTRS_o_ai
vec_popcnt(vector unsigned char __a) {
  return __builtin_altivec_vpopcntb(__a);
}
static __inline__ vector signed short __ATTRS_o_ai
vec_popcnt(vector signed short __a) {
  return __builtin_altivec_vpopcnth(__a);
}
static __inline__ vector unsigned short __ATTRS_o_ai
vec_popcnt(vector unsigned short __a) {
  return __builtin_altivec_vpopcnth(__a);
}
static __inline__ vector signed int __ATTRS_o_ai
vec_popcnt(vector signed int __a) {
  return __builtin_altivec_vpopcntw(__a);
}
static __inline__ vector unsigned int __ATTRS_o_ai
vec_popcnt(vector unsigned int __a) {
  return __builtin_altivec_vpopcntw(__a);
}
static __inline__ vector signed long long __ATTRS_o_ai
vec_popcnt(vector signed long long __a) {
  return __builtin_altivec_vpopcntd(__a);
}
static __inline__ vector unsigned long long __ATTRS_o_ai
vec_popcnt(vector unsigned long long __a) {
  return __builtin_altivec_vpopcntd(__a);
}

/* vec_cntlz */

static __inline__ vector signed char __ATTRS_o_ai
vec_cntlz(vector signed char __a) {
  return __builtin_altivec_vclzb(__a);
}
static __inline__ vector unsigned char __ATTRS_o_ai
vec_cntlz(vector unsigned char __a) {
  return __builtin_altivec_vclzb(__a);
}
static __inline__ vector signed short __ATTRS_o_ai
vec_cntlz(vector signed short __a) {
  return __builtin_altivec_vclzh(__a);
}
static __inline__ vector unsigned short __ATTRS_o_ai
vec_cntlz(vector unsigned short __a) {
  return __builtin_altivec_vclzh(__a);
}
static __inline__ vector signed int __ATTRS_o_ai
vec_cntlz(vector signed int __a) {
  return __builtin_altivec_vclzw(__a);
}
static __inline__ vector unsigned int __ATTRS_o_ai
vec_cntlz(vector unsigned int __a) {
  return __builtin_altivec_vclzw(__a);
}
static __inline__ vector signed long long __ATTRS_o_ai
vec_cntlz(vector signed long long __a) {
  return __builtin_altivec_vclzd(__a);
}
static __inline__ vector unsigned long long __ATTRS_o_ai
vec_cntlz(vector unsigned long long __a) {
  return __builtin_altivec_vclzd(__a);
}
#endif

#ifdef __POWER9_VECTOR__

/* vec_cnttz */

static __inline__ vector signed char __ATTRS_o_ai
vec_cnttz(vector signed char __a) {
  return __builtin_altivec_vctzb(__a);
}
static __inline__ vector unsigned char __ATTRS_o_ai
vec_cnttz(vector unsigned char __a) {
  return __builtin_altivec_vctzb(__a);
}
static __inline__ vector signed short __ATTRS_o_ai
vec_cnttz(vector signed short __a) {
  return __builtin_altivec_vctzh(__a);
}
static __inline__ vector unsigned short __ATTRS_o_ai
vec_cnttz(vector unsigned short __a) {
  return __builtin_altivec_vctzh(__a);
}
static __inline__ vector signed int __ATTRS_o_ai
vec_cnttz(vector signed int __a) {
  return __builtin_altivec_vctzw(__a);
}
static __inline__ vector unsigned int __ATTRS_o_ai
vec_cnttz(vector unsigned int __a) {
  return __builtin_altivec_vctzw(__a);
}
static __inline__ vector signed long long __ATTRS_o_ai
vec_cnttz(vector signed long long __a) {
  return __builtin_altivec_vctzd(__a);
}
static __inline__ vector unsigned long long __ATTRS_o_ai
vec_cnttz(vector unsigned long long __a) {
  return __builtin_altivec_vctzd(__a);
}

/* vec_first_match_index */

static __inline__ unsigned __ATTRS_o_ai
vec_first_match_index(vector signed char __a, vector signed char __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpeq(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpeq(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 3;
  }
  return __res[0] >> 3;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_match_index(vector unsigned char __a, vector unsigned char __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpeq(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpeq(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 3;
  }
  return __res[0] >> 3;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_match_index(vector signed short __a, vector signed short __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpeq(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpeq(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 4;
  }
  return __res[0] >> 4;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_match_index(vector unsigned short __a, vector unsigned short __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpeq(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpeq(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 4;
  }
  return __res[0] >> 4;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_match_index(vector signed int __a, vector signed int __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpeq(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpeq(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 5;
  }
  return __res[0] >> 5;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_match_index(vector unsigned int __a, vector unsigned int __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpeq(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpeq(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 5;
  }
  return __res[0] >> 5;
}

/* vec_first_match_or_eos_index */

static __inline__ unsigned __ATTRS_o_ai
vec_first_match_or_eos_index(vector signed char __a, vector signed char __b) {
  /* Compare the result of the comparison of two vectors with either and OR the
     result. Either the elements are equal or one will equal the comparison
     result if either is zero.
  */
  vector bool char __tmp1 = vec_cmpeq(__a, __b);
  vector bool char __tmp2 = __tmp1 |
                            vec_cmpeq((vector signed char)__tmp1, __a) |
                            vec_cmpeq((vector signed char)__tmp1, __b);

  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
      vec_cnttz((vector unsigned long long)__tmp2);
#else
      vec_cntlz((vector unsigned long long)__tmp2);
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 3;
  }
  return __res[0] >> 3;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_match_or_eos_index(vector unsigned char __a,
                             vector unsigned char __b) {
  vector bool char __tmp1 = vec_cmpeq(__a, __b);
  vector bool char __tmp2 = __tmp1 |
                            vec_cmpeq((vector unsigned char)__tmp1, __a) |
                            vec_cmpeq((vector unsigned char)__tmp1, __b);

  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
      vec_cnttz((vector unsigned long long)__tmp2);
#else
      vec_cntlz((vector unsigned long long)__tmp2);
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 3;
  }
  return __res[0] >> 3;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_match_or_eos_index(vector signed short __a, vector signed short __b) {
  vector bool short __tmp1 = vec_cmpeq(__a, __b);
  vector bool short __tmp2 = __tmp1 |
                             vec_cmpeq((vector signed short)__tmp1, __a) |
                             vec_cmpeq((vector signed short)__tmp1, __b);

  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
      vec_cnttz((vector unsigned long long)__tmp2);
#else
      vec_cntlz((vector unsigned long long)__tmp2);
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 4;
  }
  return __res[0] >> 4;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_match_or_eos_index(vector unsigned short __a,
                             vector unsigned short __b) {
  vector bool short __tmp1 = vec_cmpeq(__a, __b);
  vector bool short __tmp2 = __tmp1 |
                             vec_cmpeq((vector unsigned short)__tmp1, __a) |
                             vec_cmpeq((vector unsigned short)__tmp1, __b);

  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
      vec_cnttz((vector unsigned long long)__tmp2);
#else
      vec_cntlz((vector unsigned long long)__tmp2);
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 4;
  }
  return __res[0] >> 4;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_match_or_eos_index(vector signed int __a, vector signed int __b) {
  vector bool int __tmp1 = vec_cmpeq(__a, __b);
  vector bool int __tmp2 = __tmp1 | vec_cmpeq((vector signed int)__tmp1, __a) |
                           vec_cmpeq((vector signed int)__tmp1, __b);

  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
      vec_cnttz((vector unsigned long long)__tmp2);
#else
      vec_cntlz((vector unsigned long long)__tmp2);
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 5;
  }
  return __res[0] >> 5;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_match_or_eos_index(vector unsigned int __a, vector unsigned int __b) {
  vector bool int __tmp1 = vec_cmpeq(__a, __b);
  vector bool int __tmp2 = __tmp1 |
                           vec_cmpeq((vector unsigned int)__tmp1, __a) |
                           vec_cmpeq((vector unsigned int)__tmp1, __b);

  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)__tmp2);
#else
    vec_cntlz((vector unsigned long long)__tmp2);
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 5;
  }
  return __res[0] >> 5;
}

/* vec_first_mismatch_index */

static __inline__ unsigned __ATTRS_o_ai
vec_first_mismatch_index(vector signed char __a, vector signed char __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpne(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpne(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 3;
  }
  return __res[0] >> 3;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_mismatch_index(vector unsigned char __a, vector unsigned char __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpne(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpne(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 3;
  }
  return __res[0] >> 3;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_mismatch_index(vector signed short __a, vector signed short __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpne(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpne(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 4;
  }
  return __res[0] >> 4;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_mismatch_index(vector unsigned short __a, vector unsigned short __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpne(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpne(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 4;
  }
  return __res[0] >> 4;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_mismatch_index(vector signed int __a, vector signed int __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpne(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpne(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 5;
  }
  return __res[0] >> 5;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_mismatch_index(vector unsigned int __a, vector unsigned int __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpne(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpne(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 5;
  }
  return __res[0] >> 5;
}

/* vec_first_mismatch_or_eos_index */

static __inline__ unsigned __ATTRS_o_ai
vec_first_mismatch_or_eos_index(vector signed char __a,
                                vector signed char __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpnez(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpnez(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 3;
  }
  return __res[0] >> 3;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_mismatch_or_eos_index(vector unsigned char __a,
                                vector unsigned char __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpnez(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpnez(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 3;
  }
  return __res[0] >> 3;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_mismatch_or_eos_index(vector signed short __a,
                                vector signed short __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpnez(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpnez(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 4;
  }
  return __res[0] >> 4;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_mismatch_or_eos_index(vector unsigned short __a,
                                vector unsigned short __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpnez(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpnez(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 4;
  }
  return __res[0] >> 4;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_mismatch_or_eos_index(vector signed int __a, vector signed int __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpnez(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpnez(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 5;
  }
  return __res[0] >> 5;
}

static __inline__ unsigned __ATTRS_o_ai
vec_first_mismatch_or_eos_index(vector unsigned int __a,
                                vector unsigned int __b) {
  vector unsigned long long __res =
#ifdef __LITTLE_ENDIAN__
    vec_cnttz((vector unsigned long long)vec_cmpnez(__a, __b));
#else
    vec_cntlz((vector unsigned long long)vec_cmpnez(__a, __b));
#endif
  if (__res[0] == 64) {
    return (__res[1] + 64) >> 5;
  }
  return __res[0] >> 5;
}

static __inline__ vector double  __ATTRS_o_ai
vec_insert_exp(vector double __a, vector unsigned long long __b) {
  return __builtin_vsx_xviexpdp((vector unsigned long long)__a,__b);
}

static __inline__ vector double  __ATTRS_o_ai
vec_insert_exp(vector unsigned long long __a, vector unsigned long long __b) {
  return __builtin_vsx_xviexpdp(__a,__b);
}

static __inline__ vector float  __ATTRS_o_ai
vec_insert_exp(vector float __a, vector unsigned int __b) {
  return __builtin_vsx_xviexpsp((vector unsigned int)__a,__b);
}

static __inline__ vector float  __ATTRS_o_ai
vec_insert_exp(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_vsx_xviexpsp(__a,__b);
}

#if defined(__powerpc64__)
static __inline__ vector signed char __ATTRS_o_ai vec_xl_len(signed char *__a,
                                                             size_t __b) {
  return (vector signed char)__builtin_vsx_lxvl(__a, (__b << 56));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_xl_len(unsigned char *__a, size_t __b) {
  return (vector unsigned char)__builtin_vsx_lxvl(__a, (__b << 56));
}

static __inline__ vector signed short __ATTRS_o_ai vec_xl_len(signed short *__a,
                                                              size_t __b) {
  return (vector signed short)__builtin_vsx_lxvl(__a, (__b << 56));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_xl_len(unsigned short *__a, size_t __b) {
  return (vector unsigned short)__builtin_vsx_lxvl(__a, (__b << 56));
}

static __inline__ vector signed int __ATTRS_o_ai vec_xl_len(signed int *__a,
                                                            size_t __b) {
  return (vector signed int)__builtin_vsx_lxvl(__a, (__b << 56));
}

static __inline__ vector unsigned int __ATTRS_o_ai vec_xl_len(unsigned int *__a,
                                                              size_t __b) {
  return (vector unsigned int)__builtin_vsx_lxvl(__a, (__b << 56));
}

static __inline__ vector float __ATTRS_o_ai vec_xl_len(float *__a, size_t __b) {
  return (vector float)__builtin_vsx_lxvl(__a, (__b << 56));
}

static __inline__ vector signed __int128 __ATTRS_o_ai
vec_xl_len(signed __int128 *__a, size_t __b) {
  return (vector signed __int128)__builtin_vsx_lxvl(__a, (__b << 56));
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_xl_len(unsigned __int128 *__a, size_t __b) {
  return (vector unsigned __int128)__builtin_vsx_lxvl(__a, (__b << 56));
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_xl_len(signed long long *__a, size_t __b) {
  return (vector signed long long)__builtin_vsx_lxvl(__a, (__b << 56));
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_xl_len(unsigned long long *__a, size_t __b) {
  return (vector unsigned long long)__builtin_vsx_lxvl(__a, (__b << 56));
}

static __inline__ vector double __ATTRS_o_ai vec_xl_len(double *__a,
                                                        size_t __b) {
  return (vector double)__builtin_vsx_lxvl(__a, (__b << 56));
}

static __inline__ vector double __ATTRS_o_ai vec_xl_len_r(unsigned char *__a,
                                                          size_t __b) {
  vector unsigned char __res =
      (vector unsigned char)__builtin_vsx_lxvll(__a, (__b << 56));
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __mask =
      (vector unsigned char)__builtin_altivec_lvsr(16 - __b, (int *)NULL);
  __res = (vector unsigned char)__builtin_altivec_vperm_4si(
      (vector int)__res, (vector int)__res, __mask);
#endif
  return __res;
}

// vec_xst_len
static __inline__ void __ATTRS_o_ai vec_xst_len(vector unsigned char __a,
                                                unsigned char *__b,
                                                size_t __c) {
  return __builtin_vsx_stxvl((vector int)__a, __b, (__c << 56));
}

static __inline__ void __ATTRS_o_ai vec_xst_len(vector signed char __a,
                                                signed char *__b, size_t __c) {
  return __builtin_vsx_stxvl((vector int)__a, __b, (__c << 56));
}

static __inline__ void __ATTRS_o_ai vec_xst_len(vector signed short __a,
                                                signed short *__b, size_t __c) {
  return __builtin_vsx_stxvl((vector int)__a, __b, (__c << 56));
}

static __inline__ void __ATTRS_o_ai vec_xst_len(vector unsigned short __a,
                                                unsigned short *__b,
                                                size_t __c) {
  return __builtin_vsx_stxvl((vector int)__a, __b, (__c << 56));
}

static __inline__ void __ATTRS_o_ai vec_xst_len(vector signed int __a,
                                                signed int *__b, size_t __c) {
  return __builtin_vsx_stxvl((vector int)__a, __b, (__c << 56));
}

static __inline__ void __ATTRS_o_ai vec_xst_len(vector unsigned int __a,
                                                unsigned int *__b, size_t __c) {
  return __builtin_vsx_stxvl((vector int)__a, __b, (__c << 56));
}

static __inline__ void __ATTRS_o_ai vec_xst_len(vector float __a, float *__b,
                                                size_t __c) {
  return __builtin_vsx_stxvl((vector int)__a, __b, (__c << 56));
}

static __inline__ void __ATTRS_o_ai vec_xst_len(vector signed __int128 __a,
                                                signed __int128 *__b,
                                                size_t __c) {
  return __builtin_vsx_stxvl((vector int)__a, __b, (__c << 56));
}

static __inline__ void __ATTRS_o_ai vec_xst_len(vector unsigned __int128 __a,
                                                unsigned __int128 *__b,
                                                size_t __c) {
  return __builtin_vsx_stxvl((vector int)__a, __b, (__c << 56));
}

static __inline__ void __ATTRS_o_ai vec_xst_len(vector signed long long __a,
                                                signed long long *__b,
                                                size_t __c) {
  return __builtin_vsx_stxvl((vector int)__a, __b, (__c << 56));
}

static __inline__ void __ATTRS_o_ai vec_xst_len(vector unsigned long long __a,
                                                unsigned long long *__b,
                                                size_t __c) {
  return __builtin_vsx_stxvl((vector int)__a, __b, (__c << 56));
}

static __inline__ void __ATTRS_o_ai vec_xst_len(vector double __a, double *__b,
                                                size_t __c) {
  return __builtin_vsx_stxvl((vector int)__a, __b, (__c << 56));
}

static __inline__ void __ATTRS_o_ai vec_xst_len_r(vector unsigned char __a,
                                                  unsigned char *__b,
                                                  size_t __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __mask =
      (vector unsigned char)__builtin_altivec_lvsl(16 - __c, (int *)NULL);
  vector unsigned char __res =
      __builtin_altivec_vperm_4si((vector int)__a, (vector int)__a, __mask);
  return __builtin_vsx_stxvll((vector int)__res, __b, (__c << 56));
#else
  return __builtin_vsx_stxvll((vector int)__a, __b, (__c << 56));
#endif
}
#endif
#endif

/* vec_cpsgn */

#ifdef __VSX__
static __inline__ vector float __ATTRS_o_ai vec_cpsgn(vector float __a,
                                                      vector float __b) {
  return __builtin_vsx_xvcpsgnsp(__a, __b);
}

static __inline__ vector double __ATTRS_o_ai vec_cpsgn(vector double __a,
                                                       vector double __b) {
  return __builtin_vsx_xvcpsgndp(__a, __b);
}
#endif

/* vec_ctf */

#ifdef __VSX__
#define vec_ctf(__a, __b)                                                      \
  _Generic((__a), vector int                                                   \
           : (vector float)__builtin_altivec_vcfsx((__a), (__b)),              \
             vector unsigned int                                               \
           : (vector float)__builtin_altivec_vcfux((vector int)(__a), (__b)),  \
             vector unsigned long long                                         \
           : (__builtin_convertvector((vector unsigned long long)(__a),        \
                                      vector double) *                         \
              (vector double)(vector unsigned long long)((0x3ffULL - (__b))    \
                                                         << 52)),              \
             vector signed long long                                           \
           : (__builtin_convertvector((vector signed long long)(__a),          \
                                      vector double) *                         \
              (vector double)(vector unsigned long long)((0x3ffULL - (__b))    \
                                                         << 52)))
#else
#define vec_ctf(__a, __b)                                                      \
  _Generic((__a), vector int                                                   \
           : (vector float)__builtin_altivec_vcfsx((__a), (__b)),              \
             vector unsigned int                                               \
           : (vector float)__builtin_altivec_vcfux((vector int)(__a), (__b)))
#endif

/* vec_vcfsx */

#define vec_vcfux __builtin_altivec_vcfux

/* vec_vcfux */

#define vec_vcfsx(__a, __b) __builtin_altivec_vcfsx((vector int)(__a), (__b))

/* vec_cts */

#ifdef __VSX__
#define vec_cts(__a, __b)                                                      \
  _Generic((__a), vector float                                                 \
           : __builtin_altivec_vctsxs((__a), (__b)), vector double             \
           : __extension__({                                                   \
             vector double __ret =                                             \
                 (__a) *                                                       \
                 (vector double)(vector unsigned long long)((0x3ffULL + (__b)) \
                                                            << 52);            \
             __builtin_convertvector(__ret, vector signed long long);          \
           }))
#else
#define vec_cts __builtin_altivec_vctsxs
#endif

/* vec_vctsxs */

#define vec_vctsxs __builtin_altivec_vctsxs

/* vec_ctu */

#ifdef __VSX__
#define vec_ctu(__a, __b)                                                      \
  _Generic((__a), vector float                                                 \
           : __builtin_altivec_vctuxs((__a), (__b)), vector double             \
           : __extension__({                                                   \
             vector double __ret =                                             \
                 (__a) *                                                       \
                 (vector double)(vector unsigned long long)((0x3ffULL + __b)   \
                                                            << 52);            \
             __builtin_convertvector(__ret, vector unsigned long long);        \
           }))
#else
#define vec_ctu __builtin_altivec_vctuxs
#endif

/* vec_vctuxs */

#define vec_vctuxs __builtin_altivec_vctuxs

/* vec_signed */

static __inline__ vector signed int __ATTRS_o_ai
vec_sld(vector signed int, vector signed int, unsigned const int __c);

static __inline__ vector signed int __ATTRS_o_ai
vec_signed(vector float __a) {
  return __builtin_convertvector(__a, vector signed int);
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_signed(vector double __a) {
  return __builtin_convertvector(__a, vector signed long long);
}

static __inline__ vector signed int __attribute__((__always_inline__))
vec_signed2(vector double __a, vector double __b) {
  return (vector signed int) { __a[0], __a[1], __b[0], __b[1] };
}

static __inline__ vector signed int __ATTRS_o_ai
vec_signede(vector double __a) {
#ifdef __LITTLE_ENDIAN__
  vector signed int __ret = __builtin_vsx_xvcvdpsxws(__a);
  return vec_sld(__ret, __ret, 12);
#else
  return __builtin_vsx_xvcvdpsxws(__a);
#endif
}

static __inline__ vector signed int __ATTRS_o_ai
vec_signedo(vector double __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_vsx_xvcvdpsxws(__a);
#else
  vector signed int __ret = __builtin_vsx_xvcvdpsxws(__a);
  return vec_sld(__ret, __ret, 12);
#endif
}
#endif

/* vec_unsigned */

static __inline__ vector unsigned int __ATTRS_o_ai
vec_sld(vector unsigned int, vector unsigned int, unsigned const int __c);

static __inline__ vector unsigned int __ATTRS_o_ai
vec_unsigned(vector float __a) {
  return __builtin_convertvector(__a, vector unsigned int);
}

#ifdef __VSX__
static __inline__ vector unsigned long long __ATTRS_o_ai
vec_unsigned(vector double __a) {
  return __builtin_convertvector(__a, vector unsigned long long);
}

static __inline__ vector unsigned int __attribute__((__always_inline__))
vec_unsigned2(vector double __a, vector double __b) {
  return (vector unsigned int) { __a[0], __a[1], __b[0], __b[1] };
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_unsignede(vector double __a) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned int __ret = __builtin_vsx_xvcvdpuxws(__a);
  return vec_sld(__ret, __ret, 12);
#else
  return __builtin_vsx_xvcvdpuxws(__a);
#endif
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_unsignedo(vector double __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_vsx_xvcvdpuxws(__a);
#else
  vector unsigned int __ret = __builtin_vsx_xvcvdpuxws(__a);
  return vec_sld(__ret, __ret, 12);
#endif
}
#endif

/* vec_float */

static __inline__ vector float __ATTRS_o_ai
vec_sld(vector float, vector float, unsigned const int __c);

static __inline__ vector float __ATTRS_o_ai
vec_float(vector signed int __a) {
  return __builtin_convertvector(__a, vector float);
}

static __inline__ vector float __ATTRS_o_ai
vec_float(vector unsigned int __a) {
  return __builtin_convertvector(__a, vector float);
}

#ifdef __VSX__
static __inline__ vector float __ATTRS_o_ai
vec_float2(vector signed long long __a, vector signed long long __b) {
  return (vector float) { __a[0], __a[1], __b[0], __b[1] };
}

static __inline__ vector float __ATTRS_o_ai
vec_float2(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector float) { __a[0], __a[1], __b[0], __b[1] };
}

static __inline__ vector float __ATTRS_o_ai
vec_float2(vector double __a, vector double __b) {
  return (vector float) { __a[0], __a[1], __b[0], __b[1] };
}

static __inline__ vector float __ATTRS_o_ai
vec_floate(vector signed long long __a) {
#ifdef __LITTLE_ENDIAN__
  vector float __ret = __builtin_vsx_xvcvsxdsp(__a);
  return vec_sld(__ret, __ret, 12);
#else
  return __builtin_vsx_xvcvsxdsp(__a);
#endif
}

static __inline__ vector float __ATTRS_o_ai
vec_floate(vector unsigned long long __a) {
#ifdef __LITTLE_ENDIAN__
  vector float __ret = __builtin_vsx_xvcvuxdsp(__a);
  return vec_sld(__ret, __ret, 12);
#else
  return __builtin_vsx_xvcvuxdsp(__a);
#endif
}

static __inline__ vector float __ATTRS_o_ai
vec_floate(vector double __a) {
#ifdef __LITTLE_ENDIAN__
  vector float __ret = __builtin_vsx_xvcvdpsp(__a);
  return vec_sld(__ret, __ret, 12);
#else
  return __builtin_vsx_xvcvdpsp(__a);
#endif
}

static __inline__ vector float __ATTRS_o_ai
vec_floato(vector signed long long __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_vsx_xvcvsxdsp(__a);
#else
  vector float __ret = __builtin_vsx_xvcvsxdsp(__a);
  return vec_sld(__ret, __ret, 12);
#endif
}

static __inline__ vector float __ATTRS_o_ai
vec_floato(vector unsigned long long __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_vsx_xvcvuxdsp(__a);
#else
  vector float __ret = __builtin_vsx_xvcvuxdsp(__a);
  return vec_sld(__ret, __ret, 12);
#endif
}

static __inline__ vector float __ATTRS_o_ai
vec_floato(vector double __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_vsx_xvcvdpsp(__a);
#else
  vector float __ret = __builtin_vsx_xvcvdpsp(__a);
  return vec_sld(__ret, __ret, 12);
#endif
}
#endif

/* vec_double */

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai
vec_double(vector signed long long __a) {
  return __builtin_convertvector(__a, vector double);
}

static __inline__ vector double __ATTRS_o_ai
vec_double(vector unsigned long long __a) {
  return __builtin_convertvector(__a, vector double);
}

static __inline__ vector double __ATTRS_o_ai
vec_doublee(vector signed int __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_vsx_xvcvsxwdp(vec_sld(__a, __a, 4));
#else
  return __builtin_vsx_xvcvsxwdp(__a);
#endif
}

static __inline__ vector double __ATTRS_o_ai
vec_doublee(vector unsigned int __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_vsx_xvcvuxwdp(vec_sld(__a, __a, 4));
#else
  return __builtin_vsx_xvcvuxwdp(__a);
#endif
}

static __inline__ vector double __ATTRS_o_ai
vec_doublee(vector float __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_vsx_xvcvspdp(vec_sld(__a, __a, 4));
#else
  return __builtin_vsx_xvcvspdp(__a);
#endif
}

static __inline__ vector double __ATTRS_o_ai
vec_doubleh(vector signed int __a) {
  vector double __ret = {__a[0], __a[1]};
  return __ret;
}

static __inline__ vector double __ATTRS_o_ai
vec_doubleh(vector unsigned int __a) {
  vector double __ret = {__a[0], __a[1]};
  return __ret;
}

static __inline__ vector double __ATTRS_o_ai
vec_doubleh(vector float __a) {
  vector double __ret = {__a[0], __a[1]};
  return __ret;
}

static __inline__ vector double __ATTRS_o_ai
vec_doublel(vector signed int __a) {
  vector double __ret = {__a[2], __a[3]};
  return __ret;
}

static __inline__ vector double __ATTRS_o_ai
vec_doublel(vector unsigned int __a) {
  vector double __ret = {__a[2], __a[3]};
  return __ret;
}

static __inline__ vector double __ATTRS_o_ai
vec_doublel(vector float __a) {
  vector double __ret = {__a[2], __a[3]};
  return __ret;
}

static __inline__ vector double __ATTRS_o_ai
vec_doubleo(vector signed int __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_vsx_xvcvsxwdp(__a);
#else
  return __builtin_vsx_xvcvsxwdp(vec_sld(__a, __a, 4));
#endif
}

static __inline__ vector double __ATTRS_o_ai
vec_doubleo(vector unsigned int __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_vsx_xvcvuxwdp(__a);
#else
  return __builtin_vsx_xvcvuxwdp(vec_sld(__a, __a, 4));
#endif
}

static __inline__ vector double __ATTRS_o_ai
vec_doubleo(vector float __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_vsx_xvcvspdp(__a);
#else
  return __builtin_vsx_xvcvspdp(vec_sld(__a, __a, 4));
#endif
}
#endif

/* vec_div */

/* Integer vector divides (vectors are scalarized, elements divided
   and the vectors reassembled).
*/
static __inline__ vector signed char __ATTRS_o_ai
vec_div(vector signed char __a, vector signed char __b) {
  return __a / __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_div(vector unsigned char __a, vector unsigned char __b) {
  return __a / __b;
}

static __inline__ vector signed short __ATTRS_o_ai
vec_div(vector signed short __a, vector signed short __b) {
  return __a / __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_div(vector unsigned short __a, vector unsigned short __b) {
  return __a / __b;
}

static __inline__ vector signed int __ATTRS_o_ai
vec_div(vector signed int __a, vector signed int __b) {
  return __a / __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_div(vector unsigned int __a, vector unsigned int __b) {
  return __a / __b;
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_div(vector signed long long __a, vector signed long long __b) {
  return __a / __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_div(vector unsigned long long __a, vector unsigned long long __b) {
  return __a / __b;
}

static __inline__ vector float __ATTRS_o_ai vec_div(vector float __a,
                                                    vector float __b) {
  return __a / __b;
}

static __inline__ vector double __ATTRS_o_ai vec_div(vector double __a,
                                                     vector double __b) {
  return __a / __b;
}
#endif

/* vec_dss */

static __inline__ void __attribute__((__always_inline__)) vec_dss(int __a) {
  __builtin_altivec_dss(__a);
}

/* vec_dssall */

static __inline__ void __attribute__((__always_inline__)) vec_dssall(void) {
  __builtin_altivec_dssall();
}

/* vec_dst */
#define vec_dst(__PTR, __CW, __STR) \
  __extension__(                    \
      { __builtin_altivec_dst((const void *)(__PTR), (__CW), (__STR)); })

/* vec_dstst */
#define vec_dstst(__PTR, __CW, __STR) \
  __extension__(                      \
      { __builtin_altivec_dstst((const void *)(__PTR), (__CW), (__STR)); })

/* vec_dststt */
#define vec_dststt(__PTR, __CW, __STR) \
  __extension__(                       \
      { __builtin_altivec_dststt((const void *)(__PTR), (__CW), (__STR)); })

/* vec_dstt */
#define vec_dstt(__PTR, __CW, __STR) \
  __extension__(                     \
      { __builtin_altivec_dstt((const void *)(__PTR), (__CW), (__STR)); })

/* vec_eqv */

#ifdef __POWER8_VECTOR__
static __inline__ vector signed char __ATTRS_o_ai
vec_eqv(vector signed char __a, vector signed char __b) {
  return (vector signed char)__builtin_vsx_xxleqv((vector unsigned int)__a,
                                                  (vector unsigned int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_eqv(vector unsigned char __a, vector unsigned char __b) {
  return (vector unsigned char)__builtin_vsx_xxleqv((vector unsigned int)__a,
                                                    (vector unsigned int)__b);
}

static __inline__ vector bool char __ATTRS_o_ai vec_eqv(vector bool char __a,
                                                        vector bool char __b) {
  return (vector bool char)__builtin_vsx_xxleqv((vector unsigned int)__a,
                                                (vector unsigned int)__b);
}

static __inline__ vector signed short __ATTRS_o_ai
vec_eqv(vector signed short __a, vector signed short __b) {
  return (vector signed short)__builtin_vsx_xxleqv((vector unsigned int)__a,
                                                   (vector unsigned int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_eqv(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)__builtin_vsx_xxleqv((vector unsigned int)__a,
                                                     (vector unsigned int)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_eqv(vector bool short __a, vector bool short __b) {
  return (vector bool short)__builtin_vsx_xxleqv((vector unsigned int)__a,
                                                 (vector unsigned int)__b);
}

static __inline__ vector signed int __ATTRS_o_ai
vec_eqv(vector signed int __a, vector signed int __b) {
  return (vector signed int)__builtin_vsx_xxleqv((vector unsigned int)__a,
                                                 (vector unsigned int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_eqv(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_vsx_xxleqv(__a, __b);
}

static __inline__ vector bool int __ATTRS_o_ai vec_eqv(vector bool int __a,
                                                       vector bool int __b) {
  return (vector bool int)__builtin_vsx_xxleqv((vector unsigned int)__a,
                                               (vector unsigned int)__b);
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_eqv(vector signed long long __a, vector signed long long __b) {
  return (vector signed long long)__builtin_vsx_xxleqv(
      (vector unsigned int)__a, (vector unsigned int)__b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_eqv(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector unsigned long long)__builtin_vsx_xxleqv(
      (vector unsigned int)__a, (vector unsigned int)__b);
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_eqv(vector bool long long __a, vector bool long long __b) {
  return (vector bool long long)__builtin_vsx_xxleqv((vector unsigned int)__a,
                                                     (vector unsigned int)__b);
}

static __inline__ vector float __ATTRS_o_ai vec_eqv(vector float __a,
                                                    vector float __b) {
  return (vector float)__builtin_vsx_xxleqv((vector unsigned int)__a,
                                            (vector unsigned int)__b);
}

static __inline__ vector double __ATTRS_o_ai vec_eqv(vector double __a,
                                                     vector double __b) {
  return (vector double)__builtin_vsx_xxleqv((vector unsigned int)__a,
                                             (vector unsigned int)__b);
}
#endif

/* vec_expte */

static __inline__ vector float __attribute__((__always_inline__))
vec_expte(vector float __a) {
  return __builtin_altivec_vexptefp(__a);
}

/* vec_vexptefp */

static __inline__ vector float __attribute__((__always_inline__))
vec_vexptefp(vector float __a) {
  return __builtin_altivec_vexptefp(__a);
}

/* vec_floor */

static __inline__ vector float __ATTRS_o_ai vec_floor(vector float __a) {
#ifdef __VSX__
  return __builtin_vsx_xvrspim(__a);
#else
  return __builtin_altivec_vrfim(__a);
#endif
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_floor(vector double __a) {
  return __builtin_vsx_xvrdpim(__a);
}
#endif

/* vec_vrfim */

static __inline__ vector float __attribute__((__always_inline__))
vec_vrfim(vector float __a) {
  return __builtin_altivec_vrfim(__a);
}

/* vec_ld */

static __inline__ vector signed char __ATTRS_o_ai
vec_ld(int __a, const vector signed char *__b) {
  return (vector signed char)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_ld(int __a, const signed char *__b) {
  return (vector signed char)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_ld(int __a, const vector unsigned char *__b) {
  return (vector unsigned char)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_ld(int __a, const unsigned char *__b) {
  return (vector unsigned char)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_ld(int __a, const vector bool char *__b) {
  return (vector bool char)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_ld(int __a,
                                                   const vector short *__b) {
  return (vector short)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_ld(int __a, const short *__b) {
  return (vector short)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_ld(int __a, const vector unsigned short *__b) {
  return (vector unsigned short)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_ld(int __a, const unsigned short *__b) {
  return (vector unsigned short)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_ld(int __a, const vector bool short *__b) {
  return (vector bool short)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_ld(int __a,
                                                   const vector pixel *__b) {
  return (vector pixel)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_ld(int __a,
                                                 const vector int *__b) {
  return (vector int)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_ld(int __a, const int *__b) {
  return (vector int)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_ld(int __a, const vector unsigned int *__b) {
  return (vector unsigned int)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_ld(int __a, const unsigned int *__b) {
  return (vector unsigned int)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_ld(int __a, const vector bool int *__b) {
  return (vector bool int)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector float __ATTRS_o_ai vec_ld(int __a,
                                                   const vector float *__b) {
  return (vector float)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector float __ATTRS_o_ai vec_ld(int __a, const float *__b) {
  return (vector float)__builtin_altivec_lvx(__a, __b);
}

/* vec_lvx */

static __inline__ vector signed char __ATTRS_o_ai
vec_lvx(int __a, const vector signed char *__b) {
  return (vector signed char)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_lvx(int __a, const signed char *__b) {
  return (vector signed char)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvx(int __a, const vector unsigned char *__b) {
  return (vector unsigned char)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvx(int __a, const unsigned char *__b) {
  return (vector unsigned char)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_lvx(int __a, const vector bool char *__b) {
  return (vector bool char)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_lvx(int __a,
                                                    const vector short *__b) {
  return (vector short)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_lvx(int __a, const short *__b) {
  return (vector short)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_lvx(int __a, const vector unsigned short *__b) {
  return (vector unsigned short)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_lvx(int __a, const unsigned short *__b) {
  return (vector unsigned short)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_lvx(int __a, const vector bool short *__b) {
  return (vector bool short)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_lvx(int __a,
                                                    const vector pixel *__b) {
  return (vector pixel)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_lvx(int __a,
                                                  const vector int *__b) {
  return (vector int)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_lvx(int __a, const int *__b) {
  return (vector int)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_lvx(int __a, const vector unsigned int *__b) {
  return (vector unsigned int)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_lvx(int __a, const unsigned int *__b) {
  return (vector unsigned int)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_lvx(int __a, const vector bool int *__b) {
  return (vector bool int)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector float __ATTRS_o_ai vec_lvx(int __a,
                                                    const vector float *__b) {
  return (vector float)__builtin_altivec_lvx(__a, __b);
}

static __inline__ vector float __ATTRS_o_ai vec_lvx(int __a, const float *__b) {
  return (vector float)__builtin_altivec_lvx(__a, __b);
}

/* vec_lde */

static __inline__ vector signed char __ATTRS_o_ai
vec_lde(int __a, const signed char *__b) {
  return (vector signed char)__builtin_altivec_lvebx(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_lde(int __a, const unsigned char *__b) {
  return (vector unsigned char)__builtin_altivec_lvebx(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_lde(int __a, const short *__b) {
  return (vector short)__builtin_altivec_lvehx(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_lde(int __a, const unsigned short *__b) {
  return (vector unsigned short)__builtin_altivec_lvehx(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_lde(int __a, const int *__b) {
  return (vector int)__builtin_altivec_lvewx(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_lde(int __a, const unsigned int *__b) {
  return (vector unsigned int)__builtin_altivec_lvewx(__a, __b);
}

static __inline__ vector float __ATTRS_o_ai vec_lde(int __a, const float *__b) {
  return (vector float)__builtin_altivec_lvewx(__a, __b);
}

/* vec_lvebx */

static __inline__ vector signed char __ATTRS_o_ai
vec_lvebx(int __a, const signed char *__b) {
  return (vector signed char)__builtin_altivec_lvebx(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvebx(int __a, const unsigned char *__b) {
  return (vector unsigned char)__builtin_altivec_lvebx(__a, __b);
}

/* vec_lvehx */

static __inline__ vector short __ATTRS_o_ai vec_lvehx(int __a,
                                                      const short *__b) {
  return (vector short)__builtin_altivec_lvehx(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_lvehx(int __a, const unsigned short *__b) {
  return (vector unsigned short)__builtin_altivec_lvehx(__a, __b);
}

/* vec_lvewx */

static __inline__ vector int __ATTRS_o_ai vec_lvewx(int __a, const int *__b) {
  return (vector int)__builtin_altivec_lvewx(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_lvewx(int __a, const unsigned int *__b) {
  return (vector unsigned int)__builtin_altivec_lvewx(__a, __b);
}

static __inline__ vector float __ATTRS_o_ai vec_lvewx(int __a,
                                                      const float *__b) {
  return (vector float)__builtin_altivec_lvewx(__a, __b);
}

/* vec_ldl */

static __inline__ vector signed char __ATTRS_o_ai
vec_ldl(int __a, const vector signed char *__b) {
  return (vector signed char)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_ldl(int __a, const signed char *__b) {
  return (vector signed char)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_ldl(int __a, const vector unsigned char *__b) {
  return (vector unsigned char)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_ldl(int __a, const unsigned char *__b) {
  return (vector unsigned char)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_ldl(int __a, const vector bool char *__b) {
  return (vector bool char)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_ldl(int __a,
                                                    const vector short *__b) {
  return (vector short)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_ldl(int __a, const short *__b) {
  return (vector short)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_ldl(int __a, const vector unsigned short *__b) {
  return (vector unsigned short)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_ldl(int __a, const unsigned short *__b) {
  return (vector unsigned short)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_ldl(int __a, const vector bool short *__b) {
  return (vector bool short)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_ldl(int __a,
                                                    const vector pixel *__b) {
  return (vector pixel short)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_ldl(int __a,
                                                  const vector int *__b) {
  return (vector int)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_ldl(int __a, const int *__b) {
  return (vector int)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_ldl(int __a, const vector unsigned int *__b) {
  return (vector unsigned int)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_ldl(int __a, const unsigned int *__b) {
  return (vector unsigned int)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_ldl(int __a, const vector bool int *__b) {
  return (vector bool int)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector float __ATTRS_o_ai vec_ldl(int __a,
                                                    const vector float *__b) {
  return (vector float)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector float __ATTRS_o_ai vec_ldl(int __a, const float *__b) {
  return (vector float)__builtin_altivec_lvxl(__a, __b);
}

/* vec_lvxl */

static __inline__ vector signed char __ATTRS_o_ai
vec_lvxl(int __a, const vector signed char *__b) {
  return (vector signed char)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_lvxl(int __a, const signed char *__b) {
  return (vector signed char)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvxl(int __a, const vector unsigned char *__b) {
  return (vector unsigned char)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvxl(int __a, const unsigned char *__b) {
  return (vector unsigned char)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_lvxl(int __a, const vector bool char *__b) {
  return (vector bool char)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_lvxl(int __a,
                                                     const vector short *__b) {
  return (vector short)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_lvxl(int __a,
                                                     const short *__b) {
  return (vector short)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_lvxl(int __a, const vector unsigned short *__b) {
  return (vector unsigned short)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_lvxl(int __a, const unsigned short *__b) {
  return (vector unsigned short)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_lvxl(int __a, const vector bool short *__b) {
  return (vector bool short)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_lvxl(int __a,
                                                     const vector pixel *__b) {
  return (vector pixel)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_lvxl(int __a,
                                                   const vector int *__b) {
  return (vector int)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_lvxl(int __a, const int *__b) {
  return (vector int)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_lvxl(int __a, const vector unsigned int *__b) {
  return (vector unsigned int)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_lvxl(int __a, const unsigned int *__b) {
  return (vector unsigned int)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_lvxl(int __a, const vector bool int *__b) {
  return (vector bool int)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector float __ATTRS_o_ai vec_lvxl(int __a,
                                                     const vector float *__b) {
  return (vector float)__builtin_altivec_lvxl(__a, __b);
}

static __inline__ vector float __ATTRS_o_ai vec_lvxl(int __a,
                                                     const float *__b) {
  return (vector float)__builtin_altivec_lvxl(__a, __b);
}

/* vec_loge */

static __inline__ vector float __attribute__((__always_inline__))
vec_loge(vector float __a) {
  return __builtin_altivec_vlogefp(__a);
}

/* vec_vlogefp */

static __inline__ vector float __attribute__((__always_inline__))
vec_vlogefp(vector float __a) {
  return __builtin_altivec_vlogefp(__a);
}

/* vec_lvsl */

#ifdef __LITTLE_ENDIAN__
static __inline__ vector unsigned char __ATTRS_o_ai
    __attribute__((__deprecated__("use assignment for unaligned little endian \
loads/stores"))) vec_lvsl(int __a, const signed char *__b) {
  vector unsigned char mask =
      (vector unsigned char)__builtin_altivec_lvsl(__a, __b);
  vector unsigned char reverse = {15, 14, 13, 12, 11, 10, 9, 8,
                                  7,  6,  5,  4,  3,  2,  1, 0};
  return vec_perm(mask, mask, reverse);
}
#else
static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvsl(int __a, const signed char *__b) {
  return (vector unsigned char)__builtin_altivec_lvsl(__a, __b);
}
#endif

#ifdef __LITTLE_ENDIAN__
static __inline__ vector unsigned char __ATTRS_o_ai
    __attribute__((__deprecated__("use assignment for unaligned little endian \
loads/stores"))) vec_lvsl(int __a, const unsigned char *__b) {
  vector unsigned char mask =
      (vector unsigned char)__builtin_altivec_lvsl(__a, __b);
  vector unsigned char reverse = {15, 14, 13, 12, 11, 10, 9, 8,
                                  7,  6,  5,  4,  3,  2,  1, 0};
  return vec_perm(mask, mask, reverse);
}
#else
static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvsl(int __a, const unsigned char *__b) {
  return (vector unsigned char)__builtin_altivec_lvsl(__a, __b);
}
#endif

#ifdef __LITTLE_ENDIAN__
static __inline__ vector unsigned char __ATTRS_o_ai
    __attribute__((__deprecated__("use assignment for unaligned little endian \
loads/stores"))) vec_lvsl(int __a, const short *__b) {
  vector unsigned char mask =
      (vector unsigned char)__builtin_altivec_lvsl(__a, __b);
  vector unsigned char reverse = {15, 14, 13, 12, 11, 10, 9, 8,
                                  7,  6,  5,  4,  3,  2,  1, 0};
  return vec_perm(mask, mask, reverse);
}
#else
static __inline__ vector unsigned char __ATTRS_o_ai vec_lvsl(int __a,
                                                             const short *__b) {
  return (vector unsigned char)__builtin_altivec_lvsl(__a, __b);
}
#endif

#ifdef __LITTLE_ENDIAN__
static __inline__ vector unsigned char __ATTRS_o_ai
    __attribute__((__deprecated__("use assignment for unaligned little endian \
loads/stores"))) vec_lvsl(int __a, const unsigned short *__b) {
  vector unsigned char mask =
      (vector unsigned char)__builtin_altivec_lvsl(__a, __b);
  vector unsigned char reverse = {15, 14, 13, 12, 11, 10, 9, 8,
                                  7,  6,  5,  4,  3,  2,  1, 0};
  return vec_perm(mask, mask, reverse);
}
#else
static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvsl(int __a, const unsigned short *__b) {
  return (vector unsigned char)__builtin_altivec_lvsl(__a, __b);
}
#endif

#ifdef __LITTLE_ENDIAN__
static __inline__ vector unsigned char __ATTRS_o_ai
    __attribute__((__deprecated__("use assignment for unaligned little endian \
loads/stores"))) vec_lvsl(int __a, const int *__b) {
  vector unsigned char mask =
      (vector unsigned char)__builtin_altivec_lvsl(__a, __b);
  vector unsigned char reverse = {15, 14, 13, 12, 11, 10, 9, 8,
                                  7,  6,  5,  4,  3,  2,  1, 0};
  return vec_perm(mask, mask, reverse);
}
#else
static __inline__ vector unsigned char __ATTRS_o_ai vec_lvsl(int __a,
                                                             const int *__b) {
  return (vector unsigned char)__builtin_altivec_lvsl(__a, __b);
}
#endif

#ifdef __LITTLE_ENDIAN__
static __inline__ vector unsigned char __ATTRS_o_ai
    __attribute__((__deprecated__("use assignment for unaligned little endian \
loads/stores"))) vec_lvsl(int __a, const unsigned int *__b) {
  vector unsigned char mask =
      (vector unsigned char)__builtin_altivec_lvsl(__a, __b);
  vector unsigned char reverse = {15, 14, 13, 12, 11, 10, 9, 8,
                                  7,  6,  5,  4,  3,  2,  1, 0};
  return vec_perm(mask, mask, reverse);
}
#else
static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvsl(int __a, const unsigned int *__b) {
  return (vector unsigned char)__builtin_altivec_lvsl(__a, __b);
}
#endif

#ifdef __LITTLE_ENDIAN__
static __inline__ vector unsigned char __ATTRS_o_ai
    __attribute__((__deprecated__("use assignment for unaligned little endian \
loads/stores"))) vec_lvsl(int __a, const float *__b) {
  vector unsigned char mask =
      (vector unsigned char)__builtin_altivec_lvsl(__a, __b);
  vector unsigned char reverse = {15, 14, 13, 12, 11, 10, 9, 8,
                                  7,  6,  5,  4,  3,  2,  1, 0};
  return vec_perm(mask, mask, reverse);
}
#else
static __inline__ vector unsigned char __ATTRS_o_ai vec_lvsl(int __a,
                                                             const float *__b) {
  return (vector unsigned char)__builtin_altivec_lvsl(__a, __b);
}
#endif

/* vec_lvsr */

#ifdef __LITTLE_ENDIAN__
static __inline__ vector unsigned char __ATTRS_o_ai
    __attribute__((__deprecated__("use assignment for unaligned little endian \
loads/stores"))) vec_lvsr(int __a, const signed char *__b) {
  vector unsigned char mask =
      (vector unsigned char)__builtin_altivec_lvsr(__a, __b);
  vector unsigned char reverse = {15, 14, 13, 12, 11, 10, 9, 8,
                                  7,  6,  5,  4,  3,  2,  1, 0};
  return vec_perm(mask, mask, reverse);
}
#else
static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvsr(int __a, const signed char *__b) {
  return (vector unsigned char)__builtin_altivec_lvsr(__a, __b);
}
#endif

#ifdef __LITTLE_ENDIAN__
static __inline__ vector unsigned char __ATTRS_o_ai
    __attribute__((__deprecated__("use assignment for unaligned little endian \
loads/stores"))) vec_lvsr(int __a, const unsigned char *__b) {
  vector unsigned char mask =
      (vector unsigned char)__builtin_altivec_lvsr(__a, __b);
  vector unsigned char reverse = {15, 14, 13, 12, 11, 10, 9, 8,
                                  7,  6,  5,  4,  3,  2,  1, 0};
  return vec_perm(mask, mask, reverse);
}
#else
static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvsr(int __a, const unsigned char *__b) {
  return (vector unsigned char)__builtin_altivec_lvsr(__a, __b);
}
#endif

#ifdef __LITTLE_ENDIAN__
static __inline__ vector unsigned char __ATTRS_o_ai
    __attribute__((__deprecated__("use assignment for unaligned little endian \
loads/stores"))) vec_lvsr(int __a, const short *__b) {
  vector unsigned char mask =
      (vector unsigned char)__builtin_altivec_lvsr(__a, __b);
  vector unsigned char reverse = {15, 14, 13, 12, 11, 10, 9, 8,
                                  7,  6,  5,  4,  3,  2,  1, 0};
  return vec_perm(mask, mask, reverse);
}
#else
static __inline__ vector unsigned char __ATTRS_o_ai vec_lvsr(int __a,
                                                             const short *__b) {
  return (vector unsigned char)__builtin_altivec_lvsr(__a, __b);
}
#endif

#ifdef __LITTLE_ENDIAN__
static __inline__ vector unsigned char __ATTRS_o_ai
    __attribute__((__deprecated__("use assignment for unaligned little endian \
loads/stores"))) vec_lvsr(int __a, const unsigned short *__b) {
  vector unsigned char mask =
      (vector unsigned char)__builtin_altivec_lvsr(__a, __b);
  vector unsigned char reverse = {15, 14, 13, 12, 11, 10, 9, 8,
                                  7,  6,  5,  4,  3,  2,  1, 0};
  return vec_perm(mask, mask, reverse);
}
#else
static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvsr(int __a, const unsigned short *__b) {
  return (vector unsigned char)__builtin_altivec_lvsr(__a, __b);
}
#endif

#ifdef __LITTLE_ENDIAN__
static __inline__ vector unsigned char __ATTRS_o_ai
    __attribute__((__deprecated__("use assignment for unaligned little endian \
loads/stores"))) vec_lvsr(int __a, const int *__b) {
  vector unsigned char mask =
      (vector unsigned char)__builtin_altivec_lvsr(__a, __b);
  vector unsigned char reverse = {15, 14, 13, 12, 11, 10, 9, 8,
                                  7,  6,  5,  4,  3,  2,  1, 0};
  return vec_perm(mask, mask, reverse);
}
#else
static __inline__ vector unsigned char __ATTRS_o_ai vec_lvsr(int __a,
                                                             const int *__b) {
  return (vector unsigned char)__builtin_altivec_lvsr(__a, __b);
}
#endif

#ifdef __LITTLE_ENDIAN__
static __inline__ vector unsigned char __ATTRS_o_ai
    __attribute__((__deprecated__("use assignment for unaligned little endian \
loads/stores"))) vec_lvsr(int __a, const unsigned int *__b) {
  vector unsigned char mask =
      (vector unsigned char)__builtin_altivec_lvsr(__a, __b);
  vector unsigned char reverse = {15, 14, 13, 12, 11, 10, 9, 8,
                                  7,  6,  5,  4,  3,  2,  1, 0};
  return vec_perm(mask, mask, reverse);
}
#else
static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvsr(int __a, const unsigned int *__b) {
  return (vector unsigned char)__builtin_altivec_lvsr(__a, __b);
}
#endif

#ifdef __LITTLE_ENDIAN__
static __inline__ vector unsigned char __ATTRS_o_ai
    __attribute__((__deprecated__("use assignment for unaligned little endian \
loads/stores"))) vec_lvsr(int __a, const float *__b) {
  vector unsigned char mask =
      (vector unsigned char)__builtin_altivec_lvsr(__a, __b);
  vector unsigned char reverse = {15, 14, 13, 12, 11, 10, 9, 8,
                                  7,  6,  5,  4,  3,  2,  1, 0};
  return vec_perm(mask, mask, reverse);
}
#else
static __inline__ vector unsigned char __ATTRS_o_ai vec_lvsr(int __a,
                                                             const float *__b) {
  return (vector unsigned char)__builtin_altivec_lvsr(__a, __b);
}
#endif

/* vec_madd */
static __inline__ vector signed short __ATTRS_o_ai
vec_mladd(vector signed short, vector signed short, vector signed short);
static __inline__ vector signed short __ATTRS_o_ai
vec_mladd(vector signed short, vector unsigned short, vector unsigned short);
static __inline__ vector signed short __ATTRS_o_ai
vec_mladd(vector unsigned short, vector signed short, vector signed short);
static __inline__ vector unsigned short __ATTRS_o_ai
vec_mladd(vector unsigned short, vector unsigned short, vector unsigned short);

static __inline__ vector signed short __ATTRS_o_ai vec_madd(
    vector signed short __a, vector signed short __b, vector signed short __c) {
  return vec_mladd(__a, __b, __c);
}

static __inline__ vector signed short __ATTRS_o_ai
vec_madd(vector signed short __a, vector unsigned short __b,
         vector unsigned short __c) {
  return vec_mladd(__a, __b, __c);
}

static __inline__ vector signed short __ATTRS_o_ai
vec_madd(vector unsigned short __a, vector signed short __b,
         vector signed short __c) {
  return vec_mladd(__a, __b, __c);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_madd(vector unsigned short __a, vector unsigned short __b,
         vector unsigned short __c) {
  return vec_mladd(__a, __b, __c);
}

static __inline__ vector float __ATTRS_o_ai vec_madd(vector float __a,
                                                     vector float __b,
                                                     vector float __c) {
#ifdef __VSX__
  return __builtin_vsx_xvmaddasp(__a, __b, __c);
#else
  return __builtin_altivec_vmaddfp(__a, __b, __c);
#endif
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_madd(vector double __a,
                                                      vector double __b,
                                                      vector double __c) {
  return __builtin_vsx_xvmaddadp(__a, __b, __c);
}
#endif

/* vec_vmaddfp */

static __inline__ vector float __attribute__((__always_inline__))
vec_vmaddfp(vector float __a, vector float __b, vector float __c) {
  return __builtin_altivec_vmaddfp(__a, __b, __c);
}

/* vec_madds */

static __inline__ vector signed short __attribute__((__always_inline__))
vec_madds(vector signed short __a, vector signed short __b,
          vector signed short __c) {
  return __builtin_altivec_vmhaddshs(__a, __b, __c);
}

/* vec_vmhaddshs */
static __inline__ vector signed short __attribute__((__always_inline__))
vec_vmhaddshs(vector signed short __a, vector signed short __b,
              vector signed short __c) {
  return __builtin_altivec_vmhaddshs(__a, __b, __c);
}

/* vec_msub */

#ifdef __VSX__
static __inline__ vector float __ATTRS_o_ai vec_msub(vector float __a,
                                                     vector float __b,
                                                     vector float __c) {
  return __builtin_vsx_xvmsubasp(__a, __b, __c);
}

static __inline__ vector double __ATTRS_o_ai vec_msub(vector double __a,
                                                      vector double __b,
                                                      vector double __c) {
  return __builtin_vsx_xvmsubadp(__a, __b, __c);
}
#endif

/* vec_max */

static __inline__ vector signed char __ATTRS_o_ai
vec_max(vector signed char __a, vector signed char __b) {
  return __builtin_altivec_vmaxsb(__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_max(vector bool char __a, vector signed char __b) {
  return __builtin_altivec_vmaxsb((vector signed char)__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_max(vector signed char __a, vector bool char __b) {
  return __builtin_altivec_vmaxsb(__a, (vector signed char)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_max(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_altivec_vmaxub(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_max(vector bool char __a, vector unsigned char __b) {
  return __builtin_altivec_vmaxub((vector unsigned char)__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_max(vector unsigned char __a, vector bool char __b) {
  return __builtin_altivec_vmaxub(__a, (vector unsigned char)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_max(vector short __a,
                                                    vector short __b) {
  return __builtin_altivec_vmaxsh(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_max(vector bool short __a,
                                                    vector short __b) {
  return __builtin_altivec_vmaxsh((vector short)__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_max(vector short __a,
                                                    vector bool short __b) {
  return __builtin_altivec_vmaxsh(__a, (vector short)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_max(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_altivec_vmaxuh(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_max(vector bool short __a, vector unsigned short __b) {
  return __builtin_altivec_vmaxuh((vector unsigned short)__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_max(vector unsigned short __a, vector bool short __b) {
  return __builtin_altivec_vmaxuh(__a, (vector unsigned short)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_max(vector int __a,
                                                  vector int __b) {
  return __builtin_altivec_vmaxsw(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_max(vector bool int __a,
                                                  vector int __b) {
  return __builtin_altivec_vmaxsw((vector int)__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_max(vector int __a,
                                                  vector bool int __b) {
  return __builtin_altivec_vmaxsw(__a, (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_max(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_altivec_vmaxuw(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_max(vector bool int __a, vector unsigned int __b) {
  return __builtin_altivec_vmaxuw((vector unsigned int)__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_max(vector unsigned int __a, vector bool int __b) {
  return __builtin_altivec_vmaxuw(__a, (vector unsigned int)__b);
}

#ifdef __POWER8_VECTOR__
static __inline__ vector signed long long __ATTRS_o_ai
vec_max(vector signed long long __a, vector signed long long __b) {
  return __builtin_altivec_vmaxsd(__a, __b);
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_max(vector bool long long __a, vector signed long long __b) {
  return __builtin_altivec_vmaxsd((vector signed long long)__a, __b);
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_max(vector signed long long __a, vector bool long long __b) {
  return __builtin_altivec_vmaxsd(__a, (vector signed long long)__b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_max(vector unsigned long long __a, vector unsigned long long __b) {
  return __builtin_altivec_vmaxud(__a, __b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_max(vector bool long long __a, vector unsigned long long __b) {
  return __builtin_altivec_vmaxud((vector unsigned long long)__a, __b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_max(vector unsigned long long __a, vector bool long long __b) {
  return __builtin_altivec_vmaxud(__a, (vector unsigned long long)__b);
}
#endif

static __inline__ vector float __ATTRS_o_ai vec_max(vector float __a,
                                                    vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvmaxsp(__a, __b);
#else
  return __builtin_altivec_vmaxfp(__a, __b);
#endif
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_max(vector double __a,
                                                     vector double __b) {
  return __builtin_vsx_xvmaxdp(__a, __b);
}
#endif

/* vec_vmaxsb */

static __inline__ vector signed char __ATTRS_o_ai
vec_vmaxsb(vector signed char __a, vector signed char __b) {
  return __builtin_altivec_vmaxsb(__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vmaxsb(vector bool char __a, vector signed char __b) {
  return __builtin_altivec_vmaxsb((vector signed char)__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vmaxsb(vector signed char __a, vector bool char __b) {
  return __builtin_altivec_vmaxsb(__a, (vector signed char)__b);
}

/* vec_vmaxub */

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vmaxub(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_altivec_vmaxub(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vmaxub(vector bool char __a, vector unsigned char __b) {
  return __builtin_altivec_vmaxub((vector unsigned char)__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vmaxub(vector unsigned char __a, vector bool char __b) {
  return __builtin_altivec_vmaxub(__a, (vector unsigned char)__b);
}

/* vec_vmaxsh */

static __inline__ vector short __ATTRS_o_ai vec_vmaxsh(vector short __a,
                                                       vector short __b) {
  return __builtin_altivec_vmaxsh(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_vmaxsh(vector bool short __a,
                                                       vector short __b) {
  return __builtin_altivec_vmaxsh((vector short)__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_vmaxsh(vector short __a,
                                                       vector bool short __b) {
  return __builtin_altivec_vmaxsh(__a, (vector short)__b);
}

/* vec_vmaxuh */

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vmaxuh(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_altivec_vmaxuh(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vmaxuh(vector bool short __a, vector unsigned short __b) {
  return __builtin_altivec_vmaxuh((vector unsigned short)__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vmaxuh(vector unsigned short __a, vector bool short __b) {
  return __builtin_altivec_vmaxuh(__a, (vector unsigned short)__b);
}

/* vec_vmaxsw */

static __inline__ vector int __ATTRS_o_ai vec_vmaxsw(vector int __a,
                                                     vector int __b) {
  return __builtin_altivec_vmaxsw(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_vmaxsw(vector bool int __a,
                                                     vector int __b) {
  return __builtin_altivec_vmaxsw((vector int)__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_vmaxsw(vector int __a,
                                                     vector bool int __b) {
  return __builtin_altivec_vmaxsw(__a, (vector int)__b);
}

/* vec_vmaxuw */

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vmaxuw(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_altivec_vmaxuw(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vmaxuw(vector bool int __a, vector unsigned int __b) {
  return __builtin_altivec_vmaxuw((vector unsigned int)__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vmaxuw(vector unsigned int __a, vector bool int __b) {
  return __builtin_altivec_vmaxuw(__a, (vector unsigned int)__b);
}

/* vec_vmaxfp */

static __inline__ vector float __attribute__((__always_inline__))
vec_vmaxfp(vector float __a, vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvmaxsp(__a, __b);
#else
  return __builtin_altivec_vmaxfp(__a, __b);
#endif
}

/* vec_mergeh */

static __inline__ vector signed char __ATTRS_o_ai
vec_mergeh(vector signed char __a, vector signed char __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x10, 0x01, 0x11, 0x02, 0x12,
                                         0x03, 0x13, 0x04, 0x14, 0x05, 0x15,
                                         0x06, 0x16, 0x07, 0x17));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_mergeh(vector unsigned char __a, vector unsigned char __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x10, 0x01, 0x11, 0x02, 0x12,
                                         0x03, 0x13, 0x04, 0x14, 0x05, 0x15,
                                         0x06, 0x16, 0x07, 0x17));
}

static __inline__ vector bool char __ATTRS_o_ai
vec_mergeh(vector bool char __a, vector bool char __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x10, 0x01, 0x11, 0x02, 0x12,
                                         0x03, 0x13, 0x04, 0x14, 0x05, 0x15,
                                         0x06, 0x16, 0x07, 0x17));
}

static __inline__ vector short __ATTRS_o_ai vec_mergeh(vector short __a,
                                                       vector short __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x10, 0x11, 0x02, 0x03,
                                         0x12, 0x13, 0x04, 0x05, 0x14, 0x15,
                                         0x06, 0x07, 0x16, 0x17));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_mergeh(vector unsigned short __a, vector unsigned short __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x10, 0x11, 0x02, 0x03,
                                         0x12, 0x13, 0x04, 0x05, 0x14, 0x15,
                                         0x06, 0x07, 0x16, 0x17));
}

static __inline__ vector bool short __ATTRS_o_ai
vec_mergeh(vector bool short __a, vector bool short __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x10, 0x11, 0x02, 0x03,
                                         0x12, 0x13, 0x04, 0x05, 0x14, 0x15,
                                         0x06, 0x07, 0x16, 0x17));
}

static __inline__ vector pixel __ATTRS_o_ai vec_mergeh(vector pixel __a,
                                                       vector pixel __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x10, 0x11, 0x02, 0x03,
                                         0x12, 0x13, 0x04, 0x05, 0x14, 0x15,
                                         0x06, 0x07, 0x16, 0x17));
}

static __inline__ vector int __ATTRS_o_ai vec_mergeh(vector int __a,
                                                     vector int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x10, 0x11,
                                         0x12, 0x13, 0x04, 0x05, 0x06, 0x07,
                                         0x14, 0x15, 0x16, 0x17));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_mergeh(vector unsigned int __a, vector unsigned int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x10, 0x11,
                                         0x12, 0x13, 0x04, 0x05, 0x06, 0x07,
                                         0x14, 0x15, 0x16, 0x17));
}

static __inline__ vector bool int __ATTRS_o_ai vec_mergeh(vector bool int __a,
                                                          vector bool int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x10, 0x11,
                                         0x12, 0x13, 0x04, 0x05, 0x06, 0x07,
                                         0x14, 0x15, 0x16, 0x17));
}

static __inline__ vector float __ATTRS_o_ai vec_mergeh(vector float __a,
                                                       vector float __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x10, 0x11,
                                         0x12, 0x13, 0x04, 0x05, 0x06, 0x07,
                                         0x14, 0x15, 0x16, 0x17));
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_mergeh(vector signed long long __a, vector signed long long __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                         0x06, 0x07, 0x10, 0x11, 0x12, 0x13,
                                         0x14, 0x15, 0x16, 0x17));
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_mergeh(vector signed long long __a, vector bool long long __b) {
  return vec_perm(__a, (vector signed long long)__b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                         0x06, 0x07, 0x10, 0x11, 0x12, 0x13,
                                         0x14, 0x15, 0x16, 0x17));
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_mergeh(vector bool long long __a, vector signed long long __b) {
  return vec_perm((vector signed long long)__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                         0x06, 0x07, 0x10, 0x11, 0x12, 0x13,
                                         0x14, 0x15, 0x16, 0x17));
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_mergeh(vector unsigned long long __a, vector unsigned long long __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                         0x06, 0x07, 0x10, 0x11, 0x12, 0x13,
                                         0x14, 0x15, 0x16, 0x17));
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_mergeh(vector unsigned long long __a, vector bool long long __b) {
  return vec_perm(__a, (vector unsigned long long)__b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                         0x06, 0x07, 0x10, 0x11, 0x12, 0x13,
                                         0x14, 0x15, 0x16, 0x17));
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_mergeh(vector bool long long __a, vector unsigned long long __b) {
  return vec_perm((vector unsigned long long)__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                         0x06, 0x07, 0x10, 0x11, 0x12, 0x13,
                                         0x14, 0x15, 0x16, 0x17));
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_mergeh(vector bool long long __a, vector bool long long __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                         0x06, 0x07, 0x10, 0x11, 0x12, 0x13,
                                         0x14, 0x15, 0x16, 0x17));
}

static __inline__ vector double __ATTRS_o_ai vec_mergeh(vector double __a,
                                                        vector double __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                         0x06, 0x07, 0x10, 0x11, 0x12, 0x13,
                                         0x14, 0x15, 0x16, 0x17));
}
static __inline__ vector double __ATTRS_o_ai
vec_mergeh(vector double __a, vector bool long long __b) {
  return vec_perm(__a, (vector double)__b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                         0x06, 0x07, 0x10, 0x11, 0x12, 0x13,
                                         0x14, 0x15, 0x16, 0x17));
}
static __inline__ vector double __ATTRS_o_ai
vec_mergeh(vector bool long long __a, vector double __b) {
  return vec_perm((vector double)__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                         0x06, 0x07, 0x10, 0x11, 0x12, 0x13,
                                         0x14, 0x15, 0x16, 0x17));
}
#endif

/* vec_vmrghb */

#define __builtin_altivec_vmrghb vec_vmrghb

static __inline__ vector signed char __ATTRS_o_ai
vec_vmrghb(vector signed char __a, vector signed char __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x10, 0x01, 0x11, 0x02, 0x12,
                                         0x03, 0x13, 0x04, 0x14, 0x05, 0x15,
                                         0x06, 0x16, 0x07, 0x17));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vmrghb(vector unsigned char __a, vector unsigned char __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x10, 0x01, 0x11, 0x02, 0x12,
                                         0x03, 0x13, 0x04, 0x14, 0x05, 0x15,
                                         0x06, 0x16, 0x07, 0x17));
}

static __inline__ vector bool char __ATTRS_o_ai
vec_vmrghb(vector bool char __a, vector bool char __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x10, 0x01, 0x11, 0x02, 0x12,
                                         0x03, 0x13, 0x04, 0x14, 0x05, 0x15,
                                         0x06, 0x16, 0x07, 0x17));
}

/* vec_vmrghh */

#define __builtin_altivec_vmrghh vec_vmrghh

static __inline__ vector short __ATTRS_o_ai vec_vmrghh(vector short __a,
                                                       vector short __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x10, 0x11, 0x02, 0x03,
                                         0x12, 0x13, 0x04, 0x05, 0x14, 0x15,
                                         0x06, 0x07, 0x16, 0x17));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vmrghh(vector unsigned short __a, vector unsigned short __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x10, 0x11, 0x02, 0x03,
                                         0x12, 0x13, 0x04, 0x05, 0x14, 0x15,
                                         0x06, 0x07, 0x16, 0x17));
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vmrghh(vector bool short __a, vector bool short __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x10, 0x11, 0x02, 0x03,
                                         0x12, 0x13, 0x04, 0x05, 0x14, 0x15,
                                         0x06, 0x07, 0x16, 0x17));
}

static __inline__ vector pixel __ATTRS_o_ai vec_vmrghh(vector pixel __a,
                                                       vector pixel __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x10, 0x11, 0x02, 0x03,
                                         0x12, 0x13, 0x04, 0x05, 0x14, 0x15,
                                         0x06, 0x07, 0x16, 0x17));
}

/* vec_vmrghw */

#define __builtin_altivec_vmrghw vec_vmrghw

static __inline__ vector int __ATTRS_o_ai vec_vmrghw(vector int __a,
                                                     vector int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x10, 0x11,
                                         0x12, 0x13, 0x04, 0x05, 0x06, 0x07,
                                         0x14, 0x15, 0x16, 0x17));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vmrghw(vector unsigned int __a, vector unsigned int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x10, 0x11,
                                         0x12, 0x13, 0x04, 0x05, 0x06, 0x07,
                                         0x14, 0x15, 0x16, 0x17));
}

static __inline__ vector bool int __ATTRS_o_ai vec_vmrghw(vector bool int __a,
                                                          vector bool int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x10, 0x11,
                                         0x12, 0x13, 0x04, 0x05, 0x06, 0x07,
                                         0x14, 0x15, 0x16, 0x17));
}

static __inline__ vector float __ATTRS_o_ai vec_vmrghw(vector float __a,
                                                       vector float __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x10, 0x11,
                                         0x12, 0x13, 0x04, 0x05, 0x06, 0x07,
                                         0x14, 0x15, 0x16, 0x17));
}

/* vec_mergel */

static __inline__ vector signed char __ATTRS_o_ai
vec_mergel(vector signed char __a, vector signed char __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x18, 0x09, 0x19, 0x0A, 0x1A,
                                         0x0B, 0x1B, 0x0C, 0x1C, 0x0D, 0x1D,
                                         0x0E, 0x1E, 0x0F, 0x1F));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_mergel(vector unsigned char __a, vector unsigned char __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x18, 0x09, 0x19, 0x0A, 0x1A,
                                         0x0B, 0x1B, 0x0C, 0x1C, 0x0D, 0x1D,
                                         0x0E, 0x1E, 0x0F, 0x1F));
}

static __inline__ vector bool char __ATTRS_o_ai
vec_mergel(vector bool char __a, vector bool char __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x18, 0x09, 0x19, 0x0A, 0x1A,
                                         0x0B, 0x1B, 0x0C, 0x1C, 0x0D, 0x1D,
                                         0x0E, 0x1E, 0x0F, 0x1F));
}

static __inline__ vector short __ATTRS_o_ai vec_mergel(vector short __a,
                                                       vector short __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x18, 0x19, 0x0A, 0x0B,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x1C, 0x1D,
                                         0x0E, 0x0F, 0x1E, 0x1F));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_mergel(vector unsigned short __a, vector unsigned short __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x18, 0x19, 0x0A, 0x0B,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x1C, 0x1D,
                                         0x0E, 0x0F, 0x1E, 0x1F));
}

static __inline__ vector bool short __ATTRS_o_ai
vec_mergel(vector bool short __a, vector bool short __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x18, 0x19, 0x0A, 0x0B,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x1C, 0x1D,
                                         0x0E, 0x0F, 0x1E, 0x1F));
}

static __inline__ vector pixel __ATTRS_o_ai vec_mergel(vector pixel __a,
                                                       vector pixel __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x18, 0x19, 0x0A, 0x0B,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x1C, 0x1D,
                                         0x0E, 0x0F, 0x1E, 0x1F));
}

static __inline__ vector int __ATTRS_o_ai vec_mergel(vector int __a,
                                                     vector int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x18, 0x19,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x0E, 0x0F,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_mergel(vector unsigned int __a, vector unsigned int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x18, 0x19,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x0E, 0x0F,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}

static __inline__ vector bool int __ATTRS_o_ai vec_mergel(vector bool int __a,
                                                          vector bool int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x18, 0x19,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x0E, 0x0F,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}

static __inline__ vector float __ATTRS_o_ai vec_mergel(vector float __a,
                                                       vector float __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x18, 0x19,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x0E, 0x0F,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_mergel(vector signed long long __a, vector signed long long __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                                         0x0E, 0x0F, 0x18, 0X19, 0x1A, 0x1B,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}
static __inline__ vector signed long long __ATTRS_o_ai
vec_mergel(vector signed long long __a, vector bool long long __b) {
  return vec_perm(__a, (vector signed long long)__b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                                         0x0E, 0x0F, 0x18, 0X19, 0x1A, 0x1B,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}
static __inline__ vector signed long long __ATTRS_o_ai
vec_mergel(vector bool long long __a, vector signed long long __b) {
  return vec_perm((vector signed long long)__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                                         0x0E, 0x0F, 0x18, 0X19, 0x1A, 0x1B,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}
static __inline__ vector unsigned long long __ATTRS_o_ai
vec_mergel(vector unsigned long long __a, vector unsigned long long __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                                         0x0E, 0x0F, 0x18, 0X19, 0x1A, 0x1B,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}
static __inline__ vector unsigned long long __ATTRS_o_ai
vec_mergel(vector unsigned long long __a, vector bool long long __b) {
  return vec_perm(__a, (vector unsigned long long)__b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                                         0x0E, 0x0F, 0x18, 0X19, 0x1A, 0x1B,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}
static __inline__ vector unsigned long long __ATTRS_o_ai
vec_mergel(vector bool long long __a, vector unsigned long long __b) {
  return vec_perm((vector unsigned long long)__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                                         0x0E, 0x0F, 0x18, 0X19, 0x1A, 0x1B,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}
static __inline__ vector bool long long __ATTRS_o_ai
vec_mergel(vector bool long long __a, vector bool long long __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                                         0x0E, 0x0F, 0x18, 0X19, 0x1A, 0x1B,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}
static __inline__ vector double __ATTRS_o_ai vec_mergel(vector double __a,
                                                        vector double __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                                         0x0E, 0x0F, 0x18, 0X19, 0x1A, 0x1B,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}
static __inline__ vector double __ATTRS_o_ai
vec_mergel(vector double __a, vector bool long long __b) {
  return vec_perm(__a, (vector double)__b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                                         0x0E, 0x0F, 0x18, 0X19, 0x1A, 0x1B,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}
static __inline__ vector double __ATTRS_o_ai
vec_mergel(vector bool long long __a, vector double __b) {
  return vec_perm((vector double)__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                                         0x0E, 0x0F, 0x18, 0X19, 0x1A, 0x1B,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}
#endif

/* vec_vmrglb */

#define __builtin_altivec_vmrglb vec_vmrglb

static __inline__ vector signed char __ATTRS_o_ai
vec_vmrglb(vector signed char __a, vector signed char __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x18, 0x09, 0x19, 0x0A, 0x1A,
                                         0x0B, 0x1B, 0x0C, 0x1C, 0x0D, 0x1D,
                                         0x0E, 0x1E, 0x0F, 0x1F));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vmrglb(vector unsigned char __a, vector unsigned char __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x18, 0x09, 0x19, 0x0A, 0x1A,
                                         0x0B, 0x1B, 0x0C, 0x1C, 0x0D, 0x1D,
                                         0x0E, 0x1E, 0x0F, 0x1F));
}

static __inline__ vector bool char __ATTRS_o_ai
vec_vmrglb(vector bool char __a, vector bool char __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x18, 0x09, 0x19, 0x0A, 0x1A,
                                         0x0B, 0x1B, 0x0C, 0x1C, 0x0D, 0x1D,
                                         0x0E, 0x1E, 0x0F, 0x1F));
}

/* vec_vmrglh */

#define __builtin_altivec_vmrglh vec_vmrglh

static __inline__ vector short __ATTRS_o_ai vec_vmrglh(vector short __a,
                                                       vector short __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x18, 0x19, 0x0A, 0x0B,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x1C, 0x1D,
                                         0x0E, 0x0F, 0x1E, 0x1F));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vmrglh(vector unsigned short __a, vector unsigned short __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x18, 0x19, 0x0A, 0x0B,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x1C, 0x1D,
                                         0x0E, 0x0F, 0x1E, 0x1F));
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vmrglh(vector bool short __a, vector bool short __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x18, 0x19, 0x0A, 0x0B,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x1C, 0x1D,
                                         0x0E, 0x0F, 0x1E, 0x1F));
}

static __inline__ vector pixel __ATTRS_o_ai vec_vmrglh(vector pixel __a,
                                                       vector pixel __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x18, 0x19, 0x0A, 0x0B,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x1C, 0x1D,
                                         0x0E, 0x0F, 0x1E, 0x1F));
}

/* vec_vmrglw */

#define __builtin_altivec_vmrglw vec_vmrglw

static __inline__ vector int __ATTRS_o_ai vec_vmrglw(vector int __a,
                                                     vector int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x18, 0x19,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x0E, 0x0F,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vmrglw(vector unsigned int __a, vector unsigned int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x18, 0x19,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x0E, 0x0F,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}

static __inline__ vector bool int __ATTRS_o_ai vec_vmrglw(vector bool int __a,
                                                          vector bool int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x18, 0x19,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x0E, 0x0F,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}

static __inline__ vector float __ATTRS_o_ai vec_vmrglw(vector float __a,
                                                       vector float __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x08, 0x09, 0x0A, 0x0B, 0x18, 0x19,
                                         0x1A, 0x1B, 0x0C, 0x0D, 0x0E, 0x0F,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}

#ifdef __POWER8_VECTOR__
/* vec_mergee */

static __inline__ vector bool int __ATTRS_o_ai vec_mergee(vector bool int __a,
                                                          vector bool int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x10, 0x11,
                                         0x12, 0x13, 0x08, 0x09, 0x0A, 0x0B,
                                         0x18, 0x19, 0x1A, 0x1B));
}

static __inline__ vector signed int __ATTRS_o_ai
vec_mergee(vector signed int __a, vector signed int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x10, 0x11,
                                         0x12, 0x13, 0x08, 0x09, 0x0A, 0x0B,
                                         0x18, 0x19, 0x1A, 0x1B));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_mergee(vector unsigned int __a, vector unsigned int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x10, 0x11,
                                         0x12, 0x13, 0x08, 0x09, 0x0A, 0x0B,
                                         0x18, 0x19, 0x1A, 0x1B));
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_mergee(vector bool long long __a, vector bool long long __b) {
  return vec_mergeh(__a, __b);
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_mergee(vector signed long long __a, vector signed long long __b) {
  return vec_mergeh(__a, __b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_mergee(vector unsigned long long __a, vector unsigned long long __b) {
  return vec_mergeh(__a, __b);
}

static __inline__ vector float __ATTRS_o_ai
vec_mergee(vector float __a, vector float __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x10, 0x11,
                                         0x12, 0x13, 0x08, 0x09, 0x0A, 0x0B,
                                         0x18, 0x19, 0x1A, 0x1B));
}

static __inline__ vector double __ATTRS_o_ai
vec_mergee(vector double __a, vector double __b) {
  return vec_mergeh(__a, __b);
}

/* vec_mergeo */

static __inline__ vector bool int __ATTRS_o_ai vec_mergeo(vector bool int __a,
                                                          vector bool int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x04, 0x05, 0x06, 0x07, 0x14, 0x15,
                                         0x16, 0x17, 0x0C, 0x0D, 0x0E, 0x0F,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}

static __inline__ vector signed int __ATTRS_o_ai
vec_mergeo(vector signed int __a, vector signed int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x04, 0x05, 0x06, 0x07, 0x14, 0x15,
                                         0x16, 0x17, 0x0C, 0x0D, 0x0E, 0x0F,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_mergeo(vector unsigned int __a, vector unsigned int __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x04, 0x05, 0x06, 0x07, 0x14, 0x15,
                                         0x16, 0x17, 0x0C, 0x0D, 0x0E, 0x0F,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_mergeo(vector bool long long __a, vector bool long long __b) {
  return vec_mergel(__a, __b);
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_mergeo(vector signed long long __a, vector signed long long __b) {
  return vec_mergel(__a, __b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_mergeo(vector unsigned long long __a, vector unsigned long long __b) {
  return vec_mergel(__a, __b);
}

static __inline__ vector float __ATTRS_o_ai
vec_mergeo(vector float __a, vector float __b) {
  return vec_perm(__a, __b,
                  (vector unsigned char)(0x04, 0x05, 0x06, 0x07, 0x14, 0x15,
                                         0x16, 0x17, 0x0C, 0x0D, 0x0E, 0x0F,
                                         0x1C, 0x1D, 0x1E, 0x1F));
}

static __inline__ vector double __ATTRS_o_ai
vec_mergeo(vector double __a, vector double __b) {
  return vec_mergel(__a, __b);
}

#endif

/* vec_mfvscr */

static __inline__ vector unsigned short __attribute__((__always_inline__))
vec_mfvscr(void) {
  return __builtin_altivec_mfvscr();
}

/* vec_min */

static __inline__ vector signed char __ATTRS_o_ai
vec_min(vector signed char __a, vector signed char __b) {
  return __builtin_altivec_vminsb(__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_min(vector bool char __a, vector signed char __b) {
  return __builtin_altivec_vminsb((vector signed char)__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_min(vector signed char __a, vector bool char __b) {
  return __builtin_altivec_vminsb(__a, (vector signed char)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_min(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_altivec_vminub(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_min(vector bool char __a, vector unsigned char __b) {
  return __builtin_altivec_vminub((vector unsigned char)__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_min(vector unsigned char __a, vector bool char __b) {
  return __builtin_altivec_vminub(__a, (vector unsigned char)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_min(vector short __a,
                                                    vector short __b) {
  return __builtin_altivec_vminsh(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_min(vector bool short __a,
                                                    vector short __b) {
  return __builtin_altivec_vminsh((vector short)__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_min(vector short __a,
                                                    vector bool short __b) {
  return __builtin_altivec_vminsh(__a, (vector short)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_min(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_altivec_vminuh(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_min(vector bool short __a, vector unsigned short __b) {
  return __builtin_altivec_vminuh((vector unsigned short)__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_min(vector unsigned short __a, vector bool short __b) {
  return __builtin_altivec_vminuh(__a, (vector unsigned short)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_min(vector int __a,
                                                  vector int __b) {
  return __builtin_altivec_vminsw(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_min(vector bool int __a,
                                                  vector int __b) {
  return __builtin_altivec_vminsw((vector int)__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_min(vector int __a,
                                                  vector bool int __b) {
  return __builtin_altivec_vminsw(__a, (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_min(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_altivec_vminuw(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_min(vector bool int __a, vector unsigned int __b) {
  return __builtin_altivec_vminuw((vector unsigned int)__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_min(vector unsigned int __a, vector bool int __b) {
  return __builtin_altivec_vminuw(__a, (vector unsigned int)__b);
}

#ifdef __POWER8_VECTOR__
static __inline__ vector signed long long __ATTRS_o_ai
vec_min(vector signed long long __a, vector signed long long __b) {
  return __builtin_altivec_vminsd(__a, __b);
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_min(vector bool long long __a, vector signed long long __b) {
  return __builtin_altivec_vminsd((vector signed long long)__a, __b);
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_min(vector signed long long __a, vector bool long long __b) {
  return __builtin_altivec_vminsd(__a, (vector signed long long)__b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_min(vector unsigned long long __a, vector unsigned long long __b) {
  return __builtin_altivec_vminud(__a, __b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_min(vector bool long long __a, vector unsigned long long __b) {
  return __builtin_altivec_vminud((vector unsigned long long)__a, __b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_min(vector unsigned long long __a, vector bool long long __b) {
  return __builtin_altivec_vminud(__a, (vector unsigned long long)__b);
}
#endif

static __inline__ vector float __ATTRS_o_ai vec_min(vector float __a,
                                                    vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvminsp(__a, __b);
#else
  return __builtin_altivec_vminfp(__a, __b);
#endif
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_min(vector double __a,
                                                     vector double __b) {
  return __builtin_vsx_xvmindp(__a, __b);
}
#endif

/* vec_vminsb */

static __inline__ vector signed char __ATTRS_o_ai
vec_vminsb(vector signed char __a, vector signed char __b) {
  return __builtin_altivec_vminsb(__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vminsb(vector bool char __a, vector signed char __b) {
  return __builtin_altivec_vminsb((vector signed char)__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vminsb(vector signed char __a, vector bool char __b) {
  return __builtin_altivec_vminsb(__a, (vector signed char)__b);
}

/* vec_vminub */

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vminub(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_altivec_vminub(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vminub(vector bool char __a, vector unsigned char __b) {
  return __builtin_altivec_vminub((vector unsigned char)__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vminub(vector unsigned char __a, vector bool char __b) {
  return __builtin_altivec_vminub(__a, (vector unsigned char)__b);
}

/* vec_vminsh */

static __inline__ vector short __ATTRS_o_ai vec_vminsh(vector short __a,
                                                       vector short __b) {
  return __builtin_altivec_vminsh(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_vminsh(vector bool short __a,
                                                       vector short __b) {
  return __builtin_altivec_vminsh((vector short)__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_vminsh(vector short __a,
                                                       vector bool short __b) {
  return __builtin_altivec_vminsh(__a, (vector short)__b);
}

/* vec_vminuh */

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vminuh(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_altivec_vminuh(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vminuh(vector bool short __a, vector unsigned short __b) {
  return __builtin_altivec_vminuh((vector unsigned short)__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vminuh(vector unsigned short __a, vector bool short __b) {
  return __builtin_altivec_vminuh(__a, (vector unsigned short)__b);
}

/* vec_vminsw */

static __inline__ vector int __ATTRS_o_ai vec_vminsw(vector int __a,
                                                     vector int __b) {
  return __builtin_altivec_vminsw(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_vminsw(vector bool int __a,
                                                     vector int __b) {
  return __builtin_altivec_vminsw((vector int)__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_vminsw(vector int __a,
                                                     vector bool int __b) {
  return __builtin_altivec_vminsw(__a, (vector int)__b);
}

/* vec_vminuw */

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vminuw(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_altivec_vminuw(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vminuw(vector bool int __a, vector unsigned int __b) {
  return __builtin_altivec_vminuw((vector unsigned int)__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vminuw(vector unsigned int __a, vector bool int __b) {
  return __builtin_altivec_vminuw(__a, (vector unsigned int)__b);
}

/* vec_vminfp */

static __inline__ vector float __attribute__((__always_inline__))
vec_vminfp(vector float __a, vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvminsp(__a, __b);
#else
  return __builtin_altivec_vminfp(__a, __b);
#endif
}

/* vec_mladd */

#define __builtin_altivec_vmladduhm vec_mladd

static __inline__ vector short __ATTRS_o_ai vec_mladd(vector short __a,
                                                      vector short __b,
                                                      vector short __c) {
  return __a * __b + __c;
}

static __inline__ vector short __ATTRS_o_ai vec_mladd(
    vector short __a, vector unsigned short __b, vector unsigned short __c) {
  return __a * (vector short)__b + (vector short)__c;
}

static __inline__ vector short __ATTRS_o_ai vec_mladd(vector unsigned short __a,
                                                      vector short __b,
                                                      vector short __c) {
  return (vector short)__a * __b + __c;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_mladd(vector unsigned short __a, vector unsigned short __b,
          vector unsigned short __c) {
  return __a * __b + __c;
}

/* vec_vmladduhm */

static __inline__ vector short __ATTRS_o_ai vec_vmladduhm(vector short __a,
                                                          vector short __b,
                                                          vector short __c) {
  return __a * __b + __c;
}

static __inline__ vector short __ATTRS_o_ai vec_vmladduhm(
    vector short __a, vector unsigned short __b, vector unsigned short __c) {
  return __a * (vector short)__b + (vector short)__c;
}

static __inline__ vector short __ATTRS_o_ai
vec_vmladduhm(vector unsigned short __a, vector short __b, vector short __c) {
  return (vector short)__a * __b + __c;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vmladduhm(vector unsigned short __a, vector unsigned short __b,
              vector unsigned short __c) {
  return __a * __b + __c;
}

/* vec_mradds */

static __inline__ vector short __attribute__((__always_inline__))
vec_mradds(vector short __a, vector short __b, vector short __c) {
  return __builtin_altivec_vmhraddshs(__a, __b, __c);
}

/* vec_vmhraddshs */

static __inline__ vector short __attribute__((__always_inline__))
vec_vmhraddshs(vector short __a, vector short __b, vector short __c) {
  return __builtin_altivec_vmhraddshs(__a, __b, __c);
}

/* vec_msum */

static __inline__ vector int __ATTRS_o_ai vec_msum(vector signed char __a,
                                                   vector unsigned char __b,
                                                   vector int __c) {
  return __builtin_altivec_vmsummbm(__a, __b, __c);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_msum(vector unsigned char __a, vector unsigned char __b,
         vector unsigned int __c) {
  return __builtin_altivec_vmsumubm(__a, __b, __c);
}

static __inline__ vector int __ATTRS_o_ai vec_msum(vector short __a,
                                                   vector short __b,
                                                   vector int __c) {
  return __builtin_altivec_vmsumshm(__a, __b, __c);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_msum(vector unsigned short __a, vector unsigned short __b,
         vector unsigned int __c) {
  return __builtin_altivec_vmsumuhm(__a, __b, __c);
}

/* vec_vmsummbm */

static __inline__ vector int __attribute__((__always_inline__))
vec_vmsummbm(vector signed char __a, vector unsigned char __b, vector int __c) {
  return __builtin_altivec_vmsummbm(__a, __b, __c);
}

/* vec_vmsumubm */

static __inline__ vector unsigned int __attribute__((__always_inline__))
vec_vmsumubm(vector unsigned char __a, vector unsigned char __b,
             vector unsigned int __c) {
  return __builtin_altivec_vmsumubm(__a, __b, __c);
}

/* vec_vmsumshm */

static __inline__ vector int __attribute__((__always_inline__))
vec_vmsumshm(vector short __a, vector short __b, vector int __c) {
  return __builtin_altivec_vmsumshm(__a, __b, __c);
}

/* vec_vmsumuhm */

static __inline__ vector unsigned int __attribute__((__always_inline__))
vec_vmsumuhm(vector unsigned short __a, vector unsigned short __b,
             vector unsigned int __c) {
  return __builtin_altivec_vmsumuhm(__a, __b, __c);
}

/* vec_msums */

static __inline__ vector int __ATTRS_o_ai vec_msums(vector short __a,
                                                    vector short __b,
                                                    vector int __c) {
  return __builtin_altivec_vmsumshs(__a, __b, __c);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_msums(vector unsigned short __a, vector unsigned short __b,
          vector unsigned int __c) {
  return __builtin_altivec_vmsumuhs(__a, __b, __c);
}

/* vec_vmsumshs */

static __inline__ vector int __attribute__((__always_inline__))
vec_vmsumshs(vector short __a, vector short __b, vector int __c) {
  return __builtin_altivec_vmsumshs(__a, __b, __c);
}

/* vec_vmsumuhs */

static __inline__ vector unsigned int __attribute__((__always_inline__))
vec_vmsumuhs(vector unsigned short __a, vector unsigned short __b,
             vector unsigned int __c) {
  return __builtin_altivec_vmsumuhs(__a, __b, __c);
}

/* vec_mtvscr */

static __inline__ void __ATTRS_o_ai vec_mtvscr(vector signed char __a) {
  __builtin_altivec_mtvscr((vector int)__a);
}

static __inline__ void __ATTRS_o_ai vec_mtvscr(vector unsigned char __a) {
  __builtin_altivec_mtvscr((vector int)__a);
}

static __inline__ void __ATTRS_o_ai vec_mtvscr(vector bool char __a) {
  __builtin_altivec_mtvscr((vector int)__a);
}

static __inline__ void __ATTRS_o_ai vec_mtvscr(vector short __a) {
  __builtin_altivec_mtvscr((vector int)__a);
}

static __inline__ void __ATTRS_o_ai vec_mtvscr(vector unsigned short __a) {
  __builtin_altivec_mtvscr((vector int)__a);
}

static __inline__ void __ATTRS_o_ai vec_mtvscr(vector bool short __a) {
  __builtin_altivec_mtvscr((vector int)__a);
}

static __inline__ void __ATTRS_o_ai vec_mtvscr(vector pixel __a) {
  __builtin_altivec_mtvscr((vector int)__a);
}

static __inline__ void __ATTRS_o_ai vec_mtvscr(vector int __a) {
  __builtin_altivec_mtvscr((vector int)__a);
}

static __inline__ void __ATTRS_o_ai vec_mtvscr(vector unsigned int __a) {
  __builtin_altivec_mtvscr((vector int)__a);
}

static __inline__ void __ATTRS_o_ai vec_mtvscr(vector bool int __a) {
  __builtin_altivec_mtvscr((vector int)__a);
}

static __inline__ void __ATTRS_o_ai vec_mtvscr(vector float __a) {
  __builtin_altivec_mtvscr((vector int)__a);
}

/* vec_mul */

/* Integer vector multiplication will involve multiplication of the odd/even
   elements separately, then truncating the results and moving to the
   result vector.
*/
static __inline__ vector signed char __ATTRS_o_ai
vec_mul(vector signed char __a, vector signed char __b) {
  return __a * __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_mul(vector unsigned char __a, vector unsigned char __b) {
  return __a * __b;
}

static __inline__ vector signed short __ATTRS_o_ai
vec_mul(vector signed short __a, vector signed short __b) {
  return __a * __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_mul(vector unsigned short __a, vector unsigned short __b) {
  return __a * __b;
}

static __inline__ vector signed int __ATTRS_o_ai
vec_mul(vector signed int __a, vector signed int __b) {
  return __a * __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_mul(vector unsigned int __a, vector unsigned int __b) {
  return __a * __b;
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_mul(vector signed long long __a, vector signed long long __b) {
  return __a * __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_mul(vector unsigned long long __a, vector unsigned long long __b) {
  return __a * __b;
}
#endif

static __inline__ vector float __ATTRS_o_ai vec_mul(vector float __a,
                                                    vector float __b) {
  return __a * __b;
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_mul(vector double __a,
                                                     vector double __b) {
  return __a * __b;
}
#endif

/* The vmulos* and vmules* instructions have a big endian bias, so
   we must reverse the meaning of "even" and "odd" for little endian.  */

/* vec_mule */

static __inline__ vector short __ATTRS_o_ai vec_mule(vector signed char __a,
                                                     vector signed char __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmulosb(__a, __b);
#else
  return __builtin_altivec_vmulesb(__a, __b);
#endif
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_mule(vector unsigned char __a, vector unsigned char __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmuloub(__a, __b);
#else
  return __builtin_altivec_vmuleub(__a, __b);
#endif
}

static __inline__ vector int __ATTRS_o_ai vec_mule(vector short __a,
                                                   vector short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmulosh(__a, __b);
#else
  return __builtin_altivec_vmulesh(__a, __b);
#endif
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_mule(vector unsigned short __a, vector unsigned short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmulouh(__a, __b);
#else
  return __builtin_altivec_vmuleuh(__a, __b);
#endif
}

#ifdef __POWER8_VECTOR__
static __inline__ vector signed long long __ATTRS_o_ai
vec_mule(vector signed int __a, vector signed int __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmulosw(__a, __b);
#else
  return __builtin_altivec_vmulesw(__a, __b);
#endif
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_mule(vector unsigned int __a, vector unsigned int __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmulouw(__a, __b);
#else
  return __builtin_altivec_vmuleuw(__a, __b);
#endif
}
#endif

/* vec_vmulesb */

static __inline__ vector short __attribute__((__always_inline__))
vec_vmulesb(vector signed char __a, vector signed char __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmulosb(__a, __b);
#else
  return __builtin_altivec_vmulesb(__a, __b);
#endif
}

/* vec_vmuleub */

static __inline__ vector unsigned short __attribute__((__always_inline__))
vec_vmuleub(vector unsigned char __a, vector unsigned char __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmuloub(__a, __b);
#else
  return __builtin_altivec_vmuleub(__a, __b);
#endif
}

/* vec_vmulesh */

static __inline__ vector int __attribute__((__always_inline__))
vec_vmulesh(vector short __a, vector short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmulosh(__a, __b);
#else
  return __builtin_altivec_vmulesh(__a, __b);
#endif
}

/* vec_vmuleuh */

static __inline__ vector unsigned int __attribute__((__always_inline__))
vec_vmuleuh(vector unsigned short __a, vector unsigned short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmulouh(__a, __b);
#else
  return __builtin_altivec_vmuleuh(__a, __b);
#endif
}

/* vec_mulo */

static __inline__ vector short __ATTRS_o_ai vec_mulo(vector signed char __a,
                                                     vector signed char __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmulesb(__a, __b);
#else
  return __builtin_altivec_vmulosb(__a, __b);
#endif
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_mulo(vector unsigned char __a, vector unsigned char __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmuleub(__a, __b);
#else
  return __builtin_altivec_vmuloub(__a, __b);
#endif
}

static __inline__ vector int __ATTRS_o_ai vec_mulo(vector short __a,
                                                   vector short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmulesh(__a, __b);
#else
  return __builtin_altivec_vmulosh(__a, __b);
#endif
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_mulo(vector unsigned short __a, vector unsigned short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmuleuh(__a, __b);
#else
  return __builtin_altivec_vmulouh(__a, __b);
#endif
}

#ifdef __POWER8_VECTOR__
static __inline__ vector signed long long __ATTRS_o_ai
vec_mulo(vector signed int __a, vector signed int __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmulesw(__a, __b);
#else
  return __builtin_altivec_vmulosw(__a, __b);
#endif
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_mulo(vector unsigned int __a, vector unsigned int __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmuleuw(__a, __b);
#else
  return __builtin_altivec_vmulouw(__a, __b);
#endif
}
#endif

/* vec_vmulosb */

static __inline__ vector short __attribute__((__always_inline__))
vec_vmulosb(vector signed char __a, vector signed char __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmulesb(__a, __b);
#else
  return __builtin_altivec_vmulosb(__a, __b);
#endif
}

/* vec_vmuloub */

static __inline__ vector unsigned short __attribute__((__always_inline__))
vec_vmuloub(vector unsigned char __a, vector unsigned char __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmuleub(__a, __b);
#else
  return __builtin_altivec_vmuloub(__a, __b);
#endif
}

/* vec_vmulosh */

static __inline__ vector int __attribute__((__always_inline__))
vec_vmulosh(vector short __a, vector short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmulesh(__a, __b);
#else
  return __builtin_altivec_vmulosh(__a, __b);
#endif
}

/* vec_vmulouh */

static __inline__ vector unsigned int __attribute__((__always_inline__))
vec_vmulouh(vector unsigned short __a, vector unsigned short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vmuleuh(__a, __b);
#else
  return __builtin_altivec_vmulouh(__a, __b);
#endif
}

/*  vec_nand */

#ifdef __POWER8_VECTOR__
static __inline__ vector signed char __ATTRS_o_ai
vec_nand(vector signed char __a, vector signed char __b) {
  return ~(__a & __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_nand(vector signed char __a, vector bool char __b) {
  return ~(__a & __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_nand(vector bool char __a, vector signed char __b) {
  return ~(__a & __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_nand(vector unsigned char __a, vector unsigned char __b) {
  return ~(__a & __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_nand(vector unsigned char __a, vector bool char __b) {
  return ~(__a & __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_nand(vector bool char __a, vector unsigned char __b) {
  return ~(__a & __b);
}

static __inline__ vector bool char __ATTRS_o_ai vec_nand(vector bool char __a,
                                                         vector bool char __b) {
  return ~(__a & __b);
}

static __inline__ vector signed short __ATTRS_o_ai
vec_nand(vector signed short __a, vector signed short __b) {
  return ~(__a & __b);
}

static __inline__ vector signed short __ATTRS_o_ai
vec_nand(vector signed short __a, vector bool short __b) {
  return ~(__a & __b);
}

static __inline__ vector signed short __ATTRS_o_ai
vec_nand(vector bool short __a, vector signed short __b) {
  return ~(__a & __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_nand(vector unsigned short __a, vector unsigned short __b) {
  return ~(__a & __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_nand(vector unsigned short __a, vector bool short __b) {
  return ~(__a & __b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_nand(vector bool short __a, vector bool short __b) {
  return ~(__a & __b);
}

static __inline__ vector signed int __ATTRS_o_ai
vec_nand(vector signed int __a, vector signed int __b) {
  return ~(__a & __b);
}

static __inline__ vector signed int __ATTRS_o_ai vec_nand(vector signed int __a,
                                                          vector bool int __b) {
  return ~(__a & __b);
}

static __inline__ vector signed int __ATTRS_o_ai
vec_nand(vector bool int __a, vector signed int __b) {
  return ~(__a & __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_nand(vector unsigned int __a, vector unsigned int __b) {
  return ~(__a & __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_nand(vector unsigned int __a, vector bool int __b) {
  return ~(__a & __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_nand(vector bool int __a, vector unsigned int __b) {
  return ~(__a & __b);
}

static __inline__ vector bool int __ATTRS_o_ai vec_nand(vector bool int __a,
                                                        vector bool int __b) {
  return ~(__a & __b);
}

static __inline__ vector float __ATTRS_o_ai
vec_nand(vector float __a, vector float __b) {
  return (vector float)(~((vector unsigned int)__a &
                          (vector unsigned int)__b));
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_nand(vector signed long long __a, vector signed long long __b) {
  return ~(__a & __b);
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_nand(vector signed long long __a, vector bool long long __b) {
  return ~(__a & __b);
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_nand(vector bool long long __a, vector signed long long __b) {
  return ~(__a & __b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_nand(vector unsigned long long __a, vector unsigned long long __b) {
  return ~(__a & __b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_nand(vector unsigned long long __a, vector bool long long __b) {
  return ~(__a & __b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_nand(vector bool long long __a, vector unsigned long long __b) {
  return ~(__a & __b);
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_nand(vector bool long long __a, vector bool long long __b) {
  return ~(__a & __b);
}

static __inline__ vector double __ATTRS_o_ai
vec_nand(vector double __a, vector double __b) {
  return (vector double)(~((vector unsigned long long)__a &
                           (vector unsigned long long)__b));
}

#endif

/* vec_nmadd */

#ifdef __VSX__
static __inline__ vector float __ATTRS_o_ai vec_nmadd(vector float __a,
                                                      vector float __b,
                                                      vector float __c) {
  return __builtin_vsx_xvnmaddasp(__a, __b, __c);
}

static __inline__ vector double __ATTRS_o_ai vec_nmadd(vector double __a,
                                                       vector double __b,
                                                       vector double __c) {
  return __builtin_vsx_xvnmaddadp(__a, __b, __c);
}
#endif

/* vec_nmsub */

static __inline__ vector float __ATTRS_o_ai vec_nmsub(vector float __a,
                                                      vector float __b,
                                                      vector float __c) {
#ifdef __VSX__
  return __builtin_vsx_xvnmsubasp(__a, __b, __c);
#else
  return __builtin_altivec_vnmsubfp(__a, __b, __c);
#endif
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_nmsub(vector double __a,
                                                       vector double __b,
                                                       vector double __c) {
  return __builtin_vsx_xvnmsubadp(__a, __b, __c);
}
#endif

/* vec_vnmsubfp */

static __inline__ vector float __attribute__((__always_inline__))
vec_vnmsubfp(vector float __a, vector float __b, vector float __c) {
  return __builtin_altivec_vnmsubfp(__a, __b, __c);
}

/* vec_nor */

#define __builtin_altivec_vnor vec_nor

static __inline__ vector signed char __ATTRS_o_ai
vec_nor(vector signed char __a, vector signed char __b) {
  return ~(__a | __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_nor(vector unsigned char __a, vector unsigned char __b) {
  return ~(__a | __b);
}

static __inline__ vector bool char __ATTRS_o_ai vec_nor(vector bool char __a,
                                                        vector bool char __b) {
  return ~(__a | __b);
}

static __inline__ vector short __ATTRS_o_ai vec_nor(vector short __a,
                                                    vector short __b) {
  return ~(__a | __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_nor(vector unsigned short __a, vector unsigned short __b) {
  return ~(__a | __b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_nor(vector bool short __a, vector bool short __b) {
  return ~(__a | __b);
}

static __inline__ vector int __ATTRS_o_ai vec_nor(vector int __a,
                                                  vector int __b) {
  return ~(__a | __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_nor(vector unsigned int __a, vector unsigned int __b) {
  return ~(__a | __b);
}

static __inline__ vector bool int __ATTRS_o_ai vec_nor(vector bool int __a,
                                                       vector bool int __b) {
  return ~(__a | __b);
}

static __inline__ vector float __ATTRS_o_ai vec_nor(vector float __a,
                                                    vector float __b) {
  vector unsigned int __res =
      ~((vector unsigned int)__a | (vector unsigned int)__b);
  return (vector float)__res;
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_nor(vector double __a,
                                                     vector double __b) {
  vector unsigned long long __res =
      ~((vector unsigned long long)__a | (vector unsigned long long)__b);
  return (vector double)__res;
}
#endif

/* vec_vnor */

static __inline__ vector signed char __ATTRS_o_ai
vec_vnor(vector signed char __a, vector signed char __b) {
  return ~(__a | __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vnor(vector unsigned char __a, vector unsigned char __b) {
  return ~(__a | __b);
}

static __inline__ vector bool char __ATTRS_o_ai vec_vnor(vector bool char __a,
                                                         vector bool char __b) {
  return ~(__a | __b);
}

static __inline__ vector short __ATTRS_o_ai vec_vnor(vector short __a,
                                                     vector short __b) {
  return ~(__a | __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vnor(vector unsigned short __a, vector unsigned short __b) {
  return ~(__a | __b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vnor(vector bool short __a, vector bool short __b) {
  return ~(__a | __b);
}

static __inline__ vector int __ATTRS_o_ai vec_vnor(vector int __a,
                                                   vector int __b) {
  return ~(__a | __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vnor(vector unsigned int __a, vector unsigned int __b) {
  return ~(__a | __b);
}

static __inline__ vector bool int __ATTRS_o_ai vec_vnor(vector bool int __a,
                                                        vector bool int __b) {
  return ~(__a | __b);
}

static __inline__ vector float __ATTRS_o_ai vec_vnor(vector float __a,
                                                     vector float __b) {
  vector unsigned int __res =
      ~((vector unsigned int)__a | (vector unsigned int)__b);
  return (vector float)__res;
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_nor(vector signed long long __a, vector signed long long __b) {
  return ~(__a | __b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_nor(vector unsigned long long __a, vector unsigned long long __b) {
  return ~(__a | __b);
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_nor(vector bool long long __a, vector bool long long __b) {
  return ~(__a | __b);
}
#endif

/* vec_or */

#define __builtin_altivec_vor vec_or

static __inline__ vector signed char __ATTRS_o_ai
vec_or(vector signed char __a, vector signed char __b) {
  return __a | __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_or(vector bool char __a, vector signed char __b) {
  return (vector signed char)__a | __b;
}

static __inline__ vector signed char __ATTRS_o_ai vec_or(vector signed char __a,
                                                         vector bool char __b) {
  return __a | (vector signed char)__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_or(vector unsigned char __a, vector unsigned char __b) {
  return __a | __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_or(vector bool char __a, vector unsigned char __b) {
  return (vector unsigned char)__a | __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_or(vector unsigned char __a, vector bool char __b) {
  return __a | (vector unsigned char)__b;
}

static __inline__ vector bool char __ATTRS_o_ai vec_or(vector bool char __a,
                                                       vector bool char __b) {
  return __a | __b;
}

static __inline__ vector short __ATTRS_o_ai vec_or(vector short __a,
                                                   vector short __b) {
  return __a | __b;
}

static __inline__ vector short __ATTRS_o_ai vec_or(vector bool short __a,
                                                   vector short __b) {
  return (vector short)__a | __b;
}

static __inline__ vector short __ATTRS_o_ai vec_or(vector short __a,
                                                   vector bool short __b) {
  return __a | (vector short)__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_or(vector unsigned short __a, vector unsigned short __b) {
  return __a | __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_or(vector bool short __a, vector unsigned short __b) {
  return (vector unsigned short)__a | __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_or(vector unsigned short __a, vector bool short __b) {
  return __a | (vector unsigned short)__b;
}

static __inline__ vector bool short __ATTRS_o_ai vec_or(vector bool short __a,
                                                        vector bool short __b) {
  return __a | __b;
}

static __inline__ vector int __ATTRS_o_ai vec_or(vector int __a,
                                                 vector int __b) {
  return __a | __b;
}

static __inline__ vector int __ATTRS_o_ai vec_or(vector bool int __a,
                                                 vector int __b) {
  return (vector int)__a | __b;
}

static __inline__ vector int __ATTRS_o_ai vec_or(vector int __a,
                                                 vector bool int __b) {
  return __a | (vector int)__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_or(vector unsigned int __a, vector unsigned int __b) {
  return __a | __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_or(vector bool int __a, vector unsigned int __b) {
  return (vector unsigned int)__a | __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_or(vector unsigned int __a, vector bool int __b) {
  return __a | (vector unsigned int)__b;
}

static __inline__ vector bool int __ATTRS_o_ai vec_or(vector bool int __a,
                                                      vector bool int __b) {
  return __a | __b;
}

static __inline__ vector float __ATTRS_o_ai vec_or(vector float __a,
                                                   vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a | (vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_or(vector bool int __a,
                                                   vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a | (vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_or(vector float __a,
                                                   vector bool int __b) {
  vector unsigned int __res =
      (vector unsigned int)__a | (vector unsigned int)__b;
  return (vector float)__res;
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_or(vector bool long long __a,
                                                    vector double __b) {
  return (vector unsigned long long)__a | (vector unsigned long long)__b;
}

static __inline__ vector double __ATTRS_o_ai vec_or(vector double __a,
                                                    vector bool long long __b) {
  return (vector unsigned long long)__a | (vector unsigned long long)__b;
}

static __inline__ vector double __ATTRS_o_ai vec_or(vector double __a,
                                                    vector double __b) {
  vector unsigned long long __res =
      (vector unsigned long long)__a | (vector unsigned long long)__b;
  return (vector double)__res;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_or(vector signed long long __a, vector signed long long __b) {
  return __a | __b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_or(vector bool long long __a, vector signed long long __b) {
  return (vector signed long long)__a | __b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_or(vector signed long long __a, vector bool long long __b) {
  return __a | (vector signed long long)__b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_or(vector unsigned long long __a, vector unsigned long long __b) {
  return __a | __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_or(vector bool long long __a, vector unsigned long long __b) {
  return (vector unsigned long long)__a | __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_or(vector unsigned long long __a, vector bool long long __b) {
  return __a | (vector unsigned long long)__b;
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_or(vector bool long long __a, vector bool long long __b) {
  return __a | __b;
}
#endif

#ifdef __POWER8_VECTOR__
static __inline__ vector signed char __ATTRS_o_ai
vec_orc(vector signed char __a, vector signed char __b) {
  return __a | ~__b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_orc(vector signed char __a, vector bool char __b) {
  return __a | ~__b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_orc(vector bool char __a, vector signed char __b) {
  return __a | ~__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_orc(vector unsigned char __a, vector unsigned char __b) {
  return __a | ~__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_orc(vector unsigned char __a, vector bool char __b) {
  return __a | ~__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_orc(vector bool char __a, vector unsigned char __b) {
  return __a | ~__b;
}

static __inline__ vector bool char __ATTRS_o_ai vec_orc(vector bool char __a,
                                                        vector bool char __b) {
  return __a | ~__b;
}

static __inline__ vector signed short __ATTRS_o_ai
vec_orc(vector signed short __a, vector signed short __b) {
  return __a | ~__b;
}

static __inline__ vector signed short __ATTRS_o_ai
vec_orc(vector signed short __a, vector bool short __b) {
  return __a | ~__b;
}

static __inline__ vector signed short __ATTRS_o_ai
vec_orc(vector bool short __a, vector signed short __b) {
  return __a | ~__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_orc(vector unsigned short __a, vector unsigned short __b) {
  return __a | ~__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_orc(vector unsigned short __a, vector bool short __b) {
  return __a | ~__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_orc(vector bool short __a, vector unsigned short __b) {
  return __a | ~__b;
}

static __inline__ vector bool short __ATTRS_o_ai
vec_orc(vector bool short __a, vector bool short __b) {
  return __a | ~__b;
}

static __inline__ vector signed int __ATTRS_o_ai
vec_orc(vector signed int __a, vector signed int __b) {
  return __a | ~__b;
}

static __inline__ vector signed int __ATTRS_o_ai vec_orc(vector signed int __a,
                                                         vector bool int __b) {
  return __a | ~__b;
}

static __inline__ vector signed int __ATTRS_o_ai
vec_orc(vector bool int __a, vector signed int __b) {
  return __a | ~__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_orc(vector unsigned int __a, vector unsigned int __b) {
  return __a | ~__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_orc(vector unsigned int __a, vector bool int __b) {
  return __a | ~__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_orc(vector bool int __a, vector unsigned int __b) {
  return __a | ~__b;
}

static __inline__ vector bool int __ATTRS_o_ai vec_orc(vector bool int __a,
                                                       vector bool int __b) {
  return __a | ~__b;
}

static __inline__ vector float __ATTRS_o_ai
vec_orc(vector bool int __a, vector float __b) {
 return (vector float)(__a | ~(vector unsigned int)__b);
}

static __inline__ vector float __ATTRS_o_ai
vec_orc(vector float __a, vector bool int __b) {
  return (vector float)((vector unsigned int)__a | ~__b);
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_orc(vector signed long long __a, vector signed long long __b) {
  return __a | ~__b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_orc(vector signed long long __a, vector bool long long __b) {
  return __a | ~__b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_orc(vector bool long long __a, vector signed long long __b) {
  return __a | ~__b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_orc(vector unsigned long long __a, vector unsigned long long __b) {
  return __a | ~__b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_orc(vector unsigned long long __a, vector bool long long __b) {
  return __a | ~__b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_orc(vector bool long long __a, vector unsigned long long __b) {
  return __a | ~__b;
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_orc(vector bool long long __a, vector bool long long __b) {
  return __a | ~__b;
}

static __inline__ vector double __ATTRS_o_ai
vec_orc(vector double __a, vector bool long long __b) {
  return (vector double)((vector unsigned long long)__a | ~__b);
}

static __inline__ vector double __ATTRS_o_ai
vec_orc(vector bool long long __a, vector double __b) {
  return (vector double)(__a | ~(vector unsigned long long)__b);
}
#endif

/* vec_vor */

static __inline__ vector signed char __ATTRS_o_ai
vec_vor(vector signed char __a, vector signed char __b) {
  return __a | __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vor(vector bool char __a, vector signed char __b) {
  return (vector signed char)__a | __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vor(vector signed char __a, vector bool char __b) {
  return __a | (vector signed char)__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vor(vector unsigned char __a, vector unsigned char __b) {
  return __a | __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vor(vector bool char __a, vector unsigned char __b) {
  return (vector unsigned char)__a | __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vor(vector unsigned char __a, vector bool char __b) {
  return __a | (vector unsigned char)__b;
}

static __inline__ vector bool char __ATTRS_o_ai vec_vor(vector bool char __a,
                                                        vector bool char __b) {
  return __a | __b;
}

static __inline__ vector short __ATTRS_o_ai vec_vor(vector short __a,
                                                    vector short __b) {
  return __a | __b;
}

static __inline__ vector short __ATTRS_o_ai vec_vor(vector bool short __a,
                                                    vector short __b) {
  return (vector short)__a | __b;
}

static __inline__ vector short __ATTRS_o_ai vec_vor(vector short __a,
                                                    vector bool short __b) {
  return __a | (vector short)__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vor(vector unsigned short __a, vector unsigned short __b) {
  return __a | __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vor(vector bool short __a, vector unsigned short __b) {
  return (vector unsigned short)__a | __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vor(vector unsigned short __a, vector bool short __b) {
  return __a | (vector unsigned short)__b;
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vor(vector bool short __a, vector bool short __b) {
  return __a | __b;
}

static __inline__ vector int __ATTRS_o_ai vec_vor(vector int __a,
                                                  vector int __b) {
  return __a | __b;
}

static __inline__ vector int __ATTRS_o_ai vec_vor(vector bool int __a,
                                                  vector int __b) {
  return (vector int)__a | __b;
}

static __inline__ vector int __ATTRS_o_ai vec_vor(vector int __a,
                                                  vector bool int __b) {
  return __a | (vector int)__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vor(vector unsigned int __a, vector unsigned int __b) {
  return __a | __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vor(vector bool int __a, vector unsigned int __b) {
  return (vector unsigned int)__a | __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vor(vector unsigned int __a, vector bool int __b) {
  return __a | (vector unsigned int)__b;
}

static __inline__ vector bool int __ATTRS_o_ai vec_vor(vector bool int __a,
                                                       vector bool int __b) {
  return __a | __b;
}

static __inline__ vector float __ATTRS_o_ai vec_vor(vector float __a,
                                                    vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a | (vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_vor(vector bool int __a,
                                                    vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a | (vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_vor(vector float __a,
                                                    vector bool int __b) {
  vector unsigned int __res =
      (vector unsigned int)__a | (vector unsigned int)__b;
  return (vector float)__res;
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_vor(vector signed long long __a, vector signed long long __b) {
  return __a | __b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_vor(vector bool long long __a, vector signed long long __b) {
  return (vector signed long long)__a | __b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_vor(vector signed long long __a, vector bool long long __b) {
  return __a | (vector signed long long)__b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_vor(vector unsigned long long __a, vector unsigned long long __b) {
  return __a | __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_vor(vector bool long long __a, vector unsigned long long __b) {
  return (vector unsigned long long)__a | __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_vor(vector unsigned long long __a, vector bool long long __b) {
  return __a | (vector unsigned long long)__b;
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_vor(vector bool long long __a, vector bool long long __b) {
  return __a | __b;
}
#endif

/* vec_pack */

/* The various vector pack instructions have a big-endian bias, so for
   little endian we must handle reversed element numbering.  */

static __inline__ vector signed char __ATTRS_o_ai
vec_pack(vector signed short __a, vector signed short __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector signed char)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
                             0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E));
#else
  return (vector signed char)vec_perm(
      __a, __b,
      (vector unsigned char)(0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F,
                             0x11, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x1D, 0x1F));
#endif
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_pack(vector unsigned short __a, vector unsigned short __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector unsigned char)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
                             0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E));
#else
  return (vector unsigned char)vec_perm(
      __a, __b,
      (vector unsigned char)(0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F,
                             0x11, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x1D, 0x1F));
#endif
}

static __inline__ vector bool char __ATTRS_o_ai
vec_pack(vector bool short __a, vector bool short __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool char)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
                             0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E));
#else
  return (vector bool char)vec_perm(
      __a, __b,
      (vector unsigned char)(0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F,
                             0x11, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x1D, 0x1F));
#endif
}

static __inline__ vector short __ATTRS_o_ai vec_pack(vector int __a,
                                                     vector int __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector short)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0C, 0x0D,
                             0x10, 0x11, 0x14, 0x15, 0x18, 0x19, 0x1C, 0x1D));
#else
  return (vector short)vec_perm(
      __a, __b,
      (vector unsigned char)(0x02, 0x03, 0x06, 0x07, 0x0A, 0x0B, 0x0E, 0x0F,
                             0x12, 0x13, 0x16, 0x17, 0x1A, 0x1B, 0x1E, 0x1F));
#endif
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_pack(vector unsigned int __a, vector unsigned int __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector unsigned short)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0C, 0x0D,
                             0x10, 0x11, 0x14, 0x15, 0x18, 0x19, 0x1C, 0x1D));
#else
  return (vector unsigned short)vec_perm(
      __a, __b,
      (vector unsigned char)(0x02, 0x03, 0x06, 0x07, 0x0A, 0x0B, 0x0E, 0x0F,
                             0x12, 0x13, 0x16, 0x17, 0x1A, 0x1B, 0x1E, 0x1F));
#endif
}

static __inline__ vector bool short __ATTRS_o_ai vec_pack(vector bool int __a,
                                                          vector bool int __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool short)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0C, 0x0D,
                             0x10, 0x11, 0x14, 0x15, 0x18, 0x19, 0x1C, 0x1D));
#else
  return (vector bool short)vec_perm(
      __a, __b,
      (vector unsigned char)(0x02, 0x03, 0x06, 0x07, 0x0A, 0x0B, 0x0E, 0x0F,
                             0x12, 0x13, 0x16, 0x17, 0x1A, 0x1B, 0x1E, 0x1F));
#endif
}

#ifdef __VSX__
static __inline__ vector signed int __ATTRS_o_ai
vec_pack(vector signed long long __a, vector signed long long __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector signed int)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A, 0x0B,
                             0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x1A, 0x1B));
#else
  return (vector signed int)vec_perm(
      __a, __b,
      (vector unsigned char)(0x04, 0x05, 0x06, 0x07, 0x0C, 0x0D, 0x0E, 0x0F,
                             0x14, 0x15, 0x16, 0x17, 0x1C, 0x1D, 0x1E, 0x1F));
#endif
}
static __inline__ vector unsigned int __ATTRS_o_ai
vec_pack(vector unsigned long long __a, vector unsigned long long __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector unsigned int)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A, 0x0B,
                             0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x1A, 0x1B));
#else
  return (vector unsigned int)vec_perm(
      __a, __b,
      (vector unsigned char)(0x04, 0x05, 0x06, 0x07, 0x0C, 0x0D, 0x0E, 0x0F,
                             0x14, 0x15, 0x16, 0x17, 0x1C, 0x1D, 0x1E, 0x1F));
#endif
}

static __inline__ vector bool int __ATTRS_o_ai
vec_pack(vector bool long long __a, vector bool long long __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool int)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A, 0x0B,
                             0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x1A, 0x1B));
#else
  return (vector bool int)vec_perm(
      __a, __b,
      (vector unsigned char)(0x04, 0x05, 0x06, 0x07, 0x0C, 0x0D, 0x0E, 0x0F,
                             0x14, 0x15, 0x16, 0x17, 0x1C, 0x1D, 0x1E, 0x1F));
#endif
}

static __inline__ vector float __ATTRS_o_ai
vec_pack(vector double __a, vector double __b) {
  return (vector float) (__a[0], __a[1], __b[0], __b[1]);
}
#endif

#ifdef __POWER9_VECTOR__
static __inline__ vector unsigned short __ATTRS_o_ai
vec_pack_to_short_fp32(vector float __a, vector float __b) {
  vector float __resa = __builtin_vsx_xvcvsphp(__a);
  vector float __resb = __builtin_vsx_xvcvsphp(__b);
#ifdef __LITTLE_ENDIAN__
  return (vector unsigned short)vec_mergee(__resa, __resb);
#else
  return (vector unsigned short)vec_mergeo(__resa, __resb);
#endif
}

#endif
/* vec_vpkuhum */

#define __builtin_altivec_vpkuhum vec_vpkuhum

static __inline__ vector signed char __ATTRS_o_ai
vec_vpkuhum(vector signed short __a, vector signed short __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector signed char)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
                             0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E));
#else
  return (vector signed char)vec_perm(
      __a, __b,
      (vector unsigned char)(0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F,
                             0x11, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x1D, 0x1F));
#endif
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vpkuhum(vector unsigned short __a, vector unsigned short __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector unsigned char)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
                             0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E));
#else
  return (vector unsigned char)vec_perm(
      __a, __b,
      (vector unsigned char)(0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F,
                             0x11, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x1D, 0x1F));
#endif
}

static __inline__ vector bool char __ATTRS_o_ai
vec_vpkuhum(vector bool short __a, vector bool short __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool char)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
                             0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E));
#else
  return (vector bool char)vec_perm(
      __a, __b,
      (vector unsigned char)(0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F,
                             0x11, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x1D, 0x1F));
#endif
}

/* vec_vpkuwum */

#define __builtin_altivec_vpkuwum vec_vpkuwum

static __inline__ vector short __ATTRS_o_ai vec_vpkuwum(vector int __a,
                                                        vector int __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector short)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0C, 0x0D,
                             0x10, 0x11, 0x14, 0x15, 0x18, 0x19, 0x1C, 0x1D));
#else
  return (vector short)vec_perm(
      __a, __b,
      (vector unsigned char)(0x02, 0x03, 0x06, 0x07, 0x0A, 0x0B, 0x0E, 0x0F,
                             0x12, 0x13, 0x16, 0x17, 0x1A, 0x1B, 0x1E, 0x1F));
#endif
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vpkuwum(vector unsigned int __a, vector unsigned int __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector unsigned short)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0C, 0x0D,
                             0x10, 0x11, 0x14, 0x15, 0x18, 0x19, 0x1C, 0x1D));
#else
  return (vector unsigned short)vec_perm(
      __a, __b,
      (vector unsigned char)(0x02, 0x03, 0x06, 0x07, 0x0A, 0x0B, 0x0E, 0x0F,
                             0x12, 0x13, 0x16, 0x17, 0x1A, 0x1B, 0x1E, 0x1F));
#endif
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vpkuwum(vector bool int __a, vector bool int __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool short)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0C, 0x0D,
                             0x10, 0x11, 0x14, 0x15, 0x18, 0x19, 0x1C, 0x1D));
#else
  return (vector bool short)vec_perm(
      __a, __b,
      (vector unsigned char)(0x02, 0x03, 0x06, 0x07, 0x0A, 0x0B, 0x0E, 0x0F,
                             0x12, 0x13, 0x16, 0x17, 0x1A, 0x1B, 0x1E, 0x1F));
#endif
}

/* vec_vpkudum */

#ifdef __POWER8_VECTOR__
#define __builtin_altivec_vpkudum vec_vpkudum

static __inline__ vector int __ATTRS_o_ai vec_vpkudum(vector long long __a,
                                                      vector long long __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector int)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A, 0x0B,
                             0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x1A, 0x1B));
#else
  return (vector int)vec_perm(
      __a, __b,
      (vector unsigned char)(0x04, 0x05, 0x06, 0x07, 0x0C, 0x0D, 0x0E, 0x0F,
                             0x14, 0x15, 0x16, 0x17, 0x1C, 0x1D, 0x1E, 0x1F));
#endif
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vpkudum(vector unsigned long long __a, vector unsigned long long __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector unsigned int)vec_perm(
      __a, __b,
      (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A, 0x0B,
                             0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x1A, 0x1B));
#else
  return (vector unsigned int)vec_perm(
      __a, __b,
      (vector unsigned char)(0x04, 0x05, 0x06, 0x07, 0x0C, 0x0D, 0x0E, 0x0F,
                             0x14, 0x15, 0x16, 0x17, 0x1C, 0x1D, 0x1E, 0x1F));
#endif
}

static __inline__ vector bool int __ATTRS_o_ai
vec_vpkudum(vector bool long long __a, vector bool long long __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool int)vec_perm(
      (vector long long)__a, (vector long long)__b,
      (vector unsigned char)(0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A, 0x0B,
                             0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x1A, 0x1B));
#else
  return (vector bool int)vec_perm(
      (vector long long)__a, (vector long long)__b,
      (vector unsigned char)(0x04, 0x05, 0x06, 0x07, 0x0C, 0x0D, 0x0E, 0x0F,
                             0x14, 0x15, 0x16, 0x17, 0x1C, 0x1D, 0x1E, 0x1F));
#endif
}
#endif

/* vec_packpx */

static __inline__ vector pixel __attribute__((__always_inline__))
vec_packpx(vector unsigned int __a, vector unsigned int __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector pixel)__builtin_altivec_vpkpx(__b, __a);
#else
  return (vector pixel)__builtin_altivec_vpkpx(__a, __b);
#endif
}

/* vec_vpkpx */

static __inline__ vector pixel __attribute__((__always_inline__))
vec_vpkpx(vector unsigned int __a, vector unsigned int __b) {
#ifdef __LITTLE_ENDIAN__
  return (vector pixel)__builtin_altivec_vpkpx(__b, __a);
#else
  return (vector pixel)__builtin_altivec_vpkpx(__a, __b);
#endif
}

/* vec_packs */

static __inline__ vector signed char __ATTRS_o_ai vec_packs(vector short __a,
                                                            vector short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkshss(__b, __a);
#else
  return __builtin_altivec_vpkshss(__a, __b);
#endif
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_packs(vector unsigned short __a, vector unsigned short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkuhus(__b, __a);
#else
  return __builtin_altivec_vpkuhus(__a, __b);
#endif
}

static __inline__ vector signed short __ATTRS_o_ai vec_packs(vector int __a,
                                                             vector int __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkswss(__b, __a);
#else
  return __builtin_altivec_vpkswss(__a, __b);
#endif
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_packs(vector unsigned int __a, vector unsigned int __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkuwus(__b, __a);
#else
  return __builtin_altivec_vpkuwus(__a, __b);
#endif
}

#ifdef __POWER8_VECTOR__
static __inline__ vector int __ATTRS_o_ai vec_packs(vector long long __a,
                                                    vector long long __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpksdss(__b, __a);
#else
  return __builtin_altivec_vpksdss(__a, __b);
#endif
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_packs(vector unsigned long long __a, vector unsigned long long __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkudus(__b, __a);
#else
  return __builtin_altivec_vpkudus(__a, __b);
#endif
}
#endif

/* vec_vpkshss */

static __inline__ vector signed char __attribute__((__always_inline__))
vec_vpkshss(vector short __a, vector short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkshss(__b, __a);
#else
  return __builtin_altivec_vpkshss(__a, __b);
#endif
}

/* vec_vpksdss */

#ifdef __POWER8_VECTOR__
static __inline__ vector int __ATTRS_o_ai vec_vpksdss(vector long long __a,
                                                      vector long long __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpksdss(__b, __a);
#else
  return __builtin_altivec_vpksdss(__a, __b);
#endif
}
#endif

/* vec_vpkuhus */

static __inline__ vector unsigned char __attribute__((__always_inline__))
vec_vpkuhus(vector unsigned short __a, vector unsigned short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkuhus(__b, __a);
#else
  return __builtin_altivec_vpkuhus(__a, __b);
#endif
}

/* vec_vpkudus */

#ifdef __POWER8_VECTOR__
static __inline__ vector unsigned int __attribute__((__always_inline__))
vec_vpkudus(vector unsigned long long __a, vector unsigned long long __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkudus(__b, __a);
#else
  return __builtin_altivec_vpkudus(__a, __b);
#endif
}
#endif

/* vec_vpkswss */

static __inline__ vector signed short __attribute__((__always_inline__))
vec_vpkswss(vector int __a, vector int __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkswss(__b, __a);
#else
  return __builtin_altivec_vpkswss(__a, __b);
#endif
}

/* vec_vpkuwus */

static __inline__ vector unsigned short __attribute__((__always_inline__))
vec_vpkuwus(vector unsigned int __a, vector unsigned int __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkuwus(__b, __a);
#else
  return __builtin_altivec_vpkuwus(__a, __b);
#endif
}

/* vec_packsu */

static __inline__ vector unsigned char __ATTRS_o_ai
vec_packsu(vector short __a, vector short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkshus(__b, __a);
#else
  return __builtin_altivec_vpkshus(__a, __b);
#endif
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_packsu(vector unsigned short __a, vector unsigned short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkuhus(__b, __a);
#else
  return __builtin_altivec_vpkuhus(__a, __b);
#endif
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_packsu(vector int __a, vector int __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkswus(__b, __a);
#else
  return __builtin_altivec_vpkswus(__a, __b);
#endif
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_packsu(vector unsigned int __a, vector unsigned int __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkuwus(__b, __a);
#else
  return __builtin_altivec_vpkuwus(__a, __b);
#endif
}

#ifdef __POWER8_VECTOR__
static __inline__ vector unsigned int __ATTRS_o_ai
vec_packsu(vector long long __a, vector long long __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpksdus(__b, __a);
#else
  return __builtin_altivec_vpksdus(__a, __b);
#endif
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_packsu(vector unsigned long long __a, vector unsigned long long __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkudus(__b, __a);
#else
  return __builtin_altivec_vpkudus(__a, __b);
#endif
}
#endif

/* vec_vpkshus */

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vpkshus(vector short __a, vector short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkshus(__b, __a);
#else
  return __builtin_altivec_vpkshus(__a, __b);
#endif
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vpkshus(vector unsigned short __a, vector unsigned short __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkuhus(__b, __a);
#else
  return __builtin_altivec_vpkuhus(__a, __b);
#endif
}

/* vec_vpkswus */

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vpkswus(vector int __a, vector int __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkswus(__b, __a);
#else
  return __builtin_altivec_vpkswus(__a, __b);
#endif
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vpkswus(vector unsigned int __a, vector unsigned int __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpkuwus(__b, __a);
#else
  return __builtin_altivec_vpkuwus(__a, __b);
#endif
}

/* vec_vpksdus */

#ifdef __POWER8_VECTOR__
static __inline__ vector unsigned int __ATTRS_o_ai
vec_vpksdus(vector long long __a, vector long long __b) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vpksdus(__b, __a);
#else
  return __builtin_altivec_vpksdus(__a, __b);
#endif
}
#endif

/* vec_perm */

// The vperm instruction is defined architecturally with a big-endian bias.
// For little endian, we swap the input operands and invert the permute
// control vector.  Only the rightmost 5 bits matter, so we could use
// a vector of all 31s instead of all 255s to perform the inversion.
// However, when the PCV is not a constant, using 255 has an advantage
// in that the vec_xor can be recognized as a vec_nor (and for P8 and
// later, possibly a vec_nand).

static __inline__ vector signed char __ATTRS_o_ai vec_perm(
    vector signed char __a, vector signed char __b, vector unsigned char __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __d = {255, 255, 255, 255, 255, 255, 255, 255,
                              255, 255, 255, 255, 255, 255, 255, 255};
  __d = vec_xor(__c, __d);
  return (vector signed char)__builtin_altivec_vperm_4si((vector int)__b,
                                                         (vector int)__a, __d);
#else
  return (vector signed char)__builtin_altivec_vperm_4si((vector int)__a,
                                                         (vector int)__b, __c);
#endif
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_perm(vector unsigned char __a, vector unsigned char __b,
         vector unsigned char __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __d = {255, 255, 255, 255, 255, 255, 255, 255,
                              255, 255, 255, 255, 255, 255, 255, 255};
  __d = vec_xor(__c, __d);
  return (vector unsigned char)__builtin_altivec_vperm_4si(
      (vector int)__b, (vector int)__a, __d);
#else
  return (vector unsigned char)__builtin_altivec_vperm_4si(
      (vector int)__a, (vector int)__b, __c);
#endif
}

static __inline__ vector bool char __ATTRS_o_ai
vec_perm(vector bool char __a, vector bool char __b, vector unsigned char __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __d = {255, 255, 255, 255, 255, 255, 255, 255,
                              255, 255, 255, 255, 255, 255, 255, 255};
  __d = vec_xor(__c, __d);
  return (vector bool char)__builtin_altivec_vperm_4si((vector int)__b,
                                                       (vector int)__a, __d);
#else
  return (vector bool char)__builtin_altivec_vperm_4si((vector int)__a,
                                                       (vector int)__b, __c);
#endif
}

static __inline__ vector short __ATTRS_o_ai vec_perm(vector signed short __a,
                                                     vector signed short __b,
                                                     vector unsigned char __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __d = {255, 255, 255, 255, 255, 255, 255, 255,
                              255, 255, 255, 255, 255, 255, 255, 255};
  __d = vec_xor(__c, __d);
  return (vector signed short)__builtin_altivec_vperm_4si((vector int)__b,
                                                          (vector int)__a, __d);
#else
  return (vector signed short)__builtin_altivec_vperm_4si((vector int)__a,
                                                          (vector int)__b, __c);
#endif
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_perm(vector unsigned short __a, vector unsigned short __b,
         vector unsigned char __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __d = {255, 255, 255, 255, 255, 255, 255, 255,
                              255, 255, 255, 255, 255, 255, 255, 255};
  __d = vec_xor(__c, __d);
  return (vector unsigned short)__builtin_altivec_vperm_4si(
      (vector int)__b, (vector int)__a, __d);
#else
  return (vector unsigned short)__builtin_altivec_vperm_4si(
      (vector int)__a, (vector int)__b, __c);
#endif
}

static __inline__ vector bool short __ATTRS_o_ai vec_perm(
    vector bool short __a, vector bool short __b, vector unsigned char __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __d = {255, 255, 255, 255, 255, 255, 255, 255,
                              255, 255, 255, 255, 255, 255, 255, 255};
  __d = vec_xor(__c, __d);
  return (vector bool short)__builtin_altivec_vperm_4si((vector int)__b,
                                                        (vector int)__a, __d);
#else
  return (vector bool short)__builtin_altivec_vperm_4si((vector int)__a,
                                                        (vector int)__b, __c);
#endif
}

static __inline__ vector pixel __ATTRS_o_ai vec_perm(vector pixel __a,
                                                     vector pixel __b,
                                                     vector unsigned char __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __d = {255, 255, 255, 255, 255, 255, 255, 255,
                              255, 255, 255, 255, 255, 255, 255, 255};
  __d = vec_xor(__c, __d);
  return (vector pixel)__builtin_altivec_vperm_4si((vector int)__b,
                                                   (vector int)__a, __d);
#else
  return (vector pixel)__builtin_altivec_vperm_4si((vector int)__a,
                                                   (vector int)__b, __c);
#endif
}

static __inline__ vector int __ATTRS_o_ai vec_perm(vector signed int __a,
                                                   vector signed int __b,
                                                   vector unsigned char __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __d = {255, 255, 255, 255, 255, 255, 255, 255,
                              255, 255, 255, 255, 255, 255, 255, 255};
  __d = vec_xor(__c, __d);
  return (vector signed int)__builtin_altivec_vperm_4si(__b, __a, __d);
#else
  return (vector signed int)__builtin_altivec_vperm_4si(__a, __b, __c);
#endif
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_perm(vector unsigned int __a, vector unsigned int __b,
         vector unsigned char __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __d = {255, 255, 255, 255, 255, 255, 255, 255,
                              255, 255, 255, 255, 255, 255, 255, 255};
  __d = vec_xor(__c, __d);
  return (vector unsigned int)__builtin_altivec_vperm_4si((vector int)__b,
                                                          (vector int)__a, __d);
#else
  return (vector unsigned int)__builtin_altivec_vperm_4si((vector int)__a,
                                                          (vector int)__b, __c);
#endif
}

static __inline__ vector bool int __ATTRS_o_ai
vec_perm(vector bool int __a, vector bool int __b, vector unsigned char __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __d = {255, 255, 255, 255, 255, 255, 255, 255,
                              255, 255, 255, 255, 255, 255, 255, 255};
  __d = vec_xor(__c, __d);
  return (vector bool int)__builtin_altivec_vperm_4si((vector int)__b,
                                                      (vector int)__a, __d);
#else
  return (vector bool int)__builtin_altivec_vperm_4si((vector int)__a,
                                                      (vector int)__b, __c);
#endif
}

static __inline__ vector float __ATTRS_o_ai vec_perm(vector float __a,
                                                     vector float __b,
                                                     vector unsigned char __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __d = {255, 255, 255, 255, 255, 255, 255, 255,
                              255, 255, 255, 255, 255, 255, 255, 255};
  __d = vec_xor(__c, __d);
  return (vector float)__builtin_altivec_vperm_4si((vector int)__b,
                                                   (vector int)__a, __d);
#else
  return (vector float)__builtin_altivec_vperm_4si((vector int)__a,
                                                   (vector int)__b, __c);
#endif
}

#ifdef __VSX__
static __inline__ vector long long __ATTRS_o_ai
vec_perm(vector signed long long __a, vector signed long long __b,
         vector unsigned char __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __d = {255, 255, 255, 255, 255, 255, 255, 255,
                              255, 255, 255, 255, 255, 255, 255, 255};
  __d = vec_xor(__c, __d);
  return (vector signed long long)__builtin_altivec_vperm_4si(
      (vector int)__b, (vector int)__a, __d);
#else
  return (vector signed long long)__builtin_altivec_vperm_4si(
      (vector int)__a, (vector int)__b, __c);
#endif
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_perm(vector unsigned long long __a, vector unsigned long long __b,
         vector unsigned char __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __d = {255, 255, 255, 255, 255, 255, 255, 255,
                              255, 255, 255, 255, 255, 255, 255, 255};
  __d = vec_xor(__c, __d);
  return (vector unsigned long long)__builtin_altivec_vperm_4si(
      (vector int)__b, (vector int)__a, __d);
#else
  return (vector unsigned long long)__builtin_altivec_vperm_4si(
      (vector int)__a, (vector int)__b, __c);
#endif
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_perm(vector bool long long __a, vector bool long long __b,
         vector unsigned char __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __d = {255, 255, 255, 255, 255, 255, 255, 255,
                              255, 255, 255, 255, 255, 255, 255, 255};
  __d = vec_xor(__c, __d);
  return (vector bool long long)__builtin_altivec_vperm_4si(
      (vector int)__b, (vector int)__a, __d);
#else
  return (vector bool long long)__builtin_altivec_vperm_4si(
      (vector int)__a, (vector int)__b, __c);
#endif
}

static __inline__ vector double __ATTRS_o_ai
vec_perm(vector double __a, vector double __b, vector unsigned char __c) {
#ifdef __LITTLE_ENDIAN__
  vector unsigned char __d = {255, 255, 255, 255, 255, 255, 255, 255,
                              255, 255, 255, 255, 255, 255, 255, 255};
  __d = vec_xor(__c, __d);
  return (vector double)__builtin_altivec_vperm_4si((vector int)__b,
                                                    (vector int)__a, __d);
#else
  return (vector double)__builtin_altivec_vperm_4si((vector int)__a,
                                                    (vector int)__b, __c);
#endif
}
#endif

/* vec_vperm */

static __inline__ vector signed char __ATTRS_o_ai vec_vperm(
    vector signed char __a, vector signed char __b, vector unsigned char __c) {
  return vec_perm(__a, __b, __c);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vperm(vector unsigned char __a, vector unsigned char __b,
          vector unsigned char __c) {
  return vec_perm(__a, __b, __c);
}

static __inline__ vector bool char __ATTRS_o_ai vec_vperm(
    vector bool char __a, vector bool char __b, vector unsigned char __c) {
  return vec_perm(__a, __b, __c);
}

static __inline__ vector short __ATTRS_o_ai
vec_vperm(vector short __a, vector short __b, vector unsigned char __c) {
  return vec_perm(__a, __b, __c);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vperm(vector unsigned short __a, vector unsigned short __b,
          vector unsigned char __c) {
  return vec_perm(__a, __b, __c);
}

static __inline__ vector bool short __ATTRS_o_ai vec_vperm(
    vector bool short __a, vector bool short __b, vector unsigned char __c) {
  return vec_perm(__a, __b, __c);
}

static __inline__ vector pixel __ATTRS_o_ai
vec_vperm(vector pixel __a, vector pixel __b, vector unsigned char __c) {
  return vec_perm(__a, __b, __c);
}

static __inline__ vector int __ATTRS_o_ai vec_vperm(vector int __a,
                                                    vector int __b,
                                                    vector unsigned char __c) {
  return vec_perm(__a, __b, __c);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vperm(vector unsigned int __a, vector unsigned int __b,
          vector unsigned char __c) {
  return vec_perm(__a, __b, __c);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_vperm(vector bool int __a, vector bool int __b, vector unsigned char __c) {
  return vec_perm(__a, __b, __c);
}

static __inline__ vector float __ATTRS_o_ai
vec_vperm(vector float __a, vector float __b, vector unsigned char __c) {
  return vec_perm(__a, __b, __c);
}

#ifdef __VSX__
static __inline__ vector long long __ATTRS_o_ai vec_vperm(
    vector long long __a, vector long long __b, vector unsigned char __c) {
  return vec_perm(__a, __b, __c);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_vperm(vector unsigned long long __a, vector unsigned long long __b,
          vector unsigned char __c) {
  return vec_perm(__a, __b, __c);
}

static __inline__ vector double __ATTRS_o_ai
vec_vperm(vector double __a, vector double __b, vector unsigned char __c) {
  return vec_perm(__a, __b, __c);
}
#endif

/* vec_re */

static __inline__ vector float __ATTRS_o_ai vec_re(vector float __a) {
#ifdef __VSX__
  return __builtin_vsx_xvresp(__a);
#else
  return __builtin_altivec_vrefp(__a);
#endif
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_re(vector double __a) {
  return __builtin_vsx_xvredp(__a);
}
#endif

/* vec_vrefp */

static __inline__ vector float __attribute__((__always_inline__))
vec_vrefp(vector float __a) {
  return __builtin_altivec_vrefp(__a);
}

/* vec_rl */

static __inline__ vector signed char __ATTRS_o_ai
vec_rl(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_altivec_vrlb((vector char)__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_rl(vector unsigned char __a, vector unsigned char __b) {
  return (vector unsigned char)__builtin_altivec_vrlb((vector char)__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_rl(vector short __a,
                                                   vector unsigned short __b) {
  return __builtin_altivec_vrlh(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_rl(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)__builtin_altivec_vrlh((vector short)__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_rl(vector int __a,
                                                 vector unsigned int __b) {
  return __builtin_altivec_vrlw(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_rl(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)__builtin_altivec_vrlw((vector int)__a, __b);
}

#ifdef __POWER8_VECTOR__
static __inline__ vector signed long long __ATTRS_o_ai
vec_rl(vector signed long long __a, vector unsigned long long __b) {
  return __builtin_altivec_vrld(__a, __b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_rl(vector unsigned long long __a, vector unsigned long long __b) {
  return __builtin_altivec_vrld(__a, __b);
}
#endif

/* vec_rlmi */
#ifdef __POWER9_VECTOR__
static __inline__ vector unsigned int __ATTRS_o_ai
vec_rlmi(vector unsigned int __a, vector unsigned int __b,
         vector unsigned int __c) {
  return __builtin_altivec_vrlwmi(__a, __c, __b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_rlmi(vector unsigned long long __a, vector unsigned long long __b,
         vector unsigned long long __c) {
  return __builtin_altivec_vrldmi(__a, __c, __b);
}

/* vec_rlnm */
static __inline__ vector unsigned int __ATTRS_o_ai
vec_rlnm(vector unsigned int __a, vector unsigned int __b,
         vector unsigned int __c) {
  vector unsigned int OneByte = { 0x8, 0x8, 0x8, 0x8 };
  return __builtin_altivec_vrlwnm(__a, ((__c << OneByte) | __b));
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_rlnm(vector unsigned long long __a, vector unsigned long long __b,
         vector unsigned long long __c) {
  vector unsigned long long OneByte = { 0x8, 0x8 };
  return __builtin_altivec_vrldnm(__a, ((__c << OneByte) | __b));
}
#endif

/* vec_vrlb */

static __inline__ vector signed char __ATTRS_o_ai
vec_vrlb(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_altivec_vrlb((vector char)__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vrlb(vector unsigned char __a, vector unsigned char __b) {
  return (vector unsigned char)__builtin_altivec_vrlb((vector char)__a, __b);
}

/* vec_vrlh */

static __inline__ vector short __ATTRS_o_ai
vec_vrlh(vector short __a, vector unsigned short __b) {
  return __builtin_altivec_vrlh(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vrlh(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)__builtin_altivec_vrlh((vector short)__a, __b);
}

/* vec_vrlw */

static __inline__ vector int __ATTRS_o_ai vec_vrlw(vector int __a,
                                                   vector unsigned int __b) {
  return __builtin_altivec_vrlw(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vrlw(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)__builtin_altivec_vrlw((vector int)__a, __b);
}

/* vec_round */

static __inline__ vector float __ATTRS_o_ai vec_round(vector float __a) {
#ifdef __VSX__
  return __builtin_vsx_xvrspi(__a);
#else
  return __builtin_altivec_vrfin(__a);
#endif
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_round(vector double __a) {
  return __builtin_vsx_xvrdpi(__a);
}

/* vec_rint */

static __inline__ vector float __ATTRS_o_ai vec_rint(vector float __a) {
  return __builtin_vsx_xvrspic(__a);
}

static __inline__ vector double __ATTRS_o_ai vec_rint(vector double __a) {
  return __builtin_vsx_xvrdpic(__a);
}

/* vec_nearbyint */

static __inline__ vector float __ATTRS_o_ai vec_nearbyint(vector float __a) {
  return __builtin_vsx_xvrspi(__a);
}

static __inline__ vector double __ATTRS_o_ai vec_nearbyint(vector double __a) {
  return __builtin_vsx_xvrdpi(__a);
}
#endif

/* vec_vrfin */

static __inline__ vector float __attribute__((__always_inline__))
vec_vrfin(vector float __a) {
  return __builtin_altivec_vrfin(__a);
}

/* vec_sqrt */

#ifdef __VSX__
static __inline__ vector float __ATTRS_o_ai vec_sqrt(vector float __a) {
  return __builtin_vsx_xvsqrtsp(__a);
}

static __inline__ vector double __ATTRS_o_ai vec_sqrt(vector double __a) {
  return __builtin_vsx_xvsqrtdp(__a);
}
#endif

/* vec_rsqrte */

static __inline__ vector float __ATTRS_o_ai vec_rsqrte(vector float __a) {
#ifdef __VSX__
  return __builtin_vsx_xvrsqrtesp(__a);
#else
  return __builtin_altivec_vrsqrtefp(__a);
#endif
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_rsqrte(vector double __a) {
  return __builtin_vsx_xvrsqrtedp(__a);
}
#endif

/* vec_vrsqrtefp */

static __inline__ __vector float __attribute__((__always_inline__))
vec_vrsqrtefp(vector float __a) {
  return __builtin_altivec_vrsqrtefp(__a);
}

/* vec_sel */

#define __builtin_altivec_vsel_4si vec_sel

static __inline__ vector signed char __ATTRS_o_ai vec_sel(
    vector signed char __a, vector signed char __b, vector unsigned char __c) {
  return (__a & ~(vector signed char)__c) | (__b & (vector signed char)__c);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_sel(vector signed char __a, vector signed char __b, vector bool char __c) {
  return (__a & ~(vector signed char)__c) | (__b & (vector signed char)__c);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_sel(vector unsigned char __a, vector unsigned char __b,
        vector unsigned char __c) {
  return (__a & ~__c) | (__b & __c);
}

static __inline__ vector unsigned char __ATTRS_o_ai vec_sel(
    vector unsigned char __a, vector unsigned char __b, vector bool char __c) {
  return (__a & ~(vector unsigned char)__c) | (__b & (vector unsigned char)__c);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_sel(vector bool char __a, vector bool char __b, vector unsigned char __c) {
  return (__a & ~(vector bool char)__c) | (__b & (vector bool char)__c);
}

static __inline__ vector bool char __ATTRS_o_ai vec_sel(vector bool char __a,
                                                        vector bool char __b,
                                                        vector bool char __c) {
  return (__a & ~__c) | (__b & __c);
}

static __inline__ vector short __ATTRS_o_ai vec_sel(vector short __a,
                                                    vector short __b,
                                                    vector unsigned short __c) {
  return (__a & ~(vector short)__c) | (__b & (vector short)__c);
}

static __inline__ vector short __ATTRS_o_ai vec_sel(vector short __a,
                                                    vector short __b,
                                                    vector bool short __c) {
  return (__a & ~(vector short)__c) | (__b & (vector short)__c);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_sel(vector unsigned short __a, vector unsigned short __b,
        vector unsigned short __c) {
  return (__a & ~__c) | (__b & __c);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_sel(vector unsigned short __a, vector unsigned short __b,
        vector bool short __c) {
  return (__a & ~(vector unsigned short)__c) |
         (__b & (vector unsigned short)__c);
}

static __inline__ vector bool short __ATTRS_o_ai vec_sel(
    vector bool short __a, vector bool short __b, vector unsigned short __c) {
  return (__a & ~(vector bool short)__c) | (__b & (vector bool short)__c);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_sel(vector bool short __a, vector bool short __b, vector bool short __c) {
  return (__a & ~__c) | (__b & __c);
}

static __inline__ vector int __ATTRS_o_ai vec_sel(vector int __a,
                                                  vector int __b,
                                                  vector unsigned int __c) {
  return (__a & ~(vector int)__c) | (__b & (vector int)__c);
}

static __inline__ vector int __ATTRS_o_ai vec_sel(vector int __a,
                                                  vector int __b,
                                                  vector bool int __c) {
  return (__a & ~(vector int)__c) | (__b & (vector int)__c);
}

static __inline__ vector unsigned int __ATTRS_o_ai vec_sel(
    vector unsigned int __a, vector unsigned int __b, vector unsigned int __c) {
  return (__a & ~__c) | (__b & __c);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_sel(vector unsigned int __a, vector unsigned int __b, vector bool int __c) {
  return (__a & ~(vector unsigned int)__c) | (__b & (vector unsigned int)__c);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_sel(vector bool int __a, vector bool int __b, vector unsigned int __c) {
  return (__a & ~(vector bool int)__c) | (__b & (vector bool int)__c);
}

static __inline__ vector bool int __ATTRS_o_ai vec_sel(vector bool int __a,
                                                       vector bool int __b,
                                                       vector bool int __c) {
  return (__a & ~__c) | (__b & __c);
}

static __inline__ vector float __ATTRS_o_ai vec_sel(vector float __a,
                                                    vector float __b,
                                                    vector unsigned int __c) {
  vector int __res = ((vector int)__a & ~(vector int)__c) |
                     ((vector int)__b & (vector int)__c);
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_sel(vector float __a,
                                                    vector float __b,
                                                    vector bool int __c) {
  vector int __res = ((vector int)__a & ~(vector int)__c) |
                     ((vector int)__b & (vector int)__c);
  return (vector float)__res;
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai
vec_sel(vector double __a, vector double __b, vector bool long long __c) {
  vector long long __res = ((vector long long)__a & ~(vector long long)__c) |
                           ((vector long long)__b & (vector long long)__c);
  return (vector double)__res;
}

static __inline__ vector double __ATTRS_o_ai
vec_sel(vector double __a, vector double __b, vector unsigned long long __c) {
  vector long long __res = ((vector long long)__a & ~(vector long long)__c) |
                           ((vector long long)__b & (vector long long)__c);
  return (vector double)__res;
}
#endif

/* vec_vsel */

static __inline__ vector signed char __ATTRS_o_ai vec_vsel(
    vector signed char __a, vector signed char __b, vector unsigned char __c) {
  return (__a & ~(vector signed char)__c) | (__b & (vector signed char)__c);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vsel(vector signed char __a, vector signed char __b, vector bool char __c) {
  return (__a & ~(vector signed char)__c) | (__b & (vector signed char)__c);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsel(vector unsigned char __a, vector unsigned char __b,
         vector unsigned char __c) {
  return (__a & ~__c) | (__b & __c);
}

static __inline__ vector unsigned char __ATTRS_o_ai vec_vsel(
    vector unsigned char __a, vector unsigned char __b, vector bool char __c) {
  return (__a & ~(vector unsigned char)__c) | (__b & (vector unsigned char)__c);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_vsel(vector bool char __a, vector bool char __b, vector unsigned char __c) {
  return (__a & ~(vector bool char)__c) | (__b & (vector bool char)__c);
}

static __inline__ vector bool char __ATTRS_o_ai vec_vsel(vector bool char __a,
                                                         vector bool char __b,
                                                         vector bool char __c) {
  return (__a & ~__c) | (__b & __c);
}

static __inline__ vector short __ATTRS_o_ai
vec_vsel(vector short __a, vector short __b, vector unsigned short __c) {
  return (__a & ~(vector short)__c) | (__b & (vector short)__c);
}

static __inline__ vector short __ATTRS_o_ai vec_vsel(vector short __a,
                                                     vector short __b,
                                                     vector bool short __c) {
  return (__a & ~(vector short)__c) | (__b & (vector short)__c);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsel(vector unsigned short __a, vector unsigned short __b,
         vector unsigned short __c) {
  return (__a & ~__c) | (__b & __c);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsel(vector unsigned short __a, vector unsigned short __b,
         vector bool short __c) {
  return (__a & ~(vector unsigned short)__c) |
         (__b & (vector unsigned short)__c);
}

static __inline__ vector bool short __ATTRS_o_ai vec_vsel(
    vector bool short __a, vector bool short __b, vector unsigned short __c) {
  return (__a & ~(vector bool short)__c) | (__b & (vector bool short)__c);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vsel(vector bool short __a, vector bool short __b, vector bool short __c) {
  return (__a & ~__c) | (__b & __c);
}

static __inline__ vector int __ATTRS_o_ai vec_vsel(vector int __a,
                                                   vector int __b,
                                                   vector unsigned int __c) {
  return (__a & ~(vector int)__c) | (__b & (vector int)__c);
}

static __inline__ vector int __ATTRS_o_ai vec_vsel(vector int __a,
                                                   vector int __b,
                                                   vector bool int __c) {
  return (__a & ~(vector int)__c) | (__b & (vector int)__c);
}

static __inline__ vector unsigned int __ATTRS_o_ai vec_vsel(
    vector unsigned int __a, vector unsigned int __b, vector unsigned int __c) {
  return (__a & ~__c) | (__b & __c);
}

static __inline__ vector unsigned int __ATTRS_o_ai vec_vsel(
    vector unsigned int __a, vector unsigned int __b, vector bool int __c) {
  return (__a & ~(vector unsigned int)__c) | (__b & (vector unsigned int)__c);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_vsel(vector bool int __a, vector bool int __b, vector unsigned int __c) {
  return (__a & ~(vector bool int)__c) | (__b & (vector bool int)__c);
}

static __inline__ vector bool int __ATTRS_o_ai vec_vsel(vector bool int __a,
                                                        vector bool int __b,
                                                        vector bool int __c) {
  return (__a & ~__c) | (__b & __c);
}

static __inline__ vector float __ATTRS_o_ai vec_vsel(vector float __a,
                                                     vector float __b,
                                                     vector unsigned int __c) {
  vector int __res = ((vector int)__a & ~(vector int)__c) |
                     ((vector int)__b & (vector int)__c);
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_vsel(vector float __a,
                                                     vector float __b,
                                                     vector bool int __c) {
  vector int __res = ((vector int)__a & ~(vector int)__c) |
                     ((vector int)__b & (vector int)__c);
  return (vector float)__res;
}

/* vec_sl */

// vec_sl does modulo arithmetic on __b first, so __b is allowed to be more
// than the length of __a.
static __inline__ vector unsigned char __ATTRS_o_ai
vec_sl(vector unsigned char __a, vector unsigned char __b) {
  return __a << (__b %
                 (vector unsigned char)(sizeof(unsigned char) * __CHAR_BIT__));
}

static __inline__ vector signed char __ATTRS_o_ai
vec_sl(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)vec_sl((vector unsigned char)__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_sl(vector unsigned short __a, vector unsigned short __b) {
  return __a << (__b % (vector unsigned short)(sizeof(unsigned short) *
                                               __CHAR_BIT__));
}

static __inline__ vector short __ATTRS_o_ai vec_sl(vector short __a,
                                                   vector unsigned short __b) {
  return (vector short)vec_sl((vector unsigned short)__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_sl(vector unsigned int __a, vector unsigned int __b) {
  return __a << (__b %
                 (vector unsigned int)(sizeof(unsigned int) * __CHAR_BIT__));
}

static __inline__ vector int __ATTRS_o_ai vec_sl(vector int __a,
                                                 vector unsigned int __b) {
  return (vector int)vec_sl((vector unsigned int)__a, __b);
}

#ifdef __POWER8_VECTOR__
static __inline__ vector unsigned long long __ATTRS_o_ai
vec_sl(vector unsigned long long __a, vector unsigned long long __b) {
  return __a << (__b % (vector unsigned long long)(sizeof(unsigned long long) *
                                                   __CHAR_BIT__));
}

static __inline__ vector long long __ATTRS_o_ai
vec_sl(vector long long __a, vector unsigned long long __b) {
  return (vector long long)vec_sl((vector unsigned long long)__a, __b);
}
#endif

/* vec_vslb */

#define __builtin_altivec_vslb vec_vslb

static __inline__ vector signed char __ATTRS_o_ai
vec_vslb(vector signed char __a, vector unsigned char __b) {
  return vec_sl(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vslb(vector unsigned char __a, vector unsigned char __b) {
  return vec_sl(__a, __b);
}

/* vec_vslh */

#define __builtin_altivec_vslh vec_vslh

static __inline__ vector short __ATTRS_o_ai
vec_vslh(vector short __a, vector unsigned short __b) {
  return vec_sl(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vslh(vector unsigned short __a, vector unsigned short __b) {
  return vec_sl(__a, __b);
}

/* vec_vslw */

#define __builtin_altivec_vslw vec_vslw

static __inline__ vector int __ATTRS_o_ai vec_vslw(vector int __a,
                                                   vector unsigned int __b) {
  return vec_sl(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vslw(vector unsigned int __a, vector unsigned int __b) {
  return vec_sl(__a, __b);
}

/* vec_sld */

#define __builtin_altivec_vsldoi_4si vec_sld

static __inline__ vector signed char __ATTRS_o_ai vec_sld(
    vector signed char __a, vector signed char __b, unsigned const int __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_sld(vector unsigned char __a, vector unsigned char __b,
        unsigned const int __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector bool char __ATTRS_o_ai
vec_sld(vector bool char __a, vector bool char __b, unsigned const int __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector signed short __ATTRS_o_ai vec_sld(
    vector signed short __a, vector signed short __b, unsigned const int __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_sld(vector unsigned short __a, vector unsigned short __b,
        unsigned const int __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector bool short __ATTRS_o_ai
vec_sld(vector bool short __a, vector bool short __b, unsigned const int __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector pixel __ATTRS_o_ai vec_sld(vector pixel __a,
                                                    vector pixel __b,
                                                    unsigned const int __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector signed int __ATTRS_o_ai
vec_sld(vector signed int __a, vector signed int __b, unsigned const int __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector unsigned int __ATTRS_o_ai vec_sld(
    vector unsigned int __a, vector unsigned int __b, unsigned const int __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector bool int __ATTRS_o_ai vec_sld(vector bool int __a,
                                                       vector bool int __b,
                                                       unsigned const int __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector float __ATTRS_o_ai vec_sld(vector float __a,
                                                    vector float __b,
                                                    unsigned const int __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

#ifdef __VSX__
static __inline__ vector bool long long __ATTRS_o_ai
vec_sld(vector bool long long __a, vector bool long long __b,
        unsigned const int __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_sld(vector signed long long __a, vector signed long long __b,
        unsigned const int __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_sld(vector unsigned long long __a, vector unsigned long long __b,
        unsigned const int __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector double __ATTRS_o_ai vec_sld(vector double __a,
                                                     vector double __b,
                                                     unsigned const int __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}
#endif

/* vec_sldw */
static __inline__ vector signed char __ATTRS_o_ai vec_sldw(
    vector signed char __a, vector signed char __b, unsigned const int __c) {
  return vec_sld(__a, __b, ((__c << 2) & 0x0F));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_sldw(vector unsigned char __a, vector unsigned char __b,
         unsigned const int __c) {
  return vec_sld(__a, __b, ((__c << 2) & 0x0F));
}

static __inline__ vector signed short __ATTRS_o_ai vec_sldw(
    vector signed short __a, vector signed short __b, unsigned const int __c) {
  return vec_sld(__a, __b, ((__c << 2) & 0x0F));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_sldw(vector unsigned short __a, vector unsigned short __b,
         unsigned const int __c) {
  return vec_sld(__a, __b, ((__c << 2) & 0x0F));
}

static __inline__ vector signed int __ATTRS_o_ai
vec_sldw(vector signed int __a, vector signed int __b, unsigned const int __c) {
  return vec_sld(__a, __b, ((__c << 2) & 0x0F));
}

static __inline__ vector unsigned int __ATTRS_o_ai vec_sldw(
    vector unsigned int __a, vector unsigned int __b, unsigned const int __c) {
  return vec_sld(__a, __b, ((__c << 2) & 0x0F));
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_sldw(vector signed long long __a, vector signed long long __b,
         unsigned const int __c) {
  return vec_sld(__a, __b, ((__c << 2) & 0x0F));
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_sldw(vector unsigned long long __a, vector unsigned long long __b,
         unsigned const int __c) {
  return vec_sld(__a, __b, ((__c << 2) & 0x0F));
}
#endif

#ifdef __POWER9_VECTOR__
/* vec_slv */
static __inline__ vector unsigned char __ATTRS_o_ai
vec_slv(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_altivec_vslv(__a, __b);
}

/* vec_srv */
static __inline__ vector unsigned char __ATTRS_o_ai
vec_srv(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_altivec_vsrv(__a, __b);
}
#endif

/* vec_vsldoi */

static __inline__ vector signed char __ATTRS_o_ai
vec_vsldoi(vector signed char __a, vector signed char __b, unsigned char __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector unsigned char __ATTRS_o_ai vec_vsldoi(
    vector unsigned char __a, vector unsigned char __b, unsigned char __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector short __ATTRS_o_ai vec_vsldoi(vector short __a,
                                                       vector short __b,
                                                       unsigned char __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector unsigned short __ATTRS_o_ai vec_vsldoi(
    vector unsigned short __a, vector unsigned short __b, unsigned char __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector pixel __ATTRS_o_ai vec_vsldoi(vector pixel __a,
                                                       vector pixel __b,
                                                       unsigned char __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector int __ATTRS_o_ai vec_vsldoi(vector int __a,
                                                     vector int __b,
                                                     unsigned char __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector unsigned int __ATTRS_o_ai vec_vsldoi(
    vector unsigned int __a, vector unsigned int __b, unsigned char __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

static __inline__ vector float __ATTRS_o_ai vec_vsldoi(vector float __a,
                                                       vector float __b,
                                                       unsigned char __c) {
  unsigned char __d = __c & 0x0F;
#ifdef __LITTLE_ENDIAN__
  return vec_perm(
      __b, __a, (vector unsigned char)(16 - __d, 17 - __d, 18 - __d, 19 - __d,
                                       20 - __d, 21 - __d, 22 - __d, 23 - __d,
                                       24 - __d, 25 - __d, 26 - __d, 27 - __d,
                                       28 - __d, 29 - __d, 30 - __d, 31 - __d));
#else
  return vec_perm(
      __a, __b,
      (vector unsigned char)(__d, __d + 1, __d + 2, __d + 3, __d + 4, __d + 5,
                             __d + 6, __d + 7, __d + 8, __d + 9, __d + 10,
                             __d + 11, __d + 12, __d + 13, __d + 14, __d + 15));
#endif
}

/* vec_sll */

static __inline__ vector signed char __ATTRS_o_ai
vec_sll(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_altivec_vsl((vector int)__a,
                                                   (vector int)__b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_sll(vector signed char __a, vector unsigned short __b) {
  return (vector signed char)__builtin_altivec_vsl((vector int)__a,
                                                   (vector int)__b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_sll(vector signed char __a, vector unsigned int __b) {
  return (vector signed char)__builtin_altivec_vsl((vector int)__a,
                                                   (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_sll(vector unsigned char __a, vector unsigned char __b) {
  return (vector unsigned char)__builtin_altivec_vsl((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_sll(vector unsigned char __a, vector unsigned short __b) {
  return (vector unsigned char)__builtin_altivec_vsl((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_sll(vector unsigned char __a, vector unsigned int __b) {
  return (vector unsigned char)__builtin_altivec_vsl((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_sll(vector bool char __a, vector unsigned char __b) {
  return (vector bool char)__builtin_altivec_vsl((vector int)__a,
                                                 (vector int)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_sll(vector bool char __a, vector unsigned short __b) {
  return (vector bool char)__builtin_altivec_vsl((vector int)__a,
                                                 (vector int)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_sll(vector bool char __a, vector unsigned int __b) {
  return (vector bool char)__builtin_altivec_vsl((vector int)__a,
                                                 (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_sll(vector short __a,
                                                    vector unsigned char __b) {
  return (vector short)__builtin_altivec_vsl((vector int)__a, (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_sll(vector short __a,
                                                    vector unsigned short __b) {
  return (vector short)__builtin_altivec_vsl((vector int)__a, (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_sll(vector short __a,
                                                    vector unsigned int __b) {
  return (vector short)__builtin_altivec_vsl((vector int)__a, (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_sll(vector unsigned short __a, vector unsigned char __b) {
  return (vector unsigned short)__builtin_altivec_vsl((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_sll(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)__builtin_altivec_vsl((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_sll(vector unsigned short __a, vector unsigned int __b) {
  return (vector unsigned short)__builtin_altivec_vsl((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_sll(vector bool short __a, vector unsigned char __b) {
  return (vector bool short)__builtin_altivec_vsl((vector int)__a,
                                                  (vector int)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_sll(vector bool short __a, vector unsigned short __b) {
  return (vector bool short)__builtin_altivec_vsl((vector int)__a,
                                                  (vector int)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_sll(vector bool short __a, vector unsigned int __b) {
  return (vector bool short)__builtin_altivec_vsl((vector int)__a,
                                                  (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_sll(vector pixel __a,
                                                    vector unsigned char __b) {
  return (vector pixel)__builtin_altivec_vsl((vector int)__a, (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_sll(vector pixel __a,
                                                    vector unsigned short __b) {
  return (vector pixel)__builtin_altivec_vsl((vector int)__a, (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_sll(vector pixel __a,
                                                    vector unsigned int __b) {
  return (vector pixel)__builtin_altivec_vsl((vector int)__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_sll(vector int __a,
                                                  vector unsigned char __b) {
  return (vector int)__builtin_altivec_vsl(__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_sll(vector int __a,
                                                  vector unsigned short __b) {
  return (vector int)__builtin_altivec_vsl(__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_sll(vector int __a,
                                                  vector unsigned int __b) {
  return (vector int)__builtin_altivec_vsl(__a, (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_sll(vector unsigned int __a, vector unsigned char __b) {
  return (vector unsigned int)__builtin_altivec_vsl((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_sll(vector unsigned int __a, vector unsigned short __b) {
  return (vector unsigned int)__builtin_altivec_vsl((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_sll(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)__builtin_altivec_vsl((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_sll(vector bool int __a, vector unsigned char __b) {
  return (vector bool int)__builtin_altivec_vsl((vector int)__a,
                                                (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_sll(vector bool int __a, vector unsigned short __b) {
  return (vector bool int)__builtin_altivec_vsl((vector int)__a,
                                                (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_sll(vector bool int __a, vector unsigned int __b) {
  return (vector bool int)__builtin_altivec_vsl((vector int)__a,
                                                (vector int)__b);
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_sll(vector signed long long __a, vector unsigned char __b) {
  return (vector signed long long)__builtin_altivec_vsl((vector int)__a,
                                                        (vector int)__b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_sll(vector unsigned long long __a, vector unsigned char __b) {
  return (vector unsigned long long)__builtin_altivec_vsl((vector int)__a,
                                                          (vector int)__b);
}
#endif

/* vec_vsl */

static __inline__ vector signed char __ATTRS_o_ai
vec_vsl(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_altivec_vsl((vector int)__a,
                                                   (vector int)__b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vsl(vector signed char __a, vector unsigned short __b) {
  return (vector signed char)__builtin_altivec_vsl((vector int)__a,
                                                   (vector int)__b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vsl(vector signed char __a, vector unsigned int __b) {
  return (vector signed char)__builtin_altivec_vsl((vector int)__a,
                                                   (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsl(vector unsigned char __a, vector unsigned char __b) {
  return (vector unsigned char)__builtin_altivec_vsl((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsl(vector unsigned char __a, vector unsigned short __b) {
  return (vector unsigned char)__builtin_altivec_vsl((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsl(vector unsigned char __a, vector unsigned int __b) {
  return (vector unsigned char)__builtin_altivec_vsl((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_vsl(vector bool char __a, vector unsigned char __b) {
  return (vector bool char)__builtin_altivec_vsl((vector int)__a,
                                                 (vector int)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_vsl(vector bool char __a, vector unsigned short __b) {
  return (vector bool char)__builtin_altivec_vsl((vector int)__a,
                                                 (vector int)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_vsl(vector bool char __a, vector unsigned int __b) {
  return (vector bool char)__builtin_altivec_vsl((vector int)__a,
                                                 (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_vsl(vector short __a,
                                                    vector unsigned char __b) {
  return (vector short)__builtin_altivec_vsl((vector int)__a, (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_vsl(vector short __a,
                                                    vector unsigned short __b) {
  return (vector short)__builtin_altivec_vsl((vector int)__a, (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_vsl(vector short __a,
                                                    vector unsigned int __b) {
  return (vector short)__builtin_altivec_vsl((vector int)__a, (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsl(vector unsigned short __a, vector unsigned char __b) {
  return (vector unsigned short)__builtin_altivec_vsl((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsl(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)__builtin_altivec_vsl((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsl(vector unsigned short __a, vector unsigned int __b) {
  return (vector unsigned short)__builtin_altivec_vsl((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vsl(vector bool short __a, vector unsigned char __b) {
  return (vector bool short)__builtin_altivec_vsl((vector int)__a,
                                                  (vector int)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vsl(vector bool short __a, vector unsigned short __b) {
  return (vector bool short)__builtin_altivec_vsl((vector int)__a,
                                                  (vector int)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vsl(vector bool short __a, vector unsigned int __b) {
  return (vector bool short)__builtin_altivec_vsl((vector int)__a,
                                                  (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_vsl(vector pixel __a,
                                                    vector unsigned char __b) {
  return (vector pixel)__builtin_altivec_vsl((vector int)__a, (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_vsl(vector pixel __a,
                                                    vector unsigned short __b) {
  return (vector pixel)__builtin_altivec_vsl((vector int)__a, (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_vsl(vector pixel __a,
                                                    vector unsigned int __b) {
  return (vector pixel)__builtin_altivec_vsl((vector int)__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_vsl(vector int __a,
                                                  vector unsigned char __b) {
  return (vector int)__builtin_altivec_vsl(__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_vsl(vector int __a,
                                                  vector unsigned short __b) {
  return (vector int)__builtin_altivec_vsl(__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_vsl(vector int __a,
                                                  vector unsigned int __b) {
  return (vector int)__builtin_altivec_vsl(__a, (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsl(vector unsigned int __a, vector unsigned char __b) {
  return (vector unsigned int)__builtin_altivec_vsl((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsl(vector unsigned int __a, vector unsigned short __b) {
  return (vector unsigned int)__builtin_altivec_vsl((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsl(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)__builtin_altivec_vsl((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_vsl(vector bool int __a, vector unsigned char __b) {
  return (vector bool int)__builtin_altivec_vsl((vector int)__a,
                                                (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_vsl(vector bool int __a, vector unsigned short __b) {
  return (vector bool int)__builtin_altivec_vsl((vector int)__a,
                                                (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_vsl(vector bool int __a, vector unsigned int __b) {
  return (vector bool int)__builtin_altivec_vsl((vector int)__a,
                                                (vector int)__b);
}

/* vec_slo */

static __inline__ vector signed char __ATTRS_o_ai
vec_slo(vector signed char __a, vector signed char __b) {
  return (vector signed char)__builtin_altivec_vslo((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_slo(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_altivec_vslo((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_slo(vector unsigned char __a, vector signed char __b) {
  return (vector unsigned char)__builtin_altivec_vslo((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_slo(vector unsigned char __a, vector unsigned char __b) {
  return (vector unsigned char)__builtin_altivec_vslo((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_slo(vector short __a,
                                                    vector signed char __b) {
  return (vector short)__builtin_altivec_vslo((vector int)__a, (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_slo(vector short __a,
                                                    vector unsigned char __b) {
  return (vector short)__builtin_altivec_vslo((vector int)__a, (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_slo(vector unsigned short __a, vector signed char __b) {
  return (vector unsigned short)__builtin_altivec_vslo((vector int)__a,
                                                       (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_slo(vector unsigned short __a, vector unsigned char __b) {
  return (vector unsigned short)__builtin_altivec_vslo((vector int)__a,
                                                       (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_slo(vector pixel __a,
                                                    vector signed char __b) {
  return (vector pixel)__builtin_altivec_vslo((vector int)__a, (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_slo(vector pixel __a,
                                                    vector unsigned char __b) {
  return (vector pixel)__builtin_altivec_vslo((vector int)__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_slo(vector int __a,
                                                  vector signed char __b) {
  return (vector int)__builtin_altivec_vslo(__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_slo(vector int __a,
                                                  vector unsigned char __b) {
  return (vector int)__builtin_altivec_vslo(__a, (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_slo(vector unsigned int __a, vector signed char __b) {
  return (vector unsigned int)__builtin_altivec_vslo((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_slo(vector unsigned int __a, vector unsigned char __b) {
  return (vector unsigned int)__builtin_altivec_vslo((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector float __ATTRS_o_ai vec_slo(vector float __a,
                                                    vector signed char __b) {
  return (vector float)__builtin_altivec_vslo((vector int)__a, (vector int)__b);
}

static __inline__ vector float __ATTRS_o_ai vec_slo(vector float __a,
                                                    vector unsigned char __b) {
  return (vector float)__builtin_altivec_vslo((vector int)__a, (vector int)__b);
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_slo(vector signed long long __a, vector signed char __b) {
  return (vector signed long long)__builtin_altivec_vslo((vector int)__a,
                                                         (vector int)__b);
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_slo(vector signed long long __a, vector unsigned char __b) {
  return (vector signed long long)__builtin_altivec_vslo((vector int)__a,
                                                         (vector int)__b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_slo(vector unsigned long long __a, vector signed char __b) {
  return (vector unsigned long long)__builtin_altivec_vslo((vector int)__a,
                                                           (vector int)__b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_slo(vector unsigned long long __a, vector unsigned char __b) {
  return (vector unsigned long long)__builtin_altivec_vslo((vector int)__a,
                                                           (vector int)__b);
}
#endif

/* vec_vslo */

static __inline__ vector signed char __ATTRS_o_ai
vec_vslo(vector signed char __a, vector signed char __b) {
  return (vector signed char)__builtin_altivec_vslo((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vslo(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_altivec_vslo((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vslo(vector unsigned char __a, vector signed char __b) {
  return (vector unsigned char)__builtin_altivec_vslo((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vslo(vector unsigned char __a, vector unsigned char __b) {
  return (vector unsigned char)__builtin_altivec_vslo((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_vslo(vector short __a,
                                                     vector signed char __b) {
  return (vector short)__builtin_altivec_vslo((vector int)__a, (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_vslo(vector short __a,
                                                     vector unsigned char __b) {
  return (vector short)__builtin_altivec_vslo((vector int)__a, (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vslo(vector unsigned short __a, vector signed char __b) {
  return (vector unsigned short)__builtin_altivec_vslo((vector int)__a,
                                                       (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vslo(vector unsigned short __a, vector unsigned char __b) {
  return (vector unsigned short)__builtin_altivec_vslo((vector int)__a,
                                                       (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_vslo(vector pixel __a,
                                                     vector signed char __b) {
  return (vector pixel)__builtin_altivec_vslo((vector int)__a, (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_vslo(vector pixel __a,
                                                     vector unsigned char __b) {
  return (vector pixel)__builtin_altivec_vslo((vector int)__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_vslo(vector int __a,
                                                   vector signed char __b) {
  return (vector int)__builtin_altivec_vslo(__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_vslo(vector int __a,
                                                   vector unsigned char __b) {
  return (vector int)__builtin_altivec_vslo(__a, (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vslo(vector unsigned int __a, vector signed char __b) {
  return (vector unsigned int)__builtin_altivec_vslo((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vslo(vector unsigned int __a, vector unsigned char __b) {
  return (vector unsigned int)__builtin_altivec_vslo((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector float __ATTRS_o_ai vec_vslo(vector float __a,
                                                     vector signed char __b) {
  return (vector float)__builtin_altivec_vslo((vector int)__a, (vector int)__b);
}

static __inline__ vector float __ATTRS_o_ai vec_vslo(vector float __a,
                                                     vector unsigned char __b) {
  return (vector float)__builtin_altivec_vslo((vector int)__a, (vector int)__b);
}

/* vec_splat */

static __inline__ vector signed char __ATTRS_o_ai
vec_splat(vector signed char __a, unsigned const int __b) {
  return vec_perm(__a, __a, (vector unsigned char)(__b & 0x0F));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_splat(vector unsigned char __a, unsigned const int __b) {
  return vec_perm(__a, __a, (vector unsigned char)(__b & 0x0F));
}

static __inline__ vector bool char __ATTRS_o_ai
vec_splat(vector bool char __a, unsigned const int __b) {
  return vec_perm(__a, __a, (vector unsigned char)(__b & 0x0F));
}

static __inline__ vector signed short __ATTRS_o_ai
vec_splat(vector signed short __a, unsigned const int __b) {
  unsigned char b0 = (__b & 0x07) * 2;
  unsigned char b1 = b0 + 1;
  return vec_perm(__a, __a,
                  (vector unsigned char)(b0, b1, b0, b1, b0, b1, b0, b1, b0, b1,
                                         b0, b1, b0, b1, b0, b1));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_splat(vector unsigned short __a, unsigned const int __b) {
  unsigned char b0 = (__b & 0x07) * 2;
  unsigned char b1 = b0 + 1;
  return vec_perm(__a, __a,
                  (vector unsigned char)(b0, b1, b0, b1, b0, b1, b0, b1, b0, b1,
                                         b0, b1, b0, b1, b0, b1));
}

static __inline__ vector bool short __ATTRS_o_ai
vec_splat(vector bool short __a, unsigned const int __b) {
  unsigned char b0 = (__b & 0x07) * 2;
  unsigned char b1 = b0 + 1;
  return vec_perm(__a, __a,
                  (vector unsigned char)(b0, b1, b0, b1, b0, b1, b0, b1, b0, b1,
                                         b0, b1, b0, b1, b0, b1));
}

static __inline__ vector pixel __ATTRS_o_ai vec_splat(vector pixel __a,
                                                      unsigned const int __b) {
  unsigned char b0 = (__b & 0x07) * 2;
  unsigned char b1 = b0 + 1;
  return vec_perm(__a, __a,
                  (vector unsigned char)(b0, b1, b0, b1, b0, b1, b0, b1, b0, b1,
                                         b0, b1, b0, b1, b0, b1));
}

static __inline__ vector signed int __ATTRS_o_ai
vec_splat(vector signed int __a, unsigned const int __b) {
  unsigned char b0 = (__b & 0x03) * 4;
  unsigned char b1 = b0 + 1, b2 = b0 + 2, b3 = b0 + 3;
  return vec_perm(__a, __a,
                  (vector unsigned char)(b0, b1, b2, b3, b0, b1, b2, b3, b0, b1,
                                         b2, b3, b0, b1, b2, b3));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_splat(vector unsigned int __a, unsigned const int __b) {
  unsigned char b0 = (__b & 0x03) * 4;
  unsigned char b1 = b0 + 1, b2 = b0 + 2, b3 = b0 + 3;
  return vec_perm(__a, __a,
                  (vector unsigned char)(b0, b1, b2, b3, b0, b1, b2, b3, b0, b1,
                                         b2, b3, b0, b1, b2, b3));
}

static __inline__ vector bool int __ATTRS_o_ai
vec_splat(vector bool int __a, unsigned const int __b) {
  unsigned char b0 = (__b & 0x03) * 4;
  unsigned char b1 = b0 + 1, b2 = b0 + 2, b3 = b0 + 3;
  return vec_perm(__a, __a,
                  (vector unsigned char)(b0, b1, b2, b3, b0, b1, b2, b3, b0, b1,
                                         b2, b3, b0, b1, b2, b3));
}

static __inline__ vector float __ATTRS_o_ai vec_splat(vector float __a,
                                                      unsigned const int __b) {
  unsigned char b0 = (__b & 0x03) * 4;
  unsigned char b1 = b0 + 1, b2 = b0 + 2, b3 = b0 + 3;
  return vec_perm(__a, __a,
                  (vector unsigned char)(b0, b1, b2, b3, b0, b1, b2, b3, b0, b1,
                                         b2, b3, b0, b1, b2, b3));
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_splat(vector double __a,
                                                       unsigned const int __b) {
  unsigned char b0 = (__b & 0x01) * 8;
  unsigned char b1 = b0 + 1, b2 = b0 + 2, b3 = b0 + 3, b4 = b0 + 4, b5 = b0 + 5,
                b6 = b0 + 6, b7 = b0 + 7;
  return vec_perm(__a, __a,
                  (vector unsigned char)(b0, b1, b2, b3, b4, b5, b6, b7, b0, b1,
                                         b2, b3, b4, b5, b6, b7));
}
static __inline__ vector bool long long __ATTRS_o_ai
vec_splat(vector bool long long __a, unsigned const int __b) {
  unsigned char b0 = (__b & 0x01) * 8;
  unsigned char b1 = b0 + 1, b2 = b0 + 2, b3 = b0 + 3, b4 = b0 + 4, b5 = b0 + 5,
                b6 = b0 + 6, b7 = b0 + 7;
  return vec_perm(__a, __a,
                  (vector unsigned char)(b0, b1, b2, b3, b4, b5, b6, b7, b0, b1,
                                         b2, b3, b4, b5, b6, b7));
}
static __inline__ vector signed long long __ATTRS_o_ai
vec_splat(vector signed long long __a, unsigned const int __b) {
  unsigned char b0 = (__b & 0x01) * 8;
  unsigned char b1 = b0 + 1, b2 = b0 + 2, b3 = b0 + 3, b4 = b0 + 4, b5 = b0 + 5,
                b6 = b0 + 6, b7 = b0 + 7;
  return vec_perm(__a, __a,
                  (vector unsigned char)(b0, b1, b2, b3, b4, b5, b6, b7, b0, b1,
                                         b2, b3, b4, b5, b6, b7));
}
static __inline__ vector unsigned long long __ATTRS_o_ai
vec_splat(vector unsigned long long __a, unsigned const int __b) {
  unsigned char b0 = (__b & 0x01) * 8;
  unsigned char b1 = b0 + 1, b2 = b0 + 2, b3 = b0 + 3, b4 = b0 + 4, b5 = b0 + 5,
                b6 = b0 + 6, b7 = b0 + 7;
  return vec_perm(__a, __a,
                  (vector unsigned char)(b0, b1, b2, b3, b4, b5, b6, b7, b0, b1,
                                         b2, b3, b4, b5, b6, b7));
}
#endif

/* vec_vspltb */

#define __builtin_altivec_vspltb vec_vspltb

static __inline__ vector signed char __ATTRS_o_ai
vec_vspltb(vector signed char __a, unsigned char __b) {
  return vec_perm(__a, __a, (vector unsigned char)(__b));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vspltb(vector unsigned char __a, unsigned char __b) {
  return vec_perm(__a, __a, (vector unsigned char)(__b));
}

static __inline__ vector bool char __ATTRS_o_ai vec_vspltb(vector bool char __a,
                                                           unsigned char __b) {
  return vec_perm(__a, __a, (vector unsigned char)(__b));
}

/* vec_vsplth */

#define __builtin_altivec_vsplth vec_vsplth

static __inline__ vector short __ATTRS_o_ai vec_vsplth(vector short __a,
                                                       unsigned char __b) {
  __b *= 2;
  unsigned char b1 = __b + 1;
  return vec_perm(__a, __a,
                  (vector unsigned char)(__b, b1, __b, b1, __b, b1, __b, b1,
                                         __b, b1, __b, b1, __b, b1, __b, b1));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsplth(vector unsigned short __a, unsigned char __b) {
  __b *= 2;
  unsigned char b1 = __b + 1;
  return vec_perm(__a, __a,
                  (vector unsigned char)(__b, b1, __b, b1, __b, b1, __b, b1,
                                         __b, b1, __b, b1, __b, b1, __b, b1));
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vsplth(vector bool short __a, unsigned char __b) {
  __b *= 2;
  unsigned char b1 = __b + 1;
  return vec_perm(__a, __a,
                  (vector unsigned char)(__b, b1, __b, b1, __b, b1, __b, b1,
                                         __b, b1, __b, b1, __b, b1, __b, b1));
}

static __inline__ vector pixel __ATTRS_o_ai vec_vsplth(vector pixel __a,
                                                       unsigned char __b) {
  __b *= 2;
  unsigned char b1 = __b + 1;
  return vec_perm(__a, __a,
                  (vector unsigned char)(__b, b1, __b, b1, __b, b1, __b, b1,
                                         __b, b1, __b, b1, __b, b1, __b, b1));
}

/* vec_vspltw */

#define __builtin_altivec_vspltw vec_vspltw

static __inline__ vector int __ATTRS_o_ai vec_vspltw(vector int __a,
                                                     unsigned char __b) {
  __b *= 4;
  unsigned char b1 = __b + 1, b2 = __b + 2, b3 = __b + 3;
  return vec_perm(__a, __a,
                  (vector unsigned char)(__b, b1, b2, b3, __b, b1, b2, b3, __b,
                                         b1, b2, b3, __b, b1, b2, b3));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vspltw(vector unsigned int __a, unsigned char __b) {
  __b *= 4;
  unsigned char b1 = __b + 1, b2 = __b + 2, b3 = __b + 3;
  return vec_perm(__a, __a,
                  (vector unsigned char)(__b, b1, b2, b3, __b, b1, b2, b3, __b,
                                         b1, b2, b3, __b, b1, b2, b3));
}

static __inline__ vector bool int __ATTRS_o_ai vec_vspltw(vector bool int __a,
                                                          unsigned char __b) {
  __b *= 4;
  unsigned char b1 = __b + 1, b2 = __b + 2, b3 = __b + 3;
  return vec_perm(__a, __a,
                  (vector unsigned char)(__b, b1, b2, b3, __b, b1, b2, b3, __b,
                                         b1, b2, b3, __b, b1, b2, b3));
}

static __inline__ vector float __ATTRS_o_ai vec_vspltw(vector float __a,
                                                       unsigned char __b) {
  __b *= 4;
  unsigned char b1 = __b + 1, b2 = __b + 2, b3 = __b + 3;
  return vec_perm(__a, __a,
                  (vector unsigned char)(__b, b1, b2, b3, __b, b1, b2, b3, __b,
                                         b1, b2, b3, __b, b1, b2, b3));
}

/* vec_splat_s8 */

#define __builtin_altivec_vspltisb vec_splat_s8

// FIXME: parameter should be treated as 5-bit signed literal
static __inline__ vector signed char __ATTRS_o_ai
vec_splat_s8(signed char __a) {
  return (vector signed char)(__a);
}

/* vec_vspltisb */

// FIXME: parameter should be treated as 5-bit signed literal
static __inline__ vector signed char __ATTRS_o_ai
vec_vspltisb(signed char __a) {
  return (vector signed char)(__a);
}

/* vec_splat_s16 */

#define __builtin_altivec_vspltish vec_splat_s16

// FIXME: parameter should be treated as 5-bit signed literal
static __inline__ vector short __ATTRS_o_ai vec_splat_s16(signed char __a) {
  return (vector short)(__a);
}

/* vec_vspltish */

// FIXME: parameter should be treated as 5-bit signed literal
static __inline__ vector short __ATTRS_o_ai vec_vspltish(signed char __a) {
  return (vector short)(__a);
}

/* vec_splat_s32 */

#define __builtin_altivec_vspltisw vec_splat_s32

// FIXME: parameter should be treated as 5-bit signed literal
static __inline__ vector int __ATTRS_o_ai vec_splat_s32(signed char __a) {
  return (vector int)(__a);
}

/* vec_vspltisw */

// FIXME: parameter should be treated as 5-bit signed literal
static __inline__ vector int __ATTRS_o_ai vec_vspltisw(signed char __a) {
  return (vector int)(__a);
}

/* vec_splat_u8 */

// FIXME: parameter should be treated as 5-bit signed literal
static __inline__ vector unsigned char __ATTRS_o_ai
vec_splat_u8(unsigned char __a) {
  return (vector unsigned char)(__a);
}

/* vec_splat_u16 */

// FIXME: parameter should be treated as 5-bit signed literal
static __inline__ vector unsigned short __ATTRS_o_ai
vec_splat_u16(signed char __a) {
  return (vector unsigned short)(__a);
}

/* vec_splat_u32 */

// FIXME: parameter should be treated as 5-bit signed literal
static __inline__ vector unsigned int __ATTRS_o_ai
vec_splat_u32(signed char __a) {
  return (vector unsigned int)(__a);
}

/* vec_sr */

// vec_sr does modulo arithmetic on __b first, so __b is allowed to be more
// than the length of __a.
static __inline__ vector unsigned char __ATTRS_o_ai
vec_sr(vector unsigned char __a, vector unsigned char __b) {
  return __a >>
         (__b % (vector unsigned char)(sizeof(unsigned char) * __CHAR_BIT__));
}

static __inline__ vector signed char __ATTRS_o_ai
vec_sr(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)vec_sr((vector unsigned char)__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_sr(vector unsigned short __a, vector unsigned short __b) {
  return __a >>
         (__b % (vector unsigned short)(sizeof(unsigned short) * __CHAR_BIT__));
}

static __inline__ vector short __ATTRS_o_ai vec_sr(vector short __a,
                                                   vector unsigned short __b) {
  return (vector short)vec_sr((vector unsigned short)__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_sr(vector unsigned int __a, vector unsigned int __b) {
  return __a >>
         (__b % (vector unsigned int)(sizeof(unsigned int) * __CHAR_BIT__));
}

static __inline__ vector int __ATTRS_o_ai vec_sr(vector int __a,
                                                 vector unsigned int __b) {
  return (vector int)vec_sr((vector unsigned int)__a, __b);
}

#ifdef __POWER8_VECTOR__
static __inline__ vector unsigned long long __ATTRS_o_ai
vec_sr(vector unsigned long long __a, vector unsigned long long __b) {
  return __a >> (__b % (vector unsigned long long)(sizeof(unsigned long long) *
                                                   __CHAR_BIT__));
}

static __inline__ vector long long __ATTRS_o_ai
vec_sr(vector long long __a, vector unsigned long long __b) {
  return (vector long long)vec_sr((vector unsigned long long)__a, __b);
}
#endif

/* vec_vsrb */

#define __builtin_altivec_vsrb vec_vsrb

static __inline__ vector signed char __ATTRS_o_ai
vec_vsrb(vector signed char __a, vector unsigned char __b) {
  return vec_sr(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsrb(vector unsigned char __a, vector unsigned char __b) {
  return vec_sr(__a, __b);
}

/* vec_vsrh */

#define __builtin_altivec_vsrh vec_vsrh

static __inline__ vector short __ATTRS_o_ai
vec_vsrh(vector short __a, vector unsigned short __b) {
  return vec_sr(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsrh(vector unsigned short __a, vector unsigned short __b) {
  return vec_sr(__a, __b);
}

/* vec_vsrw */

#define __builtin_altivec_vsrw vec_vsrw

static __inline__ vector int __ATTRS_o_ai vec_vsrw(vector int __a,
                                                   vector unsigned int __b) {
  return vec_sr(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsrw(vector unsigned int __a, vector unsigned int __b) {
  return vec_sr(__a, __b);
}

/* vec_sra */

static __inline__ vector signed char __ATTRS_o_ai
vec_sra(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_altivec_vsrab((vector char)__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_sra(vector unsigned char __a, vector unsigned char __b) {
  return (vector unsigned char)__builtin_altivec_vsrab((vector char)__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_sra(vector short __a,
                                                    vector unsigned short __b) {
  return __builtin_altivec_vsrah(__a, (vector unsigned short)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_sra(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)__builtin_altivec_vsrah((vector short)__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_sra(vector int __a,
                                                  vector unsigned int __b) {
  return __builtin_altivec_vsraw(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_sra(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)__builtin_altivec_vsraw((vector int)__a, __b);
}

#ifdef __POWER8_VECTOR__
static __inline__ vector signed long long __ATTRS_o_ai
vec_sra(vector signed long long __a, vector unsigned long long __b) {
  return __a >> __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_sra(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector unsigned long long)((vector signed long long)__a >> __b);
}
#endif

/* vec_vsrab */

static __inline__ vector signed char __ATTRS_o_ai
vec_vsrab(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_altivec_vsrab((vector char)__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsrab(vector unsigned char __a, vector unsigned char __b) {
  return (vector unsigned char)__builtin_altivec_vsrab((vector char)__a, __b);
}

/* vec_vsrah */

static __inline__ vector short __ATTRS_o_ai
vec_vsrah(vector short __a, vector unsigned short __b) {
  return __builtin_altivec_vsrah(__a, (vector unsigned short)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsrah(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)__builtin_altivec_vsrah((vector short)__a, __b);
}

/* vec_vsraw */

static __inline__ vector int __ATTRS_o_ai vec_vsraw(vector int __a,
                                                    vector unsigned int __b) {
  return __builtin_altivec_vsraw(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsraw(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)__builtin_altivec_vsraw((vector int)__a, __b);
}

/* vec_srl */

static __inline__ vector signed char __ATTRS_o_ai
vec_srl(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_altivec_vsr((vector int)__a,
                                                   (vector int)__b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_srl(vector signed char __a, vector unsigned short __b) {
  return (vector signed char)__builtin_altivec_vsr((vector int)__a,
                                                   (vector int)__b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_srl(vector signed char __a, vector unsigned int __b) {
  return (vector signed char)__builtin_altivec_vsr((vector int)__a,
                                                   (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_srl(vector unsigned char __a, vector unsigned char __b) {
  return (vector unsigned char)__builtin_altivec_vsr((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_srl(vector unsigned char __a, vector unsigned short __b) {
  return (vector unsigned char)__builtin_altivec_vsr((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_srl(vector unsigned char __a, vector unsigned int __b) {
  return (vector unsigned char)__builtin_altivec_vsr((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_srl(vector bool char __a, vector unsigned char __b) {
  return (vector bool char)__builtin_altivec_vsr((vector int)__a,
                                                 (vector int)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_srl(vector bool char __a, vector unsigned short __b) {
  return (vector bool char)__builtin_altivec_vsr((vector int)__a,
                                                 (vector int)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_srl(vector bool char __a, vector unsigned int __b) {
  return (vector bool char)__builtin_altivec_vsr((vector int)__a,
                                                 (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_srl(vector short __a,
                                                    vector unsigned char __b) {
  return (vector short)__builtin_altivec_vsr((vector int)__a, (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_srl(vector short __a,
                                                    vector unsigned short __b) {
  return (vector short)__builtin_altivec_vsr((vector int)__a, (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_srl(vector short __a,
                                                    vector unsigned int __b) {
  return (vector short)__builtin_altivec_vsr((vector int)__a, (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_srl(vector unsigned short __a, vector unsigned char __b) {
  return (vector unsigned short)__builtin_altivec_vsr((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_srl(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)__builtin_altivec_vsr((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_srl(vector unsigned short __a, vector unsigned int __b) {
  return (vector unsigned short)__builtin_altivec_vsr((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_srl(vector bool short __a, vector unsigned char __b) {
  return (vector bool short)__builtin_altivec_vsr((vector int)__a,
                                                  (vector int)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_srl(vector bool short __a, vector unsigned short __b) {
  return (vector bool short)__builtin_altivec_vsr((vector int)__a,
                                                  (vector int)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_srl(vector bool short __a, vector unsigned int __b) {
  return (vector bool short)__builtin_altivec_vsr((vector int)__a,
                                                  (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_srl(vector pixel __a,
                                                    vector unsigned char __b) {
  return (vector pixel)__builtin_altivec_vsr((vector int)__a, (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_srl(vector pixel __a,
                                                    vector unsigned short __b) {
  return (vector pixel)__builtin_altivec_vsr((vector int)__a, (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_srl(vector pixel __a,
                                                    vector unsigned int __b) {
  return (vector pixel)__builtin_altivec_vsr((vector int)__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_srl(vector int __a,
                                                  vector unsigned char __b) {
  return (vector int)__builtin_altivec_vsr(__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_srl(vector int __a,
                                                  vector unsigned short __b) {
  return (vector int)__builtin_altivec_vsr(__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_srl(vector int __a,
                                                  vector unsigned int __b) {
  return (vector int)__builtin_altivec_vsr(__a, (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_srl(vector unsigned int __a, vector unsigned char __b) {
  return (vector unsigned int)__builtin_altivec_vsr((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_srl(vector unsigned int __a, vector unsigned short __b) {
  return (vector unsigned int)__builtin_altivec_vsr((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_srl(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)__builtin_altivec_vsr((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_srl(vector bool int __a, vector unsigned char __b) {
  return (vector bool int)__builtin_altivec_vsr((vector int)__a,
                                                (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_srl(vector bool int __a, vector unsigned short __b) {
  return (vector bool int)__builtin_altivec_vsr((vector int)__a,
                                                (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_srl(vector bool int __a, vector unsigned int __b) {
  return (vector bool int)__builtin_altivec_vsr((vector int)__a,
                                                (vector int)__b);
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_srl(vector signed long long __a, vector unsigned char __b) {
  return (vector signed long long)__builtin_altivec_vsr((vector int)__a,
                                                        (vector int)__b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_srl(vector unsigned long long __a, vector unsigned char __b) {
  return (vector unsigned long long)__builtin_altivec_vsr((vector int)__a,
                                                          (vector int)__b);
}
#endif

/* vec_vsr */

static __inline__ vector signed char __ATTRS_o_ai
vec_vsr(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_altivec_vsr((vector int)__a,
                                                   (vector int)__b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vsr(vector signed char __a, vector unsigned short __b) {
  return (vector signed char)__builtin_altivec_vsr((vector int)__a,
                                                   (vector int)__b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vsr(vector signed char __a, vector unsigned int __b) {
  return (vector signed char)__builtin_altivec_vsr((vector int)__a,
                                                   (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsr(vector unsigned char __a, vector unsigned char __b) {
  return (vector unsigned char)__builtin_altivec_vsr((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsr(vector unsigned char __a, vector unsigned short __b) {
  return (vector unsigned char)__builtin_altivec_vsr((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsr(vector unsigned char __a, vector unsigned int __b) {
  return (vector unsigned char)__builtin_altivec_vsr((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_vsr(vector bool char __a, vector unsigned char __b) {
  return (vector bool char)__builtin_altivec_vsr((vector int)__a,
                                                 (vector int)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_vsr(vector bool char __a, vector unsigned short __b) {
  return (vector bool char)__builtin_altivec_vsr((vector int)__a,
                                                 (vector int)__b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_vsr(vector bool char __a, vector unsigned int __b) {
  return (vector bool char)__builtin_altivec_vsr((vector int)__a,
                                                 (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_vsr(vector short __a,
                                                    vector unsigned char __b) {
  return (vector short)__builtin_altivec_vsr((vector int)__a, (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_vsr(vector short __a,
                                                    vector unsigned short __b) {
  return (vector short)__builtin_altivec_vsr((vector int)__a, (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_vsr(vector short __a,
                                                    vector unsigned int __b) {
  return (vector short)__builtin_altivec_vsr((vector int)__a, (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsr(vector unsigned short __a, vector unsigned char __b) {
  return (vector unsigned short)__builtin_altivec_vsr((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsr(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)__builtin_altivec_vsr((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsr(vector unsigned short __a, vector unsigned int __b) {
  return (vector unsigned short)__builtin_altivec_vsr((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vsr(vector bool short __a, vector unsigned char __b) {
  return (vector bool short)__builtin_altivec_vsr((vector int)__a,
                                                  (vector int)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vsr(vector bool short __a, vector unsigned short __b) {
  return (vector bool short)__builtin_altivec_vsr((vector int)__a,
                                                  (vector int)__b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vsr(vector bool short __a, vector unsigned int __b) {
  return (vector bool short)__builtin_altivec_vsr((vector int)__a,
                                                  (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_vsr(vector pixel __a,
                                                    vector unsigned char __b) {
  return (vector pixel)__builtin_altivec_vsr((vector int)__a, (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_vsr(vector pixel __a,
                                                    vector unsigned short __b) {
  return (vector pixel)__builtin_altivec_vsr((vector int)__a, (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_vsr(vector pixel __a,
                                                    vector unsigned int __b) {
  return (vector pixel)__builtin_altivec_vsr((vector int)__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_vsr(vector int __a,
                                                  vector unsigned char __b) {
  return (vector int)__builtin_altivec_vsr(__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_vsr(vector int __a,
                                                  vector unsigned short __b) {
  return (vector int)__builtin_altivec_vsr(__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_vsr(vector int __a,
                                                  vector unsigned int __b) {
  return (vector int)__builtin_altivec_vsr(__a, (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsr(vector unsigned int __a, vector unsigned char __b) {
  return (vector unsigned int)__builtin_altivec_vsr((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsr(vector unsigned int __a, vector unsigned short __b) {
  return (vector unsigned int)__builtin_altivec_vsr((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsr(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)__builtin_altivec_vsr((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_vsr(vector bool int __a, vector unsigned char __b) {
  return (vector bool int)__builtin_altivec_vsr((vector int)__a,
                                                (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_vsr(vector bool int __a, vector unsigned short __b) {
  return (vector bool int)__builtin_altivec_vsr((vector int)__a,
                                                (vector int)__b);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_vsr(vector bool int __a, vector unsigned int __b) {
  return (vector bool int)__builtin_altivec_vsr((vector int)__a,
                                                (vector int)__b);
}

/* vec_sro */

static __inline__ vector signed char __ATTRS_o_ai
vec_sro(vector signed char __a, vector signed char __b) {
  return (vector signed char)__builtin_altivec_vsro((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_sro(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_altivec_vsro((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_sro(vector unsigned char __a, vector signed char __b) {
  return (vector unsigned char)__builtin_altivec_vsro((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_sro(vector unsigned char __a, vector unsigned char __b) {
  return (vector unsigned char)__builtin_altivec_vsro((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_sro(vector short __a,
                                                    vector signed char __b) {
  return (vector short)__builtin_altivec_vsro((vector int)__a, (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_sro(vector short __a,
                                                    vector unsigned char __b) {
  return (vector short)__builtin_altivec_vsro((vector int)__a, (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_sro(vector unsigned short __a, vector signed char __b) {
  return (vector unsigned short)__builtin_altivec_vsro((vector int)__a,
                                                       (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_sro(vector unsigned short __a, vector unsigned char __b) {
  return (vector unsigned short)__builtin_altivec_vsro((vector int)__a,
                                                       (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_sro(vector pixel __a,
                                                    vector signed char __b) {
  return (vector pixel)__builtin_altivec_vsro((vector int)__a, (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_sro(vector pixel __a,
                                                    vector unsigned char __b) {
  return (vector pixel)__builtin_altivec_vsro((vector int)__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_sro(vector int __a,
                                                  vector signed char __b) {
  return (vector int)__builtin_altivec_vsro(__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_sro(vector int __a,
                                                  vector unsigned char __b) {
  return (vector int)__builtin_altivec_vsro(__a, (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_sro(vector unsigned int __a, vector signed char __b) {
  return (vector unsigned int)__builtin_altivec_vsro((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_sro(vector unsigned int __a, vector unsigned char __b) {
  return (vector unsigned int)__builtin_altivec_vsro((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector float __ATTRS_o_ai vec_sro(vector float __a,
                                                    vector signed char __b) {
  return (vector float)__builtin_altivec_vsro((vector int)__a, (vector int)__b);
}

static __inline__ vector float __ATTRS_o_ai vec_sro(vector float __a,
                                                    vector unsigned char __b) {
  return (vector float)__builtin_altivec_vsro((vector int)__a, (vector int)__b);
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_sro(vector signed long long __a, vector signed char __b) {
  return (vector signed long long)__builtin_altivec_vsro((vector int)__a,
                                                         (vector int)__b);
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_sro(vector signed long long __a, vector unsigned char __b) {
  return (vector signed long long)__builtin_altivec_vsro((vector int)__a,
                                                         (vector int)__b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_sro(vector unsigned long long __a, vector signed char __b) {
  return (vector unsigned long long)__builtin_altivec_vsro((vector int)__a,
                                                           (vector int)__b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_sro(vector unsigned long long __a, vector unsigned char __b) {
  return (vector unsigned long long)__builtin_altivec_vsro((vector int)__a,
                                                           (vector int)__b);
}
#endif

/* vec_vsro */

static __inline__ vector signed char __ATTRS_o_ai
vec_vsro(vector signed char __a, vector signed char __b) {
  return (vector signed char)__builtin_altivec_vsro((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vsro(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_altivec_vsro((vector int)__a,
                                                    (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsro(vector unsigned char __a, vector signed char __b) {
  return (vector unsigned char)__builtin_altivec_vsro((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsro(vector unsigned char __a, vector unsigned char __b) {
  return (vector unsigned char)__builtin_altivec_vsro((vector int)__a,
                                                      (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_vsro(vector short __a,
                                                     vector signed char __b) {
  return (vector short)__builtin_altivec_vsro((vector int)__a, (vector int)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_vsro(vector short __a,
                                                     vector unsigned char __b) {
  return (vector short)__builtin_altivec_vsro((vector int)__a, (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsro(vector unsigned short __a, vector signed char __b) {
  return (vector unsigned short)__builtin_altivec_vsro((vector int)__a,
                                                       (vector int)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsro(vector unsigned short __a, vector unsigned char __b) {
  return (vector unsigned short)__builtin_altivec_vsro((vector int)__a,
                                                       (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_vsro(vector pixel __a,
                                                     vector signed char __b) {
  return (vector pixel)__builtin_altivec_vsro((vector int)__a, (vector int)__b);
}

static __inline__ vector pixel __ATTRS_o_ai vec_vsro(vector pixel __a,
                                                     vector unsigned char __b) {
  return (vector pixel)__builtin_altivec_vsro((vector int)__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_vsro(vector int __a,
                                                   vector signed char __b) {
  return (vector int)__builtin_altivec_vsro(__a, (vector int)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_vsro(vector int __a,
                                                   vector unsigned char __b) {
  return (vector int)__builtin_altivec_vsro(__a, (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsro(vector unsigned int __a, vector signed char __b) {
  return (vector unsigned int)__builtin_altivec_vsro((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsro(vector unsigned int __a, vector unsigned char __b) {
  return (vector unsigned int)__builtin_altivec_vsro((vector int)__a,
                                                     (vector int)__b);
}

static __inline__ vector float __ATTRS_o_ai vec_vsro(vector float __a,
                                                     vector signed char __b) {
  return (vector float)__builtin_altivec_vsro((vector int)__a, (vector int)__b);
}

static __inline__ vector float __ATTRS_o_ai vec_vsro(vector float __a,
                                                     vector unsigned char __b) {
  return (vector float)__builtin_altivec_vsro((vector int)__a, (vector int)__b);
}

/* vec_st */

static __inline__ void __ATTRS_o_ai vec_st(vector signed char __a, int __b,
                                           vector signed char *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector signed char __a, int __b,
                                           signed char *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector unsigned char __a, int __b,
                                           vector unsigned char *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector unsigned char __a, int __b,
                                           unsigned char *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector bool char __a, int __b,
                                           signed char *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector bool char __a, int __b,
                                           unsigned char *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector bool char __a, int __b,
                                           vector bool char *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector short __a, int __b,
                                           vector short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector short __a, int __b,
                                           short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector unsigned short __a, int __b,
                                           vector unsigned short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector unsigned short __a, int __b,
                                           unsigned short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector bool short __a, int __b,
                                           short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector bool short __a, int __b,
                                           unsigned short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector bool short __a, int __b,
                                           vector bool short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector pixel __a, int __b,
                                           short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector pixel __a, int __b,
                                           unsigned short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector pixel __a, int __b,
                                           vector pixel *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector int __a, int __b,
                                           vector int *__c) {
  __builtin_altivec_stvx(__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector int __a, int __b, int *__c) {
  __builtin_altivec_stvx(__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector unsigned int __a, int __b,
                                           vector unsigned int *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector unsigned int __a, int __b,
                                           unsigned int *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector bool int __a, int __b,
                                           int *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector bool int __a, int __b,
                                           unsigned int *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector bool int __a, int __b,
                                           vector bool int *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector float __a, int __b,
                                           vector float *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_st(vector float __a, int __b,
                                           float *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

/* vec_stvx */

static __inline__ void __ATTRS_o_ai vec_stvx(vector signed char __a, int __b,
                                             vector signed char *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector signed char __a, int __b,
                                             signed char *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector unsigned char __a, int __b,
                                             vector unsigned char *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector unsigned char __a, int __b,
                                             unsigned char *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector bool char __a, int __b,
                                             signed char *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector bool char __a, int __b,
                                             unsigned char *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector bool char __a, int __b,
                                             vector bool char *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector short __a, int __b,
                                             vector short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector short __a, int __b,
                                             short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector unsigned short __a, int __b,
                                             vector unsigned short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector unsigned short __a, int __b,
                                             unsigned short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector bool short __a, int __b,
                                             short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector bool short __a, int __b,
                                             unsigned short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector bool short __a, int __b,
                                             vector bool short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector pixel __a, int __b,
                                             short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector pixel __a, int __b,
                                             unsigned short *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector pixel __a, int __b,
                                             vector pixel *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector int __a, int __b,
                                             vector int *__c) {
  __builtin_altivec_stvx(__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector int __a, int __b,
                                             int *__c) {
  __builtin_altivec_stvx(__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector unsigned int __a, int __b,
                                             vector unsigned int *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector unsigned int __a, int __b,
                                             unsigned int *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector bool int __a, int __b,
                                             int *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector bool int __a, int __b,
                                             unsigned int *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector bool int __a, int __b,
                                             vector bool int *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector float __a, int __b,
                                             vector float *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvx(vector float __a, int __b,
                                             float *__c) {
  __builtin_altivec_stvx((vector int)__a, __b, __c);
}

/* vec_ste */

static __inline__ void __ATTRS_o_ai vec_ste(vector signed char __a, int __b,
                                            signed char *__c) {
  __builtin_altivec_stvebx((vector char)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_ste(vector unsigned char __a, int __b,
                                            unsigned char *__c) {
  __builtin_altivec_stvebx((vector char)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_ste(vector bool char __a, int __b,
                                            signed char *__c) {
  __builtin_altivec_stvebx((vector char)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_ste(vector bool char __a, int __b,
                                            unsigned char *__c) {
  __builtin_altivec_stvebx((vector char)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_ste(vector short __a, int __b,
                                            short *__c) {
  __builtin_altivec_stvehx(__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_ste(vector unsigned short __a, int __b,
                                            unsigned short *__c) {
  __builtin_altivec_stvehx((vector short)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_ste(vector bool short __a, int __b,
                                            short *__c) {
  __builtin_altivec_stvehx((vector short)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_ste(vector bool short __a, int __b,
                                            unsigned short *__c) {
  __builtin_altivec_stvehx((vector short)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_ste(vector pixel __a, int __b,
                                            short *__c) {
  __builtin_altivec_stvehx((vector short)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_ste(vector pixel __a, int __b,
                                            unsigned short *__c) {
  __builtin_altivec_stvehx((vector short)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_ste(vector int __a, int __b, int *__c) {
  __builtin_altivec_stvewx(__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_ste(vector unsigned int __a, int __b,
                                            unsigned int *__c) {
  __builtin_altivec_stvewx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_ste(vector bool int __a, int __b,
                                            int *__c) {
  __builtin_altivec_stvewx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_ste(vector bool int __a, int __b,
                                            unsigned int *__c) {
  __builtin_altivec_stvewx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_ste(vector float __a, int __b,
                                            float *__c) {
  __builtin_altivec_stvewx((vector int)__a, __b, __c);
}

/* vec_stvebx */

static __inline__ void __ATTRS_o_ai vec_stvebx(vector signed char __a, int __b,
                                               signed char *__c) {
  __builtin_altivec_stvebx((vector char)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvebx(vector unsigned char __a,
                                               int __b, unsigned char *__c) {
  __builtin_altivec_stvebx((vector char)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvebx(vector bool char __a, int __b,
                                               signed char *__c) {
  __builtin_altivec_stvebx((vector char)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvebx(vector bool char __a, int __b,
                                               unsigned char *__c) {
  __builtin_altivec_stvebx((vector char)__a, __b, __c);
}

/* vec_stvehx */

static __inline__ void __ATTRS_o_ai vec_stvehx(vector short __a, int __b,
                                               short *__c) {
  __builtin_altivec_stvehx(__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvehx(vector unsigned short __a,
                                               int __b, unsigned short *__c) {
  __builtin_altivec_stvehx((vector short)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvehx(vector bool short __a, int __b,
                                               short *__c) {
  __builtin_altivec_stvehx((vector short)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvehx(vector bool short __a, int __b,
                                               unsigned short *__c) {
  __builtin_altivec_stvehx((vector short)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvehx(vector pixel __a, int __b,
                                               short *__c) {
  __builtin_altivec_stvehx((vector short)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvehx(vector pixel __a, int __b,
                                               unsigned short *__c) {
  __builtin_altivec_stvehx((vector short)__a, __b, __c);
}

/* vec_stvewx */

static __inline__ void __ATTRS_o_ai vec_stvewx(vector int __a, int __b,
                                               int *__c) {
  __builtin_altivec_stvewx(__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvewx(vector unsigned int __a, int __b,
                                               unsigned int *__c) {
  __builtin_altivec_stvewx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvewx(vector bool int __a, int __b,
                                               int *__c) {
  __builtin_altivec_stvewx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvewx(vector bool int __a, int __b,
                                               unsigned int *__c) {
  __builtin_altivec_stvewx((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvewx(vector float __a, int __b,
                                               float *__c) {
  __builtin_altivec_stvewx((vector int)__a, __b, __c);
}

/* vec_stl */

static __inline__ void __ATTRS_o_ai vec_stl(vector signed char __a, int __b,
                                            vector signed char *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector signed char __a, int __b,
                                            signed char *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector unsigned char __a, int __b,
                                            vector unsigned char *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector unsigned char __a, int __b,
                                            unsigned char *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector bool char __a, int __b,
                                            signed char *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector bool char __a, int __b,
                                            unsigned char *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector bool char __a, int __b,
                                            vector bool char *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector short __a, int __b,
                                            vector short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector short __a, int __b,
                                            short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector unsigned short __a, int __b,
                                            vector unsigned short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector unsigned short __a, int __b,
                                            unsigned short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector bool short __a, int __b,
                                            short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector bool short __a, int __b,
                                            unsigned short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector bool short __a, int __b,
                                            vector bool short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector pixel __a, int __b,
                                            short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector pixel __a, int __b,
                                            unsigned short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector pixel __a, int __b,
                                            vector pixel *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector int __a, int __b,
                                            vector int *__c) {
  __builtin_altivec_stvxl(__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector int __a, int __b, int *__c) {
  __builtin_altivec_stvxl(__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector unsigned int __a, int __b,
                                            vector unsigned int *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector unsigned int __a, int __b,
                                            unsigned int *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector bool int __a, int __b,
                                            int *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector bool int __a, int __b,
                                            unsigned int *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector bool int __a, int __b,
                                            vector bool int *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector float __a, int __b,
                                            vector float *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stl(vector float __a, int __b,
                                            float *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

/* vec_stvxl */

static __inline__ void __ATTRS_o_ai vec_stvxl(vector signed char __a, int __b,
                                              vector signed char *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector signed char __a, int __b,
                                              signed char *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector unsigned char __a, int __b,
                                              vector unsigned char *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector unsigned char __a, int __b,
                                              unsigned char *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector bool char __a, int __b,
                                              signed char *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector bool char __a, int __b,
                                              unsigned char *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector bool char __a, int __b,
                                              vector bool char *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector short __a, int __b,
                                              vector short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector short __a, int __b,
                                              short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector unsigned short __a,
                                              int __b,
                                              vector unsigned short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector unsigned short __a,
                                              int __b, unsigned short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector bool short __a, int __b,
                                              short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector bool short __a, int __b,
                                              unsigned short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector bool short __a, int __b,
                                              vector bool short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector pixel __a, int __b,
                                              short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector pixel __a, int __b,
                                              unsigned short *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector pixel __a, int __b,
                                              vector pixel *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector int __a, int __b,
                                              vector int *__c) {
  __builtin_altivec_stvxl(__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector int __a, int __b,
                                              int *__c) {
  __builtin_altivec_stvxl(__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector unsigned int __a, int __b,
                                              vector unsigned int *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector unsigned int __a, int __b,
                                              unsigned int *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector bool int __a, int __b,
                                              int *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector bool int __a, int __b,
                                              unsigned int *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector bool int __a, int __b,
                                              vector bool int *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector float __a, int __b,
                                              vector float *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvxl(vector float __a, int __b,
                                              float *__c) {
  __builtin_altivec_stvxl((vector int)__a, __b, __c);
}

/* vec_sub */

static __inline__ vector signed char __ATTRS_o_ai
vec_sub(vector signed char __a, vector signed char __b) {
  return __a - __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_sub(vector bool char __a, vector signed char __b) {
  return (vector signed char)__a - __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_sub(vector signed char __a, vector bool char __b) {
  return __a - (vector signed char)__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_sub(vector unsigned char __a, vector unsigned char __b) {
  return __a - __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_sub(vector bool char __a, vector unsigned char __b) {
  return (vector unsigned char)__a - __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_sub(vector unsigned char __a, vector bool char __b) {
  return __a - (vector unsigned char)__b;
}

static __inline__ vector short __ATTRS_o_ai vec_sub(vector short __a,
                                                    vector short __b) {
  return __a - __b;
}

static __inline__ vector short __ATTRS_o_ai vec_sub(vector bool short __a,
                                                    vector short __b) {
  return (vector short)__a - __b;
}

static __inline__ vector short __ATTRS_o_ai vec_sub(vector short __a,
                                                    vector bool short __b) {
  return __a - (vector short)__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_sub(vector unsigned short __a, vector unsigned short __b) {
  return __a - __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_sub(vector bool short __a, vector unsigned short __b) {
  return (vector unsigned short)__a - __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_sub(vector unsigned short __a, vector bool short __b) {
  return __a - (vector unsigned short)__b;
}

static __inline__ vector int __ATTRS_o_ai vec_sub(vector int __a,
                                                  vector int __b) {
  return __a - __b;
}

static __inline__ vector int __ATTRS_o_ai vec_sub(vector bool int __a,
                                                  vector int __b) {
  return (vector int)__a - __b;
}

static __inline__ vector int __ATTRS_o_ai vec_sub(vector int __a,
                                                  vector bool int __b) {
  return __a - (vector int)__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_sub(vector unsigned int __a, vector unsigned int __b) {
  return __a - __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_sub(vector bool int __a, vector unsigned int __b) {
  return (vector unsigned int)__a - __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_sub(vector unsigned int __a, vector bool int __b) {
  return __a - (vector unsigned int)__b;
}

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
static __inline__ vector signed __int128 __ATTRS_o_ai
vec_sub(vector signed __int128 __a, vector signed __int128 __b) {
  return __a - __b;
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_sub(vector unsigned __int128 __a, vector unsigned __int128 __b) {
  return __a - __b;
}
#endif // defined(__POWER8_VECTOR__) && defined(__powerpc64__)

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_sub(vector signed long long __a, vector signed long long __b) {
  return __a - __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_sub(vector unsigned long long __a, vector unsigned long long __b) {
  return __a - __b;
}

static __inline__ vector double __ATTRS_o_ai vec_sub(vector double __a,
                                                     vector double __b) {
  return __a - __b;
}
#endif

static __inline__ vector float __ATTRS_o_ai vec_sub(vector float __a,
                                                    vector float __b) {
  return __a - __b;
}

/* vec_vsububm */

#define __builtin_altivec_vsububm vec_vsububm

static __inline__ vector signed char __ATTRS_o_ai
vec_vsububm(vector signed char __a, vector signed char __b) {
  return __a - __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vsububm(vector bool char __a, vector signed char __b) {
  return (vector signed char)__a - __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vsububm(vector signed char __a, vector bool char __b) {
  return __a - (vector signed char)__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsububm(vector unsigned char __a, vector unsigned char __b) {
  return __a - __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsububm(vector bool char __a, vector unsigned char __b) {
  return (vector unsigned char)__a - __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsububm(vector unsigned char __a, vector bool char __b) {
  return __a - (vector unsigned char)__b;
}

/* vec_vsubuhm */

#define __builtin_altivec_vsubuhm vec_vsubuhm

static __inline__ vector short __ATTRS_o_ai vec_vsubuhm(vector short __a,
                                                        vector short __b) {
  return __a - __b;
}

static __inline__ vector short __ATTRS_o_ai vec_vsubuhm(vector bool short __a,
                                                        vector short __b) {
  return (vector short)__a - __b;
}

static __inline__ vector short __ATTRS_o_ai vec_vsubuhm(vector short __a,
                                                        vector bool short __b) {
  return __a - (vector short)__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsubuhm(vector unsigned short __a, vector unsigned short __b) {
  return __a - __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsubuhm(vector bool short __a, vector unsigned short __b) {
  return (vector unsigned short)__a - __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsubuhm(vector unsigned short __a, vector bool short __b) {
  return __a - (vector unsigned short)__b;
}

/* vec_vsubuwm */

#define __builtin_altivec_vsubuwm vec_vsubuwm

static __inline__ vector int __ATTRS_o_ai vec_vsubuwm(vector int __a,
                                                      vector int __b) {
  return __a - __b;
}

static __inline__ vector int __ATTRS_o_ai vec_vsubuwm(vector bool int __a,
                                                      vector int __b) {
  return (vector int)__a - __b;
}

static __inline__ vector int __ATTRS_o_ai vec_vsubuwm(vector int __a,
                                                      vector bool int __b) {
  return __a - (vector int)__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsubuwm(vector unsigned int __a, vector unsigned int __b) {
  return __a - __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsubuwm(vector bool int __a, vector unsigned int __b) {
  return (vector unsigned int)__a - __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsubuwm(vector unsigned int __a, vector bool int __b) {
  return __a - (vector unsigned int)__b;
}

/* vec_vsubfp */

#define __builtin_altivec_vsubfp vec_vsubfp

static __inline__ vector float __attribute__((__always_inline__))
vec_vsubfp(vector float __a, vector float __b) {
  return __a - __b;
}

/* vec_subc */

static __inline__ vector signed int __ATTRS_o_ai
vec_subc(vector signed int __a, vector signed int __b) {
  return (vector signed int)__builtin_altivec_vsubcuw((vector unsigned int)__a,
                                                      (vector unsigned int) __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_subc(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_altivec_vsubcuw(__a, __b);
}

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_subc(vector unsigned __int128 __a, vector unsigned __int128 __b) {
  return __builtin_altivec_vsubcuq(__a, __b);
}

static __inline__ vector signed __int128 __ATTRS_o_ai
vec_subc(vector signed __int128 __a, vector signed __int128 __b) {
  return __builtin_altivec_vsubcuq(__a, __b);
}
#endif // defined(__POWER8_VECTOR__) && defined(__powerpc64__)

/* vec_vsubcuw */

static __inline__ vector unsigned int __attribute__((__always_inline__))
vec_vsubcuw(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_altivec_vsubcuw(__a, __b);
}

/* vec_subs */

static __inline__ vector signed char __ATTRS_o_ai
vec_subs(vector signed char __a, vector signed char __b) {
  return __builtin_altivec_vsubsbs(__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_subs(vector bool char __a, vector signed char __b) {
  return __builtin_altivec_vsubsbs((vector signed char)__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_subs(vector signed char __a, vector bool char __b) {
  return __builtin_altivec_vsubsbs(__a, (vector signed char)__b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_subs(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_altivec_vsububs(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_subs(vector bool char __a, vector unsigned char __b) {
  return __builtin_altivec_vsububs((vector unsigned char)__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_subs(vector unsigned char __a, vector bool char __b) {
  return __builtin_altivec_vsububs(__a, (vector unsigned char)__b);
}

static __inline__ vector short __ATTRS_o_ai vec_subs(vector short __a,
                                                     vector short __b) {
  return __builtin_altivec_vsubshs(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_subs(vector bool short __a,
                                                     vector short __b) {
  return __builtin_altivec_vsubshs((vector short)__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_subs(vector short __a,
                                                     vector bool short __b) {
  return __builtin_altivec_vsubshs(__a, (vector short)__b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_subs(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_altivec_vsubuhs(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_subs(vector bool short __a, vector unsigned short __b) {
  return __builtin_altivec_vsubuhs((vector unsigned short)__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_subs(vector unsigned short __a, vector bool short __b) {
  return __builtin_altivec_vsubuhs(__a, (vector unsigned short)__b);
}

static __inline__ vector int __ATTRS_o_ai vec_subs(vector int __a,
                                                   vector int __b) {
  return __builtin_altivec_vsubsws(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_subs(vector bool int __a,
                                                   vector int __b) {
  return __builtin_altivec_vsubsws((vector int)__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_subs(vector int __a,
                                                   vector bool int __b) {
  return __builtin_altivec_vsubsws(__a, (vector int)__b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_subs(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_altivec_vsubuws(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_subs(vector bool int __a, vector unsigned int __b) {
  return __builtin_altivec_vsubuws((vector unsigned int)__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_subs(vector unsigned int __a, vector bool int __b) {
  return __builtin_altivec_vsubuws(__a, (vector unsigned int)__b);
}

/* vec_vsubsbs */

static __inline__ vector signed char __ATTRS_o_ai
vec_vsubsbs(vector signed char __a, vector signed char __b) {
  return __builtin_altivec_vsubsbs(__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vsubsbs(vector bool char __a, vector signed char __b) {
  return __builtin_altivec_vsubsbs((vector signed char)__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vsubsbs(vector signed char __a, vector bool char __b) {
  return __builtin_altivec_vsubsbs(__a, (vector signed char)__b);
}

/* vec_vsububs */

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsububs(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_altivec_vsububs(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsububs(vector bool char __a, vector unsigned char __b) {
  return __builtin_altivec_vsububs((vector unsigned char)__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsububs(vector unsigned char __a, vector bool char __b) {
  return __builtin_altivec_vsububs(__a, (vector unsigned char)__b);
}

/* vec_vsubshs */

static __inline__ vector short __ATTRS_o_ai vec_vsubshs(vector short __a,
                                                        vector short __b) {
  return __builtin_altivec_vsubshs(__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_vsubshs(vector bool short __a,
                                                        vector short __b) {
  return __builtin_altivec_vsubshs((vector short)__a, __b);
}

static __inline__ vector short __ATTRS_o_ai vec_vsubshs(vector short __a,
                                                        vector bool short __b) {
  return __builtin_altivec_vsubshs(__a, (vector short)__b);
}

/* vec_vsubuhs */

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsubuhs(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_altivec_vsubuhs(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsubuhs(vector bool short __a, vector unsigned short __b) {
  return __builtin_altivec_vsubuhs((vector unsigned short)__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsubuhs(vector unsigned short __a, vector bool short __b) {
  return __builtin_altivec_vsubuhs(__a, (vector unsigned short)__b);
}

/* vec_vsubsws */

static __inline__ vector int __ATTRS_o_ai vec_vsubsws(vector int __a,
                                                      vector int __b) {
  return __builtin_altivec_vsubsws(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_vsubsws(vector bool int __a,
                                                      vector int __b) {
  return __builtin_altivec_vsubsws((vector int)__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_vsubsws(vector int __a,
                                                      vector bool int __b) {
  return __builtin_altivec_vsubsws(__a, (vector int)__b);
}

/* vec_vsubuws */

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsubuws(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_altivec_vsubuws(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsubuws(vector bool int __a, vector unsigned int __b) {
  return __builtin_altivec_vsubuws((vector unsigned int)__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsubuws(vector unsigned int __a, vector bool int __b) {
  return __builtin_altivec_vsubuws(__a, (vector unsigned int)__b);
}

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
/* vec_vsubuqm */

static __inline__ vector signed __int128 __ATTRS_o_ai
vec_vsubuqm(vector signed __int128 __a, vector signed __int128 __b) {
  return __a - __b;
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_vsubuqm(vector unsigned __int128 __a, vector unsigned __int128 __b) {
  return __a - __b;
}

/* vec_vsubeuqm */


static __inline__ vector signed __int128 __ATTRS_o_ai
vec_vsubeuqm(vector signed __int128 __a, vector signed __int128 __b,
             vector signed __int128 __c) {
  return __builtin_altivec_vsubeuqm(__a, __b, __c);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_vsubeuqm(vector unsigned __int128 __a, vector unsigned __int128 __b,
             vector unsigned __int128 __c) {
  return __builtin_altivec_vsubeuqm(__a, __b, __c);
}

static __inline__ vector signed __int128 __ATTRS_o_ai
vec_sube(vector signed __int128 __a, vector signed __int128 __b,
             vector signed __int128 __c) {
  return __builtin_altivec_vsubeuqm(__a, __b, __c);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_sube(vector unsigned __int128 __a, vector unsigned __int128 __b,
             vector unsigned __int128 __c) {
  return __builtin_altivec_vsubeuqm(__a, __b, __c);
}

/* vec_vsubcuq */

static __inline__ vector signed __int128 __ATTRS_o_ai
vec_vsubcuq(vector signed __int128 __a, vector signed __int128 __b) {
  return __builtin_altivec_vsubcuq(__a, __b);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_vsubcuq(vector unsigned __int128 __a, vector unsigned __int128 __b) {
  return __builtin_altivec_vsubcuq(__a, __b);
}

/* vec_vsubecuq */

static __inline__ vector signed __int128 __ATTRS_o_ai
vec_vsubecuq(vector signed __int128 __a, vector signed __int128 __b,
             vector signed __int128 __c) {
  return __builtin_altivec_vsubecuq(__a, __b, __c);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_vsubecuq(vector unsigned __int128 __a, vector unsigned __int128 __b,
             vector unsigned __int128 __c) {
  return __builtin_altivec_vsubecuq(__a, __b, __c);
}

static __inline__ vector signed int __ATTRS_o_ai
vec_subec(vector signed int __a, vector signed int __b,
             vector signed int __c) {
  return vec_addec(__a, ~__b, __c);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_subec(vector unsigned int __a, vector unsigned int __b,
             vector unsigned int __c) {
  return vec_addec(__a, ~__b, __c);
}

static __inline__ vector signed __int128 __ATTRS_o_ai
vec_subec(vector signed __int128 __a, vector signed __int128 __b,
             vector signed __int128 __c) {
  return __builtin_altivec_vsubecuq(__a, __b, __c);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_subec(vector unsigned __int128 __a, vector unsigned __int128 __b,
             vector unsigned __int128 __c) {
  return __builtin_altivec_vsubecuq(__a, __b, __c);
}
#endif // defined(__POWER8_VECTOR__) && defined(__powerpc64__)

static __inline__ vector signed int __ATTRS_o_ai
vec_sube(vector signed int __a, vector signed int __b,
         vector signed int __c) {
  vector signed int __mask = {1, 1, 1, 1};
  vector signed int __carry = __c & __mask;
  return vec_adde(__a, ~__b, __carry);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_sube(vector unsigned int __a, vector unsigned int __b,
         vector unsigned int __c) {
  vector unsigned int __mask = {1, 1, 1, 1};
  vector unsigned int __carry = __c & __mask;
  return vec_adde(__a, ~__b, __carry);
}
/* vec_sum4s */

static __inline__ vector int __ATTRS_o_ai vec_sum4s(vector signed char __a,
                                                    vector int __b) {
  return __builtin_altivec_vsum4sbs(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_sum4s(vector unsigned char __a, vector unsigned int __b) {
  return __builtin_altivec_vsum4ubs(__a, __b);
}

static __inline__ vector int __ATTRS_o_ai vec_sum4s(vector signed short __a,
                                                    vector int __b) {
  return __builtin_altivec_vsum4shs(__a, __b);
}

/* vec_vsum4sbs */

static __inline__ vector int __attribute__((__always_inline__))
vec_vsum4sbs(vector signed char __a, vector int __b) {
  return __builtin_altivec_vsum4sbs(__a, __b);
}

/* vec_vsum4ubs */

static __inline__ vector unsigned int __attribute__((__always_inline__))
vec_vsum4ubs(vector unsigned char __a, vector unsigned int __b) {
  return __builtin_altivec_vsum4ubs(__a, __b);
}

/* vec_vsum4shs */

static __inline__ vector int __attribute__((__always_inline__))
vec_vsum4shs(vector signed short __a, vector int __b) {
  return __builtin_altivec_vsum4shs(__a, __b);
}

/* vec_sum2s */

/* The vsum2sws instruction has a big-endian bias, so that the second
   input vector and the result always reference big-endian elements
   1 and 3 (little-endian element 0 and 2).  For ease of porting the
   programmer wants elements 1 and 3 in both cases, so for little
   endian we must perform some permutes.  */

static __inline__ vector signed int __attribute__((__always_inline__))
vec_sum2s(vector int __a, vector int __b) {
#ifdef __LITTLE_ENDIAN__
  vector int __c = (vector signed int)vec_perm(
      __b, __b, (vector unsigned char)(4, 5, 6, 7, 0, 1, 2, 3, 12, 13, 14, 15,
                                       8, 9, 10, 11));
  __c = __builtin_altivec_vsum2sws(__a, __c);
  return (vector signed int)vec_perm(
      __c, __c, (vector unsigned char)(4, 5, 6, 7, 0, 1, 2, 3, 12, 13, 14, 15,
                                       8, 9, 10, 11));
#else
  return __builtin_altivec_vsum2sws(__a, __b);
#endif
}

/* vec_vsum2sws */

static __inline__ vector signed int __attribute__((__always_inline__))
vec_vsum2sws(vector int __a, vector int __b) {
#ifdef __LITTLE_ENDIAN__
  vector int __c = (vector signed int)vec_perm(
      __b, __b, (vector unsigned char)(4, 5, 6, 7, 0, 1, 2, 3, 12, 13, 14, 15,
                                       8, 9, 10, 11));
  __c = __builtin_altivec_vsum2sws(__a, __c);
  return (vector signed int)vec_perm(
      __c, __c, (vector unsigned char)(4, 5, 6, 7, 0, 1, 2, 3, 12, 13, 14, 15,
                                       8, 9, 10, 11));
#else
  return __builtin_altivec_vsum2sws(__a, __b);
#endif
}

/* vec_sums */

/* The vsumsws instruction has a big-endian bias, so that the second
   input vector and the result always reference big-endian element 3
   (little-endian element 0).  For ease of porting the programmer
   wants element 3 in both cases, so for little endian we must perform
   some permutes.  */

static __inline__ vector signed int __attribute__((__always_inline__))
vec_sums(vector signed int __a, vector signed int __b) {
#ifdef __LITTLE_ENDIAN__
  __b = (vector signed int)vec_splat(__b, 3);
  __b = __builtin_altivec_vsumsws(__a, __b);
  return (vector signed int)(0, 0, 0, __b[0]);
#else
  return __builtin_altivec_vsumsws(__a, __b);
#endif
}

/* vec_vsumsws */

static __inline__ vector signed int __attribute__((__always_inline__))
vec_vsumsws(vector signed int __a, vector signed int __b) {
#ifdef __LITTLE_ENDIAN__
  __b = (vector signed int)vec_splat(__b, 3);
  __b = __builtin_altivec_vsumsws(__a, __b);
  return (vector signed int)(0, 0, 0, __b[0]);
#else
  return __builtin_altivec_vsumsws(__a, __b);
#endif
}

/* vec_trunc */

static __inline__ vector float __ATTRS_o_ai vec_trunc(vector float __a) {
#ifdef __VSX__
  return __builtin_vsx_xvrspiz(__a);
#else
  return __builtin_altivec_vrfiz(__a);
#endif
}

#ifdef __VSX__
static __inline__ vector double __ATTRS_o_ai vec_trunc(vector double __a) {
  return __builtin_vsx_xvrdpiz(__a);
}
#endif

/* vec_vrfiz */

static __inline__ vector float __attribute__((__always_inline__))
vec_vrfiz(vector float __a) {
  return __builtin_altivec_vrfiz(__a);
}

/* vec_unpackh */

/* The vector unpack instructions all have a big-endian bias, so for
   little endian we must reverse the meanings of "high" and "low."  */

static __inline__ vector short __ATTRS_o_ai
vec_unpackh(vector signed char __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vupklsb((vector char)__a);
#else
  return __builtin_altivec_vupkhsb((vector char)__a);
#endif
}

static __inline__ vector bool short __ATTRS_o_ai
vec_unpackh(vector bool char __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool short)__builtin_altivec_vupklsb((vector char)__a);
#else
  return (vector bool short)__builtin_altivec_vupkhsb((vector char)__a);
#endif
}

static __inline__ vector int __ATTRS_o_ai vec_unpackh(vector short __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vupklsh(__a);
#else
  return __builtin_altivec_vupkhsh(__a);
#endif
}

static __inline__ vector bool int __ATTRS_o_ai
vec_unpackh(vector bool short __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool int)__builtin_altivec_vupklsh((vector short)__a);
#else
  return (vector bool int)__builtin_altivec_vupkhsh((vector short)__a);
#endif
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_unpackh(vector pixel __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector unsigned int)__builtin_altivec_vupklpx((vector short)__a);
#else
  return (vector unsigned int)__builtin_altivec_vupkhpx((vector short)__a);
#endif
}

#ifdef __POWER8_VECTOR__
static __inline__ vector long long __ATTRS_o_ai vec_unpackh(vector int __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vupklsw(__a);
#else
  return __builtin_altivec_vupkhsw(__a);
#endif
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_unpackh(vector bool int __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool long long)__builtin_altivec_vupklsw((vector int)__a);
#else
  return (vector bool long long)__builtin_altivec_vupkhsw((vector int)__a);
#endif
}

static __inline__ vector double __ATTRS_o_ai
vec_unpackh(vector float __a) {
  return (vector double)(__a[0], __a[1]);
}
#endif

/* vec_vupkhsb */

static __inline__ vector short __ATTRS_o_ai
vec_vupkhsb(vector signed char __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vupklsb((vector char)__a);
#else
  return __builtin_altivec_vupkhsb((vector char)__a);
#endif
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vupkhsb(vector bool char __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool short)__builtin_altivec_vupklsb((vector char)__a);
#else
  return (vector bool short)__builtin_altivec_vupkhsb((vector char)__a);
#endif
}

/* vec_vupkhsh */

static __inline__ vector int __ATTRS_o_ai vec_vupkhsh(vector short __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vupklsh(__a);
#else
  return __builtin_altivec_vupkhsh(__a);
#endif
}

static __inline__ vector bool int __ATTRS_o_ai
vec_vupkhsh(vector bool short __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool int)__builtin_altivec_vupklsh((vector short)__a);
#else
  return (vector bool int)__builtin_altivec_vupkhsh((vector short)__a);
#endif
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vupkhsh(vector pixel __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector unsigned int)__builtin_altivec_vupklpx((vector short)__a);
#else
  return (vector unsigned int)__builtin_altivec_vupkhpx((vector short)__a);
#endif
}

/* vec_vupkhsw */

#ifdef __POWER8_VECTOR__
static __inline__ vector long long __ATTRS_o_ai vec_vupkhsw(vector int __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vupklsw(__a);
#else
  return __builtin_altivec_vupkhsw(__a);
#endif
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_vupkhsw(vector bool int __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool long long)__builtin_altivec_vupklsw((vector int)__a);
#else
  return (vector bool long long)__builtin_altivec_vupkhsw((vector int)__a);
#endif
}
#endif

/* vec_unpackl */

static __inline__ vector short __ATTRS_o_ai
vec_unpackl(vector signed char __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vupkhsb((vector char)__a);
#else
  return __builtin_altivec_vupklsb((vector char)__a);
#endif
}

static __inline__ vector bool short __ATTRS_o_ai
vec_unpackl(vector bool char __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool short)__builtin_altivec_vupkhsb((vector char)__a);
#else
  return (vector bool short)__builtin_altivec_vupklsb((vector char)__a);
#endif
}

static __inline__ vector int __ATTRS_o_ai vec_unpackl(vector short __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vupkhsh(__a);
#else
  return __builtin_altivec_vupklsh(__a);
#endif
}

static __inline__ vector bool int __ATTRS_o_ai
vec_unpackl(vector bool short __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool int)__builtin_altivec_vupkhsh((vector short)__a);
#else
  return (vector bool int)__builtin_altivec_vupklsh((vector short)__a);
#endif
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_unpackl(vector pixel __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector unsigned int)__builtin_altivec_vupkhpx((vector short)__a);
#else
  return (vector unsigned int)__builtin_altivec_vupklpx((vector short)__a);
#endif
}

#ifdef __POWER8_VECTOR__
static __inline__ vector long long __ATTRS_o_ai vec_unpackl(vector int __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vupkhsw(__a);
#else
  return __builtin_altivec_vupklsw(__a);
#endif
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_unpackl(vector bool int __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool long long)__builtin_altivec_vupkhsw((vector int)__a);
#else
  return (vector bool long long)__builtin_altivec_vupklsw((vector int)__a);
#endif
}

static __inline__ vector double __ATTRS_o_ai
vec_unpackl(vector float __a) {
  return (vector double)(__a[2], __a[3]);
}
#endif

/* vec_vupklsb */

static __inline__ vector short __ATTRS_o_ai
vec_vupklsb(vector signed char __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vupkhsb((vector char)__a);
#else
  return __builtin_altivec_vupklsb((vector char)__a);
#endif
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vupklsb(vector bool char __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool short)__builtin_altivec_vupkhsb((vector char)__a);
#else
  return (vector bool short)__builtin_altivec_vupklsb((vector char)__a);
#endif
}

/* vec_vupklsh */

static __inline__ vector int __ATTRS_o_ai vec_vupklsh(vector short __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vupkhsh(__a);
#else
  return __builtin_altivec_vupklsh(__a);
#endif
}

static __inline__ vector bool int __ATTRS_o_ai
vec_vupklsh(vector bool short __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool int)__builtin_altivec_vupkhsh((vector short)__a);
#else
  return (vector bool int)__builtin_altivec_vupklsh((vector short)__a);
#endif
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vupklsh(vector pixel __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector unsigned int)__builtin_altivec_vupkhpx((vector short)__a);
#else
  return (vector unsigned int)__builtin_altivec_vupklpx((vector short)__a);
#endif
}

/* vec_vupklsw */

#ifdef __POWER8_VECTOR__
static __inline__ vector long long __ATTRS_o_ai vec_vupklsw(vector int __a) {
#ifdef __LITTLE_ENDIAN__
  return __builtin_altivec_vupkhsw(__a);
#else
  return __builtin_altivec_vupklsw(__a);
#endif
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_vupklsw(vector bool int __a) {
#ifdef __LITTLE_ENDIAN__
  return (vector bool long long)__builtin_altivec_vupkhsw((vector int)__a);
#else
  return (vector bool long long)__builtin_altivec_vupklsw((vector int)__a);
#endif
}
#endif

/* vec_vsx_ld */

#ifdef __VSX__

static __inline__ vector bool int __ATTRS_o_ai
vec_vsx_ld(int __a, const vector bool int *__b) {
  return (vector bool int)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector signed int __ATTRS_o_ai
vec_vsx_ld(int __a, const vector signed int *__b) {
  return (vector signed int)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector signed int __ATTRS_o_ai
vec_vsx_ld(int __a, const signed int *__b) {
  return (vector signed int)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsx_ld(int __a, const vector unsigned int *__b) {
  return (vector unsigned int)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vsx_ld(int __a, const unsigned int *__b) {
  return (vector unsigned int)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector float __ATTRS_o_ai
vec_vsx_ld(int __a, const vector float *__b) {
  return (vector float)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector float __ATTRS_o_ai vec_vsx_ld(int __a,
                                                       const float *__b) {
  return (vector float)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_vsx_ld(int __a, const vector signed long long *__b) {
  return (vector signed long long)__builtin_vsx_lxvd2x(__a, __b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_vsx_ld(int __a, const vector unsigned long long *__b) {
  return (vector unsigned long long)__builtin_vsx_lxvd2x(__a, __b);
}

static __inline__ vector double __ATTRS_o_ai
vec_vsx_ld(int __a, const vector double *__b) {
  return (vector double)__builtin_vsx_lxvd2x(__a, __b);
}

static __inline__ vector double __ATTRS_o_ai
vec_vsx_ld(int __a, const double *__b) {
  return (vector double)__builtin_vsx_lxvd2x(__a, __b);
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vsx_ld(int __a, const vector bool short *__b) {
  return (vector bool short)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector signed short __ATTRS_o_ai
vec_vsx_ld(int __a, const vector signed short *__b) {
  return (vector signed short)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector signed short __ATTRS_o_ai
vec_vsx_ld(int __a, const signed short *__b) {
  return (vector signed short)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsx_ld(int __a, const vector unsigned short *__b) {
  return (vector unsigned short)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vsx_ld(int __a, const unsigned short *__b) {
  return (vector unsigned short)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector bool char __ATTRS_o_ai
vec_vsx_ld(int __a, const vector bool char *__b) {
  return (vector bool char)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vsx_ld(int __a, const vector signed char *__b) {
  return (vector signed char)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vsx_ld(int __a, const signed char *__b) {
  return (vector signed char)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsx_ld(int __a, const vector unsigned char *__b) {
  return (vector unsigned char)__builtin_vsx_lxvw4x(__a, __b);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vsx_ld(int __a, const unsigned char *__b) {
  return (vector unsigned char)__builtin_vsx_lxvw4x(__a, __b);
}

#endif

/* vec_vsx_st */

#ifdef __VSX__

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector bool int __a, int __b,
                                               vector bool int *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector bool int __a, int __b,
                                               signed int *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector bool int __a, int __b,
                                               unsigned int *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector signed int __a, int __b,
                                               vector signed int *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector signed int __a, int __b,
                                               signed int *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector unsigned int __a, int __b,
                                               vector unsigned int *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector unsigned int __a, int __b,
                                               unsigned int *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector float __a, int __b,
                                               vector float *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector float __a, int __b,
                                               float *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector signed long long __a,
                                               int __b,
                                               vector signed long long *__c) {
  __builtin_vsx_stxvd2x((vector double)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector unsigned long long __a,
                                               int __b,
                                               vector unsigned long long *__c) {
  __builtin_vsx_stxvd2x((vector double)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector double __a, int __b,
                                               vector double *__c) {
  __builtin_vsx_stxvd2x((vector double)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector double __a, int __b,
                                               double *__c) {
  __builtin_vsx_stxvd2x((vector double)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector bool short __a, int __b,
                                               vector bool short *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector bool short __a, int __b,
                                               signed short *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector bool short __a, int __b,
                                               unsigned short *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}
static __inline__ void __ATTRS_o_ai vec_vsx_st(vector signed short __a, int __b,
                                               vector signed short *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector signed short __a, int __b,
                                               signed short *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector unsigned short __a,
                                               int __b,
                                               vector unsigned short *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector unsigned short __a,
                                               int __b, unsigned short *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector bool char __a, int __b,
                                               vector bool char *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector bool char __a, int __b,
                                               signed char *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector bool char __a, int __b,
                                               unsigned char *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector signed char __a, int __b,
                                               vector signed char *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector signed char __a, int __b,
                                               signed char *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector unsigned char __a,
                                               int __b,
                                               vector unsigned char *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_vsx_st(vector unsigned char __a,
                                               int __b, unsigned char *__c) {
  __builtin_vsx_stxvw4x((vector int)__a, __b, __c);
}

#endif

#ifdef __VSX__
#define vec_xxpermdi __builtin_vsx_xxpermdi
#define vec_xxsldwi __builtin_vsx_xxsldwi
#endif

/* vec_xor */

#define __builtin_altivec_vxor vec_xor

static __inline__ vector signed char __ATTRS_o_ai
vec_xor(vector signed char __a, vector signed char __b) {
  return __a ^ __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_xor(vector bool char __a, vector signed char __b) {
  return (vector signed char)__a ^ __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_xor(vector signed char __a, vector bool char __b) {
  return __a ^ (vector signed char)__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_xor(vector unsigned char __a, vector unsigned char __b) {
  return __a ^ __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_xor(vector bool char __a, vector unsigned char __b) {
  return (vector unsigned char)__a ^ __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_xor(vector unsigned char __a, vector bool char __b) {
  return __a ^ (vector unsigned char)__b;
}

static __inline__ vector bool char __ATTRS_o_ai vec_xor(vector bool char __a,
                                                        vector bool char __b) {
  return __a ^ __b;
}

static __inline__ vector short __ATTRS_o_ai vec_xor(vector short __a,
                                                    vector short __b) {
  return __a ^ __b;
}

static __inline__ vector short __ATTRS_o_ai vec_xor(vector bool short __a,
                                                    vector short __b) {
  return (vector short)__a ^ __b;
}

static __inline__ vector short __ATTRS_o_ai vec_xor(vector short __a,
                                                    vector bool short __b) {
  return __a ^ (vector short)__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_xor(vector unsigned short __a, vector unsigned short __b) {
  return __a ^ __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_xor(vector bool short __a, vector unsigned short __b) {
  return (vector unsigned short)__a ^ __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_xor(vector unsigned short __a, vector bool short __b) {
  return __a ^ (vector unsigned short)__b;
}

static __inline__ vector bool short __ATTRS_o_ai
vec_xor(vector bool short __a, vector bool short __b) {
  return __a ^ __b;
}

static __inline__ vector int __ATTRS_o_ai vec_xor(vector int __a,
                                                  vector int __b) {
  return __a ^ __b;
}

static __inline__ vector int __ATTRS_o_ai vec_xor(vector bool int __a,
                                                  vector int __b) {
  return (vector int)__a ^ __b;
}

static __inline__ vector int __ATTRS_o_ai vec_xor(vector int __a,
                                                  vector bool int __b) {
  return __a ^ (vector int)__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_xor(vector unsigned int __a, vector unsigned int __b) {
  return __a ^ __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_xor(vector bool int __a, vector unsigned int __b) {
  return (vector unsigned int)__a ^ __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_xor(vector unsigned int __a, vector bool int __b) {
  return __a ^ (vector unsigned int)__b;
}

static __inline__ vector bool int __ATTRS_o_ai vec_xor(vector bool int __a,
                                                       vector bool int __b) {
  return __a ^ __b;
}

static __inline__ vector float __ATTRS_o_ai vec_xor(vector float __a,
                                                    vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a ^ (vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_xor(vector bool int __a,
                                                    vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a ^ (vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_xor(vector float __a,
                                                    vector bool int __b) {
  vector unsigned int __res =
      (vector unsigned int)__a ^ (vector unsigned int)__b;
  return (vector float)__res;
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_xor(vector signed long long __a, vector signed long long __b) {
  return __a ^ __b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_xor(vector bool long long __a, vector signed long long __b) {
  return (vector signed long long)__a ^ __b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_xor(vector signed long long __a, vector bool long long __b) {
  return __a ^ (vector signed long long)__b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_xor(vector unsigned long long __a, vector unsigned long long __b) {
  return __a ^ __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_xor(vector bool long long __a, vector unsigned long long __b) {
  return (vector unsigned long long)__a ^ __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_xor(vector unsigned long long __a, vector bool long long __b) {
  return __a ^ (vector unsigned long long)__b;
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_xor(vector bool long long __a, vector bool long long __b) {
  return __a ^ __b;
}

static __inline__ vector double __ATTRS_o_ai vec_xor(vector double __a,
                                                     vector double __b) {
  return (vector double)((vector unsigned long long)__a ^
                         (vector unsigned long long)__b);
}

static __inline__ vector double __ATTRS_o_ai
vec_xor(vector double __a, vector bool long long __b) {
  return (vector double)((vector unsigned long long)__a ^
                         (vector unsigned long long)__b);
}

static __inline__ vector double __ATTRS_o_ai vec_xor(vector bool long long __a,
                                                     vector double __b) {
  return (vector double)((vector unsigned long long)__a ^
                         (vector unsigned long long)__b);
}
#endif

/* vec_vxor */

static __inline__ vector signed char __ATTRS_o_ai
vec_vxor(vector signed char __a, vector signed char __b) {
  return __a ^ __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vxor(vector bool char __a, vector signed char __b) {
  return (vector signed char)__a ^ __b;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vxor(vector signed char __a, vector bool char __b) {
  return __a ^ (vector signed char)__b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vxor(vector unsigned char __a, vector unsigned char __b) {
  return __a ^ __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vxor(vector bool char __a, vector unsigned char __b) {
  return (vector unsigned char)__a ^ __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vxor(vector unsigned char __a, vector bool char __b) {
  return __a ^ (vector unsigned char)__b;
}

static __inline__ vector bool char __ATTRS_o_ai vec_vxor(vector bool char __a,
                                                         vector bool char __b) {
  return __a ^ __b;
}

static __inline__ vector short __ATTRS_o_ai vec_vxor(vector short __a,
                                                     vector short __b) {
  return __a ^ __b;
}

static __inline__ vector short __ATTRS_o_ai vec_vxor(vector bool short __a,
                                                     vector short __b) {
  return (vector short)__a ^ __b;
}

static __inline__ vector short __ATTRS_o_ai vec_vxor(vector short __a,
                                                     vector bool short __b) {
  return __a ^ (vector short)__b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vxor(vector unsigned short __a, vector unsigned short __b) {
  return __a ^ __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vxor(vector bool short __a, vector unsigned short __b) {
  return (vector unsigned short)__a ^ __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_vxor(vector unsigned short __a, vector bool short __b) {
  return __a ^ (vector unsigned short)__b;
}

static __inline__ vector bool short __ATTRS_o_ai
vec_vxor(vector bool short __a, vector bool short __b) {
  return __a ^ __b;
}

static __inline__ vector int __ATTRS_o_ai vec_vxor(vector int __a,
                                                   vector int __b) {
  return __a ^ __b;
}

static __inline__ vector int __ATTRS_o_ai vec_vxor(vector bool int __a,
                                                   vector int __b) {
  return (vector int)__a ^ __b;
}

static __inline__ vector int __ATTRS_o_ai vec_vxor(vector int __a,
                                                   vector bool int __b) {
  return __a ^ (vector int)__b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vxor(vector unsigned int __a, vector unsigned int __b) {
  return __a ^ __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vxor(vector bool int __a, vector unsigned int __b) {
  return (vector unsigned int)__a ^ __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_vxor(vector unsigned int __a, vector bool int __b) {
  return __a ^ (vector unsigned int)__b;
}

static __inline__ vector bool int __ATTRS_o_ai vec_vxor(vector bool int __a,
                                                        vector bool int __b) {
  return __a ^ __b;
}

static __inline__ vector float __ATTRS_o_ai vec_vxor(vector float __a,
                                                     vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a ^ (vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_vxor(vector bool int __a,
                                                     vector float __b) {
  vector unsigned int __res =
      (vector unsigned int)__a ^ (vector unsigned int)__b;
  return (vector float)__res;
}

static __inline__ vector float __ATTRS_o_ai vec_vxor(vector float __a,
                                                     vector bool int __b) {
  vector unsigned int __res =
      (vector unsigned int)__a ^ (vector unsigned int)__b;
  return (vector float)__res;
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_vxor(vector signed long long __a, vector signed long long __b) {
  return __a ^ __b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_vxor(vector bool long long __a, vector signed long long __b) {
  return (vector signed long long)__a ^ __b;
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_vxor(vector signed long long __a, vector bool long long __b) {
  return __a ^ (vector signed long long)__b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_vxor(vector unsigned long long __a, vector unsigned long long __b) {
  return __a ^ __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_vxor(vector bool long long __a, vector unsigned long long __b) {
  return (vector unsigned long long)__a ^ __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_vxor(vector unsigned long long __a, vector bool long long __b) {
  return __a ^ (vector unsigned long long)__b;
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_vxor(vector bool long long __a, vector bool long long __b) {
  return __a ^ __b;
}
#endif

/* ------------------------ extensions for CBEA ----------------------------- */

/* vec_extract */

static __inline__ signed char __ATTRS_o_ai vec_extract(vector signed char __a,
                                                       int __b) {
  return __a[__b];
}

static __inline__ unsigned char __ATTRS_o_ai
vec_extract(vector unsigned char __a, int __b) {
  return __a[__b];
}

static __inline__ unsigned char __ATTRS_o_ai vec_extract(vector bool char __a,
                                                         int __b) {
  return __a[__b];
}

static __inline__ signed short __ATTRS_o_ai vec_extract(vector signed short __a,
                                                        int __b) {
  return __a[__b];
}

static __inline__ unsigned short __ATTRS_o_ai
vec_extract(vector unsigned short __a, int __b) {
  return __a[__b];
}

static __inline__ unsigned short __ATTRS_o_ai vec_extract(vector bool short __a,
                                                          int __b) {
  return __a[__b];
}

static __inline__ signed int __ATTRS_o_ai vec_extract(vector signed int __a,
                                                      int __b) {
  return __a[__b];
}

static __inline__ unsigned int __ATTRS_o_ai vec_extract(vector unsigned int __a,
                                                        int __b) {
  return __a[__b];
}

static __inline__ unsigned int __ATTRS_o_ai vec_extract(vector bool int __a,
                                                        int __b) {
  return __a[__b];
}

#ifdef __VSX__
static __inline__ signed long long __ATTRS_o_ai
vec_extract(vector signed long long __a, int __b) {
  return __a[__b];
}

static __inline__ unsigned long long __ATTRS_o_ai
vec_extract(vector unsigned long long __a, int __b) {
  return __a[__b];
}

static __inline__ unsigned long long __ATTRS_o_ai
vec_extract(vector bool long long __a, int __b) {
  return __a[__b];
}

static __inline__ double __ATTRS_o_ai vec_extract(vector double __a, int __b) {
  return __a[__b];
}
#endif

static __inline__ float __ATTRS_o_ai vec_extract(vector float __a, int __b) {
  return __a[__b];
}

#ifdef __POWER9_VECTOR__

#define vec_insert4b __builtin_vsx_insertword
#define vec_extract4b __builtin_vsx_extractuword

/* vec_extract_exp */

static __inline__ vector unsigned int __ATTRS_o_ai
vec_extract_exp(vector float __a) {
  return __builtin_vsx_xvxexpsp(__a);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_extract_exp(vector double __a) {
  return __builtin_vsx_xvxexpdp(__a);
}

/* vec_extract_sig */

static __inline__ vector unsigned int __ATTRS_o_ai
vec_extract_sig(vector float __a) {
  return __builtin_vsx_xvxsigsp(__a);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_extract_sig (vector double __a) {
  return __builtin_vsx_xvxsigdp(__a);
}

static __inline__ vector float __ATTRS_o_ai
vec_extract_fp32_from_shorth(vector unsigned short __a) {
  vector unsigned short __b =
#ifdef __LITTLE_ENDIAN__
            __builtin_shufflevector(__a, __a, 0, -1, 1, -1, 2, -1, 3, -1);
#else
            __builtin_shufflevector(__a, __a, -1, 0, -1, 1, -1, 2, -1, 3);
#endif
  return __builtin_vsx_xvcvhpsp(__b);
}

static __inline__ vector float __ATTRS_o_ai
vec_extract_fp32_from_shortl(vector unsigned short __a) {
  vector unsigned short __b =
#ifdef __LITTLE_ENDIAN__
            __builtin_shufflevector(__a, __a, 4, -1, 5, -1, 6, -1, 7, -1);
#else
            __builtin_shufflevector(__a, __a, -1, 4, -1, 5, -1, 6, -1, 7);
#endif
  return __builtin_vsx_xvcvhpsp(__b);
}
#endif /* __POWER9_VECTOR__ */

/* vec_insert */

static __inline__ vector signed char __ATTRS_o_ai
vec_insert(signed char __a, vector signed char __b, int __c) {
  __b[__c] = __a;
  return __b;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_insert(unsigned char __a, vector unsigned char __b, int __c) {
  __b[__c] = __a;
  return __b;
}

static __inline__ vector bool char __ATTRS_o_ai vec_insert(unsigned char __a,
                                                           vector bool char __b,
                                                           int __c) {
  __b[__c] = __a;
  return __b;
}

static __inline__ vector signed short __ATTRS_o_ai
vec_insert(signed short __a, vector signed short __b, int __c) {
  __b[__c] = __a;
  return __b;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_insert(unsigned short __a, vector unsigned short __b, int __c) {
  __b[__c] = __a;
  return __b;
}

static __inline__ vector bool short __ATTRS_o_ai
vec_insert(unsigned short __a, vector bool short __b, int __c) {
  __b[__c] = __a;
  return __b;
}

static __inline__ vector signed int __ATTRS_o_ai
vec_insert(signed int __a, vector signed int __b, int __c) {
  __b[__c] = __a;
  return __b;
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_insert(unsigned int __a, vector unsigned int __b, int __c) {
  __b[__c] = __a;
  return __b;
}

static __inline__ vector bool int __ATTRS_o_ai vec_insert(unsigned int __a,
                                                          vector bool int __b,
                                                          int __c) {
  __b[__c] = __a;
  return __b;
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_insert(signed long long __a, vector signed long long __b, int __c) {
  __b[__c] = __a;
  return __b;
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_insert(unsigned long long __a, vector unsigned long long __b, int __c) {
  __b[__c] = __a;
  return __b;
}

static __inline__ vector bool long long __ATTRS_o_ai
vec_insert(unsigned long long __a, vector bool long long __b, int __c) {
  __b[__c] = __a;
  return __b;
}
static __inline__ vector double __ATTRS_o_ai vec_insert(double __a,
                                                        vector double __b,
                                                        int __c) {
  __b[__c] = __a;
  return __b;
}
#endif

static __inline__ vector float __ATTRS_o_ai vec_insert(float __a,
                                                       vector float __b,
                                                       int __c) {
  __b[__c] = __a;
  return __b;
}

/* vec_lvlx */

static __inline__ vector signed char __ATTRS_o_ai
vec_lvlx(int __a, const signed char *__b) {
  return vec_perm(vec_ld(__a, __b), (vector signed char)(0),
                  vec_lvsl(__a, __b));
}

static __inline__ vector signed char __ATTRS_o_ai
vec_lvlx(int __a, const vector signed char *__b) {
  return vec_perm(vec_ld(__a, __b), (vector signed char)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvlx(int __a, const unsigned char *__b) {
  return vec_perm(vec_ld(__a, __b), (vector unsigned char)(0),
                  vec_lvsl(__a, __b));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvlx(int __a, const vector unsigned char *__b) {
  return vec_perm(vec_ld(__a, __b), (vector unsigned char)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector bool char __ATTRS_o_ai
vec_lvlx(int __a, const vector bool char *__b) {
  return vec_perm(vec_ld(__a, __b), (vector bool char)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector short __ATTRS_o_ai vec_lvlx(int __a,
                                                     const short *__b) {
  return vec_perm(vec_ld(__a, __b), (vector short)(0), vec_lvsl(__a, __b));
}

static __inline__ vector short __ATTRS_o_ai vec_lvlx(int __a,
                                                     const vector short *__b) {
  return vec_perm(vec_ld(__a, __b), (vector short)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_lvlx(int __a, const unsigned short *__b) {
  return vec_perm(vec_ld(__a, __b), (vector unsigned short)(0),
                  vec_lvsl(__a, __b));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_lvlx(int __a, const vector unsigned short *__b) {
  return vec_perm(vec_ld(__a, __b), (vector unsigned short)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector bool short __ATTRS_o_ai
vec_lvlx(int __a, const vector bool short *__b) {
  return vec_perm(vec_ld(__a, __b), (vector bool short)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector pixel __ATTRS_o_ai vec_lvlx(int __a,
                                                     const vector pixel *__b) {
  return vec_perm(vec_ld(__a, __b), (vector pixel)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector int __ATTRS_o_ai vec_lvlx(int __a, const int *__b) {
  return vec_perm(vec_ld(__a, __b), (vector int)(0), vec_lvsl(__a, __b));
}

static __inline__ vector int __ATTRS_o_ai vec_lvlx(int __a,
                                                   const vector int *__b) {
  return vec_perm(vec_ld(__a, __b), (vector int)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_lvlx(int __a, const unsigned int *__b) {
  return vec_perm(vec_ld(__a, __b), (vector unsigned int)(0),
                  vec_lvsl(__a, __b));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_lvlx(int __a, const vector unsigned int *__b) {
  return vec_perm(vec_ld(__a, __b), (vector unsigned int)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector bool int __ATTRS_o_ai
vec_lvlx(int __a, const vector bool int *__b) {
  return vec_perm(vec_ld(__a, __b), (vector bool int)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector float __ATTRS_o_ai vec_lvlx(int __a,
                                                     const float *__b) {
  return vec_perm(vec_ld(__a, __b), (vector float)(0), vec_lvsl(__a, __b));
}

static __inline__ vector float __ATTRS_o_ai vec_lvlx(int __a,
                                                     const vector float *__b) {
  return vec_perm(vec_ld(__a, __b), (vector float)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

/* vec_lvlxl */

static __inline__ vector signed char __ATTRS_o_ai
vec_lvlxl(int __a, const signed char *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector signed char)(0),
                  vec_lvsl(__a, __b));
}

static __inline__ vector signed char __ATTRS_o_ai
vec_lvlxl(int __a, const vector signed char *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector signed char)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvlxl(int __a, const unsigned char *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector unsigned char)(0),
                  vec_lvsl(__a, __b));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvlxl(int __a, const vector unsigned char *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector unsigned char)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector bool char __ATTRS_o_ai
vec_lvlxl(int __a, const vector bool char *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector bool char)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector short __ATTRS_o_ai vec_lvlxl(int __a,
                                                      const short *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector short)(0), vec_lvsl(__a, __b));
}

static __inline__ vector short __ATTRS_o_ai vec_lvlxl(int __a,
                                                      const vector short *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector short)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_lvlxl(int __a, const unsigned short *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector unsigned short)(0),
                  vec_lvsl(__a, __b));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_lvlxl(int __a, const vector unsigned short *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector unsigned short)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector bool short __ATTRS_o_ai
vec_lvlxl(int __a, const vector bool short *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector bool short)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector pixel __ATTRS_o_ai vec_lvlxl(int __a,
                                                      const vector pixel *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector pixel)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector int __ATTRS_o_ai vec_lvlxl(int __a, const int *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector int)(0), vec_lvsl(__a, __b));
}

static __inline__ vector int __ATTRS_o_ai vec_lvlxl(int __a,
                                                    const vector int *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector int)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_lvlxl(int __a, const unsigned int *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector unsigned int)(0),
                  vec_lvsl(__a, __b));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_lvlxl(int __a, const vector unsigned int *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector unsigned int)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector bool int __ATTRS_o_ai
vec_lvlxl(int __a, const vector bool int *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector bool int)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector float __ATTRS_o_ai vec_lvlxl(int __a,
                                                      const float *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector float)(0), vec_lvsl(__a, __b));
}

static __inline__ vector float __ATTRS_o_ai vec_lvlxl(int __a,
                                                      vector float *__b) {
  return vec_perm(vec_ldl(__a, __b), (vector float)(0),
                  vec_lvsl(__a, (unsigned char *)__b));
}

/* vec_lvrx */

static __inline__ vector signed char __ATTRS_o_ai
vec_lvrx(int __a, const signed char *__b) {
  return vec_perm((vector signed char)(0), vec_ld(__a, __b),
                  vec_lvsl(__a, __b));
}

static __inline__ vector signed char __ATTRS_o_ai
vec_lvrx(int __a, const vector signed char *__b) {
  return vec_perm((vector signed char)(0), vec_ld(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvrx(int __a, const unsigned char *__b) {
  return vec_perm((vector unsigned char)(0), vec_ld(__a, __b),
                  vec_lvsl(__a, __b));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvrx(int __a, const vector unsigned char *__b) {
  return vec_perm((vector unsigned char)(0), vec_ld(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector bool char __ATTRS_o_ai
vec_lvrx(int __a, const vector bool char *__b) {
  return vec_perm((vector bool char)(0), vec_ld(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector short __ATTRS_o_ai vec_lvrx(int __a,
                                                     const short *__b) {
  return vec_perm((vector short)(0), vec_ld(__a, __b), vec_lvsl(__a, __b));
}

static __inline__ vector short __ATTRS_o_ai vec_lvrx(int __a,
                                                     const vector short *__b) {
  return vec_perm((vector short)(0), vec_ld(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_lvrx(int __a, const unsigned short *__b) {
  return vec_perm((vector unsigned short)(0), vec_ld(__a, __b),
                  vec_lvsl(__a, __b));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_lvrx(int __a, const vector unsigned short *__b) {
  return vec_perm((vector unsigned short)(0), vec_ld(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector bool short __ATTRS_o_ai
vec_lvrx(int __a, const vector bool short *__b) {
  return vec_perm((vector bool short)(0), vec_ld(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector pixel __ATTRS_o_ai vec_lvrx(int __a,
                                                     const vector pixel *__b) {
  return vec_perm((vector pixel)(0), vec_ld(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector int __ATTRS_o_ai vec_lvrx(int __a, const int *__b) {
  return vec_perm((vector int)(0), vec_ld(__a, __b), vec_lvsl(__a, __b));
}

static __inline__ vector int __ATTRS_o_ai vec_lvrx(int __a,
                                                   const vector int *__b) {
  return vec_perm((vector int)(0), vec_ld(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_lvrx(int __a, const unsigned int *__b) {
  return vec_perm((vector unsigned int)(0), vec_ld(__a, __b),
                  vec_lvsl(__a, __b));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_lvrx(int __a, const vector unsigned int *__b) {
  return vec_perm((vector unsigned int)(0), vec_ld(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector bool int __ATTRS_o_ai
vec_lvrx(int __a, const vector bool int *__b) {
  return vec_perm((vector bool int)(0), vec_ld(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector float __ATTRS_o_ai vec_lvrx(int __a,
                                                     const float *__b) {
  return vec_perm((vector float)(0), vec_ld(__a, __b), vec_lvsl(__a, __b));
}

static __inline__ vector float __ATTRS_o_ai vec_lvrx(int __a,
                                                     const vector float *__b) {
  return vec_perm((vector float)(0), vec_ld(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

/* vec_lvrxl */

static __inline__ vector signed char __ATTRS_o_ai
vec_lvrxl(int __a, const signed char *__b) {
  return vec_perm((vector signed char)(0), vec_ldl(__a, __b),
                  vec_lvsl(__a, __b));
}

static __inline__ vector signed char __ATTRS_o_ai
vec_lvrxl(int __a, const vector signed char *__b) {
  return vec_perm((vector signed char)(0), vec_ldl(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvrxl(int __a, const unsigned char *__b) {
  return vec_perm((vector unsigned char)(0), vec_ldl(__a, __b),
                  vec_lvsl(__a, __b));
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_lvrxl(int __a, const vector unsigned char *__b) {
  return vec_perm((vector unsigned char)(0), vec_ldl(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector bool char __ATTRS_o_ai
vec_lvrxl(int __a, const vector bool char *__b) {
  return vec_perm((vector bool char)(0), vec_ldl(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector short __ATTRS_o_ai vec_lvrxl(int __a,
                                                      const short *__b) {
  return vec_perm((vector short)(0), vec_ldl(__a, __b), vec_lvsl(__a, __b));
}

static __inline__ vector short __ATTRS_o_ai vec_lvrxl(int __a,
                                                      const vector short *__b) {
  return vec_perm((vector short)(0), vec_ldl(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_lvrxl(int __a, const unsigned short *__b) {
  return vec_perm((vector unsigned short)(0), vec_ldl(__a, __b),
                  vec_lvsl(__a, __b));
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_lvrxl(int __a, const vector unsigned short *__b) {
  return vec_perm((vector unsigned short)(0), vec_ldl(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector bool short __ATTRS_o_ai
vec_lvrxl(int __a, const vector bool short *__b) {
  return vec_perm((vector bool short)(0), vec_ldl(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector pixel __ATTRS_o_ai vec_lvrxl(int __a,
                                                      const vector pixel *__b) {
  return vec_perm((vector pixel)(0), vec_ldl(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector int __ATTRS_o_ai vec_lvrxl(int __a, const int *__b) {
  return vec_perm((vector int)(0), vec_ldl(__a, __b), vec_lvsl(__a, __b));
}

static __inline__ vector int __ATTRS_o_ai vec_lvrxl(int __a,
                                                    const vector int *__b) {
  return vec_perm((vector int)(0), vec_ldl(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_lvrxl(int __a, const unsigned int *__b) {
  return vec_perm((vector unsigned int)(0), vec_ldl(__a, __b),
                  vec_lvsl(__a, __b));
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_lvrxl(int __a, const vector unsigned int *__b) {
  return vec_perm((vector unsigned int)(0), vec_ldl(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector bool int __ATTRS_o_ai
vec_lvrxl(int __a, const vector bool int *__b) {
  return vec_perm((vector bool int)(0), vec_ldl(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

static __inline__ vector float __ATTRS_o_ai vec_lvrxl(int __a,
                                                      const float *__b) {
  return vec_perm((vector float)(0), vec_ldl(__a, __b), vec_lvsl(__a, __b));
}

static __inline__ vector float __ATTRS_o_ai vec_lvrxl(int __a,
                                                      const vector float *__b) {
  return vec_perm((vector float)(0), vec_ldl(__a, __b),
                  vec_lvsl(__a, (unsigned char *)__b));
}

/* vec_stvlx */

static __inline__ void __ATTRS_o_ai vec_stvlx(vector signed char __a, int __b,
                                              signed char *__c) {
  return vec_st(vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, __c)), __b,
                __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector signed char __a, int __b,
                                              vector signed char *__c) {
  return vec_st(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector unsigned char __a, int __b,
                                              unsigned char *__c) {
  return vec_st(vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, __c)), __b,
                __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector unsigned char __a, int __b,
                                              vector unsigned char *__c) {
  return vec_st(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector bool char __a, int __b,
                                              vector bool char *__c) {
  return vec_st(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector short __a, int __b,
                                              short *__c) {
  return vec_st(vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, __c)), __b,
                __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector short __a, int __b,
                                              vector short *__c) {
  return vec_st(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector unsigned short __a,
                                              int __b, unsigned short *__c) {
  return vec_st(vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, __c)), __b,
                __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector unsigned short __a,
                                              int __b,
                                              vector unsigned short *__c) {
  return vec_st(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector bool short __a, int __b,
                                              vector bool short *__c) {
  return vec_st(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector pixel __a, int __b,
                                              vector pixel *__c) {
  return vec_st(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector int __a, int __b,
                                              int *__c) {
  return vec_st(vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, __c)), __b,
                __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector int __a, int __b,
                                              vector int *__c) {
  return vec_st(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector unsigned int __a, int __b,
                                              unsigned int *__c) {
  return vec_st(vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, __c)), __b,
                __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector unsigned int __a, int __b,
                                              vector unsigned int *__c) {
  return vec_st(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector bool int __a, int __b,
                                              vector bool int *__c) {
  return vec_st(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlx(vector float __a, int __b,
                                              vector float *__c) {
  return vec_st(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

/* vec_stvlxl */

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector signed char __a, int __b,
                                               signed char *__c) {
  return vec_stl(vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, __c)), __b,
                 __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector signed char __a, int __b,
                                               vector signed char *__c) {
  return vec_stl(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector unsigned char __a,
                                               int __b, unsigned char *__c) {
  return vec_stl(vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, __c)), __b,
                 __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector unsigned char __a,
                                               int __b,
                                               vector unsigned char *__c) {
  return vec_stl(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector bool char __a, int __b,
                                               vector bool char *__c) {
  return vec_stl(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector short __a, int __b,
                                               short *__c) {
  return vec_stl(vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, __c)), __b,
                 __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector short __a, int __b,
                                               vector short *__c) {
  return vec_stl(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector unsigned short __a,
                                               int __b, unsigned short *__c) {
  return vec_stl(vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, __c)), __b,
                 __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector unsigned short __a,
                                               int __b,
                                               vector unsigned short *__c) {
  return vec_stl(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector bool short __a, int __b,
                                               vector bool short *__c) {
  return vec_stl(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector pixel __a, int __b,
                                               vector pixel *__c) {
  return vec_stl(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector int __a, int __b,
                                               int *__c) {
  return vec_stl(vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, __c)), __b,
                 __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector int __a, int __b,
                                               vector int *__c) {
  return vec_stl(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector unsigned int __a, int __b,
                                               unsigned int *__c) {
  return vec_stl(vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, __c)), __b,
                 __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector unsigned int __a, int __b,
                                               vector unsigned int *__c) {
  return vec_stl(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector bool int __a, int __b,
                                               vector bool int *__c) {
  return vec_stl(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvlxl(vector float __a, int __b,
                                               vector float *__c) {
  return vec_stl(
      vec_perm(vec_lvrx(__b, __c), __a, vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

/* vec_stvrx */

static __inline__ void __ATTRS_o_ai vec_stvrx(vector signed char __a, int __b,
                                              signed char *__c) {
  return vec_st(vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, __c)), __b,
                __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector signed char __a, int __b,
                                              vector signed char *__c) {
  return vec_st(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector unsigned char __a, int __b,
                                              unsigned char *__c) {
  return vec_st(vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, __c)), __b,
                __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector unsigned char __a, int __b,
                                              vector unsigned char *__c) {
  return vec_st(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector bool char __a, int __b,
                                              vector bool char *__c) {
  return vec_st(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector short __a, int __b,
                                              short *__c) {
  return vec_st(vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, __c)), __b,
                __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector short __a, int __b,
                                              vector short *__c) {
  return vec_st(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector unsigned short __a,
                                              int __b, unsigned short *__c) {
  return vec_st(vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, __c)), __b,
                __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector unsigned short __a,
                                              int __b,
                                              vector unsigned short *__c) {
  return vec_st(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector bool short __a, int __b,
                                              vector bool short *__c) {
  return vec_st(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector pixel __a, int __b,
                                              vector pixel *__c) {
  return vec_st(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector int __a, int __b,
                                              int *__c) {
  return vec_st(vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, __c)), __b,
                __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector int __a, int __b,
                                              vector int *__c) {
  return vec_st(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector unsigned int __a, int __b,
                                              unsigned int *__c) {
  return vec_st(vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, __c)), __b,
                __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector unsigned int __a, int __b,
                                              vector unsigned int *__c) {
  return vec_st(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector bool int __a, int __b,
                                              vector bool int *__c) {
  return vec_st(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrx(vector float __a, int __b,
                                              vector float *__c) {
  return vec_st(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

/* vec_stvrxl */

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector signed char __a, int __b,
                                               signed char *__c) {
  return vec_stl(vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, __c)), __b,
                 __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector signed char __a, int __b,
                                               vector signed char *__c) {
  return vec_stl(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector unsigned char __a,
                                               int __b, unsigned char *__c) {
  return vec_stl(vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, __c)), __b,
                 __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector unsigned char __a,
                                               int __b,
                                               vector unsigned char *__c) {
  return vec_stl(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector bool char __a, int __b,
                                               vector bool char *__c) {
  return vec_stl(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector short __a, int __b,
                                               short *__c) {
  return vec_stl(vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, __c)), __b,
                 __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector short __a, int __b,
                                               vector short *__c) {
  return vec_stl(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector unsigned short __a,
                                               int __b, unsigned short *__c) {
  return vec_stl(vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, __c)), __b,
                 __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector unsigned short __a,
                                               int __b,
                                               vector unsigned short *__c) {
  return vec_stl(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector bool short __a, int __b,
                                               vector bool short *__c) {
  return vec_stl(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector pixel __a, int __b,
                                               vector pixel *__c) {
  return vec_stl(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector int __a, int __b,
                                               int *__c) {
  return vec_stl(vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, __c)), __b,
                 __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector int __a, int __b,
                                               vector int *__c) {
  return vec_stl(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector unsigned int __a, int __b,
                                               unsigned int *__c) {
  return vec_stl(vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, __c)), __b,
                 __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector unsigned int __a, int __b,
                                               vector unsigned int *__c) {
  return vec_stl(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector bool int __a, int __b,
                                               vector bool int *__c) {
  return vec_stl(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

static __inline__ void __ATTRS_o_ai vec_stvrxl(vector float __a, int __b,
                                               vector float *__c) {
  return vec_stl(
      vec_perm(__a, vec_lvlx(__b, __c), vec_lvsr(__b, (unsigned char *)__c)),
      __b, __c);
}

/* vec_promote */

static __inline__ vector signed char __ATTRS_o_ai vec_promote(signed char __a,
                                                              int __b) {
  vector signed char __res = (vector signed char)(0);
  __res[__b] = __a;
  return __res;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_promote(unsigned char __a, int __b) {
  vector unsigned char __res = (vector unsigned char)(0);
  __res[__b] = __a;
  return __res;
}

static __inline__ vector short __ATTRS_o_ai vec_promote(short __a, int __b) {
  vector short __res = (vector short)(0);
  __res[__b] = __a;
  return __res;
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_promote(unsigned short __a, int __b) {
  vector unsigned short __res = (vector unsigned short)(0);
  __res[__b] = __a;
  return __res;
}

static __inline__ vector int __ATTRS_o_ai vec_promote(int __a, int __b) {
  vector int __res = (vector int)(0);
  __res[__b] = __a;
  return __res;
}

static __inline__ vector unsigned int __ATTRS_o_ai vec_promote(unsigned int __a,
                                                               int __b) {
  vector unsigned int __res = (vector unsigned int)(0);
  __res[__b] = __a;
  return __res;
}

static __inline__ vector float __ATTRS_o_ai vec_promote(float __a, int __b) {
  vector float __res = (vector float)(0);
  __res[__b] = __a;
  return __res;
}

/* vec_splats */

static __inline__ vector signed char __ATTRS_o_ai vec_splats(signed char __a) {
  return (vector signed char)(__a);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_splats(unsigned char __a) {
  return (vector unsigned char)(__a);
}

static __inline__ vector short __ATTRS_o_ai vec_splats(short __a) {
  return (vector short)(__a);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_splats(unsigned short __a) {
  return (vector unsigned short)(__a);
}

static __inline__ vector int __ATTRS_o_ai vec_splats(int __a) {
  return (vector int)(__a);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_splats(unsigned int __a) {
  return (vector unsigned int)(__a);
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_splats(signed long long __a) {
  return (vector signed long long)(__a);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_splats(unsigned long long __a) {
  return (vector unsigned long long)(__a);
}

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
static __inline__ vector signed __int128 __ATTRS_o_ai
vec_splats(signed __int128 __a) {
  return (vector signed __int128)(__a);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_splats(unsigned __int128 __a) {
  return (vector unsigned __int128)(__a);
}

#endif

static __inline__ vector double __ATTRS_o_ai vec_splats(double __a) {
  return (vector double)(__a);
}
#endif

static __inline__ vector float __ATTRS_o_ai vec_splats(float __a) {
  return (vector float)(__a);
}

/* ----------------------------- predicates --------------------------------- */

/* vec_all_eq */

static __inline__ int __ATTRS_o_ai vec_all_eq(vector signed char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_LT, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector signed char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_LT, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector unsigned char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_LT, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector unsigned char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_LT, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector bool char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_LT, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector bool char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_LT, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector bool char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_LT, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT, __a, (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector unsigned short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector unsigned short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector bool short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector bool short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector bool short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector pixel __a,
                                              vector pixel __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector int __a, vector int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_LT, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_LT, __a, (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector unsigned int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_LT, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector unsigned int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_LT, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector bool int __a,
                                              vector int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_LT, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector bool int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_LT, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector bool int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_LT, (vector int)__a,
                                      (vector int)__b);
}

#ifdef __POWER8_VECTOR__
static __inline__ int __ATTRS_o_ai vec_all_eq(vector signed long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_LT, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_LT, __a, (vector long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector unsigned long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_LT, (vector long long)__a,
                                      (vector long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector unsigned long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_LT, (vector long long)__a,
                                      (vector long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector bool long long __a,
                                              vector long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_LT, (vector long long)__a,
                                      (vector long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector bool long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_LT, (vector long long)__a,
                                      (vector long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_eq(vector bool long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_LT, (vector long long)__a,
                                      (vector long long)__b);
}
#endif

static __inline__ int __ATTRS_o_ai vec_all_eq(vector float __a,
                                              vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvcmpeqsp_p(__CR6_LT, __a, __b);
#else
  return __builtin_altivec_vcmpeqfp_p(__CR6_LT, __a, __b);
#endif
}

#ifdef __VSX__
static __inline__ int __ATTRS_o_ai vec_all_eq(vector double __a,
                                              vector double __b) {
  return __builtin_vsx_xvcmpeqdp_p(__CR6_LT, __a, __b);
}
#endif

/* vec_all_ge */

static __inline__ int __ATTRS_o_ai vec_all_ge(vector signed char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_EQ, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector signed char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_EQ, (vector signed char)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector unsigned char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector unsigned char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ, (vector unsigned char)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector bool char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ, (vector unsigned char)__b,
                                      (vector unsigned char)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector bool char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ, __b, (vector unsigned char)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector bool char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ, (vector unsigned char)__b,
                                      (vector unsigned char)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_EQ, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_EQ, (vector short)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector unsigned short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector unsigned short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ, (vector unsigned short)__b,
                                      __a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector bool short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ, (vector unsigned short)__b,
                                      (vector unsigned short)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector bool short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ, __b,
                                      (vector unsigned short)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector bool short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ, (vector unsigned short)__b,
                                      (vector unsigned short)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector int __a, vector int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_EQ, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_EQ, (vector int)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector unsigned int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector unsigned int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ, (vector unsigned int)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector bool int __a,
                                              vector int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ, (vector unsigned int)__b,
                                      (vector unsigned int)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector bool int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ, __b, (vector unsigned int)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector bool int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ, (vector unsigned int)__b,
                                      (vector unsigned int)__a);
}

#ifdef __POWER8_VECTOR__
static __inline__ int __ATTRS_o_ai vec_all_ge(vector signed long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_EQ, __b, __a);
}
static __inline__ int __ATTRS_o_ai vec_all_ge(vector signed long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_EQ, (vector signed long long)__b,
                                      __a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector unsigned long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector unsigned long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ, (vector unsigned long long)__b,
                                      __a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector bool long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ, (vector unsigned long long)__b,
                                      (vector unsigned long long)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector bool long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ, __b,
                                      (vector unsigned long long)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_ge(vector bool long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ, (vector unsigned long long)__b,
                                      (vector unsigned long long)__a);
}
#endif

static __inline__ int __ATTRS_o_ai vec_all_ge(vector float __a,
                                              vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvcmpgesp_p(__CR6_LT, __a, __b);
#else
  return __builtin_altivec_vcmpgefp_p(__CR6_LT, __a, __b);
#endif
}

#ifdef __VSX__
static __inline__ int __ATTRS_o_ai vec_all_ge(vector double __a,
                                              vector double __b) {
  return __builtin_vsx_xvcmpgedp_p(__CR6_LT, __a, __b);
}
#endif

/* vec_all_gt */

static __inline__ int __ATTRS_o_ai vec_all_gt(vector signed char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_LT, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector signed char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_LT, __a, (vector signed char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector unsigned char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector unsigned char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT, __a, (vector unsigned char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector bool char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT, (vector unsigned char)__a,
                                      (vector unsigned char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector bool char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT, (vector unsigned char)__a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector bool char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT, (vector unsigned char)__a,
                                      (vector unsigned char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_LT, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_LT, __a, (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector unsigned short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector unsigned short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT, __a,
                                      (vector unsigned short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector bool short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT, (vector unsigned short)__a,
                                      (vector unsigned short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector bool short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT, (vector unsigned short)__a,
                                      __b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector bool short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT, (vector unsigned short)__a,
                                      (vector unsigned short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector int __a, vector int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_LT, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_LT, __a, (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector unsigned int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector unsigned int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT, __a, (vector unsigned int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector bool int __a,
                                              vector int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT, (vector unsigned int)__a,
                                      (vector unsigned int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector bool int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT, (vector unsigned int)__a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector bool int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT, (vector unsigned int)__a,
                                      (vector unsigned int)__b);
}

#ifdef __POWER8_VECTOR__
static __inline__ int __ATTRS_o_ai vec_all_gt(vector signed long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_LT, __a, __b);
}
static __inline__ int __ATTRS_o_ai vec_all_gt(vector signed long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_LT, __a,
                                      (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector unsigned long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector unsigned long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT, __a,
                                      (vector unsigned long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector bool long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT, (vector unsigned long long)__a,
                                      (vector unsigned long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector bool long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT, (vector unsigned long long)__a,
                                      __b);
}

static __inline__ int __ATTRS_o_ai vec_all_gt(vector bool long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT, (vector unsigned long long)__a,
                                      (vector unsigned long long)__b);
}
#endif

static __inline__ int __ATTRS_o_ai vec_all_gt(vector float __a,
                                              vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvcmpgtsp_p(__CR6_LT, __a, __b);
#else
  return __builtin_altivec_vcmpgtfp_p(__CR6_LT, __a, __b);
#endif
}

#ifdef __VSX__
static __inline__ int __ATTRS_o_ai vec_all_gt(vector double __a,
                                              vector double __b) {
  return __builtin_vsx_xvcmpgtdp_p(__CR6_LT, __a, __b);
}
#endif

/* vec_all_in */

static __inline__ int __attribute__((__always_inline__))
vec_all_in(vector float __a, vector float __b) {
  return __builtin_altivec_vcmpbfp_p(__CR6_EQ, __a, __b);
}

/* vec_all_le */

static __inline__ int __ATTRS_o_ai vec_all_le(vector signed char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_EQ, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector signed char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_EQ, __a, (vector signed char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector unsigned char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector unsigned char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ, __a, (vector unsigned char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector bool char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ, (vector unsigned char)__a,
                                      (vector unsigned char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector bool char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ, (vector unsigned char)__a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector bool char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ, (vector unsigned char)__a,
                                      (vector unsigned char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_EQ, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_EQ, __a, (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector unsigned short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector unsigned short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ, __a,
                                      (vector unsigned short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector bool short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ, (vector unsigned short)__a,
                                      (vector unsigned short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector bool short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ, (vector unsigned short)__a,
                                      __b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector bool short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ, (vector unsigned short)__a,
                                      (vector unsigned short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector int __a, vector int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_EQ, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_EQ, __a, (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector unsigned int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector unsigned int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ, __a, (vector unsigned int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector bool int __a,
                                              vector int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ, (vector unsigned int)__a,
                                      (vector unsigned int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector bool int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ, (vector unsigned int)__a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector bool int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ, (vector unsigned int)__a,
                                      (vector unsigned int)__b);
}

#ifdef __POWER8_VECTOR__
static __inline__ int __ATTRS_o_ai vec_all_le(vector signed long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_EQ, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector unsigned long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector signed long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_EQ, __a,
                                      (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector unsigned long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ, __a,
                                      (vector unsigned long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector bool long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ, (vector unsigned long long)__a,
                                      (vector unsigned long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector bool long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ, (vector unsigned long long)__a,
                                      __b);
}

static __inline__ int __ATTRS_o_ai vec_all_le(vector bool long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ, (vector unsigned long long)__a,
                                      (vector unsigned long long)__b);
}
#endif

static __inline__ int __ATTRS_o_ai vec_all_le(vector float __a,
                                              vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvcmpgesp_p(__CR6_LT, __b, __a);
#else
  return __builtin_altivec_vcmpgefp_p(__CR6_LT, __b, __a);
#endif
}

#ifdef __VSX__
static __inline__ int __ATTRS_o_ai vec_all_le(vector double __a,
                                              vector double __b) {
  return __builtin_vsx_xvcmpgedp_p(__CR6_LT, __b, __a);
}
#endif

/* vec_all_lt */

static __inline__ int __ATTRS_o_ai vec_all_lt(vector signed char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_LT, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector signed char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_LT, (vector signed char)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector unsigned char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector unsigned char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT, (vector unsigned char)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector bool char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT, (vector unsigned char)__b,
                                      (vector unsigned char)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector bool char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT, __b, (vector unsigned char)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector bool char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT, (vector unsigned char)__b,
                                      (vector unsigned char)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_LT, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_LT, (vector short)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector unsigned short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector unsigned short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT, (vector unsigned short)__b,
                                      __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector bool short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT, (vector unsigned short)__b,
                                      (vector unsigned short)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector bool short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT, __b,
                                      (vector unsigned short)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector bool short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT, (vector unsigned short)__b,
                                      (vector unsigned short)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector int __a, vector int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_LT, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_LT, (vector int)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector unsigned int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector unsigned int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT, (vector unsigned int)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector bool int __a,
                                              vector int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT, (vector unsigned int)__b,
                                      (vector unsigned int)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector bool int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT, __b, (vector unsigned int)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector bool int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT, (vector unsigned int)__b,
                                      (vector unsigned int)__a);
}

#ifdef __POWER8_VECTOR__
static __inline__ int __ATTRS_o_ai vec_all_lt(vector signed long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_LT, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector unsigned long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector signed long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_LT, (vector signed long long)__b,
                                      __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector unsigned long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT, (vector unsigned long long)__b,
                                      __a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector bool long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT, (vector unsigned long long)__b,
                                      (vector unsigned long long)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector bool long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT, __b,
                                      (vector unsigned long long)__a);
}

static __inline__ int __ATTRS_o_ai vec_all_lt(vector bool long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT, (vector unsigned long long)__b,
                                      (vector unsigned long long)__a);
}
#endif

static __inline__ int __ATTRS_o_ai vec_all_lt(vector float __a,
                                              vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvcmpgtsp_p(__CR6_LT, __b, __a);
#else
  return __builtin_altivec_vcmpgtfp_p(__CR6_LT, __b, __a);
#endif
}

#ifdef __VSX__
static __inline__ int __ATTRS_o_ai vec_all_lt(vector double __a,
                                              vector double __b) {
  return __builtin_vsx_xvcmpgtdp_p(__CR6_LT, __b, __a);
}
#endif

/* vec_all_nan */

static __inline__ int __ATTRS_o_ai vec_all_nan(vector float __a) {
#ifdef __VSX__
  return __builtin_vsx_xvcmpeqsp_p(__CR6_EQ, __a, __a);
#else
  return __builtin_altivec_vcmpeqfp_p(__CR6_EQ, __a, __a);
#endif
}

#ifdef __VSX__
static __inline__ int __ATTRS_o_ai vec_all_nan(vector double __a) {
  return __builtin_vsx_xvcmpeqdp_p(__CR6_EQ, __a, __a);
}
#endif

/* vec_all_ne */

static __inline__ int __ATTRS_o_ai vec_all_ne(vector signed char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_EQ, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector signed char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_EQ, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector unsigned char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_EQ, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector unsigned char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_EQ, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector bool char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_EQ, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector bool char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_EQ, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector bool char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_EQ, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ, __a, (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector unsigned short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector unsigned short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector bool short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector bool short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector bool short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector pixel __a,
                                              vector pixel __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector int __a, vector int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_EQ, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_EQ, __a, (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector unsigned int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_EQ, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector unsigned int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_EQ, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector bool int __a,
                                              vector int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_EQ, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector bool int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_EQ, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector bool int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_EQ, (vector int)__a,
                                      (vector int)__b);
}

#ifdef __POWER8_VECTOR__
static __inline__ int __ATTRS_o_ai vec_all_ne(vector signed long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_EQ, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector unsigned long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_EQ, (vector long long)__a,
                                      (vector long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector signed long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_EQ, __a,
                                      (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector unsigned long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_EQ, (vector signed long long)__a,
                                      (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector bool long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_EQ, (vector signed long long)__a,
                                      (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector bool long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_EQ, (vector signed long long)__a,
                                      (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_all_ne(vector bool long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_EQ, (vector signed long long)__a,
                                      (vector signed long long)__b);
}
#endif

static __inline__ int __ATTRS_o_ai vec_all_ne(vector float __a,
                                              vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvcmpeqdp_p(__CR6_EQ, __a, __b);
#else
  return __builtin_altivec_vcmpeqfp_p(__CR6_EQ, __a, __b);
#endif
}

#ifdef __VSX__
static __inline__ int __ATTRS_o_ai vec_all_ne(vector double __a,
                                              vector double __b) {
  return __builtin_vsx_xvcmpeqdp_p(__CR6_EQ, __a, __b);
}
#endif

/* vec_all_nge */

static __inline__ int __ATTRS_o_ai vec_all_nge(vector float __a,
                                               vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvcmpgesp_p(__CR6_EQ, __a, __b);
#else
  return __builtin_altivec_vcmpgefp_p(__CR6_EQ, __a, __b);
#endif
}

#ifdef __VSX__
static __inline__ int __ATTRS_o_ai vec_all_nge(vector double __a,
                                               vector double __b) {
  return __builtin_vsx_xvcmpgedp_p(__CR6_EQ, __a, __b);
}
#endif

/* vec_all_ngt */

static __inline__ int __ATTRS_o_ai vec_all_ngt(vector float __a,
                                               vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvcmpgtsp_p(__CR6_EQ, __a, __b);
#else
  return __builtin_altivec_vcmpgtfp_p(__CR6_EQ, __a, __b);
#endif
}

#ifdef __VSX__
static __inline__ int __ATTRS_o_ai vec_all_ngt(vector double __a,
                                               vector double __b) {
  return __builtin_vsx_xvcmpgtdp_p(__CR6_EQ, __a, __b);
}
#endif

/* vec_all_nle */

static __inline__ int __attribute__((__always_inline__))
vec_all_nle(vector float __a, vector float __b) {
  return __builtin_altivec_vcmpgefp_p(__CR6_EQ, __b, __a);
}

/* vec_all_nlt */

static __inline__ int __attribute__((__always_inline__))
vec_all_nlt(vector float __a, vector float __b) {
  return __builtin_altivec_vcmpgtfp_p(__CR6_EQ, __b, __a);
}

/* vec_all_numeric */

static __inline__ int __attribute__((__always_inline__))
vec_all_numeric(vector float __a) {
  return __builtin_altivec_vcmpeqfp_p(__CR6_LT, __a, __a);
}

/* vec_any_eq */

static __inline__ int __ATTRS_o_ai vec_any_eq(vector signed char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_EQ_REV, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector signed char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_EQ_REV, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector unsigned char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_EQ_REV, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector unsigned char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_EQ_REV, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector bool char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_EQ_REV, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector bool char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_EQ_REV, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector bool char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_EQ_REV, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ_REV, __a, (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector unsigned short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ_REV, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector unsigned short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ_REV, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector bool short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ_REV, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector bool short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ_REV, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector bool short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ_REV, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector pixel __a,
                                              vector pixel __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_EQ_REV, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector int __a, vector int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_EQ_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_EQ_REV, __a, (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector unsigned int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_EQ_REV, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector unsigned int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_EQ_REV, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector bool int __a,
                                              vector int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_EQ_REV, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector bool int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_EQ_REV, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector bool int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_EQ_REV, (vector int)__a,
                                      (vector int)__b);
}

#ifdef __POWER8_VECTOR__
static __inline__ int __ATTRS_o_ai vec_any_eq(vector signed long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_EQ_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector unsigned long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_EQ_REV, (vector long long)__a,
                                      (vector long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector signed long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_EQ_REV, __a,
                                      (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector unsigned long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpequd_p(
      __CR6_EQ_REV, (vector signed long long)__a, (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector bool long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpequd_p(
      __CR6_EQ_REV, (vector signed long long)__a, (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector bool long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpequd_p(
      __CR6_EQ_REV, (vector signed long long)__a, (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_eq(vector bool long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpequd_p(
      __CR6_EQ_REV, (vector signed long long)__a, (vector signed long long)__b);
}
#endif

static __inline__ int __ATTRS_o_ai vec_any_eq(vector float __a,
                                              vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvcmpeqsp_p(__CR6_EQ_REV, __a, __b);
#else
  return __builtin_altivec_vcmpeqfp_p(__CR6_EQ_REV, __a, __b);
#endif
}

#ifdef __VSX__
static __inline__ int __ATTRS_o_ai vec_any_eq(vector double __a,
                                              vector double __b) {
  return __builtin_vsx_xvcmpeqdp_p(__CR6_EQ_REV, __a, __b);
}
#endif

/* vec_any_ge */

static __inline__ int __ATTRS_o_ai vec_any_ge(vector signed char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_LT_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector signed char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_LT_REV, (vector signed char)__b,
                                      __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector unsigned char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector unsigned char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT_REV, (vector unsigned char)__b,
                                      __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector bool char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT_REV, (vector unsigned char)__b,
                                      (vector unsigned char)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector bool char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT_REV, __b,
                                      (vector unsigned char)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector bool char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT_REV, (vector unsigned char)__b,
                                      (vector unsigned char)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_LT_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_LT_REV, (vector short)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector unsigned short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector unsigned short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT_REV, (vector unsigned short)__b,
                                      __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector bool short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT_REV, (vector unsigned short)__b,
                                      (vector unsigned short)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector bool short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT_REV, __b,
                                      (vector unsigned short)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector bool short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT_REV, (vector unsigned short)__b,
                                      (vector unsigned short)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector int __a, vector int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_LT_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_LT_REV, (vector int)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector unsigned int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector unsigned int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT_REV, (vector unsigned int)__b,
                                      __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector bool int __a,
                                              vector int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT_REV, (vector unsigned int)__b,
                                      (vector unsigned int)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector bool int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT_REV, __b,
                                      (vector unsigned int)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector bool int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT_REV, (vector unsigned int)__b,
                                      (vector unsigned int)__a);
}

#ifdef __POWER8_VECTOR__
static __inline__ int __ATTRS_o_ai vec_any_ge(vector signed long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_LT_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector unsigned long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector signed long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_LT_REV,
                                      (vector signed long long)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector unsigned long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT_REV,
                                      (vector unsigned long long)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector bool long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT_REV,
                                      (vector unsigned long long)__b,
                                      (vector unsigned long long)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector bool long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT_REV, __b,
                                      (vector unsigned long long)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_ge(vector bool long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT_REV,
                                      (vector unsigned long long)__b,
                                      (vector unsigned long long)__a);
}
#endif

static __inline__ int __ATTRS_o_ai vec_any_ge(vector float __a,
                                              vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvcmpgesp_p(__CR6_EQ_REV, __a, __b);
#else
  return __builtin_altivec_vcmpgefp_p(__CR6_EQ_REV, __a, __b);
#endif
}

#ifdef __VSX__
static __inline__ int __ATTRS_o_ai vec_any_ge(vector double __a,
                                              vector double __b) {
  return __builtin_vsx_xvcmpgedp_p(__CR6_EQ_REV, __a, __b);
}
#endif

/* vec_any_gt */

static __inline__ int __ATTRS_o_ai vec_any_gt(vector signed char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_EQ_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector signed char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_EQ_REV, __a,
                                      (vector signed char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector unsigned char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector unsigned char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ_REV, __a,
                                      (vector unsigned char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector bool char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ_REV, (vector unsigned char)__a,
                                      (vector unsigned char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector bool char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ_REV, (vector unsigned char)__a,
                                      __b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector bool char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ_REV, (vector unsigned char)__a,
                                      (vector unsigned char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_EQ_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_EQ_REV, __a, (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector unsigned short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector unsigned short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ_REV, __a,
                                      (vector unsigned short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector bool short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ_REV, (vector unsigned short)__a,
                                      (vector unsigned short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector bool short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ_REV, (vector unsigned short)__a,
                                      __b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector bool short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ_REV, (vector unsigned short)__a,
                                      (vector unsigned short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector int __a, vector int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_EQ_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_EQ_REV, __a, (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector unsigned int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector unsigned int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ_REV, __a,
                                      (vector unsigned int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector bool int __a,
                                              vector int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ_REV, (vector unsigned int)__a,
                                      (vector unsigned int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector bool int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ_REV, (vector unsigned int)__a,
                                      __b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector bool int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ_REV, (vector unsigned int)__a,
                                      (vector unsigned int)__b);
}

#ifdef __POWER8_VECTOR__
static __inline__ int __ATTRS_o_ai vec_any_gt(vector signed long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_EQ_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector unsigned long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector signed long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_EQ_REV, __a,
                                      (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector unsigned long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ_REV, __a,
                                      (vector unsigned long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector bool long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ_REV,
                                      (vector unsigned long long)__a,
                                      (vector unsigned long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector bool long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ_REV,
                                      (vector unsigned long long)__a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_gt(vector bool long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ_REV,
                                      (vector unsigned long long)__a,
                                      (vector unsigned long long)__b);
}
#endif

static __inline__ int __ATTRS_o_ai vec_any_gt(vector float __a,
                                              vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvcmpgtsp_p(__CR6_EQ_REV, __a, __b);
#else
  return __builtin_altivec_vcmpgtfp_p(__CR6_EQ_REV, __a, __b);
#endif
}

#ifdef __VSX__
static __inline__ int __ATTRS_o_ai vec_any_gt(vector double __a,
                                              vector double __b) {
  return __builtin_vsx_xvcmpgtdp_p(__CR6_EQ_REV, __a, __b);
}
#endif

/* vec_any_le */

static __inline__ int __ATTRS_o_ai vec_any_le(vector signed char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_LT_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector signed char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_LT_REV, __a,
                                      (vector signed char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector unsigned char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector unsigned char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT_REV, __a,
                                      (vector unsigned char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector bool char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT_REV, (vector unsigned char)__a,
                                      (vector unsigned char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector bool char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT_REV, (vector unsigned char)__a,
                                      __b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector bool char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_LT_REV, (vector unsigned char)__a,
                                      (vector unsigned char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_LT_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_LT_REV, __a, (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector unsigned short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector unsigned short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT_REV, __a,
                                      (vector unsigned short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector bool short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT_REV, (vector unsigned short)__a,
                                      (vector unsigned short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector bool short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT_REV, (vector unsigned short)__a,
                                      __b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector bool short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_LT_REV, (vector unsigned short)__a,
                                      (vector unsigned short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector int __a, vector int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_LT_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_LT_REV, __a, (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector unsigned int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector unsigned int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT_REV, __a,
                                      (vector unsigned int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector bool int __a,
                                              vector int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT_REV, (vector unsigned int)__a,
                                      (vector unsigned int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector bool int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT_REV, (vector unsigned int)__a,
                                      __b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector bool int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_LT_REV, (vector unsigned int)__a,
                                      (vector unsigned int)__b);
}

#ifdef __POWER8_VECTOR__
static __inline__ int __ATTRS_o_ai vec_any_le(vector signed long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_LT_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector unsigned long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector signed long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_LT_REV, __a,
                                      (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector unsigned long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT_REV, __a,
                                      (vector unsigned long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector bool long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT_REV,
                                      (vector unsigned long long)__a,
                                      (vector unsigned long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector bool long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT_REV,
                                      (vector unsigned long long)__a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_le(vector bool long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_LT_REV,
                                      (vector unsigned long long)__a,
                                      (vector unsigned long long)__b);
}
#endif

static __inline__ int __ATTRS_o_ai vec_any_le(vector float __a,
                                              vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvcmpgesp_p(__CR6_EQ_REV, __b, __a);
#else
  return __builtin_altivec_vcmpgefp_p(__CR6_EQ_REV, __b, __a);
#endif
}

#ifdef __VSX__
static __inline__ int __ATTRS_o_ai vec_any_le(vector double __a,
                                              vector double __b) {
  return __builtin_vsx_xvcmpgedp_p(__CR6_EQ_REV, __b, __a);
}
#endif

/* vec_any_lt */

static __inline__ int __ATTRS_o_ai vec_any_lt(vector signed char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_EQ_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector signed char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtsb_p(__CR6_EQ_REV, (vector signed char)__b,
                                      __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector unsigned char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector unsigned char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ_REV, (vector unsigned char)__b,
                                      __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector bool char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ_REV, (vector unsigned char)__b,
                                      (vector unsigned char)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector bool char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ_REV, __b,
                                      (vector unsigned char)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector bool char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpgtub_p(__CR6_EQ_REV, (vector unsigned char)__b,
                                      (vector unsigned char)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_EQ_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtsh_p(__CR6_EQ_REV, (vector short)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector unsigned short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector unsigned short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ_REV, (vector unsigned short)__b,
                                      __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector bool short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ_REV, (vector unsigned short)__b,
                                      (vector unsigned short)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector bool short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ_REV, __b,
                                      (vector unsigned short)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector bool short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpgtuh_p(__CR6_EQ_REV, (vector unsigned short)__b,
                                      (vector unsigned short)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector int __a, vector int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_EQ_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtsw_p(__CR6_EQ_REV, (vector int)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector unsigned int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector unsigned int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ_REV, (vector unsigned int)__b,
                                      __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector bool int __a,
                                              vector int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ_REV, (vector unsigned int)__b,
                                      (vector unsigned int)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector bool int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ_REV, __b,
                                      (vector unsigned int)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector bool int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpgtuw_p(__CR6_EQ_REV, (vector unsigned int)__b,
                                      (vector unsigned int)__a);
}

#ifdef __POWER8_VECTOR__
static __inline__ int __ATTRS_o_ai vec_any_lt(vector signed long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_EQ_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector unsigned long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ_REV, __b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector signed long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtsd_p(__CR6_EQ_REV,
                                      (vector signed long long)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector unsigned long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ_REV,
                                      (vector unsigned long long)__b, __a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector bool long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ_REV,
                                      (vector unsigned long long)__b,
                                      (vector unsigned long long)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector bool long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ_REV, __b,
                                      (vector unsigned long long)__a);
}

static __inline__ int __ATTRS_o_ai vec_any_lt(vector bool long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpgtud_p(__CR6_EQ_REV,
                                      (vector unsigned long long)__b,
                                      (vector unsigned long long)__a);
}
#endif

static __inline__ int __ATTRS_o_ai vec_any_lt(vector float __a,
                                              vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvcmpgtsp_p(__CR6_EQ_REV, __b, __a);
#else
  return __builtin_altivec_vcmpgtfp_p(__CR6_EQ_REV, __b, __a);
#endif
}

#ifdef __VSX__
static __inline__ int __ATTRS_o_ai vec_any_lt(vector double __a,
                                              vector double __b) {
  return __builtin_vsx_xvcmpgtdp_p(__CR6_EQ_REV, __b, __a);
}
#endif

/* vec_any_nan */

static __inline__ int __attribute__((__always_inline__))
vec_any_nan(vector float __a) {
  return __builtin_altivec_vcmpeqfp_p(__CR6_LT_REV, __a, __a);
}

/* vec_any_ne */

static __inline__ int __ATTRS_o_ai vec_any_ne(vector signed char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_LT_REV, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector signed char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_LT_REV, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector unsigned char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_LT_REV, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector unsigned char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_LT_REV, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector bool char __a,
                                              vector signed char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_LT_REV, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector bool char __a,
                                              vector unsigned char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_LT_REV, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector bool char __a,
                                              vector bool char __b) {
  return __builtin_altivec_vcmpequb_p(__CR6_LT_REV, (vector char)__a,
                                      (vector char)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT_REV, __a, (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector unsigned short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT_REV, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector unsigned short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT_REV, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector bool short __a,
                                              vector short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT_REV, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector bool short __a,
                                              vector unsigned short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT_REV, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector bool short __a,
                                              vector bool short __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT_REV, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector pixel __a,
                                              vector pixel __b) {
  return __builtin_altivec_vcmpequh_p(__CR6_LT_REV, (vector short)__a,
                                      (vector short)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector int __a, vector int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_LT_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_LT_REV, __a, (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector unsigned int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_LT_REV, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector unsigned int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_LT_REV, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector bool int __a,
                                              vector int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_LT_REV, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector bool int __a,
                                              vector unsigned int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_LT_REV, (vector int)__a,
                                      (vector int)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector bool int __a,
                                              vector bool int __b) {
  return __builtin_altivec_vcmpequw_p(__CR6_LT_REV, (vector int)__a,
                                      (vector int)__b);
}

#ifdef __POWER8_VECTOR__
static __inline__ int __ATTRS_o_ai vec_any_ne(vector signed long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_LT_REV, __a, __b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector unsigned long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_LT_REV, (vector long long)__a,
                                      (vector long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector signed long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpequd_p(__CR6_LT_REV, __a,
                                      (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector unsigned long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpequd_p(
      __CR6_LT_REV, (vector signed long long)__a, (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector bool long long __a,
                                              vector signed long long __b) {
  return __builtin_altivec_vcmpequd_p(
      __CR6_LT_REV, (vector signed long long)__a, (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector bool long long __a,
                                              vector unsigned long long __b) {
  return __builtin_altivec_vcmpequd_p(
      __CR6_LT_REV, (vector signed long long)__a, (vector signed long long)__b);
}

static __inline__ int __ATTRS_o_ai vec_any_ne(vector bool long long __a,
                                              vector bool long long __b) {
  return __builtin_altivec_vcmpequd_p(
      __CR6_LT_REV, (vector signed long long)__a, (vector signed long long)__b);
}
#endif

static __inline__ int __ATTRS_o_ai vec_any_ne(vector float __a,
                                              vector float __b) {
#ifdef __VSX__
  return __builtin_vsx_xvcmpeqsp_p(__CR6_LT_REV, __a, __b);
#else
  return __builtin_altivec_vcmpeqfp_p(__CR6_LT_REV, __a, __b);
#endif
}

#ifdef __VSX__
static __inline__ int __ATTRS_o_ai vec_any_ne(vector double __a,
                                              vector double __b) {
  return __builtin_vsx_xvcmpeqdp_p(__CR6_LT_REV, __a, __b);
}
#endif

/* vec_any_nge */

static __inline__ int __attribute__((__always_inline__))
vec_any_nge(vector float __a, vector float __b) {
  return __builtin_altivec_vcmpgefp_p(__CR6_LT_REV, __a, __b);
}

/* vec_any_ngt */

static __inline__ int __attribute__((__always_inline__))
vec_any_ngt(vector float __a, vector float __b) {
  return __builtin_altivec_vcmpgtfp_p(__CR6_LT_REV, __a, __b);
}

/* vec_any_nle */

static __inline__ int __attribute__((__always_inline__))
vec_any_nle(vector float __a, vector float __b) {
  return __builtin_altivec_vcmpgefp_p(__CR6_LT_REV, __b, __a);
}

/* vec_any_nlt */

static __inline__ int __attribute__((__always_inline__))
vec_any_nlt(vector float __a, vector float __b) {
  return __builtin_altivec_vcmpgtfp_p(__CR6_LT_REV, __b, __a);
}

/* vec_any_numeric */

static __inline__ int __attribute__((__always_inline__))
vec_any_numeric(vector float __a) {
  return __builtin_altivec_vcmpeqfp_p(__CR6_EQ_REV, __a, __a);
}

/* vec_any_out */

static __inline__ int __attribute__((__always_inline__))
vec_any_out(vector float __a, vector float __b) {
  return __builtin_altivec_vcmpbfp_p(__CR6_EQ_REV, __a, __b);
}

/* Power 8 Crypto functions
Note: We diverge from the current GCC implementation with regard
to cryptography and related functions as follows:
- Only the SHA and AES instructions and builtins are disabled by -mno-crypto
- The remaining ones are only available on Power8 and up so
  require -mpower8-vector
The justification for this is that export requirements require that
Category:Vector.Crypto is optional (i.e. compliant hardware may not provide
support). As a result, we need to be able to turn off support for those.
The remaining ones (currently controlled by -mcrypto for GCC) still
need to be provided on compliant hardware even if Vector.Crypto is not
provided.
*/
#ifdef __CRYPTO__
#define vec_sbox_be __builtin_altivec_crypto_vsbox
#define vec_cipher_be __builtin_altivec_crypto_vcipher
#define vec_cipherlast_be __builtin_altivec_crypto_vcipherlast
#define vec_ncipher_be __builtin_altivec_crypto_vncipher
#define vec_ncipherlast_be __builtin_altivec_crypto_vncipherlast

static __inline__ vector unsigned long long __attribute__((__always_inline__))
__builtin_crypto_vsbox(vector unsigned long long __a) {
  return __builtin_altivec_crypto_vsbox(__a);
}

static __inline__ vector unsigned long long __attribute__((__always_inline__))
__builtin_crypto_vcipher(vector unsigned long long __a,
                         vector unsigned long long __b) {
  return __builtin_altivec_crypto_vcipher(__a, __b);
}

static __inline__ vector unsigned long long __attribute__((__always_inline__))
__builtin_crypto_vcipherlast(vector unsigned long long __a,
                             vector unsigned long long __b) {
  return __builtin_altivec_crypto_vcipherlast(__a, __b);
}

static __inline__ vector unsigned long long __attribute__((__always_inline__))
__builtin_crypto_vncipher(vector unsigned long long __a,
                          vector unsigned long long __b) {
  return __builtin_altivec_crypto_vncipher(__a, __b);
}

static __inline__ vector unsigned long long __attribute__((__always_inline__))
__builtin_crypto_vncipherlast(vector unsigned long long __a,
                              vector unsigned long long __b) {
  return __builtin_altivec_crypto_vncipherlast(__a, __b);
}

#define __builtin_crypto_vshasigmad __builtin_altivec_crypto_vshasigmad
#define __builtin_crypto_vshasigmaw __builtin_altivec_crypto_vshasigmaw

#define vec_shasigma_be(X, Y, Z)                                               \
  _Generic((X), vector unsigned int                                            \
           : __builtin_crypto_vshasigmaw, vector unsigned long long            \
           : __builtin_crypto_vshasigmad)((X), (Y), (Z))
#endif

#ifdef __POWER8_VECTOR__
static __inline__ vector bool char __ATTRS_o_ai
vec_permxor(vector bool char __a, vector bool char __b,
            vector bool char __c) {
  return __builtin_altivec_crypto_vpermxor(__a, __b, __c);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_permxor(vector signed char __a, vector signed char __b,
            vector signed char __c) {
  return __builtin_altivec_crypto_vpermxor(__a, __b, __c);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_permxor(vector unsigned char __a, vector unsigned char __b,
            vector unsigned char __c) {
  return __builtin_altivec_crypto_vpermxor(__a, __b, __c);
}

static __inline__ vector unsigned char __ATTRS_o_ai
__builtin_crypto_vpermxor(vector unsigned char __a, vector unsigned char __b,
                          vector unsigned char __c) {
  return __builtin_altivec_crypto_vpermxor(__a, __b, __c);
}

static __inline__ vector unsigned short __ATTRS_o_ai
__builtin_crypto_vpermxor(vector unsigned short __a, vector unsigned short __b,
                          vector unsigned short __c) {
  return (vector unsigned short)__builtin_altivec_crypto_vpermxor(
      (vector unsigned char)__a, (vector unsigned char)__b,
      (vector unsigned char)__c);
}

static __inline__ vector unsigned int __ATTRS_o_ai __builtin_crypto_vpermxor(
    vector unsigned int __a, vector unsigned int __b, vector unsigned int __c) {
  return (vector unsigned int)__builtin_altivec_crypto_vpermxor(
      (vector unsigned char)__a, (vector unsigned char)__b,
      (vector unsigned char)__c);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
__builtin_crypto_vpermxor(vector unsigned long long __a,
                          vector unsigned long long __b,
                          vector unsigned long long __c) {
  return (vector unsigned long long)__builtin_altivec_crypto_vpermxor(
      (vector unsigned char)__a, (vector unsigned char)__b,
      (vector unsigned char)__c);
}

static __inline__ vector unsigned char __ATTRS_o_ai
__builtin_crypto_vpmsumb(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_altivec_crypto_vpmsumb(__a, __b);
}

static __inline__ vector unsigned short __ATTRS_o_ai
__builtin_crypto_vpmsumb(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_altivec_crypto_vpmsumh(__a, __b);
}

static __inline__ vector unsigned int __ATTRS_o_ai
__builtin_crypto_vpmsumb(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_altivec_crypto_vpmsumw(__a, __b);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
__builtin_crypto_vpmsumb(vector unsigned long long __a,
                         vector unsigned long long __b) {
  return __builtin_altivec_crypto_vpmsumd(__a, __b);
}

static __inline__ vector signed char __ATTRS_o_ai
vec_vgbbd(vector signed char __a) {
  return __builtin_altivec_vgbbd((vector unsigned char)__a);
}

#define vec_pmsum_be __builtin_crypto_vpmsumb
#define vec_gb __builtin_altivec_vgbbd

static __inline__ vector unsigned char __ATTRS_o_ai
vec_vgbbd(vector unsigned char __a) {
  return __builtin_altivec_vgbbd(__a);
}

static __inline__ vector long long __ATTRS_o_ai
vec_vbpermq(vector signed char __a, vector signed char __b) {
  return __builtin_altivec_vbpermq((vector unsigned char)__a,
                                   (vector unsigned char)__b);
}

static __inline__ vector long long __ATTRS_o_ai
vec_vbpermq(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_altivec_vbpermq(__a, __b);
}

#ifdef __powerpc64__
static __inline__ vector unsigned long long __attribute__((__always_inline__))
vec_bperm(vector unsigned __int128 __a, vector unsigned char __b) {
  return __builtin_altivec_vbpermq((vector unsigned char)__a,
                                   (vector unsigned char)__b);
}
#endif
#endif


/* vec_reve */

static inline __ATTRS_o_ai vector bool char vec_reve(vector bool char __a) {
  return __builtin_shufflevector(__a, __a, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6,
                                 5, 4, 3, 2, 1, 0);
}

static inline __ATTRS_o_ai vector signed char vec_reve(vector signed char __a) {
  return __builtin_shufflevector(__a, __a, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6,
                                 5, 4, 3, 2, 1, 0);
}

static inline __ATTRS_o_ai vector unsigned char
vec_reve(vector unsigned char __a) {
  return __builtin_shufflevector(__a, __a, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6,
                                 5, 4, 3, 2, 1, 0);
}

static inline __ATTRS_o_ai vector bool int vec_reve(vector bool int __a) {
  return __builtin_shufflevector(__a, __a, 3, 2, 1, 0);
}

static inline __ATTRS_o_ai vector signed int vec_reve(vector signed int __a) {
  return __builtin_shufflevector(__a, __a, 3, 2, 1, 0);
}

static inline __ATTRS_o_ai vector unsigned int
vec_reve(vector unsigned int __a) {
  return __builtin_shufflevector(__a, __a, 3, 2, 1, 0);
}

static inline __ATTRS_o_ai vector bool short vec_reve(vector bool short __a) {
  return __builtin_shufflevector(__a, __a, 7, 6, 5, 4, 3, 2, 1, 0);
}

static inline __ATTRS_o_ai vector signed short
vec_reve(vector signed short __a) {
  return __builtin_shufflevector(__a, __a, 7, 6, 5, 4, 3, 2, 1, 0);
}

static inline __ATTRS_o_ai vector unsigned short
vec_reve(vector unsigned short __a) {
  return __builtin_shufflevector(__a, __a, 7, 6, 5, 4, 3, 2, 1, 0);
}

static inline __ATTRS_o_ai vector float vec_reve(vector float __a) {
  return __builtin_shufflevector(__a, __a, 3, 2, 1, 0);
}

#ifdef __VSX__
static inline __ATTRS_o_ai vector bool long long
vec_reve(vector bool long long __a) {
  return __builtin_shufflevector(__a, __a, 1, 0);
}

static inline __ATTRS_o_ai vector signed long long
vec_reve(vector signed long long __a) {
  return __builtin_shufflevector(__a, __a, 1, 0);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_reve(vector unsigned long long __a) {
  return __builtin_shufflevector(__a, __a, 1, 0);
}

static inline __ATTRS_o_ai vector double vec_reve(vector double __a) {
  return __builtin_shufflevector(__a, __a, 1, 0);
}
#endif

/* vec_revb */
static __inline__ vector bool char __ATTRS_o_ai
vec_revb(vector bool char __a) {
  return __a;
}

static __inline__ vector signed char __ATTRS_o_ai
vec_revb(vector signed char __a) {
  return __a;
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_revb(vector unsigned char __a) {
  return __a;
}

static __inline__ vector bool short __ATTRS_o_ai
vec_revb(vector bool short __a) {
  vector unsigned char __indices =
      { 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14 };
  return vec_perm(__a, __a, __indices);
}

static __inline__ vector signed short __ATTRS_o_ai
vec_revb(vector signed short __a) {
  vector unsigned char __indices =
      { 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14 };
  return vec_perm(__a, __a, __indices);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_revb(vector unsigned short __a) {
  vector unsigned char __indices =
     { 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14 };
  return vec_perm(__a, __a, __indices);
}

static __inline__ vector bool int __ATTRS_o_ai
vec_revb(vector bool int __a) {
  vector unsigned char __indices =
      { 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12 };
  return vec_perm(__a, __a, __indices);
}

static __inline__ vector signed int __ATTRS_o_ai
vec_revb(vector signed int __a) {
  vector unsigned char __indices =
      { 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12 };
  return vec_perm(__a, __a, __indices);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_revb(vector unsigned int __a) {
  vector unsigned char __indices =
      { 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12 };
  return vec_perm(__a, __a, __indices);
}

static __inline__ vector float __ATTRS_o_ai
vec_revb(vector float __a) {
 vector unsigned char __indices =
      { 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12 };
 return vec_perm(__a, __a, __indices);
}

#ifdef __VSX__
static __inline__ vector bool long long __ATTRS_o_ai
vec_revb(vector bool long long __a) {
  vector unsigned char __indices =
      { 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8 };
  return vec_perm(__a, __a, __indices);
}

static __inline__ vector signed long long __ATTRS_o_ai
vec_revb(vector signed long long __a) {
  vector unsigned char __indices =
      { 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8 };
  return vec_perm(__a, __a, __indices);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_revb(vector unsigned long long __a) {
  vector unsigned char __indices =
      { 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8 };
  return vec_perm(__a, __a, __indices);
}

static __inline__ vector double __ATTRS_o_ai
vec_revb(vector double __a) {
  vector unsigned char __indices =
      { 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8 };
  return vec_perm(__a, __a, __indices);
}
#endif /* End __VSX__ */

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
static __inline__ vector signed __int128 __ATTRS_o_ai
vec_revb(vector signed __int128 __a) {
  vector unsigned char __indices =
      { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
  return (vector signed __int128)vec_perm((vector signed int)__a,
                                          (vector signed int)__a,
                                           __indices);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_revb(vector unsigned __int128 __a) {
  vector unsigned char __indices =
      { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
  return (vector unsigned __int128)vec_perm((vector signed int)__a,
                                            (vector signed int)__a,
                                             __indices);
}
#endif /* END __POWER8_VECTOR__ && __powerpc64__ */

/* vec_xl */

typedef vector signed char unaligned_vec_schar __attribute__((aligned(1)));
typedef vector unsigned char unaligned_vec_uchar __attribute__((aligned(1)));
typedef vector signed short unaligned_vec_sshort __attribute__((aligned(1)));
typedef vector unsigned short unaligned_vec_ushort __attribute__((aligned(1)));
typedef vector signed int unaligned_vec_sint __attribute__((aligned(1)));
typedef vector unsigned int unaligned_vec_uint __attribute__((aligned(1)));
typedef vector float unaligned_vec_float __attribute__((aligned(1)));

static inline __ATTRS_o_ai vector signed char vec_xl(signed long long __offset,
                                                     signed char *__ptr) {
  return *(unaligned_vec_schar *)(__ptr + __offset);
}

static inline __ATTRS_o_ai vector unsigned char
vec_xl(signed long long __offset, unsigned char *__ptr) {
  return *(unaligned_vec_uchar*)(__ptr + __offset);
}

static inline __ATTRS_o_ai vector signed short vec_xl(signed long long __offset,
                                                      signed short *__ptr) {
  return *(unaligned_vec_sshort *)(__ptr + __offset);
}

static inline __ATTRS_o_ai vector unsigned short
vec_xl(signed long long __offset, unsigned short *__ptr) {
  return *(unaligned_vec_ushort *)(__ptr + __offset);
}

static inline __ATTRS_o_ai vector signed int vec_xl(signed long long __offset,
                                                    signed int *__ptr) {
  return *(unaligned_vec_sint *)(__ptr + __offset);
}

static inline __ATTRS_o_ai vector unsigned int vec_xl(signed long long __offset,
                                                      unsigned int *__ptr) {
  return *(unaligned_vec_uint *)(__ptr + __offset);
}

static inline __ATTRS_o_ai vector float vec_xl(signed long long __offset,
                                               float *__ptr) {
  return *(unaligned_vec_float *)(__ptr + __offset);
}

#ifdef __VSX__
typedef vector signed long long unaligned_vec_sll __attribute__((aligned(1)));
typedef vector unsigned long long unaligned_vec_ull __attribute__((aligned(1)));
typedef vector double unaligned_vec_double __attribute__((aligned(1)));

static inline __ATTRS_o_ai vector signed long long
vec_xl(signed long long __offset, signed long long *__ptr) {
  return *(unaligned_vec_sll *)(__ptr + __offset);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_xl(signed long long __offset, unsigned long long *__ptr) {
  return *(unaligned_vec_ull *)(__ptr + __offset);
}

static inline __ATTRS_o_ai vector double vec_xl(signed long long __offset,
                                                double *__ptr) {
  return *(unaligned_vec_double *)(__ptr + __offset);
}
#endif

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
typedef vector signed __int128 unaligned_vec_si128 __attribute__((aligned(1)));
typedef vector unsigned __int128 unaligned_vec_ui128
    __attribute__((aligned(1)));
static inline __ATTRS_o_ai vector signed __int128
vec_xl(signed long long __offset, signed __int128 *__ptr) {
  return *(unaligned_vec_si128 *)(__ptr + __offset);
}

static inline __ATTRS_o_ai vector unsigned __int128
vec_xl(signed long long __offset, unsigned __int128 *__ptr) {
  return *(unaligned_vec_ui128 *)(__ptr + __offset);
}
#endif

/* vec_xl_be */

#ifdef __LITTLE_ENDIAN__
static __inline__ vector signed char __ATTRS_o_ai
vec_xl_be(signed long long __offset, signed char *__ptr) {
  vector signed char __vec = __builtin_vsx_lxvd2x_be(__offset, __ptr);
  return __builtin_shufflevector(__vec, __vec, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14,
                                 13, 12, 11, 10, 9, 8);
}

static __inline__ vector unsigned char __ATTRS_o_ai
vec_xl_be(signed long long __offset, unsigned char *__ptr) {
  vector unsigned char __vec = __builtin_vsx_lxvd2x_be(__offset, __ptr);
  return __builtin_shufflevector(__vec, __vec, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14,
                                 13, 12, 11, 10, 9, 8);
}

static __inline__ vector signed short  __ATTRS_o_ai
vec_xl_be(signed long long __offset, signed short *__ptr) {
  vector signed short __vec = __builtin_vsx_lxvd2x_be(__offset, __ptr);
  return __builtin_shufflevector(__vec, __vec, 3, 2, 1, 0, 7, 6, 5, 4);
}

static __inline__ vector unsigned short __ATTRS_o_ai
vec_xl_be(signed long long __offset, unsigned short *__ptr) {
  vector unsigned short __vec = __builtin_vsx_lxvd2x_be(__offset, __ptr);
  return __builtin_shufflevector(__vec, __vec, 3, 2, 1, 0, 7, 6, 5, 4);
}

static __inline__ vector signed int __ATTRS_o_ai
vec_xl_be(signed long long  __offset, signed int *__ptr) {
  return (vector signed int)__builtin_vsx_lxvw4x_be(__offset, __ptr);
}

static __inline__ vector unsigned int __ATTRS_o_ai
vec_xl_be(signed long long  __offset, unsigned int *__ptr) {
  return (vector unsigned int)__builtin_vsx_lxvw4x_be(__offset, __ptr);
}

static __inline__ vector float __ATTRS_o_ai
vec_xl_be(signed long long  __offset, float *__ptr) {
  return (vector float)__builtin_vsx_lxvw4x_be(__offset, __ptr);
}

#ifdef __VSX__
static __inline__ vector signed long long __ATTRS_o_ai
vec_xl_be(signed long long  __offset, signed long long *__ptr) {
  return (vector signed long long)__builtin_vsx_lxvd2x_be(__offset, __ptr);
}

static __inline__ vector unsigned long long __ATTRS_o_ai
vec_xl_be(signed long long  __offset, unsigned long long *__ptr) {
  return (vector unsigned long long)__builtin_vsx_lxvd2x_be(__offset, __ptr);
}

static __inline__ vector double __ATTRS_o_ai
vec_xl_be(signed long long  __offset, double *__ptr) {
  return (vector double)__builtin_vsx_lxvd2x_be(__offset, __ptr);
}
#endif

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
static __inline__ vector signed __int128 __ATTRS_o_ai
vec_xl_be(signed long long  __offset, signed __int128 *__ptr) {
  return vec_xl(__offset, __ptr);
}

static __inline__ vector unsigned __int128 __ATTRS_o_ai
vec_xl_be(signed long long  __offset, unsigned __int128 *__ptr) {
  return vec_xl(__offset, __ptr);
}
#endif
#else
  #define vec_xl_be vec_xl
#endif

/* vec_xst */

static inline __ATTRS_o_ai void vec_xst(vector signed char __vec,
                                        signed long long __offset,
                                        signed char *__ptr) {
  *(unaligned_vec_schar *)(__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void vec_xst(vector unsigned char __vec,
                                        signed long long __offset,
                                        unsigned char *__ptr) {
  *(unaligned_vec_uchar *)(__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void vec_xst(vector signed short __vec,
                                        signed long long __offset,
                                        signed short *__ptr) {
  *(unaligned_vec_sshort *)(__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void vec_xst(vector unsigned short __vec,
                                        signed long long __offset,
                                        unsigned short *__ptr) {
  *(unaligned_vec_ushort *)(__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void vec_xst(vector signed int __vec,
                                        signed long long __offset,
                                        signed int *__ptr) {
  *(unaligned_vec_sint *)(__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void vec_xst(vector unsigned int __vec,
                                        signed long long __offset,
                                        unsigned int *__ptr) {
  *(unaligned_vec_uint *)(__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void vec_xst(vector float __vec,
                                        signed long long __offset,
                                        float *__ptr) {
  *(unaligned_vec_float *)(__ptr + __offset) = __vec;
}

#ifdef __VSX__
static inline __ATTRS_o_ai void vec_xst(vector signed long long __vec,
                                        signed long long __offset,
                                        signed long long *__ptr) {
  *(unaligned_vec_sll *)(__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void vec_xst(vector unsigned long long __vec,
                                        signed long long __offset,
                                        unsigned long long *__ptr) {
  *(unaligned_vec_ull *)(__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void vec_xst(vector double __vec,
                                        signed long long __offset,
                                        double *__ptr) {
  *(unaligned_vec_double *)(__ptr + __offset) = __vec;
}
#endif

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
static inline __ATTRS_o_ai void vec_xst(vector signed __int128 __vec,
                                        signed long long __offset,
                                        signed __int128 *__ptr) {
  *(unaligned_vec_si128 *)(__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void vec_xst(vector unsigned __int128 __vec,
                                        signed long long __offset,
                                        unsigned __int128 *__ptr) {
  *(unaligned_vec_ui128 *)(__ptr + __offset) = __vec;
}
#endif

/* vec_xst_be */

#ifdef __LITTLE_ENDIAN__
static __inline__ void __ATTRS_o_ai vec_xst_be(vector signed char __vec,
                                               signed long long  __offset,
                                               signed char *__ptr) {
  vector signed char __tmp =
     __builtin_shufflevector(__vec, __vec, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14,
                             13, 12, 11, 10, 9, 8);
  __builtin_vsx_stxvd2x_be(__tmp, __offset, __ptr);
}

static __inline__ void __ATTRS_o_ai vec_xst_be(vector unsigned char __vec,
                                               signed long long  __offset,
                                               unsigned char *__ptr) {
  vector unsigned char __tmp =
     __builtin_shufflevector(__vec, __vec, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14,
                             13, 12, 11, 10, 9, 8);
  __builtin_vsx_stxvd2x_be(__tmp, __offset, __ptr);
}

static __inline__ void __ATTRS_o_ai vec_xst_be(vector signed short __vec,
                                               signed long long  __offset,
                                               signed short *__ptr) {
  vector signed short __tmp =
     __builtin_shufflevector(__vec, __vec, 3, 2, 1, 0, 7, 6, 5, 4);
  __builtin_vsx_stxvd2x_be(__tmp, __offset, __ptr);
}

static __inline__ void __ATTRS_o_ai vec_xst_be(vector unsigned short __vec,
                                               signed long long  __offset,
                                               unsigned short *__ptr) {
  vector unsigned short __tmp =
     __builtin_shufflevector(__vec, __vec, 3, 2, 1, 0, 7, 6, 5, 4);
  __builtin_vsx_stxvd2x_be(__tmp, __offset, __ptr);
}

static __inline__ void __ATTRS_o_ai vec_xst_be(vector signed int __vec,
                                               signed long long  __offset,
                                               signed int *__ptr) {
  __builtin_vsx_stxvw4x_be(__vec, __offset, __ptr);
}

static __inline__ void __ATTRS_o_ai vec_xst_be(vector unsigned int __vec,
                                               signed long long  __offset,
                                               unsigned int *__ptr) {
  __builtin_vsx_stxvw4x_be(__vec, __offset, __ptr);
}

static __inline__ void __ATTRS_o_ai vec_xst_be(vector float __vec,
                                               signed long long  __offset,
                                               float *__ptr) {
  __builtin_vsx_stxvw4x_be(__vec, __offset, __ptr);
}

#ifdef __VSX__
static __inline__ void __ATTRS_o_ai vec_xst_be(vector signed long long __vec,
                                               signed long long  __offset,
                                               signed long long *__ptr) {
  __builtin_vsx_stxvd2x_be(__vec, __offset, __ptr);
}

static __inline__ void __ATTRS_o_ai vec_xst_be(vector unsigned long long __vec,
                                               signed long long  __offset,
                                               unsigned long long *__ptr) {
  __builtin_vsx_stxvd2x_be(__vec, __offset, __ptr);
}

static __inline__ void __ATTRS_o_ai vec_xst_be(vector double __vec,
                                               signed long long  __offset,
                                               double *__ptr) {
  __builtin_vsx_stxvd2x_be(__vec, __offset, __ptr);
}
#endif

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
static __inline__ void __ATTRS_o_ai vec_xst_be(vector signed __int128 __vec,
                                               signed long long  __offset,
                                               signed __int128 *__ptr) {
  vec_xst(__vec, __offset, __ptr);
}

static __inline__ void __ATTRS_o_ai vec_xst_be(vector unsigned __int128 __vec,
                                               signed long long  __offset,
                                               unsigned __int128 *__ptr) {
  vec_xst(__vec, __offset, __ptr);
}
#endif
#else
  #define vec_xst_be vec_xst
#endif

#ifdef __POWER9_VECTOR__
#define vec_test_data_class(__a, __b)                                      \
        _Generic((__a),                                                    \
           vector float:                                                   \
             (vector bool int)__builtin_vsx_xvtstdcsp((__a), (__b)),       \
           vector double:                                                  \
             (vector bool long long)__builtin_vsx_xvtstdcdp((__a), (__b))  \
        )

#endif /* #ifdef __POWER9_VECTOR__ */

static vector float __ATTRS_o_ai vec_neg(vector float __a) {
  return -__a;
}

#ifdef __VSX__
static vector double __ATTRS_o_ai vec_neg(vector double __a) {
  return -__a;
}

#endif

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
static vector long long __ATTRS_o_ai vec_neg(vector long long __a) {
  return -__a;
}
#endif

static vector signed int __ATTRS_o_ai vec_neg(vector signed int __a) {
  return -__a;
}

static vector signed short __ATTRS_o_ai vec_neg(vector signed short __a) {
  return -__a;
}

static vector signed char __ATTRS_o_ai vec_neg(vector signed char __a) {
  return -__a;
}

static vector float __ATTRS_o_ai vec_nabs(vector float __a) {
  return - vec_abs(__a);
}

#ifdef __VSX__
static vector double __ATTRS_o_ai vec_nabs(vector double __a) {
  return - vec_abs(__a);
}

#endif

#if defined(__POWER8_VECTOR__) && defined(__powerpc64__)
static vector long long __ATTRS_o_ai vec_nabs(vector long long __a) {
  return __builtin_altivec_vminsd(__a, -__a);
}
#endif

static vector signed int __ATTRS_o_ai vec_nabs(vector signed int __a) {
  return __builtin_altivec_vminsw(__a, -__a);
}

static vector signed short __ATTRS_o_ai vec_nabs(vector signed short __a) {
  return __builtin_altivec_vminsh(__a, -__a);
}

static vector signed char __ATTRS_o_ai vec_nabs(vector signed char __a) {
  return __builtin_altivec_vminsb(__a, -__a);
}
#undef __ATTRS_o_ai

#endif /* __ALTIVEC_H */
