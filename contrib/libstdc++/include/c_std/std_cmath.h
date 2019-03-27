// -*- C++ -*- C forwarding header.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
// Free Software Foundation, Inc.
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

/** @file include/cmath
 *  This is a Standard C++ Library file.  You should @c #include this file
 *  in your programs, rather than any of the "*.h" implementation files.
 *
 *  This is the C++ version of the Standard C Library header @c math.h,
 *  and its contents are (mostly) the same as that header, but are all
 *  contained in the namespace @c std (except for names which are defined
 *  as macros in C).
 */

//
// ISO C++ 14882: 26.5  C library
//

#ifndef _GLIBCXX_CMATH
#define _GLIBCXX_CMATH 1

#pragma GCC system_header

#include <bits/c++config.h>
#include <bits/cpp_type_traits.h>
#include <ext/type_traits.h>

#include <math.h>

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

_GLIBCXX_BEGIN_NAMESPACE(std)

  // Forward declaration of a helper function.  This really should be
  // an `exported' forward declaration.
  template<typename _Tp> _Tp __cmath_power(_Tp, unsigned int);

  inline double
  abs(double __x)
  { return __builtin_fabs(__x); }

  inline float
  abs(float __x)
  { return __builtin_fabsf(__x); }

  inline long double
  abs(long double __x)
  { return __builtin_fabsl(__x); }

  using ::acos;

  inline float
  acos(float __x)
  { return __builtin_acosf(__x); }

  inline long double
  acos(long double __x)
  { return __builtin_acosl(__x); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
    acos(_Tp __x)
    { return __builtin_acos(__x); }

  using ::asin;

  inline float
  asin(float __x)
  { return __builtin_asinf(__x); }

  inline long double
  asin(long double __x)
  { return __builtin_asinl(__x); }

  template<typename _Tp>
  inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value,
					 double>::__type
    asin(_Tp __x)
    { return __builtin_asin(__x); }

  using ::atan;

  inline float
  atan(float __x)
  { return __builtin_atanf(__x); }

  inline long double
  atan(long double __x)
  { return __builtin_atanl(__x); }

  template<typename _Tp>
  inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					 double>::__type
    atan(_Tp __x)
    { return __builtin_atan(__x); }

  using ::atan2;

  inline float
  atan2(float __y, float __x)
  { return __builtin_atan2f(__y, __x); }

  inline long double
  atan2(long double __y, long double __x)
  { return __builtin_atan2l(__y, __x); }

  template<typename _Tp, typename _Up>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value
    					   && __is_integer<_Up>::__value, 
					   double>::__type
    atan2(_Tp __y, _Up __x)
    { return __builtin_atan2(__y, __x); }

  using ::ceil;

  inline float
  ceil(float __x)
  { return __builtin_ceilf(__x); }

  inline long double
  ceil(long double __x)
  { return __builtin_ceill(__x); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
    ceil(_Tp __x)
    { return __builtin_ceil(__x); }

  using ::cos;

  inline float
  cos(float __x)
  { return __builtin_cosf(__x); }

  inline long double
  cos(long double __x)
  { return __builtin_cosl(__x); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
    cos(_Tp __x)
    { return __builtin_cos(__x); }

  using ::cosh;

  inline float
  cosh(float __x)
  { return __builtin_coshf(__x); }

  inline long double
  cosh(long double __x)
  { return __builtin_coshl(__x); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
    cosh(_Tp __x)
    { return __builtin_cosh(__x); }

  using ::exp;

  inline float
  exp(float __x)
  { return __builtin_expf(__x); }

  inline long double
  exp(long double __x)
  { return __builtin_expl(__x); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
    exp(_Tp __x)
    { return __builtin_exp(__x); }

  using ::fabs;

  inline float
  fabs(float __x)
  { return __builtin_fabsf(__x); }

  inline long double
  fabs(long double __x)
  { return __builtin_fabsl(__x); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
    fabs(_Tp __x)
    { return __builtin_fabs(__x); }

  using ::floor;

  inline float
  floor(float __x)
  { return __builtin_floorf(__x); }

  inline long double
  floor(long double __x)
  { return __builtin_floorl(__x); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
    floor(_Tp __x)
    { return __builtin_floor(__x); }

  using ::fmod;

  inline float
  fmod(float __x, float __y)
  { return __builtin_fmodf(__x, __y); }

  inline long double
  fmod(long double __x, long double __y)
  { return __builtin_fmodl(__x, __y); }

  using ::frexp;

  inline float
  frexp(float __x, int* __exp)
  { return __builtin_frexpf(__x, __exp); }

  inline long double
  frexp(long double __x, int* __exp)
  { return __builtin_frexpl(__x, __exp); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
    frexp(_Tp __x, int* __exp)
    { return __builtin_frexp(__x, __exp); }

  using ::ldexp;

  inline float
  ldexp(float __x, int __exp)
  { return __builtin_ldexpf(__x, __exp); }

  inline long double
  ldexp(long double __x, int __exp)
  { return __builtin_ldexpl(__x, __exp); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
  ldexp(_Tp __x, int __exp)
  { return __builtin_ldexp(__x, __exp); }

  using ::log;

  inline float
  log(float __x)
  { return __builtin_logf(__x); }

  inline long double
  log(long double __x)
  { return __builtin_logl(__x); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
    log(_Tp __x)
    { return __builtin_log(__x); }

  using ::log10;

  inline float
  log10(float __x)
  { return __builtin_log10f(__x); }

  inline long double
  log10(long double __x)
  { return __builtin_log10l(__x); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
    log10(_Tp __x)
    { return __builtin_log10(__x); }

  using ::modf;

  inline float
  modf(float __x, float* __iptr)
  { return __builtin_modff(__x, __iptr); }

  inline long double
  modf(long double __x, long double* __iptr)
  { return __builtin_modfl(__x, __iptr); }

  template<typename _Tp>
    inline _Tp
    __pow_helper(_Tp __x, int __n)
    {
      return __n < 0
        ? _Tp(1)/__cmath_power(__x, -__n)
        : __cmath_power(__x, __n);
    }

  using ::pow;

  inline float
  pow(float __x, float __y)
  { return __builtin_powf(__x, __y); }

  inline long double
  pow(long double __x, long double __y)
  { return __builtin_powl(__x, __y); }

  inline double
  pow(double __x, int __i)
  { return __builtin_powi(__x, __i); }

  inline float
  pow(float __x, int __n)
  { return __builtin_powif(__x, __n); }

  inline long double
  pow(long double __x, int __n)
  { return __builtin_powil(__x, __n); }

  using ::sin;

  inline float
  sin(float __x)
  { return __builtin_sinf(__x); }

  inline long double
  sin(long double __x)
  { return __builtin_sinl(__x); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
    sin(_Tp __x)
    { return __builtin_sin(__x); }

  using ::sinh;

  inline float
  sinh(float __x)
  { return __builtin_sinhf(__x); }

  inline long double
  sinh(long double __x)
  { return __builtin_sinhl(__x); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
    sinh(_Tp __x)
    { return __builtin_sinh(__x); }

  using ::sqrt;

  inline float
  sqrt(float __x)
  { return __builtin_sqrtf(__x); }

  inline long double
  sqrt(long double __x)
  { return __builtin_sqrtl(__x); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
    sqrt(_Tp __x)
    { return __builtin_sqrt(__x); }

  using ::tan;

  inline float
  tan(float __x)
  { return __builtin_tanf(__x); }

  inline long double
  tan(long double __x)
  { return __builtin_tanl(__x); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
    tan(_Tp __x)
    { return __builtin_tan(__x); }

  using ::tanh;

  inline float
  tanh(float __x)
  { return __builtin_tanhf(__x); }

  inline long double
  tanh(long double __x)
  { return __builtin_tanhl(__x); }

  template<typename _Tp>
    inline typename __gnu_cxx::__enable_if<__is_integer<_Tp>::__value, 
					   double>::__type
    tanh(_Tp __x)
    { return __builtin_tanh(__x); }

_GLIBCXX_END_NAMESPACE

#if _GLIBCXX_USE_C99_MATH
#if !_GLIBCXX_USE_C99_FP_MACROS_DYNAMIC
// These are possible macros imported from C99-land. For strict
// conformance, remove possible C99-injected names from the global
// namespace, and sequester them in the __gnu_cxx extension namespace.

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  template<typename _Tp>
    inline int
    __capture_fpclassify(_Tp __f) { return fpclassify(__f); }

  template<typename _Tp>
    inline int
    __capture_isfinite(_Tp __f) { return isfinite(__f); }

  template<typename _Tp>
    inline int
    __capture_isinf(_Tp __f) { return isinf(__f); }

  template<typename _Tp>
    inline int
    __capture_isnan(_Tp __f) { return isnan(__f); }

  template<typename _Tp>
    inline int
    __capture_isnormal(_Tp __f) { return isnormal(__f); }

  template<typename _Tp>
    inline int
    __capture_signbit(_Tp __f) { return signbit(__f); }

  template<typename _Tp>
    inline int
    __capture_isgreater(_Tp __f1, _Tp __f2)
    { return isgreater(__f1, __f2); }

  template<typename _Tp>
    inline int
    __capture_isgreaterequal(_Tp __f1, _Tp __f2)
    { return isgreaterequal(__f1, __f2); }

  template<typename _Tp>
    inline int
    __capture_isless(_Tp __f1, _Tp __f2) { return isless(__f1, __f2); }

  template<typename _Tp>
    inline int
    __capture_islessequal(_Tp __f1, _Tp __f2)
    { return islessequal(__f1, __f2); }

  template<typename _Tp>
    inline int
    __capture_islessgreater(_Tp __f1, _Tp __f2)
    { return islessgreater(__f1, __f2); }

  template<typename _Tp>
    inline int
    __capture_isunordered(_Tp __f1, _Tp __f2)
    { return isunordered(__f1, __f2); }

_GLIBCXX_END_NAMESPACE

// Only undefine the C99 FP macros, if actually captured for namespace movement
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

_GLIBCXX_BEGIN_NAMESPACE(std)

  template<typename _Tp>
    inline int
    fpclassify(_Tp __f) { return ::__gnu_cxx::__capture_fpclassify(__f); }

  template<typename _Tp>
    inline int
    isfinite(_Tp __f) { return ::__gnu_cxx::__capture_isfinite(__f); }

  template<typename _Tp>
    inline int
    isinf(_Tp __f) { return ::__gnu_cxx::__capture_isinf(__f); }

  template<typename _Tp>
    inline int
    isnan(_Tp __f) { return ::__gnu_cxx::__capture_isnan(__f); }

  template<typename _Tp>
    inline int
    isnormal(_Tp __f) { return ::__gnu_cxx::__capture_isnormal(__f); }

  template<typename _Tp>
    inline int
    signbit(_Tp __f) { return ::__gnu_cxx::__capture_signbit(__f); }

  template<typename _Tp>
    inline int
    isgreater(_Tp __f1, _Tp __f2)
    { return ::__gnu_cxx::__capture_isgreater(__f1, __f2); }

  template<typename _Tp>
    inline int
    isgreaterequal(_Tp __f1, _Tp __f2)
    { return ::__gnu_cxx::__capture_isgreaterequal(__f1, __f2); }

  template<typename _Tp>
    inline int
    isless(_Tp __f1, _Tp __f2)
    { return ::__gnu_cxx::__capture_isless(__f1, __f2); }

  template<typename _Tp>
    inline int
    islessequal(_Tp __f1, _Tp __f2)
    { return ::__gnu_cxx::__capture_islessequal(__f1, __f2); }

  template<typename _Tp>
    inline int
    islessgreater(_Tp __f1, _Tp __f2)
    { return ::__gnu_cxx::__capture_islessgreater(__f1, __f2); }

  template<typename _Tp>
    inline int
    isunordered(_Tp __f1, _Tp __f2)
    { return ::__gnu_cxx::__capture_isunordered(__f1, __f2); }

_GLIBCXX_END_NAMESPACE
using std::isnan;
using std::isinf;

#endif /* _GLIBCXX_USE_C99_FP_MACROS_DYNAMIC */
#endif

#ifndef _GLIBCXX_EXPORT_TEMPLATE
# include <bits/cmath.tcc>
#endif

#endif
