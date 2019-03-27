// Concept-checking control -*- C++ -*-

// Copyright (C) 2001 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

/** @file concept_check.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _CONCEPT_CHECK_H
#define _CONCEPT_CHECK_H 1

#pragma GCC system_header

#include <bits/c++config.h>

// All places in libstdc++-v3 where these are used, or /might/ be used, or
// don't need to be used, or perhaps /should/ be used, are commented with
// "concept requirements" (and maybe some more text).  So grep like crazy
// if you're looking for additional places to use these.

// Concept-checking code is off by default unless users turn it on via
// configure options or editing c++config.h.

#ifndef _GLIBCXX_CONCEPT_CHECKS

#define __glibcxx_function_requires(...)
#define __glibcxx_class_requires(_a,_b)
#define __glibcxx_class_requires2(_a,_b,_c)
#define __glibcxx_class_requires3(_a,_b,_c,_d)
#define __glibcxx_class_requires4(_a,_b,_c,_d,_e)

#else // the checks are on

#include <bits/boost_concept_check.h>

// Note that the obvious and elegant approach of
//
//#define glibcxx_function_requires(C) boost::function_requires< boost::C >()
//
// won't work due to concept templates with more than one parameter, e.g.,
// BinaryPredicateConcept.  The preprocessor tries to split things up on
// the commas in the template argument list.  We can't use an inner pair of
// parenthesis to hide the commas, because "boost::(Temp<Foo,Bar>)" isn't
// a valid instantiation pattern.  Thus, we steal a feature from C99.

#define __glibcxx_function_requires(...)                                 \
            __gnu_cxx::__function_requires< __gnu_cxx::__VA_ARGS__ >();
#define __glibcxx_class_requires(_a,_C)                                  \
            _GLIBCXX_CLASS_REQUIRES(_a, __gnu_cxx, _C);
#define __glibcxx_class_requires2(_a,_b,_C)                              \
            _GLIBCXX_CLASS_REQUIRES2(_a, _b, __gnu_cxx, _C);
#define __glibcxx_class_requires3(_a,_b,_c,_C)                           \
            _GLIBCXX_CLASS_REQUIRES3(_a, _b, _c, __gnu_cxx, _C);
#define __glibcxx_class_requires4(_a,_b,_c,_d,_C)                        \
            _GLIBCXX_CLASS_REQUIRES4(_a, _b, _c, _d, __gnu_cxx, _C);

#endif // enable/disable

#endif // _GLIBCXX_CONCEPT_CHECK
