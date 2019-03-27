dnl
dnl This file contains macros for testing linkage.
dnl

dnl
dnl Check to see if the (math function) argument passed is
dnl declared when using the c++ compiler
dnl ASSUMES argument is a math function with ONE parameter
dnl
dnl GLIBCXX_CHECK_MATH_DECL_1
AC_DEFUN([GLIBCXX_CHECK_MATH_DECL_1], [
  AC_MSG_CHECKING([for $1 declaration])
  if test x${glibcxx_cv_func_$1_use+set} != xset; then
    AC_CACHE_VAL(glibcxx_cv_func_$1_use, [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([#include <math.h>
		      #ifdef HAVE_IEEEFP_H
		      #include <ieeefp.h>
		      #endif
		     ],
                     [ $1(0);],
                      [glibcxx_cv_func_$1_use=yes], [glibcxx_cv_func_$1_use=no])
      AC_LANG_RESTORE
    ])
  fi
  AC_MSG_RESULT($glibcxx_cv_func_$1_use)
])


dnl 
dnl Define autoheader template for using the underscore functions
dnl For each parameter, create a macro where if func doesn't exist,
dnl but _func does, then it will "#define func _func".
dnl
dnl GLIBCXX_MAYBE_UNDERSCORED_FUNCS
AC_DEFUN([GLIBCXX_MAYBE_UNDERSCORED_FUNCS], 
[AC_FOREACH([glibcxx_ufunc], [$1],
  [AH_VERBATIM(_[]glibcxx_ufunc,
[#if defined (]AS_TR_CPP(HAVE__[]glibcxx_ufunc)[) && ! defined (]AS_TR_CPP(HAVE_[]glibcxx_ufunc)[)
# define ]AS_TR_CPP(HAVE_[]glibcxx_ufunc)[ 1
# define ]glibcxx_ufunc[ _]glibcxx_ufunc[
#endif])])
])


dnl
dnl Check to see if the (math function) argument passed is
dnl 1) declared when using the c++ compiler
dnl 2) has "C" linkage
dnl 3) if not, see if 1) and 2) for argument prepended with '_'
dnl
dnl Define HAVE_CARGF etc if "cargf" is declared and links
dnl
dnl argument 1 is name of function to check
dnl
dnl ASSUMES argument is a math function with ONE parameter
dnl
dnl GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1
AC_DEFUN([GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1], [
  GLIBCXX_CHECK_MATH_DECL_1($1)
  if test x$glibcxx_cv_func_$1_use = x"yes"; then
    AC_CHECK_FUNCS($1)
  else
    GLIBCXX_CHECK_MATH_DECL_1(_$1)
    if test x$glibcxx_cv_func__$1_use = x"yes"; then
      AC_CHECK_FUNCS(_$1)
    fi
  fi
  GLIBCXX_MAYBE_UNDERSCORED_FUNCS($1)
])


