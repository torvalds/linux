// TR1 math.h -*- C++ -*-

// Copyright (C) 2006 Free Software Foundation, Inc.
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

/** @file tr1/math.h
 *  This is a TR1 C++ Library header. 
 */

#ifndef _TR1_MATH_H
#define _TR1_MATH_H 1

#include <tr1/cmath>

#if _GLIBCXX_USE_C99_MATH_TR1

using std::tr1::acos;
using std::tr1::acosh;
using std::tr1::asin;
using std::tr1::asinh;
using std::tr1::atan;
using std::tr1::atan2;
using std::tr1::atanh;
using std::tr1::cbrt;
using std::tr1::ceil;
using std::tr1::copysign;
using std::tr1::cos;
using std::tr1::cosh;
using std::tr1::erf;
using std::tr1::erfc;
using std::tr1::exp;
using std::tr1::exp2;
using std::tr1::expm1;
using std::tr1::fabs;
using std::tr1::fdim;
using std::tr1::floor;
using std::tr1::fma;
using std::tr1::fmax;
using std::tr1::fmin;
using std::tr1::fmod;
using std::tr1::frexp;
using std::tr1::hypot;
using std::tr1::ilogb;
using std::tr1::ldexp;
using std::tr1::lgamma;
using std::tr1::llrint;
using std::tr1::llround;
using std::tr1::log;
using std::tr1::log10;
using std::tr1::log1p;
using std::tr1::log2;
using std::tr1::logb;
using std::tr1::lrint;
using std::tr1::lround;
using std::tr1::nearbyint;
using std::tr1::nextafter;
using std::tr1::nexttoward;
using std::tr1::pow;
using std::tr1::remainder;
using std::tr1::remquo;
using std::tr1::rint;
using std::tr1::round;
using std::tr1::scalbln;
using std::tr1::scalbn;
using std::tr1::sin;
using std::tr1::sinh;
using std::tr1::sqrt;
using std::tr1::tan;
using std::tr1::tanh;
using std::tr1::tgamma;
using std::tr1::trunc;

#endif

#endif
