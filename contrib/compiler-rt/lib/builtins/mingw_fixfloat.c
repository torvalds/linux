/* ===-- mingw_fixfloat.c - Wrap int/float conversions for arm/windows -----===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

COMPILER_RT_ABI di_int __fixdfdi(double a);
COMPILER_RT_ABI di_int __fixsfdi(float a);
COMPILER_RT_ABI du_int __fixunsdfdi(double a);
COMPILER_RT_ABI du_int __fixunssfdi(float a);
COMPILER_RT_ABI double __floatdidf(di_int a);
COMPILER_RT_ABI float __floatdisf(di_int a);
COMPILER_RT_ABI double __floatundidf(du_int a);
COMPILER_RT_ABI float __floatundisf(du_int a);

COMPILER_RT_ABI di_int __dtoi64(double a) { return __fixdfdi(a); }

COMPILER_RT_ABI di_int __stoi64(float a) { return __fixsfdi(a); }

COMPILER_RT_ABI du_int __dtou64(double a) { return __fixunsdfdi(a); }

COMPILER_RT_ABI du_int __stou64(float a) { return __fixunssfdi(a); }

COMPILER_RT_ABI double __i64tod(di_int a) { return __floatdidf(a); }

COMPILER_RT_ABI float __i64tos(di_int a) { return __floatdisf(a); }

COMPILER_RT_ABI double __u64tod(du_int a) { return __floatundidf(a); }

COMPILER_RT_ABI float __u64tos(du_int a) { return __floatundisf(a); }
