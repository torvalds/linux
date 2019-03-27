# configure.ac for GCC
# Process this file with autoconf to generate a configuration script.

# Copyright 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
# Free Software Foundation, Inc.

#This file is part of GCC.

#GCC is free software; you can redistribute it and/or modify it under
#the terms of the GNU General Public License as published by the Free
#Software Foundation; either version 2, or (at your option) any later
#version.

#GCC is distributed in the hope that it will be useful, but WITHOUT
#ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
#for more details.

#You should have received a copy of the GNU General Public License
#along with GCC; see the file COPYING.  If not, write to the Free
#Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
#02110-1301, USA.

# --------------------------------
# Initialization and sanity checks
# --------------------------------

AC_PREREQ(2.59)
AC_INIT
AC_CONFIG_SRCDIR(tree.c)
AC_CONFIG_HEADER(auto-host.h:config.in)

gcc_version=`cat $srcdir/BASE-VER`

# Determine the host, build, and target systems
AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

# Determine the noncanonical target name, for directory use.
ACX_NONCANONICAL_TARGET

# Determine the target- and build-specific subdirectories
GCC_TOPLEV_SUBDIRS

# Set program_transform_name
AC_ARG_PROGRAM

# Check for bogus environment variables.
# Test if LIBRARY_PATH contains the notation for the current directory
# since this would lead to problems installing/building glibc.
# LIBRARY_PATH contains the current directory if one of the following
# is true:
# - one of the terminals (":" and ";") is the first or last sign
# - two terminals occur directly after each other
# - the path contains an element with a dot in it
AC_MSG_CHECKING(LIBRARY_PATH variable)
changequote(,)dnl
case ${LIBRARY_PATH} in
  [:\;]* | *[:\;] | *[:\;][:\;]* |  *[:\;]. | .[:\;]*| . | *[:\;].[:\;]* )
    library_path_setting="contains current directory"
    ;;
  *)
    library_path_setting="ok"
    ;;
esac
changequote([,])dnl
AC_MSG_RESULT($library_path_setting)
if test "$library_path_setting" != "ok"; then
AC_MSG_ERROR([
*** LIBRARY_PATH shouldn't contain the current directory when
*** building gcc. Please change the environment variable
*** and run configure again.])
fi

# Test if GCC_EXEC_PREFIX contains the notation for the current directory
# since this would lead to problems installing/building glibc.
# GCC_EXEC_PREFIX contains the current directory if one of the following
# is true:
# - one of the terminals (":" and ";") is the first or last sign
# - two terminals occur directly after each other
# - the path contains an element with a dot in it
AC_MSG_CHECKING(GCC_EXEC_PREFIX variable)
changequote(,)dnl
case ${GCC_EXEC_PREFIX} in
  [:\;]* | *[:\;] | *[:\;][:\;]* |  *[:\;]. | .[:\;]*| . | *[:\;].[:\;]* )
    gcc_exec_prefix_setting="contains current directory"
    ;;
  *)
    gcc_exec_prefix_setting="ok"
    ;;
esac
changequote([,])dnl
AC_MSG_RESULT($gcc_exec_prefix_setting)
if test "$gcc_exec_prefix_setting" != "ok"; then
AC_MSG_ERROR([
*** GCC_EXEC_PREFIX shouldn't contain the current directory when
*** building gcc. Please change the environment variable
*** and run configure again.])
fi

# -----------
# Directories
# -----------

# Specify the local prefix
local_prefix=
AC_ARG_WITH(local-prefix,
[  --with-local-prefix=DIR specifies directory to put local include],
[case "${withval}" in
yes)	AC_MSG_ERROR(bad value ${withval} given for local include directory prefix) ;;
no)	;;
*)	local_prefix=$with_local_prefix ;;
esac])

# Default local prefix if it is empty
if test x$local_prefix = x; then
	local_prefix=/usr/local
fi

# Don't set gcc_gxx_include_dir to gxx_include_dir since that's only
# passed in by the toplevel make and thus we'd get different behavior
# depending on where we built the sources.
gcc_gxx_include_dir=
# Specify the g++ header file directory
AC_ARG_WITH(gxx-include-dir,
[  --with-gxx-include-dir=DIR
                          specifies directory to put g++ header files],
[case "${withval}" in
yes)	AC_MSG_ERROR(bad value ${withval} given for g++ include directory) ;;
no)	;;
*)	gcc_gxx_include_dir=$with_gxx_include_dir ;;
esac])

# This logic must match libstdc++-v3/acinclude.m4:GLIBCXX_EXPORT_INSTALL_INFO.
if test x${gcc_gxx_include_dir} = x; then
  if test x${enable_version_specific_runtime_libs} = xyes; then
    gcc_gxx_include_dir='${libsubdir}/include/c++'
  else
    libstdcxx_incdir='include/c++/$(version)'
    if test x$host != x$target; then
       libstdcxx_incdir="$target_alias/$libstdcxx_incdir"
    fi
changequote(<<, >>)dnl
    gcc_gxx_include_dir="\$(libsubdir)/\$(unlibsubdir)/..\`echo \$(exec_prefix) | sed -e 's|^\$(prefix)||' -e 's|/[^/]*|/..|g'\`/$libstdcxx_incdir"
changequote([, ])dnl
  fi
fi

AC_ARG_WITH(cpp_install_dir,
[  --with-cpp-install-dir=DIR
                          install the user visible C preprocessor in DIR
                          (relative to PREFIX) as well as PREFIX/bin],
[if test x$withval = xyes; then
  AC_MSG_ERROR([option --with-cpp-install-dir requires an argument])
elif test x$withval != xno; then
  cpp_install_dir=$withval
fi])

# We would like to our source tree to be readonly.  However when releases or
# pre-releases are generated, the flex/bison generated files as well as the 
# various formats of manuals need to be included along with the rest of the
# sources.  Therefore we have --enable-generated-files-in-srcdir to do 
# just that.

AC_MSG_CHECKING([whether to place generated files in the source directory])
  dnl generated-files-in-srcdir is disabled by default
  AC_ARG_ENABLE(generated-files-in-srcdir, 
[  --enable-generated-files-in-srcdir
                          put copies of generated files in source dir
                          intended for creating source tarballs for users
                          without texinfo bison or flex.],
      generated_files_in_srcdir=$enableval,
      generated_files_in_srcdir=no)

AC_MSG_RESULT($generated_files_in_srcdir)

if test "$generated_files_in_srcdir" = "yes"; then
  GENINSRC=''
else
  GENINSRC='#'
fi
AC_SUBST(GENINSRC)

# -------------------
# Find default linker
# -------------------

# With GNU ld
AC_ARG_WITH(gnu-ld,
[  --with-gnu-ld           arrange to work with GNU ld.],
gnu_ld_flag="$with_gnu_ld",
gnu_ld_flag=no)

# With pre-defined ld
AC_ARG_WITH(ld,
[  --with-ld               arrange to use the specified ld (full pathname)],
DEFAULT_LINKER="$with_ld")
if test x"${DEFAULT_LINKER+set}" = x"set"; then
  if test ! -x "$DEFAULT_LINKER"; then
    AC_MSG_ERROR([cannot execute: $DEFAULT_LINKER: check --with-ld or env. var. DEFAULT_LINKER])
  elif $DEFAULT_LINKER -v < /dev/null 2>&1 | grep GNU > /dev/null; then
    gnu_ld_flag=yes
  fi
  AC_DEFINE_UNQUOTED(DEFAULT_LINKER,"$DEFAULT_LINKER",
	[Define to enable the use of a default linker.])
fi

AC_MSG_CHECKING([whether a default linker was specified])
if test x"${DEFAULT_LINKER+set}" = x"set"; then
  if test x"$gnu_ld_flag" = x"no"; then
    AC_MSG_RESULT([yes ($DEFAULT_LINKER)])
  else
    AC_MSG_RESULT([yes ($DEFAULT_LINKER - GNU ld)])
  fi
else
  AC_MSG_RESULT(no)
fi

# With demangler in GNU ld
AC_ARG_WITH(demangler-in-ld,
[  --with-demangler-in-ld  try to use demangler in GNU ld.],
demangler_in_ld="$with_demangler_in_ld",
demangler_in_ld=no)

# ----------------------
# Find default assembler
# ----------------------

# With GNU as
AC_ARG_WITH(gnu-as,
[  --with-gnu-as           arrange to work with GNU as],
gas_flag="$with_gnu_as",
gas_flag=no)

AC_ARG_WITH(as,
[  --with-as               arrange to use the specified as (full pathname)],
DEFAULT_ASSEMBLER="$with_as")
if test x"${DEFAULT_ASSEMBLER+set}" = x"set"; then
  if test ! -x "$DEFAULT_ASSEMBLER"; then
    AC_MSG_ERROR([cannot execute: $DEFAULT_ASSEMBLER: check --with-as or env. var. DEFAULT_ASSEMBLER])
  elif $DEFAULT_ASSEMBLER -v < /dev/null 2>&1 | grep GNU > /dev/null; then
    gas_flag=yes
  fi
  AC_DEFINE_UNQUOTED(DEFAULT_ASSEMBLER,"$DEFAULT_ASSEMBLER",
	[Define to enable the use of a default assembler.])
fi

AC_MSG_CHECKING([whether a default assembler was specified])
if test x"${DEFAULT_ASSEMBLER+set}" = x"set"; then
  if test x"$gas_flag" = x"no"; then
    AC_MSG_RESULT([yes ($DEFAULT_ASSEMBLER)])
  else
    AC_MSG_RESULT([yes ($DEFAULT_ASSEMBLER - GNU as)])
  fi
else
  AC_MSG_RESULT(no)
fi

# ---------------
# Find C compiler
# ---------------

# If a non-executable a.out is present (e.g. created by GNU as above even if
# invoked with -v only), the IRIX 6 native ld just overwrites the existing
# file, even when creating an executable, so an execution test fails.
# Remove possible default executable files to avoid this.
#
# FIXME: This really belongs into AC_PROG_CC and can be removed once
# Autoconf includes it.
rm -f a.out a.exe b.out

# Find the native compiler
AC_PROG_CC
AM_PROG_CC_C_O
# autoconf is lame and doesn't give us any substitution variable for this.
if eval "test \"`echo '$ac_cv_prog_cc_'${ac_cc}_c_o`\" = no"; then
  NO_MINUS_C_MINUS_O=yes
else
  OUTPUT_OPTION='-o $@'
fi
AC_SUBST(NO_MINUS_C_MINUS_O)
AC_SUBST(OUTPUT_OPTION)

# Remove the -O2: for historical reasons, unless bootstrapping we prefer
# optimizations to be activated explicitly by the toplevel.
case "$CC" in
  */prev-gcc/xgcc*) ;;
  *) CFLAGS=`echo $CFLAGS | sed "s/-O[[s0-9]]* *//" ` ;;
esac
AC_SUBST(CFLAGS)

# -------------------------
# Check C compiler features
# -------------------------

AC_PROG_CPP
AC_C_INLINE

# sizeof(char) is 1 by definition.
AC_CHECK_SIZEOF(void *)
AC_CHECK_SIZEOF(short)
AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(long)
AC_CHECK_TYPES([long long], [AC_CHECK_SIZEOF(long long)])
AC_CHECK_TYPES([__int64], [AC_CHECK_SIZEOF(__int64)])

# ---------------------
# Warnings and checking
# ---------------------

# Check $CC warning features (if it's GCC).
# We want to use -pedantic, but we don't want warnings about
# * 'long long'
# * variadic macros
# * overlong strings
# So, we only use -pedantic if we can disable those warnings.

AC_CACHE_CHECK(
  [whether ${CC} accepts -Wno-long-long],
  [ac_cv_prog_cc_w_no_long_long],
  [save_CFLAGS="$CFLAGS"
  CFLAGS="-Wno-long-long"
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([[]])],
                    [ac_cv_prog_cc_w_no_long_long=yes],
                    [ac_cv_prog_cc_w_no_long_long=no])
  CFLAGS="$save_CFLAGS"
  ])

AC_CACHE_CHECK(
  [whether ${CC} accepts -Wno-variadic-macros],
  [ac_cv_prog_cc_w_no_variadic_macros],
  [save_CFLAGS="$CFLAGS"
  CFLAGS="-Wno-variadic-macros"
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([[]])],
                    [ac_cv_prog_cc_w_no_variadic_macros=yes],
                    [ac_cv_prog_cc_w_no_variadic_macros=no])
  CFLAGS="$save_CFLAGS"
  ])

AC_CACHE_CHECK(
  [whether ${CC} accepts -Wno-overlength-strings],
  [ac_cv_prog_cc_w_no_overlength_strings],
  [save_CFLAGS="$CFLAGS"
  CFLAGS="-Wno-overlength-strings"
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([[]])],
                    [ac_cv_prog_cc_w_no_overlength_strings=yes],
                    [ac_cv_prog_cc_w_no_overlength_strings=no])
  CFLAGS="$save_CFLAGS"
  ])

strict1_warn=
if test $ac_cv_prog_cc_w_no_long_long = yes \
   && test $ac_cv_prog_cc_w_no_variadic_macros = yes \
   && test $ac_cv_prog_cc_w_no_overlength_strings = yes ; then
  strict1_warn="-pedantic -Wno-long-long -Wno-variadic-macros -Wno-overlength-strings"
fi

# Add -Wold-style-definition if it's accepted
AC_CACHE_CHECK(
  [whether ${CC} accepts -Wold-style-definition],
  [ac_cv_prog_cc_w_old_style_definition],
  [save_CFLAGS="$CFLAGS"
  CFLAGS="-Wold-style-definition"
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([[]])],
                    [ac_cv_prog_cc_w_old_style_definition=yes],
                    [ac_cv_prog_cc_w_old_style_definition=no])
  CFLAGS="$save_CFLAGS"
  ])
if test $ac_cv_prog_cc_w_old_style_definition = yes ; then
  strict1_warn="${strict1_warn} -Wold-style-definition"
fi

# Add -Wmissing-format-attribute if it's accepted
AC_CACHE_CHECK(
  [whether ${CC} accepts -Wmissing-format-attribute],
  [ac_cv_prog_cc_w_missing_format_attribute],
  [save_CFLAGS="$CFLAGS"
  CFLAGS="-Wmissing-format-attribute"
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([[]])],
                    [ac_cv_prog_cc_w_missing_format_attribute=yes],
                    [ac_cv_prog_cc_w_missing_format_attribute=no])
  CFLAGS="$save_CFLAGS"
  ])
if test $ac_cv_prog_cc_w_missing_format_attribute = yes ; then
  strict1_warn="${strict1_warn} -Wmissing-format-attribute"
fi

# Enable -Werror, period.
AC_ARG_ENABLE(werror_always, 
[  --enable-werror-always         enable -Werror always], [],
[enable_werror_always=no])
if test x${enable_werror_always} = xyes ; then
  strict1_warn="${strict1_warn} -Werror"
  WERROR=-Werror
fi

AC_SUBST(strict1_warn)

# Get C++ compatibility warning flag, if supported.
AC_CACHE_CHECK(
  [whether ${CC} accepts -Wc++-compat],
  [ac_cv_prog_cc_w_cxx_compat],
  [save_CFLAGS="$CFLAGS"
  CFLAGS="-Wc++-compat"
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([[]])],
                    [ac_cv_prog_cc_w_cxx_compat=yes],
                    [ac_cv_prog_cc_w_cxx_compat=no])
  CFLAGS="$save_CFLAGS"
  ])
if test x${ac_cv_prog_cc_w_cxx_compat} = xyes; then
  cxx_compat_warn="-Wc++-compat"
fi
AC_SUBST(cxx_compat_warn)


# If the native compiler is GCC, we can enable warnings even in stage1.  
# That's useful for people building cross-compilers, or just running a
# quick `make'.
warn_cflags=
if test "x$GCC" = "xyes"; then
  warn_cflags='$(GCC_WARN_CFLAGS)'
fi
AC_SUBST(warn_cflags)

# Enable -Werror in bootstrap stage2 and later.
is_release=
if test x"`cat $srcdir/DEV-PHASE`" != xexperimental; then
  is_release=yes
fi
AC_ARG_ENABLE(werror, 
[  --enable-werror         enable -Werror in bootstrap stage2 and later], [],
[if test x$is_release = x ; then
  # Default to "yes" on development branches.
  enable_werror=yes
else
  # Default to "no" on release branches.
  enable_werror=no
fi])
if test x$enable_werror = xyes ; then
  WERROR=-Werror
fi
AC_SUBST(WERROR)

# Enable expensive internal checks
AC_ARG_ENABLE(checking,
[  --enable-checking[=LIST]
			  enable expensive run-time checks.  With LIST,
			  enable only specific categories of checks.
			  Categories are: yes,no,all,none,release.
			  Flags are: assert,fold,gc,gcac,misc,
			  rtlflag,rtl,runtime,tree,valgrind.],
[ac_checking_flags="${enableval}"],[
# Determine the default checks.
if test x$is_release = x ; then
  ac_checking_flags=yes
else
  ac_checking_flags=release
fi])
ac_assert_checking=1
ac_checking=
ac_fold_checking=
ac_gc_checking=
ac_gc_always_collect=
ac_rtl_checking=
ac_rtlflag_checking=
ac_runtime_checking=1
ac_tree_checking=
ac_valgrind_checking=
IFS="${IFS= 	}"; ac_save_IFS="$IFS"; IFS="$IFS,"
for check in $ac_checking_flags
do
	case $check in
	# these set all the flags to specific states
	yes)		ac_assert_checking=1 ; ac_checking=1 ;
			ac_fold_checking= ; ac_gc_checking=1 ;
			ac_gc_always_collect= ; ac_rtl_checking= ;
			ac_rtlflag_checking=1 ; ac_runtime_checking=1 ;
			ac_tree_checking=1 ; ac_valgrind_checking= ;;
	no|none)	ac_assert_checking= ; ac_checking= ;
			ac_fold_checking= ; ac_gc_checking= ;
			ac_gc_always_collect= ; ac_rtl_checking= ;
			ac_rtlflag_checking= ; ac_runtime_checking= ;
			ac_tree_checking= ; ac_valgrind_checking= ;;
	all)		ac_assert_checking=1 ; ac_checking=1 ;
			ac_fold_checking=1 ; ac_gc_checking=1 ;
			ac_gc_always_collect=1 ; ac_rtl_checking=1 ;
			ac_rtlflag_checking=1 ; ac_runtime_checking=1 ;
			ac_tree_checking=1 ; ac_valgrind_checking= ;;
	release)	ac_assert_checking=1 ; ac_checking= ;
			ac_fold_checking= ; ac_gc_checking= ;
			ac_gc_always_collect= ; ac_rtl_checking= ;
			ac_rtlflag_checking= ; ac_runtime_checking=1 ;
			ac_tree_checking= ; ac_valgrind_checking= ;;
	# these enable particular checks
	assert) 	ac_assert_checking=1 ;;
	fold)		ac_fold_checking=1 ;;
	gc)		ac_gc_checking=1 ;;
	gcac)		ac_gc_always_collect=1 ;;
	misc)		ac_checking=1 ;;
	rtl)		ac_rtl_checking=1 ;;
	rtlflag)	ac_rtlflag_checking=1 ;;
	runtime)	ac_runtime_checking=1 ;;
	tree)		ac_tree_checking=1 ;;
	valgrind)	ac_valgrind_checking=1 ;;
	*)	AC_MSG_ERROR(unknown check category $check) ;;
	esac