dnl
dnl Like GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1, but does a bunch of
dnl of functions at once.  It's an all-or-nothing check -- either
dnl HAVE_XYZ is defined for each of the functions, or for none of them.
dnl Doing it this way saves significant configure time.
AC_DEFUN([GLIBCXX_CHECK_MATH_DECLS_AND_LINKAGES_1], [
  define([funclist],patsubst($3,\(\w+\)\(\W*\),\1 ))dnl
  AC_MSG_CHECKING([for $1 functions])
  AC_CACHE_VAL(glibcxx_cv_func_$2_use, [
    AC_LANG_SAVE
    AC_LANG_CPLUSPLUS
    AC_TRY_COMPILE([#include <math.h>],
                   patsubst(funclist,[\w+],[\& (0);]),
                   [glibcxx_cv_func_$2_use=yes],
                   [glibcxx_cv_func_$2_use=no])
    AC_LANG_RESTORE])
  AC_MSG_RESULT($glibcxx_cv_func_$2_use)
  if test x$glibcxx_cv_func_$2_use = x"yes"; then
    AC_CHECK_FUNCS(funclist)
  else
    AC_MSG_CHECKING([for _$1 functions])
    AC_CACHE_VAL(glibcxx_cv_func__$2_use, [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([#include <math.h>],
                     patsubst(funclist,[\w+],[_\& (0);]),
                     [glibcxx_cv_func__$2_use=yes],
                     [glibcxx_cv_func__$2_use=no])
      AC_LANG_RESTORE])
    AC_MSG_RESULT($glibcxx_cv_func__$2_use)
    if test x$glibcxx_cv_func__$2_use = x"yes"; then
      AC_CHECK_FUNCS(patsubst(funclist,[\w+],[_\&]))
    fi
  fi
  GLIBCXX_MAYBE_UNDERSCORED_FUNCS(funclist)
  undefine([funclist])
])

dnl
dnl Check to see if the (math function) argument passed is
dnl declared when using the c++ compiler
dnl ASSUMES argument is a math function with TWO parameters
dnl
dnl GLIBCXX_CHECK_MATH_DECL_2
AC_DEFUN([GLIBCXX_CHECK_MATH_DECL_2], [
  AC_MSG_CHECKING([for $1 declaration])
  if test x${glibcxx_cv_func_$1_use+set} != xset; then
    AC_CACHE_VAL(glibcxx_cv_func_$1_use, [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([#include <math.h>],
                     [ $1(0, 0);],
                     [glibcxx_cv_func_$1_use=yes], [glibcxx_cv_func_$1_use=no])
      AC_LANG_RESTORE
    ])
  fi
  AC_MSG_RESULT($glibcxx_cv_func_$1_use)
])

dnl
dnl Check to see if the (math function) argument passed is
dnl 1) declared when using the c++ compiler
dnl 2) has "C" linkage
dnl
dnl Define HAVE_CARGF etc if "cargf" is declared and links
dnl
dnl argument 1 is name of function to check
dnl
dnl ASSUMES argument is a math function with TWO parameters
dnl
dnl GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2
AC_DEFUN([GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2], [
  GLIBCXX_CHECK_MATH_DECL_2($1)
  if test x$glibcxx_cv_func_$1_use = x"yes"; then
    AC_CHECK_FUNCS($1)
  else
    GLIBCXX_CHECK_MATH_DECL_2(_$1)
    if test x$glibcxx_cv_func__$1_use = x"yes"; then
      AC_CHECK_FUNCS(_$1)
    fi
  fi
  GLIBCXX_MAYBE_UNDERSCORED_FUNCS($1)
])


dnl
dnl Check to see if the (math function) argument passed is
dnl declared when using the c++ compiler
dnl ASSUMES argument is a math function with THREE parameters
dnl
dnl GLIBCXX_CHECK_MATH_DECL_3
AC_DEFUN([GLIBCXX_CHECK_MATH_DECL_3], [
  AC_MSG_CHECKING([for $1 declaration])
  if test x${glibcxx_cv_func_$1_use+set} != xset; then
    AC_CACHE_VAL(glibcxx_cv_func_$1_use, [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([#include <math.h>],
                     [ $1(0, 0, 0);],
                     [glibcxx_cv_func_$1_use=yes], [glibcxx_cv_func_$1_use=no])
      AC_LANG_RESTORE
    ])
  fi
  AC_MSG_RESULT($glibcxx_cv_func_$1_use)
])

dnl
dnl Check to see if the (math function) argument passed is
dnl 1) declared when using the c++ compiler
dnl 2) has "C" linkage
dnl
dnl Define HAVE_CARGF etc if "cargf" is declared and links
dnl
dnl argument 1 is name of function to check
dnl
dnl ASSUMES argument is a math function with THREE parameters
dnl
dnl GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_3
AC_DEFUN([GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_3], [
  GLIBCXX_CHECK_MATH_DECL_3($1)
  if test x$glibcxx_cv_func_$1_use = x"yes"; then
    AC_CHECK_FUNCS($1)
  else
    GLIBCXX_CHECK_MATH_DECL_3(_$1)
    if test x$glibcxx_cv_func__$1_use = x"yes"; then
      AC_CHECK_FUNCS(_$1)
    fi
  fi
  GLIBCXX_MAYBE_UNDERSCORED_FUNCS($1)
])


