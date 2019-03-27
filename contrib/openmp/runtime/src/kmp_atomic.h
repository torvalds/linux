/*
 * kmp_atomic.h - ATOMIC header file
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef KMP_ATOMIC_H
#define KMP_ATOMIC_H

#include "kmp_lock.h"
#include "kmp_os.h"

#if OMPT_SUPPORT
#include "ompt-specific.h"
#endif

// C++ build port.
// Intel compiler does not support _Complex datatype on win.
// Intel compiler supports _Complex datatype on lin and mac.
// On the other side, there is a problem of stack alignment on lin_32 and mac_32
// if the rhs is cmplx80 or cmplx128 typedef'ed datatype.
// The decision is: to use compiler supported _Complex type on lin and mac,
//                  to use typedef'ed types on win.
// Condition for WIN64 was modified in anticipation of 10.1 build compiler.

#if defined(__cplusplus) && (KMP_OS_WINDOWS)
// create shortcuts for c99 complex types

// Visual Studio cannot have function parameters that have the
// align __declspec attribute, so we must remove it. (Compiler Error C2719)
#if KMP_COMPILER_MSVC
#undef KMP_DO_ALIGN
#define KMP_DO_ALIGN(alignment) /* Nothing */
#endif

#if (_MSC_VER < 1600) && defined(_DEBUG)
// Workaround for the problem of _DebugHeapTag unresolved external.
// This problem prevented to use our static debug library for C tests
// compiled with /MDd option (the library itself built with /MTd),
#undef _DEBUG
#define _DEBUG_TEMPORARILY_UNSET_
#endif

#include <complex>

template <typename type_lhs, typename type_rhs>
std::complex<type_lhs> __kmp_lhs_div_rhs(const std::complex<type_lhs> &lhs,
                                         const std::complex<type_rhs> &rhs) {
  type_lhs a = lhs.real();
  type_lhs b = lhs.imag();
  type_rhs c = rhs.real();
  type_rhs d = rhs.imag();
  type_rhs den = c * c + d * d;
  type_rhs r = (a * c + b * d);
  type_rhs i = (b * c - a * d);
  std::complex<type_lhs> ret(r / den, i / den);
  return ret;
}

// complex8
struct __kmp_cmplx64_t : std::complex<double> {

  __kmp_cmplx64_t() : std::complex<double>() {}

  __kmp_cmplx64_t(const std::complex<double> &cd) : std::complex<double>(cd) {}

  void operator/=(const __kmp_cmplx64_t &rhs) {
    std::complex<double> lhs = *this;
    *this = __kmp_lhs_div_rhs(lhs, rhs);
  }

  __kmp_cmplx64_t operator/(const __kmp_cmplx64_t &rhs) {
    std::complex<double> lhs = *this;
    return __kmp_lhs_div_rhs(lhs, rhs);
  }
};
typedef struct __kmp_cmplx64_t kmp_cmplx64;

// complex4
struct __kmp_cmplx32_t : std::complex<float> {

  __kmp_cmplx32_t() : std::complex<float>() {}

  __kmp_cmplx32_t(const std::complex<float> &cf) : std::complex<float>(cf) {}

  __kmp_cmplx32_t operator+(const __kmp_cmplx32_t &b) {
    std::complex<float> lhs = *this;
    std::complex<float> rhs = b;
    return (lhs + rhs);
  }
  __kmp_cmplx32_t operator-(const __kmp_cmplx32_t &b) {
    std::complex<float> lhs = *this;
    std::complex<float> rhs = b;
    return (lhs - rhs);
  }
  __kmp_cmplx32_t operator*(const __kmp_cmplx32_t &b) {
    std::complex<float> lhs = *this;
    std::complex<float> rhs = b;
    return (lhs * rhs);
  }

  __kmp_cmplx32_t operator+(const kmp_cmplx64 &b) {
    kmp_cmplx64 t = kmp_cmplx64(*this) + b;
    std::complex<double> d(t);
    std::complex<float> f(d);
    __kmp_cmplx32_t r(f);
    return r;
  }
  __kmp_cmplx32_t operator-(const kmp_cmplx64 &b) {
    kmp_cmplx64 t = kmp_cmplx64(*this) - b;
    std::complex<double> d(t);
    std::complex<float> f(d);
    __kmp_cmplx32_t r(f);
    return r;
  }
  __kmp_cmplx32_t operator*(const kmp_cmplx64 &b) {
    kmp_cmplx64 t = kmp_cmplx64(*this) * b;
    std::complex<double> d(t);
    std::complex<float> f(d);
    __kmp_cmplx32_t r(f);
    return r;
  }

  void operator/=(const __kmp_cmplx32_t &rhs) {
    std::complex<float> lhs = *this;
    *this = __kmp_lhs_div_rhs(lhs, rhs);
  }

  __kmp_cmplx32_t operator/(const __kmp_cmplx32_t &rhs) {
    std::complex<float> lhs = *this;
    return __kmp_lhs_div_rhs(lhs, rhs);
  }

  void operator/=(const kmp_cmplx64 &rhs) {
    std::complex<float> lhs = *this;
    *this = __kmp_lhs_div_rhs(lhs, rhs);
  }

  __kmp_cmplx32_t operator/(const kmp_cmplx64 &rhs) {
    std::complex<float> lhs = *this;
    return __kmp_lhs_div_rhs(lhs, rhs);
  }
};
typedef struct __kmp_cmplx32_t kmp_cmplx32;

// complex10
struct KMP_DO_ALIGN(16) __kmp_cmplx80_t : std::complex<long double> {

  __kmp_cmplx80_t() : std::complex<long double>() {}

  __kmp_cmplx80_t(const std::complex<long double> &cld)
      : std::complex<long double>(cld) {}

  void operator/=(const __kmp_cmplx80_t &rhs) {
    std::complex<long double> lhs = *this;
    *this = __kmp_lhs_div_rhs(lhs, rhs);
  }

  __kmp_cmplx80_t operator/(const __kmp_cmplx80_t &rhs) {
    std::complex<long double> lhs = *this;
    return __kmp_lhs_div_rhs(lhs, rhs);
  }
};
typedef KMP_DO_ALIGN(16) struct __kmp_cmplx80_t kmp_cmplx80;

// complex16
#if KMP_HAVE_QUAD
struct __kmp_cmplx128_t : std::complex<_Quad> {

  __kmp_cmplx128_t() : std::complex<_Quad>() {}

  __kmp_cmplx128_t(const std::complex<_Quad> &cq) : std::complex<_Quad>(cq) {}

  void operator/=(const __kmp_cmplx128_t &rhs) {
    std::complex<_Quad> lhs = *this;
    *this = __kmp_lhs_div_rhs(lhs, rhs);
  }

  __kmp_cmplx128_t operator/(const __kmp_cmplx128_t &rhs) {
    std::complex<_Quad> lhs = *this;
    return __kmp_lhs_div_rhs(lhs, rhs);
  }
};
typedef struct __kmp_cmplx128_t kmp_cmplx128;
#endif /* KMP_HAVE_QUAD */

#ifdef _DEBUG_TEMPORARILY_UNSET_
#undef _DEBUG_TEMPORARILY_UNSET_
// Set it back now
#define _DEBUG 1
#endif

#else
// create shortcuts for c99 complex types
typedef float _Complex kmp_cmplx32;
typedef double _Complex kmp_cmplx64;
typedef long double _Complex kmp_cmplx80;
#if KMP_HAVE_QUAD
typedef _Quad _Complex kmp_cmplx128;
#endif
#endif

// Compiler 12.0 changed alignment of 16 and 32-byte arguments (like _Quad
// and kmp_cmplx128) on IA-32 architecture. The following aligned structures
// are implemented to support the old alignment in 10.1, 11.0, 11.1 and
// introduce the new alignment in 12.0. See CQ88405.
#if KMP_ARCH_X86 && KMP_HAVE_QUAD

// 4-byte aligned structures for backward compatibility.

#pragma pack(push, 4)

struct KMP_DO_ALIGN(4) Quad_a4_t {
  _Quad q;

  Quad_a4_t() : q() {}
  Quad_a4_t(const _Quad &cq) : q(cq) {}

  Quad_a4_t operator+(const Quad_a4_t &b) {
    _Quad lhs = (*this).q;
    _Quad rhs = b.q;
    return (Quad_a4_t)(lhs + rhs);
  }

  Quad_a4_t operator-(const Quad_a4_t &b) {
    _Quad lhs = (*this).q;
    _Quad rhs = b.q;
    return (Quad_a4_t)(lhs - rhs);
  }
  Quad_a4_t operator*(const Quad_a4_t &b) {
    _Quad lhs = (*this).q;
    _Quad rhs = b.q;
    return (Quad_a4_t)(lhs * rhs);
  }

  Quad_a4_t operator/(const Quad_a4_t &b) {
    _Quad lhs = (*this).q;
    _Quad rhs = b.q;
    return (Quad_a4_t)(lhs / rhs);
  }
};

struct KMP_DO_ALIGN(4) kmp_cmplx128_a4_t {
  kmp_cmplx128 q;

  kmp_cmplx128_a4_t() : q() {}

  kmp_cmplx128_a4_t(const kmp_cmplx128 &c128) : q(c128) {}

  kmp_cmplx128_a4_t operator+(const kmp_cmplx128_a4_t &b) {
    kmp_cmplx128 lhs = (*this).q;
    kmp_cmplx128 rhs = b.q;
    return (kmp_cmplx128_a4_t)(lhs + rhs);
  }
  kmp_cmplx128_a4_t operator-(const kmp_cmplx128_a4_t &b) {
    kmp_cmplx128 lhs = (*this).q;
    kmp_cmplx128 rhs = b.q;
    return (kmp_cmplx128_a4_t)(lhs - rhs);
  }
  kmp_cmplx128_a4_t operator*(const kmp_cmplx128_a4_t &b) {
    kmp_cmplx128 lhs = (*this).q;
    kmp_cmplx128 rhs = b.q;
    return (kmp_cmplx128_a4_t)(lhs * rhs);
  }

  kmp_cmplx128_a4_t operator/(const kmp_cmplx128_a4_t &b) {
    kmp_cmplx128 lhs = (*this).q;
    kmp_cmplx128 rhs = b.q;
    return (kmp_cmplx128_a4_t)(lhs / rhs);
  }
};

#pragma pack(pop)

// New 16-byte aligned structures for 12.0 compiler.
struct KMP_DO_ALIGN(16) Quad_a16_t {
  _Quad q;

  Quad_a16_t() : q() {}
  Quad_a16_t(const _Quad &cq) : q(cq) {}

  Quad_a16_t operator+(const Quad_a16_t &b) {
    _Quad lhs = (*this).q;
    _Quad rhs = b.q;
    return (Quad_a16_t)(lhs + rhs);
  }

  Quad_a16_t operator-(const Quad_a16_t &b) {
    _Quad lhs = (*this).q;
    _Quad rhs = b.q;
    return (Quad_a16_t)(lhs - rhs);
  }
  Quad_a16_t operator*(const Quad_a16_t &b) {
    _Quad lhs = (*this).q;
    _Quad rhs = b.q;
    return (Quad_a16_t)(lhs * rhs);
  }

  Quad_a16_t operator/(const Quad_a16_t &b) {
    _Quad lhs = (*this).q;
    _Quad rhs = b.q;
    return (Quad_a16_t)(lhs / rhs);
  }
};

struct KMP_DO_ALIGN(16) kmp_cmplx128_a16_t {
  kmp_cmplx128 q;

  kmp_cmplx128_a16_t() : q() {}

  kmp_cmplx128_a16_t(const kmp_cmplx128 &c128) : q(c128) {}

  kmp_cmplx128_a16_t operator+(const kmp_cmplx128_a16_t &b) {
    kmp_cmplx128 lhs = (*this).q;
    kmp_cmplx128 rhs = b.q;
    return (kmp_cmplx128_a16_t)(lhs + rhs);
  }
  kmp_cmplx128_a16_t operator-(const kmp_cmplx128_a16_t &b) {
    kmp_cmplx128 lhs = (*this).q;
    kmp_cmplx128 rhs = b.q;
    return (kmp_cmplx128_a16_t)(lhs - rhs);
  }
  kmp_cmplx128_a16_t operator*(const kmp_cmplx128_a16_t &b) {
    kmp_cmplx128 lhs = (*this).q;
    kmp_cmplx128 rhs = b.q;
    return (kmp_cmplx128_a16_t)(lhs * rhs);
  }

  kmp_cmplx128_a16_t operator/(const kmp_cmplx128_a16_t &b) {
    kmp_cmplx128 lhs = (*this).q;
    kmp_cmplx128 rhs = b.q;
    return (kmp_cmplx128_a16_t)(lhs / rhs);
  }
};