done
IFS="$ac_save_IFS"

nocommon_flag=""
if test x$ac_checking != x ; then
  AC_DEFINE(ENABLE_CHECKING, 1,
[Define if you want more run-time sanity checks.  This one gets a grab
   bag of miscellaneous but relatively cheap checks.])
  nocommon_flag=-fno-common
fi
AC_SUBST(nocommon_flag)
if test x$ac_assert_checking != x ; then
  AC_DEFINE(ENABLE_ASSERT_CHECKING, 1,
[Define if you want assertions enabled.  This is a cheap check.])
fi
GCC_TARGET_TEMPLATE(ENABLE_RUNTIME_CHECKING)
if test x$ac_runtime_checking != x ; then
  AC_DEFINE(ENABLE_RUNTIME_CHECKING, 1,
[Define if you want runtime assertions enabled.  This is a cheap check.])
fi
if test x$ac_tree_checking != x ; then
  AC_DEFINE(ENABLE_TREE_CHECKING, 1,
[Define if you want all operations on trees (the basic data
   structure of the front ends) to be checked for dynamic type safety
   at runtime.  This is moderately expensive.  The tree browser debugging
   routines will also be enabled by this option.
   ])
  TREEBROWSER=tree-browser.o
fi
AC_SUBST(TREEBROWSER)
if test x$ac_rtl_checking != x ; then
  AC_DEFINE(ENABLE_RTL_CHECKING, 1,
[Define if you want all operations on RTL (the basic data structure
   of the optimizer and back end) to be checked for dynamic type safety
   at runtime.  This is quite expensive.])
fi
if test x$ac_rtlflag_checking != x ; then
  AC_DEFINE(ENABLE_RTL_FLAG_CHECKING, 1,
[Define if you want RTL flag accesses to be checked against the RTL
   codes that are supported for each access macro.  This is relatively
   cheap.])
fi
if test x$ac_gc_checking != x ; then
  AC_DEFINE(ENABLE_GC_CHECKING, 1,
[Define if you want the garbage collector to do object poisoning and
   other memory allocation checks.  This is quite expensive.])
fi
if test x$ac_gc_always_collect != x ; then
  AC_DEFINE(ENABLE_GC_ALWAYS_COLLECT, 1,
[Define if you want the garbage collector to operate in maximally
   paranoid mode, validating the entire heap and collecting garbage at
   every opportunity.  This is extremely expensive.])
fi
if test x$ac_fold_checking != x ; then
  AC_DEFINE(ENABLE_FOLD_CHECKING, 1,
[Define if you want fold checked that it never destructs its argument.
   This is quite expensive.])
fi
valgrind_path_defines=
valgrind_command=

dnl # This check AC_REQUIREs various stuff, so it *must not* be inside
dnl # an if statement.  This was the source of very frustrating bugs
dnl # in converting to autoconf 2.5x!
AC_CHECK_HEADER(valgrind.h, have_valgrind_h=yes, have_valgrind_h=no)

if test x$ac_valgrind_checking != x ; then
  # It is certainly possible that there's valgrind but no valgrind.h.
  # GCC relies on making annotations so we must have both.
  AC_MSG_CHECKING(for VALGRIND_DISCARD in <valgrind/memcheck.h>)
  AC_PREPROC_IFELSE([AC_LANG_SOURCE(
    [[#include <valgrind/memcheck.h>
#ifndef VALGRIND_DISCARD
#error VALGRIND_DISCARD not defined
#endif]])],
  [gcc_cv_header_valgrind_memcheck_h=yes],
  [gcc_cv_header_valgrind_memcheck_h=no])
  AC_MSG_RESULT($gcc_cv_header_valgrind_memcheck_h)
  AC_MSG_CHECKING(for VALGRIND_DISCARD in <memcheck.h>)
  AC_PREPROC_IFELSE([AC_LANG_SOURCE(
    [[#include <memcheck.h>
#ifndef VALGRIND_DISCARD
#error VALGRIND_DISCARD not defined
#endif]])],
  [gcc_cv_header_memcheck_h=yes],
  [gcc_cv_header_memcheck_h=no])
  AC_MSG_RESULT($gcc_cv_header_memcheck_h)
  AM_PATH_PROG_WITH_TEST(valgrind_path, valgrind,
	[$ac_dir/$ac_word --version | grep valgrind- >/dev/null 2>&1])
  if test "x$valgrind_path" = "x" \
    || (test $have_valgrind_h = no \
	&& test $gcc_cv_header_memcheck_h = no \
	&& test $gcc_cv_header_valgrind_memcheck_h = no); then
	AC_MSG_ERROR([*** Can't find both valgrind and valgrind/memcheck.h, memcheck.h or valgrind.h])
  fi
  valgrind_path_defines=-DVALGRIND_PATH='\"'$valgrind_path'\"'
  valgrind_command="$valgrind_path -q"
  AC_DEFINE(ENABLE_VALGRIND_CHECKING, 1,
[Define if you want to run subprograms and generated programs
   through valgrind (a memory checker).  This is extremely expensive.])
  if test $gcc_cv_header_valgrind_memcheck_h = yes; then
    AC_DEFINE(HAVE_VALGRIND_MEMCHECK_H, 1,
	[Define if valgrind's valgrind/memcheck.h header is installed.])
  fi
  if test $gcc_cv_header_memcheck_h = yes; then
    AC_DEFINE(HAVE_MEMCHECK_H, 1,
	[Define if valgrind's memcheck.h header is installed.])
  fi
fi
AC_SUBST(valgrind_path_defines)
AC_SUBST(valgrind_command)

AC_ARG_ENABLE(mapped-location,
[  --enable-mapped-location   location_t is fileline integer cookie],,
enable_mapped_location=no)

if test "$enable_mapped_location" = yes ; then
  AC_DEFINE(USE_MAPPED_LOCATION, 1,
[Define if location_t is fileline integer cookie.])
fi

# Enable code coverage collection
AC_ARG_ENABLE(coverage,
[  --enable-coverage[=LEVEL]
			  enable compiler's code coverage collection.
			  Use to measure compiler performance and locate
			  unused parts of the compiler. With LEVEL, specify
			  optimization. Values are opt, noopt,
			  default is noopt],
[case "${enableval}" in
  yes|noopt)
    coverage_flags="-fprofile-arcs -ftest-coverage -frandom-seed=\$@ -O0"
    ;;
  opt)
    coverage_flags="-fprofile-arcs -ftest-coverage -frandom-seed=\$@ -O2"
    ;;
  no)
    # a.k.a. --disable-coverage
    coverage_flags=""
    ;;
  *)
    AC_MSG_ERROR(unknown coverage setting $enableval)
    ;;
esac],
[coverage_flags=""])
AC_SUBST(coverage_flags)

AC_ARG_ENABLE(gather-detailed-mem-stats, 
[  --enable-gather-detailed-mem-stats         enable detailed memory allocation stats gathering], [],
[enable_gather_detailed_mem_stats=no])
if test x$enable_gather_detailed_mem_stats = xyes ; then
  AC_DEFINE(GATHER_STATISTICS, 1,
        [Define to enable detailed memory allocation stats gathering.])
fi

# -------------------------------
# Miscenalleous configure options
# -------------------------------

# With stabs
AC_ARG_WITH(stabs,
[  --with-stabs            arrange to use stabs instead of host debug format],
stabs="$with_stabs",
stabs=no)

# Determine whether or not multilibs are enabled.
AC_ARG_ENABLE(multilib,
[  --enable-multilib       enable library support for multiple ABIs],
[], [enable_multilib=yes])
AC_SUBST(enable_multilib)

# Enable __cxa_atexit for C++.
AC_ARG_ENABLE(__cxa_atexit,
[  --enable-__cxa_atexit   enable __cxa_atexit for C++],
[], [])

# Enable C extension for decimal float if target supports it.
AC_ARG_ENABLE(decimal-float,
[  --enable-decimal-float  enable decimal float extension to C],
[
  if test x$enablevar = xyes ; then
    case $target in
    powerpc*-*-linux* | i?86*-*-linux*)
      enable_decimal_float=yes
      ;;
    *)
      AC_MSG_WARN(decimal float is not supported for this target, ignored)
      enable_decimal_float=no
      ;;
    esac
  fi
], [enable_decimal_float=no])

AC_SUBST(enable_decimal_float)

dfp=`if test $enable_decimal_float = yes; then echo 1; else echo 0; fi`
AC_DEFINE_UNQUOTED(ENABLE_DECIMAL_FLOAT, $dfp,
[Define to 1 to enable decimal float extension to C.])

# Enable threads
# Pass with no value to take the default
# Pass with a value to specify a thread package
AC_ARG_ENABLE(threads,
[  --enable-threads        enable thread usage for target GCC
  --enable-threads=LIB    use LIB thread package for target GCC],,
[enable_threads=''])

AC_ARG_ENABLE(tls,
[  --enable-tls            enable or disable generation of tls code
                          overriding the assembler check for tls support],
[
  case $enable_tls in
    yes | no) ;;
    *) AC_MSG_ERROR(['$enable_tls' is an invalid value for --enable-tls.
Valid choices are 'yes' and 'no'.]) ;;
  esac
], [enable_tls=''])

AC_ARG_ENABLE(objc-gc,
[  --enable-objc-gc	  enable the use of Boehm's garbage collector with
			  the GNU Objective-C runtime],
if test x$enable_objc_gc = xno; then
	objc_boehm_gc=''
else
	objc_boehm_gc=1
fi,
objc_boehm_gc='')

AC_ARG_WITH(dwarf2,
[  --with-dwarf2           force the default debug format to be DWARF 2],
dwarf2="$with_dwarf2",
dwarf2=no)

AC_ARG_ENABLE(shared,
[  --disable-shared        don't provide a shared libgcc],
[
  case $enable_shared in
  yes | no) ;;
  *)
    enable_shared=no
    IFS="${IFS= 	}"; ac_save_ifs="$IFS"; IFS="${IFS}:,"
    for pkg in $enableval; do
      if test "X$pkg" = "Xgcc" || test "X$pkg" = "Xlibgcc"; then
        enable_shared=yes
      fi
    done
    IFS="$ac_save_ifs"
    ;;
  esac
], [enable_shared=yes])
AC_SUBST(enable_shared)

AC_ARG_WITH(build-sysroot, 
  [  --with-build-sysroot=sysroot
                          use sysroot as the system root during the build])

AC_ARG_WITH(sysroot,
[  --with-sysroot[=DIR] Search for usr/lib, usr/include, et al, within DIR.],
[
 case ${with_sysroot} in
 yes) TARGET_SYSTEM_ROOT='${exec_prefix}/${target_noncanonical}/sys-root' ;;
 *) TARGET_SYSTEM_ROOT=$with_sysroot ;;
 esac
   
 TARGET_SYSTEM_ROOT_DEFINE='-DTARGET_SYSTEM_ROOT=\"$(TARGET_SYSTEM_ROOT)\"'
 CROSS_SYSTEM_HEADER_DIR='$(TARGET_SYSTEM_ROOT)$(NATIVE_SYSTEM_HEADER_DIR)'
	
 if test "x$exec_prefix" = xNONE; then
  if test "x$prefix" = xNONE; then
   test_prefix=/usr/local
  else
   test_prefix=$prefix
  fi
 else
  test_prefix=$exec_prefix
 fi
 case ${TARGET_SYSTEM_ROOT} in
 "${test_prefix}"|"${test_prefix}/"*|\
 '${exec_prefix}'|'${exec_prefix}/'*)
   t="$TARGET_SYSTEM_ROOT_DEFINE -DTARGET_SYSTEM_ROOT_RELOCATABLE"
   TARGET_SYSTEM_ROOT_DEFINE="$t"
   ;;
 esac
], [
 TARGET_SYSTEM_ROOT=
 TARGET_SYSTEM_ROOT_DEFINE=
 CROSS_SYSTEM_HEADER_DIR='$(gcc_tooldir)/sys-include'
])
AC_SUBST(TARGET_SYSTEM_ROOT)
AC_SUBST(TARGET_SYSTEM_ROOT_DEFINE)
AC_SUBST(CROSS_SYSTEM_HEADER_DIR)

# Build with intermodule optimisations
AC_ARG_ENABLE(intermodule,
[  --enable-intermodule    build the compiler in one step],
[case ${enable_intermodule} in
  yes) onestep="-onestep";;
  *) onestep="";;
esac],
[onestep=""])
AC_SUBST(onestep)

# Sanity check enable_languages in case someone does not run the toplevel
# configure # script.
AC_ARG_ENABLE(languages,
[  --enable-languages=LIST specify which front-ends to build],
[case ,${enable_languages}, in
       ,,|,yes,)
		# go safe -- we cannot be much sure without the toplevel
		# configure's
		# analysis of which target libs are present and usable
		enable_languages=c
		;;
	 *,all,*)
		AC_MSG_ERROR([only the toplevel supports --enable-languages=all])
		;;
	*,c,*)
		;;
        *)
		enable_languages=c,${enable_languages}
		;;
esac],
[enable_languages=c])

# Used by documentation targets
AC_SUBST(datarootdir)
AC_SUBST(docdir)
AC_SUBST(htmldir)

# -------------------------
# Checks for other programs
# -------------------------

AC_PROG_MAKE_SET

# Find some useful tools
AC_PROG_AWK
# We need awk to create options.c and options.h.
# Bail out if it's missing.
case ${AWK} in
  "") AC_MSG_ERROR([can't build without awk, bailing out]) ;;
esac

gcc_AC_PROG_LN_S
ACX_PROG_LN($LN_S)
AC_PROG_RANLIB
case "${host}" in
*-*-darwin*)
  # By default, the Darwin ranlib will not treat common symbols as
  # definitions when  building the archive table of contents.  Other 
  # ranlibs do that; pass an option to the Darwin ranlib that makes
  # it behave similarly.
  ranlib_flags="-c" 
  ;;
*)
  ranlib_flags=""
esac
AC_SUBST(ranlib_flags)
     
gcc_AC_PROG_INSTALL

# See if cmp has --ignore-initial.
gcc_AC_PROG_CMP_IGNORE_INITIAL

# See if we have the mktemp command.
AC_CHECK_PROG(have_mktemp_command, mktemp, yes, no)

MISSING="${CONFIG_SHELL-/bin/sh} $srcdir/../missing"

# See if makeinfo has been installed and is modern enough
# that we can use it.
gcc_AC_CHECK_PROG_VER(MAKEINFO, makeinfo, --version,
  [GNU texinfo.* \([0-9][0-9.]*\)],
  [4.[4-9]*])
if test $gcc_cv_prog_makeinfo_modern = no; then
  MAKEINFO="$MISSING makeinfo"
  AC_MSG_WARN([
*** Makeinfo is missing or too old.
*** Info documentation will not be built.])
  BUILD_INFO=
else
  BUILD_INFO=info
fi
AC_SUBST(BUILD_INFO)

# Is pod2man recent enough to regenerate manpages?
AC_MSG_CHECKING([for recent Pod::Man])
if (perl -e 'use 1.10 Pod::Man') >/dev/null 2>&1; then
  AC_MSG_RESULT(yes)
  GENERATED_MANPAGES=generated-manpages
else
  AC_MSG_RESULT(no)
  GENERATED_MANPAGES=
fi
AC_SUBST(GENERATED_MANPAGES)

# How about lex?
dnl Don't use AC_PROG_LEX; we insist on flex.
dnl LEXLIB is not useful in gcc.
AC_CHECK_PROGS([FLEX], flex, [$MISSING flex])

# Bison?
AC_CHECK_PROGS([BISON], bison, [$MISSING bison])

# Binutils are not build modules, unlike bison/flex/makeinfo.  So we
# check for build == host before using them.

# NM
if test x${build} = x${host} && test -f $srcdir/../binutils/nm.c \
  && test -d ../binutils ; then
  NM='$(objdir)/../binutils/nm-new'
else
  AC_CHECK_PROG(NM, nm, nm, ${CONFIG_SHELL-/bin/sh} ${srcdir}/../missing nm)
fi

# AR
if test x${build} = x${host} && test -f $srcdir/../binutils/ar.c \
  && test -d ../binutils ; then
  AR='$(objdir)/../binutils/ar'
else
  AC_CHECK_PROG(AR, ar, ar, ${CONFIG_SHELL-/bin/sh} ${srcdir}/../missing ar)
fi


# --------------------
# Checks for C headers
# --------------------

AC_MSG_CHECKING(for GNU C library)
AC_CACHE_VAL(gcc_cv_glibc,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
  [[#include <features.h>]], [[
#if ! (defined __GLIBC__ || defined __GNU_LIBRARY__)
#error Not a GNU C library system
#endif]])],
  [gcc_cv_glibc=yes],
  [gcc_cv_glibc=no])])
AC_MSG_RESULT($gcc_cv_glibc)
if test $gcc_cv_glibc = yes; then
  AC_DEFINE(_GNU_SOURCE, 1, [Always define this when using the GNU C Library])
fi

# Need to reject headers which give warnings, so that the -Werror bootstrap
# works later. *sigh*  This needs to come before all header checks.
AC_PROG_CPP_WERROR

