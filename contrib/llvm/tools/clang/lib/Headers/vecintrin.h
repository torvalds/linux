/*===---- vecintrin.h - Vector intrinsics ----------------------------------===
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

#if defined(__s390x__) && defined(__VEC__)

#define __ATTRS_ai __attribute__((__always_inline__))
#define __ATTRS_o __attribute__((__overloadable__))
#define __ATTRS_o_ai __attribute__((__overloadable__, __always_inline__))

#define __constant(PARM) \
  __attribute__((__enable_if__ ((PARM) == (PARM), \
     "argument must be a constant integer")))
#define __constant_range(PARM, LOW, HIGH) \
  __attribute__((__enable_if__ ((PARM) >= (LOW) && (PARM) <= (HIGH), \
     "argument must be a constant integer from " #LOW " to " #HIGH)))
#define __constant_pow2_range(PARM, LOW, HIGH) \
  __attribute__((__enable_if__ ((PARM) >= (LOW) && (PARM) <= (HIGH) && \
                                ((PARM) & ((PARM) - 1)) == 0, \
     "argument must be a constant power of 2 from " #LOW " to " #HIGH)))

/*-- __lcbb -----------------------------------------------------------------*/

extern __ATTRS_o unsigned int
__lcbb(const void *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

#define __lcbb(X, Y) ((__typeof__((__lcbb)((X), (Y)))) \
  __builtin_s390_lcbb((X), __builtin_constant_p((Y))? \
                           ((Y) == 64 ? 0 : \
                            (Y) == 128 ? 1 : \
                            (Y) == 256 ? 2 : \
                            (Y) == 512 ? 3 : \
                            (Y) == 1024 ? 4 : \
                            (Y) == 2048 ? 5 : \
                            (Y) == 4096 ? 6 : 0) : 0))

/*-- vec_extract ------------------------------------------------------------*/

static inline __ATTRS_o_ai signed char
vec_extract(vector signed char __vec, int __index) {
  return __vec[__index & 15];
}

static inline __ATTRS_o_ai unsigned char
vec_extract(vector bool char __vec, int __index) {
  return __vec[__index & 15];
}

static inline __ATTRS_o_ai unsigned char
vec_extract(vector unsigned char __vec, int __index) {
  return __vec[__index & 15];
}

static inline __ATTRS_o_ai signed short
vec_extract(vector signed short __vec, int __index) {
  return __vec[__index & 7];
}

static inline __ATTRS_o_ai unsigned short
vec_extract(vector bool short __vec, int __index) {
  return __vec[__index & 7];
}

static inline __ATTRS_o_ai unsigned short
vec_extract(vector unsigned short __vec, int __index) {
  return __vec[__index & 7];
}

static inline __ATTRS_o_ai signed int
vec_extract(vector signed int __vec, int __index) {
  return __vec[__index & 3];
}

static inline __ATTRS_o_ai unsigned int
vec_extract(vector bool int __vec, int __index) {
  return __vec[__index & 3];
}

static inline __ATTRS_o_ai unsigned int
vec_extract(vector unsigned int __vec, int __index) {
  return __vec[__index & 3];
}

static inline __ATTRS_o_ai signed long long
vec_extract(vector signed long long __vec, int __index) {
  return __vec[__index & 1];
}

static inline __ATTRS_o_ai unsigned long long
vec_extract(vector bool long long __vec, int __index) {
  return __vec[__index & 1];
}

static inline __ATTRS_o_ai unsigned long long
vec_extract(vector unsigned long long __vec, int __index) {
  return __vec[__index & 1];
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai float
vec_extract(vector float __vec, int __index) {
  return __vec[__index & 3];
}
#endif

static inline __ATTRS_o_ai double
vec_extract(vector double __vec, int __index) {
  return __vec[__index & 1];
}

/*-- vec_insert -------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_insert(signed char __scalar, vector signed char __vec, int __index) {
  __vec[__index & 15] = __scalar;
  return __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_insert(unsigned char __scalar, vector bool char __vec, int __index) {
  vector unsigned char __newvec = (vector unsigned char)__vec;
  __newvec[__index & 15] = (unsigned char)__scalar;
  return __newvec;
}

static inline __ATTRS_o_ai vector unsigned char
vec_insert(unsigned char __scalar, vector unsigned char __vec, int __index) {
  __vec[__index & 15] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai vector signed short
vec_insert(signed short __scalar, vector signed short __vec, int __index) {
  __vec[__index & 7] = __scalar;
  return __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_insert(unsigned short __scalar, vector bool short __vec, int __index) {
  vector unsigned short __newvec = (vector unsigned short)__vec;
  __newvec[__index & 7] = (unsigned short)__scalar;
  return __newvec;
}

static inline __ATTRS_o_ai vector unsigned short
vec_insert(unsigned short __scalar, vector unsigned short __vec, int __index) {
  __vec[__index & 7] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai vector signed int
vec_insert(signed int __scalar, vector signed int __vec, int __index) {
  __vec[__index & 3] = __scalar;
  return __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_insert(unsigned int __scalar, vector bool int __vec, int __index) {
  vector unsigned int __newvec = (vector unsigned int)__vec;
  __newvec[__index & 3] = __scalar;
  return __newvec;
}

static inline __ATTRS_o_ai vector unsigned int
vec_insert(unsigned int __scalar, vector unsigned int __vec, int __index) {
  __vec[__index & 3] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai vector signed long long
vec_insert(signed long long __scalar, vector signed long long __vec,
           int __index) {
  __vec[__index & 1] = __scalar;
  return __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_insert(unsigned long long __scalar, vector bool long long __vec,
           int __index) {
  vector unsigned long long __newvec = (vector unsigned long long)__vec;
  __newvec[__index & 1] = __scalar;
  return __newvec;
}

static inline __ATTRS_o_ai vector unsigned long long
vec_insert(unsigned long long __scalar, vector unsigned long long __vec,
           int __index) {
  __vec[__index & 1] = __scalar;
  return __vec;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_insert(float __scalar, vector float __vec, int __index) {
  __vec[__index & 1] = __scalar;
  return __vec;
}
#endif

static inline __ATTRS_o_ai vector double
vec_insert(double __scalar, vector double __vec, int __index) {
  __vec[__index & 1] = __scalar;
  return __vec;
}

/*-- vec_promote ------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_promote(signed char __scalar, int __index) {
  const vector signed char __zero = (vector signed char)0;
  vector signed char __vec = __builtin_shufflevector(__zero, __zero,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
  __vec[__index & 15] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai vector unsigned char
vec_promote(unsigned char __scalar, int __index) {
  const vector unsigned char __zero = (vector unsigned char)0;
  vector unsigned char __vec = __builtin_shufflevector(__zero, __zero,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
  __vec[__index & 15] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai vector signed short
vec_promote(signed short __scalar, int __index) {
  const vector signed short __zero = (vector signed short)0;
  vector signed short __vec = __builtin_shufflevector(__zero, __zero,
                                -1, -1, -1, -1, -1, -1, -1, -1);
  __vec[__index & 7] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai vector unsigned short
vec_promote(unsigned short __scalar, int __index) {
  const vector unsigned short __zero = (vector unsigned short)0;
  vector unsigned short __vec = __builtin_shufflevector(__zero, __zero,
                                  -1, -1, -1, -1, -1, -1, -1, -1);
  __vec[__index & 7] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai vector signed int
vec_promote(signed int __scalar, int __index) {
  const vector signed int __zero = (vector signed int)0;
  vector signed int __vec = __builtin_shufflevector(__zero, __zero,
                                                    -1, -1, -1, -1);
  __vec[__index & 3] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai vector unsigned int
vec_promote(unsigned int __scalar, int __index) {
  const vector unsigned int __zero = (vector unsigned int)0;
  vector unsigned int __vec = __builtin_shufflevector(__zero, __zero,
                                                      -1, -1, -1, -1);
  __vec[__index & 3] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai vector signed long long
vec_promote(signed long long __scalar, int __index) {
  const vector signed long long __zero = (vector signed long long)0;
  vector signed long long __vec = __builtin_shufflevector(__zero, __zero,
                                                          -1, -1);
  __vec[__index & 1] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai vector unsigned long long
vec_promote(unsigned long long __scalar, int __index) {
  const vector unsigned long long __zero = (vector unsigned long long)0;
  vector unsigned long long __vec = __builtin_shufflevector(__zero, __zero,
                                                            -1, -1);
  __vec[__index & 1] = __scalar;
  return __vec;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_promote(float __scalar, int __index) {
  const vector float __zero = (vector float)0;
  vector float __vec = __builtin_shufflevector(__zero, __zero, -1, -1, -1, -1);
  __vec[__index & 3] = __scalar;
  return __vec;
}
#endif

static inline __ATTRS_o_ai vector double
vec_promote(double __scalar, int __index) {
  const vector double __zero = (vector double)0;
  vector double __vec = __builtin_shufflevector(__zero, __zero, -1, -1);
  __vec[__index & 1] = __scalar;
  return __vec;
}

/*-- vec_insert_and_zero ----------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_insert_and_zero(const signed char *__ptr) {
  vector signed char __vec = (vector signed char)0;
  __vec[7] = *__ptr;
  return __vec;
}

static inline __ATTRS_o_ai vector unsigned char
vec_insert_and_zero(const unsigned char *__ptr) {
  vector unsigned char __vec = (vector unsigned char)0;
  __vec[7] = *__ptr;
  return __vec;
}

static inline __ATTRS_o_ai vector signed short
vec_insert_and_zero(const signed short *__ptr) {
  vector signed short __vec = (vector signed short)0;
  __vec[3] = *__ptr;
  return __vec;
}

static inline __ATTRS_o_ai vector unsigned short
vec_insert_and_zero(const unsigned short *__ptr) {
  vector unsigned short __vec = (vector unsigned short)0;
  __vec[3] = *__ptr;
  return __vec;
}

static inline __ATTRS_o_ai vector signed int
vec_insert_and_zero(const signed int *__ptr) {
  vector signed int __vec = (vector signed int)0;
  __vec[1] = *__ptr;
  return __vec;
}

static inline __ATTRS_o_ai vector unsigned int
vec_insert_and_zero(const unsigned int *__ptr) {
  vector unsigned int __vec = (vector unsigned int)0;
  __vec[1] = *__ptr;
  return __vec;
}

static inline __ATTRS_o_ai vector signed long long
vec_insert_and_zero(const signed long long *__ptr) {
  vector signed long long __vec = (vector signed long long)0;
  __vec[0] = *__ptr;
  return __vec;
}

static inline __ATTRS_o_ai vector unsigned long long
vec_insert_and_zero(const unsigned long long *__ptr) {
  vector unsigned long long __vec = (vector unsigned long long)0;
  __vec[0] = *__ptr;
  return __vec;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_insert_and_zero(const float *__ptr) {
  vector float __vec = (vector float)0;
  __vec[1] = *__ptr;
  return __vec;
}
#endif

static inline __ATTRS_o_ai vector double
vec_insert_and_zero(const double *__ptr) {
  vector double __vec = (vector double)0;
  __vec[0] = *__ptr;
  return __vec;
}

/*-- vec_perm ---------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_perm(vector signed char __a, vector signed char __b,
         vector unsigned char __c) {
  return (vector signed char)__builtin_s390_vperm(
           (vector unsigned char)__a, (vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai vector unsigned char
vec_perm(vector unsigned char __a, vector unsigned char __b,
         vector unsigned char __c) {
  return (vector unsigned char)__builtin_s390_vperm(
           (vector unsigned char)__a, (vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai vector bool char
vec_perm(vector bool char __a, vector bool char __b,
         vector unsigned char __c) {
  return (vector bool char)__builtin_s390_vperm(
           (vector unsigned char)__a, (vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai vector signed short
vec_perm(vector signed short __a, vector signed short __b,
         vector unsigned char __c) {
  return (vector signed short)__builtin_s390_vperm(
           (vector unsigned char)__a, (vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai vector unsigned short
vec_perm(vector unsigned short __a, vector unsigned short __b,
         vector unsigned char __c) {
  return (vector unsigned short)__builtin_s390_vperm(
           (vector unsigned char)__a, (vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai vector bool short
vec_perm(vector bool short __a, vector bool short __b,
         vector unsigned char __c) {
  return (vector bool short)__builtin_s390_vperm(
           (vector unsigned char)__a, (vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai vector signed int
vec_perm(vector signed int __a, vector signed int __b,
         vector unsigned char __c) {
  return (vector signed int)__builtin_s390_vperm(
           (vector unsigned char)__a, (vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai vector unsigned int
vec_perm(vector unsigned int __a, vector unsigned int __b,
         vector unsigned char __c) {
  return (vector unsigned int)__builtin_s390_vperm(
           (vector unsigned char)__a, (vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai vector bool int
vec_perm(vector bool int __a, vector bool int __b,
         vector unsigned char __c) {
  return (vector bool int)__builtin_s390_vperm(
           (vector unsigned char)__a, (vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai vector signed long long
vec_perm(vector signed long long __a, vector signed long long __b,
         vector unsigned char __c) {
  return (vector signed long long)__builtin_s390_vperm(
           (vector unsigned char)__a, (vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_perm(vector unsigned long long __a, vector unsigned long long __b,
         vector unsigned char __c) {
  return (vector unsigned long long)__builtin_s390_vperm(
           (vector unsigned char)__a, (vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai vector bool long long
vec_perm(vector bool long long __a, vector bool long long __b,
         vector unsigned char __c) {
  return (vector bool long long)__builtin_s390_vperm(
           (vector unsigned char)__a, (vector unsigned char)__b, __c);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_perm(vector float __a, vector float __b,
         vector unsigned char __c) {
  return (vector float)__builtin_s390_vperm(
           (vector unsigned char)__a, (vector unsigned char)__b, __c);
}
#endif

static inline __ATTRS_o_ai vector double
vec_perm(vector double __a, vector double __b,
         vector unsigned char __c) {
  return (vector double)__builtin_s390_vperm(
           (vector unsigned char)__a, (vector unsigned char)__b, __c);
}

/*-- vec_permi --------------------------------------------------------------*/

// This prototype is deprecated.
extern __ATTRS_o vector signed long long
vec_permi(vector signed long long __a, vector signed long long __b, int __c)
  __constant_range(__c, 0, 3);

// This prototype is deprecated.
extern __ATTRS_o vector unsigned long long
vec_permi(vector unsigned long long __a, vector unsigned long long __b, int __c)
  __constant_range(__c, 0, 3);

// This prototype is deprecated.
extern __ATTRS_o vector bool long long
vec_permi(vector bool long long __a, vector bool long long __b, int __c)
  __constant_range(__c, 0, 3);

// This prototype is deprecated.
extern __ATTRS_o vector double
vec_permi(vector double __a, vector double __b, int __c)
  __constant_range(__c, 0, 3);

#define vec_permi(X, Y, Z) ((__typeof__((vec_permi)((X), (Y), (Z)))) \
  __builtin_s390_vpdi((vector unsigned long long)(X), \
                      (vector unsigned long long)(Y), \
                      (((Z) & 2) << 1) | ((Z) & 1)))

/*-- vec_bperm_u128 ---------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_ai vector unsigned long long
vec_bperm_u128(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vbperm(__a, __b);
}
#endif

/*-- vec_sel ----------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_sel(vector signed char __a, vector signed char __b,
        vector unsigned char __c) {
  return ((vector signed char)__c & __b) | (~(vector signed char)__c & __a);
}

static inline __ATTRS_o_ai vector signed char
vec_sel(vector signed char __a, vector signed char __b, vector bool char __c) {
  return ((vector signed char)__c & __b) | (~(vector signed char)__c & __a);
}

static inline __ATTRS_o_ai vector bool char
vec_sel(vector bool char __a, vector bool char __b, vector unsigned char __c) {
  return ((vector bool char)__c & __b) | (~(vector bool char)__c & __a);
}

static inline __ATTRS_o_ai vector bool char
vec_sel(vector bool char __a, vector bool char __b, vector bool char __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai vector unsigned char
vec_sel(vector unsigned char __a, vector unsigned char __b,
        vector unsigned char __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai vector unsigned char
vec_sel(vector unsigned char __a, vector unsigned char __b,
        vector bool char __c) {
  return ((vector unsigned char)__c & __b) | (~(vector unsigned char)__c & __a);
}

static inline __ATTRS_o_ai vector signed short
vec_sel(vector signed short __a, vector signed short __b,
        vector unsigned short __c) {
  return ((vector signed short)__c & __b) | (~(vector signed short)__c & __a);
}

static inline __ATTRS_o_ai vector signed short
vec_sel(vector signed short __a, vector signed short __b,
        vector bool short __c) {
  return ((vector signed short)__c & __b) | (~(vector signed short)__c & __a);
}

static inline __ATTRS_o_ai vector bool short
vec_sel(vector bool short __a, vector bool short __b,
        vector unsigned short __c) {
  return ((vector bool short)__c & __b) | (~(vector bool short)__c & __a);
}

static inline __ATTRS_o_ai vector bool short
vec_sel(vector bool short __a, vector bool short __b, vector bool short __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai vector unsigned short
vec_sel(vector unsigned short __a, vector unsigned short __b,
        vector unsigned short __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai vector unsigned short
vec_sel(vector unsigned short __a, vector unsigned short __b,
        vector bool short __c) {
  return (((vector unsigned short)__c & __b) |
          (~(vector unsigned short)__c & __a));
}

static inline __ATTRS_o_ai vector signed int
vec_sel(vector signed int __a, vector signed int __b,
        vector unsigned int __c) {
  return ((vector signed int)__c & __b) | (~(vector signed int)__c & __a);
}

static inline __ATTRS_o_ai vector signed int
vec_sel(vector signed int __a, vector signed int __b, vector bool int __c) {
  return ((vector signed int)__c & __b) | (~(vector signed int)__c & __a);
}

static inline __ATTRS_o_ai vector bool int
vec_sel(vector bool int __a, vector bool int __b, vector unsigned int __c) {
  return ((vector bool int)__c & __b) | (~(vector bool int)__c & __a);
}

static inline __ATTRS_o_ai vector bool int
vec_sel(vector bool int __a, vector bool int __b, vector bool int __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai vector unsigned int
vec_sel(vector unsigned int __a, vector unsigned int __b,
        vector unsigned int __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai vector unsigned int
vec_sel(vector unsigned int __a, vector unsigned int __b, vector bool int __c) {
  return ((vector unsigned int)__c & __b) | (~(vector unsigned int)__c & __a);
}

static inline __ATTRS_o_ai vector signed long long
vec_sel(vector signed long long __a, vector signed long long __b,
        vector unsigned long long __c) {
  return (((vector signed long long)__c & __b) |
          (~(vector signed long long)__c & __a));
}

static inline __ATTRS_o_ai vector signed long long
vec_sel(vector signed long long __a, vector signed long long __b,
        vector bool long long __c) {
  return (((vector signed long long)__c & __b) |
          (~(vector signed long long)__c & __a));
}

static inline __ATTRS_o_ai vector bool long long
vec_sel(vector bool long long __a, vector bool long long __b,
        vector unsigned long long __c) {
  return (((vector bool long long)__c & __b) |
          (~(vector bool long long)__c & __a));
}

static inline __ATTRS_o_ai vector bool long long
vec_sel(vector bool long long __a, vector bool long long __b,
        vector bool long long __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_sel(vector unsigned long long __a, vector unsigned long long __b,
        vector unsigned long long __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_sel(vector unsigned long long __a, vector unsigned long long __b,
        vector bool long long __c) {
  return (((vector unsigned long long)__c & __b) |
          (~(vector unsigned long long)__c & __a));
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_sel(vector float __a, vector float __b, vector unsigned int __c) {
  return (vector float)((__c & (vector unsigned int)__b) |
                        (~__c & (vector unsigned int)__a));
}

static inline __ATTRS_o_ai vector float
vec_sel(vector float __a, vector float __b, vector bool int __c) {
  vector unsigned int __ac = (vector unsigned int)__a;
  vector unsigned int __bc = (vector unsigned int)__b;
  vector unsigned int __cc = (vector unsigned int)__c;
  return (vector float)((__cc & __bc) | (~__cc & __ac));
}
#endif

static inline __ATTRS_o_ai vector double
vec_sel(vector double __a, vector double __b, vector unsigned long long __c) {
  return (vector double)((__c & (vector unsigned long long)__b) |
                         (~__c & (vector unsigned long long)__a));
}

static inline __ATTRS_o_ai vector double
vec_sel(vector double __a, vector double __b, vector bool long long __c) {
  vector unsigned long long __ac = (vector unsigned long long)__a;
  vector unsigned long long __bc = (vector unsigned long long)__b;
  vector unsigned long long __cc = (vector unsigned long long)__c;
  return (vector double)((__cc & __bc) | (~__cc & __ac));
}

/*-- vec_gather_element -----------------------------------------------------*/

static inline __ATTRS_o_ai vector signed int
vec_gather_element(vector signed int __vec, vector unsigned int __offset,
                   const signed int *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  __vec[__index] = *(const signed int *)(
    (__INTPTR_TYPE__)__ptr + (__INTPTR_TYPE__)__offset[__index]);
  return __vec;
}

static inline __ATTRS_o_ai vector bool int
vec_gather_element(vector bool int __vec, vector unsigned int __offset,
                   const unsigned int *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  __vec[__index] = *(const unsigned int *)(
    (__INTPTR_TYPE__)__ptr + (__INTPTR_TYPE__)__offset[__index]);
  return __vec;
}

static inline __ATTRS_o_ai vector unsigned int
vec_gather_element(vector unsigned int __vec, vector unsigned int __offset,
                   const unsigned int *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  __vec[__index] = *(const unsigned int *)(
    (__INTPTR_TYPE__)__ptr + (__INTPTR_TYPE__)__offset[__index]);
  return __vec;
}

static inline __ATTRS_o_ai vector signed long long
vec_gather_element(vector signed long long __vec,
                   vector unsigned long long __offset,
                   const signed long long *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  __vec[__index] = *(const signed long long *)(
    (__INTPTR_TYPE__)__ptr + (__INTPTR_TYPE__)__offset[__index]);
  return __vec;
}

static inline __ATTRS_o_ai vector bool long long
vec_gather_element(vector bool long long __vec,
                   vector unsigned long long __offset,
                   const unsigned long long *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  __vec[__index] = *(const unsigned long long *)(
    (__INTPTR_TYPE__)__ptr + (__INTPTR_TYPE__)__offset[__index]);
  return __vec;
}

