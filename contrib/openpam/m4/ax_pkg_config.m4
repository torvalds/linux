dnl -*- autoconf -*-
dnl
dnl Copyright (c) 2017 The University of Oslo
dnl All rights reserved.
dnl
dnl Redistribution and use in source and binary forms, with or without
dnl modification, are permitted provided that the following conditions
dnl are met:
dnl 1. Redistributions of source code must retain the above copyright
dnl    notice, this list of conditions and the following disclaimer.
dnl 2. Redistributions in binary form must reproduce the above copyright
dnl    notice, this list of conditions and the following disclaimer in the
dnl    documentation and/or other materials provided with the distribution.
dnl 3. The name of the author may not be used to endorse or promote
dnl    products derived from this software without specific prior written
dnl    permission.
dnl
dnl THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
dnl ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
dnl IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
dnl ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
dnl FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
dnl DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
dnl OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
dnl HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
dnl LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
dnl OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
dnl SUCH DAMAGE.
dnl

m4_define([AX_PKG_CONFIG_MACROS_VERSION], [0.20170404])

dnl
dnl AX_PROG_PKG_CONFIG([min-version])
dnl ---------------------------------
dnl
dnl Verify that pkgconf or pkg-config are present.
dnl
AC_DEFUN([AX_PROG_PKG_CONFIG], [
    m4_pattern_forbid([^AX_PKG_CONFIG_[A-Z_]+$])
    AC_ARG_VAR([PKG_CONFIG], [path to pkg-config binary])
    AC_ARG_VAR([PKG_CONFIG_PATH], [list of directories to prepend to default search path])
    AC_ARG_VAR([PKG_CONFIG_LIBDIR], [list of directories to search instead of default search path])
    if test x"${PKG_CONFIG}" = x"" ; then
        AC_PATH_PROGS([PKG_CONFIG], [pkgconf pkg-config]) >/dev/null
    else
        AC_PATH_PROG([PKG_CONFIG], [${PKG_CONFIG}])
    fi
    AC_MSG_CHECKING([for pkg-config or pkgconf])
    if test -x "${PKG_CONFIG}" ; then
        AC_MSG_RESULT([${PKG_CONFIG}])
        case "${PKG_CONFIG}" in
        *pkgconf)
            _min_version="m4_default([$1], [1.3.0])"
            ;;
        *pkg-config)
            _min_version="m4_default([$1], [0.23])"
            ;;
        *)
            _min_version="9.9.error"
            ;;
        esac
        AC_MSG_CHECKING([that ${PKG_CONFIG} is at least version ${_min_version}])
        _act_version=`"${PKG_CONFIG}" --version`
        if ! "${PKG_CONFIG}" --atleast-pkgconfig-version="${_min_version}" ; then
            PKG_CONFIG=""
        fi
        AC_MSG_RESULT([${_act_version}])
    else
        AC_MSG_RESULT([no])
        PKG_CONFIG=""
    fi
    if test x"${PKG_CONFIG}" = x"" ; then
        AC_MSG_ERROR([pkg-config was not found or is too old])
    fi
    AC_ARG_WITH([pkgconfigdir],
        AS_HELP_STRING([--with-pkgconfigdir],
	    [installation directory for .pc files @<:@LIBDIR/pkgconfig@:>@]),
	[pkgconfigdir=$withval], [pkgconfigdir='${libdir}/pkgconfig'])
    AC_SUBST([pkgconfigdir], [$pkgconfigdir])
])

dnl
dnl AX_PKG_CONFIG_VAR(package-name, var-name)
dnl -----------------------------------------
dnl
dnl Retrieve specific pkg-config variables for the specified package.
dnl
AC_DEFUN([AX_PKG_CONFIG_VAR], [
    AC_REQUIRE([AX_PROG_PKG_CONFIG])
    m4_define([_p], AS_TR_SH([m4_tolower([$1])]))
    m4_case([$2],
        [version], [ax_pc_cv_[]_p[]_version=`"${PKG_CONFIG}" --modversion [$1]`],
        [cflags], [ax_pc_cv_[]_p[]_cflags=`"${PKG_CONFIG}" --cflags [$1]`],
        [libs], [ax_pc_cv_[]_p[]_libs=`"${PKG_CONFIG}" --libs [$1]`],
        [ax_pc_cv_[]_p[]_[$2]=`"${PKG_CONFIG}" --variable=[$2] [$1]`])
])

dnl
dnl AX_PKG_CONFIG_CHECK(package-name,
dnl     [action-if-found], [action-if-not-found])
dnl -------------------------------------------
dnl
dnl Check if the specified package is installed.  If it is, define
dnl HAVE_PACKAGE, PACKAGE_VERSION, PACKAGE_CFLAGS and PACKAGE_LIBS.
dnl The specified actions are performed in addition to the standard
dnl actions.
dnl
AC_DEFUN([AX_PKG_CONFIG_CHECK], [
    AC_REQUIRE([AX_PROG_PKG_CONFIG])
    m4_define([_P], AS_TR_SH([m4_toupper([$1])]))
    m4_define([_p], AS_TR_SH([m4_tolower([$1])]))
    AC_ARG_VAR(_P[_CFLAGS], [C compiler flags for $1])
    AC_ARG_VAR(_P[_LIBS], [linker flags for $1])
    AC_MSG_CHECKING([if $1 is installed])
    if AC_RUN_LOG(["${PKG_CONFIG}" --exists --print-errors "$1"]) ; then
        AC_MSG_RESULT([yes])
        [ax_pc_cv_have_]_p=yes
        AC_DEFINE([HAVE_]_P, [1], [Define to 1 if you have $1])
dnl
        AC_MSG_CHECKING([$1 version])
        AX_PKG_CONFIG_VAR([$1], [version])
        AC_SUBST(_P[_VERSION], [$ax_pc_cv_]_p[_version])
        AC_MSG_RESULT([${ax_pc_cv_]_p[_version:-unknown}])
dnl
        AC_MSG_CHECKING([$1 compiler flags])
        AX_PKG_CONFIG_VAR([$1], [cflags])
        AC_SUBST(_P[_CFLAGS], [$ax_pc_cv_]_p[_cflags])
        AC_MSG_RESULT([${ax_pc_cv_]_p[_cflags:-none}])
dnl
        AC_MSG_CHECKING([$1 linker flags])
        AX_PKG_CONFIG_VAR([$1], [libs])
        AC_SUBST(_P[_LIBS], [$ax_pc_cv_]_p[_libs])
        AC_MSG_RESULT([${ax_pc_cv_]_p[_libs:-none}])
dnl
        m4_default([$2], [:])
    else
        AC_MSG_RESULT([no])
        [ax_pc_cv_have_]_p=no
        m4_default([$3], [:])
    fi
    m4_ifdef([AM_CONDITIONAL], [
        AM_CONDITIONAL([HAVE_]_P, [test x"$ax_pc_cv_have_]_p[" = x"yes"])
    ])
])

dnl
dnl AX_PKG_CONFIG_REQUIRE(package-name)
dnl -----------------------------------
dnl
dnl As above, but fail if the package is not installed.
dnl
AC_DEFUN([AX_PKG_CONFIG_REQUIRE], [
    AX_PKG_CONFIG_CHECK([$1], [], [
        AC_MSG_ERROR([cannot proceed without $1])
    ])
])