AC_HEADER_STDC
AC_HEADER_TIME
ACX_HEADER_STRING
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS(limits.h stddef.h string.h strings.h stdlib.h time.h iconv.h \
		 fcntl.h unistd.h sys/file.h sys/time.h sys/mman.h \
		 sys/resource.h sys/param.h sys/times.h sys/stat.h \
		 direct.h malloc.h langinfo.h ldfcn.h locale.h wchar.h)

# Check for thread headers.
AC_CHECK_HEADER(thread.h, [have_thread_h=yes], [have_thread_h=])
AC_CHECK_HEADER(pthread.h, [have_pthread_h=yes], [have_pthread_h=])

# These tests can't be done till we know if we have limits.h.
gcc_AC_C_CHAR_BIT
AC_C_BIGENDIAN

# --------
# UNSORTED
# --------

# Stage specific cflags for build.
stage1_cflags=
case $build in
vax-*-*)
  if test x$GCC = xyes
  then
    stage1_cflags="-Wa,-J"
  else
    stage1_cflags="-J"
  fi
  ;;
esac
AC_SUBST(stage1_cflags)

# These libraries may be used by collect2.
# We may need a special search path to get them linked.
AC_CACHE_CHECK(for collect2 libraries, gcc_cv_collect2_libs,
[save_LIBS="$LIBS"
for libs in '' -lld -lmld \
		'-L/usr/lib/cmplrs/cc2.11 -lmld' \
		'-L/usr/lib/cmplrs/cc3.11 -lmld'
do
	LIBS="$libs"
	AC_TRY_LINK_FUNC(ldopen,
		[gcc_cv_collect2_libs="$libs"; break])
done
LIBS="$save_LIBS"
test -z "$gcc_cv_collect2_libs" && gcc_cv_collect2_libs='none required'])
case $gcc_cv_collect2_libs in
	"none required")	;;
	*)	COLLECT2_LIBS=$gcc_cv_collect2_libs ;;
esac
AC_SUBST(COLLECT2_LIBS)

# When building Ada code on Alpha, we need exc_resume which is usually in
# -lexc.  So test for it.
save_LIBS="$LIBS"
LIBS=
AC_SEARCH_LIBS(exc_resume, exc)
GNAT_LIBEXC="$LIBS"
LIBS="$save_LIBS"
AC_SUBST(GNAT_LIBEXC)

# Some systems put ldexp and frexp in libm instead of libc; assume
# they're both in the same place.  jcf-dump needs them.
save_LIBS="$LIBS"
LIBS=
AC_SEARCH_LIBS(ldexp, m)
LDEXP_LIB="$LIBS"
LIBS="$save_LIBS"
AC_SUBST(LDEXP_LIB)

# Use <inttypes.h> only if it exists,
# doesn't clash with <sys/types.h>, and declares intmax_t.
AC_MSG_CHECKING(for inttypes.h)
AC_CACHE_VAL(gcc_cv_header_inttypes_h,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[#include <sys/types.h>
#include <inttypes.h>]],
  [[intmax_t i = -1;]])],
  [gcc_cv_header_inttypes_h=yes],
  [gcc_cv_header_inttypes_h=no])])
AC_MSG_RESULT($gcc_cv_header_inttypes_h)
if test $gcc_cv_header_inttypes_h = yes; then
  AC_DEFINE(HAVE_INTTYPES_H, 1,
	[Define if you have a working <inttypes.h> header file.])
fi

dnl Disabled until we have a complete test for buggy enum bitfields.
dnl gcc_AC_C_ENUM_BF_UNSIGNED

define(gcc_UNLOCKED_FUNCS, clearerr_unlocked feof_unlocked dnl
  ferror_unlocked fflush_unlocked fgetc_unlocked fgets_unlocked dnl
  fileno_unlocked fprintf_unlocked fputc_unlocked fputs_unlocked dnl
  fread_unlocked fwrite_unlocked getchar_unlocked getc_unlocked dnl
  putchar_unlocked putc_unlocked)
AC_CHECK_FUNCS(times clock kill getrlimit setrlimit atoll atoq \
	sysconf strsignal getrusage nl_langinfo scandir alphasort \
	gettimeofday mbstowcs wcswidth mmap mincore setlocale \
	gcc_UNLOCKED_FUNCS)

if test x$ac_cv_func_mbstowcs = xyes; then
  AC_CACHE_CHECK(whether mbstowcs works, gcc_cv_func_mbstowcs_works,
[    AC_RUN_IFELSE([AC_LANG_SOURCE([[#include <stdlib.h>
int main()
{
  mbstowcs(0, "", 0);
  return 0;
}]])],
    [gcc_cv_func_mbstowcs_works=yes],
    [gcc_cv_func_mbstowcs_works=no],
    [gcc_cv_func_mbstowcs_works=yes])])
  if test x$gcc_cv_func_mbstowcs_works = xyes; then
    AC_DEFINE(HAVE_WORKING_MBSTOWCS, 1,
  [Define this macro if mbstowcs does not crash when its
   first argument is NULL.])
  fi
fi

AC_CHECK_TYPE(ssize_t, int)

# Try to determine the array type of the second argument of getgroups
# for the target system (int or gid_t).
AC_TYPE_GETGROUPS
if test "${target}" = "${build}"; then
  TARGET_GETGROUPS_T=$ac_cv_type_getgroups
else
  case "${target}" in
	# This condition may need some tweaking.  It should include all
	# targets where the array type of the second argument of getgroups
	# is int and the type of gid_t is not equivalent to int.
	*-*-sunos* | *-*-ultrix*)
		TARGET_GETGROUPS_T=int
		;;
	*)
		TARGET_GETGROUPS_T=gid_t
		;;
  esac
fi
AC_SUBST(TARGET_GETGROUPS_T)

gcc_AC_FUNC_MMAP_BLACKLIST

case "${host}" in
*-*-*vms*)
  # Under VMS, vfork works very differently than on Unix. The standard test 
  # won't work, and it isn't easily adaptable. It makes more sense to
  # just force it.
  ac_cv_func_vfork_works=yes
  ;;
esac
AC_FUNC_FORK

AM_ICONV
# Until we have in-tree GNU iconv:
LIBICONV_DEP=
AC_SUBST(LIBICONV_DEP)

AM_LC_MESSAGES

AM_LANGINFO_CODESET

