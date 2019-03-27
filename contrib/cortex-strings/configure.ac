# Copyright (c) 2011-2012, Linaro Limited
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of the Linaro nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

AC_INIT(cortex-strings, 1.1-2012.06~dev)
AM_INIT_AUTOMAKE(foreign subdir-objects color-tests dist-bzip2)
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES(Makefile)
AC_CANONICAL_HOST
AM_PROG_AS
AC_PROG_CC
AC_PROG_LIBTOOL

default_submachine=

case $host in
aarch64*-*-*)
  arch=aarch64
  ;;
arm*-*-*)
  arch=aarch32
  default_submachine=cortex-a9
  ;;
x86_64-*-*-*)
  arch=generic
  ;;
*)
  AC_MSG_ERROR([unknown architecture $host])
  ;;
esac

AM_CONDITIONAL([HOST_AARCH32], [test x$arch = xaarch32])
AM_CONDITIONAL([HOST_AARCH64], [test x$arch = xaarch64])
AM_CONDITIONAL([HOST_GENERIC], [test x$arch = xgeneric])

AC_ARG_WITH([cpu],
	    AS_HELP_STRING([--with-cpu=CPU],
                           [select code for CPU variant @<:@default=cortex-a9@:>@]]),
	    [dnl
  case "$withval" in
  yes|'') AC_MSG_ERROR([--with-cpu requires an argument]) ;;
  no) ;;
  *) submachine="$withval" ;;
  esac
],
[submachine=$default_submachine])

AC_SUBST(submachine)
AM_CONDITIONAL([WITH_SUBMACHINE], [test x$submachine != x])

AC_ARG_WITH([neon],
            AC_HELP_STRING([--with-neon],
                           [include NEON specific routines @<:@default=yes@:>@]),
	    [with_neon=$withval],
	    [with_neon=yes])
AC_SUBST(with_neon)
AM_CONDITIONAL(WITH_NEON, test x$with_neon = xyes)

AC_ARG_WITH([vfp],
            AC_HELP_STRING([--with-vfp],
                           [include VFP specific routines @<:@default=yes@:>@]),
	    [with_vfp=$withval],
	    [with_vfp=yes])
AC_SUBST(with_vfp)
AM_CONDITIONAL(WITH_VFP, test x$with_vfp = xyes)

AC_OUTPUT
