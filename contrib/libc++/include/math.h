// -*- C++ -*-
//===---------------------------- math.h ----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_MATH_H
#define _LIBCPP_MATH_H

/*
    math.h synopsis

Macros:

    HUGE_VAL
    HUGE_VALF               // C99
    HUGE_VALL               // C99
    INFINITY                // C99
    NAN                     // C99
    FP_INFINITE             // C99
    FP_NAN                  // C99
    FP_NORMAL               // C99
    FP_SUBNORMAL            // C99
    FP_ZERO                 // C99
    FP_FAST_FMA             // C99
    FP_FAST_FMAF            // C99
    FP_FAST_FMAL            // C99
    FP_ILOGB0               // C99
    FP_ILOGBNAN             // C99
    MATH_ERRNO              // C99
    MATH_ERREXCEPT          // C99
    math_errhandling        // C99

Types:

    float_t                 // C99
    double_t                // C99

// C90

floating_point abs(floating_point x);

floating_point acos (arithmetic x);
float          acosf(float x);
long double    acosl(long double x);

floating_point asin (arithmetic x);
float          asinf(float x);
long double    asinl(long double x);

floating_point atan (arithmetic x);
float          atanf(float x);
long double    atanl(long double x);

floating_point atan2 (arithmetic y, arithmetic x);
float          atan2f(float y, float x);
long double    atan2l(long double y, long double x);

floating_point ceil (arithmetic x);
float          ceilf(float x);
long double    ceill(long double x);

floating_point cos (arithmetic x);
float          cosf(float x);
long double    cosl(long double x);

floating_point cosh (arithmetic x);
float          coshf(float x);
long double    coshl(long double x);

floating_point exp (arithmetic x);
float          expf(float x);
long double    expl(long double x);

floating_point fabs (arithmetic x);
float          fabsf(float x);
long double    fabsl(long double x);

floating_point floor (arithmetic x);
float          floorf(float x);
long double    floorl(long double x);

floating_point fmod (arithmetic x, arithmetic y);
float          fmodf(float x, float y);
long double    fmodl(long double x, long double y);

floating_point frexp (arithmetic value, int* exp);
float          frexpf(float value, int* exp);
long double    frexpl(long double value, int* exp);

floating_point ldexp (arithmetic value, int exp);
float          ldexpf(float value, int exp);
long double    ldexpl(long double value, int exp);

floating_point log (arithmetic x);
float          logf(float x);
long double    logl(long double x);

floating_point log10 (arithmetic x);
float          log10f(float x);
long double    log10l(long double x);

floating_point modf (floating_point value, floating_point* iptr);
float          modff(float value, float* iptr);
long double    modfl(long double value, long double* iptr);

floating_point pow (arithmetic x, arithmetic y);
float          powf(float x, float y);
long double    powl(long double x, long double y);

floating_point sin (arithmetic x);
float          sinf(float x);
long double    sinl(long double x);

floating_point sinh (arithmetic x);
float          sinhf(float x);
long double    sinhl(long double x);

floating_point sqrt (arithmetic x);
float          sqrtf(float x);
long double    sqrtl(long double x);

floating_point tan (arithmetic x);
float          tanf(float x);
long double    tanl(long double x);

floating_point tanh (arithmetic x);
float          tanhf(float x);
long double    tanhl(long double x);

//  C99

bool signbit(arithmetic x);

int fpclassify(arithmetic x);

bool isfinite(arithmetic x);
bool isinf(arithmetic x);
bool isnan(arithmetic x);
bool isnormal(arithmetic x);

bool isgreater(arithmetic x, arithmetic y);
bool isgreaterequal(arithmetic x, arithmetic y);
bool isless(arithmetic x, arithmetic y);
bool islessequal(arithmetic x, arithmetic y);
bool islessgreater(arithmetic x, arithmetic y);
bool isunordered(arithmetic x, arithmetic y);

floating_point acosh (arithmetic x);
float          acoshf(float x);
long double    acoshl(long double x);

floating_point asinh (arithmetic x);
float          asinhf(float x);
long double    asinhl(long double x);

floating_point atanh (arithmetic x);
float          atanhf(float x);
long double    atanhl(long double x);

floating_point cbrt (arithmetic x);
float          cbrtf(float x);
long double    cbrtl(long double x);

floating_point copysign (arithmetic x, arithmetic y);
float          copysignf(float x, float y);
long double    copysignl(long double x, long double y);

floating_point erf (arithmetic x);
float          erff(float x);
long double    erfl(long double x);

floating_point erfc (arithmetic x);
float          erfcf(float x);
long double    erfcl(long double x);

floating_point exp2 (arithmetic x);
float          exp2f(float x);
long double    exp2l(long double x);

floating_point expm1 (arithmetic x);
float          expm1f(float x);
long double    expm1l(long double x);

floating_point fdim (arithmetic x, arithmetic y);
float          fdimf(float x, float y);
long double    fdiml(long double x, long double y);

floating_point fma (arithmetic x, arithmetic y, arithmetic z);
float          fmaf(float x, float y, float z);
long double    fmal(long double x, long double y, long double z);

floating_point fmax (arithmetic x, arithmetic y);
float          fmaxf(float x, float y);
long double    fmaxl(long double x, long double y);

floating_point fmin (arithmetic x, arithmetic y);
float          fminf(float x, float y);
long double    fminl(long double x, long double y);

floating_point hypot (arithmetic x, arithmetic y);
float          hypotf(float x, float y);
long double    hypotl(long double x, long double y);

int ilogb (arithmetic x);
int ilogbf(float x);
int ilogbl(long double x);

floating_point lgamma (arithmetic x);
float          lgammaf(float x);
long double    lgammal(long double x);

long long llrint (arithmetic x);
long long llrintf(float x);
long long llrintl(long double x);

long long llround (arithmetic x);
long long llroundf(float x);
long long llroundl(long double x);

floating_point log1p (arithmetic x);
float          log1pf(float x);
long double    log1pl(long double x);

floating_point log2 (arithmetic x);
float          log2f(float x);
long double    log2l(long double x);

floating_point logb (arithmetic x);
float          logbf(float x);
long double    logbl(long double x);

long lrint (arithmetic x);
long lrintf(float x);
long lrintl(long double x);

long lround (arithmetic x);
long lroundf(float x);
long lroundl(long double x);

double      nan (const char* str);
float       nanf(const char* str);
long double nanl(const char* str);

floating_point nearbyint (arithmetic x);
float          nearbyintf(float x);
long double    nearbyintl(long double x);

floating_point nextafter (arithmetic x, arithmetic y);
float          nextafterf(float x, float y);
long double    nextafterl(long double x, long double y);

floating_point nexttoward (arithmetic x, long double y);
float          nexttowardf(float x, long double y);
long double    nexttowardl(long double x, long double y);

floating_point remainder (arithmetic x, arithmetic y);
float          remainderf(float x, float y);
long double    remainderl(long double x, long double y);

floating_point remquo (arithmetic x, arithmetic y, int* pquo);
float          remquof(float x, float y, int* pquo);
long double    remquol(long double x, long double y, int* pquo);

floating_point rint (arithmetic x);
float          rintf(float x);
long double    rintl(long double x);

floating_point round (arithmetic x);
float          roundf(float x);
long double    roundl(long double x);

floating_point scalbln (arithmetic x, long ex);
float          scalblnf(float x, long ex);
long double    scalblnl(long double x, long ex);

floating_point scalbn (arithmetic x, int ex);
float          scalbnf(float x, int ex);
long double    scalbnl(long double x, int ex);

floating_point tgamma (arithmetic x);
float          tgammaf(float x);
long double    tgammal(long double x);

floating_point trunc (arithmetic x);
float          truncf(float x);
long double    truncl(long double x);

*/

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#pragma GCC system_header
#endif