#endif

#if (KMP_ARCH_X86)
#define QUAD_LEGACY Quad_a4_t
#define CPLX128_LEG kmp_cmplx128_a4_t
#else
#define QUAD_LEGACY _Quad
#define CPLX128_LEG kmp_cmplx128
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern int __kmp_atomic_mode;

// Atomic locks can easily become contended, so we use queuing locks for them.
typedef kmp_queuing_lock_t kmp_atomic_lock_t;

static inline void __kmp_acquire_atomic_lock(kmp_atomic_lock_t *lck,
                                             kmp_int32 gtid) {
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.ompt_callback_mutex_acquire) {
    ompt_callbacks.ompt_callback(ompt_callback_mutex_acquire)(
        ompt_mutex_atomic, 0, kmp_mutex_impl_queuing, (ompt_wait_id_t)lck,
        OMPT_GET_RETURN_ADDRESS(0));
  }
#endif

  __kmp_acquire_queuing_lock(lck, gtid);

#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.ompt_callback_mutex_acquired) {
    ompt_callbacks.ompt_callback(ompt_callback_mutex_acquired)(
        ompt_mutex_atomic, (ompt_wait_id_t)lck, OMPT_GET_RETURN_ADDRESS(0));
  }
#endif
}

static inline int __kmp_test_atomic_lock(kmp_atomic_lock_t *lck,
                                         kmp_int32 gtid) {
  return __kmp_test_queuing_lock(lck, gtid);
}

static inline void __kmp_release_atomic_lock(kmp_atomic_lock_t *lck,
                                             kmp_int32 gtid) {
  __kmp_release_queuing_lock(lck, gtid);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.ompt_callback_mutex_released) {
    ompt_callbacks.ompt_callback(ompt_callback_mutex_released)(
        ompt_mutex_atomic, (ompt_wait_id_t)lck, OMPT_GET_RETURN_ADDRESS(0));
  }
#endif
}

static inline void __kmp_init_atomic_lock(kmp_atomic_lock_t *lck) {
  __kmp_init_queuing_lock(lck);
}

static inline void __kmp_destroy_atomic_lock(kmp_atomic_lock_t *lck) {
  __kmp_destroy_queuing_lock(lck);
}

// Global Locks
extern kmp_atomic_lock_t __kmp_atomic_lock; /* Control access to all user coded
                                               atomics in Gnu compat mode   */
extern kmp_atomic_lock_t __kmp_atomic_lock_1i; /* Control access to all user
                                                  coded atomics for 1-byte fixed
                                                  data types */
extern kmp_atomic_lock_t __kmp_atomic_lock_2i; /* Control access to all user
                                                  coded atomics for 2-byte fixed
                                                  data types */
extern kmp_atomic_lock_t __kmp_atomic_lock_4i; /* Control access to all user
                                                  coded atomics for 4-byte fixed
                                                  data types */
extern kmp_atomic_lock_t __kmp_atomic_lock_4r; /* Control access to all user
                                                  coded atomics for kmp_real32
                                                  data type    */
extern kmp_atomic_lock_t __kmp_atomic_lock_8i; /* Control access to all user
                                                  coded atomics for 8-byte fixed
                                                  data types */
extern kmp_atomic_lock_t __kmp_atomic_lock_8r; /* Control access to all user
                                                  coded atomics for kmp_real64
                                                  data type    */
extern kmp_atomic_lock_t
    __kmp_atomic_lock_8c; /* Control access to all user coded atomics for
                             complex byte data type  */
extern kmp_atomic_lock_t
    __kmp_atomic_lock_10r; /* Control access to all user coded atomics for long
                              double data type   */
extern kmp_atomic_lock_t __kmp_atomic_lock_16r; /* Control access to all user
                                                   coded atomics for _Quad data
                                                   type         */
extern kmp_atomic_lock_t __kmp_atomic_lock_16c; /* Control access to all user
                                                   coded atomics for double
                                                   complex data type*/
extern kmp_atomic_lock_t
    __kmp_atomic_lock_20c; /* Control access to all user coded atomics for long
                              double complex type*/
extern kmp_atomic_lock_t __kmp_atomic_lock_32c; /* Control access to all user
                                                   coded atomics for _Quad
                                                   complex data type */

//  Below routines for atomic UPDATE are listed

// 1-byte
void __kmpc_atomic_fixed1_add(ident_t *id_ref, int gtid, char *lhs, char rhs);
void __kmpc_atomic_fixed1_andb(ident_t *id_ref, int gtid, char *lhs, char rhs);
void __kmpc_atomic_fixed1_div(ident_t *id_ref, int gtid, char *lhs, char rhs);
void __kmpc_atomic_fixed1u_div(ident_t *id_ref, int gtid, unsigned char *lhs,
                               unsigned char rhs);
void __kmpc_atomic_fixed1_mul(ident_t *id_ref, int gtid, char *lhs, char rhs);
void __kmpc_atomic_fixed1_orb(ident_t *id_ref, int gtid, char *lhs, char rhs);
void __kmpc_atomic_fixed1_shl(ident_t *id_ref, int gtid, char *lhs, char rhs);
void __kmpc_atomic_fixed1_shr(ident_t *id_ref, int gtid, char *lhs, char rhs);
void __kmpc_atomic_fixed1u_shr(ident_t *id_ref, int gtid, unsigned char *lhs,
                               unsigned char rhs);
void __kmpc_atomic_fixed1_sub(ident_t *id_ref, int gtid, char *lhs, char rhs);
void __kmpc_atomic_fixed1_xor(ident_t *id_ref, int gtid, char *lhs, char rhs);
// 2-byte
void __kmpc_atomic_fixed2_add(ident_t *id_ref, int gtid, short *lhs, short rhs);
void __kmpc_atomic_fixed2_andb(ident_t *id_ref, int gtid, short *lhs,
                               short rhs);
void __kmpc_atomic_fixed2_div(ident_t *id_ref, int gtid, short *lhs, short rhs);
void __kmpc_atomic_fixed2u_div(ident_t *id_ref, int gtid, unsigned short *lhs,
                               unsigned short rhs);
void __kmpc_atomic_fixed2_mul(ident_t *id_ref, int gtid, short *lhs, short rhs);
void __kmpc_atomic_fixed2_orb(ident_t *id_ref, int gtid, short *lhs, short rhs);
void __kmpc_atomic_fixed2_shl(ident_t *id_ref, int gtid, short *lhs, short rhs);
void __kmpc_atomic_fixed2_shr(ident_t *id_ref, int gtid, short *lhs, short rhs);
void __kmpc_atomic_fixed2u_shr(ident_t *id_ref, int gtid, unsigned short *lhs,
                               unsigned short rhs);
void __kmpc_atomic_fixed2_sub(ident_t *id_ref, int gtid, short *lhs, short rhs);
void __kmpc_atomic_fixed2_xor(ident_t *id_ref, int gtid, short *lhs, short rhs);
// 4-byte add / sub fixed
void __kmpc_atomic_fixed4_add(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                              kmp_int32 rhs);
void __kmpc_atomic_fixed4_sub(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                              kmp_int32 rhs);
// 4-byte add / sub float
void __kmpc_atomic_float4_add(ident_t *id_ref, int gtid, kmp_real32 *lhs,
                              kmp_real32 rhs);
void __kmpc_atomic_float4_sub(ident_t *id_ref, int gtid, kmp_real32 *lhs,
                              kmp_real32 rhs);
// 8-byte add / sub fixed
void __kmpc_atomic_fixed8_add(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                              kmp_int64 rhs);
void __kmpc_atomic_fixed8_sub(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                              kmp_int64 rhs);
// 8-byte add / sub float
void __kmpc_atomic_float8_add(ident_t *id_ref, int gtid, kmp_real64 *lhs,
                              kmp_real64 rhs);
void __kmpc_atomic_float8_sub(ident_t *id_ref, int gtid, kmp_real64 *lhs,
                              kmp_real64 rhs);
// 4-byte fixed
void __kmpc_atomic_fixed4_andb(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                               kmp_int32 rhs);
void __kmpc_atomic_fixed4_div(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                              kmp_int32 rhs);
void __kmpc_atomic_fixed4u_div(ident_t *id_ref, int gtid, kmp_uint32 *lhs,
                               kmp_uint32 rhs);
void __kmpc_atomic_fixed4_mul(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                              kmp_int32 rhs);
void __kmpc_atomic_fixed4_orb(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                              kmp_int32 rhs);
void __kmpc_atomic_fixed4_shl(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                              kmp_int32 rhs);
void __kmpc_atomic_fixed4_shr(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                              kmp_int32 rhs);
void __kmpc_atomic_fixed4u_shr(ident_t *id_ref, int gtid, kmp_uint32 *lhs,
                               kmp_uint32 rhs);
void __kmpc_atomic_fixed4_xor(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                              kmp_int32 rhs);
// 8-byte fixed
void __kmpc_atomic_fixed8_andb(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                               kmp_int64 rhs);
void __kmpc_atomic_fixed8_div(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                              kmp_int64 rhs);
void __kmpc_atomic_fixed8u_div(ident_t *id_ref, int gtid, kmp_uint64 *lhs,
                               kmp_uint64 rhs);
void __kmpc_atomic_fixed8_mul(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                              kmp_int64 rhs);
void __kmpc_atomic_fixed8_orb(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                              kmp_int64 rhs);
void __kmpc_atomic_fixed8_shl(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                              kmp_int64 rhs);
void __kmpc_atomic_fixed8_shr(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                              kmp_int64 rhs);
void __kmpc_atomic_fixed8u_shr(ident_t *id_ref, int gtid, kmp_uint64 *lhs,
                               kmp_uint64 rhs);
void __kmpc_atomic_fixed8_xor(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                              kmp_int64 rhs);
// 4-byte float
void __kmpc_atomic_float4_div(ident_t *id_ref, int gtid, kmp_real32 *lhs,
                              kmp_real32 rhs);
void __kmpc_atomic_float4_mul(ident_t *id_ref, int gtid, kmp_real32 *lhs,
                              kmp_real32 rhs);
// 8-byte float
void __kmpc_atomic_float8_div(ident_t *id_ref, int gtid, kmp_real64 *lhs,
                              kmp_real64 rhs);
void __kmpc_atomic_float8_mul(ident_t *id_ref, int gtid, kmp_real64 *lhs,
                              kmp_real64 rhs);
// 1-, 2-, 4-, 8-byte logical (&&, ||)
void __kmpc_atomic_fixed1_andl(ident_t *id_ref, int gtid, char *lhs, char rhs);
void __kmpc_atomic_fixed1_orl(ident_t *id_ref, int gtid, char *lhs, char rhs);
void __kmpc_atomic_fixed2_andl(ident_t *id_ref, int gtid, short *lhs,
                               short rhs);
void __kmpc_atomic_fixed2_orl(ident_t *id_ref, int gtid, short *lhs, short rhs);
void __kmpc_atomic_fixed4_andl(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                               kmp_int32 rhs);
void __kmpc_atomic_fixed4_orl(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                              kmp_int32 rhs);
void __kmpc_atomic_fixed8_andl(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                               kmp_int64 rhs);
void __kmpc_atomic_fixed8_orl(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                              kmp_int64 rhs);
// MIN / MAX
void __kmpc_atomic_fixed1_max(ident_t *id_ref, int gtid, char *lhs, char rhs);
void __kmpc_atomic_fixed1_min(ident_t *id_ref, int gtid, char *lhs, char rhs);
void __kmpc_atomic_fixed2_max(ident_t *id_ref, int gtid, short *lhs, short rhs);
void __kmpc_atomic_fixed2_min(ident_t *id_ref, int gtid, short *lhs, short rhs);
void __kmpc_atomic_fixed4_max(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                              kmp_int32 rhs);
void __kmpc_atomic_fixed4_min(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                              kmp_int32 rhs);
void __kmpc_atomic_fixed8_max(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                              kmp_int64 rhs);
void __kmpc_atomic_fixed8_min(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                              kmp_int64 rhs);
void __kmpc_atomic_float4_max(ident_t *id_ref, int gtid, kmp_real32 *lhs,
                              kmp_real32 rhs);
void __kmpc_atomic_float4_min(ident_t *id_ref, int gtid, kmp_real32 *lhs,
                              kmp_real32 rhs);
void __kmpc_atomic_float8_max(ident_t *id_ref, int gtid, kmp_real64 *lhs,
                              kmp_real64 rhs);
void __kmpc_atomic_float8_min(ident_t *id_ref, int gtid, kmp_real64 *lhs,
                              kmp_real64 rhs);