# We will need to find libiberty.h and ansidecl.h
saved_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -I${srcdir} -I${srcdir}/../include"
gcc_AC_CHECK_DECLS(getenv atol asprintf sbrk abort atof getcwd getwd \
	strsignal strstr strverscmp \
	errno snprintf vsnprintf vasprintf malloc realloc calloc \
	free basename getopt clock getpagesize gcc_UNLOCKED_FUNCS, , ,[
#include "ansidecl.h"
#include "system.h"])

gcc_AC_CHECK_DECLS(getrlimit setrlimit getrusage, , ,[
#include "ansidecl.h"
#include "system.h"
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
])

AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include "ansidecl.h"
#include "system.h"
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
]], [[rlim_t l = 0;]])],[],[AC_DEFINE([rlim_t],[long],
[Define to \`long' if <sys/resource.h> doesn't define.])])

# On AIX 5.2, <ldfcn.h> conflicts with <fcntl.h>, as both define incompatible
# FREAD and FWRITE macros.  Fortunately, for GCC's single usage of ldgetname
# in collect2.c, <fcntl.h> isn't visible, but the configure test below needs
# to undef these macros to get the correct value for HAVE_DECL_LDGETNAME.
gcc_AC_CHECK_DECLS(ldgetname, , ,[
#include "ansidecl.h"
#include "system.h"
#ifdef HAVE_LDFCN_H
#undef FREAD
#undef FWRITE
#include <ldfcn.h>
#endif
])

gcc_AC_CHECK_DECLS(times, , ,[
#include "ansidecl.h"
#include "system.h"
#ifdef HAVE_SYS_TIMES_H
#include <sys/times.h>
#endif
])

gcc_AC_CHECK_DECLS(sigaltstack, , ,[
#include "ansidecl.h"
#include "system.h"
#include <signal.h>
])

# More time-related stuff.
AC_CACHE_CHECK(for struct tms, ac_cv_struct_tms, [
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include "ansidecl.h"
#include "system.h"
#ifdef HAVE_SYS_TIMES_H
#include <sys/times.h>
#endif
]], [[struct tms tms;]])],[ac_cv_struct_tms=yes],[ac_cv_struct_tms=no])])
if test $ac_cv_struct_tms = yes; then
  AC_DEFINE(HAVE_STRUCT_TMS, 1,
  [Define if <sys/times.h> defines struct tms.])
fi

# use gcc_cv_* here because this doesn't match the behavior of AC_CHECK_TYPE.
# revisit after autoconf 2.50.
AC_CACHE_CHECK(for clock_t, gcc_cv_type_clock_t, [
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include "ansidecl.h"
#include "system.h"
]], [[clock_t x;]])],[gcc_cv_type_clock_t=yes],[gcc_cv_type_clock_t=no])])
if test $gcc_cv_type_clock_t = yes; then
  AC_DEFINE(HAVE_CLOCK_T, 1,
  [Define if <time.h> defines clock_t.])
fi

# Restore CFLAGS from before the gcc_AC_NEED_DECLARATIONS tests.
CFLAGS="$saved_CFLAGS"

gcc_AC_INITFINI_ARRAY

# mkdir takes a single argument on some systems. 
gcc_AC_FUNC_MKDIR_TAKES_ONE_ARG

# File extensions
manext='.1'
objext='.o'
AC_SUBST(manext)
AC_SUBST(objext)

# With Setjmp/Longjmp based exception handling.
AC_ARG_ENABLE(sjlj-exceptions,
[  --enable-sjlj-exceptions
                          arrange to use setjmp/longjmp exception handling],
[sjlj=`if test $enableval = yes; then echo 1; else echo 0; fi`
AC_DEFINE_UNQUOTED(CONFIG_SJLJ_EXCEPTIONS, $sjlj,
  [Define 0/1 to force the choice for exception handling model.])])

# For platforms with the unwind ABI which includes an unwind library,
# libunwind, we can choose to use the system libunwind.
AC_ARG_WITH(system-libunwind,
[  --with-system-libunwind use installed libunwind])

# config.gcc also contains tests of with_system_libunwind.
if test x$with_system_libunwind = xyes; then
        AC_DEFINE(HAVE_GETIPINFO, 1,
[Define to 1 if system unwind library has _Unwind_GetIPInfo.])
fi

# --------------------------------------------------------
# Build, host, and target specific configuration fragments
# --------------------------------------------------------

# Collect build-machine-specific information.
. ${srcdir}/config.build

# Collect host-machine-specific information.
. ${srcdir}/config.host

target_gtfiles=

# Collect target-machine-specific information.
. ${srcdir}/config.gcc

extra_objs="${host_extra_objs} ${extra_objs}"
extra_gcc_objs="${host_extra_gcc_objs} ${extra_gcc_objs}"

# Default the target-machine variables that were not explicitly set.
if test x"$tm_file" = x
then tm_file=$cpu_type/$cpu_type.h; fi

if test x"$extra_headers" = x
then extra_headers=; fi

if test x$md_file = x
then md_file=$cpu_type/$cpu_type.md; fi

if test x$out_file = x
then out_file=$cpu_type/$cpu_type.c; fi

if test x"$tmake_file" = x
then tmake_file=$cpu_type/t-$cpu_type
fi

if test x"$dwarf2" = xyes
then tm_file="$tm_file tm-dwarf2.h"
fi

# Say what files are being used for the output code and MD file.
echo "Using \`$srcdir/config/$out_file' for machine-specific logic."
echo "Using \`$srcdir/config/$md_file' as machine description file."

# If any of the xm_file variables contain nonexistent files, warn
# about them and drop them.

bx=
for x in $build_xm_file; do
  if    test -f $srcdir/config/$x
  then      bx="$bx $x"
  else      AC_MSG_WARN($srcdir/config/$x does not exist.)
  fi
done
build_xm_file="$bx"

hx=
for x in $host_xm_file; do
  if    test -f $srcdir/config/$x
  then      hx="$hx $x"
  else      AC_MSG_WARN($srcdir/config/$x does not exist.)
  fi
done
host_xm_file="$hx"

tx=
for x in $xm_file; do
  if    test -f $srcdir/config/$x
  then      tx="$tx $x"
  else      AC_MSG_WARN($srcdir/config/$x does not exist.)
  fi
done
xm_file="$tx"

count=a
for f in $tm_file; do
	count=${count}x
done
if test $count = ax; then
	echo "Using \`$srcdir/config/$tm_file' as target machine macro file."
else
	echo "Using the following target machine macro files:"
	for f in $tm_file; do
		echo "	$srcdir/config/$f"
	done
fi

if test x$need_64bit_hwint = xyes; then
	AC_DEFINE(NEED_64BIT_HOST_WIDE_INT, 1,
[Define to 1 if HOST_WIDE_INT must be 64 bits wide (see hwint.h).])
fi

if test x$use_long_long_for_widest_fast_int = xyes; then
	AC_DEFINE(USE_LONG_LONG_FOR_WIDEST_FAST_INT, 1,
[Define to 1 if the 'long long' (or '__int64') is wider than 'long' but still
efficiently supported by the host hardware.])
fi

count=a
for f in $host_xm_file; do
	count=${count}x
done
if test $count = a; then
	:
elif test $count = ax; then
	echo "Using \`$srcdir/config/$host_xm_file' as host machine macro file."
else
	echo "Using the following host machine macro files:"
	for f in $host_xm_file; do
		echo "	$srcdir/config/$f"
	done
fi
echo "Using ${out_host_hook_obj} for host machine hooks."

if test "$host_xm_file" != "$build_xm_file"; then
	count=a
	for f in $build_xm_file; do
		count=${count}x
	done
	if test $count = a; then
		:
	elif test $count = ax; then
		echo "Using \`$srcdir/config/$build_xm_file' as build machine macro file."
	else
		echo "Using the following build machine macro files:"
		for f in $build_xm_file; do
			echo "	$srcdir/config/$f"
		done
	fi
fi

case ${host} in
  powerpc*-*-darwin*)
    AC_CACHE_CHECK([whether mcontext_t fields have underscores],
      gcc_cv_mcontext_underscores,
      AC_COMPILE_IFELSE([
#include <sys/signal.h>
#include <ucontext.h>
int main() { mcontext_t m; if (m->ss.srr0) return 0; return 0; }
],
	gcc_cv_mcontext_underscores=no, gcc_cv_mcontext_underscores=yes))
      if test $gcc_cv_mcontext_underscores = yes; then
        AC_DEFINE(HAS_MCONTEXT_T_UNDERSCORES,,dnl
          [mcontext_t fields start with __])
      fi
    ;;
esac

# ---------
# Threading
# ---------

# Check if a valid thread package
case ${enable_threads} in
  "" | no)
    # No threads
    target_thread_file='single'
    ;;
  yes)
    # default
    target_thread_file='single'
    ;;
  aix | dce | gnat | irix | posix | posix95 | rtems | \
  single | solaris | vxworks | win32 )
    target_thread_file=${enable_threads}
    ;;
  *)
    echo "${enable_threads} is an unknown thread package" 1>&2
    exit 1
    ;;
esac

if test x${thread_file} = x; then
  # No thread file set by target-specific clauses in config.gcc,
  # so use file chosen by default logic above
  thread_file=${target_thread_file}
fi

# Make gthr-default.h if we have a thread file.
gthread_flags=
if test $thread_file != single; then
  rm -f gthr-default.h
  echo "#include \"gthr-${thread_file}.h\"" > gthr-default.h
  gthread_flags=-DHAVE_GTHR_DEFAULT
fi
AC_SUBST(gthread_flags)

# --------
# UNSORTED
# --------

use_cxa_atexit=no
if test x$enable___cxa_atexit = xyes || \
   test x$enable___cxa_atexit = x -a x$default_use_cxa_atexit = xyes; then
  if test x$host = x$target; then
    AC_CHECK_FUNC(__cxa_atexit,[use_cxa_atexit=yes],
	[echo "__cxa_atexit can't be enabled on this target"])
  else
    # We can't check for __cxa_atexit when building a cross, so assume
    # it is available 
    use_cxa_atexit=yes
  fi
  if test x$use_cxa_atexit = xyes; then
    AC_DEFINE(DEFAULT_USE_CXA_ATEXIT, 2,
      [Define if you want to use __cxa_atexit, rather than atexit, to
      register C++ destructors for local statics and global objects.
      This is essential for fully standards-compliant handling of
      destructors, but requires __cxa_atexit in libc.])
  fi
fi

use_getipinfo=yes
if test x$with_system_libunwind = xyes; then
   if test x$host = x$target; then
     AC_SEARCH_LIBS(_Unwind_GetIPInfo, unwind,, [use_getipinfo=no])
   fi
fi
GCC_TARGET_TEMPLATE(HAVE_GETIPINFO)
if test x$use_getipinfo = xyes; then
   AC_DEFINE(HAVE_GETIPINFO, 1,
	[Define to 1 if system unwind library has _Unwind_GetIPInfo.])
else
   echo "The system unwind library does not support _Unwind_GetIPInfo."
fi

# Look for a file containing extra machine modes.
if test -n "$extra_modes" && test -f $srcdir/config/$extra_modes; then
  extra_modes_file='$(srcdir)'/config/${extra_modes}
  AC_SUBST(extra_modes_file)
  AC_DEFINE_UNQUOTED(EXTRA_MODES_FILE, "config/$extra_modes",
  [Define to the name of a file containing a list of extra machine modes
   for this architecture.])
fi

# Convert extra_options into a form suitable for Makefile use.
extra_opt_files=
for f in $extra_options; do
  extra_opt_files="$extra_opt_files \$(srcdir)/config/$f"
done
AC_SUBST(extra_opt_files)

# auto-host.h is the file containing items generated by autoconf and is
# the first file included by config.h.
# If host=build, it is correct to have bconfig include auto-host.h
# as well.  If host!=build, we are in error and need to do more 
# work to find out the build config parameters.
if test x$host = x$build
then
	build_auto=auto-host.h
else
	# We create a subdir, then run autoconf in the subdir.
	# To prevent recursion we set host and build for the new
	# invocation of configure to the build for this invocation
	# of configure. 
	tempdir=build.$$
	rm -rf $tempdir
	mkdir $tempdir
	cd $tempdir
	case ${srcdir} in
	/* | [A-Za-z]:[\\/]* ) realsrcdir=${srcdir};;
	*) realsrcdir=../${srcdir};;
	esac
	saved_CFLAGS="${CFLAGS}"
	CC="${CC_FOR_BUILD}" CFLAGS="${CFLAGS_FOR_BUILD}" \
	${realsrcdir}/configure \
		--enable-languages=${enable_languages-all} \
		--target=$target_alias --host=$build_alias --build=$build_alias
	CFLAGS="${saved_CFLAGS}"

	# We just finished tests for the build machine, so rename
	# the file auto-build.h in the gcc directory.
	mv auto-host.h ../auto-build.h
	cd ..
	rm -rf $tempdir
	build_auto=auto-build.h
fi
AC_SUBST(build_subdir)

tm_file="${tm_file} defaults.h"
tm_p_file="${tm_p_file} tm-preds.h"
host_xm_file="auto-host.h ansidecl.h ${host_xm_file}"
build_xm_file="${build_auto} ansidecl.h ${build_xm_file}"
# We don't want ansidecl.h in target files, write code there in ISO/GNU C.
# put this back in temporarily.
xm_file="auto-host.h ansidecl.h ${xm_file}"

# --------
# UNSORTED
# --------

changequote(,)dnl
# Compile in configure arguments.
if test -f configargs.h ; then
	# Being re-configured.
	gcc_config_arguments=`grep configuration_arguments configargs.h | sed -e 's/.*"\([^"]*\)".*/\1/'`
	gcc_config_arguments="$gcc_config_arguments : (reconfigured) $TOPLEVEL_CONFIGURE_ARGUMENTS"
else
	gcc_config_arguments="$TOPLEVEL_CONFIGURE_ARGUMENTS"
fi

# Double all backslashes and backslash all quotes to turn
# gcc_config_arguments into a C string.
sed -e 's/\\/\\\\/g; s/"/\\"/g' <<EOF >conftest.out
$gcc_config_arguments
EOF
gcc_config_arguments_str=`cat conftest.out`
rm -f conftest.out

cat > configargs.h <<EOF
/* Generated automatically. */
static const char configuration_arguments[] = "$gcc_config_arguments_str";
static const char thread_model[] = "$thread_file";

static const struct {
  const char *name, *value;
} configure_default_options[] = $configure_default_options;
EOF
changequote([,])dnl

# Internationalization
ZW_GNU_GETTEXT_SISTER_DIR

# If LIBINTL contains LIBICONV, then clear LIBICONV so we don't get
# -liconv on the link line twice.
case "$LIBINTL" in *$LIBICONV*)
	LIBICONV= ;;
esac

AC_ARG_ENABLE(secureplt,
[  --enable-secureplt      enable -msecure-plt by default for PowerPC],
[], [])

# Windows32 Registry support for specifying GCC installation paths.
AC_ARG_ENABLE(win32-registry,
[  --disable-win32-registry
                          disable lookup of installation paths in the
                          Registry on Windows hosts
  --enable-win32-registry enable registry lookup (default)
  --enable-win32-registry=KEY
                          use KEY instead of GCC version as the last portion
                          of the registry key],,)

case $host_os in
  win32 | pe | cygwin* | mingw32* | uwin*)
    if test "x$enable_win32_registry" != xno; then
      AC_SEARCH_LIBS(RegOpenKeyExA, advapi32,, [enable_win32_registry=no])
    fi

    if test "x$enable_win32_registry" != xno; then
      AC_DEFINE(ENABLE_WIN32_REGISTRY, 1,
  [Define to 1 if installation paths should be looked up in the Windows
   Registry. Ignored on non-Windows hosts.])

      if test "x$enable_win32_registry" != xyes \
         && test "x$enable_win32_registry" != x; then
	AC_DEFINE_UNQUOTED(WIN32_REGISTRY_KEY, "$enable_win32_registry",
  [Define to be the last component of the Windows registry key under which
   to look for installation paths.  The full key used will be 
   HKEY_LOCAL_MACHINE/SOFTWARE/Free Software Foundation/{WIN32_REGISTRY_KEY}.
   The default is the GCC version number.])
      fi
    fi
  ;;
esac

# Get an absolute path to the GCC top-level source directory
holddir=`${PWDCMD-pwd}`
cd $srcdir
topdir=`${PWDCMD-pwd}`
cd $holddir

# Conditionalize the makefile for this host machine.
xmake_file=
for f in ${host_xmake_file}
do
	if test -f ${srcdir}/config/$f
	then
		xmake_file="${xmake_file} \$(srcdir)/config/$f"
	fi
done

# Conditionalize the makefile for this target machine.
tmake_file_=
for f in ${tmake_file}
do
	if test -f ${srcdir}/config/$f
	then
		tmake_file_="${tmake_file_} \$(srcdir)/config/$f"
	fi
done
tmake_file="${tmake_file_}"

# If the host doesn't support symlinks, modify CC in
# FLAGS_TO_PASS so CC="stage1/xgcc -Bstage1/" works.
# Otherwise, we can use "CC=$(CC)".
rm -f symtest.tem
case "$LN_S" in
	*-s*)
		cc_set_by_configure="\$(CC)"
		quoted_cc_set_by_configure="\$(CC)"
		stage_prefix_set_by_configure="\$(STAGE_PREFIX)"
		quoted_stage_prefix_set_by_configure="\$(STAGE_PREFIX)"
		;;
	*)
		cc_set_by_configure="\`case '\$(CC)' in stage*) echo '\$(CC)' | sed -e 's|stage|../stage|g';; *) echo '\$(CC)';; esac\`"
		quoted_cc_set_by_configure="\\\`case '\\\$(CC)' in stage*) echo '\\\$(CC)' | sed -e 's|stage|../stage|g';; *) echo '\\\$(CC)';; esac\\\`"
		stage_prefix_set_by_configure="\`case '\$(STAGE_PREFIX)' in stage*) echo '\$(STAGE_PREFIX)' | sed -e 's|stage|../stage|g';; *) echo '\$(STAGE_PREFIX)';; esac\`"
		quoted_stage_prefix_set_by_configure="\\\`case '\\\$(STAGE_PREFIX)' in stage*) echo '\\\$(STAGE_PREFIX)' | sed -e 's|stage|../stage|g';; *) echo '\\\$(STAGE_PREFIX)';; esac\\\`"
		;;
esac

# This is a terrible hack which will go away some day.
host_cc_for_libada=${CC}
AC_SUBST(host_cc_for_libada)

out_object_file=`basename $out_file .c`.o

tm_file_list="options.h"
tm_include_list="options.h"
for f in $tm_file; do
  case $f in
    defaults.h )
       tm_file_list="${tm_file_list} \$(srcdir)/$f"
       tm_include_list="${tm_include_list} $f"
       ;;
    * )
       tm_file_list="${tm_file_list} \$(srcdir)/config/$f"
       tm_include_list="${tm_include_list} config/$f"
       ;;
  esac
done

tm_p_file_list=
tm_p_include_list=
for f in $tm_p_file; do
  case $f in
    tm-preds.h )
       tm_p_file_list="${tm_p_file_list} $f"
       tm_p_include_list="${tm_p_include_list} $f"
       ;;
    * )
       tm_p_file_list="${tm_p_file_list} \$(srcdir)/config/$f"
       tm_p_include_list="${tm_p_include_list} config/$f"
  esac
done

xm_file_list=
xm_include_list=
for f in $xm_file; do
  case $f in
    ansidecl.h )
       xm_file_list="${xm_file_list} \$(srcdir)/../include/$f"
       xm_include_list="${xm_include_list} $f"
       ;;
    auto-host.h )
       xm_file_list="${xm_file_list} $f"
       xm_include_list="${xm_include_list} $f"
       ;;
    * )
       xm_file_list="${xm_file_list} \$(srcdir)/config/$f"
       xm_include_list="${xm_include_list} config/$f"
       ;;
  esac
done

host_xm_file_list=
host_xm_include_list=
for f in $host_xm_file; do
  case $f in
    ansidecl.h )
       host_xm_file_list="${host_xm_file_list} \$(srcdir)/../include/$f"
       host_xm_include_list="${host_xm_include_list} $f"
       ;;
    auto-host.h )
       host_xm_file_list="${host_xm_file_list} $f"
       host_xm_include_list="${host_xm_include_list} $f"
       ;;
    * )
       host_xm_file_list="${host_xm_file_list} \$(srcdir)/config/$f"
       host_xm_include_list="${host_xm_include_list} config/$f"
       ;;
  esac
done

build_xm_file_list=
for f in $build_xm_file; do
  case $f in
    ansidecl.h )
       build_xm_file_list="${build_xm_file_list} \$(srcdir)/../include/$f"
       build_xm_include_list="${build_xm_include_list} $f"
       ;;
    auto-build.h | auto-host.h )
       build_xm_file_list="${build_xm_file_list} $f"
       build_xm_include_list="${build_xm_include_list} $f"
       ;;
    * )
       build_xm_file_list="${build_xm_file_list} \$(srcdir)/config/$f"
       build_xm_include_list="${build_xm_include_list} config/$f"
       ;;
  esac
done

# Define macro CROSS_DIRECTORY_STRUCTURE in compilation if this is a
# cross-compiler which does not use the native headers and libraries.
# Also use all.cross instead of all.internal and adjust SYSTEM_HEADER_DIR.
CROSS=						AC_SUBST(CROSS)
ALL=all.internal				AC_SUBST(ALL)
SYSTEM_HEADER_DIR='$(NATIVE_SYSTEM_HEADER_DIR)'	AC_SUBST(SYSTEM_HEADER_DIR)

if test "x$with_build_sysroot" != x; then
  build_system_header_dir=$with_build_sysroot'$(NATIVE_SYSTEM_HEADER_DIR)'
else
  # This value is used, even on a native system, because 
  # CROSS_SYSTEM_HEADER_DIR is just 
  # $(TARGET_SYSTEM_ROOT)$(NATIVE_SYSTEM_HEADER_DIR).
  build_system_header_dir='$(CROSS_SYSTEM_HEADER_DIR)'
fi

if test x$host != x$target
then
	CROSS="-DCROSS_DIRECTORY_STRUCTURE"
	ALL=all.cross
	SYSTEM_HEADER_DIR=$build_system_header_dir
	case "$host","$target" in
	# Darwin crosses can use the host system's libraries and headers,
	# because of the fat library support.  Of course, it must be the
	# same version of Darwin on both sides.  Allow the user to
	# just say --target=foo-darwin without a version number to mean
	# "the version on this system".
	    *-*-darwin*,*-*-darwin*)
		hostos=`echo $host | sed 's/.*-darwin/darwin/'`
		targetos=`echo $target | sed 's/.*-darwin/darwin/'`
		if test $hostos = $targetos -o $targetos = darwin ; then
		    CROSS=
		    SYSTEM_HEADER_DIR='$(NATIVE_SYSTEM_HEADER_DIR)'
		    with_headers=yes
		fi
		;;

	    i?86-*-*,x86_64-*-* \
	    | powerpc*-*-*,powerpc64*-*-*)
		CROSS="$CROSS -DNATIVE_CROSS" ;;
	esac
elif test "x$TARGET_SYSTEM_ROOT" != x; then
        SYSTEM_HEADER_DIR=$build_system_header_dir 
fi

# If this is a cross-compiler that does not
# have its own set of headers then define
# inhibit_libc

# If this is using newlib, without having the headers available now,
# then define inhibit_libc in LIBGCC2_CFLAGS.
# This prevents libgcc2 from containing any code which requires libc
# support.
inhibit_libc=false
if { { test x$host != x$target && test "x$with_sysroot" = x ; } ||
       test x$with_newlib = xyes ; } &&
     { test "x$with_headers" = x || test "x$with_headers" = xno ; } ; then
       inhibit_libc=true
fi
AC_SUBST(inhibit_libc)

# When building gcc with a cross-compiler, we need to adjust things so
# that the generator programs are still built with the native compiler.
# Also, we cannot run fixincludes or fix-header.

# These are the normal (build=host) settings:
CC_FOR_BUILD='$(CC)'		AC_SUBST(CC_FOR_BUILD)
BUILD_CFLAGS='$(ALL_CFLAGS)'	AC_SUBST(BUILD_CFLAGS)
STMP_FIXINC=stmp-fixinc		AC_SUBST(STMP_FIXINC)

# Possibly disable fixproto, on a per-target basis.
case ${use_fixproto} in
  no)
    STMP_FIXPROTO=
    ;;
  yes)
    STMP_FIXPROTO=stmp-fixproto
    ;;
esac
AC_SUBST(STMP_FIXPROTO)

# And these apply if build != host, or we are generating coverage data
if test x$build != x$host || test "x$coverage_flags" != x
then
    BUILD_CFLAGS='$(INTERNAL_CFLAGS) $(T_CFLAGS) $(CFLAGS_FOR_BUILD)'

    if test "x$TARGET_SYSTEM_ROOT" = x; then
	if test "x$STMP_FIXPROTO" != x; then
	  STMP_FIXPROTO=stmp-install-fixproto
	fi
    fi
fi

# Expand extra_headers to include complete path.
# This substitutes for lots of t-* files.
extra_headers_list=
# Prepend $(srcdir)/config/${cpu_type}/ to every entry in extra_headers.
for file in ${extra_headers} ; do
  extra_headers_list="${extra_headers_list} \$(srcdir)/config/${cpu_type}/${file}"
done

# Define collect2 in Makefile.
case $host_can_use_collect2 in
  no) collect2= ;;
  *) collect2='collect2$(exeext)' ;;
esac
AC_SUBST([collect2])

# Add a definition of USE_COLLECT2 if system wants one.
case $use_collect2 in
  no) use_collect2= ;;
  "") ;;
  *) 
    host_xm_defines="${host_xm_defines} USE_COLLECT2"
    xm_defines="${xm_defines} USE_COLLECT2"
    case $host_can_use_collect2 in
      no)
        AC_MSG_ERROR([collect2 is required but cannot be built on this system])
        ;;
    esac
    ;;
esac

# ---------------------------
# Assembler & linker features
# ---------------------------

# Identify the assembler which will work hand-in-glove with the newly
# built GCC, so that we can examine its features.  This is the assembler
# which will be driven by the driver program.
#
# If build != host, and we aren't building gas in-tree, we identify a
# build->target assembler and hope that it will have the same features
# as the host->target assembler we'll be using.
gcc_cv_gas_major_version=
gcc_cv_gas_minor_version=
gcc_cv_as_gas_srcdir=`echo $srcdir | sed -e 's,/gcc$,,'`/gas

m4_pattern_allow([AS_FOR_TARGET])dnl
AS_VAR_SET_IF(gcc_cv_as,, [
if test -x "$DEFAULT_ASSEMBLER"; then
	gcc_cv_as="$DEFAULT_ASSEMBLER"
elif test -f $gcc_cv_as_gas_srcdir/configure.in \
     && test -f ../gas/Makefile \
     && test x$build = x$host; then
	gcc_cv_as=../gas/as-new$build_exeext
elif test -x as$build_exeext; then
	# Build using assembler in the current directory.
	gcc_cv_as=./as$build_exeext
elif test -x $AS_FOR_TARGET; then
        gcc_cv_as="$AS_FOR_TARGET"
else
        AC_PATH_PROG(gcc_cv_as, $AS_FOR_TARGET)
fi])

ORIGINAL_AS_FOR_TARGET=$gcc_cv_as
AC_SUBST(ORIGINAL_AS_FOR_TARGET)

AC_MSG_CHECKING(what assembler to use)
if test "$gcc_cv_as" = ../gas/as-new$build_exeext; then
  # Single tree build which includes gas.  We want to prefer it
  # over whatever linker top-level may have detected, since
  # we'll use what we're building after installation anyway.
  AC_MSG_RESULT(newly built gas)
  in_tree_gas=yes
  _gcc_COMPUTE_GAS_VERSION
  in_tree_gas_is_elf=no
  if grep 'obj_format = elf' ../gas/Makefile > /dev/null \
     || (grep 'obj_format = multi' ../gas/Makefile \
         && grep 'extra_objects =.* obj-elf' ../gas/Makefile) > /dev/null
  then
    in_tree_gas_is_elf=yes
  fi
else
  AC_MSG_RESULT($gcc_cv_as)
  in_tree_gas=no
fi

# Identify the linker which will work hand-in-glove with the newly
# built GCC, so that we can examine its features.  This is the linker
# which will be driven by the driver program.
#
# If build != host, and we aren't building gas in-tree, we identify a
# build->target linker and hope that it will have the same features
# as the host->target linker we'll be using.
gcc_cv_gld_major_version=
gcc_cv_gld_minor_version=
gcc_cv_ld_gld_srcdir=`echo $srcdir | sed -e 's,/gcc$,,'`/ld
gcc_cv_ld_bfd_srcdir=`echo $srcdir | sed -e 's,/gcc$,,'`/bfd

AS_VAR_SET_IF(gcc_cv_ld,, [
if test -x "$DEFAULT_LINKER"; then
	gcc_cv_ld="$DEFAULT_LINKER"
elif test -f $gcc_cv_ld_gld_srcdir/configure.in \
     && test -f ../ld/Makefile \
     && test x$build = x$host; then
	gcc_cv_ld=../ld/ld-new$build_exeext
elif test -x collect-ld$build_exeext; then
	# Build using linker in the current directory.
	gcc_cv_ld=./collect-ld$build_exeext
elif test -x $LD_FOR_TARGET; then
        gcc_cv_ld="$LD_FOR_TARGET"
else
        AC_PATH_PROG(gcc_cv_ld, $LD_FOR_TARGET)
fi])

ORIGINAL_LD_FOR_TARGET=$gcc_cv_ld
AC_SUBST(ORIGINAL_LD_FOR_TARGET)

AC_MSG_CHECKING(what linker to use)
if test "$gcc_cv_ld" = ../ld/ld-new$build_exeext; then
	# Single tree build which includes ld.  We want to prefer it
	# over whatever linker top-level may have detected, since
	# we'll use what we're building after installation anyway.
	AC_MSG_RESULT(newly built ld)
	in_tree_ld=yes
	in_tree_ld_is_elf=no
	if (grep 'EMUL = .*elf' ../ld/Makefile \
	    || grep 'EMUL = .*linux' ../ld/Makefile \
	    || grep 'EMUL = .*lynx' ../ld/Makefile) > /dev/null; then
	  in_tree_ld_is_elf=yes
	fi
	for f in $gcc_cv_ld_bfd_srcdir/configure $gcc_cv_ld_gld_srcdir/configure $gcc_cv_ld_gld_srcdir/configure.in $gcc_cv_ld_gld_srcdir/Makefile.in
	do
changequote(,)dnl
		gcc_cv_gld_version=`sed -n -e 's/^[ 	]*\(VERSION=[0-9]*\.[0-9]*.*\)/\1/p' < $f`
		if test x$gcc_cv_gld_version != x; then
			break
		fi
	done
	gcc_cv_gld_major_version=`expr "$gcc_cv_gld_version" : "VERSION=\([0-9]*\)"`
	gcc_cv_gld_minor_version=`expr "$gcc_cv_gld_version" : "VERSION=[0-9]*\.\([0-9]*\)"`
changequote([,])dnl
else
	AC_MSG_RESULT($gcc_cv_ld)
	in_tree_ld=no
fi

# Figure out what nm we will be using.
gcc_cv_binutils_srcdir=`echo $srcdir | sed -e 's,/gcc$,,'`/binutils
AS_VAR_SET_IF(gcc_cv_nm,, [
if test -f $gcc_cv_binutils_srcdir/configure.in \
     && test -f ../binutils/Makefile \
     && test x$build = x$host; then
	gcc_cv_nm=../binutils/nm-new$build_exeext
elif test -x nm$build_exeext; then
	gcc_cv_nm=./nm$build_exeext
elif test -x $NM_FOR_TARGET; then
        gcc_cv_nm="$NM_FOR_TARGET"
else
        AC_PATH_PROG(gcc_cv_nm, $NM_FOR_TARGET)
fi])

AC_MSG_CHECKING(what nm to use)
if test "$gcc_cv_nm" = ../binutils/nm-new$build_exeext; then
	# Single tree build which includes binutils.
	AC_MSG_RESULT(newly built nm)
	in_tree_nm=yes
else
	AC_MSG_RESULT($gcc_cv_nm)
	in_tree_nm=no
fi

ORIGINAL_NM_FOR_TARGET=$gcc_cv_nm
AC_SUBST(ORIGINAL_NM_FOR_TARGET)

# Figure out what objdump we will be using.
AS_VAR_SET_IF(gcc_cv_objdump,, [
if test -f $gcc_cv_binutils_srcdir/configure.in \
     && test -f ../binutils/Makefile \
     && test x$build = x$host; then
	# Single tree build which includes binutils.
	gcc_cv_objdump=../binutils/objdump$build_exeext
elif test -x objdump$build_exeext; then
	gcc_cv_objdump=./objdump$build_exeext
elif test -x $OBJDUMP_FOR_TARGET; then
        gcc_cv_objdump="$OBJDUMP_FOR_TARGET"
else
        AC_PATH_PROG(gcc_cv_objdump, $OBJDUMP_FOR_TARGET)
fi])

AC_MSG_CHECKING(what objdump to use)
if test "$gcc_cv_objdump" = ../binutils/objdump$build_exeext; then
	# Single tree build which includes binutils.
	AC_MSG_RESULT(newly built objdump)
elif test x$gcc_cv_objdump = x; then
	AC_MSG_RESULT(not found)
else
	AC_MSG_RESULT($gcc_cv_objdump)
fi

# Figure out what assembler alignment features are present.
gcc_GAS_CHECK_FEATURE([.balign and .p2align], gcc_cv_as_balign_and_p2align,
 [2,6,0],,
[.balign 4
.p2align 2],,
[AC_DEFINE(HAVE_GAS_BALIGN_AND_P2ALIGN, 1,
  [Define if your assembler supports .balign and .p2align.])])

gcc_GAS_CHECK_FEATURE([.p2align with maximum skip], gcc_cv_as_max_skip_p2align,
 [2,8,0],,
 [.p2align 4,,7],,
[AC_DEFINE(HAVE_GAS_MAX_SKIP_P2ALIGN, 1,
  [Define if your assembler supports specifying the maximum number
   of bytes to skip when using the GAS .p2align command.])])

gcc_GAS_CHECK_FEATURE([working .subsection -1], gcc_cv_as_subsection_m1,
 [elf,2,9,0],,
 [conftest_label1: .word 0
.subsection -1
conftest_label2: .word 0
.previous],
 [if test x$gcc_cv_nm != x; then
    $gcc_cv_nm conftest.o | grep conftest_label1 > conftest.nm1
    $gcc_cv_nm conftest.o | grep conftest_label2 | sed -e 's/label2/label1/' > conftest.nm2
    if cmp conftest.nm1 conftest.nm2 > /dev/null 2>&1
    then :
    else gcc_cv_as_subsection_m1=yes
    fi
    rm -f conftest.nm1 conftest.nm2
  fi],
 [AC_DEFINE(HAVE_GAS_SUBSECTION_ORDERING, 1,
  [Define if your assembler supports .subsection and .subsection -1 starts
   emitting at the beginning of your section.])])

gcc_GAS_CHECK_FEATURE([.weak], gcc_cv_as_weak,
 [2,2,0],,
 [	.weak foobar],,
[AC_DEFINE(HAVE_GAS_WEAK, 1, [Define if your assembler supports .weak.])])

gcc_GAS_CHECK_FEATURE([.weakref], gcc_cv_as_weakref,
 [2,17,0],,
 [	.weakref foobar, barfnot],,
[AC_DEFINE(HAVE_GAS_WEAKREF, 1, [Define if your assembler supports .weakref.])])

gcc_GAS_CHECK_FEATURE([.nsubspa comdat], gcc_cv_as_nsubspa_comdat,
 [2,15,91],,
 [	.SPACE $TEXT$
	.NSUBSPA $CODE$,COMDAT],,
[AC_DEFINE(HAVE_GAS_NSUBSPA_COMDAT, 1, [Define if your assembler supports .nsubspa comdat option.])])

# .hidden needs to be supported in both the assembler and the linker,
# because GNU LD versions before 2.12.1 have buggy support for STV_HIDDEN.
# This is irritatingly difficult to feature test for; we have to check the
# date string after the version number.  If we've got an in-tree
# ld, we don't know its patchlevel version, so we set the baseline at 2.13
# to be safe.
# The gcc_GAS_CHECK_FEATURE call just sets a cache variable.
gcc_GAS_CHECK_FEATURE([.hidden], gcc_cv_as_hidden,
 [elf,2,13,0],,
[	.hidden foobar
foobar:])

AC_CACHE_CHECK(linker for .hidden support, gcc_cv_ld_hidden,
[if test $in_tree_ld = yes ; then
  gcc_cv_ld_hidden=no
  if test "$gcc_cv_gld_major_version" -eq 2 -a "$gcc_cv_gld_minor_version" -ge 13 -o "$gcc_cv_gld_major_version" -gt 2 \
     && test $in_tree_ld_is_elf = yes; then
     gcc_cv_ld_hidden=yes
  fi
else
  gcc_cv_ld_hidden=yes
  ld_ver=`$gcc_cv_ld --version 2>/dev/null | sed 1q`
  if echo "$ld_ver" | grep GNU > /dev/null; then
changequote(,)dnl
    ld_vers=`echo $ld_ver | sed -n \
	-e 's,^.*[	 ]\([0-9][0-9]*\.[0-9][0-9]*\)$,\1,p' \
	-e 's,^.*[	 ]\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)$,\1,p' \
	-e 's,^.*[	 ]\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)$,\1,p' \
	-e 's,^.*[	 ]\([0-9][0-9]*\.[0-9][0-9]*\)[	 ].*$,\1,p' \
	-e 's,^.*[	 ]\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)[	 ].*$,\1,p' \
	-e 's,^.*[ 	 ]\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)[ 	].*$,\1,p' \
	-e 's,^.*[	 ]\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)[	 -].*$,\1,p'`
    ld_date=`echo $ld_ver | sed -n 's,^.*\([2-9][0-9][0-9][0-9]\)[-]*\([01][0-9]\)[-]*\([0-3][0-9]\).*$,\1\2\3,p'`
    ld_vers_major=`expr "$ld_vers" : '\([0-9]*\)'`
    ld_vers_minor=`expr "$ld_vers" : '[0-9]*\.\([0-9]*\)'`
    ld_vers_patch=`expr "$ld_vers" : '[0-9]*\.[0-9]*\.\([0-9]*\)'`
    if test 0"$ld_date" -lt 20020404; then
      if test -n "$ld_date"; then
	# If there was date string, but was earlier than 2002-04-04, fail
	gcc_cv_ld_hidden=no
      elif test -z "$ld_vers"; then
	# If there was no date string nor ld version number, something is wrong
	gcc_cv_ld_hidden=no
      else
	test -z "$ld_vers_patch" && ld_vers_patch=0
	if test "$ld_vers_major" -lt 2; then
	  gcc_cv_ld_hidden=no
	elif test "$ld_vers_major" -eq 2 -a "$ld_vers_minor" -lt 12; then
	  gcc_cv_ld_hidden="no"
	elif test "$ld_vers_major" -eq 2 -a "$ld_vers_minor" -eq 12 -a "$ld_vers_patch" -eq 0; then
	  gcc_cv_ld_hidden=no
	fi
      fi
changequote([,])dnl
    fi
  else
    case "${target}" in
      hppa64*-*-hpux* | ia64*-*-hpux*)
	gcc_cv_ld_hidden=yes
	;;
      *)
	gcc_cv_ld_hidden=no
	;;
    esac
  fi
fi])
libgcc_visibility=no
AC_SUBST(libgcc_visibility)
GCC_TARGET_TEMPLATE([HAVE_GAS_HIDDEN])
if test $gcc_cv_as_hidden = yes && test $gcc_cv_ld_hidden = yes; then
  libgcc_visibility=yes
  AC_DEFINE(HAVE_GAS_HIDDEN, 1,
  [Define if your assembler and linker support .hidden.])
fi

# Check if we have .[us]leb128, and support symbol arithmetic with it.
gcc_GAS_CHECK_FEATURE([.sleb128 and .uleb128], gcc_cv_as_leb128,
  [elf,2,11,0],,
[	.data
	.uleb128 L2 - L1
L1:
	.uleb128 1280
	.sleb128 -1010
L2:],
 [# GAS versions before 2.11 do not support uleb128,
  # despite appearing to.
  # ??? There exists an elf-specific test that will crash
  # the assembler.  Perhaps it's better to figure out whether
  # arbitrary sections are supported and try the test.
  as_ver=`$gcc_cv_as --version 2>/dev/null | sed 1q`
  if echo "$as_ver" | grep GNU > /dev/null; then
changequote(,)dnl
    as_ver=`echo $as_ver | sed -e 's/GNU assembler \([0-9.][0-9.]*\).*/\1/'`
    as_major=`echo $as_ver | sed 's/\..*//'`
    as_minor=`echo $as_ver | sed 's/[^.]*\.\([0-9]*\).*/\1/'`
changequote([,])dnl
    if test $as_major -eq 2 && test $as_minor -lt 11
    then :
    else gcc_cv_as_leb128=yes
    fi
  fi],
  [AC_DEFINE(HAVE_AS_LEB128, 1,
    [Define if your assembler supports .sleb128 and .uleb128.])])

# GAS versions up to and including 2.11.0 may mis-optimize
# .eh_frame data.
gcc_GAS_CHECK_FEATURE(eh_frame optimization, gcc_cv_as_eh_frame,
  [elf,2,12,0],,
[	.text
.LFB1:
	.4byte	0
.L1:
	.4byte	0
.LFE1:
	.section	.eh_frame,"aw",@progbits
__FRAME_BEGIN__:
	.4byte	.LECIE1-.LSCIE1
.LSCIE1:
	.4byte	0x0
	.byte	0x1
	.ascii "z\0"
	.byte	0x1
	.byte	0x78
	.byte	0x1a
	.byte	0x0
	.byte	0x4
	.4byte	1
	.p2align 1
.LECIE1:
.LSFDE1:
	.4byte	.LEFDE1-.LASFDE1
.LASFDE1:
	.4byte	.LASFDE1-__FRAME_BEGIN__
	.4byte	.LFB1
	.4byte	.LFE1-.LFB1
	.byte	0x4
	.4byte	.LFE1-.LFB1
	.byte	0x4
	.4byte	.L1-.LFB1
.LEFDE1:],
[  dnl # For autoconf 2.5x, must protect trailing spaces with @&t@.
cat > conftest.lit <<EOF
 0000 10000000 00000000 017a0001 781a0004  .........z..x...
 0010 01000000 12000000 18000000 00000000  ................
 0020 08000000 04080000 0044               .........D      @&t@
EOF
cat > conftest.big <<EOF
 0000 00000010 00000000 017a0001 781a0004  .........z..x...
 0010 00000001 00000012 00000018 00000000  ................
 0020 00000008 04000000 0844               .........D      @&t@
EOF
  # If the assembler didn't choke, and we can objdump,
  # and we got the correct data, then succeed.
  # The text in the here-document typically retains its unix-style line
  # endings, while the output of objdump will use host line endings.
  # Therefore, use diff -b for the comparisons.
  if test x$gcc_cv_objdump != x \
  && $gcc_cv_objdump -s -j .eh_frame conftest.o 2>/dev/null \
     | tail -3 > conftest.got \
  && { diff -b conftest.lit conftest.got > /dev/null 2>&1 \
    || diff -b conftest.big conftest.got > /dev/null 2>&1; }
  then
    gcc_cv_as_eh_frame=yes
  elif AC_TRY_COMMAND($gcc_cv_as -o conftest.o --traditional-format /dev/null); then
    gcc_cv_as_eh_frame=buggy
  else
    # Uh oh, what do we do now?
    gcc_cv_as_eh_frame=no
  fi])

if test $gcc_cv_as_eh_frame = buggy; then
  AC_DEFINE(USE_AS_TRADITIONAL_FORMAT, 1,
  [Define if your assembler mis-optimizes .eh_frame data.])
fi

gcc_GAS_CHECK_FEATURE(section merging support, gcc_cv_as_shf_merge,
 [elf,2,12,0], [--fatal-warnings],
 [.section .rodata.str, "aMS", @progbits, 1])
if test $gcc_cv_as_shf_merge = no; then
  gcc_GAS_CHECK_FEATURE(section merging support, gcc_cv_as_shf_merge,
    [elf,2,12,0], [--fatal-warnings],
    [.section .rodata.str, "aMS", %progbits, 1])
fi
AC_DEFINE_UNQUOTED(HAVE_GAS_SHF_MERGE,
  [`if test $gcc_cv_as_shf_merge = yes; then echo 1; else echo 0; fi`],
[Define 0/1 if your assembler supports marking sections with SHF_MERGE flag.])

gcc_GAS_CHECK_FEATURE(COMDAT group support, gcc_cv_as_comdat_group,
 [elf,2,16,0], [--fatal-warnings],
 [.section .text,"axG",@progbits,.foo,comdat])
if test $gcc_cv_as_comdat_group = yes; then
  gcc_cv_as_comdat_group_percent=no
else
 gcc_GAS_CHECK_FEATURE(COMDAT group support, gcc_cv_as_comdat_group_percent,
   [elf,2,16,0], [--fatal-warnings],
   [.section .text,"axG",%progbits,.foo,comdat])
fi
if test $in_tree_ld != yes && test x"$ld_vers" != x; then
  comdat_group=yes
  if test 0"$ld_date" -lt 20050308; then
    if test -n "$ld_date"; then
      # If there was date string, but was earlier than 2005-03-08, fail
      comdat_group=no
    elif test "$ld_vers_major" -lt 2; then
      comdat_group=no
    elif test "$ld_vers_major" -eq 2 -a "$ld_vers_minor" -lt 16; then
      comdat_group=no
    fi
  fi
else
  # assume linkers other than GNU ld don't support COMDAT group
  comdat_group=no
fi
if test $comdat_group = no; then
  gcc_cv_as_comdat_group=no
  gcc_cv_as_comdat_group_percent=no
fi
AC_DEFINE_UNQUOTED(HAVE_COMDAT_GROUP,
  [`if test $gcc_cv_as_comdat_group = yes || test $gcc_cv_as_comdat_group_percent = yes; then echo 1; else echo 0; fi`],
[Define 0/1 if your assembler and linker support COMDAT groups.])

# Thread-local storage - the check is heavily parametrized.
conftest_s=
tls_first_major=
tls_first_minor=
tls_as_opt=
case "$target" in
changequote(,)dnl
  alpha*-*-*)
    conftest_s='
	.section ".tdata","awT",@progbits
foo:	.long	25
	.text
	ldq	$27,__tls_get_addr($29)		!literal!1
	lda	$16,foo($29)			!tlsgd!1
	jsr	$26,($27),__tls_get_addr	!lituse_tlsgd!1
	ldq	$27,__tls_get_addr($29)		!literal!2
	lda	$16,foo($29)			!tlsldm!2
	jsr	$26,($27),__tls_get_addr	!lituse_tlsldm!2
	ldq	$1,foo($29)			!gotdtprel
	ldah	$2,foo($29)			!dtprelhi
	lda	$3,foo($2)			!dtprello
	lda	$4,foo($29)			!dtprel
	ldq	$1,foo($29)			!gottprel
	ldah	$2,foo($29)			!tprelhi
	lda	$3,foo($2)			!tprello
	lda	$4,foo($29)			!tprel'
	tls_first_major=2
	tls_first_minor=13
	tls_as_opt=--fatal-warnings
	;;
  frv*-*-*)
    conftest_s='
	.section ".tdata","awT",@progbits
x:      .long   25
        .text
        call    #gettlsoff(x)'
	tls_first_major=2
	tls_first_minor=14
	;;
  hppa*-*-linux*)
    conftest_s='
t1:	.reg	%r20
t2:	.reg	%r21
gp:	.reg	%r19
	.section ".tdata","awT",@progbits
foo:	.long	25
	.text
	.align	4
	addil LT%foo-$tls_gdidx$,gp
	ldo RT%foo-$tls_gdidx$(%r1),%arg0
	b __tls_get_addr
	nop 		
	addil LT%foo-$tls_ldidx$,gp
	b __tls_get_addr
	ldo RT%foo-$tls_ldidx$(%r1),%arg0
	addil LR%foo-$tls_dtpoff$,%ret0
	ldo RR%foo-$tls_dtpoff$(%r1),%t1
	mfctl %cr27,%t1 		
	addil LT%foo-$tls_ieoff$,gp
	ldw RT%foo-$tls_ieoff$(%r1),%t2
	add %t1,%t2,%t3 		
	mfctl %cr27,%t1 		
	addil LR%foo-$tls_leoff$,%t1
	ldo RR%foo-$tls_leoff$(%r1),%t2'
	tls_first_major=2
	tls_first_minor=15
	tls_as_opt=--fatal-warnings
	;;
  arm*-*-*)
    conftest_s='
	.section ".tdata","awT",%progbits
foo:	.long	25
	.text
.word foo(gottpoff)
.word foo(tpoff)
.word foo(tlsgd)
.word foo(tlsldm)
.word foo(tlsldo)'
	tls_first_major=2
	tls_first_minor=17
	;;
  i[34567]86-*-*)
    conftest_s='
	.section ".tdata","awT",@progbits
foo:	.long	25
	.text
	movl	%gs:0, %eax
	leal	foo@TLSGD(,%ebx,1), %eax
	leal	foo@TLSLDM(%ebx), %eax
	leal	foo@DTPOFF(%eax), %edx
	movl	foo@GOTTPOFF(%ebx), %eax
	subl	foo@GOTTPOFF(%ebx), %eax
	addl	foo@GOTNTPOFF(%ebx), %eax
	movl	foo@INDNTPOFF, %eax
	movl	$foo@TPOFF, %eax
	subl	$foo@TPOFF, %eax
	leal	foo@NTPOFF(%ecx), %eax'
	tls_first_major=2
	tls_first_minor=14
	tls_as_opt=--fatal-warnings
	;;
  x86_64-*-*)
    conftest_s='
	.section ".tdata","awT",@progbits
foo:	.long	25
	.text
	movq	%fs:0, %rax
	leaq	foo@TLSGD(%rip), %rdi
	leaq	foo@TLSLD(%rip), %rdi
	leaq	foo@DTPOFF(%rax), %rdx
	movq	foo@GOTTPOFF(%rip), %rax
	movq	$foo@TPOFF, %rax'
	tls_first_major=2
	tls_first_minor=14
	tls_as_opt=--fatal-warnings
	;;
  ia64-*-*)
    conftest_s='
	.section ".tdata","awT",@progbits
foo:	data8	25
	.text
	addl	r16 = @ltoff(@dtpmod(foo#)), gp
	addl	r17 = @ltoff(@dtprel(foo#)), gp
	addl	r18 = @ltoff(@tprel(foo#)), gp
	addl	r19 = @dtprel(foo#), gp
	adds	r21 = @dtprel(foo#), r13
	movl	r23 = @dtprel(foo#)
	addl	r20 = @tprel(foo#), gp
	adds	r22 = @tprel(foo#), r13
	movl	r24 = @tprel(foo#)'
	tls_first_major=2
	tls_first_minor=13
	tls_as_opt=--fatal-warnings
	;;
  mips*-*-*)
    conftest_s='
	.section .tdata,"awT",@progbits
x:
	.word 2
	.text
	addiu $4, $28, %tlsgd(x)
	addiu $4, $28, %tlsldm(x)
	lui $4, %dtprel_hi(x)
	addiu $4, $4, %dtprel_lo(x)
	lw $4, %gottprel(x)($28)
	lui $4, %tprel_hi(x)
	addiu $4, $4, %tprel_lo(x)'
	tls_first_major=2
	tls_first_minor=16
	tls_as_opt='-32 --fatal-warnings'
	;;
  powerpc-*-*)
    conftest_s='
	.section ".tdata","awT",@progbits
	.align 2
ld0:	.space 4
ld1:	.space 4
x1:	.space 4
x2:	.space 4
x3:	.space 4
	.text
	addi 3,31,ld0@got@tlsgd
	bl __tls_get_addr
	addi 3,31,x1@got@tlsld
	bl __tls_get_addr
	addi 9,3,x1@dtprel
	addis 9,3,x2@dtprel@ha
	addi 9,9,x2@dtprel@l
	lwz 9,x3@got@tprel(31)
	add 9,9,x@tls
	addi 9,2,x1@tprel
	addis 9,2,x2@tprel@ha
	addi 9,9,x2@tprel@l'
	tls_first_major=2
	tls_first_minor=14
	tls_as_opt="-a32 --fatal-warnings"
	;;
  powerpc64-*-*)
    conftest_s='
	.section ".tdata","awT",@progbits
	.align 3
ld0:	.space 8
ld1:	.space 8
x1:	.space 8
x2:	.space 8
x3:	.space 8
	.text
	addi 3,2,ld0@got@tlsgd
	bl .__tls_get_addr
	nop
	addi 3,2,ld1@toc
	bl .__tls_get_addr
	nop
	addi 3,2,x1@got@tlsld
	bl .__tls_get_addr
	nop
	addi 9,3,x1@dtprel
	bl .__tls_get_addr
	nop
	addis 9,3,x2@dtprel@ha
	addi 9,9,x2@dtprel@l
	bl .__tls_get_addr
	nop
	ld 9,x3@got@dtprel(2)
	add 9,9,3
	bl .__tls_get_addr
	nop'
	tls_first_major=2
	tls_first_minor=14
	tls_as_opt="-a64 --fatal-warnings"
	;;
  s390-*-*)
    conftest_s='
	.section ".tdata","awT",@progbits
foo:	.long	25
	.text
	.long	foo@TLSGD
	.long	foo@TLSLDM
	.long	foo@DTPOFF
	.long	foo@NTPOFF
	.long	foo@GOTNTPOFF
	.long	foo@INDNTPOFF
	l	%r1,foo@GOTNTPOFF(%r12)
	l	%r1,0(%r1):tls_load:foo
	bas	%r14,0(%r1,%r13):tls_gdcall:foo
	bas	%r14,0(%r1,%r13):tls_ldcall:foo'
	tls_first_major=2
	tls_first_minor=14
	tls_as_opt="-m31 --fatal-warnings"
	;;
  s390x-*-*)
    conftest_s='
	.section ".tdata","awT",@progbits
foo:	.long	25
	.text
	.quad	foo@TLSGD
	.quad	foo@TLSLDM
	.quad	foo@DTPOFF
	.quad	foo@NTPOFF
	.quad	foo@GOTNTPOFF
	lg	%r1,foo@GOTNTPOFF(%r12)
	larl	%r1,foo@INDNTPOFF
	brasl	%r14,__tls_get_offset@PLT:tls_gdcall:foo
	brasl	%r14,__tls_get_offset@PLT:tls_ldcall:foo'
	tls_first_major=2
	tls_first_minor=14
	tls_as_opt="-m64 -Aesame --fatal-warnings"
	;;
  sh-*-* | sh[34]-*-*)
    conftest_s='
	.section ".tdata","awT",@progbits
foo:	.long	25
	.text
	.long	foo@TLSGD
	.long	foo@TLSLDM
	.long	foo@DTPOFF
	.long	foo@GOTTPOFF
	.long	foo@TPOFF'
	tls_first_major=2
	tls_first_minor=13
	tls_as_opt=--fatal-warnings
	;;
  sparc*-*-*)
    case "$target" in
      sparc*-sun-solaris2.[56789]*)
	# TLS was introduced in the Solaris 9 4/04 release but
	# we do not enable it by default on Solaris 9 either.
	if test "x$enable_tls" = xyes ; then
	  on_solaris=yes
	else
	  enable_tls=no;
	fi
	;;
      sparc*-sun-solaris2.*)
	on_solaris=yes
	;;
      *)
	on_solaris=no
	;;
    esac
    if test x$on_solaris = xyes && test x$gas_flag = xno; then
      conftest_s='
	.section ".tdata",#alloc,#write,#tls
foo:	.long	25
	.text
	sethi	%tgd_hi22(foo), %o0
	add	%o0, %tgd_lo10(foo), %o1
	add	%l7, %o1, %o0, %tgd_add(foo)
	call	__tls_get_addr, %tgd_call(foo)
	sethi	%tldm_hi22(foo), %l1
	add	%l1, %tldm_lo10(foo), %l2
	add	%l7, %l2, %o0, %tldm_add(foo)
	call	__tls_get_addr, %tldm_call(foo)
	sethi	%tldo_hix22(foo), %l3
	xor	%l3, %tldo_lox10(foo), %l4
	add	%o0, %l4, %l5, %tldo_add(foo)
	sethi	%tie_hi22(foo), %o3
	add	%o3, %tie_lo10(foo), %o3
	ld	[%l7 + %o3], %o2, %tie_ld(foo)
	add	%g7, %o2, %o4, %tie_add(foo)
	sethi	%tle_hix22(foo), %l1
	xor	%l1, %tle_lox10(foo), %o5
	ld	[%g7 + %o5], %o1'
	tls_first_major=0
	tls_first_minor=0
    else
      conftest_s='
	.section ".tdata","awT",@progbits
foo:	.long	25
	.text
	sethi	%tgd_hi22(foo), %o0
	add	%o0, %tgd_lo10(foo), %o1
	add	%l7, %o1, %o0, %tgd_add(foo)
	call	__tls_get_addr, %tgd_call(foo)
	sethi	%tldm_hi22(foo), %l1
	add	%l1, %tldm_lo10(foo), %l2
	add	%l7, %l2, %o0, %tldm_add(foo)
	call	__tls_get_addr, %tldm_call(foo)
	sethi	%tldo_hix22(foo), %l3
	xor	%l3, %tldo_lox10(foo), %l4
	add	%o0, %l4, %l5, %tldo_add(foo)
	sethi	%tie_hi22(foo), %o3
	add	%o3, %tie_lo10(foo), %o3
	ld	[%l7 + %o3], %o2, %tie_ld(foo)
	add	%g7, %o2, %o4, %tie_add(foo)
	sethi	%tle_hix22(foo), %l1
	xor	%l1, %tle_lox10(foo), %o5
	ld	[%g7 + %o5], %o1'
	tls_first_major=2
	tls_first_minor=14
	tls_as_opt="-32 --fatal-warnings"
      fi
	;;
changequote([,])dnl
esac
set_have_as_tls=no
if test "x$enable_tls" = xno ; then
  : # TLS explicitly disabled.
elif test "x$enable_tls" = xyes ; then
  set_have_as_tls=yes # TLS explicitly enabled.
elif test -z "$tls_first_major"; then
  : # If we don't have a check, assume no support.
else
  gcc_GAS_CHECK_FEATURE(thread-local storage support, gcc_cv_as_tls,
  [$tls_first_major,$tls_first_minor,0], [$tls_as_opt], [$conftest_s],,
  [set_have_as_tls=yes])
fi
if test $set_have_as_tls = yes ; then
  AC_DEFINE(HAVE_AS_TLS, 1,
	    [Define if your assembler supports thread-local storage.])
fi

# Target-specific assembler checks.

AC_MSG_CHECKING(linker -Bstatic/-Bdynamic option)
gcc_cv_ld_static_dynamic=no
if test $in_tree_ld = yes ; then
  if test "$gcc_cv_gld_major_version" -eq 2 -a "$gcc_cv_gld_minor_version" -ge 10; then
    gcc_cv_ld_static_dynamic=yes
  fi
elif test x$gcc_cv_ld != x; then
	# Check if linker supports -Bstatic/-Bdynamic option
	if $gcc_cv_ld --help 2>/dev/null | grep -- -Bstatic > /dev/null \
	  && $gcc_cv_ld --help 2>/dev/null | grep -- -Bdynamic > /dev/null; then
		gcc_cv_ld_static_dynamic=yes
	fi
fi
if test x"$gcc_cv_ld_static_dynamic" = xyes; then
	AC_DEFINE(HAVE_LD_STATIC_DYNAMIC, 1,
[Define if your linker supports -Bstatic/-Bdynamic option.])
fi
AC_MSG_RESULT($gcc_cv_ld_static_dynamic)

if test x"$demangler_in_ld" = xyes; then
  AC_MSG_CHECKING(linker --demangle support)
  gcc_cv_ld_demangle=no
  if test $in_tree_ld = yes; then
    if test "$gcc_cv_gld_major_version" -eq 2 -a "$gcc_cv_gld_minor_version" -ge 14 -o "$gcc_cv_gld_major_version" -gt 2; then \
      gcc_cv_ld_demangle=yes
    fi
  elif test x$gcc_cv_ld != x -a x"$gnu_ld" = xyes; then
    # Check if the GNU linker supports --demangle option
    if $gcc_cv_ld --help 2>/dev/null | grep no-demangle > /dev/null; then
      gcc_cv_ld_demangle=yes
    fi
  fi
  if test x"$gcc_cv_ld_demangle" = xyes; then
    AC_DEFINE(HAVE_LD_DEMANGLE, 1,
[Define if your linker supports --demangle option.])
  fi
  AC_MSG_RESULT($gcc_cv_ld_demangle)
fi

case "$target" in
  # All TARGET_ABI_OSF targets.
  alpha*-*-osf* | alpha*-*-linux* | alpha*-*-*bsd*)
    gcc_GAS_CHECK_FEATURE([explicit relocation support],
	gcc_cv_as_alpha_explicit_relocs, [2,12,0],,
[	.set nomacro
	.text
	extbl	$3, $2, $3	!lituse_bytoff!1
	ldq	$2, a($29)	!literal!1
	ldq	$4, b($29)	!literal!2
	ldq_u	$3, 0($2)	!lituse_base!1
	ldq	$27, f($29)	!literal!5
	jsr	$26, ($27), f	!lituse_jsr!5
	ldah	$29, 0($26)	!gpdisp!3
	lda	$0, c($29)	!gprel
	ldah	$1, d($29)	!gprelhigh
	lda	$1, d($1)	!gprellow
	lda	$29, 0($29)	!gpdisp!3],,
    [AC_DEFINE(HAVE_AS_EXPLICIT_RELOCS, 1,
  [Define if your assembler supports explicit relocations.])])
    gcc_GAS_CHECK_FEATURE([jsrdirect relocation support],
	gcc_cv_as_alpha_jsrdirect_relocs, [2,16,90],,
[	.set nomacro
	.text
	ldq	$27, a($29)	!literal!1
	jsr	$26, ($27), a	!lituse_jsrdirect!1],,
    [AC_DEFINE(HAVE_AS_JSRDIRECT_RELOCS, 1,
  [Define if your assembler supports the lituse_jsrdirect relocation.])])
    ;;

  cris-*-*)
    gcc_GAS_CHECK_FEATURE([-no-mul-bug-abort option],
      gcc_cv_as_cris_no_mul_bug,[2,15,91],
      [-no-mul-bug-abort], [.text],,
      [AC_DEFINE(HAVE_AS_NO_MUL_BUG_ABORT_OPTION, 1,
		[Define if your assembler supports the -no-mul-bug-abort option.])])
    ;;

  sparc*-*-*)
    gcc_GAS_CHECK_FEATURE([.register], gcc_cv_as_sparc_register_op,,,
      [.register %g2, #scratch],,
      [AC_DEFINE(HAVE_AS_REGISTER_PSEUDO_OP, 1,
		[Define if your assembler supports .register.])])

    gcc_GAS_CHECK_FEATURE([-relax option], gcc_cv_as_sparc_relax,,
      [-relax], [.text],,
      [AC_DEFINE(HAVE_AS_RELAX_OPTION, 1,
		[Define if your assembler supports -relax option.])])

    gcc_GAS_CHECK_FEATURE([unaligned pcrel relocs],
      gcc_cv_as_sparc_ua_pcrel,,
      [-K PIC],
[.text
foo:
	nop
.data
.align 4
.byte 0
.uaword %r_disp32(foo)],
      [if test x$gcc_cv_ld != x \
       && $gcc_cv_ld -o conftest conftest.o -G > /dev/null 2>&1; then
	 gcc_cv_as_sparc_ua_pcrel=yes
       fi
       rm -f conftest],
      [AC_DEFINE(HAVE_AS_SPARC_UA_PCREL, 1,
		[Define if your assembler and linker support unaligned PC relative relocs.])

      gcc_GAS_CHECK_FEATURE([unaligned pcrel relocs against hidden symbols],
	gcc_cv_as_sparc_ua_pcrel_hidden,,
	[-K PIC],
[.data
.align 4
.byte 0x31
.uaword %r_disp32(foo)
.byte 0x32, 0x33, 0x34
.global foo
.hidden foo
foo:
.skip 4],
	[if test x$gcc_cv_ld != x && test x$gcc_cv_objdump != x \
	 && $gcc_cv_ld -o conftest conftest.o -G > /dev/null 2>&1 \
	 && $gcc_cv_objdump -s -j .data conftest 2> /dev/null \
	    | grep ' 31000000 07323334' > /dev/null 2>&1; then
	    if $gcc_cv_objdump -R conftest 2> /dev/null \
	       | grep 'DISP32' > /dev/null 2>&1; then
		:
	    else
		gcc_cv_as_sparc_ua_pcrel_hidden=yes
	    fi
	 fi
	 rm -f conftest],
	 [AC_DEFINE(HAVE_AS_SPARC_UA_PCREL_HIDDEN, 1,
		   [Define if your assembler and linker support unaligned PC relative relocs against hidden symbols.])])
    ]) # unaligned pcrel relocs

    gcc_GAS_CHECK_FEATURE([offsetable %lo()],
      gcc_cv_as_sparc_offsetable_lo10,,
      [-xarch=v9],
[.text
	or %g1, %lo(ab) + 12, %g1
	or %g1, %lo(ab + 12), %g1],
      [if test x$gcc_cv_objdump != x \
       && $gcc_cv_objdump -s -j .text conftest.o 2> /dev/null \
          | grep ' 82106000 82106000' > /dev/null 2>&1; then
	 gcc_cv_as_sparc_offsetable_lo10=yes
       fi],
       [AC_DEFINE(HAVE_AS_OFFSETABLE_LO10, 1,
	         [Define if your assembler supports offsetable %lo().])])
    ;;

changequote(,)dnl
  i[34567]86-*-* | x86_64-*-*)
changequote([,])dnl
    case $target_os in
      cygwin* | pe | mingw32*)
	# Used for DWARF 2 in PE
	gcc_GAS_CHECK_FEATURE([.secrel32 relocs],
	  gcc_cv_as_ix86_pe_secrel32,
	  [2,15,91],,
[.text
foo:	nop
.data
	.secrel32 foo],
	  [if test x$gcc_cv_ld != x \
	   && $gcc_cv_ld -o conftest conftest.o > /dev/null 2>&1; then
	     gcc_cv_as_ix86_pe_secrel32=yes
	   fi
	   rm -f conftest],
	  [AC_DEFINE(HAVE_GAS_PE_SECREL32_RELOC, 1,
	    [Define if your assembler and linker support 32-bit section relative relocs via '.secrel32 label'.])])
	;;
    esac

    gcc_GAS_CHECK_FEATURE([filds and fists mnemonics],
       gcc_cv_as_ix86_filds_fists,
      [2,9,0],, [filds mem; fists mem],,
      [AC_DEFINE(HAVE_GAS_FILDS_FISTS, 1,
        [Define if your assembler uses the new HImode fild and fist notation.])])

    gcc_GAS_CHECK_FEATURE([cmov syntax],
      gcc_cv_as_ix86_cmov_sun_syntax,,,
      [cmovl.l %edx, %eax],,
      [AC_DEFINE(HAVE_AS_IX86_CMOV_SUN_SYNTAX, 1,
        [Define if your assembler supports the Sun syntax for cmov.])])

    gcc_GAS_CHECK_FEATURE([ffreep mnemonic],
      gcc_cv_as_ix86_ffreep,,,
      [ffreep %st(1)],,
      [AC_DEFINE(HAVE_AS_IX86_FFREEP, 1,
        [Define if your assembler supports the ffreep mnemonic.])])

    # This one is used unconditionally by i386.[ch]; it is to be defined
    # to 1 if the feature is present, 0 otherwise.
    gcc_GAS_CHECK_FEATURE([GOTOFF in data],
        gcc_cv_as_ix86_gotoff_in_data, [2,11,0],,
[	.text
.L0:
	nop
	.data
	.long .L0@GOTOFF])
    AC_DEFINE_UNQUOTED(HAVE_AS_GOTOFF_IN_DATA,
      [`if test $gcc_cv_as_ix86_gotoff_in_data = yes; then echo 1; else echo 0; fi`],
      [Define true if the assembler supports '.long foo@GOTOFF'.])
    ;;

  ia64*-*-*)
    gcc_GAS_CHECK_FEATURE([ltoffx and ldxmov relocs],
	gcc_cv_as_ia64_ltoffx_ldxmov_relocs, [2,14,0],,
[	.text
	addl r15 = @ltoffx(x#), gp
	;;
	ld8.mov r16 = [[r15]], x#],,
    [AC_DEFINE(HAVE_AS_LTOFFX_LDXMOV_RELOCS, 1,
	  [Define if your assembler supports ltoffx and ldxmov relocations.])])

    ;;

  powerpc*-*-*)
    case $target in
      *-*-aix*) conftest_s='	.machine "pwr5"
	.csect .text[[PR]]
	mfcr 3,128';;
      *-*-darwin*)
	gcc_GAS_CHECK_FEATURE([.machine directive support],
	  gcc_cv_as_machine_directive,,,
	  [	.machine ppc7400])
	if test x$gcc_cv_as_machine_directive != xyes; then
	  echo "*** This target requires an assembler supporting \".machine\"" >&2
	  echo you can get it from: ftp://gcc.gnu.org/pub/gcc/infrastructure/cctools-528.5.dmg >&2
	  test x$build = x$target && exit 1
	fi
	conftest_s='	.text
	mfcr r3,128';;
      *) conftest_s='	.machine power4
	.text
	mfcr 3,128';;
    esac

    gcc_GAS_CHECK_FEATURE([mfcr field support],
      gcc_cv_as_powerpc_mfcrf, [2,14,0],,
      [$conftest_s],,
      [AC_DEFINE(HAVE_AS_MFCRF, 1,
	  [Define if your assembler supports mfcr field.])])

    case $target in
      *-*-aix*) conftest_s='	.machine "pwr5"
	.csect .text[[PR]]
	popcntb 3,3';;
      *) conftest_s='	.machine power5
	.text
	popcntb 3,3';;
    esac

    gcc_GAS_CHECK_FEATURE([popcntb support],
      gcc_cv_as_powerpc_popcntb, [2,17,0],,
      [$conftest_s],,
      [AC_DEFINE(HAVE_AS_POPCNTB, 1,
	  [Define if your assembler supports popcntb field.])])

    case $target in
      *-*-aix*) conftest_s='	.machine "pwr5x"
	.csect .text[[PR]]
	frin 1,1';;
      *) conftest_s='	.machine power5
	.text
	frin 1,1';;
    esac

    gcc_GAS_CHECK_FEATURE([fp round support],
      gcc_cv_as_powerpc_fprnd, [2,17,0],,
      [$conftest_s],,
      [AC_DEFINE(HAVE_AS_FPRND, 1,
	  [Define if your assembler supports fprnd.])])

    case $target in
      *-*-aix*) conftest_s='	.csect .text[[PR]]
LCF..0:
	addis 11,30,_GLOBAL_OFFSET_TABLE_-LCF..0@ha';;
      *-*-darwin*)
	conftest_s='	.text
LCF0:
	addis r11,r30,_GLOBAL_OFFSET_TABLE_-LCF0@ha';;
      *) conftest_s='	.text
.LCF0:
	addis 11,30,_GLOBAL_OFFSET_TABLE_-.LCF0@ha';;
    esac

    gcc_GAS_CHECK_FEATURE([rel16 relocs],
      gcc_cv_as_powerpc_rel16, [2,17,0], -a32,
      [$conftest_s],,
      [AC_DEFINE(HAVE_AS_REL16, 1,
	  [Define if your assembler supports R_PPC_REL16 relocs.])])
    ;;

  mips*-*-*)
    gcc_GAS_CHECK_FEATURE([explicit relocation support],
      gcc_cv_as_mips_explicit_relocs, [2,14,0],,
[	lw $4,%gp_rel(foo)($4)],,
      [if test x$target_cpu_default = x
       then target_cpu_default=MASK_EXPLICIT_RELOCS
       else target_cpu_default="($target_cpu_default)|MASK_EXPLICIT_RELOCS"
       fi])
    ;;
esac

# Mips and HP-UX need the GNU assembler.
# Linux on IA64 might be able to use the Intel assembler.

case "$target" in
  mips*-*-* | *-*-hpux* )
    if test x$gas_flag = xyes \
       || test x"$host" != x"$build" \
       || test ! -x "$gcc_cv_as" \
       || "$gcc_cv_as" -v < /dev/null 2>&1 | grep GNU > /dev/null; then
      :
    else
      echo "*** This configuration requires the GNU assembler" >&2
      exit 1
    fi
    ;;
esac

# ??? Not all targets support dwarf2 debug_line, even within a version
# of gas.  Moreover, we need to emit a valid instruction to trigger any
# info to the output file.  So, as supported targets are added to gas 2.11,
# add some instruction here to (also) show we expect this might work.
# ??? Once 2.11 is released, probably need to add first known working
# version to the per-target configury.
case "$target" in
  i?86*-*-* | mips*-*-* | alpha*-*-* | powerpc*-*-* | sparc*-*-* | m68*-*-* \
  | x86_64*-*-* | hppa*-*-* | arm*-*-* | strongarm*-*-* | xscale*-*-* \
  | xstormy16*-*-* | cris-*-* | xtensa-*-* | bfin-*-* | score*-*-*)
    insn="nop"
    ;;
  ia64*-*-* | s390*-*-*)
    insn="nop 0"
    ;;
  mmix-*-*)
    insn="swym 0"
    ;;
esac
if test x"$insn" != x; then
 conftest_s="\
	.file 1 \"conftest.s\"
	.loc 1 3 0
	$insn"
 gcc_GAS_CHECK_FEATURE([dwarf2 debug_line support],
  gcc_cv_as_dwarf2_debug_line,
  [elf,2,11,0],, [$conftest_s],
  [if test x$gcc_cv_objdump != x \
   && $gcc_cv_objdump -h conftest.o 2> /dev/null \
      | grep debug_line > /dev/null 2>&1; then
     gcc_cv_as_dwarf2_debug_line=yes
   fi])

# The .debug_line file table must be in the exact order that
# we specified the files, since these indices are also used
# by DW_AT_decl_file.  Approximate this test by testing if
# the assembler bitches if the same index is assigned twice.
 gcc_GAS_CHECK_FEATURE([buggy dwarf2 .file directive],
  gcc_cv_as_dwarf2_file_buggy,,,
[	.file 1 "foo.s"
	.file 1 "bar.s"])

 if test $gcc_cv_as_dwarf2_debug_line = yes \
 && test $gcc_cv_as_dwarf2_file_buggy = no; then
	AC_DEFINE(HAVE_AS_DWARF2_DEBUG_LINE, 1,
  [Define if your assembler supports dwarf2 .file/.loc directives,
   and preserves file table indices exactly as given.])
 fi

 gcc_GAS_CHECK_FEATURE([--gdwarf2 option],
  gcc_cv_as_gdwarf2_flag,
  [elf,2,11,0], [--gdwarf2], [$insn],,
  [AC_DEFINE(HAVE_AS_GDWARF2_DEBUG_FLAG, 1,
[Define if your assembler supports the --gdwarf2 option.])])

 gcc_GAS_CHECK_FEATURE([--gstabs option],
  gcc_cv_as_gstabs_flag,
  [elf,2,11,0], [--gstabs], [$insn],
  [# The native Solaris 9/Intel assembler doesn't understand --gstabs
   # and warns about it, but still exits successfully.  So check for
   # this.
   if AC_TRY_COMMAND([$gcc_cv_as --gstabs -o conftest.o conftest.s 2>&1 | grep -i warning > /dev/null])
   then :
   else gcc_cv_as_gstabs_flag=yes
   fi],
  [AC_DEFINE(HAVE_AS_GSTABS_DEBUG_FLAG, 1,
[Define if your assembler supports the --gstabs option.])])
fi

AC_MSG_CHECKING(linker read-only and read-write section mixing)
gcc_cv_ld_ro_rw_mix=unknown
if test $in_tree_ld = yes ; then
  if test "$gcc_cv_gld_major_version" -eq 2 -a "$gcc_cv_gld_minor_version" -ge 10 -o "$gcc_cv_gld_major_version" -gt 2 \
     && test $in_tree_ld_is_elf = yes; then
    gcc_cv_ld_ro_rw_mix=read-write
  fi
elif test x$gcc_cv_as != x -a x$gcc_cv_ld != x -a x$gcc_cv_objdump != x ; then
  echo '.section myfoosect, "a"' > conftest1.s
  echo '.section myfoosect, "aw"' > conftest2.s
  echo '.byte 1' >> conftest2.s
  echo '.section myfoosect, "a"' > conftest3.s
  echo '.byte 0' >> conftest3.s
  if $gcc_cv_as -o conftest1.o conftest1.s > /dev/null 2>&1 \
     && $gcc_cv_as -o conftest2.o conftest2.s > /dev/null 2>&1 \
     && $gcc_cv_as -o conftest3.o conftest3.s > /dev/null 2>&1 \
     && $gcc_cv_ld -shared -o conftest1.so conftest1.o \
	conftest2.o conftest3.o > /dev/null 2>&1; then
    gcc_cv_ld_ro_rw_mix=`$gcc_cv_objdump -h conftest1.so \
			 | sed -e '/myfoosect/!d' -e N`
    if echo "$gcc_cv_ld_ro_rw_mix" | grep CONTENTS > /dev/null; then
      if echo "$gcc_cv_ld_ro_rw_mix" | grep READONLY > /dev/null; then
	gcc_cv_ld_ro_rw_mix=read-only
      else
	gcc_cv_ld_ro_rw_mix=read-write
      fi
    fi
  fi
changequote(,)dnl
  rm -f conftest.* conftest[123].*
changequote([,])dnl
fi
if test x$gcc_cv_ld_ro_rw_mix = xread-write; then
	AC_DEFINE(HAVE_LD_RO_RW_SECTION_MIXING, 1,
  [Define if your linker links a mix of read-only
   and read-write sections into a read-write section.])
fi
AC_MSG_RESULT($gcc_cv_ld_ro_rw_mix)

AC_MSG_CHECKING(linker PT_GNU_EH_FRAME support)
gcc_cv_ld_eh_frame_hdr=no
if test $in_tree_ld = yes ; then
  if test "$gcc_cv_gld_major_version" -eq 2 -a "$gcc_cv_gld_minor_version" -ge 12 -o "$gcc_cv_gld_major_version" -gt 2 \
     && test $in_tree_ld_is_elf = yes; then
    gcc_cv_ld_eh_frame_hdr=yes
  fi
elif test x$gcc_cv_ld != x; then
	# Check if linker supports --eh-frame-hdr option
	if $gcc_cv_ld --help 2>/dev/null | grep eh-frame-hdr > /dev/null; then
		gcc_cv_ld_eh_frame_hdr=yes
	fi
fi
GCC_TARGET_TEMPLATE([HAVE_LD_EH_FRAME_HDR])
if test x"$gcc_cv_ld_eh_frame_hdr" = xyes; then
	AC_DEFINE(HAVE_LD_EH_FRAME_HDR, 1,
[Define if your linker supports --eh-frame-hdr option.])
fi
AC_MSG_RESULT($gcc_cv_ld_eh_frame_hdr)

AC_MSG_CHECKING(linker position independent executable support)
gcc_cv_ld_pie=no
if test $in_tree_ld = yes ; then
  if test "$gcc_cv_gld_major_version" -eq 2 -a "$gcc_cv_gld_minor_version" -ge 15 -o "$gcc_cv_gld_major_version" -gt 2 \
     && test $in_tree_ld_is_elf = yes; then
    gcc_cv_ld_pie=yes
  fi
elif test x$gcc_cv_ld != x; then
	# Check if linker supports -pie option
	if $gcc_cv_ld --help 2>/dev/null | grep -- -pie > /dev/null; then
		gcc_cv_ld_pie=yes
	fi
fi
if test x"$gcc_cv_ld_pie" = xyes; then
	AC_DEFINE(HAVE_LD_PIE, 1,
[Define if your linker supports -pie option.])
fi
AC_MSG_RESULT($gcc_cv_ld_pie)

# --------
# UNSORTED
# --------

AC_CACHE_CHECK(linker --as-needed support,
gcc_cv_ld_as_needed,
[gcc_cv_ld_as_needed=no
if test $in_tree_ld = yes ; then
  if test "$gcc_cv_gld_major_version" -eq 2 -a "$gcc_cv_gld_minor_version" -ge 16 -o "$gcc_cv_gld_major_version" -gt 2 \
     && test $in_tree_ld_is_elf = yes; then
    gcc_cv_ld_as_needed=yes
  fi
elif test x$gcc_cv_ld != x; then
	# Check if linker supports --as-needed and --no-as-needed options
	if $gcc_cv_ld --help 2>/dev/null | grep as-needed > /dev/null; then
		gcc_cv_ld_as_needed=yes
	fi
fi
])
if test x"$gcc_cv_ld_as_needed" = xyes; then
	AC_DEFINE(HAVE_LD_AS_NEEDED, 1,
[Define if your linker supports --as-needed and --no-as-needed options.])
fi

case "$target:$tm_file" in
  powerpc64*-*-linux* | powerpc*-*-linux*rs6000/biarch64.h*)
    AC_CACHE_CHECK(linker support for omitting dot symbols,
    gcc_cv_ld_no_dot_syms,
    [gcc_cv_ld_no_dot_syms=no
    if test $in_tree_ld = yes ; then
      if test "$gcc_cv_gld_major_version" -eq 2 -a "$gcc_cv_gld_minor_version" -ge 16 -o "$gcc_cv_gld_major_version" -gt 2; then
        gcc_cv_ld_no_dot_syms=yes
      fi
    elif test x$gcc_cv_as != x -a x$gcc_cv_ld != x ; then
      cat > conftest1.s <<EOF
	.text
	bl .foo
EOF
      cat > conftest2.s <<EOF
	.section ".opd","aw"
	.align 3
	.globl foo
	.type foo,@function
foo:
	.quad .LEfoo,.TOC.@tocbase,0
	.text
.LEfoo:
	blr
	.size foo,.-.LEfoo
EOF
      if $gcc_cv_as -a64 -o conftest1.o conftest1.s > /dev/null 2>&1 \
         && $gcc_cv_as -a64 -o conftest2.o conftest2.s > /dev/null 2>&1 \
         && $gcc_cv_ld -melf64ppc -o conftest conftest1.o conftest2.o > /dev/null 2>&1; then
        gcc_cv_ld_no_dot_syms=yes
      fi
      rm -f conftest conftest1.o conftest2.o conftest1.s conftest2.s
    fi
    ])
    if test x"$gcc_cv_ld_no_dot_syms" = xyes; then
      AC_DEFINE(HAVE_LD_NO_DOT_SYMS, 1,
    [Define if your PowerPC64 linker only needs function descriptor syms.])
    fi
    ;;
esac

AC_CACHE_CHECK(linker --sysroot support,
  gcc_cv_ld_sysroot,
  [gcc_cv_ld_sysroot=no
  if test $in_tree_ld = yes ; then
      if test "$gcc_cv_gld_major_version" -eq 2 -a "$gcc_cv_gld_minor_version" -ge 16 -o "$gcc_cv_gld_major_version" -gt 2 ; then
        gcc_cv_ld_sysroot=yes
      fi
  elif test x$gcc_cv_ld != x; then 
    if $gcc_cv_ld --help 2>/dev/null | grep sysroot > /dev/null; then
      gcc_cv_ld_sysroot=yes
    fi
  fi])
if test x"$gcc_cv_ld_sysroot" = xyes; then
  AC_DEFINE(HAVE_LD_SYSROOT, 1,
  [Define if your linker supports --sysroot.])
fi	  

if test x$with_sysroot = x && test x$host = x$target \
   && test "$prefix" != "/usr" && test "x$prefix" != "x$local_prefix" ; then
  AC_DEFINE_UNQUOTED(PREFIX_INCLUDE_DIR, "$prefix/include",
[Define to PREFIX/include if cpp should also search that directory.])
fi

# Test for stack protector support in target C library.
case "$target" in
  *-*-linux*)
    AC_CACHE_CHECK(__stack_chk_fail in target GNU C library,
      gcc_cv_libc_provides_ssp,
      [gcc_cv_libc_provides_ssp=no
      if test x$host != x$target || test "x$TARGET_SYSTEM_ROOT" != x; then
	if test "x$with_sysroot" = x; then
	  glibc_header_dir="${exec_prefix}/${target_noncanonical}/sys-include"
	elif test "x$with_sysroot" = xyes; then
	  glibc_header_dir="${exec_prefix}/${target_noncanonical}/sys-root/usr/include"
	else
	  glibc_header_dir="${with_sysroot}/usr/include"
	fi
      else
	glibc_header_dir=/usr/include
      fi
      # glibc 2.4 and later provides __stack_chk_fail and
      # either __stack_chk_guard, or TLS access to stack guard canary.
      if test -f $glibc_header_dir/features.h \
	 && $EGREP '^@<:@ 	@:>@*#[ 	]*define[ 	]+__GNU_LIBRARY__[ 	]+([1-9][0-9]|[6-9])' \
	    $glibc_header_dir/features.h > /dev/null; then
	if $EGREP '^@<:@ 	@:>@*#[ 	]*define[ 	]+__GLIBC__[ 	]+([1-9][0-9]|[3-9])' \
	   $glibc_header_dir/features.h > /dev/null; then
	  gcc_cv_libc_provides_ssp=yes
	elif $EGREP '^@<:@ 	@:>@*#[ 	]*define[ 	]+__GLIBC__[ 	]+2' \
	     $glibc_header_dir/features.h > /dev/null \
	     && $EGREP '^@<:@ 	@:>@*#[ 	]*define[ 	]+__GLIBC_MINOR__[ 	]+([1-9][0-9]|[4-9])' \
	     $glibc_header_dir/features.h > /dev/null; then
	  gcc_cv_libc_provides_ssp=yes
	fi
      fi]) ;;
  *) gcc_cv_libc_provides_ssp=no ;;
esac
if test x$gcc_cv_libc_provides_ssp = xyes; then
  AC_DEFINE(TARGET_LIBC_PROVIDES_SSP, 1,
	    [Define if your target C library provides stack protector support])
fi

# Check if TFmode long double should be used by default or not.
# Some glibc targets used DFmode long double, but with glibc 2.4
# and later they can use TFmode.
case "$target" in
  powerpc*-*-*gnu* | \
  sparc*-*-linux* | \
  s390*-*-linux* | \
  alpha*-*-linux*)
    AC_ARG_WITH(long-double-128,
[  --with-long-double-128  Use 128-bit long double by default.],
      gcc_cv_target_ldbl128="$with_long_double_128",
      [gcc_cv_target_ldbl128=no
      if test x$host != x$target || test "x$TARGET_SYSTEM_ROOT" != x; then
	if test "x$with_sysroot" = x; then
	  glibc_header_dir="${exec_prefix}/${target_noncanonical}/sys-include"
	elif test "x$with_sysroot" = xyes; then
	  glibc_header_dir="${exec_prefix}/${target_noncanonical}/sys-root/usr/include"
	else
	  glibc_header_dir="${with_sysroot}/usr/include"
	fi
      else
	glibc_header_dir=/usr/include
      fi
changequote(,)dnl
      grep '^[ 	]*#[ 	]*define[ 	][ 	]*__LONG_DOUBLE_MATH_OPTIONAL' \
        $glibc_header_dir/bits/wordsize.h > /dev/null 2>&1 \
      && gcc_cv_target_ldbl128=yes
changequote([,])dnl
      ])
    ;;
esac
if test x$gcc_cv_target_ldbl128 = xyes; then
  AC_DEFINE(TARGET_DEFAULT_LONG_DOUBLE_128, 1,
	    [Define if TFmode long double should be the default])
fi

# Find out what GC implementation we want, or may, use.
AC_ARG_WITH(gc,
[  --with-gc={page,zone}   choose the garbage collection mechanism to use
                          with the compiler],
[case "$withval" in
  page)
    GGC=ggc-$withval
    ;;
  zone)
    GGC=ggc-$withval
    AC_DEFINE(GGC_ZONE, 1, [Define if the zone collector is in use])
    ;;
  *)
    AC_MSG_ERROR([$withval is an invalid option to --with-gc])
    ;;
esac],
[GGC=ggc-page])
AC_SUBST(GGC)
echo "Using $GGC for garbage collection."

# Use the system's zlib library.
zlibdir=-L../zlib
zlibinc="-I\$(srcdir)/../zlib"
AC_ARG_WITH(system-zlib,
[  --with-system-zlib      use installed libz],
zlibdir=
zlibinc=
)
AC_SUBST(zlibdir)
AC_SUBST(zlibinc)

dnl Very limited version of automake's enable-maintainer-mode

AC_MSG_CHECKING([whether to enable maintainer-specific portions of Makefiles])
  dnl maintainer-mode is disabled by default
  AC_ARG_ENABLE(maintainer-mode,
[  --enable-maintainer-mode
                          enable make rules and dependencies not useful
                          (and sometimes confusing) to the casual installer],
      maintainer_mode=$enableval,
      maintainer_mode=no)

AC_MSG_RESULT($maintainer_mode)

if test "$maintainer_mode" = "yes"; then
  MAINT=''
else
  MAINT='#'
fi
AC_SUBST(MAINT)dnl

# --------------
# Language hooks
# --------------

# Make empty files to contain the specs and options for each language.
# Then add #include lines to for a compiler that has specs and/or options.

subdirs=
lang_opt_files=
lang_specs_files=
lang_tree_files=
# These (without "all_") are set in each config-lang.in.
# `language' must be a single word so is spelled singularly.
all_languages=
all_boot_languages=
all_compilers=
all_stagestuff=
all_outputs='Makefile gccbug mklibgcc libada-mk'
# List of language makefile fragments.
all_lang_makefrags=
# List of language subdirectory makefiles.  Deprecated.
all_lang_makefiles=
# Files for gengtype
all_gtfiles="$target_gtfiles"
# Files for gengtype with language
all_gtfiles_files_langs=
all_gtfiles_files_files=

# These are the languages that are set in --enable-languages,
# and are available in the GCC tree.
all_selected_languages=

# Add the language fragments.
# Languages are added via two mechanisms.  Some information must be
# recorded in makefile variables, these are defined in config-lang.in.
# We accumulate them and plug them into the main Makefile.
# The other mechanism is a set of hooks for each of the main targets
# like `clean', `install', etc.

language_hooks="Make-hooks"

for lang in ${srcdir}/*/config-lang.in
do
changequote(,)dnl
	test "$lang" = "${srcdir}/*/config-lang.in" && continue

        lang_alias=`sed -n -e 's,^language=['"'"'"'"]\(.*\)["'"'"'"'].*$,\1,p' -e 's,^language=\([^   ]*\).*$,\1,p' $lang`
        if test "x$lang_alias" = x
        then
              echo "$lang doesn't set \$language." 1>&2
              exit 1
        fi
        subdir="`echo $lang | sed -e 's,^.*/\([^/]*\)/config-lang.in$,\1,'`"
        subdirs="$subdirs $subdir"
        case ",$enable_languages," in
        *,$lang_alias,*)
            all_selected_languages="$all_selected_languages $lang_alias"
	    if test -f $srcdir/$subdir/lang-specs.h; then
	        lang_specs_files="$lang_specs_files $srcdir/$subdir/lang-specs.h"
	    fi
	    ;;
        esac
changequote([,])dnl

	language=
	boot_language=
	compilers=
	stagestuff=
	outputs=
	gtfiles=
	subdir_requires=
	. ${srcdir}/$subdir/config-lang.in
	if test "x$language" = x
	then
		echo "${srcdir}/$subdir/config-lang.in doesn't set \$language." 1>&2
		exit 1
	fi

	ok=:
        case ",$enable_languages," in
        	*,$lang_alias,*) ;;
		*)
			for i in $subdir_requires; do
				test -f "${srcdir}/$i/config-lang.in" && continue
				ok=false
				break
			done
		;;
	esac
	$ok || continue

	all_lang_makefrags="$all_lang_makefrags \$(srcdir)/$subdir/Make-lang.in"
	if test -f $srcdir/$subdir/lang.opt; then
	    lang_opt_files="$lang_opt_files $srcdir/$subdir/lang.opt"
	fi
	if test -f $srcdir/$subdir/$subdir-tree.def; then
	    lang_tree_files="$lang_tree_files $srcdir/$subdir/$subdir-tree.def"
	fi
	if test -f ${srcdir}/$subdir/Makefile.in
		then all_lang_makefiles="$subdir/Makefile"
	fi
	all_languages="$all_languages $language"
	if test "x$boot_language" = xyes
	then
		all_boot_languages="$all_boot_languages $language"
	fi
	all_compilers="$all_compilers $compilers"
	all_stagestuff="$all_stagestuff $stagestuff"
	all_outputs="$all_outputs $outputs"
	all_gtfiles="$all_gtfiles $gtfiles"
	for f in $gtfiles
	do
		all_gtfiles_files_langs="$all_gtfiles_files_langs ${subdir} "
		all_gtfiles_files_files="$all_gtfiles_files_files ${f} "
	done