dnl
dnl Check to see if the (stdlib function) argument passed is
dnl 1) declared when using the c++ compiler
dnl 2) has "C" linkage
dnl
dnl argument 1 is name of function to check
dnl
dnl ASSUMES argument is a stdlib function without parameters
dnl
dnl GLIBCXX_CHECK_STDLIB_DECL_AND_LINKAGE_0
AC_DEFUN([GLIBCXX_CHECK_STDLIB_DECL_AND_LINKAGE_0], [
  AC_MSG_CHECKING([for $1 declaration])
  if test x${glibcxx_cv_func_$1_use+set} != xset; then
    AC_CACHE_VAL(glibcxx_cv_func_$1_use, [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([#include <stdlib.h>],
                     [ $1();],
                     [glibcxx_cv_func_$1_use=yes], [glibcxx_cv_func_$1_use=no])
      AC_LANG_RESTORE
    ])
  fi
  AC_MSG_RESULT($glibcxx_cv_func_$1_use)
  if test x$glibcxx_cv_func_$1_use = x"yes"; then
    AC_CHECK_FUNCS($1)
  fi
])


dnl
dnl Check to see if the (stdlib function) argument passed is
dnl 1) declared when using the c++ compiler
dnl 2) has "C" linkage
dnl
dnl argument 1 is name of function to check
dnl
dnl ASSUMES argument is a stdlib function with TWO parameters
dnl
dnl GLIBCXX_CHECK_STDLIB_DECL_AND_LINKAGE_2
AC_DEFUN([GLIBCXX_CHECK_STDLIB_DECL_AND_LINKAGE_2], [
  AC_MSG_CHECKING([for $1 declaration])
  if test x${glibcxx_cv_func_$1_use+set} != xset; then
    AC_CACHE_VAL(glibcxx_cv_func_$1_use, [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([#include <stdlib.h>],
                     [ $1(0, 0);],
                     [glibcxx_cv_func_$1_use=yes], [glibcxx_cv_func_$1_use=no])
      AC_LANG_RESTORE
    ])
  fi
  AC_MSG_RESULT($glibcxx_cv_func_$1_use)
  if test x$glibcxx_cv_func_$1_use = x"yes"; then
    AC_CHECK_FUNCS($1)
  fi
  GLIBCXX_MAYBE_UNDERSCORED_FUNCS($1)
])


dnl
dnl Check to see if the (stdlib function) argument passed is
dnl 1) declared when using the c++ compiler
dnl 2) has "C" linkage
dnl
dnl argument 1 is name of function to check
dnl
dnl ASSUMES argument is a stdlib function with THREE parameters
dnl
dnl GLIBCXX_CHECK_STDLIB_DECL_AND_LINKAGE_3
AC_DEFUN([GLIBCXX_CHECK_STDLIB_DECL_AND_LINKAGE_3], [
  AC_MSG_CHECKING([for $1 declaration])
  if test x${glibcxx_cv_func_$1_use+set} != xset; then
    AC_CACHE_VAL(glibcxx_cv_func_$1_use, [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([#include <stdlib.h>],
                     [ $1(0, 0, 0);],
                     [glibcxx_cv_func_$1_use=yes], [glibcxx_cv_func_$1_use=no])
      AC_LANG_RESTORE
    ])
  fi
  AC_MSG_RESULT($glibcxx_cv_func_$1_use)
  if test x$glibcxx_cv_func_$1_use = x"yes"; then
    AC_CHECK_FUNCS($1)
  fi
])

dnl
dnl Because the builtins are picky picky picky about the arguments they take,
dnl do an explict linkage tests here.
dnl Check to see if the (math function) argument passed is
dnl 1) declared when using the c++ compiler
dnl 2) has "C" linkage
dnl
dnl Define HAVE_CARGF etc if "cargf" is declared and links
dnl
dnl argument 1 is name of function to check
dnl
dnl ASSUMES argument is a math function with ONE parameter
dnl
dnl GLIBCXX_CHECK_BUILTIN_MATH_DECL_LINKAGE_1
AC_DEFUN([GLIBCXX_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1], [
  AC_MSG_CHECKING([for $1 declaration])
  if test x${glibcxx_cv_func_$1_use+set} != xset; then
    AC_CACHE_VAL(glibcxx_cv_func_$1_use, [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([#include <math.h>],
                     [ $1(0);],
                     [glibcxx_cv_func_$1_use=yes], [glibcxx_cv_func_$1_use=no])
      AC_LANG_RESTORE
    ])
  fi
  AC_MSG_RESULT($glibcxx_cv_func_$1_use)
  if test x$glibcxx_cv_func_$1_use = x"yes"; then
    AC_MSG_CHECKING([for $1 linkage])
    if test x${glibcxx_cv_func_$1_link+set} != xset; then
      AC_CACHE_VAL(glibcxx_cv_func_$1_link, [
        AC_TRY_LINK([#include <math.h>],
                    [ $1(0);],
                    [glibcxx_cv_func_$1_link=yes], [glibcxx_cv_func_$1_link=no])
      ])
    fi
    AC_MSG_RESULT($glibcxx_cv_func_$1_link)
    define([NAME], [translit([$1],[abcdefghijklmnopqrstuvwxyz],
    				  [ABCDEFGHIJKLMNOPQRSTUVWXYZ])])
    if test x$glibcxx_cv_func_$1_link = x"yes"; then
      AC_DEFINE([HAVE_]NAME, 1, [Define if the compiler/host combination has $1.])
    fi
    undefine([NAME])
  fi
])


dnl
dnl Check to see what builtin math functions are supported
dnl
dnl check for __builtin_abs
dnl check for __builtin_fabsf
dnl check for __builtin_fabs
dnl check for __builtin_fabl
dnl check for __builtin_labs
dnl check for __builtin_sqrtf
dnl check for __builtin_sqrtl
dnl check for __builtin_sqrt
dnl check for __builtin_sinf
dnl check for __builtin_sin
dnl check for __builtin_sinl
dnl check for __builtin_cosf
dnl check for __builtin_cos
dnl check for __builtin_cosl
dnl
dnl GLIBCXX_CHECK_BUILTIN_MATH_SUPPORT
AC_DEFUN([GLIBCXX_CHECK_BUILTIN_MATH_SUPPORT], [
  dnl Test for builtin math functions.
  dnl These are made in gcc/c-common.c
  GLIBCXX_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_abs)
  GLIBCXX_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_fabsf)
  GLIBCXX_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_fabs)
  GLIBCXX_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_fabsl)
  GLIBCXX_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_labs)

  GLIBCXX_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_sqrtf)
  GLIBCXX_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_sqrt)
  GLIBCXX_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_sqrtl)

  GLIBCXX_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_sinf)
  GLIBCXX_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_sin)
  GLIBCXX_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_sinl)

  GLIBCXX_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_cosf)
  GLIBCXX_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_cos)
  GLIBCXX_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_cosl)
])