#if KMP_HAVE_QUAD
void __kmpc_atomic_float16_max(ident_t *id_ref, int gtid, QUAD_LEGACY *lhs,
                               QUAD_LEGACY rhs);
void __kmpc_atomic_float16_min(ident_t *id_ref, int gtid, QUAD_LEGACY *lhs,
                               QUAD_LEGACY rhs);
#if (KMP_ARCH_X86)
// Routines with 16-byte arguments aligned to 16-byte boundary; IA-32
// architecture only
void __kmpc_atomic_float16_max_a16(ident_t *id_ref, int gtid, Quad_a16_t *lhs,
                                   Quad_a16_t rhs);
void __kmpc_atomic_float16_min_a16(ident_t *id_ref, int gtid, Quad_a16_t *lhs,
                                   Quad_a16_t rhs);
#endif
#endif
// .NEQV. (same as xor)
void __kmpc_atomic_fixed1_neqv(ident_t *id_ref, int gtid, char *lhs, char rhs);
void __kmpc_atomic_fixed2_neqv(ident_t *id_ref, int gtid, short *lhs,
                               short rhs);
void __kmpc_atomic_fixed4_neqv(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                               kmp_int32 rhs);
void __kmpc_atomic_fixed8_neqv(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                               kmp_int64 rhs);
// .EQV. (same as ~xor)
void __kmpc_atomic_fixed1_eqv(ident_t *id_ref, int gtid, char *lhs, char rhs);
void __kmpc_atomic_fixed2_eqv(ident_t *id_ref, int gtid, short *lhs, short rhs);
void __kmpc_atomic_fixed4_eqv(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                              kmp_int32 rhs);
void __kmpc_atomic_fixed8_eqv(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                              kmp_int64 rhs);
// long double type
void __kmpc_atomic_float10_add(ident_t *id_ref, int gtid, long double *lhs,
                               long double rhs);
void __kmpc_atomic_float10_sub(ident_t *id_ref, int gtid, long double *lhs,
                               long double rhs);
void __kmpc_atomic_float10_mul(ident_t *id_ref, int gtid, long double *lhs,
                               long double rhs);
void __kmpc_atomic_float10_div(ident_t *id_ref, int gtid, long double *lhs,
                               long double rhs);
// _Quad type
#if KMP_HAVE_QUAD
void __kmpc_atomic_float16_add(ident_t *id_ref, int gtid, QUAD_LEGACY *lhs,
                               QUAD_LEGACY rhs);
void __kmpc_atomic_float16_sub(ident_t *id_ref, int gtid, QUAD_LEGACY *lhs,
                               QUAD_LEGACY rhs);
void __kmpc_atomic_float16_mul(ident_t *id_ref, int gtid, QUAD_LEGACY *lhs,
                               QUAD_LEGACY rhs);
void __kmpc_atomic_float16_div(ident_t *id_ref, int gtid, QUAD_LEGACY *lhs,
                               QUAD_LEGACY rhs);
#if (KMP_ARCH_X86)
// Routines with 16-byte arguments aligned to 16-byte boundary
void __kmpc_atomic_float16_add_a16(ident_t *id_ref, int gtid, Quad_a16_t *lhs,
                                   Quad_a16_t rhs);
void __kmpc_atomic_float16_sub_a16(ident_t *id_ref, int gtid, Quad_a16_t *lhs,
                                   Quad_a16_t rhs);
void __kmpc_atomic_float16_mul_a16(ident_t *id_ref, int gtid, Quad_a16_t *lhs,
                                   Quad_a16_t rhs);
void __kmpc_atomic_float16_div_a16(ident_t *id_ref, int gtid, Quad_a16_t *lhs,
                                   Quad_a16_t rhs);
#endif
#endif
// routines for complex types
void __kmpc_atomic_cmplx4_add(ident_t *id_ref, int gtid, kmp_cmplx32 *lhs,
                              kmp_cmplx32 rhs);
void __kmpc_atomic_cmplx4_sub(ident_t *id_ref, int gtid, kmp_cmplx32 *lhs,
                              kmp_cmplx32 rhs);
void __kmpc_atomic_cmplx4_mul(ident_t *id_ref, int gtid, kmp_cmplx32 *lhs,
                              kmp_cmplx32 rhs);
void __kmpc_atomic_cmplx4_div(ident_t *id_ref, int gtid, kmp_cmplx32 *lhs,
                              kmp_cmplx32 rhs);
void __kmpc_atomic_cmplx8_add(ident_t *id_ref, int gtid, kmp_cmplx64 *lhs,
                              kmp_cmplx64 rhs);
void __kmpc_atomic_cmplx8_sub(ident_t *id_ref, int gtid, kmp_cmplx64 *lhs,
                              kmp_cmplx64 rhs);
void __kmpc_atomic_cmplx8_mul(ident_t *id_ref, int gtid, kmp_cmplx64 *lhs,
                              kmp_cmplx64 rhs);
void __kmpc_atomic_cmplx8_div(ident_t *id_ref, int gtid, kmp_cmplx64 *lhs,
                              kmp_cmplx64 rhs);
void __kmpc_atomic_cmplx10_add(ident_t *id_ref, int gtid, kmp_cmplx80 *lhs,
                               kmp_cmplx80 rhs);
void __kmpc_atomic_cmplx10_sub(ident_t *id_ref, int gtid, kmp_cmplx80 *lhs,
                               kmp_cmplx80 rhs);
void __kmpc_atomic_cmplx10_mul(ident_t *id_ref, int gtid, kmp_cmplx80 *lhs,
                               kmp_cmplx80 rhs);
void __kmpc_atomic_cmplx10_div(ident_t *id_ref, int gtid, kmp_cmplx80 *lhs,
                               kmp_cmplx80 rhs);
#if KMP_HAVE_QUAD
void __kmpc_atomic_cmplx16_add(ident_t *id_ref, int gtid, CPLX128_LEG *lhs,
                               CPLX128_LEG rhs);
void __kmpc_atomic_cmplx16_sub(ident_t *id_ref, int gtid, CPLX128_LEG *lhs,
                               CPLX128_LEG rhs);
void __kmpc_atomic_cmplx16_mul(ident_t *id_ref, int gtid, CPLX128_LEG *lhs,
                               CPLX128_LEG rhs);
void __kmpc_atomic_cmplx16_div(ident_t *id_ref, int gtid, CPLX128_LEG *lhs,
                               CPLX128_LEG rhs);
#if (KMP_ARCH_X86)
// Routines with 16-byte arguments aligned to 16-byte boundary
void __kmpc_atomic_cmplx16_add_a16(ident_t *id_ref, int gtid,
                                   kmp_cmplx128_a16_t *lhs,
                                   kmp_cmplx128_a16_t rhs);
void __kmpc_atomic_cmplx16_sub_a16(ident_t *id_ref, int gtid,
                                   kmp_cmplx128_a16_t *lhs,
                                   kmp_cmplx128_a16_t rhs);
void __kmpc_atomic_cmplx16_mul_a16(ident_t *id_ref, int gtid,
                                   kmp_cmplx128_a16_t *lhs,
                                   kmp_cmplx128_a16_t rhs);
void __kmpc_atomic_cmplx16_div_a16(ident_t *id_ref, int gtid,
                                   kmp_cmplx128_a16_t *lhs,
                                   kmp_cmplx128_a16_t rhs);
#endif
#endif

#if OMP_40_ENABLED

// OpenMP 4.0: x = expr binop x for non-commutative operations.
// Supported only on IA-32 architecture and Intel(R) 64
#if KMP_ARCH_X86 || KMP_ARCH_X86_64

void __kmpc_atomic_fixed1_sub_rev(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs);
void __kmpc_atomic_fixed1_div_rev(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs);
void __kmpc_atomic_fixed1u_div_rev(ident_t *id_ref, int gtid,
                                   unsigned char *lhs, unsigned char rhs);
void __kmpc_atomic_fixed1_shl_rev(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs);
void __kmpc_atomic_fixed1_shr_rev(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs);
void __kmpc_atomic_fixed1u_shr_rev(ident_t *id_ref, int gtid,
                                   unsigned char *lhs, unsigned char rhs);
void __kmpc_atomic_fixed2_sub_rev(ident_t *id_ref, int gtid, short *lhs,
                                  short rhs);
void __kmpc_atomic_fixed2_div_rev(ident_t *id_ref, int gtid, short *lhs,
                                  short rhs);
void __kmpc_atomic_fixed2u_div_rev(ident_t *id_ref, int gtid,
                                   unsigned short *lhs, unsigned short rhs);
void __kmpc_atomic_fixed2_shl_rev(ident_t *id_ref, int gtid, short *lhs,
                                  short rhs);
void __kmpc_atomic_fixed2_shr_rev(ident_t *id_ref, int gtid, short *lhs,
                                  short rhs);
void __kmpc_atomic_fixed2u_shr_rev(ident_t *id_ref, int gtid,
                                   unsigned short *lhs, unsigned short rhs);
void __kmpc_atomic_fixed4_sub_rev(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                                  kmp_int32 rhs);
void __kmpc_atomic_fixed4_div_rev(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                                  kmp_int32 rhs);
void __kmpc_atomic_fixed4u_div_rev(ident_t *id_ref, int gtid, kmp_uint32 *lhs,
                                   kmp_uint32 rhs);
void __kmpc_atomic_fixed4_shl_rev(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                                  kmp_int32 rhs);
void __kmpc_atomic_fixed4_shr_rev(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                                  kmp_int32 rhs);
void __kmpc_atomic_fixed4u_shr_rev(ident_t *id_ref, int gtid, kmp_uint32 *lhs,
                                   kmp_uint32 rhs);
void __kmpc_atomic_fixed8_sub_rev(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                                  kmp_int64 rhs);
void __kmpc_atomic_fixed8_div_rev(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                                  kmp_int64 rhs);
void __kmpc_atomic_fixed8u_div_rev(ident_t *id_ref, int gtid, kmp_uint64 *lhs,
                                   kmp_uint64 rhs);
void __kmpc_atomic_fixed8_shl_rev(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                                  kmp_int64 rhs);
void __kmpc_atomic_fixed8_shr_rev(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                                  kmp_int64 rhs);
void __kmpc_atomic_fixed8u_shr_rev(ident_t *id_ref, int gtid, kmp_uint64 *lhs,
                                   kmp_uint64 rhs);
void __kmpc_atomic_float4_sub_rev(ident_t *id_ref, int gtid, float *lhs,
                                  float rhs);
void __kmpc_atomic_float4_div_rev(ident_t *id_ref, int gtid, float *lhs,
                                  float rhs);
void __kmpc_atomic_float8_sub_rev(ident_t *id_ref, int gtid, double *lhs,
                                  double rhs);
void __kmpc_atomic_float8_div_rev(ident_t *id_ref, int gtid, double *lhs,
                                  double rhs);
void __kmpc_atomic_float10_sub_rev(ident_t *id_ref, int gtid, long double *lhs,
                                   long double rhs);
void __kmpc_atomic_float10_div_rev(ident_t *id_ref, int gtid, long double *lhs,
                                   long double rhs);
#if KMP_HAVE_QUAD
void __kmpc_atomic_float16_sub_rev(ident_t *id_ref, int gtid, QUAD_LEGACY *lhs,
                                   QUAD_LEGACY rhs);
void __kmpc_atomic_float16_div_rev(ident_t *id_ref, int gtid, QUAD_LEGACY *lhs,
                                   QUAD_LEGACY rhs);
#endif
void __kmpc_atomic_cmplx4_sub_rev(ident_t *id_ref, int gtid, kmp_cmplx32 *lhs,
                                  kmp_cmplx32 rhs);
void __kmpc_atomic_cmplx4_div_rev(ident_t *id_ref, int gtid, kmp_cmplx32 *lhs,
                                  kmp_cmplx32 rhs);
void __kmpc_atomic_cmplx8_sub_rev(ident_t *id_ref, int gtid, kmp_cmplx64 *lhs,
                                  kmp_cmplx64 rhs);
void __kmpc_atomic_cmplx8_div_rev(ident_t *id_ref, int gtid, kmp_cmplx64 *lhs,
                                  kmp_cmplx64 rhs);
void __kmpc_atomic_cmplx10_sub_rev(ident_t *id_ref, int gtid, kmp_cmplx80 *lhs,
                                   kmp_cmplx80 rhs);
void __kmpc_atomic_cmplx10_div_rev(ident_t *id_ref, int gtid, kmp_cmplx80 *lhs,
                                   kmp_cmplx80 rhs);
#if KMP_HAVE_QUAD
void __kmpc_atomic_cmplx16_sub_rev(ident_t *id_ref, int gtid, CPLX128_LEG *lhs,
                                   CPLX128_LEG rhs);
