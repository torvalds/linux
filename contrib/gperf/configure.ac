dnl autoconf configuration for gperf

dnl Copyright (C) 1998, 2002, 2003, 2005-2007 Free Software Foundation, Inc.
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
AC_INIT(doc/gperf.1)
AC_PROG_MAKE_SET
AC_OBJEXT
AC_EXEEXT

dnl An autoconf-2.52 bug: AC_CONFIG_SUBDIRS requires AC_CONFIG_AUX_DIR_DEFAULT.
ac_aux_dir=$srcdir
AC_PROVIDE([AC_CONFIG_AUX_DIR_DEFAULT])
AC_CONFIG_SUBDIRS(lib src tests doc)

dnl This piece of sed script replaces every line containing '@subdir@'
dnl by several consecutive lines, each referencing one subdir.
extrasub="$extrasub"'
/@subdir@/{
h
g
s/@subdir@/lib/
p
g
s/@subdir@/src/
p
g
s/@subdir@/tests/
p
g
s/@subdir@/doc/
p
d
}
'
AC_OUTPUT(Makefile)