#include_next <math.h>

#ifdef __cplusplus

// We support including .h headers inside 'extern "C"' contexts, so switch
// back to C++ linkage before including these C++ headers.
extern "C++" {

#include <type_traits>
#include <limits>

// signbit

#ifdef signbit

template <class _A1>
_LIBCPP_INLINE_VISIBILITY
bool
__libcpp_signbit(_A1 __lcpp_x) _NOEXCEPT
{
    return signbit(__lcpp_x);
}

#undef signbit

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_floating_point<_A1>::value, bool>::type
signbit(_A1 __lcpp_x) _NOEXCEPT
{
    return __libcpp_signbit((typename std::__promote<_A1>::type)__lcpp_x);
}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<
    std::is_integral<_A1>::value && std::is_signed<_A1>::value, bool>::type
signbit(_A1 __lcpp_x) _NOEXCEPT
{ return __lcpp_x < 0; }

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<
    std::is_integral<_A1>::value && !std::is_signed<_A1>::value, bool>::type
signbit(_A1) _NOEXCEPT
{ return false; }

#elif defined(_LIBCPP_MSVCRT)

template <typename _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_floating_point<_A1>::value, bool>::type
signbit(_A1 __lcpp_x) _NOEXCEPT
{
  return ::signbit(static_cast<typename std::__promote<_A1>::type>(__lcpp_x));
}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<
    std::is_integral<_A1>::value && std::is_signed<_A1>::value, bool>::type
signbit(_A1 __lcpp_x) _NOEXCEPT
{ return __lcpp_x < 0; }

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<
    std::is_integral<_A1>::value && !std::is_signed<_A1>::value, bool>::type
signbit(_A1) _NOEXCEPT
{ return false; }

#endif  // signbit

// fpclassify

#ifdef fpclassify

template <class _A1>
_LIBCPP_INLINE_VISIBILITY
int
__libcpp_fpclassify(_A1 __lcpp_x) _NOEXCEPT
{
    return fpclassify(__lcpp_x);
}

#undef fpclassify

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_floating_point<_A1>::value, int>::type
fpclassify(_A1 __lcpp_x) _NOEXCEPT
{
    return __libcpp_fpclassify((typename std::__promote<_A1>::type)__lcpp_x);
}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, int>::type
fpclassify(_A1 __lcpp_x) _NOEXCEPT
{ return __lcpp_x == 0 ? FP_ZERO : FP_NORMAL; }

#elif defined(_LIBCPP_MSVCRT)

template <typename _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_floating_point<_A1>::value, bool>::type
fpclassify(_A1 __lcpp_x) _NOEXCEPT
{
  return ::fpclassify(static_cast<typename std::__promote<_A1>::type>(__lcpp_x));
}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, int>::type
fpclassify(_A1 __lcpp_x) _NOEXCEPT
{ return __lcpp_x == 0 ? FP_ZERO : FP_NORMAL; }

#endif  // fpclassify

// isfinite

#ifdef isfinite

template <class _A1>
_LIBCPP_INLINE_VISIBILITY
bool
__libcpp_isfinite(_A1 __lcpp_x) _NOEXCEPT
{
    return isfinite(__lcpp_x);
}

#undef isfinite

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<
    std::is_arithmetic<_A1>::value && std::numeric_limits<_A1>::has_infinity,
    bool>::type
isfinite(_A1 __lcpp_x) _NOEXCEPT
{
    return __libcpp_isfinite((typename std::__promote<_A1>::type)__lcpp_x);
}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<
    std::is_arithmetic<_A1>::value && !std::numeric_limits<_A1>::has_infinity,
    bool>::type
isfinite(_A1) _NOEXCEPT
{ return true; }

#endif  // isfinite

// isinf

#ifdef isinf

template <class _A1>
_LIBCPP_INLINE_VISIBILITY
bool
__libcpp_isinf(_A1 __lcpp_x) _NOEXCEPT
{
    return isinf(__lcpp_x);
}

#undef isinf

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<
    std::is_arithmetic<_A1>::value && std::numeric_limits<_A1>::has_infinity,
    bool>::type
isinf(_A1 __lcpp_x) _NOEXCEPT
{
    return __libcpp_isinf((typename std::__promote<_A1>::type)__lcpp_x);
}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<
    std::is_arithmetic<_A1>::value && !std::numeric_limits<_A1>::has_infinity,
    bool>::type
isinf(_A1) _NOEXCEPT
{ return false; }

#ifdef _LIBCPP_PREFERRED_OVERLOAD
inline _LIBCPP_INLINE_VISIBILITY
bool
isinf(float __lcpp_x) _NOEXCEPT { return __libcpp_isinf(__lcpp_x); }

inline _LIBCPP_INLINE_VISIBILITY _LIBCPP_PREFERRED_OVERLOAD
bool
isinf(double __lcpp_x) _NOEXCEPT { return __libcpp_isinf(__lcpp_x); }

inline _LIBCPP_INLINE_VISIBILITY
bool
isinf(long double __lcpp_x) _NOEXCEPT { return __libcpp_isinf(__lcpp_x); }
#endif

#endif  // isinf

// isnan

#ifdef isnan

template <class _A1>
_LIBCPP_INLINE_VISIBILITY
bool
__libcpp_isnan(_A1 __lcpp_x) _NOEXCEPT
{
    return isnan(__lcpp_x);
}

#undef isnan

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_floating_point<_A1>::value, bool>::type
isnan(_A1 __lcpp_x) _NOEXCEPT
{
    return __libcpp_isnan((typename std::__promote<_A1>::type)__lcpp_x);
}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, bool>::type
isnan(_A1) _NOEXCEPT
{ return false; }

#ifdef _LIBCPP_PREFERRED_OVERLOAD
inline _LIBCPP_INLINE_VISIBILITY
bool
isnan(float __lcpp_x) _NOEXCEPT { return __libcpp_isnan(__lcpp_x); }

inline _LIBCPP_INLINE_VISIBILITY _LIBCPP_PREFERRED_OVERLOAD
bool
isnan(double __lcpp_x) _NOEXCEPT { return __libcpp_isnan(__lcpp_x); }

inline _LIBCPP_INLINE_VISIBILITY
bool
isnan(long double __lcpp_x) _NOEXCEPT { return __libcpp_isnan(__lcpp_x); }
#endif

#endif  // isnan

// isnormal

#ifdef isnormal

template <class _A1>
_LIBCPP_INLINE_VISIBILITY
bool
__libcpp_isnormal(_A1 __lcpp_x) _NOEXCEPT
{
    return isnormal(__lcpp_x);
}

#undef isnormal

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_floating_point<_A1>::value, bool>::type
isnormal(_A1 __lcpp_x) _NOEXCEPT
{
    return __libcpp_isnormal((typename std::__promote<_A1>::type)__lcpp_x);
}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, bool>::type
isnormal(_A1 __lcpp_x) _NOEXCEPT
{ return __lcpp_x != 0; }

#endif  // isnormal

// isgreater

#ifdef isgreater

template <class _A1, class _A2>
_LIBCPP_INLINE_VISIBILITY
bool
__libcpp_isgreater(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    return isgreater(__lcpp_x, __lcpp_y);
}

#undef isgreater

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    bool
>::type
isgreater(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type type;
    return __libcpp_isgreater((type)__lcpp_x, (type)__lcpp_y);
}

#endif  // isgreater

// isgreaterequal

#ifdef isgreaterequal

template <class _A1, class _A2>
_LIBCPP_INLINE_VISIBILITY
bool
__libcpp_isgreaterequal(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    return isgreaterequal(__lcpp_x, __lcpp_y);
}

#undef isgreaterequal

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    bool
>::type
isgreaterequal(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type type;
    return __libcpp_isgreaterequal((type)__lcpp_x, (type)__lcpp_y);
}

#endif  // isgreaterequal

// isless

#ifdef isless

template <class _A1, class _A2>
_LIBCPP_INLINE_VISIBILITY
bool
__libcpp_isless(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    return isless(__lcpp_x, __lcpp_y);
}

#undef isless

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    bool
>::type
isless(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type type;
    return __libcpp_isless((type)__lcpp_x, (type)__lcpp_y);
}

#endif  // isless

// islessequal

#ifdef islessequal

template <class _A1, class _A2>
_LIBCPP_INLINE_VISIBILITY
bool
__libcpp_islessequal(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    return islessequal(__lcpp_x, __lcpp_y);
}

#undef islessequal

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    bool
>::type
islessequal(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type type;
    return __libcpp_islessequal((type)__lcpp_x, (type)__lcpp_y);
}

#endif  // islessequal

// islessgreater

#ifdef islessgreater

template <class _A1, class _A2>
_LIBCPP_INLINE_VISIBILITY
bool
__libcpp_islessgreater(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    return islessgreater(__lcpp_x, __lcpp_y);
}

#undef islessgreater

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    bool
>::type
islessgreater(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type type;
    return __libcpp_islessgreater((type)__lcpp_x, (type)__lcpp_y);
}

#endif  // islessgreater

// isunordered

#ifdef isunordered

template <class _A1, class _A2>
_LIBCPP_INLINE_VISIBILITY
bool
__libcpp_isunordered(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    return isunordered(__lcpp_x, __lcpp_y);
}

#undef isunordered

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    bool
>::type
isunordered(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type type;
    return __libcpp_isunordered((type)__lcpp_x, (type)__lcpp_y);
}

#endif  // isunordered

// abs

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY
float
abs(float __lcpp_x) _NOEXCEPT {return ::fabsf(__lcpp_x);}

inline _LIBCPP_INLINE_VISIBILITY
double
abs(double __lcpp_x) _NOEXCEPT {return ::fabs(__lcpp_x);}

inline _LIBCPP_INLINE_VISIBILITY
long double
abs(long double __lcpp_x) _NOEXCEPT {return ::fabsl(__lcpp_x);}
#endif // !(defined(_AIX) || defined(__sun__))

// acos

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       acos(float __lcpp_x) _NOEXCEPT       {return ::acosf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double acos(long double __lcpp_x) _NOEXCEPT {return ::acosl(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
acos(_A1 __lcpp_x) _NOEXCEPT {return ::acos((double)__lcpp_x);}

// asin

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       asin(float __lcpp_x) _NOEXCEPT       {return ::asinf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double asin(long double __lcpp_x) _NOEXCEPT {return ::asinl(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
asin(_A1 __lcpp_x) _NOEXCEPT {return ::asin((double)__lcpp_x);}

// atan

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       atan(float __lcpp_x) _NOEXCEPT       {return ::atanf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double atan(long double __lcpp_x) _NOEXCEPT {return ::atanl(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
atan(_A1 __lcpp_x) _NOEXCEPT {return ::atan((double)__lcpp_x);}

// atan2

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       atan2(float __lcpp_y, float __lcpp_x) _NOEXCEPT             {return ::atan2f(__lcpp_y, __lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double atan2(long double __lcpp_y, long double __lcpp_x) _NOEXCEPT {return ::atan2l(__lcpp_y, __lcpp_x);}
#endif

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::__lazy_enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    std::__promote<_A1, _A2>
>::type
atan2(_A1 __lcpp_y, _A2 __lcpp_x) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type __result_type;
    static_assert((!(std::is_same<_A1, __result_type>::value &&
                     std::is_same<_A2, __result_type>::value)), "");
    return ::atan2((__result_type)__lcpp_y, (__result_type)__lcpp_x);
}

// ceil

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       ceil(float __lcpp_x) _NOEXCEPT       {return ::ceilf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double ceil(long double __lcpp_x) _NOEXCEPT {return ::ceill(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
ceil(_A1 __lcpp_x) _NOEXCEPT {return ::ceil((double)__lcpp_x);}

// cos

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       cos(float __lcpp_x) _NOEXCEPT       {return ::cosf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double cos(long double __lcpp_x) _NOEXCEPT {return ::cosl(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
cos(_A1 __lcpp_x) _NOEXCEPT {return ::cos((double)__lcpp_x);}

// cosh

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       cosh(float __lcpp_x) _NOEXCEPT       {return ::coshf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double cosh(long double __lcpp_x) _NOEXCEPT {return ::coshl(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
cosh(_A1 __lcpp_x) _NOEXCEPT {return ::cosh((double)__lcpp_x);}

// exp

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       exp(float __lcpp_x) _NOEXCEPT       {return ::expf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double exp(long double __lcpp_x) _NOEXCEPT {return ::expl(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
exp(_A1 __lcpp_x) _NOEXCEPT {return ::exp((double)__lcpp_x);}

// fabs

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       fabs(float __lcpp_x) _NOEXCEPT       {return ::fabsf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double fabs(long double __lcpp_x) _NOEXCEPT {return ::fabsl(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
fabs(_A1 __lcpp_x) _NOEXCEPT {return ::fabs((double)__lcpp_x);}

// floor

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       floor(float __lcpp_x) _NOEXCEPT       {return ::floorf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double floor(long double __lcpp_x) _NOEXCEPT {return ::floorl(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
floor(_A1 __lcpp_x) _NOEXCEPT {return ::floor((double)__lcpp_x);}

// fmod

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       fmod(float __lcpp_x, float __lcpp_y) _NOEXCEPT             {return ::fmodf(__lcpp_x, __lcpp_y);}
inline _LIBCPP_INLINE_VISIBILITY long double fmod(long double __lcpp_x, long double __lcpp_y) _NOEXCEPT {return ::fmodl(__lcpp_x, __lcpp_y);}
#endif

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::__lazy_enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    std::__promote<_A1, _A2>
>::type
fmod(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type __result_type;
    static_assert((!(std::is_same<_A1, __result_type>::value &&
                     std::is_same<_A2, __result_type>::value)), "");
    return ::fmod((__result_type)__lcpp_x, (__result_type)__lcpp_y);
}

// frexp

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       frexp(float __lcpp_x, int* __lcpp_e) _NOEXCEPT       {return ::frexpf(__lcpp_x, __lcpp_e);}
inline _LIBCPP_INLINE_VISIBILITY long double frexp(long double __lcpp_x, int* __lcpp_e) _NOEXCEPT {return ::frexpl(__lcpp_x, __lcpp_e);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
frexp(_A1 __lcpp_x, int* __lcpp_e) _NOEXCEPT {return ::frexp((double)__lcpp_x, __lcpp_e);}

// ldexp

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       ldexp(float __lcpp_x, int __lcpp_e) _NOEXCEPT       {return ::ldexpf(__lcpp_x, __lcpp_e);}
inline _LIBCPP_INLINE_VISIBILITY long double ldexp(long double __lcpp_x, int __lcpp_e) _NOEXCEPT {return ::ldexpl(__lcpp_x, __lcpp_e);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
ldexp(_A1 __lcpp_x, int __lcpp_e) _NOEXCEPT {return ::ldexp((double)__lcpp_x, __lcpp_e);}

// log

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       log(float __lcpp_x) _NOEXCEPT       {return ::logf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double log(long double __lcpp_x) _NOEXCEPT {return ::logl(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
log(_A1 __lcpp_x) _NOEXCEPT {return ::log((double)__lcpp_x);}

// log10

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       log10(float __lcpp_x) _NOEXCEPT       {return ::log10f(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double log10(long double __lcpp_x) _NOEXCEPT {return ::log10l(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
log10(_A1 __lcpp_x) _NOEXCEPT {return ::log10((double)__lcpp_x);}

// modf

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       modf(float __lcpp_x, float* __lcpp_y) _NOEXCEPT             {return ::modff(__lcpp_x, __lcpp_y);}
inline _LIBCPP_INLINE_VISIBILITY long double modf(long double __lcpp_x, long double* __lcpp_y) _NOEXCEPT {return ::modfl(__lcpp_x, __lcpp_y);}
#endif

// pow

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       pow(float __lcpp_x, float __lcpp_y) _NOEXCEPT             {return ::powf(__lcpp_x, __lcpp_y);}
inline _LIBCPP_INLINE_VISIBILITY long double pow(long double __lcpp_x, long double __lcpp_y) _NOEXCEPT {return ::powl(__lcpp_x, __lcpp_y);}
#endif

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::__lazy_enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    std::__promote<_A1, _A2>
>::type
pow(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type __result_type;
    static_assert((!(std::is_same<_A1, __result_type>::value &&
                     std::is_same<_A2, __result_type>::value)), "");
    return ::pow((__result_type)__lcpp_x, (__result_type)__lcpp_y);
}

// sin

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       sin(float __lcpp_x) _NOEXCEPT       {return ::sinf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double sin(long double __lcpp_x) _NOEXCEPT {return ::sinl(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
sin(_A1 __lcpp_x) _NOEXCEPT {return ::sin((double)__lcpp_x);}

// sinh

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       sinh(float __lcpp_x) _NOEXCEPT       {return ::sinhf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double sinh(long double __lcpp_x) _NOEXCEPT {return ::sinhl(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
sinh(_A1 __lcpp_x) _NOEXCEPT {return ::sinh((double)__lcpp_x);}

// sqrt

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       sqrt(float __lcpp_x) _NOEXCEPT       {return ::sqrtf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double sqrt(long double __lcpp_x) _NOEXCEPT {return ::sqrtl(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
sqrt(_A1 __lcpp_x) _NOEXCEPT {return ::sqrt((double)__lcpp_x);}

// tan

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       tan(float __lcpp_x) _NOEXCEPT       {return ::tanf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double tan(long double __lcpp_x) _NOEXCEPT {return ::tanl(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
tan(_A1 __lcpp_x) _NOEXCEPT {return ::tan((double)__lcpp_x);}

// tanh

#if !(defined(_AIX) || defined(__sun__))
inline _LIBCPP_INLINE_VISIBILITY float       tanh(float __lcpp_x) _NOEXCEPT       {return ::tanhf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double tanh(long double __lcpp_x) _NOEXCEPT {return ::tanhl(__lcpp_x);}
#endif

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
tanh(_A1 __lcpp_x) _NOEXCEPT {return ::tanh((double)__lcpp_x);}

// acosh

inline _LIBCPP_INLINE_VISIBILITY float       acosh(float __lcpp_x) _NOEXCEPT       {return ::acoshf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double acosh(long double __lcpp_x) _NOEXCEPT {return ::acoshl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
acosh(_A1 __lcpp_x) _NOEXCEPT {return ::acosh((double)__lcpp_x);}

// asinh

inline _LIBCPP_INLINE_VISIBILITY float       asinh(float __lcpp_x) _NOEXCEPT       {return ::asinhf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double asinh(long double __lcpp_x) _NOEXCEPT {return ::asinhl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
asinh(_A1 __lcpp_x) _NOEXCEPT {return ::asinh((double)__lcpp_x);}

// atanh

inline _LIBCPP_INLINE_VISIBILITY float       atanh(float __lcpp_x) _NOEXCEPT       {return ::atanhf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double atanh(long double __lcpp_x) _NOEXCEPT {return ::atanhl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
atanh(_A1 __lcpp_x) _NOEXCEPT {return ::atanh((double)__lcpp_x);}

// cbrt

inline _LIBCPP_INLINE_VISIBILITY float       cbrt(float __lcpp_x) _NOEXCEPT       {return ::cbrtf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double cbrt(long double __lcpp_x) _NOEXCEPT {return ::cbrtl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
cbrt(_A1 __lcpp_x) _NOEXCEPT {return ::cbrt((double)__lcpp_x);}

// copysign

inline _LIBCPP_INLINE_VISIBILITY float copysign(float __lcpp_x,
                                                float __lcpp_y) _NOEXCEPT {
  return ::copysignf(__lcpp_x, __lcpp_y);
}
inline _LIBCPP_INLINE_VISIBILITY long double
copysign(long double __lcpp_x, long double __lcpp_y) _NOEXCEPT {
  return ::copysignl(__lcpp_x, __lcpp_y);
}

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::__lazy_enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    std::__promote<_A1, _A2>
>::type
copysign(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type __result_type;
    static_assert((!(std::is_same<_A1, __result_type>::value &&
                     std::is_same<_A2, __result_type>::value)), "");
    return ::copysign((__result_type)__lcpp_x, (__result_type)__lcpp_y);
}

// erf

inline _LIBCPP_INLINE_VISIBILITY float       erf(float __lcpp_x) _NOEXCEPT       {return ::erff(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double erf(long double __lcpp_x) _NOEXCEPT {return ::erfl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
erf(_A1 __lcpp_x) _NOEXCEPT {return ::erf((double)__lcpp_x);}

// erfc

inline _LIBCPP_INLINE_VISIBILITY float       erfc(float __lcpp_x) _NOEXCEPT       {return ::erfcf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double erfc(long double __lcpp_x) _NOEXCEPT {return ::erfcl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
erfc(_A1 __lcpp_x) _NOEXCEPT {return ::erfc((double)__lcpp_x);}

// exp2

inline _LIBCPP_INLINE_VISIBILITY float       exp2(float __lcpp_x) _NOEXCEPT       {return ::exp2f(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double exp2(long double __lcpp_x) _NOEXCEPT {return ::exp2l(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
exp2(_A1 __lcpp_x) _NOEXCEPT {return ::exp2((double)__lcpp_x);}

// expm1

inline _LIBCPP_INLINE_VISIBILITY float       expm1(float __lcpp_x) _NOEXCEPT       {return ::expm1f(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double expm1(long double __lcpp_x) _NOEXCEPT {return ::expm1l(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
expm1(_A1 __lcpp_x) _NOEXCEPT {return ::expm1((double)__lcpp_x);}

// fdim

inline _LIBCPP_INLINE_VISIBILITY float       fdim(float __lcpp_x, float __lcpp_y) _NOEXCEPT             {return ::fdimf(__lcpp_x, __lcpp_y);}
inline _LIBCPP_INLINE_VISIBILITY long double fdim(long double __lcpp_x, long double __lcpp_y) _NOEXCEPT {return ::fdiml(__lcpp_x, __lcpp_y);}

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::__lazy_enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    std::__promote<_A1, _A2>
>::type
fdim(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type __result_type;
    static_assert((!(std::is_same<_A1, __result_type>::value &&
                     std::is_same<_A2, __result_type>::value)), "");
    return ::fdim((__result_type)__lcpp_x, (__result_type)__lcpp_y);
}

// fma

inline _LIBCPP_INLINE_VISIBILITY float       fma(float __lcpp_x, float __lcpp_y, float __lcpp_z) _NOEXCEPT                   {return ::fmaf(__lcpp_x, __lcpp_y, __lcpp_z);}
inline _LIBCPP_INLINE_VISIBILITY long double fma(long double __lcpp_x, long double __lcpp_y, long double __lcpp_z) _NOEXCEPT {return ::fmal(__lcpp_x, __lcpp_y, __lcpp_z);}

template <class _A1, class _A2, class _A3>
inline _LIBCPP_INLINE_VISIBILITY
typename std::__lazy_enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value &&
    std::is_arithmetic<_A3>::value,
    std::__promote<_A1, _A2, _A3>
>::type
fma(_A1 __lcpp_x, _A2 __lcpp_y, _A3 __lcpp_z) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2, _A3>::type __result_type;
    static_assert((!(std::is_same<_A1, __result_type>::value &&
                     std::is_same<_A2, __result_type>::value &&
                     std::is_same<_A3, __result_type>::value)), "");
    return ::fma((__result_type)__lcpp_x, (__result_type)__lcpp_y, (__result_type)__lcpp_z);
}

// fmax

inline _LIBCPP_INLINE_VISIBILITY float       fmax(float __lcpp_x, float __lcpp_y) _NOEXCEPT             {return ::fmaxf(__lcpp_x, __lcpp_y);}
inline _LIBCPP_INLINE_VISIBILITY long double fmax(long double __lcpp_x, long double __lcpp_y) _NOEXCEPT {return ::fmaxl(__lcpp_x, __lcpp_y);}

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::__lazy_enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    std::__promote<_A1, _A2>
>::type
fmax(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type __result_type;
    static_assert((!(std::is_same<_A1, __result_type>::value &&
                     std::is_same<_A2, __result_type>::value)), "");
    return ::fmax((__result_type)__lcpp_x, (__result_type)__lcpp_y);
}

// fmin

inline _LIBCPP_INLINE_VISIBILITY float       fmin(float __lcpp_x, float __lcpp_y) _NOEXCEPT             {return ::fminf(__lcpp_x, __lcpp_y);}
inline _LIBCPP_INLINE_VISIBILITY long double fmin(long double __lcpp_x, long double __lcpp_y) _NOEXCEPT {return ::fminl(__lcpp_x, __lcpp_y);}

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::__lazy_enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    std::__promote<_A1, _A2>
>::type
fmin(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type __result_type;
    static_assert((!(std::is_same<_A1, __result_type>::value &&
                     std::is_same<_A2, __result_type>::value)), "");
    return ::fmin((__result_type)__lcpp_x, (__result_type)__lcpp_y);
}

// hypot

inline _LIBCPP_INLINE_VISIBILITY float       hypot(float __lcpp_x, float __lcpp_y) _NOEXCEPT             {return ::hypotf(__lcpp_x, __lcpp_y);}
inline _LIBCPP_INLINE_VISIBILITY long double hypot(long double __lcpp_x, long double __lcpp_y) _NOEXCEPT {return ::hypotl(__lcpp_x, __lcpp_y);}

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::__lazy_enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    std::__promote<_A1, _A2>
>::type
hypot(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type __result_type;
    static_assert((!(std::is_same<_A1, __result_type>::value &&
                     std::is_same<_A2, __result_type>::value)), "");
    return ::hypot((__result_type)__lcpp_x, (__result_type)__lcpp_y);
}

// ilogb

inline _LIBCPP_INLINE_VISIBILITY int ilogb(float __lcpp_x) _NOEXCEPT       {return ::ilogbf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY int ilogb(long double __lcpp_x) _NOEXCEPT {return ::ilogbl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, int>::type
ilogb(_A1 __lcpp_x) _NOEXCEPT {return ::ilogb((double)__lcpp_x);}

// lgamma

inline _LIBCPP_INLINE_VISIBILITY float       lgamma(float __lcpp_x) _NOEXCEPT       {return ::lgammaf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double lgamma(long double __lcpp_x) _NOEXCEPT {return ::lgammal(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
lgamma(_A1 __lcpp_x) _NOEXCEPT {return ::lgamma((double)__lcpp_x);}

// llrint

inline _LIBCPP_INLINE_VISIBILITY long long llrint(float __lcpp_x) _NOEXCEPT       {return ::llrintf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long long llrint(long double __lcpp_x) _NOEXCEPT {return ::llrintl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, long long>::type
llrint(_A1 __lcpp_x) _NOEXCEPT {return ::llrint((double)__lcpp_x);}

// llround

inline _LIBCPP_INLINE_VISIBILITY long long llround(float __lcpp_x) _NOEXCEPT       {return ::llroundf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long long llround(long double __lcpp_x) _NOEXCEPT {return ::llroundl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, long long>::type
llround(_A1 __lcpp_x) _NOEXCEPT {return ::llround((double)__lcpp_x);}

// log1p

inline _LIBCPP_INLINE_VISIBILITY float       log1p(float __lcpp_x) _NOEXCEPT       {return ::log1pf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double log1p(long double __lcpp_x) _NOEXCEPT {return ::log1pl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
log1p(_A1 __lcpp_x) _NOEXCEPT {return ::log1p((double)__lcpp_x);}

// log2

inline _LIBCPP_INLINE_VISIBILITY float       log2(float __lcpp_x) _NOEXCEPT       {return ::log2f(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double log2(long double __lcpp_x) _NOEXCEPT {return ::log2l(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
log2(_A1 __lcpp_x) _NOEXCEPT {return ::log2((double)__lcpp_x);}

// logb

inline _LIBCPP_INLINE_VISIBILITY float       logb(float __lcpp_x) _NOEXCEPT       {return ::logbf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double logb(long double __lcpp_x) _NOEXCEPT {return ::logbl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
logb(_A1 __lcpp_x) _NOEXCEPT {return ::logb((double)__lcpp_x);}

// lrint

inline _LIBCPP_INLINE_VISIBILITY long lrint(float __lcpp_x) _NOEXCEPT       {return ::lrintf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long lrint(long double __lcpp_x) _NOEXCEPT {return ::lrintl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, long>::type
lrint(_A1 __lcpp_x) _NOEXCEPT {return ::lrint((double)__lcpp_x);}

// lround

inline _LIBCPP_INLINE_VISIBILITY long lround(float __lcpp_x) _NOEXCEPT       {return ::lroundf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long lround(long double __lcpp_x) _NOEXCEPT {return ::lroundl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, long>::type
lround(_A1 __lcpp_x) _NOEXCEPT {return ::lround((double)__lcpp_x);}

// nan

// nearbyint

inline _LIBCPP_INLINE_VISIBILITY float       nearbyint(float __lcpp_x) _NOEXCEPT       {return ::nearbyintf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double nearbyint(long double __lcpp_x) _NOEXCEPT {return ::nearbyintl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
nearbyint(_A1 __lcpp_x) _NOEXCEPT {return ::nearbyint((double)__lcpp_x);}

// nextafter

inline _LIBCPP_INLINE_VISIBILITY float       nextafter(float __lcpp_x, float __lcpp_y) _NOEXCEPT             {return ::nextafterf(__lcpp_x, __lcpp_y);}
inline _LIBCPP_INLINE_VISIBILITY long double nextafter(long double __lcpp_x, long double __lcpp_y) _NOEXCEPT {return ::nextafterl(__lcpp_x, __lcpp_y);}

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::__lazy_enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    std::__promote<_A1, _A2>
>::type
nextafter(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type __result_type;
    static_assert((!(std::is_same<_A1, __result_type>::value &&
                     std::is_same<_A2, __result_type>::value)), "");
    return ::nextafter((__result_type)__lcpp_x, (__result_type)__lcpp_y);
}

// nexttoward

inline _LIBCPP_INLINE_VISIBILITY float       nexttoward(float __lcpp_x, long double __lcpp_y) _NOEXCEPT       {return ::nexttowardf(__lcpp_x, __lcpp_y);}
inline _LIBCPP_INLINE_VISIBILITY long double nexttoward(long double __lcpp_x, long double __lcpp_y) _NOEXCEPT {return ::nexttowardl(__lcpp_x, __lcpp_y);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
nexttoward(_A1 __lcpp_x, long double __lcpp_y) _NOEXCEPT {return ::nexttoward((double)__lcpp_x, __lcpp_y);}

// remainder

inline _LIBCPP_INLINE_VISIBILITY float       remainder(float __lcpp_x, float __lcpp_y) _NOEXCEPT             {return ::remainderf(__lcpp_x, __lcpp_y);}
inline _LIBCPP_INLINE_VISIBILITY long double remainder(long double __lcpp_x, long double __lcpp_y) _NOEXCEPT {return ::remainderl(__lcpp_x, __lcpp_y);}

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::__lazy_enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    std::__promote<_A1, _A2>
>::type
remainder(_A1 __lcpp_x, _A2 __lcpp_y) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type __result_type;
    static_assert((!(std::is_same<_A1, __result_type>::value &&
                     std::is_same<_A2, __result_type>::value)), "");
    return ::remainder((__result_type)__lcpp_x, (__result_type)__lcpp_y);
}

// remquo

inline _LIBCPP_INLINE_VISIBILITY float       remquo(float __lcpp_x, float __lcpp_y, int* __lcpp_z) _NOEXCEPT             {return ::remquof(__lcpp_x, __lcpp_y, __lcpp_z);}
inline _LIBCPP_INLINE_VISIBILITY long double remquo(long double __lcpp_x, long double __lcpp_y, int* __lcpp_z) _NOEXCEPT {return ::remquol(__lcpp_x, __lcpp_y, __lcpp_z);}

template <class _A1, class _A2>
inline _LIBCPP_INLINE_VISIBILITY
typename std::__lazy_enable_if
<
    std::is_arithmetic<_A1>::value &&
    std::is_arithmetic<_A2>::value,
    std::__promote<_A1, _A2>
>::type
remquo(_A1 __lcpp_x, _A2 __lcpp_y, int* __lcpp_z) _NOEXCEPT
{
    typedef typename std::__promote<_A1, _A2>::type __result_type;
    static_assert((!(std::is_same<_A1, __result_type>::value &&
                     std::is_same<_A2, __result_type>::value)), "");
    return ::remquo((__result_type)__lcpp_x, (__result_type)__lcpp_y, __lcpp_z);
}

// rint

inline _LIBCPP_INLINE_VISIBILITY float       rint(float __lcpp_x) _NOEXCEPT       {return ::rintf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double rint(long double __lcpp_x) _NOEXCEPT {return ::rintl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
rint(_A1 __lcpp_x) _NOEXCEPT {return ::rint((double)__lcpp_x);}

// round

inline _LIBCPP_INLINE_VISIBILITY float       round(float __lcpp_x) _NOEXCEPT       {return ::roundf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double round(long double __lcpp_x) _NOEXCEPT {return ::roundl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
round(_A1 __lcpp_x) _NOEXCEPT {return ::round((double)__lcpp_x);}

// scalbln

inline _LIBCPP_INLINE_VISIBILITY float       scalbln(float __lcpp_x, long __lcpp_y) _NOEXCEPT       {return ::scalblnf(__lcpp_x, __lcpp_y);}
inline _LIBCPP_INLINE_VISIBILITY long double scalbln(long double __lcpp_x, long __lcpp_y) _NOEXCEPT {return ::scalblnl(__lcpp_x, __lcpp_y);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
scalbln(_A1 __lcpp_x, long __lcpp_y) _NOEXCEPT {return ::scalbln((double)__lcpp_x, __lcpp_y);}

// scalbn

inline _LIBCPP_INLINE_VISIBILITY float       scalbn(float __lcpp_x, int __lcpp_y) _NOEXCEPT       {return ::scalbnf(__lcpp_x, __lcpp_y);}
inline _LIBCPP_INLINE_VISIBILITY long double scalbn(long double __lcpp_x, int __lcpp_y) _NOEXCEPT {return ::scalbnl(__lcpp_x, __lcpp_y);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
scalbn(_A1 __lcpp_x, int __lcpp_y) _NOEXCEPT {return ::scalbn((double)__lcpp_x, __lcpp_y);}

// tgamma

inline _LIBCPP_INLINE_VISIBILITY float       tgamma(float __lcpp_x) _NOEXCEPT       {return ::tgammaf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double tgamma(long double __lcpp_x) _NOEXCEPT {return ::tgammal(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
tgamma(_A1 __lcpp_x) _NOEXCEPT {return ::tgamma((double)__lcpp_x);}

// trunc

inline _LIBCPP_INLINE_VISIBILITY float       trunc(float __lcpp_x) _NOEXCEPT       {return ::truncf(__lcpp_x);}
inline _LIBCPP_INLINE_VISIBILITY long double trunc(long double __lcpp_x) _NOEXCEPT {return ::truncl(__lcpp_x);}

template <class _A1>
inline _LIBCPP_INLINE_VISIBILITY
typename std::enable_if<std::is_integral<_A1>::value, double>::type
trunc(_A1 __lcpp_x) _NOEXCEPT {return ::trunc((double)__lcpp_x);}

} // extern "C++"

#endif // __cplusplus

#else // _LIBCPP_MATH_H

// This include lives outside the header guard in order to support an MSVC
// extension which allows users to do:
//
// #define _USE_MATH_DEFINES
// #include <math.h>
//
// and receive the definitions of mathematical constants, even if <math.h>
// has previously been included.
#if defined(_LIBCPP_MSVCRT) && defined(_USE_MATH_DEFINES)
#include_next <math.h>
#endif

#endif  // _LIBCPP_MATH_H