void __kmpc_atomic_cmplx16_div_rev(ident_t *id_ref, int gtid, CPLX128_LEG *lhs,
                                   CPLX128_LEG rhs);
#if (KMP_ARCH_X86)
// Routines with 16-byte arguments aligned to 16-byte boundary
void __kmpc_atomic_float16_sub_a16_rev(ident_t *id_ref, int gtid,
                                       Quad_a16_t *lhs, Quad_a16_t rhs);
void __kmpc_atomic_float16_div_a16_rev(ident_t *id_ref, int gtid,
                                       Quad_a16_t *lhs, Quad_a16_t rhs);
void __kmpc_atomic_cmplx16_sub_a16_rev(ident_t *id_ref, int gtid,
                                       kmp_cmplx128_a16_t *lhs,
                                       kmp_cmplx128_a16_t rhs);
void __kmpc_atomic_cmplx16_div_a16_rev(ident_t *id_ref, int gtid,
                                       kmp_cmplx128_a16_t *lhs,
                                       kmp_cmplx128_a16_t rhs);
#endif
#endif // KMP_HAVE_QUAD

#endif // KMP_ARCH_X86 || KMP_ARCH_X86_64

#endif // OMP_40_ENABLED

// routines for mixed types

// RHS=float8
void __kmpc_atomic_fixed1_mul_float8(ident_t *id_ref, int gtid, char *lhs,
                                     kmp_real64 rhs);
void __kmpc_atomic_fixed1_div_float8(ident_t *id_ref, int gtid, char *lhs,
                                     kmp_real64 rhs);
void __kmpc_atomic_fixed2_mul_float8(ident_t *id_ref, int gtid, short *lhs,
                                     kmp_real64 rhs);
void __kmpc_atomic_fixed2_div_float8(ident_t *id_ref, int gtid, short *lhs,
                                     kmp_real64 rhs);
void __kmpc_atomic_fixed4_mul_float8(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                                     kmp_real64 rhs);
void __kmpc_atomic_fixed4_div_float8(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                                     kmp_real64 rhs);
void __kmpc_atomic_fixed8_mul_float8(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                                     kmp_real64 rhs);
void __kmpc_atomic_fixed8_div_float8(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                                     kmp_real64 rhs);
void __kmpc_atomic_float4_add_float8(ident_t *id_ref, int gtid, kmp_real32 *lhs,
                                     kmp_real64 rhs);
void __kmpc_atomic_float4_sub_float8(ident_t *id_ref, int gtid, kmp_real32 *lhs,
                                     kmp_real64 rhs);
void __kmpc_atomic_float4_mul_float8(ident_t *id_ref, int gtid, kmp_real32 *lhs,
                                     kmp_real64 rhs);
void __kmpc_atomic_float4_div_float8(ident_t *id_ref, int gtid, kmp_real32 *lhs,
                                     kmp_real64 rhs);

