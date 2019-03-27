dnl autoconf configuration for gperf/src

dnl Copyright (C) 1998, 2000, 2002-2003, 2007 Free Software Foundation, Inc.
dnl Written by Douglas C. Schmidt <schmidt@ics.uci.edu>
dnl and Bruno Haible <bruno@clisp.org>.
dnl
dnl This file is part of GNU GPERF.
dnl
dnl GNU GPERF is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 2, or (at your option)
dnl any later version.
dnl
dnl GNU GPERF is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this program; see the file COPYING.
dnl If not, write to the Free Software Foundation, Inc.,
dnl 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

AC_PREREQ([2.60])
AC_INIT(main.cc)
AC_CONFIG_HEADER(config.h)
AC_PROG_MAKE_SET
dnl
dnl           checks for programs
dnl
AC_PROG_CC
                      dnl sets variable CC
AC_PROG_CPP
                      dnl sets variable CPP
AC_PROG_CXX
                      dnl sets variable CXX
AC_PROG_CXXCPP
                      dnl sets variable CXXCPP
CL_PROG_INSTALL
                      dnl sets variables INSTALL, INSTALL_DATA, INSTALL_PROGRAM
dnl
dnl           checks for compiler characteristics
dnl
AC_MSG_CHECKING([for stack-allocated variable-size arrays])
AC_CACHE_VAL(gp_cv_cxx_dynamic_array, [
AC_LANG_SAVE()
AC_LANG_CPLUSPLUS()
AC_TRY_COMPILE([int func (int n) { int dynamic_array[n]; }], [],
gp_cv_cxx_dynamic_array=yes, gp_cv_cxx_dynamic_array=no)
AC_LANG_RESTORE()
])
AC_MSG_RESULT($gp_cv_cxx_dynamic_array)
if test $gp_cv_cxx_dynamic_array = yes; then
  AC_DEFINE(HAVE_DYNAMIC_ARRAY, 1,
    [Define if the C++ compiler supports stack-allocated variable-size arrays.])
fi
dnl
dnl           checks for functions and declarations
dnl
dnl
dnl           checks for libraries
dnl
AC_CHECK_LIB(m, rand, GPERF_LIBM="-lm", GPERF_LIBM="")
AC_SUBST(GPERF_LIBM)
dnl
dnl           That's it.
dnl
AC_OUTPUT(Makefile)
