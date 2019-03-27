// -*- C++ -*-
//===---------------------------- locale.h --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_LOCALE_H
#define _LIBCPP_LOCALE_H

/*
    locale.h synopsis

Macros:

    LC_ALL
    LC_COLLATE
    LC_CTYPE
    LC_MONETARY
    LC_NUMERIC
    LC_TIME

Types:

    lconv

Functions:

   setlocale
   localeconv

*/

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#pragma GCC system_header
#endif

#include_next <locale.h>

#endif  // _LIBCPP_LOCALE_H