static inline __ATTRS_o_ai vector unsigned long long
vec_gather_element(vector unsigned long long __vec,
                   vector unsigned long long __offset,
                   const unsigned long long *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  __vec[__index] = *(const unsigned long long *)(
    (__INTPTR_TYPE__)__ptr + (__INTPTR_TYPE__)__offset[__index]);
  return __vec;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_gather_element(vector float __vec, vector unsigned int __offset,
                   const float *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  __vec[__index] = *(const float *)(
    (__INTPTR_TYPE__)__ptr + (__INTPTR_TYPE__)__offset[__index]);
  return __vec;
}
#endif

static inline __ATTRS_o_ai vector double
vec_gather_element(vector double __vec, vector unsigned long long __offset,
                   const double *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  __vec[__index] = *(const double *)(
    (__INTPTR_TYPE__)__ptr + (__INTPTR_TYPE__)__offset[__index]);
  return __vec;
}

/*-- vec_scatter_element ----------------------------------------------------*/

static inline __ATTRS_o_ai void
vec_scatter_element(vector signed int __vec, vector unsigned int __offset,
                    signed int *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  *(signed int *)((__INTPTR_TYPE__)__ptr + __offset[__index]) =
    __vec[__index];
}

static inline __ATTRS_o_ai void
vec_scatter_element(vector bool int __vec, vector unsigned int __offset,
                    unsigned int *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  *(unsigned int *)((__INTPTR_TYPE__)__ptr + __offset[__index]) =
    __vec[__index];
}

static inline __ATTRS_o_ai void
vec_scatter_element(vector unsigned int __vec, vector unsigned int __offset,
                    unsigned int *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  *(unsigned int *)((__INTPTR_TYPE__)__ptr + __offset[__index]) =
    __vec[__index];
}

static inline __ATTRS_o_ai void
vec_scatter_element(vector signed long long __vec,
                    vector unsigned long long __offset,
                    signed long long *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  *(signed long long *)((__INTPTR_TYPE__)__ptr + __offset[__index]) =
    __vec[__index];
}

static inline __ATTRS_o_ai void
vec_scatter_element(vector bool long long __vec,
                    vector unsigned long long __offset,
                    unsigned long long *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  *(unsigned long long *)((__INTPTR_TYPE__)__ptr + __offset[__index]) =
    __vec[__index];
}

static inline __ATTRS_o_ai void
vec_scatter_element(vector unsigned long long __vec,
                    vector unsigned long long __offset,
                    unsigned long long *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  *(unsigned long long *)((__INTPTR_TYPE__)__ptr + __offset[__index]) =
    __vec[__index];
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai void
vec_scatter_element(vector float __vec, vector unsigned int __offset,
                    float *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  *(float *)((__INTPTR_TYPE__)__ptr + __offset[__index]) =
    __vec[__index];
}
#endif

static inline __ATTRS_o_ai void
vec_scatter_element(vector double __vec, vector unsigned long long __offset,
                    double *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  *(double *)((__INTPTR_TYPE__)__ptr + __offset[__index]) =
    __vec[__index];
}

/*-- vec_xl -----------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_xl(long __offset, const signed char *__ptr) {
  return *(const vector signed char *)((__INTPTR_TYPE__)__ptr + __offset);
}

static inline __ATTRS_o_ai vector unsigned char
vec_xl(long __offset, const unsigned char *__ptr) {
  return *(const vector unsigned char *)((__INTPTR_TYPE__)__ptr + __offset);
}

static inline __ATTRS_o_ai vector signed short
vec_xl(long __offset, const signed short *__ptr) {
  return *(const vector signed short *)((__INTPTR_TYPE__)__ptr + __offset);
}

static inline __ATTRS_o_ai vector unsigned short
vec_xl(long __offset, const unsigned short *__ptr) {
  return *(const vector unsigned short *)((__INTPTR_TYPE__)__ptr + __offset);
}

static inline __ATTRS_o_ai vector signed int
vec_xl(long __offset, const signed int *__ptr) {
  return *(const vector signed int *)((__INTPTR_TYPE__)__ptr + __offset);
}

static inline __ATTRS_o_ai vector unsigned int
vec_xl(long __offset, const unsigned int *__ptr) {
  return *(const vector unsigned int *)((__INTPTR_TYPE__)__ptr + __offset);
}

static inline __ATTRS_o_ai vector signed long long
vec_xl(long __offset, const signed long long *__ptr) {
  return *(const vector signed long long *)((__INTPTR_TYPE__)__ptr + __offset);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_xl(long __offset, const unsigned long long *__ptr) {
  return *(const vector unsigned long long *)((__INTPTR_TYPE__)__ptr + __offset);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_xl(long __offset, const float *__ptr) {
  return *(const vector float *)((__INTPTR_TYPE__)__ptr + __offset);
}
#endif

static inline __ATTRS_o_ai vector double
vec_xl(long __offset, const double *__ptr) {
  return *(const vector double *)((__INTPTR_TYPE__)__ptr + __offset);
}

/*-- vec_xld2 ---------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_xld2(long __offset, const signed char *__ptr) {
  return *(const vector signed char *)((__INTPTR_TYPE__)__ptr + __offset);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_xld2(long __offset, const unsigned char *__ptr) {
  return *(const vector unsigned char *)((__INTPTR_TYPE__)__ptr + __offset);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_xld2(long __offset, const signed short *__ptr) {
  return *(const vector signed short *)((__INTPTR_TYPE__)__ptr + __offset);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_xld2(long __offset, const unsigned short *__ptr) {
  return *(const vector unsigned short *)((__INTPTR_TYPE__)__ptr + __offset);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_xld2(long __offset, const signed int *__ptr) {
  return *(const vector signed int *)((__INTPTR_TYPE__)__ptr + __offset);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_xld2(long __offset, const unsigned int *__ptr) {
  return *(const vector unsigned int *)((__INTPTR_TYPE__)__ptr + __offset);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_xld2(long __offset, const signed long long *__ptr) {
  return *(const vector signed long long *)((__INTPTR_TYPE__)__ptr + __offset);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_xld2(long __offset, const unsigned long long *__ptr) {
  return *(const vector unsigned long long *)((__INTPTR_TYPE__)__ptr + __offset);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector double
vec_xld2(long __offset, const double *__ptr) {
  return *(const vector double *)((__INTPTR_TYPE__)__ptr + __offset);
}

/*-- vec_xlw4 ---------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_xlw4(long __offset, const signed char *__ptr) {
  return *(const vector signed char *)((__INTPTR_TYPE__)__ptr + __offset);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_xlw4(long __offset, const unsigned char *__ptr) {
  return *(const vector unsigned char *)((__INTPTR_TYPE__)__ptr + __offset);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_xlw4(long __offset, const signed short *__ptr) {
  return *(const vector signed short *)((__INTPTR_TYPE__)__ptr + __offset);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_xlw4(long __offset, const unsigned short *__ptr) {
  return *(const vector unsigned short *)((__INTPTR_TYPE__)__ptr + __offset);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_xlw4(long __offset, const signed int *__ptr) {
  return *(const vector signed int *)((__INTPTR_TYPE__)__ptr + __offset);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_xlw4(long __offset, const unsigned int *__ptr) {
  return *(const vector unsigned int *)((__INTPTR_TYPE__)__ptr + __offset);
}

/*-- vec_xst ----------------------------------------------------------------*/

static inline __ATTRS_o_ai void
vec_xst(vector signed char __vec, long __offset, signed char *__ptr) {
  *(vector signed char *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void
vec_xst(vector unsigned char __vec, long __offset, unsigned char *__ptr) {
  *(vector unsigned char *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void
vec_xst(vector signed short __vec, long __offset, signed short *__ptr) {
  *(vector signed short *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void
vec_xst(vector unsigned short __vec, long __offset, unsigned short *__ptr) {
  *(vector unsigned short *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void
vec_xst(vector signed int __vec, long __offset, signed int *__ptr) {
  *(vector signed int *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void
vec_xst(vector unsigned int __vec, long __offset, unsigned int *__ptr) {
  *(vector unsigned int *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void
vec_xst(vector signed long long __vec, long __offset,
          signed long long *__ptr) {
  *(vector signed long long *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

static inline __ATTRS_o_ai void
vec_xst(vector unsigned long long __vec, long __offset,
          unsigned long long *__ptr) {
  *(vector unsigned long long *)((__INTPTR_TYPE__)__ptr + __offset) =
    __vec;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai void
vec_xst(vector float __vec, long __offset, float *__ptr) {
  *(vector float *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}
#endif

static inline __ATTRS_o_ai void
vec_xst(vector double __vec, long __offset, double *__ptr) {
  *(vector double *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

/*-- vec_xstd2 --------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(vector signed char __vec, long __offset, signed char *__ptr) {
  *(vector signed char *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(vector unsigned char __vec, long __offset, unsigned char *__ptr) {
  *(vector unsigned char *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(vector signed short __vec, long __offset, signed short *__ptr) {
  *(vector signed short *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(vector unsigned short __vec, long __offset, unsigned short *__ptr) {
  *(vector unsigned short *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(vector signed int __vec, long __offset, signed int *__ptr) {
  *(vector signed int *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(vector unsigned int __vec, long __offset, unsigned int *__ptr) {
  *(vector unsigned int *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(vector signed long long __vec, long __offset,
          signed long long *__ptr) {
  *(vector signed long long *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(vector unsigned long long __vec, long __offset,
          unsigned long long *__ptr) {
  *(vector unsigned long long *)((__INTPTR_TYPE__)__ptr + __offset) =
    __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(vector double __vec, long __offset, double *__ptr) {
  *(vector double *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

/*-- vec_xstw4 --------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstw4(vector signed char __vec, long __offset, signed char *__ptr) {
  *(vector signed char *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstw4(vector unsigned char __vec, long __offset, unsigned char *__ptr) {
  *(vector unsigned char *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstw4(vector signed short __vec, long __offset, signed short *__ptr) {
  *(vector signed short *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstw4(vector unsigned short __vec, long __offset, unsigned short *__ptr) {
  *(vector unsigned short *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstw4(vector signed int __vec, long __offset, signed int *__ptr) {
  *(vector signed int *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstw4(vector unsigned int __vec, long __offset, unsigned int *__ptr) {
  *(vector unsigned int *)((__INTPTR_TYPE__)__ptr + __offset) = __vec;
}

/*-- vec_load_bndry ---------------------------------------------------------*/

extern __ATTRS_o vector signed char
vec_load_bndry(const signed char *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

extern __ATTRS_o vector unsigned char
vec_load_bndry(const unsigned char *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

extern __ATTRS_o vector signed short
vec_load_bndry(const signed short *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

extern __ATTRS_o vector unsigned short
vec_load_bndry(const unsigned short *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

extern __ATTRS_o vector signed int
vec_load_bndry(const signed int *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

extern __ATTRS_o vector unsigned int
vec_load_bndry(const unsigned int *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

extern __ATTRS_o vector signed long long
vec_load_bndry(const signed long long *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

extern __ATTRS_o vector unsigned long long
vec_load_bndry(const unsigned long long *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

#if __ARCH__ >= 12
extern __ATTRS_o vector float
vec_load_bndry(const float *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);
#endif

extern __ATTRS_o vector double
vec_load_bndry(const double *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

#define vec_load_bndry(X, Y) ((__typeof__((vec_load_bndry)((X), (Y)))) \
  __builtin_s390_vlbb((X), ((Y) == 64 ? 0 : \
                            (Y) == 128 ? 1 : \
                            (Y) == 256 ? 2 : \
                            (Y) == 512 ? 3 : \
                            (Y) == 1024 ? 4 : \
                            (Y) == 2048 ? 5 : \
                            (Y) == 4096 ? 6 : -1)))

/*-- vec_load_len -----------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_load_len(const signed char *__ptr, unsigned int __len) {
  return (vector signed char)__builtin_s390_vll(__len, __ptr);
}

static inline __ATTRS_o_ai vector unsigned char
vec_load_len(const unsigned char *__ptr, unsigned int __len) {
  return (vector unsigned char)__builtin_s390_vll(__len, __ptr);
}

static inline __ATTRS_o_ai vector signed short
vec_load_len(const signed short *__ptr, unsigned int __len) {
  return (vector signed short)__builtin_s390_vll(__len, __ptr);
}

static inline __ATTRS_o_ai vector unsigned short
vec_load_len(const unsigned short *__ptr, unsigned int __len) {
  return (vector unsigned short)__builtin_s390_vll(__len, __ptr);
}

static inline __ATTRS_o_ai vector signed int
vec_load_len(const signed int *__ptr, unsigned int __len) {
  return (vector signed int)__builtin_s390_vll(__len, __ptr);
}

static inline __ATTRS_o_ai vector unsigned int
vec_load_len(const unsigned int *__ptr, unsigned int __len) {
  return (vector unsigned int)__builtin_s390_vll(__len, __ptr);
}

static inline __ATTRS_o_ai vector signed long long
vec_load_len(const signed long long *__ptr, unsigned int __len) {
  return (vector signed long long)__builtin_s390_vll(__len, __ptr);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_load_len(const unsigned long long *__ptr, unsigned int __len) {
  return (vector unsigned long long)__builtin_s390_vll(__len, __ptr);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_load_len(const float *__ptr, unsigned int __len) {
  return (vector float)__builtin_s390_vll(__len, __ptr);
}
#endif

static inline __ATTRS_o_ai vector double
vec_load_len(const double *__ptr, unsigned int __len) {
  return (vector double)__builtin_s390_vll(__len, __ptr);
}

/*-- vec_load_len_r ---------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_ai vector unsigned char
vec_load_len_r(const unsigned char *__ptr, unsigned int __len) {
  return (vector unsigned char)__builtin_s390_vlrl(__len, __ptr);
}
#endif

/*-- vec_store_len ----------------------------------------------------------*/

static inline __ATTRS_o_ai void
vec_store_len(vector signed char __vec, signed char *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((vector signed char)__vec, __len, __ptr);
}

static inline __ATTRS_o_ai void
vec_store_len(vector unsigned char __vec, unsigned char *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((vector signed char)__vec, __len, __ptr);
}

static inline __ATTRS_o_ai void
vec_store_len(vector signed short __vec, signed short *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((vector signed char)__vec, __len, __ptr);
}

static inline __ATTRS_o_ai void
vec_store_len(vector unsigned short __vec, unsigned short *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((vector signed char)__vec, __len, __ptr);
}

static inline __ATTRS_o_ai void
vec_store_len(vector signed int __vec, signed int *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((vector signed char)__vec, __len, __ptr);
}

static inline __ATTRS_o_ai void
vec_store_len(vector unsigned int __vec, unsigned int *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((vector signed char)__vec, __len, __ptr);
}

static inline __ATTRS_o_ai void
vec_store_len(vector signed long long __vec, signed long long *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((vector signed char)__vec, __len, __ptr);
}

static inline __ATTRS_o_ai void
vec_store_len(vector unsigned long long __vec, unsigned long long *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((vector signed char)__vec, __len, __ptr);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai void
vec_store_len(vector float __vec, float *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((vector signed char)__vec, __len, __ptr);
}
#endif

static inline __ATTRS_o_ai void
vec_store_len(vector double __vec, double *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((vector signed char)__vec, __len, __ptr);
}

/*-- vec_store_len_r --------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_ai void
vec_store_len_r(vector unsigned char __vec, unsigned char *__ptr,
                unsigned int __len) {
  __builtin_s390_vstrl((vector signed char)__vec, __len, __ptr);
}
#endif

/*-- vec_load_pair ----------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed long long
vec_load_pair(signed long long __a, signed long long __b) {
  return (vector signed long long)(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_load_pair(unsigned long long __a, unsigned long long __b) {
  return (vector unsigned long long)(__a, __b);
}

/*-- vec_genmask ------------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_genmask(unsigned short __mask)
  __constant(__mask) {
  return (vector unsigned char)(
    __mask & 0x8000 ? 0xff : 0,
    __mask & 0x4000 ? 0xff : 0,
    __mask & 0x2000 ? 0xff : 0,
    __mask & 0x1000 ? 0xff : 0,
    __mask & 0x0800 ? 0xff : 0,
    __mask & 0x0400 ? 0xff : 0,
    __mask & 0x0200 ? 0xff : 0,
    __mask & 0x0100 ? 0xff : 0,
    __mask & 0x0080 ? 0xff : 0,
    __mask & 0x0040 ? 0xff : 0,
    __mask & 0x0020 ? 0xff : 0,
    __mask & 0x0010 ? 0xff : 0,
    __mask & 0x0008 ? 0xff : 0,
    __mask & 0x0004 ? 0xff : 0,
    __mask & 0x0002 ? 0xff : 0,
    __mask & 0x0001 ? 0xff : 0);
}

/*-- vec_genmasks_* ---------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_genmasks_8(unsigned char __first, unsigned char __last)
  __constant(__first) __constant(__last) {
  unsigned char __bit1 = __first & 7;
  unsigned char __bit2 = __last & 7;
  unsigned char __mask1 = (unsigned char)(1U << (7 - __bit1) << 1) - 1;
  unsigned char __mask2 = (unsigned char)(1U << (7 - __bit2)) - 1;
  unsigned char __value = (__bit1 <= __bit2 ?
                           __mask1 & ~__mask2 :
                           __mask1 | ~__mask2);
  return (vector unsigned char)__value;
}

static inline __ATTRS_o_ai vector unsigned short
vec_genmasks_16(unsigned char __first, unsigned char __last)
  __constant(__first) __constant(__last) {
  unsigned char __bit1 = __first & 15;
  unsigned char __bit2 = __last & 15;
  unsigned short __mask1 = (unsigned short)(1U << (15 - __bit1) << 1) - 1;
  unsigned short __mask2 = (unsigned short)(1U << (15 - __bit2)) - 1;
  unsigned short __value = (__bit1 <= __bit2 ?
                            __mask1 & ~__mask2 :
                            __mask1 | ~__mask2);
  return (vector unsigned short)__value;
}

static inline __ATTRS_o_ai vector unsigned int
vec_genmasks_32(unsigned char __first, unsigned char __last)
  __constant(__first) __constant(__last) {
  unsigned char __bit1 = __first & 31;
  unsigned char __bit2 = __last & 31;
  unsigned int __mask1 = (1U << (31 - __bit1) << 1) - 1;
  unsigned int __mask2 = (1U << (31 - __bit2)) - 1;
  unsigned int __value = (__bit1 <= __bit2 ?
                          __mask1 & ~__mask2 :
                          __mask1 | ~__mask2);
  return (vector unsigned int)__value;
}

static inline __ATTRS_o_ai vector unsigned long long
vec_genmasks_64(unsigned char __first, unsigned char __last)
  __constant(__first) __constant(__last) {
  unsigned char __bit1 = __first & 63;
  unsigned char __bit2 = __last & 63;
  unsigned long long __mask1 = (1ULL << (63 - __bit1) << 1) - 1;
  unsigned long long __mask2 = (1ULL << (63 - __bit2)) - 1;
  unsigned long long __value = (__bit1 <= __bit2 ?
                                __mask1 & ~__mask2 :
                                __mask1 | ~__mask2);
  return (vector unsigned long long)__value;
}

/*-- vec_splat --------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_splat(vector signed char __vec, int __index)
  __constant_range(__index, 0, 15) {
  return (vector signed char)__vec[__index];
}

static inline __ATTRS_o_ai vector bool char
vec_splat(vector bool char __vec, int __index)
  __constant_range(__index, 0, 15) {
  return (vector bool char)(vector unsigned char)__vec[__index];
}

static inline __ATTRS_o_ai vector unsigned char
vec_splat(vector unsigned char __vec, int __index)
  __constant_range(__index, 0, 15) {
  return (vector unsigned char)__vec[__index];
}

static inline __ATTRS_o_ai vector signed short
vec_splat(vector signed short __vec, int __index)
  __constant_range(__index, 0, 7) {
  return (vector signed short)__vec[__index];
}

static inline __ATTRS_o_ai vector bool short
vec_splat(vector bool short __vec, int __index)
  __constant_range(__index, 0, 7) {
  return (vector bool short)(vector unsigned short)__vec[__index];
}

static inline __ATTRS_o_ai vector unsigned short
vec_splat(vector unsigned short __vec, int __index)
  __constant_range(__index, 0, 7) {
  return (vector unsigned short)__vec[__index];
}

static inline __ATTRS_o_ai vector signed int
vec_splat(vector signed int __vec, int __index)
  __constant_range(__index, 0, 3) {
  return (vector signed int)__vec[__index];
}

static inline __ATTRS_o_ai vector bool int
vec_splat(vector bool int __vec, int __index)
  __constant_range(__index, 0, 3) {
  return (vector bool int)(vector unsigned int)__vec[__index];
}

static inline __ATTRS_o_ai vector unsigned int
vec_splat(vector unsigned int __vec, int __index)
  __constant_range(__index, 0, 3) {
  return (vector unsigned int)__vec[__index];
}

static inline __ATTRS_o_ai vector signed long long
vec_splat(vector signed long long __vec, int __index)
  __constant_range(__index, 0, 1) {
  return (vector signed long long)__vec[__index];
}

static inline __ATTRS_o_ai vector bool long long
vec_splat(vector bool long long __vec, int __index)
  __constant_range(__index, 0, 1) {
  return (vector bool long long)(vector unsigned long long)__vec[__index];
}

static inline __ATTRS_o_ai vector unsigned long long
vec_splat(vector unsigned long long __vec, int __index)
  __constant_range(__index, 0, 1) {
  return (vector unsigned long long)__vec[__index];
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_splat(vector float __vec, int __index)
  __constant_range(__index, 0, 3) {
  return (vector float)__vec[__index];
}
#endif

static inline __ATTRS_o_ai vector double
vec_splat(vector double __vec, int __index)
  __constant_range(__index, 0, 1) {
  return (vector double)__vec[__index];
}

/*-- vec_splat_s* -----------------------------------------------------------*/

static inline __ATTRS_ai vector signed char
vec_splat_s8(signed char __scalar)
  __constant(__scalar) {
  return (vector signed char)__scalar;
}

static inline __ATTRS_ai vector signed short
vec_splat_s16(signed short __scalar)
  __constant(__scalar) {
  return (vector signed short)__scalar;
}

static inline __ATTRS_ai vector signed int
vec_splat_s32(signed short __scalar)
  __constant(__scalar) {
  return (vector signed int)(signed int)__scalar;
}

static inline __ATTRS_ai vector signed long long
vec_splat_s64(signed short __scalar)
  __constant(__scalar) {
  return (vector signed long long)(signed long)__scalar;
}

/*-- vec_splat_u* -----------------------------------------------------------*/

static inline __ATTRS_ai vector unsigned char
vec_splat_u8(unsigned char __scalar)
  __constant(__scalar) {
  return (vector unsigned char)__scalar;
}

static inline __ATTRS_ai vector unsigned short
vec_splat_u16(unsigned short __scalar)
  __constant(__scalar) {
  return (vector unsigned short)__scalar;
}

static inline __ATTRS_ai vector unsigned int
vec_splat_u32(signed short __scalar)
  __constant(__scalar) {
  return (vector unsigned int)(signed int)__scalar;
}

static inline __ATTRS_ai vector unsigned long long
vec_splat_u64(signed short __scalar)
  __constant(__scalar) {
  return (vector unsigned long long)(signed long long)__scalar;
}

/*-- vec_splats -------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_splats(signed char __scalar) {
  return (vector signed char)__scalar;
}

static inline __ATTRS_o_ai vector unsigned char
vec_splats(unsigned char __scalar) {
  return (vector unsigned char)__scalar;
}

static inline __ATTRS_o_ai vector signed short
vec_splats(signed short __scalar) {
  return (vector signed short)__scalar;
}

static inline __ATTRS_o_ai vector unsigned short
vec_splats(unsigned short __scalar) {
  return (vector unsigned short)__scalar;
}

static inline __ATTRS_o_ai vector signed int
vec_splats(signed int __scalar) {
  return (vector signed int)__scalar;
}

static inline __ATTRS_o_ai vector unsigned int
vec_splats(unsigned int __scalar) {
  return (vector unsigned int)__scalar;
}

static inline __ATTRS_o_ai vector signed long long
vec_splats(signed long long __scalar) {
  return (vector signed long long)__scalar;
}

static inline __ATTRS_o_ai vector unsigned long long
vec_splats(unsigned long long __scalar) {
  return (vector unsigned long long)__scalar;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_splats(float __scalar) {
  return (vector float)__scalar;
}
#endif

static inline __ATTRS_o_ai vector double
vec_splats(double __scalar) {
  return (vector double)__scalar;
}

/*-- vec_extend_s64 ---------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed long long
vec_extend_s64(vector signed char __a) {
  return (vector signed long long)(__a[7], __a[15]);
}

static inline __ATTRS_o_ai vector signed long long
vec_extend_s64(vector signed short __a) {
  return (vector signed long long)(__a[3], __a[7]);
}

static inline __ATTRS_o_ai vector signed long long
vec_extend_s64(vector signed int __a) {
  return (vector signed long long)(__a[1], __a[3]);
}

/*-- vec_mergeh -------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_mergeh(vector signed char __a, vector signed char __b) {
  return (vector signed char)(
    __a[0], __b[0], __a[1], __b[1], __a[2], __b[2], __a[3], __b[3],
    __a[4], __b[4], __a[5], __b[5], __a[6], __b[6], __a[7], __b[7]);
}

static inline __ATTRS_o_ai vector bool char
vec_mergeh(vector bool char __a, vector bool char __b) {
  return (vector bool char)(
    __a[0], __b[0], __a[1], __b[1], __a[2], __b[2], __a[3], __b[3],
    __a[4], __b[4], __a[5], __b[5], __a[6], __b[6], __a[7], __b[7]);
}

static inline __ATTRS_o_ai vector unsigned char
vec_mergeh(vector unsigned char __a, vector unsigned char __b) {
  return (vector unsigned char)(
    __a[0], __b[0], __a[1], __b[1], __a[2], __b[2], __a[3], __b[3],
    __a[4], __b[4], __a[5], __b[5], __a[6], __b[6], __a[7], __b[7]);
}

static inline __ATTRS_o_ai vector signed short
vec_mergeh(vector signed short __a, vector signed short __b) {
  return (vector signed short)(
    __a[0], __b[0], __a[1], __b[1], __a[2], __b[2], __a[3], __b[3]);
}

static inline __ATTRS_o_ai vector bool short
vec_mergeh(vector bool short __a, vector bool short __b) {
  return (vector bool short)(
    __a[0], __b[0], __a[1], __b[1], __a[2], __b[2], __a[3], __b[3]);
}

static inline __ATTRS_o_ai vector unsigned short
vec_mergeh(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)(
    __a[0], __b[0], __a[1], __b[1], __a[2], __b[2], __a[3], __b[3]);
}

static inline __ATTRS_o_ai vector signed int
vec_mergeh(vector signed int __a, vector signed int __b) {
  return (vector signed int)(__a[0], __b[0], __a[1], __b[1]);
}

static inline __ATTRS_o_ai vector bool int
vec_mergeh(vector bool int __a, vector bool int __b) {
  return (vector bool int)(__a[0], __b[0], __a[1], __b[1]);
}

static inline __ATTRS_o_ai vector unsigned int
vec_mergeh(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)(__a[0], __b[0], __a[1], __b[1]);
}

static inline __ATTRS_o_ai vector signed long long
vec_mergeh(vector signed long long __a, vector signed long long __b) {
  return (vector signed long long)(__a[0], __b[0]);
}

static inline __ATTRS_o_ai vector bool long long
vec_mergeh(vector bool long long __a, vector bool long long __b) {
  return (vector bool long long)(__a[0], __b[0]);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_mergeh(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector unsigned long long)(__a[0], __b[0]);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_mergeh(vector float __a, vector float __b) {
  return (vector float)(__a[0], __b[0], __a[1], __b[1]);
}
#endif

static inline __ATTRS_o_ai vector double
vec_mergeh(vector double __a, vector double __b) {
  return (vector double)(__a[0], __b[0]);
}

/*-- vec_mergel -------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_mergel(vector signed char __a, vector signed char __b) {
  return (vector signed char)(
    __a[8], __b[8], __a[9], __b[9], __a[10], __b[10], __a[11], __b[11],
    __a[12], __b[12], __a[13], __b[13], __a[14], __b[14], __a[15], __b[15]);
}

static inline __ATTRS_o_ai vector bool char
vec_mergel(vector bool char __a, vector bool char __b) {
  return (vector bool char)(
    __a[8], __b[8], __a[9], __b[9], __a[10], __b[10], __a[11], __b[11],
    __a[12], __b[12], __a[13], __b[13], __a[14], __b[14], __a[15], __b[15]);
}

static inline __ATTRS_o_ai vector unsigned char
vec_mergel(vector unsigned char __a, vector unsigned char __b) {
  return (vector unsigned char)(
    __a[8], __b[8], __a[9], __b[9], __a[10], __b[10], __a[11], __b[11],
    __a[12], __b[12], __a[13], __b[13], __a[14], __b[14], __a[15], __b[15]);
}

static inline __ATTRS_o_ai vector signed short
vec_mergel(vector signed short __a, vector signed short __b) {
  return (vector signed short)(
    __a[4], __b[4], __a[5], __b[5], __a[6], __b[6], __a[7], __b[7]);
}

static inline __ATTRS_o_ai vector bool short
vec_mergel(vector bool short __a, vector bool short __b) {
  return (vector bool short)(
    __a[4], __b[4], __a[5], __b[5], __a[6], __b[6], __a[7], __b[7]);
}

static inline __ATTRS_o_ai vector unsigned short
vec_mergel(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)(
    __a[4], __b[4], __a[5], __b[5], __a[6], __b[6], __a[7], __b[7]);
}

static inline __ATTRS_o_ai vector signed int
vec_mergel(vector signed int __a, vector signed int __b) {
  return (vector signed int)(__a[2], __b[2], __a[3], __b[3]);
}

static inline __ATTRS_o_ai vector bool int
vec_mergel(vector bool int __a, vector bool int __b) {
  return (vector bool int)(__a[2], __b[2], __a[3], __b[3]);
}

static inline __ATTRS_o_ai vector unsigned int
vec_mergel(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)(__a[2], __b[2], __a[3], __b[3]);
}

static inline __ATTRS_o_ai vector signed long long
vec_mergel(vector signed long long __a, vector signed long long __b) {
  return (vector signed long long)(__a[1], __b[1]);
}

static inline __ATTRS_o_ai vector bool long long
vec_mergel(vector bool long long __a, vector bool long long __b) {
  return (vector bool long long)(__a[1], __b[1]);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_mergel(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector unsigned long long)(__a[1], __b[1]);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_mergel(vector float __a, vector float __b) {
  return (vector float)(__a[2], __b[2], __a[3], __b[3]);
}
#endif

static inline __ATTRS_o_ai vector double
vec_mergel(vector double __a, vector double __b) {
  return (vector double)(__a[1], __b[1]);
}

/*-- vec_pack ---------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_pack(vector signed short __a, vector signed short __b) {
  vector signed char __ac = (vector signed char)__a;
  vector signed char __bc = (vector signed char)__b;
  return (vector signed char)(
    __ac[1], __ac[3], __ac[5], __ac[7], __ac[9], __ac[11], __ac[13], __ac[15],
    __bc[1], __bc[3], __bc[5], __bc[7], __bc[9], __bc[11], __bc[13], __bc[15]);
}

static inline __ATTRS_o_ai vector bool char
vec_pack(vector bool short __a, vector bool short __b) {
  vector bool char __ac = (vector bool char)__a;
  vector bool char __bc = (vector bool char)__b;
  return (vector bool char)(
    __ac[1], __ac[3], __ac[5], __ac[7], __ac[9], __ac[11], __ac[13], __ac[15],
    __bc[1], __bc[3], __bc[5], __bc[7], __bc[9], __bc[11], __bc[13], __bc[15]);
}

static inline __ATTRS_o_ai vector unsigned char
vec_pack(vector unsigned short __a, vector unsigned short __b) {
  vector unsigned char __ac = (vector unsigned char)__a;
  vector unsigned char __bc = (vector unsigned char)__b;
  return (vector unsigned char)(
    __ac[1], __ac[3], __ac[5], __ac[7], __ac[9], __ac[11], __ac[13], __ac[15],
    __bc[1], __bc[3], __bc[5], __bc[7], __bc[9], __bc[11], __bc[13], __bc[15]);
}

static inline __ATTRS_o_ai vector signed short
vec_pack(vector signed int __a, vector signed int __b) {
  vector signed short __ac = (vector signed short)__a;
  vector signed short __bc = (vector signed short)__b;
  return (vector signed short)(
    __ac[1], __ac[3], __ac[5], __ac[7],
    __bc[1], __bc[3], __bc[5], __bc[7]);
}

static inline __ATTRS_o_ai vector bool short
vec_pack(vector bool int __a, vector bool int __b) {
  vector bool short __ac = (vector bool short)__a;
  vector bool short __bc = (vector bool short)__b;
  return (vector bool short)(
    __ac[1], __ac[3], __ac[5], __ac[7],
    __bc[1], __bc[3], __bc[5], __bc[7]);
}

static inline __ATTRS_o_ai vector unsigned short
vec_pack(vector unsigned int __a, vector unsigned int __b) {
  vector unsigned short __ac = (vector unsigned short)__a;
  vector unsigned short __bc = (vector unsigned short)__b;
  return (vector unsigned short)(
    __ac[1], __ac[3], __ac[5], __ac[7],
    __bc[1], __bc[3], __bc[5], __bc[7]);
}

static inline __ATTRS_o_ai vector signed int
vec_pack(vector signed long long __a, vector signed long long __b) {
  vector signed int __ac = (vector signed int)__a;
  vector signed int __bc = (vector signed int)__b;
  return (vector signed int)(__ac[1], __ac[3], __bc[1], __bc[3]);
}

static inline __ATTRS_o_ai vector bool int
vec_pack(vector bool long long __a, vector bool long long __b) {
  vector bool int __ac = (vector bool int)__a;
  vector bool int __bc = (vector bool int)__b;
  return (vector bool int)(__ac[1], __ac[3], __bc[1], __bc[3]);
}

static inline __ATTRS_o_ai vector unsigned int
vec_pack(vector unsigned long long __a, vector unsigned long long __b) {
  vector unsigned int __ac = (vector unsigned int)__a;
  vector unsigned int __bc = (vector unsigned int)__b;
  return (vector unsigned int)(__ac[1], __ac[3], __bc[1], __bc[3]);
}

/*-- vec_packs --------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_packs(vector signed short __a, vector signed short __b) {
  return __builtin_s390_vpksh(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_packs(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vpklsh(__a, __b);
}

static inline __ATTRS_o_ai vector signed short
vec_packs(vector signed int __a, vector signed int __b) {
  return __builtin_s390_vpksf(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_packs(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vpklsf(__a, __b);
}

static inline __ATTRS_o_ai vector signed int
vec_packs(vector signed long long __a, vector signed long long __b) {
  return __builtin_s390_vpksg(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_packs(vector unsigned long long __a, vector unsigned long long __b) {
  return __builtin_s390_vpklsg(__a, __b);
}

/*-- vec_packs_cc -----------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_packs_cc(vector signed short __a, vector signed short __b, int *__cc) {
  return __builtin_s390_vpkshs(__a, __b, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_packs_cc(vector unsigned short __a, vector unsigned short __b, int *__cc) {
  return __builtin_s390_vpklshs(__a, __b, __cc);
}

static inline __ATTRS_o_ai vector signed short
vec_packs_cc(vector signed int __a, vector signed int __b, int *__cc) {
  return __builtin_s390_vpksfs(__a, __b, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_packs_cc(vector unsigned int __a, vector unsigned int __b, int *__cc) {
  return __builtin_s390_vpklsfs(__a, __b, __cc);
}

static inline __ATTRS_o_ai vector signed int
vec_packs_cc(vector signed long long __a, vector signed long long __b,
             int *__cc) {
  return __builtin_s390_vpksgs(__a, __b, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_packs_cc(vector unsigned long long __a, vector unsigned long long __b,
             int *__cc) {
  return __builtin_s390_vpklsgs(__a, __b, __cc);
}

/*-- vec_packsu -------------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_packsu(vector signed short __a, vector signed short __b) {
  const vector signed short __zero = (vector signed short)0;
  return __builtin_s390_vpklsh(
    (vector unsigned short)(__a >= __zero) & (vector unsigned short)__a,
    (vector unsigned short)(__b >= __zero) & (vector unsigned short)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_packsu(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vpklsh(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_packsu(vector signed int __a, vector signed int __b) {
  const vector signed int __zero = (vector signed int)0;
  return __builtin_s390_vpklsf(
    (vector unsigned int)(__a >= __zero) & (vector unsigned int)__a,
    (vector unsigned int)(__b >= __zero) & (vector unsigned int)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_packsu(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vpklsf(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_packsu(vector signed long long __a, vector signed long long __b) {
  const vector signed long long __zero = (vector signed long long)0;
  return __builtin_s390_vpklsg(
    (vector unsigned long long)(__a >= __zero) &
    (vector unsigned long long)__a,
    (vector unsigned long long)(__b >= __zero) &
    (vector unsigned long long)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_packsu(vector unsigned long long __a, vector unsigned long long __b) {
  return __builtin_s390_vpklsg(__a, __b);
}

/*-- vec_packsu_cc ----------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_packsu_cc(vector unsigned short __a, vector unsigned short __b, int *__cc) {
  return __builtin_s390_vpklshs(__a, __b, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_packsu_cc(vector unsigned int __a, vector unsigned int __b, int *__cc) {
  return __builtin_s390_vpklsfs(__a, __b, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_packsu_cc(vector unsigned long long __a, vector unsigned long long __b,
              int *__cc) {
  return __builtin_s390_vpklsgs(__a, __b, __cc);
}

/*-- vec_unpackh ------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed short
vec_unpackh(vector signed char __a) {
  return __builtin_s390_vuphb(__a);
}

static inline __ATTRS_o_ai vector bool short
vec_unpackh(vector bool char __a) {
  return (vector bool short)__builtin_s390_vuphb((vector signed char)__a);
}

static inline __ATTRS_o_ai vector unsigned short
vec_unpackh(vector unsigned char __a) {
  return __builtin_s390_vuplhb(__a);
}

static inline __ATTRS_o_ai vector signed int
vec_unpackh(vector signed short __a) {
  return __builtin_s390_vuphh(__a);
}

static inline __ATTRS_o_ai vector bool int
vec_unpackh(vector bool short __a) {
  return (vector bool int)__builtin_s390_vuphh((vector signed short)__a);
}

static inline __ATTRS_o_ai vector unsigned int
vec_unpackh(vector unsigned short __a) {
  return __builtin_s390_vuplhh(__a);
}

static inline __ATTRS_o_ai vector signed long long
vec_unpackh(vector signed int __a) {
  return __builtin_s390_vuphf(__a);
}

static inline __ATTRS_o_ai vector bool long long
vec_unpackh(vector bool int __a) {
  return (vector bool long long)__builtin_s390_vuphf((vector signed int)__a);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_unpackh(vector unsigned int __a) {
  return __builtin_s390_vuplhf(__a);
}

/*-- vec_unpackl ------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed short
vec_unpackl(vector signed char __a) {
  return __builtin_s390_vuplb(__a);
}

static inline __ATTRS_o_ai vector bool short
vec_unpackl(vector bool char __a) {
  return (vector bool short)__builtin_s390_vuplb((vector signed char)__a);
}

static inline __ATTRS_o_ai vector unsigned short
vec_unpackl(vector unsigned char __a) {
  return __builtin_s390_vupllb(__a);
}

static inline __ATTRS_o_ai vector signed int
vec_unpackl(vector signed short __a) {
  return __builtin_s390_vuplhw(__a);
}

static inline __ATTRS_o_ai vector bool int
vec_unpackl(vector bool short __a) {
  return (vector bool int)__builtin_s390_vuplhw((vector signed short)__a);
}

static inline __ATTRS_o_ai vector unsigned int
vec_unpackl(vector unsigned short __a) {
  return __builtin_s390_vupllh(__a);
}

static inline __ATTRS_o_ai vector signed long long
vec_unpackl(vector signed int __a) {
  return __builtin_s390_vuplf(__a);
}

static inline __ATTRS_o_ai vector bool long long
vec_unpackl(vector bool int __a) {
  return (vector bool long long)__builtin_s390_vuplf((vector signed int)__a);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_unpackl(vector unsigned int __a) {
  return __builtin_s390_vupllf(__a);
}

/*-- vec_cmpeq --------------------------------------------------------------*/

static inline __ATTRS_o_ai vector bool char
vec_cmpeq(vector bool char __a, vector bool char __b) {
  return (vector bool char)(__a == __b);
}

static inline __ATTRS_o_ai vector bool char
vec_cmpeq(vector signed char __a, vector signed char __b) {
  return (vector bool char)(__a == __b);
}

static inline __ATTRS_o_ai vector bool char
vec_cmpeq(vector unsigned char __a, vector unsigned char __b) {
  return (vector bool char)(__a == __b);
}

static inline __ATTRS_o_ai vector bool short
vec_cmpeq(vector bool short __a, vector bool short __b) {
  return (vector bool short)(__a == __b);
}

static inline __ATTRS_o_ai vector bool short
vec_cmpeq(vector signed short __a, vector signed short __b) {
  return (vector bool short)(__a == __b);
}

static inline __ATTRS_o_ai vector bool short
vec_cmpeq(vector unsigned short __a, vector unsigned short __b) {
  return (vector bool short)(__a == __b);
}

static inline __ATTRS_o_ai vector bool int
vec_cmpeq(vector bool int __a, vector bool int __b) {
  return (vector bool int)(__a == __b);
}

static inline __ATTRS_o_ai vector bool int
vec_cmpeq(vector signed int __a, vector signed int __b) {
  return (vector bool int)(__a == __b);
}

static inline __ATTRS_o_ai vector bool int
vec_cmpeq(vector unsigned int __a, vector unsigned int __b) {
  return (vector bool int)(__a == __b);
}

static inline __ATTRS_o_ai vector bool long long
vec_cmpeq(vector bool long long __a, vector bool long long __b) {
  return (vector bool long long)(__a == __b);
}

static inline __ATTRS_o_ai vector bool long long
vec_cmpeq(vector signed long long __a, vector signed long long __b) {
  return (vector bool long long)(__a == __b);
}

static inline __ATTRS_o_ai vector bool long long
vec_cmpeq(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector bool long long)(__a == __b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector bool int
vec_cmpeq(vector float __a, vector float __b) {
  return (vector bool int)(__a == __b);
}
#endif

static inline __ATTRS_o_ai vector bool long long
vec_cmpeq(vector double __a, vector double __b) {
  return (vector bool long long)(__a == __b);
}

/*-- vec_cmpge --------------------------------------------------------------*/

static inline __ATTRS_o_ai vector bool char
vec_cmpge(vector signed char __a, vector signed char __b) {
  return (vector bool char)(__a >= __b);
}

static inline __ATTRS_o_ai vector bool char
vec_cmpge(vector unsigned char __a, vector unsigned char __b) {
  return (vector bool char)(__a >= __b);
}

static inline __ATTRS_o_ai vector bool short
vec_cmpge(vector signed short __a, vector signed short __b) {
  return (vector bool short)(__a >= __b);
}

static inline __ATTRS_o_ai vector bool short
vec_cmpge(vector unsigned short __a, vector unsigned short __b) {
  return (vector bool short)(__a >= __b);
}

static inline __ATTRS_o_ai vector bool int
vec_cmpge(vector signed int __a, vector signed int __b) {
  return (vector bool int)(__a >= __b);
}

static inline __ATTRS_o_ai vector bool int
vec_cmpge(vector unsigned int __a, vector unsigned int __b) {
  return (vector bool int)(__a >= __b);
}

static inline __ATTRS_o_ai vector bool long long
vec_cmpge(vector signed long long __a, vector signed long long __b) {
  return (vector bool long long)(__a >= __b);
}

static inline __ATTRS_o_ai vector bool long long
vec_cmpge(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector bool long long)(__a >= __b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector bool int
vec_cmpge(vector float __a, vector float __b) {
  return (vector bool int)(__a >= __b);
}
#endif

static inline __ATTRS_o_ai vector bool long long
vec_cmpge(vector double __a, vector double __b) {
  return (vector bool long long)(__a >= __b);
}

/*-- vec_cmpgt --------------------------------------------------------------*/

static inline __ATTRS_o_ai vector bool char
vec_cmpgt(vector signed char __a, vector signed char __b) {
  return (vector bool char)(__a > __b);
}

static inline __ATTRS_o_ai vector bool char
vec_cmpgt(vector unsigned char __a, vector unsigned char __b) {
  return (vector bool char)(__a > __b);
}

static inline __ATTRS_o_ai vector bool short
vec_cmpgt(vector signed short __a, vector signed short __b) {
  return (vector bool short)(__a > __b);
}

static inline __ATTRS_o_ai vector bool short
vec_cmpgt(vector unsigned short __a, vector unsigned short __b) {
  return (vector bool short)(__a > __b);
}

static inline __ATTRS_o_ai vector bool int
vec_cmpgt(vector signed int __a, vector signed int __b) {
  return (vector bool int)(__a > __b);
}

static inline __ATTRS_o_ai vector bool int
vec_cmpgt(vector unsigned int __a, vector unsigned int __b) {
  return (vector bool int)(__a > __b);
}

static inline __ATTRS_o_ai vector bool long long
vec_cmpgt(vector signed long long __a, vector signed long long __b) {
  return (vector bool long long)(__a > __b);
}

static inline __ATTRS_o_ai vector bool long long
vec_cmpgt(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector bool long long)(__a > __b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector bool int
vec_cmpgt(vector float __a, vector float __b) {
  return (vector bool int)(__a > __b);
}
#endif

static inline __ATTRS_o_ai vector bool long long
vec_cmpgt(vector double __a, vector double __b) {
  return (vector bool long long)(__a > __b);
}

/*-- vec_cmple --------------------------------------------------------------*/

static inline __ATTRS_o_ai vector bool char
vec_cmple(vector signed char __a, vector signed char __b) {
  return (vector bool char)(__a <= __b);
}

static inline __ATTRS_o_ai vector bool char
vec_cmple(vector unsigned char __a, vector unsigned char __b) {
  return (vector bool char)(__a <= __b);
}

static inline __ATTRS_o_ai vector bool short
vec_cmple(vector signed short __a, vector signed short __b) {
  return (vector bool short)(__a <= __b);
}

static inline __ATTRS_o_ai vector bool short
vec_cmple(vector unsigned short __a, vector unsigned short __b) {
  return (vector bool short)(__a <= __b);
}

static inline __ATTRS_o_ai vector bool int
vec_cmple(vector signed int __a, vector signed int __b) {
  return (vector bool int)(__a <= __b);
}

static inline __ATTRS_o_ai vector bool int
vec_cmple(vector unsigned int __a, vector unsigned int __b) {
  return (vector bool int)(__a <= __b);
}

static inline __ATTRS_o_ai vector bool long long
vec_cmple(vector signed long long __a, vector signed long long __b) {
  return (vector bool long long)(__a <= __b);
}

static inline __ATTRS_o_ai vector bool long long
vec_cmple(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector bool long long)(__a <= __b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector bool int
vec_cmple(vector float __a, vector float __b) {
  return (vector bool int)(__a <= __b);
}
#endif

static inline __ATTRS_o_ai vector bool long long
vec_cmple(vector double __a, vector double __b) {
  return (vector bool long long)(__a <= __b);
}

/*-- vec_cmplt --------------------------------------------------------------*/

static inline __ATTRS_o_ai vector bool char
vec_cmplt(vector signed char __a, vector signed char __b) {
  return (vector bool char)(__a < __b);
}

static inline __ATTRS_o_ai vector bool char
vec_cmplt(vector unsigned char __a, vector unsigned char __b) {
  return (vector bool char)(__a < __b);
}

static inline __ATTRS_o_ai vector bool short
vec_cmplt(vector signed short __a, vector signed short __b) {
  return (vector bool short)(__a < __b);
}

static inline __ATTRS_o_ai vector bool short
vec_cmplt(vector unsigned short __a, vector unsigned short __b) {
  return (vector bool short)(__a < __b);
}

static inline __ATTRS_o_ai vector bool int
vec_cmplt(vector signed int __a, vector signed int __b) {
  return (vector bool int)(__a < __b);
}

static inline __ATTRS_o_ai vector bool int
vec_cmplt(vector unsigned int __a, vector unsigned int __b) {
  return (vector bool int)(__a < __b);
}

static inline __ATTRS_o_ai vector bool long long
vec_cmplt(vector signed long long __a, vector signed long long __b) {
  return (vector bool long long)(__a < __b);
}

static inline __ATTRS_o_ai vector bool long long
vec_cmplt(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector bool long long)(__a < __b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector bool int
vec_cmplt(vector float __a, vector float __b) {
  return (vector bool int)(__a < __b);
}
#endif

static inline __ATTRS_o_ai vector bool long long
vec_cmplt(vector double __a, vector double __b) {
  return (vector bool long long)(__a < __b);
}

/*-- vec_all_eq -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_all_eq(vector signed char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector signed char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vceqbs(__a, (vector signed char)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector bool char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(vector unsigned char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector unsigned char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector bool char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(vector bool char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(vector signed short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector signed short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, (vector signed short)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector bool short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(vector unsigned short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector unsigned short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector bool short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(vector bool short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(vector signed int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector signed int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, (vector signed int)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector bool int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(vector unsigned int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector unsigned int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector bool int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(vector bool int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(vector signed long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector signed long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, (vector signed long long)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector bool long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(vector unsigned long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector unsigned long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(vector bool long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(vector bool long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc == 0;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_eq(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfcesbs(__a, __b, &__cc);
  return __cc == 0;
}
#endif

static inline __ATTRS_o_ai int
vec_all_eq(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfcedbs(__a, __b, &__cc);
  return __cc == 0;
}

/*-- vec_all_ne -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_all_ne(vector signed char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector signed char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vceqbs(__a, (vector signed char)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector bool char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(vector unsigned char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector unsigned char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector bool char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(vector bool char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(vector signed short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector signed short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, (vector signed short)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector bool short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(vector unsigned short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector unsigned short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector bool short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(vector bool short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(vector signed int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector signed int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, (vector signed int)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector bool int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(vector unsigned int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector unsigned int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector bool int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(vector bool int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(vector signed long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector signed long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, (vector signed long long)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector bool long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(vector unsigned long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector unsigned long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(vector bool long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(vector bool long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc == 3;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_ne(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfcesbs(__a, __b, &__cc);
  return __cc == 3;
}
#endif

static inline __ATTRS_o_ai int
vec_all_ne(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfcedbs(__a, __b, &__cc);
  return __cc == 3;
}

/*-- vec_all_ge -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_all_ge(vector signed char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector signed char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchbs((vector signed char)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector bool char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, (vector signed char)__a, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ge(vector unsigned char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector unsigned char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector bool char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, (vector unsigned char)__a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector bool char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__b,
                        (vector unsigned char)__a, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ge(vector signed short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector signed short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchhs((vector signed short)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector bool short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, (vector signed short)__a, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ge(vector unsigned short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector unsigned short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector bool short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, (vector unsigned short)__a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector bool short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__b,
                        (vector unsigned short)__a, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ge(vector signed int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector signed int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchfs((vector signed int)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector bool int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, (vector signed int)__a, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ge(vector unsigned int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector unsigned int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector bool int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, (vector unsigned int)__a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector bool int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__b,
                        (vector unsigned int)__a, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ge(vector signed long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector signed long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchgs((vector signed long long)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector bool long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, (vector signed long long)__a, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ge(vector unsigned long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector unsigned long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector bool long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, (vector unsigned long long)__a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(vector bool long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__b,
                        (vector unsigned long long)__a, &__cc);
  return __cc == 3;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_ge(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__a, __b, &__cc);
  return __cc == 0;
}
#endif

static inline __ATTRS_o_ai int
vec_all_ge(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__a, __b, &__cc);
  return __cc == 0;
}

/*-- vec_all_gt -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_all_gt(vector signed char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector signed char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, (vector signed char)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector bool char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs((vector signed char)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_gt(vector unsigned char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector unsigned char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, (vector unsigned char)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector bool char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector bool char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__a,
                        (vector unsigned char)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_gt(vector signed short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector signed short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, (vector signed short)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector bool short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs((vector signed short)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_gt(vector unsigned short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector unsigned short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, (vector unsigned short)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector bool short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector bool short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__a,
                        (vector unsigned short)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_gt(vector signed int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector signed int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, (vector signed int)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector bool int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs((vector signed int)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_gt(vector unsigned int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector unsigned int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, (vector unsigned int)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector bool int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector bool int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__a,
                        (vector unsigned int)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_gt(vector signed long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector signed long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, (vector signed long long)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector bool long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs((vector signed long long)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_gt(vector unsigned long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector unsigned long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, (vector unsigned long long)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector bool long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(vector bool long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__a,
                        (vector unsigned long long)__b, &__cc);
  return __cc == 0;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_gt(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__a, __b, &__cc);
  return __cc == 0;
}
#endif

static inline __ATTRS_o_ai int
vec_all_gt(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__a, __b, &__cc);
  return __cc == 0;
}

/*-- vec_all_le -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_all_le(vector signed char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector signed char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, (vector signed char)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector bool char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs((vector signed char)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_le(vector unsigned char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector unsigned char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, (vector unsigned char)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector bool char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector bool char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__a,
                        (vector unsigned char)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_le(vector signed short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector signed short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, (vector signed short)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector bool short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs((vector signed short)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_le(vector unsigned short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector unsigned short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, (vector unsigned short)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector bool short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector bool short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__a,
                        (vector unsigned short)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_le(vector signed int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector signed int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, (vector signed int)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector bool int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs((vector signed int)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_le(vector unsigned int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector unsigned int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, (vector unsigned int)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector bool int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector bool int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__a,
                        (vector unsigned int)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_le(vector signed long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector signed long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, (vector signed long long)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector bool long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs((vector signed long long)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_le(vector unsigned long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector unsigned long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, (vector unsigned long long)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector bool long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(vector bool long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__a,
                        (vector unsigned long long)__b, &__cc);
  return __cc == 3;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_le(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__b, __a, &__cc);
  return __cc == 0;
}
#endif

static inline __ATTRS_o_ai int
vec_all_le(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__b, __a, &__cc);
  return __cc == 0;
}

/*-- vec_all_lt -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_all_lt(vector signed char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector signed char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchbs((vector signed char)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector bool char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, (vector signed char)__a, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_lt(vector unsigned char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector unsigned char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector bool char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, (vector unsigned char)__a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector bool char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__b,
                        (vector unsigned char)__a, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_lt(vector signed short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector signed short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchhs((vector signed short)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector bool short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, (vector signed short)__a, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_lt(vector unsigned short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector unsigned short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector bool short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, (vector unsigned short)__a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector bool short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__b,
                        (vector unsigned short)__a, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_lt(vector signed int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector signed int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchfs((vector signed int)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector bool int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, (vector signed int)__a, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_lt(vector unsigned int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector unsigned int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector bool int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, (vector unsigned int)__a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector bool int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__b,
                        (vector unsigned int)__a, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_lt(vector signed long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector signed long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchgs((vector signed long long)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector bool long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, (vector signed long long)__a, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_lt(vector unsigned long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector unsigned long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector bool long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, (vector unsigned long long)__a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(vector bool long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__b,
                        (vector unsigned long long)__a, &__cc);
  return __cc == 0;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_lt(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__b, __a, &__cc);
  return __cc == 0;
}
#endif

static inline __ATTRS_o_ai int
vec_all_lt(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__b, __a, &__cc);
  return __cc == 0;
}

/*-- vec_all_nge ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_nge(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__a, __b, &__cc);
  return __cc == 3;
}
#endif

static inline __ATTRS_o_ai int
vec_all_nge(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__a, __b, &__cc);
  return __cc == 3;
}

/*-- vec_all_ngt ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_ngt(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__a, __b, &__cc);
  return __cc == 3;
}
#endif

static inline __ATTRS_o_ai int
vec_all_ngt(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__a, __b, &__cc);
  return __cc == 3;
}

/*-- vec_all_nle ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_nle(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__b, __a, &__cc);
  return __cc == 3;
}
#endif

static inline __ATTRS_o_ai int
vec_all_nle(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__b, __a, &__cc);
  return __cc == 3;
}

/*-- vec_all_nlt ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_nlt(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__b, __a, &__cc);
  return __cc == 3;
}
#endif

static inline __ATTRS_o_ai int
vec_all_nlt(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__b, __a, &__cc);
  return __cc == 3;
}

/*-- vec_all_nan ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_nan(vector float __a) {
  int __cc;
  __builtin_s390_vftcisb(__a, 15, &__cc);
  return __cc == 0;
}
#endif

static inline __ATTRS_o_ai int
vec_all_nan(vector double __a) {
  int __cc;
  __builtin_s390_vftcidb(__a, 15, &__cc);
  return __cc == 0;
}

/*-- vec_all_numeric --------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_numeric(vector float __a) {
  int __cc;
  __builtin_s390_vftcisb(__a, 15, &__cc);
  return __cc == 3;
}
#endif

static inline __ATTRS_o_ai int
vec_all_numeric(vector double __a) {
  int __cc;
  __builtin_s390_vftcidb(__a, 15, &__cc);
  return __cc == 3;
}

/*-- vec_any_eq -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_any_eq(vector signed char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector signed char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vceqbs(__a, (vector signed char)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector bool char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(vector unsigned char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector unsigned char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector bool char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(vector bool char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(vector signed short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector signed short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, (vector signed short)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector bool short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(vector unsigned short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector unsigned short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector bool short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(vector bool short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(vector signed int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector signed int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, (vector signed int)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector bool int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(vector unsigned int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector unsigned int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector bool int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(vector bool int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(vector signed long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector signed long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, (vector signed long long)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector bool long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(vector unsigned long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector unsigned long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(vector bool long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(vector bool long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc <= 1;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_eq(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfcesbs(__a, __b, &__cc);
  return __cc <= 1;
}
#endif

static inline __ATTRS_o_ai int
vec_any_eq(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfcedbs(__a, __b, &__cc);
  return __cc <= 1;
}

/*-- vec_any_ne -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_any_ne(vector signed char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector signed char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vceqbs(__a, (vector signed char)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector bool char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(vector unsigned char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector unsigned char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector bool char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(vector bool char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((vector signed char)__a,
                        (vector signed char)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(vector signed short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector signed short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, (vector signed short)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector bool short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(vector unsigned short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector unsigned short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector bool short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(vector bool short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((vector signed short)__a,
                        (vector signed short)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(vector signed int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector signed int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, (vector signed int)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector bool int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(vector unsigned int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector unsigned int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector bool int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(vector bool int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((vector signed int)__a,
                        (vector signed int)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(vector signed long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector signed long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, (vector signed long long)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector bool long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(vector unsigned long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector unsigned long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(vector bool long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(vector bool long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((vector signed long long)__a,
                        (vector signed long long)__b, &__cc);
  return __cc != 0;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_ne(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfcesbs(__a, __b, &__cc);
  return __cc != 0;
}
#endif

static inline __ATTRS_o_ai int
vec_any_ne(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfcedbs(__a, __b, &__cc);
  return __cc != 0;
}

/*-- vec_any_ge -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_any_ge(vector signed char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector signed char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchbs((vector signed char)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector bool char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, (vector signed char)__a, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ge(vector unsigned char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector unsigned char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector bool char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, (vector unsigned char)__a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector bool char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__b,
                        (vector unsigned char)__a, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ge(vector signed short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector signed short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchhs((vector signed short)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector bool short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, (vector signed short)__a, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ge(vector unsigned short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector unsigned short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector bool short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, (vector unsigned short)__a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector bool short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__b,
                        (vector unsigned short)__a, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ge(vector signed int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector signed int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchfs((vector signed int)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector bool int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, (vector signed int)__a, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ge(vector unsigned int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector unsigned int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector bool int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, (vector unsigned int)__a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector bool int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__b,
                        (vector unsigned int)__a, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ge(vector signed long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector signed long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchgs((vector signed long long)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector bool long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, (vector signed long long)__a, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ge(vector unsigned long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector unsigned long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector bool long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, (vector unsigned long long)__a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(vector bool long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__b,
                        (vector unsigned long long)__a, &__cc);
  return __cc != 0;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_ge(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__a, __b, &__cc);
  return __cc <= 1;
}
#endif

static inline __ATTRS_o_ai int
vec_any_ge(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__a, __b, &__cc);
  return __cc <= 1;
}

/*-- vec_any_gt -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_any_gt(vector signed char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector signed char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, (vector signed char)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector bool char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs((vector signed char)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_gt(vector unsigned char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector unsigned char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, (vector unsigned char)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector bool char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector bool char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__a,
                        (vector unsigned char)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_gt(vector signed short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector signed short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, (vector signed short)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector bool short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs((vector signed short)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_gt(vector unsigned short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector unsigned short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, (vector unsigned short)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector bool short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector bool short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__a,
                        (vector unsigned short)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_gt(vector signed int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector signed int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, (vector signed int)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector bool int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs((vector signed int)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_gt(vector unsigned int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector unsigned int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, (vector unsigned int)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector bool int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector bool int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__a,
                        (vector unsigned int)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_gt(vector signed long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector signed long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, (vector signed long long)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector bool long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs((vector signed long long)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_gt(vector unsigned long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector unsigned long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, (vector unsigned long long)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector bool long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(vector bool long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__a,
                        (vector unsigned long long)__b, &__cc);
  return __cc <= 1;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_gt(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__a, __b, &__cc);
  return __cc <= 1;
}
#endif

static inline __ATTRS_o_ai int
vec_any_gt(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__a, __b, &__cc);
  return __cc <= 1;
}

/*-- vec_any_le -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_any_le(vector signed char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector signed char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, (vector signed char)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector bool char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs((vector signed char)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_le(vector unsigned char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector unsigned char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, (vector unsigned char)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector bool char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector bool char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__a,
                        (vector unsigned char)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_le(vector signed short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector signed short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, (vector signed short)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector bool short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs((vector signed short)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_le(vector unsigned short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector unsigned short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, (vector unsigned short)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector bool short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector bool short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__a,
                        (vector unsigned short)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_le(vector signed int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector signed int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, (vector signed int)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector bool int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs((vector signed int)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_le(vector unsigned int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector unsigned int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, (vector unsigned int)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector bool int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector bool int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__a,
                        (vector unsigned int)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_le(vector signed long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector signed long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, (vector signed long long)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector bool long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs((vector signed long long)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_le(vector unsigned long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector unsigned long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, (vector unsigned long long)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector bool long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(vector bool long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__a,
                        (vector unsigned long long)__b, &__cc);
  return __cc != 0;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_le(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__b, __a, &__cc);
  return __cc <= 1;
}
#endif

static inline __ATTRS_o_ai int
vec_any_le(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__b, __a, &__cc);
  return __cc <= 1;
}

/*-- vec_any_lt -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_any_lt(vector signed char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector signed char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchbs((vector signed char)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector bool char __a, vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, (vector signed char)__a, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_lt(vector unsigned char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector unsigned char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector bool char __a, vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, (vector unsigned char)__a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector bool char __a, vector bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((vector unsigned char)__b,
                        (vector unsigned char)__a, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_lt(vector signed short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector signed short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchhs((vector signed short)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector bool short __a, vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, (vector signed short)__a, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_lt(vector unsigned short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector unsigned short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector bool short __a, vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, (vector unsigned short)__a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector bool short __a, vector bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((vector unsigned short)__b,
                        (vector unsigned short)__a, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_lt(vector signed int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector signed int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchfs((vector signed int)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector bool int __a, vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, (vector signed int)__a, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_lt(vector unsigned int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector unsigned int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector bool int __a, vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, (vector unsigned int)__a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector bool int __a, vector bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((vector unsigned int)__b,
                        (vector unsigned int)__a, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_lt(vector signed long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector signed long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchgs((vector signed long long)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector bool long long __a, vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, (vector signed long long)__a, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_lt(vector unsigned long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector unsigned long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector bool long long __a, vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, (vector unsigned long long)__a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(vector bool long long __a, vector bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((vector unsigned long long)__b,
                        (vector unsigned long long)__a, &__cc);
  return __cc <= 1;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_lt(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__b, __a, &__cc);
  return __cc <= 1;
}
#endif

static inline __ATTRS_o_ai int
vec_any_lt(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__b, __a, &__cc);
  return __cc <= 1;
}

/*-- vec_any_nge ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_nge(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__a, __b, &__cc);
  return __cc != 0;
}
#endif

static inline __ATTRS_o_ai int
vec_any_nge(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__a, __b, &__cc);
  return __cc != 0;
}

/*-- vec_any_ngt ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_ngt(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__a, __b, &__cc);
  return __cc != 0;
}
#endif

static inline __ATTRS_o_ai int
vec_any_ngt(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__a, __b, &__cc);
  return __cc != 0;
}

/*-- vec_any_nle ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_nle(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__b, __a, &__cc);
  return __cc != 0;
}
#endif

static inline __ATTRS_o_ai int
vec_any_nle(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__b, __a, &__cc);
  return __cc != 0;
}

/*-- vec_any_nlt ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_nlt(vector float __a, vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__b, __a, &__cc);
  return __cc != 0;
}
#endif

static inline __ATTRS_o_ai int
vec_any_nlt(vector double __a, vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__b, __a, &__cc);
  return __cc != 0;
}

/*-- vec_any_nan ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_nan(vector float __a) {
  int __cc;
  __builtin_s390_vftcisb(__a, 15, &__cc);
  return __cc != 3;
}
#endif

static inline __ATTRS_o_ai int
vec_any_nan(vector double __a) {
  int __cc;
  __builtin_s390_vftcidb(__a, 15, &__cc);
  return __cc != 3;
}

/*-- vec_any_numeric --------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_numeric(vector float __a) {
  int __cc;
  __builtin_s390_vftcisb(__a, 15, &__cc);
  return __cc != 0;
}
#endif

static inline __ATTRS_o_ai int
vec_any_numeric(vector double __a) {
  int __cc;
  __builtin_s390_vftcidb(__a, 15, &__cc);
  return __cc != 0;
}

/*-- vec_andc ---------------------------------------------------------------*/

static inline __ATTRS_o_ai vector bool char
vec_andc(vector bool char __a, vector bool char __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai vector signed char
vec_andc(vector signed char __a, vector signed char __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_andc(vector bool char __a, vector signed char __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_andc(vector signed char __a, vector bool char __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai vector unsigned char
vec_andc(vector unsigned char __a, vector unsigned char __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_andc(vector bool char __a, vector unsigned char __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_andc(vector unsigned char __a, vector bool char __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai vector bool short
vec_andc(vector bool short __a, vector bool short __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai vector signed short
vec_andc(vector signed short __a, vector signed short __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_andc(vector bool short __a, vector signed short __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_andc(vector signed short __a, vector bool short __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai vector unsigned short
vec_andc(vector unsigned short __a, vector unsigned short __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_andc(vector bool short __a, vector unsigned short __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_andc(vector unsigned short __a, vector bool short __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai vector bool int
vec_andc(vector bool int __a, vector bool int __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai vector signed int
vec_andc(vector signed int __a, vector signed int __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_andc(vector bool int __a, vector signed int __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_andc(vector signed int __a, vector bool int __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai vector unsigned int
vec_andc(vector unsigned int __a, vector unsigned int __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_andc(vector bool int __a, vector unsigned int __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_andc(vector unsigned int __a, vector bool int __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai vector bool long long
vec_andc(vector bool long long __a, vector bool long long __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai vector signed long long
vec_andc(vector signed long long __a, vector signed long long __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_andc(vector bool long long __a, vector signed long long __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_andc(vector signed long long __a, vector bool long long __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai vector unsigned long long
vec_andc(vector unsigned long long __a, vector unsigned long long __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_andc(vector bool long long __a, vector unsigned long long __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_andc(vector unsigned long long __a, vector bool long long __b) {
  return __a & ~__b;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_andc(vector float __a, vector float __b) {
  return (vector float)((vector unsigned int)__a &
                         ~(vector unsigned int)__b);
}
#endif

static inline __ATTRS_o_ai vector double
vec_andc(vector double __a, vector double __b) {
  return (vector double)((vector unsigned long long)__a &
                         ~(vector unsigned long long)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector double
vec_andc(vector bool long long __a, vector double __b) {
  return (vector double)((vector unsigned long long)__a &
                         ~(vector unsigned long long)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector double
vec_andc(vector double __a, vector bool long long __b) {
  return (vector double)((vector unsigned long long)__a &
                         ~(vector unsigned long long)__b);
}

/*-- vec_nor ----------------------------------------------------------------*/

static inline __ATTRS_o_ai vector bool char
vec_nor(vector bool char __a, vector bool char __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai vector signed char
vec_nor(vector signed char __a, vector signed char __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_nor(vector bool char __a, vector signed char __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_nor(vector signed char __a, vector bool char __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_nor(vector unsigned char __a, vector unsigned char __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_nor(vector bool char __a, vector unsigned char __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_nor(vector unsigned char __a, vector bool char __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai vector bool short
vec_nor(vector bool short __a, vector bool short __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai vector signed short
vec_nor(vector signed short __a, vector signed short __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_nor(vector bool short __a, vector signed short __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_nor(vector signed short __a, vector bool short __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_nor(vector unsigned short __a, vector unsigned short __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_nor(vector bool short __a, vector unsigned short __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_nor(vector unsigned short __a, vector bool short __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai vector bool int
vec_nor(vector bool int __a, vector bool int __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai vector signed int
vec_nor(vector signed int __a, vector signed int __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_nor(vector bool int __a, vector signed int __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_nor(vector signed int __a, vector bool int __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_nor(vector unsigned int __a, vector unsigned int __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_nor(vector bool int __a, vector unsigned int __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_nor(vector unsigned int __a, vector bool int __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai vector bool long long
vec_nor(vector bool long long __a, vector bool long long __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai vector signed long long
vec_nor(vector signed long long __a, vector signed long long __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_nor(vector bool long long __a, vector signed long long __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_nor(vector signed long long __a, vector bool long long __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_nor(vector unsigned long long __a, vector unsigned long long __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_nor(vector bool long long __a, vector unsigned long long __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_nor(vector unsigned long long __a, vector bool long long __b) {
  return ~(__a | __b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_nor(vector float __a, vector float __b) {
  return (vector float)~((vector unsigned int)__a |
                         (vector unsigned int)__b);
}
#endif

static inline __ATTRS_o_ai vector double
vec_nor(vector double __a, vector double __b) {
  return (vector double)~((vector unsigned long long)__a |
                          (vector unsigned long long)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector double
vec_nor(vector bool long long __a, vector double __b) {
  return (vector double)~((vector unsigned long long)__a |
                          (vector unsigned long long)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector double
vec_nor(vector double __a, vector bool long long __b) {
  return (vector double)~((vector unsigned long long)__a |
                          (vector unsigned long long)__b);
}

/*-- vec_orc ----------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector bool char
vec_orc(vector bool char __a, vector bool char __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai vector signed char
vec_orc(vector signed char __a, vector signed char __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai vector unsigned char
vec_orc(vector unsigned char __a, vector unsigned char __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai vector bool short
vec_orc(vector bool short __a, vector bool short __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai vector signed short
vec_orc(vector signed short __a, vector signed short __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai vector unsigned short
vec_orc(vector unsigned short __a, vector unsigned short __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai vector bool int
vec_orc(vector bool int __a, vector bool int __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai vector signed int
vec_orc(vector signed int __a, vector signed int __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai vector unsigned int
vec_orc(vector unsigned int __a, vector unsigned int __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai vector bool long long
vec_orc(vector bool long long __a, vector bool long long __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai vector signed long long
vec_orc(vector signed long long __a, vector signed long long __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai vector unsigned long long
vec_orc(vector unsigned long long __a, vector unsigned long long __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai vector float
vec_orc(vector float __a, vector float __b) {
  return (vector float)((vector unsigned int)__a |
                        ~(vector unsigned int)__b);
}

static inline __ATTRS_o_ai vector double
vec_orc(vector double __a, vector double __b) {
  return (vector double)((vector unsigned long long)__a |
                         ~(vector unsigned long long)__b);
}
#endif

/*-- vec_nand ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector bool char
vec_nand(vector bool char __a, vector bool char __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai vector signed char
vec_nand(vector signed char __a, vector signed char __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_nand(vector unsigned char __a, vector unsigned char __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai vector bool short
vec_nand(vector bool short __a, vector bool short __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai vector signed short
vec_nand(vector signed short __a, vector signed short __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_nand(vector unsigned short __a, vector unsigned short __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai vector bool int
vec_nand(vector bool int __a, vector bool int __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai vector signed int
vec_nand(vector signed int __a, vector signed int __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_nand(vector unsigned int __a, vector unsigned int __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai vector bool long long
vec_nand(vector bool long long __a, vector bool long long __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai vector signed long long
vec_nand(vector signed long long __a, vector signed long long __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_nand(vector unsigned long long __a, vector unsigned long long __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai vector float
vec_nand(vector float __a, vector float __b) {
  return (vector float)~((vector unsigned int)__a &
                         (vector unsigned int)__b);
}

static inline __ATTRS_o_ai vector double
vec_nand(vector double __a, vector double __b) {
  return (vector double)~((vector unsigned long long)__a &
                          (vector unsigned long long)__b);
}
#endif

/*-- vec_eqv ----------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector bool char
vec_eqv(vector bool char __a, vector bool char __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai vector signed char
vec_eqv(vector signed char __a, vector signed char __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_eqv(vector unsigned char __a, vector unsigned char __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai vector bool short
vec_eqv(vector bool short __a, vector bool short __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai vector signed short
vec_eqv(vector signed short __a, vector signed short __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_eqv(vector unsigned short __a, vector unsigned short __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai vector bool int
vec_eqv(vector bool int __a, vector bool int __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai vector signed int
vec_eqv(vector signed int __a, vector signed int __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_eqv(vector unsigned int __a, vector unsigned int __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai vector bool long long
vec_eqv(vector bool long long __a, vector bool long long __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai vector signed long long
vec_eqv(vector signed long long __a, vector signed long long __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_eqv(vector unsigned long long __a, vector unsigned long long __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai vector float
vec_eqv(vector float __a, vector float __b) {
  return (vector float)~((vector unsigned int)__a ^
                         (vector unsigned int)__b);
}

static inline __ATTRS_o_ai vector double
vec_eqv(vector double __a, vector double __b) {
  return (vector double)~((vector unsigned long long)__a ^
                          (vector unsigned long long)__b);
}
#endif

/*-- vec_cntlz --------------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_cntlz(vector signed char __a) {
  return __builtin_s390_vclzb((vector unsigned char)__a);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cntlz(vector unsigned char __a) {
  return __builtin_s390_vclzb(__a);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cntlz(vector signed short __a) {
  return __builtin_s390_vclzh((vector unsigned short)__a);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cntlz(vector unsigned short __a) {
  return __builtin_s390_vclzh(__a);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cntlz(vector signed int __a) {
  return __builtin_s390_vclzf((vector unsigned int)__a);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cntlz(vector unsigned int __a) {
  return __builtin_s390_vclzf(__a);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_cntlz(vector signed long long __a) {
  return __builtin_s390_vclzg((vector unsigned long long)__a);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_cntlz(vector unsigned long long __a) {
  return __builtin_s390_vclzg(__a);
}

/*-- vec_cnttz --------------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_cnttz(vector signed char __a) {
  return __builtin_s390_vctzb((vector unsigned char)__a);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cnttz(vector unsigned char __a) {
  return __builtin_s390_vctzb(__a);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cnttz(vector signed short __a) {
  return __builtin_s390_vctzh((vector unsigned short)__a);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cnttz(vector unsigned short __a) {
  return __builtin_s390_vctzh(__a);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cnttz(vector signed int __a) {
  return __builtin_s390_vctzf((vector unsigned int)__a);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cnttz(vector unsigned int __a) {
  return __builtin_s390_vctzf(__a);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_cnttz(vector signed long long __a) {
  return __builtin_s390_vctzg((vector unsigned long long)__a);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_cnttz(vector unsigned long long __a) {
  return __builtin_s390_vctzg(__a);
}

/*-- vec_popcnt -------------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_popcnt(vector signed char __a) {
  return __builtin_s390_vpopctb((vector unsigned char)__a);
}

static inline __ATTRS_o_ai vector unsigned char
vec_popcnt(vector unsigned char __a) {
  return __builtin_s390_vpopctb(__a);
}

static inline __ATTRS_o_ai vector unsigned short
vec_popcnt(vector signed short __a) {
  return __builtin_s390_vpopcth((vector unsigned short)__a);
}

static inline __ATTRS_o_ai vector unsigned short
vec_popcnt(vector unsigned short __a) {
  return __builtin_s390_vpopcth(__a);
}

static inline __ATTRS_o_ai vector unsigned int
vec_popcnt(vector signed int __a) {
  return __builtin_s390_vpopctf((vector unsigned int)__a);
}

static inline __ATTRS_o_ai vector unsigned int
vec_popcnt(vector unsigned int __a) {
  return __builtin_s390_vpopctf(__a);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_popcnt(vector signed long long __a) {
  return __builtin_s390_vpopctg((vector unsigned long long)__a);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_popcnt(vector unsigned long long __a) {
  return __builtin_s390_vpopctg(__a);
}

/*-- vec_rl -----------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_rl(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_s390_verllvb(
    (vector unsigned char)__a, __b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_rl(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_verllvb(__a, __b);
}

static inline __ATTRS_o_ai vector signed short
vec_rl(vector signed short __a, vector unsigned short __b) {
  return (vector signed short)__builtin_s390_verllvh(
    (vector unsigned short)__a, __b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_rl(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_verllvh(__a, __b);
}

static inline __ATTRS_o_ai vector signed int
vec_rl(vector signed int __a, vector unsigned int __b) {
  return (vector signed int)__builtin_s390_verllvf(
    (vector unsigned int)__a, __b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_rl(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_verllvf(__a, __b);
}

static inline __ATTRS_o_ai vector signed long long
vec_rl(vector signed long long __a, vector unsigned long long __b) {
  return (vector signed long long)__builtin_s390_verllvg(
    (vector unsigned long long)__a, __b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_rl(vector unsigned long long __a, vector unsigned long long __b) {
  return __builtin_s390_verllvg(__a, __b);
}

/*-- vec_rli ----------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_rli(vector signed char __a, unsigned long __b) {
  return (vector signed char)__builtin_s390_verllb(
    (vector unsigned char)__a, (int)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_rli(vector unsigned char __a, unsigned long __b) {
  return __builtin_s390_verllb(__a, (int)__b);
}

static inline __ATTRS_o_ai vector signed short
vec_rli(vector signed short __a, unsigned long __b) {
  return (vector signed short)__builtin_s390_verllh(
    (vector unsigned short)__a, (int)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_rli(vector unsigned short __a, unsigned long __b) {
  return __builtin_s390_verllh(__a, (int)__b);
}

static inline __ATTRS_o_ai vector signed int
vec_rli(vector signed int __a, unsigned long __b) {
  return (vector signed int)__builtin_s390_verllf(
    (vector unsigned int)__a, (int)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_rli(vector unsigned int __a, unsigned long __b) {
  return __builtin_s390_verllf(__a, (int)__b);
}

static inline __ATTRS_o_ai vector signed long long
vec_rli(vector signed long long __a, unsigned long __b) {
  return (vector signed long long)__builtin_s390_verllg(
    (vector unsigned long long)__a, (int)__b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_rli(vector unsigned long long __a, unsigned long __b) {
  return __builtin_s390_verllg(__a, (int)__b);
}

/*-- vec_rl_mask ------------------------------------------------------------*/

extern __ATTRS_o vector signed char
vec_rl_mask(vector signed char __a, vector unsigned char __b,
            unsigned char __c) __constant(__c);

extern __ATTRS_o vector unsigned char
vec_rl_mask(vector unsigned char __a, vector unsigned char __b,
            unsigned char __c) __constant(__c);

extern __ATTRS_o vector signed short
vec_rl_mask(vector signed short __a, vector unsigned short __b,
            unsigned char __c) __constant(__c);

extern __ATTRS_o vector unsigned short
vec_rl_mask(vector unsigned short __a, vector unsigned short __b,
            unsigned char __c) __constant(__c);

extern __ATTRS_o vector signed int
vec_rl_mask(vector signed int __a, vector unsigned int __b,
            unsigned char __c) __constant(__c);

extern __ATTRS_o vector unsigned int
vec_rl_mask(vector unsigned int __a, vector unsigned int __b,
            unsigned char __c) __constant(__c);

extern __ATTRS_o vector signed long long
vec_rl_mask(vector signed long long __a, vector unsigned long long __b,
            unsigned char __c) __constant(__c);

extern __ATTRS_o vector unsigned long long
vec_rl_mask(vector unsigned long long __a, vector unsigned long long __b,
            unsigned char __c) __constant(__c);

#define vec_rl_mask(X, Y, Z) ((__typeof__((vec_rl_mask)((X), (Y), (Z)))) \
  __extension__ ({ \
    vector unsigned char __res; \
    vector unsigned char __x = (vector unsigned char)(X); \
    vector unsigned char __y = (vector unsigned char)(Y); \
    switch (sizeof ((X)[0])) { \
    case 1: __res = (vector unsigned char) __builtin_s390_verimb( \
             (vector unsigned char)__x, (vector unsigned char)__x, \
             (vector unsigned char)__y, (Z)); break; \
    case 2: __res = (vector unsigned char) __builtin_s390_verimh( \
             (vector unsigned short)__x, (vector unsigned short)__x, \
             (vector unsigned short)__y, (Z)); break; \
    case 4: __res = (vector unsigned char) __builtin_s390_verimf( \
             (vector unsigned int)__x, (vector unsigned int)__x, \
             (vector unsigned int)__y, (Z)); break; \
    default: __res = (vector unsigned char) __builtin_s390_verimg( \
             (vector unsigned long long)__x, (vector unsigned long long)__x, \
             (vector unsigned long long)__y, (Z)); break; \
    } __res; }))

/*-- vec_sll ----------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_sll(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_s390_vsl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_sll(vector signed char __a, vector unsigned short __b) {
  return (vector signed char)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_sll(vector signed char __a, vector unsigned int __b) {
  return (vector signed char)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool char
vec_sll(vector bool char __a, vector unsigned char __b) {
  return (vector bool char)__builtin_s390_vsl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool char
vec_sll(vector bool char __a, vector unsigned short __b) {
  return (vector bool char)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool char
vec_sll(vector bool char __a, vector unsigned int __b) {
  return (vector bool char)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_sll(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vsl(__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_sll(vector unsigned char __a, vector unsigned short __b) {
  return __builtin_s390_vsl(__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_sll(vector unsigned char __a, vector unsigned int __b) {
  return __builtin_s390_vsl(__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed short
vec_sll(vector signed short __a, vector unsigned char __b) {
  return (vector signed short)__builtin_s390_vsl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_sll(vector signed short __a, vector unsigned short __b) {
  return (vector signed short)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_sll(vector signed short __a, vector unsigned int __b) {
  return (vector signed short)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool short
vec_sll(vector bool short __a, vector unsigned char __b) {
  return (vector bool short)__builtin_s390_vsl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool short
vec_sll(vector bool short __a, vector unsigned short __b) {
  return (vector bool short)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool short
vec_sll(vector bool short __a, vector unsigned int __b) {
  return (vector bool short)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_sll(vector unsigned short __a, vector unsigned char __b) {
  return (vector unsigned short)__builtin_s390_vsl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_sll(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_sll(vector unsigned short __a, vector unsigned int __b) {
  return (vector unsigned short)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed int
vec_sll(vector signed int __a, vector unsigned char __b) {
  return (vector signed int)__builtin_s390_vsl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_sll(vector signed int __a, vector unsigned short __b) {
  return (vector signed int)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_sll(vector signed int __a, vector unsigned int __b) {
  return (vector signed int)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool int
vec_sll(vector bool int __a, vector unsigned char __b) {
  return (vector bool int)__builtin_s390_vsl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool int
vec_sll(vector bool int __a, vector unsigned short __b) {
  return (vector bool int)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool int
vec_sll(vector bool int __a, vector unsigned int __b) {
  return (vector bool int)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_sll(vector unsigned int __a, vector unsigned char __b) {
  return (vector unsigned int)__builtin_s390_vsl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_sll(vector unsigned int __a, vector unsigned short __b) {
  return (vector unsigned int)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_sll(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed long long
vec_sll(vector signed long long __a, vector unsigned char __b) {
  return (vector signed long long)__builtin_s390_vsl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_sll(vector signed long long __a, vector unsigned short __b) {
  return (vector signed long long)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_sll(vector signed long long __a, vector unsigned int __b) {
  return (vector signed long long)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool long long
vec_sll(vector bool long long __a, vector unsigned char __b) {
  return (vector bool long long)__builtin_s390_vsl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool long long
vec_sll(vector bool long long __a, vector unsigned short __b) {
  return (vector bool long long)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool long long
vec_sll(vector bool long long __a, vector unsigned int __b) {
  return (vector bool long long)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_sll(vector unsigned long long __a, vector unsigned char __b) {
  return (vector unsigned long long)__builtin_s390_vsl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_sll(vector unsigned long long __a, vector unsigned short __b) {
  return (vector unsigned long long)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_sll(vector unsigned long long __a, vector unsigned int __b) {
  return (vector unsigned long long)__builtin_s390_vsl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

/*-- vec_slb ----------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_slb(vector signed char __a, vector signed char __b) {
  return (vector signed char)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed char
vec_slb(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_s390_vslb(
    (vector unsigned char)__a, __b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_slb(vector unsigned char __a, vector signed char __b) {
  return __builtin_s390_vslb(__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_slb(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vslb(__a, __b);
}

static inline __ATTRS_o_ai vector signed short
vec_slb(vector signed short __a, vector signed short __b) {
  return (vector signed short)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed short
vec_slb(vector signed short __a, vector unsigned short __b) {
  return (vector signed short)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_slb(vector unsigned short __a, vector signed short __b) {
  return (vector unsigned short)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_slb(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed int
vec_slb(vector signed int __a, vector signed int __b) {
  return (vector signed int)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed int
vec_slb(vector signed int __a, vector unsigned int __b) {
  return (vector signed int)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_slb(vector unsigned int __a, vector signed int __b) {
  return (vector unsigned int)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_slb(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed long long
vec_slb(vector signed long long __a, vector signed long long __b) {
  return (vector signed long long)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed long long
vec_slb(vector signed long long __a, vector unsigned long long __b) {
  return (vector signed long long)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_slb(vector unsigned long long __a, vector signed long long __b) {
  return (vector unsigned long long)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_slb(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector unsigned long long)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_slb(vector float __a, vector signed int __b) {
  return (vector float)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector float
vec_slb(vector float __a, vector unsigned int __b) {
  return (vector float)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}
#endif

static inline __ATTRS_o_ai vector double
vec_slb(vector double __a, vector signed long long __b) {
  return (vector double)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector double
vec_slb(vector double __a, vector unsigned long long __b) {
  return (vector double)__builtin_s390_vslb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

/*-- vec_sld ----------------------------------------------------------------*/

extern __ATTRS_o vector signed char
vec_sld(vector signed char __a, vector signed char __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o vector bool char
vec_sld(vector bool char __a, vector bool char __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o vector unsigned char
vec_sld(vector unsigned char __a, vector unsigned char __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o vector signed short
vec_sld(vector signed short __a, vector signed short __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o vector bool short
vec_sld(vector bool short __a, vector bool short __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o vector unsigned short
vec_sld(vector unsigned short __a, vector unsigned short __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o vector signed int
vec_sld(vector signed int __a, vector signed int __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o vector bool int
vec_sld(vector bool int __a, vector bool int __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o vector unsigned int
vec_sld(vector unsigned int __a, vector unsigned int __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o vector signed long long
vec_sld(vector signed long long __a, vector signed long long __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o vector bool long long
vec_sld(vector bool long long __a, vector bool long long __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o vector unsigned long long
vec_sld(vector unsigned long long __a, vector unsigned long long __b, int __c)
  __constant_range(__c, 0, 15);

#if __ARCH__ >= 12
extern __ATTRS_o vector float
vec_sld(vector float __a, vector float __b, int __c)
  __constant_range(__c, 0, 15);
#endif

extern __ATTRS_o vector double
vec_sld(vector double __a, vector double __b, int __c)
  __constant_range(__c, 0, 15);

#define vec_sld(X, Y, Z) ((__typeof__((vec_sld)((X), (Y), (Z)))) \
  __builtin_s390_vsldb((vector unsigned char)(X), \
                       (vector unsigned char)(Y), (Z)))

/*-- vec_sldw ---------------------------------------------------------------*/

extern __ATTRS_o vector signed char
vec_sldw(vector signed char __a, vector signed char __b, int __c)
  __constant_range(__c, 0, 3);

extern __ATTRS_o vector unsigned char
vec_sldw(vector unsigned char __a, vector unsigned char __b, int __c)
  __constant_range(__c, 0, 3);

extern __ATTRS_o vector signed short
vec_sldw(vector signed short __a, vector signed short __b, int __c)
  __constant_range(__c, 0, 3);

extern __ATTRS_o vector unsigned short
vec_sldw(vector unsigned short __a, vector unsigned short __b, int __c)
  __constant_range(__c, 0, 3);

extern __ATTRS_o vector signed int
vec_sldw(vector signed int __a, vector signed int __b, int __c)
  __constant_range(__c, 0, 3);

extern __ATTRS_o vector unsigned int
vec_sldw(vector unsigned int __a, vector unsigned int __b, int __c)
  __constant_range(__c, 0, 3);

extern __ATTRS_o vector signed long long
vec_sldw(vector signed long long __a, vector signed long long __b, int __c)
  __constant_range(__c, 0, 3);

extern __ATTRS_o vector unsigned long long
vec_sldw(vector unsigned long long __a, vector unsigned long long __b, int __c)
  __constant_range(__c, 0, 3);

// This prototype is deprecated.
extern __ATTRS_o vector double
vec_sldw(vector double __a, vector double __b, int __c)
  __constant_range(__c, 0, 3);

#define vec_sldw(X, Y, Z) ((__typeof__((vec_sldw)((X), (Y), (Z)))) \
  __builtin_s390_vsldb((vector unsigned char)(X), \
                       (vector unsigned char)(Y), (Z) * 4))

/*-- vec_sral ---------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_sral(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_s390_vsra(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_sral(vector signed char __a, vector unsigned short __b) {
  return (vector signed char)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_sral(vector signed char __a, vector unsigned int __b) {
  return (vector signed char)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool char
vec_sral(vector bool char __a, vector unsigned char __b) {
  return (vector bool char)__builtin_s390_vsra(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool char
vec_sral(vector bool char __a, vector unsigned short __b) {
  return (vector bool char)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool char
vec_sral(vector bool char __a, vector unsigned int __b) {
  return (vector bool char)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_sral(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vsra(__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_sral(vector unsigned char __a, vector unsigned short __b) {
  return __builtin_s390_vsra(__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_sral(vector unsigned char __a, vector unsigned int __b) {
  return __builtin_s390_vsra(__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed short
vec_sral(vector signed short __a, vector unsigned char __b) {
  return (vector signed short)__builtin_s390_vsra(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_sral(vector signed short __a, vector unsigned short __b) {
  return (vector signed short)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_sral(vector signed short __a, vector unsigned int __b) {
  return (vector signed short)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool short
vec_sral(vector bool short __a, vector unsigned char __b) {
  return (vector bool short)__builtin_s390_vsra(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool short
vec_sral(vector bool short __a, vector unsigned short __b) {
  return (vector bool short)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool short
vec_sral(vector bool short __a, vector unsigned int __b) {
  return (vector bool short)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_sral(vector unsigned short __a, vector unsigned char __b) {
  return (vector unsigned short)__builtin_s390_vsra(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_sral(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_sral(vector unsigned short __a, vector unsigned int __b) {
  return (vector unsigned short)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed int
vec_sral(vector signed int __a, vector unsigned char __b) {
  return (vector signed int)__builtin_s390_vsra(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_sral(vector signed int __a, vector unsigned short __b) {
  return (vector signed int)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_sral(vector signed int __a, vector unsigned int __b) {
  return (vector signed int)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool int
vec_sral(vector bool int __a, vector unsigned char __b) {
  return (vector bool int)__builtin_s390_vsra(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool int
vec_sral(vector bool int __a, vector unsigned short __b) {
  return (vector bool int)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool int
vec_sral(vector bool int __a, vector unsigned int __b) {
  return (vector bool int)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_sral(vector unsigned int __a, vector unsigned char __b) {
  return (vector unsigned int)__builtin_s390_vsra(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_sral(vector unsigned int __a, vector unsigned short __b) {
  return (vector unsigned int)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_sral(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed long long
vec_sral(vector signed long long __a, vector unsigned char __b) {
  return (vector signed long long)__builtin_s390_vsra(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_sral(vector signed long long __a, vector unsigned short __b) {
  return (vector signed long long)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_sral(vector signed long long __a, vector unsigned int __b) {
  return (vector signed long long)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool long long
vec_sral(vector bool long long __a, vector unsigned char __b) {
  return (vector bool long long)__builtin_s390_vsra(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool long long
vec_sral(vector bool long long __a, vector unsigned short __b) {
  return (vector bool long long)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool long long
vec_sral(vector bool long long __a, vector unsigned int __b) {
  return (vector bool long long)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_sral(vector unsigned long long __a, vector unsigned char __b) {
  return (vector unsigned long long)__builtin_s390_vsra(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_sral(vector unsigned long long __a, vector unsigned short __b) {
  return (vector unsigned long long)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_sral(vector unsigned long long __a, vector unsigned int __b) {
  return (vector unsigned long long)__builtin_s390_vsra(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

/*-- vec_srab ---------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_srab(vector signed char __a, vector signed char __b) {
  return (vector signed char)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed char
vec_srab(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_s390_vsrab(
    (vector unsigned char)__a, __b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_srab(vector unsigned char __a, vector signed char __b) {
  return __builtin_s390_vsrab(__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_srab(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vsrab(__a, __b);
}

static inline __ATTRS_o_ai vector signed short
vec_srab(vector signed short __a, vector signed short __b) {
  return (vector signed short)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed short
vec_srab(vector signed short __a, vector unsigned short __b) {
  return (vector signed short)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_srab(vector unsigned short __a, vector signed short __b) {
  return (vector unsigned short)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_srab(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed int
vec_srab(vector signed int __a, vector signed int __b) {
  return (vector signed int)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed int
vec_srab(vector signed int __a, vector unsigned int __b) {
  return (vector signed int)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_srab(vector unsigned int __a, vector signed int __b) {
  return (vector unsigned int)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_srab(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed long long
vec_srab(vector signed long long __a, vector signed long long __b) {
  return (vector signed long long)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed long long
vec_srab(vector signed long long __a, vector unsigned long long __b) {
  return (vector signed long long)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_srab(vector unsigned long long __a, vector signed long long __b) {
  return (vector unsigned long long)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_srab(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector unsigned long long)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_srab(vector float __a, vector signed int __b) {
  return (vector float)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector float
vec_srab(vector float __a, vector unsigned int __b) {
  return (vector float)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}
#endif

static inline __ATTRS_o_ai vector double
vec_srab(vector double __a, vector signed long long __b) {
  return (vector double)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector double
vec_srab(vector double __a, vector unsigned long long __b) {
  return (vector double)__builtin_s390_vsrab(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

/*-- vec_srl ----------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_srl(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_s390_vsrl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_srl(vector signed char __a, vector unsigned short __b) {
  return (vector signed char)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_srl(vector signed char __a, vector unsigned int __b) {
  return (vector signed char)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool char
vec_srl(vector bool char __a, vector unsigned char __b) {
  return (vector bool char)__builtin_s390_vsrl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool char
vec_srl(vector bool char __a, vector unsigned short __b) {
  return (vector bool char)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool char
vec_srl(vector bool char __a, vector unsigned int __b) {
  return (vector bool char)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_srl(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vsrl(__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_srl(vector unsigned char __a, vector unsigned short __b) {
  return __builtin_s390_vsrl(__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_srl(vector unsigned char __a, vector unsigned int __b) {
  return __builtin_s390_vsrl(__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed short
vec_srl(vector signed short __a, vector unsigned char __b) {
  return (vector signed short)__builtin_s390_vsrl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_srl(vector signed short __a, vector unsigned short __b) {
  return (vector signed short)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_srl(vector signed short __a, vector unsigned int __b) {
  return (vector signed short)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool short
vec_srl(vector bool short __a, vector unsigned char __b) {
  return (vector bool short)__builtin_s390_vsrl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool short
vec_srl(vector bool short __a, vector unsigned short __b) {
  return (vector bool short)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool short
vec_srl(vector bool short __a, vector unsigned int __b) {
  return (vector bool short)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_srl(vector unsigned short __a, vector unsigned char __b) {
  return (vector unsigned short)__builtin_s390_vsrl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_srl(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_srl(vector unsigned short __a, vector unsigned int __b) {
  return (vector unsigned short)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed int
vec_srl(vector signed int __a, vector unsigned char __b) {
  return (vector signed int)__builtin_s390_vsrl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_srl(vector signed int __a, vector unsigned short __b) {
  return (vector signed int)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_srl(vector signed int __a, vector unsigned int __b) {
  return (vector signed int)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool int
vec_srl(vector bool int __a, vector unsigned char __b) {
  return (vector bool int)__builtin_s390_vsrl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool int
vec_srl(vector bool int __a, vector unsigned short __b) {
  return (vector bool int)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool int
vec_srl(vector bool int __a, vector unsigned int __b) {
  return (vector bool int)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_srl(vector unsigned int __a, vector unsigned char __b) {
  return (vector unsigned int)__builtin_s390_vsrl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_srl(vector unsigned int __a, vector unsigned short __b) {
  return (vector unsigned int)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_srl(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed long long
vec_srl(vector signed long long __a, vector unsigned char __b) {
  return (vector signed long long)__builtin_s390_vsrl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_srl(vector signed long long __a, vector unsigned short __b) {
  return (vector signed long long)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_srl(vector signed long long __a, vector unsigned int __b) {
  return (vector signed long long)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool long long
vec_srl(vector bool long long __a, vector unsigned char __b) {
  return (vector bool long long)__builtin_s390_vsrl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool long long
vec_srl(vector bool long long __a, vector unsigned short __b) {
  return (vector bool long long)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector bool long long
vec_srl(vector bool long long __a, vector unsigned int __b) {
  return (vector bool long long)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_srl(vector unsigned long long __a, vector unsigned char __b) {
  return (vector unsigned long long)__builtin_s390_vsrl(
    (vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_srl(vector unsigned long long __a, vector unsigned short __b) {
  return (vector unsigned long long)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_srl(vector unsigned long long __a, vector unsigned int __b) {
  return (vector unsigned long long)__builtin_s390_vsrl(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

/*-- vec_srb ----------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_srb(vector signed char __a, vector signed char __b) {
  return (vector signed char)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed char
vec_srb(vector signed char __a, vector unsigned char __b) {
  return (vector signed char)__builtin_s390_vsrlb(
    (vector unsigned char)__a, __b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_srb(vector unsigned char __a, vector signed char __b) {
  return __builtin_s390_vsrlb(__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_srb(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vsrlb(__a, __b);
}

static inline __ATTRS_o_ai vector signed short
vec_srb(vector signed short __a, vector signed short __b) {
  return (vector signed short)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed short
vec_srb(vector signed short __a, vector unsigned short __b) {
  return (vector signed short)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_srb(vector unsigned short __a, vector signed short __b) {
  return (vector unsigned short)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_srb(vector unsigned short __a, vector unsigned short __b) {
  return (vector unsigned short)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed int
vec_srb(vector signed int __a, vector signed int __b) {
  return (vector signed int)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed int
vec_srb(vector signed int __a, vector unsigned int __b) {
  return (vector signed int)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_srb(vector unsigned int __a, vector signed int __b) {
  return (vector unsigned int)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_srb(vector unsigned int __a, vector unsigned int __b) {
  return (vector unsigned int)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed long long
vec_srb(vector signed long long __a, vector signed long long __b) {
  return (vector signed long long)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector signed long long
vec_srb(vector signed long long __a, vector unsigned long long __b) {
  return (vector signed long long)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_srb(vector unsigned long long __a, vector signed long long __b) {
  return (vector unsigned long long)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_srb(vector unsigned long long __a, vector unsigned long long __b) {
  return (vector unsigned long long)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_srb(vector float __a, vector signed int __b) {
  return (vector float)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector float
vec_srb(vector float __a, vector unsigned int __b) {
  return (vector float)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}
#endif

static inline __ATTRS_o_ai vector double
vec_srb(vector double __a, vector signed long long __b) {
  return (vector double)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector double
vec_srb(vector double __a, vector unsigned long long __b) {
  return (vector double)__builtin_s390_vsrlb(
    (vector unsigned char)__a, (vector unsigned char)__b);
}

/*-- vec_abs ----------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_abs(vector signed char __a) {
  return vec_sel(__a, -__a, vec_cmplt(__a, (vector signed char)0));
}

static inline __ATTRS_o_ai vector signed short
vec_abs(vector signed short __a) {
  return vec_sel(__a, -__a, vec_cmplt(__a, (vector signed short)0));
}

static inline __ATTRS_o_ai vector signed int
vec_abs(vector signed int __a) {
  return vec_sel(__a, -__a, vec_cmplt(__a, (vector signed int)0));
}

static inline __ATTRS_o_ai vector signed long long
vec_abs(vector signed long long __a) {
  return vec_sel(__a, -__a, vec_cmplt(__a, (vector signed long long)0));
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_abs(vector float __a) {
  return __builtin_s390_vflpsb(__a);
}
#endif

static inline __ATTRS_o_ai vector double
vec_abs(vector double __a) {
  return __builtin_s390_vflpdb(__a);
}

/*-- vec_nabs ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_nabs(vector float __a) {
  return __builtin_s390_vflnsb(__a);
}
#endif

static inline __ATTRS_o_ai vector double
vec_nabs(vector double __a) {
  return __builtin_s390_vflndb(__a);
}

/*-- vec_max ----------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_max(vector signed char __a, vector signed char __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_max(vector signed char __a, vector bool char __b) {
  vector signed char __bc = (vector signed char)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_max(vector bool char __a, vector signed char __b) {
  vector signed char __ac = (vector signed char)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai vector unsigned char
vec_max(vector unsigned char __a, vector unsigned char __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_max(vector unsigned char __a, vector bool char __b) {
  vector unsigned char __bc = (vector unsigned char)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_max(vector bool char __a, vector unsigned char __b) {
  vector unsigned char __ac = (vector unsigned char)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai vector signed short
vec_max(vector signed short __a, vector signed short __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_max(vector signed short __a, vector bool short __b) {
  vector signed short __bc = (vector signed short)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_max(vector bool short __a, vector signed short __b) {
  vector signed short __ac = (vector signed short)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai vector unsigned short
vec_max(vector unsigned short __a, vector unsigned short __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_max(vector unsigned short __a, vector bool short __b) {
  vector unsigned short __bc = (vector unsigned short)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_max(vector bool short __a, vector unsigned short __b) {
  vector unsigned short __ac = (vector unsigned short)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai vector signed int
vec_max(vector signed int __a, vector signed int __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_max(vector signed int __a, vector bool int __b) {
  vector signed int __bc = (vector signed int)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_max(vector bool int __a, vector signed int __b) {
  vector signed int __ac = (vector signed int)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai vector unsigned int
vec_max(vector unsigned int __a, vector unsigned int __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_max(vector unsigned int __a, vector bool int __b) {
  vector unsigned int __bc = (vector unsigned int)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_max(vector bool int __a, vector unsigned int __b) {
  vector unsigned int __ac = (vector unsigned int)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai vector signed long long
vec_max(vector signed long long __a, vector signed long long __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_max(vector signed long long __a, vector bool long long __b) {
  vector signed long long __bc = (vector signed long long)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_max(vector bool long long __a, vector signed long long __b) {
  vector signed long long __ac = (vector signed long long)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai vector unsigned long long
vec_max(vector unsigned long long __a, vector unsigned long long __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_max(vector unsigned long long __a, vector bool long long __b) {
  vector unsigned long long __bc = (vector unsigned long long)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_max(vector bool long long __a, vector unsigned long long __b) {
  vector unsigned long long __ac = (vector unsigned long long)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_max(vector float __a, vector float __b) {
  return __builtin_s390_vfmaxsb(__a, __b, 0);
}
#endif

static inline __ATTRS_o_ai vector double
vec_max(vector double __a, vector double __b) {
#if __ARCH__ >= 12
  return __builtin_s390_vfmaxdb(__a, __b, 0);
#else
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
#endif
}

/*-- vec_min ----------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_min(vector signed char __a, vector signed char __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_min(vector signed char __a, vector bool char __b) {
  vector signed char __bc = (vector signed char)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed char
vec_min(vector bool char __a, vector signed char __b) {
  vector signed char __ac = (vector signed char)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai vector unsigned char
vec_min(vector unsigned char __a, vector unsigned char __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_min(vector unsigned char __a, vector bool char __b) {
  vector unsigned char __bc = (vector unsigned char)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned char
vec_min(vector bool char __a, vector unsigned char __b) {
  vector unsigned char __ac = (vector unsigned char)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai vector signed short
vec_min(vector signed short __a, vector signed short __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_min(vector signed short __a, vector bool short __b) {
  vector signed short __bc = (vector signed short)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed short
vec_min(vector bool short __a, vector signed short __b) {
  vector signed short __ac = (vector signed short)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai vector unsigned short
vec_min(vector unsigned short __a, vector unsigned short __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_min(vector unsigned short __a, vector bool short __b) {
  vector unsigned short __bc = (vector unsigned short)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned short
vec_min(vector bool short __a, vector unsigned short __b) {
  vector unsigned short __ac = (vector unsigned short)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai vector signed int
vec_min(vector signed int __a, vector signed int __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_min(vector signed int __a, vector bool int __b) {
  vector signed int __bc = (vector signed int)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed int
vec_min(vector bool int __a, vector signed int __b) {
  vector signed int __ac = (vector signed int)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai vector unsigned int
vec_min(vector unsigned int __a, vector unsigned int __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_min(vector unsigned int __a, vector bool int __b) {
  vector unsigned int __bc = (vector unsigned int)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned int
vec_min(vector bool int __a, vector unsigned int __b) {
  vector unsigned int __ac = (vector unsigned int)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai vector signed long long
vec_min(vector signed long long __a, vector signed long long __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_min(vector signed long long __a, vector bool long long __b) {
  vector signed long long __bc = (vector signed long long)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_min(vector bool long long __a, vector signed long long __b) {
  vector signed long long __ac = (vector signed long long)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai vector unsigned long long
vec_min(vector unsigned long long __a, vector unsigned long long __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_min(vector unsigned long long __a, vector bool long long __b) {
  vector unsigned long long __bc = (vector unsigned long long)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_min(vector bool long long __a, vector unsigned long long __b) {
  vector unsigned long long __ac = (vector unsigned long long)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_min(vector float __a, vector float __b) {
  return __builtin_s390_vfminsb(__a, __b, 0);
}
#endif

static inline __ATTRS_o_ai vector double
vec_min(vector double __a, vector double __b) {
#if __ARCH__ >= 12
  return __builtin_s390_vfmindb(__a, __b, 0);
#else
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
#endif
}

/*-- vec_add_u128 -----------------------------------------------------------*/

static inline __ATTRS_ai vector unsigned char
vec_add_u128(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vaq(__a, __b);
}

/*-- vec_addc ---------------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_addc(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vaccb(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_addc(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vacch(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_addc(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vaccf(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_addc(vector unsigned long long __a, vector unsigned long long __b) {
  return __builtin_s390_vaccg(__a, __b);
}

/*-- vec_addc_u128 ----------------------------------------------------------*/

static inline __ATTRS_ai vector unsigned char
vec_addc_u128(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vaccq(__a, __b);
}

/*-- vec_adde_u128 ----------------------------------------------------------*/

static inline __ATTRS_ai vector unsigned char
vec_adde_u128(vector unsigned char __a, vector unsigned char __b,
              vector unsigned char __c) {
  return __builtin_s390_vacq(__a, __b, __c);
}

/*-- vec_addec_u128 ---------------------------------------------------------*/

static inline __ATTRS_ai vector unsigned char
vec_addec_u128(vector unsigned char __a, vector unsigned char __b,
               vector unsigned char __c) {
  return __builtin_s390_vacccq(__a, __b, __c);
}

/*-- vec_avg ----------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_avg(vector signed char __a, vector signed char __b) {
  return __builtin_s390_vavgb(__a, __b);
}

static inline __ATTRS_o_ai vector signed short
vec_avg(vector signed short __a, vector signed short __b) {
  return __builtin_s390_vavgh(__a, __b);
}

static inline __ATTRS_o_ai vector signed int
vec_avg(vector signed int __a, vector signed int __b) {
  return __builtin_s390_vavgf(__a, __b);
}

static inline __ATTRS_o_ai vector signed long long
vec_avg(vector signed long long __a, vector signed long long __b) {
  return __builtin_s390_vavgg(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_avg(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vavglb(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_avg(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vavglh(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_avg(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vavglf(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_avg(vector unsigned long long __a, vector unsigned long long __b) {
  return __builtin_s390_vavglg(__a, __b);
}

/*-- vec_checksum -----------------------------------------------------------*/

static inline __ATTRS_ai vector unsigned int
vec_checksum(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vcksm(__a, __b);
}

/*-- vec_gfmsum -------------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned short
vec_gfmsum(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vgfmb(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_gfmsum(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vgfmh(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_gfmsum(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vgfmf(__a, __b);
}

/*-- vec_gfmsum_128 ---------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_gfmsum_128(vector unsigned long long __a, vector unsigned long long __b) {
  return __builtin_s390_vgfmg(__a, __b);
}

/*-- vec_gfmsum_accum -------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned short
vec_gfmsum_accum(vector unsigned char __a, vector unsigned char __b,
                 vector unsigned short __c) {
  return __builtin_s390_vgfmab(__a, __b, __c);
}

static inline __ATTRS_o_ai vector unsigned int
vec_gfmsum_accum(vector unsigned short __a, vector unsigned short __b,
                 vector unsigned int __c) {
  return __builtin_s390_vgfmah(__a, __b, __c);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_gfmsum_accum(vector unsigned int __a, vector unsigned int __b,
                 vector unsigned long long __c) {
  return __builtin_s390_vgfmaf(__a, __b, __c);
}

/*-- vec_gfmsum_accum_128 ---------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_gfmsum_accum_128(vector unsigned long long __a,
                     vector unsigned long long __b,
                     vector unsigned char __c) {
  return __builtin_s390_vgfmag(__a, __b, __c);
}

/*-- vec_mladd --------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_mladd(vector signed char __a, vector signed char __b,
          vector signed char __c) {
  return __a * __b + __c;
}

static inline __ATTRS_o_ai vector signed char
vec_mladd(vector unsigned char __a, vector signed char __b,
          vector signed char __c) {
  return (vector signed char)__a * __b + __c;
}

static inline __ATTRS_o_ai vector signed char
vec_mladd(vector signed char __a, vector unsigned char __b,
          vector unsigned char __c) {
  return __a * (vector signed char)__b + (vector signed char)__c;
}

static inline __ATTRS_o_ai vector unsigned char
vec_mladd(vector unsigned char __a, vector unsigned char __b,
          vector unsigned char __c) {
  return __a * __b + __c;
}

static inline __ATTRS_o_ai vector signed short
vec_mladd(vector signed short __a, vector signed short __b,
          vector signed short __c) {
  return __a * __b + __c;
}

static inline __ATTRS_o_ai vector signed short
vec_mladd(vector unsigned short __a, vector signed short __b,
          vector signed short __c) {
  return (vector signed short)__a * __b + __c;
}

static inline __ATTRS_o_ai vector signed short
vec_mladd(vector signed short __a, vector unsigned short __b,
          vector unsigned short __c) {
  return __a * (vector signed short)__b + (vector signed short)__c;
}

static inline __ATTRS_o_ai vector unsigned short
vec_mladd(vector unsigned short __a, vector unsigned short __b,
          vector unsigned short __c) {
  return __a * __b + __c;
}

static inline __ATTRS_o_ai vector signed int
vec_mladd(vector signed int __a, vector signed int __b,
          vector signed int __c) {
  return __a * __b + __c;
}

static inline __ATTRS_o_ai vector signed int
vec_mladd(vector unsigned int __a, vector signed int __b,
          vector signed int __c) {
  return (vector signed int)__a * __b + __c;
}

static inline __ATTRS_o_ai vector signed int
vec_mladd(vector signed int __a, vector unsigned int __b,
          vector unsigned int __c) {
  return __a * (vector signed int)__b + (vector signed int)__c;
}

static inline __ATTRS_o_ai vector unsigned int
vec_mladd(vector unsigned int __a, vector unsigned int __b,
          vector unsigned int __c) {
  return __a * __b + __c;
}

/*-- vec_mhadd --------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_mhadd(vector signed char __a, vector signed char __b,
          vector signed char __c) {
  return __builtin_s390_vmahb(__a, __b, __c);
}

static inline __ATTRS_o_ai vector unsigned char
vec_mhadd(vector unsigned char __a, vector unsigned char __b,
          vector unsigned char __c) {
  return __builtin_s390_vmalhb(__a, __b, __c);
}

static inline __ATTRS_o_ai vector signed short
vec_mhadd(vector signed short __a, vector signed short __b,
          vector signed short __c) {
  return __builtin_s390_vmahh(__a, __b, __c);
}

static inline __ATTRS_o_ai vector unsigned short
vec_mhadd(vector unsigned short __a, vector unsigned short __b,
          vector unsigned short __c) {
  return __builtin_s390_vmalhh(__a, __b, __c);
}

static inline __ATTRS_o_ai vector signed int
vec_mhadd(vector signed int __a, vector signed int __b,
          vector signed int __c) {
  return __builtin_s390_vmahf(__a, __b, __c);
}

static inline __ATTRS_o_ai vector unsigned int
vec_mhadd(vector unsigned int __a, vector unsigned int __b,
          vector unsigned int __c) {
  return __builtin_s390_vmalhf(__a, __b, __c);
}

/*-- vec_meadd --------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed short
vec_meadd(vector signed char __a, vector signed char __b,
          vector signed short __c) {
  return __builtin_s390_vmaeb(__a, __b, __c);
}

static inline __ATTRS_o_ai vector unsigned short
vec_meadd(vector unsigned char __a, vector unsigned char __b,
          vector unsigned short __c) {
  return __builtin_s390_vmaleb(__a, __b, __c);
}

static inline __ATTRS_o_ai vector signed int
vec_meadd(vector signed short __a, vector signed short __b,
          vector signed int __c) {
  return __builtin_s390_vmaeh(__a, __b, __c);
}

static inline __ATTRS_o_ai vector unsigned int
vec_meadd(vector unsigned short __a, vector unsigned short __b,
          vector unsigned int __c) {
  return __builtin_s390_vmaleh(__a, __b, __c);
}

static inline __ATTRS_o_ai vector signed long long
vec_meadd(vector signed int __a, vector signed int __b,
          vector signed long long __c) {
  return __builtin_s390_vmaef(__a, __b, __c);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_meadd(vector unsigned int __a, vector unsigned int __b,
          vector unsigned long long __c) {
  return __builtin_s390_vmalef(__a, __b, __c);
}

/*-- vec_moadd --------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed short
vec_moadd(vector signed char __a, vector signed char __b,
          vector signed short __c) {
  return __builtin_s390_vmaob(__a, __b, __c);
}

static inline __ATTRS_o_ai vector unsigned short
vec_moadd(vector unsigned char __a, vector unsigned char __b,
          vector unsigned short __c) {
  return __builtin_s390_vmalob(__a, __b, __c);
}

static inline __ATTRS_o_ai vector signed int
vec_moadd(vector signed short __a, vector signed short __b,
          vector signed int __c) {
  return __builtin_s390_vmaoh(__a, __b, __c);
}

static inline __ATTRS_o_ai vector unsigned int
vec_moadd(vector unsigned short __a, vector unsigned short __b,
          vector unsigned int __c) {
  return __builtin_s390_vmaloh(__a, __b, __c);
}

static inline __ATTRS_o_ai vector signed long long
vec_moadd(vector signed int __a, vector signed int __b,
          vector signed long long __c) {
  return __builtin_s390_vmaof(__a, __b, __c);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_moadd(vector unsigned int __a, vector unsigned int __b,
          vector unsigned long long __c) {
  return __builtin_s390_vmalof(__a, __b, __c);
}

/*-- vec_mulh ---------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_mulh(vector signed char __a, vector signed char __b) {
  return __builtin_s390_vmhb(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_mulh(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vmlhb(__a, __b);
}

static inline __ATTRS_o_ai vector signed short
vec_mulh(vector signed short __a, vector signed short __b) {
  return __builtin_s390_vmhh(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_mulh(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vmlhh(__a, __b);
}

static inline __ATTRS_o_ai vector signed int
vec_mulh(vector signed int __a, vector signed int __b) {
  return __builtin_s390_vmhf(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_mulh(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vmlhf(__a, __b);
}

/*-- vec_mule ---------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed short
vec_mule(vector signed char __a, vector signed char __b) {
  return __builtin_s390_vmeb(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_mule(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vmleb(__a, __b);
}

static inline __ATTRS_o_ai vector signed int
vec_mule(vector signed short __a, vector signed short __b) {
  return __builtin_s390_vmeh(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_mule(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vmleh(__a, __b);
}

static inline __ATTRS_o_ai vector signed long long
vec_mule(vector signed int __a, vector signed int __b) {
  return __builtin_s390_vmef(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_mule(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vmlef(__a, __b);
}

/*-- vec_mulo ---------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed short
vec_mulo(vector signed char __a, vector signed char __b) {
  return __builtin_s390_vmob(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_mulo(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vmlob(__a, __b);
}

static inline __ATTRS_o_ai vector signed int
vec_mulo(vector signed short __a, vector signed short __b) {
  return __builtin_s390_vmoh(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_mulo(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vmloh(__a, __b);
}

static inline __ATTRS_o_ai vector signed long long
vec_mulo(vector signed int __a, vector signed int __b) {
  return __builtin_s390_vmof(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_mulo(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vmlof(__a, __b);
}

/*-- vec_msum_u128 ----------------------------------------------------------*/

#if __ARCH__ >= 12
#define vec_msum_u128(X, Y, Z, W) \
  ((vector unsigned char)__builtin_s390_vmslg((X), (Y), (Z), (W)));
#endif

/*-- vec_sub_u128 -----------------------------------------------------------*/

static inline __ATTRS_ai vector unsigned char
vec_sub_u128(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vsq(__a, __b);
}

/*-- vec_subc ---------------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_subc(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vscbib(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_subc(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vscbih(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_subc(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vscbif(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_subc(vector unsigned long long __a, vector unsigned long long __b) {
  return __builtin_s390_vscbig(__a, __b);
}

/*-- vec_subc_u128 ----------------------------------------------------------*/

static inline __ATTRS_ai vector unsigned char
vec_subc_u128(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vscbiq(__a, __b);
}

/*-- vec_sube_u128 ----------------------------------------------------------*/

static inline __ATTRS_ai vector unsigned char
vec_sube_u128(vector unsigned char __a, vector unsigned char __b,
              vector unsigned char __c) {
  return __builtin_s390_vsbiq(__a, __b, __c);
}

/*-- vec_subec_u128 ---------------------------------------------------------*/

static inline __ATTRS_ai vector unsigned char
vec_subec_u128(vector unsigned char __a, vector unsigned char __b,
               vector unsigned char __c) {
  return __builtin_s390_vsbcbiq(__a, __b, __c);
}

/*-- vec_sum2 ---------------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned long long
vec_sum2(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vsumgh(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned long long
vec_sum2(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vsumgf(__a, __b);
}

/*-- vec_sum_u128 -----------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_sum_u128(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vsumqf(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_sum_u128(vector unsigned long long __a, vector unsigned long long __b) {
  return __builtin_s390_vsumqg(__a, __b);
}

/*-- vec_sum4 ---------------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned int
vec_sum4(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vsumb(__a, __b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_sum4(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vsumh(__a, __b);
}

/*-- vec_test_mask ----------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_test_mask(vector signed char __a, vector unsigned char __b) {
  return __builtin_s390_vtm((vector unsigned char)__a,
                            (vector unsigned char)__b);
}

static inline __ATTRS_o_ai int
vec_test_mask(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vtm(__a, __b);
}

static inline __ATTRS_o_ai int
vec_test_mask(vector signed short __a, vector unsigned short __b) {
  return __builtin_s390_vtm((vector unsigned char)__a,
                            (vector unsigned char)__b);
}

static inline __ATTRS_o_ai int
vec_test_mask(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vtm((vector unsigned char)__a,
                            (vector unsigned char)__b);
}

static inline __ATTRS_o_ai int
vec_test_mask(vector signed int __a, vector unsigned int __b) {
  return __builtin_s390_vtm((vector unsigned char)__a,
                            (vector unsigned char)__b);
}

static inline __ATTRS_o_ai int
vec_test_mask(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vtm((vector unsigned char)__a,
                            (vector unsigned char)__b);
}

static inline __ATTRS_o_ai int
vec_test_mask(vector signed long long __a, vector unsigned long long __b) {
  return __builtin_s390_vtm((vector unsigned char)__a,
                            (vector unsigned char)__b);
}

static inline __ATTRS_o_ai int
vec_test_mask(vector unsigned long long __a, vector unsigned long long __b) {
  return __builtin_s390_vtm((vector unsigned char)__a,
                            (vector unsigned char)__b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_test_mask(vector float __a, vector unsigned int __b) {
  return __builtin_s390_vtm((vector unsigned char)__a,
                            (vector unsigned char)__b);
}
#endif

static inline __ATTRS_o_ai int
vec_test_mask(vector double __a, vector unsigned long long __b) {
  return __builtin_s390_vtm((vector unsigned char)__a,
                            (vector unsigned char)__b);
}

/*-- vec_madd ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_madd(vector float __a, vector float __b, vector float __c) {
  return __builtin_s390_vfmasb(__a, __b, __c);
}
#endif

static inline __ATTRS_o_ai vector double
vec_madd(vector double __a, vector double __b, vector double __c) {
  return __builtin_s390_vfmadb(__a, __b, __c);
}

/*-- vec_msub ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_msub(vector float __a, vector float __b, vector float __c) {
  return __builtin_s390_vfmssb(__a, __b, __c);
}
#endif

static inline __ATTRS_o_ai vector double
vec_msub(vector double __a, vector double __b, vector double __c) {
  return __builtin_s390_vfmsdb(__a, __b, __c);
}

/*-- vec_nmadd ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_nmadd(vector float __a, vector float __b, vector float __c) {
  return __builtin_s390_vfnmasb(__a, __b, __c);
}

static inline __ATTRS_o_ai vector double
vec_nmadd(vector double __a, vector double __b, vector double __c) {
  return __builtin_s390_vfnmadb(__a, __b, __c);
}
#endif

/*-- vec_nmsub ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_nmsub(vector float __a, vector float __b, vector float __c) {
  return __builtin_s390_vfnmssb(__a, __b, __c);
}

static inline __ATTRS_o_ai vector double
vec_nmsub(vector double __a, vector double __b, vector double __c) {
  return __builtin_s390_vfnmsdb(__a, __b, __c);
}
#endif

/*-- vec_sqrt ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_sqrt(vector float __a) {
  return __builtin_s390_vfsqsb(__a);
}
#endif

static inline __ATTRS_o_ai vector double
vec_sqrt(vector double __a) {
  return __builtin_s390_vfsqdb(__a);
}

/*-- vec_ld2f ---------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_ai vector double
vec_ld2f(const float *__ptr) {
  typedef float __v2f32 __attribute__((__vector_size__(8)));
  return __builtin_convertvector(*(const __v2f32 *)__ptr, vector double);
}

/*-- vec_st2f ---------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_ai void
vec_st2f(vector double __a, float *__ptr) {
  typedef float __v2f32 __attribute__((__vector_size__(8)));
  *(__v2f32 *)__ptr = __builtin_convertvector(__a, __v2f32);
}

/*-- vec_ctd ----------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_o_ai vector double
vec_ctd(vector signed long long __a, int __b)
  __constant_range(__b, 0, 31) {
  vector double __conv = __builtin_convertvector(__a, vector double);
  __conv *= (vector double)(vector unsigned long long)((0x3ffULL - __b) << 52);
  return __conv;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai vector double
vec_ctd(vector unsigned long long __a, int __b)
  __constant_range(__b, 0, 31) {
  vector double __conv = __builtin_convertvector(__a, vector double);
  __conv *= (vector double)(vector unsigned long long)((0x3ffULL - __b) << 52);
  return __conv;
}

/*-- vec_ctsl ---------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_o_ai vector signed long long
vec_ctsl(vector double __a, int __b)
  __constant_range(__b, 0, 31) {
  __a *= (vector double)(vector unsigned long long)((0x3ffULL + __b) << 52);
  return __builtin_convertvector(__a, vector signed long long);
}

/*-- vec_ctul ---------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_o_ai vector unsigned long long
vec_ctul(vector double __a, int __b)
  __constant_range(__b, 0, 31) {
  __a *= (vector double)(vector unsigned long long)((0x3ffULL + __b) << 52);
  return __builtin_convertvector(__a, vector unsigned long long);
}

/*-- vec_doublee ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_ai vector double
vec_doublee(vector float __a) {
  typedef float __v2f32 __attribute__((__vector_size__(8)));
  __v2f32 __pack = __builtin_shufflevector(__a, __a, 0, 2);
  return __builtin_convertvector(__pack, vector double);
}
#endif

/*-- vec_floate -------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_ai vector float
vec_floate(vector double __a) {
  typedef float __v2f32 __attribute__((__vector_size__(8)));
  __v2f32 __pack = __builtin_convertvector(__a, __v2f32);
  return __builtin_shufflevector(__pack, __pack, 0, -1, 1, -1);
}
#endif

/*-- vec_double -------------------------------------------------------------*/

static inline __ATTRS_o_ai vector double
vec_double(vector signed long long __a) {
  return __builtin_convertvector(__a, vector double);
}

static inline __ATTRS_o_ai vector double
vec_double(vector unsigned long long __a) {
  return __builtin_convertvector(__a, vector double);
}

/*-- vec_signed -------------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed long long
vec_signed(vector double __a) {
  return __builtin_convertvector(__a, vector signed long long);
}

/*-- vec_unsigned -----------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned long long
vec_unsigned(vector double __a) {
  return __builtin_convertvector(__a, vector unsigned long long);
}

/*-- vec_roundp -------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_roundp(vector float __a) {
  return __builtin_s390_vfisb(__a, 4, 6);
}
#endif

static inline __ATTRS_o_ai vector double
vec_roundp(vector double __a) {
  return __builtin_s390_vfidb(__a, 4, 6);
}

/*-- vec_ceil ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_ceil(vector float __a) {
  // On this platform, vec_ceil never triggers the IEEE-inexact exception.
  return __builtin_s390_vfisb(__a, 4, 6);
}
#endif

static inline __ATTRS_o_ai vector double
vec_ceil(vector double __a) {
  // On this platform, vec_ceil never triggers the IEEE-inexact exception.
  return __builtin_s390_vfidb(__a, 4, 6);
}

/*-- vec_roundm -------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_roundm(vector float __a) {
  return __builtin_s390_vfisb(__a, 4, 7);
}
#endif

static inline __ATTRS_o_ai vector double
vec_roundm(vector double __a) {
  return __builtin_s390_vfidb(__a, 4, 7);
}

/*-- vec_floor --------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_floor(vector float __a) {
  // On this platform, vec_floor never triggers the IEEE-inexact exception.
  return __builtin_s390_vfisb(__a, 4, 7);
}
#endif

static inline __ATTRS_o_ai vector double
vec_floor(vector double __a) {
  // On this platform, vec_floor never triggers the IEEE-inexact exception.
  return __builtin_s390_vfidb(__a, 4, 7);
}

/*-- vec_roundz -------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_roundz(vector float __a) {
  return __builtin_s390_vfisb(__a, 4, 5);
}
#endif

static inline __ATTRS_o_ai vector double
vec_roundz(vector double __a) {
  return __builtin_s390_vfidb(__a, 4, 5);
}

/*-- vec_trunc --------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_trunc(vector float __a) {
  // On this platform, vec_trunc never triggers the IEEE-inexact exception.
  return __builtin_s390_vfisb(__a, 4, 5);
}
#endif

static inline __ATTRS_o_ai vector double
vec_trunc(vector double __a) {
  // On this platform, vec_trunc never triggers the IEEE-inexact exception.
  return __builtin_s390_vfidb(__a, 4, 5);
}

/*-- vec_roundc -------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_roundc(vector float __a) {
  return __builtin_s390_vfisb(__a, 4, 0);
}
#endif

static inline __ATTRS_o_ai vector double
vec_roundc(vector double __a) {
  return __builtin_s390_vfidb(__a, 4, 0);
}

/*-- vec_rint ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_rint(vector float __a) {
  // vec_rint may trigger the IEEE-inexact exception.
  return __builtin_s390_vfisb(__a, 0, 0);
}
#endif

static inline __ATTRS_o_ai vector double
vec_rint(vector double __a) {
  // vec_rint may trigger the IEEE-inexact exception.
  return __builtin_s390_vfidb(__a, 0, 0);
}

/*-- vec_round --------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai vector float
vec_round(vector float __a) {
  return __builtin_s390_vfisb(__a, 4, 4);
}
#endif

static inline __ATTRS_o_ai vector double
vec_round(vector double __a) {
  return __builtin_s390_vfidb(__a, 4, 4);
}

/*-- vec_fp_test_data_class -------------------------------------------------*/

#if __ARCH__ >= 12
extern __ATTRS_o vector bool int
vec_fp_test_data_class(vector float __a, int __b, int *__c)
  __constant_range(__b, 0, 4095);

extern __ATTRS_o vector bool long long
vec_fp_test_data_class(vector double __a, int __b, int *__c)
  __constant_range(__b, 0, 4095);

#define vec_fp_test_data_class(X, Y, Z) \
  ((__typeof__((vec_fp_test_data_class)((X), (Y), (Z)))) \
   __extension__ ({ \
     vector unsigned char __res; \
     vector unsigned char __x = (vector unsigned char)(X); \
     int *__z = (Z); \
     switch (sizeof ((X)[0])) { \
     case 4:  __res = (vector unsigned char) \
                      __builtin_s390_vftcisb((vector float)__x, (Y), __z); \
              break; \
     default: __res = (vector unsigned char) \
                      __builtin_s390_vftcidb((vector double)__x, (Y), __z); \
              break; \
     } __res; }))
#else
#define vec_fp_test_data_class(X, Y, Z) \
  ((vector bool long long)__builtin_s390_vftcidb((X), (Y), (Z)))
#endif

#define __VEC_CLASS_FP_ZERO_P (1 << 11)
#define __VEC_CLASS_FP_ZERO_N (1 << 10)
#define __VEC_CLASS_FP_ZERO (__VEC_CLASS_FP_ZERO_P | __VEC_CLASS_FP_ZERO_N)
#define __VEC_CLASS_FP_NORMAL_P (1 << 9)
#define __VEC_CLASS_FP_NORMAL_N (1 << 8)
#define __VEC_CLASS_FP_NORMAL (__VEC_CLASS_FP_NORMAL_P | \
                               __VEC_CLASS_FP_NORMAL_N)
#define __VEC_CLASS_FP_SUBNORMAL_P (1 << 7)
#define __VEC_CLASS_FP_SUBNORMAL_N (1 << 6)
#define __VEC_CLASS_FP_SUBNORMAL (__VEC_CLASS_FP_SUBNORMAL_P | \
                                  __VEC_CLASS_FP_SUBNORMAL_N)
#define __VEC_CLASS_FP_INFINITY_P (1 << 5)
#define __VEC_CLASS_FP_INFINITY_N (1 << 4)
#define __VEC_CLASS_FP_INFINITY (__VEC_CLASS_FP_INFINITY_P | \
                                 __VEC_CLASS_FP_INFINITY_N)
#define __VEC_CLASS_FP_QNAN_P (1 << 3)
#define __VEC_CLASS_FP_QNAN_N (1 << 2)
#define __VEC_CLASS_FP_QNAN (__VEC_CLASS_FP_QNAN_P | __VEC_CLASS_FP_QNAN_N)
#define __VEC_CLASS_FP_SNAN_P (1 << 1)
#define __VEC_CLASS_FP_SNAN_N (1 << 0)
#define __VEC_CLASS_FP_SNAN (__VEC_CLASS_FP_SNAN_P | __VEC_CLASS_FP_SNAN_N)
#define __VEC_CLASS_FP_NAN (__VEC_CLASS_FP_QNAN | __VEC_CLASS_FP_SNAN)
#define __VEC_CLASS_FP_NOT_NORMAL (__VEC_CLASS_FP_NAN | \
                                   __VEC_CLASS_FP_SUBNORMAL | \
                                   __VEC_CLASS_FP_ZERO | \
                                   __VEC_CLASS_FP_INFINITY)

/*-- vec_cp_until_zero ------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_cp_until_zero(vector signed char __a) {
  return (vector signed char)__builtin_s390_vistrb((vector unsigned char)__a);
}

static inline __ATTRS_o_ai vector bool char
vec_cp_until_zero(vector bool char __a) {
  return (vector bool char)__builtin_s390_vistrb((vector unsigned char)__a);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cp_until_zero(vector unsigned char __a) {
  return __builtin_s390_vistrb(__a);
}

static inline __ATTRS_o_ai vector signed short
vec_cp_until_zero(vector signed short __a) {
  return (vector signed short)__builtin_s390_vistrh((vector unsigned short)__a);
}

static inline __ATTRS_o_ai vector bool short
vec_cp_until_zero(vector bool short __a) {
  return (vector bool short)__builtin_s390_vistrh((vector unsigned short)__a);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cp_until_zero(vector unsigned short __a) {
  return __builtin_s390_vistrh(__a);
}

static inline __ATTRS_o_ai vector signed int
vec_cp_until_zero(vector signed int __a) {
  return (vector signed int)__builtin_s390_vistrf((vector unsigned int)__a);
}

static inline __ATTRS_o_ai vector bool int
vec_cp_until_zero(vector bool int __a) {
  return (vector bool int)__builtin_s390_vistrf((vector unsigned int)__a);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cp_until_zero(vector unsigned int __a) {
  return __builtin_s390_vistrf(__a);
}

/*-- vec_cp_until_zero_cc ---------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_cp_until_zero_cc(vector signed char __a, int *__cc) {
  return (vector signed char)
    __builtin_s390_vistrbs((vector unsigned char)__a, __cc);
}

static inline __ATTRS_o_ai vector bool char
vec_cp_until_zero_cc(vector bool char __a, int *__cc) {
  return (vector bool char)
    __builtin_s390_vistrbs((vector unsigned char)__a, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cp_until_zero_cc(vector unsigned char __a, int *__cc) {
  return __builtin_s390_vistrbs(__a, __cc);
}

static inline __ATTRS_o_ai vector signed short
vec_cp_until_zero_cc(vector signed short __a, int *__cc) {
  return (vector signed short)
    __builtin_s390_vistrhs((vector unsigned short)__a, __cc);
}

static inline __ATTRS_o_ai vector bool short
vec_cp_until_zero_cc(vector bool short __a, int *__cc) {
  return (vector bool short)
    __builtin_s390_vistrhs((vector unsigned short)__a, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cp_until_zero_cc(vector unsigned short __a, int *__cc) {
  return __builtin_s390_vistrhs(__a, __cc);
}

static inline __ATTRS_o_ai vector signed int
vec_cp_until_zero_cc(vector signed int __a, int *__cc) {
  return (vector signed int)
    __builtin_s390_vistrfs((vector unsigned int)__a, __cc);
}

static inline __ATTRS_o_ai vector bool int
vec_cp_until_zero_cc(vector bool int __a, int *__cc) {
  return (vector bool int)__builtin_s390_vistrfs((vector unsigned int)__a,
                                                 __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cp_until_zero_cc(vector unsigned int __a, int *__cc) {
  return __builtin_s390_vistrfs(__a, __cc);
}

/*-- vec_cmpeq_idx ----------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_cmpeq_idx(vector signed char __a, vector signed char __b) {
  return (vector signed char)
    __builtin_s390_vfeeb((vector unsigned char)__a,
                         (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpeq_idx(vector bool char __a, vector bool char __b) {
  return __builtin_s390_vfeeb((vector unsigned char)__a,
                              (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpeq_idx(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vfeeb(__a, __b);
}

static inline __ATTRS_o_ai vector signed short
vec_cmpeq_idx(vector signed short __a, vector signed short __b) {
  return (vector signed short)
    __builtin_s390_vfeeh((vector unsigned short)__a,
                         (vector unsigned short)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpeq_idx(vector bool short __a, vector bool short __b) {
  return __builtin_s390_vfeeh((vector unsigned short)__a,
                              (vector unsigned short)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpeq_idx(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vfeeh(__a, __b);
}

static inline __ATTRS_o_ai vector signed int
vec_cmpeq_idx(vector signed int __a, vector signed int __b) {
  return (vector signed int)
    __builtin_s390_vfeef((vector unsigned int)__a,
                         (vector unsigned int)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpeq_idx(vector bool int __a, vector bool int __b) {
  return __builtin_s390_vfeef((vector unsigned int)__a,
                              (vector unsigned int)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpeq_idx(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vfeef(__a, __b);
}

/*-- vec_cmpeq_idx_cc -------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_cmpeq_idx_cc(vector signed char __a, vector signed char __b, int *__cc) {
  return (vector signed char)
    __builtin_s390_vfeebs((vector unsigned char)__a,
                          (vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpeq_idx_cc(vector bool char __a, vector bool char __b, int *__cc) {
  return __builtin_s390_vfeebs((vector unsigned char)__a,
                               (vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpeq_idx_cc(vector unsigned char __a, vector unsigned char __b,
                 int *__cc) {
  return __builtin_s390_vfeebs(__a, __b, __cc);
}

static inline __ATTRS_o_ai vector signed short
vec_cmpeq_idx_cc(vector signed short __a, vector signed short __b, int *__cc) {
  return (vector signed short)
    __builtin_s390_vfeehs((vector unsigned short)__a,
                          (vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpeq_idx_cc(vector bool short __a, vector bool short __b, int *__cc) {
  return __builtin_s390_vfeehs((vector unsigned short)__a,
                               (vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpeq_idx_cc(vector unsigned short __a, vector unsigned short __b,
                 int *__cc) {
  return __builtin_s390_vfeehs(__a, __b, __cc);
}

static inline __ATTRS_o_ai vector signed int
vec_cmpeq_idx_cc(vector signed int __a, vector signed int __b, int *__cc) {
  return (vector signed int)
    __builtin_s390_vfeefs((vector unsigned int)__a,
                          (vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpeq_idx_cc(vector bool int __a, vector bool int __b, int *__cc) {
  return __builtin_s390_vfeefs((vector unsigned int)__a,
                               (vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpeq_idx_cc(vector unsigned int __a, vector unsigned int __b, int *__cc) {
  return __builtin_s390_vfeefs(__a, __b, __cc);
}

/*-- vec_cmpeq_or_0_idx -----------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_cmpeq_or_0_idx(vector signed char __a, vector signed char __b) {
  return (vector signed char)
    __builtin_s390_vfeezb((vector unsigned char)__a,
                          (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpeq_or_0_idx(vector bool char __a, vector bool char __b) {
  return __builtin_s390_vfeezb((vector unsigned char)__a,
                               (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpeq_or_0_idx(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vfeezb(__a, __b);
}

static inline __ATTRS_o_ai vector signed short
vec_cmpeq_or_0_idx(vector signed short __a, vector signed short __b) {
  return (vector signed short)
    __builtin_s390_vfeezh((vector unsigned short)__a,
                          (vector unsigned short)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpeq_or_0_idx(vector bool short __a, vector bool short __b) {
  return __builtin_s390_vfeezh((vector unsigned short)__a,
                               (vector unsigned short)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpeq_or_0_idx(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vfeezh(__a, __b);
}

static inline __ATTRS_o_ai vector signed int
vec_cmpeq_or_0_idx(vector signed int __a, vector signed int __b) {
  return (vector signed int)
    __builtin_s390_vfeezf((vector unsigned int)__a,
                          (vector unsigned int)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpeq_or_0_idx(vector bool int __a, vector bool int __b) {
  return __builtin_s390_vfeezf((vector unsigned int)__a,
                               (vector unsigned int)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpeq_or_0_idx(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vfeezf(__a, __b);
}

/*-- vec_cmpeq_or_0_idx_cc --------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_cmpeq_or_0_idx_cc(vector signed char __a, vector signed char __b,
                      int *__cc) {
  return (vector signed char)
    __builtin_s390_vfeezbs((vector unsigned char)__a,
                           (vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpeq_or_0_idx_cc(vector bool char __a, vector bool char __b, int *__cc) {
  return __builtin_s390_vfeezbs((vector unsigned char)__a,
                                (vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpeq_or_0_idx_cc(vector unsigned char __a, vector unsigned char __b,
                      int *__cc) {
  return __builtin_s390_vfeezbs(__a, __b, __cc);
}

static inline __ATTRS_o_ai vector signed short
vec_cmpeq_or_0_idx_cc(vector signed short __a, vector signed short __b,
                      int *__cc) {
  return (vector signed short)
    __builtin_s390_vfeezhs((vector unsigned short)__a,
                           (vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpeq_or_0_idx_cc(vector bool short __a, vector bool short __b, int *__cc) {
  return __builtin_s390_vfeezhs((vector unsigned short)__a,
                                (vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpeq_or_0_idx_cc(vector unsigned short __a, vector unsigned short __b,
                      int *__cc) {
  return __builtin_s390_vfeezhs(__a, __b, __cc);
}

static inline __ATTRS_o_ai vector signed int
vec_cmpeq_or_0_idx_cc(vector signed int __a, vector signed int __b, int *__cc) {
  return (vector signed int)
    __builtin_s390_vfeezfs((vector unsigned int)__a,
                           (vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpeq_or_0_idx_cc(vector bool int __a, vector bool int __b, int *__cc) {
  return __builtin_s390_vfeezfs((vector unsigned int)__a,
                                (vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpeq_or_0_idx_cc(vector unsigned int __a, vector unsigned int __b,
                      int *__cc) {
  return __builtin_s390_vfeezfs(__a, __b, __cc);
}

/*-- vec_cmpne_idx ----------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_cmpne_idx(vector signed char __a, vector signed char __b) {
  return (vector signed char)
    __builtin_s390_vfeneb((vector unsigned char)__a,
                          (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpne_idx(vector bool char __a, vector bool char __b) {
  return __builtin_s390_vfeneb((vector unsigned char)__a,
                               (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpne_idx(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vfeneb(__a, __b);
}

static inline __ATTRS_o_ai vector signed short
vec_cmpne_idx(vector signed short __a, vector signed short __b) {
  return (vector signed short)
    __builtin_s390_vfeneh((vector unsigned short)__a,
                          (vector unsigned short)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpne_idx(vector bool short __a, vector bool short __b) {
  return __builtin_s390_vfeneh((vector unsigned short)__a,
                               (vector unsigned short)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpne_idx(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vfeneh(__a, __b);
}

static inline __ATTRS_o_ai vector signed int
vec_cmpne_idx(vector signed int __a, vector signed int __b) {
  return (vector signed int)
    __builtin_s390_vfenef((vector unsigned int)__a,
                          (vector unsigned int)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpne_idx(vector bool int __a, vector bool int __b) {
  return __builtin_s390_vfenef((vector unsigned int)__a,
                               (vector unsigned int)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpne_idx(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vfenef(__a, __b);
}

/*-- vec_cmpne_idx_cc -------------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_cmpne_idx_cc(vector signed char __a, vector signed char __b, int *__cc) {
  return (vector signed char)
    __builtin_s390_vfenebs((vector unsigned char)__a,
                           (vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpne_idx_cc(vector bool char __a, vector bool char __b, int *__cc) {
  return __builtin_s390_vfenebs((vector unsigned char)__a,
                                (vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpne_idx_cc(vector unsigned char __a, vector unsigned char __b,
                 int *__cc) {
  return __builtin_s390_vfenebs(__a, __b, __cc);
}

static inline __ATTRS_o_ai vector signed short
vec_cmpne_idx_cc(vector signed short __a, vector signed short __b, int *__cc) {
  return (vector signed short)
    __builtin_s390_vfenehs((vector unsigned short)__a,
                           (vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpne_idx_cc(vector bool short __a, vector bool short __b, int *__cc) {
  return __builtin_s390_vfenehs((vector unsigned short)__a,
                                (vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpne_idx_cc(vector unsigned short __a, vector unsigned short __b,
                 int *__cc) {
  return __builtin_s390_vfenehs(__a, __b, __cc);
}

static inline __ATTRS_o_ai vector signed int
vec_cmpne_idx_cc(vector signed int __a, vector signed int __b, int *__cc) {
  return (vector signed int)
    __builtin_s390_vfenefs((vector unsigned int)__a,
                           (vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpne_idx_cc(vector bool int __a, vector bool int __b, int *__cc) {
  return __builtin_s390_vfenefs((vector unsigned int)__a,
                                (vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpne_idx_cc(vector unsigned int __a, vector unsigned int __b, int *__cc) {
  return __builtin_s390_vfenefs(__a, __b, __cc);
}

/*-- vec_cmpne_or_0_idx -----------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_cmpne_or_0_idx(vector signed char __a, vector signed char __b) {
  return (vector signed char)
    __builtin_s390_vfenezb((vector unsigned char)__a,
                           (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpne_or_0_idx(vector bool char __a, vector bool char __b) {
  return __builtin_s390_vfenezb((vector unsigned char)__a,
                                (vector unsigned char)__b);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpne_or_0_idx(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vfenezb(__a, __b);
}

static inline __ATTRS_o_ai vector signed short
vec_cmpne_or_0_idx(vector signed short __a, vector signed short __b) {
  return (vector signed short)
    __builtin_s390_vfenezh((vector unsigned short)__a,
                           (vector unsigned short)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpne_or_0_idx(vector bool short __a, vector bool short __b) {
  return __builtin_s390_vfenezh((vector unsigned short)__a,
                                (vector unsigned short)__b);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpne_or_0_idx(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vfenezh(__a, __b);
}

static inline __ATTRS_o_ai vector signed int
vec_cmpne_or_0_idx(vector signed int __a, vector signed int __b) {
  return (vector signed int)
    __builtin_s390_vfenezf((vector unsigned int)__a,
                           (vector unsigned int)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpne_or_0_idx(vector bool int __a, vector bool int __b) {
  return __builtin_s390_vfenezf((vector unsigned int)__a,
                                (vector unsigned int)__b);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpne_or_0_idx(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vfenezf(__a, __b);
}

/*-- vec_cmpne_or_0_idx_cc --------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_cmpne_or_0_idx_cc(vector signed char __a, vector signed char __b,
                      int *__cc) {
  return (vector signed char)
    __builtin_s390_vfenezbs((vector unsigned char)__a,
                            (vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpne_or_0_idx_cc(vector bool char __a, vector bool char __b, int *__cc) {
  return __builtin_s390_vfenezbs((vector unsigned char)__a,
                                 (vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_cmpne_or_0_idx_cc(vector unsigned char __a, vector unsigned char __b,
                      int *__cc) {
  return __builtin_s390_vfenezbs(__a, __b, __cc);
}

static inline __ATTRS_o_ai vector signed short
vec_cmpne_or_0_idx_cc(vector signed short __a, vector signed short __b,
                      int *__cc) {
  return (vector signed short)
    __builtin_s390_vfenezhs((vector unsigned short)__a,
                            (vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpne_or_0_idx_cc(vector bool short __a, vector bool short __b, int *__cc) {
  return __builtin_s390_vfenezhs((vector unsigned short)__a,
                                 (vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpne_or_0_idx_cc(vector unsigned short __a, vector unsigned short __b,
                      int *__cc) {
  return __builtin_s390_vfenezhs(__a, __b, __cc);
}

static inline __ATTRS_o_ai vector signed int
vec_cmpne_or_0_idx_cc(vector signed int __a, vector signed int __b, int *__cc) {
  return (vector signed int)
    __builtin_s390_vfenezfs((vector unsigned int)__a,
                            (vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpne_or_0_idx_cc(vector bool int __a, vector bool int __b, int *__cc) {
  return __builtin_s390_vfenezfs((vector unsigned int)__a,
                                 (vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpne_or_0_idx_cc(vector unsigned int __a, vector unsigned int __b,
                      int *__cc) {
  return __builtin_s390_vfenezfs(__a, __b, __cc);
}

/*-- vec_cmprg --------------------------------------------------------------*/

static inline __ATTRS_o_ai vector bool char
vec_cmprg(vector unsigned char __a, vector unsigned char __b,
          vector unsigned char __c) {
  return (vector bool char)__builtin_s390_vstrcb(__a, __b, __c, 4);
}

static inline __ATTRS_o_ai vector bool short
vec_cmprg(vector unsigned short __a, vector unsigned short __b,
          vector unsigned short __c) {
  return (vector bool short)__builtin_s390_vstrch(__a, __b, __c, 4);
}

static inline __ATTRS_o_ai vector bool int
vec_cmprg(vector unsigned int __a, vector unsigned int __b,
          vector unsigned int __c) {
  return (vector bool int)__builtin_s390_vstrcf(__a, __b, __c, 4);
}

/*-- vec_cmprg_cc -----------------------------------------------------------*/

static inline __ATTRS_o_ai vector bool char
vec_cmprg_cc(vector unsigned char __a, vector unsigned char __b,
             vector unsigned char __c, int *__cc) {
  return (vector bool char)__builtin_s390_vstrcbs(__a, __b, __c, 4, __cc);
}

static inline __ATTRS_o_ai vector bool short
vec_cmprg_cc(vector unsigned short __a, vector unsigned short __b,
             vector unsigned short __c, int *__cc) {
  return (vector bool short)__builtin_s390_vstrchs(__a, __b, __c, 4, __cc);
}

static inline __ATTRS_o_ai vector bool int
vec_cmprg_cc(vector unsigned int __a, vector unsigned int __b,
             vector unsigned int __c, int *__cc) {
  return (vector bool int)__builtin_s390_vstrcfs(__a, __b, __c, 4, __cc);
}

/*-- vec_cmprg_idx ----------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_cmprg_idx(vector unsigned char __a, vector unsigned char __b,
              vector unsigned char __c) {
  return __builtin_s390_vstrcb(__a, __b, __c, 0);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmprg_idx(vector unsigned short __a, vector unsigned short __b,
              vector unsigned short __c) {
  return __builtin_s390_vstrch(__a, __b, __c, 0);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmprg_idx(vector unsigned int __a, vector unsigned int __b,
              vector unsigned int __c) {
  return __builtin_s390_vstrcf(__a, __b, __c, 0);
}

/*-- vec_cmprg_idx_cc -------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_cmprg_idx_cc(vector unsigned char __a, vector unsigned char __b,
                 vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrcbs(__a, __b, __c, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmprg_idx_cc(vector unsigned short __a, vector unsigned short __b,
                 vector unsigned short __c, int *__cc) {
  return __builtin_s390_vstrchs(__a, __b, __c, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmprg_idx_cc(vector unsigned int __a, vector unsigned int __b,
                 vector unsigned int __c, int *__cc) {
  return __builtin_s390_vstrcfs(__a, __b, __c, 0, __cc);
}

/*-- vec_cmprg_or_0_idx -----------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_cmprg_or_0_idx(vector unsigned char __a, vector unsigned char __b,
                   vector unsigned char __c) {
  return __builtin_s390_vstrczb(__a, __b, __c, 0);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmprg_or_0_idx(vector unsigned short __a, vector unsigned short __b,
                   vector unsigned short __c) {
  return __builtin_s390_vstrczh(__a, __b, __c, 0);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmprg_or_0_idx(vector unsigned int __a, vector unsigned int __b,
                   vector unsigned int __c) {
  return __builtin_s390_vstrczf(__a, __b, __c, 0);
}

/*-- vec_cmprg_or_0_idx_cc --------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_cmprg_or_0_idx_cc(vector unsigned char __a, vector unsigned char __b,
                      vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrczbs(__a, __b, __c, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmprg_or_0_idx_cc(vector unsigned short __a, vector unsigned short __b,
                      vector unsigned short __c, int *__cc) {
  return __builtin_s390_vstrczhs(__a, __b, __c, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmprg_or_0_idx_cc(vector unsigned int __a, vector unsigned int __b,
                      vector unsigned int __c, int *__cc) {
  return __builtin_s390_vstrczfs(__a, __b, __c, 0, __cc);
}

/*-- vec_cmpnrg -------------------------------------------------------------*/

static inline __ATTRS_o_ai vector bool char
vec_cmpnrg(vector unsigned char __a, vector unsigned char __b,
           vector unsigned char __c) {
  return (vector bool char)__builtin_s390_vstrcb(__a, __b, __c, 12);
}

static inline __ATTRS_o_ai vector bool short
vec_cmpnrg(vector unsigned short __a, vector unsigned short __b,
           vector unsigned short __c) {
  return (vector bool short)__builtin_s390_vstrch(__a, __b, __c, 12);
}

static inline __ATTRS_o_ai vector bool int
vec_cmpnrg(vector unsigned int __a, vector unsigned int __b,
           vector unsigned int __c) {
  return (vector bool int)__builtin_s390_vstrcf(__a, __b, __c, 12);
}

/*-- vec_cmpnrg_cc ----------------------------------------------------------*/

static inline __ATTRS_o_ai vector bool char
vec_cmpnrg_cc(vector unsigned char __a, vector unsigned char __b,
              vector unsigned char __c, int *__cc) {
  return (vector bool char)__builtin_s390_vstrcbs(__a, __b, __c, 12, __cc);
}

static inline __ATTRS_o_ai vector bool short
vec_cmpnrg_cc(vector unsigned short __a, vector unsigned short __b,
              vector unsigned short __c, int *__cc) {
  return (vector bool short)__builtin_s390_vstrchs(__a, __b, __c, 12, __cc);
}

static inline __ATTRS_o_ai vector bool int
vec_cmpnrg_cc(vector unsigned int __a, vector unsigned int __b,
              vector unsigned int __c, int *__cc) {
  return (vector bool int)__builtin_s390_vstrcfs(__a, __b, __c, 12, __cc);
}

/*-- vec_cmpnrg_idx ---------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_cmpnrg_idx(vector unsigned char __a, vector unsigned char __b,
               vector unsigned char __c) {
  return __builtin_s390_vstrcb(__a, __b, __c, 8);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpnrg_idx(vector unsigned short __a, vector unsigned short __b,
               vector unsigned short __c) {
  return __builtin_s390_vstrch(__a, __b, __c, 8);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpnrg_idx(vector unsigned int __a, vector unsigned int __b,
               vector unsigned int __c) {
  return __builtin_s390_vstrcf(__a, __b, __c, 8);
}

/*-- vec_cmpnrg_idx_cc ------------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_cmpnrg_idx_cc(vector unsigned char __a, vector unsigned char __b,
                  vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrcbs(__a, __b, __c, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpnrg_idx_cc(vector unsigned short __a, vector unsigned short __b,
                  vector unsigned short __c, int *__cc) {
  return __builtin_s390_vstrchs(__a, __b, __c, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpnrg_idx_cc(vector unsigned int __a, vector unsigned int __b,
                  vector unsigned int __c, int *__cc) {
  return __builtin_s390_vstrcfs(__a, __b, __c, 8, __cc);
}

/*-- vec_cmpnrg_or_0_idx ----------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_cmpnrg_or_0_idx(vector unsigned char __a, vector unsigned char __b,
                    vector unsigned char __c) {
  return __builtin_s390_vstrczb(__a, __b, __c, 8);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpnrg_or_0_idx(vector unsigned short __a, vector unsigned short __b,
                    vector unsigned short __c) {
  return __builtin_s390_vstrczh(__a, __b, __c, 8);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpnrg_or_0_idx(vector unsigned int __a, vector unsigned int __b,
                    vector unsigned int __c) {
  return __builtin_s390_vstrczf(__a, __b, __c, 8);
}

/*-- vec_cmpnrg_or_0_idx_cc -------------------------------------------------*/

static inline __ATTRS_o_ai vector unsigned char
vec_cmpnrg_or_0_idx_cc(vector unsigned char __a, vector unsigned char __b,
                       vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrczbs(__a, __b, __c, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_cmpnrg_or_0_idx_cc(vector unsigned short __a, vector unsigned short __b,
                       vector unsigned short __c, int *__cc) {
  return __builtin_s390_vstrczhs(__a, __b, __c, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_cmpnrg_or_0_idx_cc(vector unsigned int __a, vector unsigned int __b,
                       vector unsigned int __c, int *__cc) {
  return __builtin_s390_vstrczfs(__a, __b, __c, 8, __cc);
}

/*-- vec_find_any_eq --------------------------------------------------------*/

static inline __ATTRS_o_ai vector bool char
vec_find_any_eq(vector signed char __a, vector signed char __b) {
  return (vector bool char)
    __builtin_s390_vfaeb((vector unsigned char)__a,
                         (vector unsigned char)__b, 4);
}

static inline __ATTRS_o_ai vector bool char
vec_find_any_eq(vector bool char __a, vector bool char __b) {
  return (vector bool char)
    __builtin_s390_vfaeb((vector unsigned char)__a,
                         (vector unsigned char)__b, 4);
}

static inline __ATTRS_o_ai vector bool char
vec_find_any_eq(vector unsigned char __a, vector unsigned char __b) {
  return (vector bool char)__builtin_s390_vfaeb(__a, __b, 4);
}

static inline __ATTRS_o_ai vector bool short
vec_find_any_eq(vector signed short __a, vector signed short __b) {
  return (vector bool short)
    __builtin_s390_vfaeh((vector unsigned short)__a,
                         (vector unsigned short)__b, 4);
}

static inline __ATTRS_o_ai vector bool short
vec_find_any_eq(vector bool short __a, vector bool short __b) {
  return (vector bool short)
    __builtin_s390_vfaeh((vector unsigned short)__a,
                         (vector unsigned short)__b, 4);
}

static inline __ATTRS_o_ai vector bool short
vec_find_any_eq(vector unsigned short __a, vector unsigned short __b) {
  return (vector bool short)__builtin_s390_vfaeh(__a, __b, 4);
}

static inline __ATTRS_o_ai vector bool int
vec_find_any_eq(vector signed int __a, vector signed int __b) {
  return (vector bool int)
    __builtin_s390_vfaef((vector unsigned int)__a,
                         (vector unsigned int)__b, 4);
}

static inline __ATTRS_o_ai vector bool int
vec_find_any_eq(vector bool int __a, vector bool int __b) {
  return (vector bool int)
    __builtin_s390_vfaef((vector unsigned int)__a,
                         (vector unsigned int)__b, 4);
}

static inline __ATTRS_o_ai vector bool int
vec_find_any_eq(vector unsigned int __a, vector unsigned int __b) {
  return (vector bool int)__builtin_s390_vfaef(__a, __b, 4);
}

/*-- vec_find_any_eq_cc -----------------------------------------------------*/

static inline __ATTRS_o_ai vector bool char
vec_find_any_eq_cc(vector signed char __a, vector signed char __b, int *__cc) {
  return (vector bool char)
    __builtin_s390_vfaebs((vector unsigned char)__a,
                          (vector unsigned char)__b, 4, __cc);
}

static inline __ATTRS_o_ai vector bool char
vec_find_any_eq_cc(vector bool char __a, vector bool char __b, int *__cc) {
  return (vector bool char)
    __builtin_s390_vfaebs((vector unsigned char)__a,
                          (vector unsigned char)__b, 4, __cc);
}

static inline __ATTRS_o_ai vector bool char
vec_find_any_eq_cc(vector unsigned char __a, vector unsigned char __b,
                   int *__cc) {
  return (vector bool char)__builtin_s390_vfaebs(__a, __b, 4, __cc);
}

static inline __ATTRS_o_ai vector bool short
vec_find_any_eq_cc(vector signed short __a, vector signed short __b,
                   int *__cc) {
  return (vector bool short)
    __builtin_s390_vfaehs((vector unsigned short)__a,
                          (vector unsigned short)__b, 4, __cc);
}

static inline __ATTRS_o_ai vector bool short
vec_find_any_eq_cc(vector bool short __a, vector bool short __b, int *__cc) {
  return (vector bool short)
    __builtin_s390_vfaehs((vector unsigned short)__a,
                          (vector unsigned short)__b, 4, __cc);
}

static inline __ATTRS_o_ai vector bool short
vec_find_any_eq_cc(vector unsigned short __a, vector unsigned short __b,
                   int *__cc) {
  return (vector bool short)__builtin_s390_vfaehs(__a, __b, 4, __cc);
}

static inline __ATTRS_o_ai vector bool int
vec_find_any_eq_cc(vector signed int __a, vector signed int __b, int *__cc) {
  return (vector bool int)
    __builtin_s390_vfaefs((vector unsigned int)__a,
                          (vector unsigned int)__b, 4, __cc);
}

static inline __ATTRS_o_ai vector bool int
vec_find_any_eq_cc(vector bool int __a, vector bool int __b, int *__cc) {
  return (vector bool int)
    __builtin_s390_vfaefs((vector unsigned int)__a,
                          (vector unsigned int)__b, 4, __cc);
}

static inline __ATTRS_o_ai vector bool int
vec_find_any_eq_cc(vector unsigned int __a, vector unsigned int __b,
                   int *__cc) {
  return (vector bool int)__builtin_s390_vfaefs(__a, __b, 4, __cc);
}

/*-- vec_find_any_eq_idx ----------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_find_any_eq_idx(vector signed char __a, vector signed char __b) {
  return (vector signed char)
    __builtin_s390_vfaeb((vector unsigned char)__a,
                         (vector unsigned char)__b, 0);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_eq_idx(vector bool char __a, vector bool char __b) {
  return __builtin_s390_vfaeb((vector unsigned char)__a,
                              (vector unsigned char)__b, 0);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_eq_idx(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vfaeb(__a, __b, 0);
}

static inline __ATTRS_o_ai vector signed short
vec_find_any_eq_idx(vector signed short __a, vector signed short __b) {
  return (vector signed short)
    __builtin_s390_vfaeh((vector unsigned short)__a,
                         (vector unsigned short)__b, 0);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_eq_idx(vector bool short __a, vector bool short __b) {
  return __builtin_s390_vfaeh((vector unsigned short)__a,
                              (vector unsigned short)__b, 0);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_eq_idx(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vfaeh(__a, __b, 0);
}

static inline __ATTRS_o_ai vector signed int
vec_find_any_eq_idx(vector signed int __a, vector signed int __b) {
  return (vector signed int)
    __builtin_s390_vfaef((vector unsigned int)__a,
                         (vector unsigned int)__b, 0);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_eq_idx(vector bool int __a, vector bool int __b) {
  return __builtin_s390_vfaef((vector unsigned int)__a,
                              (vector unsigned int)__b, 0);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_eq_idx(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vfaef(__a, __b, 0);
}

/*-- vec_find_any_eq_idx_cc -------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_find_any_eq_idx_cc(vector signed char __a, vector signed char __b,
                       int *__cc) {
  return (vector signed char)
    __builtin_s390_vfaebs((vector unsigned char)__a,
                          (vector unsigned char)__b, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_eq_idx_cc(vector bool char __a, vector bool char __b, int *__cc) {
  return __builtin_s390_vfaebs((vector unsigned char)__a,
                               (vector unsigned char)__b, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_eq_idx_cc(vector unsigned char __a, vector unsigned char __b,
                       int *__cc) {
  return __builtin_s390_vfaebs(__a, __b, 0, __cc);
}

static inline __ATTRS_o_ai vector signed short
vec_find_any_eq_idx_cc(vector signed short __a, vector signed short __b,
                       int *__cc) {
  return (vector signed short)
    __builtin_s390_vfaehs((vector unsigned short)__a,
                          (vector unsigned short)__b, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_eq_idx_cc(vector bool short __a, vector bool short __b,
                       int *__cc) {
  return __builtin_s390_vfaehs((vector unsigned short)__a,
                               (vector unsigned short)__b, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_eq_idx_cc(vector unsigned short __a, vector unsigned short __b,
                       int *__cc) {
  return __builtin_s390_vfaehs(__a, __b, 0, __cc);
}

static inline __ATTRS_o_ai vector signed int
vec_find_any_eq_idx_cc(vector signed int __a, vector signed int __b,
                       int *__cc) {
  return (vector signed int)
    __builtin_s390_vfaefs((vector unsigned int)__a,
                          (vector unsigned int)__b, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_eq_idx_cc(vector bool int __a, vector bool int __b, int *__cc) {
  return __builtin_s390_vfaefs((vector unsigned int)__a,
                               (vector unsigned int)__b, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_eq_idx_cc(vector unsigned int __a, vector unsigned int __b,
                       int *__cc) {
  return __builtin_s390_vfaefs(__a, __b, 0, __cc);
}

/*-- vec_find_any_eq_or_0_idx -----------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_find_any_eq_or_0_idx(vector signed char __a, vector signed char __b) {
  return (vector signed char)
    __builtin_s390_vfaezb((vector unsigned char)__a,
                          (vector unsigned char)__b, 0);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_eq_or_0_idx(vector bool char __a, vector bool char __b) {
  return __builtin_s390_vfaezb((vector unsigned char)__a,
                               (vector unsigned char)__b, 0);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_eq_or_0_idx(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vfaezb(__a, __b, 0);
}

static inline __ATTRS_o_ai vector signed short
vec_find_any_eq_or_0_idx(vector signed short __a, vector signed short __b) {
  return (vector signed short)
    __builtin_s390_vfaezh((vector unsigned short)__a,
                          (vector unsigned short)__b, 0);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_eq_or_0_idx(vector bool short __a, vector bool short __b) {
  return __builtin_s390_vfaezh((vector unsigned short)__a,
                               (vector unsigned short)__b, 0);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_eq_or_0_idx(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vfaezh(__a, __b, 0);
}

static inline __ATTRS_o_ai vector signed int
vec_find_any_eq_or_0_idx(vector signed int __a, vector signed int __b) {
  return (vector signed int)
    __builtin_s390_vfaezf((vector unsigned int)__a,
                          (vector unsigned int)__b, 0);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_eq_or_0_idx(vector bool int __a, vector bool int __b) {
  return __builtin_s390_vfaezf((vector unsigned int)__a,
                               (vector unsigned int)__b, 0);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_eq_or_0_idx(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vfaezf(__a, __b, 0);
}

/*-- vec_find_any_eq_or_0_idx_cc --------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_find_any_eq_or_0_idx_cc(vector signed char __a, vector signed char __b,
                            int *__cc) {
  return (vector signed char)
    __builtin_s390_vfaezbs((vector unsigned char)__a,
                           (vector unsigned char)__b, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_eq_or_0_idx_cc(vector bool char __a, vector bool char __b,
                            int *__cc) {
  return __builtin_s390_vfaezbs((vector unsigned char)__a,
                                (vector unsigned char)__b, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_eq_or_0_idx_cc(vector unsigned char __a, vector unsigned char __b,
                            int *__cc) {
  return __builtin_s390_vfaezbs(__a, __b, 0, __cc);
}

static inline __ATTRS_o_ai vector signed short
vec_find_any_eq_or_0_idx_cc(vector signed short __a, vector signed short __b,
                            int *__cc) {
  return (vector signed short)
    __builtin_s390_vfaezhs((vector unsigned short)__a,
                           (vector unsigned short)__b, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_eq_or_0_idx_cc(vector bool short __a, vector bool short __b,
                            int *__cc) {
  return __builtin_s390_vfaezhs((vector unsigned short)__a,
                                (vector unsigned short)__b, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_eq_or_0_idx_cc(vector unsigned short __a,
                            vector unsigned short __b, int *__cc) {
  return __builtin_s390_vfaezhs(__a, __b, 0, __cc);
}

static inline __ATTRS_o_ai vector signed int
vec_find_any_eq_or_0_idx_cc(vector signed int __a, vector signed int __b,
                            int *__cc) {
  return (vector signed int)
    __builtin_s390_vfaezfs((vector unsigned int)__a,
                           (vector unsigned int)__b, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_eq_or_0_idx_cc(vector bool int __a, vector bool int __b,
                            int *__cc) {
  return __builtin_s390_vfaezfs((vector unsigned int)__a,
                                (vector unsigned int)__b, 0, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_eq_or_0_idx_cc(vector unsigned int __a, vector unsigned int __b,
                            int *__cc) {
  return __builtin_s390_vfaezfs(__a, __b, 0, __cc);
}

/*-- vec_find_any_ne --------------------------------------------------------*/

static inline __ATTRS_o_ai vector bool char
vec_find_any_ne(vector signed char __a, vector signed char __b) {
  return (vector bool char)
    __builtin_s390_vfaeb((vector unsigned char)__a,
                         (vector unsigned char)__b, 12);
}

static inline __ATTRS_o_ai vector bool char
vec_find_any_ne(vector bool char __a, vector bool char __b) {
  return (vector bool char)
    __builtin_s390_vfaeb((vector unsigned char)__a,
                         (vector unsigned char)__b, 12);
}

static inline __ATTRS_o_ai vector bool char
vec_find_any_ne(vector unsigned char __a, vector unsigned char __b) {
  return (vector bool char)__builtin_s390_vfaeb(__a, __b, 12);
}

static inline __ATTRS_o_ai vector bool short
vec_find_any_ne(vector signed short __a, vector signed short __b) {
  return (vector bool short)
    __builtin_s390_vfaeh((vector unsigned short)__a,
                         (vector unsigned short)__b, 12);
}

static inline __ATTRS_o_ai vector bool short
vec_find_any_ne(vector bool short __a, vector bool short __b) {
  return (vector bool short)
    __builtin_s390_vfaeh((vector unsigned short)__a,
                         (vector unsigned short)__b, 12);
}

static inline __ATTRS_o_ai vector bool short
vec_find_any_ne(vector unsigned short __a, vector unsigned short __b) {
  return (vector bool short)__builtin_s390_vfaeh(__a, __b, 12);
}

static inline __ATTRS_o_ai vector bool int
vec_find_any_ne(vector signed int __a, vector signed int __b) {
  return (vector bool int)
    __builtin_s390_vfaef((vector unsigned int)__a,
                         (vector unsigned int)__b, 12);
}

static inline __ATTRS_o_ai vector bool int
vec_find_any_ne(vector bool int __a, vector bool int __b) {
  return (vector bool int)
    __builtin_s390_vfaef((vector unsigned int)__a,
                         (vector unsigned int)__b, 12);
}

static inline __ATTRS_o_ai vector bool int
vec_find_any_ne(vector unsigned int __a, vector unsigned int __b) {
  return (vector bool int)__builtin_s390_vfaef(__a, __b, 12);
}

/*-- vec_find_any_ne_cc -----------------------------------------------------*/

static inline __ATTRS_o_ai vector bool char
vec_find_any_ne_cc(vector signed char __a, vector signed char __b, int *__cc) {
  return (vector bool char)
    __builtin_s390_vfaebs((vector unsigned char)__a,
                          (vector unsigned char)__b, 12, __cc);
}

static inline __ATTRS_o_ai vector bool char
vec_find_any_ne_cc(vector bool char __a, vector bool char __b, int *__cc) {
  return (vector bool char)
    __builtin_s390_vfaebs((vector unsigned char)__a,
                          (vector unsigned char)__b, 12, __cc);
}

static inline __ATTRS_o_ai vector bool char
vec_find_any_ne_cc(vector unsigned char __a, vector unsigned char __b,
                   int *__cc) {
  return (vector bool char)__builtin_s390_vfaebs(__a, __b, 12, __cc);
}

static inline __ATTRS_o_ai vector bool short
vec_find_any_ne_cc(vector signed short __a, vector signed short __b,
                   int *__cc) {
  return (vector bool short)
    __builtin_s390_vfaehs((vector unsigned short)__a,
                          (vector unsigned short)__b, 12, __cc);
}

static inline __ATTRS_o_ai vector bool short
vec_find_any_ne_cc(vector bool short __a, vector bool short __b, int *__cc) {
  return (vector bool short)
    __builtin_s390_vfaehs((vector unsigned short)__a,
                          (vector unsigned short)__b, 12, __cc);
}

static inline __ATTRS_o_ai vector bool short
vec_find_any_ne_cc(vector unsigned short __a, vector unsigned short __b,
                   int *__cc) {
  return (vector bool short)__builtin_s390_vfaehs(__a, __b, 12, __cc);
}

static inline __ATTRS_o_ai vector bool int
vec_find_any_ne_cc(vector signed int __a, vector signed int __b, int *__cc) {
  return (vector bool int)
    __builtin_s390_vfaefs((vector unsigned int)__a,
                          (vector unsigned int)__b, 12, __cc);
}

static inline __ATTRS_o_ai vector bool int
vec_find_any_ne_cc(vector bool int __a, vector bool int __b, int *__cc) {
  return (vector bool int)
    __builtin_s390_vfaefs((vector unsigned int)__a,
                          (vector unsigned int)__b, 12, __cc);
}

static inline __ATTRS_o_ai vector bool int
vec_find_any_ne_cc(vector unsigned int __a, vector unsigned int __b,
                   int *__cc) {
  return (vector bool int)__builtin_s390_vfaefs(__a, __b, 12, __cc);
}

/*-- vec_find_any_ne_idx ----------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_find_any_ne_idx(vector signed char __a, vector signed char __b) {
  return (vector signed char)
    __builtin_s390_vfaeb((vector unsigned char)__a,
                         (vector unsigned char)__b, 8);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_ne_idx(vector bool char __a, vector bool char __b) {
  return __builtin_s390_vfaeb((vector unsigned char)__a,
                              (vector unsigned char)__b, 8);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_ne_idx(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vfaeb(__a, __b, 8);
}

static inline __ATTRS_o_ai vector signed short
vec_find_any_ne_idx(vector signed short __a, vector signed short __b) {
  return (vector signed short)
    __builtin_s390_vfaeh((vector unsigned short)__a,
                         (vector unsigned short)__b, 8);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_ne_idx(vector bool short __a, vector bool short __b) {
  return __builtin_s390_vfaeh((vector unsigned short)__a,
                              (vector unsigned short)__b, 8);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_ne_idx(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vfaeh(__a, __b, 8);
}

static inline __ATTRS_o_ai vector signed int
vec_find_any_ne_idx(vector signed int __a, vector signed int __b) {
  return (vector signed int)
    __builtin_s390_vfaef((vector unsigned int)__a,
                         (vector unsigned int)__b, 8);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_ne_idx(vector bool int __a, vector bool int __b) {
  return __builtin_s390_vfaef((vector unsigned int)__a,
                              (vector unsigned int)__b, 8);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_ne_idx(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vfaef(__a, __b, 8);
}

/*-- vec_find_any_ne_idx_cc -------------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_find_any_ne_idx_cc(vector signed char __a, vector signed char __b,
                       int *__cc) {
  return (vector signed char)
    __builtin_s390_vfaebs((vector unsigned char)__a,
                          (vector unsigned char)__b, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_ne_idx_cc(vector bool char __a, vector bool char __b, int *__cc) {
  return __builtin_s390_vfaebs((vector unsigned char)__a,
                               (vector unsigned char)__b, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_ne_idx_cc(vector unsigned char __a, vector unsigned char __b,
                       int *__cc) {
  return __builtin_s390_vfaebs(__a, __b, 8, __cc);
}

static inline __ATTRS_o_ai vector signed short
vec_find_any_ne_idx_cc(vector signed short __a, vector signed short __b,
                       int *__cc) {
  return (vector signed short)
    __builtin_s390_vfaehs((vector unsigned short)__a,
                          (vector unsigned short)__b, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_ne_idx_cc(vector bool short __a, vector bool short __b,
                       int *__cc) {
  return __builtin_s390_vfaehs((vector unsigned short)__a,
                               (vector unsigned short)__b, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_ne_idx_cc(vector unsigned short __a, vector unsigned short __b,
                       int *__cc) {
  return __builtin_s390_vfaehs(__a, __b, 8, __cc);
}

static inline __ATTRS_o_ai vector signed int
vec_find_any_ne_idx_cc(vector signed int __a, vector signed int __b,
                       int *__cc) {
  return (vector signed int)
    __builtin_s390_vfaefs((vector unsigned int)__a,
                          (vector unsigned int)__b, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_ne_idx_cc(vector bool int __a, vector bool int __b, int *__cc) {
  return __builtin_s390_vfaefs((vector unsigned int)__a,
                               (vector unsigned int)__b, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_ne_idx_cc(vector unsigned int __a, vector unsigned int __b,
                       int *__cc) {
  return __builtin_s390_vfaefs(__a, __b, 8, __cc);
}

/*-- vec_find_any_ne_or_0_idx -----------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_find_any_ne_or_0_idx(vector signed char __a, vector signed char __b) {
  return (vector signed char)
    __builtin_s390_vfaezb((vector unsigned char)__a,
                          (vector unsigned char)__b, 8);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_ne_or_0_idx(vector bool char __a, vector bool char __b) {
  return __builtin_s390_vfaezb((vector unsigned char)__a,
                               (vector unsigned char)__b, 8);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_ne_or_0_idx(vector unsigned char __a, vector unsigned char __b) {
  return __builtin_s390_vfaezb(__a, __b, 8);
}

static inline __ATTRS_o_ai vector signed short
vec_find_any_ne_or_0_idx(vector signed short __a, vector signed short __b) {
  return (vector signed short)
    __builtin_s390_vfaezh((vector unsigned short)__a,
                          (vector unsigned short)__b, 8);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_ne_or_0_idx(vector bool short __a, vector bool short __b) {
  return __builtin_s390_vfaezh((vector unsigned short)__a,
                               (vector unsigned short)__b, 8);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_ne_or_0_idx(vector unsigned short __a, vector unsigned short __b) {
  return __builtin_s390_vfaezh(__a, __b, 8);
}

static inline __ATTRS_o_ai vector signed int
vec_find_any_ne_or_0_idx(vector signed int __a, vector signed int __b) {
  return (vector signed int)
    __builtin_s390_vfaezf((vector unsigned int)__a,
                          (vector unsigned int)__b, 8);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_ne_or_0_idx(vector bool int __a, vector bool int __b) {
  return __builtin_s390_vfaezf((vector unsigned int)__a,
                               (vector unsigned int)__b, 8);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_ne_or_0_idx(vector unsigned int __a, vector unsigned int __b) {
  return __builtin_s390_vfaezf(__a, __b, 8);
}

/*-- vec_find_any_ne_or_0_idx_cc --------------------------------------------*/

static inline __ATTRS_o_ai vector signed char
vec_find_any_ne_or_0_idx_cc(vector signed char __a, vector signed char __b,
                            int *__cc) {
  return (vector signed char)
    __builtin_s390_vfaezbs((vector unsigned char)__a,
                           (vector unsigned char)__b, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_ne_or_0_idx_cc(vector bool char __a, vector bool char __b,
                            int *__cc) {
  return __builtin_s390_vfaezbs((vector unsigned char)__a,
                                (vector unsigned char)__b, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned char
vec_find_any_ne_or_0_idx_cc(vector unsigned char __a, vector unsigned char __b,
                            int *__cc) {
  return __builtin_s390_vfaezbs(__a, __b, 8, __cc);
}

static inline __ATTRS_o_ai vector signed short
vec_find_any_ne_or_0_idx_cc(vector signed short __a, vector signed short __b,
                            int *__cc) {
  return (vector signed short)
    __builtin_s390_vfaezhs((vector unsigned short)__a,
                           (vector unsigned short)__b, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_ne_or_0_idx_cc(vector bool short __a, vector bool short __b,
                            int *__cc) {
  return __builtin_s390_vfaezhs((vector unsigned short)__a,
                                (vector unsigned short)__b, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned short
vec_find_any_ne_or_0_idx_cc(vector unsigned short __a,
                            vector unsigned short __b, int *__cc) {
  return __builtin_s390_vfaezhs(__a, __b, 8, __cc);
}

static inline __ATTRS_o_ai vector signed int
vec_find_any_ne_or_0_idx_cc(vector signed int __a, vector signed int __b,
                            int *__cc) {
  return (vector signed int)
    __builtin_s390_vfaezfs((vector unsigned int)__a,
                           (vector unsigned int)__b, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_ne_or_0_idx_cc(vector bool int __a, vector bool int __b,
                            int *__cc) {
  return __builtin_s390_vfaezfs((vector unsigned int)__a,
                                (vector unsigned int)__b, 8, __cc);
}

static inline __ATTRS_o_ai vector unsigned int
vec_find_any_ne_or_0_idx_cc(vector unsigned int __a, vector unsigned int __b,
                            int *__cc) {
  return __builtin_s390_vfaezfs(__a, __b, 8, __cc);
}

#undef __constant_pow2_range
#undef __constant_range
#undef __constant
#undef __ATTRS_o
#undef __ATTRS_o_ai
#undef __ATTRS_ai

#else

#error "Use -fzvector to enable vector extensions"

#endif
