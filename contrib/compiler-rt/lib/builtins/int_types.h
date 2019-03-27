/* ===-- int_lib.h - configuration header for compiler-rt  -----------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file is not part of the interface of this library.
 *
 * This file defines various standard types, most importantly a number of unions
 * used to access parts of larger types.
 *
 * ===----------------------------------------------------------------------===
 */

#ifndef INT_TYPES_H
#define INT_TYPES_H

#include "int_endianness.h"

/* si_int is defined in Linux sysroot's asm-generic/siginfo.h */
#ifdef si_int
#undef si_int
#endif
typedef      int si_int;
typedef unsigned su_int;

typedef          long long di_int;
typedef unsigned long long du_int;

typedef union
{
    di_int all;
    struct
    {
#if _YUGA_LITTLE_ENDIAN
        su_int low;
        si_int high;
#else
        si_int high;
        su_int low;
#endif /* _YUGA_LITTLE_ENDIAN */
    }s;
} dwords;

typedef union
{
    du_int all;
    struct
    {
#if _YUGA_LITTLE_ENDIAN
        su_int low;
        su_int high;
#else
        su_int high;
        su_int low;
#endif /* _YUGA_LITTLE_ENDIAN */
    }s;
} udwords;

#if defined(__LP64__) || defined(__wasm__) || defined(__mips64) ||             \
    defined(__riscv) || defined(_WIN64)
#define CRT_HAS_128BIT
#endif

/* MSVC doesn't have a working 128bit integer type. Users should really compile
 * compiler-rt with clang, but if they happen to be doing a standalone build for
 * asan or something else, disable the 128 bit parts so things sort of work.
 */
#if defined(_MSC_VER) && !defined(__clang__)
#undef CRT_HAS_128BIT
#endif

#ifdef CRT_HAS_128BIT
typedef int      ti_int __attribute__ ((mode (TI)));
typedef unsigned tu_int __attribute__ ((mode (TI)));

typedef union
{
    ti_int all;
    struct
    {
#if _YUGA_LITTLE_ENDIAN
        du_int low;
        di_int high;
#else
        di_int high;
        du_int low;
#endif /* _YUGA_LITTLE_ENDIAN */
    }s;
} twords;

typedef union
{
    tu_int all;
    struct
    {
#if _YUGA_LITTLE_ENDIAN
        du_int low;
        du_int high;
#else
        du_int high;
        du_int low;
#endif /* _YUGA_LITTLE_ENDIAN */
    }s;
} utwords;

static __inline ti_int make_ti(di_int h, di_int l) {
    twords r;
    r.s.high = h;
    r.s.low = l;
    return r.all;
}

static __inline tu_int make_tu(du_int h, du_int l) {
    utwords r;
    r.s.high = h;
    r.s.low = l;
    return r.all;
}

#endif /* CRT_HAS_128BIT */

#ifndef _STANDALONE
typedef union
{
    su_int u;
    float f;
} float_bits;

typedef union
{
    udwords u;
    double  f;
} double_bits;
#endif

typedef struct
{
#if _YUGA_LITTLE_ENDIAN
    udwords low;
    udwords high;
#else
    udwords high;
    udwords low;
#endif /* _YUGA_LITTLE_ENDIAN */
} uqwords;

/* Check if the target supports 80 bit extended precision long doubles.
 * Notably, on x86 Windows, MSVC only provides a 64-bit long double, but GCC
 * still makes it 80 bits. Clang will match whatever compiler it is trying to
 * be compatible with.
 */
#if ((defined(__i386__) || defined(__x86_64__)) && !defined(_MSC_VER)) || \
    defined(__m68k__) || defined(__ia64__)
#define HAS_80_BIT_LONG_DOUBLE 1
#else
#define HAS_80_BIT_LONG_DOUBLE 0
#endif

#ifndef _STANDALONE
typedef union
{
    uqwords     u;
    long double f;
} long_double_bits;

#if __STDC_VERSION__ >= 199901L
typedef float _Complex Fcomplex;
typedef double _Complex Dcomplex;
typedef long double _Complex Lcomplex;

#define COMPLEX_REAL(x) __real__(x)
#define COMPLEX_IMAGINARY(x) __imag__(x)
#else
typedef struct { float real, imaginary; } Fcomplex;

typedef struct { double real, imaginary; } Dcomplex;

typedef struct { long double real, imaginary; } Lcomplex;

#define COMPLEX_REAL(x) (x).real
#define COMPLEX_IMAGINARY(x) (x).imaginary
#endif
#endif
#endif /* INT_TYPES_H */