done

# Pick up gtfiles for c
gtfiles=
subdir="c"
. ${srcdir}/c-config-lang.in
all_gtfiles="$all_gtfiles $gtfiles"
for f in $gtfiles
do
        all_gtfiles_files_langs="$all_gtfiles_files_langs ${subdir} "
        all_gtfiles_files_files="$all_gtfiles_files_files ${f} "
done

check_languages=
for language in $all_selected_languages
do
	check_languages="$check_languages check-$language"
done

# We link each language in with a set of hooks, reached indirectly via
# lang.${target}.  Only do so for selected languages.

rm -f Make-hooks
touch Make-hooks
target_list="all.cross start.encap rest.encap tags \
	install-common install-man install-info dvi pdf html \
	uninstall info man srcextra srcman srcinfo \
	mostlyclean clean distclean maintainer-clean \
	stage1 stage2 stage3 stage4 stageprofile stagefeedback"

for t in $target_list
do
	x=
	for lang in $all_selected_languages
	do
		x="$x $lang.$t"
	done
	echo "lang.$t: $x" >> Make-hooks
done

# --------
# UNSORTED
# --------

# Create .gdbinit.

echo "dir ." > .gdbinit
echo "dir ${srcdir}" >> .gdbinit
if test x$gdb_needs_out_file_path = xyes
then
	echo "dir ${srcdir}/config/"`dirname ${out_file}` >> .gdbinit