dnl
dnl Check to see what the underlying c library is like
dnl These checks need to do two things:
dnl 1) make sure the name is declared when using the c++ compiler
dnl 2) make sure the name has "C" linkage
dnl This might seem like overkill but experience has shown that it's not...
dnl
dnl Define HAVE_STRTOLD if "strtold" is declared and links
dnl Define HAVE_STRTOF if "strtof" is declared and links
dnl
dnl GLIBCXX_CHECK_STDLIB_SUPPORT
AC_DEFUN([GLIBCXX_CHECK_STDLIB_SUPPORT], [
  ac_test_CXXFLAGS="${CXXFLAGS+set}"
  ac_save_CXXFLAGS="$CXXFLAGS"
  CXXFLAGS='-fno-builtin -D_GNU_SOURCE'

  GLIBCXX_CHECK_STDLIB_DECL_AND_LINKAGE_2(strtold)
  GLIBCXX_CHECK_STDLIB_DECL_AND_LINKAGE_2(strtof)

  CXXFLAGS="$ac_save_CXXFLAGS"
])

dnl
dnl Check to see what the underlying c library or math library is like.
dnl These checks need to do two things:
dnl 1) make sure the name is declared when using the c++ compiler
dnl 2) make sure the name has "C" linkage
dnl This might seem like overkill but experience has shown that it's not...
dnl
dnl Define HAVE_CARGF etc if "cargf" is found.
dnl
dnl GLIBCXX_CHECK_MATH_SUPPORT
AC_DEFUN([GLIBCXX_CHECK_MATH_SUPPORT], [
  ac_test_CXXFLAGS="${CXXFLAGS+set}"
  ac_save_CXXFLAGS="$CXXFLAGS"
  CXXFLAGS='-fno-builtin -D_GNU_SOURCE'

  dnl Check libm
  AC_CHECK_LIB(m, sin, libm="-lm")
  ac_save_LIBS="$LIBS"
  LIBS="$LIBS $libm"

  dnl Check to see if certain C math functions exist.
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(isinf)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(isnan)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(finite)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(copysign)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_3(sincos)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(fpclass)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(qfpclass)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(hypot)

  dnl Check to see if basic C math functions have float versions.
  GLIBCXX_CHECK_MATH_DECLS_AND_LINKAGES_1(float trig,
                                          float_trig,
                                          acosf asinf atanf \
                                          cosf sinf tanf \
                                          coshf sinhf tanhf)
  GLIBCXX_CHECK_MATH_DECLS_AND_LINKAGES_1(float round,
                                          float_round,
                                          ceilf floorf)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(expf)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(isnanf)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(isinff)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(atan2f)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(fabsf)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(fmodf)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(frexpf)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(hypotf)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(ldexpf)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(logf)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(log10f)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(modff)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(modf)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(powf)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(sqrtf)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_3(sincosf)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(finitef)

  dnl Check to see if basic C math functions have long double versions.
  GLIBCXX_CHECK_MATH_DECLS_AND_LINKAGES_1(long double trig,
                                          long_double_trig,
                                          acosl asinl atanl \
                                          cosl sinl tanl \
                                          coshl sinhl tanhl)
  GLIBCXX_CHECK_MATH_DECLS_AND_LINKAGES_1(long double round,
                                          long_double_round,
                                          ceill floorl)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(isnanl)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(isinfl)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(copysignl)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(atan2l)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(expl)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(fabsl)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(fmodl)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(frexpl)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(hypotl)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(ldexpl)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(logl)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(log10l)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(modfl)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_2(powl)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(sqrtl)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_3(sincosl)
  GLIBCXX_CHECK_MATH_DECL_AND_LINKAGE_1(finitel)

  LIBS="$ac_save_LIBS"
  CXXFLAGS="$ac_save_CXXFLAGS"
])