// RHS=float16 (deprecated, to be removed when we are sure the compiler does not
// use them)
#if KMP_HAVE_QUAD
void __kmpc_atomic_fixed1_add_fp(ident_t *id_ref, int gtid, char *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed1u_add_fp(ident_t *id_ref, int gtid, unsigned char *lhs,
                                  _Quad rhs);
void __kmpc_atomic_fixed1_sub_fp(ident_t *id_ref, int gtid, char *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed1u_sub_fp(ident_t *id_ref, int gtid, unsigned char *lhs,
                                  _Quad rhs);
void __kmpc_atomic_fixed1_mul_fp(ident_t *id_ref, int gtid, char *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed1u_mul_fp(ident_t *id_ref, int gtid, unsigned char *lhs,
                                  _Quad rhs);
void __kmpc_atomic_fixed1_div_fp(ident_t *id_ref, int gtid, char *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed1u_div_fp(ident_t *id_ref, int gtid, unsigned char *lhs,
                                  _Quad rhs);

void __kmpc_atomic_fixed2_add_fp(ident_t *id_ref, int gtid, short *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed2u_add_fp(ident_t *id_ref, int gtid,
                                  unsigned short *lhs, _Quad rhs);
void __kmpc_atomic_fixed2_sub_fp(ident_t *id_ref, int gtid, short *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed2u_sub_fp(ident_t *id_ref, int gtid,
                                  unsigned short *lhs, _Quad rhs);
void __kmpc_atomic_fixed2_mul_fp(ident_t *id_ref, int gtid, short *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed2u_mul_fp(ident_t *id_ref, int gtid,
                                  unsigned short *lhs, _Quad rhs);
void __kmpc_atomic_fixed2_div_fp(ident_t *id_ref, int gtid, short *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed2u_div_fp(ident_t *id_ref, int gtid,
                                  unsigned short *lhs, _Quad rhs);

void __kmpc_atomic_fixed4_add_fp(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed4u_add_fp(ident_t *id_ref, int gtid, kmp_uint32 *lhs,
                                  _Quad rhs);
void __kmpc_atomic_fixed4_sub_fp(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed4u_sub_fp(ident_t *id_ref, int gtid, kmp_uint32 *lhs,
                                  _Quad rhs);
void __kmpc_atomic_fixed4_mul_fp(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed4u_mul_fp(ident_t *id_ref, int gtid, kmp_uint32 *lhs,
                                  _Quad rhs);
void __kmpc_atomic_fixed4_div_fp(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed4u_div_fp(ident_t *id_ref, int gtid, kmp_uint32 *lhs,
                                  _Quad rhs);

void __kmpc_atomic_fixed8_add_fp(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed8u_add_fp(ident_t *id_ref, int gtid, kmp_uint64 *lhs,
                                  _Quad rhs);
void __kmpc_atomic_fixed8_sub_fp(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed8u_sub_fp(ident_t *id_ref, int gtid, kmp_uint64 *lhs,
                                  _Quad rhs);
void __kmpc_atomic_fixed8_mul_fp(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed8u_mul_fp(ident_t *id_ref, int gtid, kmp_uint64 *lhs,
                                  _Quad rhs);
void __kmpc_atomic_fixed8_div_fp(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                                 _Quad rhs);
void __kmpc_atomic_fixed8u_div_fp(ident_t *id_ref, int gtid, kmp_uint64 *lhs,
                                  _Quad rhs);

void __kmpc_atomic_float4_add_fp(ident_t *id_ref, int gtid, kmp_real32 *lhs,
                                 _Quad rhs);
void __kmpc_atomic_float4_sub_fp(ident_t *id_ref, int gtid, kmp_real32 *lhs,
                                 _Quad rhs);
void __kmpc_atomic_float4_mul_fp(ident_t *id_ref, int gtid, kmp_real32 *lhs,
                                 _Quad rhs);
void __kmpc_atomic_float4_div_fp(ident_t *id_ref, int gtid, kmp_real32 *lhs,
                                 _Quad rhs);

void __kmpc_atomic_float8_add_fp(ident_t *id_ref, int gtid, kmp_real64 *lhs,
                                 _Quad rhs);
void __kmpc_atomic_float8_sub_fp(ident_t *id_ref, int gtid, kmp_real64 *lhs,
                                 _Quad rhs);
void __kmpc_atomic_float8_mul_fp(ident_t *id_ref, int gtid, kmp_real64 *lhs,
                                 _Quad rhs);
void __kmpc_atomic_float8_div_fp(ident_t *id_ref, int gtid, kmp_real64 *lhs,
                                 _Quad rhs);

void __kmpc_atomic_float10_add_fp(ident_t *id_ref, int gtid, long double *lhs,
                                  _Quad rhs);
void __kmpc_atomic_float10_sub_fp(ident_t *id_ref, int gtid, long double *lhs,
                                  _Quad rhs);
void __kmpc_atomic_float10_mul_fp(ident_t *id_ref, int gtid, long double *lhs,
                                  _Quad rhs);
void __kmpc_atomic_float10_div_fp(ident_t *id_ref, int gtid, long double *lhs,
                                  _Quad rhs);

// Reverse operations
void __kmpc_atomic_fixed1_sub_rev_fp(ident_t *id_ref, int gtid, char *lhs,
                                     _Quad rhs);
void __kmpc_atomic_fixed1u_sub_rev_fp(ident_t *id_ref, int gtid,
                                      unsigned char *lhs, _Quad rhs);
void __kmpc_atomic_fixed1_div_rev_fp(ident_t *id_ref, int gtid, char *lhs,
                                     _Quad rhs);
void __kmpc_atomic_fixed1u_div_rev_fp(ident_t *id_ref, int gtid,
                                      unsigned char *lhs, _Quad rhs);
void __kmpc_atomic_fixed2_sub_rev_fp(ident_t *id_ref, int gtid, short *lhs,
                                     _Quad rhs);
void __kmpc_atomic_fixed2u_sub_rev_fp(ident_t *id_ref, int gtid,
                                      unsigned short *lhs, _Quad rhs);
void __kmpc_atomic_fixed2_div_rev_fp(ident_t *id_ref, int gtid, short *lhs,
                                     _Quad rhs);
void __kmpc_atomic_fixed2u_div_rev_fp(ident_t *id_ref, int gtid,
                                      unsigned short *lhs, _Quad rhs);
void __kmpc_atomic_fixed4_sub_rev_fp(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                                     _Quad rhs);
void __kmpc_atomic_fixed4u_sub_rev_fp(ident_t *id_ref, int gtid,
                                      kmp_uint32 *lhs, _Quad rhs);
void __kmpc_atomic_fixed4_div_rev_fp(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                                     _Quad rhs);
void __kmpc_atomic_fixed4u_div_rev_fp(ident_t *id_ref, int gtid,
                                      kmp_uint32 *lhs, _Quad rhs);
void __kmpc_atomic_fixed8_sub_rev_fp(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                                     _Quad rhs);
void __kmpc_atomic_fixed8u_sub_rev_fp(ident_t *id_ref, int gtid,
                                      kmp_uint64 *lhs, _Quad rhs);
void __kmpc_atomic_fixed8_div_rev_fp(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                                     _Quad rhs);
void __kmpc_atomic_fixed8u_div_rev_fp(ident_t *id_ref, int gtid,
                                      kmp_uint64 *lhs, _Quad rhs);
void __kmpc_atomic_float4_sub_rev_fp(ident_t *id_ref, int gtid, float *lhs,
                                     _Quad rhs);
void __kmpc_atomic_float4_div_rev_fp(ident_t *id_ref, int gtid, float *lhs,
                                     _Quad rhs);
void __kmpc_atomic_float8_sub_rev_fp(ident_t *id_ref, int gtid, double *lhs,
                                     _Quad rhs);
void __kmpc_atomic_float8_div_rev_fp(ident_t *id_ref, int gtid, double *lhs,
                                     _Quad rhs);
void __kmpc_atomic_float10_sub_rev_fp(ident_t *id_ref, int gtid,
                                      long double *lhs, _Quad rhs);
void __kmpc_atomic_float10_div_rev_fp(ident_t *id_ref, int gtid,
                                      long double *lhs, _Quad rhs);

#endif // KMP_HAVE_QUAD

// RHS=cmplx8
void __kmpc_atomic_cmplx4_add_cmplx8(ident_t *id_ref, int gtid,
                                     kmp_cmplx32 *lhs, kmp_cmplx64 rhs);
void __kmpc_atomic_cmplx4_sub_cmplx8(ident_t *id_ref, int gtid,
                                     kmp_cmplx32 *lhs, kmp_cmplx64 rhs);
void __kmpc_atomic_cmplx4_mul_cmplx8(ident_t *id_ref, int gtid,
                                     kmp_cmplx32 *lhs, kmp_cmplx64 rhs);
void __kmpc_atomic_cmplx4_div_cmplx8(ident_t *id_ref, int gtid,
                                     kmp_cmplx32 *lhs, kmp_cmplx64 rhs);

// generic atomic routines
void __kmpc_atomic_1(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                     void (*f)(void *, void *, void *));
void __kmpc_atomic_2(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                     void (*f)(void *, void *, void *));
void __kmpc_atomic_4(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                     void (*f)(void *, void *, void *));
void __kmpc_atomic_8(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                     void (*f)(void *, void *, void *));
void __kmpc_atomic_10(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                      void (*f)(void *, void *, void *));
void __kmpc_atomic_16(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                      void (*f)(void *, void *, void *));
void __kmpc_atomic_20(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                      void (*f)(void *, void *, void *));
void __kmpc_atomic_32(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                      void (*f)(void *, void *, void *));

// READ, WRITE, CAPTURE are supported only on IA-32 architecture and Intel(R) 64
#if KMP_ARCH_X86 || KMP_ARCH_X86_64

//  Below routines for atomic READ are listed
char __kmpc_atomic_fixed1_rd(ident_t *id_ref, int gtid, char *loc);
short __kmpc_atomic_fixed2_rd(ident_t *id_ref, int gtid, short *loc);
kmp_int32 __kmpc_atomic_fixed4_rd(ident_t *id_ref, int gtid, kmp_int32 *loc);
kmp_int64 __kmpc_atomic_fixed8_rd(ident_t *id_ref, int gtid, kmp_int64 *loc);
kmp_real32 __kmpc_atomic_float4_rd(ident_t *id_ref, int gtid, kmp_real32 *loc);
kmp_real64 __kmpc_atomic_float8_rd(ident_t *id_ref, int gtid, kmp_real64 *loc);
long double __kmpc_atomic_float10_rd(ident_t *id_ref, int gtid,
                                     long double *loc);
#if KMP_HAVE_QUAD
QUAD_LEGACY __kmpc_atomic_float16_rd(ident_t *id_ref, int gtid,
                                     QUAD_LEGACY *loc);
#endif
// Fix for CQ220361: cmplx4 READ will return void on Windows* OS; read value
// will be returned through an additional parameter
#if (KMP_OS_WINDOWS)
void __kmpc_atomic_cmplx4_rd(kmp_cmplx32 *out, ident_t *id_ref, int gtid,
                             kmp_cmplx32 *loc);
#else
kmp_cmplx32 __kmpc_atomic_cmplx4_rd(ident_t *id_ref, int gtid,
                                    kmp_cmplx32 *loc);
#endif
kmp_cmplx64 __kmpc_atomic_cmplx8_rd(ident_t *id_ref, int gtid,
                                    kmp_cmplx64 *loc);
kmp_cmplx80 __kmpc_atomic_cmplx10_rd(ident_t *id_ref, int gtid,
                                     kmp_cmplx80 *loc);
#if KMP_HAVE_QUAD
CPLX128_LEG __kmpc_atomic_cmplx16_rd(ident_t *id_ref, int gtid,
                                     CPLX128_LEG *loc);
#if (KMP_ARCH_X86)
// Routines with 16-byte arguments aligned to 16-byte boundary
Quad_a16_t __kmpc_atomic_float16_a16_rd(ident_t *id_ref, int gtid,
                                        Quad_a16_t *loc);
kmp_cmplx128_a16_t __kmpc_atomic_cmplx16_a16_rd(ident_t *id_ref, int gtid,
                                                kmp_cmplx128_a16_t *loc);
#endif
#endif

//  Below routines for atomic WRITE are listed
void __kmpc_atomic_fixed1_wr(ident_t *id_ref, int gtid, char *lhs, char rhs);
void __kmpc_atomic_fixed2_wr(ident_t *id_ref, int gtid, short *lhs, short rhs);
void __kmpc_atomic_fixed4_wr(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                             kmp_int32 rhs);
void __kmpc_atomic_fixed8_wr(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                             kmp_int64 rhs);
void __kmpc_atomic_float4_wr(ident_t *id_ref, int gtid, kmp_real32 *lhs,
                             kmp_real32 rhs);
void __kmpc_atomic_float8_wr(ident_t *id_ref, int gtid, kmp_real64 *lhs,
                             kmp_real64 rhs);
void __kmpc_atomic_float10_wr(ident_t *id_ref, int gtid, long double *lhs,
                              long double rhs);
#if KMP_HAVE_QUAD
void __kmpc_atomic_float16_wr(ident_t *id_ref, int gtid, QUAD_LEGACY *lhs,
                              QUAD_LEGACY rhs);
#endif
void __kmpc_atomic_cmplx4_wr(ident_t *id_ref, int gtid, kmp_cmplx32 *lhs,
                             kmp_cmplx32 rhs);
void __kmpc_atomic_cmplx8_wr(ident_t *id_ref, int gtid, kmp_cmplx64 *lhs,
                             kmp_cmplx64 rhs);
void __kmpc_atomic_cmplx10_wr(ident_t *id_ref, int gtid, kmp_cmplx80 *lhs,
                              kmp_cmplx80 rhs);
#if KMP_HAVE_QUAD
void __kmpc_atomic_cmplx16_wr(ident_t *id_ref, int gtid, CPLX128_LEG *lhs,
                              CPLX128_LEG rhs);
#if (KMP_ARCH_X86)
// Routines with 16-byte arguments aligned to 16-byte boundary
void __kmpc_atomic_float16_a16_wr(ident_t *id_ref, int gtid, Quad_a16_t *lhs,
                                  Quad_a16_t rhs);
void __kmpc_atomic_cmplx16_a16_wr(ident_t *id_ref, int gtid,
                                  kmp_cmplx128_a16_t *lhs,
                                  kmp_cmplx128_a16_t rhs);
#endif
#endif

//  Below routines for atomic CAPTURE are listed

// 1-byte
char __kmpc_atomic_fixed1_add_cpt(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs, int flag);
char __kmpc_atomic_fixed1_andb_cpt(ident_t *id_ref, int gtid, char *lhs,
                                   char rhs, int flag);
char __kmpc_atomic_fixed1_div_cpt(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs, int flag);
unsigned char __kmpc_atomic_fixed1u_div_cpt(ident_t *id_ref, int gtid,
                                            unsigned char *lhs,
                                            unsigned char rhs, int flag);
char __kmpc_atomic_fixed1_mul_cpt(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs, int flag);
char __kmpc_atomic_fixed1_orb_cpt(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs, int flag);
char __kmpc_atomic_fixed1_shl_cpt(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs, int flag);
char __kmpc_atomic_fixed1_shr_cpt(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs, int flag);
unsigned char __kmpc_atomic_fixed1u_shr_cpt(ident_t *id_ref, int gtid,
                                            unsigned char *lhs,
                                            unsigned char rhs, int flag);
char __kmpc_atomic_fixed1_sub_cpt(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs, int flag);
char __kmpc_atomic_fixed1_xor_cpt(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs, int flag);
// 2-byte
short __kmpc_atomic_fixed2_add_cpt(ident_t *id_ref, int gtid, short *lhs,
                                   short rhs, int flag);
short __kmpc_atomic_fixed2_andb_cpt(ident_t *id_ref, int gtid, short *lhs,
                                    short rhs, int flag);
short __kmpc_atomic_fixed2_div_cpt(ident_t *id_ref, int gtid, short *lhs,
                                   short rhs, int flag);
unsigned short __kmpc_atomic_fixed2u_div_cpt(ident_t *id_ref, int gtid,
                                             unsigned short *lhs,
                                             unsigned short rhs, int flag);
short __kmpc_atomic_fixed2_mul_cpt(ident_t *id_ref, int gtid, short *lhs,
                                   short rhs, int flag);
short __kmpc_atomic_fixed2_orb_cpt(ident_t *id_ref, int gtid, short *lhs,
                                   short rhs, int flag);
short __kmpc_atomic_fixed2_shl_cpt(ident_t *id_ref, int gtid, short *lhs,
                                   short rhs, int flag);
short __kmpc_atomic_fixed2_shr_cpt(ident_t *id_ref, int gtid, short *lhs,
                                   short rhs, int flag);
unsigned short __kmpc_atomic_fixed2u_shr_cpt(ident_t *id_ref, int gtid,
                                             unsigned short *lhs,
                                             unsigned short rhs, int flag);
short __kmpc_atomic_fixed2_sub_cpt(ident_t *id_ref, int gtid, short *lhs,
                                   short rhs, int flag);
short __kmpc_atomic_fixed2_xor_cpt(ident_t *id_ref, int gtid, short *lhs,
                                   short rhs, int flag);
// 4-byte add / sub fixed
kmp_int32 __kmpc_atomic_fixed4_add_cpt(ident_t *id_ref, int gtid,
                                       kmp_int32 *lhs, kmp_int32 rhs, int flag);
kmp_int32 __kmpc_atomic_fixed4_sub_cpt(ident_t *id_ref, int gtid,
                                       kmp_int32 *lhs, kmp_int32 rhs, int flag);
// 4-byte add / sub float
kmp_real32 __kmpc_atomic_float4_add_cpt(ident_t *id_ref, int gtid,
                                        kmp_real32 *lhs, kmp_real32 rhs,
                                        int flag);
kmp_real32 __kmpc_atomic_float4_sub_cpt(ident_t *id_ref, int gtid,
                                        kmp_real32 *lhs, kmp_real32 rhs,
                                        int flag);
// 8-byte add / sub fixed
kmp_int64 __kmpc_atomic_fixed8_add_cpt(ident_t *id_ref, int gtid,
                                       kmp_int64 *lhs, kmp_int64 rhs, int flag);
kmp_int64 __kmpc_atomic_fixed8_sub_cpt(ident_t *id_ref, int gtid,
                                       kmp_int64 *lhs, kmp_int64 rhs, int flag);
// 8-byte add / sub float
kmp_real64 __kmpc_atomic_float8_add_cpt(ident_t *id_ref, int gtid,
                                        kmp_real64 *lhs, kmp_real64 rhs,
                                        int flag);
kmp_real64 __kmpc_atomic_float8_sub_cpt(ident_t *id_ref, int gtid,
                                        kmp_real64 *lhs, kmp_real64 rhs,
                                        int flag);
// 4-byte fixed
kmp_int32 __kmpc_atomic_fixed4_andb_cpt(ident_t *id_ref, int gtid,
                                        kmp_int32 *lhs, kmp_int32 rhs,
                                        int flag);
kmp_int32 __kmpc_atomic_fixed4_div_cpt(ident_t *id_ref, int gtid,
                                       kmp_int32 *lhs, kmp_int32 rhs, int flag);
kmp_uint32 __kmpc_atomic_fixed4u_div_cpt(ident_t *id_ref, int gtid,
                                         kmp_uint32 *lhs, kmp_uint32 rhs,
                                         int flag);
kmp_int32 __kmpc_atomic_fixed4_mul_cpt(ident_t *id_ref, int gtid,
                                       kmp_int32 *lhs, kmp_int32 rhs, int flag);
kmp_int32 __kmpc_atomic_fixed4_orb_cpt(ident_t *id_ref, int gtid,
                                       kmp_int32 *lhs, kmp_int32 rhs, int flag);
kmp_int32 __kmpc_atomic_fixed4_shl_cpt(ident_t *id_ref, int gtid,
                                       kmp_int32 *lhs, kmp_int32 rhs, int flag);
kmp_int32 __kmpc_atomic_fixed4_shr_cpt(ident_t *id_ref, int gtid,
                                       kmp_int32 *lhs, kmp_int32 rhs, int flag);
kmp_uint32 __kmpc_atomic_fixed4u_shr_cpt(ident_t *id_ref, int gtid,
                                         kmp_uint32 *lhs, kmp_uint32 rhs,
                                         int flag);
kmp_int32 __kmpc_atomic_fixed4_xor_cpt(ident_t *id_ref, int gtid,
                                       kmp_int32 *lhs, kmp_int32 rhs, int flag);
// 8-byte fixed
kmp_int64 __kmpc_atomic_fixed8_andb_cpt(ident_t *id_ref, int gtid,
                                        kmp_int64 *lhs, kmp_int64 rhs,
                                        int flag);
kmp_int64 __kmpc_atomic_fixed8_div_cpt(ident_t *id_ref, int gtid,
                                       kmp_int64 *lhs, kmp_int64 rhs, int flag);
kmp_uint64 __kmpc_atomic_fixed8u_div_cpt(ident_t *id_ref, int gtid,
                                         kmp_uint64 *lhs, kmp_uint64 rhs,
                                         int flag);
kmp_int64 __kmpc_atomic_fixed8_mul_cpt(ident_t *id_ref, int gtid,
                                       kmp_int64 *lhs, kmp_int64 rhs, int flag);
kmp_int64 __kmpc_atomic_fixed8_orb_cpt(ident_t *id_ref, int gtid,
                                       kmp_int64 *lhs, kmp_int64 rhs, int flag);
kmp_int64 __kmpc_atomic_fixed8_shl_cpt(ident_t *id_ref, int gtid,
                                       kmp_int64 *lhs, kmp_int64 rhs, int flag);
kmp_int64 __kmpc_atomic_fixed8_shr_cpt(ident_t *id_ref, int gtid,
                                       kmp_int64 *lhs, kmp_int64 rhs, int flag);
kmp_uint64 __kmpc_atomic_fixed8u_shr_cpt(ident_t *id_ref, int gtid,
                                         kmp_uint64 *lhs, kmp_uint64 rhs,
                                         int flag);
kmp_int64 __kmpc_atomic_fixed8_xor_cpt(ident_t *id_ref, int gtid,
                                       kmp_int64 *lhs, kmp_int64 rhs, int flag);
// 4-byte float
kmp_real32 __kmpc_atomic_float4_div_cpt(ident_t *id_ref, int gtid,
                                        kmp_real32 *lhs, kmp_real32 rhs,
                                        int flag);
kmp_real32 __kmpc_atomic_float4_mul_cpt(ident_t *id_ref, int gtid,
                                        kmp_real32 *lhs, kmp_real32 rhs,
                                        int flag);
// 8-byte float
kmp_real64 __kmpc_atomic_float8_div_cpt(ident_t *id_ref, int gtid,
                                        kmp_real64 *lhs, kmp_real64 rhs,
                                        int flag);
kmp_real64 __kmpc_atomic_float8_mul_cpt(ident_t *id_ref, int gtid,
                                        kmp_real64 *lhs, kmp_real64 rhs,
                                        int flag);
// 1-, 2-, 4-, 8-byte logical (&&, ||)
char __kmpc_atomic_fixed1_andl_cpt(ident_t *id_ref, int gtid, char *lhs,
                                   char rhs, int flag);
char __kmpc_atomic_fixed1_orl_cpt(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs, int flag);
short __kmpc_atomic_fixed2_andl_cpt(ident_t *id_ref, int gtid, short *lhs,
                                    short rhs, int flag);
short __kmpc_atomic_fixed2_orl_cpt(ident_t *id_ref, int gtid, short *lhs,
                                   short rhs, int flag);
kmp_int32 __kmpc_atomic_fixed4_andl_cpt(ident_t *id_ref, int gtid,
                                        kmp_int32 *lhs, kmp_int32 rhs,
                                        int flag);
kmp_int32 __kmpc_atomic_fixed4_orl_cpt(ident_t *id_ref, int gtid,
                                       kmp_int32 *lhs, kmp_int32 rhs, int flag);
kmp_int64 __kmpc_atomic_fixed8_andl_cpt(ident_t *id_ref, int gtid,
                                        kmp_int64 *lhs, kmp_int64 rhs,
                                        int flag);
kmp_int64 __kmpc_atomic_fixed8_orl_cpt(ident_t *id_ref, int gtid,
                                       kmp_int64 *lhs, kmp_int64 rhs, int flag);
// MIN / MAX
char __kmpc_atomic_fixed1_max_cpt(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs, int flag);
char __kmpc_atomic_fixed1_min_cpt(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs, int flag);
short __kmpc_atomic_fixed2_max_cpt(ident_t *id_ref, int gtid, short *lhs,
                                   short rhs, int flag);
short __kmpc_atomic_fixed2_min_cpt(ident_t *id_ref, int gtid, short *lhs,
                                   short rhs, int flag);
kmp_int32 __kmpc_atomic_fixed4_max_cpt(ident_t *id_ref, int gtid,
                                       kmp_int32 *lhs, kmp_int32 rhs, int flag);
kmp_int32 __kmpc_atomic_fixed4_min_cpt(ident_t *id_ref, int gtid,
                                       kmp_int32 *lhs, kmp_int32 rhs, int flag);
kmp_int64 __kmpc_atomic_fixed8_max_cpt(ident_t *id_ref, int gtid,
                                       kmp_int64 *lhs, kmp_int64 rhs, int flag);
kmp_int64 __kmpc_atomic_fixed8_min_cpt(ident_t *id_ref, int gtid,
                                       kmp_int64 *lhs, kmp_int64 rhs, int flag);
kmp_real32 __kmpc_atomic_float4_max_cpt(ident_t *id_ref, int gtid,
                                        kmp_real32 *lhs, kmp_real32 rhs,
                                        int flag);
kmp_real32 __kmpc_atomic_float4_min_cpt(ident_t *id_ref, int gtid,
                                        kmp_real32 *lhs, kmp_real32 rhs,
                                        int flag);
kmp_real64 __kmpc_atomic_float8_max_cpt(ident_t *id_ref, int gtid,
                                        kmp_real64 *lhs, kmp_real64 rhs,
                                        int flag);
kmp_real64 __kmpc_atomic_float8_min_cpt(ident_t *id_ref, int gtid,
                                        kmp_real64 *lhs, kmp_real64 rhs,
                                        int flag);
#if KMP_HAVE_QUAD
QUAD_LEGACY __kmpc_atomic_float16_max_cpt(ident_t *id_ref, int gtid,
                                          QUAD_LEGACY *lhs, QUAD_LEGACY rhs,
                                          int flag);
QUAD_LEGACY __kmpc_atomic_float16_min_cpt(ident_t *id_ref, int gtid,
                                          QUAD_LEGACY *lhs, QUAD_LEGACY rhs,
                                          int flag);
#endif
// .NEQV. (same as xor)
char __kmpc_atomic_fixed1_neqv_cpt(ident_t *id_ref, int gtid, char *lhs,
                                   char rhs, int flag);
short __kmpc_atomic_fixed2_neqv_cpt(ident_t *id_ref, int gtid, short *lhs,
                                    short rhs, int flag);
kmp_int32 __kmpc_atomic_fixed4_neqv_cpt(ident_t *id_ref, int gtid,
                                        kmp_int32 *lhs, kmp_int32 rhs,
                                        int flag);
kmp_int64 __kmpc_atomic_fixed8_neqv_cpt(ident_t *id_ref, int gtid,
                                        kmp_int64 *lhs, kmp_int64 rhs,
                                        int flag);
// .EQV. (same as ~xor)
char __kmpc_atomic_fixed1_eqv_cpt(ident_t *id_ref, int gtid, char *lhs,
                                  char rhs, int flag);
short __kmpc_atomic_fixed2_eqv_cpt(ident_t *id_ref, int gtid, short *lhs,
                                   short rhs, int flag);
kmp_int32 __kmpc_atomic_fixed4_eqv_cpt(ident_t *id_ref, int gtid,
                                       kmp_int32 *lhs, kmp_int32 rhs, int flag);
kmp_int64 __kmpc_atomic_fixed8_eqv_cpt(ident_t *id_ref, int gtid,
                                       kmp_int64 *lhs, kmp_int64 rhs, int flag);
// long double type
long double __kmpc_atomic_float10_add_cpt(ident_t *id_ref, int gtid,
                                          long double *lhs, long double rhs,
                                          int flag);
long double __kmpc_atomic_float10_sub_cpt(ident_t *id_ref, int gtid,
                                          long double *lhs, long double rhs,
                                          int flag);
long double __kmpc_atomic_float10_mul_cpt(ident_t *id_ref, int gtid,
                                          long double *lhs, long double rhs,
                                          int flag);
long double __kmpc_atomic_float10_div_cpt(ident_t *id_ref, int gtid,
                                          long double *lhs, long double rhs,
                                          int flag);
#if KMP_HAVE_QUAD
// _Quad type
QUAD_LEGACY __kmpc_atomic_float16_add_cpt(ident_t *id_ref, int gtid,
                                          QUAD_LEGACY *lhs, QUAD_LEGACY rhs,
                                          int flag);
QUAD_LEGACY __kmpc_atomic_float16_sub_cpt(ident_t *id_ref, int gtid,
                                          QUAD_LEGACY *lhs, QUAD_LEGACY rhs,
                                          int flag);
QUAD_LEGACY __kmpc_atomic_float16_mul_cpt(ident_t *id_ref, int gtid,
                                          QUAD_LEGACY *lhs, QUAD_LEGACY rhs,
                                          int flag);
QUAD_LEGACY __kmpc_atomic_float16_div_cpt(ident_t *id_ref, int gtid,
                                          QUAD_LEGACY *lhs, QUAD_LEGACY rhs,
                                          int flag);
#endif
// routines for complex types
// Workaround for cmplx4 routines - return void; captured value is returned via
// the argument
void __kmpc_atomic_cmplx4_add_cpt(ident_t *id_ref, int gtid, kmp_cmplx32 *lhs,
                                  kmp_cmplx32 rhs, kmp_cmplx32 *out, int flag);
void __kmpc_atomic_cmplx4_sub_cpt(ident_t *id_ref, int gtid, kmp_cmplx32 *lhs,
                                  kmp_cmplx32 rhs, kmp_cmplx32 *out, int flag);
void __kmpc_atomic_cmplx4_mul_cpt(ident_t *id_ref, int gtid, kmp_cmplx32 *lhs,
                                  kmp_cmplx32 rhs, kmp_cmplx32 *out, int flag);
void __kmpc_atomic_cmplx4_div_cpt(ident_t *id_ref, int gtid, kmp_cmplx32 *lhs,
                                  kmp_cmplx32 rhs, kmp_cmplx32 *out, int flag);

kmp_cmplx64 __kmpc_atomic_cmplx8_add_cpt(ident_t *id_ref, int gtid,
                                         kmp_cmplx64 *lhs, kmp_cmplx64 rhs,
                                         int flag);
kmp_cmplx64 __kmpc_atomic_cmplx8_sub_cpt(ident_t *id_ref, int gtid,
                                         kmp_cmplx64 *lhs, kmp_cmplx64 rhs,
                                         int flag);
kmp_cmplx64 __kmpc_atomic_cmplx8_mul_cpt(ident_t *id_ref, int gtid,
                                         kmp_cmplx64 *lhs, kmp_cmplx64 rhs,
                                         int flag);
kmp_cmplx64 __kmpc_atomic_cmplx8_div_cpt(ident_t *id_ref, int gtid,
                                         kmp_cmplx64 *lhs, kmp_cmplx64 rhs,
                                         int flag);
kmp_cmplx80 __kmpc_atomic_cmplx10_add_cpt(ident_t *id_ref, int gtid,
                                          kmp_cmplx80 *lhs, kmp_cmplx80 rhs,
                                          int flag);
kmp_cmplx80 __kmpc_atomic_cmplx10_sub_cpt(ident_t *id_ref, int gtid,
                                          kmp_cmplx80 *lhs, kmp_cmplx80 rhs,
                                          int flag);
kmp_cmplx80 __kmpc_atomic_cmplx10_mul_cpt(ident_t *id_ref, int gtid,
                                          kmp_cmplx80 *lhs, kmp_cmplx80 rhs,
                                          int flag);
kmp_cmplx80 __kmpc_atomic_cmplx10_div_cpt(ident_t *id_ref, int gtid,
                                          kmp_cmplx80 *lhs, kmp_cmplx80 rhs,
                                          int flag);
#if KMP_HAVE_QUAD
CPLX128_LEG __kmpc_atomic_cmplx16_add_cpt(ident_t *id_ref, int gtid,
                                          CPLX128_LEG *lhs, CPLX128_LEG rhs,
                                          int flag);
CPLX128_LEG __kmpc_atomic_cmplx16_sub_cpt(ident_t *id_ref, int gtid,
                                          CPLX128_LEG *lhs, CPLX128_LEG rhs,
                                          int flag);
CPLX128_LEG __kmpc_atomic_cmplx16_mul_cpt(ident_t *id_ref, int gtid,
                                          CPLX128_LEG *lhs, CPLX128_LEG rhs,
                                          int flag);
CPLX128_LEG __kmpc_atomic_cmplx16_div_cpt(ident_t *id_ref, int gtid,
                                          CPLX128_LEG *lhs, CPLX128_LEG rhs,
                                          int flag);
#if (KMP_ARCH_X86)
// Routines with 16-byte arguments aligned to 16-byte boundary
Quad_a16_t __kmpc_atomic_float16_add_a16_cpt(ident_t *id_ref, int gtid,
                                             Quad_a16_t *lhs, Quad_a16_t rhs,
                                             int flag);
Quad_a16_t __kmpc_atomic_float16_sub_a16_cpt(ident_t *id_ref, int gtid,
                                             Quad_a16_t *lhs, Quad_a16_t rhs,
                                             int flag);
Quad_a16_t __kmpc_atomic_float16_mul_a16_cpt(ident_t *id_ref, int gtid,
                                             Quad_a16_t *lhs, Quad_a16_t rhs,
                                             int flag);
Quad_a16_t __kmpc_atomic_float16_div_a16_cpt(ident_t *id_ref, int gtid,
                                             Quad_a16_t *lhs, Quad_a16_t rhs,
                                             int flag);
Quad_a16_t __kmpc_atomic_float16_max_a16_cpt(ident_t *id_ref, int gtid,
                                             Quad_a16_t *lhs, Quad_a16_t rhs,
                                             int flag);
Quad_a16_t __kmpc_atomic_float16_min_a16_cpt(ident_t *id_ref, int gtid,
                                             Quad_a16_t *lhs, Quad_a16_t rhs,
                                             int flag);
kmp_cmplx128_a16_t __kmpc_atomic_cmplx16_add_a16_cpt(ident_t *id_ref, int gtid,
                                                     kmp_cmplx128_a16_t *lhs,
                                                     kmp_cmplx128_a16_t rhs,
                                                     int flag);
kmp_cmplx128_a16_t __kmpc_atomic_cmplx16_sub_a16_cpt(ident_t *id_ref, int gtid,
                                                     kmp_cmplx128_a16_t *lhs,
                                                     kmp_cmplx128_a16_t rhs,
                                                     int flag);
kmp_cmplx128_a16_t __kmpc_atomic_cmplx16_mul_a16_cpt(ident_t *id_ref, int gtid,
                                                     kmp_cmplx128_a16_t *lhs,
                                                     kmp_cmplx128_a16_t rhs,
                                                     int flag);
kmp_cmplx128_a16_t __kmpc_atomic_cmplx16_div_a16_cpt(ident_t *id_ref, int gtid,
                                                     kmp_cmplx128_a16_t *lhs,
                                                     kmp_cmplx128_a16_t rhs,
                                                     int flag);
#endif
#endif

void __kmpc_atomic_start(void);
void __kmpc_atomic_end(void);

#if OMP_40_ENABLED

// OpenMP 4.0: v = x = expr binop x; { v = x; x = expr binop x; } { x = expr
// binop x; v = x; }  for non-commutative operations.

char __kmpc_atomic_fixed1_sub_cpt_rev(ident_t *id_ref, int gtid, char *lhs,
                                      char rhs, int flag);
char __kmpc_atomic_fixed1_div_cpt_rev(ident_t *id_ref, int gtid, char *lhs,
                                      char rhs, int flag);
unsigned char __kmpc_atomic_fixed1u_div_cpt_rev(ident_t *id_ref, int gtid,
                                                unsigned char *lhs,
                                                unsigned char rhs, int flag);
char __kmpc_atomic_fixed1_shl_cpt_rev(ident_t *id_ref, int gtid, char *lhs,
                                      char rhs, int flag);
char __kmpc_atomic_fixed1_shr_cpt_rev(ident_t *id_ref, int gtid, char *lhs,
                                      char rhs, int flag);
unsigned char __kmpc_atomic_fixed1u_shr_cpt_rev(ident_t *id_ref, int gtid,
                                                unsigned char *lhs,
                                                unsigned char rhs, int flag);
short __kmpc_atomic_fixed2_sub_cpt_rev(ident_t *id_ref, int gtid, short *lhs,
                                       short rhs, int flag);
short __kmpc_atomic_fixed2_div_cpt_rev(ident_t *id_ref, int gtid, short *lhs,
                                       short rhs, int flag);
unsigned short __kmpc_atomic_fixed2u_div_cpt_rev(ident_t *id_ref, int gtid,
                                                 unsigned short *lhs,
                                                 unsigned short rhs, int flag);
short __kmpc_atomic_fixed2_shl_cpt_rev(ident_t *id_ref, int gtid, short *lhs,
                                       short rhs, int flag);
short __kmpc_atomic_fixed2_shr_cpt_rev(ident_t *id_ref, int gtid, short *lhs,
                                       short rhs, int flag);
unsigned short __kmpc_atomic_fixed2u_shr_cpt_rev(ident_t *id_ref, int gtid,
                                                 unsigned short *lhs,
                                                 unsigned short rhs, int flag);
kmp_int32 __kmpc_atomic_fixed4_sub_cpt_rev(ident_t *id_ref, int gtid,
                                           kmp_int32 *lhs, kmp_int32 rhs,
                                           int flag);
kmp_int32 __kmpc_atomic_fixed4_div_cpt_rev(ident_t *id_ref, int gtid,
                                           kmp_int32 *lhs, kmp_int32 rhs,
                                           int flag);
kmp_uint32 __kmpc_atomic_fixed4u_div_cpt_rev(ident_t *id_ref, int gtid,
                                             kmp_uint32 *lhs, kmp_uint32 rhs,
                                             int flag);
kmp_int32 __kmpc_atomic_fixed4_shl_cpt_rev(ident_t *id_ref, int gtid,
                                           kmp_int32 *lhs, kmp_int32 rhs,
                                           int flag);
kmp_int32 __kmpc_atomic_fixed4_shr_cpt_rev(ident_t *id_ref, int gtid,
                                           kmp_int32 *lhs, kmp_int32 rhs,
                                           int flag);
kmp_uint32 __kmpc_atomic_fixed4u_shr_cpt_rev(ident_t *id_ref, int gtid,
                                             kmp_uint32 *lhs, kmp_uint32 rhs,
                                             int flag);
kmp_int64 __kmpc_atomic_fixed8_sub_cpt_rev(ident_t *id_ref, int gtid,
                                           kmp_int64 *lhs, kmp_int64 rhs,
                                           int flag);
kmp_int64 __kmpc_atomic_fixed8_div_cpt_rev(ident_t *id_ref, int gtid,
                                           kmp_int64 *lhs, kmp_int64 rhs,
                                           int flag);
kmp_uint64 __kmpc_atomic_fixed8u_div_cpt_rev(ident_t *id_ref, int gtid,
                                             kmp_uint64 *lhs, kmp_uint64 rhs,
                                             int flag);
kmp_int64 __kmpc_atomic_fixed8_shl_cpt_rev(ident_t *id_ref, int gtid,
                                           kmp_int64 *lhs, kmp_int64 rhs,
                                           int flag);
kmp_int64 __kmpc_atomic_fixed8_shr_cpt_rev(ident_t *id_ref, int gtid,
                                           kmp_int64 *lhs, kmp_int64 rhs,
                                           int flag);
kmp_uint64 __kmpc_atomic_fixed8u_shr_cpt_rev(ident_t *id_ref, int gtid,
                                             kmp_uint64 *lhs, kmp_uint64 rhs,
                                             int flag);
float __kmpc_atomic_float4_sub_cpt_rev(ident_t *id_ref, int gtid, float *lhs,
                                       float rhs, int flag);
float __kmpc_atomic_float4_div_cpt_rev(ident_t *id_ref, int gtid, float *lhs,
                                       float rhs, int flag);
double __kmpc_atomic_float8_sub_cpt_rev(ident_t *id_ref, int gtid, double *lhs,
                                        double rhs, int flag);
double __kmpc_atomic_float8_div_cpt_rev(ident_t *id_ref, int gtid, double *lhs,
                                        double rhs, int flag);
long double __kmpc_atomic_float10_sub_cpt_rev(ident_t *id_ref, int gtid,
                                              long double *lhs, long double rhs,
                                              int flag);
long double __kmpc_atomic_float10_div_cpt_rev(ident_t *id_ref, int gtid,
                                              long double *lhs, long double rhs,
                                              int flag);
#if KMP_HAVE_QUAD
QUAD_LEGACY __kmpc_atomic_float16_sub_cpt_rev(ident_t *id_ref, int gtid,
                                              QUAD_LEGACY *lhs, QUAD_LEGACY rhs,
                                              int flag);
QUAD_LEGACY __kmpc_atomic_float16_div_cpt_rev(ident_t *id_ref, int gtid,
                                              QUAD_LEGACY *lhs, QUAD_LEGACY rhs,
                                              int flag);
#endif
// Workaround for cmplx4 routines - return void; captured value is returned via
// the argument
void __kmpc_atomic_cmplx4_sub_cpt_rev(ident_t *id_ref, int gtid,
                                      kmp_cmplx32 *lhs, kmp_cmplx32 rhs,
                                      kmp_cmplx32 *out, int flag);
void __kmpc_atomic_cmplx4_div_cpt_rev(ident_t *id_ref, int gtid,
                                      kmp_cmplx32 *lhs, kmp_cmplx32 rhs,
                                      kmp_cmplx32 *out, int flag);
kmp_cmplx64 __kmpc_atomic_cmplx8_sub_cpt_rev(ident_t *id_ref, int gtid,
                                             kmp_cmplx64 *lhs, kmp_cmplx64 rhs,
                                             int flag);
kmp_cmplx64 __kmpc_atomic_cmplx8_div_cpt_rev(ident_t *id_ref, int gtid,
                                             kmp_cmplx64 *lhs, kmp_cmplx64 rhs,
                                             int flag);
kmp_cmplx80 __kmpc_atomic_cmplx10_sub_cpt_rev(ident_t *id_ref, int gtid,
                                              kmp_cmplx80 *lhs, kmp_cmplx80 rhs,
                                              int flag);
kmp_cmplx80 __kmpc_atomic_cmplx10_div_cpt_rev(ident_t *id_ref, int gtid,
                                              kmp_cmplx80 *lhs, kmp_cmplx80 rhs,
                                              int flag);
#if KMP_HAVE_QUAD
CPLX128_LEG __kmpc_atomic_cmplx16_sub_cpt_rev(ident_t *id_ref, int gtid,
                                              CPLX128_LEG *lhs, CPLX128_LEG rhs,
                                              int flag);
CPLX128_LEG __kmpc_atomic_cmplx16_div_cpt_rev(ident_t *id_ref, int gtid,
                                              CPLX128_LEG *lhs, CPLX128_LEG rhs,
                                              int flag);
#if (KMP_ARCH_X86)
Quad_a16_t __kmpc_atomic_float16_sub_a16_cpt_rev(ident_t *id_ref, int gtid,
                                                 Quad_a16_t *lhs,
                                                 Quad_a16_t rhs, int flag);
Quad_a16_t __kmpc_atomic_float16_div_a16_cpt_rev(ident_t *id_ref, int gtid,
                                                 Quad_a16_t *lhs,
                                                 Quad_a16_t rhs, int flag);
kmp_cmplx128_a16_t
__kmpc_atomic_cmplx16_sub_a16_cpt_rev(ident_t *id_ref, int gtid,
                                      kmp_cmplx128_a16_t *lhs,
                                      kmp_cmplx128_a16_t rhs, int flag);
kmp_cmplx128_a16_t
__kmpc_atomic_cmplx16_div_a16_cpt_rev(ident_t *id_ref, int gtid,
                                      kmp_cmplx128_a16_t *lhs,
                                      kmp_cmplx128_a16_t rhs, int flag);
#endif
#endif

//   OpenMP 4.0 Capture-write (swap): {v = x; x = expr;}
char __kmpc_atomic_fixed1_swp(ident_t *id_ref, int gtid, char *lhs, char rhs);
short __kmpc_atomic_fixed2_swp(ident_t *id_ref, int gtid, short *lhs,
                               short rhs);
kmp_int32 __kmpc_atomic_fixed4_swp(ident_t *id_ref, int gtid, kmp_int32 *lhs,
                                   kmp_int32 rhs);
kmp_int64 __kmpc_atomic_fixed8_swp(ident_t *id_ref, int gtid, kmp_int64 *lhs,
                                   kmp_int64 rhs);
float __kmpc_atomic_float4_swp(ident_t *id_ref, int gtid, float *lhs,
                               float rhs);
double __kmpc_atomic_float8_swp(ident_t *id_ref, int gtid, double *lhs,
                                double rhs);
long double __kmpc_atomic_float10_swp(ident_t *id_ref, int gtid,
                                      long double *lhs, long double rhs);
#if KMP_HAVE_QUAD
QUAD_LEGACY __kmpc_atomic_float16_swp(ident_t *id_ref, int gtid,
                                      QUAD_LEGACY *lhs, QUAD_LEGACY rhs);
#endif
// !!! TODO: check if we need a workaround here
void __kmpc_atomic_cmplx4_swp(ident_t *id_ref, int gtid, kmp_cmplx32 *lhs,
                              kmp_cmplx32 rhs, kmp_cmplx32 *out);
// kmp_cmplx32   	__kmpc_atomic_cmplx4_swp(  ident_t *id_ref, int gtid,
// kmp_cmplx32 * lhs, kmp_cmplx32 rhs );

kmp_cmplx64 __kmpc_atomic_cmplx8_swp(ident_t *id_ref, int gtid,
                                     kmp_cmplx64 *lhs, kmp_cmplx64 rhs);
kmp_cmplx80 __kmpc_atomic_cmplx10_swp(ident_t *id_ref, int gtid,
                                      kmp_cmplx80 *lhs, kmp_cmplx80 rhs);
#if KMP_HAVE_QUAD
CPLX128_LEG __kmpc_atomic_cmplx16_swp(ident_t *id_ref, int gtid,
                                      CPLX128_LEG *lhs, CPLX128_LEG rhs);
#if (KMP_ARCH_X86)
Quad_a16_t __kmpc_atomic_float16_a16_swp(ident_t *id_ref, int gtid,
                                         Quad_a16_t *lhs, Quad_a16_t rhs);
kmp_cmplx128_a16_t __kmpc_atomic_cmplx16_a16_swp(ident_t *id_ref, int gtid,
                                                 kmp_cmplx128_a16_t *lhs,
                                                 kmp_cmplx128_a16_t rhs);
#endif
#endif

// Capture routines for mixed types (RHS=float16)
#if KMP_HAVE_QUAD

char __kmpc_atomic_fixed1_add_cpt_fp(ident_t *id_ref, int gtid, char *lhs,
                                     _Quad rhs, int flag);
char __kmpc_atomic_fixed1_sub_cpt_fp(ident_t *id_ref, int gtid, char *lhs,
                                     _Quad rhs, int flag);
char __kmpc_atomic_fixed1_mul_cpt_fp(ident_t *id_ref, int gtid, char *lhs,
                                     _Quad rhs, int flag);
char __kmpc_atomic_fixed1_div_cpt_fp(ident_t *id_ref, int gtid, char *lhs,
                                     _Quad rhs, int flag);
unsigned char __kmpc_atomic_fixed1u_add_cpt_fp(ident_t *id_ref, int gtid,
                                               unsigned char *lhs, _Quad rhs,
                                               int flag);
unsigned char __kmpc_atomic_fixed1u_sub_cpt_fp(ident_t *id_ref, int gtid,
                                               unsigned char *lhs, _Quad rhs,
                                               int flag);
unsigned char __kmpc_atomic_fixed1u_mul_cpt_fp(ident_t *id_ref, int gtid,
                                               unsigned char *lhs, _Quad rhs,
                                               int flag);
unsigned char __kmpc_atomic_fixed1u_div_cpt_fp(ident_t *id_ref, int gtid,
                                               unsigned char *lhs, _Quad rhs,
                                               int flag);

short __kmpc_atomic_fixed2_add_cpt_fp(ident_t *id_ref, int gtid, short *lhs,
                                      _Quad rhs, int flag);
short __kmpc_atomic_fixed2_sub_cpt_fp(ident_t *id_ref, int gtid, short *lhs,
                                      _Quad rhs, int flag);
short __kmpc_atomic_fixed2_mul_cpt_fp(ident_t *id_ref, int gtid, short *lhs,
                                      _Quad rhs, int flag);
short __kmpc_atomic_fixed2_div_cpt_fp(ident_t *id_ref, int gtid, short *lhs,
                                      _Quad rhs, int flag);
unsigned short __kmpc_atomic_fixed2u_add_cpt_fp(ident_t *id_ref, int gtid,
                                                unsigned short *lhs, _Quad rhs,
                                                int flag);
unsigned short __kmpc_atomic_fixed2u_sub_cpt_fp(ident_t *id_ref, int gtid,
                                                unsigned short *lhs, _Quad rhs,
                                                int flag);
unsigned short __kmpc_atomic_fixed2u_mul_cpt_fp(ident_t *id_ref, int gtid,
                                                unsigned short *lhs, _Quad rhs,
                                                int flag);
unsigned short __kmpc_atomic_fixed2u_div_cpt_fp(ident_t *id_ref, int gtid,
                                                unsigned short *lhs, _Quad rhs,
                                                int flag);

kmp_int32 __kmpc_atomic_fixed4_add_cpt_fp(ident_t *id_ref, int gtid,
                                          kmp_int32 *lhs, _Quad rhs, int flag);
kmp_int32 __kmpc_atomic_fixed4_sub_cpt_fp(ident_t *id_ref, int gtid,
                                          kmp_int32 *lhs, _Quad rhs, int flag);
kmp_int32 __kmpc_atomic_fixed4_mul_cpt_fp(ident_t *id_ref, int gtid,
                                          kmp_int32 *lhs, _Quad rhs, int flag);
kmp_int32 __kmpc_atomic_fixed4_div_cpt_fp(ident_t *id_ref, int gtid,
                                          kmp_int32 *lhs, _Quad rhs, int flag);
kmp_uint32 __kmpc_atomic_fixed4u_add_cpt_fp(ident_t *id_ref, int gtid,
                                            kmp_uint32 *lhs, _Quad rhs,
                                            int flag);
kmp_uint32 __kmpc_atomic_fixed4u_sub_cpt_fp(ident_t *id_ref, int gtid,
                                            kmp_uint32 *lhs, _Quad rhs,
                                            int flag);
kmp_uint32 __kmpc_atomic_fixed4u_mul_cpt_fp(ident_t *id_ref, int gtid,
                                            kmp_uint32 *lhs, _Quad rhs,
                                            int flag);
kmp_uint32 __kmpc_atomic_fixed4u_div_cpt_fp(ident_t *id_ref, int gtid,
                                            kmp_uint32 *lhs, _Quad rhs,
                                            int flag);

kmp_int64 __kmpc_atomic_fixed8_add_cpt_fp(ident_t *id_ref, int gtid,
                                          kmp_int64 *lhs, _Quad rhs, int flag);
kmp_int64 __kmpc_atomic_fixed8_sub_cpt_fp(ident_t *id_ref, int gtid,
                                          kmp_int64 *lhs, _Quad rhs, int flag);
kmp_int64 __kmpc_atomic_fixed8_mul_cpt_fp(ident_t *id_ref, int gtid,
                                          kmp_int64 *lhs, _Quad rhs, int flag);
kmp_int64 __kmpc_atomic_fixed8_div_cpt_fp(ident_t *id_ref, int gtid,
                                          kmp_int64 *lhs, _Quad rhs, int flag);
kmp_uint64 __kmpc_atomic_fixed8u_add_cpt_fp(ident_t *id_ref, int gtid,
                                            kmp_uint64 *lhs, _Quad rhs,
                                            int flag);
kmp_uint64 __kmpc_atomic_fixed8u_sub_cpt_fp(ident_t *id_ref, int gtid,
                                            kmp_uint64 *lhs, _Quad rhs,
                                            int flag);
kmp_uint64 __kmpc_atomic_fixed8u_mul_cpt_fp(ident_t *id_ref, int gtid,
                                            kmp_uint64 *lhs, _Quad rhs,
                                            int flag);
kmp_uint64 __kmpc_atomic_fixed8u_div_cpt_fp(ident_t *id_ref, int gtid,
                                            kmp_uint64 *lhs, _Quad rhs,
                                            int flag);

float __kmpc_atomic_float4_add_cpt_fp(ident_t *id_ref, int gtid,
                                      kmp_real32 *lhs, _Quad rhs, int flag);
float __kmpc_atomic_float4_sub_cpt_fp(ident_t *id_ref, int gtid,
                                      kmp_real32 *lhs, _Quad rhs, int flag);
float __kmpc_atomic_float4_mul_cpt_fp(ident_t *id_ref, int gtid,
                                      kmp_real32 *lhs, _Quad rhs, int flag);
float __kmpc_atomic_float4_div_cpt_fp(ident_t *id_ref, int gtid,
                                      kmp_real32 *lhs, _Quad rhs, int flag);

double __kmpc_atomic_float8_add_cpt_fp(ident_t *id_ref, int gtid,
                                       kmp_real64 *lhs, _Quad rhs, int flag);
double __kmpc_atomic_float8_sub_cpt_fp(ident_t *id_ref, int gtid,
                                       kmp_real64 *lhs, _Quad rhs, int flag);
double __kmpc_atomic_float8_mul_cpt_fp(ident_t *id_ref, int gtid,
                                       kmp_real64 *lhs, _Quad rhs, int flag);
double __kmpc_atomic_float8_div_cpt_fp(ident_t *id_ref, int gtid,
                                       kmp_real64 *lhs, _Quad rhs, int flag);

long double __kmpc_atomic_float10_add_cpt_fp(ident_t *id_ref, int gtid,
                                             long double *lhs, _Quad rhs,
                                             int flag);
long double __kmpc_atomic_float10_sub_cpt_fp(ident_t *id_ref, int gtid,
                                             long double *lhs, _Quad rhs,
                                             int flag);
long double __kmpc_atomic_float10_mul_cpt_fp(ident_t *id_ref, int gtid,
                                             long double *lhs, _Quad rhs,
                                             int flag);
long double __kmpc_atomic_float10_div_cpt_fp(ident_t *id_ref, int gtid,
                                             long double *lhs, _Quad rhs,
                                             int flag);

char __kmpc_atomic_fixed1_sub_cpt_rev_fp(ident_t *id_ref, int gtid, char *lhs,
                                         _Quad rhs, int flag);
unsigned char __kmpc_atomic_fixed1u_sub_cpt_rev_fp(ident_t *id_ref, int gtid,
                                                   unsigned char *lhs,
                                                   _Quad rhs, int flag);
char __kmpc_atomic_fixed1_div_cpt_rev_fp(ident_t *id_ref, int gtid, char *lhs,
                                         _Quad rhs, int flag);
unsigned char __kmpc_atomic_fixed1u_div_cpt_rev_fp(ident_t *id_ref, int gtid,
                                                   unsigned char *lhs,
                                                   _Quad rhs, int flag);
short __kmpc_atomic_fixed2_sub_cpt_rev_fp(ident_t *id_ref, int gtid, short *lhs,
                                          _Quad rhs, int flag);
unsigned short __kmpc_atomic_fixed2u_sub_cpt_rev_fp(ident_t *id_ref, int gtid,
                                                    unsigned short *lhs,
                                                    _Quad rhs, int flag);
short __kmpc_atomic_fixed2_div_cpt_rev_fp(ident_t *id_ref, int gtid, short *lhs,
                                          _Quad rhs, int flag);
unsigned short __kmpc_atomic_fixed2u_div_cpt_rev_fp(ident_t *id_ref, int gtid,
                                                    unsigned short *lhs,
                                                    _Quad rhs, int flag);
kmp_int32 __kmpc_atomic_fixed4_sub_cpt_rev_fp(ident_t *id_ref, int gtid,
                                              kmp_int32 *lhs, _Quad rhs,
                                              int flag);
kmp_uint32 __kmpc_atomic_fixed4u_sub_cpt_rev_fp(ident_t *id_ref, int gtid,
                                                kmp_uint32 *lhs, _Quad rhs,
                                                int flag);
kmp_int32 __kmpc_atomic_fixed4_div_cpt_rev_fp(ident_t *id_ref, int gtid,
                                              kmp_int32 *lhs, _Quad rhs,
                                              int flag);
kmp_uint32 __kmpc_atomic_fixed4u_div_cpt_rev_fp(ident_t *id_ref, int gtid,
                                                kmp_uint32 *lhs, _Quad rhs,
                                                int flag);
kmp_int64 __kmpc_atomic_fixed8_sub_cpt_rev_fp(ident_t *id_ref, int gtid,
                                              kmp_int64 *lhs, _Quad rhs,
                                              int flag);
kmp_uint64 __kmpc_atomic_fixed8u_sub_cpt_rev_fp(ident_t *id_ref, int gtid,
                                                kmp_uint64 *lhs, _Quad rhs,
                                                int flag);
kmp_int64 __kmpc_atomic_fixed8_div_cpt_rev_fp(ident_t *id_ref, int gtid,
                                              kmp_int64 *lhs, _Quad rhs,
                                              int flag);
kmp_uint64 __kmpc_atomic_fixed8u_div_cpt_rev_fp(ident_t *id_ref, int gtid,
                                                kmp_uint64 *lhs, _Quad rhs,
                                                int flag);
float __kmpc_atomic_float4_sub_cpt_rev_fp(ident_t *id_ref, int gtid, float *lhs,
                                          _Quad rhs, int flag);
float __kmpc_atomic_float4_div_cpt_rev_fp(ident_t *id_ref, int gtid, float *lhs,
                                          _Quad rhs, int flag);
double __kmpc_atomic_float8_sub_cpt_rev_fp(ident_t *id_ref, int gtid,
                                           double *lhs, _Quad rhs, int flag);
double __kmpc_atomic_float8_div_cpt_rev_fp(ident_t *id_ref, int gtid,
                                           double *lhs, _Quad rhs, int flag);
long double __kmpc_atomic_float10_sub_cpt_rev_fp(ident_t *id_ref, int gtid,
                                                 long double *lhs, _Quad rhs,
                                                 int flag);
long double __kmpc_atomic_float10_div_cpt_rev_fp(ident_t *id_ref, int gtid,
                                                 long double *lhs, _Quad rhs,
                                                 int flag);

#endif // KMP_HAVE_QUAD

// End of OpenMP 4.0 capture

#endif // OMP_40_ENABLED

#endif // KMP_ARCH_X86 || KMP_ARCH_X86_64

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KMP_ATOMIC_H */

// end of file
