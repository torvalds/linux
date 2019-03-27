/* ===-- int_lib.h - configuration header for compiler-rt  -----------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file is a configuration header for compiler-rt.
 * This file is not part of the interface of this library.
 *
 * ===----------------------------------------------------------------------===
 */

#ifndef INT_LIB_H
#define INT_LIB_H

/* Assumption: Signed integral is 2's complement. */
/* Assumption: Right shift of signed negative is arithmetic shift. */
/* Assumption: Endianness is little or big (not mixed). */

#if defined(__ELF__)
#define FNALIAS(alias_name, original_name) \
  void alias_name() __attribute__((__alias__(#original_name)))
#define COMPILER_RT_ALIAS(aliasee) __attribute__((__alias__(#aliasee)))
#else
#define FNALIAS(alias, name) _Pragma("GCC error(\"alias unsupported on this file format\")")
#define COMPILER_RT_ALIAS(aliasee) _Pragma("GCC error(\"alias unsupported on this file format\")")
#endif

/* ABI macro definitions */

#if __ARM_EABI__
# if defined(COMPILER_RT_ARMHF_TARGET) || (!defined(__clang__) && \
     defined(__GNUC__) && (__GNUC__ < 4 || __GNUC__ == 4 && __GNUC_MINOR__ < 5))
/* The pcs attribute was introduced in GCC 4.5.0 */
#   define COMPILER_RT_ABI
# else
#   define COMPILER_RT_ABI __attribute__((__pcs__("aapcs")))
# endif
#else
# define COMPILER_RT_ABI
#endif

#define AEABI_RTABI __attribute__((__pcs__("aapcs")))

#if defined(_MSC_VER) && !defined(__clang__)
#define ALWAYS_INLINE __forceinline
#define NOINLINE __declspec(noinline)
#define NORETURN __declspec(noreturn)
#define UNUSED
#else
#define ALWAYS_INLINE __attribute__((always_inline))
#define NOINLINE __attribute__((noinline))
#define NORETURN __attribute__((noreturn))
#define UNUSED __attribute__((unused))
#endif

#if (defined(__FreeBSD__) || defined(__NetBSD__)) && (defined(_KERNEL) || defined(_STANDALONE))
/*
 * Kernel and boot environment can't use normal headers,
 * so use the equivalent system headers.
 */
#ifdef __FreeBSD__
#  include <sys/limits.h>
#else
#  include <machine/limits.h>
#endif
#  include <sys/stdint.h>
#  include <sys/types.h>
#else
/* Include the standard compiler builtin headers we use functionality from. */
#  include <limits.h>
#  include <stdint.h>
#  include <stdbool.h>
#  include <float.h>
#endif

/* Include the commonly used internal type definitions. */
#include "int_types.h"

/* Include internal utility function declarations. */
#include "int_util.h"

/*
 * Workaround for LLVM bug 11663.  Prevent endless recursion in
 * __c?zdi2(), where calls to __builtin_c?z() are expanded to
 * __c?zdi2() instead of __c?zsi2().
 *
 * Instead of placing this workaround in c?zdi2.c, put it in this
 * global header to prevent other C files from making the detour
 * through __c?zdi2() as well.
 *
 * This problem has been observed on FreeBSD for sparc64 and
 * mips64 with GCC 4.2.1, and for riscv with GCC 5.2.0.
 * Presumably it's any version of GCC, and targeting an arch that
 * does not have dedicated bit counting instructions.
 */
#if defined(__FreeBSD__) && (defined(__sparc64__) || \
    defined(__mips_n32) || defined(__mips_n64) || defined(__mips_o64) || \
    defined(__riscv))
si_int __clzsi2(si_int);
si_int __ctzsi2(si_int);
#define	__builtin_clz __clzsi2
#define	__builtin_ctz __ctzsi2
#endif /* FreeBSD && (sparc64 || mips_n32 || mips_n64 || mips_o64 || riscv) */

COMPILER_RT_ABI si_int __paritysi2(si_int a);
COMPILER_RT_ABI si_int __paritydi2(di_int a);

COMPILER_RT_ABI di_int __divdi3(di_int a, di_int b);
COMPILER_RT_ABI si_int __divsi3(si_int a, si_int b);
COMPILER_RT_ABI su_int __udivsi3(su_int n, su_int d);

COMPILER_RT_ABI su_int __udivmodsi4(su_int a, su_int b, su_int* rem);
COMPILER_RT_ABI du_int __udivmoddi4(du_int a, du_int b, du_int* rem);
#ifdef CRT_HAS_128BIT
COMPILER_RT_ABI si_int __clzti2(ti_int a);
COMPILER_RT_ABI tu_int __udivmodti4(tu_int a, tu_int b, tu_int* rem);
#endif

/* Definitions for builtins unavailable on MSVC */
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>

uint32_t __inline __builtin_ctz(uint32_t value) {
  unsigned long trailing_zero = 0;
  if (_BitScanForward(&trailing_zero, value))
    return trailing_zero;
  return 32;
}

uint32_t __inline __builtin_clz(uint32_t value) {
  unsigned long leading_zero = 0;
  if (_BitScanReverse(&leading_zero, value))
    return 31 - leading_zero;
  return 32;
}

#if defined(_M_ARM) || defined(_M_X64)
uint32_t __inline __builtin_clzll(uint64_t value) {
  unsigned long leading_zero = 0;
  if (_BitScanReverse64(&leading_zero, value))
    return 63 - leading_zero;
  return 64;
}
#else
uint32_t __inline __builtin_clzll(uint64_t value) {
  if (value == 0)
    return 64;
  uint32_t msh = (uint32_t)(value >> 32);
  uint32_t lsh = (uint32_t)(value & 0xFFFFFFFF);
  if (msh != 0)
    return __builtin_clz(msh);
  return 32 + __builtin_clz(lsh);
}
#endif

#define __builtin_clzl __builtin_clzll
#endif /* defined(_MSC_VER) && !defined(__clang__) */

#endif /* INT_LIB_H */