fi
if test "x$subdirs" != x; then
	for s in $subdirs
	do
		echo "dir ${srcdir}/$s" >> .gdbinit
	done
fi
echo "source ${srcdir}/gdbinit.in" >> .gdbinit

# If $(exec_prefix) exists and is not the same as $(prefix), then compute an
# absolute path for gcc_tooldir based on inserting the number of up-directory
# movements required to get from $(exec_prefix) to $(prefix) into the basic
# $(libsubdir)/@(unlibsubdir) based path.
# Don't set gcc_tooldir to tooldir since that's only passed in by the toplevel
# make and thus we'd get different behavior depending on where we built the
# sources.
if test x$exec_prefix = xNONE -o x$exec_prefix = x$prefix; then
    gcc_tooldir='$(libsubdir)/$(unlibsubdir)/../$(target_noncanonical)'
else
changequote(<<, >>)dnl
# An explanation of the sed strings:
#  -e 's|^\$(prefix)||'   matches and eliminates 'prefix' from 'exec_prefix'
#  -e 's|/$||'            match a trailing forward slash and eliminates it
#  -e 's|^[^/]|/|'        forces the string to start with a forward slash (*)
#  -e 's|/[^/]*|../|g'    replaces each occurrence of /<directory> with ../
#
# (*) Note this pattern overwrites the first character of the string
# with a forward slash if one is not already present.  This is not a
# problem because the exact names of the sub-directories concerned is
# unimportant, just the number of them matters.
#
# The practical upshot of these patterns is like this:
#
#  prefix     exec_prefix        result
#  ------     -----------        ------
#   /foo        /foo/bar          ../
#   /foo/       /foo/bar          ../
#   /foo        /foo/bar/         ../
#   /foo/       /foo/bar/         ../
#   /foo        /foo/bar/ugg      ../../
#
    dollar='$$'
    gcc_tooldir="\$(libsubdir)/\$(unlibsubdir)/\`echo \$(exec_prefix) | sed -e 's|^\$(prefix)||' -e 's|/\$(dollar)||' -e 's|^[^/]|/|' -e 's|/[^/]*|../|g'\`\$(target_noncanonical)"
