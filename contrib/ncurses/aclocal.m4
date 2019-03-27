dnl***************************************************************************
dnl Copyright (c) 1998-2013,2014 Free Software Foundation, Inc.              *
dnl                                                                          *
dnl Permission is hereby granted, free of charge, to any person obtaining a  *
dnl copy of this software and associated documentation files (the            *
dnl "Software"), to deal in the Software without restriction, including      *
dnl without limitation the rights to use, copy, modify, merge, publish,      *
dnl distribute, distribute with modifications, sublicense, and/or sell       *
dnl copies of the Software, and to permit persons to whom the Software is    *
dnl furnished to do so, subject to the following conditions:                 *
dnl                                                                          *
dnl The above copyright notice and this permission notice shall be included  *
dnl in all copies or substantial portions of the Software.                   *
dnl                                                                          *
dnl THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
dnl OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
dnl MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
dnl IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
dnl DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
dnl OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
dnl THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
dnl                                                                          *
dnl Except as contained in this notice, the name(s) of the above copyright   *
dnl holders shall not be used in advertising or otherwise to promote the     *
dnl sale, use or other dealings in this Software without prior written       *
dnl authorization.                                                           *
dnl***************************************************************************
dnl
dnl Author: Thomas E. Dickey 1995-on
dnl
dnl $Id: aclocal.m4,v 1.686 2014/02/10 00:37:02 tom Exp $
dnl Macros used in NCURSES auto-configuration script.
dnl
dnl These macros are maintained separately from NCURSES.  The copyright on
dnl this file applies to the aggregation of macros and does not affect use of
dnl these macros in other applications.
dnl
dnl See http://invisible-island.net/autoconf/ for additional information.
dnl
dnl ---------------------------------------------------------------------------
dnl ---------------------------------------------------------------------------
dnl AM_LANGINFO_CODESET version: 3 updated: 2002/10/27 23:21:42
dnl -------------------
dnl Inserted as requested by gettext 0.10.40
dnl File from /usr/share/aclocal
dnl codeset.m4
dnl ====================
dnl serial AM1
dnl
dnl From Bruno Haible.
AC_DEFUN([AM_LANGINFO_CODESET],
[
  AC_CACHE_CHECK([for nl_langinfo and CODESET], am_cv_langinfo_codeset,
    [AC_TRY_LINK([#include <langinfo.h>],
      [char* cs = nl_langinfo(CODESET);],
      am_cv_langinfo_codeset=yes,
      am_cv_langinfo_codeset=no)
    ])
  if test $am_cv_langinfo_codeset = yes; then
    AC_DEFINE(HAVE_LANGINFO_CODESET, 1,
      [Define if you have <langinfo.h> and nl_langinfo(CODESET).])
  fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ACVERSION_CHECK version: 4 updated: 2013/03/04 19:52:56
dnl ------------------
dnl Conditionally generate script according to whether we're using a given autoconf.
dnl
dnl $1 = version to compare against
dnl $2 = code to use if AC_ACVERSION is at least as high as $1.
dnl $3 = code to use if AC_ACVERSION is older than $1.
define([CF_ACVERSION_CHECK],
[
ifdef([AC_ACVERSION], ,[m4_copy([m4_PACKAGE_VERSION],[AC_ACVERSION])])dnl
ifdef([m4_version_compare],
[m4_if(m4_version_compare(m4_defn([AC_ACVERSION]), [$1]), -1, [$3], [$2])],
[CF_ACVERSION_COMPARE(
AC_PREREQ_CANON(AC_PREREQ_SPLIT([$1])),
AC_PREREQ_CANON(AC_PREREQ_SPLIT(AC_ACVERSION)), AC_ACVERSION, [$2], [$3])])])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ACVERSION_COMPARE version: 3 updated: 2012/10/03 18:39:53
dnl --------------------
dnl CF_ACVERSION_COMPARE(MAJOR1, MINOR1, TERNARY1,
dnl                      MAJOR2, MINOR2, TERNARY2,
dnl                      PRINTABLE2, not FOUND, FOUND)
define([CF_ACVERSION_COMPARE],
[ifelse(builtin([eval], [$2 < $5]), 1,
[ifelse([$8], , ,[$8])],
[ifelse([$9], , ,[$9])])])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ADA_INCLUDE_DIRS version: 8 updated: 2013/10/14 04:24:07
dnl -------------------
dnl Construct the list of include-options for the C programs in the Ada95
dnl binding.
AC_DEFUN([CF_ADA_INCLUDE_DIRS],
[
ACPPFLAGS="-I. -I../include -I../../include $ACPPFLAGS"
if test "$srcdir" != "."; then
	ACPPFLAGS="-I\${srcdir}/../../include $ACPPFLAGS"
fi
if test "$GCC" != yes; then
	ACPPFLAGS="$ACPPFLAGS -I\${includedir}"
elif test "$includedir" != "/usr/include"; then
	if test "$includedir" = '${prefix}/include' ; then
		if test x$prefix != x/usr ; then
			ACPPFLAGS="$ACPPFLAGS -I\${includedir}"
		fi
	else
		ACPPFLAGS="$ACPPFLAGS -I\${includedir}"
	fi
fi
AC_SUBST(ACPPFLAGS)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ADD_ADAFLAGS version: 1 updated: 2010/06/19 15:22:18
dnl ---------------
dnl Add to $ADAFLAGS, which is substituted into makefile and scripts.
AC_DEFUN([CF_ADD_ADAFLAGS],[
 	ADAFLAGS="$ADAFLAGS $1"
	AC_SUBST(ADAFLAGS)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ADD_CFLAGS version: 10 updated: 2010/05/26 05:38:42
dnl -------------
dnl Copy non-preprocessor flags to $CFLAGS, preprocessor flags to $CPPFLAGS
dnl The second parameter if given makes this macro verbose.
dnl
dnl Put any preprocessor definitions that use quoted strings in $EXTRA_CPPFLAGS,
dnl to simplify use of $CPPFLAGS in compiler checks, etc., that are easily
dnl confused by the quotes (which require backslashes to keep them usable).
AC_DEFUN([CF_ADD_CFLAGS],
[
cf_fix_cppflags=no
cf_new_cflags=
cf_new_cppflags=
cf_new_extra_cppflags=

for cf_add_cflags in $1
do
case $cf_fix_cppflags in
no)
	case $cf_add_cflags in #(vi
	-undef|-nostdinc*|-I*|-D*|-U*|-E|-P|-C) #(vi
		case $cf_add_cflags in
		-D*)
			cf_tst_cflags=`echo ${cf_add_cflags} |sed -e 's/^-D[[^=]]*='\''\"[[^"]]*//'`

			test "${cf_add_cflags}" != "${cf_tst_cflags}" \
				&& test -z "${cf_tst_cflags}" \
				&& cf_fix_cppflags=yes

			if test $cf_fix_cppflags = yes ; then
				cf_new_extra_cppflags="$cf_new_extra_cppflags $cf_add_cflags"
				continue
			elif test "${cf_tst_cflags}" = "\"'" ; then
				cf_new_extra_cppflags="$cf_new_extra_cppflags $cf_add_cflags"
				continue
			fi
			;;
		esac
		case "$CPPFLAGS" in
		*$cf_add_cflags) #(vi
			;;
		*) #(vi
			case $cf_add_cflags in #(vi
			-D*)
				cf_tst_cppflags=`echo "x$cf_add_cflags" | sed -e 's/^...//' -e 's/=.*//'`
				CF_REMOVE_DEFINE(CPPFLAGS,$CPPFLAGS,$cf_tst_cppflags)
				;;
			esac
			cf_new_cppflags="$cf_new_cppflags $cf_add_cflags"
			;;
		esac
		;;
	*)
		cf_new_cflags="$cf_new_cflags $cf_add_cflags"
		;;
	esac
	;;
yes)
	cf_new_extra_cppflags="$cf_new_extra_cppflags $cf_add_cflags"

	cf_tst_cflags=`echo ${cf_add_cflags} |sed -e 's/^[[^"]]*"'\''//'`

	test "${cf_add_cflags}" != "${cf_tst_cflags}" \
		&& test -z "${cf_tst_cflags}" \
		&& cf_fix_cppflags=no
	;;
esac
done

if test -n "$cf_new_cflags" ; then
	ifelse([$2],,,[CF_VERBOSE(add to \$CFLAGS $cf_new_cflags)])
	CFLAGS="$CFLAGS $cf_new_cflags"
fi

if test -n "$cf_new_cppflags" ; then
	ifelse([$2],,,[CF_VERBOSE(add to \$CPPFLAGS $cf_new_cppflags)])
	CPPFLAGS="$CPPFLAGS $cf_new_cppflags"
fi

if test -n "$cf_new_extra_cppflags" ; then
	ifelse([$2],,,[CF_VERBOSE(add to \$EXTRA_CPPFLAGS $cf_new_extra_cppflags)])
	EXTRA_CPPFLAGS="$cf_new_extra_cppflags $EXTRA_CPPFLAGS"
fi

AC_SUBST(EXTRA_CPPFLAGS)

])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ADD_INCDIR version: 13 updated: 2010/05/26 16:44:57
dnl -------------
dnl Add an include-directory to $CPPFLAGS.  Don't add /usr/include, since it's
dnl redundant.  We don't normally need to add -I/usr/local/include for gcc,
dnl but old versions (and some misinstalled ones) need that.  To make things
dnl worse, gcc 3.x may give error messages if -I/usr/local/include is added to
dnl the include-path).
AC_DEFUN([CF_ADD_INCDIR],
[
if test -n "$1" ; then
  for cf_add_incdir in $1
  do
	while test $cf_add_incdir != /usr/include
	do
	  if test -d $cf_add_incdir
	  then
		cf_have_incdir=no
		if test -n "$CFLAGS$CPPFLAGS" ; then
		  # a loop is needed to ensure we can add subdirs of existing dirs
		  for cf_test_incdir in $CFLAGS $CPPFLAGS ; do
			if test ".$cf_test_incdir" = ".-I$cf_add_incdir" ; then
			  cf_have_incdir=yes; break
			fi
		  done
		fi

		if test "$cf_have_incdir" = no ; then
		  if test "$cf_add_incdir" = /usr/local/include ; then
			if test "$GCC" = yes
			then
			  cf_save_CPPFLAGS=$CPPFLAGS
			  CPPFLAGS="$CPPFLAGS -I$cf_add_incdir"
			  AC_TRY_COMPILE([#include <stdio.h>],
				  [printf("Hello")],
				  [],
				  [cf_have_incdir=yes])
			  CPPFLAGS=$cf_save_CPPFLAGS
			fi
		  fi
		fi

		if test "$cf_have_incdir" = no ; then
		  CF_VERBOSE(adding $cf_add_incdir to include-path)
		  ifelse([$2],,CPPFLAGS,[$2])="$ifelse([$2],,CPPFLAGS,[$2]) -I$cf_add_incdir"

		  cf_top_incdir=`echo $cf_add_incdir | sed -e 's%/include/.*$%/include%'`
		  test "$cf_top_incdir" = "$cf_add_incdir" && break
		  cf_add_incdir="$cf_top_incdir"
		else
		  break
		fi
	  fi
	done
  done
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ADD_LIB version: 2 updated: 2010/06/02 05:03:05
dnl ----------
dnl Add a library, used to enforce consistency.
dnl
dnl $1 = library to add, without the "-l"
dnl $2 = variable to update (default $LIBS)
AC_DEFUN([CF_ADD_LIB],[CF_ADD_LIBS(-l$1,ifelse($2,,LIBS,[$2]))])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ADD_LIBDIR version: 9 updated: 2010/05/26 16:44:57
dnl -------------
dnl	Adds to the library-path
dnl
dnl	Some machines have trouble with multiple -L options.
dnl
dnl $1 is the (list of) directory(s) to add
dnl $2 is the optional name of the variable to update (default LDFLAGS)
dnl
AC_DEFUN([CF_ADD_LIBDIR],
[
if test -n "$1" ; then
  for cf_add_libdir in $1
  do
    if test $cf_add_libdir = /usr/lib ; then
      :
    elif test -d $cf_add_libdir
    then
      cf_have_libdir=no
      if test -n "$LDFLAGS$LIBS" ; then
        # a loop is needed to ensure we can add subdirs of existing dirs
        for cf_test_libdir in $LDFLAGS $LIBS ; do
          if test ".$cf_test_libdir" = ".-L$cf_add_libdir" ; then
            cf_have_libdir=yes; break
          fi
        done
      fi
      if test "$cf_have_libdir" = no ; then
        CF_VERBOSE(adding $cf_add_libdir to library-path)
        ifelse([$2],,LDFLAGS,[$2])="-L$cf_add_libdir $ifelse([$2],,LDFLAGS,[$2])"
      fi
    fi
  done
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ADD_LIBS version: 1 updated: 2010/06/02 05:03:05
dnl -----------
dnl Add one or more libraries, used to enforce consistency.
dnl
dnl $1 = libraries to add, with the "-l", etc.
dnl $2 = variable to update (default $LIBS)
AC_DEFUN([CF_ADD_LIBS],[ifelse($2,,LIBS,[$2])="$1 [$]ifelse($2,,LIBS,[$2])"])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ADD_SUBDIR_PATH version: 4 updated: 2013/10/08 17:47:05
dnl ------------------
dnl Append to a search-list for a nonstandard header/lib-file
dnl	$1 = the variable to return as result
dnl	$2 = the package name
dnl	$3 = the subdirectory, e.g., bin, include or lib
dnl $4 = the directory under which we will test for subdirectories
dnl $5 = a directory that we do not want $4 to match
AC_DEFUN([CF_ADD_SUBDIR_PATH],
[
test "x$4" != "x$5" && \
test -d "$4" && \
ifelse([$5],NONE,,[(test -z "$5" || test x$5 = xNONE || test "x$4" != "x$5") &&]) {
	test -n "$verbose" && echo "	... testing for $3-directories under $4"
	test -d $4/$3 &&          $1="[$]$1 $4/$3"
	test -d $4/$3/$2 &&       $1="[$]$1 $4/$3/$2"
	test -d $4/$3/$2/$3 &&    $1="[$]$1 $4/$3/$2/$3"
	test -d $4/$2/$3 &&       $1="[$]$1 $4/$2/$3"
	test -d $4/$2/$3/$2 &&    $1="[$]$1 $4/$2/$3/$2"
}
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ARG_DISABLE version: 3 updated: 1999/03/30 17:24:31
dnl --------------
dnl Allow user to disable a normally-on option.
AC_DEFUN([CF_ARG_DISABLE],
[CF_ARG_OPTION($1,[$2],[$3],[$4],yes)])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ARG_OPTION version: 4 updated: 2010/05/26 05:38:42
dnl -------------
dnl Restricted form of AC_ARG_ENABLE that ensures user doesn't give bogus
dnl values.
dnl
dnl Parameters:
dnl $1 = option name
dnl $2 = help-string
dnl $3 = action to perform if option is not default
dnl $4 = action if perform if option is default
dnl $5 = default option value (either 'yes' or 'no')
AC_DEFUN([CF_ARG_OPTION],
[AC_ARG_ENABLE([$1],[$2],[test "$enableval" != ifelse([$5],no,yes,no) && enableval=ifelse([$5],no,no,yes)
  if test "$enableval" != "$5" ; then
ifelse([$3],,[    :]dnl
,[    $3]) ifelse([$4],,,[
  else
    $4])
  fi],[enableval=$5 ifelse([$4],,,[
  $4
])dnl
  ])])dnl
dnl ---------------------------------------------------------------------------
dnl CF_AR_FLAGS version: 5 updated: 2010/05/20 20:24:29
dnl -----------
dnl Check for suitable "ar" (archiver) options for updating an archive.
AC_DEFUN([CF_AR_FLAGS],[
AC_REQUIRE([CF_PROG_AR])

AC_CACHE_CHECK(for options to update archives, cf_cv_ar_flags,[
	cf_cv_ar_flags=unknown
	for cf_ar_flags in -curv curv -crv crv -cqv cqv -rv rv
	do

		# check if $ARFLAGS already contains this choice
		if test "x$ARFLAGS" != "x" ; then
			cf_check_ar_flags=`echo "x$ARFLAGS" | sed -e "s/$cf_ar_flags\$//" -e "s/$cf_ar_flags / /"`
			if test "x$ARFLAGS" != "$cf_check_ar_flags" ; then
				cf_cv_ar_flags=
				break
			fi
		fi

		rm -f conftest.$ac_cv_objext
		rm -f conftest.a

		cat >conftest.$ac_ext <<EOF
#line __oline__ "configure"
int	testdata[[3]] = { 123, 456, 789 };
EOF
		if AC_TRY_EVAL(ac_compile) ; then
			echo "$AR $ARFLAGS $cf_ar_flags conftest.a conftest.$ac_cv_objext" >&AC_FD_CC
			$AR $ARFLAGS $cf_ar_flags conftest.a conftest.$ac_cv_objext 2>&AC_FD_CC 1>/dev/null
			if test -f conftest.a ; then
				cf_cv_ar_flags=$cf_ar_flags
				break
			fi
		else
			CF_VERBOSE(cannot compile test-program)
			break
		fi
	done
	rm -f conftest.a conftest.$ac_ext conftest.$ac_cv_objext
])

if test -n "$ARFLAGS" ; then
	if test -n "$cf_cv_ar_flags" ; then
		ARFLAGS="$ARFLAGS $cf_cv_ar_flags"
	fi
else
	ARFLAGS=$cf_cv_ar_flags
fi

AC_SUBST(ARFLAGS)
])
dnl ---------------------------------------------------------------------------
dnl CF_AWK_BIG_PRINTF version: 4 updated: 2011/10/30 17:09:50
dnl -----------------
dnl Check if awk can handle big strings using printf.  Some older versions of
dnl awk choke on large strings passed via "%s".
dnl
dnl $1 = desired string size
dnl $2 = variable to set with result
AC_DEFUN([CF_AWK_BIG_PRINTF],
[
	case x$AWK in #(vi
	x)
		eval $2=no
		;;
	*) #(vi
		if ( ${AWK} 'BEGIN { xx = "x"; while (length(xx) < $1) { xx = xx "x"; }; printf("%s\n", xx); }' 2>/dev/null \
			| $AWK '{ printf "%d\n", length([$]0); }' 2>/dev/null | $AWK 'BEGIN { eqls=0; recs=0; } { recs++; if ([$]0 == 12000) eqls++; } END { if (recs != 1 || eqls != 1) exit 1; }' 2>/dev/null >/dev/null ) ; then
			eval $2=yes
		else
			eval $2=no
		fi
		;;
	esac
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_BOOL_DECL version: 8 updated: 2004/01/30 15:51:18
dnl ------------
dnl Test if 'bool' is a builtin type in the configured C++ compiler.  Some
dnl older compilers (e.g., gcc 2.5.8) don't support 'bool' directly; gcc
dnl 2.6.3 does, in anticipation of the ANSI C++ standard.
dnl
dnl Treat the configuration-variable specially here, since we're directly
dnl substituting its value (i.e., 1/0).
dnl
dnl $1 is the shell variable to store the result in, if not $cv_cv_builtin_bool
AC_DEFUN([CF_BOOL_DECL],
[
AC_MSG_CHECKING(if we should include stdbool.h)

AC_CACHE_VAL(cf_cv_header_stdbool_h,[
	AC_TRY_COMPILE([],[bool foo = false],
		[cf_cv_header_stdbool_h=0],
		[AC_TRY_COMPILE([
#ifndef __BEOS__
#include <stdbool.h>
#endif
],[bool foo = false],
			[cf_cv_header_stdbool_h=1],
			[cf_cv_header_stdbool_h=0])])])

if test "$cf_cv_header_stdbool_h" = 1
then	AC_MSG_RESULT(yes)
else	AC_MSG_RESULT(no)
fi

AC_MSG_CHECKING([for builtin bool type])

AC_CACHE_VAL(ifelse($1,,cf_cv_builtin_bool,[$1]),[
	AC_TRY_COMPILE([
#include <stdio.h>
#include <sys/types.h>
],[bool x = false],
		[ifelse($1,,cf_cv_builtin_bool,[$1])=1],
		[ifelse($1,,cf_cv_builtin_bool,[$1])=0])
	])

if test "$ifelse($1,,cf_cv_builtin_bool,[$1])" = 1
then	AC_MSG_RESULT(yes)
else	AC_MSG_RESULT(no)
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_BOOL_SIZE version: 13 updated: 2013/04/13 18:03:21
dnl ------------
dnl Test for the size of 'bool' in the configured C++ compiler (e.g., a type).
dnl Don't bother looking for bool.h, since it's been deprecated.
dnl
dnl If the current compiler is C rather than C++, we get the bool definition
dnl from <stdbool.h>.
AC_DEFUN([CF_BOOL_SIZE],
[
AC_MSG_CHECKING([for size of bool])
AC_CACHE_VAL(cf_cv_type_of_bool,[
	rm -f cf_test.out
	AC_TRY_RUN([
#include <stdlib.h>
#include <stdio.h>

#if defined(__cplusplus)

#ifdef HAVE_GXX_BUILTIN_H
#include <g++/builtin.h>
#elif HAVE_GPP_BUILTIN_H
#include <gpp/builtin.h>
#elif HAVE_BUILTIN_H
#include <builtin.h>
#endif

#else

#if $cf_cv_header_stdbool_h
#include <stdbool.h>
#endif

#endif

int main()
{
	FILE *fp = fopen("cf_test.out", "w");
	if (fp != 0) {
		bool x = true;
		if ((bool)(-x) >= 0)
			fputs("unsigned ", fp);
		if (sizeof(x) == sizeof(int))       fputs("int",  fp);
		else if (sizeof(x) == sizeof(char)) fputs("char", fp);
		else if (sizeof(x) == sizeof(short))fputs("short",fp);
		else if (sizeof(x) == sizeof(long)) fputs("long", fp);
		fclose(fp);
	}
	${cf_cv_main_return:-return}(0);
}
		],
		[cf_cv_type_of_bool=`cat cf_test.out`
		 if test -z "$cf_cv_type_of_bool"; then
		   cf_cv_type_of_bool=unknown
		 fi],
		[cf_cv_type_of_bool=unknown],
		[cf_cv_type_of_bool=unknown])
	])
	rm -f cf_test.out
AC_MSG_RESULT($cf_cv_type_of_bool)
if test "$cf_cv_type_of_bool" = unknown ; then
	case .$NCURSES_BOOL in #(vi
	.auto|.) NCURSES_BOOL=unsigned;;
	esac
	AC_MSG_WARN(Assuming $NCURSES_BOOL for type of bool)
	cf_cv_type_of_bool=$NCURSES_BOOL
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_BUILD_CC version: 7 updated: 2012/10/06 15:31:55
dnl -----------
dnl If we're cross-compiling, allow the user to override the tools and their
dnl options.  The configure script is oriented toward identifying the host
dnl compiler, etc., but we need a build compiler to generate parts of the
dnl source.
dnl
dnl $1 = default for $CPPFLAGS
dnl $2 = default for $LIBS
AC_DEFUN([CF_BUILD_CC],[
CF_ACVERSION_CHECK(2.52,,
	[AC_REQUIRE([CF_PROG_EXT])])
if test "$cross_compiling" = yes ; then

	# defaults that we might want to override
	: ${BUILD_CFLAGS:=''}
	: ${BUILD_CPPFLAGS:='ifelse([$1],,,[$1])'}
	: ${BUILD_LDFLAGS:=''}
	: ${BUILD_LIBS:='ifelse([$2],,,[$2])'}
	: ${BUILD_EXEEXT:='$x'}
	: ${BUILD_OBJEXT:='o'}

	AC_ARG_WITH(build-cc,
		[  --with-build-cc=XXX     the build C compiler ($BUILD_CC)],
		[BUILD_CC="$withval"],
		[AC_CHECK_PROGS(BUILD_CC, gcc cc cl)])
	AC_MSG_CHECKING(for native build C compiler)
	AC_MSG_RESULT($BUILD_CC)

	AC_MSG_CHECKING(for native build C preprocessor)
	AC_ARG_WITH(build-cpp,
		[  --with-build-cpp=XXX    the build C preprocessor ($BUILD_CPP)],
		[BUILD_CPP="$withval"],
		[BUILD_CPP='${BUILD_CC} -E'])
	AC_MSG_RESULT($BUILD_CPP)

	AC_MSG_CHECKING(for native build C flags)
	AC_ARG_WITH(build-cflags,
		[  --with-build-cflags=XXX the build C compiler-flags ($BUILD_CFLAGS)],
		[BUILD_CFLAGS="$withval"])
	AC_MSG_RESULT($BUILD_CFLAGS)

	AC_MSG_CHECKING(for native build C preprocessor-flags)
	AC_ARG_WITH(build-cppflags,
		[  --with-build-cppflags=XXX the build C preprocessor-flags ($BUILD_CPPFLAGS)],
		[BUILD_CPPFLAGS="$withval"])
	AC_MSG_RESULT($BUILD_CPPFLAGS)

	AC_MSG_CHECKING(for native build linker-flags)
	AC_ARG_WITH(build-ldflags,
		[  --with-build-ldflags=XXX the build linker-flags ($BUILD_LDFLAGS)],
		[BUILD_LDFLAGS="$withval"])
	AC_MSG_RESULT($BUILD_LDFLAGS)

	AC_MSG_CHECKING(for native build linker-libraries)
	AC_ARG_WITH(build-libs,
		[  --with-build-libs=XXX   the build libraries (${BUILD_LIBS})],
		[BUILD_LIBS="$withval"])
	AC_MSG_RESULT($BUILD_LIBS)

	# this assumes we're on Unix.
	BUILD_EXEEXT=
	BUILD_OBJEXT=o

	: ${BUILD_CC:='${CC}'}

	if ( test "$BUILD_CC" = "$CC" || test "$BUILD_CC" = '${CC}' ) ; then
		AC_MSG_ERROR([Cross-build requires two compilers.
Use --with-build-cc to specify the native compiler.])
	fi

else
	: ${BUILD_CC:='${CC}'}
	: ${BUILD_CPP:='${CPP}'}
	: ${BUILD_CFLAGS:='${CFLAGS}'}
	: ${BUILD_CPPFLAGS:='${CPPFLAGS}'}
	: ${BUILD_LDFLAGS:='${LDFLAGS}'}
	: ${BUILD_LIBS:='${LIBS}'}
	: ${BUILD_EXEEXT:='$x'}
	: ${BUILD_OBJEXT:='o'}
fi

AC_SUBST(BUILD_CC)
AC_SUBST(BUILD_CPP)
AC_SUBST(BUILD_CFLAGS)
AC_SUBST(BUILD_CPPFLAGS)
AC_SUBST(BUILD_LDFLAGS)
AC_SUBST(BUILD_LIBS)
AC_SUBST(BUILD_EXEEXT)
AC_SUBST(BUILD_OBJEXT)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_CC_ENV_FLAGS version: 1 updated: 2012/10/03 05:25:49
dnl ---------------
dnl Check for user's environment-breakage by stuffing CFLAGS/CPPFLAGS content
dnl into CC.  This will not help with broken scripts that wrap the compiler with
dnl options, but eliminates a more common category of user confusion.
AC_DEFUN([CF_CC_ENV_FLAGS],
[
# This should have been defined by AC_PROG_CC
: ${CC:=cc}

AC_MSG_CHECKING(\$CC variable)
case "$CC" in #(vi
*[[\ \	]]-[[IUD]]*)
	AC_MSG_RESULT(broken)
	AC_MSG_WARN(your environment misuses the CC variable to hold CFLAGS/CPPFLAGS options)
	# humor him...
	cf_flags=`echo "$CC" | sed -e 's/^[[^ 	]]*[[ 	]]//'`
	CC=`echo "$CC" | sed -e 's/[[ 	]].*//'`
	CF_ADD_CFLAGS($cf_flags)
	;;
*)
	AC_MSG_RESULT(ok)
	;;
esac
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_CFG_DEFAULTS version: 10 updated: 2013/09/07 13:54:05
dnl ---------------
dnl Determine the default configuration into which we'll install ncurses.  This
dnl can be overridden by the user's command-line options.  There's two items to
dnl look for:
dnl	1. the prefix (e.g., /usr)
dnl	2. the header files (e.g., /usr/include/ncurses)
dnl We'll look for a previous installation of ncurses and use the same defaults.
dnl
dnl We don't use AC_PREFIX_DEFAULT, because it gets evaluated too soon, and
dnl we don't use AC_PREFIX_PROGRAM, because we cannot distinguish ncurses's
dnl programs from a vendor's.
AC_DEFUN([CF_CFG_DEFAULTS],
[
AC_MSG_CHECKING(for prefix)
if test "x$prefix" = "xNONE" ; then
	case "$cf_cv_system_name" in
		# non-vendor systems don't have a conflict
	openbsd*|freebsd*|mirbsd*|linux*|cygwin*|msys*|k*bsd*-gnu|mingw*)
		prefix=/usr
		;;
	*)	prefix=$ac_default_prefix
		;;
	esac
fi
AC_MSG_RESULT($prefix)

if test "x$prefix" = "xNONE" ; then
AC_MSG_CHECKING(for default include-directory)
test -n "$verbose" && echo 1>&AC_FD_MSG
for cf_symbol in \
	$includedir \
	$includedir/ncurses \
	$prefix/include \
	$prefix/include/ncurses \
	/usr/local/include \
	/usr/local/include/ncurses \
	/usr/include \
	/usr/include/ncurses
do
	cf_dir=`eval echo $cf_symbol`
	if test -f $cf_dir/curses.h ; then
	if ( fgrep NCURSES_VERSION $cf_dir/curses.h 2>&1 >/dev/null ) ; then
		includedir="$cf_symbol"
		test -n "$verbose"  && echo $ac_n "	found " 1>&AC_FD_MSG
		break
	fi
	fi
	test -n "$verbose"  && echo "	tested $cf_dir" 1>&AC_FD_MSG
done
AC_MSG_RESULT($includedir)
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_CGETENT version: 5 updated: 2012/10/06 17:56:13
dnl ----------
dnl Check if the terminal-capability database functions are available.  If not,
dnl ncurses has a much-reduced version.
AC_DEFUN([CF_CGETENT],[
AC_CACHE_CHECK(for terminal-capability database functions,cf_cv_cgetent,[
AC_TRY_LINK([
#include <stdlib.h>],[
	char temp[128];
	char *buf = temp;
	char *db_array = temp;
	cgetent(&buf, &db_array, "vt100");
	cgetcap(buf, "tc", '=');
	cgetmatch(buf, "tc");
	],
	[cf_cv_cgetent=yes],
	[cf_cv_cgetent=no])
])

if test "$cf_cv_cgetent" = yes
then
	AC_DEFINE(HAVE_BSD_CGETENT,1,[Define to 1 if we have BSD cgetent])
AC_CACHE_CHECK(if cgetent uses const parameter,cf_cv_cgetent_const,[
AC_TRY_LINK([
#include <stdlib.h>],[
	char temp[128];
	char *buf = temp;
#ifndef _NETBSD_SOURCE			/* given, since April 2004 in stdlib.h */
	const char *db_array = temp;
	cgetent(&buf, &db_array, "vt100");
#endif
	cgetcap(buf, "tc", '=');
	cgetmatch(buf, "tc");
	],
	[cf_cv_cgetent_const=yes],
	[cf_cv_cgetent_const=no])
])
	if test "$cf_cv_cgetent_const" = yes
	then
		AC_DEFINE_UNQUOTED(CGETENT_CONST,const,[Define to const if needed for some BSD cgetent variations])
	fi
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_CHECK_CACHE version: 12 updated: 2012/10/02 20:55:03
dnl --------------
dnl Check if we're accidentally using a cache from a different machine.
dnl Derive the system name, as a check for reusing the autoconf cache.
dnl
dnl If we've packaged config.guess and config.sub, run that (since it does a
dnl better job than uname).  Normally we'll use AC_CANONICAL_HOST, but allow
dnl an extra parameter that we may override, e.g., for AC_CANONICAL_SYSTEM
dnl which is useful in cross-compiles.
dnl
dnl Note: we would use $ac_config_sub, but that is one of the places where
dnl autoconf 2.5x broke compatibility with autoconf 2.13
AC_DEFUN([CF_CHECK_CACHE],
[
if test -f $srcdir/config.guess || test -f $ac_aux_dir/config.guess ; then
	ifelse([$1],,[AC_CANONICAL_HOST],[$1])
	system_name="$host_os"
else
	system_name="`(uname -s -r) 2>/dev/null`"
	if test -z "$system_name" ; then
		system_name="`(hostname) 2>/dev/null`"
	fi
fi
test -n "$system_name" && AC_DEFINE_UNQUOTED(SYSTEM_NAME,"$system_name",[Define to the system name.])
AC_CACHE_VAL(cf_cv_system_name,[cf_cv_system_name="$system_name"])

test -z "$system_name" && system_name="$cf_cv_system_name"
test -n "$cf_cv_system_name" && AC_MSG_RESULT(Configuring for $cf_cv_system_name)

if test ".$system_name" != ".$cf_cv_system_name" ; then
	AC_MSG_RESULT(Cached system name ($system_name) does not agree with actual ($cf_cv_system_name))
	AC_MSG_ERROR("Please remove config.cache and try again.")
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_CHECK_ERRNO version: 11 updated: 2010/05/26 05:38:42
dnl --------------
dnl Check for data that is usually declared in <stdio.h> or <errno.h>, e.g.,
dnl the 'errno' variable.  Define a DECL_xxx symbol if we must declare it
dnl ourselves.
dnl
dnl $1 = the name to check
dnl $2 = the assumed type
AC_DEFUN([CF_CHECK_ERRNO],
[
AC_CACHE_CHECK(if external $1 is declared, cf_cv_dcl_$1,[
    AC_TRY_COMPILE([
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <stdio.h>
#include <sys/types.h>
#include <errno.h> ],
    ifelse([$2],,int,[$2]) x = (ifelse([$2],,int,[$2])) $1,
    [cf_cv_dcl_$1=yes],
    [cf_cv_dcl_$1=no])
])

if test "$cf_cv_dcl_$1" = no ; then
    CF_UPPER(cf_result,decl_$1)
    AC_DEFINE_UNQUOTED($cf_result)
fi

# It's possible (for near-UNIX clones) that the data doesn't exist
CF_CHECK_EXTERN_DATA($1,ifelse([$2],,int,[$2]))
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_CHECK_EXTERN_DATA version: 3 updated: 2001/12/30 18:03:23
dnl --------------------
dnl Check for existence of external data in the current set of libraries.  If
dnl we can modify it, it's real enough.
dnl $1 = the name to check
dnl $2 = its type
AC_DEFUN([CF_CHECK_EXTERN_DATA],
[
AC_CACHE_CHECK(if external $1 exists, cf_cv_have_$1,[
    AC_TRY_LINK([
#undef $1
extern $2 $1;
],
    [$1 = 2],
    [cf_cv_have_$1=yes],
    [cf_cv_have_$1=no])
])

if test "$cf_cv_have_$1" = yes ; then
    CF_UPPER(cf_result,have_$1)
    AC_DEFINE_UNQUOTED($cf_result)
fi

])dnl
dnl ---------------------------------------------------------------------------
dnl CF_CHECK_GPM_WGETCH version: 2 updated: 2010/08/14 18:25:37
dnl -------------------
dnl Check if GPM is already linked with curses.  If so - and if the linkage
dnl is not "weak" - warn about this because it can create problems linking
dnl applications with ncurses.
AC_DEFUN([CF_CHECK_GPM_WGETCH],[
AC_CHECK_LIB(gpm,Gpm_Wgetch,[

AC_CACHE_CHECK(if GPM is weakly bound to curses library, cf_cv_check_gpm_wgetch,[
cf_cv_check_gpm_wgetch=unknown
if test "$cross_compiling" != yes ; then

cat >conftest.$ac_ext <<CF_EOF
#include <gpm.h>
int main()
{
	Gpm_Wgetch();
	${cf_cv_main_return:-return}(0);
}
CF_EOF

	cf_save_LIBS="$LIBS"
	# This only works if we can look at the symbol table.  If a shared
	# library is stripped for install, we cannot use that.  So we're forced
	# to rely on the static library, noting that some packagers may not
	# include it.
	LIBS="-static -lgpm -dynamic $LIBS"
	if AC_TRY_EVAL(ac_compile) ; then
		if AC_TRY_EVAL(ac_link) ; then
			cf_cv_check_gpm_wgetch=`nm conftest$ac_exeext | egrep '\<wgetch\>' | egrep '\<[[vVwW]]\>'`
			test -n "$cf_cv_check_gpm_wgetch" && cf_cv_check_gpm_wgetch=yes
			test -z "$cf_cv_check_gpm_wgetch" && cf_cv_check_gpm_wgetch=no
		fi
	fi
	rm -rf conftest*
	LIBS="$cf_save_LIBS"
fi
])

if test "$cf_cv_check_gpm_wgetch" != yes ; then
	AC_MSG_WARN(GPM library is already linked with curses - read the FAQ)
fi
])])dnl
dnl ---------------------------------------------------------------------------
dnl CF_CHECK_LIBTOOL_VERSION version: 1 updated: 2013/04/06 18:03:09
dnl ------------------------
dnl Show the version of libtool
dnl
dnl Save the version in a cache variable - this is not entirely a good thing,
dnl but the version string from libtool is very ugly, and for bug reports it
dnl might be useful to have the original string.
AC_DEFUN([CF_CHECK_LIBTOOL_VERSION],[
if test -n "$LIBTOOL" && test "$LIBTOOL" != none
then
	AC_MSG_CHECKING(version of $LIBTOOL)
	CF_LIBTOOL_VERSION
	AC_MSG_RESULT($cf_cv_libtool_version)
	if test -z "$cf_cv_libtool_version" ; then
		AC_MSG_ERROR(This is not GNU libtool)
	fi
else
	AC_MSG_ERROR(GNU libtool has not been found)
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_CHECK_WCHAR_H version: 1 updated: 2011/10/29 15:01:05
dnl ----------------
dnl Check if wchar.h can be used, i.e., without defining _XOPEN_SOURCE_EXTENDED
AC_DEFUN([CF_CHECK_WCHAR_H],[
AC_CACHE_CHECK(if wchar.h can be used as is,cf_cv_wchar_h_okay,[
AC_TRY_COMPILE(
[
#include <stdlib.h>
#include <wchar.h>
],[
	wint_t foo = 0;
	int bar = iswpunct(foo)],
	[cf_cv_wchar_h_okay=yes],
	[cf_cv_wchar_h_okay=no])])

if test $cf_cv_wchar_h_okay = no
then
	CF_PREDEFINE(_XOPEN_SOURCE_EXTENDED)
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_CLANG_COMPILER version: 2 updated: 2013/11/19 19:23:35
dnl -----------------
dnl Check if the given compiler is really clang.  clang's C driver defines
dnl __GNUC__ (fooling the configure script into setting $GCC to yes) but does
dnl not ignore some gcc options.
dnl
dnl This macro should be run "soon" after AC_PROG_CC or AC_PROG_CPLUSPLUS, to
dnl ensure that it is not mistaken for gcc/g++.  It is normally invoked from
dnl the wrappers for gcc and g++ warnings.
dnl
dnl $1 = GCC (default) or GXX
dnl $2 = CLANG_COMPILER (default)
dnl $3 = CFLAGS (default) or CXXFLAGS
AC_DEFUN([CF_CLANG_COMPILER],[
ifelse([$2],,CLANG_COMPILER,[$2])=no

if test "$ifelse([$1],,[$1],GCC)" = yes ; then
	AC_MSG_CHECKING(if this is really Clang ifelse([$1],GXX,C++,C) compiler)
	cf_save_CFLAGS="$ifelse([$3],,CFLAGS,[$3])"
	ifelse([$3],,CFLAGS,[$3])="$ifelse([$3],,CFLAGS,[$3]) -Qunused-arguments"
	AC_TRY_COMPILE([],[
#ifdef __clang__
#else
make an error
#endif
],[ifelse([$2],,CLANG_COMPILER,[$2])=yes
cf_save_CFLAGS="$cf_save_CFLAGS -Qunused-arguments"
],[])
	ifelse([$3],,CFLAGS,[$3])="$cf_save_CFLAGS"
	AC_MSG_RESULT($ifelse([$2],,CLANG_COMPILER,[$2]))
fi
])
dnl ---------------------------------------------------------------------------
dnl CF_CPP_PARAM_INIT version: 6 updated: 2012/10/06 17:56:13
dnl -----------------
dnl Check if the C++ compiler accepts duplicate parameter initialization.  This
dnl is a late feature for the standard and is not in some recent compilers
dnl (1999/9/11).
AC_DEFUN([CF_CPP_PARAM_INIT],
[
if test -n "$CXX"; then
AC_CACHE_CHECK(if $CXX accepts parameter initialization,cf_cv_cpp_param_init,[
	AC_LANG_SAVE
	AC_LANG_CPLUSPLUS
	AC_TRY_RUN([
class TEST {
private:
	int value;
public:
	TEST(int x = 1);
	~TEST();
};

TEST::TEST(int x = 1)	// some compilers do not like second initializer
{
	value = x;
}
int main() { }
],
	[cf_cv_cpp_param_init=yes],
	[cf_cv_cpp_param_init=no],
	[cf_cv_cpp_param_init=unknown])
	AC_LANG_RESTORE
])
fi
test "$cf_cv_cpp_param_init" = yes && AC_DEFINE(CPP_HAS_PARAM_INIT,1,[Define to 1 if C++ has parameter initialization])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_CPP_STATIC_CAST version: 3 updated: 2013/04/13 18:03:21
dnl ------------------
dnl Check if the C++ compiler accepts static_cast in generics.  This appears to
dnl not be supported in g++ before 3.0
AC_DEFUN([CF_CPP_STATIC_CAST],
[
if test -n "$CXX"; then

AC_CACHE_CHECK(if $CXX accepts static_cast,cf_cv_cpp_static_cast,[
	AC_LANG_SAVE
	AC_LANG_CPLUSPLUS

	AC_TRY_COMPILE([
class NCursesPanel
{
public:
  NCursesPanel(int nlines,
	       int ncols,
	       int begin_y = 0,
	       int begin_x = 0)
  {
  }
  NCursesPanel();
  ~NCursesPanel();
};

template<class T> class NCursesUserPanel : public NCursesPanel
{
public:
  NCursesUserPanel (int nlines,
		    int ncols,
		    int begin_y = 0,
		    int begin_x = 0,
		    const T* p_UserData = static_cast<T*>(0))
    : NCursesPanel (nlines, ncols, begin_y, begin_x)
  {
  };
  NCursesUserPanel(const T* p_UserData = static_cast<T*>(0)) : NCursesPanel()
  {
  };

  virtual ~NCursesUserPanel() {};
};
],[
	const char* p_UserData = static_cast<char*>(0)],
	[cf_cv_cpp_static_cast=yes],
	[cf_cv_cpp_static_cast=no])

	AC_LANG_RESTORE
])

fi

test "$cf_cv_cpp_static_cast" = yes && AC_DEFINE(CPP_HAS_STATIC_CAST,1,[Define to 1 if C++ has static_cast])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_CXX_AR_FLAGS version: 1 updated: 2011/10/29 08:35:34
dnl ---------------
dnl Setup special archiver flags for given compilers.
AC_DEFUN([CF_CXX_AR_FLAGS],[
	CXX_AR='$(AR)'
	CXX_ARFLAGS='$(ARFLAGS)'
	case $cf_cv_system_name in #(vi
	irix*) #(vi
	    if test "$GXX" != yes ; then
		CXX_AR='$(CXX)'
		CXX_ARFLAGS='-ar -o'
	    fi
	    ;;
	sco3.2v5*) #(vi
	    CXXLDFLAGS="-u main"
	    ;;
	solaris2*)
	    if test "$GXX" != yes ; then
		CXX_AR='$(CXX)'
		CXX_ARFLAGS='-xar -o'
	    fi
	    ;;
	esac
	AC_SUBST(CXXLDFLAGS)
	AC_SUBST(CXX_AR)
	AC_SUBST(CXX_ARFLAGS)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_CXX_IOSTREAM_NAMESPACE version: 2 updated: 2012/10/06 17:56:13
dnl -------------------------
dnl For c++, check if iostream uses "std::" namespace.
AC_DEFUN([CF_CXX_IOSTREAM_NAMESPACE],[
AC_CHECK_HEADERS(iostream)
if test x"$ac_cv_header_iostream" = xyes ; then
	AC_MSG_CHECKING(if iostream uses std-namespace)
	AC_TRY_COMPILE([
#include <iostream>
using std::endl;
using std::cerr;],[
cerr << "testing" << endl;
],[cf_iostream_namespace=yes],[cf_iostream_namespace=no])
	AC_MSG_RESULT($cf_iostream_namespace)
	if test "$cf_iostream_namespace" = yes ; then
		AC_DEFINE(IOSTREAM_NAMESPACE,1,[Define to 1 if C++ has namespace iostream])
	fi
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_C_INLINE version: 4 updated: 2012/06/16 14:55:39
dnl -----------
dnl Check if the C compiler supports "inline".
dnl $1 is the name of a shell variable to set if inline is supported
dnl $2 is the threshold for gcc 4.x's option controlling maximum inline size
AC_DEFUN([CF_C_INLINE],[
AC_C_INLINE
$1=
if test "$ac_cv_c_inline" != no ; then
  $1=inline
  if test "$INTEL_COMPILER" = yes
  then
    :
  elif test "$CLANG_COMPILER" = yes
  then
    :
  elif test "$GCC" = yes
  then
    AC_CACHE_CHECK(if $CC supports options to tune inlining,cf_cv_gcc_inline,[
      cf_save_CFLAGS=$CFLAGS
      CFLAGS="$CFLAGS --param max-inline-insns-single=$2"
      AC_TRY_COMPILE([inline int foo(void) { return 1; }],
      [${cf_cv_main_return:-return} foo()],
      [cf_cv_gcc_inline=yes],
      [cf_cv_gcc_inline=no])
      CFLAGS=$cf_save_CFLAGS
    ])
    if test "$cf_cv_gcc_inline" = yes ; then
        CF_ADD_CFLAGS([--param max-inline-insns-single=$2])
    fi
  fi
fi
AC_SUBST($1)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_DIRNAME version: 4 updated: 2002/12/21 19:25:52
dnl ----------
dnl "dirname" is not portable, so we fake it with a shell script.
AC_DEFUN([CF_DIRNAME],[$1=`echo $2 | sed -e 's%/[[^/]]*$%%'`])dnl
dnl ---------------------------------------------------------------------------
dnl CF_DIRS_TO_MAKE version: 3 updated: 2002/02/23 20:38:31
dnl ---------------
AC_DEFUN([CF_DIRS_TO_MAKE],
[
DIRS_TO_MAKE="lib"
for cf_item in $cf_list_models
do
	CF_OBJ_SUBDIR($cf_item,cf_subdir)
	for cf_item2 in $DIRS_TO_MAKE
	do
		test $cf_item2 = $cf_subdir && break
	done
	test ".$cf_item2" != ".$cf_subdir" && DIRS_TO_MAKE="$DIRS_TO_MAKE $cf_subdir"
done
for cf_dir in $DIRS_TO_MAKE
do
	test ! -d $cf_dir && mkdir $cf_dir
done
AC_SUBST(DIRS_TO_MAKE)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_DISABLE_ECHO version: 12 updated: 2012/10/06 16:30:28
dnl ---------------
dnl You can always use "make -n" to see the actual options, but it's hard to
dnl pick out/analyze warning messages when the compile-line is long.
dnl
dnl Sets:
dnl	ECHO_LT - symbol to control if libtool is verbose
dnl	ECHO_LD - symbol to prefix "cc -o" lines
dnl	RULE_CC - symbol to put before implicit "cc -c" lines (e.g., .c.o)
dnl	SHOW_CC - symbol to put before explicit "cc -c" lines
dnl	ECHO_CC - symbol to put before any "cc" line
dnl
AC_DEFUN([CF_DISABLE_ECHO],[
AC_MSG_CHECKING(if you want to see long compiling messages)
CF_ARG_DISABLE(echo,
	[  --disable-echo          do not display "compiling" commands],
	[
    ECHO_LT='--silent'
    ECHO_LD='@echo linking [$]@;'
    RULE_CC='@echo compiling [$]<'
    SHOW_CC='@echo compiling [$]@'
    ECHO_CC='@'
],[
    ECHO_LT=''
    ECHO_LD=''
    RULE_CC=''
    SHOW_CC=''
    ECHO_CC=''
])
AC_MSG_RESULT($enableval)
AC_SUBST(ECHO_LT)
AC_SUBST(ECHO_LD)
AC_SUBST(RULE_CC)
AC_SUBST(SHOW_CC)
AC_SUBST(ECHO_CC)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_DISABLE_LEAKS version: 7 updated: 2012/10/02 20:55:03
dnl ----------------
dnl Combine no-leak checks with the libraries or tools that are used for the
dnl checks.
AC_DEFUN([CF_DISABLE_LEAKS],[

AC_REQUIRE([CF_WITH_DMALLOC])
AC_REQUIRE([CF_WITH_DBMALLOC])
AC_REQUIRE([CF_WITH_VALGRIND])

AC_MSG_CHECKING(if you want to perform memory-leak testing)
AC_ARG_ENABLE(leaks,
	[  --disable-leaks         test: free permanent memory, analyze leaks],
	[if test "x$enableval" = xno; then with_no_leaks=yes; else with_no_leaks=no; fi],
	: ${with_no_leaks:=no})
AC_MSG_RESULT($with_no_leaks)

if test "$with_no_leaks" = yes ; then
	AC_DEFINE(NO_LEAKS,1,[Define to 1 if you want to perform memory-leak testing.])
	AC_DEFINE(YY_NO_LEAKS,1,[Define to 1 if you want to perform memory-leak testing.])
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_DISABLE_LIBTOOL_VERSION version: 1 updated: 2010/05/15 15:45:59
dnl --------------------------
dnl Check if we should use the libtool 1.5 feature "-version-number" instead of
dnl the older "-version-info" feature.  The newer feature allows us to use
dnl version numbering on shared libraries which make them compatible with
dnl various systems.
AC_DEFUN([CF_DISABLE_LIBTOOL_VERSION],
[
AC_MSG_CHECKING(if libtool -version-number should be used)
CF_ARG_DISABLE(libtool-version,
	[  --disable-libtool-version  enable to use libtool's incompatible naming scheme],
	[cf_libtool_version=no],
	[cf_libtool_version=yes])
AC_MSG_RESULT($cf_libtool_version)

if test "$cf_libtool_version" = yes ; then
	LIBTOOL_VERSION="-version-number"
else
	LIBTOOL_VERSION="-version-info"
fi

AC_SUBST(LIBTOOL_VERSION)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_DISABLE_RPATH_HACK version: 2 updated: 2011/02/13 13:31:33
dnl ---------------------
dnl The rpath-hack makes it simpler to build programs, particularly with the
dnl *BSD ports which may have essential libraries in unusual places.  But it
dnl can interfere with building an executable for the base system.  Use this
dnl option in that case.
AC_DEFUN([CF_DISABLE_RPATH_HACK],
[
AC_MSG_CHECKING(if rpath-hack should be disabled)
CF_ARG_DISABLE(rpath-hack,
	[  --disable-rpath-hack    don't add rpath options for additional libraries],
	[cf_disable_rpath_hack=yes],
	[cf_disable_rpath_hack=no])
AC_MSG_RESULT($cf_disable_rpath_hack)
if test "$cf_disable_rpath_hack" = no ; then
	CF_RPATH_HACK
fi
])
dnl ---------------------------------------------------------------------------
dnl CF_ENABLE_PC_FILES version: 9 updated: 2012/08/04 13:59:54
dnl ------------------
dnl This is the "--enable-pc-files" option, which is available if there is a
dnl pkg-config configuration on the local machine.
AC_DEFUN([CF_ENABLE_PC_FILES],[
AC_REQUIRE([CF_PKG_CONFIG])
AC_REQUIRE([CF_WITH_PKG_CONFIG_LIBDIR])

if test "$PKG_CONFIG" != none ; then
	AC_MSG_CHECKING(if we should install .pc files for $PKG_CONFIG)
	AC_ARG_ENABLE(pc-files,
		[  --enable-pc-files       generate and install .pc files for pkg-config],
		[enable_pc_files=$enableval],
		[enable_pc_files=no])
	AC_MSG_RESULT($enable_pc_files)
	if test "$enable_pc_files" != no
	then
		CF_PATH_SYNTAX(PKG_CONFIG_LIBDIR)
	fi
else
	enable_pc_files=no
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ENABLE_RPATH version: 2 updated: 2010/03/27 18:39:42
dnl ---------------
dnl Check if the rpath option should be used, setting cache variable
dnl cf_cv_enable_rpath if so.
AC_DEFUN([CF_ENABLE_RPATH],
[
AC_MSG_CHECKING(if rpath option should be used)
AC_ARG_ENABLE(rpath,
[  --enable-rpath          use rpath option when generating shared libraries],
[cf_cv_enable_rpath=$enableval],
[cf_cv_enable_rpath=no])
AC_MSG_RESULT($cf_cv_enable_rpath)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ENABLE_STRING_HACKS version: 3 updated: 2013/01/26 16:26:12
dnl ----------------------
dnl On a few platforms, the compiler and/or loader nags with untruthful
dnl comments stating that "most" uses of strcat/strcpy/sprintf are incorrect,
dnl and implying that most uses of the recommended alternatives are correct.
dnl
dnl Factually speaking, no one has actually counted the number of uses of these
dnl functions versus the total of incorrect uses.  Samples of a few thousand
dnl instances are meaningless compared to the hundreds of millions of lines of
dnl existing C code.
dnl
dnl strlcat/strlcpy are (as of 2012) non-standard, and are available on some
dnl platforms, in implementations of varying quality.  Likewise, snprintf is
dnl standard - but evolved through phases, and older implementations are likely
dnl to yield surprising results, as documented in manpages on various systems.
AC_DEFUN([CF_ENABLE_STRING_HACKS],
[
AC_MSG_CHECKING(if you want to work around bogus compiler/loader warnings)
AC_ARG_ENABLE(string-hacks,
	[  --enable-string-hacks   work around bogus compiler/loader warnings],
	[with_string_hacks=$enableval],
	[with_string_hacks=no])
AC_MSG_RESULT($with_string_hacks)

if test "x$with_string_hacks" = "xyes"; then
 	AC_DEFINE(USE_STRING_HACKS,1,[Define to 1 to work around bogus compiler/loader warnings])
	AC_MSG_WARN(enabling string-hacks to work around bogus compiler/loader warnings)
	AC_CHECK_FUNCS( strlcat strlcpy snprintf )
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ERRNO version: 5 updated: 1997/11/30 12:44:39
dnl --------
dnl Check if 'errno' is declared in <errno.h>
AC_DEFUN([CF_ERRNO],
[
CF_CHECK_ERRNO(errno)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ETIP_DEFINES version: 5 updated: 2012/02/18 17:51:07
dnl ---------------
dnl Test for conflicting definitions of exception in gcc 2.8.0, etc., between
dnl math.h and builtin.h, only for ncurses
AC_DEFUN([CF_ETIP_DEFINES],
[
AC_MSG_CHECKING(for special defines needed for etip.h)
cf_save_CXXFLAGS="$CXXFLAGS"
cf_result="none"

# etip.h includes ncurses.h which includes ncurses_dll.h
# But ncurses_dll.h is generated - fix here.
test -d include || mkdir include
test -f include/ncurses_dll.h || sed -e 's/@NCURSES_WRAP_PREFIX@/'$NCURSES_WRAP_PREFIX'/g' ${srcdir}/include/ncurses_dll.h.in >include/ncurses_dll.h

for cf_math in "" MATH_H
do
for cf_excp in "" MATH_EXCEPTION
do
	CXXFLAGS="$cf_save_CXXFLAGS -I${srcdir}/c++ -I${srcdir}/menu -Iinclude -I${srcdir}/include"
	test -n "$cf_math" && CXXFLAGS="$CXXFLAGS -DETIP_NEEDS_${cf_math}"
	test -n "$cf_excp" && CXXFLAGS="$CXXFLAGS -DETIP_NEEDS_${cf_excp}"
AC_TRY_COMPILE([
#include <etip.h.in>
],[],[
	test -n "$cf_math" && AC_DEFINE_UNQUOTED(ETIP_NEEDS_${cf_math})
	test -n "$cf_excp" && AC_DEFINE_UNQUOTED(ETIP_NEEDS_${cf_excp})
	cf_result="$cf_math $cf_excp"
	break 2
],[])
done
done
AC_MSG_RESULT($cf_result)
CXXFLAGS="$cf_save_CXXFLAGS"
])
dnl ---------------------------------------------------------------------------
dnl CF_FIND_LINKAGE version: 19 updated: 2010/05/29 16:31:02
dnl ---------------
dnl Find a library (specifically the linkage used in the code fragment),
dnl searching for it if it is not already in the library path.
dnl See also CF_ADD_SEARCHPATH.
dnl
dnl Parameters (4-on are optional):
dnl     $1 = headers for library entrypoint
dnl     $2 = code fragment for library entrypoint
dnl     $3 = the library name without the "-l" option or ".so" suffix.
dnl     $4 = action to perform if successful (default: update CPPFLAGS, etc)
dnl     $5 = action to perform if not successful
dnl     $6 = module name, if not the same as the library name
dnl     $7 = extra libraries
dnl
dnl Sets these variables:
dnl     $cf_cv_find_linkage_$3 - yes/no according to whether linkage is found
dnl     $cf_cv_header_path_$3 - include-directory if needed
dnl     $cf_cv_library_path_$3 - library-directory if needed
dnl     $cf_cv_library_file_$3 - library-file if needed, e.g., -l$3
AC_DEFUN([CF_FIND_LINKAGE],[

# If the linkage is not already in the $CPPFLAGS/$LDFLAGS configuration, these
# will be set on completion of the AC_TRY_LINK below.
cf_cv_header_path_$3=
cf_cv_library_path_$3=

CF_MSG_LOG([Starting [FIND_LINKAGE]($3,$6)])

cf_save_LIBS="$LIBS"

AC_TRY_LINK([$1],[$2],[
	cf_cv_find_linkage_$3=yes
	cf_cv_header_path_$3=/usr/include
	cf_cv_library_path_$3=/usr/lib
],[

LIBS="-l$3 $7 $cf_save_LIBS"

AC_TRY_LINK([$1],[$2],[
	cf_cv_find_linkage_$3=yes
	cf_cv_header_path_$3=/usr/include
	cf_cv_library_path_$3=/usr/lib
	cf_cv_library_file_$3="-l$3"
],[
	cf_cv_find_linkage_$3=no
	LIBS="$cf_save_LIBS"

    CF_VERBOSE(find linkage for $3 library)
    CF_MSG_LOG([Searching for headers in [FIND_LINKAGE]($3,$6)])

    cf_save_CPPFLAGS="$CPPFLAGS"
    cf_test_CPPFLAGS="$CPPFLAGS"

    CF_HEADER_PATH(cf_search,ifelse([$6],,[$3],[$6]))
    for cf_cv_header_path_$3 in $cf_search
    do
      if test -d $cf_cv_header_path_$3 ; then
        CF_VERBOSE(... testing $cf_cv_header_path_$3)
        CPPFLAGS="$cf_save_CPPFLAGS -I$cf_cv_header_path_$3"
        AC_TRY_COMPILE([$1],[$2],[
            CF_VERBOSE(... found $3 headers in $cf_cv_header_path_$3)
            cf_cv_find_linkage_$3=maybe
            cf_test_CPPFLAGS="$CPPFLAGS"
            break],[
            CPPFLAGS="$cf_save_CPPFLAGS"
            ])
      fi
    done

    if test "$cf_cv_find_linkage_$3" = maybe ; then

      CF_MSG_LOG([Searching for $3 library in [FIND_LINKAGE]($3,$6)])

      cf_save_LIBS="$LIBS"
      cf_save_LDFLAGS="$LDFLAGS"

      ifelse([$6],,,[
        CPPFLAGS="$cf_test_CPPFLAGS"
        LIBS="-l$3 $7 $cf_save_LIBS"
        AC_TRY_LINK([$1],[$2],[
            CF_VERBOSE(... found $3 library in system)
            cf_cv_find_linkage_$3=yes])
            CPPFLAGS="$cf_save_CPPFLAGS"
            LIBS="$cf_save_LIBS"
            ])

      if test "$cf_cv_find_linkage_$3" != yes ; then
        CF_LIBRARY_PATH(cf_search,$3)
        for cf_cv_library_path_$3 in $cf_search
        do
          if test -d $cf_cv_library_path_$3 ; then
            CF_VERBOSE(... testing $cf_cv_library_path_$3)
            CPPFLAGS="$cf_test_CPPFLAGS"
            LIBS="-l$3 $7 $cf_save_LIBS"
            LDFLAGS="$cf_save_LDFLAGS -L$cf_cv_library_path_$3"
            AC_TRY_LINK([$1],[$2],[
                CF_VERBOSE(... found $3 library in $cf_cv_library_path_$3)
                cf_cv_find_linkage_$3=yes
                cf_cv_library_file_$3="-l$3"
                break],[
                CPPFLAGS="$cf_save_CPPFLAGS"
                LIBS="$cf_save_LIBS"
                LDFLAGS="$cf_save_LDFLAGS"
                ])
          fi
        done
        CPPFLAGS="$cf_save_CPPFLAGS"
        LDFLAGS="$cf_save_LDFLAGS"
      fi

    else
      cf_cv_find_linkage_$3=no
    fi
    ],$7)
])

LIBS="$cf_save_LIBS"

if test "$cf_cv_find_linkage_$3" = yes ; then
ifelse([$4],,[
	CF_ADD_INCDIR($cf_cv_header_path_$3)
	CF_ADD_LIBDIR($cf_cv_library_path_$3)
	CF_ADD_LIB($3)
],[$4])
else
ifelse([$5],,AC_MSG_WARN(Cannot find $3 library),[$5])
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_FIXUP_ADAFLAGS version: 1 updated: 2012/03/31 18:48:10
dnl -----------------
dnl make ADAFLAGS consistent with CFLAGS
AC_DEFUN([CF_FIXUP_ADAFLAGS],[
	AC_MSG_CHECKING(optimization options for ADAFLAGS)
	case "$CFLAGS" in
	*-g*)
		CF_ADD_ADAFLAGS(-g)
		;;
	esac
	case "$CFLAGS" in
	*-O*)
		cf_O_flag=`echo "$CFLAGS" |sed -e 's/^.*-O/-O/' -e 's/[[ 	]].*//'`
		CF_ADD_ADAFLAGS($cf_O_flag)
		;;
	esac
	AC_MSG_RESULT($ADAFLAGS)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_FORGET_TOOL version: 1 updated: 2013/04/06 18:03:09
dnl --------------
dnl Forget that we saw the given tool.
AC_DEFUN([CF_FORGET_TOOL],[
unset ac_cv_prog_ac_ct_$1
unset ac_ct_$1
unset $1
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_FUNC_DLSYM version: 3 updated: 2012/10/06 11:17:15
dnl -------------
dnl Test for dlsym() and related functions, as well as libdl.
dnl
dnl Sets
dnl	$cf_have_dlsym
dnl	$cf_have_libdl
AC_DEFUN([CF_FUNC_DLSYM],[
cf_have_dlsym=no
AC_CHECK_FUNC(dlsym,cf_have_dlsym=yes,[

cf_have_libdl=no
AC_CHECK_LIB(dl,dlsym,[
	cf_have_dlsym=yes
	cf_have_libdl=yes])])

if test "$cf_have_dlsym" = yes ; then
	test "$cf_have_libdl" = yes && CF_ADD_LIB(dl)

	AC_MSG_CHECKING(whether able to link to dl*() functions)
	AC_TRY_LINK([#include <dlfcn.h>],[
		void *obj;
		if ((obj = dlopen("filename", 0)) != 0) {
			if (dlsym(obj, "symbolname") == 0) {
			dlclose(obj);
			}
		}],[
		AC_DEFINE(HAVE_LIBDL,1,[Define to 1 if we have dl library])],[
		AC_MSG_ERROR(Cannot link test program for libdl)])
	AC_MSG_RESULT(ok)
else
	AC_MSG_ERROR(Cannot find dlsym function)
fi
])
dnl ---------------------------------------------------------------------------
dnl CF_FUNC_MEMMOVE version: 8 updated: 2012/10/04 20:12:20
dnl ---------------
dnl Check for memmove, or a bcopy that can handle overlapping copy.  If neither
dnl is found, add our own version of memmove to the list of objects.
AC_DEFUN([CF_FUNC_MEMMOVE],
[
AC_CHECK_FUNC(memmove,,[
AC_CHECK_FUNC(bcopy,[
	AC_CACHE_CHECK(if bcopy does overlapping moves,cf_cv_good_bcopy,[
		AC_TRY_RUN([
int main() {
	static char data[] = "abcdefghijklmnopqrstuwwxyz";
	char temp[40];
	bcopy(data, temp, sizeof(data));
	bcopy(temp+10, temp, 15);
	bcopy(temp+5, temp+15, 10);
	${cf_cv_main_return:-return} (strcmp(temp, "klmnopqrstuwwxypqrstuwwxyz"));
}
		],
		[cf_cv_good_bcopy=yes],
		[cf_cv_good_bcopy=no],
		[cf_cv_good_bcopy=unknown])
		])
	],[cf_cv_good_bcopy=no])
	if test "$cf_cv_good_bcopy" = yes ; then
		AC_DEFINE(USE_OK_BCOPY,1,[Define to 1 to use bcopy when memmove is unavailable])
	else
		AC_DEFINE(USE_MY_MEMMOVE,1,[Define to 1 to use replacement function when memmove is unavailable])
	fi
])])dnl
dnl ---------------------------------------------------------------------------
dnl CF_FUNC_NANOSLEEP version: 4 updated: 2012/10/06 17:56:13
dnl -----------------
dnl Check for existence of workable nanosleep() function.  Some systems, e.g.,
dnl AIX 4.x, provide a non-working version.
AC_DEFUN([CF_FUNC_NANOSLEEP],[
AC_CACHE_CHECK(if nanosleep really works,cf_cv_func_nanosleep,[
AC_TRY_RUN([
#include <stdio.h>
#include <errno.h>
#include <time.h>

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

int main() {
	struct timespec ts1, ts2;
	int code;
	ts1.tv_sec  = 0;
	ts1.tv_nsec = 750000000;
	ts2.tv_sec  = 0;
	ts2.tv_nsec = 0;
	errno = 0;
	code = nanosleep(&ts1, &ts2); /* on failure errno is ENOSYS. */
	${cf_cv_main_return:-return}(code != 0);
}
],
	[cf_cv_func_nanosleep=yes],
	[cf_cv_func_nanosleep=no],
	[cf_cv_func_nanosleep=unknown])])

test "$cf_cv_func_nanosleep" = "yes" && AC_DEFINE(HAVE_NANOSLEEP,1,[Define to 1 if we have nanosleep()])
])
dnl ---------------------------------------------------------------------------
dnl CF_FUNC_OPENPTY version: 3 updated: 2010/05/29 16:31:02
dnl ---------------
dnl Check for openpty() function, along with <pty.h> header.  It may need the
dnl "util" library as well.
AC_DEFUN([CF_FUNC_OPENPTY],
[
AC_CHECK_LIB(util,openpty,cf_cv_lib_util=yes,cf_cv_lib_util=no)
AC_CACHE_CHECK(for openpty header,cf_cv_func_openpty,[
    cf_save_LIBS="$LIBS"
    test $cf_cv_lib_util = yes && CF_ADD_LIB(util)
    for cf_header in pty.h libutil.h util.h
    do
    AC_TRY_LINK([
#include <$cf_header>
],[
    int x = openpty((int *)0, (int *)0, (char *)0,
                   (struct termios *)0, (struct winsize *)0);
],[
        cf_cv_func_openpty=$cf_header
        break
],[
        cf_cv_func_openpty=no
])
    done
    LIBS="$cf_save_LIBS"
])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_FUNC_POLL version: 8 updated: 2012/10/04 05:24:07
dnl ------------
dnl See if the poll function really works.  Some platforms have poll(), but
dnl it does not work for terminals or files.
AC_DEFUN([CF_FUNC_POLL],[
AC_CACHE_CHECK(if poll really works,cf_cv_working_poll,[
AC_TRY_RUN([
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef HAVE_POLL_H
#include <poll.h>
#else
#include <sys/poll.h>
#endif
int main() {
	struct pollfd myfds;
	int ret;

	/* check for Darwin bug with respect to "devices" */
	myfds.fd = open("/dev/null", 1);	/* O_WRONLY */
	if (myfds.fd < 0)
		myfds.fd = 0;
	myfds.events = POLLIN;
	myfds.revents = 0;

	ret = poll(&myfds, 1, 100);

	if (ret < 0 || (myfds.revents & POLLNVAL)) {
		ret = -1;
	} else {
		int fd = 0;
		if (!isatty(fd)) {
			fd = open("/dev/tty", 2);	/* O_RDWR */
		}

		if (fd >= 0) {
			/* also check with standard input */
			myfds.fd = fd;
			myfds.events = POLLIN;
			myfds.revents = 0;
			ret = poll(&myfds, 1, 100);
		} else {
			ret = -1;
		}
	}
	${cf_cv_main_return:-return}(ret < 0);
}],
	[cf_cv_working_poll=yes],
	[cf_cv_working_poll=no],
	[cf_cv_working_poll=unknown])])
test "$cf_cv_working_poll" = "yes" && AC_DEFINE(HAVE_WORKING_POLL,1,[Define to 1 if the poll function seems to work])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_FUNC_TERMIOS version: 3 updated: 2012/10/06 17:56:13
dnl ---------------
dnl Some old/broken variations define tcgetattr() only as a macro in
dnl termio(s).h
AC_DEFUN([CF_FUNC_TERMIOS],[
AC_REQUIRE([CF_STRUCT_TERMIOS])
AC_CACHE_CHECK(for tcgetattr, cf_cv_have_tcgetattr,[
AC_TRY_LINK([
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#define TTY struct termios
#else
#ifdef HAVE_TERMIO_H
#include <termio.h>
#define TTY struct termio
#endif
#endif
],[
TTY foo;
tcgetattr(1, &foo);],
[cf_cv_have_tcgetattr=yes],
[cf_cv_have_tcgetattr=no])])
test "$cf_cv_have_tcgetattr" = yes && AC_DEFINE(HAVE_TCGETATTR,1,[Define to 1 if we have tcgetattr])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_FUNC_VSSCANF version: 4 updated: 2012/10/06 17:56:13
dnl ---------------
dnl Check for vsscanf() function, which is in c9x but generally not in earlier
dnl versions of C.  It is in the GNU C library, and can often be simulated by
dnl other functions.
AC_DEFUN([CF_FUNC_VSSCANF],
[
AC_CACHE_CHECK(for vsscanf function or workaround,cf_cv_func_vsscanf,[
AC_TRY_LINK([
#include <stdarg.h>
#include <stdio.h>],[
	va_list ap;
	vsscanf("from", "%d", ap)],[cf_cv_func_vsscanf=vsscanf],[
AC_TRY_LINK([
#include <stdarg.h>
#include <stdio.h>],[
    FILE strbuf;
    char *str = "from";

    strbuf._flag = _IOREAD;
    strbuf._ptr = strbuf._base = (unsigned char *) str;
    strbuf._cnt = strlen(str);
    strbuf._file = _NFILE;
    return (vfscanf(&strbuf, "%d", ap))],[cf_cv_func_vsscanf=vfscanf],[
AC_TRY_LINK([
#include <stdarg.h>
#include <stdio.h>],[
    FILE strbuf;
    char *str = "from";

    strbuf._flag = _IOREAD;
    strbuf._ptr = strbuf._base = (unsigned char *) str;
    strbuf._cnt = strlen(str);
    strbuf._file = _NFILE;
    return (_doscan(&strbuf, "%d", ap))],[cf_cv_func_vsscanf=_doscan],[
cf_cv_func_vsscanf=no])])])])

case $cf_cv_func_vsscanf in #(vi
vsscanf) AC_DEFINE(HAVE_VSSCANF,1,[Define to 1 if we have vsscanf]);; #(vi
vfscanf) AC_DEFINE(HAVE_VFSCANF,1,[Define to 1 if we have vfscanf]);; #(vi
_doscan) AC_DEFINE(HAVE__DOSCAN,1,[Define to 1 if we have _doscan]);;
esac

])dnl
dnl ---------------------------------------------------------------------------
dnl CF_GCC_ATTRIBUTES version: 16 updated: 2012/10/02 20:55:03
dnl -----------------
dnl Test for availability of useful gcc __attribute__ directives to quiet
dnl compiler warnings.  Though useful, not all are supported -- and contrary
dnl to documentation, unrecognized directives cause older compilers to barf.
AC_DEFUN([CF_GCC_ATTRIBUTES],
[
if test "$GCC" = yes
then
cat > conftest.i <<EOF
#ifndef GCC_PRINTF
#define GCC_PRINTF 0
#endif
#ifndef GCC_SCANF
#define GCC_SCANF 0
#endif
#ifndef GCC_NORETURN
#define GCC_NORETURN /* nothing */
#endif
#ifndef GCC_UNUSED
#define GCC_UNUSED /* nothing */
#endif
EOF
if test "$GCC" = yes
then
	AC_CHECKING([for $CC __attribute__ directives])
cat > conftest.$ac_ext <<EOF
#line __oline__ "${as_me:-configure}"
#include "confdefs.h"
#include "conftest.h"
#include "conftest.i"
#if	GCC_PRINTF
#define GCC_PRINTFLIKE(fmt,var) __attribute__((format(printf,fmt,var)))
#else
#define GCC_PRINTFLIKE(fmt,var) /*nothing*/
#endif
#if	GCC_SCANF
#define GCC_SCANFLIKE(fmt,var)  __attribute__((format(scanf,fmt,var)))
#else
#define GCC_SCANFLIKE(fmt,var)  /*nothing*/
#endif
extern void wow(char *,...) GCC_SCANFLIKE(1,2);
extern void oops(char *,...) GCC_PRINTFLIKE(1,2) GCC_NORETURN;
extern void foo(void) GCC_NORETURN;
int main(int argc GCC_UNUSED, char *argv[[]] GCC_UNUSED) { return 0; }
EOF
	cf_printf_attribute=no
	cf_scanf_attribute=no
	for cf_attribute in scanf printf unused noreturn
	do
		CF_UPPER(cf_ATTRIBUTE,$cf_attribute)
		cf_directive="__attribute__(($cf_attribute))"
		echo "checking for $CC $cf_directive" 1>&AC_FD_CC

		case $cf_attribute in #(vi
		printf) #(vi
			cf_printf_attribute=yes
			cat >conftest.h <<EOF
#define GCC_$cf_ATTRIBUTE 1
EOF
			;;
		scanf) #(vi
			cf_scanf_attribute=yes
			cat >conftest.h <<EOF
#define GCC_$cf_ATTRIBUTE 1
EOF
			;;
		*) #(vi
			cat >conftest.h <<EOF
#define GCC_$cf_ATTRIBUTE $cf_directive
EOF
			;;
		esac

		if AC_TRY_EVAL(ac_compile); then
			test -n "$verbose" && AC_MSG_RESULT(... $cf_attribute)
			cat conftest.h >>confdefs.h
			case $cf_attribute in #(vi
			noreturn) #(vi
				AC_DEFINE_UNQUOTED(GCC_NORETURN,$cf_directive,[Define to noreturn-attribute for gcc])
				;;
			printf) #(vi
				cf_value='/* nothing */'
				if test "$cf_printf_attribute" != no ; then
					cf_value='__attribute__((format(printf,fmt,var)))'
					AC_DEFINE(GCC_PRINTF,1,[Define to 1 if the compiler supports gcc-like printf attribute.])
				fi
				AC_DEFINE_UNQUOTED(GCC_PRINTFLIKE(fmt,var),$cf_value,[Define to printf-attribute for gcc])
				;;
			scanf) #(vi
				cf_value='/* nothing */'
				if test "$cf_scanf_attribute" != no ; then
					cf_value='__attribute__((format(scanf,fmt,var)))'
					AC_DEFINE(GCC_SCANF,1,[Define to 1 if the compiler supports gcc-like scanf attribute.])
				fi
				AC_DEFINE_UNQUOTED(GCC_SCANFLIKE(fmt,var),$cf_value,[Define to sscanf-attribute for gcc])
				;;
			unused) #(vi
				AC_DEFINE_UNQUOTED(GCC_UNUSED,$cf_directive,[Define to unused-attribute for gcc])
				;;
			esac
		fi
	done
else
	fgrep define conftest.i >>confdefs.h
fi
rm -rf conftest*
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_GCC_VERSION version: 7 updated: 2012/10/18 06:46:33
dnl --------------
dnl Find version of gcc
AC_DEFUN([CF_GCC_VERSION],[
AC_REQUIRE([AC_PROG_CC])
GCC_VERSION=none
if test "$GCC" = yes ; then
	AC_MSG_CHECKING(version of $CC)
	GCC_VERSION="`${CC} --version 2>/dev/null | sed -e '2,$d' -e 's/^.*(GCC[[^)]]*) //' -e 's/^.*(Debian[[^)]]*) //' -e 's/^[[^0-9.]]*//' -e 's/[[^0-9.]].*//'`"
	test -z "$GCC_VERSION" && GCC_VERSION=unknown
	AC_MSG_RESULT($GCC_VERSION)
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_GCC_WARNINGS version: 31 updated: 2013/11/19 19:23:35
dnl ---------------
dnl Check if the compiler supports useful warning options.  There's a few that
dnl we don't use, simply because they're too noisy:
dnl
dnl	-Wconversion (useful in older versions of gcc, but not in gcc 2.7.x)
dnl	-Wredundant-decls (system headers make this too noisy)
dnl	-Wtraditional (combines too many unrelated messages, only a few useful)
dnl	-Wwrite-strings (too noisy, but should review occasionally).  This
dnl		is enabled for ncurses using "--enable-const".
dnl	-pedantic
dnl
dnl Parameter:
dnl	$1 is an optional list of gcc warning flags that a particular
dnl		application might want to use, e.g., "no-unused" for
dnl		-Wno-unused
dnl Special:
dnl	If $with_ext_const is "yes", add a check for -Wwrite-strings
dnl
AC_DEFUN([CF_GCC_WARNINGS],
[
AC_REQUIRE([CF_GCC_VERSION])
CF_INTEL_COMPILER(GCC,INTEL_COMPILER,CFLAGS)
CF_CLANG_COMPILER(GCC,CLANG_COMPILER,CFLAGS)

cat > conftest.$ac_ext <<EOF
#line __oline__ "${as_me:-configure}"
int main(int argc, char *argv[[]]) { return (argv[[argc-1]] == 0) ; }
EOF

if test "$INTEL_COMPILER" = yes
then
# The "-wdXXX" options suppress warnings:
# remark #1419: external declaration in primary source file
# remark #1683: explicit conversion of a 64-bit integral type to a smaller integral type (potential portability problem)
# remark #1684: conversion from pointer to same-sized integral type (potential portability problem)
# remark #193: zero used for undefined preprocessing identifier
# remark #593: variable "curs_sb_left_arrow" was set but never used
# remark #810: conversion from "int" to "Dimension={unsigned short}" may lose significant bits
# remark #869: parameter "tw" was never referenced
# remark #981: operands are evaluated in unspecified order
# warning #279: controlling expression is constant

	AC_CHECKING([for $CC warning options])
	cf_save_CFLAGS="$CFLAGS"
	EXTRA_CFLAGS="-Wall"
	for cf_opt in \
		wd1419 \
		wd1683 \
		wd1684 \
		wd193 \
		wd593 \
		wd279 \
		wd810 \
		wd869 \
		wd981
	do
		CFLAGS="$cf_save_CFLAGS $EXTRA_CFLAGS -$cf_opt"
		if AC_TRY_EVAL(ac_compile); then
			test -n "$verbose" && AC_MSG_RESULT(... -$cf_opt)
			EXTRA_CFLAGS="$EXTRA_CFLAGS -$cf_opt"
		fi
	done
	CFLAGS="$cf_save_CFLAGS"

elif test "$GCC" = yes
then
	AC_CHECKING([for $CC warning options])
	cf_save_CFLAGS="$CFLAGS"
	EXTRA_CFLAGS=
	cf_warn_CONST=""
	test "$with_ext_const" = yes && cf_warn_CONST="Wwrite-strings"
	cf_gcc_warnings="Wignored-qualifiers Wlogical-op Wvarargs"
	test "x$CLANG_COMPILER" = xyes && cf_gcc_warnings=
	for cf_opt in W Wall \
		Wbad-function-cast \
		Wcast-align \
		Wcast-qual \
		Wdeclaration-after-statement \
		Wextra \
		Winline \
		Wmissing-declarations \
		Wmissing-prototypes \
		Wnested-externs \
		Wpointer-arith \
		Wshadow \
		Wstrict-prototypes \
		Wundef $cf_gcc_warnings $cf_warn_CONST $1
	do
		CFLAGS="$cf_save_CFLAGS $EXTRA_CFLAGS -$cf_opt"
		if AC_TRY_EVAL(ac_compile); then
			test -n "$verbose" && AC_MSG_RESULT(... -$cf_opt)
			case $cf_opt in #(vi
			Wcast-qual) #(vi
				CPPFLAGS="$CPPFLAGS -DXTSTRINGDEFINES"
				;;
			Winline) #(vi
				case $GCC_VERSION in
				[[34]].*)
					CF_VERBOSE(feature is broken in gcc $GCC_VERSION)
					continue;;
				esac
				;;
			Wpointer-arith) #(vi
				case $GCC_VERSION in
				[[12]].*)
					CF_VERBOSE(feature is broken in gcc $GCC_VERSION)
					continue;;
				esac
				;;
			esac
			EXTRA_CFLAGS="$EXTRA_CFLAGS -$cf_opt"
		fi
	done
	CFLAGS="$cf_save_CFLAGS"
fi
rm -rf conftest*

AC_SUBST(EXTRA_CFLAGS)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_GETOPT_HEADER version: 5 updated: 2012/10/06 16:39:58
dnl ----------------
dnl Check for getopt's variables which are commonly defined in stdlib.h,
dnl unistd.h or (nonstandard) in getopt.h
AC_DEFUN([CF_GETOPT_HEADER],
[
AC_HAVE_HEADERS(unistd.h getopt.h)
AC_CACHE_CHECK(for header declaring getopt variables,cf_cv_getopt_header,[
cf_cv_getopt_header=none
for cf_header in stdio.h stdlib.h unistd.h getopt.h
do
AC_TRY_COMPILE([
#include <$cf_header>],
[int x = optind; char *y = optarg],
[cf_cv_getopt_header=$cf_header
 break])
done
])
if test $cf_cv_getopt_header != none ; then
	AC_DEFINE(HAVE_GETOPT_HEADER,1,[Define to 1 if we need to include getopt.h])
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_GNAT_GENERICS version: 2 updated: 2011/03/23 20:24:41
dnl ----------------
AC_DEFUN([CF_GNAT_GENERICS],
[
AC_REQUIRE([CF_GNAT_VERSION])

AC_MSG_CHECKING(if GNAT supports generics)
case $cf_gnat_version in #(vi
3.[[1-9]]*|[[4-9]].*) #(vi
	cf_gnat_generics=yes
	;;
*)
	cf_gnat_generics=no
	;;
esac
AC_MSG_RESULT($cf_gnat_generics)

if test "$cf_gnat_generics" = yes
then
	cf_compile_generics=generics
	cf_generic_objects="\${GENOBJS}"
else
	cf_compile_generics=
	cf_generic_objects=
fi

AC_SUBST(cf_compile_generics)
AC_SUBST(cf_generic_objects)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_GNAT_PRAGMA_UNREF version: 1 updated: 2010/06/19 15:22:18
dnl --------------------
dnl Check if the gnat pragma "Unreferenced" works.
AC_DEFUN([CF_GNAT_PRAGMA_UNREF],[
AC_CACHE_CHECK(if GNAT pragma Unreferenced works,cf_cv_pragma_unreferenced,[
CF_GNAT_TRY_LINK([procedure conftest;],
[with Text_IO;
with GNAT.OS_Lib;
procedure conftest is
   test : Integer;
   pragma Unreferenced (test);
begin
   test := 1;
   Text_IO.Put ("Hello World");
   Text_IO.New_Line;
   GNAT.OS_Lib.OS_Exit (0);
end conftest;],
	[cf_cv_pragma_unreferenced=yes],
	[cf_cv_pragma_unreferenced=no])])

# if the pragma is supported, use it (needed in the Trace code).
if test $cf_cv_pragma_unreferenced = yes ; then
	PRAGMA_UNREF=TRUE
else
	PRAGMA_UNREF=FALSE
fi
AC_SUBST(PRAGMA_UNREF)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_GNAT_PROJECTS version: 4 updated: 2013/09/07 14:05:46
dnl ----------------
dnl GNAT projects are configured with ".gpr" project files.
dnl GNAT libraries are a further development, using the project feature.
AC_DEFUN([CF_GNAT_PROJECTS],
[
AC_REQUIRE([CF_GNAT_VERSION])

cf_gnat_libraries=no
cf_gnat_projects=no

AC_MSG_CHECKING(if GNAT supports project files)
case $cf_gnat_version in #(vi
3.[[0-9]]*) #(vi
	;;
*)
	case $cf_cv_system_name in #(vi
	cygwin*|msys*) #(vi
		;;
	*)
		mkdir conftest.src conftest.bin conftest.lib
		cd conftest.src
		rm -rf conftest* *~conftest*
		cat >>library.gpr <<CF_EOF
project Library is
  Kind := External ("LIB_KIND");
  for Library_Name use "ConfTest";
  for Object_Dir use ".";
  for Library_ALI_Dir use External("LIBRARY_DIR");
  for Library_Version use External ("SONAME");
  for Library_Kind use Kind;
  for Library_Dir use External("BUILD_DIR");
  Source_Dir := External ("SOURCE_DIR");
  for Source_Dirs use (Source_Dir);
  package Compiler is
     for Default_Switches ("Ada") use
       ("-g",
        "-O2",
        "-gnatafno",
        "-gnatVa",   -- All validity checks
        "-gnatwa");  -- Activate all optional errors
  end Compiler;
end Library;
CF_EOF
		cat >>confpackage.ads <<CF_EOF
package ConfPackage is
   procedure conftest;
end ConfPackage;
CF_EOF
		cat >>confpackage.adb <<CF_EOF
with Text_IO;
package body ConfPackage is
   procedure conftest is
   begin
      Text_IO.Put ("Hello World");
      Text_IO.New_Line;
   end conftest;
end ConfPackage;
CF_EOF
		if ( $cf_ada_make $ADAFLAGS \
				-Plibrary.gpr \
				-XBUILD_DIR=`cd ../conftest.bin;pwd` \
				-XLIBRARY_DIR=`cd ../conftest.lib;pwd` \
				-XSOURCE_DIR=`pwd` \
				-XSONAME=libConfTest.so.1 \
				-XLIB_KIND=static 1>&AC_FD_CC 2>&1 ) ; then
			cf_gnat_projects=yes
		fi
		cd ..
		if test -f conftest.lib/confpackage.ali
		then
			cf_gnat_libraries=yes
		fi
		rm -rf conftest* *~conftest*
		;;
	esac
	;;
esac
AC_MSG_RESULT($cf_gnat_projects)

if test $cf_gnat_projects = yes
then
	AC_MSG_CHECKING(if GNAT supports libraries)
	AC_MSG_RESULT($cf_gnat_libraries)
fi

if test "$cf_gnat_projects" = yes
then
	USE_OLD_MAKERULES="#"
	USE_GNAT_PROJECTS=""
else
	USE_OLD_MAKERULES=""
	USE_GNAT_PROJECTS="#"
fi

if test "$cf_gnat_libraries" = yes
then
	USE_GNAT_LIBRARIES=""
else
	USE_GNAT_LIBRARIES="#"
fi

AC_SUBST(USE_OLD_MAKERULES)
AC_SUBST(USE_GNAT_PROJECTS)
AC_SUBST(USE_GNAT_LIBRARIES)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_GNAT_SIGINT version: 1 updated: 2011/03/27 20:07:59
dnl --------------
dnl Check if gnat supports SIGINT, and presumably tasking.  For the latter, it
dnl is noted that gnat may compile a tasking unit even for configurations which
dnl fail at runtime.
AC_DEFUN([CF_GNAT_SIGINT],[
AC_CACHE_CHECK(if GNAT supports SIGINT,cf_cv_gnat_sigint,[
CF_GNAT_TRY_LINK([with Ada.Interrupts.Names;

package ConfTest is

   pragma Warnings (Off);  --  the next pragma exists since 3.11p
   pragma Unreserve_All_Interrupts;
   pragma Warnings (On);

   protected Process is
      procedure Stop;
      function Continue return Boolean;
      pragma Attach_Handler (Stop, Ada.Interrupts.Names.SIGINT);
   private
      Done : Boolean := False;
   end Process;

end ConfTest;],
[package body ConfTest is
   protected body Process is
      procedure Stop is
      begin
         Done := True;
      end Stop;
      function Continue return Boolean is
      begin
         return not Done;
      end Continue;
   end Process;
end ConfTest;],
	[cf_cv_gnat_sigint=yes],
	[cf_cv_gnat_sigint=no])])

if test $cf_cv_gnat_sigint = yes ; then
	USE_GNAT_SIGINT=""
else
	USE_GNAT_SIGINT="#"
fi
AC_SUBST(USE_GNAT_SIGINT)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_GNAT_TRY_LINK version: 3 updated: 2011/03/19 14:47:45
dnl ----------------
dnl Verify that a test program compiles/links with GNAT.
dnl $cf_ada_make is set to the program that compiles/links
dnl $ADAFLAGS may be set to the GNAT flags.
dnl
dnl $1 is the text of the spec
dnl $2 is the text of the body
dnl $3 is the shell command to execute if successful
dnl $4 is the shell command to execute if not successful
AC_DEFUN([CF_GNAT_TRY_LINK],
[
rm -rf conftest* *~conftest*
cat >>conftest.ads <<CF_EOF
$1
CF_EOF
cat >>conftest.adb <<CF_EOF
$2
CF_EOF
if ( $cf_ada_make $ADAFLAGS conftest 1>&AC_FD_CC 2>&1 ) ; then
ifelse($3,,      :,[      $3])
ifelse($4,,,[else
   $4])
fi
rm -rf conftest* *~conftest*
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_GNAT_TRY_RUN version: 5 updated: 2011/03/19 14:47:45
dnl ---------------
dnl Verify that a test program compiles and runs with GNAT
dnl $cf_ada_make is set to the program that compiles/links
dnl $ADAFLAGS may be set to the GNAT flags.
dnl
dnl $1 is the text of the spec
dnl $2 is the text of the body
dnl $3 is the shell command to execute if successful
dnl $4 is the shell command to execute if not successful
AC_DEFUN([CF_GNAT_TRY_RUN],
[
rm -rf conftest* *~conftest*
cat >>conftest.ads <<CF_EOF
$1
CF_EOF
cat >>conftest.adb <<CF_EOF
$2
CF_EOF
if ( $cf_ada_make $ADAFLAGS conftest 1>&AC_FD_CC 2>&1 ) ; then
   if ( ./conftest 1>&AC_FD_CC 2>&1 ) ; then
ifelse($3,,      :,[      $3])
ifelse($4,,,[   else
      $4])
   fi
ifelse($4,,,[else
   $4])
fi
rm -rf conftest* *~conftest*
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_GNAT_VERSION version: 18 updated: 2012/01/21 19:28:10
dnl ---------------
dnl Verify version of GNAT.
AC_DEFUN([CF_GNAT_VERSION],
[
AC_MSG_CHECKING(for gnat version)
cf_gnat_version=`${cf_ada_make:-gnatmake} -v 2>&1 | \
	grep '[[0-9]].[[0-9]][[0-9]]*' |\
    sed -e '2,$d' -e 's/[[^0-9 \.]]//g' -e 's/^[[ ]]*//' -e 's/ .*//'`
AC_MSG_RESULT($cf_gnat_version)

case $cf_gnat_version in #(vi
3.1[[1-9]]*|3.[[2-9]]*|[[4-9]].*|20[[0-9]][[0-9]]) #(vi
	cf_cv_prog_gnat_correct=yes
	;;
*)
	AC_MSG_WARN(Unsupported GNAT version $cf_gnat_version. We require 3.11 or better. Disabling Ada95 binding.)
	cf_cv_prog_gnat_correct=no
	;;
esac
])
dnl ---------------------------------------------------------------------------
dnl CF_GNU_SOURCE version: 6 updated: 2005/07/09 13:23:07
dnl -------------
dnl Check if we must define _GNU_SOURCE to get a reasonable value for
dnl _XOPEN_SOURCE, upon which many POSIX definitions depend.  This is a defect
dnl (or misfeature) of glibc2, which breaks portability of many applications,
dnl since it is interwoven with GNU extensions.
dnl
dnl Well, yes we could work around it...
AC_DEFUN([CF_GNU_SOURCE],
[
AC_CACHE_CHECK(if we must define _GNU_SOURCE,cf_cv_gnu_source,[
AC_TRY_COMPILE([#include <sys/types.h>],[
#ifndef _XOPEN_SOURCE
make an error
#endif],
	[cf_cv_gnu_source=no],
	[cf_save="$CPPFLAGS"
	 CPPFLAGS="$CPPFLAGS -D_GNU_SOURCE"
	 AC_TRY_COMPILE([#include <sys/types.h>],[
#ifdef _XOPEN_SOURCE
make an error
#endif],
	[cf_cv_gnu_source=no],
	[cf_cv_gnu_source=yes])
	CPPFLAGS="$cf_save"
	])
])
test "$cf_cv_gnu_source" = yes && CPPFLAGS="$CPPFLAGS -D_GNU_SOURCE"
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_GPP_LIBRARY version: 11 updated: 2012/10/06 17:56:13
dnl --------------
dnl If we're trying to use g++, test if libg++ is installed (a rather common
dnl problem :-).  If we have the compiler but no library, we'll be able to
dnl configure, but won't be able to build the c++ demo program.
AC_DEFUN([CF_GPP_LIBRARY],
[
cf_cxx_library=unknown
case $cf_cv_system_name in #(vi
os2*) #(vi
	cf_gpp_libname=gpp
	;;
*)
	cf_gpp_libname=g++
	;;
esac
if test "$GXX" = yes; then
	AC_MSG_CHECKING([for lib$cf_gpp_libname])
	cf_save="$LIBS"
	CF_ADD_LIB($cf_gpp_libname)
	AC_TRY_LINK([
#include <$cf_gpp_libname/builtin.h>
	],
	[two_arg_error_handler_t foo2 = lib_error_handler],
	[cf_cxx_library=yes
	 CF_ADD_LIB($cf_gpp_libname,CXXLIBS)
	 if test "$cf_gpp_libname" = cpp ; then
	    AC_DEFINE(HAVE_GPP_BUILTIN_H,1,[Define to 1 if we have gpp builtin.h])
	 else
	    AC_DEFINE(HAVE_GXX_BUILTIN_H,1,[Define to 1 if we have g++ builtin.h])
	 fi],
	[AC_TRY_LINK([
#include <builtin.h>
	],
	[two_arg_error_handler_t foo2 = lib_error_handler],
	[cf_cxx_library=yes
	 CF_ADD_LIB($cf_gpp_libname,CXXLIBS)
	 AC_DEFINE(HAVE_BUILTIN_H,1,[Define to 1 if we have builtin.h])],
	[cf_cxx_library=no])])
	LIBS="$cf_save"
	AC_MSG_RESULT($cf_cxx_library)
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_GXX_VERSION version: 7 updated: 2012/06/16 14:55:39
dnl --------------
dnl Check for version of g++
AC_DEFUN([CF_GXX_VERSION],[
AC_REQUIRE([AC_PROG_CPP])
GXX_VERSION=none
if test "$GXX" = yes; then
	AC_MSG_CHECKING(version of ${CXX:-g++})
	GXX_VERSION="`${CXX:-g++} --version| sed -e '2,$d' -e 's/^.*(GCC) //' -e 's/^[[^0-9.]]*//' -e 's/[[^0-9.]].*//'`"
	test -z "$GXX_VERSION" && GXX_VERSION=unknown
	AC_MSG_RESULT($GXX_VERSION)
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_GXX_WARNINGS version: 8 updated: 2013/11/16 14:27:53
dnl ---------------
dnl Check if the compiler supports useful warning options.
dnl
dnl Most of gcc's options apply to g++, except:
dnl	-Wbad-function-cast
dnl	-Wmissing-declarations
dnl	-Wnested-externs
dnl
dnl Omit a few (for now):
dnl	-Winline
dnl
dnl Parameter:
dnl	$1 is an optional list of g++ warning flags that a particular
dnl		application might want to use, e.g., "no-unused" for
dnl		-Wno-unused
dnl Special:
dnl	If $with_ext_const is "yes", add a check for -Wwrite-strings
dnl
AC_DEFUN([CF_GXX_WARNINGS],
[

CF_INTEL_COMPILER(GXX,INTEL_CPLUSPLUS,CXXFLAGS)
CF_CLANG_COMPILER(GXX,CLANG_CPLUSPLUS,CXXFLAGS)

AC_REQUIRE([CF_GXX_VERSION])

AC_LANG_SAVE
AC_LANG_CPLUSPLUS

cat > conftest.$ac_ext <<EOF
#line __oline__ "configure"
int main(int argc, char *argv[[]]) { return (argv[[argc-1]] == 0) ; }
EOF

if test "$INTEL_CPLUSPLUS" = yes
then
# The "-wdXXX" options suppress warnings:
# remark #1419: external declaration in primary source file
# remark #1682: implicit conversion of a 64-bit integral type to a smaller integral type (potential portability problem)
# remark #1683: explicit conversion of a 64-bit integral type to a smaller integral type (potential portability problem)
# remark #1684: conversion from pointer to same-sized integral type (potential portability problem)
# remark #193: zero used for undefined preprocessing identifier
# remark #593: variable "curs_sb_left_arrow" was set but never used
# remark #810: conversion from "int" to "Dimension={unsigned short}" may lose significant bits
# remark #869: parameter "tw" was never referenced
# remark #981: operands are evaluated in unspecified order
# warning #269: invalid format string conversion

	AC_CHECKING([for $CC warning options])
	cf_save_CXXFLAGS="$CXXFLAGS"
	EXTRA_CXXFLAGS="-Wall"
	for cf_opt in \
		wd1419 \
		wd1682 \
		wd1683 \
		wd1684 \
		wd193 \
		wd279 \
		wd593 \
		wd810 \
		wd869 \
		wd981
	do
		CXXFLAGS="$cf_save_CXXFLAGS $EXTRA_CXXFLAGS -$cf_opt"
		if AC_TRY_EVAL(ac_compile); then
			test -n "$verbose" && AC_MSG_RESULT(... -$cf_opt)
			EXTRA_CXXFLAGS="$EXTRA_CXXFLAGS -$cf_opt"
		fi
	done
	CXXFLAGS="$cf_save_CXXFLAGS"

elif test "$GXX" = yes
then
	AC_CHECKING([for $CXX warning options])
	cf_save_CXXFLAGS="$CXXFLAGS"
	EXTRA_CXXFLAGS="-W -Wall"
	cf_gxx_extra_warnings=""
	test "$with_ext_const" = yes && cf_gxx_extra_warnings="Wwrite-strings"
	case "$GCC_VERSION" in
	[[1-2]].*)
		;;
	*)
		cf_gxx_extra_warnings="$cf_gxx_extra_warnings Weffc++"
		;;
	esac
	for cf_opt in \
		Wabi \
		fabi-version=0 \
		Wextra \
		Wignored-qualifiers \
		Wlogical-op \
		Woverloaded-virtual \
		Wsign-promo \
		Wsynth \
		Wold-style-cast \
		Wcast-align \
		Wcast-qual \
		Wpointer-arith \
		Wshadow \
		Wundef $cf_gxx_extra_warnings $1
	do
		CXXFLAGS="$cf_save_CXXFLAGS $EXTRA_CXXFLAGS -Werror -$cf_opt"
		if AC_TRY_EVAL(ac_compile); then
			test -n "$verbose" && AC_MSG_RESULT(... -$cf_opt)
			EXTRA_CXXFLAGS="$EXTRA_CXXFLAGS -$cf_opt"
		else
			test -n "$verbose" && AC_MSG_RESULT(... no -$cf_opt)
		fi
	done
	CXXFLAGS="$cf_save_CXXFLAGS"
fi

rm -rf conftest*
AC_LANG_RESTORE
AC_SUBST(EXTRA_CXXFLAGS)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_HASHED_DB version: 4 updated: 2010/05/29 16:31:02
dnl ------------
dnl Look for an instance of the Berkeley hashed database.
dnl
dnl $1 = optional parameter, to specify install-prefix for the database.
AC_DEFUN([CF_HASHED_DB],
[
ifelse([$1],,,[
case $1 in #(vi
yes|*able*) #(vi
    ;;
*)
    if test -d "$1" ; then
        CF_ADD_INCDIR($1/include)
        CF_ADD_LIBDIR($1/lib)
    fi
esac
])
AC_CHECK_HEADER(db.h,[
CF_HASHED_DB_VERSION
if test "$cf_cv_hashed_db_version" = unknown ; then
	AC_MSG_ERROR(Cannot determine version of db)
else
	CF_HASHED_DB_LIBS
	if test "$cf_cv_hashed_db_libs" = unknown ; then
		AC_MSG_ERROR(Cannot determine library for db)
	elif test "$cf_cv_hashed_db_libs" != default ; then
		CF_ADD_LIB($cf_cv_hashed_db_libs)
	fi
fi
],[
	AC_MSG_ERROR(Cannot find db.h)
])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_HASHED_DB_LIBS version: 9 updated: 2010/05/29 16:31:02
dnl -----------------
dnl Given that we have the header and version for hashed database, find the
dnl library information.
AC_DEFUN([CF_HASHED_DB_LIBS],
[
AC_CACHE_CHECK(for db libraries, cf_cv_hashed_db_libs,[
cf_cv_hashed_db_libs=unknown
for cf_db_libs in "" db$cf_cv_hashed_db_version db-$cf_cv_hashed_db_version db ''
do
	cf_save_libs="$LIBS"
	if test -n "$cf_db_libs"; then
		CF_ADD_LIB($cf_db_libs)
	fi
	CF_MSG_LOG(checking for library "$cf_db_libs")
	AC_TRY_LINK([
$ac_includes_default
#include <db.h>
],[
	char *path = "/tmp/foo";
#ifdef DB_VERSION_MAJOR
#if DB_VERSION_MAJOR >= 4
	DB *result = 0;
	db_create(&result, NULL, 0);
	result->open(result,
		NULL,
		path,
		path,
		DB_HASH,
		DB_CREATE,
		0644);
#elif DB_VERSION_MAJOR >= 3
	DB *result = 0;
	db_create(&result, NULL, 0);
	result->open(result,
		path,
		path,
		DB_HASH,
		DB_CREATE,
		0644);
#elif DB_VERSION_MAJOR >= 2
	DB *result = 0;
	db_open(path,
		DB_HASH,
		DB_CREATE,
		0644,
		(DB_ENV *) 0,
		(DB_INFO *) 0,
		&result);
#endif /* DB_VERSION_MAJOR */
#else
	DB *result = dbopen(path,
		     2,
		     0644,
		     DB_HASH,
		     0);
#endif
	${cf_cv_main_return:-return}(result != 0)
],[
	if test -n "$cf_db_libs" ; then
		cf_cv_hashed_db_libs=$cf_db_libs
	else
		cf_cv_hashed_db_libs=default
	fi
	LIBS="$cf_save_libs"
	break
])
	LIBS="$cf_save_libs"
done
])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_HASHED_DB_VERSION version: 3 updated: 2007/12/01 15:01:37
dnl --------------------
dnl Given that we have the header file for hashed database, find the version
dnl information.
AC_DEFUN([CF_HASHED_DB_VERSION],
[
AC_CACHE_CHECK(for version of db, cf_cv_hashed_db_version,[
cf_cv_hashed_db_version=unknown

for cf_db_version in 1 2 3 4 5
do
	CF_MSG_LOG(checking for db version $cf_db_version)
	AC_TRY_COMPILE([
$ac_includes_default
#include <db.h>

#ifdef DB_VERSION_MAJOR
	/* db2 (DB_VERSION_MAJOR=2) has also DB_VERSION_MINOR, tested with 7 */
#if $cf_db_version == DB_VERSION_MAJOR
	/* ok */
#else
	make an error
#endif
#else
#if $cf_db_version == 1
	/* ok: assuming this is DB 1.8.5 */
#else
	make an error
#endif
#endif
],[DBT *foo = 0],[
	cf_cv_hashed_db_version=$cf_db_version
	break
	])
done
])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_HEADER_PATH version: 12 updated: 2010/05/05 05:22:40
dnl --------------
dnl Construct a search-list of directories for a nonstandard header-file
dnl
dnl Parameters
dnl	$1 = the variable to return as result
dnl	$2 = the package name
AC_DEFUN([CF_HEADER_PATH],
[
$1=

# collect the current set of include-directories from compiler flags
cf_header_path_list=""
if test -n "${CFLAGS}${CPPFLAGS}" ; then
	for cf_header_path in $CPPFLAGS $CFLAGS
	do
		case $cf_header_path in #(vi
		-I*)
			cf_header_path=`echo ".$cf_header_path" |sed -e 's/^...//' -e 's,/include$,,'`
			CF_ADD_SUBDIR_PATH($1,$2,include,$cf_header_path,NONE)
			cf_header_path_list="$cf_header_path_list [$]$1"
			;;
		esac
	done
fi

# add the variations for the package we are looking for
CF_SUBDIR_PATH($1,$2,include)

test "$includedir" != NONE && \
test "$includedir" != "/usr/include" && \
test -d "$includedir" && {
	test -d $includedir &&    $1="[$]$1 $includedir"
	test -d $includedir/$2 && $1="[$]$1 $includedir/$2"
}

test "$oldincludedir" != NONE && \
test "$oldincludedir" != "/usr/include" && \
test -d "$oldincludedir" && {
	test -d $oldincludedir    && $1="[$]$1 $oldincludedir"
	test -d $oldincludedir/$2 && $1="[$]$1 $oldincludedir/$2"
}

$1="[$]$1 $cf_header_path_list"
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_HELP_MESSAGE version: 3 updated: 1998/01/14 10:56:23
dnl ---------------
dnl Insert text into the help-message, for readability, from AC_ARG_WITH.
AC_DEFUN([CF_HELP_MESSAGE],
[AC_DIVERT_HELP([$1])dnl
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_INCLUDE_DIRS version: 8 updated: 2013/10/12 16:45:09
dnl ---------------
dnl Construct the list of include-options according to whether we're building
dnl in the source directory or using '--srcdir=DIR' option.  If we're building
dnl with gcc, don't append the includedir if it happens to be /usr/include,
dnl since that usually breaks gcc's shadow-includes.
AC_DEFUN([CF_INCLUDE_DIRS],
[
if test "$GCC" != yes; then
	CPPFLAGS="-I\${includedir} $CPPFLAGS"
elif test "$includedir" != "/usr/include"; then
	if test "$includedir" = '${prefix}/include' ; then
		if test x$prefix != x/usr ; then
			CPPFLAGS="-I\${includedir} $CPPFLAGS"
		fi
	else
		CPPFLAGS="-I\${includedir} $CPPFLAGS"
	fi
fi
if test "$srcdir" != "."; then
	CPPFLAGS="-I\${srcdir}/../include $CPPFLAGS"
fi
CPPFLAGS="-I. -I../include $CPPFLAGS"
AC_SUBST(CPPFLAGS)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_INTEL_COMPILER version: 5 updated: 2013/02/10 10:41:05
dnl -----------------
dnl Check if the given compiler is really the Intel compiler for Linux.  It
dnl tries to imitate gcc, but does not return an error when it finds a mismatch
dnl between prototypes, e.g., as exercised by CF_MISSING_CHECK.
dnl
dnl This macro should be run "soon" after AC_PROG_CC or AC_PROG_CPLUSPLUS, to
dnl ensure that it is not mistaken for gcc/g++.  It is normally invoked from
dnl the wrappers for gcc and g++ warnings.
dnl
dnl $1 = GCC (default) or GXX
dnl $2 = INTEL_COMPILER (default) or INTEL_CPLUSPLUS
dnl $3 = CFLAGS (default) or CXXFLAGS
AC_DEFUN([CF_INTEL_COMPILER],[
AC_REQUIRE([AC_CANONICAL_HOST])
ifelse([$2],,INTEL_COMPILER,[$2])=no

if test "$ifelse([$1],,[$1],GCC)" = yes ; then
	case $host_os in
	linux*|gnu*)
		AC_MSG_CHECKING(if this is really Intel ifelse([$1],GXX,C++,C) compiler)
		cf_save_CFLAGS="$ifelse([$3],,CFLAGS,[$3])"
		ifelse([$3],,CFLAGS,[$3])="$ifelse([$3],,CFLAGS,[$3]) -no-gcc"
		AC_TRY_COMPILE([],[
#ifdef __INTEL_COMPILER
#else
make an error
#endif
],[ifelse([$2],,INTEL_COMPILER,[$2])=yes
cf_save_CFLAGS="$cf_save_CFLAGS -we147 -no-gcc"
],[])
		ifelse([$3],,CFLAGS,[$3])="$cf_save_CFLAGS"
		AC_MSG_RESULT($ifelse([$2],,INTEL_COMPILER,[$2]))
		;;
	esac
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_ISASCII version: 4 updated: 2012/10/06 17:56:13
dnl ----------
dnl Check if we have either a function or macro for 'isascii()'.
AC_DEFUN([CF_ISASCII],
[
AC_MSG_CHECKING(for isascii)
AC_CACHE_VAL(cf_cv_have_isascii,[
	AC_TRY_LINK([#include <ctype.h>],[int x = isascii(' ')],
	[cf_cv_have_isascii=yes],
	[cf_cv_have_isascii=no])
])dnl
AC_MSG_RESULT($cf_cv_have_isascii)
test "$cf_cv_have_isascii" = yes && AC_DEFINE(HAVE_ISASCII,1,[Define to 1 if we have isascii()])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_LARGEFILE version: 8 updated: 2012/10/06 08:57:51
dnl ------------
dnl Add checks for large file support.
AC_DEFUN([CF_LARGEFILE],[
ifdef([AC_FUNC_FSEEKO],[
    AC_SYS_LARGEFILE
    if test "$enable_largefile" != no ; then
	AC_FUNC_FSEEKO

	# Normally we would collect these definitions in the config.h,
	# but (like _XOPEN_SOURCE), some environments rely on having these
	# defined before any of the system headers are included.  Another
	# case comes up with C++, e.g., on AIX the compiler compiles the
	# header files by themselves before looking at the body files it is
	# told to compile.  For ncurses, those header files do not include
	# the config.h
	test "$ac_cv_sys_large_files"      != no && CPPFLAGS="$CPPFLAGS -D_LARGE_FILES "
	test "$ac_cv_sys_largefile_source" != no && CPPFLAGS="$CPPFLAGS -D_LARGEFILE_SOURCE "
	test "$ac_cv_sys_file_offset_bits" != no && CPPFLAGS="$CPPFLAGS -D_FILE_OFFSET_BITS=$ac_cv_sys_file_offset_bits "

	AC_CACHE_CHECK(whether to use struct dirent64, cf_cv_struct_dirent64,[
		AC_TRY_COMPILE([
#include <sys/types.h>
#include <dirent.h>
		],[
		/* if transitional largefile support is setup, this is true */
		extern struct dirent64 * readdir(DIR *);
		struct dirent64 *x = readdir((DIR *)0);
		struct dirent *y = readdir((DIR *)0);
		int z = x - y;
		],
		[cf_cv_struct_dirent64=yes],
		[cf_cv_struct_dirent64=no])
	])
	test "$cf_cv_struct_dirent64" = yes && AC_DEFINE(HAVE_STRUCT_DIRENT64,1,[Define to 1 if we have struct dirent64])
    fi
])
])
dnl ---------------------------------------------------------------------------
dnl CF_LDFLAGS_STATIC version: 10 updated: 2011/09/24 12:51:48
dnl -----------------
dnl Check for compiler/linker flags used to temporarily force usage of static
dnl libraries.  This depends on the compiler and platform.  Use this to help
dnl ensure that the linker picks up a given library based on its position in
dnl the list of linker options and libraries.
AC_DEFUN([CF_LDFLAGS_STATIC],[

if test "$GCC" = yes ; then
	case $cf_cv_system_name in #(
	OS/2*|os2*|aix[[4]]*|solaris2.1[[0-9]]|darwin*) 	#( vi
		LDFLAGS_STATIC=
		LDFLAGS_SHARED=
		;;
    *) 	#( normally, except when broken
        LDFLAGS_STATIC=-static
        LDFLAGS_SHARED=-dynamic
        ;;
    esac
else
	case $cf_cv_system_name in #(
	aix[[4-7]]*) 	#( from ld manpage
		LDFLAGS_STATIC=-bstatic
		LDFLAGS_SHARED=-bdynamic
		;;
	hpux*)		#( from ld manpage for hpux10.20, hpux11.11
		# We could also use just "archive" and "shared".
		LDFLAGS_STATIC=-Wl,-a,archive_shared
		LDFLAGS_SHARED=-Wl,-a,shared_archive
		;;
	irix*)		#( from ld manpage IRIX64
		LDFLAGS_STATIC=-Bstatic
		LDFLAGS_SHARED=-Bdynamic
		;;
	osf[[45]]*)	#( from ld manpage osf4.0d, osf5.1
		# alternative "-oldstyle_liblookup" (not in cc manpage)
		LDFLAGS_STATIC=-noso
		LDFLAGS_SHARED=-so_archive
		;;
	solaris2*)
		LDFLAGS_STATIC=-Bstatic
		LDFLAGS_SHARED=-Bdynamic
		;;
	esac
fi

if test -n "$LDFLAGS_STATIC" && test -n "$LDFLAGS_SHARED"
then
	AC_MSG_CHECKING(if linker supports switching between static/dynamic)

	rm -f libconftest.a
	cat >conftest.$ac_ext <<EOF
#line __oline__ "configure"
#include <stdio.h>
int cf_ldflags_static(FILE *fp) { return fflush(fp); }
EOF
	if AC_TRY_EVAL(ac_compile) ; then
		( $AR $ARFLAGS libconftest.a conftest.o ) 2>&AC_FD_CC 1>/dev/null
		( eval $RANLIB libconftest.a ) 2>&AC_FD_CC >/dev/null
	fi
	rm -f conftest.*

	cf_save_LIBS="$LIBS"

	LIBS="$LDFLAGS_STATIC -L`pwd` -lconftest $LDFLAGS_DYNAMIC $LIBS"
	AC_TRY_LINK([
#line __oline__ "configure"
#include <stdio.h>
int cf_ldflags_static(FILE *fp);
],[
	return cf_ldflags_static(stdin);
],[
	# some linkers simply ignore the -dynamic
	case x`file conftest$ac_exeext 2>/dev/null` in #(vi
	*static*) # (vi
		cf_ldflags_static=no
		;;
	*)
		cf_ldflags_static=yes
		;;
	esac
],[cf_ldflags_static=no])

	rm -f libconftest.*
	LIBS="$cf_save_LIBS"

	AC_MSG_RESULT($cf_ldflags_static)

	if test $cf_ldflags_static != yes
	then
		LDFLAGS_STATIC=
		LDFLAGS_SHARED=
	fi
else
	LDFLAGS_STATIC=
	LDFLAGS_SHARED=
fi

AC_SUBST(LDFLAGS_STATIC)
AC_SUBST(LDFLAGS_SHARED)
])
dnl ---------------------------------------------------------------------------
dnl CF_LD_RPATH_OPT version: 5 updated: 2011/07/17 14:48:41
dnl ---------------
dnl For the given system and compiler, find the compiler flags to pass to the
dnl loader to use the "rpath" feature.
AC_DEFUN([CF_LD_RPATH_OPT],
[
AC_REQUIRE([CF_CHECK_CACHE])

LD_RPATH_OPT=
AC_MSG_CHECKING(for an rpath option)
case $cf_cv_system_name in #(vi
irix*) #(vi
	if test "$GCC" = yes; then
		LD_RPATH_OPT="-Wl,-rpath,"
	else
		LD_RPATH_OPT="-rpath "
	fi
	;;
linux*|gnu*|k*bsd*-gnu) #(vi
	LD_RPATH_OPT="-Wl,-rpath,"
	;;
openbsd[[2-9]].*|mirbsd*) #(vi
	LD_RPATH_OPT="-Wl,-rpath,"
	;;
dragonfly*|freebsd*) #(vi
	LD_RPATH_OPT="-rpath "
	;;
netbsd*) #(vi
	LD_RPATH_OPT="-Wl,-rpath,"
	;;
osf*|mls+*) #(vi
	LD_RPATH_OPT="-rpath "
	;;
solaris2*) #(vi
	LD_RPATH_OPT="-R"
	;;
*)
	;;
esac
AC_MSG_RESULT($LD_RPATH_OPT)

case "x$LD_RPATH_OPT" in #(vi
x-R*)
	AC_MSG_CHECKING(if we need a space after rpath option)
	cf_save_LIBS="$LIBS"
	CF_ADD_LIBS(${LD_RPATH_OPT}$libdir)
	AC_TRY_LINK(, , cf_rpath_space=no, cf_rpath_space=yes)
	LIBS="$cf_save_LIBS"
	AC_MSG_RESULT($cf_rpath_space)
	test "$cf_rpath_space" = yes && LD_RPATH_OPT="$LD_RPATH_OPT "
	;;
esac
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_LIBRARY_PATH version: 9 updated: 2010/03/28 12:52:50
dnl ---------------
dnl Construct a search-list of directories for a nonstandard library-file
dnl
dnl Parameters
dnl	$1 = the variable to return as result
dnl	$2 = the package name
AC_DEFUN([CF_LIBRARY_PATH],
[
$1=
cf_library_path_list=""
if test -n "${LDFLAGS}${LIBS}" ; then
	for cf_library_path in $LDFLAGS $LIBS
	do
		case $cf_library_path in #(vi
		-L*)
			cf_library_path=`echo ".$cf_library_path" |sed -e 's/^...//' -e 's,/lib$,,'`
			CF_ADD_SUBDIR_PATH($1,$2,lib,$cf_library_path,NONE)
			cf_library_path_list="$cf_library_path_list [$]$1"
			;;
		esac
	done
fi

CF_SUBDIR_PATH($1,$2,lib)

$1="$cf_library_path_list [$]$1"
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_LIBTOOL_VERSION version: 1 updated: 2013/04/06 18:03:09
dnl ------------------
AC_DEFUN([CF_LIBTOOL_VERSION],[
if test -n "$LIBTOOL" && test "$LIBTOOL" != none
then
	cf_cv_libtool_version=`$LIBTOOL --version 2>&1 | sed -e '/^$/d' |sed -e '2,$d' -e 's/([[^)]]*)//g' -e 's/^[[^1-9]]*//' -e 's/[[^0-9.]].*//'`
else
	cf_cv_libtool_version=
fi
test -z "$cf_cv_libtool_version" && unset cf_cv_libtool_version
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_LIB_PREFIX version: 9 updated: 2012/01/21 19:28:10
dnl -------------
dnl Compute the library-prefix for the given host system
dnl $1 = variable to set
define([CF_LIB_PREFIX],
[
	case $cf_cv_system_name in #(vi
	OS/2*|os2*) #(vi
        LIB_PREFIX=''
        ;;
	*)	LIB_PREFIX='lib'
        ;;
	esac
ifelse($1,,,[$1=$LIB_PREFIX])
	AC_SUBST(LIB_PREFIX)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_LIB_RULES version: 74 updated: 2013/09/07 13:54:05
dnl ------------
dnl Append definitions and rules for the given models to the subdirectory
dnl Makefiles, and the recursion rule for the top-level Makefile.  If the
dnl subdirectory is a library-source directory, modify the Libs_To_Make list in
dnl the corresponding makefile to list the models that we'll generate.
dnl
dnl For shared libraries, make a list of symbolic links to construct when
dnl generating each library.  The convention used for Linux is the simplest
dnl one:
dnl	lib<name>.so	->
dnl	lib<name>.so.<major>	->
dnl	lib<name>.so.<maj>.<minor>
dnl
dnl Note: Libs_To_Make is mixed case, since it is not a pure autoconf variable.
AC_DEFUN([CF_LIB_RULES],
[
cf_prefix=$LIB_PREFIX
AC_REQUIRE([CF_SUBST_NCURSES_VERSION])

case $cf_cv_shlib_version in #(vi
cygdll|msysdll|mingw)
	TINFO_NAME=$TINFO_ARG_SUFFIX
	TINFO_SUFFIX=.dll
	;;
esac

if test -n "$TINFO_SUFFIX" ; then
	case $TINFO_SUFFIX in
	tw*)
		TINFO_NAME="${TINFO_NAME}tw"
		TINFO_SUFFIX=`echo $TINFO_SUFFIX | sed 's/^tw//'`
		;;
	t*)
		TINFO_NAME="${TINFO_NAME}t"
		TINFO_SUFFIX=`echo $TINFO_SUFFIX | sed 's/^t//'`
		;;
	w*)
		TINFO_NAME="${TINFO_NAME}w"
		TINFO_SUFFIX=`echo $TINFO_SUFFIX | sed 's/^w//'`
		;;
	esac
fi

for cf_dir in $SRC_SUBDIRS
do
	if test ! -d $srcdir/$cf_dir ; then
		continue
	elif test -f $srcdir/$cf_dir/modules; then

		SHARED_LIB=
		Libs_To_Make=
		for cf_item in $cf_LIST_MODELS
		do
			CF_LIB_SUFFIX($cf_item,cf_suffix,cf_depsuf)
			cf_libname=$cf_dir
			test "$cf_dir" = c++ && cf_libname=ncurses++
			if test $cf_item = shared ; then
				if test -n "${LIB_SUFFIX}"
				then
					cf_shared_suffix=`echo "$cf_suffix" | sed 's/^'"${LIB_SUFFIX}"'//'`
				else
					cf_shared_suffix="$cf_suffix"
				fi
				if test "$cf_cv_do_symlinks" = yes ; then
					cf_version_name=

					case "$cf_cv_shlib_version" in #(vi
					rel) #(vi
						cf_version_name=REL_VERSION
						;;
					abi)
						cf_version_name=ABI_VERSION
						;;
					esac

					if test -n "$cf_version_name"
					then
						case "$cf_cv_system_name" in #(vi
						darwin*)
							# "w", etc?
							cf_suffix="${LIB_SUFFIX}"'.${'$cf_version_name'}'"$cf_shared_suffix"
							;; #(vi
						*)
							cf_suffix="$cf_suffix"'.${'$cf_version_name'}'
							;;
						esac
					fi
					if test -n "${LIB_SUFFIX}"
					then
						cf_shared_suffix=`echo "$cf_suffix" | sed 's/^'"${LIB_SUFFIX}"'//'`
					else
						cf_shared_suffix="$cf_suffix"
					fi
				fi
				# cygwin needs import library, and has unique naming convention
				# use autodetected ${cf_prefix} for import lib and static lib, but
				# use 'cyg' prefix for shared lib.
				case $cf_cv_shlib_version in #(vi
				cygdll) #(vi
					cf_cygsuf=`echo "$cf_suffix" | sed -e 's/\.dll/\${ABI_VERSION}.dll/'`
					Libs_To_Make="$Libs_To_Make ../lib/cyg${cf_libname}${cf_cygsuf}"
					continue
					;;
				msysdll) #(vi
					cf_cygsuf=`echo "$cf_suffix" | sed -e 's/\.dll/\${ABI_VERSION}.dll/'`
					Libs_To_Make="$Libs_To_Make ../lib/msys-${cf_libname}${cf_cygsuf}"
					continue
					;;
				mingw)
					cf_cygsuf=`echo "$cf_suffix" | sed -e 's/\.dll/\${ABI_VERSION}.dll/'`
					Libs_To_Make="$Libs_To_Make ../lib/lib${cf_libname}${cf_cygsuf}"
					continue
					;;
				esac
			fi
			Libs_To_Make="$Libs_To_Make ../lib/${cf_prefix}${cf_libname}${cf_suffix}"
		done

		if test $cf_dir = ncurses ; then
			cf_subsets="$LIB_SUBSETS"
			cf_r_parts="$cf_subsets"
			cf_liblist="$Libs_To_Make"

			while test -n "$cf_r_parts"
			do
				cf_l_parts=`echo "$cf_r_parts" |sed -e 's/ .*$//'`
				cf_r_parts=`echo "$cf_r_parts" |sed -e 's/^[[^ ]]* //'`
				if test "$cf_l_parts" != "$cf_r_parts" ; then
					cf_item=
					case $cf_l_parts in #(vi
					*termlib*) #(vi
						cf_item=`echo $cf_liblist |sed -e s%${LIB_NAME}${LIB_SUFFIX}%${TINFO_LIB_SUFFIX}%g`
						;;
					*ticlib*)
						cf_item=`echo $cf_liblist |sed -e s%${LIB_NAME}${LIB_SUFFIX}%${TICS_LIB_SUFFIX}%g`
						;;
					*)
						break
						;;
					esac
					if test -n "$cf_item"; then
						Libs_To_Make="$cf_item $Libs_To_Make"
					fi
				else
					break
				fi
			done
		else
			cf_subsets=`echo "$LIB_SUBSETS" | sed -e 's/^termlib.* //'`
		fi

		if test $cf_dir = c++; then
			if test "x$with_shared_cxx" != xyes && test -n "$cf_shared_suffix"; then
				cf_list=
				for cf_item in $Libs_To_Make
				do
					case $cf_item in
					*.a)
						;;
					*)
						cf_item=`echo "$cf_item" | sed -e "s,"$cf_shared_suffix",.a,"`
						;;
					esac
					for cf_test in $cf_list
					do
						if test "$cf_test" = "$cf_item"
						then
							cf_LIST_MODELS=`echo "$cf_LIST_MODELS" | sed -e 's/normal//'`
							cf_item=
							break
						fi
					done
					test -n "$cf_item" && cf_list="$cf_list $cf_item"
				done
				Libs_To_Make="$cf_list"
			fi
		fi

		sed -e "s%@Libs_To_Make@%$Libs_To_Make%" \
		    -e "s%@SHARED_LIB@%$SHARED_LIB%" \
			$cf_dir/Makefile >$cf_dir/Makefile.out
		mv $cf_dir/Makefile.out $cf_dir/Makefile

		$AWK -f $srcdir/mk-0th.awk \
			libname="${cf_dir}${LIB_SUFFIX}" subsets="$LIB_SUBSETS" ticlib="$TICS_LIB_SUFFIX" termlib="$TINFO_LIB_SUFFIX" \
			$srcdir/$cf_dir/modules >>$cf_dir/Makefile

		for cf_subset in $cf_subsets
		do
			cf_subdirs=
			for cf_item in $cf_LIST_MODELS
			do

			echo "Appending rules for ${cf_item} model (${cf_dir}: ${cf_subset})"
			CF_UPPER(cf_ITEM,$cf_item)

			CXX_MODEL=$cf_ITEM
			if test "$CXX_MODEL" = SHARED; then
				case $cf_cv_shlib_version in #(vi
				cygdll|msysdll|mingw) #(vi
					test "x$with_shared_cxx" = xno && CF_VERBOSE(overriding CXX_MODEL to SHARED)
					with_shared_cxx=yes
					;;
				*)
					test "x$with_shared_cxx" = xno && CXX_MODEL=NORMAL
					;;
				esac
			fi

			CF_LIB_SUFFIX($cf_item,cf_suffix,cf_depsuf)
			CF_OBJ_SUBDIR($cf_item,cf_subdir)

			# Test for case where we build libtinfo with a different name.
			cf_libname=$cf_dir
			if test $cf_dir = ncurses ; then
				case $cf_subset in
				*base*)
					cf_libname=${cf_libname}$LIB_SUFFIX
					;;
				*termlib*)
					cf_libname=$TINFO_LIB_SUFFIX
					;;
				ticlib*)
					cf_libname=$TICS_LIB_SUFFIX
					;;
				esac
			elif test $cf_dir = c++ ; then
				cf_libname=ncurses++$LIB_SUFFIX
			else
				cf_libname=${cf_libname}$LIB_SUFFIX
			fi
			if test -n "${DFT_ARG_SUFFIX}" ; then
				# undo $LIB_SUFFIX add-on in CF_LIB_SUFFIX
				cf_suffix=`echo $cf_suffix |sed -e "s%^${LIB_SUFFIX}%%"`
			fi

			# These dependencies really are for development, not
			# builds, but they are useful in porting, too.
			cf_depend="../include/ncurses_cfg.h"
			if test "$srcdir" = "."; then
				cf_reldir="."
			else
				cf_reldir="\${srcdir}"
			fi

			if test -f $srcdir/$cf_dir/$cf_dir.priv.h; then
				cf_depend="$cf_depend $cf_reldir/$cf_dir.priv.h"
			elif test -f $srcdir/$cf_dir/curses.priv.h; then
				cf_depend="$cf_depend $cf_reldir/curses.priv.h"
			fi

 			cf_dir_suffix=
 			old_cf_suffix="$cf_suffix"
 			if test "$cf_cv_shlib_version_infix" = yes ; then
			if test -n "$LIB_SUFFIX" ; then
				case $LIB_SUFFIX in
				tw*)
					cf_libname=`echo $cf_libname | sed 's/tw$//'`
					cf_suffix=`echo $cf_suffix | sed 's/^tw//'`
					cf_dir_suffix=tw
					;;
				t*)
					cf_libname=`echo $cf_libname | sed 's/t$//'`
					cf_suffix=`echo $cf_suffix | sed 's/^t//'`
					cf_dir_suffix=t
					;;
				w*)
					cf_libname=`echo $cf_libname | sed 's/w$//'`
					cf_suffix=`echo $cf_suffix | sed 's/^w//'`
					cf_dir_suffix=w
					;;
				esac
			fi
 			fi

			$AWK -f $srcdir/mk-1st.awk \
				name=${cf_libname}${cf_dir_suffix} \
				traces=$LIB_TRACING \
				MODEL=$cf_ITEM \
				CXX_MODEL=$CXX_MODEL \
				model=$cf_subdir \
				prefix=$cf_prefix \
				suffix=$cf_suffix \
				subset=$cf_subset \
				driver=$cf_cv_term_driver \
				SymLink="$LN_S" \
				TermlibRoot=$TINFO_NAME \
				TermlibSuffix=$TINFO_SUFFIX \
				ShlibVer=$cf_cv_shlib_version \
				ShlibVerInfix=$cf_cv_shlib_version_infix \
				ReLink=${cf_cv_do_relink:-no} \
				DoLinks=$cf_cv_do_symlinks \
				rmSoLocs=$cf_cv_rm_so_locs \
				ldconfig="$LDCONFIG" \
				overwrite=$WITH_OVERWRITE \
				depend="$cf_depend" \
				host="$host" \
				libtool_version="$LIBTOOL_VERSION" \
				$srcdir/$cf_dir/modules >>$cf_dir/Makefile

			cf_suffix="$old_cf_suffix"

			for cf_subdir2 in $cf_subdirs lib
			do
				test $cf_subdir = $cf_subdir2 && break
			done
			test "${cf_subset}.${cf_subdir2}" != "${cf_subset}.${cf_subdir}" && \
			$AWK -f $srcdir/mk-2nd.awk \
				name=$cf_dir \
				traces=$LIB_TRACING \
				MODEL=$cf_ITEM \
				model=$cf_subdir \
				subset=$cf_subset \
				srcdir=$srcdir \
				echo=$WITH_ECHO \
				crenames=$cf_cv_prog_CC_c_o \
				cxxrenames=$cf_cv_prog_CXX_c_o \
				$srcdir/$cf_dir/modules >>$cf_dir/Makefile
			cf_subdirs="$cf_subdirs $cf_subdir"
			done
		done
	fi

	echo '	cd '$cf_dir' && ${MAKE} ${TOP_MFLAGS} [$]@' >>Makefile
done

for cf_dir in $SRC_SUBDIRS
do
	if test ! -d $srcdir/$cf_dir ; then
		continue
	fi

	if test -f $cf_dir/Makefile ; then
		case "$cf_dir" in
		Ada95) #(vi
			echo 'libs \' >> Makefile
			echo 'install.libs \' >> Makefile
			echo 'uninstall.libs ::' >> Makefile
			echo '	cd '$cf_dir' && ${MAKE} ${TOP_MFLAGS} [$]@' >> Makefile
			;;
		esac
	fi

	if test -f $srcdir/$cf_dir/modules; then
		echo >> Makefile
		if test -f $srcdir/$cf_dir/headers; then
cat >> Makefile <<CF_EOF
install.includes \\
uninstall.includes \\
CF_EOF
		fi
if test "$cf_dir" != "c++" ; then
echo 'lint \' >> Makefile
fi
cat >> Makefile <<CF_EOF
libs \\
lintlib \\
install.libs \\
uninstall.libs \\
install.$cf_dir \\
uninstall.$cf_dir ::
	cd $cf_dir && \${MAKE} \${TOP_MFLAGS} \[$]@
CF_EOF
	elif test -f $srcdir/$cf_dir/headers; then
cat >> Makefile <<CF_EOF

libs \\
install.libs \\
uninstall.libs \\
install.includes \\
uninstall.includes ::
	cd $cf_dir && \${MAKE} \${TOP_MFLAGS} \[$]@
CF_EOF
fi
done

if test "x$cf_with_db_install" = xyes; then
cat >> Makefile <<CF_EOF

install.libs uninstall.libs \\
install.data uninstall.data ::
$MAKE_TERMINFO	cd misc && \${MAKE} \${TOP_MFLAGS} \[$]@
CF_EOF
fi

if test "x$cf_with_manpages" = xyes; then
cat >> Makefile <<CF_EOF

install.man \\
uninstall.man ::
	cd man && \${MAKE} \${TOP_MFLAGS} \[$]@
CF_EOF
fi

cat >> Makefile <<CF_EOF

distclean ::
	rm -f config.cache config.log config.status Makefile include/ncurses_cfg.h
	rm -f headers.sh headers.sed mk_shared_lib.sh
	rm -f edit_man.* man_alias.*
	rm -rf \${DIRS_TO_MAKE}
CF_EOF

# Special case: tack's manpage lives in its own directory.
if test "x$cf_with_manpages" = xyes; then
if test -d tack ; then
if test -f $srcdir/$tack.h; then
cat >> Makefile <<CF_EOF

install.man \\
uninstall.man ::
	cd tack && \${MAKE} \${TOP_MFLAGS} \[$]@
CF_EOF
fi
fi
fi

dnl If we're installing into a subdirectory of /usr/include, etc., we should
dnl prepend the subdirectory's name to the "#include" paths.  It won't hurt
dnl anything, and will make it more standardized.  It's awkward to decide this
dnl at configuration because of quoting, so we'll simply make all headers
dnl installed via a script that can do the right thing.

rm -f headers.sed headers.sh

dnl ( generating this script makes the makefiles a little tidier :-)
echo creating headers.sh
cat >headers.sh <<CF_EOF
#! /bin/sh
# This shell script is generated by the 'configure' script.  It is invoked in a
# subdirectory of the build tree.  It generates a sed-script in the parent
# directory that is used to adjust includes for header files that reside in a
# subdirectory of /usr/include, etc.
PRG=""
while test \[$]# != 3
do
PRG="\$PRG \[$]1"; shift
done
DST=\[$]1
REF=\[$]2
SRC=\[$]3
TMPSRC=\${TMPDIR:-/tmp}/\`basename \$SRC\`\$\$
TMPSED=\${TMPDIR:-/tmp}/headers.sed\$\$
echo installing \$SRC in \$DST
CF_EOF

if test $WITH_CURSES_H = yes; then
	cat >>headers.sh <<CF_EOF
case \$DST in
/*/include/*)
	END=\`basename \$DST\`
	for i in \`cat \$REF/../*/headers |fgrep -v "#"\`
	do
		NAME=\`basename \$i\`
		echo "s/<\$NAME>/<\$END\/\$NAME>/" >> \$TMPSED
	done
	;;
*)
	echo "" >> \$TMPSED
	;;
esac
CF_EOF

else
	cat >>headers.sh <<CF_EOF
case \$DST in
/*/include/*)
	END=\`basename \$DST\`
	for i in \`cat \$REF/../*/headers |fgrep -v "#"\`
	do
		NAME=\`basename \$i\`
		if test "\$NAME" = "curses.h"
		then
			echo "s/<curses.h>/<ncurses.h>/" >> \$TMPSED
			NAME=ncurses.h
		fi
		echo "s/<\$NAME>/<\$END\/\$NAME>/" >> \$TMPSED
	done
	;;
*)
	echo "s/<curses.h>/<ncurses.h>/" >> \$TMPSED
	;;
esac
CF_EOF
fi
cat >>headers.sh <<CF_EOF
rm -f \$TMPSRC
sed -f \$TMPSED \$SRC > \$TMPSRC
NAME=\`basename \$SRC\`
CF_EOF
if test $WITH_CURSES_H != yes; then
	cat >>headers.sh <<CF_EOF
test "\$NAME" = "curses.h" && NAME=ncurses.h
CF_EOF
fi
cat >>headers.sh <<CF_EOF
# Just in case someone gzip'd manpages, remove the conflicting copy.
test -f \$DST/\$NAME.gz && rm -f \$DST/\$NAME.gz

eval \$PRG \$TMPSRC \$DST/\$NAME
rm -f \$TMPSRC \$TMPSED
CF_EOF

chmod 0755 headers.sh

for cf_dir in $SRC_SUBDIRS
do
	if test ! -d $srcdir/$cf_dir ; then
		continue
	fi

	if test -f $srcdir/$cf_dir/headers; then
		$AWK -f $srcdir/mk-hdr.awk \
			subset="$LIB_SUBSETS" \
			compat="$WITH_CURSES_H" \
			$srcdir/$cf_dir/headers >>$cf_dir/Makefile
	fi

	if test -f $srcdir/$cf_dir/modules; then
		if test "$cf_dir" != "c++" ; then
			cat >>$cf_dir/Makefile <<"CF_EOF"
depend : ${AUTO_SRC}
	makedepend -- ${CPPFLAGS} -- ${C_SRC}

# DO NOT DELETE THIS LINE -- make depend depends on it.
CF_EOF
		fi
	fi
done
AC_SUBST(Libs_To_Make)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_LIB_SONAME version: 5 updated: 2010/08/14 18:25:37
dnl -------------
dnl Find the and soname for the given shared library.  Set the cache variable
dnl cf_cv_$3_soname to this, unless it is not found.  Then set the cache
dnl variable to "unknown".
dnl
dnl $1 = headers
dnl $2 = code
dnl $3 = library name
AC_DEFUN([CF_LIB_SONAME],
[
AC_CACHE_CHECK(for soname of $3 library,cf_cv_$3_soname,[

cf_cv_$3_soname=unknown
if test "$cross_compiling" != yes ; then
cat >conftest.$ac_ext <<CF_EOF
$1
int main()
{
$2
	${cf_cv_main_return:-return}(0);
}
CF_EOF
cf_save_LIBS="$LIBS"
	CF_ADD_LIB($3)
	if AC_TRY_EVAL(ac_compile) ; then
		if AC_TRY_EVAL(ac_link) ; then
			cf_cv_$3_soname=`ldd conftest$ac_exeext 2>/dev/null | sed -e 's,^.*/,,' -e 's, .*$,,' | fgrep lib$3.`
			test -z "$cf_cv_$3_soname" && cf_cv_$3_soname=unknown
		fi
	fi
rm -rf conftest*
LIBS="$cf_save_LIBS"
fi
])
])
dnl ---------------------------------------------------------------------------
dnl CF_LIB_SUFFIX version: 22 updated: 2013/09/07 13:54:05
dnl -------------
dnl Compute the library file-suffix from the given model name
dnl $1 = model name
dnl $2 = variable to set (the nominal library suffix)
dnl $3 = dependency variable to set (actual filename)
dnl The variable $LIB_SUFFIX, if set, prepends the variable to set.
AC_DEFUN([CF_LIB_SUFFIX],
[
	case X$1 in #(vi
	Xlibtool) #(vi
		$2='.la'
		$3=[$]$2
		;;
	Xdebug) #(vi
		$2='_g.a'
		$3=[$]$2
		;;
	Xprofile) #(vi
		$2='_p.a'
		$3=[$]$2
		;;
	Xshared) #(vi
		case $cf_cv_system_name in
		aix[[5-7]]*) #(vi
			$2='.a'
			$3=[$]$2
			;;
		cygwin*|msys*|mingw*) #(vi
			$2='.dll'
			$3='.dll.a'
			;;
		darwin*) #(vi
			$2='.dylib'
			$3=[$]$2
			;;
		hpux*) #(vi
			case $target in
			ia64*) #(vi
				$2='.so'
				$3=[$]$2
				;;
			*) #(vi
				$2='.sl'
				$3=[$]$2
				;;
			esac
			;;
		*) #(vi
			$2='.so'
			$3=[$]$2
			;;
		esac
		;;
	*)
		$2='.a'
		$3=[$]$2
		;;
	esac
	test -n "$LIB_SUFFIX" && $2="${LIB_SUFFIX}[$]{$2}"
	test -n "$LIB_SUFFIX" && $3="${LIB_SUFFIX}[$]{$3}"
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_LIB_TYPE version: 4 updated: 2000/10/20 22:57:49
dnl -----------
dnl Compute the string to append to -library from the given model name
dnl $1 = model name
dnl $2 = variable to set
dnl The variable $LIB_SUFFIX, if set, prepends the variable to set.
AC_DEFUN([CF_LIB_TYPE],
[
	case $1 in
	libtool) $2=''   ;;
	normal)  $2=''   ;;
	debug)   $2='_g' ;;
	profile) $2='_p' ;;
	shared)  $2=''   ;;
	esac
	test -n "$LIB_SUFFIX" && $2="${LIB_SUFFIX}[$]{$2}"
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_LINK_DATAONLY version: 10 updated: 2012/10/06 17:41:51
dnl ----------------
dnl Some systems have a non-ANSI linker that doesn't pull in modules that have
dnl only data (i.e., no functions), for example NeXT.  On those systems we'll
dnl have to provide wrappers for global tables to ensure they're linked
dnl properly.
AC_DEFUN([CF_LINK_DATAONLY],
[
AC_MSG_CHECKING([if data-only library module links])
AC_CACHE_VAL(cf_cv_link_dataonly,[
	rm -f conftest.a
	cat >conftest.$ac_ext <<EOF
#line __oline__ "configure"
int	testdata[[3]] = { 123, 456, 789 };
EOF
	if AC_TRY_EVAL(ac_compile) ; then
		mv conftest.o data.o && \
		( $AR $ARFLAGS conftest.a data.o ) 2>&AC_FD_CC 1>/dev/null
	fi
	rm -f conftest.$ac_ext data.o
	cat >conftest.$ac_ext <<EOF
#line __oline__ "configure"
int	testfunc()
{
#if defined(NeXT)
	${cf_cv_main_return:-return}(1);	/* I'm told this linker is broken */
#else
	extern int testdata[[3]];
	return testdata[[0]] == 123
	   &&  testdata[[1]] == 456
	   &&  testdata[[2]] == 789;
#endif
}
EOF
	if AC_TRY_EVAL(ac_compile); then
		mv conftest.o func.o && \
		( $AR $ARFLAGS conftest.a func.o ) 2>&AC_FD_CC 1>/dev/null
	fi
	rm -f conftest.$ac_ext func.o
	( eval $RANLIB conftest.a ) 2>&AC_FD_CC >/dev/null
	cf_saveLIBS="$LIBS"
	LIBS="conftest.a $LIBS"
	AC_TRY_RUN([
	int main()
	{
		extern int testfunc();
		${cf_cv_main_return:-return} (!testfunc());
	}
	],
	[cf_cv_link_dataonly=yes],
	[cf_cv_link_dataonly=no],
	[cf_cv_link_dataonly=unknown])
	LIBS="$cf_saveLIBS"
	])
AC_MSG_RESULT($cf_cv_link_dataonly)

if test "$cf_cv_link_dataonly" = no ; then
	AC_DEFINE(BROKEN_LINKER,1,[if data-only library module does not link])
	BROKEN_LINKER=1
fi

])dnl
dnl ---------------------------------------------------------------------------
dnl CF_LINK_FUNCS version: 8 updated: 2012/10/06 17:56:13
dnl -------------
dnl Most Unix systems have both link and symlink, a few don't have symlink.
dnl A few non-Unix systems implement symlink, but not link.
dnl A few non-systems implement neither (or have nonfunctional versions).
AC_DEFUN([CF_LINK_FUNCS],
[
AC_CHECK_FUNCS( \
	remove \
	unlink )

if test "$cross_compiling" = yes ; then
	AC_CHECK_FUNCS( \
		link \
		symlink )
else
	AC_CACHE_CHECK(if link/symlink functions work,cf_cv_link_funcs,[
		cf_cv_link_funcs=
		for cf_func in link symlink ; do
			AC_TRY_RUN([
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
int main()
{
	int fail = 0;
	char *src = "config.log";
	char *dst = "conftest.chk";
	struct stat src_sb;
	struct stat dst_sb;

	stat(src, &src_sb);
	fail = ($cf_func("config.log", "conftest.chk") < 0)
	    || (stat(dst, &dst_sb) < 0)
	    || (dst_sb.st_mtime != src_sb.st_mtime);
#ifdef HAVE_UNLINK
	unlink(dst);
#else
	remove(dst);
#endif
	${cf_cv_main_return:-return} (fail);
}
			],[
			cf_cv_link_funcs="$cf_cv_link_funcs $cf_func"
			eval 'ac_cv_func_'$cf_func'=yes'],[
			eval 'ac_cv_func_'$cf_func'=no'],[
			eval 'ac_cv_func_'$cf_func'=error'])
		done
		test -z "$cf_cv_link_funcs" && cf_cv_link_funcs=no
	])
	test "$ac_cv_func_link"    = yes && AC_DEFINE(HAVE_LINK,1,[Define to 1 if we have link() function])
	test "$ac_cv_func_symlink" = yes && AC_DEFINE(HAVE_SYMLINK,1,[Define to 1 if we have symlink() function])
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_MAKEFLAGS version: 14 updated: 2011/03/31 19:29:46
dnl ------------
dnl Some 'make' programs support ${MAKEFLAGS}, some ${MFLAGS}, to pass 'make'
dnl options to lower-levels.  It's very useful for "make -n" -- if we have it.
dnl (GNU 'make' does both, something POSIX 'make', which happens to make the
dnl ${MAKEFLAGS} variable incompatible because it adds the assignments :-)
AC_DEFUN([CF_MAKEFLAGS],
[
AC_CACHE_CHECK(for makeflags variable, cf_cv_makeflags,[
	cf_cv_makeflags=''
	for cf_option in '-${MAKEFLAGS}' '${MFLAGS}'
	do
		cat >cf_makeflags.tmp <<CF_EOF
SHELL = /bin/sh
all :
	@ echo '.$cf_option'
CF_EOF
		cf_result=`${MAKE:-make} -k -f cf_makeflags.tmp 2>/dev/null | fgrep -v "ing directory" | sed -e 's,[[ 	]]*$,,'`
		case "$cf_result" in
		.*k)
			cf_result=`${MAKE:-make} -k -f cf_makeflags.tmp CC=cc 2>/dev/null`
			case "$cf_result" in
			.*CC=*)	cf_cv_makeflags=
				;;
			*)	cf_cv_makeflags=$cf_option
				;;
			esac
			break
			;;
		.-)	;;
		*)	echo "given option \"$cf_option\", no match \"$cf_result\""
			;;
		esac
	done
	rm -f cf_makeflags.tmp
])

AC_SUBST(cf_cv_makeflags)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_MAKE_TAGS version: 6 updated: 2010/10/23 15:52:32
dnl ------------
dnl Generate tags/TAGS targets for makefiles.  Do not generate TAGS if we have
dnl a monocase filesystem.
AC_DEFUN([CF_MAKE_TAGS],[
AC_REQUIRE([CF_MIXEDCASE_FILENAMES])

AC_CHECK_PROGS(CTAGS, exctags ctags)
AC_CHECK_PROGS(ETAGS, exetags etags)

AC_CHECK_PROG(MAKE_LOWER_TAGS, ${CTAGS:-ctags}, yes, no)

if test "$cf_cv_mixedcase" = yes ; then
	AC_CHECK_PROG(MAKE_UPPER_TAGS, ${ETAGS:-etags}, yes, no)
else
	MAKE_UPPER_TAGS=no
fi

if test "$MAKE_UPPER_TAGS" = yes ; then
	MAKE_UPPER_TAGS=
else
	MAKE_UPPER_TAGS="#"
fi

if test "$MAKE_LOWER_TAGS" = yes ; then
	MAKE_LOWER_TAGS=
else
	MAKE_LOWER_TAGS="#"
fi

AC_SUBST(CTAGS)
AC_SUBST(ETAGS)

AC_SUBST(MAKE_UPPER_TAGS)
AC_SUBST(MAKE_LOWER_TAGS)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_MANPAGE_FORMAT version: 9 updated: 2010/10/23 16:10:30
dnl -----------------
dnl Option to allow user to override automatic configuration of manpage format.
dnl There are several special cases:
dnl
dnl	gzip - man checks for, can display gzip'd files
dnl	compress - man checks for, can display compressed files
dnl	BSDI - files in the cat-directories are suffixed ".0"
dnl	formatted - installer should format (put files in cat-directory)
dnl	catonly - installer should only format, e.g., for a turnkey system.
dnl
dnl There are other configurations which this macro does not test, e.g., HPUX's
dnl compressed manpages (but uncompressed manpages are fine, and HPUX's naming
dnl convention would not match our use).
AC_DEFUN([CF_MANPAGE_FORMAT],
[
AC_REQUIRE([CF_PATHSEP])
AC_MSG_CHECKING(format of man-pages)

AC_ARG_WITH(manpage-format,
	[  --with-manpage-format   specify manpage-format: gzip/compress/BSDI/normal and
                          optionally formatted/catonly, e.g., gzip,formatted],
	[MANPAGE_FORMAT=$withval],
	[MANPAGE_FORMAT=unknown])

test -z "$MANPAGE_FORMAT" && MANPAGE_FORMAT=unknown
MANPAGE_FORMAT=`echo "$MANPAGE_FORMAT" | sed -e 's/,/ /g'`

cf_unknown=

case $MANPAGE_FORMAT in
unknown)
  if test -z "$MANPATH" ; then
    MANPATH="/usr/man:/usr/share/man"
  fi

  # look for the 'date' man-page (it's most likely to be installed!)
  MANPAGE_FORMAT=
  cf_preform=no
  cf_catonly=yes
  cf_example=date

  IFS="${IFS:- 	}"; ac_save_ifs="$IFS"; IFS="${IFS}${PATH_SEPARATOR}"
  for cf_dir in $MANPATH; do
    test -z "$cf_dir" && cf_dir=/usr/man
    for cf_name in $cf_dir/man*/$cf_example.[[01]]* $cf_dir/cat*/$cf_example.[[01]]* $cf_dir/man*/$cf_example $cf_dir/cat*/$cf_example
    do
      cf_test=`echo $cf_name | sed -e 's/*//'`
      if test "x$cf_test" = "x$cf_name" ; then

	case "$cf_name" in
	*.gz) MANPAGE_FORMAT="$MANPAGE_FORMAT gzip";;
	*.Z)  MANPAGE_FORMAT="$MANPAGE_FORMAT compress";;
	*.0)	MANPAGE_FORMAT="$MANPAGE_FORMAT BSDI";;
	*)    MANPAGE_FORMAT="$MANPAGE_FORMAT normal";;
	esac

	case "$cf_name" in
	$cf_dir/man*)
	  cf_catonly=no
	  ;;
	$cf_dir/cat*)
	  cf_preform=yes
	  ;;
	esac
	break
      fi

      # if we found a match in either man* or cat*, stop looking
      if test -n "$MANPAGE_FORMAT" ; then
	cf_found=no
	test "$cf_preform" = yes && MANPAGE_FORMAT="$MANPAGE_FORMAT formatted"
	test "$cf_catonly" = yes && MANPAGE_FORMAT="$MANPAGE_FORMAT catonly"
	case "$cf_name" in
	$cf_dir/cat*)
	  cf_found=yes
	  ;;
	esac
	test $cf_found=yes && break
      fi
    done
    # only check the first directory in $MANPATH where we find manpages
    if test -n "$MANPAGE_FORMAT" ; then
       break
    fi
  done
  # if we did not find the example, just assume it is normal
  test -z "$MANPAGE_FORMAT" && MANPAGE_FORMAT=normal
  IFS="$ac_save_ifs"
  ;;
*)
  for cf_option in $MANPAGE_FORMAT; do
     case $cf_option in #(vi
     gzip|compress|BSDI|normal|formatted|catonly)
       ;;
     *)
       cf_unknown="$cf_unknown $cf_option"
       ;;
     esac
  done
  ;;
esac

AC_MSG_RESULT($MANPAGE_FORMAT)
if test -n "$cf_unknown" ; then
  AC_MSG_WARN(Unexpected manpage-format $cf_unknown)
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_MANPAGE_RENAMES version: 7 updated: 2005/06/18 18:51:57
dnl ------------------
dnl The Debian people have their own naming convention for manpages.  This
dnl option lets us override the name of the file containing renaming, or
dnl disable it altogether.
AC_DEFUN([CF_MANPAGE_RENAMES],
[
AC_MSG_CHECKING(for manpage renaming)

AC_ARG_WITH(manpage-renames,
	[  --with-manpage-renames  specify manpage-renaming],
	[MANPAGE_RENAMES=$withval],
	[MANPAGE_RENAMES=yes])

case ".$MANPAGE_RENAMES" in #(vi
.no) #(vi
  ;;
.|.yes)
  # Debian 'man' program?
  if test -f /etc/debian_version ; then
    MANPAGE_RENAMES=`cd $srcdir && pwd`/man/man_db.renames
  else
    MANPAGE_RENAMES=no
  fi
  ;;
esac

if test "$MANPAGE_RENAMES" != no ; then
  if test -f $srcdir/man/$MANPAGE_RENAMES ; then
    MANPAGE_RENAMES=`cd $srcdir/man && pwd`/$MANPAGE_RENAMES
  elif test ! -f $MANPAGE_RENAMES ; then
    AC_MSG_ERROR(not a filename: $MANPAGE_RENAMES)
  fi

  test ! -d man && mkdir man

  # Construct a sed-script to perform renaming within man-pages
  if test -n "$MANPAGE_RENAMES" ; then
    test ! -d man && mkdir man
    sh $srcdir/man/make_sed.sh $MANPAGE_RENAMES >./edit_man.sed
  fi
fi

AC_MSG_RESULT($MANPAGE_RENAMES)
AC_SUBST(MANPAGE_RENAMES)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_MANPAGE_SYMLINKS version: 5 updated: 2010/07/24 17:12:40
dnl -------------------
dnl Some people expect each tool to make all aliases for manpages in the
dnl man-directory.  This accommodates the older, less-capable implementations
dnl of 'man', and is optional.
AC_DEFUN([CF_MANPAGE_SYMLINKS],
[
AC_MSG_CHECKING(if manpage aliases will be installed)

AC_ARG_WITH(manpage-aliases,
	[  --with-manpage-aliases  specify manpage-aliases using .so],
	[MANPAGE_ALIASES=$withval],
	[MANPAGE_ALIASES=yes])

AC_MSG_RESULT($MANPAGE_ALIASES)

case "x$LN_S" in #(vi
xln*) #(vi
	cf_use_symlinks=yes
	;;
*)
	cf_use_symlinks=no
	;;
esac

MANPAGE_SYMLINKS=no
if test "$MANPAGE_ALIASES" = yes ; then
AC_MSG_CHECKING(if manpage symlinks should be used)

AC_ARG_WITH(manpage-symlinks,
	[  --with-manpage-symlinks specify manpage-aliases using symlinks],
	[MANPAGE_SYMLINKS=$withval],
	[MANPAGE_SYMLINKS=$cf_use_symlinks])

if test "$$cf_use_symlinks" = no; then
if test "$MANPAGE_SYMLINKS" = yes ; then
	AC_MSG_WARN(cannot make symlinks, will use .so files)
	MANPAGE_SYMLINKS=no
fi
fi

AC_MSG_RESULT($MANPAGE_SYMLINKS)
fi

])dnl
dnl ---------------------------------------------------------------------------
dnl CF_MANPAGE_TBL version: 3 updated: 2002/01/19 22:51:32
dnl --------------
dnl This option causes manpages to be run through tbl(1) to generate tables
dnl correctly.
AC_DEFUN([CF_MANPAGE_TBL],
[
AC_MSG_CHECKING(for manpage tbl)

AC_ARG_WITH(manpage-tbl,
	[  --with-manpage-tbl      specify manpage processing with tbl],
	[MANPAGE_TBL=$withval],
	[MANPAGE_TBL=no])

AC_MSG_RESULT($MANPAGE_TBL)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_MAN_PAGES version: 43 updated: 2013/02/09 12:53:45
dnl ------------
dnl Try to determine if the man-pages on the system are compressed, and if
dnl so, what format is used.  Use this information to construct a script that
dnl will install man-pages.
AC_DEFUN([CF_MAN_PAGES],
[
CF_HELP_MESSAGE(Options to Specify How Manpages are Installed:)
CF_MANPAGE_FORMAT
CF_MANPAGE_RENAMES
CF_MANPAGE_SYMLINKS
CF_MANPAGE_TBL

  if test "$prefix" = "NONE" ; then
     cf_prefix="$ac_default_prefix"
  else
     cf_prefix="$prefix"
  fi

  case "$MANPAGE_FORMAT" in # (vi
  *catonly*) # (vi
    cf_format=yes
    cf_inboth=no
    ;;
  *formatted*) # (vi
    cf_format=yes
    cf_inboth=yes
    ;;
  *)
    cf_format=no
    cf_inboth=no
    ;;
  esac

test ! -d man && mkdir man

cf_so_strip=
cf_compress=
case "$MANPAGE_FORMAT" in #(vi
*compress*) #(vi
	cf_so_strip="Z"
	cf_compress=compress
  ;;
*gzip*)
	cf_so_strip="gz"
	cf_compress=gzip
  ;;
esac

cf_edit_man=./edit_man.sh
cf_man_alias=`pwd`/man_alias.sed

cat >$cf_edit_man <<CF_EOF
#! /bin/sh
# this script is generated by the configure-script CF_MAN_PAGES macro.

prefix="$cf_prefix"
datarootdir="$datarootdir"
datadir="$datadir"

NCURSES_MAJOR="$NCURSES_MAJOR"
NCURSES_MINOR="$NCURSES_MINOR"
NCURSES_PATCH="$NCURSES_PATCH"

NCURSES_OSPEED="$NCURSES_OSPEED"
TERMINFO="$TERMINFO"

INSTALL="$INSTALL"
INSTALL_DATA="$INSTALL_DATA"

transform="$program_transform_name"

TMP=\${TMPDIR:=/tmp}/man\$\$
trap "rm -f \$TMP" 0 1 2 5 15

form=\[$]1
shift || exit 1

verb=\[$]1
shift || exit 1

mandir=\[$]1
shift || exit 1

srcdir=\[$]1
top_srcdir=\[$]srcdir/..
shift || exit 1

if test "\$form" = normal ; then
	if test "$cf_format" = yes ; then
	if test "$cf_inboth" = no ; then
		sh \[$]0 format \$verb \$mandir \$srcdir \[$]*
		exit $?
	fi
	fi
	cf_subdir=\$mandir/man
	cf_tables=$MANPAGE_TBL
else
	cf_subdir=\$mandir/cat
	cf_tables=yes
fi

# process the list of source-files
for i in \[$]* ; do
case \$i in #(vi
*.orig|*.rej) ;; #(vi
*.[[0-9]]*)
	section=\`expr "\$i" : '.*\\.\\([[0-9]]\\)[[xm]]*'\`;
	if test \$verb = installing ; then
	if test ! -d \$cf_subdir\${section} ; then
		mkdir -p \$cf_subdir\$section
	fi
	fi

	# replace variables in man page
	if test ! -f $cf_man_alias ; then
cat >>$cf_man_alias <<-CF_EOF2
		s,@DATADIR@,\$datadir,g
		s,@TERMINFO@,\${TERMINFO:="no default value"},g
		s,@TERMINFO_DIRS@,\${TERMINFO_DIRS:="no default value"},g
		s,@NCURSES_MAJOR@,\${NCURSES_MAJOR:="no default value"},g
		s,@NCURSES_MINOR@,\${NCURSES_MINOR:="no default value"},g
		s,@NCURSES_PATCH@,\${NCURSES_PATCH:="no default value"},g
		s,@NCURSES_OSPEED@,\${NCURSES_OSPEED:="no default value"},g
CF_EOF
	ifelse($1,,,[
	for cf_name in $1
	do
		cf_NAME=`echo "$cf_name" | sed y%abcdefghijklmnopqrstuvwxyz./-%ABCDEFGHIJKLMNOPQRSTUVWXYZ___%`
		cf_name=`echo $cf_name|sed "$program_transform_name"`
cat >>$cf_edit_man <<-CF_EOF
		s,@$cf_NAME@,$cf_name,g
CF_EOF
	done
	])
cat >>$cf_edit_man <<CF_EOF
CF_EOF2
		echo "...made $cf_man_alias"
	fi

	aliases=
	cf_source=\`basename \$i\`
	inalias=\$cf_source
	test ! -f \$inalias && inalias="\$srcdir/\$inalias"
	if test ! -f \$inalias ; then
		echo .. skipped \$cf_source
		continue
	fi
CF_EOF

if test "$MANPAGE_ALIASES" != no ; then
cat >>$cf_edit_man <<CF_EOF
	nCurses=ignore.3x
	test $with_curses_h = yes && nCurses=ncurses.3x
	aliases=\`sed -f \$top_srcdir/man/manlinks.sed \$inalias |sed -f $cf_man_alias | sort -u; test \$inalias = \$nCurses && echo curses\`
CF_EOF
fi

if test "$MANPAGE_RENAMES" = no ; then
cat >>$cf_edit_man <<CF_EOF
	# perform program transformations for section 1 man pages
	if test \$section = 1 ; then
		cf_target=\$cf_subdir\${section}/\`echo \$cf_source|sed "\${transform}"\`
	else
		cf_target=\$cf_subdir\${section}/\$cf_source
	fi
CF_EOF
else
cat >>$cf_edit_man <<CF_EOF
	cf_target=\`grep "^\$cf_source" $MANPAGE_RENAMES | $AWK '{print \[$]2}'\`
	if test -z "\$cf_target" ; then
		echo '? missing rename for '\$cf_source
		cf_target="\$cf_source"
	fi
	cf_target="\$cf_subdir\${section}/\${cf_target}"

CF_EOF
fi

cat >>$cf_edit_man <<CF_EOF
	sed	-f $cf_man_alias \\
CF_EOF

if test -f $MANPAGE_RENAMES ; then
cat >>$cf_edit_man <<CF_EOF
		< \$i | sed -f `pwd`/edit_man.sed >\$TMP
CF_EOF
else
cat >>$cf_edit_man <<CF_EOF
		< \$i >\$TMP
CF_EOF
fi

cat >>$cf_edit_man <<CF_EOF
if test \$cf_tables = yes ; then
	tbl \$TMP >\$TMP.out
	mv \$TMP.out \$TMP
fi
CF_EOF

if test $with_overwrite != yes ; then
cat >>$cf_edit_man <<CF_EOF
	sed -e "/\#[    ]*include/s,<curses.h,<ncurses$LIB_SUFFIX/curses.h," < \$TMP >\$TMP.out
	mv \$TMP.out \$TMP
CF_EOF
fi

if test $with_curses_h != yes ; then
cat >>$cf_edit_man <<CF_EOF
	sed -e "/\#[    ]*include/s,curses.h,ncurses.h," < \$TMP >\$TMP.out
	mv \$TMP.out \$TMP
CF_EOF
fi

cat >>$cf_edit_man <<CF_EOF
	if test \$form = format ; then
		nroff -man \$TMP >\$TMP.out
		mv \$TMP.out \$TMP
	fi
CF_EOF

if test -n "$cf_compress" ; then
cat >>$cf_edit_man <<CF_EOF
	if test \$verb = installing ; then
	if ( $cf_compress -f \$TMP )
	then
		mv \$TMP.$cf_so_strip \$TMP
	fi
	fi
	cf_target="\$cf_target.$cf_so_strip"
CF_EOF
fi

case "$MANPAGE_FORMAT" in #(vi
*BSDI*)
cat >>$cf_edit_man <<CF_EOF
	if test \$form = format ; then
		# BSDI installs only .0 suffixes in the cat directories
		cf_target="\`echo \$cf_target|sed -e 's/\.[[1-9]]\+[[a-z]]*/.0/'\`"
	fi
CF_EOF
  ;;
esac

cat >>$cf_edit_man <<CF_EOF
	suffix=\`basename \$cf_target | sed -e 's%^[[^.]]*%%'\`
	if test \$verb = installing ; then
		echo \$verb \$cf_target
		\$INSTALL_DATA \$TMP \$cf_target
		test -d \$cf_subdir\${section} &&
		test -n "\$aliases" && (
			cd \$cf_subdir\${section} && (
				cf_source=\`echo \$cf_target |sed -e 's%^.*/\([[^/]][[^/]]*/[[^/]][[^/]]*$\)%\1%'\`
				test -n "$cf_so_strip" && cf_source=\`echo \$cf_source |sed -e 's%\.$cf_so_strip\$%%'\`
				cf_target=\`basename \$cf_target\`
				for cf_alias in \$aliases
				do
					if test \$section = 1 ; then
						cf_alias=\`echo \$cf_alias|sed "\${transform}"\`
					fi

					if test "$MANPAGE_SYMLINKS" = yes ; then
						if test -f \$cf_alias\${suffix} ; then
							if ( cmp -s \$cf_target \$cf_alias\${suffix} )
							then
								continue
							fi
						fi
						echo .. \$verb alias \$cf_alias\${suffix}
CF_EOF
case "x$LN_S" in #(vi
*-f) #(vi
cat >>$cf_edit_man <<CF_EOF
						$LN_S \$cf_target \$cf_alias\${suffix}
CF_EOF
	;;
*)
cat >>$cf_edit_man <<CF_EOF
						rm -f \$cf_alias\${suffix}
						$LN_S \$cf_target \$cf_alias\${suffix}
CF_EOF
	;;
esac
cat >>$cf_edit_man <<CF_EOF
					elif test "\$cf_target" != "\$cf_alias\${suffix}" ; then
						echo ".so \$cf_source" >\$TMP
CF_EOF
if test -n "$cf_compress" ; then
cat >>$cf_edit_man <<CF_EOF
						if test -n "$cf_so_strip" ; then
							$cf_compress -f \$TMP
							mv \$TMP.$cf_so_strip \$TMP
						fi
CF_EOF
fi
cat >>$cf_edit_man <<CF_EOF
						echo .. \$verb alias \$cf_alias\${suffix}
						rm -f \$cf_alias\${suffix}
						\$INSTALL_DATA \$TMP \$cf_alias\${suffix}
					fi
				done
			)
		)
	elif test \$verb = removing ; then
		test -f \$cf_target && (
			echo \$verb \$cf_target
			rm -f \$cf_target
		)
		test -d \$cf_subdir\${section} &&
		test -n "\$aliases" && (
			cd \$cf_subdir\${section} && (
				for cf_alias in \$aliases
				do
					if test \$section = 1 ; then
						cf_alias=\`echo \$cf_alias|sed "\${transform}"\`
					fi

					echo .. \$verb alias \$cf_alias\${suffix}
					rm -f \$cf_alias\${suffix}
				done
			)
		)
	else
#		echo ".hy 0"
		cat \$TMP
	fi
	;;
esac
done

if test $cf_inboth = yes ; then
if test \$form != format ; then
	sh \[$]0 format \$verb \$mandir \$srcdir \[$]*
fi
fi

exit 0
CF_EOF
chmod 755 $cf_edit_man

])dnl
dnl ---------------------------------------------------------------------------
dnl CF_MATH_LIB version: 8 updated: 2010/05/29 16:31:02
dnl -----------
dnl Checks for libraries.  At least one UNIX system, Apple Macintosh
dnl Rhapsody 5.5, does not have -lm.  We cannot use the simpler
dnl AC_CHECK_LIB(m,sin), because that fails for C++.
AC_DEFUN([CF_MATH_LIB],
[
AC_CACHE_CHECK(if -lm needed for math functions,
	cf_cv_need_libm,[
	AC_TRY_LINK([
	#include <stdio.h>
	#include <math.h>
	],
	[double x = rand(); printf("result = %g\n", ]ifelse([$2],,sin(x),$2)[)],
	[cf_cv_need_libm=no],
	[cf_cv_need_libm=yes])])
if test "$cf_cv_need_libm" = yes
then
ifelse($1,,[
	CF_ADD_LIB(m)
],[$1=-lm])
fi
])
dnl ---------------------------------------------------------------------------
dnl CF_MIXEDCASE_FILENAMES version: 6 updated: 2013/10/08 17:47:05
dnl ----------------------
dnl Check if the file-system supports mixed-case filenames.  If we're able to
dnl create a lowercase name and see it as uppercase, it doesn't support that.
AC_DEFUN([CF_MIXEDCASE_FILENAMES],
[
AC_CACHE_CHECK(if filesystem supports mixed-case filenames,cf_cv_mixedcase,[
if test "$cross_compiling" = yes ; then
	case $target_alias in #(vi
	*-os2-emx*|*-msdosdjgpp*|*-cygwin*|*-msys*|*-mingw*|*-uwin*) #(vi
		cf_cv_mixedcase=no
		;;
	*)
		cf_cv_mixedcase=yes
		;;
	esac
else
	rm -f conftest CONFTEST
	echo test >conftest
	if test -f CONFTEST ; then
		cf_cv_mixedcase=no
	else
		cf_cv_mixedcase=yes
	fi
	rm -f conftest CONFTEST
fi
])
test "$cf_cv_mixedcase" = yes && AC_DEFINE(MIXEDCASE_FILENAMES,1,[Define to 1 if filesystem supports mixed-case filenames.])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_MKSTEMP version: 9 updated: 2012/10/03 04:34:49
dnl ----------
dnl Check for a working mkstemp.  This creates two files, checks that they are
dnl successfully created and distinct (AmigaOS apparently fails on the last).
AC_DEFUN([CF_MKSTEMP],[
AC_CACHE_CHECK(for working mkstemp, cf_cv_func_mkstemp,[
rm -rf conftest*
AC_TRY_RUN([
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
int main()
{
	char *tmpl = "conftestXXXXXX";
	char name[2][80];
	int n;
	int result = 0;
	int fd;
	struct stat sb;

	umask(077);
	for (n = 0; n < 2; ++n) {
		strcpy(name[n], tmpl);
		if ((fd = mkstemp(name[n])) >= 0) {
			if (!strcmp(name[n], tmpl)
			 || stat(name[n], &sb) != 0
			 || (sb.st_mode & S_IFMT) != S_IFREG
			 || (sb.st_mode & 077) != 0) {
				result = 1;
			}
			close(fd);
		}
	}
	if (result == 0
	 && !strcmp(name[0], name[1]))
		result = 1;
	${cf_cv_main_return:-return}(result);
}
],[cf_cv_func_mkstemp=yes
],[cf_cv_func_mkstemp=no
],[cf_cv_func_mkstemp=maybe])
])
if test "x$cf_cv_func_mkstemp" = xmaybe ; then
	AC_CHECK_FUNC(mkstemp)
fi
if test "x$cf_cv_func_mkstemp" = xyes || test "x$ac_cv_func_mkstemp" = xyes ; then
	AC_DEFINE(HAVE_MKSTEMP,1,[Define to 1 if mkstemp() is available and working.])
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_MSG_LOG version: 5 updated: 2010/10/23 15:52:32
dnl ----------
dnl Write a debug message to config.log, along with the line number in the
dnl configure script.
AC_DEFUN([CF_MSG_LOG],[
echo "${as_me:-configure}:__oline__: testing $* ..." 1>&AC_FD_CC
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_NCURSES_ABI_6 version: 1 updated: 2005/09/17 18:42:49
dnl ----------------
dnl Set ncurses' ABI to 6 unless overridden by explicit configure option, and
dnl warn about this.
AC_DEFUN([CF_NCURSES_ABI_6],[
if test "${with_abi_version+set}" != set; then
	case $cf_cv_rel_version in
	5.*)
		cf_cv_rel_version=6.0
		cf_cv_abi_version=6
		AC_MSG_WARN(Overriding ABI version to $cf_cv_abi_version)
		;;
	esac
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_NO_LEAKS_OPTION version: 5 updated: 2012/10/02 20:55:03
dnl ------------------
dnl see CF_WITH_NO_LEAKS
AC_DEFUN([CF_NO_LEAKS_OPTION],[
AC_MSG_CHECKING(if you want to use $1 for testing)
AC_ARG_WITH($1,
	[$2],
	[AC_DEFINE_UNQUOTED($3,1,"Define to 1 if you want to use $1 for testing.")ifelse([$4],,[
	 $4
])
	: ${with_cflags:=-g}
	: ${with_no_leaks:=yes}
	 with_$1=yes],
	[with_$1=])
AC_MSG_RESULT(${with_$1:-no})

case .$with_cflags in #(vi
.*-g*)
	case .$CFLAGS in #(vi
	.*-g*) #(vi
		;;
	*)
		CF_ADD_CFLAGS([-g])
		;;
	esac
	;;
esac
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_NUMBER_SYNTAX version: 1 updated: 2003/09/20 18:12:49
dnl ----------------
dnl Check if the given variable is a number.  If not, report an error.
dnl $1 is the variable
dnl $2 is the message
AC_DEFUN([CF_NUMBER_SYNTAX],[
if test -n "$1" ; then
  case $1 in #(vi
  [[0-9]]*) #(vi
 	;;
  *)
	AC_MSG_ERROR($2 is not a number: $1)
 	;;
  esac
else
  AC_MSG_ERROR($2 value is empty)
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_OBJ_SUBDIR version: 6 updated: 2013/09/07 14:06:10
dnl -------------
dnl Compute the object-directory name from the given model name
AC_DEFUN([CF_OBJ_SUBDIR],
[
	case $1 in
	libtool) $2='obj_lo'  ;;
	normal)  $2='objects' ;;
	debug)   $2='obj_g' ;;
	profile) $2='obj_p' ;;
	shared)
		case $cf_cv_system_name in #(vi
		cygwin|msys) #(vi
			$2='objects' ;;
		*)
			$2='obj_s' ;;
		esac
	esac
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_PATHSEP version: 6 updated: 2012/09/29 18:38:12
dnl ----------
dnl Provide a value for the $PATH and similar separator (or amend the value
dnl as provided in autoconf 2.5x).
AC_DEFUN([CF_PATHSEP],
[
	AC_MSG_CHECKING(for PATH separator)
	case $cf_cv_system_name in
	os2*)	PATH_SEPARATOR=';'  ;;
	*)	${PATH_SEPARATOR:=':'}  ;;
	esac
ifelse([$1],,,[$1=$PATH_SEPARATOR])
	AC_SUBST(PATH_SEPARATOR)
	AC_MSG_RESULT($PATH_SEPARATOR)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_PATH_SYNTAX version: 14 updated: 2012/06/19 20:58:54
dnl --------------
dnl Check the argument to see that it looks like a pathname.  Rewrite it if it
dnl begins with one of the prefix/exec_prefix variables, and then again if the
dnl result begins with 'NONE'.  This is necessary to work around autoconf's
dnl delayed evaluation of those symbols.
AC_DEFUN([CF_PATH_SYNTAX],[
if test "x$prefix" != xNONE; then
  cf_path_syntax="$prefix"
else
  cf_path_syntax="$ac_default_prefix"
fi

case ".[$]$1" in #(vi
.\[$]\(*\)*|.\'*\'*) #(vi
  ;;
..|./*|.\\*) #(vi
  ;;
.[[a-zA-Z]]:[[\\/]]*) #(vi OS/2 EMX
  ;;
.\[$]{*prefix}*|.\[$]{*dir}*) #(vi
  eval $1="[$]$1"
  case ".[$]$1" in #(vi
  .NONE/*)
    $1=`echo [$]$1 | sed -e s%NONE%$cf_path_syntax%`
    ;;
  esac
  ;; #(vi
.no|.NONE/*)
  $1=`echo [$]$1 | sed -e s%NONE%$cf_path_syntax%`
  ;;
*)
  ifelse([$2],,[AC_MSG_ERROR([expected a pathname, not \"[$]$1\"])],$2)
  ;;
esac
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_PKG_CONFIG version: 7 updated: 2011/04/29 04:53:22
dnl -------------
dnl Check for the package-config program, unless disabled by command-line.
AC_DEFUN([CF_PKG_CONFIG],
[
AC_MSG_CHECKING(if you want to use pkg-config)
AC_ARG_WITH(pkg-config,
	[  --with-pkg-config{=path} enable/disable use of pkg-config],
	[cf_pkg_config=$withval],
	[cf_pkg_config=yes])
AC_MSG_RESULT($cf_pkg_config)

case $cf_pkg_config in #(vi
no) #(vi
	PKG_CONFIG=none
	;;
yes) #(vi
	CF_ACVERSION_CHECK(2.52,
		[AC_PATH_TOOL(PKG_CONFIG, pkg-config, none)],
		[AC_PATH_PROG(PKG_CONFIG, pkg-config, none)])
	;;
*)
	PKG_CONFIG=$withval
	;;
esac

test -z "$PKG_CONFIG" && PKG_CONFIG=none
if test "$PKG_CONFIG" != none ; then
	CF_PATH_SYNTAX(PKG_CONFIG)
fi

AC_SUBST(PKG_CONFIG)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_POSIX_C_SOURCE version: 8 updated: 2010/05/26 05:38:42
dnl -----------------
dnl Define _POSIX_C_SOURCE to the given level, and _POSIX_SOURCE if needed.
dnl
dnl	POSIX.1-1990				_POSIX_SOURCE
dnl	POSIX.1-1990 and			_POSIX_SOURCE and
dnl		POSIX.2-1992 C-Language			_POSIX_C_SOURCE=2
dnl		Bindings Option
dnl	POSIX.1b-1993				_POSIX_C_SOURCE=199309L
dnl	POSIX.1c-1996				_POSIX_C_SOURCE=199506L
dnl	X/Open 2000				_POSIX_C_SOURCE=200112L
dnl
dnl Parameters:
dnl	$1 is the nominal value for _POSIX_C_SOURCE
AC_DEFUN([CF_POSIX_C_SOURCE],
[
cf_POSIX_C_SOURCE=ifelse([$1],,199506L,[$1])

cf_save_CFLAGS="$CFLAGS"
cf_save_CPPFLAGS="$CPPFLAGS"

CF_REMOVE_DEFINE(cf_trim_CFLAGS,$cf_save_CFLAGS,_POSIX_C_SOURCE)
CF_REMOVE_DEFINE(cf_trim_CPPFLAGS,$cf_save_CPPFLAGS,_POSIX_C_SOURCE)

AC_CACHE_CHECK(if we should define _POSIX_C_SOURCE,cf_cv_posix_c_source,[
	CF_MSG_LOG(if the symbol is already defined go no further)
	AC_TRY_COMPILE([#include <sys/types.h>],[
#ifndef _POSIX_C_SOURCE
make an error
#endif],
	[cf_cv_posix_c_source=no],
	[cf_want_posix_source=no
	 case .$cf_POSIX_C_SOURCE in #(vi
	 .[[12]]??*) #(vi
		cf_cv_posix_c_source="-D_POSIX_C_SOURCE=$cf_POSIX_C_SOURCE"
		;;
	 .2) #(vi
		cf_cv_posix_c_source="-D_POSIX_C_SOURCE=$cf_POSIX_C_SOURCE"
		cf_want_posix_source=yes
		;;
	 .*)
		cf_want_posix_source=yes
		;;
	 esac
	 if test "$cf_want_posix_source" = yes ; then
		AC_TRY_COMPILE([#include <sys/types.h>],[
#ifdef _POSIX_SOURCE
make an error
#endif],[],
		cf_cv_posix_c_source="$cf_cv_posix_c_source -D_POSIX_SOURCE")
	 fi
	 CF_MSG_LOG(ifdef from value $cf_POSIX_C_SOURCE)
	 CFLAGS="$cf_trim_CFLAGS"
	 CPPFLAGS="$cf_trim_CPPFLAGS $cf_cv_posix_c_source"
	 CF_MSG_LOG(if the second compile does not leave our definition intact error)
	 AC_TRY_COMPILE([#include <sys/types.h>],[
#ifndef _POSIX_C_SOURCE
make an error
#endif],,
	 [cf_cv_posix_c_source=no])
	 CFLAGS="$cf_save_CFLAGS"
	 CPPFLAGS="$cf_save_CPPFLAGS"
	])
])

if test "$cf_cv_posix_c_source" != no ; then
	CFLAGS="$cf_trim_CFLAGS"
	CPPFLAGS="$cf_trim_CPPFLAGS"
	CF_ADD_CFLAGS($cf_cv_posix_c_source)
fi

])dnl
dnl ---------------------------------------------------------------------------
dnl CF_PREDEFINE version: 2 updated: 2010/05/26 16:44:57
dnl ------------
dnl Add definitions to CPPFLAGS to ensure they're predefined for all compiles.
dnl
dnl $1 = symbol to test
dnl $2 = value (if any) to use for a predefinition
AC_DEFUN([CF_PREDEFINE],
[
AC_MSG_CHECKING(if we must define $1)
AC_TRY_COMPILE([#include <sys/types.h>
],[
#ifndef $1
make an error
#endif],[cf_result=no],[cf_result=yes])
AC_MSG_RESULT($cf_result)

if test "$cf_result" = yes ; then
	CPPFLAGS="$CPPFLAGS ifelse([$2],,-D$1,[-D$1=$2])"
elif test "x$2" != "x" ; then
	AC_MSG_CHECKING(checking for compatible value versus $2)
	AC_TRY_COMPILE([#include <sys/types.h>
],[
#if $1-$2 < 0
make an error
#endif],[cf_result=yes],[cf_result=no])
	AC_MSG_RESULT($cf_result)
	if test "$cf_result" = no ; then
		# perhaps we can override it - try...
		CPPFLAGS="$CPPFLAGS -D$1=$2"
	fi
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_PRG_RULES version: 1 updated: 2006/06/03 11:45:08
dnl ------------
dnl Append definitions and rules for the given programs to the subdirectory
dnl Makefiles, and the recursion rule for the top-level Makefile.
dnl
dnl parameters
dnl	$1 = script to run
dnl	$2 = list of subdirectories
dnl
dnl variables
dnl	$AWK
AC_DEFUN([CF_PRG_RULES],
[
for cf_dir in $2
do
	if test ! -d $srcdir/$cf_dir; then
		continue
	elif test -f $srcdir/$cf_dir/programs; then
		$AWK -f $1 $srcdir/$cf_dir/programs >>$cf_dir/Makefile
	fi
done

])dnl
dnl ---------------------------------------------------------------------------
dnl CF_PROG_AR version: 1 updated: 2009/01/01 20:15:22
dnl ----------
dnl Check for archiver "ar".
AC_DEFUN([CF_PROG_AR],[
AC_CHECK_TOOL(AR, ar, ar)
])
dnl ---------------------------------------------------------------------------
dnl CF_PROG_AWK version: 1 updated: 2006/09/16 11:40:59
dnl -----------
dnl Check for awk, ensure that the check found something.
AC_DEFUN([CF_PROG_AWK],
[
AC_PROG_AWK
test -z "$AWK" && AC_MSG_ERROR(No awk program found)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_PROG_CC version: 3 updated: 2012/10/06 15:31:55
dnl ----------
dnl standard check for CC, plus followup sanity checks
dnl $1 = optional parameter to pass to AC_PROG_CC to specify compiler name
AC_DEFUN([CF_PROG_CC],[
ifelse($1,,[AC_PROG_CC],[AC_PROG_CC($1)])
CF_GCC_VERSION
CF_ACVERSION_CHECK(2.52,
	[AC_PROG_CC_STDC],
	[CF_ANSI_CC_REQD])
CF_CC_ENV_FLAGS 
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_PROG_CC_C_O version: 3 updated: 2010/08/14 18:25:37
dnl --------------
dnl Analogous to AC_PROG_CC_C_O, but more useful: tests only $CC, ensures that
dnl the output file can be renamed, and allows for a shell variable that can
dnl be used later.  The parameter is either CC or CXX.  The result is the
dnl cache variable:
dnl	$cf_cv_prog_CC_c_o
dnl	$cf_cv_prog_CXX_c_o
AC_DEFUN([CF_PROG_CC_C_O],
[AC_REQUIRE([AC_PROG_CC])dnl
AC_MSG_CHECKING([whether [$]$1 understands -c and -o together])
AC_CACHE_VAL(cf_cv_prog_$1_c_o,
[
cat > conftest.$ac_ext <<CF_EOF
#include <stdio.h>
int main()
{
	${cf_cv_main_return:-return}(0);
}
CF_EOF
# We do the test twice because some compilers refuse to overwrite an
# existing .o file with -o, though they will create one.
ac_try='[$]$1 -c conftest.$ac_ext -o conftest2.$ac_objext >&AC_FD_CC'
if AC_TRY_EVAL(ac_try) &&
  test -f conftest2.$ac_objext && AC_TRY_EVAL(ac_try);
then
  eval cf_cv_prog_$1_c_o=yes
else
  eval cf_cv_prog_$1_c_o=no
fi
rm -rf conftest*
])dnl
if test $cf_cv_prog_$1_c_o = yes; then
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no])
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_PROG_EGREP version: 1 updated: 2006/09/16 11:40:59
dnl -------------
dnl AC_PROG_EGREP was introduced in autoconf 2.53.
dnl This macro adds a check to ensure the script found something.
AC_DEFUN([CF_PROG_EGREP],
[AC_CACHE_CHECK([for egrep], [ac_cv_prog_egrep],
   [if echo a | (grep -E '(a|b)') >/dev/null 2>&1
    then ac_cv_prog_egrep='grep -E'
    else ac_cv_prog_egrep='egrep'
    fi])
 EGREP=$ac_cv_prog_egrep
 AC_SUBST([EGREP])
test -z "$EGREP" && AC_MSG_ERROR(No egrep program found)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_PROG_GNAT version: 2 updated: 2011/10/22 14:01:47
dnl ------------
dnl Check for gnatmake, ensure that it is complete.
AC_DEFUN([CF_PROG_GNAT],[
cf_ada_make=gnatmake
AC_CHECK_PROG(gnat_exists, $cf_ada_make, yes, no)
if test "$ac_cv_prog_gnat_exists" = no; then
   cf_ada_make=
   cf_cv_prog_gnat_correct=no
else
   CF_GNAT_VERSION
   AC_CHECK_PROG(M4_exists, m4, yes, no)
   if test "$ac_cv_prog_M4_exists" = no; then
      cf_cv_prog_gnat_correct=no
      echo Ada95 binding required program m4 not found. Ada95 binding disabled.
   fi
   if test "$cf_cv_prog_gnat_correct" = yes; then
      AC_MSG_CHECKING(if GNAT works)
      CF_GNAT_TRY_RUN([procedure conftest;],
[with Text_IO;
with GNAT.OS_Lib;
procedure conftest is
begin
   Text_IO.Put ("Hello World");
   Text_IO.New_Line;
   GNAT.OS_Lib.OS_Exit (0);
end conftest;],[cf_cv_prog_gnat_correct=yes],[cf_cv_prog_gnat_correct=no])
      AC_MSG_RESULT($cf_cv_prog_gnat_correct)
   fi
fi

AC_SUBST(cf_ada_make)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_PROG_INSTALL version: 5 updated: 2002/12/21 22:46:07
dnl ---------------
dnl Force $INSTALL to be an absolute-path.  Otherwise, edit_man.sh and the
dnl misc/tabset install won't work properly.  Usually this happens only when
dnl using the fallback mkinstalldirs script
AC_DEFUN([CF_PROG_INSTALL],
[AC_PROG_INSTALL
case $INSTALL in
/*)
  ;;
*)
  CF_DIRNAME(cf_dir,$INSTALL)
  test -z "$cf_dir" && cf_dir=.
  INSTALL=`cd $cf_dir && pwd`/`echo $INSTALL | sed -e 's%^.*/%%'`
  ;;
esac
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_PROG_LDCONFIG version: 3 updated: 2011/06/04 20:09:13
dnl ----------------
dnl Check for ldconfig, needed to fixup shared libraries that would be built
dnl and then used in the install.
AC_DEFUN([CF_PROG_LDCONFIG],[
if test "$cross_compiling" = yes ; then
  LDCONFIG=:
else
case "$cf_cv_system_name" in #(vi
dragonfly*|mirbsd*|freebsd*) #(vi
  test -z "$LDCONFIG" && LDCONFIG="/sbin/ldconfig -R"
  ;;
*) LDPATH=$PATH:/sbin:/usr/sbin
  AC_PATH_PROG(LDCONFIG,ldconfig,,$LDPATH)
  ;;
esac
fi
AC_SUBST(LDCONFIG)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_PROG_LINT version: 2 updated: 2009/08/12 04:43:14
dnl ------------
AC_DEFUN([CF_PROG_LINT],
[
AC_CHECK_PROGS(LINT, tdlint lint alint splint lclint)
AC_SUBST(LINT_OPTS)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_PROG_LN_S version: 2 updated: 2010/08/14 18:25:37
dnl ------------
dnl Combine checks for "ln -s" and "ln -sf", updating $LN_S to include "-f"
dnl option if it is supported.
AC_DEFUN([CF_PROG_LN_S],[
AC_PROG_LN_S
AC_MSG_CHECKING(if $LN_S -f options work)

rm -f conf$$.src conf$$dst
echo >conf$$.dst
echo first >conf$$.src
if $LN_S -f conf$$.src conf$$.dst 2>/dev/null; then
	cf_prog_ln_sf=yes
else
	cf_prog_ln_sf=no
fi
rm -f conf$$.dst conf$$src
AC_MSG_RESULT($cf_prog_ln_sf)

test "$cf_prog_ln_sf" = yes && LN_S="$LN_S -f"
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_REGEX version: 10 updated: 2012/10/04 20:12:20
dnl --------
dnl Attempt to determine if we've got one of the flavors of regular-expression
dnl code that we can support.
AC_DEFUN([CF_REGEX],
[

cf_regex_func=no

cf_regex_libs="regex re"
case $host_os in #(vi
mingw*)
	cf_regex_libs="gnurx $cf_regex_libs"
	;;
esac

AC_CHECK_FUNC(regcomp,[cf_regex_func=regcomp],[
	for cf_regex_lib in $cf_regex_libs
	do
		AC_CHECK_LIB($cf_regex_lib,regcomp,[
				CF_ADD_LIB($cf_regex_lib)
				cf_regex_func=regcomp
				break])
	done
])

if test "$cf_regex_func" = no ; then
	AC_CHECK_FUNC(compile,[cf_regex_func=compile],[
		AC_CHECK_LIB(gen,compile,[
				CF_ADD_LIB(gen)
				cf_regex_func=compile])])
fi

if test "$cf_regex_func" = no ; then
	AC_MSG_WARN(cannot find regular expression library)
fi

AC_CACHE_CHECK(for regular-expression headers,cf_cv_regex_hdrs,[

cf_cv_regex_hdrs=no
case $cf_regex_func in #(vi
compile) #(vi
	for cf_regex_hdr in regexp.h regexpr.h
	do
		AC_TRY_LINK([#include <$cf_regex_hdr>],[
			char *p = compile("", "", "", 0);
			int x = step("", "");
		],[
			cf_cv_regex_hdrs=$cf_regex_hdr
			break
		])
	done
	;;
*)
	for cf_regex_hdr in regex.h
	do
		AC_TRY_LINK([#include <sys/types.h>
#include <$cf_regex_hdr>],[
			regex_t *p;
			int x = regcomp(p, "", 0);
			int y = regexec(p, "", 0, 0, 0);
			regfree(p);
		],[
			cf_cv_regex_hdrs=$cf_regex_hdr
			break
		])
	done
	;;
esac

])

case $cf_cv_regex_hdrs in #(vi
    no)	       AC_MSG_WARN(no regular expression header found) ;; #(vi
    regex.h)   AC_DEFINE(HAVE_REGEX_H_FUNCS,1,[Define to 1 to include regex.h for regular expressions]) ;; #(vi
    regexp.h)  AC_DEFINE(HAVE_REGEXP_H_FUNCS,1,[Define to 1 to include regexp.h for regular expressions]) ;; #(vi
    regexpr.h) AC_DEFINE(HAVE_REGEXPR_H_FUNCS,1,[Define to 1 to include regexpr.h for regular expressions]) ;;
esac
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_REMOVE_DEFINE version: 3 updated: 2010/01/09 11:05:50
dnl ----------------
dnl Remove all -U and -D options that refer to the given symbol from a list
dnl of C compiler options.  This works around the problem that not all
dnl compilers process -U and -D options from left-to-right, so a -U option
dnl cannot be used to cancel the effect of a preceding -D option.
dnl
dnl $1 = target (which could be the same as the source variable)
dnl $2 = source (including '$')
dnl $3 = symbol to remove
define([CF_REMOVE_DEFINE],
[
$1=`echo "$2" | \
	sed	-e 's/-[[UD]]'"$3"'\(=[[^ 	]]*\)\?[[ 	]]/ /g' \
		-e 's/-[[UD]]'"$3"'\(=[[^ 	]]*\)\?[$]//g'`
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_REMOVE_LIB version: 1 updated: 2007/02/17 14:11:52
dnl -------------
dnl Remove the given library from the symbol
dnl
dnl $1 = target (which could be the same as the source variable)
dnl $2 = source (including '$')
dnl $3 = library to remove
define([CF_REMOVE_LIB],
[
# remove $3 library from $2
$1=`echo "$2" | sed -e 's/-l$3[[ 	]]//g' -e 's/-l$3[$]//'`
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_RPATH_HACK version: 11 updated: 2013/09/01 13:02:00
dnl -------------
AC_DEFUN([CF_RPATH_HACK],
[
AC_REQUIRE([CF_LD_RPATH_OPT])
AC_MSG_CHECKING(for updated LDFLAGS)
if test -n "$LD_RPATH_OPT" ; then
	AC_MSG_RESULT(maybe)

	AC_CHECK_PROGS(cf_ldd_prog,ldd,no)
	cf_rpath_list="/usr/lib /lib"
	if test "$cf_ldd_prog" != no
	then
		cf_rpath_oops=

AC_TRY_LINK([#include <stdio.h>],
		[printf("Hello");],
		[cf_rpath_oops=`$cf_ldd_prog conftest$ac_exeext | fgrep ' not found' | sed -e 's% =>.*$%%' |sort | uniq`
		 cf_rpath_list=`$cf_ldd_prog conftest$ac_exeext | fgrep / | sed -e 's%^.*[[ 	]]/%/%' -e 's%/[[^/]][[^/]]*$%%' |sort | uniq`])

		# If we passed the link-test, but get a "not found" on a given library,
		# this could be due to inept reconfiguration of gcc to make it only
		# partly honor /usr/local/lib (or whatever).  Sometimes this behavior
		# is intentional, e.g., installing gcc in /usr/bin and suppressing the
		# /usr/local libraries.
		if test -n "$cf_rpath_oops"
		then
			for cf_rpath_src in $cf_rpath_oops
			do
				for cf_rpath_dir in \
					/usr/local \
					/usr/pkg \
					/opt/sfw
				do
					if test -f $cf_rpath_dir/lib/$cf_rpath_src
					then
						CF_VERBOSE(...adding -L$cf_rpath_dir/lib to LDFLAGS for $cf_rpath_src)
						LDFLAGS="$LDFLAGS -L$cf_rpath_dir/lib"
						break
					fi
				done
			done
		fi
	fi

	CF_VERBOSE(...checking EXTRA_LDFLAGS $EXTRA_LDFLAGS)

	CF_RPATH_HACK_2(LDFLAGS)
	CF_RPATH_HACK_2(LIBS)

	CF_VERBOSE(...checked EXTRA_LDFLAGS $EXTRA_LDFLAGS)
else
	AC_MSG_RESULT(no)
fi
AC_SUBST(EXTRA_LDFLAGS)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_RPATH_HACK_2 version: 6 updated: 2010/04/17 16:31:24
dnl ---------------
dnl Do one set of substitutions for CF_RPATH_HACK, adding an rpath option to
dnl EXTRA_LDFLAGS for each -L option found.
dnl
dnl $cf_rpath_list contains a list of directories to ignore.
dnl
dnl $1 = variable name to update.  The LDFLAGS variable should be the only one,
dnl      but LIBS often has misplaced -L options.
AC_DEFUN([CF_RPATH_HACK_2],
[
CF_VERBOSE(...checking $1 [$]$1)

cf_rpath_dst=
for cf_rpath_src in [$]$1
do
	case $cf_rpath_src in #(vi
	-L*) #(vi

		# check if this refers to a directory which we will ignore
		cf_rpath_skip=no
		if test -n "$cf_rpath_list"
		then
			for cf_rpath_item in $cf_rpath_list
			do
				if test "x$cf_rpath_src" = "x-L$cf_rpath_item"
				then
					cf_rpath_skip=yes
					break
				fi
			done
		fi

		if test "$cf_rpath_skip" = no
		then
			# transform the option
			if test "$LD_RPATH_OPT" = "-R " ; then
				cf_rpath_tmp=`echo "$cf_rpath_src" |sed -e "s%-L%-R %"`
			else
				cf_rpath_tmp=`echo "$cf_rpath_src" |sed -e "s%-L%$LD_RPATH_OPT%"`
			fi

			# if we have not already added this, add it now
			cf_rpath_tst=`echo "$EXTRA_LDFLAGS" | sed -e "s%$cf_rpath_tmp %%"`
			if test "x$cf_rpath_tst" = "x$EXTRA_LDFLAGS"
			then
				CF_VERBOSE(...Filter $cf_rpath_src ->$cf_rpath_tmp)
				EXTRA_LDFLAGS="$cf_rpath_tmp $EXTRA_LDFLAGS"
			fi
		fi
		;;
	esac
	cf_rpath_dst="$cf_rpath_dst $cf_rpath_src"
done
$1=$cf_rpath_dst

CF_VERBOSE(...checked $1 [$]$1)
AC_SUBST(EXTRA_LDFLAGS)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_SHARED_OPTS version: 84 updated: 2013/11/03 06:26:10
dnl --------------
dnl --------------
dnl Attempt to determine the appropriate CC/LD options for creating a shared
dnl library.
dnl
dnl Notes:
dnl a) ${LOCAL_LDFLAGS} is used to link executables that will run within
dnl the build-tree, i.e., by making use of the libraries that are compiled in
dnl $rel_builddir/lib We avoid compiling-in a $rel_builddir/lib path for the
dnl shared library since that can lead to unexpected results at runtime.
dnl b) ${LOCAL_LDFLAGS2} has the same intention but assumes that the shared
dnl libraries are compiled in ../../lib
dnl
dnl The variable 'cf_cv_do_symlinks' is used to control whether we configure
dnl to install symbolic links to the rel/abi versions of shared libraries.
dnl
dnl The variable 'cf_cv_shlib_version' controls whether we use the rel or abi
dnl version when making symbolic links.
dnl
dnl The variable 'cf_cv_shlib_version_infix' controls whether shared library
dnl version numbers are infix (ex: libncurses.<ver>.dylib) or postfix
dnl (ex: libncurses.so.<ver>).
dnl
dnl Some loaders leave 'so_locations' lying around.  It's nice to clean up.
AC_DEFUN([CF_SHARED_OPTS],
[
	AC_REQUIRE([CF_LD_RPATH_OPT])
	RM_SHARED_OPTS=
	LOCAL_LDFLAGS=
	LOCAL_LDFLAGS2=
	LD_SHARED_OPTS=
	INSTALL_LIB="-m 644"
	: ${rel_builddir:=.}

	shlibdir=$libdir
	AC_SUBST(shlibdir)

	MAKE_DLLS="#"
	AC_SUBST(MAKE_DLLS)

	cf_cv_do_symlinks=no
	cf_ld_rpath_opt=
	test "$cf_cv_enable_rpath" = yes && cf_ld_rpath_opt="$LD_RPATH_OPT"

	AC_MSG_CHECKING(if release/abi version should be used for shared libs)
	AC_ARG_WITH(shlib-version,
	[  --with-shlib-version=X  Specify rel or abi version for shared libs],
	[test -z "$withval" && withval=auto
	case $withval in #(vi
	yes) #(vi
		cf_cv_shlib_version=auto
		;;
	rel|abi|auto|no) #(vi
		cf_cv_shlib_version=$withval
		;;
	*)
		AC_MSG_ERROR([option value must be one of: rel, abi, auto or no])
		;;
	esac
	],[cf_cv_shlib_version=auto])
	AC_MSG_RESULT($cf_cv_shlib_version)

	cf_cv_rm_so_locs=no
	cf_try_cflags=

	# Some less-capable ports of gcc support only -fpic
	CC_SHARED_OPTS=
	if test "$GCC" = yes
	then
		AC_MSG_CHECKING(which $CC option to use)
		cf_save_CFLAGS="$CFLAGS"
		for CC_SHARED_OPTS in -fPIC -fpic ''
		do
			CFLAGS="$cf_save_CFLAGS $CC_SHARED_OPTS"
			AC_TRY_COMPILE([#include <stdio.h>],[int x = 1],[break],[])
		done
		AC_MSG_RESULT($CC_SHARED_OPTS)
		CFLAGS="$cf_save_CFLAGS"
	fi

	cf_cv_shlib_version_infix=no

	case $cf_cv_system_name in #(vi
	aix4.[3-9]*|aix[[5-7]]*) #(vi
		if test "$GCC" = yes; then
			CC_SHARED_OPTS=
			MK_SHARED_LIB='${CC} -shared -Wl,-brtl -Wl,-blibpath:${RPATH_LIST}:/usr/lib -o [$]@'
		else
			# CC_SHARED_OPTS='-qpic=large -G'
			# perhaps "-bM:SRE -bnoentry -bexpall"
			MK_SHARED_LIB='${CC} -G -Wl,-brtl -Wl,-blibpath:${RPATH_LIST}:/usr/lib -o [$]@'
		fi
		;;
	beos*) #(vi
		MK_SHARED_LIB='${CC} ${CFLAGS} -o $[@] -Xlinker -soname=`basename $[@]` -nostart -e 0'
		;;
	cygwin*) #(vi
		CC_SHARED_OPTS=
		MK_SHARED_LIB='sh '$rel_builddir'/mk_shared_lib.sh [$]@ [$]{CC} [$]{CFLAGS}'
		RM_SHARED_OPTS="$RM_SHARED_OPTS $rel_builddir/mk_shared_lib.sh *.dll.a"
		cf_cv_shlib_version=cygdll
		cf_cv_shlib_version_infix=cygdll
		shlibdir=$bindir
		MAKE_DLLS=
		cat >mk_shared_lib.sh <<-CF_EOF
		#!/bin/sh
		SHARED_LIB=\[$]1
		IMPORT_LIB=\`echo "\[$]1" | sed -e 's/cyg/lib/' -e 's/[[0-9]]*\.dll[$]/.dll.a/'\`
		shift
		cat <<-EOF
		Linking shared library
		** SHARED_LIB \[$]SHARED_LIB
		** IMPORT_LIB \[$]IMPORT_LIB
EOF
		exec \[$]* -shared -Wl,--out-implib=\[$]{IMPORT_LIB} -Wl,--export-all-symbols -o \[$]{SHARED_LIB}
CF_EOF
		chmod +x mk_shared_lib.sh
		;;
	msys*) #(vi
		CC_SHARED_OPTS=
		MK_SHARED_LIB='sh '$rel_builddir'/mk_shared_lib.sh [$]@ [$]{CC} [$]{CFLAGS}'
		RM_SHARED_OPTS="$RM_SHARED_OPTS $rel_builddir/mk_shared_lib.sh *.dll.a"
		cf_cv_shlib_version=msysdll
		cf_cv_shlib_version_infix=msysdll
		shlibdir=$bindir
		MAKE_DLLS=
		cat >mk_shared_lib.sh <<-CF_EOF
		#!/bin/sh
		SHARED_LIB=\[$]1
		IMPORT_LIB=\`echo "\[$]1" | sed -e 's/msys-/lib/' -e 's/[[0-9]]*\.dll[$]/.dll.a/'\`
		shift
		cat <<-EOF
		Linking shared library
		** SHARED_LIB \[$]SHARED_LIB
		** IMPORT_LIB \[$]IMPORT_LIB
EOF
		exec \[$]* -shared -Wl,--out-implib=\[$]{IMPORT_LIB} -Wl,--export-all-symbols -o \[$]{SHARED_LIB}
CF_EOF
		chmod +x mk_shared_lib.sh
		;;
	darwin*) #(vi
		cf_try_cflags="no-cpp-precomp"
		CC_SHARED_OPTS="-dynamic"
		MK_SHARED_LIB='${CC} ${CFLAGS} -dynamiclib -install_name ${libdir}/`basename $[@]` -compatibility_version ${ABI_VERSION} -current_version ${ABI_VERSION} -o $[@]'
		test "$cf_cv_shlib_version" = auto && cf_cv_shlib_version=abi
		cf_cv_shlib_version_infix=yes
		AC_CACHE_CHECK([if ld -search_paths_first works], cf_cv_ldflags_search_paths_first, [
			cf_save_LDFLAGS=$LDFLAGS
			LDFLAGS="$LDFLAGS -Wl,-search_paths_first"
			AC_TRY_LINK(, [int i;], cf_cv_ldflags_search_paths_first=yes, cf_cv_ldflags_search_paths_first=no)
				LDFLAGS=$cf_save_LDFLAGS])
		if test $cf_cv_ldflags_search_paths_first = yes; then
			LDFLAGS="$LDFLAGS -Wl,-search_paths_first"
		fi
		;;
	hpux[[7-8]]*) #(vi
		# HP-UX 8.07 ld lacks "+b" option used for libdir search-list 
		if test "$GCC" != yes; then
			CC_SHARED_OPTS='+Z'
		fi
		MK_SHARED_LIB='${LD} -b -o $[@]'
		INSTALL_LIB="-m 555"
		;;
	hpux*) #(vi
		# (tested with gcc 2.7.2 -- I don't have c89)
		if test "$GCC" = yes; then
			LD_SHARED_OPTS='-Xlinker +b -Xlinker ${libdir}'
		else
			CC_SHARED_OPTS='+Z'
			LD_SHARED_OPTS='-Wl,+b,${libdir}'
		fi
		MK_SHARED_LIB='${LD} +b ${libdir} -b -o $[@]'
		# HP-UX shared libraries must be executable, and should be
		# readonly to exploit a quirk in the memory manager.
		INSTALL_LIB="-m 555"
		;;
	interix*)
		test "$cf_cv_shlib_version" = auto && cf_cv_shlib_version=rel
		if test "$cf_cv_shlib_version" = rel; then
			cf_shared_soname='`basename $@ .${REL_VERSION}`.${ABI_VERSION}'
		else
			cf_shared_soname='`basename $@`'
		fi
		CC_SHARED_OPTS=
		MK_SHARED_LIB='${CC} -shared -Wl,-rpath,${RPATH_LIST} -Wl,-h,'$cf_shared_soname' -o $@'
		;;
	irix*) #(vi
		if test "$cf_cv_enable_rpath" = yes ; then
			EXTRA_LDFLAGS="${cf_ld_rpath_opt}\${RPATH_LIST} $EXTRA_LDFLAGS"
		fi
		# tested with IRIX 5.2 and 'cc'.
		if test "$GCC" != yes; then
			CC_SHARED_OPTS='-KPIC'
			MK_SHARED_LIB='${CC} -shared -rdata_shared -soname `basename $[@]` -o $[@]'
		else
			MK_SHARED_LIB='${CC} -shared -Wl,-soname,`basename $[@]` -o $[@]'
		fi
		cf_cv_rm_so_locs=yes
		;;
	linux*|gnu*|k*bsd*-gnu) #(vi
		if test "$DFT_LWR_MODEL" = "shared" ; then
			LOCAL_LDFLAGS="${LD_RPATH_OPT}\$(LOCAL_LIBDIR)"
			LOCAL_LDFLAGS2="$LOCAL_LDFLAGS"
		fi
		if test "$cf_cv_enable_rpath" = yes ; then
			EXTRA_LDFLAGS="${cf_ld_rpath_opt}\${RPATH_LIST} $EXTRA_LDFLAGS"
		fi
		CF_SHARED_SONAME
		MK_SHARED_LIB='${CC} ${CFLAGS} -shared -Wl,-soname,'$cf_cv_shared_soname',-stats,-lc -o $[@]'
		;;
	mingw*) #(vi
		cf_cv_shlib_version=mingw
		cf_cv_shlib_version_infix=mingw
		shlibdir=$bindir
		MAKE_DLLS=
		if test "$DFT_LWR_MODEL" = "shared" ; then
			LOCAL_LDFLAGS="-Wl,--enable-auto-import"
			LOCAL_LDFLAGS2="$LOCAL_LDFLAGS"
			EXTRA_LDFLAGS="-Wl,--enable-auto-import $EXTRA_LDFLAGS"
		fi
		CC_SHARED_OPTS=
		MK_SHARED_LIB='sh '$rel_builddir'/mk_shared_lib.sh [$]@ [$]{CC} [$]{CFLAGS}'
		RM_SHARED_OPTS="$RM_SHARED_OPTS $rel_builddir/mk_shared_lib.sh *.dll.a"
		cat >mk_shared_lib.sh <<-CF_EOF
		#!/bin/sh
		SHARED_LIB=\[$]1
		IMPORT_LIB=\`echo "\[$]1" | sed -e 's/[[0-9]]*\.dll[$]/.dll.a/'\`
		shift
		cat <<-EOF
		Linking shared library
		** SHARED_LIB \[$]SHARED_LIB
		** IMPORT_LIB \[$]IMPORT_LIB
EOF
		exec \[$]* -shared -Wl,--enable-auto-import,--out-implib=\[$]{IMPORT_LIB} -Wl,--export-all-symbols -o \[$]{SHARED_LIB}
CF_EOF
		chmod +x mk_shared_lib.sh
		;;
	openbsd[[2-9]].*|mirbsd*) #(vi
		if test "$DFT_LWR_MODEL" = "shared" ; then
			LOCAL_LDFLAGS="${LD_RPATH_OPT}\$(LOCAL_LIBDIR)"
			LOCAL_LDFLAGS2="$LOCAL_LDFLAGS"
		fi
		if test "$cf_cv_enable_rpath" = yes ; then
			EXTRA_LDFLAGS="${cf_ld_rpath_opt}\${RPATH_LIST} $EXTRA_LDFLAGS"
		fi
		CC_SHARED_OPTS="$CC_SHARED_OPTS -DPIC"
		CF_SHARED_SONAME
		MK_SHARED_LIB='${CC} ${CFLAGS} -shared -Wl,-Bshareable,-soname,'$cf_cv_shared_soname',-stats,-lc -o $[@]'
		;;
	nto-qnx*|openbsd*|freebsd[[12]].*) #(vi
		CC_SHARED_OPTS="$CC_SHARED_OPTS -DPIC"
		MK_SHARED_LIB='${LD} -Bshareable -o $[@]'
		test "$cf_cv_shlib_version" = auto && cf_cv_shlib_version=rel
		;;
	dragonfly*|freebsd*) #(vi
		CC_SHARED_OPTS="$CC_SHARED_OPTS -DPIC"
		if test "$DFT_LWR_MODEL" = "shared" && test "$cf_cv_enable_rpath" = yes ; then
			LOCAL_LDFLAGS="${cf_ld_rpath_opt}\$(LOCAL_LIBDIR)"
			LOCAL_LDFLAGS2="${cf_ld_rpath_opt}\${RPATH_LIST} $LOCAL_LDFLAGS"
			EXTRA_LDFLAGS="${cf_ld_rpath_opt}\${RPATH_LIST} $EXTRA_LDFLAGS"
		fi
		CF_SHARED_SONAME
		MK_SHARED_LIB='${LD} -shared -Bshareable -soname=`basename $[@]` -o $[@]'
		;;
	netbsd*) #(vi
		CC_SHARED_OPTS="$CC_SHARED_OPTS -DPIC"
		if test "$DFT_LWR_MODEL" = "shared" && test "$cf_cv_enable_rpath" = yes ; then
			LOCAL_LDFLAGS="${cf_ld_rpath_opt}\$(LOCAL_LIBDIR)"
			LOCAL_LDFLAGS2="$LOCAL_LDFLAGS"
			EXTRA_LDFLAGS="${cf_ld_rpath_opt}\${RPATH_LIST} $EXTRA_LDFLAGS"
			if test "$cf_cv_shlib_version" = auto; then
			if test -f /usr/libexec/ld.elf_so; then
				cf_cv_shlib_version=abi
			else
				cf_cv_shlib_version=rel
			fi
			fi
			CF_SHARED_SONAME
			MK_SHARED_LIB='${CC} ${CFLAGS} -shared -Wl,-soname,'$cf_cv_shared_soname' -o $[@]'
		else
			MK_SHARED_LIB='${CC} -Wl,-shared -Wl,-Bshareable -o $[@]'
		fi
		;;
	osf*|mls+*) #(vi
		# tested with OSF/1 V3.2 and 'cc'
		# tested with OSF/1 V3.2 and gcc 2.6.3 (but the c++ demo didn't
		# link with shared libs).
		MK_SHARED_LIB='${LD} -set_version ${REL_VERSION}:${ABI_VERSION} -expect_unresolved "*" -shared -soname `basename $[@]`'
		case $host_os in #(vi
		osf4*)
			MK_SHARED_LIB="${MK_SHARED_LIB} -msym"
			;;
		esac
		MK_SHARED_LIB="${MK_SHARED_LIB}"' -o $[@]'
		if test "$DFT_LWR_MODEL" = "shared" ; then
			LOCAL_LDFLAGS="${LD_RPATH_OPT}\$(LOCAL_LIBDIR)"
			LOCAL_LDFLAGS2="$LOCAL_LDFLAGS"
		fi
		cf_cv_rm_so_locs=yes
		;;
	sco3.2v5*)  # (also uw2* and UW7: hops 13-Apr-98
		# tested with osr5.0.5
		if test "$GCC" != yes; then
			CC_SHARED_OPTS='-belf -KPIC'
		fi
		MK_SHARED_LIB='${LD} -dy -G -h `basename $[@] .${REL_VERSION}`.${ABI_VERSION} -o [$]@'
		if test "$cf_cv_enable_rpath" = yes ; then
			# only way is to set LD_RUN_PATH but no switch for it
			RUN_PATH=$libdir
		fi
		test "$cf_cv_shlib_version" = auto && cf_cv_shlib_version=rel
		LINK_PROGS='LD_RUN_PATH=${libdir}'
		LINK_TESTS='Pwd=`pwd`;LD_RUN_PATH=`dirname $${Pwd}`/lib'
		;;
	sunos4*) #(vi
		# tested with SunOS 4.1.1 and gcc 2.7.0
		if test "$GCC" != yes; then
			CC_SHARED_OPTS='-KPIC'
		fi
		MK_SHARED_LIB='${LD} -assert pure-text -o $[@]'
		test "$cf_cv_shlib_version" = auto && cf_cv_shlib_version=rel
		;;
	solaris2*) #(vi
		# tested with SunOS 5.5.1 (solaris 2.5.1) and gcc 2.7.2
		# tested with SunOS 5.10 (solaris 10) and gcc 3.4.3
		if test "$DFT_LWR_MODEL" = "shared" ; then
			LOCAL_LDFLAGS="-R \$(LOCAL_LIBDIR):\${libdir}"
			LOCAL_LDFLAGS2="$LOCAL_LDFLAGS"
		fi
		if test "$cf_cv_enable_rpath" = yes ; then
			EXTRA_LDFLAGS="-R \${libdir} $EXTRA_LDFLAGS"
		fi
		CF_SHARED_SONAME
		if test "$GCC" != yes; then
			cf_save_CFLAGS="$CFLAGS"
			for cf_shared_opts in -xcode=pic32 -xcode=pic13 -KPIC -Kpic -O
			do
				CFLAGS="$cf_shared_opts $cf_save_CFLAGS"
				AC_TRY_COMPILE([#include <stdio.h>],[printf("Hello\n");],[break])
			done
			CFLAGS="$cf_save_CFLAGS"
			CC_SHARED_OPTS=$cf_shared_opts
			MK_SHARED_LIB='${CC} -dy -G -h '$cf_cv_shared_soname' -o $[@]'
		else
			MK_SHARED_LIB='${CC} -shared -dy -G -h '$cf_cv_shared_soname' -o $[@]'
		fi
		;;
	sysv5uw7*|unix_sv*) #(vi
		# tested with UnixWare 7.1.0 (gcc 2.95.2 and cc)
		if test "$GCC" != yes; then
			CC_SHARED_OPTS='-KPIC'
		fi
		MK_SHARED_LIB='${LD} -d y -G -o [$]@'
		;;
	*)
		CC_SHARED_OPTS='unknown'
		MK_SHARED_LIB='echo unknown'
		;;
	esac

	# This works if the last tokens in $MK_SHARED_LIB are the -o target.
	case "$cf_cv_shlib_version" in #(vi
	rel|abi)
		case "$MK_SHARED_LIB" in #(vi
		*'-o $[@]') #(vi
			test "$cf_cv_do_symlinks" = no && cf_cv_do_symlinks=yes
			;;
		*)
			AC_MSG_WARN(ignored --with-shlib-version)
			;;
		esac
		;;
	esac

	if test -n "$cf_try_cflags"
	then
cat > conftest.$ac_ext <<EOF
#line __oline__ "${as_me:-configure}"
#include <stdio.h>
int main(int argc, char *argv[[]])
{
	printf("hello\n");
	return (argv[[argc-1]] == 0) ;
}
EOF
		cf_save_CFLAGS="$CFLAGS"
		for cf_opt in $cf_try_cflags
		do
			CFLAGS="$cf_save_CFLAGS -$cf_opt"
			AC_MSG_CHECKING(if CFLAGS option -$cf_opt works)
			if AC_TRY_EVAL(ac_compile); then
				AC_MSG_RESULT(yes)
				cf_save_CFLAGS="$CFLAGS"
			else
				AC_MSG_RESULT(no)
			fi
		done
		CFLAGS="$cf_save_CFLAGS"
	fi


	# RPATH_LIST is a colon-separated list of directories
	test -n "$cf_ld_rpath_opt" && MK_SHARED_LIB="$MK_SHARED_LIB $cf_ld_rpath_opt\${RPATH_LIST}"
	test -z "$RPATH_LIST" && RPATH_LIST="\${libdir}"

	test $cf_cv_rm_so_locs = yes && RM_SHARED_OPTS="$RM_SHARED_OPTS so_locations"

	CF_VERBOSE(CC_SHARED_OPTS: $CC_SHARED_OPTS)
	CF_VERBOSE(MK_SHARED_LIB:  $MK_SHARED_LIB)

	AC_SUBST(CC_SHARED_OPTS)
	AC_SUBST(LD_RPATH_OPT)
	AC_SUBST(LD_SHARED_OPTS)
	AC_SUBST(MK_SHARED_LIB)
	AC_SUBST(RM_SHARED_OPTS)

	AC_SUBST(LINK_PROGS)
	AC_SUBST(LINK_TESTS)

	AC_SUBST(EXTRA_LDFLAGS)
	AC_SUBST(LOCAL_LDFLAGS)
	AC_SUBST(LOCAL_LDFLAGS2)

	AC_SUBST(INSTALL_LIB)
	AC_SUBST(RPATH_LIST)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_SHARED_SONAME version: 3 updated: 2008/09/08 18:34:43
dnl ----------------
dnl utility macro for CF_SHARED_OPTS, constructs "$cf_cv_shared_soname" for
dnl substitution into MK_SHARED_LIB string for the "-soname" (or similar)
dnl option.
dnl
dnl $1 is the default that should be used for "$cf_cv_shlib_version".
dnl If missing, use "rel".
define([CF_SHARED_SONAME],
[
	test "$cf_cv_shlib_version" = auto && cf_cv_shlib_version=ifelse($1,,rel,$1)
	if test "$cf_cv_shlib_version" = rel; then
		cf_cv_shared_soname='`basename $[@] .${REL_VERSION}`.${ABI_VERSION}'
	else
		cf_cv_shared_soname='`basename $[@]`'
	fi
])
dnl ---------------------------------------------------------------------------
dnl CF_SIGWINCH version: 1 updated: 2006/04/02 16:41:09
dnl -----------
dnl Use this macro after CF_XOPEN_SOURCE, but do not require it (not all
dnl programs need this test).
dnl
dnl This is really a MacOS X 10.4.3 workaround.  Defining _POSIX_C_SOURCE
dnl forces SIGWINCH to be undefined (breaks xterm, ncurses).  Oddly, the struct
dnl winsize declaration is left alone - we may revisit this if Apple choose to
dnl break that part of the interface as well.
AC_DEFUN([CF_SIGWINCH],
[
AC_CACHE_CHECK(if SIGWINCH is defined,cf_cv_define_sigwinch,[
	AC_TRY_COMPILE([
#include <sys/types.h>
#include <sys/signal.h>
],[int x = SIGWINCH],
	[cf_cv_define_sigwinch=yes],
	[AC_TRY_COMPILE([
#undef _XOPEN_SOURCE
#undef _POSIX_SOURCE
#undef _POSIX_C_SOURCE
#include <sys/types.h>
#include <sys/signal.h>
],[int x = SIGWINCH],
	[cf_cv_define_sigwinch=maybe],
	[cf_cv_define_sigwinch=no])
])
])

if test "$cf_cv_define_sigwinch" = maybe ; then
AC_CACHE_CHECK(for actual SIGWINCH definition,cf_cv_fixup_sigwinch,[
cf_cv_fixup_sigwinch=unknown
cf_sigwinch=32
while test $cf_sigwinch != 1
do
	AC_TRY_COMPILE([
#undef _XOPEN_SOURCE
#undef _POSIX_SOURCE
#undef _POSIX_C_SOURCE
#include <sys/types.h>
#include <sys/signal.h>
],[
#if SIGWINCH != $cf_sigwinch
make an error
#endif
int x = SIGWINCH],
	[cf_cv_fixup_sigwinch=$cf_sigwinch
	 break])

cf_sigwinch=`expr $cf_sigwinch - 1`
done
])

	if test "$cf_cv_fixup_sigwinch" != unknown ; then
		CPPFLAGS="$CPPFLAGS -DSIGWINCH=$cf_cv_fixup_sigwinch"
	fi
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_SIG_ATOMIC_T version: 3 updated: 2012/10/04 20:12:20
dnl ---------------
dnl signal handler, but there are some gcc depedencies in that recommendation.
dnl Try anyway.
AC_DEFUN([CF_SIG_ATOMIC_T],
[
AC_MSG_CHECKING(for signal global datatype)
AC_CACHE_VAL(cf_cv_sig_atomic_t,[
	for cf_type in \
		"volatile sig_atomic_t" \
		"sig_atomic_t" \
		"int"
	do
	AC_TRY_COMPILE([
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>

extern $cf_type x;
$cf_type x;
static void handler(int sig)
{
	x = 5;
}],
		[signal(SIGINT, handler);
		 x = 1],
		[cf_cv_sig_atomic_t=$cf_type],
		[cf_cv_sig_atomic_t=no])
		test "$cf_cv_sig_atomic_t" != no && break
	done
	])
AC_MSG_RESULT($cf_cv_sig_atomic_t)
test "$cf_cv_sig_atomic_t" != no && AC_DEFINE_UNQUOTED(SIG_ATOMIC_T, $cf_cv_sig_atomic_t,[Define to signal global datatype])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_SIZECHANGE version: 9 updated: 2012/10/06 11:17:15
dnl -------------
dnl Check for definitions & structures needed for window size-changing
dnl FIXME: check that this works with "snake" (HP-UX 10.x)
AC_DEFUN([CF_SIZECHANGE],
[
AC_REQUIRE([CF_STRUCT_TERMIOS])
AC_CACHE_CHECK(declaration of size-change, cf_cv_sizechange,[
    cf_cv_sizechange=unknown
    cf_save_CPPFLAGS="$CPPFLAGS"

for cf_opts in "" "NEED_PTEM_H"
do

    CPPFLAGS="$cf_save_CPPFLAGS"
    test -n "$cf_opts" && CPPFLAGS="$CPPFLAGS -D$cf_opts"
    AC_TRY_COMPILE([#include <sys/types.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#else
#ifdef HAVE_TERMIO_H
#include <termio.h>
#endif
#endif
#ifdef NEED_PTEM_H
/* This is a workaround for SCO:  they neglected to define struct winsize in
 * termios.h -- it's only in termio.h and ptem.h
 */
#include        <sys/stream.h>
#include        <sys/ptem.h>
#endif
#if !defined(sun) || !defined(HAVE_TERMIOS_H)
#include <sys/ioctl.h>
#endif
],[
#ifdef TIOCGSIZE
	struct ttysize win;	/* FIXME: what system is this? */
	int y = win.ts_lines;
	int x = win.ts_cols;
#else
#ifdef TIOCGWINSZ
	struct winsize win;
	int y = win.ws_row;
	int x = win.ws_col;
#else
	no TIOCGSIZE or TIOCGWINSZ
#endif /* TIOCGWINSZ */
#endif /* TIOCGSIZE */
	],
	[cf_cv_sizechange=yes],
	[cf_cv_sizechange=no])

	CPPFLAGS="$cf_save_CPPFLAGS"
	if test "$cf_cv_sizechange" = yes ; then
		echo "size-change succeeded ($cf_opts)" >&AC_FD_CC
		test -n "$cf_opts" && cf_cv_sizechange="$cf_opts"
		break
	fi
done
])
if test "$cf_cv_sizechange" != no ; then
	AC_DEFINE(HAVE_SIZECHANGE,1,[Define to 1 if sizechar declarations are provided])
	case $cf_cv_sizechange in #(vi
	NEED*)
		AC_DEFINE_UNQUOTED($cf_cv_sizechange )
		;;
	esac
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_SRC_MODULES version: 27 updated: 2013/08/03 18:18:08
dnl --------------
dnl For each parameter, test if the source-directory exists, and if it contains
dnl a 'modules' file.  If so, add to the list $cf_cv_src_modules which we'll
dnl use in CF_LIB_RULES.
dnl
dnl This uses the configured value to make the lists SRC_SUBDIRS and
dnl SUB_MAKEFILES which are used in the makefile-generation scheme.
AC_DEFUN([CF_SRC_MODULES],
[
AC_MSG_CHECKING(for src modules)

# dependencies and linker-arguments for test-programs
TEST_DEPS="${LIB_DIR}/${LIB_PREFIX}${LIB_NAME}${DFT_DEP_SUFFIX} $TEST_DEPS"
TEST_DEP2="${LIB_2ND}/${LIB_PREFIX}${LIB_NAME}${DFT_DEP_SUFFIX} $TEST_DEP2"
if test "$DFT_LWR_MODEL" = "libtool"; then
	TEST_ARGS="${TEST_DEPS}"
	TEST_ARG2="${TEST_DEP2}"
else
	TEST_ARGS="-l${LIB_NAME}${DFT_ARG_SUFFIX} $TEST_ARGS"
	TEST_ARG2="-l${LIB_NAME}${DFT_ARG_SUFFIX} $TEST_ARG2"
fi

PC_MODULES_TO_MAKE="ncurses${DFT_ARG_SUFFIX}"
cf_cv_src_modules=
for cf_dir in $1
do
	if test -f $srcdir/$cf_dir/modules; then

		# We may/may not have tack in the distribution, though the
		# makefile is.
		if test $cf_dir = tack ; then
			if test ! -f $srcdir/${cf_dir}/${cf_dir}.h; then
				continue
			fi
		fi

		if test -z "$cf_cv_src_modules"; then
			cf_cv_src_modules=$cf_dir
		else
			cf_cv_src_modules="$cf_cv_src_modules $cf_dir"
		fi

		# Make the ncurses_cfg.h file record the library interface files as
		# well.  These are header files that are the same name as their
		# directory.  Ncurses is the only library that does not follow
		# that pattern.
		if test $cf_dir = tack ; then
			continue
		elif test -f $srcdir/${cf_dir}/${cf_dir}.h; then
			CF_UPPER(cf_have_include,$cf_dir)
			AC_DEFINE_UNQUOTED(HAVE_${cf_have_include}_H)
			AC_DEFINE_UNQUOTED(HAVE_LIB${cf_have_include})
			TEST_DEPS="${LIB_DIR}/${LIB_PREFIX}${cf_dir}${DFT_DEP_SUFFIX} $TEST_DEPS"
			TEST_DEP2="${LIB_2ND}/${LIB_PREFIX}${cf_dir}${DFT_DEP_SUFFIX} $TEST_DEP2"
			if test "$DFT_LWR_MODEL" = "libtool"; then
				TEST_ARGS="${TEST_DEPS}"
				TEST_ARG2="${TEST_DEP2}"
			else
				TEST_ARGS="-l${cf_dir}${DFT_ARG_SUFFIX} $TEST_ARGS"
				TEST_ARG2="-l${cf_dir}${DFT_ARG_SUFFIX} $TEST_ARG2"
			fi
			PC_MODULES_TO_MAKE="${PC_MODULES_TO_MAKE} ${cf_dir}${DFT_ARG_SUFFIX}"
		fi
	fi
done
AC_MSG_RESULT($cf_cv_src_modules)

TEST_ARGS="-L${LIB_DIR} $TEST_ARGS"
TEST_ARG2="-L${LIB_2ND} $TEST_ARG2"

AC_SUBST(TEST_ARGS)
AC_SUBST(TEST_DEPS)

AC_SUBST(TEST_ARG2)
AC_SUBST(TEST_DEP2)

SRC_SUBDIRS=
if test "x$cf_with_manpages" != xno ; then
	SRC_SUBDIRS="$SRC_SUBDIRS man"
fi
SRC_SUBDIRS="$SRC_SUBDIRS include"
for cf_dir in $cf_cv_src_modules
do
	SRC_SUBDIRS="$SRC_SUBDIRS $cf_dir"
done
if test "x$cf_with_tests" != "xno" ; then
	SRC_SUBDIRS="$SRC_SUBDIRS test"
fi
if test "x$cf_with_db_install" = xyes; then
	test -z "$MAKE_TERMINFO" && SRC_SUBDIRS="$SRC_SUBDIRS misc"
fi
if test "$cf_with_cxx_binding" != no; then
	PC_MODULES_TO_MAKE="${PC_MODULES_TO_MAKE} ncurses++${DFT_ARG_SUFFIX}"
	SRC_SUBDIRS="$SRC_SUBDIRS c++"
fi

test "x$with_termlib" != xno && PC_MODULES_TO_MAKE="$PC_MODULES_TO_MAKE $TINFO_ARG_SUFFIX"
test "x$with_ticlib" != xno && PC_MODULES_TO_MAKE="$PC_MODULES_TO_MAKE $TICS_ARG_SUFFIX"

AC_SUBST(PC_MODULES_TO_MAKE)

ADA_SUBDIRS=
if test "x$cf_with_ada" = "xyes" && test "x$cf_cv_prog_gnat_correct" = xyes && test -f $srcdir/Ada95/Makefile.in; then
	SRC_SUBDIRS="$SRC_SUBDIRS Ada95"
	ADA_SUBDIRS="gen src"
	if test "x$cf_with_tests" != "xno" ; then
		ADA_SUBDIRS="$ADA_SUBDIRS samples"
	fi
fi

SUB_MAKEFILES=
for cf_dir in $SRC_SUBDIRS
do
	SUB_MAKEFILES="$SUB_MAKEFILES $cf_dir/Makefile"
done

if test -n "$ADA_SUBDIRS"; then
	for cf_dir in $ADA_SUBDIRS
	do
		SUB_MAKEFILES="$SUB_MAKEFILES Ada95/$cf_dir/Makefile"
	done
	AC_SUBST(ADA_SUBDIRS)
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_STDCPP_LIBRARY version: 7 updated: 2010/05/29 16:31:02
dnl -----------------
dnl Check for -lstdc++, which is GNU's standard C++ library.
AC_DEFUN([CF_STDCPP_LIBRARY],
[
if test -n "$GXX" ; then
case $cf_cv_system_name in #(vi
os2*) #(vi
	cf_stdcpp_libname=stdcpp
	;;
*)
	cf_stdcpp_libname=stdc++
	;;
esac
AC_CACHE_CHECK(for library $cf_stdcpp_libname,cf_cv_libstdcpp,[
	cf_save="$LIBS"
	CF_ADD_LIB($cf_stdcpp_libname)
AC_TRY_LINK([
#include <strstream.h>],[
char buf[80];
strstreambuf foo(buf, sizeof(buf))
],
	[cf_cv_libstdcpp=yes],
	[cf_cv_libstdcpp=no])
	LIBS="$cf_save"
])
test "$cf_cv_libstdcpp" = yes && CF_ADD_LIB($cf_stdcpp_libname,CXXLIBS)
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_STRIP_G_OPT version: 3 updated: 2002/12/21 19:25:52
dnl --------------
dnl	Remove "-g" option from the compiler options
AC_DEFUN([CF_STRIP_G_OPT],
[$1=`echo ${$1} | sed -e 's%-g %%' -e 's%-g$%%'`])dnl
dnl ---------------------------------------------------------------------------
dnl CF_STRUCT_SIGACTION version: 5 updated: 2012/10/06 17:56:13
dnl -------------------
dnl Check if we need _POSIX_SOURCE defined to use struct sigaction.  We'll only
dnl do this if we've found the sigaction function.
AC_DEFUN([CF_STRUCT_SIGACTION],[
AC_REQUIRE([CF_XOPEN_SOURCE])

if test "$ac_cv_func_sigaction" = yes; then
AC_MSG_CHECKING(whether sigaction needs _POSIX_SOURCE)
AC_TRY_COMPILE([
#include <sys/types.h>
#include <signal.h>],
	[struct sigaction act],
	[sigact_bad=no],
	[
AC_TRY_COMPILE([
#define _POSIX_SOURCE
#include <sys/types.h>
#include <signal.h>],
	[struct sigaction act],
	[sigact_bad=yes
	 AC_DEFINE(_POSIX_SOURCE,1,[Define to 1 if we must define _POSIX_SOURCE])],
	 [sigact_bad=unknown])])
AC_MSG_RESULT($sigact_bad)
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_STRUCT_TERMIOS version: 7 updated: 2012/10/06 17:56:13
dnl -----------------
dnl Some machines require _POSIX_SOURCE to completely define struct termios.
AC_DEFUN([CF_STRUCT_TERMIOS],[
AC_REQUIRE([CF_XOPEN_SOURCE])

AC_CHECK_HEADERS( \
termio.h \
termios.h \
unistd.h \
)

if test "$ISC" = yes ; then
	AC_CHECK_HEADERS( sys/termio.h )
fi
if test "$ac_cv_header_termios_h" = yes ; then
	case "$CFLAGS $CPPFLAGS" in
	*-D_POSIX_SOURCE*)
		termios_bad=dunno ;;
	*)	termios_bad=maybe ;;
	esac
	if test "$termios_bad" = maybe ; then
	AC_MSG_CHECKING(whether termios.h needs _POSIX_SOURCE)
	AC_TRY_COMPILE([#include <termios.h>],
		[struct termios foo; int x = foo.c_iflag],
		termios_bad=no, [
		AC_TRY_COMPILE([
#define _POSIX_SOURCE
#include <termios.h>],
			[struct termios foo; int x = foo.c_iflag],
			termios_bad=unknown,
			termios_bad=yes AC_DEFINE(_POSIX_SOURCE,1,[Define to 1 if we must define _POSIX_SOURCE]))
			])
	AC_MSG_RESULT($termios_bad)
	fi
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_SUBDIR_PATH version: 6 updated: 2010/04/21 06:20:50
dnl --------------
dnl Construct a search-list for a nonstandard header/lib-file
dnl	$1 = the variable to return as result
dnl	$2 = the package name
dnl	$3 = the subdirectory, e.g., bin, include or lib
AC_DEFUN([CF_SUBDIR_PATH],
[
$1=

CF_ADD_SUBDIR_PATH($1,$2,$3,/usr,$prefix)
CF_ADD_SUBDIR_PATH($1,$2,$3,$prefix,NONE)
CF_ADD_SUBDIR_PATH($1,$2,$3,/usr/local,$prefix)
CF_ADD_SUBDIR_PATH($1,$2,$3,/opt,$prefix)
CF_ADD_SUBDIR_PATH($1,$2,$3,[$]HOME,$prefix)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_SUBST_IF version: 2 updated: 2006/06/17 12:33:03
dnl -----------
dnl	Shorthand macro for substituting things that the user may override
dnl	with an environment variable.
dnl
dnl	$1 = condition to pass to "test"
dnl	$2 = environment variable
dnl	$3 = value if the test succeeds
dnl	$4 = value if the test fails
AC_DEFUN([CF_SUBST_IF],
[
if test $1 ; then
	$2=$3
ifelse($4,,,[else
	$2=$4])
fi
AC_SUBST($2)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_SUBST_NCURSES_VERSION version: 8 updated: 2006/09/16 11:40:59
dnl ------------------------
dnl Get the version-number for use in shared-library naming, etc.
AC_DEFUN([CF_SUBST_NCURSES_VERSION],
[
AC_REQUIRE([CF_PROG_EGREP])
NCURSES_MAJOR="`$ac_cv_prog_egrep '^NCURSES_MAJOR[[ 	]]*=' $srcdir/dist.mk | sed -e 's/^[[^0-9]]*//'`"
NCURSES_MINOR="`$ac_cv_prog_egrep '^NCURSES_MINOR[[ 	]]*=' $srcdir/dist.mk | sed -e 's/^[[^0-9]]*//'`"
NCURSES_PATCH="`$ac_cv_prog_egrep '^NCURSES_PATCH[[ 	]]*=' $srcdir/dist.mk | sed -e 's/^[[^0-9]]*//'`"
cf_cv_abi_version=${NCURSES_MAJOR}
cf_cv_rel_version=${NCURSES_MAJOR}.${NCURSES_MINOR}
dnl Show the computed version, for logging
cf_cv_timestamp=`date`
AC_MSG_RESULT(Configuring NCURSES $cf_cv_rel_version ABI $cf_cv_abi_version ($cf_cv_timestamp))
dnl We need these values in the generated headers
AC_SUBST(NCURSES_MAJOR)
AC_SUBST(NCURSES_MINOR)
AC_SUBST(NCURSES_PATCH)
dnl We need these values in the generated makefiles
AC_SUBST(cf_cv_rel_version)
AC_SUBST(cf_cv_abi_version)
AC_SUBST(cf_cv_builtin_bool)
AC_SUBST(cf_cv_header_stdbool_h)
AC_SUBST(cf_cv_type_of_bool)dnl
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_SYS_TIME_SELECT version: 5 updated: 2012/10/04 05:24:07
dnl ------------------
dnl Check if we can include <sys/time.h> with <sys/select.h>; this breaks on
dnl older SCO configurations.
AC_DEFUN([CF_SYS_TIME_SELECT],
[
AC_MSG_CHECKING(if sys/time.h works with sys/select.h)
AC_CACHE_VAL(cf_cv_sys_time_select,[
AC_TRY_COMPILE([
#include <sys/types.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
],[],[cf_cv_sys_time_select=yes],
     [cf_cv_sys_time_select=no])
     ])
AC_MSG_RESULT($cf_cv_sys_time_select)
test "$cf_cv_sys_time_select" = yes && AC_DEFINE(HAVE_SYS_TIME_SELECT,1,[Define to 1 if we can include <sys/time.h> with <sys/select.h>])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_TOP_BUILDDIR version: 2 updated: 2013/07/27 17:38:32
dnl ---------------
dnl Define a top_builddir symbol, for applications that need an absolute path.
AC_DEFUN([CF_TOP_BUILDDIR],
[
top_builddir=ifelse($1,,`pwd`,$1)
AC_SUBST(top_builddir)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_TRY_XOPEN_SOURCE version: 1 updated: 2011/10/30 17:09:50
dnl -------------------
dnl If _XOPEN_SOURCE is not defined in the compile environment, check if we
dnl can define it successfully.
AC_DEFUN([CF_TRY_XOPEN_SOURCE],[
AC_CACHE_CHECK(if we should define _XOPEN_SOURCE,cf_cv_xopen_source,[
	AC_TRY_COMPILE([
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
],[
#ifndef _XOPEN_SOURCE
make an error
#endif],
	[cf_cv_xopen_source=no],
	[cf_save="$CPPFLAGS"
	 CPPFLAGS="$CPPFLAGS -D_XOPEN_SOURCE=$cf_XOPEN_SOURCE"
	 AC_TRY_COMPILE([
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
],[
#ifdef _XOPEN_SOURCE
make an error
#endif],
	[cf_cv_xopen_source=no],
	[cf_cv_xopen_source=$cf_XOPEN_SOURCE])
	CPPFLAGS="$cf_save"
	])
])

if test "$cf_cv_xopen_source" != no ; then
	CF_REMOVE_DEFINE(CFLAGS,$CFLAGS,_XOPEN_SOURCE)
	CF_REMOVE_DEFINE(CPPFLAGS,$CPPFLAGS,_XOPEN_SOURCE)
	cf_temp_xopen_source="-D_XOPEN_SOURCE=$cf_cv_xopen_source"
	CF_ADD_CFLAGS($cf_temp_xopen_source)
fi
])
dnl ---------------------------------------------------------------------------
dnl CF_TYPEOF_CHTYPE version: 9 updated: 2012/10/06 17:56:13
dnl ----------------
dnl Determine the type we should use for chtype (and attr_t, which is treated
dnl as the same thing).  We want around 32 bits, so on most machines want a
dnl long, but on newer 64-bit machines, probably want an int.  If we're using
dnl wide characters, we have to have a type compatible with that, as well.
AC_DEFUN([CF_TYPEOF_CHTYPE],
[
AC_MSG_CHECKING([for type of chtype])
AC_CACHE_VAL(cf_cv_typeof_chtype,[
		AC_TRY_RUN([
#define WANT_BITS 31
#include <stdio.h>
int main()
{
	FILE *fp = fopen("cf_test.out", "w");
	if (fp != 0) {
		char *result = "long";
		if (sizeof(unsigned long) > sizeof(unsigned int)) {
			int n;
			unsigned int x, y;
			for (n = 0; n < WANT_BITS; n++) {
				x = (1 << n);
				y = (x >> n);
				if (y != 1 || x == 0) {
					x = 0;
					break;
				}
			}
			/*
			 * If x is nonzero, an int is big enough for the bits
			 * that we want.
			 */
			result = (x != 0) ? "int" : "long";
		}
		fputs(result, fp);
		fclose(fp);
	}
	${cf_cv_main_return:-return}(0);
}
		],
		[cf_cv_typeof_chtype=`cat cf_test.out`],
		[cf_cv_typeof_chtype=long],
		[cf_cv_typeof_chtype=long])
		rm -f cf_test.out
	])
AC_MSG_RESULT($cf_cv_typeof_chtype)

AC_SUBST(cf_cv_typeof_chtype)
AC_DEFINE_UNQUOTED(TYPEOF_CHTYPE,$cf_cv_typeof_chtype,[Define to actual type if needed for chtype])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_TYPE_SIGACTION version: 4 updated: 2012/10/06 17:56:13
dnl -----------------
dnl
AC_DEFUN([CF_TYPE_SIGACTION],
[
AC_MSG_CHECKING([for type sigaction_t])
AC_CACHE_VAL(cf_cv_type_sigaction,[
	AC_TRY_COMPILE([
#include <signal.h>],
		[sigaction_t x],
		[cf_cv_type_sigaction=yes],
		[cf_cv_type_sigaction=no])])
AC_MSG_RESULT($cf_cv_type_sigaction)
test "$cf_cv_type_sigaction" = yes && AC_DEFINE(HAVE_TYPE_SIGACTION,1,[Define to 1 if we have the sigaction_t type])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_UNSIGNED_LITERALS version: 2 updated: 1998/02/07 22:10:16
dnl --------------------
dnl Test if the compiler supports 'U' and 'L' suffixes.  Only old compilers
dnl won't, but they're still there.
AC_DEFUN([CF_UNSIGNED_LITERALS],
[
AC_MSG_CHECKING([if unsigned literals are legal])
AC_CACHE_VAL(cf_cv_unsigned_literals,[
	AC_TRY_COMPILE([],[long x = 1L + 1UL + 1U + 1],
		[cf_cv_unsigned_literals=yes],
		[cf_cv_unsigned_literals=no])
	])
AC_MSG_RESULT($cf_cv_unsigned_literals)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_UPPER version: 5 updated: 2001/01/29 23:40:59
dnl --------
dnl Make an uppercase version of a variable
dnl $1=uppercase($2)
AC_DEFUN([CF_UPPER],
[
$1=`echo "$2" | sed y%abcdefghijklmnopqrstuvwxyz./-%ABCDEFGHIJKLMNOPQRSTUVWXYZ___%`
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_UTF8_LIB version: 8 updated: 2012/10/06 08:57:51
dnl -----------
dnl Check for multibyte support, and if not found, utf8 compatibility library
AC_DEFUN([CF_UTF8_LIB],
[
AC_CACHE_CHECK(for multibyte character support,cf_cv_utf8_lib,[
	cf_save_LIBS="$LIBS"
	AC_TRY_LINK([
#include <stdlib.h>],[putwc(0,0);],
	[cf_cv_utf8_lib=yes],
	[CF_FIND_LINKAGE([
#include <libutf8.h>],[putwc(0,0);],utf8,
		[cf_cv_utf8_lib=add-on],
		[cf_cv_utf8_lib=no])
])])

# HAVE_LIBUTF8_H is used by ncurses if curses.h is shared between
# ncurses/ncursesw:
if test "$cf_cv_utf8_lib" = "add-on" ; then
	AC_DEFINE(HAVE_LIBUTF8_H,1,[Define to 1 if we should include libutf8.h])
	CF_ADD_INCDIR($cf_cv_header_path_utf8)
	CF_ADD_LIBDIR($cf_cv_library_path_utf8)
	CF_ADD_LIBS($cf_cv_library_file_utf8)
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_VA_COPY version: 3 updated: 2012/10/06 11:17:15
dnl ----------
dnl check for va_copy, part of stdarg.h
dnl Also, workaround for glibc's __va_copy, by checking for both.
AC_DEFUN([CF_VA_COPY],[
AC_CACHE_CHECK(for va_copy, cf_cv_have_va_copy,[
AC_TRY_LINK([
#include <stdarg.h>
],[
	static va_list dst;
	static va_list src;
	va_copy(dst, src)],
	cf_cv_have_va_copy=yes,
	cf_cv_have_va_copy=no)])

test "$cf_cv_have_va_copy" = yes && AC_DEFINE(HAVE_VA_COPY,1,[Define to 1 if we have va_copy])

AC_CACHE_CHECK(for __va_copy, cf_cv_have___va_copy,[
AC_TRY_LINK([
#include <stdarg.h>
],[
	static va_list dst;
	static va_list src;
	__va_copy(dst, src)],
	cf_cv_have___va_copy=yes,
	cf_cv_have___va_copy=no)])

test "$cf_cv_have___va_copy" = yes && AC_DEFINE(HAVE___VA_COPY,1,[Define to 1 if we have __va_copy])
])
dnl ---------------------------------------------------------------------------
dnl CF_VERBOSE version: 3 updated: 2007/07/29 09:55:12
dnl ----------
dnl Use AC_VERBOSE w/o the warnings
AC_DEFUN([CF_VERBOSE],
[test -n "$verbose" && echo "	$1" 1>&AC_FD_MSG
CF_MSG_LOG([$1])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WCHAR_TYPE version: 4 updated: 2012/10/06 16:39:58
dnl -------------
dnl Check if type wide-character type $1 is declared, and if so, which header
dnl file is needed.  The second parameter is used to set a shell variable when
dnl the type is not found.  The first parameter sets a shell variable for the
dnl opposite sense.
AC_DEFUN([CF_WCHAR_TYPE],
[
# This is needed on Tru64 5.0 to declare $1
AC_CACHE_CHECK(if we must include wchar.h to declare $1,cf_cv_$1,[
AC_TRY_COMPILE([
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef HAVE_LIBUTF8_H
#include <libutf8.h>
#endif],
	[$1 state],
	[cf_cv_$1=no],
	[AC_TRY_COMPILE([
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>
#ifdef HAVE_LIBUTF8_H
#include <libutf8.h>
#endif],
	[$1 value],
	[cf_cv_$1=yes],
	[cf_cv_$1=unknown])])])

if test "$cf_cv_$1" = yes ; then
	AC_DEFINE(NEED_WCHAR_H,1,[Define to 1 if we must include wchar.h])
	NEED_WCHAR_H=1
fi

ifelse([$2],,,[
# if we do not find $1 in either place, use substitution to provide a fallback.
if test "$cf_cv_$1" = unknown ; then
	$2=1
fi
])
ifelse($3,,,[
# if we find $1 in either place, use substitution to provide a fallback.
if test "$cf_cv_$1" != unknown ; then
	$3=1
fi
])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WEAK_SYMBOLS version: 1 updated: 2008/08/16 19:18:06
dnl ---------------
dnl Check for compiler-support for weak symbols.
dnl This works with "recent" gcc.
AC_DEFUN([CF_WEAK_SYMBOLS],[
AC_CACHE_CHECK(if $CC supports weak symbols,cf_cv_weak_symbols,[

AC_TRY_COMPILE([
#include <stdio.h>],
[
#if defined(__GNUC__)
#  if defined __USE_ISOC99
#    define _cat_pragma(exp)	_Pragma(#exp)
#    define _weak_pragma(exp)	_cat_pragma(weak name)
#  else
#    define _weak_pragma(exp)
#  endif
#  define _declare(name)	__extension__ extern __typeof__(name) name
#  define weak_symbol(name)	_weak_pragma(name) _declare(name) __attribute__((weak))
#endif

weak_symbol(fopen);
],[cf_cv_weak_symbols=yes],[cf_cv_weak_symbols=no])
])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_ABI_VERSION version: 1 updated: 2003/09/20 18:12:49
dnl -------------------
dnl Allow library's ABI to be overridden.  Generally this happens when a
dnl packager has incremented the ABI past that used in the original package,
dnl and wishes to keep doing this.
dnl
dnl $1 is the package name, if any, to derive a corresponding {package}_ABI
dnl symbol.
AC_DEFUN([CF_WITH_ABI_VERSION],[
test -z "$cf_cv_abi_version" && cf_cv_abi_version=0
AC_ARG_WITH(abi-version,
[  --with-abi-version=XXX  override derived ABI version],
[AC_MSG_WARN(overriding ABI version $cf_cv_abi_version to $withval)
 cf_cv_abi_version=$withval])
 CF_NUMBER_SYNTAX($cf_cv_abi_version,ABI version)
ifelse($1,,,[
$1_ABI=$cf_cv_abi_version
])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_ADA_COMPILER version: 2 updated: 2010/06/26 17:35:58
dnl --------------------
dnl Command-line option to specify the Ada95 compiler.
AC_DEFUN([CF_WITH_ADA_COMPILER],[
AC_MSG_CHECKING(for ada-compiler)
AC_ARG_WITH(ada-compiler,
	[  --with-ada-compiler=CMD specify Ada95 compiler command (default gnatmake)],
	[cf_ada_compiler=$withval],
	[cf_ada_compiler=gnatmake])
AC_SUBST(cf_ada_compiler)
AC_MSG_RESULT($cf_ada_compiler)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_ADA_INCLUDE version: 2 updated: 2010/06/26 17:35:58
dnl -------------------
dnl Command-line option to specify where Ada includes will install.
AC_DEFUN([CF_WITH_ADA_INCLUDE],[
AC_MSG_CHECKING(for ada-include)
CF_WITH_PATH(ada-include,
   [  --with-ada-include=DIR  Ada includes are in DIR],
   ADA_INCLUDE,
   PREFIX/share/ada/adainclude,
   [$]prefix/share/ada/adainclude)
AC_SUBST(ADA_INCLUDE)
AC_MSG_RESULT($ADA_INCLUDE)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_ADA_OBJECTS version: 2 updated: 2010/06/26 17:35:58
dnl -------------------
dnl Command-line option to specify where Ada objects will install.
AC_DEFUN([CF_WITH_ADA_OBJECTS],[
AC_MSG_CHECKING(for ada-objects)
CF_WITH_PATH(ada-objects,
   [  --with-ada-objects=DIR  Ada objects are in DIR],
   ADA_OBJECTS,
   PREFIX/lib/ada/adalib,
   [$]prefix/lib/ada/adalib)
AC_SUBST(ADA_OBJECTS)
AC_MSG_RESULT($ADA_OBJECTS)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_ADA_SHAREDLIB version: 2 updated: 2010/06/26 17:35:58
dnl ---------------------
dnl Command-line option to specify if an Ada95 shared-library should be built,
dnl and optionally what its soname should be.
AC_DEFUN([CF_WITH_ADA_SHAREDLIB],[
AC_MSG_CHECKING(if an Ada95 shared-library should be built)
AC_ARG_WITH(ada-sharedlib,
	[  --with-ada-sharedlib=XX build Ada95 shared-library],
	[with_ada_sharedlib=$withval],
	[with_ada_sharedlib=no])
AC_MSG_RESULT($with_ada_sharedlib)

ADA_SHAREDLIB='lib$(LIB_NAME).so.1'
MAKE_ADA_SHAREDLIB="#"

if test "x$with_ada_sharedlib" != xno
then
	MAKE_ADA_SHAREDLIB=
	if test "x$with_ada_sharedlib" != xyes
	then
		ADA_SHAREDLIB="$with_ada_sharedlib"
	fi
fi

AC_SUBST(ADA_SHAREDLIB)
AC_SUBST(MAKE_ADA_SHAREDLIB)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_DBMALLOC version: 7 updated: 2010/06/21 17:26:47
dnl ----------------
dnl Configure-option for dbmalloc.  The optional parameter is used to override
dnl the updating of $LIBS, e.g., to avoid conflict with subsequent tests.
AC_DEFUN([CF_WITH_DBMALLOC],[
CF_NO_LEAKS_OPTION(dbmalloc,
	[  --with-dbmalloc         test: use Conor Cahill's dbmalloc library],
	[USE_DBMALLOC])

if test "$with_dbmalloc" = yes ; then
	AC_CHECK_HEADER(dbmalloc.h,
		[AC_CHECK_LIB(dbmalloc,[debug_malloc]ifelse([$1],,[],[,$1]))])
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_DMALLOC version: 7 updated: 2010/06/21 17:26:47
dnl ---------------
dnl Configure-option for dmalloc.  The optional parameter is used to override
dnl the updating of $LIBS, e.g., to avoid conflict with subsequent tests.
AC_DEFUN([CF_WITH_DMALLOC],[
CF_NO_LEAKS_OPTION(dmalloc,
	[  --with-dmalloc          test: use Gray Watson's dmalloc library],
	[USE_DMALLOC])

if test "$with_dmalloc" = yes ; then
	AC_CHECK_HEADER(dmalloc.h,
		[AC_CHECK_LIB(dmalloc,[dmalloc_debug]ifelse([$1],,[],[,$1]))])
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_GPM version: 8 updated: 2012/10/06 17:56:13
dnl -----------
dnl
dnl The option parameter (if neither yes/no) is assumed to be the name of
dnl the gpm library, e.g., for dynamic loading.
AC_DEFUN([CF_WITH_GPM],
[
AC_MSG_CHECKING(if you want to link with the GPM mouse library)
AC_ARG_WITH(gpm,
	[  --with-gpm              use Alessandro Rubini's GPM library],
	[with_gpm=$withval],
	[with_gpm=maybe])
AC_MSG_RESULT($with_gpm)

if test "$with_gpm" != no ; then
	AC_CHECK_HEADER(gpm.h,[
		AC_DEFINE(HAVE_GPM_H,1,[Define to 1 if we have gpm.h header])
		if test "$with_gpm" != yes && test "$with_gpm" != maybe ; then
			CF_VERBOSE(assuming we really have GPM library)
			AC_DEFINE(HAVE_LIBGPM,1,[Define to 1 if we have the gpm library])
		else
			AC_CHECK_LIB(gpm,Gpm_Open,[:],[
				AC_MSG_ERROR(Cannot link with GPM library)
		fi
		with_gpm=yes
		])
	],[
		test "$with_gpm" != maybe && AC_MSG_WARN(Cannot find GPM header)
		with_gpm=no
	])
fi
])
dnl ---------------------------------------------------------------------------
dnl CF_WITH_LIBTOOL version: 30 updated: 2013/09/07 13:54:05
dnl ---------------
dnl Provide a configure option to incorporate libtool.  Define several useful
dnl symbols for the makefile rules.
dnl
dnl The reference to AC_PROG_LIBTOOL does not normally work, since it uses
dnl macros from libtool.m4 which is in the aclocal directory of automake.
dnl Following is a simple script which turns on the AC_PROG_LIBTOOL macro.
dnl But that still does not work properly since the macro is expanded outside
dnl the CF_WITH_LIBTOOL macro:
dnl
dnl	#!/bin/sh
dnl	ACLOCAL=`aclocal --print-ac-dir`
dnl	if test -z "$ACLOCAL" ; then
dnl		echo cannot find aclocal directory
dnl		exit 1
dnl	elif test ! -f $ACLOCAL/libtool.m4 ; then
dnl		echo cannot find libtool.m4 file
dnl		exit 1
dnl	fi
dnl
dnl	LOCAL=aclocal.m4
dnl	ORIG=aclocal.m4.orig
dnl
dnl	trap "mv $ORIG $LOCAL" 0 1 2 5 15
dnl	rm -f $ORIG
dnl	mv $LOCAL $ORIG
dnl
dnl	# sed the LIBTOOL= assignment to omit the current directory?
dnl	sed -e 's/^LIBTOOL=.*/LIBTOOL=${LIBTOOL:-libtool}/' $ACLOCAL/libtool.m4 >>$LOCAL
dnl	cat $ORIG >>$LOCAL
dnl
dnl	autoconf-257 $*
dnl
AC_DEFUN([CF_WITH_LIBTOOL],
[
AC_REQUIRE([CF_DISABLE_LIBTOOL_VERSION])
ifdef([AC_PROG_LIBTOOL],,[
LIBTOOL=
])
# common library maintenance symbols that are convenient for libtool scripts:
LIB_CREATE='${AR} -cr'
LIB_OBJECT='${OBJECTS}'
LIB_SUFFIX=.a
LIB_PREP="$RANLIB"

# symbols used to prop libtool up to enable it to determine what it should be
# doing:
LIB_CLEAN=
LIB_COMPILE=
LIB_LINK='${CC}'
LIB_INSTALL=
LIB_UNINSTALL=

AC_MSG_CHECKING(if you want to build libraries with libtool)
AC_ARG_WITH(libtool,
	[  --with-libtool          generate libraries with libtool],
	[with_libtool=$withval],
	[with_libtool=no])
AC_MSG_RESULT($with_libtool)
if test "$with_libtool" != "no"; then
ifdef([AC_PROG_LIBTOOL],[
	# missing_content_AC_PROG_LIBTOOL{{
	AC_PROG_LIBTOOL
	# missing_content_AC_PROG_LIBTOOL}}
],[
	if test "$with_libtool" != "yes" ; then
		CF_PATH_SYNTAX(with_libtool)
		LIBTOOL=$with_libtool
	else
		AC_CHECK_TOOLS(LIBTOOL,[libtool glibtool],none)
		CF_LIBTOOL_VERSION
		if test -z "$cf_cv_libtool_version" && test "$LIBTOOL" = libtool
		then
			CF_FORGET_TOOL(LIBTOOL)
			AC_CHECK_TOOLS(LIBTOOL,[glibtool],none)
			CF_LIBTOOL_VERSION
		fi
	fi
	if test -z "$LIBTOOL" ; then
		AC_MSG_ERROR(Cannot find libtool)
	fi
])dnl
	LIB_CREATE='${LIBTOOL} --mode=link ${CC} -rpath ${DESTDIR}${libdir} ${LIBTOOL_VERSION} `cut -f1 ${srcdir}/VERSION` ${LIBTOOL_OPTS} ${LT_UNDEF} $(LIBS) -o'
	LIB_OBJECT='${OBJECTS:.o=.lo}'
	LIB_SUFFIX=.la
	LIB_CLEAN='${LIBTOOL} --mode=clean'
	LIB_COMPILE='${LIBTOOL} --mode=compile'
	LIB_LINK='${LIBTOOL} --mode=link ${CC} ${LIBTOOL_OPTS}'
	LIB_INSTALL='${LIBTOOL} --mode=install'
	LIB_UNINSTALL='${LIBTOOL} --mode=uninstall'
	LIB_PREP=:

	CF_CHECK_LIBTOOL_VERSION

	# special hack to add -no-undefined (which libtool should do for itself)
	LT_UNDEF=
	case "$cf_cv_system_name" in #(vi
	cygwin*|msys*|mingw32*|uwin*|aix[[4-7]]) #(vi
		LT_UNDEF=-no-undefined
		;;
	esac
	AC_SUBST([LT_UNDEF])

	# special hack to add --tag option for C++ compiler
	case $cf_cv_libtool_version in #(vi
	1.[[5-9]]*|[[2-9]].[[0-9.a-z]]*) #(vi
		LIBTOOL_CXX="$LIBTOOL --tag=CXX"
		LIBTOOL="$LIBTOOL --tag=CC"
		;;
	*)
		LIBTOOL_CXX="$LIBTOOL"
		;;
	esac
else
	LIBTOOL=""
	LIBTOOL_CXX=""
fi

test -z "$LIBTOOL" && ECHO_LT=

AC_SUBST(LIBTOOL)
AC_SUBST(LIBTOOL_CXX)
AC_SUBST(LIBTOOL_OPTS)

AC_SUBST(LIB_CREATE)
AC_SUBST(LIB_OBJECT)
AC_SUBST(LIB_SUFFIX)
AC_SUBST(LIB_PREP)

AC_SUBST(LIB_CLEAN)
AC_SUBST(LIB_COMPILE)
AC_SUBST(LIB_LINK)
AC_SUBST(LIB_INSTALL)
AC_SUBST(LIB_UNINSTALL)

])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_LIB_PREFIX version: 1 updated: 2012/01/21 19:28:10
dnl ------------------
dnl Allow the library-prefix to be overridden.  OS/2 EMX originally had no
dnl "lib" prefix, e.g., because it used the dll naming convention.
dnl
dnl $1 = variable to set
AC_DEFUN([CF_WITH_LIB_PREFIX],
[
AC_MSG_CHECKING(if you want to have a library-prefix)
AC_ARG_WITH(lib-prefix,
	[  --with-lib-prefix       override library-prefix],
	[with_lib_prefix=$withval],
	[with_lib_prefix=auto])
AC_MSG_RESULT($with_lib_prefix)

if test $with_lib_prefix = auto
then
	CF_LIB_PREFIX($1)
elif test $with_lib_prefix = no
then
	LIB_PREFIX=
else
	LIB_PREFIX=$with_lib_prefix
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_PATH version: 11 updated: 2012/09/29 15:04:19
dnl ------------
dnl Wrapper for AC_ARG_WITH to ensure that user supplies a pathname, not just
dnl defaulting to yes/no.
dnl
dnl $1 = option name
dnl $2 = help-text
dnl $3 = environment variable to set
dnl $4 = default value, shown in the help-message, must be a constant
dnl $5 = default value, if it's an expression & cannot be in the help-message
dnl
AC_DEFUN([CF_WITH_PATH],
[AC_ARG_WITH($1,[$2 ](default: ifelse([$4],,empty,[$4])),,
ifelse([$4],,[withval="${$3}"],[withval="${$3:-ifelse([$5],,[$4],[$5])}"]))dnl
if ifelse([$5],,true,[test -n "$5"]) ; then
CF_PATH_SYNTAX(withval)
fi
eval $3="$withval"
AC_SUBST($3)dnl
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_PATHLIST version: 9 updated: 2012/10/18 05:05:24
dnl ----------------
dnl Process an option specifying a list of colon-separated paths.
dnl
dnl $1 = option name
dnl $2 = help-text
dnl $3 = environment variable to set
dnl $4 = default value, shown in the help-message, must be a constant
dnl $5 = default value, if it's an expression & cannot be in the help-message
dnl $6 = flag to tell if we want to define or substitute
dnl
AC_DEFUN([CF_WITH_PATHLIST],[
AC_REQUIRE([CF_PATHSEP])
AC_ARG_WITH($1,[$2 ](default: ifelse($4,,empty,$4)),,
ifelse($4,,[withval=${$3}],[withval=${$3:-ifelse($5,,$4,$5)}]))dnl

IFS="${IFS:- 	}"; ac_save_ifs="$IFS"; IFS="${PATH_SEPARATOR}"
cf_dst_path=
for cf_src_path in $withval
do
  CF_PATH_SYNTAX(cf_src_path)
  test -n "$cf_dst_path" && cf_dst_path="${cf_dst_path}$PATH_SEPARATOR"
  cf_dst_path="${cf_dst_path}${cf_src_path}"
done
IFS="$ac_save_ifs"

ifelse($6,define,[
# Strip single quotes from the value, e.g., when it was supplied as a literal
# for $4 or $5.
case $cf_dst_path in #(vi
\'*)
  cf_dst_path=`echo $cf_dst_path |sed -e s/\'// -e s/\'\$//`
  ;;
esac
cf_dst_path=`echo "$cf_dst_path" | sed -e 's/\\\\/\\\\\\\\/g'`
])

# This may use the prefix/exec_prefix symbols which will only yield "NONE"
# so we have to check/work around.  We do prefer the result of "eval"...
eval cf_dst_eval="$cf_dst_path"
case "x$cf_dst_eval" in #(vi
xNONE*) #(vi
	$3=$cf_dst_path
	;;
*)
	$3="$cf_dst_eval"
	;;
esac
AC_SUBST($3)dnl

])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_PKG_CONFIG_LIBDIR version: 2 updated: 2011/12/10 18:58:47
dnl -------------------------
dnl Allow the choice of the pkg-config library directory to be overridden.
AC_DEFUN([CF_WITH_PKG_CONFIG_LIBDIR],[
if test "$PKG_CONFIG" != none ; then
	AC_MSG_CHECKING(for $PKG_CONFIG library directory)
	AC_ARG_WITH(pkg-config-libdir,
		[  --with-pkg-config-libdir=XXX use given directory for installing pc-files],
		[PKG_CONFIG_LIBDIR=$withval],
		[PKG_CONFIG_LIBDIR=yes])

	case x$PKG_CONFIG_LIBDIR in #(vi
	x/*) #(vi
		;;
	xyes) #(vi
		# look for the library directory using the same prefix as the executable
		cf_path=`echo "$PKG_CONFIG" | sed -e 's,/[[^/]]*/[[^/]]*$,,'`
		case x`(arch) 2>/dev/null` in #(vi
		*64) #(vi
			for cf_config in $cf_path/share $cf_path/lib64 $cf_path/lib32 $cf_path/lib
			do
				if test -d $cf_config/pkgconfig
				then
					PKG_CONFIG_LIBDIR=$cf_config/pkgconfig
					break
				fi
			done
			;;
		*)
			PKG_CONFIG_LIBDIR=$cf_path/lib/pkgconfig
			;;
		esac
		;;
	*)
		;;
	esac

	AC_MSG_RESULT($PKG_CONFIG_LIBDIR)
fi

AC_SUBST(PKG_CONFIG_LIBDIR)
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_PTHREAD version: 6 updated: 2012/10/06 17:41:51
dnl ---------------
dnl Check for POSIX thread library.
AC_DEFUN([CF_WITH_PTHREAD],
[
AC_MSG_CHECKING(if you want to link with the pthread library)
AC_ARG_WITH(pthread,
    [  --with-pthread          use POSIX thread library],
    [with_pthread=$withval],
    [with_pthread=no])
AC_MSG_RESULT($with_pthread)

if test "$with_pthread" != no ; then
    AC_CHECK_HEADER(pthread.h,[
        AC_DEFINE(HAVE_PTHREADS_H,1,[Define to 1 if we have pthreads.h header])

	for cf_lib_pthread in pthread c_r
	do
	    AC_MSG_CHECKING(if we can link with the $cf_lib_pthread library)
	    cf_save_LIBS="$LIBS"
	    CF_ADD_LIB($cf_lib_pthread)
	    AC_TRY_LINK([
#include <pthread.h>
],[
		int rc = pthread_create(0,0,0,0);
		int r2 = pthread_mutexattr_settype(0, 0);
],[with_pthread=yes],[with_pthread=no])
	    LIBS="$cf_save_LIBS"
	    AC_MSG_RESULT($with_pthread)
	    test "$with_pthread" = yes && break
	done

	if test "$with_pthread" = yes ; then
	    CF_ADD_LIB($cf_lib_pthread)
	    AC_DEFINE(HAVE_LIBPTHREADS,1,[Define to 1 if we have pthreads library])
	else
	    AC_MSG_ERROR(Cannot link with pthread library)
	fi
    ])
fi
])
dnl ---------------------------------------------------------------------------
dnl CF_WITH_REL_VERSION version: 1 updated: 2003/09/20 18:12:49
dnl -------------------
dnl Allow library's release-version to be overridden.  Generally this happens when a
dnl packager has incremented the release-version past that used in the original package,
dnl and wishes to keep doing this.
dnl
dnl $1 is the package name, if any, to derive corresponding {package}_MAJOR
dnl and {package}_MINOR symbols
dnl symbol.
AC_DEFUN([CF_WITH_REL_VERSION],[
test -z "$cf_cv_rel_version" && cf_cv_rel_version=0.0
AC_ARG_WITH(rel-version,
[  --with-rel-version=XXX  override derived release version],
[AC_MSG_WARN(overriding release version $cf_cv_rel_version to $withval)
 cf_cv_rel_version=$withval])
ifelse($1,,[
 CF_NUMBER_SYNTAX($cf_cv_rel_version,Release version)
],[
 $1_MAJOR=`echo "$cf_cv_rel_version" | sed -e 's/\..*//'`
 $1_MINOR=`echo "$cf_cv_rel_version" | sed -e 's/^[[^.]]*//' -e 's/^\.//' -e 's/\..*//'`
 CF_NUMBER_SYNTAX([$]$1_MAJOR,Release major-version)
 CF_NUMBER_SYNTAX([$]$1_MINOR,Release minor-version)
])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_SYSMOUSE version: 3 updated: 2012/10/06 17:56:13
dnl ----------------
dnl If we can compile with sysmouse, make it available unless it is not wanted.
AC_DEFUN([CF_WITH_SYSMOUSE],[
# not everyone has "test -c"
if test -c /dev/sysmouse 2>/dev/null ; then
AC_MSG_CHECKING(if you want to use sysmouse)
AC_ARG_WITH(sysmouse,
	[  --with-sysmouse         use sysmouse (FreeBSD console)],
	[cf_with_sysmouse=$withval],
	[cf_with_sysmouse=maybe])
	if test "$cf_with_sysmouse" != no ; then
	AC_TRY_COMPILE([
#include <osreldate.h>
#if (__FreeBSD_version >= 400017)
#include <sys/consio.h>
#include <sys/fbio.h>
#else
#include <machine/console.h>
#endif
],[
	struct mouse_info the_mouse;
	ioctl(0, CONS_MOUSECTL, &the_mouse);
],[cf_with_sysmouse=yes],[cf_with_sysmouse=no])
	fi
AC_MSG_RESULT($cf_with_sysmouse)
test "$cf_with_sysmouse" = yes && AC_DEFINE(USE_SYSMOUSE,1,[Define to 1 if we can/should use the sysmouse interface])
fi
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_SYSTYPE version: 1 updated: 2013/01/26 16:26:12
dnl ---------------
dnl For testing, override the derived host system-type which is used to decide
dnl things such as the linker commands used to build shared libraries.  This is
dnl normally chosen automatically based on the type of system which you are
dnl building on.  We use it for testing the configure script.
dnl
dnl This is different from the --host option: it is used only for testing parts
dnl of the configure script which would not be reachable with --host since that
dnl relies on the build environment being real, rather than mocked up.
AC_DEFUN([CF_WITH_SYSTYPE],[
CF_CHECK_CACHE([AC_CANONICAL_SYSTEM])
AC_ARG_WITH(system-type,
	[  --with-system-type=XXX  test: override derived host system-type],
[AC_MSG_WARN(overriding system type to $withval)
	cf_cv_system_name=$withval
	host_os=$withval
])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_WITH_VALGRIND version: 1 updated: 2006/12/14 18:00:21
dnl ----------------
AC_DEFUN([CF_WITH_VALGRIND],[
CF_NO_LEAKS_OPTION(valgrind,
	[  --with-valgrind         test: use valgrind],
	[USE_VALGRIND])
])dnl
dnl ---------------------------------------------------------------------------
dnl CF_XOPEN_SOURCE version: 46 updated: 2014/02/09 19:30:15
dnl ---------------
dnl Try to get _XOPEN_SOURCE defined properly that we can use POSIX functions,
dnl or adapt to the vendor's definitions to get equivalent functionality,
dnl without losing the common non-POSIX features.
dnl
dnl Parameters:
dnl	$1 is the nominal value for _XOPEN_SOURCE
dnl	$2 is the nominal value for _POSIX_C_SOURCE
AC_DEFUN([CF_XOPEN_SOURCE],[
AC_REQUIRE([AC_CANONICAL_HOST])

cf_XOPEN_SOURCE=ifelse([$1],,500,[$1])
cf_POSIX_C_SOURCE=ifelse([$2],,199506L,[$2])
cf_xopen_source=

case $host_os in #(vi
aix[[4-7]]*) #(vi
	cf_xopen_source="-D_ALL_SOURCE"
	;;
cygwin|msys) #(vi
	cf_XOPEN_SOURCE=600
	;;
darwin[[0-8]].*) #(vi
	cf_xopen_source="-D_APPLE_C_SOURCE"
	;;
darwin*) #(vi
	cf_xopen_source="-D_DARWIN_C_SOURCE"
	cf_XOPEN_SOURCE=
	;;
freebsd*|dragonfly*) #(vi
	# 5.x headers associate
	#	_XOPEN_SOURCE=600 with _POSIX_C_SOURCE=200112L
	#	_XOPEN_SOURCE=500 with _POSIX_C_SOURCE=199506L
	cf_POSIX_C_SOURCE=200112L
	cf_XOPEN_SOURCE=600
	cf_xopen_source="-D_BSD_TYPES -D__BSD_VISIBLE -D_POSIX_C_SOURCE=$cf_POSIX_C_SOURCE -D_XOPEN_SOURCE=$cf_XOPEN_SOURCE"
	;;
hpux11*) #(vi
	cf_xopen_source="-D_HPUX_SOURCE -D_XOPEN_SOURCE=500"
	;;
hpux*) #(vi
	cf_xopen_source="-D_HPUX_SOURCE"
	;;
irix[[56]].*) #(vi
	cf_xopen_source="-D_SGI_SOURCE"
	cf_XOPEN_SOURCE=
	;;
linux*|gnu*|mint*|k*bsd*-gnu) #(vi
	CF_GNU_SOURCE
	;;
mirbsd*) #(vi
	# setting _XOPEN_SOURCE or _POSIX_SOURCE breaks <sys/select.h> and other headers which use u_int / u_short types
	cf_XOPEN_SOURCE=
	CF_POSIX_C_SOURCE($cf_POSIX_C_SOURCE)
	;;
netbsd*) #(vi
	cf_xopen_source="-D_NETBSD_SOURCE" # setting _XOPEN_SOURCE breaks IPv6 for lynx on NetBSD 1.6, breaks xterm, is not needed for ncursesw
	;;
openbsd[[4-9]]*) #(vi
	# setting _XOPEN_SOURCE lower than 500 breaks g++ compile with wchar.h, needed for ncursesw
	cf_xopen_source="-D_BSD_SOURCE"
	cf_XOPEN_SOURCE=600
	;;
openbsd*) #(vi
	# setting _XOPEN_SOURCE breaks xterm on OpenBSD 2.8, is not needed for ncursesw
	;;
osf[[45]]*) #(vi
	cf_xopen_source="-D_OSF_SOURCE"
	;;
nto-qnx*) #(vi
	cf_xopen_source="-D_QNX_SOURCE"
	;;
sco*) #(vi
	# setting _XOPEN_SOURCE breaks Lynx on SCO Unix / OpenServer
	;;
solaris2.*) #(vi
	cf_xopen_source="-D__EXTENSIONS__"
	cf_cv_xopen_source=broken
	;;
*)
	CF_TRY_XOPEN_SOURCE
	CF_POSIX_C_SOURCE($cf_POSIX_C_SOURCE)
	;;
esac

if test -n "$cf_xopen_source" ; then
	CF_ADD_CFLAGS($cf_xopen_source)
fi

dnl In anything but the default case, we may have system-specific setting
dnl which is still not guaranteed to provide all of the entrypoints that
dnl _XOPEN_SOURCE would yield.
if test -n "$cf_XOPEN_SOURCE" && test -z "$cf_cv_xopen_source" ; then
	AC_MSG_CHECKING(if _XOPEN_SOURCE really is set)
	AC_TRY_COMPILE([#include <stdlib.h>],[
#ifndef _XOPEN_SOURCE
make an error
#endif],
	[cf_XOPEN_SOURCE_set=yes],
	[cf_XOPEN_SOURCE_set=no])
	AC_MSG_RESULT($cf_XOPEN_SOURCE_set)
	if test $cf_XOPEN_SOURCE_set = yes
	then
		AC_TRY_COMPILE([#include <stdlib.h>],[
#if (_XOPEN_SOURCE - 0) < $cf_XOPEN_SOURCE
make an error
#endif],
		[cf_XOPEN_SOURCE_set_ok=yes],
		[cf_XOPEN_SOURCE_set_ok=no])
		if test $cf_XOPEN_SOURCE_set_ok = no
		then
			AC_MSG_WARN(_XOPEN_SOURCE is lower than requested)
		fi
	else
		CF_TRY_XOPEN_SOURCE
	fi
fi
])
