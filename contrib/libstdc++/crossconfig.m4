dnl
dnl This file contains details for non-native builds.
dnl

AC_DEFUN([GLIBCXX_CROSSCONFIG],[
# Base decisions on target environment.
case "${host}" in
  arm*-*-symbianelf*)
    # This is a freestanding configuration; there is nothing to do here.
    ;;

  *-darwin*)
    # Darwin versions vary, but the linker should work in a cross environment,
    # so we just check for all the features here.
    # Check for available headers.
    AC_CHECK_HEADERS([nan.h ieeefp.h endian.h sys/isa_defs.h machine/endian.h \
    machine/param.h sys/machine.h fp.h locale.h float.h inttypes.h gconv.h \
    sys/types.h])

    # Don't call GLIBCXX_CHECK_LINKER_FEATURES, Darwin doesn't have a GNU ld
    GLIBCXX_CHECK_MATH_SUPPORT
    GLIBCXX_CHECK_BUILTIN_MATH_SUPPORT
    GLIBCXX_CHECK_COMPLEX_MATH_SUPPORT
    GLIBCXX_CHECK_ICONV_SUPPORT
    GLIBCXX_CHECK_STDLIB_SUPPORT

    # For showmanyc_helper().
    AC_CHECK_HEADERS(sys/ioctl.h sys/filio.h)
    GLIBCXX_CHECK_POLL
    GLIBCXX_CHECK_S_ISREG_OR_S_IFREG

    # For xsputn_2().
    AC_CHECK_HEADERS(sys/uio.h)
    GLIBCXX_CHECK_WRITEV

    AC_DEFINE(HAVE_LC_MESSAGES)

    AC_TRY_COMPILE(
      [#include <setjmp.h>],
      [sigjmp_buf env;
       while (! sigsetjmp (env, 1))
         siglongjmp (env, 1);
    ],
    [AC_DEFINE(HAVE_SIGSETJMP, 1, [Define if sigsetjmp is available.])])

    AC_DEFINE(HAVE_MMAP)
    ;;

  *djgpp)
    AC_CHECK_HEADERS([float.h ieeefp.h inttypes.h locale.h \
      memory.h stdint.h stdlib.h strings.h string.h unistd.h \
      wchar.h wctype.h machine/endian.h sys/ioctl.h sys/param.h \
      sys/resource.h sys/stat.h sys/time.h sys/types.h sys/uio.h])
    GLIBCXX_CHECK_LINKER_FEATURES
    GLIBCXX_CHECK_MATH_SUPPORT
    GLIBCXX_CHECK_BUILTIN_MATH_SUPPORT
    GLIBCXX_CHECK_COMPLEX_MATH_SUPPORT
    GLIBCXX_CHECK_ICONV_SUPPORT
    GLIBCXX_CHECK_STDLIB_SUPPORT
    GLIBCXX_CHECK_S_ISREG_OR_S_IFREG
    AC_DEFINE(HAVE_WRITEV)
    ;;

  *-freebsd*)
    AC_CHECK_HEADERS([nan.h ieeefp.h endian.h sys/isa_defs.h \
      machine/endian.h machine/param.h sys/machine.h sys/types.h \
      fp.h locale.h float.h inttypes.h sys/resource.h sys/stat.h \
      sys/time.h unistd.h])
    SECTION_FLAGS='-ffunction-sections -fdata-sections'
    AC_SUBST(SECTION_FLAGS) 
    GLIBCXX_CHECK_LINKER_FEATURES
    GLIBCXX_CHECK_COMPLEX_MATH_SUPPORT
    GLIBCXX_CHECK_ICONV_SUPPORT
    AC_DEFINE(HAVE_LC_MESSAGES)
    AC_DEFINE(HAVE_GETPAGESIZE)
    AC_DEFINE(HAVE_SETENV)
    AC_DEFINE(HAVE_SIGSETJMP)
    AC_DEFINE(HAVE_COPYSIGN)
    AC_DEFINE(HAVE_COPYSIGNF)
    AC_DEFINE(HAVE_FINITEF)
    AC_DEFINE(HAVE_FINITE)
    AC_DEFINE(HAVE_FREXPF)
    AC_DEFINE(HAVE_HYPOT)
    AC_DEFINE(HAVE_HYPOTF)
    AC_DEFINE(HAVE_ISINF)
    AC_DEFINE(HAVE_ISNAN)
    AC_DEFINE(HAVE_ISNANF)

    AC_DEFINE(HAVE_MMAP)
    AC_DEFINE(HAVE_ACOSF)
    AC_DEFINE(HAVE_ASINF)
    AC_DEFINE(HAVE_ATAN2F)
    AC_DEFINE(HAVE_ATANF)
    AC_DEFINE(HAVE_CEILF)
    AC_DEFINE(HAVE_COPYSIGN)
    AC_DEFINE(HAVE_COPYSIGNF)
    AC_DEFINE(HAVE_COSF)
    AC_DEFINE(HAVE_COSHF)
    AC_DEFINE(HAVE_EXPF)
    AC_DEFINE(HAVE_FABSF)
    AC_DEFINE(HAVE_FLOORF)
    AC_DEFINE(HAVE_FMODF)
    AC_DEFINE(HAVE_FREXPF)
    AC_DEFINE(HAVE_LDEXPF)
    AC_DEFINE(HAVE_LOG10F)
    AC_DEFINE(HAVE_LOGF)
    AC_DEFINE(HAVE_MODFF)
    AC_DEFINE(HAVE_POWF)
    AC_DEFINE(HAVE_SINF)
    AC_DEFINE(HAVE_SINHF)
    AC_DEFINE(HAVE_SQRTF)
    AC_DEFINE(HAVE_TANF)
    AC_DEFINE(HAVE_TANHF)
    if test x"long_double_math_on_this_cpu" = x"yes"; then
      AC_DEFINE(HAVE_FINITEL)
      AC_DEFINE(HAVE_ISINFL)
      AC_DEFINE(HAVE_ISNANL)
    fi
    ;;
  *-hpux*)
    AC_CHECK_HEADERS([nan.h ieeefp.h endian.h sys/isa_defs.h \
      machine/endian.h machine/param.h sys/machine.h sys/types.h \
      fp.h locale.h float.h inttypes.h])
    SECTION_FLAGS='-ffunction-sections -fdata-sections'
    AC_SUBST(SECTION_FLAGS)
    GLIBCXX_CHECK_LINKER_FEATURES
    GLIBCXX_CHECK_COMPLEX_MATH_SUPPORT
    GLIBCXX_CHECK_ICONV_SUPPORT
    AC_DEFINE(HAVE_COPYSIGN)
    AC_DEFINE(HAVE_COPYSIGNF)
    AC_DEFINE(HAVE_FREXPF)
    AC_DEFINE(HAVE_HYPOT)
    case "$target" in
      *-hpux10*)
	AC_DEFINE(HAVE_FINITE)
	AC_DEFINE(HAVE_FINITEF)
	AC_DEFINE(HAVE_ISINF)
	AC_DEFINE(HAVE_ISINFF)
	AC_DEFINE(HAVE_ISNAN)
	AC_DEFINE(HAVE_ISNANF)
	;;
    esac
    ;;
  *-linux* | *-uclinux* | *-gnu* | *-kfreebsd*-gnu | *-knetbsd*-gnu)
    AC_CHECK_HEADERS([nan.h ieeefp.h endian.h sys/isa_defs.h \
      machine/endian.h machine/param.h sys/machine.h sys/types.h \
      fp.h float.h endian.h inttypes.h locale.h float.h stdint.h])
    SECTION_FLAGS='-ffunction-sections -fdata-sections'
    AC_SUBST(SECTION_FLAGS)
    GLIBCXX_CHECK_COMPILER_FEATURES
    GLIBCXX_CHECK_LINKER_FEATURES
    GLIBCXX_CHECK_MATH_SUPPORT
    GLIBCXX_CHECK_BUILTIN_MATH_SUPPORT
    GLIBCXX_CHECK_COMPLEX_MATH_SUPPORT
    GLIBCXX_CHECK_ICONV_SUPPORT
    GLIBCXX_CHECK_STDLIB_SUPPORT

    # For LFS.
    AC_DEFINE(HAVE_INT64_T)
    GLIBCXX_CHECK_LFS

    # For showmanyc_helper().
    AC_CHECK_HEADERS(sys/ioctl.h sys/filio.h)
    GLIBCXX_CHECK_POLL
    GLIBCXX_CHECK_S_ISREG_OR_S_IFREG

    # For xsputn_2().
    AC_CHECK_HEADERS(sys/uio.h)
    GLIBCXX_CHECK_WRITEV
    ;;
  *-mingw32*)
    AC_CHECK_HEADERS([sys/types.h locale.h float.h])
    GLIBCXX_CHECK_LINKER_FEATURES
    GLIBCXX_CHECK_COMPLEX_MATH_SUPPORT
    ;;
  *-netbsd*)
    AC_CHECK_HEADERS([nan.h ieeefp.h endian.h sys/isa_defs.h \
      machine/endian.h machine/param.h sys/machine.h sys/types.h \
      fp.h locale.h float.h inttypes.h])
    SECTION_FLAGS='-ffunction-sections -fdata-sections'
    AC_SUBST(SECTION_FLAGS) 
    GLIBCXX_CHECK_LINKER_FEATURES
    GLIBCXX_CHECK_COMPLEX_MATH_SUPPORT
    GLIBCXX_CHECK_ICONV_SUPPORT
    AC_DEFINE(HAVE_COPYSIGN)
    AC_DEFINE(HAVE_COPYSIGNF)
    AC_DEFINE(HAVE_FINITEF)
    AC_DEFINE(HAVE_FINITE)
    AC_DEFINE(HAVE_FREXPF)
    AC_DEFINE(HAVE_HYPOTF)
    AC_DEFINE(HAVE_ISINF)
    AC_DEFINE(HAVE_ISINFF)
    AC_DEFINE(HAVE_ISNAN)
    AC_DEFINE(HAVE_ISNANF)
    if test x"long_double_math_on_this_cpu" = x"yes"; then
      AC_DEFINE(HAVE_FINITEL)
      AC_DEFINE(HAVE_ISINFL)
      AC_DEFINE(HAVE_ISNANL)
    fi
    ;;
  *-netware)
    AC_CHECK_HEADERS([nan.h ieeefp.h sys/isa_defs.h sys/machine.h \
      sys/types.h locale.h float.h inttypes.h])
    SECTION_FLAGS='-ffunction-sections -fdata-sections'
    AC_SUBST(SECTION_FLAGS)
    GLIBCXX_CHECK_LINKER_FEATURES
    GLIBCXX_CHECK_COMPLEX_MATH_SUPPORT
    GLIBCXX_CHECK_ICONV_SUPPORT
    AC_DEFINE(HAVE_HYPOT)
    AC_DEFINE(HAVE_ISINF)
    AC_DEFINE(HAVE_ISNAN)

    # For showmanyc_helper().
    AC_CHECK_HEADERS(sys/ioctl.h sys/filio.h)
    GLIBCXX_CHECK_POLL
    GLIBCXX_CHECK_S_ISREG_OR_S_IFREG

    # For xsputn_2().
    AC_CHECK_HEADERS(sys/uio.h)
    GLIBCXX_CHECK_WRITEV
    ;;
  *-qnx6.1* | *-qnx6.2*)
    SECTION_FLAGS='-ffunction-sections -fdata-sections'
    AC_SUBST(SECTION_FLAGS) 
    GLIBCXX_CHECK_LINKER_FEATURES
    GLIBCXX_CHECK_COMPLEX_MATH_SUPPORT
    GLIBCXX_CHECK_ICONV_SUPPORT
    AC_DEFINE(HAVE_COSF)
    AC_DEFINE(HAVE_COSL)
    AC_DEFINE(HAVE_COSHF)
    AC_DEFINE(HAVE_COSHL)
    AC_DEFINE(HAVE_LOGF)
    AC_DEFINE(HAVE_LOGL)
    AC_DEFINE(HAVE_LOG10F)
    AC_DEFINE(HAVE_LOG10L)
    AC_DEFINE(HAVE_SINF)
    AC_DEFINE(HAVE_SINL)
    AC_DEFINE(HAVE_SINHF)
    AC_DEFINE(HAVE_SINHL)
    ;;
  *-solaris*)
    case "$target" in
    #  *-solaris2.5)
    #    os_include_dir="os/solaris/solaris2.5"
    #    ;;
    #  *-solaris2.6)
    #    os_include_dir="os/solaris/solaris2.6"
    #    ;;
      *-solaris2.7 | *-solaris2.8 | *-solaris2.9 | *-solaris2.10)
         GLIBCXX_CHECK_LINKER_FEATURES
         AC_DEFINE(HAVE_GETPAGESIZE)
         AC_DEFINE(HAVE_SIGSETJMP)
         AC_DEFINE(HAVE_MBSTATE_T)
         AC_DEFINE(HAVE_POLL)
         AC_DEFINE(HAVE_S_ISREG)
         AC_DEFINE(HAVE_LC_MESSAGES)
         AC_DEFINE(HAVE_FINITE)
         AC_DEFINE(HAVE_FPCLASS)
         AC_DEFINE(HAVE_GETPAGESIZE)
         AC_DEFINE(HAVE_NL_LANGINFO)
         AC_DEFINE(HAVE_ICONV)
         AC_DEFINE(HAVE_ICONV_CLOSE)
         AC_DEFINE(HAVE_ICONV_OPEN)
         # Look for the pieces required for wchar_t support in order to
         # get all the right HAVE_* macros defined.
         GLIBCXX_CHECK_ICONV_SUPPORT
         # All of the dependencies for wide character support are here, so
         # turn it on.  This requires some syncronization with the
         # GLIBCXX_CHECK_ICONV_SUPPORT in acinclude.m4
         AC_DEFINE(_GLIBCXX_USE_WCHAR_T) 
         # Are these tested for even when cross?
         AC_DEFINE(HAVE_FLOAT_H)
         AC_DEFINE(HAVE_IEEEFP_H)
         AC_DEFINE(HAVE_INTTYPES_H)
         AC_DEFINE(HAVE_LOCALE_H)
         AC_DEFINE(HAVE_NAN_H)
         AC_DEFINE(HAVE_SYS_FILIO_H)
         AC_DEFINE(HAVE_SYS_IOCTL_H)
         AC_DEFINE(HAVE_SYS_ISA_DEFS_H)
         AC_DEFINE(HAVE_SYS_RESOURCE_H)
         AC_DEFINE(HAVE_SYS_TIME_H)
         AC_DEFINE(HAVE_SYS_TYPES_H)
         AC_DEFINE(HAVE_UNISTD_H)
         AC_DEFINE(HAVE_WCHAR_H)
         AC_DEFINE(HAVE_WCTYPE_H)
         AC_DEFINE(HAVE_LIBM)
        ;;
    esac
    case "$target" in
      sparc*-*-solaris2.8 | sparc*-*-solaris2.9 | sparc*-*-solaris2.10)
        # I saw these on sparc-sun-solaris2.8, but not 2.6, and not on i386
        AC_DEFINE(HAVE___BUILTIN_ABS)
        AC_DEFINE(HAVE___BUILTIN_LABS)
        AC_DEFINE(HAVE___BUILTIN_FABS)
        AC_DEFINE(HAVE___BUILTIN_FABSF)
        AC_DEFINE(HAVE___BUILTIN_FABSL)
        AC_DEFINE(HAVE___BUILTIN_COS)
        AC_DEFINE(HAVE___BUILTIN_COSF)
        AC_DEFINE(HAVE___BUILTIN_SIN)
        AC_DEFINE(HAVE___BUILTIN_SINF)
       ;;
    esac
    case "$target" in
      *-*-solaris2.10)
      # These two C99 functions are present only in Solaris >= 10
      AC_DEFINE(HAVE_STRTOF)
      AC_DEFINE(HAVE_STRTOLD)
     ;;
    esac
    AC_DEFINE(HAVE_MMAP) 
    AC_DEFINE(HAVE_COPYSIGN)
    AC_DEFINE(HAVE_ISNAN)
    AC_DEFINE(HAVE_ISNANF)
    AC_DEFINE(HAVE_MODFF)
    AC_DEFINE(HAVE_HYPOT)
    ;;
  *-tpf)
    AC_CHECK_HEADERS([nan.h endian.h machine/endian.h  \
      sys/param.h sys/types.h locale.h float.h inttypes.h])
    SECTION_FLAGS='-ffunction-sections -fdata-sections'
    AC_SUBST(SECTION_FLAGS)
    GLIBCXX_CHECK_LINKER_FEATURES
    GLIBCXX_CHECK_COMPLEX_MATH_SUPPORT
    GLIBCXX_CHECK_ICONV_SUPPORT
    AC_DEFINE(HAVE_COPYSIGN)
    AC_DEFINE(HAVE_COPYSIGNF)
    AC_DEFINE(HAVE_FINITE)
    AC_DEFINE(HAVE_FINITEF)
    AC_DEFINE(HAVE_FREXPF)
    AC_DEFINE(HAVE_HYPOTF)
    AC_DEFINE(HAVE_ISINF)
    AC_DEFINE(HAVE_ISINFF)
    AC_DEFINE(HAVE_ISNAN)
    AC_DEFINE(HAVE_ISNANF)
    AC_DEFINE(HAVE_SINCOS)
    AC_DEFINE(HAVE_SINCOSF)
    if test x"long_double_math_on_this_cpu" = x"yes"; then
      AC_DEFINE(HAVE_FINITEL)
      AC_DEFINE(HAVE_HYPOTL)
      AC_DEFINE(HAVE_ISINFL)
      AC_DEFINE(HAVE_ISNANL)
    fi
    ;;
  *-vxworks)
    AC_DEFINE(HAVE_MMAP)
    AC_DEFINE(HAVE_ACOSF)
    AC_DEFINE(HAVE_ASINF)
    AC_DEFINE(HAVE_ATAN2F)
    AC_DEFINE(HAVE_ATANF)
    AC_DEFINE(HAVE_CEILF)
    AC_DEFINE(HAVE_COSF)
    AC_DEFINE(HAVE_COSHF)
    AC_DEFINE(HAVE_EXPF)
    AC_DEFINE(HAVE_FABSF)
    AC_DEFINE(HAVE_FLOORF)
    AC_DEFINE(HAVE_FMODF)
    AC_DEFINE(HAVE_HYPOT)
    AC_DEFINE(HAVE_LOG10F)
    AC_DEFINE(HAVE_LOGF)
    AC_DEFINE(HAVE_POWF)
    AC_DEFINE(HAVE_SINF)
    AC_DEFINE(HAVE_SINHF)
    AC_DEFINE(HAVE_SQRTF)
    AC_DEFINE(HAVE_TANF)
    AC_DEFINE(HAVE_TANHF)
    ;;
  *-windiss*)
    AC_DEFINE(HAVE_ACOSF)
    AC_DEFINE(HAVE_ACOSL)
    AC_DEFINE(HAVE_ASINF)
    AC_DEFINE(HAVE_ASINL)
    AC_DEFINE(HAVE_ATAN2F)
    AC_DEFINE(HAVE_ATAN2L)
    AC_DEFINE(HAVE_ATANF)
    AC_DEFINE(HAVE_ATANL)
    AC_DEFINE(HAVE_CEILF)
    AC_DEFINE(HAVE_CEILL)
    AC_DEFINE(HAVE_COPYSIGN)
    AC_DEFINE(HAVE_COPYSIGNF)
    AC_DEFINE(HAVE_COSF)
    AC_DEFINE(HAVE_COSL)
    AC_DEFINE(HAVE_COSHF)
    AC_DEFINE(HAVE_COSHL)
    AC_DEFINE(HAVE_EXPF)
    AC_DEFINE(HAVE_EXPL)
    AC_DEFINE(HAVE_FABSF)
    AC_DEFINE(HAVE_FABSL)
    AC_DEFINE(HAVE_FLOORF)
    AC_DEFINE(HAVE_FLOORL)
    AC_DEFINE(HAVE_FMODF)
    AC_DEFINE(HAVE_FMODL)
    AC_DEFINE(HAVE_FREXPF)
    AC_DEFINE(HAVE_FREXPL)
    AC_DEFINE(HAVE_LDEXPF)
    AC_DEFINE(HAVE_LDEXPL)
    AC_DEFINE(HAVE_LOG10F)
    AC_DEFINE(HAVE_LOG10L)
    AC_DEFINE(HAVE_LOGF)
    AC_DEFINE(HAVE_MODFF)
    AC_DEFINE(HAVE_MODFL)
    AC_DEFINE(HAVE_POWF)
    AC_DEFINE(HAVE_POWL)
    AC_DEFINE(HAVE_SINF)
    AC_DEFINE(HAVE_SINL)
    AC_DEFINE(HAVE_SINHF)
    AC_DEFINE(HAVE_SINHL)
    AC_DEFINE(HAVE_SQRTF)
    AC_DEFINE(HAVE_SQRTL)
    AC_DEFINE(HAVE_TANF)
    AC_DEFINE(HAVE_TANL)
    AC_DEFINE(HAVE_TANHF)
    AC_DEFINE(HAVE_TANHL)
    ;;
  *)
    AC_MSG_ERROR([No support for this host/target combination.])
   ;;
esac
])
