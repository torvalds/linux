// -*- C++ -*- forwarding header.

// Copyright (C) 2000, 2002, 2003 Free Software Foundation, Inc.
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

//
// ISO C++ 14882: 26.5  C library
//

#ifndef _GLIBCXX_CMATH
#define _GLIBCXX_CMATH 1

#pragma GCC system_header

#include <bits/c++config.h>

#include_next <math.h>

// Get rid of those macros defined in <math.h> in lieu of real functions.
#undef abs
#undef div
#undef acos
#undef asin
#undef atan
#undef atan2
#undef ceil
#undef cos
#undef cosh
#undef exp
#undef fabs
#undef floor
#undef fmod
#undef frexp
#undef ldexp
#undef log
#undef log10
#undef modf
#undef pow
#undef sin
#undef sinh
#undef sqrt
#undef tan
#undef tanh

#undef fpclassify
#undef isfinite
#undef isinf
#undef isnan
#undef isnormal
#undef signbit
#undef isgreater
#undef isgreaterequal
#undef isless
#undef islessequal
#undef islessgreater
#undef isunordered

namespace std
{
  inline double
  abs(double __x)
  { return __builtin_fabs(__x); }

  inline float
  abs(float __x)
  { return __builtin_fabsf(__x); }

  inline long double
  abs(long double __x)
  { return __builtin_fabsl(__x); }

#if _GLIBCXX_HAVE_MODFF
  inline float
  modf(float __x, float* __iptr) { return modff(__x, __iptr); }
#else
  inline float
  modf(float __x, float* __iptr)
  {
    double __tmp;
    double __res = modf(static_cast<double>(__x), &__tmp);
    *__iptr = static_cast<float>(__tmp);
    return __res;
  }
#endif

#if _GLIBCXX_HAVE_MODFL
  inline long double
  modf(long double __x, long double* __iptr) { return modfl(__x, __iptr); }
#else
  inline long double
  modf(long double __x, long double* __iptr)
  {
    double __tmp;
    double __res = modf(static_cast<double>(__x), &__tmp);
    * __iptr = static_cast<long double>(__tmp);
    return __res;
  }
#endif
}
#endif
