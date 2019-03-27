// -*- C++ -*-
//===--------------------------- float.h ----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_FLOAT_H
#define _LIBCPP_FLOAT_H

/*
    float.h synopsis

Macros:

    FLT_ROUNDS
    FLT_EVAL_METHOD     // C99
    FLT_RADIX

    FLT_MANT_DIG
    DBL_MANT_DIG
    LDBL_MANT_DIG

    FLT_HAS_SUBNORM     // C11
    DBL_HAS_SUBNORM     // C11
    LDBL_HAS_SUBNORM    // C11

    DECIMAL_DIG         // C99
    FLT_DECIMAL_DIG     // C11
    DBL_DECIMAL_DIG     // C11
    LDBL_DECIMAL_DIG    // C11

    FLT_DIG
    DBL_DIG
    LDBL_DIG

    FLT_MIN_EXP
    DBL_MIN_EXP
    LDBL_MIN_EXP

    FLT_MIN_10_EXP
    DBL_MIN_10_EXP
    LDBL_MIN_10_EXP

    FLT_MAX_EXP
    DBL_MAX_EXP
    LDBL_MAX_EXP

    FLT_MAX_10_EXP
    DBL_MAX_10_EXP
    LDBL_MAX_10_EXP

    FLT_MAX
    DBL_MAX
    LDBL_MAX

    FLT_EPSILON
    DBL_EPSILON
    LDBL_EPSILON

    FLT_MIN
    DBL_MIN
    LDBL_MIN

    FLT_TRUE_MIN        // C11
    DBL_TRUE_MIN        // C11
    LDBL_TRUE_MIN       // C11

*/

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#pragma GCC system_header
#endif

#include_next <float.h>

#ifdef __cplusplus

#ifndef FLT_EVAL_METHOD
#define FLT_EVAL_METHOD __FLT_EVAL_METHOD__
#endif

#ifndef DECIMAL_DIG
#define DECIMAL_DIG __DECIMAL_DIG__
#endif

#endif // __cplusplus

#endif  // _LIBCPP_FLOAT_H