dnl
dnl Check to see if there is native support for complex
dnl
dnl Don't compile bits in math/* if native support exits.
dnl
dnl Define USE_COMPLEX_LONG_DOUBLE etc if "copysignl" is found.
dnl
dnl GLIBCXX_CHECK_COMPLEX_MATH_SUPPORT
AC_DEFUN([GLIBCXX_CHECK_COMPLEX_MATH_SUPPORT], [
  dnl Check for complex versions of math functions of platform.  This will
  dnl always pass if libm is available, and fail if it isn't.  If it is
  dnl available, we assume we'll need it later, so add it to LIBS.
  AC_CHECK_LIB(m, main)
  AC_REPLACE_MATHFUNCS(copysignf)

  dnl For __signbit to signbit conversions.
  dnl Not sure why this is done, as these will be macros mostly. 
  dnl Should probably coordinate this with std_cmath.h.
  AC_CHECK_FUNCS([__signbit], , [LIBMATHOBJS="$LIBMATHOBJS signbit.lo"])

  AC_CHECK_FUNCS([__signbitf], , [LIBMATHOBJS="$LIBMATHOBJS signbitf.lo"])

  dnl Compile the long double complex functions only if the function
  dnl provides the non-complex long double functions that are needed.
  dnl Currently this includes copysignl, which should be
  dnl cached from the GLIBCXX_CHECK_MATH_SUPPORT macro, above.
  if test x$ac_cv_func_copysignl = x"yes"; then
    AC_CHECK_FUNCS([__signbitl], , [LIBMATHOBJS="$LIBMATHOBJS signbitl.lo"])
  fi

  # Used in libmath/Makefile.am.
  if test -n "$LIBMATHOBJS"; then
    need_libmath=yes
  fi
  AC_SUBST(LIBMATHOBJS)
])


# Check for functions in math library.
# Ulrich Drepper <drepper@cygnus.com>, 1998.
#
# This file can be copied and used freely without restrictions.  It can
# be used in projects which are not available under the GNU Public License
# but which still want to provide support for the GNU gettext functionality.
# Please note that the actual code is *not* freely available.
#
# serial 1
#
dnl AC_REPLACE_MATHFUNCS(FUNCTION...)
AC_DEFUN([AC_REPLACE_MATHFUNCS],
[AC_CHECK_FUNCS([$1], , [LIBMATHOBJS="$LIBMATHOBJS ${ac_func}.lo"])])