changequote([, ])dnl
fi
AC_SUBST(gcc_tooldir)
AC_SUBST(dollar)

# Find a directory in which to install a shared libgcc.

AC_ARG_ENABLE(version-specific-runtime-libs,
[  --enable-version-specific-runtime-libs
                          specify that runtime libraries should be
                          installed in a compiler-specific directory])

AC_ARG_WITH(slibdir,
[  --with-slibdir=DIR      shared libraries in DIR [[LIBDIR]]],
slibdir="$with_slibdir",
if test "${enable_version_specific_runtime_libs+set}" = set; then
  slibdir='$(libsubdir)'
elif test "$host" != "$target"; then
  slibdir='$(build_tooldir)/lib'
else
  slibdir='$(libdir)'
fi)
AC_SUBST(slibdir)

objdir=`${PWDCMD-pwd}`
AC_SUBST(objdir)

AC_ARG_WITH(datarootdir,
[  --with-datarootdir=DIR  Use DIR as the data root [[PREFIX/share]]],
datarootdir="\${prefix}/$with_datarootdir",
datarootdir='$(prefix)/share')
AC_SUBST(datarootdir)

AC_ARG_WITH(docdir,
[  --with-docdir=DIR       Install documentation in DIR [[DATAROOTDIR]]],
docdir="\${prefix}/$with_docdir",
docdir='$(datarootdir)')
AC_SUBST(docdir)

