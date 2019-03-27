# SYNOPSIS
#
#   Add code coverage support with gcov/lcov.
#
#   AX_CODE_COVERAGE()
#
# DESCRIPTION
#
#   Provides a --enable-coverage option which checks for available
#   gcov/lcov binaries and provides ENABLE_CODE_COVERAGE conditional.
#
# LAST MODIFICATION
#
#   $Id: coverage.m4 40881 2013-08-20 17:54:39Z damon $
#
# COPYLEFT
#
#   Copyright (c) 2012 Roy H. Stogner <roystgnr@ices.utexas.edu>
#   Copyright (c) 2010 Karl W. Schulz <karl@ices.utexas.edu>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.

AC_DEFUN([AX_CODE_COVERAGE],
[

AC_ARG_ENABLE(coverage, AC_HELP_STRING([--enable-coverage],[configure code coverage analysis tools]))

HAVE_GCOV_TOOLS=0

GCOV_FLAGS=""

if test "x$enable_coverage" = "xyes"; then

   # ----------------------------
   # Check for gcov/lcov binaries
   # ----------------------------

   AC_ARG_VAR([GCOV], [Coverage testing command])
   if test "x$GCOV" = "x"; then
    AC_PATH_PROG(GCOV, gcov, no)
   else
    AC_PATH_PROG(GCOV, $GCOV, no)
   fi

   AC_PATH_PROG(LCOV, lcov, no)
   AC_PATH_PROG(GENHTML, genhtml)

   # ----------------------------------
   # include coverage compiler options
   # ----------------------------------
   AC_MSG_CHECKING([for clang])

   AC_COMPILE_IFELSE(
   [AC_LANG_PROGRAM([], [[
 #ifndef __clang__
   not clang
 #endif
 ]])],
 [CLANG=yes], [CLANG=no])

   AC_MSG_RESULT([$CLANG])
   HAVE_GCOV_TOOLS=1
   COVERAGE_CFLAGS="-fprofile-arcs -ftest-coverage"
   COVERAGE_LDFLAGS="--coverage -fprofile-arcs -ftest-coverage"
   COVERAGE_OPTFLAGS="-O0"

   # Test for C...
   CFLAGS="${GCOV_FLAGS} ${CFLAGS}"
   CXXFLAGS="${GCOV_FLAGS} ${CXXFLAGS}"
   if test "x$GCC" = "xyes" -a "x$CLANG" = "xno"; then
     COVERAGE_LIBS="-lgcov"
   else
     COVERAGE_LIBS=""
   fi
fi

AC_SUBST([GCOV])
AC_SUBST([LCOV])
AC_SUBST([GENHTML])
AC_SUBST([GENHTML_OPTIONS])
AC_SUBST([COVERAGE_CFLAGS])
AC_SUBST([COVERAGE_OPTFLAGS])
AC_SUBST([COVERAGE_LDFLAGS])
AC_SUBST([COVERAGE_LIBS])
AM_CONDITIONAL(CODE_COVERAGE_ENABLED,test x$HAVE_GCOV_TOOLS = x1)

])