AC_ARG_WITH(htmldir,
[  --with-htmldir=DIR      html documentation in in DIR [[DOCDIR]]],
htmldir="\${prefix}/$with_htmldir",
htmldir='$(docdir)')
AC_SUBST(htmldir)

# Substitute configuration variables
AC_SUBST(subdirs)
AC_SUBST(srcdir)
AC_SUBST(all_boot_languages)
AC_SUBST(all_compilers)
AC_SUBST(all_gtfiles)
AC_SUBST(all_gtfiles_files_langs)
AC_SUBST(all_gtfiles_files_files)
AC_SUBST(all_lang_makefrags)
AC_SUBST(all_lang_makefiles)
AC_SUBST(all_languages)
AC_SUBST(all_selected_languages)
AC_SUBST(all_stagestuff)
AC_SUBST(build_exeext)
AC_SUBST(build_install_headers_dir)
AC_SUBST(build_xm_file_list)
AC_SUBST(build_xm_include_list)
AC_SUBST(build_xm_defines)
AC_SUBST(check_languages)
AC_SUBST(cc_set_by_configure)
AC_SUBST(quoted_cc_set_by_configure)
AC_SUBST(cpp_install_dir)
AC_SUBST(xmake_file)
AC_SUBST(tmake_file)
AC_SUBST(extra_gcc_objs)
AC_SUBST(extra_headers_list)
AC_SUBST(extra_objs)
AC_SUBST(extra_parts)
AC_SUBST(extra_passes)
AC_SUBST(extra_programs)
AC_SUBST(float_h_file)
AC_SUBST(gcc_config_arguments)
AC_SUBST(gcc_gxx_include_dir)
AC_SUBST(host_exeext)
AC_SUBST(host_xm_file_list)
AC_SUBST(host_xm_include_list)
AC_SUBST(host_xm_defines)
AC_SUBST(out_host_hook_obj)
AC_SUBST(install)
AC_SUBST(lang_opt_files)
AC_SUBST(lang_specs_files)
AC_SUBST(lang_tree_files)
AC_SUBST(local_prefix)
AC_SUBST(md_file)
AC_SUBST(objc_boehm_gc)
AC_SUBST(out_file)
AC_SUBST(out_object_file)
AC_SUBST(stage_prefix_set_by_configure)
AC_SUBST(quoted_stage_prefix_set_by_configure)
AC_SUBST(thread_file)
AC_SUBST(tm_file_list)
AC_SUBST(tm_include_list)
AC_SUBST(tm_defines)
AC_SUBST(tm_p_file_list)
AC_SUBST(tm_p_include_list)
AC_SUBST(xm_file_list)
AC_SUBST(xm_include_list)
AC_SUBST(xm_defines)
AC_SUBST(c_target_objs)
AC_SUBST(cxx_target_objs)
AC_SUBST(target_cpu_default)

AC_SUBST_FILE(language_hooks)

# Echo link setup.
if test x${build} = x${host} ; then
  if test x${host} = x${target} ; then
    echo "Links are now set up to build a native compiler for ${target}." 1>&2
  else
    echo "Links are now set up to build a cross-compiler" 1>&2
    echo " from ${host} to ${target}." 1>&2
  fi
else
  if test x${host} = x${target} ; then
    echo "Links are now set up to build (on ${build}) a native compiler" 1>&2
    echo " for ${target}." 1>&2
  else
    echo "Links are now set up to build (on ${build}) a cross-compiler" 1>&2
    echo " from ${host} to ${target}." 1>&2
  fi
fi

AC_ARG_VAR(GMPLIBS,[How to link GMP])
AC_ARG_VAR(GMPINC,[How to find GMP include files])

# Configure the subdirectories
# AC_CONFIG_SUBDIRS($subdirs)

# Create the Makefile
# and configure language subdirectories
AC_CONFIG_FILES($all_outputs)

AC_CONFIG_COMMANDS([default],
[
case ${CONFIG_HEADERS} in
  *auto-host.h:config.in*)
  echo > cstamp-h ;;
esac
# Make sure all the subdirs exist.
for d in $subdirs doc build
do
    test -d $d || mkdir $d
done
# If the host supports symlinks, point stage[1234] at ../stage[1234] so
# bootstrapping and the installation procedure can still use
# CC="stage1/xgcc -Bstage1/".  If the host doesn't support symlinks,
# FLAGS_TO_PASS has been modified to solve the problem there.
# This is virtually a duplicate of what happens in configure.lang; we do
# an extra check to make sure this only happens if ln -s can be used.
case "$LN_S" in
  *-s*)
    for d in ${subdirs} ; do
	STARTDIR=`${PWDCMD-pwd}`
	cd $d
	for t in stage1 stage2 stage3 stage4 stageprofile stagefeedback include
	do
		rm -f $t
		$LN_S ../$t $t 2>/dev/null
	done
	cd $STARTDIR
    done
  ;;
esac
], 
[subdirs='$subdirs'])
AC_OUTPUT
